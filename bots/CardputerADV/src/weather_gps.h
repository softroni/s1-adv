#pragma once
// Pulse's weather at the Cap's GPS position rather than the saved city. No hardware here. The
// position only leaves the device rounded to a hundredth of a degree, about a kilometre, which is
// plenty for weather, and it goes only to the owner's bridge.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "lab_gps.h"

namespace weather_gps {
constexpr uint32_t kFreshMs = 2UL * 60UL * 60UL * 1000UL;  // An older position gives way to the city.
constexpr int32_t kMoveMeters = 3000;                      // Moving further asks again sooner.
constexpr uint32_t kNudgeGapMs = 2UL * 60UL * 1000UL;      // But never more often than this.

struct Spot {
  bool known = false;
  int32_t latE5 = 0, lonE5 = 0;
  uint32_t atMs = 0;
};
inline bool usable(const Spot& spot, uint32_t nowMs) { return spot.known && nowMs - spot.atMs < kFreshMs; }

// Degrees to two places, rounded half away from zero: 4250833 is "42.51", and nothing is "-0.00".
inline void hundredths(char* out, size_t size, int32_t e5) {
  const int32_t magnitude = e5 < 0 ? -e5 : e5;
  const int32_t h = (magnitude + 500) / 1000;
  snprintf(out, size, "%s%ld.%02ld", e5 < 0 && h ? "-" : "", static_cast<long>(h / 100), static_cast<long>(h % 100));
}
inline void query(char* out, size_t size, const Spot& spot) {
  char lat[16], lon[16];
  hundredths(lat, sizeof(lat), spot.latE5);
  hundredths(lon, sizeof(lon), spot.lonE5);
  snprintf(out, size, "lat=%s&lon=%s", lat, lon);
}
// Whether the weather on screen is for somewhere else, so it's worth asking now, not at the next refresh.
inline bool askSooner(const Spot& here, const Spot& shown, uint32_t nowMs, bool nudged, uint32_t lastNudgeMs) {
  if (!usable(here, nowMs)) return false;
  if (nudged && nowMs - lastNudgeMs < kNudgeGapMs) return false;
  return !shown.known || lab_gps::metersBetween(shown.latE5, shown.lonE5, here.latE5, here.lonE5) >= kMoveMeters;
}
}  // namespace weather_gps
