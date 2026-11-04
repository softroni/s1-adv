#pragma once
// What the owner reads, US or metric, from Settings > Device > Units. Only the screen changes:
// readings and saved rules keep the Lab's own units (C, cm, km/h, m), so nothing saved moves when
// the setting does, and Claude writes rules in those units too.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace units {
enum class System : uint8_t { US = 0, Metric = 1, Count };
constexpr System kDefault = System::US;
inline System fromSaved(uint8_t value) { return value < static_cast<uint8_t>(System::Count) ? static_cast<System>(value) : kDefault; }
inline const char* name(System system) { return system == System::Metric ? "Metric" : "US"; }

// Rounded half away from zero, which is what people expect of a reading.
inline int32_t rounded(double value) { return static_cast<int32_t>(value < 0 ? value - 0.5 : value + 0.5); }
inline int32_t fahrenheit(int32_t celsius) { return rounded(celsius * 1.8 + 32); }
inline int32_t fahrenheitTenths(int32_t celsiusTenths) { return rounded(celsiusTenths * 1.8 + 320); }
inline int32_t inches(int32_t cm) { return rounded(cm / 2.54); }
inline int32_t mph(int32_t kmh) { return rounded(kmh * 0.621371); }
inline int32_t mphTenths(int32_t kmhTenths) { return rounded(kmhTenths * 0.621371); }
inline int32_t feet(int32_t meters) { return rounded(meters * 3.28084); }

// "12.3": tenths with their sign, and no "-0.0".
inline void tenthsText(char* out, size_t size, int32_t tenths) {
  const int32_t magnitude = tenths < 0 ? -tenths : tenths;
  snprintf(out, size, "%s%ld.%ld", tenths < 0 ? "-" : "", static_cast<long>(magnitude / 10), static_cast<long>(magnitude % 10));
}
// A distance in meters for a screen: feet up to a tenth of a mile, then miles; meters up to a
// kilometer, then kilometers. `capped` adds a plus, for a reading that stopped at its limit.
inline void distanceText(char* out, size_t size, int32_t meters, System system, bool capped = false) {
  const char* more = capped ? "+" : "";
  char number[16];
  if (system == System::US) {
    const int32_t ft = feet(meters);
    if (ft < 528) { snprintf(out, size, "%ld%s ft", static_cast<long>(ft), more); return; }
    tenthsText(number, sizeof(number), rounded(meters / 160.9344));
    snprintf(out, size, "%s%s mi", number, more);
    return;
  }
  if (meters < 1000) { snprintf(out, size, "%ld%s m", static_cast<long>(meters), more); return; }
  tenthsText(number, sizeof(number), rounded(meters / 100.0));
  snprintf(out, size, "%s%s km", number, more);
}

// A Lab reading in its own unit, as the owner reads it. `degree` is the font's degree sign.
inline void readingText(char* out, size_t size, int32_t value, const char* unit, const char* degree, System system) {
  if (system == System::US) {
    if (strcmp(unit, "C") == 0) { snprintf(out, size, "%ld%sF", static_cast<long>(fahrenheit(value)), degree); return; }
    if (strcmp(unit, "cm") == 0) { snprintf(out, size, "%ld in", static_cast<long>(inches(value))); return; }
    if (strcmp(unit, "km/h") == 0) { snprintf(out, size, "%ld mph", static_cast<long>(mph(value))); return; }
    if (strcmp(unit, "m") == 0) { snprintf(out, size, "%ld ft", static_cast<long>(feet(value))); return; }
  }
  if (strcmp(unit, "deg") == 0) snprintf(out, size, "%ld%s", static_cast<long>(value), degree);
  else if (strcmp(unit, "C") == 0) snprintf(out, size, "%ld%sC", static_cast<long>(value), degree);
  else if (strcmp(unit, "%") == 0) snprintf(out, size, "%ld%%", static_cast<long>(value));
  else if (unit[0]) snprintf(out, size, "%ld %s", static_cast<long>(value), unit);
  else snprintf(out, size, "%ld", static_cast<long>(value));
}
}  // namespace units
