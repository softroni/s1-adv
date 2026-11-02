  if (statusCode == 204) {
    http.end();
    setVoiceDiag("tts: empty pcm");
    if (pausedWsForTts) {
      resumeHubWebSocketAfterVoice();
    }
    if (releasedCanvasForTts) {
      restoreUiCanvasAfterNetwork();
    }
    return false;
  }

  if (statusCode != 200) {
    String errorBody = http.getString();
    logf("[TTS] bad response status=%d body=%s", statusCode, errorBody.c_str());
    http.end();
    setVoiceDiag("tts: bad status " + String(statusCode));
    releaseTtsBuffer();
    if (pausedWsForTts) {
      resumeHubWebSocketAfterVoice();
    }
    if (releasedCanvasForTts) {
      restoreUiCanvasAfterNetwork();
    }
    return false;
  }

  fs::FS* fs = activeVoiceFs();
  if (!gStorageReady || !ensureVoiceStorage() || !fs) {
    http.end();
    setVoiceDiag("tts: fs unavailable");
    if (pausedWsForTts) {
      resumeHubWebSocketAfterVoice();
    }
    if (releasedCanvasForTts) {
      restoreUiCanvasAfterNetwork();
    }
    return false;
  }

  uint32_t sampleRate = kTtsDefaultSampleRate;
  String sampleRateHeader = http.header("X-Audio-Sample-Rate");
  if (!sampleRateHeader.isEmpty()) {
    uint32_t parsed = static_cast<uint32_t>(sampleRateHeader.toInt());
    if (parsed >= 8000 && parsed <= 48000) {
      sampleRate = parsed;
    }
  }

  fsRemove(*fs, kActiveTtsPcmPath);
  File pcmFile = fs->open(kActiveTtsPcmPath, "w");
  if (!pcmFile) {
    http.end();
    setVoiceDiag("tts: fs open failed");
    if (pausedWsForTts) {
      resumeHubWebSocketAfterVoice();
    }
    if (releasedCanvasForTts) {
      restoreUiCanvasAfterNetwork();
    }
    return false;
  }

  size_t bytesRead = 0;
  uint8_t ioBuffer[1024];
  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  unsigned long deadline = millis() + 90000;
  while (stream && http.connected() && millis() < deadline &&
         (contentLength < 0 || bytesRead < static_cast<size_t>(contentLength))) {
    int avail = stream->available();
    if (avail <= 0) {
      pumpBlockingNetworkUi();
      continue;
    }
    size_t chunk = static_cast<size_t>(avail);
    if (chunk > sizeof(ioBuffer)) {
      chunk = sizeof(ioBuffer);
    }
    if (contentLength > 0) {
      size_t left = static_cast<size_t>(contentLength) - bytesRead;
      if (chunk > left) {
        chunk = left;
      }
    }
    int got = stream->readBytes(ioBuffer, chunk);
    if (got > 0) {
      size_t written = pcmFile.write(ioBuffer, static_cast<size_t>(got));
      if (written != static_cast<size_t>(got)) {
        pcmFile.close();
        http.end();
        fsRemove(*fs, kActiveTtsPcmPath);
        setVoiceDiag("tts: fs write failed");
        if (pausedWsForTts) {
          resumeHubWebSocketAfterVoice();
        }
        if (releasedCanvasForTts) {
          restoreUiCanvasAfterNetwork();
        }
        return false;
      }
      bytesRead += static_cast<size_t>(got);
      if ((bytesRead % (16 * 1024)) == 0 || (contentLength > 0 && bytesRead == static_cast<size_t>(contentLength))) {
        setVoiceDiag("tts: rx " + String(static_cast<unsigned>(bytesRead / 1024)) + "k");
      }
      pumpBlockingNetworkUi();
    }
  }
  pcmFile.close();
  http.end();

  if (bytesRead == 0) {
    logf("[TTS] empty PCM response");
    setVoiceDiag("tts: empty pcm");
    fsRemove(*fs, kActiveTtsPcmPath);
    if (pausedWsForTts) {
      resumeHubWebSocketAfterVoice();
    }
    if (releasedCanvasForTts) {
      restoreUiCanvasAfterNetwork();
    }
    return false;
  }

  if (contentLength > 0 && bytesRead < static_cast<size_t>(contentLength)) {
    logf("[TTS] short PCM response bytes=%u expected=%u",
         static_cast<unsigned>(bytesRead),
         static_cast<unsigned>(contentLength));
    fsRemove(*fs, kActiveTtsPcmPath);
    setVoiceDiag("tts: short pcm");
    if (pausedWsForTts) {
      resumeHubWebSocketAfterVoice();
    }
    if (releasedCanvasForTts) {
      restoreUiCanvasAfterNetwork();
    }
    return false;
  }

  rememberVoiceNoteFromPcmFile("Assistant voice", speakText, kActiveTtsPcmPath, bytesRead, sampleRate, true);
  bool started = startPlaybackStreamFromRawPcmFile(kActiveTtsPcmPath, sampleRate, bytesRead,
                                                   "tts: file " + String(static_cast<unsigned>(bytesRead / 1024)) + "k");
  if (!started) {
    fsRemove(*fs, kActiveTtsPcmPath);
  }
  if (pausedWsForTts) {
    resumeHubWebSocketAfterVoice();
  }
  if (releasedCanvasForTts) {
    restoreUiCanvasAfterNetwork();
  }
  return started;
}

// Keeps speech on the card for later, when there may be no network, as for a Lab rule that talks.
// The file only gets its name once it's whole, so a dropped connection leaves nothing to play.
bool speechToWavFile(const String& text, const String& path) {
  const String speakText = cleanReplyForDevice(text);
  if (speakText.isEmpty() || !gWifiReady || gHubHost.isEmpty() || !gSdReady) return false;
  waitForInboxToFinish(5000);
  prepareNetworkMemory("tts", true);
  const bool releasedCanvas = releaseUiCanvasForNetwork("voice tool");
  const bool pausedWs = gWsReady;
  if (pausedWs) pauseHubWebSocketForVoice();
  const String partPath = path + ".part";
  size_t bytes = 0;
  bool ok = false;
  {
    HTTPClient http;
    std::unique_ptr<WiFiClientSecure> secureClient;
    const String url = hubBaseUrl() + kDeviceTtsPath;
    http.setTimeout(60000);
    http.setReuse(false);
    const char* responseHeaders[] = {"X-Audio-Sample-Rate"};
    http.collectHeaders(responseHeaders, 1);
    bool opened = false;
    if (hubUsesTls()) {
      secureClient.reset(new WiFiClientSecure());
      configureSecureClient(*secureClient);
      opened = http.begin(*secureClient, url);
    } else {
      opened = http.begin(url);
    }
    if (opened) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-Device-Id", gDeviceId);
      if (!gDeviceToken.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + gDeviceToken);
        http.addHeader("X-Device-Token", gDeviceToken);
      }
      String body = "{\"device_id\":\"" + escapeJsonString(gDeviceId) + "\",\"text\":\"" + escapeJsonString(speakText) + "\"}";
      const int status = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.c_str())), body.length());
      File file = status == 200 ? SD.open(partPath, "w+") : File();
      if (file) {
        const int headerRate = http.header("X-Audio-Sample-Rate").toInt();
        const uint32_t sampleRate = headerRate >= 8000 && headerRate <= 48000 ? headerRate : kTtsDefaultSampleRate;
        uint8_t header[44];
        writeWavHeader(header, 0, sampleRate);
        bool wrote = file.write(header, sizeof(header)) == sizeof(header);
        const int expected = http.getSize();
        WiFiClient* stream = http.getStreamPtr();
        uint8_t buffer[1024];
        const uint32_t deadline = millis() + 30000;
        while (wrote && stream && http.connected() && static_cast<int32_t>(deadline - millis()) > 0 &&
               (expected < 0 || bytes < static_cast<size_t>(expected))) {
          const int available = stream->available();
          if (available <= 0) { pumpBlockingNetworkUi(); continue; }
          size_t chunk = min<size_t>(sizeof(buffer), available);
          if (expected > 0) chunk = min<size_t>(chunk, expected - bytes);
          const int got = stream->readBytes(buffer, chunk);
          if (got <= 0) continue;
          wrote = file.write(buffer, got) == static_cast<size_t>(got);
          bytes += got;
        }
        writeWavHeader(header, bytes, sampleRate);
        ok = wrote && bytes > 0 && !(bytes & 1) && (expected < 0 || bytes == static_cast<size_t>(expected)) &&
             file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
        file.close();
      }
      http.end();
    }
  }
  if (ok) {
    SD.remove(path.c_str());
    ok = SD.rename(partPath.c_str(), path.c_str());
  }
  if (!ok) SD.remove(partPath.c_str());
  logf("[LAB] speech saved=%d bytes=%u", static_cast<int>(ok), static_cast<unsigned>(bytes));
  if (pausedWs) resumeHubWebSocketAfterVoice();
  if (releasedCanvas) restoreUiCanvasAfterNetwork();
  return ok;
}

bool sendTextTurn(const String& text) {
  if ((!gWifiReady && !ensureWifiForHttp("text-turn", 3000)) || gHubHost.isEmpty()) {
    pushError("Hub is not configured.");
    return false;
  }

  HTTPClient http;
  String url = hubBaseUrl() + kDeviceTextTurnPath;
  String turnId = makeClientTurnId("txt");
  http.setTimeout(kTurnHttpTimeoutMs);
  std::unique_ptr<WiFiClientSecure> secureClient;
  bool beginOk = false;
  if (hubUsesTls()) {
    secureClient.reset(new WiFiClientSecure());
    configureSecureClient(*secureClient);
    beginOk = http.begin(*secureClient, url);
  } else {
    beginOk = http.begin(url);
  }
  if (!beginOk) {
    pushError("Failed to open HTTP connection.");
    return false;
  }

  DynamicJsonDocument doc(768);
  doc["message"] = text;
  doc["device_id"] = gDeviceId;
  doc["firmware_version"] = kAppVersion;
  doc["conversation_key"] = currentConversationKey();
  doc["input_type"] = "text";
  doc["reply_audio"] = false;
  doc["save_reply_audio"] = false;
  doc["client_msg_id"] = turnId;
  doc["turn_id"] = turnId;

  String body;
  serializeJson(doc, body);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Client-Msg-Id", turnId);
  http.addHeader("X-Turn-Id", turnId);
  http.addHeader("X-Conversation-Key", currentConversationKey());
  if (!gDeviceToken.isEmpty()) {
    http.addHeader("Authorization", "Bearer " + gDeviceToken);
  }

  gSubmitting = true;
  gThinking = true;
  gAssistantPendingVisible = true;
  setFaceMode(FaceMode::Thinking);
  setStatus("Sending...");
  render();
  gLastRenderMs = millis();
  delay(10);

  int code = http.POST(body);
  String response = http.getString();
  http.end();

  gSubmitting = false;
  gThinking = false;
  gAssistantPendingVisible = false;

  if (code < 200 || code >= 300) {
    logf("[TEXT] hub error code=%d body=%s", code, response.c_str());
    if (code <= 0) {
      String transportError = HTTPClient::errorToString(code);
      pushError("Hub transport failed: " + transportError);
      setHttpDiag("text: " + transportError);
    } else if (code == 504) {
      pushError("Assistant reply timeout.");
      setHttpDiag("text: assistant timeout");
    } else {
      pushError("Hub HTTP error: " + String(code));
      setHttpDiag("text: http " + String(code));
    }
    if (!response.isEmpty()) {
      pushError(response);
    }
    setFaceMode(FaceMode::Error);
    setStatus("Text turn failed", 2000);
    return false;
  }

  DynamicJsonDocument resp(2048);
  auto err = deserializeJson(resp, response);
  if (err) {
    pushError("Could not parse hub response.");
    setFaceMode(FaceMode::Error);
    setStatus("Bad response", 2000);
    return false;
  }
