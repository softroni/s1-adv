struct FocusRuntime {
  FocusState state = FocusState::Stopped;
  FocusState stateBeforePause = FocusState::Focus;
  uint32_t remainingSec = kFocusDefaultSec;
  uint32_t sessionTotalSec = kFocusDefaultSec;
  uint32_t lastTickMs = 0;
  uint16_t cycle = 1;
  uint16_t completedToday = 0;
  uint32_t focusedTodaySec = 0;
  uint32_t nextMetronomeMs = 0;
  bool helpVisible = false;
};

focus_lamp::Link gFocusLampLink;
IPAddress gFocusLampIp;
FocusState gPendingFocusCue = FocusState::Stopped;
bool gFocusCompletionTonePending=false;
uint32_t gPendingFocusCueAtMs = 0;
ui_frame::Policy gUiFramePolicy;

struct LauncherSubItem {
  const char* label;
  UiMode mode;
  bool startRealtime;
  constexpr LauncherSubItem(const char* text = "", UiMode target = UiMode::Home, bool realtime = false)
      : label(text), mode(target), startRealtime(realtime) {}
};

struct LauncherGroup {
  const char* title;
  const char* hint;
  uint16_t accent;
  uint8_t count;
  LauncherSubItem items[kLauncherMaxSubItems];
};

struct IrRemoteSlot {
  IRData receivedIRData = {};
  uint8_t rawCode[RAW_BUFFER_LENGTH] = {};
  uint16_t rawCodeLength = 0;
  bool learned = false;
  uint32_t learnedAtMs = 0;
};

struct IrRemotePersistSlot {
  uint32_t magic = kIrPersistMagic;
  uint8_t learned = 0;
  uint8_t reserved = 0;
  uint16_t rawCodeLength = 0;
  IRData receivedIRData = {};
  uint8_t rawCode[RAW_BUFFER_LENGTH] = {};
};

struct EmojiCacheEntry {
  uint32_t codepoint = 0;
  uint8_t* data = nullptr;
  uint32_t len = 0;
  int16_t width = 0;
  int16_t height = 0;
};

struct SmartLampRuntime {
  IPAddress ip;
  bool udpReady = false;
  bool found = false;
  bool online = false;
  bool power = true;
  bool presetsLoaded = false;
  bool presetsLearned = false;
  uint8_t effect = 0;
  uint8_t brightness = 100;
  uint8_t speed = 200;
  uint8_t scale = 100;
  uint8_t selected = 0;
  uint32_t lastSeenMs = 0;
  uint32_t lastCommandMs = 0;
  uint32_t presetSendDueMs = 0;
  cardputer_smartlamp::PresetBank presets;
  String status = "SEARCHING";
};

struct KeyRushRuntime {
  bool running = false;
  bool finished = false;
  bool bestLoaded = false;
  bool feedbackCorrect = true;
  uint8_t targetIndex = 0;
  uint8_t typedCount = 0;
  uint16_t words = 0;
  uint16_t streak = 0;
  uint16_t bestStreak = 0;
  uint32_t score = 0;
  uint32_t bestScore = 0;
  uint32_t correctKeys = 0;
  uint32_t mistakeKeys = 0;
  uint32_t startedMs = 0;
  uint32_t endsAtMs = 0;
  uint32_t feedbackUntilMs = 0;
};

// Favorites has no apps of its own here: gFavoritesTab, below, holds the ones the owner picked.
constexpr LauncherGroup kLauncherGroups[kLauncherGroupCount] = {
    {"Favorites", "Your apps, in your order", rgb565_local(255, 255, 0), 0, {}},
    {"AI Agent", "Assistant and chat", rgb565_local(47, 227, 255), 3,
     {{"Realtime", UiMode::Home, true}, {"Pulse", UiMode::Home}, {"AI Chat", UiMode::ChatFull}}},
    {"Media", "Listen and capture", rgb565_local(88, 240, 141), 9,
     {{"Library", UiMode::Library}, {"Folders", UiMode::AudioFolders}, {"Voice Notes", UiMode::Voice},
      {"Notes", UiMode::Notes}, {"Assets", UiMode::Assets}, {"Files", UiMode::Files}, {"Internet Radio", UiMode::Radio},
      {"Podcasts", UiMode::Podcasts}, {"VLC Remote", UiMode::VlcRemote}}},
    {"Focus", "Time and attention", rgb565_local(255,136,64), 8,
     {{"Pomodoro", UiMode::Focus}, {"Alarm", UiMode::Alarm},
      {"Stopwatch", UiMode::Stopwatch}, {"Calendar", UiMode::Calendar}, {"Chess Mind", UiMode::ChessMind},
      {"Chess", UiMode::Chess}, {"BrainFlow", UiMode::BrainFlow}, {"KeyRush", UiMode::KeyRush}}},
    {"Tools", "Local hardware tools", rgb565_local(255, 210, 64), 8,
     {{"Calculator", UiMode::Calculator}, {"NFC Notes", UiMode::NfcNotes}, {"RFID Lab", UiMode::RfidLab},
      {"IR Remote", UiMode::IrRemote}, {"smartLamp", UiMode::SmartLamp}, {"Sonar", UiMode::Sonar}, {"Mesh", UiMode::Mesh},
      {"Pocket Synth", UiMode::PocketSynth}}},
    {"Games", "Keyboard and board games", rgb565_local(157, 102, 255), 5,
     {{"Tetris", UiMode::Tetris}, {"NES", UiMode::Nes}, {"Checkers", UiMode::Checkers}, {"Minesweeper", UiMode::Minesweeper}, {"Snake", UiMode::Snake}}},
    {"Arcade", "Sensor and joystick games", rgb565_local(255, 126, 182), 4,
     {{"Air Music", UiMode::AirMusic}, {"Flappy Bird", UiMode::Flappy}, {"Hot Air Balloon", UiMode::Balloon}, {"Ball Catch", UiMode::BallCatch}}},
    {"Settings", "All device settings", rgb565_local(203, 216, 226), 4,
     {{"Device", UiMode::Settings}, {"Display", UiMode::ScreenSettings},
      {"Wi-Fi", UiMode::WifiSettings}, {"Pomodoro", UiMode::FocusSettings}}},
    {"System", "Firmware and diagnostics", rgb565_local(180, 150, 255), 3,
     {{"Firmware", UiMode::Ota}, {"Logs", UiMode::Logs}, {"C-Matrix", UiMode::CMatrix}}},
    {"Lab", "Test devices and play rules", rgb565_local(182, 255, 0), 3,
     {{"Devices", UiMode::LabDevices}, {"Rules", UiMode::LabRules}, {"Sequences", UiMode::LabSequences}}},
};
static_assert(launcher_favorites::kMax == kLauncherMaxSubItems, "a favorite needs a row and a number key");

// What's on the Favorites tab, saved as "favs". A favorite names an app by its place in
// kLauncherGroups, so moving apps around their tabs leaves it alone.
launcher_favorites::List gFavorites;
// The order of every other tab, saved as "menu_order": row r of tab g is app
// gLauncherOrder[g][r] of kLauncherGroups[g]. Favorites keeps its order in gFavorites.
uint8_t gLauncherOrder[kLauncherGroupCount][kLauncherMaxSubItems];
// What the menu shows: each tab with its rows in that order, rebuilt after every change, so the
// menu's code reads rows the way they're on screen.
LauncherGroup gLauncherTabs[kLauncherGroupCount];
LauncherGroup& gFavoritesTab = gLauncherTabs[kFavoritesGroup];

const LauncherGroup& launcherGroup(int group) {
  return gLauncherTabs[group];
}

// The app a row of a tab other than Favorites shows, as its place in kLauncherGroups[group].
uint8_t launcherTableItem(int group, int row) {
  return gLauncherOrder[group][row];
}

void rebuildFavoritesTab() {
  gFavoritesTab = kLauncherGroups[kFavoritesGroup];
  gFavoritesTab.count = gFavorites.count;
  for (uint8_t i = 0; i < gFavorites.count; ++i)
    gFavoritesTab.items[i] = kLauncherGroups[gFavorites.refs[i].group].items[gFavorites.refs[i].item];
}

void rebuildLauncherTab(int group) {
  if (group == kFavoritesGroup) {
    rebuildFavoritesTab();
    return;
  }
  LauncherGroup& tab = gLauncherTabs[group];
  tab = kLauncherGroups[group];
  for (uint8_t r = 0; r < tab.count; ++r) tab.items[r] = kLauncherGroups[group].items[gLauncherOrder[group][r]];
}

void rebuildLauncherTabs() {
  for (uint8_t g = 0; g < kLauncherGroupCount; ++g) rebuildLauncherTab(g);
}

// Until the settings load, every tab is in the table's order.
struct LauncherTabsAtStart {
  LauncherTabsAtStart() {
    launcher_order::reset(gLauncherOrder, kLauncherGroups, kLauncherGroupCount);
    rebuildLauncherTabs();
  }
} gLauncherTabsAtStart;

Preferences gPrefs;
WebSocketsClient gWs;
RealtimeSocket gRealtimeWs;
DNSServer gSetupDns;
WebServer gSetupServer(80);
String gSetupPortalPassword;
String gSetupPortalCsrf;
WiFiUDP gSmartLampUdp;
M5Canvas gCanvasBuffer(&M5Cardputer.Display);
lgfx::LGFXBase& uiCanvas();
// Drawing uses the current surface; allocation/presentation always names the
// backing sprite explicitly. The network worker never accesses either surface.
#define gCanvas uiCanvas()
SPIClass gSdSpi(FSPI);
std::unique_ptr<s1_mesh::RadioSession> gMeshSession;
bool gMeshBusOwned = false, gMeshCancelStart = false, gMeshExitAfterStop = false;
m5::I2C_Class* gMeshCapBus = nullptr;
uint8_t gMeshCapBits[3] = {};
bool gMeshCapBitsSaved = false;
String gMeshNotice;
uint32_t gMeshNoticeUntilMs=0;

MFRC522 gRfidReader(kRfidCsPin, kRfidResetPin);

String gDeviceId;
String gDisplayName = kDefaultDisplayName;
String gWifiSsid;
String gWifiPassword;
String gHubHost;
String gDeviceToken;
String gRealtimeExtraHeaders;
uint16_t gHubPort = kDefaultHubPort;
bool gSetupPortalActive = false;
bool gSetupPortalStarted = false;
bool gSetupPortalSaved = false;
uint32_t gSetupPortalLastReconnectMs = 0;
String gSetupPortalSsid;
String gSetupPortalMessage;

std::deque<ChatLine> gChatLines;
String gInputBuffer;
String gStatusText = "Booting";
String gCalcExpression;
String gCalcResult = "0";
String gCalcStatus = "Ready";
String gIrRemoteStatus = "IR idle";
String gRfidStatus = "RFID idle";
String gRfidUid = "-";
String gRfidType = "-";
String gRfidCopyStatus = "No dump";
String gStopwatchStatus = "Ready";
String gTetrisStatus = "Ready";
String gAlarmStatus = "Ready";
String gAlarmEditBuffer;
String gDeferredDeviceActionsJson;
bool gPendingOtaCheckAction = false;
uint32_t gHomeUpdatePreviewUntilMs = 0;
bool gPendingOtaApplyAction = false;
uint32_t gPendingOtaActionAfterMs = 0;
std::vector<VoiceNote> gVoiceNotes;
std::vector<NoteFile> gNotes;
std::vector<AudioAsset> gAudioAssets;
std::vector<AudioFolder> gAudioFolders;
runtime_log::Ring<kMaxRuntimeLogLines, kRuntimeLogChars> gRuntimeLogs;
portMUX_TYPE gRuntimeLogMux = portMUX_INITIALIZER_UNLOCKED;
IrRemoteSlot gIrRemoteSlots[kIrRemoteSlotCount];
RfidClassicDump gRfidDump;
SmartLampRuntime gSmartLamp;
KeyRushRuntime gKeyRush;

bool gWifiReady = false;
bool gWsReady = false;
bool gPendingRestart = false;
bool gSubmitting = false;
bool gThinking = false;
bool gTypingKeyHeld = false;
bool gVoiceTriggerHeld = false;
bool gRecording = false;
bool gPlaybackActive = false;
bool gResumeWsAfterVoice = false;
bool gImuEnabled = false;
bool gDebugOverlayVisible = false;
bool gStorageReady = false;
bool gLittleFsReady = false;
bool gSdReady = false;
bool gLauncherVisible = false;
bool gAssistantPendingVisible = false;
bool gNotesScanned = false;
bool gPostInstallStoryPending = false;
uint32_t gPostInstallStoryNextMs = 0;
uint8_t gPostInstallStoryAttempts = 0;
int gChatScrollOffset = 0;
int gChatVisibleRows = 0; // Measured by the last chat render; depends on text size.
int gLauncherSelection = 0;
int gLauncherSubSelection = 0;
uint8_t gLauncherLastSub[kLauncherGroupCount] = {};
// Says what a key just did in the menu, over its bottom row for a moment. setStatus can't be seen there.
String gLauncherNotice;
uint32_t gLauncherNoticeUntilMs = 0;
int gSelectedVoiceNote = 0;
int gSelectedNote = 0;
int gNoteScrollOffset = 0;
int gNoteListScrollOffset = 0;
int gSelectedAudioAsset = 0;
int gSelectedAudioFolder = 0;
String gAudioFolderFilter;
uint32_t gInboxLastSeq = 0;
uint32_t gLastInboxAutoPollMs = 0;
String gInboxLastText;
String gInboxLastKind;
String gInboxLastVoiceUrl;
String gInboxLastVoiceTitle;
String gInboxLastVoiceSha256;
uint32_t gInboxLastVoiceSize = 0;
String gInboxLastSender;
int gLogScrollOffset = 0;
int gSelectedSetting = 0;
int gCalendarMonthOffset = 0;
int gSelectedIrRemoteSlot = 0;

uint32_t gLastRenderMs = 0;
uint32_t gLastUiSlowLogMs = 0;
uint32_t gStatusUntilMs = 0;
uint32_t gRecordStartMs = 0;
uint32_t gBlinkEndMs = 0;
uint32_t gNextBlinkMs = 0;
uint32_t gLastImuSampleMs = 0;
uint32_t gLastWifiReconnectMs = 0;
network_recovery::Policy gNetworkRecovery;
uint32_t gLastWsReconnectMs = 0;
uint32_t gLastCtrlTapMs = 0;
uint32_t gLastClockRenderMs = 0;
uint32_t gLastTimeSyncAttemptMs = 0;
uint32_t gLastTimeSyncMs = 0;
uint32_t gLastLauncherToggleMs = 0;
uint32_t gLedFlashUntilMs = 0;
uint32_t gLastBatterySampleMs = 0;
uint32_t gLastBatteryHistorySampleMs = 0;
uint32_t gClientTurnSeq = 0;
uint32_t gLastBlockingRenderPumpMs = 0;
bool gBlockingRenderPumpEnabled = false;
bool gStopwatchRunning = false;
uint32_t gStopwatchElapsedMs = 0;
uint32_t gStopwatchStartedMs = 0;
uint32_t gStopwatchLapMs = 0;
uint8_t gTetrisBoard[16][10] = {};
uint8_t gTetrisPiece = 0;
uint8_t gTetrisRotation = 0;
uint8_t gTetrisNextPiece = 1;
int8_t gTetrisX = 3;
int8_t gTetrisY = 0;
uint32_t gTetrisScore = 0;
uint16_t gTetrisLines = 0;
uint8_t gTetrisLevel = 1;
uint32_t gTetrisLastDropMs = 0;
bool gTetrisStarted = false;
bool gTetrisPaused = false;
bool gTetrisGameOver = false;
bool gTetrisRightHandPortrait = true;
bool gTetrisHelpVisible = false;
bool gAlarmRinging = false;
bool gIrRemoteReady = false;
bool gIrRemoteLearning = false;
bool gIrRemoteAutoLearning = false;
bool gRfidBusReady = false;
bool gRfidReady = false;
bool gRfidHelpVisible = false;
uint32_t gAlarmTriggeredAtMs = 0;
uint32_t gAlarmNextBeepMs = 0;
uint32_t gIrRemoteLearnStartedMs = 0;
uint32_t gIrLastReceivePinProbeMs = 0;
uint32_t gRfidLastPollMs = 0;
uint32_t gRfidLastInitMs = 0;
uint8_t gRfidVersionReg = 0;
uint8_t gRfidUidSize = 0;
uint8_t gRfidSak = 0;
uint16_t gRfidLastWriteBlocks = 0;
bool gRfidLastUidWriteOk = false;
uint8_t gIrReceivePinIndex = 0;
time_t gAlarmSnoozeEpoch = 0;
int16_t gAlarmLastTriggerYear = -1;
int16_t gAlarmLastTriggerYday = -1;

struct SdFileEntry { String name; String path; uint64_t size; bool directory; };
std::vector<SdFileEntry> gFileEntries;
String gFilesDir = "/", gFilesStatus = "R to refresh";
int gSelectedFile = 0;
bool gFilesTruncated = false;
sd_file_manager::DeletePrompt gFileDelete;
