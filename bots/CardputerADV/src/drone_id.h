#pragma once
// Drones: the Remote ID that drones broadcast (ASTM F3411 "Open Drone ID", which the FAA's rule
// accepts), as the Cardputer hears it over Wi-Fi. It comes in a beacon's vendor element (OUI
// FA:0B:BC, type 0x0D) or in a Wi-Fi Aware (NAN) service discovery frame for the service
// "org.opendroneid.remoteid". Either carries a counter and a pack of 25-byte messages: who the drone
// is, where it is and how high, and where it's flown from. The layouts follow the reference
// implementation, opendroneid-core-c. No hardware here: scripts/test_drones.py runs all of it.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace drone_id {
constexpr size_t kMessage = 25, kPackMax = 9;
constexpr int32_t kUnknown = INT32_MIN;
enum class Via : uint8_t { None, Beacon, Nan };

struct Drone {
  uint8_t mac[6] = {};
  char id[21] = "";          // A serial number, a registration or a session ID, as broadcast.
  uint8_t idType = 0, uaType = 0;
  bool located = false;      // Its position has come.
  uint8_t status = 0;        // 0 undeclared, 1 on the ground, 2 flying, 3 emergency, 4 Remote ID failing.
  int32_t latE7 = 0, lonE7 = 0;
  int32_t heightDm = kUnknown, altGeoDm = kUnknown, altBaroDm = kUnknown;  // Tenths of a metre.
  bool heightOverGround = false;  // Else over where it took off.
  int32_t speedCms = -1;     // Across the ground, cm/s; -1 unknown.
  int32_t climbCms = kUnknown;
  int16_t direction = -1;    // Degrees from north it's moving, -1 unknown.
  bool hasOperator = false;  // Where it's flown from (or took off).
  uint8_t operatorType = 0;  // 0 the takeoff point, 1 the pilot's live position, 2 a fixed place.
  int32_t opLatE7 = 0, opLonE7 = 0;
  char selfId[24] = "";      // The pilot's own words, like "Recreational".
  char operatorId[21] = "";
};

// --- Reading messages -----------------------------------------------------------------------
inline int32_t le32(const uint8_t* p) {
  return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
// Half metres above -1000 m; 0 is -1000 m, which means unknown.
inline int32_t altitudeDm(uint16_t encoded) { return encoded ? int32_t(encoded) * 5 - 10000 : kUnknown; }
inline bool positionValid(int32_t latE7, int32_t lonE7) {
  return !(latE7 == 0 && lonE7 == 0) && latE7 >= -900000000 && latE7 <= 900000000 && lonE7 >= -1800000000 && lonE7 <= 1800000000;
}
// Printable ASCII up to the first zero, with trailing spaces gone.
inline void text(char* out, size_t room, const uint8_t* in, size_t n) {
  size_t k = 0;
  for (size_t i = 0; i < n && in[i] && k + 1 < room; ++i) out[k++] = in[i] >= 32 && in[i] < 127 ? char(in[i]) : '?';
  while (k && out[k - 1] == ' ') --k;
  out[k] = 0;
}

// One 25-byte message into what's known of the drone. False for a type this doesn't read.
inline bool decodeMessage(const uint8_t* m, Drone& d) {
  switch (m[0] >> 4) {
    case 0: {  // Basic ID: [id type | aircraft type], then 20 characters.
      char id[21];
      text(id, sizeof(id), m + 2, 20);
      const uint8_t idType = m[1] >> 4, uaType = m[1] & 15;
      // A drone can send two: its serial number and a session ID. The serial is kept.
      if (id[0] && (!d.id[0] || idType == 1 || d.idType != 1)) {
        memcpy(d.id, id, sizeof(id));
        d.idType = idType;
      }
      if (uaType) d.uaType = uaType;
      return true;
    }
    case 1: {  // Location: [status | - | height type | east-west | speed scale], then the numbers.
      const uint8_t flags = m[1];
      d.status = flags >> 4;
      d.heightOverGround = flags & 0x04;
      const int direction = m[2] + ((flags & 0x02) ? 180 : 0);
      d.direction = int16_t(direction < 360 ? direction : -1);
      if (m[3] == 255) d.speedCms = -1;
      else d.speedCms = (flags & 0x01) ? int32_t(m[3]) * 75 + 255 * 25 : int32_t(m[3]) * 25;
      const int8_t climb = int8_t(m[4]);
      d.climbCms = climb >= 126 ? kUnknown : int32_t(climb) * 50;
      const int32_t lat = le32(m + 5), lon = le32(m + 9);
      if (positionValid(lat, lon)) { d.latE7 = lat; d.lonE7 = lon; d.located = true; }
      d.altBaroDm = altitudeDm(le16(m + 13));
      d.altGeoDm = altitudeDm(le16(m + 15));
      d.heightDm = altitudeDm(le16(m + 17));
      return true;
    }
    case 2: return true;  // Authentication: nothing to show.
    case 3: text(d.selfId, sizeof(d.selfId), m + 2, 23); return true;
    case 4: {  // System: where it's flown from, and the area of a group of drones.
      d.operatorType = m[1] & 3;
      const int32_t lat = le32(m + 2), lon = le32(m + 6);
      if (positionValid(lat, lon)) { d.opLatE7 = lat; d.opLonE7 = lon; d.hasOperator = true; }
      return true;
    }
    case 5: text(d.operatorId, sizeof(d.operatorId), m + 2, 20); return true;
    default: return false;
  }
}
// A message pack, or a lone message: how many messages were read, -1 when it's neither.
inline int decodePack(const uint8_t* p, size_t len, Drone& d) {
  if (!p || len < kMessage) return -1;
  if ((p[0] >> 4) != 0xF) return decodeMessage(p, d) ? 1 : -1;
  const uint8_t size = p[1], count = p[2];
  if (size != kMessage || !count || count > kPackMax || len < 3 + size_t(count) * kMessage) return -1;
  int read = 0;
  for (uint8_t i = 0; i < count; ++i) read += decodeMessage(p + 3 + size_t(i) * kMessage, d) ? 1 : 0;
  return read;
}

// --- Finding it in a Wi-Fi frame ------------------------------------------------------------
// An 802.11 management frame: frame control 2, duration 2, receiver 6, sender 6, BSSID 6, sequence 2.
constexpr size_t kHeader = 24;
constexpr uint8_t kBeacon = 0x80, kAction = 0xD0;
constexpr uint8_t kAstmOui[4] = {0xFA, 0x0B, 0xBC, 0x0D};         // ASD-STAN's OUI, Remote ID type.
constexpr uint8_t kNanHeader[6] = {0x04, 0x09, 0x50, 0x6F, 0x9A, 0x13};  // Public action, vendor, Wi-Fi Alliance, NAN.
constexpr uint8_t kServiceId[6] = {0x88, 0x69, 0x19, 0x9D, 0x92, 0x09};  // "org.opendroneid.remoteid", hashed.

// Cheap enough for the Wi-Fi driver's callback: whether a frame could hold Remote ID at all.
inline bool looksLikeRemoteId(const uint8_t* f, size_t len) {
  if (len < kHeader + 8) return false;
  if (f[0] == kAction) return len >= kHeader + 6 && !memcmp(f + kHeader, kNanHeader, 6);
  if (f[0] != kBeacon) return false;
  for (size_t i = kHeader + 12; i + 4 <= len; ++i)
    if (f[i] == 0xFA && !memcmp(f + i, kAstmOui, 4)) return true;
  return false;
}
// Where the message pack is in a beacon or NAN frame, and who sent it.
inline bool findPack(const uint8_t* f, size_t len, uint8_t mac[6], const uint8_t*& pack, size_t& packLen, Via& via) {
  via = Via::None;
  if (len < kHeader + 8) return false;
  memcpy(mac, f + 10, 6);
  if (f[0] == kBeacon) {
    // The fixed fields (timestamp 8, interval 2, capabilities 2), then elements: id, length, body.
    for (size_t at = kHeader + 12; at + 2 <= len;) {
      const uint8_t id = f[at], n = f[at + 1];
      if (at + 2 + n > len) return false;
      if (id == 221 && n >= 5 + kMessage && !memcmp(f + at + 2, kAstmOui, 4)) {
        pack = f + at + 7;  // After the OUI, its type and the message counter.
        packLen = n - 5;
        via = Via::Beacon;
        return true;
      }
      at += 2 + size_t(n);
    }
    return false;
  }
  if (f[0] != kAction || len < kHeader + 6 || memcmp(f + kHeader, kNanHeader, 6)) return false;
  // NAN attributes: id, a 2-byte length, body. The service descriptor's body: service ID 6,
  // instance, requestor, control, service info length, then the info: the counter and the pack.
  for (size_t at = kHeader + 6; at + 3 <= len;) {
    const uint8_t attribute = f[at];
    const size_t n = size_t(f[at + 1]) | size_t(f[at + 2]) << 8;
    if (attribute == 0x03 && n >= 10 && at + 3 + 10 <= len && !memcmp(f + at + 3, kServiceId, 6)) {
      const size_t infoLen = f[at + 3 + 9];
      if (infoLen < 1 + kMessage || at + 3 + 10 + infoLen > len) return false;
      pack = f + at + 3 + 10 + 1;
      packLen = infoLen - 1;
      via = Via::Nan;
      return true;
    }
    if (at + 3 + n > len) return false;
    at += 3 + n;
  }
  return false;
}

// --- The drones heard ------------------------------------------------------------------------
struct Heard {
  Drone d;
  uint32_t firstMs = 0, lastMs = 0, frames = 0;
  int8_t rssi = -127;
  uint8_t channel = 0;
  Via via = Via::None;
  bool alerted = false;
};
struct Table {
  static constexpr uint8_t kMax = 8;
  static constexpr uint32_t kForgetMs = 5UL * 60UL * 1000UL;
  Heard heard[kMax];
  uint8_t count = 0;

  int byMac(const uint8_t mac[6]) const {
    for (uint8_t i = 0; i < count; ++i) if (!memcmp(heard[i].d.mac, mac, 6)) return i;
    return -1;
  }
  int byId(const char* id) const {
    if (!id || !*id) return -1;
    for (uint8_t i = 0; i < count; ++i) if (!strcmp(heard[i].d.id, id)) return i;
    return -1;
  }
  // A frame's pack, from mac. The index of the drone it's about, or -1 when it said nothing.
  // A drone that changes its address keeps its place, found by its ID.
  int hear(const uint8_t mac[6], int8_t rssi, uint8_t channel, Via via, const uint8_t* pack, size_t len, uint32_t nowMs) {
    int i = byMac(mac);
    if (i < 0) {
      Drone fresh;
      memcpy(fresh.mac, mac, 6);
      if (decodePack(pack, len, fresh) <= 0) return -1;
      i = byId(fresh.id);
      if (i >= 0) {
        memcpy(heard[i].d.mac, mac, 6);
        decodePack(pack, len, heard[i].d);
      } else {
        if (count == kMax) {  // Full: the one heard longest ago goes.
          uint8_t oldest = 0;
          for (uint8_t k = 1; k < count; ++k) if (nowMs - heard[k].lastMs > nowMs - heard[oldest].lastMs) oldest = k;
          heard[oldest] = heard[count - 1];
          --count;
        }
        i = count++;
        heard[i] = Heard{};
        heard[i].d = fresh;
        heard[i].firstMs = nowMs;
      }
    } else if (decodePack(pack, len, heard[i].d) <= 0) {
      return -1;
    }
    Heard& h = heard[i];
    h.lastMs = nowMs;
    h.rssi = rssi;
    h.channel = channel;
    h.via = via;
    ++h.frames;
    return i;
  }
  void forget(uint32_t nowMs) {
    for (uint8_t i = 0; i < count;) {
      if (nowMs - heard[i].lastMs > kForgetMs) { heard[i] = heard[count - 1]; --count; }
      else ++i;
    }
  }
};

// --- Words for the screens -------------------------------------------------------------------
inline const char* typeName(uint8_t uaType) {
  switch (uaType) {
    case 1: return "Fixed-wing drone";
    case 2: return "Multirotor drone";
    case 3: return "Gyroplane";
    case 4: return "VTOL drone";
    case 5: return "Ornithopter";
    case 6: return "Glider";
    case 7: return "Kite";
    case 8: return "Free balloon";
    case 9: return "Tethered balloon";
    case 10: return "Airship";
    case 11: return "Parachute";
    case 12: return "Rocket";
    case 13: return "Tethered aircraft";
    case 14: return "Ground obstacle";
    default: return "Drone";
  }
}
inline const char* statusWord(uint8_t status) {
  switch (status) {
    case 1: return "on the ground";
    case 2: return "flying";
    case 3: return "EMERGENCY";
    case 4: return "Remote ID failing";
    default: return "";
  }
}
// The maker, from a serial number's first four characters (ANSI/CTA-2063-A). Only the common one.
inline const char* maker(const Drone& d) {
  return d.idType == 1 && !strncmp(d.id, "1581", 4) ? "DJI" : "";
}
// "DJI #3T2A" or "Drone #3T2A": the ID's end, or the address's when there's no ID yet. The # keeps
// Spotted from reading the ID as an airline's flight number.
inline void shortName(char* out, size_t room, const Drone& d) {
  const char* who = maker(d)[0] ? maker(d) : "Drone";
  const size_t n = strlen(d.id);
  if (n) snprintf(out, room, "%s #%s", who, d.id + (n > 4 ? n - 4 : 0));
  else snprintf(out, room, "%s #%02X%02X", who, d.mac[4], d.mac[5]);
}
inline const char* signalWord(int8_t rssi) { return rssi >= -60 ? "strong" : rssi >= -75 ? "good" : "weak"; }
// How high: over the ground or takeoff when it says, else nothing (its altitude above the sea
// isn't how high it looks).
inline bool heightMeters(const Drone& d, int32_t& meters) {
  if (d.heightDm == kUnknown) return false;
  meters = (d.heightDm + (d.heightDm < 0 ? -5 : 5)) / 10;
  return true;
}
}  // namespace drone_id
