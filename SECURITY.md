# S1-ADV security

Use one device identity and a random bridge token per bridge instance. Independent
devices need separate private storage and bridge instances. Gateway credentials and its persistent device identity stay on the
bridge host. Keep the gateway on loopback; remote devices connect to the bridge
through verified HTTPS/WSS.

Private firmware builds can embed Wi-Fi passwords and device tokens. Never
attach provisioned binaries, `.env`, `bridge/config.json`, `data/`, recordings or
conversation logs to public issues. Rotate any credential that was disclosed.

By default, model calls run through the owner's `claude` program with its own
subscription login, no tools and no access to the owner's settings; the bridge holds
no Claude credentials. See [Claude Code](docs/CLAUDE-CODE.md). The OpenClaw setup uses
the OpenClaw gateway for model calls. Optional direct
provider routes are disabled by default. Do not copy rotating OAuth stores into
competing refresh writers. Configure only the agent actions you intend a
handheld client to invoke.

S1-ADV is experimental and has not had a complete independent security audit.
The maintainer's bounded [plugin review and fixes](docs/SECURITY-REVIEW-2026-10-18.md)
and [earlier review](docs/SECURITY-REVIEW.md) record
confirmed findings, fixes, verification and remaining trust boundaries.
See [architecture](docs/ARCHITECTURE.md) and [setup](docs/OPENCLAW.md).
For a vulnerability, use the private reporting flow on the repository
[Security tab](https://github.com/s1z0v/s1-adv/security). Private vulnerability
reporting, dependency analysis and secret push protection are enabled. Do not
post credentials or exploit details in public.
