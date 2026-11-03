#!/usr/bin/env python3
"""Draw Pulse's real Wi-Fi icon on host: rings by signal, the sweep while dialing, the red dot, and its place."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
pulse = (root / 'bots/CardputerADV/src/main_parts/058_main.cpp.inc').read_text()
start = pulse.index('void drawHomeWifiStatus(')
end, depth = pulse.index('{', start) + 1, 1
while depth:
    depth += (pulse[end] == '{') - (pulse[end] == '}')
    end += 1
icon = pulse[start:end]

code = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
constexpr uint16_t rgb565_local(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
struct Arc { int x, y, outer, inner, from, to; uint16_t color; };
struct Dot { int x, y, r; uint16_t color; };
struct Canvas {
  std::vector<Arc> arcs; std::vector<Dot> dots;
  void drawArc(int x, int y, int r0, int r1, float a0, float a1, uint16_t c) { arcs.push_back({x, y, r0, r1, int(a0), int(a1), c}); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { assert(w == 3 && h == 3); dots.push_back({x, y, w, c}); }
  // Pulse's owner turned filled arcs down; outline bands only.
  void fillArc(int, int, int, int, float, float, uint16_t) { assert(false && "filled arcs are off Pulse"); }
} gCanvas;
''' + icon + r'''
const uint16_t kCyan = rgb565_local(47, 227, 255), kAmber = rgb565_local(255, 182, 0);
const uint16_t kDim = rgb565_local(36, 73, 85), kRed = rgb565_local(255, 63, 85);
int litRings(uint16_t lit) { int n = 0; for (const auto& a : gCanvas.arcs) n += a.color == lit; return n; }
void draw(uint8_t bars, bool trying, uint32_t at) { gCanvas = Canvas{}; drawHomeWifiStatus(bars, trying, at); }
int main() {
  // Three rings and a dot, fanned upward, in the corner above the clock panel and inside the frame.
  draw(4, false, 0);
  assert(gCanvas.arcs.size() == 3 && gCanvas.dots.size() == 1);
  for (const auto& a : gCanvas.arcs) {
    assert(a.from == 225 && a.to == 315 && a.outer == a.inner + 1);
    const int reach = a.outer * 7 / 10 + 1;
    assert(a.x - reach >= 1 && a.x + reach <= 238 && a.y - a.outer >= 1 && a.y < 13);
  }
  // Signal: one bar is the dot alone, four is everything, in the battery's cyan.
  for (int bars = 1; bars <= 4; ++bars) {
    draw(bars, false, 0);
    assert(litRings(kCyan) == bars - 1 && litRings(kDim) == 4 - bars && gCanvas.dots[0].color == kCyan);
  }
  // Dialing: amber, filling from the dot up, then starting again.
  std::vector<int> seen;
  for (uint32_t at = 0; at < 240 * 5; at += 240) {
    draw(0, true, at);
    seen.push_back(litRings(kAmber) + (gCanvas.dots[0].color == kAmber));
  }
  assert((seen == std::vector<int>{0, 1, 2, 3, 4}));
  draw(0, true, 240 * 5); assert(litRings(kAmber) == 0);
  // Neither connected nor trying: dim rings and a red dot.
  draw(0, false, 0);
  assert(litRings(kDim) == 3 && gCanvas.dots[0].color == kRed);
  std::cout << "PASS Pulse Wi-Fi icon: rings by signal, an amber sweep while dialing, a red dot when off\n";
}
'''

with tempfile.TemporaryDirectory(prefix='s1-home-wifi-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
