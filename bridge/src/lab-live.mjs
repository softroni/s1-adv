// The Lab, live: while the Lab is open on the Cardputer at home, it posts what its parts read and
// what its outputs are set to, and the bridge's web page draws every part from that.
//
// The device asks for a ticket over HTTPS with its token, then posts over the home door, the
// plain-HTTP port Podcasts, Cameras and Planes already use, so a report every second costs it no TLS.
//   GET  /api/cardputer/lab/ticket   (HTTPS)  "none", or "ticket <port> /lab/<id>" and "lan <ip> <prefix>" lines
//   POST /lab/<ticket>/live          (home)   the snapshot below; answers "watch 1" or "watch 0"
//
// A snapshot is tab-separated lines. The page knows these, and keeps any other line as it came:
//   lab 1 | view <name> | open <part> | page <n> | units us|metric | playing 0|1
//   found <unit,unit> | wires <white> <yellow> | hub <p0,...,p5>
//   sig <key> <int> | out <key> <int> | gps <fix> <used> <in view> <altitude m> <hdop x10>
import { randomBytes } from "node:crypto";
import { homeAddress, lanAddresses } from "./podcasts.mjs";

const TICKET_MS = 12 * 60 * 60 * 1000;
const MAX_TICKETS = 16;
const MAX_BODY = 4096;
const MAX_LINES = 80;
// A page that asked in the last few seconds is watching, so the device reports every second.
const WATCH_MS = 8000;
// About two minutes of readings at one a second, for the page's graphs.
const HISTORY = 120;
// A part that hasn't reported in this long is drawn as the last reading, not as live.
export const STALE_MS = 20000;

const word = (value, max = 32) => String(value ?? "").replace(/[\u0000-\u001f\u007f]/gu, "").trim().slice(0, max);
const integer = (value) => {
  const number = Number(value);
  return Number.isInteger(number) && Math.abs(number) <= 1_000_000 ? number : null;
};
const list = (value) => word(value, 240).split(",").map((item) => word(item)).filter(Boolean).slice(0, 12);

// Unknown lines are kept, so newer firmware can add one before the page knows how to draw it.
export function parseSnapshot(text) {
  const out = { view: "", open: "", page: 0, units: "metric", playing: false, found: [], wires: null, hub: null,
    sigs: {}, outs: {}, gps: null, extra: {} };
  const rows = String(text || "").split(/\r?\n/u).slice(0, MAX_LINES);
  if (!/^lab\t1\b/u.test(rows[0] || "")) return null;
  for (const row of rows.slice(1)) {
    const f = row.split("\t");
    const key = word(f[0], 16);
    if (!key) continue;
    if (key === "view") out.view = word(f[1], 16);
    else if (key === "open") out.open = word(f[1]);
    else if (key === "page") out.page = Math.max(0, integer(f[1]) ?? 0);
    else if (key === "units") out.units = f[1] === "us" ? "us" : "metric";
    else if (key === "playing") out.playing = f[1] === "1";
    else if (key === "found") out.found = list(f[1]);
    else if (key === "wires") out.wires = { white: word(f[1]), yellow: word(f[2]) };
    else if (key === "hub") out.hub = list(f[1]).slice(0, 6);
    else if (key === "sig" || key === "out") {
      const name = word(f[1], 24), value = integer(f[2]);
      if (/^[a-z0-9_]+$/u.test(name) && value !== null) out[key === "sig" ? "sigs" : "outs"][name] = value;
    } else if (key === "gps") {
      const [fix, used, view, altitude, hdop] = f.slice(1).map(integer);
      out.gps = { fix: fix === 1, used: used ?? 0, inView: view ?? 0, altitude: altitude ?? null,
        hdop: hdop === null || hdop === undefined || hdop < 0 ? null : hdop / 10 };
    } else if (/^[a-z0-9_]+$/u.test(key) && Object.keys(out.extra).length < 24) {
      out.extra[key] = f.slice(1).map((item) => word(item, 48)).join(" ");
    }
  }
  return out;
}

export class LabLiveService {
  constructor({ now = () => Date.now() } = {}) {
    this.now = now;
    this.tickets = new Map();
    this.lanPort = 0;
    this.latest = null;      // { at, snapshot }
    this.history = new Map(); // sig key -> [[at, value], ...]
    this.watchedAt = 0;
    this.reports = 0;
  }

  // --- The home door's tickets, as Planes hands them out ---
  ticket(device) {
    const now = this.now();
    for (const [id, held] of this.tickets) if (held.expires <= now) this.tickets.delete(id);
    for (const [id, held] of this.tickets) if (held.device === device) { held.expires = now + TICKET_MS; return id; }
    while (this.tickets.size >= MAX_TICKETS) this.tickets.delete(this.tickets.keys().next().value);
    const id = randomBytes(16).toString("hex");
    this.tickets.set(id, { device, expires: now + TICKET_MS });
    return id;
  }
  redeem(id) {
    const held = this.tickets.get(id);
    return held && held.expires > this.now() ? held : null;
  }
  ticketLines(device, interfaces) {
    const addresses = this.lanPort ? lanAddresses(interfaces) : [];
    if (!addresses.length) return "none\n";
    const id = this.ticket(device);
    return [`ticket\t${this.lanPort}\t/lab/${id}`, ...addresses.map((entry) => `lan\t${entry.address}\t${entry.prefix}`)].join("\n") + "\n";
  }

  // --- What the device sent ---
  accept(text) {
    const snapshot = parseSnapshot(text);
    if (!snapshot) return false;
    const at = this.now();
    if (!this.latest || this.latest.snapshot.view === "closed") console.info(`[lab-live] the Cardputer's Lab is ${snapshot.view === "closed" ? "closed" : "reporting"}`);
    else if (snapshot.view === "closed") console.info("[lab-live] the Cardputer's Lab is closed");
    this.latest = { at, snapshot };
    this.reports += 1;
    for (const [key, value] of Object.entries(snapshot.sigs)) {
      const points = this.history.get(key) || [];
      points.push([at, value]);
      while (points.length > HISTORY) points.shift();
      this.history.set(key, points);
    }
    return true;
  }

  // The page asks for this about once a second, and asking is what makes the device hurry.
  watch() { this.watchedAt = this.now(); }
  watching() { return this.now() - this.watchedAt < WATCH_MS; }

  view() {
    const now = this.now();
    const latest = this.latest;
    const history = {};
    for (const [key, points] of this.history) {
      const recent = points.filter(([at]) => now - at <= HISTORY * 1000);
      if (recent.length) history[key] = recent.map(([at, value]) => [Math.round((at - now) / 100) / 10, value]);
    }
    return {
      reported: Boolean(latest),
      age_ms: latest ? now - latest.at : null,
      live: Boolean(latest) && latest.snapshot.view !== "closed" && now - latest.at < STALE_MS,
      lan_port: this.lanPort,
      snapshot: latest?.snapshot || null,
      history
    };
  }
}

function plain(res, status, body) {
  const data = Buffer.from(body, "utf8");
  res.writeHead(status, { "content-type": "text/plain; charset=utf-8", "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}

// The HTTPS side: only the ticket, with the device's token already checked.
export function labTicketRoute(service, device, res) {
  plain(res, 200, service.ticketLines(device));
}

// The home door's: POST /lab/<ticket>/live, from home addresses only. False when the path isn't
// the Lab's, so the next door can have it.
export function labLanRoute(service, req, res) {
  let url;
  try { url = new URL(req.url || "/", "http://lan"); } catch { return false; }
  if (!url.pathname.startsWith("/lab/")) return false;
  const match = url.pathname.match(/^\/lab\/([0-9a-f]{32})\/live$/u);
  const remote = String(req.socket?.remoteAddress || "").replace(/^::ffff:/u, "");
  const held = req.method === "POST" && match && (homeAddress(remote) || remote === "127.0.0.1") ? service.redeem(match[1]) : null;
  if (!held) {
    req.resume();
    plain(res, 404, "not_found\n");
    return true;
  }
  const chunks = [];
  let size = 0;
  req.on("data", (chunk) => {
    size += chunk.length;
    if (size > MAX_BODY) { req.destroy(); return; }
    chunks.push(chunk);
  });
  req.on("end", () => {
    if (size > MAX_BODY) return;
    if (!service.accept(Buffer.concat(chunks).toString("utf8"))) { plain(res, 400, "bad_snapshot\n"); return; }
    plain(res, 200, `watch\t${service.watching() ? 1 : 0}\n`);
  });
  req.on("error", () => res.destroy());
  return true;
}
