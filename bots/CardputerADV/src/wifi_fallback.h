// Which saved network start-up dials next when the one in use can't be joined. The rest of the
// saved list goes in its own order, most recently used first, each once, and only when every one
// has failed does start-up go on to the setup portal. Names only: a password stays on the card
// until its network is dialed. Pure, so it runs on the host.
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

}  // namespace wifi_fallback
