// Hot Air Balloon, for an ENV III and a Joystick 2 sharing the Grove port through a hub.
// Both are optional. The port is held through Air Music's sensor object, which owns the bus.
namespace {
constexpr uint8_t kBalloonEnvAddress = 0x44;      // The SHT30 inside the ENV III.
constexpr uint8_t kBalloonEnvMeasure[2] = {0x24, 0x00};  // Single reading, high accuracy.
constexpr uint8_t kBalloonStickAddress = 0x63;
constexpr uint8_t kBalloonStickOffsetReg = 0x60;  // Signed X then Y, zero at the center.
constexpr uint8_t kBalloonStickButtonReg = 0x20;  // 0 while pressed.
constexpr int kBalloonStickRightSign = 1;
constexpr int kBalloonMostSteps = 3;

class BalloonSensors {
 public:
  bool envTrigger() {
    Wire.beginTransmission(kBalloonEnvAddress);
    Wire.write(kBalloonEnvMeasure, 2);
    return Wire.endTransmission() == 0;
  }
  bool envRead(uint8_t* reading) {
    if (Wire.requestFrom(kBalloonEnvAddress, static_cast<uint8_t>(6)) != 6) return false;
    for (int i = 0; i < 6; ++i) reading[i] = static_cast<uint8_t>(Wire.read());
    return true;
  }
  bool stickRead(int8_t& x, bool& pressed) {
    uint8_t axes[2] = {};
    uint8_t button = 1;
    if (!readStick(kBalloonStickOffsetReg, axes, 2) || !readStick(kBalloonStickButtonReg, &button, 1)) return false;
    x = static_cast<int8_t>(axes[0]);
    pressed = button == 0;
    return true;
  }

 private:
  bool readStick(uint8_t reg, uint8_t* out, uint8_t length) {
    Wire.beginTransmission(kBalloonStickAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(kBalloonStickAddress, length) != length) return false;
    for (uint8_t i = 0; i < length; ++i) out[i] = static_cast<uint8_t>(Wire.read());
    return true;
  }
};

BalloonSensors gBalloonSensors;
balloon::Game gBalloon;
balloon::Env gBalloonEnv;
balloon::Stick gBalloonStick;
uint32_t gBalloonStepMs = 0;
bool gBalloonRunning = false;

void balloonRedraw() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
void balloonSounds() {
  const uint8_t events = gBalloon.events;
  gBalloon.events = 0;
  if (events & balloon::kHurt) focusTone(220, 180);
  else if (events & balloon::kLevelUp) focusTone(1760, 180);
  else if (events & balloon::kGotHeart) focusTone(1047, 120);
  else if (events & balloon::kGotStar) focusTone(1568, 60);
}
void beginBalloon() {
  gBalloon.reset(esp_random());
  gBalloonEnv = balloon::Env{};
  gBalloonStick = balloon::Stick{};
  gBalloonRunning = false;
  balloonRedraw();
}
void stopBalloon() {
  gAirSensor.close();
  gBalloonEnv = balloon::Env{};
  gBalloonStick = balloon::Stick{};
  gBalloonRunning = false;
}
void serviceBalloon() {
  if (gUiMode != UiMode::Balloon) return;
  if (gLauncherVisible) {
    gBalloonRunning = false;  // Frozen behind the menu, and it resumes without a jump.
    return;
  }
  const uint32_t now = millis();
  gAirSensor.open();
  const int32_t blown = balloon::pollEnv(gBalloonEnv, gBalloonSensors, now);
  if (blown > 0) {
    gBalloon.addHeat(blown);
    handleScreensaverWake();
  }
  if (!gBalloonRunning) {
    gBalloonRunning = true;
    gBalloonStepMs = now;
  }
  if (now - gBalloonStepMs < balloon::kStepMs) return;
  balloon::pollStick(gBalloonStick, gBalloonSensors, now);
  const int stick = gBalloonStick.x * kBalloonStickRightSign;
  if (gBalloonStick.pressed || stick > balloon::kStickDead || stick < -balloon::kStickDead) handleScreensaverWake();
  for (int steps = 0; now - gBalloonStepMs >= balloon::kStepMs && steps < kBalloonMostSteps; ++steps) {
    gBalloonStepMs += balloon::kStepMs;
    if (gBalloonStick.pressed) gBalloon.addHeat(balloon::kBurnerHeat);
    gBalloon.step(stick);
  }
  // After a long stall, slow down rather than throw the balloon into a bird.
  if (now - gBalloonStepMs >= balloon::kStepMs) gBalloonStepMs = now;
  balloonSounds();
  balloonRedraw();
}
bool handleBalloonKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  if (lower == 'a' || key == ',') gBalloon.nudge(-1);
  else if (lower == 'd' || key == '/') gBalloon.nudge(1);
  else if (lower == 'w' || key == ';' || keys.enter || key == '\r' || key == '\n' || key == ' ') {
    gBalloon.addHeat(balloon::kKeyHeat);
  }
  balloonRedraw();
  return true;
}
void drawBalloonStar(int x, int y, int r, uint16_t color) {
  auto& d = gCanvas;
  d.fillTriangle(x, y - r, x - r, y + r / 2, x + r, y + r / 2, color);
  d.fillTriangle(x, y + r, x - r, y - r / 2, x + r, y - r / 2, color);
}
void drawBalloonHeart(int x, int y, int r, uint16_t color) {
  auto& d = gCanvas;
  d.fillCircle(x - r / 2, y - r / 3, r / 2 + 1, color);
  d.fillCircle(x + r / 2, y - r / 3, r / 2 + 1, color);
  d.fillTriangle(x - r, y, x + r, y, x, y + r, color);
}
void drawBalloonBird(int x, int y, bool wingsUp) {
  auto& d = gCanvas;
  const uint16_t body = rgb565_local(60, 70, 90), beak = rgb565_local(255, 170, 60);
  d.fillCircle(x, y, 5, body);
  d.fillTriangle(x + 2, y, x + 10, y + (wingsUp ? -8 : 8), x + 9, y, body);
  d.fillTriangle(x - 5, y - 1, x - 10, y + 1, x - 5, y + 3, beak);
  d.fillCircle(x - 2, y - 2, 1, TFT_WHITE);
}
void renderBalloonUi() {
  auto& d = gCanvas;
  // The sky changes every level: day, then sunset, then a starry night.
  constexpr uint8_t kSkies[3][3] = {{92, 196, 224}, {255, 150, 100}, {20, 24, 64}};
  constexpr uint8_t kClouds[3][3] = {{245, 250, 255}, {255, 215, 190}, {70, 76, 120}};
  constexpr uint8_t kHills[3][3] = {{96, 200, 72}, {120, 140, 60}, {30, 80, 60}};
  const int time = gBalloon.level() % 3;
  const uint16_t sky = rgb565_local(kSkies[time][0], kSkies[time][1], kSkies[time][2]);
  const uint16_t cloud = rgb565_local(kClouds[time][0], kClouds[time][1], kClouds[time][2]);
  const uint16_t hill = rgb565_local(kHills[time][0], kHills[time][1], kHills[time][2]);
  const uint16_t gold = rgb565_local(255, 214, 64), red = rgb565_local(236, 60, 72);
  const uint16_t cream = rgb565_local(255, 240, 200), orange = rgb565_local(255, 136, 64);
  const uint16_t brown = rgb565_local(140, 90, 50), panel = rgb565_local(8, 13, 22);
  const int frame = gBalloon.frame;

  d.fillScreen(sky);
  if (time == 2) {
    for (int i = 0; i < 12; ++i) d.drawPixel((i * 37 + 11) % 240, (i * 23 + 5) % 100, TFT_WHITE);
  }
  for (int i = 0; i < 3; ++i) {
    const int x = 270 - ((frame / 3 + i * 97) % 300), y = 22 + i * 24;
    d.fillCircle(x, y, 7, cloud);
    d.fillCircle(x + 9, y + 2, 9, cloud);
    d.fillCircle(x + 20, y + 2, 6, cloud);
  }
  d.fillRect(0, balloon::kGroundY, 240, 135 - balloon::kGroundY, hill);

  for (const auto& thing : gBalloon.things) {
    const int x = thing.x >> 8, y = gBalloon.thingY(thing);
    if (thing.kind == balloon::Kind::Star) drawBalloonStar(x, y, 6, gold);
    else if (thing.kind == balloon::Kind::Heart) drawBalloonHeart(x, y, 6, red);
    else if (thing.kind == balloon::Kind::Bird) drawBalloonBird(x, y, (frame / 6) % 2 == 0);
  }

  // It blinks while it's safe after a bump.
  if ((gBalloon.hurtSteps / 4) % 2 == 0) {
    const int x = gBalloon.x >> 8, y = gBalloon.y >> 8;
    d.drawLine(x - 9, y + 8, x - 4, y + 18, brown);
    d.drawLine(x + 9, y + 8, x + 4, y + 18, brown);
    d.fillRect(x - 4, y + 18, 9, 6, brown);
    if (gBalloon.heat > 0) d.fillTriangle(x - 3, y + 16, x + 3, y + 16, x, y + 7 - (frame / 2) % 3, orange);
    d.fillCircle(x, y, 12, red);
    d.fillEllipse(x, y, 7, 12, cream);
    d.fillEllipse(x, y, 3, 12, red);
  }

  drawBalloonStar(9, 9, 6, gold);
  d.setFont(&fonts::Font2);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.setCursor(19, 2);
  d.print(String(int(gBalloon.stars)));
  d.fillRoundRect(80, 5, 80, 6, 3, panel);
  if (gBalloon.heat > 0) d.fillRoundRect(80, 5, max<int>(6, gBalloon.heat * 80 / balloon::kMaxHeat), 6, 3, orange);
  for (int i = 0; i < gBalloon.lives; ++i) drawBalloonHeart(230 - i * 15, 9, 6, red);

  String message;
  if (gBalloon.phase == balloon::Phase::Over) {
    message = "You got " + String(int(gBalloon.stars)) + " stars!  Best " + String(int(gBalloon.best));
  } else if (gBalloon.phase == balloon::Phase::Ready) {
    message = gBalloonEnv.unit == air_music::Unit::Missing ? "Press Space to fly" : "Blow on the sensor to fly!";
  }
  if (!message.isEmpty()) {
    d.setFont(&fonts::Font0);
    const int width = d.textWidth(message) + 16;
    d.fillRoundRect((240 - width) / 2, 54, width, 20, 5, panel);
    d.setTextColor(TFT_WHITE, panel);
    d.setCursor((240 - width) / 2 + 8, 60);
    d.print(message);
  }
  renderLauncherOverlay();
  renderDebugOverlay();
}
} // namespace
