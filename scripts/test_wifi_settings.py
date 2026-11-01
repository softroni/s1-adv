#!/usr/bin/env python3
"""Run the production Wi-Fi switching decisions against a simulated radio, on the host.

This covers what the firmware decides. It doesn't show that a real access point accepts a
join, which only a device can.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
    ('001_main.cpp.inc', '#include "wifi_settings.h"'),
    ('002_main.cpp.inc', 'WifiSettings,'),
    ('004_main.cpp.inc', '{"Wi-Fi", UiMode::WifiSettings}'),
    ('007_main.cpp.inc', 'return "Wi-Fi";'),
    ('022_main.cpp.inc', 'mode == "wifi_settings"'),
    ('024_main.cpp.inc', 'gUiMode == UiMode::WifiSettings) stopWifiSettings();'),
    ('024_main.cpp.inc', 'mode == UiMode::WifiSettings) beginWifiSettings();'),
    ('043_main.cpp.inc', 'cmd == "/wifi" || cmd == "wifi") {\n    setUiMode(UiMode::WifiSettings);'),
    ('047_main.cpp.inc', 'gUiMode == UiMode::WifiSettings && !gLauncherVisible && wifiSettingsEscape()'),
    ('048_main.cpp.inc', 'handleWifiSettingsKey(key, keys)'),
    ('061_main.cpp.inc', 'renderWifiSettingsUi();'),
    ('061_main.cpp.inc', 'serviceWifiSettings();'),
    ('063_main.cpp.inc', 'case UiMode::WifiSettings:')]:
    assert text in (parts / file).read_text(), (file, text)
assert 'main_parts/079_main.cpp.inc' in (src / 'main.cpp').read_text()

# Everything that could dial the saved network has to stand aside during a switch.
for file, body in [
    ('040_main.cpp.inc', 'bool ensureWifiForHttp('),
    ('040_main.cpp.inc', 'void serviceDeferredBoot('),
    ('043_main.cpp.inc', 'void maintainConnectivity(')]:
    text = (parts / file).read_text()
    assert 'wifiSettingsOwnsRadio()' in text[text.index(body):text.index(body) + 420], body

# The setup portal draws over every mode, so the Wi-Fi screen must be able to get past it.
assert 'if (gSetupPortalActive && gUiMode != UiMode::WifiSettings) {\n    renderSetupPortalUi();' \
    in (parts / '060_main.cpp.inc').read_text()

# On the device these bytes are the password. They go to the driver and to Preferences only.
app = (parts / '079_main.cpp.inc').read_text()
for line in app.splitlines():
    if re.search(r'\b(logf|appendRuntimeLog|pushSystem|setStatus|Serial)\b', line):
        assert 'password' not in line.lower(), line
keys = (parts / '048_main.cpp.inc').read_text()
assert keys.index('wifiSettingsTakesAllKeys()') < keys.index('setKeyDiag(String("key=")'), \
    'field keys must not reach the key diagnostics'


def function(source, signature):
    start = source.index(signature)
    i = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[start:i]


code = r'''
#include "wifi_settings.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
using namespace wifi_settings;

struct FakeRadio {
  std::map<std::string, std::string> air;      // Networks in range, and their passwords.
  std::string link, savedName = "Home", savedPassword = "homepassword";
  std::vector<Network> found;
  bool isBusy = false, dialing = true, scanRuns = false, refuseScan = false, stale = false;
  bool failScan = false, neverFinish = false;
  int holds = 0, scans = 0, stops = 0, joins = 0, rejoins = 0, saves = 0;
  std::string lastJoinName, lastJoinPassword;

  bool online() { return !link.empty(); }
  bool busy() { return isBusy; }
  void hold() { ++holds; dialing = false; }
  bool startScan() {
    if (refuseScan || (dialing && link.empty())) return false;
    ++scans; scanRuns = true;
    return true;
  }
  int scanResults(std::vector<Network>& out) {
    if (failScan) return kScanFailed;
    if (neverFinish) return kScanRunning;
    out = found; scanRuns = false;
    return static_cast<int>(found.size());
  }
  void stopScan() { ++stops; scanRuns = false; }
  void join(const std::string& name, const std::string& password) {
    assert(!scanRuns);
    ++joins; lastJoinName = name; lastJoinPassword = password; link.clear(); dialing = true;
    auto it = air.find(name);
    if (it != air.end() && it->second == password) link = name;
  }
  bool joined(const std::string& name) { return link == name || stale; }
  bool notFound() { return !lastJoinName.empty() && air.find(lastJoinName) == air.end(); }
  void rejoinSaved() {
    ++rejoins; dialing = true; link.clear(); stale = false;
    auto it = air.find(savedName);
    if (it != air.end() && it->second == savedPassword) link = savedName;
  }
  void save(const std::string& name, const std::string& password) {
    ++saves; savedName = name; savedPassword = password;
  }
};

struct Rig {
  FakeRadio radio;
  App app;
  uint32_t now = 1000;
  Rig() {
    radio.air = {{"Home", "homepassword"}, {"Cafe", "espresso1"}, {"Open", ""}};
    radio.link = "Home";
    radio.found = {{"Home", -40, true}, {"Cafe", -60, true}, {"Open", -70, false}};
  }
  void step(uint32_t ms = 0) { now += ms; app.poll(radio, now); }
  void open() { app.rescan(now); step(); step(); }
  void pick(const char* name) {
    for (size_t i = 0; i < app.networks.size(); ++i) {
      if (app.networks[i].ssid == name) app.selected = i;
    }
    app.enter(radio, now);
  }
  void typeIn(const std::string& text) { for (char c : text) app.type(c); }
  void waitPastSettle() { step(kJoinSettleMs); }
  void waitOut() { step(kJoinTimeoutMs); }
};

void listing() {
  assert(tidy({{"", -30, true}, {"A", -70, true}, {"B", -50, true}, {"A", -40, false}}).size() == 2);
  auto sorted = tidy({{"Weak", -80, true}, {"Strong", -40, true}, {"Strong", -90, false}});
  assert(sorted[0].ssid == "Strong" && sorted[0].rssi == -40 && !sorted.empty() && sorted[1].ssid == "Weak");
  std::vector<Network> many;
  for (int i = 0; i < 40; ++i) many.push_back({"Net" + std::to_string(i), -40 - i, true});
  auto capped = tidy(many);
  assert(capped.size() == kMaxNetworks && capped.front().ssid == "Net0");
  assert(signalBars(-40) == 4 && signalBars(-60) == 3 && signalBars(-70) == 2 && signalBars(-90) == 1);
  assert(masked("") == "" && masked("a") == "a" && masked("abc") == "**c");
  std::cout << "PASS the list: strongest first, one row per name, hidden left out, bounded\n";
}

void scanning() {
  Rig r;
  r.open();
  assert(r.radio.holds == 0 && r.radio.scans == 1 && !r.app.ownsRadio() && r.app.scanned);
  assert(r.app.networks.size() == 3 && r.app.rows() == 4 && r.app.selected == 0);

  // Dialing a network that isn't here blocks a scan until the radio is held.
  Rig offline;
  offline.radio.link.clear();
  offline.open();
  assert(offline.radio.holds == 1 && offline.radio.scans == 1 && offline.app.ownsRadio());
  offline.app.close(offline.radio);
  assert(offline.radio.rejoins == 1 && !offline.app.ownsRadio());

  // Busy radio: a bounded number of tries, then an honest message.
  Rig refused;
  refused.radio.refuseScan = true;
  refused.app.rescan(refused.now);
  for (int i = 0; i < 12; ++i) refused.step(kScanRetryMs);
  assert(!refused.app.scanning() && refused.app.mood == Mood::Bad);
  assert(refused.app.message.find("Scan failed") == 0);

  Rig stuck;
  stuck.radio.neverFinish = true;
  stuck.open();
  assert(stuck.app.scanning());
  stuck.step(kScanTimeoutMs);
  assert(!stuck.app.scanning() && stuck.radio.stops == 1 && stuck.app.mood == Mood::Bad);

  Rig failed;
  failed.radio.failScan = true;
  failed.open();
  assert(!failed.app.scanning() && failed.app.message.find("Scan failed") == 0);

  Rig empty;
  empty.radio.found.clear();
  empty.open();
  assert(empty.app.networks.empty() && empty.app.message.find("No networks") == 0 && empty.app.selected == 0);
  empty.app.enter(empty.radio, empty.now);
  assert(empty.app.screen == Screen::Name);

  // A rescan keeps your place, including on the last row.
  Rig kept;
  kept.open();
  kept.app.selected = 1;
  kept.radio.found = {{"New", -30, true}, {"Cafe", -60, true}};
  kept.open();
  assert(kept.app.networks[kept.app.selected].ssid == "Cafe");
  kept.app.selected = kept.app.networks.size();
  kept.radio.found = {{"New", -30, true}, {"Cafe", -60, true}, {"Third", -70, true}};
  kept.open();
  assert(kept.app.selected == kept.app.networks.size());
  std::cout << "PASS scanning: held when offline, retried, bounded, failures reported, place kept\n";
}

void joining() {
  Rig r;
  r.open();
  r.pick("Cafe");
  assert(r.app.screen == Screen::Password && r.app.secured && r.app.typing());
  r.typeIn("short");
  r.app.enter(r.radio, r.now);
  assert(r.radio.joins == 0 && r.app.mood == Mood::Bad && r.app.screen == Screen::Password);
  r.app.password.clear();
  r.app.enter(r.radio, r.now);
  assert(r.radio.joins == 0 && r.app.screen == Screen::Password);
  r.typeIn("espresso1");
  r.app.enter(r.radio, r.now);
  assert(r.app.screen == Screen::Joining && r.app.ownsRadio() && !r.app.typing());
  assert(r.radio.lastJoinName == "Cafe" && r.radio.lastJoinPassword == "espresso1");
  r.step();
  assert(r.radio.saves == 0);  // Not before the link has settled.
  r.waitPastSettle();
  assert(r.radio.saves == 1 && r.radio.savedName == "Cafe" && r.radio.savedPassword == "espresso1");
  assert(r.app.screen == Screen::List && r.app.mood == Mood::Good && !r.app.ownsRadio());
  assert(r.app.message == "Connected to Cafe" && r.app.password.empty());
  std::cout << "PASS a good join: validated, saved after it connects, draft cleared, radio released\n";
}

void wrongPassword() {
  Rig r;
  r.open();
  r.pick("Cafe");
  r.typeIn("wrongpassword");
  r.app.enter(r.radio, r.now);
  r.step(kJoinTimeoutMs - 1);
  assert(r.app.screen == Screen::Joining && r.radio.rejoins == 0);
  r.step(1);
  assert(r.radio.saves == 0 && r.radio.savedName == "Home" && r.radio.savedPassword == "homepassword");
  assert(r.radio.rejoins == 1 && r.radio.link == "Home" && !r.app.ownsRadio());
  assert(r.app.screen == Screen::Password && r.app.password == "wrongpassword");  // To fix it.
  assert(r.app.message.find("Check the password") != std::string::npos && r.app.mood == Mood::Bad);
  std::cout << "PASS a wrong password: nothing saved, old network back, typed text kept\n";
}

void staleStatus() {
  // Just after leaving a network, status can still say it's connected.
  Rig r;
  r.open();
  r.pick("Cafe");
  r.typeIn("wrongpassword");
  r.app.enter(r.radio, r.now);
  r.radio.stale = true;
  r.step(kJoinSettleMs - 1);
  assert(r.radio.saves == 0 && r.app.screen == Screen::Joining);
  r.radio.stale = false;
  r.waitOut();
  assert(r.radio.saves == 0);
  std::cout << "PASS a stale link status never saves a password\n";
}

void notFound() {
  Rig r;
  r.open();
  r.app.selected = r.app.networks.size();
  r.app.enter(r.radio, r.now);
  assert(r.app.screen == Screen::Name && !r.app.secured);
  r.app.enter(r.radio, r.now);
  assert(r.app.screen == Screen::Name && r.app.mood == Mood::Bad);
  r.typeIn("Faraway");
  r.app.enter(r.radio, r.now);
  assert(r.app.screen == Screen::Password);
  r.typeIn("somepassword");
  r.app.enter(r.radio, r.now);
  r.step();
  r.waitOut();
  assert(r.app.message.find("Not found") == 0 && r.radio.saves == 0 && r.app.screen == Screen::Password);
  std::cout << "PASS a network that isn't there says so, not 'check the password'\n";
}

void openNetworks() {
  Rig r;
  r.open();
  r.pick("Open");
  assert(r.app.screen == Screen::Joining && r.radio.lastJoinPassword.empty());
  r.waitPastSettle();
  assert(r.radio.saves == 1 && r.radio.savedName == "Open" && r.radio.savedPassword.empty());

  // A typed name may be open, so a blank password goes through. A short one still doesn't.
  Rig typed;
  typed.open();
  typed.app.selected = typed.app.networks.size();
  typed.app.enter(typed.radio, typed.now);
  typed.typeIn("Open");
  typed.app.enter(typed.radio, typed.now);
  typed.typeIn("short");
  typed.app.enter(typed.radio, typed.now);
  assert(typed.radio.joins == 0);
  for (int i = 0; i < 5; ++i) typed.app.backspace();
  typed.app.enter(typed.radio, typed.now);
  assert(typed.radio.joins == 1);
  typed.waitPastSettle();
  assert(typed.radio.savedName == "Open");

  Rig gone;
  gone.radio.air.erase("Open");
  gone.open();
  gone.pick("Open");
  gone.waitOut();
  assert(gone.app.screen == Screen::List && gone.radio.saves == 0 && gone.app.mood == Mood::Bad);
  std::cout << "PASS open networks: no password step, blank saved, failure returns to the list\n";
}

void backingOut() {
  Rig r;
  r.open();
  r.pick("Cafe");
  r.typeIn("espresso1");
  r.app.enter(r.radio, r.now);
  assert(r.app.escape(r.radio));
  assert(r.radio.rejoins == 1 && r.radio.saves == 0 && !r.app.ownsRadio() && r.radio.link == "Home");
  assert(r.app.screen == Screen::Password && r.app.message == "Cancelled");
  r.step(kJoinTimeoutMs);
  assert(r.radio.saves == 0 && r.radio.rejoins == 1);  // A cancelled join stays cancelled.
  assert(r.app.escape(r.radio) && r.app.screen == Screen::List && r.app.password.empty());
  assert(!r.app.escape(r.radio));  // The list hands Esc back, to go up to Settings.

  Rig typed;
  typed.open();
  typed.app.selected = typed.app.networks.size();
  typed.app.enter(typed.radio, typed.now);
  typed.typeIn("Name");
  typed.app.enter(typed.radio, typed.now);
  typed.typeIn("password");
  assert(typed.app.escape(typed.radio) && typed.app.screen == Screen::Name && typed.app.password.empty());
  assert(typed.app.name == "Name");
  assert(typed.app.escape(typed.radio) && typed.app.screen == Screen::List && typed.app.name.empty());

  // Leaving by the menu, or a screensaver, mid-join.
  Rig left;
  left.open();
  left.pick("Cafe");
  left.typeIn("espresso1");
  left.app.enter(left.radio, left.now);
  left.app.close(left.radio);
  assert(left.radio.rejoins == 1 && left.radio.saves == 0 && left.radio.link == "Home");
  assert(left.app.password.empty() && !left.app.ownsRadio() && left.app.screen == Screen::List);

  Rig midScan;
  midScan.radio.neverFinish = true;
  midScan.open();
  midScan.app.close(midScan.radio);
  assert(midScan.radio.stops == 1 && !midScan.radio.scanRuns);
  std::cout << "PASS backing out: one step at a time, old network restored, draft cleared\n";
}

void busy() {
  Rig r;
  r.open();
  r.pick("Cafe");
  r.typeIn("espresso1");
  r.radio.isBusy = true;
  r.app.enter(r.radio, r.now);
  assert(r.radio.joins == 0 && r.app.screen == Screen::Password && r.app.mood == Mood::Bad);
  assert(r.app.password == "espresso1" && r.radio.link == "Home");
  r.radio.isBusy = false;
  r.app.enter(r.radio, r.now);
  assert(r.radio.joins == 1);
  std::cout << "PASS a running request is never cut off, and the typed password survives the wait\n";
}

void joinDuringScan() {
  Rig r;
  r.open();
  r.radio.neverFinish = true;
  r.app.rescan(r.now);
  r.step();
  assert(r.app.scanning() && r.radio.scanRuns);
  r.pick("Open");  // FakeRadio::join() asserts no scan is running.
  assert(r.radio.stops == 1 && !r.app.scanning() && r.radio.joins == 1);
  std::cout << "PASS joining stops a scan first\n";
}

void fields() {
  Rig r;
  r.open();
  r.app.selected = r.app.networks.size();
  r.app.enter(r.radio, r.now);
  r.typeIn(std::string(40, 'n'));
  assert(r.app.name.size() == kMaxNameBytes);
  r.app.enter(r.radio, r.now);
  r.typeIn(std::string(80, 'p'));
  assert(r.app.password.size() == kMaxPasswordBytes);
  r.app.password.clear();
  r.typeIn("Aa1 `~!@#$%^&*()-_=+[]{}\\|;:'\",.<>/?");
  assert(r.app.password == "Aa1 `~!@#$%^&*()-_=+[]{}\\|;:'\",.<>/?");
  r.app.type('\t'); r.app.type(char(27)); r.app.type(char(1)); r.app.type(char(0xD0));
  assert(r.app.password.back() == '?');
  r.app.backspace();
  assert(r.app.password.back() == '/');
  assert(!r.app.revealed);
  r.app.toggleReveal();
  assert(r.app.revealed);
  r.app.password.clear();
  r.typeIn("espresso1");
  r.app.name = "Cafe";
  r.app.enter(r.radio, r.now);
  assert(!r.app.revealed);  // Never left on for the next screen.
  std::string secret = "topsecret";
  wipe(secret);
  assert(secret.empty());
  for (int moved = 0; moved < 2; ++moved) { r.app.move(0); }
  Rig list;
  list.open();
  list.app.move(-1);
  assert(list.app.selected == list.app.networks.size());
  list.app.move(1);
  assert(list.app.selected == 0);
  std::cout << "PASS fields: every printable character types, limits hold, control codes don't\n";
}

int main() {
  listing();
  scanning();
  joining();
  wrongPassword();
  staleStatus();
  notFound();
  openNetworks();
  backingOut();
  busy();
  joinDuringScan();
  fields();
}
'''

# The real functions, so the two fixes are checked where they live.
defaults = function((parts / '026_main.cpp.inc').read_text(), 'void applyCompileTimeDefaults()')
keyboard = function((parts / '025_main.cpp.inc').read_text(), 'void refreshKeyboardState()')
integration = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#define DEFAULT_WIFI_SSID "BuildNet"
#define DEFAULT_WIFI_PASSWORD "buildpassword"
#define DEFAULT_HUB_HOST ""
#define DEFAULT_DEVICE_ID ""
#define DEFAULT_DEVICE_TOKEN ""
struct String {
  std::string v;
  String(const char* s = "") : v(s) {}
  bool isEmpty() const { return v.empty(); }
  bool operator==(const char* s) const { return v == s; }
  bool operator!=(const String& o) const { return v != o.v; }
  const char* c_str() const { return v.c_str(); }
};
String gWifiSsid, gWifiPassword, gHubHost, gDeviceId, gDeviceToken;
uint16_t gHubPort = 0;
constexpr uint16_t kDefaultHubPort = 443;
uint16_t compileTimeHubPort() { return 0; }
bool activateTemporaryMidaWifi(const char*) { return false; }

struct KeysState { std::string word; };
struct {
  struct { KeysState keysState() { return state; } KeysState state; } Keyboard;
} M5Cardputer;
KeysState gKeyboardState;
String gLastKeySignature, gDiag;
std::string gLogged;
bool gTyping = false;
bool wifiSettingsTakesAllKeys() { return gTyping; }
String keyboardSignatureFromState() { return String(gKeyboardState.word.c_str()); }
void setKeyDiag(const String& text) { gDiag = text; }
template <class... T> void logf(const char*, const char* text) { gLogged += text; }
'''
integration += defaults + '\n' + keyboard + r'''
void boot(const char* ssid, const char* password) {
  gWifiSsid = String(ssid);
  gWifiPassword = String(password);
  applyCompileTimeDefaults();
}
void press(const char* word) {
  M5Cardputer.Keyboard.state.word = word;
  refreshKeyboardState();
  M5Cardputer.Keyboard.state.word = "";
  refreshKeyboardState();
}
int main() {
  boot("", "");
  assert(gWifiSsid == "BuildNet" && gWifiPassword == "buildpassword");
  boot("BuildNet", "");
  assert(gWifiPassword == "buildpassword");
  boot("Cafe", "espresso1");
  assert(gWifiPassword == "espresso1");
  boot("OpenCafe", "");
  assert(gWifiSsid == "OpenCafe" && gWifiPassword.isEmpty());
  std::cout << "PASS real applyCompileTimeDefaults: a saved open network keeps its blank password\n";

  press("h");
  assert(gLogged == "h");
  gTyping = true;
  gLogged.clear();
  press("s");
  press("e");
  assert(gLogged.empty() && gDiag.v != "s" && gDiag.v != "e");
  assert(gKeyboardState.word.empty());
  gTyping = false;
  press("k");
  assert(gLogged == "k");
  std::cout << "PASS real refreshKeyboardState: password keys never reach the uploadable key log\n";
}
'''

with tempfile.TemporaryDirectory() as tmp:
    for name, text, include in [('wifi', code, ['-I', str(src)]), ('wiring', integration, [])]:
        source = Path(tmp) / f'{name}.cpp'
        binary = Path(tmp) / name
        source.write_text(text)
        compiler = os.environ.get('CXX', 'c++')
        subprocess.run([compiler, '-std=gnu++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', *include, str(source), '-o', str(binary)],
                       check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout, end='')
        if result.returncode != 0:
            raise SystemExit(result.stderr)
print('PASS Wi-Fi settings: production decisions and wiring')
