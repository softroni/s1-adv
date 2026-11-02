#!/usr/bin/env python3
"""Exercise what the device does with live activity: states, cues, status text and speech."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
read = lambda name: (parts / f'{name}_main.cpp.inc').read_text()


def function(file, signature):
    source = read(file)
    start = source.index(signature)
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


# Both turn types ask for live status, and the short wait comes with it.
assert 'http.addHeader("X-Activity-Status", "1");' in read('037') and 'http.addHeader("X-Turn-Wait-Ms", "1000");' in read('037')
assert 'X-Activity-Status: 1' in read('035') and 'X-Activity-Status: 1' in read('065')
assert 'result->liveStatus = resp["activity_status"] | false;' in read('037')
assert 'done->replySeq, done->liveStatus);\n    gReplyWaitFromText = true;' in read('038')
assert 'resp["reply_seq"] | 0, resp["activity_status"] | false);' in read('039')
# A bridge that stops reporting must not leave the device polling at the fast rate.
inbox = read('020')
assert 'doc["activity"].is<JsonObject>()' in inbox and 'else gReplyWaitLive = false;' in inbox
# The hold has to end before the device's own read timeout.
hold = int(re.search(r'&wait_ms=(\d+)', inbox)[1])
assert hold + 1000 <= int(re.search(r'http\.setTimeout\(request->weather \? \d+ : (\d+)\);', inbox)[1]), 'a held poll would time out'
# A cue only plays when the state changes, not for each new detail.
assert 'if (changed && gActivityCueGate.allow(next, millis()))' in read('065')
# The status has to show on both screens.
assert 'look(activity).accent' in read('058') and 'gActivityDetail' in read('058')
assert 'activityLook.icon' in read('059')
assert 'drawAssistantActivityStrip(composerY - 20);' in read('060')

# The bridge and the firmware have to agree on the words.
bridge = (root / 'bridge/src/claude-activity.mjs').read_text()
wire = re.findall(r'"(\w+)"', re.search(r'ACTIVITY_STATES = Object\.freeze\(\[(.*?)\]\)', bridge, re.S)[1])
assert len(wire) >= 9
checks = ''.join(f'assert(fromWire("{name}")!=State::None);\n seen.insert(int(fromWire("{name}")));\n ' for name in wire)

code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include "assistant_activity.h"
using namespace assistant_activity;
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}
 explicit String(uint32_t n):std::string(std::to_string(n)){}bool isEmpty()const{return empty();}
 String operator+(const String&o)const{return String(std::string(*this)+std::string(o));}
 String operator+(const char*o)const{return String(std::string(*this)+o);}};
enum class VoiceRequestPhase{None,Sending};enum class FaceMode{Idle,Thinking};
VoiceRequestPhase gVoiceRequestPhase=VoiceRequestPhase::None;FaceMode gFaceMode=FaceMode::Idle;
bool gVoiceReplyPending=false,gRecording=false,gSubmitting=false,gThinking=false,gG0VoicePending=false;
bool gVoiceCapturePreparing=false,gVoiceReplySpeechBusy=false,gPlaybackActive=false,gPlaybackSpeech=false;
bool gVoiceUploadDone=false,gReplyWaitLive=false,gReplyWaitFromText=false,noticeBusy=false;
uint32_t gVoiceReplyPendingMs=0,gTextSendUntilMs=0,nowMs=100000,gActivitySeq=0,gLastRenderMs=99;
State gActivityState=State::None;String gActivityDetail,gStatusText,gQueuedVoiceReplySpeech;CueGate gActivityCueGate;
std::vector<activity_cue::Cue> played;
uint32_t millis(){return nowMs;}bool voiceRequestBusyUi(){return noticeBusy;}
void setStatus(const String&s,int){gStatusText=s;}void setFaceMode(FaceMode f){gFaceMode=f;}
bool playActivityCue(activity_cue::Cue c){played.push_back(c);return true;}
''' + function('065', 'bool activitySending()') + function('065', 'assistant_activity::State currentAssistantActivity(') + \
    function('065', 'void serviceReplyWaitStatus()') + function('065', 'void applyAssistantActivity(') + r'''
void report(const char*s,const char*d="",uint32_t seq=1){applyAssistantActivity(s,d,seq);}
void startWait(){gVoiceReplyPending=true;gVoiceReplyPendingMs=nowMs;gActivityState=State::Thinking;gActivityDetail="";
 gActivityCueGate.reset();played.clear();}
int main(){
 // Words from the bridge, including one this firmware has never heard.
 std::set<int> seen;
 ''' + checks + r'''assert(seen.size()==''' + str(len(wire)) + r''');
 assert(fromWire("idle")==State::None&&fromWire("")==State::None&&fromWire(nullptr)==State::None);
 assert(fromWire("levitating")==State::Working);
 // Each state looks and sounds like itself, and survives the 8-bit canvas.
 const State all[]={State::Listening,State::Sending,State::Thinking,State::Searching,State::Researching,State::Reading,State::Coding,
  State::Working,State::Building,State::Testing,State::Writing,State::Loading,State::Speaking};
 std::set<int> accents,cues;std::set<const uint8_t*> icons;std::set<std::string> labels;
 for(State s:all){const Look l=look(s);assert(l.label&&l.title&&l.icon);assert(std::string(l.label).size()<=11);
  accents.insert(((l.accent>>8)&0xE0)|((l.accent>>6)&0x1C)|((l.accent>>3)&0x03));
  cues.insert(int(l.cue));icons.insert(l.icon);labels.insert(l.label);}
 assert(accents.size()==13&&icons.size()==13&&labels.size()==13);
 assert(cues.size()==12);  // Listening shares one, and it never plays: the speaker is off while recording.
 assert(std::string(look(State::None).label)=="THINKING");

 // A burst of tools sounds once, and going back to thinking stays silent.
 startWait();nowMs+=4000;report("searching");assert(played.size()==1&&played[0]==activity_cue::Cue::Searching);
 report("thinking");nowMs+=200;report("reading");nowMs+=200;report("thinking");nowMs+=200;report("coding");
 assert(played.size()==1);
 // After a pause a new kind of work sounds, but the same one doesn't repeat for a while.
 nowMs+=3000;report("thinking");report("coding");assert(played.size()==2&&played.back()==activity_cue::Cue::Coding);
 nowMs+=3000;report("thinking");report("coding");assert(played.size()==2);
 nowMs+=3000;report("thinking");report("testing");assert(played.back()==activity_cue::Cue::Testing);
 nowMs+=CueGate::kRepeatMs;report("thinking");report("coding");assert(played.size()==4);
 nowMs+=CueGate::kRepeatMs;report("thinking");report("coding");assert(played.size()==5);
 // A new detail repaints but makes no sound.
 const size_t before=played.size();nowMs+=9000;gLastRenderMs=99;report("coding","snake");
 assert(played.size()==before&&gActivityDetail=="snake"&&gLastRenderMs==0);
 gLastRenderMs=99;report("coding","snake");assert(gLastRenderMs==99);
 // The gate survives millis wrapping around.
 {CueGate gate;assert(gate.allow(State::Coding,0xFFFFFF00U));assert(!gate.allow(State::Reading,0xFFFFFF10U));
  assert(gate.allow(State::Reading,0xFFFFFF00U+CueGate::kMinGapMs));}

 // The status names the work, and clears when the wait ends.
 startWait();nowMs+=12000;report("searching","esp32 heap",9);serviceReplyWaitStatus();
 assert(gStatusText=="Searching... 12s"&&gActivitySeq==9&&gFaceMode==FaceMode::Thinking);
 assert(currentAssistantActivity()==State::Searching);
 gVoiceReplyPending=false;serviceReplyWaitStatus();
 assert(gStatusText=="Ready"&&gActivityState==State::None&&gActivityDetail.empty()&&gFaceMode==FaceMode::Idle);
 // A status someone else set is left alone.
 startWait();serviceReplyWaitStatus();gStatusText="Reply received";gVoiceReplyPending=false;serviceReplyWaitStatus();
 assert(gStatusText=="Reply received");
 // A late answer can't put a status back after the reply, though its sequence still counts.
 played.clear();report("coding","late",33);
 assert(gActivityState==State::None&&gActivitySeq==33&&played.empty());
 // The wait's settings don't outlive it.
 startWait();gReplyWaitLive=gReplyWaitFromText=true;gVoiceReplyPending=false;serviceReplyWaitStatus();
 assert(!gReplyWaitLive&&!gReplyWaitFromText);
 // "idle" between turns leaves the last state alone rather than blanking a waiting screen.
 startWait();report("reading");report("idle");assert(gActivityState==State::Reading);
 gVoiceReplyPending=false;serviceReplyWaitStatus();

 // Everything the device sees for itself takes its place in the order of a turn.
 assert(currentAssistantActivity()==State::None);
 gRecording=true;assert(currentAssistantActivity()==State::Listening);gRecording=false;
 gG0VoicePending=true;assert(currentAssistantActivity()==State::Loading);gG0VoicePending=false;
 gSubmitting=true;gTextSendUntilMs=nowMs+1000;assert(currentAssistantActivity()==State::Sending);
 nowMs+=2000;assert(currentAssistantActivity()==State::Thinking);gSubmitting=false;
 gThinking=true;assert(currentAssistantActivity()==State::Thinking);gThinking=false;
 startWait();assert(currentAssistantActivity()==State::Thinking);report("coding");assert(currentAssistantActivity()==State::Coding);
 gVoiceReplyPending=false;serviceReplyWaitStatus();
 gQueuedVoiceReplySpeech="hello";assert(currentAssistantActivity()==State::Loading);gQueuedVoiceReplySpeech="";
 gVoiceReplySpeechBusy=true;assert(currentAssistantActivity()==State::Loading);gVoiceReplySpeechBusy=false;
 gPlaybackActive=true;assert(currentAssistantActivity()==State::None);
 gPlaybackSpeech=true;assert(currentAssistantActivity()==State::Speaking);
 std::cout<<"PASS assistant activity: bridge words, thirteen looks, quiet cue gate, status text and turn order\n";
}
'''

# A typed message gets the same treatment wherever its reply lands.
arrival = inbox[inbox.index('const bool voiceReplyArrived'):inbox.index('setStatus("AI Agent replied",4000);')]
assert '!gReplyWaitFromText || shouldSpeakAssistantTextReply(gInboxLastText)' in arrival
assert 'if (speakReply && (voiceReplyArrived' in arrival
turn = read('038')
assert 'if (shouldSpeakAssistantTextReply(done->reply))' in turn

with tempfile.TemporaryDirectory(prefix='s1-assistant-activity-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(src), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
