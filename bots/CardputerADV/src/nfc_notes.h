#pragma once
// NFC Notes: read a tag and save a short note on it. Everything here is decisions, not
// radio: the hardware sits behind a small Radio class, so the whole app runs on a computer.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nfc_notes {

constexpr size_t kMaxNote = 200;
constexpr uint32_t kPollMs = 350;
constexpr uint32_t kRetryUnitMs = 2000;
// A tag that wanders past an hour later must not be overwritten.
constexpr uint32_t kArmTimeoutMs = 30000;
// One missed poll can be a tag at the edge of range, not a tag that has gone.
constexpr uint8_t kMissesUntilGone = 2;
// T, L, record header, type, status, "en", terminator.
constexpr size_t kNdefOverhead = 10;

// MIFARE Classic has no standard place for text, so a note goes in the data blocks from
// block 4, behind this header. Blocks 0 to 3 hold the maker's data and are left alone.
constexpr uint8_t kClassicMagic[4] = {'S', '1', 'N', 'T'};
constexpr size_t kClassicHeader = sizeof(kClassicMagic) + 1;
constexpr size_t kClassicBlock = 16;

inline size_t classicFrameSize(size_t textLength) {
  const size_t used = kClassicHeader + textLength;
  return (used + kClassicBlock - 1) / kClassicBlock * kClassicBlock;
}
inline std::vector<uint8_t> encodeClassic(const std::string& text) {
  std::vector<uint8_t> frame(classicFrameSize(text.size()), 0);
  std::memcpy(frame.data(), kClassicMagic, sizeof(kClassicMagic));
  frame[sizeof(kClassicMagic)] = static_cast<uint8_t>(text.size());
  std::memcpy(frame.data() + kClassicHeader, text.data(), text.size());
  return frame;
}
inline bool isClassicNote(const uint8_t* data, size_t length) {
  return length >= kClassicHeader && std::memcmp(data, kClassicMagic, sizeof(kClassicMagic)) == 0 &&
         data[sizeof(kClassicMagic)] > 0 && data[sizeof(kClassicMagic)] <= kMaxNote;
}
inline bool isBlank(const uint8_t* data, size_t length) {
  return std::all_of(data, data + length, [](uint8_t byte) { return byte == 0; });
}

enum class Found { Nothing, Note, Link, Blank, OtherData, Unformatted, Private, Unsupported, Failed };
enum class Saved { Ok, TooLong, Unformatted, Unsupported, OtherData, Private, Failed, NotVerified };

struct Content {
  Found found = Found::Nothing;
  std::string text;
};
struct SaveResult {
  Saved saved = Saved::Failed;
  size_t limit = 0;
};
struct Record {
  char kind = 0;  // 'T' text, 'U' link, 0 anything else.
  std::string text;
};

inline Content describeRecords(const std::vector<Record>& records) {
  if (records.empty()) return {Found::Blank, ""};
  for (const auto& record : records) {
    if (record.kind == 'T') return {Found::Note, record.text};
  }
  for (const auto& record : records) {
    if (record.kind == 'U') return {Found::Link, record.text};
  }
  return {Found::OtherData, ""};
}

template <class Radio>
Content readClassic(Radio& radio) {
  uint8_t first[kClassicBlock] = {};
  if (!radio.classicRead(first, sizeof(first))) return {Found::Private, ""};
  if (isBlank(first, sizeof(first))) return {Found::Blank, ""};
  if (!isClassicNote(first, sizeof(first))) return {Found::OtherData, ""};
  const size_t length = first[sizeof(kClassicMagic)];
  std::vector<uint8_t> frame(classicFrameSize(length));
  if (frame.size() == kClassicBlock) std::memcpy(frame.data(), first, kClassicBlock);
  else if (!radio.classicRead(frame.data(), frame.size())) return {Found::Failed, ""};
  return {Found::Note, std::string(reinterpret_cast<const char*>(frame.data()) + kClassicHeader, length)};
}

template <class Radio>
Content readTag(Radio& radio) {
  if (radio.supportsNdef()) {
    bool formatted = false;
    if (!radio.ndefFormatted(formatted)) return {Found::Failed, ""};
    if (!formatted) return {Found::Unformatted, ""};
    std::vector<Record> records;
    if (!radio.ndefRecords(records)) return {Found::Failed, ""};
    return describeRecords(records);
  }
  if (radio.isClassic()) return readClassic(radio);
  return {Found::Unsupported, ""};
}

template <class Radio>
SaveResult saveClassic(Radio& radio, const std::string& text) {
  const std::vector<uint8_t> frame = encodeClassic(text);
  // Plenty of door and transit cards are Classic. Only a blank card or one of our own notes
  // is ours to write on, and that has to hold for every block this note would touch.
  std::vector<uint8_t> current(frame.size());
  if (!radio.classicRead(current.data(), current.size())) return {Saved::Private, 0};
  if (!isClassicNote(current.data(), current.size()) && !isBlank(current.data(), current.size())) {
    return {Saved::OtherData, 0};
  }
  if (!radio.classicWrite(frame.data(), frame.size())) return {Saved::Failed, 0};
  std::vector<uint8_t> back(frame.size());
  if (!radio.classicRead(back.data(), back.size()) || back != frame) return {Saved::NotVerified, 0};
  return {Saved::Ok, 0};
}

template <class Radio>
SaveResult saveNote(Radio& radio, const std::string& text) {
  if (text.empty() || text.size() > kMaxNote) return {Saved::TooLong, kMaxNote};
  if (radio.supportsNdef()) {
    bool formatted = false;
    if (!radio.ndefFormatted(formatted)) return {Saved::Failed, 0};
    if (!formatted) return {Saved::Unformatted, 0};
    const size_t capacity = radio.capacity();
    if (capacity > 0 && text.size() + kNdefOverhead > capacity) {
      return {Saved::TooLong, capacity > kNdefOverhead ? capacity - kNdefOverhead : 0};
    }
    if (!radio.ndefWriteText(text)) return {Saved::Failed, 0};
    std::vector<Record> records;
    if (!radio.ndefRecords(records)) return {Saved::NotVerified, 0};
    const Content back = describeRecords(records);
    if (back.found != Found::Note || back.text != text) return {Saved::NotVerified, 0};
    return {Saved::Ok, 0};
  }
  if (radio.isClassic()) return saveClassic(radio, text);
  return {Saved::Unsupported, 0};
}

inline std::string foundMessage(Found found) {
  switch (found) {
    case Found::Note: return "Note read";
    case Found::Link: return "Link read";
    case Found::Blank: return "This tag is empty";
    case Found::OtherData: return "Tag holds something else";
    case Found::Unformatted: return "Tag isn't set up for notes";
    case Found::Private: return "Card is locked with a key";
    case Found::Unsupported: return "This tag type isn't supported";
    case Found::Failed: return "Couldn't read. Hold it still";
    case Found::Nothing: break;
  }
  return "";
}
inline std::string savedMessage(const SaveResult& result) {
  switch (result.saved) {
    case Saved::Ok: return "Saved and checked";
    case Saved::TooLong:
      return "Too long: this tag holds " + std::to_string(result.limit) + " letters";
    case Saved::Unformatted: return "Not saved: tag isn't set up for notes";
    case Saved::Unsupported: return "Not saved: tag type isn't supported";
    case Saved::OtherData: return "Not saved: card holds other data";
    case Saved::Private: return "Not saved: card is locked";
    case Saved::Failed: return "Not saved. Hold it still, press Enter";
    case Saved::NotVerified: return "Save failed. Press Enter to retry";
  }
  return "";
}

// The screen font is plain ASCII. A multi-byte letter becomes one question mark.
inline std::string printable(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char raw : text) {
    const uint8_t byte = static_cast<uint8_t>(raw);
    if (byte == '\n') out += '\n';
    else if (byte == '\t') out += ' ';
    else if (byte >= 0x20 && byte < 0x7F) out += raw;
    else if (byte >= 0xC0) out += '?';
  }
  return out;
}

inline std::vector<std::string> wrapText(const std::string& text, size_t width) {
  std::vector<std::string> lines;
  if (width == 0) return lines;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = std::min(text.find('\n', start), text.size());
    std::string rest = text.substr(start, end - start);
    while (rest.size() > width) {
      size_t cut = rest.rfind(' ', width);
      const bool atSpace = cut != std::string::npos && cut > 0;
      if (!atSpace) cut = width;
      lines.push_back(rest.substr(0, cut));
      rest.erase(0, atSpace ? cut + 1 : cut);
    }
    lines.push_back(rest);
    start = end + 1;
  }
  return lines;
}

enum class Unit { Closed, Missing, Ready };
enum class Mood { Plain, Good, Bad };
enum class Beep { None, Good, Bad };

// A plain struct, because every state must be easy to set up in a test.
struct App {
  Unit unit = Unit::Closed;
  std::string draft;
  bool armed = false;
  uint32_t armedAtMs = 0;
  Content shown;
  std::string uid;
  std::string type;
  std::string message;
  Mood mood = Mood::Plain;
  Beep beep = Beep::None;
  bool changed = false;
  bool tagHere = false;
  uint8_t misses = 0;
  uint32_t lastPollMs = 0;
  uint32_t lastBeginMs = 0;
  bool triedBegin = false;
  size_t scroll = 0;

  void say(const std::string& text, Mood nextMood, Beep sound) {
    message = text;
    mood = nextMood;
    beep = sound;
    changed = true;
  }
  // A tag that can't be read is tried again every poll, and must not beep every time.
  void sayOnce(const std::string& text, Mood nextMood, Beep sound) {
    if (text != message || nextMood != mood) say(text, nextMood, sound);
  }
  void show(const Content& content) {
    shown = content;
    scroll = 0;
    changed = true;
  }
  void reset() {
    const std::string keep = draft;
    *this = App{};
    draft = keep;
    changed = true;
  }
  bool typeKey(char key) {
    if (armed || draft.size() >= kMaxNote || key < 0x20 || key > 0x7E) return false;
    draft += key;
    changed = true;
    return true;
  }
  void backspace() {
    if (armed || draft.empty()) return;
    draft.pop_back();
    changed = true;
  }
  bool arm(uint32_t now) {
    if (armed || draft.empty() || unit != Unit::Ready) return false;
    armed = true;
    armedAtMs = now;
    message.clear();
    mood = Mood::Plain;
    changed = true;
    return true;
  }
  void disarm() {
    if (!armed) return;
    armed = false;
    changed = true;
  }
  // Esc steps back one thing at a time. False means there was nothing left to undo.
  bool cancel() {
    if (armed) {
      disarm();
      say("Not saved", Mood::Plain, Beep::None);
      return true;
    }
    if (!draft.empty()) {
      draft.clear();
      changed = true;
      return true;
    }
    return false;
  }
  void scrollBy(int lines, size_t lineCount, size_t visible) {
    const size_t most = lineCount > visible ? lineCount - visible : 0;
    const int target = static_cast<int>(scroll) + lines;
    const size_t next = target < 0 ? 0 : std::min<size_t>(static_cast<size_t>(target), most);
    if (next == scroll) return;
    scroll = next;
    changed = true;
  }
};

template <class Radio>
void handleTag(App& app, Radio& radio, const std::string& uid, const std::string& type) {
  const bool stillHere = app.tagHere && uid == app.uid;
  app.tagHere = true;
  app.misses = 0;
  if (stillHere && !app.armed) return;
  if (!radio.activate()) {
    app.tagHere = false;
    app.sayOnce(foundMessage(Found::Failed), Mood::Bad, Beep::Bad);
    return;
  }
  if (app.armed) {
    const std::string note = app.draft;
    const SaveResult result = saveNote(radio, note);
    radio.release();
    // One attempt per Enter, so a bad tag is never written to over and over.
    app.armed = false;
    app.changed = true;
    if (result.saved == Saved::Ok) {
      app.uid = uid;
      app.type = type;
      app.draft.clear();
      app.show({Found::Note, note});
    } else {
      // Read it again, because a failed write may have changed what's on it.
      app.tagHere = false;
    }
    app.say(savedMessage(result), result.saved == Saved::Ok ? Mood::Good : Mood::Bad,
            result.saved == Saved::Ok ? Beep::Good : Beep::Bad);
    return;
  }
  const Content content = readTag(radio);
  radio.release();
  if (content.found == Found::Failed) {
    app.tagHere = false;
    app.sayOnce(foundMessage(Found::Failed), Mood::Bad, Beep::Bad);
    return;
  }
  app.uid = uid;
  app.type = type;
  app.show(content);
  const bool good = content.found == Found::Note || content.found == Found::Link ||
                    content.found == Found::Blank;
  app.say(foundMessage(content.found), good ? Mood::Good : Mood::Bad, good ? Beep::Good : Beep::Bad);
}

template <class Radio>
void poll(App& app, Radio& radio, uint32_t now) {
  if (app.armed && now - app.armedAtMs >= kArmTimeoutMs) {
    app.disarm();
    app.say("Not saved: no tag came", Mood::Plain, Beep::None);
  }
  if (app.unit != Unit::Ready) {
    if (app.triedBegin && now - app.lastBeginMs < kRetryUnitMs) return;
    app.triedBegin = true;
    app.lastBeginMs = now;
    const Unit next = radio.begin() ? Unit::Ready : Unit::Missing;
    if (next != app.unit) app.changed = true;
    app.unit = next;
    return;
  }
  if (app.lastPollMs != 0 && now - app.lastPollMs < kPollMs) return;
  app.lastPollMs = now;
  // The field stays on only for the poll: it wakes a tag that's already resting here, and
  // the rest of the time the unit doesn't draw on the battery.
  if (!radio.fieldOn()) {
    app.unit = Unit::Missing;
    app.tagHere = false;
    app.disarm();
    app.triedBegin = true;
    app.lastBeginMs = now;
    app.changed = true;
    return;
  }
  std::string uid, type;
  if (radio.detect(uid, type)) {
    handleTag(app, radio, uid, type);
  } else if (app.tagHere && ++app.misses >= kMissesUntilGone) {
    app.tagHere = false;
    app.misses = 0;
  }
  radio.fieldOff();
}

}  // namespace nfc_notes
