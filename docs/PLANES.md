# Planes

Planes shows what's flying over you right now, says which way to look and how high, and tells you
when the International Space Station passes overhead where you can see it. It keeps a collection of
the planes you've spotted. It's under Tab, Tools, and `/planes` (or `/iss`) opens it.

It needs Wi-Fi and the bridge. The Cardputer asks the bridge, and the bridge asks the flight
trackers and works out the station's passes.

## Where it looks

With the Cap LoRa-1262 on and a GPS fix, Planes looks around where you are, and its screens say
"near you". Without one, it uses your **Weather city** (Settings, Device, Weather city), and the
screens name that city. A fix that arrives while it's open is used straight away.

Your position leaves the Cardputer rounded to about a kilometre, as the weather's does, and goes only
to your bridge. The bridge passes that rounded position to the flight tracker. Nothing about where you
are is written to its log.

## The pages

`,` and `/` flip between four pages. The dots in the bottom corner show which one you're on. Esc
on any of them goes back to the sky map, and Esc on the sky map leaves the app.

### The sky map

![The sky map: a round map with numbered planes, and a list beside it](assets/planes/sky.png)

The round map is north up, with you at the `+` in the middle. Each plane is a number, nearest first,
the same number as in the list beside it. The number's colour is its height: green under 3,000 feet,
amber under 15,000 feet, and blue above that. A drone that broadcasts ADS-B shows in pink, but few do.
The list gives each plane's name, the direction it's in, how far away it is and how high.

- **`;` and `.`** pick a plane, and **Enter** opens it.
- **`-` and `=`** make the ring smaller or bigger: 10, 25 or 50 miles (15, 40 or 80 km).
- **S** keeps the picked plane in Spotted, and **R** looks again now.

The list updates every 10 seconds.

### A plane

![A plane: its type, where it's going, which way to look and how many fists up](assets/planes/plane.png)

At the top are the plane's type and its route, when the flight data has one. Small planes usually
don't. Below that, **LOOK** says which way to face, and how high to look in **fists**: hold your arm
out straight with a fist, put the bottom of your fist on the horizon, and stack fists on top. Each
one is about 10 degrees of sky. The last lines give its height, whether it's climbing, how far away it
is, its speed, which way it's heading, and when it comes closest to you.

`,` and `/` go to the other planes, **S** keeps it in Spotted, and Esc goes back to the map. If the
plane flies out of the ring, its screen says so and keeps what it last knew.

### Look up!

![Look up! for a plane high overhead](assets/planes/look-up.png)

When a plane is high above you, more than about 50 degrees up, the Cardputer chimes and shows
**Look up!** with which way it's coming from and where it's going. Each plane does this once, and
at most one every 45 seconds. It comes up on the sky map and a plane's screen. While Planes is open,
the screen dims at the screensaver's time instead of going to the clock, so Look up! can still light it.
**Enter** opens the plane, **S** keeps it, and Esc goes back.

### The space station's next passes

![The next times the space station can be seen, with stars for brightness](assets/planes/passes.png)

The station can only be seen when your sky is dark and the station, 260 miles up, is still in
sunlight. So passes come near dusk and dawn, and some weeks have none. The list shows the next ones
in the next 10 days: the day, the time it appears, how long you can see it, and up to four stars for
how bright it is. Four stars is brighter than any star in the sky. Passes under a minute long or low
in the sky are left out.

- **`;` and `.`** pick a pass, and **Enter** draws its path.
- **R** sets a reminder for it, and R again turns it off. A bell marks the pass with a reminder.

### A pass's path

![The path across the sky: where it appears, its highest point and where it goes](assets/planes/pass-path.png)

This map is the sky seen from below: the ring is the horizon and the middle is straight overhead. So
the closer the path comes to the middle, the higher you look. **1** is where it appears, **2** its
highest point (in fists) and **3** where it goes. "Sets" means it goes down to the horizon, and
"vanishes" means it flies into the Earth's shadow, still high up, which is fun to watch.

`,` and `/` go to the other passes, **R** sets or clears the reminder, and Esc goes back to the list.

### The reminder

![Go outside now!](assets/planes/go-outside.png)

A reminder rings 10 minutes before the pass, from any screen, even if the Cardputer was restarted
in between. The radio or a podcast stops for it. The screen counts down and says which way to look.
The station looks like a bright star moving steadily across the sky, without blinking. Planes blink.
**Enter** draws the path. There's one reminder at a time, so setting another moves it.

### The space station right now

![Where the station is right now](assets/planes/station-now.png)

This page shows what the station is flying over, how high and how fast, how long it takes to go
round the Earth, when it next passes over you, and how many people are on board. It updates every
20 seconds, and **R** asks now.

### Spotted

![Spotted: the collection](assets/planes/spotted.png)

Every plane you keep with **S** goes into `/planes/spotted.txt` on the SD card, a line each: the
time, the plane's code, its name, its type and its height. Spotted counts them up: how many planes,
airlines and types, the type that's newest in the collection, the highest and lowest planes, and the
airline seen most. **Enter** lists the newest 40, and `;` and `.` scroll. A plane can be kept once
each time Planes is opened.

## How it works

The Cardputer asks the bridge every 10 seconds while the sky map or a plane is on screen, and for the
station when its pages are. At home it goes over the bridge's plain-HTTP door on your Wi-Fi, as
Podcasts and Cameras do, and away from home over HTTPS. The bridge
(`bridge/src/planes.mjs`) answers in short lines of text:

- **Planes** come from [adsb.lol](https://adsb.lol), a free community flight tracker. Names, routes and
  airlines come from [adsbdb](https://www.adsbdb.com), and the ground's height for "how high to look"
  from [Open-Meteo](https://open-meteo.com). Planes on the ground are left out.
- **The station's orbit** comes from [CelesTrak](https://celestrak.org), at most every six hours, and
  the bridge works out the passes with [satellite.js](https://github.com/shashwatak/satellite-js),
  including where the Sun is and when the station is in the Earth's shadow.
- **What the station is over** comes from [OpenStreetMap's Nominatim](https://nominatim.org) for land,
  and the bridge names the ocean or sea itself. **Who's on board** comes from a crew list kept on
  [GitHub](https://github.com/corquaid/international-space-station-APIs).

The bridge needs satellite.js, so after updating, run `npm install` in `bridge/` before restarting it.

## If something goes wrong

- **"The bridge needs an update for Planes":** the bridge is older than this firmware. Update it, run
  `npm install` in `bridge/`, and restart it.
- **"Can't find" your town:** change the Weather city in Settings, Device. "Salem, MA" style, a town and
  a state, works best.
- **No passes:** there may be none you can see this week. Try again in a few days.
- **A route that looks wrong:** the routes come from a shared database and are sometimes out of date.
