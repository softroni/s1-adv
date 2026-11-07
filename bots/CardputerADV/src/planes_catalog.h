#pragma once
// Planes: what the bridge's planes routes answer (bridge/src/planes.mjs), read the way the device
// keeps it, and the words and numbers the screens show: which way to look, how high, how far, when
// the space station passes and what the kids have spotted. No hardware here, so
// scripts/test_planes.py runs all of it on the Mac.
//
// The bridge answers in lines of fields split by tabs, an empty text field sent as "-":
//   at     <place>
//   plane  <hex> <name> <type> <from> <from city> <to> <to city> <alt ft> <climb ft/min> <speed kt>
//          <track deg|-1> <distance m> <bearing deg> <elevation deg> <closest in s|-1> <closest m|-1> <category>
//   now    <unix> <lat x100> <lon x100> <alt km> <speed km/h> <over> <crew|-1>
//   pass   <start> <az> <el> <peak> <az> <el> <end> <az> <el> <magnitude x10> <into shadow 0|1> <az:el,...>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "podcast_catalog.h"
#include "units.h"

namespace planes {
constexpr uint8_t kMaxPlanes = 24, kMaxPasses = 8, kPathPoints = 9;
constexpr uint8_t kRanges = 3, kDefaultRange = 1;
constexpr int8_t kLookUpDegrees = 50;      // A plane this high in the sky is "right above you".

struct Plane {
  char hex[7] = "";
  char name[24] = "";
  char type[28] = "";
  char from[5] = "", fromCity[24] = "", to[5] = "", toCity[24] = "";
  int32_t altFt = 0, climbFpm = 0, closestS = -1, closestM = -1;
  uint32_t distM = 0;
  uint16_t speedKt = 0, bearing = 0;
  int16_t track = -1;
  int8_t elev = 0;
  char category[3] = "";
};
struct Sky {
  char place[26] = "";
  uint8_t count = 0;
  Plane planes[kMaxPlanes];
};

struct Pass {
  uint32_t start = 0, peak = 0, end = 0;
  int16_t startAz = 0, peakAz = 0, endAz = 0;
  int8_t startEl = 0, peakEl = 0, endEl = 0;
  int16_t magTenths = 0;
  bool shadow = false;
  uint8_t points = 0;
  int16_t pathAz[kPathPoints] = {};
  int8_t pathEl[kPathPoints] = {};
};
struct Station {
  bool known = false;
  uint32_t at = 0;
  int32_t latE2 = 0, lonE2 = 0, altKm = 0, speedKmh = 0;
  char over[32] = "";
  int16_t crew = -1;
};
struct Space {
  char place[26] = "";
  Station now;
  uint8_t count = 0;
  Pass passes[kMaxPasses];
};

// --- Reading the bridge's lines ------------------------------------------------------------
inline bool readInt(const char* s, int32_t& out, int32_t lo, int32_t hi) {
  if (!s || !*s) return false;
  const bool negative = *s == '-';
  uint32_t magnitude = 0;
  if (!podcast::readNumber(negative ? s + 1 : s, magnitude, 0x7FFFFFFFu)) return false;
  const int64_t value = negative ? -int64_t(magnitude) : int64_t(magnitude);
  if (value < lo || value > hi) return false;
  out = int32_t(value);
  return true;
}
// Text as shown: "-" is the bridge's empty field.
inline void readText(char* out, size_t room, const char* in) {
  if (!in || !strcmp(in, "-")) { if (room) out[0] = 0; return; }
  podcast::copyText(out, room, in);
}
inline bool hexValid(const char* s) {
  size_t n = 0;
  for (; s[n]; ++n) if (n >= 6 || !((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f'))) return false;
  return n == 6;
}

inline bool parsePlane(char** f, Plane& p) {
  int32_t alt, climb, speed, track, dist, bearing, elev, closeS, closeM;
  if (!hexValid(f[1]) || !readInt(f[8], alt, -2000, 200000) || !readInt(f[9], climb, -40000, 40000) ||
      !readInt(f[10], speed, 0, 3000) || !readInt(f[11], track, -1, 360) || !readInt(f[12], dist, 0, 1000000) ||
      !readInt(f[13], bearing, 0, 360) || !readInt(f[14], elev, -90, 90) || !readInt(f[15], closeS, -1, 86400) ||
      !readInt(f[16], closeM, -1, 1000000))
    return false;
  p = Plane{};
  strcpy(p.hex, f[1]);
  readText(p.name, sizeof(p.name), f[2]);
  readText(p.type, sizeof(p.type), f[3]);
  readText(p.from, sizeof(p.from), f[4]);
  readText(p.fromCity, sizeof(p.fromCity), f[5]);
  readText(p.to, sizeof(p.to), f[6]);
  readText(p.toCity, sizeof(p.toCity), f[7]);
  readText(p.category, sizeof(p.category), f[17]);
  if (!p.name[0]) { for (int i = 0; i < 6; ++i) p.name[i] = char(p.hex[i] >= 'a' ? p.hex[i] - 32 : p.hex[i]); p.name[6] = 0; }
  p.altFt = alt;
  p.climbFpm = climb;
  p.speedKt = uint16_t(speed);
  p.track = int16_t(track == 360 ? 0 : track);
  p.distM = uint32_t(dist);
  p.bearing = uint16_t(bearing % 360);
  p.elev = int8_t(elev);
  p.closestS = closeS;
  p.closestM = closeM;
  return true;
}
// False when the answer isn't the bridge's (no "at" line); bad plane lines are left out.
inline bool parseNear(const char* text, size_t len, Sky& out) {
  out.place[0] = 0;
  out.count = 0;
  bool at = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[18];
    const size_t n = podcast::splitFields(line, f, 18);
    if (n == 2 && !strcmp(f[0], "at")) { readText(out.place, sizeof(out.place), f[1]); at = true; }
    else if (n == 18 && !strcmp(f[0], "plane") && out.count < kMaxPlanes && parsePlane(f, out.planes[out.count])) ++out.count;
  });
  return at;
}

inline bool parsePath(const char* text, Pass& p) {
  p.points = 0;
  if (!text || !strcmp(text, "-")) return true;
  const char* s = text;
  while (*s && p.points < kPathPoints) {
    char piece[16];
    size_t n = 0;
    while (s[n] && s[n] != ',' && n + 1 < sizeof(piece)) { piece[n] = s[n]; ++n; }
    if (s[n] && s[n] != ',') return false;
    piece[n] = 0;
    char* colon = strchr(piece, ':');
    int32_t az, el;
    if (!colon) return false;
    *colon = 0;
    if (!readInt(piece, az, 0, 360) || !readInt(colon + 1, el, -90, 90)) return false;
    p.pathAz[p.points] = int16_t(az % 360);
    p.pathEl[p.points] = int8_t(el);
    ++p.points;
    s += n;
    if (*s == ',') ++s;
  }
  return true;
}
inline bool parsePass(char** f, Pass& p) {
  int32_t v[11];
  static const int32_t lo[11] = {0, 0, -90, 0, 0, -90, 0, 0, -90, -150, 0};
  static const int32_t hi[11] = {0x7FFFFFFF, 360, 90, 0x7FFFFFFF, 360, 90, 0x7FFFFFFF, 360, 90, 150, 1};
  for (int i = 0; i < 11; ++i) if (!readInt(f[1 + i], v[i], lo[i], hi[i])) return false;
  if (!(v[0] <= v[3] && v[3] <= v[6])) return false;
  p = Pass{};
  p.start = uint32_t(v[0]); p.startAz = int16_t(v[1] % 360); p.startEl = int8_t(v[2]);
  p.peak = uint32_t(v[3]); p.peakAz = int16_t(v[4] % 360); p.peakEl = int8_t(v[5]);
  p.end = uint32_t(v[6]); p.endAz = int16_t(v[7] % 360); p.endEl = int8_t(v[8]);
  p.magTenths = int16_t(v[9]);
  p.shadow = v[10] == 1;
  return parsePath(f[12], p);
}
inline bool parseIss(const char* text, size_t len, Space& out) {
  out.place[0] = 0;
  out.now = Station{};
  out.count = 0;
  bool at = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[13];
    const size_t n = podcast::splitFields(line, f, 13);
    if (n == 2 && !strcmp(f[0], "at")) { readText(out.place, sizeof(out.place), f[1]); at = true; return; }
    if (n == 8 && !strcmp(f[0], "now")) {
      int32_t unix = 0, lat, lon, alt, speed, crew;
      if (readInt(f[1], unix, 0, 0x7FFFFFFF) && readInt(f[2], lat, -9000, 9000) && readInt(f[3], lon, -18000, 18000) &&
          readInt(f[4], alt, 0, 2000) && readInt(f[5], speed, 0, 60000) && readInt(f[7], crew, -1, 99)) {
        Station& s = out.now;
        s.known = true; s.at = uint32_t(unix); s.latE2 = lat; s.lonE2 = lon; s.altKm = alt; s.speedKmh = speed;
        s.crew = int16_t(crew);
        readText(s.over, sizeof(s.over), f[6]);
      }
      return;
    }
    if (n == 13 && !strcmp(f[0], "pass") && out.count < kMaxPasses && parsePass(f, out.passes[out.count])) ++out.count;
  });
  return at;
}

// --- Where to ask ----------------------------------------------------------------------------
constexpr const char kBase[] = "/api/cardputer/planes/";
constexpr const char kTicketPath[] = "/api/cardputer/planes/ticket";
// `where` is "lat=..&lon=.." from the GPS or "place=<escaped>" from the saved city.
inline bool nearPath(char* out, size_t room, const char* where, uint32_t rangeM) {
  if (!where || !*where) return false;
  const int len = snprintf(out, room, "%snear?%s&r=%lu", kBase, where, static_cast<unsigned long>(rangeM));
  return len > 0 && size_t(len) < room;
}
inline bool issPath(char* out, size_t room, const char* where) {
  if (!where || !*where) return false;
  const int len = snprintf(out, room, "%siss?%s", kBase, where);
  return len > 0 && size_t(len) < room;
}
inline bool placeQuery(char* out, size_t room, const char* place) {
  if (!place || !*place || room < 8) return false;
  memcpy(out, "place=", 7);
  return podcast::putEscaped(out, room, 6, place) > 6;
}
// The home door, as the bridge answers its ticket route:
//   ticket <port> /planes/<32 hex>     and a   lan <ip> <prefix>   for each home address
inline bool ticketPathValid(const char* path) {
  static const char kStart[] = "/planes/";
  if (strncmp(path, kStart, sizeof(kStart) - 1)) return false;
  const char* id = path + sizeof(kStart) - 1;
  size_t n = 0;
  for (; id[n]; ++n) if (n >= 32 || !((id[n] >= '0' && id[n] <= '9') || (id[n] >= 'a' && id[n] <= 'f'))) return false;
  return n == 32;
}
// False when the answer isn't a ticket at all, so an old bridge reads as having none.
inline bool parseTicket(const char* text, size_t len, podcast::HomeTicket& out) {
  out = podcast::HomeTicket{};
  bool none = false, ticket = false, bad = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[3];
    const size_t n = podcast::splitFields(line, f, 3);
    uint32_t number = 0, ip = 0;
    if (n == 1 && !strcmp(f[0], "none")) none = true;
    else if (n == 3 && !strcmp(f[0], "ticket") && podcast::readNumber(f[1], number, 65535) && number &&
             ticketPathValid(f[2]) && strlen(f[2]) < sizeof(out.path)) {
      out.port = uint16_t(number); strcpy(out.path, f[2]); ticket = true;
    } else if (n == 3 && !strcmp(f[0], "lan") && podcast::parseIpv4(f[1], ip) && podcast::readNumber(f[2], number, 30) && number >= 8) {
      if (out.count < 4) { out.ip[out.count] = ip; out.prefix[out.count] = uint8_t(number); ++out.count; }
    } else bad = true;
  });
  if (bad && !ticket) return false;
  out.none = none || !ticket || !out.count;
  return none || ticket;
}
// The same request over the home door: the HTTPS path with its base swapped for the ticket's.
inline bool homePath(char* out, size_t room, const podcast::HomeTicket& t, const char* httpsPath) {
  if (t.none || !ticketPathValid(t.path) || !httpsPath || strncmp(httpsPath, kBase, sizeof(kBase) - 1)) return false;
  const int len = snprintf(out, room, "%s/%s", t.path, httpsPath + sizeof(kBase) - 1);
  return len > 0 && size_t(len) < room;
}

// --- Words for the screens -------------------------------------------------------------------
inline int normalDeg(int deg) { deg %= 360; return deg < 0 ? deg + 360 : deg; }
inline const char* compass(int deg) {
  static const char* const kPoints[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return kPoints[((normalDeg(deg) * 2 + 45) / 90) % 8];
}
inline const char* compassWord(int deg) {
  static const char* const kWords[] = {"NORTH", "NORTH-EAST", "EAST", "SOUTH-EAST", "SOUTH", "SOUTH-WEST", "WEST", "NORTH-WEST"};
  return kWords[((normalDeg(deg) * 2 + 45) / 90) % 8];
}
// A fist held at arm's length covers about ten degrees of sky, which children can count.
inline int fists(int elevDeg) { return elevDeg < 5 ? 0 : (elevDeg + 5) / 10; }
inline void heightWords(char* out, size_t room, int elevDeg) {
  const int n = fists(elevDeg);
  if (elevDeg >= 80) snprintf(out, room, "straight up!");
  else if (!n) snprintf(out, room, "low, near the horizon");
  else snprintf(out, room, "%d fist%s above the horizon", n, n == 1 ? "" : "s");
}
inline void heightShort(char* out, size_t room, int elevDeg) {
  const int n = fists(elevDeg);
  if (elevDeg >= 80) snprintf(out, room, "straight up");
  else if (!n) snprintf(out, room, "low");
  else snprintf(out, room, "%d fist%s up", n, n == 1 ? "" : "s");
}

// 34000 as "34,000".
inline void grouped(char* out, size_t room, int32_t value) {
  char digits[16];
  const bool negative = value < 0;
  snprintf(digits, sizeof(digits), "%ld", static_cast<long>(negative ? -int64_t(value) : int64_t(value)));
  const size_t n = strlen(digits);
  char text[24];
  size_t at = 0;
  if (negative) text[at++] = '-';
  for (size_t i = 0; i < n; ++i) {
    if (i && (n - i) % 3 == 0) text[at++] = ',';
    text[at++] = digits[i];
  }
  text[at] = 0;
  snprintf(out, room, "%s", text);
}
inline int32_t feetToMeters(int32_t ft) { return units::rounded(ft * 0.3048); }
// "34,000 ft" or "10,363 m".
inline void altitudeText(char* out, size_t room, int32_t altFt, units::System system) {
  char number[16];
  const bool us = system == units::System::US;
  grouped(number, sizeof(number), us ? altFt : feetToMeters(altFt));
  snprintf(out, room, "%s %s", number, us ? "ft" : "m");
}
// The list's: "34k ft" up high, "5,200 ft" and "800 ft" lower down; "10k m", "1,580 m".
inline void altitudeShort(char* out, size_t room, int32_t altFt, units::System system) {
  const bool us = system == units::System::US;
  const int32_t value = us ? altFt : feetToMeters(altFt);
  if (value >= 9950) { snprintf(out, room, "%ldk %s", static_cast<long>((value + 500) / 1000), us ? "ft" : "m"); return; }
  char number[16];
  const int32_t step = us ? 100 : 10;
  grouped(number, sizeof(number), value < 0 ? 0 : (value + step / 2) / step * step);
  snprintf(out, room, "%s %s", number, us ? "ft" : "m");
}
inline void speedText(char* out, size_t room, uint32_t knots, units::System system) {
  if (system == units::System::US) snprintf(out, room, "%ld mph", static_cast<long>(units::rounded(knots * 1.15078)));
  else snprintf(out, room, "%ld km/h", static_cast<long>(units::rounded(knots * 1.852)));
}
// The list's: "12mi", "0.6mi"; "19km", "0.9km".
inline void distanceShort(char* out, size_t room, uint32_t meters, units::System system) {
  const double unit = system == units::System::US ? 1609.344 : 1000.0;
  const char* name = system == units::System::US ? "mi" : "km";
  const double value = meters / unit;
  if (value >= 0.95) { snprintf(out, room, "%ld%s", static_cast<long>(units::rounded(value)), name); return; }
  const int tenths = int(value * 10 + 0.5);
  snprintf(out, room, "0.%d%s", tenths < 1 ? 1 : tenths, name);
}
inline const char* climbWord(int32_t climbFpm, int32_t altFt) {
  if (climbFpm >= 300) return "climbing";
  if (climbFpm <= -300) return altFt < 12000 ? "coming down" : "descending";
  return "level";
}
inline void closestText(char* out, size_t room, int32_t closestS) {
  if (closestS < 0) snprintf(out, room, "moving away");
  else if (closestS < 60) snprintf(out, room, "closest now");
  else snprintf(out, room, "closest in %ld min", static_cast<long>((closestS + 30) / 60));
}
inline bool isDrone(const Plane& p) { return !strcmp(p.category, "B6"); }
inline bool isHelicopter(const Plane& p) { return !strcmp(p.category, "A7"); }
inline bool wantsLookUp(const Plane& p) { return p.elev >= kLookUpDegrees && p.altFt > 0; }

// The rings, in the owner's units: 10, 25 or 50 miles; 15, 40 or 80 km.
inline uint32_t rangeMeters(uint8_t step, units::System system) {
  static const uint16_t kMiles[kRanges] = {10, 25, 50}, kKm[kRanges] = {15, 40, 80};
  step = step < kRanges ? step : kDefaultRange;
  return system == units::System::US ? uint32_t(kMiles[step] * 1609.344 + 0.5) : uint32_t(kKm[step]) * 1000u;
}
inline void rangeLabel(char* out, size_t room, uint8_t step, units::System system) {
  static const uint16_t kMiles[kRanges] = {10, 25, 50}, kKm[kRanges] = {15, 40, 80};
  step = step < kRanges ? step : kDefaultRange;
  if (system == units::System::US) snprintf(out, room, "%umi", unsigned(kMiles[step]));
  else snprintf(out, room, "%ukm", unsigned(kKm[step]));
}

// Where a plane sits on the round map: north up, the ring at rangeM. Beyond it, on the ring.
inline void mapPoint(uint32_t distM, int bearingDeg, uint32_t rangeM, int radius, int& dx, int& dy) {
  double r = rangeM ? double(distM) / double(rangeM) : 0;
  if (r > 1) r = 1;
  const double a = normalDeg(bearingDeg) * 3.14159265358979 / 180.0;
  dx = int(lround(sin(a) * r * radius));
  dy = int(lround(-cos(a) * r * radius));
}
// Where the station is in the sky, seen from below: the ring is the horizon, the middle overhead.
inline void skyPoint(int azDeg, int elDeg, int radius, int& dx, int& dy) {
  const int el = elDeg < 0 ? 0 : elDeg > 90 ? 90 : elDeg;
  const double r = (90 - el) / 90.0;
  const double a = normalDeg(azDeg) * 3.14159265358979 / 180.0;
  dx = int(lround(sin(a) * r * radius));
  dy = int(lround(-cos(a) * r * radius));
}

// --- The space station -------------------------------------------------------------------
// Brighter is a lower magnitude: four stars for a pass brighter than any star.
inline uint8_t stars(int16_t magTenths) {
  return magTenths <= -30 ? 4 : magTenths <= -20 ? 3 : magTenths <= -10 ? 2 : 1;
}
inline uint32_t minutesLong(const Pass& p) {
  const uint32_t s = p.end > p.start ? p.end - p.start : 0;
  return s < 60 ? 1 : (s + 30) / 60;
}
// Once round the Earth, from the station's height.
inline uint32_t orbitMinutes(int32_t altKm) {
  const double a = (6371.0 + (altKm > 0 ? altKm : 420)) * 1000.0;
  return uint32_t(2 * 3.14159265358979 * sqrt(a * a * a / 3.986004418e14) / 60.0 + 0.5);
}
// "Tonight", "Today", "Tomorrow", a weekday this week, then "Nov 16".
inline void dayWord(char* out, size_t room, const tm& now, const tm& at) {
  static const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  tm a = now, b = at;
  a.tm_hour = b.tm_hour = 12; a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0; a.tm_isdst = b.tm_isdst = -1;
  const long days = lround(difftime(mktime(&b), mktime(&a)) / 86400.0);
  if (days == 0) snprintf(out, room, "%s", at.tm_hour >= 15 ? "Tonight" : "Today");
  else if (days == 1) snprintf(out, room, "Tomorrow");
  else if (days > 1 && days < 7) snprintf(out, room, "%s", kDays[at.tm_wday % 7]);
  else snprintf(out, room, "%s %d", kMonths[at.tm_mon % 12], at.tm_mday);
}
// "6:12 pm" for a twelve-hour clock, "18:12" for twenty-four.
inline void clockText(char* out, size_t room, const tm& at, bool twelveHour) {
  if (!twelveHour) { snprintf(out, room, "%02d:%02d", at.tm_hour, at.tm_min); return; }
  const int h = at.tm_hour % 12 ? at.tm_hour % 12 : 12;
  snprintf(out, room, "%d:%02d %s", h, at.tm_min, at.tm_hour < 12 ? "am" : "pm");
}
// "in 16 min", "in 2 h 5 min", "up now!", "over".
inline void untilText(char* out, size_t room, uint32_t now, const Pass& p) {
  if (now >= p.end) { snprintf(out, room, "over"); return; }
  if (now >= p.start) { snprintf(out, room, "up now!"); return; }
  const uint32_t min = (p.start - now + 59) / 60;
  if (min < 60) snprintf(out, room, "in %lu min", static_cast<unsigned long>(min));
  else if (min < 24 * 60) snprintf(out, room, "in %lu h %lu min", static_cast<unsigned long>(min / 60), static_cast<unsigned long>(min % 60));
  else snprintf(out, room, "in %lu days", static_cast<unsigned long>((min + 12 * 60) / (24 * 60)));
}
// The first pass not yet over, or -1.
inline int nextPass(const Space& s, uint32_t now) {
  for (uint8_t i = 0; i < s.count; ++i) if (s.passes[i].end > now) return i;
  return -1;
}

// A reminder keeps the pass it's for, so its screen works with nothing fetched:
//   <remind at> <start> <peak> <end> <start az> <peak az> <peak el> <end az> <into shadow>
constexpr uint32_t kRemindBeforeS = 10 * 60;
inline void formatReminder(char* out, size_t room, uint32_t remindAt, const Pass& p) {
  snprintf(out, room, "%lu %lu %lu %lu %d %d %d %d %d", static_cast<unsigned long>(remindAt), static_cast<unsigned long>(p.start),
           static_cast<unsigned long>(p.peak), static_cast<unsigned long>(p.end), p.startAz, p.peakAz, p.peakEl, p.endAz, p.shadow ? 1 : 0);
}
inline bool parseReminder(const char* text, uint32_t& remindAt, Pass& p) {
  unsigned long at, start, peak, end;
  int startAz, peakAz, peakEl, endAz, shadow;
  if (!text || sscanf(text, "%lu %lu %lu %lu %d %d %d %d %d", &at, &start, &peak, &end, &startAz, &peakAz, &peakEl, &endAz, &shadow) != 9)
    return false;
  if (!(start <= peak && peak <= end) || startAz < 0 || startAz >= 360 || peakAz < 0 || peakAz >= 360 || endAz < 0 ||
      endAz >= 360 || peakEl < 0 || peakEl > 90)
    return false;
  p = Pass{};
  remindAt = uint32_t(at);
  p.start = uint32_t(start); p.peak = uint32_t(peak); p.end = uint32_t(end);
  p.startAz = int16_t(startAz); p.peakAz = int16_t(peakAz); p.peakEl = int8_t(peakEl); p.endAz = int16_t(endAz);
  p.shadow = shadow == 1;
  return true;
}

// --- What the kids have spotted ----------------------------------------------------------------
// /planes/spotted.txt, a line each:  <unix> <hex> <name> <type> <alt ft>
struct Sighting {
  uint32_t at = 0;
  char hex[7] = "";
  char name[24] = "";
  char type[28] = "";
  int32_t altFt = 0;
};
inline size_t formatSighting(char* out, size_t room, uint32_t at, const Plane& p) {
  const int len = snprintf(out, room, "%lu\t%s\t%s\t%s\t%ld\n", static_cast<unsigned long>(at), p.hex, p.name[0] ? p.name : "-",
                           p.type[0] ? p.type : "-", static_cast<long>(p.altFt));
  return len > 0 && size_t(len) < room ? size_t(len) : 0;
}
inline bool parseSighting(char* line, Sighting& s) {
  char* f[5];
  int32_t at, alt;
  if (podcast::splitFields(line, f, 5) != 5 || !readInt(f[0], at, 1, 0x7FFFFFFF) || !hexValid(f[1]) ||
      !readInt(f[4], alt, -2000, 200000))
    return false;
  s = Sighting{};
  s.at = uint32_t(at);
  strcpy(s.hex, f[1]);
  readText(s.name, sizeof(s.name), f[2]);
  readText(s.type, sizeof(s.type), f[3]);
  s.altFt = alt;
  return true;
}
// "United 1534" is United's; "N512AB" is nobody's.
inline bool airlineOf(const char* name, char* out, size_t room) {
  const char* space = strrchr(name, ' ');
  if (!space || space == name || !space[1]) return false;
  for (const char* p = space + 1; *p; ++p) if (*p < '0' || *p > '9') return false;
  const size_t n = size_t(space - name);
  if (n + 1 > room) return false;
  memcpy(out, name, n);
  out[n] = 0;
  return true;
}

// Names counted as they're read, for the Spotted page: up to kNames different ones.
struct Tally {
  static constexpr uint8_t kNames = 48;
  char names[kNames][28] = {};
  uint16_t counts[kNames] = {};
  uint32_t first[kNames] = {};
  uint8_t count = 0;
  void add(const char* name, uint32_t at) {
    if (!name || !*name) return;
    for (uint8_t i = 0; i < count; ++i)
      if (!strcmp(names[i], name)) { ++counts[i]; if (at < first[i]) first[i] = at; return; }
    if (count >= kNames) return;
    podcast::copyText(names[count], sizeof(names[count]), name);
    counts[count] = 1;
    first[count] = at;
    ++count;
  }
  int top() const {
    int best = -1;
    for (uint8_t i = 0; i < count; ++i) if (best < 0 || counts[i] > counts[best]) best = i;
    return best;
  }
};
struct Stats {
  uint32_t total = 0;
  Tally airlines, types;
  Sighting highest, lowest, last;
  void add(const Sighting& s) {
    ++total;
    char airline[24];
    if (airlineOf(s.name, airline, sizeof(airline))) airlines.add(airline, s.at);
    types.add(s.type, s.at);
    if (total == 1 || s.altFt > highest.altFt) highest = s;
    if (total == 1 || s.altFt < lowest.altFt) lowest = s;
    if (s.at >= last.at) last = s;
  }
  // The type first seen most recently: what's newest in the collection.
  int newestType() const {
    int best = -1;
    for (uint8_t i = 0; i < types.count; ++i) if (best < 0 || types.first[i] > types.first[best]) best = i;
    return best;
  }
};
}  // namespace planes
