# Everyday controls

- Type a question and press Enter in chat.
- Tap **Alt** on Pulse, AI Chat, the other assistant screens or [Say a rule](#lab) to start listening, then tap it again to send. Tapping it while Claude is talking stops the reply and listens. Esc cancels.
- You can also hold **G0**, speak and release, or use **Ctrl+V** or double **Ctrl**.
- **Tab** opens the app menu from Home, chat and Realtime. The arrow keys pick a row and switch group, and Enter opens. A letter jumps straight to a group: **A** AI Agent, **M** Media, **F** Focus, **T** Tools, **G** Games, **R** Arcade, **S** Settings, **Y** System, **L** Lab. The selected tab underlines its letter. **1-9** also jumps to a group, and **Ctrl+1-8** opens that row. The menu opens on what you last used, even after a restart.
- **Ctrl+K** opens assistant commands. R selects Realtime, C AI Chat.
- **Ctrl+U** on the Home clock installs an available update. An orange **NEW UPDATE / Ctrl+U** badge replaces READY until the newer firmware is installed. Without a candidate, Ctrl+U checks for updates.
- **Ctrl+R** starts Realtime or commits the current speech turn. While awaiting a reply, it stops Realtime.
- **Esc** closes an overlay, or ends Realtime and returns Home.
- **Ctrl+Left / Ctrl+Right** or **Ctrl+, / Ctrl+.** selects the previous/next animal.
- In **Realtime**, **[ / ]** also selects the previous/next animal, including while the mic is listening. In Full Chat, plain brackets remain text.
- **Ctrl+L** also opens the app menu. `/help` shows command help.
- `/status` shows connection and device information.
- `/wifi` opens Wi-Fi settings, to change network on the device. See [Wi-Fi](#wi-fi).
- `/setup` opens the setup portal, which sets Wi-Fi and the bridge from a phone or laptop.
- `/settings` opens device settings; `/ota` opens firmware update review.
- `/calc`, `/alarm`, `/stopwatch`, `/assets` and `/library` open local tools.
- `/nfc` opens NFC Notes, which reads tags and saves a name or note on them.
- `/music` opens Air Music, which plays notes as you wave a hand over an ultrasonic unit.
- `/flappy` opens Flappy Bird, steered by hand over the same unit or flapped with Space.
- `/balloon` opens Hot Air Balloon, flown by blowing on an ENV III and steered with a Joystick 2.
- `/catch` opens Ball Catch, where you drop a ball and catch it in a basket with a Joystick 2.
- `/lab` lists every sensor and output so you can test them, `/rules` opens the saved rules that
  join them, and `/sequences` opens the steps you've saved. See [Lab](#lab).

This firmware has evolved across development versions. Prefer the command help
on your built device for current shortcuts. Specialized IR, RFID and sonar
tools require compatible extra hardware; see the retained target documentation.
Voice quality and response time depend on your bridge's STT/TTS configuration
and the agent, network and provider latency.

## Wi-Fi

To change network, press Tab, open Settings, then Wi-Fi, or type `/wifi`.

- **Pick a network:** saved networks come first, most recently used at the top, then what's
  nearby, strongest first. `;` and `.` move, and Enter picks. R looks again. Signal bars show
  what's in range, and only 2.4 GHz networks appear. A filled dot marks the network in use, a
  hollow one a saved network, and a saved one out of range is greyed. The chip at the top right
  says whether the device is connected, offline or scanning. The screens share the Lab's look.
- **Switch:** Enter on a saved network joins it with no password.
- **Forget:** F on a saved network, then Enter. Its password is deleted. If it's the network
  you're on, the device moves to the next saved one.
- **Type a name:** Other network, at the end of the list, is for a hidden network. Capitals
  count. Leave the password blank for an open network.
- **Password:** type it and press Enter. Hidden characters show as dots, apart from the last
  one typed. Fn+`;` shows or hides it. Fn+`` ` ``, shown as Fn+Esc, goes back, because `` ` ``
  and `~` are typed as part of the password here.

A network is saved only once the device has joined it, which takes up to 20 seconds. Esc
cancels. If it can't join, the device goes back to the network it was on and keeps what you
typed, so you can fix it. If a saved network's password has changed, it asks for the new one.
The bridge has to be reachable from the new network too.

Saved networks need an SD card, and any network that connects is saved, including one set from
the setup portal. They're kept in `/wifi/networks.dat`, so they stay through firmware installs.
The file is encrypted with a key tied to this Cardputer's chip, so the card alone doesn't give
up a password, and it can't be read on a computer or by another Cardputer. Without a card the
device still connects to the last network it used, and it asks you to pick when that isn't
available.

At start-up the device dials the network it used last. If that doesn't connect, it tries the
other saved networks, most recently used first, each once, and joins the first that works. That
one then comes first next time, and the chat says which network it joined. A network that isn't
in range is passed over after about 6 seconds, and one that's there but refuses gets 20. The last
one tried always gets the full 20, in case its router is still starting up. This checks that the
Wi-Fi joins, not that it has internet.

The same happens while you're using the device. When the network in use is lost, the device keeps
reconnecting to it for 20 seconds, or 8 if the radio says it's gone. Then it tries the other saved
networks the same way and joins the first that works, and the chat says so. If none does, it goes
back to the lost one and keeps reconnecting, and tries the others again a minute later. It doesn't
switch during a recording, or while Settings > Wi-Fi is open.

Pulse shows Wi-Fi in its top right corner. Cyan rings show the signal strength, an amber sweep
means it's connecting, and a red dot means Wi-Fi is off or the device is waiting in setup.

The setup screen appears only when none of them connects. Press Enter on it to do all of this on
the device. It restarts once it has joined.

## Settings

Press Tab and open Settings, then pick a section. Up and down move through the rows, and longer
lists scroll. Left and right change a value, and Enter toggles it or opens it.

## Weather on Pulse

When nothing is happening, the Pulse screen shows the date and the weather where the orb usually
sits: temperature, conditions, today's high and low, the chance of rain, and the place. The orb
comes back as soon as the assistant is doing something.

Weather loads about 30 seconds after the device starts and connects, then every 30 minutes. If a
refresh fails, the last weather stays on screen and the device tries again in 2 minutes. Once a
refresh has been missed, the bottom line changes to "As of" and the time it was last updated.

To change the place, press Tab, open Settings, then Device, and pick **Weather city**. Press Enter,
type the city and press Enter again. The default is Salem, MA.

Add the state, like `Salem, MA` or `Salem, Massachusetts`, because many towns share a name. Without one
you get the largest town with that name. US places only, and a state that has no such town shows
"Check your city" rather than another state's weather.

## Status and sounds

While you wait, AI Chat and the Pulse screen show what Claude is doing right now, and each kind
of work has its own short sound. The colour, the icon and the sound all change together. In AI
Chat a bar above the text box also shows the detail and how long it has taken. On Pulse, the
badge has the word, the middle of the orb has the icon, and the box at the bottom has the detail.

| Status | What's happening | Detail shown | Sound |
| --- | --- | --- | --- |
| LISTENING | The microphone is on | Tap Alt to send | None, because the speaker is off while recording |
| SENDING | Your message is going out | | Two rising notes, for typed messages. A voice message is silent here, because the speaker is off while it uploads. |
| THINKING | Claude is working out what to do | | Two low taps |
| SEARCHING | Searching the web | What it's searching for | A sonar ping and its echo |
| RESEARCHING | Reading a web page | The site's name | Three steps up |
| READING | Reading or searching your project | The file or search | Two soft taps, stepping down |
| CODING | Editing a file | The file | Four quick key presses |
| WORKING | Running a command | Claude's note on what it's for | A plain low-high |
| BUILDING | Building the firmware or an app | Claude's note | Two knocks and a higher third |
| TESTING | Running tests | Claude's note | Up, down, up |
| WRITING | Writing the answer | | A quick soft run |
| LOADING | The reply's voice is downloading | | Two slow notes, in place of the reply sound |
| SPEAKING | Claude is talking | | A bright lift, then the speech |

A reply that won't be spoken plays two falling notes when it arrives.

Claude can change tools several times a second, so a sound only plays for a real change, at most
every 2.5 seconds, and the same one doesn't repeat for 30 seconds. Going back to THINKING between
steps is silent. Sounds follow the notification volume.

The status comes from the tools Claude really uses, so it needs the Claude Code bridge. With
OpenClaw, or an older bridge, it stays on THINKING until the reply arrives.

## NFC Notes

NFC Notes is under Tab, Tools, and `/nfc` opens it. It needs the M5Stack NFC unit
(ST25R3916) on the Grove port.

- **Read:** hold a tag on the unit. Its note, tag type and ID appear.
- **Save:** type a name or note, up to 200 letters, and press Enter. Then hold a tag
  on the unit. The app reads the note back to check it, and says so.
- Esc cancels a save, then clears what you typed, then leaves the app.
- Fn with `;` or `.` scrolls a long note.

## Mesh and Radio Scan

Mesh is under Tab, Tools, and needs the Cap LoRa-1262 with its antenna fitted. On its first
screen, Enter starts the Meshtastic chat and S starts Radio Scan, which only listens: a sweep
of signal strength across your region's band, and with Enter, a count of the Meshtastic
packets heard on a preset's channel (Up and Down change the preset). Esc stops the radio.
The region is Settings, Device, Mesh region. [Mesh](MESH.md) has the details.

## Air Music

Air Music is under Tab, Arcade, and `/music` opens it. It needs the M5Stack I2C ultrasonic
unit (address 0x57) on the Grove port.

Hold a hand 4 to 44 cm above the sensor. Closer is higher, and there are ten notes, each
about 4 cm apart. Move away to stop. Esc leaves.

## Flappy Bird

Flappy Bird is under Tab, Arcade, and `/flappy` opens it. Point the ultrasonic unit up and
hold a hand over it: 6 cm is the bottom of the screen and 30 cm is the top, so the bird
follows your hand. Take your hand away and the bird falls.

Space or any other key flaps instead, so it also plays without the sensor. The pipes speed
up every 5 points, up to triple speed at 50 points. After a crash, the score shows for a
moment and the next game is ready to start. Esc leaves.

## Hot Air Balloon

Hot Air Balloon is under Tab, Arcade, and `/balloon` opens it. Connect an ENV III and a
Joystick 2 to the Grove port through a hub. Both are optional.

- **Rise:** blow on the ENV III, hold the joystick's button, or press Space or W.
- **Steer:** push the joystick left or right, or press A and D or the arrow keys.
- Collect stars, and avoid the birds. You have three hearts, and a floating heart gives
  one back. The ground is soft, so landing is safe.
- Every 10 stars the sky changes from day to sunset to night, and the wind picks up.
- A quiet music box plays while the game is open. M turns it off and on. It pauses behind
  the menu, and it stops while the assistant is using the mic or speaker.

## Ball Catch

Ball Catch is under Tab, Arcade, and `/catch` opens it. Connect a Joystick 2 to the Grove
port, or play on the keyboard.

- A ball slides along the top. Press the joystick's button, or Space, to drop it.
- Move the basket underneath with the joystick, or with A and D or the arrow keys.
- A falling ball keeps some of its slide, so aim ahead of it. Gusts push it harder at higher
  levels. A catch scores a point, and a miss costs one of three hearts.
- Let black bombs fall past the basket. Catching one costs a heart.
- The rail runs out while a ball waits. When it's gone, the ball drops by itself.
- The ball speeds up as you score, and every 5 catches the basket shrinks, bombs get more
  common and the rail gets shorter.

## Lab

The Lab is its own tab in the app menu, and the 9 key jumps to it. Devices tests everything the
Cardputer can reach, Rules joins them together, like "if this, then that", and Sequences runs steps
in order. The Grove port is only held while a Lab screen is open. For a tour with pictures, see [The Lab](LAB.md).

### Wiring

To try one part, plug it straight into the Grove port and open it in Devices. The Mini Angle,
Light, PIR, 2 relays, Servo and LED strip can't answer on the port, so the list shows **press
ENTER** for them. Opening one tells the Lab that's what is plugged in, and it remembers.

Through the 1-to-3 hub, the two Grove wires can serve one sensor and one output together, with no
I/O hub at all:

| Wire | Part |
| --- | --- |
| White | Mini Angle, Light or PIR |
| Yellow | Servo or LED strip |
| Both | 2 relays |

That's enough for the Mini Angle to drive the servo. I2C units use both wires, so an Ultrasonic,
ENV III, Joystick 2 or NFC unit can't share the port with these, even through the 1-to-3 hub: a
Mini Angle on the same hub stops them answering at all. If an I2C unit is plugged in, those screens
say so.

Most parts have more pages: a dot under the title shows which you're on. Left and right turn the
page, or **N** on the Servo and Speaker, where the arrows are already used.

| Part | Pages |
| --- | --- |
| Ultrasonic | Live, parking sensor (B for sound), graph |
| ENV III | Live, comfort map, graph (T swaps the reading) |
| Joystick 2 | Live, draw (press the stick to clear), target game |
| Mini Angle | Live, graph, steady hand game |
| Light | Live, sky, graph |
| PIR motion | Live, guard alarm (G arms it, after 5 seconds), graph |
| Tilt | Live, spirit level, roll the ball |
| Servo | Gauge, moves (up and down pick, left and right set the speed, Enter runs it) |
| Speaker | Tone, piano (1 to 8 play) |

**D** on the Devices list opens the **Dashboard**: every live sensor at once, each with a small graph
of its last minute. A part on the I/O hub joins it once you've opened that part.

To use more parts at once, plug the I/O hub into the Grove port, and use any of its six ports for
any of these parts, one or all of them. I2C units can't go on those ports, so to add them, put the
1-to-3 hub in front of it: its other two sockets take any two of them.

You can switch between these whenever you like, even with a part's screen open. The Lab notices
the I/O hub or an I2C unit arriving or leaving, and the part follows. Rules work the same way
however the parts are connected. With a servo, strip or relay on the Grove wires, the Lab mostly
checks when you press Esc on a screen, because bus traffic would disturb them.

Nothing on an I/O hub port can say what it is either, or whether anything's there. So if a part
doesn't respond, open it and press **P**, then pick the port it's in. Devices, W shows all six at
once. This is the starting layout:

| Port | Part |
| --- | --- |
| CH0 | Mini Angle |
| CH1 | Light |
| CH2 | PIR motion |
| CH3 | 2 relays |
| CH4 | Servo |
| CH5 | LED strip |

In the W screen, up and down pick a port, and left and right change only that port. To swap two
parts, set one to Nothing first, or use P, which swaps them for you. A digital light unit (DLight)
on the Grove port needs no port and is used instead.

### Devices

Up and down pick a device, and Enter opens it. A solid dot means it's there. A cyan ring means it
can be reached but can't confirm it, which goes for outputs and anything on the I/O hub. A grey
ring means it hasn't been found. Until you open it, a part on the I/O hub shows its port rather
than a number, because an empty port would only show noise. Esc goes back, and everything turns
off when you leave a test screen.

- **Ultrasonic, Light, Tilt and PIR:** a live number and the last 30 seconds. R resets the lowest
  and highest. If the Light reads backwards, F flips it.
- **ENV III:** temperature and humidity.
- **Mini Angle:** turn it end to end once so it learns its range.
- **Joystick 2:** the stick's position and its button.
- **Servo:** left and right turn it 5 degrees, A and D turn it 1, C centers it and S sweeps it. It
  goes limp when you leave.
- **LED strip:** left and right change the color, and up and down the brightness. M steps through
  Solid, Rainbow, Chase and Count, where the arrows move one lit LED to check them all. L changes
  the power limit, which starts at 25%. If the strip flickers or shows the wrong colors, T tries
  the hub's other LED timing.
- **2 relays:** 1 and 2 switch them. Both open when you leave.
- **I/O hub:** what's on each port, and its firmware.
- **NFC:** Enter opens NFC Notes, which is the full test for that unit.
- **LoRa Cap GPS:** satellites, position, speed, heading and distance from your spot, with the Cap
  fitted. S saves where you are as the spot. M opens Mesh.
- **Cardputer LED and Speaker:** the built-in ones. They work with nothing plugged in.
- **smartLamp and IR for AC:** Enter opens their own apps.

If a unit stops answering, its screen shows what else still answers, which tells you whether to
look at that unit's cable or at the Grove port. Units reconnect by themselves.

The strip and the servo run off the Cardputer's Grove power, and M5 publishes no limit for it. At
full white 30 LEDs can draw 1 A and a stalled servo 750 mA, so keep the limit low. If the
Cardputer restarts, give the strip its own 5 V supply and join the grounds.

### Rules

Rules lists what's saved. A solid dot means the rule is on, Space turns it off or on, P plays the
ones that are on, and pressing X twice deletes one. "Wave lights" is there to start with: when the
Mini Angle turns or something comes within 30 cm, the strip's color follows the distance and the
servo follows the Mini Angle.

**+ New rule** asks one question a screen, and Esc goes back a step:

1. What to watch, and when it counts. To set a distance, put your hand there and press Enter, or
   nudge the number with left and right. Space goes back to the live reading.
2. Whether to watch something else, up to 3. With more than one, whether any or all of them must
   be true.
3. What to do, up to 3 things. An output can be **set** to a value, can **follow** a sensor so it
   moves with it, or can **do the opposite**.
4. How long it keeps going afterwards, and a name.

Nothing is saved until the last Enter. Enter on a saved rule walks the same steps with your answers
already picked, so change what you want and press Enter through the rest. Choosing "That's all"
drops any lines after that point.

**Recipes** are ten ready-made rules for these parts. Enter adds one to your list, and it's then a
rule like any other.

**Say a rule** lets you tell Claude what you want, for example "when I wave my hand, turn the strip
red". Tap Alt to listen and tap it again to send, the same as in the chat, and Esc cancels. Claude's
rule comes back to look at before anything happens: Enter saves it, E walks through it in the steps,
and Esc drops it. If it can't make a rule, the screen says what it needs and what it heard. This stays
out of the chat, though a rule that's asked for in chat arrives the same way. It needs Wi-Fi and the
bridge.

The chat knows every rule and sequence you've saved, so on Pulse or in AI Chat you can ask to change
a rule, or to write or change a sequence. A sequence comes back to look at the same way: Enter saves
it, E changes its steps first, and Esc drops it. Tap Alt on the Sequences list to do the same there.
See [From the chat](LAB.md#from-the-chat) and [Write a sequence](LAB.md#write-a-sequence).

A rule can **talk**. Say or ask for something like "when someone walks in, say welcome back". The
phrase is spoken once each time the rule starts firing, and up to 40 letters fit, once in a rule.
Its speech is made when you save the rule and kept in `/lab/say` on the SD card, so it plays with no
network. Sensors and rules wait while it speaks, and anything already on stays on. Without a card a
rule can't speak. The steps can keep or remove a phrase, but only speech can add one.

**Spoken commands** work while rules play. Ask for something like "when I say movie time, turn the
strip blue", and you get a rule called Movie time that listens for its name. On the Playing screen,
tap Alt and say the name to switch it on, and say it again to switch it off. Rules wait while it
listens, and what's on stays on. Only rules that listen for their name can be called this way, and
Practice can stand in for saying it. Stopping the rules forgets what was said.

**Change a rule** by talking to it. Pick a rule in the list, tap Alt and say what should be different,
for example "make it stay on longer" or "only when it's dark". Claude sees the rule as it is now and
sends it back. What changed stands out and the rest is grey. Enter replaces the old rule, E walks
through it in the steps, and Esc keeps the rule you had.

**Ask the Lab** is at the bottom of the rules list, and tapping Alt on Devices, on a part's own screen,
on the wiring screen or on Sequences asks it too. Claude sees what's plugged in, the live readings and your rules.
Try "why didn't my rule fire?", "why won't the strip light up?" or "what can I make with these parts?".
The answer shows and is read aloud. If it suggests rules you get up to three: left and right move
between them, Enter saves the one showing, and Esc drops them all.

**Check something once** from the chat, on any screen, with nothing saved: "what's the knob at?" or
"if the knob is over 50, turn the strip red". The Cardputer does the reading and adds the result to the
chat. It can reach the strip, its LED, the servo and the speaker, but never relays, the lamp or an
air conditioner. See [the Lab guide](LAB.md#check-something-once).

The Lab keeps a **diary** while rules play: one line an hour with how many times motion was seen, how
many times rules fired, and each sensor's lowest and highest reading. So you can leave rules playing
overnight and ask "was there motion last night?" or "how hot did it get?". It's in
`/lab/diary.txt` on the SD card, and about the last day goes to Claude when you Ask the Lab, and at no
other time. Nothing is recorded while rules aren't playing, or if the clock hasn't been set. Delete
the file to clear it.

**Say it** on the Servo, LED strip, Cardputer LED and Speaker screens, so your hands stay on the
parts. Tap Alt and say "set it to 90", "make it blue and a bit brighter" or "a bit more". A spoken
setting only moves the part whose screen is open. Relays, the lamp and the air conditioner keep to
their keys, because speech can be misheard and they can switch mains.

Rules are kept on the SD card in `/lab/rules.dat`, with the hub wiring, so they survive a firmware
install. With no card they last until the next restart.

### Sequences

A sequence is a list of steps that run in order, and a rule can start one. **Hello** is there to try,
and it only needs the Cardputer's own LED and speaker. Up to 8 can be saved, with up to 16 steps each.

| Key | In the list |
| --- | --- |
| Up, down | Pick one |
| Enter | Change it, or start a new one |
| P | Play it |
| X twice | Delete it. It's also taken out of any rule that plays it |

| Key | In its steps |
| --- | --- |
| Up, down | Pick a row |
| Enter | Change a step, add one, switch between once and over and over, or name it |
| Left, right | Move a step earlier or later |
| A | Add a step after this one |
| X twice | Delete a step |
| P | Save and play |
| Esc | Save and go back |

While one plays, P starts it again and Esc stops it. A step can wait for up to 10 minutes, set any
output a rule can, except speech, or turn one off. Turning any part of the strip off turns off the
whole strip.

To start one from a rule, choose **Sequence** under What should it do. It starts once, each time the
rule starts firing. If it plays once it finishes even if the rule stops, and if it plays over and
over it stops with the rule. Only one plays at a time, and a new one replaces it. It wins any output
it has set while rules keep the rest, and whatever it turned on goes off when it ends. Opening the
menu, pausing or leaving the Lab stops it. Sequences are kept in `/lab/seqs.dat`.

### Playing

Rules only run on the Playing screen, because the Grove port can't be shared with other apps. The
screen stays on while they play.

It shows one rule as a picture: what it's watching, what it's driving, and dots moving between them
while it fires. Left and right change rule. The dots at the bottom right stand for every rule, so
you can see one firing on another page.

**T** starts Practice, for trying a rule with nothing plugged in. Up and down change the highlighted
sensor, left and right pick another, and Esc ends it and goes back to the real readings. Real
outputs still move if they're connected.

Space pauses, and Esc stops. Either one turns everything off, and so does opening the menu.

When two rules want the same output, the lower one in the list wins. A relay can switch at most
about three times a second, however much a sensor flickers.

The smartLamp and the air conditioner are left alone until a rule asks. When that rule ends, or you
stop, an "on" that it sent is undone. A rule can turn an AC on or off at most every 3 seconds, and
only a TCL or Daikin. It sends by infrared, so point the Cardputer at it.

Realtime shows CONNECT, LISTEN, THINK, SPEAK, READY or ERROR above the large eyes.
MIC ON and the input meter appear only during recording. Connection errors remain
visible until retry or exit. The firmware uses one configured conversation;
there is no topic browser or topic creation/switching.

Device Settings → Clock format switches between 12h AM/PM (default) and 24h.
Device Settings → Mesh region picks the Meshtastic region Mesh uses, US (906.875 MHz) unless
changed. Left and Right step through the list in [Mesh](MESH.md#region).
Display Settings → Text size switches chat and voice replies between Small, Medium
and Large.
Ordinary voice keeps the original REC graphics during capture and THINK until the
reply arrives, with the status showing how long the assistant has been working.
A background task stops blocking the device as soon as its receipt arrives. Holding
G0 on Home or the clock opens AI Chat, so your words and the answer appear there as
a conversation: yours on the right, the assistant's on the left. From another app, a
compact card shows the accepted request and recognized text, or the answer. Enter
opens AI Chat; Escape closes the card while the server task continues.
A transport failure shows a delivery warning and retains the original recording
in Voice Notes. Check chat before retrying an uncertain delivery.
Receipts and replies also appear on Clock. Text is painted before the receipt
tone or reply speech starts, and unread cards remain until Enter or Escape.
The clock screen shows READY with its decorative animated bars when the
microphone is off. REC and the live input meter appear after recording starts.
A compact battery gauge with five broad diagonal strokes and a percentage sits
below READY; amber and red indicate low charge. Unknown telemetry displays --%.

While it's plugged in, a bolt appears beside the gauge and the idle bars become a
line of words over a meter:

| Home shows | What it means |
|---|---|
| **Charging**, green, with bars filling in a sweep | The battery is charging. Only charging moves, so a still screen means it isn't |
| **Fully charged**, green, with all the bars lit | The charger has finished. Full is shown once the voltage stops rising, so give it about 15 minutes at the top |
| **Not charging**, amber, with "Power switch on?" | It's plugged in, but the battery isn't taking charge. It runs from USB with the switch off, but the battery only charges with the switch on |
| **On battery**, for a few seconds | You just unplugged it. Home then goes back to the normal idle bars |

While charging, the percent stops at 99 until the battery is full. It reads high on
USB, and it drops a few points when you unplug.

The Cardputer ADV can't be asked whether it's charging: the charger chip's status
pins aren't connected to anything, and no pin senses USB power. It works it out
instead, so it isn't always sure:

- **On a computer** it knows it's plugged in from the USB traffic, within a second or two
- **On a wall charger or power bank** it judges from the battery voltage. That takes a few seconds when you plug in, and can take half an hour or so if it starts up already on the charger
- **With the switch off on a wall charger**, it can't tell it's plugged in at all

`dbg` and `battery.status` report what it decided as `power=`, and each change is
logged as `[POWER]` with the reading behind it. `battery.status` also opens the
Battery screen.

The interface and default assistant replies are English, including replies to
Russian input. Explicit requests such as "Reply in Russian" are respected.
Russian keyboard input and Cyrillic rendering remain available. See [NES games](NES.md) for SD game installation.

Realtime uses full-screen eyes with compact phase and microphone badges.
Hands-free listening resumes after each spoken reply. Ctrl+R starts or stops
Realtime; Esc ends the session, Tab opens apps and Ctrl+K opens commands.
