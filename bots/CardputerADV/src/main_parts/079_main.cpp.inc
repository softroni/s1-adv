// Settings > Wi-Fi: pick or type a network and join it from the device. wifi_settings.h decides
// what happens. This is the radio, the keys and the screen.
namespace {
using wifi_settings::Mood;
using wifi_settings::Screen;

void stopWifiScan() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) esp_wifi_scan_stop();
  WiFi.scanDelete();
}

// Leaves the setup AP alone: a failed join on that screen must still leave the portal working.
struct WifiRadio {
  bool online() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }
  bool busy() { return !networkWorkIdle(); }
  void hold() {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    gWifiReady = false;
  }
  bool startScan() {
    if (gPet.env.scanPending) return false;  // The pet would take these results for its own.
    WiFi.scanDelete();
    return WiFi.scanNetworks(true, true) != WIFI_SCAN_FAILED;
  }
  int scanResults(std::vector<wifi_settings::Network>& out) {
    const int count = WiFi.scanComplete();
    if (count < 0) return count;
    for (int i = 0; i < count; ++i) {
      out.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
    WiFi.scanDelete();
    return count;
  }
  void stopScan() { stopWifiScan(); }
  void join(const std::string& ssid, const std::string& password) {
    logf("[WIFI] switch start ssid=%s", ssid.c_str());
    gWifiReady = false;
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    delay(80);
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  bool joined(const std::string& ssid) { return online() && WiFi.SSID() == ssid.c_str(); }
  bool notFound() { return WiFi.status() == WL_NO_SSID_AVAIL; }
  void rejoinSaved() {
    logf("[WIFI] back to saved network");
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    delay(80);
    gLastWifiReconnectMs = millis();
    if (!gWifiSsid.isEmpty()) WiFi.begin(gWifiSsid.c_str(), gWifiPassword.c_str());
  }
  void save(const std::string& ssid, const std::string& password) {
    saveWifiCreds(ssid.c_str(), password.c_str());
    logf("[WIFI] switched ssid=%s ip=%s", ssid.c_str(), WiFi.localIP().toString().c_str());
    // Boot skipped its bridge steps when it fell back to the portal. Restarting runs them.
    if (gSetupPortalActive) {
      gSetupPortalSaved = true;
      setStatus("Wi-Fi saved. Restarting", 1500);
      gPendingRestart = true;
    }
  }
};

WifiRadio gWifiRadio;
wifi_settings::App gWifi;

void wifiSettle() {
  if (!gWifi.changed) return;
  gWifi.changed = false;
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}

// Head of a name, cut on a character boundary because a scanned name can be any UTF-8.
String wifiFitName(const std::string& text, int32_t limit) {
  String out(text.c_str());
  if (gCanvas.textWidth(out) <= limit) return out;
  while (out.length() > 1 && gCanvas.textWidth(out + "..") > limit) removeLastUtf8Char(out);
  return out + "..";
}

uint16_t wifiMoodColor(Mood mood) {
  return mood == Mood::Good ? rgb565_local(88, 240, 141) :
         mood == Mood::Bad ? rgb565_local(255, 110, 92) : TFT_LIGHTGREY;
}

void wifiDrawBars(int x, int y, uint8_t level, uint16_t lit, uint16_t dim) {
  for (int i = 0; i < 4; ++i) {
    const int height = 2 + i * 2;
    gCanvas.fillRect(x + i * 3, y + 8 - height, 2, height, i < level ? lit : dim);
  }
}

void renderWifiList(bool online) {
  auto& d = gCanvas;
  constexpr uint16_t kPanel = 0x0841;
  constexpr int kRowTop = 43, kRowHeight = 13, kVisible = 5;
  const uint16_t green = rgb565_local(88, 240, 141);

  d.setFont(&fonts::efontCN_12);
  d.setTextColor(online ? green : TFT_LIGHTGREY, kPanel);
  d.setCursor(16, 29);
  if (online) d.print(wifiFitName(std::string("On ") + gWifiSsid.c_str(), 208));
  else if (gWifiSsid.isEmpty()) d.print("No network saved");
  else d.print(wifiFitName(std::string("Saved: ") + gWifiSsid.c_str(), 208));

  const int rows = static_cast<int>(gWifi.rows());
  const int selected = static_cast<int>(gWifi.selected);
  const int start = max<int>(0, min<int>(selected - kVisible / 2, rows - kVisible));
  for (int i = 0; i < kVisible && start + i < rows; ++i) {
    const int index = start + i;
    const int y = kRowTop + i * kRowHeight;
    const bool picked = index == selected;
    const uint16_t bg = picked ? 0x4208 : 0x0841;
    d.fillRoundRect(14, y - 1, 212, kRowHeight - 1, 3, bg);
    d.setFont(&fonts::efontCN_12);
    d.setTextColor(picked ? TFT_WHITE : TFT_LIGHTGREY, bg);
    d.setCursor(18, y - 1);
    if (index == static_cast<int>(gWifi.networks.size())) {
      d.print("Other network...");
      continue;
    }
    const auto& network = gWifi.networks[index];
    const bool current = online && gWifiSsid == network.ssid.c_str();
    d.print(wifiFitName(network.ssid, 150));
    wifiDrawBars(209, y + 1, wifi_settings::signalBars(network.rssi), picked ? TFT_WHITE : green, 0x4208);
    if (current || !network.secured) {
      d.setFont(&fonts::Font0);
      d.setTextColor(current ? green : TFT_LIGHTGREY, bg);
      d.setCursor(current ? 172 : 180, y + 1);
      d.print(current ? "joined" : "open");
    }
  }
  if (gWifi.networks.empty()) {
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_LIGHTGREY, kPanel);
    d.setCursor(18, kRowTop + kRowHeight + 2);
    d.print(gWifi.scanning() ? "Looking for networks..." : "R looks for networks");
  }
  const bool hasMessage = !gWifi.message.empty();
  drawTinyFooter(kPanel, hasMessage ? wifiMoodColor(gWifi.mood) : TFT_LIGHTGREY,
                 hasMessage ? String(gWifi.message.c_str()) : String("^v pick  Enter join  R rescan  Esc"),
                 16, 116, 208);
}

void renderWifiField() {
  auto& d = gCanvas;
  constexpr uint16_t kPanel = 0x0841;
  const uint16_t input = rgb565_local(14, 30, 44);
  const bool forPassword = gWifi.screen == Screen::Password;

  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_LIGHTGREY, kPanel);
  d.setCursor(16, 31);
  d.print(forPassword ? "Password for" : "Network name");
  if (forPassword) {
    d.setFont(&fonts::efontCN_12);
    d.setTextColor(TFT_WHITE, kPanel);
    d.setCursor(16, 41);
    d.print(wifiFitName(gWifi.name, 208));
  }

  d.fillRoundRect(14, 57, 212, 22, 5, input);
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_WHITE, input);
  d.setCursor(20, 62);
  const std::string shown = !forPassword ? gWifi.name :
      gWifi.revealed ? gWifi.password : wifi_settings::masked(gWifi.password);
  d.print(tailUtf8ToFit(String(shown.c_str()) + "_", 198));

  const bool hasMessage = !gWifi.message.empty();
  const String hint = !forPassword ? String("Capitals count. 2.4 GHz only.") :
      gWifi.secured ? String("At least 8 characters") : String("Leave blank for an open network");
  drawTinyFooter(kPanel, hasMessage ? wifiMoodColor(gWifi.mood) : TFT_LIGHTGREY,
                 hasMessage ? String(gWifi.message.c_str()) : hint, 16, 86, 208);
  drawTinyFooter(kPanel, TFT_LIGHTGREY,
                 forPassword ? "Enter join  Fn+; show  Fn+` back" : "Enter next  Fn+` back", 16, 116, 208);
}

void renderWifiJoining() {
  auto& d = gCanvas;
  constexpr uint16_t kPanel = 0x0841;
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_LIGHTGREY, kPanel);
  d.setCursor(16, 31);
  d.print("Joining");
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_WHITE, kPanel);
  d.setCursor(16, 41);
  d.print(wifiFitName(gWifi.name, 208));

  const uint32_t elapsed = min<uint32_t>(gWifi.joinElapsed(millis()), wifi_settings::kJoinTimeoutMs);
  d.fillRoundRect(16, 62, 208, 8, 3, rgb565_local(14, 30, 44));
  d.fillRoundRect(16, 62, max<int>(8, 208 * elapsed / wifi_settings::kJoinTimeoutMs), 8, 3,
                  rgb565_local(47, 227, 255));
  drawTinyFooter(kPanel, TFT_LIGHTGREY, "Saved only if it connects", 16, 80, 208);
  drawTinyFooter(kPanel, TFT_LIGHTGREY, "Esc cancel", 16, 116, 208);
}

bool wifiSettingsOwnsRadio() { return gWifi.ownsRadio(); }

// While these are up, keys are text, and they must stay out of the key log.
bool wifiSettingsTyping() { return gUiMode == UiMode::WifiSettings && gWifi.typing(); }

bool wifiSettingsTakesAllKeys() {
  return gUiMode == UiMode::WifiSettings && (gWifi.typing() || gWifi.screen == Screen::Joining);
}

void beginWifiSettings() {
  gWifi = wifi_settings::App{};
  gWifi.rescan(millis());
  wifiSettle();
}

void stopWifiSettings() {
  gWifi.close(gWifiRadio);
}

void serviceWifiSettings() {
  if (gUiMode != UiMode::WifiSettings) return;
  gWifi.poll(gWifiRadio, millis());
  wifiSettle();
}

bool wifiSettingsEscape() {
  const bool handled = gWifi.escape(gWifiRadio);
  wifiSettle();
  return handled;
}

bool handleWifiSettingsKey(char key, const Keyboard_Class::KeysState& keys) {
  const bool enter = keys.enter || key == '\r' || key == '\n';
  if (gWifi.screen == Screen::Joining || keys.ctrl || keys.alt || keys.opt) return true;
  if (gWifi.typing()) {
    if (keys.fn) {
      if (key == ';' || key == '.') gWifi.toggleReveal();
    } else if (enter) {
      gWifi.enter(gWifiRadio, millis());
    } else if (keys.del || key == '\b') {
      gWifi.backspace();
    } else {
      gWifi.type(key);
    }
  } else if (key == ';' || key == ',' || key == char(1)) {
    gWifi.move(-1);
  } else if (key == '.' || key == '/' || key == char(2)) {
    gWifi.move(1);
  } else if (enter) {
    gWifi.enter(gWifiRadio, millis());
  } else if (key == 'r' || key == 'R') {
    gWifi.rescan(millis());
  }
  wifiSettle();
  return true;
}

void renderWifiSettingsUi() {
  auto& d = gCanvas;
  drawCardHeader("Wi-Fi", 0x7BEF);
  d.setTextWrap(false);
  const bool online = gWifiRadio.online();
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_LIGHTGREY, 0x0841);
  const char* state = gWifi.screen == Screen::Joining ? "" : gWifi.scanning() ? "Scanning..." :
                      online ? "Connected" : "Offline";
  d.setCursor(226 - d.textWidth(state), 17);
  d.print(state);
  if (gWifi.screen == Screen::Joining) renderWifiJoining();
  else if (gWifi.typing()) renderWifiField();
  else renderWifiList(online);
  d.setFont(&fonts::Font2);
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
