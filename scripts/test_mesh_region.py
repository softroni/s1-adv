#!/usr/bin/env python3
"""Check Mesh's region list against the real Meshtastic leaf library's table.

The library is fetched by the firmware build, so this runs after it (CI does).
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# The library is in the repository (patched), and Nanopb comes with the firmware build.
leaf = ROOT / 'bots/CardputerADV/lib/libmeshtastic-leaf/src'
deps = sorted((ROOT / 'bots/CardputerADV/.pio/libdeps').glob('*/Nanopb'))
if not deps:
    sys.exit('Nanopb not found: build the firmware first (python scripts/cardputer.py build)')
nanopb = deps[0]

CODE = r'''
#include "mesh_region.h"
#include "radio_scan.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
using namespace libmeshtastic_leaf;
using namespace s1_mesh;
static bool near(float a, float b) { return std::fabs(a - b) < 0.0005f; }
int main() {
  // Nothing saved is US, on Meshtastic's published US LongFast channel.
  assert(kRegions[0] == REGION_US && kDefaultRegion == REGION_US);
  assert(near(regionFrequency(0xff), 906.875f) && std::strcmp(regionName(0xff), "US") == 0);
  // Channels Meshtastic users know, so a wrong table can't pass.
  assert(near(regionFrequency(REGION_EU_868), 869.525f));
  assert(near(regionFrequency(REGION_ANZ), 919.875f));
  assert(near(regionFrequency(REGION_TH), 923.875f));
  // M5 rates the Cap for 868 to 923 MHz, meaning the AS923 channels, whose top one ends at 924.
  // Each listed region defaults to LongFast there, allows our fixed 10 dBm, and is listed once.
  std::set<int> seen;
  for (RegionCode code : kRegions) {
    const RegionInfo* info = MeshRegion::getRegion(code);
    assert(info && info->code == code && info->defaultPreset == PRESET_LONG_FAST && !info->licensedOnly());
    const float f = regionFrequency(code);
    assert(f - 0.125f >= 868.0f && f + 0.125f <= 924.0f && f > info->freqStart && f < info->freqEnd);
    assert(info->powerLimit >= 10 && seen.insert(code).second);
    assert(regionOrDefault(code) == code && std::strcmp(regionName(code), info->name) == 0);
  }
  // And no region that fits is left out.
  size_t count = 0;
  const RegionInfo* all = MeshRegion::getAllRegions(count);
  for (size_t i = 0; i < count; ++i) {
    const RegionInfo& r = all[i];
    if (r.code == REGION_UNSET || r.defaultPreset != PRESET_LONG_FAST || r.licensedOnly()) continue;
    const float f = MeshRegion::getFrequency(r.code, PRESET_LONG_FAST, "");
    if (f - 0.125f >= 868.0f && f + 0.125f <= 924.0f) assert(seen.count(r.code));
    else assert(!seen.count(r.code) && regionOrDefault(r.code) == REGION_US);
  }
  // Below the Cap's band, other presets, and values no firmware wrote all fall back to US.
  for (int code : {0, 255, int(REGION_IN), int(REGION_NZ_865), int(REGION_EU_866), int(REGION_EU_433), int(REGION_LORA_24)})
    assert(regionOrDefault(uint8_t(code)) == REGION_US);
  // Left and right visit every region once and wrap, from any start.
  for (size_t start = 0; start < kRegionCount; ++start) for (int delta : {1, -1}) {
    std::set<int> visited;
    uint8_t code = kRegions[start];
    for (size_t step = 0; step < kRegionCount; ++step) { visited.insert(code); code = stepRegion(code, delta); }
    assert(code == kRegions[start] && visited.size() == kRegionCount);
  }
  assert(stepRegion(REGION_US, -1) == kRegions[kRegionCount - 1] && stepRegion(0xff, 1) == kRegions[1]);
  // Radio Scan: LongFast first, every preset's own channel inside each region's band, and each
  // region's sweep covering its public channel.
  assert(kScanPresets[0] == PRESET_LONG_FAST && std::strcmp(scanPresetName(0), "LongFast") == 0);
  std::set<std::string> names;
  for (size_t i = 0; i < kScanPresetCount; ++i) {
    const ModemParams m = scanPresetModem(uint8_t(i));
    assert(m.sf >= 7 && m.sf <= 12 && (m.bw == 125.0f || m.bw == 250.0f) && m.cr >= 5 && m.cr <= 8);
    assert(names.insert(scanPresetName(uint8_t(i))).second);
  }
  assert(scanPreset(uint8_t(kScanPresetCount)) == PRESET_LONG_FAST);
  for (RegionCode code : kRegions) {
    float start = 0, end = 0;
    regionBand(code, start, end);
    assert(start == MeshRegion::getRegion(code)->freqStart && end == MeshRegion::getRegion(code)->freqEnd);
    // The centre, not the whole width: the library's slot sum (the one Mesh chat uses too) puts
    // PH_868's LongFast at 869.375 MHz, whose top edge is 0.1 MHz past that band's 869.4.
    for (size_t i = 0; i < kScanPresetCount; ++i) {
      const float f = scanPresetFrequency(code, uint8_t(i));
      assert(f > start && f < end);
    }
    const s1_scan::Plan plan = s1_scan::planFor(start, end);
    assert(plan.pointAt(regionFrequency(code)) < plan.points && plan.start >= 860.0f && plan.end() <= 930.0f);
  }
  const s1_scan::Plan us = s1_scan::planFor(902.0f, 928.0f);
  assert(near(us.frequency(us.pointAt(906.875f)), 906.875f));  // The US sweep lands right on LongFast.
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-region-') as directory:
    p = Path(directory)
    (p / 'region.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'bots/CardputerADV/src'), '-I' + str(leaf), '-I' + str(nanopb),
                    str(p / 'region.cpp'), '-o', str(p / 'region')], check=True)
    subprocess.run([str(p / 'region')], check=True)
print('PASS Mesh regions against the real library: US by default at 906.875 MHz, every region inside the Cap band on LongFast and none missing, unknown values fall back to US, left/right wrap, and Radio Scan presets on their own channels inside every band')
