# Cameras

Cameras shows eufy security cameras on the Cardputer: the last picture from each, a new picture
when you ask, and live video. It's under Tab, House, and `/cameras` opens it.

The Cardputer can't talk to the cameras itself. eufy's connection is private and encrypted, and the
cameras send video too big for it to decode. So the bridge's Mac does that part: a small camera
helper signs in to your eufy account, fetches the pictures and video, and makes them the size of the
Cardputer's screen. It works with cameras on a eufy HomeBase and with cameras on their own.

## What you see

- **The wall** is the first screen: each camera's last picture, its name and how long ago it saw
  motion, with a red dot when that was in the last five minutes. The last tile is the HomeBase and its
  mode. Opening the wall wakes no camera: the pictures come from the Mac.
- **A picture** fills the screen, which has the cameras' own shape. Its top bar says when it was taken
  and the camera's battery, and the bottom one the keys. They go after three seconds, and any key
  brings them back.
- **Live** has a top bar with the camera's name and a countdown. A camera that pans and tilts has a
  small four-way icon in the bottom corner.

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
| Live | arrows | Turn the camera, if it pans and tilts |
| Live | Enter | 45 seconds more |
| Live | S | Save the frame to the SD card |
| Any | Esc | Back: live to the picture, the picture to the wall, the wall out of the app |

## Battery

Battery cameras sleep until something asks for them, and watching keeps them awake. So live view
counts down from 45 seconds. With 10 seconds left the top bar turns amber and asks, and Enter gives
it another 45. When it ends, its last frame becomes the camera's picture. The Mac also stops live
video when the Cardputer stops asking for frames for six seconds, for example when it sleeps or loses
Wi-Fi, and after ten minutes however often it's extended.

A new picture (R) wakes the camera for a few seconds. The wall and the pictures never do.

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

3. **The account**, in `~/.s1-adv/eufy.env`, beside `config.json`, readable only by you
   (`chmod 600`):

   ```
   EUFY_EMAIL=you@example.com
   EUFY_PASSWORD=your eufy password
   EUFY_COUNTRY=US
   ```

4. **Restart the bridge.** Its log says `[cameras] starting the helper`, then
   `[cameras] helper ready` and `[cameras] state signing-in`.
5. **The first sign-in:** eufy sends a code by email or text. Open Cameras on the Cardputer, type the
   code and press Enter. The helper keeps the session, so this happens once. If eufy asks for a captcha
   instead, which it does after failed sign-ins, the picture is `captcha.png` in the cameras folder:
   type its letters on the Cardputer the same way.

The helper signs in as a device of its own, so the phone app stays signed in. Changing `eufy.env`
makes it sign in again within a few seconds.

Options, under `cameras` in `config.json`: `enabled: false` turns it off, `dir` moves its folder,
`envFile` the account file, and `node` names a Node 24 that isn't Homebrew's.

## What stays private

The account file is read only by the helper, on the Mac. Pictures are kept in the cameras folder's
`pictures` and sent only to the Cardputer. The bridge's log names cameras by a short code made from
their serial number, never by name or serial, and never says anything about the account.

## When something's wrong

The wall says what's missing: the set-up on the Mac, the sign-in, or the bridge itself. The bridge's
log has `[cameras]` lines: the helper's state, each still and live view with how long it took, and
`device report` lines in which the Cardputer says how a live view went (`fps=`, `ms=` per frame,
`bytes=` per frame) or what ran short (`free=`, `big=`).
