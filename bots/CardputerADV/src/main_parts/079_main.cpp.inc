// Settings > Wi-Fi: pick a saved or a nearby network and join it from the device, or forget one.
// wifi_settings.h and wifi_store.h decide what happens. This is the radio, the SD card, the keys
// and the screen.

// The Arduino scan call only says that it failed. This keeps the driver's reason for the screen.
esp_err_t gWifiScanError = ESP_OK;
extern "C" esp_err_t __real_esp_wifi_scan_start(const wifi_scan_config_t* config, bool block);
extern "C" esp_err_t __wrap_esp_wifi_scan_start(const wifi_scan_config_t* config, bool block) {
  gWifiScanError = __real_esp_wifi_scan_start(config, block);
  return gWifiScanError;
}

namespace {
using wifi_settings::Mood;
using wifi_settings::Screen;

struct WifiSdDisk {
  bool exists(const char* path) { return SD.exists(path); }
  bool read(const char* path, std::vector<uint8_t>& out) {
    File file = SD.open(path, "r");
    if (!file) return false;
    const size_t size = file.size();
    if (size > wifi_store::kMaxFileBytes) {
      file.close();
      return false;
    }
    out.resize(size);
    const size_t got = size ? file.read(out.data(), size) : 0;
    file.close();
    return got == size;
  }
  bool write(const char* path, const std::vector<uint8_t>& bytes) {
    if (!ensureDirRecursive(SD, wifi_store::kDir)) return false;
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    const bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
    file.close();
    return ok;
  }
  bool remove(const char* path) { return SD.remove(path); }
  bool rename(const char* from, const char* to) { return SD.rename(from, to); }
};

// AES-256-GCM. The key comes from IDs burned into this chip at the factory, so it survives a
// firmware install and a full flash erase, it isn't on the card, and nothing is written to eFuse.
struct WifiCipher {
  static constexpr size_t kNonceBytes = 12, kTagBytes = 16;

  void key(uint8_t out[32]) {
    static const char kLabel[] = "s1-adv wifi store v1";
    uint8_t id[16] = {}, mac[6] = {};
    esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, id, sizeof(id) * 8);
    esp_efuse_mac_get_default(mac);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>(kLabel), sizeof(kLabel) - 1);
    mbedtls_sha256_update(&sha, id, sizeof(id));
    mbedtls_sha256_update(&sha, mac, sizeof(mac));
    mbedtls_sha256_finish(&sha, out);
    mbedtls_sha256_free(&sha);
    wifi_store::zero(id, sizeof(id));
  }
  // Nonce, then tag, then ciphertext.
  bool seal(const std::vector<uint8_t>& plain, std::vector<uint8_t>& sealed) {
    uint8_t secret[32];
    key(secret);
    sealed.assign(kNonceBytes + kTagBytes + plain.size(), 0);
    uint8_t* nonce = sealed.data();
    uint8_t* tag = nonce + kNonceBytes;
    esp_fill_random(nonce, kNonceBytes);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, secret, 256) == 0 &&
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain.size(), nonce, kNonceBytes,
                                  wifi_store::kMagic, sizeof(wifi_store::kMagic), plain.data(),
                                  tag + kTagBytes, kTagBytes, tag) == 0;
    mbedtls_gcm_free(&gcm);
    wifi_store::zero(secret, sizeof(secret));
    return ok;
  }
  bool open(const std::vector<uint8_t>& sealed, std::vector<uint8_t>& plain) {
    if (sealed.size() < kNonceBytes + kTagBytes) return false;
    uint8_t secret[32];
    key(secret);
    const uint8_t* nonce = sealed.data();
    const uint8_t* tag = nonce + kNonceBytes;
    plain.assign(sealed.size() - kNonceBytes - kTagBytes, 0);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, secret, 256) == 0 &&
        mbedtls_gcm_auth_decrypt(&gcm, plain.size(), nonce, kNonceBytes, wifi_store::kMagic,
                                 sizeof(wifi_store::kMagic), tag, kTagBytes, tag + kTagBytes,
                                 plain.data()) == 0;
    mbedtls_gcm_free(&gcm);
    wifi_store::zero(secret, sizeof(secret));
    if (!ok) wifi_store::wipe(plain);
    return ok;
  }
};

WifiSdDisk gWifiDisk;
WifiCipher gWifiCipher;
String gWifiRememberedSsid;

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
    gWifiScanError = ESP_OK;
    WiFi.scanDelete();
    return WiFi.scanNetworks(true, true) != WIFI_SCAN_FAILED;
  }
  std::string scanTrouble() {
    if (gPet.env.scanPending) return "Pet is scanning. R to try again";
    char text[40];
    if (gWifiScanError != ESP_OK) {
      snprintf(text, sizeof(text), "Scan failed %x, %uK. R to retry", static_cast<unsigned>(gWifiScanError),
               static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    } else {
      snprintf(text, sizeof(text), "Scan lost, Wi-Fi %d. R to retry", static_cast<int>(WiFi.status()));
    }
    logf("[WIFI] %s", text);
    return text;
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
  void rejoinCurrent() {
    logf("[WIFI] back to configured network");
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
  bool isCurrent(const std::string& ssid) { return gWifiSsid == ssid.c_str(); }
  bool store(const wifi_store::List& list) {
    if (!gSdReady || !wifi_store::store(gWifiDisk, gWifiCipher, list)) return false;
    gWifiRememberedSsid = gWifiSsid;
    return true;
  }
  void replaceCurrent(const wifi_store::List& list) {
    if (!list.empty()) {
      saveWifiCreds(list.front().ssid.c_str(), list.front().password.c_str());
      rejoinCurrent();
      return;
    }
    gPrefs.begin(kPrefsWifiNs, false);
    gPrefs.remove("ssid");
    gPrefs.remove("password");
    gPrefs.end();
    gWifiSsid = "";
    gWifiPassword = "";
    gWifiRememberedSsid = "";
    gWifiReady = false;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, true);  // The driver keeps its own copy of the password.
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

void renderWifiList() {
  auto& d = gCanvas;
  const uint16_t accent = rgb565_local(203, 216, 226);
  const uint16_t green = rgb565_local(88, 240, 141);
  const uint16_t light = rgb565_local(182, 219, 255);
  const uint16_t black = TFT_BLACK;
  const bool online = gWifiRadio.online();
  const int rows = static_cast<int>(gWifi.rows());
  const int selected = static_cast<int>(gWifi.selected);
  const int first = bigListFirstRow(selected, rows);
  for (int i = 0; i < kBigVisibleRows && first + i < rows; ++i) {
    const int index = first + i;
    const bool picked = index == selected;
    const int y = kBigListY + i * kBigRowH;
    if (index == static_cast<int>(gWifi.networks.size())) {
      drawBigRow(i, "Other network...", "", picked, accent);
      continue;
    }
    const auto& network = gWifi.networks[index];
    const bool current = online && gWifiSsid == network.ssid.c_str();
    useBigTextFont(network.ssid.c_str());
    drawBigRow(i, wifiFitName(network.ssid, 142), "", picked, accent);
    if (network.inRange) wifiDrawBars(216, y + 5, wifi_settings::signalBars(network.rssi), picked ? black : green, 0x4208);
    const char* label = current ? "joined" : network.saved ? "saved" : !network.secured ? "open" : "";
    d.setFont(&fonts::Font0);
    d.setTextColor(picked ? black : current ? green : light);
    d.setCursor(212 - d.textWidth(label), y + 7);
    d.print(label);
  }
  drawBigScrollbar(first, rows, accent);
  if (gWifi.networks.empty()) {
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(kBigLeft, kBigListY + kBigRowH + 8);
    d.print(gWifi.scanning() ? "Looking for networks..." : "R looks for networks");
  }
  if (!gWifi.message.empty()) drawBigNotice(String(gWifi.message.c_str()), wifiMoodColor(gWifi.mood));
}

void renderWifiField() {
  auto& d = gCanvas;
  constexpr uint16_t kPanel = 0x0841;
  const uint16_t input = rgb565_local(14, 30, 44);
  const bool forPassword = gWifi.screen == Screen::Password;

  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY, kPanel);
  d.setCursor(16, 27);
  d.print(forPassword ? "Password for" : "Network name");
  if (forPassword) {
    d.setFont(&fonts::efontCN_12);
    d.setTextColor(TFT_WHITE, kPanel);
    d.setCursor(16, 43);
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
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY, kPanel);
  d.setCursor(16, 27);
  d.print("Joining");
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_WHITE, kPanel);
  d.setCursor(16, 44);
  d.print(wifiFitName(gWifi.name, 208));

  const uint32_t elapsed = min<uint32_t>(gWifi.joinElapsed(millis()), wifi_settings::kJoinTimeoutMs);
  d.fillRoundRect(16, 62, 208, 8, 3, rgb565_local(14, 30, 44));
  d.fillRoundRect(16, 62, max<int>(8, 208 * elapsed / wifi_settings::kJoinTimeoutMs), 8, 3,
                  rgb565_local(47, 227, 255));
  drawTinyFooter(kPanel, TFT_LIGHTGREY, "Saved only if it connects", 16, 80, 208);
  drawTinyFooter(kPanel, TFT_LIGHTGREY, "Esc cancel", 16, 116, 208);
}

void renderWifiForget() {
  auto& d = gCanvas;
  constexpr uint16_t kPanel = 0x0841;
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY, kPanel);
  d.setCursor(16, 27);
  d.print("Forget this network?");
  d.setFont(&fonts::efontCN_12);
  d.setTextColor(TFT_WHITE, kPanel);
  d.setCursor(16, 44);
  d.print(wifiFitName(gWifi.name, 208));
  const bool hasMessage = !gWifi.message.empty();
  const String detail = hasMessage ? String(gWifi.message.c_str()) :
      gWifiRadio.isCurrent(gWifi.name) ? String("Deletes its password, disconnects") :
      String("Its saved password is deleted");
  drawTinyFooter(kPanel, hasMessage ? wifiMoodColor(gWifi.mood) : TFT_LIGHTGREY, detail, 16, 66, 208);
  drawTinyFooter(kPanel, TFT_LIGHTGREY, "Enter forget  Esc keep", 16, 116, 208);
}

bool wifiSettingsOwnsRadio() { return gWifi.ownsRadio(); }

// While these are up, keys are text, and they must stay out of the key log.
bool wifiSettingsTyping() { return gUiMode == UiMode::WifiSettings && gWifi.typing(); }

bool wifiSettingsTakesAllKeys() { return gUiMode == UiMode::WifiSettings && gWifi.takesAllKeys(); }

void beginWifiSettings() {
  gWifi = wifi_settings::App{};
  gWifi.rescan(millis());
  wifi_store::List saved;
  const auto loaded = gSdReady ? wifi_store::load(gWifiDisk, gWifiCipher, saved) : wifi_store::Load::Empty;
  gWifi.useSaved(saved);
  wifi_store::wipe(saved);
  if (!gSdReady) gWifi.say("No SD card, so nothing gets saved");
  else if (loaded == wifi_store::Load::Unreadable) gWifi.say("Couldn't read saved networks", Mood::Bad);
  wifiSettle();
}

// Whatever connected, the setup portal included, gets remembered once it has.
void serviceWifiMemory() {
  static uint32_t retryAt = 0;
  if (gWifiSsid.isEmpty() || gWifiRememberedSsid == gWifiSsid || !gWifiReady || !gSdReady ||
      gBootBackgroundActive || !appAllowsBackgroundNetwork() || !networkWorkIdle() ||
      usingTemporaryMidaWifi() || (retryAt != 0 && static_cast<int32_t>(millis() - retryAt) < 0) ||
      WiFi.status() != WL_CONNECTED || WiFi.SSID() != gWifiSsid) {
    return;
  }
  wifi_store::List saved;
  wifi_store::load(gWifiDisk, gWifiCipher, saved);
  const bool stored = !wifi_store::remember(saved, gWifiSsid.c_str(), gWifiPassword.c_str()) ||
      wifi_store::store(gWifiDisk, gWifiCipher, saved);
  wifi_store::wipe(saved);
  if (stored) gWifiRememberedSsid = gWifiSsid;
  else retryAt = millis() + 60000U;
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
  if (gWifi.screen == Screen::Forget) {
    if (enter || key == 'y' || key == 'Y') gWifi.confirmForget(gWifiRadio);
    else if (key == 'n' || key == 'N') gWifi.escape(gWifiRadio);
  } else if (gWifi.typing()) {
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
  } else if (key == 'f' || key == 'F') {
    gWifi.askForget();
  }
  wifiSettle();
  return true;
}

void renderWifiSettingsUi() {
  auto& d = gCanvas;
  d.setTextWrap(false);
  const bool online = gWifiRadio.online();
  const char* state = gWifi.screen == Screen::Joining ? "" : gWifi.scanning() ? "Scanning..." :
                      online ? "Connected" : "Offline";
  if (gWifi.screen == Screen::List) {
    const String here = online ? String("On ") + gWifiSsid : gWifiSsid.isEmpty() ? String("") : String("Saved: ") + gWifiSsid;
    drawBigHeader("Wi-Fi", rgb565_local(203, 216, 226), state, here, gWifi.onSavedRow() ? "F forget  R rescan" : "R rescan");
    renderWifiList();
  } else {
    drawCardHeader("Wi-Fi", 0x7BEF);
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_LIGHTGREY, 0x0841);
    d.setCursor(226 - d.textWidth(state), 17);
    d.print(state);
    if (gWifi.screen == Screen::Joining) renderWifiJoining();
    else if (gWifi.screen == Screen::Forget) renderWifiForget();
    else renderWifiField();
  }
  d.setFont(&fonts::Font2);
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
