// Internet radio: the clock/Pulse palette and a separate streaming owner. The stations are a
// text file on the SD card (radio_catalog.h says how it reads), and Find adds to it from
// radio-browser.info, a public directory that needs no account and no bridge.
namespace {
enum class RadioView : uint8_t { Player, Find, Results, Remove, Visual, Move, NewGenre, Place };
constexpr const char* kRadioDir = "/radio";
constexpr const char* kRadioListPath = "/radio/stations.txt";
constexpr const char* kRadioListNewPath = "/radio/stations.new";
constexpr const char* kRadioLastPath = "/radio/last.txt";
constexpr const char* kRadioCountryPath = "/radio/country.txt";
constexpr const char* kRadioVisualsSeenPath = "/radio/visuals.seen";  // The keys card has been shown.
constexpr const char* kRadioVisualsPath = "/radio/visuals.txt";        // The look, and the logo on or off.
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
// Playing on behind another screen: since when, for Home's hint, and its genre's colour there.
// Ctrl+M plays again the last station that played from the list.
uint32_t gRadioBehindSinceMs = 0;
String gRadioResumeUrl, gRadioResumeName;
uint16_t gRadioResumeInk = 0;
// Why it stopped by itself behind a screen, which Home says for a while.
String gRadioBehindNote;
uint32_t gRadioBehindNoteUntilMs = 0;
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
// The card with the keys shows once, the first time the visuals open on this Cardputer.
bool gRadioVisualsKnown = false;
uint32_t gRadioCardUntilMs = 0;
// I shows the station's logo with the look, or hides it; it starts hidden. What changed last
// is named beside the clock for a moment: a look, or the logo going on or off.
bool gRadioVisualLogo = false;
const char* gRadioNamed = "";
uint8_t gRadioShown[internet_radio::kBands] = {}, gRadioPeaks[internet_radio::kBands] = {};
// Station art, kept on the card in the canvas's own 8-bit colours, dithered, at each size it's
// drawn: the tile's 60, and the visuals' 80 beside Bars and Wave, 64 amid Mirror and a 48 label
// in Rings. /radio/logos, from before dithering, is no longer read.
constexpr const char* kRadioLogoDir = "/radio/art";
constexpr int16_t kRadioLogoSide = 60;
constexpr int16_t kRadioArtSides[] = {60, 80, 64, 48};
constexpr size_t kRadioLogoBytes = size_t(kRadioLogoSide) * kRadioLogoSide, kRadioBigBytes = 80 * 80;
int16_t radioArtSide(uint8_t look) { return look == 1 ? 64 : look == 3 ? 48 : 80; }
enum class RadioLogoFile : uint8_t { Unknown, Have, None, Absent };  // Absent: no file, so ask.
uint8_t* gRadioLogo = nullptr;           // The shown station's, when the card has it.
uint8_t* gRadioBigLogo = nullptr;        // The visuals' size for the look, while they show it.
uint32_t gRadioBigLogoFor = 0;
int16_t gRadioBigLogoSide = 0;
uint32_t gRadioLogoFor = 0;              // Whose: its stream address's hash.
RadioLogoFile gRadioLogoFile = RadioLogoFile::Unknown;
uint32_t gRadioLogoAsked = 0;            // The station a logo job is for.
uint32_t gRadioRestSinceMs = 0;          // When the selection last changed.
uint32_t gRadioLogoTried[16] = {};       // Failed this session, so not tried again until next.
uint8_t gRadioLogoTriedCount = 0;
String gRadioLogoQueuedUrl, gRadioLogoQueuedName, gRadioLogoQueuedLogo;  // From a save.
// Move and remove: the station they're for, taken when M or Del is pressed.
uint16_t gRadioRemoveLine = 0;
String gRadioRemoveName, gRadioRemoveUrl, gRadioRemoveGenre, gRadioRemoveCountry;
int gRadioMoveSel = 0;  // A genre, then New genre, then Change its place, then Remove.
String gRadioNewGenre;
// Place: the genre's stations as list indices, in their file order (the slots) and in the order
// being made, with where the moving one is now and was.
uint8_t gRadioPlaceSlots[radio_catalog::kMaxStations], gRadioPlaceOrder[radio_catalog::kMaxStations];
uint8_t gRadioPlaceCount = 0, gRadioPlaceAt = 0, gRadioPlaceFrom = 0;

// Either player holds the speaker and the heap, so everything that waits for the radio waits
// for podcasts too.
bool radioBusy() { return internet_radio::busy() || podcast_player::busy(); }
// The worker plays, or is getting ready to: not a search or a logo, which also keep it busy.
bool radioStreaming() { return radioBusy() && !internet_radio::searching() && !internet_radio::fetchingLogo(); }
// Five percent a press, landing on the nearest five, so the number reads 60, 65, 70.
void radioStepVolume(int direction) {
  const int pct = (int(gSpeakerVolume) * 100 + 127) / 255;
  int next = direction > 0 ? (pct / 5 + 1) * 5 : ((pct + 4) / 5 - 1) * 5;
  next = next < 0 ? 0 : next > 100 ? 100 : next;
  const int target = (next * 255 + 50) / 100;
  // At either end it says so, the same as everywhere else.
  adjustSpeakerVolume(target == int(gSpeakerVolume) ? direction : target - int(gSpeakerVolume));
}
void radioInvalidate() { gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
bool radioTakesText() {
  return gUiMode == UiMode::Radio && (gRadioView == RadioView::Find || gRadioView == RadioView::NewGenre);
}
bool radioTakesArrows() {
  return gUiMode == UiMode::Radio && (gRadioView == RadioView::Find || gRadioView == RadioView::Results ||
                                      gRadioView == RadioView::Move || gRadioView == RadioView::Place);
}
void stopRadioStream() {
  gRadioWantPlaying = false;
  gRadioStartPending = false;
  gRadioRetryWaiting = false;
  gRadioRetries = 0;
  gRadioSearchPending = false;
  gRadioSearching = false;  // A stopped search shows nothing.
  internet_radio::stop();
  radioInvalidate();
}
// For anything that needs the speaker or the network to itself: both players stop.
void stopInternetRadio() {
  stopRadioStream();
  stopPodcasts();
}
// The radio wants to play: it is, it's tuning, or it's between reconnects.
bool radioOn() { return gRadioWantPlaying; }
// Screens the radio plays on behind. None of them needs the network, the mic, the speaker or
// much memory; every other screen stops it, as before.
bool radioMayPlayIn(UiMode mode) {
  switch (mode) {
    case UiMode::Radio: case UiMode::Home: case UiMode::Clock: case UiMode::Battery: case UiMode::Focus:
    case UiMode::Notes: case UiMode::Calculator: case UiMode::Alarm: case UiMode::Stopwatch: case UiMode::Calendar:
      return true;
    default:
      return false;
  }
}
bool deferInternetRadioMode(UiMode mode) {
  // Each player may keep going on its own screen, but never into the other's. The radio's
  // stream also plays on behind the light screens; anything else the radio's doing stops.
  const bool keeps = (mode == UiMode::Radio && !podcast_player::busy()) || (mode == UiMode::Podcasts && !internet_radio::busy()) ||
                     (radioMayPlayIn(mode) && radioOn() && !podcast_player::busy());
  if (!radioBusy() || keeps) return false;
  const bool podcast = podcast_player::busy();
  stopInternetRadio(); gRadioModePending = true; gRadioNextMode = mode;
  setStatus(podcast ? "Stopping podcast..." : "Stopping radio...", 800);
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
// Copies the file with some of its lines replaced, or left out where the replacement is empty,
// so every note and hand-typed station stays as it was and a station keeps its place in the
// file. lines go in ascending order, and each one has to be there.
bool radioRewriteLines(const uint16_t* lines, const String* replacements, size_t count) {
  File in = SD.open(kRadioListPath, "r");
  if (!in) return false;
  SD.remove(kRadioListNewPath);
  File out = SD.open(kRadioListNewPath, FILE_WRITE);
  if (!out) { in.close(); return false; }
  uint8_t chunk[256], kept[256];
  size_t n = 0, next = 0;
  uint16_t line = 1;
  bool ok = true, start = true, skipping = false;
  const auto flush = [&]() { if (n && out.write(kept, n) != n) ok = false; n = 0; };
  for (;;) {
    const size_t got = in.read(chunk, sizeof(chunk));
    if (!got) break;
    for (size_t i = 0; i < got && ok; ++i) {
      if (start) {
        start = false;
        skipping = next < count && lines[next] == line;
        if (skipping) {
          flush();
          const String& with = replacements[next++];
          if (ok && with.length() && out.write(reinterpret_cast<const uint8_t*>(with.c_str()), with.length()) != with.length()) ok = false;
        }
      }
      if (!skipping) { kept[n++] = chunk[i]; if (n == sizeof(kept)) flush(); }
      if (chunk[i] == '\n') { ++line; start = true; }
    }
    if (!ok) break;
  }
  flush();
  ok = ok && next == count;
  in.close();
  out.close();
  if (!ok) { SD.remove(kRadioListNewPath); return false; }
  // The old file stays until the new one is whole, so a power cut loses at most this change.
  SD.remove(kRadioListPath);
  return SD.rename(kRadioListNewPath, kRadioListPath);
}
bool radioRewriteLine(uint16_t line, const char* replacement = nullptr) {
  const String with = replacement ? replacement : "";
  return radioRewriteLines(&line, &with, 1);
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
  free(gRadioLogo);
  gRadioLogo = nullptr;
  free(gRadioBigLogo);
  gRadioBigLogo = nullptr;
  gRadioBigLogoFor = 0;
  gRadioBigLogoSide = 0;
  gRadioLogoFor = gRadioLogoAsked = 0;
  gRadioLogoFile = RadioLogoFile::Unknown;
  gRadioLogoQueuedUrl = "";
  internet_radio::clearLogo();
  delete gRadioList;
  gRadioList = nullptr;
  gRadioSelection.list = nullptr;
  internet_radio::clearFound();
  gRadioView = RadioView::Player;
}
void radioReadVisuals() {
  if (!gRadioListOnCard) return;
  File file = SD.open(kRadioVisualsPath, "r");
  if (!file) return;
  char text[8] = {};
  file.read(reinterpret_cast<uint8_t*>(text), sizeof(text) - 1);
  file.close();
  if (text[0] >= '0' && text[0] < char('0' + kRadioLookCount)) gRadioLook = uint8_t(text[0] - '0');
  // The logo setting counts only from a file written since logos start hidden: one from before
  // has it on because that was the default then, not because anyone chose it.
  gRadioVisualLogo = text[1] == ' ' && text[2] == '1' && text[3] == ' ' && text[4] == '2';
}
void radioWriteVisuals() {
  if (!gRadioListOnCard) return;
  File file = SD.open(kRadioVisualsPath, FILE_WRITE);
  if (!file) return;
  const char text[6] = {char('0' + gRadioLook), ' ', gRadioVisualLogo ? '1' : '0', ' ', '2', '\n'};
  file.write(reinterpret_cast<const uint8_t*>(text), sizeof(text));
  file.close();
}
void beginInternetRadio() {
  gRadioView = RadioView::Player;
  radioLoadList();
  radioReadLast();
  radioReadCountry();
  if (gRadioListOnCard && SD.exists(kRadioVisualsSeenPath)) gRadioVisualsKnown = true;
  radioReadVisuals();
  gRadioOnCountry = false;
  gRadioNotice = gRadioListNote.isEmpty() ? "Enter to listen" : gRadioListNote;
  // Back from a screen it played on behind: the station playing is the one shown.
  gRadioBehindSinceMs = 0;
  if (radioOn() && gRadioList) {
    const int on = gRadioList->find(gRadioWantUrl.c_str());
    if (on >= 0) gRadioSelection.select(on);
    if (gRadioListNote.isEmpty()) gRadioNotice = "";
  }
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
  if (gRadioCountry.length() == 1) {
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
  else if (!found) gRadioNotice = String("Nothing found") + (gRadioSearchedQuery.isEmpty() ? "" : " for " + gRadioSearchedQuery) +
                                  (gRadioSearchedCountry.isEmpty() ? "" : " in " + gRadioSearchedCountry);
  else if (gRadioView == RadioView::Find) { gRadioView = RadioView::Results; gRadioFoundSel = 0; gRadioNotice = ""; }
  radioInvalidate();
}
const internet_radio::Found* radioFound() { return internet_radio::found(size_t(gRadioFoundSel)); }
bool radioPlayingUrl(const char* url) {
  return (gRadioWantPlaying || radioStreaming()) && gRadioWantUrl == url;
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
  // With nothing typed there's no genre to go by, so the station's own tags choose, as by name.
  radio_catalog::genreFor(*gRadioList, gRadioSearchedByGenre && !gRadioSearchedQuery.isEmpty(), gRadioSearchedQuery.c_str(), found->tags, genre, sizeof(genre));
  if (gRadioList->findGenre(genre) < 0 && gRadioList->genres() >= radio_catalog::kMaxGenres) {
    gRadioNotice = "Too many genres: 24 at most";
    return;
  }
  char line[radio_catalog::kGenreBytes + radio_catalog::kNameBytes + internet_radio::detail::kUrlSize + 16];
  const size_t size = radio_catalog::formatLine(line, sizeof(line), genre, found->name, found->url, found->country);
  if (gRadioList->size() + size + 1 > radio_catalog::kMaxFileBytes) { gRadioNotice = "stations.txt is full: 16 KB"; return; }
  if (!size || !radioAppendLine(line, size)) { gRadioNotice = "Couldn't save to the SD card"; return; }
  const String url = found->url;
  radioLoadList();
  gRadioSelection.select(gRadioList ? gRadioList->find(url.c_str()) : -1);
  gRadioNotice = String("Saved to ") + genre;
  gRadioNoticeGood = true;
  // Its logo's address came with it, so that's asked for first, once the worker is free.
  if (found->logo[0]) { gRadioLogoQueuedUrl = url; gRadioLogoQueuedName = found->name; gRadioLogoQueuedLogo = found->logo; }
}
// Back to the player, which only ever plays a station on the list.
void radioLeaveResults() {
  if (gRadioWantPlaying || radioStreaming()) {
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
  gRadioRemoveCountry = station->country;
  gRadioView = RadioView::Remove;
  radioInvalidate();
}
// --- Moving -------------------------------------------------------------------------------
// M: the list of genres to move the station to, then New genre, then Remove.
void radioAskMove() {
  radioAskRemove();  // The same checks, and it takes the same station.
  if (gRadioView != RadioView::Remove) return;
  gRadioView = RadioView::Move;
  gRadioMoveSel = max(0, gRadioList->findGenre(gRadioRemoveGenre.c_str()));
  radioInvalidate();
}
void radioMoveTo(String genre) {
  genre.trim();
  gRadioNoticeGood = false;
  if (genre.isEmpty()) { gRadioNotice = "Type the new genre's name"; radioInvalidate(); return; }
  const int existing = gRadioList->findGenre(genre.c_str());
  const String target = existing >= 0 ? String(gRadioList->genre(size_t(existing))) : genre;  // Its spelling, if there's one.
  gRadioView = RadioView::Player;
  if (existing >= 0 && radio_catalog::sameText(target.c_str(), gRadioRemoveGenre.c_str())) {
    gRadioNotice = "Already in " + target;
    radioInvalidate();
    return;
  }
  // A new genre has to fit, unless this was its old genre's only station.
  int left = 0;
  const int from = gRadioList->findGenre(gRadioRemoveGenre.c_str());
  for (size_t i = 0; i < gRadioList->count(); ++i) if (int(gRadioList->at(i).genre) == from) ++left;
  if (existing < 0 && gRadioList->genres() >= radio_catalog::kMaxGenres && left > 1) {
    gRadioNotice = "Too many genres: 24 at most";
    radioInvalidate();
    return;
  }
  char line[radio_catalog::kGenreBytes + radio_catalog::kNameBytes + internet_radio::detail::kUrlSize + 16];
  if (!radio_catalog::formatLine(line, sizeof(line), target.c_str(), gRadioRemoveName.c_str(), gRadioRemoveUrl.c_str(), gRadioRemoveCountry.c_str()) ||
      !radioRewriteLine(gRadioRemoveLine, line)) {
    gRadioNotice = "Couldn't change the SD card";
    radioInvalidate();
    return;
  }
  radioLoadList();
  // By its line, which a move keeps: an address can be in the list twice.
  gRadioSelection.select(gRadioList ? gRadioList->findLine(gRadioRemoveLine) : -1);
  char shown[radio_catalog::kGenreBytes + 1];
  const String moved = gRadioSelection.station() ? String(gRadioSelection.genreName()) : target;
  snprintf(shown, sizeof(shown), "%s", moved.c_str());
  gRadioNotice = String("Moved to ") + shown;
  gRadioNoticeGood = true;
  radioInvalidate();
}
// The Move list: the genres, then New genre, then Change its place when the genre has more
// than one station, then Remove.
enum class RadioMoveRow : uint8_t { Genre, NewGenre, Place, Remove };
int radioGenreSize(int genre) {
  int n = 0;
  if (gRadioList && genre >= 0) for (size_t i = 0; i < gRadioList->count(); ++i) if (int(gRadioList->at(i).genre) == genre) ++n;
  return n;
}
bool radioCanPlace() { return gRadioList && radioGenreSize(gRadioList->findGenre(gRadioRemoveGenre.c_str())) > 1; }
int radioMoveRows() { return int(gRadioList ? gRadioList->genres() : 0) + (radioCanPlace() ? 3 : 2); }
RadioMoveRow radioMoveRowAt(int i) {
  const int genres = int(gRadioList ? gRadioList->genres() : 0);
  if (i < genres) return RadioMoveRow::Genre;
  if (i == genres) return RadioMoveRow::NewGenre;
  return radioCanPlace() && i == genres + 1 ? RadioMoveRow::Place : RadioMoveRow::Remove;
}
// Its genre's stations in their order, all of them whatever the country filter shows, since
// the order is the file's.
void radioAskPlace() {
  const int genre = gRadioList->findGenre(gRadioRemoveGenre.c_str());
  gRadioPlaceCount = 0;
  for (size_t i = 0; i < gRadioList->count(); ++i) {
    if (int(gRadioList->at(i).genre) != genre) continue;
    if (gRadioList->at(i).line == gRadioRemoveLine) gRadioPlaceAt = gRadioPlaceFrom = gRadioPlaceCount;
    gRadioPlaceSlots[gRadioPlaceCount] = gRadioPlaceOrder[gRadioPlaceCount] = uint8_t(i);
    ++gRadioPlaceCount;
  }
  gRadioView = RadioView::Place;
  gRadioNotice = "";
}
// The genre's lines stay in their own slots in the file, refilled in the new order.
void radioSavePlace() {
  gRadioView = RadioView::Player;
  gRadioNoticeGood = false;
  if (gRadioPlaceAt == gRadioPlaceFrom) { gRadioNotice = "Kept its place"; radioInvalidate(); return; }
  std::vector<uint16_t> lines(gRadioPlaceCount);
  std::vector<String> texts(gRadioPlaceCount);
  char line[radio_catalog::kGenreBytes + radio_catalog::kNameBytes + internet_radio::detail::kUrlSize + 16];
  const String genre = gRadioRemoveGenre;
  for (uint8_t k = 0; k < gRadioPlaceCount; ++k) {
    const auto& s = gRadioList->at(gRadioPlaceOrder[k]);
    lines[k] = gRadioList->at(gRadioPlaceSlots[k]).line;
    if (!radio_catalog::formatLine(line, sizeof(line), genre.c_str(), s.name, s.url, s.country)) { gRadioNotice = "Couldn't change the SD card"; radioInvalidate(); return; }
    texts[k] = line;
  }
  const uint16_t now = lines[gRadioPlaceAt];
  if (!radioRewriteLines(lines.data(), texts.data(), lines.size())) { gRadioNotice = "Couldn't change the SD card"; radioInvalidate(); return; }
  radioLoadList();
  gRadioSelection.select(gRadioList ? gRadioList->findLine(now) : -1);
  gRadioNotice = "Now " + String(gRadioPlaceAt + 1) + " of " + String(gRadioPlaceCount) + " in " + genre;
  gRadioNoticeGood = true;
  radioInvalidate();
}
bool radioPlaceKey(char key, const Keyboard_Class::KeysState& keys) {
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  const bool up = key == char(1) || key == char(3) || key == ';' || key == ',' || lower == 'w';
  const bool down = key == char(2) || key == char(4) || key == '.' || key == '/' || lower == 's';
  // It moves past one neighbour a press, and stops at either end rather than wrapping round.
  if (up && gRadioPlaceAt > 0) { std::swap(gRadioPlaceOrder[gRadioPlaceAt], gRadioPlaceOrder[gRadioPlaceAt - 1]); --gRadioPlaceAt; }
  else if (down && gRadioPlaceAt + 1 < gRadioPlaceCount) { std::swap(gRadioPlaceOrder[gRadioPlaceAt], gRadioPlaceOrder[gRadioPlaceAt + 1]); ++gRadioPlaceAt; }
  else if (keys.enter || key == '\n' || key == '\r') radioSavePlace();
  radioInvalidate();
  return true;
}
bool radioMoveKey(char key, const Keyboard_Class::KeysState& keys) {
  const int rows = radioMoveRows();
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  if (key == char(1) || key == char(3) || key == ';' || key == ',' || lower == 'w') gRadioMoveSel = (gRadioMoveSel + rows - 1) % rows;
  else if (key == char(2) || key == char(4) || key == '.' || key == '/' || lower == 's') gRadioMoveSel = (gRadioMoveSel + 1) % rows;
  else if (keys.enter || key == '\n' || key == '\r') {
    switch (radioMoveRowAt(gRadioMoveSel)) {
      case RadioMoveRow::Genre: radioMoveTo(gRadioList->genre(size_t(gRadioMoveSel))); break;
      case RadioMoveRow::NewGenre: gRadioView = RadioView::NewGenre; gRadioNewGenre = ""; gRadioNotice = ""; break;
      case RadioMoveRow::Place: radioAskPlace(); break;
      case RadioMoveRow::Remove: gRadioView = RadioView::Remove; break;
    }
  }
  radioInvalidate();
  return true;
}
// Every key types here, as in Find: a genre can be called anything.
bool radioNewGenreKey(char key, const Keyboard_Class::KeysState& keys) {
  if (key == '\b') { if (!gRadioNewGenre.isEmpty()) gRadioNewGenre.remove(gRadioNewGenre.length() - 1); }
  else if (keys.enter || key == '\n' || key == '\r') radioMoveTo(gRadioNewGenre);
  else if (key >= ' ' && key < 127 && key != '|' && gRadioNewGenre.length() < radio_catalog::kGenreBytes) gRadioNewGenre += key;
  radioInvalidate();
  return true;
}
void radioRemove() {
  gRadioView = RadioView::Player;
  if (radioPlayingUrl(gRadioRemoveUrl.c_str())) stopInternetRadio();
  if (!radioRewriteLine(gRadioRemoveLine)) { gRadioNotice = "Couldn't change the SD card"; radioInvalidate(); return; }
  radioLoadList();
  if (gRadioResumeUrl == gRadioRemoveUrl) gRadioResumeUrl = "";  // Ctrl+M won't bring it back.
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
    case RadioView::Move:
      gRadioView = RadioView::Player;
      gRadioNotice = "";
      break;
    case RadioView::NewGenre:
      gRadioView = RadioView::Move;
      gRadioNotice = "";
      break;
    case RadioView::Place:
      gRadioView = RadioView::Player;
      gRadioNotice = "Kept its place";
      gRadioNoticeGood = false;
      break;
    default:
      return false;
  }
  radioInvalidate();
  return true;
}

// --- Logos ----------------------------------------------------------------------------------
uint32_t radioHash(const char* text) {
  uint32_t h = 2166136261u;
  for (const char* p = text; *p; ++p) h = (h ^ uint8_t(*p)) * 16777619u;
  return h ? h : 1;
}
String radioLogoPath(uint32_t hash, const char* kind) {
  char path[40];
  snprintf(path, sizeof(path), "%s/%08lx.%s", kRadioLogoDir, static_cast<unsigned long>(hash), kind);
  return String(path);
}
#ifndef RADIO_HOST_TEST
// The service's JPEG, decoded once at a side and a scale into 16-bit colour, then dithered
// down to the canvas's 8 bits so shading and greys don't band.
bool radioDecodeLogo(const uint8_t* jpeg, size_t size, uint8_t* out, int16_t side, float scale) {
  M5Canvas logo(&M5Cardputer.Display);
  logo.setColorDepth(16);
  logo.setPsram(false);
  if (!logo.createSprite(side, side)) return false;
  logo.fillSprite(TFT_BLACK);
  const bool ok = logo.drawJpg(jpeg, size, 0, 0, side, side, 0, 0, scale, scale);
  if (ok) radio_image::dither332(side, [&](int x, int y, int& r, int& g, int& b) {
    const auto c = logo.readPixelRGB(x, y); r = c.r; g = c.g; b = c.b; }, out);
  logo.deleteSprite();
  return ok;
}
#endif
bool radioWriteLogoFile(uint32_t hash, const char* kind, const uint8_t* bytes, size_t size) {
  if (!ensureDirRecursive(SD, kRadioLogoDir)) return false;
  File file = SD.open(radioLogoPath(hash, kind).c_str(), FILE_WRITE);
  if (!file) return false;
  const bool ok = !size || file.write(bytes, size) == size;
  file.close();
  return ok;
}
// Shows the selected station's logo if the card has it, and notes whether to ask for one.
void radioShowLogoFor(const radio_catalog::Station* station) {
  const uint32_t hash = station ? radioHash(station->url) : 0;
  if (hash == gRadioLogoFor) return;
  gRadioLogoFor = hash;
  gRadioRestSinceMs = millis();
  gRadioLogoFile = RadioLogoFile::Unknown;
  if (!hash || !gRadioListOnCard) { free(gRadioLogo); gRadioLogo = nullptr; return; }
  File file = SD.open(radioLogoPath(hash, "60").c_str(), "r");
  if (file && file.size() == kRadioLogoBytes) {
    if (!gRadioLogo) gRadioLogo = static_cast<uint8_t*>(malloc(kRadioLogoBytes));
    if (gRadioLogo && file.read(gRadioLogo, kRadioLogoBytes) == kRadioLogoBytes) gRadioLogoFile = RadioLogoFile::Have;
  }
  if (file) file.close();
  if (gRadioLogoFile != RadioLogoFile::Have) {
    free(gRadioLogo);
    gRadioLogo = nullptr;
  }
  if (gRadioLogoFile != RadioLogoFile::Have)
    gRadioLogoFile = SD.exists(radioLogoPath(hash, "no").c_str()) ? RadioLogoFile::None : RadioLogoFile::Absent;
}
// The visuals' large logo, read while they show it, and let go otherwise: 9 KB isn't kept for
// a screen that isn't up.
void serviceRadioBigLogo() {
  const auto* station = gRadioSelection.station();
  const bool want = gRadioView == RadioView::Visual && gRadioVisualLogo && station && gRadioListOnCard;
  const uint32_t hash = want ? radioHash(station->url) : 0;
  const int16_t side = want ? radioArtSide(gRadioLook) : 0;
  if (hash == gRadioBigLogoFor && side == gRadioBigLogoSide) return;
  gRadioBigLogoFor = hash;
  gRadioBigLogoSide = side;
  free(gRadioBigLogo);
  gRadioBigLogo = nullptr;
  if (!hash) return;
  const size_t bytes = size_t(side) * side;
  File file = SD.open(radioLogoPath(hash, String(side).c_str()).c_str(), "r");
  if (file && file.size() == bytes) {
    gRadioBigLogo = static_cast<uint8_t*>(malloc(bytes));
    if (gRadioBigLogo && file.read(gRadioBigLogo, bytes) != bytes) { free(gRadioBigLogo); gRadioBigLogo = nullptr; }
  }
  if (file) file.close();
}
bool radioLogoTried(uint32_t hash) {
  for (uint8_t i = 0; i < gRadioLogoTriedCount; ++i) if (gRadioLogoTried[i] == hash) return true;
  return false;
}
// Fetches only while nothing plays or searches, after a second's rest on a station, so browsing
// doesn't set off a fetch per station and a stream never waits on one. Enter stops one at once.
void serviceRadioLogos() {
  if (gRadioLogoAsked && !internet_radio::fetchingLogo()) {
    const uint32_t hash = gRadioLogoAsked;
    gRadioLogoAsked = 0;
    const auto result = internet_radio::logoResult();
    if (result == internet_radio::LogoResult::Found) {
      size_t size = 0;
      const uint8_t* jpeg = internet_radio::logoJpeg(size);
      uint8_t* pixels = static_cast<uint8_t*>(malloc(kRadioLogoBytes));
      uint8_t* big = static_cast<uint8_t*>(malloc(kRadioBigBytes));
      uint16_t width = 0, height = 0;
      const bool room = pixels && big;  // Out of memory isn't the logo's fault: try again later.
      bool decoded = room && jpeg && radio_image::jpegSize(jpeg, size, width, height);
      // The larger sizes first; the tile's last, so a part-written set is never taken for whole.
      for (size_t i = 1; decoded && i <= sizeof(kRadioArtSides) / sizeof(kRadioArtSides[0]); ++i) {
        const int16_t side = kRadioArtSides[i % (sizeof(kRadioArtSides) / sizeof(kRadioArtSides[0]))];
        uint8_t* into = side == kRadioLogoSide ? pixels : big;
        decoded = radioDecodeLogo(jpeg, size, into, side, float(side) / float(min(width, height))) &&
                  radioWriteLogoFile(hash, String(side).c_str(), into, size_t(side) * side);
      }
      free(big);
      if (decoded) {
        if (hash == gRadioLogoFor) { free(gRadioLogo); gRadioLogo = pixels; pixels = nullptr; gRadioLogoFile = RadioLogoFile::Have; radioInvalidate(); }
      } else if (room) {
        radioWriteLogoFile(hash, "no", nullptr, 0);  // A JPEG that won't decode won't next time.
        if (hash == gRadioLogoFor) gRadioLogoFile = RadioLogoFile::None;
      }
      free(pixels);
    } else if (result == internet_radio::LogoResult::Missing) {
      radioWriteLogoFile(hash, "no", nullptr, 0);
      if (hash == gRadioLogoFor) gRadioLogoFile = RadioLogoFile::None;
    } else if (result == internet_radio::LogoResult::Failed && gRadioLogoTriedCount < 16) {
      gRadioLogoTried[gRadioLogoTriedCount++] = hash;
    }
    internet_radio::clearLogo();
  }
  const auto* station = gRadioSelection.station();
  radioShowLogoFor(station);
  if (gRadioLogoAsked || radioBusy() || gRadioWantPlaying || gRadioStartPending || gRadioRetryWaiting ||
      gRadioSearchPending || gRadioSearching || !gRadioListOnCard || !radioStartAllowed()) return;
  if (!gRadioLogoQueuedUrl.isEmpty()) {
    const uint32_t hash = radioHash(gRadioLogoQueuedUrl.c_str());
    if (!SD.exists(radioLogoPath(hash, "60").c_str()) &&
        internet_radio::startLogo(gRadioLogoQueuedUrl.c_str(), gRadioLogoQueuedName.c_str(), gRadioLogoQueuedLogo.c_str())) gRadioLogoAsked = hash;
    gRadioLogoQueuedUrl = "";
    return;
  }
  // Only for the station the play screen shows, not one left behind it during a search.
  if (gRadioView != RadioView::Player || !station || gRadioLogoFile != RadioLogoFile::Absent ||
      millis() - gRadioRestSinceMs < 1000 || radioLogoTried(gRadioLogoFor)) return;
  if (internet_radio::startLogo(station->url, station->name, "")) gRadioLogoAsked = gRadioLogoFor;
  else if (gRadioLogoTriedCount < 16) gRadioLogoTried[gRadioLogoTriedCount++] = gRadioLogoFor;  // A name too long, say.
}
// --- Behind other screens -----------------------------------------------------------------
uint16_t radioGenreInk(const char* genre);
// The station Ctrl+M plays again, and its genre's colour for Home: list index `on`, which plays.
void radioKeepForResume(int on) {
  if (!gRadioList || on < 0) return;
  gRadioResumeUrl = gRadioWantUrl;
  gRadioResumeName = gRadioWantName;
  gRadioResumeInk = radioGenreInk(gRadioList->genre(gRadioList->at(size_t(on)).genre));
}
// Off the radio's screen. True while the stream plays on behind this one: the list, results and
// logos go back to the heap once the worker has finished any logo, and the stream, its
// reconnects and nothing else carry on. False when the radio should stop.
bool radioBehind() {
  if (gRadioModePending || !radioOn() || podcast_player::busy() || !radioMayPlayIn(gUiMode)) return false;
  if (gRadioList && !internet_radio::fetchingLogo()) {
    // Only a station from the list: the radio's screen shows no other when it opens again.
    const int on = gRadioList->find(gRadioWantUrl.c_str());
    if (on < 0) { setStatus("Radio stopped: not on your list", 1800); return false; }
    radioKeepForResume(on);
    gRadioBehindSinceMs = max<uint32_t>(millis(), 1);
    gRadioBehindNoteUntilMs = 0;
    radioRelease();
  }
  return true;
}
// It stopped by itself behind a screen, which says why. Home says so for a while.
void radioStoppedBehind(const String& why) {
  gRadioBehindNote = why == "Idle" ? String("The stream ended") : why;  // A clean end, out of tries.
  gRadioBehindNoteUntilMs = max<uint32_t>(millis() + 8000, 1);
  setStatus(gRadioBehindNote.startsWith("Radio") ? gRadioBehindNote : "Radio: " + gRadioBehindNote, 3000);
}
void serviceInternetRadio() {
  if (gRadioModePending && !radioBusy()) {
    gRadioModePending = false; const UiMode next = gRadioNextMode;
    setUiMode(next); return;
  }
  // The worker only analyses frames while the visuals are on screen.
  internet_radio::wantVisuals(gUiMode == UiMode::Radio && gRadioView == RadioView::Visual && !gRadioModePending);
  const bool here = gUiMode == UiMode::Radio;
  if (!here && !radioBehind()) {
    // A reconnect or search still waiting would otherwise start when the radio opens again.
    // Only the radio's own worker: the podcast player looks after itself.
    if (internet_radio::busy() || gRadioWantPlaying || gRadioStartPending || gRadioRetryWaiting || gRadioSearchPending || gRadioSearching) stopRadioStream();
    else if (gRadioList) radioRelease();
    return;
  }
  if (here && gRadioSearching && !radioBusy()) { gRadioSearching = false; radioSearchDone(); }
  if (here && gRadioSearchPending && !radioBusy()) { gRadioSearchPending = false; radioStartSearch(); return; }
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
      if (!here) radioStoppedBehind(!gWifiReady ? "Wi-Fi required" : "Busy: try Ctrl+M");
    } else {
      initSpeaker();
      if (!internet_radio::start(gRadioWantUrl.c_str())) {
        gRadioWantPlaying = false; gRadioRetries = 0; gRadioNotice = internet_radio::status();
        if (!here) radioStoppedBehind(gRadioNotice);
      } else {
        gRadioNotice = "";
        gRadioPlayingUrl = gRadioWantUrl;
        if (gRadioList && gRadioList->find(gRadioWantUrl.c_str()) >= 0) {
          radioRememberLast(gRadioWantUrl);
          radioKeepForResume(gRadioList->find(gRadioWantUrl.c_str()));
        }
      }
    }
    radioInvalidate();
  } else if (!radioBusy() && gRadioWantPlaying && !gRadioRetryWaiting) {
    if (!radioScheduleRetry()) {
      gRadioWantPlaying = false; gRadioRetries = 0; gRadioNotice = internet_radio::status();
      if (!here) radioStoppedBehind(gRadioNotice);
    }
    radioInvalidate();
  }
  if (!here) return;
  serviceRadioLogos();
  serviceRadioBigLogo();
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
  else if (key == '-' || key == '_') radioStepVolume(-1);
  else if (key == '+' || key == '=') radioStepVolume(1);
  radioInvalidate();
  return true;
}
bool handleInternetRadioKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  if (gRadioView == RadioView::Find) return radioFindKey(key, keys);
  if (gRadioView == RadioView::Results) return radioResultsKey(key, keys);
  if (gRadioView == RadioView::Move) return radioMoveKey(key, keys);
  if (gRadioView == RadioView::NewGenre) return radioNewGenreKey(key, keys);
  if (gRadioView == RadioView::Place) return radioPlaceKey(key, keys);
  if (gRadioView == RadioView::Remove) {
    if (key == 'y' || key == 'Y') radioRemove();
    else { gRadioView = RadioView::Player; gRadioNotice = "Kept " + gRadioRemoveName; radioInvalidate(); }
    return true;
  }
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  // V opens the visuals, and there it changes the look. Every other key works as it does here.
  if (lower == 'i' && gRadioView == RadioView::Visual) {
    gRadioVisualLogo = !gRadioVisualLogo;
    gRadioNamed = gRadioVisualLogo ? "Logo on" : "Logo off";
    gRadioLookShownMs = millis();
    radioWriteVisuals();
    radioInvalidate();
    return true;
  }
  if (lower == 'v') {
    if (gRadioView == RadioView::Visual) {
      gRadioLook = uint8_t((gRadioLook + 1) % kRadioLookCount);
      gRadioNamed = kRadioLooks[gRadioLook];
      gRadioLookShownMs = millis();
      radioWriteVisuals();
    } else if (!gRadioVisualsKnown) {
      gRadioCardUntilMs = millis() + 3000;
      gRadioVisualsKnown = true;
      if (gRadioListOnCard) {
        File file = SD.open(kRadioVisualsSeenPath, FILE_WRITE);
        if (file) { file.write('1'); file.close(); }
      }
    }
    gRadioView = RadioView::Visual;
    radioInvalidate();
    return true;
  }
  if (lower == 'w' || key == ';') { gRadioSelection.moveGenre(-1); radioRetune(); }
  else if (lower == 's' || key == '.') { gRadioSelection.moveGenre(1); radioRetune(); }
  else if (lower == 'a' || key == ',') { gRadioSelection.moveStation(-1); radioRetune(); }
  else if (lower == 'd' || key == '/') { gRadioSelection.moveStation(1); radioRetune(); }
  else if (lower == 'l') { gRadioSelection.nextCountry(); radioRetune(); }
  else if (lower == 'f') { gRadioView = RadioView::Find; gRadioNotice = ""; radioInvalidate(); }
  else if (key == '\b') radioAskRemove();
  else if (lower == 'm') radioAskMove();
  else if (key == '-' || key == '_') { radioStepVolume(-1); radioInvalidate(); }
  else if (key == '+' || key == '=') { radioStepVolume(1); radioInvalidate(); }
  else if (keys.enter || key == '\r' || key == '\n' || key == ' ') {
    if (gRadioWantPlaying || radioStreaming()) { stopInternetRadio(); gRadioNotice = "Paused"; }
    else radioPlaySelected();
    radioInvalidate();
  }
  return true;
}

// Ctrl+M, from any screen, stops the radio. Stopped, it plays the last station from the list
// again on a screen the radio may play behind, or on the radio's own player the one shown.
// False leaves the key to the screen: podcasts playing, or a screen the radio can't play on.
bool radioMusicKey() {
  if (radioOn()) {
    stopRadioStream();
    gRadioNotice = "Paused";
    gRadioBehindNoteUntilMs = 0;
    setStatus("Radio stopped", 1200);
    return true;
  }
  if (podcast_player::busy() || !radioMayPlayIn(gUiMode)) return false;
  if (gUiMode == UiMode::Radio) {
    if (gRadioView == RadioView::Player || gRadioView == RadioView::Visual) radioPlaySelected();
    return true;
  }
  if (gRadioResumeUrl.isEmpty()) { setStatus("Open Internet Radio to pick a station", 2000); return true; }
  if (!gWifiReady) { setStatus("Radio: Wi-Fi required", 1500); return true; }
  radioPlay(gRadioResumeUrl, gRadioResumeName);
  gRadioBehindSinceMs = max<uint32_t>(millis(), 1);
  gRadioBehindNoteUntilMs = 0;
  setStatus("Radio: " + gRadioResumeName, 1500);
  return true;
}
// Esc past the radio's own views with a player busy: true when it stops, as it always did. The
// radio's stream instead plays on at Home, where Esc goes.
bool radioStopsOnEscape() { return radioBusy() && (!radioOn() || podcast_player::busy()); }

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
uint32_t radioRetrySecondsLeft() {
  const uint32_t left = int32_t(gRadioRetryAtMs - millis()) > 0 ? gRadioRetryAtMs - millis() : 0;
  return (left + 999) / 1000;
}
String radioStatusText(bool* song = nullptr) {
  if (song) *song = false;
  if (gRadioRetryWaiting) {
    if (!gWifiReady) return "Waiting for Wi-Fi...";
    return "Reconnecting in " + String(radioRetrySecondsLeft()) + "s";
  }
  if (gRadioStartPending) return "Tuning...";
  if (radioStreaming()) {
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
// The meter's last fifteen readings, about a tenth of a second apart, newest on the right. The
// tile draws the last five, and Home all of them.
constexpr int kRadioMeterLen = 15;
uint8_t gRadioMeter[kRadioMeterLen] = {};
uint32_t gRadioMeterAtMs = 0;
// Reads the decoded audio's loudness, on a log scale as a VU meter reads, so the bars move with
// the music and settle in a quiet passage.
void radioMeterTick(bool playing) {
  if (!playing) { memset(gRadioMeter, 0, sizeof(gRadioMeter)); return; }
  if (millis() - gRadioMeterAtMs < 90) return;
  gRadioMeterAtMs = millis();
  memmove(gRadioMeter, gRadioMeter + 1, sizeof(gRadioMeter) - 1);
  const uint16_t level = internet_radio::level();
  // Music sits between about 700 and 23000, some 30 dB, so that's what the 14 steps cover.
  // Spread wider, every song would fill the bars and they'd stop looking alive.
  const int h = level < 700 ? 1 : int((log2f(float(level)) - 9.5f) * 14.0f / 5.0f + 0.5f);
  gRadioMeter[kRadioMeterLen - 1] = uint8_t(h < 1 ? 1 : h > 14 ? 14 : h);
}
// Each genre keeps a colour, from its name, so a genre you add gets one too. Twelve colours
// keep the built-in seven apart, and these are in the order that suits them.
uint16_t radioGenreInk(const char* genre) {
  static const uint8_t kInks[12][3] = {{230,60,70},{240,90,150},{150,230,70},{235,200,60},{255,165,40},{255,115,80},
                                       {165,105,255},{90,230,150},{70,165,255},{40,210,190},{210,110,230},{95,125,255}};
  uint32_t h = 2166136261u;
  for (const char* p = genre; *p; ++p) h = (h ^ uint8_t(radio_catalog::lower(*p))) * 16777619u;
  const auto& k = kInks[h % 12];
  return rgb565_local(k[0], k[1], k[2]);
}
// One or two capitals from the first letters of a name's words, so a tile reads like a label.
// ASCII only, which Font4 has; a name in another script gets a note drawn instead.
String radioInitials(const char* name) {
  String out;
  bool start = true;
  for (const char* p = name; *p && out.length() < 2; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (letter && start) out += char(toupper(c));
    if (c >= 0x80) start = false;
    else start = !letter && (c == ' ' || c == '-' || c == '/' || c == '.' || c == '(' || c == '&');
  }
  return out;
}
// The station's tile: its genre's colour, its initials, and while it plays the meter in black
// beneath them. While it connects, a raised bar runs along the meter.
void drawRadioTile(int16_t x, int16_t y, const char* name, const char* genre, bool playing, bool waiting, const uint8_t* logo) {
  auto& d = gCanvas;
  constexpr int16_t kSize = 60;
  const int lit = int((millis() / 150) % 5);
  if (logo) {
    // The station's logo, its corners rounded like the tiles', and while it plays the meter in
    // white on a dark strip, which reads on any logo.
    d.pushImage(x, y, kSize, kSize, reinterpret_cast<const lgfx::rgb332_t*>(logo));
    static const uint8_t kCorner[10] = {7, 5, 3, 2, 2, 1, 1, 0, 0, 0};
    for (int i = 0; i < 10; ++i) {
      if (!kCorner[i]) continue;
      d.fillRect(x, y + i, kCorner[i], 1, TFT_BLACK); d.fillRect(x + kSize - kCorner[i], y + i, kCorner[i], 1, TFT_BLACK);
      d.fillRect(x, y + kSize - 1 - i, kCorner[i], 1, TFT_BLACK); d.fillRect(x + kSize - kCorner[i], y + kSize - 1 - i, kCorner[i], 1, TFT_BLACK);
    }
    if (!playing && !waiting) return;
    d.fillRoundRect(x + 8, y + 41, 44, 15, 4, TFT_BLACK);
    for (int i = 0; i < 5; ++i) {
      const int16_t h = playing ? int16_t(max<int>(1, gRadioMeter[kRadioMeterLen - 5 + i] * 10 / 14)) : (i == lit ? 6 : 2);
      d.fillRect(x + 12 + i * 8, y + 53 - h, 5, h, TFT_WHITE);
    }
    return;
  }
  d.fillRoundRect(x, y, kSize, kSize, 10, radioGenreInk(genre));
  const String initials = radioInitials(name);
  const bool meter = playing || waiting;
  const int16_t top = meter ? y + 8 : y + 17;
  if (!initials.isEmpty()) {
    d.setFont(&fonts::Font4);
    d.setTextColor(TFT_BLACK);
    d.setCursor(x + (kSize - d.textWidth(initials)) / 2, top);
    d.print(initials);
  } else {  // A note: a head and a stem with its flag.
    d.fillCircle(x + 25, top + 18, 6, TFT_BLACK);
    d.fillRect(x + 29, top, 3, 18, TFT_BLACK);
    d.fillRect(x + 29, top, 9, 3, TFT_BLACK);
  }
  if (!meter) return;
  const int16_t left = x + (kSize - 37) / 2, bottom = y + kSize - 6;
  for (int i = 0; i < 5; ++i) {
    const uint8_t m = gRadioMeter[kRadioMeterLen - 5 + i];
    const int16_t h = playing ? (m ? m : 1) : (i == lit ? 6 : 2);
    d.fillRect(left + i * 8, bottom - h, 5, h, TFT_BLACK);
  }
}
// Three media buttons, drawn, with the key beneath each; genre keys on the left and the volume
// on the right, so the screen says how it works without lines of hints.
void drawRadioControls(bool on, uint16_t accent, uint16_t muted) {
  auto& d = gCanvas;
  constexpr int16_t cy = 100, labelY = 115;
  const auto label = [&](int16_t cx, const char* text) {
    d.setFont(&fonts::Font0);
    d.setTextColor(muted);
    d.setCursor(cx - d.textWidth(text) / 2, labelY);
    d.print(text);
  };
  // Genre: up and down.
  d.fillTriangle(16, cy - 3, 24, cy - 3, 20, cy - 9, muted);
  d.fillTriangle(16, cy + 3, 24, cy + 3, 20, cy + 9, muted);
  label(20, "W/S");
  // Previous station, play or pause, next station.
  d.fillTriangle(74, cy, 82, cy - 7, 82, cy + 7, muted);
  d.fillTriangle(66, cy, 74, cy - 7, 74, cy + 7, muted);
  d.fillRect(63, cy - 7, 2, 15, muted);
  label(73, "A");
  d.fillCircle(120, cy, 12, accent);
  if (on) { d.fillRect(114, cy - 6, 4, 13, TFT_BLACK); d.fillRect(122, cy - 6, 4, 13, TFT_BLACK); }
  else d.fillTriangle(116, cy - 7, 116, cy + 7, 127, cy, TFT_BLACK);
  label(120, "Enter");
  d.fillTriangle(166, cy, 158, cy - 7, 158, cy + 7, muted);
  d.fillTriangle(174, cy, 166, cy - 7, 166, cy + 7, muted);
  d.fillRect(175, cy - 7, 2, 15, muted);
  label(167, "D");
  // Volume.
  d.fillRect(195, cy - 2, 3, 5, muted);
  d.fillTriangle(197, cy, 202, cy - 5, 202, cy + 5, muted);
  d.setFont(&fonts::Font2);
  d.setTextColor(muted);
  d.setCursor(207, cy - 8);
  d.print(String((int(gSpeakerVolume) * 100 + 127) / 255));
  label(215, "- +");
  d.setFont(&fonts::Font2);
}
void renderRadioPlayer() {
  auto& d = gCanvas;
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  const uint16_t green = rgb565_local(88,240,141), amber = rgb565_local(255,136,64);
  const auto state = internet_radio::state();
  const bool searching = internet_radio::searching();
  const bool playing = state == internet_radio::State::Playing && !searching;
  const bool waiting = !playing && !searching && (gRadioStartPending || gRadioRetryWaiting ||
                       (radioStreaming() && state != internet_radio::State::Stopping));
  radioMeterTick(playing);
  d.fillScreen(TFT_BLACK);
  // Above: the genre, and the clock with the country filter while it's on.
  String right = "--:--";
  const time_t now = time(nullptr);
  if (now > 100000) {
    struct tm ti; localtime_r(&now,&ti); char buf[6];
    clock_display::format(buf,sizeof(buf),ti.tm_hour,ti.tm_min,gDeviceSettings.clockTwelveHour);
    right = buf;
  }
  if (gRadioSelection.country[0]) right = String(gRadioSelection.country) + "  " + right;
  d.setFont(&fonts::Font2);
  const int16_t rightX = kBigRight - d.textWidth(right);
  d.setTextColor(muted); d.setCursor(rightX, 2); d.print(right);
  const auto* station = gRadioSelection.station();
  const String genre = station ? gRadioSelection.genreName() : "Internet Radio";
  const bool wideGenre = radioTextFont(genre);
  d.setTextColor(station ? radioGenreInk(genre.c_str()) : cyan);
  d.setCursor(kBigLeft, wideGenre ? 4 : 2);
  d.print(radioFit(genre, rightX - kBigLeft - 8));
  // The station: its tile, its name, the song, and the small facts.
  const String name = station ? station->name : "No stations";
  const uint8_t* logo = station && gRadioLogo && gRadioLogoFor == radioHash(station->url) ? gRadioLogo : nullptr;
  drawRadioTile(kBigLeft, 22, name.c_str(), station ? genre.c_str() : "", playing, waiting, logo);
  constexpr int16_t textX = 76, textW = 229 - 76;
  useBigTextFont(name);
  d.setTextColor(white); d.setCursor(textX, 22);
  d.print(radioFit(name, textW));
  bool song = false;
  const String status = radioStatusText(&song);
  const bool wide = radioTextFont(status);
  String lines[2];
  const int n = radioWrap(status, textW, lines, 2);
  d.setTextColor(song ? white : radioStreaming() || gRadioRetryWaiting ? green : amber);
  for (int i = 0; i < n; ++i) { d.setCursor(textX, (wide ? 40 : 38) + i * 16); d.print(lines[i]); }
  if (station) {
    String facts = String(gRadioSelection.index + 1) + " of " + String(gRadioSelection.count(gRadioSelection.genre));
    if (playing && internet_radio::bitrate()) facts += "   " + String(internet_radio::bitrate()) + " kbps";
    if (station->country[0]) facts += String("   ") + station->country;
    d.setFont(&fonts::Font0);
    d.setTextColor(muted); d.setCursor(textX, 75); d.print(facts);
  }
  drawRadioControls(gRadioWantPlaying || radioStreaming(), playing ? green : cyan, muted);
  // The rest of the keys, quieter than the buttons' own.
  d.setFont(&fonts::Font0);
  d.setTextColor(rgb565_local(110,150,180));
  const char* keys = "F find  V visuals  L country  M move";
  d.setCursor(120 - d.textWidth(keys) / 2, 126);
  d.print(keys);
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
  else if (line.isEmpty() && (gRadioWantPlaying || radioStreaming())) { line = "Searching stops the radio"; ink = muted; }
  else if (line.isEmpty()) { line = gRadioQuery.isEmpty() ? "Blank finds the most listened" : "Searches radio-browser.info"; ink = muted; }
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
  const bool notice = !gRadioNotice.isEmpty() || radioStreaming();
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
    const bool good = radioStreaming() || gRadioNoticeGood;
    drawBigNotice(radioFit(text, kBigRight - kBigLeft), good ? green : amber);
  }
}
// The header's small font has ASCII only, so a name in another script stays out of it.
String radioAsciiOr(const String& text, const char* otherwise) {
  for (size_t i = 0; i < text.length(); ++i) if (static_cast<uint8_t>(text[i]) >= 0x80) return otherwise;
  return text;
}
// The genres to move to, in their colours, the current one marked, then New genre and Remove.
void renderRadioMove() {
  const uint16_t cyan = rgb565_local(47,227,255), muted = rgb565_local(182,219,255), amber = rgb565_local(255,136,64);
  const int rows = radioMoveRows();
  // No counter: the scrollbar shows where it is, and the keys need the room.
  drawBigHeader("Move", cyan, "", radioAsciiOr(gRadioRemoveName, "This station"), "Enter choose  Esc back");
  const int first = bigListFirstRow(gRadioMoveSel, rows);
  for (int row = 0; row < kBigVisibleRows && first + row < rows; ++row) {
    const int i = first + row;
    switch (radioMoveRowAt(i)) {
      case RadioMoveRow::Genre: {
        const char* genre = gRadioList->genre(size_t(i));
        const bool now = radio_catalog::sameText(genre, gRadioRemoveGenre.c_str());
        drawBigRow(row, genre, now ? "now" : "", i == gRadioMoveSel, cyan, radioGenreInk(genre), muted);
        break;
      }
      case RadioMoveRow::NewGenre: drawBigRow(row, "+ New genre", "", i == gRadioMoveSel, cyan, cyan, muted); break;
      case RadioMoveRow::Place: drawBigRow(row, "Change its place", "", i == gRadioMoveSel, cyan, cyan, muted); break;
      case RadioMoveRow::Remove: drawBigRow(row, "Remove from the list", "", i == gRadioMoveSel, amber, amber, muted); break;
    }
  }
  drawBigScrollbar(first, rows, cyan);
}
// The genre's stations in order, numbered, the moving one highlighted where it is now.
void renderRadioPlace() {
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  drawBigHeader("Place", cyan, "", "Up/Down moves it", "Enter keeps  Esc undoes");
  const int first = bigListFirstRow(gRadioPlaceAt, gRadioPlaceCount);
  for (int row = 0; row < kBigVisibleRows && first + row < gRadioPlaceCount; ++row) {
    const int k = first + row;
    drawBigRow(row, gRadioList->at(gRadioPlaceOrder[k]).name, String(k + 1), k == gRadioPlaceAt, cyan, white, muted);
  }
  drawBigScrollbar(first, gRadioPlaceCount, cyan);
}
void renderRadioNewGenre() {
  auto& d = gCanvas;
  const uint16_t cyan = rgb565_local(47,227,255), white = TFT_WHITE, muted = rgb565_local(182,219,255);
  const uint16_t amber = rgb565_local(255,136,64);
  drawBigHeader("New genre", cyan, "", radioAsciiOr(gRadioRemoveName, ""));
  d.setFont(&fonts::Font2);
  d.fillRoundRect(8, 34, 222, 22, 5, TFT_BLACK);
  d.drawRoundRect(8, 34, 222, 22, 5, cyan);
  String shown = gRadioNewGenre + "_";
  while (shown.length() > 1 && d.textWidth(shown) > 210) shown.remove(0, 1);
  d.setTextColor(white); d.setCursor(14, 37); d.print(shown);
  if (gRadioNewGenre.isEmpty()) {
    const int16_t x = 14 + d.textWidth(shown) + 4;
    d.setTextColor(muted); d.setCursor(x, 37); d.print(radioFit("like News or Podcasts", 222 - x));
  }
  d.setTextColor(gRadioNotice.isEmpty() ? muted : amber);
  d.setCursor(8, 66);
  d.print(radioFit(gRadioNotice.isEmpty() ? String("A name you have joins it") : gRadioNotice, 222));
  d.setFont(&fonts::Font0);
  d.setTextColor(muted);
  d.setCursor(8, 120); d.print("Enter move  Esc back");
  d.setFont(&fonts::Font2);
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
// Station art in the visuals: the logo, framed in its genre's colour, square with rounded
// corners or round as a record's label. With no logo, its tile at that size, initials and all.
void drawRadioArt(int16_t x, int16_t y, int16_t side, bool round, const char* name, const char* genre, const uint8_t* art) {
  auto& d = gCanvas;
  const uint16_t ink = radioGenreInk(genre);
  const float r = side / 2.0f;
  if (art) {
    d.pushImage(x, y, side, side, reinterpret_cast<const lgfx::rgb332_t*>(art));
    // Black outside the shape, then a frame: the corners' curve, or the whole circle.
    const float corner = round ? r : side / 8.0f;
    for (int i = 0; i < int(corner + 0.5f); ++i) {
      const float dy = corner - i - 0.5f;
      const int16_t cut = int16_t(corner - sqrtf(max(0.0f, corner * corner - dy * dy)) + 0.5f);
      if (cut <= 0) continue;
      d.fillRect(x, y + i, cut, 1, TFT_BLACK); d.fillRect(x + side - cut, y + i, cut, 1, TFT_BLACK);
      d.fillRect(x, y + side - 1 - i, cut, 1, TFT_BLACK); d.fillRect(x + side - cut, y + side - 1 - i, cut, 1, TFT_BLACK);
    }
    if (round) d.drawCircle(x + side / 2, y + side / 2, side / 2, ink);
    else d.drawRoundRect(x, y, side, side, side / 8, ink);
    return;
  }
  if (round) d.fillCircle(x + side / 2, y + side / 2, side / 2, ink);
  else d.fillRoundRect(x, y, side, side, side / 8, ink);
  const String initials = radioInitials(name);
  d.setTextColor(TFT_BLACK);
  if (initials.isEmpty()) {  // A note, as on the tile.
    const int16_t u = side / 16;
    d.fillCircle(x + side * 5 / 12, y + side * 2 / 3, max<int16_t>(3, u * 2), TFT_BLACK);
    d.fillRect(x + side * 5 / 12 + u * 2 - 1, y + side / 4, max<int16_t>(2, u), side * 5 / 12, TFT_BLACK);
    return;
  }
  // Font4 at whatever size fills about half the art.
  const float size = side >= 80 ? 2.0f : side >= 64 ? 1.5f : 1.0f;
  d.setFont(&fonts::Font4);
  d.setTextSize(size);
  d.setCursor(x + (side - d.textWidth(initials)) / 2, y + int16_t((side - 26 * size) / 2));
  d.print(initials);
  d.setTextSize(1);
  d.setFont(&fonts::Font2);
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
  // With the logo on, each look gives it a place of its own: framed beside Bars and Wave, in
  // the middle of Mirror's spectrum, and as the label of Rings' record.
  const auto* station = gRadioSelection.station();
  const bool logo = gRadioVisualLogo && station;
  const uint8_t* art = logo && gRadioBigLogo && gRadioBigLogoFor == radioHash(station->url) && gRadioBigLogoSide == radioArtSide(gRadioLook) ? gRadioBigLogo : nullptr;
  const char* genre = station ? gRadioSelection.genreName() : "";
  const bool beside = logo && (gRadioLook == 0 || gRadioLook == 2);
  const int16_t x0 = beside ? 100 : 8, w = beside ? 132 : 224;
  if (beside) drawRadioArt(10, 26, 80, false, station->name, genre, art);
  const int16_t bottom = 114, mid = 66, height = 94;
  switch (gRadioLook) {
    case 0: {  // A spectrum, low notes on the left, with caps on the bars' recent highs.
      const int16_t gap = w >= 200 ? 2 : 1, bar = (w - 15 * gap) / 16, left = x0 + (w - (16 * bar + 15 * gap)) / 2;
      for (int i = 0; i < int(internet_radio::kBands); ++i) {
        const int16_t x = left + i * (bar + gap);
        const uint16_t ink = rgb565_local(47 + (170 - 47) * i / 15, 227 + (120 - 227) * i / 15, 255);
        const int16_t h = int16_t(gRadioShown[i] * height / 255), p = int16_t(gRadioPeaks[i] * height / 255);
        if (h > 0) d.fillRect(x, bottom - h, bar, h, ink);
        d.fillRect(x, bottom - p - 2, bar, 2, p > 0 ? white : dim);
      }
      break;
    }
    case 1: {  // The spectrum mirrored about the middle line.
      const auto ink = [&](int j, int n) { return rgb565_local(88 - 41 * j / (n - 1), 240 - 13 * j / (n - 1), 141 + 114 * j / (n - 1)); };
      if (logo) {
        // The logo in the middle, the low notes beside it and the high ones outward either side.
        d.drawFastHLine(8, mid, 76, dim);
        d.drawFastHLine(156, mid, 76, dim);
        for (int i = 0; i < int(internet_radio::kBands); ++i) {
          const int16_t half = int16_t(gRadioShown[i] * 46 / 255);
          if (half <= 0) continue;
          d.fillRect(162 + i * 4, mid - half, 3, half * 2 + 1, ink(i, 16));
          d.fillRect(75 - i * 4, mid - half, 3, half * 2 + 1, ink(i, 16));
        }
        drawRadioArt(88, 34, 64, false, station->name, genre, art);
        break;
      }
      d.drawFastHLine(8, mid, 224, dim);
      for (int j = 0; j < 32; ++j) {
        const int a = gRadioShown[j / 2], b = gRadioShown[min(15, j / 2 + 1)];
        const int16_t half = int16_t(((j & 1) ? (a + b) / 2 : a) * 46 / 255);
        if (half > 0) d.fillRect(9 + j * 7, mid - half, 5, half * 2 + 1, ink(j, 32));
      }
      break;
    }
    case 2: {  // The sound itself, about 11 ms of it, drawn twice for a line that reads.
      d.drawFastHLine(x0, mid, w, dim);
      // Scaled to its loudest point so quiet music still fills the screen, but not past
      // 46/40 times, so hiss doesn't fill it too.
      int peak = 40;
      if (live) for (int8_t s : v.wave) peak = max(peak, s < 0 ? -int(s) : int(s));
      if (live) {
        for (int i = 0; i + 1 < int(internet_radio::kWavePoints); ++i) {
          const int16_t xa = x0 + 1 + i * (w - 2) / 119, xb = x0 + 1 + (i + 1) * (w - 2) / 119;
          const int16_t ya = mid - v.wave[i] * 46 / peak, yb = mid - v.wave[i + 1] * 46 / peak;
          d.drawLine(xa, ya, xb, yb, green);
          d.drawLine(xa, ya + 1, xb, yb + 1, green);
        }
      }
      break;
    }
    default: {  // Bass, middle and treble as rings in the Pulse colours.
      int bass = 0, mids = 0, highs = 0;
      for (int i = 0; i < 4; ++i) bass += gRadioShown[i];
      for (int i = 4; i < 10; ++i) mids += gRadioShown[i];
      for (int i = 10; i < 16; ++i) highs += gRadioShown[i];
      bass /= 4; mids /= 6; highs /= 6;
      // Round the logo as its label, they spin out from it; without one, from a dot.
      const int16_t rb = logo ? int16_t(36 + bass * 10 / 255) : int16_t(8 + bass * 38 / 255);
      const int16_t rm = logo ? int16_t(31 + mids * 8 / 255) : int16_t(6 + mids * 30 / 255);
      const int16_t rh = logo ? int16_t(27 + highs * 6 / 255) : int16_t(4 + highs * 22 / 255);
      d.drawCircle(120, mid, 46, dim);
      d.drawCircle(120, mid, rb, cyan); d.drawCircle(120, mid, rb - 1, cyan);
      d.drawCircle(120, mid, rm, violet); d.drawCircle(120, mid, rm - 1, violet);
      d.drawCircle(120, mid, rh, amber); d.drawCircle(120, mid, rh - 1, amber);
      if (logo) drawRadioArt(96, 42, 48, true, station->name, genre, art);
      else d.fillCircle(120, mid, 2, live ? white : dim);
      break;
    }
  }
  // The station above, the clock beside it, or what just changed for a moment.
  const String name = station ? station->name : "Internet Radio";
  String hhmm = "--:--";
  const time_t clock = time(nullptr);
  if (clock > 100000) {
    struct tm ti; localtime_r(&clock,&ti); char buf[6];
    clock_display::format(buf,sizeof(buf),ti.tm_hour,ti.tm_min,gDeviceSettings.clockTwelveHour);
    hhmm = buf;
  }
  const bool naming = gRadioLookShownMs && now - gRadioLookShownMs < 1500 && *gRadioNamed;
  if (naming) hhmm = gRadioNamed;
  d.setFont(&fonts::Font2);
  const int16_t clockX = kBigRight - d.textWidth(hhmm);
  d.setTextColor(naming ? cyan : muted); d.setCursor(clockX, 1); d.print(hhmm);
  const bool wideName = radioTextFont(name);
  d.setTextColor(white); d.setCursor(kBigLeft, wideName ? 3 : 1);
  d.print(radioFit(name, clockX - kBigLeft - 8));
  // The song below, scrolling if it's long, or what the radio is doing.
  bool song = false;
  const String status = radioStatusText(&song);
  const bool wide = radioTextFont(status);
  d.setTextColor(song ? white : radioStreaming() || gRadioRetryWaiting ? green : amber);
  d.setCursor(kBigLeft, wide ? 120 : 118);
  d.print(song ? radioMarquee(status, 222) : radioFit(status, 222));
  // The first time only: what the look is called and the keys, for a few seconds.
  // Over the look, beside the logo when it's beside it.
  if (int32_t(gRadioCardUntilMs - now) > 0) {
    const int16_t cx = x0 + w / 2;
    d.fillRoundRect(cx - 58, 40, 116, 52, 6, TFT_BLACK);
    d.drawRoundRect(cx - 58, 40, 116, 52, 6, cyan);
    d.setFont(&fonts::Font2);
    d.setTextColor(cyan);
    const char* look = kRadioLooks[gRadioLook];
    d.setCursor(cx - d.textWidth(look) / 2, 44); d.print(look);
    d.setFont(&fonts::Font0);
    d.setTextColor(muted);
    for (const auto& line : {std::make_pair("V next  I logo", 66), std::make_pair("Esc back", 78)}) {
      d.setCursor(cx - d.textWidth(line.first) / 2, line.second); d.print(line.first);
    }
    d.setFont(&fonts::Font2);
  }
}
// Home, while the radio plays behind it: the station in its genre's colour, over a meter of what
// it's playing, in place of Home's idle bars. Ctrl+M's hint shows first; while it tunes or
// reconnects, that; and if it stops by itself, why, for a few seconds.
bool radioHomeCardShows() {
  return radioOn() || (gRadioBehindNoteUntilMs && int32_t(gRadioBehindNoteUntilMs - millis()) > 0);
}
void drawHomeRadioCard() {
  auto& d = gCanvas;
  constexpr int16_t kLeft = 121, kRight = 229, kBase = 101;
  const uint16_t amber = rgb565_local(255,136,64), quiet = rgb565_local(150,165,175);
  const bool on = radioOn();
  const uint16_t ink = !on ? amber : gRadioResumeInk ? gRadioResumeInk : rgb565_local(88,240,141);
  // A note: its head, and its stem with the flag.
  d.fillCircle(kLeft + 3, 84, 3, ink);
  d.fillRect(kLeft + 5, 74, 2, 10, ink);
  d.fillRect(kLeft + 5, 74, 6, 2, ink);
  const String name = on ? gRadioWantName : String("Radio stopped");
  const bool wide = radioTextFont(name);
  d.setTextColor(on ? TFT_WHITE : amber);
  d.setCursor(kLeft + 15, wide ? 74 : 72);
  d.print(radioFit(name, kRight - kLeft - 15));
  const bool playing = on && internet_radio::state() == internet_radio::State::Playing && !internet_radio::searching();
  radioMeterTick(playing);
  String line;
  if (!on) {
    // Under "Radio stopped" the reason needn't say radio again: "Timed out", "Connection failed".
    line = gRadioBehindNote;
    if (line.startsWith("Radio ") && line.length() > 6) { line = line.substring(6); line[0] = char(toupper(static_cast<unsigned char>(line[0]))); }
  }
  else if (gRadioBehindSinceMs && millis() - gRadioBehindSinceMs < 4000) line = "Ctrl+M stops it";
  // The player's words for a wait, shorter, to fit the line.
  else if (gRadioRetryWaiting) line = !gWifiReady ? String("Waiting for Wi-Fi") : "Reconnecting " + String(radioRetrySecondsLeft()) + "s";
  else if (!playing) line = radioStatusText();
  if (!line.isEmpty() || !playing) {
    d.setFont(&fonts::Font0);
    d.setTextColor(quiet);
    d.setCursor(kLeft, 93);
    d.print(radioFit(line, kRight - kLeft));
    d.setFont(&fonts::Font2);
    return;
  }
  for (int i = 0; i < kRadioMeterLen; ++i) {
    const int16_t h = int16_t(max<int>(2, gRadioMeter[i] * 13 / 14));
    d.fillRoundRect(kLeft - 1 + i * 7, kBase - h, 3, h, 1, ink);
  }
}
void renderInternetRadioUi() {
  switch (gRadioView) {
    case RadioView::Visual: renderRadioVisual(); break;
    case RadioView::Find: renderRadioFind(); break;
    case RadioView::Results: renderRadioResults(); break;
    case RadioView::Remove: renderRadioRemove(); break;
    case RadioView::Move: renderRadioMove(); break;
    case RadioView::NewGenre: renderRadioNewGenre(); break;
    case RadioView::Place: renderRadioPlace(); break;
    default: renderRadioPlayer(); break;
  }
  renderLauncherOverlay(); renderDebugOverlay();
}
} // namespace
