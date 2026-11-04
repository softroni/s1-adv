#!/usr/bin/env python3
"""Exercise Sonar's choice between the M5 unit on Grove and the HC-SR04 on EXT, on host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "sonar.h"'),
        ('004_main.cpp.inc', '{"Sonar", UiMode::Sonar}'),
        ('005_main.cpp.inc', 'void stopSonar();'),
        ('022_main.cpp.inc', 'mode == "sonar"'),
        ('043_main.cpp.inc', 'cmd == "/sonar"'),
        ('048_main.cpp.inc', 'gUiMode == UiMode::Sonar && handleSonarKey'),
        ('061_main.cpp.inc', 'renderSonarUi();'),
        ('063_main.cpp.inc', 'case UiMode::Sonar:'),
        # The HC-SR04 shares the Cap's pins, so Mesh still keeps Sonar out.
        ('068_main.cpp.inc', 'case UiMode::RfidLab: case UiMode::Sonar:'),
        # A check from the chat leaves the Grove port alone while Sonar has it.
        ('081_main.cpp.inc', 'mode == UiMode::NfcNotes || mode == UiMode::Sonar;')]:
    assert text in (parts / file).read_text(), (file, text)
main = (src / 'main.cpp').read_text()
assert main.index('main_parts/081_main.cpp.inc') < main.index('main_parts/088_main.cpp.inc')

# The port is let go as the screen changes, before the next app wants it, and NFC Notes lets go
# in its own service, so Sonar is serviced after it.
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('gUiMode == UiMode::Sonar) stopSonar();') < change.index('gUiMode = mode;')
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceSonar();')

code = r'''
#include "sonar.h"
#include "air_music.h"
#include "lab.h"
#include "units.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <math.h>
#include <string>
#include <vector>
using std::max;
using std::min;

#define IRAM_ATTR
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
#define portENTER_CRITICAL_ISR(m) (void)(m)
#define portEXIT_CRITICAL_ISR(m) (void)(m)
constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, CHANGE = 3;

struct String : std::string {
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  String(unsigned long n) : std::string(std::to_string(n)) {}
  String(float f, int places) {
    char text[32];
    snprintf(text, sizeof(text), "%.*f", places, static_cast<double>(f));
    assign(text);
  }
  String operator+(const char* o) const { return String(std::string(*this) + o); }
  String operator+(const String& o) const { return String(std::string(*this) + std::string(o)); }
};
String operator+(const char* a, const String& b) { return String(std::string(a) + std::string(b)); }

uint32_t nowUs = 5000000;
uint32_t micros() { return nowUs; }
uint32_t millis() { return nowUs / 1000; }
units::System gUnits = units::System::Metric;  // The checks below read centimeters; US has its own.
units::System displayUnits() { return gUnits; }
void delayMicroseconds(uint32_t) {}

// The EXT header: G4 sends the HC-SR04's trigger and G6 hears its echo through an interrupt.
struct Pins {
  int touched = 0, attaches = 0, detaches = 0, pings = 0, trig = LOW, echo = LOW, trigMode = INPUT;
  void (*isr)() = nullptr;
} pins;
void pinMode(int pin, int mode) {
  assert(pin == 4 || pin == 6);
  ++pins.touched;
  if (pin == 4) pins.trigMode = mode;
}
void digitalWrite(int pin, int level) {
  assert(pin == 4);
  if (pins.trig == HIGH && level == LOW && pins.trigMode == OUTPUT) ++pins.pings;
  pins.trig = level;
}
int digitalRead(int pin) {
  assert(pin == 6);
  return pins.echo;
}
int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(int pin, void (*isr)(), int mode) {
  assert(pin == 6 && mode == CHANGE && !pins.isr);
  pins.isr = isr;
  ++pins.attaches;
}
void detachInterrupt(int pin) {
  assert(pin == 6 && pins.isr);
  pins.isr = nullptr;
  ++pins.detaches;
}

// The Grove port, as an I2C bus with the M5 unit on it or not.
struct FakeWire {
  bool present = true, open = false;
  uint32_t um = 0;
  int begins = 0, ends = 0, asks = 0, reads = 0, lastByte = -1;
  std::vector<int> out;
  void begin(uint8_t sda, uint8_t scl, uint32_t hz) {
    assert(sda == 2 && scl == 1 && hz == 100000U && !open);
    open = true;
    ++begins;
  }
  void end() {
    assert(open);
    open = false;
    ++ends;
  }
  void beginTransmission(uint8_t address) { assert(open && address == 0x57); }
  void write(uint8_t b) { lastByte = b; }
  int endTransmission() {
    ++asks;
    assert(lastByte == 1);
    return present ? 0 : 2;
  }
  int requestFrom(uint8_t address, uint8_t n) {
    assert(open && address == 0x57 && n == 3);
    if (!present) return 0;
    ++reads;
    out = {int(um >> 16) & 255, int(um >> 8) & 255, int(um) & 255};
    return 3;
  }
  int read() {
    const int b = out.front();
    out.erase(out.begin());
    return b;
  }
} Wire;

namespace fonts { const int Font0 = 0, Font2 = 2, Font6 = 6; }
enum { top_left, middle_center };
constexpr uint16_t TFT_WHITE = 0xffff;
constexpr uint16_t rgb565_local(int r, int g, int b) { return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3); }
template <class T> T clampValue(T n, T lo, T hi) { return max(lo, min(n, hi)); }

// Font2's real widths for the titles, from M5GFX. Other text gets a generous 8 px a character.
int widthOf(int font, const std::string& s) {
  static const std::map<std::string, int> font2 = {{"SONAR", 40}, {"SONAR RADAR", 86}};
  if (font == 2 && font2.count(s)) return font2.at(s);
  return int(s.size()) * (font == 0 ? 6 : font == 6 ? 32 : 8);
}
struct Box { std::string s; int x, y, w, h, font; };
struct Canvas {
  int x = 0, y = 0, font = 2, datum = top_left;
  std::vector<Box> text, pills;
  static void inside(int a, int b, int w, int h) { assert(a >= 0 && b >= 0 && w >= 0 && h >= 0 && a + w <= 240 && b + h <= 135); }
  void fillScreen(uint16_t) { text.clear(); pills.clear(); }
  void fillRoundRect(int a, int b, int w, int h, int, uint16_t) {
    inside(a, b, w, h);
    if (b == 10 && h == 15) pills.push_back({"", a, b, w, h, -1});
  }
  void drawRoundRect(int a, int b, int w, int h, int, uint16_t) { inside(a, b, w, h); }
  void fillRect(int a, int b, int w, int h, uint16_t) { inside(a, b, w, h); }
  void drawFastHLine(int a, int b, int w, uint16_t) { inside(a, b, w, 1); }
  void drawFastVLine(int a, int b, int h, uint16_t) { inside(a, b, 1, h); }
  void drawLine(int a, int b, int e, int f, uint16_t) { inside(min(a, e), min(b, f), 1, 1); inside(max(a, e), max(b, f), 1, 1); }
  void drawCircle(int a, int b, int r, uint16_t) { inside(a - r, b - r, 2 * r + 1, 2 * r + 1); }
  void fillCircle(int a, int b, int r, uint16_t c) { drawCircle(a, b, r, c); }
  // Only the part of the ring between the two angles, clockwise from the right.
  void drawArc(int a, int b, int r0, int, int from, int to, uint16_t) {
    for (int deg = from; deg != to; deg = (deg + 1) % 360) {
      const float rad = deg * 0.017453292f;
      inside(a + int(lroundf(cosf(rad) * r0)), b + int(lroundf(sinf(rad) * r0)), 1, 1);
    }
  }
  void setFont(const int* f) { font = *f; }
  void setTextSize(int) {}
  void setTextDatum(int d) { datum = d; }
  void setTextColor(uint16_t, uint16_t) {}
  void setCursor(int a, int b) { x = a; y = b; }
  int height() const { return font == 0 ? 8 : font == 6 ? 48 : 16; }
  int textWidth(const std::string& s) const { return widthOf(font, s); }
  void place(const std::string& s, int a, int b) {
    const int w = textWidth(s), h = font == 6 ? 36 : height();  // Font6 is digits only, with room above.
    inside(a, b, w, h);
    text.push_back({s, a, b, w, h, font});
  }
  void print(const std::string& s) { place(s, x, y); x += textWidth(s); }
  void drawString(const std::string& s, int a, int b) {
    if (datum == middle_center) place(s, a - textWidth(s) / 2, b - (font == 6 ? 18 : height() / 2));
    else place(s, a, b);
  }
  const Box* find(const std::string& s) const {
    for (const auto& box : text) if (box.s == s) return &box;
    return nullptr;
  }
} gCanvas;
int truncated = 0;
String fitCurrentFontToWidth(String s, int32_t w) {
  if (gCanvas.textWidth(s) <= w) return s;
  ++truncated;
  while (s.size() > 1 && gCanvas.textWidth(s + "..") > w) s.pop_back();
  return s + "..";
}
std::string footerText;
void drawTinyFooter(uint16_t, uint16_t, const String& s, int16_t x, int16_t y, int16_t w) {
  Canvas::inside(x, y - 4, w, 8);
  assert(widthOf(0, s) <= w);  // Whole, with no "..".
  footerText = s;
}
void renderLauncherOverlay() {}
void renderDebugOverlay() {}

std::string lastStatus;
std::vector<std::string> logLines;
void setStatus(const String& s, uint32_t) { lastStatus = s; }
void appendRuntimeLog(const String& area, const String& message, bool = false) { logLines.push_back(area + ": " + message); }
struct Tone { int hz, ms; };
std::vector<Tone> tones;
void focusTone(int hz, int ms) { tones.push_back({hz, ms}); }

struct Keyboard_Class { struct KeysState {}; };
enum class UiMode { Home, Sonar, AirMusic };
UiMode gUiMode = UiMode::Home;

// What Air Music's part needs, for its sensor object, which owns the Grove bus.
struct Speaker {
  bool tone(float, uint32_t, int, bool) { return true; }
  size_t isPlaying(uint8_t) const { return 0; }
  void stop(uint8_t) {}
};
struct { Speaker Speaker; } M5Cardputer;
bool gLauncherVisible = false;
uint32_t gLastRenderMs = 0;
bool focusToneAllowed() { return true; }
bool handleScreensaverWake() { return false; }
struct { void invalidate() {} } gUiFramePolicy;

// The Lab's setup, read from the card at start-up.
lab::Book gLabBook;
bool gSdReady = true, gMeshBusOwned = false;
int bookReads = 0;
void labReadBook() { ++bookReads; }
''' + (parts / '062_main.cpp.inc').read_text() + (parts / '076_main.cpp.inc').read_text() \
    + (parts / '088_main.cpp.inc').read_text() + r'''

// --- Driving it -------------------------------------------------------------------------------
void after(uint32_t ms) { nowUs += ms * 1000U; serviceSonar(); }
// The HC-SR04 answers its last ping, as its echo line rises and falls.
void echo(uint32_t us) {
  assert(pins.isr);
  pins.echo = HIGH; pins.isr();
  nowUs += us;
  pins.echo = LOW; pins.isr();
}
// A tenth of a second of the HC-SR04: a ping, then its echo if there is one. A look at Grove in
// the same pass can find the unit instead, and then there's no ping.
void hcTenth(uint32_t echoUs) {
  const int before = pins.pings;
  after(100);
  if (sonarOnUnit()) return;
  assert(pins.pings == before + 1);
  if (echoUs) echo(echoUs);
  after(1);
}
void open() { gUiMode = UiMode::Sonar; serviceSonar(); }
void leave() { gUiMode = UiMode::Home; serviceSonar(); }
void render(SonarViewMode view) {
  gSonar.viewMode = view;
  truncated = 0;
  renderSonarUi();
  assert(truncated == 0);
  // The sensor's name sits between the title and the state, clear of both.
  const Box* tag = gCanvas.find(sonarOnUnit() ? "M5 UNIT" : "HC-SR04");
  assert(tag && tag->y == 15 && gCanvas.pills.size() == 1);
  const Box* title = gCanvas.find(view == SonarViewMode::Radar ? "SONAR RADAR" : "SONAR");
  assert(title && title->x + title->w + 4 <= tag->x);
  assert(tag->x + tag->w + 4 <= gCanvas.pills[0].x);
  // No small text runs into other small text.
  for (size_t i = 0; i < gCanvas.text.size(); ++i) {
    for (size_t j = i + 1; j < gCanvas.text.size(); ++j) {
      const Box& a = gCanvas.text[i];
      const Box& b = gCanvas.text[j];
      if (a.font != 0 || b.font != 0) continue;
      const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
      if (!apart) fprintf(stderr, "overlap: '%s' and '%s'\n", a.s.c_str(), b.s.c_str());
      assert(apart);
    }
  }
}
std::string infoLine() {
  render(SonarViewMode::Proximity);
  for (const auto& box : gCanvas.text) if (box.y == 109) return box.s;
  assert(false);
  return "";
}

struct Fake {
  bool present = true;
  uint32_t um = 0;
  int triggers = 0, reads = 0;
  bool trigger() { ++triggers; return present; }
  bool read(uint32_t& out) { ++reads; if (!present) return false; out = um; return true; }
};

int main() {
  using sonar::Picker; using sonar::Source; using sonar::Step;
  // --- The picker on its own ---
  {
    Picker p; Fake unit; uint32_t mm = 0;
    // Grove first, before anything has been heard.
    assert(sonar::service(p, unit, 1000, false, mm) == Step::Found && p.source == Source::Unit && unit.triggers == 1);
    // The unit gets its whole measurement, then answers in millimeters.
    unit.um = 1234567;
    assert(sonar::service(p, unit, 1000 + sonar::kUnitMeasureMs - 1, false, mm) == Step::Nothing && unit.reads == 0);
    // The next measurement starts in the same pass.
    assert(sonar::service(p, unit, 1000 + sonar::kUnitMeasureMs, false, mm) == Step::Reading && mm == 1234);
    assert(unit.triggers == 2 && p.waiting);
    // Nothing in range is zero, or past the unit's reach.
    unit.um = 0;
    assert(sonar::service(p, unit, 1240, false, mm) == Step::Empty && unit.triggers == 3);
    unit.um = 4501000;
    assert(sonar::service(p, unit, 1360, false, mm) == Step::Empty);
    unit.um = 4500000;
    assert(sonar::service(p, unit, 1480, false, mm) == Step::Reading && mm == 4500);
    // R doesn't move it off a unit that's answering.
    p.lookNow();
    assert(p.source == Source::Unit && p.looked);
  }
  {
    // One bad transfer isn't an unplugged unit, and transfers are retried slowly.
    Picker p; Fake unit; uint32_t mm = 0;
    sonar::service(p, unit, 0, false, mm);
    unit.present = false;
    assert(sonar::service(p, unit, 120, false, mm) == Step::Nothing && p.fails == 1 && !p.waiting);
    assert(sonar::service(p, unit, 120 + sonar::kUnitRetryMs - 1, false, mm) == Step::Nothing && unit.triggers == 1);
    assert(sonar::service(p, unit, 120 + sonar::kUnitRetryMs, false, mm) == Step::Nothing && p.fails == 2);
    unit.present = true;
    // Only a reading clears them, as in the Lab.
    assert(sonar::service(p, unit, 420, false, mm) == Step::Nothing && p.fails == 2 && p.waiting);
    unit.um = 500000;
    assert(sonar::service(p, unit, 540, false, mm) == Step::Reading && mm == 500 && p.fails == 0 && p.waiting);
    // Four in a row is.
    unit.present = false;
    uint32_t t = 660;
    sonar::service(p, unit, t, false, mm);
    Step last = Step::Nothing;
    for (int i = 0; i < 3; ++i) { t += sonar::kUnitRetryMs; last = sonar::service(p, unit, t, false, mm); }
    assert(last == Step::Lost && p.source == Source::Echo && !p.waiting && p.fails == 0);
    // The HC-SR04 gets a second before Grove is looked at again.
    const int tried = unit.triggers;
    assert(sonar::service(p, unit, t + sonar::kLookMs - 1, false, mm) == Step::Nothing && unit.triggers == tried);
    assert(sonar::service(p, unit, t + sonar::kLookMs, false, mm) == Step::Nothing && unit.triggers == tried + 1);
    // An echoing HC-SR04 keeps it: Grove isn't looked at.
    unit.present = true;
    assert(sonar::service(p, unit, t + 5 * sonar::kLookMs, true, mm) == Step::Nothing && unit.triggers == tried + 1);
    // Plugged in again, it's found at the next look, or at once after R.
    p.lookNow();
    assert(sonar::service(p, unit, t + sonar::kLookMs + 1, false, mm) == Step::Found);
  }
  {
    // Millis wrapping around doesn't stall it.
    Picker p; Fake unit; uint32_t mm = 0;
    sonar::service(p, unit, 0xffffffffU - 10, false, mm);
    unit.um = 300000;
    assert(sonar::service(p, unit, sonar::kUnitMeasureMs, false, mm) == Step::Reading && mm == 300);
  }

  // --- The real app ---
  // With the unit on Grove, it's used, and the EXT header is never touched.
  Wire.um = 1234000;
  open();
  assert(gSonar.active && gSonar.groveAllowed && sonarOnUnit() && bookReads == 1);
  assert(Wire.begins == 1 && Wire.asks == 1 && pins.touched == 0 && !pins.isr);
  assert(lastStatus == "Sonar: M5 unit on Grove");
  render(SonarViewMode::Proximity);
  assert(gCanvas.find("--") && infoLine() == "waiting  |  M5 unit on Grove");
  after(sonar::kUnitMeasureMs);
  assert(Wire.reads == 1 && fabsf(gSonar.filteredCm - 123.4f) < 0.01f && gSonar.state == SonarProximityState::Clear);
  assert(infoLine() == "M5 ultrasonic unit on Grove" && gCanvas.find("123") && gCanvas.find("cm"));
  // In US units the same 123 cm reads 48 inches, on both views.
  gUnits = units::System::US; render(SonarViewMode::Proximity); assert(gCanvas.find("48") && gCanvas.find("in") && !gCanvas.find("123"));
  render(SonarViewMode::Radar); assert(gCanvas.find("48") && gCanvas.find("79in") && !gCanvas.find("200cm"));
  gUnits = units::System::Metric;
  render(SonarViewMode::Radar);
  assert(gCanvas.find("on Grove") && !gCanvas.find("10 Hz"));
  // Readings past the HC-SR04's 4 m still count, to the unit's 4.5 m.
  for (int i = 0; i < 12; ++i) { Wire.um = 4400000; after(sonar::kUnitMeasureMs); }
  assert(gSonar.filteredCm > 430.0f && pins.touched == 0);
  // A hand close in turns it red, and beeps if asked.
  handleSonarKey('b', Keyboard_Class::KeysState{});
  for (int i = 0; i < 12; ++i) { Wire.um = 100000; after(sonar::kUnitMeasureMs); }
  assert(gSonar.state == SonarProximityState::TooClose && !tones.empty() && tones.back().hz == 1480);
  render(SonarViewMode::Radar);
  assert(gCanvas.find("TOO CLOSE"));
  render(SonarViewMode::Proximity);
  // Nothing in range: no echo, counted, still on the unit.
  for (int i = 0; i < 5; ++i) { Wire.um = 0; after(sonar::kUnitMeasureMs); }
  assert(gSonar.state == SonarProximityState::NoEcho && sonarOnUnit());
  assert(infoLine() == "no echo x5  |  M5 unit on Grove");

  // Unplugged: after four bad transfers it goes back to listening on EXT, and pings at once.
  Wire.present = false;
  after(sonar::kUnitMeasureMs);
  for (int i = 0; i < 2; ++i) { after(sonar::kUnitRetryMs); assert(sonarOnUnit()); }
  after(sonar::kUnitRetryMs);
  assert(!sonarOnUnit() && lastStatus == "Sonar: unit gone, trying HC-SR04");
  assert(pins.isr && pins.attaches == 1 && pins.trigMode == OUTPUT && pins.pings == 1 && Wire.open);
  assert(!gSonar.hasFilteredDistance && infoLine() == "waiting for echo  |  EXT or Grove");
  render(SonarViewMode::Proximity);

  // An HC-SR04 that echoes is read at 10 Hz, and Grove is left alone while it does.
  Wire.present = true;
  const int asked = Wire.asks;
  for (int i = 0; i < 30; ++i) hcTenth(1450);  // 25 cm
  assert(!sonarOnUnit() && Wire.asks == asked);
  assert(fabsf(gSonar.filteredCm - 1450 / 58.0f) < 0.01f && gSonar.state == SonarProximityState::Object);
  assert(infoLine() == "echo 1.45ms  |  10 Hz");
  render(SonarViewMode::Radar);
  assert(gCanvas.find("echo 1.45ms") && gCanvas.find("10 Hz"));
  // Past the HC-SR04's 4 m is thrown away, as before.
  const float kept = gSonar.filteredCm;
  hcTenth(4500 * 58 / 10 + 58);
  assert(gSonar.filteredCm == kept);

  // When it goes quiet, Grove is looked at about once a second, and a unit there takes over.
  Wire.present = false;
  for (int i = 0; i < 5; ++i) hcTenth(0);
  int looks = Wire.asks;
  for (int i = 0; i < 30; ++i) hcTenth(0);
  assert(Wire.asks - looks >= 2 && Wire.asks - looks <= 4 && !sonarOnUnit());
  assert(gSonar.consecutiveTimeouts >= 30 && infoLine().rfind("no echo x", 0) == 0);
  assert(infoLine().find("|  EXT or Grove") != std::string::npos);
  Wire.present = true;
  for (int i = 0; i < 11 && !sonarOnUnit(); ++i) hcTenth(0);
  assert(sonarOnUnit() && pins.detaches == 1 && !pins.isr && pins.trigMode == INPUT && !gSonar.hasFilteredDistance);
  const int pinged = pins.pings, readings = Wire.reads;
  for (int i = 0; i < 10; ++i) after(sonar::kUnitMeasureMs);
  assert(pins.pings == pinged && Wire.reads == readings + 10);

  // R starts the readings over, keeps the beep and the view, and looks at Grove at once.
  Wire.present = false;
  for (int i = 0; i < 4; ++i) after(sonar::kUnitRetryMs);
  assert(!sonarOnUnit());
  hcTenth(0);
  Wire.present = true;
  gSonar.viewMode = SonarViewMode::Radar;
  handleSonarKey('r', Keyboard_Class::KeysState{});
  assert(lastStatus == "Sonar reset" && gSonar.beepEnabled && gSonar.viewMode == SonarViewMode::Radar);
  after(1);
  assert(sonarOnUnit());

  // Leaving lets go of both, at once and only once.
  const int ends = Wire.ends;
  leave();
  assert(!gSonar.active && !Wire.open && Wire.ends == ends + 1 && !pins.isr);
  stopSonar();
  assert(Wire.ends == ends + 1);
  // The next app can open the bus again.
  gAirSensor.open();
  assert(Wire.open);
  gAirSensor.close();

  // Coming back starts fresh, from Grove.
  pins = Pins{};
  Wire.um = 800000;
  open();
  assert(sonarOnUnit() && pins.touched == 0 && !gSonar.hasFilteredDistance);
  leave();

  // With the Lab's relays saved on Grove, the bus would switch them on, so it's left alone.
  gLabBook.direct.claim(lab::Part::Relay);
  const int begins = Wire.begins, asks = Wire.asks;
  open();
  assert(!gSonar.groveAllowed && !sonarOnUnit());
  for (int i = 0; i < 30; ++i) hcTenth(0);
  assert(Wire.begins == begins && Wire.asks == asks && !Wire.open);
  assert(infoLine().find("|  G4 -> G6") != std::string::npos);
  handleSonarKey('r', Keyboard_Class::KeysState{});
  after(1);
  assert(Wire.asks == asks);
  leave();
  gLabBook.direct = lab::Direct{};

  // The screens fit in every state, from both sensors.
  for (bool unit : {false, true}) {
    for (float cm : {0.0f, 3.0f, 14.0f, 50.0f, 150.0f, 399.0f, 449.0f}) {
      for (uint8_t misses : {0, 7, 255}) {
        gSonar = SonarRuntime{};
        gSonar.groveAllowed = true;
        gSonar.pick.source = unit ? Source::Unit : Source::Echo;
        gSonar.consecutiveTimeouts = misses;
        if (cm > 0) {
          gSonar.hasFilteredDistance = true;
          gSonar.filteredCm = cm;
          gSonar.lastPulseUs = static_cast<uint32_t>(cm * 58);
          gSonar.lastValidMs = millis();
        }
        gSonar.state = classifySonarState(millis());
        render(SonarViewMode::Proximity);
        render(SonarViewMode::Radar);
      }
    }
  }
  std::cout << "PASS Sonar: Grove first, unit readings to 4.5 m, empty readings, unplug after four bad transfers, "
               "HC-SR04 at 10 Hz with Grove left alone, a look a second when quiet, R looks at once, release on "
               "leaving, relays keep it off Grove, labels clear of the state in every view\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-sonar-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-I', str(src),
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
