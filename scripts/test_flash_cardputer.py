#!/usr/bin/env python3
"""Run flash-cardputer.sh against fake esptool, clipboard and download tools."""
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import hashlib
import json
import os
import re
import stat
import subprocess
import tempfile
import threading

root = Path(__file__).resolve().parents[1]
source = (root / 'scripts/flash-cardputer.sh').read_text()
TOKEN = 'fixture-device-token-0123456789abcdef0123456789'
URL = 'https://bridge.example.com:10000'
IMAGE = 's1-adv-0.2.201-dev-full.bin'
FIRMWARE = b'fixture firmware'

# Personal settings belong in the private config, never in this public file.
for private in (r'/Users/', r'/home/', r'\.ts\.net', r'\.tail[0-9a-f]+', r'sk-ant'):
    assert not re.search(private, source), private
assert 'set -x' not in source
assert re.search(r'^IMAGE_SHA256="[0-9a-f]{64}"$', source, re.M)
assert re.search(r'^ESPTOOL_VERSION="\d+\.\d+\.\d+"$', source, re.M)

FAKE_PYTHON = r'''#!/bin/bash
home="$S1_TEST"
mode=$(cat "$home/mode")
echo "$*" >> "$home/esptool-calls"
case "$*" in
  *"esptool version"*) exit 0 ;;
  *flash_id*)
    tries=$(grep -c flash_id "$home/esptool-calls")
    case "$mode" in
      wrongchip) echo "A fatal error occurred: This chip is ESP32-C3 not ESP32-S3."; exit 2 ;;
      retry) [ "$tries" -eq 1 ] && { echo "A fatal error occurred: Failed to connect"; exit 2; } ;;
      dead) echo "A fatal error occurred: Failed to connect"; exit 2 ;;
      denied) echo "could not open port: [Errno 13] Permission denied"; exit 2 ;;
      small) echo "Detected flash size: 4MB"; exit 0 ;;
    esac
    echo "Detected flash size: 8MB"; exit 0 ;;
  *write_flash*)
    if [ "$mode" = writefail ] && [ "$(grep -c write_flash "$home/esptool-calls")" -eq 1 ]; then
      echo "Serial port error"; exit 2
    fi
    echo "Hash of data verified."; exit 0 ;;
esac
exit 3
'''


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class Health(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('Authorization') != f'Bearer {TOKEN}':
            self.send_response(401)
            self.end_headers()
            return
        body = json.dumps({'ok': True, 'device_firmware': self.server.report}).encode()
        self.send_response(200)
        self.send_header('content-type', 'application/json')
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


class Case:
    def __init__(self, base, name, mode='ok', config=True, cached=True, port=True, firmware=FIRMWARE):
        self.home = base / name
        self.flash = self.home / 'flash'
        self.script = self.home / 'flash-cardputer.sh'
        pinned = hashlib.sha256(FIRMWARE).hexdigest()
        executable(self.script, re.sub(r'^IMAGE_SHA256=.*$', f'IMAGE_SHA256="{pinned}"', source, flags=re.M))
        (self.home / 'mode').write_text(mode)
        executable(self.flash / 'venv/bin/python', FAKE_PYTHON)
        self.tools = self.home / 'bin'
        executable(self.tools / 'pbcopy', '#!/bin/bash\ncat > "$S1_TEST/clipboard"\n')
        for name_ in ('wl-copy', 'xclip'):
            executable(self.tools / name_, '#!/bin/bash\ncat > "$S1_TEST/clipboard"\n')
        self.remote = self.home / 'remote'
        self.remote.mkdir()
        (self.remote / IMAGE).write_bytes(firmware)
        if cached:
            (self.flash / 'release').mkdir(parents=True)
            (self.flash / 'release' / IMAGE).write_bytes(firmware)
        (self.home / 'dev').mkdir()
        if port:
            (self.home / 'dev/cu.usbmodem1101').touch()
        if config:
            (self.home / 'config.json').write_text(json.dumps({'server': {'port': PORT},
                'installation': {'publicUrl': URL}, 'devices': [{'device_token': TOKEN}]}))

    def run(self, keys='\n\n', args=(), no_clipboard=False):
        path = (self.tools.as_posix() + ':' if not no_clipboard else '') + os.environ['PATH']
        if no_clipboard:
            bare = self.home / 'bare'
            bare.mkdir(exist_ok=True)
            path = ':'.join(p for p in path.split(':')
                            if not any((Path(p) / tool).exists() for tool in ('pbcopy', 'wl-copy', 'xclip')))
            for tool in ('python3', 'curl', 'sha256sum', 'shasum', 'tee', 'grep', 'cut', 'cat', 'rm', 'mv',
                         'mkdir', 'chmod', 'uname', 'sleep'):
                found = subprocess.run(['/bin/sh', '-c', f'command -v {tool}'], capture_output=True, text=True).stdout.strip()
                if found and not (bare / tool).exists():
                    (bare / tool).symlink_to(found)
            path = f'{bare}:{path}'
        env = {**os.environ, 'PATH': path, 'S1_TEST': str(self.home), 'S1_ADV_HOME': str(self.home),
               'S1_ADV_PORT_GLOB': f'{self.home}/dev/cu.usbmodem*', 'S1_ADV_PORT_WAIT': '2',
               'S1_ADV_CONNECT_WAIT': '2', 'S1_ADV_RELEASE_URL': self.remote.as_uri()}
        result = subprocess.run(['/bin/bash', str(self.script), *args], input=keys, text=True,
                                capture_output=True, env=env, timeout=60)
        self.code = result.returncode
        self.output = result.stdout + result.stderr
        assert TOKEN not in self.output, 'token printed'
        calls = self.home / 'esptool-calls'
        self.calls = calls.read_text() if calls.exists() else ''
        self.writes = self.calls.count('write_flash')
        clip = self.home / 'clipboard'
        self.clipboard = clip.read_text() if clip.exists() else ''
        return self


server = ThreadingHTTPServer(('127.0.0.1', 0), Health)
server.report = {'version': '0.2.201-dev', 'git': 'abc1234'}
PORT = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()

with tempfile.TemporaryDirectory() as temp:
    base = Path(temp)

    done = Case(base, 'happy').run()
    assert done.code == 0, done.output
    assert done.writes == 1
    assert '--chip esp32s3' in done.calls and f'write_flash 0x0 {done.flash}/release/{IMAGE}' in done.calls
    assert done.clipboard == TOKEN
    assert URL in done.output and 'Connected. Firmware 0.2.201-dev' in done.output
    assert 'Done. Send this from the Cardputer' in done.output
    assert TOKEN not in (done.flash / 'flash.log').read_text()
    assert stat.S_IMODE((done.flash / 'flash.log').stat().st_mode) == 0o600

    # A computer with no bridge and no cached files: the case this script exists for.
    fresh = Case(base, 'fresh', config=False, cached=False)
    fresh.run(args=('--bridge-url', 'https://other.example.com'))
    assert fresh.code == 0, fresh.output
    assert fresh.writes == 1
    assert (fresh.flash / 'release' / IMAGE).read_bytes() == FIRMWARE
    assert 'https://other.example.com' in fresh.output
    assert "['device_token']" in fresh.output and fresh.clipboard == ''
    assert 'Waiting for the Cardputer' not in fresh.output

    unknown = Case(base, 'unknown', config=False).run()
    assert unknown.code == 0 and "your bridge's https address" in unknown.output

    noclip = Case(base, 'noclip').run(no_clipboard=True)
    assert noclip.code == 0, noclip.output
    assert "['device_token']" in noclip.output and noclip.clipboard == ''

    server.report = None
    silent = Case(base, 'silent').run()
    assert silent.code == 0 and 'That can be normal' in silent.output

    retried = Case(base, 'retry', mode='retry').run('\n\n\n')
    assert retried.code == 0 and retried.writes == 1 and 'Hold the G0 button' in retried.output
    wrote = Case(base, 'writefail', mode='writefail').run('\n\n\n')
    assert wrote.code == 0 and wrote.writes == 2

    fixed = Case(base, 'fixedport', port=False)
    chosen = fixed.home / 'dev/my-port'
    chosen.touch()
    fixed.run(args=('--port', str(chosen)))
    assert fixed.code == 0 and f'--port {chosen}' in fixed.calls

    # Everything below must stop before touching the device's flash.
    for name, kwargs, keys, expected in (
        ('wrongchip', {'mode': 'wrongchip'}, '\n', "isn't an ESP32-S3"),
        ('small', {'mode': 'small'}, '\n', "doesn't report 8 MB"),
        ('denied', {'mode': 'denied'}, '\n', 'dialout'),
        ('dead', {'mode': 'dead'}, '\n' * 8, 'after 5 tries'),
        ('noport', {'port': False}, '\n', 'No Cardputer found'),
        ('noconfirm', {}, '', 'Nothing was flashed'),
    ):
        case = Case(base, name, **kwargs).run(keys)
        assert case.code == 1, (name, case.output)
        assert case.writes == 0, name
        assert expected in case.output, (name, case.output)
        assert case.clipboard == '', name

    tampered = Case(base, 'tampered', cached=False, firmware=b'evil firmware').run()
    assert tampered.code == 1 and tampered.writes == 0 and 'checksum' in tampered.output
    assert not (tampered.flash / 'release' / IMAGE).exists()
    assert tampered.calls.count('flash_id') == 0

    swapped = Case(base, 'swapped', cached=True, firmware=b'evil firmware')
    swapped.run()
    assert swapped.code == 1 and swapped.writes == 0, swapped.output

    badarg = Case(base, 'badarg').run(args=('--nope',))
    assert badarg.code == 2 and badarg.writes == 0

server.shutdown()
print('flash script: ok')
