#include "internet_radio_transport.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <atomic>
// Upstream lieff/minimp3, CC0: vendor/minimp3.LICENSE.
// Pinned official source: https://github.com/lieff/minimp3
// Commit ea99364f61c14656440e8d77e9c233ccf3124633 (2026-10-22 retrieval).
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "vendor/minimp3.h"

namespace internet_radio { namespace detail {
static bool number(const char* s, uint32_t& n, uint32_t max) {
  if (!*s) return false;
  n = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9' || n > (max - unsigned(*s - '0')) / 10) return false;
    n = n * 10 + unsigned(*s - '0');
  }
  return true;
}
bool parseUrl(const char* s, Url& out) {
  if (!s || strnlen(s, kUrlSize) >= kUrlSize || strncmp(s, "http://", 7)) return false;
  Url u;
  const char* p = s + 7; const char* begin = p;
  while (*p && *p != '/' && *p != ':' && *p != '?') {
    if (!(isalnum(static_cast<unsigned char>(*p)) || *p == '.' || *p == '-')) return false;
    ++p;
  }
  size_t n = p - begin;
  if (!n || n >= sizeof(u.host)) return false;
  memcpy(u.host, begin, n); u.host[n] = 0;
  if (*p == ':') {
    const char* port = ++p; while (isdigit(static_cast<unsigned char>(*p))) ++p;
    char b[6]; n = p - port; if (!n || n >= sizeof(b)) return false;
    memcpy(b, port, n); b[n] = 0; uint32_t value;
    if (!number(b, value, 65535) || !value) return false;
    u.port = value;
  }
  if (!*p) strcpy(u.path, "/");
  else {
    if (*p != '/' || strlen(p) >= sizeof(u.path)) return false;
    for (const char* q = p; *q; ++q)
      if (static_cast<unsigned char>(*q) <= 32 || static_cast<unsigned char>(*q) >= 127 || *q == '#' || *q == '@' || *q == '\\') return false;
    strcpy(u.path, p);
  }
  out = u; return true;
}
bool publicIpv4(uint32_t ip) {
  unsigned a = ip >> 24, b = (ip >> 16) & 255, c = (ip >> 8) & 255;
  return !(a == 0 || a == 10 || a == 127 || a >= 224 ||
    (a == 100 && b >= 64 && b <= 127) || (a == 169 && b == 254) ||
    (a == 172 && b >= 16 && b <= 31) || (a == 192 && (b == 168 || b == 0)) ||
    (a == 192 && b == 2) || (a == 198 && (b == 18 || b == 19 || (b == 51 && c == 100))) ||
    (a == 203 && b == 0 && c == 113));
}
Error Headers::line(const char* s) {
  total += strlen(s) + 2;
  if (done || total > 8192 || strlen(s) > 767) return Error::Malformed;
  if (!status) {
    if (!strncmp(s, "HTTP/1.0 ", 9) || !strncmp(s, "HTTP/1.1 ", 9)) s += 9;
    else if (!strncmp(s, "ICY ", 4)) s += 4;
    else return Error::Malformed;
    if (strlen(s) < 3 || !isdigit(s[0]) || !isdigit(s[1]) || !isdigit(s[2]) || (s[3] && s[3] != ' ')) return Error::Malformed;
    status = (s[0]-'0')*100 + (s[1]-'0')*10 + s[2]-'0';
    return status >= 100 && status <= 599 ? Error::None : Error::Malformed;
  }
  if (!*s) {
    done = true;
    if (chunked && hasLength) return Error::Malformed;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
      return hasLocation ? Error::None : Error::Redirect;
    if (status != 200) return Error::Http;
    return hasType ? Error::None : Error::Unsupported;
  }
  const char* colon = strchr(s, ':');
  if (!colon || colon == s) return Error::Malformed;
  char name[128]; size_t len = colon-s;
  if (len >= sizeof(name)) return Error::Malformed;
  for (size_t i=0;i<len;++i) {
    unsigned char c = s[i]; if (!(isalnum(c) || c == '-')) return Error::Malformed;
    name[i] = tolower(c);
  }
  name[len] = 0;
  s = colon+1; while (*s == ' ' || *s == '\t') ++s;
  char value[768]; len = strlen(s); while (len && (s[len-1] == ' ' || s[len-1] == '\t')) --len;
  for (size_t i=0;i<len;++i) { unsigned char c=s[i]; if ((c < 32 && c != '\t') || c == 127) return Error::Malformed; value[i]=c; }
  value[len]=0;
  if (!strcmp(name,"location")) {
    if (hasLocation || len >= sizeof(location)) return Error::Malformed;
    hasLocation = true; strcpy(location,value);
  } else if (!strcmp(name,"content-type")) {
    if (hasType) return Error::Malformed;
    char* semi = strchr(value,';'); if (semi) *semi=0;
    for (char* p=value;*p;++p) *p=tolower(static_cast<unsigned char>(*p));
    const bool audio = !strcmp(value,"audio/mpeg") || !strcmp(value,"audio/mp3");
    const bool wanted = json ? !strcmp(value,"application/json") : image ? !strcmp(value,"image/jpeg") :
                        text ? !strcmp(value,"text/plain") : audio;
    if (status == 200 && !wanted) return Error::Unsupported;
    hasType=true;
  } else if (!strcmp(name,"transfer-encoding")) {
    if (hasTransfer || strcasecmp(value,"chunked")) return Error::Malformed;
    hasTransfer=true; chunked=true;
  } else if (!strcmp(name,"content-length")) {
    if (hasLength || !number(value,length,0x7fffffff)) return Error::Malformed;
    hasLength=true;
  } else if (!strcmp(name,"icy-br")) {
    unsigned n = 0; for (const char* p = value; *p >= '0' && *p <= '9' && n < 10000; ++p) n = n * 10 + unsigned(*p - '0');
    bitrate = n < 10000 ? uint16_t(n) : 0;
  } else if (!strcmp(name,"icy-metaint")) {
    if (hasMeta || !number(value,metaint,16*1024*1024) || !metaint) return Error::Malformed;
    hasMeta=true;
  } else if (!strcmp(name,"content-encoding") && strcasecmp(value,"identity")) return Error::Unsupported;
  return Error::None;
}
Body::Body(const Headers& h) : chunked_(h.chunked), bounded_(h.hasLength),
  remaining_(h.length), interval_(h.metaint), audioLeft_(h.metaint) {
  if (bounded_ && !remaining_) ended_=true;
}
int Body::entity(uint8_t b) {
  if (!interval_) return 1;
  if (metadata_) {
    // Titles come first in a block; the rest of a long block is counted, not kept.
    if (metaUsed_ < sizeof(meta_)-1) meta_[metaUsed_++]=char(b);
    if (!--metadata_) { audioLeft_=interval_; meta_[metaUsed_]=0; readTitle(); }
    return 0;
  }
  if (audioLeft_) { --audioLeft_; return 1; }
  metadata_=unsigned(b)*16; metaUsed_=0; if (!metadata_) audioLeft_=interval_; return 0;
}
void Body::readTitle() {
  static const char kKey[]="StreamTitle='";
  const char* p=strstr(meta_,kKey);  // A block without a title keeps the last one showing.
  if (!p) return;
  p+=sizeof(kKey)-1;
  const char* end=strstr(p,"';");
  size_t n=end ? size_t(end-p) : strlen(p);
  if (!end && n && p[n-1]=='\'') --n;
  cleanText(p,n,title_,sizeof(title_));
  ++titles_;
}
int Body::feed(uint8_t b) {
  if (bad_ || ended_) { bad_=true; return -1; }
  if (!chunked_) {
    int r=entity(b); if (bounded_ && --remaining_ == 0) ended_=true; return r;
  }
  switch (phase_) {
    case Phase::Size: {
      int d = b>='0'&&b<='9' ? b-'0' : b>='a'&&b<='f' ? b-'a'+10 : b>='A'&&b<='F' ? b-'A'+10 : -1;
      if (d>=0 && digits_<8 && chunk_ <= (1024*1024-unsigned(d))/16) { chunk_=chunk_*16+d; ++digits_; return 0; }
      if (b=='\r' && digits_) { phase_=Phase::SizeLF; return 0; }
      break; // Strict subset: extensions/trailers are rejected, not misparsed.
    }
    case Phase::SizeLF: if (b=='\n') { phase_=chunk_ ? Phase::Data : Phase::EndCR; return 0; } break;
    case Phase::Data: { int r=entity(b); if (!--chunk_) phase_=Phase::DataCR; return r; }
    case Phase::DataCR: if (b=='\r') { phase_=Phase::DataLF; return 0; } break;
    case Phase::DataLF: if (b=='\n') { phase_=Phase::Size; digits_=0; return 0; } break;
    case Phase::EndCR: if (b=='\r') { phase_=Phase::EndLF; return 0; } break;
    case Phase::EndLF: if (b=='\n' && !metadata_ && (!interval_ || audioLeft_)) { ended_=true; return 0; } break;
  }
  bad_=true; return -1;
}
bool Body::complete() const {
  return !bad_ && (!chunked_ || ended_) && (!bounded_ || !remaining_) && !metadata_ && (!interval_ || audioLeft_);
}
static size_t utf8Length(unsigned char c) {
  return c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
}
static bool validUtf8(const unsigned char* s, size_t n) {
  for (size_t i = 0; i < n;) {
    const size_t len = utf8Length(s[i]);
    if (!len || (len == 2 && s[i] < 0xC2) || i + len > n) return false;
    for (size_t k = 1; k < len; ++k) if ((s[i+k] & 0xC0) != 0x80) return false;
    i += len;
  }
  return true;
}
size_t cleanText(const char* in, size_t n, char* out, size_t room) {
  if (!out || !room) return 0;
  const auto* s = reinterpret_cast<const unsigned char*>(in);
  const bool utf8 = validUtf8(s, n);
  size_t len = 0;
  for (size_t i = 0; i < n; ++i) {
    unsigned c = s[i];
    if (utf8 && c >= 0x80) {
      const size_t k = utf8Length(c);
      if (len + k > room - 1) break;
      memcpy(out + len, in + i, k); len += k; i += k - 1;
      continue;
    }
    // Latin-1's control block 0x80-0x9F has no letters worth showing.
    if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xA0)) c = ' ';
    if (c < 0x80) {
      if (len + 1 > room - 1) break;
      if (c == ' ' && (!len || out[len-1] == ' ')) continue;  // No runs of spaces, none in front.
      out[len++] = char(c);
    } else {
      if (len + 2 > room - 1) break;
      out[len++] = char(0xC0 | (c >> 6)); out[len++] = char(0x80 | (c & 0x3F));
    }
  }
  while (len && out[len-1] == ' ') --len;
  out[len] = 0;
  return len;
}
// Appends s, escaping what an address can't carry. Keeps : and / so a logo's address stays
// short enough to fit inside another.
static bool appendEscaped(char* out, size_t room, size_t& at, const char* s, size_t n) {
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '-' || c == '.' || c == '_' || c == '~' || c == ':' || c == '/';
    if (at + (plain ? 1 : 3) >= room) return false;
    if (plain) out[at++] = char(c);
    else { out[at++] = '%'; out[at++] = kHex[c >> 4]; out[at++] = kHex[c & 15]; }
  }
  out[at] = 0;
  return true;
}
static bool startsWith(const char* s, const char* prefix) { return !strncmp(s, prefix, strlen(prefix)); }
static bool composed(char* out, size_t room, const char* head, const char* value, const char* tail) {
  if (!out || !room || !value) return false;
  size_t at = strlen(head);
  if (at >= room) return false;
  memcpy(out, head, at + 1);
  if (!appendEscaped(out, room, at, value, strlen(value))) return false;
  const size_t n = strlen(tail);
  if (at + n >= room || at + n >= kUrlSize) return false;
  memcpy(out + at, tail, n + 1);
  return true;
}
bool logoImageUrl(char* out, size_t room, const char* logo) {
  if (!logo || !(startsWith(logo, "http://") || startsWith(logo, "https://"))) return false;
  for (const char* p = logo; *p; ++p) if (static_cast<unsigned char>(*p) <= ' ' || static_cast<unsigned char>(*p) >= 127) return false;
  // Square and cropped, at the visuals' size; the tile's is drawn from it here. A JPEG, which
  // needs only a small decoder.
  return composed(out, room, "http://wsrv.nl/?url=", logo, "&w=96&h=96&fit=cover&output=jpg&q=85");
}
bool logoByStreamUrl(char* out, size_t room, const char* stream) {
  return stream && *stream && composed(out, room, "http://all.api.radio-browser.info/json/stations/byurl?url=", stream, "");
}
bool logoByNameUrl(char* out, size_t room, const char* name) {
  return name && *name && composed(out, room, "http://all.api.radio-browser.info/json/stations/search?name=", name, "&limit=10");
}
// The last two labels of a host, like somafm.com.
static const char* site(const char* host, size_t n, size_t& len) {
  size_t dots = 0, i = n;
  while (i > 0) { if (host[i - 1] == '.' && ++dots == 2) break; --i; }
  len = n - i;
  return host + i;
}
bool sameSite(const char* host, const char* address) {
  if (!host || !address) return false;
  const char* a = strstr(address, "://");
  a = a ? a + 3 : address;
  size_t an = 0;
  while (a[an] && a[an] != '/' && a[an] != ':' && a[an] != '?') ++an;
  size_t aLen = 0, hLen = 0;
  const char* as = site(a, an, aLen);
  const char* hs = site(host, strlen(host), hLen);
  return aLen && aLen == hLen && !strncasecmp(as, hs, aLen) && memchr(hs, '.', hLen);
}
bool searchUrl(char* out, size_t room, bool byGenre, const char* text, const char* country) {
  if (!out || !room || !text) return false;
  if (!country) country = "";
  if (*country && (strlen(country) != 2 || country[0] < 'A' || country[0] > 'Z' || country[1] < 'A' || country[1] > 'Z')) return false;
  while (*text == ' ') ++text;
  size_t n = strlen(text);
  while (n && text[n-1] == ' ') --n;
  if (n > 64) return false;
  // Nothing typed asks for no name or tag, which is the directory's most listened.
  int len = snprintf(out, room, "http://all.api.radio-browser.info/json/stations/search?%s", !n ? "" : byGenre ? "tag=" : "name=");
  if (len <= 0 || size_t(len) >= room) return false;
  size_t at = size_t(len);
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
    if (at + (plain ? 1 : 3) >= room) return false;
    if (plain) out[at++] = char(c);
    else { out[at++] = '%'; out[at++] = kHex[c >> 4]; out[at++] = kHex[c & 15]; }
  }
  if (n) { if (at + 1 >= room) return false; out[at++] = '&'; }
  if (*country) {
    if (at + 16 >= room) return false;
    memcpy(out + at, "countrycode=", 12); at += 12;
    out[at++] = country[0]; out[at++] = country[1]; out[at++] = '&';
  }
  // Only what this player can play, most listened first. 30 leaves room for the duplicates.
  static const char kRest[] = "codec=MP3&is_https=false&hidebroken=true&order=clickcount&reverse=true&limit=30";
  if (at + sizeof(kRest) > room || at + sizeof(kRest) > kUrlSize) return false;
  memcpy(out + at, kRest, sizeof(kRest));
  return true;
}
// In place, radix 2. n must be a power of two.
static void fft(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float angle = -6.28318531f / float(len);
    const float wr = cosf(angle), wi = sinf(angle);
    for (int i = 0; i < n; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len / 2; ++k) {
        const int a = i + k, b = a + len / 2;
        const float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
        const float t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}
void analyse(const int16_t* pcm, int samples, int channels, int hz, Visuals& out) {
  constexpr int kN = 512;
  float re[kN], im[kN];
  const int stereo = channels == 2 ? 1 : 0;
  if (!pcm || samples < 0) samples = 0;
  for (int i = 0; i < kN; ++i) {
    const float v = i >= samples ? 0.0f : stereo ? (float(pcm[2 * i]) + float(pcm[2 * i + 1])) * 0.5f : float(pcm[i]);
    re[i] = v * (0.5f - 0.5f * cosf(6.28318531f * float(i) / float(kN - 1)));
    im[i] = 0.0f;
  }
  fft(re, im, kN);
  const float binHz = float(hz > 0 ? hz : 44100) / float(kN);
  const float ref = float(kN) / 4.0f * 32768.0f;  // A full-scale sine's peak after the window.
  int lo = 1;
  for (size_t b = 0; b < kBands; ++b) {
    const float edge = 60.0f * powf(16000.0f / 60.0f, float(b + 1) / float(kBands));
    int hi = int(edge / binHz + 0.5f);
    if (hi <= lo) hi = lo + 1;  // Low bands are narrower than a bin: one bin each.
    if (hi > kN / 2) hi = kN / 2;
    float peak = 0.0f;
    for (int k = lo; k < hi; ++k) { const float p = re[k] * re[k] + im[k] * im[k]; if (p > peak) peak = p; }
    const float db = peak > 0.0f ? 10.0f * log10f(peak / (ref * ref)) : -120.0f;
    const float v = (db + 60.0f) * 255.0f / 60.0f;
    out.bands[b] = uint8_t(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
    lo = hi;
  }
  // About 11 ms from the frame's start, every fourth sample, as a scope would show it. Spread
  // over the whole frame instead, a tone can line up with the spacing and look flat.
  for (size_t i = 0; i < kWavePoints; ++i) {
    const int s = int(i) * 4;
    const int v = s >= samples ? 0 : stereo ? (int(pcm[2 * s]) + int(pcm[2 * s + 1])) / 2 : int(pcm[s]);
    out.wave[i] = int8_t(v >> 8);
  }
}
void StationJson::put(char c) {
  if (!dst_) return;
  if (dstLen_ + 1 < dstRoom_) dst_[dstLen_++] = c; else overflow_ = true;
}
void StationJson::emit(uint32_t cp) {
  if (cp < 0x20 || cp == 0x7f) cp = ' ';
  char b[4]; size_t n;
  if (cp < 0x80) { b[0] = char(cp); n = 1; }
  else if (cp < 0x800) { b[0] = char(0xC0 | (cp >> 6)); b[1] = char(0x80 | (cp & 0x3F)); n = 2; }
  else if (cp < 0x10000) { b[0] = char(0xE0 | (cp >> 12)); b[1] = char(0x80 | ((cp >> 6) & 0x3F)); b[2] = char(0x80 | (cp & 0x3F)); n = 3; }
  else { b[0] = char(0xF0 | (cp >> 18)); b[1] = char(0x80 | ((cp >> 12) & 0x3F)); b[2] = char(0x80 | ((cp >> 6) & 0x3F)); b[3] = char(0x80 | (cp & 0x3F)); n = 4; }
  if (!dst_) return;
  if (dstLen_ + n < dstRoom_) { memcpy(dst_ + dstLen_, b, n); dstLen_ += n; } else overflow_ = true;
}
// Inside a string, key or value. A lone half of a surrogate pair shows as '?', which every
// font has, rather than a replacement character the renderer would go looking for.
bool StationJson::text(uint8_t c, bool& closed) {
  closed = false;
  if (escape_ == 1) {
    escape_ = 0;
    if (c == 'u') { escape_ = 2; unit_ = 0; return true; }
    if (high_) { emit('?'); high_ = 0; }
    if (c == '"' || c == '\\' || c == '/') put(char(c));
    else if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') put(' ');
    else return fail();
    return true;
  }
  if (escape_ >= 2) {
    const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return fail();
    unit_ = unit_ * 16 + unsigned(d);
    if (++escape_ < 6) return true;
    escape_ = 0;
    if (high_) {
      if (unit_ >= 0xDC00 && unit_ <= 0xDFFF) { emit(0x10000 + ((high_ - 0xD800) << 10) + (unit_ - 0xDC00)); high_ = 0; return true; }
      emit('?'); high_ = 0;
    }
    if (unit_ >= 0xD800 && unit_ <= 0xDBFF) high_ = unit_;
    else emit(unit_ >= 0xDC00 && unit_ <= 0xDFFF ? '?' : unit_);
    return true;
  }
  if (c == '\\') { escape_ = 1; return true; }
  if (high_) { emit('?'); high_ = 0; }
  if (c == '"') { closed = true; return true; }
  put(c < 0x20 ? ' ' : char(c));
  return true;
}
void StationJson::startValue() {
  dst_ = nullptr; dstRoom_ = 0;
  switch (field_) {
    case Field::Name: dst_ = cur_.name; dstRoom_ = sizeof(cur_.name); break;
    case Field::Url: dst_ = cur_.url; dstRoom_ = sizeof(cur_.url); break;
    case Field::Tags: dst_ = cur_.tags; dstRoom_ = sizeof(cur_.tags); break;
    case Field::Country: dst_ = cur_.country; dstRoom_ = sizeof(cur_.country); break;
    case Field::Codec: dst_ = codec_; dstRoom_ = sizeof(codec_); break;
    case Field::Logo: dst_ = cur_.logo; dstRoom_ = sizeof(cur_.logo); break;
    default: break;
  }
  dstLen_ = 0; overflow_ = false; escape_ = 0; high_ = 0;
}
void StationJson::endValue() {
  if (!dst_) return;
  if (overflow_) {
    // Cut short: drop a letter whose last bytes didn't fit.
    size_t j = dstLen_;
    while (j && (static_cast<unsigned char>(dst_[j-1]) & 0xC0) == 0x80) --j;
    if (j && utf8Length(static_cast<unsigned char>(dst_[j-1])) > dstLen_ - (j - 1)) dstLen_ = j - 1;
    if (field_ == Field::Url) urlTooLong_ = true;  // A cut address would play nothing.
    if (field_ == Field::Logo) logoTooLong_ = true;  // Nor would a cut logo show.
  }
  dst_[dstLen_] = 0;
  dst_ = nullptr;
}
void StationJson::endItem() {
  phase_ = Phase::AfterItem;
  expectItem_ = false;
  char clean[sizeof(cur_.name)];
  size_t n = cleanText(cur_.name, strlen(cur_.name), clean, sizeof(clean));
  memcpy(cur_.name, clean, n + 1);
  char tags[sizeof(cur_.tags)];
  memcpy(cur_.tags, tags, cleanText(cur_.tags, strlen(cur_.tags), tags, sizeof(tags)) + 1);
  if (logoTooLong_) cur_.logo[0] = 0;
  if (logos_) {  // A lookup: any station with a logo, its address kept as it came.
    if (cur_.logo[0] && count_ < room_) { if (urlTooLong_) cur_.url[0] = 0; out_[count_++] = cur_; }
    return;
  }
  Url url;
  if (!n || urlTooLong_ || hls_ || count_ >= room_ || !parseUrl(cur_.url, url)) return;
  if (codec_[0] && strcasecmp(codec_, "MP3")) return;
  for (size_t i = 0; i < count_; ++i) if (!strcmp(out_[i].url, cur_.url)) return;
  out_[count_++] = cur_;
}
bool StationJson::feed(uint8_t c) {
  const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
  switch (phase_) {
    case Phase::Start:
      if (space) return true;
      if (c != '[') return fail();
      phase_ = Phase::Item; return true;
    case Phase::Item:
      if (space) return true;
      if (c == ']' && !expectItem_) { phase_ = Phase::Done; return true; }
      if (c != '{') return fail();
      cur_ = Found{}; codec_[0] = 0; hls_ = urlTooLong_ = logoTooLong_ = expectKey_ = false;
      phase_ = Phase::Key; return true;
    case Phase::AfterItem:
      if (space) return true;
      if (c == ',') { expectItem_ = true; phase_ = Phase::Item; return true; }
      if (c == ']') { phase_ = Phase::Done; return true; }
      return fail();
    case Phase::Key:
      if (space) return true;
      if (c == '}' && !expectKey_) { endItem(); return true; }
      if (c != '"') return fail();
      dst_ = key_; dstRoom_ = sizeof(key_); dstLen_ = 0; overflow_ = false; escape_ = 0; high_ = 0;
      phase_ = Phase::KeyText; return true;
    case Phase::KeyText: {
      bool closed = false;
      if (!text(c, closed)) return false;
      if (!closed) return true;
      key_[dstLen_] = 0; dst_ = nullptr;
      field_ = overflow_ ? Field::None : !strcmp(key_, "name") ? Field::Name : !strcmp(key_, "url_resolved") ? Field::Url :
        !strcmp(key_, "tags") ? Field::Tags : !strcmp(key_, "countrycode") ? Field::Country :
        !strcmp(key_, "codec") ? Field::Codec :
        !strcmp(key_, "bitrate") ? Field::Bitrate : !strcmp(key_, "hls") ? Field::Hls :
        !strcmp(key_, "favicon") ? Field::Logo : Field::None;
      phase_ = Phase::Colon; return true;
    }
    case Phase::Colon:
      if (space) return true;
      if (c != ':') return fail();
      phase_ = Phase::Value; return true;
    case Phase::Value:
      if (space) return true;
      if (c == '"') { startValue(); phase_ = Phase::ValueText; return true; }
      if (c == '-' || (c >= '0' && c <= '9')) { number_ = c == '-' ? 0 : unsigned(c - '0'); unit_ = c == '-'; phase_ = Phase::Number; return true; }
      if (c >= 'a' && c <= 'z') { unit_ = c; phase_ = Phase::Word; return true; }
      if (c == '{' || c == '[') { depth_ = 1; nestedString_ = nestedEscape_ = false; phase_ = Phase::Nested; return true; }
      return fail();
    case Phase::ValueText: {
      bool closed = false;
      if (!text(c, closed)) return false;
      if (closed) { endValue(); phase_ = Phase::AfterMember; }
      return true;
    }
    case Phase::Number:
      // unit_ says the integer part is over: a minus sign, a point or an exponent.
      if (c >= '0' && c <= '9') { if (!unit_ && number_ < 1000000) number_ = number_ * 10 + unsigned(c - '0'); return true; }
      if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') { unit_ = 1; return true; }
      if (field_ == Field::Bitrate) cur_.bitrate = uint16_t(number_ > 65535 ? 65535 : number_);
      if (field_ == Field::Hls) hls_ = number_ != 0;
      phase_ = Phase::AfterMember; return feed(c);
    case Phase::Word:
      if (c >= 'a' && c <= 'z') return true;
      if (field_ == Field::Hls) hls_ = unit_ == 't';
      phase_ = Phase::AfterMember; return feed(c);
    case Phase::Nested:
      if (nestedString_) {
        if (nestedEscape_) nestedEscape_ = false;
        else if (c == '\\') nestedEscape_ = true;
        else if (c == '"') nestedString_ = false;
        return true;
      }
      if (c == '"') nestedString_ = true;
      else if (c == '{' || c == '[') { if (++depth_ > 32) return fail(); }
      else if ((c == '}' || c == ']') && !--depth_) phase_ = Phase::AfterMember;
      return true;
    case Phase::AfterMember:
      if (space) return true;
      if (c == ',') { expectKey_ = true; phase_ = Phase::Key; return true; }
      if (c == '}') { endItem(); return true; }
      return fail();
    case Phase::Done: return true;
    case Phase::Bad: break;
  }
  return false;
}
} } // namespace internet_radio::detail

#ifndef INTERNET_RADIO_HOST_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <errno.h>

namespace internet_radio {
namespace {
constexpr uint8_t kChannel=7;
constexpr size_t kInput=8192;
constexpr uint32_t kIdleMs=5000, kHeaderMs=8000;
constexpr uint32_t kSearchBytes=128*1024;  // 30 stations come to about 35 KB.
enum class Job : uint8_t { Stream, Search, Logo };
enum class Want : uint8_t { Audio, Json, Image };
std::atomic<State> current{State::Idle};
std::atomic<Error> failure{Error::None};
std::atomic<bool> owned{false}, cancel{false};
std::atomic<uint32_t> received{0};
std::atomic<uint16_t> loudness{0}, kbps{0};
std::atomic<bool> visualsWanted{false};
Visuals shownVisuals{};  // Guarded by gate.
std::atomic<Job> job{Job::Stream};
char nowTitle[128];  // Guarded by gate.
// Search results outlive the search, so a found station can play while the list shows.
Found* results=nullptr;
std::atomic<size_t> resultCount{0};
// A logo job's inputs, written only while no job runs, and its JPEG once it has one.
char logoStream[detail::kUrlSize], logoName[64], logoGiven[sizeof(Found::logo)];
uint8_t* logoData=nullptr;
std::atomic<size_t> logoLength{0};
std::atomic<LogoResult> logoOutcome{LogoResult::None};
unsigned logoStatus=0;  // The image service's HTTP status, so a 404 reads as Missing.
portMUX_TYPE gate=portMUX_INITIALIZER_UNLOCKED;
char requested[detail::kUrlSize];
// lwIP DNS callbacks may outlive a cancelled worker. Static storage deliberately
// survives it; a new lookup is refused until the previous callback completes.
std::atomic<bool> dnsPending{false}, dnsReady{false};
std::atomic<uint32_t> dnsAddress{0};
char dnsName[128];
void dnsFound(const char*, const ip_addr_t* addr, void*) {
  uint32_t v=0;
  if (addr && IP_IS_V4(addr)) v=lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr)));
  dnsAddress.store(v); dnsReady.store(true); dnsPending.store(false);
}
void dnsBegin(void*) {
  ip_addr_t addr;
  err_t r=dns_gethostbyname(dnsName,&addr,dnsFound,nullptr);
  if (r==ERR_OK) dnsFound(nullptr,&addr,nullptr);
  else if (r!=ERR_INPROGRESS) dnsFound(nullptr,nullptr,nullptr);
}
Error resolve(const char* host, IPAddress& ip) {
  if (ip.fromString(host)) {
    uint32_t v=(uint32_t(ip[0])<<24)|(uint32_t(ip[1])<<16)|(uint32_t(ip[2])<<8)|ip[3];
    return detail::publicIpv4(v) ? Error::None : Error::PublicHttpOnly;
  }
  if (dnsPending.load()) return Error::Dns;
  strcpy(dnsName,host); dnsReady.store(false); dnsAddress.store(0); dnsPending.store(true);
  if (tcpip_callback(dnsBegin,nullptr)!=ERR_OK) { dnsPending.store(false); return Error::Dns; }
  uint32_t began=millis();
  while (!dnsReady.load() && !cancel.load() && millis()-began<4000) vTaskDelay(pdMS_TO_TICKS(10));
  if (cancel.load()) return Error::None;
  if (!dnsReady.load()) return Error::Timeout;
  uint32_t v=dnsAddress.load();
  if (!v) return Error::Dns;
  if (!detail::publicIpv4(v)) return Error::PublicHttpOnly;
  ip=IPAddress(v>>24,(v>>16)&255,(v>>8)&255,v&255); return Error::None;
}
// Buffered socket reader: no readString / unbounded blocking Stream calls.
struct Reader {
  WiFiClient& tcp; uint8_t buf[512]{}; size_t pos=0, count=0;
  uint32_t last=millis();
  void reset() { pos=count=0; last=millis(); }
  int next() {
    for (;;) {
      if (cancel.load()) return -3;
      if (pos<count) return buf[pos++];
      int avail=tcp.available();
      if (avail>0) { int n=tcp.read(buf,avail>int(sizeof(buf))?sizeof(buf):avail); if(n>0) {pos=0;count=n;last=millis();continue;} }
      if (!tcp.connected() && !tcp.available()) return -1;
      if (millis()-last>=kIdleMs) return -2;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
};
Error readHeaders(Reader& r, detail::Headers& h) {
  char line[768]; size_t n=0; bool cr=false; uint32_t start=millis();
  while (!h.done && !cancel.load()) {
    if (millis()-start>kHeaderMs) return Error::Timeout;
    int b=r.next(); if (b<0) return b==-2 ? Error::Timeout : Error::Malformed;
    if (cr) {
      if (b!='\n') return Error::Malformed;
      line[n]=0; Error e=h.line(line); if (e!=Error::None) return e;
      n=0; cr=false;
    } else if (b=='\r') cr=true;
    else { if (b=='\n' || b==0 || n>=sizeof(line)-1) return Error::Malformed; line[n++]=char(b); }
  }
  return Error::None;
}
void setState(State s) { portENTER_CRITICAL(&gate); if (!cancel.load()) current.store(s); portEXIT_CRITICAL(&gate); }
Error sendRequest(WiFiClient& tcp, const char* data, size_t size) {
  size_t sent=0; uint32_t began=millis();
  while(sent<size && !cancel.load()) {
    int n=::send(tcp.fd(),data+sent,size-sent,MSG_DONTWAIT);
    if(n>0) sent+=n;
    else if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) return Error::Connect;
    if(millis()-began>1000) return Error::Timeout;
    if(sent<size) vTaskDelay(pdMS_TO_TICKS(2));
  }
  return Error::None;
}
// Connects to the requested address, following up to three redirects, and leaves r at the
// start of a 200 answer's body. Error::None with cancel set means it was stopped.
Error open(WiFiClient& tcp, Reader& r, detail::Headers& h, Want want) {
  const bool json = want == Want::Json;
  detail::Url url; if (!detail::parseUrl(requested,url)) return Error::InvalidUrl;
  for (unsigned redirect=0;redirect<=3 && !cancel.load();++redirect) {
    setState(State::Connecting);
    IPAddress ip; Error e=resolve(url.host,ip); if(e!=Error::None || cancel.load()) return e;
    // Numeric connect after DNS/IP validation prevents DNS rebinding.
    tcp.setTimeout(1);
    if (!tcp.connect(ip,url.port,600)) return Error::Connect;
    char request[768]; int n=snprintf(request,sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: S1-ADV-Radio/1\r\nAccept: %s\r\nAccept-Encoding: identity\r\n%sConnection: close\r\n\r\n",
      url.path,url.host,url.port,json?"application/json":want==Want::Image?"image/jpeg":"audio/mpeg",want==Want::Audio?"Icy-MetaData: 1\r\n":"");
    if (n<=0 || size_t(n)>=sizeof(request)) return Error::Connect;
    e=sendRequest(tcp,request,n); if(e!=Error::None || cancel.load()) return e;
    r.reset(); h=detail::Headers{}; h.json=json; h.image=want==Want::Image; e=readHeaders(r,h);
    if(want==Want::Image) logoStatus=h.status;
    if(e!=Error::None || cancel.load()) return e;
    if(h.status==200) return Error::None;
    tcp.stop(); if(redirect==3) return Error::Redirect;
    detail::Url next;
    if (h.location[0]=='/' && h.location[1]!='/') {
      char absolute[detail::kUrlSize]; int len=snprintf(absolute,sizeof(absolute),"http://%s:%u%s",url.host,url.port,h.location);
      if(len<=0 || size_t(len)>=sizeof(absolute) || !detail::parseUrl(absolute,next)) return Error::Redirect;
    } else if(!detail::parseUrl(h.location,next)) return Error::Redirect;
    url=next;
  }
  return Error::None;
}
void showTitle(const char* text) {
  portENTER_CRITICAL(&gate);
  const size_t n=strnlen(text,sizeof(nowTitle)-1);
  memcpy(nowTitle,text,n); nowTitle[n]=0;
  portEXIT_CRITICAL(&gate);
}
// Reads the directory's JSON answer at requested into out. With logos set, for a lookup.
Error stations(WiFiClient& tcp, Found* out, size_t room, bool logos, size_t& count) {
  count=0;
  Reader r{tcp}; detail::Headers h;
  Error e=open(tcp,r,h,Want::Json); if(e!=Error::None || cancel.load()) return e;
  setState(State::Buffering);
  detail::Body body(h); detail::StationJson json(out,room,logos);
  uint32_t total=0;
  while (!cancel.load() && !json.finished() && !body.done()) {
    int b=r.next();
    if (b<0) { if(b==-3) return Error::None; if(b==-2) return Error::Timeout; break; }
    int kind=body.feed(uint8_t(b)); if(kind<0) return Error::Malformed;
    if (kind==1 && (++total>kSearchBytes || !json.feed(uint8_t(b)))) return Error::Malformed;
  }
  if (cancel.load()) return Error::None;
  if (!json.finished()) return Error::Malformed;
  count=json.count();
  return Error::None;
}
Error search(WiFiClient& tcp) {
  size_t count=0;
  const Error e=stations(tcp,results,kMaxFound,false,count);
  if(e==Error::None && !cancel.load()) resultCount.store(count);
  return e;
}
// Finds the logo's address if it wasn't given, then has the image service send it as a small
// JPEG. Sets Found or Missing itself; a network error returned is Failed.
Error logo(WiFiClient& tcp) {
  char address[sizeof(Found::logo)];
  strcpy(address,logoGiven);
  if(!*address) {
    constexpr size_t kRoom=10;
    auto* found=static_cast<Found*>(heap_caps_malloc(sizeof(Found)*kRoom,MALLOC_CAP_8BIT));
    if(!found) return Error::NoMemory;
    size_t count=0;
    Error e=stations(tcp,found,4,true,count);  // requested is the lookup by stream address.
    for(size_t i=0;e==Error::None && !*address && i<count;++i) strcpy(address,found[i].logo);
    detail::Url stream;
    if(e==Error::None && !cancel.load() && !*address && detail::parseUrl(logoStream,stream) &&
       detail::logoByNameUrl(requested,sizeof(requested),logoName)) {
      tcp.stop();
      // By name, only a station on the same website: names like Rock FM aren't unique.
      e=stations(tcp,found,kRoom,true,count);
      for(size_t i=0;e==Error::None && !*address && i<count;++i)
        if(detail::sameSite(stream.host,found[i].url)) strcpy(address,found[i].logo);
    }
    free(found);
    if(e!=Error::None || cancel.load()) return e;
    if(!*address || !detail::logoImageUrl(requested,sizeof(requested),address)) { logoOutcome.store(LogoResult::Missing); return Error::None; }
    tcp.stop();
  }
  auto* jpeg=static_cast<uint8_t*>(heap_caps_malloc(kLogoBytes,MALLOC_CAP_8BIT));
  if(!jpeg) return Error::NoMemory;
  Reader r{tcp}; detail::Headers h; logoStatus=0;
  Error e=open(tcp,r,h,Want::Image);
  size_t size=0;
  if(e==Error::None && !cancel.load()) {
    detail::Body body(h);
    while(!cancel.load() && !body.done()) {
      const int b=r.next();
      if(b<0) { if(b==-2) e=Error::Timeout; else if(b!=-3 && !body.complete()) e=Error::Malformed; break; }
      const int kind=body.feed(uint8_t(b)); if(kind<0) { e=Error::Malformed; break; }
      if(kind==1) { if(size>=kLogoBytes) { e=Error::Malformed; break; } jpeg[size++]=uint8_t(b); }
    }
  }
  // The service answers 404 or 400 for a logo it can't fetch, so there's nothing to get.
  if(e==Error::Http && logoStatus>=400 && logoStatus<500) { free(jpeg); logoOutcome.store(LogoResult::Missing); return Error::None; }
  if(e!=Error::None || cancel.load() || size<4) { free(jpeg); return e!=Error::None ? e : cancel.load() ? Error::None : Error::Malformed; }
  // Only what it needs is kept: the service's JPEGs are about 2 KB of the 16 KB allowed.
  if(auto* fit=static_cast<uint8_t*>(realloc(jpeg,size))) jpeg=fit;
  logoData=jpeg; logoLength.store(size); logoOutcome.store(LogoResult::Found);
  return Error::None;
}
Error stream(WiFiClient& tcp, mp3dec_t* decoder, uint8_t* input, int16_t* pcm[2]) {
  {
    Reader r{tcp}; detail::Headers h;
    Error e=open(tcp,r,h,Want::Audio); if(e!=Error::None || cancel.load()) return e;
    kbps.store(h.bitrate);
    detail::Body body(h); size_t used=0; unsigned slot=0, junk=0; uint32_t titles=0;
    bool played=false; uint32_t lastFrame=millis();
    mp3dec_init(decoder); setState(State::Buffering);
    while (!cancel.load()) {
      // Single producer, alternating buffers, no stop markers: M5Unified
      // documents isPlaying(channel)<2 as safe to reuse the older PCM slot.
      // Keep the next frame queued to avoid a gap at every MP3 boundary.
      uint32_t wait=millis();
      while (M5Cardputer.Speaker.isPlaying(kChannel) >= 2 && !cancel.load()) {
        if(millis()-wait>1500) return Error::Speaker;
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      if(cancel.load()) break;
      uint32_t fed=0;
      while (used<kInput && !body.done() && !cancel.load()) {
        if(++fed>128*1024 || millis()-lastFrame>8000) return Error::Decode;
        int b=r.next();
        if (b<0) {
          if (b==-3) return Error::None;
          if(b==-2) return Error::Timeout;
          if(!body.complete()) return Error::Malformed;
          // Decode the remaining bytes before treating EOF as finished.
          break;
        }
        int kind=body.feed(uint8_t(b)); if(kind<0) return Error::Malformed;
        if(kind==1) {input[used++]=b; received.fetch_add(1);}
        // A useful MP3 input window; never download the full stream.
        if (used>=4096) break;
      }
      if(cancel.load()) break;
      if(body.titles()!=titles) { titles=body.titles(); showTitle(body.title()); }
      mp3dec_frame_info_t info{};
      int samples=mp3dec_decode_frame(decoder,input,used,pcm[slot],&info);
      if(info.frame_bytes<0 || size_t(info.frame_bytes)>used) return Error::Decode;
      if(samples>0) {
        if(info.layer!=3 || (info.channels!=1 && info.channels!=2) || info.hz<8000 || info.hz>48000 || samples>1152) return Error::Decode;
        // Every other sample is plenty for a meter, and it's done before the frame is queued.
        uint32_t sum=0; const int count=samples*info.channels;
        for(int i=0;i<count;i+=2){ const int v=pcm[slot][i]; sum+=uint32_t(v<0?-v:v); }
        loudness.store(uint16_t(sum/uint32_t((count+1)/2)));
        if(visualsWanted.load()) {
          Visuals v; detail::analyse(pcm[slot],samples,info.channels,info.hz,v);
          portENTER_CRITICAL(&gate);
          memcpy(shownVisuals.bands,v.bands,sizeof(v.bands)); memcpy(shownVisuals.wave,v.wave,sizeof(v.wave)); ++shownVisuals.frames;
          portEXIT_CRITICAL(&gate);
        }
        if(!M5Cardputer.Speaker.playRaw(pcm[slot],samples*info.channels,info.hz,info.channels==2,1,kChannel,false)) return Error::Speaker;
        slot^=1; played=true; junk=0; lastFrame=millis(); setState(State::Playing);
      }
      size_t consumed=info.frame_bytes;
      if(!consumed && used==kInput) consumed=used-2048;
      if(!samples) junk+=consumed;
      if(consumed) {used-=consumed;memmove(input,input+consumed,used);}
      if(junk>65536 || millis()-lastFrame>8000) return Error::Decode;
      if(body.done() || (!r.tcp.connected() && !r.tcp.available() && r.pos==r.count)) {
        if(samples && used) continue;
        if(!body.complete()) return Error::Malformed;
        return played ? Error::None : Error::Decode;
      }
      vTaskDelay(1);
    }
    return Error::None;
  }
  return Error::None;
}
void worker(void*) {
  Error e=Error::None;
  mp3dec_t* decoder=nullptr; uint8_t* input=nullptr; int16_t* pcm[2]={nullptr,nullptr};
  if (job.load()==Job::Search) { WiFiClient tcp; e=search(tcp); tcp.stop(); }
  else if (job.load()==Job::Logo) {
    WiFiClient tcp; e=logo(tcp); tcp.stop();
    if(!cancel.load() && e!=Error::None && logoOutcome.load()==LogoResult::None) logoOutcome.store(LogoResult::Failed);
  }
  else {
  // All active allocations, TCP and decoder lifetime are worker-owned.
  decoder=static_cast<mp3dec_t*>(heap_caps_malloc(sizeof(mp3dec_t),MALLOC_CAP_8BIT));
  input=static_cast<uint8_t*>(heap_caps_malloc(kInput,MALLOC_CAP_8BIT));
  pcm[0]=static_cast<int16_t*>(heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*sizeof(int16_t),MALLOC_CAP_8BIT));
  pcm[1]=static_cast<int16_t*>(heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*sizeof(int16_t),MALLOC_CAP_8BIT));
  if(!decoder || !input || !pcm[0] || !pcm[1]) e=Error::NoMemory;
  else { WiFiClient tcp; e=stream(tcp,decoder,input,pcm); tcp.stop(); }
  }
  // Cancellation is nonblocking to the caller. Drain a single bounded MP3
  // frames naturally (at most two queued). If the speaker fails, retain buffers
  // and busy rather than free a possibly live pointer; never force-delete.
  current.store(State::Stopping);
  loudness.store(0);
  while(M5Cardputer.Speaker.isPlaying(kChannel)) vTaskDelay(pdMS_TO_TICKS(5));
  free(pcm[1]);free(pcm[0]);free(input);free(decoder);
  portENTER_CRITICAL(&gate);
  if(cancel.load()) e=Error::None;
  failure.store(e);current.store(e==Error::None?State::Idle:State::Error);
  owned.store(false);
  portEXIT_CRITICAL(&gate);
  vTaskDelete(nullptr);
}
void refuse(Error e) {
  portENTER_CRITICAL(&gate);
  if(!owned.load()) { failure.store(e); current.store(State::Error); }
  portEXIT_CRITICAL(&gate);
}
bool begin(const char* url, Job kind) {
  if(ESP.getFreeHeap()<65536 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<24576) { refuse(Error::NoMemory); return false; }
  portENTER_CRITICAL(&gate);
  if(owned.load()) {portEXIT_CRITICAL(&gate);return false;}
  owned.store(true);cancel.store(false);failure.store(Error::None);received.store(0);job.store(kind);
  strcpy(requested,url);current.store(State::Connecting);
  if(kind==Job::Stream) { nowTitle[0]=0; loudness.store(0); kbps.store(0); shownVisuals=Visuals{}; }
  else if(kind==Job::Search) resultCount.store(0);
  else logoOutcome.store(LogoResult::None);
  portEXIT_CRITICAL(&gate);
  if(kind==Job::Search && !results) results=static_cast<Found*>(heap_caps_malloc(sizeof(Found)*kMaxFound,MALLOC_CAP_8BIT));
  // ESP-IDF task stack size is bytes. minimp3 has sizeable decode scratch; a search hasn't.
  if((kind==Job::Search && !results) ||
     xTaskCreate(worker,kind==Job::Stream?"radio_stream":kind==Job::Search?"radio_search":"radio_logo",kind==Job::Stream?24576:12288,nullptr,2,nullptr)!=pdPASS) {
    portENTER_CRITICAL(&gate);failure.store(results||kind==Job::Stream?Error::TaskFailed:Error::NoMemory);current.store(State::Error);owned.store(false);portEXIT_CRITICAL(&gate);
    return false;
  }
  return true;
}
} // anonymous
bool start(const char* url) {
  detail::Url parsed;
  if(!detail::parseUrl(url,parsed)) { refuse(Error::InvalidUrl); return false; }
  return begin(url,Job::Stream);
}
bool startSearch(bool byGenre, const char* text, const char* country) {
  char url[detail::kUrlSize];
  if(!detail::searchUrl(url,sizeof(url),byGenre,text,country)) { refuse(Error::InvalidUrl); return false; }
  return begin(url,Job::Search);
}
bool searching() { return owned.load() && job.load()==Job::Search; }
bool startLogo(const char* stream, const char* name, const char* logo) {
  if(!stream || !name || !logo || strnlen(stream,detail::kUrlSize)>=detail::kUrlSize ||
     strnlen(name,sizeof(logoName))>=sizeof(logoName) || strnlen(logo,sizeof(logoGiven))>=sizeof(logoGiven)) { refuse(Error::InvalidUrl); return false; }
  char first[detail::kUrlSize];
  if(*logo ? !detail::logoImageUrl(first,sizeof(first),logo) : !detail::logoByStreamUrl(first,sizeof(first),stream)) { refuse(Error::InvalidUrl); return false; }
  if(owned.load()) return false;
  clearLogo();  // An answer nobody read goes, so only one JPEG is ever kept.
  strcpy(logoStream,stream); strcpy(logoName,name); strcpy(logoGiven,logo);
  return begin(first,Job::Logo);
}
bool fetchingLogo() { return owned.load() && job.load()==Job::Logo; }
LogoResult logoResult() { return fetchingLogo() ? LogoResult::None : logoOutcome.load(); }
const uint8_t* logoJpeg(size_t& size) {
  if(fetchingLogo() || logoOutcome.load()!=LogoResult::Found) { size=0; return nullptr; }
  size=logoLength.load(); return logoData;
}
void clearLogo() {
  portENTER_CRITICAL(&gate);
  if(owned.load() && job.load()==Job::Logo) { portEXIT_CRITICAL(&gate); return; }
  uint8_t* gone=logoData; logoData=nullptr; logoLength.store(0); logoOutcome.store(LogoResult::None);
  portEXIT_CRITICAL(&gate);
  free(gone);
}
uint16_t level() { return current.load()==State::Playing ? loudness.load() : 0; }
uint16_t bitrate() { return kbps.load(); }
void wantVisuals(bool on) { visualsWanted.store(on); }
bool visuals(Visuals& out) {
  if(current.load()!=State::Playing) return false;
  portENTER_CRITICAL(&gate); out=shownVisuals; portEXIT_CRITICAL(&gate);
  return out.frames>0;
}
size_t foundCount() { return searching() ? 0 : resultCount.load(); }
const Found* found(size_t i) { return results && i<foundCount() ? &results[i] : nullptr; }
void clearFound() {
  portENTER_CRITICAL(&gate);
  if(owned.load() && job.load()==Job::Search) { portEXIT_CRITICAL(&gate); return; }
  Found* gone=results; results=nullptr; resultCount.store(0);
  portEXIT_CRITICAL(&gate);
  free(gone);
}
size_t title(char* out, size_t room) {
  if(!out || !room) return 0;
  portENTER_CRITICAL(&gate);
  size_t n=strnlen(nowTitle,sizeof(nowTitle)-1);
  if(n>room-1) { n=room-1; while(n && (static_cast<unsigned char>(nowTitle[n])&0xC0)==0x80) --n; }
  memcpy(out,nowTitle,n); out[n]=0;
  portEXIT_CRITICAL(&gate);
  return n;
}
void stop() {portENTER_CRITICAL(&gate);if(owned.load()){cancel.store(true);current.store(State::Stopping);}portEXIT_CRITICAL(&gate);}
bool busy(){return owned.load();}
State state(){return current.load();}
Error error(){return failure.load();}
uint32_t bytes(){return received.load();}
const char* status() {
  switch(current.load()) {
    case State::Idle:return "Idle";case State::Connecting:return "Connecting";
    case State::Buffering:return "Buffering";case State::Playing:return "Playing";
    case State::Stopping:return "Stopping";case State::Error:break;
  }
  const char* msg=errorMessage(); return msg ? msg : "Radio error";
}
const char* statusText() { return status(); }
const char* errorMessage() {
  switch(failure.load()) {
    case Error::None:return nullptr;
    case Error::InvalidUrl:return "HTTP MP3 URL required";case Error::PublicHttpOnly:return "Public IPv4 HTTP only";
    case Error::NoMemory:return "Radio memory unavailable";case Error::TaskFailed:return "Radio worker unavailable";
    case Error::Dns:return "Radio DNS failed";case Error::Connect:return "Radio connection failed";
    case Error::Timeout:return "Radio timed out";case Error::Http:return "Radio HTTP error";
    case Error::Unsupported:return "Not an MP3 stream";case Error::Malformed:return "Malformed radio stream";
    case Error::Redirect:return "Radio redirect rejected";case Error::Decode:return "Invalid MP3 stream";
    case Error::Speaker:return "Radio speaker unavailable";default:return "Radio error";
  }
}
} // namespace internet_radio
#endif
