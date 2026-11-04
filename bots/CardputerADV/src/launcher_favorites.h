#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The menu's first tab: up to nine apps from the other tabs, in the order the owner puts them,
// and 1-9 opens them. Each is saved as its tab's title and its own name, one per line, like
// "Games/Tetris", so a firmware that moves things around in the menu still finds them.
namespace launcher_favorites {
constexpr uint8_t kMax = 9;

struct Ref {
  uint8_t group;
  uint8_t item;
};

struct List {
  uint8_t count = 0;
  Ref refs[kMax] = {};

  int find(uint8_t group, uint8_t item) const {
    for (uint8_t i = 0; i < count; ++i)
      if (refs[i].group == group && refs[i].item == item) return i;
    return -1;
  }
  bool full() const { return count >= kMax; }
  // A new one goes at the end, so it takes the next number and the others keep theirs.
  bool add(uint8_t group, uint8_t item) {
    if (full() || find(group, item) >= 0) return false;
    refs[count++] = Ref{group, item};
    return true;
  }
  bool removeAt(uint8_t index) {
    if (index >= count) return false;
    for (uint8_t i = index; i + 1 < count; ++i) refs[i] = refs[i + 1];
    --count;
    return true;
  }
  // Swaps with the one above or below, so only those two change number. False at either end.
  bool move(uint8_t index, int delta) {
    const int to = static_cast<int>(index) + delta;
    if (index >= count || (delta != -1 && delta != 1) || to < 0 || to >= count) return false;
    const Ref moving = refs[index];
    refs[index] = refs[to];
    refs[to] = moving;
    return true;
  }
};

inline bool sameText(const char* text, const char* other, size_t otherLength) {
  return strlen(text) == otherLength && strncmp(text, other, otherLength) == 0;
}

// The app called label in the tab called title. If that tab no longer has it, the one tab that
// does, so an app that moved tab stays a favorite. A name in two tabs, like Pomodoro, needs both.
template <class Group>
bool locate(const Group* groups, uint8_t groupCount, const char* title, size_t titleLength,
            const char* label, size_t labelLength, Ref& found) {
  int matches = 0;
  Ref only{0, 0};
  for (uint8_t g = 0; g < groupCount; ++g) {
    for (uint8_t i = 0; i < groups[g].count; ++i) {
      if (!sameText(groups[g].items[i].label, label, labelLength)) continue;
      if (sameText(groups[g].title, title, titleLength)) {
        found = Ref{g, i};
        return true;
      }
      ++matches;
      only = Ref{g, i};
    }
  }
  if (matches != 1) return false;
  found = only;
  return true;
}

// Fills the list from saved text. Lines it can't place, repeats and anything past nine are dropped.
template <class Group>
void read(List& list, const Group* groups, uint8_t groupCount, const char* text) {
  list = List();
  if (!text) return;
  while (*text && !list.full()) {
    const char* end = text;
    while (*end && *end != '\n' && *end != '\r') ++end;
    const char* slash = static_cast<const char*>(memchr(text, '/', static_cast<size_t>(end - text)));
    Ref ref{0, 0};
    if (slash && slash > text && slash + 1 < end &&
        locate(groups, groupCount, text, static_cast<size_t>(slash - text), slash + 1,
               static_cast<size_t>(end - slash - 1), ref))
      list.add(ref.group, ref.item);
    text = *end ? end + 1 : end;
  }
}

// The text read() takes back. False, with out left empty, if it doesn't fit in cap.
template <class Group>
bool write(const List& list, const Group* groups, uint8_t groupCount, char* out, size_t cap) {
  if (!cap) return false;
  out[0] = '\0';
  size_t used = 0;
  for (uint8_t i = 0; i < list.count; ++i) {
    const Ref ref = list.refs[i];
    if (ref.group >= groupCount || ref.item >= groups[ref.group].count) continue;
    const char* title = groups[ref.group].title;
    const char* label = groups[ref.group].items[ref.item].label;
    const size_t line = strlen(title) + 1 + strlen(label) + 1;
    if (used + line >= cap) {
      out[0] = '\0';
      return false;
    }
    snprintf(out + used, cap - used, "%s/%s\n", title, label);
    used += line;
  }
  return true;
}
}  // namespace launcher_favorites
