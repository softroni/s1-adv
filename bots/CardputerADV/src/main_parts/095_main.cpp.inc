// Cameras: the eufy cameras on the bridge's Mac (bridge/src/cameras.mjs). The wall shows each
// camera's last picture; Enter opens one full screen, L watches it live, R wakes it for a new
// picture, C lists the HomeBase's recordings by day (and D a month of them), and a recording plays
// in the live view, which then has a clip's keys instead of live's. The wall is drawn on the menu
// canvas, with thumbnails the bridge has already dithered to its 3-3-2 colours, and so are the
// lists of recordings. A picture, a live frame or a clip's frame is a JPEG drawn straight to the
// screen in full colour, so while one shows, the canvas isn't pushed over it (cameraDrawsDirect).
//
// At home everything comes over the bridge's plain-HTTP door with a ticket, which live view
// needs: TLS's buffers and a frame don't fit together. Away from home, pictures come over HTTPS
// and live view says it works at home only.
#include "../camera_catalog.h"
#include "../camera_net.h"
namespace {
enum class CamView : uint8_t { Wall, Picture, Live, Clips, Month };
enum class CamJob : uint8_t { None, Ticket, List, Thumb, Picture, Refresh, LiveStart, Frame, Extend, Stop, Turn, Code, Report, Setup,
                              Zoom, Pan, AutoRefresh, Audio, ClipList, ClipThumb, MonthList, ClipStart, ClipPause, ClipSeek };

constexpr uint32_t kCamListEveryMs = 60000;   // The wall's ages and battery, asked again this often.
constexpr uint32_t kCamSigningEveryMs = 4000; // And while signing in, so the screen follows the phone.
constexpr uint32_t kCamNoticeMs = 3500;
constexpr uint32_t kCamBarsMs = 3000;         // A picture's bars, after a key, before they go.
constexpr uint32_t kCamTurnLitMs = 350;
constexpr uint32_t kCamRefreshWaitMs = 45000; // A still wakes a camera: it can take a while.
constexpr int16_t kCamTopBar = 16, kCamBottomBar = 12;
constexpr int16_t kCamClipRowH = 12, kCamClipListY = 25, kCamClipListW = 156;
constexpr int kCamClipRows = 9;               // The list's rows on screen: 25 + 9 x 12 is 133.

// Live view's sound: the bridge sends 8 kHz mono 16-bit PCM, which goes into a half-second ring and
// out to the speaker in 50 ms buffers on a channel of its own, once a quarter second is held, so a
// late answer from the bridge plays from what's held instead of breaking up.
constexpr uint8_t kCamAudioChannel = 1;       // The one no other player uses (scripts/test_cameras.py checks).
constexpr uint32_t kCamAudioRate = 8000;
constexpr size_t kCamAudioRing = 4000, kCamAudioChunk = 400, kCamAudioStart = 2000, kCamAudioSlots = 3;
constexpr uint8_t kCamAudioGiveUp = 60;       // Answers with no sound before it's taken as a camera without.

uint16_t camAccent() { return rgb565_local(85, 146, 255); }
uint16_t camMuted() { return rgb565_local(146, 146, 170); }
uint16_t camRed() { return rgb565_local(255, 73, 73); }
uint16_t camAmber() { return rgb565_local(255, 182, 85); }
uint16_t camPanel() { return rgb565_local(36, 36, 85); }

CamView gCamView = CamView::Wall;
cameras::List* gCamList = nullptr;      // On the heap while the app is open.
bool gCamListOk = false;                // A list has come since the app opened.
uint32_t gCamListAtMs = 0;              // When it came: its ages count from then.
uint8_t* gCamThumb[cameras::kMax] = {};
bool gCamThumbAsked[cameras::kMax] = {}, gCamThumbNone[cameras::kMax] = {};
uint8_t gCamThumbTries[cameras::kMax] = {};  // Asked again after running short of memory, a few times.
uint32_t gCamThumbNotBefore = 0;
uint32_t gCamClipThumbNotBefore = 0;
uint32_t gCamMemReportedMs = 0;
uint8_t gCamSel = 0;                    // The wall's selected tile.
uint8_t gCamCur = 0;                    // The camera a picture or live view shows.
uint8_t* gCamJpeg = nullptr;            // What's on screen in those views.
size_t gCamJpegLen = 0;
int gCamJpegOf = -1;                    // Which camera it's of, -1 none.
bool gCamPictureNone = false;           // Asked, and the camera has no picture yet.
CamJob gCamJob = CamJob::None;
int gCamJobCam = -1;
// The home door, asked once on each network.
podcast::HomeTicket gCamTicket;
uint32_t gCamTicketFor = 0, gCamHomeOffFor = 0;
bool gCamRetriedTicket = false;
// What's waiting for the worker.
bool gCamWantList = true, gCamWantPicture = false, gCamWantStop = false, gCamWantExtend = false;
int gCamWantRefresh = -1;
cameras::Arrow gCamWantTurn = cameras::Arrow::None;
char gCamCode[9] = "";
bool gCamWantCode = false;
char gCamReport[200] = "";
// Live view.
bool gCamLive = false, gCamLiveStarted = false;
uint32_t gCamLiveEndsMs = 0, gCamLiveBeganMs = 0, gCamLiveFrames = 0, gCamLiveBytes = 0, gCamLiveFailures = 0;
uint32_t gCamFpsWindowMs = 0, gCamFpsFrames = 0;
float gCamFps = 0;
uint32_t gCamTurnLitUntil = 0;
// Zoom and pan: where the bridge says the view is, and what's waiting to be asked.
cameras::View gCamZoomView;
uint8_t gCamWantZoom = 0;                // Tenths to ask for, 0 none.
cameras::Arrow gCamWantPan = cameras::Arrow::None;
bool gCamWantPanTurn = false;            // Fn with the arrow: turn the camera whatever the zoom.
uint32_t gCamZoomLitUntil = 0;           // The zoom in the bar is lit a moment after it changes.
// Sound: the ring, the speaker's buffers, and how it's going.
int16_t* gCamAudioRing = nullptr;
int16_t* gCamAudioSlot = nullptr;
size_t gCamAudioHead = 0, gCamAudioCount = 0;
uint8_t gCamAudioNext = 0, gCamAudioEmpty = 0;
bool gCamWantAudio = false, gCamAudioPlaying = false, gCamAudioHeard = false, gCamAudioNone = false;
bool gCamAudioMuted = false;             // M; kept for the visit, so the next camera is muted too.
uint32_t gCamAudioSamples = 0, gCamAudioGaps = 0;
// Recordings: a day's list, the month, and a clip playing in the live view (gCamClipMode).
cameras::ClipDay gCamClipStore;          // The list; its clips on the heap, exactly as many as came.
cameras::ClipDay* gCamClips = nullptr;   // &gCamClipStore while the list is open.
uint32_t gCamToday = 0;                  // The Mac's today, from its first answer: 0 until then.
uint32_t gCamClipsDay = 0, gCamClipsAsked = 0;
uint8_t gCamClipRows[cameras::kMaxClips]; // The list's rows: its clips, the camera picked's only.
int gCamClipRowCount = 0, gCamClipSel = 0;
char gCamClipFilterId[cameras::kIdBytes] = "";  // A camera's id, or "" for all of them.
CamView gCamClipsBack = CamView::Wall;   // Where Esc from the list goes,
char gCamClipsBackId[cameras::kIdBytes] = "";  // and the camera whose picture it was.
bool gCamWantClips = false, gCamClipsLoading = false;
bool gCamClipsQuick = false;             // Only the camera changed: the Mac answers from the list it read.
String gCamClipsProblem;
uint8_t* gCamClipThumb = nullptr;
char gCamClipThumbId[cameras::kClipIdBytes] = "";
char gCamClipThumbNone[cameras::kClipIdBytes] = "";
char gCamClipThumbAsking[cameras::kClipIdBytes] = "";
uint32_t gCamClipSelAtMs = 0, gCamClipThumbAskedMs = 0;
cameras::Month gCamMonth;                // ym 0 until its answer comes.
uint32_t gCamMonthCursor = 0;            // The day the month view is on.
uint32_t gCamMonthAsked = 0;             // The month asked for last.
bool gCamWantMonth = false;
String gCamMonthProblem;
bool gCamClipMode = false, gCamWantClipStart = false;
char gCamClipCam[cameras::kIdBytes] = "";
cameras::Clip gCamClipNow;               // The clip playing.
uint32_t gCamClipNowDay = 0;
cameras::ClipState gCamClipState;
int gCamWantClipPause = -1;              // 1 pause, 0 play, -1 nothing to ask.
int32_t gCamWantClipSeek = -1;           // Milliseconds, -1 nothing to ask.
uint32_t gCamClipHintsUntil = 0;
uint32_t gCamClipGen = 0, gCamJobGen = 0; // Which clip a request was for: one for a clip before is stale.
uint32_t gCamFrameNotBefore = 0;         // A paused clip's frames are asked a few times a second, not at once.
char gCamStopCam[cameras::kIdBytes] = ""; // The camera live/stop is for: the one live or the clip's.
// Opening the app takes a new picture from each camera once, after the last ones are shown.
bool gCamAutoPlanned = false;
bool gCamAutoWanted[cameras::kMax] = {};
int gCamAutoCur = -1;
// The screen.
bool gCamDirty = true;                  // The picture needs drawing again.
bool gCamWasDirect = false;
uint32_t gCamBarsUntil = 0;
uint32_t gCamRefreshSince = 0;          // A new picture is being taken, since then.
uint32_t gCamOpenSince = 0;             // When the app opened, for how long its worker may take to start.
uint32_t gCamShownSecond = 0;
String gCamNotice;
uint16_t gCamNoticeInk = 0;
uint32_t gCamNoticeUntil = 0;
String gCamProblem;                     // Why the wall is empty, when the bridge couldn't be asked.
// The phone sign-in page's address, shown as a QR code until eufy is signed in.
String gCamSetupUrl;
uint32_t gCamSetupUntilMs = 0, gCamSetupAskedMs = 0;
bool gCamSetupNone = false;             // The bridge has no page (an older one, or nothing to sign in with).

void camInvalidate() { gCamDirty = true; gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
void camSay(const String& text, uint16_t ink = 0) {
  gCamNotice = text;
  gCamNoticeInk = ink ? ink : camAmber();
  gCamNoticeUntil = millis() + kCamNoticeMs;
  camInvalidate();
}
bool camNoticeShowing() { return gCamNoticeUntil && int32_t(millis() - gCamNoticeUntil) < 0; }
uint8_t camCount() { return gCamList ? gCamList->count : 0; }
const cameras::Camera* camAt(int i) { return gCamList && i >= 0 && i < gCamList->count ? &gCamList->cams[i] : nullptr; }
bool camReady() { return gCamList && gCamListOk && gCamList->state == cameras::State::Ready; }

void camFreeThumbs() {
  for (size_t i = 0; i < cameras::kMax; ++i) { free(gCamThumb[i]); gCamThumb[i] = nullptr; gCamThumbAsked[i] = gCamThumbNone[i] = false; gCamThumbTries[i] = 0; }
  gCamThumbNotBefore = 0;
}
void camFreeJpeg() { free(gCamJpeg); gCamJpeg = nullptr; gCamJpegLen = 0; gCamJpegOf = -1; }

// --- The way to the bridge -----------------------------------------------------------------
uint32_t camHomeIp() {
  const uint32_t mine = wifiLocalAddress();
  return mine && gCamTicketFor == mine && gCamHomeOffFor != mine ? podcast::homeFor(gCamTicket, mine) : 0;
}
bool camTicketWanted() {
  const uint32_t mine = wifiLocalAddress();
  return mine && gCamTicketFor != mine && gCamHomeOffFor != mine;
}
// Over the home door when there's one, else HTTPS with the memory TLS needs made ready.
bool camStart(CamJob job, int cam, const char* httpsPath, size_t maxBytes, uint32_t waitMs) {
  char home[200];
  const uint32_t ip = camHomeIp();
  const bool viaHome = ip && cameras::homePath(home, sizeof(home), gCamTicket, httpsPath);
  if (!viaHome) {
    prepareNetworkMemory("cameras", true);
    compactMemoryForUi("cameras", true);
  }
  if (!camera_net::start(viaHome ? home : httpsPath, viaHome, ip, gCamTicket.port, maxBytes, waitMs)) return false;
  gCamJob = job;
  gCamJobCam = cam;
  gCamJobGen = gCamClipGen;
  return true;
}
bool camStartFor(CamJob job, int cam, const char* what, size_t maxBytes, uint32_t waitMs, const char* extra = "") {
  const cameras::Camera* c = camAt(cam);
  char path[160];
  if (!c || !cameras::routePath(path, sizeof(path), what, c->id, extra)) return false;
  return camStart(job, cam, path, maxBytes, waitMs);
}

// The live view's camera, by id: the one picked, or a playing clip's.
const char* camLiveId() {
  if (gCamClipMode) return gCamClipCam;
  const cameras::Camera* c = camAt(gCamCur);
  return c ? c->id : "";
}
bool camStartLive(CamJob job, const char* what, size_t maxBytes, uint32_t waitMs, const char* extra = "") {
  char path[160];
  if (!cameras::routePath(path, sizeof(path), what, camLiveId(), extra)) return false;
  return camStart(job, gCamCur, path, maxBytes, waitMs);
}

// What went wrong or how live view went, for the bridge's log. Only numbers and short words.
void camQueueReport(const char* stage, const char* note, unsigned code) {
  const char* fw = APP_VERSION;
  snprintf(gCamReport, sizeof(gCamReport),
           "/api/cardputer/cameras/report?stage=%s&e=%u&free=%u&big=%u&via=%s&fps=%.1f&ms=%u&bytes=%u&fw=%s&note=%s",
           stage, code, unsigned(ESP.getFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
           camera_net::overHome() ? "home" : "https", double(gCamFps),
           unsigned(gCamLiveFrames ? (millis() - gCamLiveBeganMs) / gCamLiveFrames : 0),
           unsigned(gCamLiveFrames ? gCamLiveBytes / gCamLiveFrames : 0), fw, note && *note ? note : "-");
  for (char* p = gCamReport; *p; ++p) if (*p == ' ') *p = '_';
}

// What ran short of memory, with the heap's free and largest piece, for the bridge's log: once in a
// while, not for every request that fails the same way.
void camReportMemory(const char* what) {
  if (gCamMemReportedMs && millis() - gCamMemReportedMs < 15000) return;
  gCamMemReportedMs = millis();
  char note[64];
  snprintf(note, sizeof(note), "%s_%s", what, camera_net::memoryNote()[0] ? camera_net::memoryNote() : "heap");
  camQueueReport("memory", note, 1);
}

// --- Sound ----------------------------------------------------------------------------------
void camAudioStop() {
  if (gCamAudioPlaying || gCamAudioRing) M5Cardputer.Speaker.stop(kCamAudioChannel);
  gCamAudioPlaying = false;
  gCamAudioHead = gCamAudioCount = 0;
}
void camAudioFree() {
  camAudioStop();
  // The speaker may still read the last buffers: it's stopped first, so they can go.
  free(gCamAudioRing);
  free(gCamAudioSlot);
  gCamAudioRing = gCamAudioSlot = nullptr;
}
bool camAudioOn() { return gCamLive && gCamLiveStarted && !gCamAudioMuted && !gCamAudioNone; }
// What the bridge sent, into the ring; the oldest goes when it's full.
void camAudioTake(const uint8_t* data, size_t length) {
  if (!gCamAudioRing) {
    gCamAudioRing = static_cast<int16_t*>(malloc(kCamAudioRing * sizeof(int16_t)));
    gCamAudioSlot = static_cast<int16_t*>(malloc(kCamAudioSlots * kCamAudioChunk * sizeof(int16_t)));
    if (!gCamAudioRing || !gCamAudioSlot) {
      camAudioFree();
      gCamAudioNone = true;
      camSay("Not enough memory for sound");
      return;
    }
  }
  const size_t samples = length / 2;
  for (size_t i = 0; i < samples; ++i) {
    const int16_t v = int16_t(data[2 * i] | (data[2 * i + 1] << 8));
    gCamAudioRing[(gCamAudioHead + gCamAudioCount) % kCamAudioRing] = v;
    if (gCamAudioCount < kCamAudioRing) ++gCamAudioCount;
    else gCamAudioHead = (gCamAudioHead + 1) % kCamAudioRing;
  }
}
// From the ring to the speaker, while it has room for another buffer.
void camAudioFeed() {
  if (!gCamAudioRing || gCamAudioMuted) return;
  if (!gCamAudioPlaying) {
    if (gCamAudioCount < kCamAudioStart) return;
    gCamAudioPlaying = true;
  }
  while (gCamAudioCount >= kCamAudioChunk && M5Cardputer.Speaker.isPlaying(kCamAudioChannel) < 2) {
    int16_t* slot = gCamAudioSlot + gCamAudioNext * kCamAudioChunk;
    for (size_t i = 0; i < kCamAudioChunk; ++i) slot[i] = gCamAudioRing[(gCamAudioHead + i) % kCamAudioRing];
    gCamAudioHead = (gCamAudioHead + kCamAudioChunk) % kCamAudioRing;
    gCamAudioCount -= kCamAudioChunk;
    if (!M5Cardputer.Speaker.playRaw(slot, kCamAudioChunk, kCamAudioRate, false, 1, kCamAudioChannel, false)) break;
    gCamAudioNext = uint8_t((gCamAudioNext + 1) % kCamAudioSlots);
    gCamAudioSamples += kCamAudioChunk;
  }
  // Ran dry: hold a quarter second again before going on.
  if (gCamAudioCount < kCamAudioChunk && M5Cardputer.Speaker.isPlaying(kCamAudioChannel) == 0) {
    gCamAudioPlaying = false;
    ++gCamAudioGaps;
  }
}

// --- Leaving and opening views -------------------------------------------------------------
void camEndLive(const char* why) {
  if (!gCamLive) return;
  gCamLive = false;
  gCamWantExtend = false;
  gCamWantTurn = cameras::Arrow::None;
  gCamWantZoom = 0;
  gCamWantPan = cameras::Arrow::None;
  gCamZoomView = cameras::View{};
  gCamWantStop = gCamLiveStarted;
  snprintf(gCamStopCam, sizeof(gCamStopCam), "%s", camLiveId());
  if (gCamLiveStarted && gCamLiveFrames && !gCamClipMode) {
    char note[40];
    snprintf(note, sizeof(note), "%s_sound%us_gaps%u", why, unsigned(gCamAudioSamples / kCamAudioRate), unsigned(gCamAudioGaps));
    camQueueReport("live", note, 0);
    appendRuntimeLog("CAM", String("live ") + gCamLiveFrames + " frames " + String(gCamFps, 1) + " fps, " + why, true);
  }
  gCamLiveStarted = false;
  camAudioFree();
  gCamWantAudio = false;
  if (gCamJob == CamJob::Frame || gCamJob == CamJob::LiveStart || gCamJob == CamJob::Audio || gCamJob == CamJob::ClipStart) camera_net::cancel();
  if (gCamClipMode) {
    // A recording isn't the camera's picture now: back to the day's list, the clip still picked.
    if (gCamJob == CamJob::ClipStart) gCamWantStop = true;  // It may have begun on the Mac by now.
    ++gCamClipGen;
    gCamClipMode = false;
    gCamWantClipStart = false;
    gCamWantClipPause = -1;
    gCamWantClipSeek = -1;
    camFreeJpeg();
    gCamView = CamView::Clips;
    camInvalidate();
    return;
  }
  // The last frame is now the camera's picture, here and on the bridge.
  if (gCamJpeg && gCamJpegOf >= 0 && gCamLiveFrames) {
    gCamList->cams[gCamJpegOf].pictureAge = 0;
    gCamThumbAsked[gCamJpegOf] = false;
    free(gCamThumb[gCamJpegOf]);
    gCamThumb[gCamJpegOf] = nullptr;
  }
  gCamView = CamView::Picture;
  gCamBarsUntil = millis() + kCamBarsMs;
  camInvalidate();
}
void camLiveBegin();
void camOpenPicture(int cam) {
  if (!camAt(cam)) return;
  if (gCamJpegOf != cam) { camFreeJpeg(); gCamPictureNone = false; gCamWantPicture = true; }
  gCamCur = uint8_t(cam);
  gCamView = CamView::Picture;
  gCamBarsUntil = millis() + kCamBarsMs;
  camInvalidate();
}
void camOpenLive(int cam) {
  const cameras::Camera* c = camAt(cam);
  if (!c) return;
  if (c->off) { camSay("That camera is switched off"); return; }
  if (!camHomeIp()) { camSay(camTicketWanted() ? "Finding the Mac on this Wi-Fi..." : "Live works on home Wi-Fi only"); return; }
  if (gCamJpegOf != cam) { camFreeJpeg(); gCamWantPicture = true; }
  gCamCur = uint8_t(cam);
  camLiveBegin();
}
// What a live view and a clip both start from.
void camLiveBegin() {
  gCamView = CamView::Live;
  gCamLive = true;
  gCamLiveStarted = false;
  gCamWantStop = false;
  gCamLiveBeganMs = millis();
  gCamLiveEndsMs = millis() + cameras::kLiveSeconds * 1000;
  gCamLiveFrames = gCamLiveBytes = gCamLiveFailures = 0;
  gCamFps = 0;
  gCamFpsWindowMs = millis();
  gCamFpsFrames = 0;
  gCamRefreshSince = 0;
  gCamZoomView = cameras::View{};
  gCamWantZoom = 0;
  gCamWantPan = cameras::Arrow::None;
  camAudioFree();
  gCamWantAudio = false;
  gCamAudioHeard = gCamAudioNone = false;
  gCamAudioEmpty = 0;
  gCamAudioSamples = gCamAudioGaps = 0;
  camInvalidate();
}
void camRefresh(int cam) {
  const cameras::Camera* c = camAt(cam);
  if (!c) return;
  if (c->off) { camSay("That camera is switched off"); return; }
  if (gCamRefreshSince) { camSay("Already taking one"); return; }
  gCamWantRefresh = cam;
  gCamRefreshSince = millis();
  camInvalidate();
}
void camSave() {
  if (!gCamJpeg || gCamJpegOf < 0) { camSay("No picture to save"); return; }
  if (!gSdReady) { camSay("No SD card"); return; }
  char path[96];
  struct tm t = {};
  const time_t now = time(nullptr);
  if (gTimeValid) localtime_r(&now, &t);
  else { t.tm_year = 70; t.tm_mon = 0; t.tm_mday = 1; const uint32_t s = millis() / 1000; t.tm_hour = s / 3600 % 24; t.tm_min = s / 60 % 60; t.tm_sec = s % 60; }
  if (!cameras::savePath(path, sizeof(path), gCamList->cams[gCamJpegOf].name, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec) || !ensureDirRecursive(SD, "/cameras")) { camSay("SD card problem"); return; }
  File f = SD.open(path, FILE_WRITE);
  const bool ok = f && f.write(gCamJpeg, gCamJpegLen) == gCamJpegLen;
  if (f) f.close();
  if (!ok) { SD.remove(path); camSay("Couldn't save it to the card"); return; }
  camSay(String("Saved to ") + path, rgb565_local(88, 240, 141));
}

// --- Recordings ----------------------------------------------------------------------------
int camIndexOfId(const char* id) {
  for (uint8_t i = 0; id && i < camCount(); ++i) if (!strcmp(gCamList->cams[i].id, id)) return i;
  return -1;
}
const char* camNameOfId(const char* id) {
  const int i = camIndexOfId(id);
  return i >= 0 ? gCamList->cams[i].name : "A camera";
}
// The rows: the day's clips, only the picked camera's when one is.
void camClipRowsBuild() {
  gCamClipRowCount = 0;
  for (uint16_t i = 0; gCamClips && i < gCamClips->count; ++i) {
    if (gCamClipFilterId[0] && strcmp(gCamClips->clips[i].cam, gCamClipFilterId)) continue;
    gCamClipRows[gCamClipRowCount++] = uint8_t(i);
  }
  if (gCamClipSel >= gCamClipRowCount) gCamClipSel = gCamClipRowCount ? gCamClipRowCount - 1 : 0;
}
const cameras::Clip* camClipAtRow(int row) {
  return gCamClips && row >= 0 && row < gCamClipRowCount ? &gCamClips->clips[gCamClipRows[row]] : nullptr;
}
void camClipFreeThumb() { free(gCamClipThumb); gCamClipThumb = nullptr; gCamClipThumbId[0] = 0; }
void camClipSelect(int row) {
  if (!gCamClipRowCount) return;
  row = max(0, min(row, gCamClipRowCount - 1));
  if (row == gCamClipSel) return;
  gCamClipSel = row;
  gCamClipSelAtMs = millis();  // Its picture is asked once the selection rests.
  gCamClipThumbAskedMs = 0;
  camInvalidate();
}
// The clips go back to the heap while the next day's are asked for.
void camClipsFreeList() {
  delete[] gCamClipStore.clips;
  gCamClipStore.clips = nullptr;
  gCamClipStore.cap = gCamClipStore.count = gCamClipStore.total = 0;
  gCamClipRowCount = 0;
}
void camClipsLoadDay(uint32_t day) {
  gCamClipsDay = day;
  gCamClipsQuick = false;
  if (gCamClips) { camClipsFreeList(); gCamClips->day = day; }
  gCamClipRowCount = 0;
  gCamClipSel = 0;
  gCamClipsLoading = true;
  gCamWantClips = true;
  gCamClipsProblem = "";
  gCamClipSelAtMs = millis();
  camInvalidate();
}
// From the wall (every camera) or a picture (that camera's), today first.
void camOpenClips(const char* filterId, CamView back) {
  gCamClips = &gCamClipStore;  // Nothing to make room for yet: the clips get theirs when they come.
  snprintf(gCamClipFilterId, sizeof(gCamClipFilterId), "%s", filterId ? filterId : "");
  gCamClipsBack = back;
  snprintf(gCamClipsBackId, sizeof(gCamClipsBackId), "%s", camLiveId());
  gCamToday = 0;  // Asked without a day, the Mac answers with its today.
  gCamClipThumbNone[0] = 0;
  camClipsLoadDay(0);
  gCamView = CamView::Clips;
  camInvalidate();
}
void camCloseClips() {
  camClipsFreeList();
  gCamClips = nullptr;
  gCamClipRowCount = gCamClipSel = 0;
  camClipFreeThumb();
  gCamWantClips = gCamWantMonth = gCamClipsLoading = false;
  gCamMonth = cameras::Month{};
}
// Another camera, or all of them: F steps through them in the wall's order. The Mac sends that
// camera's clips, all of them on a day with more than the list holds.
void camClipsNextFilter() {
  if (!gCamClipsDay) return;
  const int at = gCamClipFilterId[0] ? camIndexOfId(gCamClipFilterId) : -1;
  const int next = at + 1 < camCount() ? at + 1 : -1;
  snprintf(gCamClipFilterId, sizeof(gCamClipFilterId), "%s", next >= 0 ? gCamList->cams[next].id : "");
  camClipsLoadDay(gCamClipsDay);
  gCamClipsQuick = true;
}
void camClipsStepDay(int days) {
  if (!gCamToday || !gCamClipsDay) return;
  const uint32_t day = cameras::addDays(gCamClipsDay, days);
  if (day > gCamToday) { camSay("That's today"); return; }
  camClipsLoadDay(day);
}
// The month the cursor is in, asked for unless it's here or on its way.
void camMonthWant() {
  const uint32_t ym = gCamMonthCursor / 100;
  if (gCamMonth.ym == ym || (gCamJob == CamJob::MonthList && gCamMonthAsked == ym)) return;
  gCamMonth = cameras::Month{};
  gCamMonthProblem = "";
  gCamWantMonth = true;
}
void camOpenMonth() {
  if (!gCamClipsDay) return;
  gCamMonthCursor = gCamClipsDay;
  camMonthWant();
  gCamView = CamView::Month;
  camInvalidate();
}
void camMonthMove(int days) {
  uint32_t day = cameras::addDays(gCamMonthCursor, days);
  if (gCamToday && day > gCamToday) day = gCamToday;
  if (day == gCamMonthCursor) return;
  gCamMonthCursor = day;
  camMonthWant();
  camInvalidate();
}
// A month back or on, on the same day of it where there is one.
void camMonthStep(int months) {
  cameras::Ymd v = cameras::ymdOf(gCamMonthCursor);
  v.m += months;
  if (v.m < 1) { v.m = 12; --v.y; }
  if (v.m > 12) { v.m = 1; ++v.y; }
  v.d = min(v.d, cameras::daysInMonth(v.y, v.m));
  const uint32_t day = cameras::dayOf(v);
  if (gCamToday && day / 100 > gCamToday / 100) { camSay("That's this month"); return; }
  gCamMonthCursor = gCamToday && day > gCamToday ? gCamToday : day;
  camMonthWant();
  camInvalidate();
}

// A clip plays in the live view, from the start. Another clip while one plays takes its place on the Mac.
void camPlayClip(int row) {
  const cameras::Clip* clip = camClipAtRow(row);
  if (!clip) return;
  if (!camHomeIp()) { camSay(camTicketWanted() ? "Finding the Mac on this Wi-Fi..." : "Clips play on home Wi-Fi only"); return; }
  if (gCamLive && !gCamClipMode) return;
  if (gCamJob == CamJob::Frame || gCamJob == CamJob::Audio || gCamJob == CamJob::ClipStart || gCamJob == CamJob::ClipThumb) camera_net::cancel();
  gCamClipSel = row;
  gCamClipNow = *clip;
  gCamClipNowDay = gCamClipsDay;
  snprintf(gCamClipCam, sizeof(gCamClipCam), "%s", clip->cam);
  const int cam = camIndexOfId(clip->cam);
  if (cam >= 0) gCamCur = uint8_t(cam);
  camFreeJpeg();
  camClipFreeThumb();  // The room goes to the frames; it's asked again back in the list.
  ++gCamClipGen;
  gCamClipMode = true;
  gCamClipState = cameras::ClipState{};
  gCamWantClipStart = true;
  gCamWantClipPause = -1;
  gCamWantClipSeek = -1;
  gCamFrameNotBefore = 0;
  gCamClipHintsUntil = millis() + kCamBarsMs;
  camLiveBegin();
}
// The clip's place, 10 seconds back or on, from where it is or where it's already going.
void camClipSeekBy(int32_t ms) {
  if (!gCamLiveStarted || !gCamClipState.known || !gCamClipState.dur) return;
  const int32_t from = gCamWantClipSeek >= 0 ? gCamWantClipSeek : int32_t(gCamClipState.pos);
  const int32_t last = int32_t(gCamClipState.dur) - 500;
  int32_t to = from + ms;
  if (to < 0) to = 0;
  if (to > last) to = last > 0 ? last : 0;
  gCamWantClipSeek = to;
  gCamClipState.pos = uint32_t(to);
  gCamClipState.ended = false;
  camAudioStop();  // What was held is from the old place.
  camInvalidate();
}
void camClipPlayPause() {
  if (!gCamLiveStarted || !gCamClipState.known) return;
  const bool play = !gCamClipState.playing || gCamClipState.ended;
  gCamWantClipPause = play ? 0 : 1;
  if (play && gCamClipState.ended) { gCamClipState.pos = 0; camAudioStop(); }
  gCamClipState.playing = play;
  gCamClipState.ended = false;
  camInvalidate();
}

// --- The worker's answers ------------------------------------------------------------------
// A word the bridge sent with an error, like "camera_off", for the screen.
String camWord(const uint8_t* data, size_t length) {
  String word;
  for (size_t i = 0; data && i < length && i < 40; ++i) {
    const char c = char(data[i]);
    if ((c >= 'a' && c <= 'z') || c == '_') word += c;
    else break;
  }
  return word;
}
// Cut to fit a width in the font d has now, measured by d: the screen and the canvas can have
// different fonts at once.
String camFit(lgfx::LGFXBase& d, const String& text, int32_t width) {
  if (d.textWidth(text) <= width) return text;
  String s = text;
  while (s.length() > 1 && d.textWidth(s + "..") > width) {
    s.remove(s.length() - 1);
    while (s.length() > 1 && (static_cast<uint8_t>(s[s.length() - 1]) & 0xC0) == 0x80) s.remove(s.length() - 1);
    if (s.length() && static_cast<uint8_t>(s[s.length() - 1]) >= 0xC0) s.remove(s.length() - 1);
  }
  return s + "..";
}
String camProblemText(camera_net::Error e, const String& word) {
  if (word == "camera_off") return "That camera is switched off";
  if (word == "not_set_up") return "Cameras aren't set up on the Mac";
  if (word == "starting") return "Cameras are starting on the Mac";
  if (word == "no_picture") return "No picture yet";
  if (word == "not_ready") return "eufy isn't signed in";
  if (word == "no_still" || word == "camera_timeout") return "The camera didn't answer";
  if (word == "no_live") return "Live view didn't start";
  if (word == "no_clip") return "That clip has gone";
  if (word == "clip_failed") return "The HomeBase didn't send it";
  if (word == "clip_codec") return "Can't play that clip's video";
  if (word == "no_base") return "No HomeBase on the account";
  if (word == "no_archive") return "The HomeBase can't list clips";
  if (e == camera_net::Error::NoMemory) return String("Not enough memory (") + camera_net::memoryNote() + ")";
  const char* text = camera_net::errorText(e);
  return text ? String(text) : String("Camera error");
}

void camTakeJpeg(int cam, uint8_t* data, size_t length) {
  free(gCamJpeg);
  gCamJpeg = data;
  gCamJpegLen = length;
  gCamJpegOf = cam;
  gCamPictureNone = false;
  camInvalidate();
}

// Where a clip is, from its frames' x-clip header. A pause or seek still to be asked wins.
void camClipHeard() {
  cameras::ClipState state;
  if (!cameras::parseClipState(camera_net::clipHeader(), state)) return;
  if (gCamWantClipSeek >= 0 || gCamWantClipPause >= 0) return;
  if (state.playing != gCamClipState.playing || state.ended != gCamClipState.ended || state.pos / 1000 != gCamClipState.pos / 1000) camInvalidate();
  gCamClipState = state;
  // Paused or ended, the Mac answers at once: asked a few times a second, for a seek or play from elsewhere.
  gCamFrameNotBefore = !state.playing || state.ended ? millis() + 250 : 0;
}

void camFinished() {
  const CamJob job = gCamJob;
  const int cam = gCamJobCam;
  const bool stale = gCamJobGen != gCamClipGen;  // For a clip that's no longer playing.
  gCamJob = CamJob::None;
  gCamJobCam = -1;
  const camera_net::Error e = camera_net::error();
  const unsigned status = camera_net::status();
  const bool cancelled = e == camera_net::Error::None && status == 0;
  if (e == camera_net::Error::NoMemory && job != CamJob::Frame && job != CamJob::Report) {
    static const char* const kWords[] = {"none", "ticket", "list", "thumb", "picture", "refresh", "livestart", "frame", "extend", "stop",
                                         "turn", "code", "report", "setup", "zoom", "pan", "autorefresh", "audio", "clips", "clip_thumb",
                                         "month", "clip_start", "clip_pause", "clip_seek"};
    const size_t at = size_t(job);
    camReportMemory(at < sizeof(kWords) / sizeof(kWords[0]) ? kWords[at] : "other");
  }
  const bool viaHome = camera_net::overHome();
  size_t length = 0;
  uint8_t* data = camera_net::take(length);
  const String word = e == camera_net::Error::None ? String() : camWord(data, length);
  const uint32_t mine = wifiLocalAddress();
  // The home way failed before any answer: another Wi-Fi, or a Mac that's away. HTTPS from now on,
  // on this network. A 404 there is a ticket the bridge forgot when it restarted: ask again once.
  if (viaHome && !status && (e == camera_net::Error::Connect || e == camera_net::Error::Timeout) && job != CamJob::Frame) {
    gCamHomeOffFor = mine;
    if (gCamLive) camEndLive("home door");
  } else if (viaHome && status == 404 && word == "not_found" && !gCamRetriedTicket) {
    gCamRetriedTicket = true;
    gCamTicketFor = 0;
  }
  switch (job) {
    case CamJob::Ticket: {
      podcast::HomeTicket ticket;
      if (e == camera_net::Error::None && data && cameras::parseTicket(reinterpret_cast<const char*>(data), length, ticket)) {
        gCamTicket = ticket;
        gCamTicketFor = mine;
      } else if (e == camera_net::Error::NotFound || e == camera_net::Error::None) {
        gCamTicket = podcast::HomeTicket{};  // An older bridge, or no home door: HTTPS on this network.
        gCamTicketFor = mine;
      } else {
        gCamHomeOffFor = mine;
        gCamProblem = camProblemText(e, word);
      }
      camInvalidate();
      break;
    }
    case CamJob::List: {
      if (e == camera_net::Error::None && data) {
        auto* fresh = new (std::nothrow) cameras::List();
        if (fresh && cameras::parseList(reinterpret_cast<const char*>(data), length, *fresh)) {
          // Thumbnails and the picture on screen follow their camera by its id.
          uint8_t* thumbs[cameras::kMax] = {};
          bool asked[cameras::kMax] = {}, none[cameras::kMax] = {};
          int jpegOf = -1, cur = -1;
          for (uint8_t i = 0; i < fresh->count; ++i) {
            for (uint8_t j = 0; gCamList && j < gCamList->count; ++j) {
              if (strcmp(fresh->cams[i].id, gCamList->cams[j].id)) continue;
              thumbs[i] = gCamThumb[j]; gCamThumb[j] = nullptr;
              const bool newer = fresh->cams[i].pictureAge >= 0 && gCamList->cams[j].pictureAge >= 0 &&
                                 fresh->cams[i].pictureAge + 5 < int32_t(gCamList->cams[j].pictureAge + (millis() - gCamListAtMs) / 1000);
              asked[i] = gCamThumbAsked[j] && !newer;
              none[i] = gCamThumbNone[j] && fresh->cams[i].pictureAge < 0;
              if (newer) { free(thumbs[i]); thumbs[i] = nullptr; }
              if (gCamJpegOf == j) jpegOf = i;
              if (gCamCur == j) cur = i;
            }
          }
          camFreeThumbs();
          for (uint8_t i = 0; i < cameras::kMax; ++i) { gCamThumb[i] = thumbs[i]; gCamThumbAsked[i] = asked[i]; gCamThumbNone[i] = none[i]; }
          gCamJpegOf = jpegOf;
          if (jpegOf < 0) camFreeJpeg();
          if (cur >= 0) gCamCur = uint8_t(cur);
          else if (gCamView == CamView::Picture || (gCamView == CamView::Live && !gCamClipMode)) {
            if (gCamLive) camEndLive("camera gone");
            gCamView = CamView::Wall;
          }
          delete gCamList;
          gCamList = fresh;
          gCamListOk = true;
          gCamListAtMs = millis();
          if (gCamSel >= cameras::tileCount(fresh->count)) gCamSel = 0;
          if (fresh->state == cameras::State::Ready) { gCamSetupUrl = ""; gCamSetupNone = false; }
          gCamProblem = "";
        } else {
          delete fresh;
          gCamProblem = fresh ? "Odd answer from the bridge" : "Not enough memory";
        }
      } else {
        gCamProblem = camProblemText(e, word);
      }
      camInvalidate();
      break;
    }
    case CamJob::Thumb:
      if (cam >= 0 && cam < camCount()) {
        if (e == camera_net::Error::None && data && length == cameras::kThumbBytes) { free(gCamThumb[cam]); gCamThumb[cam] = data; data = nullptr; }
        else if (e == camera_net::Error::NotFound) gCamThumbNone[cam] = true;
        else if (e == camera_net::Error::NoMemory && ++gCamThumbTries[cam] < 4) {
          gCamThumbAsked[cam] = false;  // Short of memory this once: asked again in a few seconds.
          gCamThumbNotBefore = millis() + 6000;
        }
        camInvalidate();
      }
      break;
    case CamJob::Picture:
      if (cam >= 0 && cam == gCamCur) {
        if (e == camera_net::Error::None && data && length) { camTakeJpeg(cam, data, length); data = nullptr; }
        else if (e == camera_net::Error::NotFound) { gCamPictureNone = true; camInvalidate(); }
        else camSay(camProblemText(e, word));
      }
      break;
    case CamJob::Refresh: {
      gCamRefreshSince = 0;
      if (e == camera_net::Error::None && data && length) {
        if (cam >= 0 && cam < camCount()) {
          gCamList->cams[cam].pictureAge = int32_t((millis() - gCamListAtMs) / 1000);
          gCamThumbAsked[cam] = false;
          gCamThumbNone[cam] = false;
          free(gCamThumb[cam]);
          gCamThumb[cam] = nullptr;
        }
        if (cam == gCamCur && gCamView == CamView::Picture) { camTakeJpeg(cam, data, length); data = nullptr; }
      } else {
        camSay(camProblemText(e, word));
      }
      camInvalidate();
      break;
    }
    case CamJob::LiveStart:
      if (!gCamLive || cam != gCamCur) break;
      if (e == camera_net::Error::None) { gCamLiveStarted = true; gCamLiveFailures = 0; }
      else { camSay(camProblemText(e, word)); gCamLive = false; gCamView = CamView::Picture; camInvalidate(); }
      break;
    case CamJob::Frame:
      if (!gCamLive || cam != gCamCur || stale || cancelled) break;
      if (gCamClipMode) camClipHeard();
      if (e == camera_net::Error::None && status == 200 && data && length) {
        // The 45 seconds count from the first frame, not from however long the camera took to wake.
        if (!gCamLiveFrames) gCamLiveEndsMs = millis() + cameras::kLiveSeconds * 1000;
        gCamLiveFrames++;
        gCamLiveBytes += length;
        gCamFpsFrames++;
        gCamLiveFailures = 0;
        camTakeJpeg(cam, data, length);
        data = nullptr;
        gCamWantAudio = camAudioOn();  // The sound since the last frame, before the next frame.
      } else if (e == camera_net::Error::Conflict) {
        const bool clip = gCamClipMode;
        gCamLiveStarted = false;
        camEndLive("ended on the Mac");
        camSay(clip ? "The clip stopped on the Mac" : "Live view ended");
      } else if (e != camera_net::Error::None) {
        if (e == camera_net::Error::NoMemory) camQueueReport("frame", camera_net::memoryNote(), unsigned(e));
        if (++gCamLiveFailures >= 3) { camEndLive("failures"); camSay(camProblemText(e, word)); }
      }
      break;
    case CamJob::Code: {
      const String answer = data ? String(reinterpret_cast<const char*>(data)) : String();
      if (e == camera_net::Error::None && answer.startsWith("ok")) camSay("Code sent", rgb565_local(88, 240, 141));
      else camSay(answer.startsWith("wrong") ? "That code didn't work" : camProblemText(e, word));
      gCamCode[0] = 0;
      gCamWantList = true;
      break;
    }
    case CamJob::Turn:
      if (e != camera_net::Error::None) camSay(word == "no_ptz" ? "This camera doesn't turn" : camProblemText(e, word));
      break;
    case CamJob::Zoom:
    case CamJob::Pan: {
      cameras::View view;
      if (!gCamLive || cam != gCamCur || stale) break;
      if (e == camera_net::Error::None && data && cameras::parseView(reinterpret_cast<const char*>(data), length, view)) {
        if (job == CamJob::Zoom && view.tenths != gCamZoomView.tenths) gCamZoomLitUntil = millis() + 1200;
        gCamZoomView = view;
        if (view.did == cameras::PanResult::Turned) gCamTurnLitUntil = millis() + kCamTurnLitMs;
        else if (view.did == cameras::PanResult::Edge) camSay(gCamZoomView.tenths > 10 ? "That's the edge" : "This camera doesn't turn");
      } else if (e != camera_net::Error::None) camSay(camProblemText(e, word));
      camInvalidate();
      break;
    }
    case CamJob::Audio:
      if (!gCamLive || cam != gCamCur || stale || cancelled) break;
      if (e == camera_net::Error::None && status == 200 && data && length >= 2) {
        if (!gCamAudioMuted) camAudioTake(data, length);
        gCamAudioEmpty = 0;
        if (!gCamAudioHeard) { gCamAudioHeard = true; camInvalidate(); }
      } else if (e == camera_net::Error::None && !gCamAudioHeard && ++gCamAudioEmpty >= kCamAudioGiveUp) {
        gCamAudioNone = true;  // No microphone, or one the bridge can't play: stop asking.
        camInvalidate();
      }
      break;
    case CamJob::AutoRefresh:
      gCamAutoCur = -1;
      if (e == camera_net::Error::None && data && length && cam >= 0 && cam < camCount()) {
        gCamList->cams[cam].pictureAge = int32_t((millis() - gCamListAtMs) / 1000);
        gCamThumbAsked[cam] = false;
        gCamThumbNone[cam] = false;
        free(gCamThumb[cam]);
        gCamThumb[cam] = nullptr;
        if (gCamView == CamView::Picture && cam == gCamCur && !gCamRefreshSince) { camTakeJpeg(cam, data, length); data = nullptr; }
      }
      camInvalidate();
      break;
    case CamJob::ClipStart: {
      if (!gCamClipMode || stale || cancelled) break;
      char camId[cameras::kIdBytes];
      uint32_t dur = 0;
      if (e == camera_net::Error::None && data && cameras::parseClipStart(reinterpret_cast<const char*>(data), length, camId, dur)) {
        snprintf(gCamClipCam, sizeof(gCamClipCam), "%s", camId);
        gCamLiveStarted = true;
        gCamLiveFailures = 0;
        gCamLiveBeganMs = millis();  // The frames' wait counts from here: getting it from the HomeBase took its own time.
        gCamClipState = cameras::ClipState{};
        gCamClipState.dur = dur;
        gCamClipState.playing = gCamClipState.known = true;
      } else {
        const String why = camProblemText(e, word);
        camEndLive("clip failed");
        camSay(why);
      }
      camInvalidate();
      break;
    }
    case CamJob::ClipPause:
    case CamJob::ClipSeek: {
      if (!gCamClipMode || stale || cancelled) break;
      cameras::ClipState state;
      if (e == camera_net::Error::None && data && cameras::parseClipState(reinterpret_cast<const char*>(data), state)) {
        if (gCamWantClipSeek < 0 && gCamWantClipPause < 0) gCamClipState = state;
        gCamFrameNotBefore = 0;
      } else if (e == camera_net::Error::Conflict) {
        gCamLiveStarted = false;
        camEndLive("ended on the Mac");
        camSay("The clip stopped on the Mac");
      } else if (e != camera_net::Error::None) {
        camSay(camProblemText(e, word));
      }
      camInvalidate();
      break;
    }
    case CamJob::ClipList: {
      if (!gCamClips || cancelled) { gCamWantClips = gCamClips && cancelled; break; }
      if (gCamClipsAsked != gCamClipsDay) break;  // Another day was picked meanwhile: it's asked next.
      gCamClipsLoading = false;
      camClipsFreeList();
      if (e == camera_net::Error::None && data) {
        const uint16_t lines = cameras::clipLines(reinterpret_cast<const char*>(data), length);
        gCamClipStore.clips = lines ? new (std::nothrow) cameras::Clip[lines] : nullptr;
        gCamClipStore.cap = gCamClipStore.clips ? lines : 0;
        if (lines && !gCamClipStore.clips) {
          camReportMemory("cliplist");
          gCamClipsProblem = String("Not enough memory (") + ESP.getFreeHeap() / 1024 + "K/" + heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024 + "K)";
          camInvalidate();
          break;
        }
      }
      if (e == camera_net::Error::None && data && cameras::parseClips(reinterpret_cast<const char*>(data), length, *gCamClips)) {
        // Asked without a day, the answer's is the Mac's today.
        if (!gCamClipsDay) gCamClipsDay = gCamClips->day;
        if (!gCamToday) gCamToday = gCamClips->day;
        camClipRowsBuild();
        gCamClipSelAtMs = millis();
        gCamClipThumbAskedMs = 0;
        gCamClipsProblem = "";
      } else {
        camClipsFreeList();
        gCamClipsProblem = e == camera_net::Error::None ? String("Odd answer from the bridge") : camProblemText(e, word);
      }
      camInvalidate();
      break;
    }
    case CamJob::ClipThumb:
      if (!gCamClips || cancelled) break;
      if (e == camera_net::Error::None && status == 200 && data && length == cameras::kThumbBytes) {
        camClipFreeThumb();
        gCamClipThumb = data;
        data = nullptr;
        snprintf(gCamClipThumbId, sizeof(gCamClipThumbId), "%s", gCamClipThumbAsking);
      } else if (e == camera_net::Error::NoMemory) {
        gCamClipThumbNotBefore = millis() + 5000;  // Short of memory this once: asked again later.
      } else if (!(e == camera_net::Error::None && status == 204)) {
        // 204 is the Mac still getting the clip: asked again in a moment. Anything else, there's none.
        snprintf(gCamClipThumbNone, sizeof(gCamClipThumbNone), "%s", gCamClipThumbAsking);
      }
      camInvalidate();
      break;
    case CamJob::MonthList: {
      if (cancelled) { gCamWantMonth = true; break; }
      cameras::Month month;
      if (e == camera_net::Error::None && data && cameras::parseMonth(reinterpret_cast<const char*>(data), length, month)) {
        if (month.ym == gCamMonthCursor / 100) { gCamMonth = month; gCamMonthProblem = ""; }
      } else if (gCamMonthAsked == gCamMonthCursor / 100) {
        gCamMonth = cameras::Month{};
        gCamMonth.ym = gCamMonthAsked;  // Shown without its days, and R asks again.
        gCamMonthProblem = e == camera_net::Error::None ? String("Odd answer from the bridge") : camProblemText(e, word);
      }
      camInvalidate();
      break;
    }
    case CamJob::Setup: {
      char path[32], url[200];
      uint32_t seconds = 0;
      if (e == camera_net::Error::None && data && cameras::parseSetup(reinterpret_cast<const char*>(data), length, path, sizeof(path), seconds) &&
          cameras::setupUrl(url, sizeof(url), gHubHost.c_str(), gHubPort, hubUsesTls(), path)) {
        gCamSetupUrl = url;
        // Asked again a minute before it ends, so the code on screen always works.
        gCamSetupUntilMs = millis() + (seconds > 120 ? seconds - 60 : seconds) * 1000;
        gCamSetupNone = false;
      } else {
        gCamSetupUrl = "";
        gCamSetupNone = e == camera_net::Error::None || e == camera_net::Error::NotFound;
      }
      camInvalidate();
      break;
    }
    default:
      break;
  }
  free(data);
}

// The sign-in page's QR code is for an account that isn't signed in, or one eufy refused.
bool camSignInShown() {
  if (!gCamList || !gCamListOk) return false;
  if (gCamList->state == cameras::State::Setup) return true;
  return gCamList->state == cameras::State::Error && strstr(gCamList->detail, "refused");
}
bool camWantsSetupLink() {
  return camSignInShown() && !gCamSetupNone && (gCamSetupUrl.isEmpty() || int32_t(millis() - gCamSetupUntilMs) >= 0);
}

bool camNextThumb();
bool camNextClipJob();
// The next thing to ask, most urgent first.
void camNextJob() {
  if (!gWifiReady || !wifiLocalAddress()) return;
  if (camTicketWanted()) {
    prepareNetworkMemory("cameras", true);
    compactMemoryForUi("cameras", true);
    if (camera_net::start(cameras::kTicketPath, false, 0, 0, 1024, 15000)) { gCamJob = CamJob::Ticket; gCamJobCam = -1; }
    return;
  }
  if (gCamReport[0]) {
    char path[sizeof(gCamReport)];
    strcpy(path, gCamReport);
    gCamReport[0] = 0;
    if (camStart(CamJob::Report, -1, path, 64, 10000)) return;
  }
  if (gCamWantStop) {
    char path[160];
    gCamWantStop = false;
    if (cameras::routePath(path, sizeof(path), "live/stop", gCamStopCam) && camStart(CamJob::Stop, -1, path, 64, 10000)) return;
  }
  if (gCamWantClipStart) {
    char path[120];
    gCamWantClipStart = false;
    snprintf(path, sizeof(path), "%sclips/start?day=%lu&id=%s", cameras::kBase, static_cast<unsigned long>(gCamClipNowDay), gCamClipNow.id);
    if (camStart(CamJob::ClipStart, gCamCur, path, 128, 70000)) return;
  }
  if (gCamWantCode) {
    char path[64];
    gCamWantCode = false;
    const bool captcha = gCamList && gCamList->state == cameras::State::Captcha;
    if ((captcha ? cameras::captchaPath(path, sizeof(path), gCamCode) : cameras::codePath(path, sizeof(path), gCamCode)) &&
        camStart(CamJob::Code, -1, path, 128, 30000)) return;
  }
  if (gCamLive && !gCamLiveStarted && !gCamClipMode) {
    if (camStartFor(CamJob::LiveStart, gCamCur, "live/start", 64, 30000)) return;
  }
  if (gCamClipMode && gCamLiveStarted && gCamWantClipPause >= 0) {
    char extra[8];
    snprintf(extra, sizeof(extra), "on=%d", gCamWantClipPause);
    gCamWantClipPause = -1;
    if (camStartLive(CamJob::ClipPause, "clips/pause", 128, 15000, extra)) return;
  }
  if (gCamClipMode && gCamLiveStarted && gCamWantClipSeek >= 0) {
    char extra[20];
    snprintf(extra, sizeof(extra), "ms=%ld", long(gCamWantClipSeek));
    gCamWantClipSeek = -1;
    if (camStartLive(CamJob::ClipSeek, "clips/seek", 128, 15000, extra)) return;
  }
  if (gCamLive && gCamWantExtend && !gCamClipMode) {
    gCamWantExtend = false;
    if (camStartFor(CamJob::Extend, gCamCur, "live/extend", 64, 10000)) return;
  }
  if (gCamLive && gCamLiveStarted && gCamWantZoom) {
    char extra[16];
    snprintf(extra, sizeof(extra), "x=%u", unsigned(gCamWantZoom));
    gCamWantZoom = 0;
    if (camStartLive(CamJob::Zoom, "live/zoom", 128, 15000, extra)) return;
  }
  if (gCamLive && gCamLiveStarted && gCamWantPan != cameras::Arrow::None) {
    char extra[24];
    snprintf(extra, sizeof(extra), "dir=%s%s", cameras::directionWord(gCamWantPan), gCamWantPanTurn ? "&turn=1" : "");
    gCamWantPan = cameras::Arrow::None;
    if (camStartLive(CamJob::Pan, "live/pan", 128, 15000, extra)) return;
  }
  if (gCamLive && gCamWantTurn != cameras::Arrow::None) {
    char extra[16];
    snprintf(extra, sizeof(extra), "dir=%s", cameras::directionWord(gCamWantTurn));
    gCamWantTurn = cameras::Arrow::None;
    if (camStartFor(CamJob::Turn, gCamCur, "turn", 64, 15000, extra)) return;
  }
  if (gCamWantAudio && camAudioOn()) {
    gCamWantAudio = false;
    if (camStartLive(CamJob::Audio, "live/audio", 32000, 5000)) return;
  }
  if (gCamLive && gCamLiveStarted && !(gCamFrameNotBefore && int32_t(millis() - gCamFrameNotBefore) < 0)) {
    gCamFrameNotBefore = 0;
    if (camStartLive(CamJob::Frame, "live/frame", cameras::kMaxJpeg, 10000)) return;
  }
  if (gCamWantRefresh >= 0) {
    const int cam = gCamWantRefresh;
    gCamWantRefresh = -1;
    if (camStartFor(CamJob::Refresh, cam, "refresh", cameras::kMaxJpeg, kCamRefreshWaitMs)) return;
    gCamRefreshSince = 0;
  }
  if (camWantsSetupLink() && millis() - gCamSetupAskedMs > 10000) {
    gCamSetupAskedMs = millis();
    prepareNetworkMemory("cameras", true);
    compactMemoryForUi("cameras", true);
    // HTTPS only: the home door doesn't hand out sign-in links.
    if (camera_net::start(cameras::kSetupPath, false, 0, 0, 512, 15000)) { gCamJob = CamJob::Setup; gCamJobCam = -1; return; }
  }
  const uint32_t listEvery = camReady() ? kCamListEveryMs : kCamSigningEveryMs;
  if (gCamWantList || !gCamListOk || (gCamView == CamView::Wall && millis() - gCamListAtMs > listEvery)) {
    gCamWantList = false;
    if (camStart(CamJob::List, -1, cameras::kListPath, cameras::kMaxList, 15000)) return;
  }
  if (!camReady()) return;
  if (gCamView == CamView::Picture && gCamWantPicture && gCamJpegOf != gCamCur && !gCamRefreshSince) {
    gCamWantPicture = false;
    if (camStartFor(CamJob::Picture, gCamCur, "picture", cameras::kMaxJpeg, 15000)) return;
  }
  if (camNextClipJob()) return;
  if (gCamView != CamView::Wall) return;
  // Thumbnails, the selected one's first, then the rest of its page.
  if (camNextThumb()) return;
  // Then, once the last pictures are up, a new one from each camera, the selected one first. Only at
  // home: away, each is a picture of up to 32 KB over HTTPS, and wakes a battery camera, for a wall
  // that's only glanced at. R still takes one.
  if (!camHomeIp()) return;
  if (!gCamAutoPlanned) {
    gCamAutoPlanned = true;
    for (uint8_t i = 0; i < camCount(); ++i) {
      const cameras::Camera& c = gCamList->cams[i];
      const int32_t age = c.pictureAge < 0 ? -1 : int32_t(c.pictureAge + (millis() - gCamListAtMs) / 1000);
      gCamAutoWanted[i] = !c.off && (age < 0 || age > 60);
    }
  }
  for (int pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < camCount(); ++i) {
      if (!gCamAutoWanted[i] || (pass == 0 && i != gCamSel)) continue;
      gCamAutoWanted[i] = false;
      if (camStartFor(CamJob::AutoRefresh, i, "refresh", cameras::kMaxJpeg, kCamRefreshWaitMs)) { gCamAutoCur = i; camInvalidate(); return; }
    }
  }
}
// The day's list, the month, and the selected clip's picture once the selection rests.
bool camNextClipJob() {
  char path[120];
  if (gCamView == CamView::Clips && gCamClips && gCamWantClips) {
    gCamWantClips = false;
    gCamClipsAsked = gCamClipsDay;
    int n = snprintf(path, sizeof(path), "%sclips?day=%lu", cameras::kBase, static_cast<unsigned long>(gCamClipsDay));
    if (!gCamClipsDay) n = snprintf(path, sizeof(path), "%sclips?today=1", cameras::kBase);
    if (gCamClipFilterId[0]) n += snprintf(path + n, sizeof(path) - n, "&cam=%s", gCamClipFilterId);
    if (gCamClipsQuick) snprintf(path + n, sizeof(path) - n, "%s", "&quick=1");
    if (camStart(CamJob::ClipList, -1, path, 12 * 1024, 30000)) return true;
    gCamWantClips = true;
    return false;
  }
  if (gCamView == CamView::Month && gCamWantMonth && gCamMonthCursor) {
    gCamWantMonth = false;
    gCamMonthAsked = gCamMonthCursor / 100;
    snprintf(path, sizeof(path), "%sclips/month?ym=%lu", cameras::kBase, static_cast<unsigned long>(gCamMonthAsked));
    if (camStart(CamJob::MonthList, -1, path, 1024, 45000)) return true;
    gCamWantMonth = true;
    return false;
  }
  if (gCamView != CamView::Clips || gCamClipsLoading) return false;
  const cameras::Clip* clip = camClipAtRow(gCamClipSel);
  const uint32_t now = millis();
  if (!clip || !strcmp(gCamClipThumbId, clip->id) || !strcmp(gCamClipThumbNone, clip->id)) return false;
  if (now - gCamClipSelAtMs < 500 || (gCamClipThumbAskedMs && now - gCamClipThumbAskedMs < 1500)) return false;
  if (gCamClipThumbNotBefore && int32_t(now - gCamClipThumbNotBefore) < 0) return false;
  gCamClipThumbAskedMs = now;
  snprintf(gCamClipThumbAsking, sizeof(gCamClipThumbAsking), "%s", clip->id);
  snprintf(path, sizeof(path), "%sclips/thumb?day=%lu&id=%s", cameras::kBase, static_cast<unsigned long>(gCamClipsDay), clip->id);
  return camStart(CamJob::ClipThumb, -1, path, cameras::kThumbBytes, 30000);
}
bool camNextThumb() {
  if (gCamThumbNotBefore && int32_t(millis() - gCamThumbNotBefore) < 0) return false;
  const uint8_t page = cameras::pageOf(gCamSel);
  for (int pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < camCount(); ++i) {
      if (pass == 0 && i != gCamSel) continue;
      if (pass == 1 && cameras::pageOf(i) != page) continue;
      if (gCamThumb[i] || gCamThumbAsked[i] || gCamThumbNone[i] || gCamList->cams[i].pictureAge < 0) continue;
      gCamThumbAsked[i] = true;
      if (camStartFor(CamJob::Thumb, i, "thumb", cameras::kThumbBytes, 15000)) return true;
    }
  }
  return false;
}

// --- The app's life ------------------------------------------------------------------------
void beginCameras() {
  gCamView = CamView::Wall;
  if (!gCamList) gCamList = new (std::nothrow) cameras::List();
  gCamListOk = false;
  gCamListAtMs = millis();
  gCamWantList = true;
  gCamWantPicture = gCamWantStop = gCamWantExtend = gCamWantCode = false;
  gCamWantRefresh = -1;
  gCamWantTurn = cameras::Arrow::None;
  gCamLive = gCamLiveStarted = false;
  camCloseClips();
  gCamClipMode = gCamWantClipStart = false;
  gCamWantClipPause = -1;
  gCamWantClipSeek = -1;
  gCamFrameNotBefore = 0;
  gCamRefreshSince = 0;
  gCamJob = CamJob::None;
  gCamCode[0] = 0;
  gCamReport[0] = 0;
  gCamAutoPlanned = false;
  gCamAutoCur = -1;
  for (bool& wanted : gCamAutoWanted) wanted = false;
  gCamNotice = "";
  gCamNoticeUntil = 0;
  gCamProblem = "";
  gCamRetriedTicket = false;
  camera_net::setBridge(gHubHost.c_str(), gHubPort, gDeviceToken.c_str(), kTlsRootCaPem);
  gCamOpenSince = millis();
  if (!gCamList) gCamProblem = "Not enough memory for cameras";
  else camera_net::open();  // Or once the last visit's worker has gone: serviceCameras tries again.
  camInvalidate();
}
void stopCameras() {
  if (gCamLive && gCamLiveStarted && camera_net::opened()) {
    // Tell the Mac at once rather than leave the camera to its idle stop.
    camera_net::cancel();
  }
  gCamLive = gCamLiveStarted = false;
  gCamClipMode = gCamWantClipStart = false;
  camAudioFree();
  camera_net::close();
  camFreeThumbs();
  camFreeJpeg();
  camCloseClips();
  delete gCamList;
  gCamList = nullptr;
  gCamListOk = false;
  gCamView = CamView::Wall;
  gCamJob = CamJob::None;
}

void serviceCameras() {
  if (gUiMode != UiMode::Cameras) return;
  if (!camera_net::opened()) {
    // A worker still leaving from last time: start one once it has. Still none after a few
    // seconds, and it's memory.
    static uint32_t lastTry = 0;
    if (gCamList && millis() - lastTry > 300) {
      lastTry = millis();
      if (camera_net::open()) { gCamProblem = ""; camInvalidate(); }
      else if (millis() - gCamOpenSince > 3000 && gCamProblem.isEmpty()) {
        gCamProblem = String("Not enough memory for cameras (") + camera_net::memoryNote() + ")";
        camInvalidate();
      }
    }
    return;
  }
  if (camera_net::finished()) camFinished();
  if (!camera_net::busy() && !camera_net::finished()) camNextJob();
  camAudioFeed();
  const uint32_t now = millis();
  if (gCamLive) {
    // The screen stays on while watching, as a video player's does; a paused or ended clip lets it sleep.
    if (!gCamClipMode || (gCamClipState.playing && !gCamClipState.ended)) gLastUserActivityMs = now;
    if (gCamClipMode) {
      // No countdown: a clip is as long as it is. Started, it should show a frame soon.
      if (gCamLiveStarted && !gCamLiveFrames && now - gCamLiveBeganMs > 20000) {
        camEndLive("no frames");
        camSay("The clip didn't play");
      }
    } else if (gCamLiveStarted && gCamLiveFrames && int32_t(now - gCamLiveEndsMs) >= 0) {
      camEndLive("time");
      camSay("Live ended to save the battery");
    } else if (!gCamLiveFrames && now - gCamLiveBeganMs > 30000) {
      camEndLive("no frames");
      camSay("The camera didn't answer");
    }
    if (now - gCamFpsWindowMs >= 2000) {
      gCamFps = gCamFpsFrames * 1000.0f / float(now - gCamFpsWindowMs);
      gCamFpsWindowMs = now;
      gCamFpsFrames = 0;
    }
  }
  // Once a second: the live clock, the refresh's seconds, bars and notices running out.
  const uint32_t second = now / 1000;
  if (second != gCamShownSecond) {
    gCamShownSecond = second;
    if (gCamLive || gCamRefreshSince) camInvalidate();
  }
  if (gCamBarsUntil && int32_t(now - gCamBarsUntil) >= 0) { gCamBarsUntil = 0; camInvalidate(); }
  if (gCamNoticeUntil && int32_t(now - gCamNoticeUntil) >= 0) { gCamNoticeUntil = 0; camInvalidate(); }
  if (gCamTurnLitUntil && int32_t(now - gCamTurnLitUntil) >= 0) { gCamTurnLitUntil = 0; camInvalidate(); }
  if (gCamClipHintsUntil && int32_t(now - gCamClipHintsUntil) >= 0) { gCamClipHintsUntil = 0; camInvalidate(); }
  // The list's "Looking..." counts seconds while the HomeBase answers.
  if ((gCamView == CamView::Clips && gCamClipsLoading) || (gCamView == CamView::Month && !gCamMonth.ym)) {
    static uint32_t shown = 0;
    if (second != shown) { shown = second; camInvalidate(); }
  }
}

// Live steps back to its picture, the picture to the wall; the wall's Esc leaves the app. A clip
// steps back to its day's list, the month to the list, and the list to where C opened it.
bool camerasEscape() {
  if (gCamView == CamView::Live) { camEndLive("closed"); return true; }
  if (gCamView == CamView::Month) { gCamView = CamView::Clips; camInvalidate(); return true; }
  if (gCamView == CamView::Clips) {
    const int back = gCamClipsBack == CamView::Picture ? camIndexOfId(gCamClipsBackId) : -1;
    camCloseClips();
    gCamView = CamView::Wall;
    if (back >= 0) camOpenPicture(back);
    camInvalidate();
    return true;
  }
  if (gCamView == CamView::Picture) { gCamView = CamView::Wall; gCamRefreshSince = 0; camInvalidate(); return true; }
  if (gCamCode[0]) { gCamCode[0] = 0; camInvalidate(); return true; }
  return false;
}

bool handleCamerasKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt) return false;
  const bool enter = keys.enter || key == '\n' || key == '\r';
  const bool del = keys.del || key == '\b' || key == 127;
  const cameras::Arrow arrow = cameras::arrowFor(key);
  const char lower = char(tolower(static_cast<unsigned char>(key)));
  if (gCamView == CamView::Wall) {
    if (gCamList && gCamListOk && (gCamList->state == cameras::State::Code || gCamList->state == cameras::State::Captcha)) {
      // A code is digits; a captcha's answer letters too, as its picture on the Mac shows them.
      const bool captcha = gCamList->state == cameras::State::Captcha;
      const size_t n = strlen(gCamCode);
      const bool fits = (key >= '0' && key <= '9') || (captcha && isalpha(static_cast<unsigned char>(key)));
      if (fits && n < 8) { gCamCode[n] = key; gCamCode[n + 1] = 0; camInvalidate(); }
      else if (del && n) { gCamCode[n - 1] = 0; camInvalidate(); }
      else if (enter) {
        if (n < (captcha ? 3u : 4u)) camSay(captcha ? "Type the captcha's letters" : "Type the code eufy sent");
        else { gCamWantCode = true; camSay("Sending the code..."); }
      }
      return true;
    }
    if (!camReady()) {
      if (lower == 'r' || enter) { gCamWantList = true; gCamProblem = ""; camSay("Asking again..."); }
      return true;
    }
    const uint8_t tiles = cameras::tileCount(camCount());
    if (arrow != cameras::Arrow::None) {
      const int dx = arrow == cameras::Arrow::Left ? -1 : arrow == cameras::Arrow::Right ? 1 : 0;
      const int dy = arrow == cameras::Arrow::Up ? -1 : arrow == cameras::Arrow::Down ? 1 : 0;
      gCamSel = cameras::move(gCamSel, camCount(), dx, dy);
      camInvalidate();
      return true;
    }
    const bool onCamera = gCamSel < camCount();
    if (enter) { if (onCamera) camOpenPicture(gCamSel); else camSay(gCamList->mode[0] ? "The HomeBase's mode is set in the eufy app" : "The HomeBase"); return true; }
    if (lower == 'l' && onCamera) { camOpenLive(gCamSel); return true; }
    if (lower == 'r' && onCamera) { camOpenPicture(gCamSel); camRefresh(gCamSel); return true; }
    if (lower == 'c') { camOpenClips("", CamView::Wall); return true; }
    (void)tiles;
    return true;
  }
  if (gCamView == CamView::Picture) {
    gCamBarsUntil = millis() + kCamBarsMs;
    camInvalidate();
    if (arrow == cameras::Arrow::Left || arrow == cameras::Arrow::Right) {
      if (!camCount()) return true;
      const int step = arrow == cameras::Arrow::Left ? -1 : 1;
      const int next = (int(gCamCur) + step + camCount()) % camCount();
      gCamSel = uint8_t(next);
      gCamRefreshSince = 0;
      camOpenPicture(next);
      return true;
    }
    if (enter || lower == 'l') { camOpenLive(gCamCur); return true; }
    if (lower == 'r') { camRefresh(gCamCur); return true; }
    if (lower == 's') { camSave(); return true; }
    if (lower == 'c') { const cameras::Camera* c = camAt(gCamCur); if (c) { gCamRefreshSince = 0; camOpenClips(c->id, CamView::Picture); } return true; }
    return true;
  }
  // The day's clips, newest first: up and down pick one, left and right go a day back or on.
  if (gCamView == CamView::Clips) {
    if (arrow == cameras::Arrow::Up || arrow == cameras::Arrow::Down) {
      const int step = (arrow == cameras::Arrow::Up ? -1 : 1) * (keys.fn ? kCamClipRows : 1);
      camClipSelect(gCamClipSel + step);
      return true;
    }
    if (arrow == cameras::Arrow::Left || arrow == cameras::Arrow::Right) { camClipsStepDay(arrow == cameras::Arrow::Left ? -1 : 1); return true; }
    if (enter) { if (camClipAtRow(gCamClipSel)) camPlayClip(gCamClipSel); return true; }
    if (lower == 'd') { camOpenMonth(); return true; }
    if (lower == 'f') { camClipsNextFilter(); return true; }
    if (lower == 't') { if (gCamToday && gCamClipsDay != gCamToday) camClipsLoadDay(gCamToday); return true; }
    if (lower == 'r') { camClipsLoadDay(gCamClipsDay); return true; }
    return true;
  }
  // The month: the arrows move the day, - and = the month, Enter opens the day's list.
  if (gCamView == CamView::Month) {
    if (arrow == cameras::Arrow::Up) camMonthMove(-7);
    else if (arrow == cameras::Arrow::Down) camMonthMove(7);
    else if (arrow == cameras::Arrow::Left) camMonthMove(-1);
    else if (arrow == cameras::Arrow::Right) camMonthMove(1);
    else if (key == '-' || key == '_') camMonthStep(-1);
    else if (key == '=' || key == '+') camMonthStep(1);
    else if (enter) {
      gCamView = CamView::Clips;
      if (gCamMonthCursor != gCamClipsDay) camClipsLoadDay(gCamMonthCursor);
      camInvalidate();
    } else if (lower == 'r') {
      gCamMonth = cameras::Month{};
      gCamMonthProblem = "";
      gCamWantMonth = true;
      camInvalidate();
    }
    return true;
  }
  // Live. = and - zoom; the arrows move around the zoomed picture, and at its edge, or with Fn,
  // turn a camera that pans and tilts. The bridge says which happened.
  const cameras::Camera* c = camAt(gCamCur);
  // A clip: Space or Enter plays and pauses, left and right go 10 seconds, up and down to the clip
  // above or below in the list. Zoomed in, the arrows move around instead, and with Fn they don't.
  if (gCamClipMode) {
    gCamClipHintsUntil = millis() + kCamBarsMs;
    camInvalidate();
    if (key == ' ' || enter) { camClipPlayPause(); return true; }
    if (arrow != cameras::Arrow::None && (gCamZoomView.tenths <= 10 || keys.fn)) {
      if (arrow == cameras::Arrow::Left || arrow == cameras::Arrow::Right) { camClipSeekBy(arrow == cameras::Arrow::Left ? -10000 : 10000); return true; }
      const int row = gCamClipSel + (arrow == cameras::Arrow::Up ? -1 : 1);
      if (camClipAtRow(row)) camPlayClip(row);
      else camSay(arrow == cameras::Arrow::Up ? "That's the newest clip" : "That's the day's first clip", camMuted());
      return true;
    }
    if (arrow != cameras::Arrow::None) { gCamWantPan = arrow; gCamWantPanTurn = false; return true; }
    if (lower == 'l') return true;
  }
  if (key == '=' || key == '+' || key == '-' || key == '_') {
    if ((!c && !gCamClipMode) || !gCamLiveStarted) return true;
    const bool lens = !gCamClipMode && c && c->lens;  // A recording has the wide lens's picture only.
    const uint8_t from = gCamWantZoom ? gCamWantZoom : uint8_t(gCamZoomView.tenths);
    const uint8_t to = cameras::zoomStep(lens, from, key == '=' || key == '+' ? 1 : -1);
    if (to == from) camSay(to > 10 ? "That's as close as it goes" : "That's all of it");
    else gCamWantZoom = to;
    return true;
  }
  if (arrow != cameras::Arrow::None) {
    if (!c) return true;
    if (gCamZoomView.tenths <= 10 && !c->ptz) { camSay("This camera doesn't turn: zoom in with ="); return true; }
    gCamWantPan = arrow;
    gCamWantPanTurn = keys.fn;
    return true;
  }
  if (enter) {
    gCamLiveEndsMs = millis() + cameras::kLiveSeconds * 1000;
    gCamWantExtend = true;
    camInvalidate();
    return true;
  }
  if (lower == 's') { camSave(); return true; }
  if (lower == 'm') {
    gCamAudioMuted = !gCamAudioMuted;
    if (gCamAudioMuted) camAudioStop();
    camSay(gCamAudioMuted ? "Sound off" : gCamAudioNone ? "This camera has no sound" : "Sound on", camMuted());
    camInvalidate();
    return true;
  }
  if (lower == 'l') { camEndLive("closed"); return true; }
  return true;
}

// --- Drawing -------------------------------------------------------------------------------
// While a picture or live frame is on screen and nothing of the menu's is over it, it's drawn
// straight to the screen and the canvas isn't pushed.
bool cameraDrawsDirect() {
  return gUiMode == UiMode::Cameras && !gLauncherVisible && (gCamView == CamView::Picture || gCamView == CamView::Live) && gCamJpeg && gCamJpegOf == gCamCur &&
         !gChatCommandsVisible && !gVoiceNoticeVisible && !gScreensaverActive;
}

void camDrawTurnIcon(lgfx::LGFXBase& d, int16_t cx, int16_t cy, bool lit) {
  const uint16_t ink = lit ? camAccent() : TFT_WHITE;
  d.fillCircle(cx, cy, 9, TFT_BLACK);
  d.drawCircle(cx, cy, 9, lit ? camAccent() : camMuted());
  d.fillTriangle(cx, cy - 7, cx - 3, cy - 3, cx + 3, cy - 3, ink);
  d.fillTriangle(cx, cy + 7, cx - 3, cy + 3, cx + 3, cy + 3, ink);
  d.fillTriangle(cx - 7, cy, cx - 3, cy - 3, cx - 3, cy + 3, ink);
  d.fillTriangle(cx + 7, cy, cx + 3, cy - 3, cx + 3, cy + 3, ink);
  d.fillCircle(cx, cy, 1, ink);
}
// A small ◀ or ▶ for the hints, since the font's arrows don't print.
int16_t camHintArrow(lgfx::LGFXBase& d, int16_t x, int16_t y, bool left, uint16_t ink) {
  if (left) d.fillTriangle(x, y + 3, x + 4, y, x + 4, y + 6, ink);
  else d.fillTriangle(x + 4, y + 3, x, y, x, y + 6, ink);
  return int16_t(x + 6);
}
int16_t camHintText(lgfx::LGFXBase& d, int16_t x, int16_t y, const char* text, uint16_t ink) {
  d.setTextColor(ink);
  d.setCursor(x, y);
  d.print(text);
  return int16_t(x + d.textWidth(text));
}

void camDrawPictureBars(lgfx::LGFXBase& d) {
  const cameras::Camera* c = camAt(gCamCur);
  if (!c) return;
  d.fillRect(0, 0, 240, kCamTopBar, TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  d.setCursor(4, 0);
  d.print(camFit(d, c->name, 92));
  char when[40];
  struct tm t = {};
  const time_t now = time(nullptr);
  int minute = -1, weekday = -1;
  if (gTimeValid && localtime_r(&now, &t)) { minute = t.tm_hour * 60 + t.tm_min; weekday = t.tm_wday; }
  const int32_t age = c->pictureAge < 0 ? -1 : int32_t(c->pictureAge + (millis() - gCamListAtMs) / 1000);
  cameras::pictureWhen(when, sizeof(when), age, minute, weekday);
  char battery[8] = "";
  if (c->battery >= 0) snprintf(battery, sizeof(battery), "%d%%", int(c->battery));
  d.setFont(&fonts::Font0);
  d.setTextColor(camMuted());
  const int16_t batteryW = d.textWidth(battery);
  d.setCursor(236 - batteryW, 4);
  d.print(battery);
  d.setCursor(236 - batteryW - 8 - d.textWidth(when), 4);
  d.print(when);
  d.fillRect(0, 135 - kCamBottomBar, 240, kCamBottomBar, TFT_BLACK);
  const int16_t y = 135 - kCamBottomBar + 3;
  int16_t x = 4;
  x = camHintText(d, x, y, "Enter", TFT_WHITE); x = camHintText(d, x, y, " live  ", camMuted());
  x = camHintArrow(d, x, y, true, TFT_WHITE); x = camHintArrow(d, int16_t(x + 1), y, false, TFT_WHITE);
  x = camHintText(d, x, y, " camera  ", camMuted());
  x = camHintText(d, x, y, "R", TFT_WHITE); x = camHintText(d, x, y, " new  ", camMuted());
  x = camHintText(d, x, y, "C", TFT_WHITE); camHintText(d, x, y, " clips", camMuted());
}

void camDrawLiveBar(lgfx::LGFXBase& d) {
  const cameras::Camera* c = camAt(gCamCur);
  const uint32_t now = millis();
  const uint32_t left = int32_t(gCamLiveEndsMs - now) > 0 ? (gCamLiveEndsMs - now + 999) / 1000 : 0;
  char clock[8];
  cameras::countdown(clock, sizeof(clock), gCamLiveFrames ? left : cameras::kLiveSeconds);
  const bool asking = gCamLiveFrames && left <= cameras::kAskSeconds;
  d.fillRect(0, 0, 240, kCamTopBar, asking ? camAmber() : TFT_BLACK);
  d.setFont(&fonts::Font2);
  if (asking) {
    d.setTextColor(TFT_BLACK);
    d.setCursor(4, 0);
    d.print("Still watching? Enter: 45 s more");
    d.setCursor(236 - d.textWidth(clock), 0);
    d.print(clock);
    return;
  }
  d.fillCircle(8, 8, 4, camRed());
  d.setTextColor(TFT_WHITE);
  d.setCursor(16, 0);
  d.print("LIVE");
  char right[24], zoom[8] = "";
  if (!gCamLiveStarted || !gCamLiveFrames) snprintf(right, sizeof(right), "%s", "Connecting...");
  else snprintf(right, sizeof(right), "%s  %dfps", clock, int(gCamFps + 0.5f));
  if (gCamZoomView.tenths > 10) cameras::zoomText(zoom, sizeof(zoom), gCamZoomView.tenths);
  const int16_t rightW = d.textWidth(right), zoomW = zoom[0] ? d.textWidth(zoom) + 8 : 0;
  d.setTextColor(camMuted());
  d.setCursor(52, 0);
  d.print(camFit(d, c ? c->name : "", 236 - rightW - zoomW - 52 - 6));
  if (zoom[0]) {
    const bool lit = gCamZoomLitUntil && int32_t(millis() - gCamZoomLitUntil) < 0;
    d.setTextColor(lit ? camAccent() : camAmber());
    d.setCursor(236 - rightW - zoomW, 0);
    d.print(zoom);
  }
  d.setTextColor(TFT_WHITE);
  d.setCursor(236 - rightW, 0);
  d.print(right);
}

// Sound on or off, in a circle like the turn icon beside it.
void camDrawSoundIcon(lgfx::LGFXBase& d, int16_t cx, int16_t cy, bool muted) {
  const uint16_t ink = muted ? camMuted() : TFT_WHITE;
  d.fillCircle(cx, cy, 9, TFT_BLACK);
  d.drawCircle(cx, cy, 9, camMuted());
  d.fillRect(cx - 5, cy - 2, 3, 5, ink);
  d.fillTriangle(cx - 3, cy, cx + 1, cy - 5, cx + 1, cy + 5, ink);
  if (muted) d.drawLine(cx - 6, cy + 6, cx + 6, cy - 6, camRed());
  else { d.drawLine(cx + 3, cy - 2, cx + 3, cy + 2, ink); d.drawLine(cx + 5, cy - 4, cx + 5, cy + 4, ink); }
}

// Zoomed in, a little map in the bottom corner: the whole picture, and where the view is in it.
void camDrawZoomMap(lgfx::LGFXBase& d, int16_t bottom) {
  constexpr int16_t kX = 4, kW = 39, kH = 22;
  const int16_t kY = int16_t(bottom - 4 - kH);
  const cameras::View& v = gCamZoomView;
  d.fillRect(kX, kY, kW, kH, TFT_BLACK);
  d.drawRect(kX, kY, kW, kH, camMuted());
  const int16_t x0 = int16_t(kX + 1 + (kW - 2) * v.x0 / 1000), y0 = int16_t(kY + 1 + (kH - 2) * v.y0 / 1000);
  const int16_t x1 = int16_t(kX + 1 + (kW - 2) * v.x1 / 1000), y1 = int16_t(kY + 1 + (kH - 2) * v.y1 / 1000);
  d.fillRect(x0, y0, max<int16_t>(2, int16_t(x1 - x0)), max<int16_t>(2, int16_t(y1 - y0)), camAccent());
}

// The refresh's box over the picture: "Waking Porch... 4s".
void camDrawWaking(lgfx::LGFXBase& d) {
  const cameras::Camera* c = camAt(gCamCur);
  if (!c || !gCamRefreshSince) return;
  char text[64];
  snprintf(text, sizeof(text), "Waking %s... %us", c->name, unsigned((millis() - gCamRefreshSince) / 1000));
  d.setFont(&fonts::Font2);
  const int16_t w = int16_t(min<int>(220, d.textWidth(text) + 16));
  const int16_t x = int16_t((240 - w) / 2), y = 56;
  d.fillRoundRect(x, y, w, 24, 5, TFT_BLACK);
  d.drawRoundRect(x, y, w, 24, 5, camAccent());
  d.setTextColor(TFT_WHITE);
  d.setCursor(x + 8, y + 4);
  d.print(camFit(d, text, w - 16));
}
void camDrawNoticeDirect(lgfx::LGFXBase& d) {
  if (!camNoticeShowing()) return;
  d.setFont(&fonts::Font2);
  const String text = camFit(d, gCamNotice, 220);
  const int16_t w = int16_t(d.textWidth(text) + 16), x = int16_t((240 - w) / 2), y = 92;
  d.fillRoundRect(x, y, w, 22, 5, TFT_BLACK);
  d.drawRoundRect(x, y, w, 22, 5, gCamNoticeInk);
  d.setTextColor(gCamNoticeInk);
  d.setCursor(x + 8, y + 3);
  d.print(text);
}

// A clip's top bar: played or paused, its camera and the time it was recorded at where it is, and
// how far in it is.
void camDrawClipBar(lgfx::LGFXBase& d) {
  const cameras::ClipState& st = gCamClipState;
  d.fillRect(0, 0, 240, kCamTopBar, TFT_BLACK);
  if (st.ended) d.fillRect(5, 4, 8, 8, camMuted());
  else if (st.playing) d.fillTriangle(5, 3, 5, 12, 13, 7, TFT_WHITE);
  else { d.fillRect(5, 3, 3, 10, camAmber()); d.fillRect(10, 3, 3, 10, camAmber()); }
  char pos[10], dur[10], right[24], when[8], zoom[8] = "";
  cameras::lengthText(pos, sizeof(pos), st.pos / 1000);
  cameras::lengthText(dur, sizeof(dur), (st.dur + 999) / 1000);
  if (st.known) snprintf(right, sizeof(right), "%s / %s", pos, dur);
  else snprintf(right, sizeof(right), "%s", "Getting it...");
  cameras::clockOfDay(when, sizeof(when), gCamClipNow.start + st.pos / 1000);
  if (gCamZoomView.tenths > 10) cameras::zoomText(zoom, sizeof(zoom), gCamZoomView.tenths);
  d.setFont(&fonts::Font2);
  const int16_t rightW = d.textWidth(right), zoomW = zoom[0] ? d.textWidth(zoom) + 8 : 0, whenW = d.textWidth(when) + 6;
  d.setTextColor(TFT_WHITE);
  d.setCursor(19, 0);
  const String name = camFit(d, camNameOfId(gCamClipCam), 236 - rightW - zoomW - whenW - 19 - 6);
  d.print(name);
  d.setTextColor(camMuted());
  d.setCursor(19 + d.textWidth(name) + 6, 0);
  d.print(when);
  if (zoom[0]) {
    const bool lit = gCamZoomLitUntil && int32_t(millis() - gCamZoomLitUntil) < 0;
    d.setTextColor(lit ? camAccent() : camAmber());
    d.setCursor(236 - rightW - zoomW, 0);
    d.print(zoom);
  }
  d.setTextColor(TFT_WHITE);
  d.setCursor(236 - rightW, 0);
  d.print(right);
}
bool camClipHintsShown() {
  return gCamClipMode && (!gCamClipState.playing || gCamClipState.ended || (gCamClipHintsUntil && int32_t(millis() - gCamClipHintsUntil) < 0));
}
// The keys, over the bottom of the picture while paused, ended, or just after a key.
void camDrawClipHints(lgfx::LGFXBase& d, int16_t y0) {
  d.fillRect(0, y0, 240, kCamBottomBar, TFT_BLACK);
  d.setFont(&fonts::Font0);
  const int16_t y = int16_t(y0 + 3);
  int16_t x = 4;
  if (gCamClipState.ended) {
    x = camHintText(d, x, y, "Enter", TFT_WHITE); x = camHintText(d, x, y, " again  ", camMuted());
  } else {
    x = camHintText(d, x, y, "Space", TFT_WHITE); x = camHintText(d, x, y, gCamClipState.playing ? " pause  " : " play  ", camMuted());
    if (gCamZoomView.tenths > 10) x = camHintText(d, x, y, "Fn", TFT_WHITE);
    x = camHintArrow(d, x, y, true, TFT_WHITE); x = camHintArrow(d, int16_t(x + 1), y, false, TFT_WHITE);
    x = camHintText(d, x, y, " 10s  ", camMuted());
  }
  x = camHintText(d, x, y, ";.", TFT_WHITE); x = camHintText(d, x, y, gCamClipState.ended ? " next clip  " : " clip  ", camMuted());
  // Zoomed in, "Fn" takes the room "Esc list" had.
  if (gCamClipState.ended || gCamZoomView.tenths <= 10) { x = camHintText(d, x, y, "Esc", TFT_WHITE); camHintText(d, x, y, " list", camMuted()); }
}
// How far in: a thin line along the bottom.
void camDrawClipProgress(lgfx::LGFXBase& d) {
  const uint32_t dur = gCamClipState.dur;
  const int16_t w = dur ? int16_t(min<uint32_t>(240, uint64_t(240) * gCamClipState.pos / dur)) : 0;
  d.fillRect(0, 133, 240, 2, camPanel());
  if (w) d.fillRect(0, 133, w, 2, gCamClipState.ended ? camMuted() : camAccent());
}

void camDrawDirect() {
  auto& d = M5Cardputer.Display;
  const bool live = gCamView == CamView::Live;
  const bool clip = live && gCamClipMode;
  const bool hints = clip && camClipHintsShown();
  const bool bars = !live && (gCamBarsUntil || gCamRefreshSince);
  const int16_t top = live || bars ? kCamTopBar : 0;
  const int16_t bottom = bars ? int16_t(135 - kCamBottomBar) : clip ? int16_t(hints ? 133 - kCamBottomBar : 133) : 135;
  d.startWrite();
  // The bars keep their strips: the picture is drawn only between them, so they don't flicker.
  d.setClipRect(0, top, 240, bottom - top);
  const bool ok = d.drawJpg(gCamJpeg, gCamJpegLen, 0, 0, 240, 135);
  d.clearClipRect();
  if (!ok) {
    d.fillRect(0, top, 240, bottom - top, TFT_BLACK);
    d.setFont(&fonts::Font2);
    d.setTextColor(camMuted());
    d.setCursor(40, 60);
    d.print("Couldn't show that picture");
  }
  if (clip) {
    camDrawClipBar(d);
    if (hints) camDrawClipHints(d, bottom);
    camDrawClipProgress(d);
  } else if (live) camDrawLiveBar(d);
  else if (bars) camDrawPictureBars(d);
  if (live) {
    const cameras::Camera* c = camAt(gCamCur);
    const bool turns = !clip && c && c->ptz;  // A recording doesn't turn the camera.
    const int16_t cy = int16_t(bottom - 14);
    if (turns) camDrawTurnIcon(d, 226, cy, gCamTurnLitUntil && int32_t(millis() - gCamTurnLitUntil) < 0);
    if (gCamAudioHeard || gCamAudioMuted) camDrawSoundIcon(d, turns ? 203 : 226, cy, gCamAudioMuted);
    if (gCamZoomView.tenths > 10) camDrawZoomMap(d, bottom);
  }
  if (!live) camDrawWaking(d);
  camDrawNoticeDirect(d);
  d.endWrite();
}

// --- The canvas screens: the wall, and the views while they have no picture ---------------
void camDrawCenter(const String& line1, const String& line2, uint16_t ink, int16_t y = 56) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font2);
  d.setTextColor(ink);
  const String a = fitCurrentFontToWidth(line1, 224);
  d.setCursor((240 - d.textWidth(a)) / 2, y);
  d.print(a);
  if (line2.isEmpty()) return;
  d.setTextColor(camMuted());
  const String b = fitCurrentFontToWidth(line2, 224);
  d.setCursor((240 - d.textWidth(b)) / 2, y + 20);
  d.print(b);
}

void camDrawTile(uint8_t tile) {
  auto& d = gCanvas;
  const int16_t x = cameras::tileX(tile), y = cameras::tileY(tile);
  const bool selected = tile == gCamSel;
  const uint16_t edge = selected ? camAccent() : rgb565_local(73, 73, 109);
  d.setFont(&fonts::Font0);
  if (tile >= camCount()) {
    // The HomeBase's tile.
    d.fillRoundRect(x, y, cameras::kThumbW, cameras::kThumbH, 4, camPanel());
    if (selected) d.drawRoundRect(x, y, cameras::kThumbW, cameras::kThumbH, 4, edge);
    d.setTextColor(TFT_WHITE);
    d.setCursor(x + 6, y + 12);
    d.print("HomeBase");
    d.setTextColor(camAccent());
    d.setCursor(x + 6, y + 26);
    d.print(gCamList->mode[0] ? gCamList->mode : "-");
    return;
  }
  const cameras::Camera& c = gCamList->cams[tile];
  if (gCamThumb[tile]) {
    d.pushImage(x, y, cameras::kThumbW, cameras::kThumbH, reinterpret_cast<const lgfx::rgb332_t*>(gCamThumb[tile]));
  } else {
    d.fillRect(x, y, cameras::kThumbW, cameras::kThumbH, camPanel());
    d.setTextColor(camMuted());
    const char* what = c.off ? "Off" : (c.pictureAge < 0 || gCamThumbNone[tile]) ? "No picture" : "...";
    d.setCursor(x + (cameras::kThumbW - d.textWidth(what)) / 2, y + 18);
    d.print(what);
  }
  if (selected) {
    d.drawRect(x, y, cameras::kThumbW, cameras::kThumbH, edge);
    d.drawRect(x + 1, y + 1, cameras::kThumbW - 2, cameras::kThumbH - 2, edge);
  }
  // Taking its new picture, the one-time refresh on opening: an amber strip saying so.
  if (int(tile) == gCamAutoCur) {
    d.fillRect(x + 2, y + cameras::kThumbH - 11, cameras::kThumbW - 4, 9, TFT_BLACK);
    d.setTextColor(camAmber());
    const char* text = "updating...";
    d.setCursor(x + (cameras::kThumbW - d.textWidth(text)) / 2, y + cameras::kThumbH - 10);
    d.print(text);
  }
  // The label: its name, a red dot for motion in the last five minutes, and how long ago.
  const int16_t ly = int16_t(y + cameras::kThumbH);
  if (selected) d.fillRect(x, ly, cameras::kThumbW, cameras::kLabelH, edge);
  const int32_t since = int32_t((millis() - gCamListAtMs) / 1000);
  const int32_t motion = c.motionAge < 0 ? -1 : c.motionAge + since;
  char age[8];
  cameras::ageText(age, sizeof(age), motion);
  const int16_t ageW = d.textWidth(age);
  const bool recent = motion >= 0 && motion < 300;
  d.setTextColor(selected ? TFT_BLACK : TFT_WHITE);
  d.setCursor(x + 2, ly + 2);
  d.print(fitCurrentFontToWidth(c.name, cameras::kThumbW - ageW - (recent ? 12 : 6)));
  d.setTextColor(selected ? TFT_BLACK : camMuted());
  d.setCursor(x + cameras::kThumbW - 2 - ageW, ly + 2);
  d.print(age);
  if (recent) d.fillCircle(x + cameras::kThumbW - 6 - ageW, ly + 5, 2, camRed());
}

void camDrawSignIn();
void camDrawWall() {
  auto& d = gCanvas;
  const uint8_t pageCount = cameras::pages(camCount());
  String right;
  if (camReady() && pageCount > 1) right = String(cameras::pageOf(gCamSel) + 1) + "/" + String(pageCount);
  if (camSignInShown() && gCamProblem.isEmpty() && gWifiReady) { camDrawSignIn(); return; }
  if (!camReady()) {
    drawBigHeader("Cameras", camAccent(), right);
    const cameras::State state = gCamList && gCamListOk ? gCamList->state : cameras::State::Unknown;
    const String detail = gCamList ? String(gCamList->detail) : String();
    if (!gCamProblem.isEmpty()) camDrawCenter(gCamProblem, "R tries again", camAmber());
    else if (!gWifiReady) camDrawCenter("Wi-Fi required", "", camAmber());
    else if (!gCamListOk) camDrawCenter("Asking the Mac...", "", camMuted());
    else if (state == cameras::State::Code || state == cameras::State::Captcha) {
      if (state == cameras::State::Code) camDrawCenter(detail.isEmpty() ? String("eufy sent a code") : detail, "Type it, then Enter", TFT_WHITE, 34);
      else camDrawCenter("eufy wants a captcha", "captcha.png on the Mac: type it", TFT_WHITE, 34);
      d.drawRoundRect(60, 80, 120, 26, 5, camAccent());
      d.setFont(&fonts::Font4);
      d.setTextColor(TFT_WHITE);
      const String code = String(gCamCode) + (millis() / 500 % 2 ? "_" : " ");
      d.setCursor((240 - d.textWidth(code)) / 2, 82);
      d.print(code);
      d.setFont(&fonts::Font2);
    } else if (state == cameras::State::SigningIn || state == cameras::State::Starting) camDrawCenter(detail.isEmpty() ? String("Signing in to eufy...") : detail, "", camMuted());
    else if (state == cameras::State::Setup) camDrawCenter(detail.isEmpty() ? String("Add the eufy sign-in on the Mac") : detail, "docs/CAMERAS.md says how", camAmber());
    else camDrawCenter(detail.isEmpty() ? String("Cameras aren't ready") : detail, "R tries again", camAmber());
  } else if (!camCount()) {
    drawBigHeader("Cameras", camAccent(), right);
    camDrawCenter("No cameras on the account", "", camMuted());
  } else {
    drawBigHeader("Cameras", camAccent(), right, "Enter open  L live", "R new  C clips");
    const uint8_t page = cameras::pageOf(gCamSel);
    for (uint8_t t = 0; t < cameras::tileCount(camCount()); ++t) if (cameras::pageOf(t) == page) camDrawTile(t);
  }
  if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
}

// The sign-in screen: the phone page's QR code, the screen's height, on the white it needs, and
// what to do beside it.
void camDrawSignIn() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  const bool refused = gCamList->state == cameras::State::Error;
  const bool haveCode = !gCamSetupUrl.isEmpty() && int32_t(millis() - gCamSetupUntilMs) < 0;
  int16_t textX = 8;
  if (haveCode) {
    d.fillRect(0, 0, 135, 135, TFT_WHITE);
    d.qrcode(gCamSetupUrl.c_str(), 0, 0, 135, 1, true);
    textX = 142;
  }
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(camAccent());
  d.setCursor(textX, 6);
  d.print("Cameras");
  d.setFont(&fonts::Font2);
  const char* const* lines;
  static const char* const kScan[] = {"Scan with your", "phone to sign", "in to eufy.", nullptr};
  static const char* const kRefused[] = {"eufy said no.", "Scan to sign", "in again.", nullptr};
  static const char* const kWaiting[] = {"Getting a sign-in", "code for your", "phone...", nullptr};
  static const char* const kNoPage[] = {"Add the eufy", "sign-in on", "the Mac.", nullptr};
  lines = haveCode ? (refused ? kRefused : kScan) : gCamSetupNone ? kNoPage : kWaiting;
  d.setTextColor(TFT_WHITE);
  // Three lines, a gap and three more: 30 + 6 x 16 + 4 = 130, inside the 135 rows.
  int16_t y = 30;
  for (int i = 0; lines[i]; ++i, y += 16) { d.setCursor(textX, y); d.print(lines[i]); }
  d.setTextColor(camMuted());
  static const char* const kOr[] = {"Or edit", "eufy.env on", "the Mac.", nullptr};
  static const char* const kGuide[] = {"docs/CAMERAS.md", "says how.", nullptr};
  const char* const* rest = gCamSetupNone ? kGuide : kOr;
  y += 4;
  for (int i = 0; rest[i]; ++i, y += 16) { d.setCursor(textX, y); d.print(rest[i]); }
  if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
}

// A picture or live view with nothing to draw yet.
void camDrawWaitingView() {
  const cameras::Camera* c = camAt(gCamCur);
  if (gCamClipMode) {
    char when[8], day[16];
    cameras::clockOfDay(when, sizeof(when), gCamClipNow.start);
    cameras::dayLabel(day, sizeof(day), gCamClipNowDay);
    drawBigHeader(camNameOfId(gCamClipCam), camAccent(), when);
    const uint32_t since = (millis() - gCamLiveBeganMs) / 1000;
    if (!gCamLiveStarted) camDrawCenter(String("Getting the clip... ") + since + "s", String("from the HomeBase, ") + day, TFT_WHITE);
    else camDrawCenter("Starting it...", "", camMuted());
    if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
    return;
  }
  drawBigHeader(c ? c->name : "Cameras", camAccent(), gCamView == CamView::Live ? "Live" : "");
  if (gCamRefreshSince) camDrawCenter(String("Waking ") + (c ? c->name : "the camera") + "... " + String((millis() - gCamRefreshSince) / 1000) + "s", "", TFT_WHITE);
  else if (gCamView == CamView::Live) camDrawCenter("Connecting...", "", camMuted());
  else if (gCamPictureNone) camDrawCenter("No picture yet", "R takes one", camMuted());
  else camDrawCenter("Loading the picture...", "", camMuted());
  if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
}

// The day's list: rows on the left, newest first; the picked clip's picture and the keys on the right.
void camDrawClips() {
  auto& d = gCanvas;
  char day[16] = "";
  if (gCamClipsDay && gCamClipsDay == gCamToday) snprintf(day, sizeof(day), "%s", "Today");
  else if (gCamClipsDay) cameras::dayLabel(day, sizeof(day), gCamClipsDay);
  const String who = gCamClipFilterId[0] ? String(camNameOfId(gCamClipFilterId)) : String("All cameras");
  String count;
  if (!gCamClipsLoading && gCamClipsProblem.isEmpty() && gCamClips) {
    if (gCamClips->total > gCamClips->count) count = String(gCamClipRowCount) + " of " + gCamClips->total;
    else count = String(gCamClipRowCount) + (gCamClipRowCount == 1 ? " clip" : " clips");
  }
  drawBigHeader("Clips", camAccent(), day, who, count);
  d.setFont(&fonts::Font0);
  // The rows, the picked one kept in view.
  if (gCamClipsLoading || !gCamClipsProblem.isEmpty() || !gCamClipRowCount) {
    String a, b;
    uint16_t ink = camMuted();
    if (gCamClipsLoading) { a = "Asking the HomeBase..."; b = String((millis() - gCamClipSelAtMs) / 1000) + "s"; }
    else if (!gCamClipsProblem.isEmpty()) { a = gCamClipsProblem; b = "R tries again"; ink = camAmber(); }
    else if (gCamClipFilterId[0]) { a = "None from this camera"; b = "F: another camera"; }
    else { a = "No clips this day"; b = "D: the month"; }
    d.setFont(&fonts::Font2);
    d.setTextColor(ink);
    const String fa = fitCurrentFontToWidth(a, kCamClipListW - 8);
    d.setCursor((kCamClipListW - d.textWidth(fa)) / 2, 54);
    d.print(fa);
    d.setFont(&fonts::Font0);
    d.setTextColor(camMuted());
    d.setCursor((kCamClipListW - d.textWidth(b)) / 2, 76);
    d.print(b);
  } else {
    const int first = max(0, min(gCamClipSel - kCamClipRows / 2, gCamClipRowCount - kCamClipRows));
    for (int r = 0; r < kCamClipRows && first + r < gCamClipRowCount; ++r) {
      const cameras::Clip* clip = camClipAtRow(first + r);
      const int16_t y = int16_t(kCamClipListY + r * kCamClipRowH);
      const bool sel = first + r == gCamClipSel;
      if (sel) d.fillRoundRect(0, y, kCamClipListW - 4, kCamClipRowH - 1, 3, camAccent());
      char when[8], length[10];
      cameras::clockOfDay(when, sizeof(when), clip->start);
      cameras::lengthText(length, sizeof(length), clip->seconds);
      d.setTextColor(sel ? TFT_BLACK : TFT_WHITE);
      d.setCursor(3, y + 2);
      d.print(when);
      const int16_t lengthW = d.textWidth(length);
      d.setTextColor(sel ? TFT_BLACK : camMuted());
      d.setCursor(kCamClipListW - 7 - lengthW, y + 2);
      d.print(length);
      if (!gCamClipFilterId[0]) {
        d.setTextColor(sel ? TFT_BLACK : camAccent());
        d.setCursor(37, y + 2);
        d.print(fitCurrentFontToWidth(camNameOfId(clip->cam), kCamClipListW - 7 - lengthW - 6 - 37));
      }
    }
    if (gCamClipRowCount > kCamClipRows) {
      const int16_t trackH = kCamClipRows * kCamClipRowH - 1;
      const int16_t thumbH = int16_t(max(6, trackH * kCamClipRows / gCamClipRowCount));
      d.fillRect(kCamClipListW - 2, kCamClipListY, 2, trackH, camPanel());
      d.fillRect(kCamClipListW - 2, int16_t(kCamClipListY + (trackH - thumbH) * first / (gCamClipRowCount - kCamClipRows)), 2, thumbH, camAccent());
    }
  }
  // The picked clip's picture: its first frame, made once the Mac has the clip.
  constexpr int16_t kPx = 160, kPy = 26;
  const cameras::Clip* clip = camClipAtRow(gCamClipSel);
  if (clip && gCamClipThumb && !strcmp(gCamClipThumbId, clip->id)) {
    d.pushImage(kPx, kPy, cameras::kThumbW, cameras::kThumbH, reinterpret_cast<const lgfx::rgb332_t*>(gCamClipThumb));
  } else {
    d.fillRect(kPx, kPy, cameras::kThumbW, cameras::kThumbH, camPanel());
    if (clip) {
      const char* what = !strcmp(gCamClipThumbNone, clip->id) ? "No picture" : "...";
      d.setTextColor(camMuted());
      d.setCursor(kPx + (cameras::kThumbW - d.textWidth(what)) / 2, kPy + 18);
      d.print(what);
    }
  }
  // The keys under it.
  const auto hint = [&](int16_t y, const char* key, const char* what) {
    d.setTextColor(TFT_WHITE);
    d.setCursor(kPx, y);
    d.print(key);
    d.setTextColor(camMuted());
    d.print(what);
  };
  int16_t y = kPy + cameras::kThumbH + 6;
  hint(y, "Enter", " play"); y += 11;
  if (!camHomeIp()) { d.setTextColor(camAmber()); d.setCursor(kPx, y); d.print("(home Wi-Fi)"); y += 11; }
  camHintArrow(d, kPx, y, true, TFT_WHITE);
  camHintArrow(d, int16_t(kPx + 7), y, false, TFT_WHITE);
  d.setTextColor(camMuted());
  d.setCursor(kPx + 13, y);
  d.print(" day");
  y += 11;
  hint(y, "D", " month"); y += 11;
  if (camCount() > 1) { hint(y, "F", " camera"); y += 11; }
  if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
}

// The month, Monday first: days with recordings lit, the cursor's day ringed, days to come dim.
void camDrawMonth() {
  auto& d = gCanvas;
  const cameras::Ymd v = cameras::ymdOf(gCamMonthCursor);
  const String title = String(cameras::monthName(v.m)) + " " + v.y;
  const bool loading = gCamMonth.ym != gCamMonthCursor / 100;
  drawBigHeader(title, camAccent(), loading ? String("...") : String(), "Enter that day", "- = month");
  d.setFont(&fonts::Font0);
  static const char kLetters[] = "MTWTFSS";
  constexpr int16_t kCellW = 34, kLeft = 1, kTop = 35;
  for (int c = 0; c < 7; ++c) {
    d.setTextColor(c >= 5 ? camAccent() : camMuted());
    d.setCursor(kLeft + c * kCellW + (kCellW - 6) / 2, 25);
    d.print(kLetters[c]);
  }
  const int lead = cameras::monthLead(v.y, v.m), rows = cameras::monthRows(v.y, v.m);
  const int16_t cellH = int16_t(min(16, (135 - kTop) / rows));
  for (int day = 1; day <= cameras::daysInMonth(v.y, v.m); ++day) {
    const int at = lead + day - 1;
    const int16_t x = int16_t(kLeft + (at % 7) * kCellW), y = int16_t(kTop + (at / 7) * cellH);
    const uint32_t ymd = cameras::dayOf(cameras::Ymd{v.y, v.m, day});
    const bool future = gCamToday && ymd > gCamToday;
    const bool has = !loading && gCamMonth.has[day];
    const bool cursor = ymd == gCamMonthCursor;
    if (has) d.fillRoundRect(x + 2, y + 1, kCellW - 4, cellH - 2, 3, camPanel());
    if (cursor) {
      d.drawRoundRect(x + 1, y, kCellW - 2, cellH, 4, camAccent());
      d.drawRoundRect(x + 2, y + 1, kCellW - 4, cellH - 2, 3, camAccent());
    }
    char n[4];
    snprintf(n, sizeof(n), "%d", day);
    const uint16_t ink = future ? rgb565_local(73, 73, 109) : ymd == gCamToday ? camAmber() : has ? TFT_WHITE : camMuted();
    d.setTextColor(ink);
    d.setCursor(x + (kCellW - d.textWidth(n)) / 2, y + (cellH - 8) / 2);
    d.print(n);
    if (has) d.fillRect(x + kCellW / 2 - 4, y + cellH - 3, 8, 1, camAccent());
  }
  if (!gCamMonthProblem.isEmpty()) drawBigNotice(gCamMonthProblem, camAmber());
  else if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
}

void renderCamerasUi() {
  const bool direct = cameraDrawsDirect();
  if (direct) {
    // Again after the menu or anything else was over it; else only when something changed.
    if (!gCamWasDirect) gCamDirty = true;
    gCamWasDirect = true;
    if (gCamDirty) { gCamDirty = false; camDrawDirect(); }
    return;
  }
  gCamWasDirect = false;
  gCamDirty = false;
  if (gCamView == CamView::Clips && gCamClips && gCamList) camDrawClips();
  else if (gCamView == CamView::Month && gCamClips && gCamList) camDrawMonth();
  else if (gCamView == CamView::Wall || gCamView == CamView::Clips || gCamView == CamView::Month || !gCamList) camDrawWall();
  else if (gLauncherVisible) camDrawWall();
  else camDrawWaitingView();
  renderLauncherOverlay();
  renderDebugOverlay();
}

bool camerasOn() { return gUiMode == UiMode::Cameras; }
// The worker is asking the bridge over HTTPS: away from home, or the home door's ticket. The memory
// guard lets that pass (010), as it does the inbox's.
bool cameraHttpsBusy() { return gUiMode == UiMode::Cameras && camera_net::busy() && !camera_net::overHome(); }
}  // namespace
