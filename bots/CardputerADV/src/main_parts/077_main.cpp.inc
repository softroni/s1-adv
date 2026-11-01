// Flappy Bird. A hand over the ultrasonic unit steers the bird and any key flaps it, so it
// still plays with nothing on the Grove port. It shares Air Music's sensor.
namespace {
constexpr int kFlappyMostSteps = 3;

flappy::Game gFlappy;
flappy::Hand gFlappyHand;
uint32_t gFlappyStepMs = 0;
bool gFlappyRunning = false;

void flappyRedraw() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
void flappySounds() {
  const uint8_t events = gFlappy.events;
  gFlappy.events = 0;
  if (events & flappy::kCrashed) focusTone(196, 220);
  else if (events & flappy::kScored) focusTone(1319, 70);
  else if (events & flappy::kFlapped) focusTone(880, 40);
}
void beginFlappy() {
  gFlappy.reset(esp_random());
  gFlappyHand = flappy::Hand{};
  gFlappyRunning = false;
  flappyRedraw();
}
void stopFlappy() {
  gAirSensor.close();
  gFlappyHand = flappy::Hand{};
  gFlappyRunning = false;
}
void serviceFlappy() {
  if (gUiMode != UiMode::Flappy) return;
  if (gLauncherVisible) {
    gFlappyRunning = false;  // Frozen behind the menu, and it resumes without a jump.
    return;
  }
  const uint32_t now = millis();
  gAirSensor.open();
  air_music::poll(gFlappyHand, gAirSensor, now);
  // Waving counts as using the device, so the screen stays on.
  if (gFlappyHand.here) handleScreensaverWake();
  if (!gFlappyRunning) {
    gFlappyRunning = true;
    gFlappyStepMs = now;
  }
  int steps = 0;
  while (now - gFlappyStepMs >= flappy::kStepMs && steps < kFlappyMostSteps) {
    gFlappyStepMs += flappy::kStepMs;
    gFlappy.step(gFlappyHand.here, gFlappyHand.mm);
    ++steps;
  }
  // After a long stall, slow down rather than throw the bird into a pipe.
  if (now - gFlappyStepMs >= flappy::kStepMs) gFlappyStepMs = now;
  if (steps == 0) return;
  flappySounds();
  flappyRedraw();
}
bool handleFlappyKey(char, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  gFlappy.flap();
  flappySounds();
  flappyRedraw();
  return true;
}
void renderFlappyUi() {
  auto& d = gCanvas;
  const uint16_t sky = rgb565_local(92, 196, 224), pipeColor = rgb565_local(72, 176, 64);
  const uint16_t lip = rgb565_local(46, 130, 48), grass = rgb565_local(96, 200, 72);
  const uint16_t dirt = rgb565_local(222, 200, 120), yellow = rgb565_local(255, 214, 64);
  const uint16_t orange = rgb565_local(255, 136, 64), wing = rgb565_local(255, 240, 180);
  const uint16_t panel = rgb565_local(8, 13, 22);
  d.fillScreen(sky);
  for (const auto& pipe : gFlappy.pipes) {
    const int x = pipe.x >> 8, bottom = pipe.gapTop + pipe.gapSize;
    if (x >= 240 + 2 || x + flappy::kPipeWidth + 2 <= 0) continue;
    d.fillRect(x, 0, flappy::kPipeWidth, pipe.gapTop, pipeColor);
    d.fillRect(x - 2, pipe.gapTop - 6, flappy::kPipeWidth + 4, 6, lip);
    d.fillRect(x, bottom, flappy::kPipeWidth, flappy::kGroundY - bottom, pipeColor);
    d.fillRect(x - 2, bottom, flappy::kPipeWidth + 4, 6, lip);
  }
  d.fillRect(0, flappy::kGroundY, 240, 2, grass);
  d.fillRect(0, flappy::kGroundY + 2, 240, 135 - flappy::kGroundY - 2, dirt);

  const int bx = flappy::kBirdX, by = gFlappy.y >> 8;
  d.fillCircle(bx, by, 6, yellow);
  d.fillCircle(bx - 3, by + 1, 3, wing);
  d.fillCircle(bx + 3, by - 2, 2, TFT_WHITE);
  d.drawPixel(bx + 4, by - 2, TFT_BLACK);
  d.fillTriangle(bx + 5, by - 1, bx + 11, by + 1, bx + 5, by + 3, orange);

  d.setFont(&fonts::Font2);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  const String score(int(gFlappy.score));
  d.setCursor((240 - d.textWidth(score)) / 2, 3);
  d.print(score);

  String message;
  if (gFlappy.phase == flappy::Phase::Lost) {
    message = "Score " + String(int(gFlappy.score)) + "  Best " + String(int(gFlappy.best));
  } else if (gFlappy.phase == flappy::Phase::Ready) {
    message = gFlappyHand.unit == air_music::Unit::Missing ? "Press Space to flap" : "Wave a hand or press Space";
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
