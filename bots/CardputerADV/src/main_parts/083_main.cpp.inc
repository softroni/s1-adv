// Voice tools: any screen can use the chat's recorder, and get the words or Claude's answer back
// without any of it joining the chat. docs/VOICE-TOOLS.md explains how to add one.
namespace {
struct VoiceTool {
  const char* name;  // "transcribe", or an entry in VOICE_TOOLS in bridge/src/voice-tools.mjs.
  bool (*onScreen)();
  void (*answered)(const VoiceToolAnswer&);
  String (*context)();  // What Claude should know besides the words, or nullptr for nothing.
};

// A line here is all a tool needs. While its screen shows, every way of recording works for it.
const VoiceTool kVoiceTools[] = {
    {"lab.rule", labSayOnScreen, labSayAnswered, labVoiceContext},
    {"lab.change", labChangeOnScreen, labChangeAnswered, labVoiceContext},
    {"lab.ask", labAskOnScreen, labAskAnswered, labVoiceContext},
};

const VoiceTool* gVoiceTool = nullptr;  // Who this recording is for. Nobody means the chat.
bool gVoiceToolLeft = false;            // Esc while it waited, so nobody wants the answer.

const VoiceTool* voiceToolShown() {
  for (const VoiceTool& tool : kVoiceTools) {
    if (tool.onScreen()) return &tool;
  }
  return nullptr;
}

bool voiceToolOnScreen() { return voiceToolShown() != nullptr; }
// From the tap until the microphone is on, which waits for any request already out.
bool voiceToolStarting() { return voiceToolOnScreen() && (gG0VoicePending || gVoiceCapturePreparing); }
bool voiceToolListening() { return gVoiceTool && gRecording; }
bool voiceToolWorking() { return gVoiceTool && !gRecording && !gVoiceToolLeft; }
bool voiceToolAwaitsAnswer() { return gVoiceTool != nullptr; }

// Whatever started the recorder, the screen showing decides where the words go.
void voiceToolRecordingStarted() {
  gVoiceTool = voiceToolShown();
  gVoiceToolLeft = false;
}

String voiceToolHeader() {
  if (!gVoiceTool) return String();
  String header = String("X-Voice-Tool: ") + gVoiceTool->name + "\r\n";
  if (gVoiceTool->context) {
    header += "X-Voice-Context: ";
    header += voice_tool::headerSafe<String>(gVoiceTool->context().c_str());
    header += "\r\n";
  }
  return header;
}

// The recorder is free again, and the screen has to show it.
void voiceToolRelease() {
  gVoiceTool = nullptr;
  gVoiceToolLeft = false;
  dismissVoiceNotice();
  setFaceMode(FaceMode::Idle);
  gUiFramePolicy.invalidate();
  gLastRenderMs = 0;
}

void voiceToolDeliver(const VoiceToolAnswer& answer) {
  const VoiceTool* tool = gVoiceTool;
  const bool left = gVoiceToolLeft;
  voiceToolRelease();
  if (tool && !left) tool->answered(answer);
}

// Called when a recording ends without reaching the bridge. The recorder has already said why.
void voiceToolRecordingEnded() {
  if (!gVoiceTool) return;
  VoiceToolAnswer answer;
  answer.problem = gStatusText;
  voiceToolDeliver(answer);
}

void voiceToolRecordingDropped() {
  if (gVoiceTool) voiceToolRelease();
}

bool finishVoiceToolSubmission(bool transportOk, int statusCode, const String& response) {
  const VoiceToolAnswer answer = voice_tool::readAnswer<String>(transportOk, statusCode, response.c_str(), gVoiceTool->name);
  logf("[VOICE_TOOL] %s http=%d ok=%d chars=%u", gVoiceTool->name, statusCode, static_cast<int>(answer.ok),
       static_cast<unsigned>(answer.heard.length()));
  setStatus(answer.ok ? "Ready" : answer.problem, 1500);
  voiceToolDeliver(answer);
  return answer.ok;
}

// Esc stops whatever is happening and stays on the screen. Once the recording has gone it can't be
// called back, so what comes back is left unread.
bool voiceToolEscape() {
  if (gLauncherVisible) return false;
  if (gG0VoicePending && voiceToolOnScreen()) {
    gG0VoicePending = false;
    gVoiceStartLatched = false;
    setStatus("Voice cancelled", 900);
    return true;
  }
  if (!gVoiceTool || !gVoiceTool->onScreen()) return false;
  if (gRecording) {
    cancelRecordingDiscard();
    setStatus("Cancelled", 900);
    return true;
  }
  if (gVoiceToolLeft) return false;
  gVoiceToolLeft = true;
  gUiFramePolicy.invalidate();
  gLastRenderMs = 0;
  return true;
}
}  // namespace
