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
constexpr int kLabListRows = 5, kLabRowH = 21, kLabListY = 20, kLabWizardRows = 4, kLabRecipeRows = 4;
constexpr int kLabTrailSteps = 32;    // More than the longest walk through a rule.
constexpr int kLabGraphTop = 75, kLabGraphH = 48;
constexpr const char kLabArrowUpDown[] = "\x18\x19", kLabArrowLeftRight[] = "\x1b\x1a";
constexpr const char kLabDeg[] = "\xf8";
constexpr int kLabServoStep = 5, kLabServoFine = 1, kLabHueStep = 15, kLabBrightStep = 5, kLabToneStep = 50;
constexpr uint32_t kLabSweepStepMs = 40;
constexpr uint32_t kLabBeepMs = 400;
constexpr int kLabLastPort = pbhub::kPorts - 1;
constexpr int kLabLedMilliamps = 33;  // M5 rates this strip at 2 A a meter, with 60 LEDs a meter.

enum class LabView : uint8_t { Devices, Device, Wiring, Rules, Wizard, Play, Recipes, Say, Offer };
enum class LabStep : uint8_t { Sig, Op, Value, MoreConds, Join, Out, How, OutValue, Source, MoreActions, Hold, Name };

struct LabUi {
  LabView view = LabView::Devices;
  int devRow = 0, devTop = 0;
  int wiringRow = 0;
  int ruleRow = 0, ruleTop = 0;
  bool deleteArmed = false;
  int recipeRow = 0, recipeTop = 0;
  int playRule = 0;             // Which rule the flow is showing.
  bool practicing = false;
  int practiceCond = 0;
  bool sayAsked = false, sayHeard = false, sayFailed = false;
  // The test screens.
  int servoAngle = 90, stripHue = 200, stripBright = 40, stripLed = 0, ledHue = 120, toneHz = 880;
  bool sweeping = false, sweepUp = true, ledOn = false, relays[2] = {false, false};
  uint32_t sweepMs = 0, beepUntilMs = 0;
} gLabUi;

struct LabTrail {
  LabStep step;
  uint8_t cond, action;
};
// One walk through a rule, on a copy, so Esc can leave without changing anything saved.
struct LabWizard {
  lab::Rule rule;
  int index = -1;               // Which saved rule this replaces, or -1 for a new one.
  uint8_t cond = 0, action = 0; // The line being asked about.
  uint8_t keepConds = 1, keepActions = 1;
  LabStep step = LabStep::Sig;
  int row = 0, top = 0;
  bool teaching = true;         // The value follows the live reading until a key nudges it.
  bool nameTouched = false;
  LabTrail trail[kLabTrailSteps];
  uint8_t depth = 0;
} gLabWizard;
lab::Rule gLabOffer;            // A rule from Claude, waiting for Enter.

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
  if (!ms) return "Stop at once";
  const int tenths = ms / 100;
  return String(tenths / 10) + (tenths % 10 ? String(".") + String(tenths % 10) : String("")) + " sec";
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
  // A long name gives way to the chip, which is what changes while you watch.
  d.setFont(&fonts::Font0);
  const int chipWidth = chip.isEmpty() ? 0 : d.textWidth(chip) + 14;
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(TFT_WHITE);
  d.setCursor(23, 1);
  d.print(fitCurrentFontToWidth(title, 238 - 23 - chipWidth));
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
    case Dev::Lamp: return kBulb;
    case Dev::Ac: return kRemote;
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
    case Dev::Lamp: return gSmartLamp.online ? "online" : "searching";
    case Dev::Ac: return "sends only";
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
// These already have an app that is their full test, so the Lab points there.
void renderLabHandOver(Dev dev, const char* title, const char* place, const char* line1, const char* line2, const char* keys) {
  labDrawAnswering(dev);
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, title, TFT_WHITE);
  labTextAt(&fonts::Font0, 6, 52, place, kLabMuted);
  labTextAt(&fonts::Font0, 6, 68, line1, kLabSoft);
  labTextAt(&fonts::Font0, 6, 79, line2, kLabSoft);
  labKeys(keys);
}
void renderLabNfc() {
  renderLabHandOver(Dev::Nfc, "NFC unit found", "Grove 0x50", "NFC Notes reads and writes tags, and", "it is the full test for this unit.",
                    "ENTER open NFC Notes  ESC back");
}
void renderLabLamp() {
  labDrawHeadFor(Dev::Lamp, kLabLime, gSmartLamp.online ? "ONLINE" : "SEARCHING", gSmartLamp.online ? kLabGreen : kLabAmber);
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, gSmartLamp.online ? "smartLamp found" : "Looking on Wi-Fi", TFT_WHITE);
  labTextAt(&fonts::Font0, 6, 52, "Rules can switch it and set brightness", kLabSoft);
  labTextAt(&fonts::Font0, 6, 63, "while it's on the same Wi-Fi.", kLabSoft);
  labKeys("ENTER open smartLamp  ESC back");
}
void renderLabAc() {
  labDrawHeadFor(Dev::Ac, kLabLime, "BUILT IN", kLabSoft);
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, "IR air conditioner", TFT_WHITE);
  labTextAt(&fonts::Font0, 6, 52, "Rules can turn a TCL or Daikin AC on", kLabSoft);
  labTextAt(&fonts::Font0, 6, 63, "and off. IR can't hear back, so point", kLabSoft);
  labTextAt(&fonts::Font0, 6, 74, "the Cardputer at it and watch.", kLabSoft);
  labKeys("ENTER open IR Remote  ESC back");
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
    case Dev::Lamp: renderLabLamp(); break;
    case Dev::Ac: renderLabAc(); break;
    case Dev::Led: renderLabLed(); break;
    default: renderLabSpeaker(); break;
  }
}

// --- Rules list --------------------------------------------------------------------------------
int labRuleRows() { return gLabBook.count < lab::kMaxRules ? gLabBook.count + 3 : gLabBook.count; }
void renderLabRules() {
  auto& d = gCanvas;
  labHead("Rules", launcher_icons::kFlow, kLabLime, String(int(gLabBook.count)) + " SAVED", gLabSaved ? kLabSoft : kLabAmber);
  static constexpr const char* kExtras[3] = {"+ New rule", "Say a rule", "Recipes"};
  const int rows = labRuleRows();
  for (int row = 0; row < min(kLabListRows, rows); ++row) {
    const int i = gLabUi.ruleTop + row;
    const int y = kLabListY + row * kLabRowH;
    const bool chosen = i == gLabUi.ruleRow;
    if (chosen) labRowBar(y, 233, kLabLime);
    if (i >= gLabBook.count) {
      labTextAt(&fonts::FreeSansBold9pt7b, 21, y + 2, kExtras[i - gLabBook.count], chosen ? TFT_BLACK : i == gLabBook.count ? kLabLime : kLabCyan);
      if (i == gLabBook.count + 2) labTextRight(&fonts::Font0, 228, y + 6, String(lab::kRecipeCount) + " ready to go", chosen ? TFT_BLACK : kLabMuted);
      continue;
    }
    const lab::Rule& rule = gLabBook.rules[i];
    if (rule.on) d.fillCircle(10, y + 10, 4, chosen ? TFT_BLACK : kLabGreen);
    else d.drawCircle(10, y + 10, 4, chosen ? TFT_BLACK : kLabMuted);
    labTextAt(&fonts::FreeSansBold9pt7b, 21, y + 2, rule.name, chosen ? TFT_BLACK : TFT_WHITE);
    labTextRight(&fonts::Font0, 228, y + 6, String(int(rule.conds)) + " in " + String(int(rule.actions)) + " out", chosen ? TFT_BLACK : kLabMuted);
  }
  labScrollBar(gLabUi.ruleTop, kLabListRows, rows, kLabListY, kLabListRows * kLabRowH - 1);
  if (gLabUi.deleteArmed) labKeys("X again deletes this rule");
  else labKeys("ENTER edit  SPACE on/off  P play  X del");
}
void renderLabRecipes() {
  auto& d = gCanvas;
  labHead("Recipes", launcher_icons::kFlow, kLabLime, "ADD ONE", kLabSoft);
  for (int row = 0; row < kLabRecipeRows; ++row) {
    const int i = gLabUi.recipeTop + row;
    const int y = 20 + row * 22;
    const bool chosen = i == gLabUi.recipeRow;
    if (chosen) d.fillRoundRect(1, y, 233, 21, 6, kLabLime);
    labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 2, lab::kRecipes[i].name, chosen ? TFT_BLACK : TFT_WHITE);
  }
  labScrollBar(gLabUi.recipeTop, kLabRecipeRows, lab::kRecipeCount, 20, kLabRecipeRows * 22 - 1);
  labTextAt(&fonts::Font0, 4, 113, lab::kRecipes[gLabUi.recipeRow].about, kLabAmber);
  labKeys(String(kLabArrowUpDown) + " pick  ENTER add  ESC back");
}
void renderLabSay() {
  const bool working = gSubmitting || gThinking || gPlaybackActive || !gDeferredDeviceActionsJson.isEmpty();
  const char* chip = gRecording ? "LISTENING" : working ? "WRITING" : "READY";
  labHead("Say a rule", launcher_icons::kMic, kLabLime, chip, gRecording ? kLabGreen : working ? kLabAmber : kLabSoft);
  if (gRecording) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Listening...", kLabGreen);
    labTextAt(&fonts::Font0, 6, 52, "Say what to watch and what to do.", kLabSoft);
    labKeys("ENTER done  ESC back");
  } else if (working) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Claude is writing it", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, "You'll see it before it's saved.", kLabSoft);
    labKeys("ESC back");
  } else if (!gWifiReady || gHubHost.isEmpty()) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Needs Wi-Fi", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, "Claude writes this on the bridge.", kLabSoft);
    labKeys("ESC back");
  } else {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, gLabUi.sayFailed ? "No rule came back" : "Tell Claude what you want", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, gLabUi.sayFailed ? "Try again, naming a sensor and what" : "For example:", kLabSoft);
    labTextAt(&fonts::Font0, 6, 63, gLabUi.sayFailed ? "it should do." : "\"When I wave my hand, turn the strip", gLabUi.sayFailed ? kLabSoft : kLabCyan);
    if (!gLabUi.sayFailed) labTextAt(&fonts::Font0, 6, 74, "red and move the servo.\"", kLabCyan);
    labKeys("ENTER talk  ESC back");
  }
}
void renderLabOffer() {
  const lab::Rule& rule = gLabOffer;
  labHead("Claude wrote this", launcher_icons::kFlow, kLabLime, rule.conds > 1 ? (rule.any ? "ANY" : "ALL") : "", kLabSoft);
  int y = 20;
  gCanvas.setFont(&fonts::Font2);
  for (uint8_t i = 0; i < rule.conds; ++i, y += 15) labTextAt(&fonts::Font2, 6, y, fitCurrentFontToWidth(String(!i ? "If " : rule.any ? "or " : "and ") + labCondText(rule.cond[i]), 228), kLabCyan);
  for (uint8_t i = 0; i < rule.actions; ++i, y += 15) labTextAt(&fonts::Font2, 6, y, fitCurrentFontToWidth(labActionText(rule.action[i]), 228), kLabLime);
  labTextAt(&fonts::Font0, 6, 117, String("Name: ") + rule.name, kLabMuted);
  labKeys("ENTER save  E edit  ESC drop it");
}

// --- The steps -----------------------------------------------------------------------------------
lab::Cond& labWizardCond() { return gLabWizard.rule.cond[gLabWizard.cond]; }
lab::Action& labWizardAction() { return gLabWizard.rule.action[gLabWizard.action]; }
int labWizardRowCount() {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: case LabStep::Source: return lab::kSigCount;
    case LabStep::Op: return lab::info(w.rule.cond[w.cond].sig).onOff ? 2 : 3;
    case LabStep::Out: return lab::kOutCount;
    case LabStep::How: return lab::info(w.rule.action[w.action].out).onOff ? 2 : 3;
    case LabStep::MoreConds: case LabStep::MoreActions: case LabStep::Join: return 2;
    default: return 0;
  }
}
String labWizardRow(int i) {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: case LabStep::Source: return lab::kSigs[i].name;
    case LabStep::Op: {
      if (lab::info(w.rule.cond[w.cond].sig).onOff) return i == 0 ? "turns on" : "turns off";
      static constexpr const char* kOps[3] = {"goes under", "goes over", "changes at all"};
      return kOps[i];
    }
    case LabStep::Out: return lab::kOuts[i].name;
    case LabStep::How: {
      if (lab::info(w.rule.action[w.action].out).onOff) return i == 0 ? "turn on" : "turn off";
      static constexpr const char* kHows[3] = {"set to a value", "follow a sensor", "do the opposite"};
      return kHows[i];
    }
    case LabStep::MoreConds: return i == 0 ? "That's all" : "Watch something else";
    case LabStep::MoreActions: return i == 0 ? "That's all" : "Do something else";
    case LabStep::Join: return i == 0 ? "any of them" : "all of them";
    default: return "";
  }
}
// A step opens on the answer already there, so changing a rule is mostly pressing Enter.
void labWizardPreselect() {
  LabWizard& w = gLabWizard;
  const lab::Cond& cond = w.rule.cond[w.cond];
  const lab::Action& action = w.rule.action[w.action];
  w.top = 0;
  switch (w.step) {
    case LabStep::Sig: w.row = static_cast<int>(cond.sig); break;
    case LabStep::Op:
      if (lab::info(cond.sig).onOff) w.row = cond.op == lab::Op::Above ? 0 : 1;
      else w.row = cond.op == lab::Op::Below ? 0 : cond.op == lab::Op::Above ? 1 : 2;
      break;
    case LabStep::Out: w.row = static_cast<int>(action.out); break;
    case LabStep::How:
      if (lab::info(action.out).onOff) w.row = action.value ? 0 : 1;
      else w.row = static_cast<int>(action.how);
      break;
    case LabStep::Source: w.row = static_cast<int>(action.source); break;
    case LabStep::MoreConds: w.row = w.cond + 1 < w.rule.conds ? 1 : 0; break;
    case LabStep::MoreActions: w.row = w.action + 1 < w.rule.actions ? 1 : 0; break;
    case LabStep::Join: w.row = w.rule.any ? 0 : 1; break;
    default: w.row = 0; break;
  }
  labKeepRowShown(w.row, w.top, kLabWizardRows);
}
void labWizardGo(LabStep step) {
  LabWizard& w = gLabWizard;
  if (w.depth < kLabTrailSteps) w.trail[w.depth++] = LabTrail{w.step, w.cond, w.action};
  w.step = step;
  labWizardPreselect();
}
void labWizardStart(int index, const lab::Rule* from) {
  LabWizard& w = gLabWizard;
  w = LabWizard{};
  w.index = index;
  if (from) w.rule = *from;
  else if (index >= 0) w.rule = gLabBook.rules[index];
  w.teaching = !w.rule.conds;
  w.nameTouched = w.rule.name[0] != '\0';
  w.step = LabStep::Sig;
  labWizardPreselect();
  gLabUi.view = LabView::Wizard;
}
// One step back, and out of the wizard from the first one. Nothing was saved, so nothing is lost.
void labWizardBack() {
  LabWizard& w = gLabWizard;
  if (!w.depth) {
    gLabUi.view = LabView::Rules;
    return;
  }
  const LabTrail last = w.trail[--w.depth];
  w.step = last.step;
  w.cond = last.cond;
  w.action = last.action;
  labWizardPreselect();
}
void labWizardFinish() {
  LabWizard& w = gLabWizard;
  w.rule.conds = w.keepConds;
  w.rule.actions = w.keepActions;
  if (!w.rule.name[0]) snprintf(w.rule.name, sizeof(w.rule.name), "Rule %d", gLabBook.count + 1);
  if (w.index >= 0) {
    gLabBook.rules[w.index] = w.rule;
    gLabUi.ruleRow = w.index;
  } else if (gLabBook.count < lab::kMaxRules) {
    gLabBook.rules[gLabBook.count] = w.rule;
    gLabUi.ruleRow = gLabBook.count;
    ++gLabBook.count;
  }
  labSaveBook();
  gLabUi.ruleTop = 0;
  labKeepRowShown(gLabUi.ruleRow, gLabUi.ruleTop, kLabListRows);
  gLabUi.view = LabView::Rules;
}
void labWizardCondsDone() {
  gLabWizard.keepConds = gLabWizard.cond + 1;
  labWizardGo(gLabWizard.keepConds > 1 ? LabStep::Join : LabStep::Out);
}
void labWizardCondLine() {
  LabWizard& w = gLabWizard;
  if (w.rule.conds < w.cond + 1) w.rule.conds = w.cond + 1;
  if (w.cond + 1 < lab::kMaxConds) labWizardGo(LabStep::MoreConds);
  else labWizardCondsDone();
}
void labWizardActionLine() {
  LabWizard& w = gLabWizard;
  if (w.rule.actions < w.action + 1) w.rule.actions = w.action + 1;
  if (w.action + 1 < lab::kMaxActions) {
    labWizardGo(LabStep::MoreActions);
  } else {
    w.keepActions = w.action + 1;
    labWizardGo(LabStep::Hold);
  }
}
void labWizardPick() {
  LabWizard& w = gLabWizard;
  lab::Cond& cond = labWizardCond();
  lab::Action& action = labWizardAction();
  const int pick = w.row;
  switch (w.step) {
    case LabStep::Sig:
      if (static_cast<int>(cond.sig) != pick) w.teaching = true;
      cond.sig = static_cast<Sig>(pick);
      labWizardGo(LabStep::Op);
      break;
    case LabStep::Op:
      if (lab::info(cond.sig).onOff) {
        lab::setOnOff(cond, pick == 0);
        labWizardCondLine();
      } else if (pick == 2) {
        cond.op = lab::Op::Changes;
        labWizardCondLine();
      } else {
        cond.op = pick == 0 ? lab::Op::Below : lab::Op::Above;
        labWizardGo(LabStep::Value);
      }
      break;
    case LabStep::Value:
      labWizardCondLine();
      break;
    case LabStep::MoreConds:
      if (pick == 0) {
        labWizardCondsDone();
      } else {
        ++w.cond;
        if (w.cond >= w.rule.conds) w.rule.cond[w.cond] = lab::Cond{};
        w.teaching = w.cond >= w.rule.conds;
        labWizardGo(LabStep::Sig);
      }
      break;
    case LabStep::Join:
      w.rule.any = pick == 0;
      labWizardGo(LabStep::Out);
      break;
    case LabStep::Out: {
      action.out = static_cast<Out>(pick);
      const lab::OutInfo& info = lab::info(action.out);
      action.value = lab::clampTo<int16_t>(action.value, info.lo, info.hi);
      labWizardGo(LabStep::How);
      break;
    }
    case LabStep::How:
      if (lab::info(action.out).onOff) {
        action.how = lab::How::Set;
        action.value = pick == 0 ? 1 : 0;
        labWizardActionLine();
      } else if (pick == 0) {
        action.how = lab::How::Set;
        labWizardGo(LabStep::OutValue);
      } else {
        action.how = pick == 1 ? lab::How::Follow : lab::How::FollowBack;
        labWizardGo(LabStep::Source);
      }
      break;
    case LabStep::OutValue:
      labWizardActionLine();
      break;
    case LabStep::Source:
      action.source = static_cast<Sig>(pick);
      labWizardActionLine();
      break;
    case LabStep::MoreActions:
      if (pick == 0) {
        w.keepActions = w.action + 1;
        labWizardGo(LabStep::Hold);
      } else {
        ++w.action;
        if (w.action >= w.rule.actions) w.rule.action[w.action] = lab::Action{};
        labWizardGo(LabStep::Out);
      }
      break;
    case LabStep::Hold:
      labWizardGo(LabStep::Name);
      break;
    default:
      labWizardFinish();
      break;
  }
}
void labTeachValue() {
  LabWizard& w = gLabWizard;
  const lab::Reading& reading = gLabInputs.get(labWizardCond().sig);
  if (w.teaching && reading.live) labWizardCond().value = reading.value;
}
String labWizardChip() {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: case LabStep::Op: case LabStep::Value: case LabStep::MoreConds: case LabStep::Join:
      return String("WATCH ") + String(w.cond + 1);
    case LabStep::Hold: case LabStep::Name: return "FINISH";
    default: return String("DO ") + String(w.action + 1);
  }
}
void renderLabWizard() {
  auto& d = gCanvas;
  LabWizard& w = gLabWizard;
  labHead(w.index >= 0 ? w.rule.name : "New rule", launcher_icons::kFlow, kLabLime, labWizardChip(), kLabSoft);
  if (w.step == LabStep::Value) {
    labTeachValue();
    const lab::Cond& cond = labWizardCond();
    const lab::SigInfo& info = lab::info(cond.sig);
    labTextAt(&fonts::Font2, 4, 20, String("Show me: ") + info.name, kLabCyan);
    labTextCenter(&fonts::Font6, 120, 36, String(cond.value), TFT_WHITE);
    const int position = lab::mapRange(cond.value, info.lo, info.hi, 0, 100);
    labBar(8, 87, 224, 9, position, kLabCyan);
    d.fillRect(8 + 224 * position / 100 - 1, 84, 3, 15, TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 104, labValueText(cond.sig, cond.value, kLabDeg), TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 114, w.teaching ? "Set it up, then press ENTER" : "SPACE goes back to the live reading", kLabAmber);
    labKeys(String(kLabArrowLeftRight) + " nudge  ENTER use it  ESC back");
    return;
  }
  if (w.step == LabStep::OutValue) {
    const lab::Action& action = labWizardAction();
    const lab::OutInfo& info = lab::info(action.out);
    labTextAt(&fonts::Font2, 4, 20, String("Set ") + info.name + " to", kLabCyan);
    labTextCenter(&fonts::Font6, 120, 36, String(action.value), TFT_WHITE);
    if (action.out == Out::StripColor || action.out == Out::Led) d.fillRoundRect(8, 90, 224, 16, 5, labHueColor(action.value));
    else labBar(8, 90, 224, 9, lab::mapRange(action.value, info.lo, info.hi, 0, 100), kLabLime);
    labKeys(String(kLabArrowLeftRight) + " change  ENTER use it  ESC back");
    return;
  }
  if (w.step == LabStep::Hold) {
    labTextAt(&fonts::Font2, 4, 20, "After it stops being true", kLabCyan);
    labTextCenter(&fonts::Font4, 120, 48, labHoldText(w.rule.holdMs), TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 88, "How long the rule keeps going", kLabSoft);
    labKeys(String(kLabArrowLeftRight) + " change  ENTER next  ESC back");
    return;
  }
  if (w.step == LabStep::Name) {
    labTextAt(&fonts::Font2, 4, 20, "Give it a name", kLabCyan);
    d.fillRoundRect(4, 46, 232, 28, 6, kLabPanel);
    const String name = w.rule.name[0] || w.nameTouched ? String(w.rule.name) : String("Rule ") + String(gLabBook.count + 1);
    labTextAt(&fonts::FreeSansBold9pt7b, 12, 51, name + (w.nameTouched ? "_" : ""), w.nameTouched ? TFT_WHITE : kLabMuted);
    labTextAt(&fonts::Font0, 6, 84, String(int(w.keepConds)) + " to watch, " + String(int(w.keepActions)) + " to do", kLabSoft);
    labKeys("Type a name  ENTER save  ESC back");
    return;
  }
  String question;
  switch (w.step) {
    case LabStep::Sig: question = w.cond ? "And what else?" : "What should it watch?"; break;
    case LabStep::Op: question = lab::info(labWizardCond().sig).name; break;
    case LabStep::MoreConds: question = "Anything else to watch?"; break;
    case LabStep::Join: question = "Fire when it's"; break;
    case LabStep::Out: question = w.action ? "And what else?" : "What should it do?"; break;
    case LabStep::How: question = lab::info(labWizardAction().out).name; break;
    case LabStep::MoreActions: question = "Anything else to do?"; break;
    default: question = "Follow which sensor?"; break;
  }
  labTextAt(&fonts::Font2, 4, 20, question, kLabCyan);
  if (w.step == LabStep::Op) {
    const Sig sig = labWizardCond().sig;
    const lab::Reading& reading = gLabInputs.get(sig);
    labTextRight(&fonts::Font2, 236, 20, reading.live ? String("now ") + labValueText(sig, reading.value) : String("no reading"), reading.live ? kLabGreen : kLabMuted);
  }
  const int rows = labWizardRowCount();
  for (int row = 0; row < min(kLabWizardRows, rows); ++row) {
    const int i = w.top + row;
    const int y = 37 + row * 22;
    const bool chosen = i == w.row;
    if (chosen) d.fillRoundRect(1, y, 233, 21, 6, kLabLime);
    gCanvas.setFont(&fonts::FreeSansBold9pt7b);
    labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 2, fitCurrentFontToWidth(labWizardRow(i), w.step == LabStep::Sig || w.step == LabStep::Source ? 150 : 218), chosen ? TFT_BLACK : TFT_WHITE);
    if (w.step == LabStep::Sig || w.step == LabStep::Source) {
      const Sig sig = static_cast<Sig>(i);
      const lab::Reading& reading = gLabInputs.get(sig);
      labTextRight(&fonts::Font2, 228, y + 2, reading.live ? labValueText(sig, reading.value) : String("--"), chosen ? TFT_BLACK : reading.live ? kLabGreen : kLabMuted);
    }
  }
  labScrollBar(w.top, kLabWizardRows, rows, 37, kLabWizardRows * 22 - 1);
  labKeys(String(kLabArrowUpDown) + " pick  ENTER next  ESC back");
}

// --- Playing -------------------------------------------------------------------------------------
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
// Boxes in a column share the height between the flow's header and footer.
int labFlowBoxH(int count) { return count > 2 ? 28 : 40; }
int labFlowBoxY(int index, int count) {
  if (count == 1) return 47;
  return count == 2 ? 23 + index * 47 : 22 + index * 31;
}
void renderLabFlow(int index) {
  auto& d = gCanvas;
  const lab::Rule& rule = gLabBook.rules[index];
  const lab::RuleState& state = gLabStates[index];
  const bool practicing = gLabUi.practicing;
  // Paused or switched off, nothing is being driven, so nothing should look driven.
  const bool firing = state.active && rule.on && !gLabPaused;
  const uint16_t live = firing ? kLabGreen : kLabDim;
  const char* word = gLabPaused ? "PAUSED " : practicing ? "PRACTICE " : !rule.on ? "OFF " : firing ? "FIRING " : "WAITING ";
  const String tag = String(word) + String(index + 1) + "/" + String(int(gLabBook.count));
  labHead(rule.name, launcher_icons::kFlow, kLabLime, tag, gLabPaused || practicing ? kLabAmber : firing ? kLabGreen : kLabSoft);
  constexpr int kLeft = 2, kRight = 160, kBoxW = 78, cx = 120, cy = 67, kCircle = 17;
  const int dot = firing ? static_cast<int>(millis() / 80) : -1;
  const uint32_t now = millis();
  for (int i = 0; i < rule.conds; ++i) {
    const lab::Cond& cond = rule.cond[i];
    const int h = labFlowBoxH(rule.conds), y = labFlowBoxY(i, rule.conds);
    const bool holds = lab::condHolds(cond, gLabSeen, now);
    const bool picked = practicing && i == gLabUi.practiceCond;
    labCurve(kLeft + kBoxW, y + h / 2, cx - kCircle, cy, holds ? kLabGreen : kLabDim, holds ? dot : -1);
    d.fillRoundRect(kLeft, y, kBoxW, h, 5, kLabPanel);
    if (picked) d.drawRoundRect(kLeft, y, kBoxW, h, 5, kLabAmber);
    else if (holds) d.drawRoundRect(kLeft, y, kBoxW, h, 5, kLabGreen);
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, kLeft + 5, y + 3, fitCurrentFontToWidth(lab::info(cond.sig).name, kBoxW - 6), picked ? kLabAmber : holds ? kLabGreen : kLabMuted);
    const lab::Reading& reading = gLabSeen.get(cond.sig);
    labTextAt(&fonts::Font2, kLeft + 5, y + (h > 30 ? 17 : 11), reading.live ? labValueText(cond.sig, reading.value) : String("--"), TFT_WHITE);
  }
  for (int i = 0; i < rule.actions; ++i) {
    const lab::Action& action = rule.action[i];
    const int h = labFlowBoxH(rule.actions), y = labFlowBoxY(i, rule.actions);
    labCurve(cx + kCircle, cy, kRight, y + h / 2, live, dot);
    d.fillRoundRect(kRight, y, kBoxW, h, 5, kLabPanel);
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, kRight + 5, y + 3, fitCurrentFontToWidth(lab::info(action.out).name, kBoxW - 6), kLabMuted);
    int16_t value = 0;
    const bool known = firing && lab::actionValue(action, gLabSeen, value);
    if (known && (action.out == Out::StripColor || action.out == Out::Led)) d.fillRoundRect(kRight + 5, y + (h > 30 ? 16 : 12), kBoxW - 10, h > 30 ? 16 : 12, 3, labHueColor(value));
    else labTextAt(&fonts::Font2, kRight + 5, y + (h > 30 ? 17 : 11), known ? labOutText(action.out, value) : String("--"), TFT_WHITE);
  }
  d.fillCircle(cx, cy, kCircle, live);
  labTextCenter(&fonts::Font2, cx, cy - 7, rule.conds > 1 ? (rule.any ? "ANY" : "ALL") : "IF", firing ? TFT_BLACK : kLabSoft);
  labTextAt(&fonts::Font0, 4, 116, state.fires == 1 ? String("Fired once") : String("Fired ") + String(int(state.fires)) + " times", kLabSoft);
  // Every rule at a glance, so a rule firing on another page still shows.
  const int count = gLabBook.count;
  for (int i = 0; i < count; ++i) {
    const int x = 236 - (count - 1 - i) * 9;
    const bool on = gLabBook.rules[i].on && gLabStates[i].active && !gLabPaused;
    d.fillCircle(x - 2, 119, 3, on ? kLabGreen : kLabPanel);
    if (i == index) d.drawCircle(x - 2, 119, 3, TFT_WHITE);
  }
  if (practicing) labKeys(String(kLabArrowUpDown) + " change  " + kLabArrowLeftRight + " sensor  ESC end practice");
  else labKeys(String(kLabArrowLeftRight) + " rule T practice SPACE pause ESC stop");
}
void renderLabUi() {
  switch (gLabUi.view) {
    case LabView::Devices: renderLabDevices(); break;
    case LabView::Device: renderLabDevice(); break;
    case LabView::Wiring: renderLabWiring(); break;
    case LabView::Rules: renderLabRules(); break;
    case LabView::Wizard: renderLabWizard(); break;
    case LabView::Recipes: renderLabRecipes(); break;
    case LabView::Say: renderLabSay(); break;
    case LabView::Offer: renderLabOffer(); break;
    default: renderLabFlow(gLabUi.playRule); break;
  }
  renderLauncherOverlay();
  renderDebugOverlay();
}

// --- Keys ----------------------------------------------------------------------------------------
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
    case Dev::Lamp:
      if (enter) setUiMode(UiMode::SmartLamp);
      return;
    case Dev::Ac:
      if (enter) setUiMode(UiMode::IrRemote);
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
void labRemoveRule(int index) {
  for (int i = index; i + 1 < gLabBook.count; ++i) gLabBook.rules[i] = gLabBook.rules[i + 1];
  --gLabBook.count;
  labSaveBook();
  gLabUi.ruleRow = lab::clampTo(gLabUi.ruleRow, 0, labRuleRows() - 1);
  gLabUi.ruleTop = 0;
  labKeepRowShown(gLabUi.ruleRow, gLabUi.ruleTop, kLabListRows);
}
void labStartPlaying() {
  gLabUi.playRule = 0;
  gLabUi.practicing = false;
  gLabUi.practiceCond = 0;
  labPlay();
  gLabUi.view = LabView::Play;
}
void labRulesKey(char key, char lower, bool up, bool down, bool enter) {
  LabUi& ui = gLabUi;
  const int rows = labRuleRows();
  const bool onRule = ui.ruleRow < gLabBook.count;
  if (lower != 'x') ui.deleteArmed = false;
  if (up || down) {
    ui.ruleRow = (ui.ruleRow + (up ? rows - 1 : 1)) % rows;
    labKeepRowShown(ui.ruleRow, ui.ruleTop, kLabListRows);
  } else if (enter && onRule) {
    labWizardStart(ui.ruleRow, nullptr);
  } else if (enter && ui.ruleRow == gLabBook.count) {
    labWizardStart(-1, nullptr);
  } else if (enter && ui.ruleRow == gLabBook.count + 1) {
    ui.sayFailed = false;
    ui.view = LabView::Say;
  } else if (enter) {
    ui.view = LabView::Recipes;
  } else if (key == ' ' && onRule) {
    gLabBook.rules[ui.ruleRow].on = !gLabBook.rules[ui.ruleRow].on;
    labSaveBook();
  } else if (lower == 'p' && gLabBook.count) {
    labStartPlaying();
  } else if (lower == 'x' && onRule) {
    if (ui.deleteArmed) labRemoveRule(ui.ruleRow);
    ui.deleteArmed = !ui.deleteArmed;
  }
}
void labWizardKey(char key, bool left, bool right, bool up, bool down, bool enter, bool del) {
  LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Value: {
      const lab::SigInfo& info = lab::info(labWizardCond().sig);
      if (key == ' ') {
        w.teaching = !w.teaching;
      } else if (left || right) {
        labTeachValue();
        w.teaching = false;
        labWizardCond().value = lab::clampTo<int16_t>(labWizardCond().value + (right ? 1 : -1), info.lo, info.hi);
      } else if (enter) {
        labWizardPick();
      }
      break;
    }
    case LabStep::OutValue: {
      const lab::OutInfo& info = lab::info(labWizardAction().out);
      lab::Action& action = labWizardAction();
      if (left || right) action.value = lab::clampTo<int16_t>(action.value + (right ? info.step : -info.step), info.lo, info.hi);
      else if (enter) labWizardPick();
      break;
    }
    case LabStep::Hold:
      if (left || right) {
        const int ms = w.rule.holdMs + (right ? lab::kHoldStepMs : -static_cast<int>(lab::kHoldStepMs));
        w.rule.holdMs = static_cast<uint16_t>(lab::clampTo<int>(ms, 0, lab::kMaxHoldMs));
      } else if (enter) {
        labWizardPick();
      }
      break;
    case LabStep::Name: {
      if (enter) {
        labWizardPick();
        break;
      }
      // Typing over the suggestion replaces it. Enter alone keeps it.
      const size_t length = strlen(w.rule.name);
      if (del) {
        w.nameTouched = true;
        if (length) w.rule.name[length - 1] = '\0';
      } else if (isalnum(static_cast<unsigned char>(key)) || key == ' ') {
        w.nameTouched = true;
        if (length < lab::kNameChars) {
          w.rule.name[length] = key;
          w.rule.name[length + 1] = '\0';
        }
      }
      break;
    }
    default:
      if (up || down) {
        const int rows = labWizardRowCount();
        w.row = (w.row + (up ? rows - 1 : 1)) % rows;
        labKeepRowShown(w.row, w.top, kLabWizardRows);
      } else if (enter) {
        labWizardPick();
      }
      break;
  }
}
void labPlayKey(char key, char lower, bool left, bool right, bool up, bool down) {
  LabUi& ui = gLabUi;
  const lab::Rule& rule = gLabBook.rules[ui.playRule];
  if (ui.practicing) {
    if (lower == 't') {
      ui.practicing = false;
      gLabPractice.clear();
    } else if ((left || right) && rule.conds) {
      ui.practiceCond = (ui.practiceCond + (right ? 1 : rule.conds - 1)) % rule.conds;
    } else if ((up || down) && rule.conds) {
      gLabPractice.nudge(rule.cond[ui.practiceCond].sig, up ? 1 : -1, gLabInputs, millis());
      gLabSeen = gLabInputs;
      gLabPractice.overlay(gLabSeen);
    }
    return;
  }
  if (left || right) {
    ui.playRule = (ui.playRule + (right ? 1 : gLabBook.count - 1)) % gLabBook.count;
  } else if (lower == 't') {
    ui.practicing = true;
    ui.practiceCond = 0;
  } else if (key == ' ') {
    gLabPaused = !gLabPaused;
    if (gLabPaused) labReleaseOutputs();
  }
}
// Noticing that Claude replied with no rule. A rule that does come changes the screen itself.
void serviceLabSay() {
  LabUi& ui = gLabUi;
  if (ui.view != LabView::Say || !ui.sayAsked) return;
  if (gRecording) ui.sayHeard = true;
  if (!ui.sayHeard || gRecording || gSubmitting || gThinking || gPlaybackActive || !gDeferredDeviceActionsJson.isEmpty()) return;
  ui.sayAsked = false;
  ui.sayHeard = false;
  ui.sayFailed = true;
  labRedraw();
}
// A rule from Claude waits here until someone presses Enter, because rules can switch mains.
void labOfferRule(const lab::Rule& rule) {
  gLabOffer = rule;
  gLabUi.sayAsked = false;
  gLabUi.sayHeard = false;
  if (!labMode(gUiMode)) setUiMode(UiMode::LabRules);
  labStopPlaying();
  gLabUi.view = LabView::Offer;
  labRedraw();
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
    case LabView::Rules:
      labRulesKey(key, lower, up, down, enter);
      break;
    case LabView::Wizard:
      labWizardKey(key, left, right, up, down, enter, del);
      break;
    case LabView::Recipes:
      if (up || down) {
        ui.recipeRow = (ui.recipeRow + (up ? lab::kRecipeCount - 1 : 1)) % lab::kRecipeCount;
        labKeepRowShown(ui.recipeRow, ui.recipeTop, kLabRecipeRows);
      } else if (enter && gLabBook.count < lab::kMaxRules) {
        gLabBook.rules[gLabBook.count] = lab::recipeRule(ui.recipeRow);
        ui.ruleRow = gLabBook.count;
        ++gLabBook.count;
        labSaveBook();
        ui.ruleTop = 0;
        labKeepRowShown(ui.ruleRow, ui.ruleTop, kLabListRows);
        ui.view = LabView::Rules;
        setStatus("Added to your rules", 1200);
      }
      break;
    case LabView::Say:
      if (enter && gWifiReady && !gSubmitting && !gThinking) {
        ui.sayAsked = true;
        ui.sayFailed = false;
        toggleVoiceByTap();
      }
      break;
    case LabView::Offer:
      if (lower == 'e') {
        labWizardStart(-1, &gLabOffer);
      } else if (enter && gLabBook.count < lab::kMaxRules) {
        gLabBook.rules[gLabBook.count] = gLabOffer;
        ui.ruleRow = gLabBook.count;
        ++gLabBook.count;
        labSaveBook();
        ui.ruleTop = 0;
        labKeepRowShown(ui.ruleRow, ui.ruleTop, kLabListRows);
        ui.view = LabView::Rules;
      } else if (enter) {
        setStatus("Rules are full: delete one first", 1500);
      }
      break;
    case LabView::Play:
      labPlayKey(key, lower, left, right, up, down);
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
    case LabView::Wizard: labWizardBack(); break;
    case LabView::Recipes: case LabView::Offer: ui.view = LabView::Rules; break;
    case LabView::Say:
      ui.sayAsked = false;
      ui.sayHeard = false;
      ui.view = LabView::Rules;
      break;
    default:
      if (ui.practicing) {
        ui.practicing = false;
        gLabPractice.clear();
        break;
      }
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
