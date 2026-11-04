#!/usr/bin/env python3
"""Pass real Meshtastic packets between three copies of the patched libmeshtastic-leaf.

It builds the library in bots/CardputerADV/lib with RadioLib's generic build, Nanopb and Crypto
from the firmware build's downloads, so it runs after a firmware build (CI does). The radio is a
fake that keeps what it sends; the crypto, framing and channel code are the real ones.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LEAF = ROOT / 'bots/CardputerADV/lib/libmeshtastic-leaf/src'
deps = sorted((ROOT / 'bots/CardputerADV/.pio/libdeps').glob('*/RadioLib'))
if not deps:
    sys.exit('RadioLib not found: build the firmware first (python scripts/cardputer.py build)')
DEPS = deps[0].parent
for name in ('Nanopb', 'Crypto', 'RadioLib'):
    if not (DEPS / name).exists():
        sys.exit(f'{name} not found in {DEPS}: build the firmware first')

ARDUINO = r'''#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
uint32_t millis();
uint32_t micros();
inline void delay(uint32_t) {}
inline void yield() {}
'''
RNG = r'''#include <RNG.h>
RNGClass CryptRNG;
static uint32_t s = 12345;
RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}
void RNGClass::begin(const char*) {}
void RNGClass::rand(uint8_t* d, size_t n) { while (n--) { s = s * 1103515245u + 12345u; *d++ = uint8_t(s >> 16); } }
void RNGClass::stir(const uint8_t*, size_t, unsigned int) {}
bool RNGClass::available(size_t) const { return true; }
void RNGClass::loop() {}
'''
CODE = r'''#include <libmeshtastic_leaf.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using libmeshtastic_leaf::MeshPacket; using libmeshtastic_leaf::MeshConfig; using libmeshtastic_leaf::MeshCryptoPKI; using libmeshtastic_leaf::REGION_US; using libmeshtastic_leaf::PRESET_LONG_FAST;
uint32_t gNow = 1000;
uint32_t millis() { return gNow; }
uint32_t micros() { return gNow * 1000; }
// A radio that keeps what it sends and hands over what it's given.
struct FakePhy : PhysicalLayer {
  std::vector<std::vector<uint8_t>> sent;
  std::vector<uint8_t> incoming;
  void (*irq)(void) = nullptr;
  int16_t sleep() override { return 0; }
  int16_t startReceive() override { return 0; }
  int16_t startTransmit(const uint8_t* d, size_t n, uint8_t) override { sent.emplace_back(d, d + n); return 0; }
  int16_t finishTransmit() override { return 0; }
  int16_t readData(uint8_t* d, size_t n) override { memcpy(d, incoming.data(), n < incoming.size() ? n : incoming.size()); return 0; }
  int16_t setFrequency(float) override { return 0; }
  int16_t setOutputPower(int8_t) override { return 0; }
  int16_t checkOutputPower(int8_t p, int8_t* c) override { if (c) *c = p; return 0; }
  int16_t setSyncWord(uint8_t*, size_t) override { return 0; }
  int16_t setPreambleLength(size_t) override { return 0; }
  int16_t setDataRate(DataRate_t, ModemType_t) override { return 0; }
  size_t getPacketLength(bool) override { return incoming.size(); }
  float getRSSI() override { return -90; }
  float getSNR() override { return 5; }
  RadioLibTime_t getTimeOnAir(size_t) override { return 100000; }
  int16_t scanChannel() override { return RADIOLIB_CHANNEL_FREE; }
  uint8_t randomByte() override { static uint8_t r = 7; return r += 31; }
  void setPacketReceivedAction(void (*f)(void)) override { irq = f; }
  void clearPacketReceivedAction() override { irq = nullptr; }
 protected:
  Module* getMod() override { return nullptr; }
};
uint8_t gKeys[4][32];
bool lookup(uint32_t node, uint8_t out[32]);
struct Node {
  uint8_t pub[32] = {};
  FakePhy phy; libmeshtastic_leaf::libmeshtastic_leaf mesh;
  explicit Node(uint8_t seed) {
    uint8_t priv[32]; for (int i = 0; i < 32; ++i) priv[i] = uint8_t(seed * 7 + i);
    priv[0] &= 248; priv[31] = (priv[31] & 127) | 64; MeshCryptoPKI::regeneratePublicKey(pub, priv);
    mesh.setMyPublicKey(pub); mesh.setMyPrivateKey(priv);
    MeshConfig c; c.radio.region = REGION_US; c.radio.preset = PRESET_LONG_FAST; c.radio.txPower = 10;
    assert(mesh.begin(c, &phy)); mesh.setNodeInfoInterval(0); mesh.setCarrierSense(false);
    memcpy(gKeys[seed], pub, 32); mesh.onReceivePKI(lookup);
  }
};
Node* gNodes[4] = {};
bool lookup(uint32_t node, uint8_t out[32]) {
  for (int i = 1; i < 4; ++i) if (gNodes[i] && gNodes[i]->mesh.getNodeNum() == node) { memcpy(out, gKeys[i], 32); return true; }
  return false;
}
std::vector<MeshPacket> got;
void onPacket(const MeshPacket& p) { got.push_back(p); }
// Sends whatever the node queued, and returns the frame that went on the air.
std::vector<uint8_t> air(Node& n) {
  const size_t before = n.phy.sent.size();
  for (int i = 0; i < 200 && n.phy.sent.size() == before; ++i) { gNow += 50; n.mesh.update(); }
  assert(n.phy.sent.size() == before + 1);
  n.phy.irq(); gNow += 10; n.mesh.update();  // Transmit done.
  return n.phy.sent.back();
}
void hear(Node& n, const std::vector<uint8_t>& frame) { n.phy.incoming = frame; n.phy.irq(); gNow += 10; n.mesh.update(); }
std::string text(const MeshPacket& p) { return std::string(reinterpret_cast<const char*>(p.payload), p.payloadLen); }
int main() {
  Node a(1), b(2), c(3); gNodes[1] = &a; gNodes[2] = &b; gNodes[3] = &c;
  b.mesh.onReceive(onPacket); c.mesh.onReceive(onPacket);
  uint8_t key[32]; for (int i = 0; i < 32; ++i) key[i] = uint8_t(0xA0 + i);
  // Refused: no name, a 1-byte or odd-length key.
  assert(!a.mesh.setSecondaryChannel(key, 32, "") && !a.mesh.hasSecondaryChannel());
  assert(!a.mesh.setSecondaryChannel(key, 1, "Family") && !a.mesh.setSecondaryChannel(key, 24, "Family") && !a.mesh.setSecondaryChannel(nullptr, 32, "Family"));
  assert(a.mesh.sendTextOnSecondary("nope") == 0);
  assert(a.mesh.setSecondaryChannel(key, 32, "Family") && b.mesh.setSecondaryChannel(key, 32, "Family"));
  assert(a.mesh.getSecondaryChannelHash() == b.mesh.getSecondaryChannelHash() && a.mesh.getSecondaryChannelHash() != a.mesh.getChannelHash());
  // Public text still works, and arrives as channel 0.
  assert(a.mesh.sendText("hello public")); auto f = air(a);
  assert(f[13] == a.mesh.getChannelHash());
  got.clear(); hear(b, f); assert(got.size() == 1 && text(got[0]) == "hello public" && got[0].channelIndex == 0);
  got.clear(); hear(c, f); assert(got.size() == 1 && got[0].channelIndex == 0);  // c has no secondary and hears public anyway.
  // Private text: the secondary's hash on the air, channel 1 for a node with the key, nothing for one without.
  assert(a.mesh.sendTextOnSecondary("hello family")); f = air(a);
  assert(f[13] == a.mesh.getSecondaryChannelHash());
  for (size_t i = 16; i + 12 <= f.size(); ++i) assert(memcmp(&f[i], "hello family", 12) != 0);  // Encrypted on the air.
  got.clear(); hear(b, f); assert(got.size() == 1 && text(got[0]) == "hello family" && got[0].channelIndex == 1);
  got.clear(); hear(c, f); assert(got.empty());
  // After a private send, the next public send is public again.
  assert(a.mesh.sendText("public again")); f = air(a); assert(f[13] == a.mesh.getChannelHash());
  got.clear(); hear(b, f); assert(got.size() == 1 && got[0].channelIndex == 0);
  // The same name with another key is another channel: not readable.
  uint8_t other[32]; memcpy(other, key, 32); other[5] ^= 1;
  assert(c.mesh.setSecondaryChannel(other, 32, "Family"));
  assert(a.mesh.sendTextOnSecondary("still private")); f = air(a);
  got.clear(); hear(c, f); assert(got.empty() || text(got[0]) != "still private");
  // A 16-byte key works too, and clearing the secondary stops both directions.
  uint8_t shortKey[16]; for (int i = 0; i < 16; ++i) shortKey[i] = uint8_t(0x30 + i);
  assert(a.mesh.setSecondaryChannel(shortKey, 16, "Club") && b.mesh.setSecondaryChannel(shortKey, 16, "Club"));
  assert(a.mesh.sendTextOnSecondary("club news")); f = air(a);
  got.clear(); hear(b, f); assert(got.size() == 1 && text(got[0]) == "club news" && got[0].channelIndex == 1);
  b.mesh.clearSecondaryChannel(); assert(!b.mesh.hasSecondaryChannel());
  assert(a.mesh.sendTextOnSecondary("club again")); f = air(a); got.clear(); hear(b, f); assert(got.empty());
  a.mesh.clearSecondaryChannel(); assert(a.mesh.sendTextOnSecondary("x") == 0);
  // A direct message still works with a secondary set on both ends, and isn't taken for either channel.
  assert(a.mesh.setSecondaryChannel(key, 32, "Family") && b.mesh.setSecondaryChannel(key, 32, "Family"));
  assert(a.mesh.sendTextPKI("just you", b.mesh.getNodeNum(), b.pub)); f = air(a);
  got.clear(); hear(b, f); assert(got.size() == 1 && text(got[0]) == "just you" && got[0].isPKI);
  got.clear(); hear(c, f); assert(got.empty());
  puts("PASS");
}
'''

with tempfile.TemporaryDirectory(prefix='s1-mesh-channels-') as directory:
    p = Path(directory)
    (p / 'fake').mkdir()
    (p / 'fake' / 'Arduino.h').write_text(ARDUINO)
    (p / 'rng.cpp').write_text(RNG)
    (p / 'loop.cpp').write_text(CODE)
    include = ['-I' + str(p / 'fake'), '-I' + str(LEAF), '-I' + str(LEAF / 'generated'), '-I' + str(DEPS / 'Nanopb'),
               '-I' + str(DEPS / 'Crypto'), '-I' + str(DEPS / 'RadioLib/src')]
    c_sources = [DEPS / 'Nanopb' / f for f in ('pb_common.c', 'pb_encode.c', 'pb_decode.c')]
    cpp_sources = [LEAF / f for f in ('libmeshtastic_leaf.cpp', 'MeshPacket.cpp', 'MeshPayload.cpp', 'MeshChannel.cpp',
                                      'MeshCrypto.cpp', 'MeshCryptoPKI.cpp', 'third_party/aes-ccm.cpp')]
    cpp_sources += sorted((LEAF / 'generated/meshtastic').glob('*.cpp'))
    cpp_sources += [f for f in sorted((DEPS / 'Crypto').glob('*.cpp')) if f.name not in ('AESEsp32.cpp', 'RNG.cpp', 'NoiseSource.cpp')]
    radio = DEPS / 'RadioLib/src'
    cpp_sources += [radio / 'protocols/PhysicalLayer/PhysicalLayer.cpp', radio / 'Module.cpp', radio / 'Hal.cpp']
    cpp_sources += sorted((radio / 'utils').glob('*.cpp'))
    cpp_sources += [p / 'rng.cpp', p / 'loop.cpp']
    objects = []
    for i, source in enumerate(c_sources + cpp_sources):
        out = p / f'{i}.o'
        if source.suffix == '.c':
            command = [os.environ.get('CC', 'cc'), '-c', '-w', *include, str(source), '-o', str(out)]
        else:
            command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-w', '-c', '-include', str(p / 'fake' / 'Arduino.h'),
                       *include, str(source), '-o', str(out)]
        subprocess.run(command, check=True)
        objects.append(str(out))
    subprocess.run([os.environ.get('CXX', 'c++'), *objects, '-o', str(p / 'loop')], check=True)
    result = subprocess.run([str(p / 'loop')], check=True, capture_output=True, text=True)
    assert result.stdout.strip() == 'PASS', result.stdout
print('PASS Mesh channels, real library: public as channel 0, private encrypted on the air and channel 1 only with its '
      'key, a wrong key unreadable, 16 and 32 byte keys, clearing it, public again after private, and a direct message alongside')
