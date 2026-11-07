#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "launcher_favorites.h"

// The order of the apps in each menu tab, which the owner can change. Row r of tab g shows app
// order[g][r] of that tab in the menu's table. Saved as "Tab/Label" lines, every app of each tab
// that isn't in the table's order, so a firmware that adds or moves apps still reads it: apps it
// doesn't mention follow the ones it does, in the table's order.
namespace launcher_order {
constexpr uint8_t kMax = launcher_favorites::kMax;

template <class Group>
void reset(uint8_t order[][kMax], const Group*, uint8_t groupCount) {
  for (uint8_t g = 0; g < groupCount; ++g)
    for (uint8_t r = 0; r < kMax; ++r) order[g][r] = r;
}

inline bool inTableOrder(const uint8_t* tab, uint8_t count) {
  for (uint8_t r = 0; r < count; ++r)
    if (tab[r] != r) return false;
  return true;
}

// Swaps a row with the one above or below. False at either end.
inline bool move(uint8_t* tab, uint8_t count, uint8_t row, int delta) {
  const int to = static_cast<int>(row) + delta;
  if (row >= count || (delta != -1 && delta != 1) || to < 0 || to >= count) return false;
  const uint8_t moving = tab[row];
  tab[row] = tab[to];
  tab[to] = moving;
  return true;
}

template <class Group>
void read(uint8_t order[][kMax], const Group* groups, uint8_t groupCount, const char* text) {
  reset(order, groups, groupCount);
  if (!text) return;
  uint8_t placed[256 / 8] = {};  // Which apps a saved line has already put somewhere, by g * kMax + item.
  uint8_t used[64] = {};         // How many rows of each tab the saved lines have filled.
  if (groupCount > sizeof(used) || groupCount * kMax > 256) return;
  while (*text) {
    const char* end = text;
    while (*end && *end != '\n' && *end != '\r') ++end;
    const char* slash = static_cast<const char*>(memchr(text, '/', static_cast<size_t>(end - text)));
    if (slash && slash > text && slash + 1 < end) {
      const size_t titleLength = static_cast<size_t>(slash - text), labelLength = static_cast<size_t>(end - slash - 1);
      for (uint8_t g = 0; g < groupCount; ++g) {
        if (!launcher_favorites::sameText(groups[g].title, text, titleLength)) continue;
        for (uint8_t i = 0; i < groups[g].count && i < kMax; ++i) {
          const unsigned bit = g * kMax + i;
          if (!launcher_favorites::sameApp(groups[g].items[i].label, slash + 1, labelLength) ||
              (placed[bit / 8] & (1u << (bit % 8))))
            continue;
          placed[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
          order[g][used[g]++] = i;
          break;
        }
        break;
      }
    }
    text = *end ? end + 1 : end;
  }
  for (uint8_t g = 0; g < groupCount; ++g)
    for (uint8_t i = 0; i < groups[g].count && i < kMax; ++i) {
      const unsigned bit = g * kMax + i;
      if (!(placed[bit / 8] & (1u << (bit % 8)))) order[g][used[g]++] = i;
    }
}

// The text read() takes back: nothing for a tab still in the table's order. False, with out
// left empty, if it doesn't fit in cap.
template <class Group>
bool write(const uint8_t order[][kMax], const Group* groups, uint8_t groupCount, char* out, size_t cap) {
  if (!cap) return false;
  out[0] = '\0';
  size_t used = 0;
  for (uint8_t g = 0; g < groupCount; ++g) {
    const uint8_t count = groups[g].count < kMax ? groups[g].count : kMax;
    if (inTableOrder(order[g], count)) continue;
    for (uint8_t r = 0; r < count; ++r) {
      const char* label = groups[g].items[order[g][r]].label;
      const size_t line = strlen(groups[g].title) + 1 + strlen(label) + 1;
      if (used + line >= cap) {
        out[0] = '\0';
        return false;
      }
      snprintf(out + used, cap - used, "%s/%s\n", groups[g].title, label);
      used += line;
    }
  }
  return true;
}
}  // namespace launcher_order
