#!/usr/bin/env python3
"""Mesh messaging on the host: contacts, the outbox's delivery states, Help, history and position."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CODE = r'''
#include "mesh_help.h"
#include "mesh_outbox.h"
#include <cassert>
#include <random>
#include <string>
#include <vector>
using namespace s1_mesh;

// The library as the outbox sees it: one staged frame at a time, and one reliable send it retries.
struct FakeRadio {
  uint32_t nextId = 1000;
  uint8_t result = 0;       // What a refused send says.
  bool refuse = false;
  bool pendingAck = false;  // The library's hasPendingAck().
  bool tx = false;          // isTransmitting().
  std::vector<Outgoing> sent;
  uint32_t send(const Outgoing& o) {
    if (refuse) return 0;
    sent.push_back(o);
    tx = true;
    pendingAck = o.reliable;
    return nextId++;
  }
  uint8_t lastResult() const { return result; }
  bool pending() const { return pendingAck; }
  bool transmitting() const { return tx; }
};
struct Event { uint32_t seq; Delivery d; uint8_t reason; uint32_t id; };
struct Sink {
  std::vector<Event> events;
  void status(uint32_t seq, Delivery d, uint8_t reason, uint32_t id) { events.push_back({seq, d, reason, id}); }
  Delivery last(uint32_t seq) const {
    for (size_t i = events.size(); i--;) if (events[i].seq == seq) return events[i].d;
    return Delivery::Received;
  }
};
Outgoing text(uint32_t seq, uint32_t to, bool reliable = true) {
  Outgoing o; o.seq = seq; o.to = to; o.reliable = reliable; o.pki = to != kBroadcast;
  o.len = 2; o.data[0] = 'h'; o.data[1] = 'i';
  return o;
}

void contacts() {
  uint8_t key[32]; for (int i = 0; i < 32; ++i) key[i] = uint8_t(i + 1);
  uint8_t blank[32] = {};
  Contacts c;
  assert(c.add(0, "x", key) == Contacts::Add::Bad && c.add(kBroadcast, "x", key) == Contacts::Add::Bad);
  assert(c.add(5, "x", blank) == Contacts::Add::Bad && c.add(5, "x", nullptr) == Contacts::Add::Bad);
  assert(c.add(1, "  Alex  ", key) == Contacts::Add::Added && std::string(c.find(1)->name) == "Alex");
  key[0] = 99;
  assert(c.add(1, "Alex B", key) == Contacts::Add::Updated && c.count == 1 && c.find(1)->key[0] == 99);
  for (uint32_t id = 2; id <= kContacts; ++id) assert(c.add(id, "n", key) == Contacts::Add::Added);
  assert(c.add(77, "n", key) == Contacts::Add::Full && c.count == kContacts);
  // A NodeInfo renames a contact but never changes the key it was saved with.
  assert(c.rename(1, "Alex Car") && std::string(c.find(1)->name) == "Alex Car" && c.find(1)->key[0] == 99);
  assert(!c.rename(1, "Alex Car") && !c.rename(1, "   ") && !c.rename(42, "x"));
  // A name too long for Meshtastic stops at a whole character, inside 39 bytes.
  std::string longName; for (int i = 0; i < 30; ++i) longName += u8"Ж";
  c.rename(2, longName.c_str());
  const std::string kept = c.find(2)->name;
  assert(kept.size() <= kNameBytes && kept.size() % 2 == 0 && kept.size() == 38);
  uint8_t bytes[Contacts::kBytes];
  const size_t n = c.save(bytes, sizeof(bytes));
  Contacts back; assert(back.load(bytes, n) && back.count == kContacts);
  for (size_t i = 0; i < kContacts; ++i) {
    assert(back.list[i].id == c.list[i].id && !strcmp(back.list[i].name, c.list[i].name));
    assert(!memcmp(back.list[i].key, c.list[i].key, 32));
  }
  // Damage anywhere loads nothing, and leaves no contact half-read.
  for (size_t cut = 0; cut < n; ++cut) { Contacts t; t.add(9, "old", key); assert(!t.load(bytes, cut) && t.count == 0); }
  bytes[0] = 7; { Contacts t; assert(!t.load(bytes, n) && !t.count); } bytes[0] = Contacts::kVersion;
  std::mt19937 rng(7);
  for (int trial = 0; trial < 20000; ++trial) {
    uint8_t junk[Contacts::kBytes + 8]; const size_t len = rng() % sizeof(junk);
    for (size_t i = 0; i < len; ++i) junk[i] = uint8_t(rng());
    if (len > 1 && trial % 2) { junk[0] = Contacts::kVersion; junk[1] = uint8_t(rng() % (kContacts + 1)); }
    Contacts t;
    if (t.load(junk, len)) for (size_t i = 0; i < t.count; ++i) assert(strlen(t.list[i].name) <= kNameBytes && t.list[i].id);
    else assert(!t.count);
  }
  assert(c.remove(1) && !c.find(1) && c.count == kContacts - 1 && c.list[0].id == 2 && !c.remove(1));

  char s[5];
  shortNameFor("Alex", s); assert(std::string(s) == "Alex");
  shortNameFor("Alexandra", s); assert(std::string(s) == "Alex");
  shortNameFor("alex car", s); assert(std::string(s) == "AC");
  shortNameFor("  Big  red   car van truck ", s); assert(std::string(s) == "BRCV");
  shortNameFor("S1-ADV", s); assert(std::string(s) == "S1AD");
  shortNameFor(u8"Жук", s); assert(std::string(s) == "S1");
  shortNameFor("", s); assert(std::string(s) == "S1");
  char clean[kNameBytes + 1]; cleanName(clean, sizeof(clean), "\x01" " Tab\tname  "); assert(std::string(clean) == "Tab name");
}

void outbox() {
  // A direct message the person's radio answers.
  { FakeRadio r; Sink k; Outbox o; assert(o.push(text(1, 42)));
    o.service(0, r, k); assert(k.last(1) == Delivery::Sending && r.sent.size() == 1 && o.currentId() == 1000);
    o.service(10, r, k); assert(k.last(1) == Delivery::Sending);
    assert(o.routing(7, 1000, 0, k) && k.last(1) == Delivery::Sending);  // Another radio's ack: not delivered.
    assert(o.busy());
    assert(o.routing(42, 1000, 0, k) && k.last(1) == Delivery::Delivered && o.idle()); }
  // Their radio refusing it: the reason is kept.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); o.service(0, r, k);
    assert(o.routing(42, 1000, 35, k) && k.last(1) == Delivery::Failed && k.events.back().reason == 35); }
  // Repeated by a radio on the way, then the person's ack: Heard, then Delivered.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); o.service(0, r, k);
    r.tx = false; r.pendingAck = false; o.service(100, r, k); assert(k.last(1) == Delivery::Heard && o.busy());
    o.service(100 + kAckWaitMs - 1, r, k); assert(o.busy());
    assert(o.routing(42, 1000, 0, k) && k.last(1) == Delivery::Delivered); }
  // Repeated but never answered.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); o.service(0, r, k);
    r.tx = false; r.pendingAck = false; o.service(100, r, k);
    o.service(100 + kAckWaitMs, r, k); assert(k.last(1) == Delivery::NotConfirmed && o.idle()); }
  // Every try out and nothing heard: the library stops retrying as the last try goes on the air.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); o.service(0, r, k);
    r.pendingAck = false; r.tx = true; o.service(500, r, k); assert(k.last(1) == Delivery::Sending);
    r.tx = false; o.service(500 + kLastTryWaitMs - 1, r, k); assert(o.busy());
    o.service(500 + kLastTryWaitMs, r, k); assert(k.last(1) == Delivery::NoAnswer && o.idle()); }
  // An ack for the last try still counts.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); o.service(0, r, k);
    r.pendingAck = false; o.service(500, r, k); r.tx = false; o.service(600, r, k);
    assert(o.routing(42, 1000, 0, k) && k.last(1) == Delivery::Delivered); }
  // A channel message: heard by a radio, or not. No ack can come for one, and none is counted.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, kBroadcast)); o.service(0, r, k);
    assert(o.routing(42, 1000, 0, k) && k.last(1) == Delivery::Sending);
    r.tx = false; r.pendingAck = false; o.service(10, r, k); assert(k.last(1) == Delivery::Heard && o.idle()); }
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, kBroadcast)); o.service(0, r, k);
    r.pendingAck = false; o.service(10, r, k); r.tx = false; o.service(10 + kLastTryWaitMs, r, k);
    assert(k.last(1) == Delivery::NoAnswer); }
  // One at a time: nothing else goes until the first has an ending, and order is kept.
  { FakeRadio r; Sink k; Outbox o;
    for (uint32_t s = 1; s <= kOutbox; ++s) assert(o.push(text(s, 42)));
    assert(!o.push(text(99, 42)) && o.queued() == kOutbox);
    o.service(0, r, k); r.tx = false;
    for (int i = 0; i < 20; ++i) o.service(1 + i, r, k);
    assert(r.sent.size() == 1 && o.holds(1) && o.holds(8) && !o.holds(99));
    for (uint32_t s = 1; s <= kOutbox; ++s) {
      assert(o.currentSeq() == s);
      assert(o.routing(42, 1000 + s - 1, 0, k));
      r.pendingAck = true;  // The library still retrying something of its own holds the next one.
      o.service(100 * s, r, k); assert(r.sent.size() == s);
      r.pendingAck = false; r.tx = true; o.service(100 * s + 1, r, k); assert(r.sent.size() == s);
      r.tx = false; o.service(100 * s + 2, r, k);
    }
    assert(o.idle() && r.sent.size() == kOutbox);
    for (uint32_t s = 1; s <= kOutbox; ++s) assert(r.sent[s - 1].seq == s && k.last(s) == Delivery::Delivered); }
  // Refusals: a busy radio is tried again shortly, the airtime limit later, anything else fails.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); r.refuse = true; r.result = send_result::kTxBusy;
    o.service(0, r, k); assert(k.events.empty() && o.queued() == 1);
    r.refuse = false; o.service(kBusyRetryMs - 1, r, k); assert(r.sent.empty());
    o.service(kBusyRetryMs, r, k); assert(r.sent.size() == 1); }
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); r.refuse = true; r.result = send_result::kDutyCycle;
    o.service(0, r, k); assert(k.last(1) == Delivery::Waiting && o.queued() == 1);
    r.refuse = false; o.service(kAirtimeRetryMs - 1, r, k); assert(r.sent.empty());
    o.service(kAirtimeRetryMs, r, k); assert(k.last(1) == Delivery::Sending); }
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); o.push(text(2, 42)); r.refuse = true; r.result = 6;
    o.service(0, r, k); assert(k.last(1) == Delivery::Failed && k.events.back().reason == kReasonLocal + 6);
    r.result = send_result::kOk; o.service(1, r, k); assert(k.last(2) == Delivery::Failed && k.events.back().reason == kReasonEncode);
    assert(o.idle()); }
  // A position goes once and asks for nothing.
  { FakeRadio r; Sink k; Outbox o; Outgoing p = text(0, 42, false); p.port = kPortPosition; o.push(p);
    o.service(0, r, k); assert(!r.pendingAck && o.busy()); o.service(1, r, k); assert(o.busy());
    r.tx = false; o.service(2, r, k); assert(o.idle()); }
  // Timer wrap.
  { FakeRadio r; Sink k; Outbox o; o.push(text(1, 42)); const uint32_t t = UINT32_MAX - 10;
    o.service(t, r, k); r.tx = false; r.pendingAck = false; o.service(t + 5, r, k);
    o.service(t + 5 + kAckWaitMs - 1, r, k); assert(o.busy()); o.service(t + 5 + kAckWaitMs, r, k); assert(o.idle()); }
  assert(std::string(deliveryLabel(Delivery::Heard, true)) == "relayed" && std::string(deliveryLabel(Delivery::Heard, false)) == "heard nearby");
  assert(std::string(failureLabel(35)) == "they don't have your key yet" && std::string(failureLabel(123)) == "refused");
}

void history() {
  State s;
  uint8_t bytes[kHistoryBytes];
  assert(s.saveHistory(bytes, sizeof(bytes)) == 6 && s.loadHistory(bytes, 6) && !s.messageCount);
  std::string big(kTextBytes, 'W');
  for (uint32_t i = 0; i < kMessages; ++i) {
    if (i % 3 == 0) s.message(10 + i, 500 + i, false, reinterpret_cast<const uint8_t*>(big.data()), big.size());
    else { auto& m = s.queued(1, 40 + i, i % 2, i, big.c_str()); m.id = 900 + i; m.status = Delivery(i % kDeliveryCount); m.reason = uint8_t(i); }
  }
  const size_t n = s.saveHistory(bytes, sizeof(bytes));
  assert(n == kHistoryBytes);
  State b; assert(b.loadHistory(bytes, n) && b.messageCount == kMessages);
  for (size_t i = 0; i < kMessages; ++i) {
    const auto& x = s.messages[i]; const auto& y = b.messages[i];
    assert(x.from == y.from && x.id == y.id && x.to == y.to && x.seq == y.seq && x.outgoing == y.outgoing);
    assert(x.direct == y.direct && x.reason == y.reason && !strcmp(x.text, y.text));
    // Anything still going when it was saved can't be finished after a restart.
    const bool going = x.status == Delivery::Waiting || x.status == Delivery::Sending || x.status == Delivery::Received;
    assert(!x.outgoing ? y.status == Delivery::Received : going ? y.status == Delivery::Unknown : y.status == x.status);
  }
  for (size_t cut = 0; cut < n; ++cut) { State t; t.message(1, 1, false, reinterpret_cast<const uint8_t*>("x"), 1); assert(!t.loadHistory(bytes, cut) && !t.messageCount); }
  std::mt19937 rng(11);
  for (int trial = 0; trial < 20000; ++trial) {
    uint8_t junk[kHistoryBytes]; const size_t len = rng() % sizeof(junk);
    for (size_t i = 0; i < len; ++i) junk[i] = uint8_t(rng());
    if (len > 6 && trial % 2) { memcpy(junk, "S1MH", 4); junk[4] = kHistoryVersion; junk[5] = uint8_t(rng() % (kMessages + 1)); }
    State t;
    if (t.loadHistory(junk, len)) for (size_t i = 0; i < t.messageCount; ++i) {
      assert(strlen(t.messages[i].text) <= kTextBytes && uint8_t(t.messages[i].status) < kDeliveryCount);
      for (const char* p = t.messages[i].text; *p; ++p) assert(uint8_t(*p) >= 32 && uint8_t(*p) != 127);
    } else assert(!t.messageCount);
  }
  // Waiting messages have no packet id yet; two of them are both kept.
  State w; w.queued(1, 42, true, 1, "a"); w.queued(1, 42, true, 2, "b");
  assert(w.messageCount == 2 && w.bySeq(2) && !w.bySeq(0) && !w.byPacket(0));
  w.bySeq(1)->id = 77; assert(w.byPacket(77) == w.bySeq(1));
  // A received repeat is still dropped.
  assert(w.message(5, 9, false, reinterpret_cast<const uint8_t*>("x"), 1) && !w.message(5, 9, false, reinterpret_cast<const uint8_t*>("y"), 1));
  static_assert(sizeof(State) < 9216, "bounded mode-only state: 30 messages of up to 200 bytes");
}

void help() {
  char out[kTextBytes + 1];
  char d[16];
  formatDegrees(d, sizeof(d), 4251912); assert(std::string(d) == "42.51912");
  formatDegrees(d, sizeof(d), -7089645); assert(std::string(d) == "-70.89645");
  formatDegrees(d, sizeof(d), -5); assert(std::string(d) == "-0.00005");
  formatDegrees(d, sizeof(d), 0); assert(std::string(d) == "0.00000");
  formatDegrees(d, sizeof(d), 18000000); assert(std::string(d) == "180.00000");
  Where w; w.known = true; w.latE5 = 4251912; w.lonE5 = -7089645; w.atMs = 1000;
  composeHelp(out, sizeof(out), 0, w, 1000 + kFixFreshMs - 1);
  assert(std::string(out) == "Out of gas. Please come and get me. Location: https://maps.google.com/?q=42.51912,-70.89645");
  composeHelp(out, sizeof(out), 1, w, 1000 + 12 * 60000);
  assert(std::string(out) == "Car trouble. I need help. Location 12 min ago: https://maps.google.com/?q=42.51912,-70.89645");
  composeHelp(out, sizeof(out), 2, w, 1000 + kFixUsableMs);
  assert(std::string(out) == "My phone is dead. Reply here. No GPS fix.");
  composeHelp(out, sizeof(out), 3, Where{}, 0); assert(std::string(out) == "I'm OK now. No help needed. No GPS fix.");
  composeLocation(out, sizeof(out), w, 2000); assert(std::string(out) == "My location. Location: https://maps.google.com/?q=42.51912,-70.89645");
  // The longest one fits a Meshtastic text, whatever the coordinates.
  Where far = w; far.latE5 = -8999999; far.lonE5 = -17999999;
  for (uint8_t c = 0; c < kHelpChoices; ++c) {
    const size_t n = composeHelp(out, sizeof(out), c, far, 1000 + 59 * 60000);
    assert(n == strlen(out) && n < 140);
  }
  char tiny[8]; composeHelp(tiny, sizeof(tiny), 0, w, 1000); assert(strlen(tiny) == 7);

  uint8_t pos[32];
  const size_t n = encodePosition(pos, sizeof(pos), 4251912, -7089645, 9);
  // Checked once against Meshtastic's own mesh.proto with protoc --decode=meshtastic.Position.
  const uint8_t want[] = {0x0D, 0x20, 0xE7, 0x57, 0x19,  // 425191200 as sfixed32
                          0x15, 0x6C, 0x0F, 0xBE, 0xD5,  // -708964500
                          0x28, 0x02, 0x98, 0x01, 0x09, 0xB8, 0x01, 0x20};
  assert(n == sizeof(want) && !memcmp(pos, want, n));
  assert(!encodePosition(pos, 17, 0, 0, 0));

  // The public channel's share: off says nothing about where, rough is Meshtastic's 13 bits,
  // moved to the middle of its box the way the firmware does, in the text and the pin alike.
  assert(roughE7(425191200, 13) == 424935424 && roughE7(-708964500, 13) == -709099520 && roughE7(5, 32) == 5);
  composeHelp(out, sizeof(out), 0, w, 1000, Sharing::Rough);
  assert(std::string(out) == "Out of gas. Please come and get me. Location within 3 km: https://maps.google.com/?q=42.49,-70.91");
  composeHelp(out, sizeof(out), 1, w, 1000 + 12 * 60000, Sharing::Rough);
  assert(std::string(out) == "Car trouble. I need help. Location 12 min ago, within 3 km: https://maps.google.com/?q=42.49,-70.91");
  composeHelp(out, sizeof(out), 0, w, 1000, Sharing::Off); assert(std::string(out) == "Out of gas. Please come and get me.");
  composeHelp(out, sizeof(out), 2, Where{}, 0, Sharing::Off); assert(std::string(out) == "My phone is dead. Reply here.");
  composeHelp(out, sizeof(out), 2, Where{}, 0, Sharing::Rough); assert(std::string(out) == "My phone is dead. Reply here. No GPS fix.");
  assert(!composeLocation(out, sizeof(out), w, 2000, Sharing::Off) && !out[0]);
  composeLocation(out, sizeof(out), w, 2000, Sharing::Rough);
  assert(std::string(out) == "My location. Location within 3 km: https://maps.google.com/?q=42.49,-70.91");
  // Checked once against mesh.proto with protoc too: the same spot, precision_bits 13.
  const uint8_t wantRough[] = {0x0D, 0x00, 0x00, 0x54, 0x19, 0x15, 0x00, 0x00, 0xBC, 0xD5,
                               0x28, 0x02, 0x98, 0x01, 0x09, 0xB8, 0x01, 0x0D};
  assert(encodePosition(pos, sizeof(pos), 4251912, -7089645, 9, kRoughBits) == sizeof(wantRough) && !memcmp(pos, wantRough, sizeof(wantRough)));
  assert(!encodePosition(pos, sizeof(pos), 0, 0, 0, 0) && !encodePosition(pos, sizeof(pos), 0, 0, 0, 33));
  // Anywhere on Earth, rough is never more than half its box from the real spot, and never the spot itself.
  std::mt19937 rng(3);
  for (int i = 0; i < 100000; ++i) {
    const int32_t lat = int32_t(rng() % 18000001) - 9000000, lon = int32_t(rng() % 36000001) - 18000000;
    for (int32_t e5 : {lat, lon}) {
      const int64_t d = int64_t(roughE7(e5 * 100, kRoughBits)) - int64_t(e5) * 100;
      assert(d >= -(int64_t(1) << 18) && d <= (int64_t(1) << 18));
    }
    assert(roughE5(lat) / 100000 <= 90 && roughE5(lat) / 100000 >= -90);
  }
  formatDegrees(d, sizeof(d), -4, 2); assert(std::string(d) == "0.00");
  formatDegrees(d, sizeof(d), -7090995, 2); assert(std::string(d) == "-70.91");
  formatDegrees(d, sizeof(d), 18000000, 2); assert(std::string(d) == "180.00");
  assert(sharingOrDefault(0) == Sharing::Off && sharingOrDefault(2) == Sharing::Exact && sharingOrDefault(9) == Sharing::Off);
  assert(std::string(sharingName(Sharing::Rough)) == "rough");

  Help h; assert(!h.due(0, true));
  h.start(0, 100); assert(h.due(100, true) && !h.due(100, false) && h.repeating());
  assert(h.alsoPrivate && !h.alsoPublic);  // The private channel is on unless C turns it off.
  h.seq[0] = 5; h.seq[kHelpPrivateSlot] = 7; h.seq[kHelpPublicSlot] = 6; h.roundSent(100);
  assert(!h.due(100 + kHelpRepeatMs - 1, true) && h.due(100 + kHelpRepeatMs, true));
  assert(h.nextInMs(100 + 60000) == kHelpRepeatMs - 60000);
  h.delivered(6); assert(h.repeating());      // The public channel's "heard" isn't a person.
  h.delivered(7); assert(h.repeating());      // Nor is the private channel's.
  h.delivered(99); assert(h.repeating());
  h.delivered(5); assert(!h.repeating() && h.stop == HelpStop::Confirmed && !h.due(UINT32_MAX, true));
  h.start(1, 200); h.roundSent(200); h.replied(false, false, 300); assert(h.repeating());
  h.replied(false, true, 300); assert(h.stop == HelpStop::Replied);
  h.start(1, 200); h.roundSent(200); h.replied(true, false, 300); assert(h.stop == HelpStop::Replied);  // A contact or the private channel.
  h.start(kHelpOkChoice, 0); assert(!h.repeating() && h.due(0, true));
  h.roundSent(0); assert(h.stop == HelpStop::Ok && !h.due(kHelpRepeatMs, true));
  h.start(9, 0); assert(h.choice == 0);
}

int main() { contacts(); outbox(); history(); help(); }
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-messaging-') as directory:
    p = Path(directory)
    (p / 'messaging.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'bots/CardputerADV/src'), str(p / 'messaging.cpp'), '-o', str(p / 'messaging')], check=True)
    subprocess.run([str(p / 'messaging')], check=True)
print('PASS Mesh messaging: contacts saved and loaded whole or not at all, short names, the outbox one at a time with '
      'delivered, relayed, not confirmed, no answer and refusals, history across a restart, Help text with the map '
      'link and fix age, off, rough and exact for the public channel, the Position packet at full and rough precision, and when Help repeats and stops (ASan/UBSan)')
