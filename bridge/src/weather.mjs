// Weather for the Cardputer's Pulse screen. The device trusts one root certificate
// and has no reason to know this service, so the bridge asks for it. No key is needed.
const GEOCODE_URL = "https://geocoding-api.open-meteo.com/v1/search";
const FORECAST_URL = "https://api.open-meteo.com/v1/forecast";
// The device asks every 30 minutes, so this only spares a device that keeps rebooting.
const FRESH_MS = 10 * 60 * 1000;
// Old weather beats none, and the screen says how old it is.
const STALE_LIMIT_MS = 3 * 60 * 60 * 1000;
const REQUEST_TIMEOUT_MS = 3000;
const MAX_PLACES = 8;
const MAX_PLACE_CHARS = 40;
// What Pulse shows for weather at the Cap's GPS position rather than a saved city.
export const HERE = "Here";
const COORDINATE = /^-?\d{1,3}(\.\d{1,6})?$/u;

const STATES = Object.freeze({ AL: "Alabama", AK: "Alaska", AZ: "Arizona", AR: "Arkansas", CA: "California",
  CO: "Colorado", CT: "Connecticut", DE: "Delaware", DC: "District of Columbia", FL: "Florida", GA: "Georgia",
  HI: "Hawaii", ID: "Idaho", IL: "Illinois", IN: "Indiana", IA: "Iowa", KS: "Kansas", KY: "Kentucky",
  LA: "Louisiana", ME: "Maine", MD: "Maryland", MA: "Massachusetts", MI: "Michigan", MN: "Minnesota",
  MS: "Mississippi", MO: "Missouri", MT: "Montana", NE: "Nebraska", NV: "Nevada", NH: "New Hampshire",
  NJ: "New Jersey", NM: "New Mexico", NY: "New York", NC: "North Carolina", ND: "North Dakota", OH: "Ohio",
  OK: "Oklahoma", OR: "Oregon", PA: "Pennsylvania", RI: "Rhode Island", SC: "South Carolina", SD: "South Dakota",
  TN: "Tennessee", TX: "Texas", UT: "Utah", VT: "Vermont", VA: "Virginia", WA: "Washington", WV: "West Virginia",
  WI: "Wisconsin", WY: "Wyoming" });
const STATE_NAMES = Object.values(STATES).sort((a, b) => b.length - a.length);

export function validPlace(text) {
  return typeof text === "string" && text.length <= MAX_PLACE_CHARS && /^[\p{L}\p{N}][\p{L}\p{N} ,.'-]*$/u.test(text) &&
    /\p{L}{2}|\p{N}{5}/u.test(text);
}

// A position from the Cap's GPS, as the device sends it. Only two decimals, about a kilometre,
// are kept or passed on, whatever arrives.
export function parseCoordinates(lat, lon) {
  if (typeof lat !== "string" || typeof lon !== "string" || !COORDINATE.test(lat) || !COORDINATE.test(lon)) return null;
  const latitude = Math.round(Number(lat) * 100) / 100, longitude = Math.round(Number(lon) * 100) / 100;
  if (!(Math.abs(latitude) <= 90) || !(Math.abs(longitude) <= 180)) return null;
  return { latitude: latitude || 0, longitude: longitude || 0 };  // Never -0, which would print as "-0.00".
}

// "Salem, MA", "Salem MA", "salem, massachusetts" or just "Salem".
export function parsePlace(text) {
  const clean = text.trim().replace(/\s+/gu, " ");
  const code = clean.match(/^(.+?)[,\s]\s*([A-Za-z]{2})$/u);
  if (code && STATES[code[2].toUpperCase()])
    return { city: code[1].replace(/,$/u, "").trim(), state: STATES[code[2].toUpperCase()] };
  const lower = clean.toLowerCase();
  for (const state of STATE_NAMES) {
    const named = lower.endsWith(`, ${state.toLowerCase()}`) || lower.endsWith(`,${state.toLowerCase()}`) ||
      (lower.endsWith(` ${state.toLowerCase()}`) && lower !== state.toLowerCase());
    if (named) return { city: clean.slice(0, -state.length).replace(/[,\s]+$/u, ""), state };
  }
  return { city: clean, state: null };
}

const comparable = text => String(text || "").toLowerCase().replace(/[^\p{L}\p{N}]/gu, "");

// Many towns share a name, and the service doesn't put the biggest first. A state
// that was asked for is never swapped for another, and parks aren't places to live.
export function bestMatch(results, { city, state }) {
  const towns = (results || []).filter(result => String(result.feature_code || "PPL").startsWith("PPL"));
  const inState = state ? towns.filter(result => result.admin1 === state) : towns;
  const exact = inState.filter(result => comparable(result.name) === comparable(city));
  const pool = exact.length ? exact : inState;
  return [...pool].sort((a, b) => (b.population || 0) - (a.population || 0))[0] || null;
}

// The WMO codes Open-Meteo uses. Text stays short enough for the screen.
export function weatherCondition(code, isDay = true) {
  const sky = isDay ? "sun" : "moon";
  if (code === 0) return { condition: isDay ? "Sunny" : "Clear", icon: sky };
  if (code === 1) return { condition: "Mostly clear", icon: sky };
  if (code === 2) return { condition: "Partly cloudy", icon: isDay ? "partly" : "cloud" };
  if (code === 3) return { condition: "Cloudy", icon: "cloud" };
  if (code === 45 || code === 48) return { condition: "Fog", icon: "fog" };
  if (code >= 51 && code <= 55) return { condition: "Drizzle", icon: "rain" };
  if (code === 56 || code === 57) return { condition: "Freezing drizzle", icon: "snow" };
  if (code === 61) return { condition: "Light rain", icon: "rain" };
  if (code === 63) return { condition: "Rain", icon: "rain" };
  if (code === 65) return { condition: "Heavy rain", icon: "rain" };
  if (code === 66 || code === 67) return { condition: "Freezing rain", icon: "snow" };
  if (code >= 71 && code <= 77) return { condition: "Snow", icon: "snow" };
  if (code >= 80 && code <= 82) return { condition: "Showers", icon: "rain" };
  if (code === 85 || code === 86) return { condition: "Snow showers", icon: "snow" };
  if (code >= 95 && code <= 99) return { condition: "Thunderstorm", icon: "storm" };
  return { condition: "Weather", icon: "cloud" };
}

async function getJson(fetchImpl, url) {
  const response = await fetchImpl(url, { redirect: "error", signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS) });
  if (!response.ok) throw new Error(`weather_http_${response.status}`);
  return response.json();
}

function reading(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) throw new Error("weather_bad_reading");
  return Math.round(number);
}

export class WeatherService {
  constructor({ geocodeUrl = GEOCODE_URL, forecastUrl = FORECAST_URL, fetchImpl = fetch, now = Date.now } = {}) {
    Object.assign(this, { geocodeUrl, forecastUrl, fetchImpl, now });
    this.places = new Map();
    this.reports = new Map();
    this.running = new Map();
  }

  async place(key, text) {
    if (this.places.has(key)) return this.places.get(key);
    const wanted = parsePlace(text);
    const query = new URLSearchParams({ name: wanted.city, count: "20", language: "en", format: "json", countryCode: "US" });
    const body = await getJson(this.fetchImpl, `${this.geocodeUrl}?${query}`);
    const match = bestMatch(body.results, wanted);
    if (!match) throw Object.assign(new Error("unknown_place"), { code: "unknown_place" });
    const place = { name: String(match.name || wanted.city).slice(0, 24),
      latitude: Number(match.latitude), longitude: Number(match.longitude) };
    if (!Number.isFinite(place.latitude) || !Number.isFinite(place.longitude)) throw new Error("weather_bad_place");
    if (this.places.size >= MAX_PLACES) this.places.delete(this.places.keys().next().value);
    this.places.set(key, place);
    return place;
  }

  async fetchReport(key, text) {
    return this.forecast(await this.place(key, text));
  }

  async forecast(place) {
    const query = new URLSearchParams({ latitude: place.latitude, longitude: place.longitude,
      current: "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m",
      daily: "temperature_2m_max,temperature_2m_min,precipitation_probability_max",
      temperature_unit: "fahrenheit", wind_speed_unit: "mph", timezone: "auto", forecast_days: "1" });
    const { current, daily } = await getJson(this.fetchImpl, `${this.forecastUrl}?${query}`);
    return { place: place.name, ...weatherCondition(current?.weather_code, current?.is_day !== 0),
      temp: reading(current?.temperature_2m), feels: reading(current?.apparent_temperature),
      humidity: reading(current?.relative_humidity_2m), wind: reading(current?.wind_speed_10m),
      high: reading(daily?.temperature_2m_max?.[0]), low: reading(daily?.temperature_2m_min?.[0]),
      rain: reading(daily?.precipitation_probability_max?.[0] ?? 0) };
  }

  async report(text) {
    const key = comparable(text);
    return this.cached(key, () => this.fetchReport(key, text));
  }

  // Weather at a GPS position, with no city to look up. Its key can't match a city's, which
  // only ever holds letters and digits.
  async reportAt(position) {
    const at = parseCoordinates(String(position?.latitude), String(position?.longitude));
    if (!at) throw Object.assign(new Error("invalid_position"), { code: "invalid_position" });
    const key = `gps:${at.latitude.toFixed(2)},${at.longitude.toFixed(2)}`;
    return this.cached(key, () => this.forecast({ name: HERE, ...at }));
  }

  async cached(key, fetcher) {
    const saved = this.reports.get(key);
    if (saved && this.now() - saved.at < FRESH_MS) return saved.report;
    if (!this.running.has(key)) {
      this.running.set(key, fetcher().finally(() => this.running.delete(key)));
    }
    try {
      const report = await this.running.get(key);
      if (this.reports.size >= MAX_PLACES && !saved) this.reports.delete(this.reports.keys().next().value);
      this.reports.set(key, { at: this.now(), report });
      return report;
    } catch (error) {
      if (error.code !== "unknown_place" && saved && this.now() - saved.at < STALE_LIMIT_MS)
        return { ...saved.report, stale: true };
      throw error;
    }
  }
}
