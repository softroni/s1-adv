# S1-ADV 0.2.214

NFC Notes is a new tool for the M5Stack NFC unit (ST25R3916) on the Grove port. It's under
Tools, and `/nfc` now opens it. `/rfid` still opens RFID Lab, for the separate RC522 module.

Hold a tag on the unit to read its note, type and ID. To save a name or note, type it, press
Enter, then hold a tag on the unit. The app reads the note back to check it before saying
it's saved.

NTAG and Ultralight tags get a standard NDEF text record, so a phone can read the note too.
MIFARE Classic cards work as well, in this app's own format, and the app only writes to a
blank card or one of its own notes, so a door or transit card is left alone. It never
formats a tag, because on these chips that can be permanent.
