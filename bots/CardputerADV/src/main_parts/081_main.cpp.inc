// The Lab's hardware: finding units on the Grove port, reading them, and the one place outputs
// are written. Test screens and rules both fill in gLabDrive, so the brightness cap and the relay
// guard apply to both. lab.h decides everything else. The port is held through Air Music's sensor
// object, which owns the bus, and only while a Lab screen is open.
namespace {
constexpr uint8_t kLabGpsRxPin = 15, kLabGpsTxPin = 13;  // The Cap's GPS, seen from the Cardputer.
constexpr uint32_t kLabGpsBaud = 115200;
constexpr int kLabGpsBytesPerPass = 256;
constexpr uint8_t kLabToneChannel = 6;
constexpr uint32_t kLabToneSliceMs = 60;
constexpr uint32_t kLabHubReplyUs = 150;   // The hub reads its ADC when it sees the stop.
constexpr uint32_t kLabProbeMs = 150, kLabEnvMs = 1000, kLabEnvWaitMs = 20, kLabStickMs = 50;
constexpr uint32_t kLabKnobMs = 40, kLabLightMs = 200, kLabMotionMs = 100, kLabTiltMs = 100;
constexpr uint32_t kLabServoGapMs = 40, kLabStripGapMs = 50, kLabLedGapMs = 50;
constexpr uint32_t kLabRedrawMs = 100, kLabSampleMs = 250, kLabAwakeMs = 1000;
constexpr uint32_t kLabFarthestMm = 4500;
constexpr uint32_t kLabDLightLuxForFull = 1000;
constexpr uint8_t kLabDLightStart = 0x10;  // Keep measuring at 1 lux.
constexpr uint8_t kLabStickAxes = 0x60, kLabStickButton = 0x20;
constexpr int kLabHistory = 120;           // 30 seconds.
constexpr int kLabRainbowPerPass = 3;
constexpr uint32_t kLabChaseMs = 120;
constexpr int kLabLedDivide = 4;           // The Cardputer's LED is bright at full power.
constexpr const char* kLabDir = "/lab";
constexpr const char* kLabPath = "/lab/rules.dat";
constexpr const char* kLabNewPath = "/lab/rules.new";

void serviceLabTests();
void labEnterView(UiMode mode);

enum class LabStripTest : uint8_t { Solid, Rainbow, Chase, Count };

struct LabBus {
  bool ping(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  }
  bool send(uint8_t address, const uint8_t* bytes, uint8_t length) {
    Wire.beginTransmission(address);
    Wire.write(bytes, length);
    return Wire.endTransmission() == 0;
  }
  // A full stop, because the hub only works out its answer when the request ends.
  bool fetch(uint8_t address, uint8_t reg, uint8_t* out, uint8_t length) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    delayMicroseconds(kLabHubReplyUs);
    return collect(address, out, length);
  }
  // The joystick wants its register held, as Hot Air Balloon reads it.
  bool fetchHeld(uint8_t address, uint8_t reg, uint8_t* out, uint8_t length) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    return collect(address, out, length);
  }
  bool collect(uint8_t address, uint8_t* out, uint8_t length) {
    if (Wire.requestFrom(address, length) != length) return false;
    for (uint8_t i = 0; i < length; ++i) out[i] = static_cast<uint8_t>(Wire.read());
    return true;
  }
};

LabBus gLabBus;
pbhub::Hub<LabBus> gLabHub(gLabBus);
lab::Book gLabBook;
bool gLabBookRead = false, gLabSaved = true;
lab::Inputs gLabInputs;
lab::RuleState gLabStates[lab::kMaxRules];
lab::Drive gLabDrive[lab::kOutCount];
lab_gps::Parser gLabGps;
bool gLabFound[lab::kUnitCount] = {};
uint8_t gLabProbe = 0;
uint8_t gLabHubFirmware = 0;       // 0 until the hub says.
bool gLabHubReady = false;
bool gLabOpen = false, gLabPlaying = false, gLabPaused = false, gLabBehindMenu = false;
int16_t gLabTempTenths = 0;
uint32_t gLabLux = 0;
uint16_t gLabKnobRaw = 0, gLabLightRaw = 0;
lab::AnalogRange gLabKnobRange, gLabLightRange;
uint16_t gLabMotionCount = 0;
bool gLabMotionWas = false;

bool gLabSonicWaiting = false, gLabEnvWaiting = false, gLabDLightOn = false;
uint32_t gLabProbeMs = 0, gLabSonicMs = 0, gLabEnvMs = 0, gLabStickMs = 0, gLabKnobMs = 0, gLabLightMs = 0;
uint32_t gLabMotionMs = 0, gLabTiltMs = 0, gLabRedrawMs = 0, gLabSampleMs = 0, gLabAwakeMs = 0;

// What was last written, so an unchanged output costs nothing on the bus.
int16_t gLabServoSent = -1;
bool gLabServoHeld = false;
uint32_t gLabServoMs = 0, gLabStripMs = 0, gLabLedMs = 0;
lab::StripFrame gLabStripSent;
bool gLabStripKnown = false;
lab::RelayGuard gLabRelays[2];
int gLabLedSent = -1;
bool gLabToneOn = false;
LabStripTest gLabStripTest = LabStripTest::Solid;
int gLabRainbowNext = 0, gLabChaseAt = -1;
uint32_t gLabChaseMs = 0;

// The shown sensor's last 30 seconds.
int16_t gLabHistoryValues[kLabHistory];
bool gLabHistoryLive[kLabHistory];
int gLabHistoryAt = 0;
lab::Sig gLabHistorySig = lab::Sig::Distance;
int16_t gLabLowest = 0, gLabHighest = 0;
bool gLabRangeSeen = false;

void labRedraw() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
bool labFound(lab::Unit unit) { return gLabFound[static_cast<uint8_t>(unit)]; }
void labDriveSet(lab::Out out, int value) {
  lab::Drive& drive = gLabDrive[static_cast<uint8_t>(out)];
  drive.on = true;
  drive.value = static_cast<int16_t>(value);
}
void labDriveOff(lab::Out out) { gLabDrive[static_cast<uint8_t>(out)] = lab::Drive{}; }
void labDriveClear() {
  for (lab::Drive& drive : gLabDrive) drive = lab::Drive{};
}

// Nothing is known about a hub that just came back, so everything is sent again.
void labForgetOutputs() {
  gLabServoSent = -1;
  gLabStripKnown = false;
  gLabRainbowNext = 0;
  gLabChaseAt = -1;
  gLabRelays[0].forget();
  gLabRelays[1].forget();
}
void labUnitLost(lab::Unit unit) {
  using lab::Sig;
  const uint8_t index = static_cast<uint8_t>(unit);
  if (!gLabFound[index]) return;
  gLabFound[index] = false;
  switch (unit) {
    case lab::Unit::Ultrasonic:
      gLabSonicWaiting = false;
      gLabInputs.lose(Sig::Distance);
      break;
    case lab::Unit::Env:
      gLabEnvWaiting = false;
      gLabInputs.lose(Sig::Temperature);
      gLabInputs.lose(Sig::Humidity);
      break;
    case lab::Unit::Joystick:
      gLabInputs.lose(Sig::StickX);
      gLabInputs.lose(Sig::StickY);
      gLabInputs.lose(Sig::StickPress);
      break;
    case lab::Unit::Hub:
      gLabHubReady = false;
      gLabInputs.lose(Sig::Knob);
      gLabInputs.lose(Sig::Motion);
      if (!labFound(lab::Unit::DLight)) gLabInputs.lose(Sig::Light);
      labForgetOutputs();
      break;
    case lab::Unit::DLight:
      gLabDLightOn = false;
      gLabInputs.lose(Sig::Light);
      break;
    default:
      break;
  }
}
void labHubStart() {
  const int8_t strip = gLabBook.wiring.portOf(lab::Part::Strip);
  if (strip >= 0) gLabHub.ledCount(strip, lab::kStripLeds);
  gLabHub.showMode(gLabBook.showMode);
  uint8_t version = 0;
  if (gLabHub.firmware(version)) gLabHubFirmware = version;
  labForgetOutputs();
  gLabHubReady = true;
}
void labProbeNext(uint32_t now) {
  if (now - gLabProbeMs < kLabProbeMs) return;
  gLabProbeMs = now;
  const lab::Unit unit = static_cast<lab::Unit>(gLabProbe);
  const bool here = gLabBus.ping(lab::kUnitAddresses[gLabProbe]);
  if (!here) labUnitLost(unit);
  else gLabFound[gLabProbe] = true;
  gLabProbe = (gLabProbe + 1) % lab::kUnitCount;
}
void labReadSonic(uint32_t now) {
  if (!labFound(lab::Unit::Ultrasonic)) return;
  if (gLabSonicWaiting) {
    if (now - gLabSonicMs < air_music::kMeasureMs) return;
    gLabSonicWaiting = false;
    uint32_t micrometers = 0;
    if (!gAirSensor.read(micrometers)) {
      labUnitLost(lab::Unit::Ultrasonic);
      return;
    }
    const uint32_t mm = micrometers / 1000;
    // Nothing in range reads as zero or as far past the sensor's reach.
    if (mm == 0 || mm > kLabFarthestMm) gLabInputs.lose(lab::Sig::Distance);
    else gLabInputs.set(lab::Sig::Distance, static_cast<int16_t>((mm + 5) / 10), now);
  }
  gLabSonicMs = now;
  if (gAirSensor.trigger()) gLabSonicWaiting = true;
  else labUnitLost(lab::Unit::Ultrasonic);
}
void labReadEnv(uint32_t now) {
  if (!labFound(lab::Unit::Env)) return;
  if (gLabEnvWaiting) {
    if (now - gLabEnvMs < kLabEnvWaitMs) return;
    gLabEnvWaiting = false;
    uint8_t reading[6] = {};
    if (!gBalloonSensors.envRead(reading)) {
      labUnitLost(lab::Unit::Env);
      return;
    }
    if (balloon::crc8(reading, 2) != reading[2] || balloon::crc8(reading + 3, 2) != reading[5]) return;
    const uint32_t rawTemp = (static_cast<uint32_t>(reading[0]) << 8) | reading[1];
    const uint32_t rawHumid = (static_cast<uint32_t>(reading[3]) << 8) | reading[4];
    gLabTempTenths = static_cast<int16_t>(static_cast<int32_t>(rawTemp * 1750U / 65535U) - 450);
    gLabInputs.set(lab::Sig::Temperature, static_cast<int16_t>((gLabTempTenths + (gLabTempTenths >= 0 ? 5 : -5)) / 10), now);
    gLabInputs.set(lab::Sig::Humidity, static_cast<int16_t>(rawHumid * 100U / 65535U), now);
    return;
  }
  if (now - gLabEnvMs < kLabEnvMs) return;
  gLabEnvMs = now;
  if (gBalloonSensors.envTrigger()) gLabEnvWaiting = true;
  else labUnitLost(lab::Unit::Env);
}
void labReadStick(uint32_t now) {
  if (!labFound(lab::Unit::Joystick) || now - gLabStickMs < kLabStickMs) return;
  gLabStickMs = now;
  uint8_t axes[2] = {}, button = 1;
  if (!gLabBus.fetchHeld(lab::kUnitAddresses[static_cast<uint8_t>(lab::Unit::Joystick)], kLabStickAxes, axes, 2) ||
      !gLabBus.fetchHeld(lab::kUnitAddresses[static_cast<uint8_t>(lab::Unit::Joystick)], kLabStickButton, &button, 1)) {
    labUnitLost(lab::Unit::Joystick);
    return;
  }
  gLabInputs.set(lab::Sig::StickX, lab::clampTo<int16_t>(static_cast<int8_t>(axes[0]) * 100 / 127, -100, 100), now);
  gLabInputs.set(lab::Sig::StickY, lab::clampTo<int16_t>(static_cast<int8_t>(axes[1]) * 100 / 127, -100, 100), now);
  gLabInputs.set(lab::Sig::StickPress, button == 0 ? 1 : 0, now);
}
void labReadDLight(uint32_t now) {
  if (!labFound(lab::Unit::DLight) || now - gLabLightMs < kLabLightMs) return;
  gLabLightMs = now;
  const uint8_t address = lab::kUnitAddresses[static_cast<uint8_t>(lab::Unit::DLight)];
  if (!gLabDLightOn) {
    gLabDLightOn = gLabBus.send(address, &kLabDLightStart, 1);
    return;
  }
  uint8_t bytes[2] = {};
  if (!gLabBus.collect(address, bytes, 2)) {
    labUnitLost(lab::Unit::DLight);
    return;
  }
  gLabLux = ((static_cast<uint32_t>(bytes[0]) << 8) | bytes[1]) * 10U / 12U;
  const uint32_t percent = gLabLux * 100U / kLabDLightLuxForFull;
  gLabInputs.set(lab::Sig::Light, static_cast<int16_t>(percent > 100 ? 100 : percent), now);
}
void labReadHubParts(uint32_t now) {
  if (!labFound(lab::Unit::Hub)) return;
  const lab::Wiring& wiring = gLabBook.wiring;
  if (!gLabHubReady) labHubStart();
  const int8_t knob = wiring.portOf(lab::Part::Knob);
  const int8_t light = wiring.portOf(lab::Part::Light);
  const int8_t motion = wiring.portOf(lab::Part::Motion);
  if (knob >= 0 && now - gLabKnobMs >= kLabKnobMs) {
    gLabKnobMs = now;
    if (gLabHub.analogRead(knob, gLabKnobRaw)) gLabInputs.set(lab::Sig::Knob, gLabKnobRange.percent(gLabKnobRaw), now);
    else labUnitLost(lab::Unit::Hub);
  }
  if (light >= 0 && !labFound(lab::Unit::DLight) && now - gLabLightMs >= kLabLightMs) {
    gLabLightMs = now;
    if (gLabHub.analogRead(light, gLabLightRaw)) {
      const int percent = gLabLightRange.percent(gLabLightRaw);
      gLabInputs.set(lab::Sig::Light, static_cast<int16_t>(gLabBook.lightFlipped ? 100 - percent : percent), now);
    } else {
      labUnitLost(lab::Unit::Hub);
    }
  }
  if (motion >= 0 && now - gLabMotionMs >= kLabMotionMs) {
    gLabMotionMs = now;
    bool high = false;
    if (gLabHub.digitalRead(motion, pbhub::kInputPin, high)) {
      if (high && !gLabMotionWas) ++gLabMotionCount;
      gLabMotionWas = high;
      gLabInputs.set(lab::Sig::Motion, high ? 1 : 0, now);
    } else {
      labUnitLost(lab::Unit::Hub);
    }
  }
}
void labReadTilt(uint32_t now) {
  if (!gImuEnabled || now - gLabTiltMs < kLabTiltMs) return;
  gLabTiltMs = now;
  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  gLabInputs.set(lab::Sig::Tilt, static_cast<int16_t>(asinf(clampValue(ay, -1.0f, 1.0f)) * 57.3f), now);
}
void labReadGps(uint32_t now) {
  for (int i = 0; i < kLabGpsBytesPerPass && Serial1.available() > 0; ++i) {
    gLabGps.feed(static_cast<char>(Serial1.read()), now);
  }
  if (gLabGps.hearing(now)) gLabInputs.set(lab::Sig::Satellites, gLabGps.fix.satellites, now);
  else gLabInputs.lose(lab::Sig::Satellites);
}

// --- Outputs ---------------------------------------------------------------------------------
void labWriteServo(uint32_t now) {
  const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(lab::Out::Servo)];
  const int8_t port = gLabBook.wiring.portOf(lab::Part::Servo);
  if (!drive.on || port < 0 || drive.value == gLabServoSent || now - gLabServoMs < kLabServoGapMs) return;
  gLabServoMs = now;
  if (gLabHub.servo(port, pbhub::kOutputPin, static_cast<uint8_t>(lab::clampTo<int>(drive.value, 0, pbhub::kMaxAngle)))) {
    gLabServoSent = drive.value;
    gLabServoHeld = true;
  } else {
    labUnitLost(lab::Unit::Hub);
  }
}
void labWriteStripFrame(uint32_t now, int8_t port) {
  const lab::StripFrame frame = lab::stripFrame(gLabDrive, gLabBook.capPercent);
  if ((gLabStripKnown && frame == gLabStripSent) || now - gLabStripMs < kLabStripGapMs) return;
  gLabStripMs = now;
  bool ok = true;
  if (frame.on && frame.lit < lab::kStripLeds) {
    ok = gLabHub.fillLeds(port, 0, frame.lit, frame.r, frame.g, frame.b) &&
         gLabHub.fillLeds(port, frame.lit, lab::kStripLeds - frame.lit, 0, 0, 0);
  } else {
    ok = gLabHub.fillLeds(port, 0, lab::kStripLeds, frame.r, frame.g, frame.b);
  }
  if (!ok) {
    labUnitLost(lab::Unit::Hub);
    return;
  }
  gLabStripSent = frame;
  gLabStripKnown = true;
}
// The test patterns light LEDs one at a time. The hub redraws the strip for each, so a rainbow is
// sent a few LEDs per pass rather than holding up the loop.
void labWriteStripTest(uint32_t now, int8_t port) {
  uint8_t r = 0, g = 0, b = 0;
  const int percent = gLabDrive[static_cast<uint8_t>(lab::Out::StripBright)].value * gLabBook.capPercent / 100;
  if (gLabStripTest == LabStripTest::Rainbow) {
    for (int i = 0; i < kLabRainbowPerPass && gLabRainbowNext < lab::kStripLeds; ++i, ++gLabRainbowNext) {
      lab::hueToRgb(gLabRainbowNext * 360 / lab::kStripLeds, r, g, b);
      if (!gLabHub.oneLed(port, gLabRainbowNext, r * percent / 100, g * percent / 100, b * percent / 100)) {
        labUnitLost(lab::Unit::Hub);
        return;
      }
    }
    return;
  }
  const lab::Drive& color = gLabDrive[static_cast<uint8_t>(lab::Out::StripColor)];
  int target = gLabDrive[static_cast<uint8_t>(lab::Out::StripLength)].value;
  if (gLabStripTest == LabStripTest::Chase) {
    if (now - gLabChaseMs < kLabChaseMs) return;
    gLabChaseMs = now;
    target = (gLabChaseAt + 1) % lab::kStripLeds;
  }
  if (target == gLabChaseAt) return;
  lab::hueToRgb(color.value, r, g, b);
  const bool ok = gLabChaseAt < 0 ? gLabHub.fillLeds(port, 0, lab::kStripLeds, 0, 0, 0)
                                  : gLabHub.oneLed(port, gLabChaseAt, 0, 0, 0);
  if (!ok || !gLabHub.oneLed(port, target, r * percent / 100, g * percent / 100, b * percent / 100)) {
    labUnitLost(lab::Unit::Hub);
    return;
  }
  gLabChaseAt = target;
}
void labWriteStrip(uint32_t now) {
  const int8_t port = gLabBook.wiring.portOf(lab::Part::Strip);
  if (port < 0) return;
  if (gLabStripTest == LabStripTest::Solid) labWriteStripFrame(now, port);
  else labWriteStripTest(now, port);
}
void labWriteRelays(uint32_t now) {
  const int8_t port = gLabBook.wiring.portOf(lab::Part::Relay);
  if (port < 0) return;
  for (uint8_t i = 0; i < 2; ++i) {
    const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(i == 0 ? lab::Out::Relay1 : lab::Out::Relay2)];
    const bool wanted = drive.on && drive.value > 0;
    if (!gLabRelays[i].allow(wanted, now)) continue;
    // Relay 1 is on the white wire and relay 2 on the yellow, and high closes them.
    if (!gLabHub.digitalWrite(port, i == 0 ? pbhub::kInputPin : pbhub::kOutputPin, wanted)) {
      labUnitLost(lab::Unit::Hub);
      return;
    }
  }
}
void labWriteLed(uint32_t now) {
  if (!M5.Led.isEnabled() || now - gLabLedMs < kLabLedGapMs) return;
  const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(lab::Out::Led)];
  const int wanted = drive.on ? drive.value : -1;
  if (wanted == gLabLedSent) return;
  gLabLedMs = now;
  gLabLedSent = wanted;
  uint8_t r = 0, g = 0, b = 0;
  if (drive.on) lab::hueToRgb(drive.value, r, g, b);
  M5.Led.setAllColor(r / kLabLedDivide, g / kLabLedDivide, b / kLabLedDivide);
  M5.Led.display();
}
void labWriteTone() {
  auto& speaker = M5Cardputer.Speaker;
  const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(lab::Out::Tone)];
  if (!drive.on || !focusToneAllowed()) {
    if (gLabToneOn) speaker.stop(kLabToneChannel);
    gLabToneOn = false;
    return;
  }
  // Short slices, as in Air Music, so a held note never gaps and a new one starts quickly.
  if (speaker.isPlaying(kLabToneChannel) < 2) {
    speaker.tone(static_cast<float>(drive.value), kLabToneSliceMs, kLabToneChannel, false);
  }
  gLabToneOn = true;
}
void labWriteOutputs(uint32_t now) {
  labWriteLed(now);
  labWriteTone();
  if (!labFound(lab::Unit::Hub) || !gLabHubReady) return;
  labWriteServo(now);
  if (labFound(lab::Unit::Hub)) labWriteStrip(now);
  if (labFound(lab::Unit::Hub)) labWriteRelays(now);
}
// Everything off, relays first. The servo is let go too, so it stops holding and drawing power.
void labReleaseOutputs() {
  labDriveClear();
  gLabStripTest = LabStripTest::Solid;
  if (labFound(lab::Unit::Hub) && gLabHubReady) {
    const lab::Wiring& wiring = gLabBook.wiring;
    const int8_t relay = wiring.portOf(lab::Part::Relay);
    if (relay >= 0) {
      gLabHub.digitalWrite(relay, pbhub::kInputPin, false);
      gLabHub.digitalWrite(relay, pbhub::kOutputPin, false);
    }
    const int8_t strip = wiring.portOf(lab::Part::Strip);
    if (strip >= 0) gLabHub.fillLeds(strip, 0, lab::kStripLeds, 0, 0, 0);
    const int8_t servo = wiring.portOf(lab::Part::Servo);
    if (servo >= 0 && gLabServoHeld) gLabHub.digitalWrite(servo, pbhub::kOutputPin, false);
  }
  gLabServoHeld = false;
  labForgetOutputs();
  if (M5.Led.isEnabled() && gLabLedSent >= 0) {
    M5.Led.setAllColor(0, 0, 0);
    M5.Led.display();
  }
  gLabLedSent = -1;
  if (gLabToneOn) M5Cardputer.Speaker.stop(kLabToneChannel);
  gLabToneOn = false;
}

// --- Saving ----------------------------------------------------------------------------------
bool labWriteBook() {
  if (!gSdReady || !ensureDirRecursive(SD, kLabDir)) return false;
  uint8_t bytes[lab::kFileBytes];
  const size_t size = lab::encode(gLabBook, bytes);
  SD.remove(kLabNewPath);
  File file = SD.open(kLabNewPath, FILE_WRITE);
  if (!file) return false;
  const bool wrote = file.write(bytes, size) == size;
  file.close();
  if (!wrote) return false;
  // The old file stays until the new one is whole, so a power cut loses at most this change.
  SD.remove(kLabPath);
  return SD.rename(kLabNewPath, kLabPath);
}
void labSaveBook() {
  gLabSaved = labWriteBook();
  if (!gLabSaved) setStatus(gSdReady ? "Rules not saved" : "No SD card: rules last until restart", 2500);
}
void labReadBook() {
  if (gLabBookRead) return;
  gLabBookRead = true;
  bool loaded = false;
  // A save stopped between its two last steps leaves only the new file.
  for (const char* path : {kLabPath, kLabNewPath}) {
    if (loaded || !gSdReady || !SD.exists(path)) continue;
    File file = SD.open(path, "r");
    if (!file) continue;
    uint8_t bytes[lab::kFileBytes];
    const size_t size = file.size();
    if (size <= sizeof(bytes) && file.read(bytes, size) == size) loaded = lab::decode(bytes, size, gLabBook);
    file.close();
  }
  if (!loaded) {
    gLabBook = lab::Book{};
    gLabBook.rules[0] = lab::starterRule();
    gLabBook.count = 1;
  }
}

// --- The loop --------------------------------------------------------------------------------
void labHistoryReset(lab::Sig sig) {
  gLabHistorySig = sig;
  gLabHistoryAt = 0;
  gLabRangeSeen = false;
  for (int i = 0; i < kLabHistory; ++i) gLabHistoryLive[i] = false;
}
void labHistorySample(uint32_t now) {
  if (now - gLabSampleMs < kLabSampleMs) return;
  gLabSampleMs = now;
  const lab::Reading& reading = gLabInputs.get(gLabHistorySig);
  gLabHistoryValues[gLabHistoryAt] = reading.value;
  gLabHistoryLive[gLabHistoryAt] = reading.live;
  gLabHistoryAt = (gLabHistoryAt + 1) % kLabHistory;
  if (!reading.live) return;
  if (!gLabRangeSeen || reading.value < gLabLowest) gLabLowest = reading.value;
  if (!gLabRangeSeen || reading.value > gLabHighest) gLabHighest = reading.value;
  gLabRangeSeen = true;
}
void labStopPlaying() {
  gLabPlaying = false;
  gLabPaused = false;
  labReleaseOutputs();
}
void labPlay() {
  for (lab::RuleState& state : gLabStates) state = lab::RuleState{};
  labReleaseOutputs();
  gLabPlaying = true;
  gLabPaused = false;
}
void beginLab() {
  labReadBook();
  labDriveClear();
  labForgetOutputs();
  gLabStripTest = LabStripTest::Solid;
  gLabPlaying = false;
  gLabPaused = false;
  gLabHubReady = false;
  gLabServoHeld = false;
  gLabLedSent = -1;
  gLabOpen = true;
  labEnterView(gUiMode);
  gLabGps = lab_gps::Parser{};
  Serial1.setRxBufferSize(512);
  Serial1.begin(kLabGpsBaud, SERIAL_8N1, kLabGpsRxPin, kLabGpsTxPin);
  labRedraw();
}
void stopLab() {
  if (!gLabOpen) return;
  labStopPlaying();
  Serial1.end();
  for (uint8_t i = 0; i < lab::kUnitCount; ++i) labUnitLost(static_cast<lab::Unit>(i));
  gLabInputs = lab::Inputs{};
  gAirSensor.close();
  gLabOpen = false;
}
void labKeepAwake(uint32_t now) {
  if (now - gLabAwakeMs < kLabAwakeMs) return;
  gLabAwakeMs = now;
  handleScreensaverWake();
}
void serviceLab() {
  if (!labMode(gUiMode) || !gLabOpen) return;
  // Nothing should move behind the menu, where it can't be seen or stopped.
  if (gLauncherVisible) {
    if (!gLabBehindMenu) {
      gLabBehindMenu = true;
      if (gLabPlaying) gLabPaused = true;
      labReleaseOutputs();
    }
    return;
  }
  gLabBehindMenu = false;
  // A poll stops the main loop for a moment, and the mic and the speaker run on that loop.
  if (gRecording || gRealtimeActive || gPlaybackActive) return;
  const uint32_t now = millis();
  gAirSensor.open();
  labProbeNext(now);
  labReadSonic(now);
  labReadEnv(now);
  labReadStick(now);
  labReadDLight(now);
  labReadHubParts(now);
  labReadTilt(now);
  labReadGps(now);
  labHistorySample(now);
  serviceLabTests();
  if (gLabPlaying && !gLabPaused) {
    lab::step(gLabBook.rules, gLabBook.count, gLabStates, gLabInputs, now, gLabDrive);
    labKeepAwake(now);
  } else if (gLabPlaying) {
    labDriveClear();
  }
  labWriteOutputs(now);
  if (now - gLabRedrawMs >= kLabRedrawMs) {
    gLabRedrawMs = now;
    labRedraw();
  }
}
}  // namespace
