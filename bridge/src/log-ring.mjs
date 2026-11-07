import { format } from "node:util";

// The bridge's own log, kept for the web page: each line gets the time it was written, in front,
// where bridge.log never had one, and the newest few thousand stay in memory for the Logs screen.
const MAX = 4000;

export function stampTime(date = new Date()) {
  const p = (n, w = 2) => String(n).padStart(w, "0");
  return `${date.getFullYear()}-${p(date.getMonth() + 1)}-${p(date.getDate())} ` +
    `${p(date.getHours())}:${p(date.getMinutes())}:${p(date.getSeconds())}`;
}

// "[cameras] 5 camera(s)" -> "cameras". A line with no tag is the bridge's own.
export function lineTag(text) {
  const match = /^\[([a-z][a-z0-9-]{0,23})\]/u.exec(String(text || ""));
  return match ? match[1] : "bridge";
}

export class LogRing {
  constructor(max = MAX) {
    this.max = max;
    this.lines = [];
    this.seq = 0;
  }
  push(level, text, at = Date.now()) {
    for (const part of String(text).split("\n")) {
      this.seq += 1;
      this.lines.push({ seq: this.seq, at, level, tag: lineTag(part), text: part.slice(0, 2000) });
    }
    if (this.lines.length > this.max) this.lines.splice(0, this.lines.length - this.max);
  }
}

// Every console line goes to the ring and out with its time in front. Done once, at start-up.
export function installLogRing(ring, target = console) {
  for (const level of ["log", "info", "warn", "error"]) {
    const original = target[level].bind(target);
    target[level] = (...args) => {
      const now = new Date();
      const text = format(...args);
      ring.push(level === "log" ? "info" : level, text, now.getTime());
      original(`${stampTime(now)} ${text}`);
    };
  }
  return ring;
}

// bridge.log's lines, from before this start too, as the ring has them. Lines from before times
// were written have none.
const STAMPED = /^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d) (.*)$/su;
export function parseLogFileLines(text) {
  const out = [];
  for (const raw of String(text || "").split("\n")) {
    if (!raw) continue;
    const match = STAMPED.exec(raw);
    const at = match ? new Date(+match[1], +match[2] - 1, +match[3], +match[4], +match[5], +match[6]).getTime() : null;
    const body = match ? match[7] : raw;
    const level = /\b(warn|failed|error|refused|rejected)\b/iu.test(body) ? "warn" : "info";
    out.push({ at, level, tag: lineTag(body), text: body.slice(0, 2000) });
  }
  return out;
}
