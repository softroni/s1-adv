// Mesh's private channel: its setup screen, from C on Mesh's first screen, and where it's kept.
// The channel goes alongside the public one, the way a Meshtastic app adds a secondary channel,
// so public messages, direct messages and their receipts carry on. The name and key live in NVS
// with Mesh's other settings. The key is shown only when asked for, and never logged or sent.
// mesh_private_channel.h holds the rules, and the library's patch does the radio side.
namespace {
enum class MeshChannelEdit : uint8_t { None, Name, Key };
bool gMeshChannelSetup = false;
bool gMeshChannelShowKey = false, gMeshChannelRemoving = false, gMeshChannelReplacing = false;
MeshChannelEdit gMeshChannelEdit = MeshChannelEdit::None;
char gMeshChannelInput[s1_mesh::kChannelKeyText + 8] = {};
s1_mesh::PrivateChannel gMeshChannel;  // What the screen shows and saves.
bool gMeshChannelLoaded = false;
String gMeshChannelNote;
uint32_t gMeshChannelNoteUntilMs = 0;
constexpr const char* kMeshChannelKey = "privch";

void meshChannelSay(const char* note) {
  gMeshChannelNote = note;
  gMeshChannelNoteUntilMs = millis() + 2500;
}
const s1_mesh::PrivateChannel& meshChannelSaved() {
  if (gMeshChannelLoaded) return gMeshChannel;
  gMeshChannelLoaded = true;
  Preferences prefs;
  uint8_t bytes[s1_mesh::kChannelSavedBytes] = {};
  size_t got = 0;
  if (prefs.begin("s1mesh", true)) {
    if (prefs.getBytesLength(kMeshChannelKey) == sizeof(bytes)) got = prefs.getBytes(kMeshChannelKey, bytes, sizeof(bytes));
    prefs.end();
  }
  s1_mesh::loadChannel(bytes, got, gMeshChannel);  // Anything that doesn't read back whole is no channel.
  memset(bytes, 0, sizeof(bytes));
  return gMeshChannel;
}
bool meshChannelStore() {
  uint8_t bytes[s1_mesh::kChannelSavedBytes];
  s1_mesh::saveChannel(gMeshChannel, bytes);
  Preferences prefs;
  bool saved = false;
  if (prefs.begin("s1mesh", false)) {
    saved = gMeshChannel.set() ? prefs.putBytes(kMeshChannelKey, bytes, sizeof(bytes)) == sizeof(bytes)
                               : prefs.remove(kMeshChannelKey) || !prefs.isKey(kMeshChannelKey);
    prefs.end();
  }
  memset(bytes, 0, sizeof(bytes));
  logf("[MESH] private channel saved=%d set=%d", saved, gMeshChannel.set());  // Never the key.
  return saved;
}
void meshChannelRandom(uint8_t* out, size_t length) { esp_fill_random(out, length); }

void meshChannelClearInput() {
  memset(gMeshChannelInput, 0, sizeof(gMeshChannelInput));
  gMeshChannelEdit = MeshChannelEdit::None;
}
// Typing stops at Enter, so what's typed is only checked then.
void meshChannelFinishEdit() {
  if (gMeshChannelEdit == MeshChannelEdit::Name) {
    if (!s1_mesh::validChannelName(gMeshChannelInput)) {
      meshChannelSay("Letters, digits, - and _, up to 11");
      return;
    }
    strncpy(gMeshChannel.name, gMeshChannelInput, s1_mesh::kChannelNameMax);
    gMeshChannel.name[s1_mesh::kChannelNameMax] = 0;
    meshChannelClearInput();
    if (!gMeshChannel.keyLength) {
      meshChannelSay("Now G for a new key, or K to type one");
      return;
    }
  } else if (gMeshChannelEdit == MeshChannelEdit::Key) {
    uint8_t key[s1_mesh::kChannelKeyMax];
    uint8_t length = 0;
    if (!s1_mesh::keyFromBase64(gMeshChannelInput, key, length)) {
      meshChannelSay("That isn't a 16 or 32 byte key");
      return;
    }
    if (s1_mesh::wellKnownKey(key, length)) {
      memset(key, 0, sizeof(key));
      meshChannelSay("That's Meshtastic's public key");
      return;
    }
    memcpy(gMeshChannel.key, key, length);
    gMeshChannel.keyLength = length;
    memset(key, 0, sizeof(key));
    meshChannelClearInput();
    if (!gMeshChannel.name[0]) {
      meshChannelSay("Now N to give it a name");
      return;
    }
  }
  if (gMeshChannel.set()) meshChannelSay(meshChannelStore() ? "Saved. Mesh hears it from now on" : "Couldn't save it");
}

void meshLoadPrivateChannel(s1_mesh::RadioSession& session) {
  session.privateChannel = meshChannelSaved();
  if (!session.privateChannel.set()) return;
  const bool ok = session.mesh.setSecondaryChannel(session.privateChannel.key, session.privateChannel.keyLength,
                                                   session.privateChannel.name);
  if (!ok) session.privateChannel = s1_mesh::PrivateChannel{};
  logf("[MESH] private channel on=%d", ok);
}

void meshChannelOpen() {
  meshChannelSaved();
  gMeshChannelSetup = true;
  gMeshChannelShowKey = false;
  gMeshChannelRemoving = false;
  gMeshChannelReplacing = false;
  meshChannelClearInput();
  gMeshChannelNote = "";
}

bool meshChannelEscape() {
  if (!gMeshChannelSetup) return false;
  if (gMeshChannelEdit != MeshChannelEdit::None) {
    meshChannelClearInput();
    return true;
  }
  // Leaving the screen hides the key and drops a half-made channel nobody can use yet.
  gMeshChannelSetup = false;
  gMeshChannelShowKey = false;
  gMeshChannelLoaded = false;
  meshChannelSaved();
  return true;
}

bool meshChannelKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl) return true;
  const bool enter = keys.enter || key == '\r' || key == '\n';
  if (gMeshChannelEdit != MeshChannelEdit::None) {
    if (enter) { meshChannelFinishEdit(); return true; }
    const size_t length = strlen(gMeshChannelInput);
    if (keys.del || key == '\b') {
      if (length) gMeshChannelInput[length - 1] = 0;
      return true;
    }
    const size_t most = gMeshChannelEdit == MeshChannelEdit::Name ? s1_mesh::kChannelNameMax : sizeof(gMeshChannelInput) - 1;
    if (key >= 32 && key < 127 && length < most) gMeshChannelInput[length] = key;
    return true;
  }
  const char lower = char(tolower(static_cast<unsigned char>(key)));
  if (lower != 'x') gMeshChannelRemoving = false;
  if (lower != 'g') gMeshChannelReplacing = false;
  if (lower == 'n') {
    gMeshChannelEdit = MeshChannelEdit::Name;
    strncpy(gMeshChannelInput, gMeshChannel.name, s1_mesh::kChannelNameMax);
  } else if (lower == 'k') {
    gMeshChannelEdit = MeshChannelEdit::Key;
    gMeshChannelInput[0] = 0;
  } else if (lower == 'g') {
    // A key already shared is what everyone else has, so replacing it takes a second press.
    if (gMeshChannel.keyLength && !gMeshChannelReplacing) {
      gMeshChannelReplacing = true;
      meshChannelSay("G again replaces the shared key");
      return true;
    }
    gMeshChannelReplacing = false;
    uint8_t made[s1_mesh::kChannelKeyMax];
    uint8_t length = 0;
    if (!s1_mesh::newKey(meshChannelRandom, made, length)) { meshChannelSay("Couldn't make a key"); return true; }
    memcpy(gMeshChannel.key, made, length);
    gMeshChannel.keyLength = length;
    memset(made, 0, sizeof(made));
    if (!gMeshChannel.name[0]) strcpy(gMeshChannel.name, "Private");
    gMeshChannelShowKey = true;  // A new key is only any use once it's been shared.
    meshChannelSay(meshChannelStore() ? "New key saved" : "Couldn't save it");
  } else if (lower == 'v') {
    if (gMeshChannel.keyLength) gMeshChannelShowKey = !gMeshChannelShowKey;
  } else if (lower == 'x' && gMeshChannel.set()) {
    if (!gMeshChannelRemoving) {
      gMeshChannelRemoving = true;
      meshChannelSay("X again removes it");
    } else {
      gMeshChannelRemoving = false;
      gMeshChannelShowKey = false;
      gMeshChannel = s1_mesh::PrivateChannel{};
      meshChannelSay(meshChannelStore() ? "Removed" : "Couldn't remove it");
    }
  }
  return true;
}

String meshChannelStartLine() {
  const auto& channel = meshChannelSaved();
  return channel.set() ? String("C private channel: #") + channel.name : String("C sets up a private channel");
}

void renderMeshChannelUi() {
  auto& d = gCanvas;
  const uint16_t bg = rgb565_local(3, 8, 14), muted = rgb565_local(130, 145, 155), cyan = rgb565_local(47, 227, 255),
                 amber = rgb565_local(255, 200, 64);
  d.setFont(&fonts::Font2); d.setTextColor(TFT_WHITE, bg); d.setCursor(8, 40); d.print("Private channel");
  d.setFont(&fonts::Font0);
  const bool noting = !gMeshChannelNote.isEmpty() && int32_t(millis() - gMeshChannelNoteUntilMs) < 0;
  if (gMeshChannelShowKey && gMeshChannel.keyLength && gMeshChannelEdit == MeshChannelEdit::None) {
    // To share: the key as text, and as a QR code a phone reads, since I and l look alike here.
    char text[s1_mesh::kChannelKeyText];
    s1_mesh::keyToBase64(gMeshChannel.key, gMeshChannel.keyLength, text, sizeof(text));
    d.setTextColor(muted, bg); d.setCursor(8, 60); d.print("Name");
    d.setTextColor(TFT_WHITE, bg); d.setCursor(40, 60); d.print(gMeshChannel.name);
    const String key(text);
    d.setTextColor(amber, bg);
    for (int line = 0; line < 3; ++line) { d.setCursor(8, 74 + line * 10); d.print(key.substring(line * 15, min<int>(int(key.length()), line * 15 + 15))); }
    // A note can be long, so it's cut to the space beside the code.
    String note = noting ? gMeshChannelNote : String("Scan or type it");
    while (d.textWidth(note) > 112) note.remove(note.length() - 1, 1);
    d.setTextColor(noting ? amber : muted, bg); d.setCursor(8, 108); d.print(note);
    d.qrcode(text, 124, 23, 112, 1, true);
    memset(text, 0, sizeof(text));
    d.setTextColor(cyan, bg); d.setCursor(8, 127); d.print("V hide  Esc back");
    return;
  }
  if (gMeshChannelEdit != MeshChannelEdit::None) {
    const bool naming = gMeshChannelEdit == MeshChannelEdit::Name;
    d.setTextColor(muted, bg); d.setCursor(8, 62);
    d.print(naming ? "Its name, the same on every radio" : "Its key, as base64 from the app");
    d.drawRect(5, 74, 230, 17, rgb565_local(52, 64, 74));
    String typed = gMeshChannelInput[0] ? String(gMeshChannelInput) : String(naming ? "Family" : "...");
    while (!typed.isEmpty() && d.textWidth(typed) > 220) typed.remove(0, 1);  // The end of a long key stays in view.
    d.setTextColor(gMeshChannelInput[0] ? TFT_WHITE : muted, bg); d.setCursor(10, 79); d.print(typed);
  } else {
    const auto& c = gMeshChannel;
    d.setTextColor(muted, bg); d.setCursor(8, 60); d.print("Name");
    d.setTextColor(c.name[0] ? TFT_WHITE : muted, bg); d.setCursor(44, 60); d.print(c.name[0] ? c.name : "none yet");
    d.setTextColor(muted, bg); d.setCursor(8, 72); d.print("Key");
    d.setTextColor(c.keyLength ? TFT_WHITE : muted, bg); d.setCursor(44, 72);
    d.print(c.keyLength == 32 ? "AES-256, set" : c.keyLength == 16 ? "AES-128, set" : "none yet");
    d.setTextColor(muted, bg);
    d.setCursor(8, 86); d.print("Others add the same name and key");
    d.setCursor(8, 96); d.print("in Meshtastic: Channels, add one.");
  }
  if (noting) { d.setTextColor(amber, bg); d.setCursor(8, 110); d.print(gMeshChannelNote); }
  d.setTextColor(cyan, bg); d.setCursor(8, 127);
  d.print(gMeshChannelEdit != MeshChannelEdit::None ? "Enter save  Esc cancel" : "N name K key G new V show X del");
}
}  // namespace
