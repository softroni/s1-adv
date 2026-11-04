#!/usr/bin/env python3
"""The radio's station list as the SD card holds it: reading, saving a line, choosing a genre,
leaving out a line, and moving through genres and stations. Real code under ASan/UBSan."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
code = r'''
#include "radio_catalog.h"
#include "radio_countries.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace radio_catalog;
std::string name(const List& l, size_t i) { return l.at(i).name; }
std::string skipLine(const std::string& text, uint16_t line) {
  LineSkipper s; s.skip = line; std::string out;
  for (unsigned char c : text) if (s.keep(c)) out += char(c);
  return out;
}
int main() {
  // The built-in list is a file like any other, and reads back as the 18 stations it always was.
  List l;
  assert(l.load(kStarter, sizeof(kStarter) - 1));
  assert(l.count() == 18 && l.genres() == 7 && !l.skipped() && l.endsWithNewline() && l.size() == sizeof(kStarter) - 1);
  const char* genres[] = {"Jazz", "Funk / Soul", "Rock", "Electro / House", "Chill", "Lounge", "Russian mix"};
  for (size_t g = 0; g < 7; ++g) assert(std::string(l.genre(g)) == genres[g]);
  assert(name(l, 0) == "Sonic Universe" && l.at(0).line == 5 && std::string(l.at(0).country) == "US");
  assert(name(l, 1) == "Instrumental Jazz" && std::string(l.at(1).country) == "RU" && std::string(l.at(2).country) == "IT");
  for (size_t i = 0; i < l.count(); ++i) assert(strlen(l.at(i).country) == 2);  // Every built-in one has its country.
  for (size_t i = 0; i < l.count(); ++i) {
    internet_radio::detail::Url u;
    assert(internet_radio::detail::parseUrl(l.at(i).url, u));
  }
  assert(l.find("http://ice2.somafm.com/groovesalad-128-mp3") == 11 && l.find("http://nowhere.example/") < 0);
  assert(l.findGenre("rock") == 2 && l.findGenre("ROCK") == 2 && l.findGenre("Roc") < 0);

  // What an owner might type: notes, blank lines, CRLF, spaces, a lowercase ru, no last newline.
  // Bad lines are counted, never guessed at: too few or too many fields, a bad or https address.
  const std::string typed =
    "# my stations\r\n\r\n"
    "  Talk |  Radio 4  | http://radio.example/r4  \r\n"
    "Talk|Spare|http://radio.example/spare|ru\n"
    "Talk | No address |\n"
    "Talk | Secure | https://radio.example/x\n"
    "Talk | Too | http://radio.example/y | RU | extra\n"
    "talk | Same genre | http://radio.example/z\n"
    "| No genre | http://radio.example/w\n"
    "Музыка | Радио | http://radio.example/ru";
  assert(l.load(typed.data(), typed.size()));
  assert(l.count() == 4 && l.genres() == 2 && l.skipped() == 4 && !l.endsWithNewline());
  assert(name(l, 0) == "Radio 4" && std::string(l.at(0).url) == "http://radio.example/r4" && l.at(0).line == 3);
  assert(std::string(l.at(1).country) == "RU" && l.at(1).line == 4 && !l.at(0).country[0]);
  assert(l.at(2).genre == 0 && std::string(l.genre(0)) == "Talk");  // "talk" joins "Talk".
  assert(std::string(l.genre(1)) == "Музыка" && name(l, 3) == "Радио" && l.at(3).line == 10);

  // Long names are cut at a whole letter, never inside a Cyrillic one.
  std::string longName;
  for (int i = 0; i < 40; ++i) longName += "Ж";
  const std::string longLine = "Long | " + longName + " | http://radio.example/long\n";
  assert(l.load(longLine.data(), longLine.size()) && l.count() == 1);
  assert(strlen(l.at(0).name) <= kNameBytes && strlen(l.at(0).name) % 2 == 0);
  // The limits hold: 100 stations, 24 genres, 16 KB. The rest are counted as skipped.
  std::string many;
  for (int i = 0; i < 130; ++i) many += "G" + std::to_string(i % 30) + " | S" + std::to_string(i) + " | http://radio.example/" + std::to_string(i) + "\n";
  assert(l.load(many.data(), many.size()));
  assert(l.count() <= kMaxStations && l.genres() == kMaxGenres && l.count() + l.skipped() == 130);
  assert(l.full() || l.count() < kMaxStations);
  assert(!l.buffer(kMaxFileBytes + 1) && l.count() == 0);
  assert(l.load("", 0) && l.count() == 0 && l.genres() == 0 && l.endsWithNewline());

  // A saved station becomes one line that reads back as itself. A name can't add a field, a
  // line, or turn the line into a note.
  char line[700];
  size_t n = formatLine(line, sizeof(line), "Jazz", "Smooth | Jazz\nFM", "http://radio.example/sj", "");
  assert(n && std::string(line) == "Jazz | Smooth   Jazz FM | http://radio.example/sj\n" && n == strlen(line));
  n = formatLine(line, sizeof(line), "# Talk", "Радио", "http://radio.example/ru", "ru");
  assert(n && std::string(line) == "Talk | Радио | http://radio.example/ru | RU\n");
  n = formatLine(line, sizeof(line), "Talk", "BBC", "http://bbc.example/", "GBR");  // Not a code: left out.
  assert(n && std::string(line) == "Talk | BBC | http://bbc.example/\n");
  assert(!formatLine(line, sizeof(line), "Jazz", "  ", "http://radio.example/sj", ""));
  assert(!formatLine(line, sizeof(line), "Jazz", "Name", "https://radio.example/sj", ""));
  assert(!formatLine(line, 20, "Jazz", "Name", "http://radio.example/sj", ""));
  {
    List a; assert(a.load(kStarter, sizeof(kStarter) - 1));
    std::string file(kStarter);
    n = formatLine(line, sizeof(line), "Jazz", "Smooth | Jazz", "http://radio.example/sj", "US");
    file += line;
    List b; assert(b.load(file.data(), file.size()));
    assert(b.count() == 19 && b.genres() == 7 && name(b, 18) == "Smooth   Jazz" && b.at(18).genre == 0);
  }

  // Where a found station goes. By genre: the list's own spelling, either half of a genre like
  // "Funk / Soul", or else the words typed. By name: the first genre a tag names, or "Saved".
  assert(l.load(kStarter, sizeof(kStarter) - 1));
  char g[32];
  genreFor(l, true, "jazz", "", g, sizeof(g)); assert(std::string(g) == "Jazz");
  genreFor(l, true, "soul", "", g, sizeof(g)); assert(std::string(g) == "Funk / Soul");
  genreFor(l, true, "house", "", g, sizeof(g)); assert(std::string(g) == "Electro / House");
  genreFor(l, true, "80s", "", g, sizeof(g)); assert(std::string(g) == "80s");
  genreFor(l, true, "news talk", "", g, sizeof(g)); assert(std::string(g) == "News talk");
  genreFor(l, false, "BBC", "easy listening,smooth jazz", g, sizeof(g)); assert(std::string(g) == "Jazz");
  genreFor(l, false, "BBC", "news,talk", g, sizeof(g)); assert(std::string(g) == "Other");
  genreFor(l, false, "BBC", "", g, sizeof(g)); assert(std::string(g) == "Other");
  genreFor(l, true, "a really long genre name that goes on", "", g, sizeof(g)); assert(strlen(g) <= kGenreBytes);
  genreFor(l, true, "#|", "", g, sizeof(g)); assert(std::string(g) == "Other");

  // Removing a line keeps every other byte, notes and spacing included.
  const std::string file = "# note\nA | One | http://a.example/1\nA | Two | http://a.example/2\n";
  assert(skipLine(file, 2) == "# note\nA | Two | http://a.example/2\n");
  assert(skipLine(file, 3) == "# note\nA | One | http://a.example/1\n");
  assert(skipLine(file, 9) == file);
  assert(skipLine("a\nb", 2) == "a\n");

  // Moving through the list never lands on nothing, for every filter, and wraps both ways.
  // A list made before stations had a country: the built-in ones get theirs from the built-in
  // list, matched on the whole address, and a station that isn't built in stays without one.
  {
    const std::string old = "Chill | Groove Salad | http://ice2.somafm.com/groovesalad-128-mp3\n"
      "Rock | Nashe Radio | http://nashe1.hostingradio.ru/nashe-128.mp3 | RU\n"
      "Chill | Groove | http://ice2.somafm.com/groovesalad-128\n"
      "Talk | Mine | http://mine.example/\n"
      "Talk | Odd | http://odd.example/ | International\n";
    List o; assert(o.load(old.data(), old.size()) && o.count() == 5);
    assert(std::string(o.at(0).country) == "US" && std::string(o.at(1).country) == "RU");
    assert(!o.at(2).country[0] && !o.at(3).country[0] && !o.at(4).country[0]);
  }
  char code[3];
  assert(countryCode("gb", code) && std::string(code) == "GB" && !countryCode("G", code) && !countryCode("GBR", code) && !countryCode("1A", code) && !countryCode(nullptr, code));
  for (const char* country : {"", "IT", "RU", "US"}) {
    Selection s; s.list = &l; memcpy(s.country, country, strlen(country) + 1); if (!s.count(s.genre)) s.moveGenre(1);
    for (int i = 0; i < 1000; ++i) {
      assert(s.genre < l.genres() && s.count(s.genre) && s.station() && s.matches(*s.station(), s.genre));
      s.moveStation(i % 2 ? 1 : -1); assert(s.station());
      s.moveGenre(i % 3 ? 1 : -1); assert(s.station());
    }
  }
  // Pointing at a station shows it even when the filter hid it.
  // L: all, then each country the list has, in order, then all again.
  {
    Selection c; c.list = &l; std::vector<std::string> seen;
    for (int i = 0; i < 5; ++i) { c.nextCountry(); seen.push_back(c.country); assert(c.station() || !c.country[0]); }
    assert((seen == std::vector<std::string>{"IT", "RU", "US", "", "IT"}));
    List none; const std::string bare = "Talk | Mine | http://mine.example/\n"; assert(none.load(bare.data(), bare.size()));
    Selection d; d.list = &none; d.nextCountry(); assert(!d.country[0] && d.station());
  }
  Selection s; s.list = &l; memcpy(s.country, "RU", 3);
  assert(s.select(11) && !s.country[0] && s.stationIndex() == 11 && std::string(s.genreName()) == "Chill");
  assert(!s.select(-1) && !s.select(99));
  // After a reload: the same genre and place where it can, something valid where it can't.
  List shorter; const std::string two = "Chill | Only | http://a.example/1\n";
  assert(shorter.load(two.data(), two.size()));
  s.list = &shorter; s.genre = 4; s.index = 2; s.fit();
  assert(s.genre == 0 && s.index == 0 && s.station() && name(shorter, 0) == "Only");
  memcpy(s.country, "RU", 3); s.fit(); assert(!s.country[0] && s.station());
  List empty; s.list = &empty; s.fit(); assert(!s.station() && !s.count(0));
  s.moveGenre(1); s.moveStation(1); s.nextCountry(); assert(!s.station());
  Selection none; assert(!none.station() && !none.count(0) && std::string(none.genreName()).empty());
  // Countries: sorted and unique for the halving search, two capitals each, ASCII names.
  for (size_t i = 0; i < radio_countries::kCountryCount; ++i) {
    const auto& k = radio_countries::kCountries[i];
    assert(strlen(k.code) == 2 && k.code[0] >= 'A' && k.code[0] <= 'Z' && k.code[1] >= 'A' && k.code[1] <= 'Z');
    assert(i == 0 || strcmp(radio_countries::kCountries[i - 1].code, k.code) < 0);
    assert(*k.name && strlen(k.name) <= 31);
    for (const char* q = k.name; *q; ++q) assert(*q >= ' ' && *q < 127);
    assert(radio_countries::name(k.code) == k.name);
  }
  assert(radio_countries::kCountryCount > 240 && std::string(radio_countries::name("GB")) == "United Kingdom");
  assert(std::string(radio_countries::name("US")) == "United States" && std::string(radio_countries::name("RU")) == "Russia");
  for (const char* bad : {"gb", "ZZ", "UK", "G", "GBR", ""}) assert(!radio_countries::name(bad));
  assert(!radio_countries::name(nullptr) && std::string(radio_countries::alias("UK")) == "GB" && std::string(radio_countries::alias("US")) == "US");
  puts("PASS radio station list: the built-in file, typed files, limits, safe saved lines, genre choice, line removal, selection and the country table");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-radio-catalog-') as d:
    p = Path(d); (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-DINTERNET_RADIO_HOST_TEST',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(src), str(p / 'test.cpp'),
                    str(src / 'internet_radio_transport.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, timeout=60)
