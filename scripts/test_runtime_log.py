#!/usr/bin/env python3
"""The log ring stays off the heap, and running out of memory isn't a network fault.

The ring compiles from the production header. The wiring is checked in the source, because what
matters is what the device does around TLS, and only real hardware can show the heap itself: as
Strings these lines split it so badly that every request failed with 80 KB free.
"""
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / 'bots/CardputerADV/src'


def part(number):
    return (SRC / 'main_parts' / f'{number:03d}_main.cpp.inc').read_text()


CPP = r'''
#include "runtime_log.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

static size_t gHeapCalls = 0;
void* operator new(size_t size) { ++gHeapCalls; return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }

using Ring = runtime_log::Ring<3, 8>;

void keepsTheNewestInOrder() {
  Ring ring;
  assert(ring.size() == 0);
  ring.add("one");
  ring.add("two");
  assert(ring.size() == 2);
  assert(!strcmp(ring[0], "one") && !strcmp(ring[1], "two"));
  ring.add("three");
  ring.add("four");
  ring.add("five");
  assert(ring.size() == 3);
  assert(!strcmp(ring[0], "three") && !strcmp(ring[1], "four") && !strcmp(ring[2], "five"));
}

void cutsLongLinesAndStaysTerminated() {
  Ring ring;
  ring.add("0123456789abcdef");
  assert(!strcmp(ring[0], "01234567"));
  ring.add("short");
  assert(!strcmp(ring[1], "short"));
}

void clearsAndStartsAgain() {
  Ring ring;
  ring.add("a");
  ring.add("b");
  ring.clear();
  assert(ring.size() == 0);
  ring.add("c");
  assert(ring.size() == 1 && !strcmp(ring[0], "c"));
}

void wrapsForLongerThanASession() {
  Ring ring;
  char line[9];
  for (int i = 0; i < 100000; ++i) {
    snprintf(line, sizeof(line), "%d", i);
    ring.add(line);
  }
  assert(ring.size() == 3 && !strcmp(ring[0], "99997") && !strcmp(ring[2], "99999"));
}

void neverTouchesTheHeap() {
  static Ring ring;
  const size_t before = gHeapCalls;
  for (int i = 0; i < 1000; ++i) ring.add("a line of the log that is too long for the ring");
  ring.clear();
  assert(gHeapCalls == before);
}

int main() {
  keepsTheNewestInOrder();
  cutsLongLinesAndStaysTerminated();
  clearsAndStartsAgain();
  wrapsForLongerThanASession();
  neverTouchesTheHeap();
  puts("runtime log ring passed");
}
'''


def wiring():
    logging = part(6)
    logf = logging[logging.index('void logf('):logging.index('lgfx::LGFXBase& uiCanvas')]
    assert 'String' not in logf, 'logf must not build a String for every line'
    assert 'rememberRuntimeLog("SER", buf);' in logf
    assert 'std::deque<String> gRuntimeLogs' not in part(4)
    assert 'runtime_log::Ring<kMaxRuntimeLogLines, kRuntimeLogChars> gRuntimeLogs;' in part(4)
    # Every task logs, so two of them can reach the ring together.
    remember = logging[logging.index('void rememberRuntimeLog'):logging.index('void appendRuntimeLog')]
    assert remember.index('portENTER_CRITICAL') < remember.index('gRuntimeLogs.add') < remember.index('portEXIT_CRITICAL')
    assert 'snprintf' in remember[:remember.index('portENTER_CRITICAL')], 'format outside the lock'

    # Clearing frees nothing now, and would lose the log just when something has gone wrong.
    assert 'gRuntimeLogs.clear()' not in part(10)

    inbox = part(20)
    worker = inbox[inbox.index('InboxHttpResult* performInboxHttpRequest'):inbox.index('void inboxHttpTask')]
    assert 'tlsRanOutOfMemory(*secure)' in worker
    done = inbox[inbox.index('void serviceInboxNetworkCompletion'):]
    failure = done[done.index('gBridgeHttpFailed = true;'):done.index('if (cancelled) {')]
    assert 'if (result->code <= 0 && !result->noMemory) gNetworkRecovery.failure' in failure, 'memory must not leave Wi-Fi'
    assert 'MBEDTLS_ERR_SSL_ALLOC_FAILED' in logging

    # The speaker comes back with about 10 KB of its own. It has to wait until TLS has let go, or
    # it lands around those buffers and the room they leave is too broken up for the next connection.
    turn = part(38)
    turn = turn[turn.index('auto finishVoiceTurn = [&]()'):]
    assert turn.index('if (voiceToolAwaitsAnswer()) gVoiceReplyConnection.clear();') < turn.index('initSpeaker();')

    # The notes list only copies the card. A tool's recording stays off it, and the Lab starts without it.
    notes = part(21)
    notes = notes[notes.index('bool registerDownloadedVoiceNote'):]
    assert notes.index('saveVoiceNoteMetadata') < notes.index('if (voiceToolAwaitsAnswer())') < notes.index('gVoiceNotes.push_back')
    assert 'gVoiceNotesBehindCard = true;' in notes
    lab = part(81)
    assert lab.index('std::vector<VoiceNote>().swap(gVoiceNotes);') < lab.index('labEnterView(gUiMode);\n  gLabGps')
    modes = part(24)
    assert 'if (mode == UiMode::Voice && gVoiceNotesBehindCard) {' in modes

    # Every way of recording ends up here, so this covers G0 and Ctrl+V as well as Alt.
    start = part(33)
    start = start[start.index('void startRecording()'):]
    assert start.index('labMode(gUiMode) && !voiceToolOnScreen()') < start.index('stopPocketSynth')


def clients_outlive_http():
    # A cancelled poll never reached http.end(), so ~HTTPClient stopped a client already freed and
    # the device panicked. Every request has to declare the client first.
    import re
    for path in (SRC / 'main_parts').glob('*.inc'):
        wrong = re.search(r'HTTPClient http;[^\n]*\n\s*std::unique_ptr<WiFiClientSecure>', path.read_text())
        assert not wrong, f'{path.name}: the client must be declared before HTTPClient'


def main():
    wiring()
    clients_outlive_http()
    with tempfile.TemporaryDirectory() as tmp:
        source = pathlib.Path(tmp) / 'test.cpp'
        binary = pathlib.Path(tmp) / 'test'
        source.write_text(CPP)
        cxx = os.environ.get('CXX', 'c++')
        subprocess.run([cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        f'-I{SRC}', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('runtime log wiring passed')


if __name__ == '__main__':
    main()
