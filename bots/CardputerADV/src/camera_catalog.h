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
  bool ptz = false, off = false;
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
      for (const char* p = f[2]; *p; ++p) { if (*p == 'p') c.ptz = true; if (*p == 'o') c.off = true; }
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
