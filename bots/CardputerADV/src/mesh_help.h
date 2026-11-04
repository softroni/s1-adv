#pragma once
#include "mesh_contacts.h"
#include "mesh_leaf_state.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Help: one key sends a ready message, with where the Cap's GPS last put the Cardputer, to every
// contact and the private channel. It goes again every five minutes until a contact's radio says it
// arrived or someone writes back. The position packet a Meshtastic app puts on its map is here too,
// and how much of the location the public channel gets.
namespace s1_mesh {
constexpr uint8_t kHelpChoices = 4;
constexpr uint8_t kHelpOkChoice = 3;  // "I'm OK" goes once and ends Help.
constexpr uint32_t kHelpRepeatMs = 5UL * 60UL * 1000UL;
// A fix older than this is sent with its age. One older than an hour isn't sent at all.
constexpr uint32_t kFixFreshMs = 2UL * 60UL * 1000UL, kFixUsableMs = 60UL * 60UL * 1000UL;

inline const char* helpLabel(uint8_t choice) {
  static const char* const labels[kHelpChoices] = {"Out of gas", "Car trouble", "Phone dead", "I'm OK"};
  return choice < kHelpChoices ? labels[choice] : "";
}
inline const char* helpText(uint8_t choice) {
  static const char* const texts[kHelpChoices] = {
    "Out of gas. Please come and get me.",
    "Car trouble. I need help.",
    "My phone is dead. Reply here.",
    "I'm OK now. No help needed.",
  };
  return choice < kHelpChoices ? texts[choice] : "";
}

// Where the GPS last had a fix.
struct Where {
  bool known = false;
  int32_t latE5 = 0, lonE5 = 0;  // Degrees times 100000.
  uint32_t atMs = 0;
  uint8_t satellites = 0;
};
inline bool usable(const Where& w, uint32_t now) { return w.known && uint32_t(now - w.atMs) < kFixUsableMs; }

// How much of where the Cardputer is goes on the public channel, which anyone in range hears and
// which radios with an internet link upload to maps such as meshmap.net. Settings > Device > Public
// location, off unless changed. Contacts and the private channel always get it exactly.
enum class Sharing : uint8_t { Off = 0, Rough = 1, Exact = 2 };
constexpr uint8_t kSharingCount = 3;
inline Sharing sharingOrDefault(uint8_t value) { return value < kSharingCount ? Sharing(value) : Sharing::Off; }
inline const char* sharingName(Sharing s) {
  return s == Sharing::Exact ? "exact" : s == Sharing::Rough ? "rough" : "off";
}
// Rough is Meshtastic's own way: keep the top 13 bits of each coordinate and move to the middle of
// the box that leaves, about 5.8 km from south to north, so the real spot is within about 3 km.
// The phone apps draw that box for a position whose precision_bits says 13.
constexpr uint8_t kRoughBits = 13, kExactBits = 32;
inline int32_t roughE7(int32_t e7, uint8_t bits) {
  if (bits >= 32 || !bits) return e7;
  const uint32_t mask = UINT32_MAX << (32 - bits);
  return int32_t((uint32_t(e7) & mask) + (uint32_t(1) << (31 - bits)));
}
inline int32_t roughE5(int32_t e5) { return roughE7(e5 * 100, kRoughBits) / 100; }

// 4251912 is "42.51912" and -7089645 is "-70.89645". Two places, "42.52", is for a rough spot,
// where more would look more exact than it is.
inline int formatDegrees(char* out, size_t capacity, int32_t e5, int places = 5) {
  const int64_t v = e5;
  const uint64_t a = uint64_t(v < 0 ? -v : v);
  if (places == 2) {
    const uint64_t hundredths = (a + 500) / 1000;
    return snprintf(out, capacity, "%s%u.%02u", v < 0 && hundredths ? "-" : "", unsigned(hundredths / 100), unsigned(hundredths % 100));
  }
  return snprintf(out, capacity, "%s%u.%05u", v < 0 ? "-" : "", unsigned(a / 100000), unsigned(a % 100000));
}
// "https://maps.google.com/?q=42.51912,-70.89645", which phones open as a map.
inline size_t formatMapLink(char* out, size_t capacity, int32_t latE5, int32_t lonE5, int places = 5) {
  char lat[16], lon[16];
  formatDegrees(lat, sizeof(lat), latE5, places);
  formatDegrees(lon, sizeof(lon), lonE5, places);
  const int n = snprintf(out, capacity, "https://maps.google.com/?q=%s,%s", lat, lon);
  return n < 0 ? 0 : size_t(n) < capacity ? size_t(n) : capacity ? capacity - 1 : 0;
}
// Where, as words to follow a message: the map link, with the fix's age once it's a few minutes old.
// Off says nothing at all.
inline size_t formatWhere(char* out, size_t capacity, const Where& w, uint32_t now, Sharing sharing = Sharing::Exact) {
  if (!capacity) return 0;
  out[0] = 0;
  if (sharing == Sharing::Off) return 0;
  int n;
  if (!usable(w, now)) {
    n = snprintf(out, capacity, "No GPS fix.");
    return n < 0 ? 0 : size_t(n) < capacity ? size_t(n) : capacity - 1;
  }
  const bool rough = sharing == Sharing::Rough;
  char link[64];
  if (rough) formatMapLink(link, sizeof(link), roughE5(w.latE5), roughE5(w.lonE5), 2);
  else formatMapLink(link, sizeof(link), w.latE5, w.lonE5);
  const uint32_t age = now - w.atMs;
  const char* within = rough ? ", within 3 km" : "";
  if (age < kFixFreshMs) n = snprintf(out, capacity, "Location%s: %s", rough ? " within 3 km" : "", link);
  else n = snprintf(out, capacity, "Location %u min ago%s: %s", unsigned(age / 60000UL), within, link);
  return n < 0 ? 0 : size_t(n) < capacity ? size_t(n) : capacity - 1;
}
// The Help message: what's wrong, then where, as much as the place it's going may have.
inline size_t composeHelp(char* out, size_t capacity, uint8_t choice, const Where& w, uint32_t now,
                          Sharing sharing = Sharing::Exact) {
  if (!capacity) return 0;
  char where[96];
  const size_t whereLength = formatWhere(where, sizeof(where), w, now, sharing);
  const int n = whereLength ? snprintf(out, capacity, "%s %s", helpText(choice), where)
                            : snprintf(out, capacity, "%s", helpText(choice));
  return n < 0 ? 0 : size_t(n) < capacity ? size_t(n) : capacity - 1;
}
// What Ctrl+L sends as text, beside the position packet. Nothing when the location is off.
inline size_t composeLocation(char* out, size_t capacity, const Where& w, uint32_t now,
                              Sharing sharing = Sharing::Exact) {
  if (!capacity) return 0;
  out[0] = 0;
  char where[96];
  if (!formatWhere(where, sizeof(where), w, now, sharing)) return 0;
  const int n = snprintf(out, capacity, "My location. %s", where);
  return n < 0 ? 0 : size_t(n) < capacity ? size_t(n) : capacity - 1;
}

// Meshtastic's Position message (mesh.proto): latitude_i and longitude_i (1, 2, sfixed32, degrees
// times 10^7), location_source (5, LOC_INTERNAL), sats_in_view (19) and precision_bits (23). With
// all 32 bits nothing is rounded off; with fewer, the spot is moved to its box's middle first, as
// the firmware does. The GPS gives no altitude or time here, so neither is sent.
inline size_t encodePosition(uint8_t* out, size_t capacity, int32_t latE5, int32_t lonE5, uint8_t satellites,
                             uint8_t precisionBits = kExactBits) {
  constexpr size_t kBytes = 5 + 5 + 2 + 3 + 3;
  if (capacity < kBytes || !precisionBits || precisionBits > 32) return 0;
  size_t n = 0;
  auto fixed32 = [&](uint8_t tag, int32_t value) {
    out[n++] = tag;
    const uint32_t u = uint32_t(value);
    for (int b = 0; b < 32; b += 8) out[n++] = uint8_t(u >> b);
  };
  fixed32(0x0D, roughE7(latE5 * 100, precisionBits));  // Field 1, wire type 5.
  fixed32(0x15, roughE7(lonE5 * 100, precisionBits));  // Field 2, wire type 5.
  out[n++] = 0x28; out[n++] = 2;                                 // Field 5: LOC_INTERNAL.
  out[n++] = 0x98; out[n++] = 0x01; out[n++] = uint8_t(satellites & 0x7f);  // Field 19.
  out[n++] = 0xB8; out[n++] = 0x01; out[n++] = precisionBits;    // Field 23.
  return n;
}

enum class HelpStop : uint8_t { None, Confirmed, Replied, Ok };
// Help's messages each round: one per contact, in contact order, then the private channel's, then
// the public channel's.
constexpr size_t kHelpPrivateSlot = kContacts, kHelpPublicSlot = kContacts + 1, kHelpSlots = kContacts + 2;

struct Help {
  bool on = false;
  uint8_t choice = 0;
  bool alsoPublic = false;
  bool alsoPrivate = true;  // The private channel, when there is one: on unless C turns it off.
  HelpStop stop = HelpStop::None;
  uint32_t startedMs = 0, roundMs = 0;
  uint16_t rounds = 0;
  std::array<uint32_t, kHelpSlots> seq{};

  void start(uint8_t c, uint32_t now) {
    on = true; choice = c < kHelpChoices ? c : 0; stop = HelpStop::None;
    startedMs = now; roundMs = now; rounds = 0; seq = {};
  }
  bool repeating() const { return on && stop == HelpStop::None && choice != kHelpOkChoice; }
  // The first round as soon as the outbox is free, then one every five minutes after the last.
  bool due(uint32_t now, bool outboxIdle) const {
    if (!on || stop != HelpStop::None || !outboxIdle) return false;
    return !rounds || int32_t(now - (roundMs + kHelpRepeatMs)) >= 0;
  }
  void roundSent(uint32_t now) {
    ++rounds;
    roundMs = now;
    if (choice == kHelpOkChoice) stop = HelpStop::Ok;
  }
  uint32_t nextInMs(uint32_t now) const {
    if (!repeating() || !rounds) return 0;
    const int32_t left = int32_t(roundMs + kHelpRepeatMs - now);
    return left > 0 ? uint32_t(left) : 0;
  }
  // A contact's radio saying one of this round's messages arrived ends the repeats. A channel
  // message, private or public, can only ever be heard, which isn't a person.
  void delivered(uint32_t messageSeq) {
    if (!repeating() || !messageSeq) return;
    for (size_t i = 0; i < kContacts; ++i) if (seq[i] == messageSeq) { stop = HelpStop::Confirmed; return; }
  }
  // So does anyone writing back who's trusted: a contact, or anyone on the private channel, which
  // is only the people who share its key. Or anyone at all sending this Cardputer a direct message.
  // Talk on the public channel doesn't count, since it's usually about something else.
  void replied(bool trusted, bool direct, uint32_t now) {
    if (!repeating() || int32_t(now - startedMs) < 0) return;
    if (trusted || direct) stop = HelpStop::Replied;
  }
};
}  // namespace s1_mesh
