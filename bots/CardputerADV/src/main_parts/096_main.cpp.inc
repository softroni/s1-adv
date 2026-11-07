// Skywatch (UiMode::Planes; it was called Planes until 0.2.327): what's flying over the Cardputer
// and when the space station passes, from the bridge
// (bridge/src/planes.mjs). Four pages, flipped with , and /: the sky map with the nearest planes,
// the station's next passes you can see, where the station is right now, and what the kids have
// spotted. Enter on a plane says which way to look and how high; Enter on a pass draws its path
// across the sky. A plane high overhead brings up "Look up!", S keeps a plane in Spotted on the
// SD card, and R on a pass rings ten minutes before it, from any screen. A fifth page listens for
// drones' Remote ID over Wi-Fi itself, with no bridge (drone_id.h, drone_listen.cpp).
//
// Where: the Cap's GPS when it has a fix, else the Weather city in Settings. The position leaves
// the device rounded to about a kilometre, as weather's does, and only to the owner's bridge.
// The requests go through the Cameras app's worker (camera_net), which only one app holds at a
// time: at home over the bridge's plain-HTTP door with a ticket, else over HTTPS.
#include "../planes_catalog.h"
#include "../camera_net.h"
#include "../drone_id.h"
#include "../drone_listen.h"
#include <initializer_list>
namespace {
enum class PlView : uint8_t { Sky, Plane, Passes, Pass, Now, Spotted, Sightings, GoOut, Drones, Drone, PlaneList };
enum class PlJob : uint8_t { None, Ticket, Near, Iss, Report };

constexpr uint8_t kGpsForPlanes = 16;            // Beside 081's holders: Lab 1, once 2, Pulse 4, Mesh 8.
constexpr uint32_t kPlNearEveryMs = 10000;       // Planes move a mile in about ten seconds.
constexpr uint32_t kPlIssEveryMs = 10UL * 60UL * 1000UL, kPlNowEveryMs = 20000;
// Away from home each ask is an HTTPS handshake of about 40 KB and seconds of work: less often.
constexpr uint32_t kPlNearAwayMs = 30000, kPlNowAwayMs = 60000;
constexpr uint32_t kPlRetryMs = 15000;
constexpr uint32_t kPlLookUpChimeGapMs = 30000;  // A plane overhead chimes once, and not again within this.
constexpr uint32_t kPlNoticeMs = 3000;
constexpr size_t kPlNearMax = 9000, kPlIssMax = 4096;
constexpr const char* kPlDir = "/planes";
constexpr const char* kPlSpottedPath = "/planes/spotted.txt";
constexpr const char* kPlPrefsNs = "planes";
constexpr uint8_t kPlRecent = 40;                // Sightings listed, newest first.
constexpr uint8_t kPlAlertMemory = 16, kPlSavedMemory = 24;
constexpr int16_t kPlMapX = 54, kPlMapY = 72, kPlMapR = 46;  // The round maps, left; the chosen one's card beside them.
constexpr int16_t kPlCardX = 108, kPlRowH = 24, kPlRows = 4;

uint16_t plAccent() { return rgb565_local(110, 200, 255); }
uint16_t plMuted() { return rgb565_local(146, 146, 170); }
uint16_t plDim() { return rgb565_local(60, 70, 100); }
uint16_t plFaint() { return rgb565_local(36, 36, 85); }
uint16_t plAmber() { return rgb565_local(255, 196, 64); }
uint16_t plGreen() { return rgb565_local(88, 240, 141); }
uint16_t plGold() { return rgb565_local(255, 220, 100); }
uint16_t plPink() { return rgb565_local(255, 110, 200); }

PlView gPlView = PlView::Sky, gPlBack = PlView::Sky;
PlView gPlPlaneBack = PlView::Sky;     // Where a plane's screen goes back to: the map or the list.
PlView gPlOpenOn = PlView::Sky;           // Where the next opening starts: the reminder opens on its notice.
planes::Sky* gPlSky = nullptr;            // On the heap while the app is open.
planes::Space* gPlSpace = nullptr;
planes::Stats* gPlStats = nullptr;        // And these only on the Spotted pages.
planes::Sighting* gPlRecent = nullptr;
uint8_t gPlRecentCount = 0, gPlRecentTop = 0;
bool gPlStatsRead = false;
bool gPlSkyOk = false, gPlSpaceOk = false;
uint32_t gPlSkyAtMs = 0, gPlSpaceAtMs = 0;
uint8_t gPlSel = 0, gPlPassSel = 0, gPlRange = planes::kDefaultRange;
char gPlSelHex[7] = "";
bool gPlSelGone = false;                  // The plane on screen left the last list.
planes::Plane gPlShown;                   // The plane on its own screen, kept if it leaves.
PlJob gPlJob = PlJob::None;
uint32_t gPlNearDueMs = 0, gPlIssDueMs = 0;
bool gPlAskedByGps = false;               // The last near/iss went with the GPS's position.
String gPlProblem, gPlSpaceProblem, gPlNotice;
uint16_t gPlNoticeInk = 0;
uint32_t gPlNoticeUntil = 0, gPlOpenSince = 0;
char gPlReport[200] = "";
uint32_t gPlMemReportedMs = 0;
// The home door, asked once on each network.
podcast::HomeTicket gPlTicket;
uint32_t gPlTicketFor = 0, gPlHomeOffFor = 0;
bool gPlRetriedTicket = false;
// Where the Cardputer is, from the Cap.
weather_gps::Spot gPlHere;
bool gPlHoldsGps = false;
// "Look up!"
uint32_t gPlLastChimeMs = 0;
char gPlAlerted[kPlAlertMemory][7] = {};
uint8_t gPlAlertedNext = 0;
char gPlSaved[kPlSavedMemory][7] = {};
uint8_t gPlSavedNext = 0;
// The station reminder, kept in flash so it rings after a restart too.
bool gPlRemLoaded = false;
uint32_t gPlRemindAt = 0;
planes::Pass gPlRemPass;
planes::Pass gPlGoPass;                   // The pass the "Go outside" notice is for.
bool gPlPassFromReminder = false;         // The path screen shows gPlGoPass, not a listed pass.
// Chimes play from a buffer made as each starts and given back when it's done (plServiceChime).
constexpr uint8_t kPlChimeChannel = 0;
constexpr uint32_t kPlChimeRate = 8000;
int16_t* gPlChimeBuf = nullptr;
uint32_t gPlChimeEndsMs = 0, gPlChimeNextMs = 0;
uint8_t gPlChimeLeft = 0;
planes::Chime gPlChimeKind = planes::Chime::LookUp;
// Drones: listening takes Wi-Fi off its network, which planesHoldsWifi() keeps the reconnects away
// from, and gives it back when the page is left. Enter starts it, so flipping past never drops Wi-Fi.
enum class PlDroneState : uint8_t { Off, Waiting, Listening };
PlDroneState gPlDroneState = PlDroneState::Off;
bool gPlDroneHold = false, gPlDroneWifiWasOff = false;
drone_id::Table* gPlDrones = nullptr;       // On the heap while listening.
drone_listen::Frame* gPlDroneFrame = nullptr;
uint8_t gPlDroneSel = 0;
uint8_t gPlDroneShown[6] = {};  // By address: the table reorders.
uint32_t gPlDroneSince = 0, gPlDroneLastChimeMs = 0;
constexpr uint32_t kPlDroneChimeGapMs = 20000, kPlFreshMs = 20000;  // A new drone flashes this long.

void plInvalidate() { gLastRenderMs = 0; gUiFramePolicy.invalidate(); }
void plSay(const String& text, uint16_t ink = 0) {
  gPlNotice = text;
  gPlNoticeInk = ink ? ink : plAccent();
  gPlNoticeUntil = millis() + kPlNoticeMs;
  plInvalidate();
}
bool plNoticeShowing() { return gPlNoticeUntil && int32_t(millis() - gPlNoticeUntil) < 0; }
units::System plUnits() { return displayUnits(); }
uint32_t plNowUnix() { return hasValidSystemTime() ? uint32_t(time(nullptr)) : 0; }
bool plLocal(uint32_t unix, tm& out) {
  const time_t t = time_t(unix);
  return unix && localtime_r(&t, &out);
}
void plClockNow(char* out, size_t room) {
  tm now;
  if (!plLocal(plNowUnix(), now)) { if (room) out[0] = 0; return; }
  clock_display::format(out, room, now.tm_hour, now.tm_min, gDeviceSettings.clockTwelveHour);
}
void plTimeOf(char* out, size_t room, uint32_t unix) {
  tm at;
  if (!plLocal(unix, at)) { snprintf(out, room, "--:--"); return; }
  planes::clockText(out, room, at, gDeviceSettings.clockTwelveHour);
}
void plDayOf(char* out, size_t room, uint32_t unix) {
  tm now, at;
  if (!plLocal(plNowUnix(), now) || !plLocal(unix, at)) { snprintf(out, room, "Soon"); return; }
  planes::dayWord(out, room, now, at);
}

// --- Where, and the way to the bridge --------------------------------------------------------
bool plUsingGps() { return weather_gps::usable(gPlHere, millis()); }
bool plWhere(char* out, size_t room) {
  if (plUsingGps()) { weather_gps::query(out, room, gPlHere); return true; }
  return planes::placeQuery(out, room, gDeviceSettings.weatherPlace.c_str());
}
uint32_t plHomeIp() {
  const uint32_t mine = wifiLocalAddress();
  return mine && gPlTicketFor == mine && gPlHomeOffFor != mine ? podcast::homeFor(gPlTicket, mine) : 0;
}
bool plTicketWanted() {
  const uint32_t mine = wifiLocalAddress();
  return mine && gPlTicketFor != mine && gPlHomeOffFor != mine;
}
bool plStart(PlJob job, const char* httpsPath, size_t maxBytes, uint32_t waitMs) {
  char home[240];
  const uint32_t ip = plHomeIp();
  const bool viaHome = ip && planes::homePath(home, sizeof(home), gPlTicket, httpsPath);
  if (!viaHome) {
    prepareNetworkMemory("planes", true);
    compactMemoryForUi("planes", true);
  }
  if (!camera_net::start(viaHome ? home : httpsPath, viaHome, ip, gPlTicket.port, maxBytes, waitMs)) return false;
  gPlJob = job;
  return true;
}
// What ran short, with the heap's free and largest piece, for the bridge's log: once in a while.
void plQueueReport(const char* stage, unsigned code) {
  if (gPlMemReportedMs && millis() - gPlMemReportedMs < 30000) return;
  gPlMemReportedMs = millis();
  snprintf(gPlReport, sizeof(gPlReport), "/api/cardputer/planes/report?stage=%s&e=%u&free=%u&big=%u&via=%s&fw=%s&note=%s", stage, code,
           unsigned(ESP.getFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
           camera_net::overHome() ? "home" : "https", APP_VERSION, camera_net::memoryNote()[0] ? camera_net::memoryNote() : "-");
  for (char* p = gPlReport; *p; ++p) if (*p == ' ') *p = '_';
}

bool plWantsSky() {
  return gPlView == PlView::Sky || gPlView == PlView::Plane || gPlView == PlView::PlaneList;
}
bool plWantsSpace() {
  return gPlView == PlView::Passes || gPlView == PlView::Pass || gPlView == PlView::Now || gPlView == PlView::GoOut;
}

bool plNextJob() {
  if (gPlDroneState != PlDroneState::Off || !wifiLocalAddress()) return false;
  const uint32_t now = millis();
  if (gPlReport[0]) {
    char path[sizeof(gPlReport)];
    strcpy(path, gPlReport);
    gPlReport[0] = 0;
    if (plStart(PlJob::Report, path, 256, 8000)) return true;
  }
  if (plTicketWanted()) return plStart(PlJob::Ticket, planes::kTicketPath, 1024, 8000);
  char where[96], path[200];
  // A fix that came after asking by the city is worth asking again at once.
  if (plUsingGps() && !gPlAskedByGps) { gPlNearDueMs = 0; gPlIssDueMs = 0; }
  const bool skyDue = plWantsSky() && int32_t(now - gPlNearDueMs) >= 0;
  // The station's passes are fetched once early too, so its page opens with them.
  const bool spaceDue = (plWantsSpace() || (!gPlSpaceOk && gPlSkyOk && gPlSpaceProblem.isEmpty())) && int32_t(now - gPlIssDueMs) >= 0;
  if (!skyDue && !spaceDue) return false;
  if (!plWhere(where, sizeof(where))) {
    gPlProblem = gPlSpaceProblem = "Set your town: Settings, Device, Weather city";
    plInvalidate();
    return false;
  }
  gPlAskedByGps = plUsingGps();
  if (skyDue && planes::nearPath(path, sizeof(path), where, planes::rangeMeters(gPlRange, plUnits()))) {
    gPlNearDueMs = now + kPlRetryMs;  // Moved on when it answers.
    return plStart(PlJob::Near, path, kPlNearMax, 12000);
  }
  if (spaceDue && planes::issPath(path, sizeof(path), where)) {
    gPlIssDueMs = now + kPlRetryMs;
    return plStart(PlJob::Iss, path, kPlIssMax, 15000);
  }
  return false;
}

String plWord(const uint8_t* data, size_t length) {
  String word;
  for (size_t i = 0; data && i < length && i < 40; ++i) {
    const char c = char(data[i]);
    if (c == '\n' || c == '\r') break;
    word += c;
  }
  word.trim();
  return word;
}
String plProblemText(camera_net::Error e, const String& word) {
  if (word == "unknown_place") return "Can't find " + gDeviceSettings.weatherPlace + ": change Weather city in Settings";
  if (word == "bad_position") return "The position looks wrong";
  if (word == "planes_unavailable") return "The flight data isn't answering. Trying again...";
  if (word == "not_found" || e == camera_net::Error::NotFound) return "The bridge needs an update for Skywatch";
  if (e == camera_net::Error::NoBridge) return "The bridge didn't accept this Cardputer";
  if (e == camera_net::Error::Connect) return "Can't reach the bridge";
  if (e == camera_net::Error::Timeout) return "The bridge didn't answer in time";
  if (e == camera_net::Error::NoMemory) return String("Not enough memory (") + camera_net::memoryNote() + ")";
  const char* text = camera_net::errorText(e);
  return text && *text ? String(text) : String("Something went wrong");
}

bool plAlreadyAlerted(const char* hex) {
  for (const auto& h : gPlAlerted) if (!strcmp(h, hex)) return true;
  return false;
}
// times plays it again, a second and a half apart: the station reminder's three.
void plChime(planes::Chime kind, uint8_t times = 1) { gPlChimeKind = kind; gPlChimeLeft = times; gPlChimeNextMs = millis(); }
bool plFlashOn() { return (millis() / 350) % 2 == 0; }
void plWakeScreen() {
  if (!gScreensaverActive && (gScreensaverDimmed || gScreenOffActive)) {
    gScreensaverDimmed = gScreenOffActive = false;
    applyDisplayBrightness();
  }
  markUserActivity();
}
// After a fresh list: a plane high overhead chimes, softly, once each, and lights the screen. Nothing
// opens: the sky map flashes it, U goes to it, and its own screen says to look up.
void plNoteOverhead() {
  if (!plWantsSky()) return;
  for (uint8_t i = 0; i < gPlSky->count; ++i) {
    const planes::Plane& p = gPlSky->planes[i];
    if (!planes::wantsLookUp(p) || plAlreadyAlerted(p.hex)) continue;
    strcpy(gPlAlerted[gPlAlertedNext], p.hex);
    gPlAlertedNext = uint8_t((gPlAlertedNext + 1) % kPlAlertMemory);
    if (gPlLastChimeMs && millis() - gPlLastChimeMs < kPlLookUpChimeGapMs) continue;
    gPlLastChimeMs = millis();
    plWakeScreen();
    plChime(planes::Chime::LookUp);
    appendRuntimeLog("PLANES", String("overhead ") + p.hex);
  }
}
// The nearest plane overhead, or -1.
int plOverhead() {
  if (!gPlSkyOk) return -1;
  for (uint8_t i = 0; i < gPlSky->count; ++i) if (planes::wantsLookUp(gPlSky->planes[i])) return i;
  return -1;
}

void plTakeNear(uint8_t* data, size_t length) {
  // Only a real answer replaces the list, so a bad one leaves the old on screen.
  if (!data || length < 3 || memcmp(data, "at\t", 3)) { gPlProblem = "The bridge's answer wasn't planes"; return; }
  planes::parseNear(reinterpret_cast<const char*>(data), length, *gPlSky);
  gPlSkyOk = true;
  gPlSkyAtMs = millis();
  gPlProblem = "";
  // The selection follows its plane as the list reorders.
  gPlSelGone = true;
  for (uint8_t i = 0; i < gPlSky->count; ++i)
    if (!strcmp(gPlSky->planes[i].hex, gPlSelHex)) { gPlSel = i; gPlSelGone = false; gPlShown = gPlSky->planes[i]; break; }
  if (gPlSelGone && gPlView != PlView::Plane) {
    gPlSel = gPlSky->count ? min<uint8_t>(gPlSel, gPlSky->count - 1) : 0;
    if (gPlSky->count) { strcpy(gPlSelHex, gPlSky->planes[gPlSel].hex); gPlShown = gPlSky->planes[gPlSel]; gPlSelGone = false; }
  }
  plNoteOverhead();
}
void plTakeIss(uint8_t* data, size_t length) {
  if (!data || length < 3 || memcmp(data, "at\t", 3)) { gPlSpaceProblem = "The bridge's answer wasn't the station"; return; }
  uint32_t keep = gPlSpace->count && gPlPassSel < gPlSpace->count ? gPlSpace->passes[gPlPassSel].start : 0;
  planes::parseIss(reinterpret_cast<const char*>(data), length, *gPlSpace);
  gPlSpaceOk = true;
  gPlSpaceAtMs = millis();
  gPlSpaceProblem = "";
  gPlPassSel = 0;
  for (uint8_t i = 0; i < gPlSpace->count; ++i) if (gPlSpace->passes[i].start == keep) gPlPassSel = i;
  if (keep == 0) {
    const int next = planes::nextPass(*gPlSpace, plNowUnix());
    if (next > 0) gPlPassSel = uint8_t(next);
  }
}

void plFinished() {
  const PlJob job = gPlJob;
  gPlJob = PlJob::None;
  const camera_net::Error e = camera_net::error();
  const unsigned status = camera_net::status();
  const bool viaHome = camera_net::overHome();
  const uint32_t tookMs = camera_net::tookMs();
  size_t length = 0;
  uint8_t* data = camera_net::take(length);
  const String word = e == camera_net::Error::None ? String() : plWord(data, length);
  const uint32_t mine = wifiLocalAddress();
  const uint32_t now = millis();
  if (e == camera_net::Error::None && status == 0) {
    // Cancelled, as a new ring does: ask again at once.
    free(data);
    if (job == PlJob::Near) gPlNearDueMs = now;
    if (job == PlJob::Iss) gPlIssDueMs = now;
    return;
  }
  if (e == camera_net::Error::NoMemory && job != PlJob::Report)
    plQueueReport(job == PlJob::Near ? "near" : job == PlJob::Iss ? "iss" : "ticket", unsigned(e));
  if (viaHome && !status && (e == camera_net::Error::Connect || e == camera_net::Error::Timeout)) {
    // Another Wi-Fi, or a Mac that's away: HTTPS from now on, on this network.
    gPlHomeOffFor = mine;
    gPlNearDueMs = gPlIssDueMs = now;
  } else if (viaHome && status == 404 && word == "not_found" && !gPlRetriedTicket) {
    // A ticket the bridge forgot when it restarted: ask again once.
    gPlRetriedTicket = true;
    gPlTicketFor = 0;
    gPlNearDueMs = gPlIssDueMs = now;
  } else {
    switch (job) {
      case PlJob::Ticket: {
        podcast::HomeTicket ticket;
        if (e == camera_net::Error::None && data && planes::parseTicket(reinterpret_cast<const char*>(data), length, ticket)) gPlTicket = ticket;
        else gPlTicket = podcast::HomeTicket{};  // An older bridge, or no home door: HTTPS on this network.
        gPlTicketFor = mine;
        break;
      }
      case PlJob::Near:
        if (e == camera_net::Error::None) {
          plTakeNear(data, length);
          gPlNearDueMs = now + (viaHome ? kPlNearEveryMs : kPlNearAwayMs);
          appendRuntimeLog("PLANES", String("near ") + (gPlSky ? gPlSky->count : 0) + " " + tookMs + "ms " + (viaHome ? "home" : "https"));
        } else {
          gPlProblem = plProblemText(e, word);
          gPlNearDueMs = now + (word == "unknown_place" || word == "not_found" ? 60000 : kPlRetryMs);
          appendRuntimeLog("PLANES", String("near failed ") + camera_net::errorText(e) + " " + status + " " + word);
        }
        break;
      case PlJob::Iss:
        if (e == camera_net::Error::None) {
          plTakeIss(data, length);
          gPlIssDueMs = now + (gPlView == PlView::Now ? (viaHome ? kPlNowEveryMs : kPlNowAwayMs) : kPlIssEveryMs);
        } else {
          gPlSpaceProblem = plProblemText(e, word);
          gPlIssDueMs = now + (word == "unknown_place" || word == "not_found" ? 60000 : kPlRetryMs);
          appendRuntimeLog("PLANES", String("iss failed ") + camera_net::errorText(e) + " " + status + " " + word);
        }
        break;
      default: break;
    }
  }
  free(data);
  plInvalidate();
}

// --- The station reminder ----------------------------------------------------------------------
void plLoadReminder() {
  if (gPlRemLoaded) return;
  gPlRemLoaded = true;
  gPrefs.begin(kPlPrefsNs, true);
  const String saved = gPrefs.getString("remind", "");
  gPrefs.end();
  if (!planes::parseReminder(saved.c_str(), gPlRemindAt, gPlRemPass)) gPlRemindAt = 0;
}
void plSaveReminder() {
  gPrefs.begin(kPlPrefsNs, false);
  if (gPlRemindAt) {
    char text[96];
    planes::formatReminder(text, sizeof(text), gPlRemindAt, gPlRemPass);
    gPrefs.putString("remind", text);
  } else {
    gPrefs.remove("remind");
  }
  gPrefs.end();
}
bool plReminded(const planes::Pass& p) { plLoadReminder(); return gPlRemindAt && gPlRemPass.start == p.start; }
void plToggleReminder(const planes::Pass& p) {
  const uint32_t now = plNowUnix();
  if (!now) { plSay("The clock isn't set yet", plAmber()); return; }
  if (plReminded(p)) {
    gPlRemindAt = 0;
    plSaveReminder();
    plSay("Reminder off");
    return;
  }
  if (now >= p.end) { plSay("That pass is over", plAmber()); return; }
  if (now + 60 >= p.start) { plSay("It's up now: go outside!", plAmber()); return; }
  gPlRemPass = p;
  gPlRemindAt = p.start > now + planes::kRemindBeforeS ? p.start - planes::kRemindBeforeS : now + 5;
  plSaveReminder();
  char at[16];
  plTimeOf(at, sizeof(at), gPlRemindAt);
  plSay(String("I'll ring at ") + at, plGreen());
}

// Plays a chime that's due, when the speaker is free for it as for Mesh's sounds: never over speech,
// the radio, a podcast or the synth (it's skipped then, not kept), and gives its buffer back after.
void plServiceChime() {
  const uint32_t now = millis();
  if (gPlChimeBuf && int32_t(now - gPlChimeEndsMs) >= 0 && !M5Cardputer.Speaker.isPlaying(kPlChimeChannel)) {
    free(gPlChimeBuf);
    gPlChimeBuf = nullptr;
  }
  if (!gPlChimeLeft || gPlChimeBuf || int32_t(now - gPlChimeNextMs) < 0) return;
  --gPlChimeLeft;
  gPlChimeNextMs = now + 1500;
  if (!gCanvasSpriteActive || !focusToneAllowed() || gVoiceCapturePreparing || gVoiceDialBusy || gRealtimeDialBusy ||
      gVoiceReplySpeechBusy)
    return;
  const planes::ChimeShape shape = planes::chimeShape(gPlChimeKind);
  const size_t room = size_t(kPlChimeRate) * shape.ms / 1000;
  gPlChimeBuf = static_cast<int16_t*>(malloc(room * sizeof(int16_t)));
  if (!gPlChimeBuf) return;
  const size_t n = planes::renderChime(gPlChimeKind, gPlChimeBuf, room, kPlChimeRate);
  if (!M5Cardputer.Speaker.playRaw(gPlChimeBuf, n, kPlChimeRate, false, 1, kPlChimeChannel, true)) {
    free(gPlChimeBuf);
    gPlChimeBuf = nullptr;
    return;
  }
  gPlChimeEndsMs = now + shape.ms + 40;
}

// Called from the loop on every screen: the reminder rings the way the alarm does, opening Planes.
void servicePlanesReminder() {
  plServiceChime();
  static uint32_t lastCheckMs = 0;
  if (millis() - lastCheckMs < 1000) return;
  lastCheckMs = millis();
  plLoadReminder();
  if (!gPlRemindAt || !hasValidSystemTime() || gAlarmRinging) return;
  const uint32_t now = plNowUnix();
  if (now < gPlRemindAt || gRecording || gSubmitting) return;
  const planes::Pass pass = gPlRemPass;
  gPlRemindAt = 0;
  plSaveReminder();
  if (now >= pass.end) return;  // Missed while the Cardputer was off.
  // The radio gives way, or the chime couldn't sound.
  if (radioBusy() || radioOn() || podcastOn()) stopInternetRadio();
  gPlGoPass = pass;
  gPlPassFromReminder = true;
  plWakeScreen();
  if (gUiMode == UiMode::Planes) {
    if (gPlView != PlView::GoOut) gPlBack = gPlView;
    gPlView = PlView::GoOut;
  } else {
    gPlOpenOn = PlView::GoOut;
    setUiMode(UiMode::Planes);
  }
  plChime(planes::Chime::LookUp, 3);
  flashActivityLed(255, 220, 100, 600);
  appendRuntimeLog("PLANES", "station reminder", true);
  plInvalidate();
}

// --- Spotted, on the SD card -------------------------------------------------------------------
bool plSavedThisVisit(const char* hex) {
  for (const auto& h : gPlSaved) if (!strcmp(h, hex)) return true;
  return false;
}
void plSaveSighting(const planes::Plane& p) {
  if (!p.hex[0]) return;
  if (plSavedThisVisit(p.hex)) { plSay("Already in Spotted"); return; }
  if (!gSdReady) { plSay("Spotted needs the SD card", plAmber()); return; }
  const uint32_t now = plNowUnix();
  if (!now) { plSay("The clock isn't set yet", plAmber()); return; }
  char line[128];
  const size_t n = planes::formatSighting(line, sizeof(line), now, p);
  if (!SD.exists(kPlDir)) SD.mkdir(kPlDir);
  File file = SD.open(kPlSpottedPath, FILE_APPEND);
  const bool ok = n && file && file.write(reinterpret_cast<const uint8_t*>(line), n) == n;
  if (file) file.close();
  if (!ok) { plSay("Couldn't write to the SD card", plAmber()); return; }
  strcpy(gPlSaved[gPlSavedNext], p.hex);
  gPlSavedNext = uint8_t((gPlSavedNext + 1) % kPlSavedMemory);
  gPlStatsRead = false;
  plChime(planes::Chime::Saved);
  plSay(String("Spotted! ") + p.name, plGreen());
}
void plFreeSpotted() {
  delete gPlStats;
  gPlStats = nullptr;
  delete[] gPlRecent;
  gPlRecent = nullptr;
  gPlRecentCount = gPlRecentTop = 0;
  gPlStatsRead = false;
}
// Reads the whole collection once when Spotted opens: the totals, and the newest sightings.
void plReadSpotted() {
  if (gPlStatsRead) return;
  gPlStatsRead = true;
  if (!gPlStats) gPlStats = new (std::nothrow) planes::Stats();
  if (!gPlRecent) gPlRecent = new (std::nothrow) planes::Sighting[kPlRecent];
  if (!gPlStats || !gPlRecent) { plFreeSpotted(); gPlStatsRead = true; return; }
  *gPlStats = planes::Stats{};
  gPlRecentCount = 0;
  if (!gSdReady) return;
  File file = SD.open(kPlSpottedPath, "r");
  if (!file) return;
  uint8_t next = 0;  // Where the next sighting goes in the ring of the newest.
  char line[160];
  size_t n = 0;
  uint8_t chunk[256];
  bool tooLong = false;
  const auto take = [&]() {
    if (!tooLong && n) {
      line[n] = 0;
      planes::Sighting s;
      if (planes::parseSighting(line, s)) {
        gPlStats->add(s);
        gPlRecent[next] = s;
        next = uint8_t((next + 1) % kPlRecent);
        if (gPlRecentCount < kPlRecent) ++gPlRecentCount;
      }
    }
    n = 0;
    tooLong = false;
  };
  size_t total = 0;
  while (file.available() && total < 256 * 1024) {
    const int got = file.read(chunk, sizeof(chunk));
    if (got <= 0) break;
    total += size_t(got);
    for (int i = 0; i < got; ++i) {
      const char c = char(chunk[i]);
      if (c == '\r') continue;
      if (c == '\n') { take(); continue; }
      if (n + 1 >= sizeof(line)) tooLong = true;
      else line[n++] = c;
    }
  }
  take();
  file.close();
  // Newest first: the ring read backwards from where it would write next.
  if (gPlRecentCount == kPlRecent && next) {
    planes::Sighting* ordered = new (std::nothrow) planes::Sighting[kPlRecent];
    if (ordered) {
      for (uint8_t i = 0; i < kPlRecent; ++i) ordered[i] = gPlRecent[(next + i) % kPlRecent];
      delete[] gPlRecent;
      gPlRecent = ordered;
    }
  }
  for (uint8_t i = 0, j = gPlRecentCount ? gPlRecentCount - 1 : 0; i < j; ++i, --j) {
    const planes::Sighting t = gPlRecent[i];
    gPlRecent[i] = gPlRecent[j];
    gPlRecent[j] = t;
  }
}

// --- Drawing -------------------------------------------------------------------------------
// The screen is 240 by 135 and small in the hand, so every page has the same frame and few, big
// things: a title and the clock along the top, with an icon for each of the five pages and the one
// showing lit (or, inside a page, a word about it); the page itself; and along the bottom at most
// four keys on caps, each with a word. Numbers come as tiles, a small label over a big value, and
// which way to look as an arrow in a ring, north up like the maps.
constexpr int16_t kPlKeysY = 124;
uint16_t plCap() { return rgb565_local(36, 73, 170); }
// 8x8, a byte a row, leftmost pixel in the top bit: a plane, the station's truss and panels, the
// Earth, a star and a drone's four rotors, in the order the pages go.
constexpr uint8_t kPlPageIcons[5][8] = {
    {0x18, 0x18, 0x7E, 0xFF, 0x18, 0x18, 0x3C, 0x66}, {0xC3, 0xC3, 0xDB, 0xFF, 0xDB, 0xC3, 0xC3, 0x00},
    {0x3C, 0x5A, 0x99, 0xFF, 0x99, 0x5A, 0x3C, 0x00}, {0x18, 0x18, 0xFF, 0x7E, 0x3C, 0x66, 0x42, 0x00},
    {0xC3, 0xC3, 0x24, 0x18, 0x18, 0x24, 0xC3, 0xC3}};

// Words wrapped into width from (x, y), a line every lineH, at most maxLines, in the current font.
int16_t plWrap(const String& text, int16_t x, int16_t y, int16_t width, int16_t lineH, uint8_t maxLines) {
  auto& d = gCanvas;
  String rest = text;
  uint8_t lines = 0;
  while (rest.length() && lines < maxLines) {
    int cut = rest.length();
    while (cut > 0 && d.textWidth(rest.substring(0, cut)) > width) {
      const int space = rest.lastIndexOf(' ', cut - 1);
      cut = space > 0 ? space : cut - 1;
    }
    if (cut <= 0) cut = 1;
    String line = rest.substring(0, cut);
    if (lines + 1 == maxLines && cut < int(rest.length())) line = fitCurrentFontToWidth(rest, width);
    d.setCursor(x, y + lines * lineH);
    d.print(line);
    rest = rest.substring(cut);
    rest.trim();
    ++lines;
  }
  return y + lines * lineH;
}
void plCentered(const String& text, int16_t y) {
  auto& d = gCanvas;
  const String shown = fitCurrentFontToWidth(text, 232);
  d.setCursor((240 - d.textWidth(shown)) / 2, y);
  d.print(shown);
}
uint8_t plPage() {
  switch (gPlView) {
    case PlView::Passes: case PlView::Pass: return 1;
    case PlView::Now: return 2;
    case PlView::Spotted: case PlView::Sightings: return 3;
    case PlView::Drones: case PlView::Drone: return 4;
    default: return 0;
  }
}
// The top. With no word, the page icons; with one, the word, for a screen inside a page.
void plTop(const String& title, uint16_t ink, const String& word = String()) {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  char clock[12];
  plClockNow(clock, sizeof(clock));
  d.setFont(&fonts::Font2);
  const int16_t clockX = 236 - d.textWidth(clock);
  d.setTextColor(plMuted());
  d.setCursor(clockX, 2);
  d.print(clock);
  int16_t edge = clockX - 7;
  if (word.isEmpty()) {
    const uint8_t page = plPage();
    const int16_t first = edge - 4 * 11 - 8;
    for (uint8_t i = 0; i < 5; ++i) {
      const int16_t x = first + i * 11;
      d.drawBitmap(x, 4, kPlPageIcons[i], 8, 8, i == page ? ink : plDim());
      if (i == page) d.fillRect(x, 14, 8, 2, ink);
    }
    edge = first - 7;
  } else {
    d.setFont(&fonts::Font0);
    const String shown = fitCurrentFontToWidth(word, 100);
    const int16_t x = edge - d.textWidth(shown);
    d.setTextColor(plMuted());
    d.setCursor(x, 6);
    d.print(shown);
    edge = x - 7;
  }
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(ink);
  d.setCursor(5, 2);
  d.print(fitCurrentFontToWidth(title, edge - 5));
  d.drawFastHLine(0, 20, 240, ink);
}
// The bottom: keys on caps, each with its word, as many as fit.
struct PlKey { const char* key; String word; };
void plKeys(std::initializer_list<PlKey> keys) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  int16_t x = 3;
  for (const PlKey& k : keys) {
    const int16_t capW = max<int16_t>(11, d.textWidth(k.key) + 6), wordW = d.textWidth(k.word);
    if (x + capW + 3 + wordW > 238) break;
    d.fillRoundRect(x, kPlKeysY, capW, 11, 3, plCap());
    d.setTextColor(TFT_WHITE);
    d.setCursor(x + (capW - d.textWidth(k.key)) / 2, kPlKeysY + 2);
    d.print(k.key);
    x += capW + 3;
    d.setTextColor(plMuted());
    d.setCursor(x, kPlKeysY + 2);
    d.print(k.word);
    x += wordW + 7;
  }
}
// A number: a small label over its big value.
void plTile(int16_t x, int16_t y, int16_t w, const char* label, const String& value, uint16_t ink = TFT_WHITE) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(x, y);
  d.print(fitCurrentFontToWidth(label, w - 2));
  d.setFont(&fonts::Font2);
  d.setTextColor(ink);
  d.setCursor(x, y + 9);
  d.print(fitCurrentFontToWidth(value, w - 2));
}
// "12 mi", "0.6 mi", "19 km": a distance for a tile or a sentence.
void plSpaced(char* out, size_t room, const char* compact) {
  size_t k = 0;
  while (compact[k] && ((compact[k] >= '0' && compact[k] <= '9') || compact[k] == '.')) ++k;
  snprintf(out, room, "%.*s %s", int(k), compact, compact + k);
}
void plDistanceWords(char* out, size_t room, uint32_t meters) {
  char compact[16];
  planes::distanceShort(compact, sizeof(compact), meters, plUnits());
  plSpaced(out, room, compact);
}
// Which way to look: an arrow in a ring, north up like the maps, with north marked.
void plDial(int16_t cx, int16_t cy, int16_t r, int bearing, uint16_t ink) {
  auto& d = gCanvas;
  d.drawCircle(cx, cy, r, plDim());
  d.fillRect(cx - 1, cy - r - 1, 3, 3, plMuted());
  const double a = planes::normalDeg(bearing) * 3.14159265358979 / 180.0, s = sin(a), c = -cos(a);
  const int tipX = cx + int(lround(s * (r - 2))), tipY = cy + int(lround(c * (r - 2)));
  const int tailX = cx - int(lround(s * (r - 5))), tailY = cy - int(lround(c * (r - 5)));
  const int baseX = cx + int(lround(s * (r - 10))), baseY = cy + int(lround(c * (r - 10)));
  for (int o = -1; o <= 1; ++o) d.drawLine(tailX - int(lround(c * o)), tailY + int(lround(s * o)), baseX - int(lround(c * o)), baseY + int(lround(s * o)), ink);
  d.fillTriangle(tipX, tipY, baseX - int(lround(c * 5)), baseY + int(lround(s * 5)), baseX + int(lround(c * 5)), baseY - int(lround(s * 5)), ink);
}
// A plane on the map: an arrow pointing where it's flying, or a dot when it isn't known.
void plGlyph(int16_t x, int16_t y, int track, uint16_t ink, bool chosen) {
  auto& d = gCanvas;
  if (chosen) d.drawCircle(x, y, 8, TFT_WHITE);
  if (track < 0) { d.fillCircle(x, y, chosen ? 3 : 2, chosen ? TFT_WHITE : ink); return; }
  const double a = track * 3.14159265358979 / 180.0, s = sin(a), c = -cos(a);
  const int len = chosen ? 6 : 5, back = chosen ? 4 : 3, half = chosen ? 4 : 3;
  const int bx = x - int(lround(s * back)), by = y - int(lround(c * back));
  d.fillTriangle(x + int(lround(s * len)), y + int(lround(c * len)), bx - int(lround(c * half)), by + int(lround(s * half)),
                 bx + int(lround(c * half)), by - int(lround(s * half)), chosen ? TFT_WHITE : ink);
}
// Around a plane overhead, or a drone just heard: a ring that grows and starts again.
void plPulse(int16_t x, int16_t y, uint16_t ink) {
  auto& d = gCanvas;
  const int grow = int((millis() / 110) % 5);
  d.drawCircle(x, y, 9 + grow, ink);
  if (grow < 3) d.drawCircle(x, y, 13 + grow, plDim());
}
// Signal strength as four bars.
void plBars(int16_t x, int16_t y, int8_t rssi, uint16_t ink) {
  auto& d = gCanvas;
  const int lit = rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
  for (int i = 0; i < 4; ++i) d.fillRect(x + i * 5, y + 12 - (i + 1) * 3, 3, (i + 1) * 3, i < lit ? ink : plFaint());
}
// A round map: north up, a ring at its edge and one at half, and you (or overhead) in the middle.
void plDrawRound(bool sky) {
  auto& d = gCanvas;
  d.drawCircle(kPlMapX, kPlMapY, kPlMapR, plDim());
  d.drawCircle(kPlMapX, kPlMapY, kPlMapR / 2, plFaint());
  d.drawFastHLine(kPlMapX - kPlMapR + 1, kPlMapY, kPlMapR * 2 - 1, plFaint());
  d.drawFastVLine(kPlMapX, kPlMapY - kPlMapR + 1, kPlMapR * 2 - 1, plFaint());
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_WHITE);
  d.setCursor(kPlMapX - 2, kPlMapY - kPlMapR + 3); d.print("N");
  d.setTextColor(plMuted());
  d.setCursor(kPlMapX - 2, kPlMapY + kPlMapR - 10); d.print("S");
  d.setCursor(kPlMapX - kPlMapR + 3, kPlMapY - 3); d.print("W");
  d.setCursor(kPlMapX + kPlMapR - 8, kPlMapY - 3); d.print("E");
  if (sky) {
    d.fillCircle(kPlMapX, kPlMapY, 1, plMuted());  // Straight overhead.
  } else {
    d.drawFastHLine(kPlMapX - 3, kPlMapY, 7, TFT_WHITE);  // You.
    d.drawFastVLine(kPlMapX, kPlMapY - 3, 7, TFT_WHITE);
  }
}
uint16_t plHeightInk(const planes::Plane& p) {
  if (planes::isDrone(p)) return plPink();
  if (p.altFt < 3000) return plGreen();
  if (p.altFt < 15000) return plAmber();
  return plAccent();
}
// Where the page is looking from: the GPS or the saved Weather city, and what the GPS is doing, with
// a dot in green (the GPS), amber (the city for now, or searching) or grey (no GPS at all). place is
// what the list on screen was asked about, "Here" for the GPS; null for drones, which only use the GPS.
void plWhereWords(char* out, size_t room, const char* place, uint16_t& ink) {
  const uint32_t now = millis();
  const bool hearing = gLabGps.hearing(now), fix = hearing && gLabGps.fix.valid;
  const bool byGps = place && !strcmp(place, "Here");
  String city = gDeviceSettings.weatherPlace;
  if (city.indexOf(',') > 0) city = city.substring(0, city.indexOf(','));
  if (!place) {
    if (fix) { ink = plGreen(); snprintf(out, room, "GPS fix (%u sats)", unsigned(gLabGps.fix.satellites)); }
    else if (hearing) { ink = plAmber(); snprintf(out, room, "No fix, %d sats seen", gLabGps.inView(now)); }
    else { ink = plMuted(); snprintf(out, room, "No GPS: no directions"); }
  } else if (byGps && fix) {
    ink = plGreen(); snprintf(out, room, "Using GPS (%u sats)", unsigned(gLabGps.fix.satellites));
  } else if (byGps) {
    ink = plGreen(); snprintf(out, room, "Using GPS, %lu min old", static_cast<unsigned long>((now - gPlHere.atMs) / 60000));
  } else if (fix) {
    ink = plGreen(); snprintf(out, room, "GPS fix: updating...");
  } else if (hearing) {
    ink = plAmber(); snprintf(out, room, "Using %s, no fix", city.c_str());
  } else {
    ink = plAmber(); snprintf(out, room, "Using %s, no GPS", city.c_str());
  }
}
void plWhereLine(int16_t x, int16_t y, int16_t w, const char* place) {
  auto& d = gCanvas;
  char text[64];
  uint16_t ink = plMuted();
  plWhereWords(text, sizeof(text), place, ink);
  d.fillCircle(x + 2, y + 3, 2, ink);
  d.setFont(&fonts::Font0);
  d.setTextColor(ink);
  d.setCursor(x + 8, y);
  d.print(fitCurrentFontToWidth(text, w - 8));
}
// A message where a page's card goes, in the bigger font.
void plCardSays(const String& text, uint16_t ink, int16_t x = kPlCardX) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font2);
  d.setTextColor(ink);
  plWrap(text, x, 26, 236 - x, 16, 6);
}

void plDrawSky() {
  auto& d = gCanvas;
  plTop("Planes", plAccent());
  plDrawRound(false);
  const uint32_t range = planes::rangeMeters(gPlRange, plUnits());
  char text[48], ring[12];
  planes::rangeLabel(text, sizeof(text), gPlRange, plUnits());
  plSpaced(ring, sizeof(ring), text);
  d.setFont(&fonts::Font0);
  d.setTextColor(plDim());
  d.setCursor(kPlMapX + kPlMapR - d.textWidth(ring), kPlMapY + kPlMapR - 4);
  d.print(ring);
  if (gPlSkyOk) {
    // Back to front, so the nearest and the chosen one are on top.
    for (int i = gPlSky->count - 1; i >= 0; --i) {
      if (i == gPlSel) continue;
      const planes::Plane& p = gPlSky->planes[i];
      int dx, dy;
      planes::mapPoint(p.distM, p.bearing, range, kPlMapR - 6, dx, dy);
      const bool up = planes::wantsLookUp(p);
      if (up) plPulse(kPlMapX + dx, kPlMapY + dy, plAmber());
      plGlyph(kPlMapX + dx, kPlMapY + dy, p.track, up ? (plFlashOn() ? TFT_WHITE : plAmber()) : plHeightInk(p), false);
    }
    if (gPlSel < gPlSky->count) {
      const planes::Plane& p = gPlSky->planes[gPlSel];
      int dx, dy;
      planes::mapPoint(p.distM, p.bearing, range, kPlMapR - 6, dx, dy);
      if (planes::wantsLookUp(p)) plPulse(kPlMapX + dx, kPlMapY + dy, plAmber());
      plGlyph(kPlMapX + dx, kPlMapY + dy, p.track, plHeightInk(p), true);
    }
  }
  if (!gPlSkyOk || !gPlSky->count) {
    String says;
    if (gPlProblem.length()) says = gPlProblem;
    else if (!wifiLocalAddress()) says = "Skywatch needs Wi-Fi";
    else if (!gPlSkyOk) says = "Looking for planes...";
    else says = String("No planes within ") + ring + " right now. = makes the ring bigger.";
    plCardSays(says, gPlProblem.length() ? plAmber() : plMuted());
    plWhereLine(kPlCardX, 114, 238 - kPlCardX, gPlSkyOk ? gPlSky->place : "");
    plKeys({{"-=", "Ring"}, {"R", "Look again"}, {",/", "Pages"}});
    return;
  }
  const planes::Plane& p = gPlSky->planes[gPlSel];
  const int16_t x = kPlCardX, w = 238 - kPlCardX;
  snprintf(text, sizeof(text), "%u/%u", unsigned(gPlSel + 1), unsigned(gPlSky->count));
  d.setFont(&fonts::Font0);
  const int16_t countW = d.textWidth(text);
  d.setTextColor(plMuted());
  d.setCursor(238 - countW, 27);
  d.print(text);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  d.setCursor(x, 23);
  d.print(fitCurrentFontToWidth(p.name, w - countW - 4));
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(x, 40);
  d.print(fitCurrentFontToWidth(p.type[0] ? p.type : planes::isHelicopter(p) ? "Helicopter" : "Plane", w));
  plDial(x + 13, 64, 13, p.bearing, plHeightInk(p));
  const bool overhead = planes::wantsLookUp(p);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(overhead ? (plFlashOn() ? plAmber() : TFT_WHITE) : TFT_WHITE);
  d.setCursor(x + 32, 52);
  d.print(overhead ? "LOOK UP!" : planes::compass(p.bearing));
  if (overhead) snprintf(text, sizeof(text), "above you, %s", planes::compass(p.bearing));
  else planes::heightShort(text, sizeof(text), p.elev);
  d.setFont(&fonts::Font2);
  d.setTextColor(plAmber());
  d.setCursor(x + 32, 68);
  d.print(fitCurrentFontToWidth(text, w - 32));
  char dist[16], alt[16];
  plDistanceWords(dist, sizeof(dist), p.distM);
  planes::altitudeShort(alt, sizeof(alt), p.altFt, plUnits());
  plTile(x, 87, 62, "AWAY", dist);
  plTile(x + 64, 87, 66, "HEIGHT", alt, plHeightInk(p));
  plWhereLine(x, 114, w, gPlSky->place);
  // A plane overhead that isn't the one shown: U goes to it.
  const int up = plOverhead();
  if (up >= 0 && up != gPlSel) plKeys({{"U", "Look up!"}, {"Enter", "More"}, {",/", "Pages"}});
  else plKeys({{";.", "Pick"}, {"Enter", "More"}, {"L", "List"}, {",/", "Pages"}});
}

const planes::Plane* plSelected() {
  if (gPlSkyOk && gPlSel < gPlSky->count && !strcmp(gPlSky->planes[gPlSel].hex, gPlSelHex)) return &gPlSky->planes[gPlSel];
  return gPlShown.hex[0] ? &gPlShown : nullptr;
}
// Where a plane's going, to fit width in the current font: "DEN to London" (the origin's code, the
// destination's name), then "to London", then the codes alone; "" when no route is known.
String plRouteWords(const planes::Plane& p, int16_t width) {
  auto& d = gCanvas;
  const char* to = p.toCity[0] ? p.toCity : p.to;
  const char* from = p.from[0] ? p.from : p.fromCity;
  if (!to[0]) return from[0] ? fitCurrentFontToWidth(String("from ") + from, width) : String();
  String words = from[0] ? String(from) + " to " + to : String("to ") + to;
  if (d.textWidth(words) <= width) return words;
  words = String("to ") + to;
  if (d.textWidth(words) <= width) return words;
  if (p.from[0] && p.to[0]) {
    words = String(p.from) + " to " + p.to;
    if (d.textWidth(words) <= width) return words;
  }
  return fitCurrentFontToWidth(String("to ") + to, width);
}
void plDrawPlane() {
  auto& d = gCanvas;
  const planes::Plane* sel = plSelected();
  if (!sel) { gPlView = PlView::Sky; plDrawSky(); return; }
  const planes::Plane p = *sel;
  String word = gPlSelGone ? String("out of range now") : planes::isDrone(p) ? String("drone")
                : planes::isHelicopter(p) ? String("helicopter")
                : gPlSkyOk && gPlSky->count ? String(gPlSel + 1) + " of " + gPlSky->count : String("plane");
  const bool overhead = planes::wantsLookUp(p);
  if (overhead && !gPlSelGone) word = "above you";
  plTop(p.name, plAccent(), word);
  // The sky over you: the ring is the horizon and the middle straight overhead.
  plDrawRound(true);
  d.setFont(&fonts::Font0);
  d.setTextColor(plDim());
  d.setCursor(kPlMapX + 8, kPlMapY + kPlMapR - 3);
  d.print("horizon");
  const uint16_t ink = plHeightInk(p);
  int dx, dy;
  planes::skyPoint(p.bearing, p.elev < 0 ? 0 : p.elev, kPlMapR, dx, dy);
  const int16_t planeX = kPlMapX + dx, planeY = kPlMapY + dy;
  int heading = -1;
  if (p.track >= 0 && p.speedKt >= 20) {
    // Its path, flying straight on: two minutes back, faint, and five ahead, bright, with a dot for
    // each minute and an arrow where it reaches the horizon or the five minutes end.
    int px = 0, py = 0;
    bool have = false;
    int16_t fx[32], fy[32];  // The path ahead, from the plane, for its arrow.
    uint8_t fn = 0;
    for (int32_t s = -120; s <= 300; s += 10) {
      const planes::SkySpot spot = planes::skyAt(p, s);
      if (spot.el < 0) { if (s > 0) break; have = false; continue; }
      planes::skyPoint(spot.az, spot.el, kPlMapR, dx, dy);
      const int x = kPlMapX + dx, y = kPlMapY + dy;
      if (have && s <= 0 && (s / 10) % 2) d.drawLine(px, py, x, y, plMuted());  // Dashed: where it came from.
      if (have && s > 0)
        for (int o = -1; o <= 1; ++o) { d.drawLine(px + o, py, x + o, y, ink); d.drawLine(px, py + o, x, y + o, ink); }
      if (s >= 0 && fn < 32) { fx[fn] = int16_t(x); fy[fn] = int16_t(y); ++fn; }
      if (s > 0 && s % 60 == 0) d.fillCircle(x, y, 1, TFT_WHITE);
      if (s == 10) heading = planes::normalDeg(int(lround(atan2(double(x - planeX), double(planeY - y)) * 180.0 / 3.14159265358979)));
      px = x; py = y; have = true;
    }
    // The arrow points the way the path last went: near the horizon a plane flying away hardly
    // moves on the circle, so from a point at least 6 pixels back, not from the one before.
    int k = int(fn) - 2;
    if (fn >= 2) while (k > 0 && abs(fx[k] - fx[fn - 1]) + abs(fy[k] - fy[fn - 1]) < 6) --k;
    if (fn >= 2 && (fx[k] != fx[fn - 1] || fy[k] != fy[fn - 1])) {
      px = fx[fn - 1];
      py = fy[fn - 1];
      const double a = atan2(double(px - fx[k]), double(fy[k] - py)), s = sin(a), c = -cos(a);
      const int bx = px - int(lround(s * 7)), by = py - int(lround(c * 7));
      d.fillTriangle(px + int(lround(s * 3)), py + int(lround(c * 3)), bx - int(lround(c * 6)), by + int(lround(s * 6)),
                     bx + int(lround(c * 6)), by - int(lround(s * 6)), ink);
      // Where it's going, beside the arrow: above it, or below near the top, kept on the map.
      if (p.to[0]) {
        d.setFont(&fonts::Font0);
        const int16_t tw = d.textWidth(p.to);
        double ux = c, uy = -s;  // Square to the path.
        if ((uy > 0) != (py < 40)) { ux = -ux; uy = -uy; }
        int16_t tx = int16_t(px - lround(s * 4) + lround(ux * 11) - tw / 2), ty = int16_t(py - lround(c * 4) + lround(uy * 11) - 4);
        tx = max<int16_t>(1, min<int16_t>(tx, int16_t(kPlCardX - 3 - tw)));
        ty = max<int16_t>(23, min<int16_t>(ty, 114));
        d.fillRect(tx - 1, ty - 1, tw + 2, 10, TFT_BLACK);
        d.setTextColor(ink);
        d.setCursor(tx, ty);
        d.print(p.to);
      }
    }
  }
  if (overhead) plPulse(planeX, planeY, plAmber());
  plGlyph(planeX, planeY, heading, TFT_WHITE, false);
  // Beside it: what it is, where it's going, which way and how high to look, and its numbers.
  const int16_t x = kPlCardX, w = 238 - kPlCardX;
  d.setFont(&fonts::Font2);
  d.setTextColor(p.type[0] ? TFT_WHITE : plMuted());
  d.setCursor(x, 22);
  d.print(fitCurrentFontToWidth(p.type[0] ? p.type : "Type not known", w));
  d.setCursor(x, 38);
  String route = plRouteWords(p, w);
  d.setTextColor(route.length() ? plAccent() : plMuted());
  d.print(route.length() ? route : String("Route not known"));
  char text[48], alt[16], dist[16], speed[16];
  if (overhead) {
    // Look up: big, and which way it's crossing the sky.
    d.setFont(&fonts::FreeSansBold12pt7b);
    d.setTextColor(plFlashOn() ? plAmber() : TFT_WHITE);
    d.setCursor(x, 54);
    d.print("LOOK UP!");
    if (p.track >= 0) snprintf(text, sizeof(text), "from %s, going %s", planes::compass(p.track + 180), planes::compass(p.track));
    else planes::heightShort(text, sizeof(text), p.elev);
    d.setFont(&fonts::Font2);
    d.setTextColor(plAmber());
    d.setCursor(x, 72);
    d.print(fitCurrentFontToWidth(text, w));
  } else {
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(TFT_WHITE);
    d.setCursor(x, 56);
    d.print(fitCurrentFontToWidth(planes::compassWord(p.bearing), w));
    planes::heightShort(text, sizeof(text), p.elev);
    d.setFont(&fonts::Font2);
    d.setTextColor(plAmber());
    d.setCursor(x, 71);
    d.print(fitCurrentFontToWidth(text, w));
  }
  planes::altitudeText(alt, sizeof(alt), p.altFt, plUnits());
  plDistanceWords(dist, sizeof(dist), p.distM);
  plTile(x, 88, 78, "HEIGHT", alt, ink);
  if (p.climbFpm >= 300 || p.climbFpm <= -300) {
    // Climbing or coming down: a small arrow after the height.
    d.setFont(&fonts::Font2);
    const int16_t ax = x + d.textWidth(alt) + 5, ay = 88 + 9 + 8;
    if (p.climbFpm > 0) d.fillTriangle(ax, ay + 3, ax + 6, ay + 3, ax + 3, ay - 3, ink);
    else d.fillTriangle(ax, ay - 3, ax + 6, ay - 3, ax + 3, ay + 3, ink);
  }
  plTile(x + 82, 88, 48, "AWAY", dist);
  planes::speedText(speed, sizeof(speed), p.speedKt, plUnits());
  const char* toPlace = p.toCity[0] ? p.toCity : p.to;
  const char* fromPlace = p.fromCity[0] ? p.fromCity : p.from;
  if (overhead && p.climbFpm <= -300 && toPlace[0]) snprintf(text, sizeof(text), "landing at %s", toPlace);
  else if (overhead && p.climbFpm >= 300 && fromPlace[0]) snprintf(text, sizeof(text), "just left %s", fromPlace);
  else if (p.track >= 0) snprintf(text, sizeof(text), "%s heading %s", speed, planes::compass(p.track));
  else snprintf(text, sizeof(text), "%s", speed);
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(x, 115);
  d.print(fitCurrentFontToWidth(text, w));
  plKeys({{",/", "Other planes"}, {"S", "Keep"}, {"Esc", "Back"}});
}

// Every plane in the ring, nearest first, as the bridge sends them.
void plDrawPlaneList() {
  auto& d = gCanvas;
  char ring[12], text[64];
  planes::rangeLabel(text, sizeof(text), gPlRange, plUnits());
  plSpaced(ring, sizeof(ring), text);
  plTop("Nearest", plAccent(), gPlSkyOk ? String(gPlSky->count) + " within " + ring : String("looking"));
  if (!gPlSkyOk || !gPlSky->count) {
    plCardSays(gPlProblem.length() ? gPlProblem : gPlSkyOk ? String("No planes within ") + ring + " right now." : String("Looking for planes..."),
               gPlProblem.length() ? plAmber() : plMuted(), 5);
    plKeys({{"Esc", "Back"}});
    return;
  }
  const int first = max<int>(0, min<int>(gPlSel - 1, gPlSky->count - kPlRows));
  for (int row = 0; row < kPlRows && first + row < gPlSky->count; ++row) {
    const int i = first + row;
    const planes::Plane& p = gPlSky->planes[i];
    const int16_t y = 24 + row * kPlRowH;
    const bool selected = i == gPlSel;
    if (selected) d.fillRoundRect(2, y, 236, kPlRowH - 1, 4, plAccent());
    char dist[16], high[24], alt[16];
    plDistanceWords(dist, sizeof(dist), p.distM);
    d.setFont(&fonts::Font0);
    d.setTextColor(selected ? TFT_BLACK : plHeightInk(p));
    d.setCursor(6, y + 4);
    d.print(i + 1);
    d.setFont(&fonts::Font2);
    const int16_t distW = d.textWidth(dist);
    d.setTextColor(selected ? TFT_BLACK : plAccent());
    d.setCursor(234 - distW, y);
    d.print(dist);
    d.setTextColor(selected ? TFT_BLACK : TFT_WHITE);
    d.setCursor(20, y);
    d.print(fitCurrentFontToWidth(p.name, 234 - distW - 26));
    planes::heightShort(high, sizeof(high), p.elev);
    planes::altitudeShort(alt, sizeof(alt), p.altFt, plUnits());
    const bool overhead = planes::wantsLookUp(p);
    snprintf(text, sizeof(text), "%s, %s, %s%s%s", overhead ? "LOOK UP" : planes::compass(p.bearing), overhead ? "above you" : high, alt,
             p.type[0] ? ", " : "", p.type);
    d.setFont(&fonts::Font0);
    d.setTextColor(selected ? TFT_BLACK : overhead ? plAmber() : plMuted());
    d.setCursor(20, y + 15);
    d.print(fitCurrentFontToWidth(text, 214));
  }
  plKeys({{";.", "Pick"}, {"Enter", "More"}, {"S", "Keep"}, {"Esc", "Back"}});
}


// Four stars, lit for the pass's brightness.
void plDrawStars(int16_t x, int16_t y, uint8_t lit, bool onBar) {
  auto& d = gCanvas;
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t cx = x + i * 9 + 3, cy = y + 7;
    const uint16_t ink = i < lit ? (onBar ? TFT_BLACK : plGold()) : (onBar ? rgb565_local(150, 120, 40) : plFaint());
    d.fillTriangle(cx, cy - 4, cx - 4, cy + 2, cx + 4, cy + 2, ink);
    d.fillTriangle(cx, cy + 4, cx - 4, cy - 1, cx + 4, cy - 1, ink);
  }
}
void plDrawBell(int16_t x, int16_t y, uint16_t ink) {
  auto& d = gCanvas;
  d.fillCircle(x + 3, y + 4, 3, ink);
  d.fillRect(x, y + 4, 7, 4, ink);
  d.drawFastHLine(x - 1, y + 8, 9, ink);
  d.fillCircle(x + 3, y + 10, 1, ink);
}
bool plSpaceWaiting() {
  if (gPlSpaceOk) return false;
  plCardSays(gPlSpaceProblem.length() ? gPlSpaceProblem
                                      : (wifiLocalAddress() ? String("Asking where the space station is...") : String("Skywatch needs Wi-Fi")),
             gPlSpaceProblem.length() ? plAmber() : plMuted(), 5);
  return true;
}
void plDrawPasses() {
  auto& d = gCanvas;
  plTop("Space station", plGold());
  if (plSpaceWaiting()) { plKeys({{",/", "Pages"}}); return; }
  const uint32_t now = plNowUnix();
  if (!gPlSpace->count) {
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    plWrap("None you can see in the next 10 days.", 5, 26, 230, 16, 2);
    d.setTextColor(plMuted());
    plWrap("It shows near dusk and dawn, when the sky is dark but the station is still in sunlight.", 5, 62, 230, 16, 3);
    plKeys({{",/", "Pages"}});
    return;
  }
  const bool byGps = !strcmp(gPlSpace->place, "Here");
  d.setFont(&fonts::Font0);
  d.setTextColor(byGps ? plGreen() : plAmber());
  d.setCursor(5, 24);
  d.print(fitCurrentFontToWidth(byGps ? String("When you can see it here, by GPS")
                                      : String("When you can see it from ") + gPlSpace->place + ", no GPS", 230));
  const int first = max<int>(0, min<int>(gPlPassSel - 1, gPlSpace->count - 4));
  for (int row = 0; row < 4 && first + row < gPlSpace->count; ++row) {
    const int i = first + row;
    const planes::Pass& p = gPlSpace->passes[i];
    const int16_t y = 34 + row * 19;
    const bool selected = i == gPlPassSel;
    const bool over = now && now >= p.end;
    if (selected) d.fillRoundRect(2, y - 1, 236, 18, 4, plGold());
    const uint16_t ink = selected ? TFT_BLACK : over ? plDim() : TFT_WHITE;
    char day[16], at[16], len[12];
    plDayOf(day, sizeof(day), p.start);
    plTimeOf(at, sizeof(at), p.start);
    snprintf(len, sizeof(len), "%lu min", static_cast<unsigned long>(planes::minutesLong(p)));
    d.setFont(&fonts::Font2);
    d.setTextColor(ink);
    d.setCursor(7, y);
    d.print(fitCurrentFontToWidth(now && now >= p.start && now < p.end ? String("Up now!") : String(day), 72));
    d.setCursor(82, y);
    d.print(at);
    d.setTextColor(selected ? TFT_BLACK : plMuted());
    d.setCursor(148, y);
    d.print(len);
    if (plReminded(p)) plDrawBell(188, y + 2, selected ? TFT_BLACK : plGreen());
    plDrawStars(200, y, planes::stars(p.magTenths), selected);
  }
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(5, 112);
  d.print("More stars: brighter and higher");
  plKeys({{";.", "Pick"}, {"Enter", "Path"}, {"R", "Remind"}, {",/", "Pages"}});
}

const planes::Pass* plPassShown() {
  if (gPlPassFromReminder) return &gPlGoPass;
  if (gPlSpaceOk && gPlPassSel < gPlSpace->count) return &gPlSpace->passes[gPlPassSel];
  return nullptr;
}
void plDrawPass() {
  auto& d = gCanvas;
  const planes::Pass* shown = plPassShown();
  if (!shown) { gPlView = PlView::Passes; plDrawPasses(); return; }
  const planes::Pass p = *shown;
  char day[16], until[24];
  plDayOf(day, sizeof(day), p.start);
  planes::untilText(until, sizeof(until), plNowUnix(), p);
  plTop(day, plGold(), until);
  plDrawRound(true);
  // The path: the bridge's points, or the start, top and end when there are none.
  int16_t az[planes::kPathPoints];
  int8_t el[planes::kPathPoints];
  uint8_t n = p.points;
  if (n >= 2) {
    for (uint8_t i = 0; i < n; ++i) { az[i] = p.pathAz[i]; el[i] = p.pathEl[i]; }
  } else {
    az[0] = p.startAz; el[0] = int8_t(max<int>(p.startEl, 10));
    az[1] = p.peakAz; el[1] = p.peakEl;
    az[2] = p.endAz; el[2] = int8_t(max<int>(p.endEl, 10));
    n = 3;
  }
  int px = 0, py = 0;
  for (uint8_t i = 0; i < n; ++i) {
    int dx, dy;
    planes::skyPoint(az[i], el[i], kPlMapR, dx, dy);
    const int x = kPlMapX + dx, y = kPlMapY + dy;
    if (i) {
      d.drawLine(px, py, x, y, plGold());
      d.drawLine(px + 1, py, x + 1, y, plGold());
      d.drawLine(px, py + 1, x, y + 1, plGold());
    }
    px = x; py = y;
  }
  const auto mark = [&](int16_t x, int16_t y, char label) {
    d.fillCircle(x, y, 6, TFT_WHITE);
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_BLACK);
    d.setCursor(x - 2, y - 3);
    d.print(label);
  };
  const auto markSky = [&](int azDeg, int elDeg, char label) {
    int dx, dy;
    planes::skyPoint(azDeg, elDeg, kPlMapR, dx, dy);
    mark(int16_t(kPlMapX + dx), int16_t(kPlMapY + dy), label);
  };
  markSky(az[0], el[0], '1');
  markSky(p.peakAz, p.peakEl, '2');
  markSky(az[n - 1], el[n - 1], '3');
  // Beside it, the same three: when, and where to look.
  char at[16], words[40], height[24];
  const auto entry = [&](int16_t y, char label, uint32_t when, const char* text) {
    mark(117, int16_t(y + 8), label);
    plTimeOf(at, sizeof(at), when);
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(129, y);
    d.print(at);
    d.setTextColor(plAmber());
    d.setCursor(129, y + 16);
    d.print(fitCurrentFontToWidth(text, 236 - 129));
  };
  snprintf(words, sizeof(words), "rises %s", planes::compass(p.startAz));
  entry(23, '1', p.start, words);
  planes::heightShort(height, sizeof(height), p.peakEl);
  snprintf(words, sizeof(words), "%s %s", height, planes::compass(p.peakAz));
  entry(56, '2', p.peak, words);
  snprintf(words, sizeof(words), "%s %s", p.shadow ? "vanishes" : "sets", planes::compass(p.endAz));
  entry(89, '3', p.end, words);
  String remind = "Remind me";
  if (plReminded(p)) { plTimeOf(at, sizeof(at), gPlRemindAt); remind = String("Rings at ") + at; }
  if (gPlPassFromReminder) plKeys({{"R", remind}, {"Esc", "Back"}});
  else plKeys({{"R", remind}, {",/", "Other nights"}, {"Esc", "Back"}});
}

void plDrawNow() {
  auto& d = gCanvas;
  plTop("Station now", plGold());
  if (plSpaceWaiting()) { plKeys({{",/", "Pages"}}); return; }
  const planes::Station& s = gPlSpace->now;
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(5, 24);
  d.print("THE SPACE STATION IS OVER");
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(TFT_WHITE);
  d.setCursor(5, 34);
  d.print(fitCurrentFontToWidth(s.known && s.over[0] ? String(s.over) : String("the Earth"), 230));
  const bool us = plUnits() == units::System::US;
  char number[16], text[48];
  planes::grouped(number, sizeof(number), us ? units::rounded(s.altKm * 0.621371) : s.altKm);
  plTile(5, 56, 66, "HEIGHT", String(number) + (us ? " mi" : " km"), plGold());
  planes::grouped(number, sizeof(number), us ? units::rounded(s.speedKmh * 0.621371 / 100.0) * 100 : units::rounded(s.speedKmh / 100.0) * 100);
  plTile(74, 56, 88, "SPEED", String(number) + (us ? " mph" : " km/h"), plGold());
  snprintf(text, sizeof(text), "%lu min", static_cast<unsigned long>(planes::orbitMinutes(s.altKm)));
  plTile(165, 56, 72, "ROUND EARTH", text, plGold());
  d.setFont(&fonts::Font2);
  const int next = planes::nextPass(*gPlSpace, plNowUnix());
  if (next >= 0) {
    char day[16], at[16];
    plDayOf(day, sizeof(day), gPlSpace->passes[next].start);
    plTimeOf(at, sizeof(at), gPlSpace->passes[next].start);
    day[0] = char(tolower(static_cast<unsigned char>(day[0])));
    d.setTextColor(plAmber());
    d.setCursor(5, 86);
    d.print(fitCurrentFontToWidth(String("Over you next: ") + day + ", " + at, 230));
  }
  if (s.crew > 0) {
    d.setTextColor(TFT_WHITE);
    d.setCursor(5, 104);
    d.print(String(s.crew) + (s.crew == 1 ? " person is" : " people are") + " on board");
  }
  plKeys({{"R", "Refresh"}, {",/", "Pages"}});
}

void plDrawSpotted() {
  auto& d = gCanvas;
  plReadSpotted();
  plTop("Spotted", plGreen());
  if (!gSdReady || !gPlStats || !gPlStats->total) {
    d.setFont(&fonts::Font2);
    if (!gSdReady) { d.setTextColor(plAmber()); plWrap("Spotted keeps what you saw on the SD card. Put one in to start.", 5, 26, 230, 16, 4); }
    else if (!gPlStats) { d.setTextColor(plAmber()); plWrap("Not enough memory to read the collection", 5, 26, 230, 16, 3); }
    else {
      d.setTextColor(TFT_WHITE);
      plWrap("Nothing spotted yet.", 5, 26, 230, 16, 1);
      d.setTextColor(plMuted());
      plWrap("When you see a plane or a drone, open it and press S. It joins your collection.", 5, 48, 230, 16, 4);
    }
    plKeys({{",/", "Pages"}});
    return;
  }
  const planes::Stats& s = *gPlStats;
  plTile(5, 23, 76, "SEEN", String(s.total), plGreen());
  plTile(83, 23, 76, "AIRLINES", String(s.airlines.count), plGreen());
  plTile(161, 23, 76, "TYPES", String(s.types.count), plGreen());
  const auto row = [&](int16_t y, const char* label, uint16_t labelInk, const String& what, const String& right) {
    d.setFont(&fonts::Font0);
    d.setTextColor(labelInk);
    d.setCursor(5, y + 4);
    d.print(label);
    d.setFont(&fonts::Font2);
    const int16_t rightW = right.length() ? d.textWidth(right) + 6 : 0;
    d.setTextColor(TFT_WHITE);
    d.setCursor(64, y);
    d.print(fitCurrentFontToWidth(what, 236 - 64 - rightW));
    if (right.length()) {
      d.setTextColor(plMuted());
      d.setCursor(236 - d.textWidth(right), y);
      d.print(right);
    }
  };
  const int newest = s.newestType();
  if (newest >= 0) {
    tm now, first;
    const bool today = plLocal(plNowUnix(), now) && plLocal(s.types.first[newest], first) && now.tm_yday == first.tm_yday &&
                       now.tm_year == first.tm_year;
    char day[16];
    plDayOf(day, sizeof(day), s.types.first[newest]);
    row(54, today ? "NEW TODAY" : "NEWEST", today ? plAmber() : plMuted(), s.types.names[newest], today ? String() : String(day));
  }
  char alt[16];
  planes::altitudeShort(alt, sizeof(alt), s.highest.altFt, plUnits());
  row(73, "HIGHEST", plMuted(), s.highest.type[0] ? s.highest.type : s.highest.name, alt);
  const int top = s.airlines.top();
  if (top >= 0) row(92, "MOST SEEN", plMuted(), s.airlines.names[top], String("x") + s.airlines.counts[top]);
  else {
    planes::altitudeShort(alt, sizeof(alt), s.lowest.altFt, plUnits());
    row(92, "LOWEST", plMuted(), s.lowest.type[0] ? s.lowest.type : s.lowest.name, alt);
  }
  plKeys({{"Enter", "Every sighting"}, {",/", "Pages"}});
}
void plDrawSightings() {
  auto& d = gCanvas;
  plReadSpotted();
  plTop("Spotted", plGreen(), gPlStats ? String(gPlStats->total) + " seen" : String("seen"));
  if (!gPlRecent || !gPlRecentCount) { gPlView = PlView::Spotted; return; }
  gPlRecentTop = uint8_t(min<int>(gPlRecentTop, max<int>(0, gPlRecentCount - kPlRows)));
  for (int row = 0; row < kPlRows && gPlRecentTop + row < gPlRecentCount; ++row) {
    const planes::Sighting& s = gPlRecent[gPlRecentTop + row];
    const int16_t y = 24 + row * kPlRowH;
    char day[16], at[16];
    plDayOf(day, sizeof(day), s.at);
    plTimeOf(at, sizeof(at), s.at);
    const String when = String(day) + " " + at;
    d.setFont(&fonts::Font0);
    d.setTextColor(plMuted());
    const int16_t whenX = 236 - d.textWidth(when);
    d.setCursor(whenX, y + 4);
    d.print(when);
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(5, y);
    d.print(fitCurrentFontToWidth(s.name, whenX - 11));
    d.setFont(&fonts::Font0);
    d.setTextColor(plAccent());
    d.setCursor(5, y + 15);
    d.print(fitCurrentFontToWidth(s.type[0] ? s.type : "type not known", 230));
  }
  plKeys({{";.", "Scroll"}, {"Esc", "Back"}});
}

void plDrawGoOut() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  const planes::Pass& p = gPlGoPass;
  const uint32_t now = plNowUnix();
  const bool flash = (millis() / 500) % 2 == 0;
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(flash ? plGold() : TFT_WHITE);
  plCentered("GO OUTSIDE NOW!", 6);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  char line[64];
  if (now && now < p.start) {
    const uint32_t min = (p.start - now + 59) / 60;
    snprintf(line, sizeof(line), "The space station rises in the %s", planes::compassWord(p.startAz));
    plCentered(line, 38);
    snprintf(line, sizeof(line), "in %lu min", static_cast<unsigned long>(min));
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(plGold());
    plCentered(line, 55);
  } else if (now < p.end) {
    snprintf(line, sizeof(line), "It's up now, look %s!", planes::compassWord(now < p.peak ? p.startAz : p.endAz));
    plCentered(line, 46);
  } else {
    plCentered("It has gone by. Next time!", 46);
  }
  d.setFont(&fonts::Font2);
  d.setTextColor(plMuted());
  plCentered("A bright dot moving fast,", 77);
  plCentered("and it doesn't blink.", 93);
  plKeys({{"Enter", "Path"}, {"Esc", "Close"}});
}

// --- Drones --------------------------------------------------------------------------------
bool plDroneView() { return gPlView == PlView::Drones || gPlView == PlView::Drone; }
bool plDroneListening() { return gPlDroneState == PlDroneState::Listening; }
void plDroneGiveBack() {
  if (!gPlDroneHold) return;
  gPlDroneHold = false;
  if (gPlDroneWifiWasOff) WiFi.mode(WIFI_OFF);
  else beginWifiStationConnect("Wi-Fi back on");
}
void plDroneStop() {
  if (gPlDroneState == PlDroneState::Off) return;
  const bool was = plDroneListening();
  drone_listen::stop();
  plDroneGiveBack();
  delete gPlDrones;
  gPlDrones = nullptr;
  delete gPlDroneFrame;
  gPlDroneFrame = nullptr;
  gPlDroneState = PlDroneState::Off;
  gPlNearDueMs = gPlIssDueMs = millis() + 4000;  // Wi-Fi rejoins first.
  if (was) appendRuntimeLog("PLANES", String("drones stop frames=") + drone_listen::frames() + " lost=" + drone_listen::dropped());
  plInvalidate();
}
// Enter on the page: a request under way ends first, then Wi-Fi is taken.
void plDroneAsk() {
  if (gPlDroneState != PlDroneState::Off) return;
  gPlDroneState = PlDroneState::Waiting;
  if (camera_net::busy()) camera_net::cancel();
  plInvalidate();
}
void plDroneTake() {
  if (camera_net::busy() || camera_net::finished() || !networkWorkIdle()) return;  // Next time round.
  if (!gPlDrones) gPlDrones = new (std::nothrow) drone_id::Table();
  if (!gPlDroneFrame) gPlDroneFrame = new (std::nothrow) drone_listen::Frame();
  if (!gPlDrones || !gPlDroneFrame) {
    plDroneStop();
    plSay("Not enough memory to listen", plAmber());
    return;
  }
  gPlDroneHold = true;
  gPlDroneWifiWasOff = WiFi.getMode() == WIFI_OFF;
  if (gPlDroneWifiWasOff) WiFi.mode(WIFI_STA);
  else WiFi.disconnect(false, false);
  gWifiReady = false;
  if (!drone_listen::start()) {
    plDroneStop();
    plSay("Wi-Fi wouldn't listen", plAmber());
    appendRuntimeLog("PLANES", "drones start failed", true);
    return;
  }
  gPlDroneState = PlDroneState::Listening;
  gPlDroneSince = millis();
  gPlDroneSel = 0;
  appendRuntimeLog("PLANES", "drones listening");
  plInvalidate();
}
const drone_id::Heard* plDroneByMac(const uint8_t mac[6]) {
  if (!gPlDrones) return nullptr;
  const int i = gPlDrones->byMac(mac);
  return i < 0 ? nullptr : &gPlDrones->heard[i];
}
// The drones heard, strongest signal first: about nearest first, without a GPS.
uint8_t plDroneOrder(uint8_t* order) {
  if (!gPlDrones) return 0;
  const uint8_t n = gPlDrones->count;
  for (uint8_t i = 0; i < n; ++i) order[i] = i;
  for (uint8_t i = 1; i < n; ++i)
    for (uint8_t j = i; j && gPlDrones->heard[order[j]].rssi > gPlDrones->heard[order[j - 1]].rssi; --j) {
      const uint8_t k = order[j]; order[j] = order[j - 1]; order[j - 1] = k;
    }
  return n;
}
// Where a drone is from here: needs the Cap's fix and the drone's position.
bool plDroneFrom(const drone_id::Drone& dr, int32_t& distM, int& bearing, int& elev) {
  if (!plUsingGps() || !dr.located) return false;
  const int32_t latE5 = dr.latE7 / 100, lonE5 = dr.lonE7 / 100;
  distM = lab_gps::metersBetween(gPlHere.latE5, gPlHere.lonE5, latE5, lonE5);
  bearing = lab_gps::bearingDeg(gPlHere.latE5, gPlHere.lonE5, latE5, lonE5);
  if (bearing < 0) bearing = 0;
  int32_t height = 0;
  elev = drone_id::heightMeters(dr, height) ? int(lround(atan2(double(height), double(distM > 1 ? distM : 1)) * 180.0 / 3.14159265358979)) : -1;
  return true;
}
// "590ft", "0.6mi"; "180m", "1.2km": short, for the list.
void plDroneDistance(char* out, size_t room, int32_t meters) {
  if (plUnits() == units::System::US) {
    const int32_t ft = units::feet(meters);
    if (ft < 1000) snprintf(out, room, "%ldft", static_cast<long>(ft));
    else { char n[12]; units::tenthsText(n, sizeof(n), units::rounded(meters / 160.9344)); snprintf(out, room, "%smi", n); }
  } else if (meters < 1000) {
    snprintf(out, room, "%ldm", static_cast<long>(meters));
  } else {
    char n[12]; units::tenthsText(n, sizeof(n), units::rounded(meters / 100.0)); snprintf(out, room, "%skm", n);
  }
}
// The same with a space before the unit, for sentences: "591 ft", "0.4 mi", "180 m".
void plDroneDistanceWords(char* out, size_t room, int32_t meters) {
  char compact[16];
  plDroneDistance(compact, sizeof(compact), meters);
  plSpaced(out, room, compact);
}
void plDroneHeight(char* out, size_t room, int32_t meters, bool compact) {
  const bool us = plUnits() == units::System::US;
  snprintf(out, room, compact ? "%ld%s" : "%ld %s", static_cast<long>(us ? units::feet(meters) : meters), us ? "ft" : "m");
}
// A drone heard for the first time chimes, softly, and flashes for a while; nothing opens.
void plDroneNew(const drone_id::Heard&) {
  if (gPlDroneLastChimeMs && millis() - gPlDroneLastChimeMs < kPlDroneChimeGapMs) return;
  gPlDroneLastChimeMs = millis();
  plWakeScreen();
  plChime(planes::Chime::Drone);
}
bool plDroneFresh(const drone_id::Heard& h) { return millis() - h.firstMs < kPlFreshMs; }
void plDroneService(uint32_t now) {
  if (gPlDroneState != PlDroneState::Off && !plDroneView()) { plDroneStop(); return; }
  if (gPlDroneState == PlDroneState::Waiting) { plDroneTake(); return; }
  if (!plDroneListening()) return;
  drone_listen::service(now);
  for (int n = 0; n < 6 && drone_listen::take(*gPlDroneFrame); ++n) {
    const drone_listen::Frame& f = *gPlDroneFrame;
    uint8_t mac[6];
    const uint8_t* pack = nullptr;
    size_t packLen = 0;
    drone_id::Via via;
    if (!drone_id::findPack(f.data, f.length, mac, pack, packLen, via)) continue;
    const int i = gPlDrones->hear(mac, f.rssi, f.channel, via, pack, packLen, now);
    if (i < 0) continue;
    drone_listen::heardOn(f.channel, now);
    drone_id::Heard& h = gPlDrones->heard[i];
    if (h.frames == 1)
      appendRuntimeLog("PLANES", String("drone heard ch") + f.channel + (via == drone_id::Via::Nan ? " nan " : " beacon ") + h.rssi + "dBm");
    if (!h.alerted) { h.alerted = true; plDroneNew(h); }
  }
  gPlDrones->forget(now);
}
void plSaveDrone(const drone_id::Heard& h) {
  planes::Plane p;
  snprintf(p.hex, sizeof(p.hex), "%02x%02x%02x", h.d.mac[3], h.d.mac[4], h.d.mac[5]);
  drone_id::shortName(p.name, sizeof(p.name), h.d);
  snprintf(p.type, sizeof(p.type), "%s", drone_id::typeName(h.d.uaType));
  int32_t m = 0;
  p.altFt = drone_id::heightMeters(h.d, m) ? units::feet(m) : 0;
  plSaveSighting(p);
}

void plDrawDrones() {
  auto& d = gCanvas;
  plTop("Drones", plPink());
  plDrawRound(false);
  if (!plDroneListening()) {
    d.setFont(&fonts::Font2);
    if (gPlDroneState == PlDroneState::Waiting) {
      d.setTextColor(plMuted());
      plWrap("Getting Wi-Fi ready to listen...", kPlCardX, 26, 236 - kPlCardX, 16, 4);
      plKeys({{"Esc", "Stop"}, {",/", "Pages"}});
      return;
    }
    d.setTextColor(TFT_WHITE);
    plWrap("Listen for drones nearby?", kPlCardX, 26, 236 - kPlCardX, 16, 2);
    d.setTextColor(plMuted());
    plWrap("The internet pauses while it listens.", kPlCardX, 66, 236 - kPlCardX, 16, 3);
    plKeys({{"Enter", "Listen"}, {",/", "Pages"}});
    return;
  }
  // A sweep goes round while it listens.
  const double a = (millis() % 3000) / 3000.0 * 2 * 3.14159265358979;
  d.drawLine(kPlMapX, kPlMapY, kPlMapX + int(lround(sin(a) * (kPlMapR - 2))), kPlMapY - int(lround(cos(a) * (kPlMapR - 2))),
             rgb565_local(150, 50, 120));
  uint8_t order[drone_id::Table::kMax];
  const uint8_t n = plDroneOrder(order);
  if (gPlDroneSel >= n) gPlDroneSel = n ? n - 1 : 0;
  // The ring: 300 m, 1 km or 3 km, whichever holds them all.
  int32_t ring = 300;
  for (uint8_t i = 0; i < n; ++i) {
    int32_t dist; int bearing, elev;
    if (plDroneFrom(gPlDrones->heard[order[i]].d, dist, bearing, elev)) while (ring < 3000 && dist > ring * 9 / 10) ring = ring == 300 ? 1000 : 3000;
  }
  char text[48], a1[16], a2[16];
  plDroneDistanceWords(text, sizeof(text), ring);
  d.setFont(&fonts::Font0);
  d.setTextColor(plDim());
  d.setCursor(kPlMapX + kPlMapR - d.textWidth(text), kPlMapY + kPlMapR - 4);
  d.print(text);
  for (int i = n - 1; i >= 0; --i) {
    const drone_id::Drone& dr = gPlDrones->heard[order[i]].d;
    int32_t dist; int bearing, elev;
    if (!plDroneFrom(dr, dist, bearing, elev)) continue;
    int dx, dy;
    planes::mapPoint(uint32_t(dist), bearing, uint32_t(ring), kPlMapR - 6, dx, dy);
    const bool fresh = plDroneFresh(gPlDrones->heard[order[i]]);
    if (fresh) plPulse(int16_t(kPlMapX + dx), int16_t(kPlMapY + dy), plPink());
    plGlyph(int16_t(kPlMapX + dx), int16_t(kPlMapY + dy), dr.speedCms >= 50 ? dr.direction : -1,
            fresh && !plFlashOn() ? TFT_WHITE : plPink(), i == gPlDroneSel);
  }
  const int16_t x = kPlCardX, w = 238 - kPlCardX;
  if (!n) {
    const uint32_t secs = (millis() - gPlDroneSince) / 1000;
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(x, 26);
    d.print(secs < 60 ? String("Listening...") : String("Listening ") + (secs / 60) + " min");
    d.setTextColor(plMuted());
    plWrap("No drones yet. Only ones nearby can be heard.", x, 46, w - 2, 16, 4);
    plWhereLine(x, 114, w, nullptr);
    plKeys({{"Esc", "Stop"}, {",/", "Pages"}});
    return;
  }
  const drone_id::Heard& h = gPlDrones->heard[order[gPlDroneSel]];
  char name[24];
  drone_id::shortName(name, sizeof(name), h.d);
  int16_t nameW = w;
  if (plDroneFresh(h)) {
    // Just heard: NEW beside its name, flashing.
    d.setFont(&fonts::Font0);
    const int16_t tagW = d.textWidth("NEW") + 6;
    d.fillRoundRect(238 - tagW, 25, tagW, 11, 3, plFlashOn() ? plPink() : plDim());
    d.setTextColor(TFT_BLACK);
    d.setCursor(238 - tagW + 3, 27);
    d.print("NEW");
    nameW = w - tagW - 4;
  }
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  d.setCursor(x, 23);
  d.print(fitCurrentFontToWidth(name, nameW));
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(x, 40);
  d.print(fitCurrentFontToWidth(drone_id::typeName(h.d.uaType), w));
  int32_t distM = 0, m = 0;
  int bearing = 0, elev = -1;
  const bool where = plDroneFrom(h.d, distM, bearing, elev);
  if (where) {
    plDial(x + 13, 64, 13, bearing, plPink());
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(TFT_WHITE);
    d.setCursor(x + 32, 52);
    d.print(planes::compass(bearing));
    if (elev >= 0) planes::heightShort(text, sizeof(text), elev);
    else snprintf(text, sizeof(text), "height?");
    d.setFont(&fonts::Font2);
    d.setTextColor(plAmber());
    d.setCursor(x + 32, 68);
    d.print(fitCurrentFontToWidth(text, w - 32));
  } else {
    plBars(x, 55, h.rssi, plPink());
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(x + 26, 52);
    d.print(String(drone_id::signalWord(h.rssi)) + " signal");
    d.setTextColor(plMuted());
    d.setFont(&fonts::Font0);
    d.setCursor(x, 72);
    d.print(fitCurrentFontToWidth("No GPS for which way", w));
  }
  if (drone_id::heightMeters(h.d, m)) plDroneHeight(a1, sizeof(a1), m, false);
  else snprintf(a1, sizeof(a1), "?");
  if (where) plDroneDistanceWords(a2, sizeof(a2), distM);
  plTile(x, 87, 62, "HEIGHT", a1, plPink());
  if (where) plTile(x + 64, 87, 66, "AWAY", a2);
  else plTile(x + 64, 87, 66, "STATUS", h.d.status ? drone_id::statusWord(h.d.status) : "?");
  snprintf(text, sizeof(text), "%u of %u heard", unsigned(gPlDroneSel + 1), unsigned(n));
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(x, 114);
  d.print(text);
  plKeys({{";.", "Pick"}, {"Enter", "More"}, {"S", "Keep"}, {",/", "Pages"}});
}

void plDrawDrone() {
  auto& d = gCanvas;
  const drone_id::Heard* h = plDroneByMac(gPlDroneShown);
  if (!h) { gPlView = PlView::Drones; plDrawDrones(); return; }
  const drone_id::Drone& dr = h->d;
  char name[24], line[72], a[16], b[16], c[16];
  drone_id::shortName(name, sizeof(name), dr);
  plTop(name, plPink(), dr.status ? drone_id::statusWord(dr.status) : "drone");
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  d.setCursor(5, 23);
  String kind = drone_id::typeName(dr.uaType);
  if (dr.selfId[0]) kind += String("  \"") + dr.selfId + "\"";
  d.print(fitCurrentFontToWidth(kind, 230));
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(5, 41);
  d.print(fitCurrentFontToWidth(dr.id[0] ? String("ID ") + dr.id : String("No ID heard yet"), 230));
  int32_t distM = 0, m = 0;
  int bearing = 0, elev = -1;
  const bool where = plDroneFrom(dr, distM, bearing, elev);
  if (where) {
    plDial(19, 66, 13, bearing, plPink());
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(TFT_WHITE);
    d.setCursor(40, 52);
    d.print(planes::compassWord(bearing));
    d.setFont(&fonts::Font2);
    d.setTextColor(plAmber());
    d.setCursor(40, 68);
    if (elev >= 0) { planes::heightWords(line, sizeof(line), elev); d.print(line); }
    else d.print("how high isn't known");
  } else {
    plBars(5, 55, h->rssi, plPink());
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(31, 52);
    d.print(String(drone_id::signalWord(h->rssi)) + " signal");
    d.setTextColor(plMuted());
    d.setCursor(5, 69);
    d.print(fitCurrentFontToWidth(dr.located ? "Which way to look needs the GPS" : "It hasn't said where it is", 230));
  }
  if (drone_id::heightMeters(dr, m)) plDroneHeight(a, sizeof(a), m, false);
  else snprintf(a, sizeof(a), "?");
  if (where) plDroneDistanceWords(b, sizeof(b), distM);
  else snprintf(b, sizeof(b), "?");
  if (dr.speedCms >= 0) {
    const bool us = plUnits() == units::System::US;
    snprintf(c, sizeof(c), "%ld %s", static_cast<long>(units::rounded(dr.speedCms * (us ? 0.0223694 : 0.036))), us ? "mph" : "km/h");
  } else {
    snprintf(c, sizeof(c), "?");
  }
  plTile(5, 88, 72, "HEIGHT", a, plPink());
  plTile(80, 88, 74, "AWAY", b);
  plTile(157, 88, 80, dr.speedCms >= 50 && dr.direction >= 0 ? (String("SPEED, TO ") + planes::compass(dr.direction)).c_str() : "SPEED", c);
  line[0] = 0;
  if (dr.hasOperator && dr.located) {
    const int32_t apart = lab_gps::metersBetween(dr.latE7 / 100, dr.lonE7 / 100, dr.opLatE7 / 100, dr.opLonE7 / 100);
    const int toward = lab_gps::bearingDeg(dr.latE7 / 100, dr.lonE7 / 100, dr.opLatE7 / 100, dr.opLonE7 / 100);
    plDroneDistanceWords(b, sizeof(b), apart);
    const char* who = dr.operatorType == 0 ? "Took off" : "Pilot";
    if (toward < 0 || apart < 10) snprintf(line, sizeof(line), "%s right below it", who);
    else snprintf(line, sizeof(line), "%s %s to its %s", who, b, planes::compass(toward));
  }
  const String heard = String("heard ") + ((millis() - h->lastMs) / 1000) + "s ago";
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(5, 114);
  d.print(fitCurrentFontToWidth(line[0] ? String(line) + ", " + heard : heard, 230));
  plKeys({{",/", "Other drones"}, {"S", "Keep"}, {"Esc", "Back"}});
}


// --- The app's life ------------------------------------------------------------------------
bool planesHoldsWifi() { return gPlDroneHold; }

void beginPlanes() {
  if (!gPlSky) gPlSky = new (std::nothrow) planes::Sky();
  if (!gPlSpace) gPlSpace = new (std::nothrow) planes::Space();
  gPlView = gPlOpenOn;
  gPlOpenOn = PlView::Sky;
  gPlBack = PlView::Sky;
  gPlSkyOk = gPlSpaceOk = false;
  gPlSel = 0;
  gPlSelHex[0] = 0;
  gPlShown = planes::Plane{};
  gPlSelGone = false;
  if (gPlView != PlView::GoOut) gPlPassFromReminder = false;
  gPlJob = PlJob::None;
  gPlNearDueMs = gPlIssDueMs = millis();
  gPlProblem = gPlSpaceProblem = gPlNotice = "";
  gPlNoticeUntil = 0;
  gPlReport[0] = 0;
  gPlRetriedTicket = false;
  plFreeSpotted();
  plLoadReminder();
  camera_net::setBridge(gHubHost.c_str(), gHubPort, gDeviceToken.c_str(), kTlsRootCaPem);
  gPlOpenSince = millis();
  if (!gPlSky || !gPlSpace) gPlProblem = gPlSpaceProblem = "Not enough memory for Skywatch";
  else camera_net::open();  // Or once the last app's worker has gone: servicePlanes tries again.
  if (!gPlHoldsGps) { gpsHold(kGpsForPlanes); gPlHoldsGps = true; }
  appendRuntimeLog("PLANES", "open");
  plInvalidate();
}
void stopPlanes() {
  plDroneStop();  // Wi-Fi goes back to its network.
  camera_net::close();
  delete gPlSky;
  gPlSky = nullptr;
  delete gPlSpace;
  gPlSpace = nullptr;
  plFreeSpotted();
  gPlSkyOk = gPlSpaceOk = false;
  gPlJob = PlJob::None;
  gPlView = PlView::Sky;
  gPlPassFromReminder = false;
  if (gPlHoldsGps) { gpsRelease(kGpsForPlanes); gPlHoldsGps = false; }
}

void servicePlanes() {
  if (gUiMode != UiMode::Planes) return;
  const uint32_t now = millis();
  if (gPlHoldsGps) {
    gpsFeed(now);
    if (gLabGps.hearing(now) && gLabGps.fix.valid) {
      gPlHere.known = true;
      gPlHere.latE5 = gLabGps.fix.latE5;
      gPlHere.lonE5 = gLabGps.fix.lonE5;
      gPlHere.atMs = now;
    }
  }
  plDroneService(now);
  if (!camera_net::opened()) {
    static uint32_t lastTry = 0;
    if (gPlSky && gPlSpace && now - lastTry > 300) {
      lastTry = now;
      if (camera_net::open()) { gPlProblem = gPlSpaceProblem = ""; plInvalidate(); }
      else if (now - gPlOpenSince > 3000 && gPlProblem.isEmpty()) {
        gPlProblem = gPlSpaceProblem = String("Not enough memory for Skywatch (") + camera_net::memoryNote() + ")";
        plInvalidate();
      }
    }
    return;
  }
  if (camera_net::finished()) plFinished();
  if (!camera_net::busy() && !camera_net::finished()) plNextJob();
  // The station page follows its pass's countdown; a pass in progress keeps the screen awake.
  if (gPlView == PlView::GoOut) {
    const uint32_t unix = plNowUnix();
    if (unix && unix < gPlGoPass.end) gLastUserActivityMs = now;
  }
}

// Each step goes back one: a plane, a pass or the list of sightings to its page, the notices to
// where they came up over, and the other pages to the sky map, whose Esc leaves the app.
bool planesEscape() {
  switch (gPlView) {
    case PlView::GoOut: gPlView = PlView::Passes; gPlPassFromReminder = false; break;
    case PlView::Plane: gPlView = gPlPlaneBack == PlView::PlaneList ? PlView::PlaneList : PlView::Sky; break;
    case PlView::PlaneList: gPlView = PlView::Sky; break;
    case PlView::Pass: gPlView = PlView::Passes; gPlPassFromReminder = false; break;
    case PlView::Sightings: gPlView = PlView::Spotted; break;
    case PlView::Drone: gPlView = PlView::Drones; break;
    case PlView::Drones: plDroneStop(); gPlView = PlView::Sky; break;
    case PlView::Passes: case PlView::Now: case PlView::Spotted: gPlView = PlView::Sky; plFreeSpotted(); break;
    case PlView::Sky: return false;
  }
  plInvalidate();
  return true;
}

void plOpenPage(uint8_t page) {
  static const PlView kPages[5] = {PlView::Sky, PlView::Passes, PlView::Now, PlView::Spotted, PlView::Drones};
  gPlView = kPages[page % 5];
  if (gPlView != PlView::Spotted) plFreeSpotted();
  if (gPlView == PlView::Now && gPlSpaceOk && millis() - gPlSpaceAtMs > kPlNowEveryMs) gPlIssDueMs = millis();
  plInvalidate();
}
// , and / step through the five pages, round and round.
void plFlip(bool left) { plOpenPage(uint8_t((plPage() + (left ? 4 : 1)) % 5)); }
void plSelectPlane(int i) {
  if (!gPlSkyOk || !gPlSky->count) return;
  gPlSel = uint8_t((i + gPlSky->count) % gPlSky->count);
  strcpy(gPlSelHex, gPlSky->planes[gPlSel].hex);
  gPlShown = gPlSky->planes[gPlSel];
  gPlSelGone = false;
  plInvalidate();
}

bool handlePlanesKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt) return false;
  const bool enter = keys.enter || key == '\n' || key == '\r';
  const bool up = key == ';' || key == char(1), down = key == '.' || key == char(2);
  const bool left = key == ',' || key == char(3), right = key == '/' || key == char(4);
  const char lower = char(tolower(static_cast<unsigned char>(key)));
  markUserActivity();
  switch (gPlView) {
    case PlView::Sky:
      if (up) plSelectPlane(gPlSel - 1);
      else if (down) plSelectPlane(gPlSel + 1);
      else if (left || right) plFlip(left);
      else if (enter && gPlSkyOk && gPlSky->count) { plSelectPlane(gPlSel); gPlPlaneBack = PlView::Sky; gPlView = PlView::Plane; }
      else if (lower == 'l') { gPlView = PlView::PlaneList; }
      else if (lower == 'u' && plOverhead() >= 0) { plSelectPlane(plOverhead()); gPlPlaneBack = PlView::Sky; gPlView = PlView::Plane; }
      else if (key == '-' || key == '_' || key == '=' || key == '+') {
        const bool wider = key == '=' || key == '+';
        if ((wider && gPlRange + 1 < planes::kRanges) || (!wider && gPlRange > 0)) {
          gPlRange = uint8_t(gPlRange + (wider ? 1 : -1));
          gPlNearDueMs = millis();
          if (gPlJob == PlJob::Near) camera_net::cancel();
        }
        char range[8];
        planes::rangeLabel(range, sizeof(range), gPlRange, plUnits());
        plSay(String("Ring: ") + range);
      } else if (lower == 's' && gPlSkyOk && gPlSel < gPlSky->count) plSaveSighting(gPlSky->planes[gPlSel]);
      else if (lower == 'r') { gPlNearDueMs = millis(); plSay("Looking again..."); }
      break;
    case PlView::PlaneList:
      if (up || down) plSelectPlane(gPlSel + (up ? -1 : 1));
      else if ((left || right) && gPlSkyOk && gPlSky->count)  // A screenful at a time.
        plSelectPlane(max<int>(0, min<int>(gPlSky->count - 1, gPlSel + (left ? -kPlRows : kPlRows))));
      else if (enter && gPlSkyOk && gPlSky->count) { plSelectPlane(gPlSel); gPlPlaneBack = PlView::PlaneList; gPlView = PlView::Plane; }
      else if (lower == 's' && gPlSkyOk && gPlSel < gPlSky->count) plSaveSighting(gPlSky->planes[gPlSel]);
      break;
    case PlView::Plane:
      if (left || right) plSelectPlane(gPlSel + (left ? -1 : 1));
      else if (lower == 's') { if (const planes::Plane* p = plSelected()) plSaveSighting(*p); }
      break;
    case PlView::Passes:
      if (gPlSpaceOk && gPlSpace->count && (up || down)) gPlPassSel = uint8_t((gPlPassSel + (up ? gPlSpace->count - 1 : 1)) % gPlSpace->count);
      else if (left || right) plFlip(left);
      else if (enter && gPlSpaceOk && gPlSpace->count) { gPlPassFromReminder = false; gPlView = PlView::Pass; }
      else if (lower == 'r' && gPlSpaceOk && gPlPassSel < gPlSpace->count) plToggleReminder(gPlSpace->passes[gPlPassSel]);
      break;
    case PlView::Pass:
      if ((left || right) && !gPlPassFromReminder && gPlSpaceOk && gPlSpace->count)
        gPlPassSel = uint8_t((gPlPassSel + (left ? gPlSpace->count - 1 : 1)) % gPlSpace->count);
      else if (lower == 'r') { if (const planes::Pass* p = plPassShown()) plToggleReminder(*p); }
      break;
    case PlView::Now:
      if (left || right) plFlip(left);
      else if (lower == 'r') { gPlIssDueMs = millis(); plSay("Asking again..."); }
      break;
    case PlView::Spotted:
      if (left || right) plFlip(left);
      else if (enter && gPlRecentCount) { gPlRecentTop = 0; gPlView = PlView::Sightings; }
      break;
    case PlView::Sightings:
      if (up && gPlRecentTop) --gPlRecentTop;
      else if (down && gPlRecentTop + kPlRows < gPlRecentCount) ++gPlRecentTop;
      break;
    case PlView::Drones: {
      uint8_t order[drone_id::Table::kMax];
      const uint8_t n = plDroneOrder(order);
      if (left || right) plFlip(left);
      else if (!plDroneListening()) { if (enter) plDroneAsk(); }
      else if ((up || down) && n) gPlDroneSel = uint8_t((gPlDroneSel + (up ? n - 1 : 1)) % n);
      else if (enter && n && gPlDroneSel < n) { memcpy(gPlDroneShown, gPlDrones->heard[order[gPlDroneSel]].d.mac, 6); gPlView = PlView::Drone; }
      else if (lower == 's' && n && gPlDroneSel < n) plSaveDrone(gPlDrones->heard[order[gPlDroneSel]]);
      break;
    }
    case PlView::Drone: {
      uint8_t order[drone_id::Table::kMax];
      const uint8_t n = plDroneOrder(order);
      if ((left || right) && n) {
        int at = 0;
        for (uint8_t i = 0; i < n; ++i) if (!memcmp(gPlDrones->heard[order[i]].d.mac, gPlDroneShown, 6)) at = i;
        at = (at + (left ? n - 1 : 1)) % n;
        gPlDroneSel = uint8_t(at);
        memcpy(gPlDroneShown, gPlDrones->heard[order[at]].d.mac, 6);
      } else if (lower == 's') {
        if (const drone_id::Heard* h = plDroneByMac(gPlDroneShown)) plSaveDrone(*h);
      }
      break;
    }
    case PlView::GoOut:
      if (enter) {
        // The listed pass when it's there, for its path; else the reminder's own.
        gPlPassFromReminder = true;
        for (uint8_t i = 0; gPlSpaceOk && i < gPlSpace->count; ++i)
          if (gPlSpace->passes[i].start == gPlGoPass.start) { gPlPassSel = i; gPlPassFromReminder = false; }
        gPlView = PlView::Pass;
      }
      break;
  }
  plInvalidate();
  return true;
}

void renderPlanesUi() {
  switch (gPlView) {
    case PlView::Sky: plDrawSky(); break;
    case PlView::Plane: plDrawPlane(); break;
    case PlView::PlaneList: plDrawPlaneList(); break;
    case PlView::Passes: plDrawPasses(); break;
    case PlView::Pass: plDrawPass(); break;
    case PlView::Now: plDrawNow(); break;
    case PlView::Spotted: plDrawSpotted(); break;
    case PlView::Sightings: plDrawSightings(); break;
    case PlView::GoOut: plDrawGoOut(); break;
    case PlView::Drones: plDrawDrones(); break;
    case PlView::Drone: plDrawDrone(); break;
  }
  if (plNoticeShowing()) drawBigNotice(gPlNotice, gPlNoticeInk);
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
