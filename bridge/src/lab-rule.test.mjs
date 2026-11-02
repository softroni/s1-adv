import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { LAB_RULE_POLICY, extractDeviceActionsFromText, sanitizeDeviceAction } from "./server.mjs";

const lab = new URL("../../bots/CardputerADV/src/lab.h", import.meta.url);

test("the prompt names exactly the sensors and outputs the firmware accepts", async () => {
  const header = await readFile(lab, "utf8");
  const table = (name) => {
    const start = header.indexOf(`constexpr ${name}`);
    return [...header.slice(start, header.indexOf("};", start)).matchAll(/^\s*\{"([a-z0-9_]+)",/gmu)].map((match) => match[1]);
  };
  const sensors = table("SigInfo kSigs");
  const outputs = table("OutInfo kOuts");
  assert.equal(sensors.length, 12);
  assert.equal(outputs.length, 13);
  for (const key of [...sensors, ...outputs]) {
    assert.match(LAB_RULE_POLICY, new RegExp(`\\b${key}\\b`, "u"), key);
  }
  const named = new Set([...LAB_RULE_POLICY.split(/Sensors:|Outputs with their set values:/u).slice(1).join(" ").matchAll(/\b[a-z]+_[a-z0-9]+\b/gu)].map((match) => match[0]));
  for (const word of named) {
    if (word === "hold_ms") continue;
    assert.ok(sensors.includes(word) || outputs.includes(word), `the firmware has no ${word}`);
  }
});

test("the example in the prompt survives the whole trip to the device", () => {
  const marker = LAB_RULE_POLICY.slice(LAB_RULE_POLICY.indexOf("[[cardputer_actions:"), LAB_RULE_POLICY.indexOf("]]] ") + 3);
  const { visibleText, actions } = extractDeviceActionsFromText(`Done. ${marker}`);
  assert.equal(visibleText, "Done.");
  assert.equal(actions.length, 1);
  assert.deepEqual(actions[0], {
    type: "lab.rule", name: "Wave red", any: true, hold_ms: 2000,
    when: [{ sensor: "distance", test: "under", value: 30 }],
    do: [{ output: "strip_color", set: 0 }, { output: "servo", follow: "knob" }]
  });
});

test("a rule keeps its shape and loses everything else", () => {
  const rule = sanitizeDeviceAction({
    type: "lab.rule", name: "This name goes on far too long", any: false, hold_ms: 999999, extra: "dropped",
    when: [{ sensor: "motion", test: "on", junk: 1 }, "not a line", null, { sensor: "knob", test: "changes" },
           { sensor: "light", test: "under", value: 99999 }, { sensor: "humidity", test: "over", value: 5 }],
    do: [{ output: "servo", oppose: "knob" }, { output: "relay_1", set: 1, follow: "" }, { output: "led", set: -99999 }, { output: "speaker", set: 1 }]
  });
  assert.equal(rule.name, "This name goes ");
  assert.equal(rule.any, false);
  assert.equal(rule.hold_ms, 30000);
  assert.equal(rule.extra, undefined);
  assert.deepEqual(rule.when, [{ sensor: "motion", test: "on" }, { sensor: "knob", test: "changes" }, { sensor: "light", test: "under", value: 32000 }]);
  assert.deepEqual(rule.do, [{ output: "servo", oppose: "knob" }, { output: "relay_1", set: 1 }, { output: "led", set: -32000 }]);
});

test("a rule with nothing to watch or nothing to do goes no further", () => {
  assert.equal(sanitizeDeviceAction({ type: "lab.rule", when: [], do: [{ output: "servo", set: 1 }] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.rule", when: [{ sensor: "knob", test: "changes" }] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.rule", when: "knob", do: {} }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.delete_everything" }), null);
});

test("a phrase for a rule to speak gets through, and stays short", () => {
  const rule = sanitizeDeviceAction({ type: "lab.rule", name: "Welcome", when: [{ sensor: "motion", test: "on" }],
    do: [{ output: "say", text: "welcome back" }, { output: "say", text: "x".repeat(200) }] });
  assert.deepEqual(rule.do[0], { output: "say", text: "welcome back" });
  assert.equal(rule.do[1].text.length, 40);
});

test("the Lab's screens can be opened by name", () => {
  assert.deepEqual(sanitizeDeviceAction({ type: "ui.open", mode: "lab" }), { type: "ui.open", mode: "lab" });
  assert.deepEqual(sanitizeDeviceAction({ type: "ui.open", mode: "rules" }), { type: "ui.open", mode: "rules" });
});
