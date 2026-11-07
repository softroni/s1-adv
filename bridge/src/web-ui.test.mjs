import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { PassThrough } from "node:stream";
import { WebUi, hostAllowed, parseLabState, writeAllowed } from "./web-ui.mjs";
import { LabLiveService, labLanRoute, parseSnapshot } from "./lab-live.mjs";
import { DeviceActivity, describeStart } from "./device-activity.mjs";
import { LogRing, installLogRing, lineTag, parseLogFileLines } from "./log-ring.mjs";

// --- A bridge with one device and a few messages, enough for the page's routes ---
function fakeRuntime() {
  const messages = [
    { id: "m1", seq: 1, created_at: "2026-01-02T10:00:00.000Z", client_msg_id: "voice-1", sender: { kind: "device" }, source: { transport: "cardputer" }, content: { kind: "text", text: "hello" } },
    { id: "m2", seq: 2, created_at: "2026-01-02T10:00:05.000Z", sender: { kind: "assistant" }, source: { transport: "upstream" },
      content: { kind: "text", text: 'Playing it. [[cardputer_actions:[{"type":"lab.play","name":"Door wave"}]]]' } }
  ];
  const runtime = {
    sent: [], replies: [],
    store: { state: { devices: { d: { device_id: "cardputer-test" } },
      device_events: { "cardputer-test": [{ message_id: "m2", seq: 2, queued_at: "2026-01-02T10:00:05.000Z", acked_at: "2026-01-02T10:00:25.000Z" }] } },
      getHistory: () => ({ items: messages }) },
    currentTopicForDevice: () => ({ topic_key: "cardputer:main", session_key: "s" }),
    activityForTopic: () => ({ state: "idle", detail: "" }),
    upstream: { connected: true, children: new Set() },
    deviceFirmwareReports: new Map([["cardputer-test", { version: "0.2.1-dev", git: "abc1234" }]]),
    firmwareManifestPath: () => "/nonexistent/manifest.json",
    labStateForPage: () => ({ state: "saved rules:\nWave: any distance under 30; do strip_color set 280; hold 2000\nsaved sequences:\nDoor wave: strip_color set 120, wait_ms 1000, strip_color off\n", at: 5 }),
    podcasts: { lanPort: 31890 },
    cameras: { missing: () => "Cameras aren't set up on the Mac" },
    async sendText(args) { runtime.sent.push(args); return { seq: 3 }; },
    buildDeviceTurnUpstreamText: ({ text }) => `upstream:${text}`,
    async appendAssistantReply(args) { runtime.replies.push(args); return { seq: 4 }; }
  };
  return runtime;
}

async function call(ui, method, url, { body, headers = {} } = {}) {
  const req = new PassThrough();
  req.method = method;
  req.url = url;
  req.headers = { host: "127.0.0.1:31891", ...headers };
  if (body !== undefined) req.headers["content-type"] ??= "application/json";
  req.end(body === undefined ? "" : JSON.stringify(body));
  return new Promise((resolve) => {
    const res = { headersSent: false, status: 0, headers: {},
      writeHead(status, h) { this.status = status; this.headers = h; this.headersSent = true; },
      end(data) { const text = data ? data.toString() : ""; let json = null; try { json = JSON.parse(text); } catch {} resolve({ status: this.status, json, text }); },
      destroy() { resolve({ status: -1 }); } };
    void ui.handle(req, res);
  });
}

function makeUi(extra = {}) {
  const runtime = fakeRuntime();
  const labLive = new LabLiveService();
  const ui = new WebUi({ runtime, config: { ui: { enabled: true }, upstream: { provider: "claude-code", claudeCode: { model: "claude-opus-5-5", effort: "high" } },
    firmware: { rootDir: "/nonexistent", deviceClass: "cardputer_adv" }, assets: { logsDir: "/nonexistent" }, stateDir: "/nonexistent", ...extra },
    labLive, deviceActivity: new DeviceActivity(), logRing: new LogRing(),
    storageBudget: { roots: [tmpdir()], maxBytes: 1024, maxEntries: 10, reserveBytes: 0, minFreeBytes: 0 },
    extractActions: (text) => {
      const actions = [];
      const visibleText = text.replace(/\[\[cardputer_actions:(.*)\]\]/u, (_, json) => { actions.push(...JSON.parse(json)); return ""; }).trim();
      return { visibleText, actions };
    },
    sanitizeAction: (raw) => (raw && typeof raw.type === "string" ? { ...raw } : null) });
  return { ui, runtime, labLive };
}

const write = { "x-s1-ui": "1" };

test("only loopback, tailnet names and listed hosts reach the page", () => {
  assert.ok(hostAllowed("127.0.0.1:31891"));
  assert.ok(hostAllowed("localhost"));
  assert.ok(hostAllowed("[::1]:31891"));
  assert.ok(hostAllowed("mac.example-tail.ts.net:20443"));
  assert.ok(hostAllowed("bridge.lan", ["bridge.lan"]));
  assert.ok(!hostAllowed("evil.example.com"));
  assert.ok(!hostAllowed("ts.net.evil.example.com"));
});

test("changes need the page's own header and JSON, and a matching origin", () => {
  const req = (headers) => ({ headers: { host: "a.ts.net", "content-type": "application/json", "x-s1-ui": "1", ...headers } });
  assert.ok(writeAllowed(req({})));
  assert.ok(writeAllowed(req({ origin: "https://a.ts.net" })));
  assert.ok(!writeAllowed(req({ origin: "https://evil.example.com" })));
  assert.ok(!writeAllowed(req({ "x-s1-ui": undefined })));
  assert.ok(!writeAllowed(req({ "content-type": "text/plain" })));
});

test("a page from another host is refused, and so is a write without the header", async () => {
  const { ui, runtime } = makeUi();
  assert.equal((await call(ui, "GET", "/api/status", { headers: { host: "evil.example.com" } })).status, 403);
  assert.equal((await call(ui, "POST", "/api/chat", { body: { text: "hi" } })).status, 403);
  assert.equal(runtime.sent.length, 0);
});

test("status reports the device, its firmware, deliveries and storage", async () => {
  const { ui } = makeUi();
  const { status, json } = await call(ui, "GET", "/api/status");
  assert.equal(status, 200);
  assert.equal(json.device.id, "cardputer-test");
  assert.equal(json.device.firmware.version, "0.2.1-dev");
  assert.equal(json.delivery.waiting.length, 0);
  assert.equal(json.delivery.recent[0].after_ms, 20000);
  assert.equal(json.claude.model, "claude-opus-5-5");
  assert.equal(json.services.home_door.port, 31890);
  assert.equal(typeof json.storage.bytes, "number");
});

test("the chat shows actions as actions, voice as voice, and when a reply was picked up", async () => {
  const { ui } = makeUi();
  const { json } = await call(ui, "GET", "/api/chat");
  assert.equal(json.messages[0].via, "voice");
  assert.equal(json.messages[1].text, "Playing it.");
  assert.deepEqual(json.messages[1].actions, [{ type: "lab.play", name: "Door wave" }]);
  assert.equal(json.messages[1].delivery.state, "picked");
});

test("a message from the page goes to Claude as the Cardputer's typing does", async () => {
  const { ui, runtime } = makeUi();
  const { status, json } = await call(ui, "POST", "/api/chat", { body: { text: " turn the strip green " }, headers: write });
  assert.equal(status, 200);
  assert.equal(json.seq, 3);
  assert.equal(runtime.sent[0].text, "turn the strip green");
  assert.match(runtime.sent[0].clientMsgId, /^web/u);
  assert.equal(runtime.sent[0].upstreamText, "upstream:turn the strip green");
  assert.equal((await call(ui, "POST", "/api/chat", { body: { text: "  " }, headers: write })).status, 400);
});

test("the Remote sends only its own actions, as a message with a marker", async () => {
  const { ui, runtime } = makeUi();
  const ok = await call(ui, "POST", "/api/remote", { body: { action: { type: "ui.open", mode: "battery" }, words: "Opening [Battery]" }, headers: write });
  assert.equal(ok.status, 200);
  assert.equal(runtime.replies[0].source, "web_ui");
  assert.equal(runtime.replies[0].text, 'Opening Battery [[cardputer_actions:[{"type":"ui.open","mode":"battery"}]]]');
  assert.equal((await call(ui, "POST", "/api/remote", { body: { action: { type: "focus.start" } }, headers: write })).status, 400);
  assert.equal((await call(ui, "POST", "/api/remote", { body: { action: { type: "lab.play", name: "x" } }, headers: write })).status, 400);
  assert.equal((await call(ui, "POST", "/api/lab/action", { body: { action: { type: "lab.play", name: "Door wave" } }, headers: write })).status, 200);
  assert.equal((await call(ui, "POST", "/api/lab/action", { body: { action: { type: "ota.apply" } }, headers: write })).status, 400);
});

test("the Lab screen reads what's saved and asks the device to hurry", async () => {
  const { ui, labLive } = makeUi();
  assert.equal(labLive.watching(), false);
  const { json } = await call(ui, "GET", "/api/lab");
  assert.equal(labLive.watching(), true);
  assert.equal(json.saved.rules[0].name, "Wave");
  assert.deepEqual(json.saved.rules[0].when, [{ sensor: "distance", test: "under", value: 30 }]);
  assert.equal(json.saved.sequences[0].steps.length, 3);
  assert.equal(json.live.reported, false);
});

test("saved rules and sequences parse from the Cardputer's own lines", () => {
  const parsed = parseLabState([
    "saved rules:",
    "Wave lights: any knob over 25, distance under 30; do strip_color set 280, servo follow knob; hold 2000",
    "Movie time (switched off): all spoken on; do lamp_bright set 20, say text \"lights down, please\"; hold 0",
    "saved sequences:",
    "Door wave (loops): strip_color set 120, wait_ms 1000, servo set 90, strip_color off",
    "owner reads US units (F, inches, feet, mph)"
  ].join("\n"));
  assert.equal(parsed.known, true);
  assert.equal(parsed.rules.length, 2);
  assert.equal(parsed.rules[0].any, true);
  assert.deepEqual(parsed.rules[0].do[1], { output: "servo", how: "follow", value: "knob" });
  assert.equal(parsed.rules[1].on, false);
  assert.deepEqual(parsed.rules[1].do[1], { output: "say", text: "lights down, please" });
  assert.equal(parsed.sequences[0].loop, true);
  assert.deepEqual(parsed.sequences[0].steps[3], { output: "strip_color", off: true });
  assert.equal(parseLabState("").known, false);
});

test("settings write only the keys the page shows, and refuse anything else", async () => {
  const dir = await mkdtemp(path.join(tmpdir(), "s1-ui-"));
  const file = path.join(dir, "config.json");
  await writeFile(file, JSON.stringify({ secret: "keep", upstream: { claudeCode: { model: "claude-opus-5-5", effort: "high", command: "claude" } }, storage: { maxBytes: 268435456 } }));
  const { ui } = makeUi({ configPath: file });
  const ok = await call(ui, "POST", "/api/settings", { body: { claude: { effort: "xhigh", web: false }, firmware: { keep_images: 4 } }, headers: write });
  assert.equal(ok.status, 200);
  assert.deepEqual(ok.json.changed, ["effort", "web access", "builds kept"]);
  assert.equal(ok.json.restarting, false);
  const saved = JSON.parse(await readFile(file, "utf8"));
  assert.equal(saved.secret, "keep");
  assert.equal(saved.upstream.claudeCode.command, "claude");
  assert.equal(saved.upstream.claudeCode.effort, "xhigh");
  assert.equal(saved.upstream.claudeCode.webAccess, false);
  assert.equal(saved.firmware.keepImages, 4);
  assert.equal((await call(ui, "POST", "/api/settings", { body: { claude: { model: "gpt" } }, headers: write })).status, 400);
  assert.equal((await call(ui, "POST", "/api/settings", { body: { storage: { max_bytes: 5 } }, headers: write })).status, 400);
});

test("restart is refused unless allowed, and while Claude works", async () => {
  const { ui, runtime } = makeUi();
  assert.deepEqual((await call(ui, "POST", "/api/restart", { body: {}, headers: write })).json, { restarting: false, reason: "not_allowed" });
  let restarted = "";
  const allowed = makeUi({ ui: { enabled: true, allowRestart: true } });
  allowed.ui.restart = (who) => { restarted = who; };
  allowed.runtime.upstream.children.add({});
  assert.equal((await call(allowed.ui, "POST", "/api/restart", { body: {}, headers: write })).json.reason, "claude_busy");
  allowed.runtime.upstream.children.clear();
  assert.equal((await call(allowed.ui, "POST", "/api/restart", { body: {}, headers: write })).json.restarting, true);
  await new Promise((resolve) => setTimeout(resolve, 400));
  assert.equal(restarted, "the web page");
  assert.equal(runtime.sent.length, 0);
});

// --- The Lab, live ---
const SNAPSHOT = ["lab\t1", "view\tdevice", "open\tLoRa Cap GPS", "page\t1", "units\tus", "playing\t0", "found\tUltrasonic,ENV III",
  "wires\tMini Angle\tServo", "sig\tdistance\t41", "sig\ttemperature\t23", "out\tservo\t90", "out\tled\t-1",
  "gps\t1\t8\t14\t180\t9", "strip_mode\trainbow", "sig\tBAD KEY\t1", "sig\tknob\tlots"].join("\n");

test("a snapshot parses into parts, readings and outputs, keeping lines it doesn't know", () => {
  const s = parseSnapshot(SNAPSHOT);
  assert.equal(s.view, "device");
  assert.equal(s.open, "LoRa Cap GPS");
  assert.equal(s.page, 1);
  assert.equal(s.units, "us");
  assert.deepEqual(s.found, ["Ultrasonic", "ENV III"]);
  assert.deepEqual(s.wires, { white: "Mini Angle", yellow: "Servo" });
  assert.deepEqual(s.sigs, { distance: 41, temperature: 23 });
  assert.deepEqual(s.outs, { servo: 90, led: -1 });
  assert.deepEqual(s.gps, { fix: true, used: 8, inView: 14, altitude: 180, hdop: 0.9 });
  assert.equal(s.extra.strip_mode, "rainbow");
  assert.equal(parseSnapshot("hello"), null);
});

function lanRequest(url, body, remote = "192.168.0.20") {
  const req = new PassThrough();
  req.method = "POST";
  req.url = url;
  req.headers = {};
  req.socket = { remoteAddress: remote };
  req.end(body);
  return new Promise((resolve) => {
    const res = { writeHead(status) { this.status = status; }, end(data) { resolve({ status: this.status, text: String(data) }); }, destroy() { resolve({ status: -1 }); } };
    const handled = labLanRoute(service, req, res);
    if (!handled) resolve({ status: 0 });
  });
}
let service;

test("the home door takes a snapshot only with a ticket, from home, and says whether a page watches", async () => {
  service = new LabLiveService();
  service.lanPort = 31890;
  const lines = service.ticketLines("cardputer-test", { en0: [{ family: "IPv4", address: "192.168.0.10", cidr: "192.168.0.10/24", internal: false }] });
  const ticket = /\/lab\/([0-9a-f]{32})/u.exec(lines)[1];
  assert.match(lines, /^ticket\t31890\t\/lab\/[0-9a-f]{32}\nlan\t192\.168\.0\.10\t24\n$/u);
  assert.equal((await lanRequest("/lab/" + "0".repeat(32) + "/live", SNAPSHOT)).status, 404);
  assert.equal((await lanRequest(`/lab/${ticket}/live`, SNAPSHOT, "203.0.113.5")).status, 404);
  assert.equal((await lanRequest(`/lab/${ticket}/live`, "nonsense")).status, 400);
  assert.deepEqual(await lanRequest(`/lab/${ticket}/live`, SNAPSHOT), { status: 200, text: "watch\t0\n" });
  service.watch();
  assert.deepEqual(await lanRequest(`/lab/${ticket}/live`, SNAPSHOT), { status: 200, text: "watch\t1\n" });
  assert.equal((await lanRequest("/planes/x/near", "")).status, 0);
  const view = service.view();
  assert.equal(view.live, true);
  assert.equal(view.history.distance.length, 2);
  assert.equal(new LabLiveService().ticketLines("d", {}), "none\n");
});

test("a closed Lab or an old report isn't live", () => {
  let now = 1000;
  const live = new LabLiveService({ now: () => now });
  live.accept("lab\t1\nview\tdevices\n");
  assert.equal(live.view().live, true);
  now += 30000;
  assert.equal(live.view().live, false);
  live.accept("lab\t1\nview\tclosed\n");
  assert.equal(live.view().live, false);
  assert.equal(live.view().reported, true);
});

// --- Check-ins, starts and the log ---
test("check-ins fill a minute strip, and starts read as words", () => {
  let now = 10 * 60_000;
  const activity = new DeviceActivity({ now: () => now });
  activity.seen("/api/cardputer/firmware/manifest");
  activity.seen("/api/cardputer/device-events/pending");
  now += 120_000;
  activity.seen("/x");
  activity.started("[device] started device=d reset=software note=memory_f13_b7_Pulse");
  const view = activity.view();
  assert.equal(view.strip.length, 60);
  assert.equal(view.strip.at(-1), 1);
  assert.equal(view.strip.at(-3), 2);
  assert.equal(view.last_seen_ms, 0);
  assert.deepEqual(describeStart(view.starts[0]), { title: "Restarted itself", detail: "ran low on memory in Pulse (13 KB free)", warn: true });
  assert.deepEqual(describeStart({ reset: "power-on", note: "" }), { title: "Turned on", detail: "power on", warn: false });
  assert.equal(describeStart({ reset: "crash", note: "" }).warn, true);
});

test("log lines get a time and a tag, from the console and from the file", () => {
  const ring = new LogRing(3);
  const written = [];
  const target = { log: (t) => written.push(t), info: (t) => written.push(t), warn: (t) => written.push(t), error: (t) => written.push(t) };
  installLogRing(ring, target);
  target.info("[cameras] 5 camera(s)");
  target.warn("plain %d", 7);
  assert.match(written[0], /^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d \[cameras\] 5 camera\(s\)$/u);
  assert.equal(ring.lines[0].tag, "cameras");
  assert.equal(ring.lines[1].level, "warn");
  assert.equal(ring.lines[1].text, "plain 7");
  for (let i = 0; i < 5; i += 1) target.info(`[x] ${i}`);
  assert.equal(ring.lines.length, 3);
  assert.equal(lineTag("no tag"), "bridge");
  const lines = parseLogFileLines("[firmware] manifest device=d\n2026-11-06 20:31:02 [device] started device=d reset=power-on\n2026-11-06 20:32:00 [podcasts] audio failed\n");
  assert.equal(lines[0].at, null);
  assert.equal(lines[1].tag, "device");
  assert.equal(new Date(lines[1].at).getMinutes(), 31);
  assert.equal(lines[2].level, "warn");
});
