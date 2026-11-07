#!/usr/bin/env python3
"""Cameras: the bridge's lines read as the device reads them, the home door's paths, the HTTP
answers the worker reads, the wall's layout fitting the screen, the times it shows and the keys,
all in camera_catalog.h under ASan/UBSan; and the app wired into the menu, the House tab, keys,
Esc, the loop, the screensaver and the canvas the way the other apps are."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
part = lambda name: (parts / name).read_text()
app = part('095_main.cpp.inc')

# Wired in like the other apps.
for f, s in [('002_main.cpp.inc', '  TvRemote,\n  Cameras,\n};'),
             ('004_main.cpp.inc', '{"House", "Cameras and remotes", rgb565_local(85, 146, 255), 4,'),
             ('004_main.cpp.inc', '{{"Cameras", UiMode::Cameras}, {"TV Remote", UiMode::TvRemote}, {"smartLamp", UiMode::SmartLamp},'),
             ('007_main.cpp.inc', 'case UiMode::Cameras: return "Cameras";'),
             ('022_main.cpp.inc', 'out = UiMode::Cameras;'),
             ('047_main.cpp.inc', 'gUiMode == UiMode::Cameras && !gLauncherVisible && camerasEscape()'),
             ('049_main.cpp.inc', '(gUiMode == UiMode::TvRemote || gUiMode == UiMode::Cameras) && (upDown || downDown || leftDown || rightDown)'),
             ('052_main.cpp.inc', 'case UiMode::Cameras: return kCamera;'),
             ('061_main.cpp.inc', 'renderCamerasUi();'),
             ('063_main.cpp.inc', 'case UiMode::Cameras: return false;')]:
    assert s in part(f), (f, s)
icons = (src / 'launcher_icons.h').read_text()
assert 'constexpr uint8_t kCamera[32]' in icons and 'constexpr uint8_t kHouse[32]' in icons
assert 'kWrench, kHouse,' in icons, 'the House tab has its icon in its place'
assert '"vamfthgrsyl"' in (src / 'launcher_shortcuts.h').read_text(), 'H jumps to House'
assert 'main_parts/095_main.cpp.inc' in (src / 'main.cpp').read_text()
# The House tab moved three apps out of Tools, which keeps the rest.
groups = part('004_main.cpp.inc')
tools = groups.split('{"Tools", "Local hardware tools"', 1)[1].split('}}},', 1)[0]
for gone in ('IR Remote', 'smartLamp', 'TV Remote'): assert gone not in tools, gone
for kept in ('Calculator', 'NFC Notes', 'RFID Lab', 'Sonar', 'Mesh', 'Pocket Synth'): assert kept in tools, kept
assert 'kSettingsGroup = 8, kSystemGroup = 9' in part('001_main.cpp.inc')
mode = part('024_main.cpp.inc').split('void setUiMode(UiMode mode) {', 1)[1]
assert mode.index('if (modeChanged && gUiMode == UiMode::Cameras) stopCameras();') < mode.index('  gUiMode = mode;') < \
       mode.index('if (modeChanged && mode == UiMode::Cameras) beginCameras();'), 'its worker stops before the next app starts'
loop = part('061_main.cpp.inc').split('void loop() {', 1)[1]
assert loop.index('serviceCameras();') < loop.index('if (radioBusy())')
typing = part('048_main.cpp.inc').split('void handleTypingKey(', 1)[1]
assert typing.index("if (key == '`'") < typing.index('handleCamerasKey(key, keys)) return;') < typing.index('if (handleFocusShortcut(lowerKey,keys)) return;'), \
    'its L, R and S come before the shortcuts that use them elsewhere, but after Esc'
saver = part('009_main.cpp.inc')
assert 'gUiMode == UiMode::TvRemote || gUiMode == UiMode::Cameras)) {' in saver, 'the saver would leave the app and its picture'
wake = saver.split('bool handleScreensaverWake() {', 1)[1].split('\n}\n', 1)[0]
assert wake.index('gUiMode == UiMode::Cameras') < wake.index('setUiMode(target);')
# A picture drawn straight to the screen isn't covered by the canvas.
present = part('006_main.cpp.inc').split('void presentUiFrame() {', 1)[1].split('\n}\n', 1)[0]
assert 'gCanvasSpriteActive && !cameraDrawsDirect()' in present
# The canvas stays allocated: the app never gives it back or makes it again (it would split the heap TLS needs).
assert 'releaseUiCanvasForNetwork' not in app and 'restoreUiCanvasAfterNetwork' not in app and 'createSprite' not in app
# The sign-in screen's words fit beside its QR code (142 to 238) and under each other, in Font2's
# real widths. Lines shown without the code have the screen's width.
import re as _re
sign_in = app.split('void camDrawSignIn() {', 1)[1].split('\n}\n', 1)[0]
font16 = root / 'bots/CardputerADV/.pio/libdeps/cardputer_adv/M5GFX/src/lgfx/Fonts/Font16.h'
if font16.exists():
    table = font16.read_text().split('widtbl_f16[96]', 1)[1].split('}', 1)[0]
    widths = []
    for line in table.split('\n'):
        line = line.split('//', 1)[0]
        if line.startswith('#'): continue
        widths += [int(x) for x in _re.findall(r'\d+', line)]
    widths = widths[:64] + widths[-32:]  # The grave accent's line has two versions; either is 4 or 5 wide.
    assert len(widths) == 96, len(widths)
    width = lambda text: sum(widths[ord(c) - 32] for c in text)
    beside = ['kScan', 'kRefused', 'kOr']
    for name, body in _re.findall(r'static const char\* const (k\w+)\[\] = \{(.*?)\};', sign_in):
        texts = _re.findall(r'"([^"]*)"', body)
        room = 238 - 142 if name in beside else 224
        for text in texts: assert width(text) <= room, (name, text, width(text))
        assert len(texts) <= 3, name
assert 'int16_t y = 30;' in sign_in and 'y += 16)' in sign_in and 'y += 4;' in sign_in  # 30 + 6 x 16 + 4 = 130
assert 'd.qrcode(gCamSetupUrl.c_str(), 0, 0, 135, 1, true);' in sign_in, 'the code fills the height, with its quiet zone'
# The zoom steps are the bridge's: the device steps through them and the bridge plans each one.
import json as _json, shutil as _shutil
if _shutil.which('node'):
    bridge_ladders = _json.loads(subprocess.run(['node', '--input-type=module', '-e',
        f'import {{zoomLadder}} from {_json.dumps((root / "bridge/src/cameras-media.mjs").as_uri())};'
        'console.log(JSON.stringify([zoomLadder(true), zoomLadder(false)]))'], check=True, capture_output=True, text=True).stdout)
    catalog = (src / 'camera_catalog.h').read_text()
    for name, ladder in zip(('kLens', 'kDigital'), bridge_ladders):
        assert f'static const uint8_t {name}[] = {{{", ".join(map(str, ladder))}}};' in catalog, (name, ladder)
# Live sound has a speaker channel of its own: no other player's.
import re as _re2
channel = int(_re2.search(r'kCamAudioChannel = (\d+);', app).group(1))
others = {}
for path in list(src.glob('*.cpp')) + list(src.glob('*.h')) + list(parts.glob('*.inc')):
    for name, value in _re2.findall(r'(k\w*Channel)\s*=\s*(\d+)\s*;', path.read_text(errors='ignore')):
        if name != 'kCamAudioChannel': others.setdefault(int(value), set()).add(f'{path.name}:{name}')
assert channel not in others, others.get(channel)
assert 'camAudioFree();\n  camera_net::close();' in app, 'the speaker stops before the app lets go of its buffers'
# Live view only ever goes over the home door, never TLS; pictures may come either way.
assert 'Live works on home Wi-Fi only' in app
# Recordings: C opens them from the wall and from a picture; a clip plays in the live view, its
# requests going to the clip's camera; Esc steps back one level; a clip that ends stops there.
assert "if (lower == 'c') { camOpenClips(\"\", CamView::Wall); return true; }" in app
assert 'camOpenClips(c->id, CamView::Picture)' in app
for job in ('Frame', 'Audio', 'Zoom', 'Pan', 'ClipPause', 'ClipSeek'):
    assert f'camStartLive(CamJob::{job},' in app and f'camStartFor(CamJob::{job},' not in app, job
escape = app.split('bool camerasEscape() {', 1)[1].split('\n}\n', 1)[0]
assert escape.index('CamView::Live') < escape.index('CamView::Month') < escape.index('CamView::Clips')
end_live = app.split('void camEndLive(const char* why) {', 1)[1].split('\n}\n', 1)[0]
assert 'gCamView = CamView::Clips;' in end_live, 'Esc from a clip goes back to its list'
assert app.count('camPlayClip(') == 3, 'a clip plays only by a key: none follows the last by itself'
keys = app.split('bool handleCamerasKey(', 1)[1].split('\n}\n', 1)[0]
assert keys.count('camPlayClip(') == 2
assert '(gCamView == CamView::Picture || gCamView == CamView::Live) && gCamJpeg' in app, 'the lists are drawn on the canvas'
assert 'Clips play on home Wi-Fi only' in app
# Every clip route the device asks for is one the bridge answers.
bridge_routes = (root / 'bridge/src/cameras.mjs').read_text().split('const WHAT = new Set([', 1)[1].split(']);', 1)[0]
for route in sorted(set(_re2.findall(r'"(clips[a-z/]*)', app.replace('%sclips', '"clips')))):
    assert f'"{route}"' in bridge_routes, route
# The list makes room for its clips only when they come, exactly as many: away from home the memory
# is in pieces, and 150 clips' worth at once said "Not enough memory for clips" (0.2.321).
assert 'new (std::nothrow) cameras::ClipDay' not in app and 'new (std::nothrow) cameras::Clip[lines]' in app
assert 'if (e == camera_net::Error::NoMemory && job != CamJob::Frame && job != CamJob::Report) {' in app
kinds = app.split('enum class CamJob : uint8_t {', 1)[1].split('};', 1)[0].replace('\n', ' ').split(',')
words = _re2.findall(r'"(\w+)"', app.split('static const char* const kWords[] = {', 1)[1].split('};', 1)[0])
assert len(kinds) == len(words), 'a word for each kind of request, in order'
# Away from home every request is HTTPS. The memory guard lets those pass, as it does the inbox's:
# counted, a few in a row restarted the Cardputer as Cameras opened (0.2.319). And away, opening the
# app doesn't wake every camera for a picture over HTTPS.
guard = part('010_main.cpp.inc').split('void serviceMemoryGuard() {', 1)[1].split('\n}\n', 1)[0]
assert guard.index('if (cameraHttpsBusy()) { criticalSinceMs = 0; return; }') < guard.index('ESP.restart();')
assert guard.index('noteRestart("memory", freeHeap, largest);') < guard.index('ESP.restart();')
assert 'bool cameraHttpsBusy() { return gUiMode == UiMode::Cameras && camera_net::busy() && !camera_net::overHome(); }' in app
net = (src / 'camera_net.cpp').read_text()
assert 'vTaskPrioritySet(nullptr, wantHome ? kHomePriority : kHttpsPriority);' in net and 'kHttpsPriority = tskIDLE_PRIORITY' in net, \
    'HTTPS on the worker runs at idle priority, so a handshake cannot starve the idle task'
auto = app.split('if (camNextThumb()) return;', 1)[1]
assert auto.index('if (!camHomeIp()) return;') < auto.index('if (!gCamAutoPlanned) {')
# The start-up reason goes with the first update check, once.
check = part('020_main.cpp.inc').split('bool startBackgroundManifestCheck(', 1)[1].split('\n}\n', 1)[0]
assert 'payload->url += "?" + gBootReport;' in check and 'gBootReport = "";' in check
assert 'gBootReport = String("boot=") + static_cast<int>(esp_reset_reason());' in part('061_main.cpp.inc')
# The serial number, name or address of a camera is never logged; only counts and words.
for line in app.splitlines():
    if 'appendRuntimeLog(' in line or 'logf(' in line: assert 'name' not in line and '.id' not in line, line

code = r'''
#include "camera_catalog.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
using namespace cameras;
static void lines(const char* t, List& l) { assert(parseList(t, strlen(t), l)); }
int main() {
  // The bridge's list.
  List l;
  lines("state\tready\nbase\tAway\ncam\t0123abcd\tp\t84\t720\t60\tFront door\ncam\t89abcdef\to\t-\t-\t-\tCamera\n", l);
  assert(l.state == State::Ready && !strcmp(l.mode, "Away") && l.count == 2);
  assert(!strcmp(l.cams[0].id, "0123abcd") && l.cams[0].ptz && !l.cams[0].off && l.cams[0].battery == 84);
  assert(l.cams[0].motionAge == 720 && l.cams[0].pictureAge == 60 && !strcmp(l.cams[0].name, "Front door"));
  assert(l.cams[1].off && !l.cams[1].ptz && l.cams[1].battery == -1 && l.cams[1].motionAge == -1 && l.cams[1].pictureAge == -1);
  lines("state\tcode\teufy sent a code by email\n", l);
  assert(l.state == State::Code && !strcmp(l.detail, "eufy sent a code by email") && l.count == 0);
  lines("state\toff\tCameras aren't set up on the Mac\n", l);
  assert(l.state == State::Off);
  // Bad lines are skipped, not trusted: a bad id, a battery over 100, too few fields, an age that isn't one.
  lines("state\tready\ncam\tXYZ\tp\t1\t1\t1\tA\ncam\t0123abcd\t-\t140\t5\t5\tB\ncam\t0123abcd\t-\n"
        "cam\t0123abcd\t-\t5\tsoon\t5\tC\nunknown\tline\n", l);
  assert(l.count == 1 && l.cams[0].battery == -1 && !strcmp(l.cams[0].name, "B"));
  // More cameras than the wall holds: the rest are left off.
  char many[4096] = "state\tready\n";
  for (int i = 0; i < 20; ++i) { char row[64]; snprintf(row, sizeof(row), "cam\t%08x\t-\t50\t-\t-\tCam %d\n", i, i); strcat(many, row); }
  lines(many, l);
  assert(l.count == kMax);
  // Not a list at all.
  List bad;
  assert(!parseList("<html>", 6, bad));
  assert(!parseList("", 0, bad));

  // Where to ask, and the same over the home door. (Qualified: podcast:: has the same names for its door.)
  char p[200];
  assert(routePath(p, sizeof(p), "picture", "0123abcd") && !strcmp(p, "/api/cardputer/cameras/picture?cam=0123abcd"));
  assert(routePath(p, sizeof(p), "turn", "0123abcd", "dir=left") && !strcmp(p, "/api/cardputer/cameras/turn?cam=0123abcd&dir=left"));
  assert(!routePath(p, sizeof(p), "picture", "../etc"));
  assert(!routePath(p, 20, "picture", "0123abcd"));
  assert(codePath(p, sizeof(p), "123456") && !strcmp(p, "/api/cardputer/cameras/code?value=123456"));
  assert(!codePath(p, sizeof(p), "12a456") && !codePath(p, sizeof(p), "12") && !codePath(p, sizeof(p), "123456789"));
  assert(captchaPath(p, sizeof(p), "aB3x") && !strcmp(p, "/api/cardputer/cameras/code?captcha=aB3x"));
  assert(!captchaPath(p, sizeof(p), "a b") && !captchaPath(p, sizeof(p), "ab") && !captchaPath(p, sizeof(p), "a&b=c"));
  podcast::HomeTicket t;
  const char* ticket = "ticket\t31890\t/cameras/0123456789abcdef0123456789abcdef\nlan\t192.168.0.20\t22\n";
  assert(cameras::parseTicket(ticket, strlen(ticket), t) && !t.none && t.port == 31890 && t.count == 1);
  assert(podcast::homeFor(t, (192u << 24) | (168u << 16) | (1u << 8) | 31u) == ((192u << 24) | (168u << 16) | 20u));
  assert(podcast::homeFor(t, (10u << 24) | 5u) == 0);  // Another network.
  assert(cameras::homePath(p, sizeof(p), t, "/api/cardputer/cameras/live/frame?cam=0123abcd") &&
         !strcmp(p, "/cameras/0123456789abcdef0123456789abcdef/live/frame?cam=0123abcd"));
  assert(!cameras::homePath(p, sizeof(p), t, "/api/cardputer/podcasts/search"));
  assert(cameras::parseTicket("none\n", 5, t) && t.none);
  assert(!cameras::parseTicket("ticket\t31890\t/podcasts/0123456789abcdef0123456789abcdef\n", 57, t));
  assert(!cameras::parseTicket("not_found\n", 10, t));

  // The sign-in page's link, and the QR code's address: short enough for a version 4 code (78
  // bytes at the lowest error correction), which fills the screen's height with 3-pixel squares.
  char sp[32];
  uint32_t secs = 0;
  const char* setup = "setup\t/eufy/Ab3_-x9QrZt0LmN2\t900\n";
  assert(parseSetup(setup, strlen(setup), sp, sizeof(sp), secs) && !strcmp(sp, "/eufy/Ab3_-x9QrZt0LmN2") && secs == 900);
  assert(!parseSetup("none\tCameras aren't set up on the Mac\n", 37, sp, sizeof(sp), secs));
  assert(!parseSetup("setup\t/eufy/short\t900\n", 23, sp, sizeof(sp), secs));
  assert(!parseSetup("setup\t/eufy/Ab3_-x9QrZt0Lm<2\t900\n", 34, sp, sizeof(sp), secs));
  assert(!parseSetup("setup\t/other/Ab3_-x9QrZt0LmN\t900\n", 34, sp, sizeof(sp), secs));
  char url[200];
  assert(setupUrl(url, sizeof(url), "bridge-1.tailabcdef.ts.net", 10000, true, sp) &&
         !strcmp(url, "https://bridge-1.tailabcdef.ts.net:10000/eufy/Ab3_-x9QrZt0LmN2") && strlen(url) <= 78);
  assert(setupUrl(url, sizeof(url), "bridge.example", 443, true, sp) && !strcmp(url, "https://bridge.example/eufy/Ab3_-x9QrZt0LmN2"));
  assert(setupUrl(url, sizeof(url), "192.168.0.20", 31889, false, sp) && !strcmp(url, "http://192.168.0.20:31889/eufy/Ab3_-x9QrZt0LmN2"));
  assert(!setupUrl(url, sizeof(url), "", 443, true, sp));

  // Zoom: the device's steps, its words for them, and the bridge's answer.
  List zl;
  lines("state\tready\ncam\t0123abcd\tpz\t90\t-\t-\t360\ncam\t89abcdef\t-\t90\t-\t-\tSide\n", zl);
  assert(zl.cams[0].lens && zl.cams[0].ptz && !zl.cams[1].lens);
  uint8_t ladder[kZoomSteps];
  assert(zoomLadder(true, ladder) == 8 && ladder[3] == 30 && ladder[7] == 120);
  assert(zoomLadder(false, ladder) == 5 && ladder[4] == 40);
  assert(zoomStep(true, 10, 1) == 15 && zoomStep(true, 20, 1) == 30 && zoomStep(true, 120, 1) == 120);
  assert(zoomStep(true, 45, -1) == 30 && zoomStep(false, 10, -1) == 10 && zoomStep(false, 40, 1) == 40);
  assert(zoomStep(false, 45, -1) == 40 && "from a lens zoom to a camera without one");
  char zt[8];
  zoomText(zt, sizeof(zt), 15); assert(!strcmp(zt, "1.5x"));
  zoomText(zt, sizeof(zt), 30); assert(!strcmp(zt, "3x"));
  zoomText(zt, sizeof(zt), 120); assert(!strcmp(zt, "12x"));
  View v;
  const char* va = "view\t45\t100\t200\t600\t700\tmoved\n";
  assert(parseView(va, strlen(va), v) && v.tenths == 45 && v.x0 == 100 && v.y1 == 700 && v.did == PanResult::Moved);
  const char* vb = "view\t10\t0\t0\t1000\t1000\tturned\n";
  assert(parseView(vb, strlen(vb), v) && v.did == PanResult::Turned && v.x1 == 1000);
  View kept = v;
  const char* vc = "view\t10\t600\t0\t500\t1000\t-\n";
  assert(!parseView(vc, strlen(vc), v) && v.x0 == kept.x0 && "a bad answer leaves the view as it was");
  assert(!parseView("zoom\t30\n", 8, v));

  // The worker's HTTP answers.
  Response r;
  assert(r.line("HTTP/1.1 200 OK") && r.status == 200 && !r.close);
  assert(r.line("Content-Type: image/jpeg") && r.line("content-length: 8123") && r.hasLength && r.length == 8123);
  assert(r.line("Connection: keep-alive") && !r.close && r.line("") && r.done);
  Response r2;
  assert(r2.line("HTTP/1.1 204 No Content") && r2.status == 204 && r2.line("Connection: Close") && r2.close);
  Response r3;
  assert(r3.line("HTTP/1.0 404 Not Found") && r3.close && r3.status == 404);
  Response r4;
  assert(!r4.line("ICY 200 OK"));
  Response r5;
  assert(r5.line("HTTP/1.1 200 OK") && r5.line("Transfer-Encoding: chunked") && r5.chunked);
  Response r6;
  assert(r6.line("HTTP/1.1 200 OK") && !r6.line("Content-Length: lots") && !r6.line("no colon"));

  // The wall: three tiles a row, two rows, inside the 240x135 screen under the header.
  for (uint8_t t2 = 0; t2 < kPerPage; ++t2) {
    assert(tileX(t2) >= 0 && tileX(t2) + kThumbW <= 240);
    assert(tileY(t2) >= 23 && tileY(t2) + kThumbH + kLabelH <= 136);
  }
  assert(tileX(1) - (tileX(0) + kThumbW) == kTileGapX && tileY(3) == tileY(0) + kThumbH + kLabelH);
  assert(tileX(6) == tileX(0) && tileY(6) == tileY(0) && pageOf(6) == 1);
  assert(tileCount(5) == 6 && pages(5) == 1 && pages(6) == 2 && pages(0) == 1);
  // Moving: rows don't wrap off the tiles, left and right walk them.
  assert(move(0, 5, 1, 0) == 1 && move(2, 5, 1, 0) == 3 && move(5, 5, 1, 0) == 5 && move(0, 5, -1, 0) == 0);
  assert(move(0, 5, 0, 1) == 3 && move(4, 5, 0, 1) == 4 && move(3, 5, 0, -1) == 0 && move(1, 5, 0, -1) == 1);
  assert(move(2, 2, 0, 1) == 2);  // Three tiles: nothing below.

  // Times.
  char s[48];
  ageText(s, sizeof(s), -1); assert(!strcmp(s, ""));
  ageText(s, sizeof(s), 30); assert(!strcmp(s, "now"));
  ageText(s, sizeof(s), 720); assert(!strcmp(s, "12m"));
  ageText(s, sizeof(s), 3 * 3600 + 5); assert(!strcmp(s, "3h"));
  ageText(s, sizeof(s), 2 * 86400 + 1); assert(!strcmp(s, "2d"));
  pictureWhen(s, sizeof(s), 30 * 60, 14 * 60 + 32, 3); assert(!strcmp(s, "picture from 14:02"));
  pictureWhen(s, sizeof(s), 20 * 3600, 9 * 60, 3); assert(!strcmp(s, "picture from Tue"));
  pictureWhen(s, sizeof(s), 20 * 86400, 9 * 60, 3); assert(!strcmp(s, "picture from 20d ago"));
  pictureWhen(s, sizeof(s), 7200, -1, -1); assert(!strcmp(s, "picture from 2h ago"));
  pictureWhen(s, sizeof(s), -1, 600, 1); assert(!strcmp(s, "no picture yet"));
  pictureWhen(s, sizeof(s), 10, 600, 1); assert(!strcmp(s, "picture from now"));
  countdown(s, sizeof(s), 38); assert(!strcmp(s, "0:38"));
  countdown(s, sizeof(s), 45); assert(!strcmp(s, "0:45"));

  // Keys: the keyboard's arrows and a USB keyboard's.
  assert(arrowFor(';') == Arrow::Up && arrowFor('.') == Arrow::Down && arrowFor(',') == Arrow::Left && arrowFor('/') == Arrow::Right);
  assert(arrowFor(1) == Arrow::Up && arrowFor(4) == Arrow::Right && arrowFor('l') == Arrow::None);
  assert(!strcmp(directionWord(Arrow::Left), "left") && !strcmp(directionWord(Arrow::None), ""));

  // A saved frame's name on the card.
  assert(savePath(s, sizeof(s), "Front door!", 2026, 11, 6, 9, 5, 3) && !strcmp(s, "/cameras/Front-door-20261106-090503.jpg"));
  assert(savePath(s, sizeof(s), "???", 2026, 1, 2, 3, 4, 5) && !strcmp(s, "/cameras/camera-20260102-030405.jpg"));

  // Recordings: days, as the bridge names them.
  assert(validDay(20261106) && validDay(20280229) && !validDay(20260229) && !validDay(20261131) && !validDay(19991231) && !validDay(20261300));
  assert(addDays(20261231, 1) == 20270101 && addDays(20270101, -1) == 20261231 && addDays(20280228, 1) == 20280229);
  assert(addDays(20261106, -7) == 20261030 && addDays(20261106, 0) == 20261106 && addDays(20260301, -1) == 20260228);
  assert(weekday(2026, 11, 6) == 5 && weekday(2026, 11, 1) == 0 && weekday(2000, 1, 1) == 6);
  dayLabel(s, sizeof(s), 20261106); assert(!strcmp(s, "Fri 6 Nov"));
  dayLabel(s, sizeof(s), 20270101); assert(!strcmp(s, "Fri 1 Jan"));
  clockOfDay(s, sizeof(s), 16 * 3600 + 47 * 60 + 5); assert(!strcmp(s, "16:47"));
  clockOfDay(s, sizeof(s), 9 * 60); assert(!strcmp(s, "0:09"));
  lengthText(s, sizeof(s), 41); assert(!strcmp(s, "0:41"));
  lengthText(s, sizeof(s), 120); assert(!strcmp(s, "2:00"));
  lengthText(s, sizeof(s), 3723); assert(!strcmp(s, "1:02:03"));
  // The month's grid, Monday first: November 2026 starts on a Sunday and takes six rows.
  assert(monthLead(2026, 11) == 6 && monthRows(2026, 11) == 6);
  assert(monthLead(2027, 2) == 0 && monthRows(2027, 2) == 4);
  assert(monthLead(2026, 6) == 0 && monthRows(2026, 6) == 5);
  {
    // A day's list, newest first; a bad line is skipped, not the list. The clips are the caller's.
    static Clip storage[kMaxClips];
    ClipDay day;
    day.clips = storage;
    day.cap = kMaxClips;
    const char* t = "clips\t20261106\t4\n"
                    "clip\t2026110600012\t60450\t41\tab12cd34\n"
                    "clip\t1234567890123456\t3600\t120\t0f0f0f0f\n"
                    "clip\t12345678901234567\t3600\t120\tab12cd34\n"
                    "clip\tx1\t1\t1\tab12cd34\n"
                    "clip\t7\t90000\t1\tab12cd34\n"
                    "clip\t8\t10\t1\tAB12CD34\n";
    assert(parseClips(t, strlen(t), day));
    assert(day.day == 20261106 && day.total == 4 && day.count == 2);
    assert(!strcmp(day.clips[0].id, "2026110600012") && day.clips[0].start == 60450 && day.clips[0].seconds == 41 && !strcmp(day.clips[0].cam, "ab12cd34"));
    assert(!strcmp(day.clips[1].id, "1234567890123456") && !strcmp(day.clips[1].cam, "0f0f0f0f"));
    const char* none = "clips\t20261105\t0\n";
    assert(parseClips(none, strlen(none), day) && day.day == 20261105 && day.count == 0);
    const char* bad = "clips\t20261131\t1\n";
    assert(!parseClips(bad, strlen(bad), day));
    // More than fit: the first kMaxClips are kept.
    const size_t room = 200 * 48 + 32;
    char* many = static_cast<char*>(malloc(room));
    size_t n = size_t(snprintf(many, room, "clips\t20261106\t200\n"));
    for (int i = 0; i < 200; ++i) n += size_t(snprintf(many + n, room - n, "clip\t%d\t%d\t30\tab12cd34\n", 1000 + i, 80000 - i * 300));
    assert(parseClips(many, n, day) && day.total == 200 && day.count == kMaxClips && !strcmp(day.clips[kMaxClips - 1].id, "1149"));
    assert(clipLines(many, n) == kMaxClips);
    free(many);
    // Room for exactly as many as the answer has: counted first, never more than that kept.
    assert(clipLines(t, strlen(t)) == 6 && clipLines(none, strlen(none)) == 0 && clipLines("clip", 4) == 0);
    Clip two[2];
    ClipDay small;
    small.clips = two;
    small.cap = 2;
    assert(parseClips(t, strlen(t), small) && small.count == 2);
    small.cap = 1;
    assert(parseClips(t, strlen(t), small) && small.count == 1 && !strcmp(two[0].id, "2026110600012"));
    ClipDay roomless;
    assert(parseClips(t, strlen(t), roomless) && roomless.count == 0 && roomless.day == 20261106);
  }
  {
    Month m;
    const char* t = "month\t202611\nday\t3\t1\nday\t6\t4\nday\t9\t0\nday\t0\t1\nday\t32\t1\n";
    assert(parseMonth(t, strlen(t), m) && m.ym == 202611 && m.has[3] && m.has[6] && !m.has[9] && !m.has[0] && !m.has[1]);
    const char* bad = "month\t202613\nday\t3\t1\n";
    assert(!parseMonth(bad, strlen(bad), m) && m.ym == 0);
  }
  {
    // Where a clip is: its frames' header, and the answer to a pause or seek.
    ClipState st;
    assert(parseClipState("12000 41000 1 0", st) && st.known && st.pos == 12000 && st.dur == 41000 && st.playing && !st.ended);
    assert(parseClipState("clip\t41000\t41000\t0\t1\n", st) && st.pos == 41000 && !st.playing && st.ended);
    assert(parseClipState("50000 41000 1 0", st) && st.pos == 41000);
    ClipState untouched;
    assert(!parseClipState("", untouched) && !untouched.known);
    assert(!parseClipState("1 2 3 0", untouched) && !parseClipState("1 2 1", untouched) && !parseClipState("1 2 1 0 9", untouched));
    char cam[kIdBytes];
    uint32_t dur = 0;
    const char* started = "clip\tab12cd34\t41000\n";
    assert(parseClipStart(started, strlen(started), cam, dur) && !strcmp(cam, "ab12cd34") && dur == 41000);
    const char* odd = "clip\tnot-an-id\t41000\n";
    assert(!parseClipStart(odd, strlen(odd), cam, dur));
  }
#ifdef BRIDGE_LINES
  {
    // The bridge's own lines (cameras-archive.mjs), read the device's way.
    static Clip storage[kMaxClips];
    ClipDay day;
    day.clips = storage;
    day.cap = clipLines(kBridgeClips, strlen(kBridgeClips));
    Month m;
    ClipState st;
    assert(parseClips(kBridgeClips, strlen(kBridgeClips), day) && day.day == 20261106 && day.count == 2);
    assert(!strcmp(day.clips[0].id, "2026110600013") && day.clips[0].start == 70000);  // Newest first.
    assert(parseMonth(kBridgeMonth, strlen(kBridgeMonth), m) && m.ym == 202611 && m.has[3] && !m.has[4]);
    assert(parseClipState(kBridgeState, st) && st.pos == 1200 && st.dur == 41000 && st.playing && !st.ended);
  }
#endif
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    test = Path(tmp) / 'cameras.cpp'
    exe = Path(tmp) / 'cameras'
    test.write_text(code)
    extra = []
    if _shutil.which('node'):
        # The bridge's recordings lines, as it writes them, for the device's parsers.
        made = _json.loads(subprocess.run(['node', '--input-type=module', '-e',
            f'import {{clipsLines, monthLines, clipStateLine}} from {_json.dumps((root / "bridge/src/cameras-archive.mjs").as_uri())};'
            'console.log(JSON.stringify([clipsLines("20261106", [{id: 2026110600012, start: 60450, seconds: 41, camId: "ab12cd34"},'
            '{id: 2026110600013, start: 70000, seconds: 12, camId: "0f0f0f0f"}]), monthLines("202611", new Map([["20261103", 1], ["20261004", 2]])),'
            'clipStateLine({pos: 1200, dur: 41000, playing: true, ended: false})]))'], check=True, capture_output=True, text=True).stdout)
        (Path(tmp) / 'bridge_lines.h').write_text(''.join(f'static const char {n}[] = {_json.dumps(v)};\n'
                                                          for n, v in zip(('kBridgeClips', 'kBridgeMonth', 'kBridgeState'), made)))
        extra = ['-DBRIDGE_LINES', '-include', str(Path(tmp) / 'bridge_lines.h')]
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(src)] + extra + [str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
# The bridge's pictures and live frames, made by its own ffmpeg arguments, through the device's own
# JPEG decoder (M5GFX's TJpgDec, with the 3,900-byte pool drawJpg gives it). 0.2.315 and 0.2.316 sent
# 4:2:2 that ffmpeg marks in a way this decoder refuses, and every picture said "Couldn't show".
import json, shutil
tjpgd = root / 'bots/CardputerADV/.pio/libdeps/cardputer_adv/M5GFX/src/lgfx/utility'
ffmpeg = shutil.which('ffmpeg') or next((p for p in ('/opt/homebrew/bin/ffmpeg', '/usr/local/bin/ffmpeg') if Path(p).exists()), None)
node = shutil.which('node')
if (tjpgd / 'lgfx_tjpgd.c').exists() and ffmpeg and node:
    media = root / 'bridge/src/cameras-media.mjs'
    args = json.loads(subprocess.run([node, '--input-type=module', '-e',
        f'import {{pictureArgs, liveArgs, PICTURE}} from {json.dumps(media.as_uri())};'
        'console.log(JSON.stringify({picture: pictureArgs(), live: liveArgs("h265"), size: PICTURE}))'],
        check=True, capture_output=True, text=True).stdout)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        still = subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=2880x1616',
                                '-frames:v', '1', '-c:v', 'mjpeg', '-f', 'image2pipe', 'pipe:1'], check=True, capture_output=True).stdout
        hevc = subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=15',
                               '-frames:v', '4', '-c:v', 'libx265', '-x265-params', 'log-level=none', '-f', 'hevc', 'pipe:1'],
                              check=True, capture_output=True).stdout
        (tmp / 'picture.jpg').write_bytes(subprocess.run([ffmpeg] + args['picture'], input=still, check=True, capture_output=True).stdout)
        # Live: the helper's way, a raw frame cropped at the widest and the closest zoom and encoded by
        # jpeg-js, where the eufy library's folder has it (it isn't in this repository).
        (tmp / 'live.raw').write_bytes(subprocess.run([ffmpeg] + args['live'], input=hevc, check=True, capture_output=True).stdout)
        live = []
        jpegjs_home = Path.home() / '.s1-adv/data/cameras/package.json'
        if jpegjs_home.exists():
            subprocess.run([node, '--input-type=module', '-e',
                f'import {{RAW, renderRect, viewRect, PICTURE}} from {json.dumps(media.as_uri())};'
                'import {createRequire} from "node:module"; import {readFileSync, writeFileSync} from "node:fs";'
                f'const jpeg = createRequire({json.dumps(str(jpegjs_home))})("jpeg-js");'
                f'const raw = readFileSync({json.dumps(str(tmp / "live.raw"))}).subarray(0, RAW.bytes);'
                'for (const z of [1, 4]) writeFileSync(' + json.dumps(str(tmp)) + ' + `/live${z}.jpg`, '
                'jpeg.encode({data: renderRect(raw, viewRect(z)), width: PICTURE.width, height: PICTURE.height}, 70).data);'],
                check=True)
            live = [tmp / 'live1.jpg', tmp / 'live4.jpg']
        harness = tmp / 'jd.c'
        harness.write_text(r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lgfx_tjpgd.h"
typedef struct { const unsigned char* d; size_t n, at; } Src;
static uint32_t in(void* dev, uint8_t* buf, uint32_t len) { Src* s = dev; if (s->at + len > s->n) len = (uint32_t)(s->n - s->at); if (buf) memcpy(buf, s->d + s->at, len); s->at += len; return len; }
static uint32_t out(void* dev, void* bitmap, JRECT* r) { (void)dev; (void)bitmap; (void)r; return 1; }
int main(int argc, char** argv) {
  for (int i = 1; i < argc; i += 3) {
    FILE* f = fopen(argv[i], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = malloc((size_t)n); if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    Src s = { d, (size_t)n, 0 };
    static uint8_t pool[3900];
    lgfxJdec jd;
    if (lgfx_jd_prepare(&jd, in, pool, sizeof(pool), &s) != JDR_OK) { printf("%s: prepare failed\n", argv[i]); return 1; }
    if (jd.width != atoi(argv[i + 1]) || jd.height != atoi(argv[i + 2])) { printf("%s: %ux%u\n", argv[i], jd.width, jd.height); return 1; }
    if (lgfx_jd_decomp(&jd, out, 0) != JDR_OK) { printf("%s: decomp failed\n", argv[i]); return 1; }
    free(d);
  }
  return 0;
}
""")
        exe = tmp / 'jd'
        subprocess.run([os.environ.get('CC', 'cc'), '-O1', '-w', '-I', str(tjpgd), str(harness), str(tjpgd / 'lgfx_tjpgd.c'), '-o', str(exe)], check=True)
        w, h = str(args['size']['width']), str(args['size']['height'])
        assert h in ('135', '136'), 'drawn from the top, so one row past the 135 is off the screen'
        files = [str(tmp / 'picture.jpg')] + [str(f) for f in live]
        subprocess.run([str(exe)] + [x for f in files for x in (f, w, h)], check=True)
    print('cameras: the device decodes the bridge pictures and live frames')
print('cameras: ok')
