#pragma once
// What Mesh heard of other radios: who, how well, over how many hops, and where they said they
// were. It stays in memory after Mesh closes, so the Lab's GPS screen can show the mesh around you
// once the radio is off. It's never saved, logged or sent anywhere, because other people's
// positions aren't this Cardputer's to keep.
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "mesh_leaf_state.h"

namespace s1_mesh {
constexpr size_t kHeardNodes = 8;
constexpr uint8_t kHopsUnknown = 0xFF;
// Meshtastic rounds a shared position to this many bits or fewer on the public channel unless its
// owner chooses otherwise. 19 bits is a box about 45 m across, which is as good as a point here.
constexpr uint8_t kPointBits = 19;

struct HeardNode {
  uint32_t id = 0, seenMs = 0, placedMs = 0;
  int32_t latE5 = 0, lonE5 = 0;  // Where it last said it was, in degrees times 100000.
  int16_t rssi = 0;
  int8_t snr = 0;
  uint8_t hops = kHopsUnknown;   // 0 is heard directly.
  uint8_t precisionBits = 0;     // 0 until it shares a position.
  bool contact = false;          // One of the People in Mesh.
  char name[25] = {};            // UTF-8, as it named itself, cut on a whole character.
  bool placed() const { return precisionBits != 0; }
  bool rough() const { return placed() && precisionBits < kPointBits; }
};

struct Heard {
  std::array<HeardNode, kHeardNodes> nodes{};
  size_t count = 0;
  // The one for this radio, made if it's new. When the list is full, the one heard longest ago
  // makes room, so a busy mesh can't grow it.
  HeardNode* hear(uint32_t id, uint32_t now) {
    if (!id || id == kBroadcast) return nullptr;
    for (size_t i = 0; i < count; ++i) {
      if (nodes[i].id == id) {
        nodes[i].seenMs = now;
        return &nodes[i];
      }
    }
    size_t slot = count;
    if (count == kHeardNodes) {
      slot = 0;
      for (size_t i = 1; i < kHeardNodes; ++i) {
        if (now - nodes[i].seenMs > now - nodes[slot].seenMs) slot = i;
      }
    } else {
      ++count;
    }
    nodes[slot] = HeardNode{};
    nodes[slot].id = id;
    nodes[slot].seenMs = now;
    return &nodes[slot];
  }
  // Fills `out` with the nodes, the one heard most recently first, and says how many.
  size_t newestFirst(const HeardNode** out, size_t capacity) const {
    size_t n = 0;
    for (size_t i = 0; i < count && n < capacity; ++i) out[n++] = &nodes[i];
    for (size_t i = 1; i < n; ++i) {
      for (size_t j = i; j > 0 && int32_t(out[j]->seenMs - out[j - 1]->seenMs) > 0; --j) {
        const HeardNode* swap = out[j];
        out[j] = out[j - 1];
        out[j - 1] = swap;
      }
    }
    return n;
  }
  size_t placedCount() const {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) n += nodes[i].placed() ? 1 : 0;
    return n;
  }
};

// Hops from Meshtastic's header: how many it started with less how many are left. A radio older
// than firmware 2.3 sends no start, so there's no telling.
inline uint8_t hopsTaken(uint8_t hopStart, uint8_t hopLimit) {
  return hopStart && hopStart >= hopLimit ? uint8_t(hopStart - hopLimit) : kHopsUnknown;
}

inline bool readVarint(const uint8_t* data, size_t length, size_t& at, uint64_t& out) {
  out = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (at >= length) return false;
    const uint8_t b = data[at++];
    out |= uint64_t(b & 0x7f) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}
// Meshtastic's Position (mesh.proto): latitude_i (1) and longitude_i (2) are sfixed32 degrees times
// 10^7, and precision_bits (23) is how many of their bits the sender kept. Every other field is
// skipped by its wire type. A position of exactly 0, 0 is what a radio with no fix sends.
inline bool decodePosition(const uint8_t* data, size_t length, int32_t& latE5, int32_t& lonE5, uint8_t& precisionBits) {
  bool haveLat = false, haveLon = false;
  int32_t lat = 0, lon = 0;
  uint64_t bits = 0;
  size_t at = 0;
  while (at < length) {
    uint64_t key = 0;
    if (!readVarint(data, length, at, key)) return false;
    const uint64_t field = key >> 3;
    const uint8_t wire = uint8_t(key & 7);
    if (wire == 5) {
      if (length - at < 4) return false;
      const uint32_t v = uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
      at += 4;
      if (field == 1) { lat = int32_t(v); haveLat = true; }
      else if (field == 2) { lon = int32_t(v); haveLon = true; }
    } else if (wire == 0) {
      uint64_t v = 0;
      if (!readVarint(data, length, at, v)) return false;
      if (field == 23) bits = v;
    } else if (wire == 1) {
      if (length - at < 8) return false;
      at += 8;
    } else if (wire == 2) {
      uint64_t n = 0;
      if (!readVarint(data, length, at, n) || n > length - at) return false;
      at += size_t(n);
    } else {
      return false;
    }
  }
  if (!haveLat || !haveLon || (lat == 0 && lon == 0)) return false;
  if (lat < -900000000 || lat > 900000000 || lon < -1800000000 || lon > 1800000000) return false;
  const auto toE5 = [](int32_t e7) { return (e7 + (e7 < 0 ? -50 : 50)) / 100; };
  latE5 = toE5(lat);
  lonE5 = toE5(lon);
  precisionBits = !bits || bits > 32 ? 32 : uint8_t(bits);
  return true;
}

// A name the Lab's fonts can draw: printable ASCII only, with anything else left out and the spaces
// it leaves closed up. A name that's all emoji falls back to the radio's number, as Meshtastic's
// own default names end with.
inline void shownName(const HeardNode& node, char* out, size_t capacity) {
  if (!capacity) return;
  size_t used = 0;
  for (const char* p = node.name; *p && used + 1 < capacity; ++p) {
    const uint8_t c = uint8_t(*p);
    if (c < 32 || c > 126) continue;
    if (c == ' ' && (!used || out[used - 1] == ' ')) continue;
    out[used++] = char(c);
  }
  while (used && out[used - 1] == ' ') --used;
  out[used] = 0;
  if (!used) snprintf(out, capacity, "!%04x", unsigned(node.id & 0xffff));
}
}  // namespace s1_mesh

// One list for the whole firmware, kept while it runs.
inline s1_mesh::Heard gMeshHeard;
