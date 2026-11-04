#!/usr/bin/env python3
"""Check mesh_private_channel.h: names, keys as base64 both ways, new keys, and what's saved."""
import base64
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
rng = random.Random(7)
keys = [bytes(rng.randrange(256) for _ in range(n)) for n in (16, 32, 32, 16, 32)]
vectors = '\n'.join('{"%s",{%s},%d},' % (base64.b64encode(k).decode(), ','.join(str(b) for b in k), len(k)) for k in keys)
CODE = r'''
#include "mesh_private_channel.h"
#include <cassert>
#include <string>
using namespace s1_mesh;
struct Vector { const char* text; uint8_t key[32]; uint8_t length; };
const Vector kVectors[] = {
''' + vectors + r'''
};
static uint8_t gFill = 0; static bool gBroken = false;
void randomBytes(uint8_t* out, size_t n) { for (size_t i = 0; i < n; ++i) out[i] = gBroken ? 0x55 : uint8_t(gFill += 37); }
static bool parses(const char* text) { uint8_t k[32]; uint8_t n = 0; return keyFromBase64(text, k, n); }
int main() {
  // Names: what the Meshtastic app and the keyboard both handle, up to 11.
  for (const char* good : {"Family", "a", "Club 2", "hike-team", "x_y", "ElevenChars"}) assert(validChannelName(good));
  for (const char* bad : {"", " Family", "Family ", "TwelveCharss", "Fam!ly", "caf\xc3\xa9", "a/b"}) assert(!validChannelName(bad));
  assert(!validChannelName(nullptr));
  // Keys from Python's base64, both ways, with and without padding, URL-safe and spaced out.
  for (const Vector& v : kVectors) {
    uint8_t k[32]; uint8_t n = 0;
    assert(keyFromBase64(v.text, k, n) && n == v.length && memcmp(k, v.key, n) == 0);
    char text[kChannelKeyText]; keyToBase64(v.key, v.length, text, sizeof(text)); assert(std::string(text) == v.text);
    std::string bare(v.text); while (!bare.empty() && bare.back() == '=') bare.pop_back();
    assert(keyFromBase64(bare.c_str(), k, n) && n == v.length && memcmp(k, v.key, n) == 0);
    std::string spaced; for (size_t i = 0; i < bare.size(); ++i) { spaced += bare[i]; if (i % 4 == 3) spaced += ' '; }
    assert(keyFromBase64(spaced.c_str(), k, n) && n == v.length);
    std::string url(v.text); for (char& c : url) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    assert(keyFromBase64(url.c_str(), k, n) && memcmp(k, v.key, n) == 0);
  }
  // Not a private key: Meshtastic's one-byte shorthand, the wrong length, junk, or bits left over.
  for (const char* bad : {"", "AQ==", "AA==", "QUJDREVGR0hJSktMTU5PUFFSU1RVVldY", "1PG7OiApB1nwvP+rz05pAQ=x", "1PG7OiApB1nwvP!rz05pAQ==",
                          "1PG7OiApB1nwvP+rz05pAR==", "1PG7OiApB1nwvP+rz05pAQ===", "=1PG7OiApB1nwvP+rz05pAQ"}) assert(!parses(bad));
  assert(!parses(nullptr));
  // Meshtastic's default key parses, but is known to every radio, so it's not private. Nor are its variants.
  uint8_t k[32]; uint8_t n = 0; assert(keyFromBase64("1PG7OiApB1nwvP+rz05pAQ==", k, n) && n == 16 && wellKnownKey(k, n));
  k[15] += 9; assert(wellKnownKey(k, n)); k[15] += 1; assert(!wellKnownKey(k, n));
  for (const Vector& v : kVectors) assert(!wellKnownKey(v.key, v.length));
  // New keys are 32 bytes from the random source, and a stuck source makes none.
  uint8_t made[32]; uint8_t madeLength = 0; assert(newKey(randomBytes, made, madeLength) && madeLength == 32);
  uint8_t again[32]; uint8_t againLength = 0; assert(newKey(randomBytes, again, againLength) && memcmp(made, again, 32) != 0);
  gBroken = true; assert(!newKey(randomBytes, again, againLength) && againLength == 0); gBroken = false;
  char text[kChannelKeyText]; keyToBase64(made, 32, text, sizeof(text)); assert(strlen(text) == 44 && text[43] == '=' && parses(text));
  char small[8]; keyToBase64(made, 32, small, sizeof(small)); assert(strlen(small) == 7);  // Never past the buffer.
  // Saved and read back whole; anything damaged is no channel at all.
  PrivateChannel channel; strcpy(channel.name, "Family"); memcpy(channel.key, made, 32); channel.keyLength = 32; assert(channel.set());
  uint8_t saved[kChannelSavedBytes]; saveChannel(channel, saved);
  PrivateChannel read; assert(loadChannel(saved, sizeof(saved), read) && read.set() && std::string(read.name) == "Family" && memcmp(read.key, made, 32) == 0);
  assert(!loadChannel(saved, sizeof(saved) - 1, read) && !read.set());
  uint8_t bad[kChannelSavedBytes]; memcpy(bad, saved, sizeof(bad)); bad[kChannelNameMax + 1] = 24; assert(!loadChannel(bad, sizeof(bad), read) && !read.set());
  memcpy(bad, saved, sizeof(bad)); bad[0] = '!'; assert(!loadChannel(bad, sizeof(bad), read));
  memcpy(bad, saved, sizeof(bad)); memset(bad, 'A', kChannelNameMax + 1); assert(!loadChannel(bad, sizeof(bad), read) || strlen(read.name) <= kChannelNameMax);
  assert(!loadChannel(nullptr, kChannelSavedBytes, read) && !PrivateChannel{}.set());
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-private-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'bots/CardputerADV/src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS Mesh private channel: names, base64 keys both ways with and without padding, URL-safe and spaced, the '
      'public and broken keys refused, Meshtastic default keys known, new keys, and saving that refuses damage')
