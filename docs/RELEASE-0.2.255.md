# S1-ADV 0.2.255

In short: The Lab keeps its connection to the bridge

Saying a rule could fail with "Couldn't reach the bridge" when the bridge was fine. After a few
recordings the device's memory was left in pieces too small for a secure connection, even with plenty
free overall, and from then on everything that needed the bridge failed. The device's own log was what
broke it up: each line was kept in its own small piece of memory. Those lines now live in fixed memory.

Two more things were doing the same. The speaker came back from a recording while the connection was
still open, and landed in the middle of the memory it was about to free. And every recording was also
held in memory for Voice Notes, up to 48 of them, chat ones included. That list now clears when the
Lab opens and reads the card when Voice Notes opens. Your recordings stay on the card as before.

If memory does run short, the device no longer treats it as a network fault and drops Wi-Fi.

This also fixes a crash. Pressing Alt or G0 just as a background check started could restart the device.

G0 and Ctrl+V no longer record on the Lab's other screens, such as an offer waiting for Enter. A
recording there went to the chat as a message, and uploading it could use up so much memory that the
device lost its network until it restarted.
