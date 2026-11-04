#include "podcast_player.h"
#include "podcast_catalog.h"
#include "internet_radio_transport.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <new>
// The decoder itself is built with the radio (internet_radio_transport.cpp); this only
// declares it, with the same options.
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "vendor/minimp3.h"

#ifndef PODCAST_PLAYER_HOST_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <mbedtls/ssl.h>
#endif

namespace podcast_player {
using internet_radio::detail::Body;
using internet_radio::detail::Headers;
using namespace detail;
namespace {
constexpr uint8_t kChannel = 2;
constexpr size_t kChunk = 4096, kScratch = 1024;
enum class Job : uint8_t { List, Stream, File, Save };
std::atomic<State> current{State::Idle};
std::atomic<Error> failure{Error::None};
std::atomic<bool> owned{false}, cancel{false}, ended{false}, wholeSave{false}, home{false}, lastHome{false};
std::atomic<Job> job{Job::List};
std::atomic<uint32_t> fromSeconds{0}, played{0}, rate{podcast::kRate}, written{0}, answer{0};
std::atomic<uint16_t> loudness{0};
portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;
// Written only while no job runs.
char bridgeHost[128] = "", bridgeToken[192] = "";
uint16_t bridgePort = 443;
const char* bridgeCa = nullptr;
char requested[512] = "", filePath[64] = "", partPath[64] = "";
uint32_t homeIp = 0;
uint16_t homePort = 0;
char memNote[48] = "";
void noteMemory(const char* what) {
  snprintf(memNote, sizeof(memNote), "%s %uK/%uK", what, unsigned(ESP.getFreeHeap() / 1024),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
}
char* listData = nullptr;
std::atomic<size_t> listLength{0};

void setState(State s) {
  portENTER_CRITICAL(&gate);
  if (!cancel.load()) current.store(s);
  portEXIT_CRITICAL(&gate);
}

// Buffered, bounded reads: no call here can wait longer than kIdleMs for a byte.
struct Reader {
  explicit Reader(WiFiClient& client) : tls(client) {}
  WiFiClient& tls;  // A WiFiClientSecure over HTTPS, a plain one at home.
  uint8_t buf[512];
  size_t pos = 0, count = 0;
  uint32_t last = millis(), idle = kIdleMs;
  bool ready() { return pos < count || tls.available() > 0; }
  int next() {
    for (;;) {
      if (cancel.load()) return -3;
      if (pos < count) return buf[pos++];
      const int avail = tls.available();
      if (avail > 0) {
        const int n = tls.read(buf, avail > int(sizeof(buf)) ? sizeof(buf) : size_t(avail));
        if (n > 0) { pos = 0; count = size_t(n); last = millis(); continue; }
      }
      if (!tls.connected() && tls.available() <= 0) return -1;
      if (millis() - last >= idle) return -2;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
};

Error fromStatus(unsigned status) {
  return status == 401 || status == 403 ? Error::NoBridge : status == 404 ? Error::NotFound :
         status == 503 || status == 429 ? Error::Busy : status == 502 ? Error::Unavailable : Error::Http;
}

// Opens the bridge, over HTTPS with the token or at home with the ticket in the path, asks for
// `requested`, and leaves r at the start of a 200 answer's body. Error::None with cancel set
// means it was stopped. The request and header lines share one small buffer on the heap, so
// this worker's stack can stay small.
Error openBridge(WiFiClient& client, Reader& r, Headers& h, bool text) {
  const bool atHome = home.load();
  if (atHome ? !homeIp || !homePort : !bridgeHost[0] || !bridgeCa) return Error::NoBridge;
  setState(State::Connecting);
  if (atHome) {
    const IPAddress ip(uint8_t(homeIp >> 24), uint8_t(homeIp >> 16), uint8_t(homeIp >> 8), uint8_t(homeIp));
    if (!client.connect(ip, homePort, 3000)) return Error::Connect;
  } else {
    auto& tls = static_cast<WiFiClientSecure&>(client);
    tls.setCACert(bridgeCa);
    tls.setHandshakeTimeout(15);
    if (!tls.connect(bridgeHost, bridgePort)) {
      char detail[32] = {};
      if (tls.lastError(detail, sizeof(detail)) != MBEDTLS_ERR_SSL_ALLOC_FAILED) return Error::Connect;
      noteMemory("TLS");
      return Error::NoMemory;
    }
  }
  if (cancel.load()) return Error::None;
  auto* buf = static_cast<char*>(heap_caps_malloc(kScratch, MALLOC_CAP_8BIT));
  if (!buf) { noteMemory("request"); return Error::NoMemory; }
  char host[140];
  if (atHome) snprintf(host, sizeof(host), "%u.%u.%u.%u:%u", unsigned(homeIp >> 24), unsigned((homeIp >> 16) & 255),
                       unsigned((homeIp >> 8) & 255), unsigned(homeIp & 255), unsigned(homePort));
  else if (bridgePort == 443) snprintf(host, sizeof(host), "%s", bridgeHost);
  else snprintf(host, sizeof(host), "%s:%u", bridgeHost, unsigned(bridgePort));
  int n = atHome ? snprintf(buf, kScratch, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: S1-ADV-Podcasts/1\r\n"
                            "Accept: audio/mpeg\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", requested, host)
                 : snprintf(buf, kScratch, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: S1-ADV-Podcasts/1\r\nAuthorization: Bearer %s\r\n"
                            "Accept: %s\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                            requested, host, bridgeToken, text ? "text/plain" : "audio/mpeg");
  Error e = Error::None;
  if (n <= 0 || size_t(n) >= kScratch) e = Error::Malformed;
  else if (client.write(reinterpret_cast<const uint8_t*>(buf), size_t(n)) != size_t(n)) e = Error::Connect;
  h = Headers{};
  h.text = text;
  r.pos = r.count = 0;
  r.last = millis();
  r.idle = kHeaderMs;  // The bridge answers only once the show's site has sent sound.
  size_t len = 0;
  bool cr = false;
  const uint32_t began = millis();
  while (e == Error::None && !h.done && !cancel.load()) {
    if (millis() - began > kHeaderMs) { e = Error::Timeout; break; }
    const int b = r.next();
    if (b < 0) { e = b == -3 ? Error::None : b == -2 ? Error::Timeout : Error::Connect; break; }
    if (cr) {
      if (b != '\n') { e = Error::Malformed; break; }
      buf[len] = 0;
      const auto got = h.line(buf);
      if (h.status) answer.store(h.status);
      if (got == internet_radio::Error::Http || got == internet_radio::Error::Redirect) { e = fromStatus(h.status); break; }
      if (got != internet_radio::Error::None) { e = Error::Malformed; break; }
      len = 0;
      cr = false;
    } else if (b == '\r') cr = true;
    else {
      if (b == '\n' || b == 0 || len >= 767) { e = Error::Malformed; break; }
      buf[len++] = char(b);
    }
  }
  free(buf);
  r.idle = kIdleMs;
  return e;
}

Error list(WiFiClient& client) {
  Reader r(client);
  Headers h;
  Error e = openBridge(client, r, h, true);
  if (e != Error::None || cancel.load()) return e;
  setState(State::Buffering);
  // As big as the answer says, which is usually a couple of KB; grown as it comes if it didn't.
  if (h.hasLength && h.length > kListBytes) return Error::TooBig;
  size_t room = h.hasLength ? size_t(h.length) : 2048;
  auto* data = static_cast<char*>(heap_caps_malloc(room + 1, MALLOC_CAP_8BIT));
  if (!data) { noteMemory("list"); return Error::NoMemory; }
  Body body(h);
  size_t n = 0;
  while (!cancel.load() && !body.done()) {
    const int b = r.next();
    if (b < 0) {
      if (b == -3) break;
      if (b == -2) e = Error::Timeout;
      else if (!body.complete()) e = Error::Malformed;
      break;
    }
    const int kind = body.feed(uint8_t(b));
    if (kind < 0) { e = Error::Malformed; break; }
    if (kind == 1) {
      if (n >= kListBytes) { e = Error::TooBig; break; }
      if (n >= room) {
        const size_t more = room * 2 > kListBytes ? kListBytes : room * 2;
        auto* bigger = static_cast<char*>(realloc(data, more + 1));
        if (!bigger) { noteMemory("list"); e = Error::NoMemory; break; }
        data = bigger;
        room = more;
      }
      data[n++] = char(b);
    }
  }
  if (e != Error::None || cancel.load()) { free(data); return e; }
  data[n] = 0;
  if (auto* fit = static_cast<char*>(realloc(data, n + 1))) data = fit;  // Only what it needs.
  listData = data;
  listLength.store(n);
  return Error::None;
}

// Where decoded audio comes from: the bridge's answer or a file on the card. fill adds what
// it has, waiting for more only when told to, and returns how much, kEnd at the end, or an
// error as a negative Error.
constexpr int kEnd = -1000;
struct Source {
  virtual int fill(uint8_t* to, size_t room, bool wait) = 0;
  virtual ~Source() = default;
};
struct NetSource : Source {
  Reader& r;
  Body body;
  NetSource(Reader& reader, const Headers& h) : r(reader), body(h) {}
  int fill(uint8_t* to, size_t room, bool wait) override {
    size_t n = 0;
    while (n < room && !body.done()) {
      if ((!wait || n) && !r.ready()) break;
      const int b = r.next();
      if (b < 0) {
        if (b == -3) return n ? int(n) : 0;
        if (b == -2) return n ? int(n) : -int(Error::Timeout);
        if (!body.complete()) return n ? int(n) : -int(Error::Connect);  // Cut off mid-answer.
        break;
      }
      const int kind = body.feed(uint8_t(b));
      if (kind < 0) return -int(Error::Malformed);
      if (kind == 1) to[n++] = uint8_t(b);
    }
    if (!n && body.done()) return kEnd;
    if (!n && !r.tls.connected() && !r.ready()) return body.complete() ? kEnd : -int(Error::Connect);
    return int(n);
  }
};
struct FileSource : Source {
  File& f;
  explicit FileSource(File& file) : f(file) {}
  int fill(uint8_t* to, size_t room, bool) override {
    const int n = f.read(to, room);
    return n > 0 ? n : n == 0 ? kEnd : -int(Error::Card);
  }
};

struct Decoder {
  mp3dec_t* dec = nullptr;
  uint8_t* input = nullptr;
  size_t size = 0;
  int16_t* pcm[2] = {nullptr, nullptr};
  bool alloc(size_t inputBytes) {
    size = inputBytes;
    dec = static_cast<mp3dec_t*>(heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_8BIT));
    input = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    for (auto*& slot : pcm) slot = static_cast<int16_t*>(heap_caps_malloc(kSlotSamples * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (!dec || !input || !pcm[0] || !pcm[1]) { noteMemory("player"); return false; }
    return true;
  }
  void release() { free(pcm[1]); free(pcm[0]); free(input); free(dec); *this = Decoder{}; }
};

// Plays from src until it ends. The first fill waits for a whole buffer, about a second, and
// after that it waits only when less than kLowWater is held.
Error play(Source& src, Decoder& d) {
  size_t used = 0;
  unsigned slot = 0, junk = 0;
  bool sounded = false, atEnd = false;
  uint32_t lastFrame = millis();
  mp3dec_init(d.dec);
  setState(State::Buffering);
  while (!cancel.load()) {
    // Two frames queued at most, so the older slot is free to reuse (as the radio does).
    const uint32_t wait = millis();
    while (M5Cardputer.Speaker.isPlaying(kChannel) >= 2 && !cancel.load()) {
      if (millis() - wait > 1500) return Error::Speaker;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (cancel.load()) break;
    if (!atEnd && used < d.size) {
      const int got = src.fill(d.input + used, d.size - used, sounded ? used < kLowWater : used < d.size);
      if (got == kEnd) atEnd = true;
      else if (got < 0) return Error(-got);
      else used += size_t(got);
    }
    if (cancel.load()) break;
    // A second's cushion before the first sound, so a slow start doesn't stutter.
    if (!sounded && !atEnd && used < d.size) continue;
    if (!used) {
      if (atEnd) {
        if (!sounded) return Error::Decode;  // It ended with never a frame in it.
        ended.store(true);
        return Error::None;
      }
      vTaskDelay(1);
      continue;
    }
    mp3dec_frame_info_t info{};
    // A look at the frame's header first: one that wouldn't fit a slot is never decoded into it.
    const int coming = mp3dec_decode_frame(d.dec, d.input, int(used), nullptr, &info);
    if (coming > 0 && size_t(coming) * size_t(info.channels) > kSlotSamples) return Error::Decode;
    const int samples = coming > 0 ? mp3dec_decode_frame(d.dec, d.input, int(used), d.pcm[slot], &info) : 0;
    if (info.frame_bytes < 0 || size_t(info.frame_bytes) > used) return Error::Decode;
    if (samples > 0) {
      if (info.layer != 3 || (info.channels != 1 && info.channels != 2) || info.hz < 8000 || info.hz > 48000 || samples > 1152) return Error::Decode;
      uint32_t sum = 0;
      const int count = samples * info.channels;
      for (int i = 0; i < count; i += 2) { const int v = d.pcm[slot][i]; sum += uint32_t(v < 0 ? -v : v); }
      loudness.store(uint16_t(sum / uint32_t((count + 1) / 2)));
      if (!M5Cardputer.Speaker.playRaw(d.pcm[slot], size_t(count), uint32_t(info.hz), info.channels == 2, 1, kChannel, false)) return Error::Speaker;
      slot ^= 1;
      sounded = true;
      junk = 0;
      lastFrame = millis();
      rate.store(uint32_t(info.hz));
      played.fetch_add(uint32_t(samples));
      setState(State::Playing);
    }
    size_t consumed = size_t(info.frame_bytes);
    if (!consumed && used == d.size) consumed = used - 2048;  // No frame in a full buffer: skip most of it.
    if (!samples) junk += unsigned(consumed);
    if (consumed) { used -= consumed; memmove(d.input, d.input + consumed, used); }
    if (junk > 65536 || millis() - lastFrame > kIdleMs + 5000) return Error::Decode;
    if (atEnd && !consumed) {  // What's left can't be a frame.
      if (!sounded) return Error::Decode;
      ended.store(true);
      return Error::None;
    }
    vTaskDelay(1);
  }
  return Error::None;
}

Error stream(WiFiClient& client, Decoder& d) {
  Reader r(client);
  Headers h;
  const Error e = openBridge(client, r, h, false);
  if (e != Error::None || cancel.load()) return e;
  // Only once the bridge has answered: TLS's buffers came first, so they get the room.
  if (!d.alloc(home.load() ? kInput : kInputTls)) return Error::NoMemory;
  NetSource src(r, h);
  return play(src, d);
}

Error playFile(Decoder& d) {
  File f = SD.open(filePath, FILE_READ);
  if (!f) return Error::Card;
  const uint32_t size = uint32_t(f.size()), offset = podcast::fileOffset(fromSeconds.load());
  if (offset >= size) { f.close(); ended.store(true); return Error::None; }
  if (offset && !f.seek(offset)) { f.close(); return Error::Card; }
  if (!d.alloc(kInput)) { f.close(); return Error::NoMemory; }
  FileSource src(f);
  const Error e = play(src, d);
  f.close();
  return e;
}

Error save(WiFiClient& client) {
  Reader r(client);
  Headers h;
  Error e = openBridge(client, r, h, false);
  if (e != Error::None || cancel.load()) return e;
  auto* chunk = static_cast<uint8_t*>(heap_caps_malloc(kChunk, MALLOC_CAP_8BIT));
  if (!chunk) { noteMemory("save"); return Error::NoMemory; }
  SD.remove(partPath);
  File f = SD.open(partPath, FILE_WRITE);
  if (!f) { free(chunk); return Error::Card; }
  setState(State::Saving);
  Body body(h);
  size_t n = 0;
  const auto flush = [&]() {
    if (!n) return true;
    if (f.write(chunk, n) != n) return false;
    written.fetch_add(uint32_t(n));
    n = 0;
    return true;
  };
  while (!cancel.load() && !body.done()) {
    const int b = r.next();
    if (b < 0) {
      if (b == -3) break;
      e = b == -2 ? Error::Timeout : body.complete() ? Error::None : Error::Connect;
      break;
    }
    const int kind = body.feed(uint8_t(b));
    if (kind < 0) { e = Error::Malformed; break; }
    if (kind == 1) {
      chunk[n++] = uint8_t(b);
      if (n == kChunk && !flush()) { e = Error::CardFull; break; }
    }
  }
  if (e == Error::None && !cancel.load() && !flush()) e = Error::CardFull;
  f.close();
  free(chunk);
  if (e == Error::None && !cancel.load() && written.load() < podcast::kBytesPerSecond) e = Error::Unavailable;
  if (e != Error::None || cancel.load()) { SD.remove(partPath); return e; }
  SD.remove(filePath);
  if (!SD.rename(partPath, filePath)) { SD.remove(partPath); return Error::Card; }
  wholeSave.store(true);
  return Error::None;
}

void worker(void*) {
  Error e = Error::None;
  const Job kind = job.load();
  Decoder d;
  if (kind == Job::File) e = playFile(d);
  else {
    WiFiClient* client = home.load() ? new (std::nothrow) WiFiClient() : new (std::nothrow) WiFiClientSecure();
    if (!client) { noteMemory("client"); e = Error::NoMemory; }
    else {
      e = kind == Job::List ? list(*client) : kind == Job::Save ? save(*client) : stream(*client, d);
      client->stop();
      delete client;
    }
  }
  current.store(State::Stopping);
  loudness.store(0);
  // The speaker may still be reading the last two frames: they're freed only once it's done.
  if (d.dec) while (M5Cardputer.Speaker.isPlaying(kChannel)) vTaskDelay(pdMS_TO_TICKS(5));
  d.release();
  portENTER_CRITICAL(&gate);
  if (cancel.load()) e = Error::None;
  failure.store(e);
  current.store(e == Error::None ? State::Idle : State::Error);
  owned.store(false);
  portEXIT_CRITICAL(&gate);
  vTaskDelete(nullptr);
}

void refuse(Error e) {
  portENTER_CRITICAL(&gate);
  if (!owned.load()) { failure.store(e); current.store(State::Error); }
  portEXIT_CRITICAL(&gate);
}

bool begin(Job kind, uint32_t from, bool atHome) {
  const uint32_t stack = kind == Job::Stream || kind == Job::File ? kPlayStack : kNetStack;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < stack) { noteMemory("worker"); refuse(Error::NoMemory); return false; }
  if (kind == Job::List) clearList();
  portENTER_CRITICAL(&gate);
  if (owned.load()) { portEXIT_CRITICAL(&gate); return false; }
  owned.store(true);
  cancel.store(false);
  home.store(atHome);
  if (kind == Job::Stream || kind == Job::File) lastHome.store(atHome);
  memNote[0] = 0;
  failure.store(Error::None);
  answer.store(0);
  job.store(kind);
  current.store(State::Connecting);
  if (kind == Job::Stream || kind == Job::File) {
    ended.store(false); played.store(0); fromSeconds.store(from); rate.store(podcast::kRate); loudness.store(0);
  }
  if (kind == Job::Save) { wholeSave.store(false); written.store(0); }
  portEXIT_CRITICAL(&gate);
  if (xTaskCreate(worker, "podcast", stack, nullptr, 2, nullptr) != pdPASS) {
    portENTER_CRITICAL(&gate);
    failure.store(Error::TaskFailed); current.store(State::Error); owned.store(false);
    portEXIT_CRITICAL(&gate);
    return false;
  }
  return true;
}
bool copyPath(char* out, size_t room, const char* in) {
  if (!in || !*in || strnlen(in, room) >= room) return false;
  strcpy(out, in);
  return true;
}
}  // namespace

void setBridge(const char* host, uint16_t port, const char* token, const char* rootCa) {
  if (owned.load()) return;
  podcast::copyText(bridgeHost, sizeof(bridgeHost), host ? host : "");
  podcast::copyText(bridgeToken, sizeof(bridgeToken), token ? token : "");
  bridgePort = port ? port : 443;
  bridgeCa = rootCa;
}
bool startList(const char* path) {
  if (owned.load()) return false;
  if (!copyPath(requested, sizeof(requested), path)) { refuse(Error::Malformed); return false; }
  return begin(Job::List, 0, false);
}
bool startStream(const char* path, uint32_t from) {
  if (owned.load()) return false;
  if (!copyPath(requested, sizeof(requested), path)) { refuse(Error::Malformed); return false; }
  return begin(Job::Stream, from, false);
}
bool startStreamHome(uint32_t ip, uint16_t port, const char* path, uint32_t from) {
  if (owned.load()) return false;
  if (!ip || !port || !copyPath(requested, sizeof(requested), path)) { refuse(Error::Malformed); return false; }
  homeIp = ip;
  homePort = port;
  return begin(Job::Stream, from, true);
}
bool startFile(const char* sdPath, uint32_t from) {
  if (owned.load()) return false;
  if (!copyPath(filePath, sizeof(filePath), sdPath)) { refuse(Error::Card); return false; }
  return begin(Job::File, from, false);
}
bool startSave(const char* path, const char* part, const char* sdPath, uint32_t) {
  if (owned.load()) return false;
  if (!copyPath(requested, sizeof(requested), path)) { refuse(Error::Malformed); return false; }
  if (!copyPath(partPath, sizeof(partPath), part) || !copyPath(filePath, sizeof(filePath), sdPath)) { refuse(Error::Card); return false; }
  return begin(Job::Save, 0, false);
}
bool startSaveHome(uint32_t ip, uint16_t port, const char* path, const char* part, const char* sdPath, uint32_t) {
  if (owned.load()) return false;
  if (!ip || !port || !copyPath(requested, sizeof(requested), path)) { refuse(Error::Malformed); return false; }
  if (!copyPath(partPath, sizeof(partPath), part) || !copyPath(filePath, sizeof(filePath), sdPath)) { refuse(Error::Card); return false; }
  homeIp = ip;
  homePort = port;
  return begin(Job::Save, 0, true);
}
const char* memoryNote() { return owned.load() ? "" : memNote; }
bool overHome() { return lastHome.load(); }
void stop() {
  portENTER_CRITICAL(&gate);
  if (owned.load()) { cancel.store(true); current.store(State::Stopping); }
  portEXIT_CRITICAL(&gate);
}
bool busy() { return owned.load(); }
State state() { return current.load(); }
Error error() { return failure.load(); }
unsigned httpStatus() { return answer.load(); }
bool playing() { return owned.load() && (job.load() == Job::Stream || job.load() == Job::File); }
bool saving() { return owned.load() && job.load() == Job::Save; }
bool listing() { return owned.load() && job.load() == Job::List; }
const char* listText(size_t& length) {
  if (listing() || !listData) { length = 0; return nullptr; }
  length = listLength.load();
  return listData;
}
void clearList() {
  portENTER_CRITICAL(&gate);
  if (owned.load() && job.load() == Job::List) { portEXIT_CRITICAL(&gate); return; }
  char* gone = listData;
  listData = nullptr;
  listLength.store(0);
  portEXIT_CRITICAL(&gate);
  free(gone);
}
uint32_t position() {
  const uint32_t hz = rate.load();
  return fromSeconds.load() + (hz ? played.load() / hz : 0);
}
bool reachedEnd() { return !playing() && ended.load(); }
uint32_t savedBytes() { return written.load(); }
bool saveDone() { return !saving() && wholeSave.load(); }
uint16_t level() { return current.load() == State::Playing ? loudness.load() : 0; }
const char* errorText() {
  switch (failure.load()) {
    case Error::None: return nullptr;
    case Error::NoMemory: return "Not enough memory";
    case Error::TaskFailed: return "Player unavailable";
    case Error::NoBridge: return "The bridge refused";
    case Error::Connect: return "Couldn't reach the bridge";
    case Error::Timeout: return "The bridge took too long";
    case Error::Http: return "The bridge said no";
    case Error::Busy: return "The bridge is busy";
    case Error::NotFound: return "Not found any more";
    case Error::Unavailable: return "The show's site didn't answer";
    case Error::Malformed: return "Odd answer from the bridge";
    case Error::TooBig: return "List too long";
    case Error::Decode: return "Couldn't play it";
    case Error::Speaker: return "Speaker unavailable";
    case Error::Card: return "SD card problem";
    case Error::CardFull: return "The SD card is full";
  }
  return "Player error";
}
}  // namespace podcast_player
