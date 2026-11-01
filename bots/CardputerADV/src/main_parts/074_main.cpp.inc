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
  // The board is the whole screen, and its edges are the walls. There's no text,
  // so a still snake is dimmed and a lost one has a red head.
  constexpr int kTop = 2, kCell = 10, kBlock = kCell - 1;
  const bool moving = gSnake.phase == snake::Phase::Playing;
  const uint16_t field = rgb565_local(3, 7, 12), wall = rgb565_local(100, 109, 121);
  const uint16_t red = rgb565_local(228, 39, 55);
  const uint16_t bodyColor = moving ? rgb565_local(46, 190, 110) : wall;
  const uint16_t headColor = gSnake.phase == snake::Phase::Lost ? red
      : moving ? rgb565_local(74, 238, 192) : TFT_WHITE;
  d.fillScreen(wall);
  d.fillRect(0, kTop, snake::kCols * kCell, snake::kRows * kCell, field);
  if (gSnake.phase != snake::Phase::Won) {
    const int x = (gSnake.food % snake::kCols) * kCell, y = kTop + (gSnake.food / snake::kCols) * kCell;
    d.fillCircle(x + kBlock / 2, y + kBlock / 2, kBlock / 2, red);
  }
  for (int i = 0; i < gSnake.length; ++i) {
    const int cell = gSnake.cellOf(i);
    d.fillRect((cell % snake::kCols) * kCell, kTop + (cell / snake::kCols) * kCell, kBlock, kBlock,
               i < gSnake.length - 1 ? bodyColor : headColor);
  }
  renderLauncherOverlay(); renderDebugOverlay();
}
} // namespace
