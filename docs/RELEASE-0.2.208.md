# S1-ADV 0.2.208

The device no longer looks idle while the assistant works on a reply. When a reply takes
longer than the bridge's wait, the label stays on THINKING, the status line counts
`Thinking... 12s`, and the eyes show the thinking face until the reply arrives.

The device now waits for a pending reply as long as the bridge lets Claude work, 30 minutes,
instead of 5.

Chat now looks like a conversation: your messages on the right and the assistant's on the
left, one bubble per message, without `YOU:` and `AI:` labels. Holding G0 on Home or the
clock opens chat, so your words and the reply appear there instead of in a card.
