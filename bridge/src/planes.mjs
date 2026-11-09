// Planes for the Cardputer: what's flying near it, and when the space station passes over. The
// device trusts one root certificate and reads no JSON here, so the bridge asks the services and
// answers in tab-separated lines. Over HTTPS with the device's token, and at home over the plain-HTTP
// door with a ticket, as cameras do, so a screen that asks every few seconds doesn't pay for TLS each time.
//
// Routes, under /api/cardputer/planes/ (HTTPS) or /planes/<ticket>/ (the home door):
//   ticket                         where the home door is, or "none" (HTTPS only)
//   near?<position>&r=<meters>     "at <place>", then a "plane ..." line for each one in the air, nearest first
//   iss?<position>                 "at <place>", "now ..." where the station is, then "pass ..." lines
//   report?k=v...                  a line in the log from the device, "ok"
// <position> is lat=&lon= (the Cap's GPS, two decimals kept) or place= (the saved city).
// Text fields are never empty: an empty one is "-". Positions and place names are never logged.
//
// Sources: adsb.lol (planes, ODbL), adsbdb (routes, airlines and types), Open-Meteo (ground height),
// CelesTrak (the station's orbit, worked forward with satellite.js), OpenStreetMap Nominatim (the
// country under the station) and corquaid's people-in-space list (who's on board).
import { randomBytes } from "node:crypto";
import { mkdir, readFile, stat, writeFile } from "node:fs/promises";
import path from "node:path";
import * as satellite from "satellite.js";
import { homeAddress, lanAddresses } from "./podcasts.mjs";
import { HERE, comparable, parseCoordinates, validPlace } from "./weather.mjs";

const USER_AGENT = "S1-ADV-Planes/1";
const TICKET_MS = 12 * 60 * 60 * 1000;
const MAX_TICKETS = 16;
const REQUEST_TIMEOUT_MS = 4000;
const NEAR_FRESH_MS = 8000;
const ROUTE_MS = 6 * 60 * 60 * 1000, ROUTE_MISS_MS = 60 * 60 * 1000, LOOKUP_ERROR_MS = 10 * 60 * 1000;
const AIRCRAFT_MS = 24 * 60 * 60 * 1000;
const LOOKUP_BUDGET_MS = 1500, MAX_NEW_LOOKUPS = 8;
const GROUND_RETRY_MS = 10 * 60 * 1000;
const TLE_FRESH_MS = 6 * 60 * 60 * 1000, TLE_STALE_MS = 3 * 24 * 60 * 60 * 1000, TLE_RETRY_MS = 10 * 60 * 1000;
const PASS_FRESH_MS = 30 * 60 * 1000, PASS_DAYS = 10, MAX_PASSES = 8;
const OVER_MS = 30 * 60 * 1000, OVER_RETRY_MS = 2 * 60 * 1000, NOMINATIM_GAP_MS = 2000, NOMINATIM_TIMEOUT_MS = 2500;
const CREW_MS = 12 * 60 * 60 * 1000, CREW_RETRY_MS = 30 * 60 * 1000;
const MAX_PLANES = 24;
const PRIVATE_KEYS = new Set(["lat", "lon", "latitude", "longitude", "place", "city"]);  // Never in the log.
const DEFAULT_RADIUS_M = 40000, MIN_RADIUS_M = 3000, MAX_RADIUS_M = 100000;
const EARTH_M = 6371000, EARTH_KM = 6378.137, AU_KM = 149597870.7;
const FEET = 0.3048, KNOT = 1852 / 3600;
const MIN_ELEVATION = 10, TWILIGHT = -6;
const MIN_VISIBLE_MS = 60 * 1000, MIN_PEAK = 15;  // Shorter or lower passes are left out.

export const URLS = Object.freeze({
  planes: "https://api.adsb.lol/v2/point",
  routes: "https://api.adsbdb.com/v0/callsign",
  aircraft: "https://api.adsbdb.com/v0/aircraft",
  elevation: "https://api.open-meteo.com/v1/elevation",
  tle: "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE",
  reverse: "https://nominatim.openstreetmap.org/reverse",
  crew: "https://corquaid.github.io/international-space-station-APIs/JSON/people-in-space.json",
});

export class PlanesError extends Error {
  constructor(code, status = 503) { super(code); this.code = code; this.status = status; }
}

// --- Text for the device -------------------------------------------------------------------
const LETTERS = Object.freeze({ "ß": "ss", "ø": "o", "Ø": "O", "æ": "ae", "Æ": "AE", "œ": "oe", "Œ": "OE", "ł": "l", "Ł": "L",
  "đ": "d", "Đ": "D", "ð": "d", "þ": "th", "ı": "i", "’": "'", "‘": "'", "“": "\"", "”": "\"", "–": "-", "—": "-" });

// One field: plain ASCII where accents can simply go, no tabs or control characters, at most
// maxBytes of UTF-8 cut on a character, and "-" for nothing.
export function field(value, maxBytes) {
  let text = String(value ?? "").normalize("NFKD").replace(/\p{M}/gu, "");
  text = text.replace(/[ßøØæÆœŒłŁđĐðþı’‘“”–—]/gu, (c) => LETTERS[c] || c);
  text = text.replace(/[\p{Cc}\p{Cf}\u2028\u2029]/gu, " ").replace(/\s+/gu, " ").trim();
  if (Buffer.byteLength(text, "utf8") > maxBytes) {
    let out = "";
    for (const ch of text) {
      if (Buffer.byteLength(out + ch, "utf8") > maxBytes) break;
      out += ch;
    }
    text = out.trim();
  }
  return text || "-";
}
const int = (value) => (Number.isFinite(value) ? Math.round(value) : 0) || 0;  // No "-0".

// --- Geometry ------------------------------------------------------------------------------
const rad = (deg) => (deg * Math.PI) / 180;
const deg = (r) => (r * 180) / Math.PI;
const norm360 = (d) => ((d % 360) + 360) % 360;

export function distanceMeters(lat1, lon1, lat2, lon2) {
  const a = Math.sin(rad(lat2 - lat1) / 2) ** 2 + Math.cos(rad(lat1)) * Math.cos(rad(lat2)) * Math.sin(rad(lon2 - lon1) / 2) ** 2;
  return 2 * EARTH_M * Math.asin(Math.min(1, Math.sqrt(a)));
}
export function bearingDegrees(lat1, lon1, lat2, lon2) {
  const y = Math.sin(rad(lon2 - lon1)) * Math.cos(rad(lat2));
  const x = Math.cos(rad(lat1)) * Math.sin(rad(lat2)) - Math.sin(rad(lat1)) * Math.cos(rad(lat2)) * Math.cos(rad(lon2 - lon1));
  return norm360(deg(Math.atan2(y, x)));
}
// How high above the horizon a plane looks, with the Earth curving away beneath it.
export function elevationDegrees(altitudeM, groundM, distanceM) {
  const d = Math.max(0, distanceM);
  return deg(Math.atan2(altitudeM - groundM - (d * d) / (2 * EARTH_M), d));
}
// When a plane flying straight on comes closest, on a flat map with the observer in the middle:
// seconds from now (at most an hour) and how far it is then, or -1, -1 for one moving away.
export function closestApproach(distanceM, bearingDeg, trackDeg, speedKnots) {
  if (!Number.isFinite(trackDeg) || !Number.isFinite(speedKnots) || speedKnots < 1 || !Number.isFinite(distanceM)) return { seconds: -1, meters: -1 };
  const px = distanceM * Math.sin(rad(bearingDeg)), py = distanceM * Math.cos(rad(bearingDeg));
  const speed = speedKnots * KNOT;
  const vx = speed * Math.sin(rad(trackDeg)), vy = speed * Math.cos(rad(trackDeg));
  let t = -(px * vx + py * vy) / (vx * vx + vy * vy);
  if (!(t > 0)) return { seconds: -1, meters: -1 };
  t = Math.min(t, 3600);
  return { seconds: Math.round(t), meters: Math.round(Math.hypot(px + vx * t, py + vy * t)) };
}

// --- Names ---------------------------------------------------------------------------------
// ICAO type designators people are likely to see, as a person would say them.
export const TYPES = Object.freeze({
  A306: "Airbus A300", A30B: "Airbus A300", A310: "Airbus A310", A318: "Airbus A318", A319: "Airbus A319", A320: "Airbus A320",
  A321: "Airbus A321", A19N: "Airbus A319neo", A20N: "Airbus A320neo", A21N: "Airbus A321neo", BCS1: "Airbus A220-100",
  BCS3: "Airbus A220-300", A332: "Airbus A330-200", A333: "Airbus A330-300", A338: "Airbus A330-800neo", A339: "Airbus A330-900neo",
  A342: "Airbus A340-200", A343: "Airbus A340-300", A346: "Airbus A340-600", A359: "Airbus A350-900", A35K: "Airbus A350-1000",
  A388: "Airbus A380", B712: "Boeing 717", B733: "Boeing 737-300", B734: "Boeing 737-400", B735: "Boeing 737-500",
  B736: "Boeing 737-600", B737: "Boeing 737-700", B738: "Boeing 737-800", B739: "Boeing 737-900", B37M: "Boeing 737 MAX 7",
  B38M: "Boeing 737 MAX 8", B39M: "Boeing 737 MAX 9", B3XM: "Boeing 737 MAX 10", B744: "Boeing 747-400", B748: "Boeing 747-8",
  B752: "Boeing 757-200", B753: "Boeing 757-300", B762: "Boeing 767-200", B763: "Boeing 767-300", B764: "Boeing 767-400",
  B772: "Boeing 777-200", B77L: "Boeing 777-200LR", B773: "Boeing 777-300", B77W: "Boeing 777-300ER", B778: "Boeing 777-8",
  B779: "Boeing 777-9", B788: "Boeing 787-8", B789: "Boeing 787-9", B78X: "Boeing 787-10", MD11: "McDonnell Douglas MD-11",
  MD82: "McDonnell Douglas MD-82", MD83: "McDonnell Douglas MD-83", MD88: "McDonnell Douglas MD-88", MD90: "McDonnell Douglas MD-90",
  DC10: "McDonnell Douglas DC-10", E135: "Embraer ERJ-135", E145: "Embraer ERJ-145", E170: "Embraer 170", E75L: "Embraer 175",
  E75S: "Embraer 175", E190: "Embraer 190", E195: "Embraer 195", E290: "Embraer E190-E2", E295: "Embraer E195-E2",
  E50P: "Embraer Phenom 100", E55P: "Embraer Phenom 300", E35L: "Embraer Legacy 600", E545: "Embraer Legacy 450",
  E550: "Embraer Praetor 600", E120: "Embraer Brasilia", CRJ1: "Bombardier CRJ100", CRJ2: "Bombardier CRJ200",
  CRJ7: "Bombardier CRJ700", CRJ9: "Bombardier CRJ900", CRJX: "Bombardier CRJ1000", CL30: "Challenger 300", CL35: "Challenger 350",
  CL60: "Challenger 600", GLEX: "Bombardier Global Express", GL5T: "Bombardier Global 5000", GL7T: "Bombardier Global 7500",
  DH8A: "Dash 8-100", DH8B: "Dash 8-200", DH8C: "Dash 8-300", DH8D: "Dash 8-400", AT43: "ATR 42", AT45: "ATR 42-500",
  AT46: "ATR 42-600", AT72: "ATR 72", AT75: "ATR 72-500", AT76: "ATR 72-600", SF34: "Saab 340", B190: "Beechcraft 1900",
  SW4: "Fairchild Metroliner", D328: "Dornier 328", J328: "Dornier 328JET", C150: "Cessna 150", C152: "Cessna 152",
  C162: "Cessna Skycatcher", C170: "Cessna 170", C172: "Cessna 172", C177: "Cessna Cardinal", C180: "Cessna 180",
  C182: "Cessna 182", C185: "Cessna 185", C195: "Cessna 195", C206: "Cessna 206", C207: "Cessna 207", C208: "Cessna Caravan",
  C210: "Cessna 210", C310: "Cessna 310", C340: "Cessna 340", C414: "Cessna 414", C421: "Cessna 421", C510: "Citation Mustang",
  C525: "CitationJet", C25A: "Citation CJ2", C25B: "Citation CJ3", C25C: "Citation CJ4", C550: "Citation II", C560: "Citation V",
  C56X: "Citation Excel", C650: "Citation III", C680: "Citation Sovereign", C68A: "Citation Latitude", C700: "Citation Longitude",
  C750: "Citation X", P28A: "Piper Cherokee", P28B: "Piper Cherokee", P28R: "Piper Arrow", P32R: "Piper Saratoga",
  PA32: "Piper Cherokee Six", PA34: "Piper Seneca", PA44: "Piper Seminole", PA46: "Piper Malibu", P46T: "Piper Meridian",
  PA31: "Piper Navajo", PA18: "Piper Super Cub", PA28: "Piper Cherokee", J3: "Piper Cub", BE20: "King Air 200",
  B350: "King Air 350", BE9L: "King Air 90", BE35: "Beechcraft Bonanza", BE36: "Beechcraft Bonanza", BE33: "Beechcraft Bonanza",
  BE55: "Beechcraft Baron", BE58: "Beechcraft Baron", BE40: "Beechjet 400", SR20: "Cirrus SR20", SR22: "Cirrus SR22",
  S22T: "Cirrus SR22T", SF50: "Cirrus Vision Jet", DA20: "Diamond DA20", DA40: "Diamond DA40", DA42: "Diamond DA42",
  DA62: "Diamond DA62", M20P: "Mooney M20", M20T: "Mooney M20", PC12: "Pilatus PC-12", PC24: "Pilatus PC-24", TBM7: "Daher TBM 700",
  TBM8: "Daher TBM 850", TBM9: "Daher TBM 900", GLF4: "Gulfstream IV", GLF5: "Gulfstream V", GLF6: "Gulfstream G650",
  G280: "Gulfstream G280", GA5C: "Gulfstream G500", GA6C: "Gulfstream G600", GA7C: "Gulfstream G700", LJ35: "Learjet 35",
  LJ45: "Learjet 45", LJ60: "Learjet 60", LJ75: "Learjet 75", F2TH: "Falcon 2000", F900: "Falcon 900", FA7X: "Falcon 7X",
  FA8X: "Falcon 8X", FA50: "Falcon 50", HDJT: "HondaJet", R22: "Robinson R22 helicopter", R44: "Robinson R44 helicopter",
  R66: "Robinson R66 helicopter", EC30: "Airbus H130 helicopter", EC35: "Airbus H135 helicopter", EC45: "Airbus H145 helicopter",
  EC55: "Airbus H155 helicopter", AS50: "Airbus AS350 helicopter", AS65: "Dauphin helicopter", B06: "Bell 206 helicopter",
  B407: "Bell 407 helicopter", B412: "Bell 412 helicopter", B429: "Bell 429 helicopter", B505: "Bell 505 helicopter",
  S76: "Sikorsky S-76 helicopter", S92: "Sikorsky S-92 helicopter", H60: "Black Hawk helicopter", H47: "Chinook helicopter",
  A109: "AW109 helicopter", A139: "AW139 helicopter", A169: "AW169 helicopter", MD52: "MD 520 helicopter", C17: "Boeing C-17",
  C130: "Lockheed C-130 Hercules", C30J: "Lockheed C-130J Hercules", K35R: "Boeing KC-135", KC46: "Boeing KC-46", T38: "Northrop T-38",
  F16: "F-16 Fighting Falcon", F18H: "F/A-18 Super Hornet", F18S: "F/A-18 Super Hornet", F35: "F-35 Lightning", BALL: "Hot air balloon",
  GLID: "Glider", ULAC: "Ultralight",
});

// "<manufacturer> <code>" when the table has no name: adsbdb sometimes shouts its makers.
export function typeName(code, manufacturer = "") {
  const key = String(code || "").trim().toUpperCase();
  if (TYPES[key]) return TYPES[key];
  let maker = String(manufacturer || "").trim();
  if (maker && maker === maker.toUpperCase()) maker = maker.toLowerCase().replace(/\b\p{L}/gu, (c) => c.toUpperCase());
  return [maker, key].filter(Boolean).join(" ");
}

const AIRLINE_CALLSIGN = /^([A-Z]{3})(\d{1,4}[A-Z]{0,2})$/u;
// The name a country or nation would leave alone, which would read wrong cut short.
const KEEP_WHOLE = new Set(["British", "China", "Japan", "Qatar", "Singapore", "Philippine", "Malaysia", "Kenya", "Thai", "Vietnam",
  "Gulf", "Korean", "Royal", "Air"]);
export function shortAirline(name) {
  const whole = String(name || "").replace(/\s+/gu, " ").trim();
  const cut = whole.replace(/(?:\s+(?:Airlines|Air Lines|Airways|Airline|Inc\.?))+$/iu, "").replace(/[,\s]+$/u, "").trim();
  return cut && !KEEP_WHOLE.has(cut) ? cut : whole;
}
// "United 1534", else the registration, else the callsign, else the hex address.
const coordinates = (airport) => {
  const lat = Number(airport?.latitude), lon = Number(airport?.longitude);
  return Number.isFinite(lat) && Number.isFinite(lon) && (lat || lon) ? { lat, lon } : null;
};

// Whether a route fits where the plane is. adsbdb answers by call sign, and an airline flies one
// number on several legs a day, so its route can be another leg: a plane climbing out of one city
// listed as flying between two others. A route that doesn't fit isn't shown. Fits: within 150 nm of
// the line between its ends and not over 100 nm past either; low and climbing, within 80 nm of
// where it left; low and coming down, within 120 nm of where it's going. Unknown ends fit.
export function routeFits(plane, route) {
  const a = route?.fromAt, b = route?.toAt;
  if (!a || !b || !Number.isFinite(plane?.lat) || !Number.isFinite(plane?.lon)) return true;
  const p = { lat: plane.lat, lon: plane.lon };
  const R = 3440.065, rad = Math.PI / 180;
  const d13 = distanceMeters(a.lat, a.lon, p.lat, p.lon) / 1852 / R;
  const t13 = bearingDegrees(a.lat, a.lon, p.lat, p.lon) * rad, t12 = bearingDegrees(a.lat, a.lon, b.lat, b.lon) * rad;
  const cross = Math.asin(Math.sin(d13) * Math.sin(t13 - t12));
  const along = Math.acos(Math.max(-1, Math.min(1, Math.cos(d13) / Math.cos(cross)))) * Math.sign(Math.cos(t13 - t12)) * R;
  const length = distanceMeters(a.lat, a.lon, b.lat, b.lon) / 1852;
  if (Math.abs(cross * R) > 150 || along < -100 || along > length + 100) return false;
  const low = plane.altFt < 15000;
  if (low && plane.rate > 500 && distanceMeters(a.lat, a.lon, p.lat, p.lon) / 1852 > 80) return false;
  if (low && plane.rate < -500 && distanceMeters(b.lat, b.lon, p.lat, p.lon) / 1852 > 120) return false;
  return true;
}

// A plane's radio guess for Skywatch (radio.mjs):
//   radio <hex> <frequency|-> <second|-> <likely|maybe|guess> <name pilots say> <who, short>
export function radioLine(hex, g) {
  if (!g) return "";
  const sure = ["likely", "maybe", "guess"].includes(g.sure) ? g.sure : "guess";
  return ["radio", hex, g.freq || "-", g.alt2 || "-", sure, field(g.say || "", 22) || "-", field(g.short || "", 22) || "-"].join("\t");
}

export function planeName({ callsign = "", airline = "", registration = "", hex = "" }) {
  const match = String(callsign).match(AIRLINE_CALLSIGN);
  if (match && airline) {
    const number = match[2].replace(/^0+(?=\d)/u, "");
    const room = 20 - number.length - 1;
    const short = field(shortAirline(airline), Math.max(1, room));
    if (short !== "-") return `${short} ${number}`;
  }
  return field(registration || callsign || String(hex).toUpperCase(), 20);
}

// --- Sun, shadow and the station's passes ----------------------------------------------------
// The Sun's direction, from the Astronomical Almanac's low-precision formulae (about 0.01 degree):
// a unit vector in the equatorial frame, its distance in km, and right ascension and declination.
export function sunPosition(date) {
  const n = date.getTime() / 86400000 + 2440587.5 - 2451545.0;
  const L = norm360(280.46 + 0.9856474 * n);
  const g = rad(norm360(357.528 + 0.9856003 * n));
  const lambda = rad(L + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g));
  const epsilon = rad(23.439 - 0.0000004 * n);
  const distanceKm = (1.00014 - 0.01671 * Math.cos(g) - 0.00014 * Math.cos(2 * g)) * AU_KM;
  const unit = { x: Math.cos(lambda), y: Math.cos(epsilon) * Math.sin(lambda), z: Math.sin(epsilon) * Math.sin(lambda) };
  return { unit, distanceKm, ra: Math.atan2(unit.y, unit.x), dec: Math.asin(unit.z) };
}
// The Sun's altitude in degrees for an observer (radians), given Greenwich sidereal time (radians).
export function sunAltitude(sun, latitude, longitude, gmst) {
  const hourAngle = gmst + longitude - sun.ra;
  return deg(Math.asin(Math.sin(latitude) * Math.sin(sun.dec) + Math.cos(latitude) * Math.cos(sun.dec) * Math.cos(hourAngle)));
}
// Whether a position (km, equatorial) is in sunlight, with the Earth's shadow taken as a cylinder.
export function sunlit(position, sunUnit) {
  const along = position.x * sunUnit.x + position.y * sunUnit.y + position.z * sunUnit.z;
  if (along > 0) return true;
  const px = position.x - along * sunUnit.x, py = position.y - along * sunUnit.y, pz = position.z - along * sunUnit.z;
  return Math.hypot(px, py, pz) > EARTH_KM;
}
// A satellite's brightness from its distance and how much of its lit side faces the observer.
export function magnitude(rangeKm, phaseRadians) {
  const lit = Math.max(1e-3, (1 + Math.cos(phaseRadians)) / 2);
  return Math.min(6, -1.8 - 15.75 + 2.5 * Math.log10((rangeKm * rangeKm) / lit));
}

// Everything about the station as seen from one place at one moment. Null when SGP4 can't say.
export function lookAt(satrec, observer, ms) {
  const date = new Date(ms);
  const pv = satellite.propagate(satrec, date);
  if (!pv || !pv.position || typeof pv.position === "boolean") return null;
  const gmst = satellite.gstime(date);
  const look = satellite.ecfToLookAngles(observer, satellite.eciToEcf(pv.position, gmst));
  const el = deg(look.elevation);
  const result = { ms, el, az: norm360(deg(look.azimuth)), rangeKm: look.rangeSat, position: pv.position, gmst };
  if (el < MIN_ELEVATION) return { ...result, visible: false };
  const sun = sunPosition(date);
  result.sunlit = sunlit(pv.position, sun.unit);
  result.sunAlt = sunAltitude(sun, observer.latitude, observer.longitude, gmst);
  result.sun = sun;
  result.visible = result.sunlit && result.sunAlt < TWILIGHT;
  return result;
}
function phaseAt(state, observer) {
  const here = satellite.ecfToEci(satellite.geodeticToEcf(observer), state.gmst);
  const p = state.position, s = state.sun;
  const toSun = { x: s.unit.x * s.distanceKm - p.x, y: s.unit.y * s.distanceKm - p.y, z: s.unit.z * s.distanceKm - p.z };
  const toUs = { x: here.x - p.x, y: here.y - p.y, z: here.z - p.z };
  const dot = toSun.x * toUs.x + toSun.y * toUs.y + toSun.z * toUs.z;
  return Math.acos(Math.max(-1, Math.min(1, dot / (Math.hypot(toSun.x, toSun.y, toSun.z) * Math.hypot(toUs.x, toUs.y, toUs.z)))));
}

// The visible passes from fromMs for `days`, with one already under way included. Each has its
// visible part's start, highest point and end (refined to about a second), brightness at the top,
// whether it ends by going into the Earth's shadow, and nine points along the way.
export function findPasses(satrec, { latitude, longitude, heightKm = 0 }, fromMs, { days = PASS_DAYS, max = MAX_PASSES, stepMs = 20000 } = {}) {
  const observer = { latitude: rad(latitude), longitude: rad(longitude), height: heightKm };
  const visible = (ms) => lookAt(satrec, observer, ms)?.visible === true;
  // Between a time that isn't visible and one that is (or the reverse), to within a second.
  const edge = (outside, inside) => {
    while (Math.abs(inside - outside) > 1000) {
      const mid = Math.round((outside + inside) / 2);
      if (visible(mid)) inside = mid; else outside = mid;
    }
    return inside;
  };
  const passes = [];
  const begin = fromMs - 20 * 60 * 1000, end = fromMs + days * 86400000;
  let previous = visible(begin), previousMs = begin, startMs = previous ? begin : 0;
  for (let ms = begin + stepMs; ms <= end && passes.length < max; ms += stepMs) {
    const now = visible(ms);
    if (now && !previous) startMs = edge(previousMs, ms);
    if (!now && previous && startMs) {
      const endMs = edge(ms, previousMs);
      // A pass of a few seconds low in the sky isn't worth going outside for.
      if (endMs >= fromMs && endMs - startMs >= MIN_VISIBLE_MS) {
        const pass = describePass(satrec, observer, startMs, endMs);
        if (pass.max.el >= MIN_PEAK) passes.push(pass);
      }
      startMs = 0;
    }
    previous = now;
    previousMs = ms;
  }
  return passes;
}
function describePass(satrec, observer, startMs, endMs) {
  // The highest visible point: a scan every 2 s, then to the second around it.
  let top = lookAt(satrec, observer, startMs);
  for (let ms = startMs; ms <= endMs; ms += 2000) {
    const state = lookAt(satrec, observer, ms);
    if (state?.visible && state.el > top.el) top = state;
  }
  for (let ms = Math.max(startMs, top.ms - 2000); ms <= Math.min(endMs, top.ms + 2000); ms += 1000) {
    const state = lookAt(satrec, observer, ms);
    if (state?.visible && state.el > top.el) top = state;
  }
  const first = lookAt(satrec, observer, startMs), last = lookAt(satrec, observer, endMs);
  // A pass that goes into the shadow while still climbing is highest at its end.
  for (const edgeState of [first, last]) if (edgeState?.visible && edgeState.el > top.el) top = edgeState;
  const after = lookAt(satrec, observer, endMs + 2000);
  const shadow = Boolean(after && after.el >= MIN_ELEVATION && after.sunAlt < TWILIGHT && after.sunlit === false);
  const path = [];
  for (let i = 0; i < 9; i++) {
    const state = lookAt(satrec, observer, Math.round(startMs + ((endMs - startMs) * i) / 8));
    path.push(`${int(state?.az) % 360}:${int(state?.el)}`);
  }
  const mag = top.sun ? magnitude(top.rangeKm, phaseAt(top, observer)) : 0;
  return { start: { ms: startMs, az: first.az, el: first.el }, max: { ms: top.ms, az: top.az, el: top.el },
    end: { ms: endMs, az: last.az, el: last.el }, magnitude: mag, shadow, path };
}
export function passLine(pass) {
  const s = (ms) => Math.floor(ms / 1000);
  const az = (value) => int(value) % 360;
  return ["pass", s(pass.start.ms), az(pass.start.az), int(pass.start.el), s(pass.max.ms), az(pass.max.az), int(pass.max.el),
    s(pass.end.ms), az(pass.end.az), int(pass.end.el), int(pass.magnitude * 10), pass.shadow ? 1 : 0, pass.path.join(",")].join("\t");
}

// Where the station is now: the point beneath it, how high and how fast.
export function stationNow(satrec, ms) {
  const date = new Date(ms);
  const pv = satellite.propagate(satrec, date);
  if (!pv || !pv.position || typeof pv.position === "boolean") return null;
  const geo = satellite.eciToGeodetic(pv.position, satellite.gstime(date));
  const v = pv.velocity;
  return { latitude: satellite.degreesLat(geo.latitude), longitude: satellite.degreesLong(geo.longitude), heightKm: geo.height,
    speedKmh: Math.hypot(v.x, v.y, v.z) * 3600 };
}

// --- Under the station: a country from Nominatim, or the sea it's over ------------------------
const SEAS = Object.freeze([
  ["the Gulf of Mexico", 18, 31, -98, -81], ["the Caribbean Sea", 9, 22, -88, -60], ["the Mediterranean Sea", 30, 46, -6, 36],
  ["the Black Sea", 40.5, 47, 27, 42], ["the Caspian Sea", 36.5, 47.5, 46, 55], ["the Red Sea", 12.5, 30, 32, 44],
  ["the Persian Gulf", 24, 30.5, 48, 56.5], ["the Arabian Sea", 5, 25, 50, 74], ["the Bay of Bengal", 5, 23, 80, 95],
  ["the South China Sea", 0, 23, 105, 121], ["the Sea of Japan", 34, 52, 127, 142], ["Hudson Bay", 51, 64, -95, -77],
  ["the Bering Sea", 52, 66, 162, 180], ["the Bering Sea", 52, 66, -180, -157], ["the Gulf of Alaska", 50, 61, -157, -133],
]);
export function oceanName(lat, lon) {
  if (lat <= -60) return "the Southern Ocean";
  if (lat >= 66) return "the Arctic Ocean";
  for (const [name, lat0, lat1, lon0, lon1] of SEAS) if (lat >= lat0 && lat <= lat1 && lon >= lon0 && lon <= lon1) return name;
  if (lat < 0) {
    if (lon >= -70 && lon < 20) return "the Atlantic Ocean";
    if (lon >= 20 && lon < (lat < -30 ? 147 : 130)) return "the Indian Ocean";
    return "the Pacific Ocean";
  }
  // North of the equator the Americas' west coast moves west going north.
  const westOfAmericas = lat < 9 ? -78 : lat < 15 ? -84 : lat < 30 ? -100 : lat < 50 ? -117 : -125;
  if (lon < westOfAmericas || lon >= 100) return "the Pacific Ocean";
  if (lon < 30) return "the Atlantic Ocean";
  if (lat < 30) return "the Indian Ocean";
  return "";
}
const WITH_THE = new Set(["United States", "United Kingdom", "Netherlands", "Philippines", "Bahamas", "Maldives", "Gambia",
  "Solomon Islands", "Marshall Islands", "Seychelles", "Comoros", "Czech Republic", "Dominican Republic", "Central African Republic",
  "United Arab Emirates", "Democratic Republic of the Congo", "Republic of the Congo", "Falkland Islands", "Faroe Islands"]);
const SHORTER = Object.freeze({ "Democratic Republic of the Congo": "DR Congo", "Central African Republic": "Central African Rep.",
  "United States of America": "United States", "Russian Federation": "Russia" });
export function countryName(name) {
  const plain = SHORTER[name] || name;
  return WITH_THE.has(plain) ? `the ${plain}` : plain;
}

// --- The service -----------------------------------------------------------------------------
function setCapped(map, key, value, cap) {
  if (!map.has(key) && map.size >= cap) map.delete(map.keys().next().value);
  map.set(key, value);
}
const sleep = (ms) => new Promise((resolve) => { const t = setTimeout(resolve, ms); t.unref?.(); });

export class PlanesService {
  constructor({ stateDir = "", weather = null, fetchImpl = fetch, now = Date.now, log = console } = {}) {
    Object.assign(this, { stateDir, weather, fetchImpl, now, log });
    this.lanPort = 0;
    this.tickets = new Map();
    this.nearCache = new Map();
    this.ground = new Map();
    this.routes = new Map();
    this.aircraft = new Map();
    this.lookups = new Map();
    this.passCache = new Map();
    this.overCache = new Map();
    this.lastNominatim = -Infinity;
    this.tle = null;          // { line1, line2, fetchedAt, satrec }
    this.tleTask = null;
    this.tleTriedAt = -Infinity;
    this.crew = { count: -1, until: 0 };
    this.crewTask = null;
  }

  async getJson(url, { timeoutMs = REQUEST_TIMEOUT_MS, accept = "application/json" } = {}) {
    const response = await this.fetchImpl(url, { headers: { "user-agent": USER_AGENT, accept },
      signal: AbortSignal.timeout(timeoutMs) });
    if (!response.ok) throw Object.assign(new Error(`http_${response.status}`), { status: response.status });
    return response.json();
  }

  // The position asked about: the GPS's (as "Here") or the saved city's, through the weather's geocoder.
  async position(params) {
    if (params.has("lat") || params.has("lon")) {
      const at = parseCoordinates(params.get("lat"), params.get("lon"));
      if (!at) throw new PlanesError("bad_position", 400);
      return { ...at, name: HERE };
    }
    const place = params.get("place");
    if (!validPlace(place)) throw new PlanesError("bad_position", 400);
    if (!this.weather) throw new PlanesError("planes_unavailable", 503);
    try {
      const found = await this.weather.place(comparable(place), place);
      return { latitude: found.latitude, longitude: found.longitude, name: found.name };
    } catch (error) {
      if (error?.code === "unknown_place") throw new PlanesError("unknown_place", 404);
      throw new PlanesError("planes_unavailable", 503);
    }
  }

  async answer(what, params, device, res) {
    if (what === "near") return lines(res, await this.near(params));
    if (what === "iss") return lines(res, await this.iss(params));
    if (what === "report") { this.report(params); return lines(res, "ok\n"); }
    throw new PlanesError("not_found", 404);
  }

  report(params) {
    const parts = [];
    let used = 0;
    for (const [key, value] of params) {
      if (!/^[A-Za-z0-9_.:-]{1,32}$/u.test(key) || !/^[A-Za-z0-9_.:-]{0,64}$/u.test(value) || PRIVATE_KEYS.has(key.toLowerCase())) continue;
      const part = `${key}=${value}`;
      if (used + part.length + 1 > 200) break;
      parts.push(part);
      used += part.length + 1;
    }
    this.log.info?.(`[planes] device report ${parts.join(" ")}`.trim());
  }

  // --- Planes ---
  async near(params) {
    const at = await this.position(params);
    // Where the Cardputer last looked from, for the page's Radio (radio.mjs). In memory only.
    this.lastAt = { latitude: at.latitude, longitude: at.longitude, atMs: this.now() };
    const asked = Number(params.get("r"));
    const radiusM = Number.isFinite(asked) && asked > 0 ? Math.min(MAX_RADIUS_M, Math.max(MIN_RADIUS_M, asked)) : DEFAULT_RADIUS_M;
    const nm = Math.min(100, Math.max(1, Math.ceil(radiusM / 1852)));
    const [list, groundM] = await Promise.all([this.aircraftNear(at, nm), this.groundHeight(at)]);
    const planes = [], raw = new Map();
    for (const ac of list) {
      const plane = this.planeFrom(ac, at, groundM);
      if (plane && plane.distanceM <= radiusM) { planes.push(plane); raw.set(plane.hex, ac); }
    }
    planes.sort((a, b) => a.distanceM - b.distanceM);
    const shown = planes.slice(0, MAX_PLANES);
    await this.lookUp(shown);
    const radio = this.radio ? await this.radio.guessFor(shown.map((plane) => raw.get(plane.hex))).catch(() => null) : null;
    return [`at\t${field(at.name, 24)}`, ...shown.map((plane) => this.planeLine(plane)),
      ...shown.map((plane) => radioLine(plane.hex, radio?.get(plane.hex))).filter(Boolean)].join("\n") + "\n";
  }

  async aircraftNear(at, nm) {
    const key = `${at.latitude.toFixed(2)},${at.longitude.toFixed(2)},${nm}`;
    const held = this.nearCache.get(key);
    if (held && this.now() - held.at < NEAR_FRESH_MS) return held.task;
    const entry = { at: this.now(), task: null };
    entry.task = this.getJson(`${URLS.planes}/${at.latitude.toFixed(2)}/${at.longitude.toFixed(2)}/${nm}`)
      .then((body) => (Array.isArray(body?.ac) ? body.ac : []))
      .catch((error) => {
        if (this.nearCache.get(key) === entry) this.nearCache.delete(key);
        this.log.warn?.(`[planes] planes failed: ${String(error?.status || error?.name || error?.message || error).slice(0, 60)}`);
        throw new PlanesError("planes_unavailable", 503);
      });
    setCapped(this.nearCache, key, entry, 32);
    return entry.task;
  }

  // The ground's height where the observer is, in meters; 0 until it's known.
  async groundHeight(at) {
    const key = `${at.latitude.toFixed(2)},${at.longitude.toFixed(2)}`;
    const held = this.ground.get(key);
    if (held && (held.known || this.now() < held.retryAt)) return held.meters;
    try {
      const body = await this.getJson(`${URLS.elevation}?latitude=${at.latitude.toFixed(2)}&longitude=${at.longitude.toFixed(2)}`, { timeoutMs: 3000 });
      const meters = Number(body?.elevation?.[0]);
      if (!Number.isFinite(meters)) throw new Error("no_elevation");
      setCapped(this.ground, key, { known: true, meters }, 64);
      return meters;
    } catch {
      setCapped(this.ground, key, { known: false, meters: 0, retryAt: this.now() + GROUND_RETRY_MS }, 64);
      return 0;
    }
  }

  planeFrom(ac, at, groundM) {
    const altFt = Number(ac?.alt_baro);
    if (!Number.isFinite(altFt)) return null;  // "ground", or nothing heard
    const hex = String(ac.hex || "").toLowerCase().replace(/^~/u, "");
    if (!/^[0-9a-f]{6}$/u.test(hex)) return null;
    let distanceM, bearing;
    const lat = Number(ac.lat), lon = Number(ac.lon);
    if (Number.isFinite(lat) && Number.isFinite(lon)) {
      distanceM = distanceMeters(at.latitude, at.longitude, lat, lon);
      bearing = bearingDegrees(at.latitude, at.longitude, lat, lon);
    } else if (Number.isFinite(Number(ac.dst)) && Number.isFinite(Number(ac.dir))) {
      distanceM = Number(ac.dst) * 1852;
      bearing = norm360(Number(ac.dir));
    } else return null;
    const track = Number(ac.track), gs = Number(ac.gs);
    const rate = Number.isFinite(Number(ac.baro_rate)) ? Number(ac.baro_rate) : Number(ac.geom_rate);
    const closest = closestApproach(distanceM, bearing, track, gs);
    const category = /^[A-D][0-7]$/u.test(String(ac.category || "")) ? ac.category : "-";
    return { hex, lat: Number.isFinite(lat) ? lat : null, lon: Number.isFinite(lon) ? lon : null,
      callsign: String(ac.flight || "").trim().toUpperCase(), registration: String(ac.r || "").trim(),
      typeCode: String(ac.t || "").trim().toUpperCase(), altFt, rate: Number.isFinite(rate) ? rate : 0, gs: Number.isFinite(gs) ? gs : 0,
      track: Number.isFinite(track) ? norm360(track) : -1, distanceM, bearing,
      // How high it looks goes by GPS height where there is one: the barometric altitude shown is off by
      // the day's air pressure, a few hundred feet, which puts a plane landing nearby below the ground.
      elevation: elevationDegrees((Number.isFinite(Number(ac.alt_geom)) ? Number(ac.alt_geom) : altFt) * FEET, groundM, distanceM),
      closest, category };
  }

  // Starts the route and type lookups these planes need, at most a few new ones, and waits a
  // moment for them; the rest arrive for the next ask.
  async lookUp(planes) {
    const waiting = [];
    let started = 0;
    const want = (key, start) => {
      const running = this.lookups.get(key);
      if (running) { waiting.push(running); return; }
      if (started >= MAX_NEW_LOOKUPS) return;
      started++;
      const task = start().finally(() => this.lookups.delete(key));
      this.lookups.set(key, task);
      waiting.push(task);
    };
    for (const plane of planes) {
      if (AIRLINE_CALLSIGN.test(plane.callsign) && !this.fresh(this.routes, plane.callsign))
        want(`route:${plane.callsign}`, () => this.fetchRoute(plane.callsign));
      if (!TYPES[plane.typeCode] && !this.fresh(this.aircraft, plane.hex))
        want(`aircraft:${plane.hex}`, () => this.fetchAircraft(plane.hex));
    }
    if (waiting.length) await Promise.race([Promise.allSettled(waiting), sleep(LOOKUP_BUDGET_MS)]);
  }
  fresh(map, key) {
    const held = map.get(key);
    return Boolean(held && this.now() < held.until);
  }
  async fetchRoute(callsign) {
    try {
      const body = await this.getJson(`${URLS.routes}/${encodeURIComponent(callsign)}`);
      const route = body?.response?.flightroute;
      if (!route || typeof route !== "object") throw Object.assign(new Error("unknown"), { status: 404 });
      setCapped(this.routes, callsign, { until: this.now() + ROUTE_MS, airline: String(route.airline?.name || ""),
        from: String(route.origin?.iata_code || ""), fromCity: String(route.origin?.municipality || ""),
        to: String(route.destination?.iata_code || ""), toCity: String(route.destination?.municipality || ""),
        fromAt: coordinates(route.origin), toAt: coordinates(route.destination) }, 2000);
    } catch (error) {
      setCapped(this.routes, callsign, { until: this.now() + (error?.status === 404 ? ROUTE_MISS_MS : LOOKUP_ERROR_MS) }, 2000);
    }
  }
  async fetchAircraft(hex) {
    try {
      const body = await this.getJson(`${URLS.aircraft}/${hex}`);
      const found = body?.response?.aircraft;
      if (!found || typeof found !== "object") throw Object.assign(new Error("unknown"), { status: 404 });
      setCapped(this.aircraft, hex, { until: this.now() + AIRCRAFT_MS, code: String(found.icao_type || ""),
        manufacturer: String(found.manufacturer || "") }, 2000);
    } catch (error) {
      setCapped(this.aircraft, hex, { until: this.now() + (error?.status === 404 ? AIRCRAFT_MS : LOOKUP_ERROR_MS) }, 2000);
    }
  }

  planeLine(plane) {
    const held = this.routes.get(plane.callsign) || {};
    const route = routeFits(plane, held) ? held : { airline: held.airline };
    const known = this.aircraft.get(plane.hex) || {};
    const code = plane.typeCode || known.code || "";
    const type = TYPES[code] || (known.code || known.manufacturer ? typeName(known.code || code, known.manufacturer) : code);
    return ["plane", plane.hex,
      planeName({ callsign: plane.callsign, airline: route.airline, registration: plane.registration, hex: plane.hex }),
      field(type, 24), field(route.from, 4), field(route.fromCity, 20), field(route.to, 4), field(route.toCity, 20),
      int(plane.altFt), int(plane.rate), int(plane.gs), plane.track < 0 ? -1 : int(plane.track) % 360,
      int(plane.distanceM), int(plane.bearing) % 360, int(plane.elevation), plane.closest.seconds, plane.closest.meters,
      plane.category].join("\t");
  }

  // --- The station ---
  async iss(params) {
    const at = await this.position(params);
    const satrec = await this.stationOrbit();
    const ms = this.now();
    const key = `${at.latitude.toFixed(2)},${at.longitude.toFixed(2)}`;
    let held = this.passCache.get(key);
    if (!held || ms - held.at >= PASS_FRESH_MS || held.satrec !== satrec) {
      const ground = this.ground.get(key);
      held = { at: ms, satrec, passes: findPasses(satrec, { latitude: at.latitude, longitude: at.longitude,
        heightKm: ground?.known ? ground.meters / 1000 : 0 }, ms) };
      setCapped(this.passCache, key, held, 16);
    }
    const now = stationNow(satrec, ms);
    if (!now) throw new PlanesError("planes_unavailable", 503);
    const [over, crew] = await Promise.all([this.over(now.latitude, now.longitude), this.crewCount()]);
    const out = [`at\t${field(at.name, 24)}`,
      ["now", Math.floor(ms / 1000), int(now.latitude * 100), int(now.longitude * 100), int(now.heightKm), int(now.speedKmh),
        field(over, 28), crew].join("\t")];
    for (const pass of held.passes) if (pass.end.ms >= ms) out.push(passLine(pass));
    return out.join("\n") + "\n";
  }

  tlePath() { return this.stateDir ? path.join(this.stateDir, "planes", "iss.tle") : ""; }

  // The station's orbit: fetched at most every 6 hours, kept on disk, and used up to 3 days old
  // when CelesTrak can't be reached.
  async stationOrbit() {
    const ms = this.now();
    if (!this.tle) await this.loadTle();
    if (this.tle && ms - this.tle.fetchedAt < TLE_FRESH_MS) return this.tle.satrec;
    // With an orbit in hand, CelesTrak is asked again every 10 minutes at most; with none, every minute.
    if (ms - this.tleTriedAt >= (this.tle ? TLE_RETRY_MS : 60 * 1000)) {
      this.tleTask ||= this.fetchTle().finally(() => { this.tleTask = null; });
      await this.tleTask;
    }
    if (this.tle && this.now() - this.tle.fetchedAt < TLE_STALE_MS) return this.tle.satrec;
    throw new PlanesError("planes_unavailable", 503);
  }
  async loadTle() {
    const file = this.tlePath();
    if (!file) return;
    try {
      const [text, info] = await Promise.all([readFile(file, "utf8"), stat(file)]);
      const parsed = parseTle(text);
      if (parsed) this.tle = { ...parsed, fetchedAt: info.mtimeMs };
    } catch { /* none kept yet */ }
  }
  async fetchTle() {
    this.tleTriedAt = this.now();
    try {
      const response = await this.fetchImpl(URLS.tle, { headers: { "user-agent": USER_AGENT, accept: "text/plain" },
        signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS) });
      if (!response.ok) throw new Error(`http_${response.status}`);
      const text = await response.text();
      const parsed = parseTle(text);
      if (!parsed) throw new Error("bad_tle");
      this.tle = { ...parsed, fetchedAt: this.now() };
      const file = this.tlePath();
      if (file) {
        await mkdir(path.dirname(file), { recursive: true });
        await writeFile(file, `ISS (ZARYA)\n${parsed.line1}\n${parsed.line2}\n`, "utf8");
      }
    } catch (error) {
      this.log.warn?.(`[planes] station orbit failed: ${String(error?.name || error?.message || error).slice(0, 60)}`);
    }
  }

  // What the station is over: a country, or the sea. "" when neither is known.
  async over(lat, lon) {
    const key = `${Math.floor(lat)},${Math.floor(lon)}`;
    const held = this.overCache.get(key);
    if (held && this.now() < held.until) return held.name;
    if (this.now() - this.lastNominatim < NOMINATIM_GAP_MS) return held?.name || "";
    this.lastNominatim = this.now();
    try {
      const query = new URLSearchParams({ lat: lat.toFixed(2), lon: lon.toFixed(2), zoom: "3", format: "jsonv2", "accept-language": "en" });
      const body = await this.getJson(`${URLS.reverse}?${query}`, { timeoutMs: NOMINATIM_TIMEOUT_MS });
      const country = String(body?.address?.country || body?.name || "").trim();
      const name = body?.error || !country ? oceanName(lat, lon) : countryName(country);
      setCapped(this.overCache, key, { name, until: this.now() + OVER_MS }, 512);
      return name;
    } catch {
      setCapped(this.overCache, key, { name: held?.name || "", until: this.now() + OVER_RETRY_MS }, 512);
      return held?.name || "";
    }
  }

  // How many people are on the station, or -1 when the list can't be read.
  async crewCount() {
    if (this.now() < this.crew.until) return this.crew.count;
    this.crewTask ||= this.getJson(URLS.crew)
      .then((body) => {
        const people = Array.isArray(body?.people) ? body.people : null;
        if (!people) throw new Error("no_people");
        this.crew = { count: people.filter((person) => person?.iss === true).length, until: this.now() + CREW_MS };
      })
      .catch(() => { this.crew = { count: this.crew.count, until: this.now() + CREW_RETRY_MS }; })
      .finally(() => { this.crewTask = null; });
    await this.crewTask;
    return this.crew.count;
  }

  // --- The home door ---
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
    return [`ticket\t${this.lanPort}\t/planes/${id}`, ...addresses.map((entry) => `lan\t${entry.address}\t${entry.prefix}`)].join("\n") + "\n";
  }
}

// A TLE's two lines for the station, checked, with SGP4's record made from them.
export function parseTle(text) {
  const rows = String(text || "").split(/\r?\n/u).map((row) => row.trim()).filter(Boolean);
  const line1 = rows.find((row) => /^1 25544U/u.test(row)), line2 = rows.find((row) => /^2 25544 /u.test(row));
  if (!line1 || !line2 || line1.length < 69 || line2.length < 69 || !checksumOk(line1) || !checksumOk(line2)) return null;
  const satrec = satellite.twoline2satrec(line1, line2);
  if (!satrec || satrec.error) return null;
  return { line1, line2, satrec };
}
function checksumOk(line) {
  let sum = 0;
  for (const ch of line.slice(0, 68)) sum += ch === "-" ? 1 : /\d/u.test(ch) ? Number(ch) : 0;
  return sum % 10 === Number(line[68]);
}

// --- Answers -----------------------------------------------------------------------------------
const WHAT = new Set(["near", "iss", "report"]);
// Asked every few seconds while the app is open: logged only when they fail.
const QUIET = new Set(["near", "iss"]);

function lines(res, body) {
  const data = Buffer.from(body, "utf8");
  res.writeHead(200, { "content-type": "text/plain; charset=utf-8", "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}
function traceRequest(via, what, res) {
  const started = Date.now();
  res.once("finish", () => {
    if (QUIET.has(what) && res.statusCode === 200) return;
    console.info(`[planes] device ${via} ${String(what).slice(0, 16)} ${res.statusCode} ${Date.now() - started}ms`);
  });
}
export function sendPlanesError(res, error, where = "") {
  if (res.headersSent || res.destroyed) { res.destroy(); return; }
  const status = error instanceof PlanesError ? error.status : 503;
  const code = error instanceof PlanesError ? error.code : "planes_unavailable";
  if (!(error instanceof PlanesError)) console.warn(`[planes] ${where} failed: ${String(error?.code || error?.message || error).slice(0, 120)}`);
  const data = Buffer.from(`${code}\n`, "utf8");
  res.writeHead(status, { "content-type": "text/plain; charset=utf-8", "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}

export async function handlePlanesRoute(service, pathname, params, device, res) {
  const what = pathname.slice("/api/cardputer/planes/".length);
  traceRequest("https", /^[a-z/]+$/u.test(what) ? what : "?", res);
  try {
    if (what === "ticket") { lines(res, service.ticketLines(device)); return; }
    if (!WHAT.has(what)) throw new PlanesError("not_found", 404);
    await service.answer(what, params, device, res);
  } catch (error) {
    sendPlanesError(res, error, what.slice(0, 16));
  }
}

// The home door's: /planes/<ticket>/<route>, from home addresses only. False when the path
// isn't the planes', so the next door can have it.
export function planesLanRoute(service, req, res) {
  let url;
  try { url = new URL(req.url || "/", "http://lan"); } catch { return false; }
  if (!url.pathname.startsWith("/planes/")) return false;
  const match = url.pathname.match(/^\/planes\/([0-9a-f]{32})\/([a-z]+)$/u);
  const remote = String(req.socket?.remoteAddress || "").replace(/^::ffff:/u, "");
  const held = req.method === "GET" && match && WHAT.has(match[2]) && (homeAddress(remote) || remote === "127.0.0.1")
    ? service.redeem(match[1]) : null;
  traceRequest("home", match && WHAT.has(match[2]) ? match[2] : "?", res);
  if (!held) {
    res.writeHead(404, { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" });
    res.end("not_found\n");
    return true;
  }
  void service.answer(match[2], url.searchParams, held.device, res).catch((error) => sendPlanesError(res, error, `home ${match[2]}`));
  return true;
}
