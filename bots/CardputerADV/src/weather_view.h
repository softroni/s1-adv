#pragma once
// What the Pulse screen needs to show the weather, and when to ask for it. Nothing here touches
// hardware. The bridge does the asking, so the device only needs its own ZIP code.
#include <stdint.h>
#include <string.h>

namespace weather_view {
constexpr const char* kDefaultZip = "10001";
constexpr uint32_t kRefreshMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kRetryMs = 2UL * 60UL * 1000UL;

enum class Icon : uint8_t { Sun, Moon, Partly, Cloud, Fog, Rain, Snow, Storm };

// 16x16 one-bit pictures, drawn at twice that size. Same layout as launcher_icons.h.
constexpr uint8_t kSun[32] = {
    0b00000001, 0b10000000,
    0b00000001, 0b10000000,
    0b00100000, 0b00000100,
    0b00010011, 0b11001000,
    0b00000111, 0b11100000,
    0b00001111, 0b11110000,
    0b00001111, 0b11110000,
    0b11001111, 0b11110011,
    0b11001111, 0b11110011,
    0b00001111, 0b11110000,
    0b00001111, 0b11110000,
    0b00000111, 0b11100000,
    0b00010011, 0b11001000,
    0b00100000, 0b00000100,
    0b00000001, 0b10000000,
    0b00000001, 0b10000000,
};
constexpr uint8_t kMoon[32] = {
    0b00000000, 0b00000000,
    0b00000001, 0b11110000,
    0b00000111, 0b11000000,
    0b00001111, 0b10000000,
    0b00011111, 0b00000000,
    0b00011111, 0b00000000,
    0b00111111, 0b00000000,
    0b00111111, 0b00000000,
    0b00111111, 0b00000001,
    0b00111111, 0b10000011,
    0b00011111, 0b11110110,
    0b00011111, 0b11111110,
    0b00001111, 0b11111100,
    0b00000011, 0b11110000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
};
constexpr uint8_t kPartly[32] = {
    0b00000000, 0b00000000,
    0b00000000, 0b00100000,
    0b00001000, 0b00000010,
    0b00000001, 0b11100000,
    0b00000011, 0b11110000,
    0b00100011, 0b11110110,
    0b00000011, 0b11110000,
    0b00000001, 0b11100000,
    0b00000000, 0b00000000,
    0b00001111, 0b11100000,
    0b00111111, 0b11110000,
    0b01111111, 0b11111110,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b01111111, 0b11111110,
    0b00000000, 0b00000000,
};
constexpr uint8_t kCloud[32] = {
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000011, 0b11000000,
    0b00001111, 0b11110000,
    0b00011111, 0b11111000,
    0b00111111, 0b11111000,
    0b01111111, 0b11111110,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b01111111, 0b11111110,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
};
constexpr uint8_t kFog[32] = {
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00011111, 0b11111000,
    0b01111111, 0b11111110,
    0b00000000, 0b00000000,
    0b11111111, 0b11111111,
    0b00000000, 0b00000000,
    0b00111111, 0b11111100,
    0b00000000, 0b00000000,
    0b11111111, 0b11111111,
    0b00000000, 0b00000000,
    0b00001111, 0b11110000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
};
constexpr uint8_t kRain[32] = {
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000111, 0b11000000,
    0b00011111, 0b11110000,
    0b00111111, 0b11111000,
    0b01111111, 0b11111110,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b01111111, 0b11111110,
    0b00000000, 0b00000000,
    0b00010001, 0b00010000,
    0b00100010, 0b00100000,
    0b00100010, 0b00100000,
    0b01000100, 0b01000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
};
constexpr uint8_t kSnow[32] = {
    0b00000000, 0b00000000,
    0b00000111, 0b11000000,
    0b00011111, 0b11110000,
    0b00111111, 0b11111000,
    0b01111111, 0b11111110,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b01111111, 0b11111110,
    0b00000000, 0b00000000,
    0b00110001, 0b10001100,
    0b00110001, 0b10001100,
    0b00000000, 0b00000000,
    0b00000110, 0b00110000,
    0b00000110, 0b00110000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
};
constexpr uint8_t kStorm[32] = {
    0b00000000, 0b00000000,
    0b00000111, 0b11000000,
    0b00011111, 0b11110000,
    0b00111111, 0b11111000,
    0b01111111, 0b11111110,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b01111111, 0b11111110,
    0b00000001, 0b11000000,
    0b00000011, 0b10000000,
    0b00000111, 0b11000000,
    0b00000001, 0b10000000,
    0b00000011, 0b00000000,
    0b00000100, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
};

inline const uint8_t* bitmap(Icon icon) {
  switch (icon) {
    case Icon::Sun: return kSun;
    case Icon::Moon: return kMoon;
    case Icon::Partly: return kPartly;
    case Icon::Fog: return kFog;
    case Icon::Rain: return kRain;
    case Icon::Snow: return kSnow;
    case Icon::Storm: return kStorm;
    case Icon::Cloud: break;
  }
  return kCloud;
}

constexpr uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline uint16_t tint(Icon icon) {
  switch (icon) {
    case Icon::Sun: case Icon::Partly: return color(255, 210, 64);
    case Icon::Moon: return color(203, 216, 226);
    case Icon::Rain: return color(47, 150, 255);
    case Icon::Snow: return color(255, 255, 255);
    case Icon::Storm: return color(180, 150, 255);
    case Icon::Fog: case Icon::Cloud: break;
  }
  return color(180, 190, 200);
}

inline Icon iconFromWire(const char* name) {
  if (!name) return Icon::Cloud;
  if (!strcmp(name, "sun")) return Icon::Sun;
  if (!strcmp(name, "moon")) return Icon::Moon;
  if (!strcmp(name, "partly")) return Icon::Partly;
  if (!strcmp(name, "fog")) return Icon::Fog;
  if (!strcmp(name, "rain")) return Icon::Rain;
  if (!strcmp(name, "snow")) return Icon::Snow;
  if (!strcmp(name, "storm")) return Icon::Storm;
  return Icon::Cloud;
}

inline bool validZip(const char* zip) {
  if (!zip || strlen(zip) != 5) return false;
  for (int i = 0; i < 5; ++i)
    if (zip[i] < '0' || zip[i] > '9') return false;
  return true;
}

// Asks as soon as it can after boot, then every 30 minutes. A failed attempt tries again sooner.
struct Schedule {
  bool armed = false;
  uint32_t nextMs = 0;

  void reset() { armed = false; }
  bool due(uint32_t nowMs) const { return !armed || static_cast<int32_t>(nowMs - nextMs) >= 0; }
  void asked(uint32_t nowMs) { armed = true; nextMs = nowMs + kRefreshMs; }
  void retrySoon(uint32_t nowMs) { armed = true; nextMs = nowMs + kRetryMs; }
};
}  // namespace weather_view
