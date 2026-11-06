// The Cameras app's recordings: what's stored on the HomeBase, by day, and the clips themselves.
// Pure parts, shared by the camera helper and its tests; nothing here needs the eufy library.
//
// A HomeBase 3 lists a day's recordings with the by-date query (cameras-helper.mjs), and sends a
// recording, faster than real time, as frames on the station's recording data type:
//   video  22-byte header (u32@0 length · u8@4 keyframe · u16@6 frame number · u48@0x0e epoch ms),
//          then that many bytes of Annex-B H.264 or H.265, in the clear (cipher 0)
//   audio  a 16-byte header, then an ADTS AAC frame
import { cameraId } from "./cameras-media.mjs";

export const VIDEO_HEADER = 22;
export const AUDIO_HEADER = 16;

export function parseVideoFrame(raw) {
  if (!raw || raw.length < VIDEO_HEADER) return null;
  const length = raw.readUInt32LE(0);
  if (length <= 0 || VIDEO_HEADER + length > raw.length) return null;
  const data = raw.subarray(VIDEO_HEADER, VIDEO_HEADER + length);
  if (!(data[0] === 0 && data[1] === 0 && (data[2] === 1 || (data[2] === 0 && data[3] === 1)))) return null;  // Not Annex-B: encrypted.
  return { key: raw[4] === 1, number: raw.readUInt16LE(6), ts: raw.readUIntLE(0x0e, 6), data };
}

// An audio frame's ADTS, where its sync word is: just after the header on a HomeBase 3, else looked for.
export function parseAudioFrame(raw) {
  if (!raw || raw.length < AUDIO_HEADER + 7) return null;
  const sync = (at) => raw[at] === 0xff && (raw[at + 1] & 0xf6) === 0xf0;
  let at = sync(AUDIO_HEADER) ? AUDIO_HEADER : -1;
  for (let i = 0; at < 0 && i + 7 < Math.min(raw.length, 64); ++i) if (sync(i)) at = i;
  if (at < 0) return null;
  const frameLength = ((raw[at + 3] & 0x03) << 11) | (raw[at + 4] << 3) | (raw[at + 5] >> 5);
  if (frameLength < 7 || at + frameLength > raw.length) return null;
  return raw.subarray(at, at + frameLength);
}

// The codec, from the first NAL unit's type: a VPS, SPS or PPS of H.265 (32-34), or H.264's (7, 8).
export function videoCodecOf(annexB) {
  if (!annexB || annexB.length < 5) return "";
  const at = annexB[2] === 1 ? 3 : 4;
  const byte = annexB[at];
  const h265 = (byte >> 1) & 0x3f, h264 = byte & 0x1f;
  if (h265 >= 32 && h265 <= 34 && (byte & 0x81) === 0) return "h265";
  if (h264 === 7 || h264 === 8 || h264 === 5) return "h264";
  return "";
}

// --- Days ------------------------------------------------------------------------------------
export function validDay(day) {
  if (!/^\d{8}$/u.test(String(day))) return false;
  const d = new Date(Date.UTC(+day.slice(0, 4), +day.slice(4, 6) - 1, +day.slice(6, 8)));
  return dayString(d) === day;
}
function dayString(d) {
  return `${d.getUTCFullYear()}${String(d.getUTCMonth() + 1).padStart(2, "0")}${String(d.getUTCDate()).padStart(2, "0")}`;
}
export function addDays(day, n) {
  return dayString(new Date(Date.UTC(+day.slice(0, 4), +day.slice(4, 6) - 1, +day.slice(6, 8) + n)));
}
export function today(now = new Date()) {
  return `${now.getFullYear()}${String(now.getMonth() + 1).padStart(2, "0")}${String(now.getDate()).padStart(2, "0")}`;
}
export function validMonth(ym) {
  return /^\d{6}$/u.test(String(ym)) && +ym.slice(4) >= 1 && +ym.slice(4) <= 12;
}
// The month's first day and the next month's first, the by-date queries' end being the day after.
export function monthRange(ym) {
  const start = `${ym}01`;
  const next = new Date(Date.UTC(+ym.slice(0, 4), +ym.slice(4, 6), 1));
  return { start, end: dayString(next) };
}

// --- The archive's rows ----------------------------------------------------------------------
// "2026-11-05 19:20:48" -> seconds since the day began, and the length in seconds.
function clock(text) {
  const m = /(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})/u.exec(String(text || ""));
  return m ? { day: `${m[1]}${m[2]}${m[3]}`, seconds: +m[4] * 3600 + +m[5] * 60 + +m[6] } : null;
}
// A row as the helper keeps it; null for one with no recording to play.
export function clipFromRow(row) {
  const start = clock(row?.start_time), end = clock(row?.end_time);
  if (!start || !row.storage_path || !row.device_sn || !(Number(row.frame_num) > 0)) return null;
  const id = Number(row.record_id);  // A HomeBase 3's are 13 digits: yyyyMMdd and a number.
  if (!Number.isSafeInteger(id) || id < 0) return null;
  let seconds = end ? end.seconds - start.seconds + (end.day !== start.day ? 86400 : 0) : 0;
  if (!(seconds > 0)) seconds = Math.max(1, Math.round(Number(row.frame_num) / 15));
  return { id, day: start.day, start: start.seconds, seconds, sn: row.device_sn, camId: cameraId(row.device_sn), path: String(row.storage_path),
    frames: Number(row.frame_num), cipher: Number(row.cipher_id) || 0 };
}

// --- What the device reads -------------------------------------------------------------------
//   clips <day> <count>
//   clip <id> <seconds since the day began> <length in seconds> <camera id>      newest first
export function clipsLines(day, clips) {
  const sorted = [...clips].sort((a, b) => b.start - a.start || b.id - a.id);
  return [`clips\t${day}\t${sorted.length}`, ...sorted.map(c => `clip\t${c.id}\t${c.start}\t${c.seconds}\t${c.camId}`)].join("\n") + "\n";
}
//   month <yyyymm>
//   day <d> <count>        for each day with recordings
export function monthLines(ym, counts) {
  const lines = [`month\t${ym}`];
  for (const [day, count] of [...counts.entries()].sort()) {
    if (day.startsWith(ym) && count > 0) lines.push(`day\t${Number(day.slice(6))}\t${count}`);
  }
  return lines.join("\n") + "\n";
}
//   clip <position ms> <length ms> <playing 1|0> <ended 1|0>
export function clipStateLine(state) {
  const n = (v) => Math.max(0, Math.round(Number(v) || 0));
  return `clip\t${n(state?.pos)}\t${n(state?.dur)}\t${state?.playing ? 1 : 0}\t${state?.ended ? 1 : 0}\n`;
}
