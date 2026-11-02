// The Lab's hardware: finding units on the Grove port, reading them, and the one place outputs
// are written. Test screens and rules both fill in gLabDrive, so the brightness cap and the relay
// guard apply to both. lab.h decides everything else. The port is held through Air Music's sensor
// object, which owns the bus, and only while a Lab screen is open.
namespace {
constexpr uint8_t kLabGpsRxPin = 15, kLabGpsTxPin = 13;  // The Cap's GPS, seen from the Cardputer.
constexpr uint32_t kLabGpsBaud = 115200;
constexpr int kLabGpsBytesPerPass = 256;
constexpr uint8_t kLabWhitePin = 1, kLabYellowPin = 2;   // The Grove port's two signal wires.
constexpr uint8_t kLabServoChannel = 3;  // IR takes LEDC channel 0 and the backlight takes 7.
constexpr uint8_t kLabLedPin = 21;       // M5Unified leaves M5.Led unset on the Cardputer.
constexpr uint8_t kLabToneChannel = 6;
constexpr uint32_t kLabToneSliceMs = 60;
constexpr uint32_t kLabHubReplyUs = 150;   // The hub reads its ADC when it sees the stop.
constexpr uint32_t kLabProbeMs = 150, kLabEnvMs = 1000, kLabEnvWaitMs = 20, kLabStickMs = 50;
constexpr uint32_t kLabKnobMs = 40, kLabLightMs = 200, kLabMotionMs = 100, kLabTiltMs = 100;
constexpr uint32_t kLabServoGapMs = 40, kLabStripGapMs = 50, kLabLedGapMs = 50;
constexpr uint32_t kLabLampGapMs = 500, kLabAcGapMs = 3000;  // A remote command takes a while to act.
constexpr uint32_t kLabRedrawMs = 100, kLabSampleMs = 250, kLabAwakeMs = 1000;
constexpr uint32_t kLabWireCheckMs = 400, kLabRescanMs = 3000;
constexpr uint16_t kLabBusHighRaw = 3900;  // Above any part's own output: I2C pull-ups hold it here.
constexpr uint8_t kLabSilentChecks = 3;  // In a row, because a knob near the logic threshold can fool one.
constexpr uint8_t kLabSonicFailsToLose = 4;  // Bad transfers in a row. One lost echo isn't an unplugged unit.
constexpr uint32_t kLabSonicRetryMs = 150;
constexpr uint32_t kLabPullSettleUs = 30;
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
constexpr const char* kLabSayDir = "/lab/say";

void serviceLabTests();
void labEnterView(UiMode mode);
bool labRescanAllowed();
bool labBusWanted();
void labSaveBook();

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
lab::Inputs gLabSeen;              // The readings with any practice values on top, for rules and the view.
lab::Practice gLabPractice;
lab::RuleState gLabStates[lab::kMaxRules];
uint16_t gLabSaidFires[lab::kMaxRules] = {};  // How many of each rule's firings have been spoken.
bool gLabFetchingSpeech = false;
lab::Drive gLabDrive[lab::kOutCount];
lab_gps::Parser gLabGps;
bool gLabFound[lab::kUnitCount] = {};
uint8_t gLabProbe = 0;
uint8_t gLabHubFirmware = 0;       // 0 until the hub says.
bool gLabHubReady = false;
// The Grove wires are either the I2C bus or, with nothing on it, belong to parts plugged in directly.
bool gLabOnWires = false;
bool gLabPartUsed[lab::kPartCount] = {};  // Opened since the Lab started, so it's really plugged in.
uint8_t gLabQuietProbes = 0;
uint8_t gLabWhiteSilent = 0;
uint32_t gLabWireCheckMs = 0, gLabRescanMs = 0;
uint8_t gLabSonicFails = 0;
rmt_obj_t* gLabRmt = nullptr;
uint8_t gLabStripRgb[lab::kStripLeds][3];
rmt_data_t gLabStripBits[lab::kStripLeds * lab::kStripBits];
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
lab::RemoteSwitch gLabLamp, gLabAcs[2];
int gLabLampBrightSent = -1;
uint32_t gLabLampBrightMs = 0;
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
bool labAnyUnit() {
  for (bool found : gLabFound) if (found) return true;
  return false;
}
// Whether this part is on the Grove wires right now, rather than on the I/O hub.
bool labOnWires(lab::Part part) { return gLabOnWires && gLabBook.direct.has(part); }
bool labPartReady(lab::Part part) {
  if (gLabOnWires) return gLabBook.direct.has(part);
  return labFound(lab::Unit::Hub) && gLabHubReady && gLabBook.wiring.portOf(part) >= 0;
}
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
  logf("[LAB] lost %s", lab::kUnitNames[index]);
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
  logf("[LAB] I/O hub found");
  const int8_t strip = gLabBook.wiring.portOf(lab::Part::Strip);
  if (strip >= 0) gLabHub.ledCount(strip, lab::kStripLeds);
  gLabHub.showMode(gLabBook.showMode);
  uint8_t version = 0;
  if (gLabHub.firmware(version)) gLabHubFirmware = version;
  labForgetOutputs();
  gLabHubReady = true;
}
// The ultrasonic's small chip only has to acknowledge a real command, so an empty message can
// miss it entirely, and it can't answer anything while it measures. Asking means starting a
// measurement, the same way Air Music does, and once found, its readings are the proof it's there.
bool labAsk(lab::Unit unit) {
  if (unit == lab::Unit::Ultrasonic) return gAirSensor.trigger();
  return gLabBus.ping(lab::kUnitAddresses[static_cast<uint8_t>(unit)]);
}
void labProbeNext(uint32_t now) {
  if (now - gLabProbeMs < kLabProbeMs) return;
  gLabProbeMs = now;
  const uint8_t index = gLabProbe;
  const lab::Unit unit = static_cast<lab::Unit>(index);
  gLabProbe = (gLabProbe + 1) % lab::kUnitCount;
  if (unit == lab::Unit::Ultrasonic && gLabFound[index]) {
    gLabQuietProbes = 0;
    return;
  }
  // Only a whole round of silence counts, or a unit swap could look like an empty port.
  if (labAsk(unit)) {
    if (!gLabFound[index]) logf("[LAB] found %s", lab::kUnitNames[index]);
    gLabFound[index] = true;
    gLabQuietProbes = 0;
    if (unit == lab::Unit::Ultrasonic) {
      gLabSonicWaiting = true;
      gLabSonicMs = now;
      gLabSonicFails = 0;
    }
  } else {
    labUnitLost(unit);
    if (gLabQuietProbes < lab::kUnitCount) ++gLabQuietProbes;
  }
}
void labSonicFailed(uint32_t now) {
  gLabSonicMs = now;
  if (++gLabSonicFails >= kLabSonicFailsToLose) labUnitLost(lab::Unit::Ultrasonic);
}
void labReadSonic(uint32_t now) {
  if (!labFound(lab::Unit::Ultrasonic)) return;
  if (gLabSonicWaiting) {
    if (now - gLabSonicMs < air_music::kMeasureMs) return;
    gLabSonicWaiting = false;
    uint32_t micrometers = 0;
    if (!gAirSensor.read(micrometers)) {
      labSonicFailed(now);
      return;
    }
    gLabSonicFails = 0;
    const uint32_t mm = micrometers / 1000;
    // Nothing in range reads as zero or as far past the sensor's reach.
    if (mm == 0 || mm > kLabFarthestMm) gLabInputs.lose(lab::Sig::Distance);
    else gLabInputs.set(lab::Sig::Distance, static_cast<int16_t>((mm + 5) / 10), now);
  } else if (gLabSonicFails && now - gLabSonicMs < kLabSonicRetryMs) {
    return;
  }
  if (gAirSensor.trigger()) {
    gLabSonicMs = now;
    gLabSonicWaiting = true;
  } else {
    labSonicFailed(now);
  }
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
    if (gLabHub.analogRead(knob, gLabKnobRaw)) gLabInputs.set(lab::Sig::Knob, lab::knobPercent(gLabKnobRange, gLabKnobRaw), now);
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

// --- Parts on the Grove wires -------------------------------------------------------------------
// Nothing here can answer, so the only sign of a sensor is that its wire is held. Left alone, a
// wire follows the chip's weak pulls, and anything really attached overrides them.
bool labWhiteFloats() {
  pinMode(kLabWhitePin, INPUT_PULLUP);
  delayMicroseconds(kLabPullSettleUs);
  const bool followsUp = digitalRead(kLabWhitePin);
  pinMode(kLabWhitePin, INPUT_PULLDOWN);
  delayMicroseconds(kLabPullSettleUs);
  const bool followsDown = !digitalRead(kLabWhitePin);
  pinMode(kLabWhitePin, INPUT);
  return followsUp && followsDown;
}
void labWiresOn() {
  const lab::Direct& direct = gLabBook.direct;
  if (direct.white == lab::Part::Relay) {
    digitalWrite(kLabWhitePin, LOW);
    digitalWrite(kLabYellowPin, LOW);
    pinMode(kLabWhitePin, OUTPUT);
    pinMode(kLabYellowPin, OUTPUT);
  } else if (direct.white != lab::Part::None) {
    pinMode(kLabWhitePin, INPUT);
  }
  if (direct.yellow == lab::Part::Servo) {
    ledcSetup(kLabServoChannel, lab::kServoHz, lab::kServoBits);
    ledcWrite(kLabServoChannel, 0);
    ledcAttachPin(kLabYellowPin, kLabServoChannel);
  } else if (direct.yellow == lab::Part::Strip) {
    gLabRmt = rmtInit(kLabYellowPin, RMT_TX_MODE, RMT_MEM_64);
    if (gLabRmt) rmtSetTick(gLabRmt, 100);
    memset(gLabStripRgb, 0, sizeof(gLabStripRgb));
  }
}
void labWiresOff() {
  const lab::Direct& direct = gLabBook.direct;
  if (direct.yellow == lab::Part::Servo) ledcDetachPin(kLabYellowPin);
  if (gLabRmt) {
    rmtDeinit(gLabRmt);
    gLabRmt = nullptr;
  }
  pinMode(kLabWhitePin, INPUT);
  pinMode(kLabYellowPin, INPUT);
}
void labLoseWireSignals() {
  gLabInputs.lose(lab::Sig::Knob);
  gLabInputs.lose(lab::Sig::Motion);
  gLabInputs.lose(lab::Sig::Light);
}
void labTakeWires() {
  logf("[LAB] on the Grove wires white=%d yellow=%d", static_cast<int>(gLabBook.direct.white), static_cast<int>(gLabBook.direct.yellow));
  gAirSensor.close();
  labForgetOutputs();
  gLabOnWires = true;
  gLabWhiteSilent = 0;
  gLabKnobRange.top = lab::kDirectKnobTop;
  gLabLightRange.top = lab::kDirectLightTop;
  labWiresOn();
}
// Back to the bus, which is how a unit plugged in later gets noticed.
void labGiveUpWires() {
  logf("[LAB] wires back to the I2C bus");
  labWiresOff();
  gLabOnWires = false;
  gLabQuietProbes = 0;
  gLabKnobRange = lab::AnalogRange{};
  gLabLightRange = lab::AnalogRange{};
  labLoseWireSignals();
  labForgetOutputs();
}
// A quick look at the bus at a moment when nothing's being driven. Going back to it for good is
// what lets a unit or the I/O hub plugged in later take over.
void labRescanBus() {
  // The relay unit's inputs would see every bus pulse, so it waits for the Lab to reopen.
  if (!gLabOnWires || gLabBook.direct.white == lab::Part::Relay) return;
  labWiresOff();
  gAirSensor.open();
  bool found = false;
  for (uint8_t i = 0; i < lab::kUnitCount; ++i) found = labAsk(static_cast<lab::Unit>(i)) || found;
  gAirSensor.close();
  labWiresOn();
  if (found) labGiveUpWires();
}
void labReadWires(uint32_t now) {
  const lab::Part white = gLabBook.direct.white;
  const bool sensor = white == lab::Part::Knob || white == lab::Part::Light || white == lab::Part::Motion;
  if (!sensor) return;
  if (now - gLabWireCheckMs >= kLabWireCheckMs) {
    gLabWireCheckMs = now;
    if (!labWhiteFloats()) gLabWhiteSilent = 0;
    else if (gLabWhiteSilent < kLabSilentChecks) ++gLabWhiteSilent;
  }
  if (gLabWhiteSilent >= kLabSilentChecks) {
    labLoseWireSignals();
    return;
  }
  if (white == lab::Part::Knob && now - gLabKnobMs >= kLabKnobMs) {
    gLabKnobMs = now;
    gLabKnobRaw = static_cast<uint16_t>(analogRead(kLabWhitePin));
    gLabInputs.set(lab::Sig::Knob, lab::knobPercent(gLabKnobRange, gLabKnobRaw), now);
  } else if (white == lab::Part::Light && now - gLabLightMs >= kLabLightMs) {
    gLabLightMs = now;
    gLabLightRaw = static_cast<uint16_t>(analogRead(kLabWhitePin));
    const int percent = gLabLightRange.percent(gLabLightRaw);
    gLabInputs.set(lab::Sig::Light, static_cast<int16_t>(gLabBook.lightFlipped ? 100 - percent : percent), now);
  } else if (white == lab::Part::Motion && now - gLabMotionMs >= kLabMotionMs) {
    gLabMotionMs = now;
    const bool high = digitalRead(kLabWhitePin);
    if (high && !gLabMotionWas) ++gLabMotionCount;
    gLabMotionWas = high;
    gLabInputs.set(lab::Sig::Motion, high ? 1 : 0, now);
  }
}
// Whether a look at the bus is worth it. A white wire stuck at the top means I2C pull-ups have
// arrived, so a part has been swapped. Otherwise it's only safe with nothing else to disturb: a
// knob at zero holds the bus clock low and every try would stall, and bus traffic would twitch a
// servo, flash a strip or click a relay.
bool labShouldLookAtBus() {
  const lab::Direct& direct = gLabBook.direct;
  const bool pinned = gLabWhiteSilent >= kLabSilentChecks || (direct.white == lab::Part::Knob && gLabKnobRaw >= kLabBusHighRaw) ||
                      (direct.white == lab::Part::Light && gLabLightRaw >= kLabBusHighRaw);
  const bool nothingToDisturb = direct.yellow == lab::Part::None && (direct.white == lab::Part::None || direct.white == lab::Part::Motion);
  return pinned || nothingToDisturb;
}
// With nothing on the bus, whatever was last used on the wires gets them back.
void serviceLabWires(uint32_t now, bool rescanAllowed, bool busWanted) {
  // On an I2C unit's own screen the bus is the point, whatever was last used on the wires.
  if (busWanted) {
    if (gLabOnWires) labGiveUpWires();
    return;
  }
  if (!gLabOnWires) {
    if (gLabBook.direct.any() && gLabQuietProbes >= lab::kUnitCount && !labAnyUnit()) labTakeWires();
    return;
  }
  if (rescanAllowed && labShouldLookAtBus() && now - gLabRescanMs >= kLabRescanMs) {
    gLabRescanMs = now;
    labRescanBus();
  }
}
// Opening a part with no I/O hub means it's on the Grove port, so that's where it's looked for.
void labUseOnWires(lab::Part part) {
  if (gLabBook.direct.has(part)) return;
  if (gLabOnWires) labWiresOff();
  gLabBook.direct.claim(part);
  labSaveBook();
  labLoseWireSignals();
  labForgetOutputs();
  gLabWhiteSilent = 0;
  if (gLabOnWires) labWiresOn();
  logf("[LAB] using %d on the Grove port", static_cast<int>(part));
}

// --- Outputs ---------------------------------------------------------------------------------
void labWriteServo(uint32_t now) {
  const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(lab::Out::Servo)];
  if (!drive.on || !labPartReady(lab::Part::Servo) || drive.value == gLabServoSent || now - gLabServoMs < kLabServoGapMs) return;
  gLabServoMs = now;
  const int angle = lab::clampTo<int>(drive.value, 0, pbhub::kMaxAngle);
  if (labOnWires(lab::Part::Servo)) {
    ledcWrite(kLabServoChannel, lab::servoDuty(angle));
  } else if (!gLabHub.servo(gLabBook.wiring.portOf(lab::Part::Servo), pbhub::kOutputPin, static_cast<uint8_t>(angle))) {
    labUnitLost(lab::Unit::Hub);
    return;
  }
  gLabServoSent = drive.value;
  gLabServoHeld = true;
}
void labStripShow() {
  if (!gLabRmt) return;
  lab::encodeStrip(gLabStripRgb, lab::kStripLeds, gLabStripBits);
  rmtWriteBlocking(gLabRmt, gLabStripBits, lab::kStripLeds * lab::kStripBits);
}
bool labStripFill(uint16_t start, uint16_t count, uint8_t r, uint8_t g, uint8_t b) {
  if (!labOnWires(lab::Part::Strip)) return gLabHub.fillLeds(gLabBook.wiring.portOf(lab::Part::Strip), start, count, r, g, b);
  for (uint16_t i = start; i < start + count && i < lab::kStripLeds; ++i) {
    gLabStripRgb[i][0] = r;
    gLabStripRgb[i][1] = g;
    gLabStripRgb[i][2] = b;
  }
  labStripShow();
  return true;
}
bool labStripOne(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (!labOnWires(lab::Part::Strip)) return gLabHub.oneLed(gLabBook.wiring.portOf(lab::Part::Strip), index, r, g, b);
  return labStripFill(index, 1, r, g, b);
}
void labWriteStripFrame(uint32_t now) {
  const lab::StripFrame frame = lab::stripFrame(gLabDrive, gLabBook.capPercent);
  if ((gLabStripKnown && frame == gLabStripSent) || now - gLabStripMs < kLabStripGapMs) return;
  gLabStripMs = now;
  bool ok = true;
  if (frame.on && frame.lit < lab::kStripLeds) {
    ok = labStripFill(0, frame.lit, frame.r, frame.g, frame.b) &&
         labStripFill(frame.lit, lab::kStripLeds - frame.lit, 0, 0, 0);
  } else {
    ok = labStripFill(0, lab::kStripLeds, frame.r, frame.g, frame.b);
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
void labWriteStripTest(uint32_t now) {
  uint8_t r = 0, g = 0, b = 0;
  const int percent = gLabDrive[static_cast<uint8_t>(lab::Out::StripBright)].value * gLabBook.capPercent / 100;
  if (gLabStripTest == LabStripTest::Rainbow) {
    for (int i = 0; i < kLabRainbowPerPass && gLabRainbowNext < lab::kStripLeds; ++i, ++gLabRainbowNext) {
      lab::hueToRgb(gLabRainbowNext * 360 / lab::kStripLeds, r, g, b);
      if (!labStripOne(gLabRainbowNext, r * percent / 100, g * percent / 100, b * percent / 100)) {
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
  const bool ok = gLabChaseAt < 0 ? labStripFill(0, lab::kStripLeds, 0, 0, 0) : labStripOne(gLabChaseAt, 0, 0, 0);
  if (!ok || !labStripOne(target, r * percent / 100, g * percent / 100, b * percent / 100)) {
    labUnitLost(lab::Unit::Hub);
    return;
  }
  gLabChaseAt = target;
}
void labWriteStrip(uint32_t now) {
  if (!labPartReady(lab::Part::Strip)) return;
  if (gLabStripTest == LabStripTest::Solid) labWriteStripFrame(now);
  else labWriteStripTest(now);
}
void labWriteRelays(uint32_t now) {
  if (!labPartReady(lab::Part::Relay)) return;
  const int8_t port = gLabBook.wiring.portOf(lab::Part::Relay);
  for (uint8_t i = 0; i < 2; ++i) {
    const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(i == 0 ? lab::Out::Relay1 : lab::Out::Relay2)];
    const bool wanted = drive.on && drive.value > 0;
    if (!gLabRelays[i].allow(wanted, now)) continue;
    // Relay 1 is on the white wire and relay 2 on the yellow, and high closes them.
    if (labOnWires(lab::Part::Relay)) {
      digitalWrite(i == 0 ? kLabWhitePin : kLabYellowPin, wanted ? HIGH : LOW);
    } else if (!gLabHub.digitalWrite(port, i == 0 ? pbhub::kInputPin : pbhub::kOutputPin, wanted)) {
      labUnitLost(lab::Unit::Hub);
      return;
    }
  }
}
void labWriteLed(uint32_t now) {
  if (now - gLabLedMs < kLabLedGapMs) return;
  const lab::Drive& drive = gLabDrive[static_cast<uint8_t>(lab::Out::Led)];
  const int wanted = drive.on ? drive.value : -1;
  if (wanted == gLabLedSent) return;
  gLabLedMs = now;
  gLabLedSent = wanted;
  uint8_t r = 0, g = 0, b = 0;
  if (drive.on) lab::hueToRgb(drive.value, r, g, b);
  neopixelWrite(kLabLedPin, r / kLabLedDivide, g / kLabLedDivide, b / kLabLedDivide);
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
void labSendLamp(bool on) {
  gSmartLamp.power = on;
  smartLampSendControl(smartLampControlPacket(on ? 1 : 0), on ? "Lamp on" : "Lamp off");
}
void labSendAc(int which, bool on) {
  if (which == 0) {
    gTclPower = on;
    sendTclAcState(on ? "Rule on" : "Rule off");
  } else {
    gDaikinPower = on;
    sendDaikinAcState(on ? "Rule on" : "Rule off");
  }
}
void labWriteRemotes(uint32_t now) {
  bool send = false;
  const lab::Drive& power = gLabDrive[static_cast<uint8_t>(lab::Out::LampPower)];
  if (gLabLamp.update(power.on, power.value > 0, now, kLabLampGapMs, send)) labSendLamp(send);
  const lab::Drive& bright = gLabDrive[static_cast<uint8_t>(lab::Out::LampBright)];
  if (bright.on && bright.value != gLabLampBrightSent && now - gLabLampBrightMs >= kLabLampGapMs) {
    gLabLampBrightMs = now;
    gLabLampBrightSent = bright.value;
    const int level = lab::mapRange(bright.value, 0, 100, 0, 255);
    gSmartLamp.brightness = static_cast<uint8_t>(level);
    smartLampSendControl("GL,4,2," + String(level), "Lamp " + String(int(bright.value)) + "%");
  }
  for (int i = 0; i < 2; ++i) {
    const lab::Drive& ac = gLabDrive[static_cast<uint8_t>(i == 0 ? lab::Out::AcTcl : lab::Out::AcDaikin)];
    if (gLabAcs[i].update(ac.on, ac.value > 0, now, kLabAcGapMs, send)) labSendAc(i, send);
  }
}
void labWriteOutputs(uint32_t now) {
  labWriteLed(now);
  labWriteTone();
  labWriteRemotes(now);
  labWriteServo(now);
  labWriteStrip(now);
  labWriteRelays(now);
}
// Everything off, relays first. The servo is let go too, so it stops holding and drawing power.
void labReleaseOutputs() {
  labDriveClear();
  gLabStripTest = LabStripTest::Solid;
  if (gLabOnWires) {
    if (gLabBook.direct.has(lab::Part::Relay)) {
      digitalWrite(kLabWhitePin, LOW);
      digitalWrite(kLabYellowPin, LOW);
    }
    if (gLabBook.direct.has(lab::Part::Strip)) labStripFill(0, lab::kStripLeds, 0, 0, 0);
    if (gLabBook.direct.has(lab::Part::Servo)) ledcWrite(kLabServoChannel, 0);
  } else if (labFound(lab::Unit::Hub) && gLabHubReady) {
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
  if (gLabLedSent >= 0) neopixelWrite(kLabLedPin, 0, 0, 0);
  gLabLedSent = -1;
  if (gLabToneOn) M5Cardputer.Speaker.stop(kLabToneChannel);
  gLabToneOn = false;
  // Only what a rule turned on goes back off. The lamp and the AC aren't only the Lab's.
  if (gLabLamp.release()) labSendLamp(false);
  gLabLampBrightSent = -1;
  for (int i = 0; i < 2; ++i) if (gLabAcs[i].release()) labSendAc(i, false);
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

// --- Rules that talk ---------------------------------------------------------------------------
// Rules run with no network, so a phrase's speech is fetched while there's still time to wait: when
// the rule is saved, and again before playing if that didn't work.
String labSayPath(const char* text) {
  char path[32];
  snprintf(path, sizeof(path), "%s/%08lx.wav", kLabSayDir, static_cast<unsigned long>(lab::sayHash(text)));
  return String(path);
}
void labFetchSpeech(const lab::Rule& rule) {
  if (!lab::hasSay(rule) || !gSdReady || !ensureDirRecursive(SD, kLabSayDir)) return;
  const String path = labSayPath(rule.say);
  if (SD.exists(path.c_str())) return;
  gLabFetchingSpeech = true;
  labRedraw();
  render();
  const bool ok = speechToWavFile(rule.say, path);
  gLabFetchingSpeech = false;
  labRedraw();
  if (!ok) setStatus("No voice yet for this rule", 2000);
}
// One at a time. A second rule's phrase waits, because its firing still counts as not said.
void labSpeakNewFirings() {
  for (uint8_t i = 0; i < gLabBook.count; ++i) {
    if (gLabStates[i].fires == gLabSaidFires[i]) continue;
    gLabSaidFires[i] = gLabStates[i].fires;
    if (!lab::hasSay(gLabBook.rules[i])) continue;
    startPlaybackStreamFromWavFile(labSayPath(gLabBook.rules[i].say), "lab say");
    return;
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
  gLabPractice.clear();
  labReleaseOutputs();
}
void labPlay() {
  for (lab::RuleState& state : gLabStates) state = lab::RuleState{};
  for (uint16_t& said : gLabSaidFires) said = 0;
  gLabPractice.clear();
  labReleaseOutputs();
  gLabPlaying = true;
  gLabPaused = false;
}
void beginLab() {
  labReadBook();
  for (bool& used : gLabPartUsed) used = false;
  labDriveClear();
  labForgetOutputs();
  gLabStripTest = LabStripTest::Solid;
  gLabPlaying = false;
  gLabPaused = false;
  gLabHubReady = false;
  gLabServoHeld = false;
  gLabLedSent = -1;
  gLabOnWires = false;
  gLabQuietProbes = 0;
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
  if (gLabOnWires) labWiresOff();
  gLabOnWires = false;
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
// Rules react to sensors, so nothing else should hold up the loop while they play. Before that
// the Lab has to stay reachable, because a rule asked for in the chat comes back as a reply.
bool labAllowsNetwork() { return !gLabPlaying; }
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
  serviceLabWires(now, labRescanAllowed(), labBusWanted());
  if (gLabOnWires) {
    labReadWires(now);
  } else {
    gAirSensor.open();
    labProbeNext(now);
    labReadSonic(now);
    labReadEnv(now);
    labReadStick(now);
    labReadDLight(now);
    labReadHubParts(now);
  }
  labReadTilt(now);
  labReadGps(now);
  gLabSeen = gLabInputs;
  gLabPractice.overlay(gLabSeen);
  labHistorySample(now);
  serviceLabTests();
  if (gLabPlaying && !gLabPaused) {
    lab::step(gLabBook.rules, gLabBook.count, gLabStates, gLabSeen, now, gLabDrive);
    labSpeakNewFirings();
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
