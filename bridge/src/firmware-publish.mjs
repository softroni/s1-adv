import { execFile } from "node:child_process";
import { lstat, mkdtemp, readFile, realpath, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";

// Publishes a firmware image built inside the sandbox. This runs outside it, so
// everything found in the project is treated as untrusted data: the image is
// validated as bytes, and nothing from the project is ever executed. Publishing
// only offers an update. The owner still approves the install on the device.

const FIRMWARE_DIR = path.join("bots", "CardputerADV");
const IMAGE_PATH = path.join(FIRMWARE_DIR, ".pio", "build", "cardputer_adv", "firmware.bin");
const VERSION_FILE = path.join(FIRMWARE_DIR, "platformio.ini");
const PUBLISH_SCRIPT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "scripts", "publish-firmware.mjs");
const run = promisify(execFile);

const MAX_APP_BYTES = 3 * 1024 * 1024; // The app slots in partitions_8mb_dual_ota.csv.
const MIN_APP_BYTES = 64 * 1024;
const IMAGE_MAGIC = 0xe9;
const CHIP_ID_OFFSET = 12;
const ESP32S3_CHIP_ID = 9;
const APP_DESC_OFFSET = 32;
const APP_DESC_MAGIC = 0xabcd5432;

export function inspectFirmwareImage(bytes) {
  if (bytes.length < MIN_APP_BYTES) throw new Error("the build is too small to be firmware");
  if (bytes.length > MAX_APP_BYTES) throw new Error("the build doesn't fit the 3 MB app slot");
  if (bytes[0] !== IMAGE_MAGIC || bytes.readUInt32LE(APP_DESC_OFFSET) !== APP_DESC_MAGIC)
    throw new Error("the file isn't an ESP32 app image");
  if (bytes.readUInt16LE(CHIP_ID_OFFSET) !== ESP32S3_CHIP_ID) throw new Error("the image isn't built for ESP32-S3");
}

export function firmwareVersion(text) {
  const version = /-D APP_VERSION=\\"(\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]{1,32})?)\\"/.exec(text)?.[1];
  if (!version) throw new Error("couldn't read APP_VERSION from platformio.ini");
  return version;
}

const release = version => version.split("-")[0].split(".").map(Number);
export function isNewerVersion(candidate, current) {
  if (!current) return true;
  const [a, b] = [release(candidate), release(current)];
  for (let index = 0; index < 3; index += 1) if (a[index] !== b[index]) return a[index] > b[index];
  return false;
}

// A symlink could otherwise publish a private file, or read one outside the project.
async function projectFile(projectDir, relative) {
  const file = path.join(projectDir, relative);
  const info = await lstat(file).catch(() => null);
  if (!info?.isFile()) throw new Error(`${path.basename(file)} wasn't found. Build the firmware first`);
  const root = await realpath(projectDir), resolved = await realpath(file);
  if (!resolved.startsWith(`${root}${path.sep}`)) throw new Error(`${path.basename(file)} is outside the project`);
  return resolved;
}

export async function publishProjectFirmware({ projectDir, configPath, installedVersion = null, execute = run }) {
  const bytes = await readFile(await projectFile(projectDir, IMAGE_PATH));
  inspectFirmwareImage(bytes);
  const version = firmwareVersion(await readFile(await projectFile(projectDir, VERSION_FILE), "utf8"));
  if (!isNewerVersion(version, installedVersion))
    throw new Error(`version ${version} isn't newer than ${installedVersion}. Increase APP_VERSION and rebuild`);
  // The version file can move on while an older build is still on disk. A
  // mismatched label would make the device offer the same update forever.
  if (!bytes.includes(Buffer.from(version))) throw new Error(`the build isn't version ${version}. Rebuild it`);
  // Publish the bytes that were checked, not a path that could change afterwards.
  const folder = await mkdtemp(path.join(tmpdir(), "s1-firmware-"));
  try {
    const image = path.join(folder, "firmware.bin");
    await writeFile(image, bytes, { mode: 0o600 });
    const { stdout } = await execute(process.execPath, [PUBLISH_SCRIPT, "--config", configPath, "--bin", image, "--version", version]);
    return { version, minBattery: JSON.parse(stdout).min_battery };
  } finally {
    await rm(folder, { recursive: true, force: true });
  }
}
