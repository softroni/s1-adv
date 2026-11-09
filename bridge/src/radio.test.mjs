import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, readdir, writeFile, mkdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { deflateRawSync } from "node:zlib";
import { RadioService, guess, centerNear, unzipOne, parseCsv, readFrequencies, faaCycle, quad, compass } from "./radio.mjs";

// A made-up area: a hub at 40N 100W with a second airport 15 nm east of it. No real place.
const HUB = { code: "HUB", name: "Hub", lat: 40, lon: -100,
  departures: { N: "121.1", E: "121.2", S: "121.3", W: "121.4" },
  arrivalSplit: "EW", arrivals: { E: ["122.1", "122.2"], W: ["122.3", "122.4"] },
  finals: { E: "123.1", W: "123.2" }, towerSplit: "EW", towers: { E: ["124.1", "124.2"], W: ["124.6"] } };
const CITY = { code: "CTY", name: "City", lat: 40, lon: -99.67, departures: { E: "121.2", S: "121.3" },
  arrivalSplit: "NS", arrivals: { N: ["125.1", "125.2"], S: ["125.2", "125.1"] }, towers: { "*": ["126.1"] }, airlines: ["SWX"] };
const AREA = { approachName: "Test Approach", airports: [HUB, CITY],
  local: { split: "NS", lat: 40, N: ["127.1", "127.2"], S: ["127.2", "127.1"] },
  catalog: [["Departures", [["121.4", "West"], ["121.2", "East"], ["121.1", "North"]]]] };
const TOWERS = [{ code: "HUB", lat: 40, lon: -100, freq: "124.1", note: "EAST" }, { code: "CTY", lat: 40, lon: -99.67, freq: "126.1", note: "" },
  { code: "SML", lat: 40.1, lon: -100.3, freq: "128.5", note: "" }];
const SITES = [{ center: "ZKC", name: "Near Site", lat: 40.2, lon: -100.1, freq: "132.1", band: "HIGH" },
  { center: "ZKC", name: "Far Site", lat: 41, lon: -101, freq: "133.1", band: "HIGH" },
  { center: "ZKC", name: "Low Site", lat: 40.1, lon: -100, freq: "134.1", band: "LOW" },
  { center: "ZKC", name: "Other Low", lat: 40.6, lon: -100, freq: "135.1", band: "LOW" }];
const KNOWN = { towers: TOWERS, sites: SITES, area: AREA };
// A point d nm from (lat, lon) toward bearing b.
function at(lat, lon, d, b) {
  const r = d / 3440.065, la = lat * Math.PI / 180, lo = lon * Math.PI / 180, br = b * Math.PI / 180;
  const la2 = Math.asin(Math.sin(la) * Math.cos(r) + Math.cos(la) * Math.sin(r) * Math.cos(br));
  const lo2 = lo + Math.atan2(Math.sin(br) * Math.sin(r) * Math.cos(la), Math.cos(r) - Math.sin(la) * Math.sin(la2));
  return { lat: la2 * 180 / Math.PI, lon: lo2 * 180 / Math.PI };
}
const plane = (fields) => ({ squawk: "4321", flight: "ABC123", track: 90, baro_rate: 0, ...fields });

test("helpers", () => {
  assert.equal(quad(10), "N"); assert.equal(quad(100), "E"); assert.equal(quad(200), "S"); assert.equal(quad(300), "W");
  assert.equal(compass(350), "N"); assert.equal(compass(46), "NE");
  assert.equal(faaCycle(Date.UTC(2026, 9, 29)), Date.UTC(2026, 9, 29));
  assert.equal(faaCycle(Date.UTC(2026, 10, 25)), Date.UTC(2026, 9, 29));
  assert.equal(faaCycle(Date.UTC(2026, 10, 26)), Date.UTC(2026, 10, 26));
  assert.deepEqual(parseCsv('a,"b,c","d ""e"""\r\n1,2,3\n'), [["a", "b,c", 'd "e"'], ["1", "2", "3"]]);
});

test("the FAA file: towers' local frequencies and Center sites, air band only, never 121.5", () => {
  const head = '"FACILITY","FACILITY_TYPE","ARTCC_OR_FSS_ID","SERVICED_FACILITY","LAT_DECIMAL","LONG_DECIMAL","FREQ","SECTORIZATION","FREQ_USE"';
  const csv = [head,
    '"HUB","ATCT-TRACON","ZKC","HUB",40,-100,"124.1","EAST","LCL/P"',
    '"HUB","ATCT-TRACON","ZKC","HUB",40,-100,"121.9","","GND/P"',
    '"HUB","ATCT-TRACON","ZKC","HUB",40,-100,"257.8","","LCL/P"',
    '"SITE ONE","RCAG","ZKC","",40.2,-100.1,"132.1","HIGH","SITE ONE RCAG"',
    '"SITE ONE","RCAG","ZKC","",40.2,-100.1,"121.5","HIGH","EMERG"',
    '"SITE ONE","RCAG","ZKC","",40.2,-100.1,"317.4","HIGH","SITE ONE RCAG"', ""].join("\n");
  const { towers, sites } = readFrequencies(csv);
  assert.deepEqual(towers, [{ code: "HUB", lat: 40, lon: -100, freq: "124.1", note: "EAST" }]);
  assert.deepEqual(sites, [{ center: "ZKC", name: "Site One", lat: 40.2, lon: -100.1, freq: "132.1", band: "HIGH" }]);
  assert.throws(() => readFrequencies("A,B\n1,2\n"), /lacks/);
});

// A zip with one deflated file, as the FAA's are.
function zipOf(name, text) {
  const data = deflateRawSync(Buffer.from(text)), nameBuf = Buffer.from(name);
  const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(8, 8);
  local.writeUInt32LE(data.length, 18); local.writeUInt32LE(text.length, 22); local.writeUInt16LE(nameBuf.length, 26);
  const central = Buffer.alloc(46); central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(8, 10);
  central.writeUInt32LE(data.length, 20); central.writeUInt32LE(text.length, 24); central.writeUInt16LE(nameBuf.length, 28);
  central.writeUInt32LE(0, 42);
  const cdStart = 30 + nameBuf.length + data.length, cdLen = 46 + nameBuf.length;
  const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(cdLen, 12); end.writeUInt32LE(cdStart, 16);
  return Buffer.concat([local, nameBuf, data, central, nameBuf, end]);
}
test("unzips the one file it wants", () => {
  const zip = zipOf("FRQ.csv", "hello,world\n");
  assert.equal(unzipOne(zip, "FRQ.csv").toString(), "hello,world\n");
  assert.throws(() => unzipOne(zip, "APT.csv"), /not in zip/);
  assert.throws(() => unzipOne(Buffer.from("nope"), "FRQ.csv"), /not a zip/);
});

test("guess: towers", () => {
  // An airliner low on the hub's east side: the hub's east tower, with its second frequency.
  let g = guess(plane({ ...at(40, -100, 4, 90), alt_baro: 1500, baro_rate: -700, track: 270 }), KNOWN);
  assert.deepEqual([g.freq, g.alt2, g.who, g.sure], ["124.1", "124.2", "Hub Tower (east)", "likely"]);
  // A small plane beside the small airport: that tower. An airliner there isn't given it.
  const bySmall = at(40.1, -100.3, 1, 0);
  assert.equal(guess(plane({ ...bySmall, alt_baro: 1500, flight: "N123AB" }), KNOWN).freq, "128.5");
  assert.notEqual(guess(plane({ ...bySmall, alt_baro: 1500 }), KNOWN).freq, "128.5");
  // The second airport's tower has one frequency for every side.
  assert.equal(guess(plane({ ...at(40, -99.67, 3, 0), alt_baro: 1800, baro_rate: -600, track: 180 }), KNOWN).freq, "126.1");
});

test("guess: no controller, and cruising", () => {
  const g = guess(plane({ ...at(40, -100, 20, 0), alt_baro: 3500, squawk: "1200", flight: "N55" }), KNOWN);
  assert.equal(g.freq, null);
  assert.match(g.who, /Not talking to ATC/);
  const c = guess(plane({ ...at(40, -100, 20, 0), alt_baro: 37000 }), KNOWN);
  assert.deepEqual([c.freq, c.alt2], ["132.1", "133.1"]);
  assert.match(c.who, /Kansas City Center \(high, via Near Site\)/);
  assert.deepEqual(centerNear(SITES, { lat: 40, lon: -100 }, 8000).map((x) => x.freq), ["134.1", "135.1"]);
  // A site for both bands answers for both.
  const both = [...SITES, { center: "ZKC", name: "Both", lat: 40.05, lon: -100, freq: "136.1", band: "LOW/HIGH" }];
  assert.equal(centerNear(both, { lat: 40, lon: -100 }, 8000)[0].freq, "136.1");
  assert.equal(centerNear(both, { lat: 40, lon: -100 }, 30000)[0].freq, "136.1");
});

test("guess: departures by direction, held level, and from the airline's home airport", () => {
  // Climbing out east of the hub: its east departure frequency.
  const east = at(40, -100, 12, 80);
  let g = guess(plane({ ...east, alt_baro: 7000, baro_rate: 2000, track: 80 }), KNOWN);
  assert.deepEqual([g.freq, g.sure], ["121.2", "likely"]);
  assert.match(g.who, /Test Approach, departure \(HUB, E\)/);
  // North of it: the north one. Between 13,000 and 17,000 ft it may be with Center by now.
  assert.equal(guess(plane({ ...at(40, -100, 12, 5), alt_baro: 7000, baro_rate: 2000, track: 5 }), KNOWN).freq, "121.1");
  assert.equal(guess(plane({ ...east, alt_baro: 15000, baro_rate: 2000, track: 80 }), KNOWN).sure, "maybe");
  // Level for the moment but up 3,000 ft in the last few minutes: still a departure.
  g = guess(plane({ ...east, alt_baro: 10000, baro_rate: 0, track: 80, rose: 3000 }), KNOWN);
  assert.equal(g.freq, "121.2");
  // Level near 10,000 ft with no history yet, heading away (within 90 degrees): held level.
  g = guess(plane({ ...at(40, -100, 12, 60), alt_baro: 9800, baro_rate: 0, track: 125 }), KNOWN);
  assert.equal(g.freq, "121.2");
  assert.match(g.who, /held level/);
  // An airline the area file says flies from the second airport: its departures, the nearest
  // listed direction when it has none for that way (north goes east).
  g = guess(plane({ ...at(40, -99.67, 10, 10), alt_baro: 6000, baro_rate: 1800, track: 10, flight: "SWX12" }), KNOWN);
  assert.equal(g.freq, "121.2");
  assert.match(g.who, /CTY, N/);
});

test("guess: arrivals by side, then the final", () => {
  // Coming down toward the hub from the north-east, east side: the east arrival frequencies.
  let g = guess(plane({ ...at(40, -100, 30, 45), alt_baro: 12000, baro_rate: -800, track: 225 }), KNOWN);
  assert.deepEqual([g.freq, g.alt2, g.sure], ["122.1", "122.2", "likely"]);
  // Nearly level but down 2,000 ft lately: still arriving.
  g = guess(plane({ ...at(40, -100, 30, 45), alt_baro: 11800, baro_rate: -60, track: 225, fell: 2000 }), KNOWN);
  assert.equal(g.freq, "122.1");
  // West side.
  assert.equal(guess(plane({ ...at(40, -100, 30, 300), alt_baro: 12000, baro_rate: -800, track: 120 }), KNOWN).freq, "122.3");
  // Close and low: the east runways' final.
  g = guess(plane({ ...at(40, -100, 12, 20), alt_baro: 4000, baro_rate: -700, track: 200 }), KNOWN);
  assert.deepEqual([g.freq, g.alt2], ["123.1", "122.1"]);
  // High, starting down: Center, with the arrival frequency as the second choice.
  g = guess(plane({ ...at(40, -100, 60, 45), alt_baro: 21000, baro_rate: -1500, track: 225 }), KNOWN);
  assert.deepEqual([g.freq, g.alt2, g.sure], ["132.1", "122.1", "maybe"]);
  // Low toward the second airport from its north: its north arrivals.
  g = guess(plane({ ...at(40, -99.67, 12, 0), alt_baro: 4000, baro_rate: -700, track: 180, flight: "SWX9" }), KNOWN);
  assert.deepEqual([g.freq, g.alt2], ["125.1", "125.2"]);
});

test("guess: the local sector, and without an area file", () => {
  const g = guess(plane({ ...at(40, -100, 25, 10), alt_baro: 4500, flight: "N77", track: 300 }), KNOWN);
  assert.deepEqual([g.freq, g.alt2, g.sure], ["127.1", "127.2", "maybe"]);
  // No area file: towers and Center only, never a made-up approach frequency.
  const bare = { towers: TOWERS, sites: SITES, area: null };
  assert.equal(guess(plane({ ...at(40, -100, 12, 80), alt_baro: 7000, baro_rate: 2000, track: 80 }), bare).freq, "134.1");
  assert.equal(guess(plane({ ...at(40, -100, 4, 90), alt_baro: 1500, baro_rate: -700, track: 270 }), bare).freq, "124.1");
});

// A planes service with fixed traffic, a weather geocoder, and fetch that serves the FAA zip and logos.
async function service(t, { traffic = [], lastAt = null } = {}) {
  const stateDir = await mkdtemp(path.join(tmpdir(), "s1-radio-"));
  t.after(() => {});
  await mkdir(path.join(stateDir, "radio"), { recursive: true });
  await writeFile(path.join(stateDir, "radio", "area.json"), JSON.stringify(AREA));
  const head = "FACILITY,FACILITY_TYPE,ARTCC_OR_FSS_ID,SERVICED_FACILITY,LAT_DECIMAL,LONG_DECIMAL,FREQ,SECTORIZATION,FREQ_USE";
  const csv = [head, "HUB,ATCT,ZKC,HUB,40,-100,124.1,EAST,LCL/P", "SML,ATCT,ZKC,SML,40.1,-100.3,128.5,,LCL/P",
    "NEAR SITE,RCAG,ZKC,,40.2,-100.1,132.1,HIGH,X", "LOW SITE,RCAG,ZKC,,40.1,-100,134.1,LOW,X", ""].join("\n");
  const png = (tag) => Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47]), Buffer.from(tag)]);
  const fetched = [];
  const fetchImpl = async (url) => {
    fetched.push(url);
    if (url.includes("FRQ_CSV.zip")) return new Response(zipOf("FRQ.csv", csv));
    if (url.includes("gstatic")) return new Response(png("gray tail"));          // Google's placeholder for everyone
    if (url.includes("kiwi") && url.includes("/ZZ.")) return new Response(png("kiwi placeholder"));
    if (url.includes("kiwi") && url.includes("/AA.")) return new Response(png("real logo"));
    return new Response("", { status: 404 });
  };
  const planes = { lastAt, aircraftNear: async () => traffic, getJson: async () => ({ response: { flightroute: { airline: { callsign: "TESTAIR", iata: "TT" } } } }) };
  const weather = { place: async (key) => { if (key !== "salemma") throw Object.assign(new Error("x"), { code: "unknown_place" }); return { name: "Salem", latitude: 40, longitude: -100 }; } };
  const radio = new RadioService({ stateDir, planes, weather, fetchImpl, now: () => Date.UTC(2026, 10, 3, 12), log: {} });
  return { radio, stateDir, fetched };
}

test("service: asks where until it knows, uses Skywatch's last position, and a typed town over it", async (t) => {
  const { radio } = await service(t);
  assert.equal((await radio.state()).spot, false);
  const { radio: fromSky } = await service(t, { lastAt: { latitude: 40, longitude: -100, atMs: Date.UTC(2026, 10, 3, 11) } });
  const s = await fromSky.state();
  assert.deepEqual([s.spot, s.from, s.label], [true, "cardputer", "the Cardputer's position"]);
  assert.deepEqual(await fromSky.setSpot("Nowhere, ZZ"), { error: "I couldn't find that town" });
  assert.deepEqual(await fromSky.setSpot("Salem, MA"), { ok: true, label: "Salem" });
  assert.equal((await fromSky.state()).from, "typed");
  assert.equal((await fromSky.setSpot("")).cleared, true);
  assert.equal((await fromSky.state()).from, "cardputer");
});

test("service: planes with guesses, the frequency list, and saving what was heard without a position", async (t) => {
  const traffic = [
    { hex: "abc001", flight: "AAL12  ", t: "B738", ...at(40, -100, 12, 80), alt_baro: 7000, baro_rate: 2000, track: 80, squawk: "1111" },
    { hex: "abc002", flight: "N9QX", ...at(40, -100, 25, 10), alt_baro: 4500, track: 300, squawk: "2222" },
    { hex: "abc003", flight: "XYZ1", ...at(40, -100, 70, 10), alt_baro: 4500, squawk: "2222" },   // Over 50 nm away: left out
  ];
  const { radio, stateDir } = await service(t, { traffic });
  await radio.setSpot("Salem, MA");
  const s = await radio.state();
  assert.equal(s.faa, true);
  const faa = JSON.parse(await readFile(path.join(stateDir, "radio", "faa.json"), "utf8"));
  assert.deepEqual([faa.towers.length, faa.sites.length], [2, 2]);  // Only what's used, not the whole file
  assert.deepEqual(s.planes.map((p) => [p.hex, p.say, p.freq]), [["abc001", "American 12", "121.2"], ["abc002", "November 9QX", "127.1"]]);
  assert.deepEqual(s.planes[0].hub, [12, 80]);
  // The area file's groups, then towers and Center, each group in number order; none is empty.
  assert.deepEqual(s.catalog.map(([name]) => name), ["Departures", "Towers", "Center", "Other"]);
  // The local sector's frequencies aren't in this area file's list: they show under Other.
  assert.deepEqual(s.catalog[3][1].map(([f]) => f), ["127.1", "127.2"]);
  assert.deepEqual(s.catalog[0][1].map(([f]) => f), ["121.1", "121.2", "121.4"]);
  assert.ok(s.catalog[1][1].some(([f, l]) => f === "124.1" && /Hub Tower east/.test(l)));
  assert.equal((await radio.heard({ listening: "121.2", hex: "abc001" })).predicted, "first");
  assert.equal((await radio.heard({ listening: "999", hex: "abc001" })).error, "bad_frequency");
  assert.equal((await radio.heard({ listening: "121.2", hex: "gone00" })).error, "not_found");
  await radio.heard({ listening: "127.2", typed: "Southwest 371" });
  const lines = (await readFile(path.join(stateDir, "radio", "heard.jsonl"), "utf8")).trim().split("\n").map((l) => JSON.parse(l));
  assert.equal(lines.length, 2);
  assert.deepEqual([lines[0].say, lines[0].predicted, lines[1].text], ["American 12", "first", "Southwest 371"]);
  // Nothing on disk says where the owner is.
  for (const file of await readdir(path.join(stateDir, "radio"))) {
    if (!file.endsWith(".json") && !file.endsWith(".jsonl")) continue;
    const text = await readFile(path.join(stateDir, "radio", file), "utf8");
    if (file === "area.json" || file === "faa.json") continue;  // The owner's area and the FAA's own list
    assert.doesNotMatch(text, /"lat"|"lon"|latitude|Salem/u, file);
  }
});

test("logos: Google's unless it sends its placeholder, then kiwi.com's, kept on disk", async (t) => {
  const { radio, stateDir, fetched } = await service(t);
  const png = await radio.logo("AAL");
  assert.equal(png.subarray(4).toString(), "real logo");
  assert.ok(fetched.some((u) => u.includes("gstatic") && u.includes("/AA.")));
  assert.equal((await readFile(path.join(stateDir, "radio", "logos", "AAL.png"))).subarray(4).toString(), "real logo");
  assert.equal(await radio.logo("LXJ"), null);   // No two-letter code: no logo
  assert.equal(await radio.logo("aal"), null);   // Only three capital letters
  const before = fetched.length;
  assert.ok(await radio.logo("AAL"));
  assert.equal(fetched.length, before);           // From memory now
});
