#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Mesh's private channel: a name and a key, kept alongside the public channel the way a
// Meshtastic app adds a secondary one. Anyone with both reads it; nobody else can. The key is
// shown and typed as base64, as the Meshtastic app shows a channel's key. No hardware here.
namespace s1_mesh {
constexpr size_t kChannelNameMax = 11;   // Meshtastic's limit on a channel name.
constexpr size_t kChannelKeyMax = 32;    // AES-256.
constexpr size_t kChannelKeyText = 45;   // 32 bytes as padded base64, and its end.

// The send target for the private channel. Contacts are 1 to their count, and 0 is public, so a
// fixed number keeps it from shifting when a contact is added or removed.
constexpr uint8_t kTargetPrivate = 255;

struct PrivateChannel {
  char name[kChannelNameMax + 1] = {};
  uint8_t key[kChannelKeyMax] = {};
  uint8_t keyLength = 0;  // 0 means there is no private channel.
  bool set() const { return keyLength && name[0]; }
};

// Letters, digits, spaces, - and _, up to 11, and not only spaces. A name the Meshtastic app
// accepts too, and one that can be typed on the Cardputer.
inline bool validChannelName(const char* name) {
  if (!name) return false;
  const size_t length = strlen(name);
  if (!length || length > kChannelNameMax || name[0] == ' ' || name[length - 1] == ' ') return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

inline int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;  // Standard and URL-safe both.
  if (c == '/' || c == '_') return 63;
  return -1;
}
// A key typed as base64: spaces ignored, padding optional. Only 16 or 32 bytes is a private key;
// Meshtastic's one-byte keys like "AQ==" stand for well-known public ones.
inline bool keyFromBase64(const char* text, uint8_t* out, uint8_t& length) {
  length = 0;
  if (!text) return false;
  uint32_t bits = 0;
  int count = 0;
  size_t produced = 0, padding = 0;
  uint8_t buffer[kChannelKeyMax];
  for (const char* p = text; *p; ++p) {
    if (*p == ' ') continue;
    if (*p == '=') { ++padding; continue; }
    if (padding) return false;  // Nothing after the padding.
    const int value = base64Value(*p);
    if (value < 0) return false;
    bits = (bits << 6) | uint32_t(value);
    count += 6;
    if (count >= 8) {
      count -= 8;
      if (produced >= sizeof(buffer)) return false;
      buffer[produced++] = uint8_t((bits >> count) & 0xFF);
    }
  }
  if (padding > 2 || count >= 6 || (bits & ((1u << count) - 1))) return false;  // Leftover bits must be zero.
  if (produced != 16 && produced != 32) return false;
  memcpy(out, buffer, produced);
  length = uint8_t(produced);
  return true;
}
inline void keyToBase64(const uint8_t* key, uint8_t length, char* out, size_t size) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t used = 0;
  const auto put = [&](char c) { if (used + 1 < size) out[used++] = c; };
  for (size_t i = 0; i < length; i += 3) {
    const uint32_t chunk = uint32_t(key[i]) << 16 | (i + 1 < length ? uint32_t(key[i + 1]) << 8 : 0) |
                           (i + 2 < length ? uint32_t(key[i + 2]) : 0);
    put(kAlphabet[(chunk >> 18) & 63]);
    put(kAlphabet[(chunk >> 12) & 63]);
    put(i + 1 < length ? kAlphabet[(chunk >> 6) & 63] : '=');
    put(i + 2 < length ? kAlphabet[chunk & 63] : '=');
  }
  if (size) out[used] = 0;
}
// Meshtastic's default channel key and the simple variants of it, which every radio knows. A
// channel with one of them isn't private, whatever it's called.
inline bool wellKnownKey(const uint8_t* key, uint8_t length) {
  static const uint8_t kDefault[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                       0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
  if (length != 16 || memcmp(key, kDefault, 15) != 0) return false;
  const uint8_t last = key[15];
  return last >= kDefault[15] && last < kDefault[15] + 10;  // Default keys 1 to 10.
}
// A new 32-byte key from the caller's random bytes. All zero or all one byte is refused, because a
// broken random source gives exactly that.
inline bool newKey(void (*randomBytes)(uint8_t*, size_t), uint8_t* out, uint8_t& length) {
  length = 0;
  uint8_t key[kChannelKeyMax];
  randomBytes(key, sizeof(key));
  bool varied = false;
  for (size_t i = 1; i < sizeof(key); ++i) varied = varied || key[i] != key[0];
  if (!varied) return false;
  memcpy(out, key, sizeof(key));
  length = sizeof(key);
  return true;
}

// How it's saved: name, length, key. Anything that doesn't read back whole is no channel.
constexpr size_t kChannelSavedBytes = kChannelNameMax + 1 + 1 + kChannelKeyMax;
inline void saveChannel(const PrivateChannel& channel, uint8_t* out) {
  memset(out, 0, kChannelSavedBytes);
  memcpy(out, channel.name, kChannelNameMax + 1);
  out[kChannelNameMax + 1] = channel.keyLength;
  memcpy(out + kChannelNameMax + 2, channel.key, kChannelKeyMax);
}
inline bool loadChannel(const uint8_t* in, size_t length, PrivateChannel& channel) {
  channel = PrivateChannel{};
  if (!in || length != kChannelSavedBytes) return false;
  PrivateChannel read;
  memcpy(read.name, in, kChannelNameMax + 1);
  read.name[kChannelNameMax] = 0;
  read.keyLength = in[kChannelNameMax + 1];
  memcpy(read.key, in + kChannelNameMax + 2, kChannelKeyMax);
  if (!validChannelName(read.name) || (read.keyLength != 16 && read.keyLength != 32)) return false;
  channel = read;
  return true;
}
}  // namespace s1_mesh
