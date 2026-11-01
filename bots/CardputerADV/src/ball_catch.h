#pragma once
// Ball Catch: a ball slides along the top, a press drops it, and a basket catches it. It keeps
// some of its slide as it falls, so the basket has to lead it, and some balls are bombs to let
// fall past. A ball left too long drops itself. No heap, recursion, storage or network, and
// nothing here touches hardware.
#include <stdint.h>

namespace ball_catch {
// Positions and speeds are in 1/256ths of a pixel, so slow movement stays smooth.
constexpr int32_t kOne = 256;
constexpr int kWidth = 240, kBallRadius = 6, kBallRightX = kWidth - kBallRadius - 1;
constexpr int kTrackY = 27, kRimY = 112, kGroundY = 127;
constexpr uint32_t kStepMs = 33;
constexpr int kLives = 3, kOverSteps = 75, kCatchesPerLevel = 5;
constexpr int kLargestBasket = 46, kSmallestBasket = 26;
constexpr int32_t kStartSpeed = 576, kTopSpeed = 3 * kStartSpeed, kSpeedStep = 48;
constexpr int32_t kGravity = 60, kFallLimit = 1280;
constexpr int32_t kBasketSpeed = 896, kNudgeSpeed = 640;
constexpr int kStickDead = 16, kStickFull = 127, kNudgeSteps = 10;
// A falling ball keeps a third of its slide, and gusts add to that as the levels go up.
constexpr int32_t kCarryDivisor = 3, kGustStep = 26, kStrongestGust = 205;
// The first catches are always real balls, so the game can be learned.
constexpr int kSafeCatches = 2, kFirstBombChance = 15, kBombChanceStep = 4, kMostBombChance = 40;
constexpr int kLongestWait = 150, kWaitStep = 8, kShortestWait = 75, kFlashSteps = 9;

constexpr uint8_t kCaught = 1, kMissed = 2, kLevelUp = 4, kBombed = 8, kDodged = 16;
enum class Phase : uint8_t { Ready, Playing, Over };

struct Game {
  int32_t ballX = 0, ballY = 0, ballVx = 0, fall = 0, driftVx = 0, basketX = 0;
  uint16_t caught = 0, best = 0, overSteps = 0, waited = 0;
  uint8_t lives = kLives, nudgeSteps = 0, events = 0, flashSteps = 0;
  int8_t nudgeDir = 0;
  bool falling = false, bomb = false;
  Phase phase = Phase::Ready;
  uint32_t randomState = 1;

  void reset(uint32_t seed) {
    randomState = seed ? seed : 0x9e3779b9U;
    caught = 0; overSteps = 0; lives = kLives; nudgeSteps = 0; nudgeDir = 0; events = 0;
    flashSteps = 0;
    basketX = (kWidth / 2) * kOne;
    phase = Phase::Ready;
    newBall();
  }
  uint32_t random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
  }
  int level() const { return caught / kCatchesPerLevel; }
  int32_t speed() const {
    const int32_t faster = kStartSpeed + kSpeedStep * (caught / 2);
    return faster > kTopSpeed ? kTopSpeed : faster;
  }
  int32_t gust() const {
    const int32_t strength = kGustStep * level();
    return strength > kStrongestGust ? kStrongestGust : strength;
  }
  int bombChance() const {
    if (caught < kSafeCatches) return 0;
    const int chance = kFirstBombChance + kBombChanceStep * level();
    return chance > kMostBombChance ? kMostBombChance : chance;
  }
  int waitLimit() const {
    const int limit = kLongestWait - kWaitStep * level();
    return limit < kShortestWait ? kShortestWait : limit;
  }
  int basketWidth() const {
    const int width = kLargestBasket - 2 * (caught / kCatchesPerLevel);
    return width < kSmallestBasket ? kSmallestBasket : width;
  }
  void drop() {
    if (phase == Phase::Over || falling) return;
    phase = Phase::Playing;
    falling = true;
    fall = 0;
    const int32_t strength = gust();
    const int32_t blown = strength ? static_cast<int32_t>(random() % (2 * strength + 1)) - strength : 0;
    driftVx = ballVx * speed() / kCarryDivisor + blown;
  }
  void nudge(int direction) {
    if (phase == Phase::Over) return;
    nudgeDir = static_cast<int8_t>(direction);
    nudgeSteps = kNudgeSteps;
  }
  void step(int stick) {
    if (flashSteps) --flashSteps;
    if (phase == Phase::Over) {
      if (++overSteps >= kOverSteps) reset(random());
      return;
    }
    moveBasket(stick);
    if (falling) {
      dropBall();
      return;
    }
    slideBall();
    if (phase == Phase::Playing && ++waited >= waitLimit()) drop();
  }

 private:
  void newBall() {
    const uint32_t span = static_cast<uint32_t>(kBallRightX - kBallRadius + 1);
    ballX = (kBallRadius + static_cast<int32_t>(random() % span)) * kOne;
    ballY = kTrackY * kOne;
    ballVx = (random() & 1) ? 1 : -1;
    bomb = static_cast<int>(random() % 100) < bombChance();
    fall = 0;
    driftVx = 0;
    waited = 0;
    falling = false;
  }
  void moveBasket(int stick) {
    if (stick > kStickDead || stick < -kStickDead) basketX += stick * kBasketSpeed / kStickFull;
    if (nudgeSteps) {
      --nudgeSteps;
      basketX += nudgeDir * kNudgeSpeed;
    }
    const int32_t half = (basketWidth() / 2) * kOne;
    if (basketX < half) basketX = half;
    if (basketX > kWidth * kOne - half) basketX = kWidth * kOne - half;
  }
  void slideBall() {
    ballX += ballVx * speed();
    const int32_t low = kBallRadius * kOne, high = kBallRightX * kOne;
    if (ballX < low) { ballX = low; ballVx = 1; }
    if (ballX > high) { ballX = high; ballVx = -1; }
  }
  void dropBall() {
    fall = fall + kGravity > kFallLimit ? kFallLimit : fall + kGravity;
    ballY += fall;
    ballX += driftVx;
    const int32_t low = kBallRadius * kOne, high = kBallRightX * kOne;
    if (ballX < low) { ballX = low; driftVx = -driftVx; }
    if (ballX > high) { ballX = high; driftVx = -driftVx; }
    if ((ballY >> 8) + kBallRadius < kRimY) return;
    const int32_t apart = ballX > basketX ? ballX - basketX : basketX - ballX;
    const bool inBasket = apart <= (basketWidth() / 2) * kOne;
    if (bomb && !inBasket) {
      events |= kDodged;
    } else if (inBasket && !bomb) {
      ++caught;
      if (caught > best) best = caught;
      events |= caught % kCatchesPerLevel == 0 ? kLevelUp : kCaught;
    } else {
      --lives;
      events |= bomb ? kBombed : kMissed;
      if (bomb) flashSteps = kFlashSteps;
      if (lives == 0) phase = Phase::Over;
    }
    newBall();
  }
};
} // namespace ball_catch
