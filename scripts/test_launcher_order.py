#!/usr/bin/env python3
"""Each menu tab's own order: moves, and saving and reading it by name against the real menu."""
from pathlib import Path
import os, subprocess, tempfile

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


table = (parts / '004_main.cpp.inc').read_text()
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "launcher_order.h"
constexpr uint16_t rgb565_local(int, int, int) { return 0; }
constexpr uint8_t kLauncherGroupCount = 10, kLauncherMaxSubItems = 9, kFavoritesGroup = 0;
''' + block((parts / '002_main.cpp.inc').read_text(), 'enum class UiMode : uint8_t') + \
    block(table, 'struct LauncherSubItem {') + block(table, 'struct LauncherGroup {') + \
    block(table, 'constexpr LauncherGroup kLauncherGroups') + r'''
using Order = uint8_t[kLauncherGroupCount][launcher_order::kMax];
int tab(const char* title) {
  for (int g = 0; g < kLauncherGroupCount; ++g) if (!std::strcmp(kLauncherGroups[g].title, title)) return g;
  assert(false); return -1;
}
const char* row(const Order& o, const char* title, int r) { const int g = tab(title); return kLauncherGroups[g].items[o[g][r]].label; }
std::string save(const Order& o) {
  static char out[1600];
  assert(launcher_order::write(o, kLauncherGroups, kLauncherGroupCount, out, sizeof out));
  return out;
}
void load(Order& o, const char* text) { launcher_order::read(o, kLauncherGroups, kLauncherGroupCount, text); }
// Each tab still shows each of its apps exactly once.
void whole(const Order& o) {
  for (int g = 0; g < kLauncherGroupCount; ++g) {
    bool seen[launcher_order::kMax] = {};
    for (int r = 0; r < kLauncherGroups[g].count; ++r) { assert(o[g][r] < kLauncherGroups[g].count && !seen[o[g][r]]); seen[o[g][r]] = true; }
  }
}

int main() {
  static_assert(launcher_order::kMax == kLauncherMaxSubItems, "every row of every tab has a place");
  Order o;
  launcher_order::reset(o, kLauncherGroups, kLauncherGroupCount);
  whole(o);
  assert(save(o).empty());  // The table's order is never saved.

  // Moves swap with a neighbour and stop at either end.
  const int games = tab("Games");
  const uint8_t count = kLauncherGroups[games].count;
  assert(!launcher_order::move(o[games], count, 0, -1) && !launcher_order::move(o[games], count, count - 1, 1));
  assert(!launcher_order::move(o[games], count, count, -1) && !launcher_order::move(o[games], count, 1, 2) && !launcher_order::move(o[games], count, 1, 0));
  assert(launcher_order::move(o[games], count, 4, -1) && launcher_order::move(o[games], count, 3, -1));
  assert(!std::strcmp(row(o, "Games", 2), "Snake") && !std::strcmp(row(o, "Games", 3), "Checkers"));
  whole(o);
  // Only the changed tab is saved, every app of it, and it comes back the same.
  const std::string text = save(o);
  assert(text == "Games/Tetris\nGames/NES\nGames/Snake\nGames/Checkers\nGames/Minesweeper\n");
  Order back; load(back, text.c_str());
  assert(!std::memcmp(back, o, sizeof o));

  // Every tab turned upside down still fits the firmware's buffer and comes back.
  Order flipped;
  launcher_order::reset(flipped, kLauncherGroups, kLauncherGroupCount);
  for (int g = 0; g < kLauncherGroupCount; ++g)
    for (int r = 0; r < kLauncherGroups[g].count; ++r) flipped[g][r] = static_cast<uint8_t>(kLauncherGroups[g].count - 1 - r);
  const std::string all = save(flipped);
  assert(all.size() < 1400);
  Order flippedBack; load(flippedBack, all.c_str());
  assert(!std::memcmp(flippedBack, flipped, sizeof flipped));
  char small[64];
  assert(!launcher_order::write(flipped, kLauncherGroups, kLauncherGroupCount, small, sizeof small) && small[0] == '\0');

  // A newer menu with an app this list doesn't name puts it after the named ones, in the table's
  // order; a name the menu no longer has is passed over, and so are repeats and broken lines.
  Order partial;
  load(partial, "Games/Snake\nGames/Gone\nGames/Snake\nGames/\n/NES\nNowhere/Tetris\r\nGames/Checkers\r\n");
  const char* expect[] = {"Snake", "Checkers", "Tetris", "NES", "Minesweeper"};
  for (int r = 0; r < 5; ++r) assert(!std::strcmp(row(partial, "Games", r), expect[r]));
  whole(partial);
  for (int g = 0; g < kLauncherGroupCount; ++g) if (g != games)
    for (int r = 0; r < kLauncherGroups[g].count; ++r) assert(partial[g][r] == r);
  // Pomodoro is in two tabs; each line names its own.
  Order pomodoro; load(pomodoro, "Settings/Pomodoro\nFocus/Alarm\n");
  assert(!std::strcmp(row(pomodoro, "Settings", 0), "Pomodoro") && !std::strcmp(row(pomodoro, "Settings", 1), "Device"));
  assert(!std::strcmp(row(pomodoro, "Focus", 0), "Alarm") && !std::strcmp(row(pomodoro, "Focus", 1), "Pomodoro"));
  // Nothing saved, or nothing readable, is the table's order.
  Order none; load(none, ""); load(none, nullptr); load(none, "\n\n");
  for (int g = 0; g < kLauncherGroupCount; ++g) for (int r = 0; r < kLauncherGroups[g].count; ++r) assert(none[g][r] == r);
  // Favorites has no apps in the table, so there's nothing of it to order.
  assert(kLauncherGroups[kFavoritesGroup].count == 0);
  std::puts("PASS tab order: moves stop at the ends, only changed tabs are saved, whole tabs come back, new and gone apps handled");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-order-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(root / 'bots/CardputerADV/src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
