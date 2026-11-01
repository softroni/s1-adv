#!/usr/bin/env python3
"""Run the saved Wi-Fi store on the host: damaged cards, power cuts and the real cipher code.

The firmware's WifiCipher and serviceWifiMemory are taken from the source. AES-256-GCM and
SHA-256 come from OpenSSL behind the two mbedTLS calls the firmware makes, because the device's
mbedTLS only exists as an Xtensa library. This doesn't show a real SD card behaving.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
app = (src / 'main_parts/079_main.cpp.inc').read_text()


def function(source, signature):
    start = source.index(signature)
    i = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[start:i]


cipher = app[app.index('struct WifiCipher {'):app.index('WifiSdDisk gWifiDisk;')]
memory = function(app, 'void serviceWifiMemory()')

store_tests = r'''
#include "wifi_store.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
using namespace wifi_store;
using Bytes = std::vector<uint8_t>;

// Reversible but not secret: this half is about what happens to the file.
struct FakeCipher {
  bool failSeal = false;
  uint8_t key = 0x5A;
  bool seal(const Bytes& plain, Bytes& sealed) {
    if (failSeal) return false;
    uint8_t sum = key;
    sealed.clear();
    for (uint8_t byte : plain) { sealed.push_back(byte ^ key); sum += byte; }
    sealed.push_back(sum);
    return true;
  }
  bool open(const Bytes& sealed, Bytes& plain) {
    if (sealed.empty()) return false;
    uint8_t sum = key;
    plain.clear();
    for (size_t i = 0; i + 1 < sealed.size(); ++i) { plain.push_back(sealed[i] ^ key); sum += plain.back(); }
    return sum == sealed.back();
  }
};

// Power goes after `budget` more operations. A write that it interrupts leaves half a file.
struct FakeDisk {
  std::map<std::string, Bytes> files;
  long budget = -1;
  bool dead = false, garbleReadBack = false;
  bool up() {
    if (dead) return false;
    if (budget == 0) { dead = true; return false; }
    if (budget > 0) --budget;
    return true;
  }
  bool exists(const char* path) { return up() && files.count(path); }
  bool read(const char* path, Bytes& out) {
    if (!up() || !files.count(path)) return false;
    out = files[path];
    if (garbleReadBack && std::string(path) == kNewPath) out[out.size() / 2] ^= 1;
    return true;
  }
  bool write(const char* path, const Bytes& bytes) {
    if (dead) return false;
    if (budget == 0) {
      dead = true;
      files[path] = Bytes(bytes.begin(), bytes.begin() + bytes.size() / 2);
      return false;
    }
    if (budget > 0) --budget;
    files[path] = bytes;
    return true;
  }
  bool remove(const char* path) { if (!up()) return false; return files.erase(path) > 0; }
  bool rename(const char* from, const char* to) {
    if (!up() || !files.count(from) || files.count(to)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
};

bool same(const List& a, const List& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].ssid != b[i].ssid || a[i].password != b[i].password) return false;
  }
  return true;
}

void listing() {
  List list;
  assert(remember(list, "Home", "homepassword") && list.size() == 1);
  assert(!remember(list, "Home", "homepassword"));
  assert(remember(list, "Cafe", "espresso1") && list[0].ssid == "Cafe" && list[1].ssid == "Home");
  assert(remember(list, "Home", "homepassword") && list[0].ssid == "Home" && list.size() == 2);
  assert(remember(list, "Home", "newpassword") && list[0].password == "newpassword" && list.size() == 2);
  assert(find(list, "Cafe") && !find(list, "Other"));
  assert(!forget(list, "Other") && list.size() == 2);
  assert(forget(list, "Home") && list.size() == 1 && list[0].ssid == "Cafe");
  list.clear();
  for (size_t i = 0; i < kMaxSaved + 5; ++i) remember(list, "Net" + std::to_string(i), "password");
  assert(list.size() == kMaxSaved && list[0].ssid == "Net24" && !find(list, "Net0"));
  std::string secret = "topsecret";
  wipe(secret);
  assert(secret.empty());
  std::cout << "PASS the list: most recent first, updated in place, bounded, forget removes one\n";
}

void parsing() {
  List in, out;
  in.push_back({std::string(32, 'n'), std::string(64, 'p')});
  in.push_back({"Open", ""});
  in.push_back({"Cafe Wi\xC3\xA9", "pass word`~"});
  Bytes bytes = encode(in);
  assert(decode(bytes, out) && same(in, out));
  assert(decode(encode({}), out) && out.empty());
  for (size_t cut = 0; cut < bytes.size(); ++cut) {
    assert(!decode(Bytes(bytes.begin(), bytes.begin() + cut), out) && out.empty());
  }
  Bytes extra = bytes;
  extra.push_back(0);
  assert(!decode(extra, out));
  assert(!decode(Bytes{uint8_t(kMaxSaved + 1)}, out));
  assert(!decode(Bytes{1, 0, 0}, out));                        // No name.
  Bytes longName{1, 33};
  longName.insert(longName.end(), 33, 'n');
  longName.push_back(0);
  assert(!decode(longName, out));
  Bytes longPassword{1, 1, 'n', 65};
  longPassword.insert(longPassword.end(), 65, 'p');
  assert(!decode(longPassword, out));
  srand(7);
  for (int run = 0; run < 200000; ++run) {
    Bytes noise(rand() % 64);
    for (auto& byte : noise) byte = rand() % 4 ? rand() % 6 : rand();
    decode(noise, out);
  }
  std::cout << "PASS parsing: round trips, and every cut, oversize and 200000 noise inputs are safe\n";
}

void saving() {
  FakeDisk disk;
  FakeCipher cipher;
  List list{{"Home", "homepassword"}, {"Cafe", "espresso1"}}, out;
  assert(load(disk, cipher, out) == Load::Empty && out.empty());
  assert(store(disk, cipher, list));
  assert(disk.files.size() == 1 && disk.files.count(kPath));
  std::string file(disk.files[kPath].begin(), disk.files[kPath].end());
  assert(file.compare(0, 4, "S1W1") == 0 && file.find("homepassword") == std::string::npos);
  assert(load(disk, cipher, out) == Load::Ok && same(out, list));

  // Forgetting the last network is a normal save: no name or password is left in the file.
  assert(store(disk, cipher, {}));
  assert(disk.files.size() == 1 && disk.files[kPath].size() == 4 + 1 + 1);
  assert(load(disk, cipher, out) == Load::Ok && out.empty());

  // A failed seal or a bad read-back leaves the old file alone.
  assert(store(disk, cipher, list));
  Bytes good = disk.files[kPath];
  cipher.failSeal = true;
  assert(!store(disk, cipher, {{"New", "newpassword"}}) && disk.files[kPath] == good);
  cipher.failSeal = false;
  disk.garbleReadBack = true;
  assert(!store(disk, cipher, {{"New", "newpassword"}}));
  assert(disk.files[kPath] == good && !disk.files.count(kNewPath));
  std::cout << "PASS saving: nothing readable on the card, and a failed save keeps the old list\n";
}

void powerCuts() {
  const List before{{"Home", "homepassword"}, {"Cafe", "espresso1"}};
  for (const List& after : {List{{"Cafe", "espresso1"}, {"Home", "homepassword"}, {"New", "password1"}},
                            List{{"Home", "homepassword"}}, List{}}) {
    int cuts = 0;
    for (long step = 0;; ++step) {
      FakeDisk disk;
      FakeCipher cipher;
      assert(store(disk, cipher, before));
      disk.budget = step;
      const bool finished = store(disk, cipher, after);
      const bool cut = disk.dead;
      disk.budget = -1;
      disk.dead = false;
      List out;
      const Load result = load(disk, cipher, out);
      assert(result != Load::Unreadable);
      assert(same(out, before) || same(out, after));  // Whole, one or the other. Never a mix.
      if (finished) assert(same(out, after));
      List again;
      assert(load(disk, cipher, again) != Load::Unreadable && same(again, out));  // And it's settled.
      if (!cut) break;
      ++cuts;
    }
    assert(cuts >= 3);
  }
  std::cout << "PASS a power cut at every disk step leaves the old list or the new one, never less\n";
}

void damage() {
  const List list{{"Home", "homepassword"}, {"Cafe", "espresso1"}};
  FakeCipher cipher;
  FakeDisk clean;
  assert(store(clean, cipher, list));
  const Bytes good = clean.files[kPath];
  for (size_t at = 0; at < good.size(); ++at) {
    FakeDisk disk;
    disk.files[kPath] = good;
    disk.files[kPath][at] ^= 0x20;
    List out;
    const Load result = load(disk, cipher, out);
    assert(result == Load::Unreadable && out.empty());
    assert(disk.files.count(kBadPath) && disk.files[kBadPath][at] == uint8_t(good[at] ^ 0x20));
    assert(!disk.files.count(kPath));
    // The next save starts a fresh list without touching what was set aside.
    assert(store(disk, cipher, {{"New", "newpassword"}}));
    assert(disk.files.count(kBadPath) && load(disk, cipher, out) == Load::Ok && out.size() == 1);
  }
  FakeDisk truncated;
  truncated.files[kPath] = Bytes(good.begin(), good.begin() + 3);
  List out;
  assert(load(truncated, cipher, out) == Load::Unreadable);
  FakeDisk huge;
  huge.files[kPath] = Bytes(good.size(), 0);
  assert(load(huge, cipher, out) == Load::Unreadable);

  // A file sealed by another Cardputer.
  FakeCipher other;
  other.key = 0x11;
  FakeDisk foreign;
  foreign.files[kPath] = good;
  assert(load(foreign, other, out) == Load::Unreadable && out.empty());
  std::cout << "PASS a damaged or foreign file is refused, set aside, and doesn't block saving\n";
}

int main() {
  listing();
  parsing();
  saving();
  powerCuts();
  damage();
}
'''

cipher_tests = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <openssl/evp.h>
#include "wifi_store.h"
using Bytes = std::vector<uint8_t>;

// The mbedTLS calls the firmware makes, with their argument order, on OpenSSL.
#define MBEDTLS_GCM_ENCRYPT 1
#define MBEDTLS_CIPHER_ID_AES 2
struct mbedtls_gcm_context { unsigned char key[32]; bool ready; };
void mbedtls_gcm_init(mbedtls_gcm_context* c) { memset(c, 0, sizeof(*c)); }
void mbedtls_gcm_free(mbedtls_gcm_context* c) { memset(c, 0, sizeof(*c)); }
int mbedtls_gcm_setkey(mbedtls_gcm_context* c, int cipher, const unsigned char* key, unsigned bits) {
  assert(cipher == MBEDTLS_CIPHER_ID_AES && bits == 256);
  memcpy(c->key, key, 32);
  c->ready = true;
  return 0;
}
int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context* g, int mode, size_t length, const unsigned char* iv,
                              size_t ivLen, const unsigned char* add, size_t addLen,
                              const unsigned char* input, unsigned char* output, size_t tagLen,
                              unsigned char* tag) {
  assert(g->ready && mode == MBEDTLS_GCM_ENCRYPT);
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int n = 0;
  EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, int(ivLen), nullptr);
  EVP_EncryptInit_ex(c, nullptr, nullptr, g->key, iv);
  EVP_EncryptUpdate(c, nullptr, &n, add, int(addLen));
  if (length) EVP_EncryptUpdate(c, output, &n, input, int(length));
  EVP_EncryptFinal_ex(c, output + n, &n);
  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, int(tagLen), tag);
  EVP_CIPHER_CTX_free(c);
  return 0;
}
int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context* g, size_t length, const unsigned char* iv, size_t ivLen,
                             const unsigned char* add, size_t addLen, const unsigned char* tag,
                             size_t tagLen, const unsigned char* input, unsigned char* output) {
  assert(g->ready);
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int n = 0;
  EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, int(ivLen), nullptr);
  EVP_DecryptInit_ex(c, nullptr, nullptr, g->key, iv);
  EVP_DecryptUpdate(c, nullptr, &n, add, int(addLen));
  if (length) EVP_DecryptUpdate(c, output, &n, input, int(length));
  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, int(tagLen), const_cast<unsigned char*>(tag));
  const int ok = EVP_DecryptFinal_ex(c, output + n, &n);
  EVP_CIPHER_CTX_free(c);
  return ok > 0 ? 0 : -0x12;
}
struct mbedtls_sha256_context { EVP_MD_CTX* md; };
void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->md = EVP_MD_CTX_new(); }
void mbedtls_sha256_free(mbedtls_sha256_context* c) { EVP_MD_CTX_free(c->md); }
void mbedtls_sha256_starts(mbedtls_sha256_context* c, int is224) { assert(!is224); EVP_DigestInit_ex(c->md, EVP_sha256(), nullptr); }
void mbedtls_sha256_update(mbedtls_sha256_context* c, const unsigned char* d, size_t n) { EVP_DigestUpdate(c->md, d, n); }
void mbedtls_sha256_finish(mbedtls_sha256_context* c, unsigned char* out) { unsigned n = 0; EVP_DigestFinal_ex(c->md, out, &n); assert(n == 32); }

// The chip.
const void* ESP_EFUSE_OPTIONAL_UNIQUE_ID[1];
uint8_t gId[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
uint8_t gMac[6] = {0xAA, 0xBB, 0xCC, 1, 2, 3};
int esp_efuse_read_field_blob(const void*, void* out, size_t bits) { assert(bits == 128); memcpy(out, gId, 16); return 0; }
int esp_efuse_mac_get_default(uint8_t* out) { memcpy(out, gMac, 6); return 0; }
void esp_fill_random(void* out, size_t size) { arc4random_buf(out, size); }
''' + cipher + r'''
Bytes directSeal(const uint8_t key[32], const Bytes& plain) {
  Bytes sealed(28 + plain.size());
  arc4random_buf(sealed.data(), 12);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, plain.size(), sealed.data(), 12, wifi_store::kMagic, 4,
                            plain.data(), sealed.data() + 28, 16, sealed.data() + 12);
  mbedtls_gcm_free(&g);
  return sealed;
}

int main() {
  // NIST AES-256-GCM test case 16, so the rest of this proves something.
  static const uint8_t K[32] = {0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,0x6d,0x6a,0x8f,0x94,0x67,0x30,0x83,0x08,
                                0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,0x6d,0x6a,0x8f,0x94,0x67,0x30,0x83,0x08};
  static const uint8_t IV[12] = {0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,0xde,0xca,0xf8,0x88};
  static const uint8_t A[20] = {0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xab,0xad,0xda,0xd2};
  static const uint8_t P[60] = {0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
                                0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
                                0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
                                0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,0xba,0x63,0x7b,0x39};
  static const uint8_t T[16] = {0x76,0xfc,0x6e,0xce,0x0f,0x4e,0x17,0x68,0xcd,0xdf,0x88,0x53,0xbb,0x2d,0x55,0x1b};
  uint8_t ct[60], tag[16];
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, K, 256);
  assert(!mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, 60, IV, 12, A, 20, P, ct, 16, tag));
  assert(!memcmp(tag, T, 16));
  std::cout << "PASS AES-256-GCM matches the NIST vector\n";

  WifiCipher cipher;
  uint8_t key[32];
  cipher.key(key);
  const Bytes plain = wifi_store::encode({{"Home", "correct horse"}, {"Cafe", "espresso1"}, {"Open", ""}});
  Bytes sealed, back;
  assert(cipher.seal(plain, sealed) && sealed.size() == plain.size() + 28);
  const std::string text(sealed.begin(), sealed.end());
  assert(text.find("correct horse") == std::string::npos && text.find("Home") == std::string::npos);
  assert(cipher.open(sealed, back) && back == plain);
  Bytes opened;
  assert(cipher.open(directSeal(key, plain), opened) && opened == plain);
  std::cout << "PASS the production cipher round trips and hides names and passwords\n";

  std::set<Bytes> nonces;
  for (int i = 0; i < 2000; ++i) {
    Bytes s;
    assert(cipher.seal(plain, s));
    nonces.insert(Bytes(s.begin(), s.begin() + 12));
  }
  assert(nonces.size() == 2000);
  for (size_t at = 0; at < sealed.size(); ++at) {
    Bytes bad = sealed, out{1, 2, 3};
    bad[at] ^= 0x40;
    assert(!cipher.open(bad, out) && out.empty());
  }
  for (size_t size = 0; size < sealed.size(); ++size) {
    Bytes out;
    assert(!cipher.open(Bytes(sealed.begin(), sealed.begin() + size), out));
  }
  std::cout << "PASS 2000 seals use 2000 nonces, and every tamper and truncation is refused\n";

  Bytes out;
  gId[3] ^= 1;
  assert(!cipher.open(sealed, out));
  gId[3] ^= 1;
  gMac[5] ^= 1;
  assert(!cipher.open(sealed, out));
  gMac[5] ^= 1;
  assert(cipher.open(sealed, out));
  std::cout << "PASS the key belongs to this chip: another ID or MAC can't open the file\n";
}
'''

memory_tests = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include "wifi_store.h"
struct String {
  std::string v;
  String(const char* s = "") : v(s) {}
  const char* c_str() const { return v.c_str(); }
  bool isEmpty() const { return v.empty(); }
  bool operator==(const String& o) const { return v == o.v; }
  bool operator!=(const String& o) const { return v != o.v; }
};
constexpr int WL_CONNECTED = 3;
struct Wifi { int state = WL_CONNECTED; String ssid; int status() { return state; } String SSID() { return ssid; } } WiFi;
String gWifiSsid, gWifiPassword, gWifiRememberedSsid;
bool gWifiReady = true, gSdReady = true, gBootBackgroundActive = false, gAllowed = true, gIdle = true, gMida = false;
uint32_t gNow = 1000;
uint32_t millis() { return gNow; }
bool appAllowsBackgroundNetwork() { return gAllowed; }
bool networkWorkIdle() { return gIdle; }
bool usingTemporaryMidaWifi() { return gMida; }

struct Card {
  wifi_store::List list;
  bool failWrite = false;
  int loads = 0, writes = 0;
} gCard;
struct Disk {} gWifiDisk;
struct Cipher {} gWifiCipher;
namespace wifi_store {
wifi_store::Load load(Disk&, Cipher&, List& out) { ++gCard.loads; out = gCard.list; return out.empty() ? Load::Empty : Load::Ok; }
bool store(Disk&, Cipher&, const List& list) { ++gCard.writes; if (gCard.failWrite) return false; gCard.list = list; return true; }
}
''' + memory + r'''
void reset(const char* ssid, const char* password) {
  gCard = Card{};
  gWifiSsid = String(ssid);
  gWifiPassword = String(password);
  gWifiRememberedSsid = String("");
  WiFi = Wifi{};
  WiFi.ssid = String(ssid);
  gWifiReady = gSdReady = gAllowed = gIdle = true;
  gBootBackgroundActive = gMida = false;
}
int main() {
  // A network that arrived some other way, the setup portal included, joins the list.
  reset("Portal Net", "portalpassword");
  serviceWifiMemory();
  assert(gCard.list.size() == 1 && gCard.list[0].ssid == "Portal Net");
  assert(gCard.list[0].password == "portalpassword");
  assert(gWifiRememberedSsid == String("Portal Net"));
  serviceWifiMemory();
  serviceWifiMemory();
  assert(gCard.loads == 1 && gCard.writes == 1);  // Once, not on every loop.

  // It goes to the top of what's already there, and an unchanged list isn't rewritten.
  reset("Cafe", "espresso1");
  gCard.list = {{"Home", "homepassword"}};
  serviceWifiMemory();
  assert(gCard.list.size() == 2 && gCard.list[0].ssid == "Cafe" && gCard.list[1].ssid == "Home");
  reset("Home", "homepassword");
  gCard.list = {{"Home", "homepassword"}};
  serviceWifiMemory();
  assert(gCard.writes == 0 && gWifiRememberedSsid == String("Home"));

  // Only a real connection to that network counts, and only at a quiet moment.
  for (int blocked = 0; blocked < 10; ++blocked) {
    reset("Cafe", "espresso1");
    switch (blocked) {
      case 0: WiFi.state = 0; break;
      case 1: WiFi.ssid = String("Elsewhere"); break;
      case 2: gWifiReady = false; break;
      case 3: gSdReady = false; break;
      case 4: gBootBackgroundActive = true; break;
      case 5: gAllowed = false; break;
      case 6: gIdle = false; break;
      case 7: gMida = true; break;
      case 8: gWifiSsid = String(""); break;
      case 9: gWifiRememberedSsid = String("Cafe"); break;
    }
    serviceWifiMemory();
    assert(gCard.loads == 0 && gCard.writes == 0);
  }

  // A card that refuses is retried a minute later, not on every loop.
  reset("Cafe", "espresso1");
  gCard.failWrite = true;
  serviceWifiMemory();
  assert(gCard.writes == 1 && gWifiRememberedSsid == String(""));
  gNow += 59999;
  serviceWifiMemory();
  assert(gCard.writes == 1);
  gNow += 1;
  gCard.failWrite = false;
  serviceWifiMemory();
  assert(gCard.writes == 2 && gCard.list.size() == 1 && gWifiRememberedSsid == String("Cafe"));
  std::cout << "PASS real serviceWifiMemory: any connected network is remembered once, at a quiet moment\n";
}
'''


def openssl_flags():
    for prefix in ('/opt/homebrew/opt/openssl@3', '/usr/local/opt/openssl@3'):
        if Path(prefix, 'include/openssl/evp.h').exists():
            return [f'-I{prefix}/include', f'-L{prefix}/lib', '-lcrypto']
    return ['-lcrypto']


compiler = os.environ.get('CXX', 'c++')
flags = ['-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
         '-fsanitize=address,undefined', f'-I{src}']
with tempfile.TemporaryDirectory() as tmp:
    for name, text, extra in [('store', store_tests, []), ('cipher', cipher_tests, openssl_flags()),
                              ('memory', memory_tests, [])]:
        source = Path(tmp) / f'{name}.cpp'
        binary = Path(tmp) / name
        source.write_text(text)
        subprocess.run([compiler, *flags, str(source), '-o', str(binary), *extra], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout, end='')
        if result.returncode != 0:
            raise SystemExit(result.stderr)
print('PASS saved Wi-Fi store')
