#!/usr/bin/env python3
"""Check ext_display.h against a simulated ILI9341 on EXT: found by reading back, set up with the
datasheet's waits, the Cardputer's screen copied a third bigger in short passes, black when the
screen is off, let go when unplugged, and EXT left alone when off, when a Cap or a sensor is
there, or while Mesh, the RFID Lab or Sonar has the pins. Then the firmware's wiring of it."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'bots/CardputerADV/src'
PARTS = SRC / 'main_parts'

CODE = r'''
#include "ext_display.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
using namespace ext_display;

// The world on the EXT header, and a screen that keeps its own registers and memory.
struct Fake {
  bool screen = true;   // An ILI9341 is plugged in and powered.
  bool miso = true;     // Its SDO is wired.
  uint32_t floating = 0xFFFFFFFF;  // What a read gets with nothing answering.
  bool cap = false, held = false;
  int skip = 1;         // Dummy clocks before a read's value.
  uint32_t now = 0;
  // Pins.
  bool claimed = false;
  int claims = 0, releases = 0, resets = 0, capAsks = 0, heldAsks = 0, traffic = 0, maxRows = 0, rowsSent = 0;
  // The controller.
  uint8_t madctl = 0;
  bool awake = false, on = false;
  uint32_t resetAt = 0, sleepOutAt = 0;
  std::vector<uint16_t> gram = std::vector<uint16_t>(kWidth * kHeight, 0x1234);
  uint16_t y0 = 0, y1 = 0, at = 0;
  bool writing = false;
  std::vector<uint8_t> commands;
  std::vector<std::string> lines;

  void powerBlip() { madctl = 0; awake = on = false; }  // As after its own power-on reset.
  uint32_t ms() { return now; }
  bool capPresent() { ++capAsks; return cap; }
  bool dcHeld() { assert(!claimed); ++heldAsks; return held; }  // Only asked while G6 is an input.
  void claim() { claimed = true; ++claims; }
  void release() { assert(claimed); claimed = false; ++releases; }
  void reset() {
    assert(claimed); ++resets;
    if (screen) { powerBlip(); resetAt = now; }
  }
  void command(uint8_t c, const uint8_t* data, uint8_t n) {
    assert(claimed && !writing); ++traffic;
    commands.push_back(c);
    if (!screen) return;
    if (c == cmd::kMadctl) { assert(n == 1); madctl = data[0]; }
    if (c == cmd::kSleepOut) { assert(now - resetAt >= 120); awake = true; sleepOutAt = now; }
    if (c == cmd::kDisplayOn) { assert(awake && now - sleepOutAt >= 120); on = true; }
  }
  uint32_t read32(uint8_t c) {
    assert(claimed && !writing && c == cmd::kReadMadctl); ++traffic;
    if (!screen || !miso) return floating;
    const uint32_t dummies = skip ? (0xFFFFFFFFu << (32 - skip)) : 0;  // Whatever the dummy clocks hold.
    return dummies | (uint32_t(madctl) << (24 - skip));
  }
  void beginPixels(uint16_t y, uint16_t rows) {
    assert(claimed && !writing && rows >= 1 && y + rows <= kHeight); ++traffic;
    if (screen) assert(on);
    writing = true; y0 = y; y1 = uint16_t(y + rows); at = 0;
    maxRows = rows > maxRows ? rows : maxRows; rowsSent += rows;
  }
  void put(uint16_t c) {
    assert(writing && y0 + at / kWidth < y1);
    if (screen) gram[y0 * kWidth + at] = c;
    ++at;
  }
  void pixels(const uint8_t* bytes, size_t n) { assert(n % 2 == 0); for (size_t i = 0; i < n; i += 2) put(uint16_t(bytes[i] << 8 | bytes[i + 1])); }
  void blank(uint16_t count) { for (uint16_t i = 0; i < count; ++i) put(0); }
  void endPixels() { assert(writing && at == (y1 - y0) * kWidth); writing = false; }
  void said(const char* line) { lines.push_back(line); }
  bool saidSomething(const char* part) const { for (auto& l : lines) if (l.find(part) != std::string::npos) return true; return false; }
};

uint8_t canvas[kSourceHeight][kSourceWidth];
void paint(uint8_t seed) { for (int y = 0; y < kSourceHeight; ++y) for (int x = 0; x < kSourceWidth; ++x) canvas[y][x] = uint8_t(x * 7 + y * 13 + seed); }

struct Run {
  Fake port;
  Mirror<Fake> mirror;
  Mode mode = Mode::Auto;
  bool upsideDown = false, free = true;
  Frame frame;
  Run() { frame.pixels = &canvas[0][0]; }
  // The loop, every 5 ms.
  void pass() { mirror.service(port, mode, upsideDown, free, frame); port.now += 5; }
  void run(uint32_t ms) { for (uint32_t end = port.now + ms; port.now < end;) pass(); }
  bool showsCanvas() const {
    for (int y = 0; y < kHeight; ++y)
      for (int x = 0; x < kWidth; ++x) {
        const bool picture = y >= kPictureTop && y < kPictureTop + kPictureHeight;
        const uint16_t want = picture ? rgb565(canvas[(y - kPictureTop) * 3 / 4][x * 3 / 4]) : 0;  // Nearest, from the left.
        if (port.gram[y * kWidth + x] != want) return false;
      }
    return true;
  }
  bool black() const { for (auto c : port.gram) if (c) return false; return true; }
};

int main() {
  // The setting, saved as a number, and what the row says.
  assert(modeOrDefault(0) == Mode::Auto && modeOrDefault(1) == Mode::On && modeOrDefault(2) == Mode::Off && modeOrDefault(9) == Mode::Auto);
  assert(std::string(modeName(Mode::Auto, true)) == "auto, found" && std::string(modeName(Mode::Auto, false)) == "auto, none");
  assert(std::string(modeName(Mode::On, false)) == "on" && std::string(modeName(Mode::Off, true)) == "off");

  // Colours widen as M5GFX widens them; geometry is a third bigger, every canvas pixel used.
  assert(rgb565(0x00) == 0x0000 && rgb565(0xFF) == 0xFFFF && rgb565(0xE0) == 0xF800 && rgb565(0x1C) == 0x07E0 && rgb565(0x03) == 0x001F);
  for (unsigned c = 0; c < 256; ++c) {  // Every RGB332 value, against 8-bit expansion by hand.
    const unsigned r3 = c >> 5, g3 = (c >> 2) & 7, b2 = c & 3;
    const unsigned r = r3 * 36 + (r3 >> 1), g = g3 * 36 + (g3 >> 1), b = b2 * 85;
    assert(rgb565(uint8_t(c)) == (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
  }
  assert(kPictureHeight == 180 && kPictureTop == 30 && sourceOf(0) == 0 && sourceOf(319) == 239 && sourceOf(179) == 134);
  { bool col[kSourceWidth] = {}; for (uint16_t x = 0; x < kWidth; ++x) col[sourceOf(x)] = true; for (bool c : col) assert(c); }
  { bool row[kSourceHeight] = {}; for (uint16_t y = 0; y < kPictureHeight; ++y) row[sourceOf(y)] = true; for (bool r : row) assert(r); }
  { uint8_t src[kSourceWidth], out[kWidth * 2]; for (int i = 0; i < kSourceWidth; ++i) src[i] = uint8_t(i);
    scaleRow(src, out);
    assert(out[0] == (rgb565(0) >> 8) && out[2 * 319] == (rgb565(239) >> 8) && out[2 * 319 + 1] == (rgb565(239) & 0xFF)); }
  // The start-up list parses to its 0, with 16-bit pixels in it.
  { size_t i = 0; bool sixteen = false;
    while (kSetup[i]) { if (kSetup[i] == 0x3A) sixteen = kSetup[i + 1] == 1 && kSetup[i + 2] == 0x55; i += 2 + kSetup[i + 1]; }
    assert(i == sizeof(kSetup) - 1 && sixteen); }
  // Landscape both ways round: MV set, no mirror image (MX and MY together or neither).
  assert(madctl(false) == 0xE8 && madctl(true) == 0x28);
  // A read-back is found wherever the value starts, and only both values together count.
  for (int skip = 0; skip <= kMaxSkip; ++skip) {
    const uint32_t ones = skip ? (0xFFFFFFFFu << (32 - skip)) : 0;
    assert(echoSkip(ones | uint32_t(kProbeA) << (24 - skip), ones | uint32_t(kProbeB) << (24 - skip)) == skip);
    assert(echoSkip(uint32_t(kProbeA) << (24 - skip), uint32_t(kProbeA) << (24 - skip)) == -1);
  }
  assert(echoSkip(0, 0) == -1 && echoSkip(0xFFFFFFFF, 0xFFFFFFFF) == -1 && echoSkip(0x55555555, 0xAAAAAAAA) == -1);

  // A screen that answers, wherever its read starts: found, reset, set up with the waits, then the
  // canvas a third bigger with black above and below, a few rows a pass.
  for (int skip : {0, 1, 8}) {
    Run r; r.port.skip = skip; paint(1);
    r.pass();
    assert(r.mirror.shown() && r.mirror.skip() == skip && r.port.resets == 1 && r.port.saidSomething("found an ILI9341"));
    r.run(400);
    assert(r.port.on && r.port.madctl == 0xE8 && r.showsCanvas() && r.port.maxRows <= kRowsPerPass);
    assert(r.port.saidSomething("showing the Cardputer's screen"));
    // Nothing new: nothing sent but a question every two seconds.
    const int rows = r.port.rowsSent, traffic = r.port.traffic;
    r.run(4000);
    assert(r.port.rowsSent == rows && r.port.traffic - traffic == 2 && r.port.resets == 1);
    // A new frame: the picture's rows only, at most twenty copies a second.
    paint(2); ++r.frame.generation;
    r.run(400);
    assert(r.showsCanvas() && r.port.rowsSent - rows == kPictureHeight);
    const int before = r.port.rowsSent;
    for (int i = 0; i < 40; ++i) { paint(uint8_t(3 + i)); ++r.frame.generation; r.pass(); }  // 200 ms of frames.
    assert(r.port.rowsSent - before <= 5 * kPictureHeight);
    r.run(300);
    assert(r.showsCanvas());
  }

  // The canvas lent out part-way: the copy waits for it, then finishes.
  { Run r; paint(5); r.run(260);
    ++r.frame.generation; paint(6); r.pass(); r.pass();
    const int sent = r.port.rowsSent;
    r.frame.pixels = nullptr; r.run(200);
    assert(r.port.rowsSent == sent);
    r.frame.pixels = &canvas[0][0]; r.run(200);
    assert(r.showsCanvas()); }

  // The Cardputer's screen off: black, since this one's backlight can't go off. Back on, all of it again.
  { Run r; paint(7); r.run(500);
    r.frame.screenOn = false; r.run(300);
    assert(r.black());
    const int rows = r.port.rowsSent; r.run(1000); assert(r.port.rowsSent == rows);
    r.frame.screenOn = true; r.run(300);
    assert(r.showsCanvas()); }

  // Turned round in the setting: the other landscape, all of it again, and the checks expect it.
  { Run r; paint(8); r.run(500);
    r.upsideDown = true; r.run(300);
    assert(r.port.madctl == 0x28 && r.showsCanvas());
    const int resets = r.port.resets; r.run(5000); assert(r.port.resets == resets && r.mirror.shown()); }

  // Unplugged: two checks unanswered, then let go, and looked for again every three seconds.
  { Run r; paint(9); r.run(500);
    r.port.screen = false; r.run(2100);
    assert(r.mirror.shown());  // One unanswered check could be a glitch.
    r.run(2000);
    assert(!r.mirror.shown() && !r.port.claimed && r.port.saidSomething("gone"));
    const int claims = r.port.claims; r.run(6000); assert(r.port.claims - claims == 2 && !r.port.claimed);
    // Plugged back in: found within one look, set up and drawn.
    r.port.screen = true; r.port.powerBlip(); r.run(3500);
    assert(r.mirror.shown() && r.port.on && r.showsCanvas()); }

  // Its power blips: the next check sees MADCTL lost, and it's set up again and redrawn.
  { Run r; paint(10); r.run(500);
    const int resets = r.port.resets;
    r.port.powerBlip(); r.run(2500);
    assert(r.port.resets == resets + 1 && r.port.on && r.showsCanvas() && r.port.saidSomething("restarted")); }

  // Nothing on EXT, or a reader that answers nothing back: looked at, said once, let go each time.
  for (uint32_t floating : {0x00000000u, 0xFFFFFFFFu}) {
    Run r; r.port.screen = false; r.port.floating = floating; r.run(10000);
    assert(!r.mirror.shown() && !r.port.claimed && r.port.claims == 4 && r.port.releases == 4 && r.port.resets == 0 && r.port.rowsSent == 0);
    int said = 0; for (auto& l : r.port.lines) said += l.find("nothing answered") != std::string::npos; assert(said == 1); }

  // A Cap, or a sensor holding G6 low: never claimed, never written to.
  for (bool cap : {true, false}) {
    Run r; r.port.cap = cap; r.port.held = !cap; r.run(10000);
    assert(!r.mirror.shown() && r.port.claims == 0 && r.port.traffic == 0 && r.port.saidSomething("not looking")); }

  // A Cap where the screen was, while it runs: let go at the next check.
  { Run r; paint(11); r.run(500);
    r.port.screen = false; r.port.cap = true; r.run(2100);
    assert(!r.mirror.shown() && !r.port.claimed && r.port.saidSomething("let go")); }

  // Off: EXT is never touched, not even asked about.
  { Run r; r.mode = Mode::Off; r.run(10000);
    assert(r.port.claims == 0 && r.port.traffic == 0 && r.port.capAsks == 0 && r.port.heldAsks == 0); }
  // Turned off while showing: left black, then let go.
  { Run r; paint(12); r.run(500);
    r.mode = Mode::Off; r.pass();
    assert(r.black() && !r.port.claimed && !r.mirror.shown());
    const int traffic = r.port.traffic; r.run(5000); assert(r.port.traffic == traffic); }

  // Someone else has the pins (Mesh, the RFID Lab, Sonar): nothing at all until they're back,
  // then a check at once, which finds the screen reset in the meantime and starts it again.
  { Run r; paint(13); r.run(500);
    r.free = false; const int traffic = r.port.traffic, asks = r.port.capAsks + r.port.heldAsks;
    r.run(8000);
    assert(r.port.traffic == traffic && r.port.capAsks + r.port.heldAsks == asks);
    r.port.powerBlip();
    r.free = true; r.pass();
    assert(r.port.traffic > traffic && r.port.resets == 2);
    r.run(500); assert(r.port.on && r.showsCanvas()); }
  { Run r; r.free = false; r.run(5000); assert(r.port.claims == 0 && r.port.capAsks == 0); r.free = true; r.pass(); assert(r.mirror.shown()); }

  // On: a screen without MISO is driven anyway, and set up again every five seconds, no reset,
  // so one plugged in later wakes too.
  { Run r; r.mode = Mode::On; r.port.miso = false; paint(14); r.run(500);
    assert(r.mirror.shown() && r.mirror.skip() == -1 && r.port.on && r.showsCanvas());
    const int resets = r.port.resets;
    r.port.powerBlip(); r.run(5500);
    assert(r.port.resets == resets && r.port.on && r.showsCanvas());
    r.port.screen = false; r.run(20000); assert(r.mirror.shown()); }  // On never gives up.
  // On still refuses a Cap.
  { Run r; r.mode = Mode::On; r.port.cap = true; r.run(5000); assert(r.port.claims == 0 && r.port.traffic == 0); }
  // Switching the setting starts over: Auto to On sets it up again.
  { Run r; paint(15); r.run(500);
    r.mode = Mode::On; r.run(500);
    assert(r.port.resets == 2 && r.port.on && r.showsCanvas() && r.mirror.skip() == 1); }
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='s1-ext-display-') as directory:
    p = Path(directory)
    (p / 'ext.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(SRC), str(p / 'ext.cpp'), '-o', str(p / 'ext')], check=True)
    subprocess.run([str(p / 'ext')], check=True)


def read(n):
    return (PARTS / f'{n}_main.cpp.inc').read_text()


# The firmware's side: the pins it shares, when it stands aside, where the loop runs it, the frames it sees.
part = read('099')
assert 'kExtCsPin == kRfidCsPin && kExtResetPin == kRfidResetPin && kExtDcPin == kSonarEchoPin' in part
assert 'return !gMeshBusOwned && gUiMode != UiMode::RfidLab && !gSonar.echoOn;' in part
assert 'gSdSpi.beginTransaction(' in part and 'SPI.begin' not in part and 'SD.' not in part  # The SD card's own SPI object and lock.
assert 'if (gCanvasSpriteActive && !gCanvasLent &&' in part  # Never reads the canvas while TLS has it.
assert 'readRegister(0x43, 0x03' in part and 'readRegister(0x43,0x03' in read('068')  # The Cap, as Mesh finds it.
loop = (PARTS / '061_main.cpp.inc').read_text()
setup = loop[loop.index('void setup() {'):loop.index('void serviceRealtimeExclusiveLoop')]
assert setup.index('loadDeviceSettings();') < setup.index('serviceExtScreen();') < setup.index('startDeferredBoot();')
body = loop[loop.index('void loop() {'):]
assert body.index('serviceExtScreen();') < body.index('if (gRealtimeExclusiveMode)'), 'before every early return'
present = read('006')
present = present[present.index('void presentUiFrame() {'):present.index('void noteUiInput')]
assert present.index('pushSprite') < present.index('++gExtScreenFrames;')
assert '#include "main_parts/099_main.cpp.inc"' in (SRC / 'main.cpp').read_text()
# The two Display settings rows.
assert 'constexpr uint8_t kScreenSettingsItemCount = 13;' in read('001')
assert re.search(r'kExtScreenSetting = 11;.*\n.*kExtUpsideDownSetting = 12;', read('001'))
assert 'getUChar("ext_scr", 0)' in read('026') and 'putUChar("ext_scr"' in read('026') and 'putBool("ext_flip"' in read('026')
assert 'return "Ext screen";' in read('056') and 'return "Ext upside down";' in read('056')
print('PASS ext display: found by read-back at any of 9 bit offsets, set up with the waits, copied a third bigger in passes of '
      'at most 12 rows, black when off, upside down, redrawn after a power blip, let go when unplugged or a Cap appears, '
      'EXT untouched when off, with a Cap or sensor, or while Mesh, RFID or Sonar has it; On drives a screen without MISO')
