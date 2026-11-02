# S1-ADV 0.2.236

In short: Parts work without the hub

The Lab no longer needs the I/O hub. Plug a Mini Angle, Light, PIR, 2 relays, Servo or LED strip
straight into the Grove port and open it in Devices. These parts can't answer on the port, so the
list says press ENTER for them, and the Lab then remembers what's plugged in, for rules too.

Through the 1-to-3 hub, a sensor on the white wire and an output on the yellow wire work together,
so a Mini Angle can drive the servo with no I/O hub. An I2C unit needs both wires, so if one's
plugged in, these screens say so.

A sensor screen stops showing numbers if nothing is on its wire, instead of inventing them. The
"not found" dots are grey now, because a part you haven't plugged in isn't a fault.

The Cardputer LED screen and rules that use it now try to light the real LED. Before, they only
looked as if they did.

Rules saved in earlier versions still load. This has only run against simulated parts so far.
