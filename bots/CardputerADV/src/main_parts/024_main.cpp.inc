  if (type == "focus.set_duration" || type == "focus.start") {
    if (!action["minutes"].isNull()) {
      uint32_t seconds = static_cast<uint32_t>(actionInt(action, "minutes", gFocusSettings.focusSec / 60, 1, 60)) * 60UL;
      gFocusSettings.focusSec = clampValue<uint32_t>(seconds, kFocusMinDurationSec, kFocusMaxDurationSec);
      if (gFocus.state == FocusState::Stopped) {
        gFocus.remainingSec = gFocusSettings.focusSec;
        gFocus.sessionTotalSec = gFocusSettings.focusSec;
      }
      saveFocusSettings();
    }
    setUiMode(UiMode::Focus);
    if (type == "focus.start" && !focusStateRuns(gFocus.state)) {
      focusStartOrResume();
    }
    setStatus("Action: focus " + String(gFocusSettings.focusSec / 60) + "m", 1200);
    return true;
  }
  if (type == "focus.pause") {
    setUiMode(UiMode::Focus);
    if (focusStateRuns(gFocus.state)) {
      focusStartOrResume();
    }
    return true;
  }
  if (type == "focus.resume") {
    setUiMode(UiMode::Focus);
    if (gFocus.state == FocusState::Paused || gFocus.state == FocusState::Stopped) {
      focusStartOrResume();
    }
    return true;
  }
  if (type == "focus.reset") {
    focusReset();
    setUiMode(UiMode::Focus);
    return true;
  }
  if (type == "focus.set_short_break") {
    gFocusSettings.shortBreakSec = static_cast<uint32_t>(actionInt(action, "minutes", gFocusSettings.shortBreakSec / 60, 1, 60)) * 60UL;
    saveFocusSettings();
    setStatus("Action: short break " + String(gFocusSettings.shortBreakSec / 60) + "m", 1200);
    return true;
  }
  if (type == "focus.set_long_break") {
    gFocusSettings.longBreakSec = static_cast<uint32_t>(actionInt(action, "minutes", gFocusSettings.longBreakSec / 60, 1, 60)) * 60UL;
    saveFocusSettings();
    setStatus("Action: long break " + String(gFocusSettings.longBreakSec / 60) + "m", 1200);
    return true;
  }
  if (type == "focus.set_cycles") {
    gFocusSettings.cyclesPerRound = static_cast<uint8_t>(actionInt(action, "cycles", gFocusSettings.cyclesPerRound, 1, 9));
    saveFocusSettings();
    setStatus("Action: cycles " + String(gFocusSettings.cyclesPerRound), 1200);
    return true;
  }
  if (type == "focus.set_autostart") {
    gFocusSettings.autoStart = actionBool(action["value"], gFocusSettings.autoStart);
    saveFocusSettings();
    setStatus(gFocusSettings.autoStart ? "Action: auto on" : "Action: auto off", 1200);
    return true;
  }
  if (type == "focus.set_metronome") {
    gFocusSettings.metronome = actionBool(action["value"], gFocusSettings.metronome);
    saveFocusSettings();
    setStatus(gFocusSettings.metronome ? "Action: metro on" : "Action: metro off", 1200);
    return true;
  }
  if (type == "focus.set_bpm") {
    gFocusSettings.bpm = static_cast<uint16_t>(actionInt(action, "bpm", gFocusSettings.bpm, kFocusMinBpm, kFocusMaxBpm));
    saveFocusSettings();
    setStatus("Action: bpm " + String(gFocusSettings.bpm), 1200);
    return true;
  }
  if (type == "ui.open") {
    UiMode mode;
    String modeName = action["mode"] | "";
    if (modeName.isEmpty()) {
      modeName = action["screen"] | "";
    }
    if (mapActionUiMode(modeName, mode)) {
      setUiMode(mode);
      return true;
    }
    return false;
  }
  if (type == "audio.play") {
    return playAudioAssetByAction(action);
  }
  if (type == "voice.play") {
    return playVoiceNoteByAction(action);
  }
  if (type == "settings.set_audio_language") {
    String language = action["value"] | "";
    language.trim();
    language.toLowerCase();
    if (language == "ru" || language == "en" || language == "es") {
      gDeviceSettings.audioLanguage = language;
      saveDeviceSettings();
      setStatus("Action: lang " + language, 1200);
      return true;
    }
    return false;
  }
  if (type == "settings.set_ota_auto") {
    gDeviceSettings.autoUpdateFirmware = actionBool(action["value"], gDeviceSettings.autoUpdateFirmware);
    saveDeviceSettings();
    setStatus(gDeviceSettings.autoUpdateFirmware ? "Action: OTA auto" : "Action: OTA manual", 1200);
    return true;
  }
  if (type == "settings.set_ota_boot_check") {
    gDeviceSettings.checkUpdatesOnBoot = actionBool(action["value"], gDeviceSettings.checkUpdatesOnBoot);
    saveDeviceSettings();
    setStatus(gDeviceSettings.checkUpdatesOnBoot ? "Action: boot check" : "Action: no boot check", 1200);
    return true;
  }
  if (type == "settings.set_ota_min_battery") {
    gDeviceSettings.minBatteryForUpdate = static_cast<uint8_t>(actionInt(action, "value", gDeviceSettings.minBatteryForUpdate, 5, 95));
    saveDeviceSettings();
    setStatus("Action: OTA battery " + String(gDeviceSettings.minBatteryForUpdate) + "%", 1200);
    return true;
  }
  if (type == "ota.check") {
    gPendingOtaCheckAction = true;
    gPendingOtaActionAfterMs = millis() + 500;
    logf("[ACT] ota.check queued");
    setStatus("Action: OTA check", 700);
    return true;
  }
  if (type == "ota.apply") {
    gPendingOtaCheckAction = true;
    gPendingOtaApplyAction = true;
    gPendingOtaActionAfterMs = millis() + 500;
    logf("[ACT] ota.apply queued");
    setUiMode(UiMode::Ota);
    setStatus("Action: OTA apply", 700);
    appendRuntimeLog("OTA", "apply requested by device action", true);
    return true;
  }



  return false;
}

bool executeDeviceActions(JsonVariantConst actions) {
  if (actions.isNull()) {
    return false;
  }
  bool executed = false;
  if (actions.is<JsonArrayConst>()) {
    uint8_t count = 0;
    for (JsonObjectConst action : actions.as<JsonArrayConst>()) {
      if (count++ >= 4) {
        break;
      }
      executed = executeDeviceAction(action) || executed;
    }
  } else if (actions.is<JsonObjectConst>()) {
    executed = executeDeviceAction(actions.as<JsonObjectConst>());
  }
  if (executed) {
    appendRuntimeLog("ACT", "device action executed", true);
  }
  return executed;
}

bool executeDeviceActionsJson(const String& json) {
  if (json.isEmpty()) {
    return false;
  }
  DynamicJsonDocument doc(2048);  // A full rule from Claude is the largest action.
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    appendRuntimeLog("ACT", "device action json rejected", true);
    return false;
  }
  return executeDeviceActions(doc.as<JsonVariantConst>());
}

bool executeOrDeferDeviceActionsJson(const String& json) {
  if (json.isEmpty()) {
    return false;
  }
  if (gPlaybackActive) {
    gDeferredDeviceActionsJson = json;
    appendRuntimeLog("ACT", "device action deferred until audio finishes", false);
    logf("[ACT] deferred while playback active bytes=%u", static_cast<unsigned>(json.length()));
    return true;
  }
  return executeDeviceActionsJson(json);
}

void serviceDeferredDeviceActions() {
  if (gDeferredDeviceActionsJson.isEmpty() || gPlaybackActive || gRecording ||
      gSubmitting || gThinking || gOtaApplyInProgress) {
    return;
  }
  String json = gDeferredDeviceActionsJson;
  gDeferredDeviceActionsJson = "";
  logf("[ACT] run deferred bytes=%u", static_cast<unsigned>(json.length()));
  gActionsFollowSpeech = true;
  executeDeviceActionsJson(json);
  gActionsFollowSpeech = false;
}

void servicePendingOtaAction() {
  if ((!gPendingOtaCheckAction && !gPendingOtaApplyAction) || millis() < gPendingOtaActionAfterMs ||
      gPlaybackActive || gRecording || gSubmitting || gThinking || gOtaApplyInProgress ||
      gInboxHttpBusy || gRealtimeDialBusy || gRealtimeActive || gVoiceDialBusy ||
      gVoiceReplySpeechBusy || gTextTurnTaskHandle) {
    return;
  }
  bool shouldCheck = gPendingOtaCheckAction;
  bool shouldApply = gPendingOtaApplyAction;
  if (shouldApply) { setUiMode(UiMode::Ota); render(); }
  gPendingOtaCheckAction = false;
  gPendingOtaApplyAction = false;
  logf("[ACT] ota action run check=%d apply=%d", static_cast<int>(shouldCheck), static_cast<int>(shouldApply));
  appendRuntimeLog("OTA", String("action run check=") + String(static_cast<int>(shouldCheck)) +
                            " apply=" + String(static_cast<int>(shouldApply)), true);
  if (shouldCheck) {
    // Paint the key response before network work. The shared worker owns TLS;
    // completion schedules apply only after a valid fresh manifest.
    if (!startBackgroundManifestCheck(shouldApply, !shouldApply)) {
      gPendingOtaCheckAction = true;
      gPendingOtaApplyAction = shouldApply;
    }
    return;
  }
  if (shouldApply) {
    bool applyOk = applyOtaFromManifest();
    logf("[ACT] ota.apply result=%d", static_cast<int>(applyOk));
  }
}

// The tab and row an app is on, as the menu shows them. Favorites is passed over: every app on it
// is in its own tab too, which is where an app opened some other way belongs.
bool findLauncherEntryForMode(UiMode mode, int& groupIndex, int& subIndex) {
  for (uint8_t g = 0; g < kLauncherGroupCount; ++g) {
    if (g == kFavoritesGroup) continue;
    const LauncherGroup& tab = launcherGroup(g);
    for (uint8_t s = 0; s < tab.count; ++s) {
      const LauncherSubItem& item = tab.items[s];
      if (item.mode == mode && (mode != UiMode::Home || item.startRealtime == gRealtimeActive)) {
        groupIndex = g;
        subIndex = s;
        return true;
      }
    }
  }
  return false;
}

// Coming back to an assistant screen isn't choosing something from the menu, so the menu stays
// on what you last used. These are also menu items, but opening one from there keeps its row.
bool launcherKeepsPlaceFor(UiMode mode) {
  return mode == UiMode::Home || mode == UiMode::ChatFull || mode == UiMode::Clock;
}

// Settings and System are for a quick change, so Tab doesn't bring the menu back to them. An app
// belongs to them by its own tab, so one that's also a favorite counts.
bool launcherAppInSettingsOrSystem(const LauncherSubItem& app) {
  const uint8_t tabs[] = {kSettingsGroup, kSystemGroup};
  for (const uint8_t g : tabs) {
    const LauncherGroup& tab = kLauncherGroups[g];
    for (uint8_t s = 0; s < tab.count; ++s)
      if (tab.items[s].mode == app.mode) return true;
  }
  return false;
}

// The row an app is on in a tab now, or -1.
int launcherRowOf(int group, const LauncherSubItem& app) {
  const LauncherGroup& tab = launcherGroup(group);
  for (uint8_t s = 0; s < tab.count; ++s)
    if (tab.items[s].mode == app.mode && !strcmp(tab.items[s].label, app.label)) return s;
  return -1;
}

// What the menu is on, kept as the app Tab opens it on, unless it's in Settings or System. A
// screensaver, Chess Mind's included, isn't using an app.
void rememberLauncherApp() {
  if (gScreensaverActive) return;
  if (gLauncherSelection < 0 || gLauncherSelection >= kLauncherGroupCount) return;
  const LauncherGroup& tab = launcherGroup(gLauncherSelection);
  if (gLauncherSubSelection < 0 || gLauncherSubSelection >= tab.count) return;
  if (launcherAppInSettingsOrSystem(tab.items[gLauncherSubSelection])) return;
  gLauncherLastUsedTab = gLauncherSelection;
  gLauncherLastUsed = tab.items[gLauncherSubSelection];
}

// Where the last app used is now: in the tab it was used from, or in its own tab if it has
// left Favorites since.
bool findLastLauncherApp(int& group, int& row) {
  if (gLauncherLastUsedTab < 0) return false;
  group = gLauncherLastUsedTab;
  row = launcherRowOf(group, gLauncherLastUsed);
  for (int g = 0; row < 0 && g < kLauncherGroupCount; ++g) {
    if (g == kFavoritesGroup) continue;
    group = g;
    row = launcherRowOf(g, gLauncherLastUsed);
  }
  return row >= 0;
}

// The menu follows the app in use, and keeps it as the last one used.
void syncLauncherToMode(UiMode mode) {
  int groupIndex = 0;
  int subIndex = 0;
  if (!findLauncherEntryForMode(mode, groupIndex, subIndex)) {
    return;
  }
  gLauncherSelection = groupIndex;
  gLauncherSubSelection = subIndex;
  gLauncherLastSub[groupIndex] = subIndex;
  rememberLauncherApp();
}

bool uiModeInterruptsRealtime(UiMode mode) {
  switch (mode) {
    case UiMode::Home:
    case UiMode::Face:
    case UiMode::ChatFull:
      return false;
    default:
      return true;
  }
}

void setUiMode(UiMode mode) {
  if (gMeshBusOwned && mode != UiMode::Mesh) {
    if (meshBlocksMode(mode)) { setStatus("Exit Mesh first: SD / EXT busy",1800); return; }
    if (!stopMeshMode(false)) return;
  }

  if (deferInternetRadioMode(mode)) return;
  if (deferVlcRemoteMode(mode)) return;
  if (mode != UiMode::Face) gRealtimeScreenVisible=false;
  if (gUiMode == UiMode::PocketSynth && mode != UiMode::PocketSynth && !stopPocketSynth()) {
    setStatus("Synth stopping; retry mode",1200);return;
  }
  if (mode == UiMode::Nes && gUiMode != UiMode::Nes && !enterNesApp()) return;
  if (mode == UiMode::Voice && gVoiceNotesBehindCard) {
    gVoiceNotesBehindCard = false;
    loadVoiceNotes();
  }
  if (gUiMode == UiMode::Nes && mode != UiMode::Nes) leaveNesApp();
  if (mode != gUiMode) gChatCommandsVisible = false;
  if (gUiMode==UiMode::BrainFlow && mode!=UiMode::BrainFlow) pauseBrainFlow();
  bool modeChanged = mode != gUiMode;
  if (modeChanged && gUiMode == UiMode::KeyRush && mode != UiMode::KeyRush && gKeyRush.running) {
    uint32_t bestScore = gKeyRush.bestScore;
    bool bestLoaded = gKeyRush.bestLoaded;
    gKeyRush = KeyRushRuntime{};
    gKeyRush.bestScore = bestScore;
    gKeyRush.bestLoaded = bestLoaded;
  }
  if (modeChanged && uiModeInterruptsRealtime(mode) &&
      (gRealtimeActive || gRealtimeConnected || gRealtimeRecording || gRealtimeAwaiting)) {
    closeRealtimeSession("mode change");
  }
  if (mode != UiMode::Files) {
    gFileDelete.cancel();
    if (gUiMode == UiMode::Files) std::vector<SdFileEntry>().swap(gFileEntries);
  }
  // Right away, not from its service, because the next app may want the Grove port.
  if (modeChanged) labOnceLeaving(mode);
  if (modeChanged && gUiMode == UiMode::AirMusic) stopAirMusic();
  if (modeChanged && gUiMode == UiMode::Flappy) stopFlappy();
  if (modeChanged && gUiMode == UiMode::Balloon) stopBalloon();
  if (modeChanged && gUiMode == UiMode::BallCatch) stopBallCatch();
  if (modeChanged && gUiMode == UiMode::Sonar) stopSonar();
  // The radio's knob and strip, before the next screen looks at the Grove port.
  if (modeChanged && gUiMode == UiMode::Radio) radioPartsGiveBack();
  if (modeChanged && labMode(gUiMode)) stopLab();
  if (modeChanged && gUiMode == UiMode::WifiSettings) stopWifiSettings();
  if (modeChanged && gUiMode == UiMode::TvRemote) stopTvRemote();  // Its Bluetooth goes with it.
  if (modeChanged && gUiMode == UiMode::Cameras) stopCameras();    // Its worker and pictures go with it.
  gUiMode = mode;
  if (modeChanged && mode == UiMode::Files) refreshFileManager();
  if (modeChanged && mode == UiMode::Chess) beginChessDemo();
  if (modeChanged && mode == UiMode::Checkers) beginCheckersDemo();
  if (modeChanged && mode == UiMode::Minesweeper) beginMinesweeper();
  if (modeChanged && mode == UiMode::Snake) beginSnake();
  if (modeChanged && mode == UiMode::AirMusic) beginAirMusic();
  if (modeChanged && mode == UiMode::Flappy) beginFlappy();
  if (modeChanged && mode == UiMode::Balloon) beginBalloon();
  if (modeChanged && mode == UiMode::BallCatch) beginBallCatch();
  if (modeChanged && labMode(mode)) beginLab();
  if (modeChanged && mode == UiMode::NfcNotes) beginNfcNotes();
  if (modeChanged && mode == UiMode::WifiSettings) beginWifiSettings();
  if (modeChanged && mode == UiMode::Radio) beginInternetRadio();
  if (modeChanged && mode == UiMode::Podcasts) beginPodcasts();
  if (modeChanged && mode == UiMode::TvRemote) beginTvRemote();
  if (modeChanged && mode == UiMode::Cameras) beginCameras();
  if (modeChanged && mode == UiMode::VlcRemote) beginVlcRemote();
  if (modeChanged && mode == UiMode::ChessMind) beginChessMind();
  if (modeChanged && mode == UiMode::PocketSynth) beginPocketSynth();
  if (modeChanged && mode == UiMode::Mesh) beginMesh();  // Its radio runs for as long as it's open.
  if (modeChanged && mode == UiMode::Ota && !gOtaBootApplyMode) {
    // Let the new screen paint before its bounded manifest request.
    gPendingOtaCheckAction = true;
    gPendingOtaActionAfterMs = millis() + 250;
  }
  if (mode == UiMode::ChatFull) gInboxUnread = false;
  bool enteringScreensaverUi = gScreensaverActive &&
      (mode == UiMode::CMatrix || mode == UiMode::Face || mode == UiMode::ChessMind);
  if (!enteringScreensaverUi && mode != UiMode::CMatrix &&
      (gScreensaverActive || gScreensaverDimmed || gScreenOffActive)) {
    gScreensaverActive = false;
    gScreensaverDimmed = false;
    gScreenOffActive = false;
    applyDisplayBrightness();
  }
  gLauncherVisible = false;
  gPlaceEditing = false;
  if (mode != UiMode::Tetris) {
    gTetrisHelpVisible = false;
  }
  if (mode == UiMode::CMatrix && modeChanged) {
    gCMatrixInitialized = false;
  }
  if (mode == UiMode::Face && modeChanged) {
    resetUncannyEyesRenderer();
  }
  if (modeChanged && (mode == UiMode::Settings || mode == UiMode::FocusSettings || mode == UiMode::ScreenSettings)) {
    gSelectedSetting = 0;
  }
  if (mode == UiMode::RfidLab) {
    initRfidLab(false);
  }
  if (gLauncherSelection >= 0 && gLauncherSelection < kLauncherGroupCount &&
      gLauncherSubSelection >= 0 && gLauncherSubSelection < launcherGroup(gLauncherSelection).count &&
      launcherGroup(gLauncherSelection).items[gLauncherSubSelection].mode == mode) {
    gLauncherLastSub[gLauncherSelection] = gLauncherSubSelection;
    if (!launcherKeepsPlaceFor(mode)) rememberLauncherApp();
  } else if (!launcherKeepsPlaceFor(mode)) {
    syncLauncherToMode(mode);
  }
  setStatus(String("Mode: ") + uiModeName(mode), 900);
  if (mode == UiMode::Notes && !gNotesScanned) {
    scanNotes();
  }
}

// The row a tab opens on: where it was left, or the top if that row has gone. An empty
// Favorites tab has no rows, and sits on 0 so nothing reads before its list.
int launcherRowFor(int group) {
  const uint8_t count = launcherGroup(group).count;
  return count ? clampValue<int>(gLauncherLastSub[group], 0, count - 1) : 0;
}

void showLauncherGroup(int group) {
  gLauncherSelection = group;
  gLauncherSubSelection = launcherRowFor(group);
}

// Tab opens the menu on the last app used, so a trip to Settings or System doesn't leave it there.
// Before there is one it opens where it is, unless that's in Settings or System: then on AI Agent.
void openLauncherOnLastApp() {
  int group = 0, row = 0;
  if (findLastLauncherApp(group, row)) {
    gLauncherSelection = group;
    gLauncherSubSelection = row;
    gLauncherLastSub[group] = row;
  } else {
    const LauncherGroup& tab = launcherGroup(gLauncherSelection);
    if (gLauncherSelection == kSettingsGroup || gLauncherSelection == kSystemGroup ||
        (gLauncherSubSelection >= 0 && gLauncherSubSelection < tab.count &&
         launcherAppInSettingsOrSystem(tab.items[gLauncherSubSelection])))
      showLauncherGroup(kFavoritesGroup + 1);
  }
  gLauncherVisible = true;
}

void moveLauncherSelection(int delta) {
  gLauncherVisible = true;
  showLauncherGroup((gLauncherSelection + delta + kLauncherGroupCount) % kLauncherGroupCount);
}

void moveLauncherSubSelection(int delta) {
  gLauncherVisible = true;
  uint8_t count = launcherGroup(gLauncherSelection).count;
  if (count <= 1) {
    moveLauncherSelection(delta);
    return;
  }
  gLauncherSubSelection = (gLauncherSubSelection + delta + count) % count;
  gLauncherLastSub[gLauncherSelection] = gLauncherSubSelection;
}

void applyLauncherSelection() {
  gLauncherSelection = clampValue<int>(gLauncherSelection, 0, kLauncherGroupCount - 1);
  uint8_t count = launcherGroup(gLauncherSelection).count;
  if (!count) return;  // An empty Favorites tab.
  gLauncherSubSelection = clampValue<int>(gLauncherSubSelection, 0, count - 1);
  gLauncherLastSub[gLauncherSelection] = gLauncherSubSelection;
  // A copy: opening an app can't change Favorites today, but the row is read after it opens.
  const LauncherSubItem item = launcherGroup(gLauncherSelection).items[gLauncherSubSelection];
  UiMode target = item.mode;
  if (gMeshBusOwned && item.startRealtime) {setStatus("Esc exits Mesh for voice",1600);return;}
  if (gRecording) cancelRecordingDiscard();
  if (!item.startRealtime) {
    gPendingRealtimeStart = false;
    gRealtimeScreenVisible = false;
    if (gRealtimeActive || gRealtimeExclusiveMode) closeRealtimeSession("app menu");
  }
  setUiMode(item.startRealtime ? UiMode::Face : target);
  // Idempotent open, not Ctrl+R toggle: reopening must not commit/stop audio.
  if (item.startRealtime) {
    gRealtimeScreenVisible = true;
    if (!gRealtimeActive) startRealtimeSession(true);
  }
  // Opened from Favorites, the menu stays there, so it comes back to the list it was used from.
  if (gLauncherSelection != kFavoritesGroup) syncLauncherToMode(target);
  else rememberLauncherApp();
  saveLauncherSelection();
}
