#!/usr/bin/env python3
"""Render every Mesh sound with the firmware's own code and check it fits the shared buffer and the speaker.

MESH_SOUNDS_WAV=<folder> also writes each one as an 8 kHz WAV, to listen to on a computer.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
code = r'''
#include "mesh_sounds.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <tuple>
using namespace s1_mesh;
constexpr size_t kBuffer = 1920;  // release_delivery::kChimeSamples: the buffer part 090 plays from.
constexpr uint32_t kRate = 8000;
static int16_t buf[kBuffer + 64];
static const char* names[kSoundCount] = {"ready", "off", "sent", "heard", "delivered", "failed", "message", "group",
  "help-start", "help-round", "help-answered", "nearby", "gps-fix", "ask", "nope", "saved", "removed", "packet"};
static void wav(const char* dir, const char* name, const int16_t* s, size_t n) {
  std::string path = std::string(dir) + "/" + name + ".wav";
  FILE* f = fopen(path.c_str(), "wb"); assert(f);
  const uint32_t data = uint32_t(n * 2), riff = 36 + data, fmt = 16, rate = kRate, bytes = kRate * 2;
  const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&bytes, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fwrite(s, 2, n, f); fclose(f);
}
static int crossings(const int16_t* s, size_t n) { int c = 0; for (size_t i = 1; i < n; ++i) c += (s[i - 1] < 0) != (s[i] < 0); return c; }
int main() {
  const char* dir = getenv("MESH_SOUNDS_WAV");
  std::set<std::tuple<float, int, float, float, float, float, float, float, float, float>> shapes;
  for (uint8_t i = 0; i < kSoundCount; ++i) {
    const Sound sound = Sound(i);
    const SoundShape shape = soundShape(sound);
    // It fits the buffer, and a sentinel past its end is untouched.
    assert(shape.ms > 0 && shape.ms <= kSoundLongestMs);
    for (auto& s : buf) s = 0x5555;
    const size_t n = renderSound(sound, buf, kBuffer, kRate);
    assert(n == kRate * shape.ms / 1000 && n <= kBuffer && buf[n] == 0x5555);
    // Every note is one this speaker can play, below the 8 kHz rate's limit, and starts in the sound.
    assert(shape.motif.count >= 1 && shape.motif.count <= activity_cue::kMaxNotes);
    for (int k = 0; k < shape.motif.count; ++k) {
      assert(shape.motif.hz[k] >= 360 && shape.motif.hz[k] <= 2100 && shape.motif.gain[k] > 0 && shape.motif.gain[k] <= 1);
      assert(shape.motif.at[k] >= 0 && shape.motif.at[k] * 1000 < shape.ms - 30);
    }
    // Quiet enough beside speech, loud enough to hear, and it starts and ends without a click.
    int peak = 0; double energy = 0;
    for (size_t k = 0; k < n; ++k) { peak = std::max(peak, std::abs(int(buf[k]))); energy += double(buf[k]) * buf[k]; }
    const double rms = std::sqrt(energy / n);
    assert(peak <= 1.6f * shape.level && peak < 0.15 * 32767 && rms > 0.05 * shape.level);
    assert(buf[0] == 0 && std::abs(int(buf[n - 1])) <= shape.level / 40);
    // No two sound alike.
    const auto& m = shape.motif;
    assert(shapes.insert({m.ring, m.count, m.hz[0], m.hz[1], m.hz[2], m.hz[3], m.at[1], m.at[2], m.at[3], m.gain[1]}).second ||
           sound == Sound::Packet);
    if (dir) wav(dir, names[i], buf, n);
  }
  // A buffer smaller than the sound is never written past.
  for (auto& s : buf) s = 0x5555;
  assert(renderSound(Sound::Ready, buf, 100, kRate) == 100 && buf[100] == 0x5555);
  // What matters most cuts in over what matters less, and Radio Scan's ticks give way to everything.
  const auto p = [](Sound s) { return soundShape(s).priority; };
  assert(p(Sound::HelpStart) == p(Sound::HelpAnswered) && p(Sound::HelpAnswered) > p(Sound::Message));
  assert(p(Sound::Message) == p(Sound::Group) && p(Sound::Message) > p(Sound::Delivered) && p(Sound::Delivered) > p(Sound::Sent));
  assert(p(Sound::Ask) > p(Sound::Saved) && p(Sound::Nope) > p(Sound::Sent) && p(Sound::HelpRound) > p(Sound::Packet));
  for (uint8_t i = 0; i < kSoundCount; ++i) if (Sound(i) != Sound::Packet) assert(p(Sound(i)) > p(Sound::Packet));
  // Help and messages are the loud ones; the ticks are the soft ones.
  assert(soundShape(Sound::HelpStart).level == kSoundLoud && soundShape(Sound::Message).level == kSoundLoud);
  assert(soundShape(Sound::Packet).level == kSoundSoft && soundShape(Sound::HelpRound).level == kSoundSoft);
  // The scan tick: two octaves from -125 dBm to -55, rising all the way, and a missing reading is the lowest.
  assert(std::fabs(packetHz(-125) - 500) < 0.5f && std::fabs(packetHz(-55) - 2000) < 0.5f);
  assert(packetHz(INT16_MIN) == packetHz(-125) && packetHz(-20) == packetHz(-55));
  for (int d = -125; d < -55; ++d) assert(packetHz(int16_t(d + 1)) > packetHz(int16_t(d)));
  int16_t weak[kBuffer], strong[kBuffer];
  const size_t nw = renderSound(Sound::Packet, weak, kBuffer, kRate, -120), ns = renderSound(Sound::Packet, strong, kBuffer, kRate, -60);
  assert(nw == ns && crossings(strong, ns) > 2 * crossings(weak, nw));
  if (dir) { wav(dir, "packet-weak", weak, nw); wav(dir, "packet-strong", strong, ns); }
  puts("ok");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-sounds-') as directory:
    p = Path(directory)
    (p / 'sounds.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(ROOT / 'bots/CardputerADV/src'), str(p / 'sounds.cpp'), '-o', str(p / 'sounds')],
                   check=True)
    subprocess.run([str(p / 'sounds')], check=True)
print('PASS Mesh sounds: all 18 fit the 240 ms buffer and never write past it, every note 360 Hz to 2.1 kHz, peaks under '
      '15% of full scale, no click at either end, no two alike, Help and messages cutting in over the rest, and Radio '
      "Scan's tick rising two octaves from -125 to -55 dBm (ASan/UBSan)")
