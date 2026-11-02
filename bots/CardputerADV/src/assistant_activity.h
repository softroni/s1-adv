#pragma once
// What the assistant is doing, and how the screens and speaker show it. Nothing here touches
// hardware. The bridge reports the work states; the device adds the ones only it can see.
#include <stdint.h>
#include <string.h>

#include "activity_cue.h"
#include "launcher_icons.h"

namespace assistant_activity {
enum class State : uint8_t {
  None, Sending, Thinking, Searching, Researching, Reading, Coding, Working, Building, Testing,
  Writing, Loading, Speaking
};

constexpr uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Look {
  const char* label;  // For the Home badge.
  const char* title;  // For a line of text.
  uint16_t accent;    // Stays distinct after the canvas reduces it to 8 bits.
  const uint8_t* icon;
  activity_cue::Cue cue;
};

inline Look look(State state) {
  using namespace launcher_icons;
  using Cue = activity_cue::Cue;
  switch (state) {
    case State::Sending: return {"SENDING", "Sending", color(47, 150, 255), kPlane, Cue::Sending};
    case State::Searching: return {"SEARCHING", "Searching", color(47, 227, 255), kMagnifier, Cue::Searching};
    case State::Researching: return {"RESEARCHING", "Researching", color(157, 102, 255), kGlobe, Cue::Researching};
    case State::Reading: return {"READING", "Reading", color(88, 240, 141), kBooks, Cue::Reading};
    case State::Coding: return {"CODING", "Coding", color(255, 210, 64), kCode, Cue::Coding};
    case State::Working: return {"WORKING", "Working", color(255, 126, 182), kWrench, Cue::Working};
    case State::Building: return {"BUILDING", "Building", color(255, 80, 64), kGear, Cue::Building};
    case State::Testing: return {"TESTING", "Testing", color(180, 150, 255), kCheck, Cue::Testing};
    case State::Writing: return {"WRITING", "Writing", color(203, 216, 226), kPencil, Cue::Writing};
    case State::Loading: return {"LOADING", "Loading", color(160, 255, 64), kHourglass, Cue::Loading};
    case State::Speaking: return {"SPEAKING", "Speaking", color(64, 255, 200), kWaveform, Cue::Speaking};
    case State::None:
    case State::Thinking: break;
  }
  return {"THINKING", "Thinking", color(255, 136, 64), kThought, Cue::Thinking};
}

// A state this firmware has never heard of still means the assistant is busy.
inline State fromWire(const char* name) {
  if (!name || !*name || !strcmp(name, "idle")) return State::None;
  if (!strcmp(name, "thinking")) return State::Thinking;
  if (!strcmp(name, "searching")) return State::Searching;
  if (!strcmp(name, "researching")) return State::Researching;
  if (!strcmp(name, "reading")) return State::Reading;
  if (!strcmp(name, "coding")) return State::Coding;
  if (!strcmp(name, "working")) return State::Working;
  if (!strcmp(name, "building")) return State::Building;
  if (!strcmp(name, "testing")) return State::Testing;
  if (!strcmp(name, "writing")) return State::Writing;
  return State::Working;
}

// Claude can change tools several times a second. Sounds that follow every step would be noise,
// so a cue needs a real change and some quiet before it. Going back to thinking between tools
// says nothing new.
struct CueGate {
  static constexpr uint32_t kMinGapMs = 2500, kRepeatMs = 30000;
  State last = State::None;
  uint32_t lastMs = 0;
  bool played = false;

  void reset() { *this = CueGate(); }
  bool allow(State next, uint32_t nowMs) {
    if (next == State::None || next == State::Thinking) return false;
    if (played && nowMs - lastMs < (next == last ? kRepeatMs : kMinGapMs)) return false;
    last = next;
    lastMs = nowMs;
    played = true;
    return true;
  }
};
}  // namespace assistant_activity
