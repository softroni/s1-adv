#pragma once
// Which sensor Sonar listens to, and how it reads the M5 unit. No heap, storage or network, and
// nothing here touches hardware: the unit is anything with trigger() and read(), as in Air Music.
#include <stdint.h>

namespace sonar {
// The HC-SR04 on the EXT header can't be seen until it echoes, but the M5 ultrasonic unit on
// Grove answers on the bus. So Sonar looks at Grove first, and again every second while the
// HC-SR04 hears nothing, so plugging in either one is enough. With both in, the unit wins.
constexpr uint32_t kLookMs = 1000;
constexpr uint32_t kUnitMeasureMs = 120;  // The same wait as Air Music's.
constexpr uint32_t kUnitRetryMs = 150;
constexpr uint8_t kUnitFailsToLose = 4;   // Bad transfers in a row. One isn't an unplugged unit.
constexpr uint32_t kUnitFarthestMm = 4500;

enum class Source : uint8_t { Echo, Unit };  // The HC-SR04 on EXT, or the M5 unit on Grove.
// Reading is a distance in mm. Empty is a measurement with nothing in range.
enum class Step : uint8_t { Nothing, Found, Lost, Reading, Empty };

struct Picker {
  Source source = Source::Echo;
  bool looked = false;   // At Grove, since Sonar opened or the unit was lost.
  bool waiting = false;  // For the unit's measurement.
  uint8_t fails = 0;
  uint32_t atMs = 0;

  void lookNow() {
    if (source == Source::Echo) looked = false;
  }
};

// The unit only acknowledges a real command and can't answer while it measures, so looking
// means starting a measurement, the way the Lab finds it. A found unit's first reading comes
// 120 ms later.
template <class Unit>
Step service(Picker& p, Unit& unit, uint32_t now, bool echoFresh, uint32_t& mm) {
  if (p.source == Source::Echo) {
    if (echoFresh) return Step::Nothing;
    if (p.looked && now - p.atMs < kLookMs) return Step::Nothing;
    p.looked = true;
    p.atMs = now;
    if (!unit.trigger()) return Step::Nothing;
    p.source = Source::Unit;
    p.waiting = true;
    p.fails = 0;
    return Step::Found;
  }
  if (p.waiting) {
    if (now - p.atMs < kUnitMeasureMs) return Step::Nothing;
    p.waiting = false;
    uint32_t micrometers = 0;
    if (unit.read(micrometers)) {
      p.fails = 0;
      mm = micrometers / 1000;
      // Nothing in range reads as zero, or as far past the unit's reach.
      const Step heard = mm == 0 || mm > kUnitFarthestMm ? Step::Empty : Step::Reading;
      // The next measurement starts at once, as in Air Music, so none of the loop's time is lost.
      p.atMs = now;
      if (unit.trigger()) p.waiting = true;
      else ++p.fails;
      return heard;
    }
  } else {
    if (p.fails && now - p.atMs < kUnitRetryMs) return Step::Nothing;
    if (unit.trigger()) {
      p.atMs = now;
      p.waiting = true;
      return Step::Nothing;
    }
  }
  p.atMs = now;
  if (++p.fails < kUnitFailsToLose) return Step::Nothing;
  // Back to the HC-SR04, which gets a second to echo before Grove is looked at again.
  p = Picker{};
  p.looked = true;
  p.atMs = now;
  return Step::Lost;
}
}  // namespace sonar
