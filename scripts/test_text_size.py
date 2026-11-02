#!/usr/bin/env python3
"""The text size setting must persist, and scaling must never leak into other screens."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parts = root / 'bots/CardputerADV/src/main_parts'
read = lambda name: (parts / f'{name}_main.cpp.inc').read_text()

constants = read('001')
assert 'kScreenSettingsItemCount = 11;' in constants
assert 'kTextSizeScales[kTextSizeCount] = {1.0f, 1.5f, 2.0f};' in constants
assert 'gPrefs.getUChar("text_size", 0)' in read('026') and 'gPrefs.putUChar("text_size"' in read('026')
assert 'return "Text size";' in read('056') and 'textSizeName(gDeviceSettings.textSize)' in read('057')
assert read('047').count('gDeviceSettings.textSize') >= 4

# A scaled size left on the shared canvas would corrupt every fixed-layout screen.
for name, start, end in (('060', 'void renderChatUi() {', 'void renderSmartLampUi() {'),
                         ('065', 'const float textScale = chatTextScale();', 'struct VoiceSpeechPayload {'),
                         ('011', 'std::vector<VisualLine> measureChatLines', 'int visibleChatRows')):
    source = read(name)
    body = source[source.index(start):source.index(end)]
    state = 1
    for match in re.finditer(r'setTextSize\(([^)]+)\)|setFont\(&fonts::(\w+)\)', body):
        size, font = match.groups()
        if size:
            state = 1 if size == '1' else 'scaled'
        elif font == 'Font0' or font == 'Font2':
            assert state == 1, f'{name}: {font} drawn while scaled'
    assert state == 1, f'{name}: scale left set'

chat = read('060')
assert 'buildVisualLines(showFace ? 216 : 228)' not in chat + read('011')
assert chat.count('buildVisualLines(chatWrapWidth())') == 1
assert 'gChatVisibleRows = visibleRows;' in chat
print('text size: ok')
