# S1-ADV 0.2.256

In short: Chat can change the Lab

The chat on Pulse and in AI Chat now knows every rule and sequence you've saved. Ask what you have,
ask to change a rule by name, or describe a sequence and Claude writes the steps. Sequences were
something only you could build until now.

Whatever Claude writes opens in the Lab for you to check, and nothing is saved until you press Enter.
A new screen shows a sequence's steps, with the same keys as a rule: Enter saves it, E changes its
steps first, and Esc drops it. On a change, what stayed the same is grey. If you were typing when an
offer arrives, it waits for the keys to rest for a second, so your next message can't press Enter on it.

The chat can't start, stop or delete anything, because rules and sequences can switch mains. It also
can't see what's plugged in or any readings, because the sensors stop when the Lab closes. Ask the Lab
still does that from inside the Lab.

Each message now carries a short list of what's saved. It's built when you send and thrown away after,
so it holds no memory between messages, and the rules and sequences are read off the card at startup,
so sending never waits for it.

A long reply could lose the rule at the end of it, because replies were cut to 1,024 characters on the
way to the device and the rule was in the part that went. The words are cut now, never the rule.

A rule from the chat that plays a sequence used to be refused until the Lab had been opened once,
because nothing had read the card yet. If a rule or sequence from Claude can't be used, the screen now
says so.

Pressing E on a change to a switched off rule, then finishing the steps, used to switch it back on. It
now stays off.

This needs the bridge restarted. On an older bridge the chat works as before.
