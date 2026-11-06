# TV Remote

Tab, T, TV Remote, or type `/tv`. It works an Apple TV in two ways. **Infrared** needs nothing
set up, but you point the Cardputer at the Apple TV as you would its own remote. **Bluetooth**
makes the Cardputer a keyboard the Apple TV pairs with once. Then there's no need to point, and
you can type in search boxes and passwords on the Cardputer's keyboard. The TV's own volume, mute
and power always go by infrared, and those are a Samsung TV's.

![TV Remote with Bluetooth connected: the arrow keys around OK, and the keys for Home, Sleep, the TV's volume, mute and power, typing and Bluetooth](assets/screens/tv-remote.png)

## The keys

| Key | What it does | Sent by |
|---|---|---|
| `;` `.` `,` `/` | Up, down, left and right, with or without Fn. Hold one to keep going | Bluetooth when connected, otherwise infrared |
| Enter | OK | The same |
| Del | Back, the Siri Remote's Back or Menu button | The same |
| Space | Play or pause | The same |
| H | Home, the Siri Remote's TV button | Infrared |
| Z | Sleep. With HDMI-CEC on, the TV goes off too | Infrared |
| `=` and `-` | The TV's volume up and down. Hold one to keep going | Infrared, to the TV |
| M | Mute the TV | Infrared, to the TV |
| P | The TV's power, on or off | Infrared, to the TV |
| T | [Type](#typing) on the Apple TV | Bluetooth |
| B | Bluetooth on or off | |
| Shift+B twice | [Forget](#forgetting-the-apple-tv) the Apple TV | |
| Esc | Leaves the app | |

The bottom line says what the last key sent and how, such as "Up by infrared" or "Play/Pause by
Bluetooth". The top corner says how keys are going: **Infrared**, **BT waiting** while Bluetooth
is on but nothing is connected, **Bluetooth** when the Apple TV is connected, **BT starting** while it waits for the network to
finish something, or **BT failed**.

The screen dims and goes off on the usual timers, but the screensaver doesn't start here, and a
key pressed with the screen dimmed or off still does its job, as on a real remote.

## Infrared

Point the Cardputer at the front of the Apple TV and press a key. The Apple TV's codes are its
own Siri Remote's, recorded from an Apple TV 4K, so there's nothing to teach or set up.

The TV's volume, mute and power use the codes Samsung TVs have used for years. Another make of TV
won't answer them yet. Point at the TV for those, which is usually right beside the Apple TV.

## Bluetooth

Press **B**. The first time, the screen says where to pair it:

![TV Remote waiting for its first pairing: Pair on the Apple TV, Settings, Remotes and Devices, Bluetooth, Cardputer Remote](assets/screens/tv-remote-pairing.png)

1. On the Apple TV, open **Settings**, then **Remotes and Devices**, then **Bluetooth**.
2. Pick **Cardputer Remote** from the list. No code is needed.
3. The corner changes to **Bluetooth**. From now on the arrows, OK, Back and Play go by Bluetooth.

After that the Apple TV connects again by itself, a few seconds after the app opens. Bluetooth
runs only while TV Remote is open. Leaving the app turns it off. If you left it on, it comes back
on the next time you open TV Remote. B turns it off for good, and the keys go by infrared again.

**Wi-Fi is off while Bluetooth is on.** Bluetooth needs the memory Wi-Fi uses, and the remote
doesn't need the network. Wi-Fi comes back by itself when you leave the app or press B, and
messages and updates catch up then. If something is still using the network when you press B,
the corner says **BT starting** and Bluetooth starts as soon as it's done.

Home and Sleep always go by infrared, as do the TV's own keys, because a keyboard has no key the
Apple TV reads as those.

### Forgetting the Apple TV

Press **Shift+B**, then Shift+B again within four seconds. The Cardputer forgets every device it
was paired with. Forget it on the Apple TV as well (Settings, Remotes and Devices, Bluetooth,
Cardputer Remote, Forget Device) before pairing again.

## Typing

Press **T** wherever the Apple TV wants text: a search box, a sign-in, a Wi-Fi password. If
Bluetooth is off, T turns it on first.

![The typing screen: the word bluey typed, and Keys type on the Apple TV](assets/screens/tv-remote-typing.png)

Every key types on the Apple TV, the letters that are remote keys elsewhere too, with Shift for
capitals and symbols. Enter sends, Del erases, and **Fn** with `;` `.` `,` `/` moves, so you can
reach the next field or the Apple TV's own on-screen keys. Esc goes back to the remote.

The Cardputer shows what you've typed since the last Enter. It's never logged or saved, so a
password typed here stays on the screen only. The characters are those of a US keyboard, and a
letter with an accent can't be sent.

## Memory

Bluetooth takes memory that the radio and every secure connection also need, so:

- Opening TV Remote stops the radio or a podcast.
- Background checks for messages and updates wait until you leave, as on IR Remote.
- Turning Bluetooth on turns Wi-Fi off and clears the device's caches, to give it room.
- If there still isn't enough memory, the corner says **BT failed** and the bottom line says
  why, Wi-Fi comes straight back, and infrared still works. The log's `TV` lines say how much
  there was and how much Bluetooth took.

Having Bluetooth in the firmware at all leaves every app about 17 KB less memory, even with
Bluetooth off. Most of that is the Bluetooth controller's code, which has to sit in the chip's
fast memory.

## Not tested on hardware yet

All of this has been checked on a computer, not yet on a Cardputer and an Apple TV:

- Whether the Apple TV answers the infrared codes from the Cardputer, and from how far.
- Pairing with no code, reconnecting by itself, and which keys the Apple TV takes from a keyboard.
  Esc for Back and a media key for Play/Pause are my best reading, not something seen working.
- How much memory Bluetooth takes while it's on, and whether it starts again cleanly after
  leaving. The device's log says both: look for the `TV` lines in Logs, and press U there to send
  them to the bridge. 0.2.307 started Bluetooth with Wi-Fi on and had too little left, which is
  why 0.2.308 turns Wi-Fi off first.
