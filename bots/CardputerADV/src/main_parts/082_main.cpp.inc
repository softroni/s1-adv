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
constexpr uint16_t kLabGraph = rgb565_local(0, 109, 170), kLabNeedleShade = rgb565_local(109, 182, 0);
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

enum class LabView : uint8_t { Devices, Device, PickPort, Wiring, Rules, Wizard, Play, Recipes, Say, Offer, Dash, Seqs, SeqEdit, SeqAsk, SeqPlay, SeqOffer };
enum class LabStep : uint8_t { Sig, Op, Value, MoreConds, Join, Out, How, OutValue, Source, MoreActions, Hold, Name, PickSeq };

// What a recording in the Lab is for, which sets the words on the screen around it.
enum class LabVoice : uint8_t { NewRule, Change, Ask, Command };

struct LabUi {
  LabView view = LabView::Devices;
  int devRow = 0, devTop = 0;
  int page = 0, dashPage = 0;   // Which page of a device's screen, and of the Dashboard.
  int wiringRow = 0, portRow = 0;
  int ruleRow = 0, ruleTop = 0;
  bool deleteArmed = false;
  int recipeRow = 0, recipeTop = 0;
  int playRule = 0;             // Which rule the flow is showing.
  int seqRow = 0, seqTop = 0;
  bool seqDeleteArmed = false;
  int seqShown = 0;             // Which sequence the play screen is showing.
  LabView seqBack = LabView::Seqs;
  bool practicing = false;
  int practiceCond = 0;
  String playNote;              // What came of a spoken command, for a moment.
  uint32_t playNoteUntilMs = 0;
  String spotNote;              // What came of saving the GPS spot, for a moment.
  uint32_t spotNoteUntilMs = 0;
  LabVoice voice = LabVoice::NewRule;
  LabView sayBack = LabView::Rules;  // Where Esc goes from the Say screen.
  bool sayFailed = false;            // There's an answer to show, or a reason there isn't one.
  String sayNote, sayHeard;
  // The test screens.
  int servoAngle = 90, stripHue = 200, stripBright = 40, stripLed = 0, ledHue = 120, toneHz = 880;
  bool sweeping = false, sweepUp = true, ledOn = false, relays[2] = {false, false};
  uint32_t sweepMs = 0, beepUntilMs = 0;
  // Learning which way the joystick is held: 1 waits for a push right, then 2 for a push up.
  uint8_t stickLearn = 0;
  int stickPush = -1, stickRight[2] = {0, 0};
  uint32_t stickHeldMs = 0;
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
constexpr int kLabOffers = 3;   // One to pick from, for "what can I build?".
lab::Rule gLabOffer;            // The rule from Claude being shown, waiting for Enter.
lab::Rule gLabOffers[kLabOffers];
int gLabOfferCount = 0, gLabOfferAt = 0;
bool gLabOfferBatch = false;    // These arrived together, so they're choices, not replacements.
int gLabOfferChanges = -1;      // The saved rule it would replace, or -1 for a new one.
constexpr uint32_t kLabOfferQuietMs = 1000;
bool gLabOfferSettled = true;   // The keyboard has rested since the offer landed.
uint32_t gLabOfferKeyMs = 0;    // When it landed, or the last key since.
int gLabOfferNext = -1;         // What the next offer replaces. Only a change sets this.
bool gLabVoiceSetting = false;  // Only an answer to a question asked here can move a part.

// The pages themselves are in part 084.
int labDevPages(Dev dev);
bool labArrowsTurnPages(Dev dev);
void labTurnPage(int delta);
void labPageDots();
String labPageKeys(const char* more);
void renderLabPage(Dev dev, int page);
void serviceLabPage(uint32_t now);
bool labPageKey(Dev dev, char key, char lower, bool left, bool right, bool up, bool down, bool enter);
void renderLabStickLearn();
void labStepStickLearn(uint32_t now);
void renderLabDash();
int labDashPages();

// Sequences are in part 085.
void renderLabSeqs();
void renderLabSeqEdit();
void renderLabSeqAsk();
void renderLabSeqPlay();
void renderLabSeqOffer();
void labSeqOfferKey(char lower, bool up, bool down, bool enter);
void labSeqsKey(char lower, bool up, bool down, bool enter);
void labSeqEditKey(char lower, bool left, bool right, bool up, bool down, bool enter);
void labSeqAskKey(char key, bool left, bool right, bool up, bool down, bool enter, bool del);
void labSeqPlayAgain();
void labSeqLeaveEdit();
void labSeqAskBack();

// --- Words ------------------------------------------------------------------------------------
String labNumberText(int value, const char* unit, const char* degree) {
  char text[32];
  units::readingText(text, sizeof(text), value, unit, degree, displayUnits());
  return String(text);
}
// A number on a scale, as the owner reads it, with no unit.
int labShownNumber(Sig sig, int value) { return lab::shownNumber(sig, value, displayUnits()); }
// The ENV III's temperature to a tenth, in the owner's degrees.
String labTempTenthsText(int celsiusTenths) {
  char text[16];
  const bool us = displayUnits() == units::System::US;
  units::tenthsText(text, sizeof(text), us ? units::fahrenheitTenths(celsiusTenths) : celsiusTenths);
  return String(text) + (us ? "`F" : "`C");
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
  if (cond.sig == Sig::Spoken) return cond.op == lab::Op::Above ? "You say its name" : "You haven't said its name";
  const lab::SigInfo& info = lab::info(cond.sig);
  String text = info.name;
  if (cond.op == lab::Op::Changes) return text + " changes";
  if (info.onOff) return text + (cond.op == lab::Op::Above ? " on" : " off");
  return text + (cond.op == lab::Op::Below ? " under " : " over ") + labValueText(cond.sig, cond.value);
}
const char* labSeqName(int index) { return index >= 0 && index < gLabSeqCount ? gLabSeqs[index].name : "a missing sequence"; }
String labActionText(const lab::Action& action, const char* say) {
  if (action.out == Out::Say) return String("Say \"") + say + "\"";
  if (action.out == Out::Sequence) return String("Play ") + labSeqName(action.value);
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
// Three rows of 1, 3 and 5 pixels, or the same on its side.
void labArrow(int x, int y, char kind) {
  auto& d = gCanvas;
  for (int i = 0; i < 3; ++i) {
    if (kind == kLabArrowUpDown[0]) d.drawFastHLine(x + 2 - i, y + 1 + i, i * 2 + 1, kLabSoft);
    else if (kind == kLabArrowUpDown[1]) d.drawFastHLine(x + 2 - i, y + 5 - i, i * 2 + 1, kLabSoft);
    else if (kind == kLabArrowLeftRight[0]) d.drawFastVLine(x + 1 + i, y + 3 - i, i * 2 + 1, kLabSoft);
    else d.drawFastVLine(x + 3 - i, y + 3 - i, i * 2 + 1, kLabSoft);
  }
}
// Font 0 has arrows, but text drawing skips anything below a space, so they're drawn by hand.
void labKeys(const String& text) {
  auto& d = gCanvas;
  d.setFont(&fonts::Font0);
  d.setTextColor(kLabSoft);
  int x = 3;
  String words;
  const auto flush = [&]() {
    if (words.isEmpty()) return;
    d.setCursor(x, 127);
    d.print(words);
    x += d.textWidth(words);
    words = "";
  };
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == kLabArrowUpDown[0] || c == kLabArrowUpDown[1] || c == kLabArrowLeftRight[0] || c == kLabArrowLeftRight[1]) {
      flush();
      labArrow(x, 127, c);
      x += 6;
    } else {
      words += c;
    }
  }
  flush();
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
bool labBusWanted() {
  Unit unit;
  return gLabUi.view == LabView::Device && labUnitOf(static_cast<Dev>(gLabUi.devRow), unit);
}
// Looking at the bus only makes sense while nothing is being driven through those wires.
bool labRescanAllowed() {
  if (gLabUi.view == LabView::Devices) return true;
  if (gLabUi.view != LabView::Device) return false;
  const lab::Part part = lab::partFor(static_cast<Dev>(gLabUi.devRow));
  return lab::Direct::usesWhite(part) && part != lab::Part::Relay;
}
int labPortOf(Dev dev) {
  const lab::Part part = lab::partFor(dev);
  return part == lab::Part::None ? -1 : gLabBook.wiring.portOf(part);
}
bool labOnHub(Dev dev) { return labFound(Unit::Hub) && lab::partFor(dev) != lab::Part::None && !(dev == Dev::Light && labLightIsDigital()); }
// Where a part is plugged in, as the screen should say it.
String labWhere(Dev dev) {
  if (labOnWires(lab::partFor(dev))) return "Grove";
  return String("CH") + String(labPortOf(dev));
}
String labWhereCaps(Dev dev) { return labOnWires(lab::partFor(dev)) ? String("GROVE") : labWhere(dev); }
// Whether this thing can be reached at all right now.
bool labDevHere(Dev dev) {
  Unit unit;
  if (labUnitOf(dev, unit)) return labFound(unit);
  if (dev == Dev::Light && labLightIsDigital()) return true;
  const lab::Part part = lab::partFor(dev);
  if (part != lab::Part::None) {
    if (labFound(Unit::Hub)) return labPortOf(dev) >= 0;
    const bool sensor = lab::Direct::usesWhite(part) && part != lab::Part::Relay;
    return labOnWires(part) && !(sensor && gLabWhiteSilent >= kLabSilentChecks);
  }
  if (dev == Dev::Gps) return gLabGps.hearing(millis());
  if (dev == Dev::Tilt) return gImuEnabled;
  return true;
}
// These take orders and can't answer, so being reachable is all anyone can know. The I/O hub can't
// tell if a port is empty either, so anything on it is in the same position.
bool labDevOutputOnly(Dev dev) { return dev == Dev::Relay || dev == Dev::Servo || dev == Dev::Strip || labOnHub(dev); }
String labDevValue(Dev dev) {
  // An empty hub port reads as noise, so a number only shows once the part has been opened.
  const lab::Part part = lab::partFor(dev);
  if (labOnHub(dev) && !gLabPartUsed[static_cast<uint8_t>(part)]) return labWhere(dev);
  const auto number = [](Sig sig) -> String {
    const lab::Reading& reading = gLabInputs.get(sig);
    return reading.live ? labValueText(sig, reading.value) : String("--");
  };
  switch (dev) {
    case Dev::Ultrasonic: return number(Sig::Distance);
    case Dev::Env: {
      const int degrees = displayUnits() == units::System::US ? units::rounded(units::fahrenheitTenths(gLabTempTenths) / 10.0)
                                                              : gLabTempTenths / 10;
      return gLabInputs.get(Sig::Humidity).live ? String(degrees) + "` " + number(Sig::Humidity) : String("--");
    }
    case Dev::Joystick:
      return String("x") + String(gLabInputs.get(Sig::StickX).value) + " y" + String(gLabInputs.get(Sig::StickY).value);
    case Dev::Knob: return number(Sig::Knob);
    case Dev::Light: return number(Sig::Light);
    case Dev::Motion: return gLabInputs.get(Sig::Motion).value ? "moving" : "still";
    case Dev::Relay: case Dev::Servo: return labWhere(dev);
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
  count += labFound(Unit::DLight) ? 1 : 0;
  if (gLabOnWires && gLabBook.direct.yellow != lab::Part::None) ++count;
  if (gLabOnWires && gLabBook.direct.white != lab::Part::None && gLabBook.direct.white != lab::Part::Relay &&
      gLabWhiteSilent < kLabSilentChecks) ++count;
  return count;
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
    // A hub part can also sit on the Grove port alone, and it can't announce itself there.
    const bool mayBeOnGrove = !labFound(Unit::Hub) && !labAnyUnit() && lab::partFor(dev) != lab::Part::None;
    labTextRight(&fonts::Font2, 216, y + 2, here ? labDevValue(dev) : mayBeOnGrove ? String("press ENTER") : String("not found"), soft);
    const uint16_t dot = chosen ? TFT_BLACK : !here ? kLabMuted : labDevOutputOnly(dev) ? kLabCyan : kLabGreen;
    if (here && !labDevOutputOnly(dev)) d.fillCircle(227, y + 10, 3, dot);
    else d.drawCircle(227, y + 10, 3, dot);
  }
  labScrollBar(gLabUi.devTop, kLabListRows, lab::kDevCount, kLabListY, kLabListRows * kLabRowH - 1);
  labKeys("ENTER open D dash W wiring ALT ask");
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
void labDrawAnswering(Dev dev) {
  // Only an I2C unit really answers. For anything else, the chip just says where it is.
  const bool answers = !labOnHub(dev) && !labOnWires(lab::partFor(dev));
  labDrawHeadFor(dev, kLabLime, answers ? String("ANSWERING") : String("ON ") + labWhereCaps(dev), kLabGreen);
}
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
  labKeys(labPageKeys("R reset  ESC back"));
}
void renderLabEnv() {
  labDrawAnswering(Dev::Env);
  if (gLabInputs.get(Sig::Humidity).live) {
    labTextAt(&fonts::Font4, 6, 24, labTempTenthsText(gLabTempTenths), TFT_WHITE);
    labTextAt(&fonts::Font4, 130, 24, labValueText(Sig::Humidity, gLabInputs.get(Sig::Humidity).value), kLabCyan);
    labTextAt(&fonts::Font0, 6, 54, "TEMPERATURE", kLabMuted);
    labTextAt(&fonts::Font0, 130, 54, "HUMIDITY", kLabMuted);
  } else {
    labTextAt(&fonts::Font4, 6, 33, "--", kLabMuted);
  }
  labDrawGraph();
  labKeys(labPageKeys("Breathe on it"));
}
void renderLabLight() {
  renderLabSensor(Dev::Light, Sig::Light);
  if (labLightIsDigital()) {
    labStat(170, 23, "LIGHT", String(gLabLux) + " lux");
    labStat(170, 48, "GROVE", "0x23");
    labDrawGraph();
    labKeys(labPageKeys("Cover it  R reset"));
    return;
  }
  labStat(170, 23, "RAW", String(gLabLightRaw));
  labStat(170, 48, "ON", labWhere(Dev::Light));
  labDrawGraph();
  labKeys(labPageKeys(labOnHub(Dev::Light) ? "F flip  P port  R reset" : "Cover it  F flip  R reset"));
}
void renderLabTilt() {
  renderLabSensor(Dev::Tilt, Sig::Tilt);
  labStat(170, 23, "ON", "Built in");
  labDrawGraph();
  labKeys(labPageKeys("Tip it  R reset"));
}
void renderLabMotion() {
  labDrawAnswering(Dev::Motion);
  const bool moving = gLabInputs.get(Sig::Motion).value != 0;
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, moving ? "Motion!" : "All still", moving ? kLabGreen : TFT_WHITE);
  labStat(112, 23, "SEEN", gLabMotionCount == 1 ? String("Once") : String(gLabMotionCount) + " times");
  labStat(176, 23, "ON", labWhere(Dev::Motion));
  labTextAt(&fonts::Font0, 6, 54, "Stays on about 2 sec after you stop", kLabMuted);
  labDrawGraph();
  labKeys(labPageKeys(labOnHub(Dev::Motion) ? "Wave  P port  R reset" : "Wave at it  R reset"));
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
  labStat(112, 84, "ON", labWhere(Dev::Knob));
  labKeys(labPageKeys(labOnHub(Dev::Knob) ? "Turn it  P port  R reset" : "Turn it end to end"));
}
void renderLabStick() {
  if (gLabUi.stickLearn) {
    renderLabStickLearn();
    return;
  }
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
  labKeys(labPageKeys("Move it  D set directions"));
}
void renderLabServo() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Servo, kLabLime, labWhereCaps(Dev::Servo) + " SIGNAL SENT", kLabAmber);
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
  labTextCenter(&fonts::Font0, 120, 117, gLabUi.sweeping ? "Sweeping" : "Watch it  N moves  ALT say it", kLabAmber);
  if (labOnHub(Dev::Servo)) labKeys(String(kLabArrowLeftRight) + " turn AD fine C center S sweep P port");
  else labKeys(String(kLabArrowLeftRight) + " turn  A D fine  C center  S sweep");
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
  const char* arrows = test == LabStripTest::Count ? "move" : "color";
  if (labOnHub(Dev::Strip)) labKeys(String(kLabArrowLeftRight) + " " + arrows + " " + kLabArrowUpDown + " bright M mode T time P port");
  else labKeys(String(kLabArrowLeftRight) + " " + arrows + "  " + kLabArrowUpDown + " bright  M mode  T timing");
}
void renderLabRelays() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Relay, kLabLime, labWhereCaps(Dev::Relay) + " SIGNAL SENT", kLabAmber);
  for (int i = 0; i < 2; ++i) {
    const int x = 6 + i * 116;
    const bool on = gLabUi.relays[i];
    d.fillRoundRect(x, 26, 112, 62, 8, on ? kLabLime : kLabPanel);
    labTextCenter(&fonts::Font2, x + 56, 30, String("Relay ") + String(i + 1), on ? TFT_BLACK : TFT_WHITE);
    labTextCenter(&fonts::Font4, x + 56, 50, on ? "ON" : "OFF", on ? TFT_BLACK : TFT_WHITE);
  }
  labTextCenter(&fonts::Font0, 120, 95, "Listen for the click", kLabAmber);
  labTextCenter(&fonts::Font0, 120, 107, "Both turn off when you leave", kLabMuted);
  labKeys(labOnHub(Dev::Relay) ? "1 and 2 switch them  P port  ESC back" : "1 and 2 switch them  ESC back");
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
// The one question for a part on the I/O hub, asked from its own screen.
void renderLabPickPort() {
  auto& d = gCanvas;
  const Dev dev = static_cast<Dev>(gLabUi.devRow);
  const lab::Part part = lab::partFor(dev);
  labDrawHeadFor(dev, kLabLime, "WHICH PORT?", kLabSoft);
  for (int i = 0; i < pbhub::kPorts; ++i) {
    const int y = 20 + i * 17;
    const bool chosen = i == gLabUi.portRow;
    const lab::Part there = gLabBook.wiring.port[i];
    if (chosen) d.fillRoundRect(1, y, 237, 16, 5, kLabLime);
    labTextAt(&fonts::Font2, 8, y, String("CH") + String(i), chosen ? TFT_BLACK : kLabMuted);
    labTextAt(&fonts::Font2, 48, y, lab::kPartNames[static_cast<uint8_t>(there)], chosen ? TFT_BLACK : there == part ? kLabLime : kLabSoft);
  }
  labKeys(String(kLabArrowUpDown) + " pick  ENTER it's here  ESC back");
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
// --- The GPS's round plots: the compass here, and the sky and the mesh in part 084 ------------
constexpr int kLabDialX = 40, kLabDialY = 58, kLabDialR = 31;
// Where `degrees` from north lands, `radius` out from the middle. Screen y grows downwards.
void labDialPoint(int cx, int cy, float radius, int degrees, int& x, int& y) {
  const float radians = degrees * DEG_TO_RAD;
  x = cx + static_cast<int>(roundf(radius * sinf(radians)));
  y = cy - static_cast<int>(roundf(radius * cosf(radians)));
}
// A dark disc with north at the top, and rings a third and two thirds of the way out.
void labDrawDisc(int cx, int cy, int r) {
  auto& d = gCanvas;
  d.fillCircle(cx, cy, r, kLabPanel2);
  d.drawCircle(cx, cy, r * 2 / 3, kLabPanel);
  d.drawCircle(cx, cy, r / 3, kLabPanel);
  d.drawFastHLine(cx - r + 1, cy, 2 * r - 1, kLabPanel);
  d.drawFastVLine(cx, cy - r + 12, 2 * r - 12, kLabPanel);
  d.drawCircle(cx, cy, r, kLabDim);
  labTextCenter(&fonts::Font0, cx, cy - r + 3, "N", kLabRed);
}
// North is always up, because the Cardputer has no compass. The needle is the way you're moving,
// and only shows while you are, and the amber dot on the rim is the way to the saved spot.
void labDrawCompass(const lab_gps::Fix& fix, bool fixed, int spotBearing) {
  auto& d = gCanvas;
  constexpr int cx = kLabDialX, cy = kLabDialY, r = kLabDialR;
  d.fillCircle(cx, cy, r, kLabPanel2);
  d.drawCircle(cx, cy, r, fixed ? kLabMuted : kLabDim);
  for (int deg = 0; deg < 360; deg += 30) {
    const bool cardinal = deg % 90 == 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    labDialPoint(cx, cy, r - (cardinal ? 5 : 3), deg, x0, y0);
    labDialPoint(cx, cy, r - 1, deg, x1, y1);
    d.drawLine(x0, y0, x1, y1, cardinal ? kLabSoft : kLabDim);
  }
  labTextCenter(&fonts::Font0, cx, cy - r + 7, "N", kLabRed);
  labTextCenter(&fonts::Font0, cx, cy + r - 14, "S", kLabMuted);
  labTextAt(&fonts::Font0, cx + r - 14, cy - 3, "E", kLabMuted);
  labTextAt(&fonts::Font0, cx - r + 9, cy - 3, "W", kLabMuted);
  if (spotBearing >= 0) {
    int x = 0, y = 0;
    labDialPoint(cx, cy, r, spotBearing, x, y);
    d.fillCircle(x, y, 4, kLabAmber);
    d.drawCircle(x, y, 4, TFT_BLACK);
  }
  if (fixed && fix.moving()) {
    int tipX = 0, tipY = 0, tailX = 0, tailY = 0, leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    labDialPoint(cx, cy, r - 13, fix.courseDeg, tipX, tipY);  // Short of the letters, so they still read.
    labDialPoint(cx, cy, 9, fix.courseDeg + 180, tailX, tailY);
    labDialPoint(cx, cy, 5, fix.courseDeg + 270, leftX, leftY);
    labDialPoint(cx, cy, 5, fix.courseDeg + 90, rightX, rightY);
    // Lit on one side and shaded on the other, so it reads as a needle with a point.
    d.fillTriangle(tipX, tipY, leftX, leftY, cx, cy, kLabLime);
    d.fillTriangle(tipX, tipY, rightX, rightY, cx, cy, kLabNeedleShade);
    d.fillTriangle(tailX, tailY, leftX, leftY, cx, cy, kLabMuted);
    d.fillTriangle(tailX, tailY, rightX, rightY, cx, cy, kLabDim);
  }
  d.fillCircle(cx, cy, 2, fixed ? TFT_WHITE : kLabMuted);
}
String labCoordinateText(int32_t e5) {
  const int32_t size = e5 < 0 ? -e5 : e5;
  String tail = String(size % 100000);
  while (tail.length() < 5) tail = String("0") + tail;
  return String(e5 < 0 ? "-" : "") + String(size / 100000) + "." + tail;
}
void renderLabGps() {
  const uint32_t now = millis();
  const bool hearing = gLabGps.hearing(now);
  const lab_gps::Fix& fix = gLabGps.fix;
  const bool fixed = hearing && fix.valid;
  const bool us = displayUnits() == units::System::US;
  const bool noting = !gLabUi.spotNote.isEmpty() && int32_t(now - gLabUi.spotNoteUntilMs) < 0;
  if (!hearing) {
    labDrawHeadFor(Dev::Gps, kLabRed, "NO DATA", kLabRed);
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Nothing from the GPS", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 50, "Check the Cap is on the back and", kLabSoft);
    labTextAt(&fonts::Font0, 6, 61, "its antenna is fitted.", kLabSoft);
    labTextAt(&fonts::Font0, 6, 84, "The last page shows who Mesh heard.", kLabMuted);
    labKeys(labPageKeys("M Mesh  ESC back"));
    return;
  }
  labDrawHeadFor(Dev::Gps, kLabLime, fixed ? "FIXED" : "SEARCHING", fixed ? kLabGreen : kLabAmber);
  gCanvas.drawFastVLine(84, 23, fixed ? 98 : 86, kLabPanel);  // Short of the note under both columns.
  constexpr int kRight = 90, kRight2 = 162;
  if (!fixed) {
    labDrawCompass(fix, false, -1);
    labTextCenter(&fonts::Font2, kLabDialX, 94, "No fix", kLabMuted);
    // Before a fix, how many satellites it hears is the thing to watch.
    const int inView = gLabGps.inView(now);
    const int heard = inView ? inView : fix.satellites;
    labTextAt(&fonts::Font0, kRight, 22, "SATELLITES", kLabMuted);
    labTextAt(&fonts::Font4, kRight, 31, String(heard), TFT_WHITE);
    gCanvas.setFont(&fonts::Font4);
    labTextAt(&fonts::Font2, kRight + gCanvas.textWidth(String(heard)) + 4, 41, "heard", kLabMuted);
    labStat(kRight, 62, "TRACKED", inView ? String(gLabGps.tracked(now)) : String("--"));
    labStat(kRight2, 62, "USED", String(fix.satellites));
    labTextAt(&fonts::Font0, kRight, 92, "Next page: the sky", kLabSoft);
    labTextAt(&fonts::Font0, 6, 113, noting ? gLabUi.spotNote : String("First fix can take minutes outdoors"), kLabAmber);
    labKeys(labPageKeys("S save spot  M Mesh  ESC back"));
    return;
  }
  // How far, and which way, the saved spot is from here.
  labLoadSpot();
  String away = "not saved";
  int spotBearing = -1;
  if (gLabSpot.set) {
    const int32_t m = lab_gps::metersBetween(gLabSpot.latE5, gLabSpot.lonE5, fix.latE5, fix.lonE5);
    char text[24];
    units::distanceText(text, sizeof(text), m, displayUnits(), m >= lab_gps::kFarMeters);
    away = text;
    // Closer than this, the GPS's own wander would swing the direction about.
    if (m >= 10) {
      spotBearing = lab_gps::bearingDeg(fix.latE5, fix.lonE5, gLabSpot.latE5, gLabSpot.lonE5);
      if (spotBearing >= 0) away = away + " " + lab_gps::compassPoint(spotBearing);
    }
  }
  labDrawCompass(fix, true, spotBearing);
  labTextCenter(&fonts::Font2, kLabDialX, 92, labCoordinateText(fix.latE5), TFT_WHITE);
  labTextCenter(&fonts::Font2, kLabDialX, 107, labCoordinateText(fix.lonE5), TFT_WHITE);
  char speed[24];
  units::tenthsText(speed, sizeof(speed), us ? units::mphTenths(fix.speedKmhE1) : fix.speedKmhE1);
  labTextAt(&fonts::Font0, kRight, 22, "SPEED", kLabMuted);
  labTextAt(&fonts::Font4, kRight, 31, speed, TFT_WHITE);
  gCanvas.setFont(&fonts::Font4);
  labTextAt(&fonts::Font2, kRight + gCanvas.textWidth(speed) + 4, 41, us ? "mph" : "km/h", kLabMuted);
  labStat(kRight, 62, "HEADING", fix.moving() ? String(fix.courseDeg) + "` " + lab_gps::compassPoint(fix.courseDeg) : String("still"),
          fix.moving() ? TFT_WHITE : kLabMuted);
  labStat(kRight2, 62, "FROM SPOT", noting ? gLabUi.spotNote : away, noting ? kLabAmber : gLabSpot.set ? TFT_WHITE : kLabMuted);
  labStat(kRight, 92, "SATELLITES", String(fix.satellites));
  String height = "--";
  if (fix.hasAltitude) {
    const int32_t shown = units::rounded(fix.altitudeDm * (us ? 0.328084 : 0.1));
    height = String(shown) + (us ? " ft" : " m");
  }
  labStat(kRight2, 92, "ALTITUDE", height, fix.hasAltitude ? TFT_WHITE : kLabMuted);
  labKeys(labPageKeys("S save spot  M Mesh  ESC back"));
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
  labTextCenter(&fonts::Font0, 120, 106, "Turn the page for a piano", kLabAmber);
  labKeys(labPageKeys("") + kLabArrowUpDown + " pitch  ENTER beep  ESC back");
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
    labKeys("P pick its port  ESC back");
    return;
  }
  // Alone on the Grove port it can't answer, so the only clue is its wire.
  if (viaHub && !labAnyUnit()) {
    const bool sensor = lab::Direct::usesWhite(part) && part != lab::Part::Relay;
    labDrawHeadFor(dev, kLabLime, "LOOKING", kLabAmber);
    labTextAt(&fonts::FreeSansBold9pt7b, 4, 21, sensor ? "Nothing on its wire" : "Getting ready", TFT_WHITE);
    labTextAt(&fonts::Font0, 4, 46, "Plug it into the Grove port. It can't", kLabSoft);
    labTextAt(&fonts::Font0, 4, 57, "reply like the I2C units, so the Lab", kLabSoft);
    labTextAt(&fonts::Font0, 4, 68, "watches for it and shows it here.", kLabSoft);
    labKeys("It appears by itself  ESC back");
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
  if (labFound(Unit::Hub) && direct) {
    labTextAt(&fonts::Font0, 8, 97, "The port is fine, so check this cable.", kLabAmber);
    labTextAt(&fonts::Font0, 8, 108, "I/O hub ports can't carry this unit.", kLabAmber);
  } else if (others && viaHub) {
    labTextAt(&fonts::Font0, 8, 97, "Plug in the I/O hub, or unplug these", kLabAmber);
    labTextAt(&fonts::Font0, 8, 108, "to use this part on Grove by itself.", kLabAmber);
  } else if (others) {
    labTextAt(&fonts::Font0, 8, 97, "The port is fine, so check this", kLabAmber);
    labTextAt(&fonts::Font0, 8, 108, "unit's cable and its hub socket.", kLabAmber);
  } else {
    labTextAt(&fonts::Font0, 8, 97, "Nothing answers, so check the", kLabAmber);
    labTextAt(&fonts::Font0, 8, 108, "cable into the Grove port first.", kLabAmber);
  }
  labKeys("It reconnects by itself  ESC back");
}
// What can be set by voice. A misheard word should only ever move something small and in view, so
// nothing here can switch mains.
bool labTakesVoiceSet(Dev dev) { return dev == Dev::Servo || dev == Dev::Strip || dev == Dev::Led || dev == Dev::Speaker; }
void renderLabDevice() {
  const Dev dev = static_cast<Dev>(gLabUi.devRow);
  if (dev == Dev::Gps) {
    // It has pages, and they work with no GPS data, so this comes before the missing screen.
    if (gLabUi.page) renderLabPage(dev, gLabUi.page);
    else renderLabGps();
    labPageDots();
    return;
  }
  if (!labDevHere(dev)) { renderLabMissing(dev); return; }
  if (gLabUi.page) {
    renderLabPage(dev, gLabUi.page);
    labPageDots();
    return;
  }
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
  if (dev != Dev::Servo && labTakesVoiceSet(dev)) labTextRight(&fonts::Font0, 236, 116, "ALT say it", kLabMuted);
  labPageDots();
}

// --- Rules list --------------------------------------------------------------------------------
int labRuleRows() { return gLabBook.count < lab::kMaxRules ? gLabBook.count + 4 : gLabBook.count; }
void renderLabRules() {
  auto& d = gCanvas;
  labHead("Rules", launcher_icons::kFlow, kLabLime, String(int(gLabBook.count)) + " SAVED", gLabSaved ? kLabSoft : kLabAmber);
  static constexpr const char* kExtras[4] = {"+ New rule", "Say a rule", "Recipes", "Ask the Lab"};
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
    labTextRight(&fonts::Font0, 228, y + 6, chosen ? String("ALT change") : String(int(rule.conds)) + " in " + String(int(rule.actions)) + " out", chosen ? TFT_BLACK : kLabMuted);
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
// The screen decides what a recording is for, so a tap of Alt needs no menu.
LabVoice labVoiceNow() {
  const LabUi& ui = gLabUi;
  if (ui.view == LabView::Say) return ui.voice;
  if (ui.view == LabView::Play) return LabVoice::Command;
  if (ui.view != LabView::Rules) return LabVoice::Ask;
  if (ui.ruleRow < gLabBook.count) return LabVoice::Change;
  return ui.ruleRow == gLabBook.count + 1 ? LabVoice::NewRule : LabVoice::Ask;
}
void renderLabAnswer() {
  gCanvas.setFont(&fonts::efontCN_12);
  const auto lines = wrapText(gLabUi.sayNote.isEmpty() ? String("Claude had nothing to say. Try asking another way.") : gLabUi.sayNote, 228);
  for (size_t i = 0; i < lines.size() && i < 6; ++i) labTextAt(&fonts::efontCN_12, 6, 21 + static_cast<int>(i) * 14, lines[i], TFT_WHITE);
  if (!gLabUi.sayHeard.isEmpty()) {
    gCanvas.setFont(&fonts::efontCN_12);
    labTextAt(&fonts::efontCN_12, 6, 108, fitCurrentFontToWidth("Heard: " + gLabUi.sayHeard, 228), kLabCyan);
  }
  labKeys("ALT ask again  ESC back");
}
void renderLabSay() {
  const LabVoice voice = labVoiceNow();
  const bool change = voice == LabVoice::Change, ask = voice == LabVoice::Ask, command = voice == LabVoice::Command;
  const bool listening = voiceToolListening(), working = voiceToolWorking();
  const bool starting = voiceToolStarting();
  const char* chip = listening ? "LISTENING" : working ? (ask ? "LOOKING" : command ? "CHECKING" : "WRITING") : starting ? "STARTING" : "READY";
  const char* title = change ? gLabBook.rules[gLabUi.ruleRow].name : ask ? "Ask the Lab" : command ? "Say its name" : "Say a rule";
  labHead(title, launcher_icons::kMic, kLabLime, chip, listening ? kLabGreen : working || starting ? kLabAmber : kLabSoft);
  if (starting) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Getting ready...", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, "Talk when it says Listening.", kLabSoft);
    labKeys("ESC cancel");
  } else if (listening) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Listening...", kLabGreen);
    labTextAt(&fonts::Font0, 6, 52, command ? "Rules wait until you've finished." : ask && gLabUi.view == LabView::Seqs ? "Describe the steps, or ask anything." : ask ? "Ask about your parts and rules." : change ? "Say what should be different." : "Say what to watch and what to do.", kLabSoft);
    labKeys("ALT send  ESC cancel");
  } else if (working) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, command ? "Checking what you said" : ask ? "Claude is looking" : change ? "Claude is changing it" : "Claude is writing it", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, command ? "Rules carry on when this is done." : ask && gLabUi.view != LabView::Seqs ? "It can see your parts and readings." : "You'll see it before it's saved.", kLabSoft);
    labKeys("ESC back");
  } else if (!gWifiReady || gHubHost.isEmpty()) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Needs Wi-Fi", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, "Claude writes this on the bridge.", kLabSoft);
    labKeys("ESC back");
  } else if (gLabUi.sayFailed && ask) {
    renderLabAnswer();
  } else if (gLabUi.sayFailed) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, change ? "Nothing changed" : "No rule came back", TFT_WHITE);
    gCanvas.setFont(&fonts::efontCN_12);
    const auto lines = wrapText(gLabUi.sayNote.isEmpty() ? String("Try again, naming a sensor and what it should do.") : gLabUi.sayNote, 228);
    for (size_t i = 0; i < lines.size() && i < 3; ++i) labTextAt(&fonts::efontCN_12, 6, 50 + static_cast<int>(i) * 14, lines[i], kLabSoft);
    if (!gLabUi.sayHeard.isEmpty()) {
      gCanvas.setFont(&fonts::efontCN_12);
      labTextAt(&fonts::efontCN_12, 6, 96, fitCurrentFontToWidth("Heard: " + gLabUi.sayHeard, 228), kLabCyan);
    }
    labKeys("ALT try again  ESC back");
  } else if (ask) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "What's your question?", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, "For example:", kLabSoft);
    labTextAt(&fonts::Font0, 6, 63, "\"Why didn't my rule fire?\"", kLabCyan);
    labTextAt(&fonts::Font0, 6, 74, "\"What can I make with these parts?\"", kLabCyan);
    labTextAt(&fonts::Font0, 6, 85, "\"Why won't the strip light up?\"", kLabCyan);
    labKeys("ALT talk  ESC back");
  } else {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Tell Claude what you want", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 52, "For example:", kLabSoft);
    labTextAt(&fonts::Font0, 6, 63, "\"When I wave my hand, turn the strip", kLabCyan);
    labTextAt(&fonts::Font0, 6, 74, "red and move the servo.\"", kLabCyan);
    labKeys("ALT talk  ESC back");
  }
}
bool labSameCond(const lab::Cond& a, const lab::Cond& b) { return a.sig == b.sig && a.op == b.op && a.value == b.value; }
bool labSameAction(const lab::Action& a, const lab::Action& b) {
  if (a.out != b.out || a.how != b.how) return false;
  return a.how == lab::How::Set ? a.value == b.value : a.source == b.source;
}
void renderLabOffer() {
  const lab::Rule& rule = gLabOffer;
  // A change greys out what stayed the same, so what's different is what stands out.
  const lab::Rule* was = gLabOfferChanges >= 0 && gLabOfferChanges < gLabBook.count ? &gLabBook.rules[gLabOfferChanges] : nullptr;
  const String chip = gLabOfferCount > 1 ? String(gLabOfferAt + 1) + " OF " + String(gLabOfferCount) : String(rule.conds > 1 ? (rule.any ? "ANY" : "ALL") : "");
  labHead(was ? "Claude changed this" : "Claude wrote this", launcher_icons::kFlow, kLabLime, chip, kLabSoft);
  int y = 20;
  gCanvas.setFont(&fonts::Font2);
  for (uint8_t i = 0; i < rule.conds; ++i, y += 15) {
    const bool same = was && i < was->conds && labSameCond(rule.cond[i], was->cond[i]);
    labTextAt(&fonts::Font2, 6, y, fitCurrentFontToWidth(String(!i ? "If " : rule.any ? "or " : "and ") + labCondText(rule.cond[i]), 228), same ? kLabMuted : kLabCyan);
  }
  for (uint8_t i = 0; i < rule.actions; ++i, y += 15) {
    const bool same = was && i < was->actions && labSameAction(rule.action[i], was->action[i]) &&
                      (rule.action[i].out != Out::Say || !strcmp(rule.say, was->say));
    labTextAt(&fonts::Font2, 6, y, fitCurrentFontToWidth(labActionText(rule.action[i], rule.say), 228), same ? kLabMuted : kLabLime);
  }
  labTextAt(&fonts::Font0, 6, 117, String("Name: ") + rule.name, kLabMuted);
  labKeys(gLabOfferCount > 1 ? String(kLabArrowLeftRight) + " next  ENTER save  E edit  ESC drop" : String("ENTER save  E edit  ESC drop it"));
}

// --- The steps -----------------------------------------------------------------------------------
lab::Cond& labWizardCond() { return gLabWizard.rule.cond[gLabWizard.cond]; }
lab::Action& labWizardAction() { return gLabWizard.rule.action[gLabWizard.action]; }
// A phrase has to be spoken into being, so Say is only offered to a rule that has one, and there's
// nothing to play until a sequence has been saved.
bool labWizardOffers(Out out) { return out == Out::Say ? gLabWizard.rule.say[0] != '\0' : out != Out::Sequence || gLabSeqCount > 0; }
int labWizardOutRows() {
  int rows = 0;
  for (uint8_t i = 0; i < lab::kOutCount; ++i) rows += labWizardOffers(static_cast<Out>(i)) ? 1 : 0;
  return rows;
}
Out labWizardOutAt(int row) {
  for (uint8_t i = 0; i < lab::kOutCount; ++i) {
    if (labWizardOffers(static_cast<Out>(i)) && row-- == 0) return static_cast<Out>(i);
  }
  return Out::Servo;
}
int labWizardOutRow(Out out) {
  int row = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(out); ++i) row += labWizardOffers(static_cast<Out>(i)) ? 1 : 0;
  return row < labWizardOutRows() ? row : 0;
}
// A rule's name has no value to follow, so the follow list skips it, wherever it sits in the list.
Sig labWizardSourceAt(int row) {
  const int spoken = static_cast<int>(Sig::Spoken);
  return static_cast<Sig>(row < spoken ? row : row + 1);
}
int labWizardSourceRow(Sig sig) {
  const int i = static_cast<int>(sig), spoken = static_cast<int>(Sig::Spoken);
  return i < spoken ? i : i > spoken ? i - 1 : 0;
}
// Which sensor a row of the What or Follow step stands for.
Sig labWizardSigAt(int row) { return gLabWizard.step == LabStep::Source ? labWizardSourceAt(row) : static_cast<Sig>(row); }
int labWizardRowCount() {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: return lab::kSigCount;
    case LabStep::Source: return lab::kSigCount - 1;  // All but a rule's name: see labWizardSourceAt.
    case LabStep::Op: return lab::info(w.rule.cond[w.cond].sig).onOff ? 2 : 3;
    case LabStep::Out: return labWizardOutRows();
    case LabStep::PickSeq: return gLabSeqCount;
    case LabStep::How: return lab::info(w.rule.action[w.action].out).onOff ? 2 : 3;
    case LabStep::MoreConds: case LabStep::MoreActions: case LabStep::Join: return 2;
    default: return 0;
  }
}
String labWizardRow(int i) {
  const LabWizard& w = gLabWizard;
  switch (w.step) {
    case LabStep::Sig: case LabStep::Source: return lab::info(labWizardSigAt(i)).name;
    case LabStep::Op: {
      if (w.rule.cond[w.cond].sig == Sig::Spoken) return i == 0 ? "is said" : "hasn't been said";
      if (lab::info(w.rule.cond[w.cond].sig).onOff) return i == 0 ? "turns on" : "turns off";
      static constexpr const char* kOps[3] = {"goes under", "goes over", "changes at all"};
      return kOps[i];
    }
    case LabStep::Out: return lab::info(labWizardOutAt(i)).name;
    case LabStep::PickSeq: return gLabSeqs[i].name;
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
    case LabStep::Out: w.row = labWizardOutRow(action.out); break;
    case LabStep::PickSeq: w.row = lab::clampTo<int>(action.value, 0, gLabSeqCount - 1); break;
    case LabStep::How:
      if (lab::info(action.out).onOff) w.row = action.value ? 0 : 1;
      else w.row = static_cast<int>(action.how);
      break;
    case LabStep::Source: w.row = labWizardSourceRow(action.source); break;
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
  if (from && index >= 0 && index < gLabBook.count) w.rule.on = gLabBook.rules[index].on;
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
  if (!lab::hasSay(w.rule)) w.rule.say[0] = '\0';
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
  labFetchSpeech(w.rule);
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
      action.out = labWizardOutAt(pick);
      const lab::OutInfo& info = lab::info(action.out);
      action.value = lab::clampTo<int16_t>(action.value, info.lo, info.hi);
      if (action.out == Out::Say) {
        action.how = lab::How::Set;
        action.value = 1;
        labWizardActionLine();
      } else if (action.out == Out::Sequence) {
        action.how = lab::How::Set;
        labWizardGo(LabStep::PickSeq);
      } else {
        labWizardGo(LabStep::How);
      }
      break;
    }
    case LabStep::PickSeq:
      action.value = static_cast<int16_t>(pick);
      labWizardActionLine();
      break;
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
      action.source = labWizardSourceAt(pick);
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
void labDrawOutValue(Out out, int value) {
  auto& d = gCanvas;
  const lab::OutInfo& info = lab::info(out);
  labTextAt(&fonts::Font2, 4, 20, String("Set ") + info.name + " to", kLabCyan);
  labTextCenter(&fonts::Font6, 120, 36, String(value), TFT_WHITE);
  if (out == Out::StripColor || out == Out::Led) d.fillRoundRect(8, 90, 224, 16, 5, labHueColor(value));
  else labBar(8, 90, 224, 9, lab::mapRange(value, info.lo, info.hi, 0, 100), kLabLime);
  labKeys(String(kLabArrowLeftRight) + " change  ENTER use it  ESC back");
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
    labTextCenter(&fonts::Font6, 120, 36, String(labShownNumber(cond.sig, cond.value)), TFT_WHITE);
    const int position = lab::mapRange(cond.value, info.lo, info.hi, 0, 100);
    labBar(8, 87, 224, 9, position, kLabCyan);
    d.fillRect(8 + 224 * position / 100 - 1, 84, 3, 15, TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 104, labValueText(cond.sig, cond.value, kLabDeg), TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 114, w.teaching ? "Set it up, then press ENTER" : "SPACE goes back to the live reading", kLabAmber);
    labKeys(String(kLabArrowLeftRight) + " nudge  ENTER use it  ESC back");
    return;
  }
  if (w.step == LabStep::OutValue) {
    labDrawOutValue(labWizardAction().out, labWizardAction().value);
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
    case LabStep::PickSeq: question = "Play which sequence?"; break;
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
      const Sig sig = labWizardSigAt(i);
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
    const bool said = (labNamedNow() >> index) & 1;
    const bool holds = lab::condHolds(cond, gLabSeen, now, said);
    const bool picked = practicing && i == gLabUi.practiceCond;
    labCurve(kLeft + kBoxW, y + h / 2, cx - kCircle, cy, holds ? kLabGreen : kLabDim, holds ? dot : -1);
    d.fillRoundRect(kLeft, y, kBoxW, h, 5, kLabPanel);
    if (picked) d.drawRoundRect(kLeft, y, kBoxW, h, 5, kLabAmber);
    else if (holds) d.drawRoundRect(kLeft, y, kBoxW, h, 5, kLabGreen);
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, kLeft + 5, y + 3, fitCurrentFontToWidth(lab::info(cond.sig).name, kBoxW - 6), picked ? kLabAmber : holds ? kLabGreen : kLabMuted);
    const lab::Reading& reading = gLabSeen.get(cond.sig);
    const String value = cond.sig == Sig::Spoken ? String(said ? "said" : "not yet") : reading.live ? labValueText(cond.sig, reading.value) : String("--");
    labTextAt(&fonts::Font2, kLeft + 5, y + (h > 30 ? 17 : 11), value, TFT_WHITE);
  }
  for (int i = 0; i < rule.actions; ++i) {
    const lab::Action& action = rule.action[i];
    const int h = labFlowBoxH(rule.actions), y = labFlowBoxY(i, rule.actions);
    labCurve(cx + kCircle, cy, kRight, y + h / 2, live, dot);
    d.fillRoundRect(kRight, y, kBoxW, h, 5, kLabPanel);
    gCanvas.setFont(&fonts::Font0);
    const bool sequence = action.out == Out::Sequence;
    // A sequence can outlast its rule, so this goes by whether it's running, not by the rule.
    const bool playing = sequence && gLabSeqRun.running() && gLabSeqRun.index() == action.value;
    const String title = playing ? String("Step ") + String(gLabSeqRun.shown() + 1) : String(lab::info(action.out).name);
    labTextAt(&fonts::Font0, kRight + 5, y + 3, fitCurrentFontToWidth(title, kBoxW - 6), playing ? kLabGreen : kLabMuted);
    int16_t value = 0;
    const bool known = firing && lab::actionValue(action, gLabSeen, value);
    if (action.out == Out::Say || sequence) {
      gCanvas.setFont(&fonts::Font0);
      labTextAt(&fonts::Font0, kRight + 5, y + (h > 30 ? 19 : 13), fitCurrentFontToWidth(sequence ? labSeqName(action.value) : rule.say, kBoxW - 8), known || playing ? TFT_WHITE : kLabSoft);
    } else if (known && (action.out == Out::StripColor || action.out == Out::Led)) d.fillRoundRect(kRight + 5, y + (h > 30 ? 16 : 12), kBoxW - 10, h > 30 ? 16 : 12, 3, labHueColor(value));
    else labTextAt(&fonts::Font2, kRight + 5, y + (h > 30 ? 17 : 11), known ? labOutText(action.out, value) : String("--"), TFT_WHITE);
  }
  d.fillCircle(cx, cy, kCircle, live);
  labTextCenter(&fonts::Font2, cx, cy - 7, rule.conds > 1 ? (rule.any ? "ANY" : "ALL") : "IF", firing ? TFT_BLACK : kLabSoft);
  if (static_cast<int32_t>(gLabUi.playNoteUntilMs - millis()) > 0) {
    gCanvas.setFont(&fonts::efontCN_12);
    labTextAt(&fonts::efontCN_12, 4, 113, fitCurrentFontToWidth(gLabUi.playNote, 160), kLabAmber);
  } else if (lab::listensForName(rule) && !practicing) {
    labTextAt(&fonts::Font0, 4, 116, "ALT and say its name", kLabAmber);
  } else {
    labTextAt(&fonts::Font0, 4, 116, state.fires == 1 ? String("Fired once") : String("Fired ") + String(int(state.fires)) + " times", kLabSoft);
  }
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
void renderLabFetching() {
  labHead("Rules that talk", launcher_icons::kMic, kLabLime, "SAVING", kLabAmber);
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 26, "Making its voice", TFT_WHITE);
  labTextAt(&fonts::Font0, 6, 52, "It's kept on the card, so it can", kLabSoft);
  labTextAt(&fonts::Font0, 6, 63, "play with no network.", kLabSoft);
}
void renderLabUi() {
  if (gLabFetchingSpeech) {
    renderLabFetching();
    return;
  }
  // A recording can start from a list, so what it's doing shows over whatever is underneath.
  const bool voiceBusy = voiceToolStarting() || voiceToolListening() || voiceToolWorking();
  switch (voiceBusy ? LabView::Say : gLabUi.view) {
    case LabView::Devices: renderLabDevices(); break;
    case LabView::Device: renderLabDevice(); break;
    case LabView::PickPort: renderLabPickPort(); break;
    case LabView::Wiring: renderLabWiring(); break;
    case LabView::Rules: renderLabRules(); break;
    case LabView::Wizard: renderLabWizard(); break;
    case LabView::Recipes: renderLabRecipes(); break;
    case LabView::Say: renderLabSay(); break;
    case LabView::Offer: renderLabOffer(); break;
    case LabView::Dash: renderLabDash(); break;
    case LabView::Seqs: renderLabSeqs(); break;
    case LabView::SeqEdit: renderLabSeqEdit(); break;
    case LabView::SeqAsk: renderLabSeqAsk(); break;
    case LabView::SeqPlay: renderLabSeqPlay(); break;
    case LabView::SeqOffer: renderLabSeqOffer(); break;
    default: renderLabFlow(gLabUi.playRule); break;
  }
  renderLauncherOverlay();
  renderDebugOverlay();
}

// --- Keys ----------------------------------------------------------------------------------------
void labOpenDevice(Dev dev) {
  gLabUi.view = LabView::Device;
  gLabUi.page = 0;
  gLabPartUsed[static_cast<uint8_t>(lab::partFor(dev))] = true;
  gLabUi.sweeping = false;
  gLabUi.stickLearn = 0;
  gLabUi.ledOn = false;
  gLabUi.relays[0] = gLabUi.relays[1] = false;
  Sig sig = Sig::Distance;
  if (dev == Dev::Env) sig = Sig::Humidity;
  else if (dev == Dev::Light) sig = Sig::Light;
  else if (dev == Dev::Motion) sig = Sig::Motion;
  else if (dev == Dev::Tilt) sig = Sig::Tilt;
  else if (dev == Dev::Knob) sig = Sig::Knob;
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
  gLabUi.stickLearn = 0;
  gLabUi.view = LabView::Devices;
  labRescanBus();
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
  if (gLabUi.stickLearn) return;  // Only the stick answers, and Esc stops it.
  if (labDevPages(dev) > 1 && (lower == 'n' || ((left || right) && labArrowsTurnPages(dev)))) {
    labTurnPage(left ? -1 : 1);
    return;
  }
  if (gLabUi.page && labPageKey(dev, key, lower, left, right, up, down, enter)) return;
  if (lower == 'p' && labOnHub(dev)) {
    gLabUi.portRow = max(labPortOf(dev), 0);
    gLabUi.view = LabView::PickPort;
    return;
  }
  if (lower == 'r') {
    if (dev == Dev::Knob) gLabKnobRange = gLabOnWires ? lab::AnalogRange::fixedAt(lab::kDirectKnobTop) : lab::AnalogRange{};
    else if (dev == Dev::Light) gLabLightRange = gLabOnWires ? lab::AnalogRange::fixedAt(lab::kDirectLightTop) : lab::AnalogRange{};
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
        if (labFound(Unit::Hub)) gLabHub.showMode(gLabBook.showMode);
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
    case Dev::Joystick:
      if (lower == 'd' && !gLabUi.page) {
        gLabUi.stickLearn = 1;
        gLabUi.stickPush = -1;
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
      else if (lower == 's' && !gLabUi.page) {
        // Where the Cardputer is now becomes the spot, for From spot and the Near the spot recipe.
        gLabUi.spotNote = labSaveSpot() ? "Spot saved" : gLabGps.fix.valid ? "Can't save" : "No fix yet";
        gLabUi.spotNoteUntilMs = millis() + 2000;
      }
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
      // Up and down, because left and right turn the page.
      if (up || down) gLabUi.toneHz = lab::clampTo(gLabUi.toneHz + (up ? kLabToneStep : -kLabToneStep), 200, 2000);
      if (enter || up || down) {
        gLabUi.beepUntilMs = millis() + kLabBeepMs;
        labDriveSet(Out::Tone, gLabUi.toneHz);
      }
      return;
    default:
      return;
  }
}
// Whichever way a part is connected should just work, even if that changes while its screen is
// open. With no I/O hub and an empty bus, the part on the screen must be on the Grove wires.
void labFollowPart() {
  const Dev dev = static_cast<Dev>(gLabUi.devRow);
  const lab::Part part = lab::partFor(dev);
  const bool emptyBus = gLabOnWires || (gLabQuietProbes >= lab::kUnitCount && !labAnyUnit());
  if (part != lab::Part::None && emptyBus && !labFound(Unit::Hub)) labUseOnWires(part);
}
// The servo's sweep and the speaker's beep run while the screen is up, with no key held.
void serviceLabTests() {
  if (gLabUi.view != LabView::Device) return;
  labFollowPart();
  const uint32_t now = millis();
  if (gLabUi.beepUntilMs && now >= gLabUi.beepUntilMs) {
    gLabUi.beepUntilMs = 0;
    labDriveOff(Out::Tone);
  }
  serviceLabPage(now);
  labStepStickLearn(now);
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
  for (uint8_t i = 0; i < gLabBook.count; ++i) labFetchSpeech(gLabBook.rules[i]);
  gLabUi.playRule = 0;
  gLabUi.practicing = false;
  gLabUi.practiceCond = 0;
  labPlay();
  gLabUi.view = LabView::Play;
}
void labOpenSay(LabVoice voice) {
  gLabUi.sayFailed = false;
  gLabUi.voice = voice;
  gLabUi.sayBack = LabView::Rules;
  gLabUi.view = LabView::Say;
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
    labOpenSay(LabVoice::NewRule);
  } else if (enter && ui.ruleRow == gLabBook.count + 2) {
    ui.view = LabView::Recipes;
  } else if (enter) {
    labOpenSay(LabVoice::Ask);
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
// Typing over a suggestion replaces it, and Enter alone keeps it.
void labTypeName(char* name, bool& touched, char key, bool del) {
  const size_t length = strlen(name);
  if (del) {
    touched = true;
    if (length) name[length - 1] = '\0';
  } else if (isalnum(static_cast<unsigned char>(key)) || key == ' ') {
    touched = true;
    if (length < lab::kNameChars) {
      name[length] = key;
      name[length + 1] = '\0';
    }
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
      if (enter) labWizardPick();
      else labTypeName(w.rule.name, w.nameTouched, key, del);
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
// Moves what's on the screen right now. It needs that screen open, so a word can't reach anything
// else, and a chat message can't do this at all.
bool labVoiceSet(const char* output, int value) {
  LabUi& ui = gLabUi;
  Out out = Out::Servo;
  if (!gLabVoiceSetting || !labMode(gUiMode) || ui.view != LabView::Device || !lab::outFromKey(output, out)) return false;
  const Dev dev = static_cast<Dev>(ui.devRow);
  const lab::OutInfo& info = lab::info(out);
  value = lab::clampTo<int>(value, info.lo, info.hi);
  if (out == Out::Servo && dev == Dev::Servo) {
    ui.sweeping = false;
    labServoGoTo(value);
  } else if ((out == Out::StripColor || out == Out::StripBright) && dev == Dev::Strip) {
    (out == Out::StripColor ? ui.stripHue : ui.stripBright) = value;
    labStripPick(gLabStripTest);
  } else if (out == Out::Led && dev == Dev::Led) {
    ui.ledHue = value;
    ui.ledOn = true;
    labDriveSet(Out::Led, value);
  } else if (out == Out::Tone && dev == Dev::Speaker) {
    ui.toneHz = value;
    ui.beepUntilMs = millis() + kLabBeepMs;
    labDriveSet(Out::Tone, value);
  } else {
    return false;
  }
  return true;
}
// The screens where Alt talks to Claude. The steps have keys of their own, and an offer waits for Enter.
bool labVoiceScreen() {
  if (!labMode(gUiMode)) return false;
  const LabView view = gLabUi.view;
  return view == LabView::Devices || view == LabView::Device || view == LabView::Dash || view == LabView::Wiring ||
         view == LabView::Rules || view == LabView::Say || view == LabView::Seqs;
}
bool labSayOnScreen() { return labVoiceScreen() && labVoiceNow() == LabVoice::NewRule; }
bool labChangeOnScreen() { return labVoiceScreen() && labVoiceNow() == LabVoice::Change; }
bool labAskOnScreen() { return labVoiceScreen() && labVoiceNow() == LabVoice::Ask; }
bool labCommandOnScreen() { return labMode(gUiMode) && gLabUi.view == LabView::Play; }
// Several rules at once are choices. They stay in order, and the first is shown.
bool labTakeOffers(const String& json) {
  gLabOfferCount = 0;
  gLabOfferBatch = true;
  const bool offered = executeDeviceActionsJson(json);
  gLabOfferBatch = false;
  return offered;
}
// A rule changes the screen by itself. Anything else is Claude saying what it needs.
void labRuleAnswered(const VoiceToolAnswer& answer, LabVoice voice) {
  LabUi& ui = gLabUi;
  gLabOfferNext = voice == LabVoice::Change ? ui.ruleRow : -1;
  const bool offered = answer.ok && labTakeOffers(answer.actionsJson);
  gLabOfferNext = -1;
  if (offered) return;
  ui.voice = voice;
  ui.sayBack = LabView::Rules;
  ui.view = LabView::Say;
  ui.sayFailed = true;
  ui.sayNote = answer.ok ? answer.reply : answer.problem;
  ui.sayHeard = answer.heard;
  labRedraw();
}
void labSayAnswered(const VoiceToolAnswer& answer) { labRuleAnswered(answer, LabVoice::NewRule); }
void labChangeAnswered(const VoiceToolAnswer& answer) { labRuleAnswered(answer, LabVoice::Change); }
// An answer shows and speaks, and Esc goes back to wherever the question came from. Asking what to
// build brings rules instead, which go straight to being looked at.
void labAskAnswered(const VoiceToolAnswer& answer) {
  gLabVoiceSetting = true;
  gActionsFollowSpeech = true;  // The answer is spoken below, so a check's report is too.
  const bool acted = answer.ok && labTakeOffers(answer.actionsJson);
  gActionsFollowSpeech = false;
  gLabVoiceSetting = false;
  // A check still has its report to come, and until then its words are all there is to show.
  if (acted && (!labOnceBusy() || answer.reply.isEmpty())) return;
  LabUi& ui = gLabUi;
  if (ui.view != LabView::Say) ui.sayBack = ui.view;
  ui.voice = LabVoice::Ask;
  ui.view = LabView::Say;
  ui.sayFailed = true;
  ui.sayNote = answer.ok ? answer.reply : answer.problem;
  ui.sayHeard = answer.heard;
  if (answer.ok && !answer.reply.isEmpty()) gQueuedVoiceReplySpeech = cleanReplyForDevice(answer.reply);
  labRedraw();
}
// What a check found: in the Lab an answer, and anywhere else a reply.
void labOnceSay(const char* text, bool speak) {
  logf("[LAB] check: %s", text);
  if (!labMode(gUiMode)) {
    postDeviceReply(text, speak);
    return;
  }
  LabUi& ui = gLabUi;
  if (ui.view != LabView::Say) ui.sayBack = ui.view;
  ui.voice = LabVoice::Ask;
  ui.view = LabView::Say;
  ui.sayFailed = true;
  ui.sayNote = text;
  if (speak) gQueuedVoiceReplySpeech = cleanReplyForDevice(ui.sayNote);
  labRedraw();
}
// A check in the Lab leaves a part's test screen showing what it set, in the pattern that shows it.
void labOnceMirror(const lab::SeqStep& step) {
  LabUi& ui = gLabUi;
  const bool set = step.kind == lab::SeqKind::Set;
  if (lab::isStrip(step.out)) gLabStripTest = LabStripTest::Solid;
  if (step.out == Out::StripColor && set) ui.stripHue = step.value;
  if (step.out == Out::StripBright && set) ui.stripBright = step.value;
  if (step.out == Out::Led) {
    ui.ledOn = set;
    if (set) ui.ledHue = step.value;
  }
  if (step.out == Out::Servo) {
    ui.sweeping = false;
    ui.servoAngle = step.value;
  }
}
// Only the words matter here, and rules are waiting, so this skips Claude. Saying a name again
// turns it back off.
void labCommandAnswered(const VoiceToolAnswer& answer) {
  LabUi& ui = gLabUi;
  ui.playNoteUntilMs = millis() + 3000;
  labRedraw();
  if (!answer.ok) {
    ui.playNote = answer.problem;
    return;
  }
  const int index = lab::spokenRule(answer.heard.c_str(), gLabBook.rules, gLabBook.count);
  if (index < 0) {
    ui.playNote = "Heard: " + answer.heard;
    return;
  }
  gLabNamed ^= static_cast<uint8_t>(1U << index);
  ui.playRule = index;
  ui.playNote = String(gLabBook.rules[index].name) + ((gLabNamed >> index) & 1 ? " on" : " off");
}
// What Claude can't see from the words: what's plugged in, what it reads, and the rules so far.
String labDescribe(bool withDiary) {
  constexpr size_t kChars = 4000;  // A full set of rules and a day of the diary fit in a header.
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[kChars]);
  if (!buffer) return String();
  lab::Text text(buffer.get(), kChars);
  text.add("parts answering:");
  for (uint8_t i = 0; i < lab::kUnitCount; ++i) if (gLabFound[i]) text.add(" ").add(lab::kUnitNames[i]).add(",");
  if (gLabOnWires) {
    text.add(" on the Grove port: ").add(lab::kPartNames[static_cast<uint8_t>(gLabBook.direct.white)]);
    text.add(" on white, ").add(lab::kPartNames[static_cast<uint8_t>(gLabBook.direct.yellow)]).add(" on yellow");
  }
  if (labFound(Unit::Hub)) {
    text.add("\nI/O hub ports, as the owner set them:");
    for (uint8_t i = 0; i < pbhub::kPorts; ++i) text.add(" ").add(i).add("=").add(lab::kPartNames[static_cast<uint8_t>(gLabBook.wiring.port[i])]);
  }
  text.add("\nreadings:");
  for (uint8_t i = 0; i < lab::kSigCount; ++i) {
    const lab::Reading& reading = gLabSeen.get(static_cast<Sig>(i));
    if (reading.live) text.add(" ").add(lab::kSigs[i].key).add("=").add(reading.value);
  }
  text.add("\nowner reads ").add(displayUnits() == units::System::US ? "US units (F, inches, feet, mph)" : "metric units");
  text.add(gLabPlaying ? "\nrules are playing" : "\nrules are not playing");
  if (gLabUi.view == LabView::Device) {
    const LabUi& ui = gLabUi;
    text.add("\nlooking at the test screen for: ").add(lab::kDevNames[ui.devRow]);
    if (labTakesVoiceSet(static_cast<Dev>(ui.devRow))) {
      text.add("\ntest values: servo=").add(ui.servoAngle).add(" strip_color=").add(ui.stripHue).add(" strip_bright=").add(ui.stripBright);
      text.add(" led=").add(ui.ledHue).add(" speaker=").add(ui.toneHz);
    }
  }
  if (!withDiary && gLabSeqCount) text.add("\nsaved sequences:");
  for (uint8_t i = 0; !withDiary && i < gLabSeqCount; ++i) text.add(i ? ", " : " ").add(gLabSeqs[i].name);
  for (uint8_t i = 0; i < gLabBook.count; ++i) {
    const lab::Rule& rule = gLabBook.rules[i];
    text.add("\nrule ").add(i + 1).add(rule.on ? "" : " (switched off)");
    text.add(gLabPlaying && gLabStates[i].active ? " (firing): " : ": ");
    lab::ruleJson(rule, text, gLabSeqs, gLabSeqCount);
  }
  if (labChangeOnScreen()) text.add("\nchanging rule ").add(gLabUi.ruleRow + 1);
  if (withDiary) {
    // Ask the Lab can change a sequence, so it gets the steps. They come after the rules, so a long
    // list can only cost the diary's oldest hours.
    if (gLabSeqCount) text.add("\nsaved sequences:");
    for (uint8_t i = 0; i < gLabSeqCount; ++i) lab::sequenceLine(gLabSeqs[i], text.add("\n"));
    // "Make it slower" means the one under the bar.
    if (gLabUi.view == LabView::Seqs && gLabUi.seqRow < gLabSeqCount) text.add("\npicked in the list: ").add(gLabSeqs[gLabUi.seqRow].name);
    labDiaryTail(text);
  }
  return String(buffer.get());
}
String labVoiceContext() { return labDescribe(false); }
String labAskContext() { return labDescribe(true); }
// What the chat is told, from any screen. It's only what's saved, because the sensors stop when the
// Lab closes, and it's put together for each message so it can't go stale. No card at boot means
// nothing has been read yet, so it says nothing rather than something wrong.
String labChatContext() {
  if (!gLabBookRead) return String();
  constexpr size_t kUnitsLine = 48;
  const size_t chars = lab::chatChars(gLabBook.count, gLabSeqs, gLabSeqCount) + kUnitsLine + (gLabOnceText[0] ? lab::kOnceTextChars + 16 : 0);
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[chars]);
  if (!buffer) return String();
  lab::Text text(buffer.get(), chars);
  lab::chatState(gLabBook.rules, gLabBook.count, gLabSeqs, gLabSeqCount, text);
  text.add("owner reads ").add(displayUnits() == units::System::US ? "US units (F, inches, feet, mph)" : "metric units").add("\n");
  if (gLabOnceText[0]) text.add("last check: ").add(gLabOnceText).add("\n");  // So a follow-up has the number.
  return String(buffer.get());
}
// An offer can land while someone's typing their next message, and Enter there isn't a yes. It takes
// no keys until the keyboard has rested, however long they keep typing.
void labOfferLanded() {
  gLabOfferSettled = false;
  gLabOfferKeyMs = millis();
}
bool labOfferSettling() {
  if (gLabOfferSettled || (gLabUi.view != LabView::Offer && gLabUi.view != LabView::SeqOffer)) return false;
  const uint32_t now = millis();
  gLabOfferSettled = now - gLabOfferKeyMs >= kLabOfferQuietMs;
  gLabOfferKeyMs = now;
  return !gLabOfferSettled;
}
// A rule from Claude waits here until someone presses Enter, because rules can switch mains.
void labOfferRule(const lab::Rule& rule) {
  if (!gLabOfferBatch) gLabOfferCount = 0;
  if (gLabOfferCount < kLabOffers) gLabOffers[gLabOfferCount++] = rule;
  gLabOfferAt = 0;
  gLabOffer = gLabOffers[0];
  gLabOfferChanges = gLabOfferNext;
  if (!labMode(gUiMode)) setUiMode(UiMode::LabRules);
  labStopPlaying();
  gLabUi.view = LabView::Offer;
  labOfferLanded();
  labRedraw();
}
// The chat says which rule by name, because the numbers can move. A name that's gone is a refusal,
// and not a new rule, which would leave the old one running beside it.
bool labOfferChange(const lab::Rule& rule, const char* replaces) {
  const int changes = *replaces ? lab::findByName(gLabBook.rules, gLabBook.count, replaces) : gLabOfferNext;
  if (*replaces && changes < 0) return false;
  const int asked = gLabOfferNext;
  gLabOfferNext = changes;
  labOfferRule(rule);
  gLabOfferNext = asked;
  return true;
}
bool handleLabKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  // The screen underneath is covered, so nothing should move it where it can't be seen.
  if (voiceToolStarting() || voiceToolWorking()) return true;
  if (labOfferSettling()) return true;
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
      } else if (lower == 'd') {
        ui.dashPage = 0;
        ui.view = LabView::Dash;
      }
      break;
    case LabView::Dash:
      if (left || right) ui.dashPage = (ui.dashPage + (right ? 1 : labDashPages() - 1)) % labDashPages();
      break;
    case LabView::Device:
      if (lower == 'w' && static_cast<Dev>(ui.devRow) == Dev::Hub) ui.view = LabView::Wiring;
      else labDeviceKey(key, lower, left, right, up, down, enter);
      break;
    case LabView::PickPort:
      if (up || down) {
        ui.portRow = (ui.portRow + (up ? kLabLastPort : 1)) % pbhub::kPorts;
      } else if (enter) {
        const Dev dev = static_cast<Dev>(ui.devRow);
        const lab::Part part = lab::partFor(dev), there = gLabBook.wiring.port[ui.portRow];
        labReleaseOutputs();
        gLabBook.wiring.moveTo(static_cast<uint8_t>(ui.portRow), part);
        // What was learned from the wrong port, even if only noise, shouldn't set a part's range.
        if (part == lab::Part::Knob || there == lab::Part::Knob) gLabKnobRange = lab::AnalogRange{};
        if (part == lab::Part::Light || there == lab::Part::Light) gLabLightRange = lab::AnalogRange{};
        gLabHubReady = false;
        labSaveBook();
        labOpenDevice(dev);
      }
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
      if (enter && gWifiReady && !gSubmitting && !gThinking) toggleVoiceByTap();
      break;
    case LabView::Offer:
      if ((left || right) && gLabOfferCount > 1) {
        gLabOfferAt = (gLabOfferAt + (right ? 1 : gLabOfferCount - 1)) % gLabOfferCount;
        gLabOffer = gLabOffers[gLabOfferAt];
      } else if (lower == 'e') {
        labWizardStart(gLabOfferChanges, &gLabOffer);
      } else if (enter && gLabOfferChanges >= 0 && gLabOfferChanges < gLabBook.count) {
        gLabOffer.on = gLabBook.rules[gLabOfferChanges].on;
        gLabBook.rules[gLabOfferChanges] = gLabOffer;
        ui.ruleRow = gLabOfferChanges;
        labSaveBook();
        labFetchSpeech(gLabOffer);
        ui.ruleTop = 0;
        labKeepRowShown(ui.ruleRow, ui.ruleTop, kLabListRows);
        ui.view = LabView::Rules;
      } else if (enter && gLabBook.count < lab::kMaxRules) {
        gLabBook.rules[gLabBook.count] = gLabOffer;
        ui.ruleRow = gLabBook.count;
        ++gLabBook.count;
        labSaveBook();
        labFetchSpeech(gLabOffer);
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
    case LabView::Seqs:
      labSeqsKey(lower, up, down, enter);
      break;
    case LabView::SeqEdit:
      labSeqEditKey(lower, left, right, up, down, enter);
      break;
    case LabView::SeqAsk:
      labSeqAskKey(key, left, right, up, down, enter, del);
      break;
    case LabView::SeqPlay:
      if (lower == 'p') labSeqPlayAgain();
      break;
    case LabView::SeqOffer:
      labSeqOfferKey(lower, up, down, enter);
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
    case LabView::Devices: case LabView::Rules: case LabView::Seqs: return false;
    case LabView::Device:
      if (ui.stickLearn) ui.stickLearn = 0;  // Stops learning, and the directions stay as they were.
      else labCloseDevice();
      break;
    case LabView::PickPort: ui.view = LabView::Device; break;
    case LabView::Wiring:
      // Nothing is driven on this screen. The parts may have moved, so the hub starts over.
      gLabHubReady = false;
      labSaveBook();
      ui.view = gUiMode == UiMode::LabDevices ? LabView::Devices : LabView::Rules;
      break;
    case LabView::Wizard: labWizardBack(); break;
    case LabView::Recipes: case LabView::Offer: ui.view = LabView::Rules; break;
    case LabView::Say: ui.view = ui.sayBack; break;
    case LabView::Dash: ui.view = LabView::Devices; break;
    case LabView::SeqEdit: labSeqLeaveEdit(); break;
    case LabView::SeqAsk: labSeqAskBack(); break;
    case LabView::SeqOffer: ui.view = LabView::Seqs; break;
    case LabView::SeqPlay:
      gLabSeqRun.stop();
      labReleaseOutputs();
      ui.view = ui.seqBack;
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
void labEnterView(UiMode mode) {
  gLabUi.view = mode == UiMode::LabSequences ? LabView::Seqs : mode == UiMode::LabRules ? LabView::Rules : LabView::Devices;
  gLabUi.stickLearn = 0;
}
bool labSeqOnScreen() { return gLabUi.view == LabView::SeqPlay; }
}  // namespace
