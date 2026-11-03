# Troubleshooting

| Symptom | Check |
| --- | --- |
| No USB port | Use a data cable, hold G0, press Reset, release G0; list ports again |
| Upload or boot fails | ESP32-S3 target, 8 MB flash and included dual OTA partitions |
| Screen blank or unchanged after flashing | It's still in flashing mode. Unplug it and plug it back in, or press Reset without holding G0 |
| `192.168.4.1` shows your router or `blocked.eero.com` | You aren't on the Cardputer's setup Wi-Fi. eero uses the same address. Join the setup Wi-Fi, and unplug Ethernet to that router for a moment |
| Device opens setup | None of the saved networks joined at start-up, each tried in turn. Press Enter on that screen to join Wi-Fi on the device. For the bridge URL and token, use the portal it shows, or configure private `.env` before building |
| Wi-Fi fails | 2.4 GHz coverage and credentials; captive portals need site-specific support. To change network, see [Wi-Fi controls](CONTROLS.md#wi-fi) |
| Wi-Fi scan says "Scan failed" | The device tries for about 10 seconds first. The message gives the radio's error code and the free memory. `3006` means the radio is busy connecting. "Scan lost" means the scan started and its results never arrived. A scan made while joined takes about 9 seconds. To see everything the radio hears, send `wifi.scan.raw` over USB |
| Saved networks missing | Check the SD card. If Wi-Fi settings says it couldn't read them, the file was damaged or came from another Cardputer. It's set aside as `/wifi/networks.bad`, and networks are saved again as they connect |
| Bridge returns 503 | Proxy upstream, bridge listening port, service logs and gateway connection |
| Service says active but no port listens | Check gateway handshake; update bridge client with bounded connection timeout, then restart only the bridge |
| TLS error | Correct hostname, port 443 and certificate chain rooted in firmware's trusted CA |
| 401 or 403 | Device token, device ID and topic allowlist agree on both sides |
| Topics work but no reply | Gateway pairing, scopes, session key, provider authentication and actual upstream readiness |
| Text works, voice fails | FFmpeg, STT binary/model, TTS binary and their service-user permissions. Look for `ENOENT` in the bridge log, and run [`setup.mjs voice`](CLAUDE-CODE.md#add-voice) |
| Voice replies say nothing was heard, or words are wrong | Before firmware 0.2.207, the start of each recording can be lost. Update, or hold G0, wait about 3 seconds, then speak. See [Add voice](CLAUDE-CODE.md#add-voice) |
| Claude can't build firmware | Set up the build tools and `firmwareUpdates`. See [Update firmware from the Cardputer](CLAUDE-CODE.md#update-firmware-from-the-cardputer) |
| Claude says it has no tools or doesn't know its folder | That's the default. See [Let Claude work in a folder](CLAUDE-CODE.md#let-claude-work-in-a-folder) |
| Spoken replies sound robotic | That's eSpeak. Run [`setup.mjs voice`](CLAUDE-CODE.md#add-voice) with Python 3.10 to 3.13 installed, then restart the bridge |
| OTA appears successful | Verify rebooted version on the device; a download or queued event is insufficient |

Start with `/status`, then capture the serial monitor at 115200 baud. For the
bridge, inspect `journalctl -u openclaw-cardputer-bridge -n 100` on systemd hosts.
Redact SSIDs, private messages, tokens, endpoint details and audio before sharing
logs. A private device binary can contain your Wi-Fi password and token.

After a bridge outage, repeat a short text request once service and gateway
connectivity recover. Do not assume a request reached the agent merely because
the device displayed a waiting animation.

For a bug report include board model, firmware version, commit, OpenClaw version,
which phase fails, and a minimal redacted reproduction. Do not upload state.json,
gateway config, `.env`, provisioned firmware or private voice recordings.
