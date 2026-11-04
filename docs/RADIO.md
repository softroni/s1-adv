# Internet Radio

Tab, M, Internet Radio. It plays MP3 stations over Wi-Fi, with no account and no bridge. Your
stations live in a text file on the SD card, and Find adds new ones from a public directory.

## Playing

| Key | What it does |
|---|---|
| **W / S** or Up / Down | Change genre |
| **A / D** or Left / Right | Change station |
| **Enter** or **Space** | Play or pause |
| **L** | Show all stations, only EN/INT, or only RU |
| **-** and **+** | Volume down and up, 5% a press. The **=** key works for +, so Shift isn't needed |
| **V** | Full-screen visuals, and V again for the next look |
| **F** | Find stations |
| **Del** | Remove this station from your list |
| **Esc** | Stop and go Home |

Pausing leaves the live stream, so playing again joins the broadcast where it is now.

The screen works like a media player. The station has a tile in its genre's colour with its
initials, and while it plays, bars in the tile follow how loud the music is, measured from the
sound itself. While it connects, one bar runs along them. Beside the tile are the station's name,
the song over two lines when the station sends it, and its place in the genre with its bitrate.
Below are the buttons, drawn with their keys: W/S for genre, A, Enter and D for back, play and
next, and the volume with - and +. The other keys are along the bottom, and the language filter
shows next to the clock while it's on.

If a station that was playing drops, the radio tries again after 3, 10 and 30 seconds. If Wi-Fi
went, it waits up to a minute for it first. A station that never started isn't tried again,
because it's more likely broken than dropped. Press Enter to try it yourself.

The radio opens on the last station you played. It doesn't start playing until you press Enter.

## Logos

When the radio can find a station's logo, it fills the station's tile instead of the initials.
While the station plays, the bars sit in a dark strip along the logo's bottom.

- A station saved from a search brings its logo's address with it. For the stations already in
  your list, the radio asks the directory, first by the stream's address and then by the
  station's name. A name only counts on the stream's own website, because names like Rock FM
  aren't unique.
- [wsrv.nl](https://wsrv.nl), a free image service, shrinks the logo to the tile's size and
  sends it as a small JPEG over plain HTTP, whatever format the logo was in. The radio decodes
  it once and keeps it on the card in `/radio/logos/`, ready to draw.
- It only fetches while nothing is playing, after you've rested on a station for a second on
  the play screen, so browsing doesn't fetch one per station and a stream never waits for one.
  Pressing Enter stops a fetch at once.
- A station with no logo is noted, so it isn't asked about again. If the network fails, it
  tries again the next time the Cardputer starts. Delete `/radio/logos/` to fetch them all
  afresh.
- The logo addresses go to wsrv.nl, so like the search words they're not private. With no SD
  card there's nowhere to keep logos, so the tiles show initials.

## Visuals

Press **V** while it plays for a full-screen picture of the music, with the station above and
the song below. **V** again changes the look, and its name shows next to the clock for a moment.
**Esc** goes back. The first time you open them, a card in the middle shows these keys. Enter, the station keys and
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
Jazz | Sonic Universe | http://ice2.somafm.com/sonicuniverse-128-mp3
Rock | Nashe Radio | http://nashe1.hostingradio.ru/nashe-128.mp3 | RU
```

That's the genre, the name, the address, and `RU` if it's a Russian-language station for the L
filter. Genres show in the order they first appear, and stations in the order they're written.
Edit the file on a computer, and the changes show the next time you open Internet Radio.

**Del** on a station asks first, and Y removes it. Only its own line goes: your notes and every
other line stay exactly as you typed them.

- Up to 100 stations, 24 genres and 16 KB. Names are cut at 47 bytes.
- A line it can't read is skipped, and the radio tells you how many it skipped.
- Only `http://` addresses play. `https://`, AAC, HLS (`.m3u8`) and playlist files (`.pls`,
  `.m3u`) don't.
- `/radio/logos/` holds the stations' logos.
- `/radio/last.txt` holds the last station you played, and `/radio/country.txt` the country
  you last searched in.

With no card, the built-in stations play, and nothing can be saved or removed.

## Not yet

- HTTPS, AAC and HLS streams.
- Playing on while you use another screen.
