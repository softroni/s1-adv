#!/usr/bin/env python3
"""Run the voice tool module against a stand-in recorder and the real JSON library."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
part = lambda n: (parts / f'{n:03d}_main.cpp.inc').read_text()
library = root / 'bots/CardputerADV/.pio/libdeps/cardputer_adv/ArduinoJson/src'
joined = ''.join(part(n) for n in range(1, 84) if (parts / f'{n:03d}_main.cpp.inc').exists())


def function(signature):
    start = joined.index(signature)
    while joined.find(';', start) < joined.find('{', start):  # Skip forward declarations.
        start = joined.index(signature, start + 1)
    end, depth = joined.index('{', start) + 1, 1
    while depth:
        depth += (joined[end] == '{') - (joined[end] == '}')
        end += 1
    return joined[start:end]


# The module is only worth anything if every recorder path reaches it.
start_recording = function('void startRecording()')
assert start_recording.index('if (!captureQueued) { failCaptureStart(); return; }') < start_recording.index('voiceToolRecordingStarted();')
assert start_recording.index('voiceToolRecordingStarted();') < start_recording.index('beginVoiceCaptureUpload();'), 'the header needs the tool'
stop = function('bool stopRecordingAndSend()')
assert stop.index('if (!gRecording) {') < stop.index('VoiceToolScope') < stop.index('gRecording = false;'), 'a stop that does nothing must not end a tool'
finish = function('bool finishVoiceSubmission(')
assert finish.index('voiceToolAwaitsAnswer()') < finish.index('pushError'), 'a tool never reaches the chat'
assert 'voiceToolRecordingEnded();' in function('void handleRecordingStorageFailure()')
cancel = function('void cancelRecordingDiscard()')
assert cancel.index('if (!gRecording)') < cancel.index('voiceToolRecordingDropped();')
# Both ways of sending the recording carry the tool's name.
assert part(35).count('headers += voiceToolHeader();') == 1 and part(65).count('headers += voiceToolHeader();') == 1
assert '#include "main_parts/083_main.cpp.inc"' in (src / 'main.cpp').read_text()
assert '#include "voice_tool.h"' in part(1)
# Esc reaches it before the screens, on both key paths.
assert 'if (!voiceToolEscape() && !routeBrainFlowEscape() && !routeLabEscape()) {' in part(49)
keys = part(48)
assert keys.index('if (voiceToolEscape()) {') < keys.index('if (routeBrainFlowEscape()) {')
# Enter sends on a tool's screen, because the recording guard comes before the screens' own keys.
assert keys.index("if (voiceToolOnScreen() && (keys.enter || key == '\\r' || key == '\\n')) toggleVoiceByTap();") < keys.index('handleLabKey(key, keys)')

# The Lab is the first tool, and nothing of the old way of watching the chat is left.
tools = re.search(r'kVoiceTools\[\] = \{(.*?)\n\};', part(83), re.S)[1]
assert '{"lab.rule", labSayOnScreen, labSayAnswered},' in tools
assert 'serviceLabSay' not in joined and 'sayAsked' not in joined

module = part(83).replace('{"lab.rule", labSayOnScreen, labSayAnswered},',
                          '{"lab.rule", onLabScreen, tookAnswer},\n    {"transcribe", onNotesScreen, tookAnswer},')
code = r"""
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <iostream>
#include <string>
#include "voice_tool.h"
struct String : std::string {
  using std::string::string; String() = default; String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); } size_t length() const { return size(); }
};
String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
using VoiceToolAnswer = voice_tool::Answer<String>;
enum class FaceMode { Idle, Error } gFace = FaceMode::Error;
bool gRecording = false, gG0VoicePending = false, gVoiceStartLatched = false, gVoiceCapturePreparing = false; String gStatusText; int gStatusMs = 0, gNoticesDismissed = 0, gCancelled = 0, gRepaints = 0;
uint32_t gLastRenderMs = 1;
struct { void invalidate() { ++gRepaints; } } gUiFramePolicy;
void logf(const char*, ...) {}
void setStatus(const String& text, uint32_t ms) { gStatusText = text; gStatusMs = int(ms); }
void setFaceMode(FaceMode mode) { gFace = mode; }
void dismissVoiceNotice() { ++gNoticesDismissed; }
void cancelRecordingDiscard();
bool labShown = false, notesShown = false; int answers = 0; VoiceToolAnswer last;
bool onLabScreen() { return labShown; }
bool onNotesScreen() { return notesShown; }
void tookAnswer(const VoiceToolAnswer& a) { ++answers; last = a; }
@MODULE@
void cancelRecordingDiscard() { ++gCancelled; gRecording = false; voiceToolRecordingDropped(); }
void listen() { gRecording = true; voiceToolRecordingStarted(); }
void reset() { gVoiceTool = nullptr; gVoiceToolLeft = false; gRecording = labShown = notesShown = gG0VoicePending = gVoiceStartLatched = gVoiceCapturePreparing = false; gCancelled = answers = 0; last = VoiceToolAnswer(); gStatusText = ""; }
VoiceToolAnswer read(bool sent, int code, const char* body) { return voice_tool::readAnswer<String>(sent, code, body, "lab.rule"); }
int main() {
  // The chat is what happens when no tool's screen shows.
  reset(); assert(!voiceToolOnScreen()); listen(); assert(!voiceToolAwaitsAnswer() && voiceToolHeader().empty() && !voiceToolListening());
  voiceToolRecordingEnded(); voiceToolRecordingDropped(); assert(!answers && !voiceToolEscape());

  // On a tool's screen the recording is the tool's, however it was started.
  reset(); labShown = true; assert(voiceToolOnScreen()); listen();
  assert(voiceToolListening() && !voiceToolWorking() && voiceToolAwaitsAnswer() && voiceToolHeader() == "X-Voice-Tool: lab.rule\r\n");
  gRecording = false; assert(voiceToolWorking() && !voiceToolListening());
  const int repaints = gRepaints; gLastRenderMs = 1; gFace = FaceMode::Error;
  assert(finishVoiceToolSubmission(true, 200, R"({"ok":true,"voice_tool":"lab.rule","transcript":"wave red","reply":"Done.","device_actions":[{"type":"lab.rule","name":"Wave"}]})"));
  assert(answers == 1 && last.ok && last.heard == "wave red" && last.reply == "Done." && last.actionsJson == R"([{"type":"lab.rule","name":"Wave"}])");
  assert(!voiceToolAwaitsAnswer() && !voiceToolWorking() && gFace == FaceMode::Idle && gRepaints > repaints && gLastRenderMs == 0);
  assert(voiceToolHeader().empty());  // The next recording starts clean.

  // Each tool gets its own name on the request and hears only its own answer.
  reset(); notesShown = true; listen(); assert(voiceToolHeader() == "X-Voice-Tool: transcribe\r\n"); gRecording = false;
  assert(finishVoiceToolSubmission(true, 200, R"({"ok":true,"voice_tool":"transcribe","transcript":"buy milk"})"));
  assert(answers == 1 && last.ok && last.heard == "buy milk" && last.reply.empty() && last.actionsJson.empty());

  // A failure still reaches the tool, and says why.
  reset(); labShown = true; listen(); gRecording = false; assert(!finishVoiceToolSubmission(false, -1, ""));
  assert(answers == 1 && !last.ok && last.problem == "Couldn't reach the bridge." && gStatusText == last.problem && !voiceToolAwaitsAnswer());

  // Esc while listening cancels, keeps the screen, and sends nothing.
  reset(); labShown = true; listen(); assert(voiceToolEscape()); assert(gCancelled == 1 && !gRecording && !answers && !voiceToolAwaitsAnswer());
  // A second Esc belongs to the screen.
  assert(!voiceToolEscape());
  // Esc while it waits leaves the screen, and what comes back goes nowhere.
  reset(); labShown = true; listen(); gRecording = false; assert(!voiceToolEscape());
  assert(voiceToolWorking());  // The request is still out, so going back to the screen shows it.
  finishVoiceToolSubmission(true, 200, R"({"voice_tool":"lab.rule","transcript":"x"})"); assert(!answers && !voiceToolAwaitsAnswer());
  // That doesn't outlive the one recording.
  listen(); gRecording = false; finishVoiceToolSubmission(true, 200, R"({"voice_tool":"lab.rule"})"); assert(answers == 1);
  // Wandering off isn't asking to drop it, so an answer still arrives from another screen.
  reset(); labShown = true; listen(); gRecording = false; labShown = false; assert(!voiceToolEscape());
  finishVoiceToolSubmission(true, 200, R"({"voice_tool":"lab.rule"})"); assert(answers == 1);

  // While the mic is still waiting to start, the screen can say so and Esc calls it off, or the
  // recording would start later on whatever screen came next and go to the chat.
  reset(); gG0VoicePending = true; assert(!voiceToolStarting() && !voiceToolEscape() && gG0VoicePending);
  reset(); labShown = true; assert(!voiceToolStarting()); gVoiceCapturePreparing = true; assert(voiceToolStarting());
  reset(); labShown = true; gG0VoicePending = gVoiceStartLatched = true; assert(voiceToolStarting() && voiceToolEscape());
  assert(!gG0VoicePending && !gVoiceStartLatched && gStatusText == "Voice cancelled" && !voiceToolStarting());

  // A recording that never reached the bridge frees the tool and gives the recorder's reason.
  reset(); labShown = true; listen(); gRecording = false; setStatus("Voice turn canceled", 1200); voiceToolRecordingEnded();
  assert(answers == 1 && !last.ok && last.problem == "Voice turn canceled" && !voiceToolAwaitsAnswer());
  voiceToolRecordingEnded(); assert(answers == 1);  // The recorder's scope ends after a delivery too.

  // Ten minutes of talk fits, which a fixed small buffer wouldn't.
  { std::string longWords(9000, 'w'); const auto a = read(true, 200, ("{\"voice_tool\":\"lab.rule\",\"transcript\":\"" + longWords + "\"}").c_str());
    assert(a.ok && a.heard == longWords); }

  // What the bridge can say.
  auto problem = [](bool sent, int code, const char* body) { const auto a = read(sent, code, body); assert(!a.ok); return a.problem; };
  assert(problem(false, 200, "") == "Couldn't reach the bridge.");
  assert(problem(true, -1, "") == "Couldn't reach the bridge.");
  assert(problem(true, 422, R"({"error":"could not transcribe speech"})") == "Didn't catch that. Try again.");
  assert(problem(true, 503, R"({"error":"Your Claude plan limit has been reached."})") == "Your Claude plan limit has been reached.");
  assert(problem(true, 503, "<html>") == "The bridge couldn't do that.");
  assert(problem(true, 200, "not json") == "Couldn't read the answer.");
  // An old bridge sends this to the chat, and that has to be said, not shown as an empty answer.
  assert(problem(true, 200, R"({"ok":true,"pending":true,"transcript":"x"})") == "Restart the bridge to use this.");
  assert(problem(true, 200, R"({"voice_tool":"transcribe"})") == "Restart the bridge to use this.");
  std::cout << "PASS voice tools: any way of recording, words or Claude, Esc, failures, and the chat left alone\n";
}
""".replace('@MODULE@', module)

if not (library / 'ArduinoJson.h').exists():
    # CI's firmware job has the library. The checks above don't depend on it.
    print('SKIP voice tools: ArduinoJson not fetched yet')
    raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='s1-voice-tool-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I', str(src), '-I', str(library), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
