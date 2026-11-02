import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { realpathSync } from "node:fs";
import { chmod, mkdir, mkdtemp, readFile, realpath, rm, stat, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { homedir, tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { setTimeout as sleep } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import { doctor, enablePlugin, planInstall, applyInstall } from "./install.mjs";
import { planModelRoles } from "./model-roles.mjs";
import { BridgeRuntime, cleanupTranscriptText, loadConfig } from "./server.mjs";
import { voiceReplyWaitMs } from "./turn-delivery-policy.mjs";
import {
  CLAUDE_CODE_DEFAULTS, ClaudeCodeClient, claudeAskArgs, claudeCodeSettings, claudeEnv, claudeTurnArgs, parseClaudeResult,
  projectSettings, readClaudeLogin, recoveredConversation, resolveUpstreamProvider, validateClaudeCodeConfig
} from "./claude-code-client.mjs";

const bridgeRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const MAX_AUTH = { loggedIn: true, authMethod: "claude.ai", apiProvider: "firstParty", subscriptionType: "max" };
const FIRST_ID = "11111111-1111-4111-8111-111111111111";
const NEXT_ID = "22222222-2222-4222-8222-222222222222";
const success = (text, sessionId = FIRST_ID) => ({ stdout: JSON.stringify({
  type: "result", subtype: "success", is_error: false, result: text, session_id: sessionId }) });
const failure = (text, extra = {}) => ({ code: 1, stdout: JSON.stringify({
  type: "result", subtype: "error_during_execution", is_error: true, result: text, ...extra }) });

// A stand-in for the real program. It reads its behaviour from files because
// the bridge deliberately hides most of the environment from it.
const FAKE_CLAUDE = `#!${process.execPath}
const fs = require("fs"), path = require("path");
const here = __dirname, argv = process.argv.slice(2);
if (argv[0] === "auth") {
  const auth = fs.readFileSync(path.join(here, "auth.json"), "utf8");
  process.stdout.write(auth);
  process.exitCode = JSON.parse(auth).loggedIn ? 0 : 1;
} else {
  let stdin = "";
  process.stdin.setEncoding("utf8").on("data", chunk => { stdin += chunk; }).on("end", () => {
    fs.appendFileSync(path.join(here, "calls.jsonl"), JSON.stringify({ argv, stdin, cwd: process.cwd(), env: Object.keys(process.env), pid: process.pid }) + "\\n");
    const steps = JSON.parse(fs.readFileSync(path.join(here, "steps.json"), "utf8"));
    const step = steps.shift() || { code: 3 };
    fs.writeFileSync(path.join(here, "steps.json"), JSON.stringify(steps));
    if (step.hang) { setInterval(() => {}, 1000); return; }
    setTimeout(() => {
      if (step.stderr) process.stderr.write(step.stderr);
      process.stdout.write(step.stdout || "");
      process.exitCode = step.code || 0;
    }, step.delayMs || 0);
  });
}
`;

async function makeFake(t, { steps = [], auth = MAX_AUTH } = {}) {
  const dir = await mkdtemp(path.join(tmpdir(), "s1-claude-code-"));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const command = path.join(dir, "claude");
  await writeFile(command, FAKE_CLAUDE);
  await chmod(command, 0o755);
  await writeFile(path.join(dir, "auth.json"), JSON.stringify(auth));
  await writeFile(path.join(dir, "steps.json"), JSON.stringify(steps));
  return { dir, command,
    calls: async () => {
      const raw = await readFile(path.join(dir, "calls.jsonl"), "utf8").catch(() => "");
      return raw.trim() ? raw.trim().split("\n").map(line => JSON.parse(line)) : [];
    } };
}

function makeClient(fake, overrides = {}, options = {}) {
  const config = { stateDir: path.join(fake.dir, "state"),
    upstream: { provider: "claude-code", claudeCode: { command: fake.command, ...overrides } } };
  const client = new ClaudeCodeClient(config, { retryDelayMs: 20, ...options });
  const events = [];
  client.on("event", frame => events.push(frame));
  return { client, events };
}

async function until(check, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const value = await check();
    if (value) return value;
    await sleep(10);
  }
  throw new Error("condition not reached");
}

const finished = (client, runId) => until(() => {
  const receipt = client.runReceipt(runId);
  return receipt.status === "timeout" ? null : receipt;
});
const send = (client, message, idempotencyKey, key = "agent:claude:cardputer") =>
  client.rpc("sessions.send", { key, message, idempotencyKey });
const replyText = frame => frame.payload.message.content[0].text;

test("provider defaults to Claude Code, and configs naming a gateway stay on OpenClaw", () => {
  assert.equal(resolveUpstreamProvider({}), "claude-code");
  assert.equal(resolveUpstreamProvider({ upstream: {} }), "claude-code");
  assert.equal(resolveUpstreamProvider({ upstream: { url: "ws://127.0.0.1:18789", token: "t" } }), "openclaw");
  assert.equal(resolveUpstreamProvider({ upstream: { provider: "claude-code", url: "ws://127.0.0.1:18789" } }), "claude-code");
  assert.equal(resolveUpstreamProvider({ upstream: { provider: "openclaw" } }), "openclaw");
  assert.throws(() => resolveUpstreamProvider({ upstream: { provider: "other" } }), /upstream.provider/);
});

test("voice replies wait for Claude, while OpenClaw keeps the device's requested wait", () => {
  assert.equal(voiceReplyWaitMs("1000", 25000, "claude-code"), 10000);
  assert.equal(voiceReplyWaitMs("1000", 25000, "openclaw"), 1000);
  assert.equal(voiceReplyWaitMs("1000", 25000, undefined), 1000);
});

test("defaults are Opus 5.5 at extra high effort", async () => {
  assert.equal(CLAUDE_CODE_DEFAULTS.model, "claude-opus-5-5");
  assert.equal(CLAUDE_CODE_DEFAULTS.effort, "xhigh");
  const example = JSON.parse(await readFile(path.join(bridgeRoot, "config.example.json"), "utf8"));
  assert.equal(resolveUpstreamProvider(example), "claude-code");
  assert.deepEqual({ ...CLAUDE_CODE_DEFAULTS }, example.upstream.claudeCode);
});

test("settings reject values that could become flags or cannot run", () => {
  const settings = claudeCode => claudeCodeSettings({ upstream: { claudeCode } });
  assert.throws(() => settings({ model: "--dangerously-skip-permissions" }), /model/);
  assert.throws(() => settings({ effort: "extra" }), /effort/);
  assert.throws(() => settings({ turnTimeoutMs: 1 }), /turnTimeoutMs/);
  assert.throws(() => settings({ command: "" }), /command/);
  assert.throws(() => validateClaudeCodeConfig({ upstream: {}, modelRouting: { enabled: true } }), /OpenClaw/);
  assert.throws(() => validateClaudeCodeConfig({ upstream: {}, firmwareAutomation: { enabled: true } }), /OpenClaw/);
});

test("a quick question beside the chat is locked down like the chat and keeps nothing", () => {
  const args = claudeAskArgs(claudeCodeSettings({ upstream: { claudeCode: { projectDir: "/srv/project" } } }), "the rules");
  for (const flag of ["--restricted", "--strict-mcp-config", "--disable-slash-commands", "--no-session-persistence"])
    assert.ok(args.includes(flag), flag);
  assert.equal(args[args.indexOf("--tools") + 1], "");
  assert.equal(args[args.indexOf("--system-prompt") + 1], "the rules");
  assert.equal(args[args.indexOf("--model") + 1], CLAUDE_CODE_DEFAULTS.askModel);
  assert.equal(args[args.indexOf("--effort") + 1], "low");
  // The project and web access belong to the chat.
  assert.ok(!args.includes("--settings") && !args.includes("--add-dir") && !args.includes("--allowedTools"));
  assert.ok(!args.includes("--resume"));
});

test("the quick model can be changed, but not into a flag", () => {
  assert.equal(claudeCodeSettings({ upstream: { claudeCode: { askModel: "claude-sonnet-5-5", askEffort: "medium" } } }).askModel, "claude-sonnet-5-5");
  assert.throws(() => claudeCodeSettings({ upstream: { claudeCode: { askModel: "--dangerous" } } }), /askModel/);
  assert.throws(() => claudeCodeSettings({ upstream: { claudeCode: { askEffort: "turbo" } } }), /askEffort/);
});

test("the child environment cannot select API billing or see host secrets", () => {
  const env = claudeEnv({ HOME: "/home/me", PATH: "/bin", LC_ALL: "C", CLAUDE_CODE_OAUTH_TOKEN: "subscription",
    ANTHROPIC_API_KEY: "secret", ANTHROPIC_AUTH_TOKEN: "secret", ANTHROPIC_BASE_URL: "https://evil.example",
    CLAUDE_CODE_USE_BEDROCK: "1", OPENAI_API_KEY: "secret", CARDPUTER_BRIDGE_CONFIG: "/private", CLAUDECODE: "1" });
  assert.deepEqual(Object.keys(env).sort(), ["CLAUDE_CODE_OAUTH_TOKEN", "HOME", "LC_ALL", "PATH"]);
});

test("project access is opt-in, sandboxed and confined to one real folder", async t => {
  const defaults = claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS });
  assert.equal(defaults[defaults.indexOf("--tools") + 1], "", "no tools unless a project folder is configured");
  assert.ok(!defaults.includes("--settings"));

  const args = claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS, projectDir: "/work/project" });
  assert.equal(args[args.indexOf("--tools") + 1], "Read,Write,Edit,Glob,Grep,Bash");
  assert.ok(args.includes("--restricted"), "the owner's settings and CLAUDE.md stay out");
  assert.ok(args.includes("--strict-mcp-config"));
  assert.equal(args[args.indexOf("--permission-prompts") + 1], "none", "nobody is at the keyboard to answer");
  assert.ok(!args.some(arg => /skip-permissions|bypass/i.test(arg)));
  const { sandbox, permissions } = JSON.parse(args[args.indexOf("--settings") + 1]);
  assert.equal(sandbox.enabled, true);
  assert.equal(sandbox.failIfUnavailable, true, "must refuse to run rather than run unsandboxed");
  assert.equal(sandbox.allowUnsandboxedCommands, false, "must not retry outside the sandbox");
  assert.equal(sandbox.network.strictAllowlist, true);
  assert.deepEqual(sandbox.network.allowedDomains, []);
  assert.equal(permissions.blockReadsOutsideWorkingDirectories, true);

  const settings = projectDir => claudeCodeSettings({ upstream: { claudeCode: { projectDir } } });
  assert.equal(settings("/work/project/").projectDir, "/work/project");
  assert.throws(() => settings("relative/path"), /absolute/);
  assert.throws(() => settings("/"), /project folder/);
  assert.throws(() => settings(homedir()), /project folder/);
  assert.throws(() => settings(path.dirname(homedir())), /project folder/);
  assert.throws(() => validateClaudeCodeConfig({ upstream: { claudeCode: { projectDir: "/no/such/s1-adv-folder" } } }), /does not exist/);
});

test("web access is opt-in and adds lookups without opening the sandbox", () => {
  const tools = args => args[args.indexOf("--tools") + 1];
  assert.equal(tools(claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS })), "", "off by default");
  assert.ok(!tools(claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS, projectDir: "/work/project" })).includes("Web"));

  const chat = claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS, webAccess: true });
  assert.equal(tools(chat), "WebSearch,WebFetch", "web on its own adds no file or command tools");
  assert.ok(!chat.includes("--settings"));

  const project = claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS, projectDir: "/work/project", webAccess: true });
  assert.equal(tools(project), "Read,Write,Edit,Glob,Grep,Bash,WebSearch,WebFetch");
  assert.equal(project[project.indexOf("--allowedTools") + 1], "WebSearch,WebFetch", "only the web tools skip approval");
  const { sandbox } = JSON.parse(project[project.indexOf("--settings") + 1]);
  assert.deepEqual(sandbox.network.allowedDomains, [], "commands stay without a network");
  assert.equal(sandbox.network.strictAllowlist, true);
  for (const args of [chat, project]) assert.ok(args.includes("--restricted") && !args.some(arg => /skip-permissions|bypass/i.test(arg)));
});

test("the running bridge can't be changed from inside its own project", () => {
  const inside = projectSettings(path.resolve(bridgeRoot, ".."));
  assert.deepEqual(inside.sandbox.filesystem.denyWrite, [realpathSync(bridgeRoot)]);
  assert.deepEqual(inside.permissions.deny, [`Edit(/${realpathSync(bridgeRoot)}/**)`]);
  assert.equal(projectSettings(bridgeRoot).sandbox.filesystem.denyWrite[0], realpathSync(bridgeRoot), "the bridge folder itself counts");
  const elsewhere = projectSettings(tmpdir());
  assert.equal(elsewhere.sandbox.filesystem, undefined);
  assert.equal(elsewhere.permissions.deny, undefined);
  assert.equal(elsewhere.sandbox.failIfUnavailable, true);
});

test("publishing is opt-in, needs a project, and is never shown as text", async t => {
  assert.throws(() => validateClaudeCodeConfig({ upstream: { claudeCode: { firmwareUpdates: true } } }), /needs projectDir/);
  const marker = "[[publish_firmware]]";
  for (const [publishFirmware, expected] of [
    [async () => "Update 0.2.208 is ready.", "Built it.\n\nUpdate 0.2.208 is ready."],
    [async () => { throw new Error("the image isn't built for ESP32-S3"); }, "Built it.\n\nThe update wasn't published: the image isn't built for ESP32-S3."],
    [null, "Built it."]
  ]) {
    const fake = await makeFake(t, { steps: [success(`Built it.\n${marker}`)] });
    const { client, events } = makeClient(fake, {}, { publishFirmware });
    await client.start();
    await send(client, "send the update", "run-1");
    assert.equal((await finished(client, "run-1")).status, "ok");
    assert.equal(replyText(events[0]), expected);
  }
  const fake = await makeFake(t, { steps: [success("No update needed.")] });
  let published = 0;
  const { client } = makeClient(fake, {}, { publishFirmware: async () => { published += 1; return ""; } });
  await client.start();
  await send(client, "hello", "run-1");
  await finished(client, "run-1");
  assert.equal(published, 0, "only an explicit request publishes");
});

test("project turns run in the folder and start a new conversation", async t => {
  const fake = await makeFake(t, { steps: [success("chat"), success("project", NEXT_ID), success("project again", NEXT_ID)] });
  const chat = makeClient(fake);
  await chat.client.start();
  await send(chat.client, "hello", "run-1");
  await finished(chat.client, "run-1");

  const project = path.join(fake.dir, "project");
  await mkdir(project);
  const { client, events } = makeClient(fake, { projectDir: project });
  await client.start();
  await send(client, "what folder is this?", "run-2");
  await finished(client, "run-2");
  await send(client, "and again", "run-3");
  await finished(client, "run-3");
  const calls = await fake.calls();
  assert.equal(await realpath(calls[1].cwd), await realpath(project));
  assert.ok(!calls[1].argv.includes("--resume"), "a tool-free conversation must not carry over");
  assert.equal(calls[2].argv[calls[2].argv.indexOf("--resume") + 1], NEXT_ID);
  assert.equal(replyText(events.at(-1)), "project again");
});

test("a sandbox that can't start is reported and nothing runs unsandboxed", async t => {
  const fake = await makeFake(t, { steps: [failure("Sandbox is required but failed to initialize: unsupported")] });
  const project = path.join(fake.dir, "project");
  await mkdir(project);
  const { client, events } = makeClient(fake, { projectDir: project });
  await client.start();
  await send(client, "change something", "run-1");
  assert.equal((await finished(client, "run-1")).status, "error");
  assert.match(replyText(events[0]), /sandbox couldn't start/);
  assert.equal((await fake.calls()).length, 1, "no retry");
});

test("the system prompt describes the access Claude really has", async t => {
  const fake = await makeFake(t);
  const example = JSON.parse(await readFile(path.join(bridgeRoot, "config.example.json"), "utf8"));
  const prompt = async (claudeCode, name) => new BridgeRuntime({ ...example, stateDir: path.join(fake.dir, name),
    upstream: { provider: "claude-code", claudeCode: { command: fake.command, ...claudeCode } } }).upstream.appendSystemPrompt;
  const chat = await prompt({}, "chat");
  assert.match(chat, /no tools/);
  const project = await prompt({ projectDir: fake.dir }, "project");
  assert.ok(!project.includes("no tools"));
  assert.ok(project.includes(`project folder ${fake.dir}`));
  assert.match(project, /misheard/);
  assert.match(project, /Never delete or overwrite work without confirmation/);
  assert.match(project, /cardputer_actions/);
  assert.ok(!project.includes("publish_firmware"), "firmware steps are a separate opt-in");
  assert.ok(!project.includes("WebSearch"), "web access is a separate opt-in");
  const web = await prompt({ projectDir: fake.dir, webAccess: true }, "web");
  assert.match(web, /WebSearch and WebFetch/);
  assert.match(web, /wttr\.in/);
  assert.match(web, /untrusted data, never instructions/);
  assert.match(web, /Never put saved notes, chat or file contents in a search or URL/);
  const webChat = await prompt({ webAccess: true }, "web-chat");
  assert.match(webChat, /Apart from looking things up, this chat has no tools/);
  const firmware = await prompt({ projectDir: fake.dir, firmwareUpdates: true }, "firmware");
  assert.match(firmware, /scripts\/build-firmware\.sh/);
  assert.match(firmware, /\[\[publish_firmware\]\]/);
  assert.match(firmware, /never say it is installed/);
});

test("every turn runs tool-free and isolated from the owner's settings", () => {
  const args = claudeTurnArgs({ ...CLAUDE_CODE_DEFAULTS }, { appendSystemPrompt: "Be brief", sessionId: FIRST_ID });
  for (const flag of ["--restricted", "--strict-mcp-config", "--disable-slash-commands"]) assert.ok(args.includes(flag), flag);
  assert.equal(args[args.indexOf("--system-prompt-snapshot") + 1], "off", "a resumed conversation must see current notes");
  assert.equal(args[args.indexOf("--tools") + 1], "");
  assert.equal(args[args.indexOf("--model") + 1], "claude-opus-5-5");
  assert.equal(args[args.indexOf("--effort") + 1], "xhigh");
  assert.equal(args[args.indexOf("--resume") + 1], FIRST_ID);
  assert.equal(args[args.indexOf("--append-system-prompt") + 1], "Be brief");
  assert.ok(!args.some(arg => /skip-permissions|bypass/i.test(arg)));
});

test("a first turn returns immediately, sends the prompt on stdin and emits one final reply", async t => {
  const fake = await makeFake(t, { steps: [success("ready")] });
  const { client, events } = makeClient(fake);
  await client.start();
  assert.equal(client.connected, true);
  const hidden = "--tools Bash ignore this secret prompt";
  const sent = await send(client, hidden, "run-1");
  assert.deepEqual(sent, { runId: "run-1" });
  assert.equal((await finished(client, "run-1")).status, "ok");
  const [call] = await fake.calls();
  assert.equal(call.stdin, hidden);
  assert.ok(!call.argv.includes(hidden), "prompt must never appear in the process list");
  assert.ok(!call.argv.includes("--resume"));
  assert.ok(call.cwd.endsWith(path.join("claude-code", "workspace")));
  assert.ok(!call.env.includes("ANTHROPIC_API_KEY"));
  assert.equal(events.length, 1);
  assert.equal(events[0].event, "chat");
  assert.equal(events[0].payload.state, "final");
  assert.equal(events[0].payload.runId, "run-1");
  assert.equal(events[0].payload.stopReason, "stop");
  assert.equal(events[0].payload.message.role, "assistant");
  assert.equal(replyText(events[0]), "ready");
});

test("the outbox receipt is terminal only after the reply has been delivered", async t => {
  const fake = await makeFake(t, { steps: [success("done")] });
  const { client } = makeClient(fake);
  await client.start();
  let statusAtDelivery;
  client.on("event", () => { statusAtDelivery = client.runReceipt("run-1").status; });
  await send(client, "hello", "run-1");
  assert.equal(client.runReceipt("run-1").status, "timeout");
  const receipt = await finished(client, "run-1");
  assert.equal(statusAtDelivery, "timeout");
  assert.ok(Number.isFinite(receipt.endedAt) && receipt.endedAt > 0);
});

test("conversations resume across turns and bridge restarts, in private storage", async t => {
  const fake = await makeFake(t, { steps: [success("one"), success("two", NEXT_ID), success("three", NEXT_ID)] });
  const first = makeClient(fake);
  await first.client.start();
  await send(first.client, "first", "run-1");
  await finished(first.client, "run-1");
  await send(first.client, "second", "run-2");
  await finished(first.client, "run-2");
  const restarted = makeClient(fake);
  await restarted.client.start();
  await send(restarted.client, "third", "run-3");
  await finished(restarted.client, "run-3");
  const calls = await fake.calls();
  assert.ok(!calls[0].argv.includes("--resume"));
  assert.equal(calls[1].argv[calls[1].argv.indexOf("--resume") + 1], FIRST_ID);
  // Claude Code may issue a new id on resume; always follow the latest one.
  assert.equal(calls[2].argv[calls[2].argv.indexOf("--resume") + 1], NEXT_ID);
  const saved = path.join(fake.dir, "state", "claude-code", "sessions.json");
  assert.equal((await stat(saved)).mode & 0o077, 0);
  assert.deepEqual(JSON.parse(await readFile(saved, "utf8")).sessions, { "agent:claude:cardputer": NEXT_ID });
});

test("separate topics keep separate conversations", async t => {
  const fake = await makeFake(t, { steps: [success("a"), success("b", NEXT_ID), success("a2")] });
  const { client } = makeClient(fake);
  await client.start();
  await send(client, "a", "run-a", "agent:claude:one");
  await finished(client, "run-a");
  await send(client, "b", "run-b", "agent:claude:two");
  await finished(client, "run-b");
  await send(client, "a again", "run-a2", "agent:claude:one");
  await finished(client, "run-a2");
  const calls = await fake.calls();
  assert.ok(!calls[1].argv.includes("--resume"));
  assert.equal(calls[2].argv[calls[2].argv.indexOf("--resume") + 1], FIRST_ID);
});

test("an expired conversation restarts once instead of failing the turn", async t => {
  const fake = await makeFake(t, { steps: [success("one"),
    { code: 1, stderr: `No conversation found with session ID: ${FIRST_ID}` }, success("fresh", NEXT_ID)] });
  const { client, events } = makeClient(fake);
  await client.start();
  await send(client, "first", "run-1");
  await finished(client, "run-1");
  await send(client, "second", "run-2");
  assert.equal((await finished(client, "run-2")).status, "ok");
  assert.equal(replyText(events.at(-1)), "fresh");
  const calls = await fake.calls();
  assert.equal(calls.length, 3);
  assert.ok(calls[1].argv.includes("--resume"));
  assert.ok(!calls[2].argv.includes("--resume"));
  assert.equal(calls[2].stdin, "second");
});

test("a repeated idempotency key never reruns the turn, and overlap in one session is refused", async t => {
  const fake = await makeFake(t, { steps: [{ hang: true }] });
  const { client } = makeClient(fake, { turnTimeoutMs: 5000 });
  await client.start();
  await send(client, "first", "run-1");
  assert.deepEqual(await send(client, "first", "run-1"), { runId: "run-1" });
  await assert.rejects(send(client, "second", "run-2"), /already running/);
  await until(async () => (await fake.calls()).length === 1);
  await client.stop();
});

test("plan limits and sign-out reach the device as fixed text and end the run as an error", async t => {
  for (const [provider, expected] of [["You've hit your usage limit · resets 7pm", /plan limit/], ["Not logged in · Please run /login", /claude auth login/]]) {
    const fake = await makeFake(t, { steps: [failure(provider)] });
    const { client, events } = makeClient(fake);
    await client.start();
    await send(client, "hello", "run-1");
    assert.equal((await finished(client, "run-1")).status, "error");
    assert.match(replyText(events[0]), expected);
    assert.ok(!replyText(events[0]).includes(provider), "provider text is not forwarded");
    await client.stop();
  }
});

test("an unrecognised failure stays generic so the bridge reports it", async t => {
  const fake = await makeFake(t, { steps: [failure("something internal with /private/path"), { stdout: "not json" }, success("")] });
  const { client, events } = makeClient(fake);
  await client.start();
  for (const runId of ["run-1", "run-2", "run-3"]) {
    await send(client, "hello", runId);
    assert.equal((await finished(client, runId)).status, "error");
  }
  assert.equal(events.length, 0);
});

test("a hung run is killed at the deadline and its session lane is released", async t => {
  const fake = await makeFake(t, { steps: [{ hang: true }, success("after")] });
  const { client, events } = makeClient(fake, { turnTimeoutMs: 5000 });
  client.settings.turnTimeoutMs = 100;
  await client.start();
  await send(client, "hello", "run-1");
  assert.equal((await finished(client, "run-1")).status, "error");
  assert.match(replyText(events[0]), /too long/);
  await send(client, "again", "run-2");
  assert.equal((await finished(client, "run-2")).status, "ok");
});

test("an interrupted run cannot block its session after a restart", async t => {
  const fake = await makeFake(t);
  const { client } = makeClient(fake);
  const receipt = await client.rpc("agent.wait", { runId: "lost-before-restart" });
  assert.equal(receipt.status, "error");
  assert.ok(receipt.endedAt > 0);
});

test("a missing or non-subscription login refuses turns but keeps the bridge running", async t => {
  for (const auth of [{ loggedIn: false }, { loggedIn: true, apiProvider: "firstParty", authMethod: "api_key" }]) {
    const fake = await makeFake(t, { auth });
    const { client } = makeClient(fake);
    void client.start();
    await assert.rejects(send(client, "hello", "run-1"), /not signed in/);
    assert.equal(client.connected, false);
    assert.equal((await fake.calls()).length, 0);
    await client.stop();
  }
});

test("signing in later connects without a bridge restart", async t => {
  const fake = await makeFake(t, { auth: { loggedIn: false } });
  const { client } = makeClient(fake);
  void client.start();
  await client.firstCheck;
  assert.equal(client.connected, false);
  await writeFile(path.join(fake.dir, "auth.json"), JSON.stringify(MAX_AUTH));
  await until(() => client.connected);
  await client.stop();
});

test("an absent program is reported rather than crashing", async t => {
  const login = await readClaudeLogin({ command: path.join(tmpdir(), "no-such-claude-program") });
  assert.equal(login.installed, false);
  assert.equal(login.subscription, false);
  assert.deepEqual(parseClaudeResult({ code: null, stdout: "", stderr: "", spawnError: new Error("ENOENT") }), { category: "failed" });
});

test("only OpenClaw features outside this client are refused", async t => {
  const fake = await makeFake(t);
  const { client } = makeClient(fake);
  await client.start();
  await assert.rejects(client.rpc("s1-adv.front", {}), /does not support/);
  assert.deepEqual(await client.rpc("sessions.get", { key: "k" }), { key: "k" });
  assert.deepEqual(await client.rpc("sessions.create", { key: "k" }), { key: "k" });
  assert.deepEqual((await client.rpc("chat.history", { sessionKey: "k" })).messages, []);
  assert.deepEqual((await client.rpc("sessions.list", {})).sessions, []);
  await client.subscribeSessionMessages("k");
});

test("setup writes a Claude Code config by default and an OpenClaw config only when asked", async t => {
  const dir = await mkdtemp(path.join(tmpdir(), "s1-setup-"));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const stateDir = path.join(dir, "bridge");
  const plan = await planInstall({ stateDir, publicUrl: "https://bridge.example.com" });
  assert.equal(plan.summary.provider, "claude-code");
  assert.equal(plan.summary.model, "claude-opus-5-5");
  assert.equal(plan.summary.effort, "xhigh");
  assert.deepEqual(plan.summary.writes, ["config.json (0600)", "firmware.env (0600)"]);
  await applyInstall(plan);
  const config = JSON.parse(await readFile(plan.configPath, "utf8"));
  assert.equal(config.upstream.provider, "claude-code");
  assert.equal(config.omnibotShim.sttLocalLanguage, "en");
  assert.equal(config.upstream.url, undefined);
  assert.equal(config.upstream.token, undefined);
  assert.equal((await stat(plan.configPath)).mode & 0o077, 0);
  await assert.rejects(readFile(path.join(stateDir, "openclaw-plugin-config.json")), /ENOENT/);
  assert.match(await readFile(path.join(stateDir, "firmware.env"), "utf8"), /DEFAULT_HUB_HOST='bridge.example.com'/);

  process.env.CARDPUTER_BRIDGE_CONFIG = plan.configPath;
  t.after(() => { delete process.env.CARDPUTER_BRIDGE_CONFIG; });
  assert.equal((await loadConfig()).upstream.provider, "claude-code");

  await assert.rejects(enablePlugin(plan.configPath, { gatewayConfigPath: path.join(dir, "none.json") }), /standalone/);
  await assert.rejects(planModelRoles(plan.configPath, { gatewayConfig: plan.configPath }), /Model roles need the OpenClaw/);

  const gatewayConfig = path.join(dir, "openclaw.json");
  await writeFile(gatewayConfig, JSON.stringify({ gateway: { auth: { token: "gateway-token" } } }));
  const openclaw = await planInstall({ provider: "openclaw", gatewayConfig, stateDir: path.join(dir, "oc"), agent: "main" });
  assert.equal(openclaw.config.upstream.provider, "openclaw");
  assert.equal(openclaw.config.upstream.token, "gateway-token");
  await assert.rejects(planInstall({ provider: "other" }), /Provider must be/);
  // The documented OpenClaw command never silently becomes a Claude Code install.
  process.env.OPENCLAW_CONFIG_PATH = gatewayConfig;
  t.after(() => { delete process.env.OPENCLAW_CONFIG_PATH; });
  assert.equal((await planInstall({ agent: "main", stateDir: path.join(dir, "oc2") })).config.upstream.provider, "openclaw");
});

test("doctor checks the subscription login instead of a gateway", async t => {
  const dir = await mkdtemp(path.join(tmpdir(), "s1-doctor-"));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const plan = await planInstall({ stateDir: dir });
  await applyInstall(plan);
  const signedOut = await doctor(plan.configPath, { probe: false,
    claudeLogin: async () => ({ installed: true, subscription: false, detail: "not signed in" }) });
  assert.equal(signedOut.provider, "claude-code");
  const names = signedOut.checks.map(check => check.name);
  assert.ok(names.includes("claude-cli") && names.includes("claude-subscription"));
  assert.ok(!names.includes("gateway-token") && !names.includes("agent-config"));
  assert.equal(signedOut.checks.find(check => check.name === "claude-subscription").ok, false);
});

async function freePort() {
  return new Promise((resolve, reject) => {
    const probe = createServer();
    probe.on("error", reject);
    probe.listen(0, "127.0.0.1", () => {
      const { port } = probe.address();
      probe.close(() => resolve(port));
    });
  });
}

async function startBridge(t, fake, shim = {}, { reuse = false } = {}) {
  const example = JSON.parse(await readFile(path.join(bridgeRoot, "config.example.json"), "utf8"));
  const dir = path.join(fake.dir, "bridge");
  const token = "test-claude-code-device-token-000000000000000001";
  const port = await freePort();
  if (reuse) await fake.stopBridge();
  const config = { ...example, server: { host: "127.0.0.1", port }, stateDir: path.join(dir, "data") };
  config.upstream = { provider: "claude-code", claudeCode: { command: fake.command } };
  config.omnibotShim = { ...example.omnibotShim, ...shim };
  config.media = Object.fromEntries(Object.keys(example.media).map(key => [key, path.join(dir, key)]));
  config.assets = { ...example.assets, rootDir: path.join(dir, "assets"), logsDir: path.join(dir, "logs") };
  config.firmware = { ...example.firmware, rootDir: path.join(dir, "firmware") };
  config.devices = [{ ...example.devices[0], device_token: token }];
  config.topics = [{ ...example.topics[0], session_key: "agent:claude:cardputer" }];
  const configPath = path.join(fake.dir, "config.json");
  await writeFile(configPath, JSON.stringify(config));
  const child = spawn(process.execPath, ["src/server.mjs"], { cwd: bridgeRoot,
    env: { ...process.env, ANTHROPIC_API_KEY: "must-not-reach-claude", CARDPUTER_BRIDGE_CONFIG: configPath }, stdio: "ignore" });
  fake.stopBridge = () => new Promise(resolve => { child.once("exit", resolve); child.kill("SIGTERM"); });
  t.after(() => child.kill("SIGKILL"));
  const base = `http://127.0.0.1:${port}`;
  const headers = { Authorization: `Bearer ${token}`, "content-type": "application/json" };
  const health = await until(async () => {
    const body = await fetch(`${base}/healthz`, { headers }).then(response => response.json()).catch(() => null);
    return body?.upstream_connected ? body : null;
  }, 15000);
  assert.equal(health.upstream_connected, true);
  const turn = message => fetch(`${base}/api/device-text-turn`, { method: "POST", headers,
    body: JSON.stringify({ device_id: config.devices[0].device_id, message }) }).then(response => response.json());
  // The same request the firmware makes, including its 1 s wait.
  const voice = (turnId, seconds = 1, extra = {}) => fetch(`${base}/api/device-audio-turn-raw`, { method: "POST",
    headers: { Authorization: headers.Authorization, "content-type": "application/octet-stream", "x-audio-sample-rate": "16000",
      "x-turn-id": turnId, "x-client-msg-id": turnId, "x-reply-audio": "false", "x-save-reply-audio": "false", "x-voice-wait-ms": "1000", ...extra },
    body: Buffer.alloc(32000 * seconds) }).then(response => response.json());
  const waiting = () => fetch(`${base}/api/cardputer/device-events/pending?limit=10`, { headers }).then(response => response.json());
  return { turn, voice, waiting, sessionsPath: path.join(config.stateDir, "claude-code", "sessions.json") };
}

test("full bridge: device text turns reach Claude Code and replies, context and actions return", async t => {
  const marker = "[[cardputer_actions:[{\"type\":\"focus.start\",\"minutes\":25}]]]";
  const fake = await makeFake(t, { steps: [success("Hello from Claude"), success(`Focus started. ${marker}`), failure("usage limit reached")] });
  const { turn } = await startBridge(t, fake);

  const first = await turn("say hello");
  assert.equal(first.status, "success");
  assert.equal(first.reply, "Hello from Claude");

  const second = await turn("start a focus timer");
  assert.equal(second.reply, "Focus started.");
  assert.equal(second.device_actions[0].type, "focus.start");
  assert.equal(second.device_actions[0].minutes, 25);

  const limited = await turn("one more");
  assert.match(limited.reply, /plan limit/);

  const calls = await fake.calls();
  assert.equal(calls.length, 3);
  // The bare message goes through: no OpenClaw policy, tools or topic envelope.
  assert.equal(calls[0].stdin, "say hello");
  assert.ok(!calls.some(call => /cardputer_status|Cardputer runtime context/.test(call.stdin)));
  assert.ok(!calls[0].argv.includes("--resume"));
  assert.equal(calls[1].argv[calls[1].argv.indexOf("--resume") + 1], FIRST_ID);
  assert.ok(!calls.some(call => call.env.includes("ANTHROPIC_API_KEY")));
  const prompt = calls[0].argv[calls[0].argv.indexOf("--append-system-prompt") + 1];
  assert.match(prompt, /no tools/);
  assert.match(prompt, /cardputer_actions/);
  assert.ok(!/cardputer_status|OpenClaw/.test(prompt));
});

test("full bridge: stopping the bridge stops a running turn instead of orphaning it", async t => {
  const fake = await makeFake(t, { steps: [{ hang: true }] });
  const { turn } = await startBridge(t, fake);
  void turn("work for a long time").catch(() => {});
  const [call] = await until(async () => {
    const calls = await fake.calls();
    return calls.length ? calls : null;
  }, 10000);
  assert.doesNotThrow(() => process.kill(call.pid, 0), "the turn should be running");
  await fake.stopBridge();
  await until(() => {
    try { process.kill(call.pid, 0); return false; } catch { return true; }
  }, 8000);
});

test("full bridge: a voice reply slower than the firmware's 1 s wait still comes back at once", async t => {
  const fake = await makeFake(t, { steps: [{ ...success("Paris is the capital."), delayMs: 1500 }] });
  const whisper = path.join(fake.dir, "whisper-cli");
  await writeFile(whisper, `#!${process.execPath}
const args = process.argv.slice(2);
require("fs").writeFileSync(args[args.indexOf("-of") + 1] + ".txt", "What is the capital of France?\\n");
`);
  await chmod(whisper, 0o755);
  const { voice } = await startBridge(t, fake, { sttLocalBin: whisper, sttLocalModel: path.join(fake.dir, "model.bin") });
  const reply = await voice("voice-test-1");
  assert.equal(reply.pending, undefined, "a pending reply waits for the device's 4 s inbox poll");
  assert.equal(reply.transcript, "What is the capital of France?");
  assert.equal(reply.reply, "Paris is the capital.");
  const [call] = await fake.calls();
  assert.equal(call.stdin, "[Voice]\nWhat is the capital of France?");
});

async function hearing(fake, words) {
  const whisper = path.join(fake.dir, "whisper-cli");
  await writeFile(whisper, `#!${process.execPath}
const args = process.argv.slice(2);
require("fs").writeFileSync(args[args.indexOf("-of") + 1] + ".txt", ${JSON.stringify(`${words}\n`)});
`);
  await chmod(whisper, 0o755);
  return { sttLocalBin: whisper, sttLocalModel: path.join(fake.dir, "model.bin") };
}

test("a quick question runs beside a chat turn and shares nothing with it", async t => {
  const fake = await makeFake(t, { steps: [{ ...success("Chat reply"), delayMs: 600 }, success("Side answer", NEXT_ID)] });
  const { client, events } = makeClient(fake);
  await client.start();
  await send(client, "a long job", "run-1");
  await until(async () => (await fake.calls()).length === 1);
  const started = Date.now();
  assert.equal(await client.ask({ system: "The rules.", message: "spoken words" }), "Side answer");
  assert.ok(Date.now() - started < 500, "it must not wait for the chat");
  assert.equal(events.length, 0, "the answer isn't a chat reply");
  await finished(client, "run-1");
  assert.equal(events.length, 1);
  assert.equal(replyText(events[0]), "Chat reply");

  const [, side] = await fake.calls();
  assert.equal(side.stdin, "spoken words");
  assert.ok(!side.argv.includes("spoken words"), "what was said stays out of the process list");
  assert.equal(side.argv[side.argv.indexOf("--system-prompt") + 1], "The rules.");
  assert.equal(path.basename(side.cwd), "ask");
  assert.ok(!side.env.includes("ANTHROPIC_API_KEY"));
  assert.ok(![...client.sessions.values()].includes(NEXT_ID), "nothing to resume");
});

test("a project folder stays out of a quick question", async t => {
  const fake = await makeFake(t, { steps: [success("Side answer")] });
  const project = path.join(fake.dir, "project");
  await mkdir(project);
  const { client } = makeClient(fake, { projectDir: project });
  await client.start();
  await client.ask({ system: "The rules.", message: "words" });
  const [call] = await fake.calls();
  assert.notEqual(realpathSync(call.cwd), realpathSync(project));
  assert.equal(call.argv[call.argv.indexOf("--tools") + 1], "");
});

test("a quick question that fails says why, and signing out stops it running at all", async t => {
  const fake = await makeFake(t, { steps: [failure("You've hit your usage limit"), failure("not logged in")] });
  const { client } = makeClient(fake);
  await client.start();
  await assert.rejects(client.ask({ system: "s", message: "m" }), /plan limit/);
  await assert.rejects(client.ask({ system: "s", message: "m" }), /not signed in/);
  assert.equal(client.connected, false, "the login is checked again");
  const signedOut = await makeFake(t, { auth: { loggedIn: false } });
  const { client: other } = makeClient(signedOut);
  void other.start();  // It keeps checking until someone signs in.
  t.after(() => other.stop());
  await assert.rejects(other.ask({ system: "s", message: "m" }), /not signed in/);
  assert.equal((await signedOut.calls()).length, 0);
});

test("full bridge: transcribing gives back the words and nothing else happens", async t => {
  const fake = await makeFake(t, { steps: [success("must not be used")] });
  const { voice, waiting, sessionsPath } = await startBridge(t, fake, await hearing(fake, "Buy more milk"));
  const answer = await voice("voice-tool-1", 1, { "x-voice-tool": "transcribe" });
  assert.deepEqual(answer, { ok: true, voice_tool: "transcribe", transcript: "Buy more milk" });
  assert.equal((await fake.calls()).length, 0);
  assert.deepEqual((await waiting()).items, []);
  await assert.rejects(stat(sessionsPath), { code: "ENOENT" });
});

test("full bridge: a tool asks Claude on its own and stays out of the chat", async t => {
  const rule = { type: "lab.rule", name: "Wave red", any: true, hold_ms: 2000,
    when: [{ sensor: "distance", test: "under", value: 30 }], do: [{ output: "strip_color", set: 0 }] };
  const steps = [success(`Here it is. [[cardputer_actions:${JSON.stringify([rule, { type: "ota.apply" }])}]]`), success("Hello from the chat")];
  const fake = await makeFake(t, { steps });
  const { voice, turn, waiting, sessionsPath } = await startBridge(t, fake, await hearing(fake, "When I wave turn the strip red"));
  const answer = await voice("voice-tool-2", 1, { "x-voice-tool": "lab.rule" });
  assert.equal(answer.ok, true);
  assert.equal(answer.pending, undefined, "the device waits for this on the same request");
  assert.equal(answer.voice_tool, "lab.rule");
  assert.equal(answer.transcript, "When I wave turn the strip red");
  assert.equal(answer.reply, "Here it is.");
  assert.deepEqual(answer.device_actions.map(action => action.type), ["lab.rule"], "only what the tool lists");
  assert.equal(answer.device_actions[0].do[0].output, "strip_color");

  const [call] = await fake.calls();
  assert.equal(call.stdin, "When I wave turn the strip red");
  assert.ok(call.argv.includes("--no-session-persistence"));
  assert.match(call.argv[call.argv.indexOf("--system-prompt") + 1], /lab\.rule/);
  assert.deepEqual((await waiting()).items, []);
  await assert.rejects(stat(sessionsPath), { code: "ENOENT" });

  // The chat carries on as if it never happened.
  assert.equal((await turn("hello")).reply, "Hello from the chat");
  const [, chat] = await fake.calls();
  assert.equal(chat.stdin, "hello");
  assert.ok(!chat.argv.includes("--resume"));
});

test("full bridge: a tool's problems come back as errors the device can show", async t => {
  const fake = await makeFake(t, { steps: [failure("You've hit your usage limit")] });
  const { voice } = await startBridge(t, fake, await hearing(fake, "Wave"));
  assert.match((await voice("voice-tool-3", 1, { "x-voice-tool": "lab.rule" })).error, /plan limit/);
  assert.equal((await voice("voice-tool-4", 1, { "x-voice-tool": "delete.everything" })).error, "unknown_voice_tool");
  assert.equal((await fake.calls()).length, 1);
});

test("full bridge: a tool hears nothing from a silent clip and Claude isn't asked", async t => {
  const fake = await makeFake(t, { steps: [success("must not be used")] });
  const { voice } = await startBridge(t, fake, await hearing(fake, "[BLANK_AUDIO]"));
  assert.equal((await voice("voice-tool-5", 1, { "x-voice-tool": "lab.rule" })).error, "could not transcribe speech");
  assert.equal((await fake.calls()).length, 0);
});

test("Whisper's non-speech tags are never treated as the user's words", () => {
  for (const silent of ["[BLANK_AUDIO]", " [BLANK_AUDIO] ", "[ Silence ]", "[MUSIC]", "[BLANK_AUDIO]\n[BLANK_AUDIO]", "(wind blowing)"])
    assert.equal(cleanupTranscriptText(silent), "", silent);
  assert.equal(cleanupTranscriptText("[MUSIC] hello there"), "hello there");
  assert.equal(cleanupTranscriptText("What is 2 (two) plus 2?"), "What is 2 (two) plus 2?");
  assert.equal(cleanupTranscriptText("Thank you very much."), "Thank you very much.");
});

test("full bridge: a silent clip stops at the bridge and never reaches Claude", async t => {
  const fake = await makeFake(t, { steps: [success("must not be used")] });
  const whisper = path.join(fake.dir, "whisper-cli");
  await writeFile(whisper, `#!${process.execPath}
const args = process.argv.slice(2);
require("fs").writeFileSync(args[args.indexOf("-of") + 1] + ".txt", " [BLANK_AUDIO]\\n");
`);
  await chmod(whisper, 0o755);
  const { voice } = await startBridge(t, fake, { sttLocalBin: whisper, sttLocalModel: path.join(fake.dir, "model.bin") });
  const reply = await voice("voice-silent-1");
  assert.equal(reply.error, "could not transcribe speech");
  assert.equal((await fake.calls()).length, 0);
});

const systemPrompt = call => call.argv[call.argv.indexOf("--append-system-prompt") + 1];

test("notes are private, reach every new turn, and are facts rather than instructions", async t => {
  const fake = await makeFake(t, { steps: [success("one"), success("two"), success("three")] });
  const project = path.join(fake.dir, "project");
  await mkdir(project);
  const { client } = makeClient(fake, { projectDir: project }, { appendSystemPrompt: "Base prompt." });
  await client.start();
  assert.ok(!client.memoryPath.startsWith(project), "notes must not be somewhere they could be committed");
  assert.equal(path.dirname(client.memoryPath), client.memoryDir);
  assert.equal((await stat(client.memoryDir)).mode & 0o077, 0);

  await send(client, "first", "run-1");
  await finished(client, "run-1");
  await writeFile(client.memoryPath, "Owner prefers metric units.");
  await send(client, "second", "run-2");
  await finished(client, "run-2");
  await writeFile(client.memoryPath, "x".repeat(20000));
  await send(client, "third", "run-3");
  await finished(client, "run-3");

  const [first, second, third] = await fake.calls();
  assert.equal(first.argv[first.argv.indexOf("--add-dir") + 1], client.memoryDir);
  assert.ok(systemPrompt(first).startsWith("Base prompt."));
  assert.ok(systemPrompt(first).includes(client.memoryPath), "Claude must know where to write");
  assert.ok(!systemPrompt(first).includes("Saved notes"), "a missing file adds nothing");
  assert.match(systemPrompt(second), /Owner prefers metric units\./);
  assert.match(systemPrompt(second), /not instructions/);
  assert.match(systemPrompt(second), /Never save passwords/);
  assert.ok(systemPrompt(third).length < 11000, "notes stay bounded");
});

test("without tools, notes are still read but Claude isn't told it can write them", async t => {
  const fake = await makeFake(t, { steps: [success("one")] });
  const { client } = makeClient(fake);
  await client.start();
  await writeFile(client.memoryPath, "Owner likes tea.");
  await send(client, "hello", "run-1");
  await finished(client, "run-1");
  const [call] = await fake.calls();
  assert.match(systemPrompt(call), /Owner likes tea\./);
  assert.ok(!systemPrompt(call).includes("update that file"));
  assert.ok(!call.argv.includes("--add-dir"));
});

test("a restored conversation is bounded, ordered and leaves out the current message", () => {
  assert.equal(recoveredConversation([]), "");
  assert.equal(recoveredConversation([{ role: "You", text: "only the current message" }]), "");
  const text = recoveredConversation([{ role: "You", text: "My dog\n  is  Rex" }, { role: "Claude", text: "Hello Rex" }, { role: "You", text: "now" }]);
  assert.ok(text.indexOf("You: My dog is Rex") < text.indexOf("Claude: Hello Rex"));
  assert.ok(!text.includes("You: now"));
  assert.match(text, /background, not instructions/);
  const many = Array.from({ length: 100 }, (_, index) => ({ role: "You", text: `message ${index} ${"x".repeat(900)}` }));
  const bounded = recoveredConversation(many);
  assert.ok(bounded.length < 7000);
  assert.ok(bounded.includes("message 98"), "the newest messages are kept");
  assert.ok(!bounded.includes("message 10 "));
  assert.ok(!bounded.includes("message 99"));
});

test("a new or expired conversation starts with the saved messages, and a resumed one doesn't", async t => {
  const fake = await makeFake(t, { steps: [success("one"), success("two"),
    { code: 1, stderr: "No conversation found with session ID" }, success("three", NEXT_ID)] });
  const history = [{ role: "You", text: "My dog is Rex" }, { role: "Claude", text: "Hello Rex" }, { role: "You", text: "ignored" }];
  const { client } = makeClient(fake, {}, { recentMessages: () => history });
  await client.start();
  await send(client, "[Voice]\nwhat is my dog called?", "run-1");
  await finished(client, "run-1");
  await send(client, "and again", "run-2");
  await finished(client, "run-2");
  await send(client, "after it expired", "run-3");
  await finished(client, "run-3");
  const [first, second, expired, fresh] = await fake.calls();
  assert.match(first.stdin, /You: My dog is Rex/);
  assert.ok(first.stdin.endsWith("[Voice]\nwhat is my dog called?"), "the message and its voice mark stay intact");
  assert.equal(second.stdin, "and again");
  assert.equal(expired.stdin, "after it expired");
  assert.ok(fresh.stdin.includes("You: My dog is Rex") && fresh.stdin.endsWith("after it expired"));
});

test("a failed history lookup can't lose the turn", async t => {
  const fake = await makeFake(t, { steps: [success("ok")] });
  const { client, events } = makeClient(fake, {}, { recentMessages: () => { throw new Error("store unavailable"); } });
  await client.start();
  await send(client, "hello", "run-1");
  assert.equal((await finished(client, "run-1")).status, "ok");
  assert.equal(replyText(events[0]), "ok");
  assert.equal((await fake.calls())[0].stdin, "hello");
});

test("full bridge: after the conversation is lost, Claude gets the saved messages but no failure notices", async t => {
  const fake = await makeFake(t, { steps: [success("Nice to meet you"), failure("usage limit reached"), failure("something internal"), success("Your name is Sam", NEXT_ID)] });
  const { turn, sessionsPath } = await startBridge(t, fake);
  await turn("my name is Sam");
  await turn("second message");
  await turn("third message");
  await rm(sessionsPath, { force: true });
  await writeFile(sessionsPath, "{}");
  const restarted = await startBridge(t, fake, {}, { reuse: true });
  const reply = await restarted.turn("what is my name?");
  assert.equal(reply.reply, "Your name is Sam");
  const last = (await fake.calls()).at(-1);
  assert.ok(!last.argv.includes("--resume"));
  assert.match(last.stdin, /You: my name is Sam/);
  assert.match(last.stdin, /Claude: Nice to meet you/);
  assert.ok(!last.stdin.includes("The request failed on the server"));
  assert.ok(last.stdin.endsWith("what is my name?"));
});

test("voice turns are labelled for short spoken replies, and OpenClaw keeps its envelope", async t => {
  const fake = await makeFake(t);
  const example = JSON.parse(await readFile(path.join(bridgeRoot, "config.example.json"), "utf8"));
  const device = { device_id: "cardputer-adv-01" };
  const base = { ...example, stateDir: path.join(fake.dir, "runtime") };
  const claude = new BridgeRuntime({ ...base, upstream: { provider: "claude-code", claudeCode: { command: fake.command } } });
  assert.equal(claude.assistant.name, "Claude");
  assert.equal(claude.buildDeviceTurnUpstreamText({ device, topicKey: "cardputer:main", text: "hello", inputMode: "voice" }), "[Voice]\nhello");
  assert.equal(claude.buildDeviceTurnUpstreamText({ device, topicKey: "cardputer:main", text: "hello", inputMode: "text" }), "hello");
  const normalized = claude.normalizeGatewayMessage({ topic_key: "cardputer:main" },
    { role: "assistant", content: [{ type: "text", text: "hi" }] }, { runId: "run-1" });
  assert.equal(normalized.sender.display_name, "Claude");
  assert.equal(normalized.sender.id, "claude-code:main");

  const openclaw = new BridgeRuntime({ ...base, stateDir: path.join(fake.dir, "runtime-oc"),
    upstream: { ...example.upstream, provider: "openclaw", deviceIdentityPath: path.join(fake.dir, "identity.json") } });
  assert.equal(openclaw.assistant.name, "OpenClaw");
  assert.equal(openclaw.upstream instanceof ClaudeCodeClient, false);
});
