# Podcasts

Tab, M, Podcasts. Search podcasts, free audiobooks and old-time radio shows, pick an episode,
and listen. The Cardputer remembers where you stopped, and can save episodes to the SD card to
play with no Wi-Fi.

This app needs the bridge. The bridge does the searching, reads each show's list of episodes,
and passes the episode on as a small MP3 that the radio's player can play.

At home, after one HTTPS request that gets a ticket, everything comes straight from the bridge's
computer over your Wi-Fi as plain HTTP: searches, lists and the sound. HTTPS takes memory the
player then can't have, so at home it's kept to that one request. Away from home it all goes
over HTTPS, which may run out of memory while playing, so save episodes to the card before you
go. [How the sound gets there](#how-the-sound-gets-there) has the details.

| What | Where it comes from |
|---|---|
| **Podcasts** | Apple's free iTunes Search API, then each show's own feed |
| **Audiobooks** | [LibriVox](https://librivox.org) recordings, found on the Internet Archive |
| **Old radio** | The Internet Archive's old-time radio collection |

None of them needs an account or a key.

## The library

The app opens on three tabs. `,` and `/` (Left and Right) move between them, and `;` and `.` (Up
and Down) move in the list.

| Tab | What's on it | Enter | Del |
|---|---|---|---|
| **Listening** | Episodes you've started and not finished, the latest first | Carries on from where you stopped | Takes it off the list |
| **Shows** | Shows you follow | Opens its episodes | Unfollows it |
| **Saved** | Episodes on the SD card | Plays it from the card | Deletes it, after you press Y |

On Listening and Saved, **E** opens the show the episode is from. **F** finds something new from
any tab, and **N** goes back to what's playing.

## Finding

Press **F**, type a few words, and press Enter. `,` and `/` choose what to search:
**Podcasts**, **Audiobooks** or **Old radio**. `;` and `.` can be typed, since there's nothing
above or below to move to.

- Podcasts: a show's name or a topic, like `history` or `rest is history`.
- Audiobooks: a title or writer, like `Sherlock` or `Jane Austen`.
- Old radio: a show, like `The Shadow` or `Dragnet`.

You get up to 20 shows. Enter opens one, and **S** follows it so it's on the Shows tab.

Your search words go through the bridge to Apple or the Internet Archive.

## Episodes

A show's episodes come 20 at a time, the newest first for a podcast and in order for a book.

| Key | What it does |
|---|---|
| `;` `.` | Move |
| **Enter** | Play it. On the first row, follow or unfollow the show. On "Next 20" or "Previous 20", load them |
| `,` `/` | The previous or next 20 |
| **S** | Save it to the SD card |
| **Del** | Delete the saved copy, after you press Y |
| **N** | Back to what's playing |

The right-hand column says how long an episode is, how much is left of one you've started, or
**done**. It's green when the episode is on the card.

## Playing

| Key | What it does |
|---|---|
| **Enter** or **Space** | Play or pause |
| `,` or **A** | Back 15 seconds |
| `/` or **D** | Ahead 30 seconds |
| `;` and `.` | The previous or next episode in the list |
| **-** and **+** | Volume, 5% a press. **=** works for + |
| **S** | Save it to the card |
| **E** | This show's episodes |
| **Esc** | Back to the list. It keeps playing, and from the library Esc goes Home with it playing |
| **Ctrl+M** | Pause or carry on, from any screen |

Several presses of `,` or `/` in a row add up, and the episode starts again from there once you
stop pressing. Streaming takes a few seconds to start, and some podcast hosts are slow, so the
screen says **Asking the bridge...** for up to 20 seconds or so.

On the right under the title it says where the sound is coming from: **card**, **home** for your
home Wi-Fi, or **https** through the bridge's public address.

The place is saved every 30 seconds while it plays, and whenever you pause, skip or leave. An
episode counts as **done** within 45 seconds of its end. When a book's chapter or an old radio
episode ends, the next one in the list starts by itself. A podcast stops at the end.

The app has one player, so loading a list or saving an episode pauses what's playing. After a
list loads, it carries on by itself. While something plays, the search's list of shows is let go
to give the player memory, so going back from the episodes opens Find with your words still
typed: Enter searches again.

## Listening from Home

Esc on the library goes Home and the episode keeps playing. It also plays on behind Pomodoro,
Alarm, Stopwatch, Calendar, Calculator and Notes, and the clock and battery screens, the same
screens Internet Radio plays behind. Home shows the episode in its show's colour with headphones,
over bars that follow the sound, where Home's own bars usually are. For the first few seconds it
says **Ctrl+M pauses it**.

**Ctrl+M** pauses it from any screen and keeps the place. Press it again on one of those screens
and it carries on from there, as long as a podcast was the last thing playing; after the radio,
Ctrl+M is the radio's. Open Podcasts while it plays and you're on the player.

The place is still saved every 30 seconds behind other screens, and a book's next chapter still
follows on. If it stops by itself, at the end or because the bridge went quiet, Home says
**Stopped** and why for a few seconds.

Any other screen stops it first and keeps the place: AI Chat and voice, Internet Radio, the Lab,
the games and the rest. So do an alarm going off, voice playback and updates. A save on its way
stops when you leave the app.

## Saving for offline listening

**S** on an episode, or while it plays, saves it to `/podcasts/files/` on the SD card. The screen
shows how far it's got, and **X** stops it. Saving pauses what's playing, and nothing plays while
it saves.

A saved episode plays from the card with no Wi-Fi and no bridge, and jumping back or ahead takes
under a second. Saving goes faster than real time, but it isn't instant: about 6 KB a second of
listening, so an hour is about 21 MB, and the Cardputer has to fetch it over TLS.

- Up to 25 episodes. Delete one to save another.
- The card needs room for the episode and 4 MB to spare. An episode with no length given needs
  100 MB free.
- Deleting removes only the copy on the card. The episode still plays over Wi-Fi.

## What the card keeps

Everything is in `/podcasts`, one record a line with fields separated by tabs. A change is written
to a `.new` file first and then renamed, so a power cut loses at most that change.

| File | One line for each | Kept |
|---|---|---|
| `shows.txt` | Show you follow: its id, episode count, title and author | 25 |
| `progress.txt` | Episode you've played: show, episode, where you stopped, its length, when, and the titles | The latest 20 |
| `saved.txt` | Episode on the card: show, episode, length, size and titles | 25 |
| `files/<episode>.mp3` | A saved episode's audio | 25 |

Show ids are `p` and a number for a podcast, and `a` and an Internet Archive identifier for a book
or an old radio show. An episode's id is 12 letters and digits made from its address.

With no card, the app still finds and plays episodes, but nothing is kept: no shows followed, no
places remembered and nothing saved.

## How the sound gets there

The first time the app goes to the network on a Wi-Fi network, the Cardputer asks the bridge
over HTTPS for a ticket. The answer is a random 32-character ticket and the bridge computer's
addresses on its home networks. If one is on the Cardputer's own network, searches, lists and
audio all come from there, `http://<that address>:31890/podcasts/<ticket>/search?...`,
`/episodes?...` and `/audio?...`, the same requests as over HTTPS. The device token never
travels that way. The ticket is good for 12 hours. That door only answers home-network addresses
(10.x, 172.16-31.x and 192.168.x), and only for tickets it gave out.

If the address doesn't answer, the Cardputer asks that once more over HTTPS and stays on HTTPS
on that network. If the bridge restarted and forgot the ticket, it asks for a new one. Set `"podcasts": { "lanPort": 0 }` in the bridge's config to keep the door
closed; then everything goes over HTTPS.

HTTPS needs two buffers of about 17 KB each. With the decoder that's about 80 KB at once, which
the Cardputer may not have. When something runs short the screen says what and how much there
was, like **No memory: TLS 61K/15K**: 61 KB free, the largest piece 15 KB. Saving over HTTPS needs
much less than playing, so saving first is the way to listen away from home.

The bridge runs `ffmpeg` to turn each episode into one format: MP3, one channel, 32 kHz and
48 kbit/s. Every frame is the same 216 bytes, so the Cardputer can find any second in a saved file
by arithmetic, and the bridge can start a stream from any second. It also means podcasts in AAC or
M4A play, and a stream uses less Wi-Fi than most radio stations.

The bridge needs `ffmpeg` on its computer. On a Mac with Homebrew it's `brew install ffmpeg`.

Episode addresses come from feeds that anyone can write, so the bridge fetches only from public
internet addresses, never from its own computer or network, and lets `ffmpeg` read only from the
network. Nothing about what you search for or play is written to the bridge's log.

What the log does get, to see what went wrong: one line when an episode's sound starts and one
when it ends, saying how it came (home or https), from which second, how much was sent and
whether the Cardputer hung up or the episode ended. When something fails on the Cardputer, it
sends the bridge a short report once the player is free: where it failed, the error, the memory
then and just before it began, how the sound came and how long it played. Neither says what was
playing.

## Not yet

- Streaming away from home when memory is short: save it at home first.
- Listening behind screens that need the network, the speaker or the memory, like AI Chat.
- Chapters, show notes and artwork.
- Choosing the order of a podcast's episodes.
- Saving while something plays: the Cardputer has room for one TLS connection at a time.
