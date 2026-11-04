#include "../src/launcher_shortcuts.h"

#include <cassert>
#include <iostream>

using launcher_shortcuts::Kind;

int main() {
  unsigned checks = 0;
  for (uint8_t hid = 0x1e; hid <= 0x26; ++hid) {
    assert(launcher_shortcuts::digitFromHid(hid) == static_cast<char>('1' + hid - 0x1e));
    checks += 1;
  }
  assert(launcher_shortcuts::digitFromHid(0x27) == '0');
  assert(launcher_shortcuts::digitFromHid(0x04) == '\0');
  checks += 2;
  // On its own, a digit opens that favorite, from any tab, and only if there is one.
  for (char key = '1'; key <= '9'; ++key) {
    const auto action = launcher_shortcuts::resolve(key, false, 10, 6, 9);
    assert(action.kind == Kind::Favorite);
    assert(action.index == static_cast<unsigned>(key - '1'));
    checks += 2;
  }
  assert(launcher_shortcuts::resolve('4', false, 10, 6, 3).kind == Kind::InvalidFavorite);
  assert(launcher_shortcuts::resolve('4', false, 10, 6, 3).index == 3);
  assert(launcher_shortcuts::resolve('1', false, 10, 6, 0).kind == Kind::InvalidFavorite);
  assert(launcher_shortcuts::resolve('0', false, 10, 6, 9).kind == Kind::None);
  checks += 4;
  for (char key = '1'; key <= '9'; ++key) {
    const auto action = launcher_shortcuts::resolve(key, true, 10, 9, 0);
    assert(action.kind == Kind::Submenu);
    assert(action.index == static_cast<unsigned>(key - '1'));
    checks += 2;
  }

  // Ctrl has priority: Ctrl+3 opens row 3 of the tab on screen and never favorite 3.
  assert(launcher_shortcuts::resolve('3', true, 10, 6, 9).kind == Kind::Submenu);
  assert(launcher_shortcuts::resolve('6', true, 10, 3, 9).kind == Kind::InvalidSubmenu);
  assert(launcher_shortcuts::resolve('7', true, 10, 6, 9).kind == Kind::InvalidSubmenu);
  assert(launcher_shortcuts::resolve('9', true, 10, 8, 9).kind == Kind::InvalidSubmenu);
  checks += 4;
  assert(launcher_shortcuts::resolve('9', true, 10, 9, 0).kind == Kind::Submenu);
  assert(launcher_shortcuts::resolve('9', true, 10, 9, 0).index == 8);
  checks += 2;

  // Letters jump to groups in menu order, in either case, and only for groups that exist.
  const char letters[] = "vamftgrsyl";
  for (int group = 0; group < 10; ++group) {
    assert(launcher_shortcuts::resolve(letters[group], false, 10, 6, 0).kind == Kind::Group);
    assert(launcher_shortcuts::resolve(letters[group], false, 10, 6, 0).index == group);
    assert(launcher_shortcuts::resolve(static_cast<char>(letters[group] - 'a' + 'A'), false, 10, 6, 0).index == group);
    assert(launcher_shortcuts::resolve(letters[group], true, 10, 6, 0).kind == Kind::None);
    checks += 4;
  }
  assert(launcher_shortcuts::resolve('l', false, 9, 6, 0).kind == Kind::None);
  assert(launcher_shortcuts::resolve('w', false, 10, 6, 0).kind == Kind::None);
  assert(launcher_shortcuts::resolve(' ', false, 10, 6, 9).kind == Kind::None);
  assert(launcher_shortcuts::letterPosition("Arcade", 6) == 1 && launcher_shortcuts::letterPosition("System", 8) == 1);
  assert(launcher_shortcuts::letterPosition("AI Agent", 1) == 0 && launcher_shortcuts::letterPosition("Nope", 99) == -1);
  assert(launcher_shortcuts::letterPosition("Favorites", 0) == 2);
  checks += 6;

  std::cout << "launcher_shortcut_checks=" << checks << '\n';
}
