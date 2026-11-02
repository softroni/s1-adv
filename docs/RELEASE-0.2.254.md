# S1-ADV 0.2.254

In short: Home shows when it's charging

Plug in the Cardputer and Home now tells you what the power is doing, where the idle bars were:

- **Charging:** a green bolt, and the bars fill in a sweep. Only charging moves, so a still screen
  means it isn't
- **Fully charged:** the bars stay full and green
- **Not charging:** it's plugged in but the battery isn't taking charge. This usually means the power
  switch is off, because it runs from USB with the switch off but only charges with it on
- **On battery:** a short note when you unplug, then Home goes back to normal

The small gauge under READY gets the bolt too and blinks while charging, so you can still see it while
the assistant is working. While charging, the percent stops at 99 until the battery is really full,
because the reading reaches 100 well before the battery does.

The Cardputer can't actually be asked whether it's charging, because the charger chip's status pins
aren't connected to anything. On a computer it knows from the USB traffic. On a wall charger or power
bank it works it out from the battery voltage, which takes a few seconds when you plug in, and half an
hour or so if it starts up already on the charger. See [Controls](CONTROLS.md).
