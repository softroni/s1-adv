#pragma once
#include <MeshRegion.h>
#include <cstddef>
#include <cstdint>

// The Meshtastic region Mesh uses, picked in Settings > Device. Every one here is on the
// LongFast public channel and inside the band M5 rates the Cap for (868 to 923 MHz, which
// means up to the AS923 channels). US comes first because it's the default.
namespace s1_mesh {
using libmeshtastic_leaf::RegionCode;
constexpr RegionCode kRegions[] = {
  libmeshtastic_leaf::REGION_US,     libmeshtastic_leaf::REGION_ANZ,    libmeshtastic_leaf::REGION_BR_902,
  libmeshtastic_leaf::REGION_EU_868, libmeshtastic_leaf::REGION_JP,     libmeshtastic_leaf::REGION_KR,
  libmeshtastic_leaf::REGION_MY_919, libmeshtastic_leaf::REGION_PH_868, libmeshtastic_leaf::REGION_PH_915,
  libmeshtastic_leaf::REGION_RU,     libmeshtastic_leaf::REGION_SG_923, libmeshtastic_leaf::REGION_TH,
  libmeshtastic_leaf::REGION_TW,     libmeshtastic_leaf::REGION_UA_868,
};
constexpr size_t kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);
constexpr RegionCode kDefaultRegion = libmeshtastic_leaf::REGION_US;

inline size_t regionIndex(uint8_t code) {
  for (size_t i = 0; i < kRegionCount; ++i) if (kRegions[i] == code) return i;
  return kRegionCount;
}
// A saved value from another firmware, or no value at all, means the default.
inline RegionCode regionOrDefault(uint8_t code) {
  const size_t i = regionIndex(code);
  return i < kRegionCount ? kRegions[i] : kDefaultRegion;
}
inline RegionCode stepRegion(uint8_t code, int delta) {
  const size_t i = regionIndex(regionOrDefault(code));
  return kRegions[(i + (delta < 0 ? kRegionCount - 1 : 1)) % kRegionCount];
}
inline const char* regionName(uint8_t code) {
  return libmeshtastic_leaf::MeshRegion::getRegionName(regionOrDefault(code));
}
// The public channel's centre, worked out the way Meshtastic does it.
inline float regionFrequency(uint8_t code) {
  return libmeshtastic_leaf::MeshRegion::getFrequency(regionOrDefault(code), libmeshtastic_leaf::PRESET_LONG_FAST, "");
}
// The region's whole band, which Radio Scan sweeps.
inline void regionBand(uint8_t code, float& start, float& end) {
  const libmeshtastic_leaf::RegionInfo* info = libmeshtastic_leaf::MeshRegion::getRegion(regionOrDefault(code));
  start = info ? info->freqStart : 902.0f;
  end = info ? info->freqEnd : 928.0f;
}

// The presets Radio Scan can listen with, LongFast first. Some local meshes use another preset,
// and each one has its own default channel, so listening on LongFast alone would miss them.
constexpr libmeshtastic_leaf::ModemPreset kScanPresets[] = {
  libmeshtastic_leaf::PRESET_LONG_FAST,   libmeshtastic_leaf::PRESET_MEDIUM_FAST, libmeshtastic_leaf::PRESET_MEDIUM_SLOW,
  libmeshtastic_leaf::PRESET_LONG_MODERATE, libmeshtastic_leaf::PRESET_SHORT_FAST, libmeshtastic_leaf::PRESET_SHORT_SLOW,
};
constexpr size_t kScanPresetCount = sizeof(kScanPresets) / sizeof(kScanPresets[0]);
inline libmeshtastic_leaf::ModemPreset scanPreset(uint8_t index) { return kScanPresets[index % kScanPresetCount]; }
inline const char* scanPresetName(uint8_t index) {
  return libmeshtastic_leaf::MeshRegion::getPresetName(scanPreset(index));
}
inline float scanPresetFrequency(uint8_t region, uint8_t index) {
  return libmeshtastic_leaf::MeshRegion::getFrequency(regionOrDefault(region), scanPreset(index), "");
}
inline libmeshtastic_leaf::ModemParams scanPresetModem(uint8_t index) {
  return libmeshtastic_leaf::MeshRegion::getModemParams(scanPreset(index), false);
}
}  // namespace s1_mesh
