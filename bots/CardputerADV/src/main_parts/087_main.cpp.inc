// Radio Scan, inside Mesh: the Cap's radio, listening only. A sweep reads signal strength across
// the region's band; listening tunes one preset's channel and counts the Meshtastic packets it
// hears, which works without the channel's key because the first 16 bytes aren't encrypted.
// Nothing here transmits. Starting, stopping and the bus handoff are Mesh's, in 068.
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
  scan.failures = ok ? 0 : uint16_t(std::min<uint32_t>(scan.failures + 1U, kScanGiveUp));
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
  s1_scan::Header header;
  if (e == RADIOLIB_ERR_NONE && s1_scan::parseHeader(bytes, length, header)) {
    s1_scan::Heard heard;
    heard.from = header.from;
    heard.at = now;
    heard.rssi = s1_scan::dbmFrom(rssi);
    heard.snrTenths = std::isfinite(snr) ? int16_t(lroundf(std::max(-99.9f, std::min(99.9f, snr)) * 10.0f)) : 0;
    heard.hops = s1_scan::hopsTaken(header);
    heard.publicChannel = header.channel == s.scan.publicHash;
    listen.heard(heard);
    // Heard packets are counted and shown, never read as an instruction.
  } else if (e == RADIOLIB_ERR_NONE || e == RADIOLIB_ERR_CRC_MISMATCH) {
    listen.damagedPacket(now);  // Too short for Meshtastic, or broken on the way.
  }
  scanCounted(s.scan, (e == RADIOLIB_ERR_NONE || e == RADIOLIB_ERR_CRC_MISMATCH) &&
                          r.startReceive() == RADIOLIB_ERR_NONE);
}

void serviceRadioScan(s1_mesh::RadioSession& s) {
  auto& scan = s.scan;
  const uint32_t now = millis();
  if (!scan.started) {
    scan.started = true;
    scan.fadeAt = now;
    scanCounted(scan, scanSweepOn(s));
    logf("[MESH] scan ready %s %.3f-%.3f MHz points=%u heap=%u largest=%u", s1_mesh::regionName(s.region),
         double(scan.plan.start), double(scan.plan.end()), unsigned(scan.plan.points), unsigned(s.lowHeap),
         unsigned(s.lowLargest));
  }
  if (scan.failures >= kScanGiveUp) return;
  if (scan.listening) {
    if (digitalRead(kScanIrqPin) == HIGH) scanReadPacket(s, now);
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

bool handleRadioScanKey(char key, const Keyboard_Class::KeysState& keys) {
  if (!gMeshSession) return false;
  auto& s = *gMeshSession;
  auto& scan = s.scan;
  if (scan.failures >= kScanGiveUp) return true;
  if (keys.enter || key == '\r' || key == '\n') {
    scanCounted(scan, scan.listening ? scanSweepOn(s) : scanListenOn(s));
    return true;
  }
  const bool back = key == 1 || key == ';' || key == ',', ahead = key == 2 || key == '.' || key == '/';
  if (scan.listening && (back || ahead)) {
    scan.preset = uint8_t((scan.preset + (back ? s1_mesh::kScanPresetCount - 1 : 1)) % s1_mesh::kScanPresetCount);
    scanCounted(scan, scanListenOn(s));
  }
  return true;
}

void logRadioScanStatus(s1_mesh::RadioSession& s) {
  const auto& scan = s.scan;
  const uint8_t top = scan.sweep.strongest(scan.plan);
  logf("[MESH] scan listening=%d preset=%s passes=%lu floor=%d top=%d top_mhz=%.3f packets=%lu nodes=%u "
       "damaged=%lu failures=%u",
       scan.listening, s1_mesh::scanPresetName(scan.preset), static_cast<unsigned long>(scan.sweep.passes),
       int(scan.sweep.floor(scan.plan)), top < scan.plan.points ? int(scan.sweep.peak[top]) : 0,
       top < scan.plan.points ? double(scan.plan.frequency(top)) : 0.0,
       static_cast<unsigned long>(scan.listen.packets), unsigned(scan.listen.nodeCount),
       static_cast<unsigned long>(scan.listen.damaged), unsigned(scan.failures));
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
  meshKeys("Enter listen for packets  Esc stop", ink.muted);
}

void renderScanListen(s1_mesh::RadioSession& s, const MeshInk& ink) {
  auto& d = gCanvas;
  const auto& listen = s.scan.listen;
  char text[64];
  snprintf(text, sizeof(text), "%s  %.3f MHz", s1_mesh::scanPresetName(s.scan.preset),
           double(s1_mesh::scanPresetFrequency(s.region, s.scan.preset)));
  meshTextFit(8, kMeshTop, text, 224, ink.muted);
  if (!listen.packets) meshBoldFit(8, 38, "Listening...", 224, ink.white);
  else {
    constexpr uint32_t kMostShown = 99999;  // Keeps the line on the screen however long it listens.
    snprintf(text, sizeof(text), "%lu%s packet%s, %u%s node%s",
             static_cast<unsigned long>(std::min(listen.packets, kMostShown)), listen.packets > kMostShown ? "+" : "",
             listen.packets == 1 ? "" : "s", unsigned(listen.nodeCount), listen.moreNodes ? "+" : "",
             listen.nodeCount == 1 && !listen.moreNodes ? "" : "s");
    meshBoldFit(8, 38, text, 224, ink.white);
  }
  if (!listen.packets && !listen.damaged) snprintf(text, sizeof(text), "Nothing yet on this preset");
  else {
    const uint32_t ago = (millis() - listen.lastAt) / 1000;
    if (ago < 600) snprintf(text, sizeof(text), "%lu garbled  last %lus ago", static_cast<unsigned long>(listen.damaged),
                            static_cast<unsigned long>(ago));
    else snprintf(text, sizeof(text), "%lu garbled  last %lum ago", static_cast<unsigned long>(listen.damaged),
                  static_cast<unsigned long>(ago / 60));
  }
  meshTextFit(8, 57, text, 224, ink.muted);
  // The packets themselves stay in Font0: five columns don't fit in anything bigger.
  d.setFont(&fonts::Font0);
  for (size_t i = 0; i < listen.recentCount; ++i) {
    const auto& h = listen.recent[i];
    const int y = 75 + int(i) * 11;
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
    d.setTextColor(h.publicChannel ? ink.green : ink.muted, ink.bg); d.setCursor(200, y); d.print(h.publicChannel ? "pub" : "other");
  }
  meshKeys("Enter sweep  \x18\x19 preset  Esc stop", ink.muted);
}

void renderRadioScanUi(s1_mesh::RadioSession& s) {
  MeshInk ink;
  const String tag = String(s1_mesh::regionName(s.region)) + (s.scan.listening ? " listen" : " sweep");
  meshHead("Radio Scan", ink.accent, tag, ink.muted);
  if (s.scan.failures >= kScanGiveUp) {
    meshBoldCenter(44, "Radio not answering", ink.white);
    meshTextCenter(68, "Esc, check the Cap, then try again", ink.muted);
    meshKeys("Esc stop", ink.muted);
    return;
  }
  if (s.scan.listening) renderScanListen(s, ink);
  else renderScanSweep(s, ink);
}
}  // namespace
