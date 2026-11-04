// Weather on Pulse from the Cap's GPS. With a fix, Pulse shows the weather where the Cardputer is,
// as "Here", instead of at the saved city. Pulse listens to the GPS while it's on screen, and a fix
// seen in the Lab counts too. Without the Cap, or with no fix, the saved city is used as before.
namespace {
weather_gps::Spot gWeatherHere;     // The latest fix.
weather_gps::Spot gWeatherAsking;   // What the request in flight is for: not known means the city.
weather_gps::Spot gWeatherShownAt;  // What the weather on screen is for.
bool gPulseHoldsGps = false, gWeatherNudged = false;
uint32_t gWeatherNudgeMs = 0;

bool pulseWantsGps() { return gUiMode == UiMode::Home && !gMeshBusOwned && !gLabOpen; }

void servicePulseGps(uint32_t now) {
  const bool want = pulseWantsGps();
  if (want && !gPulseHoldsGps) gpsHold(kGpsForPulse);
  else if (!want && gPulseHoldsGps) gpsRelease(kGpsForPulse);
  gPulseHoldsGps = want;
  if (gPulseHoldsGps) gpsFeed(now);
  if (gGpsHolders && gLabGps.hearing(now) && gLabGps.fix.valid) {
    gWeatherHere.known = true;
    gWeatherHere.latE5 = gLabGps.fix.latE5;
    gWeatherHere.lonE5 = gLabGps.fix.lonE5;
    gWeatherHere.atMs = now;
  }
  if (weather_gps::askSooner(gWeatherHere, gWeatherShownAt, now, gWeatherNudged, gWeatherNudgeMs)) {
    gWeatherNudged = true;
    gWeatherNudgeMs = now;
    gWeatherSchedule.reset();  // Due now; the next background poll asks.
  }
}

String weatherQuery(uint32_t now) {
  if (weather_gps::usable(gWeatherHere, now)) {
    char text[48];
    weather_gps::query(text, sizeof(text), gWeatherHere);
    gWeatherAsking = gWeatherHere;
    return String(text);
  }
  gWeatherAsking = weather_gps::Spot{};
  return "place=" + urlEncodePathSegment(gDeviceSettings.weatherPlace);
}

// The weather that just arrived is for wherever it was asked about.
void noteWeatherAnswered() { gWeatherShownAt = gWeatherAsking; }
}  // namespace
