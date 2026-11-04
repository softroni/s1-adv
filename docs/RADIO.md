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
| **+ / -** | Volume |
| **F** | Find stations |
| **Del** | Remove this station from your list |
| **Esc** | Stop and go Home |

Pausing leaves the live stream, so playing again joins the broadcast where it is now. When the
station sends the song's name, it shows under the genre and scrolls if it's long.

If a station that was playing drops, the radio tries again after 3, 10 and 30 seconds. If Wi-Fi
went, it waits up to a minute for it first. A station that never started isn't tried again,
because it's more likely broken than dropped. Press Enter to try it yourself.

The radio opens on the last station you played. It doesn't start playing until you press Enter.

## Finding stations

Press **F**. The arrow keys switch between **By genre** and **By name**, then type and press
Enter.

- **By genre** looks for a tag, like `jazz`, `rock`, `chillout`, `80s` or `news`.
- **By name** looks for part of a station's name, like `BBC` or `Paradise`.

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
- **Found by name:** under the first of your genres that one of its tags names. If none does,
  under **Other**.

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
- `/radio/last.txt` holds the last station you played.

With no card, the built-in stations play, and nothing can be saved or removed.

## Not yet

- HTTPS, AAC and HLS streams.
- Playing on while you use another screen.
