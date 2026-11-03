#pragma once
// Reads a rule or a sequence that Claude sent back as a device action. The shape is set by the
// bridge's prompt, and anything it doesn't recognize is refused here.
#include <ArduinoJson.h>
#include <math.h>
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

// One check. Anything it can't do refuses all of it, because half a check is a different question,
// and nothing will look at this one before it runs.
// A number, or a refusal: "| 0" would quietly turn 22.5 or "50" into 0.
inline bool onceNumber(JsonVariantConst value, double& out) {
  if (!value.is<double>()) return false;
  out = value.as<double>();
  return out >= -32000 && out <= 32000;
}
// A test is held to what one reading can say, but not to the range a rule spreads across, because a
// distance can read past it. One look can't tell whether something changed, or hear a name.
inline bool onceCondFromJson(JsonObjectConst line, Cond& cond) {
  const char* test = line["test"] | "";
  if (!sigFromKey(line["sensor"] | "", cond.sig) || cond.sig == Sig::Spoken) return false;
  if (info(cond.sig).onOff) {
    if (strcmp(test, "on") != 0 && strcmp(test, "off") != 0) return false;
    setOnOff(cond, test[1] == 'n');
    return true;
  }
  double value = 0;
  if ((strcmp(test, "under") != 0 && strcmp(test, "over") != 0) || !onceNumber(line["value"], value)) return false;
  // Readings are whole numbers, so over 22.5 is over 22 and under 22.3 is under 23. Rounding would
  // put a reading of 23 on the wrong side of 22.5.
  cond.op = test[0] == 'u' ? Op::Below : Op::Above;
  cond.value = static_cast<int16_t>(cond.op == Op::Above ? floor(value) : ceil(value));
  return true;
}
inline bool onceStepFromJson(JsonObjectConst line, SeqStep& step) {
  if (!outFromKey(line["output"] | "", step.out) || !onceCanDrive(step.out)) return false;
  if (!line["wait_ms"].isNull() || !line["follow"].isNull() || !line["oppose"].isNull()) return false;
  if (line["off"].is<bool>() && line["off"].as<bool>()) {
    step.kind = SeqKind::Off;
    step.value = 0;
    return canTurnOff(step.out);
  }
  double value = 0;
  if (!onceNumber(line["set"], value)) return false;
  step.kind = SeqKind::Set;
  step.value = clampTo<int16_t>(static_cast<int16_t>(lround(value)), info(step.out).lo, info(step.out).hi);
  return true;
}
inline bool onceStepsFromJson(JsonVariantConst lines, SeqStep* steps, uint8_t& count) {
  if (!lines.isNull() && !lines.is<JsonArrayConst>()) return false;
  for (JsonVariantConst line : lines.as<JsonArrayConst>()) {
    if (count >= kMaxActions || !line.is<JsonObjectConst>() || !onceStepFromJson(line.as<JsonObjectConst>(), steps[count])) return false;
    ++count;
  }
  return true;
}
inline bool onceFromJson(JsonObjectConst json, Once& out) {
  Once once;
  once.any = json["any"] | true;
  const JsonVariantConst when = json["when"], read = json["read"];
  if ((!when.isNull() && !when.is<JsonArrayConst>()) || (!read.isNull() && !read.is<JsonArrayConst>())) return false;
  for (JsonVariantConst line : when.as<JsonArrayConst>()) {
    if (once.conds >= kMaxConds || !line.is<JsonObjectConst>() || !onceCondFromJson(line.as<JsonObjectConst>(), once.cond[once.conds])) return false;
    ++once.conds;
  }
  for (JsonVariantConst key : read.as<JsonArrayConst>()) {
    if (once.reads >= kOnceReads || !sigFromKey(key.as<const char*>(), once.read[once.reads])) return false;
    if (once.read[once.reads++] == Sig::Spoken) return false;
  }
  if (!onceStepsFromJson(json["do"], once.then, once.yes) || !onceStepsFromJson(json["else"], once.otherwise, once.no)) return false;
  if (!once.usable()) return false;
  out = once;
  return true;
}
}  // namespace lab
