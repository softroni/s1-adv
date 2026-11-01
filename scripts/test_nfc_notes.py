#!/usr/bin/env python3
"""Run the production NFC Notes decisions against simulated tags, on the host."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
app = (parts / '075_main.cpp.inc').read_text()
for file, text in [
    ('001_main.cpp.inc', '#include "nfc_notes.h"'),
    ('001_main.cpp.inc', '#include <M5UnitUnifiedNFC.h>'),
    ('004_main.cpp.inc', '{"NFC Notes", UiMode::NfcNotes}'),
    ('007_main.cpp.inc', 'return "NFC Notes";'),
    ('022_main.cpp.inc', 'mode == "nfc"'),
    ('024_main.cpp.inc', 'mode == UiMode::NfcNotes) beginNfcNotes();'),
    ('043_main.cpp.inc', 'cmd == "/nfc"'),
    ('047_main.cpp.inc', 'gUiMode == UiMode::NfcNotes && !gLauncherVisible && cancelNfcNotes()'),
    ('048_main.cpp.inc', 'handleNfcNotesKey(key, keys)'),
    ('049_main.cpp.inc', 'gUiMode == UiMode::NfcNotes'),
    ('061_main.cpp.inc', 'renderNfcNotesUi();'),
    ('061_main.cpp.inc', 'serviceNfcNotes();'),
    ('063_main.cpp.inc', 'case UiMode::NfcNotes:')]:
    assert text in (parts / file).read_text(), (file, text)
assert 'main_parts/075_main.cpp.inc' in (src / 'main.cpp').read_text()
assert 'm5stack/M5Unit-NFC@' in (root / 'bots/CardputerADV/platformio.ini').read_text()
# These change one-time bits or erase a card, and a note is never worth that.
for call in ('ChangeFormatToNDEF', 'ndefPrepareDesfire', 'ChangeFormat', 'writeAccessCondition', 'Upgrade'):
    assert call not in app, call
assert not re.search(r'\bwrite4\b|\bwrite16\b', app), 'raw writes must go through the bounded calls'

code = r'''
#include "nfc_notes.h"
#include <cassert>
#include <iostream>
#include <map>
using namespace nfc_notes;

struct FakeTag {
  enum class Kind { Ndef, NdefUnformatted, Classic, Other } kind = Kind::Ndef;
  std::string uid = "04A1B2C3", type = "NTAG 213";
  size_t capacity = 144;
  std::vector<Record> records;
  std::vector<uint8_t> classic = std::vector<uint8_t>(720, 0);
  bool locked = false, failWrite = false, garbleWrite = false, failRead = false, failActivate = false;
  int writes = 0;
};
struct FakeRadio {
  FakeTag* tag = nullptr;
  bool present = false, field = false, bus = false, active = false;
  int begins = 0, reads = 0;
  bool begin() { ++begins; return present; }
  bool fieldOn() { if (!present) return false; assert(!field); field = true; return true; }
  void fieldOff() { assert(field); assert(!active); field = false; }
  bool detect(std::string& uid, std::string& type) {
    assert(field);
    if (!tag) return false;
    uid = tag->uid; type = tag->type;
    return true;
  }
  bool activate() { assert(field); if (tag->failActivate) return false; active = true; return true; }
  void release() { assert(active); active = false; }
  bool supportsNdef() const { return tag->kind == FakeTag::Kind::Ndef || tag->kind == FakeTag::Kind::NdefUnformatted; }
  bool isClassic() const { return tag->kind == FakeTag::Kind::Classic; }
  size_t capacity() const { return tag->capacity; }
  bool ndefFormatted(bool& out) { assert(active); out = tag->kind == FakeTag::Kind::Ndef; return !tag->failRead; }
  bool ndefRecords(std::vector<Record>& out) { assert(active); ++reads; if (tag->failRead) return false; out = tag->records; return true; }
  bool ndefWriteText(const std::string& text) {
    assert(active); ++tag->writes;
    if (tag->failWrite) return false;
    tag->records = {{'T', tag->garbleWrite ? text.substr(1) : text}};
    return true;
  }
  bool classicRead(uint8_t* data, size_t length) {
    assert(active); ++reads;
    if (tag->locked || tag->failRead || length > tag->classic.size()) return false;
    std::memcpy(data, tag->classic.data(), length);
    return true;
  }
  bool classicWrite(const uint8_t* data, size_t length) {
    assert(active); ++tag->writes;
    if (tag->failWrite) return false;
    std::memcpy(tag->classic.data(), data, length);
    if (tag->garbleWrite) tag->classic[length - 1] ^= 1;
    return true;
  }
};

uint32_t nowMs = 1000;
struct Bench {
  App app;
  FakeRadio radio;
  Bench() { radio.present = true; tick(); assert(app.unit == Unit::Ready); }
  void tick() { nowMs += kPollMs; poll(app, radio, nowMs); assert(!radio.field && !radio.active); }
  void place(FakeTag& tag) { radio.tag = &tag; tick(); }
  void remove() { radio.tag = nullptr; tick(); tick(); assert(!app.tagHere); }
  void write(const std::string& text) { for (char c : text) assert(app.typeKey(c)); assert(app.arm(nowMs)); }
  Beep takeBeep() { const Beep beep = app.beep; app.beep = Beep::None; return beep; }
};

int main() {
  // No unit: a clear state, a slow retry, and it recovers when plugged in, or is lost.
  {
    App app; FakeRadio radio;
    poll(app, radio, nowMs);
    assert(app.unit == Unit::Missing && radio.begins == 1);
    poll(app, radio, nowMs += 100);
    assert(radio.begins == 1);
    radio.present = true;
    poll(app, radio, nowMs += kRetryUnitMs);
    assert(app.unit == Unit::Ready && radio.begins == 2);
    assert(!app.arm(nowMs));
    app.typeKey('x');
    assert(app.arm(nowMs));
    radio.present = false;
    poll(app, radio, nowMs += kPollMs);
    assert(app.unit == Unit::Missing && !app.armed && app.draft == "x");
    assert(!app.arm(nowMs));
  }
  // Reading: the note shows, once. Taking the tag away and back reads it again.
  {
    Bench b; FakeTag tag; tag.records = {{'T', "Spare keys are in the drawer"}};
    b.place(tag);
    assert(b.app.shown.found == Found::Note && b.app.shown.text == "Spare keys are in the drawer");
    assert(b.app.uid == "04A1B2C3" && b.app.type == "NTAG 213" && b.app.tagHere);
    assert(b.takeBeep() == Beep::Good && b.app.mood == Mood::Good);
    const int reads = b.radio.reads;
    for (int i = 0; i < 5; ++i) b.tick();
    assert(b.radio.reads == reads && b.takeBeep() == Beep::None);
    // One missed poll is the edge of range, not a new tap.
    b.radio.tag = nullptr; b.tick(); assert(b.app.tagHere); b.radio.tag = &tag; b.tick();
    assert(b.radio.reads == reads);
    b.remove();
    assert(b.app.shown.text == "Spare keys are in the drawer");
    b.place(tag);
    assert(b.radio.reads == reads + 1 && b.takeBeep() == Beep::Good);
  }
  // Every other thing a tag can hold.
  {
    Bench b; FakeTag tag;
    tag.records = {{'U', "https://example.com"}}; b.place(tag);
    assert(b.app.shown.found == Found::Link && b.app.shown.text == "https://example.com"); b.remove();
    tag.records = {}; b.place(tag);
    assert(b.app.shown.found == Found::Blank && b.app.message == "This tag is empty"); b.remove();
    tag.records = {{0, "x"}}; b.place(tag);
    assert(b.app.shown.found == Found::OtherData); b.remove();
    tag.records = {{'U', "https://a"}, {'T', "note"}}; b.place(tag);
    assert(b.app.shown.found == Found::Note && b.app.shown.text == "note"); b.remove();
    tag.kind = FakeTag::Kind::NdefUnformatted; b.place(tag);
    assert(b.app.shown.found == Found::Unformatted); b.remove();
    tag.kind = FakeTag::Kind::Other; b.place(tag);
    assert(b.app.shown.found == Found::Unsupported); b.remove();
  }
  // A shaky read is retried, beeps once, and doesn't wipe what's on screen.
  {
    Bench b; FakeTag good; good.records = {{'T', "kept"}}; b.place(good); b.remove(); b.takeBeep();
    FakeTag bad; bad.failRead = true;
    b.place(bad);
    assert(b.app.mood == Mood::Bad && b.takeBeep() == Beep::Bad && b.app.shown.text == "kept");
    b.tick(); assert(b.takeBeep() == Beep::None);
    bad.failRead = false; bad.records = {{'T', "now fine"}}; b.tick();
    assert(b.app.shown.text == "now fine" && b.takeBeep() == Beep::Good);
    b.remove();
    FakeTag dead; dead.failActivate = true;
    b.place(dead);
    assert(b.app.mood == Mood::Bad && !b.app.tagHere);
  }
  // Saving: the note lands, is read back, and only then is the draft cleared.
  {
    Bench b; FakeTag tag;
    b.write("Anna");
    b.place(tag);
    assert(tag.writes == 1 && tag.records[0].text == "Anna");
    assert(!b.app.armed && b.app.draft.empty() && b.app.shown.text == "Anna" && b.app.uid == tag.uid);
    assert(b.app.message == "Saved and checked" && b.takeBeep() == Beep::Good);
    // The saved tag stays put: no second write and no pointless read.
    const int reads = b.radio.reads;
    b.tick(); b.tick();
    assert(tag.writes == 1 && b.radio.reads == reads);
    // Pressing Enter with a tag already resting on the unit saves to it.
    b.write("Second"); b.tick();
    assert(tag.writes == 2 && tag.records[0].text == "Second");
  }
  // A failed save keeps the words, disarms, and never writes twice for one Enter.
  {
    Bench b; FakeTag tag; tag.failWrite = true;
    b.write("Keep me"); b.place(tag);
    assert(tag.writes == 1 && !b.app.armed && b.app.draft == "Keep me" && b.app.mood == Mood::Bad);
    b.tick(); b.tick();
    assert(tag.writes == 1);
    tag.failWrite = false; tag.garbleWrite = true; assert(b.app.arm(nowMs)); b.tick();
    assert(b.app.message == "Save failed. Press Enter to retry" && b.app.draft == "Keep me");
    tag.garbleWrite = false; assert(b.app.arm(nowMs)); b.tick();
    assert(b.app.message == "Saved and checked" && b.app.draft.empty());
  }
  // A failed save leaves the tag to be read again, because its contents may have changed.
  {
    Bench b; FakeTag tag; tag.records = {{'T', "old"}}; b.place(tag);
    tag.failWrite = true; b.write("new"); b.tick(); assert(b.app.mood == Mood::Bad);
    tag.failWrite = false; tag.records = {{'T', "half written"}}; b.tick();
    assert(b.app.shown.text == "half written" && b.app.draft == "new");
  }
  // Tag limits, including a read-only or unformatted tag, which is never touched.
  {
    Bench b; FakeTag small; small.type = "Ultralight"; small.capacity = 48;
    b.write(std::string(39, 'x')); b.place(small);
    assert(small.writes == 0 && b.app.message == "Too long: this tag holds 38 letters");
    b.app.backspace(); assert(b.app.arm(nowMs)); b.tick();
    assert(small.writes == 1 && small.records[0].text.size() == 38);
    FakeTag blank; blank.kind = FakeTag::Kind::NdefUnformatted;
    b.remove(); b.write("note"); b.place(blank);
    assert(blank.writes == 0 && b.app.draft == "note" && b.app.mood == Mood::Bad);
    b.remove();
    FakeTag other; other.kind = FakeTag::Kind::Other; assert(b.app.arm(nowMs)); b.place(other);
    assert(other.writes == 0 && b.app.message == "Not saved: tag type isn't supported");
  }
  // MIFARE Classic: a blank card takes a note and gives it back, across blocks.
  {
    Bench b; FakeTag card; card.kind = FakeTag::Kind::Classic; card.type = "MIFARE Classic1K";
    b.place(card);
    assert(b.app.shown.found == Found::Blank);
    b.remove();
    for (const std::string& text : {std::string("Short"), std::string(11, 'a'), std::string(12, 'b'),
                                   std::string(kMaxNote, 'c'), std::string("Short again")}) {
      b.write(text); b.place(card);
      assert(b.app.message == "Saved and checked");
      b.remove(); b.place(card);
      assert(b.app.shown.found == Found::Note && b.app.shown.text == text);
      b.remove();
    }
  }
  // MIFARE Classic: a write that doesn't read back the same is reported, with the words kept.
  {
    Bench b; FakeTag card; card.kind = FakeTag::Kind::Classic; card.garbleWrite = true;
    b.write("Check me"); b.place(card);
    assert(b.app.message == "Save failed. Press Enter to retry" && b.app.draft == "Check me");
  }
  // MIFARE Classic: other people's cards are left exactly as they were.
  {
    Bench b; FakeTag door; door.kind = FakeTag::Kind::Classic; door.classic[0] = 0x42;
    const auto before = door.classic;
    b.place(door);
    assert(b.app.shown.found == Found::OtherData);
    b.write("Oops"); b.tick();
    assert(door.writes == 0 && door.classic == before && b.app.message == "Not saved: card holds other data");
    // Data further along than block 4 counts too.
    FakeTag second; second.kind = FakeTag::Kind::Classic; second.classic[20] = 7;
    b.remove(); b.write(std::string(40, 'x')); assert(b.app.draft.size() == 44);
    b.place(second);
    assert(second.writes == 0 && b.app.mood == Mood::Bad);
    FakeTag locked; locked.kind = FakeTag::Kind::Classic; locked.locked = true;
    b.remove(); assert(b.app.arm(nowMs)); b.place(locked);
    assert(locked.writes == 0 && b.app.message == "Not saved: card is locked");
    b.remove(); b.place(locked);
    assert(b.app.shown.found == Found::Private);
  }
  // A damaged frame can't claim more than a note can hold.
  {
    uint8_t frame[16] = {'S', '1', 'N', 'T', 201};
    assert(!isClassicNote(frame, sizeof(frame)));
    frame[4] = 0;
    assert(!isClassicNote(frame, sizeof(frame)));
    frame[4] = 200;
    assert(isClassicNote(frame, sizeof(frame)));
    assert(classicFrameSize(11) == 16 && classicFrameSize(12) == 32 && classicFrameSize(kMaxNote) == 208);
  }
  // An armed save runs out, and Esc steps back: armed, then the draft, then out of the app.
  {
    Bench b; b.write("Late");
    nowMs += kArmTimeoutMs;
    FakeTag tag; b.place(tag);
    assert(!b.app.armed && tag.writes == 0 && b.app.draft == "Late");
    assert(b.app.arm(nowMs) && b.app.cancel() && !b.app.armed && b.app.draft == "Late");
    assert(b.app.cancel() && b.app.draft.empty());
    assert(!b.app.cancel());
  }
  // Typing is plain text, stops at the limit, and can't change an armed note.
  {
    App app; app.unit = Unit::Ready;
    assert(!app.arm(nowMs));
    assert(!app.typeKey('\n') && !app.typeKey(char(1)) && !app.typeKey(char(200)));
    for (size_t i = 0; i < kMaxNote; ++i) assert(app.typeKey('a'));
    assert(!app.typeKey('a') && app.draft.size() == kMaxNote);
    app.backspace(); assert(app.draft.size() == kMaxNote - 1);
    assert(app.arm(nowMs) && !app.arm(nowMs));
    assert(!app.typeKey('b'));
    app.backspace(); assert(app.draft.size() == kMaxNote - 1);
  }
  // Opening the app again starts clean but keeps words that were never saved.
  {
    App app; app.draft = "Unsaved"; app.armed = true; app.unit = Unit::Ready;
    app.shown = {Found::Note, "Old"}; app.message = "x";
    app.reset();
    assert(app.draft == "Unsaved" && !app.armed && app.unit == Unit::Closed);
    assert(app.shown.found == Found::Nothing && app.message.empty());
  }
  // Text on screen: words wrap, long words split, and foreign letters stay one column.
  {
    assert(printable("caf\xC3\xA9\tok\x01\n!") == "caf? ok\n!");
    assert((wrapText("one two three", 7) == std::vector<std::string>{"one two", "three"}));
    assert((wrapText("abcdefghij", 4) == std::vector<std::string>{"abcd", "efgh", "ij"}));
    assert((wrapText("a\n\nb", 4) == std::vector<std::string>{"a", "", "b"}));
    assert((wrapText("", 4) == std::vector<std::string>{""}));
    assert(wrapText("x", 0).empty());
    for (size_t width = 1; width < 50; ++width) {
      std::string joined;
      for (const auto& line : wrapText(std::string(120, 'q') + " end of it", width)) {
        assert(line.size() <= width);
        joined += line;
      }
      assert(joined.find("end") != std::string::npos);
    }
    App app;
    app.scrollBy(1, 3, 5); assert(app.scroll == 0);
    app.scrollBy(-1, 9, 5); assert(app.scroll == 0);
    app.scrollBy(3, 9, 5); assert(app.scroll == 3);
    app.scrollBy(9, 9, 5); assert(app.scroll == 4);
    app.scrollBy(-9, 9, 5); assert(app.scroll == 0);
  }
  std::cout << "PASS NFC Notes: read, save, verify, Classic cards, limits, retries, arming, text" << std::endl;
}
'''
with tempfile.TemporaryDirectory(prefix='s1-nfc-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'), '-o', str(p / 'test')],
                   check=True)
    subprocess.run([str(p / 'test')], check=True)
