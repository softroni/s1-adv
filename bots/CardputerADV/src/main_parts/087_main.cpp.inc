// Radio Scan, inside Mesh. It opens live on the chat's own channel: every packet the chat's radio
// reads, counted from the first 16 bytes, which aren't encrypted, and the channel's signal, while
// the chat carries on. To sweep the region's band or listen with another preset it borrows the
// radio, and the chat hears nothing until it's back. Nothing here transmits. Starting, stopping and
// the bus handoff are Mesh's, in 068.
namespace {
constexpr uint8_t kScanIrqPin = 4;            // The Cap's DIO1, as wired in mesh_cap_radio.h.
constexpr uint8_t kScanPointsPerService = 6;  // A slice of a pass per loop, so keys and the screen keep up.
constexpr uint32_t kScanFadeMs = 500;         // Peaks fall 1 dB each time, so 2 dB a second.
constexpr uint16_t kScanGiveUp = 24;          // Radio calls failing in a row before saying so.
constexpr int16_t kScanChartLow = -130, kScanChartHigh = -60;
constexpr uint8_t kMeshtasticSyncWord = 0x2B;
constexpr uint16_t kMeshtasticPreamble = 16;

// The channel hash a preset's default public channel puts in every packet header.
uint8_t scanPublicHash(uint8_t preset) {
  libmeshtastic_leaf::MeshChannel channel;
  const uint8_t defaultKey = 1;
  channel.setChannel(&defaultKey, 1, s1_mesh::scanPresetName(preset));
  return channel.getHash();
}

void scanCounted(s1_scan::Scanner& scan, bool ok) {
  const bool gaveUp = scan.failures >= kScanGiveUp;
  scan.failures = ok ? 0 : uint16_t(std::min<uint32_t>(scan.failures + 1U, kScanGiveUp));
  if (!gaveUp && scan.failures >= kScanGiveUp) meshSound(s1_mesh::Sound::Failed);
}

// begin() calibrated the chip for the region's channel, and a sweep stays in that band, so every
// tune below skips RadioLib's recalibration.
bool scanSweepOn(s1_mesh::RadioSession& s) {
  auto& scan = s.scan;
  float start = 0, end = 0;
  s1_mesh::regionBand(s.region, start, end);
  scan.plan = s1_scan::planFor(start, end);
  scan.sweep.reset();
  scan.listening = false;
  int e = s.radio.standby();
  if (e == RADIOLIB_ERR_NONE) e = s.radio.setBandwidth(250.0f);  // One LongFast channel, the sweep's step.
  return e == RADIOLIB_ERR_NONE;
}

bool scanListenOn(s1_mesh::RadioSession& s) {
  auto& scan = s.scan;
  auto& r = s.radio;
  const auto modem = s1_mesh::scanPresetModem(scan.preset);
  scan.listening = true;
  scan.listen.reset();
  scan.strip.reset(millis());  // Its chart and its clock start again with each preset.
  scan.sampleAt = 0;
  scan.publicHash = scanPublicHash(scan.preset);
  int e = r.standby();
  if (e == RADIOLIB_ERR_NONE) e = r.setBandwidth(modem.bw);
  if (e == RADIOLIB_ERR_NONE) e = r.setSpreadingFactor(modem.sf);
  if (e == RADIOLIB_ERR_NONE) e = r.setCodingRate(modem.cr);
  if (e == RADIOLIB_ERR_NONE) e = r.setSyncWord(kMeshtasticSyncWord);
  if (e == RADIOLIB_ERR_NONE) e = r.setPreambleLength(kMeshtasticPreamble);
  if (e == RADIOLIB_ERR_NONE) e = r.setFrequency(s1_mesh::scanPresetFrequency(s.region, scan.preset), true);
  if (e == RADIOLIB_ERR_NONE) e = r.startReceive();
  return e == RADIOLIB_ERR_NONE;
}

// The strongest of three instant readings on one channel, or kNone if the radio didn't answer.
int16_t scanReadPoint(s1_mesh::RadioSession& s, float mhz) {
  auto& r = s.radio;
  if (r.standby() != RADIOLIB_ERR_NONE || r.setFrequency(mhz, true) != RADIOLIB_ERR_NONE ||
      r.startReceive() != RADIOLIB_ERR_NONE) return s1_scan::kNone;
  delayMicroseconds(500);  // Let the receiver settle on the new channel.
  int16_t best = s1_scan::kNone;
  for (int i = 0; i < 3; ++i) {
    const int16_t dbm = s1_scan::dbmFrom(r.getRSSI(false));
    if (dbm != s1_scan::kNone && (best == s1_scan::kNone || dbm > best)) best = dbm;
    delayMicroseconds(150);
  }
  return best;
}

void scanReadPacket(s1_mesh::RadioSession& s, uint32_t now) {
  auto& r = s.radio;
  auto& listen = s.scan.listen;
  uint8_t bytes[libmeshtastic_leaf::MAX_LORA_PAYLOAD_LEN];
  const size_t length = std::min(r.getPacketLength(), sizeof(bytes));
  const float rssi = r.getRSSI(), snr = r.getSNR();
  const int e = r.readData(bytes, sizeof(bytes));
  s1_scan::Heard heard;
  if (e == RADIOLIB_ERR_NONE && s1_scan::describe(bytes, length, now, rssi, snr, s.scan.publicHash, -1, heard)) {
    listen.heard(heard);
    s.scan.strip.note(now, s1_scan::kPacketMark);
    meshPacketTick(heard.rssi);  // A tick a packet, higher the stronger it came in.
    // Heard packets are counted and shown, never read as an instruction.
  } else if (e == RADIOLIB_ERR_NONE || e == RADIOLIB_ERR_CRC_MISMATCH) {
    listen.damagedPacket(now);  // Too short for Meshtastic, or broken on the way.
    s.scan.strip.note(now, s1_scan::kGarbledMark);
  }
  scanCounted(s.scan, (e == RADIOLIB_ERR_NONE || e == RADIOLIB_ERR_CRC_MISMATCH) &&
                          r.startReceive() == RADIOLIB_ERR_NONE);
}

// Only while the radio is borrowed: the sweep, or listening with another preset.
void serviceRadioScan(s1_mesh::RadioSession& s) {
  auto& scan = s.scan;
  const uint32_t now = millis();
  if (scan.failures >= kScanGiveUp) return;
  if (scan.listening) {
    if (digitalRead(kScanIrqPin) == HIGH) scanReadPacket(s, now);
    else if (uint32_t(now - scan.sampleAt) >= s1_scan::kListenSampleMs) {
      // The channel's signal for the live chart. Reading it leaves the radio receiving, and a radio
      // that stops answering reads as nothing, so listening notices that the way the sweep does.
      scan.sampleAt = now;
      const int16_t dbm = s1_scan::dbmFrom(s.radio.getRSSI(false));
      scanCounted(scan, dbm != s1_scan::kNone);
      scan.strip.add(now, dbm);
    }
    scan.strip.advance(now);
    return;
  }
  for (uint8_t i = 0; i < kScanPointsPerService && scan.failures < kScanGiveUp; ++i) {
    const int16_t dbm = scanReadPoint(s, scan.plan.frequency(scan.sweep.next));
    scanCounted(scan, dbm != s1_scan::kNone);
    scan.sweep.record(scan.plan, dbm);
  }
  if (uint32_t(now - scan.fadeAt) >= kScanFadeMs) {
    scan.sweep.fade(scan.plan, 1);
    scan.fadeAt = now;
  }
}

// Radio Scan borrows the chat's radio only to sweep or listen with another preset, and the chat hears
// nothing meanwhile. So it won't while Help repeats or a message is still on its way out.
bool scanLend(s1_mesh::RadioSession& s) {
  if (s.scanning) return true;
  const char* why = nullptr;
  if (s.help.repeating()) why = "Not while Help is on";
  else if (!s.outbox.idle() || s.mesh.isTransmitting() || s.mesh.hasPendingAck()) why = "Wait: a message is going out";
  if (why) {
    gMeshNotice = why;
    gMeshNoticeUntilMs = millis() + 2500;
    meshSound(s1_mesh::Sound::Nope);
    return false;
  }
  s.mesh.lendRadio();
  s.scanning = true;
  s.scan.failures = 0;
  s.scan.listening = false;
  s.scan.fadeAt = millis();
  meshSound(s1_mesh::Sound::Removed);  // Two clicks down: the chat's off for now.
  logf("[MESH] chat paused for Radio Scan");
  return true;
}

// The radio back to the chat, tuned as the library had it. If it won't go back, the radio stops,
// and this says false: nothing may use the session after that.
bool scanReturn(s1_mesh::RadioSession& s) {
  if (!s.scanning) return true;
  s.scanning = false;
  s.scan.listening = false;
  if (!s.mesh.resumeRadio()) {
    logf("[MESH] chat radio did not come back");
    stopMeshMode(false);
    gMeshNotice = "Radio stopped answering";
    gMeshNoticeUntilMs = 0;
    meshSound(s1_mesh::Sound::Failed);
    return false;
  }
  meshSound(s1_mesh::Sound::Saved);  // Two clicks up: the chat's back on.
  logf("[MESH] chat back on");
  return true;
}

// Esc gives a borrowed radio back to the chat. On the chat's own channel, Esc is Mesh's: back to the
// first screen.
bool handleRadioScanEscape(s1_mesh::RadioSession& s) {
  if (!s.scanning) return false;
  scanReturn(s);
  return true;
}

// On the chat's channel, Enter sweeps the band and Up/Down listen with another preset, both on the
// borrowed radio. Enter in the sweep, or Up/Down round to LongFast again, give it back.
bool handleRadioScanKey(char key, const Keyboard_Class::KeysState& keys) {
  if (!gMeshSession) return false;
  auto& s = *gMeshSession;
  auto& scan = s.scan;
  if (s.scanning && scan.failures >= kScanGiveUp) return true;  // Only Esc, which gives it back.
  if (keys.enter || key == '\r' || key == '\n') {
    if (s.scanning && !scan.listening) {
      scanReturn(s);
      return true;
    }
    if (!scanLend(s)) return true;
    scanCounted(scan, scanSweepOn(s));
    logf("[MESH] scan sweep %s %.3f-%.3f MHz points=%u", s1_mesh::regionName(s.region), double(scan.plan.start),
         double(scan.plan.end()), unsigned(scan.plan.points));
    return true;
  }
  const bool back = key == 1 || key == ';' || key == ',', ahead = key == 2 || key == '.' || key == '/';
  if (!back && !ahead) return true;
  if (s.scanning && !scan.listening) return true;  // The sweep has no presets.
  const uint8_t at = s.scanning ? scan.preset : 0;  // 0 is LongFast, the chat's own.
  const uint8_t next = uint8_t((at + (back ? s1_mesh::kScanPresetCount - 1 : 1)) % s1_mesh::kScanPresetCount);
  if (!next) {
    scanReturn(s);
    return true;
  }
  if (!scanLend(s)) return true;
  scan.preset = next;
  scanCounted(scan, scanListenOn(s));
  return true;
}

// While the chat has the radio: its channel's signal for the live view, read between the library's
// own work. Nothing is read while it sends, when a reading would only be its own signal.
void serviceRadioLive(s1_mesh::RadioSession& s, uint32_t now) {
  if (!s.mesh.isTransmitting() && uint32_t(now - s.sampleAt) >= s1_scan::kListenSampleMs) {
    s.sampleAt = now;
    const int16_t dbm = s1_scan::dbmFrom(s.radio.getRSSI(false));
    if (dbm != s1_scan::kNone) s.line.add(now, dbm);
  }
  s.line.advance(now);
}

bool meshLiveShowing() {
  return gUiMode == UiMode::Mesh && gMeshView == MeshView::Live && !gMeshNaming && !gMeshChannelSetup;
}

// Every packet the chat's radio reads, readable or not, counted for the live view from its header
// alone. The chat reads the ones it can by itself; nothing here is ever acted on.
void meshHeard(const uint8_t* bytes, size_t length, float rssi, float snr) {
  if (!gMeshSession) return;
  auto& s = *gMeshSession;
  const uint32_t now = millis();
  const int16_t privateHash = s.mesh.hasSecondaryChannel() ? int16_t(s.mesh.getSecondaryChannelHash()) : int16_t(-1);
  s1_scan::Heard heard;
  if (length && s1_scan::describe(bytes, length, now, rssi, snr, s.mesh.getChannelHash(), privateHash, heard)) {
    s.heard.heard(heard);
    s.line.note(now, s1_scan::kPacketMark);
    if (meshLiveShowing()) meshPacketTick(heard.rssi);
  } else {
    s.heard.damagedPacket(now);
    s.line.note(now, s1_scan::kGarbledMark);
  }
}

void logRadioScanStatus(s1_mesh::RadioSession& s) {
  const auto& scan = s.scan;
  const uint8_t top = scan.sweep.strongest(scan.plan);
  logf("[MESH] scan lent=%d listening=%d preset=%s passes=%lu floor=%d top=%d top_mhz=%.3f packets=%lu nodes=%u "
       "damaged=%lu failures=%u",
       s.scanning, scan.listening, s1_mesh::scanPresetName(scan.preset), static_cast<unsigned long>(scan.sweep.passes),
       int(scan.sweep.floor(scan.plan)), top < scan.plan.points ? int(scan.sweep.peak[top]) : 0,
       top < scan.plan.points ? double(scan.plan.frequency(top)) : 0.0,
       static_cast<unsigned long>(scan.listen.packets), unsigned(scan.listen.nodeCount),
       static_cast<unsigned long>(scan.listen.damaged), unsigned(scan.failures));
  logf("[MESH] live packets=%lu nodes=%u damaged=%lu noise=%d", static_cast<unsigned long>(s.heard.packets),
       unsigned(s.heard.nodeCount), static_cast<unsigned long>(s.heard.damaged), int(s.line.noise()));
}

int scanChartY(int16_t dbm, int top, int bottom) {
  const int clamped = std::max<int>(kScanChartLow, std::min<int>(kScanChartHigh, dbm));
  return bottom - (clamped - kScanChartLow) * (bottom - top) / (kScanChartHigh - kScanChartLow);
}

void renderScanSweep(s1_mesh::RadioSession& s, const MeshInk& ink) {
  auto& d = gCanvas;
  const auto& plan = s.scan.plan;
  const auto& sweep = s.scan.sweep;
  char text[64];
  snprintf(text, sizeof(text), "%.1f-%.1f MHz", double(plan.start), double(plan.end()));
  meshText(8, kMeshTop, text, ink.muted);
  const int after = 16 + meshWidth(text);
  meshTextFit(after, kMeshTop, "| LongFast", 232 - after, ink.orange);
  constexpr int kLeft = 8, kWidth = 224, kTop = 40, kBottom = 92;
  for (int16_t dbm : {int16_t(-120), int16_t(-100), int16_t(-80)})
    d.drawFastHLine(kLeft, scanChartY(dbm, kTop, kBottom), kWidth, rgb565_local(28, 36, 44));
  const int points = std::max<int>(1, plan.points);
  const int bar = std::max(1, kWidth / points), left = kLeft + (kWidth - bar * points) / 2;
  const uint16_t fill = rgb565_local(30, 150, 175), peakInk = TFT_WHITE;
  for (int i = 0; i < plan.points; ++i) {
    const int x = left + i * bar, w = bar > 2 ? bar - 1 : bar;
    if (sweep.now[i] != s1_scan::kNone) {
      const int y = scanChartY(sweep.now[i], kTop, kBottom);
      d.fillRect(x, y, w, kBottom - y + 1, fill);
    }
    if (sweep.peak[i] != s1_scan::kNone) d.drawFastHLine(x, scanChartY(sweep.peak[i], kTop, kBottom), w, peakInk);
  }
  const uint8_t channel = plan.pointAt(s1_mesh::regionFrequency(s.region));
  if (channel < plan.points) d.fillRect(left + channel * bar, kBottom + 3, std::max(2, bar), 4, ink.orange);
  const int16_t floor = sweep.floor(plan);
  const uint8_t top = sweep.strongest(plan);
  // The floor on the left, the strongest on the right.
  if (floor == s1_scan::kNone || top >= plan.points) meshText(8, 100, "Reading the band...", ink.white);
  else {
    char strongest[32];
    snprintf(strongest, sizeof(strongest), "Top %d at %.3f", int(sweep.peak[top]), double(plan.frequency(top)));
    // The floor gives up its unit before the two crowd each other.
    snprintf(text, sizeof(text), "Floor %d dBm", int(floor));
    if (meshWidth(text) + meshWidth(strongest) + 12 > 224) snprintf(text, sizeof(text), "Floor %d", int(floor));
    meshText(8, 100, text, ink.muted);
    meshTextRight(232, 100, strongest, ink.white);
  }
  meshFoot("Enter or Esc: back to the chat");
}

// How long it has listened: "4:12", or "1:04:12" after an hour.
void scanClock(uint32_t ms, char* out, size_t size) {
  const unsigned long seconds = ms / 1000;
  if (seconds < 3600) snprintf(out, size, "%lu:%02lu", seconds / 60, seconds % 60);
  else snprintf(out, size, "%lu:%02lu:%02lu", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

// Listening's live chart, on the sweep's scale. A white line the chart's height shows where it's
// writing now and moves right, with a gap ahead of it, so the chart moves even on a quiet channel. A
// packet that came in marks its column green, a garbled one orange: at the top of a tall chart, the
// whole column of a short one.
void scanDrawStrip(const s1_scan::Strip& strip, int top, int bottom, const MeshInk& ink) {
  auto& d = gCanvas;
  constexpr int kLeft = 8, kBar = 2;
  const bool tall = bottom - top >= 24;
  if (tall)
    for (int16_t dbm : {int16_t(-120), int16_t(-100), int16_t(-80)})
      d.drawFastHLine(kLeft, scanChartY(dbm, top, bottom), kBar * s1_scan::kStripColumns, rgb565_local(28, 36, 44));
  const uint16_t fill = rgb565_local(30, 150, 175);
  for (uint8_t i = 0; i < s1_scan::kStripColumns; ++i) {
    if (!strip.shown(i)) continue;
    const int x = kLeft + i * kBar;
    if (strip.peak[i] != s1_scan::kNone) {
      const int y = scanChartY(strip.peak[i], top, bottom);
      d.fillRect(x, y, kBar, bottom - y + 1, fill);
    }
    if (strip.mark[i]) {
      const uint16_t mark = strip.mark[i] == s1_scan::kPacketMark ? ink.green : ink.orange;
      if (tall) d.fillRect(x, top, kBar, 4, mark);
      else d.fillRect(x, top, kBar, bottom - top + 1, mark);
    }
  }
  d.drawFastVLine(kLeft + strip.head * kBar + kBar - 1, top, bottom - top + 1, TFT_WHITE);
}

// Listening, on the chat's channel or another preset's: how long, what came, the noise, the chart and
// the latest packets.
void renderScanListen(const s1_scan::Listen& listen, const s1_scan::Strip& strip, const char* preset, double mhz,
                      const MeshInk& ink) {
  auto& d = gCanvas;
  const uint32_t now = millis();
  char text[64], clock[16];
  // The preset and its channel, and on the right how long it's listened there.
  scanClock(now - strip.since, clock, sizeof(clock));
  const int clockWidth = meshWidth(clock);
  snprintf(text, sizeof(text), "%s  %.3f MHz", preset, mhz);
  if (meshWidth(text) + clockWidth + 8 > 224) snprintf(text, sizeof(text), "%s  %.3f", preset, mhz);
  meshTextFit(8, kMeshTop, text, 224 - clockWidth - 8, ink.muted);
  meshTextRight(232, kMeshTop, clock, ink.white);
  if (!listen.packets) meshBoldFit(8, 38, "Listening...", 224, ink.white);
  else {
    constexpr uint32_t kMostShown = 99999;  // Keeps the line on the screen however long it listens.
    snprintf(text, sizeof(text), "%lu%s packet%s, %u%s node%s",
             static_cast<unsigned long>(std::min(listen.packets, kMostShown)), listen.packets > kMostShown ? "+" : "",
             listen.packets == 1 ? "" : "s", unsigned(listen.nodeCount), listen.moreNodes ? "+" : "",
             listen.nodeCount == 1 && !listen.moreNodes ? "" : "s");
    meshBoldFit(8, 38, text, 224, ink.white);
  }
  // The channel's noise on the left, and on the right what it has heard and when, shortened to fit.
  const int16_t noise = strip.noise();
  const unsigned long garbled = listen.damaged;
  char left[24], right[48], ago[16];
  if (noise == s1_scan::kNone) snprintf(left, sizeof(left), "Noise ...");
  else snprintf(left, sizeof(left), "Noise %d dBm", int(noise));
  const uint32_t seconds = (now - listen.lastAt) / 1000;
  if (seconds < 600) snprintf(ago, sizeof(ago), "%lus", static_cast<unsigned long>(seconds));
  else snprintf(ago, sizeof(ago), "%lum", static_cast<unsigned long>(seconds / 60));
  if (!listen.packets && !garbled) snprintf(right, sizeof(right), "Nothing yet");
  else if (garbled) snprintf(right, sizeof(right), "%lu garbled  last %s ago", garbled, ago);
  else snprintf(right, sizeof(right), "last %s ago", ago);
  const auto fits = [&] { return meshWidth(left) + meshWidth(right) + 10 <= 224; };
  if (!fits() && noise != s1_scan::kNone) snprintf(left, sizeof(left), "Noise %d", int(noise));
  if (!fits() && garbled) snprintf(right, sizeof(right), "%lu garbled  %s ago", garbled, ago);
  if (!fits()) snprintf(right, sizeof(right), "%s ago", ago);
  meshText(8, 57, left, ink.muted);
  meshTextRight(232, 57, right, ink.muted);
  // Before any packet the chart has the room; after, a short one sits over the latest three.
  if (!listen.recentCount) {
    scanDrawStrip(strip, 76, 113, ink);
  } else {
    scanDrawStrip(strip, 75, 84, ink);
    // The packets themselves stay in Font0: five columns don't fit in anything bigger.
    d.setFont(&fonts::Font0);
    for (size_t i = 0; i < std::min<size_t>(listen.recentCount, 3); ++i) {
      const auto& h = listen.recent[i];
      const int y = 88 + int(i) * 11;
      d.setTextColor(TFT_WHITE, ink.bg); d.setCursor(8, y);
      snprintf(text, sizeof(text), "!%08lx", static_cast<unsigned long>(h.from)); d.print(text);
      if (h.rssi == s1_scan::kNone) snprintf(text, sizeof(text), "?dBm");
      else snprintf(text, sizeof(text), "%ddBm", int(h.rssi));
      d.setTextColor(ink.muted, ink.bg); d.setCursor(108 - d.textWidth(text), y); d.print(text);
      snprintf(text, sizeof(text), "%+.1fdB", double(h.snrTenths) / 10.0); d.setCursor(114, y); d.print(text);
      if (h.hops < 0) snprintf(text, sizeof(text), "hops ?");
      else if (h.hops == 0) snprintf(text, sizeof(text), "direct");
      else snprintf(text, sizeof(text), "%d hop%s", int(h.hops), h.hops == 1 ? "" : "s");
      d.setCursor(160, y); d.print(text);
      // Its channel: the public one, the private one this Cardputer shares, or another.
      d.setTextColor(h.publicChannel ? ink.green : h.privateChannel ? ink.amber : ink.muted, ink.bg);
      d.setCursor(200, y);
      d.print(h.publicChannel ? "pub" : h.privateChannel ? "priv" : "other");
    }
  }
  meshFoot("Enter sweep  \x18\x19 preset  Esc back");
}

// Live on the chat's channel while the chat runs; borrowed, the bar turns red and says the chat is
// paused, since it hears nothing until Esc gives the radio back.
void renderRadioScanUi(s1_mesh::RadioSession& s) {
  MeshInk ink;
  if (!s.scanning) {
    meshHead("Radio Scan", ink.accent, String(s1_mesh::regionName(s.region)) + " live", ink.muted);
    renderScanListen(s.heard, s.line, s1_mesh::scanPresetName(0), double(s1_mesh::regionFrequency(s.region)), ink);
    return;
  }
  meshHead("Radio Scan", ink.red, "Chat paused", ink.red);
  if (s.scan.failures >= kScanGiveUp) {
    meshBoldCenter(44, "Radio not answering", ink.white);
    meshTextCenter(68, "Esc, check the Cap, then try again", ink.muted);
    meshKeys("Esc back", ink.muted);
    return;
  }
  if (s.scan.listening)
    renderScanListen(s.scan.listen, s.scan.strip, s1_mesh::scanPresetName(s.scan.preset),
                     double(s1_mesh::scanPresetFrequency(s.region, s.scan.preset)), ink);
  else renderScanSweep(s, ink);
}
}  // namespace
