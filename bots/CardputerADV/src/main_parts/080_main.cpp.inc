// Ball Catch, for a Joystick 2 on the Grove port: drop the sliding ball, then catch it in the
// basket. The keyboard plays it too. Its joystick and sound are Hot Air Balloon's.
namespace {
constexpr int kBallMostSteps = 3;

ball_catch::Game gBall;
balloon::Stick gBallStick;
uint32_t gBallStepMs = 0;
bool gBallRunning = false, gBallHeld = false;

void ballRedraw() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
void ballSounds() {
  const uint8_t events = gBall.events;
  gBall.events = 0;
  if ((events & ball_catch::kMissed) && gBall.lives == 0) gBalloonAudio.buzz(420, 60, 0.9f, balloon_audio::kCrashLevel);
  else if (events & ball_catch::kMissed) gBalloonAudio.buzz(240, 150, 0.35f, balloon_audio::kBuzzLevel);
  else if (events & ball_catch::kLevelUp) gBalloonAudio.chime(1047, 1568);
  else if (events & ball_catch::kCaught) gBalloonAudio.chime(1047, 1319);
}
void beginBallCatch() {
  gBall.reset(esp_random());
  gBallStick = balloon::Stick{};
  gBallRunning = false;
  gBallHeld = false;
  gBalloonAudio.reset();
  gBalloonAudio.setMusic(false);
  ballRedraw();
}
void stopBallCatch() {
  M5Cardputer.Speaker.stop(kBalloonAudioChannel);
  gAirSensor.close();
  gBallStick = balloon::Stick{};
  gBallRunning = false;
}
void serviceBallCatch() {
  if (gUiMode != UiMode::BallCatch) return;
  serviceBalloonAudio();
  if (gLauncherVisible) {
    gBallRunning = false;  // Frozen behind the menu, and it resumes without a jump.
    return;
  }
  const uint32_t now = millis();
  gAirSensor.open();
  if (!gBallRunning) {
    gBallRunning = true;
    gBallStepMs = now;
  }
  if (now - gBallStepMs < ball_catch::kStepMs) return;
  balloon::pollStick(gBallStick, gBalloonSensors, now);
  // Only a fresh press drops, so a held button can't drop every ball.
  if (gBallStick.pressed && !gBallHeld) gBall.drop();
  gBallHeld = gBallStick.pressed;
  const int stick = gBallStick.x * kBalloonStickRightSign;
  if (gBallStick.pressed || stick > ball_catch::kStickDead || stick < -ball_catch::kStickDead) handleScreensaverWake();
  for (int steps = 0; now - gBallStepMs >= ball_catch::kStepMs && steps < kBallMostSteps; ++steps) {
    gBallStepMs += ball_catch::kStepMs;
    gBall.step(stick);
  }
  // After a long stall, slow down rather than jump the ball past the basket.
  if (now - gBallStepMs >= ball_catch::kStepMs) gBallStepMs = now;
  ballSounds();
  ballRedraw();
}
bool handleBallCatchKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  if (lower == 'a' || key == ',') gBall.nudge(-1);
  else if (lower == 'd' || key == '/') gBall.nudge(1);
  else if (lower == 's' || key == '.' || keys.enter || key == '\r' || key == '\n' || key == ' ') gBall.drop();
  ballRedraw();
  return true;
}
void renderBallCatchUi() {
  auto& d = gCanvas;
  constexpr uint8_t kBalls[6][3] = {{236, 60, 72},  {255, 136, 64}, {255, 214, 64},
                                    {88, 240, 141}, {47, 227, 255}, {157, 102, 255}};
  const uint16_t sky = rgb565_local(150, 216, 240), grass = rgb565_local(96, 200, 72);
  const uint16_t rail = rgb565_local(112, 170, 196), shadow = rgb565_local(60, 130, 56);
  const uint16_t wood = rgb565_local(170, 112, 60), rim = rgb565_local(120, 76, 40);
  const uint16_t gold = rgb565_local(255, 214, 64), red = rgb565_local(236, 60, 72);
  const uint16_t panel = rgb565_local(8, 13, 22);
  const uint8_t* color = kBalls[gBall.caught % 6];
  const uint16_t ball = rgb565_local(color[0], color[1], color[2]);
  const int ballX = gBall.ballX >> 8, ballY = gBall.ballY >> 8;
  const int basketX = gBall.basketX >> 8, half = gBall.basketWidth() / 2;

  d.fillScreen(sky);
  d.fillRect(0, ball_catch::kTrackY + ball_catch::kBallRadius + 1, 240, 2, rail);
  d.fillRect(0, ball_catch::kGroundY, 240, 135 - ball_catch::kGroundY, grass);
  // The ball's shadow shows where it will land.
  d.fillEllipse(ballX, ball_catch::kGroundY + 3, ball_catch::kBallRadius, 2, shadow);

  d.fillCircle(ballX, ballY, ball_catch::kBallRadius, ball);
  d.fillCircle(ballX - 2, ballY - 2, 2, TFT_WHITE);

  const int top = ball_catch::kRimY, left = basketX - half, right = basketX + half - 1;
  d.fillTriangle(left, top, right, top, left + 6, ball_catch::kGroundY - 1, wood);
  d.fillTriangle(right, top, left + 6, ball_catch::kGroundY - 1, right - 6, ball_catch::kGroundY - 1, wood);
  d.fillRect(basketX - half, top, half * 2, 3, rim);

  drawBalloonStar(9, 9, 6, gold);
  d.setFont(&fonts::Font2);
  d.setTextSize(1);
  d.setTextColor(TFT_BLACK);
  d.setCursor(19, 2);
  d.print(String(int(gBall.caught)));
  for (int i = 0; i < gBall.lives; ++i) drawBalloonHeart(230 - i * 15, 9, 6, red);

  String message;
  if (gBall.phase == ball_catch::Phase::Over) {
    message = "You caught " + String(int(gBall.caught)) + "!  Best " + String(int(gBall.best));
  } else if (gBall.phase == ball_catch::Phase::Ready) {
    message = gBallStick.unit == air_music::Unit::Missing ? "Space drops, A and D move" : "Press the button to drop!";
  }
  if (!message.isEmpty()) {
    d.setFont(&fonts::Font0);
    const int width = d.textWidth(message) + 16;
    d.fillRoundRect((240 - width) / 2, 60, width, 20, 5, panel);
    d.setTextColor(TFT_WHITE, panel);
    d.setCursor((240 - width) / 2 + 8, 66);
    d.print(message);
  }
  renderLauncherOverlay();
  renderDebugOverlay();
}
} // namespace
