# S1-ADV 0.2.232

The Lab is a new tab in the app menu for everything plugged into the Cardputer. The 9 key jumps
to it, and `/lab` and `/rules` open it.

Devices lists every sensor and output with its live value, and each has its own test screen: see
the distance as your hand moves, turn the servo with the arrow keys, step along the LED strip, or
click the relays. If a unit stops answering, its screen says whether to check that unit or the
whole port.

Rules joins them together. Pick what to watch and what to do, one question at a time, or edit the
whole rule on one card. A rule can set an output, or follow a sensor so the output moves with it.
To set a distance, hold your hand there and press Enter. The Playing screen shows what each rule
is doing, and how close a waiting one is.

This covers the Ultrasonic, ENV III, Joystick 2, NFC, Mini Angle, Light, PIR, 2-channel relay,
SG90 servo, SK6812 strip, I/O hub, the GPS in the LoRa Cap, and the built-in tilt sensor, LED and
speaker. The servo, strip and relay limits are built in: the strip starts capped at 25% power,
and a relay can't chatter. Leaving a screen turns everything off.

Rules and hub wiring are saved on the SD card, so they survive this update and later ones.

Only the code has been tested so far, against simulated units. The first run on real hardware is
still to come. See [the controls guide](CONTROLS.md#lab) for wiring.
