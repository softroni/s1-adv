#pragma once
// Hot air balloon: blow on an ENV III to heat the balloon and steer with a Joystick 2. The
// joystick's button or a key also fires the burner, so it still plays with no sensors.
// No heap, recursion, storage or network, and nothing here touches hardware.
#include <stdint.h>
#include "air_music.h"

namespace balloon {
// Positions and speeds are in 1/256ths of a pixel, so slow movement stays smooth.
constexpr int32_t kOne = 256;
constexpr int kGroundY = 122, kBasketBelow = 24, kRestY = kGroundY - kBasketBelow, kTopY = 16;
constexpr int kLeftX = 16, kRightX = 176, kStartX = 60;
constexpr uint32_t kStepMs = 33;
constexpr int kLives = 3, kThings = 6, kStarsPerLevel = 10, kReach = 18;
constexpr int kOverSteps = 90, kHurtSteps = 45, kSpawnSteps = 40;

// Heat holds the balloon up, and about a third of the full amount hovers. It takes over
// five seconds to cool, so one good breath lasts a while.
constexpr int32_t kMaxHeat = 1000, kCooling = 6, kGravity = 10, kLift = 30;
constexpr int32_t kBurnerHeat = 30, kKeyHeat = 250;
constexpr int32_t kStartScroll = 256, kTopScroll = 512, kScrollStep = 26;
constexpr int32_t kSteerSpeed = 512, kNudgeSpeed = 384;
constexpr int kStickDead = 16, kStickFull = 127, kNudgeSteps = 10;

// Breath is wet, so a quick rise in humidity means someone blew. Only the rise counts: the
// room changes far more slowly, and the sensor stays damp for a while after each breath.
constexpr int32_t kBreathRise = 15, kBreathHeat = 8;  // Hundredths of a percent, per reading.

constexpr uint32_t kEnvMeasureMs = 20, kEnvEveryMs = 250, kRetryMs = 1000;

constexpr uint8_t kGotStar = 1, kGotHeart = 2, kHurt = 4, kLevelUp = 8;
enum class Phase : uint8_t { Ready, Playing, Over };
enum class Kind : uint8_t { None, Star, Bird, Heart };

// The SHT30's checksum: CRC-8, polynomial 0x31, starting at 0xFF.
inline uint8_t crc8(const uint8_t* data, int length) {
  uint8_t crc = 0xff;
  for (int i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}
// A reading is temperature, its checksum, humidity, its checksum.
inline bool humidityCenti(const uint8_t* reading, uint16_t& centi) {
  if (crc8(reading + 3, 2) != reading[5]) return false;
  const uint32_t raw = (static_cast<uint32_t>(reading[3]) << 8) | reading[4];
  centi = static_cast<uint16_t>(raw * 10000U / 65535U);
  return true;
}

struct Breath {
  uint16_t last = 0;
  bool primed = false;
  int32_t hear(uint16_t centi) {
    const int32_t rise = static_cast<int32_t>(centi) - last;
    const bool first = !primed;
    primed = true;
    last = centi;
    return !first && rise > kBreathRise ? (rise - kBreathRise) * kBreathHeat : 0;
  }
};

struct Env {
  air_music::Unit unit = air_music::Unit::Closed;
  bool waiting = false;
  uint32_t atMs = 0;
  Breath breath;
};

// A measurement takes 15 ms, so it's started on one call and collected on a later one.
// Returns the heat this reading adds.
template <class Sensors>
int32_t pollEnv(Env& env, Sensors& sensors, uint32_t now) {
  if (env.waiting) {
    if (now - env.atMs < kEnvMeasureMs) return 0;
    env.waiting = false;
    uint8_t reading[6] = {};
    if (!sensors.envRead(reading)) {
      env.unit = air_music::Unit::Missing;
      env.atMs = now;
      env.breath = Breath{};
      return 0;
    }
    uint16_t centi = 0;
    return humidityCenti(reading, centi) ? env.breath.hear(centi) : 0;
  }
  const uint32_t wait = env.unit == air_music::Unit::Missing ? kRetryMs : kEnvEveryMs;
  if (env.unit != air_music::Unit::Closed && now - env.atMs < wait) return 0;
  env.atMs = now;
  env.unit = sensors.envTrigger() ? air_music::Unit::Ready : air_music::Unit::Missing;
  env.waiting = env.unit == air_music::Unit::Ready;
  return 0;
}

struct Stick {
  air_music::Unit unit = air_music::Unit::Closed;
  uint32_t atMs = 0;
  int8_t x = 0;
  bool pressed = false;
};

template <class Sensors>
void pollStick(Stick& stick, Sensors& sensors, uint32_t now) {
  if (stick.unit == air_music::Unit::Missing && now - stick.atMs < kRetryMs) return;
  if (sensors.stickRead(stick.x, stick.pressed)) {
    stick.unit = air_music::Unit::Ready;
  } else {
    stick.unit = air_music::Unit::Missing;
    stick.atMs = now;
    stick.x = 0;
    stick.pressed = false;
  }
}

struct Thing {
  Kind kind = Kind::None;
  int32_t x = 0, y = 0;
  uint8_t phase = 0;
};

struct Game {
  Thing things[kThings];
  int32_t x = 0, y = 0, vy = 0, heat = 0;
  uint16_t stars = 0, best = 0, frame = 0, spawnIn = 0, hurtSteps = 0, overSteps = 0;
  uint8_t lives = kLives, nudgeSteps = 0, events = 0;
  int8_t nudgeDir = 0;
  Phase phase = Phase::Ready;
  uint32_t randomState = 1;

  void reset(uint32_t seed) {
    randomState = seed ? seed : 0x9e3779b9U;
    for (auto& thing : things) thing = Thing{};
    x = kStartX * kOne; y = kRestY * kOne; vy = 0; heat = 0;
    stars = 0; spawnIn = 30; hurtSteps = 0; overSteps = 0;
    lives = kLives; nudgeSteps = 0; nudgeDir = 0; events = 0;
    phase = Phase::Ready;
  }
  uint32_t random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
  }
  int level() const { return stars / kStarsPerLevel; }
  int32_t scroll() const {
    const int32_t faster = kStartScroll + kScrollStep * level();
    return faster > kTopScroll ? kTopScroll : faster;
  }
  void addHeat(int32_t amount) {
    if (phase == Phase::Over || amount <= 0) return;
    phase = Phase::Playing;
    heat = heat + amount > kMaxHeat ? kMaxHeat : heat + amount;
  }
  void nudge(int direction) {
    if (phase != Phase::Playing) return;
    nudgeDir = static_cast<int8_t>(direction);
    nudgeSteps = kNudgeSteps;
  }
  // Birds bob up and down as they fly.
  int thingY(const Thing& thing) const {
    const int base = thing.y >> 8;
    if (thing.kind != Kind::Bird) return base;
    const int wave = (frame + thing.phase) & 63;
    return base + ((wave < 32 ? wave : 63 - wave) - 16) / 2;
  }
  void step(int stick) {
    ++frame;
    if (phase == Phase::Over) {
      if (++overSteps >= kOverSteps) reset(random());
      return;
    }
    if (phase == Phase::Ready) return;
    moveBalloon(stick);
    moveThings();
    spawn();
    if (hurtSteps) --hurtSteps;
  }

 private:
  void moveBalloon(int stick) {
    heat = heat > kCooling ? heat - kCooling : 0;
    vy += kGravity - heat * kLift / kMaxHeat;
    vy -= vy / 16;
    y += vy;
    if (y < kTopY * kOne) { y = kTopY * kOne; vy = 0; }
    // The ground is soft. The balloon just rests there until it's heated again.
    if (y > kRestY * kOne) { y = kRestY * kOne; vy = 0; }
    if (stick > kStickDead || stick < -kStickDead) x += stick * kSteerSpeed / kStickFull;
    if (nudgeSteps) { --nudgeSteps; x += nudgeDir * kNudgeSpeed; }
    if (x < kLeftX * kOne) x = kLeftX * kOne;
    if (x > kRightX * kOne) x = kRightX * kOne;
  }
  void moveThings() {
    const int32_t move = scroll();
    for (auto& thing : things) {
      if (thing.kind == Kind::None) continue;
      thing.x -= thing.kind == Kind::Bird ? move + move / 3 : move;
      if ((thing.x >> 8) < -12) { thing.kind = Kind::None; continue; }
      if (touching(thing)) meet(thing);
    }
  }
  bool touching(const Thing& thing) const {
    const int dx = (x >> 8) - (thing.x >> 8), dy = (y >> 8) - thingY(thing);
    return dx * dx + dy * dy < kReach * kReach;
  }
  void meet(Thing& thing) {
    if (thing.kind == Kind::Bird && hurtSteps) return;  // A moment of safety after a bump.
    const Kind kind = thing.kind;
    thing.kind = Kind::None;
    if (kind == Kind::Star) {
      ++stars;
      if (stars > best) best = stars;
      events |= stars % kStarsPerLevel == 0 ? kLevelUp : kGotStar;
    } else if (kind == Kind::Heart) {
      if (lives < kLives) ++lives;
      events |= kGotHeart;
    } else {
      --lives;
      hurtSteps = kHurtSteps;
      events |= kHurt;
      if (lives == 0) phase = Phase::Over;
    }
  }
  void spawn() {
    if (spawnIn > 1) { --spawnIn; return; }
    const int faster = level() * 2;
    spawnIn = static_cast<uint16_t>(kSpawnSteps - (faster > 16 ? 16 : faster));
    for (auto& thing : things) {
      if (thing.kind != Kind::None) continue;
      const int birdChance = 25 + (level() > 5 ? 25 : level() * 5);
      const int roll = static_cast<int>(random() % 100);
      thing.kind = roll < birdChance ? Kind::Bird : (roll > 90 && lives < kLives) ? Kind::Heart : Kind::Star;
      thing.x = 250 * kOne;
      thing.y = static_cast<int32_t>(kTopY + 8 + random() % (kRestY - kTopY - 8)) * kOne;
      thing.phase = static_cast<uint8_t>(random() & 63);
      return;
    }
  }
};
} // namespace balloon
