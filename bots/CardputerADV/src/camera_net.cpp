#include "camera_net.h"
#include "camera_catalog.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

namespace camera_net {
namespace {
constexpr uint32_t kStack = 10240;     // TLS's handshake runs on it; plain HTTP needs far less.
// At home it asks a little above the main loop, for live view's frame rate. Over HTTPS it runs beside
// the main loop. It once dropped to idle priority, because a handshake is seconds of sums and a core
// whose idle task can't run for 5 s is restarted by the task watchdog; but sharing core 0 with the
// idle task, a handshake through Let's Encrypt's four-certificate chain took over 25 s and ran out its
// 15 (Skywatch, 0.2.335), where the same handshake above idle takes about 6. Now core 0's idle task is
// let off the watchdog for the handshake instead (tlsConnect).
constexpr UBaseType_t kHomePriority = 2, kHttpsPriority = 1;
constexpr uint32_t kIdleMs = 10000;    // A body that stops for this long has failed.
constexpr size_t kLineMax = 384;
constexpr size_t kLineRoom = kLineMax + 256;  // The request, then each line of the answer's head.
// An HTTPS connection stays open this long after its last answer, so the next of a run of requests
// (the wall's list and pictures) skips the handshake: seconds, and 40 KB taken and given back.
constexpr uint32_t kTlsIdleMs = 3000;

enum class Phase : uint8_t { Idle, Queued, Running, Done };
std::atomic<Phase> phase{Phase::Idle};
std::atomic<bool> cancelled{false}, stopping{false}, lastHome{false};
std::atomic<Error> failure{Error::None};
std::atomic<unsigned> answer{0};
std::atomic<uint32_t> took{0};
TaskHandle_t worker = nullptr;
portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;

// Set while no request runs.
char bridgeHost[128] = "", bridgeToken[192] = "";
uint16_t bridgePort = 443;
const char* bridgeCa = nullptr;
char requested[400] = "";
bool wantHome = false;
uint32_t homeIp = 0, maxBody = 0, waitFor = 0;
uint16_t homePort = 0;
// Kept by the worker between requests: the home connection, and where it goes; or the HTTPS one.
WiFiClient* kept = nullptr;
uint32_t keptIp = 0;
uint16_t keptPort = 0;
WiFiClientSecure* keptTls = nullptr;
uint32_t keptTlsAt = 0;
std::atomic<bool> tlsHeld{false};
char* line = nullptr;                  // The worker's, for its whole life: no heap taken per request.
// The caller's fixed buffers (reserve), and the one this request reads into, or -1.
uint8_t* slots[kSlots] = {};
size_t slotSize[kSlots] = {};
bool slotDropLater[kSlots] = {};       // Released while a request wrote it: freed as that one ends.
int intoSlot = -1;
// The answer, the caller's once Done.
uint8_t* body = nullptr;
size_t bodyLength = 0;
bool bodyInSlot = false;
char memNote[48] = "";
char failNote[48] = "";
char lastClip[40] = "";

// --- Lent memory (lend) ---
// mbedTLS's own allocator, which every other TLS user keeps: the IDF's, in internal memory.
extern "C" void* esp_mbedtls_mem_calloc(size_t n, size_t size);
extern "C" void esp_mbedtls_mem_free(void* p);
// Where the lent memory is, while it is: a piece inside it goes back to it.
std::atomic<uint8_t*> lentStart{nullptr};
size_t lentBytes = 0, lentFree = 0;
multi_heap_handle_t lentHeap = nullptr;
std::atomic<bool> lentTaking{false};  // New pieces made on the worker go into it.
std::atomic<int> lentPieces{0};
size_t lentPeakBytes = 0;
bool allocatorSet = false;

void* tlsCalloc(size_t n, size_t size) {
  // Only the worker's, so a TLS connection on another task (the inbox, voice) never lands in it.
  if (lentTaking.load() && worker && xTaskGetCurrentTaskHandle() == worker && size && n <= SIZE_MAX / size) {
    if (void* p = multi_heap_malloc(lentHeap, n * size)) {
      memset(p, 0, n * size);
      lentPieces.fetch_add(1);
      const size_t used = lentFree - multi_heap_free_size(lentHeap);
      if (used > lentPeakBytes) lentPeakBytes = used;
      return p;
    }
  }
  return esp_mbedtls_mem_calloc(n, size);
}
void tlsFree(void* p) {
  if (!p) return;
  uint8_t* start = lentStart.load();
  if (start && static_cast<uint8_t*>(p) >= start && static_cast<uint8_t*>(p) < start + lentBytes) {
    multi_heap_free(lentHeap, p);
    lentPieces.fetch_sub(1);
    return;
  }
  esp_mbedtls_mem_free(p);
}

void noteMemory(const char* what) {
  snprintf(memNote, sizeof(memNote), "%s %uK/%uK", what, unsigned(ESP.getFreeHeap() / 1024),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
}
void noteFailure(const char* how, int code, uint32_t ms = 0) {
  if (ms) {
    snprintf(failNote, sizeof(failNote), "%s %d %ums %uK/%uK", how, code, unsigned(ms), unsigned(ESP.getFreeHeap() / 1024),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    return;
  }
  snprintf(failNote, sizeof(failNote), "%s %d %uK/%uK", how, code, unsigned(ESP.getFreeHeap() / 1024),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
}
// mbedTLS's ways of saying it ran out of memory, beyond the TLS layer's own: a certificate, a
// number, a key or a cipher that couldn't be made is memory too, not an unreachable bridge.
bool tlsOutOfMemory(int code) {
  switch (code) {
    case MBEDTLS_ERR_SSL_ALLOC_FAILED: case -0x2880: /* X509 */ case -0x0010: /* MPI */ case -0x3F80: /* PK */
    case -0x006A: /* ASN1 */ case -0x6180: /* cipher */ case -0x4D80: /* ECP */ case -0x5180: /* MD */
      return true;
    default:
      return false;
  }
}
void dropKept() {
  if (!kept) return;
  kept->stop();
  delete kept;
  kept = nullptr;
}
void dropTls() {
  if (!keptTls) return;
  keptTls->stop();
  delete keptTls;
  keptTls = nullptr;
  tlsHeld.store(false);
}
Error fromStatus(unsigned s) {
  if (s == 200 || s == 204) return Error::None;
  if (s == 401 || s == 403) return Error::NoBridge;
  if (s == 404) return Error::NotFound;
  if (s == 409) return Error::Conflict;
  if (s == 429 || s == 503 || s == 504) return Error::Busy;
  return Error::Http;
}

// A byte, or -1 closed, -2 out of time, -3 cancelled. Reads in small pieces.
struct Reader {
  WiFiClient& c;
  uint8_t buf[256];
  size_t pos = 0, count = 0;
  uint32_t last = millis(), idle = kIdleMs;
  explicit Reader(WiFiClient& client) : c(client) {}
  int next() {
    for (;;) {
      if (cancelled.load()) return -3;
      if (pos < count) return buf[pos++];
      const int avail = c.available();
      if (avail > 0) {
        const int n = c.read(buf, avail > int(sizeof(buf)) ? sizeof(buf) : size_t(avail));
        if (n > 0) { pos = 0; count = size_t(n); last = millis(); continue; }
      }
      if (!c.connected() && c.available() <= 0) return -1;
      if (millis() - last >= idle) return -2;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  // The rest of a body straight into out, in the client's own reads.
  int into(uint8_t* out, size_t want) {
    size_t got = 0;
    while (got < want && pos < count) out[got++] = buf[pos++];
    while (got < want) {
      if (cancelled.load()) return -3;
      const int avail = c.available();
      if (avail > 0) {
        const size_t room = want - got;
        const int n = c.read(out + got, size_t(avail) < room ? size_t(avail) : room);
        if (n > 0) { got += size_t(n); last = millis(); continue; }
      }
      if (!c.connected() && c.available() <= 0) return -1;
      if (millis() - last >= idle) return -2;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    return 0;
  }
};

// One request on a connected client. `fresh` says the connection is new, which decides whether
// a failure before any answer is worth one more try on a new one.
Error exchange(WiFiClient& client, bool home, bool& anyAnswer, bool& reusable) {
  anyAnswer = false;
  reusable = false;
  char host[140];
  if (home) snprintf(host, sizeof(host), "%u.%u.%u.%u:%u", unsigned(homeIp >> 24), unsigned((homeIp >> 16) & 255),
                     unsigned((homeIp >> 8) & 255), unsigned(homeIp & 255), unsigned(homePort));
  else if (bridgePort == 443) snprintf(host, sizeof(host), "%s", bridgeHost);
  else snprintf(host, sizeof(host), "%s:%u", bridgeHost, unsigned(bridgePort));
  if (!line) { noteMemory("request"); return Error::NoMemory; }
  const int n = home
    ? snprintf(line, kLineRoom, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: S1-ADV-Cameras/1\r\nAccept: */*\r\n"
               "Connection: keep-alive\r\n\r\n", requested, host)
    : snprintf(line, kLineRoom, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: S1-ADV-Cameras/1\r\nAuthorization: Bearer %s\r\n"
               "Accept: */*\r\nConnection: keep-alive\r\n\r\n", requested, host, bridgeToken);
  if (n <= 0 || size_t(n) >= kLineRoom) return Error::Malformed;
  if (client.write(reinterpret_cast<const uint8_t*>(line), size_t(n)) != size_t(n)) return Error::Connect;
  Reader r(client);
  r.idle = waitFor;  // The bridge may take a while to start: a still wakes a camera.
  cameras::Response h;
  size_t len = 0;
  bool cr = false;
  Error e = Error::None;
  while (!h.done) {
    const int b = r.next();
    if (b < 0) { e = b == -3 ? Error::None : b == -2 ? Error::Timeout : Error::Connect; break; }
    anyAnswer = true;
    r.idle = kIdleMs;
    if (cr) {
      if (b != '\n') { e = Error::Malformed; break; }
      line[len] = 0;
      if (!h.line(line)) { e = Error::Malformed; break; }
      if (h.status) answer.store(h.status);
      len = 0;
      cr = false;
    } else if (b == '\r') cr = true;
    else if (b == '\n' || len >= kLineMax) { e = Error::Malformed; break; }
    else line[len++] = char(b);
  }
  if (e != Error::None || cancelled.load()) return e;
  memcpy(lastClip, h.clip, sizeof(lastClip));
  if (h.chunked) return Error::Malformed;  // The bridge always says how long.
  const bool bodyless = h.status == 204 || h.status == 304;
  if (!bodyless && !h.hasLength) return Error::Malformed;
  const size_t length = bodyless ? 0 : h.length;
  // An error's few words are kept too, so the screen can say which.
  const size_t limit = h.status == 200 ? maxBody : 256;
  if (length > limit) return Error::TooBig;
  // A 200 for a request with a slot goes into it: no heap taken. Anything else, as big as it is.
  const bool toSlot = h.status == 200 && intoSlot >= 0 && slots[intoSlot];
  if (toSlot && length + 1 > slotSize[intoSlot]) return Error::TooBig;
  if (length) {
    auto* data = toSlot ? slots[intoSlot] : static_cast<uint8_t*>(heap_caps_malloc(length + 1, MALLOC_CAP_8BIT));
    if (!data) { noteMemory("answer"); return Error::NoMemory; }
    const int got = r.into(data, length);
    if (got) { if (!toSlot) free(data); return got == -3 ? Error::None : got == -2 ? Error::Timeout : Error::Connect; }
    data[length] = 0;
    body = data;
    bodyLength = length;
    bodyInSlot = toSlot;
  }
  reusable = !h.close;
  return fromStatus(h.status);
}

Error runHome() {
  dropTls();
  for (int attempt = 0; attempt < 2; ++attempt) {
    const bool reuse = kept && keptIp == homeIp && keptPort == homePort && kept->connected();
    if (!reuse) {
      dropKept();
      kept = new (std::nothrow) WiFiClient();
      if (!kept) { noteMemory("client"); return Error::NoMemory; }
      const IPAddress ip(uint8_t(homeIp >> 24), uint8_t(homeIp >> 16), uint8_t(homeIp >> 8), uint8_t(homeIp));
      if (!kept->connect(ip, homePort, 3000)) { noteFailure("home", -1); dropKept(); return Error::Connect; }
      kept->setNoDelay(true);
      keptIp = homeIp;
      keptPort = homePort;
    }
    bool anyAnswer = false, reusable = false;
    const Error e = exchange(*kept, true, anyAnswer, reusable);
    if (!reusable || e == Error::Malformed || e == Error::Timeout || e == Error::Connect) dropKept();
    // A kept connection the bridge had already closed fails before any answer: once more on a new one.
    if (reuse && !anyAnswer && e == Error::Connect && !cancelled.load()) continue;
    return e;
  }
  return Error::Connect;
}

// The handshake: four signatures to check in sums, seconds with nothing to wait on, so core 0's idle
// task, which this task keeps from running meanwhile, is let off the task watchdog until it's done.
// The handshake has its own limit between its steps (setHandshakeTimeout).
bool tlsConnect(WiFiClientSecure& client) {
  disableCore0WDT();
  const bool ok = client.connect(bridgeHost, bridgePort);
  enableCore0WDT();
  return ok;
}

// Over HTTPS on the connection kept from the last request when it's still open, else a new one.
Error runHttps() {
  dropKept();
  if (!bridgeHost[0] || !bridgeCa) return Error::NoBridge;
  for (int attempt = 0; attempt < 2; ++attempt) {
    const bool reuse = keptTls && keptTls->connected();
    if (!reuse) {
      dropTls();
      keptTls = new (std::nothrow) WiFiClientSecure();
      if (!keptTls) { noteMemory("client"); return Error::NoMemory; }
      tlsHeld.store(true);
      keptTls->setCACert(bridgeCa);
      keptTls->setHandshakeTimeout(15);
      const uint32_t began = millis();
      const bool ok = tlsConnect(*keptTls);
      const uint32_t tookConnect = millis() - began;
      Serial.printf("[CAMNET] tls %s %ums free=%u largest=%u lent=%d\n", ok ? "open" : "failed", unsigned(tookConnect),
                    unsigned(ESP.getFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)), lentStart.load() != nullptr);
      if (!ok) {
        char detail[32] = {};
        const int code = keptTls->lastError(detail, sizeof(detail));
        noteFailure("tls", code, tookConnect);
        const Error e = tlsOutOfMemory(code) ? Error::NoMemory : Error::Connect;
        if (e == Error::NoMemory) noteMemory("TLS");
        dropTls();
        return e;
      }
    }
    bool anyAnswer = false, reusable = false;
    const Error e = exchange(*keptTls, false, anyAnswer, reusable);
    // On lent memory it's never kept: the lender wants its memory back as soon as the answer is in.
    if (!reusable || e == Error::Malformed || e == Error::Timeout || e == Error::Connect || cancelled.load() || lentStart.load())
      dropTls();
    else keptTlsAt = millis();
    // A kept connection the far end had already closed fails before any answer: once more on a new one.
    if (reuse && !anyAnswer && e == Error::Connect && !cancelled.load()) continue;
    return e;
  }
  return Error::Connect;
}
void loop(void*) {
  line = static_cast<char*>(heap_caps_malloc(kLineRoom, MALLOC_CAP_8BIT));
  for (;;) {
    // With an HTTPS connection kept, it wakes now and then to close it once it's been idle a while.
    ulTaskNotifyTake(pdTRUE, keptTls ? pdMS_TO_TICKS(500) : portMAX_DELAY);
    if (stopping.load()) break;
    if (keptTls && phase.load() != Phase::Queued && millis() - keptTlsAt >= kTlsIdleMs) dropTls();
    if (phase.load() != Phase::Queued) continue;
    phase.store(Phase::Running);
    const uint32_t began = millis();
    vTaskPrioritySet(nullptr, wantHome ? kHomePriority : kHttpsPriority);
    const Error e = wantHome ? runHome() : runHttps();
    took.store(millis() - began);
    // What the caller gets: nothing for a cancelled request, nor what went into a slot released
    // meanwhile. Slots released while it ran go now. Frees happen after the gate.
    uint8_t* unwanted = nullptr;
    uint8_t* gone[kSlots] = {};
    portENTER_CRITICAL(&gate);
    const bool dropBody = cancelled.load() || (bodyInSlot && intoSlot >= 0 && slotDropLater[intoSlot]);
    if (dropBody) {
      if (!bodyInSlot) unwanted = body;
      body = nullptr;
      bodyLength = 0;
      bodyInSlot = false;
    }
    for (uint8_t i = 0; i < kSlots; ++i) {
      if (!slotDropLater[i]) continue;
      gone[i] = slots[i];
      slots[i] = nullptr;
      slotSize[i] = 0;
      slotDropLater[i] = false;
    }
    intoSlot = -1;
    if (cancelled.load()) { failure.store(Error::None); answer.store(0); }
    else failure.store(e);
    phase.store(Phase::Done);
    portEXIT_CRITICAL(&gate);
    free(unwanted);
    for (uint8_t* p : gone) free(p);
    if (stopping.load()) break;
  }
  // Leaving: everything it held goes with it, the caller's slots too.
  dropKept();
  dropTls();
  if (body && !bodyInSlot) free(body);
  body = nullptr;
  bodyLength = 0;
  bodyInSlot = false;
  for (uint8_t i = 0; i < kSlots; ++i) { free(slots[i]); slots[i] = nullptr; slotSize[i] = 0; slotDropLater[i] = false; }
  free(line);
  line = nullptr;
  phase.store(Phase::Idle);
  worker = nullptr;
  vTaskDelete(nullptr);
}
}  // namespace

void setBridge(const char* host, uint16_t port, const char* token, const char* rootCa) {
  if (phase.load() == Phase::Queued || phase.load() == Phase::Running) return;
  podcast::copyText(bridgeHost, sizeof(bridgeHost), host ? host : "");
  podcast::copyText(bridgeToken, sizeof(bridgeToken), token ? token : "");
  bridgePort = port ? port : 443;
  bridgeCa = rootCa;
}
bool open() {
  if (worker) return !stopping.load();  // One still leaving: try again in a moment.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kStack + 2048) { noteMemory("worker"); return false; }
  stopping.store(false);
  phase.store(Phase::Idle);
  if (xTaskCreate(loop, "cameras", kStack, nullptr, kHomePriority, &worker) != pdPASS) { worker = nullptr; return false; }
  return true;
}
// Doesn't wait: a TLS handshake under way can take seconds to notice, and the screen shouldn't.
// The worker frees what it holds as it goes.
void close() {
  if (!worker) {
    drop();
    for (uint8_t i = 0; i < kSlots; ++i) { free(slots[i]); slots[i] = nullptr; slotSize[i] = 0; slotDropLater[i] = false; }
    return;
  }
  cancelled.store(true);
  stopping.store(true);
  xTaskNotifyGive(worker);
}
bool opened() { return worker != nullptr && !stopping.load(); }
bool start(const char* path, bool home, uint32_t ip, uint16_t port, size_t maxBytes, uint32_t waitMs, int slot) {
  if (!worker || stopping.load() || !path || !*path || strlen(path) >= sizeof(requested)) return false;
  if (home && (!ip || !port)) return false;
  if (slot >= int(kSlots) || (slot >= 0 && !slots[slot])) return false;
  portENTER_CRITICAL(&gate);
  const Phase now = phase.load();
  if (now == Phase::Queued || now == Phase::Running) { portEXIT_CRITICAL(&gate); return false; }
  portEXIT_CRITICAL(&gate);
  if (body && !bodyInSlot) free(body);
  body = nullptr;
  bodyLength = 0;
  bodyInSlot = false;
  intoSlot = slot;
  if (slot >= 0 && maxBytes + 1 > slotSize[slot]) maxBytes = slotSize[slot] - 1;
  strcpy(requested, path);
  wantHome = home;
  homeIp = ip;
  homePort = port;
  maxBody = uint32_t(maxBytes);
  waitFor = waitMs ? waitMs : kIdleMs;
  memNote[0] = 0;
  lastClip[0] = 0;
  cancelled.store(false);
  failure.store(Error::None);
  answer.store(0);
  lastHome.store(home);
  phase.store(Phase::Queued);
  xTaskNotifyGive(worker);
  return true;
}
bool busy() { const Phase p = phase.load(); return worker && (p == Phase::Queued || p == Phase::Running); }
bool finished() { return phase.load() == Phase::Done; }
Error error() { return failure.load(); }
unsigned status() { return answer.load(); }
bool overHome() { return lastHome.load(); }
uint32_t tookMs() { return took.load(); }
uint8_t* take(size_t& length) {
  if (phase.load() != Phase::Done) { length = 0; return nullptr; }
  uint8_t* out = body;
  length = bodyLength;
  body = nullptr;
  bodyLength = 0;
  bodyInSlot = false;
  phase.store(Phase::Idle);
  return out;
}
void drop() {
  if (busy()) return;
  if (body && !bodyInSlot) free(body);
  body = nullptr;
  bodyLength = 0;
  bodyInSlot = false;
  if (phase.load() == Phase::Done) phase.store(Phase::Idle);
}
bool reserve(uint8_t slot, size_t bytes) {
  if (slot >= kSlots || !bytes) return false;
  portENTER_CRITICAL(&gate);
  const bool inUse = (phase.load() == Phase::Queued || phase.load() == Phase::Running) && intoSlot == int(slot);
  const bool have = slots[slot] && slotSize[slot] >= bytes && !slotDropLater[slot];
  portEXIT_CRITICAL(&gate);
  if (have) return true;
  if (inUse) return false;
  release(slot);
  auto* p = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  if (!p) { noteMemory("slot"); return false; }
  portENTER_CRITICAL(&gate);
  slots[slot] = p;
  slotSize[slot] = bytes;
  slotDropLater[slot] = false;
  portEXIT_CRITICAL(&gate);
  return true;
}
void release(uint8_t slot) {
  if (slot >= kSlots) return;
  uint8_t* p = nullptr;
  portENTER_CRITICAL(&gate);
  const bool inUse = (phase.load() == Phase::Queued || phase.load() == Phase::Running) && intoSlot == int(slot);
  if (inUse) slotDropLater[slot] = slots[slot] != nullptr;
  else { p = slots[slot]; slots[slot] = nullptr; slotSize[slot] = 0; slotDropLater[slot] = false; }
  portEXIT_CRITICAL(&gate);
  free(p);
}
bool isSlot(const uint8_t* p) {
  if (!p) return false;
  for (uint8_t i = 0; i < kSlots; ++i) if (slots[i] == p) return true;
  return false;
}
bool tlsOpen() { return tlsHeld.load(); }
bool lend(void* memory, size_t bytes) {
  if (!memory || bytes < 8192 || lentStart.load() || busy() || tlsHeld.load()) return false;
  multi_heap_handle_t heap = multi_heap_register(memory, bytes);
  if (!heap) return false;
  // Installed once and kept: a piece made elsewhere still goes to the IDF's allocator and back.
  if (!allocatorSet) allocatorSet = mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree) == 0;
  if (!allocatorSet) return false;
  lentHeap = heap;
  lentBytes = bytes;
  lentFree = multi_heap_free_size(heap);
  lentPieces.store(0);
  lentPeakBytes = 0;
  lentStart.store(static_cast<uint8_t*>(memory));
  lentTaking.store(true);
  return true;
}
bool lent() { return lentStart.load() != nullptr; }
bool giveBack() {
  if (!lentStart.load()) return true;
  if (busy() || tlsHeld.load()) return false;  // The request, or its connection, still runs on it.
  lentTaking.store(false);
  // Every TLS piece is freed with its connection, so one left would be a leak: keep the memory
  // lent rather than hand back what something still writes to.
  if (lentPieces.load() != 0) return false;
  lentStart.store(nullptr);
  lentHeap = nullptr;
  return true;
}
size_t lentPeak() { return lentPeakBytes; }
void cancel() { if (busy()) cancelled.store(true); }
const char* memoryNote() { return memNote; }
const char* failureNote() { return failNote; }
const char* clipHeader() { return busy() ? "" : lastClip; }
const char* errorText(Error e) {
  switch (e) {
    case Error::None: return nullptr;
    case Error::NoMemory: return "Not enough memory";
    case Error::TaskFailed: return "Cameras couldn't start";
    case Error::NoBridge: return "The bridge refused";
    case Error::Connect: return "Couldn't reach the bridge";
    case Error::Timeout: return "The bridge took too long";
    case Error::Http: return "The bridge said no";
    case Error::NotFound: return "Not there any more";
    case Error::Conflict: return "Not ready yet";
    case Error::Busy: return "The cameras are busy";
    case Error::Malformed: return "Odd answer from the bridge";
    case Error::TooBig: return "Picture too big";
  }
  return "Camera error";
}
}  // namespace camera_net
