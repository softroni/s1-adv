#!/usr/bin/env python3
"""Exercise the assistant's status sounds, when they may play, and the sending label."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "activity_cue.h"'),
        ('037_main.cpp.inc', 'playActivityCue(activity_cue::Cue::Sending);'),
        ('065_main.cpp.inc', 'playActivityCue(activity_cue::Cue::Thinking);'),
        ('020_main.cpp.inc', 'playActivityCue(activity_cue::Cue::ReplyReady);'),
        ('065_main.cpp.inc', 'gPlaybackSpeech = started;'),
        ('059_main.cpp.inc', 'gPlaybackSpeech ? "SPEAK" : "PLAY"')]:
    assert text in (parts / file).read_text(), (file, text)

# The sending sound has to come after the screen is painted, or TLS can take the canvas first.
text = (parts / '037_main.cpp.inc').read_text()
assert text.index('render(); // Paint the pending') < text.index('Cue::Sending') < text.index('xTaskCreate')
# Every saved-file playback must clear the speech flag, or a song could be labeled SPEAK.
playback = (parts / '022_main.cpp.inc').read_text()
assert playback.count('gPlaybackActive = true;\n  gPlaybackSpeech = false;') == 2
# Nothing in the assistant flow still uses the harsh raw tone.
for file in ['020_main.cpp.inc', '065_main.cpp.inc']:
    assert 'Speaker.tone(' not in (parts / file).read_text(), file


def function(file, signature):
    source = (parts / file).read_text()
    start = source.index(signature)
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


code = r'''
#include "activity_cue.h"
#include "release_delivery.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>
using namespace activity_cue;
enum class VoiceRequestPhase{None,Recording,Sending,Processing};
VoiceRequestPhase gVoiceRequestPhase=VoiceRequestPhase::None;
bool gCanvasSpriteActive=true,gRecording=false,gPlaybackActive=false,gRealtimeActive=false,gRealtimeExclusiveMode=false;
bool gVoiceCapturePreparing=false,gVoiceDialBusy=false,gRealtimeDialBusy=false,gVoiceReplySpeechBusy=false;
bool gSubmitting=false,gVoiceUploadDone=false;
uint32_t gTextSendUntilMs=0,nowMs=1000;
uint32_t millis(){return nowMs;}
int16_t gUpdateChime[release_delivery::kChimeSamples];
struct Speaker {bool busy=false;int queued=0;bool isPlaying(){return busy;}
 bool playRaw(int16_t*p,size_t n,unsigned rate,bool stereo,int repeat,int channel){
  assert(p==gUpdateChime&&n==1920&&rate==8000&&!stereo&&repeat==1&&channel==0);++queued;return true;}
};struct{struct Speaker Speaker;}M5Cardputer;
''' + function('027_main.cpp.inc', 'bool chimeMayPlay()') + '\n' + \
    function('027_main.cpp.inc', 'bool playActivityCue(') + '\n' + \
    function('065_main.cpp.inc', 'bool activitySending()') + r'''
constexpr size_t kN=release_delivery::kChimeSamples;constexpr uint32_t kRate=release_delivery::kChimeRate;
struct Sound{std::vector<int16_t> pcm=std::vector<int16_t>(kN);int peak=0,step=0;};
Sound make(Cue cue){Sound s;render(cue,s.pcm.data(),kN,kRate);
 for(size_t i=0;i<kN;++i){s.peak=std::max(s.peak,std::abs(int(s.pcm[i])));if(i)s.step=std::max(s.step,std::abs(s.pcm[i]-s.pcm[i-1]));}
 return s;}
int crossings(const Sound&s,size_t from,size_t to){int n=0;for(size_t i=from+1;i<to;++i)n+=(s.pcm[i-1]<0)!=(s.pcm[i]<0);return n;}
int main(){
 const Sound send=make(Cue::Sending),think=make(Cue::Thinking),reply=make(Cue::ReplyReady);
 // Each one is quiet but audible, starts and ends in silence, and has no clicks.
 for(const Sound*s:{&send,&think,&reply}){
  assert(s->peak>800&&s->peak<3000);assert(std::abs(int(s->pcm[0]))<5&&std::abs(int(s->pcm[kN-1]))<5);
  assert(s->step<int(2*kTwoPi*988*kLevel/kRate));}
 // Their shapes differ: sending climbs, a ready reply falls, and thinking is lower than both.
 const size_t a=size_t(kRate*0.002),b=size_t(kRate*0.06);
 const size_t c=size_t(kRate*0.09),d=size_t(kRate*0.15);
 assert(crossings(send,c,d)>crossings(send,a,b));
 assert(crossings(reply,c,d)<crossings(reply,a,b));
 assert(crossings(think,a,b)<crossings(send,a,b)&&crossings(think,a,b)<crossings(reply,a,b));
 // Thinking is two taps: the sound dies away, then comes back.
 {int middle=0,second=0;for(size_t i=size_t(kRate*0.10);i<size_t(kRate*0.12);++i)middle=std::max(middle,std::abs(int(think.pcm[i])));
  for(size_t i=size_t(kRate*0.13);i<size_t(kRate*0.16);++i)second=std::max(second,std::abs(int(think.pcm[i])));assert(second>middle);}
 // A cue plays only when the speaker is free.
 assert(playActivityCue(Cue::Sending)&&M5Cardputer.Speaker.queued==1);
 for(bool*flag:{&gRecording,&gPlaybackActive,&gRealtimeActive,&gRealtimeExclusiveMode,&gVoiceCapturePreparing,
                &gVoiceDialBusy,&gRealtimeDialBusy,&gVoiceReplySpeechBusy,&M5Cardputer.Speaker.busy}){
  *flag=true;assert(!playActivityCue(Cue::Thinking)&&M5Cardputer.Speaker.queued==1);*flag=false;}
 gCanvasSpriteActive=false;assert(!playActivityCue(Cue::Thinking));gCanvasSpriteActive=true;
 // A voice message shows sending until the upload is done, however long that takes.
 gVoiceRequestPhase=VoiceRequestPhase::Sending;gSubmitting=true;nowMs+=60000;assert(activitySending());
 gVoiceUploadDone=true;assert(!activitySending());gVoiceRequestPhase=VoiceRequestPhase::None;gSubmitting=false;gVoiceUploadDone=false;
 // A text message shows it briefly, and only while it's going out.
 gTextSendUntilMs=nowMs+kSendingShowMs;assert(!activitySending());gSubmitting=true;assert(activitySending());
 nowMs+=kSendingShowMs-1;assert(activitySending());nowMs+=1;assert(!activitySending());
 // Millis wrapping around doesn't stick it on.
 nowMs=0xfffffff0U;gTextSendUntilMs=nowMs+kSendingShowMs;assert(activitySending());nowMs+=kSendingShowMs;assert(!activitySending());
 std::cout<<"PASS activity cues: three quiet distinct sounds, speaker guards, voice and text sending labels\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-activity-cue-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
