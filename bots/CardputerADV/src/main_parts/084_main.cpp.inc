// The Lab's extra device pages and the Dashboard. They read what part 081 reads, and anything they
// move goes through gLabDrive, so the strip cap and the relay guard still apply. Everything here
// fits in LabPlay, because the Lab has little memory to spare and these reset with each page.
namespace {
constexpr int kLabTrailPoints = 96;
constexpr uint32_t kLabPlayStepMs = 40;
constexpr int kLabArenaX = 4, kLabArenaY = 21, kLabArenaW = 232, kLabArenaH = 102;
constexpr int kLabDashTiles = 6;
constexpr int kLabGuardArmSeconds = 5;
constexpr int kLabPianoKeys = 8;
constexpr int kLabPianoHz[kLabPianoKeys] = {523, 587, 659, 698, 784, 880, 988, 1047};
constexpr const char* kLabPianoNames[kLabPianoKeys] = {"C", "D", "E", "F", "G", "A", "B", "C"};
constexpr uint16_t kLabGround = rgb565_local(0, 73, 0), kLabComfy = rgb565_local(0, 73, 85);

enum class LabGuard : uint8_t { Off, Arming, Armed, Tripped };
enum class LabMove : uint8_t { Sweep, Wave, Tick, Count };
constexpr const char* kLabMoveNames[static_cast<int>(LabMove::Count)] = {"Sweep", "Wave", "Tick"};

struct LabPlay {
  uint32_t stepMs = 0;
  // Parking.
  bool beeps = false;
  uint32_t beepMs = 0;
  // Draw.
  uint8_t trailX[kLabTrailPoints], trailY[kLabTrailPoints];
  int trailAt = 0, trailCount = 0, penX = kLabArenaW / 2, penY = kLabArenaH / 2;
  // Target.
  int targetX = kLabArenaW / 3, targetY = kLabArenaH / 3, hits = 0;
  uint32_t targetMs = 0, bestMs = 0;
  // Steady hand.
  int zone = 50, zoneStep = 1;
  uint32_t heldMs = 0, bestHeldMs = 0;
  // Guard.
  LabGuard guard = LabGuard::Off;
  uint32_t guardMs = 0;
  int trips = 0;
  // Roll.
  float ballX = kLabArenaW / 2.0f, ballY = kLabArenaH / 2.0f, speedX = 0.0f, speedY = 0.0f;
  int goals = 0;
  // Moves.
  LabMove move = LabMove::Sweep;
  bool moving = false, moveUp = true;
  int moveSpeed = 2;
  uint32_t tickMs = 0;
  // Piano.
  int note = -1;
} gLabPlay;

uint32_t gLabDice = 1;
int labRoll(int below) {
  gLabDice = gLabDice * 1103515245U + 12345U;
  return static_cast<int>((gLabDice >> 16) % static_cast<uint32_t>(below));
}
void labBeep(int hz, uint32_t ms) {
  gLabUi.beepUntilMs = millis() + ms;
  labDriveSet(Out::Tone, hz);
}
bool labArrowsTurnPages(Dev dev) { return dev != Dev::Servo && dev != Dev::Speaker; }

// --- Getting between pages -------------------------------------------------------------------
int labDevPages(Dev dev) {
  switch (dev) {
    case Dev::Ultrasonic: case Dev::Env: case Dev::Joystick: case Dev::Knob: case Dev::Light: case Dev::Motion: case Dev::Tilt:
      return 3;
    case Dev::Servo: case Dev::Speaker:
      return 2;
    default:
      return 1;
  }
}
void labNewTarget() {
  LabPlay& p = gLabPlay;
  p.targetX = 12 + labRoll(kLabArenaW - 24);
  p.targetY = 12 + labRoll(kLabArenaH - 24);
  p.targetMs = millis();
}
void labTurnPage(int delta) {
  LabUi& ui = gLabUi;
  const int pages = labDevPages(static_cast<Dev>(ui.devRow));
  ui.page = (ui.page + delta + pages) % pages;
  ui.beepUntilMs = 0;
  labDriveOff(Out::Tone);
  gLabDice ^= millis() | 1U;
  gLabPlay = LabPlay{};
  labNewTarget();
}
void labPageDots() {
  const int pages = labDevPages(static_cast<Dev>(gLabUi.devRow));
  if (pages < 2) return;
  auto& d = gCanvas;
  const int width = pages * 8 + 4, x = 120 - width / 2;
  d.fillRect(x, 17, width, 3, TFT_BLACK);
  for (int i = 0; i < pages; ++i) d.fillCircle(x + 6 + i * 8, 18, i == gLabUi.page ? 2 : 1, i == gLabUi.page ? TFT_WHITE : kLabMuted);
}
String labPageKeys(const char* more) { return String(kLabArrowLeftRight) + " page  " + more; }

// --- A graph that fills the page -------------------------------------------------------------
// It scales to what's been seen, so a small change in a room still shows.
void renderLabGraphPage(Dev dev) {
  auto& d = gCanvas;
  labDrawHeadFor(dev, kLabLime, "LAST 30 SEC", kLabSoft);
  const Sig sig = gLabHistorySig;
  const lab::SigInfo& info = lab::info(sig);
  int low = 0, high = 0, count = 0;
  int32_t sum = 0;
  for (int i = 0; i < kLabHistory; ++i) {
    if (!gLabHistoryLive[i]) continue;
    const int value = gLabHistoryValues[i];
    low = count ? min(low, value) : value;
    high = count ? max(high, value) : value;
    sum += value;
    ++count;
  }
  const int newest = (gLabHistoryAt + kLabHistory - 1) % kLabHistory;
  labStat(6, 21, info.name, gLabHistoryLive[newest] ? labValueText(sig, gLabHistoryValues[newest]) : String("--"), kLabCyan);
  labStat(92, 21, "LOW", count ? labValueText(sig, low) : String("--"));
  labStat(142, 21, "AVG", count ? labValueText(sig, static_cast<int>(sum / count)) : String("--"));
  labStat(192, 21, "HIGH", count ? labValueText(sig, high) : String("--"));
  constexpr int kLeft = 28, kTop = 47, kWidth = 208, kHeight = 75;
  d.fillRect(kLeft, kTop, kWidth, kHeight, kLabPanel2);
  d.drawFastHLine(kLeft, kTop + kHeight / 2, kWidth, kLabPanel);
  d.drawFastVLine(kLeft + kWidth / 2, kTop, kHeight, kLabPanel);
  if (!count) {
    labTextCenter(&fonts::Font0, kLeft + kWidth / 2, kTop + 33, "Waiting for a reading", kLabMuted);
  } else {
    const int least = max<int>(info.step * 4, 2);
    int floorValue = info.onOff ? 0 : low, ceilValue = info.onOff ? 1 : high;
    if (ceilValue - floorValue < least) {
      floorValue = max<int>(floorValue - (least - (ceilValue - floorValue)) / 2, info.lo);
      ceilValue = floorValue + least;
    }
    for (int i = 0; i < kLabHistory; ++i) {
      const int at = (gLabHistoryAt + i) % kLabHistory;
      if (!gLabHistoryLive[at]) continue;
      const int height = lab::mapRange(gLabHistoryValues[at], floorValue, ceilValue, 2, kHeight - 2);
      const int x = kLeft + i * kWidth / kLabHistory, y = kTop + kHeight - height;
      d.fillRect(x, y, 2, height, kLabGraph);
      d.fillRect(x, y, 2, 2, kLabCyan);
    }
    labTextRight(&fonts::Font0, 26, kTop, info.onOff ? String("on") : String(ceilValue), kLabSoft);
    labTextRight(&fonts::Font0, 26, kTop + kHeight - 8, info.onOff ? String("off") : String(floorValue), kLabSoft);
  }
  labKeys(labPageKeys(dev == Dev::Env ? "T other reading  R reset" : "R reset  ESC back"));
}

// --- Ultrasonic: a parking sensor -------------------------------------------------------------
void renderLabParking() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Ultrasonic, kLabLime, gLabPlay.beeps ? "PARKING SOUND ON" : "PARKING", gLabPlay.beeps ? kLabAmber : kLabSoft);
  const lab::Reading& reading = gLabInputs.get(Sig::Distance);
  const int cm = reading.live ? reading.value : 999;
  const uint16_t tone = cm < lab::kParkingStopCm ? kLabRed : cm < lab::kParkingCloseCm ? kLabAmber : kLabGreen;
  labTextAt(&fonts::Font4, 6, 23, reading.live ? String(cm) + " cm" : String("--"), TFT_WHITE);
  labTextRight(&fonts::FreeSansBold9pt7b, 234, 27, reading.live ? lab::parkingWord(cm) : "Nothing in range", tone);
  constexpr int kBars = 12;
  const int lit = reading.live ? lab::mapRange(cm, 100, 5, 0, kBars) : 0;
  for (int i = 0; i < kBars; ++i) {
    const int height = 12 + i * 5, x = 8 + i * 19;
    const uint16_t color = i < 6 ? kLabGreen : i < 9 ? kLabAmber : kLabRed;
    d.fillRoundRect(x, 118 - height, 15, height, 2, i < lit ? color : kLabPanel);
  }
  labKeys(labPageKeys(gLabPlay.beeps ? "B quiet  ESC back" : "B sound  ESC back"));
}
void labStepParking(uint32_t now) {
  LabPlay& p = gLabPlay;
  const lab::Reading& reading = gLabInputs.get(Sig::Distance);
  if (!p.beeps || !reading.live || reading.value >= 100) return;
  const uint16_t gap = lab::parkingGapMs(reading.value);
  if (!gap) {
    labBeep(1200, 200);
  } else if (now - p.beepMs >= gap) {
    p.beepMs = now;
    labBeep(1200, 50);
  }
}

// --- ENV III: how the room feels --------------------------------------------------------------
void renderLabComfort() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Env, kLabLime, "COMFORT", kLabSoft);
  constexpr int kLeft = 6, kTop = 22, kWidth = 122, kHeight = 100;
  constexpr int kColdest = 10, kHottest = 35;
  d.fillRoundRect(kLeft, kTop, kWidth, kHeight, 4, kLabPanel2);
  const int zoneX = lab::mapRange(lab::kComfyLowC, kColdest, kHottest, kLeft, kLeft + kWidth);
  const int zoneRight = lab::mapRange(lab::kComfyHighC, kColdest, kHottest, kLeft, kLeft + kWidth);
  const int zoneTop = lab::mapRange(lab::kComfyHighHumidity, 0, 100, kTop + kHeight, kTop);
  const int zoneBottom = lab::mapRange(lab::kComfyLowHumidity, 0, 100, kTop + kHeight, kTop);
  d.fillRect(zoneX, zoneTop, zoneRight - zoneX, zoneBottom - zoneTop, kLabComfy);
  d.drawRect(zoneX, zoneTop, zoneRight - zoneX, zoneBottom - zoneTop, kLabGreen);
  labTextAt(&fonts::Font0, kLeft + 3, kTop + 3, "damp", kLabMuted);
  labTextAt(&fonts::Font0, kLeft + 3, kTop + kHeight - 10, "dry", kLabMuted);
  labTextRight(&fonts::Font0, kLeft + kWidth - 3, kTop + kHeight - 10, "hot", kLabMuted);
  const lab::Reading& temperature = gLabInputs.get(Sig::Temperature);
  const lab::Reading& humidity = gLabInputs.get(Sig::Humidity);
  if (!temperature.live || !humidity.live) {
    labTextAt(&fonts::FreeSansBold9pt7b, 136, 26, "Waiting", kLabMuted);
    labKeys(labPageKeys("ESC back"));
    return;
  }
  const int x = lab::mapRange(temperature.value, kColdest, kHottest, kLeft + 5, kLeft + kWidth - 5);
  const int y = lab::mapRange(humidity.value, 0, 100, kTop + kHeight - 5, kTop + 5);
  d.drawFastHLine(kLeft, y, kWidth, kLabDim);
  d.drawFastVLine(x, kTop, kHeight, kLabDim);
  d.fillCircle(x, y, 4, kLabCyan);
  const char* word = lab::comfortWord(temperature.value, humidity.value);
  labTextAt(&fonts::Font2, 136, 23, word, strcmp(word, "Comfortable") == 0 ? kLabGreen : kLabAmber);
  labStat(136, 46, "TEMP", labValueText(Sig::Temperature, temperature.value));
  labStat(190, 46, "HUMID", labValueText(Sig::Humidity, humidity.value));
  labStat(136, 78, "DEW POINT", String("about ") + labValueText(Sig::Temperature, lab::dewPoint(temperature.value, humidity.value)));
  labKeys(labPageKeys("Green is comfortable"));
}

// --- Joystick: draw, and hit the target -------------------------------------------------------
void renderLabDraw() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  labDrawHeadFor(Dev::Joystick, kLabLime, "DRAW", kLabSoft);
  d.fillRoundRect(kLabArenaX - 2, kLabArenaY - 1, kLabArenaW + 4, kLabArenaH + 2, 4, kLabPanel2);
  for (int i = 1; i < p.trailCount; ++i) {
    const int from = (p.trailAt + kLabTrailPoints - p.trailCount + i - 1) % kLabTrailPoints;
    const int to = (from + 1) % kLabTrailPoints;
    d.drawLine(kLabArenaX + p.trailX[from], kLabArenaY + p.trailY[from], kLabArenaX + p.trailX[to], kLabArenaY + p.trailY[to], labHueColor(i * 7));
  }
  d.fillCircle(kLabArenaX + p.penX, kLabArenaY + p.penY, 3, TFT_WHITE);
  labKeys(labPageKeys("Press the stick to clear"));
}
void labStepDraw() {
  LabPlay& p = gLabPlay;
  if (gLabInputs.get(Sig::StickPress).value) {
    p.trailCount = p.trailAt = 0;
    return;
  }
  const int stickX = gLabInputs.get(Sig::StickX).value, stickY = gLabInputs.get(Sig::StickY).value;
  const int moveX = abs(stickX) < 10 ? 0 : stickX / 16, moveY = abs(stickY) < 10 ? 0 : -stickY / 16;
  if (!moveX && !moveY) return;
  p.penX = lab::clampTo(p.penX + moveX, 0, kLabArenaW - 1);
  p.penY = lab::clampTo(p.penY + moveY, 0, kLabArenaH - 1);
  p.trailX[p.trailAt] = static_cast<uint8_t>(p.penX);
  p.trailY[p.trailAt] = static_cast<uint8_t>(p.penY);
  p.trailAt = (p.trailAt + 1) % kLabTrailPoints;
  if (p.trailCount < kLabTrailPoints) ++p.trailCount;
}
void labStickSpot(int& x, int& y) {
  x = lab::mapRange(gLabInputs.get(Sig::StickX).value, -100, 100, 0, kLabArenaW - 1);
  y = lab::mapRange(gLabInputs.get(Sig::StickY).value, -100, 100, kLabArenaH - 1, 0);
}
void renderLabTarget() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  labDrawHeadFor(Dev::Joystick, kLabLime, String(p.hits) + (p.hits == 1 ? " HIT" : " HITS"), kLabGreen);
  d.fillRoundRect(kLabArenaX - 2, kLabArenaY - 1, kLabArenaW + 4, kLabArenaH + 2, 4, kLabPanel2);
  d.fillCircle(kLabArenaX + p.targetX, kLabArenaY + p.targetY, 10, kLabRed);
  d.fillCircle(kLabArenaX + p.targetX, kLabArenaY + p.targetY, 6, TFT_WHITE);
  d.fillCircle(kLabArenaX + p.targetX, kLabArenaY + p.targetY, 3, kLabRed);
  int x = 0, y = 0;
  labStickSpot(x, y);
  d.drawCircle(kLabArenaX + x, kLabArenaY + y, 5, kLabLime);
  d.drawFastHLine(kLabArenaX + x - 8, kLabArenaY + y, 17, kLabLime);
  d.drawFastVLine(kLabArenaX + x, kLabArenaY + y - 8, 17, kLabLime);
  labKeys(labPageKeys(p.bestMs ? (String("Best ") + String(p.bestMs) + " ms").c_str() : "Hit the target"));
}
void labStepTarget(uint32_t now) {
  LabPlay& p = gLabPlay;
  int x = 0, y = 0;
  labStickSpot(x, y);
  const int gapX = x - p.targetX, gapY = y - p.targetY;
  if (gapX * gapX + gapY * gapY > 100) return;
  const uint32_t took = now - p.targetMs;
  if (!p.bestMs || took < p.bestMs) p.bestMs = took;
  ++p.hits;
  labBeep(1400, 80);
  labNewTarget();
}

// --- Mini Angle: keep it in the moving zone ---------------------------------------------------
void renderLabSteady() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  labDrawHeadFor(Dev::Knob, kLabLime, "STEADY HAND", kLabSoft);
  const lab::Reading& reading = gLabInputs.get(Sig::Knob);
  constexpr int kLeft = 10, kWidth = 220, kTop = 62;
  const int value = reading.live ? reading.value : 0;
  const bool inside = reading.live && abs(value - p.zone) <= lab::kSteadyZone;
  d.fillRoundRect(kLeft, kTop, kWidth, 18, 5, kLabPanel);
  const int zoneLeft = lab::mapRange(p.zone - lab::kSteadyZone, 0, 100, kLeft, kLeft + kWidth);
  const int zoneRight = lab::mapRange(p.zone + lab::kSteadyZone, 0, 100, kLeft, kLeft + kWidth);
  d.fillRoundRect(zoneLeft, kTop, zoneRight - zoneLeft, 18, 4, inside ? kLabGreen : kLabGraph);
  d.fillCircle(lab::mapRange(value, 0, 100, kLeft + 4, kLeft + kWidth - 4), kTop + 9, 7, TFT_WHITE);
  labTextAt(&fonts::FreeSansBold9pt7b, 6, 25, inside ? "In the zone" : "Find the zone", inside ? kLabGreen : TFT_WHITE);
  labStat(6, 90, "HOLDING", labHoldText(static_cast<uint16_t>(min<uint32_t>(p.heldMs, 60000U))), inside ? kLabGreen : TFT_WHITE);
  labStat(120, 90, "BEST", labHoldText(static_cast<uint16_t>(min<uint32_t>(p.bestHeldMs, 60000U))));
  labKeys(labPageKeys("Follow it with the knob"));
}
void labStepSteady() {
  LabPlay& p = gLabPlay;
  if (!labRoll(40)) p.zoneStep = -p.zoneStep;
  if (labRoll(2)) p.zone += p.zoneStep;
  if (p.zone <= lab::kSteadyZone || p.zone >= 100 - lab::kSteadyZone) {
    p.zone = lab::clampTo<int>(p.zone, lab::kSteadyZone, 100 - lab::kSteadyZone);
    p.zoneStep = -p.zoneStep;
  }
  const lab::Reading& reading = gLabInputs.get(Sig::Knob);
  if (reading.live && abs(reading.value - p.zone) <= lab::kSteadyZone) p.heldMs += kLabPlayStepMs;
  else p.heldMs = 0;
  if (p.heldMs > p.bestHeldMs) p.bestHeldMs = p.heldMs;
}

// --- Light: the sky outside -------------------------------------------------------------------
void renderLabSky() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Light, kLabLime, "SKY", kLabSoft);
  const lab::Reading& reading = gLabInputs.get(Sig::Light);
  const int percent = reading.live ? reading.value : 0;
  constexpr int kGround = 104;
  const uint16_t sky = percent < 15 ? rgb565_local(0, 0, 85) : percent < 40 ? rgb565_local(73, 36, 85) : percent < 75 ? rgb565_local(36, 109, 170) : rgb565_local(109, 182, 255);
  d.fillRect(0, 20, 240, kGround - 20, sky);
  d.fillRect(0, kGround, 240, 20, kLabGround);
  const int height = lab::mapRange(percent, 0, 100, kGround - 6, 36);
  if (percent < 15) {
    for (int i = 5; i < 14; ++i) d.fillRect(8 + i * 17, 26 + (i * 37) % 62, 2, 2, TFT_WHITE);  // Clear of the words.
    d.fillCircle(176, 50, 13, kLabSoft);
    d.fillCircle(182, 46, 12, sky);
  } else {
    d.fillCircle(176, height, 15, percent < 40 ? kLabRed : kLabAmber);
  }
  labTextAt(&fonts::Font4, 8, 26, reading.live ? String(percent) + "%" : String("--"), TFT_WHITE);
  labTextAt(&fonts::FreeSansBold9pt7b, 8, 58, reading.live ? lab::skyWord(percent) : "Waiting", TFT_WHITE);
  labKeys(labPageKeys("Cover it, or shine a light"));
}

// --- PIR: an alarm that arms itself -----------------------------------------------------------
void renderLabGuard() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  const uint32_t now = millis();
  const bool tripped = p.guard == LabGuard::Tripped, flash = tripped && (now / 250) & 1;
  labDrawHeadFor(Dev::Motion, kLabLime, "GUARD", kLabSoft);
  const uint16_t color = flash ? TFT_WHITE : tripped ? kLabRed : p.guard == LabGuard::Armed ? kLabGreen : p.guard == LabGuard::Arming ? kLabAmber : kLabPanel;
  const uint16_t ink = p.guard == LabGuard::Off ? TFT_WHITE : TFT_BLACK;
  d.fillRoundRect(6, 24, 228, 62, 8, color);
  const int left = kLabGuardArmSeconds - static_cast<int>((now - p.guardMs) / 1000U);
  const String word = tripped ? String("Motion!") : p.guard == LabGuard::Armed ? String("Watching") : p.guard == LabGuard::Arming ? String("Leave in ") + String(max(left, 1)) : String("Off");
  labTextCenter(&fonts::Font4, 120, 42, word, ink);
  labStat(6, 92, "TIMES SET OFF", String(p.trips));
  labKeys(labPageKeys(p.guard == LabGuard::Off ? "G arm it" : "G turn it off"));
}
void labStepGuard(uint32_t now) {
  LabPlay& p = gLabPlay;
  const bool moving = gLabInputs.get(Sig::Motion).value != 0;
  if (p.guard == LabGuard::Arming && now - p.guardMs >= kLabGuardArmSeconds * 1000U) p.guard = LabGuard::Armed;
  if (p.guard == LabGuard::Armed && moving) {
    p.guard = LabGuard::Tripped;
    ++p.trips;
  }
  if (p.guard == LabGuard::Tripped) labBeep((now / 250) & 1 ? 1400 : 900, 260);
}

// --- Tilt: a spirit level, and a ball to roll -------------------------------------------------
void renderLabLevel() {
  auto& d = gCanvas;
  labDrawHeadFor(Dev::Tilt, kLabLime, "LEVEL", kLabSoft);
  const int side = gLabTiltSide, front = gLabInputs.get(Sig::Tilt).value;
  const bool flat = abs(side) <= 2 && abs(front) <= 2;
  constexpr int cx = 62, cy = 72, kRadius = 48;
  d.fillCircle(cx, cy, kRadius, flat ? kLabComfy : kLabPanel2);
  d.drawCircle(cx, cy, kRadius, kLabSoft);
  d.drawCircle(cx, cy, 11, kLabSoft);
  d.drawFastHLine(cx - kRadius + 2, cy, kRadius * 2 - 3, kLabDim);
  d.drawFastVLine(cx, cy - kRadius + 2, kRadius * 2 - 3, kLabDim);
  const int reachX = lab::clampTo(side, -40, 40) * (kRadius - 10) / 40, reachY = lab::clampTo(front, -40, 40) * (kRadius - 10) / 40;
  d.fillCircle(cx - reachX, cy + reachY, 9, flat ? kLabGreen : kLabCyan);
  labTextAt(&fonts::FreeSansBold9pt7b, 124, 25, flat ? "Level" : "Tilted", flat ? kLabGreen : TFT_WHITE);
  labStat(124, 54, "LEFT-RIGHT", String(side) + "`");
  labStat(124, 86, "FRONT-BACK", String(front) + "`");
  labKeys(labPageKeys("Tip the Cardputer"));
}
void renderLabRoll() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  labDrawHeadFor(Dev::Tilt, kLabLime, String(p.goals) + (p.goals == 1 ? " GOAL" : " GOALS"), kLabGreen);
  d.fillRoundRect(kLabArenaX - 2, kLabArenaY - 1, kLabArenaW + 4, kLabArenaH + 2, 4, kLabPanel2);
  d.fillCircle(kLabArenaX + p.targetX, kLabArenaY + p.targetY, 9, TFT_BLACK);
  d.drawCircle(kLabArenaX + p.targetX, kLabArenaY + p.targetY, 9, kLabLime);
  d.fillCircle(kLabArenaX + static_cast<int>(p.ballX), kLabArenaY + static_cast<int>(p.ballY), 5, kLabCyan);
  labKeys(labPageKeys("Roll it into the hole"));
}
void labStepRoll() {
  LabPlay& p = gLabPlay;
  p.speedX = (p.speedX + gLabTiltSide * 0.03f) * 0.97f;
  p.speedY = (p.speedY - gLabInputs.get(Sig::Tilt).value * 0.03f) * 0.97f;
  p.ballX += p.speedX;
  p.ballY += p.speedY;
  if (p.ballX < 5.0f || p.ballX > kLabArenaW - 6.0f) {
    p.ballX = lab::clampTo(p.ballX, 5.0f, kLabArenaW - 6.0f);
    p.speedX = -p.speedX * 0.5f;
  }
  if (p.ballY < 5.0f || p.ballY > kLabArenaH - 6.0f) {
    p.ballY = lab::clampTo(p.ballY, 5.0f, kLabArenaH - 6.0f);
    p.speedY = -p.speedY * 0.5f;
  }
  const float gapX = p.ballX - p.targetX, gapY = p.ballY - p.targetY;
  if (gapX * gapX + gapY * gapY > 64.0f) return;
  ++p.goals;
  p.speedX = p.speedY = 0.0f;
  labBeep(1400, 80);
  labNewTarget();
}

// --- Servo: ready-made motions ----------------------------------------------------------------
void renderLabMoves() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  labDrawHeadFor(Dev::Servo, kLabLime, p.moving ? "MOVING" : "MOVES", p.moving ? kLabGreen : kLabSoft);
  for (int i = 0; i < static_cast<int>(LabMove::Count); ++i) {
    const int y = 23 + i * 22;
    const bool chosen = i == static_cast<int>(p.move);
    d.fillRoundRect(4, y, 110, 20, 6, chosen ? kLabLime : kLabPanel);
    labTextAt(&fonts::FreeSansBold9pt7b, 12, y + 1, kLabMoveNames[i], chosen ? TFT_BLACK : TFT_WHITE);
  }
  labStat(124, 24, "ANGLE", String(gLabUi.servoAngle) + "`");
  labStat(184, 24, "SPEED", String(p.moveSpeed) + " of 3");
  labBar(124, 58, 110, 11, gLabUi.servoAngle * 100 / 180, kLabLime);
  labTextAt(&fonts::Font0, 124, 76, p.moving ? "ENTER stops it" : "ENTER starts it", kLabAmber);
  labKeys(String(kLabArrowUpDown) + " pick  " + kLabArrowLeftRight + " speed  N back  ESC out");
}
void labStepMoves(uint32_t now) {
  LabPlay& p = gLabPlay;
  if (!p.moving) return;
  const int angle = gLabUi.servoAngle;
  if (p.move == LabMove::Tick) {
    if (now - p.tickMs < 1000U / p.moveSpeed) return;
    p.tickMs = now;
    labServoGoTo(angle >= 180 ? 0 : angle + 30);
    return;
  }
  const int low = p.move == LabMove::Wave ? 60 : 0, high = p.move == LabMove::Wave ? 120 : 180;
  const int step = (p.move == LabMove::Wave ? 5 : 3) * p.moveSpeed;
  if (angle >= high) p.moveUp = false;
  if (angle <= low) p.moveUp = true;
  labServoGoTo(lab::clampTo(angle + (p.moveUp ? step : -step), 0, 180));
}

// --- Speaker: a small piano -------------------------------------------------------------------
void renderLabPiano() {
  auto& d = gCanvas;
  const LabPlay& p = gLabPlay;
  labDrawHeadFor(Dev::Speaker, kLabLime, "PIANO", kLabSoft);
  const bool sounding = gLabUi.beepUntilMs != 0;
  for (int i = 0; i < kLabPianoKeys; ++i) {
    const int x = 5 + i * 29;
    const bool pressed = sounding && i == p.note;
    d.fillRoundRect(x, 24, 27, 78, 3, pressed ? kLabLime : TFT_WHITE);
    labTextCenter(&fonts::Font2, x + 13, 60, kLabPianoNames[i], TFT_BLACK);
    labTextCenter(&fonts::Font0, x + 13, 90, String(i + 1), kLabDim);
  }
  labTextAt(&fonts::Font0, 6, 108, "Play the number keys", kLabAmber);
  labKeys("1 to 8 play  N back  ESC out");
}

// --- Stepping and keys ------------------------------------------------------------------------
void serviceLabPage(uint32_t now) {
  LabPlay& p = gLabPlay;
  const int page = gLabUi.page;
  if (!page || now - p.stepMs < kLabPlayStepMs) return;
  p.stepMs = now;
  switch (static_cast<Dev>(gLabUi.devRow)) {
    case Dev::Ultrasonic: if (page == 1) labStepParking(now); break;
    case Dev::Joystick: if (page == 1) labStepDraw(); else labStepTarget(now); break;
    case Dev::Knob: if (page == 2) labStepSteady(); break;
    case Dev::Motion: if (page == 1) labStepGuard(now); break;
    case Dev::Tilt: if (page == 2) labStepRoll(); break;
    case Dev::Servo: labStepMoves(now); break;
    default: break;
  }
}
// Whether this page used the key. Everything else falls through to the part's own keys.
bool labPageKey(Dev dev, char key, char lower, bool left, bool right, bool up, bool down, bool enter) {
  LabPlay& p = gLabPlay;
  const int page = gLabUi.page;
  if (dev == Dev::Ultrasonic && page == 1 && lower == 'b') {
    p.beeps = !p.beeps;
    gLabUi.beepUntilMs = 0;
    labDriveOff(Out::Tone);
    return true;
  }
  if (dev == Dev::Env && page == 2 && lower == 't') {
    labHistoryReset(gLabHistorySig == Sig::Humidity ? Sig::Temperature : Sig::Humidity);
    return true;
  }
  if (dev == Dev::Motion && page == 1 && lower == 'g') {
    p.guard = p.guard == LabGuard::Off ? LabGuard::Arming : LabGuard::Off;
    p.guardMs = millis();
    gLabUi.beepUntilMs = 0;
    labDriveOff(Out::Tone);
    return true;
  }
  if (dev == Dev::Servo && page == 1) {
    const int moves = static_cast<int>(LabMove::Count);
    if (up || down) {
      p.move = static_cast<LabMove>((static_cast<int>(p.move) + (up ? moves - 1 : 1)) % moves);
    } else if (left || right) {
      p.moveSpeed = lab::clampTo(p.moveSpeed + (right ? 1 : -1), 1, 3);
    } else if (enter) {
      p.moving = !p.moving;
    }
    return true;
  }
  if (dev == Dev::Speaker && page == 1) {
    if (key >= '1' && key < '1' + kLabPianoKeys) {
      p.note = key - '1';
      labBeep(kLabPianoHz[p.note], 300);
    }
    return true;
  }
  return false;
}
void renderLabPage(Dev dev, int page) {
  switch (dev) {
    case Dev::Ultrasonic: if (page == 1) renderLabParking(); else renderLabGraphPage(dev); break;
    case Dev::Env: if (page == 1) renderLabComfort(); else renderLabGraphPage(dev); break;
    case Dev::Joystick: if (page == 1) renderLabDraw(); else renderLabTarget(); break;
    case Dev::Knob: if (page == 1) renderLabGraphPage(dev); else renderLabSteady(); break;
    case Dev::Light: if (page == 1) renderLabSky(); else renderLabGraphPage(dev); break;
    case Dev::Motion: if (page == 1) renderLabGuard(); else renderLabGraphPage(dev); break;
    case Dev::Tilt: if (page == 1) renderLabLevel(); else renderLabRoll(); break;
    case Dev::Servo: renderLabMoves(); break;
    default: renderLabPiano(); break;
  }
}

// --- Dashboard: everything live at once -------------------------------------------------------
// An empty hub port reads as noise, so a part on the hub only counts once it's been opened.
bool labDashShows(Sig sig) {
  if (sig == Sig::Spoken || !gLabInputs.get(sig).live) return false;
  const Dev dev = sig == Sig::Knob ? Dev::Knob : sig == Sig::Light ? Dev::Light : sig == Sig::Motion ? Dev::Motion : Dev::Ultrasonic;
  return lab::partFor(dev) == lab::Part::None || !labOnHub(dev) || gLabPartUsed[static_cast<uint8_t>(lab::partFor(dev))];
}
int labDashCount() {
  int count = 0;
  for (uint8_t i = 0; i < lab::kSigCount; ++i) count += labDashShows(static_cast<Sig>(i)) ? 1 : 0;
  return count;
}
int labDashPages() { return max(1, (labDashCount() + kLabDashTiles - 1) / kLabDashTiles); }
void renderLabDash() {
  auto& d = gCanvas;
  const int count = labDashCount(), pages = labDashPages();
  if (gLabUi.dashPage >= pages) gLabUi.dashPage = 0;
  labHead("Dashboard", launcher_icons::kLab, kLabLime, String(count) + " LIVE", count ? kLabGreen : kLabSoft);
  if (!count) {
    labTextAt(&fonts::FreeSansBold9pt7b, 6, 28, "Nothing reading yet", TFT_WHITE);
    labTextAt(&fonts::Font0, 6, 54, "Plug a sensor in, or open a part in", kLabSoft);
    labTextAt(&fonts::Font0, 6, 65, "Devices so the Lab knows it's there.", kLabSoft);
    labKeys("ESC back");
    return;
  }
  int shown = 0, skip = gLabUi.dashPage * kLabDashTiles;
  for (uint8_t i = 0; i < lab::kSigCount && shown < kLabDashTiles; ++i) {
    const Sig sig = static_cast<Sig>(i);
    if (!labDashShows(sig) || skip-- > 0) continue;
    const int x = 2 + (shown % 3) * 79, y = 21 + (shown / 3) * 52;
    d.fillRoundRect(x, y, 77, 50, 5, kLabPanel2);
    gCanvas.setFont(&fonts::Font0);
    labTextAt(&fonts::Font0, x + 4, y + 3, fitCurrentFontToWidth(lab::kSigs[i].name, 72), kLabMuted);
    labTextAt(&fonts::Font2, x + 4, y + 12, labValueText(sig, gLabInputs.get(sig).value), TFT_WHITE);
    int fromX = -1, fromY = 0;
    for (int at = 0; at < lab::kTrendPoints; ++at) {
      const uint8_t point = gLabTrends.point(sig, at);
      if (!point) {
        fromX = -1;
        continue;
      }
      const int toX = x + 4 + at * 68 / (lab::kTrendPoints - 1), toY = y + 46 - point * 15 / 255;
      if (fromX >= 0) d.drawLine(fromX, fromY, toX, toY, kLabLime);
      fromX = toX;
      fromY = toY;
    }
    ++shown;
  }
  if (pages > 1) labKeys(String(kLabArrowLeftRight) + " more  " + String(gLabUi.dashPage + 1) + " of " + String(pages) + "  ESC back");
  else labKeys("The last minute of each  ESC back");
}
}  // namespace
