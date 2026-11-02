#pragma once
// Reads the position lines from the GPS in the Cap LoRa-1262. Characters go in one at a time, so
// the main loop never waits for a sentence. A line with a bad checksum is dropped, because a
// wrong position looks just like a right one.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace lab_gps {
constexpr int kLineChars = 96;
constexpr uint32_t kSilentMs = 3000;

struct Fix {
  bool valid = false;
  uint8_t satellites = 0;
  int32_t latE5 = 0, lonE5 = 0;   // Degrees times 100000.
  uint16_t speedKmhE1 = 0;        // Km/h times 10.
};

// "4230.5000" is 42 degrees and 30.5 minutes, and the degrees end two digits before the dot.
inline bool parseCoordinate(const char* text, char hemisphere, int32_t& out) {
  const char* dot = strchr(text, '.');
  if (!dot || dot - text < 3) return false;
  for (const char* p = text; p < dot; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  int32_t degrees = 0;
  for (const char* p = text; p < dot - 2; ++p) degrees = degrees * 10 + (*p - '0');
  const double minutes = atof(dot - 2);
  if (minutes < 0 || minutes >= 60 || degrees > 180) return false;
  out = degrees * 100000 + static_cast<int32_t>(minutes * 100000.0 / 60.0 + 0.5);
  if (hemisphere == 'S' || hemisphere == 'W') out = -out;
  else if (hemisphere != 'N' && hemisphere != 'E') return false;
  return true;
}

class Parser {
 public:
  Fix fix;
  uint32_t lines = 0;    // Good sentences of any kind, so a silent module can be told from no fix.
  uint32_t lastMs = 0;

  void feed(char c, uint32_t now) {
    if (c == '$') { mLength = 0; mOpen = true; return; }
    if (!mOpen) return;
    if (c == '\r' || c == '\n') {
      mOpen = false;
      mLine[mLength] = '\0';
      if (finish()) { ++lines; lastMs = now; }
      return;
    }
    if (mLength >= kLineChars - 1) { mOpen = false; return; }
    mLine[mLength++] = c;
  }
  bool hearing(uint32_t now) const { return lines > 0 && now - lastMs < kSilentMs; }

 private:
  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  bool finish() {
    char* star = strchr(mLine, '*');
    if (!star || star - mLine < 6 || mLength - (star - mLine) < 3) return false;
    const int high = hexValue(star[1]), low = hexValue(star[2]);
    if (high < 0 || low < 0) return false;
    uint8_t sum = 0;
    for (const char* p = mLine; p < star; ++p) sum ^= static_cast<uint8_t>(*p);
    if (sum != ((high << 4) | low)) return false;
    *star = '\0';
    // Fields are cut by hand, because strtok would skip the empty ones.
    const char* fields[16] = {};
    int count = 0;
    char* cursor = mLine;
    while (count < 16) {
      fields[count++] = cursor;
      char* comma = strchr(cursor, ',');
      if (!comma) break;
      *comma = '\0';
      cursor = comma + 1;
    }
    // The first two letters name the satellite system, which doesn't matter here.
    const char* kind = fields[0] + 2;
    if (strcmp(kind, "GGA") == 0 && count > 7) fix.satellites = static_cast<uint8_t>(atoi(fields[7]));
    else if (strcmp(kind, "RMC") == 0 && count > 7) readPosition(fields);
    return true;
  }
  void readPosition(const char* const* fields) {
    int32_t lat = 0, lon = 0;
    if (fields[2][0] != 'A' || !parseCoordinate(fields[3], fields[4][0], lat) ||
        !parseCoordinate(fields[5], fields[6][0], lon)) {
      fix.valid = false;
      return;
    }
    fix.valid = true;
    fix.latE5 = lat;
    fix.lonE5 = lon;
    fix.speedKmhE1 = static_cast<uint16_t>(atof(fields[7]) * 18.52 + 0.5);  // From knots.
  }
  char mLine[kLineChars] = {};
  int mLength = 0;
  bool mOpen = false;
};
}  // namespace lab_gps
