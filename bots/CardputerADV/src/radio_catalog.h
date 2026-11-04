#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include "internet_radio_transport.h"

// The station list. It lives on the SD card as /radio/stations.txt, one station per line:
//   Genre | Name | http://address | RU
// The last field is optional: RU marks a Russian-language station for the L filter, and
// anything else counts as EN/INT. Lines starting with # are notes. Genres show in the order
// they first appear. With no card the built-in list below is used, and nothing can be saved.
namespace radio_catalog {
enum class Language : uint8_t { All, International, Russian };
constexpr size_t kMaxStations = 100;
constexpr size_t kMaxGenres = 24;
constexpr size_t kMaxFileBytes = 16384;
constexpr size_t kNameBytes = 47, kGenreBytes = 23;  // UTF-8 bytes, cut at a whole letter

// What a new card gets, in the file's own format. Checked 2026-10-22 and again 2026-11-03.
constexpr char kStarter[] =
  "# S1-ADV Internet Radio stations, one per line: Genre | Name | http:// address | RU\n"
  "# RU marks a Russian-language station for the L filter. Lines starting with # are notes.\n"
  "# Only plain http:// MP3 streams play. Find on the Cardputer adds stations here.\n"
  "Jazz | Sonic Universe | http://ice2.somafm.com/sonicuniverse-128-mp3\n"
  "Jazz | Instrumental Jazz | http://jfm1.hostingradio.ru:14536/ijstream.mp3 | RU\n"
  "Funk / Soul | Funky Radio | http://funkyradio.streamingmedia.it/play.mp3\n"
  "Funk / Soul | Seven Inch Soul | http://ice2.somafm.com/7soul-128-mp3\n"
  "Rock | Radio Paradise | http://stream.radioparadise.com/mp3-128\n"
  "Rock | Indie Pop Rocks | http://ice2.somafm.com/indiepop-128-mp3\n"
  "Rock | Nashe Radio | http://nashe1.hostingradio.ru/nashe-128.mp3 | RU\n"
  "Rock | Rock FM | http://nashe1.hostingradio.ru/rock-128.mp3 | RU\n"
  "Electro / House | Beat Blender | http://ice2.somafm.com/beatblender-128-mp3\n"
  "Electro / House | DEF CON | http://ice2.somafm.com/defcon-128-mp3\n"
  "Electro / House | cliqhop IDM | http://ice2.somafm.com/cliqhop-128-mp3\n"
  "Chill | Groove Salad | http://ice2.somafm.com/groovesalad-128-mp3\n"
  "Chill | Lush | http://ice2.somafm.com/lush-128-mp3\n"
  "Chill | 101.ru Chill Out | http://pub0201.101.ru/stream/trust/mp3/128/24 | RU\n"
  "Lounge | Secret Agent | http://ice2.somafm.com/secretagent-128-mp3\n"
  "Lounge | Illinois Lounge | http://ice2.somafm.com/illstreet-128-mp3\n"
  "Russian mix | Russkie Pesni | http://listen.rusongs.ru/ru-mp3-128 | RU\n"
  "Russian mix | Nashe Radio | http://nashe1.hostingradio.ru/nashe-128.mp3 | RU\n";

inline const char* languageName(Language language) {
  return language == Language::All ? "ALL" : language == Language::Russian ? "RU" : "EN/INT";
}

// ASCII letters only: enough for genre words, and never splits a UTF-8 letter.
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
inline bool sameText(const char* a, const char* b) {
  for (; *a && *b; ++a, ++b) if (lower(*a) != lower(*b)) return false;
  return !*a && !*b;
}
// Whether `text` holds `part` anywhere, ignoring ASCII case. Bounded by both lengths.
inline bool holds(const char* text, size_t textLen, const char* part, size_t partLen) {
  if (!partLen || partLen > textLen) return false;
  for (size_t i = 0; i + partLen <= textLen; ++i) {
    size_t j = 0;
    while (j < partLen && lower(text[i + j]) == lower(part[j])) ++j;
    if (j == partLen) return true;
  }
  return false;
}
// The longest start of s, at most `room` bytes, that doesn't end inside a UTF-8 letter.
inline size_t utf8Cut(const char* s, size_t len, size_t room) {
  if (len <= room) return len;
  size_t n = room;
  while (n && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
  return n;
}

struct Station {
  const char* name = "";
  const char* url = "";
  uint8_t genre = 0;
  Language language = Language::International;
  uint16_t line = 0;  // Line number in the file, from 1, so a removal can skip exactly it.
};

class List {
 public:
  List() = default;
  ~List() { delete[] text_; }
  List(const List&) = delete;
  List& operator=(const List&) = delete;

  // Room for `size` bytes of file text, which the caller fills and then hands to parse().
  // Replaces whatever was loaded. nullptr when there's no memory for it, or it's too big.
  char* buffer(size_t size) {
    clear();
    if (size > kMaxFileBytes) return nullptr;
    text_ = new (std::nothrow) char[size + 1];
    if (text_) text_[size] = 0;
    return text_;
  }
  void parse(size_t size) {
    if (!text_) return;
    size_ = size;
    endsWithNewline_ = !size || text_[size - 1] == '\n';
    char* p = text_;
    char* const end = text_ + size;
    uint16_t number = 0;
    while (p < end) {
      char* eol = p;
      while (eol < end && *eol != '\n') ++eol;
      *eol = 0;  // The last line ends at the terminator buffer() left past the text.
      ++number;
      line(p, eol, number);
      p = eol + 1;
    }
  }
  bool load(const char* text, size_t size) {
    char* to = buffer(size);
    if (!to) return false;
    memcpy(to, text, size);
    parse(size);
    return true;
  }
  void clear() {
    delete[] text_;
    text_ = nullptr;
    size_ = 0;
    count_ = genreCount_ = 0;
    skipped_ = 0;
    endsWithNewline_ = true;
  }

  size_t count() const { return count_; }
  size_t size() const { return size_; }  // Bytes in the file, so a save can't outgrow kMaxFileBytes.
  const Station& at(size_t i) const { return stations_[i]; }
  size_t genres() const { return genreCount_; }
  const char* genre(size_t i) const { return i < genreCount_ ? genres_[i] : ""; }
  uint16_t skipped() const { return skipped_; }
  bool full() const { return count_ >= kMaxStations; }
  // Whether a line appended to the file starts on a line of its own.
  bool endsWithNewline() const { return endsWithNewline_; }
  int find(const char* url) const {
    for (size_t i = 0; i < count_; ++i) if (!strcmp(stations_[i].url, url)) return int(i);
    return -1;
  }
  int findGenre(const char* name) const {
    for (size_t i = 0; i < genreCount_; ++i) if (sameText(genres_[i], name)) return int(i);
    return -1;
  }

 private:
  static char* trim(char* from, char* to) {
    while (from < to && (*from == ' ' || *from == '\t')) ++from;
    while (to > from && (to[-1] == ' ' || to[-1] == '\t' || to[-1] == '\r')) --to;
    *to = 0;
    return from;
  }
  void line(char* p, char* eol, uint16_t number) {
    char* text = trim(p, eol);
    if (!*text || *text == '#') return;
    char* field[4] = {};
    size_t fields = 0;
    for (char* q = text;;) {
      char* bar = q;
      while (*bar && *bar != '|') ++bar;
      const bool last = !*bar;
      if (fields == 4) { ++skipped_; return; }
      field[fields++] = trim(q, bar);
      if (last) break;
      q = bar + 1;
    }
    internet_radio::detail::Url url;
    if (fields < 3 || !*field[0] || !*field[1] || !internet_radio::detail::parseUrl(field[2], url) || full()) {
      ++skipped_;
      return;
    }
    field[0][utf8Cut(field[0], strlen(field[0]), kGenreBytes)] = 0;
    field[1][utf8Cut(field[1], strlen(field[1]), kNameBytes)] = 0;
    int genre = findGenre(field[0]);
    if (genre < 0) {
      if (genreCount_ >= kMaxGenres) { ++skipped_; return; }
      genres_[genreCount_] = field[0];
      genre = genreCount_++;
    }
    Station& s = stations_[count_++];
    s.name = field[1];
    s.url = field[2];
    s.genre = uint8_t(genre);
    s.language = fields == 4 && sameText(field[3], "RU") ? Language::Russian : Language::International;
    s.line = number;
  }

  char* text_ = nullptr;
  size_t size_ = 0;
  Station stations_[kMaxStations];
  const char* genres_[kMaxGenres] = {};
  uint8_t count_ = 0, genreCount_ = 0;
  uint16_t skipped_ = 0;
  bool endsWithNewline_ = true;
};

// One file line for a station, ending in a newline. A '|' or control character in a name
// becomes a space, so a name can't add fields or lines. 0 when it doesn't fit or isn't valid.
inline size_t formatLine(char* out, size_t room, const char* genre, const char* name, const char* url, Language language) {
  internet_radio::detail::Url parsed;
  if (!out || !room || !internet_radio::detail::parseUrl(url, parsed)) return 0;
  size_t n = 0;
  bool ok = true;
  const auto put = [&](char c) { if (n + 1 < room) out[n++] = c; else ok = false; };
  const auto text = [&](const char* s, size_t limit) {
    const size_t len = utf8Cut(s, strlen(s), limit);
    size_t start = 0, stop = len;
    while (start < stop && (static_cast<unsigned char>(s[start]) <= ' ' || s[start] == '#' || s[start] == '|')) ++start;
    while (stop > start && static_cast<unsigned char>(s[stop - 1]) <= ' ') --stop;
    if (start == stop) ok = false;
    for (size_t i = start; i < stop; ++i) {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      put(c < ' ' || c == 127 || c == '|' ? ' ' : char(c));
    }
  };
  text(genre, kGenreBytes);
  for (const char* s = " | "; *s; ++s) put(*s);
  text(name, kNameBytes);
  for (const char* s = " | "; *s; ++s) put(*s);
  for (const char* s = url; *s; ++s) put(*s);
  if (language == Language::Russian) for (const char* s = " | RU"; *s; ++s) put(*s);
  put('\n');
  if (!ok) return 0;
  out[n] = 0;
  return n;
}

// The genre a station found by searching goes under. A search by genre uses the list's own
// genre if the words name one, or else the words typed. A search by name uses the first of
// the list's genres that one of the station's tags names, or "Other". A genre like
// "Funk / Soul" matches on either half. `words` is comma-separated: tags, or what was typed.
inline int matchGenre(const List& list, const char* words) {
  for (size_t g = 0; g < list.genres(); ++g) {
    const char* genre = list.genre(g);
    for (const char* part = genre; *part;) {
      const char* stop = part;
      while (*stop && *stop != '/') ++stop;
      const char* a = part;
      const char* b = stop;
      while (a < b && *a == ' ') ++a;
      while (b > a && b[-1] == ' ') --b;
      for (const char* word = words; *word;) {
        const char* comma = word;
        while (*comma && *comma != ',') ++comma;
        if (holds(word, size_t(comma - word), a, size_t(b - a))) return int(g);
        word = *comma ? comma + 1 : comma;
      }
      part = *stop ? stop + 1 : stop;
    }
  }
  return -1;
}
inline void genreFor(const List& list, bool byGenre, const char* typed, const char* tags, char* out, size_t room) {
  if (!room) return;
  const int match = matchGenre(list, byGenre ? typed : tags);
  const char* from = match >= 0 ? list.genre(size_t(match)) : byGenre ? typed : "Other";
  while (*from == ' ' || *from == '#' || *from == '|') ++from;
  size_t n = utf8Cut(from, strlen(from), room - 1 < kGenreBytes ? room - 1 : kGenreBytes);
  while (n && from[n - 1] == ' ') --n;
  memcpy(out, from, n);
  out[n] = 0;
  if (!n) snprintf(out, room, "%s", "Other");
  else if (match < 0 && out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - 'a' + 'A');
}

// Copies a file a byte at a time, leaving out one line, so a removal keeps every note and
// station the owner typed in exactly as it was. Lines count from 1, as List does.
struct LineSkipper {
  uint16_t skip = 0, line = 1;
  bool keep(uint8_t c) {
    const bool kept = line != skip;
    if (c == '\n') ++line;
    return kept;
  }
};

struct Selection {
  const List* list = nullptr;
  uint8_t genre = 0;
  uint8_t index = 0;
  Language language = Language::All;
  bool matches(const Station& station, uint8_t group) const {
    return station.genre == group && (language == Language::All || language == station.language);
  }
  uint8_t count(uint8_t group) const {
    uint8_t n = 0;
    if (list) for (size_t i = 0; i < list->count(); ++i) if (matches(list->at(i), group)) ++n;
    return n;
  }
  int stationIndex() const {
    if (!list) return -1;
    uint8_t n = 0;
    for (size_t i = 0; i < list->count(); ++i) if (matches(list->at(i), genre) && n++ == index) return int(i);
    return -1;
  }
  const Station* station() const {
    const int i = stationIndex();
    return i < 0 ? nullptr : &list->at(size_t(i));
  }
  const char* genreName() const { return list && genre < list->genres() ? list->genre(genre) : ""; }
  void moveGenre(int delta) {
    const size_t genres = list ? list->genres() : 0;
    index = 0;
    for (size_t i = 0; i < genres; ++i) {
      genre = static_cast<uint8_t>((int(genre) + int(genres) + (delta < 0 ? -1 : 1)) % int(genres));
      if (count(genre)) return;
    }
  }
  void moveStation(int delta) {
    const int n = count(genre);
    index = n ? static_cast<uint8_t>((int(index) + n + (delta < 0 ? -1 : 1)) % n) : 0;
  }
  void nextLanguage() {
    language = static_cast<Language>((static_cast<unsigned>(language) + 1) % 3);
    index = 0;
    if (!count(genre)) moveGenre(1);
  }
  // Points at the list's station i, showing every language if the filter would hide it.
  bool select(int i) {
    if (!list || i < 0 || size_t(i) >= list->count()) return false;
    const Station& s = list->at(size_t(i));
    if (language != Language::All && language != s.language) language = Language::All;
    genre = s.genre;
    index = 0;
    for (size_t j = 0; j < size_t(i); ++j) if (matches(list->at(j), genre)) ++index;
    return true;
  }
  // After the list changes: stays on the same genre and place where it still can.
  void fit() {
    const size_t genres = list ? list->genres() : 0;
    if (!genres) { genre = index = 0; return; }
    if (genre >= genres) genre = uint8_t(genres - 1);
    if (!count(genre)) moveGenre(1);
    if (!count(genre) && language != Language::All) {
      language = Language::All;
      if (!count(genre)) moveGenre(1);
    }
    const uint8_t n = count(genre);
    if (index >= n) index = n ? uint8_t(n - 1) : 0;
  }
};
}  // namespace radio_catalog
