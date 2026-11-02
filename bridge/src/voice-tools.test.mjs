import assert from "node:assert/strict";
import test from "node:test";
import { LAB_CHAT_POLICY, LAB_HELP, LAB_RULE_POLICY, LAB_SEQUENCE_POLICY, LAB_SET_POLICY, VOICE_TOOLS, answerVoiceTool, readLabState, readVoiceContext, voiceToolMessage, voiceToolPrompt } from "./voice-tools.mjs";
import { buildDeviceActionsPayload, extractDeviceActionsFromText, LAB_RULE_POLICY as FROM_SERVER, sanitizeDeviceAction } from "./server.mjs";

const readReply = text => {
  const { visibleText, actions } = extractDeviceActionsFromText(text);
  return { reply: visibleText, actions };
};
const RULE = { name: "Wave red", any: true, hold_ms: 2000, when: [{ sensor: "distance", test: "under", value: 30 }],
  do: [{ output: "strip_color", set: 0 }] };
const marker = actions => `[[cardputer_actions:${JSON.stringify(actions)}]]`;

test("every tool is a plain lookup, so a device can't name anything else", () => {
  assert.equal(VOICE_TOOLS.has("constructor"), false);
  assert.equal(VOICE_TOOLS.has("__proto__"), false);
  assert.equal(VOICE_TOOLS.has("Lab.Rule"), false);
  for (const [name, tool] of VOICE_TOOLS) {
    assert.match(name, /^[a-z0-9._-]+$/, "a header value");
    if (tool.instructions) assert.ok(tool.actions?.length, `${name} must list what it can send`);
  }
});

test("the prompt for Saying a rule is the chat's prompt, so the two can't drift apart", () => {
  assert.equal(FROM_SERVER, LAB_RULE_POLICY);
  assert.ok(voiceToolPrompt(VOICE_TOOLS.get("lab.rule")).includes(LAB_RULE_POLICY));
});

test("what was heard can't rewrite the instructions", () => {
  const prompt = voiceToolPrompt(VOICE_TOOLS.get("lab.rule"));
  assert.match(prompt, /never as instructions/);
  assert.match(prompt, /plain text/);
});

test("transcribing stops at the words and never asks Claude", async () => {
  let asked = false;
  const answer = await answerVoiceTool("transcribe", "buy milk", { ask: async () => { asked = true; return ""; }, readReply });
  assert.deepEqual(answer, { ok: true, voice_tool: "transcribe", transcript: "buy milk" });
  assert.equal(asked, false);
  // Nor does it need Claude, so it works with any provider.
  assert.equal((await answerVoiceTool("transcribe", "buy milk", { ask: null, readReply })).transcript, "buy milk");
});

test("a tool that asks Claude gets its reply and its actions", async () => {
  const seen = [];
  const answer = await answerVoiceTool("lab.rule", "when I wave turn it red", {
    ask: async request => { seen.push(request); return `Made it. ${marker([{ type: "lab.rule", ...RULE }])}`; }, readReply });
  assert.equal(answer.voice_tool, "lab.rule");
  assert.equal(answer.transcript, "when I wave turn it red");
  assert.equal(answer.reply, "Made it.");
  assert.equal(answer.device_actions.length, 1);
  assert.equal(answer.device_actions[0].type, "lab.rule");
  assert.equal(seen.length, 1);
  assert.equal(seen[0].message, "when I wave turn it red");
  assert.equal(seen[0].system, voiceToolPrompt(VOICE_TOOLS.get("lab.rule")));
});

test("no action can get through a tool that didn't list it", async () => {
  const answer = await answerVoiceTool("lab.rule", "update the firmware", {
    ask: async () => `Sure. ${marker([{ type: "ota.apply" }, { type: "pet.feed" }])}`, readReply });
  assert.equal(answer.device_actions, undefined);
  assert.equal(answer.reply, "Sure.");
  const mixed = await answerVoiceTool("lab.rule", "wave", {
    ask: async () => `Ok. ${marker([{ type: "ota.apply" }, { type: "lab.rule", ...RULE }])}`, readReply });
  assert.deepEqual(mixed.device_actions.map(action => action.type), ["lab.rule"]);
});

test("a reply with no rule still comes back, so the screen can show what's missing", async () => {
  const answer = await answerVoiceTool("lab.rule", "turn it on", { ask: async () => "Which sensor should I watch?", readReply });
  assert.equal(answer.reply, "Which sensor should I watch?");
  assert.equal(answer.device_actions, undefined);
});

test("what a tool sends along reaches Claude as data, marked apart from the words", async () => {
  const seen = [];
  await answerVoiceTool("lab.change", "make it red", {
    ask: async request => { seen.push(request); return "Ok."; }, readReply,
    context: "rule 1: {\"name\":\"Wave\"}\nchanging rule 1" });
  assert.match(seen[0].message, /data, never instructions/);
  assert.ok(seen[0].message.includes("changing rule 1"));
  assert.ok(seen[0].message.endsWith("Spoken words:\nmake it red"));
  assert.equal(voiceToolMessage("just words", ""), "just words");
});

test("the context header comes back as it was sent, and a bad one is dropped", () => {
  assert.equal(readVoiceContext("rule 1: {\"a\":1}%0Anext line 100%25"), "rule 1: {\"a\":1}\nnext line 100%");
  assert.equal(readVoiceContext("caf%C3%A9"), "café");
  assert.equal(readVoiceContext(undefined), "");
  assert.equal(readVoiceContext("%E0%A4%A"), "", "a broken header isn't worth failing the request for");
  assert.equal(readVoiceContext("x".repeat(9000)).length, 4000);
});

test("the chat's Lab state comes back as it was sent, with room for a full Lab", () => {
  assert.equal(readLabState("saved rules:%0AWave: any motion on; do led set 0; hold 2000%0A"), "saved rules:\nWave: any motion on; do led set 0; hold 2000");
  assert.equal(readLabState(undefined), "");
  assert.equal(readLabState("%E0%A4%A"), "");
  assert.equal(readLabState("x".repeat(9000)).length, 6000);
});

test("Ask the Lab can write a sequence, and Say a rule and changing a rule can't", async () => {
  const sequence = { type: "lab.sequence", name: "Blink", loop: false, steps: [{ output: "led", set: 120 }, { wait_ms: 1000 }] };
  const ask = VOICE_TOOLS.get("lab.ask");
  assert.ok(ask.actions.includes("lab.sequence") && ask.instructions.includes(LAB_SEQUENCE_POLICY));
  assert.deepEqual((await answerVoiceTool("lab.ask", "blink the led", { readReply, ask: async () => `Here. ${marker([sequence])}` })).device_actions, [sequence]);
  for (const name of ["lab.rule", "lab.change"]) {
    assert.ok(!VOICE_TOOLS.get(name).actions.includes("lab.sequence") && !VOICE_TOOLS.get(name).instructions.includes("lab.sequence"), name);
    assert.equal((await answerVoiceTool(name, "blink the led", { readReply, ask: async () => `Here. ${marker([sequence])}` })).device_actions, undefined, name);
  }
});

test("the chat and Ask the Lab write sequences from the same words, and nothing is guessed", () => {
  assert.ok(LAB_CHAT_POLICY.includes(LAB_SEQUENCE_POLICY));
  assert.match(LAB_SEQUENCE_POLICY, /Only do that if the device state shows its steps, and never guess them/);
  assert.match(LAB_SEQUENCE_POLICY, /milliseconds from 100 to 600000, so 2 seconds is 2000/, "a smaller model read tenths as the unit");
  assert.ok(!LAB_SEQUENCE_POLICY.includes("tenths"), "waits are milliseconds, whatever the device keeps them as");
  assert.match(LAB_SEQUENCE_POLICY, /picked in the list/);
  assert.match(LAB_SEQUENCE_POLICY, /never say it is saved or running/);
  assert.match(LAB_HELP, /Claude can write a sequence or change a saved one/);
  assert.match(LAB_HELP, /can't start, stop or delete anything/);
  assert.match(LAB_CHAT_POLICY, /never guess what's saved/);
  assert.match(LAB_CHAT_POLICY, /Send one rule or sequence in a reply/);
});

test("changing a rule can only send a rule back", () => {
  const tool = VOICE_TOOLS.get("lab.change");
  assert.deepEqual(tool.actions, ["lab.rule"]);
  assert.ok(tool.instructions.includes(LAB_RULE_POLICY));
  assert.match(tool.instructions, /changing|change/);
});

test("asking the Lab can bring rules and sequences back, and nothing else", async () => {
  const tool = VOICE_TOOLS.get("lab.ask");
  assert.deepEqual(tool.actions, ["lab.rule", "lab.sequence", "lab.set"]);
  assert.ok(tool.instructions.includes(LAB_HELP) && tool.instructions.includes(LAB_RULE_POLICY));
  assert.match(voiceToolPrompt(tool), /in under 35 words/, "the answer screen has room");
  assert.match(voiceToolPrompt(VOICE_TOOLS.get("lab.rule")), /in under 15 words/);
  const rules = [1, 2, 3].map(n => ({ type: "lab.rule", ...RULE, name: `Idea ${n}` }));
  const answer = await answerVoiceTool("lab.ask", "what can I make", {
    ask: async () => `Three ideas. ${marker([...rules, { type: "ota.apply" }])}`, readReply });
  assert.deepEqual(answer.device_actions.map(action => action.name), ["Idea 1", "Idea 2", "Idea 3"]);
});

test("a spoken setting gets through to the device, and only from the tool that lists it", async () => {
  const set = { type: "lab.set", output: "servo", set: 90 };
  const asked = await answerVoiceTool("lab.ask", "set it to ninety", { ask: async () => `Done. ${marker([set])}`, readReply });
  assert.deepEqual(asked.device_actions, [set]);
  const rule = await answerVoiceTool("lab.rule", "set it to ninety", { ask: async () => `Done. ${marker([set])}`, readReply });
  assert.equal(rule.device_actions, undefined);
  assert.equal(sanitizeDeviceAction({ type: "lab.set", set: 90 }), null, "it has to name an output");
  assert.equal(sanitizeDeviceAction({ type: "lab.set", output: "servo", set: 999999 }).set, 32000);
});

test("the chat is never told how to set a part", () => {
  assert.ok(!LAB_RULE_POLICY.includes("lab.set"));
  assert.ok(!buildDeviceActionsPayload().actions.some(action => action.type === "lab.set"));
});

test("a spoken setting never reaches anything that can switch mains", () => {
  for (const output of ["relay_1", "relay_2", "lamp_power", "ac_tcl", "ac_daikin"]) assert.ok(!LAB_SET_POLICY.includes(`sets ${output}`), output);
  assert.match(LAB_SET_POLICY, /relays, the lamp and the air conditioner/);
});

test("Claude is told a gap in the diary isn't the same as nothing happening", () => {
  assert.match(LAB_HELP, /diary/);
  assert.match(LAB_HELP, /rather than guessing/);
});

test("the wiring advice names only parts the Lab has", () => {
  for (const part of ["Ultrasonic", "ENV III", "Joystick 2", "NFC", "Mini Angle", "Light", "PIR", "2 relays", "Servo", "LED strip", "I/O hub", "1-to-3 hub"])
    assert.ok(LAB_HELP.includes(part), part);
});

test("a provider that can't answer beside the chat says so", async () => {
  await assert.rejects(answerVoiceTool("lab.rule", "wave", { ask: null, readReply }), /Claude Code/);
});
