# Everyday controls

- Type a question and press Enter in chat.
- Hold **G0**, speak, then release to send. **Ctrl+V** or double **Ctrl** toggles recording.
- **Tab** opens the app menu from Home and the chat/eyes screens; arrows select, Enter opens.
- **Ctrl+K** opens assistant commands. R selects Realtime, C AI Chat, E Chat Eyes, F Big Eyes.
- **Ctrl+U** on the Home clock installs an available update. An orange **NEW UPDATE / Ctrl+U** badge replaces READY until the newer firmware is installed. Without a candidate, Ctrl+U checks for updates.
- **Ctrl+R** starts Realtime or commits the current speech turn. While awaiting a reply, it stops Realtime.
- **Esc** closes an overlay, or ends Realtime and returns Home.
- **Ctrl+Left / Ctrl+Right** or **Ctrl+, / Ctrl+.** selects the previous/next animal.
- On **Big Eyes, Chat Eyes and Realtime**, **[ / ]** also selects the previous/next animal, including while the mic is listening. In Full Chat, plain brackets remain text.
- **Ctrl+L** also opens the app menu. `/help` shows command help.
- `/status` shows connection and device information.
- `/setup` or `/wifi` opens the Wi-Fi and bridge setup portal.
- `/settings` opens device settings; `/ota` opens firmware update review.
- `/calc`, `/alarm`, `/stopwatch`, `/assets` and `/library` open local tools.
- `/nfc` opens NFC Notes, which reads tags and saves a name or note on them.
- `/music` opens Air Music, which plays notes as you wave a hand over an ultrasonic unit.
- `/flappy` opens Flappy Bird, steered by hand over the same unit or flapped with Space.
- `/balloon` opens Hot Air Balloon, flown by blowing on an ENV III and steered with a Joystick 2.

This firmware has evolved across development versions. Prefer the command help
on your built device for current shortcuts. Specialized IR, RFID and sonar
tools require compatible extra hardware; see the retained target documentation.
Voice quality and response time depend on your bridge's STT/TTS configuration
and the agent, network and provider latency.

## NFC Notes

NFC Notes is under Tab, Tools, and `/nfc` opens it. It needs the M5Stack NFC unit
(ST25R3916) on the Grove port.

- **Read:** hold a tag on the unit. Its note, tag type and ID appear.
- **Save:** type a name or note, up to 200 letters, and press Enter. Then hold a tag
  on the unit. The app reads the note back to check it, and says so.
- Esc cancels a save, then clears what you typed, then leaves the app.
- Fn with `;` or `.` scrolls a long note.

## Air Music

Air Music is under Tab, Games, and `/music` opens it. It needs the M5Stack I2C ultrasonic
unit (address 0x57) on the Grove port.

Hold a hand 4 to 44 cm above the sensor. Closer is higher, and there are ten notes, each
about 4 cm apart. Move away to stop. Esc leaves.

## Flappy Bird

Flappy Bird is under Tab, Games, and `/flappy` opens it. Point the ultrasonic unit up and
hold a hand over it: 6 cm is the bottom of the screen and 30 cm is the top, so the bird
follows your hand. Take your hand away and the bird falls.

Space or any other key flaps instead, so it also plays without the sensor. The pipes speed
up every 5 points, up to triple speed at 50 points. After a crash, the score shows for a
moment and the next game is ready to start. Esc leaves.

## Hot Air Balloon

Hot Air Balloon is under Tab, Games, and `/balloon` opens it. Connect an ENV III and a
Joystick 2 to the Grove port through a hub. Both are optional.

- **Rise:** blow on the ENV III, hold the joystick's button, or press Space or W.
- **Steer:** push the joystick left or right, or press A and D or the arrow keys.
- Collect stars, and avoid the birds. You have three hearts, and a floating heart gives
  one back. The ground is soft, so landing is safe.
- Every 10 stars the sky changes from day to sunset to night, and the wind picks up.
- A quiet music box plays while the game is open. It pauses behind the menu, and it stops
  while the assistant is using the mic or speaker.

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
