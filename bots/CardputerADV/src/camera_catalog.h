#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "podcast_catalog.h"

// The Cameras app's pure parts: what the bridge's lines say, where to ask for things, the wall's
// layout and the times it shows. No hardware here, so it's tested on the host
// (scripts/test_cameras.py).
//
// The bridge answers its list as tab-separated lines (bridge/src/cameras-media.mjs listLines):
//   state <ready|setup|signing-in|code|captcha|error|off|starting> [<what to show>]
//   base <the HomeBase's mode>
//   cam <id> <flags> <battery %|-> <seconds since motion|-> <seconds since its picture|-> <name>
namespace cameras {
constexpr size_t kMax = 12;                 // Cameras on the wall; more are left off.
constexpr size_t kIdBytes = 9, kNameBytes = 40, kDetailBytes = 64, kModeBytes = 16;
// The screen's picture and the wall's thumbnails, as the bridge makes them.
constexpr int16_t kPictureW = 240, kPictureH = 135;
constexpr int16_t kThumbW = 78, kThumbH = 44;
constexpr size_t kThumbBytes = size_t(kThumbW) * kThumbH;
constexpr size_t kMaxJpeg = 32 * 1024;      // A picture or frame bigger than this is refused.
constexpr size_t kMaxList = 4096;
// Live view: the device's own countdown, which it extends. The battery is why it ends.
constexpr uint32_t kLiveSeconds = 45, kAskSeconds = 10;

enum class State : uint8_t { Unknown, Ready, Setup, SigningIn, Code, Captcha, Error, Off, Starting };
inline State stateFrom(const char* s) {
  if (!strcmp(s, "ready")) return State::Ready;
  if (!strcmp(s, "setup")) return State::Setup;
  if (!strcmp(s, "signing-in")) return State::SigningIn;
  if (!strcmp(s, "code")) return State::Code;
  if (!strcmp(s, "captcha")) return State::Captcha;
  if (!strcmp(s, "error")) return State::Error;
  if (!strcmp(s, "off")) return State::Off;
  if (!strcmp(s, "starting")) return State::Starting;
  return State::Unknown;
}

struct Camera {
  char id[kIdBytes] = "";
  char name[kNameBytes] = "";
  bool ptz = false, off = false, lens = false;  // lens: a second, telephoto lens to zoom with.
  int16_t battery = -1;        // Percent, or -1 when the camera doesn't say.
  int32_t motionAge = -1;      // Seconds since it last saw motion, when the list came; -1 never.
  int32_t pictureAge = -1;     // Seconds since its picture was taken; -1 when there's none.
};
struct List {
  State state = State::Unknown;
  char detail[kDetailBytes] = "";
  char mode[kModeBytes] = "";
  Camera cams[kMax];
  uint8_t count = 0;
};

inline bool idValid(const char* id) {
  size_t n = 0;
  for (; id[n]; ++n) if (n >= 8 || !((id[n] >= '0' && id[n] <= '9') || (id[n] >= 'a' && id[n] <= 'f'))) return false;
  return n == 8;
}
inline bool readAge(const char* s, int32_t& out) {
  if (!strcmp(s, "-")) { out = -1; return true; }
  uint32_t n = 0;
  if (!podcast::readNumber(s, n, 100u * 365 * 86400)) return false;
  out = int32_t(n);
  return true;
}
// False when the answer isn't a list at all (an old bridge, an error page): no state line.
inline bool parseList(const char* text, size_t len, List& out) {
  out = List{};
  bool sawState = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[7];
    const size_t n = podcast::splitFields(line, f, 7);
    if (n >= 2 && !strcmp(f[0], "state")) {
      out.state = stateFrom(f[1]);
      podcast::copyText(out.detail, sizeof(out.detail), n >= 3 ? f[2] : "");
      sawState = true;
    } else if (n == 2 && !strcmp(f[0], "base")) {
      podcast::copyText(out.mode, sizeof(out.mode), f[1]);
    } else if (n == 7 && !strcmp(f[0], "cam") && out.count < kMax && idValid(f[1])) {
      Camera c;
      strcpy(c.id, f[1]);
      for (const char* p = f[2]; *p; ++p) { if (*p == 'p') c.ptz = true; if (*p == 'o') c.off = true; if (*p == 'z') c.lens = true; }
      uint32_t battery = 0;
      c.battery = podcast::readNumber(f[3], battery, 100) ? int16_t(battery) : -1;
      if (!readAge(f[4], c.motionAge) || !readAge(f[5], c.pictureAge)) return;
      podcast::copyText(c.name, sizeof(c.name), f[6]);
      if (!c.name[0]) podcast::copyText(c.name, sizeof(c.name), "Camera");
      out.cams[out.count++] = c;
    }
  });
  return sawState;
}

// --- Where to ask --------------------------------------------------------------------------
constexpr const char kBase[] = "/api/cardputer/cameras/";
constexpr const char kTicketPath[] = "/api/cardputer/cameras/ticket";
constexpr const char kListPath[] = "/api/cardputer/cameras/list";
// /api/cardputer/cameras/<what>?cam=<id>[&<extra>]
inline bool routePath(char* out, size_t room, const char* what, const char* id, const char* extra = "") {
  if (!idValid(id)) return false;
  const int len = snprintf(out, room, "%s%s?cam=%s%s%s", kBase, what, id, extra && *extra ? "&" : "", extra ? extra : "");
  return len > 0 && size_t(len) < room;
}
inline bool turnPath(char* out, size_t room, const char* id, const char* direction) {
  char extra[16];
  snprintf(extra, sizeof(extra), "dir=%s", direction);
  return routePath(out, room, "turn", id, extra);
}
inline bool codePath(char* out, size_t room, const char* digits) {
  const size_t n = strlen(digits);
  if (n < 4 || n > 8) return false;
  for (size_t i = 0; i < n; ++i) if (digits[i] < '0' || digits[i] > '9') return false;
  const int len = snprintf(out, room, "%scode?value=%s", kBase, digits);
  return len > 0 && size_t(len) < room;
}
// The phone sign-in page's link, asked over HTTPS, as the bridge answers:
//   setup <path> <seconds it lasts>      or   none <why>
constexpr const char kSetupPath[] = "/api/cardputer/cameras/setup";
inline bool parseSetup(const char* text, size_t len, char* path, size_t room, uint32_t& seconds) {
  bool found = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[3];
    const size_t n = podcast::splitFields(line, f, 3);
    uint32_t s = 0;
    if (found || n != 3 || strcmp(f[0], "setup") || strncmp(f[1], "/eufy/", 6) || strlen(f[1]) != 22 ||
        !podcast::readNumber(f[2], s, 24 * 3600) || strlen(f[1]) >= room) return;
    for (const char* p = f[1] + 6; *p; ++p)
      if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return;
    strcpy(path, f[1]);
    seconds = s;
    found = true;
  });
  return found;
}
// The page's address for the QR code: the bridge's own, as the device reaches it.
inline bool setupUrl(char* out, size_t room, const char* host, uint16_t port, bool tls, const char* path) {
  if (!host || !*host || !path || !*path) return false;
  const bool plainPort = (tls && port == 443) || (!tls && port == 80) || !port;
  const int len = plainPort ? snprintf(out, room, "%s://%s%s", tls ? "https" : "http", host, path)
                            : snprintf(out, room, "%s://%s:%u%s", tls ? "https" : "http", host, unsigned(port), path);
  return len > 0 && size_t(len) < room;
}

// A captcha's answer: letters and digits, as the picture on the Mac shows them.
inline bool captchaPath(char* out, size_t room, const char* answer) {
  const size_t n = strlen(answer);
  if (n < 3 || n > 8) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = answer[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  const int len = snprintf(out, room, "%scode?captcha=%s", kBase, answer);
  return len > 0 && size_t(len) < room;
}
// The home door, as the bridge answers its ticket route:
//   ticket <port> /cameras/<32 hex>     and a   lan <ip> <prefix>   for each home address
inline bool ticketPathValid(const char* path) {
  static const char kStart[] = "/cameras/";
  if (strncmp(path, kStart, sizeof(kStart) - 1)) return false;
  const char* id = path + sizeof(kStart) - 1;
  size_t n = 0;
  for (; id[n]; ++n) if (n >= 32 || !((id[n] >= '0' && id[n] <= '9') || (id[n] >= 'a' && id[n] <= 'f'))) return false;
  return n == 32;
}
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

// --- A bridge's answer, read a line at a time ----------------------------------------------
// Only what this app needs: the status, the length, and whether the connection stays open.
struct Response {
  unsigned status = 0;
  uint32_t length = 0;
  bool hasLength = false, close = false, chunked = false, done = false;
  char clip[40] = "";  // A clip's frames' "<position ms> <length ms> <playing> <ended>" (x-clip).
  // False when the line makes no sense as an HTTP answer. An empty line ends the headers.
  bool line(const char* s) {
    if (!status) {
      unsigned code = 0;
      if (strncmp(s, "HTTP/1.", 7) || (s[7] != '0' && s[7] != '1') || s[8] != ' ') return false;
      for (int i = 9; i < 12; ++i) { if (s[i] < '0' || s[i] > '9') return false; code = code * 10 + unsigned(s[i] - '0'); }
      if (s[12] && s[12] != ' ') return false;
      status = code;
      if (s[7] == '0') close = true;
      return true;
    }
    if (!*s) { done = true; return true; }
    const char* colon = strchr(s, ':');
    if (!colon) return false;
    const size_t key = size_t(colon - s);
    const char* value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    const auto is = [&](const char* name) { return strlen(name) == key && sameWord(s, name, key); };
    if (is("content-length")) {
      uint32_t n = 0;
      char digits[12] = {};
      size_t i = 0;
      while (value[i] >= '0' && value[i] <= '9' && i + 1 < sizeof(digits)) { digits[i] = value[i]; ++i; }
      if (!i || !podcast::readNumber(digits, n)) return false;
      length = n;
      hasLength = true;
    } else if (is("connection")) {
      if (sameWord(value, "close", 5)) close = true;
    } else if (is("transfer-encoding")) {
      if (sameWord(value, "chunked", 7)) chunked = true;
    } else if (is("x-clip")) {
      size_t n = 0;
      while (value[n] && n + 1 < sizeof(clip)) { clip[n] = value[n]; ++n; }
      clip[n] = 0;
    }
    return true;
  }
  static bool sameWord(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      char x = a[i], y = b[i];
      if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
      if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
      if (x != y || !x) return false;
    }
    return true;
  }
};

// --- Zoom and pan in live view --------------------------------------------------------------
// The steps = and - go through, in tenths, as the bridge plans them (cameras-media.mjs zoomLadder):
// digital to 4x, or with a second lens 2x digital, the lens's 3x, and digital on top to 12x.
constexpr uint8_t kZoomSteps = 8;
inline uint8_t zoomLadder(bool lens, uint8_t* out) {
  static const uint8_t kDigital[] = {10, 15, 20, 30, 40};
  static const uint8_t kLens[] = {10, 15, 20, 30, 45, 60, 90, 120};
  const uint8_t* from = lens ? kLens : kDigital;
  const uint8_t n = lens ? sizeof(kLens) : sizeof(kDigital);
  for (uint8_t i = 0; i < n; ++i) out[i] = from[i];
  return n;
}
// The next step in or out from where it is now; the same when there's none.
inline uint8_t zoomStep(bool lens, uint8_t now, int direction) {
  uint8_t ladder[kZoomSteps];
  const uint8_t n = zoomLadder(lens, ladder);
  if (direction > 0) { for (uint8_t i = 0; i < n; ++i) if (ladder[i] > now) return ladder[i]; return ladder[n - 1]; }
  for (int i = n - 1; i >= 0; --i) if (ladder[i] < now) return ladder[i];
  return ladder[0];
}
// "1.5x", "3x", "12x".
inline void zoomText(char* out, size_t room, unsigned tenths) {
  if (tenths % 10) snprintf(out, room, "%u.%ux", tenths / 10, tenths % 10);
  else snprintf(out, room, "%ux", tenths / 10);
}
// The bridge's answer to a zoom or an arrow: the zoom now, where the view is in the frame in
// thousandths, and what the arrow did.
//   view <tenths> <x0> <y0> <x1> <y1> <moved|turned|edge|->
enum class PanResult : uint8_t { None, Moved, Turned, Edge };
struct View {
  uint16_t tenths = 10;
  uint16_t x0 = 0, y0 = 0, x1 = 1000, y1 = 1000;
  PanResult did = PanResult::None;
};
inline bool parseView(const char* text, size_t len, View& out) {
  bool found = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[7];
    if (found || podcast::splitFields(line, f, 7) != 7 || strcmp(f[0], "view")) return;
    uint32_t v[5];
    for (int i = 0; i < 5; ++i) if (!podcast::readNumber(f[1 + i], v[i], i ? 1000 : 1200)) return;
    if (v[0] < 10 || v[1] >= v[3] || v[2] >= v[4]) return;
    View got;
    got.tenths = uint16_t(v[0]);
    got.x0 = uint16_t(v[1]); got.y0 = uint16_t(v[2]); got.x1 = uint16_t(v[3]); got.y1 = uint16_t(v[4]);
    got.did = !strcmp(f[6], "moved") ? PanResult::Moved : !strcmp(f[6], "turned") ? PanResult::Turned
            : !strcmp(f[6], "edge") ? PanResult::Edge : PanResult::None;
    out = got;
    found = true;
  });
  return found;
}

// --- Recordings: days, the list, the month, a clip playing -----------------------------------
// A day is its yyyymmdd as a number, 20261106, as the bridge names it.
struct Ymd { int y = 1970, m = 1, d = 1; };
inline Ymd ymdOf(uint32_t day) { Ymd v; v.y = int(day / 10000); v.m = int(day / 100 % 100); v.d = int(day % 100); return v; }
inline uint32_t dayOf(const Ymd& v) { return uint32_t(v.y) * 10000u + uint32_t(v.m) * 100u + uint32_t(v.d); }
inline int daysInMonth(int y, int m) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : kDays[m - 1];
}
inline bool validDay(uint32_t day) {
  const Ymd v = ymdOf(day);
  return v.y >= 2000 && v.y <= 2099 && v.m >= 1 && v.m <= 12 && v.d >= 1 && v.d <= daysInMonth(v.y, v.m);
}
inline uint32_t addDays(uint32_t day, int n) {
  Ymd v = ymdOf(day);
  while (n > 0) { if (++v.d > daysInMonth(v.y, v.m)) { v.d = 1; if (++v.m > 12) { v.m = 1; ++v.y; } } --n; }
  while (n < 0) { if (--v.d < 1) { if (--v.m < 1) { v.m = 12; --v.y; } v.d = daysInMonth(v.y, v.m); } ++n; }
  return dayOf(v);
}
// 0 Sunday to 6 Saturday (Sakamoto's).
inline int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
inline const char* monthName(int m) {
  static const char* const k[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  return m >= 1 && m <= 12 ? k[m - 1] : "";
}
// "Fri 6 Nov".
inline void dayLabel(char* out, size_t room, uint32_t day) {
  static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const Ymd v = ymdOf(day);
  char month[4] = "";
  memcpy(month, monthName(v.m), 3);
  snprintf(out, room, "%s %d %s", kDays[weekday(v.y, v.m, v.d)], v.d, month);
}
// "16:47" from seconds since the day began; "0:41", "2:00" or "1:02:03" for a length in seconds.
inline void clockOfDay(char* out, size_t room, uint32_t seconds) { snprintf(out, room, "%u:%02u", unsigned(seconds / 3600 % 24), unsigned(seconds / 60 % 60)); }
inline void lengthText(char* out, size_t room, uint32_t seconds) {
  if (seconds >= 3600) snprintf(out, room, "%u:%02u:%02u", unsigned(seconds / 3600), unsigned(seconds / 60 % 60), unsigned(seconds % 60));
  else snprintf(out, room, "%u:%02u", unsigned(seconds / 60), unsigned(seconds % 60));
}

// A day's list, as the bridge answers it, newest first:
//   clips <day> <count>
//   clip <id> <seconds since the day began> <length in seconds> <camera id>
constexpr size_t kMaxClips = 150, kClipIdBytes = 17;
struct Clip {
  char id[kClipIdBytes] = "";
  char cam[kIdBytes] = "";
  uint32_t start = 0;
  uint16_t seconds = 0;
};
struct ClipDay {
  uint32_t day = 0;
  uint16_t total = 0;  // As the bridge counted, which can be more than fit.
  uint16_t count = 0;
  Clip clips[kMaxClips];
};
inline bool clipIdValid(const char* id) {
  size_t n = 0;
  for (; id[n]; ++n) if (n >= kClipIdBytes - 1 || id[n] < '0' || id[n] > '9') return false;
  return n > 0;
}
inline bool parseClips(const char* text, size_t len, ClipDay& out) {
  out.day = 0; out.total = 0; out.count = 0;
  bool head = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[5];
    const size_t n = podcast::splitFields(line, f, 5);
    uint32_t a = 0, b = 0, c = 0;
    if (n == 3 && !strcmp(f[0], "clips") && podcast::readNumber(f[1], a) && validDay(a) && podcast::readNumber(f[2], b, 65535)) {
      out.day = a; out.total = uint16_t(b); head = true;
    } else if (n == 5 && !strcmp(f[0], "clip") && out.count < kMaxClips && clipIdValid(f[1]) && podcast::readNumber(f[2], b, 86399) &&
               podcast::readNumber(f[3], c, 65535) && idValid(f[4])) {
      Clip& clip = out.clips[out.count++];
      strcpy(clip.id, f[1]); strcpy(clip.cam, f[4]); clip.start = b; clip.seconds = uint16_t(c);
    }
  });
  return head;
}
// A month's days with recordings:  month <yyyymm>  then  day <d> <count>.
struct Month { uint32_t ym = 0; bool has[32] = {}; };
inline bool parseMonth(const char* text, size_t len, Month& out) {
  out = Month{};
  bool head = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[3];
    const size_t n = podcast::splitFields(line, f, 3);
    uint32_t a = 0, b = 0;
    if (n == 2 && !strcmp(f[0], "month") && podcast::readNumber(f[1], a) && a / 100 >= 2000 && a % 100 >= 1 && a % 100 <= 12) { out.ym = a; head = true; }
    else if (n == 3 && !strcmp(f[0], "day") && podcast::readNumber(f[1], a, 31) && a >= 1 && podcast::readNumber(f[2], b)) out.has[a] = b > 0;
  });
  return head;
}
// Where a clip is: from its frames' x-clip "<position> <length> <playing> <ended>", or the answer to a
// pause or seek, "clip <position> <length> <playing> <ended>".
struct ClipState { uint32_t pos = 0, dur = 0; bool playing = false, ended = false, known = false; };
inline bool parseClipState(const char* text, ClipState& out) {
  unsigned long pos = 0, dur = 0;
  int playing = 0, ended = 0;
  if (!strncmp(text, "clip\t", 5)) text += 5;
  char tail = 0;
  const int got = sscanf(text, "%lu %lu %d %d%c", &pos, &dur, &playing, &ended, &tail);
  if ((got != 4 && !(got == 5 && tail == '\n')) || playing < 0 || playing > 1 || ended < 0 || ended > 1 || dur > 86400000UL) return false;
  out.pos = uint32_t(pos > dur && dur ? dur : pos); out.dur = uint32_t(dur); out.playing = playing; out.ended = ended; out.known = true;
  return true;
}
// The answer to starting one: "clip <camera id> <length ms>".
inline bool parseClipStart(const char* text, size_t len, char* cam, uint32_t& dur) {
  bool ok = false;
  podcast::eachLine(text, len, [&](char* line, size_t) {
    char* f[3];
    uint32_t d = 0;
    if (ok || podcast::splitFields(line, f, 3) != 3 || strcmp(f[0], "clip") || !idValid(f[1]) || !podcast::readNumber(f[2], d)) return;
    strcpy(cam, f[1]); dur = d; ok = true;
  });
  return ok;
}
// The month's grid, Monday first: which row and column day d is in, and how many rows it takes.
inline int monthLead(int y, int m) { return (weekday(y, m, 1) + 6) % 7; }
inline int monthRows(int y, int m) { return (monthLead(y, m) + daysInMonth(y, m) + 6) / 7; }

// --- What the screen shows -----------------------------------------------------------------
// How long ago: "now", "12m", "3h", "2d". Empty for never.
inline void ageText(char* out, size_t room, int32_t seconds) {
  if (seconds < 0) { snprintf(out, room, "%s", ""); return; }
  if (seconds < 60) snprintf(out, room, "%s", "now");
  else if (seconds < 3600) snprintf(out, room, "%dm", int(seconds / 60));
  else if (seconds < 86400) snprintf(out, room, "%dh", int(seconds / 3600));
  else snprintf(out, room, "%dd", int(seconds / 86400));
}
// "picture from 14:02" when it's today, "picture from Mon" this week, else "picture from 3w".
// `nowMinute` is minutes since midnight and `weekday` 0..6 (Sunday 0), or -1 without a clock.
inline void pictureWhen(char* out, size_t room, int32_t age, int nowMinute, int weekday) {
  if (age < 0) { snprintf(out, room, "%s", "no picture yet"); return; }
  if (age < 60) { snprintf(out, room, "%s", "picture from now"); return; }
  if (nowMinute >= 0 && age < nowMinute * 60) {
    const int at = nowMinute - (age + 59) / 60;
    snprintf(out, room, "picture from %d:%02d", at / 60, at % 60);
    return;
  }
  if (weekday >= 0 && age < 6 * 86400) {
    static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int daysBack = nowMinute >= 0 ? 1 + int((age - nowMinute * 60) / 86400) : int(age / 86400);
    snprintf(out, room, "picture from %s", kDays[((weekday - daysBack) % 7 + 7) % 7]);
    return;
  }
  char ago[8];
  ageText(ago, sizeof(ago), age);
  snprintf(out, room, "picture from %s ago", ago);
}
// The live view's clock: "0:38".
inline void countdown(char* out, size_t room, uint32_t seconds) {
  snprintf(out, room, "%u:%02u", unsigned(seconds / 60), unsigned(seconds % 60));
}

// The wall: three tiles a row, two rows a page, the HomeBase's tile after the cameras.
constexpr uint8_t kCols = 3, kRows = 2, kPerPage = kCols * kRows;
// Under the 22-pixel header: 3 x 78 + 2 x 3 is the screen's 240, and 24 + 2 x (44 + 12) its 136.
constexpr int16_t kWallTop = 24, kTileGapX = 3, kLabelH = 12;
inline uint8_t tileCount(uint8_t cameras) { return uint8_t(cameras + 1); }
inline uint8_t pageOf(uint8_t tile) { return uint8_t(tile / kPerPage); }
inline uint8_t pages(uint8_t cameras) { return uint8_t((tileCount(cameras) + kPerPage - 1) / kPerPage); }
inline int16_t tileX(uint8_t tile) { return int16_t((tile % kCols) * (kThumbW + kTileGapX)); }
inline int16_t tileY(uint8_t tile) { return int16_t(kWallTop + ((tile % kPerPage) / kCols) * (kThumbH + kLabelH)); }
// Moving the selection on the wall: up and down by a row, left and right by one, kept on the tiles.
inline uint8_t move(uint8_t tile, uint8_t cameras, int dx, int dy) {
  const int count = tileCount(cameras);
  int next = int(tile) + dx + dy * kCols;
  if (dy && (next < 0 || next >= count)) return tile;
  if (next < 0) next = 0;
  if (next >= count) next = count - 1;
  return uint8_t(next);
}

// --- Keys ----------------------------------------------------------------------------------
// The keyboard's own arrows are ; . , / and a USB keyboard's arrive as 1 to 4.
enum class Arrow : uint8_t { None, Up, Down, Left, Right };
inline Arrow arrowFor(char key) {
  switch (key) {
    case ';': case 1: return Arrow::Up;
    case '.': case 2: return Arrow::Down;
    case ',': case 3: return Arrow::Left;
    case '/': case 4: return Arrow::Right;
    default: return Arrow::None;
  }
}
inline const char* directionWord(Arrow a) {
  switch (a) {
    case Arrow::Up: return "up";
    case Arrow::Down: return "down";
    case Arrow::Left: return "left";
    case Arrow::Right: return "right";
    default: return "";
  }
}

// A saved frame's name on the card: /cameras/<name>-YYYYMMDD-HHMMSS.jpg, the name kept to
// letters and digits.
inline bool savePath(char* out, size_t room, const char* name, int year, int month, int day, int hour, int minute, int second) {
  char safe[20];
  size_t n = 0;
  for (const char* p = name; *p && n + 1 < sizeof(safe); ++p) {
    const char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) safe[n++] = c;
    else if ((c == ' ' || c == '-' || c == '_') && n && safe[n - 1] != '-') safe[n++] = '-';
  }
  while (n && safe[n - 1] == '-') --n;
  safe[n] = 0;
  const int len = snprintf(out, room, "/cameras/%s-%04d%02d%02d-%02d%02d%02d.jpg", n ? safe : "camera",
                           year, month, day, hour, minute, second);
  return len > 0 && size_t(len) < room;
}
}  // namespace cameras
