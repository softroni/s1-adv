import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { mkdtemp, readFile, rm, utimes, writeFile, mkdir } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { PlanesError, PlanesService, closestApproach, countryName, elevationDegrees, field, findPasses, handlePlanesRoute,
  oceanName, parseTle, planeName, planesLanRoute, shortAirline, sunAltitude, sunPosition, typeName, URLS } from "./planes.mjs";

// The station's orbit as CelesTrak gave it on 2026-11-06 (public data).
const TLE = `ISS (ZARYA)
1 25544U 98067A   26310.49826972  .00011492  00000+0  21472-3 0  9994
2 25544  51.6322 315.8635 0007225 355.0594   5.0322 15.49214228593815
`;
const T0 = Date.UTC(2026, 10, 6, 12, 0, 0);
// A public place, nowhere in particular: Chicago's O'Hare.
const LAT = 41.98, LON = -87.9;

function fakeRes() {
  const res = new EventEmitter();
  res.status = 0; res.headers = {}; res.body = ""; res.headersSent = false; res.destroyed = false;
  res.writeHead = (status, headers = {}) => { res.status = status; res.headers = headers; res.headersSent = true; return res; };
  res.end = (data) => { if (data) res.body += String(data); res.emit("finish"); };
  res.destroy = () => { res.destroyed = true; };
  return res;
}
const done = (res) => new Promise((resolve) => (res.status ? resolve() : res.once("finish", resolve)));
const quiet = { info() {}, warn() {} };
const json = (body, status = 200) => ({ ok: status >= 200 && status < 300, status, json: async () => body, text: async () => JSON.stringify(body) });
const text = (body, status = 200) => ({ ok: status >= 200 && status < 300, status, json: async () => JSON.parse(body), text: async () => body });

// A fake internet: answers by URL prefix and counts what was asked.
function fakeFetch(routes) {
  const calls = [];
  const fetchImpl = async (url, options = {}) => {
    calls.push(String(url));
    assert.equal(options.headers?.["user-agent"], "S1-ADV-Planes/1");
    for (const [prefix, answer] of routes) if (String(url).startsWith(prefix)) return typeof answer === "function" ? answer(String(url)) : answer;
    throw new Error(`unexpected ${url}`);
  };
  return { fetchImpl, calls };
}
const fields = (line) => line.split("\t");

test("text for the device has no tabs, no gaps, and fits", () => {
  assert.equal(field("São\tPaulo\n", 20), "Sao Paulo");
  assert.equal(field("Zürich–Kloten", 20), "Zurich-Kloten");
  assert.equal(field("Øresund ßtrasse", 40), "Oresund sstrasse");
  assert.equal(field("", 10), "-");
  assert.equal(field(null, 10), "-");
  assert.equal(field("  \u0007  ", 10), "-");
  assert.equal(field("abcdefghij-klm", 10), "abcdefghij");
  assert.equal(Buffer.byteLength(field("日本語の空港です", 10)), 9);  // Cut on a character.
});

test("names read as people say them", () => {
  assert.equal(shortAirline("American Airlines"), "American");
  assert.equal(shortAirline("Delta Air Lines"), "Delta");
  assert.equal(shortAirline("JetBlue Airways"), "JetBlue");
  assert.equal(shortAirline("Atlas Air Inc."), "Atlas Air");
  assert.equal(shortAirline("British Airways"), "British Airways");
  assert.equal(shortAirline("Japan Airlines"), "Japan Airlines");
  assert.equal(planeName({ callsign: "AAL0012", airline: "American Airlines", registration: "N1", hex: "abc123" }), "American 12");
  assert.equal(planeName({ callsign: "UAL1534", airline: "United Airlines" }), "United 1534");
  assert.equal(planeName({ callsign: "UAL1534", airline: "", registration: "N37577" }), "N37577");
  assert.equal(planeName({ callsign: "N174HA", airline: "", registration: "" }), "N174HA");
  assert.equal(planeName({ hex: "a1b2c3" }), "A1B2C3");
  assert.ok(planeName({ callsign: "XYZ1234", airline: "Very Long Regional Airline Company" }).length <= 20);
  assert.ok(planeName({ callsign: "XYZ1234", airline: "Very Long Regional Airline Company" }).endsWith(" 1234"));
  assert.equal(typeName("B39M"), "Boeing 737 MAX 9");
  assert.equal(typeName("ZZZ9", "PIPER"), "Piper ZZZ9");
  assert.equal(typeName("ZZZ9"), "ZZZ9");
});

test("closest approach: when and how far, or -1 for a plane going away", () => {
  // 10 km north, flying south at 300 knots: overhead in about 65 s.
  assert.deepEqual(closestApproach(10000, 0, 180, 300), { seconds: 65, meters: 0 });
  // 2 km to the east of that line: closest is 2 km.
  const offset = closestApproach(Math.hypot(10000, 2000), Math.atan2(2000, 10000) * 180 / Math.PI, 180, 300);
  assert.equal(offset.meters, 2000);
  assert.deepEqual(closestApproach(10000, 0, 0, 300), { seconds: -1, meters: -1 });     // Flying away.
  assert.deepEqual(closestApproach(10000, 0, NaN, 300), { seconds: -1, meters: -1 });   // No track.
  assert.deepEqual(closestApproach(10000, 0, 180, 0), { seconds: -1, meters: -1 });     // Hovering.
  assert.equal(closestApproach(10_000_000, 0, 180, 100).seconds, 3600);                 // An hour at most.
});

test("how high a plane looks, with the Earth curving away", () => {
  assert.equal(Math.round(elevationDegrees(3000, 0, 0)), 90);
  assert.equal(Math.round(elevationDegrees(1000, 0, 1000)), 45);
  assert.equal(Math.round(elevationDegrees(1000, 1000, 1000)) + 0, 0);
  assert.ok(elevationDegrees(10000, 0, 400000) < 0);  // Far enough, it's below the horizon.
});

test("seas and countries under the station", () => {
  assert.equal(oceanName(-30, -140), "the Pacific Ocean");
  assert.equal(oceanName(40, -40), "the Atlantic Ocean");
  assert.equal(oceanName(-20, 70), "the Indian Ocean");
  assert.equal(oceanName(-70, 0), "the Southern Ocean");
  assert.equal(oceanName(80, 0), "the Arctic Ocean");
  assert.equal(oceanName(35, 18), "the Mediterranean Sea");
  assert.equal(oceanName(25, -90), "the Gulf of Mexico");
  assert.equal(oceanName(10, -100), "the Pacific Ocean");
  assert.equal(oceanName(15, -70), "the Caribbean Sea");
  assert.equal(oceanName(15, 88), "the Bay of Bengal");
  assert.equal(oceanName(-35, -20), "the Atlantic Ocean");
  assert.equal(oceanName(-40, 160), "the Pacific Ocean");
  assert.equal(countryName("United States"), "the United States");
  assert.equal(countryName("France"), "France");
  assert.equal(countryName("Democratic Republic of the Congo"), "DR Congo");
});

test("the Sun's position and altitude", () => {
  assert.ok(Math.abs(sunPosition(new Date(Date.UTC(2026, 5, 21, 12))).dec * 180 / Math.PI - 23.44) < 0.05);
  assert.ok(Math.abs(sunPosition(new Date(Date.UTC(2026, 2, 20, 15))).dec * 180 / Math.PI) < 0.2);
  // Noon on the Greenwich meridian at the June solstice, from the equator: about 66.6 degrees up.
  const date = new Date(Date.UTC(2026, 5, 21, 12, 2));
  const gmst = ((280.46061837 + 360.98564736629 * (date.getTime() / 86400000 + 2440587.5 - 2451545)) % 360) * Math.PI / 180;
  assert.ok(Math.abs(sunAltitude(sunPosition(date), 0, 0, gmst) - 66.6) < 0.5);
});

test("a TLE is only taken whole and with good checksums", () => {
  assert.ok(parseTle(TLE));
  assert.equal(parseTle(TLE.replace("15.49214228593815", "15.49214228593816")), null);
  assert.equal(parseTle("No GP data found"), null);
});

test("the station's visible passes match an independent prediction", () => {
  const { satrec } = parseTle(TLE);
  const passes = findPasses(satrec, { latitude: LAT, longitude: LON }, T0);
  assert.ok(passes.length >= 3 && passes.length <= 8);
  for (const [i, pass] of passes.entries()) {
    assert.ok(pass.start.ms <= pass.max.ms && pass.max.ms <= pass.end.ms);
    assert.ok(pass.end.ms - pass.start.ms < 12 * 60 * 1000);
    assert.ok(pass.start.el >= 9.5 && pass.max.el >= pass.start.el && pass.max.el >= pass.end.el);
    assert.equal(pass.path.length, 9);
    assert.ok(pass.magnitude < 3);
    if (i) assert.ok(pass.start.ms > passes[i - 1].end.ms);
  }
  // skyfield (with JPL's DE421 for the Sun) puts the highest point of the 13 November pass at
  // 23:46:42 UTC, 76.7 degrees up at azimuth 143.6.
  const high = passes.find((pass) => Math.abs(pass.max.ms - Date.UTC(2026, 10, 13, 23, 46, 42)) < 5000);
  assert.ok(high, "the 13 November pass");
  assert.ok(Math.abs(high.max.el - 76.7) < 1 && Math.abs(high.max.az - 143.6) < 2);
  // A pass that ends while it's still high has gone into the Earth's shadow.
  assert.ok(passes.some((pass) => pass.shadow && pass.end.el > 20));
});

function planesFetch() {
  const plane = (o) => ({ alt_baro: 30000, gs: 450, track: 180, baro_rate: 0, category: "A3", ...o });
  const ac = [
    plane({ hex: "a00001", flight: "UAL1534 ", r: "N37577", t: "B39M", lat: LAT + 0.1, lon: LON }),      // ~11 km north
    plane({ hex: "a00002", flight: "N174HA  ", r: "N174HA", t: "ZZZ9", lat: LAT, lon: LON + 0.05, alt_baro: 1500, gs: 90, track: 90, category: "A1" }),
    plane({ hex: "a00003", flight: "SWA2210 ", r: "N1", t: "B38M", alt_baro: "ground", lat: LAT, lon: LON }),  // On the ground.
    plane({ hex: "a00004", flight: "DAL9", r: "N2", t: "A321", lat: LAT + 1.5, lon: LON }),                    // Beyond the radius.
    plane({ hex: "zzz", flight: "BAD1", lat: LAT, lon: LON }),                                                   // No usable address.
    { hex: "~a00005", alt_baro: 5000, alt_geom: 400, dst: 2.5, dir: 270 },                                          // Little known about it.
  ];
  return fakeFetch([
    [URLS.planes, json({ ac, total: ac.length })],
    [URLS.elevation, json({ elevation: [200] })],
    [`${URLS.routes}/UAL1534`, json({ response: { flightroute: { airline: { name: "United Airlines" },
      origin: { iata_code: "DEN", municipality: "Denver" }, destination: { iata_code: "LAX", municipality: "Los Angeles" } } } })],
    [URLS.routes, json({ response: "unknown callsign" }, 404)],
    [`${URLS.aircraft}/a00002`, json({ response: { aircraft: { icao_type: "ZZZ9", manufacturer: "CESSNA" } } })],
    [URLS.aircraft, json({ response: "unknown aircraft" }, 404)],
  ]);
}

test("near: planes in the air, nearest first, with routes and types", async () => {
  const { fetchImpl, calls } = planesFetch();
  const service = new PlanesService({ fetchImpl, now: () => T0, log: quiet });
  const body = await service.near(new URLSearchParams({ lat: String(LAT), lon: String(LON), r: "40000" }));
  assert.ok(body.endsWith("\n"));
  const rows = body.trimEnd().split("\n");
  assert.equal(rows[0], "at\tHere");
  assert.equal(rows.length, 4);
  for (const row of rows.slice(1)) {
    assert.equal(fields(row).length, 18);
    assert.ok(!row.includes("\t\t") && !row.endsWith("\t"));
  }
  const [gaPlane, littlePlane, united] = rows.slice(1).map(fields);
  assert.deepEqual(gaPlane.slice(0, 8), ["plane", "a00002", "N174HA", "Cessna ZZZ9", "-", "-", "-", "-"]);
  assert.equal(gaPlane[8], "1500");
  assert.equal(gaPlane[13], "90");                      // Due east.
  assert.equal(gaPlane[17], "A1");
  assert.deepEqual(littlePlane.slice(0, 4), ["plane", "a00005", "A00005", "-"]);
  assert.equal(littlePlane[13], "270");
  assert.equal(littlePlane[8], "5000");                 // The altitude shown is the barometric one,
  assert.equal(littlePlane[14], "-1");                  // but how high it looks goes by its GPS height.
  assert.equal(littlePlane[11], "-1");                  // No track: no closest approach either.
  assert.deepEqual(littlePlane.slice(15, 18), ["-1", "-1", "-"]);
  assert.deepEqual(united.slice(0, 8), ["plane", "a00001", "United 1534", "Boeing 737 MAX 9", "DEN", "Denver", "LAX", "Los Angeles"]);
  const [alt, rate, gs, track, distance, bearing, elevation, seconds, closest] = united.slice(8, 17).map(Number);
  assert.deepEqual([alt, rate, gs, track], [30000, 0, 450, 180]);
  assert.ok(Math.abs(distance - 11119) < 20 && bearing === 0);
  assert.ok(elevation > 35 && elevation < 45);
  assert.ok(seconds > 40 && seconds < 52 && closest < 50);  // Coming straight at us.
  assert.equal(calls.filter((url) => url.startsWith(URLS.planes)).length, 1);
  assert.ok(calls.some((url) => url === `${URLS.planes}/41.98/-87.90/22`));
  // Asked again within 8 seconds: the same list, with nothing looked up twice.
  await service.near(new URLSearchParams({ lat: String(LAT), lon: String(LON), r: "40000" }));
  assert.equal(calls.filter((url) => url.startsWith(URLS.planes)).length, 1);
  assert.equal(calls.filter((url) => url.startsWith(URLS.routes)).length, 1);
  assert.equal(calls.filter((url) => url.startsWith(URLS.elevation)).length, 1);
});

test("near: at most eight new lookups at a time, the rest on the next ask", async () => {
  const ac = Array.from({ length: 12 }, (_, i) => ({ hex: `b000${String(i).padStart(2, "0")}`, flight: `AAL${100 + i}`,
    t: "B738", alt_baro: 20000, gs: 400, track: 90, lat: LAT + 0.01 * (i + 1), lon: LON }));
  let now = T0;
  const { fetchImpl, calls } = fakeFetch([
    [URLS.planes, json({ ac })],
    [URLS.elevation, json({ elevation: [0] })],
    [URLS.routes, json({ response: { flightroute: { airline: { name: "American Airlines" }, origin: {}, destination: {} } } })],
  ]);
  const service = new PlanesService({ fetchImpl, now: () => now, log: quiet });
  const first = await service.near(new URLSearchParams({ lat: String(LAT), lon: String(LON) }));
  assert.equal(calls.filter((url) => url.startsWith(URLS.routes)).length, 8);
  assert.equal(first.split("\n").filter((row) => row.includes("\tAmerican ")).length, 8);
  now += 9000;
  const second = await service.near(new URLSearchParams({ lat: String(LAT), lon: String(LON) }));
  assert.equal(calls.filter((url) => url.startsWith(URLS.routes)).length, 12);
  assert.equal(second.split("\n").filter((row) => row.includes("\tAmerican ")).length, 12);
});

test("near: a city by name, and the positions that are refused", async () => {
  const { fetchImpl } = planesFetch();
  const asked = [];
  const weather = { place: async (key, text) => {
    asked.push([key, text]);
    if (text === "Nowhere") throw Object.assign(new Error("unknown_place"), { code: "unknown_place" });
    return { name: "Salem", latitude: LAT, longitude: LON };
  } };
  const service = new PlanesService({ fetchImpl, weather, now: () => T0, log: quiet });
  const body = await service.near(new URLSearchParams({ place: "Salem, MA" }));
  assert.equal(body.split("\n")[0], "at\tSalem");
  assert.deepEqual(asked[0], ["salemma", "Salem, MA"]);
  await assert.rejects(service.near(new URLSearchParams({ place: "Nowhere" })), (e) => e instanceof PlanesError && e.status === 404 && e.code === "unknown_place");
  await assert.rejects(service.near(new URLSearchParams({ place: "<script>" })), (e) => e.status === 400 && e.code === "bad_position");
  await assert.rejects(service.near(new URLSearchParams({ lat: "91", lon: "0" })), (e) => e.status === 400);
  await assert.rejects(service.near(new URLSearchParams({ lat: "1" })), (e) => e.status === 400);
  await assert.rejects(service.near(new URLSearchParams()), (e) => e.status === 400);
});

test("near: a planes service that's down is a 503", async () => {
  const { fetchImpl } = fakeFetch([[URLS.planes, json({}, 502)], [URLS.elevation, json({ elevation: [0] })]]);
  const service = new PlanesService({ fetchImpl, now: () => T0, log: quiet });
  await assert.rejects(service.near(new URLSearchParams({ lat: "1", lon: "2" })), (e) => e.status === 503 && e.code === "planes_unavailable");
});

function issFetch(over = { error: "Unable to geocode" }) {
  return fakeFetch([
    [URLS.tle, text(TLE)],
    [URLS.reverse, json(over)],
    [URLS.crew, json({ number: 10, people: [{ iss: true }, { iss: true }, { iss: false }, { iss: true }] })],
  ]);
}

test("iss: where it is now, and the passes to come", async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), "planes-test-"));
  try {
    const { fetchImpl, calls } = issFetch();
    const service = new PlanesService({ fetchImpl, stateDir: dir, now: () => T0, log: quiet });
    const body = await service.iss(new URLSearchParams({ lat: String(LAT), lon: String(LON) }));
    const rows = body.trimEnd().split("\n");
    assert.equal(rows[0], "at\tHere");
    const now = fields(rows[1]);
    assert.equal(now.length, 8);
    assert.equal(now[0], "now");
    assert.equal(Number(now[1]), T0 / 1000);
    assert.ok(Math.abs(Number(now[2])) <= 5200 && Math.abs(Number(now[3])) <= 18000);
    assert.ok(Number(now[4]) > 380 && Number(now[4]) < 450);
    assert.ok(Number(now[5]) > 26000 && Number(now[5]) < 29000);
    assert.ok(now[6].startsWith("the ") && now[6].includes(" "));  // Over water, from the classifier.
    assert.equal(now[7], "3");
    const passes = rows.slice(2).map(fields);
    assert.ok(passes.length >= 3 && passes.length <= 8);
    for (const pass of passes) {
      assert.equal(pass.length, 13);
      assert.equal(pass[0], "pass");
      assert.ok(Number(pass[1]) <= Number(pass[4]) && Number(pass[4]) <= Number(pass[7]));
      assert.match(pass[11], /^[01]$/u);
      const path = pass[12].split(",");
      assert.equal(path.length, 9);
      for (const point of path) assert.match(point, /^\d{1,3}:-?\d{1,2}$/u);
    }
    for (let i = 1; i < passes.length; i++) assert.ok(Number(passes[i][1]) > Number(passes[i - 1][1]));
    // The orbit is kept on disk, for a bridge that restarts while CelesTrak is down.
    assert.match(await readFile(path.join(dir, "planes", "iss.tle"), "utf8"), /^ISS \(ZARYA\)\n1 25544U/u);
    // Asked again: no new orbit, no second country lookup for the same place.
    await service.iss(new URLSearchParams({ lat: String(LAT), lon: String(LON) }));
    assert.equal(calls.filter((url) => url.startsWith(URLS.tle)).length, 1);
    assert.equal(calls.filter((url) => url.startsWith(URLS.reverse)).length, 1);
    assert.equal(calls.filter((url) => url.startsWith(URLS.crew)).length, 1);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("iss: a country by name, a pass under way, and the orbit from disk when CelesTrak is down", async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), "planes-test-"));
  try {
    await mkdir(path.join(dir, "planes"), { recursive: true });
    const file = path.join(dir, "planes", "iss.tle");
    await writeFile(file, TLE);
    const { satrec } = parseTle(TLE);
    const pass = findPasses(satrec, { latitude: LAT, longitude: LON }, T0)[1];
    const during = Math.round((pass.start.ms + pass.end.ms) / 2);
    const keptAt = new Date(during - 24 * 60 * 60 * 1000);  // A day old: past its 6 hours, but usable.
    await utimes(file, keptAt, keptAt);
    const { fetchImpl, calls } = fakeFetch([
      [URLS.tle, text("", 503)],
      [URLS.reverse, json({ name: "United States", address: { country: "United States" } })],
      [URLS.crew, json({}, 500)],
    ]);
    const service = new PlanesService({ fetchImpl, stateDir: dir, now: () => during, log: quiet });
    const rows = (await service.iss(new URLSearchParams({ lat: String(LAT), lon: String(LON) }))).trimEnd().split("\n");
    assert.equal(fields(rows[1])[6], "the United States");
    assert.equal(fields(rows[1])[7], "-1");
    assert.ok(Math.abs(Number(fields(rows[2])[1]) - pass.start.ms / 1000) <= 2);  // The one under way comes first.
    assert.equal(calls.filter((url) => url.startsWith(URLS.tle)).length, 1);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("iss: no orbit at all, or one too old, is a 503", async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), "planes-test-"));
  try {
    const { fetchImpl } = fakeFetch([[URLS.tle, text("", 503)]]);
    const service = new PlanesService({ fetchImpl, stateDir: dir, now: () => T0, log: quiet });
    await assert.rejects(service.iss(new URLSearchParams({ lat: "1", lon: "2" })), (e) => e.status === 503);
    await mkdir(path.join(dir, "planes"), { recursive: true });
    const file = path.join(dir, "planes", "iss.tle");
    await writeFile(file, TLE);
    const old = new Date(T0 - 4 * 24 * 60 * 60 * 1000);
    await utimes(file, old, old);
    const later = new PlanesService({ fetchImpl, stateDir: dir, now: () => T0, log: quiet });
    await assert.rejects(later.iss(new URLSearchParams({ lat: "1", lon: "2" })), (e) => e.status === 503);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("routes: tickets, unknown routes and the device's reports", async () => {
  const logged = [];
  const service = new PlanesService({ fetchImpl: async () => { throw new Error("offline"); }, now: () => T0,
    log: { info: (line) => logged.push(line), warn() {} } });
  assert.equal(service.ticketLines("dev1"), "none\n");
  service.lanPort = 31890;
  const interfaces = { en0: [{ family: "IPv4", address: "192.168.0.20", cidr: "192.168.0.20/22", internal: false }] };
  const lines = service.ticketLines("dev1", interfaces).trimEnd().split("\n");
  assert.match(lines[0], /^ticket\t31890\t\/planes\/[0-9a-f]{32}$/u);
  assert.equal(lines[1], "lan\t192.168.0.20\t22");
  assert.equal(service.ticketLines("dev1", interfaces).split("\n")[0], lines[0]);  // One per device.

  let res = fakeRes();
  await handlePlanesRoute(service, "/api/cardputer/planes/nope", new URLSearchParams(), "dev1", res);
  assert.deepEqual([res.status, res.body], [404, "not_found\n"]);
  res = fakeRes();
  await handlePlanesRoute(service, "/api/cardputer/planes/report",
    new URLSearchParams("stage=near&e=3&free=81234&note=hello world&lat=41.98&fw=0.2.323-dev"), "dev1", res);
  assert.deepEqual([res.status, res.body, res.headers["cache-control"]], [200, "ok\n", "no-store"]);
  assert.equal(logged.at(-1), "[planes] device report stage=near e=3 free=81234 fw=0.2.323-dev");
  res = fakeRes();
  await handlePlanesRoute(service, "/api/cardputer/planes/near", new URLSearchParams("lat=x&lon=y"), "dev1", res);
  assert.deepEqual([res.status, res.body], [400, "bad_position\n"]);
});

test("the home door: tickets from home addresses only", async () => {
  const service = new PlanesService({ fetchImpl: async () => { throw new Error("offline"); }, now: () => T0, log: quiet });
  service.lanPort = 31890;
  const id = service.ticket("dev1");
  const ask = (url, remoteAddress, method = "GET") => {
    const res = fakeRes();
    const handled = planesLanRoute(service, { url, method, socket: { remoteAddress } }, res);
    return { handled, res };
  };
  assert.equal(ask("/podcasts/abc/audio", "192.168.0.30").handled, false);
  let { handled, res } = ask(`/planes/${id}/report?stage=x`, "203.0.113.9");
  assert.ok(handled);
  assert.deepEqual([res.status, res.body], [404, "not_found\n"]);
  ({ res } = ask(`/planes/${"0".repeat(32)}/report`, "192.168.0.30"));
  assert.equal(res.status, 404);
  ({ res } = ask(`/planes/${id}/ticket`, "192.168.0.30"));
  assert.equal(res.status, 404);
  ({ res } = ask(`/planes/${id}/report`, "192.168.0.30", "POST"));
  assert.equal(res.status, 404);
  ({ res } = ask(`/planes/${id}/report?stage=x`, "::ffff:192.168.0.30"));
  await done(res);
  assert.deepEqual([res.status, res.body], [200, "ok\n"]);
  ({ res } = ask(`/planes/${id}/near?lat=1&lon=2`, "127.0.0.1"));
  await done(res);
  assert.deepEqual([res.status, res.body], [503, "planes_unavailable\n"]);
});
