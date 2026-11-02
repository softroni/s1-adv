# S1-ADV 0.2.252

In short: Steps that play in order

The Lab has a new row: **Sequences**. A rule decides when something happens, and a sequence decides
what happens, in what order. It's a list of steps with waits between them, like "strip green, wait a
second, servo to 90, wait 2 seconds, servo back, strip off".

- **Try it with no parts:** Hello is already there, and it only needs the Cardputer's own LED and
  speaker
- **Build one** a step at a time. Each step waits, sets an output or turns one off, and steps can be
  moved, changed and added in the middle
- **Play it** once or over and over, and watch which step it's on
- **Start it from a rule:** choose Sequence under What should it do. It starts each time the rule
  starts firing
- **Say a rule** can play a saved sequence, once the bridge is restarted

Only one plays at a time. It wins any output it has set, rules keep the rest, and whatever it turned
on goes off when it ends.
