import assert from "node:assert/strict";
import { chmod, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { KOKORO_DEFAULTS, KokoroVoice, kokoroSettings } from "./kokoro-tts.mjs";
import { BridgeRuntime } from "./server.mjs";

const bridgeRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

// Stands in for the Python worker and speaks the same framed protocol.
const FAKE_PYTHON = `#!${process.execPath}
const fs = require("fs"), path = require("path"), readline = require("readline");
const here = path.dirname(process.argv[1]);
fs.appendFileSync(path.join(here, "starts.jsonl"), JSON.stringify({ argv: process.argv.slice(2), env: Object.keys(process.env) }) + "\\n");
const frame = (pcm, rate) => { const head = Buffer.alloc(8); head.writeUInt32LE(pcm.length, 0); head.writeUInt32LE(rate, 4); return Buffer.concat([head, pcm]); };
readline.createInterface({ input: process.stdin }).on("line", line => {
  const { text } = JSON.parse(line);
  fs.appendFileSync(path.join(here, "requests.txt"), text + "\\n");
  if (text === "crash") process.exit(7);
  if (text === "hang") return;
  if (text === "fail") { process.stdout.write(frame(Buffer.alloc(0), 0)); return; }
  const pcm = Buffer.alloc(48000);
  for (let i = 0; i < pcm.length / 2; i += 1) pcm.writeInt16LE(i % 2 ? 1000 : -1000, i * 2);
  const data = frame(pcm, 24000);
  // Arrive in pieces, as a real pipe can.
  process.stdout.write(data.subarray(0, 5));
  setTimeout(() => process.stdout.write(data.subarray(5)), 5);
});
`;

async function fixture(t, overrides = {}) {
  const dir = await mkdtemp(path.join(tmpdir(), "s1-kokoro-"));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const python = path.join(dir, "python");
  await writeFile(python, FAKE_PYTHON);
  await chmod(python, 0o755);
  const settings = { ...KOKORO_DEFAULTS, python, model: "model.onnx", voices: "voices.bin", ...overrides };
  const voice = new KokoroVoice(settings, { worker: path.join(dir, "worker.py"),
    env: { ...process.env, ANTHROPIC_API_KEY: "secret", OPENAI_API_KEY: "secret", CARDPUTER_BRIDGE_CONFIG: "/private" } });
  t.after(() => voice.stop());
  return { dir, voice, settings };
}

test("settings are off by default and reject unsafe values", () => {
  assert.equal(kokoroSettings({ omnibotShim: { ttsFallbackProvider: "espeak" } }), null);
  const config = ttsKokoro => ({ omnibotShim: { ttsFallbackProvider: "kokoro", ttsKokoro } });
  const base = { python: "/p", model: "/m", voices: "/v" };
  assert.equal(kokoroSettings(config(base)).voice, "af_heart");
  assert.throws(() => kokoroSettings(config({ model: "/m", voices: "/v" })), /python/);
  assert.throws(() => kokoroSettings(config({ ...base, voice: "--flag" })), /voice/);
  assert.throws(() => kokoroSettings(config({ ...base, speed: 9 })), /speed/);
});

test("speech frames survive split delivery, and text never reaches the process list", async t => {
  const { voice, dir } = await fixture(t);
  const result = await voice.synthesize("secret words");
  assert.equal(result.sampleRate, 24000);
  assert.equal(result.pcm.length, 48000);
  const [start] = (await readFile(path.join(dir, "starts.jsonl"), "utf8")).trim().split("\n").map(line => JSON.parse(line));
  assert.deepEqual(start.argv.slice(0, 2), ["-I", path.join(dir, "worker.py")], "isolated from user Python packages");
  assert.ok(!JSON.stringify(start.argv).includes("secret words"));
  for (const name of ["ANTHROPIC_API_KEY", "OPENAI_API_KEY", "CARDPUTER_BRIDGE_CONFIG"]) assert.ok(!start.env.includes(name), name);
  assert.ok(start.env.includes("PYTHONNOUSERSITE"));
});

test("the loaded worker is reused and overlapping requests answer in order", async t => {
  const { voice, dir } = await fixture(t);
  const results = await Promise.all(["one", "two", "three"].map(text => voice.synthesize(text)));
  assert.ok(results.every(result => result.pcm.length === 48000));
  assert.equal((await readFile(path.join(dir, "requests.txt"), "utf8")), "one\ntwo\nthree\n");
  assert.equal((await readFile(path.join(dir, "starts.jsonl"), "utf8")).trim().split("\n").length, 1);
});

test("a failed request, a crash and a hang each recover on the next reply", async t => {
  const { voice, dir } = await fixture(t, { requestTimeoutMs: 400, startupTimeoutMs: 10000 });
  await assert.rejects(voice.synthesize("fail"), /kokoro_synthesis_failed/);
  assert.equal((await voice.synthesize("after failure")).pcm.length, 48000);
  await assert.rejects(voice.synthesize("crash"), /kokoro_worker_exit_7/);
  assert.equal((await voice.synthesize("after crash")).pcm.length, 48000);
  await assert.rejects(voice.synthesize("hang"), /kokoro_timeout/);
  assert.equal((await voice.synthesize("after hang")).pcm.length, 48000);
  assert.equal((await readFile(path.join(dir, "starts.jsonl"), "utf8")).trim().split("\n").length, 3);
});

test("a missing program rejects instead of crashing the bridge", async t => {
  const { voice } = await fixture(t, { python: path.join(tmpdir(), "no-such-python-program") });
  await assert.rejects(voice.synthesize("hello"));
});

test("long text is capped and empty text does no work", async t => {
  const { voice, dir } = await fixture(t);
  assert.equal(await voice.synthesize("   "), null);
  await voice.synthesize("x".repeat(5000));
  assert.equal((await readFile(path.join(dir, "requests.txt"), "utf8")).trim().length, 600);
});

async function runtime(t, settings, stateDir) {
  const example = JSON.parse(await readFile(path.join(bridgeRoot, "config.example.json"), "utf8"));
  const bridge = new BridgeRuntime({ ...example, stateDir,
    upstream: { provider: "claude-code", claudeCode: { command: "claude" } },
    omnibotShim: { ...example.omnibotShim, ttsFallbackProvider: "kokoro", ttsKokoro: settings } });
  t.after(() => bridge.kokoro?.stop());
  return bridge;
}

test("speech is resampled for the device, and Russian stays with eSpeak", async t => {
  const { settings, dir } = await fixture(t);
  const bridge = await runtime(t, settings, path.join(dir, "state"));
  bridge.kokoro.worker = path.join(dir, "worker.py");
  const speech = await bridge.synthesizeKokoro("Hello there", 12000);
  assert.equal(speech.sampleRate, 12000);
  assert.equal(speech.pcm.length, 24000);
  const unsupported = await bridge.synthesizeKokoro("Hello there", 16000);
  assert.equal(unsupported.sampleRate, 24000, "the rate must match the samples when it can't divide exactly");
  assert.equal(unsupported.pcm.length, 48000);
  assert.equal(await bridge.synthesizeKokoro("Привет", 12000), null);
});

test("a failed voice lets the bridge fall back instead of losing the reply", async t => {
  const { settings, dir } = await fixture(t);
  const bridge = await runtime(t, settings, path.join(dir, "state"));
  bridge.kokoro.worker = path.join(dir, "worker.py");
  assert.equal(await bridge.synthesizeKokoro("fail", 12000), null);
  const plain = await runtime(t, undefined, path.join(dir, "plain")).catch(error => error);
  assert.match(String(plain), /ttsKokoro.python/);
});
