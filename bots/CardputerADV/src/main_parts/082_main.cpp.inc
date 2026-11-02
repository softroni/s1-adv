// The Lab's screens and keys: Devices lists everything and tests it, and Rules builds, saves and
// plays rules. The canvas is 8-bit, so every color here sits on a 3-3-2 step and survives it.
namespace {
using lab::Dev;
using lab::Out;
using lab::Sig;
using lab::Unit;

constexpr uint16_t kLabLime = rgb565_local(182, 255, 0), kLabCyan = rgb565_local(36, 255, 255);
constexpr uint16_t kLabGreen = rgb565_local(0, 255, 85), kLabAmber = rgb565_local(255, 182, 0);
constexpr uint16_t kLabRed = rgb565_local(255, 73, 85), kLabPanel = rgb565_local(36, 36, 85);
constexpr uint16_t kLabPanel2 = rgb565_local(0, 36, 85), kLabDim = rgb565_local(73, 73, 85);
constexpr uint16_t kLabMuted = rgb565_local(146, 146, 170), kLabSoft = rgb565_local(182, 182, 170);
constexpr uint16_t kLabGraph = rgb565_local(0, 109, 170);
constexpr int kLabListRows = 5, kLabRowH = 21, kLabListY = 20, kLabCardRows = 6, kLabCardRowH = 17;
constexpr int kLabWizardRows = 4;
constexpr int kLabGraphTop = 75, kLabGraphH = 48;
constexpr const char kLabArrowUpDown[] = "\x18\x19", kLabArrowLeftRight[] = "\x1b\x1a";
constexpr const char kLabDeg[] = "\xf8";
constexpr int kLabServoStep = 5, kLabServoFine = 1, kLabHueStep = 15, kLabBrightStep = 5, kLabToneStep = 50;
constexpr uint32_t kLabSweepStepMs = 40;
constexpr uint32_t kLabBeepMs = 400;
constexpr int kLabLastPort = pbhub::kPorts - 1;
constexpr int kLabLedMilliamps = 33;  // M5 rates this strip at 2 A a meter, with 60 LEDs a meter.

enum class LabView : uint8_t { Devices, Device, Wiring, Rules, Rule, Wizard, Play };
enum class LabStep : uint8_t { Sig, Op, Value, Out, How, OutValue, Source };
enum class LabLine : uint8_t { Name, Join, Cond, AddCond, Then, Action, AddAction, Hold, Delete };
struct LabCardRow {
  LabLine line;
  uint8_t index;
};
constexpr int kLabMostCardRows = lab::kMaxConds + lab::kMaxActions + 7;

struct LabUi {
  LabView view = LabView::Devices;
  int devRow = 0, devTop = 0;
  int wiringRow = 0;
  int ruleRow = 0, ruleTop = 0;
  int ruleIndex = 0;            // The rule open in the card, and being edited in the wizard.
  int cardRow = 1, cardTop = 0;
  bool deleteArmed = false;
  int playPage = 0;             // 0 is the board, and after it one page per rule.
  int boardTop = 0;
  // The test screens.
  int servoAngle = 90, stripHue = 200, stripBright = 40, stripLed = 0, ledHue = 120, toneHz = 880;
  bool sweeping = false, sweepUp = true, ledOn = false, relays[2] = {false, false};
  uint32_t sweepMs = 0, beepUntilMs = 0;
} gLabUi;

struct LabWizard {
  lab::Rule rule;
  bool fresh = false;           // A new rule, so it goes on from the condition to the action.
  int8_t condAt = -1, actionAt = -1;  // The line being replaced, or -1 to add one.
  LabStep step = LabStep::Sig;
  int row = 0, top = 0;
  lab::Cond cond;
  lab::Action action;
  bool teaching = true;         // The value follows the live reading until a key nudges it.
} gLabWizard;

// --- Words ------------------------------------------------------------------------------------
String labNumberText(int value, const char* unit, const char* degree) {
  String text = String(value);
  if (strcmp(unit, "deg") == 0) return text + degree;
  if (strcmp(unit, "C") == 0) return text + degree + "C";
  if (strcmp(unit, "%") == 0) return text + "%";
  if (unit[0]) return text + " " + unit;
  return text;
}
// Font 0 has its degree sign at 0xF8. Font 2 and Font 4 print one for a backtick.
String labValueText(Sig sig, int value, const char* degree = "`") {
  const lab::SigInfo& info = lab::info(sig);
  if (info.onOff) return value ? "on" : "off";
  return labNumberText(value, info.unit, degree);
}
String labOutText(Out out, int value, const char* degree = "`") {
  const lab::OutInfo& info = lab::info(out);
  if (info.onOff) return value ? "on" : "off";
  return labNumberText(value, info.unit, degree);
}
String labCondText(const lab::Cond& cond) {
  const lab::SigInfo& info = lab::info(cond.sig);
  String text = info.name;
  if (cond.op == lab::Op::Changes) return text + " changes";
  if (info.onOff) return text + (cond.op == lab::Op::Above ? " on" : " off");
  return text + (cond.op == lab::Op::Below ? " under " : " over ") + labValueText(cond.sig, cond.value);
}
String labActionText(const lab::Action& action) {
  const lab::OutInfo& info = lab::info(action.out);
  String text = info.name;
  if (action.how != lab::How::Set) {
    return text + (action.how == lab::How::Follow ? " follows " : " opposes ") + lab::info(action.source).name;
  }
  if (info.onOff) return text + (action.value ? " on" : " off");
  return text + " to " + labOutText(action.out, action.value);
}
String labHoldText(uint16_t ms) {
  if (!ms) return "Stop right away after";
  const int tenths = ms / 100;
  return String("Keep going ") + String(tenths / 10) + (tenths % 10 ? String(".") + String(tenths % 10) : String("")) + " sec after";
}

// --- Drawing pieces ---------------------------------------------------------------------------
void labChip(const String& text, uint16_t color) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  const int width = d.textWidth(text) + 10;
  const int x = 238 - width;
  d.drawRoundRect(x, 3, width, 13, 5, color);
  d.setTextColor(color);
  d.setCursor(x + 5, 6);
  d.print(text);
}
void labHead(const char* title, const uint8_t* icon, uint16_t accent, const String& chip, uint16_t chipColor) {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(1);
  d.setTextWrap(false);
  d.drawBitmap(3, 1, icon, launcher_icons::kSize, launcher_icons::kSize, accent);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(TFT_WHITE);
  d.setCursor(23, 1);
  d.print(title);
  if (!chip.isEmpty()) labChip(chip, chipColor);
  d.drawFastHLine(0, 18, 240, accent);
}
void labKeys(const String& text) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  d.setTextColor(kLabSoft);
  d.setCursor(3, 127);
  d.print(text);
}
void labTextAt(const lgfx::IFont* font, int x, int y, const String& text, uint16_t color) {
  auto& d = gCanvas;
  d.setFont(font);
  d.setTextColor(color);
  d.setCursor(x, y);
  d.print(text);
}
void labTextRight(const lgfx::IFont* font, int right, int y, const String& text, uint16_t color) {
  gCanvas.setFont(font);
  labTextAt(font, right - gCanvas.textWidth(text), y, text, color);
}
void labTextCenter(const lgfx::IFont* font, int center, int y, const String& text, uint16_t color) {
  gCanvas.setFont(font);
  labTextAt(font, center - gCanvas.textWidth(text) / 2, y, text, color);
}
void labStat(int x, int y, const char* label, const String& value, uint16_t color = TFT_WHITE) {
  labTextAt(&fonts::Font0, x, y, label, kLabMuted);
  labTextAt(&fonts::Font2, x, y + 8, value, color);
}
void labRowBar(int y, int width, uint16_t color) { gCanvas.fillRoundRect(1, y, width, kLabRowH - 1, 6, color); }
void labScrollBar(int top, int visible, int total, int y, int height) {
  if (total <= visible) return;
  auto& d = gCanvas;
  const int thumb = height * visible / total;
  d.fillRect(236, y, 3, height, kLabPanel);
  d.fillRect(236, y + (height - thumb) * top / (total - visible), 3, thumb, kLabLime);
}
void labKeepRowShown(int row, int& top, int visible) {
  if (row < top) top = row;
  if (row >= top + visible) top = row - visible + 1;
}
uint16_t labHueColor(int hue) {
  uint8_t r = 0, g = 0, b = 0;
  lab::hueToRgb(hue, r, g, b);
  return rgb565_local(r, g, b);
}
void labBar(int x, int y, int width, int height, int percent, uint16_t color) {
  auto& d = gCanvas;
  d.fillRoundRect(x, y, width, height, height / 2, kLabPanel);
  const int filled = width * lab::clampTo(percent, 0, 100) / 100;
  if (filled >= height) d.fillRoundRect(x, y, filled, height, height / 2, color);
}

// --- Device list ------------------------------------------------------------------------------
const uint8_t* labDevIcon(Dev dev) {
  using namespace launcher_icons;
  switch (dev) {
    case Dev::Ultrasonic: return kSonar;
    case Dev::Env: return kThermometer;
    case Dev::Joystick: return kJoystick;
    case Dev::Knob: return kKnob;
    case Dev::Light: return kBulb;
    case Dev::Motion: return kEyes;
    case Dev::Relay: return kBolt;
    case Dev::Servo: return kServo;
    case Dev::Strip: return kStrip;
    case Dev::Hub: return kChip;
    case Dev::Nfc: return kNfc;
    case Dev::Gps: return kGlobe;
    case Dev::Tilt: return kPlane;
    case Dev::Led: return kDisplay;
    case Dev::Speaker: return kMusic;
    default: return kChip;
  }
}
bool labUnitOf(Dev dev, Unit& unit) {
  switch (dev) {
    case Dev::Ultrasonic: unit = Unit::Ultrasonic; return true;
    case Dev::Env: unit = Unit::Env; return true;
    case Dev::Joystick: unit = Unit::Joystick; return true;
    case Dev::Hub: unit = Unit::Hub; return true;
    case Dev::Nfc: unit = Unit::Nfc; return true;
    default: return false;
  }
}
bool labLightIsDigital() { return labFound(Unit::DLight); }
int labPortOf(Dev dev) {
  const lab::Part part = lab::partFor(dev);
  return part == lab::Part::None ? -1 : gLabBook.wiring.portOf(part);
}
// Whether this thing can be reached at all right now.
bool labDevHere(Dev dev) {
  Unit unit;
  if (labUnitOf(dev, unit)) return labFound(unit);
  if (dev == Dev::Light && labLightIsDigital()) return true;
  if (lab::partFor(dev) != lab::Part::None) return labFound(Unit::Hub) && labPortOf(dev) >= 0;
  if (dev == Dev::Gps) return gLabGps.hearing(millis());
  if (dev == Dev::Tilt) return gImuEnabled;
  return true;
}
// These take orders and can't answer, so being reachable is all anyone can know.
bool labDevOutputOnly(Dev dev) { return dev == Dev::Relay || dev == Dev::Servo || dev == Dev::Strip; }
String labDevValue(Dev dev) {
  const auto number = [](Sig sig) -> String {
    const lab::Reading& reading = gLabInputs.get(sig);
    return reading.live ? labValueText(sig, reading.value) : String("--");
  };
  switch (dev) {
    case Dev::Ultrasonic: return number(Sig::Distance);
    case Dev::Env: return gLabInputs.get(Sig::Humidity).live ? String(gLabTempTenths / 10) + "` " + number(Sig::Humidity) : String("--");
    case Dev::Joystick:
      return String("x") + String(gLabInputs.get(Sig::StickX).value) + " y" + String(gLabInputs.get(Sig::StickY).value);
    case Dev::Knob: return number(Sig::Knob);
    case Dev::Light: return number(Sig::Light);
    case Dev::Motion: return gLabInputs.get(Sig::Motion).value ? "moving" : "still";
    case Dev::Relay: case Dev::Servo: return String("CH") + String(labPortOf(dev));
    case Dev::Strip: return String(lab::kStripLeds) + " LEDs";
    case Dev::Hub: return gLabHubFirmware ? String("firmware ") + String(gLabHubFirmware) : String("found");
    case Dev::Nfc: return "found";
    case Dev::Gps: return String(gLabGps.fix.satellites) + " sats";
    case Dev::Tilt: return number(Sig::Tilt);
    default: return "built in";
  }
}
int labFoundCount() {
  int count = 0;
  for (uint8_t i = 0; i < lab::kDevCount; ++i) {
    Unit unit;
    if (labUnitOf(static_cast<Dev>(i), unit) && labFound(unit)) ++count;
  }
  return count + (labFound(Unit::DLight) ? 1 : 0);
}
void renderLabDevices() {
  auto& d = gCanvas;
  labHead("Devices", launcher_icons::kLab, kLabLime, String(labFoundCount()) + " ON THE PORT", kLabSoft);
  for (int row = 0; row < min<int>(kLabListRows, lab::kDevCount); ++row) {
    const int i = gLabUi.devTop + row;
    const Dev dev = static_cast<Dev>(i);
    const int y = kLabListY + row * kLabRowH;
    const bool chosen = i == gLabUi.devRow;
    const bool here = labDevHere(dev);
    if (chosen) labRowBar(y, 233, kLabLime);
    d.drawBitmap(5, y + 2, labDevIcon(dev), launcher_icons::kSize, launcher_icons::kSize, chosen ? TFT_BLACK : kLabLime);
    labTextAt(&fonts::FreeSansBold9pt7b, 26, y + 2, lab::kDevNames[i], chosen ? TFT_BLACK : here ? TFT_WHITE : kLabMuted);
    const uint16_t soft = chosen ? TFT_BLACK : here ? kLabSoft : kLabMuted;
    labTextRight(&fonts::Font2, 216, y + 2, here ? labDevValue(dev) : String("not found"), soft);
    const uint16_t dot = chosen ? TFT_BLACK : !here ? kLabRed : labDevOutputOnly(dev) ? kLabCyan : kLabGreen;
    if (here && !labDevOutputOnly(dev)) d.fillCircle(227, y + 10, 3, dot);
    else d.drawCircle(227, y + 10, 3, dot);
  }
  labScrollBar(gLabUi.devTop, kLabListRows, lab::kDevCount, kLabListY, kLabListRows * kLabRowH - 1);
  labKeys(String(kLabArrowUpDown) + " pick  ENTER open  W hub wiring");
}

// --- A sensor's screen -------------------------------------------------------------------------
void labDrawGraph() {
  auto& d = gCanvas;
  const lab::SigInfo& info = lab::info(gLabHistorySig);
  d.fillRect(2, kLabGraphTop, 236, kLabGraphH, kLabPanel2);
  for (int i = 0; i < kLabHistory; ++i) {
    const int at = (gLabHistoryAt + i) % kLabHistory;
    if (!gLabHistoryLive[at]) continue;
    const int height = lab::mapRange(gLabHistoryValues[at], info.lo, info.hi, 1, kLabGraphH - 1);
    const int x = 2 + i * 236 / kLabHistory, y = kLabGraphTop + kLabGraphH - height;
    d.fillRect(x, y, 2, height, kLabGraph);
    d.fillRect(x, y, 2, 2, kLabCyan);
  }
  labTextAt(&fonts::Font0, 5, kLabGraphTop + 2, "last 30 seconds", TFT_WHITE);
}
void labDrawRange() {
  labStat(96, 23, "LOWEST", gLabRangeSeen ? labValueText(gLabHistorySig, gLabLowest) : String("--"));
  labStat(96, 48, "HIGHEST", gLabRangeSeen ? labValueText(gLabHistorySig, gLabHighest) : String("--"));
}
void labDrawHeadFor(Dev dev, uint16_t accent, const String& chip, uint16_t chipColor) {
  labHead(lab::kDevNames[static_cast<uint8_t>(dev)], labDevIcon(dev), accent, chip, chipColor);
}
void labDrawAnswering(Dev dev) { labDrawHeadFor(dev, kLabLime, "ANSWERING", kLabGreen); }
void renderLabSensor(Dev dev, Sig sig) {
  labDrawAnswering(dev);
  const lab::Reading& reading = gLabInputs.get(sig);
  if (reading.live) {
    labTextAt(&fonts::Font6, 6, 22, String(reading.value), TFT_WHITE);
    labTextAt(&fonts::Font2, 8 + gCanvas.textWidth(String(reading.value)), 52, labValueText(sig, 0).substring(1), kLabMuted);
  } else {
    labTextAt(&fonts::Font4, 6, 33, "--", kLabMuted);
    labTextAt(&fonts::Font0, 6, 60, dev == Dev::Ultrasonic ? "no echo" : "waiting", kLabMuted);
  }
  labDrawRange();
}
void renderLabUltrasonic() {
  renderLabSensor(Dev::Ultrasonic, Sig::Distance);
  labStat(170, 23, "READINGS", "8 a sec");
  labStat(170, 48, "GROVE", "0x57");
  labDrawGraph();
  labKeys("R reset  ESC back");
}
void renderLabEnv() {
  labDrawAnswering(Dev::Env);
  if (gLabInputs.get(Sig::Humidity).live) {
    const int tenths = gLabTempTenths < 0 ? -gLabTempTenths : gLabTempTenths;
    labTextAt(&fonts::Font4, 6, 24, String(gLabTempTenths < 0 ? "-" : "") + String(tenths / 10) + "." + String(tenths % 10) + "`C", TFT_WHITE);
    labTextAt(&fonts::Font4, 130, 24, labValueText(Sig::Humidity, gLabInputs.get(Sig::Humidity).value), kLabCyan);
    labTextAt(&fonts::Font0, 6, 54, "TEMPERATURE", kLabMuted);
    labTextAt(&fonts::Font0, 130, 54, "HUMIDITY", kLabMuted);
  } else {
    labTextAt(&fonts::Font4, 6, 33, "--", kLabMuted);
  }
  labDrawGraph();
  labKeys("Breathe on it to see the humidity move");
}
void renderLabLight() {
  renderLabSensor(Dev::Light, Sig::Light);
  if (labLightIsDigital()) {
    labStat(170, 23, "LIGHT", String(gLabLux) + " lux");
    labStat(170, 48, "GROVE", "0x23");
    labDrawGraph();
    labKeys("Cover it to see it work  R reset");
    return;
  }
  labStat(170, 23, "RAW", String(gLabLightRaw));
  labStat(170, 48, "ON", String("CH") + String(labPortOf(Dev::Light)));
  labDrawGraph();
  labKeys("Cover it to test  F flip  R reset");
}
void renderLabTilt() {
  renderLabSensor(Dev::Tilt, Sig::Tilt);
  labStat(170, 23, "ON", "Built in");
  labDrawGraph();
  labKeys("Tip the Cardputer  R reset");
}
void renderLabMotion() {
  labDrawAnswering(Dev::Motion);
  const bool moving = gLabInputs.get(Sig::Motion).value != 0;
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, moving ? "Motion!" : "All still", moving ? kLabGreen : TFT_WHITE);
  labStat(112, 23, "SEEN", String(gLabMotionCount) + " times");
  labStat(176, 23, "ON", String("CH") + String(labPortOf(Dev::Motion)));
  labTextAt(&fonts::Font0, 6, 54, "Stays on about 2 sec after you stop", kLabMuted);
  labDrawGraph();
  labKeys("Wave in front of it  R reset");
}
void renderLabKnob() {
  auto& d = gCanvas;
  labDrawAnswering(Dev::Knob);
  const lab::Reading& reading = gLabInputs.get(Sig::Knob);
  constexpr int cx = 54, cy = 72, kDots = 30, kRange = 300;
  const int value = reading.live ? reading.value : 0;
  const int shown = value * kDots / 100;
  for (int i = 0; i <= kDots; ++i) {
    const float angle = (240.0f - i * static_cast<float>(kRange) / kDots) * DEG_TO_RAD;
    d.fillCircle(cx + static_cast<int>(42.0f * cosf(angle)), cy - static_cast<int>(42.0f * sinf(angle)), 2, i <= shown ? kLabLime : kLabPanel);
  }
  d.fillCircle(cx, cy, 31, kLabPanel);
  d.drawCircle(cx, cy, 31, kLabSoft);
  const float angle = (240.0f - value * static_cast<float>(kRange) / 100.0f) * DEG_TO_RAD;
  d.drawLine(cx + static_cast<int>(12.0f * cosf(angle)), cy - static_cast<int>(12.0f * sinf(angle)),
             cx + static_cast<int>(28.0f * cosf(angle)), cy - static_cast<int>(28.0f * sinf(angle)), TFT_WHITE);
  labTextAt(&fonts::Font4, 112, 23, reading.live ? String(value) + "%" : String("--"), TFT_WHITE);
  const bool turning = reading.live && reading.moved && millis() - reading.changedMs < lab::kChangedMs;
  labStat(112, 54, "TURNING", turning ? "Yes" : "No", turning ? kLabGreen : TFT_WHITE);
  labStat(176, 54, "RAW", String(gLabKnobRaw));
  labStat(112, 84, "ON", String("CH") + String(labPortOf(Dev::Knob)));
  labKeys("Turn it end to end to set its range");
}
void renderLabStick() {
  auto& d = gCanvas;
  labDrawAnswering(Dev::Joystick);
  constexpr int x = 10, y = 23, size = 98, center = size / 2;
  d.fillRoundRect(x, y, size, size, 8, kLabPanel2);
  d.drawFastHLine(x + 4, y + center, size - 8, kLabDim);
  d.drawFastVLine(x + center, y + 4, size - 8, kLabDim);
  d.drawCircle(x + center, y + center, 10, kLabDim);
  const int stickX = gLabInputs.get(Sig::StickX).value, stickY = gLabInputs.get(Sig::StickY).value;
  const int dotX = x + center + stickX * 42 / 100, dotY = y + center - stickY * 42 / 100;
  d.drawLine(x + center, y + center, dotX, dotY, kLabLime);
  d.fillCircle(dotX, dotY, 6, kLabLime);
  const bool pressed = gLabInputs.get(Sig::StickPress).value != 0;
  labStat(124, 25, "LEFT-RIGHT", String(stickX));
  labStat(124, 53, "UP-DOWN", String(stickY));
  labStat(124, 81, "BUTTON", pressed ? "Pressed" : "Up", pressed ? kLabGreen : TFT_WHITE);
  labKeys("Move it and press it  ESC back");
}
void renderLabServo() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Servo, kLabLime, String("CH") + String(labPortOf(Dev::Servo)) + " SIGNAL SENT", kLabAmber);
  constexpr int cx = 120, cy = 110;
  const int angle = gLabUi.servoAngle;
  d.fillArc(cx, cy, 58, 66, 180, 360, kLabPanel);
  if (angle > 0) d.fillArc(cx, cy, 58, 66, 180, 180 + angle, kLabLime);
  const float radians = (180 - angle) * DEG_TO_RAD;
  const int tipX = cx + static_cast<int>(50.0f * cosf(radians)), tipY = cy - static_cast<int>(50.0f * sinf(radians));
  for (int side = -1; side <= 1; ++side) d.drawLine(cx + side, cy, tipX + side, tipY, TFT_WHITE);
  d.fillCircle(cx, cy, 5, TFT_WHITE);
  labTextCenter(&fonts::Font0, 120, 34, "90", kLabSoft);
  labTextAt(&fonts::Font0, 34, 102, "0", kLabSoft);
  labTextAt(&fonts::Font0, 190, 102, "180", kLabSoft);
  labTextAt(&fonts::Font4, 2, 24, String(angle) + "`", TFT_WHITE);
  labStat(198, 26, "STEP", String(kLabServoStep) + "`");
  labTextCenter(&fonts::Font0, 120, 117, gLabUi.sweeping ? "Sweeping" : "A servo cannot reply: watch it move", kLabAmber);
  labKeys(String(kLabArrowLeftRight) + " turn  A D fine  C center  S sweep");
}
void renderLabStrip() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Strip, kLabLime, String(lab::kStripLeds) + " LEDS", kLabSoft);
  const LabStripTest test = gLabStripTest;
  for (int i = 0; i < lab::kStripLeds; ++i) {
    uint16_t color = kLabPanel;
    if (test == LabStripTest::Rainbow) color = labHueColor(i * 360 / lab::kStripLeds);
    else if (test == LabStripTest::Solid) color = labHueColor(gLabUi.stripHue);
    else if (i == gLabChaseAt) color = labHueColor(gLabUi.stripHue);
    d.fillRoundRect(15 + i * 7, 22, 6, 10, 1, color);
  }
  static constexpr const char* kModes[4] = {"Solid", "Rainbow", "Chase", "Count"};
  for (int i = 0; i < 4; ++i) {
    const int x = 3 + i * 59;
    const bool on = static_cast<int>(test) == i;
    d.fillRoundRect(x, 36, 56, 17, 5, on ? kLabLime : kLabPanel);
    labTextCenter(&fonts::Font2, x + 28, 37, kModes[i], on ? TFT_BLACK : TFT_WHITE);
  }
  labTextAt(&fonts::Font0, 4, 62, test == LabStripTest::Count ? "LED" : "COLOR", kLabMuted);
  if (test == LabStripTest::Count) {
    labTextAt(&fonts::Font2, 48, 58, String(gLabUi.stripLed + 1) + " of " + String(lab::kStripLeds), TFT_WHITE);
  } else {
    d.fillRoundRect(48, 60, 150, 11, 4, labHueColor(gLabUi.stripHue));
    labTextRight(&fonts::Font2, 236, 58, String(gLabUi.stripHue), TFT_WHITE);
  }
  labTextAt(&fonts::Font0, 4, 80, "BRIGHT", kLabMuted);
  labBar(48, 78, 150, 11, gLabUi.stripBright, kLabLime);
  labTextRight(&fonts::Font2, 236, 76, String(gLabUi.stripBright * gLabBook.capPercent / 100) + "%", TFT_WHITE);
  labTextAt(&fonts::Font0, 4, 96, String("Limit ") + String(gLabBook.capPercent) + "% of full power. L changes it", kLabMuted);
  const int milliamps = gLabUi.stripBright * gLabBook.capPercent / 100 * lab::kStripLeds * kLabLedMilliamps / 100;
  labTextAt(&fonts::Font0, 4, 107, String("Worst case ") + String(milliamps) + " mA", kLabAmber);
  if (gLabHubFirmware == 1) labTextRight(&fonts::Font0, 236, 107, "Hub firmware 1", kLabAmber);
  labKeys(String(kLabArrowLeftRight) + " " + (test == LabStripTest::Count ? "move" : "color") + "  " + kLabArrowUpDown + " bright  M mode  T timing");
}
void renderLabRelays() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Relay, kLabLime, String("CH") + String(labPortOf(Dev::Relay)) + " SIGNAL SENT", kLabAmber);
  for (int i = 0; i < 2; ++i) {
    const int x = 6 + i * 116;
    const bool on = gLabUi.relays[i];
    d.fillRoundRect(x, 26, 112, 62, 8, on ? kLabLime : kLabPanel);
    labTextCenter(&fonts::Font2, x + 56, 30, String("Relay ") + String(i + 1), on ? TFT_BLACK : TFT_WHITE);
    labTextCenter(&fonts::Font4, x + 56, 50, on ? "ON" : "OFF", on ? TFT_BLACK : TFT_WHITE);
  }
  labTextCenter(&fonts::Font0, 120, 95, "Listen for the click", kLabAmber);
  labTextCenter(&fonts::Font0, 120, 107, "Both turn off when you leave", kLabMuted);
  labKeys("1 and 2 switch them  ESC back");
}
void renderLabHub() {
  auto& d = gCanvas;
  labDrawAnswering(Dev::Hub);
  for (int i = 0; i < pbhub::kPorts; ++i) {
    const int y = 22 + i * 14;
    const lab::Part part = gLabBook.wiring.port[i];
    labTextAt(&fonts::Font0, 6, y + 3, String("CH") + String(i), kLabMuted);
    labTextAt(&fonts::Font2, 34, y - 1, lab::kPartNames[static_cast<uint8_t>(part)], part == lab::Part::None ? kLabMuted : TFT_WHITE);
  }
  labTextRight(&fonts::Font0, 236, 25, "FIRMWARE", kLabMuted);
  labTextRight(&fonts::Font2, 236, 33, gLabHubFirmware ? String(gLabHubFirmware) : String("?"), TFT_WHITE);
  d.drawFastHLine(0, 108, 240, kLabDim);
  labTextAt(&fonts::Font0, 4, 113, "These must match your cables", kLabAmber);
  labKeys("W change what's on each port");
}
void renderLabWiring() {
  auto& d = gCanvas;
  labHead("Hub wiring", launcher_icons::kChip, kLabLime, "SAVED ON ESC", kLabSoft);
  for (int i = 0; i < pbhub::kPorts; ++i) {
    const int y = 20 + i * 17;
    const bool chosen = i == gLabUi.wiringRow;
    const lab::Part part = gLabBook.wiring.port[i];
    if (chosen) d.fillRoundRect(1, y, 237, 16, 5, kLabLime);
    labTextAt(&fonts::Font2, 8, y, String("CH") + String(i), chosen ? TFT_BLACK : kLabMuted);
    labTextAt(&fonts::Font2, 48, y, lab::kPartNames[static_cast<uint8_t>(part)], chosen ? TFT_BLACK : part == lab::Part::None ? kLabMuted : TFT_WHITE);
  }
  labKeys(String(kLabArrowUpDown) + " pick  " + kLabArrowLeftRight + " change  ESC save");
}
void renderLabNfc() {
  labDrawAnswering(Dev::Nfc);
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, "NFC unit found", TFT_WHITE);
  labTextAt(&fonts::Font0, 6, 52, "Grove 0x50", kLabMuted);
  labTextAt(&fonts::Font0, 6, 68, "NFC Notes reads and writes tags, and", kLabSoft);
  labTextAt(&fonts::Font0, 6, 79, "it is the full test for this unit.", kLabSoft);
  labKeys("ENTER open NFC Notes  ESC back");
}
void renderLabGps() {
  const uint32_t now = millis();
  const bool hearing = gLabGps.hearing(now);
  const lab_gps::Fix& fix = gLabGps.fix;
  labDrawHeadFor(Dev::Gps, hearing ? kLabLime : kLabRed, hearing ? (fix.valid ? "FIXED" : "SEARCHING") : "NO DATA", hearing ? kLabGreen : kLabRed);
  if (!hearing) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Nothing from the GPS", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 50, "Check the Cap is on the back and", kLabSoft);
    labTextAt(&fonts::Font0, 6, 61, "its antenna is fitted.", kLabSoft);
    labKeys("M open Mesh  ESC back");
    return;
  }
  labStat(6, 24, "SATELLITES", String(fix.satellites));
  labStat(100, 24, "SPEED", fix.valid ? String(fix.speedKmhE1 / 10) + "." + String(fix.speedKmhE1 % 10) + " km/h" : String("--"));
  const auto coordinate = [](int32_t e5) -> String {
    const int32_t size = e5 < 0 ? -e5 : e5;
    String tail = String(size % 100000);
    while (tail.length() < 5) tail = String("0") + tail;
    return String(e5 < 0 ? "-" : "") + String(size / 100000) + "." + tail;
  };
  labStat(6, 58, "LATITUDE", fix.valid ? coordinate(fix.latE5) : String("--"));
  labStat(120, 58, "LONGITUDE", fix.valid ? coordinate(fix.lonE5) : String("--"));
  if (!fix.valid) labTextAt(&fonts::Font0, 6, 96, "First fix can take minutes outdoors", kLabAmber);
  labKeys("M open Mesh  ESC back");
}
void renderLabLed() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Led, kLabLime, "BUILT IN", kLabSoft);
  d.fillCircle(120, 62, 30, gLabUi.ledOn ? labHueColor(gLabUi.ledHue) : kLabPanel);
  labTextCenter(&fonts::Font2, 120, 100, gLabUi.ledOn ? "On" : "Off", TFT_WHITE);
  labKeys(String(kLabArrowLeftRight) + " color  SPACE on/off  ESC back");
}
void renderLabSpeaker() {
  labDrawHeadFor(Dev::Speaker, kLabLime, "BUILT IN", kLabSoft);
  labTextCenter(&fonts::Font6, 120, 34, String(gLabUi.toneHz), TFT_WHITE);
  labTextCenter(&fonts::Font2, 120, 88, "Hz", kLabMuted);
  labKeys(String(kLabArrowLeftRight) + " pitch  ENTER beep  ESC back");
}
// Says what is wrong as far as the bus can tell: this unit, or the whole port.
void renderLabMissing(Dev dev) {
  auto& d = gCanvas;
  const lab::Part part = lab::partFor(dev);
  const bool viaHub = part != lab::Part::None;
  if (viaHub && labFound(Unit::Hub) && labPortOf(dev) < 0) {
    labDrawHeadFor(dev, kLabAmber, "NOT SET UP", kLabAmber);
    labTextAt(&fonts::FreeSansBold9pt7b, 4, 21, "Not on a hub port", TFT_WHITE);
    labTextAt(&fonts::Font0, 4, 46, "The I/O hub can't tell what's plugged", kLabSoft);
    labTextAt(&fonts::Font0, 4, 57, "in, so it needs telling which port.", kLabSoft);
    labKeys("ESC, then W to set its port");
    return;
  }
  labDrawHeadFor(dev, kLabRed, "NO ANSWER", kLabRed);
  Unit unit = Unit::Hub;
  const bool direct = labUnitOf(dev, unit);
  String title = viaHub ? String("I/O hub not found") : String("Nothing at 0x") + String(lab::kUnitAddresses[static_cast<uint8_t>(unit)], HEX);
  if (dev == Dev::Tilt) title = "Tilt sensor is off";
  labTextAt(&fonts::FreeSansBold9pt7b, 4, 21, title, TFT_WHITE);
  labTextAt(&fonts::Font0, 4, 42, "Still answering on this port:", kLabSoft);
  int y = 53, others = 0;
  for (uint8_t i = 0; i < lab::kUnitCount && others < 3; ++i) {
    if (!gLabFound[i] || (direct && static_cast<Unit>(i) == unit)) continue;
    d.fillCircle(8, y + 3, 2, kLabGreen);
    labTextAt(&fonts::Font0, 15, y, lab::kUnitNames[i], TFT_WHITE);
    y += 11;
    ++others;
  }
  if (!others) labTextAt(&fonts::Font0, 8, y, "Nothing", kLabMuted);
  d.fillRoundRect(3, 93, 234, 29, 5, kLabPanel);
  if (others) {
    labTextAt(&fonts::Font0, 8, 97, "The port is fine, so check this", kLabAmber);
    labTextAt(&fonts::Font0, 8, 108, "unit's cable and its hub socket.", kLabAmber);
  } else {
    labTextAt(&fonts::Font0, 8, 97, "Nothing answers, so check the", kLabAmber);
    labTextAt(&fonts::Font0, 8, 108, "cable into the Grove port first.", kLabAmber);
  }
  labKeys("It reconnects by itself  ESC back");
}
void renderLabDevice() {
  const Dev dev = static_cast<Dev>(gLabUi.devRow);
  if (dev == Dev::Gps) { renderLabGps(); return; }
  if (!labDevHere(dev)) { renderLabMissing(dev); return; }
  switch (dev) {
    case Dev::Ultrasonic: renderLabUltrasonic(); break;
    case Dev::Env: renderLabEnv(); break;
    case Dev::Joystick: renderLabStick(); break;
    case Dev::Knob: renderLabKnob(); break;
    case Dev::Light: renderLabLight(); break;
    case Dev::Motion: renderLabMotion(); break;
    case Dev::Relay: renderLabRelays(); break;
    case Dev::Servo: renderLabServo(); break;
    case Dev::Strip: renderLabStrip(); break;
    case Dev::Hub: renderLabHub(); break;
    case Dev::Nfc: renderLabNfc(); break;
    case Dev::Tilt: renderLabTilt(); break;
    case Dev::Led: renderLabLed(); break;
    default: renderLabSpeaker(); break;
  }
}

// --- Rules list and card -----------------------------------------------------------------------
int labRuleRows() { return gLabBook.count + (gLabBook.count < lab::kMaxRules ? 1 : 0); }
void renderLabRules() {
  auto& d = gCanvas;
  labHead("Rules", launcher_icons::kFlow, kLabLime, String(int(gLabBook.count)) + " SAVED", gLabSaved ? kLabSoft : kLabAmber);
  const int rows = labRuleRows();
  for (int row = 0; row < min(kLabListRows, rows); ++row) {
    const int i = gLabUi.ruleTop + row;
    const int y = kLabListY + row * kLabRowH;
    const bool chosen = i == gLabUi.ruleRow;
    if (chosen) labRowBar(y, 233, kLabLime);
    if (i == gLabBook.count) {
      labTextAt(&fonts::FreeSansBold9pt7b, 21, y + 2, "+ New rule", chosen ? TFT_BLACK : kLabLime);
      continue;
    }
    const lab::Rule& rule = gLabBook.rules[i];
    if (rule.on) d.fillCircle(10, y + 10, 4, chosen ? TFT_BLACK : kLabGreen);
    else d.drawCircle(10, y + 10, 4, chosen ? TFT_BLACK : kLabMuted);
    labTextAt(&fonts::FreeSansBold9pt7b, 21, y + 2, rule.name, chosen ? TFT_BLACK : TFT_WHITE);
    labTextRight(&fonts::Font0, 228, y + 6, String(int(rule.conds)) + " in " + String(int(rule.actions)) + " out", chosen ? TFT_BLACK : kLabMuted);
  }
  labScrollBar(gLabUi.ruleTop, kLabListRows, rows, kLabListY, kLabListRows * kLabRowH - 1);
  labKeys("ENTER edit  SPACE on/off  P play");
}
int labCardList(const lab::Rule& rule, LabCardRow* rows) {
  int count = 0;
  const auto add = [&](LabLine line, uint8_t index) {
    rows[count].line = line;
    rows[count].index = index;
    ++count;
  };
  add(LabLine::Name, 0);
  add(LabLine::Join, 0);
  for (uint8_t i = 0; i < rule.conds; ++i) add(LabLine::Cond, i);
  if (rule.conds < lab::kMaxConds) add(LabLine::AddCond, 0);
  add(LabLine::Then, 0);
  for (uint8_t i = 0; i < rule.actions; ++i) add(LabLine::Action, i);
  if (rule.actions < lab::kMaxActions) add(LabLine::AddAction, 0);
  add(LabLine::Hold, 0);
  add(LabLine::Delete, 0);
  return count;
}
String labCardText(const lab::Rule& rule, const LabCardRow& row) {
  switch (row.line) {
    case LabLine::Name: return String("Name: ") + rule.name;
    case LabLine::Join: return rule.any ? "WHEN ANY OF THESE" : "WHEN ALL OF THESE";
    case LabLine::Cond: return labCondText(rule.cond[row.index]);
    case LabLine::AddCond: return "+ Add a condition";
    case LabLine::Then: return "DO ALL OF THESE";
    case LabLine::Action: return labActionText(rule.action[row.index]);
    case LabLine::AddAction: return "+ Add an action";
    case LabLine::Hold: return labHoldText(rule.holdMs);
    default: return gLabUi.deleteArmed ? "ENTER again to delete" : "Delete this rule";
  }
}
void renderLabRule() {
  auto& d = gCanvas;
  const lab::Rule& rule = gLabBook.rules[gLabUi.ruleIndex];
  labHead(rule.name[0] ? rule.name : "Rule", launcher_icons::kFlow, kLabLime, rule.on ? "ON" : "OFF", rule.on ? kLabGreen : kLabMuted);
  LabCardRow rows[kLabMostCardRows];
  const int count = labCardList(rule, rows);
  for (int row = 0; row < min(kLabCardRows, count); ++row) {
    const int i = gLabUi.cardTop + row;
    const int y = 20 + row * kLabCardRowH;
    const bool chosen = i == gLabUi.cardRow;
    const LabLine line = rows[i].line;
    const bool label = line == LabLine::Join || line == LabLine::Then;
    if (chosen) d.fillRoundRect(2, y, 233, kLabCardRowH - 1, 5, kLabLime);
    else if (line == LabLine::Cond || line == LabLine::Action) d.fillRoundRect(2, y, 233, kLabCardRowH - 1, 5, kLabPanel);
    const uint16_t ink = chosen ? TFT_BLACK : label ? (line == LabLine::Then ? kLabLime : kLabCyan)
                       : line == LabLine::Delete ? kLabRed
                       : line == LabLine::Cond || line == LabLine::Action || line == LabLine::Name ? TFT_WHITE : kLabSoft;
    const lgfx::IFont* font = label ? static_cast<const lgfx::IFont*>(&fonts::Font0) : &fonts::Font2;
    d.setFont(font);
    labTextAt(font, 8, y + (label ? 4 : 0), fitCurrentFontToWidth(labCardText(rule, rows[i]), line == LabLine::Cond ? 170 : 218), ink);
    if (line == LabLine::Cond) {
      const lab::Reading& reading = gLabInputs.get(rule.cond[rows[i].index].sig);
      if (reading.live) labTextRight(&fonts::Font2, 231, y, labValueText(rule.cond[rows[i].index].sig, reading.value), chosen ? TFT_BLACK : kLabGreen);
    }
  }
  labScrollBar(gLabUi.cardTop, kLabCardRows, count, 20, kLabCardRows * kLabCardRowH - 1);
  const LabLine line = rows[gLabUi.cardRow].line;
  if (line == LabLine::Name) labKeys("Type a name  ESC save");
  else if (line == LabLine::Hold) labKeys(String(kLabArrowLeftRight) + " change  ESC save");
  else if (line == LabLine::Cond || line == LabLine::Action) labKeys("ENTER change  X remove  ESC save");
  else labKeys("ENTER choose  P play  ESC save");
}

// --- Rule wizard -------------------------------------------------------------------------------
int labWizardRowCount() {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: case LabStep::Source: return lab::kSigCount;
    case LabStep::Op: return lab::info(w.cond.sig).onOff ? 2 : 3;
    case LabStep::Out: return lab::kOutCount;
    case LabStep::How: return lab::info(w.action.out).onOff ? 2 : 3;
    default: return 0;
  }
}
String labWizardRow(int i) {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: case LabStep::Source: return lab::kSigs[i].name;
    case LabStep::Op: {
      if (lab::info(w.cond.sig).onOff) return i == 0 ? "turns on" : "turns off";
      static constexpr const char* kOps[3] = {"goes under", "goes over", "changes at all"};
      return kOps[i];
    }
    case LabStep::Out: return lab::kOuts[i].name;
    case LabStep::How: {
      if (lab::info(w.action.out).onOff) return i == 0 ? "turn on" : "turn off";
      static constexpr const char* kHows[3] = {"set to a value", "follow a sensor", "do the opposite"};
      return kHows[i];
    }
    default: return "";
  }
}
int labWizardStepNumber() {
  switch (gLabWizard.step) {
    case LabStep::Sig: return 1;
    case LabStep::Op: return 2;
    case LabStep::Value: return 3;
    case LabStep::Out: return 4;
    case LabStep::How: return 5;
    default: return 6;
  }
}
void labTeachValue() {
  LabWizard& w = gLabWizard;
  const lab::Reading& reading = gLabInputs.get(w.cond.sig);
  if (w.teaching && reading.live) w.cond.value = reading.value;
}
void renderLabWizard() {
  auto& d = gCanvas;
  LabWizard& w = gLabWizard;
  const bool asksInput = w.step == LabStep::Sig || w.step == LabStep::Op || w.step == LabStep::Value;
  const String chip = w.fresh ? String("STEP ") + String(labWizardStepNumber()) + " OF 6" : String(asksInput ? "WHEN" : "DO");
  labHead(w.fresh ? "New rule" : "Change", launcher_icons::kFlow, kLabLime, chip, kLabSoft);
  if (w.step == LabStep::Value) {
    labTeachValue();
    const lab::SigInfo& info = lab::info(w.cond.sig);
    labTextAt(&fonts::Font2, 4, 20, String("Show me: ") + info.name, kLabCyan);
    labTextCenter(&fonts::Font6, 120, 36, String(w.cond.value), TFT_WHITE);
    const int position = lab::mapRange(w.cond.value, info.lo, info.hi, 0, 100);
    labBar(8, 87, 224, 9, position, kLabCyan);
    d.fillRect(8 + 224 * position / 100 - 1, 84, 3, 15, TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 104, labValueText(w.cond.sig, w.cond.value, kLabDeg), TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 114, w.teaching ? "Set it up, then press ENTER" : "Press ENTER to use this", kLabAmber);
    labKeys(String(kLabArrowLeftRight) + " nudge  ENTER use it  ESC back");
    return;
  }
  if (w.step == LabStep::OutValue) {
    const lab::OutInfo& info = lab::info(w.action.out);
    labTextAt(&fonts::Font2, 4, 20, String("Set ") + info.name + " to", kLabCyan);
    labTextCenter(&fonts::Font6, 120, 36, String(w.action.value), TFT_WHITE);
    if (w.action.out == Out::StripColor || w.action.out == Out::Led) d.fillRoundRect(8, 90, 224, 16, 5, labHueColor(w.action.value));
    else labBar(8, 90, 224, 9, lab::mapRange(w.action.value, info.lo, info.hi, 0, 100), kLabLime);
    labKeys(String(kLabArrowLeftRight) + " change  ENTER use it  ESC back");
    return;
  }
  const char* question = "";
  switch (w.step) {
    case LabStep::Sig: question = "What should it watch?"; break;
    case LabStep::Op: question = lab::info(w.cond.sig).name; break;
    case LabStep::Out: question = "What should it do?"; break;
    case LabStep::How: question = lab::info(w.action.out).name; break;
    default: question = "Follow which sensor?"; break;
  }
  labTextAt(&fonts::Font2, 4, 20, question, kLabCyan);
  if (w.step == LabStep::Op) {
    const lab::Reading& reading = gLabInputs.get(w.cond.sig);
    labTextRight(&fonts::Font2, 236, 20, reading.live ? String("now ") + labValueText(w.cond.sig, reading.value) : String("no reading"), reading.live ? kLabGreen : kLabMuted);
  }
  const int rows = labWizardRowCount();
  for (int row = 0; row < min(kLabWizardRows, rows); ++row) {
    const int i = w.top + row;
    const int y = 37 + row * 22;
    const bool chosen = i == w.row;
    if (chosen) d.fillRoundRect(1, y, 233, 21, 6, kLabLime);
    labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 2, fitCurrentFontToWidth(labWizardRow(i), 150), chosen ? TFT_BLACK : TFT_WHITE);
    if (w.step == LabStep::Sig || w.step == LabStep::Source) {
      const Sig sig = static_cast<Sig>(i);
      const lab::Reading& reading = gLabInputs.get(sig);
      labTextRight(&fonts::Font2, 228, y + 2, reading.live ? labValueText(sig, reading.value) : String("--"), chosen ? TFT_BLACK : reading.live ? kLabGreen : kLabMuted);
    }
  }
  labScrollBar(w.top, kLabWizardRows, rows, 37, kLabWizardRows * 22 - 1);
  labKeys(String(kLabArrowUpDown) + " pick  ENTER next  ESC back");
}

// --- Playing -----------------------------------------------------------------------------------
int labPlayPages() { return 1 + gLabBook.count; }
void labBoardRow(int y, int i) {
  auto& d = gCanvas;
  const lab::Rule& rule = gLabBook.rules[i];
  const lab::RuleState& state = gLabStates[i];
  const bool follows = rule.actions && rule.action[0].how != lab::How::Set;
  const uint16_t back = !rule.on ? kLabPanel2 : state.active ? (follows ? kLabPanel : kLabLime) : kLabPanel2;
  const bool bright = back == kLabLime;
  d.fillRoundRect(2, y, 233, 34, 6, back);
  labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 1, fitCurrentFontToWidth(rule.name, 150), bright ? TFT_BLACK : rule.on ? TFT_WHITE : kLabMuted);
  const char* word = !rule.on ? "OFF" : state.active ? (follows ? "FOLLOWING" : "FIRING") : "WAITING";
  const uint16_t wordColor = bright ? TFT_BLACK : !rule.on ? kLabMuted : state.active ? kLabCyan : kLabAmber;
  labTextRight(&fonts::Font0, 230, y + 5, word, wordColor);
  if (state.active && rule.actions) {
    String line;
    for (uint8_t a = 0; a < rule.actions; ++a) {
      int16_t value = 0;
      const bool known = lab::actionValue(rule.action[a], gLabInputs, value);
      if (a) line = line + ", ";
      line = line + lab::info(rule.action[a].out).name + " " + (known ? labOutText(rule.action[a].out, value, kLabDeg) : String("--"));
    }
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, 8, y + 22, fitCurrentFontToWidth(line, 218), bright ? TFT_BLACK : kLabSoft);
    return;
  }
  if (!rule.conds) return;
  const lab::Cond& cond = rule.cond[0];
  const lab::Reading& reading = gLabInputs.get(cond.sig);
  const lab::SigInfo& info = lab::info(cond.sig);
  String line = info.name;
  if (reading.live) line = line + " " + labValueText(cond.sig, reading.value, kLabDeg);
  else line = line + " no reading";
  gCanvas.setFont(&fonts::Font0);
  labTextAt(&fonts::Font0, 8, y + 22, fitCurrentFontToWidth(line, 124), kLabSoft);
  if (reading.live && cond.op != lab::Op::Changes && !info.onOff) labBar(136, y + 22, 92, 7, lab::closeness(cond, reading.value), kLabAmber);
}
void renderLabBoard() {
  labHead("Playing", launcher_icons::kFlow, kLabLime, gLabPaused ? "PAUSED" : "RUNNING", gLabPaused ? kLabAmber : kLabGreen);
  for (int row = 0; row < min<int>(3, gLabBook.count); ++row) labBoardRow(20 + row * 35, gLabUi.boardTop + row);
  labScrollBar(gLabUi.boardTop, 3, gLabBook.count, 20, 104);
  labKeys(String(kLabArrowLeftRight) + " view  SPACE pause  ESC stop");
}
void labCurve(int x0, int y0, int x1, int y1, uint16_t color, int dot) {
  auto& d = gCanvas;
  constexpr int kSegments = 12;
  const auto pointX = [&](int i) { return x0 + (x1 - x0) * i / kSegments; };
  const auto pointY = [&](int i) {
    const int t = i * 256 / kSegments;
    const int smooth = t * t * (3 * 256 - 2 * t) / (256 * 256);  // Eases in and out.
    return y0 + (y1 - y0) * smooth / 256;
  };
  for (int i = 0; i < kSegments; ++i) d.drawLine(pointX(i), pointY(i), pointX(i + 1), pointY(i + 1), color);
  if (dot >= 0) d.fillCircle(pointX(dot % kSegments), pointY(dot % kSegments), 2, TFT_WHITE);
}
void renderLabFlow(int index) {
  auto& d = gCanvas;
  const lab::Rule& rule = gLabBook.rules[index];
  const lab::RuleState& state = gLabStates[index];
  const uint16_t live = state.active ? kLabGreen : kLabDim;
  labHead(rule.name, launcher_icons::kFlow, kLabLime, String(state.active ? "FIRING " : "WAITING ") + String(index + 1) + "/" + String(int(gLabBook.count)), state.active ? kLabGreen : kLabAmber);
  constexpr int kLeft = 2, kRight = 160, kBoxW = 78, kBoxH = 40, cx = 120, cy = 67, kCircle = 17;
  const int conds = min<int>(2, rule.conds), actions = min<int>(2, rule.actions);
  const int dot = state.active ? static_cast<int>(millis() / 80) : -1;
  for (int i = 0; i < conds; ++i) {
    const lab::Cond& cond = rule.cond[i];
    const int y = conds == 1 ? 47 : 23 + i * 47;
    const bool holds = lab::condHolds(cond, gLabInputs, millis());
    labCurve(kLeft + kBoxW, y + kBoxH / 2, cx - kCircle, cy, holds ? kLabGreen : kLabDim, holds ? dot : -1);
    d.fillRoundRect(kLeft, y, kBoxW, kBoxH, 5, kLabPanel);
    if (holds) d.drawRoundRect(kLeft, y, kBoxW, kBoxH, 5, kLabGreen);
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, kLeft + 5, y + 3, fitCurrentFontToWidth(lab::info(cond.sig).name, kBoxW - 6), holds ? kLabGreen : kLabMuted);
    const lab::Reading& reading = gLabInputs.get(cond.sig);
    labTextAt(&fonts::Font2, kLeft + 5, y + 17, reading.live ? labValueText(cond.sig, reading.value) : String("--"), TFT_WHITE);
  }
  for (int i = 0; i < actions; ++i) {
    const lab::Action& action = rule.action[i];
    const int y = actions == 1 ? 47 : 23 + i * 47;
    labCurve(cx + kCircle, cy, kRight, y + kBoxH / 2, live, dot);
    d.fillRoundRect(kRight, y, kBoxW, kBoxH, 5, kLabPanel);
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, kRight + 5, y + 3, fitCurrentFontToWidth(lab::info(action.out).name, kBoxW - 6), kLabMuted);
    int16_t value = 0;
    const bool known = state.active && lab::actionValue(action, gLabInputs, value);
    if (known && (action.out == Out::StripColor || action.out == Out::Led)) d.fillRoundRect(kRight + 5, y + 16, kBoxW - 10, 16, 3, labHueColor(value));
    else labTextAt(&fonts::Font2, kRight + 5, y + 17, known ? labOutText(action.out, value) : String("--"), TFT_WHITE);
  }
  d.fillCircle(cx, cy, kCircle, live);
  labTextCenter(&fonts::Font2, cx, cy - 7, rule.any ? "ANY" : "ALL", state.active ? TFT_BLACK : kLabSoft);
  labTextCenter(&fonts::Font0, 120, 115, state.fires == 1 ? String("Fired once") : String("Fired ") + String(int(state.fires)) + " times", kLabSoft);
  labKeys(String(kLabArrowLeftRight) + " view  SPACE pause  ESC stop");
}
void renderLabUi() {
  switch (gLabUi.view) {
    case LabView::Devices: renderLabDevices(); break;
    case LabView::Device: renderLabDevice(); break;
    case LabView::Wiring: renderLabWiring(); break;
    case LabView::Rules: renderLabRules(); break;
    case LabView::Rule: renderLabRule(); break;
    case LabView::Wizard: renderLabWizard(); break;
    default:
      if (gLabUi.playPage == 0) renderLabBoard();
      else renderLabFlow(gLabUi.playPage - 1);
      break;
  }
  renderLauncherOverlay();
  renderDebugOverlay();
}

// --- Keys --------------------------------------------------------------------------------------
void labOpenDevice(Dev dev) {
  gLabUi.view = LabView::Device;
  gLabUi.sweeping = false;
  gLabUi.ledOn = false;
  gLabUi.relays[0] = gLabUi.relays[1] = false;
  Sig sig = Sig::Distance;
  if (dev == Dev::Env) sig = Sig::Humidity;
  else if (dev == Dev::Light) sig = Sig::Light;
  else if (dev == Dev::Motion) sig = Sig::Motion;
  else if (dev == Dev::Tilt) sig = Sig::Tilt;
  labHistoryReset(sig);
  if (dev == Dev::Strip) {
    gLabStripTest = LabStripTest::Solid;
    labDriveSet(Out::StripColor, gLabUi.stripHue);
    labDriveSet(Out::StripBright, gLabUi.stripBright);
  }
}
void labCloseDevice() {
  labReleaseOutputs();
  gLabUi.sweeping = false;
  gLabUi.view = LabView::Devices;
}
void labStripPick(LabStripTest test) {
  gLabStripTest = test;
  gLabRainbowNext = 0;
  gLabChaseAt = -1;
  gLabStripKnown = false;
  labDriveSet(Out::StripColor, gLabUi.stripHue);
  labDriveSet(Out::StripBright, gLabUi.stripBright);
  if (test == LabStripTest::Count) labDriveSet(Out::StripLength, gLabUi.stripLed);
  else labDriveOff(Out::StripLength);
}
void labServoGoTo(int angle) {
  gLabUi.servoAngle = lab::clampTo(angle, 0, static_cast<int>(pbhub::kMaxAngle));
  labDriveSet(Out::Servo, gLabUi.servoAngle);
}
void labDeviceKey(char key, char lower, bool left, bool right, bool up, bool down, bool enter) {
  const Dev dev = static_cast<Dev>(gLabUi.devRow);
  if (lower == 'r') {
    if (dev == Dev::Knob) gLabKnobRange = lab::AnalogRange{};
    else if (dev == Dev::Light) gLabLightRange = lab::AnalogRange{};
    else if (dev == Dev::Motion) gLabMotionCount = 0;
    labHistoryReset(gLabHistorySig);
    return;
  }
  switch (dev) {
    case Dev::Servo:
      gLabUi.sweeping = lower == 's' ? !gLabUi.sweeping : false;
      if (left) labServoGoTo(gLabUi.servoAngle - kLabServoStep);
      else if (right) labServoGoTo(gLabUi.servoAngle + kLabServoStep);
      else if (lower == 'a') labServoGoTo(gLabUi.servoAngle - kLabServoFine);
      else if (lower == 'd') labServoGoTo(gLabUi.servoAngle + kLabServoFine);
      else if (lower == 'c') labServoGoTo(90);
      return;
    case Dev::Strip:
      if (lower == 'm') labStripPick(static_cast<LabStripTest>((static_cast<int>(gLabStripTest) + 1) % 4));
      else if (lower == 'l') {
        const uint8_t cap = gLabBook.capPercent;
        gLabBook.capPercent = cap == lab::kCaps[0] ? lab::kCaps[1] : cap == lab::kCaps[1] ? lab::kCaps[2] : lab::kCaps[0];
        labSaveBook();
        labStripPick(gLabStripTest);
      } else if (lower == 't') {
        gLabBook.showMode = gLabBook.showMode ? 0 : 1;
        gLabHub.showMode(gLabBook.showMode);
        labSaveBook();
        labStripPick(gLabStripTest);
      } else if (up || down) {
        gLabUi.stripBright = lab::clampTo(gLabUi.stripBright + (up ? kLabBrightStep : -kLabBrightStep), 0, 100);
        labStripPick(gLabStripTest);
      } else if ((left || right) && gLabStripTest == LabStripTest::Count) {
        gLabUi.stripLed = (gLabUi.stripLed + (right ? 1 : lab::kStripLeds - 1)) % lab::kStripLeds;
        labDriveSet(Out::StripLength, gLabUi.stripLed);
      } else if (left || right) {
        gLabUi.stripHue = (gLabUi.stripHue + (right ? kLabHueStep : 360 - kLabHueStep)) % 360;
        labStripPick(gLabStripTest);
      }
      return;
    case Dev::Relay:
      if (key == '1' || key == '2') {
        const int i = key - '1';
        gLabUi.relays[i] = !gLabUi.relays[i];
        labDriveSet(i == 0 ? Out::Relay1 : Out::Relay2, gLabUi.relays[i] ? 1 : 0);
      }
      return;
    case Dev::Light:
      if (lower == 'f' && !labLightIsDigital()) {
        gLabBook.lightFlipped = !gLabBook.lightFlipped;
        labSaveBook();
      }
      return;
    case Dev::Nfc:
      if (enter) setUiMode(UiMode::NfcNotes);
      return;
    case Dev::Gps:
      if (lower == 'm') setUiMode(UiMode::Mesh);
      return;
    case Dev::Led:
      if (key == ' ') gLabUi.ledOn = !gLabUi.ledOn;
      else if (left || right) {
        gLabUi.ledHue = (gLabUi.ledHue + (right ? kLabHueStep : 360 - kLabHueStep)) % 360;
        gLabUi.ledOn = true;
      }
      if (gLabUi.ledOn) labDriveSet(Out::Led, gLabUi.ledHue);
      else labDriveOff(Out::Led);
      return;
    case Dev::Speaker:
      if (left || right) gLabUi.toneHz = lab::clampTo(gLabUi.toneHz + (right ? kLabToneStep : -kLabToneStep), 200, 2000);
      if (enter || left || right) {
        gLabUi.beepUntilMs = millis() + kLabBeepMs;
        labDriveSet(Out::Tone, gLabUi.toneHz);
      }
      return;
    default:
      return;
  }
}
// The servo's sweep and the speaker's beep run while the screen is up, with no key held.
void serviceLabTests() {
  if (gLabUi.view != LabView::Device) return;
  const uint32_t now = millis();
  if (gLabUi.beepUntilMs && now >= gLabUi.beepUntilMs) {
    gLabUi.beepUntilMs = 0;
    labDriveOff(Out::Tone);
  }
  if (!gLabUi.sweeping || now - gLabUi.sweepMs < kLabSweepStepMs) return;
  gLabUi.sweepMs = now;
  if (gLabUi.servoAngle >= pbhub::kMaxAngle) gLabUi.sweepUp = false;
  if (gLabUi.servoAngle <= 0) gLabUi.sweepUp = true;
  labServoGoTo(gLabUi.servoAngle + (gLabUi.sweepUp ? 2 : -2));
}
void labWizardGo(LabStep step) {
  LabWizard& w = gLabWizard;
  w.step = step;
  w.row = 0;
  w.top = 0;
  w.teaching = true;
}
void labWizardStart(bool fresh, int condAt, int actionAt, bool forAction) {
  LabWizard& w = gLabWizard;
  w.rule = fresh ? lab::Rule{} : gLabBook.rules[gLabUi.ruleIndex];
  w.fresh = fresh;
  w.condAt = static_cast<int8_t>(condAt);
  w.actionAt = static_cast<int8_t>(actionAt);
  w.cond = condAt >= 0 ? w.rule.cond[condAt] : lab::Cond{};
  w.action = actionAt >= 0 ? w.rule.action[actionAt] : lab::Action{};
  gLabUi.view = LabView::Wizard;
  labWizardGo(forAction ? LabStep::Out : LabStep::Sig);
}
void labOpenRule(int index) {
  gLabUi.ruleIndex = index;
  gLabUi.cardRow = 1;
  gLabUi.cardTop = 0;
  gLabUi.deleteArmed = false;
  gLabUi.view = LabView::Rule;
}
void labWizardFinish() {
  LabWizard& w = gLabWizard;
  if (w.fresh) {
    int number = gLabBook.count + 1;
    snprintf(w.rule.name, sizeof(w.rule.name), "Rule %d", number);
    gLabBook.rules[gLabBook.count] = w.rule;
    gLabUi.ruleIndex = gLabBook.count;
    ++gLabBook.count;
  } else {
    gLabBook.rules[gLabUi.ruleIndex] = w.rule;
  }
  labSaveBook();
  labOpenRule(gLabUi.ruleIndex);
}
void labWizardCondDone() {
  LabWizard& w = gLabWizard;
  if (w.condAt >= 0) w.rule.cond[w.condAt] = w.cond;
  else w.rule.cond[w.rule.conds++] = w.cond;
  if (w.fresh) labWizardGo(LabStep::Out);
  else labWizardFinish();
}
void labWizardActionDone() {
  LabWizard& w = gLabWizard;
  if (w.actionAt >= 0) w.rule.action[w.actionAt] = w.action;
  else w.rule.action[w.rule.actions++] = w.action;
  labWizardFinish();
}
void labWizardPick() {
  LabWizard& w = gLabWizard;
  const int pick = w.row;
  switch (w.step) {
    case LabStep::Sig:
      w.cond.sig = static_cast<Sig>(pick);
      labWizardGo(LabStep::Op);
      break;
    case LabStep::Op:
      if (lab::info(w.cond.sig).onOff) {
        lab::setOnOff(w.cond, pick == 0);
        labWizardCondDone();
      } else if (pick == 2) {
        w.cond.op = lab::Op::Changes;
        labWizardCondDone();
      } else {
        w.cond.op = pick == 0 ? lab::Op::Below : lab::Op::Above;
        labWizardGo(LabStep::Value);
      }
      break;
    case LabStep::Value:
      labWizardCondDone();
      break;
    case LabStep::Out: {
      w.action.out = static_cast<Out>(pick);
      const lab::OutInfo& info = lab::info(w.action.out);
      w.action.value = lab::clampTo<int16_t>(w.action.value, info.lo, info.hi);
      labWizardGo(LabStep::How);
      break;
    }
    case LabStep::How:
      if (lab::info(w.action.out).onOff) {
        w.action.how = lab::How::Set;
        w.action.value = pick == 0 ? 1 : 0;
        labWizardActionDone();
      } else if (pick == 0) {
        w.action.how = lab::How::Set;
        labWizardGo(LabStep::OutValue);
      } else {
        w.action.how = pick == 1 ? lab::How::Follow : lab::How::FollowBack;
        labWizardGo(LabStep::Source);
      }
      break;
    case LabStep::OutValue:
      labWizardActionDone();
      break;
    default:
      w.action.source = static_cast<Sig>(pick);
      labWizardActionDone();
      break;
  }
}
// One step back, and out to where the wizard started from the first step.
void labWizardBack() {
  LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Op: labWizardGo(LabStep::Sig); return;
    case LabStep::Value: labWizardGo(LabStep::Op); return;
    case LabStep::How: labWizardGo(LabStep::Out); return;
    case LabStep::OutValue: case LabStep::Source: labWizardGo(LabStep::How); return;
    case LabStep::Out:
      if (w.fresh) {
        // The condition is already in the working rule, so it's taken back out to be asked again.
        if (w.rule.conds) --w.rule.conds;
        labWizardGo(LabStep::Sig);
        return;
      }
      break;
    default: break;
  }
  if (w.fresh) gLabUi.view = LabView::Rules;
  else gLabUi.view = LabView::Rule;
}
void labCardMove(const lab::Rule& rule, int delta) {
  LabCardRow rows[kLabMostCardRows];
  const int count = labCardList(rule, rows);
  gLabUi.deleteArmed = false;
  int row = gLabUi.cardRow;
  do {
    row = (row + delta + count) % count;
  } while (rows[row].line == LabLine::Then);
  gLabUi.cardRow = row;
  labKeepRowShown(row, gLabUi.cardTop, kLabCardRows);
}
void labRemoveRule(int index) {
  for (int i = index; i + 1 < gLabBook.count; ++i) gLabBook.rules[i] = gLabBook.rules[i + 1];
  --gLabBook.count;
  labSaveBook();
  gLabUi.ruleRow = lab::clampTo(gLabUi.ruleRow, 0, labRuleRows() - 1);
  gLabUi.ruleTop = 0;
  gLabUi.view = LabView::Rules;
}
// Removing a line can shorten the card, or leave the cursor on the DO label.
void labCardSettle(const lab::Rule& rule) {
  LabCardRow rows[kLabMostCardRows];
  const int count = labCardList(rule, rows);
  int row = min(gLabUi.cardRow, count - 1);
  if (rows[row].line == LabLine::Then) ++row;
  gLabUi.cardRow = row;
  labKeepRowShown(row, gLabUi.cardTop, kLabCardRows);
}
void labRuleCardKey(char key, char lower, bool left, bool right, bool up, bool down, bool enter, bool del) {
  lab::Rule& rule = gLabBook.rules[gLabUi.ruleIndex];
  LabCardRow rows[kLabMostCardRows];
  labCardList(rule, rows);
  const LabCardRow chosen = rows[gLabUi.cardRow];
  if (up || down) { labCardMove(rule, up ? -1 : 1); return; }
  if (chosen.line == LabLine::Name) {
    const size_t length = strlen(rule.name);
    if (del && length) rule.name[length - 1] = '\0';
    else if (length < lab::kNameChars && (isalnum(static_cast<unsigned char>(key)) || key == ' ')) {
      rule.name[length] = key;
      rule.name[length + 1] = '\0';
    }
    return;
  }
  if (lower == 'p') {
    labSaveBook();
    labPlay();
    gLabUi.view = LabView::Play;
    gLabUi.playPage = 0;
    gLabUi.boardTop = 0;
    return;
  }
  if (chosen.line == LabLine::Hold && (left || right)) {
    const int ms = rule.holdMs + (right ? lab::kHoldStepMs : -static_cast<int>(lab::kHoldStepMs));
    rule.holdMs = static_cast<uint16_t>(lab::clampTo<int>(ms, 0, lab::kMaxHoldMs));
    return;
  }
  if (lower == 'x' && chosen.line == LabLine::Cond && rule.conds > 1) {
    for (int i = chosen.index; i + 1 < rule.conds; ++i) rule.cond[i] = rule.cond[i + 1];
    --rule.conds;
    labCardSettle(rule);
    return;
  }
  if (lower == 'x' && chosen.line == LabLine::Action && rule.actions > 1) {
    for (int i = chosen.index; i + 1 < rule.actions; ++i) rule.action[i] = rule.action[i + 1];
    --rule.actions;
    labCardSettle(rule);
    return;
  }
  if (!enter) return;
  switch (chosen.line) {
    case LabLine::Join: rule.any = !rule.any; break;
    case LabLine::Cond: labWizardStart(false, chosen.index, -1, false); break;
    case LabLine::AddCond: labWizardStart(false, -1, -1, false); break;
    case LabLine::Action: labWizardStart(false, -1, chosen.index, true); break;
    case LabLine::AddAction: labWizardStart(false, -1, -1, true); break;
    case LabLine::Delete:
      if (gLabUi.deleteArmed) labRemoveRule(gLabUi.ruleIndex);
      else gLabUi.deleteArmed = true;
      break;
    default: break;
  }
}
bool handleLabKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  const bool up = key == ';', down = key == '.', left = key == ',', right = key == '/';
  const bool enter = keys.enter || key == '\r' || key == '\n';
  const bool del = keys.del || key == '\b';
  LabUi& ui = gLabUi;
  switch (ui.view) {
    case LabView::Devices:
      if (up || down) {
        ui.devRow = (ui.devRow + (up ? lab::kDevCount - 1 : 1)) % lab::kDevCount;
        labKeepRowShown(ui.devRow, ui.devTop, kLabListRows);
      } else if (enter) {
        labOpenDevice(static_cast<Dev>(ui.devRow));
      } else if (lower == 'w') {
        ui.view = LabView::Wiring;
      }
      break;
    case LabView::Device:
      if (lower == 'w' && static_cast<Dev>(ui.devRow) == Dev::Hub) ui.view = LabView::Wiring;
      else labDeviceKey(key, lower, left, right, up, down, enter);
      break;
    case LabView::Wiring:
      if (up || down) ui.wiringRow = (ui.wiringRow + (up ? kLabLastPort : 1)) % pbhub::kPorts;
      else if (left || right) gLabBook.wiring.cycle(ui.wiringRow, right ? 1 : -1);
      break;
    case LabView::Rules: {
      const int rows = labRuleRows();
      if (up || down) {
        ui.ruleRow = (ui.ruleRow + (up ? rows - 1 : 1)) % rows;
        labKeepRowShown(ui.ruleRow, ui.ruleTop, kLabListRows);
      } else if (enter) {
        if (ui.ruleRow == gLabBook.count) labWizardStart(true, -1, -1, false);
        else labOpenRule(ui.ruleRow);
      } else if (key == ' ' && ui.ruleRow < gLabBook.count) {
        gLabBook.rules[ui.ruleRow].on = !gLabBook.rules[ui.ruleRow].on;
        labSaveBook();
      } else if (lower == 'p' && gLabBook.count) {
        labPlay();
        ui.view = LabView::Play;
        ui.playPage = 0;
        ui.boardTop = 0;
      }
      break;
    }
    case LabView::Rule:
      labRuleCardKey(key, lower, left, right, up, down, enter, del);
      break;
    case LabView::Wizard: {
      LabWizard& w = gLabWizard;
      if (w.step == LabStep::Value) {
        const lab::SigInfo& info = lab::info(w.cond.sig);
        if (left || right) {
          labTeachValue();
          w.teaching = false;
          w.cond.value = lab::clampTo<int16_t>(w.cond.value + (right ? 1 : -1), info.lo, info.hi);
        } else if (enter) {
          labWizardPick();
        }
      } else if (w.step == LabStep::OutValue) {
        const lab::OutInfo& info = lab::info(w.action.out);
        if (left || right) w.action.value = lab::clampTo<int16_t>(w.action.value + (right ? info.step : -info.step), info.lo, info.hi);
        else if (enter) labWizardPick();
      } else if (up || down) {
        const int rows = labWizardRowCount();
        w.row = (w.row + (up ? rows - 1 : 1)) % rows;
        labKeepRowShown(w.row, w.top, kLabWizardRows);
      } else if (enter) {
        labWizardPick();
      }
      break;
    }
    case LabView::Play:
      if (left || right) {
        const int pages = labPlayPages();
        ui.playPage = (ui.playPage + (right ? 1 : pages - 1)) % pages;
      } else if ((up || down) && ui.playPage == 0) {
        ui.boardTop = lab::clampTo(ui.boardTop + (up ? -1 : 1), 0, max<int>(0, gLabBook.count - 3));
      } else if (key == ' ') {
        gLabPaused = !gLabPaused;
        if (gLabPaused) labReleaseOutputs();
      }
      break;
  }
  labRedraw();
  return true;
}
// Esc goes back one screen, and only leaves the Lab from the screen it opened on.
bool routeLabEscape() {
  if (!labMode(gUiMode) || gLauncherVisible) return false;
  LabUi& ui = gLabUi;
  switch (ui.view) {
    case LabView::Devices: case LabView::Rules: return false;
    case LabView::Device: labCloseDevice(); break;
    case LabView::Wiring:
      // Nothing is driven on this screen. The parts may have moved, so the hub starts over.
      gLabHubReady = false;
      labSaveBook();
      ui.view = gUiMode == UiMode::LabDevices ? LabView::Devices : LabView::Rules;
      break;
    case LabView::Rule:
      labSaveBook();
      ui.view = LabView::Rules;
      break;
    case LabView::Wizard: labWizardBack(); break;
    default:
      labStopPlaying();
      ui.view = LabView::Rules;
      break;
  }
  labRedraw();
  return true;
}
// Each menu entry opens on its own first screen.
void labEnterView(UiMode mode) { gLabUi.view = mode == UiMode::LabRules ? LabView::Rules : LabView::Devices; }
}  // namespace
