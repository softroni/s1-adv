# The Lab

**Plug in a part, watch it respond, then make parts work together. No code.**

The Lab turns your Cardputer into a test bench. **Devices** shows everything it can reach, live. **Rules** joins sensors to lights, motors and switches with "if this, then that", built one question at a time on the little screen.

<table>
<tr>
<td width="33%"><img src="assets/lab/devices.png" alt="The Devices list with live readings from five parts" width="100%"><br><sub><b>1. Test a part.</b> See that it works before you build on it.</sub></td>
<td width="33%"><img src="assets/lab/step-show.png" alt="A rule step where a slider sets a distance of 50 cm" width="100%"><br><sub><b>2. Build a rule.</b> Answer a few questions, or show it with your hand.</sub></td>
<td width="33%"><img src="assets/lab/flow-firing.png" alt="A rule firing, with a knob driving an LED strip and a servo" width="100%"><br><sub><b>3. Play it.</b> Watch the signal travel from sensor to output.</sub></td>
</tr>
</table>

To open it, press **Tab**, then **L**. You can also type `/lab` or `/rules` in chat.

On the Cardputer, the arrow keys are `;` up, `.` down, `,` left and `/` right, and Esc is the top-left key.

[Try it with no parts](#try-it-with-no-parts) · [Connect a part](#connect-a-part) · [Test a part](#test-a-part) · [Build a rule](#build-a-rule) · [Play it](#play-it) · [Shortcuts](#shortcuts) · [If a part doesn't answer](#if-a-part-doesnt-answer) · [Good to know](#good-to-know)

> The Lab needs firmware built from source, as in step 1 of the [README](../README.md#set-it-up). The older one-command flash doesn't have it.

## Try it with no parts

You don't need to buy anything to see how a rule works.

1. Press **Tab**, **L**, then open **Rules**.
2. **Wave lights** is already there. Press **P** to play it.
3. Press **T** to start Practice, then press up and down to fake the sensor.

The boxes turn green, the dots start moving, and the rule fires. Press Esc to end Practice.

Devices also has three things built into the Cardputer: **Tilt**, **Cardputer LED** and **Speaker**.

## Connect a part

| Part | What it gives the Lab | How it connects |
| --- | --- | --- |
| Ultrasonic | Distance | I2C |
| ENV III | Temperature and humidity | I2C |
| Joystick 2 | Stick left-right, stick up-down, and its button | I2C |
| NFC Universal | A shortcut to NFC Notes | I2C |
| Mini Angle | A knob, from 0 to 100% | White wire |
| Light | Brightness, from 0 to 100% | White wire |
| PIR | Motion | White wire |
| Servo | An angle you can set | Yellow wire |
| LED strip | Color, brightness and how many LEDs are lit | Yellow wire |
| 2 relays | Two switches | Both wires |
| Cap LoRa-1262 | GPS position, speed and satellites | Fits on the Cardputer, not on Grove |

The Lab can also reach a smartLamp over Wi-Fi, and a TCL or Daikin air conditioner by infrared.

### Why the wire matters

The Grove port has two signal wires. **I2C** units share both and take turns, each with its own address. The simple parts need a wire to themselves. That's why the two kinds can't share the port, even through a hub: a Mini Angle on the same hub stops an I2C unit answering at all.

### Pick a setup

| What you want | What to plug in |
| --- | --- |
| One part | Straight into the Grove port. Any part works |
| A few I2C units | A 1-to-3 hub on the Grove port |
| One sensor driving one output, like a Mini Angle moving a servo | A 1-to-3 hub on the Grove port, with no I/O hub: one white-wire part and one yellow-wire part |
| Lots of simple parts | An I/O hub on the Grove port. Any part can go on any of its six ports |
| Simple parts and I2C units together | A 1-to-3 hub first, then the I/O hub in one socket and up to two I2C units in the others |

You can change your setup whenever you like, even with a part's screen open. The Lab notices the I/O hub or an I2C unit arriving or leaving, and the part follows. Rules work the same however the parts are connected.

<table>
<tr>
<td width="33%"><img src="assets/lab/devices-grove.png" alt="The Devices list with no hub, where simple parts say press ENTER" width="100%"><br><sub><b>Straight in.</b> Simple parts can't answer, so they say <i>press ENTER</i>. Opening one tells the Lab it's plugged in, and it remembers.</sub></td>
<td width="33%"><img src="assets/lab/devices-hub.png" alt="The Devices list with an I/O hub, where simple parts show their ports" width="100%"><br><sub><b>On the I/O hub.</b> Each part shows its port until you open it.</sub></td>
<td width="33%"><img src="assets/lab/pick-port.png" alt="A list of six ports asking which one the Mini Angle is on" width="100%"><br><sub><b>Wrong port?</b> Open the part, press <b>P</b> and pick the port. It swaps with whatever was there.</sub></td>
</tr>
</table>

To see all six ports at once, press **W** in Devices.

<img src="assets/lab/hub-wiring.png" alt="The Hub wiring screen listing six ports and their parts" width="360">

This is the starting layout. The I/O hub can't tell what's on a port, or whether anything is, so these need to match your cables. In this screen, up and down pick a port, and left and right change it.

## Test a part

Up and down pick a device, and Enter opens it. Esc goes back, and everything turns off when you leave a test screen.

The dot beside each device tells you what the Lab knows:

| Dot | Meaning |
| --- | --- |
| Solid | It's there |
| Cyan ring | It can be reached, but it can't confirm it's there. Outputs and I/O hub parts look like this |
| Grey ring | Not found yet. That's fine if you haven't plugged it in |

<table>
<tr>
<td width="33%"><img src="assets/lab/ultrasonic.png" alt="Ultrasonic screen showing 41 cm and a chart of the last 30 seconds" width="100%"><br><sub><b>Ultrasonic.</b> Live distance and the last 30 seconds. R resets the lowest and highest.</sub></td>
<td width="33%"><img src="assets/lab/env.png" alt="ENV III screen showing temperature and humidity" width="100%"><br><sub><b>ENV III.</b> Breathe on it and watch the humidity jump.</sub></td>
<td width="33%"><img src="assets/lab/joystick.png" alt="Joystick 2 screen showing the stick position and a pressed button" width="100%"><br><sub><b>Joystick 2.</b> The stick's position and its button.</sub></td>
</tr>
<tr>
<td><img src="assets/lab/knob.png" alt="Mini Angle screen with a dial at 67%" width="100%"><br><sub><b>Mini Angle.</b> Turn it end to end once so it learns its range.</sub></td>
<td><img src="assets/lab/light.png" alt="Light screen showing 50%" width="100%"><br><sub><b>Light.</b> Cover it to test it. If it reads backwards, F flips it.</sub></td>
<td><img src="assets/lab/motion.png" alt="PIR screen showing Motion" width="100%"><br><sub><b>PIR.</b> Wave at it. It stays on for about 2 seconds after you stop.</sub></td>
</tr>
<tr>
<td><img src="assets/lab/servo.png" alt="Servo screen with a gauge at 112 degrees" width="100%"><br><sub><b>Servo.</b> Left and right turn it 5°, A and D turn it 1°, C centers it and S sweeps it.</sub></td>
<td><img src="assets/lab/strip.png" alt="LED strip screen in rainbow mode with a power limit" width="100%"><br><sub><b>LED strip.</b> M changes the pattern, L changes the power limit, and T tries other timing if it flickers.</sub></td>
<td><img src="assets/lab/relays.png" alt="Relay screen with both relays on" width="100%"><br><sub><b>2 relays.</b> Keys 1 and 2 switch them. Listen for the click.</sub></td>
</tr>
<tr>
<td><img src="assets/lab/hub.png" alt="I/O hub screen listing what's on each port" width="100%"><br><sub><b>I/O hub.</b> What's on each port, and its firmware.</sub></td>
<td><img src="assets/lab/tilt.png" alt="Tilt screen showing 30 degrees" width="100%"><br><sub><b>Tilt.</b> Built in. Tip the Cardputer to see it move.</sub></td>
<td><img src="assets/lab/gps.png" alt="GPS screen with satellites, speed, latitude and longitude" width="100%"><br><sub><b>LoRa Cap GPS.</b> Satellites, position and speed. M opens Mesh.</sub></td>
</tr>
</table>

The label at the top right tells you how much the Lab really knows. **ANSWERING** means the part replied just now. **ON CH0** or **ON GROVE** says where it is. **SIGNAL SENT** means a command went out, but an output can't reply, so watch the part itself.

## Build a rule

A rule has three pieces: what to **watch**, what to **do**, and how long to **keep going** afterwards. Open **Rules** and choose **+ New rule**. It asks one question a screen, and Esc goes back a step.

<table>
<tr>
<td width="33%"><img src="assets/lab/step-watch.png" alt="What should it watch, with Distance selected" width="100%"><br><sub><b>1. What to watch.</b> Each sensor shows its live reading.</sub></td>
<td width="33%"><img src="assets/lab/step-when.png" alt="Distance: goes under, goes over, changes at all" width="100%"><br><sub><b>2. When it counts.</b> Under a value, over it, or any change.</sub></td>
<td width="33%"><img src="assets/lab/step-show.png" alt="Show me: Distance, set to 50 cm" width="100%"><br><sub><b>3. Show it.</b> Put your hand where you want it and press Enter, or nudge the number. Space goes back to the live reading.</sub></td>
</tr>
<tr>
<td><img src="assets/lab/step-more.png" alt="Anything else to watch, with That's all selected" width="100%"><br><sub><b>4. Anything else?</b> Watch up to 3 things, then choose whether any or all must be true.</sub></td>
<td><img src="assets/lab/step-do.png" alt="What should it do, with Servo selected" width="100%"><br><sub><b>5. What to do.</b> Pick an output. A rule can do up to 3 things.</sub></td>
<td><img src="assets/lab/step-how.png" alt="Set to a value, follow a sensor, or do the opposite" width="100%"><br><sub><b>6. How.</b> See <a href="#set-follow-or-do-the-opposite">the three choices</a> below.</sub></td>
</tr>
<tr>
<td><img src="assets/lab/step-value.png" alt="Set Built-in LED to 120, with a green color bar" width="100%"><br><sub><b>7. The value.</b> The bar shows the LED's color as you change it.</sub></td>
<td><img src="assets/lab/step-hold.png" alt="After it stops being true: 2 seconds" width="100%"><br><sub><b>8. Keep going.</b> How long it carries on once the condition stops being true.</sub></td>
<td><img src="assets/lab/step-name.png" alt="Give it a name, with Close and turn typed in" width="100%"><br><sub><b>9. Name it.</b> Enter saves it. Nothing is saved before this.</sub></td>
</tr>
</table>

To change a saved rule, press Enter on it. It walks the same steps with your answers already picked, so change what you want and press Enter through the rest.

### Set, follow or do the opposite

| Choice | What it does | Try it with |
| --- | --- | --- |
| **Set to a value** | Goes to one value while the rule fires | Turn the strip red when someone walks in |
| **Follow a sensor** | Moves along with a sensor | The servo turns as you turn the Mini Angle |
| **Do the opposite** | Moves against a sensor | More LEDs light up as your hand gets closer |

Relays, lamp power and the air conditioners are simply on or off, so they can only be set.

### What a rule can use

| Watch | Do |
| --- | --- |
| Distance, Temperature, Humidity | Servo |
| Knob, Light, Motion | Strip color, Strip bright, Strip length |
| Stick X, Stick Y, Stick button | Relay 1, Relay 2 |
| Tilt, Satellites | Built-in LED, Speaker |
| | Lamp power, Lamp bright |
| | TCL AC, Daikin AC |

You can save up to 8 rules.

## Play it

<table>
<tr>
<td width="50%"><img src="assets/lab/rules.png" alt="The Rules list with two saved rules" width="100%"><br><sub><b>Rules.</b> A solid dot means the rule is on. Space turns it off or on, <b>P</b> plays the ones that are on, and pressing X twice deletes one.</sub></td>
<td width="50%"><img src="assets/lab/flow-waiting.png" alt="A rule waiting, with grey wires and no outputs running" width="100%"><br><sub><b>Waiting.</b> Sensors on the left, outputs on the right. The middle says whether <b>any</b> or <b>all</b> must be true. A single condition shows <b>IF</b>.</sub></td>
</tr>
<tr>
<td><img src="assets/lab/flow-firing.png" alt="The same rule firing, with green wires and a lit strip color" width="100%"><br><sub><b>Firing.</b> The true condition turns green, dots run along the wires, and the outputs show what they're doing.</sub></td>
<td><img src="assets/lab/flow-paused.png" alt="A paused rule" width="100%"><br><sub><b>Paused.</b> Space pauses, and everything turns off until you press it again.</sub></td>
</tr>
</table>

| Key | While playing |
| --- | --- |
| Left, right | Change rule. The dots at the bottom right stand for every rule, so you can see one firing on another page |
| **T** | Start Practice |
| Space | Pause, or carry on |
| Esc | Stop. Everything turns off, and opening the menu does the same |

Rules only run on this screen, because the Grove port can't be shared with other apps. The screen stays on while they play.

### Practice

<img src="assets/lab/practice.png" alt="Practice mode, with the knob set by hand to 55% and the rule firing" width="360">

Practice lets you try a rule with fake readings. Up and down change the highlighted sensor, left and right pick another, and Esc goes back to the real readings. Real outputs still move if they're connected, so it's also a good way to test a wiring job.

## Shortcuts

### Recipes

Open **Recipes** from the Rules list. Enter adds one, and it's then a rule like any other, so you can change it.

<img src="assets/lab/recipes.png" alt="The Recipes list with Knob to servo selected" width="360">

| Recipe | What it does | Parts |
| --- | --- | --- |
| Knob to servo | Turn the knob and the servo follows | Mini Angle, servo |
| Stick steers | Push the joystick to aim the servo | Joystick 2, servo |
| Distance bar | Closer lights more of the strip | Ultrasonic, LED strip |
| **Air theremin** | Move your hand to change the pitch | Ultrasonic |
| Night light | Warm strip when the room goes dark | Light, LED strip |
| Motion alarm | Someone moves: red strip and a beep | PIR, LED strip |
| Fan for damp | Humid air closes relay 1 | ENV III, relays |
| Tilt to servo | Tip the Cardputer to turn the servo | Servo |
| Dark lamp | Dark room turns the smartLamp on | Light, smartLamp |
| Hot room | Over 28 °C turns the TCL AC on | ENV III, TCL air conditioner |

**Air theremin** is the cheapest place to start, because it needs one sensor and uses the Cardputer's own speaker.

### Say a rule

With [Claude connected](../README.md#3-connect-claude), you can just describe what you want.

<table>
<tr>
<td width="33%"><img src="assets/lab/say.png" alt="Say a rule, with an example sentence" width="100%"><br><sub><b>1.</b> Open <b>Say a rule</b> and tap Alt.</sub></td>
<td width="33%"><img src="assets/lab/say-listening.png" alt="Say a rule, listening" width="100%"><br><sub><b>2.</b> Say what to watch and what to do, then tap Alt. Esc cancels.</sub></td>
<td width="33%"><img src="assets/lab/say-offer.png" alt="Claude wrote this, showing the rule it built" width="100%"><br><sub><b>3.</b> Check it. Enter saves it, E walks through it in the steps, and Esc drops it.</sub></td>
</tr>
</table>

Nothing happens until you've looked at it. If Claude can't make a rule from what it heard, the screen says what it needs. A rule you ask for in chat arrives the same way.

To change a rule, pick it in the list and tap Alt, then say what should be different, like "make it stay on longer" or "only when it's dark". You see what changed before anything is saved.

### Ask the Lab

Stuck? Tap Alt on Devices or on a part's screen and just ask. Claude can see what's plugged in, the live readings and your rules.

- "Why didn't my rule fire?"
- "Why won't the strip light up?"
- "What can I make with these parts?"

The answer shows on the screen and is read aloud. When it suggests rules, you get up to three to choose from.

## If a part doesn't answer

The Lab tells you where to look, and units reconnect by themselves.

<table>
<tr>
<td width="33%"><img src="assets/lab/missing.png" alt="Nothing at 0x57, with three other units still answering" width="100%"><br><sub><b>Others still answer.</b> The port is fine, so check that part's cable.</sub></td>
<td width="33%"><img src="assets/lab/wrong-port.png" alt="Nothing at 0x57, saying I/O hub ports can't carry this unit" width="100%"><br><sub><b>On an I/O hub port.</b> I2C units can't go there. Use the 1-to-3 hub.</sub></td>
<td width="33%"><img src="assets/lab/wires-busy.png" alt="I/O hub not found, with the Ultrasonic still answering" width="100%"><br><sub><b>Sharing the wires.</b> Plug in the I/O hub, or unplug the I2C unit.</sub></td>
</tr>
</table>

| What you see | What to try |
| --- | --- |
| Nothing answers at all | Check the cable into the Grove port, then try one part by itself |
| A part on the I/O hub doesn't respond | Open it, press **P** and pick the port it's really in |
| The Light reads backwards | Press **F** on its screen |
| The strip flickers or shows the wrong colors | Press **T** on its screen to try other timing |
| The Cardputer restarts when the strip or servo moves | It's probably a power problem. See below |

To see what the Lab finds as it happens, connect USB and run `python scripts/cardputer.py monitor`. Its messages start with `[LAB]`.

## Good to know

- **Watch the power.** The strip and the servo run off the Cardputer's Grove power, and M5Stack doesn't publish a limit for it. At full white, 30 LEDs can draw 1 A, and a stalled servo 750 mA. The strip's limit starts at 25%, so keep it low. If the Cardputer restarts, give the strip its own 5 V supply and join the grounds.
- **Rules live on the SD card**, in `/lab/rules.dat`, along with your hub wiring. They survive a firmware install. With no card they last until the next restart.
- **When two rules want the same output**, the lower one in the list wins.
- **A relay can switch about three times a second at most**, however much a sensor flickers.
- **The smartLamp and air conditioner are left alone until a rule asks.** When that rule ends or you stop, an "on" that it sent is undone. An air conditioner changes at most every 3 seconds. It sends by infrared, so point the Cardputer at it.
- **The Lab is new.** It's still being tuned on real parts, and some fixes have only run against simulated ones, so a part may behave differently from these pictures. If it does, the USB log above is the best place to start.

<sub>These are real screens: the firmware's own drawing code, run on a computer against simulated parts. They're shown three times their actual size.</sub>

For every key in one place, see [Everyday controls](CONTROLS.md#lab). Back to the [README](../README.md).
