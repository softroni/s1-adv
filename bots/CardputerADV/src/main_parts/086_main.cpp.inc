// Settings > Wi-Fi's screens, in the Lab's look: its header and status chip, its lime row bar and
// dots, its panel for messages and its key line. They come after the Lab to use those pieces. The
// radio, the keys and the screen's decisions are in 079 and wifi_settings.h.
namespace {

// An iPhone names its hotspot with a curly apostrophe, which the bold font lacks, so the whole
// name would drop to the small font. Typographic marks are shown as their plain forms instead.
// Only on screen: the radio always gets the name's exact bytes.
String wifiShownName(const std::string& name) {
  static const struct {
    const char* from;
    const char* to;
  } kPlain[] = {{"\xE2\x80\x98", "'"}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\x9C", "\""},
                {"\xE2\x80\x9D", "\""}, {"\xE2\x80\x93", "-"}, {"\xE2\x80\x94", "-"},
                {"\xE2\x80\xA6", "..."}, {"\xC2\xA0", " "}};
  std::string out = name;
  for (const auto& mark : kPlain) {
    const size_t fromLen = strlen(mark.from), toLen = strlen(mark.to);
    for (size_t at = out.find(mark.from); at != std::string::npos; at = out.find(mark.from, at + toLen)) {
      out.replace(at, fromLen, mark.to);
    }
  }
  return String(out.c_str());
}

// Head of a name as shown, cut on a character boundary because a scanned name can be any UTF-8.
// Measured in the current font, so set it from wifiShownName first.
String wifiFitName(const std::string& text, int32_t limit) {
  String out = wifiShownName(text);
  if (gCanvas.textWidth(out) <= limit) return out;
  while (out.length() > 1 && gCanvas.textWidth(out + "..") > limit) removeLastUtf8Char(out);
  return out + "..";
}

uint16_t wifiMoodColor(Mood mood) {
  return mood == Mood::Good ? kLabGreen : mood == Mood::Bad ? kLabRed : kLabSoft;
}

// Where typing goes: a panel like the Lab's notes, outlined in its lime.
constexpr int kWifiFieldY = 42, kWifiFieldH = 26, kWifiTextX = 9, kWifiTextW = 222;
// One line of news under the field, in the same panel.
constexpr int kWifiNoteY = 96;

void wifiHead(const String& chip, uint16_t chipColor, uint16_t accent = kLabLime) {
  labHead("Wi-Fi", launcher_icons::kWifi, accent, chip, chipColor);
}

// The bold font, or the one for other scripts, which sits a pixel lower.
void wifiNameAt(int x, int y, const std::string& name, int limit, uint16_t ink) {
  auto& d = gCanvas;
  const int16_t drop = useBigTextFont(wifiShownName(name)) - 3;
  d.setTextColor(ink);
  d.setCursor(x, y + drop);
  d.print(wifiFitName(name, limit));
}

void wifiNote(int y, int height, const std::string& text, Mood mood) {
  gCanvas.fillRoundRect(3, y, 234, height, 5, kLabPanel);
  gCanvas.setFont(&fonts::Font2);
  labTextAt(&fonts::Font2, kWifiTextX, y + (height - 16) / 2, fitCurrentFontToWidth(String(text.c_str()), kWifiTextW),
            wifiMoodColor(mood));
}

// Signal strength as the row's icon: four bars in its 16 pixel square.
void wifiSignalIcon(int x, int y, uint8_t level, uint16_t lit, uint16_t dim) {
  for (int i = 0; i < 4; ++i) {
    const int height = 4 + i * 3;
    gCanvas.fillRect(x + 1 + i * 4, y + 15 - height, 3, height, i < level ? lit : dim);
  }
}

void renderWifiList() {
  auto& d = gCanvas;
  const bool online = gWifiRadio.online();
  const bool scanning = gWifi.scanning();
  wifiHead(scanning ? "SCANNING" : online ? "CONNECTED" : "OFFLINE",
           scanning ? kLabAmber : online ? kLabGreen : kLabRed);
  const int rows = static_cast<int>(gWifi.rows());
  const int selected = static_cast<int>(gWifi.selected);
  // A message takes the last row, because it matters more than one row while it's showing, and
  // the chosen row stays above it.
  const bool note = !gWifi.message.empty();
  const int visible = note ? kLabListRows - 1 : kLabListRows;
  const int first = max<int>(0, min<int>(selected - visible / 2, rows - visible));
  for (int row = 0; row < visible && first + row < rows; ++row) {
    const int index = first + row;
    const bool chosen = index == selected;
    const int y = kLabListY + row * kLabRowH;
    if (chosen) labRowBar(y, 233, kLabLime);
    if (index == static_cast<int>(gWifi.networks.size())) {
      const uint16_t ink = chosen ? TFT_BLACK : kLabCyan;
      d.drawBitmap(5, y + 2, launcher_icons::kKeyboard, launcher_icons::kSize, launcher_icons::kSize, ink);
      labTextAt(&fonts::FreeSansBold9pt7b, 26, y + 2, "Other network", ink);
      continue;
    }
    const auto& network = gWifi.networks[index];
    const bool current = online && gWifiSsid == network.ssid.c_str();
    const char* tag = current ? "joined" : network.saved ? "saved" : !network.secured ? "open" : "";
    d.setFont(&fonts::Font2);
    const int tagRoom = *tag ? d.textWidth(tag) + 8 : 0;
    // A saved network that isn't in range is greyed, as the Lab greys a part it can't find.
    wifiSignalIcon(5, y + 2, network.inRange ? wifi_settings::signalBars(network.rssi) : 0,
                   chosen ? TFT_BLACK : kLabLime, kLabDim);
    wifiNameAt(26, y + 2, network.ssid, 216 - 26 - tagRoom, chosen ? TFT_BLACK : network.inRange ? TFT_WHITE : kLabMuted);
    if (*tag) {
      labTextRight(&fonts::Font2, 216, y + 2, tag,
                   chosen ? TFT_BLACK : current ? kLabGreen : network.inRange ? kLabSoft : kLabMuted);
    }
    // Filled for the network in use, hollow for one that's saved.
    if (current) d.fillCircle(227, y + 10, 3, chosen ? TFT_BLACK : kLabGreen);
    else if (network.saved) d.drawCircle(227, y + 10, 3, chosen ? TFT_BLACK : network.inRange ? kLabSoft : kLabMuted);
  }
  labScrollBar(first, visible, rows, kLabListY, visible * kLabRowH - 1);
  if (gWifi.networks.empty()) {
    labTextAt(&fonts::FreeSansBold9pt7b, 4, kLabListY + kLabRowH + 4,
              scanning ? "Looking for networks" : "No networks yet", TFT_WHITE);
    labTextAt(&fonts::Font2, 4, kLabListY + 2 * kLabRowH + 6, "Only 2.4 GHz networks show up", kLabSoft);
  }
  if (note) wifiNote(kLabListY + (kLabListRows - 1) * kLabRowH, kLabRowH - 1, gWifi.message, gWifi.mood);
  const bool otherRow = gWifi.selected >= gWifi.networks.size();
  labKeys(otherRow ? "ENTER type a name R rescan ESC back" :
          gWifi.onSavedRow() ? "ENTER join R rescan F forget ESC back" : "ENTER join R rescan ESC back");
}

void renderWifiField() {
  auto& d = gCanvas;
  const bool forPassword = gWifi.screen == Screen::Password;
  wifiHead(forPassword ? "PASSWORD" : "OTHER NETWORK", kLabSoft);
  if (forPassword) wifiNameAt(4, 21, gWifi.name, 232, TFT_WHITE);
  else labTextAt(&fonts::FreeSansBold9pt7b, 4, 21, "Network name", TFT_WHITE);

  d.fillRoundRect(3, kWifiFieldY, 234, kWifiFieldH, 6, kLabPanel);
  d.drawRoundRect(3, kWifiFieldY, 234, kWifiFieldH, 6, kLabLime);
  const std::string shown = !forPassword ? gWifi.name :
      gWifi.revealed ? gWifi.password : wifi_settings::masked(gWifi.password);
  // The bold font's asterisk is a speck, so each hidden character is a dot, easier to count.
  // The newest ones stay in view, like the end of the text does.
  const size_t hidden = forPassword && !gWifi.revealed && !shown.empty() ? shown.size() - 1 : 0;
  const String typed = String(shown.substr(hidden).c_str()) + "_";
  const int16_t drop = useBigTextFont(typed) - 3;
  constexpr int kDot = 9;
  const int typedW = min<int>(d.textWidth(typed), kWifiTextW);
  const int dots = min<int>(static_cast<int>(hidden), (kWifiTextW - typedW) / kDot);
  for (int i = 0; i < dots; ++i) d.fillCircle(kWifiTextX + 3 + i * kDot, kWifiFieldY + 13, 3, TFT_WHITE);
  d.setTextColor(TFT_WHITE);
  d.setCursor(kWifiTextX + dots * kDot, kWifiFieldY + 5 + drop);
  d.print(tailUtf8ToFit(typed, kWifiTextW - dots * kDot));

  const String hint = !forPassword ? String("Capitals count. 2.4 GHz only.") :
      gWifi.secured ? String("At least 8 characters") : String("Leave blank for an open network");
  labTextAt(&fonts::Font2, 4, kWifiFieldY + kWifiFieldH + 5, hint, kLabSoft);
  if (!gWifi.message.empty()) wifiNote(kWifiNoteY, 26, gWifi.message, gWifi.mood);
  // While typing, ` is a character, so going back takes Fn.
  labKeys(forPassword ? "ENTER join FN+; show FN+ESC back" : "ENTER next FN+ESC back");
}

void renderWifiJoining() {
  wifiHead("JOINING", kLabAmber);
  wifiNameAt(4, 21, gWifi.name, 232, TFT_WHITE);
  const uint32_t elapsed = min<uint32_t>(gWifi.joinElapsed(millis()), wifi_settings::kJoinTimeoutMs);
  labBar(4, 47, 232, 8, static_cast<int>(elapsed * 100 / wifi_settings::kJoinTimeoutMs), kLabLime);
  labTextAt(&fonts::Font2, 4, 62, "Saved only if it connects", kLabSoft);
  labKeys("ESC cancel");
}

// Red, as the Lab colours what can't be undone.
void renderWifiForget() {
  wifiHead("FORGET", kLabRed, kLabRed);
  wifiNameAt(4, 21, gWifi.name, 232, TFT_WHITE);
  labTextAt(&fonts::Font2, 4, 43, "Forget this network?", TFT_WHITE);
  labTextAt(&fonts::Font2, 4, 61, gWifiRadio.isCurrent(gWifi.name) ? "Deletes its password, disconnects" :
                                                                       "Its saved password is deleted", kLabSoft);
  if (!gWifi.message.empty()) wifiNote(kWifiNoteY, 26, gWifi.message, gWifi.mood);
  labKeys("ENTER forget ESC keep");
}

void renderWifiSettingsUi() {
  gCanvas.setTextWrap(false);
  if (gWifi.screen == Screen::List) renderWifiList();
  else if (gWifi.screen == Screen::Joining) renderWifiJoining();
  else if (gWifi.screen == Screen::Forget) renderWifiForget();
  else renderWifiField();
  gCanvas.setFont(&fonts::Font2);
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
