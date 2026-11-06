void renderDebugOverlay() {
  if (!gDebugOverlayVisible) {
    return;
  }
  auto& d = gCanvas;
  const uint16_t bg = rgb565_local(0, 3, 8);
  const uint16_t line = rgb565_local(53, 68, 82);
  d.fillRoundRect(8, 8, 224, 104, 10, bg);
  d.drawRoundRect(8, 8, 224, 104, 10, line);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_WHITE, bg);
  d.setCursor(14, 15);
  d.print("DBG");
  d.setCursor(48, 15);
  d.print(gWifiReady ? "wifi=ok" : "wifi=off");
  d.setCursor(118, 15);
  d.print(gWsReady ? "ws=ok" : "ws=off");
  d.setCursor(14, 28);
  d.print("face=");
  d.print(static_cast<int>(gFaceMode));
  d.setCursor(86, 28);
  d.print("scroll=");
  d.print(gChatScrollOffset);
  d.setCursor(150, 28);
  d.print(layoutName(gKeyboardLayout));
  d.setCursor(14, 41);
  d.print("rec=");
  d.print(static_cast<unsigned>(gRecordCapacityBytes / 1024));
  d.print("k");
  d.setCursor(86, 41);
  d.print("tts=");
  d.print(static_cast<unsigned>(gTtsCapacityBytes / 1024));
  d.print("k");
  d.setCursor(150, 41);
  d.print(gStorageReady ? gStorageLabel : "fs=off");
  d.setCursor(14, 55);
  String diag = gLastVoiceDiag;
  if (diag.length() > 28) {
    diag = diag.substring(diag.length() - 28);
  }
  d.print(diag);
  d.setCursor(14, 69);
  String httpDiag = gLastHttpDiag;
  if (httpDiag.length() > 28) {
    httpDiag = httpDiag.substring(httpDiag.length() - 28);
  }
  d.print(httpDiag);
  d.setCursor(14, 83);
  String keyDiag = gLastKeyDiag;
  if (keyDiag.length() > 28) {
    keyDiag = keyDiag.substring(keyDiag.length() - 28);
  }
  d.print(keyDiag);
  d.setCursor(150, 83);
  d.print(String(uiModeName(gUiMode)).substring(0, 6));
  d.setFont(&fonts::Font2);
}

const uint8_t* launcherItemIcon(const LauncherSubItem& item) {
  using namespace launcher_icons;
  switch (item.mode) {
    case UiMode::Home: return item.startRealtime ? kWaveform : kHeart;
    case UiMode::ChatFull: return kChat;
    case UiMode::Library: return kBooks;
    case UiMode::AudioFolders: return kFolder;
    case UiMode::Voice: return kMic;
    case UiMode::Notes: return kNotepad;
    case UiMode::Assets: return kPicture;
    case UiMode::Files: return kFile;
    case UiMode::Radio: return kRadio;
    case UiMode::Podcasts: return kHeadphones;
    case UiMode::VlcRemote: return kCone;
    case UiMode::Focus:
    case UiMode::FocusSettings: return kTomato;
    case UiMode::Alarm: return kBell;
    case UiMode::Stopwatch: return kStopwatch;
    case UiMode::Calendar: return kCalendar;
    case UiMode::ChessMind: return kKnight;
    case UiMode::Chess: return kPawn;
    case UiMode::BrainFlow: return kBolt;
    case UiMode::KeyRush: return kKeyboard;
    case UiMode::Calculator: return kCalculator;
    case UiMode::NfcNotes: return kNfc;
    case UiMode::RfidLab: return kCard;
    case UiMode::IrRemote: return kRemote;
    case UiMode::TvRemote: return kTv;
    case UiMode::SmartLamp: return kBulb;
    case UiMode::Sonar: return kSonar;
    case UiMode::Mesh: return kMesh;
    case UiMode::PocketSynth: return kPiano;
    case UiMode::Tetris: return kTetris;
    case UiMode::Nes: return kNesPad;
    case UiMode::Checkers: return kCheckers;
    case UiMode::Minesweeper: return kMine;
    case UiMode::Snake: return kSnake;
    case UiMode::AirMusic: return kAirMusic;
    case UiMode::Flappy: return kBird;
    case UiMode::Balloon: return kBalloon;
    case UiMode::BallCatch: return kBallCatch;
    case UiMode::LabDevices: return kPlug;
    case UiMode::LabRules: return kFlow;
    case UiMode::LabSequences: return kSteps;
    case UiMode::Settings: return kDevice;
    case UiMode::ScreenSettings: return kDisplay;
    case UiMode::WifiSettings: return kWifi;
    case UiMode::Ota: return kDownload;
    case UiMode::Logs: return kLog;
    case UiMode::CMatrix: return kMatrix;
    default: return nullptr;
  }
}

// Pomodoro is in Focus and in Settings, so on Favorites it needs its tab's name beside it.
bool launcherLabelInTwoTabs(const char* label) {
  int tabs = 0;
  for (uint8_t g = 0; g < kLauncherGroupCount; ++g)
    for (uint8_t i = 0; i < kLauncherGroups[g].count; ++i)
      if (!strcmp(kLauncherGroups[g].items[i].label, label)) {
        ++tabs;
        break;
      }
  return tabs > 1;
}

// The menu fills the screen: a strip with one tab per group, where the selected tab widens to
// hold its name, then five rows of that group. Ten tabs leave the selected one room for its
// name but not its icon too. The canvas is 8-bit, so every colour here has to stay distinct
// after RGB332 reduction. Dark tints do not survive it.
void renderLauncherOverlay() {
  if (!gLauncherVisible) {
    return;
  }
  auto& d = gCanvas;
  gLauncherSelection = clampValue<int>(gLauncherSelection, 0, kLauncherGroupCount - 1);
  const LauncherGroup& group = launcherGroup(gLauncherSelection);
  const bool favorites = gLauncherSelection == kFavoritesGroup;
  gLauncherSubSelection = group.count ? clampValue<int>(gLauncherSubSelection, 0, group.count - 1) : 0;

  static_assert(sizeof(launcher_icons::kGroups) / sizeof(launcher_icons::kGroups[0]) == kLauncherGroupCount,
                "launcher_icons::kGroups needs one icon per group");
  static_assert(launcher_shortcuts::kGroupLetterCount == kLauncherGroupCount,
                "launcher_shortcuts::kGroupLetters needs one letter per group");
  constexpr int16_t kScreenW = 240;
  constexpr int16_t kTabH = 21;
  constexpr int16_t kTabPad = 6;
  constexpr int16_t kListY = 24;
  constexpr int16_t kRowH = 22;
  constexpr int kVisibleRows = 5;
  constexpr int16_t kRowNumberX = 223;
  constexpr int16_t kMarkX = kRowNumberX - 12;
  const int16_t iconSize = launcher_icons::kSize;
  const uint16_t black = TFT_BLACK;
  const uint16_t white = TFT_WHITE;
  const uint16_t muted = rgb565_local(109, 109, 85);
  const uint16_t scrollTrack = rgb565_local(36, 36, 85);
  const uint16_t star = kLauncherGroups[kFavoritesGroup].accent;
  const uint16_t help = rgb565_local(182, 182, 170);  // Brighter than muted: these are sentences to read.

  d.fillScreen(black);

  d.setFont(&fonts::FreeSansBold9pt7b);
  const int16_t selectedTabW = 2 * kTabPad + d.textWidth(group.title);
  const int16_t otherTabW = (kScreenW - selectedTabW) / (kLauncherGroupCount - 1);
  int16_t tabX = (kScreenW - selectedTabW - otherTabW * (kLauncherGroupCount - 1)) / 2;
  for (int i = 0; i < kLauncherGroupCount; ++i) {
    if (i == gLauncherSelection) {
      d.fillRoundRect(tabX, 0, selectedTabW, kTabH, 6, group.accent);
      d.setTextColor(black);
      d.setCursor(tabX + kTabPad, 3);
      d.print(group.title);
      // The letter that jumps here, underlined the way a menu shows its shortcut.
      const int mark = launcher_shortcuts::letterPosition(group.title, i);
      if (mark >= 0) {
        const String title(group.title);
        const int16_t start = d.textWidth(title.substring(0, mark));
        d.drawFastHLine(tabX + kTabPad + start, 18, d.textWidth(title.substring(0, mark + 1)) - start - 1, black);
      }
      tabX += selectedTabW;
    } else {
      d.drawBitmap(tabX + (otherTabW - iconSize) / 2, 2, launcher_icons::kGroups[i], iconSize, iconSize,
                   kLauncherGroups[i].accent);
      tabX += otherTabW;
    }
  }
  d.drawFastHLine(0, kTabH + 1, kScreenW, group.accent);

  if (!group.count) {
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(white);
    const char* heading = "No favorites yet";
    d.setCursor((kScreenW - d.textWidth(heading)) / 2, 40);
    d.print(heading);
    d.setFont(&fonts::Font2);
    d.setTextColor(help);
    const char* lines[] = {"Space on any app adds it here,", "and its number opens it from", "any tab in the menu."};
    for (int line = 0; line < 3; ++line) {
      d.setCursor((kScreenW - d.textWidth(lines[line])) / 2, 68 + line * 18);
      d.print(lines[line]);
    }
  }

  const int first = clampValue<int>(gLauncherSubSelection - kVisibleRows + 1, 0,
                                    max<int>(0, group.count - kVisibleRows));
  for (int row = 0; row < min<int>(kVisibleRows, group.count); ++row) {
    const int i = first + row;
    const int16_t y = kListY + row * kRowH;
    const bool selected = i == gLauncherSubSelection;
    const LauncherSubItem& item = group.items[i];
    const uint8_t* icon = launcherItemIcon(item);
    // A favorite keeps its own tab's colour, so the list reads like the tabs it came from.
    const int home = favorites ? gFavorites.refs[i].group : gLauncherSelection;
    const bool marked = !favorites && gFavorites.find(gLauncherSelection, launcherTableItem(gLauncherSelection, i)) >= 0;
    if (selected) {
      d.fillRoundRect(1, y, 233, kRowH - 1, 6, group.accent);
    }
    d.drawBitmap(7, y + 2, icon ? icon : launcher_icons::kGroups[home], iconSize, iconSize,
                 selected ? black : kLauncherGroups[home].accent);
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(selected ? black : white);
    d.setCursor(30, y + 3);
    d.print(fitCurrentFontToWidth(item.label, (marked ? kMarkX - 4 : kRowNumberX) - 34));
    if (favorites && launcherLabelInTwoTabs(item.label)) {
      d.setFont(&fonts::Font2);
      d.setTextColor(selected ? black : muted);
      d.setCursor(d.getCursorX() + 6, y + 4);
      d.print(fitCurrentFontToWidth(kLauncherGroups[home].title, kRowNumberX - 4 - d.getCursorX()));
    }
    // In its own tab, an app that is also a favorite has a star by its number.
    if (marked) {
      d.drawBitmap(kMarkX, y + 6, launcher_icons::kStarMark, launcher_icons::kMarkSize, launcher_icons::kMarkSize,
                   selected ? black : star);
    }
    // This number opens the row: on its own on Favorites, from any tab, and with Ctrl elsewhere.
    d.setFont(&fonts::Font2);
    d.setTextColor(selected ? black : muted);
    d.setCursor(kRowNumberX, y + 3);
    d.print(String(i + 1));
  }

  if (group.count > kVisibleRows) {
    const int16_t trackH = kVisibleRows * kRowH - 1;
    const int16_t thumbH = trackH * kVisibleRows / group.count;
    d.fillRect(236, kListY, 3, trackH, scrollTrack);
    d.fillRect(236, kListY + (trackH - thumbH) * first / (group.count - kVisibleRows), 3, thumbH, group.accent);
  }

  // Short lists have room to say how to change them.
  if (favorites && group.count > 0 && group.count < kVisibleRows) {
    d.setFont(&fonts::Font2);
    d.setTextColor(rgb565_local(145, 145, 170));
    const char* hint = "Fn+up/down moves  Space removes";
    d.setCursor((kScreenW - d.textWidth(hint)) / 2, 117);
    d.print(hint);
  }

  if (gLauncherNoticeUntilMs && static_cast<int32_t>(gLauncherNoticeUntilMs - millis()) > 0) {
    d.setFont(&fonts::Font2);
    const String notice = fitCurrentFontToWidth(gLauncherNotice.c_str(), 220);
    d.fillRoundRect(4, 112, 232, 21, 6, white);
    d.setTextColor(black);
    d.setCursor((kScreenW - d.textWidth(notice)) / 2, 115);
    d.print(notice);
  } else {
    gLauncherNoticeUntilMs = 0;
  }
  d.setFont(&fonts::Font2);
}

String batteryEtaText() {
  if (!gBatteryDrainKnown || gBatteryEtaMinutes == 0 || batteryExternalPowerPresent()) {
    return "--";
  }
  uint16_t h = gBatteryEtaMinutes / 60;
  uint16_t m = gBatteryEtaMinutes % 60;
  if (h > 0) {
    return String(h) + "h" + String(m) + "m";
  }
  return String(m) + "m";
}

void drawBatteryHistoryGraph(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t panel) {
  auto& d = gCanvas;
  uint16_t grid = rgb565_local(39, 54, 64);
  uint16_t line = rgb565_local(47, 227, 255);
  uint16_t muted = rgb565_local(126, 143, 157);
  d.fillRoundRect(x, y, w, h, 5, rgb565_local(5, 14, 19));
  d.drawRoundRect(x, y, w, h, 5, grid);
  d.drawFastHLine(x + 4, y + h / 2, w - 8, rgb565_local(21, 31, 38));
  if (gBatteryHistoryCount < 2) {
    d.setFont(&fonts::Font0);
    d.setTextColor(muted, rgb565_local(5, 14, 19));
    d.setCursor(x + 8, y + 9);
    d.print("collecting drain history");
    return;
  }

  int16_t prevX = -1;
  int16_t prevY = -1;
  for (uint8_t i = 0; i < gBatteryHistoryCount; ++i) {
    BatteryHistorySample sample = batteryHistorySampleAt(i);
    int value = sample.level >= 0 ? sample.level : estimateBatteryLevelFromVoltage(sample.voltageMv);
    if (value < 0) {
      continue;
    }
    int16_t px = x + 4 + (gBatteryHistoryCount <= 1 ? 0 : (static_cast<int32_t>(i) * (w - 8)) / (gBatteryHistoryCount - 1));
    int16_t py = y + h - 4 - (clampValue<int>(value, 0, 100) * (h - 8)) / 100;
    if (prevX >= 0) {
      d.drawLine(prevX, prevY, px, py, line);
    }
    d.fillCircle(px, py, sample.externalPower ? 2 : 1, sample.externalPower ? rgb565_local(88, 240, 141) : line);
    prevX = px;
    prevY = py;
  }
}

void renderBatteryUi() {
  updateBatterySnapshot();
  auto& d = gCanvas;
  const uint16_t bg = rgb565_local(2, 7, 10);
  const uint16_t panel = rgb565_local(8, 17, 23);
  const uint16_t border = rgb565_local(48, 54, 61);
  const uint16_t accent = rgb565_local(47, 227, 255);
  const uint16_t muted = rgb565_local(126, 143, 157);
  d.fillScreen(bg);
  d.fillRoundRect(6, 5, 228, 124, 12, panel);
  d.drawRoundRect(6, 5, 228, 124, 12, border);

  bool levelKnown = gBatterySnapshot.level >= 0;
  int level = levelKnown ? clampValue<int>(gBatterySnapshot.level, 0, 100) : 0;
  uint16_t barColor = !levelKnown ? rgb565_local(80, 130, 180)
                     : level >= 60 ? rgb565_local(88, 240, 141)
                     : level >= 25 ? rgb565_local(255,136,64)
                                   : rgb565_local(255, 93, 88);

  d.setFont(&fonts::Font2);
  d.setTextColor(accent, panel);
  d.setCursor(14, 12);
  d.print("BATTERY");
  d.setFont(&fonts::Font0);
  d.setTextColor(muted, panel);
  d.setCursor(68, 15);
  d.print(gBatteryHistoryCount);
  d.print(" samples");

  d.setFont(&fonts::Font7);
  d.setTextColor(TFT_WHITE, panel);
  d.setCursor(16, 28);
  d.print(levelKnown ? String(level) : "--");
