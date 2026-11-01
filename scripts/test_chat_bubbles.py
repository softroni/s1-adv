#!/usr/bin/env python3
"""Chat must read as a conversation, and voice from Home must land in it."""
from pathlib import Path

parts = Path(__file__).resolve().parents[1] / 'bots/CardputerADV/src/main_parts'
read = lambda name: (parts / f'{name}_main.cpp.inc').read_text()
chat, lines = read('060'), read('011')

assert 'kind == LineKind::User ? bubbleRight - bw : 8' in chat, 'the owner sits on the right'
assert 'lines[last].chatIndex == lines[i].chatIndex' in chat, 'one bubble per message'
assert 'rows * (lineHeight + 2) - 1' in chat
assert 'min<int>(widest + 10, chatWrapWidth(showFace) + 10)' in chat, 'bubbles fit their text'
assert 'if (!conversation) full = entry.prefix' in lines and '"AI: "' not in lines
start = read('033')
assert start.index('setUiMode(UiMode::ChatFull)') < start.index('beginVoiceRequestUi();')
assert 'gUiMode == UiMode::Home || gUiMode == UiMode::Clock' in start
print('chat bubbles: ok')
