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
  for (char key = '1'; key <= '7'; ++key) {
    const auto action = launcher_shortcuts::resolve(key, false, 7, 6);
    assert(action.kind == Kind::Group);
    assert(action.index == static_cast<unsigned>(key - '1'));
    checks += 2;
  }
  for (char key = '1'; key <= '6'; ++key) {
    const auto action = launcher_shortcuts::resolve(key, true, 7, 6);
    assert(action.kind == Kind::Submenu);
    assert(action.index == static_cast<unsigned>(key - '1'));
    checks += 2;
  }

  // Ctrl has priority: Ctrl+3 opens submenu 3 and must never select group 3.
  assert(launcher_shortcuts::resolve('3', true, 7, 6).kind == Kind::Submenu);
  assert(launcher_shortcuts::resolve('6', true, 7, 3).kind == Kind::InvalidSubmenu);
  assert(launcher_shortcuts::resolve('7', true, 7, 6).kind == Kind::InvalidSubmenu);
  assert(launcher_shortcuts::resolve('8', false, 7, 6).kind == Kind::None);
  checks += 4;
  assert(launcher_shortcuts::resolve('7', true, 7, 7).kind == Kind::Submenu);
  assert(launcher_shortcuts::resolve('7', true, 7, 7).index == 6);
  checks += 2;

  // Letters jump to groups in menu order, in either case, and only for groups that exist.
  const char letters[] = "amftgrsyl";
  for (int group = 0; group < 9; ++group) {
    assert(launcher_shortcuts::resolve(letters[group], false, 9, 6).kind == Kind::Group);
    assert(launcher_shortcuts::resolve(letters[group], false, 9, 6).index == group);
    assert(launcher_shortcuts::resolve(static_cast<char>(letters[group] - 'a' + 'A'), false, 9, 6).index == group);
    assert(launcher_shortcuts::resolve(letters[group], true, 9, 6).kind == Kind::None);
    checks += 4;
  }
  assert(launcher_shortcuts::resolve('l', false, 8, 6).kind == Kind::None);
  assert(launcher_shortcuts::resolve('w', false, 9, 6).kind == Kind::None);
  assert(launcher_shortcuts::letterPosition("Arcade", 5) == 1 && launcher_shortcuts::letterPosition("System", 7) == 1);
  assert(launcher_shortcuts::letterPosition("AI Agent", 0) == 0 && launcher_shortcuts::letterPosition("Nope", 99) == -1);
  checks += 4;

  std::cout << "launcher_shortcut_checks=" << checks << '\n';
}
