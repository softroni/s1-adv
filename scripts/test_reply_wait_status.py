#!/usr/bin/env python3
"""A pending reply must look like work in progress, for as long as the bridge allows."""
from pathlib import Path

parts = Path(__file__).resolve().parents[1] / 'bots/CardputerADV/src/main_parts'
read = lambda name: (parts / f'{name}_main.cpp.inc').read_text()
reply = read('065')

accept = reply[reply.index('void acceptPendingAssistantRequest('):reply.index('void serviceReplyWaitStatus()')]
assert 'setFaceMode(FaceMode::Thinking);' in accept and 'setFaceMode(FaceMode::Idle);' not in accept
assert 'setStatus("Thinking...", 0);' in accept, 'a timed status would fall back to Ready'
assert 'gVoiceReplyPending || voiceRequestBusyUi()) return State::Thinking' in reply
assert 'gVoiceReplyPending || (gVoiceNoticeVisible' in read('009')
assert 'serviceReplyWaitStatus' in read('061') and 'void serviceReplyWaitStatus();' in read('005')

# Both sides must wait as long as the bridge's turn limit.
assert 'kReplyWaitTimeoutMs = 1800000;' in read('001')
assert '300000U' not in read('020')
assert read('020').count('kReplyWaitTimeoutMs') == 1

status = reply[reply.index('void serviceReplyWaitStatus()'):reply.index('void applyFirmwareJobState')]
assert '!gRecording && !gSubmitting' in status, 'a new recording owns the status line'
assert 'if (gStatusText == shown) setStatus("Ready", 0);' in status, 'the wait status must clear when the wait ends'
print('reply wait status: ok')
