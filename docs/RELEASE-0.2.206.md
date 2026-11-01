# S1-ADV 0.2.206

Firmware source release. Build it with your own settings; see
[Build and flash](FLASHING.md). The public release image stays at 0.2.201.

## Voice capture

Hold-to-talk could lose the first 2 to 3 seconds of a recording. The secure connection
to the bridge opens as recording starts, and that task could run on the same CPU core
as the loop that drains the microphone, which only has two 64 ms chunks queued. Voice
connection tasks now stay on core 0.

## Text size

**Display > Text size** offers Small, Medium and Large. It applies to the chat
transcript, the message you're typing, and voice replies. Other screens have fixed
layouts and keep their sizes, and the face view stays Small. Large shows about three
lines at a time.

Chat lines now wrap to the width they're drawn at, so line ends are no longer cut off.

## Validation

Compiled and flashed to one Cardputer ADV. Physical acceptance of the voice change and
each text size is recorded in [Validation](VALIDATION.md) once confirmed.
