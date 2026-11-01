#pragma once
// Fixed-size Flappy Bird, steered by a hand over an ultrasonic sensor or flapped by key.
// No heap, recursion, storage or network, and nothing here touches hardware.
#include <stdint.h>
#include "air_music.h"

namespace flappy {
// Positions and speeds are in 1/256ths of a pixel, so slow movement stays smooth.
constexpr int32_t kOne = 256;
constexpr int kGroundY = 127, kBirdX = 48, kBirdHalf = 5, kBirdTopY = 7;
constexpr int kPipes = 3, kPipeWidth = 20, kPipeSpacing = 120, kFirstPipeX = 260, kGapMargin = 12;
constexpr int kLargestGap = 60, kSmallestGap = 48;
constexpr uint32_t kStepMs = 33;
constexpr int kLostSteps = 45;  // A moment to see the score.
constexpr int32_t kGravity = 72, kFlap = -819, kFallLimit = 1280;
constexpr int32_t kStartSpeed = 333, kTopSpeed = 563, kSpeedGain = 6;

// A hand 6 to 30 cm from the sensor covers the screen, and higher is further away.
constexpr uint32_t kHandLowMm = 60, kHandHighMm = 300, kHandGoneMm = 500;
constexpr int kHandTopY = 12, kHandBottomY = 116;
constexpr int32_t kHandSpeed = 4 * kOne;

constexpr uint8_t kFlapped = 1, kScored = 2, kCrashed = 4;
enum class Phase : uint8_t { Ready, Playing, Lost };

inline int32_t heightFor(uint32_t mm) {
  if (mm <= kHandLowMm) return kHandBottomY * kOne;
  if (mm >= kHandHighMm) return kHandTopY * kOne;
  const uint32_t span = (kHandBottomY - kHandTopY) * kOne;
  return kHandBottomY * kOne - static_cast<int32_t>((mm - kHandLowMm) * span / (kHandHighMm - kHandLowMm));
}

// Works with air_music::poll, which shares the sensor's timing with Air Music.
struct Hand {
  air_music::Unit unit = air_music::Unit::Closed;
  bool waiting = false, here = false;
  uint8_t misses = 0;
  uint32_t atMs = 0, mm = 0;

  void silence() { here = false; misses = 0; }
  void hear(uint32_t reading) {
    if (reading != 0 && reading < kHandGoneMm) {
      mm = reading; here = true; misses = 0;
    } else if (here && ++misses >= air_music::kMissesToSilence) {
      silence();
    }
  }
};

struct Pipe {
  int32_t x = 0;
  int16_t gapTop = 0, gapSize = 0;
  bool scored = false;
};

struct Game {
  Pipe pipes[kPipes];
  int32_t y = 0, vy = 0;
  uint16_t score = 0, best = 0, lostSteps = 0;
  Phase phase = Phase::Ready;
  uint8_t events = 0;
  uint32_t randomState = 1;

  void reset(uint32_t seed) {
    randomState = seed ? seed : 0x9e3779b9U;
    y = 60 * kOne; vy = 0; score = 0; lostSteps = 0; events = 0;
    phase = Phase::Ready;
    for (int i = 0; i < kPipes; ++i) place(pipes[i], (kFirstPipeX + i * kPipeSpacing) * kOne);
  }
  uint32_t random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
  }
  int32_t speed() const {
    const int32_t faster = kStartSpeed + kSpeedGain * score;
    return faster > kTopSpeed ? kTopSpeed : faster;
  }
  int currentGap() const {
    const int gap = kLargestGap - score / 2;
    return gap < kSmallestGap ? kSmallestGap : gap;
  }
  void place(Pipe& pipe, int32_t x) {
    const int size = currentGap();
    const uint32_t room = static_cast<uint32_t>(kGroundY - 2 * kGapMargin - size);
    pipe.x = x;
    pipe.gapSize = static_cast<int16_t>(size);
    pipe.gapTop = static_cast<int16_t>(kGapMargin + random() % (room + 1));
    pipe.scored = false;
  }
  void flap() {
    if (phase == Phase::Lost) return;
    phase = Phase::Playing;
    vy = kFlap;
    events |= kFlapped;
  }
  void step(bool handHere, uint32_t handMm) {
    if (phase == Phase::Lost) {
      if (++lostSteps >= kLostSteps) reset(random());
      return;
    }
    if (phase == Phase::Ready) {
      if (!handHere) return;
      phase = Phase::Playing;
    }
    moveBird(handHere, handMm);
    movePipes();
    if (hit()) {
      phase = Phase::Lost;
      events |= kCrashed;
    }
  }

 private:
  void moveBird(bool handHere, uint32_t handMm) {
    if (handHere) {
      const int32_t distance = heightFor(handMm) - y;
      y += distance > kHandSpeed ? kHandSpeed : distance < -kHandSpeed ? -kHandSpeed : distance;
      vy = 0;
    } else {
      vy += kGravity;
      if (vy > kFallLimit) vy = kFallLimit;
      y += vy;
    }
    if (y < kBirdTopY * kOne) { y = kBirdTopY * kOne; vy = 0; }
  }
  void movePipes() {
    const int32_t move = speed();
    int32_t furthest = 0;
    for (auto& pipe : pipes) {
      pipe.x -= move;
      if (pipe.x > furthest) furthest = pipe.x;
    }
    for (auto& pipe : pipes) {
      const int right = (pipe.x >> 8) + kPipeWidth;
      if (!pipe.scored && right < kBirdX - kBirdHalf) {
        pipe.scored = true;
        ++score;
        if (score > best) best = score;
        events |= kScored;
      }
      if (right < -4) place(pipe, furthest + kPipeSpacing * kOne);
    }
  }
  bool hit() const {
    const int bird = y >> 8;
    if (bird + kBirdHalf >= kGroundY) return true;
    for (const auto& pipe : pipes) {
      const int left = pipe.x >> 8;
      if (kBirdX + kBirdHalf <= left || kBirdX - kBirdHalf >= left + kPipeWidth) continue;
      if (bird - kBirdHalf < pipe.gapTop || bird + kBirdHalf > pipe.gapTop + pipe.gapSize) return true;
    }
    return false;
  }
};
} // namespace flappy
