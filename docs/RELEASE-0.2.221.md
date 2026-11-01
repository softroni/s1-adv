# S1-ADV 0.2.221

You can change Wi-Fi on the device now. Open Settings, then Wi-Fi, or type `/wifi`. Pick a
nearby network, or choose Other network to type a name, then enter the password on the
keyboard.

The new network is saved only after the device has joined it. If the password is wrong or the
network isn't there, the device goes back to the network it was on, and what you typed stays
in the field so you can fix it.

When the old network is gone and the setup screen appears, press Enter on it to do the same
thing without a phone. The device restarts once it has joined.

`/wifi` used to open the setup portal. `/setup` still does.

On builds with a default Wi-Fi password, a saved open network picked up that password at
boot and couldn't connect. It now keeps its blank password.
