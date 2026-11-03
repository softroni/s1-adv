#!/usr/bin/env python3
"""Draw the real Wi-Fi screens with the Lab's real drawing pieces, checking fit, fonts and what shows."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'


def part(number):
    return (parts / f'{number:03d}_main.cpp.inc').read_text()


def function(source, signature):
    start = source.index(signature)
    while source.find(';', start) < source.find('{', start):  # Skip forward declarations.
        start = source.index(signature, start + 1)
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


lab = part(82)
lab_style = lab[lab.index('constexpr uint16_t kLabLime'):lab.index('constexpr int kLabTrailSteps')]
lab_style += re.search(r'constexpr const char kLabArrowUpDown.*\n', lab).group(0)
lab_style += ''.join(function(lab, f'void {name}(') for name in
                     ['labChip', 'labHead', 'labArrow', 'labKeys', 'labTextAt', 'labTextRight', 'labRowBar', 'labScrollBar', 'labBar'])
shared = function(part(7), 'void removeLastUtf8Char(') + function(part(7), 'void removeFirstUtf8Char(')
shared += function(part(8), 'String tailUtf8ToFit(')
big = part(53)
shared += function(big, 'String fitCurrentFontToWidth(') + function(big, 'int16_t useBigTextFont(') + function(big, 'int bigListFirstRow(')
screens = part(86)
assert 'renderWifiSettingsUi' in screens

code = r'''
#include "lab.h"
#include "launcher_icons.h"
#include "wifi_settings.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using std::min; using std::max;

struct String : std::string {
  String() = default;
  String(const char* s) : std::string(s) {}
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  size_t length() const { return size(); }
  void remove(size_t from) { erase(from); }
  void remove(size_t from, size_t count) { erase(from, count); }
};
String operator+(const String& a, const String& b) { return String(std::string(a) + std::string(b)); }
String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
String operator+(const char* a, const String& b) { return String(a + std::string(b)); }

uint32_t nowMs = 100000;
uint32_t millis() { return nowMs; }
constexpr uint16_t rgb565_local(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
constexpr uint16_t TFT_BLACK = 0, TFT_WHITE = 0xffff;
constexpr int kBigVisibleRows = 5;

namespace lgfx { struct IFont { int id; int charWidth; int height; }; }
namespace fonts {
const lgfx::IFont Font0{0, 6, 8}, Font2{2, 7, 16}, FreeSansBold9pt7b{9, 9, 17}, efontCN_12{12, 6, 12};
}
struct Text { std::string s; int font, x, y, w, h; uint16_t color; };
struct Canvas {
  const lgfx::IFont* font = &fonts::Font0;
  int x = 0, y = 0, dots = 0, bars = 0; uint16_t color = 0;
  std::vector<Text> text;
  static void on(int a, int b) { if (a < 0 || b < 0 || a >= 240 || b >= 135) { std::fprintf(stderr, "off screen at %d,%d\n", a, b); std::abort(); } }
  static void box(int a, int b, int w, int h) { assert(w > 0 && h > 0); on(a, b); on(a + w - 1, b + h - 1); }
  void fillScreen(uint16_t) { text.clear(); dots = bars = 0; }
  void setTextSize(int) {}
  void setTextWrap(bool) {}
  void setFont(const lgfx::IFont* f) { font = f; }
  void setTextColor(uint16_t c) { color = c; }
  void setCursor(int a, int b) { x = a; y = b; }
  int textWidth(const std::string& s) const { return int(s.size()) * font->charWidth; }
  void print(const std::string& s) {
    if (s.empty()) return;
    const int w = textWidth(s);
    box(x, y, w, font->height);
    // Nothing printed lands on other text.
    for (const auto& t : text) {
      const bool apart = x >= t.x + t.w || t.x >= x + w || y >= t.y + t.h || t.y >= y + font->height;
      if (!apart) { std::fprintf(stderr, "[%s] overlaps [%s]\n", s.c_str(), t.s.c_str()); std::abort(); }
    }
    text.push_back({s, font->id, x, y, w, font->height, color});
    x += w;
  }
  void drawBitmap(int a, int b, const uint8_t* bits, int w, int h, uint16_t) { assert(bits); box(a, b, w, h); }
  void drawRoundRect(int a, int b, int w, int h, int, uint16_t) { box(a, b, w, h); }
  void fillRoundRect(int a, int b, int w, int h, int, uint16_t) { box(a, b, w, h); }
  void fillRect(int a, int b, int w, int h, uint16_t) { box(a, b, w, h); ++bars; }
  void drawFastHLine(int a, int b, int w, uint16_t) { box(a, b, w, 1); }
  void drawFastVLine(int a, int b, int h, uint16_t) { box(a, b, 1, h); }
  void fillCircle(int a, int b, int r, uint16_t) { box(a - r, b - r, 2 * r + 1, 2 * r + 1); ++dots; }
  void drawCircle(int a, int b, int r, uint16_t) { box(a - r, b - r, 2 * r + 1, 2 * r + 1); }
} gCanvas;
void renderLauncherOverlay() {}
void renderDebugOverlay() {}

using wifi_settings::Mood; using wifi_settings::Screen;
wifi_settings::App gWifi;
String gWifiSsid = "Home";
struct { bool on = true; bool online() { return on; } bool isCurrent(const std::string& n) { return n == "Home"; } } gWifiRadio;
''' + shared + lab_style + screens + r'''
const Text* find(const std::string& s) { for (const auto& t : gCanvas.text) if (t.s == s) return &t; return nullptr; }
bool shows(const std::string& s) { return find(s) != nullptr; }
int fontOf(const std::string& s) { const Text* t = find(s); return t ? t->font : -1; }
uint16_t colorOf(const std::string& s) { const Text* t = find(s); return t ? t->color : 1; }
// Every screen is the Lab's: its title, its chip in the corner and its key line along the bottom,
// and the smallest font is kept for those two.
void looksLikeTheLab(const char* chip, uint16_t chipColor, const char* keys) {
  assert(fontOf("Wi-Fi") == 9 && find("Wi-Fi")->x == 23);
  const Text* c = find(chip);
  assert(c && c->font == 0 && c->y == 6 && c->color == chipColor && c->x + c->w <= 238);
  const Text* k = find(keys);
  assert(k && k->font == 0 && k->y == 127);
  for (const auto& t : gCanvas.text) assert(t.font != 0 || t.y == 6 || t.y == 127);
}
void show(Screen screen) { gWifi.screen = screen; renderWifiSettingsUi(); }

int main() {
  gWifi.networks = {{"Home", -40, true, true}, {"Cafe", -70, false}, {"Neighbours", -85, true},
                    {"Cabin", -100, true, true, false}};
  gWifi.selected = 1;
  show(Screen::List);
  looksLikeTheLab("CONNECTED", kLabGreen, "ENTER join R rescan ESC back");
  assert(fontOf("Home") == 9 && fontOf("Cafe") == 9 && fontOf("Other network") == 9);
  assert(colorOf("Cafe") == TFT_BLACK && colorOf("Other network") == kLabCyan);
  assert(fontOf("joined") == 2 && colorOf("joined") == kLabGreen && fontOf("open") == 2 && colorOf("open") == TFT_BLACK);
  // A saved network out of range is greyed, like a part the Lab can't find.
  assert(colorOf("Cabin") == kLabMuted && colorOf("saved") == kLabMuted);
  assert(gCanvas.dots == 1);
  std::cout << "PASS Wi-Fi list: the Lab's header, chip, rows, dots and key line\n";

  gWifi.selected = 0; show(Screen::List); assert(shows("ENTER join R rescan F forget ESC back"));
  gWifi.selected = 4; show(Screen::List); assert(shows("ENTER type a name R rescan ESC back") && colorOf("Other network") == TFT_BLACK);
  gWifi.say("Not found. Is it 2.4 GHz?", Mood::Bad); show(Screen::List);
  assert(colorOf("Not found. Is it 2.4 GHz?") == kLabRed && fontOf("Not found. Is it 2.4 GHz?") == 2);
  gWifi.say("");
  gWifiRadio.on = false; show(Screen::List); looksLikeTheLab("OFFLINE", kLabRed, "ENTER type a name R rescan ESC back");
  gWifiRadio.on = true;
  {
    wifi_settings::App kept = gWifi;
    gWifi = wifi_settings::App{};
    gWifi.rescan(0);
    show(Screen::List);
    looksLikeTheLab("SCANNING", kLabAmber, "ENTER type a name R rescan ESC back");
    assert(shows("Looking for networks") && shows("Only 2.4 GHz networks show up"));
    gWifi = kept;
  }
  // A long list keeps the chosen row in view, and every row fits.
  for (int i = 0; i < 12; ++i) gWifi.networks.push_back({"Network with a long name " + std::to_string(i), -60, true});
  for (size_t i = 0; i < gWifi.rows(); ++i) { gWifi.selected = i; show(Screen::List); }
  gWifi.networks.resize(4);
  std::cout << "PASS Wi-Fi list: its keys follow the row, messages, states and a long list fit\n";

  gWifi.selected = 1; gWifi.name = "Cafe Wi-Fi"; gWifi.password = "hunter22"; gWifi.secured = true;
  show(Screen::Password);
  looksLikeTheLab("PASSWORD", kLabSoft, "ENTER join FN+; show FN+ESC back");
  assert(fontOf("Cafe Wi-Fi") == 9 && shows("At least 8 characters"));
  // Hidden characters are dots, and the newest one shows.
  assert(fontOf("2_") == 9 && gCanvas.dots == 7);
  gWifi.revealed = true; show(Screen::Password); assert(shows("hunter22_") && gCanvas.dots == 0); gWifi.revealed = false;
  gWifi.password = std::string(63, 'x'); show(Screen::Password); assert(gCanvas.dots > 10 && gCanvas.dots < 62 && shows("x_"));
  gWifi.password = "hunter22";
  gWifi.say("Use at least 8 characters", Mood::Bad); show(Screen::Password);
  assert(shows("At least 8 characters") && colorOf("Use at least 8 characters") == kLabRed);
  gWifi.say("");
  // An iPhone's curly apostrophe shows as a plain one, so its name stays in the bold font.
  gWifi.name = "Alex\xE2\x80\x99s iPhone"; show(Screen::Password);
  assert(fontOf("Alex's iPhone") == 9 && !shows("Alex\xE2\x80\x99s iPhone"));
  assert(wifiShownName("\xE2\x80\x9C" "A\xE2\x80\x9D \xE2\x80\x93 B\xE2\x80\xA6") == "\"A\" - B...");
  // A name in another script keeps the font that can show it.
  gWifi.name = "\xD0\x9A\xD0\xB0\xD1\x84\xD0\xB5"; show(Screen::Password); assert(fontOf("\xD0\x9A\xD0\xB0\xD1\x84\xD0\xB5") == 12);
  gWifi.secured = false; gWifi.password = ""; show(Screen::Password); assert(shows("Leave blank for an open network") && shows("_"));
  std::cout << "PASS Wi-Fi password: dots, the network's name in bold, hints and messages\n";

  gWifi.name = "Cafe"; show(Screen::Name);
  looksLikeTheLab("OTHER NETWORK", kLabSoft, "ENTER next FN+ESC back");
  assert(shows("Network name") && fontOf("Cafe_") == 9 && shows("Capitals count. 2.4 GHz only."));
  gWifi.name = "Alex\xE2\x80\x99s iPhone"; show(Screen::Joining);
  looksLikeTheLab("JOINING", kLabAmber, "ESC cancel");
  assert(fontOf("Alex's iPhone") == 9 && shows("Saved only if it connects"));
  gWifi.name = "Home"; show(Screen::Forget);
  looksLikeTheLab("FORGET", kLabRed, "ENTER forget ESC keep");
  assert(fontOf("Home") == 9 && shows("Forget this network?") && shows("Deletes its password, disconnects"));
  gWifi.name = "Cafe"; show(Screen::Forget); assert(shows("Its saved password is deleted"));
  std::cout << "PASS Wi-Fi name, Joining and Forget: the Lab's look, with what each is for\n";
}
'''

with tempfile.TemporaryDirectory(prefix='s1-wifi-ui-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-variable', '-Wno-unused-const-variable', '-Wno-sign-compare', '-I', str(src), str(p / 'test.cpp'), '-o', str(p / 'test')],
                   check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS Wi-Fi screens')
