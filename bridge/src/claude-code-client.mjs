import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { EventEmitter } from "node:events";
import { mkdir, readFile } from "node:fs/promises";
import path from "node:path";
import { sleep, writeJsonAtomic } from "./utils.mjs";

// Runs each Cardputer turn through the owner's installed, unmodified `claude`
// program and its own subscription login. The bridge never reads, stores or
// forwards Claude credentials. It exposes the same interface as
// UpstreamGatewayClient, so the turn outbox and delivery path do not change.

export const CLAUDE_CODE_PROVIDER = "claude-code";
export const OPENCLAW_PROVIDER = "openclaw";
export const CLAUDE_CODE_DEFAULTS = Object.freeze({
  command: "claude",
  model: "claude-opus-5-5",
  effort: "xhigh",
  turnTimeoutMs: 600000
});

const EFFORTS = new Set(["low", "medium", "high", "xhigh", "max"]);
const MAX_OUTPUT_BYTES = 8 * 1024 * 1024;
const MAX_FINISHED_RUNS = 256;
const SESSION_ID = /^[A-Za-z0-9_-]{8,128}$/;

// An allowlist, not a denylist: ANTHROPIC_API_KEY, ANTHROPIC_AUTH_TOKEN and
// provider switches would silently replace the subscription login, and any
// other host secret has no reason to reach this process.
const CHILD_ENV = /^(HOME|USER|LOGNAME|PATH|SHELL|TMPDIR|TEMP|TMP|LANG|LC_[A-Z]+|TZ|TERM|XDG_[A-Z_]+|CLAUDE_CONFIG_DIR|CLAUDE_CODE_OAUTH_TOKEN|NODE_EXTRA_CA_CERTS|HTTPS?_PROXY|https?_proxy|ALL_PROXY|all_proxy|NO_PROXY|no_proxy)$/;

// Each flag is load-bearing. Without --restricted a tool-free run still loads
// the owner's global CLAUDE.md, memory paths and MCP servers; see
// docs/CLAUDE-CODE.md before changing this list.
const ISOLATION_ARGS = Object.freeze([
  "--restricted",
  "--tools", "",
  "--strict-mcp-config",
  "--disable-slash-commands"
]);

// Fixed text only: provider error messages are never shown or logged.
const FAILURE_REPLIES = Object.freeze({
  auth: "Claude Code is not signed in on the bridge computer. Run claude auth login there, then try again.",
  limit: "Your Claude plan limit has been reached. Try again after it resets.",
  timeout: "Claude took too long, so this request was stopped. Try again or ask something shorter."
});

export function resolveUpstreamProvider(config) {
  const provider = config?.upstream?.provider;
  // A config that already names a gateway was written for OpenClaw and keeps
  // working. Claude Code is the default for everything else.
  if (provider === undefined || provider === null || provider === "")
    return config?.upstream?.url ? OPENCLAW_PROVIDER : CLAUDE_CODE_PROVIDER;
  if (provider !== CLAUDE_CODE_PROVIDER && provider !== OPENCLAW_PROVIDER)
    throw new Error(`upstream.provider must be ${CLAUDE_CODE_PROVIDER} or ${OPENCLAW_PROVIDER}`);
  return provider;
}

export function claudeCodeSettings(config) {
  const settings = { ...CLAUDE_CODE_DEFAULTS, ...(config?.upstream?.claudeCode || {}) };
  if (typeof settings.command !== "string" || !settings.command.trim() || settings.command.includes("\0"))
    throw new Error("upstream.claudeCode.command must name the claude program");
  if (typeof settings.model !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:[\]-]{0,127}$/.test(settings.model))
    throw new Error("upstream.claudeCode.model is not a valid model name");
  if (!EFFORTS.has(settings.effort))
    throw new Error("upstream.claudeCode.effort must be low, medium, high, xhigh or max");
  if (!Number.isInteger(settings.turnTimeoutMs) || settings.turnTimeoutMs < 5000 || settings.turnTimeoutMs > 3600000)
    throw new Error("upstream.claudeCode.turnTimeoutMs must be 5000..3600000");
  return settings;
}

export function validateClaudeCodeConfig(config) {
  claudeCodeSettings(config);
  if (config.modelRouting?.enabled)
    throw new Error("modelRouting needs the OpenClaw provider; set upstream.claudeCode.model instead.");
  if (config.firmwareAutomation?.enabled)
    throw new Error("firmwareAutomation needs the OpenClaw provider; Claude Code mode has no tools.");
}

export function claudeEnv(env = process.env) {
  return Object.fromEntries(Object.entries(env).filter(([name]) => CHILD_ENV.test(name)));
}

export function claudeTurnArgs(settings, { appendSystemPrompt = "", sessionId = null } = {}) {
  return [
    "-p", "--output-format", "json",
    "--model", settings.model,
    "--effort", settings.effort,
    ...ISOLATION_ARGS,
    ...(appendSystemPrompt ? ["--append-system-prompt", appendSystemPrompt] : []),
    ...(sessionId ? ["--resume", sessionId] : [])
  ];
}

function runClaude(spawnImpl, command, args, { cwd, env, input = "", timeoutMs }) {
  return new Promise(resolve => {
    let child;
    try {
      child = spawnImpl(command, args, { cwd, env, stdio: ["pipe", "pipe", "pipe"] });
    } catch (error) {
      resolve({ code: null, stdout: "", stderr: "", spawnError: error });
      return;
    }
    const output = { stdout: [], stderr: [], bytes: 0 };
    let timedOut = false, overflowed = false, spawnError = null, escalate = null, settled = false;
    const stop = () => {
      child.kill("SIGTERM");
      escalate = setTimeout(() => child.kill("SIGKILL"), 5000);
      escalate.unref?.();
    };
    const timer = setTimeout(() => { timedOut = true; stop(); }, timeoutMs);
    const collect = stream => chunk => {
      output.bytes += chunk.length;
      if (output.bytes > MAX_OUTPUT_BYTES) {
        if (!overflowed) { overflowed = true; stop(); }
        return;
      }
      output[stream].push(chunk);
    };
    child.stdout.on("data", collect("stdout"));
    child.stderr.on("data", collect("stderr"));
    const finish = code => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      clearTimeout(escalate);
      resolve({ code, timedOut, overflowed, spawnError,
        stdout: Buffer.concat(output.stdout).toString("utf8"),
        stderr: Buffer.concat(output.stderr).toString("utf8") });
    };
    child.on("error", error => { spawnError = error; finish(null); });
    child.on("close", finish);
    child.stdin.on("error", () => {});
    child.stdin.end(input);
  });
}

export async function readClaudeLogin({ command = CLAUDE_CODE_DEFAULTS.command, spawnImpl = spawn, env = process.env } = {}) {
  const result = await runClaude(spawnImpl, command, ["auth", "status", "--json"],
    { env: claudeEnv(env), timeoutMs: 15000 });
  if (result.spawnError)
    return { installed: false, subscription: false, detail: "Claude Code was not found. Install it or set upstream.claudeCode.command." };
  let status = null;
  try { status = JSON.parse(result.stdout); } catch {}
  if (status?.loggedIn !== true)
    return { installed: true, subscription: false, detail: "Claude Code is not signed in. Run claude auth login." };
  if (status.apiProvider !== "firstParty" || typeof status.subscriptionType !== "string" || !status.subscriptionType)
    return { installed: true, subscription: false, detail: "Claude Code is not using a Claude subscription login." };
  return { installed: true, subscription: true, detail: `Signed in with a Claude ${status.subscriptionType} subscription.` };
}

function classifyFailure(detail) {
  if (/no conversation found|session[^\n]*not found/iu.test(detail)) return "missing-session";
  if (/not logged in|\/login|invalid api key|authentication|unauthorized|oauth token/iu.test(detail)) return "auth";
  if (/usage limit|rate limit|limit reached|hit your[^\n]*limit|quota/iu.test(detail)) return "limit";
  return "failed";
}

export function parseClaudeResult({ code, stdout, stderr, timedOut, overflowed, spawnError }) {
  if (timedOut) return { category: "timeout" };
  if (spawnError) return { category: "failed" };
  if (overflowed) return { category: "failed" };
  let result = null;
  try { result = JSON.parse(stdout); } catch {}
  const sessionId = typeof result?.session_id === "string" && SESSION_ID.test(result.session_id) ? result.session_id : null;
  const text = typeof result?.result === "string" ? result.result.trim() : "";
  if (code === 0 && result?.type === "result" && result.is_error === false && text)
    return { text, sessionId };
  return { category: classifyFailure(`${text}\n${stderr || ""}`), sessionId };
}

export class ClaudeCodeClient extends EventEmitter {
  constructor(config, { appendSystemPrompt = "", spawnImpl = spawn, env = process.env, retryDelayMs = 1000 } = {}) {
    super();
    this.settings = claudeCodeSettings(config);
    this.appendSystemPrompt = appendSystemPrompt;
    this.spawnImpl = spawnImpl;
    this.env = env;
    this.retryDelayMs = retryDelayMs;
    const root = path.join(config.stateDir, "claude-code");
    this.workspace = path.join(root, "workspace");
    this.sessionsPath = path.join(root, "sessions.json");
    this.sessions = new Map();
    this.runs = new Map();
    this.active = new Map();
    this.children = new Set();
    this.connected = false;
    this.shouldRun = true;
    this.watchingLogin = false;
    this.firstCheck = new Promise(resolve => { this.finishFirstCheck = resolve; });
  }

  async start() {
    await mkdir(this.workspace, { recursive: true, mode: 0o700 });
    await this.loadSessions();
    await this.watchLogin();
  }

  async stop() {
    this.shouldRun = false;
    this.connected = false;
    for (const child of this.children) child.kill("SIGTERM");
  }

  async watchLogin() {
    if (this.watchingLogin) return;
    this.watchingLogin = true;
    let delayMs = this.retryDelayMs;
    try {
      while (this.shouldRun && !this.connected) {
        const login = await readClaudeLogin({ command: this.settings.command, spawnImpl: this.spawnImpl, env: this.env });
        this.connected = login.subscription;
        this.finishFirstCheck();
        if (this.connected) return;
        console.warn(`[claude-code] waiting for login: ${login.detail}`);
        this.emit("warning", { type: "claude_code_login_required", error: new Error(login.detail) });
        await sleep(delayMs);
        delayMs = Math.min(delayMs * 2, 60000);
      }
    } finally {
      this.watchingLogin = false;
    }
  }

  async loadSessions() {
    try {
      const saved = JSON.parse(await readFile(this.sessionsPath, "utf8"));
      for (const [key, id] of Object.entries(saved.sessions || {}))
        if (typeof id === "string" && SESSION_ID.test(id)) this.sessions.set(key, id);
    } catch (error) {
      if (error.code !== "ENOENT") console.warn("[claude-code] saved sessions unreadable; starting new conversations");
    }
  }

  async saveSessions() {
    try {
      await writeJsonAtomic(this.sessionsPath, { version: 1, sessions: Object.fromEntries(this.sessions) });
    } catch {
      // The reply is already complete. The next turn can only lose continuity.
      console.warn("[claude-code] could not save the conversation id");
    }
  }

  async subscribeSessionMessages() {}

  async rpc(method, params = {}) {
    // Must answer even while signed out, or a held turn could never finish.
    if (method === "agent.wait") return this.runReceipt(params.runId);
    await this.firstCheck;
    if (!this.connected) throw new Error("Claude Code is not signed in with a subscription on the bridge host.");
    switch (method) {
      case "sessions.send": return this.send(params);
      case "sessions.get":
      case "sessions.create": return { key: params.key };
      // The bridge's own StateStore keeps the visible history.
      case "chat.history": return { sessionKey: params.sessionKey, messages: [] };
      case "sessions.list": return { sessions: [] };
      default: throw new Error(`Claude Code mode does not support ${method}.`);
    }
  }

  runReceipt(runId) {
    const run = this.runs.get(runId);
    // Unknown means a restart interrupted it. Nothing owns that turn any more,
    // and reporting it as still running would block the session lane forever.
    if (!run) return { runId, status: "error", endedAt: Date.now() };
    return run.status === "running" ? { runId, status: "timeout" } : { runId, status: run.status, endedAt: run.endedAt };
  }

  send({ key, message, idempotencyKey }) {
    const runId = String(idempotencyKey || randomUUID());
    if (this.runs.has(runId)) return { runId };
    if (this.active.has(key)) throw new Error("A Claude Code turn is already running in this session.");
    this.runs.set(runId, { status: "running" });
    this.active.set(key, runId);
    void this.runTurn({ key, message, runId });
    return { runId };
  }

  async runTurn({ key, message, runId }) {
    let outcome = await this.invoke(message, this.sessions.get(key));
    if (outcome.category === "missing-session") {
      // Claude Code expires old transcripts. Continue in a new conversation.
      this.sessions.delete(key);
      outcome = await this.invoke(message, null);
    }
    if (outcome.sessionId) {
      this.sessions.set(key, outcome.sessionId);
      await this.saveSessions();
    }
    if (outcome.category === "auth") {
      this.connected = false;
      void this.watchLogin();
    }
    const failed = !outcome.text;
    if (failed) console.warn(`[claude-code] turn failed category=${outcome.category}`);
    const text = outcome.text || FAILURE_REPLIES[outcome.category];
    if (text) this.emitReply(key, runId, text);
    this.finishRun(key, runId, failed ? "error" : "ok");
  }

  invoke(message, sessionId) {
    const args = claudeTurnArgs(this.settings, { appendSystemPrompt: this.appendSystemPrompt, sessionId });
    let tracked;
    const run = runClaude(this.trackedSpawn(child => { tracked = child; }), this.settings.command, args, {
      cwd: this.workspace, env: claudeEnv(this.env), input: message, timeoutMs: this.settings.turnTimeoutMs });
    return run.then(result => {
      if (tracked) this.children.delete(tracked);
      return parseClaudeResult(result);
    });
  }

  trackedSpawn(onSpawn) {
    return (...args) => {
      const child = this.spawnImpl(...args);
      this.children.add(child);
      onSpawn(child);
      return child;
    };
  }

  emitReply(sessionKey, runId, text) {
    this.emit("event", { type: "event", event: "chat", payload: {
      state: "final", sessionKey, runId, stopReason: "stop",
      message: { role: "assistant", content: [{ type: "text", text }], timestamp: Date.now() }
    } });
  }

  finishRun(key, runId, status) {
    this.runs.set(runId, { status, endedAt: Date.now() });
    this.active.delete(key);
    const finished = [...this.runs].filter(([, run]) => run.status !== "running");
    for (const [id] of finished.slice(0, Math.max(0, finished.length - MAX_FINISHED_RUNS))) this.runs.delete(id);
  }
}
