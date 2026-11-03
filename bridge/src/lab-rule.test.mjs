import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { buildDeviceActionsPayload, LAB_RULE_POLICY, extractDeviceActionsFromText, sanitizeDeviceAction, trimDeviceEventMessage } from "./server.mjs";
import { LAB_CHAT_POLICY, LAB_LIVE_CHAT_POLICY, LAB_LIVE_ONCE_POLICY, LAB_ONCE_POLICY, LAB_PLAY_POLICY, VOICE_TOOLS } from "./voice-tools.mjs";

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

test("the chat can't stop, start rules or delete anything, and is told so", () => {
  assert.match(LAB_CHAT_POLICY, /never say it is saved or running/u);
  assert.match(LAB_CHAT_POLICY, /can't stop a sequence, start or stop rules, or delete rules or sequences/u);
  assert.match(LAB_LIVE_CHAT_POLICY, /can't start, stop or delete rules or sequences/u);
  assert.equal(sanitizeDeviceAction({ type: "lab.stop" }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.delete", name: "Door wave" }), null);
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

test("a one-time check keeps only its own fields", () => {
  assert.deepEqual(sanitizeDeviceAction({
    type: "lab.once", any: false, extra: 1, name: "dropped", hold_ms: 5,
    read: ["knob", 7, "tilt"], when: [{ sensor: "knob", test: "over", value: 99999, extra: true }],
    do: [{ output: "strip_color", set: 0, follow: "knob" }], else: [{ output: "led", off: true }]
  }), {
    type: "lab.once", any: false, read: ["knob", "tilt"], when: [{ sensor: "knob", test: "over", value: 32000 }],
    do: [{ output: "strip_color", set: 0 }], else: [{ output: "led", off: true }]
  });
});

test("a check can be a reading, an action or both, and has to be something", () => {
  assert.deepEqual(sanitizeDeviceAction({ type: "lab.once", read: ["humidity"] }).read, ["humidity"]);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", do: [{ output: "led", set: 120 }] }).do.length, 1);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", when: [{ sensor: "motion", test: "on" }] }).any, true);
  assert.equal(sanitizeDeviceAction({ type: "lab.once" }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", read: [], when: [], do: [] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", read: "knob" }), null);
});

test("a check with a line too many is refused rather than cut", () => {
  const led = { output: "led", set: 1 };
  assert.equal(sanitizeDeviceAction({ type: "lab.once", do: [led, led, led] }).do.length, 3);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", do: [led, led, led, led] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", read: ["a", "b", "c", "d"] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", read: ["knob"], else: [led, led, led, led] }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.once", read: ["knob"], when: Array(4).fill({ sensor: "knob", test: "on" }) }), null);
});

test("every example the prompt gives for a check is one the bridge passes", () => {
  const markers = [...LAB_ONCE_POLICY.matchAll(/\[\[cardputer_actions:(.*?)\]\]\]/gu)];
  assert.equal(markers.length, 3);
  for (const marker of markers) {
    const { actions } = extractDeviceActionsFromText(`Checking. ${marker[0]}`);
    assert.equal(actions.length, 1);
    assert.equal(actions[0].type, "lab.once");
  }
});

test("the chat and Ask the Lab both know about a check, and it never reports for the device", () => {
  assert.ok(LAB_CHAT_POLICY.includes(LAB_ONCE_POLICY));
  assert.ok(VOICE_TOOLS.get("lab.ask").actions.includes("lab.once"));
  assert.ok(VOICE_TOOLS.get("lab.ask").instructions.includes(LAB_ONCE_POLICY));
  assert.ok(!VOICE_TOOLS.get("lab.rule").actions.includes("lab.once"), "saying a rule is still only a rule");
  assert.match(LAB_ONCE_POLICY, /never a number, a result or that anything changed/u);
  assert.match(LAB_ONCE_POLICY, /An if means one time/u);
  assert.doesNotMatch(LAB_CHAT_POLICY, /tell the owner to tap Alt on a Lab screen and ask there\. To change/u);
});

test("the outputs a check is told about are the ones the firmware lets it drive", async () => {
  const header = await readFile(lab, "utf8");
  assert.match(header, /onceCanDrive\(Out out\) \{ return out == Out::Servo \|\| isStrip\(out\) \|\| out == Out::Led \|\| out == Out::Tone; \}/u);
  assert.match(LAB_ONCE_POLICY, /A do or else line is servo, strip_color, strip_bright, strip_length, led or speaker,/u);
});

test("a live voice session isn't offered a check, because the device can't run one then", async () => {
  assert.ok(!LAB_LIVE_CHAT_POLICY.includes("lab.once action") && !LAB_LIVE_CHAT_POLICY.includes(LAB_ONCE_POLICY));
  assert.ok(LAB_LIVE_CHAT_POLICY.includes(LAB_LIVE_ONCE_POLICY));
  assert.equal(LAB_CHAT_POLICY.replace(LAB_ONCE_POLICY, "").length > 0, true);
  const server = await readFile(new URL("./server.mjs", import.meta.url), "utf8");
  const realtime = server.slice(server.indexOf("function realtimeInstructions("), server.indexOf("function realtimeInstructions(") + 2000);
  assert.match(realtime, /buildDeviceActionPolicyText\(\{ live: true \}\)/u);
  assert.match(server, /live \? DEVICE_ACTION_CATALOG\.filter\(\(action\) => action\.type !== "lab\.once" && action\.type !== "lab\.play"\)/u);
});

test("the check example only has the parts its words ask for", () => {
  const markers = [...LAB_ONCE_POLICY.matchAll(/\[\[cardputer_actions:(.*?)\]\]\]/gu)].map((match) => JSON.parse(`${match[1]}]`));
  assert.ok(markers.every(([action]) => !Object.hasOwn(action, "else")), "an else nobody asked for would act on its own");
});

test("a check's numbers stay numbers and keep their fractions, and anything else is dropped", () => {
  const action = sanitizeDeviceAction({ type: "lab.once", when: [{ sensor: "temperature", test: "over", value: 22.5 }, { sensor: "knob", test: "over", value: "50" }],
    do: [{ output: "servo", set: 90.5 }, { output: "led", set: "120" }, { output: "strip_color", set: true }] });
  assert.equal(action.when[0].value, 22.5, "the device rounds it toward the side that keeps its meaning");
  assert.equal(Object.hasOwn(action.when[1], "value"), false, "a string isn't a number, so the device refuses it");
  assert.equal(action.do[0].set, 90.5);
  assert.equal(Object.hasOwn(action.do[1], "set"), false);
  assert.equal(Object.hasOwn(action.do[2], "set"), false);
});

test("playing a saved sequence takes only its name, and needs one", () => {
  assert.deepEqual(sanitizeDeviceAction({ type: "lab.play", name: "Happy birthday", steps: [1], loop: true }), { type: "lab.play", name: "Happy birthday" });
  assert.equal(sanitizeDeviceAction({ type: "lab.play", name: "x".repeat(40) }).name.length, 15);
  assert.equal(sanitizeDeviceAction({ type: "lab.play" }), null);
  assert.equal(sanitizeDeviceAction({ type: "lab.play", name: "" }), null);
});

test("the chat and Ask the Lab can play a sequence, and a live voice session can't", () => {
  const markers = [...LAB_PLAY_POLICY.matchAll(/\[\[cardputer_actions:(.*?)\]\]\]/gu)];
  assert.equal(markers.length, 1);
  assert.deepEqual(extractDeviceActionsFromText(`Playing it. ${markers[0][0]}`).actions, [{ type: "lab.play", name: "Happy birthday" }]);
  assert.ok(LAB_CHAT_POLICY.includes(LAB_PLAY_POLICY) && !LAB_LIVE_CHAT_POLICY.includes(LAB_PLAY_POLICY));
  assert.ok(VOICE_TOOLS.get("lab.ask").actions.includes("lab.play") && VOICE_TOOLS.get("lab.ask").instructions.includes(LAB_PLAY_POLICY));
  assert.match(LAB_PLAY_POLICY, /only plays when the owner presses P/u);
});

test("the outputs the prompt says wait for P are the ones the firmware holds back", async () => {
  const header = await readFile(lab, "utf8");
  const start = header.indexOf("constexpr bool switchesMains(Out out)");
  const named = [...header.slice(start, header.indexOf("}", start)).matchAll(/Out::(\w+)/gu)].map((m) => m[1]);
  assert.deepEqual(named, ["Relay1", "Relay2", "LampPower", "LampBright", "AcTcl", "AcDaikin"]);
  assert.match(LAB_PLAY_POLICY, /relay_1, relay_2, lamp_power, lamp_bright, ac_tcl or ac_daikin/u);
});
