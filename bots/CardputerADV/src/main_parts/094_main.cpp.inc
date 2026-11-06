// TV Remote: a page for each device, the Apple TV, a Bose soundbar, an Epson projector and a
// Samsung TV, all by infrared, and the page last used comes back. With Bluetooth built in, the
// Apple TV also pairs with it as a keyboard that types; it runs only while this app is open,
// because its stack takes heap the radio and every TLS connection need elsewhere.
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
// Saved: the page on screen, and which of the Bose's two code sets works.
tv_remote::Device gTvPage = tv_remote::Device::AppleTv;
uint8_t gTvBoseSet = 0;
tv_remote::Press gTvLast = tv_remote::kNothing;
bool gTvLastByBluetooth = false;
// The button or arrow just sent, lit on screen until this time.
constexpr uint32_t kTvFlashMs = 180;
tv_remote::Press gTvFlash = tv_remote::kNothing;
uint32_t gTvFlashUntilMs = 0;
// The key being held, for its repeats. A typed arrow repeats as its keyboard usage.
char gTvHeldKey = 0;
tv_remote::Press gTvHeldAction = tv_remote::kNothing;
uint8_t gTvHeldUsage = 0;
uint32_t gTvHeldSinceMs = 0, gTvHeldLastMs = 0;
uint32_t gTvForgetAskedMs = 0;
String gTvNotice;
uint16_t gTvNoticeInk = 0;
uint32_t gTvNoticeUntilMs = 0;
// Only on the typing screen, and never logged: it can be a password.
String gTvTyped;

// Each device has its colour: its tab, its buttons when they light, its ring.
uint16_t tvAccentOf(tv_remote::Device d) {
  switch (d) {
    case tv_remote::Device::Bose: return rgb565_local(255, 182, 85);
    case tv_remote::Device::Epson: return rgb565_local(146, 219, 85);
    case tv_remote::Device::Samsung: return rgb565_local(182, 146, 255);
    default: return rgb565_local(110, 200, 255);
  }
}
uint16_t tvAccent() { return tvAccentOf(gTvPage); }
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
  const uint8_t page = gPrefs.getUChar("page", 0);
  const uint8_t set = gPrefs.getUChar("bose", 0);
  gPrefs.end();
  gTvPage = page < tv_remote::kDeviceCount ? tv_remote::Device(page) : tv_remote::Device::AppleTv;
  gTvBoseSet = set < tv_remote::kBoseCodeSets ? set : 0;
  gTvLoaded = true;
}
void tvSaveByte(const char* key, uint8_t value) {
  gPrefs.begin(kTvPrefsNs, false);
  gPrefs.putUChar(key, value);
  gPrefs.end();
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
  if (!tv_ble::available()) {
    tvSay(tv_ble::failure(), tvMuted());
    return false;
  }
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

bool tvSendIr(tv_remote::Press p) {
  const tv_remote::IrCode code = tv_remote::irCode(p, gTvBoseSet);
  if (code.protocol == tv_remote::IrProtocol::None) return false;
  initIrRemote();
  if (code.protocol == tv_remote::IrProtocol::Nec) {
    IrSender.sendNECRaw(code.nec, 0);
  } else if (code.protocol == tv_remote::IrProtocol::Samsung) {
    IrSender.sendSamsung(code.address, code.command, 0);
  } else {
    uint16_t raw[tv_remote::kBoseRaw];
    const size_t n = tv_remote::boseRaw(code.command, raw);
    IrSender.sendRaw(raw, n, 38);
  }
  return true;
}

// One press: by Bluetooth when the Apple TV is connected and has a key for it, so there's no
// need to point; otherwise, and always for the other devices, by infrared.
void tvSend(tv_remote::Press a, bool repeat) {
  const tv_remote::BluetoothSend bt = tv_remote::bluetoothFor(a);
  const bool byBluetooth = tvBluetoothReady() && (bt.key || bt.media);
  bool sent = false;
  if (byBluetooth) sent = bt.key ? tv_ble::key(bt.key) : tv_ble::media(bt.media);
  else sent = tvSendIr(a);
  if (!sent) {
    if (!repeat) tvSay(byBluetooth ? "Bluetooth didn't send it" : "Nothing to send", tvAmber());
    return;
  }
  gTvLast = a;
  gTvLastByBluetooth = byBluetooth;
  gTvFlash = a;
  gTvFlashUntilMs = millis() + kTvFlashMs;
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

void tvHold(char key, tv_remote::Press action, uint8_t usage) {
  gTvHeldKey = key;
  gTvHeldAction = action;
  gTvHeldUsage = usage;
  gTvHeldSinceMs = gTvHeldLastMs = millis();
}

void tvShowPage(tv_remote::Device page) {
  if (page == gTvPage) return;
  gTvPage = page;
  gTvHeldKey = 0;
  gTvLast = tv_remote::kNothing;
  gTvNoticeUntilMs = 0;
  tvSaveByte("page", uint8_t(page));
  tvInvalidate();
}

// What the page's keys are, for a key that does nothing there. Each fits the bottom line.
const char* tvPageHint() {
  switch (gTvPage) {
    case tv_remote::Device::Bose: return "= - volume, P power, C other codes";
    case tv_remote::Device::Epson: return "P power, I O HDMI 1 2, S source";
    case tv_remote::Device::Samsung: return "H home, S source, E exit, = - volume";
    default: return tv_ble::available() ? "H home, T type, B Bluetooth" : "1-4 change remote, = - Bose volume";
  }
}

// The two Bose remotes' formats, for whichever the soundbar answers. Kept, once it's found.
const char* tvBoseCodesName() { return gTvBoseSet == 0 ? "600" : "300"; }
void tvNextBoseCodes() {
  gTvBoseSet = uint8_t((gTvBoseSet + 1) % tv_remote::kBoseCodeSets);
  tvSaveByte("bose", gTvBoseSet);
  tvSay(String("Bose codes: Soundbar ") + tvBoseCodesName() + "'s remote", tvAccentOf(tv_remote::Device::Bose));
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
    if (key > 4) tvHold(key, tv_remote::kNothing, arrow);
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
  gTvLast = tv_remote::kNothing;
  initIrRemote();
  if (gTvBluetoothWanted && tv_ble::available()) tvBluetoothStart();
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
  if (gTvFlash != tv_remote::kNothing && int32_t(millis() - gTvFlashUntilMs) >= 0) {
    gTvFlash = tv_remote::kNothing;  // Unlit again.
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
  if (key >= '1' && key < char('1' + tv_remote::kDeviceCount)) {
    tvShowPage(tv_remote::Device(key - '1'));
    return true;
  }
  const tv_remote::Press p = tv_remote::keyFor(gTvPage, key, keys.enter, keys.del);
  const Action a = p.action;
  if (a != Action::Forget) gTvForgetAskedMs = 0;
  if ((a == Action::Bluetooth || a == Action::Typing || a == Action::Forget) && !tv_ble::available()) {
    tvSay(tv_ble::failure(), tvMuted());
    return true;
  }
  switch (a) {
    case Action::None: tvSay(tvPageHint(), tvMuted()); return true;
    case Action::Bluetooth: tvToggleBluetooth(); return true;
    case Action::Typing: tvOpenTyping(); return true;
    case Action::Forget: tvForget(); return true;
    case Action::Codes: tvNextBoseCodes(); return true;
    default: break;
  }
  tvSend(p, false);
  if (tv_remote::repeats(a) && key > 4) tvHold(key, p, 0);
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

// The colours hold up in the canvas's 3-3-2 bits, where a darker tint would turn black.
uint16_t tvPanel() { return rgb565_local(36, 36, 85); }
uint16_t tvEdge() { return rgb565_local(73, 73, 170); }
uint16_t tvKeycap() { return rgb565_local(36, 73, 170); }
uint16_t tvLit() { return rgb565_local(36, 109, 170); }
uint16_t tvLabel() { return rgb565_local(146, 146, 170); }
bool tvLitNow(tv_remote::Press p) { return gTvFlash == p && int32_t(millis() - gTvFlashUntilMs) < 0; }

// A ring: the four arrows around OK, for the Apple TV or the projector's menus, the one just sent lit.
void tvDrawPad(int16_t cx, int16_t cy, tv_remote::Device device) {
  using tv_remote::Action;
  auto& d = gCanvas;
  constexpr int16_t r = 37;
  d.fillCircle(cx, cy, r, tvPanel());
  // Which quarter to light, as LovyanGFX's angles go: clockwise from three o'clock.
  struct Side { Action action; float from, to; };
  constexpr Side sides[] = {{Action::Up, 225, 315}, {Action::Down, 45, 135}, {Action::Left, 135, 225}};
  for (const Side& side : sides)
    if (tvLitNow({device, side.action})) d.fillArc(cx, cy, 16, r - 1, side.from, side.to, tvLit());
  if (tvLitNow({device, Action::Right})) {
    d.fillArc(cx, cy, 16, r - 1, 315, 360, tvLit());
    d.fillArc(cx, cy, 16, r - 1, 0, 45, tvLit());
  }
  d.drawCircle(cx, cy, r, tvEdge());
  const uint16_t arrow = TFT_WHITE;
  d.fillTriangle(cx, cy - 33, cx - 6, cy - 26, cx + 6, cy - 26, arrow);
  d.fillTriangle(cx, cy + 33, cx - 6, cy + 26, cx + 6, cy + 26, arrow);
  d.fillTriangle(cx - 33, cy, cx - 26, cy - 6, cx - 26, cy + 6, arrow);
  d.fillTriangle(cx + 33, cy, cx + 26, cy - 6, cx + 26, cy + 6, arrow);
  // No key names here: the Cardputer's ; . , / keys have these arrows on them.
  const bool ok = tvLitNow({device, Action::Select});
  d.fillCircle(cx, cy, 14, ok ? TFT_WHITE : tvAccentOf(device));
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_BLACK);
  d.setCursor(cx - d.textWidth("OK") / 2, cy - 7);
  d.print("OK");
}

// A remote's button: its key on a small cap, then what it does. Lit, in its device's colour,
// while it's being sent.
constexpr int16_t kTvButtonW = 70, kTvButtonH = 17;
void tvButton(int16_t x, int16_t y, const char* key, const String& label, tv_remote::Press press, int16_t w = kTvButtonW) {
  auto& d = gCanvas;
  const bool lit = tvLitNow(press);
  const uint16_t accent = tvAccentOf(press.device);
  d.fillRoundRect(x, y, w, kTvButtonH, 5, lit ? accent : tvPanel());
  d.drawRoundRect(x, y, w, kTvButtonH, 5, lit ? accent : tvEdge());
  d.setFont(&fonts::Font0);
  const int16_t capW = max<int16_t>(13, d.textWidth(key) + 6);
  d.fillRoundRect(x + 2, y + 2, capW, kTvButtonH - 4, 3, lit ? TFT_WHITE : tvKeycap());
  d.setTextColor(lit ? TFT_BLACK : TFT_WHITE);
  d.setCursor(x + 2 + (capW - d.textWidth(key)) / 2, y + 5);
  d.print(key);
  d.setFont(&fonts::Font2);
  d.setTextColor(lit ? TFT_BLACK : TFT_WHITE);
  d.setCursor(x + capW + 6, y + 1);
  d.print(fitCurrentFontToWidth(label, w - capW - 8));
}

// A group's name over its buttons, in its device's colour, with a rule to the edge.
void tvGroup(int16_t x, int16_t y, const char* name, uint16_t ink) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  d.setTextColor(ink);
  d.setCursor(x, y);
  d.print(name);
  const int16_t after = x + d.textWidth(name) + 4;
  d.drawFastHLine(after, y + 3, 236 - after, tvEdge());
}

// The volume as a rocker, + over -, each half lit as it's sent.
void tvRocker(int16_t x, int16_t y, int16_t w, int16_t h, tv_remote::Device device) {
  using tv_remote::Action;
  auto& d = gCanvas;
  const int16_t half = (h - 4) / 2;
  const auto part = [&](int16_t top, Action action, bool plus, const char* key) {
    const bool lit = tvLitNow({device, action});
    const uint16_t accent = tvAccentOf(device), ink = lit ? TFT_BLACK : TFT_WHITE;
    d.fillRoundRect(x, top, w, half, 8, lit ? accent : tvPanel());
    d.drawRoundRect(x, top, w, half, 8, lit ? accent : tvEdge());
    // A bold + or -, drawn, since the fonts' are thin at this size.
    const int16_t cx = x + w / 2, cy = top + half / 2;
    d.fillRect(cx - 11, cy - 3, 22, 6, ink);
    if (plus) d.fillRect(cx - 3, cy - 11, 6, 22, ink);
    d.setFont(&fonts::Font0);
    d.fillRoundRect(x + 4, top + 4, 13, 12, 3, lit ? TFT_WHITE : tvKeycap());
    d.setTextColor(lit ? TFT_BLACK : TFT_WHITE);
    d.setCursor(x + 4 + (13 - d.textWidth(key)) / 2, top + 6);
    d.print(key);
  };
  part(y, Action::VolumeUp, true, "=");
  part(y + half + 4, Action::VolumeDown, false, "-");
  d.setFont(&fonts::Font0);
  d.setTextColor(tvLabel());
  d.setCursor(x + (w - d.textWidth("VOLUME")) / 2, y + h + 2);
  d.print("VOLUME");
}

// The tabs, one a device, the page on screen filled in its colour. With Bluetooth built in, a dot
// on the Apple TV's tab says how it's connected.
void tvTabs() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::Font2);
  int16_t x = 2;
  for (uint8_t i = 0; i < tv_remote::kDeviceCount; ++i) {
    const tv_remote::Device device = tv_remote::Device(i);
    const char* name = tv_remote::deviceName(device);
    const bool on = device == gTvPage;
    const int16_t w = d.textWidth(name) + 10;
    const uint16_t accent = tvAccentOf(device);
    if (on) d.fillRoundRect(x, 1, w, 19, 5, accent);
    else d.drawRoundRect(x, 1, w, 19, 5, tvEdge());
    d.setTextColor(on ? TFT_BLACK : accent);
    d.setCursor(x + 5, 3);
    d.print(name);
    if (device == tv_remote::Device::AppleTv && tv_ble::available() && tv_ble::state() != tv_ble::State::Off) {
      uint16_t ink = 0;
      tvLinkLabel(ink);
      d.fillCircle(x + w - 4, 5, 2, ink);
    }
    x += w + 3;
  }
  d.drawFastHLine(0, 22, 240, tvAccent());
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
  using tv_remote::Action;
  using tv_remote::Device;
  if (tvNoticeShowing()) {
    ink = gTvNoticeInk;
    return gTvNotice;
  }
  ink = tvMuted();
  if (gTvLast.action == Action::None) {
    switch (gTvPage) {
      case Device::Bose: return "Point at the Bose. C: other codes";
      case Device::Epson: return "Point at the projector. 1-4: remotes";
      case Device::Samsung: return "Point at the TV. 1-4: remotes";
      default: return tvBluetoothReady() ? "Connected: no need to point it" : "Point at the Apple TV. 1-4: remotes";
    }
  }
  // Epson projectors ask for a second press before they turn off.
  if (gTvLast == tv_remote::Press{Device::Epson, Action::Power}) return "Power: twice turns the projector off";
  const String name = tv_remote::actionName(gTvLast.action);
  if (gTvLastByBluetooth) return name + " by Bluetooth";
  return name + " to " + tv_remote::deviceThe(gTvLast.device);
}

void renderTvRemote() {
  using tv_remote::Action;
  using tv_remote::Device;
  using tv_remote::Press;
  auto& d = gCanvas;
  tvTabs();
  constexpr int16_t left = 92, right = left + kTvButtonW + 4, wide = 2 * kTvButtonW + 4;
  const uint16_t boseInk = tvAccentOf(Device::Bose);
  switch (gTvPage) {
    case Device::Bose:
      tvRocker(8, 26, 72, 88, Device::Bose);
      tvButton(left, 25, "P", "Power", {Device::Bose, Action::Power}, wide);
      tvButton(left, 45, "M", "Mute", {Device::Bose, Action::Mute}, wide);
      tvButton(left, 65, "T", "TV input", {Device::Bose, Action::TvInput}, wide);
      tvButton(left, 85, "B", "Bluetooth input", {Device::Bose, Action::BluetoothInput}, wide);
      tvButton(left, 105, "C", String("Codes: ") + tvBoseCodesName() + " remote", {Device::Bose, Action::Codes}, wide);
      break;
    case Device::Epson:
      tvDrawPad(45, 73, Device::Epson);
      tvButton(left, 26, "P", "Power", {Device::Epson, Action::Power});
      tvButton(right, 26, "S", "Source", {Device::Epson, Action::Source});
      tvButton(left, 48, "I", "HDMI 1", {Device::Epson, Action::Hdmi1});
      tvButton(right, 48, "O", "HDMI 2", {Device::Epson, Action::Hdmi2});
      tvButton(left, 70, "M", "Menu", {Device::Epson, Action::Menu});
      tvButton(right, 70, "Del", "Back", {Device::Epson, Action::Back});
      tvButton(left, 92, "B", "Blank", {Device::Epson, Action::Blank});
      break;
    case Device::Samsung: {
      const uint16_t ink = tvAccentOf(Device::Samsung);
      tvDrawPad(45, 78, Device::Samsung);
      tvGroup(left, 25, "SAMSUNG TV", ink);
      tvButton(left, 35, "Del", "Back", {Device::Samsung, Action::Back});
      tvButton(right, 35, "H", "Home", {Device::Samsung, Action::Home});
      tvButton(left, 55, "S", "Source", {Device::Samsung, Action::Source});
      tvButton(right, 55, "E", "Exit", {Device::Samsung, Action::Exit});
      tvGroup(left, 77, "SOUND, POWER", ink);
      tvButton(left, 87, "=", "Vol +", {Device::Samsung, Action::VolumeUp});
      tvButton(right, 87, "-", "Vol -", {Device::Samsung, Action::VolumeDown});
      tvButton(left, 107, "M", "Mute", {Device::Samsung, Action::Mute});
      tvButton(right, 107, "P", "Power", {Device::Samsung, Action::Power});
      break;
    }
    default: {
      tvDrawPad(45, 78, Device::AppleTv);
      const bool pairing = tv_ble::state() == tv_ble::State::Waiting && !tv_ble::paired();
      if (pairing) {
        tvPairingSteps(left, 26);
      } else if (tv_ble::available()) {
        // With Bluetooth there's a third Apple TV row, so the groups lose their names for a rule.
        tvButton(left, 25, "Del", "Back", {Device::AppleTv, Action::Back});
        tvButton(right, 25, "Spc", "Play", {Device::AppleTv, Action::PlayPause});
        tvButton(left, 45, "H", "Home", {Device::AppleTv, Action::Home});
        tvButton(right, 45, "Z", "Sleep", {Device::AppleTv, Action::Sleep});
        tvButton(left, 65, "T", "Type", {Device::AppleTv, Action::Typing});
        tvButton(right, 65, "B", tv_ble::on() ? "BT off" : "BT on", {Device::AppleTv, Action::Bluetooth});
        d.drawFastHLine(left, 84, 236 - left, tvEdge());
        tvButton(left, 86, "=", "Vol +", {Device::Bose, Action::VolumeUp});
        tvButton(right, 86, "-", "Vol -", {Device::Bose, Action::VolumeDown});
        tvButton(left, 106, "M", "Mute", {Device::Bose, Action::Mute});
        tvButton(right, 106, "P", "Power", {Device::Bose, Action::Power});
      } else {
        tvGroup(left, 25, "APPLE TV", tvAccentOf(Device::AppleTv));
        tvButton(left, 35, "Del", "Back", {Device::AppleTv, Action::Back});
        tvButton(right, 35, "Spc", "Play", {Device::AppleTv, Action::PlayPause});
        tvButton(left, 55, "H", "Home", {Device::AppleTv, Action::Home});
        tvButton(right, 55, "Z", "Sleep", {Device::AppleTv, Action::Sleep});
        tvGroup(left, 77, "BOSE SOUND", boseInk);
        tvButton(left, 87, "=", "Vol +", {Device::Bose, Action::VolumeUp});
        tvButton(right, 87, "-", "Vol -", {Device::Bose, Action::VolumeDown});
        tvButton(left, 107, "M", "Mute", {Device::Bose, Action::Mute});
        tvButton(right, 107, "P", "Power", {Device::Bose, Action::Power});
      }
      break;
    }
  }
  uint16_t ink = 0;
  const String line = tvLastLine(ink);
  d.setFont(&fonts::Font0);
  d.setTextColor(ink);
  d.setCursor(8, 126);  // Under the last row of buttons, the whole width.
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
