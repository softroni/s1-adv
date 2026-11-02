#pragma once
// Whether the Cardputer is plugged in and charging, worked out from what the board gives us. Nothing
// here touches hardware.
//
// The ADV can't be asked. The TP4057 charger's CHRG and STDBY pins go nowhere and no pin senses USB
// 5 V, which is why M5's docs say charging status can't be read. Two things are left:
//   - USB frames prove a computer is attached. A wall charger or power bank sends none.
//   - The battery voltage on G10. 5 V switches the load off the battery, so while plugged in this
//     reading can't sag under load. It steps up when the charger takes over and down when it lets go.
//
// So plugged in is only certain with a computer attached. Anything else is a judgment from the
// voltage, and every judgment here can be taken back by the readings that follow it.
//
// Measured on the device on USB: 4196-4208 mV at the top of charge with TLS running, so about +-6 mV
// of noise and no load on the reading, and 3.5 mV a minute mid-charge. The R5 resistor sets about
// 300 mA, roughly 20% an hour. The step sizes below are worked out from that current, not measured:
// about 400 mA of swing across about 0.2 ohm when the load comes off and the charge goes on.
#include <stdint.h>

namespace battery_power {
enum class State : uint8_t {
  Battery,      // Running from the battery.
  Charging,
  Full,         // Plugged in, and the charger has stopped pushing the voltage up.
  NotCharging,  // Plugged in, and the battery isn't taking charge.
};

constexpr uint32_t kMinSpacingMs = 500;
constexpr uint32_t kGapMs = 15000;  // Longer than this and recent readings say nothing about now.
constexpr uint32_t kSettleMs = 45000;  // Boot's Wi-Fi and update check ending looks like a step up.

constexpr int16_t kStepUpMv = 60;  // Charger takes over.
constexpr int16_t kSagMv = 25;  // Load back on the battery, at its smallest on a full one.
constexpr int16_t kComputerGoneSagMv = 12;  // Frames just stopped, so a smaller sag will do.
constexpr uint32_t kComputerGoneMs = 20000;
constexpr int16_t kChargingFallMv = 20;  // A charger never lets the voltage fall.
constexpr int16_t kFullFallMv = 50;  // A finished cell relaxes a little first.
constexpr int16_t kFloatMv = 4185;  // Only a charger holds a cell this high.

constexpr uint32_t kClimbWindowMs = 10UL * 60UL * 1000UL;
constexpr int16_t kClimbMv = 8;  // Twice running: a charger already on when the reading began.
constexpr uint8_t kClimbWindows = 2;

// Readings this far from their neighbours mean load on the battery, or no cell at all: with the power
// switch off the charger has nothing to hold up, so its output rises and falls many times a second.
constexpr int16_t kJumpMv = 30;
constexpr uint8_t kLoadJumps = 3;  // Of the last 16 readings.
constexpr uint8_t kNoCellJumps = 6;
constexpr int16_t kNoCellFloorMv = 3950;
constexpr int16_t kNoBatteryMv = 2500;

constexpr int16_t kFlatMv = 8;
constexpr int16_t kNewLevelMv = 40;  // A move this far down is a new situation, not a plateau.
constexpr int16_t kFullMv = 4050;  // Past here, a flat reading means the charger has stopped pushing.
constexpr int16_t kRechargeMv = 3980;
constexpr uint32_t kFullHoldMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kStallMs = 30UL * 60UL * 1000UL;  // Mid-charge rises at least 0.5 mV a minute.

constexpr uint32_t kUnpluggedNoteMs = 6000;

inline const char* name(State state) {
  switch (state) {
    case State::Charging: return "charging";
    case State::Full: return "full";
    case State::NotCharging: return "not_charging";
    case State::Battery: break;
  }
  return "battery";
}

constexpr uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Look {
  const char* word;
  const char* hint;  // Takes the meter's place when there's something to do about it.
  uint16_t accent;
  bool moving;
};

inline Look look(State state) {
  switch (state) {
    case State::Charging: return {"Charging", nullptr, color(88, 240, 141), true};
    case State::Full: return {"Fully charged", nullptr, color(88, 240, 141), false};
    case State::NotCharging: return {"Not charging", "Power switch on?", color(255, 136, 64), false};
    case State::Battery: break;
  }
  return {"On battery", nullptr, color(47, 227, 255), false};
}

// The percent to show. The reading hits 100 well before the battery does, so 100 waits for Full.
inline int shownLevel(int32_t level, State state) {
  if (state == State::Full) return 100;
  if (level < 0) return -1;
  const int ceiling = state == State::Charging ? 99 : 100;
  return level > ceiling ? ceiling : static_cast<int>(level);
}

class Tracker {
 public:
  // Takes the battery reading and whether USB frames are arriving, about once a second. Returns true
  // when the state changes.
  bool update(uint32_t now, int16_t mv, bool computer) {
    if (mv <= 0 || (started_ && now - lastMs_ < kMinSpacingMs)) return false;
    const State before = state_;
    if (!started_) {
      started_ = true;
      startMs_ = now;
      climbMs_ = now;
      climbMv_ = mv;
    } else if (now - lastMs_ > kGapMs) {
      forgetRecent();
    }
    lastMs_ = now;

    raw_[rawHead_] = mv;
    rawHead_ = (rawHead_ + 1) % kRawCount;
    if (rawFill_ < kRawCount) ++rawFill_;
    const int16_t level = median();
    const int16_t then = settledFill_ >= kSettledMin ? oldestSettled() : level;
    settled_[settledHead_] = level;
    settledHead_ = (settledHead_ + 1) % kSettledCount;
    if (settledFill_ < kSettledCount) ++settledFill_;
    jumps_ = static_cast<uint16_t>((jumps_ << 1) | (distance(mv, level) >= kJumpMv ? 1 : 0));

    if (computer) {
      computerSeen_ = true;
      computerMs_ = now;
    }
    if (!plugged_) {
      if (computer || pluggedFromVoltage(now, level, then)) plugIn(now, level);
    } else if (!computer && unpluggedFromVoltage(now, level, then)) {
      unplug(now, level);
    }
    if (plugged_) classify(now, level, computer);

    if (state_ == before) return false;
    changedMs_ = now;
    leftPower_ = state_ == State::Battery;
    return true;
  }

  State state() const { return state_; }
  bool computerSeen(uint32_t now) const {
    return computerSeen_ && now - computerMs_ <= kComputerGoneMs;
  }
  // There's something to say on Home, including a moment's note that it just came unplugged.
  bool noteVisible(uint32_t now) const {
    return state_ != State::Battery || (leftPower_ && now - changedMs_ < kUnpluggedNoteMs);
  }

 private:
  static constexpr uint8_t kRawCount = 5;
  static constexpr uint8_t kSettledCount = 8;
  static constexpr uint8_t kSettledMin = 3;

  static int16_t distance(int16_t a, int16_t b) {
    return a > b ? static_cast<int16_t>(a - b) : static_cast<int16_t>(b - a);
  }

  int16_t median() const {
    int16_t sorted[kRawCount];
    for (uint8_t i = 0; i < rawFill_; ++i) {
      int16_t value = raw_[i];
      uint8_t slot = i;
      for (; slot > 0 && sorted[slot - 1] > value; --slot) sorted[slot] = sorted[slot - 1];
      sorted[slot] = value;
    }
    return sorted[rawFill_ / 2];
  }

  int16_t oldestSettled() const {
    return settled_[(settledHead_ + kSettledCount - settledFill_) % kSettledCount];
  }

  void forgetRecent() {
    rawFill_ = rawHead_ = 0;
    settledFill_ = settledHead_ = 0;
    jumps_ = 0;
  }

  bool pluggedFromVoltage(uint32_t now, int16_t level, int16_t then) {
    if (now - startMs_ < kSettleMs) {
      climbMs_ = now;
      climbMv_ = level;
      settledFill_ = settledHead_ = 0;  // So no comparison reaches back into boot.
      return false;
    }
    if (level - then >= kStepUpMv || level >= kFloatMv) return true;
    if (now - climbMs_ < kClimbWindowMs) return false;
    climbs_ = level - climbMv_ >= kClimbMv ? climbs_ + 1 : 0;
    climbMs_ = now;
    climbMv_ = level;
    return climbs_ >= kClimbWindows;
  }

  bool unpluggedFromVoltage(uint32_t now, int16_t level, int16_t then) const {
    const bool justLost = computerSeen_ && now - computerMs_ <= kComputerGoneMs;
    if (then - level >= (justLost ? kComputerGoneSagMv : kSagMv)) return true;
    if (__builtin_popcount(jumps_) >= kLoadJumps) return true;
    if (peakMv_ - level >= (state_ == State::Full ? kFullFallMv : kChargingFallMv)) return true;
    // With no computer to vouch for it, a reading going nowhere is a battery at rest.
    return level < kFullMv && now - flatMs_ >= kStallMs;
  }

  void plugIn(uint32_t now, int16_t level) {
    plugged_ = true;
    peakMv_ = level;
    flatMv_ = level;
    flatMs_ = now;
    jumps_ = 0;  // The step itself is not load.
    state_ = State::Charging;
  }

  void unplug(uint32_t now, int16_t level) {
    plugged_ = false;
    state_ = State::Battery;
    jumps_ = 0;
    climbs_ = 0;
    climbMs_ = now;
    climbMv_ = level;
    // So a quick replug still counts as a step up from here.
    settledFill_ = settledHead_ = 0;
  }

  void classify(uint32_t now, int16_t level, bool computer) {
    if (level > peakMv_) peakMv_ = level;
    if (level > flatMv_ + kFlatMv || level < flatMv_ - kNewLevelMv) {
      flatMv_ = level;
      flatMs_ = now;
    }
    const uint32_t flatFor = now - flatMs_;
    // Once seen it stays until 16 readings pass quietly, so the display can't flicker.
    const int needed = state_ == State::NotCharging ? 1 : kNoCellJumps;
    const bool noCell = (__builtin_popcount(jumps_) >= needed && level >= kNoCellFloorMv) ||
                        level < kNoBatteryMv;
    if (noCell) {
      state_ = State::NotCharging;
    } else if (state_ == State::Full && level >= kRechargeMv) {
      return;
    } else if (level >= kFullMv && flatFor >= kFullHoldMs) {
      state_ = State::Full;
    } else if (computer && flatFor >= kStallMs) {
      state_ = State::NotCharging;
    } else {
      if (state_ == State::Full) peakMv_ = level;
      state_ = State::Charging;
    }
  }

  bool started_ = false;
  bool plugged_ = false;
  bool computerSeen_ = false;
  bool leftPower_ = false;
  State state_ = State::Battery;
  uint32_t startMs_ = 0;
  uint32_t lastMs_ = 0;
  uint32_t changedMs_ = 0;
  uint32_t computerMs_ = 0;
  uint32_t flatMs_ = 0;
  uint32_t climbMs_ = 0;
  int16_t raw_[kRawCount] = {};
  int16_t settled_[kSettledCount] = {};
  int16_t peakMv_ = 0;
  int16_t flatMv_ = 0;
  int16_t climbMv_ = 0;
  uint16_t jumps_ = 0;
  uint8_t rawHead_ = 0;
  uint8_t rawFill_ = 0;
  uint8_t settledHead_ = 0;
  uint8_t settledFill_ = 0;
  uint8_t climbs_ = 0;
};
}  // namespace battery_power
