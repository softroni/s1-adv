// Mesh's hardware beyond the radio: the Cap's GPS, the Cardputer's own flash for its name, its
// contacts and its history, a chime, and the screen going dark while Mesh waits. It comes after the
// Lab, which owns the GPS's parser, and after the screensaver, whose screen-off it uses.
namespace {
// Internal flash, not the SD card, which the radio shares a bus with.
constexpr const char* kMeshHistoryPath = "/mesh_history.bin";
constexpr const char* kMeshHistoryTemp = "/mesh_history.tmp";
// While Help repeats, the screen goes dark after this long without a key, to save the battery.
constexpr uint32_t kMeshHelpScreenOffMs = 30000;
bool gMeshHoldsGps = false, gMeshNameLoaded = false;
char gMeshLongName[s1_mesh::kNameBytes + 1] = "S1-ADV";
char gMeshShortName[5] = "S1";

void meshGps(bool on) {
  if (on == gMeshHoldsGps) return;
  gMeshHoldsGps = on;
  if (on) gpsHold(kGpsForMesh);
  else gpsRelease(kGpsForMesh);
}
// The latest fix, kept with when it was seen, so a lost fix still says where the Cardputer last was.
void meshReadGps(uint32_t now, s1_mesh::Where& where) {
  if (!gMeshHoldsGps) return;
  gpsFeed(now);
  const bool hearing = gLabGps.hearing(now);
  where.satellites = hearing ? gLabGps.fix.satellites : 0;
  if (hearing && gLabGps.fix.valid) {
    where.known = true;
    where.latE5 = gLabGps.fix.latE5;
    where.lonE5 = gLabGps.fix.lonE5;
    where.atMs = now;
  }
}
bool meshGpsHearing(uint32_t now) { return gMeshHoldsGps && gLabGps.hearing(now); }

// The name this Cardputer announces, saved in NVS beside its identity. None saved is S1-ADV.
void meshSavedName(char* longName, char* shortName) {
  if (!gMeshNameLoaded) {
    gMeshNameLoaded = true;
    Preferences prefs;
    if (prefs.begin("s1mesh", true)) {
      char raw[s1_mesh::kNameBytes + 1] = {};
      const size_t length = prefs.getBytesLength("name");
      const size_t got = length && length <= s1_mesh::kNameBytes ? prefs.getBytes("name", raw, length) : 0;
      raw[got] = 0;
      prefs.end();
      char clean[s1_mesh::kNameBytes + 1];
      s1_mesh::cleanName(clean, sizeof(clean), raw);
      if (clean[0]) {
        strcpy(gMeshLongName, clean);
        s1_mesh::shortNameFor(clean, gMeshShortName);
      }
    }
  }
  strcpy(longName, gMeshLongName);
  strcpy(shortName, gMeshShortName);
}
bool meshSaveName(const char* name) {
  char clean[s1_mesh::kNameBytes + 1];
  s1_mesh::cleanName(clean, sizeof(clean), name);
  Preferences prefs;
  if (!prefs.begin("s1mesh", false)) return false;
  const bool saved = clean[0] ? prefs.putBytes("name", clean, strlen(clean)) == strlen(clean)
                              : (!prefs.isKey("name") || prefs.remove("name"));
  prefs.end();
  if (!saved) return false;
  strcpy(gMeshLongName, clean[0] ? clean : s1_mesh::kDefaultLongName);
  if (clean[0]) s1_mesh::shortNameFor(clean, gMeshShortName);
  else strcpy(gMeshShortName, s1_mesh::kDefaultShortName);
  gMeshNameLoaded = true;
  logf("[MESH] name saved short=%s", gMeshShortName);
  return true;
}
void meshLoadProfile(s1_mesh::RadioSession& session) {
  meshSavedName(session.longName, session.shortName);
  Preferences prefs;
  if (!prefs.begin("s1mesh", true)) return;
  const size_t length = prefs.getBytesLength("contacts");
  if (length && length <= s1_mesh::Contacts::kBytes) {
    uint8_t bytes[s1_mesh::Contacts::kBytes];
    if (prefs.getBytes("contacts", bytes, length) != length || !session.contacts.load(bytes, length))
      logf("[MESH] contacts unreadable; none loaded");
  }
  prefs.end();
}
bool meshSaveContacts(const s1_mesh::Contacts& contacts) {
  uint8_t bytes[s1_mesh::Contacts::kBytes];
  const size_t length = contacts.save(bytes, sizeof(bytes));
  Preferences prefs;
  if (!length || !prefs.begin("s1mesh", false)) return false;
  const bool saved = prefs.putBytes("contacts", bytes, length) == length;
  prefs.end();
  logf("[MESH] contacts saved=%d count=%u", saved, unsigned(contacts.count));
  return saved;
}

// The history lives in LittleFS, written whole to a spare file and then renamed over the old one,
// so a write cut short by a flat battery leaves the last good copy.
void meshLoadHistory(s1_mesh::State& state) {
  if (!gLittleFsReady || !LittleFS.exists(kMeshHistoryPath)) return;
  File file = LittleFS.open(kMeshHistoryPath, "r");
  if (!file) return;
  const size_t size = file.size();
  if (size && size <= s1_mesh::kHistoryBytes) {
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]);
    if (bytes && file.read(bytes.get(), size) == size && !state.loadHistory(bytes.get(), size))
      logf("[MESH] history unreadable; starting fresh");
  }
  file.close();
}
bool meshWriteHistory(const s1_mesh::State& state) {
  if (!gLittleFsReady) return false;
  std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[s1_mesh::kHistoryBytes]);
  if (!bytes) return false;
  const size_t length = state.saveHistory(bytes.get(), s1_mesh::kHistoryBytes);
  File file = LittleFS.open(kMeshHistoryTemp, "w");
  if (!file) return false;
  const bool wrote = file.write(bytes.get(), length) == length;
  file.close();
  if (!wrote) { LittleFS.remove(kMeshHistoryTemp); return false; }
  if (LittleFS.rename(kMeshHistoryTemp, kMeshHistoryPath)) return true;
  LittleFS.remove(kMeshHistoryPath);
  return LittleFS.rename(kMeshHistoryTemp, kMeshHistoryPath);
}

// A message for this Cardputer, or from a contact. Talk on the public channel stays quiet.
void meshChime(bool important) { focusTone(important ? 1760 : 1320, important ? 220 : 90); }

// Mesh skips the screensaver, so it turns the screen off itself: after the device's own screen-off
// time if that's on, and after 30 seconds while Help repeats, whatever the setting. A key brings it
// back as anywhere else, and so does a message worth a chime.
void meshScreenService(uint32_t now, bool helpOn) {
  if (gScreenOffActive || gScreensaverActive) return;
  uint32_t limit = 0;
  if (helpOn) limit = kMeshHelpScreenOffMs;
  else if (gDeviceSettings.screenAutoOffEnabled) limit = uint32_t(max<uint8_t>(5, gDeviceSettings.screenOffAfterMin)) * 60000UL;
  if (!limit || now - gLastUserActivityMs < limit) return;
  gScreensaverPreviousMode = UiMode::Mesh;
  gScreenOffActive = true;
  applyDisplayBrightness();
  logf("[MESH] screen off help=%d", helpOn);
}
void meshWakeScreen() {
  if (gScreenOffActive || gScreensaverDimmed) handleScreensaverWake();
}
}  // namespace
