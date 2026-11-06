// What the Cameras app's pictures are made of, shared by the bridge and its camera helper.
// Nothing here needs the eufy library, so it runs (and is tested) on the bridge's own Node.
//
// The Cardputer's screen is 240x135, the cameras' own 16:9, so a picture fills it. Pictures and
// live frames go to it as baseline 4:2:0 JPEGs 240x136, which its decoder (M5GFX's TJpgDec) draws
// straight to the screen, the last row off its bottom. It takes no other layout: ffmpeg's 4:2:2
// and 4:4:4 give the colour planes sampling factors it refuses, and 4:2:0 needs an even height. The
// wall's thumbnails go as raw 8-bit pixels, 3 bits of red, 3 of green and 2 of blue, the menu
// canvas's own format, already dithered here so the device only copies them.
import { createHash } from "node:crypto";

export const PICTURE = Object.freeze({ width: 240, height: 136 });
export const THUMB = Object.freeze({ width: 78, height: 44, bytes: 78 * 44 });

// A camera's id for the device and the log: short, stable, and not its serial number.
export function cameraId(serial) {
  return createHash("sha256").update(`s1-adv-camera:${String(serial)}`).digest("hex").slice(0, 8);
}
export const CAMERA_ID = /^[0-9a-f]{8}$/u;

// One pixel's 8 bits a channel to 3-3-2, rounding each channel to its nearest level.
function level(v, top) { return Math.round((Math.max(0, Math.min(255, v)) * top) / 255); }
export function to332(r, g, b) { return (level(r, 7) << 5) | (level(g, 7) << 2) | level(b, 3); }
function from332(v) {
  return [Math.floor(((v >> 5) * 255) / 7), Math.floor((((v >> 2) & 7) * 255) / 7), Math.floor(((v & 3) * 255) / 3)];
}

// Floyd-Steinberg, run boustrophedon, as the firmware's station art is (radio_image.h): what
// each pixel loses to rounding goes to the ones not yet done, so shading doesn't band.
export function dither332(rgb, width, height) {
  const out = new Uint8Array(width * height);
  let now = new Int16Array((width + 2) * 3), next = new Int16Array((width + 2) * 3);
  for (let y = 0; y < height; ++y) {
    next.fill(0);
    const right = (y & 1) === 0;
    for (let k = 0; k < width; ++k) {
      const x = right ? k : width - 1 - k;
      const at = (y * width + x) * 3, e = (x + 1) * 3;
      const c = [rgb[at] + now[e], rgb[at + 1] + now[e + 1], rgb[at + 2] + now[e + 2]];
      const v = to332(c[0], c[1], c[2]);
      out[y * width + x] = v;
      const q = from332(v), step = right ? 3 : -3;
      for (let i = 0; i < 3; ++i) {
        const err = c[i] - q[i];
        now[e + step + i] += Math.trunc((err * 7) / 16);
        next[e - step + i] += Math.trunc((err * 3) / 16);
        next[e + i] += Math.trunc((err * 5) / 16);
        next[e + step + i] += Math.trunc(err / 16);
      }
    }
    [now, next] = [next, now];
  }
  return out;
}

// A baseline JPEG's size and each component's sampling byte (0x22 is 2x2), from its frame header.
export function jpegLayout(jpeg) {
  if (!jpeg || jpeg.length < 4 || jpeg[0] !== 0xff || jpeg[1] !== 0xd8) return null;
  let at = 2;
  while (at + 4 <= jpeg.length) {
    if (jpeg[at] !== 0xff) return null;
    const marker = jpeg[at + 1];
    if (marker === 0xff) { at += 1; continue; }
    const length = jpeg.readUInt16BE(at + 2);
    if (marker === 0xc0 || marker === 0xc1) {
      const count = jpeg[at + 9];
      const sampling = [];
      for (let i = 0; i < count; ++i) sampling.push(jpeg[at + 11 + i * 3]);
      return { height: jpeg.readUInt16BE(at + 5), width: jpeg.readUInt16BE(at + 7), sampling };
    }
    if (marker === 0xda || marker === 0xd9 || (marker >= 0xc2 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc)) return null;
    at += 2 + length;
  }
  return null;
}
// What the Cardputer's decoder draws: the screen's picture, in colour, 4:2:0 as ffmpeg makes it or
// 4:4:4 as the helper's own encoder (jpeg-js) does. ffmpeg's 4:2:2 and 4:4:4 it refuses.
export function deviceDecodes(jpeg) {
  const layout = jpegLayout(jpeg);
  const sampling = layout?.sampling.join(",");
  return Boolean(layout && layout.width === PICTURE.width && layout.height === PICTURE.height &&
    (sampling === "34,17,17" || sampling === "17,17,17"));
}

// ffmpeg's MJPEG output, one JPEG after another, cut back into whole pictures. Inside a JPEG's
// coded data every 0xFF byte is followed by 0x00, so FF D9 is only ever the end of a picture.
export class JpegSplitter {
  constructor(onPicture, maxBytes = 256 * 1024) {
    this.onPicture = onPicture;
    this.maxBytes = maxBytes;
    this.parts = [];
    this.length = 0;
  }
  push(chunk) {
    let from = 0;
    for (let i = 0; i < chunk.length; ++i) {
      const prev = i > 0 ? chunk[i - 1] : this.lastByte;
      if (prev === 0xff && chunk[i] === 0xd9) {
        this.parts.push(chunk.subarray(from, i + 1));
        const picture = Buffer.concat(this.parts);
        this.parts = [];
        this.length = 0;
        from = i + 1;
        const start = picture.indexOf(Buffer.from([0xff, 0xd8]));
        if (start >= 0) this.onPicture(start ? picture.subarray(start) : picture);
      }
    }
    this.lastByte = chunk.length ? chunk[chunk.length - 1] : this.lastByte;
    if (from < chunk.length) {
      this.parts.push(chunk.subarray(from));
      this.length += chunk.length - from;
      if (this.length > this.maxBytes) { this.parts = []; this.length = 0; }  // Not a picture: start again.
    }
  }
}

// ffmpeg's arguments. Each reads stdin and writes stdout, and only those: no files, no network.
const QUIET = ["-nostdin", "-hide_banner", "-loglevel", "error"];
const FILL = `scale=${PICTURE.width}:${PICTURE.height}:force_original_aspect_ratio=increase:flags=area,crop=${PICTURE.width}:${PICTURE.height}`;
const BASELINE = ["-c:v", "mjpeg", "-pix_fmt", "yuvj420p", "-huffman", "default", "-q:v"];
// A camera's still, any size, to the screen's picture.
export function pictureArgs(quality = 5) {
  return [...QUIET, "-f", "image2pipe", "-c:v", "mjpeg", "-i", "pipe:0", "-frames:v", "1", "-vf", FILL,
    ...BASELINE, String(quality), "-f", "image2pipe", "pipe:1"];
}
// A recording's first keyframe to a thumbnail, as RGB for dither332.
export function stillArgs(codec) {
  const format = codec === "h264" ? "h264" : "hevc";
  return [...QUIET, "-f", format, "-i", "pipe:0", "-frames:v", "1",
    "-vf", `scale=${THUMB.width}:${THUMB.height}:force_original_aspect_ratio=increase:flags=area,crop=${THUMB.width}:${THUMB.height}`,
    "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"];
}
// A picture to the wall's thumbnail, as RGB for dither332.
export function thumbArgs() {
  return [...QUIET, "-f", "image2pipe", "-c:v", "mjpeg", "-i", "pipe:0", "-frames:v", "1",
    "-vf", `scale=${THUMB.width}:${THUMB.height}:flags=area`, "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"];
}
// A live camera's Annex-B video to a picture per frame, as small and as soon as it can.
// Live view: ffmpeg decodes the camera's video to raw 960x540 RGB frames on stdout, and the helper
// cuts the part on screen out of each (viewRect, renderRect) and encodes it itself, so a zoom or a
// pan is the very next picture, not a restart that waits for the camera's next keyframe. 960 wide is
// four screens, so the 4x digital zoom still has a pixel for every one on the screen.
export const RAW = Object.freeze({ width: 960, height: 540, bytes: 960 * 540 * 3 });
export const DIGITAL_ZOOM = Object.freeze([1, 1.5, 2, 3, 4]);
export function liveArgs(codec) {
  const format = codec === "h264" ? "h264" : codec === "h265" ? "hevc" : "";
  if (!format) return null;
  // No "-fflags nobuffer": it throws away the packets ffmpeg probed, the first keyframe among them,
  // and then nothing after it decodes.
  return [...QUIET, "-flags", "low_delay", "-probesize", "32768", "-analyzeduration", "0",
    "-f", format, "-i", "pipe:0", "-an",
    "-vf", `scale=${RAW.width}:${RAW.height}:force_original_aspect_ratio=increase:flags=area,crop=${RAW.width}:${RAW.height}`,
    "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"];
}

// The part of the frame on screen at digital zoom z, its middle near (cx, cy), kept inside the frame.
// Its shape is the screen's.
export function viewRect(z, cx = RAW.width / 2, cy = RAW.height / 2) {
  const fullW = Math.min(RAW.width, (RAW.height * PICTURE.width) / PICTURE.height);
  const w = fullW / Math.max(1, Number(z) || 1), h = (w * PICTURE.height) / PICTURE.width;
  const x = Math.min(Math.max(cx - w / 2, 0), RAW.width - w), y = Math.min(Math.max(cy - h / 2, 0), RAW.height - h);
  return { x, y, w, h, cx: x + w / 2, cy: y + h / 2 };
}
// One arrow press moves the view a third of itself. moved is false at the frame's edge.
export function panRect(rect, direction, z) {
  const [dx, dy] = { left: [-1, 0], right: [1, 0], up: [0, -1], down: [0, 1] }[direction] || [0, 0];
  const next = viewRect(z, rect.cx + (dx * rect.w) / 3, rect.cy + (dy * rect.h) / 3);
  return { rect: next, moved: Math.abs(next.x - rect.x) > 0.5 || Math.abs(next.y - rect.y) > 0.5 };
}
// The view's place in the frame for the device's little map, in thousandths: x0 y0 x1 y1.
export function rectMille(rect) {
  return [rect.x / RAW.width, rect.y / RAW.height, (rect.x + rect.w) / RAW.width, (rect.y + rect.h) / RAW.height]
    .map(v => Math.max(0, Math.min(1000, Math.round(v * 1000))));
}
// The rect of a raw frame to the screen's picture, as RGBA for the JPEG encoder: each pixel the
// average of the frame's pixels under it.
export function renderRect(rgb, rect, out = Buffer.alloc(PICTURE.width * PICTURE.height * 4)) {
  const W = PICTURE.width, H = PICTURE.height, stride = RAW.width * 3;
  const xs = new Int32Array(W + 1), ys = new Int32Array(H + 1);
  for (let i = 0; i <= W; ++i) xs[i] = Math.min(RAW.width, Math.floor(rect.x + (i * rect.w) / W));
  for (let j = 0; j <= H; ++j) ys[j] = Math.min(RAW.height, Math.floor(rect.y + (j * rect.h) / H));
  let o = 0;
  for (let j = 0; j < H; ++j) {
    const y0 = Math.min(ys[j], RAW.height - 1), y1 = Math.max(ys[j + 1], y0 + 1);
    for (let i = 0; i < W; ++i) {
      const x0 = Math.min(xs[i], RAW.width - 1), x1 = Math.max(xs[i + 1], x0 + 1);
      let r = 0, g = 0, b = 0;
      for (let y = y0; y < y1; ++y) {
        for (let p = y * stride + x0 * 3, end = y * stride + x1 * 3; p < end; p += 3) { r += rgb[p]; g += rgb[p + 1]; b += rgb[p + 2]; }
      }
      const n = (y1 - y0) * (x1 - x0);
      out[o++] = r / n; out[o++] = g / n; out[o++] = b / n; out[o++] = 255;
    }
  }
  return out;
}

// Live view's sound: the camera's AAC (ADTS) or A-law, as 8 kHz mono 16-bit PCM, little-endian,
// which the device plays as it comes with no decoder of its own: 16 KB a second. -flush_packets,
// because ffmpeg otherwise holds 32 KB of it, two seconds, before writing any.
export const AUDIO = Object.freeze({ rate: 8000, bytesPerSecond: 16000 });
export function audioArgs(codec) {
  const input = codec === "aac-lc" ? ["-probesize", "2048", "-analyzeduration", "0", "-f", "aac"]
    : codec === "g711a" ? ["-f", "alaw", "-ar", "16000", "-ac", "1"] : null;
  if (!input) return null;
  // Camera microphones are quiet: three times louder, with a limiter (5 ms ahead) for the loud parts.
  return [...QUIET, ...input, "-i", "pipe:0", "-vn", "-af", "volume=3,alimiter=limit=0.95:attack=5:release=60",
    "-ac", "1", "-ar", String(AUDIO.rate), "-f", "s16le", "-flush_packets", "1", "pipe:1"];
}

// A camera with a second, telephoto lens (the library's ptz().zoom) zooms 3x with it, and the
// digital zoom goes on top. The device steps through the ladder, in tenths, and the plan says which
// lens and which digital output make each step.
export const LENS_ZOOM = 3;
export function zoomLadder(lens) {
  const digital = DIGITAL_ZOOM.map(z => Math.round(z * 10));
  return lens ? [...digital.filter(t => t < LENS_ZOOM * 10), ...digital.map(t => t * LENS_ZOOM)] : digital;
}
export function zoomPlan(tenths, lens) {
  const ladder = zoomLadder(lens);
  const asked = Number(tenths) || 10;
  const step = ladder.reduce((best, t) => Math.abs(t - asked) < Math.abs(best - asked) ? t : best, ladder[0]);
  const optical = lens && step >= LENS_ZOOM * 10 ? LENS_ZOOM : 1;
  const branch = DIGITAL_ZOOM.findIndex(z => Math.round(z * 10 * optical) === step);
  return { tenths: step, optical, branch };
}

// The HomeBase's mode, as one word the device shows. The library's typed getter answers the wire
// number; its names are taken too.
export function modeWord(mode) {
  const byNumber = { 0: "away", 1: "home", 2: "schedule", 3: "custom1", 4: "custom2", 5: "custom3", 6: "off", 47: "geo", 63: "disarmed" };
  const known = { away: "Away", home: "Home", disarmed: "Disarmed", schedule: "Schedule", geo: "Geofence", off: "Off",
    custom: "Custom", custom1: "Custom 1", custom2: "Custom 2", custom3: "Custom 3" };
  const key = typeof mode === "number" ? byNumber[mode] : String(mode ?? "").toLowerCase();
  return known[key] || "";
}

// A name the device can show: printable, one line, short, with no tab to break its lines.
export function shownName(name) {
  const text = String(name || "").replace(/[\u0000-\u001f\u007f]+/gu, " ").replace(/\s+/gu, " ").trim();
  return Array.from(text).slice(0, 24).join("") || "Camera";
}

// The device's list, as tab-separated lines (the firmware reads them without a JSON parser):
//   state <ready|setup|signing-in|code|captcha|error|off> [<what to show>]
//   base <mode word>
//   cam <id> <flags> <battery %|-> <seconds since motion|-> <seconds since its picture|-> <name>
// flags: p pans and tilts, z zooms with a second lens, o switched off, - nothing. Ages are seconds, so the device's own
// clock doesn't matter.
export function listLines(list, now = Date.now()) {
  const lines = [];
  const state = String(list?.state || "off");
  const detail = String(list?.detail || "").replace(/[\t\r\n]+/gu, " ").slice(0, 60);
  lines.push(detail ? `state\t${state}\t${detail}` : `state\t${state}`);
  const word = modeWord(list?.base?.mode);
  if (word) lines.push(`base\t${word}`);
  const age = (ms) => Number.isFinite(ms) && ms > 0 ? String(Math.max(0, Math.round((now - ms) / 1000))) : "-";
  for (const cam of list?.cameras || []) {
    if (!CAMERA_ID.test(String(cam.id || ""))) continue;
    const flags = `${cam.ptz ? "p" : ""}${cam.zoom ? "z" : ""}${cam.off ? "o" : ""}` || "-";
    const battery = Number.isInteger(cam.battery) && cam.battery >= 0 && cam.battery <= 100 ? String(cam.battery) : "-";
    lines.push(["cam", cam.id, flags, battery, age(cam.motionAt), age(cam.pictureAt), shownName(cam.name)].join("\t"));
  }
  return `${lines.join("\n")}\n`;
}

// What the bridge's log may say: no serial numbers, no names, no addresses, no email.
export function scrub(text, serials = []) {
  let out = String(text ?? "");
  for (const serial of serials) if (serial && serial.length >= 6) out = out.split(serial).join(cameraId(serial));
  return out
    .replace(/[\w.+-]+@[\w-]+(?:\.[\w-]+)+/gu, "<email>")
    .replace(/\bT[0-9A-Z]{4}[0-9A-Z]{6,}\b/gu, "<serial>")
    .replace(/\b\d{1,3}(?:\.\d{1,3}){3}\b/gu, "<address>")
    .replace(/https?:\/\/\S+/gu, "<url>")
    .slice(0, 300);
}
