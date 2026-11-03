#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Radio Scan's sums, kept apart from the radio so they can be tested anywhere. The scanner only
// ever listens: nothing here builds anything to send.
namespace s1_scan {
constexpr float kStepMHz = 0.25f;       // one LongFast channel wide, the receiver's width in a sweep
constexpr float kMinSpanMHz = 8.0f;     // EU_868 is a single 0.25 MHz channel, so show its neighbours
constexpr uint8_t kMaxPoints = 112;     // 28 MHz at 0.25 MHz, two pixels each across the chart
constexpr int16_t kNone = INT16_MIN;    // a point not read yet, or a reading that failed

// Where a sweep reads: the region's band, widened to kMinSpanMHz around its centre if narrower.
struct Plan {
  float start = 0, step = kStepMHz;
  uint8_t points = 0;
  float frequency(uint8_t i) const { return start + step * (float(i) + 0.5f); }
  float end() const { return start + step * float(points); }
  // The point a frequency falls in, or points when it's outside the sweep.
  uint8_t pointAt(float mhz) const {
    if (!points || !(mhz >= start) || !(mhz < end())) return points;
    return uint8_t(std::min<float>(float(points - 1), std::floor((mhz - start) / step)));
  }
};
inline Plan planFor(float bandStart, float bandEnd) {
  float span = bandEnd - bandStart;
  const float centre = (bandStart + bandEnd) / 2;
  if (!(span >= kMinSpanMHz)) span = kMinSpanMHz;  // also catches a band that makes no sense
  uint32_t points = uint32_t(std::ceil(span / kStepMHz - 0.001f));
  points = std::min<uint32_t>(std::max<uint32_t>(points, 1), kMaxPoints);
  Plan plan;
  plan.points = uint8_t(points);
  plan.step = span / float(points);
  plan.start = centre - span / 2;
  return plan;
}

inline int16_t dbmFrom(float reading) {
  // A reading of 0 is what a failed SPI read gives, never a real signal at the antenna.
  if (!std::isfinite(reading) || reading > -1.0f || reading < -200) return kNone;
  return int16_t(std::lround(reading));
}

// The latest reading at each point, and a peak that holds a burst on screen and fades with time.
struct Sweep {
  int16_t now[kMaxPoints];
  int16_t peak[kMaxPoints];
  uint8_t next = 0;
  uint32_t passes = 0;
  Sweep() { reset(); }
  void reset() {
    std::fill(now, now + kMaxPoints, kNone);
    std::fill(peak, peak + kMaxPoints, kNone);
    next = 0;
    passes = 0;
  }
  // Records the point due next and says whether that finished a pass.
  bool record(const Plan& plan, int16_t dbm) {
    if (!plan.points) return false;
    if (next >= plan.points) next = 0;
    now[next] = dbm;
    if (dbm != kNone && (peak[next] == kNone || dbm > peak[next])) peak[next] = dbm;
    if (++next < plan.points) return false;
    next = 0;
    ++passes;
    return true;
  }
  // Lowers every peak by db, never below what that point reads now.
  void fade(const Plan& plan, int16_t db) {
    for (uint8_t i = 0; i < plan.points; ++i) {
      if (peak[i] == kNone) continue;
      const int16_t lowered = int16_t(peak[i] - db);
      peak[i] = now[i] != kNone && lowered < now[i] ? now[i] : lowered;
    }
  }
  // The noise floor: the middle reading of the last pass, so a few busy channels don't move it.
  int16_t floor(const Plan& plan) const {
    int16_t sorted[kMaxPoints];
    uint8_t n = 0;
    for (uint8_t i = 0; i < plan.points; ++i) if (now[i] != kNone) sorted[n++] = now[i];
    if (!n) return kNone;
    std::nth_element(sorted, sorted + n / 2, sorted + n);
    return sorted[n / 2];
  }
  // The point with the highest peak, or plan.points if nothing has been read.
  uint8_t strongest(const Plan& plan) const {
    uint8_t best = plan.points;
    for (uint8_t i = 0; i < plan.points; ++i)
      if (peak[i] != kNone && (best == plan.points || peak[i] > peak[best])) best = i;
    return best;
  }
};

// The 16 bytes in front of every Meshtastic packet, sent unencrypted.
constexpr size_t kHeaderBytes = 16;
struct Header {
  uint32_t to = 0, from = 0, id = 0;
  uint8_t hopLimit = 0, hopStart = 0, channel = 0;
  bool viaMqtt = false;
};
inline uint32_t le32(const uint8_t* b) {
  return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
inline bool parseHeader(const uint8_t* bytes, size_t length, Header& header) {
  if (!bytes || length < kHeaderBytes) return false;
  header.to = le32(bytes);
  header.from = le32(bytes + 4);
  header.id = le32(bytes + 8);
  header.hopLimit = bytes[12] & 0x07;
  header.viaMqtt = (bytes[12] & 0x10) != 0;
  header.hopStart = uint8_t(bytes[12] >> 5);
  header.channel = bytes[13];
  return true;
}
// Hops a packet has made so far, or -1 when the sender's firmware is too old to say.
inline int8_t hopsTaken(const Header& header) {
  if (!header.hopStart || header.hopStart < header.hopLimit) return -1;
  return int8_t(header.hopStart - header.hopLimit);
}

struct Heard {
  uint32_t from = 0, at = 0;
  int16_t rssi = 0, snrTenths = 0;
  int8_t hops = -1;
  bool publicChannel = false;
};
constexpr size_t kRecent = 4, kNodes = 32;
// What listening has picked up: the newest packets first, a count of senders, and damaged ones.
struct Listen {
  Heard recent[kRecent];
  size_t recentCount = 0;
  uint32_t nodes[kNodes] = {};
  size_t nodeCount = 0;
  bool moreNodes = false;
  uint32_t packets = 0, damaged = 0, lastAt = 0;
  void reset() { *this = Listen{}; }
  void heard(const Heard& packet) {
    for (size_t i = std::min(recentCount, kRecent - 1); i > 0; --i) recent[i] = recent[i - 1];
    recent[0] = packet;
    recentCount = std::min(recentCount + 1, kRecent);
    ++packets;
    lastAt = packet.at;
    if (!packet.from || packet.from == UINT32_MAX) return;
    for (size_t i = 0; i < nodeCount; ++i) if (nodes[i] == packet.from) return;
    if (nodeCount < kNodes) nodes[nodeCount++] = packet.from;
    else moreNodes = true;
  }
  void damagedPacket(uint32_t now) {
    ++damaged;
    lastAt = now;
  }
};

// What the scanner is doing. It lives in the radio session, so it goes when the radio does.
struct Scanner {
  bool started = false, listening = false;
  uint8_t preset = 0;  // index into s1_mesh::kScanPresets
  uint8_t publicHash = 0;
  Plan plan;
  Sweep sweep;
  Listen listen;
  uint32_t fadeAt = 0;
  uint16_t failures = 0;  // radio calls in a row that failed
};
}  // namespace s1_scan
