#pragma once
#include "mesh_leaf_state.h"
#include <array>
#include <cstdint>
#include <cstring>

// The people Mesh can message directly: saved from the radios it has heard, kept in NVS, and
// each with the public key from that radio's NodeInfo, so only that radio can read what's sent.
namespace s1_mesh {
constexpr size_t kContacts = 4;
constexpr size_t kNameBytes = 39;  // Meshtastic's long name: 39 bytes and its end.
constexpr const char* kDefaultLongName = "S1-ADV";
constexpr const char* kDefaultShortName = "S1";

struct Contact {
  uint32_t id = 0;
  char name[kNameBytes+1] = {};
  uint8_t key[32] = {};
};

// Printable and whole UTF-8 only, no spaces at either end, and at most kNameBytes.
inline void cleanName(char* out, size_t capacity, const char* in) {
  if (!capacity) return;
  const size_t limit = capacity - 1 < kNameBytes ? capacity - 1 : kNameBytes;
  char buffer[kNameBytes+1];
  copyText(buffer, limit + 1, reinterpret_cast<const uint8_t*>(in), strlen(in));
  const char* start = buffer;
  while (*start == ' ') ++start;
  size_t length = strlen(start);
  while (length && start[length-1] == ' ') --length;
  memmove(out, start, length);
  out[length] = 0;
}

// Meshtastic shows the short name in a small circle. Two words or more give their initials, up to
// four; one word gives its first four letters. Only ASCII letters and digits are used, so the four
// bytes it may hold are always four characters.
inline void shortNameFor(const char* longName, char out[5]) {
  size_t words = 0;
  bool inWord = false;
  for (const char* p = longName; *p; ++p) {
    const bool space = *p == ' ';
    if (!space && !inWord) ++words;
    inWord = !space;
  }
  size_t n = 0;
  inWord = false;
  for (const char* p = longName; *p && n < 4; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool space = c == ' ';
    const bool usable = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (words >= 2) {
      if (!space && !inWord && usable) out[n++] = char(c >= 'a' && c <= 'z' ? c - 32 : c);
    } else if (usable) {
      out[n++] = char(c);
    }
    inWord = !space;
  }
  out[n] = 0;
  if (!n) strcpy(out, kDefaultShortName);
}

struct Contacts {
  std::array<Contact,kContacts> list{};
  size_t count = 0;

  int indexOf(uint32_t id) const {
    for (size_t i = 0; i < count; ++i) if (list[i].id == id) return int(i);
    return -1;
  }
  const Contact* find(uint32_t id) const {
    const int i = indexOf(id);
    return i < 0 ? nullptr : &list[size_t(i)];
  }
  enum class Add : uint8_t { Added, Updated, Full, Bad };
  // Saving a radio that's already a contact takes its current name and key: the person chose it.
  Add add(uint32_t id, const char* name, const uint8_t key[32]) {
    if (!id || id == kBroadcast || !key) return Add::Bad;
    bool blank = true;
    for (size_t i = 0; i < 32; ++i) blank &= key[i] == 0;
    if (blank) return Add::Bad;
    int i = indexOf(id);
    const bool fresh = i < 0;
    if (fresh) {
      if (count == kContacts) return Add::Full;
      i = int(count++);
    }
    Contact& c = list[size_t(i)];
    c.id = id;
    cleanName(c.name, sizeof(c.name), name ? name : "");
    memcpy(c.key, key, 32);
    return fresh ? Add::Added : Add::Updated;
  }
  bool remove(uint32_t id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    for (size_t j = size_t(i) + 1; j < count; ++j) list[j-1] = list[j];
    list[--count] = Contact{};
    return true;
  }
  // A contact's radio announcing a new name. Its key is never changed this way.
  bool rename(uint32_t id, const char* name) {
    const int i = indexOf(id);
    if (i < 0 || !name || !*name) return false;
    char clean[kNameBytes+1];
    cleanName(clean, sizeof(clean), name);
    if (!clean[0] || !strcmp(clean, list[size_t(i)].name)) return false;
    strcpy(list[size_t(i)].name, clean);
    return true;
  }
  // Version, count, then per contact: id (4 bytes, little-endian), key (32), name length, name.
  static constexpr uint8_t kVersion = 1;
  static constexpr size_t kBytes = 2 + kContacts * (4 + 32 + 1 + kNameBytes);
  size_t save(uint8_t* out, size_t capacity) const {
    if (capacity < 2) return 0;
    size_t n = 0;
    out[n++] = kVersion;
    const size_t countAt = n++;
    uint8_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
      const Contact& c = list[i];
      const size_t length = strnlen(c.name, kNameBytes);
      if (n + 37 + length > capacity) break;
      for (int b = 0; b < 32; b += 8) out[n++] = uint8_t(c.id >> b);
      memcpy(out + n, c.key, 32); n += 32;
      out[n++] = uint8_t(length);
      memcpy(out + n, c.name, length); n += length;
      ++kept;
    }
    out[countAt] = kept;
    return n;
  }
  // All or nothing: a damaged copy loads no contacts rather than a wrong key for someone.
  bool load(const uint8_t* in, size_t length) {
    Contacts read;
    if (length < 2 || in[0] != kVersion || in[1] > kContacts) { *this = Contacts{}; return false; }
    size_t n = 2;
    for (size_t i = 0; i < in[1]; ++i) {
      if (n + 37 > length) { *this = Contacts{}; return false; }
      uint32_t id = 0;
      for (int b = 0; b < 32; b += 8) id |= uint32_t(in[n++]) << b;
      uint8_t key[32];
      memcpy(key, in + n, 32); n += 32;
      const size_t nameLength = in[n++];
      if (nameLength > kNameBytes || n + nameLength > length) { *this = Contacts{}; return false; }
      char name[kNameBytes+1];
      memcpy(name, in + n, nameLength); name[nameLength] = 0; n += nameLength;
      if (read.add(id, name, key) != Add::Added) { *this = Contacts{}; return false; }
    }
    if (n != length) { *this = Contacts{}; return false; }
    *this = read;
    return true;
  }
};
}  // namespace s1_mesh
