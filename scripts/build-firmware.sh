#!/bin/bash
# Build the firmware with the toolchain kept inside this repository, so it works
# in Claude's sandbox: no network, and nothing outside this folder. The image holds
# no credentials, because a device keeps its saved settings across updates.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -x "$root/.venv/bin/python" ] || [ ! -d "$root/.platformio" ]; then
  echo "The build tools aren't set up. See docs/CLAUDE-CODE.md, under firmware updates." >&2
  exit 1
fi
export PLATFORMIO_CORE_DIR="$root/.platformio"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
export PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL=3650
exec "$root/.venv/bin/python" "$root/scripts/cardputer.py" build
