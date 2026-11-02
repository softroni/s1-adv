#pragma once
// Short sounds that tell the assistant's stages apart by ear. Each is a quiet run of fading
// notes. Nothing here touches hardware.
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace activity_cue {
enum class Cue : uint8_t {
  Sending, Thinking, ReplyReady, Searching, Researching, Reading, Coding, Working, Building,
  Testing, Writing, Loading, Speaking
};

// Long enough to read. A text message has no upload stage to wait for.
constexpr uint32_t kSendingShowMs = 1500;
constexpr float kLevel = 1800.0f;  // About the update chime's level, well below speech.
constexpr float kAttackSeconds = 0.003f, kTailSeconds = 0.01f;
constexpr float kTwoPi = 6.2831853f;
constexpr int kMaxNotes = 4;

// How fast a note dies away. Tick sounds like a key press, ring like a small bell.
constexpr float kRing = 0.05f, kTick = 0.012f, kBell = 0.09f;

struct Motif {
  float ring;
  int count;
  float hz[kMaxNotes];
  float at[kMaxNotes];
  float gain[kMaxNotes];
};

// Every cue differs in rhythm or shape as well as pitch, because this speaker is too small to
// make pitch alone easy to hear. It also has almost no output below about 300 Hz.
inline Motif motifFor(Cue cue) {
  switch (cue) {
    // Sending climbs, thinking taps twice on one low note, and a ready reply falls.
    case Cue::Sending: return {kRing, 2, {659, 880}, {0, 0.07f}, {1, 1}};
    case Cue::Thinking: return {kRing, 2, {440, 440}, {0, 0.12f}, {1, 1}};
    case Cue::ReplyReady: return {kRing, 2, {988, 740}, {0, 0.08f}, {1, 1}};
    // A sonar ping and its fainter echo.
    case Cue::Searching: return {kBell, 2, {1319, 1319}, {0, 0.14f}, {1, 0.4f}};
    // Three steps up, like following a trail.
    case Cue::Researching: return {kRing, 3, {523, 659, 784}, {0, 0.07f, 0.14f}, {1, 1, 1}};
    // Two soft taps stepping down, like turning a page.
    case Cue::Reading: return {kTick, 2, {698, 587}, {0, 0.09f}, {1, 1}};
    // Four quick key presses.
    case Cue::Coding: return {kTick, 4, {1760, 1480, 1760, 1568}, {0, 0.05f, 0.1f, 0.15f}, {1, 1, 1, 1}};
    // A plain low-high, the general busy sound.
    case Cue::Working: return {kRing, 2, {392, 523}, {0, 0.1f}, {1, 1}};
    // Two knocks and a higher third, like hammering.
    case Cue::Building: return {kTick, 3, {392, 392, 523}, {0, 0.07f, 0.14f}, {1, 1, 1}};
    // Up, down, up, like a check.
    case Cue::Testing: return {kRing, 3, {880, 659, 880}, {0, 0.07f, 0.14f}, {1, 1, 1}};
    // A quick soft run, like a pen stroke.
    case Cue::Writing: return {kTick, 4, {784, 880, 988, 1175}, {0, 0.04f, 0.08f, 0.12f}, {1, 1, 1, 1}};
    // Two slow notes, a step apart.
    case Cue::Loading: return {kBell, 2, {523, 587}, {0, 0.13f}, {0.8f, 0.8f}};
    // A bright lift, just before the voice starts.
    case Cue::Speaking: return {kBell, 2, {880, 1175}, {0, 0.06f}, {0.8f, 0.8f}};
  }
  return {kRing, 2, {440, 440}, {0, 0.12f}, {1, 1}};
}

inline float note(float hz, float seconds, float ring) {
  if (seconds < 0) return 0;
  const float attack = 1.0f - expf(-seconds / kAttackSeconds);
  return attack * expf(-seconds / ring) * sinf(kTwoPi * hz * seconds);
}

inline void render(Cue cue, int16_t* out, size_t samples, uint32_t rate) {
  const Motif motif = motifFor(cue);
  const float length = static_cast<float>(samples) / rate;
  for (size_t i = 0; i < samples; ++i) {
    const float t = static_cast<float>(i) / rate;
    const float left = (length - t) / kTailSeconds;
    const float tail = left < 1.0f ? left : 1.0f;
    float mix = 0;
    for (int n = 0; n < motif.count; ++n)
      mix += motif.gain[n] * note(motif.hz[n], t - motif.at[n], motif.ring);
    out[i] = static_cast<int16_t>(kLevel * tail * mix);
  }
}
} // namespace activity_cue
