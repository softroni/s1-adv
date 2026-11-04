// Podcasts, audiobooks and old-time radio. The bridge searches the free iTunes Search API and
// the Internet Archive, reads the feeds, and passes each episode on as one small MP3 format
// that the radio's decoder plays (podcast_player.h). What you follow, where you stopped and
// what you saved for offline listening are text files on the SD card (podcast_catalog.h).
// The player has one worker, so loading a list or saving an episode pauses what's playing.
namespace {
enum class PodView : uint8_t { Library, Find, Shows, Episodes, Player, Remove };
enum class PodTab : uint8_t { Listening, Following, Saved };
enum class PodAsk : uint8_t { None, Search, Episodes };
enum class PodRow : uint8_t { Follow, Previous, Episode, Next };
constexpr const char* kPodFilesDir = "/podcasts/files";
constexpr const char* kPodShowsPath = "/podcasts/shows.txt";
constexpr const char* kPodProgressPath = "/podcasts/progress.txt";
constexpr const char* kPodSavedPath = "/podcasts/saved.txt";
constexpr uint32_t kPodRememberEveryMs = 30000, kPodSeekWaitMs = 700;
constexpr int32_t kPodBack = 15, kPodAhead = 30;
constexpr size_t kPodQueryChars = 40;
constexpr const char* kPodTabNames[] = {"Listening", "Shows", "Saved"};

PodView gPodView = PodView::Library, gPodPlayerBack = PodView::Library, gPodEpisodesBack = PodView::Library;
PodView gPodRemoveBack = PodView::Library;
PodTab gPodTab = PodTab::Listening;
podcast::Kind gPodKind = podcast::Kind::Podcast;
String gPodQuery, gPodNotice;
bool gPodNoticeGood = false;
bool gPodCard = false;  // The card answers, so what you do can be kept.
int gPodSel = 0;
// Each list is on the heap only while a screen can show it.
podcast::Show* gPodFound = nullptr;
uint8_t gPodFoundCount = 0;
int gPodFoundSel = 0;
podcast::Show* gPodFollowed = nullptr;
uint8_t gPodFollowedCount = 0;
podcast::Entry* gPodEntries = nullptr;
uint8_t gPodEntryCount = 0;
podcast::Page* gPodPage = nullptr;
podcast::Show gPodShow;  // The show whose episodes are open.
uint32_t gPodPageAt[podcast::kPage] = {};  // Where you stopped in each, from progress.txt.
uint32_t gPodPageSaved = 0;  // A bit for each one on the card.
bool gPodShowFollowed = false;
// A list asked of the bridge, waiting for the worker or on it.
PodAsk gPodAsking = PodAsk::None;
bool gPodAskPending = false, gPodAskRunning = false;
String gPodAskPath;
// What's playing, or paused. .at is where it starts next.
podcast::Entry gPodNow;
bool gPodHasNow = false, gPodNowFile = false, gPodNowSaved = false;
bool gPodWantPlaying = false, gPodStartPending = false, gPodPlayStarted = false;
uint32_t gPodStartAtMs = 0, gPodRememberedMs = 0;
// An episode being saved to the card.
podcast::Entry gPodSaving;
bool gPodSavePending = false, gPodSaveRunning = false;
podcast::Entry gPodRemove;
bool gPodDeletePending = false;  // Waiting for the player to let go of the file first.
// The home-network ticket for one episode, asked of the bridge over HTTPS before it plays or
// saves, so the audio itself needs no TLS. Kept for the session: it's good for any second.
podcast::HomeTicket gPodTicket;
char gPodTicketShow[podcast::kIdBytes + 1] = "", gPodTicketEpisode[podcast::kEpisodeIdBytes + 1] = "";
bool gPodTicketRunning = false;
uint32_t gPodHomeOffFor = 0;  // The device address on which the home way didn't answer: HTTPS there.
bool gPodRetried = false, gPodSaveRetried = false;  // One more try after the home way fails.
bool gPodPlayHome = false, gPodSaveHome = false;  // How the job under way is getting its audio.
bool gPodResume = false;  // Playing when a list was asked for, so it carries on after.
// Playing on behind Home and the light screens, as the radio does: since when, for Home's hint,
// and why it stopped by itself, which Home says for a while.
uint32_t gPodBehindSinceMs = 0, gPodBehindNoteUntilMs = 0;
String gPodBehindNote;
bool gPodLastAudio = false;  // A podcast played more recently than the radio, so Ctrl+M means it.

void podInvalidate() { gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
// An episode wants to play: it is, it's starting, or it's waiting to go on from a new place.
bool podcastOn() { return gPodWantPlaying; }
// Where it plays on: its own screen, and the light screens the radio plays behind.
bool podcastMayPlayIn(UiMode mode) { return mode == UiMode::Podcasts || (mode != UiMode::Radio && radioMayPlayIn(mode)); }
void podSay(const String& text, bool good = false) { gPodNotice = text; gPodNoticeGood = good; podInvalidate(); }
bool podcastTakesText() { return gUiMode == UiMode::Podcasts && gPodView == PodView::Find; }
bool podcastTakesArrows() { return gUiMode == UiMode::Podcasts; }
bool sameEpisode(const podcast::Entry& a, const char* show, const char* episode) {
  return !strcmp(a.show, show) && !strcmp(a.episode, episode);
}

// --- The card -----------------------------------------------------------------------------
// Calls fn(line) for each line of a file, with its own copy of the line to cut up.
template <class Fn>
void podEachLine(const char* path, Fn fn) {
  if (!gPodCard) return;
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  char line[512];
  uint8_t chunk[256];
  size_t n = 0;
  bool tooLong = false;
  for (;;) {
    const int got = f.read(chunk, sizeof(chunk));
    if (got <= 0) break;
    for (int i = 0; i < got; ++i) {
      const char c = char(chunk[i]);
      if (c == '\r') continue;
      if (c == '\n') {
        if (!tooLong && n) { line[n] = 0; fn(line); }
        n = 0; tooLong = false;
      } else if (n + 1 >= sizeof(line)) tooLong = true;
      else line[n++] = c;
    }
  }
  if (!tooLong && n) { line[n] = 0; fn(line); }
  f.close();
}
// Writes `first`, then the lines keep() wants, at most `limit` in all. The old file stays
// until the new one is whole, so a power cut loses at most this change.
template <class Keep>
bool podRewrite(const char* path, const char* first, size_t limit, Keep keep) {
  if (!gPodCard || !ensureDirRecursive(SD, kPodFilesDir)) return false;
  const String fresh = String(path) + ".new";
  SD.remove(fresh.c_str());
  File out = SD.open(fresh.c_str(), FILE_WRITE);
  if (!out) return false;
  bool ok = true;
  size_t lines = 0;
  if (first && *first) {
    const size_t n = strlen(first);
    ok = out.write(reinterpret_cast<const uint8_t*>(first), n) == n;
    ++lines;
  }
  podEachLine(path, [&](char* line) {
    if (!ok || lines >= limit) return;
    char copy[512];
    strcpy(copy, line);
    if (!keep(copy)) return;
    const size_t n = strlen(line);
    ok = out.write(reinterpret_cast<const uint8_t*>(line), n) == n && out.write('\n') == 1;
    ++lines;
  });
  out.close();
  if (!ok) { SD.remove(fresh.c_str()); return false; }
  SD.remove(path);
  return SD.rename(fresh.c_str(), path);
}
// A change cut off between its last two steps leaves only the new file.
void podRecoverFile(const char* path) {
  const String fresh = String(path) + ".new";
  if (!SD.exists(path) && SD.exists(fresh.c_str())) SD.rename(fresh.c_str(), path);
}
// Where you stopped is kept for the last 20 episodes, the latest first.
void podRemember(const podcast::Entry& e) {
  if (!gPodCard || !e.show[0]) return;
  podcast::Entry record = e;
  const time_t now = time(nullptr);
  record.stamp = now > 1600000000 ? uint32_t(now) : 0;
  char line[400];
  if (!podcast::formatProgress(line, sizeof(line), record)) return;
  podRewrite(kPodProgressPath, line, podcast::kMaxProgress, [&](char* l) {
    podcast::Entry old;
    return podcast::parseProgressLine(l, old) && !sameEpisode(old, e.show, e.episode);
  });
}
void podForgetProgress(const char* show, const char* episode) {
  podRewrite(kPodProgressPath, nullptr, podcast::kMaxProgress, [&](char* l) {
    podcast::Entry old;
    return podcast::parseProgressLine(l, old) && !sameEpisode(old, show, episode);
  });
}
// Where you stopped in an episode, or 0.
uint32_t podStoppedAt(const char* show, const char* episode) {
  uint32_t at = 0;
  podEachLine(kPodProgressPath, [&](char* line) {
    podcast::Entry e;
    if (podcast::parseProgressLine(line, e) && sameEpisode(e, show, episode)) at = e.at;
  });
  return at;
}
bool podFollowing(const char* id) {
  bool found = false;
  podEachLine(kPodShowsPath, [&](char* line) {
    podcast::Show s;
    if (podcast::parseShowLine(line, s) && !strcmp(s.id, id)) found = true;
  });
  return found;
}
bool podFollow(const podcast::Show& show) {
  char line[300];
  if (!podcast::formatShow(line, sizeof(line), show)) return false;
  return podRewrite(kPodShowsPath, line, podcast::kMaxFollowed, [&](char* l) {
    podcast::Show s;
    return podcast::parseShowLine(l, s) && strcmp(s.id, show.id);
  });
}
bool podUnfollow(const char* id) {
  return podRewrite(kPodShowsPath, nullptr, podcast::kMaxFollowed, [&](char* l) {
    podcast::Show s;
    return podcast::parseShowLine(l, s) && strcmp(s.id, id);
  });
}
String podFileName(const char* episode, bool partial = false) {
  char path[64];
  return podcast::filePath(path, sizeof(path), episode, partial) ? String(path) : String();
}
bool podOnCard(const char* episode) {
  if (!gPodCard) return false;
  bool listed = false;
  podEachLine(kPodSavedPath, [&](char* line) {
    podcast::Entry e;
    if (podcast::parseSavedLine(line, e) && !strcmp(e.episode, episode)) listed = true;
  });
  const String path = podFileName(episode);
  return listed && !path.isEmpty() && SD.exists(path.c_str());
}
bool podAddSaved(const podcast::Entry& e) {
  char line[400];
  if (!podcast::formatSaved(line, sizeof(line), e)) return false;
  return podRewrite(kPodSavedPath, line, podcast::kMaxSaved, [&](char* l) {
    podcast::Entry old;
    return podcast::parseSavedLine(l, old) && strcmp(old.episode, e.episode);
  });
}
bool podDeleteSaved(const char* episode) {
  const String path = podFileName(episode);
  if (!path.isEmpty()) SD.remove(path.c_str());
  return podRewrite(kPodSavedPath, nullptr, podcast::kMaxSaved, [&](char* l) {
    podcast::Entry old;
    return podcast::parseSavedLine(l, old) && strcmp(old.episode, episode);
  });
}
// Free space for an episode of this length, with room to spare. A length nobody knows
// needs 100 MB, a little over four hours.
bool podRoomFor(uint32_t seconds) {
  const uint64_t total = SD.totalBytes(), used = SD.usedBytes();
  const uint64_t need = uint64_t(seconds ? seconds : 15000) * podcast::kBytesPerSecond + 4u * 1024u * 1024u;
  return total > used && total - used >= need;
}

// --- Lists --------------------------------------------------------------------------------
void podFreeTab() {
  delete[] gPodEntries; gPodEntries = nullptr; gPodEntryCount = 0;
  delete[] gPodFollowed; gPodFollowed = nullptr; gPodFollowedCount = 0;
}
void podFreeFound() { delete[] gPodFound; gPodFound = nullptr; gPodFoundCount = 0; }
// Before the player starts: every list it can do without, the emoji cache, and the chat's long
// history, which the firmware trims the same way whenever memory runs low. The search's shows
// go too unless they're on screen; going back then opens Find, with the words still there.
void podFreeForPlay() {
  podFreeTab();
  if (gPodView != PodView::Shows || gUiMode != UiMode::Podcasts) podFreeFound();
  compactMemoryForUi("podcast", true);
}
void podFreePage() { delete gPodPage; gPodPage = nullptr; }
int podTabCount() { return gPodTab == PodTab::Following ? gPodFollowedCount : gPodEntryCount; }
void podLoadTab() {
  podFreeTab();
  if (!gPodCard) return;
  if (gPodTab == PodTab::Following) {
    gPodFollowed = new (std::nothrow) podcast::Show[podcast::kMaxFollowed];
    if (!gPodFollowed) { podSay("Not enough memory"); return; }
    podEachLine(kPodShowsPath, [&](char* line) {
      if (gPodFollowedCount < podcast::kMaxFollowed && podcast::parseShowLine(line, gPodFollowed[gPodFollowedCount])) ++gPodFollowedCount;
    });
  } else {
    const size_t room = gPodTab == PodTab::Saved ? podcast::kMaxSaved : podcast::kMaxProgress;
    gPodEntries = new (std::nothrow) podcast::Entry[room];
    if (!gPodEntries) { podSay("Not enough memory"); return; }
    if (gPodTab == PodTab::Listening) {
      // What's started and not finished, the latest first.
      podEachLine(kPodProgressPath, [&](char* line) {
        podcast::Entry& e = gPodEntries[gPodEntryCount];
        if (gPodEntryCount < room && podcast::parseProgressLine(line, e) && e.at > 0 && !podcast::finished(e.at, e.seconds)) ++gPodEntryCount;
      });
    } else {
      podEachLine(kPodSavedPath, [&](char* line) {
        if (gPodEntryCount < room && podcast::parseSavedLine(line, gPodEntries[gPodEntryCount])) ++gPodEntryCount;
      });
      podEachLine(kPodProgressPath, [&](char* line) {
        podcast::Entry e;
        if (!podcast::parseProgressLine(line, e)) return;
        for (uint8_t i = 0; i < gPodEntryCount; ++i)
          if (sameEpisode(gPodEntries[i], e.show, e.episode)) gPodEntries[i].at = e.at;
      });
    }
  }
  gPodSel = max(0, min(gPodSel, podTabCount() - 1));
}
// Where you are in each episode on the page, and which are on the card.
void podMarkPage() {
  memset(gPodPageAt, 0, sizeof(gPodPageAt));
  gPodPageSaved = 0;
  if (!gPodPage) return;
  gPodShowFollowed = podFollowing(gPodShow.id);
  podEachLine(kPodProgressPath, [&](char* line) {
    podcast::Entry e;
    if (!podcast::parseProgressLine(line, e) || strcmp(e.show, gPodShow.id)) return;
    for (uint8_t i = 0; i < gPodPage->count; ++i) if (!strcmp(gPodPage->episodes[i].id, e.episode)) gPodPageAt[i] = e.at;
  });
  podEachLine(kPodSavedPath, [&](char* line) {
    podcast::Entry e;
    if (!podcast::parseSavedLine(line, e)) return;
    for (uint8_t i = 0; i < gPodPage->count; ++i) if (!strcmp(gPodPage->episodes[i].id, e.episode)) gPodPageSaved |= 1u << i;
  });
}
int podEpisodeRows() {
  if (!gPodPage) return 1;
  return 1 + (gPodPage->from ? 1 : 0) + gPodPage->count + (gPodPage->from + gPodPage->count < gPodPage->total ? 1 : 0);
}
PodRow podEpisodeRow(int row, int& index) {
  index = -1;
  if (row <= 0 || !gPodPage) return PodRow::Follow;
  --row;
  if (gPodPage->from) { if (!row) return PodRow::Previous; --row; }
  if (row < gPodPage->count) { index = row; return PodRow::Episode; }
  return PodRow::Next;
}
podcast::Entry podEntryFor(int index) {
  podcast::Entry e;
  if (!gPodPage || index < 0 || index >= gPodPage->count) return e;
  const auto& ep = gPodPage->episodes[index];
  podcast::copyText(e.show, sizeof(e.show), gPodShow.id);
  podcast::copyText(e.episode, sizeof(e.episode), ep.id);
  podcast::copyText(e.showTitle, sizeof(e.showTitle), gPodShow.title);
  podcast::copyText(e.title, sizeof(e.title), ep.title);
  e.seconds = ep.seconds;
  e.at = gPodPageAt[index];
  return e;
}
// The episode now playing, if it's on the open page.
int podNowIndex() {
  if (!gPodHasNow || !gPodPage || strcmp(gPodShow.id, gPodNow.show)) return -1;
  for (uint8_t i = 0; i < gPodPage->count; ++i) if (!strcmp(gPodPage->episodes[i].id, gPodNow.episode)) return i;
  return -1;
}

// --- The worker ---------------------------------------------------------------------------
void podUseBridge() { podcast_player::setBridge(gHubHost.c_str(), gHubPort, gDeviceToken.c_str(), kTlsRootCaPem); }
bool podStartAllowed(bool network, bool sound) {
  return (!network || (gWifiReady && !gHubHost.isEmpty())) && !gBootBackgroundActive && gCanvasSpriteActive &&
    !gRecording && !gSubmitting && !gThinking && !gPlaybackActive && !gTransferUiActive && !gOtaApplyInProgress &&
    !gInboxHttpBusy && !gVoiceDialBusy && !gVoiceReplySpeechBusy && gQueuedVoiceReplySpeech.isEmpty() &&
    !gRealtimeActive && !gRealtimeDialBusy && !gRealtimeExclusiveMode && !gPendingRealtimeStart &&
    !gMeshBusOwned && !gTextTurnTaskHandle && !gCompletedTextTurn && gPendingTextTurns.empty() &&
    gActiveTextTurn.isEmpty() && !gG0VoicePending && !gVoiceCapturePreparing && !internet_radio::busy() &&
    (!sound || !M5Cardputer.Speaker.isPlaying());
}
String podError(const char* what) {
  if (podcast_player::error() == podcast_player::Error::NoMemory) {
    // What ran short and the memory then, so a report from the screen says what to fix.
    const char* note = podcast_player::memoryNote();
    logf("[PODCAST] no memory %s free=%u largest=%u", note, unsigned(ESP.getFreeHeap()),
         unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    return note && *note ? String("No memory: ") + note : String("Not enough memory");
  }
  const char* text = podcast_player::errorText();
  if (podcast_player::error() == podcast_player::Error::NotFound && what) return String(what) + ": not found";
  return text ? String(text) : String("Stopped");
}
// Where the episode is now: from the worker while it plays, else where it will start.
uint32_t podNowAt() {
  return podcast_player::playing() && gPodPlayStarted && !gPodStartPending ? podcast_player::position() : gPodNow.at;
}
void podRememberNow() {
  if (!gPodHasNow) return;
  podRemember(gPodNow);
  gPodRememberedMs = millis();
}
// Pauses and keeps the place, so the next play carries on from it.
void podPause(const char* why = "Paused") {
  if (!gPodWantPlaying && !podcast_player::playing()) return;
  if (gPodPlayStarted && !gPodStartPending) gPodNow.at = podcast_player::position();
  gPodWantPlaying = false;
  gPodStartPending = false;
  podcast_player::stop();
  podRememberNow();
  if (why) podSay(why, true);
}
void stopPodcasts() {
  podPause(nullptr);
  gPodAskPending = gPodSavePending = false;
  podcast_player::stop();
  podInvalidate();
}
// Before any HTTPS: what this screen holds that the request can have.
void podPrepareNetwork() {
  podUseBridge();
  prepareNetworkMemory("podcast", true);
  clearEmojiCache();
}
bool podTicketFor(const podcast::Entry& e) {
  return !gPodTicketRunning && !strcmp(gPodTicketShow, e.show) && !strcmp(gPodTicketEpisode, e.episode);
}
// A ticket is worth asking for when this episode has none and the home way hasn't already
// failed on this network.
bool podTicketWanted(const podcast::Entry& e) {
  const uint32_t mine = wifiLocalAddress();
  return mine && gPodHomeOffFor != mine && !podTicketFor(e);
}
uint32_t podHomeIp(const podcast::Entry& e) {
  const uint32_t mine = wifiLocalAddress();
  return podTicketFor(e) && gPodHomeOffFor != mine ? podcast::homeFor(gPodTicket, mine) : 0;
}
void podForgetTicket() { gPodTicket = podcast::HomeTicket{}; gPodTicketShow[0] = gPodTicketEpisode[0] = 0; }
bool podStartTicket(const podcast::Entry& e) {
  char path[200];
  podForgetTicket();
  if (!podcast::ticketPath(path, sizeof(path), e.show, e.episode)) return false;
  podcast::copyText(gPodTicketShow, sizeof(gPodTicketShow), e.show);
  podcast::copyText(gPodTicketEpisode, sizeof(gPodTicketEpisode), e.episode);
  podFreeForPlay();
  podPrepareNetwork();
  gPodTicketRunning = podcast_player::startList(path);
  if (!gPodTicketRunning) podForgetTicket();
  return gPodTicketRunning;
}
void podAsk(PodAsk what, const String& path, const char* saying) {
  if (gPodAskPending || gPodAskRunning) return;
  if (gPodSaveRunning || gPodSavePending) { podSay("Saving: X stops it first"); return; }
  gPodResume = gPodWantPlaying || podcast_player::playing();
  if (gPodResume) podPause(nullptr);
  gPodAsking = what;
  gPodAskPath = path;
  gPodAskPending = true;
  podFreeTab();  // Its memory goes to the request; the library is read again on the way back.
  podSay(saying, true);
}
void podSearch() {
  char path[256];
  String words = gPodQuery;
  words.trim();
  if (words.isEmpty()) { podSay("Type a few words first"); return; }
  if (!gWifiReady) { podSay("Wi-Fi required"); return; }
  if (!podcast::searchPath(path, sizeof(path), gPodKind, words.c_str())) { podSay("Too long: 64 letters at most"); return; }
  podAsk(PodAsk::Search, path, "Searching...");
}
void podOpenShow(const podcast::Show& show, PodView back, uint32_t from = 0) {
  char path[200];
  if (!podcast::episodesPath(path, sizeof(path), show.id, from)) { podSay("Can't open that show"); return; }
  if (!gWifiReady) { podSay("Wi-Fi required"); return; }
  if (strcmp(gPodShow.id, show.id)) podFreePage();  // Never another show's page under this one's name.
  gPodShow = show;
  gPodEpisodesBack = back;
  podAsk(PodAsk::Episodes, path, from ? "Loading more..." : "Loading episodes...");
}
// Plays from where you stopped, or from the start if it's done. From the card when it's there.
void podPlay(podcast::Entry e, PodView back, bool show = true) {
  if (gPodSaveRunning || gPodSavePending) { podSay("Saving: X stops it first"); return; }
  const bool same = gPodHasNow && sameEpisode(gPodNow, e.show, e.episode);
  if (same && (gPodWantPlaying || podcast_player::playing())) { if (show) gPodView = PodView::Player; podInvalidate(); return; }
  const bool onCard = podOnCard(e.episode);
  // Checked before anything stops, so what's playing carries on if this one can't.
  if (!onCard && !gWifiReady) { podSay("Wi-Fi required: S saves it for later"); return; }
  if (gPodHasNow && !same && (gPodWantPlaying || podcast_player::playing())) podPause(nullptr);
  if (same) e.at = gPodNow.at;  // Fresher than any list's copy.
  if (podcast::finished(e.at, e.seconds)) e.at = 0;
  gPodNow = e;
  gPodHasNow = true;
  gPodNowFile = gPodNowSaved = onCard;
  gPodWantPlaying = true;
  gPodStartPending = true;
  gPodPlayStarted = false;
  gPodRetried = false;
  gPodStartAtMs = millis();
  if (podcast_player::busy()) podcast_player::stop();  // Starts once the worker has let go.
  if (!show) return;
  if (back != PodView::Player) gPodPlayerBack = back;
  gPodView = PodView::Player;
  podFreeTab();  // The library is read again from the card on the way back.
  podSay("");
}
void podToggle() {
  if (!gPodHasNow) { podSay("Choose an episode first"); return; }
  if (gPodWantPlaying || podcast_player::playing()) podPause();
  else podPlay(gPodNow, PodView::Player);
}
// Moves by `delta` seconds. While playing it starts again from there once the keys rest.
void podSeek(int32_t delta) {
  if (!gPodHasNow) return;
  const int64_t at = int64_t(podNowAt()) + delta;
  const int64_t last = gPodNow.seconds > 5 ? int64_t(gPodNow.seconds) - 5 : INT32_MAX;
  gPodNow.at = uint32_t(at < 0 ? 0 : at > last ? last : at);
  if (gPodWantPlaying) {
    if (podcast_player::busy() && !gPodTicketRunning) podcast_player::stop();
    gPodStartPending = true;
    gPodPlayStarted = false;
    gPodRetried = false;
    gPodStartAtMs = millis() + kPodSeekWaitMs;
  } else podRememberNow();
  podInvalidate();
}
// The next or previous episode on the open page.
void podStep(int direction) {
  const int i = podNowIndex();
  if (i < 0) { podSay("E opens this show's episodes"); return; }
  const int next = i + direction;
  if (next < 0 || next >= gPodPage->count) { podSay(direction > 0 ? "No more on this page" : "This is the first"); return; }
  if (gPodWantPlaying || podcast_player::playing()) podPause(nullptr);
  podMarkPage();
  podPlay(podEntryFor(next), PodView::Player);
}
void podSave(podcast::Entry e) {
  if (!gPodCard) { podSay(gSdReady ? "SD card isn't answering" : "No SD card"); return; }
  if (gPodSaveRunning || gPodSavePending) { podSay("Already saving one"); return; }
  if (podOnCard(e.episode)) { podSay("Already on the card", true); return; }
  size_t saved = 0;
  podEachLine(kPodSavedPath, [&](char* line) { podcast::Entry old; if (podcast::parseSavedLine(line, old)) ++saved; });
  if (saved >= podcast::kMaxSaved) { podSay("25 saved already: delete one first"); return; }
  if (!gWifiReady) { podSay("Wi-Fi required"); return; }
  if (!podRoomFor(e.seconds)) { podSay("Not enough room on the card"); return; }
  if (gPodWantPlaying || podcast_player::playing()) podPause(nullptr);
  e.at = 0;
  gPodSaving = e;
  gPodSavePending = true;
  gPodSaveRetried = false;
  podSay("Saving...", true);
}

// --- Opening and leaving ------------------------------------------------------------------
void beginPodcasts() {
  gPodCard = radioCardAnswers();
  if (gPodCard) {
    ensureDirRecursive(SD, kPodFilesDir);
    for (const char* path : {kPodShowsPath, kPodProgressPath, kPodSavedPath}) podRecoverFile(path);
  }
  gPodView = PodView::Library;
  gPodSel = 0;
  gPodBehindSinceMs = gPodBehindNoteUntilMs = 0;
  if (podcastOn() && gPodHasNow) {
    // Back from a screen it played behind: what's playing, with the library behind it.
    gPodView = PodView::Player;
    gPodPlayerBack = PodView::Library;
    return;
  }
  podLoadTab();
  if (!gPodCard) podSay(gSdReady ? "SD card isn't answering: nothing is kept" : "No SD card: nothing is kept");
  else if (!podTabCount() && gPodTab == PodTab::Listening) podSay("F finds shows and books", true);
  else podSay("");
}
void podRelease() {
  podFreeTab();
  podFreeFound();
  podFreePage();
  podcast_player::clearList();
  gPodView = PodView::Library;
}
bool podcastEscape() {
  if (gPodView != PodView::Library && (gPodAskPending || gPodAskRunning)) {
    // Going back stops a list on its way, so it can't pull this screen back when it comes.
    gPodAskPending = false;
    gPodAsking = PodAsk::None;
    if (podcast_player::listing()) podcast_player::stop();
  }
  switch (gPodView) {
    case PodView::Find:
      podFreeFound();
      gPodView = PodView::Library;
      podLoadTab();
      podSay("");
      break;
    case PodView::Shows: gPodView = PodView::Find; podSay(""); break;
    case PodView::Episodes:
      gPodView = gPodEpisodesBack;
      if (gPodView == PodView::Shows && !gPodFound) gPodView = PodView::Find;  // Let go for the player.
      if (gPodView == PodView::Library) podLoadTab();
      podSay("");
      break;
    case PodView::Player:
      gPodView = gPodPlayerBack == PodView::Player ? PodView::Library : gPodPlayerBack;
      if (gPodView == PodView::Library) podLoadTab();
      if (gPodView == PodView::Episodes) podMarkPage();
      if (!gPodSaveRunning) podSay("");
      break;
    case PodView::Remove:
      gPodView = gPodRemoveBack;
      podSay(String("Kept ") + gPodRemove.title, true);
      break;
    default:
      return false;
  }
  podInvalidate();
  return true;
}

// --- Each loop ----------------------------------------------------------------------------
void podListDone() {
  const PodAsk what = gPodAsking;
  gPodAsking = PodAsk::None;
  size_t length = 0;
  const char* text = podcast_player::listText(length);
  if (what == PodAsk::None) { podcast_player::clearList(); return; }  // Called off on the way.
  if (podcast_player::error() != podcast_player::Error::None || !text) {
    podSay(what == PodAsk::Search ? "Search: " + podError(nullptr) : podError("Show"));
    podcast_player::clearList();
    if (gPodView == PodView::Library) podLoadTab();
    return;
  }
  if (what == PodAsk::Search) {
    if (!gPodFound) gPodFound = new (std::nothrow) podcast::Show[podcast::kMaxShows];
    if (!gPodFound) { podcast_player::clearList(); podSay("Not enough memory"); return; }
    gPodFoundCount = uint8_t(podcast::parseShows(text, length, gPodFound, podcast::kMaxShows));
    podcast_player::clearList();
    if (!gPodFoundCount) { String words = gPodQuery; words.trim(); podSay("Nothing found for " + words); return; }
    gPodFoundSel = 0;
    if (gPodView == PodView::Find) gPodView = PodView::Shows;
    podSay("");
    return;
  }
  if (!gPodPage) gPodPage = new (std::nothrow) podcast::Page;
  if (!gPodPage) { podcast_player::clearList(); podSay("Not enough memory"); return; }
  const bool ok = podcast::parsePage(text, length, *gPodPage);
  podcast_player::clearList();
  if (!ok) { podFreePage(); podSay("Odd answer from the bridge"); return; }
  if (gPodPage->title[0]) podcast::copyText(gPodShow.title, sizeof(gPodShow.title), gPodPage->title);
  if (gPodPage->by[0]) podcast::copyText(gPodShow.by, sizeof(gPodShow.by), gPodPage->by);
  gPodShow.count = gPodPage->total;
  podMarkPage();
  if (gPodView != PodView::Player) {
    gPodView = PodView::Episodes;
    podFreeTab();
  }
  // On the first episode, or where you were in the list.
  gPodSel = gPodPage->count ? 1 + (gPodPage->from ? 1 : 0) : 0;
  const int now = podNowIndex();
  if (now >= 0) gPodSel = 1 + (gPodPage->from ? 1 : 0) + now;
  podSay(gPodPage->count ? "" : "No episodes to play");
}
void podFinishDelete() {
  gPodDeletePending = false;
  const String title = gPodRemove.title;
  const bool gone = podDeleteSaved(gPodRemove.episode);
  if (gPodView == PodView::Library) podLoadTab();
  if (gPodView == PodView::Episodes) podMarkPage();
  podSay(gone ? "Deleted " + title : String("Couldn't change the SD card"), gone);
}
// The home way failed before anything came: no route there (another Wi-Fi), or a ticket the
// bridge has forgotten since it restarted. One more try, over HTTPS or with a new ticket.
bool podHomeRetry(bool& retried, bool viaHome, bool nothingCame) {
  using podcast_player::Error;
  const Error e = podcast_player::error();
  if (!viaHome || !nothingCame || (e != Error::Connect && e != Error::NotFound && e != Error::Timeout)) return false;
  if (e == Error::NotFound) {
    if (retried) return false;  // A new ticket once: a bridge that keeps forgetting would loop.
    retried = true;
    podForgetTicket();
  } else gPodHomeOffFor = wifiLocalAddress();  // HTTPS from now on here, so this can't loop.
  logf("[PODCAST] home way failed (%s), trying again", podcast_player::errorText());
  return true;
}
void podSaveDone() {
  gPodSaveRunning = false;
  if (!podcast_player::saveDone() && podHomeRetry(gPodSaveRetried, gPodSaveHome, !podcast_player::savedBytes())) {
    gPodSavePending = true;
    return;
  }
  if (podcast_player::saveDone()) {
    gPodSaving.bytes = podcast_player::savedBytes();
    if (!gPodSaving.seconds) gPodSaving.seconds = gPodSaving.bytes / podcast::kBytesPerSecond;
    if (podAddSaved(gPodSaving)) podSay("Saved: it plays with no Wi-Fi", true);
    else podSay("Saved, but saved.txt couldn't change");
    if (gPodHasNow && sameEpisode(gPodNow, gPodSaving.show, gPodSaving.episode)) gPodNowSaved = true;
  } else {
    podSay(podcast_player::error() == podcast_player::Error::None ? String("Saving stopped") : "Save: " + podError(nullptr));
  }
  if (gPodView == PodView::Episodes) podMarkPage();
  if (gPodView == PodView::Library) podLoadTab();
}
// The episode played to its end. A book's next chapter, or the next show of an old series,
// follows on; a podcast stops.
void podFinished() {
  gPodNow.at = gPodNow.seconds ? gPodNow.seconds : podcast_player::position();
  if (!gPodNow.seconds) gPodNow.seconds = gPodNow.at;
  podRememberNow();
  const int i = podNowIndex();
  if (gPodNow.show[0] == 'a' && i >= 0 && i + 1 < gPodPage->count) {
    podMarkPage();
    podPlay(podEntryFor(i + 1), PodView::Player, gUiMode == UiMode::Podcasts && gPodView == PodView::Player);
    podSay("Next: " + String(gPodNow.title), true);
    return;
  }
  podSay("Finished", true);
}
// Off the podcast screen. True while the episode plays on behind this one: what only the screen
// uses goes back to the heap, lists and saves on their way stop, and playing carries on. False
// when it should all stop.
bool podcastBehind() {
  if (gRadioModePending || !podcastOn() || internet_radio::busy() || !podcastMayPlayIn(gUiMode)) return false;
  if (!gPodBehindSinceMs) {
    gPodBehindSinceMs = max<uint32_t>(millis(), 1);
    gPodBehindNoteUntilMs = 0;
    gPodAskPending = gPodSavePending = false;
    gPodAsking = PodAsk::None;
    if (podcast_player::listing() && !gPodTicketRunning) podcast_player::stop();
    podFreeTab();
    podFreeFound();
  }
  return true;
}
void servicePodcastWork(bool here);
// It stopped by itself behind a screen: Home says why for a while, and the status bar now.
void podStoppedBehind() {
  gPodBehindNote = gPodNotice.isEmpty() ? String("Stopped") : gPodNotice;
  gPodBehindNoteUntilMs = max<uint32_t>(millis() + 8000, 1);
  setStatus("Podcast: " + gPodBehindNote, 3000);
}
void servicePodcasts() {
  // The radio streaming is the most recent sound now, so Ctrl+M is for it.
  if (internet_radio::busy() && !internet_radio::searching() && !internet_radio::fetchingLogo()) gPodLastAudio = false;
  const bool here = gUiMode == UiMode::Podcasts;
  const bool wasOn = podcastOn();
  if (!here && !podcastBehind()) {
    gPodBehindSinceMs = 0;
    if (gPodDeletePending && !podcast_player::busy()) podFinishDelete();
    if (podcast_player::busy() || gPodWantPlaying || gPodStartPending || gPodAskPending || gPodSavePending) stopPodcasts();
    else if (gPodEntries || gPodFollowed || gPodFound || gPodPage) podRelease();
    gPodAskRunning = gPodSaveRunning = false;
    if (gPodTicketRunning) { gPodTicketRunning = false; podForgetTicket(); }
    gPodAsking = PodAsk::None;
    return;
  }
  if (here) gPodBehindSinceMs = 0;
  servicePodcastWork(here);
  if (!here && wasOn && !podcastOn()) { podStoppedBehind(); }
}
void servicePodcastWork(bool here) {
  const bool idle = !podcast_player::busy() && !internet_radio::busy();
  if (gPodDeletePending && !podcast_player::busy()) { podFinishDelete(); podInvalidate(); }
  if (gPodAskRunning && !podcast_player::busy()) {
    gPodAskRunning = false;
    podListDone();
    if (gPodResume && gPodHasNow && !gPodAskPending) podPlay(gPodNow, PodView::Player, false);
    gPodResume = false;
    podInvalidate();
  }
  if (gPodSaveRunning && !podcast_player::busy()) { podSaveDone(); podInvalidate(); }
  if (gPodTicketRunning && !podcast_player::busy()) {
    gPodTicketRunning = false;
    using podcast_player::Error;
    const Error e = podcast_player::error();
    size_t length = 0;
    const char* text = podcast_player::listText(length);
    podcast::HomeTicket ticket;
    if (e == Error::None && text && podcast::parseTicket(text, length, ticket)) gPodTicket = ticket;
    else if (e == Error::None && !text) podForgetTicket();  // Stopped on the way: asked again next time.
    else if (e == Error::None || e == Error::NotFound || e == Error::Http || e == Error::Malformed) gPodTicket = podcast::HomeTicket{};  // None: over HTTPS.
    else {
      // Couldn't ask at all, so the audio couldn't come either.
      const String why = podError(nullptr);
      podForgetTicket();
      if (gPodStartPending) { gPodStartPending = gPodWantPlaying = false; podSay(why); }
      if (gPodSavePending) { gPodSavePending = false; podSay("Save: " + why); }
    }
    podcast_player::clearList();
    podInvalidate();
  }
  if (here && gPodAskPending && idle) {
    gPodAskPending = false;
    if (!podStartAllowed(true, false)) { gPodAsking = PodAsk::None; podSay(!gWifiReady ? "Wi-Fi required" : "Busy: try again"); }
    else {
      podPrepareNetwork();
      if (podcast_player::startList(gPodAskPath.c_str())) gPodAskRunning = true;
      else { gPodAsking = PodAsk::None; podSay(podError(nullptr)); }
    }
    podInvalidate();
    return;
  }
  if (here && gPodSavePending && idle) {
    if (!podStartAllowed(true, false)) { gPodSavePending = false; podSay(!gWifiReady ? "Wi-Fi required" : "Busy: try again"); podInvalidate(); return; }
    if (podTicketWanted(gPodSaving)) {
      if (!podStartTicket(gPodSaving)) { gPodSavePending = false; podSay("Save: " + podError(nullptr)); }
      podInvalidate();
      return;
    }
    gPodSavePending = false;
    char path[300];
    const String part = podFileName(gPodSaving.episode, true), file = podFileName(gPodSaving.episode);
    const uint32_t ip = podHomeIp(gPodSaving), expect = gPodSaving.seconds * podcast::kBytesPerSecond;
    if (!ensureDirRecursive(SD, kPodFilesDir) || part.isEmpty() ||
        !(ip ? podcast::homeAudioPath(path, sizeof(path), gPodTicket, 0) : podcast::audioPath(path, sizeof(path), gPodSaving.show, gPodSaving.episode, 0))) podSay("Can't save that one");
    else {
      podPrepareNetwork();
      gPodSaveHome = ip != 0;
      if (ip ? podcast_player::startSaveHome(ip, gPodTicket.port, path, part.c_str(), file.c_str(), expect)
             : podcast_player::startSave(path, part.c_str(), file.c_str(), expect)) gPodSaveRunning = true;
      else podSay("Save: " + podError(nullptr));
    }
    podInvalidate();
    return;
  }
  if (gPodStartPending && idle && int32_t(millis() - gPodStartAtMs) >= 0) {
    if (gPodNowFile && !podOnCard(gPodNow.episode)) gPodNowFile = gPodNowSaved = false;  // Deleted meanwhile.
    if (!gPodNowFile && gWifiReady && podTicketWanted(gPodNow) && podStartAllowed(true, false)) {
      if (!podStartTicket(gPodNow)) { gPodStartPending = gPodWantPlaying = false; podSay(podError(nullptr)); }
      podInvalidate();
      return;  // Starts once the ticket is in.
    }
    gPodStartPending = false;
    if (!podStartAllowed(!gPodNowFile, true)) {
      gPodWantPlaying = false;
      podSay(!gPodNowFile && !gWifiReady ? "Wi-Fi required" : "Busy: retry Enter");
    } else {
      podFreeForPlay();
      initSpeaker();
      bool started = false;
      if (gPodNowFile) started = podcast_player::startFile(podFileName(gPodNow.episode).c_str(), gPodNow.at);
      else {
        char path[300];
        const uint32_t ip = podHomeIp(gPodNow);
        gPodPlayHome = ip != 0;
        if (ip) started = podcast::homeAudioPath(path, sizeof(path), gPodTicket, gPodNow.at) &&
                          podcast_player::startStreamHome(ip, gPodTicket.port, path, gPodNow.at);
        else {
          podPrepareNetwork();
          started = podcast::audioPath(path, sizeof(path), gPodNow.show, gPodNow.episode, gPodNow.at) &&
                    podcast_player::startStream(path, gPodNow.at);
        }
      }
      if (!started) { gPodWantPlaying = false; podSay(podError(nullptr)); }
      else {
        gPodPlayStarted = true;
        gPodLastAudio = true;
        gPodRememberedMs = millis();
        podRememberNow();  // It's in Listening from the moment it starts.
      }
    }
    podInvalidate();
  } else if (gPodWantPlaying && !gPodStartPending && !podcast_player::busy()) {
    // It stopped by itself: the end, or a failure.
    gPodWantPlaying = false;
    if (podcast_player::reachedEnd()) podFinished();
    else if (podHomeRetry(gPodRetried, gPodPlayHome && !gPodNowFile, podcast_player::position() == gPodNow.at)) {
      gPodWantPlaying = gPodStartPending = true;
      gPodPlayStarted = false;
      gPodStartAtMs = millis();
    } else {
      if (gPodPlayStarted) gPodNow.at = podcast_player::position();
      podRememberNow();
      podSay(podError("Episode"));
    }
    podInvalidate();
  }
  if (gPodWantPlaying && podcast_player::playing() && millis() - gPodRememberedMs >= kPodRememberEveryMs) {
    podcast::Entry e = gPodNow;
    e.at = podcast_player::position();
    podRemember(e);
    gPodRememberedMs = millis();
  }
}

// --- Keys ---------------------------------------------------------------------------------
// The Cardputer's arrows are the ; . , / keys, with or without Fn, and char(1) to char(4)
// are a USB keyboard's. Here they move everywhere except in Find, where only , and / do:
// they change what to search, and ; and . can be typed.
char podArrow(char key) {
  if (key == char(1) || key == ';') return 'U';
  if (key == char(2) || key == '.') return 'D';
  if (key == char(3) || key == ',') return 'L';
  if (key == char(4) || key == '/') return 'R';
  return 0;
}
bool podEnter(char key, const Keyboard_Class::KeysState& keys) { return keys.enter || key == '\r' || key == '\n' || key == ' '; }
void podMove(int& sel, int count, int delta) { sel = count ? (sel + delta + count) % count : 0; }
bool podVolumeKey(char key) {
  if (key == '-' || key == '_') { radioStepVolume(-1); podInvalidate(); return true; }
  if (key == '+' || key == '=') { radioStepVolume(1); podInvalidate(); return true; }
  return false;
}
bool podFindKey(char key, const Keyboard_Class::KeysState& keys) {
  if (key == char(3) || key == ',') gPodKind = podcast::Kind((uint8_t(gPodKind) + podcast::kKinds - 1) % podcast::kKinds);
  else if (key == char(4) || key == '/') gPodKind = podcast::Kind((uint8_t(gPodKind) + 1) % podcast::kKinds);
  else if (key == char(1) || key == char(2)) {}
  else if (key == '\b') { if (!gPodQuery.isEmpty()) radioDropLastLetter(gPodQuery); }
  else if (podEnter(key, keys) && key != ' ') { if (!gPodAskPending && !gPodAskRunning) podSearch(); }
  else if (key >= ' ' && key < 127 && gPodQuery.length() < kPodQueryChars) gPodQuery += key;
  if (key != '\r' && key != '\n' && !keys.enter && !gPodAskPending && !gPodAskRunning) gPodNotice = "";
  podInvalidate();
  return true;
}
void podAskRemove(const podcast::Entry& e, PodView back) {
  gPodRemove = e;
  gPodRemoveBack = back;
  gPodView = PodView::Remove;
  podInvalidate();
}
void podLibraryKey(char key, const Keyboard_Class::KeysState& keys) {
  const char arrow = podArrow(key), lower = char(tolower(static_cast<unsigned char>(key)));
  const int count = podTabCount();
  if (arrow == 'L' || arrow == 'R') {
    gPodTab = PodTab((uint8_t(gPodTab) + (arrow == 'L' ? 2 : 1)) % 3);
    gPodSel = 0;
    gPodNotice = "";
    podLoadTab();
    if (!podTabCount()) podSay(gPodTab == PodTab::Following ? "S on a show follows it" : gPodTab == PodTab::Saved ?
                                "S on an episode saves it" : "F finds shows and books", true);
  } else if (arrow == 'U' || arrow == 'D') { podMove(gPodSel, count, arrow == 'U' ? -1 : 1); gPodNotice = ""; }
  else if (lower == 'f') { gPodView = PodView::Find; podFreeTab(); podSay(""); }
  else if (lower == 'n' && gPodHasNow) { gPodView = PodView::Player; podFreeTab(); podSay(""); }
  else if (lower == 'x' && gPodSaveRunning) podcast_player::stop();
  else if (podVolumeKey(key)) return;
  else if (!count) {}
  else if (gPodTab == PodTab::Following) {
    const podcast::Show& show = gPodFollowed[gPodSel];
    if (podEnter(key, keys)) podOpenShow(show, PodView::Library);
    else if (key == '\b') {
      const String title = show.title;
      if (podUnfollow(show.id)) { podLoadTab(); podSay("Unfollowed " + title, true); }
      else podSay("Couldn't change the SD card");
    }
  } else {
    const podcast::Entry e = gPodEntries[gPodSel];
    if (podEnter(key, keys)) podPlay(e, PodView::Library);
    else if (lower == 'e') {
      podcast::Show show;
      podcast::copyText(show.id, sizeof(show.id), e.show);
      podcast::copyText(show.title, sizeof(show.title), e.showTitle);
      podOpenShow(show, PodView::Library);
    } else if (key == '\b' && gPodTab == PodTab::Saved) podAskRemove(e, PodView::Library);
    else if (key == '\b') { podForgetProgress(e.show, e.episode); podLoadTab(); podSay("Taken off Listening", true); }
  }
  podInvalidate();
}
void podShowsKey(char key, const Keyboard_Class::KeysState& keys) {
  const char arrow = podArrow(key), lower = char(tolower(static_cast<unsigned char>(key)));
  if (arrow == 'U' || arrow == 'L') { podMove(gPodFoundSel, gPodFoundCount, -1); gPodNotice = ""; }
  else if (arrow == 'D' || arrow == 'R') { podMove(gPodFoundSel, gPodFoundCount, 1); gPodNotice = ""; }
  else if (lower == 'f') { gPodView = PodView::Find; podSay(""); }
  else if (podVolumeKey(key)) return;
  else if (gPodFoundCount && podEnter(key, keys)) podOpenShow(gPodFound[gPodFoundSel], PodView::Shows);
  else if (gPodFoundCount && lower == 's') {
    const podcast::Show& show = gPodFound[gPodFoundSel];
    if (!gPodCard) podSay("No SD card: can't follow");
    else if (podFollowing(show.id)) podSay("Already following", true);
    else if (podFollow(show)) podSay("Following: it's in Shows", true);
    else podSay("Couldn't change the SD card");
  }
  podInvalidate();
}
void podEpisodesKey(char key, const Keyboard_Class::KeysState& keys) {
  const char arrow = podArrow(key), lower = char(tolower(static_cast<unsigned char>(key)));
  const int rows = podEpisodeRows();
  int index = -1;
  const PodRow row = podEpisodeRow(gPodSel, index);
  if (arrow == 'U' || arrow == 'D') { podMove(gPodSel, rows, arrow == 'U' ? -1 : 1); gPodNotice = ""; }
  else if (arrow == 'L' && gPodPage && gPodPage->from) podOpenShow(gPodShow, gPodEpisodesBack, gPodPage->from >= podcast::kPage ? gPodPage->from - podcast::kPage : 0);
  else if (arrow == 'R' && gPodPage && gPodPage->from + gPodPage->count < gPodPage->total) podOpenShow(gPodShow, gPodEpisodesBack, gPodPage->from + gPodPage->count);
  else if (lower == 'n' && gPodHasNow) { gPodView = PodView::Player; podSay(""); }
  else if (lower == 'x' && gPodSaveRunning) podcast_player::stop();
  else if (podVolumeKey(key)) return;
  else if (podEnter(key, keys)) {
    if (row == PodRow::Follow) {
      if (!gPodCard) podSay("No SD card: can't follow");
      else if (gPodShowFollowed ? podUnfollow(gPodShow.id) : podFollow(gPodShow)) {
        gPodShowFollowed = !gPodShowFollowed;
        podSay(gPodShowFollowed ? "Following: it's in Shows" : "Unfollowed", true);
      } else podSay("Couldn't change the SD card");
    } else if (row == PodRow::Previous) podOpenShow(gPodShow, gPodEpisodesBack, gPodPage->from >= podcast::kPage ? gPodPage->from - podcast::kPage : 0);
    else if (row == PodRow::Next) podOpenShow(gPodShow, gPodEpisodesBack, gPodPage->from + gPodPage->count);
    else podPlay(podEntryFor(index), PodView::Episodes);
  } else if (row == PodRow::Episode && lower == 's') podSave(podEntryFor(index));
  else if (row == PodRow::Episode && key == '\b') {
    if (gPodPageSaved & (1u << index)) podAskRemove(podEntryFor(index), PodView::Episodes);
    else podSay("Del removes one saved on the card");
  }
  podInvalidate();
}
void podPlayerKey(char key, const Keyboard_Class::KeysState& keys) {
  const char arrow = podArrow(key), lower = char(tolower(static_cast<unsigned char>(key)));
  if (arrow == 'L' || lower == 'a') podSeek(-kPodBack);
  else if (arrow == 'R' || lower == 'd') podSeek(kPodAhead);
  else if (arrow == 'U' || lower == 'w') podStep(-1);
  else if (arrow == 'D') podStep(1);
  else if (podEnter(key, keys)) podToggle();
  else if (lower == 's' && gPodHasNow) podSave(gPodNow);
  else if (lower == 'x' && gPodSaveRunning) podcast_player::stop();
  else if (lower == 'e' && gPodHasNow) {
    if (gPodPage && !strcmp(gPodShow.id, gPodNow.show)) { gPodView = PodView::Episodes; podMarkPage(); podSay(""); }
    else {
      podcast::Show show;
      podcast::copyText(show.id, sizeof(show.id), gPodNow.show);
      podcast::copyText(show.title, sizeof(show.title), gPodNow.showTitle);
      podOpenShow(show, PodView::Player);
      if (gPodAskPending) gPodView = PodView::Episodes;
    }
  } else podVolumeKey(key);
  podInvalidate();
}
bool handlePodcastKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  switch (gPodView) {
    case PodView::Find: return podFindKey(key, keys);
    case PodView::Shows: podShowsKey(key, keys); break;
    case PodView::Episodes: podEpisodesKey(key, keys); break;
    case PodView::Player: podPlayerKey(key, keys); break;
    case PodView::Remove:
      if (key == 'y' || key == 'Y') {
        gPodView = gPodRemoveBack;
        if (gPodHasNow && sameEpisode(gPodNow, gPodRemove.show, gPodRemove.episode)) {
          if (gPodNowFile && (gPodWantPlaying || podcast_player::playing())) podPause(nullptr);
          gPodNowFile = gPodNowSaved = false;
        }
        // The file goes only once nothing is reading it.
        if (podcast_player::busy()) { gPodDeletePending = true; podSay("Deleting...", true); }
        else podFinishDelete();
      } else podcastEscape();
      break;
    default: podLibraryKey(key, keys); break;
  }
  return true;
}

// --- Drawing ------------------------------------------------------------------------------
uint16_t podAccent() { return rgb565_local(190, 130, 255); }
String podClockText() {
  const time_t now = time(nullptr);
  if (now <= 100000) return "--:--";
  struct tm ti;
  localtime_r(&now, &ti);
  char buf[6];
  clock_display::format(buf, sizeof(buf), ti.tm_hour, ti.tm_min, gDeviceSettings.clockTwelveHour);
  return buf;
}
String podClock(uint32_t seconds) { char b[16]; podcast::clock(b, sizeof(b), seconds); return b; }
String podLength(uint32_t seconds) { char b[16]; podcast::length(b, sizeof(b), seconds); return b; }
String podLeft(uint32_t at, uint32_t seconds) {
  if (!at) return podLength(seconds);
  char b[24];
  podcast::left(b, sizeof(b), at, seconds);
  return b;
}
// What the worker is doing, for the last row of a list or the player's status line.
String podActivity(bool& good) {
  good = true;
  if (gPodSaveRunning || gPodSavePending) {
    const uint32_t want = gPodSaving.seconds * podcast::kBytesPerSecond, got = podcast_player::savedBytes();
    if (!podcast_player::saving() || podcast_player::state() == podcast_player::State::Connecting) return "Saving: connecting  X stops";
    if (!want) return "Saving " + String(got / (1024 * 1024)) + " MB  X stops";
    return "Saving " + String(min<uint32_t>(99, uint32_t(uint64_t(got) * 100 / want))) + "%  X stops";
  }
  if (gPodAskPending || gPodAskRunning) return gPodAsking == PodAsk::Search ? "Searching..." : "Loading...";
  good = gPodNoticeGood;
  return gPodNotice;
}
// The list's notice over its last row, so the selection has to stay in the four above it.
template <class Row>
void podDrawList(int count, int& sel, Row row, const String& empty, const char* hint = nullptr) {
  bool good = true;
  const String notice = podActivity(good);
  const bool noticed = !notice.isEmpty() || !count;
  const int visible = noticed ? kBigVisibleRows - 1 : kBigVisibleRows;
  sel = max(0, min(sel, count - 1));
  const int first = max<int>(0, min<int>(sel - visible / 2, count - visible));
  for (int r = 0; r < min(visible, count - first); ++r) row(first + r, r);
  drawBigScrollbar(first, count, podAccent());
  gCanvas.setFont(&fonts::Font2);  // The notice's own font, so it's measured as it will show.
  if (noticed) drawBigNotice(radioFit(notice.isEmpty() ? empty : notice, kBigRight - kBigLeft), notice.isEmpty() || good ? rgb565_local(88, 240, 141) : rgb565_local(255, 136, 64));
  // A short list with nothing to say has room for its keys in the last row.
  if (hint && !noticed && count < kBigVisibleRows) {
    auto& d = gCanvas;
    d.setFont(&fonts::Font0);
    d.setTextColor(rgb565_local(110, 150, 180));
    d.setCursor(120 - d.textWidth(hint) / 2, 125);
    d.print(hint);
    d.setFont(&fonts::Font2);
  }
}
void renderPodLibrary() {
  auto& d = gCanvas;
  const uint16_t accent = podAccent(), white = TFT_WHITE, muted = rgb565_local(182, 219, 255), dim = rgb565_local(60, 60, 110);
  d.fillScreen(TFT_BLACK);
  // The three tabs, with the clock in its corner as on the radio.
  const String clock = podClockText();
  d.setFont(&fonts::Font2);
  const int16_t clockX = kBigRight - d.textWidth(clock);
  d.setTextColor(muted);
  d.setCursor(clockX, 3);
  d.print(clock);
  int16_t x = 2;
  for (int i = 0; i < 3; ++i) {
    const bool on = int(gPodTab) == i;
    const int16_t w = d.textWidth(kPodTabNames[i]) + 12;
    if (on) d.fillRoundRect(x, 1, w, 19, 5, accent);
    else d.drawRoundRect(x, 1, w, 19, 5, dim);
    d.setTextColor(on ? TFT_BLACK : muted);
    d.setCursor(x + 6, 3);
    d.print(kPodTabNames[i]);
    x += w + 3;
  }
  d.drawFastHLine(0, 22, 240, accent);
  const int count = podTabCount();
  const char* empty = gPodTab == PodTab::Following ? "S on a show follows it" : gPodTab == PodTab::Saved ? "S on an episode saves it" : "F finds shows and books";
  podDrawList(count, gPodSel, [&](int i, int r) {
    if (gPodTab == PodTab::Following) {
      const auto& s = gPodFollowed[i];
      drawBigRow(r, s.title, s.count ? String(s.count) : String(""), i == gPodSel, accent, white, muted);
      return;
    }
    const auto& e = gPodEntries[i];
    const bool now = gPodHasNow && sameEpisode(gPodNow, e.show, e.episode) && (gPodWantPlaying || podcast_player::playing());
    const uint32_t at = now ? podNowAt() : e.at;
    drawBigRow(r, e.title, now ? String("playing") : podLeft(at, e.seconds), i == gPodSel, accent, white,
               now ? rgb565_local(88, 240, 141) : muted);
  }, gPodCard ? empty : "No SD card: nothing is kept",
     gPodTab == PodTab::Following ? "Enter episodes  Del unfollow  F find" : gPodTab == PodTab::Saved ?
     "Enter play  Del delete  F find" : "Enter play  E show  Del remove  F find");
}
void renderPodFind() {
  auto& d = gCanvas;
  const uint16_t accent = podAccent(), white = TFT_WHITE, muted = rgb565_local(182, 219, 255), dim = rgb565_local(60, 60, 110);
  const uint16_t green = rgb565_local(88, 240, 141), amber = rgb565_local(255, 136, 64);
  drawBigHeader("Find", accent, podClockText(), ", / what to search", "Enter search");
  d.setFont(&fonts::Font2);
  for (uint8_t i = 0; i < podcast::kKinds; ++i) {
    const bool on = uint8_t(gPodKind) == i;
    const int16_t x = 8 + i * 75;
    const char* name = podcast::kindName(podcast::Kind(i));
    if (on) d.fillRoundRect(x, 28, 71, 20, 5, accent);
    else d.drawRoundRect(x, 28, 71, 20, 5, dim);
    d.setTextColor(on ? TFT_BLACK : muted);
    d.setCursor(x + (71 - d.textWidth(name)) / 2, 30);
    d.print(name);
  }
  d.drawRoundRect(8, 54, 222, 22, 5, accent);
  String shown = gPodQuery + "_";
  while (shown.length() > 1 && d.textWidth(shown) > 210) shown.remove(0, 1);  // The end, where typing is.
  d.setTextColor(white);
  d.setCursor(14, 57);
  d.print(shown);
  if (gPodQuery.isEmpty()) {
    const int16_t hx = 14 + d.textWidth(shown) + 4;
    d.setTextColor(muted);
    d.setCursor(hx, 57);
    d.print(radioFit(podcast::kindHint(gPodKind), 224 - hx));
  }
  bool good = true;
  String line = podActivity(good);
  uint16_t ink = good ? green : amber;
  if (line.isEmpty()) {
    line = gPodKind == podcast::Kind::Podcast ? "Searches Apple Podcasts" : gPodKind == podcast::Kind::Book ? "Free LibriVox books" : "From the Internet Archive";
    ink = muted;
  }
  d.setTextColor(ink);
  d.setCursor(8, 84);
  d.print(radioFit(line, 222));
  d.setFont(&fonts::Font0);
  d.setTextColor(muted);
  d.setCursor(8, 108);
  d.print(gPodWantPlaying ? "Searching pauses what's playing" : "Your words go to Apple or the Archive");
  d.setCursor(8, 122);
  d.print("Esc back");
  d.setFont(&fonts::Font2);
}
void renderPodShows() {
  const uint16_t accent = podAccent(), white = TFT_WHITE, muted = rgb565_local(182, 219, 255);
  drawBigHeader(podcast::kindName(gPodKind), accent, gPodFoundCount ? String(gPodFoundSel + 1) + "/" + String(gPodFoundCount) : String(""),
                "Enter opens", "S follows");
  podDrawList(gPodFoundCount, gPodFoundSel, [&](int i, int r) {
    const auto& s = gPodFound[i];
    drawBigRow(r, s.title, s.count ? String(s.count) : String(""), i == gPodFoundSel, accent, white, muted);
  }, "Nothing found");
}
void renderPodEpisodes() {
  const uint16_t accent = podAccent(), white = TFT_WHITE, muted = rgb565_local(182, 219, 255), green = rgb565_local(88, 240, 141);
  String place;
  if (gPodPage && gPodPage->count) place = String(gPodPage->from + 1) + "-" + String(gPodPage->from + gPodPage->count) + " of " + String(gPodPage->total);
  // The header's font has ASCII only, so a name in another script shows on the rows instead.
  const bool ascii = useBigTextFont(gPodShow.title) == 3;
  drawBigHeader(ascii && gPodShow.title[0] ? radioFit(gPodShow.title, 100) : String("Episodes"), accent, "", place, "Enter play  S save");
  const int rows = podEpisodeRows();
  const int now = podNowIndex();
  podDrawList(rows, gPodSel, [&](int i, int r) {
    int index = -1;
    const PodRow kind = podEpisodeRow(i, index);
    const bool selected = i == gPodSel;
    if (kind == PodRow::Follow) drawBigRow(r, gPodShowFollowed ? "Following" : "+ Follow this show", gPodShowFollowed ? "yes" : "", selected, accent, gPodShowFollowed ? green : white, green);
    else if (kind == PodRow::Previous) drawBigRow(r, "< Previous " + String(podcast::kPage), "", selected, accent, muted, muted);
    else if (kind == PodRow::Next) drawBigRow(r, "Next " + String(podcast::kPage) + " >", "", selected, accent, muted, muted);
    else {
      const auto& ep = gPodPage->episodes[index];
      const bool on = index == now && (gPodWantPlaying || podcast_player::playing());
      const bool saved = gPodPageSaved & (1u << index);
      // Green when it's on the card: "on card" until it's started, then what's left.
      String value = on ? String("playing") : saved && !gPodPageAt[index] ? String("on card") : podLeft(gPodPageAt[index], ep.seconds);
      drawBigRow(r, ep.title, value, selected, accent, white, on || saved ? green : muted);
    }
  }, "No episodes");
}
void renderPodRemove() {
  auto& d = gCanvas;
  const uint16_t muted = rgb565_local(182, 219, 255), amber = rgb565_local(255, 150, 80), white = TFT_WHITE;
  drawBigHeader("Delete from the card?", amber);
  useBigTextFont(gPodRemove.title);
  d.setTextColor(white);
  d.setCursor(8, 28);
  d.print(radioFit(gPodRemove.title, 222));
  radioTextFont(gPodRemove.showTitle);
  d.setTextColor(muted);
  d.setCursor(8, 49);
  d.print(radioFit(gPodRemove.showTitle, 222));
  d.setFont(&fonts::Font2);
  d.setTextColor(amber);
  d.setCursor(8, 70);
  d.print("Only the copy on the card goes:");
  d.setCursor(8, 86);
  d.print("it still plays over Wi-Fi.");
  drawBigNotice("Y delete   any other key keeps", amber);
}
// The meter, as the radio's: the decoded audio's loudness on a log scale.
uint32_t gPodMeterAtMs = 0;
void podMeterTick(bool playing) {
  if (!playing) { memset(gRadioMeter, 0, sizeof(gRadioMeter)); return; }
  if (millis() - gPodMeterAtMs < 90) return;
  gPodMeterAtMs = millis();
  memmove(gRadioMeter, gRadioMeter + 1, sizeof(gRadioMeter) - 1);
  const uint16_t level = podcast_player::level();
  const int h = level < 700 ? 1 : int((log2f(float(level)) - 9.5f) * 14.0f / 5.0f + 0.5f);
  gRadioMeter[kRadioMeterLen - 1] = uint8_t(h < 1 ? 1 : h > 14 ? 14 : h);
}
void renderPodPlayer() {
  auto& d = gCanvas;
  const uint16_t accent = podAccent(), white = TFT_WHITE, muted = rgb565_local(182, 219, 255), dim = rgb565_local(50, 50, 90);
  const uint16_t green = rgb565_local(88, 240, 141), amber = rgb565_local(255, 136, 64);
  const auto state = podcast_player::state();
  const bool audio = podcast_player::playing();
  const bool sounding = audio && state == podcast_player::State::Playing;
  const bool waiting = !sounding && (gPodStartPending || (audio && state != podcast_player::State::Stopping));
  podMeterTick(sounding);
  d.fillScreen(TFT_BLACK);
  // The show above, and the clock.
  const String clock = podClockText();
  d.setFont(&fonts::Font2);
  const int16_t clockX = kBigRight - d.textWidth(clock);
  d.setTextColor(muted);
  d.setCursor(clockX, 1);
  d.print(clock);
  const String show = gPodHasNow ? String(gPodNow.showTitle) : String("Podcasts");
  const bool wideShow = radioTextFont(show);
  d.setTextColor(gPodHasNow ? radioGenreInk(gPodNow.showTitle) : accent);
  d.setCursor(kBigLeft, wideShow ? 3 : 1);
  d.print(radioFit(show, clockX - kBigLeft - 8));
  drawRadioTile(kBigLeft, 20, gPodHasNow ? gPodNow.showTitle : "Podcasts", gPodHasNow ? gPodNow.showTitle : "", sounding, waiting, nullptr);
  constexpr int16_t textX = 76, textW = 229 - 76;
  // The episode over two lines, then what's happening.
  const String title = gPodHasNow ? String(gPodNow.title) : String("Nothing chosen yet");
  const bool wide = radioTextFont(title);
  String lines[2];
  const int n = radioWrap(title, textW, lines, 2);
  d.setTextColor(white);
  for (int i = 0; i < n; ++i) { d.setCursor(textX, (wide ? 22 : 20) + i * 16); d.print(lines[i]); }
  String status;
  bool good = true;
  if (gPodStartPending) status = int32_t(gPodStartAtMs - millis()) > 0 ? "Going to " + podClock(gPodNow.at) : String("Starting...");
  else if (audio) status = state == podcast_player::State::Connecting ? (gPodNowFile ? "Opening..." : "Asking the bridge...") :
                           state == podcast_player::State::Buffering ? String("Buffering...") :
                           state == podcast_player::State::Stopping ? String("Stopping...") : String(gPodNowFile ? "Playing from the card" : "Playing");
  else { status = podActivity(good); if (status.isEmpty() && gPodHasNow) status = "Paused"; }
  d.setFont(&fonts::Font2);
  d.setTextColor(good ? (audio || gPodStartPending ? green : muted) : amber);
  d.setCursor(textX, 54);
  d.print(radioFit(status, textW));
  // Where it is, how long it is, and the bar between.
  const uint32_t at = podNowAt(), seconds = gPodNow.seconds;
  d.setFont(&fonts::Font0);
  d.setTextColor(muted);
  d.setCursor(textX, 73);
  const String place = podClock(at) + (seconds ? " / " + podClock(seconds) : String(""));
  d.print(gPodHasNow ? place : String(""));
  // Where the sound comes from, on the right: the card, the home network, or over HTTPS.
  const char* source = !gPodHasNow ? "" : gPodNowSaved ? "card" : audio && gPodPlayHome ? "home" : audio ? "https" : "";
  if (*source) { d.setCursor(kBigRight - d.textWidth(source), 73); d.print(source); }
  d.fillRoundRect(kBigLeft, 85, 222, 4, 2, dim);
  if (seconds && at) d.fillRoundRect(kBigLeft, 85, max<int>(3, int(222ULL * min(at, seconds) / seconds)), 4, 2, audio ? green : accent);
  // The buttons with their keys: episode, back 15, play, ahead 30, volume.
  constexpr int16_t cy = 103, labelY = 117;
  const auto label = [&](int16_t cx, const char* text) {
    d.setFont(&fonts::Font0);
    d.setTextColor(muted);
    d.setCursor(cx - d.textWidth(text) / 2, labelY);
    d.print(text);
  };
  d.fillTriangle(16, cy - 3, 24, cy - 3, 20, cy - 9, muted);
  d.fillTriangle(16, cy + 3, 24, cy + 3, 20, cy + 9, muted);
  label(20, "; .");
  d.fillTriangle(62, cy, 70, cy - 7, 70, cy + 7, muted);
  d.fillTriangle(70, cy, 78, cy - 7, 78, cy + 7, muted);
  label(70, ", -15");
  const bool on = gPodWantPlaying || audio;
  d.fillCircle(120, cy, 11, audio ? green : accent);
  if (on) { d.fillRect(114, cy - 6, 4, 13, TFT_BLACK); d.fillRect(122, cy - 6, 4, 13, TFT_BLACK); }
  else d.fillTriangle(116, cy - 7, 116, cy + 7, 127, cy, TFT_BLACK);
  label(120, "Enter");
  d.fillTriangle(170, cy, 162, cy - 7, 162, cy + 7, muted);
  d.fillTriangle(178, cy, 170, cy - 7, 170, cy + 7, muted);
  label(170, "/ +30");
  d.fillRect(197, cy - 2, 3, 5, muted);
  d.fillTriangle(199, cy, 204, cy - 5, 204, cy + 5, muted);
  d.setFont(&fonts::Font2);
  d.setTextColor(muted);
  d.setCursor(208, cy - 8);
  d.print(String((int(gSpeakerVolume) * 100 + 127) / 255));
  label(215, "- +");
  d.setFont(&fonts::Font0);
  d.setTextColor(rgb565_local(110, 150, 180));
  const char* keys = gPodSaveRunning ? "X stops saving  E episodes  Esc back" : gPodNowSaved ? "E episodes  Esc back" : "S save to card  E episodes  Esc back";
  d.setCursor(120 - d.textWidth(keys) / 2, 127);
  d.print(keys);
  d.setFont(&fonts::Font2);
}
// Home, while an episode plays behind it: the episode in its show's colour, over a meter of what
// it's playing, where the radio's card goes. Ctrl+M's hint shows first; while it starts, that;
// and if it stops by itself, why, for a few seconds.
bool podcastHomeCardShows() {
  return (podcastOn() && gUiMode != UiMode::Podcasts) || (gPodBehindNoteUntilMs && int32_t(gPodBehindNoteUntilMs - millis()) > 0);
}
void drawHomePodcastCard() {
  auto& d = gCanvas;
  constexpr int16_t kLeft = 121, kRight = 229, kBase = 101;
  const uint16_t amber = rgb565_local(255, 136, 64), quiet = rgb565_local(150, 165, 175);
  const bool on = podcastOn();
  const uint16_t ink = !on ? amber : radioGenreInk(gPodNow.showTitle);
  // Headphones: the band and two cups.
  d.fillRect(kLeft + 3, 73, 8, 2, ink);
  d.fillRect(kLeft + 1, 75, 2, 6, ink);
  d.fillRect(kLeft + 11, 75, 2, 6, ink);
  d.fillRoundRect(kLeft, 80, 4, 8, 1, ink);
  d.fillRoundRect(kLeft + 10, 80, 4, 8, 1, ink);
  const String name = on ? String(gPodNow.title) : String("Stopped");  // The headphones say what.
  const bool wide = radioTextFont(name);
  d.setTextColor(on ? TFT_WHITE : amber);
  d.setCursor(kLeft + 18, wide ? 74 : 72);
  d.print(radioFit(name, kRight - kLeft - 18));
  const auto state = podcast_player::state();
  const bool sounding = on && podcast_player::playing() && state == podcast_player::State::Playing;
  podMeterTick(sounding);
  String line;
  if (!on) line = gPodBehindNote;
  else if (gPodBehindSinceMs && millis() - gPodBehindSinceMs < 4000) line = "Ctrl+M pauses it";
  else if (!sounding) line = gPodStartPending || gPodTicketRunning ? String("Starting...") : String("Buffering...");
  if (!line.isEmpty() || !sounding) {
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
// Ctrl+M, from any screen: pauses the episode, keeping the place. Paused, it carries on where it
// may play, if a podcast played last. False leaves the key to the radio.
bool podcastMusicKey() {
  if (podcastOn()) {
    podPause("Paused");
    gPodBehindNoteUntilMs = 0;
    setStatus("Podcast paused", 1200);
    return true;
  }
  if (radioOn() || internet_radio::busy() || !gPodHasNow || !gPodLastAudio || !podcastMayPlayIn(gUiMode)) return false;
  if (!gPodNowFile && !podOnCard(gPodNow.episode) && !gWifiReady) { setStatus("Podcast: Wi-Fi required", 1500); return true; }
  const bool here = gUiMode == UiMode::Podcasts;
  podPlay(gPodNow, PodView::Player, here);
  if (!here) {
    gPodBehindSinceMs = max<uint32_t>(millis(), 1);
    gPodBehindNoteUntilMs = 0;
    setStatus("Podcast: " + String(gPodNow.title), 1500);
  }
  return true;
}
void renderPodcastsUi() {
  switch (gPodView) {
    case PodView::Find: renderPodFind(); break;
    case PodView::Shows: renderPodShows(); break;
    case PodView::Episodes: renderPodEpisodes(); break;
    case PodView::Player: renderPodPlayer(); break;
    case PodView::Remove: renderPodRemove(); break;
    default: renderPodLibrary(); break;
  }
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
