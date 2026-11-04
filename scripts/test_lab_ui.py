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
const lgfx::IFont Font0{0, 6, 8}, Font2{2, 7, 16}, Font4{4, 14, 26}, Font6{6, 27, 48}, FreeSansBold9pt7b{9, 9, 18}, efontCN_12{12, 6, 12};
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
  void drawRect(int a, int b, int w, int h, uint16_t c) { box(a, b, w, h); log("rrect_o", {a, b, w, h, 0}, c); }
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
  // The RCWL-9620 is a small chip running its own I2C. It can't answer while it measures, and it
  // only needs to acknowledge a real command, so these make the simulated one as awkward.
  bool sonicBusyWhileMeasuring = false, sonicIgnoresEmptyMessage = false; uint32_t sonicBusyUntil = 0;
} gUnits;
// The Grove wires themselves, for parts with no I2C. G1 is white and G2 is yellow.
constexpr int INPUT = 1, OUTPUT = 2, INPUT_PULLUP = 3, INPUT_PULLDOWN = 4, LOW = 0, HIGH = 1;
bool gBusOpen = false;
struct Grove {
  bool held = true;           // Something is really on the white wire.
  int adc = 1500; bool level = false;
  int mode[3] = {}, out[3] = {};
  uint32_t duty = 0; int channel = -1, attached = -1, bits = 0; uint32_t hz = 0;
  int ledsLit = 0, frames = 0; uint8_t lastGreen = 0, lastRed = 0, lastBlue = 0;
  bool scrambled = false;     // Bus traffic reached a strip on yellow, and nothing has been sent since.
} gGrove;
void pinMode(uint8_t pin, int mode) { assert(pin == 1 || pin == 2); assert(!gBusOpen); gGrove.mode[pin] = mode; }
// Anything on the I2C bus brings pull-ups, which hold the wire high whatever else is there.
bool busPullsUp() { return gHubUnit.here || !gUnits.present.empty(); }
int digitalRead(uint8_t pin) { assert(pin == 1 && !gBusOpen); if (busPullsUp()) return HIGH; if (gGrove.held) return gGrove.level; assert(gGrove.mode[1] == INPUT_PULLUP || gGrove.mode[1] == INPUT_PULLDOWN || gGrove.mode[1] == INPUT); return gGrove.mode[1] == INPUT_PULLUP; }
void digitalWrite(uint8_t pin, int value) { assert((pin == 1 || pin == 2) && !gBusOpen); gGrove.out[pin] = value; }
int analogRead(uint8_t pin) { assert(pin == 1 && !gBusOpen); if (busPullsUp()) return 4095; return gGrove.held ? gGrove.adc : (rand() & 4095); }
void ledcSetup(uint8_t channel, uint32_t hz, uint8_t bits) { gGrove.channel = channel; gGrove.hz = hz; gGrove.bits = bits; }
void ledcAttachPin(uint8_t pin, uint8_t channel) { assert(pin == 2 && !gBusOpen && channel == gGrove.channel); gGrove.attached = channel; }
void ledcDetachPin(uint8_t pin) { assert(pin == 2); gGrove.attached = -1; }
void ledcWrite(uint8_t channel, uint32_t duty) { assert(channel == gGrove.channel); gGrove.duty = duty; }
struct rmt_obj_t { int pin; }; rmt_obj_t gRmtObject; int gRmtLive = 0;
struct rmt_data_t { uint32_t level0, duration0, level1, duration1; };
constexpr bool RMT_TX_MODE = true; constexpr int RMT_MEM_64 = 1;
rmt_obj_t* rmtInit(int pin, bool tx, int) { assert(pin == 2 && tx && !gBusOpen); ++gRmtLive; return &gRmtObject; }
float rmtSetTick(rmt_obj_t*, float tick) { assert(tick == 100); return tick; }
bool rmtDeinit(rmt_obj_t*) { --gRmtLive; return true; }
bool rmtWriteBlocking(rmt_obj_t*, rmt_data_t* data, size_t size) {
  assert(size == 720 && gRmtLive == 1); ++gGrove.frames; gGrove.ledsLit = 0; gGrove.scrambled = false;
  for (int led = 0; led < 30; ++led) { uint8_t c[3] = {}; for (int bit = 0; bit < 24; ++bit) { assert(data[led * 24 + bit].duration0 + data[led * 24 + bit].duration1 == 12); if (data[led * 24 + bit].duration0 == 7) c[bit / 8] |= uint8_t(0x80 >> (bit % 8)); }
    if (c[0] || c[1] || c[2]) { ++gGrove.ledsLit; gGrove.lastGreen = c[0]; gGrove.lastRed = c[1]; gGrove.lastBlue = c[2]; } }
  return true;
}
struct WireClass {
  uint8_t address = 0; std::vector<uint8_t> out, in; size_t readAt = 0; bool held = false;
  void beginTransmission(uint8_t a) { address = a; out.clear(); }
  void write(uint8_t b) { out.push_back(b); }
  void write(const uint8_t* b, size_t n) { out.insert(out.end(), b, b + n); }
  uint8_t endTransmission(bool stop = true) {
    held = !stop; ++gUnits.commands; assert(gBusOpen);
    const bool here = address == pbhub::kAddress ? gHubUnit.here : gUnits.present.count(address) > 0;
    if (!here) return 2;
    if (address == 0x57) {
      if (gUnits.sonicBusyWhileMeasuring && nowMs < gUnits.sonicBusyUntil) return 2;
      if (gUnits.sonicIgnoresEmptyMessage && out.empty()) return 2;
      if (!out.empty()) gUnits.sonicBusyUntil = nowMs + 100;
    }
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
  void open() { if (!open_) ++opens; open_ = true; gBusOpen = true; gGrove.scrambled = true; }
  void close() { if (open_) ++closes; open_ = false; gBusOpen = false; }
  bool trigger() { Wire.beginTransmission(0x57); Wire.write(uint8_t(1)); return Wire.endTransmission() == 0; }
  bool read(uint32_t& um) { if (!gUnits.present.count(0x57)) return false; if (gUnits.sonicBusyWhileMeasuring && nowMs < gUnits.sonicBusyUntil) return false; um = gUnits.um; return true; }
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
struct { int r = 0, g = 0, b = 0; } gLed;
void neopixelWrite(uint8_t pin, uint8_t r, uint8_t g, uint8_t b) { assert(pin == 21); gLed.r = r; gLed.g = g; gLed.b = b; }
// The LED runs off the screen light, which is only steady at 100%. This is what part 009 does with it.
bool gLedWantsLight = false, gFakeScreenOff = false;
uint32_t gLedPowerSinceMs = 0;
int gBacklightPct = 70;
void applyDisplayBrightness() {
  gBacklightPct = gFakeScreenOff ? 0 : gLedWantsLight ? 100 : 70;
  if (gBacklightPct < 100) gLedPowerSinceMs = 0;
  else if (!gLedPowerSinceMs) gLedPowerSinceMs = std::max<uint32_t>(millis(), 1);
}
struct ImuClass { float ax = 0, ay = 0.5f; bool getAccel(float* x, float* y, float* z) { *x = ax; *y = ay; *z = 1; return true; } };
struct { ImuClass Imu; } M5;
struct SpeakerClass { int stops = 0, tones = 0, queued = 0; float hz = 0;
  void stop(uint8_t) { ++stops; queued = 0; } int isPlaying(uint8_t) { return queued; }
  void tone(float f, uint32_t, uint8_t, bool) { hz = f; ++tones; } };
struct { SpeakerClass Speaker; } M5Cardputer;
constexpr int SERIAL_8N1 = 1;
struct SerialClass { std::string data; size_t at = 0; bool open = false; int buffer = 0, rx = -1, tx = -1, baud = 0;
  void setRxBufferSize(int n) { buffer = n; }
  void begin(int b, int, int r, int t) { baud = b; rx = r; tx = t; open = true; }
  void end() { open = false; } int available() { return int(data.size() - at); } int read() { return data[at++]; } } Serial1;
// NVS, kept in memory: what the Lab keeps on the device rather than on the SD card.
std::map<std::string, std::map<std::string, int32_t>> gNvs; bool gNvsWritable = true;
struct Preferences { std::string ns; bool ro = true;
  bool begin(const char* n, bool readOnly) { if (readOnly && !gNvs.count(n)) return false; ns = n; ro = readOnly; return true; }
  void end() {}
  int32_t getInt(const char* k, int32_t d) { auto& m = gNvs[ns]; return m.count(k) ? m[k] : d; }
  bool getBool(const char* k, bool d) { return getInt(k, d) != 0; }
  size_t putInt(const char* k, int32_t v) { if (ro || !gNvsWritable) return 0; gNvs[ns][k] = v; return 4; }
  size_t putBool(const char* k, bool v) { if (ro || !gNvsWritable) return 0; gNvs[ns][k] = v; return 1; } };
void gpsLine(const std::string& body) {
  uint8_t sum = 0; for (char c : body) sum ^= uint8_t(c);
  char tail[8]; std::snprintf(tail, sizeof(tail), "*%02X\r\n", sum); Serial1.data += "$" + body + tail;
}
constexpr const char* FILE_WRITE = "w";
constexpr const char* FILE_APPEND = "a";
bool gClockOk = true;
bool hasValidSystemTime() { return gClockOk; }
struct Disk { std::map<std::string, std::vector<uint8_t>> files; bool broken = false, gone = false; } gDisk;  // gone: the card stopped answering.
struct File {
  std::string path; size_t at = 0; bool ok = false, writing = false;
  operator bool() const { return ok; }
  size_t size() { return gDisk.files[path].size(); }
  size_t read(uint8_t* out, size_t n) { auto& f = gDisk.files[path]; const size_t got = min(n, f.size() - at); std::memcpy(out, f.data() + at, got); at += got; return got; }
  size_t write(const uint8_t* b, size_t n) { auto& f = gDisk.files[path]; f.insert(f.end(), b, b + n); return n; }
  bool seek(size_t to) { at = to; return true; }
  bool isDirectory() const { return path == "/"; }
  void close() {}
};
struct SdClass {
  bool exists(const char* p) { return !gDisk.gone && gDisk.files.count(p) > 0; }
  File open(const char* p, const char* mode = "r") { File f; f.path = p; f.ok = !gDisk.broken && !gDisk.gone && (mode[0] != 'r' || exists(p) || f.path == "/"); return f; }
  bool remove(const char* p) { return gDisk.files.erase(p) > 0; }
  bool rename(const char* a, const char* b) { auto it = gDisk.files.find(a); if (it == gDisk.files.end()) return false; gDisk.files[b] = it->second; gDisk.files.erase(it); return true; }
} SD;
bool gSdReady = true, gImuEnabled = true;
bool ensureDirRecursive(SdClass&, const char*) { return !gDisk.broken; }
int gSpeechFetches = 0, gSpeechPlays = 0; bool gSpeechWorks = true; std::string gSpeechPath, gSpeechText;
bool waitForInboxToFinish(uint32_t) { return true; }
bool speechToWavFile(const String& text, const String& path) { ++gSpeechFetches; gSpeechText = text; if (!gSpeechWorks) return false; gDisk.files[std::string(path)] = {1, 2}; return true; }
void render() {}
struct Keyboard_Class { struct KeysState { bool ctrl = false, alt = false, opt = false, enter = false, del = false; }; };
enum class UiMode { Home, NfcNotes, Mesh, SmartLamp, IrRemote, LabDevices, LabRules, LabSequences, AirMusic, Flappy, Balloon, BallCatch, Nes };
UiMode gUiMode = UiMode::Home;
bool gLauncherVisible = false, gRecording = false, gRealtimeActive = false, gPlaybackActive = false, gToneAllowed = true;
bool gRealtimeExclusiveMode = false, gMeshBusOwned = false, gActionsFollowSpeech = false, gRadioBusy = false;
bool radioBusy() { return gRadioBusy; }
bool vlcBusy() { return false; }
bool synthAudioBusy() { return false; }
bool synthForegroundAllowed() { return false; }
struct { bool open = false; bool busOpen() const { return open; } } gNfcRadio;
bool gSubmitting = false, gThinking = false, gWifiReady = true;
struct VoiceNote {};
std::vector<VoiceNote> gVoiceNotes;
int gSelectedVoiceNote = 0;
bool gVoiceNotesBehindCard = false;
String gHubHost = "bridge";
int gVoiceTaps = 0;
void toggleVoiceByTap() { ++gVoiceTaps; if (gRecording) { gRecording = false; gThinking = true; } else gRecording = true; }
bool startPlaybackStreamFromWavFile(const String& path, const String&) { ++gSpeechPlays; gSpeechPath = path; gPlaybackActive = true; return true; }
// The module itself runs in test_voice_tool.py. This is what a tool's screen sees of it.
struct VoiceToolAnswer { bool ok = false; String heard, reply, actionsJson, problem; };
bool gVoiceStarting = false;
bool voiceToolStarting() { return gVoiceStarting; }
bool voiceToolListening() { return gRecording; }
bool voiceToolWorking() { return gThinking; }
String gQueuedVoiceReplySpeech;
bool gVoiceReplySpeechBusy = false;
std::vector<std::string> gReplies;  // What the chat was told.
std::vector<bool> gRepliesSpoken;
void postDeviceReply(const String& text, bool speak) { gReplies.push_back(text.c_str()); gRepliesSpoken.push_back(speak); }
String cleanReplyForDevice(const String& text) { return text; }
bool gActionsRun = false;
const void* gActionRule = nullptr;  // Offered the way the real device action does.
int gActionRepeats = 1;
void offerFromAction(const void* rule);
const void* gActionSequence = nullptr;  // The same, for a sequence.
bool offerSequenceFromAction(const void* sequence);
const void* gActionOnce = nullptr;  // The same, for a check.
void startOnceFromAction(const void* once);
bool executeDeviceActionsJson(const String& json) { if (gActionOnce) { startOnceFromAction(gActionOnce); return true; } if (gActionSequence) return offerSequenceFromAction(gActionSequence); if (gActionRule) for (int i = 0; i < gActionRepeats; ++i) offerFromAction(static_cast<const char*>(gActionRule) + 0); return !std::string(json).empty() && gActionsRun; }
std::vector<String> wrapText(const String& raw, int) {
  std::vector<String> lines; std::string text = raw;
  for (size_t at = 0; at < text.size(); at += 38) lines.push_back(String(text.substr(at, 38).c_str()));
  return lines;
}
struct { bool power = false, online = false; uint8_t brightness = 0; } gSmartLamp;
std::vector<std::string> gLampSent;
void smartLampSendControl(const String& packet, const String&) { gLampSent.push_back(packet); }
String smartLampControlPacket(uint8_t command) { return String("power") + String(int(command)); }
bool gTclPower = false, gDaikinPower = false; int gTclSends = 0, gDaikinSends = 0;
void sendTclAcState(const String&) { ++gTclSends; }
void sendDaikinAcState(const String&) { ++gDaikinSends; }
uint32_t gLastRenderMs = 1;
struct { int changes = 0; void invalidate() { ++changes; } } gUiFramePolicy;
String gStatus; int gWakes = 0;
void setStatus(const String& s, int) { gStatus = s; }
void logf(const char*, ...) {}
bool focusToneAllowed() { return gToneAllowed; }
bool handleScreensaverWake() { ++gWakes; return false; }
void renderLauncherOverlay() {}
void renderDebugOverlay() {}
bool labMode(UiMode mode) { return mode == UiMode::LabDevices || mode == UiMode::LabRules || mode == UiMode::LabSequences; }
namespace { void beginLab(); void stopLab(); void labOnceLeaving(UiMode next); }
void setUiMode(UiMode mode) { if (mode != gUiMode) labOnceLeaving(mode); if (labMode(gUiMode) && mode != gUiMode) stopLab(); gUiMode = mode; if (labMode(mode)) beginLab(); }
''' + (parts / '081_main.cpp.inc').read_text() + (parts / '082_main.cpp.inc').read_text() + (parts / '084_main.cpp.inc').read_text() + (parts / '085_main.cpp.inc').read_text() + r'''
// --- Driving it ----------------------------------------------------------------------------------
FILE* gDump = nullptr;
lab::Rule gOfferSteps[4]; int gOfferStepAt = -1;
bool offerSequenceFromAction(const void* sequence) { return labOfferSequence(*static_cast<const lab::Sequence*>(sequence), ""); }
void offerFromAction(const void* rule) { labOfferRule(gOfferStepAt >= 0 ? gOfferSteps[gOfferStepAt++] : *static_cast<const lab::Rule*>(rule)); }
void show(const char* scene) {
  renderLabUi();
  if (!gDump) return;
  std::fprintf(gDump, "scene %s\n", scene);
  for (const auto& op : gOps) std::fprintf(gDump, "%s\n", op.c_str());
}
void run(int ms) { for (int t = 0; t < ms; t += 10) { nowMs += 10; serviceLabOnce(); serviceLabPlayAsked(); serviceLab(); } }
void pressNow(char k) { Keyboard_Class::KeysState keys; keys.enter = k == '\n'; keys.del = k == '\b'; assert(handleLabKey(k, keys)); }
void key(char k) { gLabOfferSettled = true; pressNow(k); }
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
  assert(gLabInputs.get(Sig::Knob).live && gLabInputs.get(Sig::Knob).value == 0);  // Top voltage is its left end.
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
  assert(gCanvas.shows("112") && gCanvas.shows("Watch it  N moves"));
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
  gHubUnit.analog[ports(lab::Part::Knob)] = 660; pickDevice(Dev::Knob); run(100); show("knob"); assert(gCanvas.shows("67%"));
  gHubUnit.analog[ports(lab::Part::Knob)] = 1400; run(100); show("knob-turning"); assert(gCanvas.shows("Yes"));
  for (int v : {0, 2000}) { gHubUnit.analog[ports(lab::Part::Knob)] = v; run(100); show("knob-ends"); } esc();
  pickDevice(Dev::Light); run(300); show("light"); keys("f"); run(300); assert(!gLabBook.lightFlipped && gLabInputs.get(Sig::Light).value == 50); keys("f"); esc();
  const int motion = ports(lab::Part::Motion);
  pickDevice(Dev::Motion); gLabMotionCount = 0; gHubUnit.level[motion] = 1; run(200); gHubUnit.level[motion] = 0; run(200); gHubUnit.level[motion] = 1; run(200);
  assert(gLabMotionCount == 2); show("motion"); assert(gCanvas.shows("Motion!")); gHubUnit.level[motion] = 0; run(200); esc();
  pickDevice(Dev::Hub); show("hub"); assert(gCanvas.shows("Mini Angle")); esc();
  pickDevice(Dev::Tilt); run(100); show("tilt"); esc();
  assert(gBacklightPct == 70);
  pickDevice(Dev::Led); keys(" "); assert(gLabUi.ledOn && gLed.g > 0 && gBacklightPct == 100); show("led"); keys("/"); esc(); assert(gLed.r == 0 && gLed.g == 0 && gLed.b == 0 && gBacklightPct == 70);
  pickDevice(Dev::Speaker); keys(";"); assert(M5Cardputer.Speaker.tones > 0 && M5Cardputer.Speaker.hz == 930 && gLabUi.page == 0); show("speaker");
  run(600); assert(!gLabDrive[uint8_t(Out::Tone)].on); esc();

  // GPS: says when nothing's there, then shows the Cap's fix.
  pickDevice(Dev::Gps); show("gps-silent"); assert(gCanvas.shows("Nothing from the GPS"));
  gpsLine("GNGGA,123519,4230.5000,N,07054.0000,W,1,08,0.9,180.0,M,0,M,,"); run(30); show("gps-searching"); assert(gCanvas.shows("SEARCHING") && gCanvas.shows("8"));
  gpsLine("GNRMC,123519,A,4230.5000,N,07054.0000,W,10.0,084.4,230394,,,A"); run(30); show("gps-fixed");
  assert(gCanvas.shows("42.50833") && gCanvas.shows("-70.90000") && gCanvas.shows("18.5 km/h"));
  assert(gLabInputs.get(Sig::Satellites).value == 8);
  esc();

  // The smartLamp and the AC point at the apps that test them fully.
  pickDevice(Dev::Lamp); show("lamp-searching"); assert(gCanvas.shows("SEARCHING")); gSmartLamp.online = true; show("lamp"); assert(gCanvas.shows("smartLamp found"));
  key(ENTER); assert(gUiMode == UiMode::SmartLamp); setUiMode(UiMode::LabDevices); fingers(); run(1500);
  pickDevice(Dev::Ac); show("ac"); assert(gCanvas.shows("IR air conditioner")); key(ENTER); assert(gUiMode == UiMode::IrRemote); setUiMode(UiMode::LabDevices); fingers(); run(1500);
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

// Every page, played the way a person would, with the bench's fake parts.
void testPages() {
  setUiMode(UiMode::LabDevices); fingers(); run(500); gLabUi.view = LabView::Devices;
  const auto flip = [](const char* scene, const char* word) { key(RIGHT); run(150); show(scene); assert(gCanvas.shows(word)); };
  const auto tone = []() { return gLabDrive[int(Out::Tone)].on; };
  // Arrows in a hint are drawn by hand, because text drawing skips them.
  pickDevice(Dev::Ultrasonic); assert(gLabUi.page == 0); show("ultrasonic-paged"); assert(gCanvas.shows(" page  R reset") && !gCanvas.shows("\x1b") && !gCanvas.shows("\x1a"));
  // Pages wrap both ways, and a part's screen always opens on its first page.
  key(LEFT); assert(gLabUi.page == 2); key(RIGHT); assert(gLabUi.page == 0);
  gUnits.um = 300000; run(300);
  flip("page-parking", "PARKING"); assert(gCanvas.shows("30 cm") && gCanvas.shows("Getting close") && gCanvas.shows("B sound"));
  assert(!tone()); key('b'); gUnits.um = 100000; run(300); int tones = 0; { bool was = false; for (int i = 0; i < 100; ++i) { run(10); tones += tone() && !was; was = tone(); } } assert(tones >= 5);
  gUnits.um = 60000; run(300); show("page-parking-near"); assert(gCanvas.shows("Stop!") && gCanvas.shows("B quiet") && gCanvas.shows("SOUND ON")); key('b'); assert(!tone());
  gUnits.um = 0; run(300); renderLabUi(); assert(gCanvas.shows("Nothing in range")); gUnits.um = 240000; run(300);
  for (int i = 0; i < 20; ++i) { gUnits.um = uint32_t((30 + 20 * std::sin(i / 3.0)) * 10000); run(260); }
  flip("page-graph", "LAST 30 SEC"); assert(gCanvas.shows("LOW") && gCanvas.shows("AVG") && gCanvas.shows("HIGH"));
  key('r'); run(10); gUnits.um = 0; run(300); gUnits.um = 240000; esc(); assert(gLabUi.view == LabView::Devices);
  pickDevice(Dev::Ultrasonic); assert(gLabUi.page == 0); esc();
  // ENV III: a dot on a map of how the room feels, and either reading on the graph.
  pickDevice(Dev::Env); run(1200); flip("page-comfort", "Comfortable"); assert(gCanvas.shows("DEW POINT"));
  gUnits.rawHumid = 8000; run(1200); renderLabUi(); assert(gCanvas.shows("Dry air"));
  gUnits.rawHumid = 27000; gUnits.rawTemp = 12000; run(1200); renderLabUi(); assert(gCanvas.shows("Chilly")); gUnits.rawTemp = 26000; run(1200);
  key(RIGHT); run(300); assert(gLabHistorySig == Sig::Humidity); key('t'); run(600); assert(gLabHistorySig == Sig::Temperature); show("page-env-graph"); assert(gCanvas.shows("Temperature") && gCanvas.shows("T other reading")); esc();
  // Joystick: drawing leaves a line, and pressing the stick clears it.
  gUnits.stickX = 120; gUnits.stickY = 60; pickDevice(Dev::Joystick); key(RIGHT); run(800); assert(gLabPlay.trailCount > 5 && gLabPlay.penX > kLabArenaW / 2); gUnits.stickX = -100; gUnits.stickY = -90; run(500); gUnits.stickX = 40; gUnits.stickY = 120; run(600); show("page-draw"); assert(gCanvas.shows("DRAW"));
  for (int i = 0; i < 400; ++i) run(10); assert(gLabPlay.trailCount == kLabTrailPoints);  // It keeps the newest once it's full.
  gUnits.stickButton = 0; run(150); assert(gLabPlay.trailCount == 0); gUnits.stickButton = 1;
  // The target counts a hit, moves, and remembers the quickest.
  key(RIGHT); run(100); const auto aim = [](int x, int y) { gUnits.stickX = int8_t((x * 200 / 231 - 100) * 127 / 100); gUnits.stickY = int8_t((100 - y * 200 / 101) * 127 / 100); };
  gUnits.stickX = 0; gUnits.stickY = 0; run(100); assert(gLabPlay.hits == 0 || gLabPlay.bestMs);
  { const int before = gLabPlay.hits, x = gLabPlay.targetX, y = gLabPlay.targetY; aim(x, y); run(200); assert(gLabPlay.hits == before + 1 && gLabPlay.bestMs); assert(gLabPlay.targetX != x || gLabPlay.targetY != y); }
  aim(gLabPlay.targetX, gLabPlay.targetY); run(100); show("page-target"); assert(gCanvas.shows("Best ") && gCanvas.shows(" HITS")); gUnits.stickX = 47; gUnits.stickY = -15; run(100); esc();
  // Mini Angle: its own graph, then following the moving zone.
  const int knob = ports(lab::Part::Knob); pickDevice(Dev::Knob); assert(gLabHistorySig == Sig::Knob); key(RIGHT); run(800); show("page-knob-graph"); assert(gCanvas.shows("Knob") && !gCanvas.shows("-")); key(RIGHT);
  for (int i = 0; i < 80; ++i) { gHubUnit.analog[knob] = uint16_t((100 - gLabPlay.zone) * gLabKnobRange.top / 100); run(40); }
  show("page-steady"); assert(gCanvas.shows("In the zone") && gLabPlay.heldMs >= 1000 && gLabPlay.bestHeldMs >= gLabPlay.heldMs);
  gHubUnit.analog[knob] = gLabPlay.zone > 50 ? gLabKnobRange.top : 0; run(200); renderLabUi(); assert(gCanvas.shows("Find the zone") && gLabPlay.heldMs == 0 && gLabPlay.bestHeldMs >= 1000); fingers(); esc();
  // Light: the sky follows it.
  const int light = ports(lab::Part::Light); pickDevice(Dev::Light); key(RIGHT); gHubUnit.analog[light] = 1000; run(500); show("page-sky-night"); assert(gCanvas.shows("Night"));
  gHubUnit.analog[light] = 450; run(500); show("page-sky-day"); assert(gCanvas.shows("Daylight")); gHubUnit.analog[light] = 0; run(500); show("page-sky-sun"); assert(gCanvas.shows("Bright sun"));
  key(RIGHT); run(300); show("page-light-graph"); assert(gCanvas.shows("Light")); gHubUnit.analog[light] = 500; run(300); esc();
  // PIR: arm it, walk away, and it goes off once, until told to stop.
  const int motion = ports(lab::Part::Motion); gHubUnit.level[motion] = 0; pickDevice(Dev::Motion); key(RIGHT); run(150); show("page-guard"); assert(gCanvas.shows("GUARD") && gCanvas.shows("G arm it"));
  key('g'); run(200); show("page-guard-arming"); assert(gLabPlay.guard == LabGuard::Arming && gCanvas.shows("Leave in 5"));
  gHubUnit.level[motion] = 1; run(1000); assert(gLabPlay.guard == LabGuard::Arming); gHubUnit.level[motion] = 0;  // You get time to leave.
  run(4200); show("page-guard-armed"); assert(gLabPlay.guard == LabGuard::Armed && gCanvas.shows("Watching") && !tone());
  gHubUnit.level[motion] = 1; run(300); show("page-guard-tripped"); assert(gLabPlay.guard == LabGuard::Tripped && gLabPlay.trips == 1 && gCanvas.shows("Motion!") && tone());
  gHubUnit.level[motion] = 0; run(1000); assert(gLabPlay.guard == LabGuard::Tripped && tone());  // It stays on after they've gone.
  key('g'); assert(gLabPlay.guard == LabGuard::Off && !tone()); run(100); assert(!tone());
  // Leaving the page stops the alarm too.
  key('g'); gLabPlay.guardMs -= 6000; run(100); gHubUnit.level[motion] = 1; run(200); assert(tone()); key(RIGHT); assert(!tone()); gHubUnit.level[motion] = 0; run(300); assert(!tone()); show("page-motion-graph"); assert(gCanvas.shows("LAST 30 SEC")); esc();
  // Tilt: flat is level, and rolling the ball into the hole scores. The ball rolls downhill, and on
  // the Cardputer a positive side reading is its left edge down and a positive front-back one its near edge.
  M5.Imu.ax = 0; M5.Imu.ay = 0; pickDevice(Dev::Tilt); key(RIGHT); run(300); show("page-level"); assert(gCanvas.shows("Level"));
  M5.Imu.ax = 0.34f; M5.Imu.ay = -0.26f; run(300); show("page-level-tilted"); assert(gCanvas.shows("Tilted") && gLabTiltSide == 19); key(RIGHT);
  gLabPlay.targetX = kLabArenaW - 12; gLabPlay.targetY = kLabArenaH - 12; run(100);  // Out of its way, so it can't score yet.
  { const float startX = gLabPlay.ballX, startY = gLabPlay.ballY; run(600); assert(gLabPlay.ballX < startX && gLabPlay.ballY < startY); run(4000); assert(gLabPlay.ballX <= kLabArenaW - 6.0f && gLabPlay.ballX >= 5.0f && gLabPlay.ballY <= kLabArenaH - 6.0f && gLabPlay.ballY >= 5.0f); }
  assert(gLabPlay.goals == 0); gLabPlay.targetX = int(gLabPlay.ballX); gLabPlay.targetY = int(gLabPlay.ballY); run(100); assert(gLabPlay.goals == 1); show("page-roll"); assert(gCanvas.shows("1 GOAL")); M5.Imu.ax = 0; M5.Imu.ay = 0.5f; run(200); esc();
  // Servo: N, because its arrows turn it. A move runs by itself, and stops with the page.
  const int servo = ports(lab::Part::Servo); pickDevice(Dev::Servo); keys("c"); key('n'); run(100); show("page-moves"); assert(gLabUi.page == 1 && gCanvas.shows("MOVES") && gCanvas.shows("Sweep") && gCanvas.shows("Tick"));
  key(ENTER); assert(gLabPlay.moving); { std::set<int> seen; for (int i = 0; i < 100; ++i) { run(40); seen.insert(gHubUnit.angle[servo][1]); } assert(seen.size() > 10); show("page-moves-running"); assert(gCanvas.shows("MOVING")); }
  key(ENTER); key(DOWN); key(ENTER); assert(gLabPlay.move == LabMove::Wave); { int lo = 180, hi = 0; run(2000); for (int i = 0; i < 100; ++i) { run(40); lo = std::min(lo, gHubUnit.angle[servo][1]); hi = std::max(hi, gHubUnit.angle[servo][1]); } assert(lo >= 50 && hi <= 130 && hi > lo); }
  key(RIGHT); assert(gLabPlay.moveSpeed == 3); for (int i = 0; i < 5; ++i) key(LEFT); assert(gLabPlay.moveSpeed == 1); key(ENTER); key(DOWN); key(ENTER); assert(gLabPlay.move == LabMove::Tick); { std::set<int> seen; run(100); for (int i = 0; i < 500; ++i) { run(10); seen.insert(gHubUnit.angle[servo][1]); } assert(seen.size() >= 3 && seen.size() <= 7); for (int a : seen) assert(a % 30 == 0); }
  key('n'); assert(gLabUi.page == 0); { const int a = gHubUnit.angle[servo][1]; run(1200); assert(gHubUnit.angle[servo][1] == a); } esc();
  // Speaker: up and down change the pitch, so the arrows can turn to a piano on the number keys.
  pickDevice(Dev::Speaker); show("speaker-paged"); assert(gCanvas.shows("Turn the page for a piano"));
  { const int hz = gLabUi.toneHz; key(DOWN); assert(gLabUi.toneHz == hz - 50 && tone() && gLabUi.page == 0); key(UP); assert(gLabUi.toneHz == hz && gLabUi.page == 0); }
  key(RIGHT); assert(gLabUi.page == 1 && !tone()); run(50); key('3'); assert(tone() && gLabDrive[int(Out::Tone)].value == 659); show("page-piano"); assert(gCanvas.shows("PIANO")); run(400); assert(!tone()); key('9'); assert(!tone()); key(ENTER); assert(!tone()); { const int hz = gLabUi.toneHz; key(UP); assert(gLabUi.toneHz == hz && !tone()); }
  key(LEFT); assert(gLabUi.page == 0); key('n'); assert(gLabUi.page == 1); key('n'); assert(gLabUi.page == 0); esc();
  // A page that's lost its part shows the same help as the first one.
  pickDevice(Dev::Ultrasonic); key(RIGHT); gUnits.present.erase(0x57); run(2000); show("page-missing"); assert(gCanvas.shows("Nothing at 0x57")); gUnits.present.insert(0x57); run(2000); renderLabUi(); assert(gCanvas.shows("PARKING")); esc();
  // Parts with one screen ignore N.
  pickDevice(Dev::Relay); key('n'); assert(gLabUi.page == 0); esc();
  // The Dashboard: everything live, with a graph of the last minute.
  gLabUi.view = LabView::Devices; show("devices-dash"); assert(gCanvas.shows("D dash"));
  key('d'); assert(gLabUi.view == LabView::Dash); for (int i = 0; i < 12; ++i) { gUnits.um = uint32_t((30 + 20 * std::sin(i)) * 10000); run(2000); } show("dash"); assert(gCanvas.shows(" LIVE") && gCanvas.shows("Distance") && gCanvas.shows("1 of 2"));
  key(RIGHT); show("dash-2"); assert(gLabUi.dashPage == 1 && gCanvas.shows("2 of 2") && gCanvas.shows("Tilt")); key(RIGHT); assert(gLabUi.dashPage == 0); key(LEFT); assert(gLabUi.dashPage == 1);
  assert(labAskOnScreen() && !labChangeOnScreen());
  // An empty hub port is noise, so it stays off until the part has been opened.
  gLabPartUsed[int(lab::Part::Knob)] = false; assert(!labDashShows(Sig::Knob) && labDashShows(Sig::Distance)); gLabPartUsed[int(lab::Part::Knob)] = true; assert(labDashShows(Sig::Knob));
  esc(); assert(gLabUi.view == LabView::Devices);
  // With nothing live it says what to do.
  { const lab::Inputs kept = gLabInputs; gLabInputs = lab::Inputs{}; gLabUi.view = LabView::Dash; renderLabUi(); assert(gCanvas.shows("Nothing reading yet")); gLabInputs = kept; gLabUi.view = LabView::Devices; }
  run(100);
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

bool same(const lab::Rule& a, const lab::Rule& b) {
  lab::Book x, y; x.count = y.count = 1; x.rules[0] = a; y.rules[0] = b;
  uint8_t l[lab::kFileBytes], r[lab::kFileBytes]; const size_t n = lab::encode(x, l); return n == lab::encode(y, r) && !std::memcmp(l, r, n);
}
void goToRow(int row) { for (int i = 0; i < 40 && gLabUi.ruleRow != row; ++i) key(DOWN); assert(gLabUi.ruleRow == row); }
void pickRow(int row) { for (int i = 0; i < 40 && gLabWizard.row != row; ++i) key(DOWN); assert(gLabWizard.row == row); key(ENTER); }
LabStep stepNow() { return gLabWizard.step; }

void testBuildRule() {
  setUiMode(UiMode::LabRules); fingers(); run(1500);
  assert(gLabUi.view == LabView::Rules && gLabBook.count == 1 && !std::strcmp(gLabBook.rules[0].name, "Wave lights"));
  show("rules"); assert(gCanvas.shows("Wave lights") && gCanvas.shows("+ New rule") && gCanvas.shows("Say a rule") && gCanvas.shows("Recipes"));
  // The steps, from the top, for "if something's close and the knob turns, close relay 1 and turn the LED blue".
  goToRow(1); key(ENTER); assert(gLabUi.view == LabView::Wizard && gLabWizard.index < 0 && stepNow() == LabStep::Sig); show("step-sig");
  for (int i = 0; i < lab::kSigCount; ++i) { key(DOWN); show("step-sig-scroll"); } assert(gLabWizard.row == 0);
  key(ENTER); assert(stepNow() == LabStep::Op); show("step-op");
  key(ENTER); assert(stepNow() == LabStep::Value); gUnits.um = 270000; run(300); show("step-teach"); assert(gLabWizard.rule.cond[0].value == 27);
  // Esc walks back a step and forward again loses nothing.
  esc(); assert(stepNow() == LabStep::Op); key(ENTER); run(100); key(RIGHT); key(RIGHT); assert(gLabWizard.rule.cond[0].value == 29 && !gLabWizard.teaching);
  gUnits.um = 500000; run(300); show("step-teach-held"); assert(gLabWizard.rule.cond[0].value == 29);
  key(' '); run(100); assert(gLabWizard.teaching); show("step-teach"); assert(gLabWizard.rule.cond[0].value == 50);
  key(' '); key(LEFT); assert(gLabWizard.rule.cond[0].value == 49 && !gLabWizard.teaching); gUnits.um = 240000; run(300); for (int i = 0; i < 20; ++i) key(LEFT); assert(gLabWizard.rule.cond[0].value == 29);
  key(ENTER); assert(stepNow() == LabStep::MoreConds && gLabWizard.row == 0); show("step-more-conds");
  // A second thing to watch.
  key(DOWN); key(ENTER); assert(stepNow() == LabStep::Sig && gLabWizard.cond == 1); show("step-sig-second");
  pickRow(int(Sig::Knob)); assert(stepNow() == LabStep::Op); pickRow(2); assert(stepNow() == LabStep::MoreConds && gLabWizard.rule.cond[1].op == lab::Op::Changes);
  // "That's all", then ANY or ALL, which only comes up with two.
  key(ENTER); assert(stepNow() == LabStep::Join); show("step-join"); key(DOWN); key(ENTER); assert(!gLabWizard.rule.any && stepNow() == LabStep::Out && gLabWizard.keepConds == 2); show("step-out");
  for (int i = 0; i < lab::kOutCount; ++i) { key(DOWN); show("step-out-scroll"); }
  // Back from the first action reaches the join, and forward again.
  esc(); assert(stepNow() == LabStep::Join && gLabWizard.row == 1); key(ENTER);
  pickRow(int(Out::Relay1)); assert(stepNow() == LabStep::How); show("step-how-relay");
  key(ENTER); assert(stepNow() == LabStep::MoreActions && gLabWizard.rule.action[0].value == 1); show("step-more-actions");
  key(DOWN); key(ENTER); assert(stepNow() == LabStep::Out && gLabWizard.action == 1);
  pickRow(int(Out::Led)); assert(stepNow() == LabStep::How); show("step-how"); key(ENTER); assert(stepNow() == LabStep::OutValue);
  key(RIGHT); key(RIGHT); show("step-value"); assert(gLabWizard.rule.action[1].value == 120); key(ENTER); assert(stepNow() == LabStep::MoreActions);
  key(ENTER); assert(stepNow() == LabStep::Hold && gLabWizard.keepActions == 2); show("step-hold");
  for (int i = 0; i < 80; ++i) key(LEFT); assert(gLabWizard.rule.holdMs == 0); show("step-hold-zero"); for (int i = 0; i < 80; ++i) key(RIGHT); assert(gLabWizard.rule.holdMs == lab::kMaxHoldMs); show("step-hold-long");
  for (int i = 0; i < 60; ++i) key(LEFT); assert(gLabWizard.rule.holdMs == 0);
  key(ENTER); assert(stepNow() == LabStep::Name); show("step-name");
  // Typing replaces the suggestion, and a name can't run past its room.
  for (const char* c = "Close and turn!!!!!!!"; *c; ++c) key(*c); show("step-name-typed"); assert(!std::strcmp(gLabWizard.rule.name, "Close and turn"));
  // Nothing is saved until the last Enter, and walking out drops it all.
  assert(gLabBook.count == 1); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) esc(); assert(gLabUi.view == LabView::Rules && gLabBook.count == 1);
}

// Walking a rule with Enter alone changes nothing.
void enterThrough(int index) {
  gLabUi.ruleRow = index; key(ENTER); assert(gLabUi.view == LabView::Wizard && gLabWizard.index == index);
  for (int i = 0; i < 60 && gLabUi.view == LabView::Wizard; ++i) key(ENTER);
  assert(gLabUi.view == LabView::Rules);
}
void addRuleByHand() {
  // The same walk as above, all the way to the end.
  goToRow(gLabBook.count); key(ENTER); key(ENTER); key(ENTER); run(100); key(' '); for (int i = 0; i < 80; ++i) key(RIGHT); for (int i = 0; i < 71; ++i) key(LEFT); assert(gLabWizard.rule.cond[0].value == 29);
  key(ENTER); key(DOWN); key(ENTER); pickRow(int(Sig::Knob)); pickRow(2); key(ENTER); key(ENTER); assert(stepNow() == LabStep::Out && gLabWizard.rule.any);
  pickRow(int(Out::Relay1)); key(ENTER); key(DOWN); key(ENTER); pickRow(int(Out::Led)); key(ENTER); key(RIGHT); key(RIGHT); key(ENTER); key(ENTER);
  assert(stepNow() == LabStep::Hold); for (int i = 0; i < 60; ++i) key(LEFT); key(ENTER);
  for (const char* c = "Close and turn"; *c; ++c) key(*c);
  key(ENTER); assert(gLabUi.view == LabView::Rules && gLabBook.count == 2 && gLabUi.ruleRow == 1 && gLabSaved && gDisk.files.count("/lab/rules.dat"));
  const lab::Rule& made = gLabBook.rules[1];
  assert(!std::strcmp(made.name, "Close and turn") && made.conds == 2 && made.any && made.holdMs == 0);
  assert(made.cond[0].sig == Sig::Distance && made.cond[0].op == lab::Op::Below && made.cond[0].value == 29 && made.cond[1].sig == Sig::Knob && made.cond[1].op == lab::Op::Changes);
  assert(made.actions == 2 && made.action[0].out == Out::Relay1 && made.action[0].value == 1 && made.action[1].out == Out::Led && made.action[1].how == lab::How::Set && made.action[1].value == 120);
  show("rules-two");
}
void testEditRule() {
  addRuleByHand();
  // Enter on a rule walks the same steps with every answer already picked.
  const lab::Rule before = gLabBook.rules[1], first = gLabBook.rules[0];
  enterThrough(1); assert(same(gLabBook.rules[1], before)); enterThrough(0); assert(same(gLabBook.rules[0], first));
  // The steps open on what's there, including the second line.
  gLabUi.ruleRow = 1; key(ENTER); assert(stepNow() == LabStep::Sig && gLabWizard.row == int(Sig::Distance)); show("edit-sig"); assert(gCanvas.shows("Close and turn"));
  key(ENTER); assert(gLabWizard.row == 0); key(ENTER); assert(stepNow() == LabStep::Value && !gLabWizard.teaching);
  // A stored number stays put, whatever the sensor is doing now.
  gUnits.um = 700000; run(300); assert(gLabWizard.rule.cond[0].value == 29); gUnits.um = 240000; run(300);
  key(ENTER); assert(stepNow() == LabStep::MoreConds && gLabWizard.row == 1); show("edit-more"); key(ENTER); assert(gLabWizard.cond == 1 && gLabWizard.row == int(Sig::Knob)); key(ENTER); assert(gLabWizard.row == 2);
  // Changing a line partway keeps the rest.
  esc(); esc(); esc(); assert(stepNow() == LabStep::Value); key(RIGHT); key(ENTER); key(ENTER); key(ENTER); key(ENTER); assert(stepNow() == LabStep::MoreConds);
  // "That's all" here drops the lines after it, but only once the walk is finished.
  for (int i = 0; i < 60 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabBook.rules[1].cond[0].value == 30 && gLabBook.rules[1].conds == 2);
  gLabUi.ruleRow = 1; key(ENTER); key(ENTER); key(ENTER); key(ENTER); assert(stepNow() == LabStep::MoreConds); key(UP); key(ENTER);
  assert(stepNow() == LabStep::Out && gLabWizard.keepConds == 1 && gLabBook.rules[1].conds == 2);  // Not even a Join step for one line.
  for (int i = 0; i < 60 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabBook.rules[1].conds == 1);
  // Esc leaves a rule exactly as it was, whatever was changed on the way.
  const lab::Rule kept = gLabBook.rules[1]; gLabUi.ruleRow = 1; key(ENTER); pickRow(int(Sig::Light)); key(ENTER); key(ENTER);
  for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) esc(); assert(same(gLabBook.rules[1], kept));
  // Three of each is the most, and then the steps stop asking.
  gLabUi.ruleRow = 1; key(ENTER); key(ENTER); key(ENTER); key(ENTER); key(DOWN); key(ENTER);  // Second line.
  pickRow(int(Sig::Motion)); key(ENTER); assert(stepNow() == LabStep::MoreConds); key(DOWN); key(ENTER); pickRow(int(Sig::Tilt)); pickRow(2); assert(stepNow() == LabStep::Join && gLabWizard.keepConds == 3);
  key(ENTER); key(ENTER); assert(stepNow() == LabStep::How); key(ENTER); assert(stepNow() == LabStep::MoreActions); key(ENTER); assert(stepNow() == LabStep::OutValue || stepNow() == LabStep::Out || stepNow() == LabStep::MoreActions || stepNow() == LabStep::Hold);
  for (int i = 0; i < 60 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabBook.rules[1].conds == 3);
  // Back to the shape the play test uses.
  gLabBook.rules[1] = kept; gLabBook.rules[1].holdMs = 0; gLabBook.rules[1].cond[0] = {Sig::Distance, lab::Op::Below, 29}; gLabBook.rules[1].conds = 1;
  // On and off, and two presses to delete. Moving away cancels the first.
  gLabUi.ruleRow = 1; keys(" "); assert(!gLabBook.rules[1].on); keys(" "); assert(gLabBook.rules[1].on);
  key('x'); assert(gLabUi.deleteArmed && gLabBook.count == 2); show("rules-delete"); assert(gCanvas.shows("X again")); key(DOWN); assert(!gLabUi.deleteArmed); key(UP);
  key('x'); key('p'); assert(!gLabUi.deleteArmed || gLabPlaying); if (gLabPlaying) esc();
  assert(gLabBook.count == 2);
}

void testPlay() {
  // The owner's example, end to end through the hub.
  const int servo = ports(lab::Part::Servo), strip = ports(lab::Part::Strip), relay = ports(lab::Part::Relay);
  gUnits.um = 800000; gHubUnit.analog[ports(lab::Part::Knob)] = 1000; run(1200);
  gLabUi.ruleRow = 0; keys("p"); assert(gLabPlaying && gLabUi.view == LabView::Play && gLabUi.playRule == 0); run(800); show("flow-waiting");
  assert(gCanvas.shows("WAITING") && !gLabStates[0].active && gHubUnit.written[relay][0] == 0);
  gHubUnit.angle[servo][1] = -1;
  // Turn the knob: the rule fires, the servo moves with it, and the strip lights.
  gHubUnit.analog[ports(lab::Part::Knob)] = 1500; run(300);
  assert(gLabStates[0].active && gHubUnit.angle[servo][1] == 45 && sumLeds(strip) == 30);
  show("flow-firing"); assert(gCanvas.shows("FIRING 1/2") && gCanvas.shows("Knob") && gCanvas.shows("45") && gCanvas.shows("Fired once"));
  // It holds for 2 seconds after, then lets the strip go. The servo stays where it was put.
  run(3000); assert(!gLabStates[0].active && sumLeds(strip) == 0 && gHubUnit.pulses[servo][1] == 1);
  // A hand close: the other condition, and the second rule's relay and LED.
  gUnits.um = 200000; run(300); assert(gLabStates[0].active && gLabStates[0].fires == 2 && gLabStates[1].active);
  assert(gHubUnit.written[relay][0] == 1 && gLed.g > 0); show("flow-again"); assert(gCanvas.shows("Fired 2 times"));
  // Paging wraps, and shows the rule that's on each page.
  keys("/"); assert(gLabUi.playRule == 1); show("flow-two"); assert(gCanvas.shows("Close and turn") || gCanvas.shows("2/2")); keys("/"); assert(gLabUi.playRule == 0); keys(","); assert(gLabUi.playRule == 1);
  // Pause stops everything at once and resumes.
  keys(" "); assert(gLabPaused && gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0); show("flow-paused"); assert(gCanvas.shows("PAUSED") && !gCanvas.shows("Built-in LED--") && gCanvas.shows("--"));
  run(500); assert(gHubUnit.written[relay][0] == 0);
  keys(" "); run(500); assert(!gLabPaused && gHubUnit.written[relay][0] == 1);
  // Playing keeps the screen on, because the screensaver would stop the rules.
  { const int wakes = gWakes; run(3000); assert(gWakes >= wakes + 2); }
  // The menu on top stops everything, once, and leaves it paused.
  gLauncherVisible = true; { auto before = gHubUnit.log.size(); run(500); assert(gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0); const auto after = gHubUnit.log.size(); run(2000); assert(gHubUnit.log.size() == after && after > before); }
  gLauncherVisible = false; run(100); assert(gLabPaused); keys(" "); run(500); assert(gHubUnit.written[relay][0] == 1);
  // A rule that is off stays out of it.
  gLabBook.rules[1].on = false; run(500); assert(gHubUnit.written[relay][0] == 0); show("flow-off"); assert(gCanvas.shows("OFF 2/2") && !gCanvas.shows("FIRING")); gLabBook.rules[1].on = true;
  // No flicker reaches the relay: a sensor bouncing across the line can't chatter it.
  { auto before = gHubUnit.log.size(); for (int i = 0; i < 100; ++i) { gUnits.um = i % 2 ? 200000 : 800000; run(130); }
    int toggles = 0; for (size_t i = before; i < gHubUnit.log.size(); ++i) { auto& b = gHubUnit.log[i]; if (b.size() == 2 && (b[0] >> 4) == pbhub::channel(uint8_t(relay)) >> 4 && (b[0] & 15) == 0) ++toggles; } assert(toggles <= 13000 / int(lab::kRelayGapMs) + 1); }
  // The hub vanishing mid-play doesn't stop the rest, and it picks back up.
  gUnits.um = 200000; run(300); gHubUnit.here = false; run(1500); assert(!labFound(Unit::Hub) && gLabPlaying);
  gHubUnit.here = true; std::memset(gHubUnit.ledRgb, 0, sizeof(gHubUnit.ledRgb)); fingers(); run(1500); assert(labFound(Unit::Hub) && gHubUnit.written[relay][0] == 1 && sumLeds(strip) == 30);
  // Leaving: relays open, strip off, servo limp, and the port is free.
  esc(); assert(!gLabPlaying && gLabUi.view == LabView::Rules && gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0 && gHubUnit.pulses[servo][1] == 0 && gLed.g == 0);
  const int closes = gAirSensor.closes;
  keys("p"); run(300); assert(gLabPlaying);
  setUiMode(UiMode::Home); assert(!gLabOpen && !gLabPlaying && gHubUnit.written[relay][0] == 0 && sumLeds(strip) == 0 && gAirSensor.closes == closes + 1 && !Serial1.open);
  { const auto before = gUnits.commands; run(500); assert(gUnits.commands == before); }  // Nothing touches the bus once it's closed.
}

void testPractice() {
  // Nothing plugged in at all, and a rule can still be tried from the keyboard.
  gUnits.present.clear(); gHubUnit.here = false; gLabInputs = lab::Inputs{};
  setUiMode(UiMode::LabRules); run(1500); gLabUi.ruleRow = 0; keys("p"); run(200); assert(gLabPlaying && !gLabStates[0].active);
  show("practice-none"); assert(gCanvas.shows("--") && gCanvas.shows("T practice"));
  keys("t"); assert(gLabUi.practicing && gLabUi.practiceCond == 0); show("practice-start"); assert(gCanvas.shows("PRACTICE"));
  // Up changes the first sensor. The Wave lights knob "changes", so the rule fires, and its servo follows.
  keys(";"); run(50); assert(gLabPractice.faking(Sig::Knob) && gLabStates[0].active); show("practice-knob"); assert(gCanvas.shows("55%") && gCanvas.shows("FIRING") == false);
  assert(gLabDrive[uint8_t(Out::Servo)].on && gLabDrive[uint8_t(Out::Servo)].value == 99);
  // Across to the next sensor, down to under 30 cm.
  keys("/"); assert(gLabUi.practiceCond == 1); for (int i = 0; i < 12; ++i) keys("."); run(50);
  assert(gLabPractice.faking(Sig::Distance) && gLabSeen.get(Sig::Distance).value < 30); show("practice-distance");
  assert(lab::condHolds(gLabBook.rules[0].cond[1], gLabSeen, nowMs));
  // A real sensor showing up doesn't disturb the faked one.
  gUnits.present.insert(0x57); gUnits.um = 900000; run(1500); assert(gLabInputs.get(Sig::Distance).value == 90 && gLabSeen.get(Sig::Distance).value < 30);
  // Esc ends practice first, and the next Esc stops. The real reading comes back.
  esc(); assert(!gLabUi.practicing && gLabPlaying && !gLabPractice.any()); run(100); assert(gLabSeen.get(Sig::Distance).value == 90);
  keys("t"); keys("t"); assert(!gLabUi.practicing && gLabPlaying);
  esc(); assert(!gLabPlaying); gUnits.present = {0x57, 0x44, 0x63, 0x50}; gHubUnit.here = true; gUnits.um = 240000; run(1500); fingers(); run(100);
  // Left and right only change rules outside practice.
  keys("p"); keys("/"); assert(gLabUi.playRule == 1); keys("t"); const int rule = gLabUi.playRule; keys("/"); assert(gLabUi.playRule == rule); esc(); esc();
}

void testRecipes() {
  const int count = gLabBook.count;
  goToRow(count + 2); show("rules-recipes"); assert(gCanvas.shows(std::to_string(lab::kRecipeCount) + " ready to go"));
  key(ENTER); assert(gLabUi.view == LabView::Recipes); show("recipes"); assert(gCanvas.shows("Knob to servo") && gCanvas.shows("Turn the knob"));
  for (int i = 0; i < lab::kRecipeCount; ++i) { char name[16]; std::snprintf(name, 16, "recipe-%d", i); show(name); assert(gCanvas.shows(lab::kRecipes[gLabUi.recipeRow].about)); key(DOWN); }
  assert(gLabUi.recipeRow == 0);
  key(UP); assert(gLabUi.recipeRow == lab::kRecipeCount - 1); key(DOWN);
  esc(); assert(gLabUi.view == LabView::Rules && gLabBook.count == count);  // Looking adds nothing.
  key(ENTER); key(DOWN); key(ENTER); assert(gLabBook.count == count + 1 && !std::strcmp(gLabBook.rules[count].name, "Stick steers") && gLabUi.view == LabView::Rules && gLabUi.ruleRow == count);
  assert(gLabSaved); show("rules-added");
  // A recipe is then a rule like any other.
  enterThrough(count); assert(!std::strcmp(gLabBook.rules[count].name, "Stick steers"));
  key('x'); key('x'); assert(gLabBook.count == count);
}

void testSay() {
  goToRow(gLabBook.count + 1); key(ENTER); assert(gLabUi.view == LabView::Say); show("say"); assert(gCanvas.shows("Tell Claude"));
  // Talk, stop, wait, and the rule comes back to look at.
  const int taps = gVoiceTaps;
  // When something is already on the network the mic takes a moment, and the screen says so.
  gVoiceStarting = true; show("say-starting"); assert(gCanvas.shows("Getting ready...") && gCanvas.shows("ESC cancel") && gCanvas.shows("STARTING")); gVoiceStarting = false; show("say");
  assert(gCanvas.shows("ALT talk")); key(ENTER); assert(gRecording && gVoiceTaps == taps + 1); run(50); show("say-listening"); assert(gCanvas.shows("Listening") && gCanvas.shows("ALT send  ESC cancel"));
  key(ENTER); assert(!gRecording && gThinking); run(50); show("say-writing"); assert(gCanvas.shows("Claude is writing it"));
  key(ENTER); assert(gVoiceTaps == taps + 2);  // A press while it's writing does nothing.
  lab::Draft d; d.name("Wave red"); d.addCond("distance", "under", 30); d.addSet("strip_color", 0); d.addFollow("servo", "knob", false); assert(d.usable());
  gThinking = false; labOfferRule(d.rule); run(50); assert(gLabUi.view == LabView::Offer); show("offer"); assert(gCanvas.shows("Claude wrote this") && gCanvas.shows("Distance under 30 cm") && gCanvas.shows("Servo follows Knob"));
  // Nothing is saved or driven by arriving. Esc drops it.
  const int count = gLabBook.count; assert(!gLabPlaying); esc(); assert(gLabUi.view == LabView::Rules && gLabBook.count == count);
  // Enter saves it, as an ordinary rule.
  labOfferRule(d.rule); key(ENTER); assert(gLabUi.view == LabView::Rules && gLabBook.count == count + 1 && gLabUi.ruleRow == count && !std::strcmp(gLabBook.rules[count].name, "Wave red") && gLabSaved);
  key('x'); key('x'); assert(gLabBook.count == count);
  // E walks the steps, and saves as a new rule.
  labOfferRule(d.rule); key('e'); assert(gLabUi.view == LabView::Wizard && gLabWizard.index < 0 && gLabWizard.rule.cond[0].value == 30); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER);
  assert(gLabBook.count == count + 1 && gLabBook.rules[count].actions == 2); key('x'); key('x'); assert(gLabBook.count == count);
  // A reply with no rule shows what Claude needs and what it heard, and it can be tried again.
  assert(!labSayOnScreen()); goToRow(gLabBook.count + 1); key(ENTER); assert(labSayOnScreen()); key(ENTER); run(20); key(ENTER); run(20); gThinking = false;
  { VoiceToolAnswer a; a.ok = true; a.heard = "turn it on"; a.reply = "Which sensor should I watch?"; labSayAnswered(a); }
  run(50); assert(gLabUi.sayFailed); show("say-failed"); assert(gCanvas.shows("No rule came back") && gCanvas.shows("Which sensor should I watch?") && gCanvas.shows("Heard: turn it on") && gCanvas.shows("ALT try again"));
  // An action Claude sent that the device refused counts as no rule.
  { VoiceToolAnswer a; a.ok = true; a.actionsJson = "[{}]"; labSayAnswered(a); assert(gLabUi.sayFailed); renderLabUi(); assert(gCanvas.shows("Try again, naming a sensor")); }
  // A rule that was taken leaves this alone, because the offer has already opened.
  { gLabUi.sayFailed = false; gActionsRun = true; VoiceToolAnswer a; a.ok = true; a.actionsJson = "[{}]"; labSayAnswered(a); assert(!gLabUi.sayFailed); gActionsRun = false; }
  // When it never reached Claude it says why.
  { VoiceToolAnswer a; a.problem = "Couldn't reach the bridge."; labSayAnswered(a); renderLabUi(); assert(gCanvas.shows("Couldn't reach the bridge.") && !gCanvas.shows("Heard:")); }
  key(ENTER); assert(gRecording); key(ENTER); gThinking = false; run(50); esc(); assert(gLabUi.view == LabView::Rules); run(100);
  // Change a rule: Alt on a saved rule. The chosen row says so, and the rest of the list doesn't.
  {
    goToRow(0); show("rules-chosen"); assert(gCanvas.shows("ALT change"));
    assert(labChangeOnScreen() && !labSayOnScreen());
    gRecording = true; show("change-listening"); assert(gCanvas.shows("Wave lights") && gCanvas.shows("Say what should be different.") && gCanvas.shows("ALT send  ESC cancel"));
    gRecording = false; gThinking = true; show("change-writing"); assert(gCanvas.shows("Claude is changing it"));
    const int row = gLabUi.ruleRow; key(DOWN); assert(gLabUi.ruleRow == row);  // The list underneath can't move while it's covered.
    gThinking = false;
    // The rule comes back to look at, and only saving replaces the old one.
    lab::Rule before = gLabBook.rules[0]; lab::Rule changed = before; changed.holdMs = 9000; changed.action[0].how = lab::How::Set; changed.action[0].value = 240;
    gOfferStepAt = -1; gActionRule = &changed; gActionsRun = true; { VoiceToolAnswer a; a.ok = true; a.heard = "make it blue"; a.actionsJson = "[{}]"; labChangeAnswered(a); } gActionRule = nullptr; gActionsRun = false;
    assert(gLabUi.view == LabView::Offer && gLabOfferChanges == 0); show("change-offer"); assert(gCanvas.shows("Claude changed this"));
    const int count = gLabBook.count; esc(); assert(gLabUi.view == LabView::Rules && gLabBook.count == count && gLabBook.rules[0].holdMs == before.holdMs);
    // A rule that was switched off stays off, and nothing is added.
    gLabBook.rules[0].on = false; gLabOfferNext = 0; labOfferRule(changed); gLabOfferNext = -1; key(ENTER);
    assert(gLabUi.view == LabView::Rules && gLabBook.count == count && gLabBook.rules[0].holdMs == 9000 && gLabBook.rules[0].action[0].value == 240 && !gLabBook.rules[0].on && gLabUi.ruleRow == 0 && gLabSaved);
    // E walks the change through the steps, and it still replaces rather than adds.
    gLabOfferNext = 0; labOfferRule(before); gLabOfferNext = -1; key('e'); assert(gLabUi.view == LabView::Wizard && gLabWizard.index == 0);
    for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER);
    assert(gLabBook.count == count && gLabBook.rules[0].holdMs == before.holdMs);
    gLabBook.rules[0] = before;
    // A rule asked for in the chat afterwards is new, whatever happened here.
    labOfferRule(changed); assert(gLabOfferChanges < 0); show("offer-new"); assert(gCanvas.shows("Claude wrote this")); esc();
    // No rule back says what's missing, and Alt tries that same rule again.
    { VoiceToolAnswer a; a.ok = true; a.heard = "do the thing"; a.reply = "Change what about it?"; labChangeAnswered(a); }
    assert(gLabUi.view == LabView::Say && labChangeOnScreen()); show("change-failed"); assert(gCanvas.shows("Nothing changed") && gCanvas.shows("Change what about it?") && gCanvas.shows("Wave lights"));
    esc(); assert(gLabUi.view == LabView::Rules);
    // What Claude is told.
    const String context = labVoiceContext(); const std::string text = context;
    assert(text.find("rule 1:") != std::string::npos && text.find("\"name\":\"Wave lights\"") != std::string::npos && text.find("rules are not playing") != std::string::npos);
    assert(text.find("changing rule 1") != std::string::npos);
    goToRow(gLabBook.count + 1); assert(std::string(labVoiceContext()).find("changing rule") == std::string::npos);
  }
  // Ask the Lab: its own row, and Alt from anywhere that isn't a saved rule.
  {
    goToRow(gLabBook.count + 3); show("rules-ask"); assert(gCanvas.shows("Ask the Lab") && labAskOnScreen() && !labChangeOnScreen());
    key(ENTER); assert(gLabUi.view == LabView::Say && labAskOnScreen()); show("ask"); assert(gCanvas.shows("What's your question?") && gCanvas.shows("What can I make with these parts?"));
    gRecording = true; show("ask-listening"); assert(gCanvas.shows("Ask about your parts and rules."));
    gRecording = false; gThinking = true; show("ask-working"); assert(gCanvas.shows("Claude is looking") && gCanvas.shows("LOOKING")); gThinking = false;
    { VoiceToolAnswer a; a.ok = true; a.heard = "why didn't my rule fire"; a.reply = "Distance is 42 cm and Wave red needs under 30. Move closer, or say change it to 50."; labAskAnswered(a); }
    assert(std::string(gQueuedVoiceReplySpeech).find("Distance is 42") == 0); gQueuedVoiceReplySpeech = "";
    show("ask-answer"); assert(gCanvas.shows("Distance is 42") && gCanvas.shows("Heard: why didn't my rule fire") && gCanvas.shows("ALT ask again"));
    esc(); assert(gLabUi.view == LabView::Rules);
    // A reason it couldn't answer shows, but nothing is read aloud.
    gLabUi.view = LabView::Devices; assert(labAskOnScreen());
    { VoiceToolAnswer a; a.problem = "Your Claude plan limit has been reached."; labAskAnswered(a); } assert(gLabUi.view == LabView::Say && std::string(gQueuedVoiceReplySpeech).empty());
    renderLabUi(); assert(gCanvas.shows("Your Claude plan limit"));
    // Esc goes back to where the question came from, here the Devices list, and asking again stays put.
    gRecording = false; { VoiceToolAnswer a; a.ok = true; a.reply = "Again."; labAskAnswered(a); } esc(); assert(gLabUi.view == LabView::Devices); gQueuedVoiceReplySpeech = "";
    show("devices-ask"); assert(gCanvas.shows("ALT ask"));
    // From a part's own test screen, the question says so, and Esc goes back there.
    labOpenDevice(Dev::Servo); assert(labAskOnScreen()); assert(std::string(labVoiceContext()).find("test screen for: Servo") != std::string::npos);
    { VoiceToolAnswer a; a.ok = true; a.reply = "Check its port."; labAskAnswered(a); } assert(gLabUi.view == LabView::Say); esc(); assert(gLabUi.view == LabView::Device); labCloseDevice(); gQueuedVoiceReplySpeech = "";
    // "What can I build?" brings up to three rules to pick from, and Esc drops them all.
    gLabUi.view = LabView::Rules; goToRow(gLabBook.count + 3);
    lab::Draft d; d.name("Idea"); d.addCond("distance", "under", 30); d.addSet("strip_color", 0);
    gOfferSteps[0] = d.rule; std::strcpy(gOfferSteps[0].name, "First"); gOfferSteps[1] = d.rule; std::strcpy(gOfferSteps[1].name, "Second"); gOfferSteps[2] = d.rule; std::strcpy(gOfferSteps[2].name, "Third"); gOfferSteps[3] = d.rule;
    gOfferStepAt = 0; gActionRule = gOfferSteps; gActionRepeats = 4; gActionsRun = true; { VoiceToolAnswer a; a.ok = true; a.reply = "Three ideas."; a.actionsJson = "[{}]"; labAskAnswered(a); } gActionRule = nullptr; gActionRepeats = 1; gActionsRun = false;
    assert(gLabUi.view == LabView::Offer && gLabOfferCount == 3 && gLabOfferAt == 0 && gLabOfferChanges < 0 && std::string(gQueuedVoiceReplySpeech).empty());
    show("ask-offers"); assert(gCanvas.shows("1 OF 3") && gCanvas.shows("Name: First") && gCanvas.shows(" next"));
    key(RIGHT); show("ask-offers-2"); assert(gCanvas.shows("2 OF 3") && gCanvas.shows("Name: Second")); key(RIGHT); key(RIGHT); assert(gLabOfferAt == 0); key(LEFT); assert(gLabOfferAt == 2);
    // Enter saves the one showing.
    const int count = gLabBook.count; key(ENTER); assert(gLabBook.count == count + 1 && !std::strcmp(gLabBook.rules[count].name, "Third") && gLabUi.view == LabView::Rules);
    key('x'); key('x'); assert(gLabBook.count == count);
    // A rule from the chat is a single offer again, whatever came before.
    labOfferRule(d.rule); assert(gLabOfferCount == 1); renderLabUi(); assert(!gCanvas.shows(" OF ")); esc();
    gLabUi.view = LabView::Rules; goToRow(gLabBook.count + 1);
  }
  // A rule that talks: the speech is made when it's saved, and plays once each time it starts firing.
  {
    M5.Imu.ay = 0;
    lab::Draft d; d.name("Tip hello"); d.addCond("tilt", "over", 45); d.addSay("Hello there"); d.addSet("led", 30); d.hold(0); assert(d.usable());
    const int count = gLabBook.count, fetches = gSpeechFetches;
    labOfferRule(d.rule); show("say-offer-speech"); assert(gCanvas.shows("Say \"Hello there\""));
    gLabFetchingSpeech = true; show("say-fetching"); assert(gCanvas.shows("Making its voice")); gLabFetchingSpeech = false;
    key(ENTER); assert(gLabBook.count == count + 1 && gSpeechFetches == fetches + 1 && gSpeechText == "Hello there");
    const std::string path = labSayPath("Hello there"); assert(gDisk.files.count(path) && path.rfind("/lab/say/", 0) == 0);
    // Saving the same words again costs nothing.
    labFetchSpeech(gLabBook.rules[count]); assert(gSpeechFetches == fetches + 1);
    // Playing: nothing until it fires, then once, however long it stays true.
    const int plays = gSpeechPlays; keys("p"); assert(gLabPlaying); gLabUi.playRule = count; run(300); assert(gSpeechPlays == plays);
    show("say-flow"); assert(gCanvas.shows("Hello there"));
    M5.Imu.ay = 1; run(300); assert(gSpeechPlays == plays + 1 && gSpeechPath == path && gPlaybackActive && gLabStates[count].active);
    gPlaybackActive = false; run(500); assert(gSpeechPlays == plays + 1);
    M5.Imu.ay = 0; run(300); assert(!gLabStates[count].active);
    M5.Imu.ay = 1; run(300); assert(gSpeechPlays == plays + 2);  // Firing again speaks again.
    gPlaybackActive = false; M5.Imu.ay = 0; run(200); labStopPlaying(); gLabUi.view = LabView::Rules;
    // Lost speech comes back before playing, and if it can't, playing still starts.
    gDisk.files.erase(path); gSpeechWorks = false; const int tries = gSpeechFetches; keys("p"); assert(gSpeechFetches == tries + 1 && gLabPlaying && !gDisk.files.count(path) && !gLabFetchingSpeech); labStopPlaying(); gLabUi.view = LabView::Rules; gSpeechWorks = true;
    // The steps keep a phrase, and only offer Say to a rule with one, skipping what makes no sense.
    labWizardStart(count, nullptr); gLabWizard.step = LabStep::Out; assert(labWizardRowCount() == lab::kOutCount); assert(std::string(labWizardRow(lab::kOutCount - 2)) == "Say" && std::string(labWizardRow(lab::kOutCount - 1)) == "Sequence");
    gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
    labWizardStart(-1, nullptr); gLabWizard.step = LabStep::Out; assert(labWizardRowCount() == lab::kOutCount - 1); gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
    labWizardStart(count, nullptr); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER);
    assert(gLabUi.view == LabView::Rules && !std::strcmp(gLabBook.rules[count].say, "Hello there") && lab::hasSay(gLabBook.rules[count]) && gLabBook.rules[count].actions == 2);
    // Taking Say off a rule drops its phrase.
    lab::Rule plain = gLabBook.rules[count]; plain.action[0] = plain.action[1]; plain.actions = 1; labWizardStart(count, &plain); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabBook.rules[count].say[0] == 0);
    gLabUi.ruleRow = count; key('x'); key('x'); assert(gLabBook.count == count);
    goToRow(gLabBook.count + 1);
  }
  // Spoken commands: Alt while playing, and the name toggles the rule, without leaving the screen.
  {
    M5.Imu.ay = 0;
    lab::Draft d; d.name("Movie time"); d.addCond("spoken", "on", 0); d.addSet("led", 240); d.hold(0); assert(d.usable());
    lab::Draft other; other.name("Lamp on"); other.addCond("spoken", "on", 0); other.addSet("led", 30); other.hold(0);
    const int count = gLabBook.count; assert(count + 2 <= lab::kMaxRules);
    gLabBook.rules[count] = d.rule; gLabBook.rules[count + 1] = other.rule; gLabBook.count = count + 2;
    labOfferRule(d.rule); renderLabUi(); assert(gCanvas.shows("You say its name")); esc();
    assert(!labCommandOnScreen()); keys("p"); assert(gLabPlaying && labCommandOnScreen() && !labAskOnScreen() && !labChangeOnScreen());
    gLabUi.playRule = count; run(100); show("name-flow"); assert(gCanvas.shows("ALT and say its name") && gCanvas.shows("not yet") && !gLabStates[count].active);
    gRecording = true; show("name-listening"); assert(gCanvas.shows("Say its name") && gCanvas.shows("ALT send  ESC cancel"));
    gRecording = false; gThinking = true; show("name-checking"); assert(gCanvas.shows("Checking what you said")); gThinking = false;
    const auto heard = [](const char* words) { VoiceToolAnswer a; a.ok = true; a.heard = words; labCommandAnswered(a); };
    heard("Lamp on."); assert(gLabUi.view == LabView::Play && gLabUi.playRule == count + 1 && gLabNamed == (1 << (count + 1))); run(100);
    assert(gLabStates[count + 1].active && !gLabStates[count].active); show("name-on"); assert(gCanvas.shows("Lamp on on") && gCanvas.shows("said"));
    heard("movie time"); run(100); assert(gLabStates[count].active && gLabUi.playRule == count);
    heard("Lamp on"); run(100); assert(!gLabStates[count + 1].active && gLabStates[count].active); renderLabUi();
    // Words that aren't a name say what was heard, and change nothing.
    const uint8_t before = gLabNamed; heard("pizza"); assert(gLabNamed == before); show("name-unknown"); assert(gCanvas.shows("Heard: pizza"));
    // A failure says why, then the screen goes back to normal.
    { VoiceToolAnswer a; a.problem = "Couldn't reach the bridge."; labCommandAnswered(a); } renderLabUi(); assert(gCanvas.shows("Couldn't reach the bridge."));
    run(3100); renderLabUi(); assert(!gCanvas.shows("Couldn't reach the bridge.") && gCanvas.shows("ALT and say its name"));
    // Practice stands in for saying it.
    gLabUi.playRule = count + 1; key('t'); assert(gLabUi.practicing); key(UP); run(100); assert(gLabStates[count + 1].active); key('t'); gLabPractice.clear(); run(100);
    // Stopping forgets what was said.
    esc(); assert(!gLabPlaying && gLabNamed == 0); gLabBook.count = count; gLabUi.view = LabView::Rules; goToRow(gLabBook.count + 1);
    // The steps put it in words.
    labWizardStart(-1, nullptr); gLabWizard.rule.cond[0].sig = Sig::Spoken; gLabWizard.step = LabStep::Op; assert(labWizardRowCount() == 2 && std::string(labWizardRow(0)) == "is said");
    gLabWizard.step = LabStep::Sig; assert(labWizardRowCount() == lab::kSigCount); gLabWizard.step = LabStep::Source; assert(labWizardRowCount() == lab::kSigCount - 1);
    // Follow lists every sensor but a rule's name, which isn't last any more, and maps both ways.
    for (int row = 0; row < labWizardRowCount(); ++row) {
      const Sig sig = labWizardSourceAt(row);
      assert(sig != Sig::Spoken && sig < Sig::Count && labWizardSourceRow(sig) == row && std::string(labWizardRow(row)) == lab::info(sig).name);
    }
    assert(labWizardSourceAt(labWizardRowCount() - 1) == Sig::SpotDistance && std::string(labWizardRow(labWizardRowCount() - 1)) == "From spot");
    gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
  }
  // The diary: a line an hour while rules play, kept for questions, and nowhere else.
  {
    const std::string diary = "/lab/diary.txt";
    const auto text = [&]() { const auto& f = gDisk.files[diary]; return std::string(f.begin(), f.end()); };
    gDisk.files.erase(diary); M5.Imu.ay = 0.5f;
    keys("p"); assert(gLabPlaying); run(10100); assert(!gLabDiary.empty() && !gDisk.files.count(diary));  // Nothing yet: the hour isn't over.
    // When the hour turns, the last one goes on the card with its own time.
    std::strcpy(gLabDiaryHour, "Nov 02 03h"); run(10100);
    assert(text().rfind("Nov 02 03h: motion 0, rules fired ", 0) == 0 && text().find("tilt 30 to 30") != std::string::npos && text().back() == '\n');
    // Stopping saves what there is, so nothing is lost.
    const size_t size = gDisk.files[diary].size(); esc(); assert(gDisk.files[diary].size() > size && gLabDiary.empty()); gLabUi.view = LabView::Rules;
    // It goes with a question, but not with writing a rule.
    assert(std::string(labAskContext()).find("diary, an hour a line, newest last:\nNov 02 03h: motion 0") != std::string::npos);
    assert(std::string(labVoiceContext()).find("diary") == std::string::npos);
    // A long file sends only its newest whole lines, and the rest of the state still fits.
    { std::string many; for (int i = 0; i < 2000; ++i) many += "Nov 01 " + std::to_string(i) + "h: motion 1, rules fired 2, tilt 1 to 2\n"; gDisk.files[diary].assign(many.begin(), many.end()); }
    { const std::string asked = labAskContext(); const size_t at = asked.find("newest last:\n"); assert(at != std::string::npos && asked.find("h: motion", at) != std::string::npos);
      assert(asked.compare(at + 13, 7, "Nov 01 ") == 0 && asked.back() == '\n' && asked.find("1999h") != std::string::npos && asked.size() < 3900); }
    // Past its limit the file starts over, and the old part is kept once.
    { std::string big(kLabDiaryMaxBytes + 10, 'x'); gDisk.files[diary].assign(big.begin(), big.end()); gDisk.files["/lab/diary.old"] = {1}; }
    keys("p"); run(10100); esc(); gLabUi.view = LabView::Rules; assert(gDisk.files["/lab/diary.old"].size() > kLabDiaryMaxBytes && gDisk.files[diary].size() < 300);
    // Practice isn't what really happened, and with no clock a line can't say when.
    gDisk.files.erase(diary); gLabDiary.clear(); keys("p"); key('t'); key(UP); run(10100); assert(gLabDiary.empty()); key('t'); gLabPractice.clear(); esc(); gLabUi.view = LabView::Rules;
    gClockOk = false; keys("p"); run(10100); assert(gLabDiary.empty()); esc(); gLabUi.view = LabView::Rules; gClockOk = true;
    assert(!gDisk.files.count(diary));
    // No card: it doesn't get in the way.
    gSdReady = false; keys("p"); run(10100); esc(); gLabUi.view = LabView::Rules; assert(std::string(labAskContext()).find("diary") == std::string::npos); gSdReady = true;
    goToRow(gLabBook.count + 1);
  }
  // Set a part by voice: only the one on the screen, only something small, only from the Lab.
  {
    gLabUi.view = LabView::Devices;
    const auto openPart = [](Dev dev) { gLabUi.devRow = int(dev); labOpenDevice(dev); };
    const auto spoken = [](const char* output, int value) { gLabVoiceSetting = true; const bool done = labVoiceSet(output, value); gLabVoiceSetting = false; return done; };
    openPart(Dev::Servo); show("servo-voice"); assert(gCanvas.shows("ALT say it"));
    gLabUi.sweeping = true; assert(spoken("servo", 135) && gLabUi.servoAngle == 135 && !gLabUi.sweeping && gLabDrive[int(Out::Servo)].on && gLabDrive[int(Out::Servo)].value == 135);
    assert(spoken("servo", 900) && gLabUi.servoAngle == 180);  // Past the end stops at the end.
    assert(!spoken("strip_color", 0));                         // Not what's on the screen.
    assert(!spoken("relay_1", 1) && !gLabDrive[int(Out::Relay1)].on);
    assert(!spoken("ghost", 1));
    assert(!labVoiceSet("servo", 10) && gLabUi.servoAngle == 180);  // Not an answer: a chat message can't do this.
    // What Claude is told on this screen includes where things are now, for "a bit more".
    assert(std::string(labVoiceContext()).find("test values: servo=180") != std::string::npos);
    labCloseDevice(); assert(std::string(labVoiceContext()).find("test values") == std::string::npos);
    assert(!spoken("servo", 5));                               // Off its screen it does nothing.
    openPart(Dev::Strip); assert(spoken("strip_color", 240) && gLabUi.stripHue == 240 && gLabDrive[int(Out::StripColor)].value == 240);
    assert(spoken("strip_bright", 60) && gLabUi.stripBright == 60 && gLabDrive[int(Out::StripBright)].value == 60); assert(!spoken("servo", 90)); labCloseDevice();
    openPart(Dev::Led); assert(spoken("led", 120) && gLabUi.ledOn && gLabDrive[int(Out::Led)].value == 120); labCloseDevice();
    openPart(Dev::Speaker); assert(spoken("speaker", 5000) && gLabUi.toneHz == 2000 && gLabUi.beepUntilMs && gLabDrive[int(Out::Tone)].on); run(600); assert(!gLabDrive[int(Out::Tone)].on); labCloseDevice();
    // A sensor's screen or the relays can't be set, and say nothing about it.
    openPart(Dev::Relay); show("relay-voice"); assert(!gCanvas.shows("ALT say it") && !spoken("relay_1", 1) && !gLabUi.relays[0]); labCloseDevice();
    // Something set by voice stays on its screen with no answer to read.
    openPart(Dev::Servo); gActionsRun = true; { VoiceToolAnswer a; a.ok = true; a.reply = "Set."; a.actionsJson = "[{}]"; labAskAnswered(a); } gActionsRun = false;
    assert(gLabUi.view == LabView::Device && std::string(gQueuedVoiceReplySpeech).empty()); labCloseDevice();
    gLabUi.view = LabView::Rules; goToRow(gLabBook.count + 1);
  }
  // With no Wi-Fi it says so instead of trying.
  key(ENTER); gWifiReady = false; show("say-offline"); assert(gCanvas.shows("Needs Wi-Fi")); { const int t = gVoiceTaps; key(ENTER); assert(gVoiceTaps == t); } gWifiReady = true; esc();
  // A rule arriving elsewhere still reaches this screen, and stops rules that are playing.
  setUiMode(UiMode::Home); labOfferRule(d.rule); assert(gUiMode == UiMode::LabRules && gLabOpen && gLabUi.view == LabView::Offer); esc();
  keys("p"); run(300); assert(gLabPlaying); labOfferRule(d.rule); assert(!gLabPlaying && gLabUi.view == LabView::Offer); run(100); esc();
  // With the list full, Enter refuses and the offer stays.
  const lab::Book kept = gLabBook; gLabBook.count = lab::kMaxRules; labOfferRule(d.rule); key(ENTER); assert(gLabUi.view == LabView::Offer && gStatus.find("full") != std::string::npos); esc(); gLabBook = kept;
  // A blank name still saves under something readable.
  lab::Draft blank; blank.addCond("knob", "changes", 0); blank.addSet("servo", 1); labOfferRule(blank.rule); key('e'); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabBook.rules[gLabBook.count - 1].name[0]); key('x'); key('x');
  gLabUi.ruleRow = 0; gLabUi.ruleTop = 0;
}

void testRemotes() {
  const lab::Book kept = gLabBook;
  lab::Draft d; d.name("Dark"); d.addCond("motion", "on", 0); d.addSet("lamp_power", 1); d.addSet("lamp_bright", 50); d.addSet("ac_tcl", 1); assert(d.usable()); d.rule.holdMs = 0;
  gLabBook.count = 1; gLabBook.rules[0] = d.rule;
  const int motion = ports(lab::Part::Motion);
  gHubUnit.level[motion] = 0; run(200); gLampSent.clear(); gTclSends = 0; gDaikinSends = 0;
  // Starting to play touches neither: the lamp may be in use.
  keys("p"); run(4000); assert(gLampSent.empty() && gTclSends == 0);
  gHubUnit.level[motion] = 1; run(300);
  assert(gSmartLamp.power && gTclPower && gTclSends == 1 && gLampSent.size() == 2 && gLampSent[0] == "power1" && gLampSent[1] == "GL,4,2,128" && gSmartLamp.brightness == 128);
  show("flow-remote");
  run(5000); assert(gTclSends == 1 && gLampSent.size() == 2);  // Nothing is sent again while nothing changes.
  // The rule ending undoes what it did, after the AC's gap, and brightness is left as it was.
  gHubUnit.level[motion] = 0; run(4000); assert(!gSmartLamp.power && !gTclPower && gTclSends == 2 && gLampSent.size() == 3 && gLampSent[2] == "power0");
  // Stopping while it's on does the same at once.
  gHubUnit.level[motion] = 1; run(3500); assert(gTclPower); esc(); assert(!gLabPlaying && !gTclPower && !gSmartLamp.power);
  { const int sends = gTclSends; const size_t lamp = gLampSent.size(); keys("p"); run(500); gHubUnit.level[motion] = 0; run(500); assert(gTclSends >= sends && gLampSent.size() >= lamp); esc(); }
  // The Daikin has its own.
  lab::Draft e; e.addCond("motion", "on", 0); e.addSet("ac_daikin", 1); gLabBook.rules[0] = e.rule; gLabBook.rules[0].holdMs = 0; gHubUnit.level[motion] = 0; run(200); keys("p"); run(300); gDaikinSends = 0; gHubUnit.level[motion] = 1; run(300); assert(gDaikinPower && gDaikinSends == 1); esc(); assert(!gDaikinPower && gDaikinSends == 2);
  // A rule that says off sends off, and leaves it off.
  lab::Draft f; f.addCond("motion", "on", 0); f.addSet("lamp_power", 0); gLabBook.rules[0] = f.rule; gLabBook.rules[0].holdMs = 0; gHubUnit.level[motion] = 0; run(200); keys("p"); run(800); gLampSent.clear(); gHubUnit.level[motion] = 1; run(600); assert(gLampSent.size() == 1 && gLampSent[0] == "power0"); gHubUnit.level[motion] = 0; run(600); assert(gLampSent.size() == 1); esc(); assert(gLampSent.size() == 1);
  gLabBook = kept;
  setUiMode(UiMode::Home);
}

void testSaving() {
  // Everything comes back after a restart.
  setUiMode(UiMode::LabRules); run(100);
  const std::string name = gLabBook.rules[1].name;
  { uint8_t bytes[lab::kFileBytes]; const size_t n = lab::encode(gLabBook, bytes); gDisk.files["/lab/rules.dat"].assign(bytes, bytes + n); }
  setUiMode(UiMode::Home); gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(100);
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
  // The last rule can go, and then there's nothing to play.
  gLabUi.ruleRow = 0; key('x'); key('x'); assert(gLabBook.count == 0 && gLabUi.view == LabView::Rules); show("rules-empty"); keys("p"); assert(!gLabPlaying);
  // Full up, the extra rows go and the list stops growing.
  gLabBook.count = lab::kMaxRules; for (int i = 0; i < lab::kMaxRules; ++i) { gLabBook.rules[i] = lab::starterRule(); snprintf(gLabBook.rules[i].name, 16, "Long name no %d", i); }
  gLabUi.ruleRow = 0; gLabUi.ruleTop = 0; show("rules-full"); assert(!gCanvas.shows("+ New rule"));
  for (int i = 0; i < lab::kMaxRules; ++i) { key(DOWN); show("rules-full"); }
  keys("p"); run(300); assert(gLabPlaying);
  for (int page = 0; page <= lab::kMaxRules; ++page) { show("pages"); keys("/"); }
  esc();
  // A rule with everything in it fits its flow, with live values and without.
  lab::Rule big; std::strcpy(big.name, "Fifteen letters"); big.conds = 3; big.actions = 3; big.any = false;
  big.cond[0] = {Sig::Temperature, lab::Op::Above, 45}; big.cond[1] = {Sig::StickX, lab::Op::Below, -100}; big.cond[2] = {Sig::Satellites, lab::Op::Changes, 0};
  big.action[0].out = Out::StripBright; big.action[0].how = lab::How::FollowBack; big.action[0].source = Sig::StickPress;
  big.action[1].out = Out::Tone; big.action[1].how = lab::How::Set; big.action[1].value = 2000; big.action[2].out = Out::StripColor; big.action[2].how = lab::How::Follow; big.action[2].source = Sig::Temperature;
  gLabBook.rules[0] = big; gLabUi.ruleRow = 0; keys("p"); gLabUi.playRule = 0; show("flow-big-waiting"); gLabStates[0].active = true; show("flow-big"); keys("t"); show("flow-big-practice"); keys("/"); keys("/"); show("flow-big-practice"); esc(); esc();
  labOfferRule(big); show("offer-big"); assert(gCanvas.shows("and Stick X") && !gCanvas.shows("or Stick X")); esc();
}

void plugOnly(std::set<uint8_t> units = {}, bool hub = false) { gUnits.present = units; gHubUnit.here = hub; }
// The ways the owner uses it: one part directly, or one or several through a hub.
void testAnySetup() {
  lab::Book book; book.count = 1; book.rules[0] = lab::recipeRule(0); gLabBook = book;
  gLabBookRead = true; gLabUi = LabUi{}; gDisk.files.clear(); gHubUnit.reset(); gGrove = Grove{};
  for (int& v : gHubUnit.analog) v = 0;
  // One part on the I/O hub, in whatever port was nearest. The list doesn't claim the rest are there.
  plugOnly({}, true); gHubUnit.analog[3] = 1400; setUiMode(UiMode::LabDevices); run(1500);
  gLabUi.devRow = int(Dev::Knob); gLabUi.devTop = 2; show("hub-list"); assert(gCanvas.shows("CH0") && gCanvas.shows("CH1") && !gCanvas.shows("%"));
  key(ENTER); run(200); show("hub-knob-wrong-port"); assert(gCanvas.shows("0%") && gCanvas.shows("P port"));
  // P on its own screen says which port, and the reading follows straight away.
  key('p'); assert(gLabUi.view == LabView::PickPort && gLabUi.portRow == 0); show("hub-pick-port"); assert(gCanvas.shows("WHICH PORT?") && gCanvas.shows("Mini Angle"));
  key(DOWN); key(DOWN); key(DOWN); key(ENTER); assert(gLabUi.view == LabView::Device && ports(lab::Part::Knob) == 3 && gLabBook.wiring.port[0] == lab::Part::Relay); run(300);
  show("hub-knob"); assert(gLabInputs.get(Sig::Knob).live && gLabInputs.get(Sig::Knob).value == 0 && gCanvas.shows("CH3"));
  gHubUnit.analog[3] = 700; run(200); assert(gLabInputs.get(Sig::Knob).value == 50);
  assert(gLabSaved && gDisk.files.count("/lab/rules.dat"));
  // Esc from the question changes nothing.
  key('p'); key(DOWN); esc(); assert(gLabUi.view == LabView::Device && ports(lab::Part::Knob) == 3); esc();
  // Now it's been opened, the list shows its number, and the others still say where they'd be.
  show("hub-list-used"); assert(gCanvas.shows("50%"));
  // Moving a driven part leaves nothing on behind it, and sets the strip up on its new port.
  const int old = ports(lab::Part::Strip);
  pickDevice(Dev::Strip); run(200); assert(sumLeds(old) == 30); show("hub-strip"); assert(gCanvas.shows("P port"));
  key('p'); assert(gLabUi.portRow == old); key(UP); key(ENTER); run(300); assert(sumLeds(old) == 0 && ports(lab::Part::Strip) == old - 1 && gHubUnit.leds[old - 1] == 30 && sumLeds(old - 1) == 30); esc(); run(100);
  // A relay can't be reached through its own 1 and 2 keys by accident.
  pickDevice(Dev::Relay); run(400); key('p'); assert(gLabUi.view == LabView::PickPort); esc(); key('1'); run(400); assert(gLabUi.relays[0]); show("hub-relay"); esc();
  // Several parts on the hub together, and I2C units beside it through the 1-to-3 hub.
  plugOnly({0x57, 0x44}, true); gUnits.um = 240000; gHubUnit.level[ports(lab::Part::Motion)] = 1; run(1500);
  assert(labFound(Unit::Hub) && labFound(Unit::Ultrasonic) && labFound(Unit::Env) && gLabInputs.get(Sig::Distance).value == 24 && gLabInputs.get(Sig::Motion).value == 1 && gLabInputs.get(Sig::Knob).value == 50 && !gLabOnWires);
  gHubUnit.level[ports(lab::Part::Motion)] = 0;
  // Out of the hub and straight into Grove, with the same screen still open.
  pickDevice(Dev::Knob); run(100); plugOnly(); gGrove.adc = 700; run(2500); assert(gLabOnWires && gLabBook.direct.white == lab::Part::Knob && gLabInputs.get(Sig::Knob).value == 75); show("moved-to-grove"); assert(gCanvas.shows("ON GROVE") && !gCanvas.shows("P port"));
  key('p'); assert(gLabUi.view == LabView::Device);  // There's no port to pick on Grove.
  // And back: the hub's pull-ups hold the wire high, so the reading can't be a knob. The screen moves by itself.
  plugOnly({}, true); gHubUnit.analog[ports(lab::Part::Knob)] = 700; run(4500); assert(!gLabOnWires && labFound(Unit::Hub) && gLabInputs.get(Sig::Knob).live); show("moved-to-hub"); assert(gCanvas.shows("ON CH3") && !gCanvas.shows("ANSWERING") && gCanvas.shows("P port"));
  esc();
  // A knob at zero isn't mistaken for the bus, so the bus isn't tried: it would only stall.
  plugOnly(); gGrove.adc = 0; run(2500); assert(gLabOnWires); { const auto before = gUnits.commands; run(7000); assert(gUnits.commands == before); }
  // The relay's inputs never see bus traffic once it's on the wires.
  pickDevice(Dev::Relay); run(200); assert(gLabBook.direct.white == lab::Part::Relay); { const auto before = gUnits.commands; esc(); run(7000); assert(gUnits.commands == before); }
  setUiMode(UiMode::Home); plugOnly({0x57, 0x44, 0x63, 0x50}, true); gLabUi = LabUi{};
}

void testGrove() {
  // The owner's photo: a Mini Angle straight on the Grove port, with no I/O hub.
  lab::Book book; book.count = 1; book.rules[0] = lab::recipeRule(0); gLabBook = book;
  gLabBookRead = true; gLabUi = LabUi{}; gDisk.files.clear();
  plugOnly(); gGrove = Grove{}; gGrove.adc = 1400;
  setUiMode(UiMode::LabDevices); run(1500);
  assert(!gLabOnWires);  // Nothing to use yet: it can't be found, only asked for.
  gLabUi.devRow = int(Dev::Knob); gLabUi.devTop = 2; show("grove-list"); assert(gCanvas.shows("press ENTER"));
  key(ENTER); run(1500); assert(gLabOnWires && gLabBook.direct.white == lab::Part::Knob && !gBusOpen);
  show("grove-knob"); assert(gCanvas.shows("50%") && gCanvas.shows("Grove") && gCanvas.shows("ON GROVE") && !gCanvas.shows("ANSWERING") && !gCanvas.shows("hub"));
  assert(gLabInputs.get(Sig::Knob).live && gLabInputs.get(Sig::Knob).value == 50);
  // It follows the knob, both ways, and the ends are the ends.
  // Turning it clockwise lowers the voltage, and that has to read as going up.
  gGrove.adc = 0; run(100); assert(gLabInputs.get(Sig::Knob).value == 100); show("grove-knob-zero");
  gGrove.adc = 2800; run(100); assert(gLabInputs.get(Sig::Knob).value == 0);
  gGrove.adc = 3100; run(100); assert(gLabInputs.get(Sig::Knob).value == 0 && gLabKnobRange.top == 3100); gGrove.adc = 1550; run(100); assert(gLabInputs.get(Sig::Knob).value == 50);
  // The bus stays out of it: the wire is the knob's.
  { const auto before = gUnits.commands; run(1000); assert(gUnits.commands == before); }
  assert(gLabSaved && gDisk.files.count("/lab/rules.dat"));
  // Pull the cable and it stops making numbers up from a floating wire, then comes back.
  gGrove.held = false; run(2000); assert(!gLabInputs.get(Sig::Knob).live); show("grove-unplugged"); assert(gCanvas.shows("Nothing on its wire"));
  gGrove.held = true; run(1000); assert(gLabInputs.get(Sig::Knob).live); show("grove-knob");
  // A single odd check near the logic threshold doesn't drop it.
  gGrove.held = false; run(450); gGrove.held = true; run(100); assert(gLabInputs.get(Sig::Knob).live);
  esc(); run(200); assert(gLabUi.view == LabView::Devices && gLabOnWires); show("grove-list-live"); assert(gCanvas.shows("1 ON THE PORT"));
  // Rules read it, in the steps and when playing, with the servo through the 1-to-3 hub.
  pickDevice(Dev::Servo); run(100); assert(gLabBook.direct.white == lab::Part::Knob && gLabBook.direct.yellow == lab::Part::Servo && gGrove.attached == 3 && gGrove.hz == 50 && gGrove.bits == 14);
  show("grove-servo"); assert(gCanvas.shows("GROVE SIGNAL SENT"));
  assert(gGrove.duty == 0);  // Nothing moves until a key says so.
  keys("c"); assert(gGrove.duty == lab::servoDuty(90)); keys("/"); assert(gGrove.duty == lab::servoDuty(95));
  esc(); assert(gGrove.duty == 0 && gGrove.attached == 3);  // Limp, with the wire still set up.
  setUiMode(UiMode::LabRules); run(1500); assert(gLabOnWires && gGrove.attached == 3); assert(gLabInputs.get(Sig::Knob).live);
  gLabUi.ruleRow = 0; keys("p"); run(500); gGrove.adc = 2800; run(300); assert(gLabStates[0].active && gGrove.duty == lab::servoDuty(0));
  gGrove.adc = 0; run(300); assert(gGrove.duty == lab::servoDuty(180));
  show("grove-play"); gGrove.adc = 1400; run(300); assert(gGrove.duty == lab::servoDuty(90));
  esc(); assert(gGrove.duty == 0);
  // After a restart the Lab remembers what was on the wires.
  setUiMode(UiMode::Home); assert(!gBusOpen && gGrove.attached == -1 && gGrove.mode[1] == INPUT);
  gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(1500); assert(gLabOnWires && gLabBook.direct.white == lab::Part::Knob && gLabBook.direct.yellow == lab::Part::Servo);
  setUiMode(UiMode::LabDevices); run(1500);
  // PIR on the white wire: its signal is plain on or off.
  gGrove.level = false; gGrove.adc = 0; pickDevice(Dev::Motion); run(300); assert(gLabBook.direct.white == lab::Part::Motion && !gLabInputs.get(Sig::Knob).live && gLabInputs.get(Sig::Motion).live);
  gLabMotionCount = 0; gGrove.level = true; run(200); assert(gLabInputs.get(Sig::Motion).value == 1 && gLabMotionCount == 1); show("grove-pir"); assert(gCanvas.shows("Motion!") && gCanvas.shows("Grove")); gGrove.level = false; run(200); esc();
  // Light: analog on white.
  gGrove.adc = 1000; pickDevice(Dev::Light); run(400); assert(gLabBook.direct.white == lab::Part::Light && gLabInputs.get(Sig::Light).value == 76); show("grove-light"); esc();
  // Relay: both wires, high closes, white is relay 1, and it opens on leaving.
  pickDevice(Dev::Relay); run(400); assert(gLabBook.direct.white == lab::Part::Relay && gLabBook.direct.yellow == lab::Part::Relay && gGrove.attached == -1 && gGrove.mode[1] == OUTPUT && gGrove.mode[2] == OUTPUT);
  key('1'); run(400); assert(gGrove.out[1] == 1 && gGrove.out[2] == 0); key('2'); run(400); assert(gGrove.out[2] == 1); show("grove-relay"); assert(gCanvas.shows("GROVE SIGNAL SENT"));
  esc(); assert(gGrove.out[1] == 0 && gGrove.out[2] == 0);
  // Strip: its data on yellow, under the same power limit.
  pickDevice(Dev::Strip); run(200); assert(gLabBook.direct.yellow == lab::Part::Strip && gLabBook.direct.white == lab::Part::None && gRmtLive == 1 && gGrove.ledsLit == 30 && gGrove.lastBlue > 0 && gGrove.lastBlue <= 25);
  show("grove-strip");
  keys("m"); run(300); keys("m"); run(500); assert(gGrove.ledsLit == 1); keys("m"); run(100); assert(gGrove.ledsLit == 1); keys("m"); run(200); assert(gGrove.ledsLit == 30);
  esc(); run(50); assert(gGrove.ledsLit == 0);
  // Swapping one part for another leaves no timer or LED driver behind.
  pickDevice(Dev::Servo); run(100); assert(gRmtLive == 0 && gGrove.attached == 3); esc(); pickDevice(Dev::Strip); run(100); assert(gRmtLive == 1 && gGrove.attached == -1); esc(); pickDevice(Dev::Knob); run(100);
  assert(gLabBook.direct.white == lab::Part::Knob && gLabBook.direct.yellow == lab::Part::Strip); esc();
  // An I2C unit is noticed once the wires go quiet, and takes the port back.
  gGrove.held = false; gUnits.present.insert(0x57); gUnits.um = 240000; run(6000);
  assert(!gLabOnWires && gBusOpen && labFound(Unit::Ultrasonic) && gLabInputs.get(Sig::Distance).value == 24 && !gLabInputs.get(Sig::Knob).live); gGrove.held = true;
  // With a unit on the port the wires can't be shared, and the screen says why.
  pickDevice(Dev::Knob); run(500); assert(!gLabOnWires); show("grove-busy"); assert(gCanvas.shows("Ultrasonic") && gCanvas.shows("unplug these")); esc();
  // The I/O hub always wins, even when a unit leaves just as it arrives, and the same part goes through it.
  gLabBook.direct.yellow = lab::Part::None; plugOnly({}, true); fingers(); run(6000); assert(labFound(Unit::Hub) && !gLabOnWires && gLabInputs.get(Sig::Knob).live);
  // The list notices it while a sensor is on the wires, however it was plugged in.
  plugOnly(); run(3000); assert(gLabOnWires); gGrove.adc = 1400; run(300); assert(gLabInputs.get(Sig::Knob).value == 50);
  plugOnly({}, true); fingers(); run(4000); assert(!gLabOnWires && labFound(Unit::Hub));
  // With a servo on yellow the list leaves the bus alone, and Esc from a screen checks instead.
  plugOnly(); run(3000); assert(gLabOnWires); pickDevice(Dev::Servo); run(100); assert(gLabBook.direct.yellow == lab::Part::Servo); esc();
  { const auto before = gUnits.commands; run(7000); assert(gUnits.commands == before); }
  pickDevice(Dev::Knob); run(200); plugOnly({}, true); fingers(); esc(); assert(!gLabOnWires); run(1500); assert(labFound(Unit::Hub) && gLabInputs.get(Sig::Knob).live);
  setUiMode(UiMode::Home); plugOnly({0x57, 0x44, 0x63, 0x50}, true); gLabBook = lab::Book{}; gLabBook.count = 1; gLabBook.rules[0] = lab::starterRule(); gDisk.files.clear(); gLabUi = LabUi{};
  // The built-in LED, which M5's library leaves off on this board, is driven directly.
  setUiMode(UiMode::LabDevices); run(100); labDriveSet(Out::Led, 120); run(100); assert(gLed.g > 0); labDriveClear(); run(100); assert(gLed.g == 0); setUiMode(UiMode::LabRules); run(1500);
}

// The owner's report: the ultrasonic always says "Nothing at 0x57", after using the knob directly.
bool sonicOnGrove(bool busy, bool deaf, bool knobRemembered) {
  lab::Book book; book.count = 1; book.rules[0] = lab::starterRule();
  if (knobRemembered) book.direct.claim(lab::Part::Knob);
  gLabBook = book; gLabBookRead = true; gLabUi = LabUi{}; gDisk.files.clear();
  gUnits.sonicBusyWhileMeasuring = busy; gUnits.sonicIgnoresEmptyMessage = deaf; gUnits.sonicBusyUntil = 0;
  plugOnly({0x57}); gUnits.um = 240000;
  setUiMode(UiMode::LabDevices); fingers();
  gLabUi.devRow = int(Dev::Ultrasonic); key(ENTER);
  int seen = 0;
  for (int i = 0; i < 600; ++i) { run(10); seen += labFound(Unit::Ultrasonic) && gLabInputs.get(Sig::Distance).value == 24; }
  std::fprintf(stderr, "sonic busy=%d deaf=%d knob=%d: live %d%% of the time, wires=%d found=%d\n", busy, deaf, knobRemembered, seen / 6, gLabOnWires, labFound(Unit::Ultrasonic));
  show("sonic-grove");
  const bool good = !gLabOnWires && labFound(Unit::Ultrasonic) && gLabInputs.get(Sig::Distance).value == 24 && seen >= 500 && !gCanvas.shows("Nothing at 0x57") && gCanvas.shows("ANSWERING");
  esc(); setUiMode(UiMode::Home);
  gUnits.sonicBusyWhileMeasuring = false; gUnits.sonicIgnoresEmptyMessage = false;
  return good;
}
void testSonic() {
  int bad = 0;
  for (int busy = 0; busy < 2; ++busy) for (int deaf = 0; deaf < 2; ++deaf) for (int knob = 0; knob < 2; ++knob) bad += !sonicOnGrove(busy, deaf, knob);
  assert(bad == 0);
  // Swapping the knob for the ultrasonic with the Lab open: its screen takes the bus back at once,
  // and a lost echo or two doesn't drop it.
  gUnits.sonicBusyWhileMeasuring = true; gUnits.sonicIgnoresEmptyMessage = true;
  lab::Book book; book.count = 1; book.rules[0] = lab::starterRule(); book.direct.claim(lab::Part::Knob); gLabBook = book; gLabUi = LabUi{};
  plugOnly(); gGrove = Grove{}; gGrove.adc = 1400; setUiMode(UiMode::LabDevices); run(1500); assert(gLabOnWires);
  gUnits.present.insert(0x57); gUnits.um = 240000; pickDevice(Dev::Ultrasonic); run(2000); assert(!gLabOnWires && labFound(Unit::Ultrasonic) && gLabInputs.get(Sig::Distance).value == 24);
  gUnits.present.erase(0x57); run(300); assert(labFound(Unit::Ultrasonic)); gUnits.present.insert(0x57); run(500); assert(labFound(Unit::Ultrasonic) && gLabSonicFails == 0);
  // Really unplugged, it goes, and the screen says so. Then it comes back by itself.
  gUnits.present.erase(0x57); run(1500); assert(!labFound(Unit::Ultrasonic)); show("sonic-gone"); assert(gCanvas.shows("Nothing at 0x57"));
  gUnits.present.insert(0x57); run(1500); assert(labFound(Unit::Ultrasonic) && gLabInputs.get(Sig::Distance).value == 24);
  // With the I/O hub found, it says this unit can't go on the hub's ports.
  gUnits.present.erase(0x57); gHubUnit.here = true; fingers(); run(1500); show("sonic-on-hub"); assert(gCanvas.shows("can't carry this unit"));
  esc(); gUnits.sonicBusyWhileMeasuring = false; gUnits.sonicIgnoresEmptyMessage = false; setUiMode(UiMode::Home);
  setUiMode(UiMode::Home); plugOnly({0x57, 0x44, 0x63, 0x50}, true); gLabUi = LabUi{};
}

void testGuards() {
  // Keys with Ctrl and the like are left alone, and the voice path keeps the bus quiet.
  gLabUi.view = LabView::Rules; gLabUi.ruleRow = 0; Keyboard_Class::KeysState ctrl; ctrl.ctrl = true; assert(handleLabKey('.', ctrl)); assert(gLabUi.ruleRow == 0);
  gRecording = true; { const auto before = gUnits.commands; run(300); assert(gUnits.commands == before); } gRecording = false;
  gRealtimeActive = true; { const auto before = gUnits.commands; run(300); assert(gUnits.commands == before); } gRealtimeActive = false;
  gPlaybackActive = true; { const auto before = gUnits.commands; run(300); assert(gUnits.commands == before); } gPlaybackActive = false;
  // The network waits while rules play, and only then.
  assert(labAllowsNetwork()); gLabUi.ruleRow = 0; keys("p"); assert(!labAllowsNetwork()); esc(); assert(labAllowsNetwork());
  // The speaker only sounds when it's allowed to.
  labDriveSet(Out::Tone, 500); gToneAllowed = false; { const int tones = M5Cardputer.Speaker.tones; run(100); assert(M5Cardputer.Speaker.tones == tones); } gToneAllowed = true; run(100); assert(gLabToneOn); labDriveClear(); run(100); assert(!gLabToneOn);
  // Esc on a first screen falls through to leave the app, and it does nothing outside the Lab.
  gLabUi.view = LabView::Rules; assert(!routeLabEscape()); gLabUi.view = LabView::Devices; assert(!routeLabEscape());
  gLabUi.view = LabView::Wizard; gLauncherVisible = true; assert(!routeLabEscape()); gLauncherVisible = false; gLabUi.view = LabView::Rules;
  setUiMode(UiMode::Home); assert(!routeLabEscape());
  // Each menu entry opens on its own screen.
  setUiMode(UiMode::LabDevices); assert(gLabUi.view == LabView::Devices); setUiMode(UiMode::LabRules); assert(gLabUi.view == LabView::Rules && gLabOpen); setUiMode(UiMode::Home);
  // Stopping twice, and a service with the Lab closed, are harmless.
  stopLab(); stopLab(); serviceLab();
}


// The pictures in docs/LAB.md's Talk to it section, using the examples it gives.
void testGuidePictures() {
  gDisk.files.clear(); gLabBook = lab::Book{}; freshBoot(); setUiMode(UiMode::LabRules); run(1500);
  { lab::Draft d; d.name("Welcome back"); d.addCond("motion", "on", 0); d.addSay("welcome back"); d.addSet("strip_color", 30); assert(d.usable());
    labOfferRule(d.rule); show("doc-offer-say"); assert(gCanvas.shows("Say \"welcome back\"") && gCanvas.shows("Strip color to 30")); esc(); }
  { lab::Draft a, b, c; a.name("Hand lights"); a.addCond("distance", "under", 30); a.addSet("strip_color", 120); b.name("Too humid"); b.addCond("humidity", "over", 60); b.addSet("relay_1", 1); c.name("Dark room"); c.addCond("light", "under", 20); c.addSet("strip_bright", 40);
    gLabOfferCount = 0; gLabOfferBatch = true; labOfferRule(a.rule); labOfferRule(b.rule); labOfferRule(c.rule); gLabOfferBatch = false; show("doc-ask-offers"); assert(gCanvas.shows("1 OF 3") && gCanvas.shows("Name: Hand lights")); esc(); }
  { lab::Draft m; m.name("Movie time"); m.addCond("spoken", "on", 0); m.addSet("strip_color", 240); assert(m.usable()); gLabBook.rules[0] = m.rule; gLabBook.count = 1; gLabUi.ruleRow = 0; gLabUi.view = LabView::Rules;
    keys("p"); run(200); show("doc-name-waiting"); assert(gCanvas.shows("ALT and say its name") && gCanvas.shows("not yet"));
    VoiceToolAnswer heard; heard.ok = true; heard.heard = "movie time"; labCommandAnswered(heard); run(200); show("doc-name-on"); assert(gCanvas.shows("Movie time on") && gLabStates[0].active); esc(); }
  gLabBook = lab::Book{}; gDisk.files.clear(); setUiMode(UiMode::Home); freshBoot();
}

// --- Sequences -----------------------------------------------------------------------------------
void seqPick(int row) { for (int i = 0; i < 40 && gLabSeqEdit.askRow != row; ++i) key(DOWN); assert(gLabSeqEdit.askRow == row); key(ENTER); }
void seqRowTo(int row) { for (int i = 0; i < 40 && gLabSeqEdit.row != row; ++i) key(DOWN); assert(gLabSeqEdit.row == row); }
void seqListTo(int row) { for (int i = 0; i < 40 && gLabUi.seqRow != row; ++i) key(DOWN); assert(gLabUi.seqRow == row); }
// From the screen that asks, one step, then back on the list of steps.
void addWait(int presses) { assert(gLabUi.view == LabView::SeqAsk && gLabSeqEdit.ask == LabSeqAsk::What); seqPick(0); assert(gLabSeqEdit.ask == LabSeqAsk::Wait); for (int i = 0; i < presses; ++i) key(LEFT); key(ENTER); assert(gLabUi.view == LabView::SeqEdit); }
void addServo(int value) { seqPick(int(Out::Servo) + 1); assert(gLabSeqEdit.ask == LabSeqAsk::Value && gLabSeqEdit.step.value == 90); for (int i = 90; i < value; i += 5) key(RIGHT); for (int i = 90; i > value; i -= 5) key(LEFT); key(ENTER); assert(gLabUi.view == LabView::SeqEdit); }
void addRelay(Out out, bool on) { seqPick(int(out) + 1); assert(gLabSeqEdit.ask == LabSeqAsk::How && gLabSeqEdit.askRow == 0); seqPick(on ? 0 : 1); assert(gLabUi.view == LabView::SeqEdit); }
int stepsOf(int index) { return gLabSeqs[index].count; }
std::string stepAt(int index, int step) { return labStepText(gLabSeqs[index].steps[step]); }

void testSequences() {
  gDisk.files.clear(); gLabBook = lab::Book{}; freshBoot(); fingers();
  setUiMode(UiMode::LabSequences); run(1500);
  // A new device already has one to try, and it needs no parts at all.
  assert(gLabUi.view == LabView::Seqs && gLabSeqCount == 1 && !std::strcmp(gLabSeqs[0].name, "Hello") && labAskOnScreen());
  show("seqs"); assert(gCanvas.shows("Sequences") && gCanvas.shows("Hello") && gCanvas.shows("+ New sequence") && gCanvas.shows("8 steps"));
  // Playing it reaches the Cardputer's own LED and speaker, in order, and everything lets go at the end.
  { const int tones = M5Cardputer.Speaker.tones; key('p'); assert(gLabUi.view == LabView::SeqPlay && gLabSeqRun.running() && gLabSeqRun.owner() == -1 && !labAllowsNetwork()); run(100);
    show("seq-play"); assert(gCanvas.shows("STEP 3/8") && gCanvas.shows("Wait 0.3 sec")); assert(gLabDrive[int(Out::Led)].value == 240 && gLabDrive[int(Out::Tone)].value == 660 && gLed.b > 0);
    run(300); assert(gLabDrive[int(Out::Led)].value == 120 && gLabDrive[int(Out::Tone)].value == 880 && gLed.g > 0); run(300); assert(!gLabDrive[int(Out::Tone)].on && gLabDrive[int(Out::Led)].on);
    assert(M5Cardputer.Speaker.tones > tones); { const int awake = gWakes; run(500); run(100); assert(gWakes >= awake); }
    assert(!gLabSeqRun.running() && !gLabDrive[int(Out::Led)].on && gLed.g == 0 && !gLabToneOn && labAllowsNetwork()); show("seq-done"); assert(gCanvas.shows("DONE") && gCanvas.shows("P play again"));
    key('p'); run(50); assert(gLabSeqRun.running()); esc(); assert(gLabUi.view == LabView::Seqs && !gLabSeqRun.running() && !gLed.g && !gLed.b); }

  // Building one from nothing: the first step is asked for straight away.
  const int servo = ports(lab::Part::Servo), relay = ports(lab::Part::Relay);
  seqListTo(1); key(ENTER); assert(gLabUi.view == LabView::SeqAsk && gLabSeqEdit.index < 0 && gLabSeqEdit.adding); show("seq-what"); assert(gCanvas.shows("What should this step do?") && gCanvas.shows("Wait"));
  // Say and Sequence aren't offered, so a sequence can't talk or start another.
  assert(labSeqAskRows() == 1 + lab::kSeqOutCount && labSeqAskRow(labSeqAskRows() - 1) == "Daikin AC");
  for (int i = 0; i < labSeqAskRows(); ++i) { key(DOWN); show("seq-what-scroll"); }
  // Leaving before a step is chosen keeps nothing.
  esc(); assert(gLabUi.view == LabView::SeqEdit); show("seq-empty"); assert(gCanvas.shows("+ Add a step")); keys("p"); assert(gStatus == "Add a step first" && gLabUi.view == LabView::SeqEdit);
  esc(); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == 1);
  seqListTo(1); key(ENTER);
  addServo(0); assert(gLabSeqEdit.seq.count == 1 && gLabSeqEdit.row == 1);  // It lands on "+ Add a step", so another is one key away.
  key(ENTER); seqPick(0); show("seq-wait"); assert(gCanvas.shows("1 sec")); for (int i = 0; i < 4; ++i) key(LEFT); assert(gLabSeqEdit.step.value == 6); key(ENTER); assert(labStepText(gLabSeqEdit.seq.steps[1]) == "Wait 0.6 sec");
  key(ENTER); addServo(90); key(ENTER); addRelay(Out::Relay1, true); key(ENTER); seqPick(0); key(RIGHT); key(ENTER); assert(labStepText(gLabSeqEdit.seq.steps[4]) == "Wait 1.5 sec");
  // Strip color takes a value or turns off, and off stands for the whole strip.
  key(ENTER); seqPick(int(Out::StripColor) + 1); assert(gLabSeqEdit.ask == LabSeqAsk::How); show("seq-how"); assert(gCanvas.shows("set to a value")); seqPick(0); assert(gLabSeqEdit.ask == LabSeqAsk::Value); show("seq-value"); key(ENTER);
  key(ENTER); seqPick(int(Out::StripBright) + 1); seqPick(1); assert(labStepText(gLabSeqEdit.seq.steps[6]) == "Strip off");
  show("seq-edit"); assert(gCanvas.shows("SAVED ON ESC"));
  // Changing a step opens on what it already says, and a new step can go in the middle.
  seqRowTo(1); key(ENTER); assert(gLabSeqEdit.askRow == 0 && !gLabSeqEdit.adding); key(ENTER); key(RIGHT); key(ENTER); assert(gLabUi.view == LabView::SeqEdit && gLabSeqEdit.seq.count == 7 && gLabSeqEdit.seq.steps[1].value == 7);
  seqRowTo(0); key(ENTER); assert(gLabSeqEdit.askRow == int(Out::Servo) + 1); esc(); assert(gLabUi.view == LabView::SeqEdit);
  seqRowTo(1); keys("a"); assert(gLabSeqEdit.stepAt == 2); addWait(0); assert(gLabSeqEdit.seq.count == 8 && labStepText(gLabSeqEdit.seq.steps[2]) == "Wait 1 sec" && gLabSeqEdit.row == 3);
  // X takes two presses, and anything else between them calls it off.
  seqRowTo(2); keys("x"); show("seq-delete-step"); assert(gCanvas.shows("X again deletes this step")); key(DOWN); assert(!gLabSeqEdit.deleteArmed && gLabSeqEdit.seq.count == 8); key(UP); keys("xx"); assert(gLabSeqEdit.seq.count == 7 && labStepText(gLabSeqEdit.seq.steps[2]) == "Servo to 90`" && !gLabSeqEdit.deleteArmed);
  // Deleting from the bottom of a long list doesn't leave the window past its end.
  { const lab::Sequence kept = gLabSeqEdit.seq; for (int i = gLabSeqEdit.seq.count; i < 9; ++i) gLabSeqEdit.seq.steps[gLabSeqEdit.seq.count++] = lab::SeqStep{}; seqRowTo(8); assert(gLabSeqEdit.top == 4);
    for (int i = 8; i >= 2; --i) { seqRowTo(i); keys("xx"); assert(gLabSeqEdit.top + kLabListRows <= max(labSeqEditRows(), kLabListRows)); } gLabSeqEdit.seq = kept; gLabSeqEdit.top = 0; gLabSeqEdit.row = 0; }
  // Left and right move a step, and stop at the ends.
  seqRowTo(1); key(RIGHT); assert(gLabSeqEdit.row == 2 && labStepText(gLabSeqEdit.seq.steps[1]) == "Servo to 90`"); key(LEFT); assert(gLabSeqEdit.row == 1); seqRowTo(0); key(LEFT); assert(gLabSeqEdit.row == 0); seqRowTo(6); key(RIGHT); assert(gLabSeqEdit.row == 6);
  // Its name, typed over the suggestion, which is kept if nothing is typed.
  seqRowTo(labSeqEditRows() - 1); show("seq-edit-end"); assert(gCanvas.shows("Sequence 2")); key(ENTER); assert(gLabSeqEdit.ask == LabSeqAsk::Name); show("seq-name"); assert(gCanvas.shows("Sequence 2"));
  key(LEFT); assert(!std::strcmp(gLabSeqEdit.seq.name, "Sequence 2") && !gLabSeqEdit.nameTouched);  // An arrow doesn't count as typing.
  for (const char* c = "Door wave"; *c; ++c) key(*c); show("seq-name-typed"); key(ENTER); assert(gLabUi.view == LabView::SeqEdit && !std::strcmp(gLabSeqEdit.seq.name, "Door wave"));
  // Nothing is saved until the way out, and then it's on the card.
  assert(gLabSeqCount == 1); esc(); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == 2 && gLabUi.seqRow == 1 && !std::strcmp(gLabSeqs[1].name, "Door wave") && stepsOf(1) == 7 && gDisk.files.count("/lab/seqs.dat")); show("seqs-two"); assert(gCanvas.shows("7 steps"));
  // A saved name opens ready to correct, not to be replaced.
  key(ENTER); seqRowTo(labSeqEditRows() - 1); key(ENTER); key('\b'); key('E'); assert(!std::strcmp(gLabSeqEdit.seq.name, "Door wavE")); key('\b'); key('e'); key(ENTER); esc(); assert(!std::strcmp(gLabSeqs[1].name, "Door wave"));
  // Back after a restart, and the rules' file was left alone.
  { const bool noRules = !gDisk.files.count("/lab/rules.dat"); setUiMode(UiMode::Home); gLabSeqCount = 0; freshBoot(); setUiMode(UiMode::LabSequences); run(1500); assert(gLabSeqCount == 2 && stepsOf(1) == 7 && stepAt(1, 0) == "Servo to 0`" && noRules == !gDisk.files.count("/lab/rules.dat")); }

  // Playing it moves the real parts in order, on time, through the hub.
  gHubUnit.angle[servo][1] = 77; seqListTo(1); key('p'); run(100); show("seq-play-parts"); assert(gHubUnit.angle[servo][1] == 0 && gCanvas.shows("Door wave") && gCanvas.shows(" left"));
  run(700); assert(gHubUnit.angle[servo][1] == 90 && gHubUnit.written[relay][0] == 1); show("seq-play-wait"); run(1500); assert(gLabSeqRun.shown() == 6 && !gLabDrive[int(Out::StripColor)].on);
  // What it left on goes off at the end, without being told to.
  run(300); assert(!gLabSeqRun.running()); run(400); assert(gHubUnit.written[relay][0] == 0); esc();
  // Opening the menu stops it, the same as it does rules, and it doesn't carry on afterwards.
  key('p'); run(200); assert(gLabSeqRun.running()); gLauncherVisible = true; run(50); assert(!gLabSeqRun.running()); gLauncherVisible = false; run(100); assert(!gLabSeqRun.running()); renderLabUi(); assert(gCanvas.shows("DONE")); esc();
  // So does leaving the Lab.
  key('p'); run(50); setUiMode(UiMode::Home); assert(!gLabSeqRun.running() && !gLabOpen); setUiMode(UiMode::LabSequences); run(1500); assert(gLabUi.view == LabView::Seqs);

  // A loop keeps going until it's stopped, and P from the steps plays what's on the screen.
  seqListTo(1); key(ENTER); seqRowTo(labSeqEditRows() - 2); key(ENTER); assert(gLabSeqEdit.seq.loop); show("seq-loops"); assert(gCanvas.shows("Over and over")); keys("p"); assert(gLabUi.view == LabView::SeqPlay && gLabSeqs[1].loop && gLabUi.seqBack == LabView::SeqEdit);
  run(3500); assert(gLabSeqRun.running()); renderLabUi(); assert(gCanvas.shows("LOOP ")); show("seq-loop"); esc(); assert(gLabUi.view == LabView::SeqEdit && !gLabSeqRun.running());
  while (gLabUi.view != LabView::Seqs) esc();
  gLabSeqs[1].loop = false; labSaveSeqs();

  // A rule plays one: it's on the steps once there's something saved, and asks which.
  setUiMode(UiMode::LabRules); run(1500); labWizardStart(-1, nullptr); gLabWizard.step = LabStep::Out;
  assert(labWizardRowCount() == lab::kOutCount - 1 && labWizardRow(labWizardRowCount() - 1) == "Sequence");
  { const uint8_t kept = gLabSeqCount; gLabSeqCount = 0; assert(labWizardRowCount() == lab::kOutCount - 2); gLabSeqCount = kept; }
  gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
  labWizardStart(-1, nullptr); pickRow(int(Sig::Motion)); pickRow(0); key(ENTER); assert(stepNow() == LabStep::Out); pickRow(labWizardRowCount() - 1); assert(stepNow() == LabStep::PickSeq); show("step-pick-seq"); assert(gCanvas.shows("Play which sequence?") && gCanvas.shows("Door wave"));
  esc(); assert(stepNow() == LabStep::Out && gLabWizard.row == labWizardRowCount() - 1); key(ENTER); pickRow(1); assert(stepNow() == LabStep::MoreActions && gLabWizard.rule.action[0].out == lab::Out::Sequence && gLabWizard.rule.action[0].value == 1);
  key(ENTER); key(ENTER); assert(stepNow() == LabStep::Name); for (const char* c = "At the door"; *c; ++c) key(*c); key(ENTER); assert(gLabUi.view == LabView::Rules);
  const int door = gLabBook.count - 1;
  assert(labActionText(gLabBook.rules[door].action[0], "") == "Play Door wave"); assert(gLabBook.rules[door].holdMs == 2000);
  // Changing that rule comes back to the same sequence.
  gLabUi.ruleRow = door; labWizardStart(door, nullptr); key(ENTER); assert(stepNow() == LabStep::Op); key(ENTER); key(ENTER); assert(stepNow() == LabStep::Out && gLabWizard.row == labWizardRowCount() - 1); key(ENTER); assert(stepNow() == LabStep::PickSeq && gLabWizard.row == 1); gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
  for (int i = 0; i < gLabBook.count; ++i) gLabBook.rules[i].on = i == door;
  gLabBook.rules[door].holdMs = 0;
  // It waits for its rule, starts once, and a sequence with parts on the hub really moves them.
  const int motion = ports(lab::Part::Motion);
  gHubUnit.level[motion] = 0; gHubUnit.angle[servo][1] = 123; keys("p"); run(300); assert(gLabPlaying && !gLabSeqRun.running() && gHubUnit.angle[servo][1] == 123);
  gLabUi.playRule = door; show("flow-seq-waiting"); assert(gCanvas.shows("Door wave") && gCanvas.shows("Sequence"));
  gHubUnit.level[motion] = 1; run(250); assert(gLabSeqRun.running() && gLabSeqRun.owner() == door && gLabStates[door].fires == 1 && gHubUnit.angle[servo][1] == 0); show("flow-seq-firing"); assert(gCanvas.shows("Step ") && gCanvas.shows("Door wave"));
  // Motion ending doesn't cut a play-once sequence short.
  gHubUnit.level[motion] = 0; run(300); assert(!gLabStates[door].active && gLabSeqRun.running()); gLabUi.playRule = door; show("flow-seq-outlasts"); assert(gCanvas.shows("Step "));
  run(2100); assert(!gLabSeqRun.running() && gLabPlaying && gLabStates[door].fires == 1);
  gHubUnit.level[motion] = 1; run(250); assert(gLabSeqRun.running() && gLabStates[door].fires == 2);
  // Pausing stops it and everything it drove, and it doesn't jump back in.
  key(' '); run(100); assert(!gLabSeqRun.running() && gLabPaused && gHubUnit.written[relay][0] == 0); key(' '); run(200); assert(!gLabSeqRun.running()); esc(); assert(gLabUi.view == LabView::Rules && !gLabPlaying);
  // A loop lasts as long as its rule, and comes back after a pause.
  gLabSeqs[1].loop = true; keys("p"); gHubUnit.level[motion] = 1; run(4500); assert(gLabSeqRun.running()); key(' '); run(100); assert(!gLabSeqRun.running()); key(' '); run(100); assert(gLabSeqRun.running());
  gHubUnit.level[motion] = 0; run(300); assert(!gLabStates[door].active && !gLabSeqRun.running()); esc(); gLabSeqs[1].loop = false;
  // Claude is told what's saved, and gets a rule's sequence back by name, not by number.
  { const String seen = labVoiceContext(); assert(std::string(seen).find("saved sequences: Hello, Door wave") != std::string::npos); }
  { const String seen = labAskContext(); assert(std::string(seen).find("saved sequences:\nHello: led set 240") != std::string::npos && std::string(seen).find("\nDoor wave: ") != std::string::npos); assert(std::string(seen).find("\"output\":\"sequence\",\"play\":\"Door wave\"") != std::string::npos); }

  // Deleting one warns about the rules using it, and takes it out of them.
  setUiMode(UiMode::LabSequences); run(100); seqListTo(1); key('x'); show("seq-delete"); assert(gCanvas.shows("1 rule uses it") && gLabUi.seqDeleteArmed); key(UP); assert(!gLabUi.seqDeleteArmed && gLabSeqCount == 2);
  seqListTo(0); key('x'); renderLabUi(); assert(gCanvas.shows("X again deletes this sequence")); key(DOWN);  // Hello isn't used by anything.
  seqListTo(0); key('x'); key('x'); assert(gLabSeqCount == 1 && !std::strcmp(gLabSeqs[0].name, "Door wave") && gLabBook.rules[door].action[0].value == 0);  // Its rule moves with it.
  seqListTo(0); key('x'); key('x'); assert(gLabSeqCount == 0 && gLabBook.rules[door].actions == 0); show("seqs-empty"); assert(gCanvas.shows("Rules can start them"));
  // It stays gone after a restart, rather than the starter coming back.
  setUiMode(UiMode::Home); freshBoot(); setUiMode(UiMode::LabSequences); run(100); assert(gLabSeqCount == 0 && gLabBook.rules[door].actions == 0);
  // A rule from a card with no sequences on it does nothing, and says so.
  gLabBook.rules[door].actions = 1; gLabBook.rules[door].action[0] = lab::Action{Out::Sequence, lab::How::Set, 5, Sig::Knob}; assert(labActionText(gLabBook.rules[door].action[0], "") == "Play a missing sequence");
  setUiMode(UiMode::LabRules); run(1500); gLabUi.ruleRow = 0; gHubUnit.level[motion] = 1; keys("p"); run(300); assert(gLabStates[door].active && !gLabSeqRun.running()); gLabUi.playRule = door; show("flow-seq-missing"); esc();
  // A card that can't be written says so.
  setUiMode(UiMode::LabSequences); run(100); gDisk.broken = true; key(ENTER); addServo(45); esc(); assert(gLabSeqCount == 1 && !gLabSaved && gStatus == "Sequences not saved"); renderLabUi(); gDisk.broken = false;
  // The list fits, and the most steps there can be.
  while (gLabSeqCount < lab::kMaxSequences) { gLabSeqs[gLabSeqCount] = lab::starterSequence(); ++gLabSeqCount; } assert(labSeqRows() == lab::kMaxSequences); seqListTo(lab::kMaxSequences - 1); show("seqs-full");
  key(ENTER); for (int i = stepsOf(lab::kMaxSequences - 1); i < lab::kMaxSeqSteps; ++i) { keys("a"); addWait(0); seqRowTo(gLabSeqEdit.seq.count - 1); } assert(gLabSeqEdit.seq.count == lab::kMaxSeqSteps && !labSeqHasAddRow()); keys("a"); assert(gLabUi.view == LabView::SeqEdit); show("seq-full");
  for (int i = 0; i < labSeqEditRows(); ++i) { key(DOWN); show("seq-full-scroll"); } keys("p"); run(2000); show("seq-play-long"); esc(); esc();
  // The pictures in docs/LAB.md: the example from the start of its Sequences section.
  setUiMode(UiMode::Home); gLabBook = lab::Book{}; gDisk.files.clear(); freshBoot(); setUiMode(UiMode::LabSequences); run(1500);
  { lab::Sequence& w = gLabSeqs[1]; w = lab::Sequence{}; std::strcpy(w.name, "Welcome"); using lab::SeqKind; using lab::SeqStep;
    for (const SeqStep& step : {SeqStep{SeqKind::Set, Out::StripColor, 120}, SeqStep{SeqKind::Wait, Out::Servo, 10}, SeqStep{SeqKind::Set, Out::Servo, 90}, SeqStep{SeqKind::Wait, Out::Servo, 20}, SeqStep{SeqKind::Set, Out::Servo, 0}, SeqStep{SeqKind::Off, Out::StripColor, 0}}) w.steps[w.count++] = step;
    gLabSeqCount = 2; labSaveSeqs(); }
  gLabUi.seqTop = 0; gLabUi.seqRow = 1; show("doc-seqs"); assert(gCanvas.shows("Welcome") && gCanvas.shows("6 steps"));
  key(ENTER); show("doc-seq-edit"); assert(gCanvas.shows("Strip color to 120") && gCanvas.shows("Wait 2 sec")); keys("p"); run(1300); show("doc-seq-play"); assert(gCanvas.shows("STEP 4/6") && gCanvas.shows(" left")); esc(); esc();
  setUiMode(UiMode::LabRules); run(1500); gLabBook.rules[0].holdMs = 0; lab::setOnOff(gLabBook.rules[0].cond[0], true); gLabBook.rules[0].cond[0].sig = Sig::Motion; std::strcpy(gLabBook.rules[0].name, "At the door"); gLabBook.rules[0].conds = 1; gLabBook.rules[0].actions = 1; gLabBook.rules[0].action[0] = lab::Action{Out::Sequence, lab::How::Set, 1, Sig::Knob};
  labWizardStart(0, nullptr); key(ENTER); key(ENTER); key(ENTER); key(ENTER); show("doc-pick-seq"); assert(stepNow() == LabStep::PickSeq && gCanvas.shows("Welcome")); gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
  gLabUi.ruleRow = 0; gHubUnit.level[motion] = 0; keys("p"); run(300); gHubUnit.level[motion] = 1; run(1400); show("doc-flow-seq"); assert(gCanvas.shows("Step 4") && gCanvas.shows("Welcome")); esc();
  gLabSeqCount = 0; gLabBook = lab::Book{}; gDisk.files.clear(); setUiMode(UiMode::Home); freshBoot();
  gHubUnit.level[motion] = 0;
}

// --- What the chat can reach ------------------------------------------------------------------
lab::Sequence chatSequence(const char* name, std::initializer_list<lab::SeqStep> steps, bool loop = false) {
  lab::Sequence seq; std::strcpy(seq.name, name); seq.loop = loop;
  for (const lab::SeqStep& step : steps) seq.steps[seq.count++] = step;
  return seq;
}
void testChat() {
  using lab::SeqKind; using lab::SeqStep;
  const lab::Sequence wave = chatSequence("Door wave", {SeqStep{SeqKind::Set, Out::StripColor, 120}, SeqStep{SeqKind::Wait, Out::Servo, 10}, SeqStep{SeqKind::Set, Out::Servo, 90},
                                                        SeqStep{SeqKind::Wait, Out::Servo, 20}, SeqStep{SeqKind::Set, Out::Servo, 0}, SeqStep{SeqKind::Off, Out::StripColor, 0}});
  gDisk.files.clear(); gLabBook = lab::Book{}; freshBoot(); fingers();
  // Before the card has been read it says nothing, rather than that nothing is saved.
  gLabBookRead = false; gLabSeqCount = 0; assert(labChatContext().empty());
  // Read at boot, it can tell the chat what's saved with the Lab shut and nothing on the bus.
  labReadBook(); assert(!gLabOpen);
  { const std::string said = labChatContext(); assert(said.find("saved rules:\n") == 0 && said.find("saved sequences:\nHello: led set 240") != std::string::npos && said.find("; hold ") != std::string::npos && !gLabOpen); }
  { lab::Book kept = gLabBook; const uint8_t seqs = gLabSeqCount; gLabBook.count = 0; gLabSeqCount = 0; assert(labChatContext() == "saved rules: none\nsaved sequences: none\n"); gLabBook = kept; gLabSeqCount = seqs; }

  // A change names its rule, from anywhere, and goes through the same screen as a spoken one.
  setUiMode(UiMode::LabRules); run(1500);
  lab::Draft first, second; first.name("Wave red"); first.addCond("distance", "under", 30); first.addSet("strip_color", 0); second.name("Too humid"); second.addCond("humidity", "over", 60); second.addSet("relay_1", 1);
  gLabBook.rules[0] = first.rule; gLabBook.rules[1] = second.rule; gLabBook.rules[1].on = false; gLabBook.count = 2; labSaveBook();
  setUiMode(UiMode::Home); assert(!gLabOpen);
  { const std::string said = labChatContext(); assert(said.find("Wave red: any distance under 30; do strip_color set 0; hold 2000\n") != std::string::npos && said.find("Too humid (switched off): any humidity over 60") != std::string::npos); }
  lab::Rule blue = first.rule; blue.action[0].value = 240; std::strcpy(blue.name, "Wave blue");
  assert(labOfferChange(blue, "too HUMID") && gUiMode == UiMode::LabRules && gLabOpen && gLabUi.view == LabView::Offer && gLabOfferChanges == 1 && gLabOfferNext == -1);
  show("chat-change-offer"); assert(gCanvas.shows("Claude changed this")); esc(); assert(gLabBook.count == 2 && !std::strcmp(gLabBook.rules[1].name, "Too humid"));
  assert(labOfferChange(blue, "wave red") && gLabOfferChanges == 0); key(ENTER); assert(gLabUi.view == LabView::Rules && gLabBook.count == 2 && gLabBook.rules[0].action[0].value == 240 && !std::strcmp(gLabBook.rules[0].name, "Wave blue") && gLabBook.rules[0].on);
  // A switched off rule stays off when it's changed, the same as a spoken change.
  assert(labOfferChange(blue, "Too humid")); key(ENTER); assert(!gLabBook.rules[1].on && !std::strcmp(gLabBook.rules[1].name, "Wave blue")); std::strcpy(gLabBook.rules[1].name, "Too humid");
  assert(labOfferChange(blue, "Too humid")); key('e'); assert(gLabUi.view == LabView::Wizard && gLabWizard.index == 1); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabUi.view != LabView::Wizard && !gLabBook.rules[1].on); std::strcpy(gLabBook.rules[1].name, "Too humid");
  // A name that isn't there changes nothing, and doesn't add a second rule beside the first.
  setUiMode(UiMode::Home); assert(!labOfferChange(blue, "Ghost") && gUiMode == UiMode::Home && !gLabOpen && gLabBook.count == 2);
  // No name is a new rule, and a spoken change keeps naming its row the way it always did.
  assert(labOfferChange(blue, "") && gLabOfferChanges < 0); esc();
  gLabOfferNext = 1; assert(labOfferChange(blue, "") && gLabOfferChanges == 1 && gLabOfferNext == 1); gLabOfferNext = -1; esc();

  // A new sequence comes in from anywhere, waits to be looked at, and nothing runs or saves until then.
  setUiMode(UiMode::Home); const auto onCard = gDisk.files; const uint8_t saved = gLabSeqCount;
  assert(labOfferSequence(wave, "") && gUiMode == UiMode::LabSequences && gLabOpen && gLabUi.view == LabView::SeqOffer && gLabSeqEdit.index < 0);
  run(200); show("chat-seq-offer");
  assert(gCanvas.shows("Claude wrote this") && gCanvas.shows("Strip color to 120") && gCanvas.shows("Wait 1 sec") && gCanvas.shows("Servo to 0") && gCanvas.shows("Strip off") && gCanvas.shows("Name: Door wave, plays once") && gCanvas.shows("ENTER save") && !gCanvas.shows("more"));
  assert(gLabSeqCount == saved && !gLabSeqRun.running() && gDisk.files == onCard && !gLabDrive[int(Out::StripColor)].on);
  // It doesn't listen, like a rule waiting for Enter.
  assert(!labSayOnScreen() && !labChangeOnScreen() && !labAskOnScreen() && !labCommandOnScreen());
  esc(); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == saved && gDisk.files == onCard);
  // Enter keeps it, on the card, and shows where it landed.
  assert(labOfferSequence(wave, "")); key(ENTER); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == saved + 1 && gLabUi.seqRow == saved && gDisk.files != onCard && !std::strcmp(gLabSeqs[saved].name, "Door wave") && gLabSeqs[saved].count == 6 && !gLabSeqRun.running());
  { const std::string said = labChatContext(); assert(said.find("Door wave: strip_color set 120, wait_ms 1000, servo set 90, wait_ms 2000, servo set 0, strip_color off\n") != std::string::npos); }
  // E opens it on the steps, and Esc saves from there as it does for any sequence.
  { lab::Sequence other = wave; std::strcpy(other.name, "Other"); assert(labOfferSequence(other, "")); const auto before = gDisk.files; key('e'); assert(gLabUi.view == LabView::SeqEdit && gLabSeqEdit.seq.count == 6 && gLabSeqEdit.index < 0 && gLabSeqEdit.offered);
    show("chat-seq-edit"); assert(gCanvas.shows("Strip color to 120") && gCanvas.shows("NOT SAVED") && !gCanvas.shows("P play"));
    // P can't save it or run it from here, whatever is in the steps, and neither can Esc.
    key('p'); assert(gLabUi.view == LabView::SeqEdit && !gLabSeqRun.running() && gLabSeqCount == saved + 1 && gDisk.files == before && gStatus.find("Save it first") != std::string::npos);
    seqRowTo(labSeqEditRows() - 2); show("chat-seq-edit-end"); assert(gCanvas.shows("ESC back") && !gCanvas.shows("P play")); key(ENTER); assert(gLabSeqEdit.seq.loop);
    esc(); assert(gLabUi.view == LabView::SeqOffer && gLabSeqCount == saved + 1 && gDisk.files == before); run(50); renderLabUi(); assert(gCanvas.shows("Claude wrote this") && gCanvas.shows("loops"));
    // Enter is the only yes, and it saves what's on the screen.
    key(ENTER); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == saved + 2 && !std::strcmp(gLabSeqs[saved + 1].name, "Other") && gLabSeqs[saved + 1].loop && !gLabSeqRun.running());
    // Its steps can be changed and it can still be dropped.
    { const uint8_t kept = gLabSeqCount; lab::Sequence third = wave; std::strcpy(third.name, "Third"); assert(labOfferSequence(third, "")); key('e'); esc(); esc(); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == kept); }
    // A second reply replaces an offer being changed, and doesn't save it on the way.
    { const uint8_t kept = gLabSeqCount; lab::Sequence third = wave; std::strcpy(third.name, "Third"); assert(labOfferSequence(third, "")); key('e'); assert(gLabUi.view == LabView::SeqEdit);
      assert(!labOfferSequence(third, "Ghost") && gLabUi.view == LabView::SeqEdit && gLabSeqCount == kept); lab::Sequence fourth = third; std::strcpy(fourth.name, "Fourth"); assert(labOfferSequence(fourth, "") && gLabSeqCount == kept && !std::strcmp(gLabSeqEdit.seq.name, "Fourth") && gLabUi.view == LabView::SeqOffer); esc(); }
    lab::removeSequence(gLabSeqs, gLabSeqCount, gLabBook.rules, gLabBook.count, saved + 1); labSaveSeqs(); }

  // Keys typed for the chat don't reach an offer that lands under them, for as long as the typing goes on.
  { const uint8_t kept = gLabSeqCount; const lab::Sequence s = chatSequence("Typed", {SeqStep{SeqKind::Set, Out::Relay1, 1}});
    assert(labOfferSequence(s, "")); for (const char* c = "keep the step\n"; *c; ++c) { pressNow(*c); nowMs += 300; pressNow('\n'); nowMs += 300; } assert(gLabUi.view == LabView::SeqOffer && gLabSeqCount == kept && !gLabSeqRun.running());
    nowMs += 1000; pressNow('\n'); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == kept + 1); lab::removeSequence(gLabSeqs, gLabSeqCount, gLabBook.rules, gLabBook.count, kept); labSaveSeqs();
    assert(labOfferChange(blue, "") && gLabUi.view == LabView::Offer); const uint8_t rules = gLabBook.count; pressNow('\n'); assert(gLabUi.view == LabView::Offer && gLabBook.count == rules); nowMs += 300; pressNow('\n'); assert(gLabUi.view == LabView::Offer); nowMs += 1000; pressNow('e'); assert(gLabUi.view == LabView::Wizard); gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
    // After the wait keys come as fast as they like, so a long list scrolls.
    lab::Sequence longest = s; longest.count = lab::kMaxSeqSteps; assert(labOfferSequence(longest, "")); nowMs += 1000; pressNow(DOWN); pressNow(DOWN); pressNow(DOWN); assert(gLabSeqEdit.top == 3); esc(); }

  // A change shows what's different, keeps its place, and a rule playing it keeps playing it.
  lab::Sequence slower = wave; slower.steps[1].value = 30; slower.steps[2].value = 45; std::strcpy(slower.name, "Slow wave");
  gLabBook.rules[0].actions = 1; gLabBook.rules[0].action[0] = lab::Action{Out::Sequence, lab::How::Set, int16_t(saved), Sig::Knob};
  assert(!labOfferSequence(slower, "Ghost") && gLabUi.view == LabView::Seqs);
  assert(labOfferSequence(slower, "door wave") && gLabSeqEdit.index == saved); show("chat-seq-change"); assert(gCanvas.shows("Claude changed this") && gCanvas.shows("Wait 3 sec") && gCanvas.shows("Servo to 45") && gCanvas.shows("Name: Slow wave"));
  esc(); assert(!std::strcmp(gLabSeqs[saved].name, "Door wave") && gLabSeqs[saved].steps[1].value == 10);
  assert(labOfferSequence(slower, "door wave")); key(ENTER); assert(gLabSeqCount == saved + 1 && !std::strcmp(gLabSeqs[saved].name, "Slow wave") && gLabSeqs[saved].steps[1].value == 30 && gLabBook.rules[0].action[0].value == saved);
  // What the rows can't show still stands out, so a grey screen is one where nothing changed.
  { lab::Sequence shorter = gLabSeqs[saved]; shorter.count = 4; assert(labOfferSequence(shorter, "")); show("chat-seq-fewer"); assert(gCanvas.shows("was 6 steps") && gCanvas.shows("Slow wave")); esc();
    lab::Sequence looped = gLabSeqs[saved]; looped.loop = true; assert(labOfferSequence(looped, "")); renderLabUi(); assert(gCanvas.shows("loops")); esc(); }
  // The same name with nothing said about it is that one too, because two couldn't be told apart.
  { lab::Sequence again = slower; again.steps[1].value = 5; assert(labOfferSequence(again, "") && gLabSeqEdit.index == saved); renderLabUi(); assert(gCanvas.shows("Claude changed this")); esc(); }

  // One that's still being changed on the screen is kept, not overwritten.
  seqListTo(0); key(ENTER); assert(gLabUi.view == LabView::SeqEdit); gLabSeqEdit.seq.loop = !gLabSeqs[0].loop; { const bool loop = gLabSeqEdit.seq.loop; assert(labOfferSequence(wave, "") && gLabSeqs[0].loop == loop); esc(); gLabSeqs[0].loop = false; labSaveSeqs(); }

  // Whatever was running stops first, so nothing is left going under the offer.
  seqListTo(0); key('p'); run(100); assert(gLabSeqRun.running()); assert(labOfferSequence(wave, "") && !gLabSeqRun.running() && gLabUi.view == LabView::SeqOffer); esc();
  setUiMode(UiMode::LabRules); run(300); keys("p"); run(300); assert(gLabPlaying); assert(labOfferSequence(wave, "") && !gLabPlaying && gLabUi.view == LabView::SeqOffer); esc();

  // The most steps there can be scroll, and stop at both ends.
  { lab::Sequence longest; std::strcpy(longest.name, "Fifteen letters"); longest.loop = true; longest.count = lab::kMaxSeqSteps;
    for (int i = 0; i < lab::kMaxSeqSteps; ++i) longest.steps[i] = i % 2 ? SeqStep{SeqKind::Wait, Out::Servo, lab::kMaxWaitTenths} : SeqStep{SeqKind::Set, Out::StripBright, 100};
    assert(labOfferSequence(longest, "")); show("chat-seq-long"); assert(gCanvas.shows("Name: Fifteen letters, loops") && gCanvas.shows("more"));
    key(UP); assert(gLabSeqEdit.top == 0); for (int i = 0; i < 30; ++i) key(DOWN); assert(gLabSeqEdit.top == lab::kMaxSeqSteps - 6); show("chat-seq-long-end"); assert(gCanvas.shows("16"));
    key('e'); assert(gLabSeqEdit.top == 0 && gLabUi.view == LabView::SeqEdit); esc(); esc();
    // A full list can't take another, and says what to do. A change to a saved one still goes through.
    const uint8_t was = gLabSeqCount; while (gLabSeqCount < lab::kMaxSequences) { gLabSeqs[gLabSeqCount] = lab::starterSequence(); std::strcpy(gLabSeqs[gLabSeqCount].name, "Spare"); ++gLabSeqCount; }
    assert(labOfferSequence(longest, "")); key(ENTER); assert(gLabUi.view == LabView::SeqOffer && gLabSeqCount == lab::kMaxSequences && gStatus.find("full") != std::string::npos);
    key('e'); assert(gLabUi.view == LabView::SeqEdit); esc(); key(ENTER); assert(gLabUi.view == LabView::SeqOffer && gLabSeqCount == lab::kMaxSequences); esc();
    assert(labOfferSequence(wave, "Spare")); key(ENTER); assert(gLabSeqCount == lab::kMaxSequences && !std::strcmp(gLabSeqs[was].name, "Door wave"));
    gLabSeqCount = was; }

  // Alt on the list of sequences can write one, and it lands on the same review, not the answer screen.
  setUiMode(UiMode::LabSequences); run(300); gLabUi.seqRow = 0; gRecording = false; gThinking = false;
  assert(labAskOnScreen());
  { gRecording = true; renderLabUi(); assert(gCanvas.shows("Describe the steps")); gRecording = false; gThinking = true; renderLabUi(); assert(gCanvas.shows("before it's saved")); gThinking = false;
    gLabUi.view = LabView::Devices; gRecording = true; renderLabUi(); assert(gCanvas.shows("Ask about your parts") && !gCanvas.shows("Describe the steps")); gRecording = false; gLabUi.view = LabView::Seqs; }
  { const uint8_t kept = gLabSeqCount; const auto cardNow = gDisk.files; lab::Sequence spoken = chatSequence("Blink", {SeqStep{SeqKind::Set, Out::Led, 120}, SeqStep{SeqKind::Wait, Out::Servo, 10}});
    gActionSequence = &spoken; { VoiceToolAnswer a; a.ok = true; a.heard = "blink the led"; a.reply = "Here it is."; a.actionsJson = "[{}]"; labAskAnswered(a); } gActionSequence = nullptr;
    assert(gLabUi.view == LabView::SeqOffer && gLabSeqEdit.index < 0 && gLabSeqCount == kept && gDisk.files == cardNow && std::string(gQueuedVoiceReplySpeech).empty());
    run(200); show("ask-seq-offer"); assert(gCanvas.shows("Claude wrote this") && gCanvas.shows("Name: Blink, plays once")); key(ENTER); assert(gLabUi.view == LabView::Seqs && gLabSeqCount == kept + 1 && !gLabSeqRun.running());
    // One it can't use leaves the answer on the screen, so nothing just disappears.
    { VoiceToolAnswer a; a.ok = true; a.heard = "blink the led"; a.reply = "Claude's words."; a.actionsJson = "[{}]"; labAskAnswered(a); assert(gLabUi.view == LabView::Say && gLabUi.sayNote == "Claude's words."); esc(); assert(gLabUi.view == LabView::Seqs); }
    lab::removeSequence(gLabSeqs, gLabSeqCount, gLabBook.rules, gLabBook.count, kept); labSaveSeqs(); }
  // "Make it slower" needs to know which one, and that's only true on the list, on a real row.
  { const int row = gLabUi.seqRow; gLabUi.seqRow = 1; assert(gLabSeqCount > 1 && std::string(labAskContext()).find(std::string("\npicked in the list: ") + gLabSeqs[1].name) != std::string::npos);
    gLabUi.seqRow = gLabSeqCount; assert(std::string(labAskContext()).find("picked in the list") == std::string::npos);
    gLabUi.seqRow = 0; gLabUi.view = LabView::Devices; assert(std::string(labAskContext()).find("picked in the list") == std::string::npos); gLabUi.view = LabView::Seqs; assert(std::string(labVoiceContext()).find("picked in the list") == std::string::npos); gLabUi.seqRow = row; }
  // Say a rule and changing a rule still only write rules, and only Ask the Lab sees the steps.
  { gLabUi.view = LabView::Say; gLabUi.voice = LabVoice::NewRule; const String names = labVoiceContext(); assert(std::string(names).find("wait_ms") == std::string::npos && std::string(names).find("saved sequences: ") != std::string::npos); gLabUi.view = LabView::Seqs; }

  // The steps can use up the room, and what gives way is the diary's oldest hours, not its newest.
  { const uint8_t kept = gLabSeqCount; const lab::Sequence longest = [] { lab::Sequence seq; std::strcpy(seq.name, "Fifteen letters"); seq.count = lab::kMaxSeqSteps; for (int i = 0; i < lab::kMaxSeqSteps; ++i) seq.steps[i] = SeqStep{SeqKind::Set, Out::StripBright, 100}; return seq; }();
    std::string diary; for (int hour = 0; hour < 24; ++hour) { char line[160]; std::snprintf(line, sizeof(line), "2026-11-%02d %02d: motion 1, rules fired 2, distance 3 to 4, temperature 20 to 21, humidity 40 to 41\n", 1 + hour / 24, hour); diary += line; }
    gDisk.files[kLabDiaryPath] = std::vector<uint8_t>(diary.begin(), diary.end());
    { const std::string roomy = labAskContext(); assert(roomy.find("2026-11-01 23:") != std::string::npos); }
    while (gLabSeqCount < lab::kMaxSequences) gLabSeqs[gLabSeqCount++] = longest;
    { const std::string full = labAskContext(); assert(full.find("2026-11-01 23: motion 1") != std::string::npos && full.find("humidity 40 to 41\n") != std::string::npos); assert(full.size() < 4000 && full.compare(full.size() - 1, 1, "\n") == 0); assert(full.find("\nrule 1") < full.find("saved sequences:\n")); }
    gLabSeqCount = kept; gDisk.files.erase(kLabDiaryPath); }

  // The pictures in docs/LAB.md.
  setUiMode(UiMode::Home); gLabBook = lab::Book{}; gDisk.files.clear(); freshBoot(); setUiMode(UiMode::Home); labReadBook();
  assert(labOfferSequence(wave, "")); run(200); show("doc-chat-seq"); key(ENTER); assert(gLabSeqCount == 2);
  { lab::Sequence slow = wave; slow.steps[1].value = 30; slow.steps[3].value = 40; assert(labOfferSequence(slow, "Door wave")); run(200); show("doc-chat-seq-change"); assert(gCanvas.shows("Claude changed this") && gCanvas.shows("Wait 3 sec")); esc(); }
  gLabSeqCount = 0; gLabBook = lab::Book{}; gDisk.files.clear(); setUiMode(UiMode::Home); freshBoot();
}

void startOnceFromAction(const void* once) { labOnceStart(static_cast<const lab::Once*>(once)); }
lab::Once onceOver(int over, Out out, int set) {
  lab::Once once; once.conds = 1; once.cond[0] = {Sig::Knob, lab::Op::Above, int16_t(over)};
  once.yes = 1; once.then[0] = {lab::SeqKind::Set, out, int16_t(set)};
  return once;
}
lab::Once onceDo(Out out, int set, lab::SeqKind kind = lab::SeqKind::Set) { lab::Once once; once.yes = 1; once.then[0] = {kind, out, int16_t(set)}; return once; }
lab::Once onceRead(Sig sig) { lab::Once once; once.reads = 1; once.read[0] = sig; return once; }
// A knob on the white wire and a strip on the yellow one, with every screen closed.
void onceOnGrove(lab::Part yellow = lab::Part::Strip) {
  setUiMode(UiMode::Home); run(100); gLabDrive[int(Out::Led)] = {}; gLabLedSent = -1; gLed = {};
  for (Out out : {Out::StripColor, Out::StripBright, Out::StripLength}) gLabDrive[int(out)] = {};
  gLabBook = lab::Book{}; gLabBook.direct.white = lab::Part::Knob; gLabBook.direct.yellow = yellow;
  gLabBookRead = gLabBookKnown = true; gDisk.files.clear(); plugOnly(); gGrove = Grove{}; gGrove.adc = 700; gReplies.clear(); gRepliesSpoken.clear(); gLabOnceText[0] = 0;
  gQueuedVoiceReplySpeech = ""; gPlaybackActive = gRecording = gRealtimeActive = gRealtimeExclusiveMode = gLauncherVisible = gRadioBusy = false; gToneAllowed = true;
}
std::string onceAsk(const lab::Once& once, int ms = 4500) {
  gReplies.clear(); gRepliesSpoken.clear(); labOnceStart(&once); run(ms); assert(!labOnceBusy() && gReplies.size() == 1 && !gBusOpen && gRmtLive == 0);
  return gReplies[0];
}
void testOnce() {
  // What the owner asked for: from another screen, the knob over 50 turns the strip red.
  onceOnGrove();
  { const lab::Once once = onceOver(50, Out::StripColor, 0);
    assert(onceAsk(once) == "Knob 75%, over 50. Strip set to red.");
    assert(gUiMode == UiMode::Home && gGrove.ledsLit == 30 && gGrove.lastRed > 0 && gGrove.lastGreen == 0 && gGrove.lastBlue == 0 && !gGrove.scrambled);
    assert(!gRepliesSpoken[0]);  // Typed, with a reply too short to speak, so the report is only shown.
    assert(gLabBook.count == 0 && !gDisk.files.count("/lab/rules.dat"));  // No rule, and nothing watching.
    assert(gGrove.mode[2] == INPUT_PULLDOWN);  // The strip stays lit, on a data line that can't float.
    { const auto bus = gUnits.commands; const int frames = gGrove.frames; run(5000); assert(gUnits.commands == bus && gGrove.frames == frames && gAirSensor.open_ == false); }
    // It answers from what's there now, and what was left lit stays lit through the next look.
    gGrove.adc = 1960; assert(onceAsk(once) == "Knob 30%, not over 50. Nothing changed."); assert(gGrove.ledsLit == 30 && gGrove.lastRed > 0 && !gGrove.scrambled);
    // And if the sensor's gone, the strip still comes back from the bus traffic before the port goes.
    gGrove.held = false; assert(onceAsk(once, 5000) == "No reading from Knob. Nothing changed."); assert(gGrove.ledsLit == 30 && gGrove.lastRed > 0 && !gGrove.scrambled); gGrove.held = true; }
  { lab::Once once = onceOver(50, Out::StripColor, 0); once.no = 1; once.otherwise[0] = {lab::SeqKind::Set, Out::StripColor, 120};
    assert(onceAsk(once) == "Knob 30%, not over 50. Strip set to green."); assert(gGrove.lastGreen > 0 && gGrove.lastRed == 0); }
  // Turning it off from the chat goes the same way, and nothing's left to look after.
  assert(onceAsk(onceDo(Out::StripColor, 0, lab::SeqKind::Off)) == "Strip off."); assert(gGrove.ledsLit == 0 && gGrove.mode[2] == INPUT);
  // The report goes along with the next message, so "what did it read?" has an answer.
  assert(labChatContext().find("last check: Strip off.\n") != std::string::npos);
  // A reading alone. Tilt is on the board, so the port isn't touched at all.
  { onceOnGrove(); const int opens = gAirSensor.opens; const auto bus = gUnits.commands;
    assert(onceAsk(onceRead(Sig::Tilt)) == "Tilt 30 deg."); assert(gAirSensor.opens == opens && gUnits.commands == bus && gGrove.frames == 0); }
  { onceOnGrove(); const int opens = gAirSensor.opens; assert(onceAsk(onceDo(Out::Led, 120)) == "LED set to green."); assert(gLed.g > 0 && gAirSensor.opens == opens); }
  // I2C units are found the way the Lab finds them, and two readings make one report.
  { onceOnGrove(); gLabBook.direct = lab::Direct{}; plugOnly({0x44}); lab::Once once; once.reads = 2; once.read[0] = Sig::Temperature; once.read[1] = Sig::Humidity;
    const std::string said = onceAsk(once); assert(said.find("Temperature ") == 0 && said.find(" C; Humidity ") != std::string::npos && said.back() == '.'); }
  // Nothing plugged in means no answer, and no guess from a floating wire.
  { onceOnGrove(); gGrove.held = false; const lab::Once once = onceOver(50, Out::StripColor, 0); const int frames = gGrove.frames;
    assert(onceAsk(once, 5000) == "No reading from Knob. Nothing changed."); assert(gGrove.frames == frames && gGrove.ledsLit == 0); }
  // A part that isn't where the Lab thinks says so, and still gives the reading it did get.
  { onceOnGrove(lab::Part::None); const lab::Once once = onceOver(50, Out::StripColor, 0);
    assert(onceAsk(once, 5000) == "Knob 75%, over 50. LED strip isn't connected. Nothing changed."); }
  // On the I/O hub. A knob there has no scale until it's been turned, so its number isn't given.
  { onceOnGrove(); gLabBook.direct = lab::Direct{}; gLabBook.wiring.port[0] = lab::Part::Knob; gLabBook.wiring.port[2] = lab::Part::Strip;
    plugOnly({}, true); gLabKnobRange = lab::AnalogRange{}; gHubUnit.analog[0] = 2000;
    const lab::Once once = onceOver(50, Out::StripColor, 240);
    assert(onceAsk(once) == "Knob has no range yet, so turn it end to end in the Lab. Nothing changed.");
    gLabKnobRange.top = 3000; gLabKnobRange.low = 10; gHubUnit.analog[0] = 900;
    assert(onceAsk(once) == "Knob 70%, over 50. Strip set to blue.");
    assert((gHubUnit.ledRgb[2][0] & 0xFF) > 0);  // The hub keeps driving it after the bus has gone.
    // A scale learned somewhere else doesn't count: R, the Grove wires, or a part turned only partway.
    gLabKnobRange = lab::AnalogRange::fixedAt(lab::kDirectKnobTop); assert(onceAsk(once).find("has no range yet") != std::string::npos);
    gLabKnobRange = lab::AnalogRange{}; gLabKnobRange.percent(3000); gLabKnobRange.percent(1400); assert(onceAsk(once).find("has no range yet") != std::string::npos);
    // The hub goes in the middle: the reading it decided on stays, and the strip isn't kept to come back unasked.
    gLabKnobRange.top = 3000; gLabKnobRange.low = 10; gReplies.clear(); labOnceStart(&once);
    for (int t = 0; t < 4000 && gLabOncePhase != LabOncePhase::Acting; t += 10) run(10); assert(gLabOncePhase == LabOncePhase::Acting);
    gHubUnit.here = false; run(400); assert(gReplies.size() == 1 && gReplies[0] == "Knob 70%, over 50. LED strip stopped answering."); assert(!gLabDrive[int(Out::StripColor)].on);
    gHubUnit.here = true; gLabKnobRange = lab::AnalogRange{}; }
  // A servo moves and lets go, and a beep ends by itself.
  { onceOnGrove(lab::Part::Servo); const lab::Once once = onceDo(Out::Servo, 90); gReplies.clear(); labOnceStart(&once); for (int t = 0; t < 3000 && gGrove.duty != lab::servoDuty(90); t += 10) run(10); assert(gGrove.duty == lab::servoDuty(90)); run(500); assert(gGrove.duty == lab::servoDuty(90) && labOnceBusy());
    run(1500); assert(!labOnceBusy() && gGrove.duty == 0 && gGrove.attached == -1 && !gLabDrive[int(Out::Servo)].on && !gLabServoHeld); assert(gReplies.back() == "Servo moved to 90 deg.");
    labOnceStart(&once); for (int t = 0; t < 3000 && gGrove.duty != lab::servoDuty(90); t += 10) run(10); assert(gGrove.duty == lab::servoDuty(90)); run(1500); assert(!labOnceBusy()); }
  { onceOnGrove(); gToneAllowed = false; assert(onceAsk(onceDo(Out::Tone, 880)) == "The speaker was busy, so it didn't beep."); gToneAllowed = true; }
  { onceOnGrove(); M5Cardputer.Speaker = SpeakerClass{}; assert(onceAsk(onceDo(Out::Tone, 880)) == "Beeped at 880 Hz."); assert(M5Cardputer.Speaker.tones > 0 && M5Cardputer.Speaker.hz == 880 && M5Cardputer.Speaker.stops > 0 && !gLabDrive[int(Out::Tone)].on && !gLabToneOn); }
  // The relays read the bus as a switch, so with them on Grove the port stays shut.
  { onceOnGrove(); gLabBook.direct = lab::Direct{}; gLabBook.direct.claim(lab::Part::Relay); const int opens = gAirSensor.opens; const lab::Once once = onceOver(50, Out::StripColor, 0);
    assert(onceAsk(once) == "The relays are on the Grove port, so I left it alone. Nothing changed."); assert(gAirSensor.opens == opens);
    assert(onceAsk(onceRead(Sig::Tilt)) == "Tilt 30 deg."); assert(gAirSensor.opens == opens); }
  // Another app holds the port, or takes it while this is running, which also stops at once.
  { onceOnGrove(); gUiMode = UiMode::AirMusic; const lab::Once once = onceOver(50, Out::StripColor, 0); const int opens = gAirSensor.opens;
    assert(onceAsk(once) == "Another app is using the Grove port. Nothing changed."); assert(gAirSensor.opens == opens); gUiMode = UiMode::Home;
    labOnceStart(&once); run(300); assert(gBusOpen); gReplies.clear(); setUiMode(UiMode::AirMusic); assert(!gBusOpen); run(50); assert(gReplies.size() == 1 && gReplies[0] == "That screen needed the parts, so the check stopped."); gUiMode = UiMode::Home; }
  // The mic and the speaker come first, and the report never talks over the reply before it.
  { onceOnGrove(); const lab::Once once = onceOver(50, Out::StripColor, 0); const int opens = gAirSensor.opens;
    gQueuedVoiceReplySpeech = "Checking the knob."; gReplies.clear(); labOnceStart(&once); run(2000); assert(gAirSensor.opens == opens);
    gQueuedVoiceReplySpeech = ""; gPlaybackActive = true; run(2000); assert(gAirSensor.opens == opens); gPlaybackActive = false;
    run(4500); assert(gReplies.size() == 1 && gReplies[0] == "Knob 75%, over 50. Strip set to red.");
    gQueuedVoiceReplySpeech = "Long"; gReplies.clear(); labOnceStart(&once); run(31000); assert(gReplies.empty() && labOnceBusy()); gQueuedVoiceReplySpeech = ""; run(50); assert(gReplies[0] == "The Cardputer stayed busy, so I didn't check. Nothing changed.");
    // Speech that never drains can't hold a report back for good.
    gQueuedVoiceReplySpeech = "Stuck"; gReplies.clear(); labOnceStart(nullptr); run(29000); assert(gReplies.empty()); run(1100); assert(gReplies.size() == 1 && !labOnceBusy()); gQueuedVoiceReplySpeech = ""; }
  // One at a time, and one that can't be run says so.
  { onceOnGrove(); const lab::Once first = onceOver(50, Out::StripColor, 0), second = onceOver(50, Out::StripColor, 240); gReplies.clear(); labOnceStart(&first); run(100); gQueuedVoiceReplySpeech = ""; labOnceStart(&second); assert(gReplies.size() == 1 && gReplies[0] == "Still on the last check, so I didn't start that one." && !gRepliesSpoken[0]); run(4500); assert(gReplies.size() == 2 && gReplies[1] == "Knob 75%, over 50. Strip set to red.");
    gReplies.clear(); labOnceStart(nullptr); run(50); assert(gReplies.size() == 1 && gReplies[0] == "That check wouldn't work. Nothing changed."); }
  // A setup it couldn't read might have relays on Grove, so the port stays shut, but the board still works.
  { onceOnGrove(); gLabBookKnown = false; const int opens = gAirSensor.opens; const lab::Once once = onceOver(50, Out::StripColor, 0);
    assert(onceAsk(once) == "The Lab's setup couldn't be read from the SD card, so I left the Grove port alone. Nothing changed."); assert(gAirSensor.opens == opens);
    assert(onceAsk(onceRead(Sig::Tilt)) == "Tilt 30 deg."); }
  // A card with no Lab files on it has nothing set up, so that's known. With no card it isn't.
  { setUiMode(UiMode::Home); gLabBookRead = false; gDisk.files.clear(); gSdReady = true; labReadBook(); assert(gLabBookKnown);
    gLabBookRead = false; gSdReady = false; labReadBook(); assert(!gLabBookKnown); labSaveBook(); assert(!gLabBookKnown); gSdReady = true;
    // A card that stopped answering says no files too, and that isn't the same as none.
    gLabBookRead = false; gDisk.gone = true; labReadBook(); assert(!gLabBookKnown); gDisk.gone = false; gLabBookRead = false; labReadBook(); assert(gLabBookKnown); }
  // A live voice session owns the loop, so a check there says so, and only once the session is over.
  { onceOnGrove(); gRealtimeExclusiveMode = true; const lab::Once once = onceOver(50, Out::StripColor, 0); const int opens = gAirSensor.opens;
    labOnceStart(&once); run(60000); assert(gReplies.empty() && gAirSensor.opens == opens); gRealtimeExclusiveMode = false; run(50);
    assert(gReplies.size() == 1 && gReplies[0] == "A check can't run during a live voice session, so ask in the chat. Nothing changed."); }
  // Something that takes the loop after it was asked: it waits, and a late check never acts.
  { onceOnGrove(); const lab::Once once = onceOver(50, Out::StripColor, 0); const int opens = gAirSensor.opens; gReplies.clear(); labOnceStart(&once);
    gRadioBusy = true; run(45000); assert(gAirSensor.opens == opens); gRadioBusy = false; run(50); assert(gReplies.size() == 1 && gReplies[0] == "The Cardputer stayed busy, so I didn't check. Nothing changed."); assert(gGrove.ledsLit == 0); }
  // In the middle of a look, playback gives the port straight back, and the look starts again after.
  { onceOnGrove(); const lab::Once once = onceOver(50, Out::StripColor, 0); gReplies.clear(); labOnceStart(&once); run(300); assert(gBusOpen);
    gPlaybackActive = true; run(20); assert(!gBusOpen && gLabOncePhase == LabOncePhase::Waiting); run(2000); assert(!gBusOpen);
    gPlaybackActive = false; run(4500); assert(gReplies.size() == 1 && gReplies[0] == "Knob 75%, over 50. Strip set to red."); }
  // A look cut short in its first second still puts a lit strip back before the port goes, even if it
  // then never gets to run.
  { onceOnGrove(); assert(onceAsk(onceOver(50, Out::StripColor, 0)) == "Knob 75%, over 50. Strip set to red.");
    const lab::Once once = onceOver(50, Out::StripColor, 240); gReplies.clear(); labOnceStart(&once); run(100); assert(gBusOpen && gGrove.scrambled && gLabQuietProbes < lab::kUnitCount);
    gPlaybackActive = true; run(20); assert(!gBusOpen && !gGrove.scrambled && gGrove.ledsLit == 30 && gGrove.lastRed > 0 && gGrove.mode[2] == INPUT_PULLDOWN);
    run(31000); gPlaybackActive = false; run(50); assert(gReplies.size() == 1 && gReplies[0] == "The Cardputer stayed busy, so I didn't check. Nothing changed." && gGrove.lastRed > 0 && !gGrove.scrambled); }
  // Readings are whole numbers, so thresholds keep their side.
  { onceOnGrove(); lab::Once once = onceOver(74, Out::Led, 120); assert(onceAsk(once) == "Knob 75%, over 74. LED set to green."); }
  // A reply that was spoken has a spoken report.
  { onceOnGrove(); const lab::Once tilt = onceRead(Sig::Tilt); gActionsFollowSpeech = true; labOnceStart(&tilt); gActionsFollowSpeech = false; run(1000); assert(gReplies.size() == 1 && gRepliesSpoken[0]); }
  // A check that needs no port leaves another app's bus alone.
  { onceOnGrove(); gUiMode = UiMode::AirMusic; gAirSensor.open(); const int closes = gAirSensor.closes;
    const lab::Once led = onceDo(Out::Led, 0); gReplies.clear(); labOnceStart(&led); run(1000);
    assert(gReplies.size() == 1 && gReplies[0] == "LED set to red." && gAirSensor.open_ && gAirSensor.closes == closes); gAirSensor.close(); gUiMode = UiMode::Home; }
  // Opening the Lab takes back what was left on.
  { onceOnGrove(); assert(onceAsk(onceDo(Out::Led, 240)) == "LED set to blue."); assert(gLed.b > 0); const lab::Once red = onceOver(50, Out::StripColor, 0); onceAsk(red); assert(gGrove.ledsLit == 30);
    setUiMode(UiMode::LabDevices); run(1500); assert(gLed.r == 0 && gLed.g == 0 && gLed.b == 0 && gGrove.ledsLit == 0 && !gLabDrive[int(Out::StripColor)].on); setUiMode(UiMode::Home); run(100); }
  // Leaving it mid-check gives everything back before the Lab starts.
  { onceOnGrove(); const lab::Once once = onceOver(50, Out::StripColor, 0); gReplies.clear(); labOnceStart(&once); run(300); setUiMode(UiMode::LabDevices); run(100); assert(gLabOpen && !labOnceBusy() && gReplies.empty() && gLabUi.view == LabView::Say && gLabUi.sayNote == "That screen needed the parts, so the check stopped."); gQueuedVoiceReplySpeech = ""; esc(); setUiMode(UiMode::Home); run(100); }
  // With the Lab open it uses the Lab's own readings and parts, and answers there. Practice isn't real.
  { onceOnGrove(); setUiMode(UiMode::LabDevices); run(2000); assert(gLabOnWires && gLabInputs.get(Sig::Knob).value == 75);
    gLabPractice.nudge(Sig::Knob, -1, gLabInputs, nowMs); const int opens = gAirSensor.opens;
    const lab::Once once = onceOver(50, Out::StripColor, 0); gActionOnce = &once; gReplies.clear();
    VoiceToolAnswer answer; answer.ok = true; answer.reply = "Checking the knob."; answer.actionsJson = "[x]"; gLabUi.view = LabView::Devices;
    labAskAnswered(answer); gActionOnce = nullptr; assert(gLabUi.view == LabView::Say && gLabUi.sayNote == "Checking the knob." && labOnceBusy() && gQueuedVoiceReplySpeech == "Checking the knob.");
    gQueuedVoiceReplySpeech = ""; run(1000); assert(!labOnceBusy() && gLabUi.sayNote == "Knob 75%, over 50. Strip set to red." && gQueuedVoiceReplySpeech == "Knob 75%, over 50. Strip set to red." && gReplies.empty());
    show("doc-once-answer"); assert(gCanvas.shows("Strip set to red"));
    assert(gLabOnWires && gGrove.ledsLit == 30 && gAirSensor.opens == opens); gQueuedVoiceReplySpeech = ""; esc(); assert(gLabUi.view == LabView::Devices);
    gLabPractice.clear(); setUiMode(UiMode::Home); run(100); assert(gGrove.ledsLit == 0); }  // Left on for as long as the Lab is open.
  // In the Lab: rules that are playing keep their parts, the menu over it means wait, and a strip test
  // pattern gives way to the color that was asked for.
  { onceOnGrove(); gLabBook.count = 1; gLabBook.rules[0] = lab::recipeRule(5); setUiMode(UiMode::LabRules); run(2000); gLabUi.ruleRow = 0; keys("p"); run(300); assert(gLabPlaying);
    { const lab::Once blue = onceOver(50, Out::StripColor, 240); gReplies.clear(); labOnceStart(&blue); run(200);
      assert(!labOnceBusy() && gReplies.empty() && gLabUi.sayNote == "A rule or a sequence has the parts in the Lab, so I left them alone. Nothing changed." && gLabPlaying); }
    gQueuedVoiceReplySpeech = ""; setUiMode(UiMode::LabDevices); run(2000); assert(!gLabPlaying);
    gLauncherVisible = true; const lab::Once once = onceOver(50, Out::StripColor, 240); gReplies.clear(); labOnceStart(&once); run(2000); assert(labOnceBusy() && gLabOncePhase == LabOncePhase::Waiting);
    gLauncherVisible = false; run(1000); assert(!labOnceBusy() && gLabUi.sayNote == "Knob 75%, over 50. Strip set to blue."); gQueuedVoiceReplySpeech = ""; esc();
    gLabStripTest = LabStripTest::Rainbow; labOnceStart(&once); run(1000); assert(gLabStripTest == LabStripTest::Solid && gLabUi.stripHue == 240); gQueuedVoiceReplySpeech = ""; esc();
    setUiMode(UiMode::Home); run(100); }
  // A sequence's screen takes the outputs even once it's finished, so a check there doesn't act. And
  // if something takes an output back before it goes out, the report says so.
  { onceOnGrove(); setUiMode(UiMode::LabSequences); run(2000); gLabUi.view = LabView::SeqPlay; assert(labSeqOnScreen() && !gLabSeqRun.running());
    const lab::Once red = onceOver(50, Out::StripColor, 0); labOnceStart(&red); run(300);
    assert(!labOnceBusy() && gLabUi.sayNote == "A rule or a sequence has the parts in the Lab, so I left them alone. Nothing changed."); gQueuedVoiceReplySpeech = "";
    setUiMode(UiMode::LabDevices); run(2000); labOnceStart(&red);
    for (int t = 0; t < 3000 && gLabOncePhase != LabOncePhase::Acting; t += 10) { nowMs += 10; serviceLabOnce(); if (gLabOncePhase != LabOncePhase::Acting) serviceLab(); }
    assert(gLabOncePhase == LabOncePhase::Acting); labDriveClear(); for (int t = 0; t < 400; t += 10) { nowMs += 10; serviceLabOnce(); labDriveClear(); serviceLab(); }
    assert(!labOnceBusy() && gLabUi.sayNote == "Knob 75%, over 50. LED strip didn't change, because something else had it."); gQueuedVoiceReplySpeech = ""; esc(); setUiMode(UiMode::Home); run(100); }
  onceOnGrove(); gLabBook = lab::Book{}; gDisk.files.clear(); plugOnly(); freshBoot();
}

// What the owner asked for: "play Happy birthday" in the chat plays the saved one.
void testPlayFromChat() {
  setUiMode(UiMode::Home); run(100); gLabBook = lab::Book{}; gLabBookRead = gLabBookKnown = true; gDisk.files.clear(); plugOnly(); gReplies.clear();
  gQueuedVoiceReplySpeech = ""; gPlaybackActive = gRecording = gRadioBusy = gRealtimeExclusiveMode = false;
  lab::Sequence song = lab::starterSequence(); std::strcpy(song.name, "Happy birthday");
  lab::Sequence fan; std::strcpy(fan.name, "Fan"); fan.count = 2; fan.steps[0] = {lab::SeqKind::Set, Out::Relay1, 1}; fan.steps[1] = {lab::SeqKind::Wait, Out::Servo, 50};
  gLabSeqs[0] = song; gLabSeqs[1] = fan; gLabSeqCount = 2;
  assert(!lab::switchesMains(song) && lab::switchesMains(fan));
  // It waits for the reply to finish speaking, then plays on its own screen, where Esc stops it.
  gQueuedVoiceReplySpeech = "Playing it."; assert(labPlayAsked("happy BIRTHDAY")); run(500); assert(gUiMode == UiMode::Home && !gLabSeqRun.running());
  gQueuedVoiceReplySpeech = ""; run(20);
  assert(gUiMode == UiMode::LabSequences && gLabUi.view == LabView::SeqPlay && gLabSeqRun.running() && gLabUi.seqShown == 0 && gLabUi.seqRow == 0);
  run(100); assert(gLed.b > 0 || gLed.g > 0 || gLed.r > 0); show("doc-play-from-chat"); assert(gCanvas.shows("Happy birthday") && gCanvas.shows("ESC stop"));
  esc(); assert(!gLabSeqRun.running() && gLabUi.view == LabView::Seqs);
  // One that switches mains opens ready, and only the owner's P plays it.
  setUiMode(UiMode::Home); run(100); assert(labPlayAsked("Fan")); run(20);
  assert(gUiMode == UiMode::LabSequences && gLabUi.view == LabView::Seqs && gLabUi.seqRow == 1 && !gLabSeqRun.running() && gStatus == "P plays it: it switches mains");
  keys("p"); assert(gLabSeqRun.running() && gLabUi.seqShown == 1); esc();
  // A name that isn't saved does nothing, and one deleted before it plays does nothing either.
  setUiMode(UiMode::Home); run(100); assert(!labPlayAsked("Nope") && gStatus == "No saved sequence by that name"); run(100); assert(gUiMode == UiMode::Home);
  gQueuedVoiceReplySpeech = "Playing it."; assert(labPlayAsked("Fan")); gLabSeqCount = 1; gQueuedVoiceReplySpeech = ""; run(100); assert(gUiMode == UiMode::Home); gLabSeqCount = 2;
  // Something that keeps the loop gives up after a while, and says so.
  gRadioBusy = true; assert(labPlayAsked("Happy birthday")); run(29000); assert(gUiMode == UiMode::Home && gReplies.empty());
  run(1100); assert(gUiMode == UiMode::Home && gReplies.size() == 1 && gReplies[0] == "The Cardputer stayed busy, so the sequence didn't play."); gRadioBusy = false; run(100); assert(gUiMode == UiMode::Home);
  // Rules playing in the Lab give way, the same as pressing P on the list.
  gLabBook.count = 1; gLabBook.rules[0] = lab::recipeRule(0); setUiMode(UiMode::LabRules); run(500); gLabUi.ruleRow = 0; keys("p"); run(200); assert(gLabPlaying);
  assert(labPlayAsked("Happy birthday")); run(20); assert(!gLabPlaying && gUiMode == UiMode::LabSequences && gLabSeqRun.running()); esc();
  setUiMode(UiMode::Home); gLabSeqCount = 0; gLabBook = lab::Book{}; gDisk.files.clear(); freshBoot();
}

// The Cardputer LED runs off the screen light, which only holds steady at full brightness.
void testLedPower() {
  setUiMode(UiMode::LabDevices); run(100); gLed = {}; assert(gBacklightPct == 70 && !gLedWantsLight);
  labDriveSet(Out::Led, 120); run(10); assert(gBacklightPct == 100 && gLed.g == 0);  // Its power settles first.
  run(10); assert(gLed.g > 0);
  // It goes dark when the screen does, and gets its color back when the screen wakes.
  gFakeScreenOff = true; applyDisplayBrightness(); gLed = {}; run(300); assert(gLed.g == 0 && gLabLedSent == -1 && gLedWantsLight);
  gFakeScreenOff = false; applyDisplayBrightness(); run(100); assert(gLed.g > 0 && gBacklightPct == 100);
  // Turned off, it's sent dark before the screen dims, so it can't keep a stale color.
  labDriveOff(Out::Led); run(10); assert(gLed.g == 0 && gBacklightPct == 100); run(60); assert(gBacklightPct == 70 && !gLedWantsLight);
  // Leaving the Lab with it lit puts the screen back too.
  labDriveSet(Out::Led, 240); run(100); assert(gLed.b > 0 && gBacklightPct == 100);
  setUiMode(UiMode::Home); assert(gLed.b == 0 && gBacklightPct == 70 && !gLedWantsLight);
}

// The joystick can be held any way round, so the Lab learns which way is right and which is up.
void testStickLearn() {
  const auto plugged = gUnits.present; const bool hub = gHubUnit.here;
  gUnits.present = {0x57, 0x44, 0x63, 0x50}; gHubUnit.here = true; gUnits.stickX = 0; gUnits.stickY = 0;
  setUiMode(UiMode::LabDevices); run(1500); gLabUi.devRow = 0;
  pickDevice(Dev::Joystick); renderLabUi(); assert(gCanvas.shows("D set directions") && gLabBook.stick.bits() == 0);
  key('d'); run(100); show("stick-learn-right"); assert(gLabUi.stickLearn == 1 && gCanvas.shows("STEP 1 OF 2") && gCanvas.shows("right"));
  // Arrows and N don't turn the page while it listens, and a small push doesn't count.
  key(RIGHT); key('n'); assert(gLabUi.page == 0 && gLabUi.stickLearn == 1);
  gUnits.stickX = 30; run(1000); assert(gLabUi.stickLearn == 1);
  // This one is held a quarter turn round and upside down: right reads as Y going down, up as X going up.
  gUnits.stickX = 4; gUnits.stickY = -110; run(250); show("stick-learn-holding"); assert(gLabUi.stickLearn == 1 && gCanvas.shows("Push it"));
  run(400); assert(gLabUi.stickLearn == 2);
  run(800); assert(gLabUi.stickLearn == 2);  // Still pushed right, which can't be up too.
  gUnits.stickX = 0; gUnits.stickY = 0; run(200); show("stick-learn-up"); assert(gCanvas.shows("STEP 2 OF 2") && gCanvas.shows("up"));
  gUnits.stickX = 105; gUnits.stickY = 8; run(700); assert(gLabUi.stickLearn == 0 && gLabBook.stick.swap && gLabBook.stick.flipX && !gLabBook.stick.flipY);
  // Now right is right and up is up, everywhere the stick is read.
  gUnits.stickX = 0; gUnits.stickY = -127; run(100); assert(gLabInputs.get(Sig::StickX).value == 100 && gLabInputs.get(Sig::StickY).value == 0);
  gUnits.stickX = 127; gUnits.stickY = 0; run(100); assert(gLabInputs.get(Sig::StickY).value == 100 && gLabInputs.get(Sig::StickX).value == 0);
  renderLabUi(); assert(gCanvas.shows("Move it"));
  // Esc while it listens stops it, keeps what was there, and stays on the page.
  key('d'); run(100); gUnits.stickX = -110; gUnits.stickY = 0; run(700); assert(gLabUi.stickLearn == 2);
  esc(); assert(gLabUi.stickLearn == 0 && gLabUi.view == LabView::Device && gLabBook.stick.swap && gLabBook.stick.flipX);
  // It's saved with the rest of the Lab.
  esc(); freshBoot(); gLabBook = lab::Book{}; setUiMode(UiMode::LabDevices); run(1500); assert(gLabBook.stick.swap && gLabBook.stick.flipX && !gLabBook.stick.flipY);
  // Leaving the screen mid-way stops it too.
  gLabUi.devRow = 0; pickDevice(Dev::Joystick); key('d'); run(100); assert(gLabUi.stickLearn == 1); esc(); esc(); assert(gLabUi.view == LabView::Devices && !gLabUi.stickLearn);
  pickDevice(Dev::Joystick); key('d'); run(100); assert(gLabUi.stickLearn == 1); setUiMode(UiMode::Home); setUiMode(UiMode::LabDevices); run(100); assert(!gLabUi.stickLearn);
  // If the joystick goes, so does the question.
  pickDevice(Dev::Joystick); key('d'); run(100); gUnits.present.erase(0x63); run(2000); assert(!gLabUi.stickLearn); gUnits.present.insert(0x63); run(2000); esc();
  gLabBook.stick = lab::StickTurn{}; labSaveBook(); gUnits.stickX = 47; gUnits.stickY = -15; setUiMode(UiMode::Home);
  gUnits.present = plugged; gHubUnit.here = hub;
}

// The GPS's readings beyond satellites. They run last, so the time they take moves no other scene.
void testGpsReadings() {
  setUiMode(UiMode::LabDevices); run(1500); gLabUi.devRow = 0; pickDevice(Dev::Gps);
  gpsLine("GNGGA,123519,4230.5000,N,07054.0000,W,1,08,0.9,180.0,M,0,M,,");
  gpsLine("GNRMC,123519,A,4230.5000,N,07054.0000,W,10.0,084.4,230394,,,A"); run(30); renderLabUi();
  // Moving, so a heading; no spot yet, so From spot has no reading rather than a made-up one.
  assert(gCanvas.shows("84` E") && gCanvas.shows("not saved") && gCanvas.shows("S save spot  M Mesh  ESC back"));
  assert(gLabInputs.get(Sig::Speed).value == 19 && gLabInputs.get(Sig::Heading).value == 84 && !gLabInputs.get(Sig::SpotDistance).live);
  // S saves where it is now, on the device, and the distance starts at nothing.
  key('s'); show("gps-spot-saved"); assert(gCanvas.shows("Spot saved") && gNvs["labspot"]["set"] == 1);
  assert(gNvs["labspot"]["lat"] == 4250833 && gNvs["labspot"]["lon"] == -7090000);
  run(30); assert(gLabInputs.get(Sig::SpotDistance).live && gLabInputs.get(Sig::SpotDistance).value == 0);
  // A thousandth of a degree north is 111 m.
  gpsLine("GNRMC,123520,A,4230.5600,N,07054.0000,W,10.0,000.0,230394,,,A"); run(2100); show("gps");
  assert(gLabInputs.get(Sig::SpotDistance).value == 111 && gCanvas.shows("111 m") && gCanvas.shows("0` N"));
  // Standing still, a GPS still names a direction, and it wanders, so there's no heading.
  gpsLine("GNRMC,123521,A,4230.5600,N,07054.0000,W,0.5,211.0,230394,,,A"); run(30); renderLabUi();
  assert(!gLabInputs.get(Sig::Heading).live && gLabInputs.get(Sig::Speed).value == 1 && gCanvas.shows("still"));
  gpsLine("GNRMC,123521,A,4230.5600,N,07054.0000,W,20.0,,230394,,,A"); run(30); assert(!gLabInputs.get(Sig::Heading).live);
  // Far away reads as the most a reading can hold.
  gpsLine("GNRMC,123522,A,4300.0000,N,07054.0000,W,0.0,,230394,,,A"); run(30); renderLabUi();
  assert(gLabInputs.get(Sig::SpotDistance).value == lab_gps::kFarMeters && gCanvas.shows("32+ km"));
  gpsLine("GNRMC,123522,A,4231.5000,N,07054.0000,W,0.0,,230394,,,A"); run(30); renderLabUi(); assert(gCanvas.shows("1.8 km"));
  // The saved spot comes back after a restart.
  gLabSpot = LabSpot{}; gLabSpotLoaded = false; run(30); assert(gLabSpot.set && gLabSpot.latE5 == 4250833);
  // Losing the fix loses every reading that needs one, and S says why it can't save.
  gpsLine("GNRMC,123523,V,,,,,,,230394,,,N"); run(30);
  assert(gLabInputs.get(Sig::Satellites).live && !gLabInputs.get(Sig::Speed).live && !gLabInputs.get(Sig::Heading).live && !gLabInputs.get(Sig::SpotDistance).live);
  key('s'); show("gps-no-fix"); assert(gCanvas.shows("No fix yet") && gNvs["labspot"]["lat"] == 4250833);
  run(2100); renderLabUi(); assert(gCanvas.shows("First fix can take minutes outdoors"));
  gpsLine("GNRMC,123524,A,4230.5000,N,07054.0000,W,0.0,,230394,,,A"); run(30);
  gNvsWritable = false; key('s'); renderLabUi(); assert(gCanvas.shows("Couldn't save") && gLabSpot.latE5 == 4250833); gNvsWritable = true;
  // The guide's picture: moving north, 111 m from the spot.
  gpsLine("GNRMC,123525,A,4230.5000,N,07054.0000,W,10.0,000.0,230394,,,A"); key('s'); run(2100);
  gpsLine("GNGGA,123526,4230.5600,N,07054.0000,W,1,08,0.9,180.0,M,0,M,,");
  gpsLine("GNRMC,123526,A,4230.5600,N,07054.0000,W,10.0,000.0,230394,,,A"); run(30); show("doc-gps");
  assert(gCanvas.shows("111 m") && gCanvas.shows("0` N") && gCanvas.shows("18.5 km/h"));
  esc();
  // Speed from another screen opens the GPS for it, and like tilt leaves the port alone.
  { onceOnGrove(); const int opens = gAirSensor.opens, bus = gUnits.commands; Serial1.open = false;
    gpsLine("GNRMC,123519,A,4230.5000,N,07054.0000,W,10.0,084.4,230394,,,A");
    assert(onceAsk(onceRead(Sig::Speed)) == "Speed 19 km/h."); assert(gAirSensor.opens == opens && gUnits.commands == bus && gGrove.frames == 0);
    assert(!Serial1.open); }
}
int main(int argc, char** argv) {
  if (argc > 1) gDump = std::fopen(argv[1], "w");
  for (bool& live : gLabHistoryLive) live = false;
  testDevices(); testPages(); testMissing(); testWiring(); testBuildRule(); testEditRule(); testPlay(); testPractice(); testRecipes(); testSay(); testRemotes(); testSaving(); testGrove(); testAnySetup(); testSonic(); testGuidePictures(); testSequences(); testChat(); testOnce(); testPlayFromChat(); setUiMode(UiMode::LabRules); run(1500); testGuards(); testLedPower(); testStickLearn(); testGpsReadings();
  if (gDump) std::fclose(gDump);
  std::puts("PASS Lab screens: every device test, the wiring screen, building and changing a rule by steps, playing it through a simulated hub, practice with nothing plugged in, recipes, a rule said aloud, sequences on their own and from a rule, what the chat can see and offer, one check from the chat, a sequence played from the chat, the lamp and AC guards, units leaving and returning, saving and damage, the LED waiting for full screen light, learning which way the joystick is held, GPS speed, heading and distance from a saved spot, and every screen stays on the display");
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
