// Radio: which air-band frequency a plane near the Cardputer is probably on, for listening on an
// ordinary air-band radio (118-137 MHz AM). Served on the bridge's page (web-ui.mjs, "Radio").
//
// Nothing says which frequency a plane is tuned to, so this guesses from what it's doing: low beside
// a towered airport (that tower), climbing away from or coming down to a big airport (its approach
// control's departure or arrival frequency), flying a VFR code 1200 (no controller), or cruising
// (the nearest Center radio sites for its height). The towers and Center sites come from the FAA's
// free 28-day data (NASR, FRQ.csv), downloaded here. Approach control's own split (which frequency
// for which direction, side or runway) differs at every airport and isn't in a usable form there,
// so it comes from an optional area file the owner keeps beside the bridge's state, never in the
// repo: <stateDir>/radio/area.json (see docs/BRIDGE-UI.md#radio).
//
// Where: the position Skywatch last asked about (the Cap's GPS or the saved city), or a town typed on
// the page. Either lives only in memory. What the owner says they heard goes to
// <stateDir>/radio/heard.jsonl, with no position in it.
import { mkdir, readFile, rename, writeFile, appendFile } from "node:fs/promises";
import path from "node:path";
import { inflateRawSync } from "node:zlib";
import { createHash } from "node:crypto";
import { distanceMeters, bearingDegrees } from "./planes.mjs";
import { comparable, parseCoordinates, validPlace } from "./weather.mjs";

const NM = 1852;
const RADIUS_NM = 50;
const FETCH_EVERY_MS = 9000;                // The page asks every 10 s; adsb.lol turns away more.
const HISTORY_MS = 240000;                  // Heights kept per plane, to tell a departure held level.
const SPOT_FROM_PLANES_MS = 12 * 3600000;   // Skywatch's last position is used for this long.
const FAA_CYCLE_DAYS = 28;
const FAA_EPOCH = Date.UTC(2026, 9, 29);    // A known NASR cycle date; the rest are 28 days apart.
const FAA_URL = (date) => {
  const d = new Date(date);
  const mon = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][d.getUTCMonth()];
  return `https://nfdc.faa.gov/webContent/28DaySub/extra/${String(d.getUTCDate()).padStart(2, "0")}_${mon}_${d.getUTCFullYear()}_FRQ_CSV.zip`;
};
const LOGO_SOURCES = ["https://www.gstatic.com/flights/airline_logos/70px/{}.png", "https://images.kiwi.com/airlines/64/{}.png"];
const CENTERS = { ZAB: "Albuquerque", ZAN: "Anchorage", ZAU: "Chicago", ZBW: "Boston", ZDC: "Washington", ZDV: "Denver",
  ZFW: "Fort Worth", ZHU: "Houston", ZID: "Indianapolis", ZJX: "Jacksonville", ZKC: "Kansas City", ZLA: "Los Angeles",
  ZLC: "Salt Lake", ZMA: "Miami", ZME: "Memphis", ZMP: "Minneapolis", ZNY: "New York", ZOA: "Oakland", ZOB: "Cleveland",
  ZSE: "Seattle", ZTL: "Atlanta", ZHN: "Honolulu", ZSU: "San Juan" };
// What pilots say for an airline's three-letter prefix, and its two-letter code (for its logo). The
// rest are asked of adsbdb once each.
const AIRLINES = { AAL: ["American", "AA"], SWA: ["Southwest", "WN"], UAL: ["United", "UA"], DAL: ["Delta", "DL"],
  ENY: ["Envoy", "MQ"], SKW: ["SkyWest", "OO"], JIA: ["Blue Streak", "OH"], ASH: ["Air Shuttle", "YV"], RPA: ["Brickyard", "YX"],
  EDV: ["Endeavor", "9E"], GJS: ["Lindbergh", "G7"], FDX: ["FedEx", "FX"], UPS: ["UPS", "5X"], NKS: ["Spirit Wings", "NK"],
  FFT: ["Frontier Flight", "F9"], JBU: ["JetBlue", "B6"], ASA: ["Alaska", "AS"], AAY: ["Allegiant", "G4"], QXE: ["Horizon", "QX"],
  PDT: ["Piedmont", "PT"], CPZ: ["Compass", "CP"], SCX: ["Sun Country", "SY"], BAW: ["Speedbird", "BA"], ACA: ["Air Canada", "AC"],
  AMX: ["Aeromexico", "AM"], WJA: ["WestJet", "WS"], LXJ: ["Flexjet", ""], EJA: ["Execjet", ""] };

// --- Small helpers ---------------------------------------------------------------------------------
export const angle = (a, b) => Math.abs(((a - b + 540) % 360) - 180);
export const quad = (b) => (b >= 315 || b < 45 ? "N" : b < 135 ? "E" : b < 225 ? "S" : "W");
const COMPASS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
export const compass = (b) => COMPASS[Math.floor(((b + 22.5) % 360) / 45)];
const DIRS = { N: 0, E: 90, S: 180, W: 270 };
const vhf = (f) => { const n = Number(f); return n >= 118 && n < 137; };
const nm = (a, b) => distanceMeters(a.lat, a.lon, b.lat, b.lon) / NM;
const bearing = (a, b) => bearingDegrees(a.lat, a.lon, b.lat, b.lon);
const airlineCall = (cs) => /^[A-Z]{3}\d/u.test(cs);
// The listed direction nearest to the one wanted: an airport may list departures only east and south.
function nearestDir(map, want) {
  if (map?.[want]) return map[want];
  let best = null, gap = 999;
  for (const [dir, value] of Object.entries(map || {})) {
    const g = angle(DIRS[dir] ?? 0, DIRS[want]);
    if (DIRS[dir] !== undefined && g < gap) { gap = g; best = value; }
  }
  return best;
}
const side = (split, from, p) => split === "NS" ? (p.lat > from.lat ? "N" : "S") : (p.lon > from.lon ? "E" : "W");
const first = (v) => (Array.isArray(v) ? v[0] : v) || null;
const second = (v) => (Array.isArray(v) ? v[1] : null) || null;

// --- The FAA's file --------------------------------------------------------------------------------
// One file out of a zip, without a library: the central directory says where it is and how it's packed.
export function unzipOne(zip, wanted) {
  let end = zip.length - 22;
  while (end >= 0 && zip.readUInt32LE(end) !== 0x06054b50) end--;
  if (end < 0) throw new Error("not a zip");
  const count = zip.readUInt16LE(end + 10);
  let at = zip.readUInt32LE(end + 16);
  for (let i = 0; i < count; i++) {
    if (zip.readUInt32LE(at) !== 0x02014b50) break;
    const method = zip.readUInt16LE(at + 10), size = zip.readUInt32LE(at + 20);
    const nameLen = zip.readUInt16LE(at + 28), extraLen = zip.readUInt16LE(at + 30), noteLen = zip.readUInt16LE(at + 32);
    const local = zip.readUInt32LE(at + 42);
    const name = zip.toString("utf8", at + 46, at + 46 + nameLen);
    if (path.basename(name) === wanted) {
      const start = local + 30 + zip.readUInt16LE(local + 26) + zip.readUInt16LE(local + 28);
      const data = zip.subarray(start, start + size);
      if (method === 0) return Buffer.from(data);
      if (method === 8) return inflateRawSync(data);
      throw new Error(`zip method ${method}`);
    }
    at += 46 + nameLen + extraLen + noteLen;
  }
  throw new Error(`${wanted} not in zip`);
}

export function parseCsv(text) {
  const rows = [];
  let row = [], cell = "", quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"') { if (text[i + 1] === '"') { cell += '"'; i++; } else quoted = false; } else cell += c;
    } else if (c === '"') quoted = true;
    else if (c === ",") { row.push(cell); cell = ""; }
    else if (c === "\n" || c === "\r") {
      if (c === "\r" && text[i + 1] === "\n") i++;
      row.push(cell); rows.push(row); row = []; cell = "";
    } else cell += c;
  }
  if (cell || row.length) { row.push(cell); rows.push(row); }
  return rows;
}

// Towers (their local-control frequencies) and Center radio sites, air band only, never 121.5.
export function readFrequencies(csvText) {
  const rows = parseCsv(csvText);
  const head = rows.shift() || [];
  const col = Object.fromEntries(head.map((h, i) => [h.trim(), i]));
  const need = ["FACILITY", "FACILITY_TYPE", "ARTCC_OR_FSS_ID", "SERVICED_FACILITY", "LAT_DECIMAL", "LONG_DECIMAL", "FREQ", "SECTORIZATION", "FREQ_USE"];
  for (const name of need) if (col[name] === undefined) throw new Error(`FRQ.csv lacks ${name}`);
  const towers = [], sites = [];
  for (const r of rows) {
    const freq = String(r[col.FREQ] || "").trim(), lat = Number(r[col.LAT_DECIMAL]), lon = Number(r[col.LONG_DECIMAL]);
    if (!vhf(freq) || Number(freq) === 121.5 || !Number.isFinite(lat) || !Number.isFinite(lon) || !r[col.LAT_DECIMAL]) continue;
    const type = r[col.FACILITY_TYPE] || "", use = r[col.FREQ_USE] || "";
    if (type.startsWith("ATCT") && use.startsWith("LCL"))
      towers.push({ code: r[col.SERVICED_FACILITY], lat, lon, freq, note: (r[col.SECTORIZATION] || "").trim() });
    else if (type === "RCAG")
      sites.push({ center: r[col.ARTCC_OR_FSS_ID], name: titleCase(r[col.FACILITY] || ""), lat, lon, freq, band: r[col.SECTORIZATION] || "" });
  }
  return { towers, sites };
}
const titleCase = (s) => s.toLowerCase().replace(/\b\p{L}/gu, (c) => c.toUpperCase());

export function faaCycle(now) {
  return FAA_EPOCH + Math.floor((now - FAA_EPOCH) / (FAA_CYCLE_DAYS * 86400000)) * FAA_CYCLE_DAYS * 86400000;
}

// --- The guess -------------------------------------------------------------------------------------
// The nearest Center radio sites for a height band: two different frequencies.
export function centerNear(sites, p, alt, count = 2) {
  const want = alt < 18000 ? ["LOW", "LOW/HIGH"] : alt < 35000 ? ["HIGH", "LOW/HIGH", "ULTRA-HIGH"] : ["ULTRA-HIGH", "HIGH", "LOW/HIGH"];
  const near = sites.filter((s) => want.includes(s.band)).map((s) => [nm(p, s), s]).sort((a, b) => a[0] - b[0]);
  const out = [], seen = new Set();
  for (const [, s] of near) {
    if (seen.has(s.freq)) continue;
    seen.add(s.freq);
    out.push({ freq: s.freq, who: `${CENTERS[s.center] || s.center} Center`, via: s.name });
    if (out.length === count) break;
  }
  return out;
}

// a: one adsb.lol aircraft, with rose/fell (feet climbed or come down over the last few minutes).
// area: the owner's area file, or null. Returns { freq, alt2, who, sure, why }.
export function guess(a, { towers = [], sites = [], area = null } = {}) {
  const p = { lat: a.lat, lon: a.lon };
  const alt = a.alt_baro;
  const vr = Number(a.baro_rate ?? a.geom_rate ?? 0) || 0;
  const trk = Number.isFinite(Number(a.track)) ? Number(a.track) : null;
  const sq = String(a.squawk || "");
  const cs = String(a.flight || "").trim().toUpperCase();
  const airline = airlineCall(cs);
  const rose = a.rose || 0, fell = a.fell || 0;
  const climbing = vr > 400 || (rose > 1500 && vr > -300);
  const descending = vr < -300 || (fell > 1500 && vr < 300);
  const g = { freq: null, alt2: null, who: "", sure: "guess", why: "" };
  const airports = (area?.airports || []).map((ap) => ({ ...ap, d: nm(ap, p), b: bearing(ap, p) }));
  const big = new Set(airports.map((ap) => ap.code));
  const away = (ap, slack = 60) => trk !== null && angle(trk, ap.b) < slack;
  const toward = (ap) => trk !== null && angle(trk, (ap.b + 180) % 360) < 60;
  const home = airports.find((ap) => (ap.airlines || []).includes(cs.slice(0, 3)));
  const center = centerNear(sites, p, alt);
  const c0 = center[0] || { freq: null, who: "Center", via: "?" };
  const c1 = center[1] || null;

  // Low beside a towered airport: its tower. An airliner only uses the area's big airports, out to
  // 8 nm on its final; anything else the nearest tower within 5 nm.
  const nearTower = towers.map((t) => [nm(p, t), t]).sort((x, y) => x[0] - y[0]);
  for (const [d, t] of nearTower) {
    if (d >= 8) break;
    if (alt >= 2500) break;
    if (airline && big.size && !big.has(t.code)) continue;
    if (d >= (airline ? 8 : 5)) continue;
    const ap = airports.find((x) => x.code === t.code);
    if (ap?.towers) {
      const s = ap.towers["*"] ? "*" : side(ap.towerSplit || "EW", ap, p);
      const tw = ap.towers[s];
      const word = { E: " (east)", W: " (west)", N: " (north)", S: " (south)" }[s] || "";
      return { ...g, freq: first(tw), alt2: second(tw), who: `${ap.name || ap.code} Tower${word}`, sure: "likely", why: `${alt} ft, ${d.toFixed(1)} nm from ${ap.code}` };
    }
    return { ...g, freq: t.freq, who: `${t.code} Tower`, sure: "likely", why: `${alt} ft, ${d.toFixed(1)} nm from ${t.code}` };
  }
  if (sq === "1200") return { ...g, who: "Not talking to ATC (VFR, code 1200)", sure: "likely", why: "code 1200 means no controller" };
  if (alt >= 24000 && Math.abs(vr) < 1000)
    return { ...g, freq: c0.freq, alt2: c1?.freq || null, who: `${c0.who} (high, via ${c0.via})`, why: `cruising at ${alt} ft; nearest high-altitude radio sites` };
  const approach = area?.approachName || "Approach";
  const departure = (ap) => nearestDir(ap.departures, quad(ap.b));

  // Climbing away from a big airport, or near one: a departure.
  if (climbing && alt < 24000 && airports.length) {
    const leaving = airports.filter((ap) => ap.d < 60 && ap.departures && away(ap));
    let from = home && home.d < 60 ? home : leaving.sort((x, y) => x.d - y.d)[0];
    if (!from && airline) from = airports.filter((ap) => ap.departures && ap.d < 30).sort((x, y) => x.d - y.d)[0];
    if (from) {
      const dep = departure(from);
      const why = `climbing (${vr} ft/min, up ${rose} ft lately) away from ${from.code}, ${alt} ft`;
      if (alt < 13000) return { ...g, freq: dep, alt2: c0.freq, who: `${approach}, departure (${from.code}, ${quad(from.b)})`, sure: "likely", why };
      if (alt < 17000) return { ...g, freq: dep, alt2: c0.freq, who: `Departure, or ${c0.who} by now`, sure: "maybe", why };
      return { ...g, freq: c0.freq, alt2: c1?.freq || dep, who: `${c0.who} (via ${c0.via})`, why };
    }
  }
  // Coming down toward a big airport, or an airliner low near one: an arrival.
  if (descending && airports.length) {
    const hub = airports[0];
    const heading = airports.filter((ap) => toward(ap) && ap.d < 70);
    let to = home && home.d < 40 ? home : null;
    if (!to) {
      const other = heading.filter((ap) => ap !== hub && ap.d < 30 && alt < 9000).sort((x, y) => x.d - y.d)[0];
      to = other && (other.d < hub.d || !heading.includes(hub)) ? other : heading.includes(hub) || (airline && hub.d < 45 && alt < 14000) ? hub : heading[0];
    }
    if (to) {
      const s = side(to.arrivalSplit || "EW", to, p);
      const arr = to.arrivals?.[s];
      const why = `descending (${vr} ft/min, down ${fell} ft lately) toward ${to.code}, ${alt} ft`;
      const sideWord = { E: "east", W: "west", N: "north", S: "south" }[s];
      if (to.finals && to.d < 18 && alt < 6000) {
        const fs = side(to.finalSplit || "EW", to, p);
        return { ...g, freq: to.finals[fs], alt2: first(arr), who: `${approach}, ${to.code} final (${{ E: "east", W: "west", N: "north", S: "south" }[fs]})`, sure: "likely", why };
      }
      if (arr && alt < 18000) return { ...g, freq: first(arr), alt2: second(arr), who: `${approach}, ${to.code} arrivals (${sideWord})`, sure: "likely", why };
      if (arr) return { ...g, freq: c0.freq, alt2: first(arr), who: `${c0.who}, starting down to ${to.code}`, sure: "maybe", why };
    }
  }
  // A departure held level near 10,000 ft, under the arrivals, is still with departure control.
  if (airline && vr > -300 && vr <= 400 && fell < 1000 && alt >= 8000 && alt <= 12500) {
    const from = (home && home.d < 40 && away(home, 90) ? home : null) ||
      airports.filter((ap) => ap.departures && ap.d < 40 && away(ap, 90)).sort((x, y) => x.d - y.d)[0];
    if (from) return { ...g, freq: departure(from), alt2: c0.freq, who: `${approach}, departure (${from.code}, ${quad(from.b)}), held level`,
      sure: "maybe", why: `level at ${alt} ft heading away from ${from.code}: departures are often held near 10,000 ft` };
  }
  // Below 10,000 ft with a code a controller gave it, not clearly arriving or leaving: the local sector.
  if (alt < 10000 && sq && area?.local) {
    const loc = area.local, s = side(loc.split || "NS", loc, p), v = loc[s];
    if (v) return { ...g, freq: first(v), alt2: second(v), who: `${approach}, local sector ${{ E: "east", W: "west", N: "north", S: "south" }[s]}`,
      sure: "maybe", why: `${alt} ft with an assigned code, not clearly arriving or leaving` };
  }
  return { ...g, freq: c0.freq, alt2: c1?.freq || null, who: `${c0.who} (via ${c0.via})`, why: `${alt} ft, not clearly arriving or leaving` };
}

// --- The service -----------------------------------------------------------------------------------
export class RadioService {
  constructor({ stateDir = "", planes = null, weather = null, fetchImpl = fetch, now = Date.now, log = console } = {}) {
    Object.assign(this, { stateDir, planes, weather, fetchImpl, now, log });
    this.dir = stateDir ? path.join(stateDir, "radio") : "";
    this.freqs = null;          // { towers, sites, cycle }
    this.freqsTask = null;
    this.freqsTriedAt = -Infinity;
    this.area = undefined;      // The owner's area file, read once (null when there's none).
    this.typed = null;          // { lat, lon, label }: a town typed on the page, in memory only.
    this.last = { at: 0, planes: [], error: "" };
    this.fetching = null;
    this.history = new Map();   // hex -> [[ms, alt], ...]
    this.names = new Map(Object.entries(AIRLINES));
    this.asked = new Set();     // Airline prefixes already asked of adsbdb.
    this.logos = new Map();     // icao -> { at, png | null }
    this.placeholders = [];
    this.saved = 0;
  }

  // --- FAA data, refreshed each 28-day cycle; the last good copy stays if a download fails.
  async frequencies() {
    if (this.freqs && (this.freqs.cycle === faaCycle(this.now()) || this.now() - this.freqsTriedAt < 600000)) return this.freqs;
    if (!this.freqsTask && this.now() - this.freqsTriedAt > 600000) {
      this.freqsTriedAt = this.now();
      this.freqsTask = this.loadFrequencies().finally(() => { this.freqsTask = null; });
    }
    if (!this.freqs) await this.freqsTask?.catch(() => {});
    return this.freqs || { towers: [], sites: [], cycle: 0 };
  }
  // Kept as <stateDir>/radio/faa.json: the cycle, and only the towers and Center sites (not the 8 MB file).
  async loadFrequencies() {
    const cycle = faaCycle(this.now());
    const file = this.dir ? path.join(this.dir, "faa.json") : "";
    let held = null;
    if (file) try { held = JSON.parse(await readFile(file, "utf8")); } catch {}
    if (held?.cycle === cycle && Array.isArray(held.towers) && Array.isArray(held.sites)) return (this.freqs = held);
    for (const at of [cycle, cycle - FAA_CYCLE_DAYS * 86400000]) {
      if (held?.cycle === at && at !== cycle) break;  // The older copy is that one already.
      try {
        const response = await this.fetchImpl(FAA_URL(at), { signal: AbortSignal.timeout(60000) });
        if (!response.ok) throw new Error(`http_${response.status}`);
        const fresh = { cycle: at, ...readFrequencies(unzipOne(Buffer.from(await response.arrayBuffer()), "FRQ.csv").toString("latin1")) };
        if (!fresh.towers.length || !fresh.sites.length) throw new Error("no towers or sites in it");
        if (file) {
          await mkdir(this.dir, { recursive: true });
          await writeFile(`${file}.tmp`, JSON.stringify(fresh));
          await rename(`${file}.tmp`, file);
        }
        this.log.info?.(`[radio] FAA frequencies for ${new Date(at).toISOString().slice(0, 10)}`);
        held = fresh;
        break;
      } catch (error) {
        this.log.warn?.(`[radio] FAA frequencies failed: ${String(error?.message || error).slice(0, 80)}`);
      }
    }
    if (!held?.towers) throw new Error("no FAA frequencies");
    // An older copy serves until the next try, ten minutes on.
    this.freqs = { ...held, cycle: held.cycle === cycle ? cycle : held.cycle };
    return this.freqs;
  }

  async areaFile() {
    if (this.area !== undefined) return this.area;
    try { this.area = this.dir ? JSON.parse(await readFile(path.join(this.dir, "area.json"), "utf8")) : null; }
    catch { this.area = null; }
    return this.area;
  }

  // --- Where: a typed town, else where Skywatch last asked about.
  spot() {
    if (this.typed) return { ...this.typed, from: "typed" };
    const at = this.planes?.lastAt;
    if (at && this.now() - at.atMs < SPOT_FROM_PLANES_MS) return { lat: at.latitude, lon: at.longitude, label: "the Cardputer's position", from: "cardputer" };
    return null;
  }
  async setSpot(text) {
    const clean = String(text || "").trim();
    if (!clean) { this.typed = null; this.last = { at: 0, planes: [], error: "" }; return { ok: true, cleared: true }; }
    const parts = clean.split(",").map((s) => s.trim());
    let spot = null;
    if (parts.length === 2) {
      const at = parseCoordinates(parts[0], parts[1]);
      if (at) spot = { lat: at.latitude, lon: at.longitude, label: "the position you typed" };
    }
    if (!spot) {
      if (!validPlace(clean) || !this.weather) return { error: "Type a town and state, like Salem, MA" };
      try {
        const found = await this.weather.place(comparable(clean), clean);
        spot = { lat: found.latitude, lon: found.longitude, label: found.name };
      } catch { return { error: "I couldn't find that town" }; }
    }
    this.typed = spot;
    this.last = { at: 0, planes: [], error: "" };
    this.history.clear();
    return { ok: true, label: spot.label };
  }

  // --- What the page shows: the planes within 50 nm with a guess each, and the frequency list.
  async state() {
    const spot = this.spot();
    const area = await this.areaFile();
    const freqs = await this.frequencies().catch(() => ({ towers: [], sites: [] }));
    if (!spot) return { spot: false, area: !!area, faa: !!freqs.towers.length, saved: this.saved };
    if (this.now() - this.last.at >= FETCH_EVERY_MS) {
      this.fetching ??= this.refresh(spot, freqs, area).finally(() => { this.fetching = null; });
      await this.fetching;
    }
    return { spot: true, label: spot.label, from: spot.from, area: !!area, faa: !!freqs.towers.length,
      age: this.last.at ? Math.round((this.now() - this.last.at) / 1000) : 0, error: this.last.error, saved: this.saved,
      catalog: this.catalog(spot, freqs, area), planes: this.last.planes };
  }

  async refresh(spot, freqs, area) {
    try {
      const list = await this.planes.aircraftNear({ latitude: spot.lat, longitude: spot.lon }, RADIUS_NM);
      const now = this.now();
      for (const [hex, h] of this.history) if (now - h[h.length - 1][0] > 300000) this.history.delete(hex);
      const planes = [];
      for (const ac of list) {
        const lat = Number(ac.lat), lon = Number(ac.lon), alt = Number(ac.alt_baro);
        if (!Number.isFinite(lat) || !Number.isFinite(lon) || !Number.isFinite(alt)) continue;
        const where = { lat, lon };
        const d = nm(spot, where);
        if (d > RADIUS_NM) continue;
        const hex = String(ac.hex || "").replace(/^~/u, "").toLowerCase();
        const h = this.history.get(hex) || [];
        h.push([now, alt]);
        while (h.length && now - h[0][0] > HISTORY_MS) h.shift();
        this.history.set(hex, h);
        const alts = h.map((x) => x[1]);
        const g = guess({ ...ac, rose: alt - Math.min(...alts), fell: Math.max(...alts) - alt }, { towers: freqs.towers, sites: freqs.sites, area });
        const cs = String(ac.flight || "").trim().toUpperCase();
        const hub = area?.airports?.[0];
        planes.push({ hex, cs, say: this.spoken(cs) || hex, type: String(ac.t || ""), alt, vr: Number(ac.baro_rate ?? ac.geom_rate ?? 0) || 0,
          mi: Math.round(d * 1.15078 * 10) / 10, dir: compass(bearing(spot, where)), near: d <= 4.35,
          trk: Number.isFinite(Number(ac.track)) ? Math.round(Number(ac.track)) : -1,
          hub: hub ? [Math.round(nm(hub, where) * 10) / 10, Math.round(bearing(hub, where))] : null, ...g });
      }
      planes.sort((x, y) => x.mi - y.mi);
      this.last = { at: now, planes, error: "" };
      this.learnAirline();
    } catch (error) {
      this.last = { ...this.last, at: this.now(), error: "Flight data isn't answering. Trying again..." };
      this.log.warn?.(`[radio] planes failed: ${String(error?.code || error?.message || error).slice(0, 60)}`);
    }
  }

  // The frequencies to pick from, in a fixed order: the area file's groups, then the towers within
  // 25 nm and Center sites within 80 nm, each group in number order; anything a guess uses that isn't
  // there goes under Other.
  catalog(spot, freqs, area) {
    const groups = (area?.catalog || []).map(([name, rows]) => [name, rows.map(([f, l]) => [String(f), String(l)])]);
    const listed = new Set(groups.flatMap(([, rows]) => rows.map(([f]) => f)));
    const towers = new Map();
    for (const ap of area?.airports || []) for (const [s, v] of Object.entries(ap.towers || {})) {
      const all = Array.isArray(v) ? v : [v];
      all.forEach((f, i) => towers.set(f, `${ap.name || ap.code} Tower${s === "*" ? "" : ` ${{ E: "east", W: "west", N: "north", S: "south" }[s]}`}${i ? ", second" : ""}`));
    }
    for (const t of freqs.towers) if (!towers.has(t.freq) && nm(spot, t) < 25) towers.set(t.freq, `${t.code} Tower${t.note ? ` (${t.note.toLowerCase()})` : ""}`);
    const centers = new Map();
    for (const s of freqs.sites) if (!centers.has(s.freq) && nm(spot, s) < 80)
      centers.set(s.freq, `${CENTERS[s.center] || s.center} Center via ${s.name} (${s.band.toLowerCase()})`);
    for (const [f] of towers) listed.add(f);
    for (const [f] of centers) listed.add(f);
    const other = new Map();
    for (const p of this.last.planes) for (const f of [p.freq, p.alt2]) if (f && !listed.has(f) && !other.has(f)) other.set(f, p.who);
    const byNumber = (m) => [...m].sort((x, y) => Number(x[0]) - Number(y[0]));
    return [...groups.map(([n, rows]) => [n, rows.sort((x, y) => Number(x[0]) - Number(y[0]))]),
      ["Towers", byNumber(towers)], ["Center", byNumber(centers)], ["Other", byNumber(other)]].filter(([, rows]) => rows.length);
  }

  // --- Names pilots say, and logos.
  spoken(cs) {
    if (/^N\d/u.test(cs)) return `November ${cs.slice(1)}`;
    if (!airlineCall(cs)) return cs;
    const name = this.names.get(cs.slice(0, 3))?.[0];
    return name ? `${name} ${cs.slice(3).replace(/^0+(?=\d)/u, "")}` : cs;
  }
  // One unknown airline at a time, from a real call sign seen in traffic.
  learnAirline() {
    if (this.learning || !this.planes?.getJson) return;
    const cs = this.last.planes.map((p) => p.cs).find((c) => airlineCall(c) && !this.names.has(c.slice(0, 3)) && !this.asked.has(c.slice(0, 3)));
    if (!cs) return;
    this.asked.add(cs.slice(0, 3));
    this.learning = this.planes.getJson(`https://api.adsbdb.com/v0/callsign/${encodeURIComponent(cs)}`, { timeoutMs: 6000 })
      .then((body) => {
        const al = body?.response?.flightroute?.airline || {};
        const name = String(al.callsign || "").toLowerCase().replace(/\b\p{L}/gu, (c) => c.toUpperCase());
        if (name || al.iata) this.names.set(cs.slice(0, 3), [name, String(al.iata || "")]);
      }).catch(() => {}).finally(() => { this.learning = null; });
  }
  async logo(icao) {
    if (!/^[A-Z]{3}$/u.test(icao)) return null;
    const held = this.logos.get(icao);
    if (held && (held.png || this.now() - held.at < 3600000)) return held.png;
    const file = this.dir ? path.join(this.dir, "logos", `${icao}.png`) : "";
    if (file) try { const png = await readFile(file); this.logos.set(icao, { at: this.now(), png }); return png; } catch {}
    const iata = this.names.get(icao)?.[1];
    if (!iata) { this.logos.set(icao, { at: this.now(), png: null }); return null; }
    for (let i = 0; i < LOGO_SOURCES.length; i++) {
      const png = await this.png(LOGO_SOURCES[i].replace("{}", iata));
      if (png && sha(png) !== await this.placeholder(i)) {
        if (file) { await mkdir(path.dirname(file), { recursive: true }); await writeFile(file, png); }
        this.logos.set(icao, { at: this.now(), png });
        return png;
      }
    }
    this.logos.set(icao, { at: this.now(), png: null });
    return null;
  }
  async png(url) {
    try {
      const response = await this.fetchImpl(url, { signal: AbortSignal.timeout(8000) });
      if (!response.ok) return null;
      const buf = Buffer.from(await response.arrayBuffer());
      return buf.length < 200000 && buf.subarray(0, 4).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47])) ? buf : null;
    } catch { return null; }
  }
  // What a source sends for an airline it has no logo for: its answer for a made-up code.
  async placeholder(i) {
    if (this.placeholders[i] === undefined) { const png = await this.png(LOGO_SOURCES[i].replace("{}", "ZZ")); this.placeholders[i] = png ? sha(png) : ""; }
    return this.placeholders[i];
  }

  // --- "I heard it": what was guessed, never where the owner is.
  async heard(body) {
    const listening = String(body?.listening || "").slice(0, 12);
    if (!/^\d{3}\.\d{1,3}$/u.test(listening)) return { error: "bad_frequency" };
    let rec;
    if (body.typed) rec = { mode: "typed", listening, text: String(body.typed).slice(0, 60) };
    else {
      const p = this.last.planes.find((x) => x.hex === body.hex) || null;
      if (!p) return { error: "not_found" };
      const keep = ["cs", "say", "type", "alt", "vr", "mi", "trk", "hub", "who", "sure", "freq", "alt2", "why"];
      rec = { mode: "freq", listening, ...Object.fromEntries(keep.map((k) => [k, p[k]])),
        predicted: p.freq === listening ? "first" : p.alt2 === listening ? "second" : "no" };
    }
    rec.t = Math.round(this.now() / 1000);
    if (this.dir) { await mkdir(this.dir, { recursive: true }); await appendFile(path.join(this.dir, "heard.jsonl"), `${JSON.stringify(rec)}\n`); }
    this.saved++;
    return { ok: true, predicted: rec.predicted || null };
  }
}
const sha = (buf) => createHash("sha256").update(buf).digest("hex");
