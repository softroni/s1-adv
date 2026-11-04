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
- `sendTextOnSecondary(text)` broadcasts text on the secondary channel.
  `sendPacket()` encrypts with whichever channel `sendChannel_` points at, which
  is the primary except during that call. NodeInfo, acks, PKI and every other
  send stay on the primary.

Why: the library held one channel and dropped any packet whose hash didn't match
it. A private channel in place of the public one would stop the radio hearing
public traffic and the Routing acks a stock node sends for a direct message on
its primary channel.

`scripts/test_mesh_channels.py` builds this copy on a computer, with RadioLib's
generic build and Crypto, and passes real packets between three nodes: public,
private, a wrong key, a 16 byte key, clearing it, and a direct message alongside.
