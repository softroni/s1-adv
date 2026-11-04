// Internet radio: the clock/Pulse palette and a separate streaming owner. The stations are a
// text file on the SD card (radio_catalog.h says how it reads), and Find adds to it from
// radio-browser.info, a public directory that needs no account and no bridge.
namespace {
enum class RadioView : uint8_t { Player, Find, Results, Remove, Visual };
constexpr const char* kRadioDir = "/radio";
constexpr const char* kRadioListPath = "/radio/stations.txt";
constexpr const char* kRadioListNewPath = "/radio/stations.new";
constexpr const char* kRadioLastPath = "/radio/last.txt";
constexpr const char* kRadioCountryPath = "/radio/country.txt";
constexpr uint32_t kRadioPlayedBytes = 32768;  // About two seconds of 128k: it was playing.
constexpr uint32_t kRadioRetryMs[] = {3000, 10000, 30000};
constexpr uint32_t kRadioWifiWaitMs = 60000;
constexpr size_t kRadioQueryChars = 32;

radio_catalog::List* gRadioList = nullptr;
radio_catalog::Selection gRadioSelection;
bool gRadioListOnCard = false;  // False: the built-in stations, and nothing saves.
String gRadioListNote;
RadioView gRadioView = RadioView::Player;
bool gRadioWantPlaying = false;
bool gRadioStartPending = false;
bool gRadioModePending = false;
UiMode gRadioNextMode = UiMode::Home;
String gRadioNotice;
bool gRadioNoticeGood = false;  // Saved or paused: green on the results list, not amber.
// What Enter asked for, and what is playing: a found station needn't be on the list.
String gRadioWantUrl, gRadioWantName, gRadioPlayingUrl;
String gRadioLastUrl;  // What last.txt holds, so it's only written when it changes.
// Reconnecting after a stream that was playing drops.
uint8_t gRadioRetries = 0;
bool gRadioRetryWaiting = false;
uint32_t gRadioRetryAtMs = 0, gRadioDroppedAtMs = 0;
// Find. The country is two capital letters, or empty for anywhere, and the card keeps it.
String gRadioQuery, gRadioSearchedQuery;
String gRadioCountry, gRadioSearchedCountry, gRadioSavedCountry;
bool gRadioOnCountry = false;  // Find's cursor is in the country box, not the words.
bool gRadioByGenre = true, gRadioSearchedByGenre = true;
bool gRadioSearchPending = false, gRadioSearching = false;
int gRadioFoundSel = 0;
// Visuals: which look, when its name last showed, and the bars as drawn, which rise at once
// and fall back slowly, with caps that fall slower still.
constexpr const char* kRadioLooks[] = {"Bars", "Mirror", "Wave", "Rings"};
constexpr uint8_t kRadioLookCount = sizeof(kRadioLooks) / sizeof(kRadioLooks[0]);
uint8_t gRadioLook = 0;
uint32_t gRadioLookShownMs = 0, gRadioVisualAtMs = 0;
uint8_t gRadioShown[internet_radio::kBands] = {}, gRadioPeaks[internet_radio::kBands] = {};
// Remove.
uint16_t gRadioRemoveLine = 0;
String gRadioRemoveName, gRadioRemoveUrl, gRadioRemoveGenre;

bool radioBusy() { return internet_radio::busy(); }
void radioInvalidate() { gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
bool radioTakesText() { return gUiMode == UiMode::Radio && gRadioView == RadioView::Find; }
bool radioTakesArrows() {
  return gUiMode == UiMode::Radio && (gRadioView == RadioView::Find || gRadioView == RadioView::Results);
}
void stopInternetRadio() {
  gRadioWantPlaying = false;
  gRadioStartPending = false;
  gRadioRetryWaiting = false;
  gRadioRetries = 0;
  gRadioSearchPending = false;
  gRadioSearching = false;  // A stopped search shows nothing.
  internet_radio::stop();
  radioInvalidate();
}
bool deferInternetRadioMode(UiMode mode) {
  if (!radioBusy() || mode == UiMode::Radio) return false;
  stopInternetRadio(); gRadioModePending = true; gRadioNextMode = mode;
  setStatus("Stopping radio...", 800);
  return true; // Do not reclaim the speaker/RAM before the worker exits.
}
bool radioStartAllowed() {
  return gWifiReady && !gBootBackgroundActive && gCanvasSpriteActive && !gRecording &&
    !gSubmitting && !gThinking && !gPlaybackActive && !gTransferUiActive && !gOtaApplyInProgress &&
    !gInboxHttpBusy && !gVoiceDialBusy && !gVoiceReplySpeechBusy && gQueuedVoiceReplySpeech.isEmpty() &&
    !gRealtimeActive && !gRealtimeDialBusy && !gRealtimeExclusiveMode && !gPendingRealtimeStart &&
    !gMeshBusOwned && !gTextTurnTaskHandle && !gCompletedTextTurn && gPendingTextTurns.empty() &&
    gActiveTextTurn.isEmpty() && !gG0VoicePending && !gVoiceCapturePreparing &&
    !M5Cardputer.Speaker.isPlaying();
}

// --- The list on the card ---------------------------------------------------------------
// exists() also says no when the card has stopped answering, so the card has to answer first.
bool radioCardAnswers() {
  File root = gSdReady ? SD.open("/") : File();
  const bool answers = root && root.isDirectory();
  if (root) root.close();
  return answers;
}
// A new card gets the built-in stations as a file to add to. Never over a list: the rename
// is only tried while there's no list there.
void radioWriteStarter() {
  if (!ensureDirRecursive(SD, kRadioDir)) return;
  SD.remove(kRadioListNewPath);
  File file = SD.open(kRadioListNewPath, FILE_WRITE);
  if (!file) return;
  const size_t size = sizeof(radio_catalog::kStarter) - 1;
  const bool wrote = file.write(reinterpret_cast<const uint8_t*>(radio_catalog::kStarter), size) == size;
  file.close();
  if (wrote && !SD.exists(kRadioListPath)) SD.rename(kRadioListNewPath, kRadioListPath);
  else SD.remove(kRadioListNewPath);
}
void radioLoadList() {
  if (!gRadioList) gRadioList = new (std::nothrow) radio_catalog::List;
  gRadioSelection.list = gRadioList;
  gRadioListOnCard = false;
  gRadioListNote = "";
  if (!gRadioList) { gRadioListNote = "Radio memory unavailable"; return; }
  if (radioCardAnswers()) {
    // A change stopped between its last two steps leaves only the new file.
    if (!SD.exists(kRadioListPath) && SD.exists(kRadioListNewPath)) SD.rename(kRadioListNewPath, kRadioListPath);
    if (!SD.exists(kRadioListPath)) radioWriteStarter();
    File file = SD.open(kRadioListPath, "r");
    if (file) {
      const size_t size = file.size();
      char* text = gRadioList->buffer(size);
      if (text && file.read(reinterpret_cast<uint8_t*>(text), size) == size) {
        gRadioList->parse(size);
        gRadioListOnCard = true;
      } else {
        gRadioListNote = size > radio_catalog::kMaxFileBytes ? "stations.txt is over 16 KB" : "Can't read stations.txt";
      }
      file.close();
    }
  }
  if (!gRadioListOnCard) {
    gRadioList->load(radio_catalog::kStarter, sizeof(radio_catalog::kStarter) - 1);
    if (gRadioListNote.isEmpty()) gRadioListNote = gSdReady ? "Can't open stations.txt" : "No SD card: built-in stations";
  } else if (gRadioList->skipped()) {
    gRadioListNote = String(gRadioList->skipped()) + (gRadioList->skipped() == 1 ? " line" : " lines") + " in stations.txt skipped";
  }
  gRadioSelection.fit();
}
bool radioAppendLine(const char* line, size_t size) {
  File file = SD.open(kRadioListPath, FILE_APPEND);
  if (!file) return false;
  bool ok = gRadioList->endsWithNewline() || file.write('\n') == 1;
  ok = ok && file.write(reinterpret_cast<const uint8_t*>(line), size) == size;
  file.close();
  return ok;
}
// Copies the file without one line, so every note and hand-typed station stays as it was.
bool radioRewriteWithout(uint16_t line) {
  File in = SD.open(kRadioListPath, "r");
  if (!in) return false;
  SD.remove(kRadioListNewPath);
  File out = SD.open(kRadioListNewPath, FILE_WRITE);
  if (!out) { in.close(); return false; }
  radio_catalog::LineSkipper skipper;
  skipper.skip = line;
  uint8_t chunk[256], kept[256];
  bool ok = true;
  for (;;) {
    const size_t got = in.read(chunk, sizeof(chunk));
    if (!got) break;
    size_t n = 0;
    for (size_t i = 0; i < got; ++i) if (skipper.keep(chunk[i])) kept[n++] = chunk[i];
    if (n && out.write(kept, n) != n) { ok = false; break; }
  }
  in.close();
  out.close();
  if (!ok) { SD.remove(kRadioListNewPath); return false; }
  // The old file stays until the new one is whole, so a power cut loses at most this change.
  SD.remove(kRadioListPath);
  return SD.rename(kRadioListNewPath, kRadioListPath);
}
void radioReadLast() {
  gRadioLastUrl = "";
  if (!gRadioListOnCard) return;
  File file = SD.open(kRadioLastPath, "r");
  if (!file) return;
  char url[internet_radio::detail::kUrlSize];
  const size_t n = file.read(reinterpret_cast<uint8_t*>(url), sizeof(url) - 1);
  file.close();
  url[n] = 0;
  for (size_t i = 0; i < n; ++i) if (url[i] == '\n' || url[i] == '\r') { url[i] = 0; break; }
  gRadioLastUrl = url;
  gRadioSelection.select(gRadioList->find(url));
}
void radioRememberLast(const String& url) {
  if (!gRadioListOnCard || url == gRadioLastUrl) return;
  File file = SD.open(kRadioLastPath, FILE_WRITE);
  if (!file) return;
  file.write(reinterpret_cast<const uint8_t*>(url.c_str()), url.length());
  file.write('\n');
  file.close();
  gRadioLastUrl = url;
}
void radioReadCountry() {
  if (!gRadioListOnCard) return;
  File file = SD.open(kRadioCountryPath, "r");
  char code[3] = {};
  if (file) {
    if (file.read(reinterpret_cast<uint8_t*>(code), 2) != 2) code[0] = 0;
    file.close();
  }
  gRadioSavedCountry = radio_countries::name(code) ? code : "";
  gRadioCountry = gRadioSavedCountry;
}
void radioRememberCountry(const String& code) {
  if (!gRadioListOnCard || code == gRadioSavedCountry) return;
  if (code.isEmpty()) SD.remove(kRadioCountryPath);  // No file means anywhere.
  else {
    File file = SD.open(kRadioCountryPath, FILE_WRITE);
    if (!file) return;
    file.write(reinterpret_cast<const uint8_t*>(code.c_str()), code.length());
    file.write('\n');
    file.close();
  }
  gRadioSavedCountry = code;
}
// Off the Radio screen, with the worker gone: the list and any results go back to the heap.
void radioRelease() {
  delete gRadioList;
  gRadioList = nullptr;
  gRadioSelection.list = nullptr;
  internet_radio::clearFound();
  gRadioView = RadioView::Player;
}
void beginInternetRadio() {
  gRadioView = RadioView::Player;
  radioLoadList();
  radioReadLast();
  radioReadCountry();
  gRadioOnCountry = false;
  gRadioNotice = gRadioListNote.isEmpty() ? "Enter to listen" : gRadioListNote;
  radioInvalidate();
}

// --- Playing ------------------------------------------------------------------------------
void radioPlay(const String& url, const String& name) {
  gRadioWantUrl = url;
  gRadioWantName = name;
  gRadioWantPlaying = true;
  gRadioStartPending = true;
  gRadioRetryWaiting = false;
  gRadioRetries = 0;
  gRadioNotice = "Starting...";
  if (radioBusy()) internet_radio::stop();  // Starts once the worker has let go.
  radioInvalidate();
}
void radioPlaySelected() {
  const auto* station = gRadioSelection.station();
  if (!station) { gRadioNotice = "No stations: F finds some"; radioInvalidate(); return; }
  radioPlay(station->url, station->name);
}
void radioRetune() {
  gRadioNotice = "Enter to listen";
  if (gRadioWantPlaying) radioPlaySelected();
  radioInvalidate();
}
// After a stream that played ends by itself: three tries, further apart each time. A station
// that never played is broken, not dropped, so it isn't tried again.
bool radioScheduleRetry() {
  using internet_radio::Error;
  const Error e = internet_radio::error();
  if (e == Error::InvalidUrl || e == Error::PublicHttpOnly || e == Error::NoMemory || e == Error::TaskFailed ||
      e == Error::Unsupported || e == Error::Redirect || e == Error::Speaker) return false;
  if (internet_radio::bytes() >= kRadioPlayedBytes) {
    gRadioRetries = 0;
    gRadioDroppedAtMs = millis();
  } else if (!gRadioRetries) {
    return false;
  }
  if (gRadioRetries >= sizeof(kRadioRetryMs) / sizeof(kRadioRetryMs[0])) return false;
  gRadioRetryAtMs = millis() + kRadioRetryMs[gRadioRetries++];
  gRadioRetryWaiting = true;
  gRadioWantUrl = gRadioPlayingUrl;
  return true;
}

// --- Finding ------------------------------------------------------------------------------
String radioSearchError() {
  using internet_radio::Error;
  switch (internet_radio::error()) {
    case Error::Dns: return "Search: can't find the directory";
    case Error::Connect: case Error::Timeout: return "Search: the directory didn't answer";
    case Error::NoMemory: case Error::TaskFailed: return "Search: not enough memory";
    case Error::InvalidUrl: return "Search: too long";
    default: return "Search: odd answer, try again";
  }
}
void radioRequestSearch() {
  String query = gRadioQuery;
  query.trim();
  if (query.isEmpty()) {
    gRadioNotice = gRadioByGenre ? "Type a genre, like jazz" : "Type part of a name";
    gRadioOnCountry = false;
  } else if (gRadioCountry.length() == 1) {
    gRadioNotice = "Type 2 letters, like US";
    gRadioOnCountry = true;
  } else if (!gRadioCountry.isEmpty() && !radio_countries::name(gRadioCountry.c_str())) {
    gRadioNotice = "No country " + gRadioCountry + ": try one like US";
    gRadioOnCountry = true;
  } else if (!gWifiReady) {
    gRadioNotice = "Wi-Fi required";
  } else {
    if (radioBusy() || gRadioWantPlaying) stopInternetRadio();  // One worker: the stream gives way.
    gRadioSearchedQuery = query;
    gRadioSearchedByGenre = gRadioByGenre;
    gRadioSearchedCountry = gRadioCountry;
    radioRememberCountry(gRadioCountry);
    gRadioSearchPending = true;
    gRadioNotice = "Searching...";
  }
  radioInvalidate();
}
void radioStartSearch() {
  if (!radioStartAllowed()) gRadioNotice = !gWifiReady ? "Wi-Fi required" : "Busy: retry Enter";
  else if (internet_radio::startSearch(gRadioSearchedByGenre, gRadioSearchedQuery.c_str(), gRadioSearchedCountry.c_str())) gRadioSearching = true;
  else gRadioNotice = radioSearchError();
  radioInvalidate();
}
void radioSearchDone() {
  const size_t found = internet_radio::foundCount();
  if (internet_radio::error() != internet_radio::Error::None) gRadioNotice = radioSearchError();
  else if (!found) gRadioNotice = "Nothing found for " + gRadioSearchedQuery + (gRadioSearchedCountry.isEmpty() ? "" : " in " + gRadioSearchedCountry);
  else if (gRadioView == RadioView::Find) { gRadioView = RadioView::Results; gRadioFoundSel = 0; gRadioNotice = ""; }
  radioInvalidate();
}
const internet_radio::Found* radioFound() { return internet_radio::found(size_t(gRadioFoundSel)); }
bool radioPlayingUrl(const char* url) {
  return (gRadioWantPlaying || radioBusy()) && gRadioWantUrl == url;
}
void radioPlayFound() {
  const auto* found = radioFound();
  if (!found) return;
  gRadioNoticeGood = true;
  if (radioPlayingUrl(found->url)) { stopInternetRadio(); gRadioNotice = "Paused"; return; }
  radioPlay(found->url, found->name);
  gRadioNotice = "";
}
void radioSaveFound() {
  const auto* found = radioFound();
  gRadioNoticeGood = false;
  if (!found || !gRadioList) return;
  if (!gRadioListOnCard) { gRadioNotice = gSdReady ? "Can't save: stations.txt unreadable" : "No SD card: can't save"; return; }
  const int had = gRadioList->find(found->url);
  if (had >= 0) { gRadioNotice = String("Already in ") + gRadioList->genre(gRadioList->at(size_t(had)).genre); return; }
  if (gRadioList->full()) { gRadioNotice = "The list is full: 100 stations"; return; }
  char genre[radio_catalog::kGenreBytes + 1];
  radio_catalog::genreFor(*gRadioList, gRadioSearchedByGenre, gRadioSearchedQuery.c_str(), found->tags, genre, sizeof(genre));
  if (gRadioList->findGenre(genre) < 0 && gRadioList->genres() >= radio_catalog::kMaxGenres) {
    gRadioNotice = "Too many genres: 24 at most";
    return;
  }
  char line[radio_catalog::kGenreBytes + radio_catalog::kNameBytes + internet_radio::detail::kUrlSize + 16];
  const size_t size = radio_catalog::formatLine(line, sizeof(line), genre, found->name, found->url,
    found->russian ? radio_catalog::Language::Russian : radio_catalog::Language::International);
  if (gRadioList->size() + size + 1 > radio_catalog::kMaxFileBytes) { gRadioNotice = "stations.txt is full: 16 KB"; return; }
  if (!size || !radioAppendLine(line, size)) { gRadioNotice = "Couldn't save to the SD card"; return; }
  const String url = found->url;
  radioLoadList();
  gRadioSelection.select(gRadioList ? gRadioList->find(url.c_str()) : -1);
  gRadioNotice = String("Saved to ") + genre;
  gRadioNoticeGood = true;
}
// Back to the player, which only ever plays a station on the list.
void radioLeaveResults() {
  if (gRadioWantPlaying || radioBusy()) {
    const int on = gRadioList ? gRadioList->find(gRadioWantUrl.c_str()) : -1;
    if (on >= 0) gRadioSelection.select(on);
    else { stopInternetRadio(); gRadioNotice = "Stopped: not on your list"; }
  }
  internet_radio::clearFound();
  gRadioView = RadioView::Player;
}

// --- Removing -----------------------------------------------------------------------------
void radioAskRemove() {
  const auto* station = gRadioSelection.station();
  if (!station) return;
  if (!gRadioListOnCard) {
    gRadioNotice = gSdReady ? "Can't change stations.txt" : "No SD card: built-in stations";
    radioInvalidate();
    return;
  }
  gRadioRemoveLine = station->line;
  gRadioRemoveName = station->name;
  gRadioRemoveUrl = station->url;
  gRadioRemoveGenre = gRadioSelection.genreName();
  gRadioView = RadioView::Remove;
  radioInvalidate();
}
void radioRemove() {
  gRadioView = RadioView::Player;
  if (radioPlayingUrl(gRadioRemoveUrl.c_str())) stopInternetRadio();
  if (!radioRewriteWithout(gRadioRemoveLine)) { gRadioNotice = "Couldn't change the SD card"; radioInvalidate(); return; }
  radioLoadList();
  gRadioNotice = "Removed " + gRadioRemoveName;
  radioInvalidate();
}
bool radioEscape() {
  switch (gRadioView) {
    case RadioView::Find:
      if (gRadioSearching || gRadioSearchPending) stopInternetRadio();
      gRadioView = RadioView::Player;
      gRadioNotice = "Enter to listen";
      break;
    case RadioView::Results:
      radioLeaveResults();
      break;
    case RadioView::Remove:
      gRadioView = RadioView::Player;
      gRadioNotice = "Kept " + gRadioRemoveName;
      break;
    case RadioView::Visual:
      gRadioView = RadioView::Player;
      break;
    default:
      return false;
  }
  radioInvalidate();
  return true;
}

void serviceInternetRadio() {
  if (gRadioModePending && !radioBusy()) {
    gRadioModePending = false; const UiMode next = gRadioNextMode;
    setUiMode(next); return;
  }
  // The worker only analyses frames while the visuals are on screen.
  internet_radio::wantVisuals(gUiMode == UiMode::Radio && gRadioView == RadioView::Visual && !gRadioModePending);
  if (gUiMode != UiMode::Radio) {
    // A reconnect or search still waiting would otherwise start when the radio opens again.
    if (radioBusy() || gRadioWantPlaying || gRadioStartPending || gRadioRetryWaiting || gRadioSearchPending || gRadioSearching) stopInternetRadio();
    else if (gRadioList) radioRelease();
    return;
  }
  if (gRadioSearching && !radioBusy()) { gRadioSearching = false; radioSearchDone(); }
  if (gRadioSearchPending && !radioBusy()) { gRadioSearchPending = false; radioStartSearch(); return; }
  if (gRadioRetryWaiting && !radioBusy() && int32_t(millis() - gRadioRetryAtMs) >= 0) {
    // A dropped stream is often Wi-Fi going, so wait a while for it to come back.
    if (!gWifiReady && millis() - gRadioDroppedAtMs < kRadioWifiWaitMs) gRadioRetryAtMs = millis() + 1000;
    else { gRadioRetryWaiting = false; gRadioStartPending = true; }
    radioInvalidate();
  }
  if (gRadioStartPending && !radioBusy()) {
    gRadioStartPending = false;
    if (gRadioWantUrl.isEmpty() || !radioStartAllowed()) {
      gRadioWantPlaying = false; gRadioRetries = 0;
      gRadioNotice = !gWifiReady ? "Wi-Fi required" : "Busy: retry Enter";
    } else {
      initSpeaker();
      if (!internet_radio::start(gRadioWantUrl.c_str())) {
        gRadioWantPlaying = false; gRadioRetries = 0; gRadioNotice = internet_radio::status();
      } else {
        gRadioNotice = "";
        gRadioPlayingUrl = gRadioWantUrl;
        if (gRadioList && gRadioList->find(gRadioWantUrl.c_str()) >= 0) radioRememberLast(gRadioWantUrl);
      }
    }
    radioInvalidate();
  } else if (!radioBusy() && gRadioWantPlaying && !gRadioRetryWaiting) {
    if (!radioScheduleRetry()) { gRadioWantPlaying = false; gRadioRetries = 0; gRadioNotice = internet_radio::status(); }
    radioInvalidate();
  }
}

// --- Keys ---------------------------------------------------------------------------------
// The Cardputer's arrows are the ; . , / keys, and Fn doesn't change what they send, so here
// they move rather than type, as they do everywhere else in the radio. char(1) to char(4) are
// a USB keyboard's arrows. Left and right pick genre or name; up and down move between the
// words and the country.
bool radioFindKey(char key, const Keyboard_Class::KeysState& keys) {
  if (key == ';') key = char(1);
  else if (key == '.') key = char(2);
  else if (key == ',') key = char(3);
  else if (key == '/') key = char(4);
  String& field = gRadioOnCountry ? gRadioCountry : gRadioQuery;
  if (key == char(3) || key == char(4)) { gRadioByGenre = !gRadioByGenre; gRadioNotice = ""; }
  else if (key == char(1) || key == char(2)) { gRadioOnCountry = !gRadioOnCountry; gRadioNotice = ""; }
  else if (key == '\b') { if (!field.isEmpty()) field.remove(field.length() - 1); }
  else if (keys.enter || key == '\n' || key == '\r') { if (!gRadioSearching && !gRadioSearchPending) radioRequestSearch(); }
  else if (gRadioOnCountry) {
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(key)));
    if (letter >= 'A' && letter <= 'Z' && gRadioCountry.length() < 2) {
      gRadioCountry += letter;
      gRadioCountry = radio_countries::alias(gRadioCountry.c_str());
    }
  }
  else if (key >= ' ' && key < 127 && gRadioQuery.length() < kRadioQueryChars) gRadioQuery += key;
  radioInvalidate();
  return true;
}
bool radioResultsKey(char key, const Keyboard_Class::KeysState& keys) {
  const int count = int(internet_radio::foundCount());
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  if (key == char(1) || key == char(3) || key == ';' || key == ',') gRadioFoundSel = count ? (gRadioFoundSel + count - 1) % count : 0;
  else if (key == char(2) || key == char(4) || key == '.' || key == '/') gRadioFoundSel = count ? (gRadioFoundSel + 1) % count : 0;
  else if (keys.enter || key == '\r' || key == '\n' || key == ' ') radioPlayFound();
  else if (lower == 's') radioSaveFound();
  else if (lower == 'f') { radioLeaveResults(); gRadioView = RadioView::Find; gRadioNotice = ""; }
  else if (key == '-' || key == '_') adjustSpeakerVolume(-16);
  else if (key == '+' || key == '=') adjustSpeakerVolume(16);
  radioInvalidate();
  return true;
}
bool handleInternetRadioKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  if (gRadioView == RadioView::Find) return radioFindKey(key, keys);
  if (gRadioView == RadioView::Results) return radioResultsKey(key, keys);
  if (gRadioView == RadioView::Remove) {
    if (key == 'y' || key == 'Y') radioRemove();
    else { gRadioView = RadioView::Player; gRadioNotice = "Kept " + gRadioRemoveName; radioInvalidate(); }
    return true;
  }
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  // V opens the visuals, and there it changes the look. Every other key works as it does here.
  if (lower == 'v') {
    if (gRadioView == RadioView::Visual) gRadioLook = uint8_t((gRadioLook + 1) % kRadioLookCount);
    gRadioView = RadioView::Visual;
    gRadioLookShownMs = millis();
    radioInvalidate();
    return true;
  }
  if (lower == 'w' || key == ';') { gRadioSelection.moveGenre(-1); radioRetune(); }
  else if (lower == 's' || key == '.') { gRadioSelection.moveGenre(1); radioRetune(); }
  else if (lower == 'a' || key == ',') { gRadioSelection.moveStation(-1); radioRetune(); }
  else if (lower == 'd' || key == '/') { gRadioSelection.moveStation(1); radioRetune(); }
  else if (lower == 'l') { gRadioSelection.nextLanguage(); radioRetune(); }
  else if (lower == 'f') { gRadioView = RadioView::Find; gRadioNotice = ""; radioInvalidate(); }
  else if (key == '\b') radioAskRemove();
  else if (key == '-' || key == '_') { adjustSpeakerVolume(-16); radioInvalidate(); }
  else if (key == '+' || key == '=') { adjustSpeakerVolume(16); radioInvalidate(); }
  else if (keys.enter || key == '\r' || key == '\n' || key == ' ') {
    if (gRadioWantPlaying || radioBusy()) { stopInternetRadio(); gRadioNotice = "Paused"; }
    else radioPlaySelected();
    radioInvalidate();
  }
  return true;
}

// --- Drawing ------------------------------------------------------------------------------
// Font2 has ASCII only. A name or song in another script needs the font with its letters,
// which sits 2 pixels lower. True when it picked that one.
bool radioTextFont(const String& text) {
  for (size_t i = 0; i < text.length(); ++i) {
    if (static_cast<uint8_t>(text[i]) >= 0x80) { gCanvas.setFont(&fonts::efontCN_12); return true; }
  }
  gCanvas.setFont(&fonts::Font2);
  return false;
}
bool radioLetterStart(char c) { return (static_cast<uint8_t>(c) & 0xC0) != 0x80; }
void radioDropLastLetter(String& text) {
  size_t n = text.length();
  while (n > 0 && !radioLetterStart(text[n - 1])) --n;
  text.remove(n > 0 ? n - 1 : 0);
}
// Scrolls a letter at a time. marqueeText steps by bytes, which would split a Cyrillic letter.
String radioMarquee(const String& text, int32_t pixelLimit) {
  if (gCanvas.textWidth(text) <= pixelLimit) return text;
  const String padded = text + "    " + text;
  size_t letters = 0;
  for (size_t i = 0; i < text.length() + 4; ++i) if (radioLetterStart(padded[i])) ++letters;
  const size_t step = letters ? (millis() / 240) % letters : 0;
  size_t at = 0;
  for (size_t seen = 0; at < padded.length(); ++at) {
    if (radioLetterStart(padded[at]) && seen++ == step) break;
  }
  String out = padded.substring(at);
  while (out.length() > 1 && gCanvas.textWidth(out) > pixelLimit) radioDropLastLetter(out);
  return out;
}
String radioFit(String text, int32_t pixelLimit) {
  if (gCanvas.textWidth(text) <= pixelLimit) return text;
  while (text.length() > 1 && gCanvas.textWidth(text + "..") > pixelLimit) radioDropLastLetter(text);
  return text + "..";
}
String radioStatusText(bool* song = nullptr) {
  if (song) *song = false;
  if (gRadioRetryWaiting) {
    if (!gWifiReady) return "Waiting for Wi-Fi...";
    const uint32_t left = int32_t(gRadioRetryAtMs - millis()) > 0 ? gRadioRetryAtMs - millis() : 0;
    return "Reconnecting in " + String((left + 999) / 1000) + "s";
  }
  if (gRadioStartPending) return "Tuning...";
  if (radioBusy() && !internet_radio::searching()) {
    char title[128];
    if (internet_radio::state() == internet_radio::State::Playing && internet_radio::title(title, sizeof(title))) {
      if (song) *song = true;
      return String(title);
    }
    return internet_radio::status();
  }
  return gRadioNotice;
}
// Whole words over up to `lines` lines of `width` pixels in the current font. The last line
// ends in ".." if there's more. Returns how many lines it used.
int radioWrap(const String& text, int32_t width, String* out, int lines) {
  String rest = text;
  int n = 0;
  while (!rest.isEmpty() && n < lines) {
    if (n == lines - 1 || gCanvas.textWidth(rest) <= width) { out[n++] = radioFit(rest, width); break; }
    int cut = -1;
    for (int i = 1; i < int(rest.length()); ++i) {
      if (rest[i] != ' ') continue;
      if (gCanvas.textWidth(rest.substring(0, i)) > width) break;
      cut = i;
    }
    if (cut < 0) { out[n++] = radioFit(rest, width); break; }  // One word too long for a line.
    out[n++] = rest.substring(0, cut);
    rest = rest.substring(cut + 1);
  }
  return n;
}
// The meter's last five readings, about a tenth of a second apart, newest on the right.
uint8_t gRadioMeter[5] = {};
uint32_t gRadioMeterAtMs = 0;
// Bars from the decoded audio's loudness, on a log scale as a VU meter reads, so they move with
// the music and settle in a quiet passage. While it connects, a raised bar runs along them.
// Stopped, it's a play mark.
void drawRadioMeter(bool playing, bool waiting, uint16_t accent, uint16_t dim) {
  auto& d = gCanvas;
  const int16_t left = 205, bottom = 41;
  if (playing) {
    if (millis() - gRadioMeterAtMs >= 90) {
      gRadioMeterAtMs = millis();
      memmove(gRadioMeter, gRadioMeter + 1, sizeof(gRadioMeter) - 1);
      const uint16_t level = internet_radio::level();
      // Music sits between about 700 and 23000, some 30 dB, so that's what the 14 pixels cover.
      // Spread wider, every song would fill the bars and they'd stop looking alive.
      const int h = level < 700 ? 1 : int((log2f(float(level)) - 9.5f) * 14.0f / 5.0f + 0.5f);
      gRadioMeter[4] = uint8_t(h < 1 ? 1 : h > 14 ? 14 : h);
    }
    // A bar with no reading yet sits on the baseline, as a quiet one does.
    for (int i = 0; i < 5; ++i) {
      const int16_t h = gRadioMeter[i] ? gRadioMeter[i] : 1;
      d.fillRect(left + i * 5, bottom - h, 4, h, accent);
    }
    return;
  }
  memset(gRadioMeter, 0, sizeof(gRadioMeter));
  if (waiting) {
    const int lit = int((millis() / 150) % 5);
    for (int i = 0; i < 5; ++i) d.fillRect(left + i * 5, bottom - (i == lit ? 6 : 2), 4, i == lit ? 6 : 2, i == lit ? accent : dim);
  } else {
    d.fillTriangle(219, 27, 219, 40, 230, 33, accent);
  }
}
void renderRadioPlayer() {
  auto& d = gCanvas;
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  const uint16_t green = rgb565_local(88,240,141), amber = rgb565_local(255,136,64), dim = rgb565_local(36,73,85);
  const auto state = internet_radio::state();
  const bool searching = internet_radio::searching();
  const bool playing = state == internet_radio::State::Playing && !searching;
  const bool waiting = !playing && !searching && (gRadioStartPending || gRadioRetryWaiting ||
                       (radioBusy() && state != internet_radio::State::Stopping));
  const uint16_t accent = playing ? green : cyan;
  String right = "--:--";
  const time_t now = time(nullptr);
  if (now > 100000) {
    struct tm ti; localtime_r(&now,&ti); char buf[6];
    clock_display::format(buf,sizeof(buf),ti.tm_hour,ti.tm_min,gDeviceSettings.clockTwelveHour);
    right = buf;
  }
  // The language filter only shows while it's hiding something.
  if (gRadioSelection.language != radio_catalog::Language::All) right = String(radio_catalog::languageName(gRadioSelection.language)) + "  " + right;
  drawBigHeader("Internet Radio",accent,right);
  const auto* station = gRadioSelection.station();
  const String name = station ? station->name : "No stations";
  useBigTextFont(name);
  d.setTextColor(white); d.setCursor(kBigLeft,26);
  d.print(radioFit(name,194));
  drawRadioMeter(playing, waiting, gRadioRetryWaiting ? amber : accent, dim);
  // Genre, then where this station is in it, and a Russian station says so.
  if (station) {
    const String genre = gRadioSelection.genreName();
    const bool wideGenre = radioTextFont(genre);
    d.setTextColor(cyan); d.setCursor(kBigLeft, wideGenre ? 47 : 45);
    const String shownGenre = radioFit(genre, 120);
    d.print(shownGenre);
    int16_t x = kBigLeft + d.textWidth(shownGenre) + 5;
    d.setFont(&fonts::Font2);
    d.fillRect(x, 52, 2, 2, muted);
    x += 7;
    String where = String(gRadioSelection.index + 1) + " of " + String(gRadioSelection.count(gRadioSelection.genre));
    if (station->language == radio_catalog::Language::Russian) where += "  RU";
    d.setTextColor(muted); d.setCursor(x, 45); d.print(where);
    if (playing && internet_radio::bitrate()) {
      const String kbps = String(internet_radio::bitrate()) + "k";
      d.setCursor(kBigRight - d.textWidth(kbps), 45); d.print(kbps);
    }
  }
  // The song, over two lines if it needs them, or what the radio is doing.
  bool song = false;
  const String status = radioStatusText(&song);
  const bool wide = radioTextFont(status);
  const int16_t top = wide ? 66 : 64;
  if (song) {
    String lines[2];
    const int n = radioWrap(status, 222, lines, 2);
    d.setTextColor(white);
    for (int i = 0; i < n; ++i) { d.setCursor(kBigLeft, top + i * 16); d.print(lines[i]); }
  } else {
    d.setTextColor(radioBusy() || gRadioRetryWaiting ? green : amber);
    d.setCursor(kBigLeft, top);
    d.print(radioFit(status, 222));
  }
  // Volume: a speaker and the number. - lowers it; + or = raises it, so Shift isn't needed.
  d.fillRect(8, 101, 3, 5, muted);
  d.fillTriangle(10, 103, 15, 98, 15, 108, muted);
  d.setFont(&fonts::Font2);
  d.setTextColor(muted); d.setCursor(20, 96);
  d.print(String((int(gSpeakerVolume) * 100 + 127) / 255) + "%");
  d.setFont(&fonts::Font0);
  d.setCursor(8,112); d.print("Enter play  A/D station  W/S genre");
  d.setCursor(8,123); d.print("V visuals  F find  L EN/RU  +/- vol");
  d.setFont(&fonts::Font2);
}
void renderRadioFind() {
  auto& d = gCanvas;
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  const uint16_t green = rgb565_local(88,240,141), amber = rgb565_local(255,136,64), dim = rgb565_local(36,73,85);
  drawBigHeader("Find stations", cyan, "", "Left/Right kind", "Up/Down country");
  d.setFont(&fonts::Font2);
  const char* tabs[] = {"By genre", "By name"};
  for (int i = 0; i < 2; ++i) {
    const bool on = (i == 0) == gRadioByGenre;
    const int16_t x = 8 + i * 114;
    if (on) d.fillRoundRect(x, 28, 108, 20, 5, cyan);
    else d.drawRoundRect(x, 28, 108, 20, 5, dim);
    d.setTextColor(on ? TFT_BLACK : muted);
    d.setCursor(x + (108 - d.textWidth(tabs[i])) / 2, 30);
    d.print(tabs[i]);
  }
  // The words, with a hint of what to type while there are none.
  d.fillRoundRect(8, 52, 222, 20, 5, TFT_BLACK);
  d.drawRoundRect(8, 52, 222, 20, 5, gRadioOnCountry ? dim : cyan);
  String shown = gRadioQuery + (gRadioOnCountry ? "" : "_");
  while (shown.length() > 1 && d.textWidth(shown) > 210) shown.remove(0, 1);  // The end, where typing is.
  d.setTextColor(white);
  d.setCursor(14, 54);
  d.print(shown);
  if (gRadioQuery.isEmpty()) {
    const int16_t x = 14 + d.textWidth(shown) + (shown.isEmpty() ? 0 : 4);
    d.setTextColor(muted);
    d.setCursor(x, 54);
    d.print(radioFit(gRadioByGenre ? "jazz, rock, chillout, 80s" : "part of a name, like BBC", 222 - x));
  }
  // The country: its code, and its name to show the code is the one meant.
  d.setTextColor(muted);
  d.setCursor(8, 78);
  d.print("Country");
  d.fillRoundRect(72, 76, 158, 20, 5, TFT_BLACK);
  d.drawRoundRect(72, 76, 158, 20, 5, gRadioOnCountry ? cyan : dim);
  const String code = gRadioCountry + (gRadioOnCountry && gRadioCountry.length() < 2 ? "_" : "");
  d.setTextColor(white);
  d.setCursor(78, 78);
  d.print(code);
  const char* name = radio_countries::name(gRadioCountry.c_str());
  String about = name ? String(name) : gRadioCountry.length() == 2 ? String("not a country") :
                 gRadioOnCountry ? String("like US, GB, DE") : gRadioCountry.isEmpty() ? String("Anywhere") : String("");
  const int16_t aboutX = 78 + d.textWidth(code) + (code.isEmpty() ? 0 : 8);
  d.setTextColor(!name && gRadioCountry.length() == 2 ? amber : muted);
  d.setCursor(aboutX, 78);
  d.print(radioFit(about, 224 - aboutX));
  String line = gRadioNotice;
  uint16_t ink = amber;
  if (gRadioSearching || gRadioSearchPending) { line = "Searching..."; ink = green; }
  else if (line.isEmpty() && (gRadioWantPlaying || radioBusy())) { line = "Searching stops the radio"; ink = muted; }
  else if (line.isEmpty()) { line = "Searches radio-browser.info"; ink = muted; }
  d.setTextColor(ink);
  d.setCursor(8, 100);
  d.print(radioFit(line, 222));
  d.setFont(&fonts::Font0);
  d.setTextColor(muted);
  d.setCursor(8, 120);
  d.print("Enter search  Esc back");
  d.setFont(&fonts::Font2);
}
void renderRadioResults() {
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  const uint16_t green = rgb565_local(88,240,141), amber = rgb565_local(255,136,64);
  const int count = int(internet_radio::foundCount());
  drawBigHeader("Found", cyan, count ? String(gRadioFoundSel + 1) + "/" + String(count) : String(""),
                "Enter play  S save", "F search  Esc back");
  const String status = radioStatusText();
  const bool notice = !gRadioNotice.isEmpty() || radioBusy();
  // The notice takes the last row, so the selection has to stay in the four above it.
  const int visible = notice ? kBigVisibleRows - 1 : kBigVisibleRows;
  const int first = max<int>(0, min<int>(gRadioFoundSel - visible / 2, count - visible));
  const int rows = min<int>(visible, count - first);
  for (int row = 0; row < rows; ++row) {
    const int i = first + row;
    const auto* found = internet_radio::found(size_t(i));
    if (!found) break;
    const bool saved = gRadioList && gRadioList->find(found->url) >= 0;
    const bool on = radioPlayingUrl(found->url);
    // The country only: in this font a bitrate too would leave the name a dozen letters.
    const String value = on ? "playing" : saved ? "saved" : String(found->country);
    drawBigRow(row, found->name, value, i == gRadioFoundSel, cyan, white, on || saved ? green : muted);
  }
  drawBigScrollbar(first, count, cyan);
  if (notice) {
    const String text = gRadioNotice.isEmpty() ? status : gRadioNotice;
    const bool good = radioBusy() || gRadioNoticeGood;
    drawBigNotice(radioFit(text, kBigRight - kBigLeft), good ? green : amber);
  }
}
void renderRadioRemove() {
  auto& d = gCanvas;
  const uint16_t muted = rgb565_local(182,219,255), amber = rgb565_local(255,150,80), white = TFT_WHITE;
  drawBigHeader("Remove station?", amber);
  useBigTextFont(gRadioRemoveName);
  d.setTextColor(white);
  d.setCursor(8, 28);
  d.print(radioFit(gRadioRemoveName, 222));
  radioTextFont(gRadioRemoveGenre);
  d.setTextColor(muted);
  d.setCursor(8, 49);
  d.print(radioFit(gRadioRemoveGenre, 222));
  d.setFont(&fonts::Font2);
  d.setCursor(8, 67);
  d.print(marqueeText(gRadioRemoveUrl, 222, 160));
  d.setTextColor(amber);
  d.setCursor(8, 85);
  d.print("From stations.txt on the card");
  drawBigNotice("Y remove   any other key keeps", amber);
}
// The whole screen for the music: four looks from the same analysis of what's playing, with
// the station above and the song below.
void renderRadioVisual() {
  auto& d = gCanvas;
  const uint16_t cyan = rgb565_local(47,227,255), violet = rgb565_local(170,120,255), white = TFT_WHITE;
  const uint16_t muted = rgb565_local(182,219,255), green = rgb565_local(88,240,141), amber = rgb565_local(255,136,64);
  const uint16_t dim = rgb565_local(36,73,85);
  d.fillScreen(TFT_BLACK);
  const bool playing = internet_radio::state() == internet_radio::State::Playing && !internet_radio::searching();
  internet_radio::Visuals v{};
  const bool live = playing && internet_radio::visuals(v);
  // Falls by time, not by frame, so it looks the same however often this draws.
  const uint32_t now = millis();
  const uint32_t ticks = min<uint32_t>((now - gRadioVisualAtMs) / 20, 50);
  if (ticks) gRadioVisualAtMs = now;
  for (size_t i = 0; i < internet_radio::kBands; ++i) {
    const int target = live ? v.bands[i] : 0;
    const int fallen = max<int>(0, int(gRadioShown[i]) - 6 * int(ticks));
    gRadioShown[i] = uint8_t(max(target, fallen));
    gRadioPeaks[i] = uint8_t(max<int>(gRadioShown[i], int(gRadioPeaks[i]) - 2 * int(ticks)));
  }
  const int16_t bottom = 114, mid = 66, height = 94;
  switch (gRadioLook) {
    case 0:  // A spectrum, low notes on the left, with caps on the bars' recent highs.
      for (int i = 0; i < int(internet_radio::kBands); ++i) {
        const int16_t x = 9 + i * 14;
        const uint16_t ink = rgb565_local(47 + (170 - 47) * i / 15, 227 + (120 - 227) * i / 15, 255);
        const int16_t h = int16_t(gRadioShown[i] * height / 255), p = int16_t(gRadioPeaks[i] * height / 255);
        if (h > 0) d.fillRect(x, bottom - h, 12, h, ink);
        d.fillRect(x, bottom - p - 2, 12, 2, p > 0 ? white : dim);
      }
      break;
    case 1:  // The same, twice as fine, mirrored about the middle.
      d.drawFastHLine(8, mid, 224, dim);
      for (int j = 0; j < 32; ++j) {
        const int a = gRadioShown[j / 2], b = gRadioShown[min(15, j / 2 + 1)];
        const int16_t half = int16_t(((j & 1) ? (a + b) / 2 : a) * 46 / 255);
        if (half > 0) d.fillRect(9 + j * 7, mid - half, 5, half * 2 + 1, rgb565_local(88 - 41 * j / 31, 240 - 13 * j / 31, 141 + 114 * j / 31));
      }
      break;
    case 2: {  // The sound itself, about 11 ms of it, drawn twice for a line that reads.
      d.drawFastHLine(8, mid, 224, dim);
      // Scaled to its loudest point so quiet music still fills the screen, but not past
      // 46/40 times, so hiss doesn't fill it too.
      int peak = 40;
      if (live) for (int8_t w : v.wave) peak = max(peak, w < 0 ? -int(w) : int(w));
      if (live) {
        for (int i = 0; i + 1 < int(internet_radio::kWavePoints); ++i) {
          const int16_t x0 = 9 + i * 222 / 119, x1 = 9 + (i + 1) * 222 / 119;
          const int16_t y0 = mid - v.wave[i] * 46 / peak, y1 = mid - v.wave[i + 1] * 46 / peak;
          d.drawLine(x0, y0, x1, y1, green);
          d.drawLine(x0, y0 + 1, x1, y1 + 1, green);
        }
      }
      break;
    }
    default: {  // Bass, middle and treble as rings in the Pulse colours, a dot for the level.
      int bass = 0, mids = 0, highs = 0;
      for (int i = 0; i < 4; ++i) bass += gRadioShown[i];
      for (int i = 4; i < 10; ++i) mids += gRadioShown[i];
      for (int i = 10; i < 16; ++i) highs += gRadioShown[i];
      const int16_t rb = int16_t(8 + bass / 4 * 38 / 255), rm = int16_t(6 + mids / 6 * 30 / 255), rh = int16_t(4 + highs / 6 * 22 / 255);
      d.drawCircle(120, mid, 46, dim);
      d.drawCircle(120, mid, rb, cyan); d.drawCircle(120, mid, rb - 1, cyan);
      d.drawCircle(120, mid, rm, violet); d.drawCircle(120, mid, rm - 1, violet);
      d.drawCircle(120, mid, rh, amber); d.drawCircle(120, mid, rh - 1, amber);
      d.fillCircle(120, mid, 2, live ? white : dim);
      break;
    }
  }
  // The station above, the clock beside it.
  const auto* station = gRadioSelection.station();
  const String name = station ? station->name : "Internet Radio";
  String hhmm = "--:--";
  const time_t clock = time(nullptr);
  if (clock > 100000) {
    struct tm ti; localtime_r(&clock,&ti); char buf[6];
    clock_display::format(buf,sizeof(buf),ti.tm_hour,ti.tm_min,gDeviceSettings.clockTwelveHour);
    hhmm = buf;
  }
  d.setFont(&fonts::Font2);
  const int16_t clockX = kBigRight - d.textWidth(hhmm);
  d.setTextColor(muted); d.setCursor(clockX, 1); d.print(hhmm);
  const bool wideName = radioTextFont(name);
  d.setTextColor(white); d.setCursor(kBigLeft, wideName ? 3 : 1);
  d.print(radioFit(name, clockX - kBigLeft - 8));
  // The song below, scrolling if it's long, or what the radio is doing.
  bool song = false;
  const String status = radioStatusText(&song);
  const bool wide = radioTextFont(status);
  d.setTextColor(song ? white : radioBusy() || gRadioRetryWaiting ? green : amber);
  d.setCursor(kBigLeft, wide ? 120 : 118);
  d.print(song ? radioMarquee(status, 222) : radioFit(status, 222));
  // The look's name for a moment after it changes, with the keys.
  if (now - gRadioLookShownMs < 1500) {
    d.fillRoundRect(60, 46, 120, 40, 6, TFT_BLACK);
    d.drawRoundRect(60, 46, 120, 40, 6, cyan);
    d.setFont(&fonts::Font2);
    d.setTextColor(cyan);
    const char* look = kRadioLooks[gRadioLook];
    d.setCursor(120 - d.textWidth(look) / 2, 50); d.print(look);
    d.setFont(&fonts::Font0);
    d.setTextColor(muted);
    const char* keys = "V next  Esc back";
    d.setCursor(120 - d.textWidth(keys) / 2, 72); d.print(keys);
    d.setFont(&fonts::Font2);
  }
}
void renderInternetRadioUi() {
  switch (gRadioView) {
    case RadioView::Visual: renderRadioVisual(); break;
    case RadioView::Find: renderRadioFind(); break;
    case RadioView::Results: renderRadioResults(); break;
    case RadioView::Remove: renderRadioRemove(); break;
    default: renderRadioPlayer(); break;
  }
  renderLauncherOverlay(); renderDebugOverlay();
}
} // namespace
