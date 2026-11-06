#include "camera_net.h"
#include "camera_catalog.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/ssl.h>
#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

namespace camera_net {
namespace {
constexpr uint32_t kStack = 10240;     // TLS's handshake runs on it; plain HTTP needs far less.
constexpr uint32_t kIdleMs = 10000;    // A body that stops for this long has failed.
constexpr size_t kLineMax = 384;

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
// Kept by the worker between requests: the home connection, and where it goes.
WiFiClient* kept = nullptr;
uint32_t keptIp = 0;
uint16_t keptPort = 0;
// The answer, the caller's once Done.
uint8_t* body = nullptr;
size_t bodyLength = 0;
char memNote[48] = "";
char lastClip[40] = "";

void noteMemory(const char* what) {
  snprintf(memNote, sizeof(memNote), "%s %uK/%uK", what, unsigned(ESP.getFreeHeap() / 1024),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
}
void dropKept() {
  if (!kept) return;
  kept->stop();
  delete kept;
  kept = nullptr;
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
  auto* line = static_cast<char*>(heap_caps_malloc(kLineMax + 256, MALLOC_CAP_8BIT));
  if (!line) { noteMemory("request"); return Error::NoMemory; }
  const int n = home
    ? snprintf(line, kLineMax + 256, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: S1-ADV-Cameras/1\r\nAccept: */*\r\n"
               "Connection: keep-alive\r\n\r\n", requested, host)
    : snprintf(line, kLineMax + 256, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: S1-ADV-Cameras/1\r\nAuthorization: Bearer %s\r\n"
               "Accept: */*\r\nConnection: close\r\n\r\n", requested, host, bridgeToken);
  if (n <= 0 || size_t(n) >= kLineMax + 256) { free(line); return Error::Malformed; }
  if (client.write(reinterpret_cast<const uint8_t*>(line), size_t(n)) != size_t(n)) { free(line); return Error::Connect; }
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
  free(line);
  if (e != Error::None || cancelled.load()) return e;
  memcpy(lastClip, h.clip, sizeof(lastClip));
  if (h.chunked) return Error::Malformed;  // The bridge always says how long.
  const bool bodyless = h.status == 204 || h.status == 304;
  if (!bodyless && !h.hasLength) return Error::Malformed;
  const size_t length = bodyless ? 0 : h.length;
  // An error's few words are kept too, so the screen can say which.
  const size_t limit = h.status == 200 ? maxBody : 256;
  if (length > limit) return Error::TooBig;
  if (length) {
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(length + 1, MALLOC_CAP_8BIT));
    if (!data) { noteMemory("answer"); return Error::NoMemory; }
    const int got = r.into(data, length);
    if (got) { free(data); return got == -3 ? Error::None : got == -2 ? Error::Timeout : Error::Connect; }
    data[length] = 0;
    body = data;
    bodyLength = length;
  }
  reusable = home && !h.close;
  return fromStatus(h.status);
}

Error runHome() {
  for (int attempt = 0; attempt < 2; ++attempt) {
    const bool reuse = kept && keptIp == homeIp && keptPort == homePort && kept->connected();
    if (!reuse) {
      dropKept();
      kept = new (std::nothrow) WiFiClient();
      if (!kept) { noteMemory("client"); return Error::NoMemory; }
      const IPAddress ip(uint8_t(homeIp >> 24), uint8_t(homeIp >> 16), uint8_t(homeIp >> 8), uint8_t(homeIp));
      if (!kept->connect(ip, homePort, 3000)) { dropKept(); return Error::Connect; }
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

Error runHttps() {
  dropKept();
  if (!bridgeHost[0] || !bridgeCa) return Error::NoBridge;
  auto* tls = new (std::nothrow) WiFiClientSecure();
  if (!tls) { noteMemory("client"); return Error::NoMemory; }
  tls->setCACert(bridgeCa);
  tls->setHandshakeTimeout(15);
  Error e = Error::None;
  if (!tls->connect(bridgeHost, bridgePort)) {
    char detail[32] = {};
    e = tls->lastError(detail, sizeof(detail)) == MBEDTLS_ERR_SSL_ALLOC_FAILED ? Error::NoMemory : Error::Connect;
    if (e == Error::NoMemory) noteMemory("TLS");
  } else {
    bool anyAnswer = false, reusable = false;
    e = exchange(*tls, false, anyAnswer, reusable);
  }
  tls->stop();
  delete tls;
  return e;
}

void loop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (stopping.load()) break;
    if (phase.load() != Phase::Queued) continue;
    phase.store(Phase::Running);
    const uint32_t began = millis();
    const Error e = wantHome ? runHome() : runHttps();
    took.store(millis() - began);
    portENTER_CRITICAL(&gate);
    if (cancelled.load()) { failure.store(Error::None); answer.store(0); }
    else failure.store(e);
    phase.store(Phase::Done);
    portEXIT_CRITICAL(&gate);
    if (cancelled.load() || e != Error::None) {
      // A cancelled or failed answer isn't wanted, and only a whole one is any use.
      if (cancelled.load() && body) { free(body); body = nullptr; bodyLength = 0; }
    }
    if (stopping.load()) break;
  }
  // Leaving: everything it held goes with it.
  dropKept();
  if (body) { free(body); body = nullptr; bodyLength = 0; }
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
  if (xTaskCreate(loop, "cameras", kStack, nullptr, 2, &worker) != pdPASS) { worker = nullptr; return false; }
  return true;
}
// Doesn't wait: a TLS handshake under way can take seconds to notice, and the screen shouldn't.
// The worker frees what it holds as it goes.
void close() {
  if (!worker) { drop(); return; }
  cancelled.store(true);
  stopping.store(true);
  xTaskNotifyGive(worker);
}
bool opened() { return worker != nullptr && !stopping.load(); }
bool start(const char* path, bool home, uint32_t ip, uint16_t port, size_t maxBytes, uint32_t waitMs) {
  if (!worker || stopping.load() || !path || !*path || strlen(path) >= sizeof(requested)) return false;
  if (home && (!ip || !port)) return false;
  portENTER_CRITICAL(&gate);
  const Phase now = phase.load();
  if (now == Phase::Queued || now == Phase::Running) { portEXIT_CRITICAL(&gate); return false; }
  portEXIT_CRITICAL(&gate);
  if (body) { free(body); body = nullptr; bodyLength = 0; }
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
  phase.store(Phase::Idle);
  return out;
}
void drop() {
  if (busy()) return;
  if (body) { free(body); body = nullptr; }
  bodyLength = 0;
  if (phase.load() == Phase::Done) phase.store(Phase::Idle);
}
void cancel() { if (busy()) cancelled.store(true); }
const char* memoryNote() { return memNote; }
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
