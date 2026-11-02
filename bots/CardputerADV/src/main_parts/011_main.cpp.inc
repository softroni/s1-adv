  String out;
  out.reserve(cleaned.length());
  int i = 0;
  while (i < cleaned.length()) {
    uint32_t codepoint = 0;
    int consumed = 1;
    bool ok = decodeUtf8At(cleaned, i, codepoint, consumed);
    if (ok && shouldSuppressEmojiModifier(codepoint)) {
      i += consumed;
      continue;
    }
    if (ok && isEmojiAssetCodepoint(codepoint) && (!keepAvailableEmoji || !emojiAssetAvailable(codepoint))) {
      i += consumed;
      continue;
    }
    for (int n = 0; n < consumed && i + n < cleaned.length(); ++n) {
      out += cleaned[i + n];
    }
    i += consumed;
  }
  out.trim();
  if (out.isEmpty()) {
    out = "Topic";
  }
  return out;
}

float chatTextScale() {
  return kTextSizeScales[min<uint8_t>(gDeviceSettings.textSize, kTextSizeCount - 1)];
}

// Matches the narrowest bubble text area, so a wrapped line is never cut off.
int chatWrapWidth() {
  return 200;
}

std::vector<VisualLine> buildVisualLines(int32_t pixelLimit) {
  std::vector<VisualLine> lines;
  int16_t chatIndex = 0;
  for (const auto& entry : gChatLines) {
    // Side and colour show who is speaking, so only system notices keep a label.
    const bool conversation = entry.kind == LineKind::User || entry.kind == LineKind::Assistant;
    String full = normalizeEmojiDisplayText(entry.text);
    if (!conversation) full = entry.prefix + ": " + full;
    auto wrapped = wrapText(full, pixelLimit);
    for (const auto& part : wrapped) {
      lines.push_back({part, entry.kind, chatIndex});
    }
    chatIndex++;
  }
  if (gAssistantPendingVisible) {
    String dots = ".";
    uint8_t dotCount = 1 + static_cast<uint8_t>((millis() / 350) % 3);
    while (dots.length() < dotCount) {
      dots += '.';
    }
    auto pendingWrapped = wrapText(dots, pixelLimit);
    for (const auto& part : pendingWrapped) {
      lines.push_back({part, LineKind::Assistant, -1});
    }
  }
  return lines;
}

// Line wrapping depends on the font and size, and the canvas may hold another
// screen's settings when this runs outside a chat render.
std::vector<VisualLine> measureChatLines() {
  gCanvas.setFont(&fonts::efontCN_12);
  gCanvas.setTextSize(chatTextScale());
  auto lines = buildVisualLines(chatWrapWidth());
  gCanvas.setTextSize(1);
  return lines;
}

int visibleChatRows(bool showStatus) {
  if (gChatVisibleRows > 0) return gChatVisibleRows;
  return showStatus ? 7 : 8;
}

bool playVisibleVoiceHistoryLine() {
  if (!gInputBuffer.isEmpty() || gChatLines.empty()) {
    return false;
  }
  bool showStatus = !gStatusText.isEmpty() && gStatusText != "Ready";
  int visibleRows = visibleChatRows(showStatus);
  auto lines = measureChatLines();
  int maxOffset = max<int>(0, static_cast<int>(lines.size()) - visibleRows);
  gChatScrollOffset = clampValue(gChatScrollOffset, 0, maxOffset);
  int startIndex = max<int>(0, static_cast<int>(lines.size()) - visibleRows - gChatScrollOffset);
  int endIndex = min<int>(static_cast<int>(lines.size()), startIndex + visibleRows);
  for (int i = startIndex; i < endIndex; ++i) {
    int16_t chatIndex = lines[i].chatIndex;
    if (chatIndex < 0 || chatIndex >= static_cast<int>(gChatLines.size())) {
      continue;
    }
    ChatLine& line = gChatLines[chatIndex];
    if (!line.voicePlayable || line.voiceUrl.isEmpty()) {
      continue;
    }
    String url = line.voiceUrl;
    if (url.startsWith("/")) {
      url = hubBaseUrl() + url;
    }
    String title = line.voiceTitle.isEmpty() ? "history voice" : line.voiceTitle;
    setStatus("Playing voice...", 900);
    return downloadAndPlayResponseAudio(url, "", line.voiceSha256, line.voiceSize, title, line.text);
  }
  return false;
}

void scrollChatBy(int delta) {
  bool showStatus = !gStatusText.isEmpty() && gStatusText != "Ready";
  int visibleRows = visibleChatRows(showStatus);
  auto lines = measureChatLines();
  int maxOffset = static_cast<int>(lines.size()) - visibleRows;
  if (maxOffset < 0) {
    maxOffset = 0;
  }
  gChatScrollOffset = clampValue(gChatScrollOffset + delta, 0, maxOffset);
  setStatus("Scroll " + String(gChatScrollOffset), 600);
}

fs::FS* activeVoiceFs() {
  if (gMeshBusOwned) return nullptr;
  if (gSdReady) {
    return &SD;
  }
  if (gLittleFsReady) {
    return &LittleFS;
  }
  return nullptr;
}

fs::FS* fallbackVoiceFs() {
  if (gSdReady && gLittleFsReady) {
    return &LittleFS;
  }
  return nullptr;
}

String activeVoiceStorageLabel() {
  if (gSdReady) {
    return "sd";
  }
  if (gLittleFsReady) {
    return "flash";
  }
  return "off";
}

bool fsExists(fs::FS& fs, const String& path) {
  return fs.exists(path.c_str());
}

bool fsRemove(fs::FS& fs, const String& path) {
  return fs.remove(path.c_str());
}

bool fsRename(fs::FS& fs, const String& from, const String& to) {
  return fs.rename(from.c_str(), to.c_str());
}

String parentDirOf(const String& path) {
  int slash = path.lastIndexOf('/');
  if (slash <= 0) {
    return "/";
  }
  return path.substring(0, slash);
}

bool ensureDirRecursive(fs::FS& fs, const String& dirPath) {
  if (dirPath.isEmpty() || dirPath == "/" || fsExists(fs, dirPath)) {
    return true;
  }
  int start = 1;
  while (start < dirPath.length()) {
    int slash = dirPath.indexOf('/', start);
    String part = slash < 0 ? dirPath : dirPath.substring(0, slash);
    if (!part.isEmpty() && !fsExists(fs, part) && !fs.mkdir(part)) {
      return false;
    }
    if (slash < 0) {
      break;
    }
    start = slash + 1;
  }
  return fsExists(fs, dirPath) || fs.mkdir(dirPath);
}

bool ensureVoiceStorageOn(fs::FS& fs) {
  if (!fsExists(fs, kVoiceDir)) {
    return fs.mkdir(kVoiceDir);
  }
  return true;
}

bool ensureVoiceStorage() {
  fs::FS* fs = activeVoiceFs();
  if (!fs) {
    return false;
  }
  return ensureVoiceStorageOn(*fs);
}

fs::FS* activeNotesFs() {
  if (gMeshBusOwned) return nullptr;
  if (gSdReady) {
    return &SD;
  }
  if (gLittleFsReady) {
    return &LittleFS;
  }
  return nullptr;
}

String activeNotesStorageLabel() {
  if (gSdReady) {
    return "SD";
  }
  if (gLittleFsReady) {
    return "FLASH";
  }
  return "OFF";
}

bool ensureNotesStorageOn(fs::FS& fs) {
  return ensureDirRecursive(fs, kNotesDir);
}

bool ensureNotesStorage() {
  fs::FS* fs = activeNotesFs();
  if (!fs) {
    return false;
  }
  return ensureNotesStorageOn(*fs);
}

bool ensurePomodoroStorageOn(fs::FS& fs) {
  if (!fsExists(fs, kPomodoroDir) && !fs.mkdir(kPomodoroDir)) {
    return false;
  }
  if (!fsExists(fs, kPomodoroAudioDir) && !fs.mkdir(kPomodoroAudioDir)) {
    return false;
  }
  if (!fsExists(fs, kPomodoroLogsDir) && !fs.mkdir(kPomodoroLogsDir)) {
    return false;
  }
  return true;
}

uint64_t activeStorageTotalBytes() {
  if (gSdReady) {
    return SD.totalBytes();
  }
  if (gLittleFsReady) {
    return LittleFS.totalBytes();
  }
  return 0;
}

uint64_t activeStorageUsedBytes() {
  if (gSdReady) {
    return SD.usedBytes();
  }
  if (gLittleFsReady) {
    return LittleFS.usedBytes();
  }
  return 0;
}

bool isNoteFileName(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".txt") || lower.endsWith(".md") || lower.endsWith(".note");
}

String basenameOfPath(const String& path) {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}
