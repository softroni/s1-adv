#!/usr/bin/env python3
"""The canvas's memory lent to one HTTPS request (camera_net::lend, 006's lendUiCanvasToTls).

Skywatch keeps the screen's 32 KB canvas and has about 60 KB left, less than a TLS handshake and
Wi-Fi need together. So for an HTTPS request the canvas's memory is lent to TLS where it lies, never
freed and made again (which split the heap in 0.2.173). This compiles the real allocator hooks, lend
and give-back from camera_net.cpp and the real canvas side from 006, against stubs, and checks that:
only the worker's pieces go into the lent memory, a piece is freed back to where it came from, the
memory comes back only once the request is over and every piece is freed, the canvas is never drawn
into or remade while lent, and the screen holds its last frame meanwhile.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
net = (src / 'camera_net.cpp').read_text()
part = lambda name: (src / 'main_parts' / name).read_text()

hooks = net[net.index('// --- Lent memory (lend) ---'):net.index('void noteMemory(const char* what) {')]
lend = net[net.index('bool lend(void* memory, size_t bytes) {'):net.index('void cancel() {')]
canvas6 = part('006_main.cpp.inc')
canvas = canvas6[canvas6.index('bool gCanvasLent = false;'):canvas6.index('String chipIdSuffix()')]
restore = canvas6[canvas6.index('void restoreUiCanvasAfterNetwork() {'):canvas6.index('// The canvas lent to an app')]
render = part('060_main.cpp.inc').split('void render() {', 1)[1].split('UiFrameTrace frameTrace;', 1)[0]

# What the firmware's text must keep, beside what the program below checks by running it.
assert 'if (gCanvasLent && !gUiInputPending && uiSceneKey() == gUiFramePolicy.lastScene) return;' in render, \
    'while lent, the screen keeps its last frame unless a key or a new screen asks'
assert 'cancelled.load() || lentStart.load())\n      dropTls();' in net, 'a connection on lent memory is never kept'
loop = part('061_main.cpp.inc')
assert loop.index('takeUiCanvasBack();') < loop.index('if (gRealtimeExclusiveMode) {'), 'given back ahead of every early return'
assert '!gCanvasSpriteActive && !gCanvasLent && gUiMode != UiMode::Nes' in loop, 'the loop never remakes a lent canvas'
planes = part('096_main.cpp.inc')
start = planes.split('bool plStart(', 1)[1].split('\n}\n', 1)[0]
assert start.index('lent = lendUiCanvasToTls("planes");') < start.index('camera_net::start(')
assert 'if (lent) takeUiCanvasBack();' in start, 'a request that never started gives the canvas straight back'
waits = planes.split('bool plHttpsWaits() {', 1)[1].split('\n}\n', 1)[0]
assert 'gPlChimeLeft || gPlChimeBuf' in waits and 'uiCanvasReadyToLend()' in waits
nxt = planes.split('bool plNextJob() {', 1)[1].split('\n}\n', 1)[0]
assert nxt.index('if (plHttpsWaits()) return false;') < nxt.index('gPlReport[0]'), 'it waits before taking any job'
chime = planes.split('void plServiceChime() {', 1)[1].split('\n}\n', 1)[0]
assert chime.index('if (gCanvasLent) return;') < chime.index('--gPlChimeLeft;'), 'a chime waits for the canvas, not lost'

code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
// --- FreeRTOS, the IDF heap and mbedTLS, as far as the code below uses them ---
typedef void* TaskHandle_t;
static int workerTask = 1, otherTask = 2;
static TaskHandle_t current = &workerTask;
TaskHandle_t xTaskGetCurrentTaskHandle() { return current; }
struct Arena { uint8_t* start; size_t size, used, pieces; };
typedef Arena* multi_heap_handle_t;
static Arena arenaStore;
multi_heap_handle_t multi_heap_register(void* start, size_t size) {
  arenaStore = Arena{static_cast<uint8_t*>(start), size, 64, 0};  // Its own header at the front.
  return &arenaStore;
}
void* multi_heap_malloc(multi_heap_handle_t h, size_t n) {
  n = (n + 3) & ~size_t(3);
  if (h->used + n > h->size) return nullptr;
  void* p = h->start + h->used;
  h->used += n;
  ++h->pieces;
  return p;
}
void multi_heap_free(multi_heap_handle_t h, void* p) {
  assert(p >= h->start && p < h->start + h->size && h->pieces);
  if (!--h->pieces) h->used = 64;
}
size_t multi_heap_free_size(multi_heap_handle_t h) { return h->size - h->used; }
static std::set<void*> heapPieces;
extern "C" void* esp_mbedtls_mem_calloc(size_t n, size_t size) { void* p = calloc(n, size); if (p) heapPieces.insert(p); return p; }
extern "C" void esp_mbedtls_mem_free(void* p) { assert(heapPieces.erase(p)); free(p); }
static void* (*mbedCalloc)(size_t, size_t) = esp_mbedtls_mem_calloc;
static void (*mbedFree)(void*) = esp_mbedtls_mem_free;
int mbedtls_platform_set_calloc_free(void* (*c)(size_t, size_t), void (*f)(void*)) { mbedCalloc = c; mbedFree = f; return 0; }
// --- camera_net's state that lend() looks at ---
namespace camera_net {
namespace {
TaskHandle_t worker = &workerTask;
bool requestRunning = false;
std::atomic<bool> tlsHeld{false};
''' + hooks + r'''
}  // namespace
bool busy() { return requestRunning; }
''' + lend + r'''
}  // namespace camera_net
// --- The canvas, the screen and the frame policy, as 006 uses them ---
struct Policy { bool valid = true; uint32_t lastScene = 7; void invalidate() { valid = false; } };
Policy gUiFramePolicy;
uint32_t sceneNow = 7, gLastRenderMs = 99;
uint32_t uiSceneKey() { return sceneNow; }
uint8_t canvasMemory[32400];
int creates = 0, waits = 0;
struct Canvas {
  void* getBuffer() { return canvasMemory; }
  uint32_t bufferLength() { return sizeof(canvasMemory); }
  void setPsram(bool) {} void setColorDepth(int) {} void setTextWrap(bool) {} void setFont(const void*) {} void setTextSize(int) {}
  void* createSprite(int, int) { ++creates; return canvasMemory; }
} gCanvasBuffer;
struct { struct { void waitDMA() { ++waits; } } Display; } M5Cardputer;
struct { unsigned getFreeHeap() { return 60000; } } ESP;
constexpr int MALLOC_CAP_8BIT = 0;
unsigned heap_caps_get_largest_free_block(int) { return 49000; }
uint32_t nowMs = 1000;
uint32_t millis() { return nowMs; }
bool psramFound() { return false; }
void configureEmojiRendering() {}
std::string lastLog;
void logf(const char* f, ...) { lastLog = f; }
namespace fonts { int Font2; }
bool gCanvasSpriteActive = true;
bool takeUiCanvasBack();
''' + restore + canvas + r'''
using namespace camera_net;
void* tlsAlloc(size_t n) { return mbedCalloc(1, n); }
bool inCanvas(void* p) { return p >= canvasMemory && p < canvasMemory + sizeof(canvasMemory); }
int main() {
  // Nothing is lent while a request or a connection still runs, nor a scrap too small to hold TLS.
  requestRunning = true; assert(!lend(canvasMemory, sizeof(canvasMemory))); requestRunning = false;
  tlsHeld = true; assert(!lend(canvasMemory, sizeof(canvasMemory))); tlsHeld = false;
  assert(!lend(canvasMemory, 4096) && !lent());
  // And the canvas only once its frame is up to date: a fresh answer is seen before the next loan.
  gUiFramePolicy.valid = false; assert(!lendUiCanvasToTls("t") && gCanvasSpriteActive); gUiFramePolicy.valid = true;
  sceneNow = 8; assert(!lendUiCanvasToTls("t")); sceneNow = 7;

  assert(lendUiCanvasToTls("planes") && gCanvasLent && !gCanvasSpriteActive && lent() && waits == 1);
  assert(!lendUiCanvasToTls("planes") && !lend(canvasMemory, sizeof(canvasMemory)));  // Once at a time.
  // TLS's pieces on the worker go into it, zeroed; another task's TLS keeps the heap.
  requestRunning = true; tlsHeld = true;
  memset(canvasMemory, 0xAB, sizeof(canvasMemory));
  uint8_t* inBuf = static_cast<uint8_t*>(tlsAlloc(16717));
  assert(inCanvas(inBuf) && inBuf[0] == 0 && inBuf[16716] == 0);
  void* outBuf = tlsAlloc(16717);  // No room left in it: the heap, as before.
  assert(!inCanvas(outBuf) && heapPieces.count(outBuf));
  void* small = tlsAlloc(900);
  assert(inCanvas(small));
  current = &otherTask;
  void* voice = tlsAlloc(500);
  assert(!inCanvas(voice) && heapPieces.count(voice));
  current = &workerTask;
  assert(!mbedCalloc(SIZE_MAX / 2, 4));  // A size that overflows never reaches the lent memory.
  assert(lentPeak() >= 16717 + 900);
  // Not back while the request runs, while its connection is open, nor while a piece is out.
  assert(!giveBack() && !takeUiCanvasBack() && gCanvasLent);
  requestRunning = false;
  assert(!giveBack());
  tlsHeld = false;
  mbedFree(outBuf); mbedFree(inBuf);
  assert(!takeUiCanvasBack() && gCanvasLent && !gCanvasSpriteActive);
  // Meanwhile the loop's restore never remakes it over the lent memory.
  restoreUiCanvasAfterNetwork();
  assert(creates == 0 && gCanvasLent);
  mbedFree(small);
  // A piece freed elsewhere goes back to the heap it came from, lent or not.
  current = &otherTask; mbedFree(voice); current = &workerTask;
  assert(heapPieces.empty());
  // Every piece back: the canvas is the screen's again, drawn whole from the next frame.
  gUiFramePolicy.valid = true;
  assert(takeUiCanvasBack() && !gCanvasLent && gCanvasSpriteActive && !lent());
  assert(!gUiFramePolicy.valid && gLastRenderMs == 0 && creates == 0);
  // After it, the worker's TLS uses the heap again.
  void* later = tlsAlloc(100);
  assert(!inCanvas(later));
  mbedFree(later);
  assert(heapPieces.empty());
  // And a canvas that is back is never remade either.
  restoreUiCanvasAfterNetwork();
  assert(creates == 0);
}
'''
with tempfile.TemporaryDirectory(prefix='s1-canvas-loan-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS canvas loan: only the worker\'s TLS uses the lent canvas, which comes back whole once every piece is freed')
