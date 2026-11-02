#pragma once
// What came back for a recording made for a voice tool. The bridge's half is
// bridge/src/voice-tools.mjs, and docs/VOICE-TOOLS.md explains how to add a tool.
#include <ArduinoJson.h>
#include <string.h>

namespace voice_tool {
template <class Text>
struct Answer {
  bool ok = false;
  Text heard;        // The words. A "transcribe" tool gets only these.
  Text reply;        // What Claude said, for a tool that asks it.
  Text actionsJson;  // Device actions from Claude as a JSON array, or empty.
  Text problem;      // Why ok is false, short enough for a small screen.
};

// A header can't hold a line break, so anything out of the ordinary travels percent-encoded.
template <class Text>
Text headerSafe(const char* raw) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  const auto plain = [](unsigned char c) { return c >= 0x20 && c <= 0x7E && c != '%'; };
  Text out;
  if (!raw) return out;  // What a String's c_str() gives when it ran out of memory.
  size_t length = 0;
  for (const char* c = raw; *c; ++c) length += plain(static_cast<unsigned char>(*c)) ? 1 : 3;
  out.reserve(length);  // In one piece, so this can't scatter the heap.
  for (; *raw; ++raw) {
    const unsigned char c = static_cast<unsigned char>(*raw);
    if (plain(c)) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  // Nothing beats part of it, which Claude would read as the whole list.
  return out.length() == length ? out : Text();
}

template <class Text>
Answer<Text> readAnswer(bool delivered, int status, const char* body, const char* tool) {
  Answer<Text> answer;
  if (!delivered || status <= 0) {
    answer.problem = "Couldn't reach the bridge.";
    return answer;
  }
  if (status == 422) {
    answer.problem = "Didn't catch that. Try again.";
    return answer;
  }
  DynamicJsonDocument doc(strlen(body) + 2048);  // Long dictation is what "transcribe" is for.
  const bool readable = !deserializeJson(doc, body);
  if (status < 200 || status >= 300) {
    const char* said = readable ? (doc["error"] | "") : "";
    if (strcmp(said, "unknown_voice_tool") == 0) answer.problem = "Restart the bridge to use this.";
    else if (*said) answer.problem = said;
    else answer.problem = "The bridge couldn't do that.";
    return answer;
  }
  if (!readable) {
    answer.problem = "Couldn't read the answer.";
    return answer;
  }
  // A bridge from before voice tools ignores the header and treats this as a chat message.
  if (strcmp(doc["voice_tool"] | "", tool) != 0) {
    answer.problem = "Restart the bridge to use this.";
    return answer;
  }
  answer.ok = true;
  answer.heard = doc["transcript"] | "";
  answer.reply = doc["reply"] | "";
  if (!doc["device_actions"].isNull()) serializeJson(doc["device_actions"], answer.actionsJson);
  return answer;
}
}  // namespace voice_tool
