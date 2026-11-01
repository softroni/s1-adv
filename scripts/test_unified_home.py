#!/usr/bin/env python3
"""Execute the real unified Home renderer and one-action OTA renderer."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parts = root / 'bots/CardputerADV/src/main_parts'
source = '\n'.join(p.read_text() for p in sorted(parts.glob('*.inc')))


def function(signature):
    tail = r'[^;{]*\{' if signature.endswith('(') else r'\s*\{'
    start = re.search(re.escape(signature) + tail, source).start()
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


code = r'''
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include "firmware_job_display.h"
#include "release_delivery.h"
using std::min;using std::max;
struct String:std::string {
 using std::string::string;String(const std::string&s):std::string(s){}
 String(int n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}void trim(){}
};
enum class AssistantPulseState{Ready,Recording,Thinking,Playback,Realtime};
enum class VoiceRequestPhase{None,Recording,Sending,Processing,Accepted,Reply,Error};
bool gRealtimeActive=false,gRealtimeRecording=false,gRealtimeAwaiting=false,gRealtimeConnected=false;
bool gVoiceReplyPending=false,gRecording=false,gSubmitting=false,gThinking=false,gPlaybackActive=false,gVoiceNoticeVisible=false;
bool gAssistantPendingVisible=false,gBootBackgroundActive=false;
bool gVoiceCapturePreparing=false,gG0VoicePending=false;
VoiceRequestPhase gVoiceRequestPhase=VoiceRequestPhase::None;
uint32_t nowMs=13000,gRecordStartMs=1000,gHomeUpdatePreviewUntilMs=0,gAssistantPulsePressUntilMs=0;
uint32_t millis(){return nowMs;}
String gInputBuffer,gStatusText="Sending voice...",gBootBackgroundLabel,gQueuedVoiceReplySpeech;
bool gVoiceReplySpeechBusy=false;
firmware_job_display::State gFirmwareJobState=firmware_job_display::State::Idle;
struct{bool clockTwelveHour=false;uint8_t minBatteryForUpdate=25;}gDeviceSettings;
struct{int level=72;}gBatterySnapshot;
uint8_t gVoiceLevel8=64;
constexpr const char*kAppVersion="0.2.178-dev";
namespace fonts{int Font0=0,Font2=2,Font4=4,Font7=7,efontCN_12=12;}
constexpr uint16_t TFT_WHITE=0xffff;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
template<class T>T clampValue(T n,T low,T high){return max(low,min(n,high));}
struct Canvas {
 const int*font=&fonts::Font0;int x=0,y=0,chatRects=0,clockRects=0;
 std::vector<std::string> printed;std::vector<uint16_t> arcs;std::vector<std::vector<int>> shapes;
 void fillScreen(uint16_t){}
 template<class...T>void fillCircle(T...a){shapes.push_back({int(a)...});}template<class...T>void drawCircle(T...a){shapes.push_back({int(a)...});}
 template<class...T>void fillTriangle(T...){}template<class...T>void drawFastHLine(T...a){shapes.push_back({int(a)...});}
 void fillRoundRect(int xx,int yy,int w,int h,int,uint16_t){
  shapes.push_back({xx,yy,w,h});
  if(xx==10&&yy==107)++chatRects;
  if(xx==119&&yy==13){assert(w==103&&h==57);++clockRects;}
 }
 template<class...T>void drawRoundRect(T...){}
 void setFont(const int*f){font=f;}template<class...T>void setTextColor(T...){}
 int textWidth(const String&s){return int(s.size())*(font==&fonts::Font7?18:font==&fonts::Font0?6:8);}
 int fontHeight(){return font==&fonts::Font0?8:font==&fonts::Font7?48:font==&fonts::Font4?26:16;}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 void print(const String&s){assert(x>=0&&y>=0&&x+textWidth(s)<=240&&y<135);if(s=="REC"){assert(font==&fonts::Font2&&x>=154&&x+textWidth(s)<=216&&y>=105&&y+fontHeight()<=126);}printed.push_back(s);}
}gCanvas;
int badges=0,batteries=0,meters=0;
void drawHomeReadyStatus(bool,bool){++badges;}
void drawHomeBatteryStatus(int32_t){++batteries;}
void drawAssistantPulseArc(int,int,int,int,float a,float b,uint16_t c){gCanvas.arcs.push_back(c);gCanvas.shapes.push_back({int(a*1000),int(b*1000)});}
void renderLauncherOverlay(){}void renderDebugOverlay(){}void ensureDeviceTimezone(){}
String fitCurrentFontToWidth(const String&s,int w){return s.substr(0,w/8);}
String tailUtf8ToFit(const String&s,int w){return s.substr(0,w/8);}
uint32_t playbackProgressPct(){return 10;}uint32_t playbackRemainingMs(){return 1000;}
String formatPlaybackMs(uint32_t){return "00:01";}
struct{int visibleLevel(bool,uint32_t){return 30;}}gMicInputMeter;
namespace mic_input_meter {template<class...T>void draw(T&&...){++meters;}}
namespace clock_display {void format(char*p,size_t n,int h,int m,bool){std::snprintf(p,n,"%02d:%02d",h,m);}}
bool candidateAvailable=false,gSdReady=true,gOtaPendingVerify=false,gOtaConfirmedThisBoot=false;
bool gPendingOtaCheckAction=false,gInboxHttpBusy=false;
struct{bool parsed=true,present=true;String version="0.2.178-dev",notes,error;int minBattery=25;unsigned size=2000000;}gOtaManifest;
String gOtaRunningSlot="app1",gOtaBootState="valid";
bool homeFirmwareUpdateAvailable(){return candidateAvailable;}
void renderSdRequiredBanner(const char*){}
struct FirmwareCapacityInfo{unsigned usableBytes=3000000,freeBytes=900000,reserveBytes=65536;};
FirmwareCapacityInfo firmwareCapacityInfo(){return {};}
String compactFlashKb(unsigned){return "900k";}String compactBytes(unsigned){return "2M";}
void updateBatterySnapshot(){}bool batteryExternalPowerPresent(){return false;}
template<class...T>void drawServiceOrb(T...){}
template<class...T>void drawInfoRow(T...){}
'''
for signature in ['AssistantPulseState assistantPulseState()', 'void drawAssistantPulseButton(', 'void renderAssistantPulseChatHint()',
                  'bool assistantPulseWorking(', 'void renderAssistantPulseUi()', 'void renderOtaUi()']:
    code += function(signature) + '\n'
code += r'''
void reset(){gCanvas=Canvas{};badges=batteries=meters=0;}
bool printed(const char*s){return std::find(gCanvas.printed.begin(),gCanvas.printed.end(),s)!=gCanvas.printed.end();}
std::vector<std::vector<int>> frame(uint32_t at,uint8_t voice){nowMs=at;gVoiceLevel8=voice;reset();renderAssistantPulseUi();nowMs=13000;return gCanvas.shapes;}
// Same picture at another time and at another voice level.
bool still(){const auto first=frame(20000,200);return first==frame(23700,40);}
void checkMotion(){
 assert(still());
 for(bool*on:{&gRecording,&gPlaybackActive,&gRealtimeActive,&gRealtimeRecording}){
  *on=true;assert(still());assert(meters==0);*on=false;}
 gRealtimeActive=gRealtimeConnected=true;assert(still());gRealtimeConnected=false;
 gRealtimeAwaiting=true;assert(!still());gRealtimeActive=gRealtimeAwaiting=false;
 for(bool*on:{&gThinking,&gSubmitting,&gVoiceReplyPending,&gVoiceReplySpeechBusy,&gVoiceCapturePreparing,&gG0VoicePending}){
  *on=true;assert(!still());*on=false;}
 gQueuedVoiceReplySpeech="Four";assert(!still());gQueuedVoiceReplySpeech.clear();
 using State=firmware_job_display::State;
 for(State job:{State::Building,State::Working}){gFirmwareJobState=job;assert(!still());}
 for(State job:{State::Ready,State::Attention,State::Failed,State::Stalled,State::Unconfirmed,State::AwaitingInput,State::TaskError}){gFirmwareJobState=job;assert(still());}
 gFirmwareJobState=State::Idle;
 // Working while recording keeps the live mic meter.
 gFirmwareJobState=State::Working;gRecording=true;frame(20000,64);assert(meters==1);gRecording=false;gFirmwareJobState=State::Idle;
 assert(still());gVoiceLevel8=64;
}
int main(){
 checkMotion();
 reset();renderAssistantPulseUi();assert(gCanvas.clockRects==1&&gCanvas.chatRects==1&&badges==1&&batteries==1);
 assert(printed("> type to chat"));
 for(bool pending:{false,true}){
  gVoiceCapturePreparing=!pending;gG0VoicePending=pending;gInputBuffer="saved draft";
  reset();renderAssistantPulseUi();
  assert(printed("Loading voice...")&&!printed("REC")&&!printed("00:12"));
  assert(gCanvas.clockRects==1&&gInputBuffer=="saved draft");
 }
 gVoiceCapturePreparing=gG0VoicePending=false;
 gRecording=true;gInputBuffer="saved draft";reset();renderAssistantPulseUi();
 assert(gCanvas.clockRects==1&&gCanvas.chatRects==0&&batteries==0&&badges==0&&printed("00:12"));
 assert(!printed("saved draft")&&!printed("> type to chat"));
 assert(printed("REC")&&!printed("RECORDING")&&!printed("Loading voice..."));
 nowMs=gRecordStartMs+6000000;reset();renderAssistantPulseUi();assert(printed("99:00"));nowMs=13000;
 for(uint16_t color:{rgb565_local(255,63,85),rgb565_local(255,111,128)})
  assert(std::find(gCanvas.arcs.begin(),gCanvas.arcs.end(),color)!=gCanvas.arcs.end());
 gRecording=false;gVoiceNoticeVisible=true;gVoiceRequestPhase=VoiceRequestPhase::Sending;
 reset();renderAssistantPulseUi();
 assert(gCanvas.clockRects==1&&gCanvas.chatRects==1&&badges==1&&batteries==1);
 assert(printed("Sending voice."));assert(!printed("AI AGENT")&&!printed("THINK"));
 assert(gInputBuffer=="saved draft");
 gVoiceNoticeVisible=false;gVoiceRequestPhase=VoiceRequestPhase::Accepted;
 reset();renderAssistantPulseUi();assert(badges==1&&batteries==1);
 // Firmware state never exposes three actions or manual refresh commands.
 gInputBuffer.clear();candidateAvailable=true;reset();renderOtaUi();
 assert(printed("Enter  Install update"));
 for(const char*s:{"fetch","apply","check","confirm","rollback"})assert(!printed(s));
 candidateAvailable=false;reset();renderOtaUi();
 assert(!printed("Enter  Install update")&&printed("Up to date; checks every 5s"));
 gOtaPendingVerify=true;reset();renderOtaUi();assert(printed("Verifying installed update"));
 gOtaPendingVerify=false;gOtaManifest.error="network unavailable";reset();renderOtaUi();
 assert(printed("Check failed; retrying"));
}
'''
with tempfile.TemporaryDirectory(prefix='s1-unified-home-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'bots/CardputerADV/src'), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS actual Home/OTA renderers: one scene, red recording, clock/timer, hidden draft/battery, compact progress, one Enter action, still unless thinking or working')
