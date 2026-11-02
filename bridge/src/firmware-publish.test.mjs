import assert from "node:assert/strict";
import { chmod, mkdir, mkdtemp, readFile, readdir, rm, stat, symlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { execFile } from "node:child_process";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { firmwareVersion, inspectFirmwareImage, isNewerVersion, publishProjectFirmware, releaseNote, shortNote } from "./firmware-publish.mjs";

function image({ size = 128 * 1024, chip = 9, magic = 0xe9, desc = 0xabcd5432, version = "0.2.208-dev" } = {}) {
  const bytes = Buffer.alloc(size);
  bytes.write(version, 4096);
  bytes[0] = magic;
  bytes.writeUInt16LE(chip, 12);
  bytes.writeUInt32LE(desc, 32);
  return bytes;
}

async function project(t, { version = "0.2.208-dev", bytes = image() } = {}) {
  const root = await mkdtemp(path.join(tmpdir(), "s1-publish-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const projectDir = path.join(root, "project");
  const firmware = path.join(projectDir, "bots", "CardputerADV");
  const build = path.join(firmware, ".pio", "build", "cardputer_adv");
  await mkdir(build, { recursive: true });
  await writeFile(path.join(firmware, "platformio.ini"), `build_flags =\n    -D APP_VERSION=\\"${version}\\"\n`);
  if (bytes) await writeFile(path.join(build, "firmware.bin"), bytes);
  const stateDir = path.join(root, "state");
  const configPath = path.join(root, "config.json");
  await writeFile(configPath, JSON.stringify({ stateDir, firmware: { rootDir: path.join(stateDir, "firmware"), deviceClass: "cardputer_adv" } }));
  return { root, projectDir, build, configPath, stateDir, manifest: path.join(stateDir, "firmware", "cardputer_adv", "manifest.json") };
}

test("only a real ESP32-S3 app image that fits is accepted", () => {
  inspectFirmwareImage(image());
  assert.throws(() => inspectFirmwareImage(Buffer.alloc(100)), /too small/);
  assert.throws(() => inspectFirmwareImage(image({ size: 4 * 1024 * 1024 })), /doesn't fit/);
  assert.throws(() => inspectFirmwareImage(image({ magic: 0x7f })), /isn't an ESP32 app image/);
  assert.throws(() => inspectFirmwareImage(image({ desc: 1 })), /isn't an ESP32 app image/);
  assert.throws(() => inspectFirmwareImage(image({ chip: 0 })), /ESP32-S3/);
});

test("versions are read strictly and must move forward", () => {
  assert.equal(firmwareVersion('-D APP_VERSION=\\"0.2.208-dev\\"'), "0.2.208-dev");
  assert.throws(() => firmwareVersion('-D APP_VERSION=\\"$(touch /tmp/x)\\"'), /APP_VERSION/);
  assert.throws(() => firmwareVersion("nothing here"), /APP_VERSION/);
  assert.equal(isNewerVersion("0.2.208-dev", "0.2.207-dev"), true);
  assert.equal(isNewerVersion("0.2.10-dev", "0.2.9-dev"), true, "numeric, not alphabetical");
  assert.equal(isNewerVersion("0.2.207-dev", "0.2.207-dev"), false);
  assert.equal(isNewerVersion("0.2.206-dev", "0.2.207-dev"), false);
  assert.equal(isNewerVersion("0.2.206-dev", null), true);
});

test("a valid build reaches the update manifest with approval still required", async t => {
  const fixture = await project(t);
  const result = await publishProjectFirmware({ ...fixture, installedVersion: "0.2.207-dev" });
  assert.equal(result.version, "0.2.208-dev");
  assert.equal(result.minBattery, 40);
  const manifest = JSON.parse(await readFile(fixture.manifest, "utf8"));
  assert.equal(manifest.version, "0.2.208-dev");
  assert.equal(manifest.confirm_required, true, "the owner must approve the install");
  assert.equal(manifest.size, 128 * 1024);
  const published = path.join(path.dirname(fixture.manifest), manifest.filename);
  assert.equal((await stat(published)).mode & 0o077, 0);
});

test("nothing is published for a bad or old build", async t => {
  for (const [options, installed, expected] of [
    [{ bytes: image({ chip: 0 }) }, "0.2.207-dev", /ESP32-S3/],
    [{ version: "0.2.207-dev", bytes: image({ version: "0.2.207-dev" }) }, "0.2.207-dev", /isn't newer/],
    [{ version: "0.2.209-dev", bytes: image({ version: "0.2.208-dev" }) }, "0.2.207-dev", /isn't version 0\.2\.209-dev/],
    [{ bytes: null }, "0.2.207-dev", /wasn't found/]
  ]) {
    const fixture = await project(t, options);
    await assert.rejects(publishProjectFirmware({ ...fixture, installedVersion: installed }), expected);
    await assert.rejects(stat(fixture.manifest));
  }
});

test("a symlink can't publish a private file or read outside the project", async t => {
  const fixture = await project(t, { bytes: null });
  const secret = path.join(fixture.root, "private.bin");
  await writeFile(secret, image());
  await symlink(secret, path.join(fixture.build, "firmware.bin"));
  await assert.rejects(publishProjectFirmware(fixture), /wasn't found/);
  await assert.rejects(stat(fixture.manifest));

  const redirected = await project(t, { bytes: null });
  const outside = path.join(redirected.root, "outside");
  await mkdir(outside);
  await writeFile(path.join(outside, "firmware.bin"), image());
  await rm(redirected.build, { recursive: true });
  await symlink(outside, redirected.build);
  await assert.rejects(publishProjectFirmware(redirected), /outside the project/);
});

test("nothing from the project is executed, and no temporary image is left", async t => {
  const fixture = await project(t);
  const planted = path.join(fixture.projectDir, "bridge", "scripts");
  await mkdir(planted, { recursive: true });
  await writeFile(path.join(planted, "publish-firmware.mjs"), "throw new Error('project code ran')");
  await chmod(path.join(planted, "publish-firmware.mjs"), 0o755);
  const calls = [];
  await publishProjectFirmware({ ...fixture, execute: async (command, args) => {
    calls.push([command, ...args]);
    assert.ok((await readFile(args[args.indexOf("--bin") + 1])).equals(image()), "publishes the checked bytes");
    return { stdout: JSON.stringify({ min_battery: 40 }) };
  } });
  assert.equal(calls[0][0], process.execPath);
  assert.ok(!calls[0].some(argument => argument.startsWith(fixture.projectDir)));
  assert.equal((await readdir(tmpdir())).filter(name => calls[0].join(" ").includes(name) && name.startsWith("s1-firmware-")).length, 0);
});

test("an update's note is five plain words at most", () => {
  assert.equal(shortNote("Bigger screens, menu letters"), "Bigger screens, menu letters");
  assert.equal(shortNote("one two three four five six seven"), "one two three four five");
  assert.equal(shortNote("**Bold** `code` <b>tag</b>"), "Bold code btag/b");
  assert.equal(shortNote("Caf\u00e9 \u043c\u0435\u043d\u044e\nnext"), "Caf next");
  assert.equal(shortNote("  spaced   out  "), "spaced out");
  assert.equal(shortNote("x".repeat(90)).length, 48);
  for (const empty of ["", null, undefined, "\u0000\u0001"]) assert.equal(shortNote(empty), "");
});

test("the note comes from the release notes, and a bad file can't stop an update", async t => {
  const root = await mkdtemp(path.join(tmpdir(), "s1-note-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  await mkdir(path.join(root, "docs"));
  const notes = body => writeFile(path.join(root, "docs", "RELEASE-1.2.3.md"), body);
  await notes("# S1-ADV 1.2.3\n\nIn short: Colour weather icons\n\nLonger text.\n");
  assert.equal(await releaseNote(root, "1.2.3-dev"), "Colour weather icons");
  await notes("# Title\n\nin SHORT:   Lower case works\n");
  assert.equal(await releaseNote(root, "1.2.3"), "Lower case works");
  await notes("# Title\n\nNothing marked here, in short: not a line start.\n");
  assert.equal(await releaseNote(root, "1.2.3"), "");
  assert.equal(await releaseNote(root, "9.9.9"), "", "no notes for this version");
  // A link out of the project must not read a private file.
  const secret = path.join(root, "secret.txt");
  await writeFile(secret, "In short: private words\n");
  await rm(path.join(root, "docs", "RELEASE-1.2.3.md"));
  await symlink(secret, path.join(root, "docs", "RELEASE-1.2.3.md"));
  assert.equal(await releaseNote(root, "1.2.3"), "");
});

test("publishing puts the note in the manifest the device reads", async t => {
  const root = await mkdtemp(path.join(tmpdir(), "s1-note-publish-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const projectDir = path.join(root, "project");
  await mkdir(path.join(projectDir, "docs"), { recursive: true });
  await writeFile(path.join(projectDir, "docs", "RELEASE-1.2.3.md"), "# x\n\nIn short: Five words at most here, truly\n");
  const bin = path.join(root, "firmware.bin");
  await writeFile(bin, image());
  const configPath = path.join(root, "config.json");
  const config = { stateDir: path.join(root, "state"), upstream: { claudeCode: { projectDir } } };
  await writeFile(configPath, JSON.stringify(config));
  const script = fileURLToPath(new URL("../scripts/publish-firmware.mjs", import.meta.url));
  const publish = async (...extra) => JSON.parse((await promisify(execFile)(process.execPath,
    [script, "--config", configPath, "--bin", bin, "--version", "1.2.3-dev", ...extra])).stdout);
  assert.equal((await publish()).notes, "Five words at most here,");
  assert.equal((await publish("--notes", "Said by hand")).notes, "Said by hand");
  await writeFile(configPath, JSON.stringify({ stateDir: config.stateDir }));
  assert.equal((await publish()).notes, "", "without a project there's nothing to read");
});
