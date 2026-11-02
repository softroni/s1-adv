#pragma once
// Reads a rule or a sequence that Claude sent back as a device action. The shape is set by the
// bridge's prompt, and anything it doesn't recognize is refused here.
#include <ArduinoJson.h>
#include "lab.h"

namespace lab {
inline bool ruleFromJson(JsonObjectConst json, Rule& out, const Sequence* seqs, uint8_t seqCount) {
  Draft draft;
  draft.name(json["name"] | "");
  draft.rule.any = json["any"] | true;
  draft.hold(json["hold_ms"] | 2000);
  for (JsonObjectConst line : json["when"].as<JsonArrayConst>()) {
    draft.addCond(line["sensor"] | "", line["test"] | "", line["value"] | 0);
  }
  for (JsonObjectConst line : json["do"].as<JsonArrayConst>()) {
    const char* output = line["output"] | "";
    if (!line["text"].isNull()) draft.addSay(line["text"] | "");
    else if (!line["play"].isNull()) draft.addPlay(line["play"] | "", seqs, seqCount);
    else if (!line["follow"].isNull()) draft.addFollow(output, line["follow"] | "", false);
    else if (!line["oppose"].isNull()) draft.addFollow(output, line["oppose"] | "", true);
    else if (!line["set"].isNull()) draft.addSet(output, line["set"] | 0);
    else draft.ok = false;
  }
  if (!draft.usable()) return false;
  if (!draft.rule.name[0]) strcpy(draft.rule.name, "Spoken rule");
  out = draft.rule;
  return true;
}

// One step the Lab can't run refuses the whole sequence, because with a step missing it isn't the
// one that was asked for. Waits come in milliseconds and are kept in tenths of a second.
inline bool stepFromJson(JsonObjectConst line, SeqStep& step) {
  if (!line["wait_ms"].isNull()) {
    step.kind = SeqKind::Wait;
    const int tenths = (clampTo<int>(line["wait_ms"] | 0, 0, kMaxWaitTenths * 100) + 50) / 100;
    step.value = static_cast<int16_t>(clampTo<int>(tenths, 1, kMaxWaitTenths));
    return stepAllowed(step);
  }
  if (!outFromKey(line["output"] | "", step.out)) return false;
  const OutInfo& o = info(step.out);
  if (line["off"] | false) {
    if (!canTurnOff(step.out)) return false;
    step.kind = o.onOff ? SeqKind::Set : SeqKind::Off;  // The steps on the screen say it the same way.
    step.value = 0;
  } else if (!line["set"].isNull()) {
    step.kind = SeqKind::Set;
    step.value = clampTo<int16_t>(static_cast<int16_t>(clampTo<int>(line["set"] | 0, -32000, 32000)), o.lo, o.hi);
  } else {
    return false;
  }
  return stepAllowed(step);
}
inline bool sequenceFromJson(JsonObjectConst json, Sequence& out) {
  Sequence seq;
  cleanName(json["name"] | "", seq.name);
  seq.loop = json["loop"] | false;
  for (JsonObjectConst line : json["steps"].as<JsonArrayConst>()) {
    if (seq.count >= kMaxSeqSteps || !stepFromJson(line, seq.steps[seq.count])) return false;
    ++seq.count;
  }
  if (!seq.count) return false;
  if (!seq.name[0]) strcpy(seq.name, "New sequence");
  out = seq;
  return true;
}
}  // namespace lab
