// Classic Snake, fully offline and independent of SD/EXT.
namespace {
snake::Game gSnake;

void snakeChanged() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
void newSnakeGame() {
  gSnake.reset(esp_random());
  snakeChanged();
}
void beginSnake() {
  if (!gSnake.initialized) newSnakeGame();
  snakeChanged();
}
void serviceSnake() {
  if (!gSnake.initialized) return;
  // Leaving the game or opening the menu must never cost a life.
  if (gUiMode != UiMode::Snake || gLauncherVisible) {
    if (gSnake.phase == snake::Phase::Playing) { gSnake.pause(); snakeChanged(); }
    return;
  }
  if (gSnake.advance(millis())) snakeChanged();
}
void steerSnake(snake::Dir dir) {
  if (gSnake.phase == snake::Phase::Lost || gSnake.phase == snake::Phase::Won) return;
  gSnake.play(millis());
  gSnake.turn(dir);
}
bool handleSnakeKey(char key, const Keyboard_Class::KeysState& keys) {
  beginSnake();
  if (keys.ctrl || keys.alt || keys.opt) return true;
  const char lower = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  if (lower == 'n') newSnakeGame();
  else if (lower == 'w' || key == ';') steerSnake(snake::Dir::Up);
  else if (lower == 's' || key == '.') steerSnake(snake::Dir::Down);
  else if (lower == 'a' || key == ',') steerSnake(snake::Dir::Left);
  else if (lower == 'd' || key == '/') steerSnake(snake::Dir::Right);
  else if (keys.enter || key == '\r' || key == '\n' || key == ' ') {
    if (gSnake.phase == snake::Phase::Playing) gSnake.pause();
    else if (gSnake.phase == snake::Phase::Lost || gSnake.phase == snake::Phase::Won) newSnakeGame();
    else gSnake.play(millis());
  }
  snakeChanged();
  return true;
}
void renderSnakeUi() {
  beginSnake();
  auto& d = gCanvas;
  const uint16_t bg = rgb565_local(8, 13, 22), muted = rgb565_local(157, 174, 195);
  const uint16_t field = rgb565_local(3, 7, 12), edge = rgb565_local(100, 109, 121);
  const uint16_t bodyColor = rgb565_local(46, 190, 110), headColor = rgb565_local(74, 238, 192);
  const uint16_t red = rgb565_local(228, 39, 55), amber = rgb565_local(255, 185, 45);
  constexpr int kLeft = 7, kTop = 13, kCell = 6;
  const bool playing = gSnake.phase == snake::Phase::Playing;
  const bool lost = gSnake.phase == snake::Phase::Lost;
  const bool won = gSnake.phase == snake::Phase::Won;
  d.fillScreen(bg); d.setFont(&fonts::Font0);
  d.fillRect(kLeft, kTop, snake::kCols * kCell, snake::kRows * kCell, field);
  d.drawRect(kLeft - 1, kTop - 1, snake::kCols * kCell + 2, snake::kRows * kCell + 2, edge);
  if (!won) {
    const int x = kLeft + (gSnake.food % snake::kCols) * kCell, y = kTop + (gSnake.food / snake::kCols) * kCell;
    d.fillCircle(x + 2, y + 2, 2, red);
  }
  for (int i = 0; i < gSnake.length; ++i) {
    const int cell = gSnake.cellOf(i);
    const uint16_t color = i < gSnake.length - 1 ? bodyColor : lost ? red : headColor;
    d.fillRect(kLeft + (cell % snake::kCols) * kCell, kTop + (cell / snake::kCols) * kCell, 5, 5, color);
  }
  d.setFont(&fonts::Font2); d.setTextColor(headColor, bg); d.setCursor(132, 8); d.print("SNAKE");
  d.setFont(&fonts::Font0); d.setTextColor(muted, bg);
  d.setCursor(132, 29); d.print("Score " + String(int(gSnake.score)));
  d.setCursor(132, 41); d.print("Best " + String(int(gSnake.best)));
  d.setCursor(132, 53); d.print("Length " + String(int(gSnake.length)));
  const char* state = "READY";
  const char* action = "Enter: start";
  if (playing) { state = "GO!"; action = "Enter: pause"; }
  else if (gSnake.phase == snake::Phase::Paused) { state = "PAUSED"; action = "Enter: resume"; }
  else if (lost) { state = "GAME OVER"; action = "Enter: new game"; }
  else if (won) { state = "YOU WIN!"; action = "Enter: new game"; }
  d.setTextColor(TFT_WHITE, bg); d.setCursor(132, 67); d.print(state);
  d.setTextColor(amber, bg); d.setCursor(132, 79); d.print(action);
  d.setTextColor(muted, bg); d.setCursor(132, 91); d.print("WASD / arrows");
  d.setCursor(132, 101); d.print("N: new game");
  d.setCursor(132, 111); d.print("Esc exit Tab menu");
  renderLauncherOverlay(); renderDebugOverlay();
}
} // namespace
