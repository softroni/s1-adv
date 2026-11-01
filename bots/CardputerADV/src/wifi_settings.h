// Switching Wi-Fi from the device. A network is saved only after the device has joined it, so
// a wrong password can't replace the network that works. Nothing here touches the radio
// directly: it's handed one, which lets every failure path run on the host.
//
// Radio needs:
//   bool online();                  Joined any network, with an address.
//   bool busy();                    A request or a recording needs the current link.
//   void hold();                    Stop dialing, so that a scan can start.
//   bool startScan();
//   int  scanResults(std::vector<Network>&);   kScanRunning, kScanFailed, or a count.
//   void stopScan();
//   void join(ssid, password);
//   bool joined(ssid);              On exactly this network, with an address.
//   bool notFound();
//   void rejoinSaved();             Go back to the saved network and resume dialing.
//   void save(ssid, password);
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace wifi_settings {

constexpr size_t kMaxNameBytes = 32;       // 802.11.
constexpr size_t kMinPasswordBytes = 8;    // WPA.
constexpr size_t kMaxPasswordBytes = 64;
constexpr size_t kMaxNetworks = 16;
constexpr uint32_t kJoinTimeoutMs = 20000;
// Status can still describe the old link just after leaving it, and it would look like a join.
constexpr uint32_t kJoinSettleMs = 1000;
constexpr uint32_t kScanTimeoutMs = 16000;
constexpr uint32_t kScanRetryMs = 1000;
constexpr uint8_t kScanTries = 5;
constexpr int kScanRunning = -1;
constexpr int kScanFailed = -2;

struct Network {
  std::string ssid;
  int rssi = -100;
  bool secured = true;
};

enum class Screen : uint8_t { List, Name, Password, Joining };
enum class Mood : uint8_t { Plain, Good, Bad };

inline void wipe(std::string& secret) {
  std::fill(secret.begin(), secret.end(), '\0');
  secret.clear();
}

// Strongest first, one row per name. A hidden network has no name to show.
inline std::vector<Network> tidy(std::vector<Network> found) {
  std::stable_sort(found.begin(), found.end(),
                   [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  std::vector<Network> out;
  for (auto& network : found) {
    if (out.size() == kMaxNetworks) break;
    if (network.ssid.empty()) continue;
    const bool seen = std::any_of(out.begin(), out.end(), [&](const Network& kept) {
      return kept.ssid == network.ssid;
    });
    if (!seen) out.push_back(std::move(network));
  }
  return out;
}

inline uint8_t signalBars(int rssi) {
  return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
}

// Hides all but the last character, as a phone does, so a slip on this keyboard shows.
inline std::string masked(const std::string& password) {
  if (password.empty()) return "";
  return std::string(password.size() - 1, '*') + password.back();
}

class App {
 public:
  Screen screen = Screen::List;
  std::vector<Network> networks;
  size_t selected = 0;  // networks.size() is the "Other network" row.
  std::string name, password, message;
  Mood mood = Mood::Plain;
  bool secured = true;  // False for an open network, and for a typed name, which might be one.
  bool revealed = false;
  bool scanned = false;
  bool changed = false;

  size_t rows() const { return networks.size() + 1; }
  bool typing() const { return screen == Screen::Name || screen == Screen::Password; }
  bool scanning() const { return mScan != Scan::Idle; }
  bool ownsRadio() const { return mHeld; }
  uint32_t joinElapsed(uint32_t now) const { return now - mJoinAt; }

  void rescan(uint32_t now) {
    if (scanning()) return;
    mScan = Scan::Waiting;
    mScanAt = now;
    mScanTries = kScanTries;
    say("");
  }
  void move(int delta) {
    const int count = static_cast<int>(rows());
    selected = static_cast<size_t>((static_cast<int>(selected) + count + delta) % count);
    say("");
  }
  void type(char c) {
    if (c < 32 || c > 126) return;
    std::string& field = screen == Screen::Name ? name : password;
    if (field.size() >= (screen == Screen::Name ? kMaxNameBytes : kMaxPasswordBytes)) return;
    field.push_back(c);
    say("");
  }
  void backspace() {
    std::string& field = screen == Screen::Name ? name : password;
    if (!field.empty()) field.pop_back();
    say("");
  }
  void toggleReveal() {
    revealed = !revealed;
    changed = true;
  }

  template <class Radio>
  void enter(Radio& radio, uint32_t now) {
    say("");
    if (screen == Screen::List) {
      if (selected >= networks.size()) {
        name.clear();
        mTypedName = true;
        secured = false;
        show(Screen::Name);
        return;
      }
      name = networks[selected].ssid;
      mTypedName = false;
      secured = networks[selected].secured;
      if (secured) show(Screen::Password);
      else join(radio, now);
    } else if (screen == Screen::Name) {
      if (name.empty()) say("Type the network's name", Mood::Bad);
      else show(Screen::Password);
    } else if (screen == Screen::Password) {
      const bool tooShort = password.empty() ? secured : password.size() < kMinPasswordBytes;
      if (tooShort) say("Use at least 8 characters", Mood::Bad);
      else join(radio, now);
    }
  }

  // Returns false when there's nothing left to back out of.
  template <class Radio>
  bool escape(Radio& radio) {
    switch (screen) {
      case Screen::Joining:
        radio.rejoinSaved();
        mHeld = false;
        show(afterJoin());
        say("Cancelled");
        return true;
      case Screen::Password:
        wipe(password);
        revealed = false;
        show(mTypedName ? Screen::Name : Screen::List);
        say("");
        return true;
      case Screen::Name:
        name.clear();
        show(Screen::List);
        say("");
        return true;
      case Screen::List:
        return false;
    }
    return false;
  }

  template <class Radio>
  void close(Radio& radio) {
    if (scanning()) radio.stopScan();
    if (mHeld) radio.rejoinSaved();
    wipe(password);
    *this = App{};
  }

  template <class Radio>
  void poll(Radio& radio, uint32_t now) {
    if (screen == Screen::Joining) pollJoin(radio, now);
    else if (scanning()) pollScan(radio, now);
  }

 private:
  enum class Scan : uint8_t { Idle, Waiting, Running };

  void say(const std::string& text, Mood how = Mood::Plain) {
    message = text;
    mood = how;
    changed = true;
  }
  void show(Screen next) {
    screen = next;
    changed = true;
  }
  Screen afterJoin() const { return mTypedName || secured ? Screen::Password : Screen::List; }

  template <class Radio>
  void join(Radio& radio, uint32_t now) {
    if (radio.busy()) {
      say("Finish the current request first", Mood::Bad);
      return;
    }
    if (scanning()) radio.stopScan();
    mScan = Scan::Idle;
    mNotFound = false;
    revealed = false;
    mHeld = true;
    mJoinAt = now;
    radio.join(name, password);
    show(Screen::Joining);
  }

  template <class Radio>
  void pollJoin(Radio& radio, uint32_t now) {
    const uint32_t elapsed = now - mJoinAt;
    if (radio.notFound()) mNotFound = true;
    if (elapsed >= kJoinSettleMs && radio.joined(name)) {
      radio.save(name, password);
      mHeld = false;
      wipe(password);
      show(Screen::List);
      say("Connected to " + name, Mood::Good);
      return;
    }
    if (elapsed < kJoinTimeoutMs) return;
    radio.rejoinSaved();
    mHeld = false;
    show(afterJoin());
    if (mNotFound) say("Not found. Is it 2.4 GHz?", Mood::Bad);
    else say(secured || mTypedName ? "Couldn't join. Check the password" : "Couldn't join that network",
             Mood::Bad);
  }

  template <class Radio>
  void pollScan(Radio& radio, uint32_t now) {
    if (mScan == Scan::Waiting) {
      if (static_cast<int32_t>(now - mScanAt) < 0) return;
      // A scan can't start while the station is dialing a network that isn't here.
      if (!mHeld && !radio.online()) {
        mHeld = true;
        radio.hold();
      }
      if (radio.startScan()) {
        mScan = Scan::Running;
        mScanAt = now;
      } else if (--mScanTries == 0) {
        endScan("Scan failed. R to try again");
      } else {
        mScanAt = now + kScanRetryMs;
      }
      return;
    }
    std::vector<Network> found;
    const int count = radio.scanResults(found);
    if (count == kScanRunning) {
      if (now - mScanAt < kScanTimeoutMs) return;
      radio.stopScan();
      endScan("Scan failed. R to try again");
      return;
    }
    if (count < 0) {
      endScan("Scan failed. R to try again");
      return;
    }
    const bool onOther = !networks.empty() && selected == networks.size();
    const std::string keep = selected < networks.size() ? networks[selected].ssid : "";
    networks = tidy(std::move(found));
    selected = onOther ? networks.size() : 0;
    for (size_t i = 0; i < networks.size(); ++i) {
      if (networks[i].ssid == keep) selected = i;
    }
    scanned = true;
    endScan(networks.empty() ? "No networks found. R to try again" : "");
  }

  void endScan(const std::string& text) {
    mScan = Scan::Idle;
    if (text.empty()) changed = true;
    else say(text, Mood::Bad);
  }

  Scan mScan = Scan::Idle;
  uint32_t mScanAt = 0, mJoinAt = 0;
  uint8_t mScanTries = 0;
  bool mHeld = false, mTypedName = false, mNotFound = false;
};

}  // namespace wifi_settings
