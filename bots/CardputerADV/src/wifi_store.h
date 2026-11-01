// Saved Wi-Fi networks, kept on the SD card so that they outlive a firmware install. The card
// is removable, so the file is sealed and holds no readable password. The disk and the cipher
// are handed in, which lets a power cut at any step, and a damaged card, run on the host.
//
// Disk needs:
//   bool exists(const char* path);
//   bool read(const char* path, std::vector<uint8_t>& out);
//   bool write(const char* path, const std::vector<uint8_t>& bytes);
//   bool remove(const char* path);
//   bool rename(const char* from, const char* to);
// Cipher needs:
//   bool seal(const std::vector<uint8_t>& plain, std::vector<uint8_t>& sealed);
//   bool open(const std::vector<uint8_t>& sealed, std::vector<uint8_t>& plain);
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace wifi_store {

constexpr const char* kDir = "/wifi";
constexpr const char* kPath = "/wifi/networks.dat";
constexpr const char* kNewPath = "/wifi/networks.new";
constexpr const char* kBadPath = "/wifi/networks.bad";
constexpr size_t kMaxSaved = 20;
constexpr size_t kMaxNameBytes = 32;       // 802.11.
constexpr size_t kMaxPasswordBytes = 64;
constexpr size_t kMaxFileBytes = 4096;
constexpr uint8_t kMagic[4] = {'S', '1', 'W', '1'};

struct Saved {
  std::string ssid, password;
};
using List = std::vector<Saved>;

enum class Load : uint8_t { Ok, Empty, Unreadable };

// The compiler may not drop these writes, unlike a fill just before a buffer is released.
inline void zero(void* data, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (size--) *bytes++ = 0;
}
inline void wipe(std::string& secret) {
  zero(&secret[0], secret.size());
  secret.clear();
}
inline void wipe(std::vector<uint8_t>& bytes) {
  zero(bytes.data(), bytes.size());
  bytes.clear();
}
inline void wipe(List& list) {
  for (auto& saved : list) wipe(saved.password);
  list.clear();
}

inline const Saved* find(const List& list, const std::string& ssid) {
  for (const auto& saved : list) {
    if (saved.ssid == ssid) return &saved;
  }
  return nullptr;
}

// Moves the network to the top, most recently used first. Returns false if nothing changed.
inline bool remember(List& list, const std::string& ssid, const std::string& password) {
  if (!list.empty() && list.front().ssid == ssid && list.front().password == password) return false;
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].ssid == ssid) {
      wipe(list[i].password);
      list.erase(list.begin() + i);
      break;
    }
  }
  list.insert(list.begin(), Saved{ssid, password});
  while (list.size() > kMaxSaved) {
    wipe(list.back().password);
    list.pop_back();
  }
  return true;
}

inline bool forget(List& list, const std::string& ssid) {
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].ssid == ssid) {
      wipe(list[i].password);
      list.erase(list.begin() + i);
      return true;
    }
  }
  return false;
}

inline std::vector<uint8_t> encode(const List& list) {
  std::vector<uint8_t> out;
  out.push_back(static_cast<uint8_t>(list.size()));
  for (const auto& saved : list) {
    out.push_back(static_cast<uint8_t>(saved.ssid.size()));
    out.insert(out.end(), saved.ssid.begin(), saved.ssid.end());
    out.push_back(static_cast<uint8_t>(saved.password.size()));
    out.insert(out.end(), saved.password.begin(), saved.password.end());
  }
  return out;
}

inline bool decode(const std::vector<uint8_t>& bytes, List& out) {
  out.clear();
  size_t at = 0;
  const auto field = [&](size_t least, size_t most, std::string& value) {
    if (at >= bytes.size()) return false;
    const size_t size = bytes[at++];
    if (size < least || size > most || size > bytes.size() - at) return false;
    value.assign(bytes.begin() + at, bytes.begin() + at + size);
    at += size;
    return true;
  };
  if (bytes.empty() || bytes[0] > kMaxSaved) return false;
  const size_t count = bytes[at++];
  for (size_t i = 0; i < count; ++i) {
    Saved saved;
    if (!field(1, kMaxNameBytes, saved.ssid) || !field(0, kMaxPasswordBytes, saved.password)) {
      wipe(saved.password);
      wipe(out);
      return false;
    }
    out.push_back(std::move(saved));
  }
  if (at != bytes.size()) {
    wipe(out);
    return false;
  }
  return true;
}

template <class Disk, class Cipher>
bool readFile(Disk& disk, Cipher& cipher, const char* path, List& out) {
  std::vector<uint8_t> file, plain;
  if (!disk.read(path, file) || file.size() <= sizeof(kMagic) ||
      !std::equal(kMagic, kMagic + sizeof(kMagic), file.begin())) {
    return false;
  }
  const std::vector<uint8_t> sealed(file.begin() + sizeof(kMagic), file.end());
  const bool ok = cipher.open(sealed, plain) && decode(plain, out);
  wipe(plain);
  return ok;
}

template <class Disk, class Cipher>
Load load(Disk& disk, Cipher& cipher, List& out) {
  out.clear();
  // A whole .new is a save that lost power before the swap. It's the newer list. A partial one
  // fails its seal, and then the old file still stands.
  if (disk.exists(kNewPath)) {
    if (readFile(disk, cipher, kNewPath, out)) {
      disk.remove(kPath);
      disk.rename(kNewPath, kPath);
      return Load::Ok;
    }
    disk.remove(kNewPath);
  }
  if (!disk.exists(kPath)) return Load::Empty;
  if (readFile(disk, cipher, kPath, out)) return Load::Ok;
  // Moved aside, so the next save can't destroy what might only have been a bad read.
  disk.remove(kBadPath);
  disk.rename(kPath, kBadPath);
  return Load::Unreadable;
}

template <class Disk, class Cipher>
bool store(Disk& disk, Cipher& cipher, const List& list) {
  std::vector<uint8_t> plain = encode(list), sealed;
  const bool ok = cipher.seal(plain, sealed);
  wipe(plain);
  if (!ok) return false;
  std::vector<uint8_t> file(kMagic, kMagic + sizeof(kMagic)), back;
  file.insert(file.end(), sealed.begin(), sealed.end());
  // The old list goes only once the new one has been read back whole.
  if (!disk.write(kNewPath, file) || !disk.read(kNewPath, back) || back != file) {
    disk.remove(kNewPath);
    return false;
  }
  if (disk.exists(kPath)) disk.remove(kPath);
  return disk.rename(kNewPath, kPath);
}

}  // namespace wifi_store
