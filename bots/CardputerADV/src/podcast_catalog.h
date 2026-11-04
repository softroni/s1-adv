#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Podcasts, audiobooks and old-time radio. The bridge searches and reads the feeds, and sends
// back plain lines, one record a line with its fields split by tabs and the text last:
//   show <id> <episodes> <title> <by>
//   about <episodes> <from> <title> <by>
//   ep <id> <seconds> <date or -> <title>
// It also passes each episode on as one MP3 format, 32 kHz mono at 48 kbit/s. That's 6000
// bytes a second in frames of 216 bytes, so a second in a saved file is found by arithmetic.
//
// The card keeps three files in /podcasts, one record a line, in the same tab-separated way:
//   shows.txt     shows you follow:       <id> <episodes> <title> <by>
//   progress.txt  where you stopped:      <show> <episode> <at> <seconds> <stamp> <show title> <title>
//   saved.txt     episodes on the card:   <show> <episode> <seconds> <bytes> <show title> <title>
// The episode audio saved for offline listening is /podcasts/files/<episode>.mp3.
namespace podcast {
constexpr size_t kIdBytes = 100, kTitleBytes = 96, kByBytes = 48, kEpisodeIdBytes = 12, kShowTitleBytes = 48;
// What the card keeps, and what a screen holds in memory: about 7 KB for a full list.
constexpr size_t kMaxShows = 20, kPage = 20, kMaxFollowed = 25, kMaxProgress = 20, kMaxSaved = 25;
constexpr uint32_t kRate = 32000, kFrameSamples = 1152, kFrameBytes = 216, kBytesPerSecond = 6000;
// Close enough to the end to count as heard: most episodes end on music or credits.
constexpr uint32_t kDoneSeconds = 45;

enum class Kind : uint8_t { Podcast, Book, Radio };
constexpr uint8_t kKinds = 3;
inline const char* kindKey(Kind kind) { return kind == Kind::Book ? "book" : kind == Kind::Radio ? "radio" : "podcast"; }
inline const char* kindName(Kind kind) { return kind == Kind::Book ? "Audiobooks" : kind == Kind::Radio ? "Old radio" : "Podcasts"; }
inline const char* kindHint(Kind kind) {
  return kind == Kind::Book ? "a title or writer, like Sherlock" : kind == Kind::Radio ? "a show, like The Shadow" : "a show's name or a topic";
}

struct Show {
  char id[kIdBytes + 1] = "";
  char title[kTitleBytes + 1] = "";
  char by[kByBytes + 1] = "";
  uint16_t count = 0;
};
struct Episode {
  char id[kEpisodeIdBytes + 1] = "";
  char title[kTitleBytes + 1] = "";
  char date[11] = "";  // YYYY-MM-DD, or empty.
  uint32_t seconds = 0;  // 0 when the feed didn't say.
};
struct Page {
  char title[kTitleBytes + 1] = "";
  char by[kByBytes + 1] = "";
  uint16_t total = 0, from = 0;
  uint8_t count = 0;
  Episode episodes[kPage];
};
// A line from progress.txt or saved.txt: an episode with the show it's from.
struct Entry {
  char show[kIdBytes + 1] = "";
  char episode[kEpisodeIdBytes + 1] = "";
  char showTitle[kShowTitleBytes + 1] = "";
  char title[kTitleBytes + 1] = "";
  uint32_t at = 0, seconds = 0, stamp = 0, bytes = 0;
};

inline bool idChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}
// p and digits for iTunes, a and an Archive identifier. Safe in a path and in a URL as is.
inline bool showIdValid(const char* id) {
  if (!id || (id[0] != 'p' && id[0] != 'a') || !id[1]) return false;
  size_t n = 1;
  for (; id[n]; ++n) {
    if (n > kIdBytes - 1 || !idChar(id[n]) || (id[0] == 'p' && (id[n] < '0' || id[n] > '9'))) return false;
  }
  return id[1] != '.' && id[1] != '-' && id[1] != '_';
}
inline bool episodeIdValid(const char* id) {
  if (!id) return false;
  size_t n = 0;
  for (; id[n]; ++n) if (n >= kEpisodeIdBytes || !((id[n] >= '0' && id[n] <= '9') || (id[n] >= 'a' && id[n] <= 'f'))) return false;
  return n == kEpisodeIdBytes;
}

// --- Reading lines ------------------------------------------------------------------------
// Splits one line at tabs, in place. Returns how many fields, at most `room`; the last field
// keeps any further tabs.
inline size_t splitFields(char* line, char** fields, size_t room) {
  size_t n = 0;
  if (!room) return 0;
  fields[n++] = line;
  for (char* p = line; *p && n < room; ++p) if (*p == '\t') { *p = 0; fields[n++] = p + 1; }
  return n;
}
// Copies at most room-1 bytes, never ending inside a UTF-8 letter.
inline void copyText(char* out, size_t room, const char* in) {
  if (!room) return;
  size_t n = strlen(in);
  if (n > room - 1) {
    n = room - 1;
    while (n && (static_cast<unsigned char>(in[n]) & 0xC0) == 0x80) --n;
  }
  memcpy(out, in, n);
  out[n] = 0;
}
inline bool readNumber(const char* s, uint32_t& out, uint32_t max = 0xFFFFFFFFu) {
  if (!s || !*s) return false;
  uint64_t n = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    n = n * 10 + uint64_t(*s - '0');
    if (n > max) return false;
  }
  out = uint32_t(n);
  return true;
}
// Calls fn(line, length) for each line of text, with the line copied so it can be cut up.
// Lines longer than the buffer are skipped. A final line without a newline still counts.
template <class Fn>
void eachLine(const char* text, size_t len, Fn fn) {
  char line[512];
  size_t n = 0;
  bool tooLong = false;
  for (size_t i = 0; i <= len; ++i) {
    const char c = i < len ? text[i] : '\n';
    if (c == '\r') continue;
    if (c == '\n') {
      if (!tooLong && n) { line[n] = 0; fn(line, n); }
      n = 0; tooLong = false;
      continue;
    }
    if (n + 1 >= sizeof(line)) tooLong = true;
    else line[n++] = c;
  }
}

// The shows a search found. Lines that aren't shows, or are bad, are skipped.
inline size_t parseShows(const char* text, size_t len, Show* out, size_t room) {
  size_t count = 0;
  eachLine(text, len, [&](char* line, size_t) {
    char* f[5];
    if (count >= room || splitFields(line, f, 5) != 5 || strcmp(f[0], "show") || !showIdValid(f[1]) || !*f[3]) return;
    uint32_t episodes = 0;
    if (!readNumber(f[2], episodes)) return;
    Show& show = out[count];
    show = Show{};
    copyText(show.id, sizeof(show.id), f[1]);
    show.count = uint16_t(episodes > 65535 ? 65535 : episodes);
    copyText(show.title, sizeof(show.title), f[3]);
    copyText(show.by, sizeof(show.by), f[4]);
    ++count;
  });
  return count;
}
// A page of a show's episodes. False without its "about" line.
inline bool parsePage(const char* text, size_t len, Page& out) {
  out = Page{};
  bool about = false;
  eachLine(text, len, [&](char* line, size_t) {
    char* f[5];
    const size_t n = splitFields(line, f, 5);
    if (n == 5 && !strcmp(f[0], "about") && !about) {
      uint32_t total = 0, from = 0;
      if (!readNumber(f[1], total) || !readNumber(f[2], from)) return;
      out.total = uint16_t(total > 65535 ? 65535 : total);
      out.from = uint16_t(from > 65535 ? 65535 : from);
      copyText(out.title, sizeof(out.title), f[3]);
      copyText(out.by, sizeof(out.by), f[4]);
      about = true;
    } else if (n == 5 && !strcmp(f[0], "ep") && out.count < kPage && episodeIdValid(f[1])) {
      uint32_t seconds = 0;
      if (!readNumber(f[2], seconds)) return;
      Episode& e = out.episodes[out.count++];
      copyText(e.id, sizeof(e.id), f[1]);
      e.seconds = seconds;
      copyText(e.date, sizeof(e.date), strcmp(f[3], "-") ? f[3] : "");
      copyText(e.title, sizeof(e.title), *f[4] ? f[4] : "Untitled");
    }
  });
  return about;
}

// --- The card's files ---------------------------------------------------------------------
// Tabs and line breaks would split a record, so they become spaces. Returns the length, or 0.
inline size_t putText(char* out, size_t room, size_t at, const char* text, bool last) {
  for (const char* p = text; *p; ++p) {
    if (at + 1 >= room) return 0;
    out[at++] = (*p == '\t' || *p == '\n' || *p == '\r') ? ' ' : *p;
  }
  if (at + 1 >= room) return 0;
  out[at++] = last ? '\n' : '\t';
  out[at] = 0;
  return at;
}
inline size_t putNumber(char* out, size_t room, size_t at, uint32_t value) {
  char digits[12];
  snprintf(digits, sizeof(digits), "%lu", static_cast<unsigned long>(value));
  return putText(out, room, at, digits, false);
}
inline size_t formatShow(char* out, size_t room, const Show& s) {
  size_t at = putText(out, room, 0, s.id, false);
  if (at) at = putNumber(out, room, at, s.count);
  if (at) at = putText(out, room, at, s.title, false);
  if (at) at = putText(out, room, at, s.by, true);
  return at;
}
inline bool parseShowLine(char* line, Show& s) {
  char* f[4];
  uint32_t count = 0;
  if (splitFields(line, f, 4) != 4 || !showIdValid(f[0]) || !readNumber(f[1], count) || !*f[2]) return false;
  s = Show{};
  copyText(s.id, sizeof(s.id), f[0]);
  s.count = uint16_t(count > 65535 ? 65535 : count);
  copyText(s.title, sizeof(s.title), f[2]);
  copyText(s.by, sizeof(s.by), f[3]);
  return true;
}
// progress.txt: show, episode, at, seconds, stamp, show title, title.
inline size_t formatProgress(char* out, size_t room, const Entry& e) {
  size_t at = putText(out, room, 0, e.show, false);
  if (at) at = putText(out, room, at, e.episode, false);
  if (at) at = putNumber(out, room, at, e.at);
  if (at) at = putNumber(out, room, at, e.seconds);
  if (at) at = putNumber(out, room, at, e.stamp);
  if (at) at = putText(out, room, at, e.showTitle, false);
  if (at) at = putText(out, room, at, e.title, true);
  return at;
}
inline bool parseProgressLine(char* line, Entry& e) {
  char* f[7];
  e = Entry{};
  if (splitFields(line, f, 7) != 7 || !showIdValid(f[0]) || !episodeIdValid(f[1]) ||
      !readNumber(f[2], e.at) || !readNumber(f[3], e.seconds) || !readNumber(f[4], e.stamp)) return false;
  copyText(e.show, sizeof(e.show), f[0]);
  copyText(e.episode, sizeof(e.episode), f[1]);
  copyText(e.showTitle, sizeof(e.showTitle), f[5]);
  copyText(e.title, sizeof(e.title), f[6]);
  return true;
}
// saved.txt: show, episode, seconds, bytes, show title, title.
inline size_t formatSaved(char* out, size_t room, const Entry& e) {
  size_t at = putText(out, room, 0, e.show, false);
  if (at) at = putText(out, room, at, e.episode, false);
  if (at) at = putNumber(out, room, at, e.seconds);
  if (at) at = putNumber(out, room, at, e.bytes);
  if (at) at = putText(out, room, at, e.showTitle, false);
  if (at) at = putText(out, room, at, e.title, true);
  return at;
}
inline bool parseSavedLine(char* line, Entry& e) {
  char* f[6];
  e = Entry{};
  if (splitFields(line, f, 6) != 6 || !showIdValid(f[0]) || !episodeIdValid(f[1]) ||
      !readNumber(f[2], e.seconds) || !readNumber(f[3], e.bytes)) return false;
  copyText(e.show, sizeof(e.show), f[0]);
  copyText(e.episode, sizeof(e.episode), f[1]);
  copyText(e.showTitle, sizeof(e.showTitle), f[4]);
  copyText(e.title, sizeof(e.title), f[5]);
  return true;
}
// Done: heard to within the last 45 seconds, or to the end when the length isn't known.
inline bool finished(uint32_t at, uint32_t seconds) {
  return seconds ? at + kDoneSeconds >= seconds : false;
}

// --- Addresses on the bridge --------------------------------------------------------------
inline size_t putEscaped(char* out, size_t room, size_t at, const char* text) {
  static const char kHex[] = "0123456789ABCDEF";
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    const bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                       *p == '-' || *p == '.' || *p == '_' || *p == '~';
    if (at + (plain ? 1 : 3) >= room) return 0;
    if (plain) out[at++] = char(*p);
    else { out[at++] = '%'; out[at++] = kHex[*p >> 4]; out[at++] = kHex[*p & 15]; }
  }
  out[at] = 0;
  return at;
}
// False when the words are empty, over 64 bytes, or won't fit.
inline bool searchPath(char* out, size_t room, Kind kind, const char* words) {
  if (!words) return false;
  while (*words == ' ') ++words;
  size_t n = strlen(words);
  while (n && words[n - 1] == ' ') --n;
  if (!n || n > 64) return false;
  char trimmed[65];
  memcpy(trimmed, words, n);
  trimmed[n] = 0;
  const int len = snprintf(out, room, "/api/cardputer/podcasts/search?kind=%s&q=", kindKey(kind));
  return len > 0 && size_t(len) < room && putEscaped(out, room, size_t(len), trimmed) > 0;
}
inline bool episodesPath(char* out, size_t room, const char* show, uint32_t from) {
  if (!showIdValid(show)) return false;
  const int len = snprintf(out, room, "/api/cardputer/podcasts/episodes?show=%s&from=%lu", show, static_cast<unsigned long>(from));
  return len > 0 && size_t(len) < room;
}
inline bool audioPath(char* out, size_t room, const char* show, const char* episode, uint32_t seconds) {
  if (!showIdValid(show) || !episodeIdValid(episode)) return false;
  const int len = snprintf(out, room, "/api/cardputer/podcasts/audio?show=%s&ep=%s&t=%lu", show, episode,
                           static_cast<unsigned long>(seconds));
  return len > 0 && size_t(len) < room;
}
inline bool filePath(char* out, size_t room, const char* episode, bool partial = false) {
  if (!episodeIdValid(episode)) return false;
  const int len = snprintf(out, room, "/podcasts/files/%s.%s", episode, partial ? "part" : "mp3");
  return len > 0 && size_t(len) < room;
}
// The ticket for an episode over the home network, as the bridge answers:
//   ticket <port> /podcasts/<32 hex>
//   lan <ip> <prefix>        one for each of the bridge computer's home addresses
// or "none" when it has no home network open. The device picks the address on its own network.
struct HomeTicket {
  char path[48] = "";
  uint16_t port = 0;
  uint8_t count = 0;
  uint32_t ip[4] = {};
  uint8_t prefix[4] = {};
  bool none = true;
};
inline bool parseIpv4(const char* s, uint32_t& out) {
  uint32_t value = 0;
  for (int part = 0; part < 4; ++part) {
    if (*s < '0' || *s > '9') return false;
    uint32_t n = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + uint32_t(*s++ - '0'); if (++digits > 3 || n > 255) return false; }
    value = (value << 8) | n;
    if (part < 3 && *s++ != '.') return false;
  }
  if (*s) return false;
  out = value;
  return true;
}
inline bool ticketPathValid(const char* path) {
  static const char kStart[] = "/podcasts/";
  if (strncmp(path, kStart, sizeof(kStart) - 1)) return false;
  const char* id = path + sizeof(kStart) - 1;
  size_t n = 0;
  for (; id[n]; ++n) if (n >= 32 || !((id[n] >= '0' && id[n] <= '9') || (id[n] >= 'a' && id[n] <= 'f'))) return false;
  return n == 32;
}
// False when the answer isn't a ticket at all, so an old bridge reads as having none.
inline bool parseTicket(const char* text, size_t len, HomeTicket& out) {
  out = HomeTicket{};
  bool none = false, ticket = false, bad = false;
  eachLine(text, len, [&](char* line, size_t) {
    char* f[3];
    const size_t n = splitFields(line, f, 3);
    uint32_t number = 0, ip = 0;
    if (n == 1 && !strcmp(f[0], "none")) none = true;
    else if (n == 3 && !strcmp(f[0], "ticket") && readNumber(f[1], number, 65535) && number && ticketPathValid(f[2]) && strlen(f[2]) < sizeof(out.path)) {
      out.port = uint16_t(number); strcpy(out.path, f[2]); ticket = true;
    } else if (n == 3 && !strcmp(f[0], "lan") && parseIpv4(f[1], ip) && readNumber(f[2], number, 30) && number >= 8) {
      if (out.count < 4) { out.ip[out.count] = ip; out.prefix[out.count] = uint8_t(number); ++out.count; }
    } else bad = true;
  });
  if (bad && !ticket) return false;
  out.none = none || !ticket || !out.count;
  return none || ticket;
}
// The bridge's address on the same network as `mine`, or 0 when none is.
inline uint32_t homeFor(const HomeTicket& t, uint32_t mine) {
  if (t.none || !mine) return 0;
  for (uint8_t i = 0; i < t.count; ++i) {
    const uint32_t mask = t.prefix[i] >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> t.prefix[i]);
    if ((t.ip[i] & mask) == (mine & mask) && t.ip[i] != mine) return t.ip[i];
  }
  return 0;
}
inline bool ticketPath(char* out, size_t room, const char* show, const char* episode) {
  if (!showIdValid(show) || !episodeIdValid(episode)) return false;
  const int len = snprintf(out, room, "/api/cardputer/podcasts/ticket?show=%s&ep=%s", show, episode);
  return len > 0 && size_t(len) < room;
}
inline bool homeAudioPath(char* out, size_t room, const HomeTicket& t, uint32_t seconds) {
  if (t.none || !ticketPathValid(t.path)) return false;
  const int len = snprintf(out, room, "%s?t=%lu", t.path, static_cast<unsigned long>(seconds));
  return len > 0 && size_t(len) < room;
}

// Where a second starts in a saved file: a whole frame, since the bridge's are all one size.
inline uint32_t fileOffset(uint32_t seconds) {
  const uint64_t frame = uint64_t(seconds) * kRate / kFrameSamples;
  return uint32_t(frame * kFrameBytes);
}

// --- Times on the screen ------------------------------------------------------------------
// 0:42, 12:05 or 1:02:03.
inline void clock(char* out, size_t room, uint32_t seconds) {
  const unsigned h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
  if (h) snprintf(out, room, "%u:%02u:%02u", h, m, s);
  else snprintf(out, room, "%u:%02u", m, s);
}
// 45m or 1h 5m, short enough for a list's right-hand column; and with left, what remains.
inline void length(char* out, size_t room, uint32_t seconds) {
  const unsigned minutes = (seconds + 30) / 60;
  if (!seconds) snprintf(out, room, "%s", "");
  else if (minutes < 60) snprintf(out, room, "%um", minutes ? minutes : 1);
  else if (minutes % 60) snprintf(out, room, "%uh %um", minutes / 60, minutes % 60);
  else snprintf(out, room, "%uh", minutes / 60);
}
inline void left(char* out, size_t room, uint32_t at, uint32_t seconds) {
  if (finished(at, seconds)) { snprintf(out, room, "%s", "done"); return; }
  if (!seconds || at >= seconds) { clock(out, room, at); return; }
  char rest[16];
  length(rest, sizeof(rest), seconds - at);
  snprintf(out, room, "%s left", rest);
}
}  // namespace podcast
