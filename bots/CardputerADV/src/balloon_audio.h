#pragma once
// A soft music box for Hot Air Balloon: a slow waltz and gentle chimes, mixed into one quiet
// stream. No heap, storage or network, and nothing here touches hardware.
#include <math.h>
#include <stdint.h>

namespace balloon_audio {
constexpr uint32_t kRate = 11025;
constexpr int kBlockSamples = 512;
constexpr uint32_t kBeatSamples = kRate * 6 / 10;  // 100 beats a minute.
constexpr int kBeatsPerBar = 3, kBars = 8, kBeats = kBars * kBeatsPerBar;
constexpr uint32_t kChimeGapSamples = kRate * 9 / 100;
constexpr float kTwoPi = 6.2831853f;

// The tune stays near 5% of full scale, so it sits well behind the game's other sounds.
constexpr float kMelodyLevel = 1000.0f, kBassLevel = 700.0f, kChimeLevel = 1700.0f;
constexpr float kMelodySeconds = 0.5f, kBassSeconds = 1.2f, kChimeSeconds = 0.12f;

// One note a beat, and zero is a rest. An original tune, so it's free to ship.
constexpr uint16_t kMelody[kBeats] = {
    659, 784, 1047,  1175, 1047, 880,  698, 880, 1047,  988, 784, 0,
    659, 784, 1047,  1319, 1175, 1047, 880, 784, 659,   1047, 0, 0};
// One low note a bar.
constexpr uint16_t kBass[kBars] = {262, 220, 349, 392, 262, 392, 349, 262};

// A plucked sine wave that rings away. Notes ease in, so they start without a click.
struct Voice {
  float phase = 0, step = 0, target = 0, level = 0, fade = 1;

  void strike(float hz, float peak, float seconds) {
    step = kTwoPi * hz / kRate;
    target = peak;
    fade = expf(-1.0f / (seconds * kRate));
  }
  float next() {
    if (target == 0 && level < 0.5f) return 0;
    target = target < 0.5f ? 0 : target * fade;
    level += (target - level) * 0.02f;
    const float value = sinf(phase) * level;
    phase += step;
    if (phase >= kTwoPi) phase -= kTwoPi;
    return value;
  }
};

struct Player {
  Voice melody, bass, chimeFirst, chimeSecond;
  uint32_t toNextBeat = 0, chimeWait = 0;
  float chimeHz = 0;
  int beat = 0;

  void reset() { *this = Player{}; }
  // Two notes, a moment apart.
  void chime(float firstHz, float secondHz) {
    chimeFirst.strike(firstHz, kChimeLevel, kChimeSeconds);
    chimeHz = secondHz;
    chimeWait = kChimeGapSamples;
  }
  void render(int16_t* out, int samples) {
    for (int i = 0; i < samples; ++i) {
      if (toNextBeat == 0) startBeat();
      --toNextBeat;
      if (chimeWait && --chimeWait == 0) chimeSecond.strike(chimeHz, kChimeLevel, kChimeSeconds);
      out[i] = static_cast<int16_t>(melody.next() + bass.next() + chimeFirst.next() + chimeSecond.next());
    }
  }

 private:
  void startBeat() {
    toNextBeat = kBeatSamples;
    if (beat % kBeatsPerBar == 0) bass.strike(kBass[beat / kBeatsPerBar], kBassLevel, kBassSeconds);
    if (kMelody[beat]) melody.strike(kMelody[beat], kMelodyLevel, kMelodySeconds);
    beat = (beat + 1) % kBeats;
  }
};
} // namespace balloon_audio
