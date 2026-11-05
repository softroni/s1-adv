#pragma once
// Reads the position, quality and satellite lines from the GPS in the Cap LoRa-1262. Characters go
// in one at a time, so the main loop never waits for a sentence. A line with a bad checksum is
// dropped, because a wrong position looks just like a right one.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace lab_gps {
constexpr int kLineChars = 96;
constexpr uint32_t kSilentMs = 3000;
// Standing still, a GPS still reports a direction, and it wanders, so heading counts from here up.
constexpr uint16_t kHeadingMinKmhE1 = 30;
constexpr int32_t kFarMeters = 32000;  // Readings are 16-bit, so anything further reads as this.
constexpr int kSkySatellites = 24;      // More than a GPS and BeiDou receiver sees at once.
constexpr uint32_t kSkyStaleMs = 5000;  // A satellite it stops reporting has set, or is blocked.

struct Fix {
  bool valid = false;
  uint8_t satellites = 0;         // Used for the fix, from GGA.
  int32_t latE5 = 0, lonE5 = 0;   // Degrees times 100000.
  uint16_t speedKmhE1 = 0;        // Km/h times 10.
  int16_t courseDeg = -1;         // Direction of travel, 0 north to 359, or -1 when the GPS gives none.
  uint16_t hdopE1 = 0;            // How well the satellites are spread, times 10. 0 is unknown.
  bool hasAltitude = false;
  int32_t altitudeDm = 0;         // Above sea level, in tenths of a metre.
  bool moving() const { return valid && speedKmhE1 >= kHeadingMinKmhE1 && courseDeg >= 0; }
};

// One satellite in view, from GSV. Elevation and azimuth are -1 when the GPS doesn't know yet.
struct Satellite {
  uint32_t seenMs = 0;
  int16_t azimuth = -1;   // Degrees from north, clockwise.
  int8_t elevation = -1;  // Degrees above the horizon.
  uint8_t prn = 0;        // Its number within its system.
  uint8_t snr = 0;        // Signal in dB-Hz, 0 when it isn't being tracked.
  char system = 'P';      // The talker's second letter: P GPS, L GLONASS, A Galileo, B or D BeiDou.
};

// The flat-earth step from a to b in metres, north and east, which is well inside a metre over the
// distances a handheld radio reaches.
inline void stepBetween(int32_t latE5a, int32_t lonE5a, int32_t latE5b, int32_t lonE5b, double& north, double& east) {
  const double kMetersPerE5 = 1.11195;  // 1e-5 of a degree along a meridian.
  const double midLat = (latE5a + latE5b) / 2.0 / 100000.0 * 3.14159265358979 / 180.0;
  double dLon = static_cast<double>(lonE5b) - lonE5a;
  if (dLon > 18000000) dLon -= 36000000;  // The short way round, across the date line.
  else if (dLon < -18000000) dLon += 36000000;
  north = (static_cast<double>(latE5b) - latE5a) * kMetersPerE5;
  east = dLon * kMetersPerE5 * cos(midLat);
}
// Metres between two points, with no cap, for places far beyond what a reading holds.
inline int32_t metersApart(int32_t latE5a, int32_t lonE5a, int32_t latE5b, int32_t lonE5b) {
  double north = 0, east = 0;
  stepBetween(latE5a, lonE5a, latE5b, lonE5b, north, east);
  const double meters = sqrt(north * north + east * east);
  return meters >= 2.0e9 ? 2000000000 : static_cast<int32_t>(meters + 0.5);
}
// The same, capped at kFarMeters, for a Lab reading.
inline int32_t metersBetween(int32_t latE5a, int32_t lonE5a, int32_t latE5b, int32_t lonE5b) {
  const int32_t meters = metersApart(latE5a, lonE5a, latE5b, lonE5b);
  return meters >= kFarMeters ? kFarMeters : meters;
}
// Which way b lies from a, 0 north to 359 clockwise, or -1 when they're the same place.
inline int bearingDeg(int32_t latE5a, int32_t lonE5a, int32_t latE5b, int32_t lonE5b) {
  double north = 0, east = 0;
  stepBetween(latE5a, lonE5a, latE5b, lonE5b, north, east);
  if (north * north + east * east < 0.25) return -1;
  const int degrees = static_cast<int>(atan2(east, north) * 180.0 / 3.14159265358979 + 360.5);
  return degrees % 360;
}
// The eight points of the compass, for a bearing.
inline const char* compassPoint(int degrees) {
  static constexpr const char* kPoints[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return degrees < 0 ? "" : kPoints[((degrees % 360) + 22) % 360 / 45];
}
// Signal in dB-Hz as a word: 35 and up is what a clear sky gives.
inline const char* signalWord(uint8_t snr) {
  return snr >= 35 ? "strong" : snr >= 25 ? "good" : snr >= 15 ? "weak" : snr ? "faint" : "not tracked";
}
// HDOP as a word. Under 2 is good enough to trust to a few metres.
inline const char* spreadWord(uint16_t hdopE1) {
  return !hdopE1 ? "" : hdopE1 <= 10 ? "excellent" : hdopE1 <= 20 ? "good" : hdopE1 <= 50 ? "fair" : "poor";
}

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
  Satellite sky[kSkySatellites];  // What GSV last said of each satellite in view.

  void feed(char c, uint32_t now) {
    if (c == '$') { mLength = 0; mOpen = true; return; }
    if (!mOpen) return;
    if (c == '\r' || c == '\n') {
      mOpen = false;
      mLine[mLength] = '\0';
      if (finish(now)) { ++lines; lastMs = now; }
      return;
    }
    if (mLength >= kLineChars - 1) { mOpen = false; return; }
    mLine[mLength++] = c;
  }
  bool hearing(uint32_t now) const { return lines > 0 && now - lastMs < kSilentMs; }
  bool current(const Satellite& s, uint32_t now) const { return s.prn && now - s.seenMs < kSkyStaleMs; }
  // Satellites reported in the last few seconds, and how many of those are being tracked.
  int inView(uint32_t now) const {
    int count = 0;
    for (const Satellite& s : sky) count += current(s, now) ? 1 : 0;
    return count;
  }
  int tracked(uint32_t now) const {
    int count = 0;
    for (const Satellite& s : sky) count += current(s, now) && s.snr ? 1 : 0;
    return count;
  }

 private:
  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  bool finish(uint32_t now) {
    char* star = strchr(mLine, '*');
    if (!star || star - mLine < 6 || mLength - (star - mLine) < 3) return false;
    const int high = hexValue(star[1]), low = hexValue(star[2]);
    if (high < 0 || low < 0) return false;
    uint8_t sum = 0;
    for (const char* p = mLine; p < star; ++p) sum ^= static_cast<uint8_t>(*p);
    if (sum != ((high << 4) | low)) return false;
    *star = '\0';
    // Fields are cut by hand, because strtok would skip the empty ones. GSV has the most: 21.
    constexpr int kFields = 24;
    const char* fields[kFields] = {};
    int count = 0;
    char* cursor = mLine;
    while (count < kFields) {
      fields[count++] = cursor;
      char* comma = strchr(cursor, ',');
      if (!comma) break;
      *comma = '\0';
      cursor = comma + 1;
    }
    // The first two letters name the satellite system, which only matters for GSV.
    const char* kind = fields[0] + 2;
    if (strcmp(kind, "GGA") == 0 && count > 7) readQuality(fields, count);
    else if (strcmp(kind, "RMC") == 0 && count > 7) readPosition(fields);
    else if (strcmp(kind, "GSV") == 0 && count > 7) readSky(fields, count, fields[0][1], now);
    return true;
  }
  static bool isNumber(const char* text) {
    if (!text || !*text) return false;
    for (const char* p = text; *p; ++p) {
      if ((*p < '0' || *p > '9') && *p != '.' && !(p == text && *p == '-')) return false;
    }
    return true;
  }
  void readQuality(const char* const* fields, int count) {
    fix.satellites = static_cast<uint8_t>(atoi(fields[7]));
    const bool fixed = fields[6][0] && fields[6][0] != '0';
    const double hdop = count > 8 && isNumber(fields[8]) ? atof(fields[8]) : 0;
    fix.hdopE1 = fixed && hdop > 0 && hdop < 100 ? static_cast<uint16_t>(hdop * 10 + 0.5) : 0;
    fix.hasAltitude = fixed && count > 9 && isNumber(fields[9]);
    if (fix.hasAltitude) {
      const double meters = atof(fields[9]);
      fix.hasAltitude = meters > -1000 && meters < 20000;
      fix.altitudeDm = fix.hasAltitude ? static_cast<int32_t>(meters * 10 + (meters < 0 ? -0.5 : 0.5)) : 0;
    }
  }
  // Each GSV line lists up to four satellites as number, elevation, azimuth and signal. A satellite
  // keeps its slot, so a system's lines can come in any order, and the stalest slot is reused.
  void readSky(const char* const* fields, int count, char system, uint32_t now) {
    // Only whole groups: NMEA 4.10 adds a signal number after the last one.
    for (int at = 4; at + 3 < count; at += 4) {
      if (!isNumber(fields[at])) continue;
      const int prn = atoi(fields[at]);
      if (prn <= 0 || prn > 255) continue;
      Satellite* slot = nullptr;
      for (Satellite& s : sky) {
        if (s.prn == prn && s.system == system) { slot = &s; break; }
      }
      if (!slot) {
        slot = &sky[0];
        for (Satellite& s : sky) {
          if (!s.prn) { slot = &s; break; }
          if (now - s.seenMs > now - slot->seenMs) slot = &s;
        }
      }
      const int elevation = isNumber(fields[at + 1]) ? atoi(fields[at + 1]) : -1;
      const int azimuth = isNumber(fields[at + 2]) ? atoi(fields[at + 2]) : -1;
      const int snr = isNumber(fields[at + 3]) ? atoi(fields[at + 3]) : 0;
      slot->system = system;
      slot->prn = static_cast<uint8_t>(prn);
      slot->elevation = static_cast<int8_t>(elevation >= 0 && elevation <= 90 ? elevation : -1);
      slot->azimuth = static_cast<int16_t>(azimuth >= 0 && azimuth < 360 ? azimuth : -1);
      slot->snr = static_cast<uint8_t>(snr > 0 && snr < 100 ? snr : 0);
      slot->seenMs = now;
    }
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
    const double knots = atof(fields[7]);
    fix.speedKmhE1 = knots > 0 && knots < 3000 ? static_cast<uint16_t>(knots * 18.52 + 0.5) : 0;  // From knots.
    const double course = fields[8] && fields[8][0] ? atof(fields[8]) : -1;
    fix.courseDeg = course >= 0 && course < 360 ? static_cast<int16_t>(static_cast<int>(course + 0.5) % 360) : -1;
  }
  char mLine[kLineChars] = {};
  int mLength = 0;
  bool mOpen = false;
};
}  // namespace lab_gps
