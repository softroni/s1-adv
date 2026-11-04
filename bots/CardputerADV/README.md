# S1-ADV for Cardputer ADV

For this fork's current OpenClaw installation, follow the
[project README](../../README.md), [USB guide](../../docs/FLASHING.md) and
[bridge setup](../../docs/OPENCLAW.md). Use `scripts/cardputer.py` from the
repository root. The original OmniBot hub/BLE workflow below is historical;
current firmware uses a Wi-Fi setup portal.


`CardputerADV` is the S1-ADV firmware target for M5Stack Cardputer ADV.
The project began with OmniBot; see [Credits](../../CREDITS.md) for its origin.

## Current OpenClaw workflow

- Firmware: ESP32-S3, 8 MB flash, dual OTA partitions.
- Transport: your bridge, authenticated HTTP/WebSocket, HTTPS/WSS for remote use.
- First boot: Wi-Fi setup portal, or your private compile-time provisioning.
- Later: Settings > Wi-Fi or `/wifi` changes network on the device.
- Chat: keyboard text and ordinary push-to-talk with bridge-side STT/TTS.
- Offline: focus timer, notes, calculator, clocks and games.

```bash
# From the repository root, with PlatformIO installed in this Python environment:
python3 scripts/cardputer.py build
python3 scripts/cardputer.py upload --port YOUR_USB_PORT
python3 scripts/cardputer.py monitor --port YOUR_USB_PORT
```

See [the flashing guide](../../docs/FLASHING.md) before building or uploading.
Use `/help` for current controls, `/status` for diagnostics and `/setup` for
network and bridge provisioning. Realtime voice is separately configured and
experimental. Camera and hands-free wake-word features are not supported here.

## Sonar

Sonar measures distance with either of two sensors, and finds whichever is plugged in:

- **The M5Stack I2C ultrasonic unit** (RCWL-9620, address 0x57) on the Grove port, the same
  unit Air Music uses. It reads up to 4.5 m. Each reading takes it 120 ms, so it gives up to
  eight a second.
- **An HC-SR04** wired to the EXT header, read ten times a second up to 4 m. Wiring is below.

When Sonar opens, it checks the Grove port first. If the unit answers, Sonar uses it and leaves
the EXT pins alone. Otherwise it listens for the HC-SR04, and while that gets no echo it checks
Grove again every second, so you can plug the unit in with Sonar open. If the unit stops
answering, Sonar goes back to the HC-SR04. With both plugged in, the unit wins. The name next to
the title, `M5 UNIT` or `HC-SR04`, says which one Sonar is using, and `R` checks Grove again
straight away.

If the Lab has relays saved on the Grove wires, Sonar doesn't touch the port, because the I2C
bus would switch them on. It uses the HC-SR04 only. Mesh keeps Sonar closed, because the
HC-SR04's pins are the Cap's.

### HC-SR04 wiring

The HC-SR04 uses the Cardputer ADV EXT 2.54 mm header:

- `VCC` → `5VOUT` (third contact on the lower row)
- `GND` → `GND` (second contact on the lower row)
- `TRIG` → `G4` (second contact on the upper row)
- `ECHO` → voltage divider → `G6` (third contact on the upper row)

The HC-SR04 ECHO signal is 5 V and must not go directly into the ESP32-S3.
Use `ECHO → 1 kOhm → G6`, with `2 kOhm` from the G6 node to GND.
`4.7 kOhm / 10 kOhm` is also suitable.

On the Sonar screen, press `M` to switch between the large Proximity view and
the animated Radar view. Radar plots the measured range on the fixed forward
bearing; a single stationary sensor cannot determine left/right angle.

## Air Music

**Air Music** works with the M5Stack I2C ultrasonic unit (RCWL-9620, address 0x57), plugged
into the Grove port. Open it from Arcade in the launcher, or type `/music` in chat. Only one
app can use the Grove port at a time. Air Music, Flappy Bird, Hot Air Balloon, NFC Notes and
Sonar each release it when they close.

**Flappy Bird** uses the same unit: a hand 6 to 30 cm above it steers the bird. Any key
flaps instead, so it also plays without the sensor. Open it from Arcade, or type `/flappy`.

**Hot Air Balloon** uses an ENV III (SHT30, address 0x44) and a Joystick 2 (address 0x63),
both on the Grove port through a passive hub. A quick rise in humidity counts as a breath.
Either unit can be left out, because the joystick's button and the keyboard also work.
Open it from Arcade, or type `/balloon`.

**Ball Catch** uses a Joystick 2 on the Grove port: its button drops the ball and the stick
moves the basket. The keyboard also plays it. Open it from Arcade, or type `/catch`.

## Lab

**The Lab** is a tab in the launcher, and `/lab`, `/rules` or `/sequences` opens it. Devices tests
every sensor and output, Rules saves "if this, then that" rules and plays them with live status,
and Sequences runs saved steps in order, by themselves or from a rule. Rules come from steps on the
device, ready-made recipes, or asking Claude, and Practice tries them with nothing plugged in. It
uses:

| Connection | Parts |
| --- | --- |
| Grove port, directly or through a passive hub | Ultrasonic (RCWL-9620, 0x57), ENV III (SHT30, 0x44), Joystick 2 (0x63), NFC (0x50), DLight (BH1750, 0x23) |
| I/O Hub (Unit PbHub, 0x61) on the Grove port | Mini Angle, Light, PIR, 2-channel relay, SG90 servo, 30-LED SK6812 strip |
| Cap LoRa-1262 | GPS on G15 (receive) and G13 (transmit), 115200 baud, NMEA |
| Built in | BMI270 tilt, RGB LED, speaker, and the IR transmitter for TCL and Daikin air conditioners |
| Wi-Fi | smartLamp |

Any of those can be used alone or together, and the Lab follows when the connection changes. The
parts on the I/O hub can also go straight on the Grove port with no I2C unit attached: a sensor
on white (G1), which reads analog, and a servo or strip on yellow (G2). The relay unit uses both.
See [Lab in the controls guide](../../docs/CONTROLS.md#lab) for the wiring and every key.

The ENV III's pressure sensor isn't read. The M5StickS3 and M5StickC Plus2 are separate computers
with no sensor mode, so they would need firmware of their own.

## NFC Notes

**NFC Notes** works with the M5Stack NFC unit (ST25R3916), plugged into the Grove port.
Open it from Tools in the launcher, or type `/nfc` in chat. Hold a tag on the unit to read
it. To save a name or note, type it, press Enter, then hold a tag on the unit. Saving
replaces what the tag holds, and the note is read back to check it. A save that isn't
finished in 30 seconds is cancelled.

| Tag | What happens |
| --- | --- |
| NTAG 213/215/216, Ultralight, other NFC Forum tags | Stored as a standard NDEF text record, so a phone can read it too. |
| MIFARE Classic with the default key | Stored from block 4 in this app's own format, so only this app can read it. |
| MIFARE Classic with other data or a private key | Left alone. The app only writes to a blank card or one of its own notes. |
| A tag that isn't NDEF formatted | Left alone. Formatting can be permanent, so the app doesn't do it. |

An NTAG 213 holds 134 letters, and an original Ultralight holds 38. The Grove port is only
in use while the app is open, and the radio is only on for a moment at each check.

## RFID Lab wiring

The current firmware uses an **MFRC522/RC522 SPI module**, usually the board
with eight contacts. It does not use an I2C RFID2/Grove module or a PN532 driver.

| RC522 contact | Cardputer ADV connection |
| --- | --- |
| SDA / SS / NSS | EXT **G5** (SPI chip select, not I2C SDA) |
| SCK | EXT **G40** |
| MOSI | EXT **G14** |
| MISO | EXT **G39** |
| RST | EXT **G3** |
| GND | EXT **GND**, shared with the 3.3 V supply |
| 3.3V | Regulated **3.3 V** |
| IRQ | Leave unconnected |

**The EXT header has 5VIN and 5VOUT, but no 3.3 V supply contact. Do not
connect a bare RC522's 3.3V contact directly to 5VOUT or the Grove red wire.**
One external-power arrangement is Cardputer 5VOUT → 5-to-3.3 V regulator →
RC522 3.3V, with a common ground. Identify wires by their printed GPIO/signal
labels; connector orientation changes when the board is turned over.
See the [official Cardputer ADV pin map](https://docs.m5stack.com/en/core/Cardputer-Adv)
and [NXP MFRC522 electrical specifications](https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf).

Open **RFID Lab** from the app launcher, or type `/rfid` in chat. `R` retries
reader initialization; `?`/`H` shows the wiring help. The expected idle status
is `Ready: present card`. Present a compatible 13.56 MHz ISO/IEC 14443-A tag to
read its UID, SAK, UID length and PICC type. This is not a 125 kHz reader.

`D` reads accessible MIFARE Classic blocks using the default `FF FF FF FF FF FF`
key into RAM. `W` immediately writes the captured data to a compatible target,
skipping sector trailers and the manufacturer block in the regular write loop;
the code also attempts a UID change for supported UID-writable cards. This is
not a universal card clone. Start with UID detection and use only a spare tag
for write tests. NTAG/NDEF text or URL writing is not implemented in this app.

The reader shares SCK/MOSI/MISO with microSD; its chip select is G5, while
microSD uses G12. The pin map and controls were checked against the source;
physical reader detection, card reads and SD coexistence still need a hardware
test on the attached module.

## Voice Flow

1. Hold `BtnA` or `Fn`
2. Speak into the Cardputer mic
3. Release the trigger
4. The hub transcribes speech with OpenAI, generates a reply, and streams PCM back
5. The Cardputer shows transcript + reply and plays the spoken answer through its speaker

## Emoji SD Pack

The firmware renders color emoji from the SD card only. Put PNG assets under
`/emoji/uHEX.png` on the card root, for example `/emoji/u1F600.png`.

Emoji assets are optional and are not bundled with the public source.
