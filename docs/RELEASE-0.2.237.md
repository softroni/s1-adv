# S1-ADV 0.2.237

In short: Lab follows how you connect

Use one part directly or any number through a hub, and change your mind whenever you like. Move a
part between the Grove port and the I/O hub with its screen open, and the screen follows it.

On the I/O hub, a part can be on any port. If it doesn't respond, open it, press P and pick the port
it's in, which swaps it with whatever was there.

The hub can't tell if a port is empty, and an empty port reads as noise, so the device list now
shows a part's port until you open it, rather than a number that means nothing. Screens say where
a part is instead of claiming it answered.

The Lab also writes what it finds to the USB log, for checking on a real board.
