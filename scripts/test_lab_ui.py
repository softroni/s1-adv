#!/usr/bin/env python3
"""Run the real Lab screens, keys and service loop on host, against simulated units on the bus."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'

code = r'''
#include "air_music.h"
#include "balloon.h"
#include "lab.h"
#include "lab_gps.h"
#include "launcher_icons.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
using std::min; using std::max;

// --- Arduino ---------------------------------------------------------------------------------
constexpr int HEX = 16;
constexpr float DEG_TO_RAD = 0.017453292f;
struct String : std::string {
  String() = default;
  String(const char* s) : std::string(s) {}
  String(const std::string& s) : std::string(s) {}
  template <class T, typename = std::enable_if_t<std::is_integral_v<T>>> String(T n) : std::string(std::to_string(+n)) {}
  String(unsigned char n, int base) { char b[8]; std::snprintf(b, sizeof(b), base == 16 ? "%x" : "%u", n); assign(b); }
  bool isEmpty() const { return empty(); }
  String substring(size_t from) const { return from >= size() ? String("") : String(std::string(*this).substr(from)); }
  size_t length() const { return size(); }
};
String operator+(const String& a, const String& b) { return String(std::string(a) + std::string(b)); }
String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
String operator+(const char* a, const String& b) { return String(a + std::string(b)); }

uint32_t nowMs = 1000;
uint32_t millis() { return nowMs; }
void delayMicroseconds(uint32_t) {}
template <class T> T clampValue(T x, T lo, T hi) { return std::min(hi, std::max(lo, x)); }
constexpr uint16_t rgb565_local(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
constexpr uint16_t TFT_BLACK = 0, TFT_WHITE = 0xffff;

// --- The canvas, which checks every draw and can write them down -------------------------------
namespace lgfx { struct IFont { int id; int charWidth; int height; }; }
namespace fonts {
const lgfx::IFont Font0{0, 6, 8}, Font2{2, 7, 16}, Font4{4, 14, 26}, Font6{6, 27, 48}, FreeSansBold9pt7b{9, 9, 18};
}
std::string gScene;
std::vector<std::string> gOps;
struct Canvas {
  const lgfx::IFont* font = &fonts::Font0;
  int x = 0, y = 0; uint16_t color = 0;
  std::vector<std::string> text;
  std::string pending;
  void on(int xx, int yy) {
    if (xx >= 0 && yy >= 0 && xx < 240 && yy < 135) return;
    std::fprintf(stderr, "off screen at %d,%d drawing [%s], font %d\n", xx, yy, pending.c_str(), font->id);
    std::abort();
  }
  void box(int a, int b, int w, int h) { assert(w > 0 && h > 0); on(a, b); on(a + w - 1, b + h - 1); }
  void log(const char* name, std::vector<int> v, uint16_t c) {
    std::string s = name; for (int n : v) s += " " + std::to_string(n); s += " " + std::to_string(c); gOps.push_back(s);
  }
  void fillScreen(uint16_t c) { text.clear(); gOps.clear(); log("clear", {}, c); }
  void setTextSize(int) {}
  void setTextWrap(bool) {}
  void drawBitmap(int a, int b, const uint8_t* bits, int w, int h, uint16_t c) { box(a, b, w, h); assert(bits);
    char p[20]; std::snprintf(p, sizeof(p), "%p", static_cast<const void*>(bits));
    std::string hex; for (int i = 0; i < 32; ++i) { char q[3]; std::snprintf(q, 3, "%02x", bits[i]); hex += q; }
    gOps.push_back("icon " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + " " + hex); }
  void setFont(const lgfx::IFont* f) { assert(f); font = f; }
  void setTextColor(uint16_t c) { color = c; }
  void setCursor(int a, int b) { x = a; y = b; }
  int textWidth(const String& s) const { return int(s.size()) * font->charWidth; }
  void print(const String& s) {
    pending = s; on(x, y); on(x + std::max(1, textWidth(s)) - 1, y + font->height - 1); pending.clear();
    text.push_back(s);
    gOps.push_back("text " + std::to_string(font->id) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(color) + " " + std::string(s));
    x += textWidth(s);
  }
  void drawRoundRect(int a, int b, int w, int h, int r, uint16_t c) { box(a, b, w, h); log("rrect_o", {a, b, w, h, r}, c); }
  void fillRoundRect(int a, int b, int w, int h, int r, uint16_t c) { box(a, b, w, h); log("rrect", {a, b, w, h, r}, c); }
  void fillRect(int a, int b, int w, int h, uint16_t c) { box(a, b, w, h); log("rect", {a, b, w, h}, c); }
  void drawFastHLine(int a, int b, int w, uint16_t c) { box(a, b, w, 1); log("rect", {a, b, w, 1}, c); }
  void drawFastVLine(int a, int b, int h, uint16_t c) { box(a, b, 1, h); log("rect", {a, b, 1, h}, c); }
  void fillCircle(int a, int b, int r, uint16_t c) { on(a - r, b - r); on(a + r, b + r); log("fcircle", {a, b, r}, c); }
  void drawCircle(int a, int b, int r, uint16_t c) { on(a - r, b - r); on(a + r, b + r); log("circle", {a, b, r}, c); }
  void drawLine(int a, int b, int e, int f, uint16_t c) { on(a, b); on(e, f); log("line", {a, b, e, f}, c); }
  void fillArc(int a, int b, int r0, int r1, float s, float e, uint16_t c) { on(a - r1, b - r1); on(a + r1, b);
    assert(s >= 180 && e <= 360 && e > s); log("arc", {a, b, r0, r1, int(s), int(e)}, c); }
  bool shows(const std::string& s) const { for (const auto& t : text) if (t.find(s) != std::string::npos) return true; return false; }
} gCanvas;
String fitCurrentFontToWidth(String s, int width) {
  while (gCanvas.textWidth(s) > width && s.size() > 1) s = String(s.substr(0, s.size() - 1));
  return s;
}

// --- The units ---------------------------------------------------------------------------------
struct Hub {
  bool here = true;
  int analog[6] = {}, level[6] = {0, 0, 0, 0, 0, 0};
  int leds[6] = {}, angle[6][2], written[6][2], mode = 0;
  uint8_t pulses[6][2];
  std::vector<std::vector<uint8_t>> log;
  uint32_t ledRgb[6][30];
  int reply[2] = {0, 0}, replyBytes = 0;
  Hub() { reset(); }
  void reset() { for (auto& p : angle) p[0] = p[1] = -1; for (auto& p : written) p[0] = p[1] = -1;
    std::memset(pulses, 0, sizeof(pulses)); std::memset(ledRgb, 0, sizeof(ledRgb)); log.clear(); }
  static int port(uint8_t reg) { const int high = reg >> 4; return high == 0xA ? 5 : high - 4; }
  void receive(const std::vector<uint8_t>& b) {
    log.push_back(b);
    const uint8_t reg = b[0];
    if (reg == 0xFA) { mode = b[1]; return; }
    if (reg == 0xFE) { reply[0] = 2; replyBytes = 1; return; }
    const int p = port(reg), cmd = reg & 0x0F;
    assert(p >= 0 && p < 6);
    if (b.size() == 1) {
      if (cmd == 6) { reply[0] = analog[p] & 255; reply[1] = analog[p] >> 8; replyBytes = 2; }
      else if (cmd == 4) { reply[0] = level[p]; replyBytes = 1; }
      else assert(cmd == 5);
      return;
    }
    if (cmd <= 1) { written[p][cmd] = b[1] & 1; pulses[p][cmd] = 0; }
    else if (cmd == 0x0C || cmd == 0x0D) { assert(b[1] <= 180); angle[p][cmd - 0x0C] = b[1]; pulses[p][cmd - 0x0C] = 1; }
    else if (cmd == 8) { leds[p] = b[1] | (b[2] << 8); }
    else if (cmd == 9) { assert(b.size() == 6); const int i = b[1] | (b[2] << 8); assert(i < leds[p]); ledRgb[p][i] = uint32_t(b[3] << 16 | b[4] << 8 | b[5]); }
    else if (cmd == 0x0A) { assert(b.size() == 8); const int from = b[1] | (b[2] << 8), n = b[3] | (b[4] << 8);
      assert(from + n <= leds[p]); for (int i = from; i < from + n; ++i) ledRgb[p][i] = uint32_t(b[5] << 16 | b[6] << 8 | b[7]); }
    else assert(false);
  }
} gHubUnit;
struct Units {
  std::set<uint8_t> present{0x57, 0x44, 0x63, 0x50};
  uint32_t um = 240000; uint16_t rawTemp = 26000, rawHumid = 27000; int8_t stickX = 47, stickY = -15; uint8_t stickButton = 1;
  uint16_t lux = 600; int commands = 0;
} gUnits;
struct WireClass {
  uint8_t address = 0; std::vector<uint8_t> out, in; size_t readAt = 0; bool held = false;
  void beginTransmission(uint8_t a) { address = a; out.clear(); }
  void write(uint8_t b) { out.push_back(b); }
  void write(const uint8_t* b, size_t n) { out.insert(out.end(), b, b + n); }
  uint8_t endTransmission(bool stop = true) {
    held = !stop; ++gUnits.commands;
    const bool here = address == pbhub::kAddress ? gHubUnit.here : gUnits.present.count(address) > 0;
    if (!here) return 2;
    if (address == pbhub::kAddress && !out.empty()) { assert(stop); gHubUnit.receive(out); }
    if (address == 0x63 && !out.empty()) assert(!stop);
    return 0;
  }
  uint8_t requestFrom(uint8_t a, uint8_t n) {
    in.clear(); readAt = 0;
    if (a == pbhub::kAddress) { if (!gHubUnit.here) return 0; assert(gHubUnit.replyBytes == n); in = {uint8_t(gHubUnit.reply[0]), uint8_t(gHubUnit.reply[1])}; }
    else if (!gUnits.present.count(a)) return 0;
    else if (a == 0x57) { for (int s = 16; s >= 0; s -= 8) in.push_back(uint8_t(gUnits.um >> s)); }
    else if (a == 0x63) { if (out[0] == 0x60) in = {uint8_t(gUnits.stickX), uint8_t(gUnits.stickY)}; else in = {gUnits.stickButton}; }
    else if (a == 0x23) { const uint32_t raw = gUnits.lux * 12U / 10U; in = {uint8_t(raw >> 8), uint8_t(raw)}; }
    in.resize(n);
    return n;
  }
  int read() { return in[readAt++]; }
} Wire;

// --- What the Lab borrows from the games ---------------------------------------------------------
struct AirSensor {
  int opens = 0, closes = 0; bool open_ = false;
  void open() { if (!open_) ++opens; open_ = true; }
  void close() { if (open_) ++closes; open_ = false; }
  bool trigger() { Wire.beginTransmission(0x57); Wire.write(uint8_t(1)); return Wire.endTransmission() == 0; }
  bool read(uint32_t& um) { if (!gUnits.present.count(0x57)) return false; um = gUnits.um; return true; }
} gAirSensor;
struct EnvSensors {
  bool envTrigger() { return gUnits.present.count(0x44) > 0; }
  bool envRead(uint8_t* r) {
    if (!gUnits.present.count(0x44)) return false;
    r[0] = uint8_t(gUnits.rawTemp >> 8); r[1] = uint8_t(gUnits.rawTemp); r[2] = balloon::crc8(r, 2);
    r[3] = uint8_t(gUnits.rawHumid >> 8); r[4] = uint8_t(gUnits.rawHumid); r[5] = balloon::crc8(r + 3, 2);
    return true;
  }
} gBalloonSensors;

// --- The rest of the board -----------------------------------------------------------------------
struct LedClass { int r = 0, g = 0, b = 0, shows = 0; bool isEnabled() { return true; }
  void setAllColor(int a, int c, int d) { r = a; g = c; b = d; } void display() { ++shows; } };
struct ImuClass { float ay = 0.5f; bool getAccel(float* x, float* y, float* z) { *x = 0; *y = ay; *z = 1; return true; } };
struct { ImuClass Imu; LedClass Led; } M5;
struct SpeakerClass { int stops = 0, tones = 0, queued = 0; float hz = 0;
  void stop(uint8_t) { ++stops; queued = 0; } int isPlaying(uint8_t) { return queued; }
  void tone(float f, uint32_t, uint8_t, bool) { hz = f; ++tones; } };
struct { SpeakerClass Speaker; } M5Cardputer;
constexpr int SERIAL_8N1 = 1;
struct SerialClass { std::string data; size_t at = 0; bool open = false; int buffer = 0, rx = -1, tx = -1, baud = 0;
  void setRxBufferSize(int n) { buffer = n; }
  void begin(int b, int, int r, int t) { baud = b; rx = r; tx = t; open = true; }
  void end() { open = false; } int available() { return int(data.size() - at); } int read() { return data[at++]; } } Serial1;
void gpsLine(const std::string& body) {
  uint8_t sum = 0; for (char c : body) sum ^= uint8_t(c);
  char tail[8]; std::snprintf(tail, sizeof(tail), "*%02X\r\n", sum); Serial1.data += "$" + body + tail;
}
constexpr const char* FILE_WRITE = "w";
struct Disk { std::map<std::string, std::vector<uint8_t>> files; bool broken = false; } gDisk;
struct File {
  std::string path; size_t at = 0; bool ok = false, writing = false;
  operator bool() const { return ok; }
  size_t size() { return gDisk.files[path].size(); }
  size_t read(uint8_t* out, size_t n) { auto& f = gDisk.files[path]; const size_t got = min(n, f.size() - at); std::memcpy(out, f.data() + at, got); at += got; return got; }
  size_t write(const uint8_t* b, size_t n) { auto& f = gDisk.files[path]; f.insert(f.end(), b, b + n); return n; }
  void close() {}
};
struct SdClass {
  bool exists(const char* p) { return gDisk.files.count(p) > 0; }
  File open(const char* p, const char* mode) { File f; f.path = p; f.ok = !gDisk.broken && (mode[0] == 'w' || exists(p)); return f; }
  bool remove(const char* p) { return gDisk.files.erase(p) > 0; }
  bool rename(const char* a, const char* b) { auto it = gDisk.files.find(a); if (it == gDisk.files.end()) return false; gDisk.files[b] = it->second; gDisk.files.erase(it); return true; }
} SD;
bool gSdReady = true, gImuEnabled = true;
bool ensureDirRecursive(SdClass&, const char*) { return !gDisk.broken; }
struct Keyboard_Class { struct KeysState { bool ctrl = false, alt = false, opt = false, enter = false, del = false; }; };
enum class UiMode { Home, NfcNotes, Mesh, LabDevices, LabRules };
UiMode gUiMode = UiMode::Home;
bool gLauncherVisible = false, gRecording = false, gRealtimeActive = false, gPlaybackActive = false, gToneAllowed = true;
uint32_t gLastRenderMs = 1;
struct { int changes = 0; void invalidate() { ++changes; } } gUiFramePolicy;
String gStatus; int gWakes = 0;
void setStatus(const String& s, int) { gStatus = s; }
bool focusToneAllowed() { return gToneAllowed; }
bool handleScreensaverWake() { ++gWakes; return false; }
void renderLauncherOverlay() {}
void renderDebugOverlay() {}
bool labMode(UiMode mode) { return mode == UiMode::LabDevices || mode == UiMode::LabRules; }
namespace { void beginLab(); void stopLab(); }
void setUiMode(UiMode mode) { if (labMode(gUiMode) && mode != gUiMode) stopLab(); gUiMode = mode; if (labMode(mode)) beginLab(); }
''' + (parts / '081_main.cpp.inc').read_text() + (parts / '082_main.cpp.inc').read_text() + r'''
// --- Driving it ----------------------------------------------------------------------------------
FILE* gDump = nullptr;
void show(const char* scene) {
  renderLabUi();
  if (!gDump) return;
  std::fprintf(gDump, "scene %s\n", scene);
  for (const auto& op : gOps) std::fprintf(gDump, "%s\n", op.c_str());
}
void run(int ms) { for (int t = 0; t < ms; t += 10) { nowMs += 10; serviceLab(); } }
void key(char k) { Keyboard_Class::KeysState keys; keys.enter = k == '\n'; keys.del = k == '\b'; assert(handleLabKey(k, keys)); }
void keys(const char* text) { for (const char* c = text; *c; ++c) { key(*c); run(60); } }
void esc() { assert(routeLabEscape()); run(60); }
const char UP = ';', DOWN = '.', LEFT = ',', RIGHT = '/', ENTER = '\n';
void pickDevice(Dev dev) { while (gLabUi.devRow != int(dev)) key(DOWN); key(ENTER); run(100); }
int sumLeds(int port) { int n = 0; for (uint32_t c : gHubUnit.ledRgb[port]) n += c != 0; return n; }
int ports(lab::Part part) { return gLabBook.wiring.portOf(part); }
void freshBoot() {
  if (gUiMode != UiMode::Home) setUiMode(UiMode::Home);
  gLabBookRead = false; gLabUi = LabUi{}; gLabStates[0] = lab::RuleState{};
}
void fingers() {
  gHubUnit.analog[ports(lab::Part::Knob)] = 2000;
  gHubUnit.analog[ports(lab::Part::Light)] = 1000;
}

void testDevices() {
  setUiMode(UiMode::LabDevices);
  assert(gLabOpen && Serial1.open && Serial1.rx == 15 && Serial1.tx == 13 && Serial1.baud == 115200);
  fingers();
  run(1500);
  // Everything on the port is found by asking, and the hub's ports get read by what's on them.
  for (uint8_t a : {uint8_t(Unit::Ultrasonic), uint8_t(Unit::Env), uint8_t(Unit::Joystick), uint8_t(Unit::Hub), uint8_t(Unit::Nfc)}) assert(gLabFound[a]);
  assert(!labFound(Unit::DLight) && gLabHubReady && gHubUnit.leds[ports(lab::Part::Strip)] == 30);
  assert(gLabHubFirmware == 2);
  assert(gLabInputs.get(Sig::Distance).value == 24);
  assert(gLabInputs.get(Sig::Knob).live && gLabInputs.get(Sig::Knob).value == 100);
  gHubUnit.analog[ports(lab::Part::Knob)] = 1000; run(100); assert(gLabInputs.get(Sig::Knob).value == 50);
  assert(gLabInputs.get(Sig::Temperature).live && gLabTempTenths == 244);  // 26000 raw.
  assert(gLabInputs.get(Sig::Humidity).value == 41);
  assert(gLabInputs.get(Sig::StickX).value == 37 && gLabInputs.get(Sig::StickY).value == -11);
  assert(gLabInputs.get(Sig::Tilt).value == 30);
  // Bright reads low on the analog unit, so it's flipped by default.
  assert(gLabInputs.get(Sig::Light).value == 0);
  gHubUnit.analog[ports(lab::Part::Light)] = 500; run(300); assert(gLabInputs.get(Sig::Light).value == 50);
  show("devices"); assert(gCanvas.shows("Ultrasonic") && gCanvas.shows("24 cm") && gCanvas.shows("4 ON THE PORT") == false);
  assert(gCanvas.shows("5 ON THE PORT"));
  // The list scrolls over everything, and wraps both ways.
  for (int i = 0; i < lab::kDevCount; ++i) { key(DOWN); show("scroll"); assert(gLabUi.devRow == (i + 1) % lab::kDevCount); }
  key(UP); assert(gLabUi.devRow == lab::kDevCount - 1); key(DOWN);
  gLabUi.devTop = 10; key(UP); key(DOWN); gLabUi.devTop = 0; show("devices-bottom");

  // Ultrasonic: the screen and its graph.
  pickDevice(Dev::Ultrasonic);
  for (int i = 0; i < 40; ++i) { gUnits.um = uint32_t((30 + 25 * std::sin(i / 3.0)) * 10000); run(260); }
  show("ultrasonic"); assert(gCanvas.shows("ANSWERING") && gCanvas.shows("last 30 seconds"));
  assert(gLabRangeSeen && gLabHighest > gLabLowest);
  gUnits.um = 0; run(300); assert(!gLabInputs.get(Sig::Distance).live); show("ultrasonic-empty"); assert(gCanvas.shows("no echo"));
  gUnits.um = 240000; run(300); assert(gLabInputs.get(Sig::Distance).live);
  esc(); assert(gLabUi.view == LabView::Devices);

  // Servo: arrows step 5, A and D step 1, C centers, and the hub gets it on the yellow pin.
  const int servo = ports(lab::Part::Servo);
  pickDevice(Dev::Servo); assert(gHubUnit.angle[servo][1] == -1);
  keys("///"); assert(gHubUnit.angle[servo][1] == 105 && gLabUi.servoAngle == 105);
  keys("d"); assert(gHubUnit.angle[servo][1] == 106); keys("a,"); assert(gHubUnit.angle[servo][1] == 100);
  gLabUi.servoAngle = 112; labDriveSet(Out::Servo, 112); run(60); show("servo");
  assert(gCanvas.shows("112") && gCanvas.shows("A servo cannot reply"));
  for (int i = 0; i < 60; ++i) keys("/"); assert(gHubUnit.angle[servo][1] == 180);
  for (int i = 0; i < 60; ++i) keys(","); assert(gHubUnit.angle[servo][1] == 0);
  show("servo-zero"); keys("c"); assert(gHubUnit.angle[servo][1] == 90);
  keys("s"); assert(gLabUi.sweeping);
  { bool up = false, down = false; int last = gHubUnit.angle[servo][1]; for (int i = 0; i < 300; ++i) { run(40); const int now = gHubUnit.angle[servo][1]; up |= now > last; down |= now < last; last = now; assert(now >= 0 && now <= 180); } assert(up && down); }
  keys("/"); assert(!gLabUi.sweeping);
  // Leaving lets it go limp.
  assert(gHubUnit.pulses[servo][1] == 1); esc(); assert(gHubUnit.pulses[servo][1] == 0 && gHubUnit.written[servo][1] == 0);

  // Strip: lit at the capped level as soon as it's opened, and off on leaving.
  const int strip = ports(lab::Part::Strip);
  pickDevice(Dev::Strip); run(100);
  assert(sumLeds(strip) == 30);
  { uint32_t top = 0; for (uint32_t c : gHubUnit.ledRgb[strip]) for (int s = 0; s <= 16; s += 8) top = std::max(top, (c >> s) & 255U); assert(top <= 25); }
  show("strip");
  keys("/"); assert(gLabUi.stripHue == 215);
  keys(";"); assert(gLabUi.stripBright == 45);
  keys("m"); run(300); assert(gLabStripTest == LabStripTest::Rainbow); show("strip-rainbow");
  { std::set<uint32_t> colors(std::begin(gHubUnit.ledRgb[strip]), std::end(gHubUnit.ledRgb[strip])); assert(colors.size() > 10); }
  keys("m"); run(500); assert(gLabStripTest == LabStripTest::Chase); assert(sumLeds(strip) == 1); { const int a = gLabChaseAt; run(240); assert(gLabChaseAt != a); } show("strip-chase");
  keys("m"); run(100); assert(gLabStripTest == LabStripTest::Count); assert(sumLeds(strip) == 1);
  keys("//"); assert(gLabUi.stripLed == 2 && gLabChaseAt == 2 && gHubUnit.ledRgb[strip][2] != 0 && sumLeds(strip) == 1); show("strip-count");
  keys(","); keys(","); keys(","); assert(gLabUi.stripLed == 29 && sumLeds(strip) == 1);
  keys("m"); run(200); assert(gLabStripTest == LabStripTest::Solid && sumLeds(strip) == 30);
  // The limit goes round three values, saves, and the timing key reaches the hub.
  keys("l"); assert(gLabBook.capPercent == 50); keys("l"); assert(gLabBook.capPercent == 100); keys("l"); assert(gLabBook.capPercent == 25);
  keys("t"); assert(gHubUnit.mode == 1 && gLabBook.showMode == 1); keys("t"); assert(gHubUnit.mode == 0);
  esc(); run(100); assert(sumLeds(strip) == 0 && gLabStripTest == LabStripTest::Solid);

  // Relays: white is 1 and yellow is 2, high closes them, and leaving opens both.
  const int relay = ports(lab::Part::Relay);
  pickDevice(Dev::Relay); run(400);
  key('1'); run(400); assert(gHubUnit.written[relay][0] == 1 && gHubUnit.written[relay][1] == 0);
  show("relay-1");
  key('2'); run(400); assert(gHubUnit.written[relay][1] == 1); show("relay-both");
  key('1'); run(400); assert(gHubUnit.written[relay][0] == 0 && gHubUnit.written[relay][1] == 1);
  { auto before = gHubUnit.log.size(); run(2000); int writes = 0; for (size_t i = before; i < gHubUnit.log.size(); ++i) writes += gHubUnit.log[i].size() > 1; assert(writes == 0); }  // Nothing resent while nothing changes.
  esc(); assert(gHubUnit.written[relay][0] == 0 && gHubUnit.written[relay][1] == 0);

  // The sensor screens, each shaped like its unit.
  pickDevice(Dev::Env); run(1200); show("env"); assert(gCanvas.shows("24.4") && gCanvas.shows("41%")); esc();
  pickDevice(Dev::Joystick); run(100); show("joystick"); assert(gCanvas.shows("37")); gUnits.stickButton = 0; run(100); show("joystick-pressed"); assert(gCanvas.shows("Pressed")); gUnits.stickButton = 1; esc();
  gHubUnit.analog[ports(lab::Part::Knob)] = 660; pickDevice(Dev::Knob); run(100); show("knob"); assert(gCanvas.shows("33%"));
  gHubUnit.analog[ports(lab::Part::Knob)] = 1400; run(100); show("knob-turning"); assert(gCanvas.shows("Yes"));
  for (int v : {0, 2000}) { gHubUnit.analog[ports(lab::Part::Knob)] = v; run(100); show("knob-ends"); } esc();
  pickDevice(Dev::Light); run(300); show("light"); keys("f"); run(300); assert(!gLabBook.lightFlipped && gLabInputs.get(Sig::Light).value == 50); keys("f"); esc();
  const int motion = ports(lab::Part::Motion);
  pickDevice(Dev::Motion); gLabMotionCount = 0; gHubUnit.level[motion] = 1; run(200); gHubUnit.level[motion] = 0; run(200); gHubUnit.level[motion] = 1; run(200);
  assert(gLabMotionCount == 2); show("motion"); assert(gCanvas.shows("Motion!")); gHubUnit.level[motion] = 0; run(200); esc();
  pickDevice(Dev::Hub); show("hub"); assert(gCanvas.shows("Mini Angle")); esc();
  pickDevice(Dev::Tilt); run(100); show("tilt"); esc();
  pickDevice(Dev::Led); keys(" "); assert(gLabUi.ledOn && M5.Led.g > 0); show("led"); keys("/"); esc(); assert(M5.Led.r == 0 && M5.Led.g == 0 && M5.Led.b == 0);
  pickDevice(Dev::Speaker); keys("/"); assert(M5Cardputer.Speaker.tones > 0 && M5Cardputer.Speaker.hz == 930); show("speaker");
  run(600); assert(!gLabDrive[uint8_t(Out::Tone)].on); esc();

  // GPS: says when nothing's there, then shows the Cap's fix.
  pickDevice(Dev::Gps); show("gps-silent"); assert(gCanvas.shows("Nothing from the GPS"));
  gpsLine("GNGGA,123519,4230.5000,N,07054.0000,W,1,08,0.9,180.0,M,0,M,,"); run(30); show("gps-searching"); assert(gCanvas.shows("SEARCHING") && gCanvas.shows("8"));
  gpsLine("GNRMC,123519,A,4230.5000,N,07054.0000,W,10.0,084.4,230394,,,A"); run(30); show("gps-fixed");
  assert(gCanvas.shows("42.50833") && gCanvas.shows("-70.90000") && gCanvas.shows("18.5 km/h"));
  assert(gLabInputs.get(Sig::Satellites).value == 8);
  esc();

  // NFC hands over to the app that does the real test, and that releases the bus and the GPS.
  const int opens = gAirSensor.closes;
  pickDevice(Dev::Nfc); show("nfc"); key(ENTER); assert(gUiMode == UiMode::NfcNotes && !gLabOpen && !Serial1.open && gAirSensor.closes == opens + 1);
  setUiMode(UiMode::LabDevices); fingers(); run(1500); assert(gLabOpen && gLabUi.view == LabView::Devices);

  // The digital light unit is used when it answers, and the analog one when it doesn't.
  gUnits.present.insert(0x23); run(1500); run(400);
  assert(labFound(Unit::DLight) && gLabLux == 600 && gLabInputs.get(Sig::Light).value == 60);
  pickDevice(Dev::Light); show("dlight"); assert(gCanvas.shows("600 lux")); esc();
  gUnits.present.erase(0x23); run(1500); assert(!labFound(Unit::DLight));
}

void testMissing() {
  // A unit going away: the screen says which, and whether the port is at fault.
  while (gLabUi.devRow != int(Dev::Ultrasonic)) key(DOWN);
  pickDevice(Dev::Ultrasonic); gUnits.present.erase(0x57); run(1500);
  assert(!labFound(Unit::Ultrasonic) && !gLabInputs.get(Sig::Distance).live);
  show("missing"); assert(gCanvas.shows("NO ANSWER") && gCanvas.shows("Nothing at 0x57") && gCanvas.shows("port is fine"));
  // It comes back by itself.
  gUnits.present.insert(0x57); run(1500); assert(labFound(Unit::Ultrasonic) && gLabInputs.get(Sig::Distance).live);
  esc();
  // The hub going away takes its parts with it, and it's set up again on return.
  const int strip = ports(lab::Part::Strip);
  pickDevice(Dev::Strip); run(200); assert(sumLeds(strip) == 30);
  gHubUnit.here = false; std::memset(gHubUnit.ledRgb, 0, sizeof(gHubUnit.ledRgb)); gHubUnit.leds[strip] = 0;
  run(1500); assert(!labFound(Unit::Hub) && !gLabInputs.get(Sig::Knob).live); show("hub-gone"); assert(gCanvas.shows("I/O hub not found"));
  gHubUnit.here = true; run(1500); assert(labFound(Unit::Hub) && gLabHubReady && gHubUnit.leds[strip] == 30 && sumLeds(strip) == 30);
  esc();
  // Nothing at all on the port.
  gUnits.present.clear(); gHubUnit.here = false; pickDevice(Dev::Env); run(1500); show("all-gone"); assert(gCanvas.shows("Nothing answers"));
  esc(); show("devices-empty");
  gUnits.present = {0x57, 0x44, 0x63, 0x50}; gHubUnit.here = true; run(1500); fingers();
}

void testWiring() {
  gLabUi.view = LabView::Devices; keys("w"); assert(gLabUi.view == LabView::Wiring); show("wiring");
  const int was = ports(lab::Part::Servo);
  // A row only ever changes itself, however far it's turned.
  gLabUi.wiringRow = 0; keys("/"); assert(gLabBook.wiring.port[0] == lab::Part::None); show("wiring-empty");
  for (int i = 0; i < 40; ++i) { keys("/"); assert(gLabBook.wiring.port[1] == lab::Part::Light && gLabBook.wiring.port[5] == lab::Part::Strip && ports(lab::Part::Servo) == was); }
  gLabBook.wiring = lab::Wiring{}; show("wiring");
  gLabUi.wiringRow = 5; keys(";"); assert(gLabUi.wiringRow == 4); keys("."); keys("."); assert(gLabUi.wiringRow == 0);
  gHubUnit.leds[ports(lab::Part::Strip)] = 0; esc(); assert(gLabUi.view == LabView::Devices);
  run(300); assert(gHubUnit.leds[ports(lab::Part::Strip)] == 30);  // The strip's port is set up again.
  // A part on no port says so, doesn't blame the hub, and drives nothing.
  gLabUi.wiringRow = 4; keys("w"); keys("/"); assert(ports(lab::Part::Servo) < 0); esc(); run(300);
  while (gLabUi.devRow != int(Dev::Servo)) key(DOWN);
  key(ENTER); run(100); show("not-set-up"); assert(gCanvas.shows("Not on a hub port") && !gCanvas.shows("not found"));
  { const auto before = gHubUnit.log.size(); keys("//"); int servoWrites = 0; for (size_t i = before; i < gHubUnit.log.size(); ++i) servoWrites += (gHubUnit.log[i][0] & 0x0E) == 0x0C; assert(servoWrites == 0); }
  esc(); gLabBook.wiring = lab::Wiring{}; gLabHubReady = false; run(300);
}

void testBuildAndPlay() {
  setUiMode(UiMode::LabRules); fingers(); run(1500);
  assert(gLabUi.view == LabView::Rules && gLabBook.count == 1 && !std::strcmp(gLabBook.rules[0].name, "Wave lights"));
  show("rules");  assert(gCanvas.shows("Wave lights") && gCanvas.shows("+ New rule"));
  // Open the starter and look at the card.
  key(ENTER); assert(gLabUi.view == LabView::Rule); gUnits.um = 240000; run(300); show("rule-card");
  assert(gCanvas.shows("Knob changes") && gCanvas.shows("Distance under 30 cm") && gCanvas.shows("WHEN ANY OF THESE"));
  // Every line can be reached, and the DO label can't.
  for (int i = 0; i < 24; ++i) { key(DOWN); LabCardRow rows[kLabMostCardRows]; labCardList(gLabBook.rules[0], rows); assert(rows[gLabUi.cardRow].line != LabLine::Then); show("rule-card-scroll"); }
  for (int i = 0; i < 24; ++i) { key(UP); LabCardRow rows[kLabMostCardRows]; labCardList(gLabBook.rules[0], rows); assert(rows[gLabUi.cardRow].line != LabLine::Then); }
  // ANY and ALL, and the hold time.
  gLabUi.cardRow = 1; key(ENTER); assert(!gLabBook.rules[0].any); key(ENTER); assert(gLabBook.rules[0].any);
  while (true) { LabCardRow rows[kLabMostCardRows]; labCardList(gLabBook.rules[0], rows); if (rows[gLabUi.cardRow].line == LabLine::Hold) break; key(DOWN); }
  key(RIGHT); assert(gLabBook.rules[0].holdMs == 2500); key(LEFT); show("rule-hold");
  for (int i = 0; i < 80; ++i) key(LEFT); assert(gLabBook.rules[0].holdMs == 0); show("rule-hold-zero"); for (int i = 0; i < 80; ++i) key(RIGHT); assert(gLabBook.rules[0].holdMs == lab::kMaxHoldMs);
  gLabBook.rules[0].holdMs = 2000;
  // Rename it from the keyboard.
  gLabUi.cardRow = 0; for (int i = 0; i < 20; ++i) key('\b'); assert(!gLabBook.rules[0].name[0]); show("rule-no-name");
  for (const char* c = "Wave lights!!!!!!!!"; *c; ++c) key(*c); assert(!std::strcmp(gLabBook.rules[0].name, "Wave lights"));
  esc(); assert(gLabUi.view == LabView::Rules && gDisk.files.count("/lab/rules.dat"));

  // Build a new rule by the steps: when it gets close, relay 1 closes.
  key(DOWN); assert(gLabUi.ruleRow == 1); key(ENTER); assert(gLabUi.view == LabView::Wizard && gLabWizard.fresh); show("step-sig");
  for (int i = 0; i < lab::kSigCount; ++i) { key(DOWN); show("step-sig-scroll"); } assert(gLabWizard.row == 0);
  key(ENTER); assert(gLabWizard.step == LabStep::Op); show("step-op");
  key(ENTER); assert(gLabWizard.step == LabStep::Value); gUnits.um = 270000; run(300); show("step-teach"); assert(gLabWizard.cond.value == 27);
  // Stepping out and back loses nothing.
  esc(); assert(gLabWizard.step == LabStep::Op); key(ENTER); run(100); key(RIGHT); key(RIGHT); assert(gLabWizard.cond.value == 29 && !gLabWizard.teaching);
  gUnits.um = 500000; run(300); show("step-teach-held"); assert(gLabWizard.cond.value == 29); gUnits.um = 240000; run(300);
  key(ENTER); assert(gLabWizard.step == LabStep::Out); show("step-out");
  for (int i = 0; i < lab::kOutCount; ++i) { key(DOWN); show("step-out-scroll"); }
  while (gLabWizard.row != int(Out::Relay1)) key(DOWN);
  key(ENTER); assert(gLabWizard.step == LabStep::How); show("step-how-relay");
  key(ENTER); assert(gLabUi.view == LabView::Rule && gLabBook.count == 2);
  const lab::Rule& made = gLabBook.rules[1];
  assert(!std::strcmp(made.name, "Rule 2") && made.conds == 1 && made.cond[0].sig == Sig::Distance && made.cond[0].op == lab::Op::Below && made.cond[0].value == 29);
  assert(made.actions == 1 && made.action[0].out == Out::Relay1 && made.action[0].how == lab::How::Set && made.action[0].value == 1);
  show("rule-made");
  // Add an action through the card: the servo set to a value.
  { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[1], rows); for (int i = 0; i < n; ++i) if (rows[i].line == LabLine::AddAction) gLabUi.cardRow = i; }
  key(ENTER); assert(gLabWizard.step == LabStep::Out && !gLabWizard.fresh); show("change-out"); key(ENTER); key(ENTER); assert(gLabWizard.step == LabStep::OutValue);
  key(RIGHT); key(RIGHT); show("step-value"); assert(gLabWizard.action.value == 100); key(ENTER); assert(gLabBook.rules[1].actions == 2 && gLabBook.rules[1].action[1].value == 100);
  // And one that follows, backwards.
  { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[1], rows); for (int i = 0; i < n; ++i) if (rows[i].line == LabLine::AddAction) gLabUi.cardRow = i; }
  key(ENTER); key(DOWN); key(ENTER); assert(gLabWizard.action.out == Out::StripColor); key(DOWN); key(DOWN); show("step-how"); key(ENTER); assert(gLabWizard.step == LabStep::Source); show("step-source");
  while (gLabWizard.row != int(Sig::Knob)) key(DOWN);
  key(ENTER); assert(gLabBook.rules[1].actions == 3 && gLabBook.rules[1].action[2].how == lab::How::FollowBack);
  // Change a line that's already there, and take one away. The last of a kind stays.
  { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[1], rows); for (int i = 0; i < n; ++i) if (rows[i].line == LabLine::Cond) gLabUi.cardRow = i; }
  key(ENTER); assert(gLabWizard.condAt == 0); while (gLabWizard.row != int(Sig::Motion)) key(DOWN); key(ENTER); show("step-op-onoff"); key(ENTER);
  assert(gLabBook.rules[1].conds == 1 && gLabBook.rules[1].cond[0].sig == Sig::Motion && gLabBook.rules[1].cond[0].op == lab::Op::Above);
  gLabBook.rules[1].cond[0] = {Sig::Distance, lab::Op::Below, 29};
  { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[1], rows); for (int i = 0; i < n; ++i) if (rows[i].line == LabLine::Cond) gLabUi.cardRow = i; }
  key('x'); assert(gLabBook.rules[1].conds == 1);
  { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[1], rows); for (int i = n - 1; i >= 0; --i) if (rows[i].line == LabLine::Action) gLabUi.cardRow = i; }
  key('x'); assert(gLabBook.rules[1].actions == 2 && gLabBook.rules[1].action[0].how == lab::How::Set && gLabBook.rules[1].action[0].out == Out::Servo);
  { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[1], rows); assert(gLabUi.cardRow < n && rows[gLabUi.cardRow].line != LabLine::Then); }
  key('x'); key(ENTER); esc(); esc();  // Backs out of the wizard unchanged.
  if (gLabUi.view != LabView::Rules) esc();
  assert(gLabUi.view == LabView::Rules);
  gLabBook.rules[1].action[0] = lab::Action{}; gLabBook.rules[1].action[0].out = Out::Relay1; gLabBook.rules[1].action[0].how = lab::How::Set; gLabBook.rules[1].action[0].value = 1;
  gLabBook.rules[1].action[1] = lab::Action{}; gLabBook.rules[1].action[1].out = Out::Led; gLabBook.rules[1].action[1].how = lab::How::Set; gLabBook.rules[1].action[1].value = 240;
  gLabBook.rules[1].actions = 2; gLabBook.rules[1].holdMs = 0;

  // Play: the owner's example, end to end through the hub.
  const int servo = ports(lab::Part::Servo), strip = ports(lab::Part::Strip), relay = ports(lab::Part::Relay);
  gUnits.um = 800000; gHubUnit.analog[ports(lab::Part::Knob)] = 1000; run(1200);
  keys("p"); assert(gLabPlaying && gLabUi.view == LabView::Play); run(800); show("board-waiting");
  assert(gCanvas.shows("WAITING") && !gLabStates[0].active && gHubUnit.written[relay][0] == 0);
  gHubUnit.angle[servo][1] = -1;
  // Turn the knob: the rule fires, the servo moves with it, and the strip lights.
  gHubUnit.analog[ports(lab::Part::Knob)] = 1500; run(300);
  assert(gLabStates[0].active && gHubUnit.angle[servo][1] == 135 && sumLeds(strip) == 30);
  show("board-firing"); gLabUi.playPage = 1; show("flow-firing"); gLabUi.playPage = 0;
  assert(gLabStates[0].fires == 1);
  // It holds for 2 seconds after, then lets the strip go. The servo stays where it was put.
  run(3000); assert(!gLabStates[0].active && sumLeds(strip) == 0 && gHubUnit.pulses[servo][1] == 1);
  // A hand close: the other condition, and the second rule's relay and LED.
  gUnits.um = 200000; run(300); assert(gLabStates[0].active && gLabStates[0].fires == 2 && gLabStates[1].active);
  assert(gHubUnit.written[relay][0] == 1 && M5.Led.b > 0); show("board-both"); gLabUi.playPage = 2; show("flow-two"); gLabUi.playPage = 0;
  // Both views, and paging wraps.
  keys("/"); assert(gLabUi.playPage == 1); keys("/"); assert(gLabUi.playPage == 2); keys("/"); assert(gLabUi.playPage == 0); keys(","); assert(gLabUi.playPage == 2); keys("/");
  // Pause stops everything at once and resumes.
  assert(gLabPlaying); keys(" "); assert(gLabPaused && gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0); show("board-paused"); assert(gCanvas.shows("PAUSED"));
  run(500); assert(gHubUnit.written[relay][0] == 0);
  keys(" "); run(500); assert(!gLabPaused && gHubUnit.written[relay][0] == 1);
  // Playing keeps the screen on, because the screensaver would stop the rules.
  { const int wakes = gWakes; run(3000); assert(gWakes >= wakes + 2); }
  // The menu on top stops everything, once, and leaves it paused.
  gLauncherVisible = true; { auto before = gHubUnit.log.size(); run(500); assert(gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0); const auto after = gHubUnit.log.size(); run(2000); assert(gHubUnit.log.size() == after && after > before); }
  gLauncherVisible = false; run(100); assert(gLabPaused); keys(" "); run(500); assert(gHubUnit.written[relay][0] == 1);
  // A rule that is off stays out of it.
  gLabBook.rules[1].on = false; run(500); assert(gHubUnit.written[relay][0] == 0); show("board-off"); assert(gCanvas.shows("OFF")); gLabUi.playPage = 2; show("flow-off"); gLabUi.playPage = 0; gLabBook.rules[1].on = true;
  // No flicker reaches the relay: a sensor bouncing across the line can't chatter it.
  { auto before = gHubUnit.log.size(); for (int i = 0; i < 100; ++i) { gUnits.um = i % 2 ? 200000 : 800000; run(130); }
    int toggles = 0; for (size_t i = before; i < gHubUnit.log.size(); ++i) { auto& b = gHubUnit.log[i]; if (b.size() == 2 && (b[0] >> 4) == pbhub::channel(uint8_t(relay)) >> 4 && (b[0] & 15) == 0) ++toggles; } assert(toggles <= 13000 / int(lab::kRelayGapMs) + 1); }
  // The hub vanishing mid-play doesn't stop the rest, and it picks back up.
  gUnits.um = 200000; run(300); gHubUnit.here = false; run(1500); assert(!labFound(Unit::Hub) && gLabPlaying);
  gHubUnit.here = true; std::memset(gHubUnit.ledRgb, 0, sizeof(gHubUnit.ledRgb)); fingers(); run(1500); assert(labFound(Unit::Hub) && gHubUnit.written[relay][0] == 1 && sumLeds(strip) == 30);
  // Leaving: relays open, strip off, servo limp, and the port is free.
  esc(); assert(!gLabPlaying && gLabUi.view == LabView::Rules && gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0 && gHubUnit.pulses[servo][1] == 0 && M5.Led.b == 0);
  const int closes = gAirSensor.closes;
  keys("p"); run(300); assert(gLabPlaying);
  setUiMode(UiMode::Home); assert(!gLabOpen && !gLabPlaying && gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0 && gAirSensor.closes == closes + 1 && !Serial1.open);
  { const auto before = gUnits.commands; run(500); assert(gUnits.commands == before); }  // Nothing touches the bus once it's closed.
}

void testSaving() {
  // Everything comes back after a restart.
  const std::string name = gLabBook.rules[1].name;
  setUiMode(UiMode::LabRules); run(100); key(ENTER); key(DOWN); esc(); setUiMode(UiMode::Home);
  { uint8_t bytes[lab::kFileBytes]; const size_t n = lab::encode(gLabBook, bytes); gDisk.files["/lab/rules.dat"].assign(bytes, bytes + n); }
  gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(100);
  assert(gLabBook.count == 2 && name == gLabBook.rules[1].name);
  setUiMode(UiMode::Home);
  // A power cut between the last two steps leaves the new file, and that one's used.
  gDisk.files["/lab/rules.new"] = gDisk.files["/lab/rules.dat"]; gDisk.files.erase("/lab/rules.dat");
  gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(100); assert(gLabBook.count == 2); setUiMode(UiMode::Home);
  // A damaged file falls back to the starter, not to nothing or a crash.
  gDisk.files.erase("/lab/rules.new"); gDisk.files["/lab/rules.dat"] = {1, 2, 3};
  gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(100); assert(gLabBook.count == 1 && !std::strcmp(gLabBook.rules[0].name, "Wave lights"));
  // A file too big for the buffer is refused, not overrun.
  setUiMode(UiMode::Home); gDisk.files["/lab/rules.dat"].assign(lab::kFileBytes + 100, 0x41);
  gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(100); assert(gLabBook.count == 1);
  // No card: it works until restart, and says so.
  gSdReady = false; gStatus = ""; keys(" "); assert(!gLabSaved && gStatus.find("No SD card") != std::string::npos); show("rules-unsaved");
  gSdReady = true; keys(" "); assert(gLabSaved);
  // Deleting takes two presses, and the last rule can go.
  key(ENTER); { LabCardRow rows[kLabMostCardRows]; const int n = labCardList(gLabBook.rules[0], rows); for (int i = 0; i < n; ++i) if (rows[i].line == LabLine::Delete) gLabUi.cardRow = i; }
  labKeepRowShown(gLabUi.cardRow, gLabUi.cardTop, kLabCardRows); show("rule-delete");
  key(ENTER); assert(gLabUi.deleteArmed); show("rule-delete-armed"); assert(gCanvas.shows("ENTER again")); key(UP); assert(!gLabUi.deleteArmed); key(DOWN);
  key(ENTER); key(ENTER); assert(gLabBook.count == 0 && gLabUi.view == LabView::Rules); show("rules-empty");
  keys("p"); assert(!gLabPlaying);  // Nothing to play.
  // Full up, the new row goes and the list stops growing.
  gLabBook.count = lab::kMaxRules; for (int i = 0; i < lab::kMaxRules; ++i) { gLabBook.rules[i] = lab::starterRule(); snprintf(gLabBook.rules[i].name, 16, "Long name no %d", i); }
  gLabUi.ruleRow = 0; gLabUi.ruleTop = 0; show("rules-full"); assert(!gCanvas.shows("+ New rule"));
  for (int i = 0; i < lab::kMaxRules; ++i) { key(DOWN); show("rules-full"); }
  keys("p"); run(300); assert(gLabPlaying);
  for (int i = 0; i < 8; ++i) { keys("."); show("board-full"); } keys(",");
  for (int page = 0; page <= lab::kMaxRules; ++page) { gLabUi.playPage = page; show("pages"); }
  esc();
  // A rule with everything in it fits its card and its flow.
  lab::Rule big; std::strcpy(big.name, "Fifteen letters"); big.conds = 3; big.actions = 3; big.any = false;
  big.cond[0] = {Sig::Temperature, lab::Op::Above, 45}; big.cond[1] = {Sig::StickX, lab::Op::Below, -100}; big.cond[2] = {Sig::Satellites, lab::Op::Changes, 0};
  big.action[0].out = Out::StripBright; big.action[0].how = lab::How::FollowBack; big.action[0].source = Sig::StickPress;
  big.action[1].out = Out::Tone; big.action[1].how = lab::How::Set; big.action[1].value = 2000; big.action[2].out = Out::StripLength; big.action[2].how = lab::How::Follow; big.action[2].source = Sig::Temperature;
  gLabBook.rules[0] = big; gLabUi.ruleIndex = 0; gLabUi.view = LabView::Rule; gLabUi.cardTop = 0; gLabUi.cardRow = 1; show("rule-big");
  for (int i = 0; i < 16; ++i) { key(DOWN); show("rule-big"); }
  gLabUi.view = LabView::Rules; gLabPlaying = true; gLabUi.view = LabView::Play; gLabUi.playPage = 1; show("flow-big"); gLabPlaying = false; gLabUi.view = LabView::Rules;
}

void testGuards() {
  // Keys with Ctrl and the like are left alone, and the voice path keeps the bus quiet.
  gLabUi.view = LabView::Rules; gLabUi.ruleRow = 0; Keyboard_Class::KeysState ctrl; ctrl.ctrl = true; assert(handleLabKey('.', ctrl)); assert(gLabUi.ruleRow == 0);
  gRecording = true; { const auto before = gUnits.commands; run(300); assert(gUnits.commands == before); } gRecording = false;
  gRealtimeActive = true; { const auto before = gUnits.commands; run(300); assert(gUnits.commands == before); } gRealtimeActive = false;
  gPlaybackActive = true; { const auto before = gUnits.commands; run(300); assert(gUnits.commands == before); } gPlaybackActive = false;
  // The speaker only sounds when it's allowed to.
  labDriveSet(Out::Tone, 500); gToneAllowed = false; { const int tones = M5Cardputer.Speaker.tones; run(100); assert(M5Cardputer.Speaker.tones == tones); } gToneAllowed = true; run(100); assert(gLabToneOn); labDriveClear(); run(100); assert(!gLabToneOn);
  // Esc on a first screen falls through to leave the app, and it does nothing outside the Lab.
  gLabUi.view = LabView::Rules; assert(!routeLabEscape()); gLabUi.view = LabView::Devices; assert(!routeLabEscape());
  gLabUi.view = LabView::Rule; gLauncherVisible = true; assert(!routeLabEscape()); gLauncherVisible = false; gLabUi.view = LabView::Rules;
  setUiMode(UiMode::Home); assert(!routeLabEscape());
  // Each menu entry opens on its own screen.
  setUiMode(UiMode::LabDevices); assert(gLabUi.view == LabView::Devices); setUiMode(UiMode::LabRules); assert(gLabUi.view == LabView::Rules && gLabOpen); setUiMode(UiMode::Home);
  // Stopping twice, and a service with the Lab closed, are harmless.
  stopLab(); stopLab(); serviceLab();
}

int main(int argc, char** argv) {
  if (argc > 1) gDump = std::fopen(argv[1], "w");
  for (bool& live : gLabHistoryLive) live = false;
  testDevices(); testMissing(); testWiring(); testBuildAndPlay(); testSaving(); testGuards();
  if (gDump) std::fclose(gDump);
  std::puts("PASS Lab screens: every device test, the wiring screen, building a rule by steps and by card, playing it through a simulated hub, pause and the menu, units leaving and returning, saving and damage, and every screen stays on the display");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-lab-ui-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    run = [str(p / 'test')]
    if os.environ.get('LAB_UI_DUMP'):
        run.append(os.environ['LAB_UI_DUMP'])
    subprocess.run(run, check=True)
