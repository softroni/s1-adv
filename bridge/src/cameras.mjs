// The Cameras app's side of the bridge. It starts the camera helper (cameras-helper.mjs) on
// Node 24, restarts it if it stops, and answers the Cardputer: over HTTPS with its token, and
// at home over the plain-HTTP door with a ticket, as podcasts do, because a live view's frames
// don't fit beside TLS in the device's memory.
//
// Routes, under /api/cardputer/cameras/ (HTTPS) or /cameras/<ticket>/ (the home door):
//   ticket                     where the home door is, or "none" (HTTPS only)
//   setup                      the phone sign-in page's one-time path, "setup <path> <seconds>",
//                              or "none <why>" (HTTPS only; the page is cameras-setup.mjs)
//   list                       the cameras, as lines (cameras-media.mjs listLines)
//   thumb?cam=                 the wall's 78x44 picture, raw 3-3-2 pixels, 404 when there's none
//   picture?cam=               the last picture, a 240x135 JPEG, 404 when there's none
//   refresh?cam=               wakes the camera for a new picture and answers it
//   live/start?cam=            starts a live view; then live/frame?cam= answers the next frame,
//                              204 when none came in time, 409 once it has ended
//   live/extend?cam=  live/stop?cam=  turn?cam=&dir=left|right|up|down  code?value=  report?...
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { readFile } from "node:fs/promises";
import { findFfmpeg, homeAddress, lanAddresses } from "./podcasts.mjs";
import { CAMERA_ID, listLines } from "./cameras-media.mjs";
import { SetupLinks, accountFile, emailIn, writeAccount } from "./cameras-setup.mjs";

const HELPER = fileURLToPath(new URL("./cameras-helper.mjs", import.meta.url));
const TICKET_MS = 12 * 60 * 60 * 1000;
const MAX_TICKETS = 16;
const SDK_PACKAGE = path.join("node_modules", "@mega-yfue", "eufy-sdk", "package.json");

export class CameraError extends Error {
  constructor(code, status = 503) { super(code); this.code = code; this.status = status; }
}

export function findNode24(configured) {
  for (const candidate of [configured, "/opt/homebrew/opt/node@24/bin/node", "/usr/local/opt/node@24/bin/node"])
    if (candidate && existsSync(candidate)) return candidate;
  return "";
}

const WHAT = new Set(["list", "thumb", "picture", "refresh", "live/start", "live/frame", "live/extend", "live/stop", "turn", "code", "report"]);

export class CameraService {
  constructor({ dir = "", envFile = "", node, ffmpeg, enabled = true, spawnImpl = spawn, fetchImpl = fetch, now = Date.now,
    helperPath = HELPER } = {}) {
    Object.assign(this, { dir, envFile, enabled, spawnImpl, fetchImpl, now, helperPath });
    this.node = findNode24(node);
    this.ffmpeg = findFfmpeg(ffmpeg);
    this.port = 0;
    this.secret = "";
    this.child = null;
    this.stopping = false;
    this.restartMs = 5000;
    this.tickets = new Map();
    this.sent = new Map();   // device + camera -> the last live frame it was sent
    this.lanPort = 0;
    this.links = new SetupLinks(now);
  }

  // The phone page's link for the device to show as a QR code.
  setupLines() {
    const missing = this.missing();
    if (missing) return `none\t${missing}\n`;
    const link = this.links.current();
    return `setup\t/eufy/${link.token}\t${Math.round((link.expires - this.now()) / 1000)}\n`;
  }

  // The account from the phone page: into eufy.env, and the helper signs in with it now. A new
  // email starts a new session rather than keep the last account's.
  async saveAccount(account) {
    if (this.missing()) throw new CameraError("not_set_up", 503);
    if (!this.envFile) throw new CameraError("no_account_file", 503);
    const text = accountFile(account);
    let old = "";
    try { old = await readFile(this.envFile, "utf8"); } catch {}
    await writeAccount(this.envFile, text);
    console.log("[cameras] account saved from the phone page");
    // Only a different account needs its session forgotten; a first sign-in has none.
    const before = emailIn(old);
    if (this.port) await this.call("POST", "/signin", before && before !== emailIn(text) ? { fresh: "1" } : {}, 10000);
  }

  // Why the helper can't run, for the device's screen, or "" when it can.
  missing() {
    if (this.enabled === false) return "Cameras are turned off on the Mac";
    if (!this.dir || !existsSync(path.join(this.dir, SDK_PACKAGE))) return "Cameras aren't set up on the Mac";
    if (!this.node) return "The Mac needs Node 24 for cameras";
    return "";
  }

  start() {
    if (this.child || this.stopping || this.missing()) return false;
    this.secret = randomBytes(24).toString("hex");
    this.port = 0;
    const env = {
      PATH: process.env.PATH || "/usr/bin:/bin", HOME: process.env.HOME || "", LANG: process.env.LANG || "en_US.UTF-8",
      TMPDIR: process.env.TMPDIR || "/tmp", CAMERAS_DIR: this.dir, CAMERAS_ENV: this.envFile,
      CAMERAS_SECRET: this.secret, CAMERAS_FFMPEG: this.ffmpeg,
      ...(process.env.CAMERAS_DEBUG ? { CAMERAS_DEBUG: "1" } : {}),
    };
    const child = this.spawnImpl(this.node, [this.helperPath], { cwd: this.dir, env, stdio: ["pipe", "pipe", "pipe"] });
    this.child = child;
    let out = "";
    child.stdout.on("data", (chunk) => {
      out += chunk;
      let at;
      while ((at = out.indexOf("\n")) >= 0) {
        const line = out.slice(0, at);
        out = out.slice(at + 1);
        const port = /^listening (\d+)$/u.exec(line);
        if (port) { this.port = Number(port[1]); this.restartMs = 5000; console.log(`[cameras] helper ready`); }
        else if (line.trim()) console.log(line.slice(0, 400));
      }
    });
    let err = "";
    child.stderr.on("data", (chunk) => {
      err += chunk;
      let at;
      while ((at = err.indexOf("\n")) >= 0) {
        const line = err.slice(0, at);
        err = err.slice(at + 1);
        if (line.trim()) console.warn(line.startsWith("[cameras]") ? line.slice(0, 400) : `[cameras] ${line.slice(0, 200)}`);
      }
    });
    child.stdin.on("error", () => {});
    child.on("error", (error) => console.warn(`[cameras] helper didn't start: ${error.code || error.message}`));
    child.on("exit", (code, signal) => {
      if (this.child === child) { this.child = null; this.port = 0; }
      if (this.stopping) return;
      console.warn(`[cameras] helper stopped (${signal || code}); again in ${Math.round(this.restartMs / 1000)} s`);
      setTimeout(() => this.start(), this.restartMs).unref?.();
      this.restartMs = Math.min(this.restartMs * 2, 5 * 60 * 1000);
    });
    return true;
  }

  stop() {
    this.stopping = true;
    try { this.child?.stdin.end(); } catch {}
    try { this.child?.kill("SIGTERM"); } catch {}
  }

  // One call to the helper. Answers { status, body, type, seq }.
  async call(method, pathname, params = {}, timeoutMs = 15000) {
    if (!this.port) throw new CameraError(this.missing() ? "not_set_up" : "starting", 503);
    const query = new URLSearchParams(params).toString();
    let response;
    try {
      response = await this.fetchImpl(`http://127.0.0.1:${this.port}${pathname}${query ? `?${query}` : ""}`, {
        method, headers: { "x-cameras-secret": this.secret }, signal: AbortSignal.timeout(timeoutMs) });
    } catch (error) {
      throw new CameraError(error?.name === "TimeoutError" ? "camera_timeout" : "helper_unreachable", 504);
    }
    const body = Buffer.from(await response.arrayBuffer());
    return { status: response.status, body, type: response.headers.get("content-type") || "", seq: Number(response.headers.get("x-seq")) || 0 };
  }

  // The list for the device, which says on its first line why there's nothing when there isn't.
  async listText() {
    const missing = this.missing();
    if (missing) return listLines({ state: "off", detail: missing });
    if (!this.port) return listLines({ state: "starting", detail: "Starting cameras on the Mac" });
    const got = await this.call("GET", "/list");
    if (got.status !== 200) throw new CameraError("list_failed", 502);
    return listLines(JSON.parse(got.body.toString("utf8")), this.now());
  }

  // Answers one of the routes above into res. `device` is the device's id, for its live frames.
  async answer(what, params, device, res) {
    const cam = params.get("cam") || "";
    if (what !== "list" && what !== "report" && what !== "code" && !CAMERA_ID.test(cam)) throw new CameraError("no_camera", 404);
    const bytes = (got, type) => {
      if (got.status === 204) { res.writeHead(204, { "cache-control": "no-store" }); res.end(); return; }
      if (got.status !== 200) throw new CameraError(helperCode(got), got.status === 404 || got.status === 409 || got.status === 400 ? got.status : 502);
      res.writeHead(200, { "content-type": type, "content-length": String(got.body.length), "cache-control": "no-store" });
      res.end(got.body);
    };
    const ok = (got) => {
      if (got.status !== 200) throw new CameraError(helperCode(got), got.status === 404 || got.status === 409 || got.status === 400 ? got.status : 502);
      lines(res, "ok\n");
    };
    switch (what) {
      case "list": lines(res, await this.listText()); return;
      case "thumb": bytes(await this.call("GET", "/thumb", { cam }), "application/octet-stream"); return;
      case "picture": bytes(await this.call("GET", "/picture", { cam }), "image/jpeg"); return;
      case "refresh": bytes(await this.call("POST", "/refresh", { cam }, 45000), "image/jpeg"); return;
      case "live/start": this.sent.delete(`${device}\t${cam}`); ok(await this.call("POST", "/live/start", { cam }, 30000)); return;
      case "live/frame": {
        const key = `${device}\t${cam}`;
        const got = await this.call("GET", "/live/frame", { cam, after: String(this.sent.get(key) || 0) }, 10000);
        if (got.status === 200 && got.seq) this.sent.set(key, got.seq);
        bytes(got, "image/jpeg");
        return;
      }
      case "live/extend": ok(await this.call("POST", "/live/extend", { cam })); return;
      case "live/stop": this.sent.delete(`${device}\t${cam}`); ok(await this.call("POST", "/live/stop", { cam }, 10000)); return;
      case "turn": ok(await this.call("POST", "/turn", { cam, dir: params.get("dir") || "" })); return;
      case "code": {
        const captcha = params.has("captcha");
        const got = captcha
          ? await this.call("POST", "/captcha", { value: String(params.get("captcha") || "").replace(/[^A-Za-z0-9]+/gu, "").slice(0, 8) }, 30000)
          : await this.call("POST", "/code", { value: String(params.get("value") || "").replace(/\D+/gu, "").slice(0, 8) }, 30000);
        lines(res, got.status === 200 ? "ok\n" : `${helperCode(got)}\n`);
        return;
      }
      case "report": report(params); lines(res, "ok\n"); return;
      default: throw new CameraError("not_found", 404);
    }
  }

  // A ticket for the home door: one per device, good for 12 hours.
  ticket(device) {
    const now = this.now();
    for (const [id, held] of this.tickets) if (held.expires <= now) this.tickets.delete(id);
    for (const [id, held] of this.tickets) if (held.device === device) { held.expires = now + TICKET_MS; return id; }
    while (this.tickets.size >= MAX_TICKETS) this.tickets.delete(this.tickets.keys().next().value);
    const id = randomBytes(16).toString("hex");
    this.tickets.set(id, { device, expires: now + TICKET_MS });
    return id;
  }
  redeem(id) {
    const held = this.tickets.get(id);
    return held && held.expires > this.now() ? held : null;
  }
  ticketLines(device, interfaces) {
    const addresses = this.lanPort ? lanAddresses(interfaces) : [];
    if (!addresses.length) return "none\n";
    const id = this.ticket(device);
    return [`ticket\t${this.lanPort}\t/cameras/${id}`, ...addresses.map(entry => `lan\t${entry.address}\t${entry.prefix}`)].join("\n") + "\n";
  }
}

function helperCode(got) {
  try { return String(JSON.parse(got.body.toString("utf8")).error || "failed").replace(/[^a-z_]/gu, "").slice(0, 32) || "failed"; } catch { return "failed"; }
}

function lines(res, body) {
  const data = Buffer.from(body, "utf8");
  res.writeHead(200, { "content-type": "text/plain; charset=utf-8", "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}

// What the device says about itself, for the log: short plain values only.
function report(params) {
  const keep = [];
  for (const key of ["stage", "e", "free", "big", "via", "fps", "ms", "bytes", "fw", "note"]) {
    const value = String(params?.get?.(key) ?? "").replace(/[^A-Za-z0-9 ./:_-]/gu, "").slice(0, 40);
    if (value) keep.push(`${key}=${value.replace(/ /gu, "_")}`);
  }
  console.log(`[cameras] device report ${keep.join(" ") || "(empty)"}`);
}

// Errors for the device: plain text, one word, so it can show the right line.
export function sendCameraError(res, error, where = "") {
  if (res.headersSent || res.destroyed) { res.destroy(); return; }
  const status = error instanceof CameraError ? error.status : 503;
  const code = error instanceof CameraError ? error.code : "cameras_unavailable";
  if (!(error instanceof CameraError) || status >= 500 && code !== "starting" && code !== "not_set_up")
    console.warn(`[cameras] ${where} failed: ${String(error?.code || error?.message || error).slice(0, 120)}`);
  const data = Buffer.from(`${code}\n`, "utf8");
  res.writeHead(status, { "content-type": "text/plain; charset=utf-8", "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}

// The HTTPS side's route, with the device already known from its token. True when it answered.
export async function handleCameraRoute(service, pathname, params, device, res) {
  const what = pathname.slice("/api/cardputer/cameras/".length);
  try {
    if (what === "ticket") { lines(res, service.ticketLines(device)); return; }
    if (what === "setup") { lines(res, service.setupLines()); return; }
    if (!WHAT.has(what)) throw new CameraError("not_found", 404);
    await service.answer(what, params, device, res);
  } catch (error) {
    sendCameraError(res, error, what.slice(0, 16));
  }
}

// The home door's: /cameras/<ticket>/<route>, from home addresses only. False when the path
// isn't a camera one, so the door's other routes can have it.
export function cameraLanRoute(service, req, res) {
  let url;
  try { url = new URL(req.url || "/", "http://lan"); } catch { return false; }
  const match = url.pathname.match(/^\/cameras\/([0-9a-f]{32})\/([a-z/]+)$/u);
  if (!url.pathname.startsWith("/cameras/")) return false;
  const remote = String(req.socket?.remoteAddress || "").replace(/^::ffff:/u, "");
  const held = req.method === "GET" && match && WHAT.has(match[2]) && (homeAddress(remote) || remote === "127.0.0.1")
    ? service.redeem(match[1]) : null;
  if (!held) {
    res.writeHead(404, { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" });
    res.end("not_found\n");
    return true;
  }
  void service.answer(match[2], url.searchParams, held.device, res).catch(error => sendCameraError(res, error, `home ${match[2]}`));
  return true;
}
