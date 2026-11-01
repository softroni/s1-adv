#pragma once
// Ball Catch: a ball slides along the top, a press drops it, and a basket catches it. No heap,
// recursion, storage or network, and nothing here touches hardware.
#include <stdint.h>

namespace ball_catch {
// Positions and speeds are in 1/256ths of a pixel, so slow movement stays smooth.
constexpr int32_t kOne = 256;
constexpr int kWidth = 240, kBallRadius = 6, kTrackY = 27, kRimY = 112, kGroundY = 127;
constexpr uint32_t kStepMs = 33;
constexpr int kLives = 3, kOverSteps = 75, kCatchesPerLevel = 5;
constexpr int kLargestBasket = 46, kSmallestBasket = 26;
constexpr int32_t kStartSpeed = 384, kTopSpeed = 3 * kStartSpeed, kSpeedStep = 38;
constexpr int32_t kGravity = 46, kFallLimit = 1024;
constexpr int32_t kBasketSpeed = 896, kNudgeSpeed = 640;
constexpr int kStickDead = 16, kStickFull = 127, kNudgeSteps = 10;

constexpr uint8_t kCaught = 1, kMissed = 2, kLevelUp = 4;
enum class Phase : uint8_t { Ready, Playing, Over };

struct Game {
  int32_t ballX = 0, ballY = 0, ballVx = 0, fall = 0, basketX = 0;
  uint16_t caught = 0, best = 0, overSteps = 0;
  uint8_t lives = kLives, nudgeSteps = 0, events = 0;
  int8_t nudgeDir = 0;
  bool falling = false;
  Phase phase = Phase::Ready;
  uint32_t randomState = 1;

  void reset(uint32_t seed) {
    randomState = seed ? seed : 0x9e3779b9U;
    caught = 0; overSteps = 0; lives = kLives; nudgeSteps = 0; nudgeDir = 0; events = 0;
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
  int32_t speed() const {
    const int32_t faster = kStartSpeed + kSpeedStep * (caught / 2);
    return faster > kTopSpeed ? kTopSpeed : faster;
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
  }
  void nudge(int direction) {
    if (phase == Phase::Over) return;
    nudgeDir = static_cast<int8_t>(direction);
    nudgeSteps = kNudgeSteps;
  }
  void step(int stick) {
    if (phase == Phase::Over) {
      if (++overSteps >= kOverSteps) reset(random());
      return;
    }
    moveBasket(stick);
    if (falling) dropBall();
    else slideBall();
  }

 private:
  void newBall() {
    const uint32_t span = static_cast<uint32_t>(kWidth - 2 * kBallRadius);
    ballX = (kBallRadius + static_cast<int32_t>(random() % span)) * kOne;
    ballY = kTrackY * kOne;
    ballVx = (random() & 1) ? 1 : -1;
    fall = 0;
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
    const int32_t low = kBallRadius * kOne, high = (kWidth - kBallRadius) * kOne;
    if (ballX < low) { ballX = low; ballVx = 1; }
    if (ballX > high) { ballX = high; ballVx = -1; }
  }
  void dropBall() {
    fall = fall + kGravity > kFallLimit ? kFallLimit : fall + kGravity;
    ballY += fall;
    if ((ballY >> 8) + kBallRadius < kRimY) return;
    const int32_t apart = ballX > basketX ? ballX - basketX : basketX - ballX;
    if (apart <= (basketWidth() / 2) * kOne) {
      ++caught;
      if (caught > best) best = caught;
      events |= caught % kCatchesPerLevel == 0 ? kLevelUp : kCaught;
    } else {
      --lives;
      events |= kMissed;
      if (lives == 0) phase = Phase::Over;
    }
    newBall();
  }
};
} // namespace ball_catch
