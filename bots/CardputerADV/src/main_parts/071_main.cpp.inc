// VLC remote controls the Mac; no local audio/SD ownership and no autoplay.
namespace {
vlc_remote::Config gVlcConfig;
bool gVlcLoaded=false,gVlcConnectPending=false,gVlcModePending=false;
UiMode gVlcNextMode=UiMode::Home;
uint32_t gVlcLastPollMs=0;
String gVlcNotice;

bool vlcBusy(){return vlc_remote::busy();}
void vlcInvalidate(){gLastRenderMs=0;gUiFramePolicy.invalidate();}
void loadVlcSettings() {
  if(gVlcLoaded)return;
  gVlcLoaded=true;gVlcConfig=vlc_remote::defaults();
  // Old device NVS has no VLC namespace/marker. It must not mask provisioned
  // defaults with empty legacy values or a general device "configured" flag.
  if(!gPrefs.begin("vlc_remote",true))return;
  if(gPrefs.getBool("configured",false)) {
    vlc_remote::Config saved;
    snprintf(saved.host,sizeof(saved.host),"%s",gPrefs.getString("host","").c_str());
    snprintf(saved.fallback,sizeof(saved.fallback),"%s",gPrefs.getString("fallback","").c_str());
    snprintf(saved.password,sizeof(saved.password),"%s",gPrefs.getString("password","").c_str());
    saved.port=gPrefs.getUShort("port",8080);
    if(vlc_remote::configured(saved))gVlcConfig=saved;
  }
  gPrefs.end(); // No writes: private defaults need not be copied into NVS.
}
bool vlcStartAllowed() {
  return gWifiReady && gCanvasSpriteActive && !gBootBackgroundActive && !radioBusy() &&
    !gRecording && !gSubmitting && !gThinking && !gPlaybackActive && !gTransferUiActive &&
    !gOtaApplyInProgress && !gInboxHttpBusy && !gVoiceDialBusy && !gVoiceReplySpeechBusy &&
    gQueuedVoiceReplySpeech.isEmpty() && !gRealtimeActive && !gRealtimeDialBusy &&
    !gRealtimeExclusiveMode && !gPendingRealtimeStart && !gMeshBusOwned &&
    !gTextTurnTaskHandle && !gCompletedTextTurn && gPendingTextTurns.empty() &&
    gActiveTextTurn.isEmpty() && !gG0VoicePending && !gVoiceCapturePreparing;
}
void beginVlcRemote() {
  loadVlcSettings();gVlcConnectPending=true;gVlcNotice="Searching Mac...";vlcInvalidate();
}
bool deferVlcRemoteMode(UiMode mode) {
  if(mode==UiMode::VlcRemote)return false;
  gVlcConnectPending=false;
  if(!vlcBusy())return false;
  vlc_remote::cancel();gVlcModePending=true;gVlcNextMode=mode;
  gVlcNotice="Closing connection...";vlcInvalidate();return true;
}
void vlcRequest(vlc_remote::Command command,bool discover=false) {
  if(vlcBusy()){gVlcNotice="Busy; key not queued";vlcInvalidate();return;}
  if(!vlcStartAllowed()){gVlcNotice=!gWifiReady?"Wi-Fi required":"Busy; R retry";vlcInvalidate();return;}
  if(vlc_remote::start(gVlcConfig,command,discover))gVlcNotice=discover?"Searching Mac...":"Requesting...";
  else gVlcNotice=vlc_remote::message(vlc_remote::snapshot().error);
  gVlcLastPollMs=millis();vlcInvalidate();
}
void serviceVlcRemote() {
  static uint32_t seen=0;
  if(gVlcModePending && !vlcBusy()) {
    gVlcModePending=false;setUiMode(gVlcNextMode);return;
  }
  if(gUiMode!=UiMode::VlcRemote) {gVlcConnectPending=false;return;}
  auto state=vlc_remote::snapshot();
  if(state.revision!=seen){seen=state.revision;gVlcNotice=vlc_remote::message(state.error);vlcInvalidate();}
  if(gLauncherVisible || gVlcModePending)return;
  if(gVlcConnectPending && !vlcBusy()) {
    if(!vlcStartAllowed()){gVlcNotice=!gWifiReady?"Wi-Fi required":"Waiting for network...";return;}
    gVlcConnectPending=false;vlcRequest(vlc_remote::Command::Status,true);
  } else if(!vlcBusy() && state.connected && millis()-gVlcLastPollMs>=3000 && vlcStartAllowed()) {
    vlcRequest(vlc_remote::Command::Status); // Read-only; commands are never retried.
  }
}
bool handleVlcRemoteKey(char key,const Keyboard_Class::KeysState& keys) {
  if(keys.ctrl || keys.alt || keys.opt)return true;
  char lower=static_cast<char>(tolower(static_cast<unsigned char>(key)));
  using vlc_remote::Command;
  if(lower=='r'){vlcRequest(Command::Status,true);return true;}
  const auto state=vlc_remote::snapshot();
  if(!state.connected){gVlcNotice="R: find Mac / connect";vlcInvalidate();return true;}
  if(keys.enter || key=='\r' || key=='\n' || key==' ')vlcRequest(Command::Toggle);
  else if(lower=='x')vlcRequest(Command::Stop);
  else if(lower=='n')vlcRequest(Command::Next);
  else if(lower=='p')vlcRequest(Command::Previous);
  else if(lower=='w' || key=='+' || key=='=' || key==';')vlcRequest(Command::VolumeUp);
  else if(lower=='s' || key=='-' || key=='_' || key=='.')vlcRequest(Command::VolumeDown);
  else if(lower=='a' || key==',')vlcRequest(Command::Back);
  else if(lower=='d' || key=='/')vlcRequest(Command::Forward);
  return true;
}
String vlcTime(int seconds) {
  char text[24];snprintf(text,sizeof(text),"%d:%02d",seconds/60,seconds%60);return text;
}
void renderVlcRemoteUi() {
  auto& d=gCanvas;const auto state=vlc_remote::snapshot();
  const uint16_t cyan=rgb565_local(47,227,255),white=TFT_WHITE,dim=rgb565_local(36,73,85);
  const uint16_t muted=rgb565_local(182,219,255),green=rgb565_local(88,240,141),amber=rgb565_local(255,136,64);
  drawBigHeader("VLC Remote",state.connected?green:cyan,state.connected?"Connected":"Offline");
  const String title=state.connected?String(state.title):String("Open VLC on your Mac");
  useBigTextFont(title);
  d.setTextColor(white);d.setCursor(kBigLeft,27);d.print(fitCurrentFontToWidth(title,222));
  d.setFont(&fonts::Font2);
  d.setTextColor(muted);d.setCursor(kBigLeft,47);
  d.print(fitCurrentFontToWidth(String(state.connected?state.playback:"stopped")+"  "+vlcTime(state.seconds)+" / "+vlcTime(state.length),222));
  d.fillRoundRect(kBigLeft,65,222,5,2,dim);
  int bar=state.length>0?int(std::min<int64_t>(222,int64_t(state.seconds)*222/state.length)):0;
  if(bar>0)d.fillRect(kBigLeft,65,bar,5,cyan);
  d.setTextColor(muted);d.setCursor(kBigLeft,73);
  d.print(fitCurrentFontToWidth(String("Vol ")+String((state.volume*100+128)/256)+"%  "+state.route,222));
  d.setTextColor(state.connected?green:amber);d.setCursor(kBigLeft,90);d.print(fitCurrentFontToWidth(gVlcNotice,222));
  d.setFont(&fonts::Font0);
  d.setTextColor(muted);d.setCursor(8,110);d.print("Enter play/pause X stop N/P track");
  d.setCursor(8,120);d.print("W/S vol A/D seek 10s R connect");
  d.setFont(&fonts::Font2);
  renderLauncherOverlay();renderDebugOverlay();
}
} // namespace
