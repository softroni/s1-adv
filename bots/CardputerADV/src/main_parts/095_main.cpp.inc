// Cameras: the eufy cameras on the bridge's Mac (bridge/src/cameras.mjs). The wall shows each
// camera's last picture; Enter opens one full screen, L watches it live, R wakes it for a new
// picture. The wall is drawn on the menu canvas, with thumbnails the bridge has already dithered
// to its 3-3-2 colours. A picture or a live frame is a JPEG drawn straight to the screen in full
// colour, so while one shows, the canvas isn't pushed over it (cameraDrawsDirect).
//
// At home everything comes over the bridge's plain-HTTP door with a ticket, which live view
// needs: TLS's buffers and a frame don't fit together. Away from home, pictures come over HTTPS
// and live view says it works at home only.
#include "../camera_catalog.h"
#include "../camera_net.h"
namespace {
enum class CamView : uint8_t { Wall, Picture, Live };
enum class CamJob : uint8_t { None, Ticket, List, Thumb, Picture, Refresh, LiveStart, Frame, Extend, Stop, Turn, Code, Report, Setup,
                              Zoom, Pan, AutoRefresh, Audio };

constexpr uint32_t kCamListEveryMs = 60000;   // The wall's ages and battery, asked again this often.
constexpr uint32_t kCamSigningEveryMs = 4000; // And while signing in, so the screen follows the phone.
constexpr uint32_t kCamNoticeMs = 3500;
constexpr uint32_t kCamBarsMs = 3000;         // A picture's bars, after a key, before they go.
constexpr uint32_t kCamTurnLitMs = 350;
constexpr uint32_t kCamRefreshWaitMs = 45000; // A still wakes a camera: it can take a while.
constexpr int16_t kCamTopBar = 16, kCamBottomBar = 12;

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
  for (size_t i = 0; i < cameras::kMax; ++i) { free(gCamThumb[i]); gCamThumb[i] = nullptr; gCamThumbAsked[i] = gCamThumbNone[i] = false; }
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
  return true;
}
bool camStartFor(CamJob job, int cam, const char* what, size_t maxBytes, uint32_t waitMs, const char* extra = "") {
  const cameras::Camera* c = camAt(cam);
  char path[160];
  if (!c || !cameras::routePath(path, sizeof(path), what, c->id, extra)) return false;
  return camStart(job, cam, path, maxBytes, waitMs);
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
  if (gCamLiveStarted && gCamLiveFrames) {
    char note[40];
    snprintf(note, sizeof(note), "%s_sound%us_gaps%u", why, unsigned(gCamAudioSamples / kCamAudioRate), unsigned(gCamAudioGaps));
    camQueueReport("live", note, 0);
    appendRuntimeLog("CAM", String("live ") + gCamLiveFrames + " frames " + String(gCamFps, 1) + " fps, " + why, true);
  }
  gCamLiveStarted = false;
  camAudioFree();
  gCamWantAudio = false;
  if (gCamJob == CamJob::Frame || gCamJob == CamJob::LiveStart || gCamJob == CamJob::Audio) camera_net::cancel();
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

void camFinished() {
  const CamJob job = gCamJob;
  const int cam = gCamJobCam;
  gCamJob = CamJob::None;
  gCamJobCam = -1;
  const camera_net::Error e = camera_net::error();
  const unsigned status = camera_net::status();
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
          else if (gCamView != CamView::Wall) { if (gCamLive) camEndLive("camera gone"); gCamView = CamView::Wall; }
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
      if (!gCamLive || cam != gCamCur) break;
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
        gCamLiveStarted = false;
        camEndLive("ended on the Mac");
        camSay("Live view ended");
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
      if (!gCamLive || cam != gCamCur) break;
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
      if (!gCamLive || cam != gCamCur) break;
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
    gCamWantStop = false;
    if (camStartFor(CamJob::Stop, gCamCur, "live/stop", 64, 10000)) return;
  }
  if (gCamWantCode) {
    char path[64];
    gCamWantCode = false;
    const bool captcha = gCamList && gCamList->state == cameras::State::Captcha;
    if ((captcha ? cameras::captchaPath(path, sizeof(path), gCamCode) : cameras::codePath(path, sizeof(path), gCamCode)) &&
        camStart(CamJob::Code, -1, path, 128, 30000)) return;
  }
  if (gCamLive && !gCamLiveStarted) {
    if (camStartFor(CamJob::LiveStart, gCamCur, "live/start", 64, 30000)) return;
  }
  if (gCamLive && gCamWantExtend) {
    gCamWantExtend = false;
    if (camStartFor(CamJob::Extend, gCamCur, "live/extend", 64, 10000)) return;
  }
  if (gCamLive && gCamLiveStarted && gCamWantZoom) {
    char extra[16];
    snprintf(extra, sizeof(extra), "x=%u", unsigned(gCamWantZoom));
    gCamWantZoom = 0;
    if (camStartFor(CamJob::Zoom, gCamCur, "live/zoom", 128, 15000, extra)) return;
  }
  if (gCamLive && gCamLiveStarted && gCamWantPan != cameras::Arrow::None) {
    char extra[24];
    snprintf(extra, sizeof(extra), "dir=%s%s", cameras::directionWord(gCamWantPan), gCamWantPanTurn ? "&turn=1" : "");
    gCamWantPan = cameras::Arrow::None;
    if (camStartFor(CamJob::Pan, gCamCur, "live/pan", 128, 15000, extra)) return;
  }
  if (gCamLive && gCamWantTurn != cameras::Arrow::None) {
    char extra[16];
    snprintf(extra, sizeof(extra), "dir=%s", cameras::directionWord(gCamWantTurn));
    gCamWantTurn = cameras::Arrow::None;
    if (camStartFor(CamJob::Turn, gCamCur, "turn", 64, 15000, extra)) return;
  }
  if (gCamWantAudio && camAudioOn()) {
    gCamWantAudio = false;
    if (camStartFor(CamJob::Audio, gCamCur, "live/audio", 32000, 5000)) return;
  }
  if (gCamLive && gCamLiveStarted) {
    if (camStartFor(CamJob::Frame, gCamCur, "live/frame", cameras::kMaxJpeg, 10000)) return;
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
  if (gCamView != CamView::Wall) return;
  // Thumbnails, the selected one's first, then the rest of its page.
  if (camNextThumb()) return;
  // Then, once the last pictures are up, a new one from each camera, the selected one first.
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
bool camNextThumb() {
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
  camAudioFree();
  camera_net::close();
  camFreeThumbs();
  camFreeJpeg();
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
    // The screen stays on while watching, as a video player's does.
    gLastUserActivityMs = now;
    if (gCamLiveStarted && gCamLiveFrames && int32_t(now - gCamLiveEndsMs) >= 0) {
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
}

// Live steps back to its picture, the picture to the wall; the wall's Esc leaves the app.
bool camerasEscape() {
  if (gCamView == CamView::Live) { camEndLive("closed"); return true; }
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
    return true;
  }
  // Live. = and - zoom; the arrows move around the zoomed picture, and at its edge, or with Fn,
  // turn a camera that pans and tilts. The bridge says which happened.
  const cameras::Camera* c = camAt(gCamCur);
  if (key == '=' || key == '+' || key == '-' || key == '_') {
    if (!c || !gCamLiveStarted) return true;
    const uint8_t from = gCamWantZoom ? gCamWantZoom : uint8_t(gCamZoomView.tenths);
    const uint8_t to = cameras::zoomStep(c->lens, from, key == '=' || key == '+' ? 1 : -1);
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
  return gUiMode == UiMode::Cameras && !gLauncherVisible && gCamView != CamView::Wall && gCamJpeg && gCamJpegOf == gCamCur &&
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
  x = camHintText(d, x, y, "Esc", TFT_WHITE); camHintText(d, x, y, " wall", camMuted());
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
void camDrawZoomMap(lgfx::LGFXBase& d) {
  constexpr int16_t kX = 4, kY = 135 - 4 - 22, kW = 39, kH = 22;
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

void camDrawDirect() {
  auto& d = M5Cardputer.Display;
  const bool live = gCamView == CamView::Live;
  const bool bars = !live && (gCamBarsUntil || gCamRefreshSince);
  const int16_t top = live || bars ? kCamTopBar : 0;
  const int16_t bottom = bars ? int16_t(135 - kCamBottomBar) : 135;
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
  if (live) camDrawLiveBar(d);
  else if (bars) camDrawPictureBars(d);
  if (live) {
    const cameras::Camera* c = camAt(gCamCur);
    if (c && c->ptz) camDrawTurnIcon(d, 226, 121, gCamTurnLitUntil && int32_t(millis() - gCamTurnLitUntil) < 0);
    if (gCamAudioHeard || gCamAudioMuted) camDrawSoundIcon(d, c && c->ptz ? 203 : 226, 121, gCamAudioMuted);
    if (gCamZoomView.tenths > 10) camDrawZoomMap(d);
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
    drawBigHeader("Cameras", camAccent(), right, "Enter open  L live", "R new picture");
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
  drawBigHeader(c ? c->name : "Cameras", camAccent(), gCamView == CamView::Live ? "Live" : "");
  if (gCamRefreshSince) camDrawCenter(String("Waking ") + (c ? c->name : "the camera") + "... " + String((millis() - gCamRefreshSince) / 1000) + "s", "", TFT_WHITE);
  else if (gCamView == CamView::Live) camDrawCenter("Connecting...", "", camMuted());
  else if (gCamPictureNone) camDrawCenter("No picture yet", "R takes one", camMuted());
  else camDrawCenter("Loading the picture...", "", camMuted());
  if (camNoticeShowing()) drawBigNotice(gCamNotice, gCamNoticeInk);
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
  if (gCamView == CamView::Wall || !gCamList) camDrawWall();
  else if (gLauncherVisible) camDrawWall();
  else camDrawWaitingView();
  renderLauncherOverlay();
  renderDebugOverlay();
}

bool camerasOn() { return gUiMode == UiMode::Cameras; }
}  // namespace
