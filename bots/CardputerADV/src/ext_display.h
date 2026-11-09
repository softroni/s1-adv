#pragma once
// A 2.8" ILI9341 screen on the EXT header, showing what the Cardputer's own screen shows, a third
// bigger: 320x180 across the middle of its 320x240, black above and below. It's wired to the pins
// the Cap LoRa-1262 uses: CS G5, RESET G3, DC G6, and the SD card's SCK G40, MOSI G14, MISO G39
// (docs/EXT-SCREEN.md). The Cap and the screen can't both be on the header.
//
// Finding it: two values are written to the screen's MADCTL register and each is read back
// (command 0Bh). Only a live controller hands both back, so an empty header, a loose MISO or an
// RC522 never pass. G6 is the screen's DC, an output from here, so nothing is written while a Cap
// answers on I2C or something holds G6 low (an HC-SR04's ECHO, an awake SX1262's BUSY). A screen
// without MISO can't be found, which is what Settings > Display > Ext screen "on" is for.
//
// Every pin goes through Port, so the rules run on a computer (scripts/test_ext_display.py);
// part 099 is the real one, on the SD card's SPI object, whose lock keeps the two apart.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace ext_display {

// Settings > Display > Ext screen.
enum class Mode : uint8_t { Auto = 0, On = 1, Off = 2 };
constexpr uint8_t kModeCount = 3;
inline Mode modeOrDefault(uint8_t saved) { return saved < kModeCount ? static_cast<Mode>(saved) : Mode::Auto; }
inline const char* modeName(Mode mode, bool shown) {
  if (mode == Mode::On) return "on";
  if (mode == Mode::Off) return "off";
  return shown ? "auto, found" : "auto, none";
}

constexpr uint16_t kWidth = 320, kHeight = 240;              // Landscape.
constexpr uint16_t kSourceWidth = 240, kSourceHeight = 135;  // The Cardputer's own screen.
constexpr uint16_t kPictureHeight = kSourceHeight * 4 / 3;   // 180.
constexpr uint16_t kPictureTop = (kHeight - kPictureHeight) / 2;
static_assert(kSourceWidth * 4 / 3 == kWidth, "a third bigger fills the width");
// The canvas pixel each screen pixel shows, the same way across and down.
inline uint16_t sourceOf(uint16_t out) { return static_cast<uint16_t>(out * 3U / 4U); }

// MADCTL for landscape: with the pin header on the left (MY MX MV BGR), or turned round.
constexpr uint8_t kMadctlHeaderLeft = 0xE8, kMadctlHeaderRight = 0x28;
inline uint8_t madctl(bool upsideDown) { return upsideDown ? kMadctlHeaderRight : kMadctlHeaderLeft; }

namespace cmd {
constexpr uint8_t kReadMadctl = 0x0B, kSleepOut = 0x11, kDisplayOn = 0x29, kColumns = 0x2A, kRows = 0x2B,
                  kWrite = 0x2C, kMadctl = 0x36;
}

// The usual ILI9341 start-up (the values Adafruit_ILI9341 and LovyanGFX send), as command, count,
// parameters, ending in 0. 16-bit pixels; MADCTL and Sleep Out follow.
constexpr uint8_t kSetup[] = {
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x23,
    0xC1, 1, 0x10,
    0xC5, 2, 0x3E, 0x28,
    0xC7, 1, 0x86,
    0x37, 1, 0x00,
    0x3A, 1, 0x55,
    0xB1, 2, 0x00, 0x18,
    0xB6, 3, 0x08, 0x82, 0x27,
    0xF2, 1, 0x00,
    0x26, 1, 0x01,
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    0x00};

// The canvas's 8-bit RGB332 as RGB565, widened the way M5GFX widens it for the Cardputer's screen.
inline uint16_t rgb565(uint8_t c) {
  const unsigned r3 = c >> 5, g3 = (c >> 2) & 7U, b2 = c & 3U;
  const unsigned r8 = ((r3 * 9U) << 2) + (r3 >> 1), g8 = ((g3 * 9U) << 2) + (g3 >> 1), b8 = b2 * 0x55U;
  return static_cast<uint16_t>(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
}

// One canvas row as one screen row, high byte first, the order the screen takes.
inline void scaleRow(const uint8_t* source, uint8_t* out) {
  for (uint16_t x = 0; x < kWidth; ++x) {
    const uint16_t c = rgb565(source[sourceOf(x)]);
    out[2 * x] = static_cast<uint8_t>(c >> 8);
    out[2 * x + 1] = static_cast<uint8_t>(c);
  }
}

// A read of 0Bh, as the 32 bits clocked in after the command. Controllers differ in how many
// dummy clocks come first, so the value may start on any of the first nine bits.
constexpr uint8_t kProbeA = 0xA8, kProbeB = 0x48;
constexpr int kMaxSkip = 8;
inline uint8_t readAt(uint32_t raw, int skip) { return static_cast<uint8_t>(raw >> (24 - skip)); }
// Where both values came back, or -1: nothing there answers.
inline int echoSkip(uint32_t rawA, uint32_t rawB) {
  for (int skip = 0; skip <= kMaxSkip; ++skip)
    if (readAt(rawA, skip) == kProbeA && readAt(rawB, skip) == kProbeB) return skip;
  return -1;
}

constexpr uint32_t kLookMs = 3000;     // How often to look for a screen that isn't there.
constexpr uint32_t kCheckMs = 2000;    // How often to ask a screen that is whether it still is.
constexpr uint32_t kRefreshMs = 5000;  // "on" with nothing to ask: the set-up again, for one plugged in since.
constexpr uint32_t kWakeMs = 120;      // After RESET, and after Sleep Out, before the next step.
constexpr uint32_t kFrameGapMs = 50;   // At most 20 copies a second.
constexpr uint16_t kRowsPerPass = 12;  // About 4 ms at 20 MHz, so a copy never holds the loop long.
constexpr uint8_t kMissesToLose = 2;

struct Frame {
  const uint8_t* pixels = nullptr;  // The canvas, 240x135 RGB332, or nullptr while it's lent or gone.
  uint32_t generation = 0;          // Moves each time the Cardputer's screen gets a new frame.
  bool screenOn = true;             // Off, this screen goes black: its backlight is on the 5 V pin.
};

// Port, supplied by the caller:
//   uint32_t ms();
//   bool capPresent();       a Cap LoRa-1262's I/O expander answers on I2C
//   bool dcHeld();           something holds G6 low against a pull-up (G6 an input at the time)
//   void claim();            CS high, DC an output, RESET driven high (and given time to start)
//   void release();          DC and RESET back to inputs, CS stays high
//   void reset();            a short low pulse on RESET
//   void command(uint8_t c, const uint8_t* data, uint8_t n);
//   uint32_t read32(uint8_t c);
//   void beginPixels(uint16_t y, uint16_t rows);   whole rows from y
//   void pixels(const uint8_t* bytes, size_t n);
//   void blank(uint16_t count);                    black pixels
//   void endPixels();
//   void said(const char* line);
template <class Port>
class Mirror {
 public:
  enum class Phase : uint8_t { Absent, Resetting, Waking, Ready };

  // free: nothing else has the EXT pins just now (Mesh, the RC522 Lab, Sonar's HC-SR04). While
  // they're busy this leaves them alone, and checks the screen as soon as they're back.
  void service(Port& port, Mode mode, bool upsideDown, bool free, const Frame& frame) {
    const uint32_t now = port.ms();
    upsideDown_ = upsideDown;
    if (mode != mode_) {
      if (phase_ != Phase::Absent && free) {
        if (mode == Mode::Off) blackNow(port);  // Its backlight stays on, so leave it dark, not frozen.
        port.release();
        say(port, mode == Mode::Off ? "turned off" : "set up again");
      }
      mode_ = mode;
      phase_ = Phase::Absent;
      lookAtMs_ = now;
      looked_ = false;
      warned_ = false;
    }
    if (mode == Mode::Off) return;
    if (!free) {
      away_ = true;
      return;
    }
    if (away_) {
      away_ = false;
      if (phase_ == Phase::Ready) {
        port.claim();
        checkAtMs_ = now;  // It may have been reset meanwhile: ask now.
      } else if (phase_ != Phase::Absent) {
        phase_ = Phase::Absent;  // Interrupted while starting: start again.
        lookAtMs_ = now;
      }
    }
    switch (phase_) {
      case Phase::Absent:
        if (static_cast<int32_t>(now - lookAtMs_) >= 0) {
          lookAtMs_ = now + kLookMs;
          look(port, now);
        }
        return;
      case Phase::Resetting:
        if (static_cast<int32_t>(now - untilMs_) < 0) return;
        setUp(port);
        phase_ = Phase::Waking;
        untilMs_ = now + kWakeMs;
        return;
      case Phase::Waking:
        if (static_cast<int32_t>(now - untilMs_) < 0) return;
        port.command(cmd::kDisplayOn, nullptr, 0);
        phase_ = Phase::Ready;
        whole_ = true;
        dark_ = false;
        row_ = kIdle;
        checkAtMs_ = now + (canAsk() ? kCheckMs : kRefreshMs);
        if (!started_) say(port, canAsk() ? "showing the Cardputer's screen" : "on, showing the Cardputer's screen");
        started_ = true;
        return;
      case Phase::Ready:
        break;
    }
    if (sent_ != madctl(upsideDown_)) {  // Turned round in the setting.
      turn(port);
      whole_ = true;
      row_ = kIdle;
    }
    if (static_cast<int32_t>(now - checkAtMs_) >= 0 && !check(port, now)) return;
    draw(port, now, frame);
  }

  bool shown() const { return phase_ != Phase::Absent; }
  Phase phase() const { return phase_; }
  int skip() const { return skip_; }

 private:
  static constexpr uint16_t kIdle = 0xFFFF;

  bool canAsk() const { return skip_ >= 0; }

  void say(Port& port, const char* what, uint32_t a = 0, uint32_t b = 0) {
    char line[112];
    snprintf(line, sizeof(line), "[EXT] %s raw=%08lx %08lx skip=%d", what, static_cast<unsigned long>(a),
             static_cast<unsigned long>(b), skip_);
    port.said(line);
  }

  int probe(Port& port, uint32_t& a, uint32_t& b) {
    sent_ = kProbeA;
    port.command(cmd::kMadctl, &sent_, 1);
    a = port.read32(cmd::kReadMadctl);
    sent_ = kProbeB;
    port.command(cmd::kMadctl, &sent_, 1);
    b = port.read32(cmd::kReadMadctl);
    return echoSkip(a, b);
  }

  void turn(Port& port) {
    sent_ = madctl(upsideDown_);
    port.command(cmd::kMadctl, &sent_, 1);
  }

  void look(Port& port, uint32_t now) {
    if (port.capPresent() || port.dcHeld()) {
      if (!warned_) say(port, "a Cap or a sensor is on EXT, not looking");
      warned_ = true;
      return;
    }
    warned_ = false;
    port.claim();
    uint32_t a = 0, b = 0;
    skip_ = probe(port, a, b);
    // Once a start and once a setting change, what came back, so a screen that won't answer can be told apart.
    if (!looked_ || skip_ >= 0) say(port, skip_ >= 0 ? "found an ILI9341" : "looked, nothing answered", a, b);
    looked_ = true;
    if (skip_ < 0 && mode_ == Mode::Auto) {
      port.release();
      return;
    }
    start(port, now);
  }

  void start(Port& port, uint32_t now) {
    port.reset();
    phase_ = Phase::Resetting;
    untilMs_ = now + kWakeMs;
    misses_ = 0;
    row_ = kIdle;
  }

  void setUp(Port& port) {
    for (const uint8_t* p = kSetup; *p; p += 2 + p[1]) port.command(p[0], p + 2, p[1]);
    turn(port);
    port.command(cmd::kSleepOut, nullptr, 0);
  }

  // False when the screen was lost or is starting again.
  bool check(Port& port, uint32_t now) {
    checkAtMs_ = now + (canAsk() ? kCheckMs : kRefreshMs);
    if (port.capPresent()) {  // Swapped for a Cap while running. G6 is this one's output now, so it can't be asked.
      port.release();
      phase_ = Phase::Absent;
      lookAtMs_ = now + kLookMs;
      started_ = false;
      say(port, "a Cap is on EXT now, let go");
      return false;
    }
    if (canAsk() && readAt(port.read32(cmd::kReadMadctl), skip_) == madctl(upsideDown_)) {
      misses_ = 0;
      return true;
    }
    if (!canAsk()) {
      // "on" and it can't be asked: the set-up again, no reset, so one plugged in since wakes too.
      setUp(port);
      phase_ = Phase::Waking;
      untilMs_ = now + kWakeMs;
      return false;
    }
    // Not as it was set up: it restarted (its power or RESET), or it's gone.
    uint32_t a = 0, b = 0;
    const int skip = probe(port, a, b);
    if (skip >= 0) {
      skip_ = skip;
      say(port, "restarted, setting it up again", a, b);
      start(port, now);
      return false;
    }
    turn(port);
    if (mode_ == Mode::On || ++misses_ < kMissesToLose) return true;
    port.release();
    phase_ = Phase::Absent;
    lookAtMs_ = now + kLookMs;
    started_ = false;
    say(port, "gone", a, b);
    return false;
  }

  void blackNow(Port& port) {
    for (uint16_t y = 0; y < kHeight; y += kRowsPerPass) {
      port.beginPixels(y, kRowsPerPass);
      for (uint16_t i = 0; i < kRowsPerPass; ++i) port.blank(kWidth);
      port.endPixels();
    }
  }

  void draw(Port& port, uint32_t now, const Frame& frame) {
    if (row_ != kIdle && black_ == frame.screenOn) {  // Turned on or off part-way: drop the copy.
      row_ = kIdle;
      whole_ = true;
    }
    if (row_ == kIdle) {
      if (!frame.screenOn) {
        if (dark_) return;
        begin(0, kHeight, true, now);
      } else if (frame.pixels && (whole_ || dark_ || frame.generation != generation_) &&
                 now - startedMs_ >= kFrameGapMs) {
        const bool all = whole_ || dark_;
        generation_ = frame.generation;
        whole_ = dark_ = false;
        begin(all ? 0 : kPictureTop, all ? kHeight : kPictureTop + kPictureHeight, false, now);
      } else {
        return;
      }
    }
    if (!black_ && !frame.pixels) return;  // The canvas is lent: carry on once it's back.
    const uint16_t rows = static_cast<uint16_t>(end_ - row_ < kRowsPerPass ? end_ - row_ : kRowsPerPass);
    uint8_t line[kWidth * 2];
    int lastSource = -1;
    port.beginPixels(row_, rows);
    for (uint16_t y = row_; y < row_ + rows; ++y) {
      if (black_ || y < kPictureTop || y >= kPictureTop + kPictureHeight) {
        port.blank(kWidth);
        continue;
      }
      const int source = sourceOf(static_cast<uint16_t>(y - kPictureTop));
      if (source != lastSource) scaleRow(frame.pixels + source * kSourceWidth, line);
      lastSource = source;
      port.pixels(line, sizeof(line));
    }
    port.endPixels();
    row_ = static_cast<uint16_t>(row_ + rows);
    if (row_ >= end_) {
      row_ = kIdle;
      if (black_) dark_ = true;
    }
  }

  void begin(uint16_t from, uint16_t to, bool black, uint32_t now) {
    row_ = from;
    end_ = to;
    black_ = black;
    startedMs_ = now;
  }

  Phase phase_ = Phase::Absent;
  Mode mode_ = Mode::Auto;
  bool upsideDown_ = false, away_ = false, looked_ = false, warned_ = false, started_ = false;
  int skip_ = -1;
  uint8_t misses_ = 0, sent_ = 0;  // sent_: the MADCTL the screen was last given.
  uint32_t lookAtMs_ = 0, untilMs_ = 0, checkAtMs_ = 0, startedMs_ = 0;
  uint32_t generation_ = 0;
  bool whole_ = true, dark_ = false, black_ = false;
  uint16_t row_ = kIdle, end_ = 0;
};

}  // namespace ext_display
