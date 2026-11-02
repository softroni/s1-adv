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
  assert(size == 720 && gRmtLive == 1); ++gGrove.frames; gGrove.ledsLit = 0;
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
  void open() { if (!open_) ++opens; open_ = true; gBusOpen = true; }
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
struct ImuClass { float ay = 0.5f; bool getAccel(float* x, float* y, float* z) { *x = 0; *y = ay; *z = 1; return true; } };
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
int gSpeechFetches = 0, gSpeechPlays = 0; bool gSpeechWorks = true; std::string gSpeechPath, gSpeechText;
bool speechToWavFile(const String& text, const String& path) { ++gSpeechFetches; gSpeechText = text; if (!gSpeechWorks) return false; gDisk.files[std::string(path)] = {1, 2}; return true; }
void render() {}
struct Keyboard_Class { struct KeysState { bool ctrl = false, alt = false, opt = false, enter = false, del = false; }; };
enum class UiMode { Home, NfcNotes, Mesh, SmartLamp, IrRemote, LabDevices, LabRules };
UiMode gUiMode = UiMode::Home;
bool gLauncherVisible = false, gRecording = false, gRealtimeActive = false, gPlaybackActive = false, gToneAllowed = true;
bool gSubmitting = false, gThinking = false, gWifiReady = true;
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
String cleanReplyForDevice(const String& text) { return text; }
bool gActionsRun = false;
const void* gActionRule = nullptr;  // Offered the way the real device action does.
int gActionRepeats = 1;
void offerFromAction(const void* rule);
bool executeDeviceActionsJson(const String& json) { if (gActionRule) for (int i = 0; i < gActionRepeats; ++i) offerFromAction(static_cast<const char*>(gActionRule) + 0); return !std::string(json).empty() && gActionsRun; }
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
bool labMode(UiMode mode) { return mode == UiMode::LabDevices || mode == UiMode::LabRules; }
namespace { void beginLab(); void stopLab(); }
void setUiMode(UiMode mode) { if (labMode(gUiMode) && mode != gUiMode) stopLab(); gUiMode = mode; if (labMode(mode)) beginLab(); }
''' + (parts / '081_main.cpp.inc').read_text() + (parts / '082_main.cpp.inc').read_text() + r'''
// --- Driving it ----------------------------------------------------------------------------------
FILE* gDump = nullptr;
lab::Rule gOfferSteps[4]; int gOfferStepAt = -1;
void offerFromAction(const void* rule) { labOfferRule(gOfferStepAt >= 0 ? gOfferSteps[gOfferStepAt++] : *static_cast<const lab::Rule*>(rule)); }
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
  assert(gCanvas.shows("112") && gCanvas.shows("Watch it move"));
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
  pickDevice(Dev::Led); keys(" "); assert(gLabUi.ledOn && gLed.g > 0); show("led"); keys("/"); esc(); assert(gLed.r == 0 && gLed.g == 0 && gLed.b == 0);
  pickDevice(Dev::Speaker); keys("/"); assert(M5Cardputer.Speaker.tones > 0 && M5Cardputer.Speaker.hz == 930); show("speaker");
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
  goToRow(count + 2); show("rules-recipes"); assert(gCanvas.shows("10 ready to go"));
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
    labWizardStart(count, nullptr); gLabWizard.step = LabStep::Out; assert(labWizardRowCount() == lab::kOutCount); assert(std::string(labWizardRow(lab::kOutCount - 1)) == "Say");
    gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
    labWizardStart(-1, nullptr); gLabWizard.step = LabStep::Out; assert(labWizardRowCount() == lab::kOutCount - 1); gLabWizard = LabWizard{}; gLabUi.view = LabView::Rules;
    labWizardStart(count, nullptr); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER);
    assert(gLabUi.view == LabView::Rules && !std::strcmp(gLabBook.rules[count].say, "Hello there") && lab::hasSay(gLabBook.rules[count]) && gLabBook.rules[count].actions == 2);
    // Taking Say off a rule drops its phrase.
    lab::Rule plain = gLabBook.rules[count]; plain.action[0] = plain.action[1]; plain.actions = 1; labWizardStart(count, &plain); for (int i = 0; i < 40 && gLabUi.view == LabView::Wizard; ++i) key(ENTER); assert(gLabBook.rules[count].say[0] == 0);
    gLabUi.ruleRow = count; key('x'); key('x'); assert(gLabBook.count == count);
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

int main(int argc, char** argv) {
  if (argc > 1) gDump = std::fopen(argv[1], "w");
  for (bool& live : gLabHistoryLive) live = false;
  testDevices(); testMissing(); testWiring(); testBuildRule(); testEditRule(); testPlay(); testPractice(); testRecipes(); testSay(); testRemotes(); testSaving(); testGrove(); testAnySetup(); testSonic(); setUiMode(UiMode::LabRules); run(1500); testGuards();
  if (gDump) std::fclose(gDump);
  std::puts("PASS Lab screens: every device test, the wiring screen, building and changing a rule by steps, playing it through a simulated hub, practice with nothing plugged in, recipes, a rule said aloud, the lamp and AC guards, units leaving and returning, saving and damage, and every screen stays on the display");
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
