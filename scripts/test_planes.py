#!/usr/bin/env python3
"""Planes: the bridge's lines read as the device reads them (and, when the bridge's packages are
installed, the bridge's own answers fed straight into the device's reader), the home door's paths,
the words the screens show (which way to look, fists above the horizon, heights and distances in
both units, the station's days and times), reminders and sightings kept and read back, the
Spotted totals, all in planes_catalog.h under ASan/UBSan; and the app wired into the menu, keys,
Esc, the loop, the screensaver and the network the way the other apps are."""
from pathlib import Path
import os, shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
part = lambda name: (parts / name).read_text()
app = part('096_main.cpp.inc')

# Wired in like the other apps.
for f, s in [('002_main.cpp.inc', '  Cameras,\n  Planes,\n'),
             ('004_main.cpp.inc', '{"Pocket Synth", UiMode::PocketSynth}, {"Skywatch", UiMode::Planes}}},'),
             ('004_main.cpp.inc', '{"Tools", "Local hardware tools", rgb565_local(255, 210, 64), 7,'),
             ('007_main.cpp.inc', 'case UiMode::Planes: return "Skywatch";'),
             ('022_main.cpp.inc', 'out = UiMode::Planes;'),
             ('043_main.cpp.inc', 'cmd == "/skywatch" || cmd == "/planes"'),
             ('047_main.cpp.inc', 'gUiMode == UiMode::Planes && !gLauncherVisible && planesEscape()'),
             ('049_main.cpp.inc', 'gUiMode == UiMode::Cameras || gUiMode == UiMode::Planes) && (upDown || downDown || leftDown || rightDown)'),
             ('052_main.cpp.inc', 'case UiMode::Planes: return kAirliner;'),
             ('061_main.cpp.inc', 'renderPlanesUi();'),
             ('063_main.cpp.inc', 'case UiMode::Planes: return false;')]:
    assert s in part(f), (f, s)
assert 'constexpr uint8_t kAirliner[32]' in (src / 'launcher_icons.h').read_text()
assert 'main_parts/096_main.cpp.inc' in (src / 'main.cpp').read_text()
mode = part('024_main.cpp.inc').split('void setUiMode(UiMode mode) {', 1)[1]
assert mode.index('if (modeChanged && gUiMode == UiMode::Planes) stopPlanes();') < mode.index('  gUiMode = mode;') < \
       mode.index('if (modeChanged && mode == UiMode::Planes) beginPlanes();'), 'its worker and GPS stop before the next app starts'
loop = part('061_main.cpp.inc').split('void loop() {', 1)[1]
# The reminder is checked ahead of every early return but Realtime's, so the radio playing doesn't hold it.
assert loop.index('servicePlanes();') < loop.index('servicePlanesReminder();') < loop.index('if (vlcBusy())') < loop.index('if (radioBusy())')
typing = part('048_main.cpp.inc').split('void handleTypingKey(', 1)[1]
assert typing.index("if (key == '`'") < typing.index('handlePlanesKey(key, keys)) return;') < typing.index('if (handleFocusShortcut(lowerKey,keys)) return;'), \
    'its S, R, - and = come before the shortcuts that use them elsewhere, but after Esc'
saver = part('009_main.cpp.inc')
assert 'gUiMode == UiMode::Cameras ||\n                              gUiMode == UiMode::Planes)) {' in saver, 'the screen dims instead, so Look up! can still come'
wake = saver.split('bool handleScreensaverWake() {', 1)[1].split('\n}\n', 1)[0]
assert wake.index('gUiMode == UiMode::Planes && gScreensaverPreviousMode == UiMode::Planes') < wake.index('setUiMode(target);')
# It borrows the Cameras worker and a GPS holder bit no one else uses.
assert 'camera_net::open()' in app and 'camera_net::close();' in app.split('void stopPlanes() {', 1)[1].split('\n}\n', 1)[0]
holders = part('081_main.cpp.inc').split('constexpr uint8_t kGpsForLab', 1)[1].split(';', 1)[0]
assert '16' not in holders and 'constexpr uint8_t kGpsForPlanes = 16;' in app
stop = app.split('void stopPlanes() {', 1)[1].split('\n}\n', 1)[0]
assert 'gpsRelease(kGpsForPlanes)' in stop and 'delete gPlSky;' in stop and 'delete gPlSpace;' in stop and 'plFreeSpotted();' in stop
# Esc goes back one step at a time; only the sky map leaves the app (the owner's rule).
esc = app.split('bool planesEscape() {', 1)[1].split('\n}\n', 1)[0]
assert 'case PlView::Sky: return false;' in esc and esc.count('return false') == 1
# Its HTTPS passes the memory guard, as Cameras' does: the ticket's handshake restarted 0.2.323.
assert '(gUiMode == UiMode::Cameras || gUiMode == UiMode::Planes) &&\n         ((camera_net::busy() && !camera_net::overHome()) || camera_net::tlsOpen());' in part('095_main.cpp.inc')
guard = part('010_main.cpp.inc').split('void serviceMemoryGuard() {', 1)[1]
assert guard.index('if (cameraHttpsBusy()) { criticalSinceMs = 0; return; }') < guard.index('ESP.restart();')
assert 'gPlNearDueMs = now + (viaHome ? kPlNearEveryMs : kPlNearAwayMs);' in app
# The plane's screen draws its sky and its path; L lists every plane, and Esc goes back to where it came from.
plane_screen = app.split('void plDrawPlane() {', 1)[1].split('\n}\n', 1)[0]
assert 'plDrawRound(true);' in plane_screen and 'planes::skyAt(p, s)' in plane_screen
assert "else if (lower == 'l') { gPlView = PlView::PlaneList; }" in app
assert 'case PlView::Plane: gPlView = gPlPlaneBack == PlView::PlaneList ? PlView::PlaneList : PlView::Sky; break;' in esc
# The sky page always says where it's looking from: the GPS or the saved city.
sky = app.split('void plDrawSky() {', 1)[1].split('\n}\n', 1)[0]
assert sky.count('plWhereLine(') == 2, 'with planes and without'
where = app.split('void plWhereWords(', 1)[1].split('\n}\n', 1)[0]
assert 'Using GPS' in where and ', no GPS' in where and ', no fix' in where
# Every screen has the same frame: its keys on caps along the bottom, and the pages the top's icons.
# Nothing pops up or closes by itself: a plane overhead flashes on the map and chimes once, softly.
assert 'PlView::LookUp' not in app and 'PlView::DroneAlert' not in app and 'focusTone(' not in app
note = app.split('void plNoteOverhead() {', 1)[1].split('\n}\n', 1)[0]
assert 'gPlView' not in note.replace('plWantsSky()', '') and 'plChime(planes::Chime::LookUp);' in note
assert "else if (lower == 'u' && plOverhead() >= 0)" in app
for fn in ['plDrawSky', 'plDrawPlane', 'plDrawPasses', 'plDrawPass', 'plDrawNow', 'plDrawSpotted',
           'plDrawSightings', 'plDrawGoOut', 'plDrawDrones', 'plDrawDrone', 'plDrawPlaneList']:
    body = app.split('void %s() {' % fn, 1)[1].split('\n}\n', 1)[0]
    assert 'plKeys({' in body, fn
keys = app.split('void plKeys(', 1)[1].split('\n}\n', 1)[0]
assert 'if (x + capW + 3 + wordW > 238) break;' in keys, 'a key that would run off the screen is left off'
# The canvas stays allocated, and no position is ever logged.
assert 'releaseUiCanvasForNetwork' not in app and 'createSprite' not in app
for line in app.splitlines():
    if 'appendRuntimeLog' in line or 'logf(' in line: assert 'lat' not in line.lower() and 'weatherPlace' not in line, line
# The reminder rings once and is forgotten, even when it was missed.
remind = app.split('void servicePlanesReminder() {', 1)[1].split('\n}\n', 1)[0]
assert remind.index('gPlRemindAt = 0;') < remind.index('if (now >= pass.end) return;') < remind.index('setUiMode(UiMode::Planes);')

code = r'''
#include "planes_catalog.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
using namespace planes;
static std::string text(const char* s) { return s; }

int main() {
  // --- The bridge's lines ----------------------------------------------------------------
  const char near[] =
    "at\tHere\n"
    "plane\ta35a98\tEnvoy Air 3390\tEmbraer 175\tEYW\tKey West\tORD\tChicago\t475\t-640\t130\t270\t1425\t49\t-2\t16\t929\tA3\n"
    "plane\tad9edf\tAmerican 2278\tBoeing 737-800\tPHL\tPhiladelphia\tMIA\tMiami\t34000\t1200\t452\t198\t19312\t315\t28\t-1\t-1\tA3\n"
    "plane\ta1b2c3\t-\t-\t-\t-\t-\t-\t1100\t0\t90\t-1\t900\t120\t35\t-1\t-1\t-\n"
    "plane\tBAD\tX\t-\t-\t-\t-\t-\t1\t0\t0\t0\t0\t0\t0\t-1\t-1\t-\n"
    "plane\ta1b2c4\tToo few\n"
    "junk line\n";
  Sky sky;
  assert(parseNear(near, sizeof(near) - 1, sky));
  assert(text(sky.place) == "Here" && sky.count == 3);
  const Plane& envoy = sky.planes[0];
  assert(text(envoy.hex) == "a35a98" && text(envoy.name) == "Envoy Air 3390" && text(envoy.type) == "Embraer 175");
  assert(text(envoy.from) == "EYW" && text(envoy.fromCity) == "Key West" && text(envoy.to) == "ORD" && text(envoy.toCity) == "Chicago");
  assert(envoy.altFt == 475 && envoy.climbFpm == -640 && envoy.speedKt == 130 && envoy.track == 270 && envoy.distM == 1425);
  assert(envoy.bearing == 49 && envoy.elev == -2 && envoy.closestS == 16 && envoy.closestM == 929 && text(envoy.category) == "A3");
  const Plane& unknown = sky.planes[2];
  assert(text(unknown.name) == "A1B2C3" && !unknown.type[0] && !unknown.from[0] && !unknown.category[0] && unknown.track == -1);
  Sky empty;
  assert(!parseNear("plane\tnope\n", 11, empty) && empty.count == 0);
  assert(parseNear("at\tSalem\n", 9, empty) && text(empty.place) == "Salem" && empty.count == 0);

  const char iss[] =
    "at\tSalem\n"
    "now\t1794017715\t5146\t-4601\t425\t27587\tthe Atlantic Ocean\t7\n"
    "pass\t1794613402\t229\t10\t1794613601\t144\t77\t1794613697\t62\t28\t-39\t1\t229:10,228:15,227:21,225:30,219:46,192:70,85:66,66:42,62:28\n"
    "pass\t1794700000\t300\t10\t1794700200\t20\t40\t1794700400\t60\t10\t-12\t0\t-\n"
    "pass\t5\t1\t1\t4\t1\t1\t6\t1\t1\t0\t0\t-\n"        // Peak before start: left out.
    "pass\t1794800000\t300\t10\t1794800200\t20\t40\t1794800400\t60\t10\t-12\t0\t1:2,3\n";  // A broken path: left out.
  Space space;
  assert(parseIss(iss, sizeof(iss) - 1, space));
  assert(text(space.place) == "Salem" && space.count == 2);
  assert(space.now.known && space.now.at == 1794017715u && space.now.latE2 == 5146 && space.now.lonE2 == -4601);
  assert(space.now.altKm == 425 && space.now.speedKmh == 27587 && text(space.now.over) == "the Atlantic Ocean" && space.now.crew == 7);
  const Pass& p = space.passes[0];
  assert(p.start == 1794613402u && p.startAz == 229 && p.startEl == 10 && p.peak == 1794613601u && p.peakAz == 144 && p.peakEl == 77);
  assert(p.end == 1794613697u && p.endAz == 62 && p.endEl == 28 && p.magTenths == -39 && p.shadow && p.points == 9);
  assert(p.pathAz[5] == 192 && p.pathEl[5] == 70 && p.pathAz[8] == 62 && p.pathEl[8] == 28);
  assert(space.passes[1].points == 0 && !space.passes[1].shadow);
  Space blankSpace;
  assert(parseIss("at\t-\nnow\tx\n", 12, blankSpace) && !blankSpace.place[0] && !blankSpace.now.known);

  // --- Where to ask ------------------------------------------------------------------------
  char path[200], where[96];
  assert(placeQuery(where, sizeof(where), "Salem, MA") && text(where) == "place=Salem%2C%20MA");
  assert(!placeQuery(where, sizeof(where), ""));
  assert(nearPath(path, sizeof(path), "lat=42.51&lon=-70.90", 40234) && text(path) == "/api/cardputer/planes/near?lat=42.51&lon=-70.90&r=40234");
  assert(issPath(path, sizeof(path), where) && text(path) == "/api/cardputer/planes/iss?place=Salem%2C%20MA");
  assert(!nearPath(path, 20, "lat=42.51&lon=-70.90", 40234));
  podcast::HomeTicket t;
  const char ticket[] = "ticket\t31890\t/planes/0123456789abcdef0123456789abcdef\nlan\t192.168.0.20\t22\n";
  assert(planes::parseTicket(ticket, sizeof(ticket) - 1, t) && !t.none && t.port == 31890 && t.count == 1);
  assert(podcast::homeFor(t, (192u << 24) | (168u << 16) | (2u << 8) | 9u) == ((192u << 24) | (168u << 16) | 20u));
  assert(planes::homePath(path, sizeof(path), t, "/api/cardputer/planes/near?lat=1.00&lon=2.00&r=3000") &&
         text(path) == "/planes/0123456789abcdef0123456789abcdef/near?lat=1.00&lon=2.00&r=3000");
  assert(!planes::homePath(path, sizeof(path), t, "/api/cardputer/cameras/list"));
  assert(planes::parseTicket("none\n", 5, t) && t.none);
  assert(!planes::parseTicket("ticket\t31890\t/cameras/0123456789abcdef0123456789abcdef\n", 54, t));  // Another app's door.
  assert(!planes::parseTicket("<html>\n", 7, t));

  // --- Words ---------------------------------------------------------------------------------
  assert(text(compass(0)) == "N" && text(compass(22)) == "N" && text(compass(23)) == "NE" && text(compass(315)) == "NW");
  assert(text(compass(359)) == "N" && text(compass(-45)) == "NW" && text(compass(540)) == "S");
  assert(text(compassWord(300)) == "NORTH-WEST" && text(compassWord(90)) == "EAST");
  char s[64];
  heightWords(s, sizeof(s), 28); assert(text(s) == "3 fists above the horizon");
  heightWords(s, sizeof(s), 6); assert(text(s) == "1 fist above the horizon");
  heightWords(s, sizeof(s), 3); assert(text(s) == "low, near the horizon");
  heightWords(s, sizeof(s), 85); assert(text(s) == "straight up!");
  heightShort(s, sizeof(s), 60); assert(text(s) == "6 fists up");
  altitudeText(s, sizeof(s), 34000, units::System::US); assert(text(s) == "34,000 ft");
  altitudeText(s, sizeof(s), 34000, units::System::Metric); assert(text(s) == "10,363 m");
  altitudeText(s, sizeof(s), 450, units::System::US); assert(text(s) == "450 ft");
  altitudeShort(s, sizeof(s), 34000, units::System::US); assert(text(s) == "34k ft");
  altitudeShort(s, sizeof(s), 5200, units::System::US); assert(text(s) == "5,200 ft");
  altitudeShort(s, sizeof(s), 9960, units::System::US); assert(text(s) == "10k ft");
  altitudeShort(s, sizeof(s), 5000, units::System::Metric); assert(text(s) == "1,520 m");
  altitudeShort(s, sizeof(s), 800, units::System::US); assert(text(s) == "800 ft");
  altitudeShort(s, sizeof(s), 34000, units::System::Metric); assert(text(s) == "10k m");
  grouped(s, sizeof(s), 1234567); assert(text(s) == "1,234,567");
  grouped(s, sizeof(s), -1200); assert(text(s) == "-1,200");
  grouped(s, sizeof(s), 999); assert(text(s) == "999");
  speedText(s, sizeof(s), 445, units::System::US); assert(text(s) == "512 mph");
  speedText(s, sizeof(s), 445, units::System::Metric); assert(text(s) == "824 km/h");
  distanceShort(s, sizeof(s), 19312, units::System::US); assert(text(s) == "12mi");
  distanceShort(s, sizeof(s), 966, units::System::US); assert(text(s) == "0.6mi");
  distanceShort(s, sizeof(s), 10, units::System::US); assert(text(s) == "0.1mi");
  distanceShort(s, sizeof(s), 1530, units::System::US); assert(text(s) == "1mi");
  distanceShort(s, sizeof(s), 19312, units::System::Metric); assert(text(s) == "19km");
  assert(text(climbWord(1200, 30000)) == "climbing" && text(climbWord(-800, 5000)) == "coming down" &&
         text(climbWord(-800, 30000)) == "descending" && text(climbWord(100, 30000)) == "level");
  closestText(s, sizeof(s), -1); assert(text(s) == "moving away");
  closestText(s, sizeof(s), 20); assert(text(s) == "closest now");
  closestText(s, sizeof(s), 130); assert(text(s) == "closest in 2 min");
  assert(isDrone(Plane{}) == false);
  Plane drone; strcpy(drone.category, "B6"); assert(isDrone(drone));
  Plane high; high.elev = 55; high.altFt = 3000; assert(wantsLookUp(high));
  high.elev = 49; assert(!wantsLookUp(high));
  Plane parked; parked.elev = 80; parked.altFt = 0; assert(!wantsLookUp(parked));
  assert(kDefaultRange == 0 && rangeMeters(kDefaultRange, units::System::US) == 8047);  // 5 miles to begin with.
  assert(rangeMeters(2, units::System::US) == 40234 && rangeMeters(0, units::System::Metric) == 8000 &&
         rangeMeters(9, units::System::US) == rangeMeters(kDefaultRange, units::System::US));
  rangeLabel(s, sizeof(s), 0, units::System::US); assert(text(s) == "5mi");
  rangeLabel(s, sizeof(s), 3, units::System::US); assert(text(s) == "50mi");
  rangeLabel(s, sizeof(s), 3, units::System::Metric); assert(text(s) == "80km");

  // The round map: north up, east right, and nothing outside the ring.
  int dx, dy;
  mapPoint(20000, 0, 40000, 40, dx, dy); assert(dx == 0 && dy == -20);
  mapPoint(40000, 90, 40000, 40, dx, dy); assert(dx == 40 && dy == 0);
  mapPoint(90000, 225, 40000, 40, dx, dy); assert(dx == -28 && dy == 28);
  skyPoint(0, 90, 46, dx, dy); assert(dx == 0 && dy == 0);
  skyPoint(270, 0, 46, dx, dy); assert(dx == -46 && dy == 0);
  skyPoint(180, -5, 46, dx, dy); assert(dx == 0 && dy == 46);
  skyPoint(90, 45, 46, dx, dy); assert(dx == 23 && dy == 0);

  {
  // Where a plane is in the sky over time: now where it's drawn, ahead along its track.
  Plane over; over.distM = 1300; over.bearing = 80; over.elev = 50; over.track = 270; over.speedKt = 160; over.climbFpm = -800; over.altFt = 5200;
  SkySpot now0 = skyAt(over, 0); assert(now0.az == 80 && now0.el == 50);
  SkySpot next = skyAt(over, 15); assert(next.el > 70);                  // Crossing almost overhead...
  SkySpot later = skyAt(over, 60); assert(later.az > 240 && later.az < 300 && later.el > 0 && later.el < 40);  // ...then low in the west.
  SkySpot gone = skyAt(over, 600); assert(gone.el < 0);                   // Coming down: under the horizon in the end.
  Plane still = over; still.track = -1; assert(skyAt(still, 300).az == 80 && skyAt(still, 300).el < 50);  // No track: it only sinks.
  Plane far; far.distM = 160000; far.bearing = 0; far.elev = 2; far.track = 0; far.speedKt = 450; far.climbFpm = 0;
  assert(skyAt(far, 300).el < 2 && skyAt(far, 300).az == 0);              // Going away, the Earth's curve takes it down.
  }

  {
    // Chimes are soft bells: quiet, starting from nothing and fading to nothing, nothing low.
    for (int k = 0; k < 3; ++k) {
      const Chime c = Chime(k);
      const ChimeShape shape = chimeShape(c);
      int16_t* buf = new int16_t[8000];
      const size_t n = renderChime(c, buf, 8000, 8000);
      assert(n == size_t(8 * shape.ms) && n <= 8000);
      int peak = 0;
      for (size_t i = 0; i < n; ++i) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
      assert(peak > 300 && peak < 3000);                     // Far under full scale (32767), and not silent.
      assert(abs(buf[0]) < 50 && abs(buf[n - 1]) < 50);      // No click at either end.
      for (uint8_t i = 0; i < shape.count; ++i) assert(shape.hz[i] >= 1000);
      assert(renderChime(c, buf, 100, 8000) == 100);         // Never past the room it's given.
      delete[] buf;
    }
  }

  // --- The station ---------------------------------------------------------------------------
  assert(stars(-39) == 4 && stars(-25) == 3 && stars(-12) == 2 && stars(2) == 1);
  assert(minutesLong(p) == 5);
  Pass brief; brief.start = 100; brief.end = 120; assert(minutesLong(brief) == 1);
  assert(orbitMinutes(420) == 93 && orbitMinutes(0) == 93);
  setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1); tzset();
  const auto at = [](int y, int mo, int d, int h, int mi) { tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1; mktime(&t); return t; };
  const tm now = at(2026, 11, 6, 17, 56);
  dayWord(s, sizeof(s), now, at(2026, 11, 6, 18, 12)); assert(text(s) == "Tonight");
  dayWord(s, sizeof(s), now, at(2026, 11, 6, 9, 0)); assert(text(s) == "Today");
  dayWord(s, sizeof(s), now, at(2026, 11, 7, 5, 24)); assert(text(s) == "Tomorrow");
  dayWord(s, sizeof(s), now, at(2026, 11, 8, 18, 11)); assert(text(s) == "Sunday");
  dayWord(s, sizeof(s), now, at(2026, 11, 16, 18, 0)); assert(text(s) == "Nov 16");
  dayWord(s, sizeof(s), at(2026, 3, 7, 20, 0), at(2026, 3, 8, 20, 0)); assert(text(s) == "Tomorrow");  // Across a clock change.
  clockText(s, sizeof(s), at(2026, 11, 6, 18, 12), true); assert(text(s) == "6:12 pm");
  clockText(s, sizeof(s), at(2026, 11, 6, 0, 5), true); assert(text(s) == "12:05 am");
  clockText(s, sizeof(s), at(2026, 11, 6, 18, 12), false); assert(text(s) == "18:12");
  Pass soon; soon.start = 1000; soon.peak = 1200; soon.end = 1400;
  untilText(s, sizeof(s), 40, soon); assert(text(s) == "in 16 min");
  Pass later; later.start = 10000; later.peak = 10200; later.end = 10400;
  untilText(s, sizeof(s), 2500, later); assert(text(s) == "in 2 h 5 min");
  untilText(s, sizeof(s), 1100, soon); assert(text(s) == "up now!");
  untilText(s, sizeof(s), 1400, soon); assert(text(s) == "over");
  assert(nextPass(space, 0) == 0 && nextPass(space, 1794613697u) == 1 && nextPass(space, 2000000000u) == -1);

  // A reminder survives being written down, and a damaged one is refused.
  char saved[96];
  uint32_t remindAt = 0;
  Pass back;
  formatReminder(saved, sizeof(saved), p.start - kRemindBeforeS, p);
  assert(parseReminder(saved, remindAt, back) && remindAt == p.start - 600 && back.start == p.start && back.peak == p.peak &&
         back.end == p.end && back.startAz == 229 && back.peakAz == 144 && back.peakEl == 77 && back.endAz == 62 && back.shadow);
  assert(!parseReminder("", remindAt, back) && !parseReminder("1 2 3", remindAt, back));
  assert(!parseReminder("5 10 9 20 0 0 10 0 0", remindAt, back));     // Its top before its start.
  assert(!parseReminder("5 10 15 20 400 0 10 0 0", remindAt, back));  // No such direction.

  // --- Spotted -------------------------------------------------------------------------------
  char line[128];
  const size_t n = formatSighting(line, sizeof(line), 1794017715u, sky.planes[1]);
  assert(n && text(line) == "1794017715\tad9edf\tAmerican 2278\tBoeing 737-800\t34000\n");
  line[n - 1] = 0;
  Sighting got;
  assert(parseSighting(line, got) && got.at == 1794017715u && text(got.hex) == "ad9edf" && text(got.name) == "American 2278" &&
         text(got.type) == "Boeing 737-800" && got.altFt == 34000);
  char bad[] = "17\tzzzzzz\tName\tType\t100";
  assert(!parseSighting(bad, got));
  char airline[24];
  assert(airlineOf("United 1534", airline, sizeof(airline)) && text(airline) == "United");
  assert(airlineOf("Envoy Air 3390", airline, sizeof(airline)) && text(airline) == "Envoy Air");
  assert(!airlineOf("N512AB", airline, sizeof(airline)) && !airlineOf("A1B2C3", airline, sizeof(airline)) && !airlineOf("Big 7a", airline, sizeof(airline)));
  Stats* stats = new Stats();
  const auto add = [&](uint32_t when, const char* name, const char* type, int32_t alt) {
    Sighting x; x.at = when; strcpy(x.hex, "abcdef"); strcpy(x.name, name); strcpy(x.type, type); x.altFt = alt; stats->add(x);
  };
  add(100, "Southwest 2210", "Boeing 737-800", 5200);
  add(200, "Southwest 18", "Boeing 737 MAX 8", 31000);
  add(300, "N512AB", "Cessna 172", 1100);
  add(400, "United 9", "Boeing 777-300ER", 41000);
  add(500, "Southwest 77", "Boeing 737-800", 9000);
  assert(stats->total == 5 && stats->airlines.count == 2 && stats->types.count == 4);
  assert(text(stats->highest.type) == "Boeing 777-300ER" && stats->highest.altFt == 41000);
  assert(text(stats->lowest.type) == "Cessna 172" && text(stats->last.name) == "Southwest 77");
  const int top = stats->airlines.top();
  assert(top >= 0 && text(stats->airlines.names[top]) == "Southwest" && stats->airlines.counts[top] == 3);
  const int newest = stats->newestType();
  assert(newest >= 0 && text(stats->types.names[newest]) == "Boeing 777-300ER" && stats->types.first[newest] == 400);
  for (int i = 0; i < 80; ++i) { char name[16]; snprintf(name, sizeof(name), "Type %d", i); stats->types.add(name, 1000 + i); }
  assert(stats->types.count == Tally::kNames);  // Full: more are counted in the total but not named.
  delete stats;
  puts("planes catalog: ok");
}
'''

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-no-pie', '-I' + str(src), str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)

    # The bridge's real answers, made from a fake internet, read by the device's own reader.
    node = shutil.which('node')
    bridge = root / 'bridge'
    if node and (bridge / 'node_modules/satellite.js').exists():
        script = r'''
import { PlanesService, URLS } from "%s";
const TLE = `ISS (ZARYA)
1 25544U 98067A   26310.49826972  .00011492  00000+0  21472-3 0  9994
2 25544  51.6322 315.8635 0007225 355.0594   5.0322 15.49214228593815
`;
const json = (body) => ({ ok: true, status: 200, json: async () => body, text: async () => JSON.stringify(body) });
const text = (body) => ({ ok: true, status: 200, json: async () => JSON.parse(body), text: async () => body });
const routes = [
  [URLS.planes, json({ ac: [
    { hex: "ad9edf", flight: "AAL2278 ", r: "N977NN", t: "B738", alt_baro: 34000, alt_geom: 34500, baro_rate: 1200, gs: 445, track: 198,
      lat: 42.10, lon: -88.05, category: "A3" },
    { hex: "a1b2c3", r: "N512AB", t: "C172", alt_baro: 2500, gs: 95, track: 40, lat: 41.99, lon: -87.89, category: "A1" },
    { hex: "b00b00", flight: "UAL1 ", t: "B39M", alt_baro: "ground", lat: 41.98, lon: -87.90 } ] })],
  [URLS.elevation, json({ elevation: [200] })],
  [URLS.routes + "/AAL2278", json({ response: { flightroute: { airline: { name: "American Airlines" },
    origin: { iata_code: "PHL", municipality: "Philadelphia" }, destination: { iata_code: "MIA", municipality: "Miami" } } } })],
  [URLS.routes, json({ response: "unknown callsign" })],
  [URLS.aircraft, json({ response: "unknown aircraft" })],
  [URLS.tle, text(TLE)],
  [URLS.reverse, json({ error: "Unable to geocode" })],
  [URLS.crew, json({ number: 10, people: [{ iss: true }, { iss: true }, { iss: false }] })],
];
const fetchImpl = async (url) => { for (const [prefix, answer] of routes) if (String(url).startsWith(prefix)) return answer; throw new Error(url); };
const service = new PlanesService({ fetchImpl, now: () => Date.UTC(2026, 10, 6, 12, 0, 0), log: { info() {}, warn() {} } });
const where = new URLSearchParams({ lat: "41.98", lon: "-87.90", r: "40000" });
process.stdout.write(await service.near(where));
process.stdout.write("=====\n");
process.stdout.write(await service.iss(where));
''' % (bridge / 'src/planes.mjs').as_uri()
        (tmp / 'answers.mjs').write_text(script)
        out = subprocess.run([node, str(tmp / 'answers.mjs')], check=True, capture_output=True, text=True).stdout
        nearText, issText = out.split('=====\n')
        reader = r'''
#include "planes_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
static const char kNear[] = R"BRIDGE(%s)BRIDGE";
static const char kIss[] = R"BRIDGE(%s)BRIDGE";
int main() {
  planes::Sky sky;
  assert(planes::parseNear(kNear, strlen(kNear), sky));
  assert(std::string(sky.place) == "Here" && sky.count == 2);  // The one on the ground is left out.
  assert(std::string(sky.planes[0].hex) == "a1b2c3" && std::string(sky.planes[0].name) == "N512AB" && sky.planes[0].altFt == 2500);
  const planes::Plane& aa = sky.planes[1];
  assert(std::string(aa.name) == "American 2278" && std::string(aa.type) == "Boeing 737-800");
  assert(std::string(aa.from) == "PHL" && std::string(aa.toCity) == "Miami" && aa.altFt == 34000 && aa.climbFpm == 1200);
  assert(aa.speedKt == 445 && aa.track == 198 && aa.bearing > 270 && aa.bearing < 360 && aa.elev > 10 && aa.elev < 60);
  planes::Space space;
  assert(planes::parseIss(kIss, strlen(kIss), space));
  assert(std::string(space.place) == "Here" && space.now.known && space.now.crew == 2 && space.now.altKm > 380 && space.now.altKm < 460);
  assert(std::string(space.now.over).rfind("the ", 0) == 0);
  assert(space.count >= 1);
  for (int i = 0; i < space.count; ++i) {
    const planes::Pass& p = space.passes[i];
    assert(p.start <= p.peak && p.peak <= p.end && p.end - p.start >= 60 && p.peakEl >= 15 && p.points == planes::kPathPoints);
  }
  printf("bridge to device: %%d planes, %%d passes read\n", sky.count, space.count);
}
''' % (nearText, issText)
        (tmp / 'reader.cpp').write_text(reader)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-no-pie', '-I' + str(src), str(tmp / 'reader.cpp'), '-o', str(tmp / 'reader')], check=True)
        subprocess.run([str(tmp / 'reader')], check=True)
    else:
        print('bridge to device: skipped (needs node and npm install in bridge/)')
print('PASS Planes: lines, paths, words, units, the station, reminders, sightings and Spotted; wired like the other apps')
