#pragma once
// The Lab, live on the bridge's web page. While the Lab is open on the home Wi-Fi, the Cardputer
// tells the bridge what every part reads and does, over the bridge's plain-HTTP home door with a
// ticket, the way Planes and Cameras talk to it at home. Away from home nothing is sent.
//
// A report is lines of tab-separated fields:
//   lab 1                         always first
//   view <devices|device|dashboard|rules|seqs|playing|other|closed>
//   open <part name>              on a part's test screen, with   page <n>   from 0
//   units <us|metric>             what the owner reads; the values below are always the Lab's units
//   playing <0|1>                 rules playing
//   found <unit,unit>             the I2C units that answer
//   wires <white part> <yellow part>     only with parts straight on the Grove wires
//   hub <part,part,part,part,part,part>  only with the I/O hub answering, ports 0 to 5
//   sig <key> <value>             each live reading, in cm, C, %, deg, km/h and m
//   out <key> <value>             each output: switches 0 or 1, the rest -1 when nothing drives it
//   gps <fix> <used> <in view> <altitude m or -> <hdop x10 or -1>   only with the Cap's GPS heard
//   strip_test <solid|rainbow|chase>   sweeping 1   firing <rule,rule>   seq <name>
// The bridge answers   watch 1   while the page is open, so the next report comes in a second,
// or   watch 0   for one in 15 seconds. Pure functions over plain data, so it all runs on the host.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lab.h"
#include "podcast_catalog.h"

namespace lab_live {
constexpr size_t kMaxBody = 1500;
constexpr uint32_t kWatchedMs = 1000;    // The page is open: about as often as the Lab redraws.
constexpr uint32_t kIdleMs = 15000;      // Nobody's looking: a heartbeat, so the page knows.
constexpr uint32_t kRequestMs = 2500;    // A report that takes longer than this has failed.
constexpr uint8_t kMaxFailures = 3;      // Then it stops until the ticket changes or the Lab reopens.
constexpr const char kTicketPath[] = "/api/cardputer/lab/ticket";
constexpr const char kStripTests[][8] = {"solid", "rainbow", "chase"};

// The Lab, read off its globals by the firmware for one report.
struct Snapshot {
  const char* view = "other";
  const char* open = nullptr;   // A part's name, on its test screen.
  int page = -1;
  bool us = false, playing = false;
  bool found[lab::kUnitCount] = {};
  bool onWires = false;
  lab::Part white = lab::Part::None, yellow = lab::Part::None;
  bool hub = false;
  lab::Part ports[pbhub::kPorts] = {};
  bool live[lab::kSigCount] = {};
  int16_t value[lab::kSigCount] = {};
  bool driven[lab::kOutCount] = {};   // An output something is asking for.
  int16_t out[lab::kOutCount] = {};
  bool gps = false, fix = false, hasAltitude = false;
  int used = 0, inView = 0, altitudeM = 0, hdopE1 = 0;  // hdopE1 0 is unknown.
  int stripTest = -1;            // On the LED strip's screen: an index into kStripTests.
  bool sweeping = false;
  uint8_t firing = 0;            // Rules firing, one bit each, rule 1 the lowest.
  const char* seq = nullptr;     // The sequence running.
};

// Whole lines only: the first that wouldn't fit is left out, never cut, and so is all after it.
class Lines {
 public:
  Lines(char* out, size_t room) : mOut(out), mRoom(room) { if (room) out[0] = '\0'; }
  template <class... Args>
  void add(const char* format, Args... args) {
    if (mFull || mAt >= mRoom) { mFull = true; return; }
    const int len = snprintf(mOut + mAt, mRoom - mAt, format, args...);
    if (len < 0 || size_t(len) >= mRoom - mAt) { mOut[mAt] = '\0'; mFull = true; return; }
    mAt += size_t(len);
  }
  size_t length() const { return mAt; }
  bool full() const { return mFull; }

 private:
  char* mOut;
  size_t mRoom, mAt = 0;
  bool mFull = false;
};

// Switches are 0 or 1; anything else with nothing asking for it is -1, because 0 is a real angle,
// a real hue and a real brightness.
inline bool switchOut(lab::Out out) {
  return out == lab::Out::Relay1 || out == lab::Out::Relay2 || out == lab::Out::LampPower ||
         out == lab::Out::AcTcl || out == lab::Out::AcDaikin;
}
inline bool reported(lab::Out out) { return out != lab::Out::Say && out != lab::Out::Sequence; }
inline int outValue(const Snapshot& s, lab::Out out) {
  const uint8_t i = static_cast<uint8_t>(out);
  if (switchOut(out)) return s.driven[i] && s.out[i] > 0 ? 1 : 0;
  return s.driven[i] ? s.out[i] : -1;
}

// The report's body; its length, or 0 when even the first lines don't fit.
inline size_t body(const Snapshot& s, char* buffer, size_t room) {
  Lines out(buffer, room);
  out.add("lab\t1\nview\t%s\n", s.view ? s.view : "other");
  if (out.full()) return 0;
  if (s.open && *s.open) {
    out.add("open\t%s\n", s.open);
    if (s.page >= 0) out.add("page\t%d\n", s.page);
  }
  out.add("units\t%s\nplaying\t%d\n", s.us ? "us" : "metric", s.playing ? 1 : 0);
  char list[96];
  Lines units(list, sizeof(list));
  for (uint8_t i = 0; i < lab::kUnitCount; ++i) if (s.found[i]) units.add(units.length() ? ",%s" : "%s", lab::kUnitNames[i]);
  out.add("found\t%s\n", list);
  if (s.onWires) out.add("wires\t%s\t%s\n", lab::kPartNames[static_cast<uint8_t>(s.white)], lab::kPartNames[static_cast<uint8_t>(s.yellow)]);
  if (s.hub) {
    Lines ports(list, sizeof(list));
    for (uint8_t i = 0; i < pbhub::kPorts; ++i) {
      const uint8_t part = static_cast<uint8_t>(s.ports[i]);
      ports.add(i ? ",%s" : "%s", lab::kPartNames[part < lab::kPartCount ? part : 0]);
    }
    out.add("hub\t%s\n", list);
  }
  for (uint8_t i = 0; i < lab::kSigCount; ++i) if (s.live[i]) out.add("sig\t%s\t%d\n", lab::kSigs[i].key, s.value[i]);
  for (uint8_t i = 0; i < lab::kOutCount; ++i) {
    const lab::Out o = static_cast<lab::Out>(i);
    if (reported(o)) out.add("out\t%s\t%d\n", lab::kOuts[i].key, outValue(s, o));
  }
  if (s.gps) {
    char altitude[12];
    if (s.hasAltitude) snprintf(altitude, sizeof(altitude), "%d", s.altitudeM);
    else strcpy(altitude, "-");
    out.add("gps\t%d\t%d\t%d\t%s\t%d\n", s.fix ? 1 : 0, s.used, s.inView, altitude, s.hdopE1 > 0 ? s.hdopE1 : -1);
  }
  if (s.stripTest >= 0 && s.stripTest < int(sizeof(kStripTests) / sizeof(kStripTests[0]))) out.add("strip_test\t%s\n", kStripTests[s.stripTest]);
  if (s.sweeping) out.add("%s", "sweeping\t1\n");
  if (s.firing) {
    Lines rules(list, sizeof(list));
    for (uint8_t i = 0; i < 8; ++i) if (s.firing & (1u << i)) rules.add(rules.length() ? ",%d" : "%d", i + 1);
    out.add("firing\t%s\n", list);
  }
  if (s.seq && *s.seq) out.add("seq\t%s\n", s.seq);
  return out.length();
}
// The last one, as the Lab closes.
inline size_t closedBody(char* buffer, size_t room) {
  Lines out(buffer, room);
  out.add("%s", "lab\t1\nview\tclosed\n");
  return out.full() ? 0 : out.length();
}

// The request in front of the body, for a plain socket. ip is host order.
inline size_t head(char* buffer, size_t room, uint32_t ip, uint16_t port, const char* path, size_t bodyLength) {
  Lines out(buffer, room);
  out.add("POST %s/live HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\nContent-Type: text/plain\r\n"
          "Content-Length: %u\r\nConnection: close\r\n\r\n",
          path, unsigned(ip >> 24), unsigned((ip >> 16) & 255), unsigned((ip >> 8) & 255), unsigned(ip & 255),
          unsigned(port), unsigned(bodyLength));
  return out.full() ? 0 : out.length();
}

// What came back: the status, and whether anyone is watching (1, 0, or -1 when it didn't say).
struct Answer { int status = 0; int8_t watch = -1; };
inline Answer readAnswer(const char* text, size_t len) {
  Answer a;
  unsigned major = 0, minor = 0, code = 0;
  if (len < 12 || sscanf(text, "HTTP/%u.%u %u", &major, &minor, &code) != 3 || code < 100 || code > 599) return a;
  a.status = int(code);
  const char* end = text + len;
  for (const char* p = text; p + 3 < end; ++p) {
    if (memcmp(p, "\r\n\r\n", 4)) continue;
    for (const char* line = p + 4; line < end;) {
      const char* stop = static_cast<const char*>(memchr(line, '\n', size_t(end - line)));
      const size_t n = size_t((stop ? stop : end) - line);
      if (n >= 7 && !memcmp(line, "watch\t", 6) && (line[6] == '0' || line[6] == '1') &&
          (n == 7 || line[7] == '\r')) a.watch = int8_t(line[6] - '0');
      line += n + 1;
    }
    break;
  }
  return a;
}

// The bridge's ticket answer, read the way Podcasts reads its own:
//   ticket <port> /lab/<32 hex>     and a   lan <ip> <prefix>   for each home address
inline bool ticketPathValid(const char* path) { return podcast::homePathValid(path, "/lab/"); }
inline bool parseTicket(const char* text, size_t len, podcast::HomeTicket& out) {
  return podcast::parseHomeTicket(text, len, out, ticketPathValid);
}

// When to send: every second while someone watches, else every 15, and not at all after three
// failures in a row, until the ticket changes or the Lab opens again.
class Cadence {
 public:
  void restart() { *this = Cadence{}; }
  bool stopped() const { return mFailures >= kMaxFailures; }
  uint32_t interval() const { return mWatch == 1 ? kWatchedMs : kIdleMs; }
  bool due(uint32_t now) const { return !stopped() && (!mSent || now - mLastMs >= interval()); }
  void sent(uint32_t now) { mSent = true; mLastMs = now; }
  void answered(const Answer& a) {
    if (a.status == 200) { mFailures = 0; mWatch = a.watch == 1 ? 1 : 0; }
    else failed();
  }
  void failed() { if (mFailures < kMaxFailures) ++mFailures; mWatch = 0; }
  uint8_t failures() const { return mFailures; }
  bool watched() const { return mWatch == 1; }

 private:
  bool mSent = false;
  uint32_t mLastMs = 0;
  uint8_t mFailures = 0;
  int8_t mWatch = -1;
};
}  // namespace lab_live
