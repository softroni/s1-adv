# Cameras

Cameras shows eufy security cameras on the Cardputer: the last picture from each, a new picture
when you ask, and live video. It's under Tab, House, and `/cameras` opens it.

The Cardputer can't talk to the cameras itself. eufy's connection is private and encrypted, and the
cameras send video too big for it to decode. So the bridge's Mac does that part: a small camera
helper signs in to your eufy account, fetches the pictures and video, and makes them the size of the
Cardputer's screen. It works with cameras on a eufy HomeBase and with cameras on their own.

## Signing in, from your phone

Until the Mac has your eufy account, Cameras shows a QR code. Scan it with your phone's camera: it
opens a page on the bridge's own HTTPS address, where you type your eufy email and password. The
Mac keeps them and signs in at once, and when eufy sends a code (it does the first time), the same
page asks for it. The Cardputer follows along and shows your cameras when it's done.

The link in the code is good for 15 minutes and stops working once you're signed in; a new one
comes up on the Cardputer by itself. It works from anywhere, without Tailscale. If eufy refuses the
email or password later, the code comes back so you can sign in again.

## What you see

- **The wall** is the first screen: each camera's last picture, its name and how long ago it saw
  motion, with a red dot when that was in the last five minutes. The last tile is the HomeBase and its
  mode. The last pictures come up first, from the Mac; then each camera is woken once, in turn, for a
  new one (the one being taken says "updating"), unless its picture is under a minute old.
- **A picture** fills the screen, which has the cameras' own shape. Its top bar says when it was taken
  and the camera's battery, and the bottom one the keys. They go after three seconds, and any key
  brings them back.
- **Live** has a top bar with the camera's name, the zoom when it's zoomed in, and a countdown. A
  camera that pans and tilts has a small four-way icon in the bottom corner, and when zoomed in a
  little map in the other corner shows where in the picture you are.

## Keys

| Where | Key | Does |
| --- | --- | --- |
| Wall | arrows | Pick a camera |
| Wall | Enter | Open its picture |
| Wall | L | Watch it live |
| Wall | R | Wake it for a new picture |
| Picture | `,` `/` | The previous or next camera |
| Picture | Enter or L | Watch it live |
| Picture | R | Wake it for a new picture, about five seconds |
| Picture | S | Save the picture to the SD card, in `/cameras` |
| Live | = and - | Zoom in and out |
| Live | arrows | Move around the zoomed picture; at its edge, a camera that pans and tilts turns |
| Live | Fn + arrows | Turn the camera, however far it's zoomed |
| Live | Enter | 45 seconds more |
| Live | S | Save the frame to the SD card |
| Live | M | Sound off and on |
| Any | Esc | Back: live to the picture, the picture to the wall, the wall out of the app |

## Sound

Live view plays the camera's microphone through the Cardputer's speaker, at the volume set in
Settings. The Mac turns the camera's sound into plain 8 kHz audio, made a little louder because
camera microphones are quiet, and the Cardputer fetches it between video frames. It holds a quarter
of a second before playing, so a late answer plays from what's held instead of breaking up; the
sound is that much behind the picture. A speaker icon in the bottom corner shows it's on, and M turns
it off and on (off stays off for the rest of the visit). A camera that sends no sound shows no icon.

## Zoom

Every camera zooms digitally to 4x: the Mac cuts the part you're looking at out of the camera's own
video, which has more pixels than the screen at every step, and the arrows move around in it. A
camera with a second, telephoto lens zooms with it too: from 3x it switches to that lens, and the
digital zoom goes on top, to 12x. Zooming and moving are instant, because the Mac already has the
whole frame. The lens goes back to wide when the live view ends, and the picture kept for the wall is
the whole view, not the zoomed one.

## Battery

Battery cameras sleep until something asks for them, and watching keeps them awake. So live view
counts down from 45 seconds. With 10 seconds left the top bar turns amber and asks, and Enter gives
it another 45. When it ends, its last frame becomes the camera's picture. The Mac also stops live
video when the Cardputer stops asking for frames for six seconds, for example when it sleeps or loses
Wi-Fi, and after ten minutes however often it's extended.

A new picture (R) wakes the camera for a few seconds, and so does opening the app, once for each
camera whose picture is over a minute old. Looking at the wall or a picture again doesn't.

## Where it works

At home, pictures and live video come straight from the Mac over your Wi-Fi, the way podcasts do.
Away from home, pictures come over the bridge's HTTPS address, and live view says it works on home
Wi-Fi only: a live frame and HTTPS don't fit in the Cardputer's memory together.

## Motion pictures

The picture shown is the last one taken through the Cardputer: by R, or the last frame of a live
view. eufy also sends a small picture with every motion alert, but some accounts and HomeBases now
encrypt those with the account's own key, and the library the helper uses can't open them yet. The
motion times on the wall come through either way.

## Setting it up on the Mac

The helper uses [eufy-sdk](https://github.com/mega-yfue/eufy-sdk) (Apache-2.0), an independent
library that talks to eufy the way its app does. It needs Node 24, which can sit beside the Node the
bridge runs on. These steps use Homebrew, and `~/.s1-adv` stands for the folder that holds the
bridge's `config.json`, with `data` for its `stateDir`.

1. **Node 24:** `brew install node@24`. Homebrew keeps it out of the way. Installing it can update a
   library the default `node` uses, so check that `node --version` still answers; if it doesn't, run
   `brew upgrade node@22` and `brew link --overwrite node@22`.
2. **The library**, in the bridge's data folder, with Node 24's npm:

   ```bash
   mkdir -p ~/.s1-adv/data/cameras && cd ~/.s1-adv/data/cameras
   echo '{"private": true}' > package.json
   PATH=/opt/homebrew/opt/node@24/bin:$PATH npm install @mega-yfue/eufy-sdk@0.5.0
   ```

3. **The account.** Open Cameras on the Cardputer and scan its QR code (above), or put it in
   `~/.s1-adv/eufy.env`, beside `config.json`, readable only by you (`chmod 600`):

   ```
   EUFY_EMAIL=you@example.com
   EUFY_PASSWORD=your eufy password
   EUFY_COUNTRY=US
   ```

4. **Restart the bridge** after installing the library. Its log says `[cameras] starting the
   helper`, then `[cameras] helper ready`.
5. **The first sign-in:** eufy sends a code by email or text. The phone page asks for it; it can
   also be typed on the Cardputer's Cameras screen. The helper keeps the session, so this happens
   once. If eufy asks for a captcha instead, which it does after failed sign-ins, the phone page shows
   it (it's also `captcha.png` in the cameras folder, and the Cardputer takes its letters too).

The helper signs in as a device of its own, so the phone app stays signed in. Changing `eufy.env`
makes it sign in again within a few seconds.

Options, under `cameras` in `config.json`: `enabled: false` turns it off, `dir` moves its folder,
`envFile` the account file, and `node` names a Node 24 that isn't Homebrew's.

## What stays private

The account file is read only by the helper, on the Mac. The phone page sends it to the Mac over
HTTPS, behind a link nobody can guess, and neither the link nor what's typed into it is logged. Pictures are kept in the cameras folder's
`pictures` and sent only to the Cardputer. The bridge's log names cameras by a short code made from
their serial number, never by name or serial, and never says anything about the account.

## When something's wrong

The wall says what's missing: the set-up on the Mac, the sign-in, or the bridge itself. The bridge's
log has `[cameras]` lines: the helper's state, each still and live view with how long it took, and
`device report` lines in which the Cardputer says how a live view went (`fps=`, `ms=` per frame,
`bytes=` per frame) or what ran short (`free=`, `big=`).
