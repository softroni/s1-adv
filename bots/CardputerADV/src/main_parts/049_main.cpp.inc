  if (escDown) {
    if (!voiceToolEscape() && !routeBrainFlowEscape() && !routeLabEscape()) {
      handleGlobalEscape();
    }
  } else if (gChatCommandsVisible) {
    if (tabDown) handleTypingKey('\t',keys);
    else if (upDown || leftDown) gChatCommandSelection=(gChatCommandSelection+kChatCommandCount-1)%kChatCommandCount;
    else if (downDown || rightDown) gChatCommandSelection=(gChatCommandSelection+1)%kChatCommandCount;
    else if (enterDown) runChatCommand(gChatCommandSelection);
    else if (charDown) handleTypingKey(curWordChar,keys);
  } else if (isAssistantUi() && !gLauncherVisible && keys.ctrl && (leftDown || rightDown)) {
    runChatCommand(leftDown?2:3);

  } else if (altVoiceTap && (isAssistantUi() || voiceToolOnScreen()) && !gLauncherVisible) {
    toggleVoiceByTap();
  } else if (optionViewToggle) {
    togglePulseChatDisplay();
	  } else if (gUiMode == UiMode::Tetris && (tabDown || tabPrevCombo || tabNextCombo)) {
	    handleTypingKey('\t', keys);
	  } else if (tabPrevCombo) {
	    switchLauncherGroupAndApply(-1);
  } else if (tabNextCombo) {
    switchLauncherGroupAndApply(1);
  } else if (tabDown) {
    handleTypingKey('\t',keys);
  } else if (gLauncherVisible && upDown) {
    moveLauncherSubSelection(-1);
  } else if (gLauncherVisible && downDown) {
    moveLauncherSubSelection(1);
  } else if (gLauncherVisible && leftDown) {
    moveLauncherSelection(-1);
  } else if (gLauncherVisible && rightDown) {
    moveLauncherSelection(1);
  } else if ((gUiMode == UiMode::Mesh || gUiMode == UiMode::NfcNotes || gUiMode == UiMode::WifiSettings) && (upDown || downDown)) {
    handleTypingKey(upDown ? char(1) : char(2),keys);
  } else if (gUiMode == UiMode::Mesh && (leftDown || rightDown)) {
    handleTypingKey(leftDown ? char(3) : char(4),keys);  // Who Enter sends to.
  } else if (labMode(gUiMode) && (upDown || downDown || leftDown || rightDown)) {
    handleTypingKey(upDown ? ';' : downDown ? '.' : leftDown ? ',' : '/', keys);
  } else if (gUiMode == UiMode::Files && (upDown || downDown || leftDown || rightDown)) {
    handleTypingKey((upDown || leftDown) ? ',' : '.', keys);
  } else if ((radioTakesArrows() || podcastTakesArrows()) && (upDown || downDown || leftDown || rightDown)) {
    // Find and its results: the arrows move, so the letters can be typed or mean themselves.
    handleTypingKey(upDown ? char(1) : downDown ? char(2) : leftDown ? char(3) : char(4), keys);
  } else if (gUiMode == UiMode::TvRemote && (upDown || downDown || leftDown || rightDown)) {
    // A USB keyboard's arrows, as the remote's and the typing screen's own.
    handleTypingKey(upDown ? char(1) : downDown ? char(2) : leftDown ? char(3) : char(4), keys);
  } else if ((gUiMode == UiMode::Minesweeper || gUiMode == UiMode::Snake || gUiMode == UiMode::Balloon || gUiMode == UiMode::BallCatch || gUiMode == UiMode::Radio || gUiMode == UiMode::VlcRemote) && (upDown || downDown || leftDown || rightDown)) {
    handleTypingKey(upDown ? 'w' : downDown ? 's' : leftDown ? 'a' : 'd', keys);
  } else if ((gUiMode == UiMode::Chess || gUiMode == UiMode::Checkers) && (upDown || downDown || leftDown || rightDown)) {
    handleTypingKey(upDown ? 'w' : downDown ? 's' : leftDown ? 'a' : 'd', keys);
  } else if (gUiMode == UiMode::SmartLamp && upDown) {
    handleTypingKey('w', keys);
  } else if (gUiMode == UiMode::SmartLamp && downDown) {
    handleTypingKey('s', keys);
  } else if (gUiMode == UiMode::SmartLamp && leftDown) {
    handleTypingKey('a', keys);
  } else if (gUiMode == UiMode::SmartLamp && rightDown) {
    handleTypingKey('d', keys);
  } else if (isSettingsMode(gUiMode) && (upDown || downDown || leftDown || rightDown)) {
    handleSettingsNavigation(upDown,downDown,leftDown,rightDown);
  } else if (!gLauncherVisible && gPlaybackActive && upDown) {
    adjustSpeakerVolume(24);
    setKeyDiag("vol:up");
  } else if (!gLauncherVisible && gPlaybackActive && downDown) {
    adjustSpeakerVolume(-24);
    setKeyDiag("vol:down");
  } else if (gUiMode == UiMode::ChatFull && upDown) {
    logf("[CHAT] scroll up arrow");
    scrollChatBy(1);
  } else if (gUiMode == UiMode::ChatFull && downDown) {
    logf("[CHAT] scroll down arrow");
    scrollChatBy(-1);
  } else if (gUiMode == UiMode::Notes && (leftDown || upDown)) {
    handleTypingKey(',', keys);
  } else if (gUiMode == UiMode::Notes && (rightDown || downDown)) {
    handleTypingKey('.', keys);
  } else if (gUiMode == UiMode::Alarm && (leftDown || upDown)) {
    handleTypingKey('-', keys);
  } else if (gUiMode == UiMode::Alarm && (rightDown || downDown)) {
    handleTypingKey('+', keys);
  } else if (gUiMode == UiMode::Calendar && (leftDown || upDown)) {
    handleTypingKey(',', keys);
  } else if (gUiMode == UiMode::Calendar && (rightDown || downDown)) {
    handleTypingKey('.', keys);
  } else if (gUiMode == UiMode::IrRemote && (leftDown || upDown)) {
    handleTypingKey(',', keys);
  } else if (gUiMode == UiMode::IrRemote && (rightDown || downDown)) {
    handleTypingKey('.', keys);

  } else if (gUiMode == UiMode::Tetris && leftDown) {
    handleTypingKey(',', keys);
  } else if (gUiMode == UiMode::Tetris && rightDown) {
    handleTypingKey('.', keys);
  } else if (gUiMode == UiMode::Tetris && upDown) {
    handleTypingKey('w', keys);
  } else if (gUiMode == UiMode::Tetris && downDown) {
    handleTypingKey('s', keys);
  } else if (spaceDown && keys.ctrl) {
    handleTypingKey(' ', keys);
  } else if (enterDown) {
    handleTypingKey('\n', keys);
  } else if (delDown) {
    handleTypingKey('\b', keys);
  } else if (charDown) {
    handleTypingKey(curWordChar, keys);
  } else if ((keys.fn || keys.ctrl || keys.alt || keys.opt) && !wifiSettingsTyping()) {
    setKeyDiag("mods=" + keyboardSignatureFromState());
  }

  rememberInputState();
	}

bool ctrlAlonePressedNow() {
  return gKeyboardState.ctrl && !gKeyboardState.fn && !gKeyboardState.shift &&
         !gKeyboardState.alt && !gKeyboardState.opt && !gKeyboardState.tab &&
         !gKeyboardState.enter && !gKeyboardState.del && !gKeyboardState.space &&
         gKeyboardState.word.empty();
}

// One tap listens and the next sends, for when holding G0 is too much. It has to work whenever
// it's pressed, because a tap can't be held down until the device is ready.
void toggleVoiceByTap() {
  if (gRecording) {
    logf("[VOICE] Alt stop");
    stopRecordingAndSend();
    return;
  }
  if (gG0VoicePending) {
    gG0VoicePending = false;
    gVoiceStartLatched = false;
    setStatus("Voice cancelled", 900);
    return;
  }
  if (gSubmitting || gThinking) {
    setStatus("Sending; tap Alt again", 1200);
    return;
  }
  if (gPlaybackActive) stopPlaybackStream();  // Talking over the reply is the natural moment.
  logf("[VOICE] Alt start");
  if (gInboxHttpBusy) {
    gG0VoicePending = true;
    gVoiceStartLatched = true;
    gInboxCancelRequested = true;
    setStatus("Loading voice...", 0);
    render();
    return;
  }
  startRecording();
}

void updateVoiceTrigger() {
  bool held = ctrlAlonePressedNow();
  bool ctrlComboActive = gKeyboardState.ctrl &&
                         (!gKeyboardState.word.empty() || gKeyboardState.space ||
                          gKeyboardState.tab || gKeyboardState.enter ||
                          gKeyboardState.del || gKeyboardState.fn ||
                          gKeyboardState.shift || gKeyboardState.alt ||
                          gKeyboardState.opt);
  if (gRecording && millis() - gRecordStartMs >= currentRecordLimitMs()) {
    logf("[VOICE] auto-stop at max duration");
    stopRecordingAndSend();
    return;
  }
  if (ctrlComboActive) {
    gCtrlSoloCandidate = false;
    gLastCtrlTapMs = 0;
  }
  if (held && !gCtrlSoloHeld) {
    gCtrlSoloHeld = true;
    gCtrlSoloCandidate = !ctrlComboActive;
    uint32_t now = millis();
    if (gCtrlSoloCandidate && gLastCtrlTapMs != 0 &&
        now - gLastCtrlTapMs <= kCtrlDoubleTapWindowMs) {
      gLastCtrlTapMs = 0;
      gCtrlSoloCandidate = false;
      if (gRecording) {
        logf("[VOICE] ctrl double tap stop");
        stopRecordingAndSend();
      } else {
        logf("[VOICE] ctrl double tap start");
        startRecording();
      }
      return;
    }
    if (gCtrlSoloCandidate) {
      gLastCtrlTapMs = now;
      setStatus(gRecording ? "Ctrl x2 stop" : "Ctrl x2 start", 600);
    }
    return;
  }
  if (gCtrlSoloHeld && !held) {
    gCtrlSoloHeld = false;
    gCtrlSoloCandidate = false;
  }
  if (gLastCtrlTapMs != 0 && millis() - gLastCtrlTapMs > kCtrlDoubleTapWindowMs) {
    gLastCtrlTapMs = 0;
  }
}

void updateG0VoiceTrigger() {
  if (gMeshBusOwned) {
    gG0VoicePending=false;
    if(M5Cardputer.BtnA.wasPressed()) {gMeshNotice="Esc exits Mesh for voice";gMeshNoticeUntilMs=millis()+1500;}
    return;
  }
  bool pressed = M5.BtnA.isPressed();
  // G0 is push-to-talk even when the clock/screen saver is asleep. Do not
  // consume the first press solely to wake the display.
  if (pressed && !gG0VoiceHeld) {
    gG0VoiceRequestedMs = millis();
    noteUiInput("G0");
    handleScreensaverWake();
  }
  if (!gG0VoicePending) gVoiceStartLatched = false;
  if ((pressed || gVoiceStartLatched) && gG0VoicePending && !gInboxHttpBusy) {
    gG0VoicePending = false;
    gVoiceStartLatched = false;
    startRecording();
  }
  if (gUiMode == UiMode::Ota) {
    if (pressed && !gG0VoiceHeld) {
      gG0VoiceHeld = true;
      pulseButtonPress();
      setStatus("Release G0 to apply OTA", 1200);
      appendRuntimeLog("OTA", "G0 press", false);
      return;
    }
    if (!pressed && gG0VoiceHeld) {
      gG0VoiceHeld = false;
      setStatus("OTA apply requested", 700);
      appendRuntimeLog("OTA", "apply requested by G0", true);
      render();
      applyOtaFromManifest();
      return;
    }
    return;
  }
  if (pressed && !gG0VoiceHeld) {
    gG0VoiceHeld = true;
    gLastCtrlTapMs = 0;
    gCtrlSoloCandidate = false;
    if (!gRecording) {
      logf("[VOICE] G0 hold start");
      if (gInboxHttpBusy) {
        gG0VoicePending = true;
        gInboxCancelRequested = true;
        setStatus("Loading voice...", 0);
        render();
      } else startRecording();
    }
    return;
  }
  if (!pressed && gG0VoiceHeld) {
    gG0VoiceHeld = false;
    if (gG0VoicePending) setStatus("Voice cancelled", 900);
    gG0VoicePending = false;
    if (gRecording) {
      logf("[VOICE] G0 hold stop");
      stopRecordingAndSend();
    }
  }
}

void updateBlink() {
  uint32_t now = millis();
  if (gBlinkEndMs == 0 && now >= gNextBlinkMs) {
    gBlinkEndMs = now + 120;
  } else if (gBlinkEndMs != 0 && now >= gBlinkEndMs) {
    gBlinkEndMs = 0;
    scheduleBlink();
  }
}
