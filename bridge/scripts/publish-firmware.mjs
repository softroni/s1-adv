import { copyFile, mkdir, readFile, readdir, rename, rm, stat, writeFile, chown, chmod } from "node:fs/promises";
import { createHash } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { releaseNote } from "../src/firmware-publish.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function parseArgs(argv) {
  const out = {};
  for (let index = 0; index < argv.length; index += 1) {
    const item = argv[index];
    if (!item.startsWith("--")) {
      continue;
    }
    const key = item.slice(2);
    const next = argv[index + 1];
    if (!next || next.startsWith("--")) {
      out[key] = "true";
      continue;
    }
    out[key] = next;
    index += 1;
  }
  return out;
}

function resolveConfigPath(args) {
  return path.resolve(
    process.cwd(),
    args.config || process.env.CARDPUTER_BRIDGE_CONFIG || path.resolve(__dirname, "..", "config.json")
  );
}

function resolvePath(baseDir, maybeRelative) {
  return path.isAbsolute(maybeRelative) ? maybeRelative : path.resolve(baseDir, maybeRelative);
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function safeFilename(value) {
  const filename = String(value || "").trim();
  if (!/^[a-z0-9][a-z0-9._-]{0,63}\.bin$/u.test(filename)) {
    throw new Error(`unsafe firmware filename: ${filename}`);
  }
  return filename;
}

const args = parseArgs(process.argv.slice(2));
if (!args.bin || !args.version) {
  throw new Error("usage: node scripts/publish-firmware.mjs --bin <firmware.bin> --version <version> [--config config.json]");
}

const configPath = resolveConfigPath(args);
const config = JSON.parse(await readFile(configPath, "utf8"));
const configDir = path.dirname(configPath);
const firmwareConfig = config.firmware || {};
const assetsConfig = config.assets || {};
const stateDir = resolvePath(configDir, config.stateDir || "./data");
const rootDir = resolvePath(configDir, firmwareConfig.rootDir || path.join(stateDir, "firmware"));
const deviceClass = firmwareConfig.deviceClass || assetsConfig.deviceClass || "cardputer_adv";
const channel = args.channel || firmwareConfig.channel || "dev";
const filename = safeFilename(args.filename || `cardputer-adv-${args.version}.bin`);
const destDir = path.join(rootDir, deviceClass);
const destPath = path.join(destDir, filename);
const owner = firmwareConfig.storageOwner;
if (owner && (!Number.isSafeInteger(owner.uid) || owner.uid < 0 || !Number.isSafeInteger(owner.gid) || owner.gid < 0 ||
    (process.getuid?.() !== 0 && process.getuid?.() !== owner.uid)))
  throw new Error("Firmware storageOwner requires valid numeric IDs and a matching publisher or root operator.");
async function protectPublishedFile(file) {
  if (owner) await chown(file, owner.uid, owner.gid);
  await chmod(file, 0o600);
}

await mkdir(destDir, { recursive: true });
await copyFile(path.resolve(process.cwd(), args.bin), destPath);
await protectPublishedFile(destPath);
const firmwareBytes = await readFile(destPath);
// This script runs fresh each time, so a running bridge gets the words without a restart.
const projectDir = config.upstream?.claudeCode?.projectDir;
const notes = args.notes || (projectDir ? await releaseNote(projectDir, args.version) : "");
const manifest = {
  device: deviceClass,
  version: args.version,
  channel,
  filename,
  sha256: sha256(firmwareBytes),
  size: firmwareBytes.length,
  min_battery: Number(args.minBattery || firmwareConfig.minBattery || 40),
  confirm_required: String(args.confirmRequired ?? firmwareConfig.confirmRequired ?? "true") !== "false",
  built_at: args.builtAt || new Date().toISOString(),
  build_git_sha: args.git || "",
  notes
};

const manifestPath = path.join(destDir, firmwareConfig.manifestFilename || "manifest.json");
const tmpPath = `${manifestPath}.${process.pid}.${Date.now()}.tmp`;
await writeFile(tmpPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
await protectPublishedFile(tmpPath);
await rename(tmpPath, manifestPath);
// Older images go: a device only downloads the manifest's, and each is about 3 MB of the bridge's
// storage budget (storage-budget.mjs). A hundred of them filled it, and the bridge then refused
// every message, voice note and log upload. The newest few stay, the one just published among them.
const keep = Math.max(1, Math.floor(Number(firmwareConfig.keepImages)) || 3);
const older = [];
for (const name of await readdir(destDir)) {
  if (name === filename || !/^[a-z0-9][a-z0-9._-]{0,63}\.bin$/u.test(name)) continue;
  const info = await stat(path.join(destDir, name));
  if (info.isFile()) older.push({ name, at: info.mtimeMs });
}
older.sort((a, b) => b.at - a.at);
const removed = [];
for (const { name } of older.slice(keep - 1)) {
  await rm(path.join(destDir, name), { force: true });
  removed.push(name);
}
process.stdout.write(`${JSON.stringify({ manifest_path: manifestPath, firmware_path: destPath, ...manifest, removed }, null, 2)}\n`);
