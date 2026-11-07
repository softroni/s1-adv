// Planes: what's flying over the Cardputer and when the space station passes, from the bridge
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
namespace {
enum class PlView : uint8_t { Sky, Plane, Passes, Pass, Now, Spotted, Sightings, LookUp, GoOut, Drones, Drone, DroneAlert };
enum class PlJob : uint8_t { None, Ticket, Near, Iss, Report };

constexpr uint8_t kGpsForPlanes = 16;            // Beside 081's holders: Lab 1, once 2, Pulse 4, Mesh 8.
constexpr uint32_t kPlNearEveryMs = 10000;       // Planes move a mile in about ten seconds.
constexpr uint32_t kPlIssEveryMs = 10UL * 60UL * 1000UL, kPlNowEveryMs = 20000;
constexpr uint32_t kPlRetryMs = 15000;
constexpr uint32_t kPlLookUpShowMs = 20000, kPlLookUpGapMs = 45000;
constexpr uint32_t kPlNoticeMs = 3000;
constexpr size_t kPlNearMax = 9000, kPlIssMax = 4096;
constexpr const char* kPlDir = "/planes";
constexpr const char* kPlSpottedPath = "/planes/spotted.txt";
constexpr const char* kPlPrefsNs = "planes";
constexpr uint8_t kPlRecent = 40;                // Sightings listed, newest first.
constexpr uint8_t kPlAlertMemory = 16, kPlSavedMemory = 24;
constexpr int16_t kPlMapX = 56, kPlMapY = 75, kPlMapR = 46;
constexpr int16_t kPlListX = 109, kPlRowH = 24, kPlRows = 4, kPlFootY = 126;

uint16_t plAccent() { return rgb565_local(110, 200, 255); }
uint16_t plMuted() { return rgb565_local(146, 146, 170); }
uint16_t plDim() { return rgb565_local(60, 70, 100); }
uint16_t plFaint() { return rgb565_local(36, 36, 85); }
uint16_t plAmber() { return rgb565_local(255, 196, 64); }
uint16_t plGreen() { return rgb565_local(88, 240, 141); }
uint16_t plGold() { return rgb565_local(255, 220, 100); }
uint16_t plPink() { return rgb565_local(255, 110, 200); }

PlView gPlView = PlView::Sky, gPlBack = PlView::Sky;
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
char gPlLookHex[7] = "";
planes::Plane gPlLook;
uint32_t gPlLookUntilMs = 0, gPlLastLookMs = 0;
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
struct PlChirp { uint32_t nextMs = 0; uint8_t left = 0, step = 0; } gPlChirp;
// Drones: listening takes Wi-Fi off its network, which planesHoldsWifi() keeps the reconnects away
// from, and gives it back when the page is left. Enter starts it, so flipping past never drops Wi-Fi.
enum class PlDroneState : uint8_t { Off, Waiting, Listening };
PlDroneState gPlDroneState = PlDroneState::Off;
bool gPlDroneHold = false, gPlDroneWifiWasOff = false;
drone_id::Table* gPlDrones = nullptr;       // On the heap while listening.
drone_listen::Frame* gPlDroneFrame = nullptr;
uint8_t gPlDroneSel = 0;
uint8_t gPlDroneShown[6] = {}, gPlDroneAlertMac[6] = {};  // By address: the table reorders.
uint32_t gPlDroneSince = 0, gPlDroneLastAlertMs = 0, gPlDroneAlertUntil = 0;
PlView gPlDroneBack = PlView::Drones;
constexpr uint32_t kPlDroneAlertMs = 20000, kPlDroneAlertGapMs = 20000;

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
const char* plPlaceWords(const char* place) {
  return !place || !*place || !strcmp(place, "Here") ? "you" : place;
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

bool plWantsSky() { return gPlView == PlView::Sky || gPlView == PlView::Plane || gPlView == PlView::LookUp; }
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
  if (word == "not_found" || e == camera_net::Error::NotFound) return "The bridge needs an update for Planes";
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
void plChirp(uint8_t notes) { gPlChirp.left = notes; gPlChirp.step = 0; gPlChirp.nextMs = millis(); }
void plWakeScreen() {
  if (!gScreensaverActive && (gScreensaverDimmed || gScreenOffActive)) {
    gScreensaverDimmed = gScreenOffActive = false;
    applyDisplayBrightness();
  }
  markUserActivity();
}
// After a fresh list: a plane high overhead, not shown before, brings up "Look up!".
void plCheckLookUp() {
  if (gPlView != PlView::Sky && gPlView != PlView::Plane) return;
  if (gPlLastLookMs && millis() - gPlLastLookMs < kPlLookUpGapMs) return;
  for (uint8_t i = 0; i < gPlSky->count; ++i) {
    const planes::Plane& p = gPlSky->planes[i];
    if (!planes::wantsLookUp(p) || plAlreadyAlerted(p.hex)) continue;
    strcpy(gPlAlerted[gPlAlertedNext], p.hex);
    gPlAlertedNext = uint8_t((gPlAlertedNext + 1) % kPlAlertMemory);
    gPlLook = p;
    strcpy(gPlLookHex, p.hex);
    gPlBack = gPlView;
    gPlView = PlView::LookUp;
    gPlLookUntilMs = millis() + kPlLookUpShowMs;
    gPlLastLookMs = millis();
    plWakeScreen();
    plChirp(3);
    appendRuntimeLog("PLANES", String("look up ") + p.hex);
    return;
  }
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
  for (uint8_t i = 0; i < gPlSky->count; ++i)
    if (!strcmp(gPlSky->planes[i].hex, gPlLookHex)) gPlLook = gPlSky->planes[i];
  plCheckLookUp();
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
          gPlNearDueMs = now + kPlNearEveryMs;
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
          gPlIssDueMs = now + (gPlView == PlView::Now ? kPlNowEveryMs : kPlIssEveryMs);
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

void plServiceChirp() {
  if (!gPlChirp.left || int32_t(millis() - gPlChirp.nextMs) < 0) return;
  static const uint16_t kNotes[] = {1319, 1568, 2093};  // E, G, high C: a rising "look!"
  focusTone(kNotes[gPlChirp.step % 3], 110);
  ++gPlChirp.step;
  --gPlChirp.left;
  gPlChirp.nextMs = millis() + (gPlChirp.step % 3 == 0 ? 700 : 150);
}

// Called from the loop on every screen: the reminder rings the way the alarm does, opening Planes.
void servicePlanesReminder() {
  plServiceChirp();
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
    if (gPlView != PlView::GoOut && gPlView != PlView::LookUp) gPlBack = gPlView;
    gPlView = PlView::GoOut;
  } else {
    gPlOpenOn = PlView::GoOut;
    setUiMode(UiMode::Planes);
  }
  plChirp(9);
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
  plChirp(2);
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
    case PlView::Drones: case PlView::Drone: case PlView::DroneAlert: return 4;
    default: return 0;
  }
}
// The keys along the bottom, and on the four pages, which one this is.
void plFooter(const char* keys, bool dots) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(4, kPlFootY);
  d.print(fitCurrentFontToWidth(keys, dots ? 206 : 232));
  if (!dots) return;
  for (uint8_t i = 0; i < 5; ++i) {
    const int16_t x = 213 + i * 6, y = kPlFootY + 3;
    if (i == plPage()) d.fillCircle(x, y, 2, plAccent());
    else d.drawCircle(x, y, 2, plDim());
  }
}
void plHeader(const String& title, uint16_t ink, const String& hint, const String& hint2 = "") {
  char clock[12];
  plClockNow(clock, sizeof(clock));
  drawBigHeader(title, ink, clock, hint, hint2);
}
// A round map: north up, a ring at its edge and one at half, and you in the middle.
void plDrawRound(bool sky) {
  auto& d = gCanvas;
  d.drawCircle(kPlMapX, kPlMapY, kPlMapR, plDim());
  d.drawCircle(kPlMapX, kPlMapY, kPlMapR / 2, plFaint());
  d.drawFastHLine(kPlMapX - kPlMapR + 1, kPlMapY, kPlMapR * 2 - 1, plFaint());
  d.drawFastVLine(kPlMapX, kPlMapY - kPlMapR + 1, kPlMapR * 2 - 1, plFaint());
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(kPlMapX - 2, kPlMapY - kPlMapR + 3); d.print("N");
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
void plDrawPlaneDots() {
  auto& d = gCanvas;
  const uint32_t range = planes::rangeMeters(gPlRange, plUnits());
  d.setFont(&fonts::Font0);
  // Back to front, so the nearest and the chosen are drawn on top.
  for (int i = gPlSky->count - 1; i >= 0; --i) {
    const planes::Plane& p = gPlSky->planes[i];
    int dx, dy;
    planes::mapPoint(p.distM, p.bearing, range, kPlMapR - 5, dx, dy);
    const int16_t x = kPlMapX + dx, y = kPlMapY + dy;
    if (i >= 9) { d.fillCircle(x, y, 1, plHeightInk(p)); continue; }
    const char n[2] = {char('1' + i), 0};
    if (i == gPlSel) {
      d.fillRoundRect(x - 4, y - 5, 9, 11, 2, TFT_WHITE);
      d.setTextColor(TFT_BLACK);
    } else {
      d.fillRoundRect(x - 4, y - 5, 9, 11, 2, TFT_BLACK);
      d.setTextColor(plHeightInk(p));
    }
    d.setCursor(x - 2, y - 3);
    d.print(n);
  }
}
void plPlaneLine(char* out, size_t room, const planes::Plane& p) {
  char dist[12], alt[12];
  planes::distanceShort(dist, sizeof(dist), p.distM, plUnits());
  planes::altitudeShort(alt, sizeof(alt), p.altFt, plUnits());
  snprintf(out, room, "%s %s %s", planes::compass(p.bearing), dist, alt);
}
void plDrawSky() {
  auto& d = gCanvas;
  String hint;
  if (gPlSkyOk) hint = (gPlSky->count ? String(gPlSky->count) : String("none")) + " near " + plPlaceWords(gPlSky->place);
  else hint = plUsingGps() ? "GPS" : gDeviceSettings.weatherPlace;
  plHeader("Planes", plAccent(), hint);
  plDrawRound(false);
  if (gPlSkyOk) plDrawPlaneDots();
  char range[8];
  planes::rangeLabel(range, sizeof(range), gPlRange, plUnits());
  d.setFont(&fonts::Font0);
  d.setTextColor(plDim());
  d.setCursor(kPlMapX + kPlMapR - 22, kPlMapY + kPlMapR - 6);
  d.print(range);
  const int16_t listW = 238 - kPlListX;
  if (!gPlSkyOk || !gPlSky->count) {
    d.setFont(&fonts::Font2);
    d.setTextColor(gPlProblem.length() ? plAmber() : plMuted());
    String text;
    if (gPlProblem.length()) text = gPlProblem;
    else if (!wifiLocalAddress()) text = "Planes needs Wi-Fi";
    else if (!gPlSkyOk) text = "Looking for planes...";
    else text = String("No planes within ") + range + " right now. = makes the ring wider.";
    plWrap(text, kPlListX + 4, 30, listW - 6, 17, 5);
  } else {
    const int first = max<int>(0, min<int>(gPlSel - 1, gPlSky->count - kPlRows));
    for (int row = 0; row < kPlRows && first + row < gPlSky->count; ++row) {
      const int i = first + row;
      const planes::Plane& p = gPlSky->planes[i];
      const int16_t y = 25 + row * kPlRowH;
      const bool selected = i == gPlSel;
      if (selected) d.fillRoundRect(kPlListX, y, listW + 1, kPlRowH - 1, 4, plAccent());
      d.setFont(&fonts::Font0);
      d.setTextColor(selected ? TFT_BLACK : plHeightInk(p));
      d.setCursor(kPlListX + 3, y + 4);
      d.print(i + 1);
      d.setFont(&fonts::Font2);
      d.setTextColor(selected ? TFT_BLACK : TFT_WHITE);
      d.setCursor(kPlListX + 15, y);
      d.print(fitCurrentFontToWidth(p.name, listW - 17));
      char line[32];
      plPlaneLine(line, sizeof(line), p);
      d.setFont(&fonts::Font0);
      d.setTextColor(selected ? TFT_BLACK : plMuted());
      d.setCursor(kPlListX + 15, y + 15);
      d.print(fitCurrentFontToWidth(line, listW - 17));
    }
  }
  plFooter(";. pick Enter look -= ring ,/ more", true);
}

const planes::Plane* plSelected() {
  if (gPlSkyOk && gPlSel < gPlSky->count && !strcmp(gPlSky->planes[gPlSel].hex, gPlSelHex)) return &gPlSky->planes[gPlSel];
  return gPlShown.hex[0] ? &gPlShown : nullptr;
}
void plDrawPlane() {
  auto& d = gCanvas;
  const planes::Plane* sel = plSelected();
  if (!sel) { gPlView = PlView::Sky; plDrawSky(); return; }
  const planes::Plane p = *sel;
  const char* kind = planes::isDrone(p) ? "Drone" : planes::isHelicopter(p) ? "Helicopter" : "";
  plHeader(p.name, plAccent(), gPlSelGone ? "out of range now" : kind);
  d.setFont(&fonts::Font2);
  d.setTextColor(p.type[0] ? TFT_WHITE : plMuted());
  d.setCursor(8, 26);
  d.print(fitCurrentFontToWidth(p.type[0] ? p.type : "Type not known", 224));
  if (p.from[0] || p.to[0]) {
    const String from = p.from[0] ? p.from : "?", to = p.to[0] ? p.to : "?";
    d.setTextColor(plAccent());
    d.setCursor(8, 44);
    d.print(from);
    const int16_t fromEnd = 8 + d.textWidth(from) + 6, toX = 232 - d.textWidth(to);
    d.setCursor(toX, 44);
    d.print(to);
    const int16_t lineY = 51, arrowX = toX - 6;
    if (arrowX - fromEnd > 12) {
      d.drawFastHLine(fromEnd, lineY, arrowX - fromEnd, plDim());
      d.fillTriangle(arrowX, lineY, arrowX - 6, lineY - 4, arrowX - 6, lineY + 4, plAccent());
      d.fillTriangle((fromEnd + arrowX) / 2 + 3, lineY, (fromEnd + arrowX) / 2 - 3, lineY - 3, (fromEnd + arrowX) / 2 - 3, lineY + 3, plDim());
    }
    d.setFont(&fonts::Font0);
    d.setTextColor(plMuted());
    d.setCursor(8, 61);
    d.print(fitCurrentFontToWidth(p.fromCity, 110));
    const String toCity = fitCurrentFontToWidth(p.toCity, 110);
    d.setCursor(232 - d.textWidth(toCity), 61);
    d.print(toCity);
  } else {
    d.setFont(&fonts::Font0);
    d.setTextColor(plMuted());
    d.setCursor(8, 48);
    d.print("Where it's going isn't known");
  }
  d.setFont(&fonts::Font0);
  d.setTextColor(plAmber());
  d.setCursor(8, 78);
  d.print("LOOK");
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(TFT_WHITE);
  d.setCursor(40, 72);
  d.print(planes::compassWord(p.bearing));
  char height[40];
  planes::heightWords(height, sizeof(height), p.elev);
  d.setFont(&fonts::Font2);
  d.setTextColor(plAmber());
  d.setCursor(40, 88);
  d.print(height);
  char alt[16], dist[16], speed[16], closest[24], line[64];
  planes::altitudeText(alt, sizeof(alt), p.altFt, plUnits());
  units::distanceText(dist, sizeof(dist), int32_t(p.distM), plUnits());
  planes::speedText(speed, sizeof(speed), p.speedKt, plUnits());
  planes::closestText(closest, sizeof(closest), p.closestS);
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  snprintf(line, sizeof(line), "%s %s, %s away", alt, planes::climbWord(p.climbFpm, p.altFt), dist);
  d.setCursor(8, 106);
  d.print(fitCurrentFontToWidth(line, 228));
  if (p.track >= 0) snprintf(line, sizeof(line), "%s heading %s, %s", speed, planes::compass(p.track), closest);
  else snprintf(line, sizeof(line), "%s, %s", speed, closest);
  d.setCursor(8, 115);
  d.print(fitCurrentFontToWidth(line, 228));
  plFooter(",/ other planes  S I saw it!  Esc", false);
}

void plDrawLookUp() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  const planes::Plane& p = gPlLook;
  const bool flash = (millis() / 400) % 2 == 0;
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(flash ? plAmber() : TFT_WHITE);
  plCentered("LOOK UP!", 6);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(TFT_WHITE);
  plCentered(planes::isDrone(p) ? String("A drone, ") + p.name : String(p.name), 34);
  d.setFont(&fonts::Font2);
  d.setTextColor(plAccent());
  char line[72], alt[16];
  if (p.track >= 0) snprintf(line, sizeof(line), "coming from the %s", planes::compassWord(p.track + 180));
  else snprintf(line, sizeof(line), "high up to the %s", planes::compassWord(p.bearing));
  plCentered(line, 56);
  planes::altitudeText(alt, sizeof(alt), p.altFt, plUnits());
  d.setTextColor(TFT_WHITE);
  plCentered(String(alt) + " up", 73);
  line[0] = 0;
  if (p.climbFpm <= -300 && p.to[0]) snprintf(line, sizeof(line), "coming down to land at %s", p.to);
  else if (p.climbFpm >= 300 && p.from[0]) snprintf(line, sizeof(line), "climbing out of %s", p.from);
  else if (p.to[0]) snprintf(line, sizeof(line), "on its way to %s", p.toCity[0] ? p.toCity : p.to);
  if (line[0]) plCentered(line, 90);
  if (p.type[0]) {
    d.setTextColor(plMuted());
    plCentered(p.type, line[0] ? 107 : 90);
  }
  plFooter("Enter more   S I saw it!   Esc", false);
}

// Four stars, lit for the pass's brightness.
void plDrawStars(int16_t x, int16_t y, uint8_t lit, bool onBar) {
  auto& d = gCanvas;
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t cx = x + i * 9 + 3, cy = y + 7;
    const uint16_t ink = i < lit ? (onBar ? TFT_BLACK : plGold()) : (onBar ? rgb565_local(40, 90, 120) : plFaint());
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
String plSpaceHint() {
  if (gPlSpaceOk) return String("near ") + plPlaceWords(gPlSpace->place);
  return plUsingGps() ? "GPS" : gDeviceSettings.weatherPlace;
}
bool plSpaceWaiting(int16_t y) {
  if (gPlSpaceOk) return false;
  auto& d = gCanvas;
  d.setFont(&fonts::Font2);
  d.setTextColor(gPlSpaceProblem.length() ? plAmber() : plMuted());
  plWrap(gPlSpaceProblem.length() ? gPlSpaceProblem : (wifiLocalAddress() ? String("Asking where the station is...") : String("Planes needs Wi-Fi")),
         8, y, 224, 17, 5);
  return true;
}
void plDrawPasses() {
  auto& d = gCanvas;
  plHeader("Space station", plGold(), plSpaceHint());
  if (!plSpaceWaiting(30)) {
    const uint32_t now = plNowUnix();
    if (!gPlSpace->count) {
      d.setFont(&fonts::Font2);
      d.setTextColor(TFT_WHITE);
      plWrap("None you can see in the next 10 days.", 8, 30, 224, 17, 2);
      d.setTextColor(plMuted());
      plWrap("It shows near dusk and dawn, when the sky is dark but the station is still in sunlight.", 8, 66, 224, 17, 3);
    } else {
      d.setFont(&fonts::Font2);
      d.setTextColor(plMuted());
      d.setCursor(8, 26);
      d.print("Next times you can see it:");
      const int first = max<int>(0, min<int>(gPlPassSel - 1, gPlSpace->count - 4));
      for (int row = 0; row < 4 && first + row < gPlSpace->count; ++row) {
        const int i = first + row;
        const planes::Pass& p = gPlSpace->passes[i];
        const int16_t y = 44 + row * 18;
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
        d.setCursor(8, y);
        d.print(fitCurrentFontToWidth(now && now >= p.start && now < p.end ? String("Up now!") : String(day), 72));
        d.setCursor(84, y);
        d.print(at);
        d.setCursor(150, y);
        d.print(len);
        if (plReminded(p)) plDrawBell(190, y + 2, selected ? TFT_BLACK : plGreen());
        plDrawStars(201, y, planes::stars(p.magTenths), selected);
      }
      d.setFont(&fonts::Font0);
      d.setTextColor(plMuted());
      d.setCursor(8, 117);
      d.print("More stars: brighter and higher");
    }
  }
  plFooter(gPlSpaceOk && gPlSpace->count ? "Enter path  R remind me  ,/ more" : ",/ more", true);
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
  plHeader(day, plGold(), until);
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
    }
    px = x; py = y;
  }
  const auto mark = [&](int azDeg, int elDeg, char label) {
    int dx, dy;
    planes::skyPoint(azDeg, elDeg, kPlMapR, dx, dy);
    const int16_t x = kPlMapX + dx, y = kPlMapY + dy;
    d.fillCircle(x, y, 5, TFT_WHITE);
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_BLACK);
    d.setCursor(x - 2, y - 3);
    d.print(label);
  };
  mark(az[0], el[0], '1');
  mark(p.peakAz, p.peakEl, '2');
  mark(az[n - 1], el[n - 1], '3');
  const int16_t x = 114;
  char at[16], words[40], height[24];
  const auto entry = [&](int16_t y, char label, uint32_t when, const char* text) {
    plTimeOf(at, sizeof(at), when);
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(x, y);
    d.print(String(label) + "  " + at);
    d.setFont(&fonts::Font0);
    d.setTextColor(plMuted());
    d.setCursor(x + 15, y + 16);
    d.print(fitCurrentFontToWidth(text, 238 - x - 15));
  };
  snprintf(words, sizeof(words), "appears %s", planes::compassWord(p.startAz));
  entry(26, '1', p.start, words);
  planes::heightShort(height, sizeof(height), p.peakEl);
  snprintf(words, sizeof(words), "%s %s", height, planes::compass(p.peakAz));
  entry(54, '2', p.peak, words);
  snprintf(words, sizeof(words), "%s %s", p.shadow ? "vanishes" : "sets", planes::compass(p.endAz));
  entry(82, '3', p.end, words);
  d.setFont(&fonts::Font0);
  if (plReminded(p)) {
    plTimeOf(at, sizeof(at), gPlRemindAt);
    d.setTextColor(plGreen());
    d.setCursor(x, 114);
    d.print(String("rings at ") + at);
  } else if (p.points) {
    plDrawStars(x, 109, planes::stars(p.magTenths), false);
  }
  plFooter(gPlPassFromReminder ? "R remind me  Esc" : "R remind me  ,/ other nights  Esc", false);
}

void plDrawNow() {
  auto& d = gCanvas;
  plHeader("Space station", plGold(), "right now");
  if (!plSpaceWaiting(30)) {
    const planes::Station& s = gPlSpace->now;
    d.setFont(&fonts::Font0);
    d.setTextColor(plMuted());
    d.setCursor(8, 28);
    d.print("Over");
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(TFT_WHITE);
    d.setCursor(8, 38);
    d.print(fitCurrentFontToWidth(s.known && s.over[0] ? String(s.over) : String("the Earth"), 224));
    const bool us = plUnits() == units::System::US;
    char number[16], line[48];
    d.setFont(&fonts::Font2);
    d.setTextColor(plGold());
    planes::grouped(number, sizeof(number), us ? units::rounded(s.altKm * 0.621371) : s.altKm);
    snprintf(line, sizeof(line), "%s %s up", number, us ? "miles" : "km");
    d.setCursor(8, 60);
    d.print(line);
    planes::grouped(number, sizeof(number), us ? units::rounded(s.speedKmh * 0.621371 / 100.0) * 100 : units::rounded(s.speedKmh / 100.0) * 100);
    snprintf(line, sizeof(line), "%s %s", number, us ? "mph" : "km/h");
    d.setCursor(8, 76);
    d.print(line);
    d.setTextColor(TFT_WHITE);
    snprintf(line, sizeof(line), "Around the whole Earth in %lu min", static_cast<unsigned long>(planes::orbitMinutes(s.altKm)));
    d.setCursor(8, 92);
    d.print(fitCurrentFontToWidth(line, 228));
    d.setFont(&fonts::Font0);
    const int next = planes::nextPass(*gPlSpace, plNowUnix());
    if (next >= 0) {
      char day[16], at[16];
      plDayOf(day, sizeof(day), gPlSpace->passes[next].start);
      plTimeOf(at, sizeof(at), gPlSpace->passes[next].start);
      day[0] = char(tolower(static_cast<unsigned char>(day[0])));
      d.setTextColor(plAmber());
      d.setCursor(8, 108);
      d.print(String("Next over us: ") + day + ", " + at);
    }
    if (s.crew > 0) {
      d.setTextColor(plMuted());
      d.setCursor(8, 117);
      d.print(String(s.crew) + (s.crew == 1 ? " person is" : " people are") + " on board");
    }
  }
  plFooter("R refresh  ,/ more", true);
}

void plDrawSpotted() {
  auto& d = gCanvas;
  plReadSpotted();
  plHeader("Spotted", plGreen(), gPlStats && gPlStats->total ? "your collection" : "");
  d.setFont(&fonts::Font2);
  if (!gSdReady) {
    d.setTextColor(plAmber());
    plWrap("Spotted keeps the planes you saw on the SD card. Put one in to start.", 8, 30, 224, 17, 4);
  } else if (!gPlStats) {
    d.setTextColor(plAmber());
    plWrap("Not enough memory to read the collection", 8, 30, 224, 17, 3);
  } else if (!gPlStats->total) {
    d.setTextColor(TFT_WHITE);
    plWrap("Nothing spotted yet.", 8, 30, 224, 17, 1);
    d.setTextColor(plMuted());
    plWrap("When you see a plane in the sky, open it and press S. It joins your collection.", 8, 52, 224, 17, 4);
  } else {
    const planes::Stats& s = *gPlStats;
    char line[64];
    snprintf(line, sizeof(line), "%lu planes  %u airlines  %u types", static_cast<unsigned long>(s.total), unsigned(s.airlines.count),
             unsigned(s.types.count));
    d.setTextColor(TFT_WHITE);
    d.setCursor(8, 26);
    d.print(fitCurrentFontToWidth(line, 228));
    const auto row = [&](int16_t y, const char* label, uint16_t labelInk, const String& what, const String& right) {
      d.setFont(&fonts::Font0);
      d.setTextColor(labelInk);
      d.setCursor(8, y + 4);
      d.print(label);
      d.setFont(&fonts::Font2);
      const int16_t rightW = right.length() ? d.textWidth(right) + 6 : 0;
      d.setTextColor(TFT_WHITE);
      d.setCursor(64, y);
      d.print(fitCurrentFontToWidth(what, 232 - 64 - rightW));
      if (right.length()) {
        d.setTextColor(plMuted());
        d.setCursor(232 - d.textWidth(right), y);
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
      row(46, today ? "NEW TODAY" : "NEWEST", today ? plAmber() : plMuted(), s.types.names[newest], today ? String() : String(day));
    }
    char alt[16];
    planes::altitudeShort(alt, sizeof(alt), s.highest.altFt, plUnits());
    row(64, "HIGHEST", plMuted(), s.highest.type[0] ? s.highest.type : s.highest.name, alt);
    planes::altitudeShort(alt, sizeof(alt), s.lowest.altFt, plUnits());
    row(82, "LOWEST", plMuted(), s.lowest.type[0] ? s.lowest.type : s.lowest.name, alt);
    const int top = s.airlines.top();
    if (top >= 0) row(100, "MOST", plMuted(), s.airlines.names[top], String("x") + s.airlines.counts[top]);
  }
  plFooter(gPlStats && gPlStats->total ? "Enter every sighting  ,/ more" : ",/ more", true);
}
void plDrawSightings() {
  auto& d = gCanvas;
  plReadSpotted();
  plHeader("Spotted", plGreen(), gPlStats ? String(gPlStats->total) + " seen" : "");
  if (!gPlRecent || !gPlRecentCount) { gPlView = PlView::Spotted; return; }
  gPlRecentTop = uint8_t(min<int>(gPlRecentTop, max<int>(0, gPlRecentCount - kPlRows)));
  for (int row = 0; row < kPlRows && gPlRecentTop + row < gPlRecentCount; ++row) {
    const planes::Sighting& s = gPlRecent[gPlRecentTop + row];
    const int16_t y = 25 + row * kPlRowH;
    char day[16], at[16];
    plDayOf(day, sizeof(day), s.at);
    plTimeOf(at, sizeof(at), s.at);
    const String when = String(day) + " " + at;
    d.setFont(&fonts::Font0);
    d.setTextColor(plMuted());
    const int16_t whenX = 232 - d.textWidth(when);
    d.setCursor(whenX, y + 4);
    d.print(when);
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
    d.setCursor(8, y);
    d.print(fitCurrentFontToWidth(s.name, whenX - 14));
    d.setFont(&fonts::Font0);
    d.setTextColor(plAccent());
    d.setCursor(8, y + 15);
    d.print(fitCurrentFontToWidth(s.type[0] ? s.type : "type not known", 224));
  }
  plFooter(gPlRecentCount >= kPlRecent ? ";. scroll  the newest 40  Esc" : ";. scroll  Esc back", false);
}

void plDrawGoOut() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  const planes::Pass& p = gPlGoPass;
  const uint32_t now = plNowUnix();
  const bool flash = (millis() / 500) % 2 == 0;
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(flash ? plGold() : TFT_WHITE);
  plCentered("GO OUTSIDE NOW!", 8);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  char line[64];
  if (now && now < p.start) {
    const uint32_t min = (p.start - now + 59) / 60;
    snprintf(line, sizeof(line), "The space station rises in the %s", planes::compassWord(p.startAz));
    plCentered(line, 40);
    snprintf(line, sizeof(line), "in %lu min", static_cast<unsigned long>(min));
    d.setTextColor(plGold());
    plCentered(line, 57);
  } else if (now < p.end) {
    snprintf(line, sizeof(line), "It's up now, look %s!", planes::compassWord(now < p.peak ? p.startAz : p.endAz));
    plCentered(line, 48);
  } else {
    plCentered("It has gone by. Next time!", 48);
  }
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  plCentered("Look for a bright dot that moves fast", 80);
  plCentered("and doesn't blink. Planes blink!", 90);
  if (gPlSpaceOk && gPlSpace->now.crew > 0) {
    d.setTextColor(plAccent());
    plCentered(String(gPlSpace->now.crew) + " people are on board", 106);
  }
  plFooter("Enter path   Esc", false);
}

// --- Drones --------------------------------------------------------------------------------
bool plDroneView() { return gPlView == PlView::Drones || gPlView == PlView::Drone || gPlView == PlView::DroneAlert; }
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
  size_t k = 0;
  while (compact[k] && ((compact[k] >= '0' && compact[k] <= '9') || compact[k] == '.')) ++k;
  snprintf(out, room, "%.*s %s", int(k), compact, compact + k);
}
void plDroneHeight(char* out, size_t room, int32_t meters, bool compact) {
  const bool us = plUnits() == units::System::US;
  snprintf(out, room, compact ? "%ld%s" : "%ld %s", static_cast<long>(us ? units::feet(meters) : meters), us ? "ft" : "m");
}
void plDroneAlert(const drone_id::Heard& h) {
  if (gPlView != PlView::Drones && gPlView != PlView::Drone) return;
  if (gPlDroneLastAlertMs && millis() - gPlDroneLastAlertMs < kPlDroneAlertGapMs) return;
  gPlDroneLastAlertMs = millis();
  memcpy(gPlDroneAlertMac, h.d.mac, 6);
  gPlDroneBack = gPlView;
  gPlView = PlView::DroneAlert;
  gPlDroneAlertUntil = millis() + kPlDroneAlertMs;
  plWakeScreen();
  plChirp(3);
}
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
    if (!h.alerted) { h.alerted = true; plDroneAlert(h); }
  }
  gPlDrones->forget(now);
  if (gPlView == PlView::DroneAlert && int32_t(now - gPlDroneAlertUntil) >= 0) { gPlView = gPlDroneBack; plInvalidate(); }
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
  String hint = plDroneListening() ? String("listening ch ") + drone_listen::channel()
                : gPlDroneState == PlDroneState::Waiting ? String("starting...") : String("not listening");
  plHeader("Drones", plPink(), hint);
  plDrawRound(false);
  const int16_t listW = 238 - kPlListX;
  if (!plDroneListening()) {
    d.setFont(&fonts::Font2);
    d.setTextColor(plMuted());
    plWrap(gPlDroneState == PlDroneState::Waiting ? String("Waiting for the network to finish...")
                                                   : String("Drones send their ID over Wi-Fi. Enter listens for them, and the internet pauses while it does."),
           kPlListX + 4, 28, listW - 6, 16, 6);
    plFooter("Enter listen  ,/ more", true);
    return;
  }
  // A sweep goes round while it listens.
  const double a = (millis() % 3000) / 3000.0 * 2 * 3.14159265358979;
  d.drawLine(kPlMapX, kPlMapY, kPlMapX + int(lround(sin(a) * (kPlMapR - 2))), kPlMapY - int(lround(cos(a) * (kPlMapR - 2))), rgb565_local(110, 40, 90));
  uint8_t order[drone_id::Table::kMax];
  const uint8_t n = plDroneOrder(order);
  if (gPlDroneSel >= n) gPlDroneSel = n ? n - 1 : 0;
  // The ring: 300 m, 1 km or 3 km, whichever holds them all.
  int32_t ring = 300;
  for (uint8_t i = 0; i < n; ++i) {
    int32_t dist; int bearing, elev;
    if (plDroneFrom(gPlDrones->heard[order[i]].d, dist, bearing, elev)) while (ring < 3000 && dist > ring * 9 / 10) ring = ring == 300 ? 1000 : 3000;
  }
  char text[40];
  units::distanceText(text, sizeof(text), ring, plUnits());
  d.setFont(&fonts::Font0);
  d.setTextColor(plDim());
  d.setCursor(kPlMapX + kPlMapR - 30, kPlMapY + kPlMapR - 6);
  d.print(text);
  for (int i = n - 1; i >= 0; --i) {
    int32_t dist; int bearing, elev;
    if (!plDroneFrom(gPlDrones->heard[order[i]].d, dist, bearing, elev)) continue;
    int dx, dy;
    planes::mapPoint(uint32_t(dist), bearing, uint32_t(ring), kPlMapR - 5, dx, dy);
    const int16_t x = kPlMapX + dx, y = kPlMapY + dy;
    const char label[2] = {char('1' + i), 0};
    d.fillRoundRect(x - 4, y - 5, 9, 11, 2, i == gPlDroneSel ? TFT_WHITE : TFT_BLACK);
    d.setTextColor(i == gPlDroneSel ? TFT_BLACK : plPink());
    d.setCursor(x - 2, y - 3);
    d.print(label);
  }
  if (!n) {
    d.setFont(&fonts::Font2);
    d.setTextColor(plMuted());
    const uint32_t secs = (millis() - gPlDroneSince) / 1000;
    plWrap(String("Listening") + (secs < 60 ? String("...") : String(" for ") + (secs / 60) + " min.") +
           " No drones yet. Only ones nearby can be heard.", kPlListX + 4, 28, listW - 6, 16, 6);
  } else {
    const int first = max<int>(0, min<int>(gPlDroneSel - 1, n - kPlRows));
    for (int row = 0; row < kPlRows && first + row < n; ++row) {
      const int i = first + row;
      const drone_id::Heard& h = gPlDrones->heard[order[i]];
      const int16_t y = 25 + row * kPlRowH;
      const bool selected = i == gPlDroneSel;
      if (selected) d.fillRoundRect(kPlListX, y, listW + 1, kPlRowH - 1, 4, plPink());
      d.setFont(&fonts::Font0);
      d.setTextColor(selected ? TFT_BLACK : plPink());
      d.setCursor(kPlListX + 3, y + 4);
      d.print(i + 1);
      char name[24];
      drone_id::shortName(name, sizeof(name), h.d);
      d.setFont(&fonts::Font2);
      d.setTextColor(selected ? TFT_BLACK : TFT_WHITE);
      d.setCursor(kPlListX + 15, y);
      d.print(fitCurrentFontToWidth(name, listW - 17));
      char dist[12] = "", height[12] = "";
      int32_t m, distM; int bearing, elev;
      const bool high = drone_id::heightMeters(h.d, m);
      if (high) plDroneHeight(height, sizeof(height), m, true);
      if (plDroneFrom(h.d, distM, bearing, elev)) {
        plDroneDistance(dist, sizeof(dist), distM);
        snprintf(text, sizeof(text), "%s %s%s%s", planes::compass(bearing), dist, high ? " " : "", high ? height : "");
      } else {
        snprintf(text, sizeof(text), "%s%s%s", high ? height : "", high ? " up, " : "", drone_id::signalWord(h.rssi));
      }
      d.setFont(&fonts::Font0);
      d.setTextColor(selected ? TFT_BLACK : plMuted());
      d.setCursor(kPlListX + 15, y + 15);
      d.print(fitCurrentFontToWidth(text, listW - 17));
    }
  }
  plFooter(n ? ";. pick Enter info S keep ,/ more" : "Esc stops listening  ,/ more", true);
}

void plDrawDrone() {
  auto& d = gCanvas;
  const drone_id::Heard* h = plDroneByMac(gPlDroneShown);
  if (!h) { gPlView = PlView::Drones; plDrawDrones(); return; }
  const drone_id::Drone& dr = h->d;
  char name[24], line[72], a[16], b[16], c[16];
  drone_id::shortName(name, sizeof(name), dr);
  plHeader(name, plPink(), drone_id::statusWord(dr.status));
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  d.setCursor(8, 26);
  String kind = drone_id::typeName(dr.uaType);
  if (dr.selfId[0]) kind += String("  \"") + dr.selfId + "\"";
  d.print(fitCurrentFontToWidth(kind, 224));
  d.setFont(&fonts::Font0);
  d.setTextColor(plMuted());
  d.setCursor(8, 44);
  d.print(fitCurrentFontToWidth(dr.id[0] ? String("ID ") + dr.id : String("No ID heard yet"), 224));
  int32_t distM = 0, m = 0;
  int bearing = 0, elev = -1;
  const bool where = plDroneFrom(dr, distM, bearing, elev);
  if (where) {
    d.setTextColor(plAmber());
    d.setCursor(8, 62);
    d.print("LOOK");
    d.setFont(&fonts::FreeSansBold9pt7b);
    d.setTextColor(TFT_WHITE);
    d.setCursor(40, 56);
    d.print(planes::compassWord(bearing));
    d.setFont(&fonts::Font2);
    d.setTextColor(plAmber());
    d.setCursor(40, 74);
    if (elev >= 0) { planes::heightWords(line, sizeof(line), elev); d.print(line); }
    else d.print("how high isn't known");
  } else {
    d.setFont(&fonts::Font2);
    d.setTextColor(plMuted());
    plWrap(dr.located ? "Which way to look needs the Cap's GPS, outdoors." : "It hasn't said where it is yet.", 8, 55, 224, 15, 2);
  }
  // Height and distance, then speed: whichever it has said.
  String facts;
  if (drone_id::heightMeters(dr, m)) { plDroneHeight(a, sizeof(a), m, false); facts += String(a) + " up"; }
  if (where) { plDroneDistanceWords(b, sizeof(b), distM); facts += (facts.length() ? ", " : "") + String(b) + " away"; }
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_WHITE);
  d.setCursor(8, 90);
  d.print(fitCurrentFontToWidth(facts.length() ? facts : String("It hasn't said how high it is"), 228));
  if (dr.speedCms >= 0) {
    const bool us = plUnits() == units::System::US;
    snprintf(c, sizeof(c), "%ld %s", static_cast<long>(units::rounded(dr.speedCms * (us ? 0.0223694 : 0.036))), us ? "mph" : "km/h");
    if (dr.speedCms < 50) snprintf(line, sizeof(line), "Hovering");
    else if (dr.direction >= 0) snprintf(line, sizeof(line), "Moving %s to the %s", c, planes::compassWord(dr.direction));
    else snprintf(line, sizeof(line), "Moving %s", c);
    d.setCursor(8, 99);
    d.print(fitCurrentFontToWidth(line, 228));
  }
  if (dr.hasOperator && dr.located) {
    const int32_t apart = lab_gps::metersBetween(dr.latE7 / 100, dr.lonE7 / 100, dr.opLatE7 / 100, dr.opLonE7 / 100);
    const int toward = lab_gps::bearingDeg(dr.latE7 / 100, dr.lonE7 / 100, dr.opLatE7 / 100, dr.opLonE7 / 100);
    plDroneDistanceWords(b, sizeof(b), apart);
    if (toward < 0 || apart < 10) snprintf(line, sizeof(line), "%s right below it", dr.operatorType == 0 ? "Took off" : "Flown from");
    else snprintf(line, sizeof(line), "%s %s to its %s", dr.operatorType == 0 ? "Took off" : "Flown from", b, planes::compassWord(toward));
    d.setTextColor(plMuted());
    d.setCursor(8, 108);
    d.print(fitCurrentFontToWidth(line, 228));
  }
  snprintf(line, sizeof(line), "Signal %s, heard %lus ago, ch %u", drone_id::signalWord(h->rssi),
           static_cast<unsigned long>((millis() - h->lastMs) / 1000), unsigned(h->channel));
  d.setTextColor(plMuted());
  d.setCursor(8, 117);
  d.print(fitCurrentFontToWidth(line, 228));
  plFooter(",/ other drones  S I saw it!  Esc", false);
}

void plDrawDroneAlert() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  const drone_id::Heard* h = plDroneByMac(gPlDroneAlertMac);
  const bool flash = (millis() / 400) % 2 == 0;
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(flash ? plPink() : TFT_WHITE);
  plCentered("DRONE NEARBY!", 6);
  if (!h) { plFooter("Esc", false); return; }
  char name[24], text[48], a[16];
  drone_id::shortName(name, sizeof(name), h->d);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(TFT_WHITE);
  plCentered(name, 34);
  d.setFont(&fonts::Font2);
  int32_t distM = 0, m = 0;
  int bearing = 0, elev = -1;
  d.setTextColor(plAccent());
  if (plDroneFrom(h->d, distM, bearing, elev)) {
    plDroneDistanceWords(a, sizeof(a), distM);
    snprintf(text, sizeof(text), "to the %s, %s away", planes::compassWord(bearing), a);
  } else {
    snprintf(text, sizeof(text), "signal %s", drone_id::signalWord(h->rssi));
  }
  plCentered(text, 56);
  d.setTextColor(TFT_WHITE);
  if (drone_id::heightMeters(h->d, m)) {
    plDroneHeight(a, sizeof(a), m, false);
    snprintf(text, sizeof(text), "%s up%s%s", a, h->d.status ? ", " : "", drone_id::statusWord(h->d.status));
    plCentered(text, 73);
  } else if (h->d.status) {
    plCentered(drone_id::statusWord(h->d.status), 73);
  }
  d.setTextColor(plMuted());
  plCentered(drone_id::typeName(h->d.uaType), 90);
  plFooter("Enter more   S I saw it!   Esc", false);
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
  gPlLookUntilMs = 0;
  plFreeSpotted();
  plLoadReminder();
  camera_net::setBridge(gHubHost.c_str(), gHubPort, gDeviceToken.c_str(), kTlsRootCaPem);
  gPlOpenSince = millis();
  if (!gPlSky || !gPlSpace) gPlProblem = gPlSpaceProblem = "Not enough memory for Planes";
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
        gPlProblem = gPlSpaceProblem = String("Not enough memory for Planes (") + camera_net::memoryNote() + ")";
        plInvalidate();
      }
    }
    return;
  }
  if (camera_net::finished()) plFinished();
  if (!camera_net::busy() && !camera_net::finished()) plNextJob();
  // The station page follows its pass's countdown; a pass in progress keeps the screen awake.
  if (gPlView == PlView::LookUp && int32_t(now - gPlLookUntilMs) >= 0) { gPlView = gPlBack; plInvalidate(); }
  if (gPlView == PlView::GoOut) {
    const uint32_t unix = plNowUnix();
    if (unix && unix < gPlGoPass.end) gLastUserActivityMs = now;
  }
}

// Each step goes back one: a plane, a pass or the list of sightings to its page, the notices to
// where they came up over, and the other pages to the sky map, whose Esc leaves the app.
bool planesEscape() {
  switch (gPlView) {
    case PlView::LookUp: gPlView = gPlBack; break;
    case PlView::GoOut: gPlView = PlView::Passes; gPlPassFromReminder = false; break;
    case PlView::Plane: gPlView = PlView::Sky; break;
    case PlView::Pass: gPlView = PlView::Passes; gPlPassFromReminder = false; break;
    case PlView::Sightings: gPlView = PlView::Spotted; break;
    case PlView::DroneAlert: gPlView = gPlDroneBack; break;
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
      else if (enter && gPlSkyOk && gPlSky->count) { plSelectPlane(gPlSel); gPlView = PlView::Plane; }
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
    case PlView::Plane:
      if (left || right) plSelectPlane(gPlSel + (left ? -1 : 1));
      else if (lower == 's') { if (const planes::Plane* p = plSelected()) plSaveSighting(*p); }
      break;
    case PlView::LookUp:
      if (enter) {
        for (uint8_t i = 0; gPlSkyOk && i < gPlSky->count; ++i) if (!strcmp(gPlSky->planes[i].hex, gPlLookHex)) plSelectPlane(i);
        if (!gPlSelHex[0] || strcmp(gPlSelHex, gPlLookHex)) { gPlShown = gPlLook; strcpy(gPlSelHex, gPlLookHex); }
        gPlView = PlView::Plane;
      } else if (lower == 's') plSaveSighting(gPlLook);
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
    case PlView::DroneAlert:
      if (enter) { memcpy(gPlDroneShown, gPlDroneAlertMac, 6); gPlView = PlView::Drone; }
      else if (lower == 's') { if (const drone_id::Heard* h = plDroneByMac(gPlDroneAlertMac)) plSaveDrone(*h); }
      break;
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
    case PlView::LookUp: plDrawLookUp(); break;
    case PlView::Passes: plDrawPasses(); break;
    case PlView::Pass: plDrawPass(); break;
    case PlView::Now: plDrawNow(); break;
    case PlView::Spotted: plDrawSpotted(); break;
    case PlView::Sightings: plDrawSightings(); break;
    case PlView::GoOut: plDrawGoOut(); break;
    case PlView::Drones: plDrawDrones(); break;
    case PlView::Drone: plDrawDrone(); break;
    case PlView::DroneAlert: plDrawDroneAlert(); break;
  }
  if (plNoticeShowing()) drawBigNotice(gPlNotice, gPlNoticeInk);
  renderLauncherOverlay();
  renderDebugOverlay();
}
}  // namespace
