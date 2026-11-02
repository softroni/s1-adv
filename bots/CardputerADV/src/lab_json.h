#pragma once
// Reads a rule that Claude sent back as a device action. The shape is set by the bridge's prompt,
// and lab::Draft refuses anything it doesn't recognize.
#include <ArduinoJson.h>
#include "lab.h"

namespace lab {
inline bool ruleFromJson(JsonObjectConst json, Rule& out) {
  Draft draft;
  draft.name(json["name"] | "");
  draft.rule.any = json["any"] | true;
  draft.hold(json["hold_ms"] | 2000);
  for (JsonObjectConst line : json["when"].as<JsonArrayConst>()) {
    draft.addCond(line["sensor"] | "", line["test"] | "", line["value"] | 0);
  }
  for (JsonObjectConst line : json["do"].as<JsonArrayConst>()) {
    const char* output = line["output"] | "";
    if (!line["follow"].isNull()) draft.addFollow(output, line["follow"] | "", false);
    else if (!line["oppose"].isNull()) draft.addFollow(output, line["oppose"] | "", true);
    else if (!line["set"].isNull()) draft.addSet(output, line["set"] | 0);
    else draft.ok = false;
  }
  if (!draft.usable()) return false;
  if (!draft.rule.name[0]) strcpy(draft.rule.name, "Spoken rule");
  out = draft.rule;
  return true;
}
}  // namespace lab
