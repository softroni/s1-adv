# S1-ADV 0.2.228

You can now see what Claude is doing while you wait. AI Chat and the Pulse screen show live
status: Thinking, Searching, Researching, Reading, Coding, Working, Building, Testing, Writing,
Loading and Speaking. Each has its own colour, icon and short sound, and most show a detail like
the search or the file. See the status table in [CONTROLS.md](CONTROLS.md#status-and-sounds).

This needs the bridge restarted on the new code. With an older bridge, or with OpenClaw, the
device keeps working as before and shows Thinking until the reply arrives.

The device now gets its reply through the inbox rather than one long request, so "Claude is
working on it." no longer appears in chat on each message. Short typed replies still aren't read
aloud, and this now also holds for replies that take over a minute.
