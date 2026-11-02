// Weather for the Cardputer's Pulse screen. The device trusts one root certificate
// and has no reason to know these services, so the bridge asks for it. Neither
// service needs a key.
const ZIP_URL = "https://api.zippopotam.us/us/";
const FORECAST_URL = "https://api.open-meteo.com/v1/forecast";
// The device asks every 30 minutes, so this only spares a device that keeps rebooting.
const FRESH_MS = 10 * 60 * 1000;
// Old weather beats none, and the screen says how old it is.
const STALE_LIMIT_MS = 3 * 60 * 60 * 1000;
const REQUEST_TIMEOUT_MS = 3000;
const MAX_ZIPS = 8;

export function validZip(zip) {
  return typeof zip === "string" && /^\d{5}$/u.test(zip);
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
  if (response.status === 404) throw Object.assign(new Error("unknown_zip"), { code: "unknown_zip" });
  if (!response.ok) throw new Error(`weather_http_${response.status}`);
  return response.json();
}

function reading(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) throw new Error("weather_bad_reading");
  return Math.round(number);
}

export class WeatherService {
  constructor({ zipUrl = ZIP_URL, forecastUrl = FORECAST_URL, fetchImpl = fetch, now = Date.now } = {}) {
    Object.assign(this, { zipUrl, forecastUrl, fetchImpl, now });
    this.places = new Map();
    this.reports = new Map();
    this.running = new Map();
  }

  async place(zip) {
    if (this.places.has(zip)) return this.places.get(zip);
    const body = await getJson(this.fetchImpl, `${this.zipUrl}${zip}`);
    const match = body.places?.[0];
    const place = { name: String(match?.["place name"] || "").slice(0, 24),
      latitude: Number(match?.latitude), longitude: Number(match?.longitude) };
    if (!place.name || !Number.isFinite(place.latitude) || !Number.isFinite(place.longitude))
      throw new Error("weather_bad_place");
    if (this.places.size >= MAX_ZIPS) this.places.delete(this.places.keys().next().value);
    this.places.set(zip, place);
    return place;
  }

  async fetchReport(zip) {
    const place = await this.place(zip);
    const query = new URLSearchParams({ latitude: place.latitude, longitude: place.longitude,
      current: "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m",
      daily: "temperature_2m_max,temperature_2m_min,precipitation_probability_max",
      temperature_unit: "fahrenheit", wind_speed_unit: "mph", timezone: "auto", forecast_days: "1" });
    const { current, daily } = await getJson(this.fetchImpl, `${this.forecastUrl}?${query}`);
    return { zip, place: place.name, ...weatherCondition(current?.weather_code, current?.is_day !== 0),
      temp: reading(current?.temperature_2m), feels: reading(current?.apparent_temperature),
      humidity: reading(current?.relative_humidity_2m), wind: reading(current?.wind_speed_10m),
      high: reading(daily?.temperature_2m_max?.[0]), low: reading(daily?.temperature_2m_min?.[0]),
      rain: reading(daily?.precipitation_probability_max?.[0] ?? 0) };
  }

  async report(zip) {
    const saved = this.reports.get(zip);
    if (saved && this.now() - saved.at < FRESH_MS) return saved.report;
    if (!this.running.has(zip)) {
      this.running.set(zip, this.fetchReport(zip).finally(() => this.running.delete(zip)));
    }
    try {
      const report = await this.running.get(zip);
      if (this.reports.size >= MAX_ZIPS && !saved) this.reports.delete(this.reports.keys().next().value);
      this.reports.set(zip, { at: this.now(), report });
      return report;
    } catch (error) {
      if (error.code !== "unknown_zip" && saved && this.now() - saved.at < STALE_LIMIT_MS)
        return { ...saved.report, stale: true };
      throw error;
    }
  }
}
