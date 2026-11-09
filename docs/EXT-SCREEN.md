# Ext screen

A 2.8" ILI9341 screen on the EXT header shows a copy of the Cardputer's screen, a third bigger.
It's the common red SPI module, 240x320, often sold with a touch panel and an SD card slot on the
back. Nothing needs turning on: plug it in and within three seconds it starts showing what the
Cardputer shows, and the Cardputer says "Ext screen found".

## Wiring

Nine wires, on the same EXT pins the Cap LoRa-1262 uses. The header's odd pins are the row with
G3 (pin 1) at one end and G5 (pin 13) at the other; the even pins are the row beside it.

| Screen pin | EXT pin | On the Cardputer |
| --- | --- | --- |
| VCC | 2 | 5 V in |
| GND | 4 | GND |
| CS | 13 | G5 |
| RESET | 1 | G3 |
| DC (or RS) | 5 | G6 |
| SDI (MOSI) | 9 | G14, MOSI |
| SCK | 7 | G40, SCK |
| LED | 6 | 5 V out |
| SDO (MISO) | 11 | G39, MISO |
| T_CLK, T_CS, T_DIN, T_DO, T_IRQ | | not connected |

SDO matters: the Cardputer finds the screen by writing to it and reading the answer back on that
wire. A screen without it can still be used with the setting below set to **on**.

The screen's backlight (LED) is wired straight to 5 V, so it can't be dimmed or turned off from
the Cardputer. When the Cardputer's own screen goes off, this one shows black instead.

## What it shows

Everything the Cardputer draws on its screen, 320x180 across the middle, with a black band above
and below. It copies at most 20 times a second, and only when something changed, a few rows
each time round the loop so keys and sound don't wait for it.

Two apps draw straight onto the Cardputer's screen and aren't copied: the Cameras picture and NES.
The Ext screen keeps the last picture until they close.

Touch isn't used yet. The touch chip shares the same SPI wires and needs its own select line,
such as G4 on EXT pin 3.

## Settings

Settings, Display:

- **Ext screen**: **auto** (the default) looks for it every three seconds, and says
  **auto, found** while it's there or **auto, none** when nothing answered. **on** drives it
  without looking, for a screen whose SDO isn't wired. **off** leaves the EXT pins alone.
- **Ext upside down**: **yes** turns the picture round, for a screen mounted with its pin header
  on the right.

## Sharing the header

The screen is on the SD card's SPI bus, and the firmware takes turns, so the SD card keeps working.

Its other pins are the Cap's, the RFID Lab's RC522 and the HC-SR04's, so only one of those can be on
the header at a time. With a Cap fitted the Cardputer never writes to it, and if a Cap takes the
screen's place while running, it lets go. While Mesh, RFID Lab or Sonar's HC-SR04 is using the
pins, the screen keeps its last picture, then picks up again when they close.

## When it stays white

White means the screen has power but hasn't been set up.

- **Ext screen says auto, none.** Nothing answered. Check SDO (pin 11) and CS (pin 13), or set it
  to **on**.
- **The picture is upside down.** Set **Ext upside down** to **yes**.
- **Speckles or wrong colours.** Use shorter wires. The pixels go at 20 MHz.
- **It worked on USB and not on battery.** Try VCC on pin 6, with LED.

Over USB, the serial log has a line starting `[EXT]` for each thing it found or lost, with what
the screen read back.
