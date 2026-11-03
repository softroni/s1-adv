# Mesh on S1-ADV

The Tools → Mesh application adds a Meshtastic leaf client to S1-ADV on the
Cardputer ADV with Cap LoRa-1262. It preserves the existing applications and
8 MB dual-OTA partition layout. It does not replace S1-ADV with the standalone
Meshtastic firmware.

Attach the antenna before powering the Cap. Start Mesh with Enter. Esc stops
the radio and returns Home; while reading a message, the first Esc closes the
message. Cancelling during radio initialization waits for the worker to finish
before releasing the radio or restoring SD.

## Shared hardware

Mesh exclusively owns the shared SPI bus while active. SD-dependent audio,
voice, notes, files, assets and NES applications are temporarily unavailable.
RFID and Sonar also conflict with the Cap's GPIOs. Exit Mesh before using these
applications. Selecting another non-conflicting application stops Mesh first.
The SD is remounted after radio shutdown, including failed initialization.
No files are formatted or deleted. Mount failure is reported separately from
radio state; internal storage becomes available again after leaving Mesh.

Network-heavy polling and voice services pause during Mesh. Pomodoro time,
alarms, battery and SmartLamp servicing continue. This first version does not
run Mesh in the background alongside those applications.

## Region

Mesh uses Meshtastic's US region unless you pick another: LongFast public
channel, 906.875 MHz, 250 kHz, SF11, CR4/5. Pick yours in Settings → Device →
**Mesh region** with Left and Right (Enter steps forward too). The Mesh screen
shows the region and its channel before you start the radio, and a running
radio keeps the region it started with until you leave Mesh.

The list holds the Meshtastic regions inside the band M5Stack rates the Cap for,
868 to 923 MHz (the AS923 channels), all on their LongFast channel:

| Region | Channel |
| --- | --- |
| US (default) | 906.875 MHz |
| ANZ | 919.875 MHz |
| BR_902 | 903.875 MHz |
| EU_868 | 869.525 MHz |
| JP | 923.375 MHz |
| KR | 922.875 MHz |
| MY_919 | 922.875 MHz |
| PH_868 | 869.375 MHz |
| PH_915 | 917.875 MHz |
| RU | 869.075 MHz |
| SG_923 | 917.875 MHz |
| TH | 923.875 MHz |
| TW | 923.875 MHz |
| UA_868 | 868.375 MHz |

Regions below 868 MHz (IN, NZ_865, KZ_863, NP_865, EU_866), the 433 MHz and
2.4 GHz regions, and the licensed amateur bands are left out. Picking a region
in software doesn't make transmitting legal where you are, so check your local
rules. Until 0.2.269 Mesh was fixed to TH.

## Channel and controls

- Transmit power is fixed at 10 dBm; the library applies regional airtime
  accounting and carrier sensing. These software settings do not certify an
  antenna or regulatory approval.
- Enter sends up to 200 UTF-8 bytes to the public channel. `Queued` means local
  acceptance, not confirmed delivery. Public-channel messages are public.
- Ctrl+N switches to nodes heard; Up/Down changes pages or message selection.
- Ctrl+O opens the selected message. Esc closes it before exiting Mesh.
- The in-memory history holds eight messages and sixteen recently heard nodes.
  It resets on exit. No SD writes occur while the radio owns SPI.
- The device identity is generated once and retained in NVS, independently of SD.

The client receives channel text and node announcements and sends channel text
and its own NodeInfo. It does not relay packets, provide the Meshtastic phone
API, use the Cap GNSS, or provide private-message UI. Chat stays on its region's
LongFast channel; [Radio Scan](#radio-scan) is the only part that tunes anywhere
else, and it only listens.
An empty list means no compatible announcements were received during this
session; it does not establish that there are no nodes nearby. Received radio text
is display data and is never executed as a device or agent instruction.

## Radio Scan

Press S on the Mesh screen instead of Enter. Radio Scan never transmits, needs
no identity and writes nothing, and it takes the bus from the SD card exactly as
Mesh does, so the same apps wait until you leave. Esc stops it.

It opens on a **sweep** of your region's band (Settings → Device → Mesh region),
one 250 kHz channel at a time. US is 902 to 928 MHz in 104 steps. A region
narrower than 8 MHz, such as EU_868, is shown 8 MHz wide around its band.

- Each bar is the latest reading on that channel, from -130 dBm at the bottom
  to -60 dBm at the top. The white tick above it is the strongest reading
  lately, falling 2 dB a second, so a short burst stays visible for a while.
- The orange mark under the chart is the region's LongFast public channel.
- **Floor** is the middle reading of the band, roughly the background noise.
  **Top** is the strongest recent reading and its channel.

A LongFast packet lasts about a second and most meshes are quiet between
packets, so a sweep shows bursts rather than a steady signal. Other devices
share the band too, so a burst isn't necessarily Meshtastic.

Enter switches to **listening**. It tunes to one preset's default public channel
with Meshtastic's sync word and counts the packets it hears, whoever sent them
and whatever channel they're on. Up and Down change the preset: LongFast,
MediumFast, MediumSlow, LongModerate, ShortFast and ShortSlow. If LongFast stays
quiet, try the others, because some local meshes use another preset. Each
preset starts counting afresh, and Enter goes back to the sweep.

- The top lines count packets, the senders they came from (up to 32, then
  `32+`) and **garbled** ones: too short to be Meshtastic, or damaged on the way.
- Each row is a packet, newest first: the sender's node ID, its signal strength
  and SNR, how many hops it made (`direct`, a number, or `hops ?` from older
  firmware), and `pub` when it's on that preset's default public channel or
  `other` for any other channel, which is often a private one.
- Only the 16-byte header every Meshtastic packet sends unencrypted is read.
  Nothing is decrypted or shown as text, and nothing heard is ever treated as
  an instruction. Reading messages is what Mesh chat (Enter) is for.

If the radio stops answering, the screen says so and Radio Scan stops using it.

## Checks

`scripts/test_mesh_leaf_state.py`, `test_mesh_lifecycle.py`, `test_mesh_radio.py`
and `test_mesh_ui.py` exercise the production state, lifecycle, radio setup and
240×135 renderer. `test_radio_scan.py` runs Radio Scan's sums, service loop, keys
and screen against a pretend radio that has no way to transmit. `test_mesh_region.py`
checks the region list, channels and scan presets against the library's own
table, so it runs after a firmware build. Lifecycle tests use controlled hardware mocks and include
worker cancellation, partial initialization, SD restoration and app guards.
State and UI tests include malformed UTF-8, fixed bounds and sanitizers.
These checks do not substitute for RF communication with a second node.

USB commands: `mesh.open`, `mesh.start`, `mesh.scan`, `mesh.status`, `mesh.nodes`, `mesh.stop`.
During Radio Scan, `mesh.status` adds a line with the sweep's floor and top and the
listening counts.
They do not send a test chat message. Serial logs report successful radio init,
session heap minima and SD restoration. No private keys are logged.

## Source and licensing

S1-ADV's original MIT notices remain in `LICENSE`. The firmware image linked
with libmeshtastic-leaf is distributed under GPL-3.0-only; this does not
relicense the separately distributed bridge/plugin or remove existing notices.
The GPL text is included in `third_party/libmeshtastic-leaf/LICENSE`.

Complete S1 source and build instructions are in this repository. Dependencies
are fetched by `bots/CardputerADV/platformio.ini`: libmeshtastic-leaf commit
`bd542d6e77aa7b9507dcc8d153f6dad0c35a8c17`, RadioLib 7.7.1, Nanopb 0.4.92,
and the leaf dependency's Crypto commit
`591ff9a690e8168ccb7a36abde8d7783e448d395`. The leaf repository includes its
pinned protobuf submodule and generated sources. Source links and notices:

- https://github.com/Meshtastic-Solutions/libmeshtastic-leaf/tree/bd542d6e77aa7b9507dcc8d153f6dad0c35a8c17
- https://github.com/jgromes/RadioLib/tree/7.7.1
- https://github.com/nanopb/nanopb/tree/0.4.9.2
- https://github.com/meshtastic/Crypto/tree/591ff9a690e8168ccb7a36abde8d7783e448d395

Build with `python scripts/cardputer.py build` using the documented ESP32-S3,
8 MB configuration. Provisioning credentials are local inputs, never source
files. Preserve the source commit listed in each firmware manifest when
rebuilding a distributed image.
