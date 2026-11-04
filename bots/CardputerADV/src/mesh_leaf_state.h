#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Fixed bounds: remote packets cannot grow the node list, history or composer.
namespace s1_mesh {
constexpr size_t kNodes = 16, kMessages = 8, kTextBytes = 200;
constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

// What became of a message this Cardputer sent. A received message stays at Received.
enum class Delivery : uint8_t {
  Received = 0,
  Waiting,       // Queued behind another message, which the radio sends one at a time.
  Sending,       // On the air: the radio tries again until another radio repeats or answers it.
  Heard,         // Another radio repeated it. For a channel message, that's all anyone can know.
  Delivered,     // The person's own radio said it arrived.
  NotConfirmed,  // Repeated by a radio on the way, but the person's radio never answered.
  NoAnswer,      // Every try went out and no radio repeated or answered it.
  Failed,        // Refused, here or by their radio. The message's reason says why.
  Unknown,       // It was still going when the Cardputer restarted.
};
constexpr uint8_t kDeliveryCount = 9;

inline size_t copyText(char* out, size_t capacity, const uint8_t* in, size_t length) {
  if (!capacity) return 0;
  size_t used = 0;
  for (size_t i = 0; i < length && used + 1 < capacity;) {
    const uint8_t c = in[i];
    if (c < 32 || c == 127) { out[used++] = ' '; ++i; continue; }
    if (c < 128) { out[used++] = char(c); ++i; continue; }
    size_t n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    bool valid = n && i + n <= length;
    for (size_t j = 1; valid && j < n; ++j) valid = (in[i+j] & 0xc0) == 0x80;
    if (valid && n >= 3) valid = !(c == 0xe0 && in[i+1] < 0xa0) && !(c == 0xed && in[i+1] >= 0xa0);
    if (valid && n == 4) valid = !(c == 0xf0 && in[i+1] < 0x90) && !(c == 0xf4 && in[i+1] >= 0x90);
    if (!valid) { out[used++] = '?'; ++i; continue; }
    if (used + n >= capacity) break;
    memcpy(out + used, in + i, n); used += n; i += n;
  }
  out[used] = 0;
  return used;
}
struct Node {
  uint32_t id = 0, seen = 0;
  int16_t rssi = 0;
  bool hasKey = false;  // Its public key, from its NodeInfo, which a direct message needs.
  char name[41] = {};
  uint8_t key[32] = {};
};
struct Message {
  uint32_t from = 0, id = 0;  // id is the packet's, so 0 until a waiting message goes out.
  uint32_t to = kBroadcast;
  uint32_t seq = 0;           // This Cardputer's own number for a message it sent.
  bool outgoing = false;
  bool direct = false;        // Sent to one person, encrypted so only their radio can read it.
  Delivery status = Delivery::Received;
  uint8_t reason = 0;         // Why it failed: Meshtastic's Routing error, or one of s1_mesh's.
  char text[kTextBytes+1] = {};
};
// The history kept on the Cardputer's own flash: a tag, the version, the count, then per message
// from, id, to and seq (4 bytes each, little-endian), flags, status, reason, length and the text.
constexpr uint8_t kHistoryVersion = 1;
constexpr size_t kHistoryBytes = 6 + kMessages * (16 + 4 + kTextBytes);

struct State {
  std::array<Node,kNodes> nodes{};
  std::array<Message,kMessages> messages{};
  size_t nodeCount = 0, messageCount = 0;
  bool nodeView = false;
  bool detailView = false;
  bool helpView = false;
  size_t nodeSel = 0, historyOffset = 0;
  uint8_t target = 0;  // Who Enter sends to: 0 is the public channel, then each contact in turn.
  char input[kTextBytes+1] = {};
  Node* hear(uint32_t id, uint32_t now, int16_t rssi) {
    if (!id || id == UINT32_MAX) return nullptr;
    size_t slot = nodeCount;
    for (size_t i=0;i<nodeCount;++i) if(nodes[i].id==id) { slot=i; break; }
    if (slot == nodeCount && nodeCount == kNodes) {
      slot=0;
      for(size_t i=1;i<kNodes;++i) if(uint32_t(now-nodes[i].seen)>uint32_t(now-nodes[slot].seen)) slot=i;
    }
    if (slot == nodeCount) ++nodeCount;
    if(nodes[slot].id!=id) nodes[slot]=Node{};
    nodes[slot].id=id;nodes[slot].seen=now;nodes[slot].rssi=rssi;
    return &nodes[slot];
  }
  const Node* node(uint32_t id) const {
    for (size_t i=0;i<nodeCount;++i) if (nodes[i].id==id) return &nodes[i];
    return nullptr;
  }
  Message& add() {
    if(messageCount==kMessages) {
      for(size_t i=1;i<kMessages;++i) messages[i-1]=messages[i];
      --messageCount;
    }
    auto& m=messages[messageCount++];m=Message{};
    return m;
  }
  // A message heard on the radio, or one sent at once. A repeat of one already kept is dropped.
  Message* message(uint32_t from, uint32_t id, bool outgoing, const uint8_t* data, size_t size) {
    for(size_t i=0;i<messageCount;++i) if(messages[i].from==from && messages[i].id==id) return nullptr;
    auto& m=add();m.from=from;m.id=id;m.outgoing=outgoing;
    if(outgoing)m.status=Delivery::Sending;
    copyText(m.text,sizeof(m.text),data,size);
    return &m;
  }
  // One of this Cardputer's own, waiting its turn in the outbox, so it has no packet id yet.
  Message& queued(uint32_t from, uint32_t to, bool direct, uint32_t seq, const char* text) {
    auto& m=add();m.from=from;m.to=to;m.seq=seq;m.outgoing=true;m.direct=direct;m.status=Delivery::Waiting;
    copyText(m.text,sizeof(m.text),reinterpret_cast<const uint8_t*>(text),strlen(text));
    return m;
  }
  Message* bySeq(uint32_t seq) {
    if (!seq) return nullptr;
    for (size_t i=0;i<messageCount;++i) if (messages[i].outgoing && messages[i].seq==seq) return &messages[i];
    return nullptr;
  }
  Message* byPacket(uint32_t id) {
    if (!id) return nullptr;
    for (size_t i=0;i<messageCount;++i) if (messages[i].outgoing && messages[i].id==id) return &messages[i];
    return nullptr;
  }
  bool append(const char* text) {
    const size_t a=strlen(input),b=strlen(text);
    if(a+b>kTextBytes)return false;
    memcpy(input+a,text,b+1);return true;
  }
  void erase() {
    size_t n=strlen(input);if(!n)return;
    --n;while(n && (uint8_t(input[n])&0xc0)==0x80)--n;input[n]=0;
  }
  size_t saveHistory(uint8_t* out, size_t capacity) const {
    if (capacity < 6) return 0;
    size_t n = 0;
    out[n++] = 'S'; out[n++] = '1'; out[n++] = 'M'; out[n++] = 'H';
    out[n++] = kHistoryVersion;
    const size_t countAt = n++;
    uint8_t kept = 0;
    for (size_t i = 0; i < messageCount; ++i) {
      const Message& m = messages[i];
      const size_t length = strnlen(m.text, kTextBytes);
      if (n + 20 + length > capacity) break;
      for (uint32_t v : {m.from, m.id, m.to, m.seq})
        for (int b = 0; b < 32; b += 8) out[n++] = uint8_t(v >> b);
      out[n++] = uint8_t((m.outgoing ? 1 : 0) | (m.direct ? 2 : 0));
      out[n++] = uint8_t(m.status);
      out[n++] = m.reason;
      out[n++] = uint8_t(length);
      memcpy(out + n, m.text, length); n += length;
      ++kept;
    }
    out[countAt] = kept;
    return n;
  }
  // Anything that doesn't read back cleanly leaves the history empty, never half-filled. A message
  // that was still going when the Cardputer stopped can't be finished now, so it reads as Unknown.
  bool loadHistory(const uint8_t* in, size_t length) {
    messageCount = 0;
    if (length < 6 || memcmp(in, "S1MH", 4) || in[4] != kHistoryVersion || in[5] > kMessages) return false;
    const size_t count = in[5];
    size_t n = 6;
    for (size_t i = 0; i < count; ++i) {
      if (n + 20 > length) return false;
      uint32_t v[4] = {};
      for (uint32_t& value : v) { for (int b = 0; b < 32; b += 8) value |= uint32_t(in[n++]) << b; }
      const uint8_t flags = in[n++], status = in[n++], reason = in[n++], textLength = in[n++];
      if (flags > 3 || status >= kDeliveryCount || textLength > kTextBytes || n + textLength > length) return false;
      Message& m = messages[i];
      m = Message{};
      m.from = v[0]; m.id = v[1]; m.to = v[2]; m.seq = v[3];
      m.outgoing = (flags & 1) != 0; m.direct = (flags & 2) != 0;
      m.status = Delivery(status); m.reason = reason;
      if (!m.outgoing) m.status = Delivery::Received;
      else if (m.status == Delivery::Waiting || m.status == Delivery::Sending || m.status == Delivery::Received)
        m.status = Delivery::Unknown;
      copyText(m.text, sizeof(m.text), in + n, textLength); n += textLength;
    }
    if (n != length) return false;
    messageCount = count;  // Only now: a bad copy above leaves the count at 0.
    return true;
  }
};
} // namespace s1_mesh
