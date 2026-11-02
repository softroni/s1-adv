# S1-ADV 0.2.239

In short: Lab finds the Ultrasonic

The Lab kept saying "Nothing at 0x57" for an Ultrasonic that was plugged in. It now asks the unit
the way Air Music does, by starting a real measurement, and it no longer gives up on the unit
after one lost reading.

If you'd used the Mini Angle on the Grove port, the Lab could also hold on to the wires for it. Now
the screen of an I2C unit always takes the port back.

An I2C unit can't go on an I/O hub port, or share the 1-to-3 hub with a Mini Angle, Light or PIR,
because they use the same wires. The unit's screen now says so when the I/O hub is found.

This has only run against a simulated Ultrasonic so far.
