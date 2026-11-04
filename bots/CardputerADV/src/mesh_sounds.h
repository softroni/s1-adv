#pragma once
// Mesh's sounds: short runs of fading notes, made the way the assistant's activity cues are, and
// told apart by rhythm and shape as much as by pitch, since the speaker is small and has almost
// nothing below about 300 Hz. Nothing here touches hardware: part 090 plays them.
#include "activity_cue.h"

namespace s1_mesh {
enum class Sound : uint8_t {
  Ready, Off, Sent, Heard, Delivered, Failed, Message, Group, HelpStart, HelpRound, HelpAnswered,
  Nearby, GpsFix, Ask, Nope, Saved, Removed, Packet
};
constexpr uint8_t kSoundCount = 18;

// Louder for what you'd want to hear from a pocket, plainer for what answers a key, and softest
// for what repeats. All stay well under a tenth of full scale before the speaker's own volume.
constexpr float kSoundLoud = 3600.0f, kSoundPlain = 2000.0f, kSoundSoft = 1200.0f;
constexpr uint16_t kSoundLongestMs = 240;  // The buffer the update chime and the activity cues use.

struct SoundShape {
  activity_cue::Motif motif;
  float level;
  uint16_t ms;       // How long it plays.
  uint8_t priority;  // A sound cuts off a quieter-ranked Mesh sound still playing, and waits for the rest.
};

inline SoundShape soundShape(Sound sound) {
  using activity_cue::kBell;
  using activity_cue::kRing;
  using activity_cue::kTick;
  switch (sound) {
    // The radio coming up climbs three notes, and going off comes back down them.
    case Sound::Ready: return {{kRing, 3, {523, 784, 1047}, {0, 0.06f, 0.12f}, {0.8f, 0.9f, 1}}, kSoundPlain, 240, 2};
    case Sound::Off: return {{kRing, 3, {1047, 784, 523}, {0, 0.06f, 0.12f}, {1, 0.9f, 0.8f}}, kSoundPlain, 240, 2};
    // A quick run of ticks up, like something leaving.
    case Sound::Sent: return {{kTick, 3, {988, 1319, 1760}, {0, 0.035f, 0.07f}, {0.8f, 0.9f, 1}}, kSoundPlain, 140, 2};
    // A channel message repeated nearby, which is all a channel can ever confirm: one small bell.
    case Sound::Heard: return {{kBell, 1, {1319}, {0}, {0.7f}}, kSoundPlain, 200, 2};
    // Their radio said it arrived: two bells, up.
    case Sound::Delivered: return {{kBell, 2, {1047, 1568}, {0, 0.07f}, {1, 1}}, kSoundPlain, 240, 3};
    // It didn't get there: two low notes, down.
    case Sound::Failed: return {{kRing, 2, {494, 370}, {0, 0.11f}, {1, 1}}, kSoundPlain, 240, 3};
    // A message for you or from a contact rings twice on one high note, like a doorbell...
    case Sound::Message: return {{kBell, 2, {1568, 1568}, {0, 0.11f}, {1, 0.9f}}, kSoundLoud, 240, 4};
    // ...and one on the private channel goes high, lower, high.
    case Sound::Group: return {{kRing, 3, {1319, 1047, 1319}, {0, 0.07f, 0.14f}, {1, 0.85f, 1}}, kSoundLoud, 240, 4};
    // Help going out: three even beeps, the loudest thing Mesh makes.
    case Sound::HelpStart: return {{kRing, 3, {1760, 1760, 1760}, {0, 0.07f, 0.14f}, {1, 1, 1}}, kSoundLoud, 240, 5};
    // Each time it goes out again: one soft tick, so a pocket knows it's still trying.
    case Sound::HelpRound: return {{kTick, 1, {1760}, {0}, {1}}, kSoundSoft, 80, 1};
    // Someone has it: four notes climbing.
    case Sound::HelpAnswered:
      return {{kRing, 4, {784, 988, 1175, 1568}, {0, 0.045f, 0.09f, 0.135f}, {0.8f, 0.85f, 0.9f, 1}}, kSoundLoud, 240, 5};
    // A contact's radio heard for the first time since Mesh started: a ping and its echo.
    case Sound::Nearby: return {{kBell, 2, {1175, 1175}, {0, 0.13f}, {1, 0.35f}}, kSoundPlain, 240, 2};
    // The GPS has a fix: two quick high blips.
    case Sound::GpsFix: return {{kRing, 2, {2093, 2093}, {0, 0.06f}, {0.8f, 0.8f}}, kSoundPlain, 160, 2};
    // Press it again to be sure: a rising question.
    case Sound::Ask: return {{kRing, 2, {784, 1175}, {0, 0.1f}, {1, 1}}, kSoundPlain, 240, 3};
    // Couldn't: two dull knocks on a low note.
    case Sound::Nope: return {{kTick, 2, {392, 392}, {0, 0.07f}, {1, 1}}, kSoundPlain, 140, 3};
    // Kept, or switched on: two clicks up. Taken away, or switched off: the same two down.
    case Sound::Saved: return {{kTick, 2, {1319, 1760}, {0, 0.05f}, {1, 1}}, kSoundPlain, 120, 2};
    case Sound::Removed: return {{kTick, 2, {1760, 1319}, {0, 0.05f}, {1, 1}}, kSoundPlain, 120, 2};
    // Radio Scan: one tick a packet, its pitch set by how strong it came in.
    case Sound::Packet: return {{kTick, 1, {1000}, {0}, {1}}, kSoundSoft, 50, 0};
  }
  return {{kTick, 1, {1000}, {0}, {1}}, kSoundSoft, 50, 0};
}

// Radio Scan's tick: 500 Hz for -125 dBm or weaker, up two octaves to 2 kHz at -55 or stronger, so
// walking toward a radio sounds like the ticks rising.
inline float packetHz(int16_t dbm) {
  const float d = dbm < -125 ? -125.0f : dbm > -55 ? -55.0f : static_cast<float>(dbm);
  return 500.0f * powf(4.0f, (d + 125.0f) / 70.0f);
}

// Writes the sound into out, which holds capacity samples at rate, and returns how many it wrote.
inline size_t renderSound(Sound sound, int16_t* out, size_t capacity, uint32_t rate, int16_t dbm = -90) {
  SoundShape shape = soundShape(sound);
  if (sound == Sound::Packet) shape.motif.hz[0] = packetHz(dbm);
  size_t samples = static_cast<size_t>(rate) * shape.ms / 1000;
  if (samples > capacity) samples = capacity;
  const float length = static_cast<float>(samples) / rate;
  for (size_t i = 0; i < samples; ++i) {
    const float t = static_cast<float>(i) / rate;
    const float left = (length - t) / activity_cue::kTailSeconds;
    const float tail = left < 1.0f ? left : 1.0f;
    float mix = 0;
    for (int n = 0; n < shape.motif.count; ++n)
      mix += shape.motif.gain[n] * activity_cue::note(shape.motif.hz[n], t - shape.motif.at[n], shape.motif.ring);
    out[i] = static_cast<int16_t>(shape.level * tail * mix);
  }
  return samples;
}
}  // namespace s1_mesh
