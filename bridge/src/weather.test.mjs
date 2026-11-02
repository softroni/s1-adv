import assert from "node:assert/strict";
import test from "node:test";
import { WeatherService, validZip, weatherCondition } from "./weather.mjs";

const PLACE = { places: [{ "place name": "Salem", latitude: "42.5195", longitude: "-70.8967" }] };
const FORECAST = {
  current: { temperature_2m: 69.8, apparent_temperature: 73.2, relative_humidity_2m: 84, weather_code: 3, is_day: 0, wind_speed_10m: 6.4 },
  daily: { temperature_2m_max: [78.4], temperature_2m_min: [60.6], precipitation_probability_max: [20] }
};
const reply = (body, status = 200) => ({ status, ok: status < 400, json: async () => body });

function fakeFetch(handlers = {}) {
  const calls = [];
  const impl = async url => {
    calls.push(String(url));
    if (String(url).includes("zippopotam") || String(url).includes("/zip/")) return handlers.place ? handlers.place() : reply(PLACE);
    return handlers.forecast ? handlers.forecast() : reply(FORECAST);
  };
  return { impl, calls };
}

test("only a five digit ZIP is accepted", () => {
  assert.ok(validZip("10001"));
  for (const bad of ["1000", "100011", "1000a", "", null, undefined, 10001, "../x", "10001\n"]) assert.ok(!validZip(bad), String(bad));
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

test("a report is whole numbers in Fahrenheit for the ZIP's location", async () => {
  const fake = fakeFetch();
  const report = await new WeatherService({ fetchImpl: fake.impl }).report("10001");
  assert.deepEqual(report, { zip: "10001", place: "Salem", condition: "Cloudy", icon: "cloud",
    temp: 70, feels: 73, humidity: 84, wind: 6, high: 78, low: 61, rain: 20 });
  const forecast = new URL(fake.calls[1]);
  assert.equal(forecast.searchParams.get("temperature_unit"), "fahrenheit");
  assert.equal(forecast.searchParams.get("latitude"), "42.5195");
  assert.equal(forecast.searchParams.get("forecast_days"), "1");
});

test("reports are cached briefly, the place for good, and old weather beats none", async () => {
  let at = 1000, down = false;
  const fake = fakeFetch({ forecast: () => { if (down) throw new Error("offline"); return reply(FORECAST); } });
  const service = new WeatherService({ fetchImpl: fake.impl, now: () => at });
  await service.report("10001");
  assert.equal(fake.calls.length, 2);
  at += 9 * 60000;
  await service.report("10001");
  assert.equal(fake.calls.length, 2, "still fresh");
  at += 2 * 60000;
  await service.report("10001");
  assert.equal(fake.calls.length, 3, "the place isn't looked up again");
  down = true;
  at += 11 * 60000;
  assert.equal((await service.report("10001")).stale, true);
  at += 4 * 60 * 60000;
  await assert.rejects(service.report("10001"), /offline/u);
});

test("two devices asking at once make one request", async () => {
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  const fake = fakeFetch({ forecast: async () => { await gate; return reply(FORECAST); } });
  const service = new WeatherService({ fetchImpl: fake.impl });
  const both = Promise.all([service.report("10001"), service.report("10001")]);
  await new Promise(resolve => setImmediate(resolve));
  release();
  const [first, second] = await both;
  assert.deepEqual(first, second);
  assert.equal(fake.calls.filter(url => url.includes("forecast")).length, 1);
});

test("an unknown ZIP says so and a broken answer isn't passed on", async () => {
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ place: () => reply({}, 404) }).impl }).report("00000"),
    error => error.code === "unknown_zip");
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ place: () => reply({ places: [] }) }).impl }).report("10001"), /bad_place/u);
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ forecast: () => reply({ current: {}, daily: {} }) }).impl }).report("10001"), /bad_reading/u);
  await assert.rejects(new WeatherService({ fetchImpl: fakeFetch({ forecast: () => reply({}, 500) }).impl }).report("10001"), /weather_http_500/u);
});

test("the place list can't grow without limit", async () => {
  const service = new WeatherService({ fetchImpl: fakeFetch().impl });
  for (let i = 0; i < 20; ++i) await service.report(String(70000 + i));
  assert.ok(service.places.size <= 8 && service.reports.size <= 8);
});
