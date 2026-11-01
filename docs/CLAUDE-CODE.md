# Use your Claude subscription

Claude Code is the bridge's default provider. Each Cardputer message runs through
the `claude` program on your computer, under your own subscription login. You
don't need OpenClaw or an API key. To use OpenClaw instead, see
[Switch to OpenClaw](#switch-to-openclaw).

```text
Cardputer ADV  <--- HTTPS/WSS --->  Bridge  --->  claude -p  --->  Claude
keyboard / mic / speaker            Node.js       your login
```

The bridge starts `claude` for one message at a time and resumes the same
conversation on the next one. It never reads, stores or forwards your Claude
credentials.

## What you need

- [Claude Code](https://code.claude.com/docs) installed and signed in with a
  Claude subscription. Check with `claude auth status`.
- Node.js 22+ and FFmpeg on the computer that runs the bridge.
- A Cardputer ADV with S1-ADV firmware and 2.4 GHz Wi-Fi.
- A public HTTPS address for the bridge. The firmware only connects over TLS and
  trusts ISRG Root X1 by default.

That computer has to stay on and awake while you use the Cardputer.

## Set it up

```bash
cd bridge
npm ci
node scripts/setup.mjs init --public-url https://YOUR_BRIDGE --apply
node scripts/setup.mjs doctor --config ~/.s1-adv/config.json --no-probe
```

Include the port in `YOUR_BRIDGE` if it isn't 443, for example
`https://bridge.example.com:10000`. Setup creates `~/.s1-adv/` with `config.json`
and `firmware.env`. Both are private and hold your device token, so never commit
or share them. Then start the bridge:

```bash
CARDPUTER_BRIDGE_CONFIG=~/.s1-adv/config.json npm start
```

To keep it running after you close the terminal, use this instead. It stops when
the computer restarts:

```bash
CARDPUTER_BRIDGE_CONFIG=~/.s1-adv/config.json nohup npm start >> ~/.s1-adv/bridge.log 2>&1 &
```

The bridge listens on `127.0.0.1:31889`, so put an HTTPS proxy in front of it.

### Tailscale Funnel

If you use Tailscale, Funnel is the shortest route, because the Cardputer can't
join your tailnet and `tailscale serve` won't reach it. Funnel only works on ports
443, 8443 and 10000, and it replaces whatever already uses the port. Check first:

```bash
tailscale serve status
```

If 443 is free, run `tailscale funnel --bg 31889`. If it's in use, use a free port:

```bash
tailscale funnel --bg --https=10000 31889
```

Your bridge URL is then `https://YOUR-MACHINE.YOUR-TAILNET.ts.net:10000`. Funnel
can take a few minutes to become reachable after you first enable it. It puts the
token-protected bridge on the public internet. Physical testing of a Funnel
certificate on the device is still outstanding, so check TLS first if the Cardputer
can't connect.

### Flash the Cardputer

Plug in the Cardputer and run this from the repository root:

```bash
./scripts/flash-cardputer.sh
```

It finds your bridge URL and copies your device token to the clipboard. Then:

1. Unplug the Cardputer and plug it back in, so it starts the new firmware.
2. Join the setup Wi-Fi shown on its screen, and open `http://192.168.4.1/`.
3. Enter your Wi-Fi, bridge URL and device token, then save.
4. Send `Reply with: ready`. Seeing that answer on the device is the real test.

See [Build and flash](FLASHING.md) for details.

## Add voice

Push-to-talk needs a speech-to-text and a text-to-speech program on the bridge
computer. Text chat works without them. On macOS:

```bash
brew install whisper-cpp espeak-ng ffmpeg
cd bridge
node scripts/setup.mjs voice --config ~/.s1-adv/config.json
node scripts/setup.mjs voice --config ~/.s1-adv/config.json --apply
```

The first run previews what will change. `--apply` downloads the Whisper speech
model and a natural Kokoro voice, about 260 MiB in total, and checks both against
pinned checksums. It installs Kokoro in a private Python environment, confirms it can
speak, then saves the paths in your private config. Restart the bridge afterwards.

The natural voice needs Python 3.10 to 3.13. Without one, spoken replies use eSpeak,
which is robotic but works. On Linux, install `espeak-ng`, `ffmpeg` and whisper.cpp's
`whisper-cli`, then run the same commands.

To change the voice, add `--voice am_michael` for example and restart the bridge.
Kokoro's English voices start with `af_`, `am_`, `bf_` and `bm_`. Russian replies
stay with eSpeak, which is also the fallback if Kokoro fails.

Your audio stays on your computer. Only the transcribed text goes to Claude.

Setup transcribes English, because language detection is unreliable on short clips.
To use another language, set `omnibotShim.sttLocalLanguage` to its code or `auto`.

Firmware before 0.2.207 can lose the first 2 to 3 seconds of a recording, because it
opens its secure connection as recording starts. On those versions, hold G0, wait about
3 seconds before you speak, then keep holding for a moment after you finish.

Spoken replies are limited to about 20 words unless you ask for more, because the
Cardputer downloads the whole reply before it plays anything. Claude usually answers
in 1 to 2 seconds, and the bridge holds the request until then, so replies don't wait
for the device's 4-second inbox check.

## Settings

These live under `upstream.claudeCode` in `config.json`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `command` | `claude` | Program to run. Use a full path if the bridge's `PATH` can't find it |
| `model` | `claude-opus-5-5` | Claude Code model name or alias |
| `effort` | `xhigh` | `low`, `medium`, `high`, `xhigh` or `max` |
| `turnTimeoutMs` | `600000` | Longest a single reply can run before it is stopped |

Restart the bridge after changing them. Lower effort gives faster replies and uses
less of your plan.

A reply that takes more than 60 seconds isn't lost. The device gets a pending
response, and the answer arrives on the live feed.

## What this mode can do

Chat, push-to-talk, conversation history, spoken replies, device actions such as
starting a focus timer, and Wi-Fi updates all work.

By default the session has **no tools**. Claude can't read files, run commands or
browse the web from the Cardputer, and it has no access to your projects. To change
that for one folder, see [Let Claude work in a folder](#let-claude-work-in-a-folder).
These OpenClaw features aren't available in this mode, and the bridge refuses to start
if they are enabled:

- `modelRouting` (fast and smart model roles)
- `firmwareAutomation` (agent-built firmware)

Voice needs local speech programs. See [Add voice](#add-voice). `doctor` reports
what's missing.

## How Claude remembers

Claude remembers in three layers, so a restart or an expired chat doesn't start you from
nothing:

| Layer | What it keeps | Lost when |
| --- | --- | --- |
| The conversation | Everything, resumed on every message | Claude Code clears old transcripts, or `projectDir` changes |
| Saved messages | The latest 30 messages are passed to any new conversation | The bridge's history is deleted. It keeps the latest 250 |
| Permanent notes | Facts you ask it to remember | You delete `data/claude-code/memory/MEMORY.md` |

Restarting the Cardputer, the bridge or the computer loses nothing.

To save something permanently, say "remember that I prefer metric units", and "forget that"
to remove it. Claude also saves lasting facts it notices, and it's told never to save
passwords or tokens. It decides what to write, and voice can be misheard, so you can check
or edit the file yourself:

```bash
cat ~/.s1-adv/data/claude-code/memory/MEMORY.md
```

Notes live in the bridge's private folder, not your project, so they can't be committed or
pushed. Claude gets access to that one folder and nothing else in private state. Notes are
loaded into every message as facts, not instructions, up to 8,000 characters.

Claude can only change notes when [`projectDir`](#let-claude-work-in-a-folder) is set.
Without it, existing notes are still read.

## Let Claude work in a folder

To let the Cardputer change a project, set `upstream.claudeCode.projectDir` in
`config.json` to that folder's full path, then restart the bridge:

```json
"claudeCode": { "projectDir": "/full/path/to/your/project" }
```

Claude then starts in that folder and can read and edit files, run commands and make
git commits there, with no approval prompts. It must be a project folder: the bridge
refuses your home folder or a whole disk. Conversations restart when you turn this on.

The limits are enforced by the operating system's sandbox, not by asking Claude:

| | Inside the folder | Outside it |
| --- | --- | --- |
| Read and edit files | Yes | No |
| Run commands | Yes | Commands can't read or change it |
| Network | No | No |

So `git push`, package installs and firmware builds don't work from the Cardputer. If
the sandbox can't start, nothing runs.

**Know the risk.** Your bridge is on the internet, so your device token now protects a
folder as well as a chat. Anyone with the token can change or delete anything in that
folder, including `.git`, and text in the project's own files can try to steer Claude.
Speech can also be misheard, so Claude is told to confirm voice requests before changing
files. Use a folder you can restore, keep your work pushed, and don't put secrets in it.
If the token leaks, replace it and turn this off first.

## Privacy and isolation

Your device token is the only thing between the internet and this bridge. The
session is locked down so that token reaches as little as possible:

- `--restricted`, `--tools ""`, `--strict-mcp-config` and `--disable-slash-commands`
  keep out your tools, MCP servers, plugins, hooks, memory and global `CLAUDE.md`.
  A tools-off run without `--restricted` still loads your `CLAUDE.md` and MCP
  servers, so don't remove these flags.
- The message goes through standard input, so it never appears in the process list.
- The child process gets a short list of environment variables. `ANTHROPIC_API_KEY`
  and `ANTHROPIC_AUTH_TOKEN` are removed, so a key on the host can't silently bill
  your API account, and other host secrets stay out.
- Without `projectDir`, each conversation runs in its own empty folder,
  `data/claude-code/workspace`. With it, these isolation flags still apply, and tools
  are limited as described in [Let Claude work in a folder](#let-claude-work-in-a-folder).
- If `claude auth status` doesn't show a subscription login, the bridge refuses
  turns instead of falling back to anything else.

If a token leaks, someone could chat in that conversation, read it and use up your
plan limits, and change your project folder if you've set one. Generate a new token and
reflash or reprovision the device.

Claude Code keeps its usual transcripts for these conversations in
`~/.claude/projects`. The bridge keeps only the conversation ID.

## Terms and plan limits

Replies count against the same plan limits as the rest of your Claude Code use.

Anthropic's [legal and compliance terms](https://code.claude.com/docs/en/legal-and-compliance)
say subscription login supports ordinary use of Claude Code. They don't allow
developers to route requests through subscription credentials for their users, or
to handle Claude.ai credentials. They don't explicitly cover a personal bridge like
this one, so read them yourself. This mode is designed for one person using the
unmodified `claude` program, on their own computer, with their own login. **Don't host
it for other people.** Anyone else should run their own bridge with their own login,
or use an API key through OpenClaw.

## Remote Control

Remote Control can't carry Cardputer traffic: only claude.ai/code and the Claude
apps can connect to it. You can still run `claude remote-control` on the same
computer for your phone, but that's a separate conversation from the Cardputer's.

## If something goes wrong

| On the device | Cause | Fix |
| --- | --- | --- |
| `Claude Code is not signed in...` | Login expired or missing | Run `claude auth login` on the bridge computer. The bridge reconnects by itself |
| `Your Claude plan limit has been reached...` | Plan limit | Wait for the reset |
| `Claude took too long...` | Hit `turnTimeoutMs` | Lower `effort`, or raise the timeout |
| `The request failed on the server...` | Other failure, or the bridge restarted mid-reply | Check the bridge log, then send it again |

Claude Code plans to make `--bare` the default for `claude -p`, and bare mode doesn't
read subscription logins. If turns start reporting that you aren't signed in right
after a Claude Code update while `claude auth status` still shows your login, this
is the likely cause. Check that update's release notes.

Old conversations expire with Claude Code's transcript cleanup. The bridge then
starts a new conversation by itself, so Claude won't remember the earlier one.

## Switch to OpenClaw

OpenClaw stays fully supported. Follow the [OpenClaw guide](OPENCLAW.md). Its setup
writes `"provider": "openclaw"` for you. To switch an existing config by hand, set
these and restart:

```json
"upstream": {
  "provider": "openclaw",
  "url": "ws://127.0.0.1:18789",
  "token": "YOUR_GATEWAY_TOKEN"
}
```

A config with an `upstream.url` and no `provider` keeps using OpenClaw, so existing
installations don't change.
