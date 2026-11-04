// The radio's knob and LED strip: the Lab's Mini Angle and its SK6812 strip on the Grove port,
// where the Lab's Devices screen last found them. The knob is on the white wire and the strip on
// the yellow, so both fit at once through the 1-to-3 hub. The knob turns a dial through every
// station on screen. The strip shows how loud the music is, the volume while it changes, and where
// the station is on the dial when that changes. Only on the radio's own screen, and never through
// the I/O hub, whose default setup names a knob on its first port whether one is there or not, and
// an empty port's reading would tune at random.
namespace {
constexpr uint32_t kRadioKnobRestMs = 600;    // Still this long, the knob's station plays.
constexpr uint8_t kRadioKnobSure = 3;         // Readings in a row at a new place before the dial goes there.
constexpr uint32_t kRadioStripShowMs = 1500;  // The volume, or the dial, before the music again.
bool gRadioPartsHeld = false, gRadioPartsTried = false;
// The dial: where the knob last chose, and a new place it has to hold before that changes.
int gRadioKnobSlot = -1, gRadioKnobNext = -1, gRadioKnobCount = 0;
uint8_t gRadioKnobNextSeen = 0;
uint32_t gRadioKnobReadMs = 0;
// The strip: what it's showing, and the frame last sent, so an unchanged one costs nothing.
uint32_t gRadioStripMs = 0, gRadioStripVolumeUntilMs = 0, gRadioStripDialUntilMs = 0;
int gRadioStripVolume = -1, gRadioStripStation = -1, gRadioStripLevel = 0;
uint8_t gRadioStripSent[4] = {0xFF, 0, 0, 0};  // How many lit, or the dial's LED with 0x80, then the colour.

void radioPartsReopen() { gRadioPartsTried = false; }

// Only with the Lab's setup read from the card: a setup that couldn't be might have the relays on
// the port, and the strip's pulses would click them.
void radioPartsTake() {
  labReadBook();
  const lab::Direct& direct = gLabBook.direct;
  if (!gLabBookKnown || direct.has(lab::Part::Relay) || !(direct.has(lab::Part::Knob) || direct.has(lab::Part::Strip))) return;
  if (gNfcRadio.busOpen()) return;
  // The radio has the strip now, so a colour a check from the chat left on it isn't put back later.
  for (lab::Out out : {lab::Out::StripColor, lab::Out::StripBright, lab::Out::StripLength}) labDriveOff(out);
  labPartsFresh();
  labTakeWires();
  gRadioPartsHeld = true;
  gRadioKnobSlot = gRadioKnobNext = -1;
  gRadioKnobReadMs = 0;
  gRadioStripVolume = gSpeakerVolume;
  gRadioStripStation = -1;  // So the strip opens on where the station is.
  gRadioStripLevel = 0;
  gRadioStripSent[0] = 0xFF;
  logf("[RADIO] on the Grove port: knob=%d strip=%d", int(direct.has(lab::Part::Knob)), int(direct.has(lab::Part::Strip)));
}
// Dark before it goes. This can run inside a screen change, already deep in the stack, so the
// frame is on the heap for the moment it takes rather than adding 2.9 KB there.
void radioStripBlank() {
  if (!gLabRmt || !labOnWires(lab::Part::Strip)) return;
  memset(gLabStripRgb, 0, sizeof(gLabStripRgb));
  const size_t symbols = size_t(lab::kStripLeds) * lab::kStripBits;
  auto* bits = static_cast<rmt_data_t*>(malloc(sizeof(rmt_data_t) * symbols));
  if (!bits) return;
  lab::encodeStrip(gLabStripRgb, lab::kStripLeds, bits);
  rmtWriteBlocking(gLabRmt, bits, symbols);
  free(bits);
}
// On leaving the radio's screen, before the next one starts, so the Lab or a game finds the port free.
void radioPartsGiveBack() {
  if (!gRadioPartsHeld) return;
  gRadioPartsHeld = false;
  gRadioKnobTuning = false;
  const bool strip = labOnWires(lab::Part::Strip);
  radioStripBlank();
  labPartsOff();
  // Nothing drives the strip's data line now, and a floating one can show as a flicker.
  if (strip) pinMode(kLabYellowPin, INPUT_PULLDOWN);
  labForgetOutputs();
}

// --- The knob ---------------------------------------------------------------------------------
// Its reading as 0 to 1000, clockwise up, as the Lab shows it.
int radioKnobPermille() {
  const int top = gLabKnobRange.top ? gLabKnobRange.top : 1;
  const int raw = gLabKnobRaw > top ? top : gLabKnobRaw;
  return 1000 - raw * 1000 / top;
}
void radioReadKnob(uint32_t now) {
  if (!gLabBook.direct.has(lab::Part::Knob)) return;
  // Unplugged, the white wire floats and the Lab stops believing it, so the dial starts afresh.
  if (!gLabInputs.get(lab::Sig::Knob).live || gLabWhiteSilent >= kLabSilentChecks) {
    gRadioKnobSlot = gRadioKnobNext = -1;
    return;
  }
  if (gLabKnobMs == gRadioKnobReadMs) return;  // Nothing new since the last look.
  gRadioKnobReadMs = gLabKnobMs;
  const int count = gRadioSelection.dialCount();
  if (count <= 0) return;
  const int slot = radio_catalog::dialSlotFor(radioKnobPermille(), count, gRadioKnobSlot);
  // Where it sits when it's found, or when L or a change to the list redraws the dial, isn't a
  // turn, so the station stays. The knob tunes once it's turned.
  if (gRadioKnobSlot < 0 || count != gRadioKnobCount) {
    gRadioKnobSlot = slot;
    gRadioKnobCount = count;
    gRadioKnobNext = -1;
    return;
  }
  if (slot == gRadioKnobSlot) {
    gRadioKnobNext = -1;
    return;
  }
  if (slot != gRadioKnobNext) {
    gRadioKnobNext = slot;
    gRadioKnobNextSeen = 1;
    return;
  }
  if (++gRadioKnobNextSeen < kRadioKnobSure) return;
  gRadioKnobSlot = slot;
  gRadioKnobNext = -1;
  // Only the player and the visuals show a station to tune. Find, the results and Move use theirs.
  if (gLauncherVisible || (gRadioView != RadioView::Player && gRadioView != RadioView::Visual)) return;
  const int at = gRadioSelection.dialAt(slot);
  if (at < 0 || at == gRadioSelection.stationIndex()) return;
  gRadioSelection.select(at);
  gRadioRestSinceMs = now;
  handleScreensaverWake();  // Turning it is using the Cardputer.
  // A sweep across the dial shouldn't start every station it passes, so the one it stops on plays.
  if (gRadioWantPlaying) {
    gRadioKnobTuning = true;
    gRadioKnobTuneAtMs = now + kRadioKnobRestMs;
  }
  radioInvalidate();
}
void radioKnobTune(uint32_t now) {
  if (!gRadioKnobTuning || int32_t(now - gRadioKnobTuneAtMs) < 0) return;
  gRadioKnobTuning = false;
  const auto* station = gRadioSelection.station();
  if (gRadioWantPlaying && station && !radioPlayingUrl(station->url)) radioPlaySelected();
  radioInvalidate();
}

// --- The strip --------------------------------------------------------------------------------
// LEDs lit for the music's loudness, on the tile meter's log scale stretched to the strip.
int radioStripLevelFor(uint16_t level) {
  if (level < 700) return 0;
  const int lit = int((log2f(float(level)) - 9.5f) * float(lab::kStripLeds) / 5.0f + 0.5f);
  return lit < 1 ? 1 : lit > lab::kStripLeds ? lab::kStripLeds : lit;
}
void radioWriteStrip(uint32_t now) {
  if (!gLabRmt || !labOnWires(lab::Part::Strip) || now - gRadioStripMs < kLabStripGapMs) return;
  gRadioStripMs = now;
  // What changed last shows for a moment: the volume, or the station's place on the dial.
  if (int(gSpeakerVolume) != gRadioStripVolume) {
    gRadioStripVolume = gSpeakerVolume;
    gRadioStripVolumeUntilMs = now + kRadioStripShowMs;
  }
  if (gRadioSelection.stationIndex() != gRadioStripStation) {
    gRadioStripStation = gRadioSelection.stationIndex();
    gRadioStripDialUntilMs = now + kRadioStripShowMs;
  }
  const uint8_t* ink = radioGenreRgb(gRadioSelection.station() ? gRadioSelection.genreName() : "");
  uint8_t r = ink[0], g = ink[1], b = ink[2], lit = 0;
  int dot = -1;
  const bool playing = internet_radio::state() == internet_radio::State::Playing;
  if (int32_t(gRadioStripVolumeUntilMs - now) > 0) {
    lit = uint8_t((int(gSpeakerVolume) * lab::kStripLeds + 127) / 255);
    r = g = b = 200;  // White, so it reads as the volume and not the music.
  } else if (int32_t(gRadioStripDialUntilMs - now) > 0 || gRadioKnobTuning || (gRadioWantPlaying && !playing)) {
    // One LED where the station is along the dial, as a radio's needle, while it changes or tunes.
    const int count = gRadioSelection.dialCount(), slot = gRadioSelection.dialSlot();
    if (slot >= 0) dot = count > 1 ? slot * (lab::kStripLeds - 1) / (count - 1) : 0;
  } else if (playing) {
    // Up at once and down a LED a frame, so it falls like a meter's needle rather than flickering.
    const int want = radioStripLevelFor(internet_radio::level());
    gRadioStripLevel = want >= gRadioStripLevel ? want : gRadioStripLevel - 1;
    lit = uint8_t(gRadioStripLevel);
  }
  if (!playing) gRadioStripLevel = 0;
  // The Lab's cap on the strip's power holds here too.
  const int cap = gLabBook.capPercent > 100 ? 100 : gLabBook.capPercent;
  r = uint8_t(r * cap / 100);
  g = uint8_t(g * cap / 100);
  b = uint8_t(b * cap / 100);
  const uint8_t frame[4] = {uint8_t(dot >= 0 ? 0x80 | dot : lit), r, g, b};
  if (!memcmp(frame, gRadioStripSent, sizeof(frame))) return;
  memcpy(gRadioStripSent, frame, sizeof(frame));
  for (int i = 0; i < lab::kStripLeds; ++i) {
    const bool on = dot >= 0 ? i == dot : i < lit;
    gLabStripRgb[i][0] = on ? r : 0;
    gLabStripRgb[i][1] = on ? g : 0;
    gLabStripRgb[i][2] = on ? b : 0;
  }
  labStripShow();
}

// From the radio's service, every pass, ahead of anything that returns early.
void serviceRadioParts() {
  const bool here = gUiMode == UiMode::Radio && !gRadioModePending && gRadioList;
  if (!here) {
    radioPartsGiveBack();
    return;
  }
  if (!gRadioPartsHeld) {
    if (gRadioPartsTried) return;
    gRadioPartsTried = true;
    radioPartsTake();
    if (!gRadioPartsHeld) return;
  }
  const uint32_t now = millis();
  labReadWires(now);
  radioReadKnob(now);
  radioKnobTune(now);
  radioWriteStrip(now);
}
}  // namespace
