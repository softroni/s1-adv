// Air Music: wave a hand over the M5Stack I2C ultrasonic unit on the Grove port to play
// notes. The bus is only held while the app is open, so NFC Notes can use the port too.
namespace {
constexpr uint8_t kAirSdaPin = 2;   // Grove yellow.
constexpr uint8_t kAirSclPin = 1;   // Grove white.
constexpr uint32_t kAirI2cHz = 100000U;
constexpr uint8_t kAirSensorAddress = 0x57;
constexpr uint8_t kAirSensorMeasure = 0x01;
constexpr uint8_t kAirSpeakerChannel = 5;
// Notes are queued in short slices, so a new note starts quickly and a held one never gaps.
constexpr uint32_t kAirSliceMs = 60;

class AirMusicSensor {
 public:
  void open() {
    if (mOpen) return;
    Wire.begin(kAirSdaPin, kAirSclPin, kAirI2cHz);
    mOpen = true;
  }
  void close() {
    if (!mOpen) return;
    Wire.end();
    mOpen = false;
  }
  bool trigger() {
    Wire.beginTransmission(kAirSensorAddress);
    Wire.write(kAirSensorMeasure);
    return Wire.endTransmission() == 0;
  }
  bool read(uint32_t& micrometers) {
    if (Wire.requestFrom(kAirSensorAddress, static_cast<uint8_t>(3)) != 3) return false;
    micrometers = 0;
    for (int i = 0; i < 3; ++i) micrometers = (micrometers << 8) | static_cast<uint8_t>(Wire.read());
    return true;
  }

 private:
  bool mOpen = false;
};

AirMusicSensor gAirSensor;
air_music::App gAirMusic;
int8_t gAirPlaying = -1;

void airMusicRedraw() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
void airMusicQuiet() {
  if (gAirPlaying >= 0) M5Cardputer.Speaker.stop(kAirSpeakerChannel);
  gAirPlaying = -1;
}
void airMusicPlay(int note) {
  if (note < 0) { airMusicQuiet(); return; }
  auto& speaker = M5Cardputer.Speaker;
  const float hz = air_music::kHz[note];
  if (note != gAirPlaying) speaker.tone(hz, kAirSliceMs, kAirSpeakerChannel, true);
  else if (speaker.isPlaying(kAirSpeakerChannel) < 2) speaker.tone(hz, kAirSliceMs, kAirSpeakerChannel, false);
  gAirPlaying = static_cast<int8_t>(note);
}
void beginAirMusic() {
  gAirMusic.reset();
  airMusicRedraw();
}
void stopAirMusic() {
  airMusicQuiet();
  gAirSensor.close();
  gAirMusic.reset();
}
void serviceAirMusic() {
  if (gUiMode != UiMode::AirMusic) return;
  if (gLauncherVisible || !focusToneAllowed()) {
    airMusicQuiet();
    if (gAirMusic.note >= 0) { gAirMusic.silence(); airMusicRedraw(); }
    return;
  }
  const air_music::Unit unit = gAirMusic.unit;
  const int note = gAirMusic.note;
  gAirSensor.open();
  air_music::poll(gAirMusic, gAirSensor, millis());
  // Waving counts as using the device, so the screen stays on for the whole song.
  if (gAirMusic.note >= 0) handleScreensaverWake();
  airMusicPlay(gAirMusic.note);
  if (gAirMusic.note != note || gAirMusic.unit != unit) airMusicRedraw();
}
void renderAirMusicUi() {
  auto& d = gCanvas;
  constexpr uint8_t kRainbow[air_music::kNotes][3] = {
      {255, 63, 85}, {255, 120, 60}, {255, 176, 64}, {250, 225, 70}, {120, 235, 90},
      {60, 220, 160}, {47, 227, 255}, {70, 150, 255}, {130, 105, 255}, {220, 100, 255}};
  constexpr int kBarWidth = 24;
  const uint16_t bg = rgb565_local(4, 10, 18);
  d.fillScreen(bg);
  d.setFont(&fonts::Font0);
  d.setTextSize(1);
  // A staircase of bars, like a xylophone: high notes are on the right, nearest the sensor.
  for (int i = 0; i < air_music::kNotes; ++i) {
    const uint8_t* c = kRainbow[i];
    const bool on = i == gAirMusic.note;
    const uint16_t color = on ? rgb565_local(c[0], c[1], c[2]) : rgb565_local(c[0] / 4, c[1] / 4, c[2] / 4);
    const int height = 40 + i * 9, x = i * kBarWidth, y = 135 - height;
    d.fillRoundRect(x + 1, y, kBarWidth - 2, height, 4, color);
    if (on) d.drawRoundRect(x + 1, y, kBarWidth - 2, height, 4, TFT_WHITE);
    const char name[2] = {air_music::kNames[i], '\0'};
    d.setTextColor(on ? bg : rgb565_local(c[0], c[1], c[2]), color);
    d.setCursor(x + 9, 124);
    d.print(name);
  }
  d.setTextColor(rgb565_local(156, 185, 203), bg);
  d.setCursor(6, 3);
  if (gAirMusic.unit == air_music::Unit::Missing) d.print("Plug the ultrasonic into Grove");
  else if (gAirMusic.note < 0) d.print("Wave a hand over the sensor");
  renderLauncherOverlay();
  renderDebugOverlay();
}
} // namespace
