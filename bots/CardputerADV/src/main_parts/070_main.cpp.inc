// Internet radio: clock/Pulse palette and thin rings, separate streaming owner.
namespace {
radio_catalog::Selection gRadioSelection;
bool gRadioWantPlaying = false;
bool gRadioStartPending = false;
bool gRadioModePending = false;
UiMode gRadioNextMode = UiMode::Home;
String gRadioNotice;

bool radioBusy() { return internet_radio::busy(); }
void radioInvalidate() { gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
void stopInternetRadio() {
  gRadioWantPlaying = false;
  gRadioStartPending = false;
  internet_radio::stop();
  radioInvalidate();
}
bool deferInternetRadioMode(UiMode mode) {
  if (!radioBusy() || mode == UiMode::Radio) return false;
  stopInternetRadio(); gRadioModePending = true; gRadioNextMode = mode;
  setStatus("Stopping radio...", 800);
  return true; // Do not reclaim the speaker/RAM before the worker exits.
}
void beginInternetRadio() { gRadioNotice = "Enter to listen"; radioInvalidate(); }
bool radioStartAllowed() {
  return gWifiReady && !gBootBackgroundActive && gCanvasSpriteActive && !gRecording &&
    !gSubmitting && !gThinking && !gPlaybackActive && !gTransferUiActive && !gOtaApplyInProgress &&
    !gInboxHttpBusy && !gVoiceDialBusy && !gVoiceReplySpeechBusy && gQueuedVoiceReplySpeech.isEmpty() &&
    !gRealtimeActive && !gRealtimeDialBusy && !gRealtimeExclusiveMode && !gPendingRealtimeStart &&
    !gMeshBusOwned && !gTextTurnTaskHandle && !gCompletedTextTurn && gPendingTextTurns.empty() &&
    gActiveTextTurn.isEmpty() && !gG0VoicePending && !gVoiceCapturePreparing &&
    !M5Cardputer.Speaker.isPlaying();
}
void serviceInternetRadio() {
  if (gRadioModePending && !radioBusy()) {
    gRadioModePending = false; const UiMode next = gRadioNextMode;
    setUiMode(next); return;
  }
  if (gUiMode != UiMode::Radio) {
    if (radioBusy()) stopInternetRadio();
    return;
  }
  if (gRadioStartPending && !radioBusy()) {
    gRadioStartPending = false;
    const auto* station = gRadioSelection.station();
    if (!station || !radioStartAllowed()) {
      gRadioWantPlaying = false;
      gRadioNotice = !gWifiReady ? "Wi-Fi required" : "Busy: retry Enter";
    } else {
      initSpeaker();
      if (!internet_radio::start(station->url)) {
        gRadioWantPlaying = false; gRadioNotice = internet_radio::status();
      } else { gRadioNotice = ""; }
    }
    radioInvalidate();
  } else if (!radioBusy() && gRadioWantPlaying && !gRadioStartPending) {
    gRadioWantPlaying = false; gRadioNotice = internet_radio::status(); radioInvalidate();
  }
}
void radioRetune() {
  gRadioNotice = "Enter to listen";
  if (gRadioWantPlaying) { internet_radio::stop(); gRadioStartPending = true; }
  radioInvalidate();
}
bool handleInternetRadioKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  if (lower == 'w' || key == ';') { gRadioSelection.moveGenre(-1); radioRetune(); }
  else if (lower == 's' || key == '.') { gRadioSelection.moveGenre(1); radioRetune(); }
  else if (lower == 'a' || key == ',') { gRadioSelection.moveStation(-1); radioRetune(); }
  else if (lower == 'd' || key == '/') { gRadioSelection.moveStation(1); radioRetune(); }
  else if (lower == 'l') { gRadioSelection.nextLanguage(); radioRetune(); }
  else if (key == '-' || key == '_') { adjustSpeakerVolume(-16); radioInvalidate(); }
  else if (key == '+' || key == '=') { adjustSpeakerVolume(16); radioInvalidate(); }
  else if (keys.enter || key == '\r' || key == '\n' || key == ' ') {
    if (gRadioWantPlaying || radioBusy()) { stopInternetRadio(); gRadioNotice = "Paused"; }
    else { gRadioWantPlaying = true; gRadioStartPending = true; gRadioNotice = "Starting..."; }
    radioInvalidate();
  }
  return true;
}
void renderInternetRadioUi() {
  auto& d = gCanvas;
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  const uint16_t green = rgb565_local(88,240,141), amber = rgb565_local(255,136,64), dim = rgb565_local(36,73,85);
  const bool playing = internet_radio::state() == internet_radio::State::Playing;
  const uint16_t accent = playing ? green : cyan;
  String hhmm = "--:--";
  const time_t now = time(nullptr);
  if (now > 100000) {
    struct tm ti; localtime_r(&now,&ti); char buf[6];
    clock_display::format(buf,sizeof(buf),ti.tm_hour,ti.tm_min,gDeviceSettings.clockTwelveHour);
    hhmm = buf;
  }
  drawBigHeader("Internet Radio",accent,hhmm);
  const auto* station = gRadioSelection.station();
  const String name = station ? station->name : "No stations";
  useBigTextFont(name);
  d.setTextColor(white); d.setCursor(kBigLeft,27);
  d.print(fitCurrentFontToWidth(name,202));
  // Shows whether the stream is on, in the same spot the title ends.
  if (playing) { d.fillRect(218,29,4,13,accent); d.fillRect(226,29,4,13,accent); }
  else d.fillTriangle(219,29,219,42,230,35,accent);
  d.setFont(&fonts::Font2);
  d.setTextColor(cyan); d.setCursor(kBigLeft,48);
  d.print(fitCurrentFontToWidth(radio_catalog::kGenres[gRadioSelection.genre],150));
  const String where = String(gRadioSelection.index+1)+"/"+String(gRadioSelection.count(gRadioSelection.genre))+" "+
      (station ? radio_catalog::languageName(station->language) : "--");
  d.setTextColor(muted); d.setCursor(kBigRight-d.textWidth(where),48); d.print(where);
  d.setTextColor(radioBusy() ? green : amber); d.setCursor(kBigLeft,66);
  const String status = gRadioStartPending ? "Tuning..." : radioBusy() ? internet_radio::status() : gRadioNotice;
  d.print(fitCurrentFontToWidth(status,222));
  const int volume = (int(gSpeakerVolume)*100+127)/255;
  d.setTextColor(muted); d.setCursor(kBigLeft,84); d.print("Vol "+String(volume)+"%");
  for (int i = 0; i < 9; ++i) d.fillRect(84+i*16,87,12,9,i*255/9<int(gSpeakerVolume)?accent:dim);
  d.setFont(&fonts::Font0);
  d.setTextColor(muted); d.setCursor(8,108); d.print("W/S genre  A/D tune  Enter play/pause");
  d.setCursor(8,120); d.print("L EN/RU  +/- vol  Esc exit  Tab menu");
  d.setFont(&fonts::Font2);
  renderLauncherOverlay(); renderDebugOverlay();
}
} // namespace
