#pragma once
#include <cstddef>
#include <cstdint>

// Station art for an 8-bit canvas. Its 256 colours are 3 bits of red, 3 of green and 2 of
// blue, and rounding each pixel to the nearest bands shading and greys. Floyd-Steinberg error
// diffusion, run boustrophedon, spreads what each pixel loses to the ones not yet done, so
// at the screen's size the colours average out to the real ones. Colours that are exact stay
// exact, so flat logos stay clean.
namespace radio_image {
inline uint8_t to332(int r, int g, int b) {
  const auto level = [](int v, int top) { v = v < 0 ? 0 : v > 255 ? 255 : v; return (v * top + 127) / 255; };
  return uint8_t(level(r, 7) << 5 | level(g, 7) << 2 | level(b, 3));
}
inline void from332(uint8_t v, int& r, int& g, int& b) {
  r = (v >> 5) * 255 / 7;
  g = ((v >> 2) & 7) * 255 / 7;
  b = (v & 3) * 255 / 3;
}
constexpr int kMaxSide = 96;
// rgb(x, y, r, g, b) reads a pixel at 8 bits a channel; out gets side*side bytes.
template <class Pixel>
void dither332(int side, Pixel rgb, uint8_t* out) {
  if (side <= 0 || side > kMaxSide) return;
  // Two rows of carried error, one pixel of margin each side, three channels.
  int16_t rows[2][(kMaxSide + 2) * 3] = {};
  for (int y = 0; y < side; ++y) {
    int16_t* now = rows[y & 1];
    int16_t* next = rows[(y + 1) & 1];
    for (int i = 0; i < (side + 2) * 3; ++i) next[i] = 0;
    const bool right = !(y & 1);
    for (int k = 0; k < side; ++k) {
      const int x = right ? k : side - 1 - k;
      int c[3];
      rgb(x, y, c[0], c[1], c[2]);
      int16_t* e = now + (x + 1) * 3;
      for (int i = 0; i < 3; ++i) c[i] += e[i];
      const uint8_t v = to332(c[0], c[1], c[2]);
      out[y * side + x] = v;
      int q[3];
      from332(v, q[0], q[1], q[2]);
      const int step = right ? 3 : -3;
      for (int i = 0; i < 3; ++i) {
        const int err = c[i] - q[i];
        e[step + i] = int16_t(e[step + i] + err * 7 / 16);
        int16_t* d = next + (x + 1) * 3 + i;
        d[-step] = int16_t(d[-step] + err * 3 / 16);
        d[0] = int16_t(d[0] + err * 5 / 16);
        d[step] = int16_t(d[step] + err / 16);
      }
    }
  }
}
// A JPEG's size, from its start-of-frame header. False when it isn't a baseline JPEG, which is
// all the device's decoder reads.
inline bool jpegSize(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height) {
  if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
  size_t at = 2;
  while (at + 4 <= size) {
    if (data[at] != 0xFF) return false;
    const uint8_t marker = data[at + 1];
    if (marker == 0xFF) { ++at; continue; }  // Fill bytes.
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { at += 2; continue; }
    const size_t length = size_t(data[at + 2]) << 8 | data[at + 3];
    if (length < 2 || at + 2 + length > size) return false;
    if (marker == 0xC0 || marker == 0xC1) {
      if (length < 7) return false;
      height = uint16_t(data[at + 5] << 8 | data[at + 6]);
      width = uint16_t(data[at + 7] << 8 | data[at + 8]);
      return width && height;
    }
    if ((marker >= 0xC2 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) || marker == 0xDA || marker == 0xD9) return false;
    at += 2 + length;
  }
  return false;
}
}  // namespace radio_image
