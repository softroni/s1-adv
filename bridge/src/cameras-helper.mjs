#!/usr/bin/env node
// The camera helper: the bridge starts it (cameras.mjs) and talks to it over 127.0.0.1 only.
// It signs in to eufy with the account in the bridge's eufy.env, keeps the cameras' last
// pictures, and turns a live camera's video into small JPEGs for the Cardputer.
//
// It runs on Node 24 because the eufy library (@mega-yfue/eufy-sdk, Apache-2.0) needs it, and the
// library lives in the bridge's data folder, not in this package, so the bridge itself needs
// neither. Nothing it logs names a camera, its serial number or the account.
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { mkdir, readFile, rename, stat, writeFile, unlink } from "node:fs/promises";
import path from "node:path";
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import {
  CAMERA_ID, DIGITAL_ZOOM, PICTURE, RAW, THUMB, cameraId, deviceDecodes, dither332, liveArgs, modeWord, panRect, pictureArgs, rectMille,
  renderRect, scrub, shownName, thumbArgs, viewRect, zoomPlan
} from "./cameras-media.mjs";

const DIR = process.env.CAMERAS_DIR || "";
const ENV_FILE = process.env.CAMERAS_ENV || "";
const SECRET = process.env.CAMERAS_SECRET || "";
const FFMPEG = process.env.CAMERAS_FFMPEG || "ffmpeg";
const PICTURES = path.join(DIR, "pictures");
const STATE_FILE = path.join(DIR, "state.json");
const SESSION_FILE = path.join(DIR, "eufy-session.json");

const LIVE_IDLE_MS = 6000;          // No frame asked for this long: the Cardputer has gone, so stop.
const LIVE_MAX_MS = 10 * 60 * 1000; // However often it's extended, a live view ends after this.
const LIVE_EXTEND_MS = 55 * 1000;   // An extend from the device keeps it going this much longer.
const FRAME_WAIT_MS = 4000;         // A frame request waits this long for a new frame.
const LIVE_QUALITY = 70;            // jpeg-js's quality for live pictures: about 7 KB each.
const RETRY_MS = 10 * 60 * 1000;    // A failed sign-in is tried again this rarely, or when eufy.env changes.

const serials = new Set();
const log = (text) => console.log(`[cameras] ${scrub(text, [...serials])}`);
const warn = (text) => console.warn(`[cameras] ${scrub(text, [...serials])}`);

class HelperError extends Error {
  constructor(code, status = 503) { super(code); this.code = code; this.status = status; }
}

// --- The eufy library, from the data folder -------------------------------------------------
async function loadSdk() {
  const root = path.join(DIR, "node_modules", "@mega-yfue", "eufy-sdk");
  const pkg = JSON.parse(await readFile(path.join(root, "package.json"), "utf8"));
  const entry = typeof pkg.exports === "string" ? pkg.exports
    : pkg.exports?.["."]?.import || pkg.exports?.["."]?.default || pkg.exports?.["."] || pkg.main;
  if (typeof entry !== "string") throw new Error("eufy library has no entry point");
  return import(pathToFileURL(path.join(root, entry)).href);
}

// jpeg-js (BSD-3-Clause), which the eufy library brings: the live pictures' encoder. Its 4:4:4
// baseline JPEGs are ones the device's decoder takes, and it adds no frame of delay as ffmpeg does.
let jpeg = null;
function loadJpeg() {
  jpeg ||= createRequire(path.join(DIR, "package.json"))("jpeg-js");
  return jpeg;
}

// --- The account ---------------------------------------------------------------------------
export function parseEnv(text) {
  const out = {};
  for (const raw of String(text || "").split(/\r?\n/u)) {
    const line = raw.trim();
    if (!line || line.startsWith("#")) continue;
    const at = line.indexOf("=");
    if (at <= 0) continue;
    let value = line.slice(at + 1).trim();
    if (value.length >= 2 && (value[0] === "\"" || value[0] === "'") && value.at(-1) === value[0]) value = value.slice(1, -1);
    out[line.slice(0, at).trim()] = value;
  }
  return { email: out.EUFY_EMAIL || "", password: out.EUFY_PASSWORD || "", country: (out.EUFY_COUNTRY || "US").toUpperCase() };
}
async function envChangedAt() {
  try { return (await stat(ENV_FILE)).mtimeMs; } catch { return 0; }
}

// --- State ---------------------------------------------------------------------------------
let sdk = null;
let eufy = null;
const status = { state: "starting", detail: "" };
const cameras = new Map();   // id -> { sn, name, dev, station }
const stations = new Map();  // sn -> dev
let saved = { motion: {} };  // id -> ms of the last motion, kept across restarts
let lastTried = 0, triedEnv = 0, signingIn = false, signInAgain = false, freshAgain = false;
let live = null;

function setState(state, detail = "") {
  if (status.state !== state || status.detail !== detail) log(`state ${state}${detail ? ` (${detail})` : ""}`);
  status.state = state;
  status.detail = detail;
}

async function loadSaved() {
  try { saved = { motion: {}, ...JSON.parse(await readFile(STATE_FILE, "utf8")) }; } catch { saved = { motion: {} }; }
}
let saveTimer = null;
function saveSoon() {
  if (saveTimer) return;
  saveTimer = setTimeout(async () => {
    saveTimer = null;
    try { await writeAtomic(STATE_FILE, JSON.stringify(saved)); } catch (error) { warn(`state not saved: ${error.code || error.message}`); }
  }, 2000);
}
async function writeAtomic(file, data) {
  const temp = `${file}.${process.pid}.tmp`;
  await writeFile(temp, data, { mode: 0o600 });
  await rename(temp, file);
}

// --- Signing in ----------------------------------------------------------------------------
async function signIn(reason) {
  if (signingIn) { signInAgain = true; return; }
  signingIn = true;
  try {
    lastTried = Date.now();
    triedEnv = await envChangedAt();
    let account;
    try { account = parseEnv(await readFile(ENV_FILE, "utf8")); } catch { account = parseEnv(""); }
    if (!account.email || !account.password) { setState("setup", "Add the eufy sign-in on the Mac"); return; }
    if (eufy) { try { await eufy.disconnect(); } catch {} eufy = null; }
    sdk ||= await loadSdk();
    eufy = new sdk.EufyMega({
      email: account.email, password: account.password, countryCode: account.country,
      store: new sdk.FileSessionStore(SESSION_FILE), ffmpegPath: FFMPEG,
      logger: {
        debug: () => {}, info: () => {},
        warn: (m) => { if (process.env.CAMERAS_DEBUG) warn(`eufy: ${m}`); },
        error: (m) => warn(`eufy: ${m}`),
      },
    });
    eufy.on("error", (error) => warn(`eufy error: ${error?.code || error?.message || error}`));
    eufy.on("sessionExpired", () => { setState("signing-in"); void signIn("expired"); });
    eufy.on("motion", (e) => noteMotion(e?.deviceSn));
    eufy.on("personDetected", (e) => noteMotion(e?.deviceSn));
    setState("signing-in");
    log(`signing in (${reason})`);
    await afterLogin(await eufy.login());
  } catch (error) {
    setState("error", shortError(error));
  } finally {
    signingIn = false;
    if (signInAgain) {
      signInAgain = false;
      const fresh = freshAgain;
      freshAgain = false;
      void (fresh ? forgetSession() : Promise.resolve()).then(() => signIn("again"));
    }
  }
}
// The phone page saved the account: sign in with it now. A new account starts a new session.
// eufy.env changing also wakes the 5-second check; whichever comes first signs in, and the other
// leaves it alone, because each sign-in has eufy send another code and only the last one works.
async function signInNow(fresh) {
  const changed = await envChangedAt();
  if (signingIn && !fresh && triedEnv === changed) return;
  if (live) await liveStop("signing in again");
  if (signingIn) { signInAgain = true; freshAgain ||= fresh; return; }
  if (fresh) await forgetSession();
  void signIn("phone page");
}
async function forgetSession() {
  try { await eufy?.disconnect(); } catch {}
  eufy = null;
  try { await unlink(SESSION_FILE); } catch {}
}
function shortError(error) {
  const text = scrub(error?.code || error?.message || error, [...serials]);
  if (/password|credential|10003|26006|unauthor/iu.test(text)) return "eufy refused the sign-in";
  return text.slice(0, 60) || "eufy sign-in failed";
}
async function afterLogin(result) {
  if (result?.status === sdk.LoginStatus.Ok) {
    setState("ready");
    await loadDevices();
  } else if (result?.status === sdk.LoginStatus.TwoFactor) {
    // The library asks eufy to email it (its message_type 2), to the account's address.
    setState("code", "eufy emailed you a code");
  } else if (result?.status === sdk.LoginStatus.Captcha) {
    try {
      const png = Buffer.from(String(result.image || "").replace(/^data:image\/\w+;base64,/u, ""), "base64");
      await writeFile(path.join(DIR, "captcha.png"), png, { mode: 0o600 });
    } catch {}
    setState("captcha", "eufy wants a captcha, on the Mac");
  } else {
    setState("error", "eufy sign-in failed");
  }
}
async function submitCode(code) {
  if (status.state !== "code" || !eufy) throw new HelperError("no_code_asked", 409);
  if (!/^\d{4,8}$/u.test(code)) throw new HelperError("bad_code", 400);
  setState("signing-in");
  try { await afterLogin(await eufy.submitVerifyCode(code)); } catch (error) { setState("code", "That code didn't work"); throw new HelperError("wrong_code", 400); }
}
async function submitCaptcha(answer) {
  if (status.state !== "captcha" || !eufy) throw new HelperError("no_captcha_asked", 409);
  setState("signing-in");
  try { await afterLogin(await eufy.solveCaptcha(String(answer).slice(0, 16))); } catch { setState("error", "The captcha didn't work"); }
}

// --- Devices -------------------------------------------------------------------------------
async function loadDevices() {
  const records = await eufy.getDevices();
  cameras.clear();
  stations.clear();
  for (const record of records) {
    serials.add(record.sn);
    let dev;
    try { dev = await eufy.getDevice(record.sn); } catch { continue; }
    if (dev.camera?.()) cameras.set(cameraId(record.sn), { sn: record.sn, name: record.name || record.model, dev, station: record.stationSn });
    else if (record.deviceClass === "homebase" || dev.has?.("arming")) stations.set(record.sn, dev);
  }
  log(`${cameras.size} camera(s), ${stations.size} base(s)`);
  for (const station of stations.values()) {
    let caps = "";
    try { caps = (station.capabilities || []).join(","); } catch {}
    log(`base caps=${caps || "-"} mode=${modeWord(modeOf(station)) || JSON.stringify(modeOf(station) ?? null).slice(0, 40)}`);
  }
}
function noteMotion(serial) {
  if (!serial) return;
  saved.motion[cameraId(serial)] = Date.now();
  saveSoon();
}
// getProperty answers { name, paramType, value, ts }; the typed getters answer the value itself.
function property(dev, name) {
  try {
    const got = dev?.getProperty?.(name);
    return got && typeof got === "object" && "value" in got ? got.value : got;
  } catch { return undefined; }
}
function batteryOf(dev) {
  try { const level = Number(dev?.battery?.()?.level); if (Number.isFinite(level)) return level; } catch {}
  return Number(property(dev, "battery"));
}
function modeOf(station) {
  try { const mode = station?.arming?.()?.mode; if (mode !== undefined && mode !== null) return mode; } catch {}
  return property(station, "armingMode");
}
async function pictureAt(id) {
  try { return (await stat(path.join(PICTURES, `${id}.jpg`))).mtimeMs; } catch { return 0; }
}
async function list() {
  const out = { state: status.state, detail: status.detail, cameras: [] };
  if (status.state !== "ready") return out;
  for (const station of stations.values()) {
    const mode = modeOf(station);
    if (mode !== undefined && mode !== null) { out.base = { mode }; break; }
  }
  for (const [id, cam] of cameras) {
    const battery = batteryOf(cam.dev);
    out.cameras.push({
      id, name: shownName(cam.name), ptz: Boolean(cam.dev.has?.("ptz") && cam.dev.ptz?.()?.rotate), zoom: hasLens(cam),
      off: cam.dev.camera?.()?.enabled === false,
      battery: Number.isFinite(battery) ? Math.round(battery) : undefined,
      motionAt: saved.motion[id] || 0, pictureAt: await pictureAt(id),
    });
  }
  out.cameras.sort((a, b) => a.name.localeCompare(b.name));
  return out;
}
function cameraFor(id) {
  if (status.state !== "ready") throw new HelperError("not_ready", 409);
  const cam = CAMERA_ID.test(String(id || "")) ? cameras.get(id) : null;
  if (!cam) throw new HelperError("no_camera", 404);
  return cam;
}

// --- Pictures ------------------------------------------------------------------------------
function ffmpeg(args, input, maxBytes = 4 * 1024 * 1024, timeoutMs = 20000) {
  return new Promise((resolve, reject) => {
    const child = spawn(FFMPEG, args, { stdio: ["pipe", "pipe", "pipe"] });
    const parts = [];
    let length = 0, errText = "";
    const timer = setTimeout(() => child.kill("SIGKILL"), timeoutMs);
    child.stdout.on("data", (chunk) => { length += chunk.length; if (length <= maxBytes) parts.push(chunk); else child.kill("SIGKILL"); });
    child.stderr.on("data", (chunk) => { errText = (errText + chunk).slice(-300); });
    child.on("error", (error) => { clearTimeout(timer); reject(error); });
    child.on("close", (code) => {
      clearTimeout(timer);
      if (code === 0 && length && length <= maxBytes) resolve(Buffer.concat(parts));
      else reject(new HelperError(`ffmpeg ${code}: ${scrub(errText).slice(0, 80)}`, 502));
    });
    child.stdin.on("error", () => {});
    child.stdin.end(input);
  });
}
async function thumbFrom(picture) {
  const rgb = await ffmpeg(thumbArgs(), picture, THUMB.bytes * 3 + 16);
  if (rgb.length !== THUMB.bytes * 3) throw new HelperError("thumb_size", 502);
  return Buffer.from(dither332(rgb, THUMB.width, THUMB.height));
}
// A picture already the screen's size (a live frame) goes as it is; anything else is resized.
async function savePicture(id, jpeg, sized = false) {
  const picture = sized ? jpeg : await ffmpeg(pictureArgs(), jpeg, 512 * 1024);
  const thumb = await thumbFrom(picture);
  await mkdir(PICTURES, { recursive: true, mode: 0o700 });
  await writeAtomic(path.join(PICTURES, `${id}.332`), thumb);
  await writeAtomic(path.join(PICTURES, `${id}.jpg`), picture);
  return picture;
}
async function readPicture(id, kind) {
  cameraFor(id);
  let data;
  try { data = await readFile(path.join(PICTURES, `${id}.${kind}`)); } catch { throw new HelperError("no_picture", 404); }
  // One kept by 0.2.315 or 0.2.316 is in a layout the device can't draw: made again, once.
  if (kind === "jpg" && !deviceDecodes(data)) {
    try { data = await savePicture(id, data); log(`picture ${id} remade for the device`); } catch (error) { warn(`picture ${id} not remade: ${error.code || error.message}`); }
  }
  return data;
}
async function refresh(id) {
  const cam = cameraFor(id);
  if (live?.id === id && live.raw) {
    return savePicture(id, jpeg.encode({ data: renderRect(live.raw, viewRect(1)), width: PICTURE.width, height: PICTURE.height }, LIVE_QUALITY).data, true);
  }
  const media = cam.dev.camera?.();
  if (!media?.snapshotLive) throw new HelperError("no_still", 409);
  const began = Date.now();
  let shot;
  try { shot = await media.snapshotLive({ timeoutMs: 25000 }); } catch (error) {
    warn(`still ${id} failed after ${Date.now() - began} ms: ${error?.code || error?.name || error?.message}`);
    throw new HelperError(isOff(error) ? "camera_off" : "no_still", 503);
  }
  log(`still ${id} in ${Date.now() - began} ms${shot.retained ? " (the kept one)" : ""}`);
  return savePicture(id, shot.jpeg);
}
function isOff(error) {
  return Boolean(sdk?.CameraDisabledError && error instanceof sdk.CameraDisabledError);
}
async function turn(id, direction) {
  const cam = cameraFor(id);
  if (!["left", "right", "up", "down"].includes(direction)) throw new HelperError("bad_direction", 400);
  const ptz = cam.dev.ptz?.();
  if (!ptz?.rotate) throw new HelperError("no_ptz", 409);
  await ptz.rotate(direction);
}

// --- Live ----------------------------------------------------------------------------------
async function liveStart(id) {
  const cam = cameraFor(id);
  if (live?.id === id && !live.ended) { live.askedAt = Date.now(); return; }
  if (live) await liveStop("another camera");
  const media = cam.dev.camera?.();
  if (!media?.live) throw new HelperError("no_live", 409);
  // raw is the newest decoded frame, filling into next; shot is the screen's picture of it at the
  // zoom and place asked for, and seq counts them. optical is the lens the camera was asked for,
  // digital the crop on top of it, rect where that crop is.
  const view = { id, raw: null, next: Buffer.alloc(RAW.bytes), filled: 0, shot: null, seq: 0, rgba: null,
    optical: 1, digital: 1, tenths: 10, rect: viewRect(1), waiters: [], startedAt: Date.now(), askedAt: Date.now(),
    extendUntil: Date.now() + LIVE_EXTEND_MS, ff: null, codec: "", frames: 0, ended: false, stream: null, cam };
  live = view;
  log(`live ${id} starting`);
  let stream;
  try {
    stream = await media.live({ batteryBudgetMs: 60000, budgetGraceMs: 10000 });
  } catch (error) {
    if (live === view) live = null;
    warn(`live ${id} failed: ${error?.code || error?.name || error?.message}`);
    throw new HelperError(isOff(error) ? "camera_off" : "no_live", 503);
  }
  view.stream = stream;
  if (view.ended) { stream.stop(); return; }
  stream.on("video", (frame) => onVideo(view, frame));
  stream.on("budget", (notice) => {
    if (Date.now() < view.extendUntil && Date.now() - view.startedAt < LIVE_MAX_MS) notice.extend();
  });
  stream.on("stop", () => void liveEnded(view, "camera stopped"));
  stream.on("error", (error) => { warn(`live ${id}: ${error?.name || error?.message}`); void liveEnded(view, "error"); });
}
// The picture of the newest frame at the view's zoom and place, for the frames waiting on it.
function render(view) {
  if (!view.raw) return;
  view.rgba = renderRect(view.raw, view.rect, view.rgba || undefined);
  view.shot = jpeg.encode({ data: view.rgba, width: PICTURE.width, height: PICTURE.height }, LIVE_QUALITY).data;
  view.seq += 1;
  for (const wake of view.waiters.splice(0)) wake();
}
function onRaw(view, chunk) {
  let at = 0;
  while (at < chunk.length) {
    const take = Math.min(RAW.bytes - view.filled, chunk.length - at);
    chunk.copy(view.next, view.filled, at, at + take);
    view.filled += take;
    at += take;
    if (view.filled === RAW.bytes) {
      [view.raw, view.next] = [view.next, view.raw || Buffer.alloc(RAW.bytes)];
      view.filled = 0;
      view.frames += 1;
      render(view);
    }
  }
}
function onVideo(view, frame) {
  if (view.ended) return;
  if (!view.ff) {
    if (!frame.keyframe) return;
    const args = liveArgs(frame.codec);
    if (!args) { warn(`live ${view.id}: no decoder for ${frame.codec}`); void liveEnded(view, "codec"); return; }
    view.codec = frame.codec;
    const child = spawn(FFMPEG, args, { stdio: ["pipe", "pipe", "pipe"] });
    child.stdout.on("data", (chunk) => onRaw(view, chunk));
    child.stderr.on("data", () => {});
    child.stdin.on("error", () => {});
    child.on("close", () => { if (!view.ended) void liveEnded(view, "decoder stopped"); });
    view.ff = child;
    log(`live ${view.id} ${frame.codec} ${frame.width}x${frame.height}`);
  }
  if (frame.codec !== view.codec) return;  // A switch mid-stream: keep what decodes.
  if (view.ff.stdin.writableLength < 8 * 1024 * 1024) view.ff.stdin.write(frame.data);
}
async function liveEnded(view, why) {
  if (view.ended) return;
  view.ended = true;
  for (const wake of view.waiters.splice(0)) wake();
  // The lens back to wide before the camera's session goes, so it isn't left zoomed for the next.
  if (view.optical !== 1) {
    try { await Promise.race([view.cam.dev.ptz?.()?.zoom?.(1), new Promise(resolve => setTimeout(resolve, 3000))]); } catch {}
  }
  try { view.stream?.stop(); } catch {}
  try { view.ff?.stdin.end(); } catch {}
  setTimeout(() => { try { view.ff?.kill("SIGKILL"); } catch {} }, 2000).unref();
  const seconds = Math.round((Date.now() - view.startedAt) / 1000);
  log(`live ${view.id} ended (${why}) after ${seconds} s, ${view.frames} frames`);
  // The wall keeps the camera's whole view, not the zoomed one.
  if (view.raw) {
    try {
      const whole = renderRect(view.raw, viewRect(1));
      await savePicture(view.id, jpeg.encode({ data: whole, width: PICTURE.width, height: PICTURE.height }, LIVE_QUALITY).data, true);
    } catch (error) { warn(`last frame not kept: ${error.code || error.message}`); }
  }
  if (live === view) live = null;
}
async function liveStop(why) {
  if (live) await liveEnded(live, why);
}
async function liveFrame(id, after) {
  if (!live || live.id !== id) throw new HelperError("not_live", 409);
  const view = live;
  view.askedAt = Date.now();
  if (view.seq <= after && !view.ended) {
    await new Promise((resolve) => {
      const timer = setTimeout(resolve, FRAME_WAIT_MS);
      view.waiters.push(() => { clearTimeout(timer); resolve(); });
    });
  }
  view.askedAt = Date.now();
  if (view.seq > after && view.shot) return { jpeg: view.shot, seq: view.seq };
  if (view.ended) throw new HelperError("not_live", 409);
  return null;
}
// A camera with a second lens zooms with it (the library's ptz().zoom, which eufy's app sends for
// a pinch); the rest of a step is the digital zoom's.
function hasLens(cam) {
  try { return typeof cam.dev.ptz?.()?.zoom === "function"; } catch { return false; }
}
function viewState(view, action = "-") {
  return { tenths: view.tenths, rect: rectMille(view.rect), action };
}
async function liveZoom(id, tenths) {
  if (!live || live.id !== id) throw new HelperError("not_live", 409);
  const view = live;
  view.askedAt = Date.now();
  let plan = zoomPlan(tenths, hasLens(view.cam));
  if (plan.optical !== view.optical) {
    try {
      await view.cam.dev.ptz().zoom(plan.optical);
      view.optical = plan.optical;
      log(`live ${id} lens ${plan.optical}x`);
    } catch (error) {
      warn(`live ${id} lens zoom failed: ${error?.code || error?.name || error?.message}`);
      plan = zoomPlan(Math.round(tenths / view.optical), false);
      plan = { ...plan, tenths: plan.tenths * view.optical };
    }
  }
  view.digital = DIGITAL_ZOOM[plan.branch];
  view.tenths = plan.tenths;
  view.rect = viewRect(view.digital, view.rect.cx, view.rect.cy);  // It stays where it was looking.
  render(view);
  return viewState(view);
}
// An arrow: the view moves inside the zoomed picture; at its edge, or with turn, a camera that
// pans and tilts turns that way instead.
async function livePan(id, direction, turnOnly) {
  if (!live || live.id !== id) throw new HelperError("not_live", 409);
  if (!["left", "right", "up", "down"].includes(direction)) throw new HelperError("bad_direction", 400);
  const view = live;
  view.askedAt = Date.now();
  if (!turnOnly && view.digital > 1) {
    const step = panRect(view.rect, direction, view.digital);
    if (step.moved) { view.rect = step.rect; render(view); return viewState(view, "moved"); }
  }
  const ptz = view.cam.dev.ptz?.();
  if (!ptz?.rotate) return viewState(view, "edge");
  await ptz.rotate(direction);
  return viewState(view, "turned");
}
function liveExtend(id) {
  if (!live || live.id !== id) throw new HelperError("not_live", 409);
  live.askedAt = Date.now();
  live.extendUntil = Date.now() + LIVE_EXTEND_MS;
}
function startTimers() {
  // A live view ends when nobody asks for its frames, when it runs too long, or when the device
  // stopped extending it.
  setInterval(() => {
    if (!live || live.ended) return;
    const now = Date.now();
    if (now - live.askedAt > LIVE_IDLE_MS) void liveEnded(live, "nobody watching");
    else if (now - live.startedAt > LIVE_MAX_MS) void liveEnded(live, "too long");
    else if (now > live.extendUntil + 15000) void liveEnded(live, "not extended");
  }, 1000).unref();
  // Sign in again when eufy.env changes, or now and then after a failure.
  setInterval(async () => {
    if (signingIn || status.state === "ready" || status.state === "code" || status.state === "captcha") return;
    const changed = await envChangedAt();
    if (changed !== triedEnv || Date.now() - lastTried > RETRY_MS) void signIn(changed !== triedEnv ? "eufy.env changed" : "retry");
  }, 5000).unref();
  // The device list, now and then: names change, cameras come and go.
  setInterval(() => {
    if (status.state === "ready" && !live) loadDevices().catch((e) => warn(`devices: ${e?.code || e?.message}`));
  }, 30 * 60 * 1000).unref();
}

// --- The bridge's door ---------------------------------------------------------------------
function send(res, status, body, type = "application/json") {
  const data = Buffer.isBuffer(body) ? body : Buffer.from(typeof body === "string" ? body : JSON.stringify(body));
  res.writeHead(status, { "content-type": type, "content-length": String(data.length), "cache-control": "no-store" });
  res.end(data);
}
export function handler() {
  return (req, res) => void (async () => {
    if (!SECRET || req.headers["x-cameras-secret"] !== SECRET) { send(res, 403, { error: "forbidden" }); return; }
    const url = new URL(req.url || "/", "http://helper");
    const q = url.searchParams;
    const cam = q.get("cam") || "";
    try {
      switch (`${req.method} ${url.pathname}`) {
        case "GET /status": send(res, 200, status); return;
        case "GET /list": send(res, 200, await list()); return;
        case "GET /thumb": send(res, 200, await readPicture(cam, "332"), "application/octet-stream"); return;
        case "GET /picture": send(res, 200, await readPicture(cam, "jpg"), "image/jpeg"); return;
        case "POST /refresh": send(res, 200, await refresh(cam), "image/jpeg"); return;
        case "POST /turn": await turn(cam, q.get("dir") || ""); send(res, 200, { ok: true }); return;
        case "POST /live/start": await liveStart(cam); send(res, 200, { ok: true }); return;
        case "GET /live/frame": {
          const got = await liveFrame(cam, Number(q.get("after")) || 0);
          if (!got) { res.writeHead(204, { "cache-control": "no-store" }); res.end(); return; }
          res.writeHead(200, { "content-type": "image/jpeg", "content-length": String(got.jpeg.length), "x-seq": String(got.seq), "cache-control": "no-store" });
          res.end(got.jpeg);
          return;
        }
        case "POST /live/extend": liveExtend(cam); send(res, 200, { ok: true }); return;
        case "POST /live/zoom": send(res, 200, await liveZoom(cam, Number(q.get("x")) || 10)); return;
        case "POST /live/pan": send(res, 200, await livePan(cam, q.get("dir") || "", q.get("turn") === "1")); return;
        case "POST /live/stop": if (live?.id === cam) await liveStop("closed"); send(res, 200, { ok: true }); return;
        case "POST /code": await submitCode(q.get("value") || ""); send(res, 200, status); return;
        case "POST /captcha": await submitCaptcha(q.get("value") || ""); send(res, 200, status); return;
        case "POST /signin": await signInNow(q.get("fresh") === "1"); send(res, 200, status); return;
        default: send(res, 404, { error: "not_found" });
      }
    } catch (error) {
      if (res.headersSent) { res.destroy(); return; }
      const code = error instanceof HelperError ? error.code : "failed";
      if (!(error instanceof HelperError)) warn(`${url.pathname} failed: ${error?.code || error?.message || error}`);
      send(res, error instanceof HelperError ? error.status : 500, { error: code });
    }
  })();
}

async function main() {
  if (!DIR || !SECRET) { console.error("[cameras] CAMERAS_DIR and CAMERAS_SECRET are needed"); process.exit(2); }
  await mkdir(PICTURES, { recursive: true, mode: 0o700 });
  await loadSaved();
  loadJpeg();
  startTimers();
  const server = createServer(handler());
  server.listen(0, "127.0.0.1", () => {
    // The bridge reads this line for the port.
    process.stdout.write(`listening ${server.address().port}\n`);
  });
  const stop = async () => { try { await liveStop("helper stopping"); } catch {} try { await eufy?.disconnect(); } catch {} process.exit(0); };
  process.once("SIGTERM", stop);
  process.once("SIGINT", stop);
  // The bridge going away closes stdin: so does the helper.
  process.stdin.on("end", stop);
  process.stdin.resume();
  await signIn("start");
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname)) void main();
