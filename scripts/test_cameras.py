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
# Live view only ever goes over the home door, never TLS; pictures may come either way.
assert 'Live works on home Wi-Fi only' in app
# The serial number, name or address of a camera is never logged; only counts and words.
for line in app.splitlines():
    if 'appendRuntimeLog(' in line or 'logf(' in line: assert 'name' not in line and '.id' not in line, line

code = r'''
#include "camera_catalog.h"
#include <cassert>
#include <cstring>
#include <cstdio>
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
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    test = Path(tmp) / 'cameras.cpp'
    exe = Path(tmp) / 'cameras'
    test.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(src), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
print('cameras: ok')
