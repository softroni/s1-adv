import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { buildDeviceActionsPayload, LAB_RULE_POLICY, extractDeviceActionsFromText, sanitizeDeviceAction, trimDeviceEventMessage } from "./server.mjs";
import { LAB_CHAT_POLICY, VOICE_TOOLS } from "./voice-tools.mjs";

const lab = new URL("../../bots/CardputerADV/src/lab.h", import.meta.url);
const firmwareCatalog = new URL("../../bots/CardputerADV/src/main_parts/003_main.cpp.inc", import.meta.url);

test("the prompt names exactly the sensors and outputs the firmware accepts", async () => {
  const header = await readFile(lab, "utf8");
  const table = (name) => {
    const start = header.indexOf(`constexpr ${name}`);
    return [...header.slice(start, header.indexOf("};", start)).matchAll(/^\s*\{"([a-z0-9_]+)",/gmu)].map((match) => match[1]);
  };
  const sensors = table("SigInfo kSigs");
  const outputs = table("OutInfo kOuts");
  assert.equal(sensors.length, 12);
  assert.equal(outputs.length, 14);
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

test("a sequence goes through by name, and stays short", () => {
  const rule = sanitizeDeviceAction({ type: "lab.rule", name: "Door", when: [{ sensor: "motion", test: "on" }],
    do: [{ output: "sequence", play: "Door wave", set: 3 }, { output: "sequence", play: "y".repeat(200) }, { output: "sequence", play: 4 }] });
  assert.deepEqual(rule.do[0], { output: "sequence", play: "Door wave" });
  assert.equal(rule.do[1].play.length, 15);
  assert.deepEqual(rule.do[2], { output: "sequence" });
});

test("the Lab's screens can be opened by name", () => {
  assert.deepEqual(sanitizeDeviceAction({ type: "ui.open", mode: "lab" }), { type: "ui.open", mode: "lab" });
  assert.deepEqual(sanitizeDeviceAction({ type: "ui.open", mode: "rules" }), { type: "ui.open", mode: "rules" });
  assert.deepEqual(sanitizeDeviceAction({ type: "ui.open", mode: "sequences" }), { type: "ui.open", mode: "sequences" });
});

test("the chat can say which rule a change replaces, and only by a short name", () => {
  const rule = { type: "lab.rule", when: [{ sensor: "motion", test: "on" }], do: [{ output: "servo", set: 1 }] };
  assert.equal(sanitizeDeviceAction({ ...rule, replaces: "Wave red" }).replaces, "Wave red");
  assert.equal(sanitizeDeviceAction({ ...rule, replaces: "x".repeat(200) }).replaces.length, 15);
  assert.equal(Object.hasOwn(sanitizeDeviceAction(rule), "replaces"), false, "a new rule says nothing");
});

test("the sequence in the prompt survives the whole trip to the device", () => {
  const start = LAB_CHAT_POLICY.indexOf("[[cardputer_actions:");
  const marker = LAB_CHAT_POLICY.slice(start, LAB_CHAT_POLICY.indexOf("]]] ", start) + 3);
  const { visibleText, actions } = extractDeviceActionsFromText(`Done. ${marker}`);
  assert.equal(visibleText, "Done.");
  assert.deepEqual(actions, [{
    type: "lab.sequence", name: "Door wave", loop: false,
    steps: [{ output: "strip_color", set: 120 }, { wait_ms: 1000 }, { output: "servo", set: 90 }, { wait_ms: 2000 },
      { output: "servo", set: 0 }, { output: "strip_color", off: true }]
  }]);
});

test("a sequence keeps its shape and loses everything else", () => {
  const action = sanitizeDeviceAction({ type: "lab.sequence", name: "n".repeat(99), loop: "yes", replaces: "r".repeat(99), extra: 1,
    steps: [{ output: "servo", set: 1e9, note: "x" }, { wait_ms: 5 }, { wait_ms: 9e9 }, { wait_ms: "soon" }, { output: "led", off: true, set: 3 }, { output: "led", off: "no" }, "junk", null] });
  assert.deepEqual(action, { type: "lab.sequence", name: "n".repeat(15), loop: false, replaces: "r".repeat(15),
    steps: [{ output: "servo", set: 32000 }, { wait_ms: 100 }, { wait_ms: 600000 }, { wait_ms: 1000 }, { output: "led", off: true }, { output: "led" }] });
  assert.equal(sanitizeDeviceAction({ type: "lab.sequence", loop: true, steps: [{ wait_ms: 100 }] }).loop, true);
});

test("a sequence goes whole or not at all", () => {
  const step = { output: "led", set: 1 };
  assert.equal(sanitizeDeviceAction({ type: "lab.sequence", name: "Empty", steps: [] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.sequence", name: "None" }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.sequence", name: "Text", steps: "wave" }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.sequence", name: "Long", steps: Array(17).fill(step) }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.sequence", name: "Full", steps: Array(16).fill(step) }).steps.length, 16);
});

test("the prompt allows only the outputs a sequence step can drive", async () => {
  const header = await readFile(lab, "utf8");
  assert.match(header, /kSeqOutCount = static_cast<uint8_t>\(Out::Say\)/u);
  assert.match(LAB_CHAT_POLICY, /same outputs and values as a rule except say and sequence/u);
  assert.match(LAB_CHAT_POLICY, /A servo can't be turned off/u);
});

test("the chat can't run anything, and is told so", () => {
  assert.match(LAB_CHAT_POLICY, /never say it is saved or running/u);
  assert.match(LAB_CHAT_POLICY, /can't start, stop or delete/u);
  assert.equal(VOICE_TOOLS.has("lab.play"), false);
  assert.equal(sanitizeDeviceAction({ type: "lab.play", name: "Door wave" }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.stop" }), null);
});

test("the bridge and the firmware list the same actions, at the same version", async () => {
  const firmware = await readFile(firmwareCatalog, "utf8");
  const payload = buildDeviceActionsPayload();
  assert.equal(firmware.match(/kDeviceActionCatalogVersion = "([^"]+)"/u)[1], payload.catalog_version);
  const table = firmware.slice(firmware.indexOf("kDeviceActionCatalog[] = {"), firmware.indexOf("};", firmware.indexOf("kDeviceActionCatalog[] = {")));
  const types = [...table.matchAll(/^\s*\{"([a-z_.]+)",/gmu)].map(match => match[1]);
  assert.deepEqual(new Set(types), new Set(payload.actions.map(action => action.type)));
});

test("a long reply loses words, never the action at its end", () => {
  const steps = Array.from({ length: 16 }, (_, i) => (i % 2 ? { wait_ms: 600000 } : { output: "strip_bright", set: 100 }));
  const action = { type: "lab.sequence", name: "Fifteen letters", loop: true, steps };
  const marker = `[[cardputer_actions:${JSON.stringify([action])}]]`;
  const message = { seq: 7, content: { text: `${"word ".repeat(400)}${marker}` } };
  const trimmed = trimDeviceEventMessage(message, 1024);
  assert.equal(trimmed.content.text.length, 1024, "it still fits what the device asked for");
  assert.equal(trimmed.content.truncated, true);
  assert.equal(trimmed.content.original_chars, message.content.text.length);
  assert.ok(trimmed.content.text.endsWith(marker));
  assert.deepEqual(extractDeviceActionsFromText(trimmed.content.text).actions, [action]);
  assert.equal(message.content.text.length, 2000 + marker.length, "the stored reply isn't touched");
});

test("a marker longer than the limit still goes through whole", () => {
  const marker = `[[cardputer_actions:${JSON.stringify([{ type: "lab.sequence", name: "x", steps: Array(16).fill({ wait_ms: 100 }) }])}]]`;
  const trimmed = trimDeviceEventMessage({ content: { text: `Done. ${marker}` } }, 40);
  assert.equal(trimmed.content.text, `Done. ${marker}`, "some words stay, or the chat would show the marker");
  const wordy = trimDeviceEventMessage({ content: { text: `${"word ".repeat(40)}${marker}` } }, 40);
  assert.equal(wordy.content.text, `${"word ".repeat(40).slice(0, 40)}${marker}`);
});

test("a reply with no action is cut where it always was, and a short one isn't touched", () => {
  assert.equal(trimDeviceEventMessage({ content: { text: "y".repeat(3000) } }, 1024).content.text.length, 1024);
  assert.equal(trimDeviceEventMessage({ content: { text: "short" } }, 1024).content.truncated, undefined);
  const broken = "z".repeat(1100) + "[[cardputer_actions:[{";
  assert.equal(trimDeviceEventMessage({ content: { text: broken } }, 1024).content.text.length, 1024, "half a marker is only words");
});
