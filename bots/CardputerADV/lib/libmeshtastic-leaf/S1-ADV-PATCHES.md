# libmeshtastic-leaf with a secondary channel

Source: [libmeshtastic-leaf](https://github.com/Meshtastic-Solutions/libmeshtastic-leaf)
commit `bd542d6e77aa7b9507dcc8d153f6dad0c35a8c17` (1.0.0), GPL-3.0-only, with its
protobuf submodule. Original headers, `LICENSE` and attribution are kept. This
project-local copy replaces the git dependency in `platformio.ini`; its `.git`,
`.github` and `.trunk` folders were left out.

Changes on 2026-11-03, all in `src/libmeshtastic_leaf.h` and `src/libmeshtastic_leaf.cpp`:

- `setSecondaryChannel(psk, length, name)`, `clearSecondaryChannel()`,
  `hasSecondaryChannel()` and `getSecondaryChannelHash()`: an optional second
  channel, the way a Meshtastic app adds a secondary one. It needs a name and a
  16 or 32 byte key. It shares the primary channel's radio settings, so it never
  moves the frequency.
- `receive()` tries the primary channel and then the secondary one when a
  packet's one-byte hash matches either, and keeps going if the primary's key
  doesn't give a Data message that parses. A packet read with the secondary key
  has `MeshPacket::channelIndex` 1, and 0 otherwise, as before.
- `sendDataOnSecondary(port, data, length, wantAck)` broadcasts on the
  secondary channel, reliably when asked, like `sendData()` on the primary, and
  `sendTextOnSecondary(text)` is the plain text form. `sendPacket()` encrypts
  with whichever channel `sendChannel_` points at, which is the primary except
  during that call, and a retry resends the frame it already made. NodeInfo,
  acks, PKI and every other send stay on the primary.

Why: the library held one channel and dropped any packet whose hash didn't match
it. A private channel in place of the public one would stop the radio hearing
public traffic and the Routing acks a stock node sends for a direct message on
its primary channel.

Changes on 2026-11-04, in the same two files:

- `onHeard(callback)`: every packet the radio reads, before the duplicate filter
  and before any decryption, with its RSSI and SNR. A packet whose CRC failed is
  reported with no bytes. `update()` now reads the packet before deciding what
  to do with it, which is the order it already had for accepted packets.
- `lendRadio()`, `resumeRadio()` and `radioLent()`: the application borrows the
  radio and gives it back. Lending clears the receive interrupt and makes
  `update()` do nothing. Resuming puts the radio in standby, applies the modem,
  frequency, sync word, preamble and power exactly as `begin()` did, sets the
  interrupt again and starts receiving. Nothing else changes, so pending acks,
  NodeInfo timers, keys and channels carry on.

Why: S1-ADV keeps the radio on for as long as its Mesh app is open, so Radio
Scan counts packets from the chat's own radio instead of turning it off, and
borrows the radio only to sweep the band or listen with another preset.

`scripts/test_mesh_channels.py` builds this copy on a computer, with RadioLib's
generic build and Crypto, and passes real packets between three nodes: public,
private, a wrong key, a 16 byte key, clearing it, and a direct message alongside.
