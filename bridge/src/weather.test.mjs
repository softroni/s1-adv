import assert from "node:assert/strict";
import test from "node:test";
import { HERE, WeatherService, bestMatch, parseCoordinates, parsePlace, validPlace, weatherCondition } from "./weather.mjs";

// Sample answers for these names, trimmed, with North Carolina listed first.
const town = (name, admin1, population, latitude, longitude, feature_code = "PPL") => ({ name, admin1, population, latitude, longitude, feature_code });
const SALEMS = [town("Salem", "North Carolina", 1615, 35.088, -84.035, "PPLA2"), town("Salem", "Idaho", 97, 43.218, -116.552, "PPLA2"),
  town("Salem", "Oregon", undefined, 42.348, -123.333), town("Salem", "Massachusetts", 20610, 42.519, -70.897), town("Salem", "Missouri", 8690, 38.49, -90.487),
  town("Salems", "California", 2213, 38.138, -120.461)];
const FALLONS = [town("O'Fallon", "Missouri", 85040, 38.811, -90.7), town("O'Fallon", "Illinois", 29002, 38.592, -89.911),
  town("O'Fallon Park", "Missouri", undefined, 38.682, -90.22, "PRK"), town("O'Fallons", "Nebraska", undefined, 41.16, -101.077)];
const FORECAST = {
  current: { temperature_2m: 69.8, apparent_temperature: 73.2, relative_humidity_2m: 84, weather_code: 3, is_day: 0, wind_speed_10m: 6.4 },
  daily: { temperature_2m_max: [78.4], temperature_2m_min: [60.6], precipitation_probability_max: [20] }
};
const reply = (body, status = 200) => ({ status, ok: status < 400, json: async () => body });

function fakeFetch(handlers = {}) {
  const calls = [];
  const impl = async url => {
    calls.push(String(url));
    if (String(url).includes("geocoding")) return handlers.place ? handlers.place() : reply({ results: SALEMS });
    return handlers.forecast ? handlers.forecast() : reply(FORECAST);
  };
  return { impl, calls };
}

test("a place can be a city, with or without its state, or a ZIP", () => {
  for (const good of ["Salem, MA", "Salem", "St. Louis, MO", "O'Fallon, IL", "Winston-Salem", "10001", "New York"]) assert.ok(validPlace(good), good);
  for (const bad of ["", " ", "a", "x".repeat(41), null, undefined, 10001, "../etc", "a&b=c", "<b>", "Salem\n", "123"]) assert.ok(!validPlace(bad), String(bad));
});

test("the state is understood however it's written", () => {
  const parsed = text => { const { city, state } = parsePlace(text); return `${city}|${state}`; };
  assert.equal(parsed("Salem, MA"), "Salem|Massachusetts");
  assert.equal(parsed("Salem,MA"), "Salem|Massachusetts");
  assert.equal(parsed("salem ma"), "salem|Massachusetts");
  assert.equal(parsed("Salem, Massachusetts"), "Salem|Massachusetts");
  assert.equal(parsed("Salem Massachusetts"), "Salem|Massachusetts");
  assert.equal(parsed("  Salt   Lake City, UT "), "Salt Lake City|Utah");
  assert.equal(parsed("Winston-Salem, North Carolina"), "Winston-Salem|North Carolina");
  assert.equal(parsed("Charleston, West Virginia"), "Charleston|West Virginia");
  // Things that only look like a state stay part of the city.
  assert.equal(parsed("Salem"), "Salem|null");
  assert.equal(parsed("New York"), "New York|null");
  assert.equal(parsed("Massachusetts"), "Massachusetts|null");
  assert.equal(parsed("Fort Worth"), "Fort Worth|null");
  assert.equal(parsed("10001"), "10001|null");
});

test("the right town wins even when the service lists another first", () => {
  const pick = text => bestMatch(SALEMS, parsePlace(text));
  assert.equal(pick("Salem, MA").admin1, "Massachusetts");
  assert.equal(pick("Salem, NC").admin1, "North Carolina");
  assert.equal(pick("Salem").admin1, "Massachusetts", "with no state, the largest exact match");
  assert.equal(pick("Salem, CA").name, "Salems", "a close name in the right state beats nothing");
  assert.equal(pick("Salem, FL"), null, "never another state's town");
  assert.equal(bestMatch(FALLONS, parsePlace("O'Fallon, IL")).admin1, "Illinois");
  assert.equal(bestMatch(FALLONS, parsePlace("OFallon")).admin1, "Missouri", "punctuation doesn't matter");
  assert.equal(bestMatch([FALLONS[2]], parsePlace("O'Fallon Park")), null, "a park isn't a place to live");
  assert.equal(bestMatch(undefined, parsePlace("Nowhere")), null);
  assert.equal(bestMatch([town("St Louis", "Missouri", 279695, 38.6, -90.2, "PPLA2")], parsePlace("St. Louis, MO")).name, "St Louis");
});

test("each weather code reads sensibly by day and night", () => {
  assert.deepEqual(weatherCondition(0, true), { condition: "Sunny", icon: "sun" });
  assert.deepEqual(weatherCondition(0, false), { condition: "Clear", icon: "moon" });
  assert.equal(weatherCondition(2, true).icon, "partly");
  assert.equal(weatherCondition(2, false).icon, "cloud");
  assert.equal(weatherCondition(63).condition, "Rain");
  assert.equal(weatherCondition(95).icon, "storm");
  assert.equal(weatherCondition(75).icon, "snow");
  assert.equal(weatherCondition(48).icon, "fog");
  assert.equal(weatherCondition(undefined).icon, "cloud");
  for (let code = 0; code < 100; ++code) assert.ok(weatherCondition(code).condition.length <= 16, String(code));
});

test("a report is whole numbers in Fahrenheit for that town, and shows its own name", async () => {
  const fake = fakeFetch();
  const report = await new WeatherService({ fetchImpl: fake.impl }).report("Salem, MA");
  assert.deepEqual(report, { place: "Salem", condition: "Cloudy", icon: "cloud",
    temp: 70, feels: 73, humidity: 84, wind: 6, high: 78, low: 61, rain: 20 });
  const lookup = new URL(fake.calls[0]);
  assert.equal(lookup.searchParams.get("name"), "Salem", "the state filters the answer, it isn't part of the search");
  assert.equal(lookup.searchParams.get("countryCode"), "US");
  const forecast = new URL(fake.calls[1]);
  assert.equal(forecast.searchParams.get("temperature_unit"), "fahrenheit");
  assert.equal(forecast.searchParams.get("latitude"), "42.519");
  assert.equal(forecast.searchParams.get("longitude"), "-70.897");
});

test("reports are cached briefly, the place for good, and old weather beats none", async () => {
  let at = 1000, down = false;
  const fake = fakeFetch({ forecast: () => { if (down) throw new Error("offline"); return reply(FORECAST); } });
  const service = new WeatherService({ fetchImpl: fake.impl, now: () => at });
  await service.report("Salem, MA");
  assert.equal(fake.calls.length, 2);
  at += 9 * 60000;
  await service.report("salem, ma");
  assert.equal(fake.calls.length, 2, "still fresh, however it's typed");
  at += 2 * 60000;
  await service.report("Salem, MA");
  assert.equal(fake.calls.length, 3, "the place isn't looked up again");
  down = true;
  at += 11 * 60000;
  assert.equal((await service.report("Salem, MA")).stale, true);
  at += 4 * 60 * 60000;
  await assert.rejects(service.report("Salem, MA"), /offline/u);
});

test("two devices asking at once make one request", async () => {
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  const fake = fakeFetch({ forecast: async () => { await gate; return reply(FORECAST); } });
  const service = new WeatherService({ fetchImpl: fake.impl });
  const both = Promise.all([service.report("Salem, MA"), service.report("Salem, MA")]);
  await new Promise(resolve => setImmediate(resolve));
  release();
  const [first, second] = await both;
  assert.deepEqual(first, second);
  assert.equal(fake.calls.filter(url => url.includes("forecast")).length, 1);
});

test("an unknown place says so and a broken answer isn't passed on", async () => {
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ place: () => reply({}) }).impl }).report("Zzqxville"),
    error => error.code === "unknown_place");
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch().impl }).report("Salem, FL"), error => error.code === "unknown_place");
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ forecast: () => reply({ current: {}, daily: {} }) }).impl }).report("Salem"), /bad_reading/u);
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ forecast: () => reply({}, 500) }).impl }).report("Salem"), /weather_http_500/u);
});

test("the place list can't grow without limit", async () => {
  const service = new WeatherService({ fetchImpl: fakeFetch().impl });
  for (let i = 0; i < 20; ++i) await service.report(`Salem${"x".repeat(i)}`).catch(() => {});
  assert.ok(service.places.size <= 8 && service.reports.size <= 8);
});

test("a GPS position is kept to two decimals, and anything else is refused", () => {
  assert.deepEqual(parseCoordinates("42.5083", "-70.9049"), { latitude: 42.51, longitude: -70.9 });
  assert.deepEqual(parseCoordinates("42.51", "-70.90"), { latitude: 42.51, longitude: -70.9 });
  assert.deepEqual(parseCoordinates("-0.004", "0"), { latitude: 0, longitude: 0 });
  assert.deepEqual(parseCoordinates("90", "-180"), { latitude: 90, longitude: -180 });
  for (const [lat, lon] of [["91", "0"], ["0", "180.01"], ["1e2", "0"], ["42.5", ""], ["", "0"], [null, "0"], ["42.5", undefined],
    ["0x10", "0"], ["42.5083331", "0"], ["NaN", "0"], ["Infinity", "0"], [" 42", "0"], ["42,5", "0"], ["4250", "0"]])
    assert.equal(parseCoordinates(lat, lon), null, `${lat},${lon}`);
});

test("weather at a GPS position needs no city, asks with two decimals, and says Here", async () => {
  const fake = fakeFetch();
  const service = new WeatherService({ fetchImpl: fake.impl });
  const report = await service.reportAt({ latitude: 42.5083, longitude: -70.9049 });
  assert.equal(report.place, HERE);
  assert.equal(report.temp, 70);
  assert.equal(fake.calls.length, 1, "no city lookup");
  const asked = new URL(fake.calls[0]).searchParams;
  assert.equal(asked.get("latitude"), "42.51");
  assert.equal(asked.get("longitude"), "-70.9");
  // Within the same hundredth of a degree it's the same report, and a city's report is separate.
  await service.reportAt({ latitude: 42.5149, longitude: -70.9001 });
  assert.equal(fake.calls.length, 1);
  await service.report("Salem, MA");
  assert.equal(service.reports.size, 2);
  await assert.rejects(service.reportAt({ latitude: 95, longitude: 0 }), error => error.code === "invalid_position");
  await assert.rejects(service.reportAt(null), error => error.code === "invalid_position");
});

test("old weather at a position beats none, and the list stays bounded", async () => {
  let now = 0, fail = false;
  const service = new WeatherService({ now: () => now,
    fetchImpl: fakeFetch({ forecast: () => fail ? reply({}, 500) : reply(FORECAST) }).impl });
  await service.reportAt({ latitude: 42.51, longitude: -70.9 });
  now += 20 * 60 * 1000;
  fail = true;
  assert.equal((await service.reportAt({ latitude: 42.51, longitude: -70.9 })).stale, true);
  fail = false;
  for (let i = 0; i < 20; ++i) await service.reportAt({ latitude: i, longitude: i });
  assert.ok(service.reports.size <= 8);
});
