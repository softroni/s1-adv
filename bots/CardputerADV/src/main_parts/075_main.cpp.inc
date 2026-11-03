// NFC Notes for the M5Stack NFC unit (ST25R3916) on the Grove port. The bus is only held
// while the app is open, so a different unit can use the port the rest of the time.
namespace {
using m5::nfc::a::PICC;
constexpr uint8_t kNfcSdaPin = 2;   // Grove yellow.
constexpr uint8_t kNfcSclPin = 1;   // Grove white.
constexpr uint32_t kNfcI2cHz = 400000U;
constexpr uint32_t kNfcDetectMs = 15;
constexpr uint32_t kNfcTagPowerUpMs = 6;
constexpr uint8_t kNfcClassicStartBlock = 4;

m5::unit::UnitUnified gNfcUnits;
m5::unit::UnitNFC gNfcUnit{};
m5::nfc::NFCLayerA gNfcLayer{gNfcUnit};

// Nothing here may call the library's format functions: on Ultralight and NTAG they change
// one-time bits for good, and on DESFire they erase the card.
class NfcRadio {
 public:
  bool begin() {
    openBus();
    if (!mAdded) mAdded = gNfcUnits.add(gNfcUnit, Wire);
    return mAdded && gNfcUnits.begin();
  }
  bool busOpen() const { return mBusOpen; }  // It closes from its service, a pass after its screen does.
  void close() {
    if (!mBusOpen) return;
    if (mAdded) gNfcUnit.disableField();
    Wire.end();
    mBusOpen = false;
  }
  bool fieldOn() {
    gNfcUnits.update();
    if (!gNfcUnit.enableField()) return false;
    delay(kNfcTagPowerUpMs);
    return true;
  }
  void fieldOff() { gNfcUnit.disableField(); }
  bool detect(std::string& uid, std::string& type) {
    mPicc = PICC{};
    if (!gNfcLayer.detect(mPicc, kNfcDetectMs)) return false;
    uid = mPicc.uidAsString();
    type = mPicc.typeAsString();
    return true;
  }
  bool activate() {
    if (gNfcLayer.identify(mPicc) && gNfcLayer.reactivate(mPicc)) return true;
    gNfcLayer.deactivate();
    return false;
  }
  void release() { gNfcLayer.deactivate(); }
  bool supportsNdef() const { return mPicc.supportsNDEF(); }
  bool isClassic() const { return mPicc.isMifareClassic(); }
  size_t capacity() const { return mPicc.userAreaSize(); }
  bool ndefFormatted(bool& formatted) { return gNfcLayer.ndefIsValidFormat(formatted); }
  bool ndefRecords(std::vector<nfc_notes::Record>& records) {
    records.clear();
    m5::nfc::ndef::TLV message{};
    if (!gNfcLayer.ndefRead(message)) return false;
    if (!message.isMessageTLV()) return true;
    for (auto&& record : message.records()) {
      nfc_notes::Record out;
      if (record.tnf() == m5::nfc::ndef::TNF::Wellknown) {
        const std::string type = record.type();
        if (type == "T" || type == "U") out.kind = type[0];
      }
      out.text = record.payloadAsString();
      records.push_back(out);
    }
    return true;
  }
  bool ndefWriteText(const std::string& text) {
    m5::nfc::ndef::TLV message{m5::nfc::ndef::Tag::Message};
    m5::nfc::ndef::Record record{};
    if (!record.setTextPayload(text.c_str(), "en")) return false;
    message.push_back(record);
    return gNfcLayer.ndefWrite(message);
  }
  bool classicRead(uint8_t* data, size_t length) {
    uint16_t got = static_cast<uint16_t>(length);
    return gNfcLayer.read(data, got, kNfcClassicStartBlock, m5::nfc::a::mifare::classic::DEFAULT_KEY) &&
           got == length;
  }
  bool classicWrite(const uint8_t* data, size_t length) {
    return gNfcLayer.write(kNfcClassicStartBlock, data, static_cast<uint16_t>(length),
                           m5::nfc::a::mifare::classic::DEFAULT_KEY);
  }

 private:
  void openBus() {
    if (mBusOpen) return;
    Wire.begin(kNfcSdaPin, kNfcSclPin, kNfcI2cHz);
    mBusOpen = true;
  }
  PICC mPicc{};
  bool mBusOpen = false;
  bool mAdded = false;
};

NfcRadio gNfcRadio;
nfc_notes::App gNfc;

void nfcRedraw() {
  gLastRenderMs = 0;
  gUiFramePolicy.invalidate();
}
void nfcSettle() {
  if (gNfc.beep != nfc_notes::Beep::None) {
    if (gNfc.beep == nfc_notes::Beep::Good) focusTone(2093, 70);
    else focusTone(330, 160);
    gNfc.beep = nfc_notes::Beep::None;
  }
  if (gNfc.changed) {
    gNfc.changed = false;
    nfcRedraw();
  }
}
// Chat shares this font and these text sizes, so a size set for chat works here too.
void nfcNoteFont() {
  gCanvas.setFont(&fonts::efontCN_12);
  gCanvas.setTextSize(chatTextScale());
}
size_t nfcNoteColumns() {
  return static_cast<size_t>(224.0f / (6.0f * chatTextScale()));
}
int nfcLineHeight() {
  return static_cast<int>(13.0f * chatTextScale() + 0.5f);
}
std::vector<std::string> nfcNoteLines() {
  return nfc_notes::wrapText(nfc_notes::printable(gNfc.shown.text), nfcNoteColumns());
}
constexpr int kNfcNoteTop = 21;
constexpr int kNfcNoteBottom = 86;
size_t nfcVisibleLines() {
  return static_cast<size_t>(max(1, (kNfcNoteBottom - kNfcNoteTop) / nfcLineHeight()));
}

void nfcDrawLines(const std::vector<std::string>& lines, size_t first, uint16_t color, uint16_t bg) {
  nfcNoteFont();
  gCanvas.setTextColor(color, bg);
  const size_t last = min(lines.size(), first + nfcVisibleLines());
  for (size_t i = first; i < last; ++i) {
    gCanvas.setCursor(6, kNfcNoteTop + static_cast<int>(i - first) * nfcLineHeight());
    gCanvas.print(lines[i].c_str());
  }
}
void beginNfcNotes() {
  gNfc.reset();
  nfcSettle();
}
void serviceNfcNotes() {
  if (gUiMode != UiMode::NfcNotes) {
    if (gNfc.unit != nfc_notes::Unit::Closed) {
      gNfcRadio.close();
      gNfc.reset();
    }
    return;
  }
  if (gLauncherVisible) {
    // A pending save must not wait behind a menu that hides it.
    gNfc.disarm();
    nfcSettle();
    return;
  }
  // A poll stops the main loop for a moment, and the mic and the speaker run on that loop.
  if (gRecording || gRealtimeActive || gPlaybackActive) return;
  nfc_notes::poll(gNfc, gNfcRadio, millis());
  nfcSettle();
}
bool cancelNfcNotes() {
  const bool undone = gNfc.cancel();
  nfcSettle();
  return undone;
}
bool handleNfcNotesKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || keys.opt) return true;
  if (key == char(1) || key == char(2)) {
    gNfc.scrollBy(key == char(1) ? -1 : 1, nfcNoteLines().size(), nfcVisibleLines());
  } else if (keys.enter || key == '\r' || key == '\n') {
    gNfc.arm(millis());
  } else if (keys.del || key == '\b') {
    gNfc.backspace();
  } else {
    gNfc.typeKey(key);
  }
  nfcSettle();
  return true;
}

void renderNfcNotesUi() {
  auto& d = gCanvas;
  const uint16_t bg = rgb565_local(4, 10, 18);
  const uint16_t cyan = rgb565_local(47, 227, 255);
  const uint16_t green = rgb565_local(88, 240, 141);
  const uint16_t amber = rgb565_local(255, 176, 64);
  const uint16_t red = rgb565_local(255, 110, 92);
  const uint16_t muted = rgb565_local(156, 185, 203);
  const uint16_t input = rgb565_local(14, 30, 44);

  d.fillScreen(bg);
  d.setTextWrap(false);
  d.setFont(&fonts::Font2);
  d.setTextSize(1);
  d.setTextColor(cyan, bg);
  d.setCursor(6, 2);
  d.print("NFC NOTES");

  d.setFont(&fonts::Font0);
  const bool ready = gNfc.unit == nfc_notes::Unit::Ready;
  String state = !ready ? "No NFC unit" : gNfc.tagHere ? "Tag here" : "Ready";
  if (ready && !gNfc.tagHere && !gNfc.type.empty()) state = "Tag gone";
  d.setTextColor(ready ? green : red, bg);
  d.setCursor(234 - d.textWidth(state), 6);
  d.print(state);
  d.drawFastHLine(6, 18, 228, rgb565_local(40, 58, 74));

  if (gNfc.unit == nfc_notes::Unit::Missing) {
    nfcDrawLines(nfc_notes::wrapText("Plug in the NFC unit", nfcNoteColumns()), 0, TFT_WHITE, bg);
  } else if (gNfc.armed) {
    d.fillRoundRect(6, 24, 228, 48, 8, amber);
    d.setFont(&fonts::Font2);
    d.setTextSize(1);
    d.setTextColor(TFT_BLACK, amber);
    d.setCursor(16, 31);
    d.print("Hold a tag to save");
    d.setFont(&fonts::Font0);
    d.setCursor(16, 54);
    d.print("Replaces what's there. Esc cancels.");
  } else {
    if (gNfc.shown.found == nfc_notes::Found::Note || gNfc.shown.found == nfc_notes::Found::Link) {
      nfcDrawLines(nfcNoteLines(), gNfc.scroll, TFT_WHITE, bg);
    } else if (gNfc.shown.found == nfc_notes::Found::Nothing) {
      nfcDrawLines(nfc_notes::wrapText("Hold a tag on the unit to read it.", nfcNoteColumns()), 0, muted, bg);
    }
  }

  // Which tag, then what just happened.
  d.setFont(&fonts::Font0);
  d.setTextSize(1);
  if (!gNfc.type.empty()) {
    d.setTextColor(muted, bg);
    d.setCursor(6, 88);
    d.print(fitCurrentFontToWidth(String(gNfc.type.c_str()) + "  " + gNfc.uid.c_str(), 228));
  }
  d.setTextColor(gNfc.mood == nfc_notes::Mood::Good ? green
                 : gNfc.mood == nfc_notes::Mood::Bad ? red : muted, bg);
  d.setCursor(6, 99);
  d.print(fitCurrentFontToWidth(String(gNfc.message.c_str()), 228));

  // The input keeps its size at every text setting, so it can't push the note off screen.
  d.fillRoundRect(4, 110, 232, 23, 6, input);
  d.setFont(&fonts::efontCN_12);
  d.setTextSize(1);
  d.setCursor(10, 115);
  if (gNfc.draft.empty()) {
    d.setTextColor(muted, input);
    d.print(ready ? "Type a name or note" : "");
  } else {
    d.setTextColor(TFT_WHITE, input);
    const size_t room = 36;
    const size_t from = gNfc.draft.size() > room ? gNfc.draft.size() - room : 0;
    d.print(gNfc.draft.c_str() + from);
    if (!gNfc.armed) d.print("_");
  }
  d.setTextSize(1);
  renderLauncherOverlay();
  renderDebugOverlay();
}
} // namespace
