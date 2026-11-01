#pragma once
// Short sounds that tell the assistant's stages apart by ear. Each is a quiet pair of fading
// notes. Nothing here touches hardware.
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace activity_cue {
enum class Cue : uint8_t { Sending, Thinking, ReplyReady };

// Long enough to read. A text message has no upload stage to wait for.
constexpr uint32_t kSendingShowMs = 1500;
constexpr float kLevel = 1800.0f;  // About the update chime's level, well below speech.
constexpr float kAttackSeconds = 0.003f, kRingSeconds = 0.05f, kTailSeconds = 0.01f;
constexpr float kTwoPi = 6.2831853f;

struct Notes {
  float firstHz, secondHz, gapSeconds;
};
// Sending climbs, thinking taps twice on one low note, and a ready reply falls.
inline Notes notesFor(Cue cue) {
  switch (cue) {
    case Cue::Sending: return {659.0f, 880.0f, 0.07f};
    case Cue::Thinking: return {440.0f, 440.0f, 0.12f};
    case Cue::ReplyReady: return {988.0f, 740.0f, 0.08f};
  }
  return {440.0f, 440.0f, 0.12f};
}
inline float note(float hz, float seconds) {
  if (seconds < 0) return 0;
  const float attack = 1.0f - expf(-seconds / kAttackSeconds);
  return attack * expf(-seconds / kRingSeconds) * sinf(kTwoPi * hz * seconds);
}
inline void render(Cue cue, int16_t* out, size_t samples, uint32_t rate) {
  const Notes notes = notesFor(cue);
  const float length = static_cast<float>(samples) / rate;
  for (size_t i = 0; i < samples; ++i) {
    const float t = static_cast<float>(i) / rate;
    const float left = (length - t) / kTailSeconds;
    const float tail = left < 1.0f ? left : 1.0f;
    const float mix = note(notes.firstHz, t) + note(notes.secondHz, t - notes.gapSeconds);
    out[i] = static_cast<int16_t>(kLevel * tail * mix);
  }
}
} // namespace activity_cue
