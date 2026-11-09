// The 2.8" ILI9341 screen on the EXT header (ext_display.h, docs/EXT-SCREEN.md): what the
// Cardputer's own screen shows, a third bigger. It's on the SD card's SPI object, so the bus's own
// lock keeps the two apart, and its pins are the Cap's and the RC522's, so it leaves them alone
// while Mesh, the RFID Lab or Sonar's HC-SR04 has them. Settings > Display > Ext screen.
namespace {
constexpr uint8_t kExtCsPin = 5, kExtResetPin = 3, kExtDcPin = 6;
static_assert(kExtCsPin == kRfidCsPin && kExtResetPin == kRfidResetPin && kExtDcPin == kSonarEchoPin,
              "the EXT screen shares its pins with the Cap, the RC522 and the HC-SR04");
constexpr uint32_t kExtWriteHz = 20000000;  // Pixels. Jumper wires and the SD card share the lines.
constexpr uint32_t kExtReadHz = 4000000;    // Commands and reads; the ILI9341 reads at 6.6 MHz at most.

struct ExtScreenPort {
  uint32_t ms() { return millis(); }

  // A Cap's I/O expander on the G8/G9 bus, as meshEnableCap finds it. Reading changes nothing.
  bool capPresent() {
    m5::I2C_Class* buses[] = {&m5::In_I2C, &m5::Ex_I2C};
    for (auto* bus : buses) {
      if (!bus->isEnabled() || bus->getSDA() != 8 || bus->getSCL() != 9) continue;
      uint8_t bits = 0;
      return bus->readRegister(0x43, 0x03, &bits, 1, 100000);
    }
    return false;
  }

  // An HC-SR04's ECHO and an awake SX1262's BUSY sit low; the screen's DC is an input.
  bool dcHeld() {
    pinMode(kExtDcPin, INPUT_PULLUP);
    delayMicroseconds(50);
    const bool held = digitalRead(kExtDcPin) == LOW;
    pinMode(kExtDcPin, INPUT);
    return held;
  }

  void claim() {
    digitalWrite(kExtCsPin, HIGH);
    pinMode(kExtCsPin, OUTPUT);
    digitalWrite(kExtDcPin, HIGH);
    pinMode(kExtDcPin, OUTPUT);
    digitalWrite(kExtResetPin, HIGH);
    pinMode(kExtResetPin, OUTPUT);
    delay(6);  // Let out of reset, a controller takes 5 ms before it listens.
    if (!gSdSpi.bus()) gSdSpi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  }

  void release() {
    pinMode(kExtDcPin, INPUT);
    pinMode(kExtResetPin, INPUT);
    digitalWrite(kExtCsPin, HIGH);  // Still deselected, as the SD card's set-up leaves it.
  }

  void reset() {
    digitalWrite(kExtResetPin, LOW);
    delayMicroseconds(20);
    digitalWrite(kExtResetPin, HIGH);
  }

  void command(uint8_t c, const uint8_t* data, uint8_t n) {
    begin(kExtReadHz);
    send(c);
    if (n) gSdSpi.writeBytes(data, n);
    end();
  }

  uint32_t read32(uint8_t c) {
    const uint8_t zeros[4] = {};
    uint8_t in[4] = {};
    begin(kExtReadHz);
    send(c);
    gSdSpi.transferBytes(zeros, in, sizeof(in));
    end();
    return uint32_t(in[0]) << 24 | uint32_t(in[1]) << 16 | uint32_t(in[2]) << 8 | in[3];
  }

  void beginPixels(uint16_t y, uint16_t rows) {
    const uint16_t last = y + rows - 1;
    const uint8_t columns[4] = {0, 0, (ext_display::kWidth - 1) >> 8, (ext_display::kWidth - 1) & 0xFF};
    const uint8_t lines[4] = {uint8_t(y >> 8), uint8_t(y), uint8_t(last >> 8), uint8_t(last)};
    begin(kExtWriteHz);
    send(ext_display::cmd::kColumns);
    gSdSpi.writeBytes(columns, sizeof(columns));
    send(ext_display::cmd::kRows);
    gSdSpi.writeBytes(lines, sizeof(lines));
    send(ext_display::cmd::kWrite);
  }
  void pixels(const uint8_t* bytes, size_t n) { gSdSpi.writeBytes(bytes, n); }
  void blank(uint16_t count) {
    static const uint8_t kBlack[64] = {};
    for (size_t left = size_t(count) * 2; left;) {
      const size_t n = left < sizeof(kBlack) ? left : sizeof(kBlack);
      gSdSpi.writeBytes(kBlack, n);
      left -= n;
    }
  }
  void endPixels() { end(); }

  void said(const char* line) { logf("%s", line); }

 private:
  void begin(uint32_t hz) {
    gSdSpi.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
    digitalWrite(kExtCsPin, LOW);
  }
  void end() {
    digitalWrite(kExtCsPin, HIGH);
    gSdSpi.endTransaction();
  }
  void send(uint8_t c) {  // A command byte; what follows is data.
    digitalWrite(kExtDcPin, LOW);
    gSdSpi.transfer(c);
    digitalWrite(kExtDcPin, HIGH);
  }
};

ExtScreenPort gExtScreenPort;
ext_display::Mirror<ExtScreenPort> gExtScreen;

// Mesh has the bus and the Cap's pins, the RFID Lab the RC522's (G5, G3), Sonar the HC-SR04's (G6).
bool extScreenPinsFree() { return !gMeshBusOwned && gUiMode != UiMode::RfidLab && !gSonar.echoOn; }

ext_display::Frame extScreenFrame() {
  ext_display::Frame frame;
  if (gCanvasSpriteActive && !gCanvasLent && gCanvasBuffer.width() == ext_display::kSourceWidth &&
      gCanvasBuffer.height() == ext_display::kSourceHeight && gCanvasBuffer.getColorDepth() == lgfx::rgb332_1Byte &&
      gCanvasBuffer.bufferLength() >= size_t(ext_display::kSourceWidth) * ext_display::kSourceHeight)
    frame.pixels = static_cast<const uint8_t*>(gCanvasBuffer.getBuffer());
  frame.generation = gExtScreenFrames;
  frame.screenOn = !gScreenOffActive && M5Cardputer.Display.getBrightness() > 0;
  return frame;
}

bool extScreenShown() { return gExtScreen.shown(); }

void serviceExtScreen() {
  const bool was = gExtScreen.shown();
  const auto mode = ext_display::modeOrDefault(gDeviceSettings.extScreen);
  gExtScreen.service(gExtScreenPort, mode, gDeviceSettings.extScreenUpsideDown, extScreenPinsFree(), extScreenFrame());
  if (mode == ext_display::Mode::Auto && gExtScreen.shown() != was)
    setStatus(gExtScreen.shown() ? "Ext screen found" : "Ext screen gone", 1500);
}
}  // namespace
