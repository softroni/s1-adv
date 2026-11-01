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
// Bad news is meant to sound bad: a falling square wave, louder than anything else here.
constexpr float kBuzzLevel = 2200.0f, kCrashLevel = 3400.0f;

// One note a beat, and zero is a rest. An original tune, so it's free to ship.
constexpr uint16_t kMelody[kBeats] = {
    659, 784, 1047,  1175, 1047, 880,  698, 880, 1047,  988, 784, 0,
    659, 784, 1047,  1319, 1175, 1047, 880, 784, 659,   1047, 0, 0};
// One low note a bar.
constexpr uint16_t kBass[kBars] = {262, 220, 349, 392, 262, 392, 349, 262};

// A plucked sine wave that rings away. Notes ease in, so they start without a click.
struct Voice {
  float phase = 0, step = 0, target = 0, level = 0, fade = 1, glide = 1;
  bool square = false;

  void strike(float hz, float peak, float seconds) {
    step = kTwoPi * hz / kRate;
    target = peak;
    fade = expf(-1.0f / (seconds * kRate));
    glide = 1;
  }
  float next() {
    if (target == 0 && level < 0.5f) return 0;
    target = target < 0.5f ? 0 : target * fade;
    level += (target - level) * 0.02f;
    const float wave = sinf(phase);
    const float value = (square ? (wave < 0 ? -1.0f : 1.0f) : wave) * level;
    step *= glide;
    phase += step;
    if (phase >= kTwoPi) phase -= kTwoPi;
    return value;
  }
};

struct Player {
  Voice melody, bass, chimeFirst, chimeSecond, buzzer;
  uint32_t toNextBeat = 0, chimeWait = 0, buzzLeft = 0;
  float chimeHz = 0;
  int beat = 0;
  bool music = true;

  void reset() { *this = Player{}; }
  // Only the tune stops. Chimes and buzzes still play over the silence.
  void setMusic(bool on) {
    music = on;
    if (!on) melody.target = bass.target = 0;
  }
  // Two notes, a moment apart.
  void chime(float firstHz, float secondHz) {
    chimeFirst.strike(firstHz, kChimeLevel, kChimeSeconds);
    chimeHz = secondHz;
    chimeWait = kChimeGapSamples;
  }
  // A single note that slides down from one pitch to the other.
  void buzz(float startHz, float endHz, float seconds, float peak) {
    buzzer.square = true;
    buzzer.strike(startHz, peak, seconds * 2);
    buzzer.glide = powf(endHz / startHz, 1.0f / (seconds * kRate));
    buzzLeft = static_cast<uint32_t>(seconds * kRate);
  }
  void render(int16_t* out, int samples) {
    for (int i = 0; i < samples; ++i) {
      if (toNextBeat == 0) startBeat();
      --toNextBeat;
      if (chimeWait && --chimeWait == 0) chimeSecond.strike(chimeHz, kChimeLevel, kChimeSeconds);
      if (buzzLeft && --buzzLeft == 0) buzzer.target = 0;
      out[i] = static_cast<int16_t>(melody.next() + bass.next() + chimeFirst.next() + chimeSecond.next() + buzzer.next());
    }
  }

 private:
  void startBeat() {
    toNextBeat = kBeatSamples;
    if (music && beat % kBeatsPerBar == 0) bass.strike(kBass[beat / kBeatsPerBar], kBassLevel, kBassSeconds);
    if (music && kMelody[beat]) melody.strike(kMelody[beat], kMelodyLevel, kMelodySeconds);
    beat = (beat + 1) % kBeats;
  }
};
} // namespace balloon_audio
