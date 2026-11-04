#!/usr/bin/env python3
"""Exercise the actual launcher: tabs, five rows, an icon per app, and the keys that drive them."""
from pathlib import Path
import os, re, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
parts = root / 'bots/CardputerADV/src/main_parts'


def block(text, marker):
    a = text.index(marker)
    open_brace = text.index('{', a)
    depth = 0
    for i in range(open_brace, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if not depth:
            return text[a:i + 1] + ';\n'
    raise AssertionError(marker)


render = (parts / '052_main.cpp.inc').read_text()
render = render[render.index('const uint8_t* launcherItemIcon('):render.index('String batteryEtaText()')]
table = (parts / '004_main.cpp.inc').read_text()
keys = (parts / '044_main.cpp.inc').read_text()

# Fn+arrow arrives on its own path, so it is pinned here.
arrows = (parts / '049_main.cpp.inc').read_text()
for held, call in [('upDown', 'moveLauncherSubSelection(-1)'), ('downDown', 'moveLauncherSubSelection(1)'),
                   ('leftDown', 'moveLauncherSelection(-1)'), ('rightDown', 'moveLauncherSelection(1)')]:
    assert re.search(r'gLauncherVisible && ' + held + r'\) \{\s+' + re.escape(call), arrows), held

code = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include "launcher_icons.h"
#include "launcher_shortcuts.h"
using std::min; using std::max;
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  explicit String(int i) : std::string(std::to_string(i)) {}
  String substring(size_t a, size_t b) const { return String(substr(a, b - a)); }
  String operator+(const String& o) const { return String(std::string(*this) + std::string(o)); }
};
String operator+(const char* a, const String& b) { return String(std::string(a) + std::string(b)); }
namespace fonts { const int Font2 = 2, FreeSansBold9pt7b = 9; }
const uint16_t TFT_BLACK = 0, TFT_WHITE = 0xffff;
constexpr uint16_t rgb565_local(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
template <class T> T clampValue(T x, T lo, T hi) { return std::min(hi, std::max(lo, x)); }
constexpr uint8_t kLauncherGroupCount = 9, kLauncherMaxSubItems = 9;
''' + block((parts / '002_main.cpp.inc').read_text(), 'enum class UiMode : uint8_t') + \
    block(table, 'struct LauncherSubItem {') + block(table, 'struct LauncherGroup {') + \
    block(table, 'constexpr LauncherGroup kLauncherGroups') + r'''
int gLauncherSelection = 0, gLauncherSubSelection = 0, gLauncherLastSub[9] = {};
bool gLauncherVisible = true;
struct Text { std::string value; int x, y, font; uint16_t color; };
struct Bitmap { const uint8_t* data; int x, y; uint16_t color; };
struct Fill { int x, y, w, h; uint16_t color; };
struct Canvas {
  int x = 0, y = 0, font = 2;
  uint16_t color = 0;
  bool cleared = false;
  std::vector<Text> texts; std::vector<Bitmap> bitmaps; std::vector<Fill> fills;
  void reset() { *this = Canvas(); }
  static void inScreen(int x, int y, int w, int h) { assert(x >= 0 && y >= 0 && x + w <= 240 && y + h <= 135); }
  void fillScreen(uint16_t) { cleared = true; }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t c) { inScreen(x, y, w, h); fills.push_back({x, y, w, h, c}); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { fillRoundRect(x, y, w, h, 0, c); }
  int underlines = 0;
  void drawFastHLine(int x, int y, int w, uint16_t) { inScreen(x, y, w, 1); ++underlines; }
  void drawBitmap(int x, int y, const uint8_t* b, int w, int h, uint16_t c) {
    assert(b && w == 16 && h == 16); inScreen(x, y, w, h); bitmaps.push_back({b, x, y, c});
  }
  void setFont(const int* f) { font = *f; }
  void setTextColor(uint16_t c) { color = c; }
  void setCursor(int xx, int yy) { x = xx; y = yy; }
  // An upper bound for the bold font: its widest label is about 8.7 px per character.
  int textWidth(const std::string& s) { return int(s.size()) * (font == 9 ? 10 : 8); }
  void print(const std::string& s) {
    assert(cleared);
    inScreen(x, y, textWidth(s), font == 9 ? 19 : 16);
    texts.push_back({s, x, y, font, color}); x += textWidth(s);
  }
} gCanvas;
String fitCurrentFontToWidth(const char* s, int width) { assert(gCanvas.textWidth(s) <= width); return String(s); }
''' + render + r'''
namespace Keyboard_Class {
struct KeysState { bool ctrl = false, enter = false; std::vector<uint8_t> hid_keys; };
}
int groupMove = 0, rowMove = 0;
void moveLauncherSelection(int d) { groupMove += d; }
void moveLauncherSubSelection(int d) { rowMove += d; }
void applyLauncherSelection() {}
void setStatus(const String&, int) {}
''' + block(keys, 'void handleLauncherKey(char key') + r'''
void press(char key, int group, int row) {
  groupMove = rowMove = 0;
  handleLauncherKey(key, Keyboard_Class::KeysState());
  assert(groupMove == group && rowMove == row);
}
int main() {
  for (int g = 0; g < kLauncherGroupCount; ++g) {
    const LauncherGroup& group = kLauncherGroups[g];
    for (int sel = 0; sel < group.count; ++sel) {
      gLauncherSelection = g; gLauncherSubSelection = sel; gCanvas.reset();
      renderLauncherOverlay();
      assert(gCanvas.font == 2);  // screens drawn next expect the default font

      // One tab per group, left to right, none touching the next.
      std::vector<Bitmap> tabs;
      for (const auto& b : gCanvas.bitmaps) if (b.y == 2) tabs.push_back(b);
      assert(tabs.size() == kLauncherGroupCount);
      for (int i = 0; i < kLauncherGroupCount; ++i) {
        assert(tabs[i].data == launcher_icons::kGroups[i]);
        assert(i == 0 || tabs[i].x >= tabs[i - 1].x + 16);
      }

      // Five rows at most, and the selected one is always among them and always barred.
      const int rows = std::min<int>(5, group.count);
      int bars = 0, labels = 0;
      bool selectedShown = false;
      for (const auto& t : gCanvas.texts) {
        if (t.font != 9 || t.value == group.title) continue;
        ++labels;
        const int row = (t.y - 27) / 22;
        assert(t.y == 27 + row * 22 && row >= 0 && row < rows);
        assert(t.x + gCanvas.textWidth(t.value) <= 223);
        if (t.value == group.items[sel].label) {
          selectedShown = true;
          assert(t.color == TFT_BLACK);
          for (const auto& f : gCanvas.fills)
            if (f.color == group.accent && f.y == 24 + row * 22 && f.w == 233) ++bars;
        }
      }
      assert(labels == rows && selectedShown && bars == 1);

      // The printed number is the Ctrl+digit that opens the row.
      for (int i = 0; i < group.count; ++i) {
        const auto& ts = gCanvas.texts;
        auto label = std::find_if(ts.begin(), ts.end(), [&](const Text& t) { return t.value == group.items[i].label && t.font == 9; });
        if (label == ts.end()) continue;
        auto number = std::find_if(ts.begin(), ts.end(), [&](const Text& t) { return t.font == 2 && t.y == label->y; });
        assert(number != ts.end() && number->value == std::to_string(i + 1));
      }

      const bool scrollbar = std::any_of(gCanvas.fills.begin(), gCanvas.fills.end(), [](const Fill& f) { return f.x == 236; });
      assert(scrollbar == (group.count > 5));
      if (scrollbar) {
        const Fill& thumb = gCanvas.fills.back();
        assert(thumb.color == group.accent && thumb.y >= 24 && thumb.y + thumb.h <= 133);
        if (sel + 1 == group.count) assert(thumb.y + thumb.h >= 132);
        if (sel < 5) assert(thumb.y == 24);
      }
    }
  }

  // Every app has its own picture. Pomodoro is listed twice and deliberately shares one.
  std::set<const uint8_t*> seen;
  int items = 0;
  for (const auto& group : kLauncherGroups) for (int i = 0; i < group.count; ++i) {
    const uint8_t* icon = launcherItemIcon(group.items[i]);
    assert(icon);
    ++items;
    const bool first = seen.insert(icon).second;
    assert(first || std::strcmp(group.items[i].label, "Pomodoro") == 0);
  }
  assert(items == 47 && seen.size() == 46);

  // Accents must stay apart once the canvas reduces them to 8 bits.
  std::set<int> accents;
  for (const auto& group : kLauncherGroups)
    accents.insert(((group.accent >> 8) & 0xE0) | ((group.accent >> 6) & 0x1C) | ((group.accent >> 3) & 0x03));
  assert(accents.size() == kLauncherGroupCount);

  press(';', 0, -1); press('.', 0, 1); press(',', -1, 0); press('/', 1, 0);
  // Letters jump to their group in either case, and the old movement letters do nothing.
  using launcher_shortcuts::kGroupLetters;
  for (int group = 0; group < kLauncherGroupCount; ++group) {
    const char letter = kGroupLetters[group];
    for (char key : {letter, char(letter - 'a' + 'A')}) {
      gLauncherSelection = (group + 1) % kLauncherGroupCount;
      press(key, 0, 0);
      assert(gLauncherSelection == group);
    }
    // Each letter is in the name, so the tab can underline it.
    assert(launcher_shortcuts::letterPosition(kLauncherGroups[group].title, group) >= 0);
    gLauncherSelection = group; gLauncherSubSelection = 0; gCanvas.reset(); renderLauncherOverlay();
    assert(gCanvas.underlines == 2);  // The rule under the tabs, and the letter.
  }
  std::set<char> letters(kGroupLetters, kGroupLetters + kLauncherGroupCount);
  assert(letters.size() == kLauncherGroupCount);
  gLauncherSelection = 2;
  for (char key : {'w', 'd', 'h', 'j', 'k', 'x', 'z'}) { press(key, 0, 0); assert(gLauncherSelection == 2); }
  // Ctrl+letter belongs to the rest of the device.
  groupMove = rowMove = 0;
  { Keyboard_Class::KeysState chord; chord.ctrl = true; handleLauncherKey('a', chord); assert(gLauncherSelection == 2); }
}
'''
with tempfile.TemporaryDirectory(prefix='s1-launcher-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'bots/CardputerADV/src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS actual launcher: every selection shows tabs, five rows and a selected bar on screen, each app has its own icon, arrows move, and each group has its letter')
