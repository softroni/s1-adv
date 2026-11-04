void cycleCurrentAppScreen(int delta) {
  syncLauncherToMode(gUiMode);
  gLauncherVisible = false;
  uint8_t count = launcherGroup(gLauncherSelection).count;
  if (!count) return;
  gLauncherSubSelection = (gLauncherSubSelection + delta + count) % count;
  gLauncherLastSub[gLauncherSelection] = gLauncherSubSelection;
  applyLauncherSelection();
}

void togglePulseChatDisplay() {
  gLauncherVisible = false;
  if (gUiMode == UiMode::Home) {
    setUiMode(UiMode::ChatFull);
    setStatus("View: full chat", 900);
  } else if (gUiMode == UiMode::ChatFull) {
    setUiMode(UiMode::Home);
    setStatus("View: pulse", 900);
  } else {
    setUiMode(UiMode::Home);
    setStatus("View: pulse", 900);
  }
}

// Tab with an arrow opens the next tab's app. Favorites is passed over: its apps are all in
// other tabs already, and an empty one has nothing to open.
void switchLauncherGroupAndApply(int delta) {
  syncLauncherToMode(gUiMode);
  gLauncherVisible = false;
  int group = gLauncherSelection;
  do group = (group + delta + kLauncherGroupCount) % kLauncherGroupCount;
  while (group == kFavoritesGroup);
  showLauncherGroup(group);
  applyLauncherSelection();
}

size_t replyVisualLineCount(const String& reply) {
  String full = "AI: " + reply;
  auto wrapped = wrapText(full, 216);
  return wrapped.size();
}

void appendAssistantReply(const String& reply) {
  String displayReply = cleanReplyForDevice(reply);
  if (displayReply.isEmpty()) {
    return;
  }
  flashActivityLed(0, 180, 0);
  pushAssistant(displayReply);
}

bool setupKeyboardInput() {
  // M5Cardputer 1.1.1 owns the ADV TCA8418 reader and its IRQ/state machine.
  // It is initialized by M5Cardputer.begin(..., true).
  gKeyboardState = M5Cardputer.Keyboard.keysState();
  logf("[KEY] using M5Cardputer ADV keyboard reader");
  setKeyDiag("kbd:m5-adv");
  return true;
}

void refreshKeyboardState() {
  // M5Cardputer.update() has already advanced the built-in keyboard reader.
  gKeyboardState = M5Cardputer.Keyboard.keysState();
  // The key log can be uploaded, and this would write a password into it.
  if (wifiSettingsTakesAllKeys()) {
    gLastKeySignature = "";
    return;
  }
  String sig = keyboardSignatureFromState();
  if (sig != gLastKeySignature) {
    gLastKeySignature = sig;
    setKeyDiag(sig.isEmpty() ? "-" : sig);
    if (!sig.isEmpty()) {
      logf("[KEY] %s", sig.c_str());
    }
  }
}
