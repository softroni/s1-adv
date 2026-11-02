#pragma once
// What the Pulse screen needs to show the weather, and when to ask for it. Nothing here touches
// hardware. The bridge does the asking, so the device only needs the name of a place.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace weather_view {
constexpr const char* kDefaultPlace = "Salem, MA";
constexpr size_t kMaxPlaceChars = 32;
constexpr uint32_t kRefreshMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kRetryMs = 2UL * 60UL * 1000UL;

enum class Icon : uint8_t { Sun, Moon, Partly, Cloud, Fog, Rain, Snow, Storm };

// 16x16 pictures, drawn at twice that size. Each letter is a colour from ink(), and a dot is
// see-through, so the rows are the picture and can be edited directly.
constexpr const char* kSun[16] = {
    ".......OO.......",
    ".......OO.......",
    "..O..........O..",
    "...O..YYYY..O...",
    ".....YYYYYY.....",
    "....YYWWYYYY....",
    "....YWWYYYYY....",
    "OO..YYYYYYYO..OO",
    "OO..YYYYYYYO..OO",
    "....YYYYYYOO....",
    "....YYYYYOOO....",
    ".....YYOOOO.....",
    "...O..OOOO..O...",
    "..O..........O..",
    ".......OO.......",
    ".......OO.......",
};
constexpr const char* kMoon[16] = {
    ".......PPPP.....",
    ".....PPPP.....W.",
    "....PPPP........",
    "...PPPP.....W...",
    "...PPPP.........",
    "..PPPPP.........",
    "..PPPPP....W....",
    "..PPPPP.........",
    "..PPPPPP.......Y",
    "..YPPPPPP.....PY",
    "...YPPPPPPPPPPY.",
    "...YYPPPPPPPPYY.",
    "....YYYPPPPYY...",
    "......YYYYYY....",
    "................",
    "................",
};
constexpr const char* kPartly[16] = {
    ".........O......",
    "....O........O..",
    ".......YYYY.....",
    "......YYYYYY....",
    "..O..YYYYYYYY.OO",
    ".....YYYYYYYY...",
    "......YYYYYYO...",
    ".....WWWWYYYO.O.",
    "...WWWWWWWWYO...",
    "..WWWWWWWWWWWWW.",
    ".WWWWWWWWWWWWWWW",
    "WWWWWWWWWWWWWWWW",
    "LLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLL",
    ".GGGGGGGGGGGGGG.",
    "................",
};
constexpr const char* kCloud[16] = {
    "................",
    "................",
    "................",
    "......WWWW......",
    "....WWWWWWWW....",
    "...WWWWWWWWWW...",
    "..WWWWWWWWWWW...",
    ".WWWWWWWWWWWWWW.",
    "WWWWWWWWWWWWWWWW",
    "WWWWWWWWWWWWWWWW",
    "LLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLL",
    ".GGGGGGGGGGGGGG.",
    "................",
    "................",
    "................",
};
constexpr const char* kFog[16] = {
    "................",
    "................",
    "...LLLLLLLLLL...",
    ".LLLLLLLLLLLLLL.",
    "................",
    "GGGGGGGGGGGGGGGG",
    "................",
    "..WWWWWWWWWWWW..",
    "................",
    "GGGGGGGGGGGGGGGG",
    "................",
    "....LLLLLLLL....",
    "................",
    "................",
    "................",
    "................",
};
constexpr const char* kRain[16] = {
    "................",
    ".....WWWWW......",
    "...WWWWWWWWW....",
    "..WWWWWWWWWWW...",
    ".LLLLLLLLLLLLLL.",
    "LLLLLLLLLLLLLLLL",
    "GGGGGGGGGGGGGGGG",
    ".GGGGGGGGGGGGGG.",
    "................",
    "...B...B...B....",
    "...B...B...B....",
    "..C...C...C.....",
    "..B...B...B.....",
    ".B...B...B......",
    ".B...B...B......",
    "................",
};
constexpr const char* kSnow[16] = {
    "................",
    ".....WWWWW......",
    "...WWWWWWWWW....",
    "..WWWWWWWWWWW...",
    ".WWWWWWWWWWWWWW.",
    "LLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLL",
    ".GGGGGGGGGGGGGG.",
    "................",
    "..C....C....C...",
    ".CWC..CWC..CWC..",
    "..C....C....C...",
    "................",
    ".....C....C.....",
    "....CWC..CWC....",
    ".....C....C.....",
};
constexpr const char* kStorm[16] = {
    "................",
    ".....GGGGG......",
    "...GGGGGGGGG....",
    "..GGGGGGGGGGG...",
    ".GGGGGGGGGGGGGG.",
    "DDDDDDDDDDDDDDDD",
    "DDDDDDDDDDDDDDDD",
    ".DDDDDYYYDDDDDD.",
    "......YYY.......",
    ".....YYY........",
    "....YYYYYY......",
    "......YYO.......",
    ".....YYO........",
    ".....YO.........",
    "....YO..........",
    "................",
};

inline const char* const* picture(Icon icon) {
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

// Exact 8-bit colours, so what's drawn here is what the canvas shows.
inline uint16_t ink(char name) {
  switch (name) {
    case 'Y': return color(255, 219, 0);
    case 'O': return color(255, 146, 0);
    case 'P': return color(255, 255, 170);
    case 'B': return color(0, 146, 255);
    case 'C': return color(0, 255, 255);
    case 'L': return color(182, 219, 255);
    case 'G': return color(146, 146, 170);
    case 'D': return color(73, 73, 85);
    default: return color(255, 255, 255);
  }
}

// The colour for words beside the picture.
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

inline bool placeChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == ' ' || c == ',' || c == '.' || c == '\'' || c == '-';
}

// The bridge decides whether a place exists. This only stops what could never be one.
inline bool validPlace(const char* place) {
  if (!place) return false;
  const size_t length = strlen(place);
  if (length < 2 || length > kMaxPlaceChars || place[0] == ' ' || place[0] == ',' || place[0] == '.' ||
      place[0] == '\'' || place[0] == '-') return false;
  int letters = 0, digits = 0;
  for (size_t i = 0; i < length; ++i) {
    if (!placeChar(place[i])) return false;
    letters += (place[i] >= 'a' && place[i] <= 'z') || (place[i] >= 'A' && place[i] <= 'Z');
    digits += place[i] >= '0' && place[i] <= '9';
  }
  return letters >= 2 || digits >= 5;
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
