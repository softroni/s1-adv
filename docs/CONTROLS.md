# Everyday controls

- Type a question and press Enter in chat.
- Hold **G0**, speak, then release to send. **Ctrl+V** or double **Ctrl** toggles recording.
- **Tab** opens the app menu from Home and the chat/eyes screens. Up/down or W/S picks a row, left/right or A/D switches group, and Enter opens. **1-8** jumps to a group, and **Ctrl+1-8** opens that row.
- **Ctrl+K** opens assistant commands. R selects Realtime, C AI Chat, E Chat Eyes, F Big Eyes.
- **Ctrl+U** on the Home clock installs an available update. An orange **NEW UPDATE / Ctrl+U** badge replaces READY until the newer firmware is installed. Without a candidate, Ctrl+U checks for updates.
- **Ctrl+R** starts Realtime or commits the current speech turn. While awaiting a reply, it stops Realtime.
- **Esc** closes an overlay, or ends Realtime and returns Home.
- **Ctrl+Left / Ctrl+Right** or **Ctrl+, / Ctrl+.** selects the previous/next animal.
- On **Big Eyes, Chat Eyes and Realtime**, **[ / ]** also selects the previous/next animal, including while the mic is listening. In Full Chat, plain brackets remain text.
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

This firmware has evolved across development versions. Prefer the command help
on your built device for current shortcuts. Specialized IR, RFID and sonar
tools require compatible extra hardware; see the retained target documentation.
Voice quality and response time depend on your bridge's STT/TTS configuration
and the agent, network and provider latency.

## Wi-Fi

To change network, press Tab, open Settings, then Wi-Fi, or type `/wifi`.

- **Pick a network:** saved networks come first, most recently used at the top, then what's
  nearby, strongest first. `;` and `.` move, and Enter picks. R looks again. Signal bars show
  what's in range, and only 2.4 GHz networks appear.
- **Switch:** Enter on a saved network joins it with no password.
- **Forget:** F on a saved network, then Enter. Its password is deleted. If it's the network
  you're on, the device moves to the next saved one.
- **Type a name:** Other network, at the end of the list, is for a hidden network. Capitals
  count. Leave the password blank for an open network.
- **Password:** type it and press Enter. Fn+`;` shows or hides it. Fn+`` ` `` goes back,
  because `` ` `` and `~` are typed as part of the password here.

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

If the setup screen appears because the old network is gone, press Enter on it to do all of
this on the device. It restarts once it has joined.

## Status and sounds

The Home screen shows what the assistant is doing, and a soft sound marks most stages.

| Status | Means | Sound |
| --- | --- | --- |
| SENDING | Your message is going out | Two rising notes, for typed messages. A voice message is silent here, because the speaker is off while it uploads |
| THINKING | The assistant has your message | Two low taps |
| LOADING | The reply's voice is downloading | The reply sound plays just before this: two falling notes |
| SPEAK | The assistant is talking | The speech itself |

The bridge doesn't tell the device whether Claude is searching or writing code, so those all
show as THINKING.

## NFC Notes

NFC Notes is under Tab, Tools, and `/nfc` opens it. It needs the M5Stack NFC unit
(ST25R3916) on the Grove port.

- **Read:** hold a tag on the unit. Its note, tag type and ID appear.
- **Save:** type a name or note, up to 200 letters, and press Enter. Then hold a tag
  on the unit. The app reads the note back to check it, and says so.
- Esc cancels a save, then clears what you typed, then leaves the app.
- Fn with `;` or `.` scrolls a long note.

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

Realtime shows CONNECT, LISTEN, THINK, SPEAK, READY or ERROR above the large eyes.
MIC ON and the input meter appear only during recording. Connection errors remain
visible until retry or exit. The firmware uses one configured conversation;
there is no topic browser or topic creation/switching.

Device Settings → Clock format switches between 12h AM/PM (default) and 24h.
Display Settings → Text size switches chat and voice replies between Small, Medium
and Large.
Ordinary voice keeps the original REC graphics during capture and THINK until the
reply arrives, with the status line counting how long the assistant has been working.
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

The interface and default assistant replies are English, including replies to
Russian input. Explicit requests such as "Reply in Russian" are respected.
Russian keyboard input and Cyrillic rendering remain available. See [NES games](NES.md) for SD game installation.

Realtime uses full-screen eyes with compact phase and microphone badges.
Hands-free listening resumes after each spoken reply. Ctrl+R starts or stops
Realtime; Esc ends the session, Tab opens apps and Ctrl+K opens commands.
