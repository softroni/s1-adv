#!/usr/bin/env python3
"""The Podcasts screen as the device runs it, with a fake player and card: finding shows, opening
episodes, following, playing, pausing and carrying on where you stopped, seeking, saving to the
card and playing from it with no Wi-Fi, deleting, a book's next chapter, leaving the screen, and
every screen fitting. Built on the radio's own test harness, since the two share code."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
radio = (parts / '070_main.cpp.inc').read_text()
pods = (parts / '092_main.cpp.inc').read_text()
# Wired in where the radio is: menu, keys, Esc, the loop, background work and mode names.
for f, s in [('004_main.cpp.inc', '{"Podcasts", UiMode::Podcasts}'), ('024_main.cpp.inc', 'if (modeChanged && mode == UiMode::Podcasts) beginPodcasts();'),
             ('047_main.cpp.inc', 'gUiMode == UiMode::Podcasts && !gLauncherVisible && podcastEscape()'),
             ('048_main.cpp.inc', 'if (podcastTakesText() && !gLauncherVisible && !keys.tab)'),
             ('048_main.cpp.inc', 'if (gUiMode == UiMode::Podcasts && handlePodcastKey(key, keys)) return;'),
             ('049_main.cpp.inc', '(radioTakesArrows() || podcastTakesArrows()) && (upDown'),
             ('061_main.cpp.inc', 'servicePodcasts();'), ('061_main.cpp.inc', 'renderPodcastsUi();'),
             ('063_main.cpp.inc', 'case UiMode::Podcasts:'), ('052_main.cpp.inc', 'case UiMode::Podcasts: return kHeadphones;'),
             ('022_main.cpp.inc', 'out = UiMode::Podcasts;'), ('007_main.cpp.inc', 'case UiMode::Podcasts: return "Podcasts";')]:
    assert s in (parts / f).read_text(), (f, s)
assert 'main_parts/092_main.cpp.inc' in (src / 'main.cpp').read_text()
loop = (parts / '061_main.cpp.inc').read_text().split('void loop() {', 1)[1]
assert loop.index('servicePodcasts();') < loop.index('if (radioBusy())'), 'it must run while audio holds the loop'
assert 'bool radioBusy() { return internet_radio::busy() || podcast_player::busy(); }' in radio
harness = (root / 'scripts/test_internet_radio.py').read_text()
prefix = harness.split("code=r'''", 1)[1].split("'''+app+r'''", 1)[0]
for old, new in [('enum class UiMode{Home,Radio,Voice,Mesh,Podcasts,Clock,Battery,Focus,Notes,Calculator,Alarm,Stopwatch,Calendar,Chess};UiMode gUiMode=UiMode::Radio;', 'enum class UiMode{Home,Radio,Voice,Mesh,Podcasts,Clock,Battery,Focus,Notes,Calculator,Alarm,Stopwatch,Calendar,Chess};UiMode gUiMode=UiMode::Podcasts;'),
                 ('namespace podcast_player{bool fakeBusy=false;bool busy(){return fakeBusy;}}\nint podcastStops=0;void stopPodcasts(){++podcastStops;}bool podcastOn(){return false;}bool podcastMayPlayIn(UiMode){return false;}\n',
                  'namespace { void stopPodcasts(); bool podcastOn(); bool podcastMayPlayIn(UiMode mode); }\n'),
                 ('void useBigTextFont(const String&s){bool wide=false;for(unsigned char c:s)wide=wide||c>=0x80;gCanvas.setFont(wide?&fonts::efontCN_12:&fonts::FreeSansBold9pt7b);}',
                  'int16_t useBigTextFont(const String&s){bool wide=false;for(unsigned char c:s)wide=wide||c>=0x80;gCanvas.setFont(wide?&fonts::efontCN_12:&fonts::FreeSansBold9pt7b);return wide?4:3;}'),
                 ('}SD;', ' uint64_t totalBytes(){return cardTotal;}uint64_t usedBytes(){return cardUsed;}\n}SD;'),
                 # The podcasts' own fakes log too.
                 ('void logf(const char*,...){}\n', ''),
                 ('bool gSdReady=true,cardAnswers=true,writeFails=false;', 'bool gSdReady=true,cardAnswers=true,writeFails=false;uint64_t cardTotal=32ull<<30,cardUsed=1ull<<30;'),
                 ]:
    assert old in prefix, old[:60]
    prefix = prefix.replace(old, new)
fakes = r'''
#include "podcast_catalog.h"
#include "podcast_player.h"
#include <cstdarg>
#define FILE_READ "r"
const char* kTlsRootCaPem = "ROOT CA";
const char* kAppVersion = "0.2.298-dev";
String gHubHost = "bridge.example", gDeviceToken = "tok-1";
uint16_t gHubPort = 10000;
int netPreps = 0, emojiClears = 0;
void prepareNetworkMemory(const char*, bool) { ++netPreps; }
void clearEmojiCache() { ++emojiClears; }
int compactions = 0;
bool compactMemoryForUi(const char*, bool) { ++compactions; return true; }
uint32_t myAddress = 0;  // No address: nothing to try at home, so audio comes over HTTPS.
uint32_t wifiLocalAddress() { return myAddress; }
std::vector<std::string> logged;
void logf(const char* format, ...) { char b[200]; va_list a; va_start(a, format); vsnprintf(b, sizeof(b), format, a); va_end(a); logged.push_back(b); }
constexpr int MALLOC_CAP_8BIT = 1;
size_t heap_caps_get_largest_free_block(int) { return 15000; }
struct { uint32_t getFreeHeap() { return 61000; } } ESP;
// The player, as the screen sees it.
namespace podcast_player {
enum class Job { None, List, Stream, File, Save };
Job job = Job::None;
bool running = false, cancelling = false, ended = false, saved = false, listReady = false;
State phase = State::Idle;
Error err = Error::None;
std::string path, sdPath, partPath, listBody, host, token, note;
uint32_t from = 0, pos = 0, written = 0, expected = 0, homeIp = 0;
bool home = false;
uint16_t port = 0;
int lists = 0, streams = 0, files = 0, saves = 0, stops = 0;
void setBridge(const char* h, uint16_t p, const char* t, const char* ca) { assert(std::string(ca) == "ROOT CA"); if (running) return; host = h; port = p; token = t; }
bool begin(Job j) { if (running) return false; running = true; cancelling = false; job = j; err = Error::None; phase = State::Connecting; return true; }
std::vector<std::string> reports;
bool report(const char* p) { const std::string q = p; if (q.find("/report?") == std::string::npos) return false; reports.push_back(q); return true; }
bool startList(const char* p) { if (running) return false; if (report(p)) return true; ++lists; path = p; listReady = false; listBody.clear(); home = false; return begin(Job::List); }
bool startListHome(uint32_t ip, uint16_t port, const char* p) {
  if (running) return false; assert(port == 31890); if (report(p)) return true; ++lists; path = p; listReady = false; listBody.clear(); home = true; homeIp = ip; return begin(Job::List); }
bool startStream(const char* p, uint32_t f) { if (running) return false; ++streams; path = p; from = pos = f; ended = false; home = false; return begin(Job::Stream); }
bool startStreamHome(uint32_t ip, uint16_t port, const char* p, uint32_t f) { if (running) return false; assert(port == 31890); ++streams; path = p; from = pos = f; ended = false; home = true; homeIp = ip; return begin(Job::Stream); }
bool startFile(const char* p, uint32_t f) { if (running) return false; ++files; sdPath = p; from = pos = f; ended = false; return begin(Job::File); }
bool startSave(const char* p, const char* part, const char* sd, uint32_t expect) { if (running) return false; ++saves; path = p; partPath = part; sdPath = sd; expected = expect; saved = false; written = 0; home = false; return begin(Job::Save); }
bool startSaveHome(uint32_t ip, uint16_t port, const char* p, const char* part, const char* sd, uint32_t expect) {
  if (running) return false; assert(port == 31890); ++saves; path = p; partPath = part; sdPath = sd; expected = expect; saved = false; written = 0; home = true; homeIp = ip; return begin(Job::Save); }
const char* memoryNote() { return running ? "" : note.c_str(); }
bool overHome() { return home; }
void stop() { ++stops; if (running) { cancelling = true; phase = State::Stopping; } }
bool busy() { return running; }
State state() { return phase; }
Error error() { return err; }
const char* errorText() { return err == Error::None ? nullptr : err == Error::NotFound ? "Not found any more" : err == Error::NoMemory ? "Not enough memory" : "Couldn't reach the bridge"; }
unsigned httpStatus() { return 200; }
bool playing() { return running && (job == Job::Stream || job == Job::File); }
bool saving() { return running && job == Job::Save; }
bool listing() { return running && job == Job::List; }
const char* listText(size_t& n) { if (listing() || !listReady) { n = 0; return nullptr; } n = listBody.size(); return listBody.c_str(); }
void clearList() { if (listing()) return; listReady = false; listBody.clear(); }
uint32_t position() { return pos; }
bool reachedEnd() { return !playing() && ended; }
uint32_t savedBytes() { return written; }
bool saveDone() { return !saving() && saved; }
uint16_t level() { return phase == State::Playing ? 3000 : 0; }
// What the test does as the worker.
void finish(Error e = Error::None) { assert(running); if (cancelling) e = Error::None; running = false; cancelling = false; err = e; phase = e == Error::None ? State::Idle : State::Error; }
void answer(const std::string& body) { assert(listing()); listBody = body; listReady = !cancelling; finish(); }
void sound(uint32_t at) { assert(playing()); phase = State::Playing; pos = at; }
void end() { assert(playing()); ended = true; finish(); }
void wrote(uint32_t bytes) { assert(saving()); written = bytes; saved = true; disk[sdPath] = std::string(16, 'm'); finish(); }
}  // namespace podcast_player
'''
code = prefix + fakes + radio + pods + (parts / '093_main.cpp.inc').read_text() + r'''
const std::string kHex = "0123456789abcdef0123456789abcdef";
const std::string kTicket = "ticket\t31890\t/podcasts/" + kHex + "\nlan\t192.168.0.20\t22\n";
void setUiMode(UiMode m) { if (deferInternetRadioMode(m)) return; const bool changed = gUiMode != m; gUiMode = m; if (changed && m == UiMode::Podcasts) beginPodcasts(); }
namespace pp = podcast_player;
void press(char c, bool enter = false) { Keyboard_Class::KeysState k; k.enter = enter; handlePodcastKey(c, k); }
void enter() { press('\n', true); }
void type(const char* s) { for (; *s; ++s) press(*s); }
void service(int n = 1) { for (int i = 0; i < n; ++i) { serviceInternetRadio(); servicePodcasts(); } }
void render() { selectedRow = -1; renderPodcastsUi(); }
void later(uint32_t ms) { tick += ms; }
// Shows and pages as the bridge writes them.
const std::string kFound = "show\tp42\t3\tHistory & Tea\tAnn\nshow\taholmes\t0\tThe Adventures of Sherlock Holmes\tSir Arthur Conan Doyle\n";
std::string page(const char* title, int total, int from, std::initializer_list<std::string> eps) {
  std::string s = "about\t" + std::to_string(total) + "\t" + std::to_string(from) + "\t" + title + "\tAnn\n";
  for (const auto& e : eps) s += e + "\n";
  return s;
}
const std::string kTea = page("History & Tea", 3, 0, {"ep\t111111111111\t3600\t2026-11-02\tThe Newest", "ep\t222222222222\t1800\t2026-10-26\tThe Middle", "ep\t333333333333\t0\t-\tThe Oldest"});
size_t lines(const std::string& path) { size_t n = 0; for (char c : disk[path]) n += c == '\n'; return n; }
bool has(const std::string& path, const std::string& part) { return disk[path].find(part) != std::string::npos; }
void searchFor(const char* words, podcast::Kind kind) {
  press('f'); assert(gPodView == PodView::Find);
  while (!gPodQuery.isEmpty()) press('\b');
  type(words);
  while (gPodKind != kind) press('/');
  enter(); service();
  if (!pp::listing()) fprintf(stderr, "searchFor %s: view=%d notice=%s pending=%d running=%d busy=%d radio=%d\n", words, int(gPodView), gPodNotice.c_str(), int(gPodAskPending), int(gPodAskRunning), int(pp::running), int(internet_radio::busy()));
  assert(pp::listing() && pp::path.rfind("/api/cardputer/podcasts/search?kind=", 0) == 0);
  render(); assert(shows("Searching..."));
}
int main() {
  using pp::Error;
  const std::string kShows = "/podcasts/shows.txt", kProgress = "/podcasts/progress.txt", kSaved = "/podcasts/saved.txt";
  // No card: it still finds and plays, and says nothing is kept.
  gSdReady = false; cardAnswers = false; beginPodcasts();
  assert(gPodNotice == "No SD card: nothing is kept" && disk.empty());
  render(); assert(showsPart("nothing is kept"));
  gSdReady = true; cardAnswers = true; beginPodcasts();
  assert(gPodCard && gPodView == PodView::Library && gPodTab == PodTab::Listening && gPodNotice == "F finds shows and books");
  render(); assert(shows("Listening") && shows("Shows") && shows("Saved"));

  // Finding: the words and the kind of thing go to the bridge, through its own address. The
  // library's list goes first, so the request has its memory.
  searchFor("history", podcast::Kind::Podcast);
  assert(!gPodEntries && !gPodFollowed && compactions);  // The firmware's own low-memory tidy, emoji cache and all.
  assert(pp::path == "/api/cardputer/podcasts/search?kind=podcast&q=history" && pp::host == "bridge.example" && pp::port == 10000 && pp::token == "tok-1" && netPreps);
  pp::answer(kFound); service();
  assert(gPodView == PodView::Shows && gPodFoundCount == 2 && std::string(gPodFound[0].title) == "History & Tea" && gPodFound[0].count == 3);
  render(); assert(shows("History & Tea") && shows("3"));
  // Nothing found says what was looked for; Esc while searching calls it off.
  searchFor("zzzz", podcast::Kind::Radio);
  assert(pp::path == "/api/cardputer/podcasts/search?kind=radio&q=zzzz");
  pp::answer(""); service(); assert(gPodView == PodView::Find && gPodNotice == "Nothing found for zzzz");
  press(','); assert(gPodKind == podcast::Kind::Book); press(','); assert(gPodKind == podcast::Kind::Podcast); press(','); assert(gPodKind == podcast::Kind::Radio); press('/');
  searchFor("history", podcast::Kind::Podcast); podcastEscape(); assert(gPodView == PodView::Library && pp::cancelling);
  pp::answer(kFound); service(); assert(gPodView == PodView::Library && !gPodFound);
  // ; and . can be typed in a search; , and / pick the kind.
  press('f'); while (!gPodQuery.isEmpty()) press('\b'); type("dr. who;"); assert(gPodQuery == "dr. who;"); while (!gPodQuery.isEmpty()) press('\b');
  enter(); assert(gPodNotice == "Type a few words first" && !pp::running);
  gWifiReady = false; type("x"); enter(); assert(gPodNotice == "Wi-Fi required"); gWifiReady = true; press('\b');
  searchFor("history", podcast::Kind::Podcast); pp::answer(kFound); service(); assert(gPodView == PodView::Shows);

  // Following: S, once, kept on the card.
  press('s'); assert(lines(kShows) == 1 && has(kShows, "p42\t3\tHistory & Tea\tAnn\n") && gPodNotice == "Following: it's in Shows");
  press('s'); assert(lines(kShows) == 1 && gPodNotice == "Already following");

  // Episodes: asked for by the show's id, with the follow row first.
  enter(); service(); assert(pp::listing() && pp::path == "/api/cardputer/podcasts/episodes?show=p42&from=0");
  pp::answer(kTea); service();
  assert(gPodView == PodView::Episodes && gPodPage && gPodPage->count == 3 && gPodShowFollowed && gPodSel == 1);
  render(); assert(shows("Following") && shows("The Newest") && shows("1h") && showsPart("1-3 of 3") == false);
  // Unfollow from here, and follow again.
  press(';'); assert(gPodSel == 0); enter(); assert(!gPodShowFollowed && lines(kShows) == 0);
  enter(); assert(gPodShowFollowed && lines(kShows) == 1); press('.');

  // Playing streams from the bridge, from the start, and the place is kept as it goes.
  enter(); assert(gPodView == PodView::Player && gPodWantPlaying && gPodStartPending);
  service(); assert(pp::playing() && pp::streams == 1 && pp::path == "/api/cardputer/podcasts/audio?show=p42&ep=111111111111&t=0");
  assert(lines(kProgress) == 1 && has(kProgress, "p42\t111111111111\t0\t3600\t"));
  render(); assert(shows("Asking the bridge..."));
  pp::sound(12); render(); assert(shows("Playing") && showsPart("0:12 / 1:00:00"));
  later(31000); pp::sound(95); service(); assert(has(kProgress, "p42\t111111111111\t95\t3600\t"));
  // Ahead 30 twice: it waits for the keys to rest, then starts once from the new place.
  press('/'); press('/'); assert(gPodNow.at == 155 && pp::cancelling);
  pp::finish(); service(); assert(pp::streams == 1);
  render(); assert(shows("Going to 2:35"));
  later(800); service(); assert(pp::streams == 2 && pp::path == "/api/cardputer/podcasts/audio?show=p42&ep=111111111111&t=155");
  pp::sound(160);
  press(','); assert(gPodNow.at == 145); pp::finish(); later(800); service(); assert(pp::from == 145);
  pp::sound(200);
  // Pause keeps the place on the card.
  enter(); assert(!gPodWantPlaying && pp::cancelling && gPodNotice == "Paused" && has(kProgress, "p42\t111111111111\t200\t3600\t"));
  pp::finish(); service(); assert(!pp::running);
  render(); assert(shows("Paused") && showsPart("3:20 / 1:00:00"));
  // Back in the list it says how much is left; the library has it under Listening.
  podcastEscape(); assert(gPodView == PodView::Episodes); render(); assert(shows("57m left"));
  // The shows went back to the heap when it played, so the way back is Find, words kept.
  assert(!gPodFound && compactions);
  podcastEscape(); assert(gPodView == PodView::Find && gPodQuery == "history");
  podcastEscape(); assert(gPodView == PodView::Library && gPodEntryCount == 1);
  render(); assert(shows("The Newest") && shows("57m left"));
  // And Enter there carries on from it.
  enter(); service(); assert(pp::streams == 4 && pp::from == 200 && gPodView == PodView::Player);
  pp::sound(210);
  // Searching pauses it, and once the list is in, it carries on from where it was.
  podcastEscape(); press('f'); while (!gPodQuery.isEmpty()) press('\b'); type("tea"); enter(); service();
  assert(!gPodWantPlaying && gPodNow.at == 210 && pp::cancelling && gPodAskPending);  // It waits for the stream to stop.
  pp::finish(); service(); assert(pp::listing() && pp::path == "/api/cardputer/podcasts/search?kind=podcast&q=tea");
  pp::answer(kFound); service(); assert(gPodView == PodView::Shows && gPodWantPlaying);
  service(); assert(pp::streams == 5 && pp::from == 210);
  pp::sound(220);

  // Saving pauses what plays, writes through a part file, and lists it once whole.
  enter(); service(); assert(pp::cancelling && gPodAskPending); pp::finish(); service();
  pp::answer(kTea); service(); assert(gPodView == PodView::Episodes && gPodWantPlaying);
  service(); assert(pp::playing() && pp::from == 220); pp::sound(221);  // Carried on after the list.
  gPodSel = 2; press('s');
  assert(gPodSavePending && !gPodWantPlaying && pp::cancelling && has(kProgress, "p42\t111111111111\t221\t"));
  pp::finish(); service(); assert(pp::saving() && pp::path == "/api/cardputer/podcasts/audio?show=p42&ep=222222222222&t=0" &&
                    pp::partPath == "/podcasts/files/222222222222.part" && pp::sdPath == "/podcasts/files/222222222222.mp3" && pp::expected == 1800 * 6000);
  render(); assert(shows("Saving: connecting  X stops"));
  pp::phase = pp::State::Saving; pp::written = 1800 * 3000; render(); assert(shows("Saving 50%  X stops"));
  press('s'); assert(gPodNotice == "Already saving one");
  enter(); assert(gPodNotice == "Saving: X stops it first" && !gPodWantPlaying);
  pp::wrote(1800 * 6000); service();
  assert(lines(kSaved) == 1 && has(kSaved, "p42\t222222222222\t1800\t10800000\tHistory & Tea\tThe Middle\n") && gPodNotice == "Saved: it plays with no Wi-Fi");
  assert(gPodPageSaved == 2u); render(); assert(shows("on card"));
  press('s'); assert(gPodNotice == "Already on the card");
  // The card keeps 25 at most, so the 26th asks for one to go first.
  { const std::string kept = disk[kSaved]; for (int i = 0; i < 25; ++i) { char l[200]; snprintf(l, sizeof(l), "p9\t%012x\t60\t360000\tS\tE\n", i + 1); disk[kSaved] += l; }
    gPodSel = 3; press('s'); assert(gPodNotice == "25 saved already: delete one first" && !gPodSavePending); disk[kSaved] = kept; }
  // Not enough room, or no card: refused before anything is asked.
  gPodSel = 3; cardUsed = cardTotal - (1u << 20); press('s'); assert(gPodNotice == "Not enough room on the card" && !gPodSavePending); cardUsed = 1ull << 30;
  // A saved episode plays from the card, with no Wi-Fi.
  gPodSel = 2; gWifiReady = false; enter(); service();
  assert(pp::playing() && pp::files == 1 && pp::sdPath == "/podcasts/files/222222222222.mp3" && pp::from == 0);
  pp::sound(5); render(); assert(shows("Playing from the card") && shows("card"));
  // Not saved and no Wi-Fi: it says so rather than trying.
  podcastEscape(); gPodSel = 3; enter(); assert(gPodNotice == "Wi-Fi required: S saves it for later");
  assert(pp::playing() && !pp::cancelling && gPodWantPlaying);  // And what was playing carries on.
  gWifiReady = true;
  // Deleting a saved one asks first, stops it if it's playing, and leaves the rest.
  gPodSel = 2; press('\b'); assert(gPodView == PodView::Remove); render(); assert(showsPart("Delete") || shows("The Middle"));
  press('n'); assert(gPodView == PodView::Episodes && disk.count("/podcasts/files/222222222222.mp3"));
  press('\b'); press('y');
  // It was playing from that file, so the file goes once the player has let go of it.
  assert(gPodView == PodView::Episodes && pp::cancelling && disk.count("/podcasts/files/222222222222.mp3") && gPodNotice == "Deleting..." && !gPodNowFile);
  pp::finish(); service();
  assert(!disk.count("/podcasts/files/222222222222.mp3") && lines(kSaved) == 0 && gPodNotice == "Deleted The Middle");
  gPodSel = 1; press('\b'); assert(gPodNotice == "Del removes one saved on the card");

  // An episode that plays to its end is done, and leaves Listening.
  gPodSel = 3; enter(); service(); assert(pp::streams >= 6 && pp::path.find("ep=333333333333") != std::string::npos);
  pp::sound(400); pp::end(); service();
  assert(gPodNotice == "Finished" && has(kProgress, "p42\t333333333333\t400\t400\t"));
  podcastEscape(); podcastEscape(); podcastEscape(); podcastEscape(); assert(gPodView == PodView::Library);
  for (int i = 0; i < gPodEntryCount; ++i) assert(std::string(gPodEntries[i].episode) != "333333333333");
  // A book's next chapter follows on by itself; a failure stops and says why.
  press('f'); while (!gPodQuery.isEmpty()) press('\b'); type("holmes"); while (gPodKind != podcast::Kind::Book) press('/'); enter(); service();
  pp::answer("show\taholmes\t0\tSherlock\tDoyle\n"); service(); enter(); service();
  pp::answer(page("Sherlock", 2, 0, {"ep\taaaaaaaaaaaa\t3906\t-\t01 - A Scandal in Bohemia", "ep\tbbbbbbbbbbbb\t3546\t-\t02 - The Red-Headed League"})); service();
  enter(); service(); pp::sound(3906); pp::end(); service();
  assert(gPodWantPlaying && std::string(gPodNow.episode) == "bbbbbbbbbbbb" && gPodNotice == "Next: 02 - The Red-Headed League");
  service(); assert(pp::playing() && pp::path.find("ep=bbbbbbbbbbbb&t=0") != std::string::npos);
  pp::sound(60); pp::finish(Error::Connect); service();
  assert(!gPodWantPlaying && gPodNotice == "Couldn't reach the bridge" && gPodNow.at == 60 && has(kProgress, "aholmes\tbbbbbbbbbbbb\t60\t3546"));
  // A long list comes a page at a time: / or the last row for the next, , or a row for the one before.
  podcastEscape(); assert(gPodView == PodView::Episodes);
  pp::listBody.clear();
  gPodPage->total = 45;
  press('/'); service(); assert(pp::listing() && pp::path == "/api/cardputer/podcasts/episodes?show=aholmes&from=2");
  pp::answer(page("Sherlock", 45, 20, {"ep\tcccccccccccc\t60\t-\tTwenty-one", "ep\tdddddddddddd\t60\t-\tTwenty-two"})); service();
  assert(gPodPage->from == 20 && podEpisodeRows() == 1 + 1 + 2 + 1 && gPodSel == 2);
  render(); assert(shows("< Previous 20") && shows("Next 20 >") && shows("Twenty-one"));
  int index = -1;
  assert(podEpisodeRow(0, index) == PodRow::Follow && podEpisodeRow(1, index) == PodRow::Previous && podEpisodeRow(3, index) == PodRow::Episode && index == 1 && podEpisodeRow(4, index) == PodRow::Next);
  gPodSel = 4; enter(); service(); assert(pp::path == "/api/cardputer/podcasts/episodes?show=aholmes&from=22");
  pp::answer(page("Sherlock", 45, 40, {"ep\teeeeeeeeeeee\t60\t-\tForty-one"})); service();
  press(','); service(); assert(pp::path == "/api/cardputer/podcasts/episodes?show=aholmes&from=20");
  pp::answer(page("Sherlock", 45, 20, {"ep\tcccccccccccc\t60\t-\tTwenty-one"})); service();
  // A list that fails leaves a message and the screen it was on.
  press(','); service(); pp::finish(Error::NotFound); service(); assert(gPodNotice == "Show: not found" && gPodView == PodView::Episodes);

  // The library's tabs: shows you follow open their episodes; Del unfollows.
  podcastEscape(); podcastEscape(); podcastEscape(); assert(gPodView == PodView::Library);
  press('/'); assert(gPodTab == PodTab::Following && gPodFollowedCount == 1 && std::string(gPodFollowed[0].id) == "p42");
  render(); assert(shows("History & Tea") && shows("3"));
  enter(); service(); assert(pp::path == "/api/cardputer/podcasts/episodes?show=p42&from=0"); pp::answer(kTea); service();
  assert(gPodView == PodView::Episodes && gPodEpisodesBack == PodView::Library);
  podcastEscape(); assert(gPodView == PodView::Library && gPodTab == PodTab::Following);
  press('\b'); assert(lines(kShows) == 0 && gPodFollowedCount == 0 && gPodNotice == "Unfollowed History & Tea");
  render(); assert(showsPart("Unfollowed"));
  press('/'); assert(gPodTab == PodTab::Saved && !gPodEntryCount && gPodNotice == "S on an episode saves it");
  press('/'); assert(gPodTab == PodTab::Listening); press(','); assert(gPodTab == PodTab::Saved); press('/');
  // Listening: E opens the episode's show, Del takes it off without touching the card's audio.
  assert(gPodEntryCount >= 2);
  const std::string firstShow = gPodEntries[0].show;
  press('e'); service(); assert(pp::path == "/api/cardputer/podcasts/episodes?show=" + firstShow + "&from=0");
  pp::finish(Error::Connect); service(); podcastEscape();
  const int had = gPodEntryCount; press('\b'); assert(gPodEntryCount == had - 1 && gPodNotice == "Taken off Listening");
  // Saved: what's on the card, with Del asking first.
  disk["/podcasts/files/111111111111.mp3"] = "mp3";
  disk[kSaved] = "p42\t111111111111\t3600\t21600000\tHistory & Tea\tThe Newest\n";
  press(','); assert(gPodTab == PodTab::Saved && gPodEntryCount == 1);
  render(); assert(shows("The Newest") && (shows("56m left") || shows("1h")));
  press('\b'); assert(gPodView == PodView::Remove); press('y');
  assert(gPodView == PodView::Library && !gPodEntryCount && !disk.count("/podcasts/files/111111111111.mp3") && lines(kSaved) == 0);
  // Only the last 20 places are kept, the latest first.
  for (int i = 0; i < 25; ++i) { podcast::Entry e{}; snprintf(e.show, sizeof(e.show), "p%d", 100 + i); strcpy(e.episode, "abcdefabcdef"); e.at = 10; e.seconds = 1000; podRemember(e); }
  assert(lines(kProgress) == podcast::kMaxProgress && disk[kProgress].rfind("p124\t", 0) == 0);

  // Leaving for Home while it plays: it plays on behind, Home shows it, and the lists go back to
  // the heap. The place is still kept every 30 seconds.
  press('/'); press('/'); press('/');
  gPodTab = PodTab::Listening; podLoadTab(); gPodSel = 0; enter(); service(); assert(pp::playing()); pp::sound(77);
  setUiMode(UiMode::Home); assert(gUiMode == UiMode::Home && !gRadioModePending && pp::playing() && !pp::cancelling);
  service(); assert(pp::playing() && podcastOn() && !gPodEntries && !gPodFound && !gPodFollowed && gPodBehindSinceMs);
  assert(podcastHomeCardShows() && !radioHomeCardShows());
  gCanvas.fillScreen(0); drawHomePodcastCard(); assert(shows("Ctrl+M pauses it") && showsPart(std::string(gPodNow.title).substr(0, 6)));
  later(5000); pp::sound(50); gCanvas.fillScreen(0); drawHomePodcastCard(); assert(!showsPart("Ctrl+M"));  // The meter now.
  later(31000); pp::sound(60); service(); assert(has(kProgress, std::string(gPodNow.episode) + "\t60\t"));
  // Ctrl+M pauses it there, keeping the place, and plays it again from it.
  assert(podcastMusicKey() && !podcastOn() && pp::cancelling); pp::finish(); service();
  assert(has(kProgress, std::string(gPodNow.episode) + "\t60\t") && !podcastHomeCardShows());
  assert(podcastMusicKey() && podcastOn()); service(); assert(pp::playing() && pp::from == 60); pp::sound(65);
  // Stopping by itself behind Home: Home says why for a while.
  pp::finish(Error::Connect); service();
  assert(!podcastOn() && podcastHomeCardShows() && gPodBehindNote == "Couldn't reach the bridge");
  gCanvas.fillScreen(0); drawHomePodcastCard(); assert(shows("Stopped") && showsPart("Couldn't reach"));
  later(9000); assert(!podcastHomeCardShows());
  // A podcast played last, so Ctrl+M is for it; once the radio has played, it's the radio's.
  assert(podcastMusicKey()); service(); assert(pp::playing()); pp::sound(70);
  // A screen it can't play behind stops it, keeping the place, and opens once it has.
  setUiMode(UiMode::Voice); assert(gUiMode == UiMode::Home && gRadioModePending && pp::cancelling && gPodNow.at == 70);
  service(); assert(gUiMode == UiMode::Home);
  pp::finish(); service(); assert(gUiMode == UiMode::Voice); service(); assert(!gPodEntries && !gPodFound && !gPodPage && !gPodFollowed);
  // Opening Podcasts while it plays behind shows the player.
  gUiMode = UiMode::Home; assert(podcastMusicKey()); service(); assert(pp::playing()); pp::sound(71);
  setUiMode(UiMode::Podcasts); assert(gPodView == PodView::Player && pp::playing());
  assert(podcastMusicKey() && !podcastOn()); pp::finish(); service();
  gUiMode = UiMode::Voice; service(); gUiMode = UiMode::Home; service();
  // Back again, it opens on Listening with that episode first. Esc from the library leaves.
  setUiMode(UiMode::Podcasts); assert(gPodView == PodView::Library && gPodEntryCount && std::string(gPodEntries[0].episode) == gPodNow.episode);
  assert(!podcastEscape());
  // A change cut off between its last two steps leaves only the new file, which is used.
  disk.erase(kShows); disk[kShows + ".new"] = "p7\t1\tRecovered\t\n"; gUiMode = UiMode::Home; service(); setUiMode(UiMode::Podcasts);
  assert(disk.count(kShows) && !disk.count(kShows + ".new") && podFollowing("p7"));
  // Saving can be stopped with X, and nothing is listed.
  gPodTab = PodTab::Following; podLoadTab(); enter(); service(); pp::answer(kTea); service(); gPodSel = 1; press('s'); service();
  assert(pp::saving()); press('x'); assert(pp::cancelling); pp::finish(); service(); assert(gPodNotice == "Saving stopped" && lines(kSaved) == 0);
  // Volume: 5% a press, as on the radio.
  const uint8_t before = gSpeakerVolume; press('='); assert(gSpeakerVolume > before); press('-'); assert(gSpeakerVolume == before);

  // At home: one ticket over HTTPS, then searches, lists and audio straight from the bridge's
  // computer as plain HTTP, so nothing at home needs TLS after it.
  if (pp::running) { pp::finish(); } service();
  myAddress = 0xC0A8021Eu;  // 192.168.2.30, on the bridge's 192.168.0.0/22.
  const std::string session = "/podcasts/" + kHex;
  pp::reports.clear();
  podForgetTicket(); gPodHomeOffFor = 0;
  gUiMode = UiMode::Podcasts; gPodView = PodView::Find; gPodKind = podcast::Kind::Podcast; while (!gPodQuery.isEmpty()) press('\b'); type("tea"); enter(); service();
  assert(pp::listing() && pp::path == "/api/cardputer/podcasts/ticket" && !pp::home && gPodAskPending);
  pp::answer(kTicket); service(); service();
  assert(pp::listing() && pp::home && pp::homeIp == 0xC0A80014u && pp::path == session + "/search?kind=podcast&q=tea");
  pp::answer(kFound); service(); assert(gPodView == PodView::Shows && gPodFoundCount == 2);
  enter(); service(); assert(pp::home && pp::path == session + "/episodes?show=p42&from=0");
  pp::answer(kTea); service(); assert(gPodView == PodView::Episodes);
  int lists = pp::lists, streams = pp::streams;
  podcast::Entry tea{};
  strcpy(tea.show, "p42"); strcpy(tea.episode, "111111111111"); strcpy(tea.showTitle, "History & Tea"); strcpy(tea.title, "The Newest");
  tea.seconds = 3600; tea.at = 40;
  gPodHasNow = false; podPlay(tea, PodView::Episodes); service();
  assert(pp::playing() && pp::home && pp::path == session + "/audio?show=p42&ep=111111111111&t=40" && pp::streams == streams + 1 && pp::lists == lists);
  render(); assert(shows("Asking the bridge..."));
  pp::sound(45); render(); assert(shows("home"));
  press('/'); pp::finish(); later(800); service();
  assert(pp::home && pp::path == session + "/audio?show=p42&ep=111111111111&t=75" && pp::lists == lists);
  // A ticket the bridge has forgotten since it restarted: a new session, once.
  pp::finish(Error::NotFound); service(); service();
  assert(pp::listing() && pp::path == "/api/cardputer/podcasts/ticket" && gPodWantPlaying);
  pp::answer(kTicket); service(); service(); assert(pp::home && pp::path == session + "/audio?show=p42&ep=111111111111&t=75");
  // Not reachable from here: over HTTPS straight away, and on this network from then on.
  pp::finish(Error::Connect); service(); service();
  assert(pp::playing() && !pp::home && pp::path == "/api/cardputer/podcasts/audio?show=p42&ep=111111111111&t=75");
  pp::sound(80); enter(); pp::finish(); service();
  lists = pp::lists;
  enter(); service(); assert(pp::playing() && !pp::home && pp::lists == lists);  // No ticket asked again here.
  pp::sound(90); enter(); pp::finish(); service();
  // Lists go over HTTPS here too now.
  press(','); service(); if (pp::listing()) { assert(!pp::home); pp::finish(Error::NotFound); service(); }
  // Another network asks again; this ticket has no address on it, so it's HTTPS there.
  myAddress = 0x0A000063u; enter(); service(); assert(pp::listing() && pp::path == "/api/cardputer/podcasts/ticket");
  pp::answer(kTicket); service(); service(); assert(pp::playing() && !pp::home);
  pp::sound(95); enter(); pp::finish(); service();
  // A list at home that can't connect is asked again over HTTPS, at once.
  myAddress = 0xC0A8021Eu; gPodHomeOffFor = 0; podForgetTicket();
  gPodView = PodView::Find; enter(); service(); assert(pp::path == "/api/cardputer/podcasts/ticket"); pp::answer(kTicket); service(); service();
  assert(pp::home && pp::path == session + "/search?kind=podcast&q=tea");
  pp::finish(Error::Connect); service(); service();
  assert(pp::listing() && !pp::home && pp::path == "/api/cardputer/podcasts/search?kind=podcast&q=tea" && gPodHomeOffFor == myAddress);
  pp::answer(kFound); service(); assert(gPodView == PodView::Shows);
  // A bridge with no home door open says none, and HTTPS it is.
  gPodHomeOffFor = 0; podForgetTicket(); gPodView = PodView::Episodes;
  enter(); service(); assert(pp::listing() && pp::path == "/api/cardputer/podcasts/ticket"); pp::answer("none\n"); service(); service();
  assert(!pp::home);
  if (pp::listing()) { pp::answer(kTea); service(); }
  if (pp::playing()) { pp::sound(95); enter(); pp::finish(); service(); }
  // When the ticket can't be had for memory, the screen says what ran short, and the bridge hears.
  gPodHomeOffFor = 0; podForgetTicket(); gPodView = PodView::Player; gPodHasNow = true; gPodNow = tea; pp::reports.clear();
  enter(); service(); assert(pp::listing() && pp::path == "/api/cardputer/podcasts/ticket");
  pp::note = "TLS 61K/15K"; pp::finish(Error::NoMemory); service();
  assert(!gPodWantPlaying && gPodNotice == "No memory: TLS 61K/15K" && !logged.empty());
  service(); pp::note.clear();
  assert(pp::reports.size() == 1 && pp::reports[0].rfind("/api/cardputer/podcasts/report?stage=ticket&e=1&free=61000&big=15000&", 0) == 0 &&
         pp::reports[0].find("&note=TLS%2061K%2F15K") != std::string::npos);
  // A player that runs short reports too, over the home door when there's one.
  enter(); service(); pp::answer(kTicket); service(); service(); assert(pp::home && pp::playing());
  pp::note = "player 17K/9K"; pp::finish(Error::NoMemory); service(); service(); pp::note.clear();
  assert(gPodNotice == "No memory: player 17K/9K" && pp::reports.size() == 2 && pp::reports[1].rfind(session + "/report?stage=play&e=1&", 0) == 0 &&
         pp::reports[1].find("&via=home&") != std::string::npos);
  // Saving at home goes the same way.
  enter(); service(); assert(pp::home && pp::playing()); pp::sound(96);
  disk.erase("/podcasts/files/111111111111.mp3");
  { std::string kept; std::swap(kept, disk[kSaved]); press('s'); assert(gPodSavePending); pp::finish(); service(); service();
    assert(pp::saving() && pp::home && pp::path == session + "/audio?show=p42&ep=111111111111&t=0" && pp::partPath == "/podcasts/files/111111111111.part");
    pp::wrote(3600 * 6000); service(); assert(has(kSaved, "p42\t111111111111\t3600\t")); disk[kSaved] = kept; }
  // And a home save that can't connect goes over HTTPS.
  podDeleteSaved("111111111111"); gPodHomeOffFor = 0; press('s'); service(); assert(pp::saving() && pp::home);
  pp::finish(Error::Connect); service(); service(); assert(pp::saving() && !pp::home && pp::path == "/api/cardputer/podcasts/audio?show=p42&ep=111111111111&t=0");
  pp::wrote(100); service();
  myAddress = 0;
  // Every screen fits, with long names and other scripts, in each state.
  const char* titles[] = {"A", "The Very Long Title Of An Episode That Goes On And On Past Two Lines Of The Player Screen Easily",
                          "Привет мир, это очень длинное название выпуска подкаста", "日本語のタイトル"};
  for (const char* t : titles) {
    podcast::copyText(gPodNow.title, sizeof(gPodNow.title), t);
    podcast::copyText(gPodNow.showTitle, sizeof(gPodNow.showTitle), t);
    podcast::copyText(gPodShow.title, sizeof(gPodShow.title), t);
    gPodHasNow = true;
    for (uint32_t secs : {0u, 59u, 3600u, 360000u}) {
      gPodNow.seconds = secs; gPodNow.at = secs / 2;
      for (PodView v : {PodView::Library, PodView::Find, PodView::Shows, PodView::Episodes, PodView::Player, PodView::Remove}) {
        gPodView = v;
        for (PodTab tab : {PodTab::Listening, PodTab::Following, PodTab::Saved}) { gPodTab = tab; podLoadTab(); render(); }
      }
    }
    podcast::copyText(gPodRemove.title, sizeof(gPodRemove.title), t);
    podcast::copyText(gPodRemove.showTitle, sizeof(gPodRemove.showTitle), t);
    gPodView = PodView::Remove; render();
  }
  gPodView = PodView::Player; gPodHasNow = false; render(); assert(shows("Nothing chosen yet"));
  gPodView = PodView::Find; for (podcast::Kind k : {podcast::Kind::Podcast, podcast::Kind::Book, podcast::Kind::Radio}) {
    gPodKind = k; gPodQuery = ""; render(); gPodQuery = std::string(40, 'W'); render(); }
  puts("PASS podcasts screen: finding, following, episodes a page at a time, playing and carrying on, seeking, saving and playing from the card, deleting, a book's next chapter, the library's tabs, leaving mid-episode, and screens that fit");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-podcasts-ui-') as d:
    p = Path(d); (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-unused-function',
                    '-DINTERNET_RADIO_HOST_TEST', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(src), str(p / 'test.cpp'),
                    str(src / 'internet_radio_transport.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, timeout=120)
