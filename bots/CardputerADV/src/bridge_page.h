#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// System > Bridge page: the address of the bridge's web page, asked from the bridge each time the
// screen opens, so it's never written into the firmware. The bridge answers one line:
//   url <https://...>      the page, as a phone on the owner's tailnet opens it
//   none <why>             no page to show, in a few words for the screen
namespace bridge_page {

constexpr const char kPath[] = "/api/cardputer/ui/link";
constexpr size_t kUrlChars = 120;
constexpr size_t kWhyChars = 48;
constexpr int kMaxLines = 6;

enum class Answer : uint8_t { Bad, Link, None };

struct Link {
  char url[kUrlChars] = "";
  char why[kWhyChars] = "";
};

// A link is http(s) and plain printable ASCII with no spaces, short enough to keep whole.
inline bool urlValid(const char* text, size_t length) {
  if (length < 10 || length >= kUrlChars) return false;
  if (strncmp(text, "https://", 8) != 0 && strncmp(text, "http://", 7) != 0) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c <= ' ' || c >= 0x7f) return false;
  }
  return true;
}

// Reads the bridge's answer into out. Anything it can't use is Bad, and out keeps what it had.
inline Answer parse(const char* text, size_t length, Link& out) {
  if (!text) return Answer::Bad;
  size_t end = 0;
  while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
  const char* tab = static_cast<const char*>(memchr(text, '\t', end));
  if (!tab) return Answer::Bad;
  const size_t keyLength = static_cast<size_t>(tab - text);
  const char* value = tab + 1;
  const size_t valueLength = end - keyLength - 1;
  if (keyLength == 3 && !strncmp(text, "url", 3)) {
    if (!urlValid(value, valueLength)) return Answer::Bad;
    memcpy(out.url, value, valueLength);
    out.url[valueLength] = 0;
    out.why[0] = 0;
    return Answer::Link;
  }
  if (keyLength == 4 && !strncmp(text, "none", 4)) {
    char why[kWhyChars];
    size_t n = 0;
    for (size_t i = 0; i < valueLength && n + 1 < kWhyChars; ++i) {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      if (c >= ' ' && c < 0x7f) why[n++] = static_cast<char>(c);
    }
    if (!n) return Answer::Bad;
    why[n] = 0;
    memcpy(out.why, why, n + 1);
    return Answer::None;
  }
  return Answer::Bad;
}

// What's left of a link once its scheme is off, which is what a person reads and types.
inline const char* shown(const char* url) {
  if (!strncmp(url, "https://", 8)) return url + 8;
  if (!strncmp(url, "http://", 7)) return url + 7;
  return url;
}

// Breaks text into lines no wider than maxWidth, preferring to break after . : / or -, so a
// host name splits where a person would. width(text, n) measures the first n characters.
// Returns the number of lines; each starts at starts[i] and runs for lengths[i] characters.
template <class Width>
int wrap(const char* text, int maxWidth, Width width, size_t* starts, size_t* lengths, int maxLines) {
  const size_t total = strlen(text);
  int lines = 0;
  size_t at = 0;
  while (at < total && lines < maxLines) {
    size_t fit = 0;
    while (at + fit < total && width(text + at, fit + 1) <= maxWidth) ++fit;
    if (!fit) fit = 1;  // One character always goes, even one too wide.
    size_t take = fit;
    if (at + fit < total) {
      for (size_t i = fit; i > fit / 3; --i) {  // Unless the line would be left under a third full.
        const char c = text[at + i - 1];
        if (c == '.' || c == ':' || c == '/' || c == '-') { take = i; break; }
      }
    }
    starts[lines] = at;
    lengths[lines] = take;
    ++lines;
    at += take;
  }
  return lines;
}

}  // namespace bridge_page
