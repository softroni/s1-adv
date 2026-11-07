// The bridge's own web page: the Cardputer's status, the chat, a remote, the Lab live, the log,
// firmware and settings. It has its own port on the loopback address and nothing else; share it
// with your own devices through something like `tailscale serve`, never through Funnel or a
// public proxy. docs/BRIDGE-UI.md explains how to turn it on.
//
// Requests that change anything need a JSON body and an X-S1-UI header, which a browser only sends
// from this page itself, so another site open in the same browser can't make them.
import { createServer } from "node:http";
import { execFile, execFileSync } from "node:child_process";
import { open, readdir, readFile, rename, stat, statfs, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { describeStart } from "./device-activity.mjs";
import { parseLogFileLines } from "./log-ring.mjs";
import { makeId } from "./utils.mjs";

const UI_DIR = fileURLToPath(new URL("../ui/", import.meta.url));
const BRIDGE_DIR = fileURLToPath(new URL("..", import.meta.url));
export const UI_PORT = 31891;
const STATIC = new Map([
  ["/", ["index.html", "text/html; charset=utf-8"]],
  ["/index.html", ["index.html", "text/html; charset=utf-8"]],
  ["/app.js", ["app.js", "text/javascript; charset=utf-8"]],
  ["/style.css", ["style.css", "text/css; charset=utf-8"]],
  ["/icon.svg", ["icon.svg", "image/svg+xml"]]
]);
const MAX_JSON = 16 * 1024;
const ONLINE_MS = 90_000;
const LOG_TAIL_BYTES = 2 * 1024 * 1024;
const APP_SLOT_BYTES = 0x300000;
// What the Remote may send. Lab actions come from the Lab screen, the rest stay with the chat.
const REMOTE_ACTIONS = new Set(["ui.open", "settings.set_ota_auto", "settings.set_ota_boot_check",
  "settings.set_ota_min_battery", "ota.check", "ota.apply"]);
const LAB_ACTIONS = new Set(["lab.play", "lab.rule", "lab.sequence", "lab.once"]);
export const MODELS = ["claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5", "claude-fable-5-1"];
export const EFFORTS = ["low", "medium", "high", "xhigh", "max"];
// Kokoro's English voices: a for American, b for British, then f or m.
export const VOICES = ["af_heart", "af_bella", "af_nicole", "af_sarah", "af_sky", "af_nova", "af_river",
  "am_adam", "am_michael", "am_echo", "am_eric", "am_liam", "bf_emma", "bf_isabella", "bf_alice", "bm_george", "bm_lewis", "bm_daniel"];
const STORAGE_CHOICES = [128, 256, 512, 1024].map((mib) => mib * 1024 * 1024);
const TAILSCALE = ["tailscale", "/usr/local/bin/tailscale", "/opt/homebrew/bin/tailscale", "/Applications/Tailscale.app/Contents/MacOS/Tailscale"];
const LINK_MS = 30_000;

// The page's address on the tailnet, from `tailscale serve status --json`: the site whose handler
// proxies to the page's own port. A site on Funnel is public, so it's reported as that, not as a link.
export function linkFromServeStatus(status, port) {
  for (const [hostPort, site] of Object.entries(status?.Web || {})) {
    for (const [mount, handler] of Object.entries(site?.Handlers || {})) {
      const proxy = /^https?:\/\/(?:127\.0\.0\.1|localhost|\[::1\]):(\d+)\/?$/u.exec(String(handler?.Proxy || ""));
      if (!proxy || Number(proxy[1]) !== port) continue;
      if (status?.AllowFunnel?.[hostPort]) return { funnel: true };
      const [host, sitePort] = hostPort.split(":");
      if (!/^[a-z0-9.-]+$/iu.test(host)) continue;
      return { url: `https://${host}${sitePort && sitePort !== "443" ? `:${sitePort}` : ""}${mount.startsWith("/") ? mount : "/"}` };
    }
  }
  return {};
}

// For the Cardputer's Bridge page, as lines: "url <address>" or "none <why, for its screen>".
export function linkLines({ enabled, url = "", funnel = false, tailscale = true }) {
  if (!enabled) return "none\tThe page is off on the bridge\n";
  if (funnel) return "none\tIt's on Funnel: take it off\n";
  if (url) return `url\t${url}\n`;
  return tailscale ? "none\tNot shared on Tailscale yet\n" : "none\tTailscale isn't running\n";
}

// --- Small helpers ------------------------------------------------------------------------------
function send(res, status, payload) {
  const data = Buffer.from(JSON.stringify(payload));
  res.writeHead(status, { "content-type": "application/json; charset=utf-8", "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}
const fail = (res, status, error, detail = "") => send(res, status, { error, ...(detail ? { detail } : {}) });

async function readBody(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_JSON) throw Object.assign(new Error("too_large"), { status: 413 });
    chunks.push(chunk);
  }
  try { return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}"); }
  catch { throw Object.assign(new Error("invalid_json"), { status: 400 }); }
}

// Loopback, a tailnet name, or one the owner listed. Anything else is a page pretending to be this
// one through DNS, which must not reach the bridge.
export function hostAllowed(host, allowed = []) {
  const name = String(host || "").toLowerCase().replace(/:\d+$/u, "").replace(/^\[|\]$/gu, "");
  return name === "localhost" || name === "127.0.0.1" || name === "::1" || name.endsWith(".ts.net") ||
    allowed.map((entry) => String(entry).toLowerCase()).includes(name);
}

export function writeAllowed(req) {
  if (req.headers["x-s1-ui"] !== "1") return false;
  if (!String(req.headers["content-type"] || "").startsWith("application/json")) return false;
  const origin = req.headers.origin;
  if (!origin) return true;
  try { return new URL(origin).host === req.headers.host; } catch { return false; }
}

function wav(pcm, sampleRate) {
  const header = Buffer.alloc(44);
  header.write("RIFF", 0, "ascii"); header.writeUInt32LE(36 + pcm.length, 4); header.write("WAVE", 8, "ascii");
  header.write("fmt ", 12, "ascii"); header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20); header.writeUInt16LE(1, 22);
  header.writeUInt32LE(sampleRate, 24); header.writeUInt32LE(sampleRate * 2, 28); header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34); header.write("data", 36, "ascii"); header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

function gitHead() {
  try { return execFileSync("git", ["-C", BRIDGE_DIR, "rev-parse", "--short", "HEAD"], { encoding: "utf8", timeout: 3000 }).trim(); }
  catch { return ""; }
}

async function tailFile(file, bytes) {
  const handle = await open(file, "r");
  try {
    const { size } = await handle.stat();
    const start = Math.max(0, size - bytes);
    const buffer = Buffer.alloc(size - start);
    await handle.read(buffer, 0, buffer.length, start);
    const text = buffer.toString("utf8");
    return start ? text.slice(text.indexOf("\n") + 1) : text;
  } finally { await handle.close(); }
}

const versionOf = (file) => /(\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?)\.bin$/u.exec(file)?.[1] || "";
const versionKey = (version) => version.split(/[.-]/u).map((part) => (/^\d+$/u.test(part) ? part.padStart(6, "0") : part)).join(".");

async function images(dir) {
  let names = [];
  try { names = (await readdir(dir)).filter((name) => name.endsWith(".bin")); } catch { return []; }
  const out = [];
  for (const name of names) {
    try { const info = await stat(path.join(dir, name)); out.push({ file: name, version: versionOf(name), size: info.size, at: info.mtimeMs }); } catch {}
  }
  return out.sort((a, b) => versionKey(b.version).localeCompare(versionKey(a.version)));
}

// Splits on sep, but not inside a quoted phrase: a rule's say text can hold commas.
function splitOutside(text, sep) {
  const out = [];
  let quoted = false, start = 0;
  for (let i = 0; i < text.length; i += 1) {
    if (text[i] === '"') quoted = !quoted;
    else if (!quoted && text.startsWith(sep, i)) { out.push(text.slice(start, i)); start = i + sep.length; i += sep.length - 1; }
  }
  out.push(text.slice(start));
  return out;
}

// "Wave lights: any distance under 30, knob over 25; do strip_color set 280, servo follow knob; hold 2000"
export function parseLabState(text) {
  const rules = [], sequences = [];
  let section = "";
  for (const raw of String(text || "").split("\n")) {
    const line = raw.trim();
    if (!line) continue;
    if (line.startsWith("saved rules")) { section = "rules"; continue; }
    if (line.startsWith("saved sequences")) { section = "seqs"; continue; }
    if (/^(owner reads|last check)/u.test(line)) { section = ""; continue; }
    const colon = line.indexOf(": ");
    if (colon < 1) continue;
    let name = line.slice(0, colon);
    const body = line.slice(colon + 2);
    if (section === "rules") {
      const on = !name.endsWith(" (switched off)");
      name = name.replace(/ \(switched off\)$/u, "");
      const [when = "", dos = "", hold = ""] = splitOutside(body, "; ");
      const any = when.startsWith("any ");
      const conds = when.replace(/^(any|all) /u, "").split(", ").filter(Boolean).map((cond) => {
        const [sensor, test, value] = cond.split(" ");
        return { sensor, test, value: value === undefined ? null : Number(value) };
      });
      const actions = splitOutside(dos.replace(/^do /u, ""), ", ").filter(Boolean).map((item) => {
        const say = /^say text "(.*)"$/u.exec(item);
        if (say) return { output: "say", text: say[1] };
        const [output, how, ...rest] = item.split(" ");
        const value = rest.join(" ");
        return { output, how, value: how === "set" ? Number(value) : value };
      });
      rules.push({ name, on, any, when: conds, do: actions, hold_ms: Number(hold.replace(/^hold /u, "")) || 0 });
    } else if (section === "seqs") {
      const loop = name.endsWith(" (loops)");
      name = name.replace(/ \(loops\)$/u, "");
      const steps = body.split(", ").filter(Boolean).map((item) => {
        const [first, how, value] = item.split(" ");
        if (first === "wait_ms") return { wait_ms: Number(how) };
        if (how === "off") return { output: first, off: true };
        return { output: first, set: Number(value) };
      });
      sequences.push({ name, loop, steps });
    }
  }
  return { rules, sequences, known: /saved rules/u.test(String(text || "")) };
}

// --- The page's server --------------------------------------------------------------------------
export class WebUi {
  constructor({ runtime, config, labLive, deviceActivity, logRing, storageBudget, extractActions, sanitizeAction, restart }) {
    Object.assign(this, { runtime, config, labLive, deviceActivity, logRing, storageBudget, extractActions, sanitizeAction, restart });
    this.settings = config.ui || {};
    this.commit = gitHead();
    this.startedAt = Date.now();
    this.server = null;
    this.linkCache = null;
    this.run = promisify(execFile);
  }

  // Where a phone opens the page: ui.url if the owner set one, or Tailscale's serve settings.
  async link() {
    const configured = String(this.settings.url || "").trim();
    if (/^https?:\/\/[^\s]+$/u.test(configured)) return { enabled: true, url: configured };
    if (this.linkCache && Date.now() - this.linkCache.at < LINK_MS) return this.linkCache.value;
    let value = { enabled: true, tailscale: false };
    for (const command of TAILSCALE) {
      try {
        const { stdout } = await this.run(command, ["serve", "status", "--json"], { timeout: 4000, maxBuffer: 256 * 1024 });
        value = { enabled: true, tailscale: true, ...linkFromServeStatus(JSON.parse(stdout || "{}"), Number(this.settings.port) || UI_PORT) };
        break;
      } catch (error) {
        if (error.code !== "ENOENT") break;
      }
    }
    this.linkCache = { at: Date.now(), value };
    return value;
  }

  device() { return Object.values(this.runtime.store.state.devices || {})[0] || null; }
  topic() {
    const device = this.device();
    return device ? this.runtime.currentTopicForDevice(device.device_id) : null;
  }
  busy(topicKey) {
    const activity = topicKey ? this.runtime.activityForTopic(topicKey) : { state: "idle" };
    // A running claude child is a turn in progress, whatever the activity line says.
    const children = this.runtime.upstream?.children?.size || 0;
    return activity.state !== "idle" || children > 0;
  }

  start() {
    const port = Number(this.settings.port) || UI_PORT;
    const host = this.settings.host || "127.0.0.1";
    this.server = createServer((req, res) => { void this.handle(req, res); });
    this.server.on("error", (error) => console.warn(`[ui] web page off: ${String(error.code || error.message || error).slice(0, 80)}`));
    this.server.listen(port, host, () => console.log(`[ui] web page on http://${host}:${port}/`));
    return this.server;
  }
  close() { this.server?.close(); }

  async handle(req, res) {
    try {
      if (!hostAllowed(req.headers.host, this.settings.allowedHosts)) { fail(res, 403, "host_not_allowed"); return; }
      const url = new URL(req.url || "/", "http://ui");
      if (req.method === "GET" && STATIC.has(url.pathname)) { await this.sendStatic(res, ...STATIC.get(url.pathname)); return; }
      if (!url.pathname.startsWith("/api/")) { fail(res, 404, "not_found"); return; }
      const route = `${req.method} ${url.pathname}`;
      if (req.method === "POST" && !writeAllowed(req)) { fail(res, 403, "write_not_allowed"); return; }
      const body = req.method === "POST" ? await readBody(req) : null;
      switch (route) {
        case "GET /api/status": send(res, 200, await this.status()); return;
        case "GET /api/chat": send(res, 200, this.chat(Number(url.searchParams.get("limit")) || 80)); return;
        case "POST /api/chat": send(res, 200, await this.sendChat(body)); return;
        case "POST /api/speak": await this.speak(body, res); return;
        case "POST /api/remote": send(res, 200, await this.remote(body, REMOTE_ACTIONS)); return;
        case "GET /api/lab": send(res, 200, this.lab()); return;
        case "POST /api/lab/action": send(res, 200, await this.remote(body, LAB_ACTIONS)); return;
        case "POST /api/lab/control": this.labControl(body, res); return;
        case "GET /api/logs": send(res, 200, await this.logs(url.searchParams)); return;
        case "GET /api/device-logs": send(res, 200, await this.deviceLogs()); return;
        case "GET /api/firmware": send(res, 200, await this.firmware()); return;
        case "GET /api/settings": send(res, 200, this.readSettings()); return;
        case "POST /api/settings": send(res, 200, await this.saveSettings(body)); return;
        case "GET /api/cameras/link": send(res, 200, this.cameraLink()); return;
        case "GET /api/link": send(res, 200, await this.link()); return;
        case "POST /api/restart": send(res, 200, this.requestRestart()); return;
        default: fail(res, 404, "not_found");
      }
    } catch (error) {
      const status = Number(error.status) || 500;
      if (status >= 500) console.warn(`[ui] ${req.method} ${String(req.url).slice(0, 60)} failed: ${String(error.message || error).slice(0, 160)}`);
      if (!res.headersSent) fail(res, status, String(error.code || error.message || "failed").slice(0, 80));
      else res.destroy();
    }
  }

  async sendStatic(res, file, type) {
    const data = await readFile(path.join(UI_DIR, file));
    res.writeHead(200, { "content-type": type, "content-length": String(data.length), "cache-control": "no-cache",
      "x-content-type-options": "nosniff", "referrer-policy": "no-referrer",
      "content-security-policy": "default-src 'self'; img-src 'self' data:; media-src 'self' blob:; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'" });
    res.end(data);
  }

  // --- Status ---
  async storage() {
    let bytes = 0, entries = 0, error = "";
    const pending = [...this.storageBudget.roots];
    try {
      while (pending.length && entries < 200_000) {
        const dir = pending.pop();
        let children;
        try { children = await readdir(dir, { withFileTypes: true }); } catch (e) { if (e.code === "ENOENT") continue; throw e; }
        for (const child of children) {
          entries += 1;
          const file = path.join(dir, child.name);
          if (child.isDirectory()) pending.push(file);
          else if (child.isFile()) { try { bytes += (await stat(file)).size; } catch {} }
        }
      }
    } catch (e) { error = String(e.code || e.message || e); }
    let freeBytes = null;
    try { const fs = await statfs(this.storageBudget.roots[0]); freeBytes = fs.bavail * fs.bsize; } catch {}
    return { bytes, entries, max_bytes: this.storageBudget.maxBytes, max_entries: this.storageBudget.maxEntries,
      reserve_bytes: this.storageBudget.reserveBytes, free_bytes: freeBytes, min_free_bytes: this.storageBudget.minFreeBytes, error };
  }

  delivery(deviceId) {
    const events = (this.runtime.store.state.device_events || {})[deviceId] || [];
    const now = Date.now();
    const waiting = events.filter((event) => !event.acked_at).map((event) => ({ seq: event.seq, queued_at: event.queued_at,
      waiting_ms: now - Date.parse(event.queued_at), text: String(event.message?.content?.text || "").slice(0, 80) }));
    const recent = events.filter((event) => event.acked_at).slice(-6).reverse().map((event) => ({ seq: event.seq,
      queued_at: event.queued_at, acked_at: event.acked_at, after_ms: Date.parse(event.acked_at) - Date.parse(event.queued_at) }));
    return { waiting, recent };
  }

  async status() {
    const device = this.device();
    const topic = this.topic();
    const upstream = this.runtime.upstream;
    const cc = this.config.upstream?.claudeCode || {};
    const activity = topic ? this.runtime.activityForTopic(topic.topic_key) : { state: "idle", detail: "" };
    const seen = this.deviceActivity.view();
    const report = device ? this.runtime.deviceFirmwareReports.get(device.device_id) || null : null;
    let offered = null;
    try { offered = JSON.parse(await readFile(this.runtime.firmwareManifestPath(), "utf8")); } catch {}
    const messagesToday = topic ? this.messages(topic.topic_key).filter((m) => m.from !== "claude" && new Date(m.at).toDateString() === new Date().toDateString()) : [];
    return {
      now: Date.now(),
      bridge: { started_at: this.startedAt, commit: this.commit, pid: process.pid, can_restart: this.settings.allowRestart === true },
      claude: { provider: this.config.upstream?.provider || "", model: cc.model || "", effort: cc.effort || "",
        connected: Boolean(upstream?.connected), waiting_for_login: Boolean(upstream?.watchingLogin && !upstream?.connected),
        state: activity.state, detail: activity.detail, busy: topic ? this.busy(topic.topic_key) : false,
        today: { messages: messagesToday.length, voice: messagesToday.filter((m) => m.via === "voice").length } },
      device: device ? { id: device.device_id, online: seen.last_seen_ms !== null && seen.last_seen_ms < ONLINE_MS, ...seen,
        starts: seen.starts.map((start) => ({ at: start.at, ...describeStart(start) })), firmware: report } : null,
      delivery: device ? this.delivery(device.device_id) : { waiting: [], recent: [] },
      storage: await this.storage(),
      offered: offered ? { version: offered.version, size: offered.size, built_at: offered.built_at } : null,
      services: {
        voice: { on: Boolean(this.runtime.kokoro), ready: Boolean(this.runtime.kokoro?.ready) },
        cameras: { on: !this.runtime.cameras?.missing?.(), missing: this.runtime.cameras?.missing?.() || "", running: Boolean(this.runtime.cameras?.port) },
        home_door: { port: this.runtime.podcasts?.lanPort || 0 },
        realtime: { on: this.config.realtimeVoice?.enabled === true },
        lab_live: { reporting: this.labLive.view().live }
      }
    };
  }

  // --- Chat ---
  messages(topicKey) {
    const items = this.runtime.store.getHistory(topicKey, undefined, 250)?.items || [];
    const device = this.device();
    const events = new Map(((this.runtime.store.state.device_events || {})[device?.device_id] || []).map((event) => [event.message_id, event]));
    return items.map((item) => {
      const kind = item.sender?.kind;
      const transport = item.source?.transport || "";
      const raw = String(item.content?.text || "");
      const parsed = kind === "assistant" ? this.extractActions(raw) : { visibleText: raw, actions: [] };
      const event = events.get(item.id);
      const via = transport === "web_ui" ? "web" : String(item.client_msg_id || "").startsWith("voice") ? "voice" :
        String(item.client_msg_id || "").startsWith("web") ? "web" : "typed";
      return {
        seq: item.seq, id: item.id, at: item.created_at,
        from: kind === "assistant" ? (transport === "web_ui" ? "page" : "claude") : "you",
        via, text: parsed.visibleText, actions: parsed.actions,
        failed: Boolean(item.source?.terminal_error),
        delivery: event ? (event.acked_at ? { state: "picked", after_ms: Date.parse(event.acked_at) - Date.parse(event.queued_at) }
          : { state: "waiting", since: event.queued_at }) : null
      };
    });
  }

  chat(limit) {
    const topic = this.topic();
    if (!topic) return { messages: [], activity: { state: "idle", detail: "" }, busy: false };
    const all = this.messages(topic.topic_key);
    return { total: all.length, messages: all.slice(-Math.min(Math.max(limit, 1), 250)),
      activity: this.runtime.activityForTopic(topic.topic_key), busy: this.busy(topic.topic_key) };
  }

  async sendChat(body) {
    const text = String(body?.text || "").trim().slice(0, 2000);
    if (!text) throw Object.assign(new Error("empty"), { status: 400 });
    const device = this.device();
    const topic = this.topic();
    if (!device || !topic) throw Object.assign(new Error("no_device"), { status: 409 });
    const clientMsgId = makeId("web");
    console.info(`[ui] chat message from the web page (${text.length} chars)`);
    // The same turn the Cardputer's typing makes, so the reply lands in the same conversation.
    const ack = await this.runtime.sendText({ device, topicKey: topic.topic_key, clientMsgId, text,
      upstreamText: this.runtime.buildDeviceTurnUpstreamText({ clientMsgId, device, topicKey: topic.topic_key, text, inputMode: "text" }),
      waitForUpstream: false });
    return { ok: true, seq: ack?.seq ?? null };
  }

  async speak(body, res) {
    const topic = this.topic();
    const message = topic ? this.messages(topic.topic_key).find((m) => m.seq === Number(body?.seq)) : null;
    if (!message?.text) { fail(res, 404, "not_found"); return; }
    const audio = await this.runtime.synthesizeKokoro(message.text.slice(0, 1200), 24000);
    if (!audio) { fail(res, 503, "voice_unavailable"); return; }
    const pcm = Buffer.isBuffer(audio.pcm) ? audio.pcm : Buffer.from(audio.pcm.buffer, audio.pcm.byteOffset, audio.pcm.byteLength);
    const data = wav(pcm, audio.sampleRate);
    res.writeHead(200, { "content-type": "audio/wav", "content-length": String(data.length), "cache-control": "no-store" });
    res.end(data);
  }

  // --- The Remote and the Lab: an action rides a short message, as Claude's do, and the
  // Cardputer runs it when it next collects its messages. ---
  async remote(body, allowed) {
    const action = this.sanitizeAction(body?.action);
    if (!action || !allowed.has(action.type)) throw Object.assign(new Error("action_not_allowed"), { status: 400 });
    const topic = this.topic();
    if (!topic) throw Object.assign(new Error("no_device"), { status: 409 });
    const words = String(body?.words || "").replace(/[[\]\s]+/gu, " ").trim().slice(0, 120) || "From the web page.";
    const message = await this.runtime.appendAssistantReply({ topicKey: topic.topic_key, clientMsgId: makeId("web"),
      text: `${words} [[cardputer_actions:${JSON.stringify([action])}]]`, source: "web_ui" });
    console.info(`[ui] sent ${action.type} to the Cardputer (seq ${message.seq})`);
    return { ok: true, seq: message.seq, action };
  }

  // A part set from the page: it rides the answer to the Cardputer's next report, about a second
  // away while the Lab is open at home. Relays, the lamp and the air conditioners too, with no
  // confirm, because the owner asked for that.
  labControl(body, res) {
    const output = String(body?.output || "");
    const value = Number(body?.value);
    const result = this.labLive.control(output, value);
    if (!result.ok) { fail(res, result.error === "lab_not_live" ? 409 : 400, result.error); return; }
    console.info(`[ui] lab control ${output}=${value} (${result.id})`);
    send(res, 200, result);
  }

  lab() {
    this.labLive.watch();
    const topic = this.topic();
    const { state, at } = topic ? this.runtime.labStateForPage(topic.session_key) : { state: "", at: null };
    return { saved: { at, ...parseLabState(state) }, live: this.labLive.view() };
  }

  // --- Logs ---
  async logs(params) {
    let lines;
    let source = "memory";
    if (this.settings.logFile) {
      try { lines = parseLogFileLines(await tailFile(this.settings.logFile, LOG_TAIL_BYTES)); source = "file"; } catch {}
    }
    if (!lines) lines = this.logRing.lines.map(({ at, level, tag, text }) => ({ at, level, tag, text }));
    const isCheck = (line) => /^\[firmware\] manifest /u.test(line.text);
    const hideChecks = params.get("checks") !== "1";
    const tag = String(params.get("tag") || "");
    const q = String(params.get("q") || "").toLowerCase().slice(0, 80);
    const problems = params.get("problems") === "1";
    const tags = {};
    let hidden = 0;
    const out = [];
    for (const line of lines) {
      tags[line.tag] = (tags[line.tag] || 0) + 1;
      if (hideChecks && isCheck(line)) { hidden += 1; continue; }
      if (tag && line.tag !== tag) continue;
      if (problems && line.level === "info" && !/\bwarn|fail|error|refused|rejected|crash|memory\b/iu.test(line.text)) continue;
      if (q && !line.text.toLowerCase().includes(q)) continue;
      out.push(line);
    }
    const limit = Math.min(Number(params.get("limit")) || 400, 2000);
    return { source, total: lines.length, hidden_checks: hidden, tags, lines: out.slice(-limit).reverse() };
  }

  async deviceLogs() {
    const device = this.device();
    if (!device) return { uploads: [] };
    let text = "";
    try { text = await tailFile(path.join(this.config.assets.logsDir, `${device.device_id}.jsonl`), 512 * 1024); } catch {}
    const uploads = text.split("\n").filter(Boolean).slice(-20).reverse().map((line) => {
      try { const item = JSON.parse(line); return { at: item.received_at, events: (item.events || []).slice(0, 400) }; }
      catch { return null; }
    }).filter(Boolean);
    return { uploads };
  }

  // --- Firmware ---
  async firmware() {
    const root = path.join(this.config.firmware.rootDir, this.config.firmware.deviceClass);
    let offered = null;
    try { offered = JSON.parse(await readFile(this.runtime.firmwareManifestPath(), "utf8")); } catch {}
    const device = this.device();
    const kept = await images(root);
    const archiveDir = this.config.firmware.archiveDir || path.join(path.dirname(this.config.configPath || this.config.stateDir), "firmware-archive");
    const archived = await images(archiveDir);
    const sizes = [...kept, ...archived].sort((a, b) => versionKey(a.version).localeCompare(versionKey(b.version))).slice(-20)
      .map(({ version, size }) => ({ version, size }));
    return {
      installed: device ? this.runtime.deviceFirmwareReports.get(device.device_id) || null : null,
      offered: offered ? { version: offered.version, size: offered.size, built_at: offered.built_at, git: offered.build_git_sha || "", notes: offered.notes || "" } : null,
      slot_bytes: Number(this.config.firmware.appSlotBytes) || APP_SLOT_BYTES,
      kept,
      keep: Math.max(1, Math.floor(Number(this.config.firmware.keepImages)) || 3),
      archive: { count: archived.length, bytes: archived.reduce((sum, item) => sum + item.size, 0) },
      sizes,
      min_battery: this.config.firmware.minBattery,
      confirm_required: this.config.firmware.confirmRequired
    };
  }

  // --- Settings ---
  readSettings() {
    const cc = this.config.upstream?.claudeCode || {};
    const kokoro = this.config.omnibotShim?.ttsKokoro || {};
    return {
      claude: { model: cc.model || "", effort: cc.effort || "", web: cc.webAccess !== false, firmware: cc.firmwareUpdates === true,
        models: MODELS, efforts: EFFORTS, provider: this.config.upstream?.provider || "" },
      voice: { voice: kokoro.voice || "", speed: Number(kokoro.speed) || 1, voices: VOICES, on: Boolean(this.runtime.kokoro) },
      storage: { max_bytes: this.storageBudget.maxBytes, choices: STORAGE_CHOICES },
      firmware: { keep_images: Math.max(1, Math.floor(Number(this.config.firmware.keepImages)) || 3) },
      home_door: { port: this.runtime.podcasts?.lanPort || 0 },
      cameras: { missing: this.runtime.cameras?.missing?.() || "" },
      can_restart: this.settings.allowRestart === true,
      can_save: Boolean(this.config.configPath)
    };
  }

  // Only these, checked, are written; everything else in the file stays exactly as it was.
  async saveSettings(body) {
    if (!this.config.configPath) throw Object.assign(new Error("no_config_file"), { status: 409 });
    const raw = JSON.parse(await readFile(this.config.configPath, "utf8"));
    const changed = [];
    const set = (object, key, value, name) => { if (object[key] !== value) { object[key] = value; changed.push(name); } };
    const c = body?.claude || {};
    raw.upstream ||= {}; raw.upstream.claudeCode ||= {};
    if (c.model !== undefined) { if (!MODELS.includes(c.model)) throw Object.assign(new Error("bad_model"), { status: 400 }); set(raw.upstream.claudeCode, "model", c.model, "model"); }
    if (c.effort !== undefined) { if (!EFFORTS.includes(c.effort)) throw Object.assign(new Error("bad_effort"), { status: 400 }); set(raw.upstream.claudeCode, "effort", c.effort, "effort"); }
    if (c.web !== undefined) set(raw.upstream.claudeCode, "webAccess", Boolean(c.web), "web access");
    if (c.firmware !== undefined) set(raw.upstream.claudeCode, "firmwareUpdates", Boolean(c.firmware), "firmware work");
    const v = body?.voice || {};
    if (v.voice !== undefined || v.speed !== undefined) { raw.omnibotShim ||= {}; raw.omnibotShim.ttsKokoro ||= {}; }
    if (v.voice !== undefined) { if (!VOICES.includes(v.voice)) throw Object.assign(new Error("bad_voice"), { status: 400 }); set(raw.omnibotShim.ttsKokoro, "voice", v.voice, "voice"); }
    if (v.speed !== undefined) {
      const speed = Math.round(Number(v.speed) * 100) / 100;
      if (!(speed >= 0.5 && speed <= 2)) throw Object.assign(new Error("bad_speed"), { status: 400 });
      set(raw.omnibotShim.ttsKokoro, "speed", speed, "speed");
    }
    if (body?.storage?.max_bytes !== undefined) {
      const bytes = Number(body.storage.max_bytes);
      if (!STORAGE_CHOICES.includes(bytes)) throw Object.assign(new Error("bad_storage"), { status: 400 });
      raw.storage ||= {}; set(raw.storage, "maxBytes", bytes, "storage");
    }
    if (body?.firmware?.keep_images !== undefined) {
      const keep = Number(body.firmware.keep_images);
      if (!Number.isInteger(keep) || keep < 1 || keep > 10) throw Object.assign(new Error("bad_keep"), { status: 400 });
      raw.firmware ||= {}; set(raw.firmware, "keepImages", keep, "builds kept");
    }
    if (!changed.length) return { ok: true, changed, restarting: false };
    const temp = `${this.config.configPath}.${process.pid}.tmp`;
    await writeFile(temp, `${JSON.stringify(raw, null, 2)}\n`, { mode: 0o600 });
    await rename(temp, this.config.configPath);
    console.info(`[ui] settings saved from the web page: ${changed.join(", ")}`);
    const restart = this.settings.allowRestart === true ? this.requestRestart() : { restarting: false };
    return { ok: true, changed, ...restart };
  }

  cameraLink() {
    const lines = this.runtime.cameras?.setupLines?.() || "none\tCameras aren't set up on the Mac\n";
    const [kind, value, seconds] = lines.trim().split("\t");
    if (kind !== "setup") return { link: null, reason: value || "" };
    const base = String(this.config.installation?.publicUrl || "").replace(/\/+$/u, "");
    return { link: base ? `${base}${value}` : null, path: value, expires_in_s: Number(seconds) || 0 };
  }

  requestRestart() {
    if (this.settings.allowRestart !== true || typeof this.restart !== "function") return { restarting: false, reason: "not_allowed" };
    const topic = this.topic();
    if (topic && this.busy(topic.topic_key)) return { restarting: false, reason: "claude_busy" };
    setTimeout(() => this.restart("the web page"), 300).unref?.();
    return { restarting: true };
  }
}
