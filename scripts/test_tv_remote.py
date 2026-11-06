#!/usr/bin/env python3
"""TV Remote: the infrared frames byte for byte against the recorded Apple TV 4K and Samsung codes,
the Bluetooth keyboard's report map and characters, and the app as the device runs it with a fake
Bluetooth stack and infrared sender: every key, holding a key, Bluetooth on and off, pairing,
typing, forgetting, leaving, and every screen fitting. Real code under ASan/UBSan."""
from pathlib import Path
import os, re, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
app = (parts / '094_main.cpp.inc').read_text()
part = lambda name: (parts / name).read_text()

# Wired in like the other apps: menu, name, keys, Esc, the loop, background work, the saver.
for f, s in [('002_main.cpp.inc', '  Podcasts,\n  TvRemote,\n};'),
             ('004_main.cpp.inc', '{"Pocket Synth", UiMode::PocketSynth}, {"TV Remote", UiMode::TvRemote}}},'),
             ('004_main.cpp.inc', '{"Tools", "Local hardware tools", rgb565_local(255, 210, 64), 9,'),
             ('007_main.cpp.inc', 'case UiMode::TvRemote: return "TV Remote";'),
             ('022_main.cpp.inc', 'out = UiMode::TvRemote;'),
             ('043_main.cpp.inc', 'setUiMode(UiMode::TvRemote);'),
             ('047_main.cpp.inc', 'gUiMode == UiMode::TvRemote && !gLauncherVisible && tvRemoteEscape()'),
             ('049_main.cpp.inc', 'gUiMode == UiMode::TvRemote && (upDown || downDown || leftDown || rightDown)'),
             ('052_main.cpp.inc', 'case UiMode::TvRemote: return kTv;'),
             ('061_main.cpp.inc', 'renderTvRemoteUi();'),
             ('063_main.cpp.inc', 'case UiMode::TvRemote: return false;'),
             ('025_main.cpp.inc', 'if (wifiSettingsTakesAllKeys() || tvRemoteTakesText()) {')]:
    assert s in part(f), (f, s)
assert 'constexpr uint8_t kTv[32]' in (src / 'launcher_icons.h').read_text()
for f, head in [('043_main.cpp.inc', 'void maintainConnectivity() {'), ('040_main.cpp.inc', 'bool ensureWifiForHttp(const char* reason, uint32_t timeoutMs) {')]:
    body = part(f).split(head, 1)[1]
    assert body.index('if (tvRemoteHoldsWifi()) return') < body.index('WiFi.'), (f, 'Bluetooth has Wi-Fi off: no reconnect, no dial')
assert 'main_parts/094_main.cpp.inc' in (src / 'main.cpp').read_text()
mode = part('024_main.cpp.inc').split('void setUiMode(UiMode mode) {', 1)[1]
assert mode.index('if (modeChanged && gUiMode == UiMode::TvRemote) stopTvRemote();') < mode.index('  gUiMode = mode;') < \
       mode.index('if (modeChanged && mode == UiMode::TvRemote) beginTvRemote();'), 'Bluetooth stops before the next app starts'
loop = part('061_main.cpp.inc').split('void loop() {', 1)[1]
assert loop.index('serviceTvRemote();') < loop.index('if (radioBusy())'), 'it must run wherever the loop goes'
typing = part('048_main.cpp.inc').split('void handleTypingKey(', 1)[1]
# What's typed for the Apple TV can be a password: it's handled before the key diagnostics log it.
assert typing.index('if (tvRemoteTakesText() && !keys.tab && handleTvRemoteKey(key, keys)) return;') < typing.index('setKeyDiag(String("key=")')
# The remote's plain keys come before the shortcuts that use them elsewhere, but after Esc.
assert typing.index('if (key == \'`\'') < typing.index('if (gUiMode == UiMode::TvRemote && !gLauncherVisible && !keys.tab && handleTvRemoteKey(key, keys)) return;') \
       < typing.index('if (handleFocusShortcut(lowerKey,keys)) return;')
saver = part('009_main.cpp.inc')
assert '(radioBusy() || radioOn() || podcastOn() || gUiMode == UiMode::TvRemote)' in saver, 'the saver would leave the app and stop Bluetooth'
wake = saver.split('bool handleScreensaverWake() {', 1)[1].split('\n}\n', 1)[0]
assert wake.index('gUiMode == UiMode::TvRemote') < wake.index('setUiMode(target);'), "a dimmed remote's key goes through"
ini = (root / 'bots/CardputerADV/platformio.ini').read_text()
# Bluetooth is a build switch: the library is in the image exactly when the switch is on, because
# linking it costs every app 17.5 KB of heap, which broke HTTPS on 0.2.307.
bluetooth = '-D TV_REMOTE_BLUETOOTH=1' in ini
assert bluetooth != ('-D TV_REMOTE_BLUETOOTH=0' in ini)
assert (re.search(r'^\s+h2zero/NimBLE-Arduino@2\.5\.2\s*$', ini, re.M) is not None) == bluetooth
assert '-D CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED' in ini and 'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1' in ini
assert '#ifndef TV_REMOTE_BLUETOOTH\n#define TV_REMOTE_BLUETOOTH 0' in (src / 'tv_ble.h').read_text()
# The Wi-Fi library aborts if modem sleep is off while Bluetooth is enabled, and the firmware turns
# it off in several places, so every request goes through Bluetooth's guard.
ble = (src / 'tv_ble.cpp').read_text()
assert '-Wl,--wrap=esp_wifi_set_ps' in ini and 'extern "C" esp_err_t __wrap_esp_wifi_set_ps(wifi_ps_type_t type) {' in ble
assert 'if (type == WIFI_PS_NONE) type = WIFI_PS_MIN_MODEM;' in ble
assert ble.index('__wrap_esp_wifi_set_ps') < ble.index('#if TV_REMOTE_BLUETOOTH\nnamespace tv_ble'), 'the wrap links with or without Bluetooth'
started = ble.split('bool start(const char* name, int batteryPercent) {', 1)[1]
assert started.index('holdWifiSleep();') < started.index('NimBLEDevice::init(name)'), 'sleep before the controller starts'
stopped = ble.split('void stop() {', 1)[1]
assert stopped.index('NimBLEDevice::deinit(true);') < stopped.index('releaseWifiSleep();'), 'and back only once it has stopped'

# Every message fits the bottom line whole: Font0 is 6 px a letter, and the line is 224 px.
said = re.findall(r'tvSay\("([^"]*)"', app) + re.findall(r'gFailure = "([^"]*)"', (src / 'tv_ble.cpp').read_text())
assert len(said) >= 12, said
for line in said: assert len(line) * 6 <= 224, line
app = app.replace('#include "../tv_ble.h"\n', '').replace('#include "../tv_remote.h"\n', '')
code = r'''
#include "tv_remote.h"
#include "tv_ble.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
using std::min; using std::max;
struct String : std::string {
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  template <class T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, char>>>
  String(T n) : std::string(std::to_string(n)) {}
  bool isEmpty() const { return empty(); }
  void remove(size_t i) { if (i < size()) erase(i); }
  void remove(size_t i, size_t n) { if (i < size()) erase(i, n); }
};
namespace fonts { const int Font0 = 0, Font2 = 2, FreeSansBold9pt7b = 9; }
const uint16_t TFT_WHITE = 0xffff, TFT_BLACK = 0;
struct Box { int x, y, w, h; };
struct Canvas {
  const int* font = &fonts::Font0; int x = 0, y = 0; std::vector<Box> texts; std::vector<std::string> printed;
  void bounds(int a, int b, int w, int h) { assert(a >= 0 && b >= 0 && w > 0 && h > 0 && a + w <= 240 && b + h <= 135); }
  void fillScreen(uint16_t) { texts.clear(); printed.clear(); arcs = 0; }
  void fillRoundRect(int a, int b, int w, int h, int, uint16_t) { bounds(a, b, w, h); }
  void drawRoundRect(int a, int b, int w, int h, int, uint16_t) { bounds(a, b, w, h); }
  void drawCircle(int a, int b, int r, uint16_t) { assert(r > 0); bounds(a - r, b - r, 2 * r + 1, 2 * r + 1); }
  int arcs = 0;
  void fillArc(int a, int b, int r0, int r1, float from, float to, uint16_t c) { assert(0 < r0 && r0 < r1 && 0 <= from && from < to && to <= 360); drawCircle(a, b, r1, c); ++arcs; }
  void drawFastHLine(int a, int b, int w, uint16_t) { bounds(a, b, w, 1); }
  void fillCircle(int a, int b, int r, uint16_t c) { drawCircle(a, b, r, c); }
  void fillTriangle(int a, int b, int c, int d, int e, int f, uint16_t) {
    bounds(min({a, c, e}), min({b, d, f}), max({a, c, e}) - min({a, c, e}) + 1, max({b, d, f}) - min({b, d, f}) + 1); }
  void setFont(const int* f) { font = f; }
  void setTextColor(uint16_t, uint16_t = 0) {}
  void setCursor(int a, int b) { x = a; y = b; }
  // Roughly the real widths, a little wide so a screen that fits here fits there.
  int textWidth(const String& s) { return int(s.size()) * (*font == 9 ? 10 : *font == 2 ? 7 : 6); }
  int height() { return *font == 2 ? 16 : *font == 9 ? 14 : 8; }
  void print(char c) { print(String(1, c)); }
  void print(const String& s) {
    if (s.empty()) return;
    for (unsigned char c : s) assert(c >= 0x20 && c < 0x7f);  // These fonts have ASCII only.
    Box box{x, y, textWidth(s), height()}; bounds(box.x, box.y, box.w, box.h);
    for (const auto& t : texts) assert(x + box.w <= t.x || t.x + t.w <= x || y + box.h <= t.y || t.y + t.h <= y);
    texts.push_back(box); printed.push_back(s); x += box.w; }
} gCanvas;
bool shows(const std::string& s) { for (auto& t : gCanvas.printed) if (t == s) return true; return false; }
bool showsPart(const std::string& s) { for (auto& t : gCanvas.printed) if (t.find(s) != std::string::npos) return true; return false; }
uint16_t rgb565_local(int r, int g, int b) { return ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3); }
String fitCurrentFontToWidth(String s, int w) { while (!s.empty() && gCanvas.textWidth(s) > w) s.pop_back(); return s; }
[[maybe_unused]] constexpr int16_t kBigLeft = 8, kBigRight = 229;
void drawBigHeader(const String& t, uint16_t, const String& = String(), const String& = String(), const String& = String()) {
  gCanvas.fillScreen(0); gCanvas.setFont(&fonts::FreeSansBold9pt7b); gCanvas.setCursor(8, 3); gCanvas.print(t); gCanvas.setFont(&fonts::Font2); }
int overlays = 0;
void renderLauncherOverlay() { ++overlays; }
void renderDebugOverlay() {}
struct Keyboard_Class { struct KeysState { bool ctrl = false, alt = false, enter = false, del = false, fn = false, tab = false; std::vector<char> word; }; };
Keyboard_Class::KeysState gKeyboardState;
enum class UiMode { Home, TvRemote };
UiMode gUiMode = UiMode::TvRemote;
bool gLauncherVisible = false;
uint32_t gLastRenderMs = 0, tick = 1000;
uint32_t millis() { return tick; }
struct { int changes = 0; void invalidate() { ++changes; } } gUiFramePolicy;
struct { int32_t level = 77; } gBatterySnapshot;
struct { uint32_t getFreeHeap() { return 98000; } } ESP;
constexpr int MALLOC_CAP_8BIT = 1;
size_t heap_caps_get_largest_free_block(int) { return 40000; }
std::vector<std::string> logged, heapTags;
void appendRuntimeLog(const String& area, const String& message, bool) { assert(area == "TV"); logged.push_back(message); }
void logHeapStats(const char* tag) { heapTags.push_back(tag); }
int flashes = 0;
void flashActivityLed(uint8_t, uint8_t, uint8_t, uint32_t) { ++flashes; }
// Wi-Fi, as Bluetooth takes it off and gives it back.
enum { WIFI_OFF = 0, WIFI_STA = 1 };
bool gWifiReady = true, networkIdle = true;
int emojiClears = 0, netPreps = 0, wifiDials = 0;
std::string lastDial;
bool networkWorkIdle() { return networkIdle; }
void clearEmojiCache() { ++emojiClears; }
void prepareNetworkMemory(const char* why, bool force) { assert(std::string(why) == "TV" && force); ++netPreps; }
struct {
  int m = WIFI_STA, disconnects = 0;
  int getMode() { return m; }
  bool disconnect(bool, bool) { ++disconnects; return true; }
  bool mode(int to) { m = to; return true; }
} WiFi;
void beginWifiStationConnect(const char* why) { assert(WiFi.m == WIFI_OFF); WiFi.m = WIFI_STA; ++wifiDials; lastDial = why; }
// The saved setting, as Preferences keeps it.
std::map<std::string, bool> saved;
struct {
  std::string ns; bool readOnly = true, open = false;
  bool begin(const char* n, bool ro) { assert(!open); ns = n; readOnly = ro; open = true; return true; }
  void end() { assert(open); open = false; }
  bool getBool(const char* k, bool d) { assert(open); auto it = saved.find(ns + "/" + k); return it == saved.end() ? d : it->second; }
  void putBool(const char* k, bool v) { assert(open && !readOnly); saved[ns + "/" + k] = v; }
} gPrefs;
// The infrared LED: every frame it sends.
struct Sent { char kind; uint32_t value; };
std::vector<Sent> ir;
int irInits = 0;
void initIrRemote() { ++irInits; }
struct {
  void sendNECRaw(uint32_t raw, int repeats) { assert(repeats == 0); ir.push_back({'N', raw}); }
  void sendSamsung(uint16_t address, uint16_t command, int repeats) { assert(repeats == 0 && address < 0x100 && command < 0x100);
    ir.push_back({'S', tv_remote::samsungFrame(uint8_t(address), uint8_t(command))}); }
} IrSender;
// The Bluetooth stack, as the app sees it.
namespace tv_ble {
State st = State::Off; bool isOn = false, bonded = false, startFails = false;
int starts = 0, stops = 0, forgets = 0, battery = -2;
std::vector<std::pair<uint8_t, uint8_t>> keys; std::vector<uint16_t> medias;
bool start(const char* name, int b) { ++starts; assert(std::string(name) == "Cardputer Remote"); battery = b; assert(!isOn);
  assert(WiFi.m == WIFI_OFF);  // Wi-Fi's memory is free first.
  if (startFails) { st = State::Failed; return false; } isOn = true; st = State::Waiting; return true; }
void stop() { ++stops; if (isOn) assert(WiFi.m == WIFI_OFF); isOn = false; st = State::Off; }  // Running, Wi-Fi was off.
State state() { return st; }
bool on() { return isOn; }
uint32_t heapCost() { return 31000; }
const char* failure() { if (!available()) return "Bluetooth isn't in this version";  // As the real one without it.
  return st == State::Failed ? "Not enough memory for Bluetooth" : ""; }
bool paired() { return isOn && bonded; }
bool forget() { assert(isOn); ++forgets; bonded = false; return true; }
bool key(uint8_t u, uint8_t m) { assert(u); if (st != State::Connected) return false; keys.push_back({u, m}); return true; }
bool media(uint16_t u) { assert(u); if (st != State::Connected) return false; medias.push_back(u); return true; }
void connect() { assert(isOn); st = State::Connected; bonded = true; }
void drop() { assert(isOn); st = State::Waiting; }
}  // namespace tv_ble
''' + app + r'''
using namespace tv_remote;
void render() { renderTvRemoteUi(); }
// A key as the keyboard sends it: the character, and while it's down, the held word.
void press(char c, bool fn = false) {
  Keyboard_Class::KeysState k; k.fn = fn; k.word = {c};
  gKeyboardState = k; assert(handleTvRemoteKey(c, k)); render();
}
void enter() { Keyboard_Class::KeysState k; k.enter = true; gKeyboardState = k; assert(handleTvRemoteKey('\n', k)); render(); }
void del() { Keyboard_Class::KeysState k; k.del = true; gKeyboardState = k; assert(handleTvRemoteKey('\b', k)); render(); }
void release() { gKeyboardState = Keyboard_Class::KeysState{}; }
void later(uint32_t ms) { tick += ms; serviceTvRemote(); }
uint32_t lastIr() { assert(!ir.empty()); return ir.back().value; }
// Every screen, in every state, fits and keeps its text apart.
void renderAll() {
  for (auto s : {tv_ble::State::Off, tv_ble::State::Waiting, tv_ble::State::Connected, tv_ble::State::Failed})
    for (bool bonded : {false, true})
      for (TvView v : {TvView::Remote, TvView::Typing}) for (bool pending : {false, true}) {
        const auto keepState = tv_ble::st; const bool keepOn = tv_ble::isOn, keepBond = tv_ble::bonded; const TvView keepView = gTvView;
        const bool keepPending = gTvBluetoothPending;
        tv_ble::st = s; tv_ble::isOn = s == tv_ble::State::Waiting || s == tv_ble::State::Connected; tv_ble::bonded = bonded; gTvView = v;
        gTvBluetoothPending = pending && !tv_ble::isOn;
        render();
        tv_ble::st = keepState; tv_ble::isOn = keepOn; tv_ble::bonded = keepBond; gTvView = keepView; gTvBluetoothPending = keepPending;
      }
  render();
}

int main() {
  // --- The codes -----------------------------------------------------------------------------
  // Byte for byte the Flipper-IRDB recording from an Apple TV 4K: address EE 87 or E5 87, then the command.
  const auto bytes = [](uint32_t f) { return std::vector<uint8_t>{uint8_t(f), uint8_t(f >> 8), uint8_t(f >> 16), uint8_t(f >> 24)}; };
  using B = std::vector<uint8_t>;
  assert(bytes(irCode(Action::Up).nec) == (B{0xEE, 0x87, 0x0A, 0x8C}));
  assert(bytes(irCode(Action::Down).nec) == (B{0xEE, 0x87, 0x0C, 0x8C}));
  assert(bytes(irCode(Action::Left).nec) == (B{0xEE, 0x87, 0x09, 0x8C}));
  assert(bytes(irCode(Action::Right).nec) == (B{0xEE, 0x87, 0x06, 0x8C}));
  assert(bytes(irCode(Action::Select).nec) == (B{0xEE, 0x87, 0x5C, 0x8C}));
  assert(bytes(irCode(Action::Back).nec) == (B{0xEE, 0x87, 0x03, 0x8C}));
  assert(bytes(irCode(Action::PlayPause).nec) == (B{0xEE, 0x87, 0x5F, 0x8C}));
  assert(bytes(irCode(Action::Home).nec) == (B{0xE5, 0x87, 0x40, 0xFE}));
  assert(bytes(irCode(Action::Sleep).nec) == (B{0xE5, 0x87, 0x2A, 0xFE}));
  // In Apple's EE 87 frames the command's low bit is parity: the ones in it and the last byte are odd.
  for (Action a : {Action::Up, Action::Down, Action::Left, Action::Right, Action::Select, Action::Back, Action::PlayPause}) {
    const IrCode c = irCode(a); assert(c.protocol == IrProtocol::Nec);
    assert((__builtin_popcount((c.nec >> 16) & 0xFF) + __builtin_popcount(c.nec >> 24)) % 2 == 1);
  }
  // Samsung32 at address 07: 07 07, the command, its inverse. Vol+ is the well-known E0E0E01F.
  assert(irCode(Action::VolumeUp).protocol == IrProtocol::Samsung && samsungFrame(0x07, 0x07) == 0xF8070707u);
  assert(irCode(Action::VolumeDown).command == 0x0B && irCode(Action::Mute).command == 0x0F && irCode(Action::TvPower).command == 0x02);
  for (Action a : {Action::VolumeUp, Action::VolumeDown, Action::Mute, Action::TvPower}) assert(irCode(a).address == 0x07 && forTv(a));
  for (Action a : {Action::Bluetooth, Action::Typing, Action::Forget, Action::None}) assert(irCode(a).protocol == IrProtocol::None);
  // Every frame is different, so no key can do another's job.
  std::vector<uint32_t> frames;
  for (int i = 1; i <= int(Action::TvPower); ++i) { const IrCode c = irCode(Action(i));
    frames.push_back(c.protocol == IrProtocol::Nec ? c.nec : samsungFrame(c.address, c.command)); }
  std::sort(frames.begin(), frames.end()); assert(std::adjacent_find(frames.begin(), frames.end()) == frames.end());

  // The report map: collections close, and the reports are a keyboard (1) and a consumer control (2).
  int depth = 0; std::vector<int> ids;
  for (size_t i = 0; i < sizeof(kReportMap);) {
    const uint8_t prefix = kReportMap[i]; const size_t n = (prefix & 3) == 3 ? 4 : (prefix & 3);
    if (prefix == 0xA1) { ++depth; }
    if (prefix == 0xC0) { assert(depth > 0); --depth; }
    if (prefix == 0x85) { ids.push_back(kReportMap[i + 1]); }
    i += 1 + n; assert(i <= sizeof(kReportMap));
  }
  assert(depth == 0 && ids == (std::vector<int>{1, 2}) && sizeof(kReportMap) == 90);

  // Every printable character has a key, capitals and symbols with Shift, and no two share one.
  std::map<std::pair<int, int>, char> seen;
  for (int c = 0x20; c < 0x7f; ++c) {
    const HidKey k = hidForChar(char(c)); assert(k.usage && k.usage <= 0x65 && (k.modifiers == 0 || k.modifiers == kShift));
    assert(seen.emplace(std::make_pair(int(k.usage), int(k.modifiers)), char(c)).second);
  }
  assert(hidForChar('a').usage == 0x04 && hidForChar('z').usage == 0x1D && hidForChar('A').modifiers == kShift);
  assert(hidForChar('1').usage == 0x1E && hidForChar('0').usage == 0x27 && hidForChar('!').usage == 0x1E && hidForChar('!').modifiers == kShift);
  assert(hidForChar(' ').usage == kKeySpace && hidForChar('\n').usage == kKeyEnter && hidForChar('\b').usage == kKeyBackspace);
  assert(hidForChar(char(0xC3)).usage == 0 && hidForChar('\x01').usage == 0);
  // The remote's keys over Bluetooth: arrows, Enter, Esc for Back, and a media key for Play/Pause.
  assert(bluetoothFor(Action::Up).key == kKeyUp && bluetoothFor(Action::Back).key == kKeyEscape && bluetoothFor(Action::Select).key == kKeyEnter);
  assert(bluetoothFor(Action::PlayPause).media == kMediaPlayPause && !bluetoothFor(Action::PlayPause).key);
  for (Action a : {Action::Home, Action::Sleep, Action::VolumeUp, Action::Mute, Action::TvPower}) assert(!bluetoothFor(a).key && !bluetoothFor(a).media);
  // Keys, with Fn or without, and a USB keyboard's arrows.
  assert(actionForKey(';', false, false) == Action::Up && actionForKey(':', false, false) == Action::Up && actionForKey(1, false, false) == Action::Up);
  assert(actionForKey('/', false, false) == Action::Right && actionForKey(4, false, false) == Action::Right);
  assert(actionForKey('x', true, false) == Action::Select && actionForKey('x', false, true) == Action::Back);
  assert(actionForKey('b', false, false) == Action::Bluetooth && actionForKey('B', false, false) == Action::Forget);
  assert(actionForKey('x', false, false) == Action::None && actionForKey('`', false, false) == Action::None);
  assert(typingArrow(';', true) == kKeyUp && typingArrow(';', false) == 0 && typingArrow(3, false) == kKeyLeft && typingArrow('a', true) == 0);
  assert(repeats(Action::Up) && repeats(Action::VolumeDown) && !repeats(Action::Select) && !repeats(Action::Mute) && !repeats(Action::Sleep));

  // --- The app -------------------------------------------------------------------------------
#if !TV_REMOTE_BLUETOOTH
  // Without Bluetooth: infrared only. A saved "on" doesn't start it, B, T and Shift+B say why, and
  // Wi-Fi is never touched.
  saved["tv_remote/bt"] = true;
  beginTvRemote(); render();
  assert(tv_ble::starts == 0 && WiFi.m == WIFI_STA && emojiClears == 0 && shows("Infrared") && shows("APPLE TV") && shows("SAMSUNG TV"));
  for (const char* s : {"Del", "Back", "Spc", "Play", "H", "Home", "Z", "Sleep", "=", "Vol +", "-", "Vol -", "M", "Mute", "P", "Power", "OK"}) assert(shows(s));
  assert(!shows("T") && !shows("Type") && !shows("B") && !showsPart("BT") && !showsPart("Bluetooth"));
  for (char c : {'b', 't', 'B'}) { press(c); assert(shows("Bluetooth isn't in this version") && !tvRemoteTakesText()); }
  assert(tv_ble::starts == 0 && WiFi.m == WIFI_STA && !tvRemoteHoldsWifi());
  press('x'); assert(shows("H home, Z sleep, = - volume"));
  press(';'); assert(lastIr() == irCode(Action::Up).nec && shows("Up by infrared"));
  press('='); assert(lastIr() == samsungFrame(7, 0x07));
  release(); press(';'); later(450); assert(lastIr() == irCode(Action::Up).nec); release(); later(0);
  stopTvRemote(); assert(tv_ble::stops == 0 && wifiDials == 0);
  beginTvRemote();
  for (int i = 0; i <= int(Action::TvPower); ++i) { gTvLast = Action(i); gTvNoticeUntilMs = 0; render(); }
  tvSay("Bluetooth isn't in this version", tvMuted()); render();
  puts("PASS TV Remote without Bluetooth: infrared only, Bluetooth keys say why, Wi-Fi untouched, screens fit");
  return 0;
#endif
  // Opened with Bluetooth off, as it starts: everything goes by infrared.
  beginTvRemote(); render();
  assert(tv_ble::starts == 0 && irInits > 0 && shows("TV Remote") && shows("Infrared") && shows("Point it at the Apple TV") && overlays == 1);
  for (const char* s : {"Del", "Back", "Spc", "Play", "H", "Home", "Z", "Sleep", "T", "Type", "B", "BT on", "=", "Vol +", "-", "Vol -", "M", "Mute", "P", "Power", "OK"}) assert(shows(s));
  assert(gCanvas.arcs == 0);  // Nothing lit yet.
  assert(!tvRemoteTakesText());
  press(';'); assert(lastIr() == irCode(Action::Up).nec && shows("Up by infrared") && flashes == 1);
  // The arrow just sent lights its quarter of the ring, and goes dark again after a moment.
  assert(gCanvas.arcs == 1 && gTvFlash == Action::Up);
  later(179); render(); assert(gCanvas.arcs == 1);
  later(1); render(); assert(gCanvas.arcs == 0 && gTvFlash == Action::None);
  press('.', true); assert(lastIr() == irCode(Action::Down).nec);  // Fn makes no difference here.
  press(','); assert(lastIr() == irCode(Action::Left).nec);
  press('/'); assert(lastIr() == irCode(Action::Right).nec && gCanvas.arcs == 2);  // Right spans 0 degrees: two arcs.
  enter(); assert(lastIr() == irCode(Action::Select).nec && shows("OK by infrared"));
  del(); assert(lastIr() == irCode(Action::Back).nec);
  press(' '); assert(lastIr() == irCode(Action::PlayPause).nec);
  press('h'); assert(lastIr() == irCode(Action::Home).nec);
  press('Z'); assert(lastIr() == irCode(Action::Sleep).nec);
  press('='); assert(lastIr() == samsungFrame(7, 0x07) && shows("Volume up to the TV"));
  press('+'); press('-'); press('_'); assert(lastIr() == samsungFrame(7, 0x0B));
  press('m'); assert(lastIr() == samsungFrame(7, 0x0F));
  press('p'); assert(lastIr() == samsungFrame(7, 0x02));
  { Keyboard_Class::KeysState k; k.word = {char(2)}; assert(handleTvRemoteKey(char(2), k)); assert(lastIr() == irCode(Action::Down).nec); }
  // A key with no job says what the keys are, and sends nothing.
  size_t before = ir.size(); press('x'); assert(ir.size() == before && shows("H home, T type, B Bluetooth"));
  // Ctrl and Alt keys go on to the device's own shortcuts.
  { Keyboard_Class::KeysState k; k.ctrl = true; assert(!handleTvRemoteKey('l', k)); k.ctrl = false; k.alt = true; assert(!handleTvRemoteKey('h', k)); }
  assert(ir.size() == before);
  assert(logged.empty());  // No key is logged.

  // Holding an arrow: nothing until 450 ms, then every 110 ms, until it's let go.
  release(); press(';'); before = ir.size();
  later(449); assert(ir.size() == before);
  later(1); assert(ir.size() == before + 1 && lastIr() == irCode(Action::Up).nec);
  later(109); assert(ir.size() == before + 1);
  later(1); assert(ir.size() == before + 2);
  later(500); assert(ir.size() == before + 3);  // Late, still once: no burst to catch up.
  release(); later(500); assert(ir.size() == before + 3);
  press('-'); later(450); assert(lastIr() == samsungFrame(7, 0x0B) && ir.size() == before + 5);  // The volume repeats too.
  release(); later(0);
  // Not OK, Mute or Sleep: one press is one.
  enter(); before = ir.size(); later(600); later(600); assert(ir.size() == before);
  press('m'); before = ir.size(); later(600); later(600); assert(ir.size() == before);
  // A held key that changes, or the menu opening, stops the repeats.
  press('/'); gKeyboardState.word = {'/', ';'}; before = ir.size(); later(600); assert(ir.size() == before);
  press('/'); gLauncherVisible = true; later(600); assert(ir.size() == before + 1); gLauncherVisible = false; release();

  // B turns Bluetooth on, and saves that. Until something pairs, the screen says where to pair it.
  press('b');
  assert(tv_ble::starts == 1 && tv_ble::on() && tv_ble::battery == 77 && saved["tv_remote/bt"]);
  assert(shows("BT waiting") && shows("Pair on the Apple TV:") && shows("Remotes and Devices >") && shows("Cardputer Remote"));
  assert(heapTags == (std::vector<std::string>{"tv-bt-before", "tv-bt-on"}) && logged.size() == 1);
  assert(logged[0] == "Bluetooth on, with Wi-Fi=98000 before=98000 largest=40000 took=31000 now=98000");
  assert(WiFi.m == WIFI_OFF && WiFi.disconnects == 1 && !gWifiReady && tvRemoteHoldsWifi() && emojiClears == 1 && netPreps == 1);
  assert(shows("Wi-Fi is off while Bluetooth is on"));
  // Not connected yet, so the arrows still go by infrared.
  press(';'); assert(lastIr() == irCode(Action::Up).nec && tv_ble::keys.empty());
  // Connected: the Apple TV's keys go by Bluetooth, with no pointing; the TV's still by infrared.
  tv_ble::connect(); serviceTvRemote(); render();
  assert(shows("Bluetooth") && !shows("BT waiting") && logged.back() == "Bluetooth connected" && shows("H"));
  before = ir.size();
  press(';'); press('.'); press(','); press('/'); enter(); del(); press(' ');
  assert(ir.size() == before && tv_ble::keys.size() == 6 && tv_ble::medias == (std::vector<uint16_t>{kMediaPlayPause}));
  assert(tv_ble::keys[0].first == kKeyUp && tv_ble::keys[3].first == kKeyRight && tv_ble::keys[4].first == kKeyEnter && tv_ble::keys[5].first == kKeyEscape);
  assert(shows("Play/Pause by Bluetooth"));
  press('h'); press('='); assert(ir.size() == before + 2 && lastIr() == samsungFrame(7, 0x07));
  // Holding an arrow repeats over Bluetooth too.
  press(';'); const size_t k0 = tv_ble::keys.size(); later(450); later(110); assert(tv_ble::keys.size() == k0 + 2); release(); later(0);
  // It drops: back to infrared, and back to Bluetooth when the Apple TV is there again.
  tv_ble::drop(); serviceTvRemote(); assert(logged.back() == "Bluetooth disconnected");
  render(); assert(shows("BT waiting") && shows("H"));  // Paired before, so no pairing steps.
  press(';'); assert(lastIr() == irCode(Action::Up).nec);
  tv_ble::connect(); serviceTvRemote();

  // T types on the Apple TV: each key as a keyboard's, Shift for capitals, and what's typed on screen.
  press('t'); assert(tvRemoteTakesText() && shows("Type") && shows("_") && shows("Keys type on the Apple TV"));
  tv_ble::keys.clear();
  press('H'); press('i'); press('!');
  assert(tv_ble::keys == (std::vector<std::pair<uint8_t, uint8_t>>{{0x0B, kShift}, {0x0C, 0}, {0x1E, kShift}}) && shows("Hi!_"));
  // The arrows' own characters type: ; . , / are text here. Fn makes them arrows.
  press(';'); assert(tv_ble::keys.back() == std::make_pair(uint8_t(0x33), uint8_t(0)) && shows("Hi!;_"));
  press(';', true); assert(tv_ble::keys.back().first == kKeyUp && shows("Hi!;_"));
  later(450); assert(tv_ble::keys.back().first == kKeyUp); release(); later(0);  // A held Fn arrow repeats.
  { Keyboard_Class::KeysState k; assert(handleTvRemoteKey(char(4), k)); assert(tv_ble::keys.back().first == kKeyRight); }
  // Letters that mean something on the remote are only letters.
  before = ir.size(); press('h'); press('b'); press('t'); assert(ir.size() == before && tv_ble::on() && shows("Hi!;hbt_"));
  del(); assert(tv_ble::keys.back().first == kKeyBackspace && shows("Hi!;hb_"));
  enter(); assert(tv_ble::keys.back().first == kKeyEnter && shows("_"));
  // A letter no US key makes isn't sent.
  const size_t k1 = tv_ble::keys.size(); press(char(0xC3)); assert(tv_ble::keys.size() == k1 && showsPart("can't be typed"));
  // A long word shows its end, and what's kept stays short.
  for (int i = 0; i < 100; ++i) press(char('a' + i % 26));
  assert(gTvTyped.size() == kTvTypedKept && gTvTyped.back() == 'v'); render();
  // Ctrl still reaches the device's shortcuts.
  { Keyboard_Class::KeysState k; k.ctrl = true; assert(!handleTvRemoteKey('l', k)); }
  // Nothing typed is ever logged.
  for (auto& line : logged) assert(line.find("Hi") == std::string::npos && line.find("abc") == std::string::npos);
  // Esc goes back to the remote, and the typing goes with it. Esc on the remote leaves the app.
  assert(tvRemoteEscape() && !tvRemoteTakesText() && gTvTyped.empty()); render(); assert(shows("TV Remote"));
  assert(!tvRemoteEscape());
  // Not connected: typing says so and sends nothing.
  press('t'); tv_ble::drop(); const size_t k2 = tv_ble::keys.size(); press('a');
  assert(tv_ble::keys.size() == k2 && showsPart("Not connected yet") && gTvTyped.empty());
  tvRemoteEscape(); tv_ble::connect();

  // Shift+B twice forgets the Apple TV; once, or too slowly, doesn't.
  press('B'); assert(tv_ble::forgets == 0 && showsPart("Shift+B again forgets"));
  tick += 4001; press('B'); assert(tv_ble::forgets == 0);
  press(';'); press('B'); assert(tv_ble::forgets == 0);  // Another key in between starts again.
  press('B'); assert(tv_ble::forgets == 1 && !tv_ble::bonded && shows("Forgotten: pair again on the Apple TV") && logged.back() == "Forgot paired devices");

  // Leaving turns Bluetooth off, and coming back turns it on again, because that's how it was left.
  stopTvRemote(); assert(!tv_ble::on() && tv_ble::stops == 1 && heapTags.back() == "tv-bt-off" && saved["tv_remote/bt"]);
  assert(WiFi.m == WIFI_STA && wifiDials == 1 && lastDial == "Wi-Fi back on" && !tvRemoteHoldsWifi());
  stopTvRemote(); assert(wifiDials == 1);  // Once.
  beginTvRemote(); assert(tv_ble::on() && tv_ble::starts == 2 && gTvView == TvView::Remote && WiFi.m == WIFI_OFF);
  // B again: off, saved off, and the next visit starts with infrared only.
  press('b'); assert(!tv_ble::on() && !saved["tv_remote/bt"] && showsPart("keys go by infrared") && WiFi.m == WIFI_STA && wifiDials == 2);
  stopTvRemote(); beginTvRemote(); assert(!tv_ble::on() && tv_ble::starts == 2);
  // With Bluetooth off, Shift+B says how to turn it on.
  press('B'); assert(showsPart("B turns it on") && tv_ble::forgets == 1);
  // T turns Bluetooth on by itself, for typing.
  press('t'); assert(tv_ble::on() && saved["tv_remote/bt"] && tvRemoteTakesText());
  assert(shows("Pair first: Settings > Remotes") && shows("Cardputer Remote"));  // Nothing paired: where to pair.
  tvRemoteEscape(); press('b');

  // When Bluetooth can't start, it says why, and the remote still works by infrared.
  tv_ble::startFails = true;
  press('b'); assert(!tv_ble::on() && shows("BT failed") && shows("Not enough memory for Bluetooth"));
  assert(logged.back().find("Bluetooth failed: Not enough memory for Bluetooth with Wi-Fi=") == 0);
  assert(WiFi.m == WIFI_STA && !tvRemoteHoldsWifi());
  press('t'); assert(!tvRemoteTakesText());
  press(';'); assert(lastIr() == irCode(Action::Up).nec);
  stopTvRemote(); tv_ble::startFails = false; tv_ble::st = tv_ble::State::Off;
  beginTvRemote();

  // A request under way keeps its network: Bluetooth waits, then starts by itself.
  press('b'); assert(!tv_ble::on()); stopTvRemote();
  networkIdle = false; const int startsBefore = tv_ble::starts;
  beginTvRemote(); press('b'); render();
  assert(!tv_ble::on() && gTvBluetoothPending && WiFi.m == WIFI_STA && shows("BT starting") && shows("Bluetooth waits for a request to end"));
  press('t'); render(); assert(tvRemoteTakesText() && shows("network is free..."));
  tvRemoteEscape();
  later(100); assert(tv_ble::starts == startsBefore);
  networkIdle = true; later(10); assert(tv_ble::on() && !gTvBluetoothPending && WiFi.m == WIFI_OFF && tv_ble::starts == startsBefore + 1);
  // B while waiting gives up the wait; so does leaving.
  press('b'); networkIdle = false; press('b'); assert(gTvBluetoothPending); press('b');
  assert(!gTvBluetoothPending && !saved["tv_remote/bt"] && WiFi.m == WIFI_STA);
  press('b'); assert(gTvBluetoothPending); stopTvRemote(); assert(!gTvBluetoothPending && WiFi.m == WIFI_STA);
  networkIdle = true; later(10); assert(!tv_ble::on());
  beginTvRemote();

  // Every screen fits, in every state, with a notice and without, and with a long typed line.
  renderAll();
  tvSay("Shift+B again forgets the Apple TV", tvAmber()); renderAll();
  gTvTyped = std::string(kTvTypedKept, 'W'); renderAll();
  for (int i = 1; i <= int(Action::Forget); ++i) { gTvLast = Action(i); gTvLastByBluetooth = i % 2; gTvNoticeUntilMs = 0; render(); }
  puts("PASS TV Remote: Apple TV and Samsung infrared frames, the keyboard's report map and characters, every key, holding keys, Bluetooth on and off, pairing, typing, forgetting, leaving and every screen fitting");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-tv-remote-') as d:
    p = Path(d); (p / 'test.cpp').write_text(code)
    # Both builds: with Bluetooth, as it may be switched back on, and without, as it ships now.
    for flag in ('1', '0'):
        exe = p / ('test' + flag)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-DTV_REMOTE_BLUETOOTH=' + flag, '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(src),
                        str(p / 'test.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=60)
