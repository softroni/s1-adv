# S1-ADV 0.2.247

In short: Ask what happened overnight

The Lab now keeps a diary while rules play: one line an hour with how many times motion was seen, how
many times rules fired, and each sensor's lowest and highest reading.

Leave rules playing overnight, then Ask the Lab "was there motion last night?" or "how hot did it
get?". If the diary has no line for a time, Claude says so.

It's in `/lab/diary.txt` on the SD card, and about the last day goes to Claude when you ask, and at no
other time. Nothing is recorded while rules aren't playing, or if the clock hasn't been set.

**The bridge needs a restart for this.** Until then, it shows "Restart the bridge to use this."
