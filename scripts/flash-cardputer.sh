#!/bin/bash
# Flash the public S1-ADV release onto a Cardputer ADV from macOS or Linux.
#
# Needs bash, curl and Python 3. On first use it downloads the release image
# and esptool into ~/.s1-adv/flash. A bridge set up on this computer is found
# in ~/.s1-adv/config.json. The device token is only ever copied to the
# clipboard, and this file holds no personal settings.

set -u

VERSION="0.2.201-dev"
IMAGE="s1-adv-${VERSION}-full.bin"
IMAGE_SHA256="64377f7e0166bbba5ef23d7c8dce880dec1e2fb1ee319efd1d2dbb335a8a277f"
RELEASE_URL="${S1_ADV_RELEASE_URL:-https://github.com/s1z0v/s1-adv/releases/download/v${VERSION}}"
ESPTOOL_VERSION="4.5.1"

ROOT="${S1_ADV_HOME:-$HOME/.s1-adv}"
CONFIG="$ROOT/config.json"
CACHE="$ROOT/flash"
PY="$CACHE/venv/bin/python"
IMAGE_PATH="$CACHE/release/$IMAGE"
LOG="$CACHE/flash.log"
PORT_WAIT="${S1_ADV_PORT_WAIT:-180}"
CONNECT_WAIT="${S1_ADV_CONNECT_WAIT:-600}"
BRIDGE_URL="${S1_ADV_BRIDGE_URL:-}"
PORT=""
FIXED_PORT=""

case "$(uname -s)" in
  Darwin) PORT_GLOB="${S1_ADV_PORT_GLOB:-/dev/cu.usbmodem*}" ;;
  Linux)  PORT_GLOB="${S1_ADV_PORT_GLOB:-/dev/ttyACM*}" ;;
  *) echo "This script supports macOS and Linux. See docs/FLASHING.md for other systems." >&2; exit 1 ;;
esac

usage() {
  cat <<EOF
Usage: scripts/flash-cardputer.sh [--port PATH] [--bridge-url https://YOUR_BRIDGE]

Flashes S1-ADV $VERSION and shows the setup steps.

  --port PATH        USB port, if there is more than one device
  --bridge-url URL   Bridge address to show in the setup steps. A bridge set up
                     on this computer is found automatically.
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --port) FIXED_PORT="${2:-}"; [ -n "$FIXED_PORT" ] || { usage >&2; exit 2; }; shift 2 ;;
    --bridge-url) BRIDGE_URL="${2:-}"; [ -n "$BRIDGE_URL" ] || { usage >&2; exit 2; }; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

image_ok() { [ -f "$IMAGE_PATH" ] && [ "$(sha256 "$IMAGE_PATH")" = "$IMAGE_SHA256" ]; }

ensure_image() {
  image_ok && return 0
  say "Downloading S1-ADV $VERSION"
  mkdir -p "$CACHE/release"
  curl -fsSL "$RELEASE_URL/$IMAGE" -o "$IMAGE_PATH.tmp" || fail "Couldn't download the firmware. Nothing was flashed."
  mv "$IMAGE_PATH.tmp" "$IMAGE_PATH"
  if ! image_ok; then
    rm -f "$IMAGE_PATH"
    fail "The firmware doesn't match its checksum, so it was deleted. Nothing was flashed."
  fi
}

ensure_esptool() {
  "$PY" -m esptool version >/dev/null 2>&1 && return 0
  say "Installing the flashing tool"
  command -v python3 >/dev/null 2>&1 || fail "Python 3 is required. Install it, then run this again."
  python3 -m venv "$CACHE/venv" || fail "Couldn't create a Python environment. On Debian or Ubuntu, install python3-venv."
  "$PY" -m pip -q install "esptool==$ESPTOOL_VERSION" || fail "Couldn't install esptool. Nothing was flashed."
}

# Reads one setting from the private bridge config without printing anything else.
config_value() {
  [ -f "$CONFIG" ] || return 1
  python3 - "$CONFIG" "$1" <<'PY' 2>/dev/null
import json, sys
config = json.load(open(sys.argv[1]))
value = {"public_url": (config.get("installation") or {}).get("publicUrl"),
         "token": config["devices"][0]["device_token"]}[sys.argv[2]]
if not value:
    sys.exit(1)
sys.stdout.write(value)
PY
}

clipboard() {
  if command -v pbcopy >/dev/null 2>&1; then pbcopy
  elif command -v wl-copy >/dev/null 2>&1; then wl-copy
  elif command -v xclip >/dev/null 2>&1; then xclip -selection clipboard
  else return 1; fi
}

token_step() {
  # Without a config there is no token, and copying nothing would clear the clipboard.
  if [ -f "$CONFIG" ]; then
    config_value token | clipboard 2>/dev/null
    local statuses=("${PIPESTATUS[@]}")
    if [ "${statuses[0]}" -eq 0 ] && [ "${statuses[1]}" -eq 0 ]; then
      TOKEN_HELP="Paste it from the clipboard"
      return 0
    fi
  fi
  TOKEN_HELP="Use the token below"
  say "Get your device token on the computer that runs the bridge:"
  cat <<'EOF'
  python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.s1-adv/config.json')))['devices'][0]['device_token'])"
EOF
}

find_port() {
  if [ -n "$FIXED_PORT" ]; then
    [ -e "$FIXED_PORT" ] && { PORT="$FIXED_PORT"; return 0; }
    return 1
  fi
  local ports=() path
  for path in $PORT_GLOB; do [ -e "$path" ] && ports+=("$path"); done
  [ "${#ports[@]}" -eq 0 ] && return 1
  if [ "${#ports[@]}" -eq 1 ]; then PORT="${ports[0]}"; return 0; fi
  echo "More than one USB device found. Type the number of the Cardputer:" >&2
  select path in "${ports[@]}"; do [ -n "$path" ] && { PORT="$path"; return 0; }; done
  return 1
}

wait_for_port() {
  local waited=0
  find_port && return 0
  say "Plug in the Cardputer with a USB data cable and turn it on."
  echo "If your computer asks whether to allow the accessory, click Allow."
  while [ "$waited" -lt "$PORT_WAIT" ]; do
    sleep 2; waited=$((waited + 2))
    find_port && return 0
    printf '.'
  done
  echo
  fail "No Cardputer found. Check the power switch, then try another cable. Charge-only cables don't work."
}

bootloader_help() {
  say "The Cardputer didn't answer. Put it in flashing mode:"
  echo "  1. Hold the G0 button"
  echo "  2. Press and release Reset"
  echo "  3. Release G0"
  printf 'Press Return to try again, or Ctrl-C to stop. '
  read -r _ || fail "Stopped."
}

# Runs esptool, showing its output and keeping it for the checks below.
run_esptool() {
  local out="$CACHE/.esptool.out"
  PYTHONUNBUFFERED=1 "$PY" -m esptool --chip esp32s3 --port "$PORT" "$@" 2>&1 | tee "$out" | tee -a "$LOG"
  ESPTOOL_STATUS=${PIPESTATUS[0]}
  ESPTOOL_OUTPUT=$(cat "$out"); rm -f "$out"
  if printf '%s' "$ESPTOOL_OUTPUT" | grep -q 'Permission denied'; then
    fail "No permission to use $PORT. On Linux, add yourself to the dialout group, log in again and retry."
  fi
}

check_device() {
  local attempt
  for attempt in 1 2 3 4 5; do
    say "Checking the device on $PORT"
    run_esptool --after no_reset flash_id
    [ "$ESPTOOL_STATUS" -eq 0 ] && break
    if printf '%s' "$ESPTOOL_OUTPUT" | grep -q 'not ESP32-S3'; then
      fail "This isn't an ESP32-S3 device, so it isn't a Cardputer ADV. Nothing was flashed."
    fi
    [ "$attempt" -eq 5 ] && fail "Couldn't reach the Cardputer after 5 tries. Nothing was flashed. Details: $LOG"
    bootloader_help
    wait_for_port
  done
  printf '%s' "$ESPTOOL_OUTPUT" | grep -q 'Detected flash size: 8MB' \
    || fail "This device doesn't report 8 MB flash, so it isn't a Cardputer ADV. Nothing was flashed."
}

flash_image() {
  local attempt
  for attempt in 1 2 3 4 5; do
    say "Flashing. Don't unplug it."
    run_esptool --baud 460800 write_flash 0x0 "$IMAGE_PATH"
    [ "$ESPTOOL_STATUS" -eq 0 ] && return 0
    [ "$attempt" -eq 5 ] && fail "Flashing failed after 5 tries. Run this script again. Details: $LOG"
    bootloader_help
    wait_for_port
  done
}

show_setup_steps() {
  # Set here, not in the heredoc: bash 3.2 on macOS breaks on an apostrophe there.
  local bridge="$BRIDGE_URL"
  [ -n "$bridge" ] || bridge="your bridge's https address"
  say "Flashed. Finish on the setup page:"
  cat <<EOF

  1. Press Reset on the Cardputer. It shows a setup Wi-Fi name and password.
  2. Join that Wi-Fi network from this computer or your phone.
  3. Open http://192.168.4.1/
  4. Choose your 2.4 GHz Wi-Fi and enter its password.
  5. Replace the Bridge URL with:  $bridge
  6. Device token:                 $TOKEN_HELP
  7. Click "Save and restart".
  8. Switch this computer back to your normal Wi-Fi.
EOF
}

# Only a bridge on this computer can be watched, because the check needs its token.
wait_for_bridge() {
  [ -f "$CONFIG" ] || return 2
  say "Waiting for the Cardputer to reach your bridge. Leave this window open."
  python3 - "$CONFIG" "$CONNECT_WAIT" <<'PY'
import json, sys, time, urllib.request
config = json.load(open(sys.argv[1]))
token = config["devices"][0]["device_token"]
url = f"http://127.0.0.1:{config['server']['port']}/healthz"
deadline = time.time() + int(sys.argv[2])
while time.time() < deadline:
    try:
        request = urllib.request.Request(url, headers={"Authorization": f"Bearer {token}"})
        with urllib.request.urlopen(request, timeout=5) as response:
            report = json.load(response).get("device_firmware")
        if report:
            print(f"Connected. Firmware {report.get('version')} reached your bridge.")
            sys.exit(0)
    except Exception:
        pass
    time.sleep(3)
print("The bridge hasn't heard from the Cardputer yet.")
sys.exit(1)
PY
}

mkdir -p "$CACHE"
chmod 700 "$ROOT" 2>/dev/null
ensure_image
ensure_esptool
: > "$LOG"; chmod 600 "$LOG"
[ -n "$BRIDGE_URL" ] || BRIDGE_URL="$(config_value public_url || true)"
if [ -n "$BRIDGE_URL" ] && ! curl -fs -m 10 -o /dev/null "$BRIDGE_URL/healthz"; then
  echo "Warning: the bridge isn't reachable at $BRIDGE_URL. You can still flash."
fi

wait_for_port
check_device

say "Ready to flash S1-ADV $VERSION"
echo "This replaces the firmware on the Cardputer and may reset its saved settings."
echo "The SD card isn't touched."
printf 'Press Return to flash, or Ctrl-C to stop. '
read -r _ || fail "Stopped. Nothing was flashed."

flash_image
token_step
show_setup_steps
wait_for_bridge
case $? in
  0) say "Done. Send this from the Cardputer:  Reply with: ready" ;;
  1) echo "That can be normal. Check that this computer is back on your Wi-Fi, then send"
     echo "this from the Cardputer:  Reply with: ready" ;;
  *) say "When setup is saved, send this from the Cardputer:  Reply with: ready" ;;
esac
