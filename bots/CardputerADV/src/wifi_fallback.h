// Which saved network to dial next when the one in use can't be joined: at start-up, and when it's
// lost while running. The rest of the saved list goes in its own order, most recently used first,
// each once. At start-up the setup portal comes only after every one has failed. While running, the
// lost network is dialed again after them, with a rest before the next round. Names only: a
// password stays on the card until its network is dialed. Pure, so it runs on the host.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wifi_fallback {

constexpr uint32_t kJoinTimeoutMs = 20000;
// The radio reports a network missing after one sweep of the channels. Two sweeps, then move on.
constexpr uint32_t kNotFoundMs = 6000;

class Dial {
 public:
  // The network in use is dialed first, by start-up itself. It may be empty, or not on the list.
  void begin(const std::string& current, const std::vector<std::string>& saved) {
    mQueue.clear();
    mNext = 0;
    for (const auto& name : saved) {
      if (name.empty() || name == current) continue;
      bool twice = false;
      for (const auto& kept : mQueue) twice = twice || kept == name;
      if (!twice) mQueue.push_back(name);
    }
  }
  bool another() const { return mNext < mQueue.size(); }
  size_t queued() const { return mQueue.size(); }
  // The next saved network to dial, or "" when all have been tried.
  std::string next() { return another() ? mQueue[mNext++] : std::string(); }
  // A network the radio can't see is left after kNotFoundMs, but only for another one to try. The
  // last gets the full wait, as before this, in case its router is still starting up.
  bool giveUp(uint32_t elapsedMs, bool notFound) const {
    return elapsedMs >= kJoinTimeoutMs || (notFound && another() && elapsedMs >= kNotFoundMs);
  }

 private:
  std::vector<std::string> mQueue;
  size_t mNext = 0;
};

// While running. A blip, or a router restarting, gets kRoamAfterMs to come back by itself, and a
// network the radio says is gone gets kRoamAfterMissingMs. After a round that joins nothing, the
// lost one is dialed again and left to the usual reconnects for kRoamRestMs, so a long outage
// isn't all dialing.
constexpr uint32_t kRoamAfterMs = 20000;
constexpr uint32_t kRoamAfterMissingMs = 8000;
constexpr uint32_t kRoamRestMs = 60000;

class Roam {
 public:
  enum class Act : uint8_t { Wait, Dial, BackToFirst };
  // The link is up, on whichever network.
  void up() {
    if (!mDown) return;
    *this = Roam{};
  }
  bool dialing() const { return mDialing; }
  bool another() const { return mDial.another(); }
  std::string next() { return mDial.next(); }
  // Each pass while the link is down. `notFound` is the radio saying the network it's dialing
  // isn't there. `names` gives the saved networks, and is called only when a round begins.
  template <class Names>
  Act down(uint32_t now, bool notFound, const std::string& current, Names names) {
    if (!mDown) {
      mDown = true;
      mSince = now;
    }
    if (mDialing) {
      if (!mDial.giveUp(now - mDialAt, notFound)) return Act::Wait;
      mDialAt = now;
      if (mDial.another()) return Act::Dial;
      mDialing = false;
      rest(now);
      return Act::BackToFirst;
    }
    if (mResting) {
      if (static_cast<int32_t>(now - mRestUntil) < 0) return Act::Wait;
    } else if (now - mSince < (notFound ? kRoamAfterMissingMs : kRoamAfterMs)) {
      return Act::Wait;
    }
    mDial.begin(current, names());
    if (!mDial.another()) {
      rest(now);
      return Act::Wait;
    }
    mDialing = true;
    mDialAt = now;
    return Act::Dial;
  }

 private:
  void rest(uint32_t now) {
    mResting = true;
    mRestUntil = now + kRoamRestMs;
  }
  Dial mDial;
  bool mDown = false, mDialing = false, mResting = false;
  uint32_t mSince = 0, mDialAt = 0, mRestUntil = 0;
};

}  // namespace wifi_fallback
