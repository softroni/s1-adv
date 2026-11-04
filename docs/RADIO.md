# Internet Radio

Tab, M, Internet Radio. It plays MP3 stations over Wi-Fi, with no account and no bridge. Your
stations live in a text file on the SD card, and Find adds new ones from a public directory.

## Playing

| Key | What it does |
|---|---|
| **W / S** or Up / Down | Change genre |
| **A / D** or Left / Right | Change station |
| **Enter** or **Space** | Play or pause |
| **L** | Show one country's stations: L steps through the countries in your list, then back to all |
| **-** and **+** | Volume down and up, 5% a press. The **=** key works for +, so Shift isn't needed |
| **V** | Full-screen visuals, and V again for the next look |
| **F** | Find stations |
| **M** | Move this station to another genre or a new one, change its place in its genre, or remove it |
| **Del** | Remove this station from your list |
| **1** to **9** | Play the station kept on that number. An empty number keeps the station on screen there |
| **Shift** with **1** to **9** | Keep the station on screen on that number instead, or free the number if it's already there |
| **Esc** | Go Home. The radio keeps playing there |
| **Ctrl+M** | Stop the radio from any screen. Press it again to play the last station again |

Pausing leaves the live stream, so playing again joins the broadcast where it is now.

The screen works like a media player. The station has a tile in its genre's colour with its
initials, and while it plays, bars in the tile follow how loud the music is, measured from the
sound itself. While it connects, one bar runs along them. Beside the tile are the station's name,
the song over two lines when the station sends it, and its place in the genre with its bitrate.
Below are the buttons, drawn with their keys: W/S for genre, A, Enter and D for back, play and
next, and the volume with - and +. The other keys are along the bottom, and the country filter
shows next to the clock while it's on.

If a station that was playing drops, the radio tries again after 3, 10 and 30 seconds. If Wi-Fi
went, it waits up to a minute for it first. A station that never started isn't tried again,
because it's more likely broken than dropped. Press Enter to try it yourself.

The radio opens on the last station you played. It doesn't start playing until you press Enter.

## Number keys

1 to 9 work like the buttons on a car radio. Press an empty one and it keeps the station on
screen. From then on that number plays it, from anywhere in your list and whatever else is
playing. The number shows as a key after the station's facts, and the line under the name says
**Kept on 3** for a moment.

Shift with a number (`!` to `(` on this keyboard) puts the station on screen there instead,
moving it if it had another number, because a station has one number at most. Shift on its own
number frees it. The numbers work on the player and in the visuals, and they're kept on the SD
card, so they're still there after a restart. Removing a station frees its number. With no card
there's nothing to keep them on.

## A knob and an LED strip

Plug the Lab's Mini Angle (the knob) and LED strip into the Grove port, both at once through the
1-to-3 hub, and the radio uses them while its screen is open. The knob is on the white wire and
the strip on the yellow, so they share the port. Set each one up once in the Lab first: plug it
straight in and open it in Tab, L, Devices, which is how the Lab, and now the radio, knows it's
there. See [Connect a part](LAB.md#connect-a-part).

- **The knob tunes.** One turn runs across every station on screen, genre after genre in the
  order they're listed, so the dial covers your whole list, or one country's stations while L is
  on. Where it sits when the radio opens isn't a turn: the station only changes once you turn
  it. While one plays, the screen shows each station you pass and the line under the name says
  **Tuning...**, and the one you stop on plays once the knob rests. It works on the player and in
  the visuals.
- **The strip is a meter.** It lights in the genre's colour as loud as the music is. Change the
  volume and it shows the volume in white for a moment. When the station changes, and while it
  tunes, one LED shows where it is along the dial, like a radio's needle. Paused, it's dark. The
  Lab's limit on the strip's power, 25% unless you changed it, holds here too.
- **Only on the radio's own screen.** Leaving it turns the strip off and lets go of both, so the
  Lab and the games find the port free. They don't work while the radio plays behind Home.
- **Not through the I/O hub.** Its setup names a knob on its first port whether one is plugged
  in or not, and an empty port would turn the dial at random. Plug them straight into Grove.
- With the relays set up on the Grove port in the Lab, the radio leaves the port alone.

## Playing behind other screens

Esc goes Home and the music keeps going. It also keeps playing on Pomodoro, Alarm, Stopwatch,
Calendar, Calculator and Notes, and on the clock and battery screens. None of them needs the
speaker, the mic, the network or much memory. Home shows the station in its genre's colour, over
bars that follow the music where Home's own bars usually are. For the first few seconds it says
**Ctrl+M stops it**, and while the radio reconnects it says that instead.

**Ctrl+M** stops the radio from any screen. A podcast playing comes first: then Ctrl+M pauses
that, and [Podcasts](PODCASTS.md) plays behind the same screens. On one of the screens above, Ctrl+M again plays the
last station from your list. Open Internet Radio and it shows the station that's playing, with
every key working as usual.

Anything else stops the radio first, the same as before: AI Chat and voice, the Lab, Podcasts,
games, the NES, Mesh, Wi-Fi settings and updates. The alarm stops it too, so it can ring. A
station you found and played without saving stops when you leave, because the radio's own screen
only shows the stations in your list.

The radio needs the network to itself, so while it plays the Cardputer doesn't fetch chat
messages, check the bridge or look for updates, and Pomodoro's sounds stay quiet. All of that
comes back when it stops.

While it plays, the screen dims when the screensaver would start, and if **Screen off** is on in
Settings it goes dark when it would have. The animated screensaver doesn't run, because it would
take over the screen and stop the music. Any key brings the screen back. If the radio stops by
itself behind another screen, after its reconnects run out, Home says why for a few seconds.

## Logos

When the radio can find a station's logo, it fills the station's tile instead of the initials.
While the station plays, the bars sit in a dark strip along the logo's bottom.

- A station saved from a search brings its logo's address with it. For the stations already in
  your list, the radio asks the directory, first by the stream's address and then by the
  station's name. A name only counts on the stream's own website, because names like Rock FM
  aren't unique.
- [wsrv.nl](https://wsrv.nl), a free image service, sends the logo as a JPEG of up to 96×96
  over plain HTTP, whatever format it was in, and never enlarges it. A logo under 60 pixels,
  like most websites' little icons, would only be a blur, so that station keeps its initials.
- The radio decodes the logo once at each size it's drawn (the tile's, and the visuals' three)
  and dithers it to the screen's 256 colours, so shading and greys don't band. It keeps them on
  the card in `/radio/art/`, ready to draw. `/radio/logos/`, from versions before dithering, is
  no longer read and can be deleted; those stations' logos are fetched again.
- It only fetches while nothing is playing, after you've rested on a station for a second on
  the play screen, so browsing doesn't fetch one per station and a stream never waits for one.
  Pressing Enter stops a fetch at once.
- A station with no logo is noted, so it isn't asked about again. If the network fails, it
  tries again the next time the Cardputer starts. Delete `/radio/art/` to fetch them all
  afresh.
- The logo addresses go to wsrv.nl, so like the search words they're not private. With no SD
  card there's nowhere to keep logos, so the tiles show initials.

## Visuals

Press **V** while it plays for a full-screen picture of the music, with the station above and
the song below. **V** again changes the look, and its name shows next to the clock for a moment.
**I** shows the station's logo with the look, and **I** again hides it; it starts hidden. Each
look gives the logo its own place: framed beside Bars and Wave, in the middle of Mirror's
spectrum with the low notes either side of it, and as the round label of Rings' record. A
station with no logo shows its tile there instead, initials and all. **Esc** goes back.
The first time you open them, a card shows these keys. The look and the logo setting are kept
on the card in `/radio/visuals.txt`. Enter, the station keys and
the volume keys work there as they do on the play screen.

| Look | What it shows |
|---|---|
| **Bars** | The spectrum in 16 bars, low notes on the left, with caps that hold each bar's recent high |
| **Mirror** | The same, twice as fine, mirrored above and below a line |
| **Wave** | The sound wave itself, about 11 ms of it, scaled to fill the screen |
| **Rings** | Bass, middle and treble as three rings that swell with them |

They come from the music itself: while the visuals are on, the radio works out each moment's
spectrum as it plays. That's a little extra work, so it only happens while they're showing.

## Finding stations

Press **F**, type, and press Enter.

| Key | What it does |
|---|---|
| Left / Right (`,` `/`) | Search **By genre** or **By name** |
| Up / Down (`;` `.`) | Move between the words and the country |
| Enter | Search |
| Esc | Back to your stations |

The arrow keys work with or without Fn, so `;` `.` `,` and `/` can't be typed into a search.

- **By genre** looks for a tag, like `jazz`, `rock`, `chillout`, `80s` or `news`.
- **By name** looks for part of a station's name, like `BBC` or `Paradise`.
- **Nothing typed** finds the most listened stations, in the country if you've set one.
- **Country** keeps the search to one country. Type its two-letter code, like `US`, `GB`, `DE`
  or `RU`, and its name shows beside it so you can tell it's the right one. `UK` becomes `GB`,
  the code the directory uses. Leave it empty to search everywhere. The radio remembers the
  country you last searched with, in `/radio/country.txt`.

You get up to 20 stations, the most listened first. The search only asks for stations this
radio can play, which means plain `http://` addresses and MP3. A search stops the radio while it
runs, because the Cardputer has room for one at a time.

In the results:

| Key | What it does |
|---|---|
| Arrows, or `;` and `.` | Move |
| **Enter** | Play it, or pause it |
| **S** | Save it to your list |
| **F** | Search again |
| **Esc** | Back to your stations |

A station you saved is marked **saved**, and the one on is marked **playing**. Going back stops a
station you didn't save. One you saved keeps playing.

Where a saved station goes:

- **Found by genre:** under that genre. If your list already has it, your spelling wins, so
  `jazz` joins **Jazz**, and `house` joins **Electro / House**. If not, it starts a new genre at
  the end of the list.
- **Found by name, or with nothing typed:** under the first of your genres that one of its tags
  names. If none does, under **Other**.

The stations come from [radio-browser.info](https://www.radio-browser.info), a free directory
that anyone can add to. Your search words go to it over plain HTTP, and the music comes the same
way, so neither is private. Some listings are out of date. A station that won't play says why,
like "Radio connection failed" or "Not an MP3 stream".

## Your list on the SD card

The list is `/radio/stations.txt`. The first time Internet Radio opens with a card in, it writes
the built-in stations there, and after that it never replaces the file. One station per line:

```
# Lines starting with # are notes.
Jazz | Sonic Universe | http://ice2.somafm.com/sonicuniverse-128-mp3 | US
Rock | Nashe Radio | http://nashe1.hostingradio.ru/nashe-128.mp3 | RU
```

That's the genre, the name, the address, and the station's country as a two-letter code, which
the L filter steps through. The country is optional. A built-in station without one gets its
country from the built-in list, so a list made before countries existed still filters by
country. A station saved from Find gets the country the directory gives. Genres show in the order they first appear, and stations in the order they're written.
Edit the file on a computer, and the changes show the next time you open Internet Radio.

**M** on a station lists your genres, each in its colour with the current one marked "now",
then **+ New genre** and **Remove from the list**. Pick a genre and press Enter to move the
station there. **+ New genre** asks for a name; typing one you already have, in any case, joins
that genre. A genre left with no stations goes. Only the station's own line changes, and it
keeps its place in the file. **Esc** steps back.

**Change its place**, in the same list when the genre has more than one station, shows the
genre's stations in order, numbered, with this one highlighted. Up and Down move it past one
station at a time, Enter keeps the new order, and Esc leaves it as it was. In the file, the
genre's lines are put back in their own spots in the new order, so other genres' lines and
your notes stay where they are.

**Del** on a station asks first, and Y removes it. Only its own line goes: your notes and every
other line stay exactly as you typed them.

- Up to 100 stations, 24 genres and 16 KB. Names are cut at 47 bytes.
- A line it can't read is skipped, and the radio tells you how many it skipped.
- Only `http://` addresses play. `https://`, AAC, HLS (`.m3u8`) and playlist files (`.pls`,
  `.m3u`) don't.
- `/radio/art/` holds the stations' logos.
- `/radio/last.txt` holds the last station you played, and `/radio/country.txt` the country
  you last searched in.
- `/radio/presets.txt` holds the number keys' stations, one address per line, 1 to 9.

With no card, the built-in stations play, and nothing can be saved or removed.

## Not yet

- HTTPS, AAC and HLS streams.
- The knob and the strip through the I/O hub.
- Playing on behind the screens that need the network or the speaker, such as AI Chat.
