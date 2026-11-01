#pragma once
// Fixed-size Snake. No heap, recursion, storage or network.
#include <stdint.h>

namespace snake {
constexpr int kCols = 24, kRows = 13, kCells = kCols * kRows, kStartLength = 3;
constexpr uint32_t kStartStepMs = 200, kFastestStepMs = 80, kStepGainMs = 4;
enum class Phase : uint8_t { Ready, Playing, Paused, Lost, Won };
enum class Dir : uint8_t { Up, Right, Down, Left };
inline bool opposite(Dir a, Dir b) { return (static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b)) == 2; }
inline int cellAt(int x, int y) { return y * kCols + x; }

struct Game {
  // Ring buffer of occupied cells, tail first.
  uint16_t body[kCells] = {};
  bool occupied[kCells] = {};
  uint16_t tail = 0, length = 0, food = 0, score = 0;
  // Two turns can land inside one step, so a quick U-turn isn't lost.
  Dir heading = Dir::Right, queued[2] = {Dir::Right, Dir::Right};
  uint8_t queuedCount = 0;
  Phase phase = Phase::Ready;
  bool initialized = false;
  uint32_t randomState = 1, lastStepMs = 0;

  void reset(uint32_t seed) {
    for (auto& cell : occupied) cell = false;
    tail = 0; length = 0; score = 0; queuedCount = 0;
    heading = Dir::Right; phase = Phase::Ready; initialized = true;
    randomState = seed ? seed : 0x9e3779b9U;
    for (int i = 0; i < kStartLength; ++i) {
      const int cell = cellAt(kCols / 2 - kStartLength + 1 + i, kRows / 2);
      body[length++] = static_cast<uint16_t>(cell);
      occupied[cell] = true;
    }
    placeFood();
  }
  uint32_t random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
  }
  uint16_t cellOf(int index) const { return body[(tail + index) % kCells]; }
  uint16_t head() const { return cellOf(length - 1); }
  uint32_t stepMs() const {
    const uint32_t gain = kStepGainMs * score;
    return gain >= kStartStepMs - kFastestStepMs ? kFastestStepMs : kStartStepMs - gain;
  }
  void placeFood() {
    int pick = static_cast<int>(random() % static_cast<uint32_t>(kCells - length));
    for (int cell = 0; cell < kCells; ++cell) {
      if (occupied[cell]) continue;
      if (pick-- == 0) { food = static_cast<uint16_t>(cell); return; }
    }
  }
  bool turn(Dir next) {
    const Dir last = queuedCount ? queued[queuedCount - 1] : heading;
    if (next == last || opposite(next, last) || queuedCount == 2) return false;
    queued[queuedCount++] = next;
    return true;
  }
  void play(uint32_t now) {
    if (phase != Phase::Ready && phase != Phase::Paused) return;
    phase = Phase::Playing;
    lastStepMs = now;
  }
  void pause() {
    if (phase == Phase::Playing) phase = Phase::Paused;
  }
  void step() {
    if (phase != Phase::Playing) return;
    if (queuedCount) {
      heading = queued[0];
      queued[0] = queued[1];
      --queuedCount;
    }
    const int from = head();
    int x = from % kCols, y = from / kCols;
    if (heading == Dir::Up) --y;
    else if (heading == Dir::Down) ++y;
    else if (heading == Dir::Left) --x;
    else ++x;
    if (x < 0 || x >= kCols || y < 0 || y >= kRows) { phase = Phase::Lost; return; }
    const int next = cellAt(x, y);
    const bool eating = next == food;
    // The tail moves out of the way this step, so following it is legal.
    const bool intoTail = !eating && next == cellOf(0);
    if (occupied[next] && !intoTail) { phase = Phase::Lost; return; }
    if (!eating) {
      occupied[cellOf(0)] = false;
      tail = static_cast<uint16_t>((tail + 1) % kCells);
      --length;
    }
    body[(tail + length) % kCells] = static_cast<uint16_t>(next);
    occupied[next] = true;
    ++length;
    if (!eating) return;
    ++score;
    if (length == kCells) phase = Phase::Won;
    else placeFood();
  }
  bool advance(uint32_t now) {
    if (phase != Phase::Playing || now - lastStepMs < stepMs()) return false;
    lastStepMs = now;
    step();
    return true;
  }
};
} // namespace snake
