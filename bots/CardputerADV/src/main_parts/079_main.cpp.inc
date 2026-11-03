// Settings > Wi-Fi: pick a saved or a nearby network and join it from the device, or forget one.
// wifi_settings.h and wifi_store.h decide what happens. This is the radio, the SD card and the
// keys. The screens are in 086, after the Lab, whose look they share.

// The Arduino scan call only says that it failed. This keeps the driver's reason for the screen.
esp_err_t gWifiScanError = ESP_OK;

// The driver answers every scan it starts with one done event: when the scan ends, when it's
// stopped, and when a newer scan replaces it. Arduino stops waiting after 6 s and calls the scan
// failed, but a scan made while joined takes over 8 s here, because the radio keeps going back to
// its own channel. The screen then started again, which replaced the scan in flight, and that
// scan's empty answer read as the new one's: "No networks found". So a scan is over when the
// driver has answered every start, counted from the first time the events are watched.
// Each count has one writer: starts on the loop, done events on Arduino's event task.
volatile uint32_t gWifiScansStarted = 0, gWifiScanDoneCount = 0, gWifiLeftCount = 0;
volatile uint8_t gWifiScanDoneStatus = 0xFF, gWifiScanDoneHeard = 0, gWifiLeftReason = 0;
volatile bool gWifiWatching = false;
uint32_t gWifiScanStartedMs = 0;
// Longer than the screen's own scan limit, which stops the scan and so brings its answer.
constexpr uint32_t kWifiScanAnswerMs = 20000;

bool wifiScanInFlight() {
  if (gWifiScansStarted == gWifiScanDoneCount) return false;
  // Turning Wi-Fi off mid-scan can lose the answer. Waiting for it would block every later scan.
  if (millis() - gWifiScanStartedMs > kWifiScanAnswerMs) {
    gWifiScansStarted = gWifiScanDoneCount;
    return false;
  }
  return true;
}

extern "C" esp_err_t __real_esp_wifi_scan_start(const wifi_scan_config_t* config, bool block);
extern "C" esp_err_t __wrap_esp_wifi_scan_start(const wifi_scan_config_t* config, bool block) {
  gWifiScanError = __real_esp_wifi_scan_start(config, block);
  if (gWifiScanError == ESP_OK && gWifiWatching) {
    gWifiScansStarted = gWifiScansStarted + 1;
    gWifiScanStartedMs = millis();
  }
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

// Arduino may already have given up on a scan the driver is still running, so ask the driver too.
void stopWifiScan() {
  if (wifiScanInFlight() || WiFi.scanComplete() == WIFI_SCAN_RUNNING) esp_wifi_scan_stop();
  WiFi.scanDelete();
}

// Arduino stores a scan's results before it calls this. The event task only stores numbers here:
// it runs on a small stack, so the logging happens on the loop.
void noteWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
    gWifiScanDoneStatus = static_cast<uint8_t>(info.wifi_scan_done.status);
    gWifiScanDoneHeard = info.wifi_scan_done.number;
    gWifiScanDoneCount = gWifiScanDoneCount + 1;
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    gWifiLeftReason = info.wifi_sta_disconnected.reason;
    gWifiLeftCount = gWifiLeftCount + 1;
  }
}

// Called from the loop before a scan starts, so no scan is already in flight uncounted.
void watchWifiEvents() {
  if (gWifiWatching) return;
  WiFi.onEvent(noteWifiEvent, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(noteWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  gWifiWatching = true;
}

// A name as the radio has it. Bytes outside plain ASCII, like an iPhone's curly apostrophe, show
// as \xNN, so a typed name can be compared with a heard one.
String wifiLoggedName(const char* name) {
  String out;
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(name); *c; ++c) {
    if (*c >= 32 && *c < 127 && *c != '\\') {
      out += static_cast<char>(*c);
    } else {
      char hex[5];
      snprintf(hex, sizeof(hex), "\\x%02X", *c);
      out += hex;
    }
  }
  return out;
}

void logWifiScanDriver(const char* what, int count) {
  logf("[WIFI] %s at=%u count=%d err=%x status=%d scans=%u/%u heard=%u scan_status=%u left=%u reason=%u heap=%u",
       what, static_cast<unsigned>(millis()), count, static_cast<unsigned>(gWifiScanError),
       static_cast<int>(WiFi.status()), static_cast<unsigned>(gWifiScanDoneCount),
       static_cast<unsigned>(gWifiScansStarted), gWifiScanDoneHeard, gWifiScanDoneStatus,
       static_cast<unsigned>(gWifiLeftCount), gWifiLeftReason, static_cast<unsigned>(ESP.getFreeHeap()));
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
    watchWifiEvents();
    // An unanswered scan's answer would read as this one's. Stopping it brings the answer.
    if (wifiScanInFlight()) {
      esp_wifi_scan_stop();
      logWifiScanDriver("scan start waits for the last scan", WIFI_SCAN_RUNNING);
      return false;
    }
    gWifiScanError = ESP_OK;
    WiFi.scanDelete();
    const int16_t started = WiFi.scanNetworks(true, true);
    logWifiScanDriver("scan start", started);
    return started != WIFI_SCAN_FAILED;
  }
  std::string scanTrouble() {
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
    // Not Arduino's 6 s guess: the screen's own limit covers a driver that never answers.
    if (wifiScanInFlight()) return wifi_settings::kScanRunning;
    const int count = WiFi.scanComplete();
    if (count < 0) {
      logWifiScanDriver("scan results", count);
      return count;
    }
    int hidden = 0;
    for (int i = 0; i < count; ++i) {
      out.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
      if (out.back().ssid.empty()) ++hidden;
    }
    logWifiScanDriver("scan results", count);
    logf("[WIFI] scan hidden=%d", hidden);
    WiFi.scanDelete();
    return count;
  }
  void stopScan() { stopWifiScan(); }
  void join(const std::string& ssid, const std::string& password) {
    logf("[WIFI] switch start ssid=%s", wifiLoggedName(ssid.c_str()).c_str());
    gWifiReady = false;
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    delay(80);
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  bool joined(const std::string& ssid) { return online() && WiFi.SSID() == ssid.c_str(); }
  // Polled while joining, so each change of the station's state reaches the log once.
  bool notFound() {
    static int logged = -1;
    const wl_status_t status = WiFi.status();
    if (static_cast<int>(status) != logged) {
      logged = static_cast<int>(status);
      logWifiScanDriver("join state", 0);
    }
    return status == WL_NO_SSID_AVAIL;
  }
  void rejoinCurrent() {
    logWifiScanDriver("back to configured network", 0);
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

bool wifiSettingsOwnsRadio() { return gWifi.ownsRadio(); }

// While these are up, keys are text, and they must stay out of the key log.
bool wifiSettingsTyping() { return gUiMode == UiMode::WifiSettings && gWifi.typing(); }

bool wifiSettingsTakesAllKeys() { return gUiMode == UiMode::WifiSettings && gWifi.takesAllKeys(); }

void beginWifiSettings() {
  watchWifiEvents();
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

// Serial wifi.rescan: R on the Wi-Fi screen.
void rescanWifiSettings() {
  if (gUiMode != UiMode::WifiSettings) {
    logf("[WIFI] rescan needs the Wi-Fi screen. Send wifi.open first");
    return;
  }
  gWifi.rescan(millis());
  wifiSettle();
}

// Serial wifi.scan.raw: one plain blocking scan with every network heard, the screen's filtering
// left out, and the radio's channel rules.
void runWifiRawScan() {
  watchWifiEvents();
  if (gUiMode == UiMode::WifiSettings && (gWifi.scanning() || gWifi.ownsRadio())) {
    logf("[WIFI] raw scan skipped: the Wi-Fi screen is using the radio");
    return;
  }
  wifi_country_t country = {};
  esp_wifi_get_country(&country);
  logf("[WIFI] raw scan country=%.2s first=%u channels=%u policy=%d mode=%d home=%d", country.cc,
       country.schan, country.nchan, static_cast<int>(country.policy), static_cast<int>(WiFi.getMode()),
       static_cast<int>(WiFi.channel()));
  gWifiScanError = ESP_OK;
  const uint32_t started = millis();
  const int16_t count = WiFi.scanNetworks(false, true);
  logWifiScanDriver("raw scan", count);
  logf("[WIFI] raw scan ms=%u", static_cast<unsigned>(millis() - started));
  for (int i = 0; i < count; ++i) {
    Serial.printf("[WIFI] raw %2d ch=%2d rssi=%4d auth=%d name=%s\n", i, static_cast<int>(WiFi.channel(i)),
                  static_cast<int>(WiFi.RSSI(i)), static_cast<int>(WiFi.encryptionType(i)),
                  wifiLoggedName(WiFi.SSID(i).c_str()).c_str());
  }
  WiFi.scanDelete();
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

}  // namespace
