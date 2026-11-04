#!/usr/bin/env python3
"""Exercise the actual launcher: tabs, five rows, an icon per app, Favorites, and the keys that drive them."""
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


def function(text, marker):
    return block(text, marker)[:-2] + '\n'


render = (parts / '052_main.cpp.inc').read_text()
render = render[render.index('const uint8_t* launcherItemIcon('):render.index('String batteryEtaText()')]
table = (parts / '004_main.cpp.inc').read_text()
favorites = table[table.index('static_assert(launcher_favorites::kMax'):table.index('Preferences gPrefs;')]
keys = (parts / '044_main.cpp.inc').read_text()
keys = keys[keys.index('void launcherNotice('):keys.index('void handleLauncherKey(char key')] + \
    function(keys, 'void handleLauncherKey(char key')
rows = (parts / '024_main.cpp.inc').read_text()
rows = function(rows, 'int launcherRowFor(') + function(rows, 'void showLauncherGroup(')

# Fn+arrow arrives on its own path, so it is pinned here.
arrows = (parts / '049_main.cpp.inc').read_text()
for held, call in [('upDown', 'moveLauncherSubSelection(-1)'), ('downDown', 'moveLauncherSubSelection(1)'),
                   ('leftDown', 'moveLauncherSelection(-1)'), ('rightDown', 'moveLauncherSelection(1)')]:
    assert re.search(r'gLauncherVisible && ' + held + r'\) \{\s+' + re.escape(call), arrows), held

# FreeSansBold9pt7b's advance for each character from space to tilde, so tab widths are real.
# Font2's, from its width table, with ` drawn as a degree sign.
SMALL = '6,3,4,9,8,9,9,3,7,7,8,6,3,6,5,7,8,8,8,8,8,8,8,8,8,8,3,3,6,6,6,8,9,8,8,8,8,8,8,8,8,4,8,8,7,10,8,8,8,8,8,8,8,8,8,10,8,8,8,4,7,4,7,9,5,7,7,7,7,7,6,7,7,4,5,6,4,8,7,8,7,8,6,6,5,7,8,8,6,7,7,5,3,5,8'
BOLD = '5,6,9,10,10,16,13,5,6,6,7,11,4,6,4,5,10,10,10,10,10,10,10,10,10,10,4,4,11,11,11,11,18,13,13,13,13,12,11,14,13,6,10,13,11,16,14,14,12,14,13,12,12,13,12,17,12,12,11,6,5,6,11,10,5,10,11,10,11,10,6,11,11,5,5,10,5,16,11,11,11,11,7,10,6,11,10,14,10,10,9,7,5,7,9'

code = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include "launcher_favorites.h"
#include "launcher_order.h"
#include "launcher_icons.h"
#include "launcher_shortcuts.h"
using std::min; using std::max;
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  explicit String(int i) : std::string(std::to_string(i)) {}
  String substring(size_t a, size_t b) const { return String(substr(a, b - a)); }
};
String operator+(const String& a, const String& b) { return String(static_cast<const std::string&>(a) + b.c_str()); }
String operator+(const String& a, const char* b) { return String(static_cast<const std::string&>(a) + b); }
String operator+(const char* a, const String& b) { return String(a + static_cast<const std::string&>(b)); }
namespace fonts { const int Font2 = 2, FreeSansBold9pt7b = 9; }
const uint16_t TFT_BLACK = 0, TFT_WHITE = 0xffff;
constexpr uint16_t rgb565_local(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
template <class T> T clampValue(T x, T lo, T hi) { return std::min(hi, std::max(lo, x)); }
uint32_t nowMs = 1000;
uint32_t millis() { return nowMs; }
constexpr uint8_t kLauncherGroupCount = 10, kLauncherMaxSubItems = 9, kFavoritesGroup = 0;
''' + block((parts / '002_main.cpp.inc').read_text(), 'enum class UiMode : uint8_t') + \
    block(table, 'struct LauncherSubItem {') + block(table, 'struct LauncherGroup {') + \
    block(table, 'constexpr LauncherGroup kLauncherGroups') + favorites + r'''
int gLauncherSelection = 0, gLauncherSubSelection = 0, gLauncherLastSub[10] = {};
bool gLauncherVisible = true;
String gLauncherNotice;
uint32_t gLauncherNoticeUntilMs = 0;
const int kBold[] = {''' + BOLD + r'''}, kSmall[] = {''' + SMALL + r'''};
struct Text { std::string value; int x, y, font; uint16_t color; };
struct Bitmap { const uint8_t* data; int x, y, w; uint16_t color; };
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
    assert(b && w == h && (w == 16 || (w == 8 && b == launcher_icons::kStarMark)));
    inScreen(x, y, w, h); bitmaps.push_back({b, x, y, w, c});
  }
  void setFont(const int* f) { font = *f; }
  void setTextColor(uint16_t c) { color = c; }
  void setCursor(int xx, int yy) { x = xx; y = yy; }
  int getCursorX() const { return x; }
  // Both fonts' real widths. Anything past ASCII counts as wide as either font's widest.
  int textWidth(const std::string& s) {
    int w = 0;
    for (unsigned char c : s) w += c >= 32 && c < 127 ? (font == 9 ? kBold : kSmall)[c - 32] : (font == 9 ? 18 : 10);
    return w;
  }
  void print(const std::string& s) {
    assert(cleared);
    inScreen(x, y, textWidth(s), font == 9 ? 19 : 16);
    texts.push_back({s, x, y, font, color}); x += textWidth(s);
  }
  const Text* find(const std::string& s) const {
    for (const auto& t : texts) if (t.value == s) return &t;
    return nullptr;
  }
} gCanvas;
String fitCurrentFontToWidth(const char* s, int width) { assert(gCanvas.textWidth(s) <= width); return String(s); }
String fitCurrentFontToWidth(const String& s, int width) { return fitCurrentFontToWidth(s.c_str(), width); }
''' + render + r'''
namespace Keyboard_Class {
struct KeysState { bool ctrl = false, enter = false, fn = false, del = false; std::vector<uint8_t> hid_keys; };
}
int groupMove = 0, rowMove = 0, opened = 0, saves = 0, openedGroup = -1, openedRow = -1;
void moveLauncherSelection(int d) { groupMove += d; }
void moveLauncherSubSelection(int d) { rowMove += d; }
void applyLauncherSelection() { ++opened; openedGroup = gLauncherSelection; openedRow = gLauncherSubSelection; }
void saveFavorites() { ++saves; }
int orderSaves = 0;
void saveLauncherOrder() { ++orderSaves; }
''' + rows + keys + r'''
void press(char key, int group, int row) {
  groupMove = rowMove = 0;
  handleLauncherKey(key, Keyboard_Class::KeysState());
  assert(groupMove == group && rowMove == row);
}
Keyboard_Class::KeysState withFn() { Keyboard_Class::KeysState k; k.fn = true; return k; }
void show(int group, int row) { gLauncherSelection = group; gLauncherSubSelection = row; gCanvas.reset(); renderLauncherOverlay(); }
int at(const char* tab, const char* label) {
  for (int g = 0; g < kLauncherGroupCount; ++g) if (!std::strcmp(kLauncherGroups[g].title, tab))
    for (int i = 0; i < kLauncherGroups[g].count; ++i) if (!std::strcmp(kLauncherGroups[g].items[i].label, label)) return g * 16 + i;
  assert(false); return -1;
}
// Where an app is on screen, which is its place in the table until the tab's order changes.
int rowOf(const char* tab, const char* label) {
  for (int g = 0; g < kLauncherGroupCount; ++g) if (g != kFavoritesGroup && !std::strcmp(launcherGroup(g).title, tab))
    for (int i = 0; i < launcherGroup(g).count; ++i) if (!std::strcmp(launcherGroup(g).items[i].label, label)) return g * 16 + i;
  assert(false); return -1;
}
// Picks an app in its own tab and presses Space on it.
void star(const char* tab, const char* label) { const int a = rowOf(tab, label); gLauncherSelection = a / 16; gLauncherSubSelection = a % 16; press(' ', 0, 0); }
std::string notice() { return nowMs < gLauncherNoticeUntilMs ? std::string(gLauncherNotice) : std::string(); }
const char* favorite(int i) { return gFavoritesTab.items[i].label; }

// What every frame must show, whatever the tab and row.
void checkFrame(int g, int sel) {
  const LauncherGroup& group = launcherGroup(g);
  assert(gCanvas.font == 2);  // screens drawn next expect the default font

  // One icon for each tab but the selected one, which shows its name instead, left to right
  // and none touching the next or the name.
  std::vector<Bitmap> tabs;
  for (const auto& b : gCanvas.bitmaps) if (b.y == 2) tabs.push_back(b);
  assert(tabs.size() == kLauncherGroupCount - 1);
  const Text* title = gCanvas.find(group.title);
  assert(title && title->y == 3 && title->font == 9 && title->color == TFT_BLACK);
  for (int i = 0, t = 0; i < kLauncherGroupCount; ++i) {
    if (i == g) continue;
    assert(tabs[t].data == launcher_icons::kGroups[i] && tabs[t].color == kLauncherGroups[i].accent);
    assert(t == 0 || tabs[t].x >= tabs[t - 1].x + 16);
    if (i < g) assert(tabs[t].x + 16 <= title->x);
    else assert(tabs[t].x >= title->x + gCanvas.textWidth(group.title));
    ++t;
  }

  // Five rows at most, and the selected one is always among them and always barred.
  // Favorites can list two apps of one name, so rows are told apart by where they are.
  const int rows = std::min<int>(5, group.count);
  const int selectedRow = sel - std::max(0, std::min(sel - 4, group.count - 5));
  int bars = 0, labels = 0;
  bool selectedShown = false;
  for (const auto& t : gCanvas.texts) {
    if (t.font != 9 || t.y < 24 || t.value == "No favorites yet") continue;
    ++labels;
    const int row = (t.y - 27) / 22;
    assert(t.y == 27 + row * 22 && row >= 0 && row < rows);
    assert(t.x + gCanvas.textWidth(t.value) <= 223);
    assert((t.color == TFT_BLACK) == (row == selectedRow));
    if (row == selectedRow) {
      selectedShown = t.value == group.items[sel].label;
      for (const auto& f : gCanvas.fills)
        if (f.color == group.accent && f.y == 24 + row * 22 && f.w == 233) ++bars;
    }
  }
  assert(labels == rows && (group.count == 0 || (selectedShown && bars == 1)));

  // The printed number is the key that opens the row: alone on Favorites, with Ctrl elsewhere.
  const auto& ts = gCanvas.texts;
  const int first = std::max(0, std::min(sel - 4, group.count - 5));
  for (int i = first; i < first + rows; ++i) {
    auto label = std::find_if(ts.begin(), ts.end(), [&](const Text& t) { return t.font == 9 && t.y == 27 + (i - first) * 22; });
    assert(label != ts.end() && label->value == group.items[i].label);
    auto number = std::find_if(ts.begin(), ts.end(), [&](const Text& t) { return t.font == 2 && t.y == label->y && t.x == 223; });
    assert(number != ts.end() && number->value == std::to_string(i + 1));
    // A name that's in two tabs has its tab's name beside it on Favorites, before the number.
    auto tab = std::find_if(ts.begin(), ts.end(), [&](const Text& t) { return t.font == 2 && t.y == label->y + 1; });
    const bool shared = !std::strcmp(group.items[i].label, "Pomodoro");
    assert((tab != ts.end()) == (g == kFavoritesGroup && shared));
    if (tab != ts.end()) assert(tab->value == kLauncherGroups[gFavorites.refs[i].group].title &&
                                tab->x > label->x + gCanvas.textWidth(label->value) && tab->x + gCanvas.textWidth(tab->value) < 223 &&
                                tab->color == (i == sel ? TFT_BLACK : rgb565_local(109, 109, 85)));
    // On Favorites an app keeps its own tab's colour. In its own tab, a favorite has a star.
    const int home = g == kFavoritesGroup ? gFavorites.refs[i].group : g;
    const bool selected = i == sel;
    auto icon = std::find_if(gCanvas.bitmaps.begin(), gCanvas.bitmaps.end(), [&](const Bitmap& b) { return b.y == label->y - 1 && b.x == 7; });
    assert(icon != gCanvas.bitmaps.end() && icon->color == (selected ? TFT_BLACK : kLauncherGroups[home].accent));
    auto mark = std::find_if(gCanvas.bitmaps.begin(), gCanvas.bitmaps.end(), [&](const Bitmap& b) { return b.w == 8 && b.y == label->y + 3; });
    const bool marked = g != kFavoritesGroup && gFavorites.find(g, launcherTableItem(g, i)) >= 0;
    assert((mark != gCanvas.bitmaps.end()) == marked);
    if (marked) assert(mark->x + 8 < 223 && label->x + gCanvas.textWidth(label->value) < mark->x &&
                       mark->color == (selected ? TFT_BLACK : kLauncherGroups[kFavoritesGroup].accent));
  }

  const bool scrollbar = std::any_of(gCanvas.fills.begin(), gCanvas.fills.end(), [](const Fill& f) { return f.x == 236; });
  assert(scrollbar == (group.count > 5));
  if (scrollbar) {
    const Fill& thumb = *std::find_if(gCanvas.fills.rbegin(), gCanvas.fills.rend(), [](const Fill& f) { return f.x == 236; });
    assert(thumb.color == group.accent && thumb.y >= 24 && thumb.y + thumb.h <= 133);
    if (sel + 1 == group.count) assert(thumb.y + thumb.h >= 132);
    if (sel < 5) assert(thumb.y == 24);
  }

  // An empty Favorites tab says how to fill it, and a short one how to change it.
  assert(!!gCanvas.find("No favorites yet") == (g == kFavoritesGroup && group.count == 0));
  const Text* hint = gCanvas.find("Fn+up/down moves  Space removes");
  assert(!!hint == (g == kFavoritesGroup && group.count > 0 && group.count < 5));
  if (hint) assert(hint->y >= 24 + group.count * 22);
}

void checkEveryFrame() {
  for (int g = 0; g < kLauncherGroupCount; ++g) {
    const LauncherGroup& group = launcherGroup(g);
    for (int sel = 0; sel < std::max<int>(1, group.count); ++sel) { show(g, sel); checkFrame(g, sel); }
  }
}

int main() {
  checkEveryFrame();

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
  assert(kLauncherGroups[kFavoritesGroup].count == 0 && !std::strcmp(kLauncherGroups[kFavoritesGroup].title, "Favorites"));

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
    show(group, 0);
    assert(gCanvas.underlines == 2);  // The rule under the tabs, and the letter.
  }
  std::set<char> letters(kGroupLetters, kGroupLetters + kLauncherGroupCount);
  assert(letters.size() == kLauncherGroupCount && kGroupLetters[kFavoritesGroup] == 'v');
  gLauncherSelection = 2;
  for (char key : {'w', 'd', 'h', 'j', 'k', 'x', 'z'}) { press(key, 0, 0); assert(gLauncherSelection == 2); }
  // Ctrl+letter belongs to the rest of the device.
  groupMove = rowMove = 0;
  { Keyboard_Class::KeysState chord; chord.ctrl = true; handleLauncherKey('a', chord); assert(gLauncherSelection == 2); }

  // With no favorites, a number opens nothing and says why, and Space on Favorites says how.
  gLauncherSelection = 3; opened = 0; press('1', 0, 0);
  assert(opened == 0 && gLauncherSelection == 3 && notice() == "No favorites yet. Space adds one");
  gLauncherSelection = kFavoritesGroup; gLauncherSubSelection = 0;
  press(' ', 0, 0); press('\n', 0, 0);
  assert(gFavorites.count == 0 && saves == 0 && notice() == "Space on any app adds it here" && opened == 1);
  { Keyboard_Class::KeysState del; del.del = true; handleLauncherKey('\b', del); }
  handleLauncherKey('.', withFn()); assert(gFavorites.count == 0 && saves == 0);

  // Space on an app in its own tab adds it with the next number, and Space again takes it off.
  star("Games", "Tetris");
  assert(gFavorites.count == 1 && !std::strcmp(favorite(0), "Tetris") && saves == 1 && notice() == "Tetris is favorite 1");
  show(gLauncherSelection, gLauncherSubSelection); checkFrame(gLauncherSelection, gLauncherSubSelection);
  star("Games", "Tetris");
  assert(gFavorites.count == 0 && gFavoritesTab.count == 0 && saves == 2 && notice() == "Tetris left Favorites");
  // The notice sits over the last row for a moment, then goes.
  show(5, 0); assert(gCanvas.find("Tetris left Favorites") && gCanvas.find("Tetris left Favorites")->y == 115);
  nowMs += 1600; show(5, 0); assert(!gCanvas.find("Tetris left Favorites") && gLauncherNoticeUntilMs == 0);

  // Up to nine, from any tab. Pomodoro is in two tabs and each is its own favorite.
  const char* picks[][2] = {{"Media", "Internet Radio"}, {"Games", "Snake"}, {"AI Agent", "AI Chat"},
                            {"Focus", "Pomodoro"}, {"Settings", "Pomodoro"}, {"Tools", "Calculator"},
                            {"Arcade", "Hot Air Balloon"}, {"Lab", "Sequences"}, {"AI Agent", "Realtime"}};
  for (int i = 0; i < 9; ++i) {
    star(picks[i][0], picks[i][1]);
    assert(gFavorites.count == i + 1 && !std::strcmp(favorite(i), picks[i][1]) && saves == 3 + i);
    assert(notice() == std::string(picks[i][1]) + " is favorite " + std::to_string(i + 1));
  }
  star("System", "Logs");
  assert(gFavorites.count == 9 && saves == 11 && notice() == "Favorites is full at 9");
  checkEveryFrame();  // Nine on Favorites scroll, and their tabs show their stars.
  show(kFavoritesGroup, 0);
  assert(gCanvas.find("Internet Radio") && !gCanvas.find("Sequences") && !gCanvas.find("Fn+up/down moves  Space removes"));

  // 1-9 opens that favorite from any tab, and the menu shows it opened from Favorites.
  for (int g : {1, 5, 9}) for (char key = '1'; key <= '9'; ++key) {
    gLauncherSelection = g; gLauncherSubSelection = 0; opened = 0;
    press(key, 0, 0);
    assert(opened == 1 && openedGroup == kFavoritesGroup && openedRow == key - '1');
  }
  // Ctrl with a number still opens that row of the tab on screen.
  {
    Keyboard_Class::KeysState chord; chord.ctrl = true; chord.hid_keys = {0x20};  // the 3 key
    gLauncherSelection = 2; opened = 0; handleLauncherKey('#', chord);
    assert(opened == 1 && openedGroup == 2 && openedRow == 2);
    chord.hid_keys = {0x26};  // 9: Media has nine rows now
    opened = 0; handleLauncherKey('(', chord); assert(opened == 1 && openedGroup == 2 && openedRow == 8);
    gLauncherSelection = 1; opened = 0; handleLauncherKey('(', chord);
    assert(opened == 0 && notice() == "No row 9 here");
  }

  // Fn with up or down moves the selected favorite, and its number goes with it.
  gLauncherSelection = kFavoritesGroup; gLauncherSubSelection = 0;
  handleLauncherKey(';', withFn()); assert(gLauncherSubSelection == 0 && !std::strcmp(favorite(0), "Internet Radio"));
  const int before = saves;
  handleLauncherKey('.', withFn());
  assert(gLauncherSubSelection == 1 && !std::strcmp(favorite(0), "Snake") && !std::strcmp(favorite(1), "Internet Radio") && saves == before + 1);
  for (int i = 0; i < 10; ++i) handleLauncherKey('.', withFn());
  assert(gLauncherSubSelection == 8 && !std::strcmp(favorite(8), "Internet Radio") && !std::strcmp(favorite(7), "Realtime"));
  assert(saves == before + 8 && gLauncherLastSub[kFavoritesGroup] == 8);
  handleLauncherKey(';', withFn()); assert(gLauncherSubSelection == 7 && !std::strcmp(favorite(7), "Internet Radio"));
  // Plain up and down on Favorites move the bar, as anywhere.
  gLauncherSelection = kFavoritesGroup; rowMove = 0; press('.', 0, 1);

  // Del or Space on Favorites takes the selected one off, and the bar stays in the list.
  gLauncherSelection = kFavoritesGroup; gLauncherSubSelection = 8;
  { Keyboard_Class::KeysState del; del.del = true; handleLauncherKey('\b', del); }
  assert(gFavorites.count == 8 && gLauncherSubSelection == 7 && notice() == "Realtime left Favorites");
  gLauncherSubSelection = 0; press(' ', 0, 0);
  assert(gFavorites.count == 7 && gLauncherSubSelection == 0 && !std::strcmp(favorite(0), "AI Chat"));
  // A number past the list says so.
  opened = 0; press('8', 0, 0); assert(opened == 0 && notice() == "No favorite 8");
  // Removing from Favorites takes the star off the app in its own tab.
  const int pomodoro = at("Settings", "Pomodoro");
  assert(gFavorites.find(pomodoro / 16, pomodoro % 16) >= 0);
  gLauncherSelection = kFavoritesGroup;
  for (gLauncherSubSelection = 0; std::strcmp(favorite(gLauncherSubSelection), "Pomodoro") ||
       gFavorites.refs[gLauncherSubSelection].group != pomodoro / 16; ++gLauncherSubSelection) {}
  press(' ', 0, 0);
  assert(gFavorites.find(pomodoro / 16, pomodoro % 16) < 0 && gFavorites.find(at("Focus", "Pomodoro") / 16, at("Focus", "Pomodoro") % 16) >= 0);
  checkEveryFrame();
  // Short lists show how to change them.
  while (gFavorites.count > 3) { gLauncherSelection = kFavoritesGroup; gLauncherSubSelection = 0; press(' ', 0, 0); }
  checkEveryFrame();
  show(kFavoritesGroup, 0); assert(gCanvas.find("Fn+up/down moves  Space removes"));

  // Fn with up or down moves an app in every other tab too, and its number goes with it.
  {
    const int before = orderSaves, favoriteSaves = saves;
    gLauncherSelection = 5; gLauncherSubSelection = 4;  // Games, Snake
    groupMove = rowMove = 0; handleLauncherKey('.', withFn());
    assert(orderSaves == before && gLauncherSubSelection == 4 && rowMove == 0);  // Already last: nothing.
    for (int i = 0; i < 4; ++i) handleLauncherKey(';', withFn());
    assert(gLauncherSubSelection == 0 && gLauncherLastSub[5] == 0 && orderSaves == before + 4 && saves == favoriteSaves && rowMove == 0);
    const char* games[] = {"Snake", "Tetris", "NES", "Checkers", "Minesweeper"};
    for (int r = 0; r < 5; ++r) assert(!std::strcmp(launcherGroup(5).items[r].label, games[r]));
    handleLauncherKey(';', withFn()); assert(orderSaves == before + 4);  // Already first.
    handleLauncherKey('.', withFn()); handleLauncherKey(';', withFn());
    assert(orderSaves == before + 6 && !std::strcmp(launcherGroup(5).items[0].label, "Snake"));
    // The menu's table stays as it was, and so do the other tabs.
    assert(!std::strcmp(kLauncherGroups[5].items[0].label, "Tetris") && launcherTableItem(5, 0) == 4);
    assert(!std::strcmp(launcherGroup(6).items[0].label, "Air Music") && launcherTableItem(6, 0) == 0);
    // Ctrl with a number opens the row as it is now.
    Keyboard_Class::KeysState chord; chord.ctrl = true; chord.hid_keys = {0x1e};  // the 1 key
    opened = 0; handleLauncherKey('!', chord);
    assert(opened == 1 && openedGroup == 5 && openedRow == 0 && launcherGroup(5).items[0].mode == UiMode::Snake);
    // Space makes the app on the row a favorite, wherever it moved, and its star follows it.
    const launcher_favorites::Ref snake{5, 4};
    if (gFavorites.find(snake.group, snake.item) >= 0) { gLauncherSelection = 5; gLauncherSubSelection = 0; press(' ', 0, 0); }
    assert(gFavorites.find(snake.group, snake.item) < 0);
    gLauncherSelection = 5; gLauncherSubSelection = 0; press(' ', 0, 0);
    assert(gFavorites.find(snake.group, snake.item) == gFavorites.count - 1 && !std::strcmp(favorite(gFavorites.count - 1), "Snake"));
    assert(notice() == "Snake is favorite " + std::to_string(gFavorites.count));
    for (int sel = 0; sel < 5; ++sel) { show(5, sel); checkFrame(5, sel); }
    show(5, 1);
    assert(std::any_of(gCanvas.bitmaps.begin(), gCanvas.bitmaps.end(), [](const Bitmap& b) { return b.w == 8 && b.y == 30; }));
    // Moving a favorite's app in its own tab leaves Favorites as it was.
    const int was = gFavorites.find(snake.group, snake.item);
    gLauncherSelection = 5; gLauncherSubSelection = 0; handleLauncherKey('.', withFn());
    assert(gFavorites.find(snake.group, snake.item) == was && !std::strcmp(favorite(was), "Snake") && !std::strcmp(launcherGroup(5).items[1].label, "Snake"));
    checkEveryFrame();
  }
}
'''
with tempfile.TemporaryDirectory(prefix='s1-launcher-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'bots/CardputerADV/src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS actual launcher: every tab and row on screen, an icon per app, Favorites added, opened by number, '
      'reordered and removed, apps moved in every tab, and each group has its letter')
