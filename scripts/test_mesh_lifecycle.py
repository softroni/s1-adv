#!/usr/bin/env python3
"""Compile actual Mesh lifecycle functions against controlled hardware/tasks."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PARTS=ROOT/'bots/CardputerADV/src/main_parts'
def function(file,signature):
    s=(PARTS/file).read_text();start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]

enum=(PARTS/'002_main.cpp.inc').read_text().split('enum class UiMode',1)[1].split('};',1)[0]
code=r'''
#include "mesh_contacts.h"
#include "mesh_help.h"
#include "mesh_leaf_state.h"
#include "mesh_outbox.h"
#include <cassert>
#include <cctype>
using std::max;using std::min;
bool radioBusy(){return false;}
bool vlcBusy(){return false;} namespace vlc_remote{void cancel(){}}
void stopInternetRadio(){}
bool synthStopOk=true;bool stopPocketSynth(){return synthStopOk;}
template<class T>bool deferInternetRadioMode(T){return false;}
template<class T>bool deferVlcRemoteMode(T){return false;}
#include <atomic>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
struct String:std::string {
 using std::string::string;using std::string::operator=;
 String(const std::string&s):std::string(s){} String(int n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
};
uint32_t clockMs=1000;uint32_t millis(){return clockMs;}
constexpr int HIGH=1,OUTPUT=1,CARD_NONE=0,pdPASS=1,RADIOLIB_ERR_UNKNOWN=-1;
constexpr int kRfidCsPin=5,kSdCsPin=12,kSdSckPin=40,kSdMisoPin=39,kSdMosiPin=14,kSdFrequencyHz=4000000;
int live=0,sdEnds=0,sdBegins=0,spiBegins=0,updates=0,renders=0,initError=0,taskCreates=0;
bool workerPending=false,taskSucceeds=true,sdSucceeds=true;
void(*workerFunction)(void*)=nullptr;void* workerArg=nullptr;
bool gMeshBusOwned=false,gMeshCancelStart=false,gMeshExitAfterStop=false,gLauncherVisible=false;
bool gSdReady=true,gStorageReady=true,gLittleFsReady=true;
bool gBootBackgroundActive=false,gRecording=false,gSubmitting=false,gThinking=false,gPlaybackActive=false;
bool gTransferUiActive=false,gOtaApplyInProgress=false,gInboxHttpBusy=false,gVoiceDialBusy=false;
bool gVoiceReplySpeechBusy=false,gRealtimeActive=false,gRealtimeDialBusy=false,gRealtimeExclusiveMode=false;
bool gPendingRealtimeStart=false,gG0VoicePending=false,gVoiceCapturePreparing=false,gCanvasSpriteActive=true;
void* gTextTurnTaskHandle=nullptr;void* gCompletedTextTurn=nullptr;
String gActiveTextTurn,gQueuedVoiceReplySpeech,gMeshNotice;
std::vector<String>gPendingTextTurns;
uint32_t gMeshNoticeUntilMs=0;
struct {uint32_t free=128000;uint32_t getFreeHeap(){return free;}}ESP;
constexpr int MALLOC_CAP_8BIT=1;uint32_t largest=64000;
uint32_t heap_caps_get_largest_free_block(int){return largest;}
std::array<int,49>pins{};
void digitalWrite(int p,int v){assert((p==5||p==12)&&v==HIGH);pins[p]=v;}
void pinMode(int p,int v){assert((p==5||p==12)&&v==OUTPUT&&pins[p]==HIGH);}
struct Spi {void begin(int a,int b,int c,int d){assert(a==40&&b==39&&c==14&&d==12);assert(pins[5]==HIGH&&pins[12]==HIGH);++spiBegins;}}gSdSpi;
namespace fs {struct FS{};}
struct Sd:fs::FS {
 void end(){++sdEnds;}
 bool begin(int cs,Spi&,int hz){assert(cs==12&&hz==4000000&&!gMeshBusOwned&&live==0);++sdBegins;return sdSucceeds;}
 int cardType(){return 1;}
}SD;
fs::FS LittleFS;
namespace m5 {
struct I2C_Class {
 bool enabled=true;int sda=8,scl=9,writes=0,failWrite=0;std::array<uint8_t,256>reg{};
 bool isEnabled()const{return enabled;}int getSDA()const{return sda;}int getSCL()const{return scl;}
 bool readRegister(int a,int r,uint8_t*b,size_t n,int hz){assert(a==0x43&&n==1&&hz==100000);if(!enabled)return false;*b=reg[r];return true;}
 bool writeRegister8(int a,int r,uint8_t b,int hz){assert(a==0x43&&hz==100000);++writes;if(failWrite==writes)return false;reg[r]=b;return true;}
};I2C_Class In_I2C,Ex_I2C;
}
m5::I2C_Class* gMeshCapBus=nullptr;uint8_t gMeshCapBits[3]={};bool gMeshCapBitsSaved=false;
std::array<uint8_t,64> nv{};bool nvPresent=false,nvOpen=true,nvWritable=true;int nvWrites=0;
struct Preferences {
 bool begin(const char*s,bool ro){assert(std::string(s)=="s1mesh"&&!ro);return nvOpen;}
 size_t getBytesLength(const char*){return nvPresent?64:0;}
 size_t getBytes(const char*,uint8_t*b,size_t n){if(!nvPresent)return 0;memcpy(b,nv.data(),n);return n;}
 size_t putBytes(const char*,const uint8_t*b,size_t n){++nvWrites;if(!nvWritable)return 0;memcpy(nv.data(),b,n);nvPresent=true;return n;}
 void end(){}
};
using meshtastic_PortNum=int;
namespace libmeshtastic_leaf {
using NodeNum=uint32_t;
struct MeshPacket{};
struct MeshCryptoPKI {
 static bool isUsablePublicKey(const uint8_t*p){return p[0]!=0;}
 static bool regeneratePublicKey(uint8_t*out,const uint8_t*p){memcpy(out,p,32);return p[0]!=0;}
};
struct libmeshtastic_leaf {
 static bool generateKeyPair(uint8_t*p,uint8_t*s){memset(p,42,32);memset(s,42,32);return true;}
};
}
uint32_t nextSendId=100;
struct Sent{int port;uint32_t to;bool pki,wantAck;uint8_t key0;std::string data;};
struct FakeMesh {
 bool pendingAck=false,tx=false;int lastResult=0,nodeInfos=0;std::vector<Sent>sent;
 void setMyPublicKey(const uint8_t*p){assert(p[0]==42);}void setMyPrivateKey(const uint8_t*p){assert(p[0]==42);}
 uint32_t getNodeNum()const{return 42;}
 uint32_t record(int port,const uint8_t*d,size_t n,uint32_t to,bool pki,const uint8_t*key,bool ack){
  assert(gMeshBusOwned&&!gSdReady&&n<=200);if(!nextSendId)return 0;
  sent.push_back({port,to,pki,ack,key?key[0]:uint8_t(0),std::string(reinterpret_cast<const char*>(d),n)});
  pendingAck=ack;tx=true;return nextSendId++;
 }
 uint32_t sendData(int port,const uint8_t*d,size_t n,uint32_t to,bool ack){return record(port,d,n,to,false,nullptr,ack);}
 uint32_t sendDataPKI(int port,const uint8_t*d,size_t n,uint32_t to,const uint8_t key[32],bool ack){return record(port,d,n,to,true,key,ack);}
 int getLastSendResult()const{return lastResult;}
 bool hasPendingAck()const{return pendingAck;}bool isTransmitting()const{return tx;}
 uint32_t sendNodeInfo(){++nodeInfos;return 1;}
 void onReceive(void(*)(const libmeshtastic_leaf::MeshPacket&)){}
 void onReceivePKI(bool(*)(uint32_t,uint8_t*)){}
 void update(){assert(gMeshBusOwned&&!gSdReady);++updates;}
};
struct {uint8_t meshRegion=1;}gDeviceSettings;
namespace s1_mesh {
constexpr int kInitPending=-32768;
inline const char* regionName(uint8_t){return "US";}
inline float regionFrequency(uint8_t){return 906.875f;}
enum class Use:uint8_t{Chat,Scan};
constexpr int8_t kTxPowerDbm=20;
struct RadioSession {
 FakeMesh mesh;State state;std::atomic<int>initResult{kInitPending};
 bool readyReported=false;uint32_t lowHeap=UINT32_MAX,lowLargest=UINT32_MAX;uint8_t region;Use use;
 Contacts contacts;Outbox outbox;Help help;Where where;uint32_t nextSeq=1;
 char longName[kNameBytes+1]="S1-ADV";char shortName[5]="S1";
 bool historyDirty=false,announce=false;uint32_t savedMs=0,publicLocationMs=0,stopAskedMs=0;
 // The radio starts on whatever region Settings holds at that moment.
 RadioSession(Spi&,uint8_t saved,Use purpose):region(saved),use(purpose){assert(saved==gDeviceSettings.meshRegion);++live;}
 ~RadioSession(){assert(!workerPending);--live;}
 int initialize(){assert(gMeshBusOwned&&!gSdReady);return initError;}
};
}
std::unique_ptr<s1_mesh::RadioSession>gMeshSession;
int xTaskCreate(void(*fn)(void*),const char*,int stack,void*arg,int priority,void*) {
 assert(stack==8192&&priority==1&&gMeshBusOwned&&!gSdReady);++taskCreates;
 if(!taskSucceeds){return 0;}
 workerFunction=fn;workerArg=arg;workerPending=true;return pdPASS;
}
void vTaskDelete(void*p){assert(p==nullptr);workerPending=false;}
void render(){++renders;}
template<class...T>void logf(const char*,T...){ }
void setStatus(const String&,int){}
struct Keyboard_Class {struct KeysState {bool ctrl=false,enter=false,del=false;};};
int scanServices=0,scanKeys=0;
void serviceRadioScan(s1_mesh::RadioSession& s){assert(gMeshBusOwned&&!gSdReady&&s.use==s1_mesh::Use::Scan);++scanServices;}
bool handleRadioScanKey(char,const Keyboard_Class::KeysState&){++scanKeys;return true;}
void startMeshMode(s1_mesh::Use use=s1_mesh::Use::Chat);
String translateInputChar(char c){return String(1,c);}
void meshHandlePacket(const libmeshtastic_leaf::MeshPacket&){}
// Part 090's hardware: the GPS, NVS, LittleFS, the speaker and the screen.
bool gpsOn=false;s1_mesh::Where fakeWhere;
void meshGps(bool on){gpsOn=on;}
void meshReadGps(uint32_t,s1_mesh::Where& w){if(fakeWhere.known)w=fakeWhere;}
bool meshGpsHearing(uint32_t){return true;}
std::string savedName;int nameSaves=0,profileLoads=0,contactSaves=0,historyLoads=0,historyWrites=0,chimes=0,screenServices=0;
void meshSavedName(char* l,char* s){strcpy(l,savedName.empty()?"S1-ADV":savedName.c_str());strcpy(s,"S1");}
bool meshSaveName(const char* n){savedName=n;++nameSaves;return true;}
void meshLoadProfile(s1_mesh::RadioSession&){++profileLoads;}
bool meshSaveContacts(const s1_mesh::Contacts&){++contactSaves;return true;}
void meshLoadHistory(s1_mesh::State&){++historyLoads;}
bool meshWriteHistory(const s1_mesh::State&){++historyWrites;return true;}
void meshChime(bool){++chimes;}
void meshScreenService(uint32_t,bool){++screenServices;}
void meshWakeScreen(){}
void setUiMode(UiMode);
'''
code=code.replace('void setUiMode(UiMode);','enum class UiMode'+enum+'};\nUiMode gUiMode=UiMode::Mesh;\nvoid setUiMode(UiMode);')
for signature in ['bool mountSdCardOnSharedSpi()','fs::FS* activeVoiceFs()','fs::FS* activeNotesFs()']:
    file='032_main.cpp.inc' if signature.startswith('bool mount') else '011_main.cpp.inc'
    code+=function(file,signature)+'\n'
code+='bool stopMeshMode(bool goHome);\n'
# Every part of Mesh's logic but the packet decoder (faked above) and the screens.
mesh=(PARTS/'068_main.cpp.inc').read_text()
body=mesh[mesh.index('namespace {')+len('namespace {'):mesh.index('String meshClip(')]
code+=body[:body.index('void meshHandlePacket(')]+body[body.index('void serviceMeshMode('):body.index('bool handleMeshHostCommand(')]+'\n'
actual=function('024_main.cpp.inc','void setUiMode(')
code+=actual[:actual.index('  if (mode != UiMode::Face)')]+ 'gUiMode=mode;\n}\n'
realtime=function('042_main.cpp.inc','void startRealtimeSession(')
code+='int realtimeStarts=0;\n'+realtime[:realtime.index('  handleScreensaverWake();')]+ '(void)startAfterReady;++realtimeStarts;\n}\n'
code+=r'''
void reset() {
 assert(!gMeshBusOwned&&!workerPending&&live==0);
 gUiMode=UiMode::Mesh;gSdReady=true;gStorageReady=true;gLittleFsReady=true;
 ESP.free=128000;largest=64000;gCanvasSpriteActive=true;taskSucceeds=sdSucceeds=true;initError=0;
 m5::In_I2C={};m5::Ex_I2C={};m5::Ex_I2C.enabled=false;
 m5::In_I2C.reg[3]=0xaa;m5::In_I2C.reg[5]=0x54;m5::In_I2C.reg[7]=0xff;
 nvPresent=false;nvOpen=nvWritable=true;nvWrites=0;
 sdEnds=sdBegins=spiBegins=updates=renders=taskCreates=0;nextSendId=100;
}
void completeWorker();
void active();
void restored();
// Contacts, direct messages, Help, the name and sharing a location, through the keys.
void mesh_features() {
 Keyboard_Class::KeysState plain,ctrl,enter,del;ctrl.ctrl=true;enter.enter=true;del.del=true;
 uint8_t key[32];memset(key,7,32);
 // The People list: contacts first, then radios heard. Only one with a key can be saved.
 active();auto& s=*gMeshSession;
 auto* heard=s.state.hear(77,clockMs,-80);strcpy(heard->name,"Alex");
 s.state.hear(88,clockMs,-90);
 assert(handleMeshKey('n',ctrl)&&s.state.nodeView);
 assert(s.state.nodeSel==0&&handleMeshKey('\n',enter)&&!s.contacts.count&&gMeshNotice.find("No key")!=std::string::npos);
 heard->hasKey=true;memcpy(heard->key,key,32);
 const int saves=contactSaves;assert(handleMeshKey('\n',enter)&&s.contacts.count==1&&contactSaves==saves+1);
 assert(s.contacts.list[0].id==77&&std::string(s.contacts.list[0].name)=="Alex");
 assert(handleMeshKey(char(2),plain)&&s.state.nodeSel==1&&handleMeshKey(char(2),plain)&&s.state.nodeSel==0);
 // Left and Right pick who Enter sends to: Public, then each contact.
 assert(handleMeshEscape()&&!s.state.nodeView&&gMeshBusOwned);
 assert(handleMeshKey(char(4),plain)&&s.state.target==1&&handleMeshKey(char(4),plain)&&s.state.target==0);
 assert(handleMeshKey(char(3),plain)&&s.state.target==1);
 assert(handleMeshKey('h',plain)&&handleMeshKey('i',plain)&&handleMeshKey('\n',enter));
 for(int i=0;i<3;++i)serviceMeshMode();
 // A direct message: encrypted to Alex's key, asking for an answer.
 auto& dm=s.mesh.sent.back();assert(dm.to==77&&dm.pki&&dm.wantAck&&dm.key0==7&&dm.data=="hi"&&dm.port==1);
 assert(s.state.bySeq(1)->direct&&s.state.bySeq(1)->to==77);
 // Alex's radio answers: delivered.
 struct Sink{s1_mesh::RadioSession& s;void status(uint32_t q,s1_mesh::Delivery d,uint8_t r,uint32_t id){
  if(auto* m=s.state.bySeq(q)){m->status=d;m->reason=r;if(id)m->id=id;}}} sink{s};
 assert(s.outbox.routing(77,s.outbox.currentId(),0,sink)&&s.state.bySeq(1)->status==s1_mesh::Delivery::Delivered);
 s.mesh.pendingAck=false;s.mesh.tx=false;
 // Removing a contact moves Enter back to the public channel.
 assert(handleMeshKey('n',ctrl)&&s.state.nodeSel==0&&handleMeshKey('\b',del)&&!s.contacts.count&&s.state.target==0);
 assert(handleMeshEscape());
 // Ctrl+L: nothing without a fix, and the public channel asks twice.
 const size_t before=s.mesh.sent.size();
 assert(handleMeshKey('l',ctrl)&&gMeshNotice.find("No GPS")!=std::string::npos&&!s.outbox.queued());
 fakeWhere.known=true;fakeWhere.latE5=4251912;fakeWhere.lonE5=-7089645;fakeWhere.atMs=clockMs;fakeWhere.satellites=8;
 serviceMeshMode();
 assert(handleMeshKey('l',ctrl)&&!s.outbox.queued()&&gMeshNotice.find("again")!=std::string::npos);
 assert(handleMeshKey('l',ctrl)&&s.outbox.queued()==2);
 serviceMeshMode();assert(s.mesh.sent.size()==before+1&&s.mesh.sent.back().port==3&&!s.mesh.sent.back().wantAck);
 s.mesh.tx=false;serviceMeshMode();serviceMeshMode();
 assert(s.mesh.sent.back().port==1&&s.mesh.sent.back().data.find("maps.google.com/?q=42.51912,-70.89645")!=std::string::npos);
 s.mesh.pendingAck=false;s.mesh.tx=false;serviceMeshMode();
 // Help: to every contact, then again in five minutes until a contact's radio has it.
 s.contacts.add(77,"Alex",key);s.contacts.add(99,"Sam",key);
 assert(handleMeshKey('h',ctrl)&&s.state.helpView&&handleMeshKey('p',plain)&&s.help.alsoPublic&&handleMeshKey('p',plain));
 assert(handleMeshKey('1',plain)&&s.help.on&&s.help.repeating());
 const size_t start=s.mesh.sent.size();serviceMeshMode();
 assert(s.help.rounds==1&&s.mesh.sent.size()==start+1&&s.mesh.sent.back().to==77);
 assert(s.mesh.sent.back().data.rfind("Out of gas.",0)==0&&!s.help.seq[s1_mesh::kContacts]);
 s.outbox.routing(77,s.outbox.currentId(),0,sink);s.mesh.pendingAck=false;s.mesh.tx=false;serviceMeshMode();
 assert(s.mesh.sent.back().to==99);
 // The session's own sink is what Help hears from: Sam's radio doesn't answer, Alex's did.
 s.mesh.pendingAck=false;s.mesh.tx=true;serviceMeshMode();s.mesh.tx=false;clockMs+=s1_mesh::kLastTryWaitMs;serviceMeshMode();
 assert(s.state.bySeq(s.help.seq[1])->status==s1_mesh::Delivery::NoAnswer);
 // A Help that's on warns before Esc turns the radio off.
 assert(handleMeshEscape()&&!s.state.helpView&&handleMeshEscape()&&gMeshBusOwned&&gMeshNotice.find("Help is on")!=std::string::npos);
 clockMs+=s1_mesh::kHelpRepeatMs;serviceMeshMode();assert(s.help.rounds==2);
 // Delivered through the session's sink ends it, with a chime.
 const int rang=chimes;serviceMeshMode();
 MeshSink real{s};real.status(s.help.seq[0],s1_mesh::Delivery::Delivered,0,0);
 assert(!s.help.repeating()&&s.help.stop==s1_mesh::HelpStop::Confirmed&&chimes==rang+1);
 // A radio without this one's key asks for it to be announced again.
 real.status(s.help.seq[1],s1_mesh::Delivery::Failed,35,0);assert(s.announce);
 s.mesh.pendingAck=false;s.mesh.tx=false;
 for(int i=0;i<200&&!s.outbox.idle();++i){s.mesh.pendingAck=false;s.mesh.tx=false;clockMs+=40000;serviceMeshMode();}
 serviceMeshMode();assert(!s.announce&&s.mesh.nodeInfos==1);
 assert(handleMeshEscape());restored();assert(gUiMode==UiMode::Home&&!gpsOn);
 fakeWhere=s1_mesh::Where{};
 // H on the start screen starts the radio on Help.
 reset();assert(handleMeshKey('H',plain)&&gMeshBusOwned&&gMeshSession->state.helpView&&profileLoads>0&&historyLoads>0);
 completeWorker();serviceMeshMode();assert(gpsOn);
 assert(handleMeshKey('5',plain)&&!gMeshSession->help.on);  // Only 1 to 4.
 assert(handleMeshEscape()&&!gMeshSession->state.helpView&&handleMeshEscape());restored();
 // N on the start screen: the name others see. Esc leaves it as it was.
 reset();assert(handleMeshKey('n',plain)&&gMeshNaming&&!gMeshBusOwned&&!gMeshNameInput[0]);
 for(char c:std::string("Al x"))handleMeshKey(c,plain);
 assert(handleMeshKey('\b',del)&&std::string(gMeshNameInput)=="Al ");
 assert(handleMeshKey('\n',enter)&&!gMeshNaming&&savedName=="Al"&&nameSaves==1);
 assert(handleMeshKey('n',plain)&&std::string(gMeshNameInput)=="Al");
 assert(handleMeshEscape()&&!gMeshNaming&&nameSaves==1);
 assert(handleMeshKey('n',plain)&&gMeshNaming);
 for(int i=0;i<60;++i)handleMeshKey('z',plain);
 assert(strlen(gMeshNameInput)==s1_mesh::kNameBytes);handleMeshEscape();
}
void completeWorker(){assert(workerPending);workerFunction(workerArg);assert(!workerPending);}
void active(){reset();startMeshMode();assert(gMeshBusOwned&&!gSdReady&&!gStorageReady&&workerPending);completeWorker();serviceMeshMode();assert(updates==1);}
void restored(){assert(!gMeshBusOwned&&gSdReady&&gStorageReady&&live==0&&sdBegins==1);assert(m5::In_I2C.reg[3]==0xaa&&m5::In_I2C.reg[5]==0x54&&m5::In_I2C.reg[7]==0xff);}
int main() {
 synthStopOk=false;startRealtimeSession(true);assert(realtimeStarts==0);synthStopOk=true;
 reset();
 bool* flags[]={&gBootBackgroundActive,&gRecording,&gSubmitting,&gThinking,&gPlaybackActive,&gTransferUiActive,
 &gOtaApplyInProgress,&gInboxHttpBusy,&gVoiceDialBusy,&gVoiceReplySpeechBusy,&gRealtimeActive,
 &gRealtimeDialBusy,&gRealtimeExclusiveMode,&gPendingRealtimeStart,&gG0VoicePending,&gVoiceCapturePreparing};
 for(auto* p:flags){*p=true;startMeshMode();assert(!live&&!sdEnds&&!taskCreates&&!m5::In_I2C.writes);*p=false;}
 ESP.free=65535;startMeshMode();assert(!live&&!sdEnds);ESP.free=128000;
 largest=24575;startMeshMode();assert(!live&&!sdEnds);largest=64000;
 gCanvasSpriteActive=false;startMeshMode();assert(!live&&!sdEnds);gCanvasSpriteActive=true;
 nvOpen=false;startMeshMode();assert(!live&&!sdEnds);nvOpen=true;
 nvWritable=false;startMeshMode();assert(!live&&!sdEnds);nvWritable=true;
 m5::In_I2C.enabled=false;startMeshMode();assert(!live&&!sdEnds);m5::In_I2C.enabled=true;
 for(int fail=1;fail<=3;++fail){reset();m5::In_I2C.failWrite=fail;startMeshMode();assert(!live&&!sdEnds&&!gMeshCapBitsSaved);assert(m5::In_I2C.reg[3]==0xaa&&m5::In_I2C.reg[5]==0x54&&m5::In_I2C.reg[7]==0xff);}
 reset();startMeshMode();assert(gMeshBusOwned&&live==1&&workerPending);
 const int begins=spiBegins;assert(!mountSdCardOnSharedSpi()&&spiBegins==begins);
 assert(activeVoiceFs()==nullptr&&activeNotesFs()==nullptr);
 assert(!stopMeshMode(true)&&workerPending&&live==1&&sdBegins==0);
 completeWorker();serviceMeshMode();restored();assert(gUiMode==UiMode::Home);
 active();startRealtimeSession(true);startRealtimeSession(false);assert(realtimeStarts==0);
 gLauncherVisible=true;startRealtimeSession(true);assert(realtimeStarts==0);gLauncherVisible=false;
 Keyboard_Class::KeysState key;
 assert(handleMeshKey('h',key)&&handleMeshKey('i',key)&&std::string(gMeshSession->state.input)=="hi");
 key.ctrl=true;assert(handleMeshKey('n',key)&&gMeshSession->state.nodeView);
 assert(handleMeshKey('o',key)&&gMeshSession->state.detailView&&!gMeshSession->state.nodeView);
 assert(handleMeshKey('r',key)&&gMeshBusOwned);key.ctrl=false;key.del=true;
 assert(handleMeshKey('\b',key)&&std::string(gMeshSession->state.input)=="h");key.del=false;key.enter=true;
 assert(handleMeshKey('\n',key)&&gMeshSession->state.messageCount==1&&!gMeshSession->state.input[0]);
 stopMeshMode(false);restored();
 startRealtimeSession(false);assert(realtimeStarts==1);
 reset();taskSucceeds=false;startMeshMode();restored();
 reset();initError=-19;startMeshMode();completeWorker();serviceMeshMode();restored();assert(gMeshNotice.find("-19")!=std::string::npos);
 for(UiMode mode:{UiMode::Voice,UiMode::Library,UiMode::AudioFolders,UiMode::Notes,UiMode::Assets,UiMode::Files,UiMode::Nes,UiMode::RfidLab,UiMode::Sonar}) {
   active();setUiMode(mode);assert(gUiMode==UiMode::Mesh&&gMeshBusOwned&&sdBegins==0);assert(stopMeshMode(false));restored();
 }
 for(UiMode mode:{UiMode::Home,UiMode::Calculator,UiMode::Chess,UiMode::Checkers,UiMode::Focus,UiMode::Settings}) {
   active();setUiMode(mode);restored();assert(gUiMode==mode);
 }
 active();gMeshSession->state.detailView=true;assert(handleMeshEscape());
 assert(!gMeshSession->state.detailView&&gMeshBusOwned&&sdBegins==0);
 gLauncherVisible=true;assert(!handleMeshEscape()&&gMeshBusOwned);gLauncherVisible=false;
 assert(handleMeshEscape());restored();assert(gUiMode==UiMode::Home&&!handleMeshEscape());
 reset();startMeshMode();assert(handleMeshEscape()&&workerPending&&live==1&&sdBegins==0);
 completeWorker();serviceMeshMode();restored();assert(gUiMode==UiMode::Home);
 active();
 for(size_t i=0;i<s1_mesh::kOutbox;++i){assert(gMeshSession->state.append("m")&&meshSendInput());}
 assert(gMeshSession->state.append("hello"));assert(!meshSendInput());
 assert(gMeshSession->state.messageCount==s1_mesh::kMessages&&std::string(gMeshSession->state.input)=="hello");
 assert(gMeshSession->state.messages[0].outgoing&&gMeshSession->state.messages[0].status==s1_mesh::Delivery::Waiting);
 const int writes=historyWrites;
 for(int i=0;i<50;++i){serviceMeshMode();}
 assert(historyWrites==writes+1);  // Saved once, though it changed again: at most once a second.
 // One goes out, on the public channel, asking for an answer; the rest wait their turn.
 assert(gMeshSession->mesh.sent.size()==1&&gMeshSession->mesh.sent[0].to==s1_mesh::kBroadcast&&gMeshSession->mesh.sent[0].wantAck);
 assert(gMeshSession->state.bySeq(1)->status==s1_mesh::Delivery::Sending&&gMeshSession->state.bySeq(1)->id==100);
 // Another radio repeats it: heard, and the next one goes.
 gMeshSession->mesh.pendingAck=false;gMeshSession->mesh.tx=false;serviceMeshMode();
 assert(gMeshSession->state.bySeq(1)->status==s1_mesh::Delivery::Heard);serviceMeshMode();assert(gMeshSession->mesh.sent.size()==2);
 clockMs+=1000;serviceMeshMode();assert(historyWrites==writes+2);
 assert(sdBegins==0);stopMeshMode(false);restored();
 mesh_features();
 active();sdSucceeds=false;stopMeshMode(false);assert(!gMeshBusOwned&&!gSdReady&&gStorageReady&&!live);assert(activeVoiceFs()==&LittleFS);assert(activeNotesFs()==&LittleFS);
 // Scan: S on the start screen. Same bus handoff and guards, no identity written, no mesh run,
 // no sending, and Esc stops it the same way.
 reset();Keyboard_Class::KeysState plain;assert(handleMeshKey('s',plain));
 assert(gMeshBusOwned&&live==1&&workerPending&&gMeshSession->use==s1_mesh::Use::Scan&&nvWrites==0);
 assert(handleMeshKey('x',plain)&&scanKeys==0);  // Keys wait until the radio is ready.
 completeWorker();serviceMeshMode();assert(scanServices==1&&updates==0);
 assert(handleMeshKey('x',plain)&&scanKeys==1&&!gMeshSession->state.input[0]);
 plain.ctrl=true;assert(!handleMeshKey('n',plain)&&!gMeshSession->state.nodeView);plain.ctrl=false;
 assert(gMeshSession->state.append("hi")&&!meshSendInput()&&gMeshSession->state.messageCount==0);
 setUiMode(UiMode::Files);assert(gUiMode==UiMode::Mesh&&gMeshBusOwned);
 assert(handleMeshEscape());restored();assert(gUiMode==UiMode::Home);
 // A scan whose radio fails to start is stopped before the scanner ever runs.
 reset();const int served=scanServices;initError=-19;handleMeshKey('S',plain);assert(gMeshSession->use==s1_mesh::Use::Scan);
 completeWorker();serviceMeshMode();restored();assert(scanServices==served&&gMeshNotice.find("-19")!=std::string::npos);
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-mode-') as directory:
    cpp=Path(directory)/'lifecycle.cpp';exe=Path(directory)/'lifecycle';cpp.write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(ROOT/'bots/CardputerADV/src'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS actual Mesh lifecycle: busy/heap/NVS/Cap/task failures, cancellation during init, SD exclusivity and rollback, app guards, bounded send, the outbox one at a time, contacts and direct messages, Help repeating until delivered and warning before Esc, Ctrl+L asking twice for public, the name editor, Scan sharing all of it without identity, mesh or sending, and no worker use-after-free (ASan/UBSan)')
