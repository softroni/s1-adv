/*
  Copyright (c) 2008, Adobe Systems Incorporated
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  * Neither the name of Adobe Systems Incorporated nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// The Cameras app's live and clip frames: a baseline JPEG with 4:2:0 colour, the layout the
// Cardputer's decoder (TJpgDec) reads fastest and the smallest for the picture. Adapted from
// jpeg-js's encoder (BSD-3-Clause, itself Andreas Ritter's port of Adobe's), which only writes
// 4:4:4: twice the colour data, about 40% bigger files and twice the decoding on the Cardputer.
// The tables, the DCT and the Huffman coding are its; the 16x16 blocks, typed arrays and the
// output buffer are new. encodeWithin keeps a frame under a size the Cardputer has room for.

const ZIGZAG = [
  0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42, 3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
  10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60, 21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63,
];
const DC_LUM_CODES = [0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0];
const DC_LUM_VALUES = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
const AC_LUM_CODES = [0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d];
const AC_LUM_VALUES = [
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
];
const DC_CHR_CODES = [0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0];
const DC_CHR_VALUES = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
const AC_CHR_CODES = [0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77];
const AC_CHR_VALUES = [
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
];
const Y_QT = [
  16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
];
const UV_QT = [
  17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
];
const AASF = [1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.5411961, 0.275899379];

function huffman(codes, values) {
  const code = new Int32Array(256), length = new Int32Array(256);
  let value = 0, at = 0;
  for (let k = 1; k <= 16; ++k) {
    for (let j = 1; j <= codes[k]; ++j) { code[values[at]] = value; length[values[at]] = k; ++at; ++value; }
    value *= 2;
  }
  return { code, length };
}
const YDC = huffman(DC_LUM_CODES, DC_LUM_VALUES), YAC = huffman(AC_LUM_CODES, AC_LUM_VALUES);
const UVDC = huffman(DC_CHR_CODES, DC_CHR_VALUES), UVAC = huffman(AC_CHR_CODES, AC_CHR_VALUES);

// Each value's size category and its bits, for -32767..32767.
const CATEGORY = new Uint8Array(65535), BITS = new Int32Array(65535);
for (let cat = 1, lower = 1, upper = 2; cat <= 15; ++cat, lower <<= 1, upper <<= 1) {
  for (let nr = lower; nr < upper; ++nr) { CATEGORY[32767 + nr] = cat; BITS[32767 + nr] = nr; }
  for (let nr = -(upper - 1); nr <= -lower; ++nr) { CATEGORY[32767 + nr] = cat; BITS[32767 + nr] = upper - 1 + nr; }
}

const tables = new Map();  // quality -> { y, uv, fy, fuv }
function quantTables(quality) {
  let t = tables.get(quality);
  if (t) return t;
  const q = Math.max(1, Math.min(100, Math.round(quality)));
  const sf = q < 50 ? Math.floor(5000 / q) : Math.floor(200 - q * 2);
  const y = new Uint8Array(64), uv = new Uint8Array(64), fy = new Float64Array(64), fuv = new Float64Array(64);
  for (let i = 0; i < 64; ++i) {
    y[ZIGZAG[i]] = Math.max(1, Math.min(255, Math.floor((Y_QT[i] * sf + 50) / 100)));
    uv[ZIGZAG[i]] = Math.max(1, Math.min(255, Math.floor((UV_QT[i] * sf + 50) / 100)));
  }
  for (let row = 0, k = 0; row < 8; ++row) {
    for (let col = 0; col < 8; ++col, ++k) {
      fy[k] = 1.0 / (y[ZIGZAG[k]] * AASF[row] * AASF[col] * 8.0);
      fuv[k] = 1.0 / (uv[ZIGZAG[k]] * AASF[row] * AASF[col] * 8.0);
    }
  }
  t = { y, uv, fy, fuv };
  tables.set(quality, t);
  return t;
}

class BitWriter {
  constructor(size) { this.buf = Buffer.allocUnsafe(size); this.at = 0; this.acc = 0; this.n = 0; }
  byte(v) {
    if (this.at >= this.buf.length) { const bigger = Buffer.allocUnsafe(this.buf.length * 2); this.buf.copy(bigger); this.buf = bigger; }
    this.buf[this.at++] = v;
  }
  word(v) { this.byte((v >> 8) & 0xff); this.byte(v & 0xff); }
  bits(value, length) {
    for (let i = length - 1; i >= 0; --i) {
      this.acc = (this.acc << 1) | ((value >> i) & 1);
      if (++this.n === 8) {
        this.byte(this.acc);
        if (this.acc === 0xff) this.byte(0);  // Byte stuffing.
        this.acc = 0; this.n = 0;
      }
    }
  }
  flush() { if (this.n) this.bits((1 << (8 - this.n)) - 1, 8 - this.n); }
}

const quant = new Int32Array(64), zz = new Int32Array(64);
function fdctQuant(data, fdtbl) {
  for (let off = 0; off < 64; off += 8) {
    const d0 = data[off], d1 = data[off + 1], d2 = data[off + 2], d3 = data[off + 3];
    const d4 = data[off + 4], d5 = data[off + 5], d6 = data[off + 6], d7 = data[off + 7];
    const t0 = d0 + d7, t7 = d0 - d7, t1 = d1 + d6, t6 = d1 - d6, t2 = d2 + d5, t5 = d2 - d5, t3 = d3 + d4, t4 = d3 - d4;
    let t10 = t0 + t3; const t13 = t0 - t3; let t11 = t1 + t2; let t12 = t1 - t2;
    data[off] = t10 + t11; data[off + 4] = t10 - t11;
    const z1 = (t12 + t13) * 0.707106781;
    data[off + 2] = t13 + z1; data[off + 6] = t13 - z1;
    t10 = t4 + t5; t11 = t5 + t6; t12 = t6 + t7;
    const z5 = (t10 - t12) * 0.382683433, z2 = 0.5411961 * t10 + z5, z4 = 1.306562965 * t12 + z5, z3 = t11 * 0.707106781;
    const z11 = t7 + z3, z13 = t7 - z3;
    data[off + 5] = z13 + z2; data[off + 3] = z13 - z2; data[off + 1] = z11 + z4; data[off + 7] = z11 - z4;
  }
  for (let off = 0; off < 8; ++off) {
    const d0 = data[off], d1 = data[off + 8], d2 = data[off + 16], d3 = data[off + 24];
    const d4 = data[off + 32], d5 = data[off + 40], d6 = data[off + 48], d7 = data[off + 56];
    const t0 = d0 + d7, t7 = d0 - d7, t1 = d1 + d6, t6 = d1 - d6, t2 = d2 + d5, t5 = d2 - d5, t3 = d3 + d4, t4 = d3 - d4;
    let t10 = t0 + t3; const t13 = t0 - t3; let t11 = t1 + t2; let t12 = t1 - t2;
    data[off] = t10 + t11; data[off + 32] = t10 - t11;
    const z1 = (t12 + t13) * 0.707106781;
    data[off + 16] = t13 + z1; data[off + 48] = t13 - z1;
    t10 = t4 + t5; t11 = t5 + t6; t12 = t6 + t7;
    const z5 = (t10 - t12) * 0.382683433, z2 = 0.5411961 * t10 + z5, z4 = 1.306562965 * t12 + z5, z3 = t11 * 0.707106781;
    const z11 = t7 + z3, z13 = t7 - z3;
    data[off + 40] = z13 + z2; data[off + 24] = z13 - z2; data[off + 8] = z11 + z4; data[off + 56] = z11 - z4;
  }
  for (let i = 0; i < 64; ++i) {
    const v = data[i] * fdtbl[i];
    quant[i] = v > 0 ? (v + 0.5) | 0 : (v - 0.5) | 0;
  }
  return quant;
}

function block(out, data, fdtbl, dc, hdc, hac) {
  const q = fdctQuant(data, fdtbl);
  for (let j = 0; j < 64; ++j) zz[ZIGZAG[j]] = q[j];
  const diff = zz[0] - dc;
  if (diff === 0) out.bits(hdc.code[0], hdc.length[0]);
  else { const p = 32767 + diff; out.bits(hdc.code[CATEGORY[p]], hdc.length[CATEGORY[p]]); out.bits(BITS[p], CATEGORY[p]); }
  let end = 63;
  while (end > 0 && zz[end] === 0) --end;
  if (end === 0) { out.bits(hac.code[0], hac.length[0]); return zz[0]; }
  for (let i = 1; i <= end; ++i) {
    const start = i;
    while (zz[i] === 0 && i <= end) ++i;
    let zeros = i - start;
    for (; zeros >= 16; zeros -= 16) out.bits(hac.code[0xf0], hac.length[0xf0]);
    const p = 32767 + zz[i];
    const sym = (zeros << 4) + CATEGORY[p];
    out.bits(hac.code[sym], hac.length[sym]);
    out.bits(BITS[p], CATEGORY[p]);
  }
  if (end !== 63) out.bits(hac.code[0], hac.length[0]);
  return zz[0];
}

function headers(out, width, height, t) {
  out.word(0xffd8);
  out.word(0xffe0); out.word(16); for (const b of [0x4a, 0x46, 0x49, 0x46, 0, 1, 1, 0]) out.byte(b); out.word(1); out.word(1); out.byte(0); out.byte(0);
  out.word(0xffdb); out.word(132);
  out.byte(0); for (let i = 0; i < 64; ++i) out.byte(t.y[i]);
  out.byte(1); for (let i = 0; i < 64; ++i) out.byte(t.uv[i]);
  out.word(0xffc0); out.word(17); out.byte(8); out.word(height); out.word(width); out.byte(3);
  out.byte(1); out.byte(0x22); out.byte(0);  // Y: 2x2, the 4:2:0 layout.
  out.byte(2); out.byte(0x11); out.byte(1);
  out.byte(3); out.byte(0x11); out.byte(1);
  out.word(0xffc4); out.word(0x01a2);
  out.byte(0x00); for (let i = 1; i <= 16; ++i) out.byte(DC_LUM_CODES[i]); for (const v of DC_LUM_VALUES) out.byte(v);
  out.byte(0x10); for (let i = 1; i <= 16; ++i) out.byte(AC_LUM_CODES[i]); for (const v of AC_LUM_VALUES) out.byte(v);
  out.byte(0x01); for (let i = 1; i <= 16; ++i) out.byte(DC_CHR_CODES[i]); for (const v of DC_CHR_VALUES) out.byte(v);
  out.byte(0x11); for (let i = 1; i <= 16; ++i) out.byte(AC_CHR_CODES[i]); for (const v of AC_CHR_VALUES) out.byte(v);
  out.word(0xffda); out.word(12); out.byte(3); out.byte(1); out.byte(0); out.byte(2); out.byte(0x11); out.byte(3); out.byte(0x11);
  out.byte(0); out.byte(0x3f); out.byte(0);
}

const Y = [new Float64Array(64), new Float64Array(64), new Float64Array(64), new Float64Array(64)];
const U = new Float64Array(64), V = new Float64Array(64);
// rgba: width x height x 4 bytes (the 4th is ignored), as renderRect makes it.
export function encodeJpeg(rgba, width, height, quality = 75) {
  const t = quantTables(quality);
  const out = new BitWriter(16 * 1024);
  headers(out, width, height, t);
  let dcY = 0, dcU = 0, dcV = 0;
  const stride = width * 4;
  for (let my = 0; my < height; my += 16) {
    for (let mx = 0; mx < width; mx += 16) {
      U.fill(0); V.fill(0);
      for (let py = 0; py < 16; ++py) {
        const y = Math.min(height - 1, my + py);  // Past the edge, the last row and column again.
        for (let px = 0; px < 16; ++px) {
          const x = Math.min(width - 1, mx + px), p = y * stride + x * 4;
          const r = rgba[p], g = rgba[p + 1], b = rgba[p + 2];
          Y[((py >> 3) << 1) | (px >> 3)][((py & 7) << 3) | (px & 7)] = 0.299 * r + 0.587 * g + 0.114 * b - 128;
          const c = ((py >> 1) << 3) | (px >> 1);
          U[c] += (-0.168736 * r - 0.331264 * g + 0.5 * b) / 4;
          V[c] += (0.5 * r - 0.418688 * g - 0.081312 * b) / 4;
        }
      }
      for (let i = 0; i < 4; ++i) dcY = block(out, Y[i], t.fy, dcY, YDC, YAC);
      dcU = block(out, U, t.fuv, dcU, UVDC, UVAC);
      dcV = block(out, V, t.fuv, dcV, UVDC, UVAC);
    }
  }
  out.flush();
  out.word(0xffd9);
  return out.buf.subarray(0, out.at);
}

// A frame no bigger than maxBytes: at the quality `state` keeps, lowered at once when a frame comes
// out too big and raised slowly when they come out small, so most frames take one encoding.
export const FRAME_BUDGET = 7600;
export function encodeWithin(rgba, width, height, maxBytes = FRAME_BUDGET, state = { quality: 75 }) {
  let quality = state.quality ?? 75;
  let jpeg = encodeJpeg(rgba, width, height, quality);
  while (jpeg.length > maxBytes && quality > 20) {
    quality = Math.max(20, quality - Math.max(5, Math.ceil((quality * (jpeg.length - maxBytes)) / jpeg.length)));
    jpeg = encodeJpeg(rgba, width, height, quality);
  }
  if (jpeg.length < maxBytes * 0.7 && quality < 80) quality += 1;
  state.quality = quality;
  return jpeg;
}
