import { createHash, randomBytes } from "node:crypto";
import { readFile, writeFile, mkdir, chmod, access, stat, rename, rm } from "node:fs/promises";
import { constants, createReadStream, createWriteStream } from "node:fs";
import path from "node:path";
import os from "node:os";
import { Readable, Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import JSON5 from "json5";
import { firmwareCapability } from "./firmware-automation.mjs";
import { loadFocusAudioBundle } from "./focus-audio.mjs";
import { CLAUDE_CODE_PROVIDER, OPENCLAW_PROVIDER, claudeCodeSettings, readClaudeLogin, resolveUpstreamProvider } from "./claude-code-client.mjs";
import { writeJsonAtomic } from "./utils.mjs";

const packageRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

// Pinned so a replaced download can never reach the speech pipeline.
export const WHISPER_MODEL = Object.freeze({
  name: "ggml-base.bin",
  url: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
  sha256: "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe",
  bytes: 147951465
});

// Services and plugin hosts can start with a short PATH that misses Homebrew.
const PROGRAM_DIRECTORIES = ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"];
export async function findProgram(name, env = process.env, directories = PROGRAM_DIRECTORIES) {
  for (const directory of [...String(env.PATH || "").split(path.delimiter), ...directories].filter(Boolean)) {
    const candidate = path.join(directory, name);
    try { await access(candidate, constants.X_OK); return candidate; } catch {}
  }
  return null;
}
export const defaultGatewayConfig = () => process.env.OPENCLAW_CONFIG_PATH ||
  path.join(process.env.OPENCLAW_STATE_DIR || path.join(os.homedir(), ".openclaw"), "openclaw.json");

export function gatewayToken(config, env = process.env) {
  const token = config.gateway?.auth?.token;
  if (typeof token === "string") {
    const ref = /^\$\{([A-Z_][A-Z0-9_]*)\}$/.exec(token);
    if (ref) return env[ref[1]] || "";
    return token;
  }
  if (token?.source === "env") return env[token.id] || "";
  return ""; // External SecretRefs must be resolved by their owner, not scraped.
}

function validAgent(value) {
  if (!/^[a-z0-9][a-z0-9_-]{0,63}$/.test(value)) throw new Error("Invalid agent ID");
  return value;
}

export function configuredAgentExists(gateway, agent) {
  const agents=gateway.agents || {};
  return (agent==="main"&&agents.ownership!=="explicit") ||
    agents.list?.some(record=>record.id===agent) || Object.hasOwn(agents.entries || {},agent);
}

function installProvider(options) {
  if (options.provider && ![CLAUDE_CODE_PROVIDER, OPENCLAW_PROVIDER].includes(options.provider))
    throw new Error(`Provider must be ${CLAUDE_CODE_PROVIDER} or ${OPENCLAW_PROVIDER}`);
  // Naming a gateway or agent is an explicit OpenClaw install. Otherwise Claude Code is the default.
  return options.provider || (options.gatewayConfig || options.gatewayTokenFile || options.gatewayUrl || options.agent
    ? OPENCLAW_PROVIDER : CLAUDE_CODE_PROVIDER);
}

function validatedInstallIdentity(options) {
  const deviceId = options.deviceId || "cardputer-adv-01";
  if (!/^[a-zA-Z0-9_-]{1,64}$/.test(deviceId)) throw new Error("Invalid device ID");
  const port = Number(options.port || 31889);
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error("Bridge port must be 1024..65535");
  const publicUrl = options.publicUrl ? new URL(options.publicUrl) : null;
  if (publicUrl && (publicUrl.protocol !== "https:" || publicUrl.username || publicUrl.password ||
      publicUrl.pathname !== "/" || publicUrl.search || publicUrl.hash))
    throw new Error("Public URL must be an HTTPS origin without credentials, path or query");
  return { deviceId, port, publicUrl };
}

function applyNewPaths(config, { stateDir, port }) {
  config.server = { host: "127.0.0.1", port };
  config.stateDir = path.join(stateDir, "data");
  for (const [key, relative] of Object.entries({ inboundDir: "inbound", canonicalDir: "media/canonical", playbackDir: "media/playback", tmpDir: "media/tmp" }))
    config.media[key] = path.join(config.stateDir, relative);
  config.assets.rootDir = path.join(config.stateDir, "assets");
  config.assets.logsDir = path.join(config.stateDir, "logs");
  config.firmware.rootDir = path.join(config.stateDir, "firmware");
}

async function readExistingConfig(configPath) {
  try { return JSON.parse(await readFile(configPath, "utf8")); }
  catch (error) { if (error.code !== "ENOENT") throw error; return null; }
}

async function planClaudeCodeInstall(options) {
  const { deviceId, port, publicUrl } = validatedInstallIdentity(options);
  const stateDir = path.resolve(options.stateDir || path.join(os.homedir(), ".s1-adv"));
  const configPath = path.join(stateDir, "config.json");
  const existing = await readExistingConfig(configPath);
  const config = existing || JSON.parse(await readFile(path.join(packageRoot, "config.example.json"), "utf8"));
  if (!existing) {
    applyNewPaths(config, { stateDir, port });
    delete config.upstream.url;
    delete config.upstream.token;
    config.upstream.provider = CLAUDE_CODE_PROVIDER;
    config.omnibotShim = { ...config.omnibotShim,
      sttLocalModel: path.join(stateDir, "models", WHISPER_MODEL.name),
      sttLocalBin: await findProgram("whisper-cli") || config.omnibotShim.sttLocalBin,
      ttsFallbackBin: await findProgram("espeak-ng") || "espeak-ng" };
    config.devices = [{ device_id: deviceId, device_token: randomBytes(32).toString("hex"), default_topic_key: "cardputer:main", topics: ["cardputer:main"] }];
    config.topics = [{ topic_key: "cardputer:main", session_key: "agent:claude:cardputer", title: "Cardputer", contour_id: "claude" }];
    config.firmwareAutomation = { enabled: false };
    config.installation = { publicUrl: publicUrl?.origin || "" };
  }
  const settings = claudeCodeSettings(config);
  return { config, configPath, stateDir, existing: !!existing, provider: CLAUDE_CODE_PROVIDER,
    summary: { provider: CLAUDE_CODE_PROVIDER, configPath, stateDir, bridge: config.server,
      publicUrl: config.installation?.publicUrl || "not configured", model: settings.model, effort: settings.effort,
      existingConfigurationPreserved: !!existing, firmwareAutomation: false,
      writes: existing ? [] : ["config.json (0600)", "firmware.env (0600)"],
      notes: ["Uses the claude program and login already on this computer. No credentials are read or copied.",
        "Replies use your Claude plan limits. Tools are off, so the device can only chat and send device actions.",
        "Set Wi-Fi on the device, and expose the bridge through your own HTTPS proxy.",
        "For voice, run the voice command after this. See docs/CLAUDE-CODE.md."] }
  };
}

export async function planInstall(options = {}) {
  if (installProvider(options) === CLAUDE_CODE_PROVIDER) return planClaudeCodeInstall(options);
  const gatewayPath = path.resolve(options.gatewayConfig || defaultGatewayConfig());
  const gateway = JSON5.parse(await readFile(gatewayPath, "utf8"));
  const stateDir = path.resolve(options.stateDir || path.join(path.dirname(gatewayPath), "s1-adv"));
  const configPath = path.join(stateDir, "config.json");
  const agent = validAgent(options.agent || "cardputer");
  const { deviceId, port, publicUrl } = validatedInstallIdentity(options);
  const token = options.gatewayTokenFile
    ? (await readFile(path.resolve(options.gatewayTokenFile), "utf8")).trim() : gatewayToken(gateway);
  if (!token) throw new Error("Gateway token unavailable. Use an environment SecretRef or --gateway-token-file; never pass a token on the command line.");
  const gatewayPort = Number(gateway.gateway?.port || 18789);
  const upstreamUrl = options.gatewayUrl || `ws://127.0.0.1:${gatewayPort}`;
  const upstream = new URL(upstreamUrl);
  if (upstream.username || upstream.password || !["ws:", "wss:"].includes(upstream.protocol) ||
      (upstream.protocol === "ws:" && !["127.0.0.1", "localhost", "[::1]"].includes(upstream.hostname)))
    throw new Error("Gateway must use loopback WS or verified WSS");
  const existing = await readExistingConfig(configPath);
  const config = existing || JSON.parse(await readFile(path.join(packageRoot, "config.example.json"), "utf8"));
  if (!existing) {
    applyNewPaths(config, { stateDir, port });
    config.upstream.provider = OPENCLAW_PROVIDER;
    config.upstream.url = upstreamUrl;
    config.upstream.token = token;
    config.devices = [{ device_id: deviceId, device_token: randomBytes(32).toString("hex"), default_topic_key: "cardputer:main", topics: ["cardputer:main"] }];
    config.topics = [{ topic_key: "cardputer:main", session_key: `agent:${agent}:cardputer`, title: "Cardputer", contour_id: agent }];
    config.firmwareAutomation = { enabled: false };
    config.installation = { publicUrl: publicUrl?.origin || "", agentId: agent, gatewayConfigPath: gatewayPath };
  }
  const selectedAgent = config.installation?.agentId || agent;
  const agentExists = !!configuredAgentExists(gateway, selectedAgent);
  return { config, configPath, stateDir, gatewayPath, existing: !!existing,
    summary: { configPath, stateDir, bridge: config.server, gateway: config.upstream.url,
      publicUrl: config.installation?.publicUrl || "not configured", agent: selectedAgent, agentExists,
      existingConfigurationPreserved: !!existing, firmwareAutomation: config.firmwareAutomation?.enabled === true,
      writes: existing ? [] : ["config.json (0600)", "firmware.env (0600)", "openclaw-plugin-config.json (0600)"],
      notes: ["No gateway, channel, provider, OAuth or firewall settings are changed.",
        "Three English Focus cues are available for authenticated SD download. Other languages are optional manual downloads. Playback is controlled on the device; no audio is copied into firmware flash.",
        "The device token grants access to the selected agent. Use a dedicated restricted agent for shared devices.",
        "Firmware automation is off until explicitly provisioned. Voice needs the selected STT/TTS providers."] }
  };
}

async function modelReady(file, model) {
  try {
    if ((await stat(file)).size !== model.bytes) return false;
    const hash = createHash("sha256");
    await pipeline(createReadStream(file), hash);
    return hash.digest("hex") === model.sha256;
  } catch { return false; }
}

function voiceInstallHint(platform, missing) {
  const packages = { "whisper-cli": "whisper-cpp", "espeak-ng": "espeak-ng", ffmpeg: "ffmpeg" };
  if (platform === "darwin") return `brew install ${missing.map(name => packages[name]).join(" ")}`;
  return `Install ${missing.join(", ")}. On Debian or Ubuntu, apt provides espeak-ng and ffmpeg; whisper-cli comes from whisper.cpp.`;
}

export async function planVoice(configPath, { env = process.env, platform = process.platform, model = WHISPER_MODEL, directories = PROGRAM_DIRECTORIES } = {}) {
  const absolute = path.resolve(configPath);
  const config = JSON.parse(await readFile(absolute, "utf8"));
  const modelPath = path.join(path.dirname(absolute), "models", model.name);
  const programs = {};
  for (const name of ["whisper-cli", "espeak-ng", "ffmpeg"]) programs[name] = await findProgram(name, env, directories);
  const missing = Object.keys(programs).filter(name => !programs[name]);
  const ready = await modelReady(modelPath, model);
  return { configPath: absolute, config, model, modelPath, modelReady: ready, programs, missing,
    summary: { configPath: absolute, programs, missing, install: missing.length ? voiceInstallHint(platform, missing) : null,
      model: { path: modelPath, ready, downloadBytes: ready ? 0 : model.bytes },
      writes: [...(ready ? [] : [`${model.name} (${Math.round(model.bytes / 1048576)} MiB)`]), "voice paths in config.json (0600)"],
      notes: ["Audio stays on this computer. Only the transcribed text goes to the assistant.",
        "Restart the bridge after applying."] } };
}

export async function applyVoice(plan, { fetchImpl = fetch } = {}) {
  if (plan.missing.length) throw new Error(`Missing ${plan.missing.join(", ")}. ${plan.summary.install}`);
  if (!plan.modelReady) {
    const response = await fetchImpl(plan.model.url, { redirect: "follow" });
    if (!response.ok || !response.body) throw new Error(`Speech model download failed (HTTP ${response.status}).`);
    await mkdir(path.dirname(plan.modelPath), { recursive: true, mode: 0o700 });
    const partial = `${plan.modelPath}.part`, hash = createHash("sha256");
    const hashing = new Transform({ transform(chunk, _encoding, done) { hash.update(chunk); done(null, chunk); } });
    try {
      await pipeline(Readable.fromWeb(response.body), hashing, createWriteStream(partial, { mode: 0o600 }));
      if (hash.digest("hex") !== plan.model.sha256) throw new Error("Speech model checksum mismatch. Nothing was installed.");
      await rename(partial, plan.modelPath);
    } finally { await rm(partial, { force: true }); }
  }
  const config = JSON.parse(await readFile(plan.configPath, "utf8"));
  config.omnibotShim = { ...config.omnibotShim, sttLocalBin: plan.programs["whisper-cli"], sttLocalModel: plan.modelPath,
    ttsFallbackBin: plan.programs["espeak-ng"] };
  await writeJsonAtomic(plan.configPath, config);
  return { ...plan.summary, model: { path: plan.modelPath, ready: true, downloadBytes: 0 }, applied: true, restartRequired: true };
}

const envQuote = value => `'${String(value).replaceAll("'", "'\\''")}'`;
export async function applyInstall(plan) {
  if (plan.provider !== CLAUDE_CODE_PROVIDER && !plan.summary.agentExists) throw new Error("Selected agent does not exist. Create a restricted cardputer agent with OpenClaw, or explicitly select an existing agent, then repeat setup.");
  await mkdir(plan.stateDir, { recursive: true, mode: 0o700 });
  await chmod(plan.stateDir,0o700);
  if (plan.existing) return { ...plan.summary, applied: false, reason: "Existing configuration retained; device token unchanged." };
  const outputs = ["config.json", "firmware.env", ...(plan.provider === CLAUDE_CODE_PROVIDER ? [] : ["openclaw-plugin-config.json"])];
  for (const name of outputs) {
    try { await stat(path.join(plan.stateDir, name)); }
    catch (error) { if (error.code === "ENOENT") continue; throw error; }
    throw new Error("Setup output already exists; refusing to overwrite private files.");
  }
  const publicUrl = plan.config.installation.publicUrl ? new URL(plan.config.installation.publicUrl) : null;
  const device = plan.config.devices[0];
  const values = { DEFAULT_WIFI_SSID: "", DEFAULT_WIFI_PASSWORD: "", DEFAULT_HUB_HOST: publicUrl?.hostname || "",
    DEFAULT_HUB_PORT: publicUrl?.port || "443", DEFAULT_DEVICE_ID: device.device_id,
    DEFAULT_DEVICE_TOKEN: device.device_token, DEFAULT_TOPIC_KEY: device.default_topic_key };
  await writeFile(plan.configPath, `${JSON.stringify(plan.config, null, 2)}\n`, { mode: 0o600, flag: "wx" });
  await writeFile(path.join(plan.stateDir, "firmware.env"),
    "# Private. Set Wi-Fi locally or use device setup. Never commit or share this file.\n" +
    Object.entries(values).map(([k,v]) => `${k}=${envQuote(v)}`).join("\n") + "\n", { mode: 0o600, flag: "wx" });
  if (plan.provider !== CLAUDE_CODE_PROVIDER) await writeFile(path.join(plan.stateDir, "openclaw-plugin-config.json"),
    `${JSON.stringify({ plugins: { entries: { "s1-adv": { enabled: true, config: { configPath: plan.configPath, autoStart: true } } } } }, null, 2)}\n`,
    { mode: 0o600, flag: "wx" });
  return { ...plan.summary, applied: true };
}

export async function doctor(configPath, { probe = true, fetchImpl = fetch, claudeLogin = readClaudeLogin } = {}) {
  const absolute = path.resolve(configPath);
  const config = JSON.parse(await readFile(absolute, "utf8"));
  const provider = resolveUpstreamProvider(config);
  const checks = [];
  const add = (name, ok, detail) => checks.push({ name, ok, detail });
  const mode = (await stat(absolute)).mode & 0o777;
  add("private-config", process.platform === "win32" || (mode & 0o077) === 0, "Config should be readable only by its owner.");
  add("device-token", config.devices?.every(d => typeof d.device_token === "string" && d.device_token.length >= 32), "One unique random token per device.");
  add("single-device",config.devices?.length===1,"One device identity per bridge instance; keep private state/media/OTA directories separate.");
  add("loopback-listener", ["127.0.0.1", "::1", "localhost"].includes(config.server?.host), "Expose through a verified TLS proxy, not a public raw listener.");
  if (provider === CLAUDE_CODE_PROVIDER) {
    const login = await claudeLogin({ command: claudeCodeSettings(config).command });
    add("claude-cli", login.installed, login.installed ? "Available" : login.detail);
    add("claude-subscription", login.subscription, login.detail);
  } else try {
    add("gateway-token", !!config.upstream?.token && config.upstream.token !== "replace-me", "Gateway token present; value omitted.");
    const gateway = JSON5.parse(await readFile(config.installation?.gatewayConfigPath || defaultGatewayConfig(), "utf8"));
    const agent = config.installation?.agentId || config.topics?.[0]?.session_key?.split(":")[1];
    add("agent-config", !!configuredAgentExists(gateway,agent), "Configured session must name an existing agent; model response is checked separately.");
    if(config.modelRouting?.enabled) {
      const policy=gateway.plugins?.entries?.['s1-adv']?.llm;
      add('fast-completion-restricted',config.modelRouting.fast?.execution==='isolated-agent-runtime' &&
        policy?.allowModelOverride===true && policy?.allowAuthProfileOverride===false &&
        policy?.allowedCompletionModels?.length===1 && policy.allowedCompletionModels[0]===config.modelRouting.fast.model,
        'Fast role uses native isolated zero-tool completion with the exact model allowlist. No added agent or Gateway admin scope.');
      add('model-roles',!!config.modelRouting.fast?.model && !!config.modelRouting.smart?.model,
        'Fast/smart model choices are explicit; real responses and latency require a separate practical check.');
    }
  } catch { add("agent-config", false, "Cannot verify selected agent against the local gateway configuration."); }
  const voiceHelp = "See docs/CLAUDE-CODE.md#add-voice, or run the voice setup command.";
  const speech = [["ffmpeg", "ffmpeg", "-version"]];
  if (config.omnibotShim?.ttsFallbackProvider === "espeak")
    speech.push(["espeak-ng", config.omnibotShim.ttsFallbackBin || "espeak-ng", "--version"]);
  for (const [name, binary, flag] of speech) {
    const found = spawnSync(binary, [flag], { encoding: "utf8", timeout: 5000 });
    add(name, found.status === 0, found.status === 0 ? "Available" : `Install for configured audio support. ${voiceHelp}`);
  }
  if (config.omnibotShim?.sttProvider === "local-whisper") {
    for (const [name, file, permission] of [["whisper-binary", config.omnibotShim.sttLocalBin, constants.X_OK], ["whisper-model", config.omnibotShim.sttLocalModel, constants.R_OK]]) {
      let ok = false; try { await access(file, permission); ok = true; } catch {}
      add(name, ok, ok ? "Available" : `Configured local speech dependency is missing; text chat can still work. ${voiceHelp}`);
    }
  }
  if (probe) {
    const host = config.server?.host;
    const port = Number(config.server?.port);
    const privateConfig = checks.find(check => check.name === "private-config")?.ok;
    const token = config.devices?.length === 1 ? config.devices[0]?.device_token : null;
    const safeProbe = privateConfig && ["127.0.0.1", "::1", "localhost"].includes(host) &&
      Number.isInteger(port) && port >= 1 && port <= 65535 &&
      typeof token === "string" && token.length >= 32 && !/[\r\n]/.test(token);
    if (!safeProbe) {
      add("bridge-http", false, "Authenticated diagnostics require private configuration, one device token and an exact loopback host/port. No request sent.");
    } else try {
      // Pin localhost to a numeric loopback address; never resolve an arbitrary
      // configured hostname or follow redirects while carrying a device token.
      const endpointHost = host === "::1" ? "[::1]" : "127.0.0.1";
      const response = await fetchImpl(`http://${endpointHost}:${port}/healthz`, {
        redirect: "error", headers: { Authorization: `Bearer ${token}` }, signal: AbortSignal.timeout(5000) });
      const body = await response.json();
      add("bridge-http", response.ok && body.ok === true, `HTTP ${response.status}`);
      add("upstream", body.upstream_connected === true, provider === CLAUDE_CODE_PROVIDER
        ? "Checks the Claude Code login seen by the bridge, not a model answer."
        : "Checks the real gateway connection, not a model answer.");
      add("delivery", body.delivery_ready === true, body.delivery_hold?.message ||
        "The bridge must explicitly confirm delivery readiness; a legacy health response is not qualification.");
    } catch { add("bridge-http", false, "Not reachable. Start the reviewed bridge/plugin and approve its exact device identity if requested."); }
  }
  const firmware = await firmwareCapability(config);
  let focusAudio = { enabled: config.assets?.bundledFocusAudio !== false, available: false };
  if (focusAudio.enabled) {
    try {
      const files = await loadFocusAudioBundle();
      focusAudio = { ...focusAudio, available: true, files: files.length,
        bytes: files.reduce((sum, file) => sum + file.size, 0) };
    } catch { focusAudio.error = "Bundled Focus audio is missing or invalid; text delivery is independent."; }
  }
  const textChecks = ["private-config","device-token","single-device","loopback-listener","bridge-http","upstream","delivery",
    ...(provider === CLAUDE_CODE_PROVIDER ? ["claude-cli","claude-subscription"] : ["gateway-token","agent-config"])];
  return { configPath: absolute, provider, checks, textReady: probe && checks.filter(c => textChecks.includes(c.name)).every(c => c.ok),
    audioDependenciesReady: checks.filter(c => ["ffmpeg","espeak-ng","whisper-binary","whisper-model"].includes(c.name)).every(c => c.ok),
    firmwareAutomationEnabled: config.firmwareAutomation?.enabled === true, firmwareCapability: firmware, focusAudio,
    note: "A model round trip and physical-device acceptance are separate checks." };
}

export async function enablePlugin(configPath, { apply = false, gatewayConfigPath = defaultGatewayConfig(), runCommand = spawnSync, openclawCommand = ["openclaw"] } = {}) {
  if (!Array.isArray(openclawCommand) || !openclawCommand.length ||
      openclawCommand.some(value => typeof value !== "string" || !value || value.includes("\0")))
    throw new Error("Invalid OpenClaw CLI invocation");
  const [program, ...prefix] = openclawCommand;
  if (resolveUpstreamProvider(JSON.parse(await readFile(path.resolve(configPath), "utf8"))) === CLAUDE_CODE_PROVIDER)
    throw new Error("Claude Code mode runs as a standalone bridge. It needs no OpenClaw plugin entry.");
  gatewayConfigPath = path.resolve(gatewayConfigPath);
  const report = await doctor(configPath, { probe: false });
  const required = ["private-config", "device-token", "single-device", "loopback-listener", "gateway-token", "agent-config"];
  if (report.checks.some(check => required.includes(check.name) && !check.ok))
    throw new Error("Configuration is not ready to enable. Resolve the failed doctor checks first.");
  const host = JSON5.parse(await readFile(gatewayConfigPath, "utf8"));
  const operations = [];
  if (Array.isArray(host.plugins?.allow) && !host.plugins.allow.includes("s1-adv"))
    operations.push(["config", "set", "plugins.allow", JSON.stringify([...host.plugins.allow,"s1-adv"]), "--strict-json"]);
  const prior=host.plugins?.entries?.['s1-adv'] || {};
  operations.push(["config", "set", "plugins.entries.s1-adv", JSON.stringify({ ...prior, enabled:true,
    config:{...prior.config,configPath:path.resolve(configPath),autoStart:true} }), "--strict-json"]);
  if (!apply) return { dryRun:true, gatewayConfigPath, openclawCommand, operations, note:"Only this plugin entry and its allowlist membership are changed. Gateway restart remains an explicit operator step." };
  const backup = path.join(path.dirname(configPath), `gateway-config-before-enable-${Date.now()}.json5`);
  await writeFile(backup, await readFile(gatewayConfigPath), { mode:0o600, flag:"wx" });
  for (const args of operations) {
    const result = runCommand(program,[...prefix,...args],{encoding:"utf8",timeout:30000,
      env:{...process.env,OPENCLAW_CONFIG_PATH:gatewayConfigPath}});
    if (result.status !== 0) {
      const code = /^[A-Z0-9_]+$/u.test(result.error?.code || "") ? result.error.code : "none";
      throw new Error(`OpenClaw rejected the plugin configuration (exit=${result.status ?? "none"}, signal=${result.signal || "none"}, error=${code}). Inspect its config locally; private backup: ${backup}. No restart was performed.`);
    }
  }
  return { enabled:true, gatewayConfigPath, backup, restartRequired:true, note:"Restart your intended Gateway when ready; existing channels and agents were preserved." };
}
