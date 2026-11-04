#!/usr/bin/env python3
"""Podcasts: the bridge's lines as the device reads them, the card's three files, the bridge
addresses, the place of a second in a saved file, and the times shown. Real code under
ASan/UBSan. The sample lines are what bridge/src/podcasts.mjs writes."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
bridge = (root / 'bridge/src/podcasts.mjs').read_text()
# The two ends agree on the limits and the audio format.
assert 'LIMITS = Object.freeze({ id: 100, title: 96, by: 48 })' in bridge
assert 'AUDIO = Object.freeze({ rate: 32000, kbps: 48, bytesPerSecond: 6000 })' in bridge
assert 'export const PAGE = 20;' in bridge
code = r'''
#include "podcast_catalog.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace podcast;
std::string S(const char* s) { return s; }
int main() {
  assert(kIdBytes == 100 && kTitleBytes == 96 && kByBytes == 48 && kPage == 20);
  assert(kBytesPerSecond == 6000 && kFrameBytes * kRate == kBytesPerSecond * kFrameSamples);  // 216 bytes every 36 ms.

  // Ids are safe in a path and a URL as they are.
  for (const char* good : {"p1537788786", "aadventures_holmes", "athe-shadow-1938-10-09-141-death-stalks-the-shadow", "aRko.Welles"}) assert(showIdValid(good));
  for (const char* bad : {"", "p", "a", "x123", "p12a", "a../etc", "a-x", "a.x", "aa/b", "aa b", "aa%2F", "pp"}) assert(!showIdValid(bad));
  std::string longest = "a" + std::string(99, 'x');
  assert(showIdValid(longest.c_str()) && !showIdValid((longest + "x").c_str()));
  assert(episodeIdValid("618afa868312") && !episodeIdValid("618afa86831") && !episodeIdValid("618afa8683123") && !episodeIdValid("618AFA868312") && !episodeIdValid("../../../etc"));

  // A search's lines. Bad ones are skipped, and text keeps whole letters.
  const char found[] =
    "show\tp1537788786\t1007\tThe Rest Is History\tGoalhanger\n"
    "show\taadventures_holmes\t0\tThe Adventures of Sherlock Holmes\tSir Arthur Conan Doyle\n"
    "show\tbad id\t3\tX\tY\n"
    "show\tp12\tlots\tX\tY\n"
    "show\tp13\t3\t\tY\n"
    "noise\r\n"
    "show\tp14\t2\tПривет мир\t\r\n"
    "show\tp15\t70000\tNo newline at the end\tZ";
  Show shows[8];
  size_t n = parseShows(found, sizeof(found) - 1, shows, 8);
  assert(n == 4);
  assert(S(shows[0].id) == "p1537788786" && shows[0].count == 1007 && S(shows[0].title) == "The Rest Is History" && S(shows[0].by) == "Goalhanger");
  assert(S(shows[1].id) == "aadventures_holmes" && shows[1].count == 0 && S(shows[1].by) == "Sir Arthur Conan Doyle");
  assert(S(shows[2].title) == "Привет мир" && S(shows[2].by).empty());
  assert(shows[3].count == 65535 && S(shows[3].title) == "No newline at the end");
  assert(parseShows(found, sizeof(found) - 1, shows, 2) == 2);
  // Twenty fit, and the twenty-first doesn't overrun.
  std::string many;
  for (int i = 0; i < 25; ++i) many += "show\tp" + std::to_string(100 + i) + "\t1\tShow " + std::to_string(i) + "\tby\n";
  Show twenty[kMaxShows];
  assert(parseShows(many.data(), many.size(), twenty, kMaxShows) == kMaxShows && S(twenty[19].title) == "Show 19");
  // A title longer than the device keeps is cut at a whole letter.
  std::string longTitle = "show\tp9\t1\t" + std::string(95, 'a') + "Ж\tby\n";
  assert(parseShows(longTitle.data(), longTitle.size(), shows, 1) == 1 && S(shows[0].title) == std::string(95, 'a'));
  std::string huge = "show\tp9\t1\t" + std::string(600, 'a') + "\tby\nshow\tp10\t1\tShort\tby\n";
  assert(parseShows(huge.data(), huge.size(), shows, 2) == 1 && S(shows[0].id) == "p10");  // A line past 511 bytes goes.

  // A page of episodes.
  const char page[] =
    "about\t737\t20\tThe Rest Is History\tGoalhanger\n"
    "ep\t618afa868312\t5131\t2026-11-02\tRevolution in the Soviet Empire\n"
    "ep\tff40407aea90\t0\t-\t\n"
    "ep\tnothex000000\t10\t-\tBad id\n"
    "ep\tcbd77d25613c\tx\t-\tBad length\n"
    "ep\t7744fb23413c\t2719\t-\t03 - A Case of Identity\n";
  Page p;
  assert(parsePage(page, sizeof(page) - 1, p));
  assert(p.total == 737 && p.from == 20 && S(p.title) == "The Rest Is History" && S(p.by) == "Goalhanger" && p.count == 3);
  assert(S(p.episodes[0].id) == "618afa868312" && p.episodes[0].seconds == 5131 && S(p.episodes[0].date) == "2026-11-02");
  assert(S(p.episodes[1].title) == "Untitled" && S(p.episodes[1].date).empty() && p.episodes[1].seconds == 0);
  assert(S(p.episodes[2].title) == "03 - A Case of Identity");
  assert(!parsePage("ep\t618afa868312\t1\t-\tNo about line\n", 33, p));
  std::string full = "about\t40\t0\tT\t\n";
  for (int i = 0; i < 25; ++i) full += "ep\t" + std::string(11, 'a') + char('a' + i % 6) + "\t60\t-\tE\n";
  assert(parsePage(full.data(), full.size(), p) && p.count == kPage);

  // The card's files: what's written reads back, and tabs or breaks in text can't split a record.
  Show s;
  strcpy(s.id, "p42"); s.count = 7; strcpy(s.title, "Tab\there"); strcpy(s.by, "Line\nbreak");
  char line[400];
  size_t len = formatShow(line, sizeof(line), s);
  assert(len && S(line) == "p42\t7\tTab here\tLine break\n");
  line[len - 1] = 0;
  Show back;
  assert(parseShowLine(line, back) && S(back.id) == "p42" && back.count == 7 && S(back.title) == "Tab here" && S(back.by) == "Line break");
  char badShow[] = "../x\t1\tT\tB";
  assert(!parseShowLine(badShow, back));

  Entry e;
  strcpy(e.show, "aadventures_holmes"); strcpy(e.episode, "1e2ecb91af8f"); strcpy(e.showTitle, "The Adventures of Sherlock Holmes");
  strcpy(e.title, "01 - A Scandal in Bohemia"); e.at = 1234; e.seconds = 3906; e.stamp = 1793750000; e.bytes = 23440104;
  len = formatProgress(line, sizeof(line), e);
  assert(len && S(line) == "aadventures_holmes\t1e2ecb91af8f\t1234\t3906\t1793750000\tThe Adventures of Sherlock Holmes\t01 - A Scandal in Bohemia\n");
  line[len - 1] = 0;
  Entry r;
  assert(parseProgressLine(line, r) && S(r.show) == e.show && S(r.episode) == e.episode && r.at == 1234 && r.seconds == 3906 &&
         r.stamp == 1793750000 && S(r.showTitle) == e.showTitle && S(r.title) == e.title);
  len = formatSaved(line, sizeof(line), e);
  assert(len && S(line) == "aadventures_holmes\t1e2ecb91af8f\t3906\t23440104\tThe Adventures of Sherlock Holmes\t01 - A Scandal in Bohemia\n");
  line[len - 1] = 0;
  assert(parseSavedLine(line, r) && r.bytes == 23440104 && r.seconds == 3906 && r.at == 0);
  char badProgress[] = "p1\tnothex\t1\t2\t3\tT\tE";
  assert(!parseProgressLine(badProgress, r));
  char shortProgress[] = "p1\t1e2ecb91af8f\t1\t2\t3\tT";
  assert(!parseProgressLine(shortProgress, r));
  char tooBig[] = "p1\t1e2ecb91af8f\t99999999999\t2\t3\tT\tE";
  assert(!parseProgressLine(tooBig, r));
  // A record that won't fit in its buffer isn't written half-way.
  char tiny[20];
  assert(!formatProgress(tiny, sizeof(tiny), e));

  // What counts as heard: within 45 seconds of the end, never when the length isn't known.
  assert(finished(3861, 3906) && finished(3906, 3906) && !finished(3860, 3906) && !finished(99999, 0));

  // Addresses on the bridge.
  char url[300];
  assert(searchPath(url, sizeof(url), Kind::Podcast, "  rest is history ") && S(url) == "/api/cardputer/podcasts/search?kind=podcast&q=rest%20is%20history");
  assert(searchPath(url, sizeof(url), Kind::Book, "Doyle & co?") && S(url) == "/api/cardputer/podcasts/search?kind=book&q=Doyle%20%26%20co%3F");
  assert(searchPath(url, sizeof(url), Kind::Radio, "Привет") && S(url) == "/api/cardputer/podcasts/search?kind=radio&q=%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82");
  assert(!searchPath(url, sizeof(url), Kind::Podcast, "   ") && !searchPath(url, sizeof(url), Kind::Podcast, std::string(65, 'x').c_str()));
  assert(!searchPath(url, 40, Kind::Podcast, "history"));
  assert(episodesPath(url, sizeof(url), "p1537788786", 40) && S(url) == "/api/cardputer/podcasts/episodes?show=p1537788786&from=40");
  assert(!episodesPath(url, sizeof(url), "a../x", 0));
  assert(audioPath(url, sizeof(url), "aadventures_holmes", "1e2ecb91af8f", 600) && S(url) == "/api/cardputer/podcasts/audio?show=aadventures_holmes&ep=1e2ecb91af8f&t=600");
  assert(!audioPath(url, sizeof(url), "p1", "1e2ecb91af8", 0) && !audioPath(url, sizeof(url), "p1 ", "1e2ecb91af8f", 0));
  assert(filePath(url, sizeof(url), "1e2ecb91af8f") && S(url) == "/podcasts/files/1e2ecb91af8f.mp3");
  assert(filePath(url, sizeof(url), "1e2ecb91af8f", true) && S(url) == "/podcasts/files/1e2ecb91af8f.part");
  assert(!filePath(url, sizeof(url), "../../etc/pw"));

  // A second in a saved file is a whole frame from the start: 6000 bytes a second, give or take
  // the part of a frame.
  assert(fileOffset(0) == 0 && fileOffset(1) == 27 * 216 && fileOffset(36) == 1000 * 216);
  for (uint32_t t : {1u, 59u, 600u, 3906u, 86399u}) {
    const uint32_t at = fileOffset(t);
    assert(at % kFrameBytes == 0 && at <= t * kBytesPerSecond && t * kBytesPerSecond - at < kFrameBytes);
  }

  // The home-network ticket: the bridge's address on this device's network, or none.
  HomeTicket t;
  const char ticket[] = "ticket\t31890\t/podcasts/0123456789abcdef0123456789abcdef\nlan\t192.168.0.20\t22\nlan\t10.0.0.7\t24\n";
  assert(parseTicket(ticket, sizeof(ticket) - 1, t) && !t.none && t.port == 31890 && t.count == 2);
  assert(S(t.path) == "/podcasts/0123456789abcdef0123456789abcdef" && t.ip[0] == 0xC0A80014u && t.prefix[0] == 22);
  assert(homeFor(t, 0xC0A8021Eu) == 0xC0A80014u);    // 192.168.2.30 is on 192.168.0.0/22.
  assert(homeFor(t, 0x0A000063u) == 0x0A000007u);    // 10.0.0.99, on the second.
  assert(!homeFor(t, 0xC0A80814u) && !homeFor(t, 0) && !homeFor(t, 0xC0A80014u));  // Another network, none, or itself.
  assert(homeAudioPath(url, sizeof(url), t, 95) && S(url) == "/podcasts/0123456789abcdef0123456789abcdef?t=95");
  assert(parseTicket("none\n", 5, t) && t.none && !homeFor(t, 0xC0A8021Eu) && !homeAudioPath(url, sizeof(url), t, 0));
  assert(parseTicket("ticket\t31890\t/podcasts/0123456789abcdef0123456789abcdef\n", 55, t) && t.none);  // No address: none.
  for (const char* bad : {"{\"error\":\"not_found\"}", "ticket\t0\t/podcasts/0123456789abcdef0123456789abcdef\nlan\t1.2.3.4\t24\n",
                          "ticket\t80\t/elsewhere/0123456789abcdef0123456789abcdef\nlan\t1.2.3.4\t24\n", ""})
    assert(!parseTicket(bad, strlen(bad), t) && t.none);
  const char badLan[] = "ticket\t80\t/podcasts/0123456789abcdef0123456789abcdef\nlan\t300.1.1.1\t24\nlan\t1.2.3\t24\nlan\t1.2.3.4\t40\n";
  assert(parseTicket(badLan, sizeof(badLan) - 1, t) && t.none && !t.count);
  uint32_t ip = 0;
  assert(parseIpv4("192.168.0.20", ip) && ip == 0xC0A80014u && !parseIpv4("192.168.4", ip) && !parseIpv4("1.2.3.4.5", ip) && !parseIpv4("01234.1.1.1", ip));
  assert(ticketPath(url, sizeof(url), "p42", "111111111111") && S(url) == "/api/cardputer/podcasts/ticket?show=p42&ep=111111111111");
  assert(!ticketPath(url, sizeof(url), "p42", "x"));

  // Times.
  char b[24];
  clock(b, sizeof(b), 42); assert(S(b) == "0:42");
  clock(b, sizeof(b), 725); assert(S(b) == "12:05");
  clock(b, sizeof(b), 3723); assert(S(b) == "1:02:03");
  length(b, sizeof(b), 0); assert(S(b).empty());
  length(b, sizeof(b), 20); assert(S(b) == "1m");
  length(b, sizeof(b), 2730); assert(S(b) == "46m");
  length(b, sizeof(b), 5131); assert(S(b) == "1h 26m");
  length(b, sizeof(b), 7200); assert(S(b) == "2h");
  left(b, sizeof(b), 1800, 5131); assert(S(b) == "56m left");
  left(b, sizeof(b), 5100, 5131); assert(S(b) == "done");
  left(b, sizeof(b), 75, 0); assert(S(b) == "1:15");
  puts("PASS podcast catalog: the bridge's lines, the card's files, bridge addresses, file offsets and times");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-podcast-catalog-') as d:
    p = Path(d); (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, timeout=60)
