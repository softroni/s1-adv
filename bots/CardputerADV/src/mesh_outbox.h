#pragma once
#include "mesh_leaf_state.h"
#include <array>
#include <cstdint>
#include <cstring>

// Mesh's outbox: messages go out one at a time, and each one ends in a Delivery.
//
// One at a time because the library keeps a single send waiting for an answer, and a second
// reliable send would quietly take its place. The library also says nothing about how a send
// ended, so the outbox reads it from what the library does:
// - The person's radio answering is a Routing ack, which arrives through receive (routing()).
// - Another radio repeating the message makes the library stop retrying while the radio is quiet:
//   it heard its own packet come back, which Meshtastic counts as an answer for a channel message.
// - Running out of tries makes it stop retrying as it puts the last try on the air.
namespace s1_mesh {
constexpr size_t kOutbox = 8;
// After a radio repeated a direct message, how long the person's radio has to answer.
constexpr uint32_t kAckWaitMs = 30000;
// After the last try went out, how long an answer to it may take.
constexpr uint32_t kLastTryWaitMs = 30000;
// The radio or the channel was busy, or the region's airtime limit was reached.
constexpr uint32_t kBusyRetryMs = 1500, kAirtimeRetryMs = 60000;

// The library's SendResult values, by number, as the outbox sees them.
namespace send_result {
constexpr uint8_t kOk = 0, kTxBusy = 1, kChannelBusy = 2, kDutyCycle = 3;
}
// Reasons this side adds to Meshtastic's Routing errors, which stop below 64.
constexpr uint8_t kReasonLocal = 200;   // 200 plus the library's SendResult.
constexpr uint8_t kReasonEncode = 250;  // The library refused it without saying why.
constexpr uint16_t kPortText = 1, kPortPosition = 3;

struct Outgoing {
  uint32_t seq = 0;  // The history's number for it, or 0 for one that isn't shown (a position).
  uint32_t to = kBroadcast;
  uint16_t port = kPortText;
  bool pki = false;       // Encrypted to the person's own key.
  bool reliable = true;   // Asks for an answer, and is retried until one comes.
  uint8_t channel = 0;    // 1 is the private channel, for a channel message; direct messages ignore it.
  uint8_t len = 0;
  uint8_t data[kTextBytes] = {};
  uint8_t key[32] = {};
};

class Outbox {
 public:
  bool push(const Outgoing& o) {
    if (mCount == kOutbox || o.len > kTextBytes) return false;
    mQueue[(mHead + mCount) % kOutbox] = o;
    ++mCount;
    return true;
  }
  size_t queued() const { return mCount; }
  bool idle() const { return !mBusy && !mCount; }
  bool busy() const { return mBusy; }
  uint32_t currentId() const { return mBusy ? mCur.id : 0; }
  uint32_t currentSeq() const { return mBusy ? mCur.seq : 0; }
  bool holds(uint32_t seq) const {
    if (!seq) return false;
    if (mBusy && mCur.seq == seq) return true;
    for (size_t i = 0; i < mCount; ++i) if (mQueue[(mHead + i) % kOutbox].seq == seq) return true;
    return false;
  }

  // Radio: uint32_t send(const Outgoing&), uint8_t lastResult(), bool pending(), bool transmitting().
  // Sink: void status(uint32_t seq, Delivery, uint8_t reason, uint32_t packetId).
  template <class Radio, class Sink>
  void service(uint32_t now, Radio& radio, Sink& sink) {
    if (mBusy) { track(now, radio, sink); return; }
    if (!mCount) return;
    if (mRetry && int32_t(now - mRetryAt) < 0) return;
    // An ack, a NodeInfo or a reliable send of the library's own goes first.
    if (radio.transmitting() || radio.pending()) return;
    const Outgoing& o = mQueue[mHead];
    const uint32_t id = radio.send(o);
    if (!id) {
      const uint8_t result = radio.lastResult();
      if (result == send_result::kTxBusy || result == send_result::kChannelBusy) {
        mRetry = true; mRetryAt = now + kBusyRetryMs;
        return;
      }
      if (result == send_result::kDutyCycle) {
        mRetry = true; mRetryAt = now + kAirtimeRetryMs;
        sink.status(o.seq, Delivery::Waiting, uint8_t(kReasonLocal + result), 0);
        return;
      }
      sink.status(o.seq, Delivery::Failed, result == send_result::kOk ? kReasonEncode : uint8_t(kReasonLocal + result), 0);
      pop();
      mRetry = false;
      return;
    }
    mRetry = false;
    mCur = Current{};
    mCur.seq = o.seq; mCur.id = id; mCur.to = o.to; mCur.reliable = o.reliable;
    pop();
    mBusy = true;
    sink.status(mCur.seq, Delivery::Sending, 0, id);
  }

  // A Routing message for one of this Cardputer's packets. Returns false when it isn't about the
  // message in flight, so a late answer can still be matched against the history.
  template <class Sink>
  bool routing(uint32_t from, uint32_t requestId, uint8_t error, Sink& sink) {
    if (!mBusy || !requestId || requestId != mCur.id) return false;
    // A channel message has no one radio to answer for it.
    if (mCur.to == kBroadcast) return true;
    if (error == 0) {
      if (from != mCur.to) return true;  // Only the person's own radio can say it arrived.
      sink.status(mCur.seq, Delivery::Delivered, 0, mCur.id);
    } else {
      sink.status(mCur.seq, Delivery::Failed, error, mCur.id);
    }
    finish();
    return true;
  }

 private:
  enum class Phase : uint8_t { Trying, Waiting };
  struct Current {
    uint32_t seq = 0, id = 0, to = kBroadcast, deadline = 0;
    bool reliable = true, repeated = false;
    Phase phase = Phase::Trying;
  };

  template <class Radio, class Sink>
  void track(uint32_t now, Radio& radio, Sink& sink) {
    if (!mCur.reliable) {  // Sent once, with nothing to wait for.
      if (!radio.transmitting()) { sink.status(mCur.seq, Delivery::Heard, 0, mCur.id); finish(); }
      return;
    }
    if (mCur.phase == Phase::Trying) {
      if (radio.pending()) return;
      if (radio.transmitting()) {
        // The library stopped retrying as it put the last try on the air.
        mCur.phase = Phase::Waiting;
        mCur.deadline = now + kLastTryWaitMs;
        return;
      }
      // It stopped with the radio quiet: another radio repeated it.
      mCur.repeated = true;
      sink.status(mCur.seq, Delivery::Heard, 0, mCur.id);
      if (mCur.to == kBroadcast) { finish(); return; }
      mCur.phase = Phase::Waiting;
      mCur.deadline = now + kAckWaitMs;
      return;
    }
    if (int32_t(now - mCur.deadline) < 0) return;
    if (mCur.repeated) sink.status(mCur.seq, Delivery::NotConfirmed, 0, mCur.id);
    else sink.status(mCur.seq, Delivery::NoAnswer, 0, mCur.id);
    finish();
  }
  void pop() {
    mQueue[mHead] = Outgoing{};
    mHead = (mHead + 1) % kOutbox;
    --mCount;
  }
  void finish() { mBusy = false; mCur = Current{}; }

  std::array<Outgoing,kOutbox> mQueue{};
  size_t mHead = 0, mCount = 0;
  Current mCur;
  bool mBusy = false, mRetry = false;
  uint32_t mRetryAt = 0;
};

// A short word for each Delivery, as the chat shows it. A direct message and a channel message
// mean different things by the same state, so the caller says which.
inline const char* deliveryLabel(Delivery d, bool direct) {
  switch (d) {
    case Delivery::Received: return "";
    case Delivery::Waiting: return "waiting";
    case Delivery::Sending: return "sending";
    case Delivery::Heard: return direct ? "relayed" : "heard nearby";
    case Delivery::Delivered: return "delivered";
    case Delivery::NotConfirmed: return "not confirmed";
    case Delivery::NoAnswer: return direct ? "no answer" : "not heard";
    case Delivery::Failed: return "failed";
    case Delivery::Unknown: return "unknown";
  }
  return "";
}
// Why a message failed, in a few words.
inline const char* failureLabel(uint8_t reason) {
  switch (reason) {
    case 35: return "they don't have your key yet";   // PKI_UNKNOWN_PUBKEY
    case 34: case 39: return "encryption failed";     // PKI_FAILED, PKI_SEND_FAIL_PUBLIC_KEY
    case 6: return "their radio can't read it";       // NO_CHANNEL
    case 7: return "too long";                        // TOO_LARGE
    case 1: case 5: return "no way through";          // NO_ROUTE, MAX_RETRANSMIT
    case 38: return "too many messages";              // RATE_LIMIT_EXCEEDED
    case kReasonLocal + 3: return "airtime limit";    // DUTY_CYCLE
    case kReasonLocal + 4: return "no identity";      // NO_IDENTITY
    case kReasonLocal + 6: case kReasonEncode: return "too long";
    case kReasonLocal + 8: return "radio error";      // RADIO_ERROR
    default: return "refused";
  }
}
}  // namespace s1_mesh
