#!/usr/bin/env python3
"""Favorites keep their order and numbers, and come back from saved names against the real menu."""
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
#include "launcher_favorites.h"
constexpr uint16_t rgb565_local(int, int, int) { return 0; }
constexpr uint8_t kLauncherGroupCount = 10, kLauncherMaxSubItems = 9, kFavoritesGroup = 0;
''' + block((parts / '002_main.cpp.inc').read_text(), 'enum class UiMode : uint8_t') + \
    block(table, 'struct LauncherSubItem {') + block(table, 'struct LauncherGroup {') + \
    block(table, 'constexpr LauncherGroup kLauncherGroups') + r'''
using launcher_favorites::List;
using launcher_favorites::Ref;
Ref at(const char* tab, const char* label) {
  for (uint8_t g = 0; g < kLauncherGroupCount; ++g) if (!std::strcmp(kLauncherGroups[g].title, tab))
    for (uint8_t i = 0; i < kLauncherGroups[g].count; ++i) if (!std::strcmp(kLauncherGroups[g].items[i].label, label)) return Ref{g, i};
  assert(false); return Ref{0, 0};
}
const char* label(const List& l, int i) { return kLauncherGroups[l.refs[i].group].items[l.refs[i].item].label; }
const char* tab(const List& l, int i) { return kLauncherGroups[l.refs[i].group].title; }
List load(const char* text) { List l; launcher_favorites::read(l, kLauncherGroups, kLauncherGroupCount, text); return l; }
std::string save(const List& l) {
  char out[384];
  assert(launcher_favorites::write(l, kLauncherGroups, kLauncherGroupCount, out, sizeof out));
  return out;
}
bool add(List& l, const char* t, const char* name) { const Ref r = at(t, name); return l.add(r.group, r.item); }

int main() {
  static_assert(launcher_favorites::kMax == kLauncherMaxSubItems, "a favorite for every number key");
  // Every app in the menu can be a favorite, and comes back from its saved name. Nothing is in
  // two tabs under one name but Pomodoro, and that is told apart by its tab.
  int apps = 0;
  for (uint8_t g = 0; g < kLauncherGroupCount; ++g) for (uint8_t i = 0; i < kLauncherGroups[g].count; ++i) {
    List one; assert(one.add(g, i)); ++apps;
    const std::string text = save(one);
    assert(text == std::string(kLauncherGroups[g].title) + "/" + kLauncherGroups[g].items[i].label + "\n");
    const List back = load(text.c_str());
    assert(back.count == 1 && back.refs[0].group == g && back.refs[0].item == i);
  }
  assert(apps == 48 && kLauncherGroups[kFavoritesGroup].count == 0);

  // New ones go at the end, so the others keep their numbers; nine at most, and no repeats.
  List l;
  assert(add(l, "Games", "Tetris") && add(l, "Media", "Internet Radio") && add(l, "Focus", "Pomodoro"));
  assert(!add(l, "Games", "Tetris") && l.count == 3);
  assert(add(l, "Settings", "Pomodoro") && l.count == 4);
  assert(l.find(at("Media", "Internet Radio").group, at("Media", "Internet Radio").item) == 1 && l.find(0, 0) < 0);
  for (const char* name : {"Snake", "NES", "Checkers", "Minesweeper"}) assert(add(l, "Games", name));
  assert(add(l, "Lab", "Rules") && l.full() && !add(l, "System", "Logs") && l.count == 9);

  // Moving swaps with a neighbour and stops at either end.
  assert(!l.move(0, -1) && !l.move(8, 1) && !l.move(9, 1) && !l.move(2, 2) && !l.move(2, 0));
  assert(l.move(0, 1) && !std::strcmp(label(l, 0), "Internet Radio") && !std::strcmp(label(l, 1), "Tetris"));
  assert(l.move(1, -1) && !std::strcmp(label(l, 0), "Tetris"));
  // Removing closes the gap.
  assert(l.removeAt(1) && l.count == 8 && !std::strcmp(label(l, 1), "Pomodoro") && !l.removeAt(8));

  // What's saved is what comes back, in the same order, Pomodoro's two tabs kept apart.
  const std::string text = save(l);
  assert(text.rfind("Games/Tetris\nFocus/Pomodoro\nSettings/Pomodoro\nGames/Snake\n", 0) == 0);
  const List back = load(text.c_str());
  assert(back.count == l.count);
  for (int i = 0; i < l.count; ++i) assert(back.refs[i].group == l.refs[i].group && back.refs[i].item == l.refs[i].item);

  // An app that moved tab in a newer menu is still found by its name, if only one tab has it.
  List moved = load("Games/Pocket Synth\nOld tab/Calculator\n");
  assert(moved.count == 2 && !std::strcmp(tab(moved, 0), "Tools") && !std::strcmp(label(moved, 1), "Calculator"));
  // But Pomodoro without a tab it's in could be either, so it's dropped.
  assert(load("Old tab/Pomodoro\n").count == 0);
  // Gone apps, broken lines, repeats and Windows line ends don't get in the way.
  List messy = load("Games/Gone\r\n/Tetris\nGames/\nTetris\n\n\nGames/Tetris\r\nGames/Tetris\nMedia/Internet Radio");
  assert(messy.count == 2 && !std::strcmp(label(messy, 0), "Tetris") && !std::strcmp(label(messy, 1), "Internet Radio"));
  assert(load("").count == 0 && load(nullptr).count == 0);
  // Names must match whole: a prefix or a longer name is someone else.
  assert(load("Games/Tet\nGames/Tetris2\nGame/Snake\n").count == 1 && !std::strcmp(label(load("Game/Snake\n"), 0), "Snake"));
  // Past nine, the rest are dropped.
  std::string many;
  for (const char* name : {"Tetris", "NES", "Checkers", "Minesweeper", "Snake"}) many += std::string("Games/") + name + "\n";
  for (const char* name : {"Library", "Folders", "Notes", "Assets", "Files"}) many += std::string("Media/") + name + "\n";
  const List capped = load(many.c_str());
  assert(capped.count == 9 && !std::strcmp(label(capped, 8), "Assets"));

  // Too small a buffer writes nothing rather than half a list.
  char tiny[20] = "x";
  assert(!launcher_favorites::write(l, kLauncherGroups, kLauncherGroupCount, tiny, sizeof tiny) && tiny[0] == '\0');
  assert(!launcher_favorites::write(l, kLauncherGroups, kLauncherGroupCount, tiny, 0));
  List none; char empty[4] = "x";
  assert(launcher_favorites::write(none, kLauncherGroups, kLauncherGroupCount, empty, sizeof empty) && empty[0] == '\0');
  // Nine of the longest names fit the firmware's buffer.
  List longest;
  for (const char* name : {"Hot Air Balloon", "Flappy Bird", "Ball Catch", "Air Music"}) assert(add(longest, "Arcade", name));
  for (const char* name : {"Internet Radio", "Voice Notes", "VLC Remote", "Podcasts", "Library"}) assert(add(longest, "Media", name));
  assert(save(longest).size() < 384);
  std::puts("PASS Favorites: every app saves and comes back by name, nine at most in their order, moves and removals keep numbers, damaged lists recover");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-favorites-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(root / 'bots/CardputerADV/src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
