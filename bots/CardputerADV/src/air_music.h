#pragma once
// Turns a hand's distance from an ultrasonic sensor into notes. No heap, storage or
// network, and nothing here touches hardware.
#include <stdint.h>

namespace air_music {
// A pentatonic scale, so any run of notes sounds good. Pitched for the small speaker.
constexpr int kNotes = 10;
constexpr uint16_t kHz[kNotes] = {523, 587, 659, 784, 880, 1047, 1175, 1319, 1568, 1760};
constexpr char kNames[kNotes + 1] = "CDEGACDEGA";

// Closer is higher. Each note gets its own 4 cm of air, and the far edge is 44 cm away.
constexpr uint32_t kNearMm = 40, kBandMm = 40, kFarMm = kNearMm + kNotes * kBandMm;
// A note holds through this much wobble, so a steady hand doesn't flicker between two.
constexpr uint32_t kSlackMm = 8;
constexpr uint32_t kMeasureMs = 120, kRetryMs = 500;
// One lost echo shouldn't cut a note off.
constexpr uint8_t kMissesToSilence = 2;

enum class Unit : uint8_t { Closed, Ready, Missing };

// The inner edge of a note's band. The top note reaches all the way to the sensor.
inline uint32_t bandNear(int note) {
  return note == kNotes - 1 ? 0 : kNearMm + (kNotes - 1 - note) * kBandMm;
}
inline uint32_t bandFar(int note) { return kNearMm + (kNotes - note) * kBandMm; }
inline int noteFor(uint32_t mm) {
  if (mm == 0 || mm >= kFarMm) return -1;
  for (int note = kNotes - 1; note >= 0; --note) {
    if (mm < bandFar(note)) return note;
  }
  return -1;
}

struct App {
  Unit unit = Unit::Closed;
  int8_t note = -1;
  uint8_t misses = 0;
  bool waiting = false;
  uint32_t atMs = 0;

  void reset() { *this = App{}; }
  void silence() { note = -1; misses = 0; }
  void hear(uint32_t mm) {
    const bool holding = note >= 0 && mm != 0 &&
        mm + kSlackMm >= bandNear(note) && mm < bandFar(note) + kSlackMm;
    if (holding) { misses = 0; return; }
    const int next = noteFor(mm);
    if (next >= 0) { note = static_cast<int8_t>(next); misses = 0; return; }
    if (note >= 0 && ++misses >= kMissesToSilence) silence();
  }
};

// The sensor takes 120 ms to measure, far too long to hold up the main loop, so a reading
// is started on one call and collected on a later one.
template <class Sensor>
void poll(App& app, Sensor& sensor, uint32_t now) {
  if (app.waiting) {
    if (now - app.atMs < kMeasureMs) return;
    app.waiting = false;
    uint32_t micrometers = 0;
    if (!sensor.read(micrometers)) {
      app.unit = Unit::Missing;
      app.atMs = now;
      app.silence();
      return;
    }
    app.hear(micrometers / 1000);
  } else if (app.unit == Unit::Missing && now - app.atMs < kRetryMs) {
    return;
  }
  app.atMs = now;
  if (sensor.trigger()) {
    app.unit = Unit::Ready;
    app.waiting = true;
  } else {
    app.unit = Unit::Missing;
    app.silence();
  }
}
} // namespace air_music
