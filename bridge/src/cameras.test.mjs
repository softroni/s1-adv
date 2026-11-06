import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { EventEmitter } from "node:events";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { createServer } from "node:http";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { JpegSplitter, PICTURE, THUMB, cameraId, deviceDecodes, dither332, jpegLayout, listLines, liveArgs, modeWord, pictureArgs, scrub,
  shownName, thumbArgs, to332 } from "./cameras-media.mjs";
import { CameraError, CameraService, cameraLanRoute, handleCameraRoute } from "./cameras.mjs";
import { parseEnv } from "./cameras-helper.mjs";

const FFMPEG = ["/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"].find(p => spawnSync(p, ["-version"]).status === 0);

function fakeRes() {
  const res = new EventEmitter();
  res.status = 0; res.headers = {}; res.body = Buffer.alloc(0); res.headersSent = false; res.destroyed = false;
  res.writeHead = (status, headers = {}) => { res.status = status; res.headers = headers; res.headersSent = true; return res; };
  res.end = (data) => { if (data) res.body = Buffer.concat([res.body, Buffer.from(data)]); res.emit("finish"); };
  res.destroy = () => { res.destroyed = true; };
  return res;
}
const done = (res) => new Promise(resolve => res.once("finish", resolve));

test("a camera's id is short, stable and not its serial", () => {
  const id = cameraId("T9001P0000000000");
  assert.match(id, /^[0-9a-f]{8}$/u);
  assert.equal(id, cameraId("T9001P0000000000"));
  assert.notEqual(id, cameraId("T9001P0000000001"));
  assert(!id.includes("T9001"));
});

test("dithering keeps exact colours exact and spreads the rest", () => {
  const flat = new Uint8Array(4 * 2 * 3).fill(0);
  for (let i = 0; i < 8; ++i) flat.set([255, 0, 0], i * 3);
  assert.deepEqual([...dither332(flat, 4, 2)], new Array(8).fill(to332(255, 0, 0)));
  assert.equal(to332(255, 255, 255), 0xff);
  assert.equal(to332(0, 0, 0), 0);
  // A mid grey isn't one of the 256 colours: dithered, the picture averages back to it.
  const grey = new Uint8Array(16 * 16 * 3).fill(100);
  const out = dither332(grey, 16, 16);
  const mean = [...out].reduce((sum, v) => sum + ((v >> 5) * 255) / 7, 0) / out.length;
  assert(Math.abs(mean - 100) < 6, `mean red ${mean}`);
  assert(new Set(out).size > 1);
});

test("ffmpeg's pictures are cut back into whole JPEGs, however they arrive", () => {
  const got = [];
  const split = new JpegSplitter(jpeg => got.push(jpeg));
  const one = Buffer.from([0xff, 0xd8, 1, 2, 0xff, 0x00, 3, 0xff, 0xd9]);
  const two = Buffer.from([0xff, 0xd8, 9, 0xff, 0xd9]);
  const all = Buffer.concat([one, two]);
  for (let i = 0; i < all.length; ++i) split.push(all.subarray(i, i + 1));  // One byte at a time.
  split.push(Buffer.concat([one, two.subarray(0, 3)]));
  split.push(two.subarray(3));
  assert.deepEqual(got.map(b => [...b]), [[...one], [...two], [...one], [...two]]);
});

test("the device's list is lines, with ages in seconds and nothing it can't show", () => {
  const now = 1_000_000_000;
  const text = listLines({ state: "ready", base: { mode: "away" }, cameras: [
    { id: "0123abcd", name: "Front\tdoor\n", ptz: true, battery: 84, motionAt: now - 720000, pictureAt: now - 60000 },
    { id: "89abcdef", name: "", off: true, battery: 140 },
    { id: "NOT-AN-ID", name: "x" },
  ] }, now);
  assert.equal(text, "state\tready\nbase\tAway\ncam\t0123abcd\tp\t84\t720\t60\tFront door\ncam\t89abcdef\to\t-\t-\t-\tCamera\n");
  assert.equal(listLines({ state: "code", detail: "eufy sent a code\tby email" }), "state\tcode\teufy sent a code by email\n");
  assert.equal(modeWord("geo"), "Geofence");
  assert.equal(modeWord(1), "Home");
  assert.equal(modeWord(63), "Disarmed");
  assert.equal(modeWord(0), "Away");
  assert.equal(modeWord(99), "");
  assert.equal(listLines({ state: "ready", base: { mode: 0 } }), "state\tready\nbase\tAway\n");
  assert.equal(modeWord("?"), "");
  assert.equal(shownName("a".repeat(40)).length, 24);
});

test("the log never gets serials, emails, addresses or links", () => {
  const text = scrub("T9002P1234567890 at 192.168.0.20 for me@example.com via https://x.example/a?b", ["T9002P1234567890"]);
  assert(!/T9002P|192\.168|example\.com|https/u.test(text), text);
  assert(text.includes(cameraId("T9002P1234567890")));
  assert.equal(scrub("T9003P0000000000 alone"), "<serial> alone");
});

test("the eufy account file is read as KEY=value lines", () => {
  assert.deepEqual(parseEnv("# note\nEUFY_EMAIL= a@b.c \nEUFY_PASSWORD=\"p=w#1\"\n"), { email: "a@b.c", password: "p=w#1", country: "US" });
  assert.deepEqual(parseEnv("EUFY_COUNTRY=gb"), { email: "", password: "", country: "GB" });
});

test("ffmpeg's arguments read stdin and write stdout only", () => {
  for (const args of [pictureArgs(), thumbArgs(), liveArgs("h264"), liveArgs("h265")]) {
    assert.equal(args[args.indexOf("-i") + 1], "pipe:0");
    assert.equal(args.at(-1), "pipe:1");
    assert(!args.some(a => /^(?:https?|file):/u.test(a)));
  }
  assert.equal(liveArgs("av1"), null);
});

test("the helper isn't started when the Mac has no camera set-up", () => {
  let spawned = 0;
  const service = new CameraService({ dir: "/nonexistent", node: "/bin/sh", spawnImpl: () => { spawned++; } });
  assert.equal(service.missing(), "Cameras aren't set up on the Mac");
  assert.equal(service.start(), false);
  assert.equal(spawned, 0);
  assert.equal(new CameraService({ enabled: false }).missing(), "Cameras are turned off on the Mac");
});

test("without a helper, the list says why, and other routes say it's not set up", async () => {
  const service = new CameraService({ dir: "/nonexistent" });
  let res = fakeRes();
  await handleCameraRoute(service, "/api/cardputer/cameras/list", new URLSearchParams(), "dev1", res);
  assert.equal(res.status, 200);
  assert.equal(res.body.toString(), "state\toff\tCameras aren't set up on the Mac\n");
  res = fakeRes();
  await handleCameraRoute(service, "/api/cardputer/cameras/picture", new URLSearchParams("cam=0123abcd"), "dev1", res);
  assert.equal(res.status, 503);
  assert.equal(res.body.toString(), "not_set_up\n");
  res = fakeRes();
  await handleCameraRoute(service, "/api/cardputer/cameras/picture", new URLSearchParams("cam=../x"), "dev1", res);
  assert.equal(res.status, 404);
  res = fakeRes();
  await handleCameraRoute(service, "/api/cardputer/cameras/elsewhere", new URLSearchParams(), "dev1", res);
  assert.equal(res.status, 404);
});

test("tickets: none without the home door, one per device with it", () => {
  const service = new CameraService({});
  assert.equal(service.ticketLines("dev1"), "none\n");
  service.lanPort = 31890;
  const interfaces = { en0: [{ family: "IPv4", address: "192.168.0.20", cidr: "192.168.0.20/22", internal: false }] };
  const text = service.ticketLines("dev1", interfaces);
  const [ticket, lan] = text.trim().split("\n");
  assert.match(ticket, /^ticket\t31890\t\/cameras\/[0-9a-f]{32}$/u);
  assert.equal(lan, "lan\t192.168.0.20\t22");
  assert.equal(service.ticketLines("dev1", interfaces), text);  // The same one again.
  assert.notEqual(service.ticketLines("dev2", interfaces), text);
  assert.equal(service.redeem(ticket.split("/").at(-1)).device, "dev1");
  assert.equal(service.redeem("f".repeat(32)), null);
});

// A stand-in for the helper, on a real port, checking the secret as the real one does.
async function withHelper(routes, body) {
  const server = createServer((req, res) => {
    if (req.headers["x-cameras-secret"] !== "s3cret") { res.writeHead(403); res.end(); return; }
    const url = new URL(req.url, "http://h");
    const route = routes[`${req.method} ${url.pathname}`];
    if (!route) { res.writeHead(404, { "content-type": "application/json" }); res.end('{"error":"not_found"}'); return; }
    route(url.searchParams, res);
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const dir = await mkdtemp(path.join(os.tmpdir(), "cams-"));
  await mkdir(path.join(dir, "node_modules", "@mega-yfue", "eufy-sdk"), { recursive: true });
  await writeFile(path.join(dir, "node_modules", "@mega-yfue", "eufy-sdk", "package.json"), "{}");
  const service = new CameraService({ dir, node: process.execPath });
  service.port = server.address().port;
  service.secret = "s3cret";
  try { await body(service); } finally { server.close(); await rm(dir, { recursive: true, force: true }); }
}

test("the list and pictures come from the helper, as the device reads them", async () => {
  const jpeg = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);
  await withHelper({
    "GET /list": (q, res) => { res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ state: "ready", cameras: [{ id: "0123abcd", name: "Porch", ptz: true, battery: 90, motionAt: 0, pictureAt: 0 }] })); },
    "GET /picture": (q, res) => {
      if (q.get("cam") !== "0123abcd") { res.writeHead(404, { "content-type": "application/json" }); res.end('{"error":"no_picture"}'); return; }
      res.writeHead(200, { "content-type": "image/jpeg" }); res.end(jpeg);
    },
  }, async (service) => {
    let res = fakeRes();
    await handleCameraRoute(service, "/api/cardputer/cameras/list", new URLSearchParams(), "dev1", res);
    assert.equal(res.body.toString(), "state\tready\ncam\t0123abcd\tp\t90\t-\t-\tPorch\n");
    res = fakeRes();
    await handleCameraRoute(service, "/api/cardputer/cameras/picture", new URLSearchParams("cam=0123abcd"), "dev1", res);
    assert.equal(res.status, 200);
    assert.equal(res.headers["content-type"], "image/jpeg");
    assert.deepEqual([...res.body], [...jpeg]);
    res = fakeRes();
    await handleCameraRoute(service, "/api/cardputer/cameras/picture", new URLSearchParams("cam=89abcdef"), "dev1", res);
    assert.equal(res.status, 404);
    assert.equal(res.body.toString(), "no_picture\n");
  });
});

test("live frames: each device is sent only frames newer than its last", async () => {
  const afters = [];
  let seq = 0;
  await withHelper({
    "POST /live/start": (q, res) => { seq = 0; res.writeHead(200, { "content-type": "application/json" }); res.end("{}"); },
    "GET /live/frame": (q, res) => {
      afters.push(Number(q.get("after")));
      if (seq >= 2) { res.writeHead(204); res.end(); return; }
      seq += 1;
      res.writeHead(200, { "content-type": "image/jpeg", "x-seq": String(seq) });
      res.end(Buffer.from([0xff, 0xd8, seq, 0xff, 0xd9]));
    },
  }, async (service) => {
    const ask = async (what, dev = "dev1") => {
      const res = fakeRes();
      await handleCameraRoute(service, `/api/cardputer/cameras/${what}`, new URLSearchParams("cam=0123abcd"), dev, res);
      return res;
    };
    assert.equal((await ask("live/start")).body.toString(), "ok\n");
    assert.equal((await ask("live/frame")).body[2], 1);
    assert.equal((await ask("live/frame")).body[2], 2);
    assert.equal((await ask("live/frame")).status, 204);
    await ask("live/frame", "dev2");
    assert.deepEqual(afters, [0, 1, 2, 0]);
  });
});

test("the home door answers home addresses with a ticket, and nothing else", async () => {
  const service = new CameraService({});
  service.lanPort = 31890;
  const interfaces = { en0: [{ family: "IPv4", address: "192.168.0.20", cidr: "192.168.0.20/22", internal: false }] };
  const ticket = service.ticketLines("dev1", interfaces).split("\n")[0].split("/").at(-1);
  const req = (url, remote = "192.168.0.31") => ({ method: "GET", url, socket: { remoteAddress: remote } });
  let res = fakeRes();
  assert.equal(cameraLanRoute(service, req("/podcasts/x"), res), false);
  res = fakeRes();
  assert.equal(cameraLanRoute(service, req(`/cameras/${ticket}/list`, "100.64.0.9"), res), true);
  assert.equal(res.status, 404);
  res = fakeRes();
  assert.equal(cameraLanRoute(service, req(`/cameras/${"0".repeat(32)}/list`), res), true);
  assert.equal(res.status, 404);
  res = fakeRes();
  assert.equal(cameraLanRoute(service, req(`/cameras/${ticket}/ticket`), res), true);
  assert.equal(res.status, 404);
  res = fakeRes();
  const finished = done(res);
  assert.equal(cameraLanRoute(service, req(`/cameras/${ticket}/list`), res), true);
  await finished;
  assert.equal(res.status, 200);
  assert.equal(res.body.toString(), "state\toff\tCameras aren't set up on the Mac\n");
});

test("CameraError carries the status the device sees", () => {
  assert.equal(new CameraError("x", 409).status, 409);
});

// The real ffmpeg: H.265 video in, the screen's JPEGs out, and a still to its thumbnail.
test("ffmpeg turns camera video into screen-sized baseline JPEGs", { skip: !FFMPEG }, async () => {
  const hevc = spawnSync(FFMPEG, ["-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=15",
    "-frames:v", "8", "-c:v", "libx265", "-x265-params", "log-level=none", "-f", "hevc", "pipe:1"], { maxBuffer: 64 << 20 });
  assert.equal(hevc.status, 0, String(hevc.stderr));
  const frames = await new Promise((resolve, reject) => {
    const got = [];
    const child = spawn(FFMPEG, liveArgs("h265"));
    const split = new JpegSplitter(jpeg => got.push(jpeg));
    child.stdout.on("data", chunk => split.push(chunk));
    child.on("error", reject);
    child.on("close", () => resolve(got));
    child.stdin.end(hevc.stdout);
  });
  assert(frames.length >= 6, `${frames.length} frames`);
  for (const jpeg of frames) {
    assert.equal(jpeg[0], 0xff); assert.equal(jpeg[1], 0xd8);
    assert(jpeg.includes(Buffer.from([0xff, 0xc0])), "baseline");
    assert(!jpeg.includes(Buffer.from([0xff, 0xc2])), "not progressive");
    const sof = jpeg.indexOf(Buffer.from([0xff, 0xc0]));
    assert.equal(jpeg.readUInt16BE(sof + 5), PICTURE.height);
    assert.equal(jpeg.readUInt16BE(sof + 7), PICTURE.width);
    assert(jpeg.length < 24 * 1024, `${jpeg.length} bytes`);
    assert(deviceDecodes(jpeg), JSON.stringify(jpegLayout(jpeg)));
  }
  // The layouts the device's decoder refuses are told apart: 4:2:2 and 4:4:4 from ffmpeg, and a wrong size.
  for (const [format, size] of [["yuvj422p", "240x136"], ["yuvj444p", "240x136"], ["yuvj420p", "240x134"]]) {
    const other = spawnSync(FFMPEG, ["-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", `testsrc2=size=${size}`, "-frames:v", "1",
      "-c:v", "mjpeg", "-pix_fmt", format, "-f", "image2pipe", "pipe:1"], { maxBuffer: 4 << 20 });
    assert.equal(other.status, 0);
    assert.equal(deviceDecodes(other.stdout), false, `${format} ${size}`);
  }
  const rgb = spawnSync(FFMPEG, thumbArgs(), { input: frames[0], maxBuffer: 1 << 20 });
  assert.equal(rgb.status, 0);
  assert.equal(rgb.stdout.length, THUMB.bytes * 3);
  assert.equal(dither332(rgb.stdout, THUMB.width, THUMB.height).length, THUMB.bytes);
  const big = spawnSync(FFMPEG, ["-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=2880x1616",
    "-frames:v", "1", "-f", "image2pipe", "-c:v", "mjpeg", "pipe:1"], { maxBuffer: 16 << 20 });
  const small = spawnSync(FFMPEG, pictureArgs(), { input: big.stdout, maxBuffer: 1 << 20 });
  assert.equal(small.status, 0, String(small.stderr));
  const sof = small.stdout.indexOf(Buffer.from([0xff, 0xc0]));
  assert.equal(small.stdout.readUInt16BE(sof + 7), PICTURE.width);
});

// --- The phone sign-in page ----------------------------------------------------------------
import { Readable } from "node:stream";
import { readFile as readText, stat as statFile } from "node:fs/promises";
import { SetupLinks, accountFile, emailIn, handleSetupPage } from "./cameras-setup.mjs";

test("a sign-in link lasts 15 minutes, is reused while it has a while left, and only it opens the page", () => {
  let now = 1_000_000;
  const links = new SetupLinks(() => now);
  const a = links.current();
  assert.match(a.token, /^[A-Za-z0-9_-]{16}$/u);
  now += 9 * 60 * 1000;
  assert.equal(links.current().token, a.token, "kept: a page already open keeps working");
  now += 2 * 60 * 1000;
  const b = links.current();
  assert.notEqual(b.token, a.token, "under five minutes left: a new one");
  assert.equal(links.check(a.token), null);
  assert.equal(links.check(b.token), b);
  assert.equal(links.check("x".repeat(16)), null);
  assert.equal(links.check(undefined), null);
  now += 16 * 60 * 1000;
  assert.equal(links.check(b.token), null, "expired");
  links.current();
  links.finish();
  assert.equal(links.link, null);
});

test("the account file keeps any password and refuses what would break its lines", () => {
  const text = accountFile({ email: " Me@Example.com ", password: ' p#ss "word" = x ', country: "gb" }, new Date("2026-11-06T12:00:00Z"));
  assert.deepEqual(parseEnv(text), { email: "Me@Example.com", password: ' p#ss "word" = x ', country: "GB" });
  assert.equal(emailIn(text), "me@example.com");
  for (const bad of [{ email: "nope", password: "x" }, { email: "a@b.co", password: "" }, { email: "a@b.co", password: "line\nbreak" },
    { email: "a@b.co", password: "x", country: "USA" }, { email: "a b@c.co", password: "x" }])
    assert.throws(() => accountFile(bad), (e) => e.status === 400);
});

function phoneReq(method, url, body) {
  const req = Readable.from(body === undefined ? [] : [Buffer.from(JSON.stringify(body))]);
  req.method = method;
  req.url = url;
  return req;
}

test("the sign-in page: wrong links count as failures, the right one saves the account and signs in", async () => {
  const calls = [];
  let state = "setup";
  await withHelper({
    "GET /status": (q, res) => { res.writeHead(200, { "content-type": "application/json" }); res.end(JSON.stringify({ state, detail: state === "code" ? "eufy sent a code by email" : "" })); },
    "GET /list": (q, res) => { res.writeHead(200, { "content-type": "application/json" }); res.end(JSON.stringify({ state, cameras: [{ id: "0123abcd" }, { id: "89abcdef" }] })); },
    "POST /signin": (q, res) => { calls.push(`signin fresh=${q.get("fresh") || ""}`); state = "code"; res.writeHead(200, { "content-type": "application/json" }); res.end("{}"); },
    "POST /code": (q, res) => { calls.push(`code ${q.get("value")}`); state = "ready"; res.writeHead(200, { "content-type": "application/json" }); res.end("{}"); },
  }, async (service) => {
    service.envFile = path.join(service.dir, "eufy.env");
    let failed = 0, limited = false;
    const guard = { limited: () => limited, failed: () => { failed++; } };
    const ask = async (method, pathname, body) => {
      const res = fakeRes();
      const handled = await handleSetupPage(service, phoneReq(method, pathname, body), res, new URL(pathname, "https://bridge.example"), guard);
      return { handled, res, json: () => JSON.parse(res.body.toString() || "{}") };
    };
    assert.equal((await ask("GET", "/api/cardputer/cameras/list")).handled, false);
    // No link yet, a made-up one, a malformed one: all 404, all counted.
    let r = await ask("GET", "/eufy/AAAAAAAAAAAAAAAA");
    assert.equal(r.res.status, 404);
    assert.match(r.res.body.toString(), /This link has expired/u);
    assert.equal((await ask("GET", "/eufy/short%2F..")).res.status, 404);
    assert.equal(failed, 2);
    // The device asks for the link: the page answers it, with no caching and a strict policy.
    const line = (await (async () => { const res = fakeRes(); await handleCameraRoute(service, "/api/cardputer/cameras/setup", new URLSearchParams(), "dev1", res); return res.body.toString(); })());
    const [, setupPath, seconds] = line.trim().split("\t");
    assert.match(setupPath, /^\/eufy\/[A-Za-z0-9_-]{16}$/u);
    assert(Number(seconds) > 14 * 60 && Number(seconds) <= 15 * 60);
    const logged = [];
    const log = console.log;
    console.log = (...a) => logged.push(a.join(" "));
    try {
      r = await ask("GET", setupPath);
      assert.equal(r.res.status, 200);
      assert.equal(r.res.headers["cache-control"], "no-store");
      assert.equal(r.res.headers["referrer-policy"], "no-referrer");
      assert.match(r.res.headers["content-security-policy"], /default-src 'none'.*script-src 'nonce-/u);
      assert.match(r.res.body.toString(), /eufy password/u);
      // Signing in: the account is saved for the helper only, and it signs in at once, afresh.
      r = await ask("POST", `${setupPath}/account`, { email: "me@example.com", password: "s3cret pass", country: "US" });
      assert.equal(r.res.status, 200);
      assert.equal(r.json().state, "code");
      assert.deepEqual(parseEnv(await readText(service.envFile, "utf8")), { email: "me@example.com", password: "s3cret pass", country: "US" });
      assert.equal((await statFile(service.envFile)).mode & 0o777, 0o600);
      // The same account again keeps its session; a different one has the last one's forgotten.
      // (A first account has no session to forget.)
      r = await ask("POST", `${setupPath}/account`, { email: "me@example.com", password: "other" });
      r = await ask("POST", `${setupPath}/account`, { email: "someone@example.org", password: "x" });
      r = await ask("POST", `${setupPath}/code`, { code: "12 34 56" });
      assert.deepEqual(r.json(), { state: "ready", detail: "", cameras: 2 });
      assert.deepEqual(calls, ["signin fresh=", "signin fresh=", "signin fresh=1", "code 123456"]);
    } finally { console.log = log; }
    assert(!logged.some(l => l.includes(setupPath.slice(6)) || l.includes("s3cret") || l.includes("example.com") || l.includes("example.org")), logged.join("\n"));
    // Signed in, the link has done its job.
    assert.equal((await ask("GET", `${setupPath}/status`)).res.status, 404);
    // Bad input is refused without touching the file; too many tries from one address are stopped.
    const again = service.links.current().token;
    r = await ask("POST", `/eufy/${again}/account`, { email: "x", password: "y" });
    assert.equal(r.res.status, 400);
    limited = true;
    assert.equal((await ask("GET", `/eufy/${again}`)).res.status, 429);
  });
});
