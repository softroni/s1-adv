// TV Remote: the Apple TV by infrared, or as a Bluetooth keyboard that also types, and the TV's
// own volume, mute and power by infrared. Bluetooth runs only while this app is open, because its
// stack takes heap the radio and every TLS connection need elsewhere.
#include "../tv_ble.h"
#include "../tv_remote.h"
namespace {
enum class TvView : uint8_t { Remote, Typing };
constexpr const char* kTvPrefsNs = "tv_remote";
constexpr const char* kTvBluetoothName = "Cardputer Remote";
constexpr uint32_t kTvRepeatDelayMs = 450;   // Held this long, an arrow or the volume goes again,
constexpr uint32_t kTvRepeatEveryMs = 110;   // this often.
constexpr uint32_t kTvForgetAskMs = 4000;    // Shift+B a second time within this, to forget.
constexpr uint32_t kTvNoticeMs = 3500;
constexpr size_t kTvTypedKept = 48;          // Of what was typed, the end the screen shows.

TvView gTvView = TvView::Remote;
bool gTvLoaded = false;
bool gTvBluetoothWanted = false;  // Saved: Bluetooth starts with the app, as it was left.
// Bluetooth needs Wi-Fi's memory, so Wi-Fi is off while it's on, and back when it stops.
bool gTvWifiOff = false;
// Waiting for a request under way to finish, because it would lose its network.
bool gTvBluetoothPending = false;
tv_ble::State gTvShownState = tv_ble::State::Off;
tv_remote::Action gTvLast = tv_remote::Action::None;
bool gTvLastByBluetooth = false;
// The key being held, for its repeats. A typed arrow repeats as its keyboard usage.
char gTvHeldKey = 0;
tv_remote::Action gTvHeldAction = tv_remote::Action::None;
uint8_t gTvHeldUsage = 0;
uint32_t gTvHeldSinceMs = 0, gTvHeldLastMs = 0;
uint32_t gTvForgetAskedMs = 0;
String gTvNotice;
uint16_t gTvNoticeInk = 0;
uint32_t gTvNoticeUntilMs = 0;
// Only on the typing screen, and never logged: it can be a password.
String gTvTyped;

uint16_t tvAccent() { return rgb565_local(110, 200, 255); }
uint16_t tvGreen() { return rgb565_local(88, 240, 141); }
uint16_t tvAmber() { return rgb565_local(255, 136, 64); }
uint16_t tvRed() { return rgb565_local(255, 82, 82); }
uint16_t tvMuted() { return rgb565_local(150, 170, 190); }

void tvInvalidate() { gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
void tvSay(const String& text, uint16_t ink) {
  gTvNotice = text;
  gTvNoticeInk = ink;
  gTvNoticeUntilMs = millis() + kTvNoticeMs;
  tvInvalidate();
}
bool tvNoticeShowing() { return !gTvNotice.isEmpty() && int32_t(millis() - gTvNoticeUntilMs) < 0; }

void tvLoad() {
  if (gTvLoaded) return;
  gPrefs.begin(kTvPrefsNs, true);
  gTvBluetoothWanted = gPrefs.getBool("bt", false);
  gPrefs.end();
  gTvLoaded = true;
}
void tvSaveBluetooth(bool wanted) {
  gTvBluetoothWanted = wanted;
  gPrefs.begin(kTvPrefsNs, false);
  gPrefs.putBool("bt", wanted);
  gPrefs.end();
}

void tvWifiBack() {
  if (!gTvWifiOff) return;
  gTvWifiOff = false;
  beginWifiStationConnect("Wi-Fi back on");
}

bool tvBluetoothStart() {
  if (tv_ble::on()) return true;
  if (!networkWorkIdle()) {
    gTvBluetoothPending = true;
    tvSay("Bluetooth waits for a request to end", tvAmber());
    return false;
  }
  gTvBluetoothPending = false;
  // Everything that can go goes first: caches, the voice buffers, and Wi-Fi, which the remote
  // doesn't use. Even with all of Wi-Fi's memory in use, the stack didn't fit on 0.2.307.
  const uint32_t withWifi = ESP.getFreeHeap();
  clearEmojiCache();
  prepareNetworkMemory("TV", true);
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    gWifiReady = false;
    gTvWifiOff = true;
  }
  const uint32_t before = ESP.getFreeHeap(), largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  logHeapStats("tv-bt-before");
  const bool started = tv_ble::start(kTvBluetoothName, gBatterySnapshot.level);
  gTvShownState = tv_ble::state();
  const String heap = " with Wi-Fi=" + String(withWifi) + " before=" + String(before) + " largest=" + String(largest) +
                      " took=" + String(tv_ble::heapCost()) + " now=" + String(ESP.getFreeHeap());
  if (!started) {
    appendRuntimeLog("TV", String("Bluetooth failed: ") + tv_ble::failure() + heap, true);
    tvWifiBack();
    tvSay(tv_ble::failure(), tvRed());
    return false;
  }
  logHeapStats("tv-bt-on");
  appendRuntimeLog("TV", "Bluetooth on," + heap, true);
  return true;
}
void tvBluetoothStop() {
  gTvBluetoothPending = false;
  if (tv_ble::on() || tv_ble::state() == tv_ble::State::Failed) {
    tv_ble::stop();
    gTvShownState = tv_ble::State::Off;
    logHeapStats("tv-bt-off");
    appendRuntimeLog("TV", "Bluetooth off, free=" + String(ESP.getFreeHeap()), true);
  }
  tvWifiBack();  // Only once the stack's memory is back.
}
bool tvBluetoothReady() { return tv_ble::state() == tv_ble::State::Connected; }

bool tvSendIr(tv_remote::Action a) {
  const tv_remote::IrCode code = tv_remote::irCode(a);
  if (code.protocol == tv_remote::IrProtocol::None) return false;
  initIrRemote();
  if (code.protocol == tv_remote::IrProtocol::Nec) IrSender.sendNECRaw(code.nec, 0);
  else IrSender.sendSamsung(code.address, code.command, 0);
  return true;
}

// One press: by Bluetooth when the Apple TV is connected and has a key for it, so there's no
// need to point; otherwise, and always for the TV, by infrared.
void tvSend(tv_remote::Action a, bool repeat) {
  const tv_remote::BluetoothSend bt = tv_remote::bluetoothFor(a);
  const bool byBluetooth = tvBluetoothReady() && !tv_remote::forTv(a) && (bt.key || bt.media);
  bool sent = false;
  if (byBluetooth) sent = bt.key ? tv_ble::key(bt.key) : tv_ble::media(bt.media);
  else sent = tvSendIr(a);
  if (!sent) {
    if (!repeat) tvSay(byBluetooth ? "Bluetooth didn't send it" : "Nothing to send", tvAmber());
    return;
  }
  gTvLast = a;
  gTvLastByBluetooth = byBluetooth;
  if (!repeat) {
    flashActivityLed(80, 190, 255, 90);
    gTvNoticeUntilMs = 0;
  }
  tvInvalidate();
}

void tvToggleBluetooth() {
  if (tv_ble::on() || gTvBluetoothPending) {
    tvBluetoothStop();
    tvSaveBluetooth(false);
    if (gTvView == TvView::Typing) gTvView = TvView::Remote;
    tvSay("Bluetooth off: keys go by infrared", tvMuted());
    return;
  }
  tvSaveBluetooth(true);
  if (tvBluetoothStart()) tvSay("Wi-Fi is off while Bluetooth is on", tvGreen());
}

void tvForget() {
  if (!tv_ble::on()) {
    tvSay("Bluetooth is off: B turns it on", tvAmber());
    return;
  }
  const uint32_t now = millis();
  if (!gTvForgetAskedMs || now - gTvForgetAskedMs > kTvForgetAskMs) {
    gTvForgetAskedMs = now;
    tvSay("Shift+B again forgets the Apple TV", tvAmber());
    return;
  }
  gTvForgetAskedMs = 0;
  const bool forgot = tv_ble::forget();
  appendRuntimeLog("TV", forgot ? "Forgot paired devices" : "Forgetting paired devices failed", true);
  tvSay(forgot ? "Forgotten: pair again on the Apple TV" : "Couldn't forget it", forgot ? tvGreen() : tvRed());
}

void tvOpenTyping() {
  if (!tv_ble::on()) {
    tvSaveBluetooth(true);
    if (!tvBluetoothStart() && !gTvBluetoothPending) return;
  }
  gTvView = TvView::Typing;
  gTvTyped = "";
  gTvHeldKey = 0;
  gTvNoticeUntilMs = 0;  // The remote's last message would cover where to pair.
  tvInvalidate();
}

void tvHold(char key, tv_remote::Action action, uint8_t usage) {
  gTvHeldKey = key;
  gTvHeldAction = action;
  gTvHeldUsage = usage;
  gTvHeldSinceMs = gTvHeldLastMs = millis();
}

bool tvTypingKey(char key, const Keyboard_Class::KeysState& keys) {
  const bool enter = keys.enter || key == '\n' || key == '\r';
  const bool del = keys.del || key == '\b' || key == 127;
  if (!tvBluetoothReady()) {
    tvSay(tv_ble::state() == tv_ble::State::Failed ? tv_ble::failure() : "Not connected yet", tvAmber());
    return true;
  }
  if (const uint8_t arrow = tv_remote::typingArrow(key, keys.fn)) {
    tv_ble::key(arrow);
    if (key > 4) tvHold(key, tv_remote::Action::None, arrow);
    return true;
  }
  if (enter) {
    tv_ble::key(tv_remote::kKeyEnter);
    gTvTyped = "";
  } else if (del) {
    tv_ble::key(tv_remote::kKeyBackspace);
    if (!gTvTyped.isEmpty()) gTvTyped.remove(gTvTyped.length() - 1);
  } else {
    const tv_remote::HidKey hid = tv_remote::hidForChar(key);
    if (!hid.usage) {
      tvSay("That key can't be typed there", tvAmber());
      return true;
    }
    tv_ble::key(hid.usage, hid.modifiers);
    gTvTyped += key;
    if (gTvTyped.length() > kTvTypedKept) gTvTyped.remove(0, gTvTyped.length() - kTvTypedKept);
  }
  tvInvalidate();
  return true;
}

bool tvRemoteTakesText() { return gUiMode == UiMode::TvRemote && gTvView == TvView::Typing && !gLauncherVisible; }
// Wi-Fi is off for Bluetooth: nothing may dial it until TV Remote gives it back.
bool tvRemoteHoldsWifi() { return gTvWifiOff; }

// Typing steps back to the remote; the remote's Esc leaves the app.
bool tvRemoteEscape() {
  gTvForgetAskedMs = 0;
  gTvHeldKey = 0;
  if (gTvView != TvView::Typing) return false;
  gTvView = TvView::Remote;
  gTvTyped = "";
  tvInvalidate();
  return true;
}

void beginTvRemote() {
  tvLoad();
  gTvView = TvView::Remote;
  gTvTyped = "";
  gTvHeldKey = 0;
  gTvForgetAskedMs = 0;
  gTvNotice = "";
  gTvLast = tv_remote::Action::None;
  initIrRemote();
  if (gTvBluetoothWanted) tvBluetoothStart();
  tvInvalidate();
}

void stopTvRemote() {
  gTvView = TvView::Remote;
  gTvTyped = "";
  gTvHeldKey = 0;
  tvBluetoothStop();
}

void serviceTvRemote() {
  if (gUiMode != UiMode::TvRemote) return;
  if (gTvBluetoothPending && networkWorkIdle() && tvBluetoothStart()) tvSay("Wi-Fi is off while Bluetooth is on", tvGreen());
  const tv_ble::State now = tv_ble::state();
  if (now != gTvShownState) {
    if (now == tv_ble::State::Connected) appendRuntimeLog("TV", "Bluetooth connected", true);
    else if (gTvShownState == tv_ble::State::Connected) appendRuntimeLog("TV", "Bluetooth disconnected", true);
    gTvShownState = now;
    tvInvalidate();
  }
  if (gTvNoticeUntilMs && !tvNoticeShowing()) {
    gTvNoticeUntilMs = 0;
    tvInvalidate();
  }
  if (!gTvHeldKey) return;
  const auto& word = gKeyboardState.word;
  if (gLauncherVisible || word.size() != 1 || word[0] != gTvHeldKey) {
    gTvHeldKey = 0;
    return;
  }
  const uint32_t ms = millis();
  if (ms - gTvHeldSinceMs < kTvRepeatDelayMs || ms - gTvHeldLastMs < kTvRepeatEveryMs) return;
  gTvHeldLastMs = ms;
  if (gTvHeldUsage) tv_ble::key(gTvHeldUsage);
  else tvSend(gTvHeldAction, true);
}

bool handleTvRemoteKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt) return false;  // Ctrl+L, Ctrl+V and the rest still work here.
  if (gTvView == TvView::Typing) return tvTypingKey(key, keys);
  using tv_remote::Action;
  const Action a = tv_remote::actionForKey(key, keys.enter, keys.del);
  if (a != Action::Forget) gTvForgetAskedMs = 0;
  switch (a) {
    case Action::None:
      tvSay("H home, T type, B Bluetooth", tvMuted());
      return true;
    case Action::Bluetooth: tvToggleBluetooth(); return true;
    case Action::Typing: tvOpenTyping(); return true;
    case Action::Forget: tvForget(); return true;
    default: break;
  }
  tvSend(a, false);
  if (tv_remote::repeats(a) && key > 4) tvHold(key, a, 0);
  return true;
}

// --- Drawing ------------------------------------------------------------------------------
String tvLinkLabel(uint16_t& ink) {
  switch (tv_ble::state()) {
    case tv_ble::State::Connected: ink = tvGreen(); return "Bluetooth";
    case tv_ble::State::Waiting: ink = tvAmber(); return "BT waiting";
    case tv_ble::State::Failed: ink = tvRed(); return "BT failed";
    default:
      if (gTvBluetoothPending) { ink = tvAmber(); return "BT starting"; }
      ink = tvMuted(); return "Infrared";
  }
}

void tvHeader(const char* title) {
  uint16_t ink = 0;
  const String link = tvLinkLabel(ink);
  drawBigHeader(title, tvAccent());
  auto& d = gCanvas;
  d.setFont(&fonts::Font2);
  d.setTextColor(ink);
  d.setCursor(kBigRight - d.textWidth(link), 3);
  d.print(link);
}

// The Apple TV's ring: the four arrow keys around OK.
void tvDrawPad(int16_t cx, int16_t cy) {
  auto& d = gCanvas;
  const uint16_t accent = tvAccent(), ring = rgb565_local(60, 80, 110);
  d.drawCircle(cx, cy, 36, ring);
  d.drawCircle(cx, cy, 35, ring);
  d.fillTriangle(cx, cy - 33, cx - 5, cy - 27, cx + 5, cy - 27, accent);
  d.fillTriangle(cx, cy + 33, cx - 5, cy + 27, cx + 5, cy + 27, accent);
  d.fillTriangle(cx - 33, cy, cx - 27, cy - 5, cx - 27, cy + 5, accent);
  d.fillTriangle(cx + 33, cy, cx + 27, cy - 5, cx + 27, cy + 5, accent);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_WHITE);
  d.setCursor(cx - 2, cy - 24);
  d.print(';');
  d.setCursor(cx - 2, cy + 17);
  d.print('.');
  d.setCursor(cx - 25, cy - 3);
  d.print(',');
  d.setCursor(cx + 20, cy - 3);
  d.print('/');
  d.fillCircle(cx, cy, 14, accent);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_BLACK);
  d.setCursor(cx - d.textWidth("OK") / 2, cy - 7);
  d.print("OK");
}

// A row of one or two keys with what they do, the key in the accent colour.
void tvKeyRow(int16_t x, int16_t y, const char* key, const char* label, const char* key2 = nullptr,
              const char* label2 = nullptr) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font2);
  int16_t at = x;
  const auto one = [&](const char* k, const char* l) {
    d.setTextColor(tvAccent());
    d.setCursor(at, y);
    d.print(k);
    at += d.textWidth(k) + 5;
    d.setTextColor(TFT_WHITE);
    d.setCursor(at, y);
    d.print(l);
    at += d.textWidth(l) + 12;
  };
  one(key, label);
  if (key2) one(key2, label2);
}

// Where the Apple TV pairs, for as long as nothing has paired yet.
void tvPairingSteps(int16_t x, int16_t y) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font2);
  d.setTextColor(tvAmber());
  d.setCursor(x, y);
  d.print("Pair on the Apple TV:");
  d.setTextColor(TFT_WHITE);
  const char* steps[] = {"Settings >", "Remotes and Devices >", "Bluetooth >", kTvBluetoothName};
  for (int i = 0; i < 4; ++i) {
    d.setCursor(x, y + 17 * (i + 1));
    d.print(steps[i]);
  }
}

String tvLastLine(uint16_t& ink) {
  if (tvNoticeShowing()) {
    ink = gTvNoticeInk;
    return gTvNotice;
  }
  ink = tvMuted();
  if (gTvLast == tv_remote::Action::None) {
    return tvBluetoothReady() ? "Connected: no need to point it" : "Point it at the Apple TV";
  }
  const char* name = tv_remote::actionName(gTvLast);
  if (tv_remote::forTv(gTvLast)) return String(name) + " to the TV";
  return String(name) + (gTvLastByBluetooth ? " by Bluetooth" : " by infrared");
}

void renderTvRemote() {
  auto& d = gCanvas;
  tvHeader("TV Remote");
  tvDrawPad(46, 64);
  d.setFont(&fonts::Font0);
  d.setTextColor(tvAccent());
  d.setCursor(8, 106);
  d.print("Del");
  d.setCursor(8, 116);
  d.print("Space");
  d.setTextColor(TFT_WHITE);
  d.setCursor(44, 106);
  d.print("back");
  d.setCursor(44, 116);
  d.print("play");
  const bool pairing = tv_ble::state() == tv_ble::State::Waiting && !tv_ble::paired();
  if (pairing) {
    tvPairingSteps(92, 26);
  } else {
    tvKeyRow(92, 27, "H", "Home", "Z", "Sleep");
    tvKeyRow(92, 44, "= -", "TV volume");
    tvKeyRow(92, 61, "M", "Mute", "P", "Power");
    tvKeyRow(92, 78, "T", "Type");
    tvKeyRow(92, 95, "B", tv_ble::on() ? "Bluetooth off" : "Bluetooth");
  }
  uint16_t ink = 0;
  const String line = tvLastLine(ink);
  d.setFont(&fonts::Font0);
  d.setTextColor(ink);
  d.setCursor(8, 126);
  d.print(fitCurrentFontToWidth(line, 224));
  d.setFont(&fonts::Font2);
}

void renderTvTyping() {
  auto& d = gCanvas;
  const uint16_t accent = tvAccent();
  tvHeader("Type");
  d.setFont(&fonts::Font2);
  d.drawRoundRect(8, 28, 222, 22, 5, accent);
  String shown = gTvTyped + "_";
  while (shown.length() > 1 && d.textWidth(shown) > 210) shown.remove(0, 1);  // The end, where typing is.
  d.setTextColor(TFT_WHITE);
  d.setCursor(14, 31);
  d.print(shown);
  const tv_ble::State state = tv_ble::state();
  if (tvNoticeShowing()) {
    d.setTextColor(gTvNoticeInk);
    d.setCursor(8, 56);
    d.print(fitCurrentFontToWidth(gTvNotice, 222));
  } else if (state == tv_ble::State::Connected) {
    d.setTextColor(tvGreen());
    d.setCursor(8, 56);
    d.print("Keys type on the Apple TV");
  } else if (state == tv_ble::State::Failed) {
    d.setTextColor(tvRed());
    d.setCursor(8, 56);
    d.print(fitCurrentFontToWidth(tv_ble::failure(), 222));
  } else if (gTvBluetoothPending) {
    d.setTextColor(tvAmber());
    d.setCursor(8, 56);
    d.print("Bluetooth starts when the");
    d.setCursor(8, 73);
    d.print("network is free...");
  } else if (!tv_ble::paired()) {
    d.setTextColor(tvAmber());
    d.setCursor(8, 56);
    d.print("Pair first: Settings > Remotes");
    d.setCursor(8, 73);
    d.print("and Devices > Bluetooth >");
    d.setCursor(8, 90);
    d.print(kTvBluetoothName);
  } else {
    d.setTextColor(tvAmber());
    d.setCursor(8, 56);
    d.print("Waiting for the Apple TV...");
  }
  d.setFont(&fonts::Font0);
  d.setTextColor(tvMuted());
  d.setCursor(8, 112);
  d.print("Enter sends  Del erases");
  d.setCursor(8, 124);
  d.print("Fn+; . , / move  Esc back");
  d.setFont(&fonts::Font2);
}

void renderTvRemoteUi() {
  if (gTvView == TvView::Typing) renderTvTyping();
  else renderTvRemote();
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
