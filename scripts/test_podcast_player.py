#!/usr/bin/env python3
"""The podcast player's actual worker against a fake bridge, card, scheduler and speaker: lists,
streaming from a second, playing a saved file from a second, saving, the cushion before the
first sound, stalls, cancelling, and every failure the screen has words for. ASan/UBSan."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
production = (src / 'podcast_player.cpp').read_text()
# The worker reads the bridge over TLS with the device's own root certificate, never plain HTTP.
assert 'tls.setCACert(bridgeCa);' in production and '::send(' not in production
assert 'Authorization: Bearer %s' in production
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
constexpr int MALLOC_CAP_8BIT = 1;
int allocateAfter = -1;  // Fail the nth allocation from now, or never.
void* heap_caps_malloc(size_t n, int) { if (allocateAfter == 0) { allocateAfter = -1; return nullptr; } if (allocateAfter > 0) --allocateAfter; return malloc(n); }
size_t heap_caps_get_largest_free_block(int) { return 100000; }
struct { int getFreeHeap() { return 150000; } } ESP;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
constexpr int pdPASS = 1;
bool createFails = false;
uint32_t lastStack = 0;
void (*scheduled)(void*) = nullptr;
int xTaskCreate(void (*f)(void*), const char*, uint32_t n, void*, int, void*) {
  assert(n == 24576 || n == 16384); lastStack = n;
  if (createFails) return 0;
  assert(!scheduled); scheduled = f; return pdPASS;
}
void vTaskDelete(void*) {}
constexpr int MBEDTLS_ERR_SSL_ALLOC_FAILED = -0x7F00;

// The bridge: one reply per connection, with an optional stall partway through.
std::string reply, request;
size_t stallAt = SIZE_MAX; uint32_t stallMs = 0, stallUntil = 0; bool stalled = false;
bool connectFails = false, tlsNoMemory = false;
std::function<void()> readHook;
size_t firstSoundAt = 0;  // How much of the reply had been read when the first frame was queued.
struct WiFiClientSecure;
WiFiClientSecure* lastTls = nullptr;
struct WiFiClientSecure {
  WiFiClientSecure() { lastTls = this; }
  ~WiFiClientSecure() { if (lastTls == this) lastTls = nullptr; }
  size_t pos = 0; bool open = false; const char* ca = nullptr;
  void setCACert(const char* c) { ca = c; }
  void setHandshakeTimeout(unsigned long) {}
  int connect(const char* host, uint16_t port) {
    assert(ca && std::string(ca) == "ROOT CA" && std::string(host) == "bridge.example" && port == 10000);
    pos = 0; stalled = false; open = !connectFails && !tlsNoMemory; return open;
  }
  int lastError(char*, size_t) { return tlsNoMemory ? MBEDTLS_ERR_SSL_ALLOC_FAILED : -1; }
  size_t write(const uint8_t* p, size_t n) { request.assign((const char*)p, n); return n; }
  size_t limit() {
    if (pos < stallAt) return stallAt;
    if (!stalled) { stalled = true; stallUntil = clockMs + stallMs; }
    return clockMs >= stallUntil ? reply.size() : stallAt;
  }
  int available() { return open ? int(std::min(reply.size(), limit()) - pos) : 0; }
  uint8_t connected() { return open && pos < reply.size(); }
  int read(uint8_t* p, size_t n) {
    n = std::min(n, std::min(reply.size(), limit()) - pos); memcpy(p, reply.data() + pos, n); pos += n;
    if (readHook) readHook();
    return int(n);
  }
  void stop() { open = false; }
};
// The card: files in memory, and a write that can fail.
std::map<std::string, std::string> card;
bool cardFull = false;
constexpr const char* FILE_READ = "r";
constexpr const char* FILE_WRITE = "w";
struct File {
  std::string path; size_t pos = 0; bool ok = false, writing = false;
  explicit operator bool() const { return ok; }
  int read(uint8_t* p, size_t n) { const std::string& s = card[path]; n = std::min(n, s.size() - pos); memcpy(p, s.data() + pos, n); pos += n; return int(n); }
  size_t write(const uint8_t* p, size_t n) { assert(writing); if (cardFull) return 0; card[path].append((const char*)p, n); return n; }
  bool seek(uint32_t at) { if (at > card[path].size()) return false; pos = at; return true; }
  size_t size() { return card[path].size(); }
  void close() { ok = false; }
};
struct {
  File open(const char* p, const char* mode) {
    File f; f.path = p; f.writing = !strcmp(mode, "w");
    if (f.writing) { card[p].clear(); f.ok = true; } else f.ok = card.count(p) > 0;
    return f;
  }
  bool remove(const char* p) { return card.erase(p) > 0; }
  bool rename(const char* a, const char* b) { if (!card.count(a)) return false; card[b] = card[a]; card.erase(a); return true; }
  bool exists(const char* p) { return card.count(p) > 0; }
} SD;

uint64_t hashPcm(const int16_t* p, size_t n) { uint64_t h = 1469598103934665603ULL; for (size_t i = 0; i < n; ++i) { h ^= uint16_t(p[i]); h *= 1099511628211ULL; } return h; }
struct Frame { const int16_t* data; size_t n; uint64_t hash; uint32_t end; };
struct FakeSpeaker {
  std::deque<Frame> queue; unsigned rendered = 0; size_t maxQueued = 0; uint32_t quietMs = 0, lastEnd = 0; uint32_t hz = 0;
  size_t isPlaying(uint8_t channel) { assert(channel == 2); return queue.size(); }
  bool playRaw(const int16_t* p, size_t n, uint32_t rate, bool stereo, int repeat, uint8_t channel, bool stop) {
    assert(channel == 2 && !stop && repeat == 1 && queue.size() < 2 && !stereo && n > 0 && n <= 1152);
    for (auto& f : queue) assert(f.data != p);  // Never the slot the speaker is still reading.
    if (!rendered && queue.empty() && !firstSoundAt) firstSoundAt = lastTls ? lastTls->pos : 1;
    hz = rate;
    const uint32_t start = queue.empty() ? clockMs : queue.back().end;
    if (rendered && start > lastEnd) quietMs += start - lastEnd;  // A gap the listener would hear.
    const uint32_t end = start + uint32_t(n * 1000 / rate);
    queue.push_back({p, n, hashPcm(p, n), end}); lastEnd = end;
    maxQueued = std::max(maxQueued, queue.size());
    return true;
  }
  void advance() { while (!queue.empty() && clockMs >= queue.front().end) { auto& f = queue.front(); assert(f.hash == hashPcm(f.data, f.n)); queue.pop_front(); ++rendered; } }
};
struct { FakeSpeaker Speaker; } M5Cardputer;
void vTaskDelay(unsigned n) { clockMs += n; M5Cardputer.Speaker.advance(); }
#include "podcast_player.cpp"

using namespace podcast_player;
void execute() {
  assert(scheduled); auto f = scheduled; scheduled = nullptr; f(nullptr);
  assert(!busy() && M5Cardputer.Speaker.queue.empty());
}
std::string chunks(const std::string& s, size_t size = 333) {
  std::string w;
  for (size_t at = 0; at < s.size(); at += size) { auto b = s.substr(at, size); char h[16]; snprintf(h, sizeof(h), "%zx\r\n", b.size()); w += h; w += b; w += "\r\n"; }
  return w + "0\r\n\r\n";
}
const std::string kAudio = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nTransfer-Encoding: chunked\r\n\r\n";
std::string text(const std::string& body, const char* status = "200 OK", const char* type = "text/plain; charset=utf-8") {
  return std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
void reset() { M5Cardputer.Speaker = FakeSpeaker{}; firstSoundAt = 0; stallAt = SIZE_MAX; stallMs = 0; readHook = {}; }
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream in(argv[1], std::ios::binary);
  const std::string mp3((std::istreambuf_iterator<char>(in)), {});
  assert(mp3.size() > 20000);
  // Nothing goes anywhere before the bridge is known.
  assert(startList("/api/cardputer/podcasts/search?kind=podcast&q=x")); execute();
  assert(error() == Error::NoBridge && state() == State::Error);
  setBridge("bridge.example", 10000, "tok-123", "ROOT CA");
  using Clock = uint32_t;

  // A list: asked with the token, read whole, kept after the worker has gone.
  const std::string lines = "show\tp1\t3\tHistory & Tea\tAnn\n";
  reply = text(lines);
  assert(startList("/api/cardputer/podcasts/search?kind=podcast&q=tea") && busy() && listing() && lastStack == 16384);
  assert(!startList("/x") && !startStream("/y", 0));  // One job at a time.
  size_t length = 9;
  assert(!listText(length) && !length);  // Not while it's being read.
  execute();
  assert(error() == Error::None && httpStatus() == 200);
  assert(request.rfind("GET /api/cardputer/podcasts/search?kind=podcast&q=tea HTTP/1.1\r\n", 0) == 0);
  assert(request.find("Host: bridge.example:10000\r\n") != std::string::npos && request.find("Authorization: Bearer tok-123\r\n") != std::string::npos);
  assert(request.find("Accept: text/plain\r\n") != std::string::npos && request.find("Connection: close\r\n") != std::string::npos);
  const char* got = listText(length);
  assert(got && length == lines.size() && std::string(got, length) == lines);
  clearList(); assert(!listText(length) && !length);
  reply = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\n" + chunks(lines + lines, 7);
  assert(startList("/l")); execute(); got = listText(length);
  assert(error() == Error::None && length == 2 * lines.size());
  // A new list replaces the old one; a failed one leaves none.
  struct Case { std::string wire; Error want; };
  const std::vector<Case> lists = {
    {text("{}", "404 Not Found", "application/json"), Error::NotFound}, {text("{}", "401 Unauthorized", "application/json"), Error::NoBridge},
    {text("{}", "503 Busy", "application/json"), Error::Busy}, {text("{}", "502 Bad Gateway", "application/json"), Error::Unavailable},
    {text("{}", "500 Oops", "application/json"), Error::Http}, {text("[]", "200 OK", "application/json"), Error::Malformed},
    {text(std::string(13000, 'x')), Error::TooBig}, {"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 50\r\n\r\nshort", Error::Malformed},
    {"garbage\r\n\r\n", Error::Malformed}};
  for (const auto& c : lists) {
    reply = c.wire; assert(startList("/l")); execute();
    assert(error() == c.want && state() == State::Error && !listText(length));
  }
  connectFails = true; assert(startList("/l")); execute(); assert(error() == Error::Connect); connectFails = false;
  tlsNoMemory = true; assert(startList("/l")); execute(); assert(error() == Error::NoMemory && std::string(errorText()) == "Not enough memory"); tlsNoMemory = false;
  allocateAfter = 0; reply = text(lines); assert(startList("/l")); execute(); assert(error() == Error::NoMemory);
  createFails = true; assert(!startList("/l") && !busy() && error() == Error::TaskFailed); createFails = false;
  assert(!startList("") && error() == Error::Malformed);
  assert(!startList(std::string(600, 'x').c_str()));
  // The bridge can take a while to answer, but not for ever.
  reply = text(lines); stallAt = 0; stallMs = 30000; assert(startList("/l")); execute(); assert(error() == Error::None); reset();
  reply = text(lines); stallAt = 0; stallMs = 45000; assert(startList("/l")); execute(); assert(error() == Error::Timeout); reset();

  // Streaming: from second 100, a second's cushion first, then every frame on two safe slots.
  reset();
  reply = kAudio + chunks(mp3);
  const Clock began = clockMs;
  assert(startStream("/api/cardputer/podcasts/audio?show=p1&ep=618afa868312&t=100", 100) && playing() && lastStack == 24576);
  assert(position() == 100);
  execute();
  assert(error() == Error::None && reachedEnd() && !playing());
  assert(request.find("Accept: audio/mpeg\r\n") != std::string::npos);
  auto& sp = M5Cardputer.Speaker;
  const uint32_t seconds = uint32_t(mp3.size() / 6000);
  assert(sp.hz == 32000 && sp.maxQueued == 2 && sp.rendered > 25 * seconds);
  assert(position() >= 100 + seconds - 1 && position() <= 100 + seconds + 1);
  assert(firstSoundAt >= kAudio.size() + kInput);  // Not a sound before a whole buffer.
  assert(sp.quietMs == 0);  // And no gap after it.
  assert(clockMs - began >= seconds * 1000);  // It plays in real time, not as fast as it reads.
  // A stall shorter than the idle limit plays on from what's held; a long one is a timeout.
  reset(); reply = kAudio + chunks(mp3); stallAt = reply.size() / 2; stallMs = 15000;
  assert(startStream("/a", 0)); execute(); assert(error() == Error::None && reachedEnd());
  reset(); reply = kAudio + chunks(mp3); stallAt = reply.size() / 2; stallMs = 40000;
  assert(startStream("/a", 0)); execute(); assert(error() == Error::Timeout && !reachedEnd() && position() > 0 && position() < seconds);
  // Stopped partway: no error, not the end, and the place is kept for the next play.
  reset(); reply = kAudio + chunks(mp3);
  readHook = [&]() { if (M5Cardputer.Speaker.rendered > 60) podcast_player::stop(); };
  assert(startStream("/a", 7)); execute(); reset();
  assert(error() == Error::None && state() == State::Idle && !reachedEnd() && position() > 7 && position() < 7 + seconds);
  const uint32_t pausedAt = position();
  assert(pausedAt == position());  // Still there after the worker has gone.
  // Cut off mid-answer is a failure, not the end of the episode.
  reply = kAudio + chunks(mp3).substr(0, mp3.size() * 2 / 3); assert(startStream("/a", 0)); execute();
  assert(error() == Error::Connect && !reachedEnd());
  // Not MP3, or no sound at all.
  reply = kAudio + chunks(std::string(9000, 'z')); assert(startStream("/a", 0)); execute(); assert(error() == Error::Decode);
  reply = text("hello"); assert(startStream("/a", 0)); execute(); assert(error() == Error::Malformed);
  reply = text("{}", "502 Bad Gateway", "application/json"); assert(startStream("/a", 0)); execute();
  assert(error() == Error::Unavailable && std::string(errorText()) == "The show's site didn't answer");
  // Out of memory after TLS: the decoder's buffers are asked for only once the bridge answers.
  reset(); reply = kAudio + chunks(mp3); allocateAfter = 0; assert(startStream("/a", 0)); execute();
  assert(error() == Error::NoMemory && !sp.rendered && request.find("GET /a ") == 0);

  // Saving: every byte to a part file, renamed whole; nothing left behind otherwise.
  reset(); card.clear();
  reply = kAudio + chunks(mp3, 4000);
  assert(startSave("/api/cardputer/podcasts/audio?show=p1&ep=618afa868312&t=0", "/podcasts/files/618afa868312.part", "/podcasts/files/618afa868312.mp3", 0));
  assert(saving() && !playing() && lastStack == 16384);
  execute();
  assert(error() == Error::None && saveDone() && savedBytes() == mp3.size());
  assert(card.count("/podcasts/files/618afa868312.mp3") && card["/podcasts/files/618afa868312.mp3"] == mp3 && !card.count("/podcasts/files/618afa868312.part"));
  assert(!M5Cardputer.Speaker.rendered);  // Saving makes no sound.
  card["/podcasts/files/aaaaaaaaaaaa.mp3"] = "old";
  cardFull = true; assert(startSave("/a", "/podcasts/files/aaaaaaaaaaaa.part", "/podcasts/files/aaaaaaaaaaaa.mp3", 0)); execute(); cardFull = false;
  assert(error() == Error::CardFull && !saveDone() && !card.count("/podcasts/files/aaaaaaaaaaaa.part") && card["/podcasts/files/aaaaaaaaaaaa.mp3"] == "old");
  reply = kAudio + chunks(mp3).substr(0, mp3.size() / 2);
  assert(startSave("/a", "/p.part", "/p.mp3", 0)); execute(); assert(error() == Error::Connect && !card.count("/p.part") && !card.count("/p.mp3"));
  reply = kAudio + chunks("");
  assert(startSave("/a", "/p.part", "/p.mp3", 0)); execute(); assert(error() == Error::Unavailable && !card.count("/p.mp3"));
  reply = kAudio + chunks(mp3); readHook = [&]() { if (savedBytes() > 8000) podcast_player::stop(); };
  assert(startSave("/a", "/p.part", "/p.mp3", 0)); execute(); readHook = {};
  assert(error() == Error::None && !saveDone() && !card.count("/p.part") && !card.count("/p.mp3"));
  assert(!startSave("/a", "", "/p.mp3", 0) && error() == Error::Card);

  // Playing the saved file from second 1: from its frame, through to the end, no network.
  reset(); connectFails = true; request.clear();
  assert(startFile("/podcasts/files/618afa868312.mp3", 1) && playing() && lastStack == 24576);
  execute(); connectFails = false;
  assert(error() == Error::None && reachedEnd() && request.empty());
  assert(position() >= seconds - 1 && position() <= seconds + 1);
  const unsigned fromOne = M5Cardputer.Speaker.rendered;
  reset(); assert(startFile("/podcasts/files/618afa868312.mp3", 0)); execute();
  assert(M5Cardputer.Speaker.rendered >= fromOne + 26 && M5Cardputer.Speaker.rendered <= fromOne + 30);  // A second is 27.8 frames.
  // Past its end is the end at once; a file that isn't there is a card problem.
  reset(); assert(startFile("/podcasts/files/618afa868312.mp3", 99999)); execute(); assert(error() == Error::None && reachedEnd() && !M5Cardputer.Speaker.rendered);
  assert(startFile("/podcasts/files/nothere.mp3", 0)); execute(); assert(error() == Error::Card && std::string(errorText()) == "SD card problem");
  readHook = {};
  // The bridge isn't changed under a running job.
  reply = text(lines); assert(startList("/l")); setBridge("elsewhere.example", 1, "other", "X"); execute(); assert(error() == Error::None);
  puts("PASS podcast player: lists, streaming and saved files from a second, saving, the cushion, stalls, stops and failures (ASan/UBSan)");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-podcast-player-') as d:
    p = Path(d); (p / 'test.cpp').write_text(code)
    # What the bridge sends: 32 kHz mono at 48 kbit/s, every frame the same size, no tags.
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=330:duration=4:sample_rate=44100',
                    '-ac', '1', '-ar', '32000', '-c:a', 'libmp3lame', '-b:a', '48k', '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', str(p / 'test.mp3')], check=True)
    data = (p / 'test.mp3').read_bytes()
    assert len(data) % 216 == 0 and all(data[i] == 0xFF for i in range(0, len(data), 216)), 'every frame 216 bytes'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-DPODCAST_PLAYER_HOST_TEST', '-DINTERNET_RADIO_HOST_TEST', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(src), str(p / 'test.cpp'), str(src / 'internet_radio_transport.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test'), str(p / 'test.mp3')], check=True, timeout=120)
