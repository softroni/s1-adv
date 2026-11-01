import { spawn } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const WORKER = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "scripts", "kokoro-worker.py");
const CHILD_ENV = /^(HOME|USER|LOGNAME|PATH|TMPDIR|TEMP|TMP|LANG|LC_[A-Z]+)$/;
const MAX_FRAME_BYTES = 32 * 1024 * 1024;
// Longer than the device can buffer, and bounds the work a single request can cause.
const MAX_TEXT_CHARS = 600;

export const KOKORO_DEFAULTS = Object.freeze({ voice: "af_heart", speed: 1.05, requestTimeoutMs: 15000, startupTimeoutMs: 45000 });

export function kokoroSettings(config) {
  const shim = config?.omnibotShim;
  if (String(shim?.ttsFallbackProvider || "").toLowerCase() !== "kokoro") return null;
  const settings = { ...KOKORO_DEFAULTS, ...(shim.ttsKokoro || {}) };
  for (const key of ["python", "model", "voices"])
    if (typeof settings[key] !== "string" || !settings[key]) throw new Error(`omnibotShim.ttsKokoro.${key} is required`);
  if (!/^[a-z]{2}_[a-z]+$/.test(settings.voice)) throw new Error("omnibotShim.ttsKokoro.voice is not a valid voice name");
  if (!(settings.speed >= 0.7 && settings.speed <= 1.4)) throw new Error("omnibotShim.ttsKokoro.speed must be 0.7..1.4");
  return settings;
}

export class KokoroVoice {
  constructor(settings, { spawnImpl = spawn, env = process.env, worker = WORKER } = {}) {
    Object.assign(this, { settings, spawnImpl, env, worker });
    this.child = null;
    this.ready = false;
    this.pending = null;
    this.buffer = Buffer.alloc(0);
    this.queue = Promise.resolve();
  }

  start() {
    if (this.child) return;
    const { python, model, voices, voice, speed, espeakLib, espeakData } = this.settings;
    const child = this.spawnImpl(python, ["-I", this.worker, JSON.stringify({ model, voices, voice, speed, espeakLib, espeakData })], {
      stdio: ["pipe", "pipe", "ignore"],
      env: { ...Object.fromEntries(Object.entries(this.env).filter(([name]) => CHILD_ENV.test(name))), PYTHONNOUSERSITE: "1" }
    });
    this.child = child;
    this.buffer = Buffer.alloc(0);
    child.stdout.on("data", chunk => this.receive(chunk));
    child.stdin.on("error", () => {});
    child.on("error", error => this.lost(child, error));
    child.on("close", code => this.lost(child, new Error(`kokoro_worker_exit_${code}`)));
  }

  stop() {
    this.child?.kill("SIGTERM");
  }

  lost(child, error) {
    if (this.child !== child) return;
    this.child = null;
    this.ready = false;
    this.settle(error);
  }

  // Forget it first: the next reply must start a new worker, not reach a dying one.
  abandon(child, error) {
    this.lost(child, error);
    child.kill("SIGKILL");
  }

  settle(error, value) {
    const pending = this.pending;
    if (!pending) return;
    this.pending = null;
    clearTimeout(pending.timer);
    if (error) pending.reject(error);
    else pending.resolve(value);
  }

  receive(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    if (this.buffer.length < 8) return;
    const size = this.buffer.readUInt32LE(0), sampleRate = this.buffer.readUInt32LE(4);
    if (size > MAX_FRAME_BYTES) {
      this.abandon(this.child, new Error("kokoro_frame_too_large"));
      return;
    }
    if (this.buffer.length < 8 + size) return;
    const pcm = this.buffer.subarray(8, 8 + size);
    this.buffer = this.buffer.subarray(8 + size);
    this.ready = true;
    this.settle(size ? null : new Error("kokoro_synthesis_failed"), { pcm, sampleRate });
  }

  request(text) {
    return new Promise((resolve, reject) => {
      this.start();
      const child = this.child;
      const timeoutMs = this.ready ? this.settings.requestTimeoutMs : this.settings.startupTimeoutMs;
      const timer = setTimeout(() => this.abandon(child, new Error(`kokoro_timeout_${timeoutMs}ms`)), timeoutMs);
      this.pending = { resolve, reject, timer };
      child.stdin.write(`${JSON.stringify({ text })}\n`);
    });
  }

  // One at a time: the worker answers in order and a frame has no request id.
  synthesize(text) {
    const cleaned = String(text || "").trim().slice(0, MAX_TEXT_CHARS);
    if (!cleaned) return Promise.resolve(null);
    const run = () => this.request(cleaned);
    const result = this.queue.then(run, run);
    this.queue = result.catch(() => {});
    return result;
  }
}
