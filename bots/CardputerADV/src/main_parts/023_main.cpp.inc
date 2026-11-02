bool textMatchesQuery(const String& value, const String& query) {
  if (query.isEmpty()) {
    return false;
  }
  String hay = value;
  String needle = query;
  hay.toLowerCase();
  needle.toLowerCase();
  return hay.indexOf(needle) >= 0;
}

bool playAudioAssetByAction(JsonObjectConst action) {
  if (gAudioAssets.empty()) {
    scanAudioAssets();
  }
  if (gAudioAssets.empty()) {
    setStatus("No audio assets", 1200);
    return false;
  }

  int index = -1;
  if (!action["index"].isNull()) {
    int requested = actionInt(action, "index", 0, 0, static_cast<int>(gAudioAssets.size()));
    index = requested > 0 ? requested - 1 : 0;
  }
  String path = action["path"] | "";
  if (index < 0 && !path.isEmpty() && actionLocalPathAllowed(path)) {
    for (int i = 0; i < static_cast<int>(gAudioAssets.size()); ++i) {
      if (gAudioAssets[i].path == path) {
        index = i;
        break;
      }
    }
  }
  String query = action["query"] | "";
  if (index < 0 && !query.isEmpty()) {
    for (int i = 0; i < static_cast<int>(gAudioAssets.size()); ++i) {
      const auto& asset = gAudioAssets[i];
      if (textMatchesQuery(asset.title, query) || textMatchesQuery(asset.path, query) ||
          textMatchesQuery(asset.category, query)) {
        index = i;
        break;
      }
    }
  }
  if (index < 0 || index >= static_cast<int>(gAudioAssets.size())) {
    setStatus("Audio not found", 1200);
    return false;
  }
  gSelectedAudioAsset = index;
  setUiMode(UiMode::Library);
  return startPlaybackStreamFromWavFile(gAudioAssets[index].path, String("asset: ") + gAudioAssets[index].title);
}

bool playVoiceNoteByAction(JsonObjectConst action) {
  if (gVoiceNotes.empty()) {
    loadVoiceNotes();
  }
  if (gVoiceNotes.empty()) {
    setStatus("No voice notes", 1200);
    return false;
  }
  int index = -1;
  if (!action["index"].isNull()) {
    int requested = actionInt(action, "index", 0, 0, static_cast<int>(gVoiceNotes.size()));
    index = requested > 0 ? requested - 1 : 0;
  }
  String query = action["query"] | "";
  if (index < 0 && !query.isEmpty()) {
    for (int i = 0; i < static_cast<int>(gVoiceNotes.size()); ++i) {
      const auto& note = gVoiceNotes[i];
      if (textMatchesQuery(note.title, query) || textMatchesQuery(note.preview, query) ||
          textMatchesQuery(note.audioPath, query)) {
        index = i;
        break;
      }
    }
  }
  if (index < 0 || index >= static_cast<int>(gVoiceNotes.size())) {
    setStatus("Voice note not found", 1200);
    return false;
  }
  gSelectedVoiceNote = index;
  setUiMode(UiMode::Voice);
  return playVoiceNoteAt(index);
}

bool executeDeviceAction(JsonObjectConst action) {
  String type = normalizeActionText(action["type"] | "");
  if (type.isEmpty()) {
    return false;
  }

  if (type == "lab.set") return labVoiceSet(action["output"] | "", action["set"] | 0);
  if (type == "lab.rule") {
    lab::Rule rule;
    if (!lab::ruleFromJson(action, rule)) {
      appendRuntimeLog("LAB", "rule from Claude refused", true);
      return false;
    }
    labOfferRule(rule);
    return true;
  }

