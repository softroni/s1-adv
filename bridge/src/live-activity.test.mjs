import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { chmod, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createServer as createHttpServer } from "node:http";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { setTimeout as sleep } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import { ActivityReader, activityFromEvent, shortDetail } from "./claude-activity.mjs";
import { ClaudeCodeClient, claudeTurnArgs, claudeCodeSettings } from "./claude-code-client.mjs";
import { heldPollMs, textReplyWaitMs, voiceReplyWaitMs, MAX_HELD_POLL_MS } from "./turn-delivery-policy.mjs";

const bridgeRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const TOKEN = "dtk_live_activity_test_only_0000000000000000000000001";
const SESSION = "11111111-1111-4111-8111-111111111111";
const block = (type, extra = {}) => ({ type: "stream_event", event: { type: "content_block_start", content_block: { type, ...extra } } });
const tool = (name, input) => ({ type: "assistant", message: { content: [{ type: "tool_use", name, input }] } });
const toolDone = { type: "user", message: { content: [{ type: "tool_result", content: "ok" }] } };
const result = text => ({ type: "result", subtype: "success", is_error: false, result: text, session_id: SESSION });

test("each tool maps to the status of what Claude is really doing", () => {
  const state = (name, input) => activityFromEvent(tool(name, input))?.state;
  assert.equal(state("WebSearch", { query: "esp32" }), "searching");
  assert.equal(state("WebFetch", { url: "https://example.com/a" }), "researching");
  for (const name of ["Read", "Glob", "Grep"]) assert.equal(state(name, {}), "reading");
  for (const name of ["Edit", "Write"]) assert.equal(state(name, {}), "coding");
  assert.equal(state("Bash", { command: "ls -la" }), "working");
  assert.equal(state("Bash", { command: ".venv/bin/python scripts/cardputer.py build" }), "building");
  assert.equal(state("Bash", { command: "npm run build" }), "building");
  assert.equal(state("Bash", { command: "python3 scripts/test_snake.py" }), "testing");
  assert.equal(state("Bash", { command: "npm run test:security" }), "testing");
  assert.equal(state("Bash", { command: "node --test src/a.test.mjs" }), "testing");
  assert.equal(state("SomethingNew", {}), "working");
  // A tool shows the moment it starts, before its input has streamed.
  assert.deepEqual(activityFromEvent(block("tool_use", { name: "WebSearch" })), { state: "searching", detail: "" });
  assert.equal(activityFromEvent(block("text")).state, "writing");
  assert.equal(activityFromEvent(block("thinking")).state, "thinking");
  assert.equal(activityFromEvent(toolDone).state, "thinking");
  for (const quiet of [{ type: "system", subtype: "init" }, { type: "rate_limit_event" }, result("x"), null, {},
    { type: "assistant", message: { content: [{ type: "text", text: "hi" }] } },
    { type: "user", message: { content: "plain" } }]) assert.equal(activityFromEvent(quiet), null);
});

test("the detail is short and never carries a command, path or URL path", () => {
  const detail = (name, input) => activityFromEvent(tool(name, input)).detail;
  assert.equal(detail("WebSearch", { query: "esp32 tls heap" }), "esp32 tls heap");
  assert.equal(detail("WebFetch", { url: "https://docs.example.com/a/b?token=secret" }), "docs.example.com");
  assert.equal(detail("WebFetch", { url: "not a url" }), "");
  assert.equal(detail("Read", { file_path: "/Users/me/dev/s1-adv/.env" }), ".env");
  assert.equal(detail("Edit", { file_path: "/a/b/activity_cue.h" }), "activity_cue.h");
  assert.equal(detail("Bash", { command: "curl -H 'Authorization: Bearer abc123' x" }), "");
  assert.equal(detail("Bash", { command: "curl -H 'Authorization: Bearer abc123' x", description: "Check the API" }), "Check the API");
  assert.equal(shortDetail("a\n\tb\u0000c‮d"), "a b c d");
  assert.equal(Array.from(shortDetail("я".repeat(200))).length, 48);
  assert.equal(shortDetail(42), "");
});

test("a tool finishing beside a slower one doesn't look like thinking", () => {
  const reader = new ActivityReader();
  const use = (id, name, input) => ({ type: "assistant", message: { content: [{ type: "tool_use", id, name, input }] } });
  const done = id => ({ type: "user", message: { content: [{ type: "tool_result", tool_use_id: id }] } });
  assert.equal(reader.read(use("a", "WebSearch", { query: "slow" })).state, "searching");
  assert.equal(reader.read(use("b", "Read", { file_path: "/x/fast.h" })).state, "reading");
  assert.equal(reader.read(done("b")), null, "the search is still running");
  assert.equal(reader.read(done("a")).state, "thinking");
  assert.equal(reader.read(done("unknown")).state, "thinking");
});

test("the bridge streams Claude's output", () => {
  const args = claudeTurnArgs(claudeCodeSettings({ upstream: {} }));
  assert.equal(args[args.indexOf("--output-format") + 1], "stream-json");
  assert.ok(args.includes("--verbose") && args.includes("--include-partial-messages"));
});

test("turn waits only shorten for firmware that asked, and a held poll stays under the device timeout", () => {
  assert.equal(textReplyWaitMs("1000", 60000, true), 1000);
  assert.equal(textReplyWaitMs("1000", 60000, false), 60000);
  assert.equal(textReplyWaitMs(undefined, 60000, true), 60000);
  assert.equal(textReplyWaitMs("junk", 60000, true), 60000);
  assert.equal(textReplyWaitMs("1", 60000, true), 250);
  assert.equal(textReplyWaitMs("999999", 60000, true), 60000);
  assert.equal(voiceReplyWaitMs("1000", 10000, "claude-code"), 10000);
  assert.equal(voiceReplyWaitMs("1000", 10000, "claude-code", true), 1000);
  assert.equal(heldPollMs("2000"), 2000);
  assert.equal(heldPollMs("999999"), MAX_HELD_POLL_MS);
  assert.ok(MAX_HELD_POLL_MS < 3500, "the device reads for 4 s");
  assert.equal(heldPollMs(null), 0);
  assert.equal(heldPollMs("-5"), 0);
  assert.equal(heldPollMs("junk"), 0);
});

const FAKE = `#!${process.execPath}
const fs = require("fs"), path = require("path");
const argv = process.argv.slice(2);
if (argv[0] === "auth") {
  process.stdout.write(JSON.stringify({ loggedIn: true, authMethod: "claude.ai", apiProvider: "firstParty", subscriptionType: "max" }));
} else {
  process.stdin.resume().on("end", () => {
    const lines = JSON.parse(fs.readFileSync(path.join(__dirname, "script.json"), "utf8"));
    let at = 0;
    const next = () => {
      if (at >= lines.length) return;
      const { after = 0, line } = lines[at++];
      setTimeout(() => { process.stdout.write(typeof line === "string" ? line : JSON.stringify(line) + "\\n"); next(); }, after);
    };
    next();
  });
}
`;

async function makeFake(t) {
  const dir = await mkdtemp(path.join(tmpdir(), "s1-live-activity-"));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const command = path.join(dir, "claude");
  await writeFile(command, FAKE);
  await chmod(command, 0o755);
  return { dir, command, script: lines => writeFile(path.join(dir, "script.json"), JSON.stringify(lines)) };
}

test("a client reports each change once, in order, and still returns the reply", async t => {
  const fake = await makeFake(t);
  await fake.script([
    { line: block("thinking") },
    { line: block("tool_use", { name: "WebSearch" }) },
    { line: tool("WebSearch", { query: "esp32" }) },
    { line: toolDone },
    { line: block("tool_use", { name: "Read" }) },
    { line: "not json at all\n" },
    { line: tool("Read", { file_path: "/x/main.cpp" }) },
    { line: toolDone },
    { line: block("text") },
    { line: result("All done") }
  ]);
  const client = new ClaudeCodeClient({ stateDir: fake.dir, upstream: { provider: "claude-code", claudeCode: { command: fake.command } } }, { retryDelayMs: 1 });
  await client.start();
  const seen = [], replies = [];
  client.on("activity", frame => seen.push(frame.activity));
  client.on("event", frame => replies.push(frame.payload.message.content[0].text));
  await client.rpc("sessions.send", { key: "k", message: "go", idempotencyKey: "run-1" });
  while (client.runReceipt("run-1").status === "timeout") await sleep(5);
  assert.deepEqual(seen.map(item => item && `${item.state}:${item.detail}`), ["thinking:", "searching:", "searching:esp32",
    "thinking:", "reading:", "reading:main.cpp", "thinking:", "writing:", null]);
  assert.deepEqual(replies, ["All done"]);
  assert.equal(client.runReceipt("run-1").status, "ok");
});

test("one enormous line is dropped without losing the turn, and a long run has no total limit", async t => {
  const fake = await makeFake(t);
  const huge = JSON.stringify(toolDone).slice(0, -1) + `,"pad":"${"x".repeat(9 * 1024 * 1024)}"}\n`;
  const many = Array.from({ length: 1200 }, () => ({ line: JSON.stringify({ type: "stream_event", pad: "y".repeat(8192) }) + "\n" }));
  await fake.script([{ line: huge }, ...many, { line: result("Survived") }]);
  const client = new ClaudeCodeClient({ stateDir: fake.dir, upstream: { provider: "claude-code", claudeCode: { command: fake.command } } }, { retryDelayMs: 1 });
  await client.start();
  const replies = [];
  client.on("event", frame => replies.push(frame.payload.message.content[0].text));
  await client.rpc("sessions.send", { key: "k", message: "go", idempotencyKey: "run-2" });
  while (client.runReceipt("run-2").status === "timeout") await sleep(10);
  assert.deepEqual(replies, ["Survived"]);
});

test("no result line still fails cleanly", async t => {
  const fake = await makeFake(t);
  await fake.script([{ line: block("thinking") }]);
  const client = new ClaudeCodeClient({ stateDir: fake.dir, upstream: { provider: "claude-code", claudeCode: { command: fake.command } } }, { retryDelayMs: 1 });
  await client.start();
  const seen = [];
  client.on("activity", frame => seen.push(frame.activity));
  await client.rpc("sessions.send", { key: "k", message: "go", idempotencyKey: "run-3" });
  while (client.runReceipt("run-3").status === "timeout") await sleep(5);
  assert.equal(client.runReceipt("run-3").status, "error");
  assert.equal(seen.at(-1), null, "a failed turn must not leave a status showing");
});

async function freePort() {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const { port } = probe.address();
  await new Promise(resolve => probe.close(resolve));
  return port;
}

// The real server process, spoken to over HTTP the way the firmware does.
async function startBridge(t, fake, extra = {}) {
  const config = JSON.parse(await readFile(path.join(bridgeRoot, "config.smoke.json"), "utf8"));
  const port = await freePort();
  Object.assign(config, {
    server: { host: "127.0.0.1", port }, stateDir: path.join(fake.dir, "state"),
    upstream: { provider: "claude-code", claudeCode: { command: fake.command, turnTimeoutMs: 30000 } },
    omnibotShim: { enabled: true, sttProvider: "none", turnWaitMs: 60000, turnPollMs: 20 }, ...extra
  });
  config.devices[0].device_token = TOKEN;
  config.security.inboxInjectEnabled = true;
  const configPath = path.join(fake.dir, "config.json");
  await writeFile(configPath, JSON.stringify(config), { mode: 0o600 });
  const child = spawn(process.execPath, ["src/server.mjs"], { cwd: bridgeRoot,
    env: { ...process.env, CARDPUTER_BRIDGE_CONFIG: configPath }, stdio: ["ignore", "pipe", "pipe"] });
  let output = "";
  child.stdout.on("data", chunk => { output += chunk; });
  child.stderr.on("data", chunk => { output += chunk; });
  t.after(() => { child.kill("SIGKILL"); });
  const base = `http://127.0.0.1:${port}`;
  const authorization = `Bearer ${TOKEN}`;
  for (let attempt = 0; ; ++attempt) {
    try {
      const health = await fetch(`${base}/healthz`, { headers: { authorization } }).then(r => r.json());
      if (health.upstream_connected) break;
    } catch {}
    assert.ok(attempt < 100 && child.exitCode === null, `bridge did not start: ${output.slice(-600)}`);
    await sleep(50);
  }
  const topic = config.devices[0].topics[0];
  const poll = async (waitMs, seq) => {
    const started = Date.now();
    const url = `${base}/api/cardputer/device-events/pending?limit=2&sender_kind=assistant&topic_key=${encodeURIComponent(topic)}` +
      (waitMs ? `&wait_ms=${waitMs}&activity_seq=${seq}` : "");
    const body = await fetch(url, { headers: { authorization } }).then(r => r.json());
    return { ...body, heldMs: Date.now() - started };
  };
  const inject = text => fetch(`${base}/api/cardputer/v1/topics/${encodeURIComponent(topic)}/inbox/inject`, { method: "POST",
    headers: { authorization, "content-type": "application/json" },
    body: JSON.stringify({ text, sender_kind: "assistant", sender_name: "Test" }) });
  const send = (headers = {}, client = "txt-1") => fetch(`${base}/api/device-text-turn`, { method: "POST",
    headers: { authorization, "content-type": "application/json", "x-client-msg-id": client, "x-turn-id": client,
      "x-conversation-key": topic, ...headers },
    body: JSON.stringify({ message: "hello there", device_id: "cpadv-dev", conversation_key: topic, input_type: "text",
      client_msg_id: client, turn_id: client, reply_audio: false, save_reply_audio: false }) });
  return { base, poll, send, inject, authorization, output: () => output };
}

test("new firmware gets a quick answer, then each status and the reply through held polls", async t => {
  const fake = await makeFake(t);
  await fake.script([
    { after: 1500, line: block("tool_use", { name: "WebSearch" }) },
    { line: tool("WebSearch", { query: "esp32 heap" }) },
    { after: 500, line: toolDone },
    { after: 300, line: block("tool_use", { name: "Edit" }) },
    { line: tool("Edit", { file_path: "/p/activity_cue.h" }) },
    { after: 400, line: toolDone },
    { after: 200, line: result("Finished the work") }
  ]);
  const bridge = await startBridge(t, fake);
  const started = Date.now();
  const response = await bridge.send({ "x-activity-status": "1", "x-turn-wait-ms": "1000" });
  const accepted = await response.json();
  assert.equal(response.status, 202);
  assert.equal(accepted.pending, true);
  assert.equal(accepted.activity_status, true);
  assert.ok(Date.now() - started < 2000, "the device must get its connection back quickly");

  const states = [];
  let seq = -1, reply = null;
  while (!reply) {
    assert.ok(Date.now() - started < 15000, `no reply; saw ${states}`);
    const body = await bridge.poll(2500, seq);
    assert.ok(body.heldMs < 2800, "a held poll has to end before the device gives up");
    seq = body.activity.seq;
    const label = body.activity.detail ? `${body.activity.state}:${body.activity.detail}` : body.activity.state;
    if (states.at(-1) !== label) states.push(label);
    reply = body.items.find(item => item.message?.content?.text === "Finished the work");
  }
  assert.ok(states.includes("searching:esp32 heap"), `saw ${states}`);
  assert.ok(states.includes("coding:activity_cue.h"), `saw ${states}`);
  assert.ok(states.indexOf("searching:esp32 heap") < states.indexOf("coding:activity_cue.h"));
  assert.ok(Date.now() - started < 5000, "the reply must not wait out a poll");
  assert.equal((await bridge.poll(0)).activity.state, "idle");
});

test("a held poll waits for a change, answers at once when it is behind, and gives up in time", async t => {
  const fake = await makeFake(t);
  await fake.script([{ line: result("ok") }]);
  const bridge = await startBridge(t, fake);
  const idle = await bridge.poll(0);
  assert.equal(idle.activity.state, "idle");
  assert.ok(Number.isInteger(idle.activity.seq));
  const behind = await bridge.poll(2500, idle.activity.seq + 7);
  assert.ok(behind.heldMs < 500, "a device that is behind must not be held");
  const held = await bridge.poll(600, idle.activity.seq);
  assert.ok(held.heldMs >= 550 && held.heldMs < 1500, `held ${held.heldMs} ms`);
  const capped = await bridge.poll(999999, idle.activity.seq);
  assert.ok(capped.heldMs >= 2400 && capped.heldMs < 3000, `held ${capped.heldMs} ms`);
});

test("a message ends a hold by itself, and one already waiting is never held", async t => {
  const fake = await makeFake(t);
  await fake.script([{ line: result("ok") }]);
  const bridge = await startBridge(t, fake);
  const seq = (await bridge.poll(0)).activity.seq;
  // Nothing is running here, so only the queued message can wake this poll.
  const held = bridge.poll(2500, seq);
  await sleep(300);
  const injected = await bridge.inject("A notice with no turn behind it");
  assert.ok(injected.ok, `inject failed: ${injected.status}`);
  const answered = await held;
  assert.ok(answered.heldMs < 1500, `held ${answered.heldMs} ms`);
  assert.equal(answered.items.length, 1);
  assert.equal(answered.activity.seq, seq, "the activity did not change, so the message is what woke it");
  const waiting = await bridge.poll(2500, seq);
  assert.ok(waiting.heldMs < 500 && waiting.items.length === 1, "an unacknowledged message must not wait out a hold");
});

test("a device that hangs up during a hold leaves nothing behind", async t => {
  const fake = await makeFake(t);
  await fake.script([{ line: result("ok") }]);
  const bridge = await startBridge(t, fake);
  const seq = (await bridge.poll(0)).activity.seq;
  const abort = new AbortController();
  const held = fetch(`${bridge.base}/api/cardputer/device-events/pending?wait_ms=2500&activity_seq=${seq}`,
    { headers: { authorization: bridge.authorization }, signal: abort.signal }).catch(error => error.name);
  await sleep(200);
  abort.abort();
  assert.equal(await held, "AbortError");
  await sleep(100);
  assert.equal((await bridge.poll(0)).activity.state, "idle", "the bridge keeps serving");
  assert.doesNotMatch(bridge.output(), /ERR_|Unhandled|Cannot set headers/u);
});

test("old firmware keeps today's behaviour: one long request and no held polls", async t => {
  const fake = await makeFake(t);
  await fake.script([{ after: 1800, line: result("Plain reply") }]);
  const bridge = await startBridge(t, fake);
  const started = Date.now();
  const response = await bridge.send();
  const body = await response.json();
  assert.equal(response.status, 200);
  assert.equal(body.reply, "Plain reply");
  assert.equal(body.activity_status, undefined);
  assert.ok(Date.now() - started >= 1700, "the reply belongs on this request");
  const plain = await bridge.poll(0);
  assert.ok(plain.heldMs < 400, "without wait_ms nothing is held");
});

test("a short wait alone can't be used without asking for live status", async t => {
  const fake = await makeFake(t);
  await fake.script([{ after: 1500, line: result("Still here") }]);
  const bridge = await startBridge(t, fake);
  const response = await bridge.send({ "x-turn-wait-ms": "1000" });
  assert.equal(response.status, 200);
  assert.equal((await response.json()).reply, "Still here");
});

test("the weather route answers the device, and only the device", async t => {
  const fake = await makeFake(t);
  await fake.script([{ line: result("ok") }]);
  // Stands in for both weather services, so this test needs no internet.
  const seen = [];
  const services = createHttpServer((req, res) => {
    seen.push(req.url);
    res.setHeader("content-type", "application/json");
    const name = new URL(req.url, "http://x").searchParams.get("name");
    if (name === "Nowhereville") return res.end("{}");
    if (name) return res.end(JSON.stringify({ results: [
      { name: "Salem", admin1: "North Carolina", latitude: 35.1, longitude: -84, feature_code: "PPLA2", population: 1615 },
      { name: "Salem", admin1: "Massachusetts", latitude: 42.519, longitude: -70.897, feature_code: "PPL", population: 20610 }] }));
    res.end(JSON.stringify({ current: { temperature_2m: 70, apparent_temperature: 71, relative_humidity_2m: 50, weather_code: 0, is_day: 1, wind_speed_10m: 4 },
      daily: { temperature_2m_max: [78], temperature_2m_min: [60], precipitation_probability_max: [10] } }));
  });
  await new Promise(resolve => services.listen(0, "127.0.0.1", resolve));
  t.after(() => services.close());
  const url = `http://127.0.0.1:${services.address().port}`;
  const bridge = await startBridge(t, fake, { weather: { geocodeUrl: `${url}/geocoding`, forecastUrl: `${url}/forecast` } });
  const get = (place, headers = { authorization: bridge.authorization }) =>
    fetch(`${bridge.base}/api/cardputer/weather?place=${encodeURIComponent(place)}`, { headers });
  const good = await get("Salem, MA");
  assert.equal(good.status, 200);
  assert.deepEqual(await good.json(), { place: "Salem", condition: "Sunny", icon: "sun",
    temp: 70, feels: 71, humidity: 50, wind: 4, high: 78, low: 60, rain: 10 });
  assert.ok(seen.some(item => item.includes("latitude=42.519")), "the Massachusetts town, not the first one listed");
  assert.equal((await get("Salem, MA", {})).status, 401);
  assert.equal((await get("<script>")).status, 400);
  assert.equal(seen.length, 2, "a bad request reaches no service");
  const unknown = await get("Nowhereville");
  assert.equal(unknown.status, 404);
  assert.match(await unknown.text(), /unknown_place/u);
  // By the Cap's GPS: no city to look up, two decimals passed on, and the device is still checked.
  const at = (query, headers = { authorization: bridge.authorization }) => fetch(`${bridge.base}/api/cardputer/weather?${query}`, { headers });
  const before = seen.length;
  const here = await at("lat=42.5083&lon=-70.9049");
  assert.equal(here.status, 200);
  assert.equal((await here.json()).place, "Here");
  assert.equal(seen.length, before + 1);
  assert.ok(seen.at(-1).includes("latitude=42.51") && seen.at(-1).includes("longitude=-70.9") && !seen.at(-1).includes("42.5083"));
  assert.equal((await at("lat=42.51&lon=-70.90", {})).status, 401);
  assert.equal((await at("lat=42.51&lon=-70.90&units=metric")).status, 200);
  assert.ok(seen.at(-1).includes("temperature_unit=celsius") && seen.at(-1).includes("wind_speed_unit=kmh"));
  assert.ok((await get("Salem, MA")).ok && seen.filter(item => item.includes("temperature_unit=fahrenheit")).length >= 1);
  for (const bad of ["lat=42.51", "lon=-70.9", "lat=95&lon=0", "lat=abc&lon=1", "lat=&lon="]) {
    const response = await at(bad);
    assert.equal(response.status, 400, bad);
    assert.match(await response.text(), /invalid_position/u);
  }
  assert.equal(seen.length, before + 2, "a bad position reaches no service");
});
