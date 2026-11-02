#!/usr/bin/env python3
"""Exercise tapping Alt to listen and tapping it again to send."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
joined = ''.join((parts / f'{n:03d}_main.cpp.inc').read_text() for n in range(1, 82) if (parts / f'{n:03d}_main.cpp.inc').exists())


def function(signature):
    start = joined.index(signature)
    while joined.find(';', start) < joined.find('{', start):  # Skip forward declarations.
        start = joined.index(signature, start + 1)
    end, depth = joined.index('{', start) + 1, 1
    while depth:
        depth += (joined[end] == '{') - (joined[end] == '}')
        end += 1
    return joined[start:end] + '\n'


# A tap is Alt with nothing else down, on the press, so it reaches the assistant and voice tool screens.
tap = re.search(r'bool altVoiceTap = (.*?);', joined, re.S)[1]
for held in ['keys.opt', 'keys.ctrl', 'keys.fn', 'keys.shift', 'keys.tab', 'keys.enter', 'keys.del', 'keys.space',
             'upHeld', 'downHeld', 'leftHeld', 'rightHeld']:
    assert f'!{held}' in tap, held
assert tap.startswith('altDown') and 'wordSignature.isEmpty()' in tap
assert 'bool altDown = keys.alt && !pAlt;' in joined and 'pAlt = keys.alt;' in joined
assert '} else if (altVoiceTap && (isAssistantUi() || voiceToolOnScreen()) && !gLauncherVisible) {\n    toggleVoiceByTap();' in joined
assert joined.index('altVoiceTap && (isAssistantUi()') < joined.index('} else if (optionViewToggle) {')
# It has to count as input, or it wouldn't wake the screen.
assert 'rightDown || optDown || altDown || charDown || escDown;' in joined
assert 'gVoiceStartLatched = false;\n  gPendingOtaCheckAction' in joined, 'Esc must drop a waiting start'
# People need to be told.
assert 'Alt: voice  Tab: menu' in joined and 'tap Alt' in joined and 'Esc cancel, Alt send' in joined

trigger = function('void updateG0VoiceTrigger()')
start = trigger.index('if (!gG0VoicePending) gVoiceStartLatched = false;')
waiting = trigger[start:trigger.index('if (gUiMode == UiMode::Ota)')]

code = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
bool gRecording=false,gG0VoicePending=false,gVoiceStartLatched=false,gSubmitting=false,gThinking=false,gPlaybackActive=false;
bool gInboxHttpBusy=false,gInboxCancelRequested=false,pressed=false;
int starts=0,sends=0,stops=0,frames=0;std::string statusText;
void logf(const char*,...){}void setStatus(const char*s,int){statusText=s;}void render(){++frames;}
void startRecording(){assert(!gInboxHttpBusy&&!gPlaybackActive);++starts;gRecording=true;}
bool stopRecordingAndSend(){++sends;gRecording=false;return true;}
void stopPlaybackStream(){++stops;gPlaybackActive=false;}
''' + function('void toggleVoiceByTap()') + r'''
void serviceWaiting(){''' + waiting + r'''}
void reset(){gRecording=gG0VoicePending=gVoiceStartLatched=gSubmitting=gThinking=gPlaybackActive=gInboxHttpBusy=gInboxCancelRequested=pressed=false;
 starts=sends=stops=frames=0;statusText.clear();}
int main(){
 // The plain case: one tap listens, the next sends.
 reset();toggleVoiceByTap();assert(gRecording&&starts==1&&sends==0);
 toggleVoiceByTap();assert(!gRecording&&sends==1&&starts==1);
 // During a background request a tap can't stay held, so it waits by itself.
 reset();gInboxHttpBusy=true;toggleVoiceByTap();
 assert(starts==0&&gG0VoicePending&&gVoiceStartLatched&&gInboxCancelRequested&&frames==1&&statusText=="Loading voice...");
 serviceWaiting();assert(starts==0);
 gInboxHttpBusy=false;serviceWaiting();assert(starts==1&&gRecording&&!gG0VoicePending&&!gVoiceStartLatched);
 // A second tap while it waits calls it off.
 reset();gInboxHttpBusy=true;toggleVoiceByTap();toggleVoiceByTap();
 assert(!gG0VoicePending&&statusText=="Voice cancelled");
 gInboxHttpBusy=false;serviceWaiting();assert(starts==0&&!gRecording);
 // Esc drops it the same way, and the latch can't fire a later G0 press.
 reset();gInboxHttpBusy=true;toggleVoiceByTap();gG0VoicePending=false;serviceWaiting();assert(!gVoiceStartLatched);
 gInboxHttpBusy=false;serviceWaiting();assert(starts==0);
 // G0 still needs holding: without the latch, letting go means no recording.
 reset();gG0VoicePending=true;pressed=false;serviceWaiting();assert(starts==0&&gG0VoicePending);
 pressed=true;serviceWaiting();assert(starts==1);
 // Talking over a reply stops the reply and listens.
 reset();gPlaybackActive=true;toggleVoiceByTap();assert(stops==1&&starts==1&&gRecording);
 // While a message is going out it says why nothing happened.
 for(bool*busy:{&gSubmitting,&gThinking}){reset();*busy=true;toggleVoiceByTap();assert(starts==0&&!gG0VoicePending&&statusText=="Sending; tap Alt again");}
 // Started by holding G0, a tap still sends.
 reset();gRecording=true;toggleVoiceByTap();assert(sends==1);
 std::cout<<"PASS Alt voice: tap to listen, tap to send, waits by itself, cancels, talks over a reply\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-voice-tap-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
