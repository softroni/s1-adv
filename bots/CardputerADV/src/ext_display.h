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
// Copying: a new frame goes in one go, but only the rows that changed since the last copy, so a
// clock ticking or a menu moving costs a few rows, not the screen. Pixels go as fast as the wires
// take them: at set-up, rows of noise are written at 40, 27 and then 20 MHz and read back
// (RAMRD), and the fastest that comes back whole is kept. Every check after reads back one row
// that hasn't changed since it went, and a wrong one steps the speed down.
//
// Every pin goes through Port, so the rules run on a computer (scripts/test_ext_display.py);
// part 099 is the real one, on the SD card's SPI object, whose lock keeps the two apart.
#include <initializer_list>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
                  kWrite = 0x2C, kRead = 0x2E, kMadctl = 0x36;
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
constexpr uint16_t rgb565(uint8_t c) {
  const unsigned r3 = c >> 5, g3 = (c >> 2) & 7U, b2 = c & 3U;
  const unsigned r8 = ((r3 * 9U) << 2) + (r3 >> 1), g8 = ((g3 * 9U) << 2) + (g3 >> 1), b8 = b2 * 0x55U;
  return static_cast<uint16_t>(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
}
struct Rgb565Table {
  uint16_t of[256];
  constexpr Rgb565Table() : of() {
    for (unsigned c = 0; c < 256; ++c) of[c] = rgb565(static_cast<uint8_t>(c));
  }
};
constexpr Rgb565Table kRgb565{};

// One canvas row as one screen row, high byte first, the order the screen takes. Every three
// canvas pixels make four: the first twice, as sourceOf says.
inline void scaleRow(const uint8_t* source, uint8_t* out) {
  for (uint16_t s = 0; s < kSourceWidth; s += 3, out += 8) {
    const uint16_t a = kRgb565.of[source[s]], b = kRgb565.of[source[s + 1]], c = kRgb565.of[source[s + 2]];
    out[0] = out[2] = static_cast<uint8_t>(a >> 8);
    out[1] = out[3] = static_cast<uint8_t>(a);
    out[4] = static_cast<uint8_t>(b >> 8);
    out[5] = static_cast<uint8_t>(b);
    out[6] = static_cast<uint8_t>(c >> 8);
    out[7] = static_cast<uint8_t>(c);
  }
}

// A canvas row's fingerprint, to tell which rows changed since the last copy. Each step is a
// bijection, so a row that differs in one word always gets a different one.
inline uint32_t rowHash(const uint8_t* row) {
  uint32_t h = 2166136261u;
  for (uint16_t i = 0; i < kSourceWidth; i += 4) {
    uint32_t word;
    memcpy(&word, row + i, sizeof(word));
    h = (h ^ word) * 16777619u;
  }
  return h;
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

// A row read back with RAMRD: three bytes a pixel (6 bits of each colour, high bits first), after
// a dummy byte, give or take a dummy bit, so pixels may start on any of the first 17 bits.
constexpr int kMaxReadSkip = 16;
constexpr size_t kRowReadBytes = kWidth * 3 + 3;
inline uint8_t byteAt(const uint8_t* in, size_t bit) {
  const size_t i = bit / 8;
  const unsigned shift = bit % 8;
  return shift ? static_cast<uint8_t>(in[i] << shift | in[i + 1] >> (8 - shift)) : in[i];
}
// Whether pixel x of a row read back is c. The order of red and blue is learned once, with skip.
inline uint16_t pixelOf(const uint8_t* line, uint16_t x) { return static_cast<uint16_t>(line[2 * x] << 8 | line[2 * x + 1]); }
inline bool readBackIs(const uint8_t* in, int skip, bool blueFirst, uint16_t x, uint16_t c) {
  const size_t at = static_cast<size_t>(skip) + 24U * x;
  const uint8_t first = byteAt(in, at), green = byteAt(in, at + 8), last = byteAt(in, at + 16);
  const unsigned red = c >> 11, blue = c & 31U;
  return (green >> 2) == ((c >> 5) & 63U) && (first >> 3) == (blueFirst ? blue : red) &&
         (last >> 3) == (blueFirst ? red : blue);
}

// Pixel clocks to try, fastest first. The last is what 0.2.339 ran at, without asking.
constexpr uint32_t kSpeedsHz[] = {40000000, 26666667, 20000000};
constexpr uint8_t kSpeedCount = sizeof(kSpeedsHz) / sizeof(kSpeedsHz[0]);
constexpr uint8_t kSafeSpeed = kSpeedCount - 1;
constexpr uint16_t kTestRows = 8;  // Rows of noise per speed tried: 82,000 bits, in the top band.

constexpr uint32_t kLookMs = 3000;     // How often to look for a screen that isn't there.
constexpr uint32_t kCheckMs = 2000;    // How often to ask a screen that is whether it still is.
constexpr uint32_t kRefreshMs = 5000;  // "on" with nothing to ask: the set-up again, for one plugged in since.
constexpr uint32_t kWakeMs = 120;      // After RESET, and after Sleep Out, before the next step.
constexpr uint32_t kMinGapMs = 30;     // At most about 33 copies a second, and never more than half the time.
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
//   void speed(uint32_t hz); the clock for pixels from now on
//   void command(uint8_t c, const uint8_t* data, uint8_t n);
//   uint32_t read32(uint8_t c);
//   void readRow(uint16_t y, uint8_t* in, size_t n);  RAMRD of row y, n bytes as clocked in
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
        if (mode == Mode::Off) blackAll(port);  // Its backlight stays on, so leave it dark, not frozen.
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
        pickSpeed(port);  // While it's still dark, and before anything is drawn.
        port.command(cmd::kDisplayOn, nullptr, 0);
        phase_ = Phase::Ready;
        whole_ = true;
        dark_ = false;
        checkAtMs_ = now + (canAsk() ? kCheckMs : kRefreshMs);
        if (!started_) {
          char line[96];
          snprintf(line, sizeof(line), "[EXT] showing the Cardputer's screen, pixels at %lu kHz%s",
                   static_cast<unsigned long>(kSpeedsHz[speed_] / 1000), checked() ? ", checked" : ", unchecked");
          port.said(line);
        }
        started_ = true;
        return;
      case Phase::Ready:
        break;
    }
    if (sent_ != madctl(upsideDown_)) {  // Turned round in the setting.
      turn(port);
      whole_ = true;
    }
    if (static_cast<int32_t>(now - checkAtMs_) >= 0 && !check(port, now, frame)) return;
    draw(port, now, frame);
  }

  bool shown() const { return phase_ != Phase::Absent; }
  Phase phase() const { return phase_; }
  int skip() const { return skip_; }
  uint32_t speedHz() const { return kSpeedsHz[speed_]; }
  bool checked() const { return readSkip_ >= 0; }  // Pixels are read back to check them.

 private:
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
  }

  void setUp(Port& port) {
    for (const uint8_t* p = kSetup; *p; p += 2 + p[1]) port.command(p[0], p + 2, p[1]);
    turn(port);
    port.command(cmd::kSleepOut, nullptr, 0);
  }

  // The test rows: noise, the same each time for a seed, so it can be made again to compare.
  static uint16_t noise(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<uint16_t>(state ^ (state >> 16));
  }

  // The fastest pixel clock whose rows read back whole. A screen that can't be read back (no
  // MISO, or no RAMRD) gets the speed the first version used.
  void pickSpeed(Port& port) {
    speed_ = kSafeSpeed;
    if (canAsk()) {
      for (uint8_t i = 0; i < kSpeedCount; ++i) {
        port.speed(kSpeedsHz[i]);
        if (rowsReadBack(port, 0x9E3779B9u * (i + 1U))) {
          speed_ = i;
          break;
        }
      }
    }
    if (!checked()) speed_ = kSafeSpeed;
    port.speed(kSpeedsHz[speed_]);
    blank(port, 0, kTestRows);  // Gone before the screen comes on.
  }

  static void noiseRow(uint32_t& state, uint8_t* line) {
    for (uint16_t x = 0; x < kWidth; ++x) {
      const uint16_t c = noise(state);
      line[2 * x] = static_cast<uint8_t>(c >> 8);
      line[2 * x + 1] = static_cast<uint8_t>(c);
    }
  }

  bool rowsReadBack(Port& port, uint32_t seed) {
    uint8_t line[kWidth * 2], in[kRowReadBytes];
    uint32_t state = seed;
    port.beginPixels(0, kTestRows);
    for (uint16_t y = 0; y < kTestRows; ++y) {
      noiseRow(state, line);
      port.pixels(line, sizeof(line));
    }
    port.endPixels();
    state = seed;
    for (uint16_t y = 0; y < kTestRows; ++y) {
      noiseRow(state, line);
      port.readRow(y, in, sizeof(in));
      if (!checked() && !learnRead(in, line)) return false;
      for (uint16_t x = 0; x < kWidth; ++x)
        if (!readBackIs(in, readSkip_, blueFirst_, x, pixelOf(line, x))) return false;
    }
    return true;
  }

  // Where the pixels start in a read, and red or blue first: where the first sixteen all match.
  bool learnRead(const uint8_t* in, const uint8_t* line) {
    for (int skip = 0; skip <= kMaxReadSkip; ++skip)
      for (bool blueFirst : {false, true}) {
        uint16_t x = 0;
        while (x < 16 && readBackIs(in, skip, blueFirst, x, pixelOf(line, x))) ++x;
        if (x == 16) {
          readSkip_ = skip;
          blueFirst_ = blueFirst;
          return true;
        }
      }
    return false;
  }

  // One picture row that hasn't changed since it went, read back. Wrong, the clock steps down and
  // the whole picture goes again; still wrong at the slowest, pixels stop being checked.
  void checkPixels(Port& port, const Frame& frame) {
    if (!checked() || !frame.pixels || !frame.screenOn || dark_ || whole_) return;
    checkRow_ = static_cast<uint16_t>((checkRow_ + 37) % kPictureHeight);  // Round all of them in time.
    const uint16_t source = sourceOf(checkRow_);
    const uint8_t* row = frame.pixels + source * kSourceWidth;
    if (rowHash(row) != hashes_[source]) return;  // Changed since: the next copy sends it.
    uint8_t in[kRowReadBytes];
    port.readRow(static_cast<uint16_t>(kPictureTop + checkRow_), in, sizeof(in));
    bool same = true;
    for (uint16_t x = 0; x < kWidth && same; ++x) same = readBackIs(in, readSkip_, blueFirst_, x, kRgb565.of[row[sourceOf(x)]]);
    if (same) return;
    char line[96];
    if (speed_ + 1 < kSpeedCount) {
      ++speed_;
      port.speed(kSpeedsHz[speed_]);
      whole_ = true;
      snprintf(line, sizeof(line), "[EXT] a row came back wrong, pixels at %lu kHz now",
               static_cast<unsigned long>(kSpeedsHz[speed_] / 1000));
    } else {
      readSkip_ = -1;
      snprintf(line, sizeof(line), "[EXT] a row came back wrong at the slowest clock, not checking pixels");
    }
    port.said(line);
  }

  // False when the screen was lost or is starting again.
  bool check(Port& port, uint32_t now, const Frame& frame) {
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
      checkPixels(port, frame);
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

  void blank(Port& port, uint16_t y, uint16_t rows) {
    port.beginPixels(y, rows);
    for (uint16_t i = 0; i < rows; ++i) port.blank(kWidth);
    port.endPixels();
  }
  void blackAll(Port& port) { blank(port, 0, kHeight); }

  // A copy, in one go: the rows whose canvas row changed since the last one, or all of them.
  void draw(Port& port, uint32_t now, const Frame& frame) {
    if (!frame.screenOn) {
      if (!dark_) blackAll(port);
      dark_ = whole_ = true;
      return;
    }
    if (!frame.pixels || (!whole_ && frame.generation == generation_)) return;
    if (now - copiedMs_ < gapMs_) return;
    const uint32_t began = port.ms();
    generation_ = frame.generation;
    bool changed[kSourceHeight];
    for (uint16_t s = 0; s < kSourceHeight; ++s) {
      const uint32_t h = rowHash(frame.pixels + s * kSourceWidth);
      changed[s] = whole_ || h != hashes_[s];
      hashes_[s] = h;
    }
    if (whole_) {
      blank(port, 0, kPictureTop);
      blank(port, kPictureTop + kPictureHeight, kHeight - kPictureTop - kPictureHeight);
    }
    uint8_t line[kWidth * 2];
    for (uint16_t y = 0; y < kPictureHeight;) {
      if (!changed[sourceOf(y)]) {
        ++y;
        continue;
      }
      uint16_t end = y;
      while (end < kPictureHeight && changed[sourceOf(end)]) ++end;
      port.beginPixels(static_cast<uint16_t>(kPictureTop + y), static_cast<uint16_t>(end - y));
      int last = -1;
      for (; y < end; ++y) {
        const int source = sourceOf(y);
        if (source != last) scaleRow(frame.pixels + source * kSourceWidth, line);
        last = source;
        port.pixels(line, sizeof(line));
      }
      port.endPixels();
    }
    whole_ = dark_ = false;
    copiedMs_ = now;
    const uint32_t took = port.ms() - began;  // Start to start at least twice this: copying never takes over.
    gapMs_ = 2 * took > kMinGapMs ? 2 * took : kMinGapMs;
  }

  Phase phase_ = Phase::Absent;
  Mode mode_ = Mode::Auto;
  bool upsideDown_ = false, away_ = false, looked_ = false, warned_ = false, started_ = false;
  int skip_ = -1;
  uint8_t misses_ = 0, sent_ = 0;  // sent_: the MADCTL the screen was last given.
  uint32_t lookAtMs_ = 0, untilMs_ = 0, checkAtMs_ = 0;
  // Copying.
  uint32_t generation_ = 0, copiedMs_ = 0, gapMs_ = 0;
  bool whole_ = true, dark_ = false;
  uint32_t hashes_[kSourceHeight] = {};  // Each canvas row as it was last sent.
  // The pixel clock, and how rows read back.
  uint8_t speed_ = kSafeSpeed;
  int readSkip_ = -1;
  bool blueFirst_ = false;
  uint16_t checkRow_ = 0;
};

}  // namespace ext_display
