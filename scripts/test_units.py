#!/usr/bin/env python3
"""Check units.h: US by default, conversions rounded the way a reading should be, and the words on screen."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CODE = r'''
#include "units.h"
#include <cassert>
#include <string>
using namespace units;
static std::string reading(int v, const char* unit, System s) { char t[32]; readingText(t, sizeof(t), v, unit, "`", s); return t; }
static std::string distance(int m, System s, bool capped = false) { char t[32]; distanceText(t, sizeof(t), m, s, capped); return t; }
static std::string tenths(int v) { char t[16]; tenthsText(t, sizeof(t), v); return t; }
int main() {
  // US unless the owner picked metric, and anything else saved means the default.
  assert(kDefault == System::US && fromSaved(0) == System::US && fromSaved(1) == System::Metric && fromSaved(7) == System::US);
  assert(std::string(name(System::US)) == "US" && std::string(name(System::Metric)) == "Metric");
  // Conversions, rounded half away from zero, below zero too.
  assert(fahrenheit(0) == 32 && fahrenheit(100) == 212 && fahrenheit(-40) == -40 && fahrenheit(24) == 75 && fahrenheit(-10) == 14);
  assert(fahrenheitTenths(244) == 759 && fahrenheitTenths(-50) == 230 && fahrenheitTenths(-450) == -490);
  assert(inches(30) == 12 && inches(24) == 9 && inches(2) == 1 && inches(1) == 0 && inches(450) == 177);
  assert(mph(19) == 12 && mph(25) == 16 && mph(100) == 62 && mphTenths(185) == 115 && mphTenths(0) == 0);
  assert(feet(111) == 364 && feet(30) == 98 && feet(32000) == 104987);
  assert(rounded(-2.5) == -3 && rounded(2.5) == 3 && rounded(-0.4) == 0);
  assert(tenths(115) == "11.5" && tenths(-5) == "-0.5" && tenths(0) == "0.0" && tenths(-759) == "-75.9");
  // What a reading says, in each system. Units the US doesn't change stay as they are.
  assert(reading(24, "C", System::US) == "75`F" && reading(24, "C", System::Metric) == "24`C");
  assert(reading(30, "cm", System::US) == "12 in" && reading(30, "cm", System::Metric) == "30 cm");
  assert(reading(19, "km/h", System::US) == "12 mph" && reading(19, "km/h", System::Metric) == "19 km/h");
  assert(reading(111, "m", System::US) == "364 ft" && reading(111, "m", System::Metric) == "111 m");
  for (System s : {System::US, System::Metric}) {
    assert(reading(30, "deg", s) == "30`" && reading(41, "%", s) == "41%" && reading(1200, "Hz", s) == "1200 Hz" && reading(8, "", s) == "8");
    assert(reading(-32768, "C", s).size() < 32 && reading(32767, "m", s).size() < 32);
  }
  // Distances: feet to a tenth of a mile, then miles; meters to a kilometer, then kilometers.
  assert(distance(0, System::US) == "0 ft" && distance(160, System::US) == "525 ft" && distance(161, System::US) == "0.1 mi");
  assert(distance(1853, System::US) == "1.2 mi" && distance(32000, System::US, true) == "19.9+ mi");
  assert(distance(111, System::Metric) == "111 m" && distance(999, System::Metric) == "999 m" && distance(1853, System::Metric) == "1.9 km");
  assert(distance(32000, System::Metric, true) == "32.0+ km");
  char small[4]; distanceText(small, sizeof(small), 32000, System::US, true); assert(std::string(small).size() == 3);  // Never past the buffer.
}
'''
with tempfile.TemporaryDirectory(prefix='s1-units-') as directory:
    p = Path(directory)
    (p / 'units.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation',
                    '-fsanitize=address,undefined', '-I' + str(ROOT / 'bots/CardputerADV/src'),
                    str(p / 'units.cpp'), '-o', str(p / 'units')], check=True)
    subprocess.run([str(p / 'units')], check=True)
print('PASS units: US by default, conversions rounded half away from zero, readings and distances in both systems, bounded text')
