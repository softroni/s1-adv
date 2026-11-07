#!/usr/bin/env python3
"""The Lab, live on the bridge's page: the report's exact lines, the request in front of them, the
bridge's answers and tickets read as the device reads them, and when it sends, all in lab_live.h
under ASan/UBSan; the real socket pump in lab_live_net.cpp against a small server on this
computer, answered, refused and left hanging; and the reports wired into the Lab's open, close and
loop the way the rest of the Lab is, with the ticket asked through the background check's worker."""
from pathlib import Path
import http.server, os, socket, subprocess, tempfile, threading
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
part = lambda name: (parts / name).read_text()
live = part('097_main.cpp.inc')
cxx = os.environ.get('CXX', 'c++')
flags = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']

# --- Wired in ------------------------------------------------------------------------------------
assert 'main_parts/097_main.cpp.inc' in (src / 'main.cpp').read_text()
lab = part('081_main.cpp.inc')
begin = lab.split('void beginLab() {', 1)[1].split('\n}\n', 1)[0]
assert 'labLiveOpened();' in begin
stop = lab.split('void stopLab() {', 1)[1].split('\n}\n', 1)[0]
assert stop.index('if (!gLabOpen) return;') < stop.index('labLiveClosing();') < stop.index('labStopPlaying();'), \
    'the last report goes while the Lab still knows it was open'
service = lab.split('void serviceLab() {', 1)[1].split('\n}\n', 1)[0]
assert service.index('labWriteOutputs(now);') < service.index('serviceLabLive(now);'), 'reports what was just read and driven'
assert service.index('if (gLauncherVisible) {') < service.index('serviceLabLive(now);'), 'nothing goes behind the menu'
loop = part('061_main.cpp.inc').split('void loop() {', 1)[1]
assert loop.index('serviceLab();') < loop.index('serviceLabLiveNet();') < loop.index('servicePulseGps(millis());')
for name in ['void labLiveOpened();', 'void labLiveClosing();', 'void serviceLabLive(uint32_t now);', 'void serviceLabLiveNet();']:
    assert name in part('005_main.cpp.inc'), name
# The ticket rides the background check's worker; its answer never counts for or against the link.
inbox = part('020_main.cpp.inc')
assert 'result->done = request->done;' in inbox
done = inbox.split('void serviceInboxNetworkCompletion() {', 1)[1]
assert done.index('if (result->weather) {') < done.index('if (result->done) {') < done.index('gBridgeLastSuccessMs = millis();')
ask = live.split('bool labLiveAskTicket(uint32_t now) {', 1)[1].split('\n}\n', 1)[0]
for gate in ['gInboxHttpBusy', 'labAllowsNetwork()', 'autoInboxPollAllowed()', 'startInboxNetworkRequest(', 'ask->done = labLiveTicketAnswered;']:
    assert gate in ask, gate
send = live.split('void serviceLabLive(uint32_t now) {', 1)[1].split('\n}\n', 1)[0]
assert 'gInboxHttpBusy' in send and 'labLiveHomeIp()' in send, 'a report waits for TLS, and only goes at home'
# The page's buttons: applied as the answer is read, only at 200, answered in the very next report, once.
assert 'const bool answering = gLabLiveDids.count() && !gLabLiveCadence.stopped();' in send
assert '(!gLabLiveCadence.due(now) && !answering)' in send and 'else gLabLiveDids.clear();' in send
net = live.split('void serviceLabLiveNet() {', 1)[1].split('\n}\n', 1)[0]
assert net.index('if (gLabLiveLast) {') < net.index('if (answer.status != 200) return;') < net.index('labPageSet(set.out, set.value, millis())')
assert 'gLabLiveDids.clear();' in live.split('void labLiveOpened() {', 1)[1].split('\n}\n', 1)[0]
assert 's.did = gLabLiveDids.list();' in live
assert 'kAnswerBytes = 512;' in (src / 'lab_live_net.cpp').read_text()
# No task, no TLS: the Lab's memory stays the chat's.
for text in [live, (src / 'lab_live_net.cpp').read_text(), (src / 'lab_live.h').read_text()]:
    assert 'xTaskCreate' not in text and 'WiFiClientSecure' not in text
assert 'O_NONBLOCK' in (src / 'lab_live_net.cpp').read_text()
answered = live.split('void labLiveTicketAnswered(int code, const String& body) {', 1)[1].split('\n}\n', 1)[0]
assert 'lab_live::parseTicket(' in answered and 'gLabLiveTicketFor = wifiLocalAddress();' in answered
# Every view of the Lab has a word, so a new one fails the build until it gets one.
views = part('082_main.cpp.inc').split('enum class LabView : uint8_t {', 1)[1].split('}', 1)[0]
for view in [v.strip() for v in views.split(',') if v.strip()]:
    assert f'LabView::{view}:' in live, view

# --- The pure part -------------------------------------------------------------------------------
pure = r'''
#include <assert.h>
#include <string>
#include "lab_live.h"
using namespace lab_live;
static std::string body(const Snapshot& s, size_t room = kMaxBody + 1) {
  char out[2048];
  const size_t n = lab_live::body(s, out, room);
  assert(n == strlen(out) && n < room);
  return out;
}
int main() {
  Snapshot s;
  // Nothing found, nothing driven: switches read 0 and everything else -1.
  assert(body(s) == "lab\t1\nview\tother\nunits\tmetric\nplaying\t0\nfound\t\n"
         "out\tservo\t-1\nout\tstrip_color\t-1\nout\tstrip_bright\t-1\nout\tstrip_length\t-1\nout\trelay_1\t0\n"
         "out\trelay_2\t0\nout\tled\t-1\nout\tspeaker\t-1\nout\tlamp_power\t0\nout\tlamp_bright\t-1\nout\tac_tcl\t0\nout\tac_daikin\t0\n");
  // A full Lab: on the GPS's second page, units found, a hub, readings, outputs and rules firing.
  s.view = "device"; s.open = "LoRa Cap GPS"; s.page = 1; s.us = true; s.playing = true;
  s.found[static_cast<int>(lab::Unit::Ultrasonic)] = s.found[static_cast<int>(lab::Unit::Env)] = s.found[static_cast<int>(lab::Unit::Hub)] = true;
  s.hub = true; s.ports[0] = lab::Part::Knob; s.ports[2] = lab::Part::Servo; s.ports[5] = lab::Part::Strip;
  s.live[static_cast<int>(lab::Sig::Distance)] = true; s.value[static_cast<int>(lab::Sig::Distance)] = 31;
  s.live[static_cast<int>(lab::Sig::Temperature)] = true; s.value[static_cast<int>(lab::Sig::Temperature)] = -4;
  s.live[static_cast<int>(lab::Sig::Motion)] = true; s.value[static_cast<int>(lab::Sig::Motion)] = 1;
  s.driven[static_cast<int>(lab::Out::Servo)] = true; s.out[static_cast<int>(lab::Out::Servo)] = 0;   // 0 degrees is real.
  s.driven[static_cast<int>(lab::Out::Led)] = true; s.out[static_cast<int>(lab::Out::Led)] = 0;       // So is red.
  s.driven[static_cast<int>(lab::Out::Relay1)] = true; s.out[static_cast<int>(lab::Out::Relay1)] = 1;
  s.driven[static_cast<int>(lab::Out::Relay2)] = true; s.out[static_cast<int>(lab::Out::Relay2)] = 0;
  s.driven[static_cast<int>(lab::Out::Tone)] = true; s.out[static_cast<int>(lab::Out::Tone)] = 930;
  s.gps = true; s.fix = true; s.used = 8; s.inView = 12; s.hasAltitude = true; s.altitudeM = 180; s.hdopE1 = 9;
  s.firing = 0b101; s.seq = "Door wave"; s.sweeping = true; s.stripTest = 1;
  const std::string full = body(s);
  assert(full == "lab\t1\nview\tdevice\nopen\tLoRa Cap GPS\npage\t1\nunits\tus\nplaying\t1\nfound\tUltrasonic,ENV III,I/O hub\n"
         "hub\tMini Angle,Nothing,Servo,Nothing,Nothing,LED strip\n"
         "sig\tdistance\t31\nsig\ttemperature\t-4\nsig\tmotion\t1\n"
         "out\tservo\t0\nout\tstrip_color\t-1\nout\tstrip_bright\t-1\nout\tstrip_length\t-1\nout\trelay_1\t1\n"
         "out\trelay_2\t0\nout\tled\t0\nout\tspeaker\t930\nout\tlamp_power\t0\nout\tlamp_bright\t-1\nout\tac_tcl\t0\nout\tac_daikin\t0\n"
         "gps\t1\t8\t12\t180\t9\nstrip_test\trainbow\nsweeping\t1\nfiring\t1,3\nseq\tDoor wave\n");
  // Parts straight on the wires; a GPS heard before its fix, altitude and spread unknown.
  Snapshot w; w.view = "dashboard"; w.page = 0; w.onWires = true; w.white = lab::Part::Light; w.yellow = lab::Part::Strip;
  w.gps = true; w.inView = 5;
  const std::string wires = body(w);
  assert(wires.find("wires\tLight\tLED strip\n") != std::string::npos && wires.find("gps\t0\t0\t5\t-\t-1\n") != std::string::npos);
  assert(wires.find("page\t") == std::string::npos && wires.find("hub\t") == std::string::npos);  // A page only with a part open.
  // Every report fits, with every reading live and every output driven at its longest.
  Snapshot most = s;
  for (int i = 0; i < lab::kSigCount; ++i) { most.live[i] = true; most.value[i] = -32000; }
  for (int i = 0; i < lab::kOutCount; ++i) { most.driven[i] = true; most.out[i] = -32000; }
  for (int i = 0; i < lab::kUnitCount; ++i) most.found[i] = true;
  most.firing = 0xFF; most.seq = "ABCDEFGHIJKLMNO"; most.open = "Cardputer LED";
  Dids answered;  // And the page's 8 buttons answered with the longest words.
  for (int i = 0; i < kMaxSets; ++i) answered.add(65535, lab::PageSet::SequenceScreen);
  most.did = answered.list(); most.dids = answered.count();
  const std::string longest = body(most);
  assert(longest.find("did\t65535\tsequence screen open\n") != std::string::npos);
  printf("longest report %zu bytes\n", longest.size());
  // With the page's 8 answers on top, a quarter of the room is still to spare.
  assert(longest.size() < kMaxBody * 3 / 4 && longest.find("firing\t1,2,3,4,5,6,7,8\n") != std::string::npos);
  // Too little room drops whole lines and never cuts one.
  for (size_t room = 0; room < full.size() + 2; ++room) {
    char out[2048];
    const size_t n = lab_live::body(s, out, room);
    assert(n < (room ? room : 1));
    assert(full.compare(0, n, out, n) == 0 && (n == 0 || out[n - 1] == '\n'));
    if (room > 0 && room <= strlen("lab\t1\nview\tdevice\n")) assert(n == 0);
  }
  char closed[32];
  assert(closedBody(closed, sizeof(closed)) == 18 && std::string(closed) == "lab\t1\nview\tclosed\n");
  assert(closedBody(closed, 10) == 0);
  // The request's head.
  char head[192];
  const uint32_t ip = (192u << 24) | (168u << 16) | (0u << 8) | 20u;
  const size_t h = lab_live::head(head, sizeof(head), ip, 31890, "/lab/0123456789abcdef0123456789abcdef", 42);
  assert(h == strlen(head) && std::string(head) ==
         "POST /lab/0123456789abcdef0123456789abcdef/live HTTP/1.1\r\nHost: 192.168.0.20:31890\r\n"
         "Content-Type: text/plain\r\nContent-Length: 42\r\nConnection: close\r\n\r\n");
  assert(lab_live::head(head, 40, ip, 31890, "/lab/x", 1) == 0);
  // The bridge's answers.
  auto read = [](const char* t) { return readAnswer(t, strlen(t)); };
  Answer a = read("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nwatch\t1\n");
  assert(a.status == 200 && a.watch == 1);
  a = read("HTTP/1.1 200 OK\r\n\r\nwatch\t0"); assert(a.status == 200 && a.watch == 0);
  a = read("HTTP/1.1 200 OK\r\n\r\nwatch\t0\r\n"); assert(a.watch == 0);
  a = read("HTTP/1.1 200 OK\r\nX-Note: watch\t1\r\n\r\nok\n"); assert(a.status == 200 && a.watch == -1);  // Only the body counts.
  a = read("HTTP/1.1 200 OK\r\n\r\nwatch\t12\n"); assert(a.watch == -1);
  a = read("HTTP/1.1 404 Not Found\r\n\r\nnot_found\n"); assert(a.status == 404 && a.watch == -1);
  a = read("HTTP/1.1 200 OK\r\n"); assert(a.status == 200 && a.watch == -1);  // Cut off before the body.
  // The page's buttons: set <id> <output> <value>, read in order, each with what's wrong with it, if anything.
  a = read("HTTP/1.1 200 OK\r\n\r\nwatch\t1\nset\t1\tservo\t90\nset\t2\tstrip_color\t-1\r\nset\t65535\trelay_1\t1\n"
           "set\t4\tled\t-1\nset\t5\tspeaker\t0\nset\t6\tspeaker\t2000\nset\t7\tlamp_bright\t100\nset\t8\tac_daikin\t0\n");
  assert(a.status == 200 && a.watch == 1 && a.sets == 8);
  const struct { unsigned id; lab::Out out; int value; } want[] = {{1, lab::Out::Servo, 90}, {2, lab::Out::StripColor, -1},
      {65535, lab::Out::Relay1, 1}, {4, lab::Out::Led, -1}, {5, lab::Out::Tone, 0}, {6, lab::Out::Tone, 2000},
      {7, lab::Out::LampBright, 100}, {8, lab::Out::AcDaikin, 0}};
  for (int i = 0; i < 8; ++i) assert(a.set[i].id == want[i].id && a.set[i].out == want[i].out && a.set[i].value == want[i].value &&
                                     a.set[i].check == lab::PageSet::Done);
  // Every output the page can set, at both ends of its range, and one past each end refused.
  for (uint8_t i = 0; i < lab::kOutCount; ++i) {
    const lab::Out out = static_cast<lab::Out>(i);
    char text[160];
    const lab::OutInfo& o = lab::kOuts[i];
    snprintf(text, sizeof(text), "HTTP/1.1 200 OK\r\n\r\nset\t1\t%s\t%d\nset\t2\t%s\t%d\nset\t3\t%s\t%d\nset\t4\t%s\t%d\n",
             o.key, o.lo, o.key, o.hi, o.key, o.lo - (out == lab::Out::StripColor || out == lab::Out::Led ? 2 : 1), o.key, o.hi + 1);
    a = read(text);
    assert(a.sets == 4);
    const lab::PageSet ok = lab::pageCanSet(out) ? lab::PageSet::Done : lab::PageSet::UnknownOutput;
    const lab::PageSet past = lab::pageCanSet(out) ? lab::PageSet::OutOfRange : lab::PageSet::UnknownOutput;
    assert(a.set[0].check == ok && a.set[1].check == ok && a.set[2].check == past && a.set[3].check == past);
  }
  assert(!lab::pageValueValid(lab::Out::Tone, 199) && lab::pageValueValid(lab::Out::Tone, 0) && !lab::pageValueValid(lab::Out::Servo, -1));
  // Unknown outputs and bad values are answered as such; a line with no id it can read is dropped.
  a = read("HTTP/1.1 200 OK\r\n\r\nset\t9\tghost\t1\nset\t10\tsay\t1\nset\t11\tservo\tninety\nset\t12\tservo\nset\t13\tservo\t1.5\n"
           "set\t0\tservo\t1\nset\t65536\tservo\t1\nset\tx\tservo\t1\nset\t-3\tservo\t1\nset\t\tservo\t1\nhello\t1\nset\t14\t\t1\n"
           "set\t15\tservo\t-0\nset\t16\tservo\t99999999\n");
  assert(a.sets == 8);
  const lab::PageSet bad[] = {lab::PageSet::UnknownOutput, lab::PageSet::UnknownOutput, lab::PageSet::OutOfRange,
      lab::PageSet::OutOfRange, lab::PageSet::OutOfRange, lab::PageSet::UnknownOutput, lab::PageSet::Done, lab::PageSet::OutOfRange};
  const unsigned badIds[] = {9, 10, 11, 12, 13, 14, 15, 16};
  for (int i = 0; i < 8; ++i) assert(a.set[i].id == badIds[i] && a.set[i].check == bad[i]);
  assert(a.set[6].value == 0);  // "-0" is 0.
  // More than 8 in one answer: the first 8, in order, and the rest ignored.
  std::string many = "HTTP/1.1 200 OK\r\n\r\nwatch\t0\n";
  for (int i = 1; i <= 11; ++i) many += "set\t" + std::to_string(i) + "\tservo\t" + std::to_string(i * 10) + "\n";
  a = read(many.c_str());
  assert(a.watch == 0 && a.sets == 8 && a.set[0].id == 1 && a.set[7].id == 8 && a.set[7].value == 80);
  // A 404's body isn't read for buttons any more than for watch.
  a = read("HTTP/1.1 404 Not Found\r\n\r\nset\t1\tservo\t9\n"); assert(a.status == 404 && a.sets == 1);  // Read, but 097 applies only at 200.
  // What became of them goes first in the next report, once.
  Dids dids;
  dids.add(1, lab::PageSet::Done); dids.add(65535, lab::PageSet::RulesPlaying); dids.add(3, lab::PageSet::NotSetUp);
  Snapshot d; d.did = dids.list(); d.dids = dids.count();
  const std::string withDids = body(d);
  assert(withDids.rfind("lab\t1\nview\tother\ndid\t1\tok\ndid\t65535\trules are playing\ndid\t3\tnot set up\nunits\t", 0) == 0);
  for (int i = 0; i < 12; ++i) dids.add(uint16_t(100 + i), lab::PageSet::OutOfRange);
  assert(dids.count() == kMaxSets);  // Never more than an answer can hold.
  dids.clear(); assert(dids.count() == 0);
  for (uint8_t i = 0; i < uint8_t(lab::PageSet::Count); ++i) assert(!strchr(lab::kPageSetWords[i], '\t') && strlen(lab::kPageSetWords[i]) < 24);
  assert(read("").status == 0 && read("garbage that is long").status == 0 && read("HTTP/1.1 999 Huh\r\n\r\n").status == 0);
  // Tickets, as Planes reads its own.
  podcast::HomeTicket t;
  const char* good = "ticket\t31890\t/lab/0123456789abcdef0123456789abcdef\nlan\t192.168.0.20\t24\nlan\t10.0.0.7\t8\n";
  assert(lab_live::parseTicket(good, strlen(good), t) && !t.none && t.port == 31890 && t.count == 2 && t.prefix[0] == 24);
  assert(!strcmp(t.path, "/lab/0123456789abcdef0123456789abcdef") && t.ip[0] == ip);
  const uint32_t mine = ip + 5, away = (172u << 24) | (20u << 16) | 1u;
  assert(podcast::homeFor(t, mine) == ip && podcast::homeFor(t, away) == 0 && podcast::homeFor(t, ip) == 0);
  assert(lab_live::parseTicket("none\n", 5, t) && t.none);
  const char* noLan = "ticket\t31890\t/lab/0123456789abcdef0123456789abcdef\n";
  assert(lab_live::parseTicket(noLan, strlen(noLan), t) && t.none);  // Nowhere to send it.
  for (const char* bad : {"ticket\t31890\t/planes/0123456789abcdef0123456789abcdef\nlan\t192.168.0.20\t24\n",
                          "ticket\t31890\t/lab/0123456789ABCDEF0123456789abcdef\nlan\t192.168.0.20\t24\n",
                          "ticket\t31890\t/lab/0123\nlan\t192.168.0.20\t24\n", "ticket\t0\t/lab/0123456789abcdef0123456789abcdef\n",
                          "{\"error\":\"not_found\"}", "<html>Not found</html>"}) {
    assert(!lab_live::parseTicket(bad, strlen(bad), t) && t.none);
  }
  // When to send.
  Cadence c;
  assert(c.due(0) && c.due(123456) && c.interval() == kIdleMs);
  c.sent(1000); assert(!c.due(1500) && !c.due(15999) && c.due(16000));
  Answer watched; watched.status = 200; watched.watch = 1;
  c.answered(watched); assert(c.watched() && c.interval() == kWatchedMs && !c.due(1999) && c.due(2000));
  Answer quiet; quiet.status = 200; quiet.watch = 0;
  c.answered(quiet); assert(!c.watched() && !c.due(2000) && c.due(16000));
  Answer silent; silent.status = 200;  // A bridge that says nothing about watching: the heartbeat.
  c.answered(watched); c.answered(silent); assert(c.interval() == kIdleMs);
  c.sent(0xFFFFF000u); assert(!c.due(0x00000100u) && c.due(0xFFFFF000u + kIdleMs));  // millis() wraps.
  Answer missing; missing.status = 404;
  c.answered(watched); c.failed(); assert(!c.watched() && c.failures() == 1 && !c.stopped());
  c.answered(missing); assert(c.failures() == 2 && !c.stopped());
  c.answered(watched); assert(c.failures() == 0);  // One good answer forgives.
  c.failed(); c.failed(); c.failed(); assert(c.stopped() && !c.due(0xFFFFFFFFu));
  c.failed(); assert(c.failures() == kMaxFailures);
  c.restart(); assert(!c.stopped() && c.due(5));
  return 0;
}
'''

# --- The socket pump, against a real server ------------------------------------------------------
pump = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <string>
#include <thread>
#include "lab_live.h"
#include "lab_live_net.h"
static uint32_t ms() {
  using namespace std::chrono;
  return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
// Runs one report to the end, never waiting inside the pump; how long it took.
static uint32_t run(uint16_t port, const char* path, const char* body, uint32_t timeoutMs, std::string& answer) {
  const uint32_t ip = (127u << 24) | 1u;
  char head[192];
  const size_t h = lab_live::head(head, sizeof(head), ip, port, path, strlen(body));
  const uint32_t started = ms();
  assert(lab_live_net::start(ip, port, head, h, body, strlen(body), started, timeoutMs));
  assert(lab_live_net::busy() && !lab_live_net::finished());
  assert(!lab_live_net::start(ip, port, head, h, body, strlen(body), started, timeoutMs));  // One at a time.
  uint32_t longest = 0;
  while (!lab_live_net::finished()) {
    const uint32_t before = ms();
    lab_live_net::service(ms());
    const uint32_t took = ms() - before;
    if (took > longest) longest = took;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(ms() - started < timeoutMs + 1000);
  }
  assert(longest < 50 && "a turn of the pump returns at once");
  size_t length = 0;
  answer.assign(lab_live_net::answer(length), length);
  assert(!lab_live_net::busy());
  lab_live_net::drop();
  assert(!lab_live_net::finished());
  return ms() - started;
}
int main(int argc, char** argv) {
  assert(argc == 4);
  const uint16_t good = uint16_t(atoi(argv[1])), closed = uint16_t(atoi(argv[2])), hangs = uint16_t(atoi(argv[3]));
  const std::string body(1200, 'x');  // Big enough to need more than one send on a slow socket.
  std::string answer;
  // Answered: the bridge's watch comes back.
  run(good, "/lab/0123456789abcdef0123456789abcdef", ("lab\t1\nview\tdevice\n" + body + "\n").c_str(), 2500, answer);
  lab_live::Answer a = lab_live::readAnswer(answer.data(), answer.size());
  assert(a.status == 200 && a.watch == 1);
  run(good, "/lab/ffffffffffffffffffffffffffffffff", "lab\t1\nview\tclosed\n", 2500, answer);
  a = lab_live::readAnswer(answer.data(), answer.size());
  assert(a.status == 404 && a.watch == -1);
  // Refused: it ends straight away with nothing.
  assert(run(closed, "/lab/0123456789abcdef0123456789abcdef", "lab\t1\n", 2500, answer) < 1000 && answer.empty());
  // Accepted but never answered: it gives up at its deadline, and the pump still never waits.
  const uint32_t took = run(hangs, "/lab/0123456789abcdef0123456789abcdef", "lab\t1\n", 400, answer);
  assert(took >= 400 && took < 1400 && answer.empty());
  // Abort drops one under way, and the next starts clean.
  const uint32_t ip = (127u << 24) | 1u;
  char head[192];
  const size_t h = lab_live::head(head, sizeof(head), ip, hangs, "/lab/0123456789abcdef0123456789abcdef", 6);
  assert(lab_live_net::start(ip, hangs, head, h, "lab\t1\n", 6, ms(), 5000));
  lab_live_net::service(ms());
  lab_live_net::abort();
  assert(!lab_live_net::busy() && !lab_live_net::finished());
  run(good, "/lab/0123456789abcdef0123456789abcdef", "lab\t1\n", 2500, answer);
  assert(lab_live::readAnswer(answer.data(), answer.size()).watch == 1);
  puts("pump ok");
  return 0;
}
'''

received = []
class Door(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        data = self.rfile.read(length).decode()
        received.append((self.path, self.headers.get('Content-Type'), data))
        ok = self.path.startswith('/lab/0123456789abcdef0123456789abcdef/live')
        reply = b'watch\t1\n' if ok else b'not_found\n'
        self.send_response(200 if ok else 404)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(reply)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(reply)

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'pure.cpp').write_text(pure)
    subprocess.run([cxx, *flags, '-I' + str(src), str(tmp / 'pure.cpp'), '-o', str(tmp / 'pure')], check=True)
    subprocess.run([str(tmp / 'pure')], check=True)
    (tmp / 'pump.cpp').write_text(pump)
    subprocess.run([cxx, *flags, '-I' + str(src), str(tmp / 'pump.cpp'), str(src / 'lab_live_net.cpp'), '-o', str(tmp / 'pump')], check=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Door)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    spare = socket.socket(); spare.bind(('127.0.0.1', 0)); closed = spare.getsockname()[1]; spare.close()
    silent = socket.socket(); silent.bind(('127.0.0.1', 0)); silent.listen(4)  # Accepts, never answers.
    try:
        out = subprocess.run([str(tmp / 'pump'), str(server.server_address[1]), str(closed), str(silent.getsockname()[1])],
                             check=True, capture_output=True, text=True, timeout=30).stdout
    finally:
        server.shutdown()
        silent.close()
    assert out.strip() == 'pump ok', out
    assert len(received) == 3, received
    path, kind, data = received[0]
    assert path == '/lab/0123456789abcdef0123456789abcdef/live' and kind == 'text/plain'
    assert data == 'lab\t1\nview\tdevice\n' + 'x' * 1200 + '\n', 'the body arrives whole'
    assert received[1][2] == 'lab\t1\nview\tclosed\n'
print('lab live ok')
