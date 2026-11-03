#!/usr/bin/env python3
"""Start-up's way through the saved Wi-Fi networks, on the host.

The order and timing rules in wifi_fallback.h are tested directly. Then the firmware's real
deferred boot, its blocking connect for the install-at-boot updater, and the glue in 079 run
against a fake radio, card and clock: which networks are dialed, in what order, how long each
gets, which one is kept, and when the setup portal opens. It doesn't show a real radio's timing.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'


def part(number):
    return (parts / f'{number:03d}_main.cpp.inc').read_text()


def function(source, signature):
    start = source.index(signature)
    while source.find(';', start) < source.find('{', start):  # Skip forward declarations.
        start = source.index(signature, start + 1)
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


boot, wifi, mida = part(40), part(79), part(6)
glue = wifi[wifi.index('// --- Start-up: the other saved networks'):wifi.index('void stopWifiSettings() {')]
firmware = ''.join(function(mida, s) for s in ['bool temporaryMidaCaptiveEnabled(', 'bool usingTemporaryMidaWifi(',
                                               'bool activateTemporaryMidaWifi('])
firmware += function(wifi, 'String wifiLoggedName(') + glue
firmware += ''.join(function(boot, s) for s in ['void beginWifiStationConnect(', 'bool waitForWifiConnection(',
                                                'bool waitForBootWifi(', 'bool ensureWifi(', 'void beginDeferredWifi(',
                                                'void serviceDeferredBoot('])

code = r'''
#include "wifi_fallback.h"
#include "wifi_store.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// --- Arduino and the radio -------------------------------------------------------------------
struct String : std::string {
  String() = default;
  String(const char* s) : std::string(s) {}
  String(const std::string& s) : std::string(s) {}
  explicit String(int n) : std::string(std::to_string(n)) {}
  bool isEmpty() const { return empty(); }
};
String operator+(const String& a, const String& b) { return String(std::string(a) + std::string(b)); }
String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
String operator+(const char* a, const String& b) { return String(a + std::string(b)); }

uint32_t gNow = 0;
uint32_t millis() { return gNow; }
void delay(uint32_t ms) { gNow += ms; }
std::vector<std::string> gLog;
void logf(const char* format, ...) {
  char line[512]; va_list args; va_start(args, format); vsnprintf(line, sizeof(line), format, args); va_end(args);
  gLog.push_back(line);
}
bool logged(const std::string& text) { for (const auto& line : gLog) if (line.find(text) != std::string::npos) return true; return false; }
void appendRuntimeLog(const String&, const String&, bool) {}
std::vector<std::string> gChat;
void pushSystem(const String& text) { gChat.push_back(text); }
void pushError(const String& text) { gChat.push_back(text); }
void setStatus(const String&, uint32_t) {}
void handleHostSerialInput() {}

enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
enum { WIFI_STA = 1, WIFI_POWER_19_5dBm = 78, WIFI_PS_NONE = 0 };
void esp_wifi_set_ps(int) {}
struct Ip { String toString() const { return "10.0.0.2"; } };
constexpr uint32_t kSweepMs = 2500;   // How long the fake radio takes to find, or miss, a network.
struct Radio {
  std::map<std::string, std::string> air;   // In range, with their passwords.
  std::string ssid, password;
  bool dialing = false;
  uint32_t dialedAt = 0;
  std::vector<std::string> dialed;
  void mode(int) {}
  void setSleep(bool) {}
  void setAutoReconnect(bool) {}
  void setTxPower(int) {}
  void disconnect(bool, bool) { dialing = false; }
  void begin(const char* s, const char* p) { ssid = s; password = p; dialing = true; dialedAt = gNow; dialed.push_back(s); }
  int status() const {
    if (!dialing) return WL_DISCONNECTED;
    if (gNow - dialedAt < kSweepMs) return WL_DISCONNECTED;
    auto it = air.find(ssid);
    if (it == air.end()) return WL_NO_SSID_AVAIL;
    return it->second == password ? WL_CONNECTED : WL_DISCONNECTED;
  }
  String SSID() const { return status() == WL_CONNECTED ? String(ssid) : String(""); }
  Ip localIP() const { return {}; }
} WiFi;

// --- The card, and what the device remembers -------------------------------------------------
bool gSdReady = true;
struct Disk {} gWifiDisk;
struct Cipher {} gWifiCipher;
wifi_store::List gCard;
namespace wifi_store {
Load load(Disk&, Cipher&, List& out) { out = gCard; return out.empty() ? Load::Empty : Load::Ok; }
}
String gWifiSsid, gWifiPassword;
std::vector<std::string> gSavedAsCurrent;
void saveWifiCreds(const String& ssid, const String& password) { gWifiSsid = ssid; gWifiPassword = password; gSavedAsCurrent.push_back(ssid); }
constexpr uint32_t kWifiConnectTimeoutMs = 20000;

// --- The temporary network, off unless a build sets it up --------------------------------------
bool gMidaOn = false;
#define TEMP_MIDA_CAPTIVE_AUTOCONNECT gMidaOn
#define TEMP_MIDA_WIFI_SSID "Lobby"
#define TEMP_MIDA_CAPTIVE_LOGIN_URL "http://login.example"

// --- The rest of start-up --------------------------------------------------------------------
bool gBootBackgroundActive = true, gOtaBootApplyMode = false, gBootWifiStarted = false, gWifiReady = false;
bool gOtaPendingVerify = false, gPendingOtaCheckAction = false, gPendingOtaApplyAction = false;
uint32_t gBootWifiStartMs = 0, gBootStepStartedMs = 0, gPendingOtaActionAfterMs = 0;
int gBootBackgroundStep = 6, gEmojiAssetCount = 0;
String gBootBackgroundLabel;
struct { bool checkUpdatesOnBoot = false; bool autoUpdateFirmware = false; } gDeviceSettings;
std::string gPortal;
// Like the real one, a second start only reopens what the first started.
void startSetupPortal(const char* reason) { if (gPortal.empty()) gPortal = reason; }
bool wifiSettingsOwnsRadio() { return false; }
bool ensureCaptivePortalAccess(bool, const char*) { return true; }
void syncClockFromNtp(bool) {}
void startHubWebSocket() {}
bool probeOtaBridgeHealth(uint32_t) { return true; }
void confirmOtaBootIfPending(const char*) {}
void maybeSyncPomodoroMeditationAssetsOnBoot() {}
void scanEmojiAssets() {}
void loadAssetManifestSummary() {}
void loadOtaManifestSummary() {}
''' + firmware + r'''

void reset(const std::string& ssid, const std::string& password, wifi_store::List card, std::map<std::string, std::string> air) {
  gNow = 0; gLog.clear(); gChat.clear(); gSavedAsCurrent.clear(); gPortal.clear(); gSdReady = true; gMidaOn = false;
  gWifiSsid = ssid; gWifiPassword = password; gCard = card;
  WiFi = Radio{}; WiFi.air = air;
  gBootBackgroundActive = true; gBootBackgroundStep = 6; gBootWifiStarted = false; gWifiReady = false;
}
// Runs start-up's background steps until it's online or has opened the portal.
void boot() {
  while (gBootBackgroundStep == 6 || gBootBackgroundStep == 7) { serviceDeferredBoot(); gNow += 100; assert(gNow < 600000); }
}
using Dialed = std::vector<std::string>;

int main() {
  // --- The order and when to move on -------------------------------------------------------
  wifi_fallback::Dial dial;
  dial.begin("Phone", {"Phone", "Home", "Cafe", "Home", ""});
  assert(dial.queued() == 2 && dial.next() == "Home" && dial.next() == "Cafe" && !dial.another() && dial.next().empty());
  dial.begin("", {"Home", "Cafe"});
  assert(dial.next() == "Home");
  dial.begin("Elsewhere", {"Home"});
  assert(dial.another());
  // Missing with another to try: move on after two sweeps. The last one waits the full time.
  assert(!dial.giveUp(5999, true) && dial.giveUp(6000, true));
  assert(!dial.giveUp(19999, false) && dial.giveUp(20000, false));
  dial.next();
  assert(!dial.giveUp(19999, true) && dial.giveUp(20000, true));
  std::cout << "PASS fallback order: the rest of the list, newest first, each once; missing ones are left early\n";

  // --- The real start-up -------------------------------------------------------------------
  // The last used network is gone: the next saved one connects, and comes first from now on.
  reset("Phone", "phonepass", {{"Phone", "phonepass"}, {"Home", "homepass"}}, {{"Home", "homepass"}});
  boot();
  assert(gBootBackgroundStep == 8 && gPortal.empty() && gWifiSsid == "Home");
  assert((WiFi.dialed == Dialed{"Phone", "Home"}) && (gSavedAsCurrent == std::vector<std::string>{"Home"}));
  assert(gNow < 6000 + kSweepMs + 1000);
  assert(logged("start-up trying saved ssid=Home after ssid=Phone") && logged("start-up joined saved ssid=Home"));
  assert(gChat.back() == "Joined Home: Phone didn't connect" || gChat[gChat.size() - 2] == "Joined Home: Phone didn't connect");
  std::cout << "PASS start-up: the last network missing, the next saved one joins and is kept\n";

  // It's there: nothing else is dialed and nothing changes.
  reset("Home", "homepass", {{"Home", "homepass"}, {"Phone", "phonepass"}}, {{"Home", "homepass"}, {"Phone", "phonepass"}});
  boot();
  assert(gBootBackgroundStep == 8 && (WiFi.dialed == Dialed{"Home"}) && gSavedAsCurrent.empty());

  // Nothing is there: each is tried in turn, newest first, then the portal, from the first network.
  reset("Phone", "phonepass", {{"Phone", "phonepass"}, {"Home", "homepass"}, {"Cafe", "cafepass"}}, {});
  boot();
  assert(gBootBackgroundStep == 10 && gPortal == "deferred-wifi-failed");
  assert((WiFi.dialed == Dialed{"Phone", "Home", "Cafe"}) && gSavedAsCurrent.empty());
  assert(gWifiSsid == "Phone" && gWifiPassword == "phonepass");
  // Two missing ones are left after 6 seconds each, and the last gets the full 20, as before.
  assert(gNow >= 32000 && gNow < 33000);
  assert(logged("start-up tried every saved network"));
  std::cout << "PASS start-up: none connects, so every saved one is tried before the setup portal\n";

  // There but refusing, say a changed password: it gets the full wait, then the next one joins.
  reset("Home", "oldpass", {{"Home", "oldpass"}, {"Phone", "phonepass"}}, {{"Home", "newpass"}, {"Phone", "phonepass"}});
  boot();
  assert(gWifiSsid == "Phone" && (WiFi.dialed == Dialed{"Home", "Phone"}) && gNow >= 20000 && gNow < 20000 + kSweepMs + 1000);

  // Nothing set up on the device, but the card has networks: the newest is dialed first.
  reset("", "", {{"Cafe", "cafepass"}, {"Home", "homepass"}}, {{"Home", "homepass"}});
  boot();
  assert(gWifiSsid == "Home" && (WiFi.dialed == Dialed{"Cafe", "Home"}) && (gSavedAsCurrent == std::vector<std::string>{"Home"}));
  assert(gChat[gChat.size() - 2] == "Joined saved Wi-Fi Home" || gChat.back() == "Joined saved Wi-Fi Home");

  // Nothing anywhere: the portal opens at once, as before.
  reset("", "", {}, {{"Home", "homepass"}});
  boot();
  assert(gPortal == "deferred-wifi-missing" && WiFi.dialed.empty());

  // Without a card there's nothing else to try: the full wait, then the portal, as before.
  reset("Phone", "phonepass", {{"Home", "homepass"}}, {{"Home", "homepass"}});
  gSdReady = false;
  boot();
  assert(gPortal == "deferred-wifi-failed" && (WiFi.dialed == Dialed{"Phone"}) && gNow >= 20000 && gNow < 21000);

  // The network in use isn't on the card's list: every saved one is still tried.
  reset("Old", "oldpass", {{"Home", "homepass"}}, {{"Home", "homepass"}});
  boot();
  assert(gWifiSsid == "Home" && (WiFi.dialed == Dialed{"Old", "Home"}));

  // The temporary network comes after every saved one, and is never kept as the one in use.
  reset("Phone", "phonepass", {{"Phone", "phonepass"}, {"Home", "homepass"}}, {{"Lobby", ""}});
  gMidaOn = true;
  boot();
  assert((WiFi.dialed == Dialed{"Phone", "Home", "Lobby"}) && gWifiSsid == "Lobby" && gSavedAsCurrent.empty() && gPortal.empty());
  reset("Phone", "phonepass", {{"Phone", "phonepass"}, {"Home", "homepass"}}, {});
  gMidaOn = true;
  boot();
  assert((WiFi.dialed == Dialed{"Phone", "Home", "Lobby"}) && gPortal == "deferred-wifi-failed");
  std::cout << "PASS start-up: refusing, unset, cardless and unlisted networks, and the temporary one last\n";

  // The install-at-boot updater connects the same way, without the background steps.
  reset("Phone", "phonepass", {{"Phone", "phonepass"}, {"Home", "homepass"}}, {{"Home", "homepass"}});
  assert(ensureWifi() && gWifiSsid == "Home" && (WiFi.dialed == Dialed{"Phone", "Home"}) && (gSavedAsCurrent == std::vector<std::string>{"Home"}));
  reset("Phone", "phonepass", {{"Phone", "phonepass"}, {"Home", "homepass"}}, {});
  assert(!ensureWifi() && gPortal == "wifi-connect-failed" && (WiFi.dialed == Dialed{"Phone", "Home"}) && gWifiSsid == "Phone");
  reset("", "", {{"Home", "homepass"}}, {{"Home", "homepass"}});
  assert(ensureWifi() && gWifiSsid == "Home" && (WiFi.dialed == Dialed{"Home"}));
  std::cout << "PASS install-at-boot connect: the same order, then the portal\n";
}
'''

with tempfile.TemporaryDirectory(prefix='s1-wifi-fallback-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS Wi-Fi start-up fallback')
