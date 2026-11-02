# Voice tools

Any screen on the Cardputer can use the same recorder as the chat. What you say goes to that screen
and not into the conversation, and it comes back as text, or as Claude's answer.

**Say a rule** in the Lab is the first one, and it's a good one to read: it's about ten lines.

## What a tool gets

While a tool's screen is showing, all of this works without any code in the tool:

- Tap **Alt** to listen and tap it again to send. Holding **G0**, **Ctrl+V** and **Enter** work too.
- **Esc** cancels and stays on the screen. If the recording has already gone, whatever comes back
  is ignored.
- Nothing joins the chat. There's no chat message, no history and no spoken reply, and Claude's
  answer can arrive while a chat reply is still being worked on.
- The recording is kept in Voice Notes, like one made in the chat.

## Add a tool that wants the words

This needs one line in `kVoiceTools`, at the top of
[`083_main.cpp.inc`](../bots/CardputerADV/src/main_parts/083_main.cpp.inc). `transcribe` is built
into the bridge, so nothing changes there.

```cpp
{"transcribe", notesOnScreen, notesHeard, nullptr},
```

The two functions go in your tool's own part:

```cpp
bool notesOnScreen() { return gUiMode == UiMode::Notes; }

void notesHeard(const VoiceToolAnswer& answer) {
  if (answer.ok) appendToNote(answer.heard);
  else setStatus(answer.problem, 2000);
}
```

## Add a tool that asks Claude

Add the same line with a name of your own, then give that name an entry in `VOICE_TOOLS` in
[`bridge/src/voice-tools.mjs`](../bridge/src/voice-tools.mjs), and restart the bridge.

```js
["notes.tidy", {
  instructions: "Tidy these spoken notes into a short list.",
  actions: []
}]
```

`instructions` tells Claude what the tool is for. `actions` lists the device actions it can send.
Anything else it tries to send is dropped.

## Tell Claude more than the words

The last entry on a tool's line can be a function that returns what Claude should know, such as what
a screen is showing. It runs when recording starts, and travels with the upload as the
`X-Voice-Context` header. Claude gets it marked as data, not instructions, and up to 4,000 characters
are used.

```cpp
{"notes.tidy", notesOnScreen, notesHeard, notesContext},

String notesContext() { return "the note so far: " + gNoteText; }
```

The Lab sends what's plugged in, the readings and the saved rules, in `labVoiceContext()`.

## What comes back

| Field | Meaning |
| --- | --- |
| `ok` | False if there's no answer, and `problem` says why |
| `heard` | The words, as the bridge heard them |
| `reply` | What Claude said. Empty for `transcribe` |
| `actionsJson` | Device actions from Claude, ready for `executeDeviceActionsJson` |
| `problem` | A short reason, fit for the screen |

The callback runs even if the person has since moved to another screen, unless they pressed Esc on
the tool's screen.

To show what's happening, draw from `voiceToolStarting()`, `voiceToolListening()` and
`voiceToolWorking()`. If a key of
your own should start the recorder, have it call `toggleVoiceByTap()`.

## How it works

Whatever starts the recorder, the screen showing at that moment decides who it's for. The firmware
adds an `X-Voice-Tool` header to the usual upload, and the bridge answers on that same request.
Tools don't use the chat's inbox, so there's nothing to poll.

Claude is asked with `askModel` at `askEffort`, which default to Haiku at low effort so an answer
takes a few seconds. See [the settings](CLAUDE-CODE.md#settings). Each question stands alone, with
no tools, no saved notes, no project folder and no memory of earlier questions, and Claude Code
doesn't keep a transcript of it.

The device waits up to 65 seconds and the bridge gives up after 50, so this suits quick questions.
Long work belongs in the chat. Asking Claude needs the Claude Code provider. With OpenClaw,
`transcribe` still works, and a rule can still be asked for in the chat.

New firmware on an old bridge sends the recording to the chat instead, because the old bridge
doesn't know the header. The tool then shows "Restart the bridge to use this."
