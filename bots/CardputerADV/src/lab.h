#pragma once
// The Lab: every sensor and output the Cardputer can reach, and saved rules that join them. Rules
// read a value for each sensor and say what each output should be, so all of it runs on the host.
// No heap, storage or network, and nothing here touches hardware.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pbhub.h"

namespace lab {
// --- What can be read -----------------------------------------------------------------------
// New signals go at the end, because saved rules store these numbers.
enum class Sig : uint8_t { Distance, Temperature, Humidity, Knob, Light, Motion, StickX, StickY, StickPress, Tilt,
                           Satellites, Spoken, Count };
constexpr uint8_t kSigCount = static_cast<uint8_t>(Sig::Count);

// lo to hi is what a follow spreads across its output. A reading can go past it. `step` is how
// far a value must move to count as changed, so a noisy sensor doesn't look busy.
// `key` is the name a rule from Claude uses, so the bridge's prompt has to list the same ones.
struct SigInfo { const char* key; const char* name; const char* unit; int16_t lo, hi, step; bool onOff; };
constexpr SigInfo kSigs[kSigCount] = {
    {"distance", "Distance", "cm", 2, 100, 3, false},
    {"temperature", "Temperature", "C", -10, 50, 1, false},
    {"humidity", "Humidity", "%", 0, 100, 2, false},
    {"knob", "Knob", "%", 0, 100, 2, false},
    {"light", "Light", "%", 0, 100, 3, false},
    {"motion", "Motion", "", 0, 1, 1, true},
    {"stick_x", "Stick X", "", -100, 100, 8, false},
    {"stick_y", "Stick Y", "", -100, 100, 8, false},
    {"stick_button", "Stick button", "", 0, 1, 1, true},
    {"tilt", "Tilt", "deg", -90, 90, 5, false},
    {"satellites", "Satellites", "", 0, 32, 1, false},
    {"spoken", "Its name", "", 0, 1, 1, true},  // On once the rule's name is said, off when said again.
};
constexpr const SigInfo& info(Sig sig) { return kSigs[static_cast<uint8_t>(sig)]; }

// --- What can be driven ---------------------------------------------------------------------
// New outputs go at the end, because saved rules store these numbers.
enum class Out : uint8_t { Servo, StripColor, StripBright, StripLength, Relay1, Relay2, Led, Tone, LampPower,
                           LampBright, AcTcl, AcDaikin, Say, Sequence, Count };
constexpr uint8_t kOutCount = static_cast<uint8_t>(Out::Count);
constexpr uint8_t kMaxSequences = 8;

constexpr int kStripLeds = 30;
struct OutInfo { const char* key; const char* name; const char* unit; int16_t lo, hi, step; bool onOff; };
constexpr OutInfo kOuts[kOutCount] = {
    {"servo", "Servo", "deg", 0, 180, 5, false},
    {"strip_color", "Strip color", "", 0, 359, 15, false},
    {"strip_bright", "Strip bright", "%", 0, 100, 5, false},
    {"strip_length", "Strip length", "LEDs", 0, kStripLeds, 1, false},
    {"relay_1", "Relay 1", "", 0, 1, 1, true},
    {"relay_2", "Relay 2", "", 0, 1, 1, true},
    {"led", "Built-in LED", "", 0, 359, 15, false},
    {"speaker", "Speaker", "Hz", 200, 2000, 50, false},
    {"lamp_power", "Lamp power", "", 0, 1, 1, true},
    {"lamp_bright", "Lamp bright", "%", 0, 100, 5, false},
    {"ac_tcl", "TCL AC", "", 0, 1, 1, true},
    {"ac_daikin", "Daikin AC", "", 0, 1, 1, true},
    {"say", "Say", "", 0, 1, 1, true},  // Speaks the rule's phrase once, each time it starts firing.
    {"sequence", "Sequence", "", 0, kMaxSequences - 1, 1, true},  // Its value says which, and it starts the same way.
};
constexpr const OutInfo& info(Out out) { return kOuts[static_cast<uint8_t>(out)]; }

// What an output should be right now. Off means nothing is asking, so it goes back to rest.
struct Drive {
  bool on = false;
  int16_t value = 0;
};

template <class T>
constexpr T clampTo(T value, T lo, T hi) { return value < lo ? lo : value > hi ? hi : value; }

// Maps across two ranges, rounded, and either one can run backwards.
inline int16_t mapRange(int32_t value, int32_t fromLo, int32_t fromHi, int32_t toLo, int32_t toHi) {
  if (fromHi == fromLo) return static_cast<int16_t>(toLo);
  if (fromHi < fromLo) return mapRange(value, fromHi, fromLo, toHi, toLo);
  value = clampTo(value, fromLo, fromHi);
  const int32_t span = fromHi - fromLo;
  const int32_t scaled = (value - fromLo) * (toHi - toLo);
  const int32_t rounded = (scaled >= 0 ? scaled + span / 2 : scaled - span / 2) / span;
  return static_cast<int16_t>(toLo + rounded);
}

// --- Readings -------------------------------------------------------------------------------
constexpr uint32_t kChangedMs = 400;  // How long "it just changed" stays true.

struct Reading {
  int16_t value = 0, anchor = 0;
  bool live = false, moved = false;
  uint32_t changedMs = 0;
};

class Inputs {
 public:
  void set(Sig sig, int16_t value, uint32_t now) {
    Reading& r = mReadings[static_cast<uint8_t>(sig)];
    if (!r.live) {
      // A sensor coming back isn't a change, or plugging one in would fire its rules.
      r.live = true;
      r.value = r.anchor = value;
      return;
    }
    r.value = value;
    const int32_t gap = static_cast<int32_t>(value) - r.anchor;
    if (gap >= info(sig).step || -gap >= info(sig).step) {
      r.anchor = value;
      r.moved = true;
      r.changedMs = now;
    }
  }
  void lose(Sig sig) {
    Reading& r = mReadings[static_cast<uint8_t>(sig)];
    r.live = false;
    r.moved = false;
  }
  void adopt(Sig sig, const Reading& reading) { mReadings[static_cast<uint8_t>(sig)] = reading; }
  const Reading& get(Sig sig) const { return mReadings[static_cast<uint8_t>(sig)]; }

 private:
  Reading mReadings[kSigCount];
};

// Practice stands in for sensors from the keyboard. Its readings sit apart from the real ones, so
// a live sensor underneath can't look like a change, and going back loses nothing.
class Practice {
 public:
  void clear() { *this = Practice{}; }
  bool faking(Sig sig) const { return mOn[static_cast<uint8_t>(sig)]; }
  bool any() const {
    for (bool on : mOn) if (on) return true;
    return false;
  }
  // The first press starts from the real reading, or the middle of the range without one.
  void nudge(Sig sig, int direction, const Inputs& real, uint32_t now) {
    const SigInfo& s = info(sig);
    const uint8_t index = static_cast<uint8_t>(sig);
    if (!mOn[index]) {
      const Reading& reading = real.get(sig);
      mOn[index] = true;
      mInputs.set(sig, reading.live ? reading.value : static_cast<int16_t>((s.lo + s.hi) / 2), now);
    }
    const int16_t was = mInputs.get(sig).value;
    if (s.onOff) {
      mInputs.set(sig, was ? 0 : 1, now);
      return;
    }
    const int jump = (s.hi - s.lo) / 20;
    const int size = jump > s.step ? jump : s.step;
    mInputs.set(sig, clampTo<int16_t>(static_cast<int16_t>(was + direction * size), s.lo, s.hi), now);
  }
  void overlay(Inputs& view) const {
    for (uint8_t i = 0; i < kSigCount; ++i) if (mOn[i]) view.adopt(static_cast<Sig>(i), mInputs.get(static_cast<Sig>(i)));
  }

 private:
  bool mOn[kSigCount] = {};
  Inputs mInputs;
};

// --- Rules ----------------------------------------------------------------------------------
constexpr uint8_t kMaxRules = 8, kMaxConds = 3, kMaxActions = 3, kNameChars = 15, kSayChars = 40;
constexpr uint16_t kMaxHoldMs = 30000, kHoldStepMs = 500;

enum class Op : uint8_t { Below, Above, Changes };
struct Cond {
  Sig sig = Sig::Distance;
  Op op = Op::Below;
  int16_t value = 30;
};

enum class How : uint8_t { Set, Follow, FollowBack };
struct Action {
  Out out = Out::Servo;
  How how = How::Set;
  int16_t value = 90;
  Sig source = Sig::Knob;
};

struct Rule {
  char name[kNameChars + 1] = "";
  bool on = true, any = true;
  uint8_t conds = 0, actions = 0;
  uint16_t holdMs = 2000;
  Cond cond[kMaxConds];
  Action action[kMaxActions];
  char say[kSayChars + 1] = "";  // What Out::Say speaks. Travels with the rule, so it can't go astray.
};

inline bool hasSay(const Rule& rule) {
  if (!rule.say[0]) return false;
  for (uint8_t i = 0; i < rule.actions; ++i) if (rule.action[i].out == Out::Say) return true;
  return false;
}
// Plain enough for a file, a header and JSON, and for speech to read out.
constexpr bool sayAllowed(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
         c == ',' || c == '!' || c == '?' || c == '\'' || c == '-';
}
// Names the file a phrase's speech is kept in, so the same words share one.
inline uint32_t sayHash(const char* text) {
  uint32_t hash = 2166136261U;
  for (; *text; ++text) hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619U;
  return hash;
}

// An on or off signal is "above 0" for on and "below 1" for off.
inline void setOnOff(Cond& cond, bool on) {
  cond.op = on ? Op::Above : Op::Below;
  cond.value = on ? 0 : 1;
}

// `said` is whether this rule's name has been spoken, which belongs to the rule and not to a sensor.
inline bool condHolds(const Cond& cond, const Inputs& inputs, uint32_t now, bool said = false) {
  if (cond.sig == Sig::Spoken) return cond.op == Op::Above ? said : !said;
  const Reading& r = inputs.get(cond.sig);
  if (!r.live) return false;
  switch (cond.op) {
    case Op::Below: return r.value < cond.value;
    case Op::Above: return r.value > cond.value;
    case Op::Changes: return r.moved && now - r.changedMs < kChangedMs;
  }
  return false;
}

inline bool ruleHolds(const Rule& rule, const Inputs& inputs, uint32_t now, bool said = false) {
  if (!rule.conds) return false;
  for (uint8_t i = 0; i < rule.conds; ++i) {
    const bool holds = condHolds(rule.cond[i], inputs, now, said);
    if (holds == rule.any) return holds;
  }
  return !rule.any;
}

inline bool actionValue(const Action& action, const Inputs& inputs, int16_t& value) {
  if (action.how == How::Set) {
    value = action.value;
    return true;
  }
  const Reading& r = inputs.get(action.source);
  if (!r.live) return false;
  const SigInfo& from = info(action.source);
  const OutInfo& to = info(action.out);
  value = action.how == How::Follow ? mapRange(r.value, from.lo, from.hi, to.lo, to.hi)
                                    : mapRange(r.value, from.lo, from.hi, to.hi, to.lo);
  return true;
}

// How close a waiting condition is to coming true, from 0 to 100, for the bar on the board.
inline int closeness(const Cond& cond, int16_t value) {
  const SigInfo& sig = info(cond.sig);
  if (cond.op == Op::Above) return mapRange(value, sig.lo, cond.value, 0, 100);
  if (cond.op == Op::Below) return mapRange(value, sig.hi, cond.value, 0, 100);
  return 0;
}

struct RuleState {
  bool active = false;
  uint16_t fires = 0;
  uint32_t heldMs = 0, firedMs = 0;
};

inline bool listensForName(const Rule& rule) {
  for (uint8_t i = 0; i < rule.conds; ++i) if (rule.cond[i].sig == Sig::Spoken) return true;
  return false;
}
// Whether `name` appears in `heard`, ignoring case, spaces and punctuation, which speech
// recognition is free to change. Returns how many letters matched, or 0.
inline size_t nameInWords(const char* name, const char* heard) {
  const auto plain = [](char c) -> char {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : '\0';
  };
  for (const char* start = heard; *start; ++start) {
    if (!plain(*start)) continue;
    const char* n = name;
    size_t matched = 0;
    for (const char* h = start;; ++n) {
      while (*n && !plain(*n)) ++n;
      if (!*n) {
        if (matched) return matched;
        break;
      }
      while (*h && !plain(*h)) ++h;
      if (!*h || plain(*h) != plain(*n)) break;
      ++h;
      ++matched;
    }
  }
  return 0;
}
// The same words, whatever their case or spacing.
inline bool sameName(const char* a, const char* b) {
  const size_t letters = nameInWords(a, b);
  return letters && letters == nameInWords(b, a);
}
// By name, because that's all Claude knows a rule or a sequence by. -1 means nothing has that name.
template <class Named>
int findByName(const Named* items, uint8_t count, const char* name) {
  for (uint8_t i = 0; i < count; ++i) if (sameName(items[i].name, name)) return i;
  return -1;
}
// Which rule was named, for rules that listen for theirs. The longest name wins, so "movie time"
// isn't taken for a rule called "time".
inline int spokenRule(const char* heard, const Rule* rules, uint8_t count) {
  int best = -1;
  size_t bestLetters = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (!listensForName(rules[i])) continue;
    const size_t letters = nameInWords(rules[i].name, heard);
    if (letters > bestLetters) {
      bestLetters = letters;
      best = i;
    }
  }
  return best;
}

// One pass over the rules in order, so when two want the same output the later one wins.
// `spoken` has a bit for each rule whose name has been said.
inline void step(const Rule* rules, uint8_t count, RuleState* states, const Inputs& inputs, uint32_t now,
                 Drive* drive, uint8_t spoken = 0) {
  for (uint8_t i = 0; i < kOutCount; ++i) drive[i] = Drive{};
  for (uint8_t i = 0; i < count; ++i) {
    const Rule& rule = rules[i];
    RuleState& state = states[i];
    if (!rule.on) {
      state.active = false;
      continue;
    }
    if (ruleHolds(rule, inputs, now, (spoken >> i) & 1)) {
      if (!state.active) {
        state.active = true;
        state.firedMs = now;
        if (state.fires < 0xFFFF) ++state.fires;
      }
      state.heldMs = now;
    } else if (state.active && now - state.heldMs >= rule.holdMs) {
      state.active = false;
    }
    if (!state.active) continue;
    for (uint8_t a = 0; a < rule.actions; ++a) {
      int16_t value = 0;
      if (!actionValue(rule.action[a], inputs, value)) continue;
      Drive& out = drive[static_cast<uint8_t>(rule.action[a].out)];
      out.on = true;
      out.value = value;
    }
  }
}

// --- Sequences ---------------------------------------------------------------------------------
// A rule says when, and a sequence says what happens, in what order. Steps land in the same Drive
// table as rules, so the brightness cap, the relay guard and the gaps for remotes cover them too.
constexpr uint8_t kMaxSeqSteps = 16;
constexpr int16_t kMaxWaitTenths = 6000;  // Ten minutes.
constexpr uint32_t kSeqEndMs = 100;       // Long enough for a last step with no wait to reach its part.
// What a step can drive. Speech stops the Lab until it's done, and a sequence can't start another.
constexpr uint8_t kSeqOutCount = static_cast<uint8_t>(Out::Say);
static_assert(kOutCount <= 16, "SeqRun marks the outputs it holds in 16 bits");

enum class SeqKind : uint8_t { Set, Off, Wait };
struct SeqStep {
  SeqKind kind = SeqKind::Set;
  Out out = Out::Servo;
  int16_t value = 90;  // What to set it to, or how long to wait in tenths of a second.
};
struct Sequence {
  char name[kNameChars + 1] = "";
  bool loop = false;
  uint8_t count = 0;
  SeqStep steps[kMaxSeqSteps];
};

constexpr bool isStrip(Out out) { return out == Out::StripColor || out == Out::StripBright || out == Out::StripLength; }
// A servo stays where it is with nothing asking, so "off" would look like a step that did nothing.
constexpr bool canTurnOff(Out out) { return out != Out::Servo; }
inline bool stepAllowed(const SeqStep& step) {
  if (static_cast<uint8_t>(step.out) >= kSeqOutCount) return false;
  if (step.kind == SeqKind::Wait) return step.value >= 1 && step.value <= kMaxWaitTenths;
  return step.kind == SeqKind::Off || (step.value >= info(step.out).lo && step.value <= info(step.out).hi);
}
// Fine steps for a short wait and big ones for a long wait, and going back retraces the way up.
inline int16_t nextWait(int tenths, int direction) {
  const int from = direction > 0 ? tenths : tenths - 1;
  const int size = from < 10 ? 1 : from < 100 ? 5 : from < 600 ? 50 : 300;
  return clampTo<int16_t>(static_cast<int16_t>(tenths + direction * size), 1, kMaxWaitTenths);
}

// One sequence running. What it sets stays set until a later step changes it, and those outputs are
// its own until it ends, while rules keep the rest. Then they all let go, so a forgotten last step
// can't leave a relay closed.
class SeqRun {
 public:
  bool running() const { return mRunning; }
  int owner() const { return mOwner; }  // The rule that started it, or -1.
  uint8_t index() const { return mIndex; }
  uint8_t shown() const { return mNext ? static_cast<uint8_t>(mNext - 1) : 0; }  // The step in progress.
  uint32_t leftMs(uint32_t now) const {
    const int32_t left = static_cast<int32_t>(mDueMs - now);
    return mRunning && left > 0 ? static_cast<uint32_t>(left) : 0;
  }
  void start(uint8_t index, int owner, uint32_t now) {
    *this = SeqRun{};
    mRunning = true;
    mIndex = index;
    mOwner = static_cast<int8_t>(owner);
    mDueMs = now;
  }
  void stop() { *this = SeqRun{}; }
  void service(const Sequence* seqs, uint8_t count, uint32_t now) {
    if (!mRunning) return;
    if (mIndex >= count) {
      stop();
      return;
    }
    if (static_cast<int32_t>(now - mDueMs) < 0) return;
    const Sequence& seq = seqs[mIndex];
    if (mNext >= seq.count) {
      if (!seq.loop || !seq.count) {
        stop();
        return;
      }
      mNext = 0;
    }
    while (mNext < seq.count) {
      const SeqStep& step = seq.steps[mNext++];
      if (step.kind == SeqKind::Wait) {
        mDueMs = now + static_cast<uint32_t>(step.value) * 100U;
        return;
      }
      hold(step);
    }
    mDueMs = now + kSeqEndMs;  // This also stops a loop with no waits from taking over the Lab.
  }
  void apply(Drive* drive) const {
    if (!mRunning) return;
    for (uint8_t i = 0; i < kOutCount; ++i) if ((mHeld >> i) & 1) drive[i] = mDrive[i];
  }

 private:
  void claim(Out out, const Drive& drive) {
    mDrive[static_cast<uint8_t>(out)] = drive;
    mHeld = static_cast<uint16_t>(mHeld | (1U << static_cast<uint8_t>(out)));
  }
  void hold(const SeqStep& step) {
    const Drive drive{step.kind == SeqKind::Set, step.value};
    if (step.kind == SeqKind::Off && isStrip(step.out)) {  // Its three outputs are one strip.
      claim(Out::StripColor, drive);
      claim(Out::StripBright, drive);
      claim(Out::StripLength, drive);
    } else {
      claim(step.out, drive);
    }
  }

  bool mRunning = false;
  uint8_t mIndex = 0, mNext = 0;
  int8_t mOwner = -1;
  uint16_t mHeld = 0;
  uint32_t mDueMs = 0;
  Drive mDrive[kOutCount];
};

// A rule starts its sequence once, each time it starts firing, and a later start replaces whatever
// is running. A loop has no end of its own, so it lasts as long as its rule does, and picks up
// again if something shorter took over or the rules were paused.
inline void runSequences(const Rule* rules, uint8_t count, const RuleState* states, uint16_t* seen, const Sequence* seqs,
                         uint8_t seqCount, SeqRun& run, uint32_t now) {
  if (run.running() && run.owner() >= 0 && run.index() < seqCount && seqs[run.index()].loop && !states[run.owner()].active) run.stop();
  for (uint8_t i = 0; i < count; ++i) {
    const bool fired = states[i].fires != seen[i];
    seen[i] = states[i].fires;
    for (uint8_t a = 0; a < rules[i].actions; ++a) {
      const Action& action = rules[i].action[a];
      if (action.out != Out::Sequence || action.value < 0 || action.value >= seqCount) continue;
      const uint8_t index = static_cast<uint8_t>(action.value);
      if (fired || (states[i].active && seqs[index].loop && !run.running())) run.start(index, i, now);
    }
  }
}

inline int sequenceUsers(const Rule* rules, uint8_t count, uint8_t index) {
  int users = 0;
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t a = 0; a < rules[i].actions; ++a) {
      if (rules[i].action[a].out == Out::Sequence && rules[i].action[a].value == index) {
        ++users;
        break;
      }
    }
  }
  return users;
}
// Rules store a sequence by its place in the list, so taking one out has to move theirs too.
inline void removeSequence(Sequence* seqs, uint8_t& seqCount, Rule* rules, uint8_t count, uint8_t index) {
  for (uint8_t i = 0; i < count; ++i) {
    Rule& rule = rules[i];
    uint8_t kept = 0;
    for (uint8_t a = 0; a < rule.actions; ++a) {
      Action action = rule.action[a];
      if (action.out == Out::Sequence && action.value == index) continue;
      if (action.out == Out::Sequence && action.value > index) --action.value;
      rule.action[kept++] = action;
    }
    rule.actions = kept;
  }
  for (uint8_t i = index; i + 1 < seqCount; ++i) seqs[i] = seqs[i + 1];
  --seqCount;
}

// The first one to try: it only needs the Cardputer's own LED and speaker.
inline Sequence starterSequence() {
  constexpr SeqStep kSteps[] = {{SeqKind::Set, Out::Led, 240}, {SeqKind::Set, Out::Tone, 660}, {SeqKind::Wait, Out::Servo, 3},
                                {SeqKind::Set, Out::Led, 120}, {SeqKind::Set, Out::Tone, 880}, {SeqKind::Wait, Out::Servo, 3},
                                {SeqKind::Off, Out::Tone, 0},  {SeqKind::Wait, Out::Servo, 5}};
  Sequence seq;
  strcpy(seq.name, "Hello");
  for (const SeqStep& step : kSteps) seq.steps[seq.count++] = step;
  return seq;
}

// The first thing to try with the Lab: the rule that started it, ready on a new device.
inline Rule starterRule() {
  Rule rule;
  strcpy(rule.name, "Wave lights");
  rule.conds = 2;
  rule.cond[0].sig = Sig::Knob;
  rule.cond[0].op = Op::Changes;
  rule.cond[1].sig = Sig::Distance;
  rule.cond[1].op = Op::Below;
  rule.cond[1].value = 30;
  rule.actions = 2;
  rule.action[0].out = Out::StripColor;
  rule.action[0].how = How::Follow;
  rule.action[0].source = Sig::Distance;
  rule.action[1].out = Out::Servo;
  rule.action[1].how = How::Follow;
  rule.action[1].source = Sig::Knob;
  return rule;
}

// --- Recipes ----------------------------------------------------------------------------------
struct Recipe {
  const char* name;
  const char* about;
  uint16_t holdMs;
  Cond when;
  Action first;
  bool second;
  Action then;
};
constexpr Action setTo(Out out, int16_t value) { return Action{out, How::Set, value, Sig::Knob}; }
constexpr Action follow(Out out, Sig source) { return Action{out, How::Follow, 0, source}; }
constexpr Action oppose(Out out, Sig source) { return Action{out, How::FollowBack, 0, source}; }
constexpr int kRecipeCount = 10;
constexpr Recipe kRecipes[kRecipeCount] = {
    {"Knob to servo", "Turn the knob and the servo follows", 2000, {Sig::Knob, Op::Changes, 0},
     follow(Out::Servo, Sig::Knob), false, {}},
    {"Stick steers", "Push the joystick to aim the servo", 1000, {Sig::StickX, Op::Changes, 0},
     follow(Out::Servo, Sig::StickX), false, {}},
    {"Distance bar", "Closer lights more of the strip", 500, {Sig::Distance, Op::Below, 100},
     oppose(Out::StripLength, Sig::Distance), false, {}},
    {"Air theremin", "Move your hand to change the pitch", 0, {Sig::Distance, Op::Below, 60},
     oppose(Out::Tone, Sig::Distance), false, {}},
    {"Night light", "Warm strip when the room goes dark", 5000, {Sig::Light, Op::Below, 20},
     setTo(Out::StripColor, 30), false, {}},
    {"Motion alarm", "Someone moves: red strip and a beep", 3000, {Sig::Motion, Op::Above, 0},
     setTo(Out::StripColor, 0), true, setTo(Out::Tone, 880)},
    {"Fan for damp", "Humid air closes relay 1", 5000, {Sig::Humidity, Op::Above, 60},
     setTo(Out::Relay1, 1), true, setTo(Out::Led, 200)},
    {"Tilt to servo", "Tip the Cardputer to turn the servo", 500, {Sig::Tilt, Op::Changes, 0},
     follow(Out::Servo, Sig::Tilt), false, {}},
    {"Dark lamp", "Dark room turns the smartLamp on", 10000, {Sig::Light, Op::Below, 20},
     setTo(Out::LampPower, 1), false, {}},
    {"Hot room", "Over 28 C turns the TCL AC on", 30000, {Sig::Temperature, Op::Above, 28},
     setTo(Out::AcTcl, 1), false, {}},
};
inline Rule recipeRule(int index) {
  const Recipe& recipe = kRecipes[index];
  Rule rule;
  strncpy(rule.name, recipe.name, kNameChars);
  rule.name[kNameChars] = '\0';
  rule.holdMs = recipe.holdMs;
  rule.conds = 1;
  rule.cond[0] = recipe.when;
  rule.actions = recipe.second ? 2 : 1;
  rule.action[0] = recipe.first;
  rule.action[1] = recipe.then;
  return rule;
}

// --- What the device pages work out -----------------------------------------------------------
constexpr int kParkingStopCm = 15, kParkingCloseCm = 40;
// Beeps get closer together as something does, and 0 means one steady tone.
inline uint16_t parkingGapMs(int cm) { return cm <= 8 ? 0 : static_cast<uint16_t>(clampTo(cm, 8, 100) * 10); }
inline const char* parkingWord(int cm) { return cm < kParkingStopCm ? "Stop!" : cm < kParkingCloseCm ? "Getting close" : "All clear"; }

constexpr int kComfyLowC = 18, kComfyHighC = 27, kComfyLowHumidity = 30, kComfyHighHumidity = 60;
inline const char* comfortWord(int tempC, int humidity) {
  if (tempC < kComfyLowC) return "Chilly";
  if (tempC > kComfyHighC) return "Warm";
  if (humidity < kComfyLowHumidity) return "Dry air";
  if (humidity > kComfyHighHumidity) return "Damp";
  return "Comfortable";
}
// A rule of thumb, close enough for a room, and it needs no floating point.
inline int dewPoint(int tempC, int humidity) { return tempC - (100 - clampTo(humidity, 0, 100)) / 5; }
inline const char* skyWord(int percent) { return percent < 15 ? "Night" : percent < 40 ? "Dim" : percent < 75 ? "Daylight" : "Bright sun"; }
constexpr int kSteadyZone = 10;  // How far from the middle of the zone still counts, in percent.

// The last minute of every sensor, in a byte each, for the Dashboard's small graphs. A point is 0
// with no reading, or 1 to 255 across that sensor's range.
constexpr int kTrendPoints = 30;
class Trends {
 public:
  void sample(const Inputs& inputs) {
    for (uint8_t i = 0; i < kSigCount; ++i) {
      const Reading& reading = inputs.get(static_cast<Sig>(i));
      mPoints[i][mAt] = reading.live ? static_cast<uint8_t>(1 + mapRange(reading.value, kSigs[i].lo, kSigs[i].hi, 0, 254)) : 0;
    }
    mAt = static_cast<uint8_t>((mAt + 1) % kTrendPoints);
  }
  // Oldest first.
  uint8_t point(Sig sig, int index) const { return mPoints[static_cast<uint8_t>(sig)][(mAt + index) % kTrendPoints]; }
  void clear() { *this = Trends{}; }

 private:
  uint8_t mPoints[kSigCount][kTrendPoints] = {};
  uint8_t mAt = 0;
};

// --- Rules that arrive from Claude ------------------------------------------------------------
// Everything here came from a language model over the network, so a word that doesn't match is
// a refusal, not a guess. A rule that gets through still waits for a press of Enter.
inline bool sigFromKey(const char* key, Sig& out) {
  for (uint8_t i = 0; key && i < kSigCount; ++i) {
    if (strcmp(key, kSigs[i].key) == 0) {
      out = static_cast<Sig>(i);
      return true;
    }
  }
  return false;
}
inline bool outFromKey(const char* key, Out& out) {
  for (uint8_t i = 0; key && i < kOutCount; ++i) {
    if (strcmp(key, kOuts[i].key) == 0) {
      out = static_cast<Out>(i);
      return true;
    }
  }
  return false;
}
// Keeps what the small screen and a header can both carry, up to kNameChars.
inline void cleanName(const char* text, char* out) {
  size_t length = 0;
  for (const char* c = text; c && *c && length < kNameChars; ++c) {
    const bool word = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ';
    if (word) out[length++] = *c;
  }
  out[length] = '\0';
}
class Draft {
 public:
  Rule rule;
  bool ok = true;

  void name(const char* text) { cleanName(text, rule.name); }
  void hold(int ms) { rule.holdMs = static_cast<uint16_t>(clampTo<int>(ms, 0, kMaxHoldMs)); }
  void addCond(const char* sensor, const char* test, int value) {
    Cond cond;
    if (rule.conds >= kMaxConds || !sigFromKey(sensor, cond.sig) || !test) { ok = false; return; }
    const SigInfo& s = info(cond.sig);
    if (cond.sig == Sig::Spoken && strcmp(test, "on") != 0 && strcmp(test, "off") != 0) { ok = false; return; }
    if (strcmp(test, "changes") == 0) cond.op = Op::Changes;
    else if (strcmp(test, "on") == 0 && s.onOff) setOnOff(cond, true);
    else if (strcmp(test, "off") == 0 && s.onOff) setOnOff(cond, false);
    else if ((strcmp(test, "under") == 0 || strcmp(test, "over") == 0) && !s.onOff) {
      cond.op = test[0] == 'u' ? Op::Below : Op::Above;
      cond.value = clampTo<int16_t>(static_cast<int16_t>(clampTo<int>(value, -32000, 32000)), s.lo, s.hi);
    } else {
      ok = false;
      return;
    }
    rule.cond[rule.conds++] = cond;
  }
  void addSay(const char* text) {
    size_t length = 0;
    for (const char* c = text; c && *c && length < kSayChars; ++c) {
      if (sayAllowed(*c) && (length || *c != ' ')) rule.say[length++] = *c;
    }
    while (length && rule.say[length - 1] == ' ') --length;
    rule.say[length] = '\0';
    if (!length || rule.actions >= kMaxActions || hasSay(rule)) { ok = false; return; }
    Action action;
    action.out = Out::Say;
    action.how = How::Set;
    action.value = 1;
    rule.action[rule.actions++] = action;
  }
  // By name, because Claude can't know the number, and a name that isn't saved is a refusal.
  void addPlay(const char* name, const Sequence* seqs, uint8_t count) {
    const int index = findByName(seqs, count, name);
    if (index < 0 || rule.actions >= kMaxActions) { ok = false; return; }
    Action action;
    action.out = Out::Sequence;
    action.how = How::Set;
    action.value = static_cast<int16_t>(index);
    rule.action[rule.actions++] = action;
  }
  void addSet(const char* output, int value) {
    Action action;
    if (rule.actions >= kMaxActions || !outFromKey(output, action.out) || action.out == Out::Say || action.out == Out::Sequence) { ok = false; return; }
    const OutInfo& o = info(action.out);
    action.how = How::Set;
    action.value = clampTo<int16_t>(static_cast<int16_t>(clampTo<int>(value, -32000, 32000)), o.lo, o.hi);
    rule.action[rule.actions++] = action;
  }
  void addFollow(const char* output, const char* sensor, bool backwards) {
    Action action;
    if (rule.actions >= kMaxActions || !outFromKey(output, action.out) || !sigFromKey(sensor, action.source) ||
        info(action.out).onOff || action.source == Sig::Spoken) {  // Say is on or off too, and a name has no value.
      ok = false;
      return;
    }
    action.how = backwards ? How::FollowBack : How::Follow;
    rule.action[rule.actions++] = action;
  }
  bool usable() const { return ok && rule.conds > 0 && rule.actions > 0; }
};

// Fills a fixed buffer and stops at its end, so a long description can only come up short.
class Text {
 public:
  Text(char* out, size_t size) : mOut(out), mSize(size) {
    if (size) out[0] = '\0';
  }
  Text& add(const char* text) {
    for (; text && *text && mAt + 1 < mSize; ++text) mOut[mAt++] = *text;
    if (mSize) mOut[mAt] = '\0';
    return *this;
  }
  Text& add(int value) {
    char digits[12];
    snprintf(digits, sizeof(digits), "%d", value);
    return add(digits);
  }
  size_t length() const { return mAt; }
  size_t room() const { return mSize > mAt + 1 ? mSize - mAt - 1 : 0; }

 private:
  char* mOut;
  size_t mSize, mAt = 0;
};

// A rule in the shape Claude writes one, so it can be handed back to be changed.
inline void ruleJson(const Rule& rule, Text& out, const Sequence* seqs = nullptr, uint8_t seqCount = 0) {
  out.add("{\"name\":\"").add(rule.name).add("\",\"any\":").add(rule.any ? "true" : "false");
  out.add(",\"hold_ms\":").add(static_cast<int>(rule.holdMs)).add(",\"when\":[");
  for (uint8_t i = 0; i < rule.conds; ++i) {
    const Cond& cond = rule.cond[i];
    out.add(i ? ",{\"sensor\":\"" : "{\"sensor\":\"").add(info(cond.sig).key).add("\",\"test\":\"");
    if (cond.op == Op::Changes) out.add("changes\"}");
    else if (info(cond.sig).onOff) out.add(cond.op == Op::Above ? "on\"}" : "off\"}");
    else out.add(cond.op == Op::Below ? "under" : "over").add("\",\"value\":").add(cond.value).add("}");
  }
  out.add("],\"do\":[");
  for (uint8_t i = 0; i < rule.actions; ++i) {
    const Action& action = rule.action[i];
    out.add(i ? ",{\"output\":\"" : "{\"output\":\"").add(info(action.out).key).add("\",");
    if (action.out == Out::Say) out.add("\"text\":\"").add(rule.say).add("\"}");
    else if (action.out == Out::Sequence) out.add("\"play\":\"").add(action.value >= 0 && action.value < seqCount ? seqs[action.value].name : "").add("\"}");
    else if (action.how == How::Set) out.add("\"set\":").add(action.value).add("}");
    else out.add(action.how == How::Follow ? "\"follow\":\"" : "\"oppose\":\"").add(info(action.source).key).add("\"}");
  }
  out.add("]}");
}

// The same facts as ruleJson in far fewer letters, for the chat, which gets them with every message.
inline void ruleLine(const Rule& rule, Text& out, const Sequence* seqs, uint8_t seqCount) {
  out.add(rule.name).add(rule.on ? ": " : " (switched off): ").add(rule.any ? "any " : "all ");
  for (uint8_t i = 0; i < rule.conds; ++i) {
    const Cond& cond = rule.cond[i];
    out.add(i ? ", " : "").add(info(cond.sig).key);
    if (cond.op == Op::Changes) out.add(" changes");
    else if (info(cond.sig).onOff) out.add(cond.op == Op::Above ? " on" : " off");
    else out.add(cond.op == Op::Below ? " under " : " over ").add(cond.value);
  }
  out.add("; do ");
  for (uint8_t i = 0; i < rule.actions; ++i) {
    const Action& action = rule.action[i];
    out.add(i ? ", " : "").add(info(action.out).key);
    if (action.out == Out::Say) out.add(" text \"").add(rule.say).add("\"");
    else if (action.out == Out::Sequence) out.add(" play ").add(action.value >= 0 && action.value < seqCount ? seqs[action.value].name : "");
    else if (action.how == How::Set) out.add(" set ").add(action.value);
    else out.add(action.how == How::Follow ? " follow " : " oppose ").add(info(action.source).key);
  }
  out.add("; hold ").add(static_cast<int>(rule.holdMs));
}
inline void sequenceLine(const Sequence& seq, Text& out) {
  out.add(seq.name).add(seq.loop ? " (loops): " : ": ");
  for (uint8_t i = 0; i < seq.count; ++i) {
    const SeqStep& step = seq.steps[i];
    out.add(i ? ", " : "");
    if (step.kind == SeqKind::Wait) out.add("wait_ms ").add(static_cast<int>(step.value) * 100);
    else if (step.kind == SeqKind::Off) out.add(info(step.out).key).add(" off");
    else out.add(info(step.out).key).add(" set ").add(step.value);
  }
}
// Everything saved, and the room it needs, which is worked out rather than fixed because this is
// made on every chat message and a full Lab is about five times what a usual one needs.
constexpr size_t kChatHeadChars = 64, kChatRuleChars = 240, kChatSeqChars = 32, kChatStepChars = 24;
inline size_t chatChars(uint8_t ruleCount, const Sequence* seqs, uint8_t seqCount) {
  size_t chars = kChatHeadChars + ruleCount * kChatRuleChars + seqCount * kChatSeqChars;
  for (uint8_t i = 0; i < seqCount; ++i) chars += seqs[i].count * kChatStepChars;
  return chars;
}
inline void chatState(const Rule* rules, uint8_t ruleCount, const Sequence* seqs, uint8_t seqCount, Text& out) {
  out.add(ruleCount ? "saved rules:\n" : "saved rules: none\n");
  for (uint8_t i = 0; i < ruleCount; ++i) {
    ruleLine(rules[i], out, seqs, seqCount);
    out.add("\n");
  }
  out.add(seqCount ? "saved sequences:\n" : "saved sequences: none\n");
  for (uint8_t i = 0; i < seqCount; ++i) {
    sequenceLine(seqs[i], out);
    out.add("\n");
  }
}

// --- The diary ---------------------------------------------------------------------------------
// What happened in an hour, in one line, because a day of these has to fit in a question to Claude.
constexpr size_t kDiaryLineChars = 384;  // With every sensor at its widest.
class Diary {
 public:
  void note(const Inputs& inputs) {
    mNoted = true;
    for (uint8_t i = 0; i < kSigCount; ++i) {
      const Reading& reading = inputs.get(static_cast<Sig>(i));
      if (!reading.live || kSigs[i].onOff) continue;
      if (!mSeen[i] || reading.value < mLow[i]) mLow[i] = reading.value;
      if (!mSeen[i] || reading.value > mHigh[i]) mHigh[i] = reading.value;
      mSeen[i] = true;
    }
  }
  void count(uint16_t motions, uint16_t fires) {
    mMotions = static_cast<uint16_t>(mMotions + motions);
    mFires = static_cast<uint16_t>(mFires + fires);
  }
  bool empty() const { return !mNoted; }
  void write(const char* hour, Text& out) const {
    out.add(hour).add(": motion ").add(static_cast<int>(mMotions)).add(", rules fired ").add(static_cast<int>(mFires));
    for (uint8_t i = 0; i < kSigCount; ++i) {
      if (mSeen[i]) out.add(", ").add(kSigs[i].key).add(" ").add(mLow[i]).add(" to ").add(mHigh[i]);
    }
    out.add("\n");
  }
  void clear() { *this = Diary{}; }

 private:
  bool mNoted = false, mSeen[kSigCount] = {};
  int16_t mLow[kSigCount] = {}, mHigh[kSigCount] = {};
  uint16_t mMotions = 0, mFires = 0;
};

// --- The I/O hub's six ports ------------------------------------------------------------------
// Nothing on a hub port can say what it is, so the owner says, and the Lab remembers.
enum class Part : uint8_t { None, Knob, Light, Motion, Relay, Servo, Strip, Count };
constexpr uint8_t kPartCount = static_cast<uint8_t>(Part::Count);
constexpr const char* kPartNames[kPartCount] = {"Nothing", "Mini Angle", "Light", "PIR motion", "2 relays", "Servo",
                                                "LED strip"};

struct Wiring {
  Part port[pbhub::kPorts] = {Part::Knob, Part::Light, Part::Motion, Part::Relay, Part::Servo, Part::Strip};

  int8_t portOf(Part part) const {
    for (uint8_t i = 0; i < pbhub::kPorts; ++i) if (port[i] == part) return static_cast<int8_t>(i);
    return -1;
  }
  // Puts a part on a port, and whatever was there takes the port it came from, so nothing is lost.
  void moveTo(uint8_t target, Part part) {
    const int8_t from = portOf(part);
    const Part there = port[target];
    port[target] = part;
    if (from >= 0 && from != target) port[from] = there;
  }
  // Steps one port to the next part that isn't on another port, so a change never touches any
  // row but its own. To swap two parts, set one to Nothing first.
  void cycle(uint8_t target, int delta) {
    int next = static_cast<int>(port[target]);
    do {
      next = (next + delta + kPartCount) % kPartCount;
    } while (next != 0 && portOf(static_cast<Part>(next)) >= 0);
    port[target] = static_cast<Part>(next);
  }
};

// --- Parts straight on the Grove port ----------------------------------------------------------
// Without the I/O hub, a part can use the Grove port's own two wires, which are the ones I2C units
// need, so this only works with no I2C unit attached. White is G1, which reads analog, and
// yellow is G2. Each wire has one job, so through the 1-to-3 hub a knob on white can drive a servo
// on yellow. The relay unit needs both.
struct Direct {
  Part white = Part::None, yellow = Part::None;

  static constexpr bool usesWhite(Part part) {
    return part == Part::Knob || part == Part::Light || part == Part::Motion || part == Part::Relay;
  }
  static constexpr bool usesYellow(Part part) { return part == Part::Servo || part == Part::Strip || part == Part::Relay; }
  bool any() const { return white != Part::None || yellow != Part::None; }
  bool has(Part part) const { return part != Part::None && (white == part || yellow == part); }
  // The newest part on a wire takes it. Replacing half of the relay unit removes all of it.
  void claim(Part part) {
    if (usesWhite(part) && yellow == Part::Relay) yellow = Part::None;
    if (usesYellow(part) && white == Part::Relay) white = Part::None;
    if (usesWhite(part)) white = part;
    if (usesYellow(part)) yellow = part;
  }
};

constexpr uint16_t kDirectKnobTop = 2800;   // What the ADC reads for the Mini Angle's top, about 2.5 V.
constexpr uint16_t kDirectLightTop = 4095;

// A 50 Hz pulse from 500 to 2500 microseconds, as a 14-bit duty, which is what the I/O hub's chip
// sends for the same angle.
constexpr uint32_t kServoHz = 50;
constexpr uint8_t kServoBits = 14;
inline uint32_t servoDuty(int degrees) {
  const uint32_t pulseUs = 500U + static_cast<uint32_t>(clampTo(degrees, 0, 180)) * 2000U / 180U;
  return pulseUs * (1UL << kServoBits) / (1000000UL / kServoHz);
}

// SK6812 data for `leds` colors, green first, in 100 ns ticks. These timings are inside both the
// SK6812 and WS2812 limits. Symbol needs level0, duration0, level1 and duration1.
constexpr int kStripBits = 24;
template <class Symbol>
void encodeStrip(const uint8_t (*rgb)[3], int leds, Symbol* out) {
  for (int led = 0; led < leds; ++led) {
    const uint8_t bytes[3] = {rgb[led][1], rgb[led][0], rgb[led][2]};
    for (uint8_t byte : bytes) {
      for (uint8_t mask = 0x80; mask; mask >>= 1, ++out) {
        const bool one = byte & mask;
        out->level0 = 1;
        out->duration0 = one ? 7 : 3;
        out->level1 = 0;
        out->duration1 = one ? 5 : 9;
      }
    }
  }
}

// --- Everything the Lab lists -----------------------------------------------------------------
enum class Dev : uint8_t { Ultrasonic, Env, Joystick, Knob, Light, Motion, Relay, Servo, Strip, Hub, Nfc, Gps, Tilt,
                           Led, Speaker, Lamp, Ac, Count };
constexpr uint8_t kDevCount = static_cast<uint8_t>(Dev::Count);
constexpr const char* kDevNames[kDevCount] = {"Ultrasonic", "ENV III", "Joystick 2", "Mini Angle", "Light", "PIR motion",
                                              "2 relays", "Servo", "LED strip", "I/O hub", "NFC", "LoRa Cap GPS",
                                              "Tilt", "Cardputer LED", "Speaker", "smartLamp", "IR for AC"};

// I2C units found by asking. The Light unit is analog, but the digital one (DLight) answers here.
enum class Unit : uint8_t { Ultrasonic, Env, Joystick, Hub, Nfc, DLight, Count };
constexpr uint8_t kUnitCount = static_cast<uint8_t>(Unit::Count);
constexpr uint8_t kUnitAddresses[kUnitCount] = {0x57, 0x44, 0x63, pbhub::kAddress, 0x50, 0x23};
constexpr const char* kUnitNames[kUnitCount] = {"Ultrasonic", "ENV III", "Joystick 2", "I/O hub", "NFC", "DLight"};

inline Part partFor(Dev dev) {
  switch (dev) {
    case Dev::Knob: return Part::Knob;
    case Dev::Light: return Part::Light;
    case Dev::Motion: return Part::Motion;
    case Dev::Relay: return Part::Relay;
    case Dev::Servo: return Part::Servo;
    case Dev::Strip: return Part::Strip;
    default: return Part::None;
  }
}

// A knob's top depends on the unit and the hub, so the top seen so far is the top.
struct AnalogRange {
  uint16_t top = 512;
  uint8_t percent(uint16_t raw) {
    if (raw > top) top = raw;
    return static_cast<uint8_t>((static_cast<uint32_t>(raw) * 100U + top / 2) / top);
  }
};

// The Mini Angle's voltage falls as it turns clockwise, so the number has to be turned around for
// clockwise to read as up, on the screen and in every rule that follows it. The owner found this
// on the real part.
inline int16_t knobPercent(AnalogRange& range, uint16_t raw) { return static_cast<int16_t>(100 - range.percent(raw)); }

// Hue is 0 to 359, and the color comes out at full strength.
inline void hueToRgb(int hue, uint8_t& r, uint8_t& g, uint8_t& b) {
  hue = ((hue % 360) + 360) % 360;
  const int sector = hue / 60, rise = (hue % 60) * 255 / 60, fall = 255 - rise;
  const uint8_t up = static_cast<uint8_t>(rise), down = static_cast<uint8_t>(fall);
  const uint8_t rows[6][3] = {{255, up, 0}, {down, 255, 0}, {0, 255, up}, {0, down, 255}, {up, 0, 255}, {255, 0, down}};
  r = rows[sector][0];
  g = rows[sector][1];
  b = rows[sector][2];
}

// What the strip should show. The cap is a share of full power, so nothing can ask for more.
struct StripFrame {
  bool on = false;
  uint8_t r = 0, g = 0, b = 0, lit = 0;
  bool operator==(const StripFrame& o) const { return on == o.on && r == o.r && g == o.g && b == o.b && lit == o.lit; }
  bool operator!=(const StripFrame& o) const { return !(*this == o); }
};
constexpr uint8_t kDefaultCapPercent = 25;
constexpr uint8_t kCaps[3] = {25, 50, 100};
constexpr int kStripRestHue = 200;

inline StripFrame stripFrame(const Drive* drive, uint8_t capPercent) {
  const Drive& color = drive[static_cast<uint8_t>(Out::StripColor)];
  const Drive& bright = drive[static_cast<uint8_t>(Out::StripBright)];
  const Drive& length = drive[static_cast<uint8_t>(Out::StripLength)];
  StripFrame frame;
  if (!color.on && !bright.on && !length.on) return frame;
  const int percent = (bright.on ? clampTo<int>(bright.value, 0, 100) : 100) * clampTo<int>(capPercent, 0, 100) / 100;
  frame.lit = static_cast<uint8_t>(length.on ? clampTo<int>(length.value, 0, kStripLeds) : kStripLeds);
  frame.on = frame.lit > 0 && percent > 0;
  if (!frame.on) return StripFrame{};
  hueToRgb(color.on ? color.value : kStripRestHue, frame.r, frame.g, frame.b);
  frame.r = static_cast<uint8_t>(frame.r * percent / 100);
  frame.g = static_cast<uint8_t>(frame.g * percent / 100);
  frame.b = static_cast<uint8_t>(frame.b * percent / 100);
  return frame;
}

// A relay is mechanical and a sensor can flicker, so it may only change this often.
constexpr uint32_t kRelayGapMs = 300;
class RelayGuard {
 public:
  // Whether to pass a new state on now. A held-back change is asked for again next pass.
  bool allow(bool wanted, uint32_t now) {
    if (mKnown && wanted == mState) return false;
    if (mKnown && now - mChangedMs < kRelayGapMs) return false;
    mKnown = true;
    mState = wanted;
    mChangedMs = now;
    return true;
  }
  void forget() { mKnown = false; }
  bool state() const { return mKnown && mState; }

 private:
  bool mKnown = false, mState = false;
  uint32_t mChangedMs = 0;
};

// The lamp and the air conditioner are shared with the room, so unlike a relay they're left alone
// until a rule asks, and only an "on" that a rule sent gets undone. They also take longer to obey.
class RemoteSwitch {
 public:
  // Whether to send now, and what. A change that's too soon is asked for again next pass.
  bool update(bool driven, bool wanted, uint32_t now, uint32_t gapMs, bool& send) {
    if (mEver && now - mSentMs < gapMs) return false;
    if (driven) {
      if (mHeld && wanted == mState) return false;
      mHeld = true;
      mState = wanted;
    } else {
      if (!mHeld) return false;
      mHeld = false;
      if (!mState) return false;
      mState = false;
    }
    mEver = true;
    mSentMs = now;
    send = mState;
    return true;
  }
  // Leaving can't wait for the gap. Says whether an "off" has to go out now.
  bool release() {
    const bool undo = mHeld && mState;
    mHeld = false;
    mState = false;
    return undo;
  }

 private:
  bool mHeld = false, mState = false, mEver = false;
  uint32_t mSentMs = 0;
};

// --- Saving ----------------------------------------------------------------------------------
// The file holds only these numbers, and every one is checked on the way back in, so a damaged
// card can't point a rule at something that doesn't exist.
constexpr uint8_t kFileMagic[3] = {'S', 'L', 'B'};
constexpr uint8_t kFileVersion = 3;     // Version 1 had no Direct and 2 no phrases, and both still load.
constexpr size_t kOldRuleBytes = kNameChars + 1 + 6 + kMaxConds * 4 + kMaxActions * 5;
constexpr size_t kRuleBytes = kOldRuleBytes + kSayChars + 1;
constexpr size_t kOldHeaderBytes = 4 + 1 + pbhub::kPorts + 3;
constexpr size_t kHeaderBytes = kOldHeaderBytes + 2;
constexpr size_t kFileBytes = kHeaderBytes + kMaxRules * kRuleBytes + 2;

struct Book {
  Rule rules[kMaxRules];
  uint8_t count = 0;
  Wiring wiring;
  uint8_t capPercent = kDefaultCapPercent;
  bool lightFlipped = true;   // Most units read lower as it gets brighter.
  uint8_t showMode = 0;
  Direct direct;
};

inline uint16_t checksum(const uint8_t* bytes, size_t length) {
  uint16_t low = 1, high = 0;
  for (size_t i = 0; i < length; ++i) {
    low = static_cast<uint16_t>((low + bytes[i]) % 251);
    high = static_cast<uint16_t>((high + low) % 251);
  }
  return static_cast<uint16_t>((high << 8) | low);
}

inline size_t encode(const Book& book, uint8_t* out) {
  size_t at = 0;
  auto put = [&](uint8_t value) { out[at++] = value; };
  auto put16 = [&](int16_t value) {
    put(static_cast<uint8_t>(value & 0xFF));
    put(static_cast<uint8_t>((value >> 8) & 0xFF));
  };
  for (uint8_t b : kFileMagic) put(b);
  put(kFileVersion);
  put(book.count);
  for (Part part : book.wiring.port) put(static_cast<uint8_t>(part));
  put(book.capPercent);
  put(book.lightFlipped ? 1 : 0);
  put(book.showMode);
  put(static_cast<uint8_t>(book.direct.white));
  put(static_cast<uint8_t>(book.direct.yellow));
  for (uint8_t i = 0; i < book.count; ++i) {
    const Rule& rule = book.rules[i];
    for (uint8_t c = 0; c <= kNameChars; ++c) put(static_cast<uint8_t>(rule.name[c]));
    put(rule.on ? 1 : 0);
    put(rule.any ? 1 : 0);
    put(rule.conds);
    put(rule.actions);
    put16(static_cast<int16_t>(rule.holdMs));
    for (const Cond& cond : rule.cond) {
      put(static_cast<uint8_t>(cond.sig));
      put(static_cast<uint8_t>(cond.op));
      put16(cond.value);
    }
    for (const Action& action : rule.action) {
      put(static_cast<uint8_t>(action.out));
      put(static_cast<uint8_t>(action.how));
      put16(action.value);
      put(static_cast<uint8_t>(action.source));
    }
    for (uint8_t c = 0; c <= kSayChars; ++c) put(static_cast<uint8_t>(rule.say[c]));
  }
  const uint16_t sum = checksum(out, at);
  put(static_cast<uint8_t>(sum & 0xFF));
  put(static_cast<uint8_t>(sum >> 8));
  return at;
}

inline bool decode(const uint8_t* bytes, size_t length, Book& book) {
  if (length < 6 || length > kFileBytes) return false;
  if (memcmp(bytes, kFileMagic, sizeof(kFileMagic)) != 0) return false;
  const uint8_t version = bytes[3];
  if (version < 1 || version > kFileVersion) return false;
  const size_t headerBytes = version == 1 ? kOldHeaderBytes : kHeaderBytes;
  const size_t ruleBytes = version >= 3 ? kRuleBytes : kOldRuleBytes;
  if (length < headerBytes + 2) return false;
  const uint16_t sum = static_cast<uint16_t>(bytes[length - 2] | (bytes[length - 1] << 8));
  if (checksum(bytes, length - 2) != sum) return false;
  const uint8_t count = bytes[4];
  if (count > kMaxRules || length != headerBytes + count * ruleBytes + 2) return false;
  Book in;
  in.count = count;
  size_t at = 5;
  auto get = [&]() { return bytes[at++]; };
  auto get16 = [&]() {
    const int16_t value = static_cast<int16_t>(bytes[at] | (bytes[at + 1] << 8));
    at += 2;
    return value;
  };
  bool seen[kPartCount] = {};
  for (Part& part : in.wiring.port) {
    const uint8_t value = get();
    if (value >= kPartCount || (value != 0 && seen[value])) return false;
    seen[value] = true;
    part = static_cast<Part>(value);
  }
  const uint8_t cap = get();
  if (cap != kCaps[0] && cap != kCaps[1] && cap != kCaps[2]) return false;
  in.capPercent = cap;
  in.lightFlipped = get() != 0;
  in.showMode = get() ? 1 : 0;
  if (version >= 2) {
    const uint8_t white = get(), yellow = get();
    if (white >= kPartCount || yellow >= kPartCount) return false;
    const Part onWhite = static_cast<Part>(white), onYellow = static_cast<Part>(yellow);
    const bool bothRelay = onWhite == Part::Relay && onYellow == Part::Relay;
    if ((onWhite != Part::None && !Direct::usesWhite(onWhite)) || (onYellow != Part::None && !Direct::usesYellow(onYellow))) return false;
    if ((onWhite == Part::Relay || onYellow == Part::Relay) && !bothRelay) return false;
    in.direct.white = onWhite;
    in.direct.yellow = onYellow;
  }
  for (uint8_t i = 0; i < count; ++i) {
    Rule& rule = in.rules[i];
    for (uint8_t c = 0; c <= kNameChars; ++c) rule.name[c] = static_cast<char>(get());
    rule.name[kNameChars] = '\0';
    rule.on = get() != 0;
    rule.any = get() != 0;
    rule.conds = get();
    rule.actions = get();
    rule.holdMs = static_cast<uint16_t>(get16());
    if (rule.conds > kMaxConds || rule.actions > kMaxActions || rule.holdMs > kMaxHoldMs) return false;
    for (Cond& cond : rule.cond) {
      const uint8_t sig = get(), op = get();
      cond.value = get16();
      if (sig >= kSigCount || op > static_cast<uint8_t>(Op::Changes)) return false;
      cond.sig = static_cast<Sig>(sig);
      cond.op = static_cast<Op>(op);
    }
    for (Action& action : rule.action) {
      const uint8_t out = get(), how = get();
      action.value = get16();
      const uint8_t source = get();
      if (out >= kOutCount || how > static_cast<uint8_t>(How::FollowBack) || source >= kSigCount) return false;
      action.out = static_cast<Out>(out);
      action.how = static_cast<How>(how);
      action.source = static_cast<Sig>(source);
    }
    if (version >= 3) {
      for (uint8_t c = 0; c <= kSayChars; ++c) {
        rule.say[c] = static_cast<char>(get());
        if (rule.say[c] && !sayAllowed(rule.say[c])) return false;
      }
      rule.say[kSayChars] = '\0';
    }
  }
  book = in;
  return true;
}

// Sequences keep to a file of their own, with only the steps each one uses, so rules.dat stays as
// it was and more steps can fit one day without changing this.
constexpr uint8_t kSeqFileMagic[3] = {'S', 'L', 'Q'};
constexpr uint8_t kSeqFileVersion = 1;
constexpr size_t kSeqStepBytes = 4;
constexpr size_t kSeqFileBytes = 5 + kMaxSequences * (kNameChars + 3 + kMaxSeqSteps * kSeqStepBytes) + 2;

inline size_t encodeSequences(const Sequence* seqs, uint8_t count, uint8_t* out) {
  size_t at = 0;
  auto put = [&](uint8_t value) { out[at++] = value; };
  for (uint8_t b : kSeqFileMagic) put(b);
  put(kSeqFileVersion);
  put(count);
  for (uint8_t i = 0; i < count; ++i) {
    const Sequence& seq = seqs[i];
    for (uint8_t c = 0; c <= kNameChars; ++c) put(static_cast<uint8_t>(seq.name[c]));
    put(seq.loop ? 1 : 0);
    put(seq.count);
    for (uint8_t s = 0; s < seq.count; ++s) {
      const SeqStep& step = seq.steps[s];
      put(static_cast<uint8_t>(step.kind));
      put(static_cast<uint8_t>(step.out));
      put(static_cast<uint8_t>(step.value & 0xFF));
      put(static_cast<uint8_t>((step.value >> 8) & 0xFF));
    }
  }
  const uint16_t sum = checksum(out, at);
  put(static_cast<uint8_t>(sum & 0xFF));
  put(static_cast<uint8_t>(sum >> 8));
  return at;
}

inline bool decodeSequences(const uint8_t* bytes, size_t length, Sequence* seqs, uint8_t& count) {
  if (length < 7 || length > kSeqFileBytes) return false;
  if (memcmp(bytes, kSeqFileMagic, sizeof(kSeqFileMagic)) != 0 || bytes[3] != kSeqFileVersion) return false;
  const uint16_t sum = static_cast<uint16_t>(bytes[length - 2] | (bytes[length - 1] << 8));
  if (checksum(bytes, length - 2) != sum) return false;
  const uint8_t saved = bytes[4];
  if (saved > kMaxSequences) return false;
  const size_t end = length - 2;
  size_t at = 5;
  Sequence in[kMaxSequences];
  for (uint8_t i = 0; i < saved; ++i) {
    Sequence& seq = in[i];
    if (at + kNameChars + 3 > end) return false;
    for (uint8_t c = 0; c <= kNameChars; ++c) seq.name[c] = static_cast<char>(bytes[at++]);
    seq.name[kNameChars] = '\0';
    seq.loop = bytes[at++] != 0;
    seq.count = bytes[at++];
    if (seq.count > kMaxSeqSteps || at + seq.count * kSeqStepBytes > end) return false;
    for (uint8_t s = 0; s < seq.count; ++s) {
      const uint8_t kind = bytes[at++], out = bytes[at++];
      if (kind > static_cast<uint8_t>(SeqKind::Wait) || out >= kSeqOutCount) return false;
      SeqStep& step = seq.steps[s];
      step.kind = static_cast<SeqKind>(kind);
      step.out = static_cast<Out>(out);
      step.value = static_cast<int16_t>(bytes[at] | (bytes[at + 1] << 8));
      at += 2;
      if (!stepAllowed(step)) return false;
    }
  }
  if (at != end) return false;
  for (uint8_t i = 0; i < saved; ++i) seqs[i] = in[i];
  count = saved;
  return true;
}
}  // namespace lab
