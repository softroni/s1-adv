#!/usr/bin/env python3
"""Run Radio Scan's actual sums, service loop, keys and 240x135 screen against a pretend radio.

The pretend radio has no way to transmit, so this only compiles while the scanner never sends. The
pretend mesh only lends the radio and takes it back.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
part = (ROOT / 'bots/CardputerADV/src/main_parts/087_main.cpp.inc').read_text()
# Mesh's drawing helpers, which Radio Scan's screen shares.
mesh = (ROOT / 'bots/CardputerADV/src/main_parts/068_main.cpp.inc').read_text()
drawing = mesh[mesh.index('// --- Mesh drawing'):mesh.index('// --- end of Mesh drawing')]

code = r'''
#include "launcher_icons.h"
#include "radio_scan.h"
#include "mesh_sounds.h"
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
struct String:std::string {
 using std::string::string;
 String(const std::string&s):std::string(s){}
 bool isEmpty()const{return empty();}
 void remove(size_t p,size_t n){erase(p,n);}
 String substring(size_t a,size_t b)const{return substr(a,b-a);}
};
namespace fonts {int Font0=0,Font2=2,FreeSansBold9pt7b=9,efontCN_12=12;}
constexpr int TFT_WHITE=65535,HIGH=1,LOW=0;
constexpr int RADIOLIB_ERR_NONE=0,RADIOLIB_ERR_CRC_MISMATCH=-7,RADIOLIB_ERR_SPI=-16;
int rgb565_local(int,int,int){return 1;}
uint32_t gNow=1000;uint32_t millis(){return gNow;}
void delayMicroseconds(int){}
int dio1=LOW;int digitalRead(int pin){assert(pin==4);return dio1;}
std::vector<std::string> logs;
void logf(const char* f,...){char b[256];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);logs.push_back(b);}
struct Keyboard_Class {struct KeysState {bool ctrl=false,enter=false;};};

struct Canvas {
 int font=0,x=0,y=0,calls=0;std::set<std::string> printed;
 static void check(int a,int b,int w,int h){assert(a>=0&&b>=0&&w>=0&&h>=0&&a+w<=240&&b+h<=135);}
 void fillScreen(int){printed.clear();}
 void setTextSize(int n){assert(n==1);}void setFont(int*p){font=*p;}
 void setTextColor(int,int=0){}void setCursor(int a,int b){x=a;y=b;}
 // Font2 is proportional, and 9 is wider than most of it; the bold one is wider still.
 int textWidth(const String&s)const{return int(s.size())*(font==0||font==12?6:font==9?12:9);}
 template<class T>void print(T value){String s(value);if(s.empty())return;const int w=textWidth(s);
  check(x,y,w,font==0?8:font==9?18:font==12?12:16);if(font==0)assert(x+w<=232);printed.insert(s);x+=w;++calls;}
 void fillRect(int a,int b,int w,int h,int){check(a,b,w,h);++calls;}
 void drawFastHLine(int a,int b,int w,int){check(a,b,w,1);++calls;}
 void drawFastVLine(int a,int b,int h,int){check(a,b,1,h);++calls;}
 void drawRoundRect(int a,int b,int w,int h,int,int){check(a,b,w,h);++calls;}
 void fillRoundRect(int a,int b,int w,int h,int,int){check(a,b,w,h);++calls;}
 void drawBitmap(int a,int b,const uint8_t*,int w,int h,int){check(a,b,w,h);++calls;}
}gCanvas;

namespace libmeshtastic_leaf {
constexpr size_t MAX_LORA_PAYLOAD_LEN=255;
struct MeshChannel {
 uint8_t hash=0;
 bool setChannel(const uint8_t*psk,size_t n,const char*name){assert(n==1&&psk[0]==1);hash=uint8_t(std::strlen(name));return true;}
 uint8_t getHash()const{return hash;}
};
struct ModemParams {uint8_t sf;float bw;uint8_t cr;};
}
// Every call the scanner makes, so the tests can check how it tunes. No transmit exists here.
struct Packet {std::vector<uint8_t> bytes;int result=RADIOLIB_ERR_NONE;float rssi=-90,snr=5;};
struct FakeRadio {
 bool broken=false;float freq=0,bw=0;int sf=0,cr=0,sync=0,preamble=0;bool receiving=false;
 int standbys=0,receives=0,reads=0;std::vector<float> tunes;std::deque<Packet> packets;
 float signalAt=915.125f;
 int ok(){return broken?RADIOLIB_ERR_SPI:RADIOLIB_ERR_NONE;}
 int standby(){++standbys;receiving=false;return ok();}
 int setBandwidth(float b){assert(!receiving);bw=b;return ok();}
 int setSpreadingFactor(uint8_t s){assert(!receiving);sf=s;return ok();}
 int setCodingRate(uint8_t c){assert(!receiving);cr=c;return ok();}
 int setSyncWord(uint8_t s){assert(!receiving);sync=s;return ok();}
 int setPreambleLength(size_t p){assert(!receiving);preamble=int(p);return ok();}
 int setFrequency(float f,bool skipCalibration){assert(skipCalibration&&!receiving);freq=f;tunes.push_back(f);return ok();}
 int startReceive(){++receives;receiving=!broken;return ok();}
 float getRSSI(bool packet){assert(!packet&&receiving);return broken?0.0f:std::fabs(freq-signalAt)<0.01f?-72.4f:-118.6f;}
 float getRSSI(){return packets.empty()?0:packets.front().rssi;}
 float getSNR(){return packets.empty()?0:packets.front().snr;}
 size_t getPacketLength(){return packets.empty()?0:packets.front().bytes.size();}
 int readData(uint8_t*out,size_t n){
  assert(receiving&&n==255&&!packets.empty());++reads;Packet p=packets.front();packets.pop_front();
  std::memcpy(out,p.bytes.data(),std::min(n,p.bytes.size()));dio1=LOW;return p.result;
 }
};
// The chat's side of the session: the library lends the radio and takes it back, retuned for the chat.
struct FakeMesh {
 FakeRadio* radio=nullptr;bool lent=false,resumeOk=true,tx=false,pendingAck=false,secondary=false;int lends=0,resumes=0;
 void lendRadio(){lent=true;++lends;}
 bool resumeRadio(){++resumes;if(!resumeOk)return false;lent=false;radio->freq=906.875f;radio->bw=250;radio->sf=11;radio->sync=0x2B;
  radio->preamble=16;radio->receiving=true;return true;}
 bool isTransmitting()const{return tx;}bool hasPendingAck()const{return pendingAck;}
 uint8_t getChannelHash()const{return 8;}bool hasSecondaryChannel()const{return secondary;}uint8_t getSecondaryChannelHash()const{return 77;}
};
struct FakeHelp {bool on=false;bool repeating()const{return on;}};
struct FakeOutbox {bool busy=false;bool idle()const{return !busy;}};
namespace s1_mesh {
constexpr size_t kScanPresetCount=6;
const char* names[kScanPresetCount]={"LongFast","MediumFast","MediumSlow","LongModerate","ShortFast","ShortSlow"};
uint8_t gBandRegion=1;
const char* regionName(uint8_t c){return c==1?"US":"UA_868";}
float regionFrequency(uint8_t c){return c==1?906.875f:869.525f;}
void regionBand(uint8_t c,float&a,float&b){if(c==1){a=902;b=928;}else{a=869.4f;b=869.65f;}}
const char* scanPresetName(uint8_t i){return names[i%kScanPresetCount];}
float scanPresetFrequency(uint8_t,uint8_t i){return 906.875f+float(i);}
libmeshtastic_leaf::ModemParams scanPresetModem(uint8_t i){return {uint8_t(7+i),i==3?125.0f:250.0f,5};}
struct RadioSession {
 FakeRadio radio;uint8_t region=1;s1_scan::Scanner scan;uint32_t lowHeap=1,lowLargest=1;
 bool scanning=false;s1_scan::Listen heard;s1_scan::Strip line;uint32_t sampleAt=0;
 FakeMesh mesh;FakeHelp help;FakeOutbox outbox;
 // As Mesh leaves it once the radio is up: the chat's channel, receiving.
 RadioSession(){mesh.radio=&radio;radio.receiving=true;radio.freq=906.875f;radio.bw=250;line.reset(gNow);}
};
}
std::unique_ptr<s1_mesh::RadioSession> gMeshSession;
// What part 068 owns that Radio Scan reads: the screen up, the notice line, and stopping the radio.
enum class UiMode{Home,Mesh};UiMode gUiMode=UiMode::Mesh;
enum class MeshView:uint8_t{Hub,Chat,Live};MeshView gMeshView=MeshView::Live;
bool gMeshNaming=false,gMeshChannelSetup=false;
String gMeshNotice;uint32_t gMeshNoticeUntilMs=0;
int stops=0;bool stopMeshMode(bool){++stops;return true;}
// Radio Scan's sounds: a tick for each packet, at its strength, and the knock when the radio gives up.
std::vector<int16_t> ticks;std::vector<s1_mesh::Sound> sounds;
void meshPacketTick(int16_t dbm){ticks.push_back(dbm);}
void meshSound(s1_mesh::Sound s){sounds.push_back(s);}
namespace {  // As in 005: declared in the same unnamed namespace the part defines them in.
void serviceRadioScan(s1_mesh::RadioSession& session);
bool handleRadioScanKey(char key, const Keyboard_Class::KeysState& keys);
bool handleRadioScanEscape(s1_mesh::RadioSession& session);
void renderRadioScanUi(s1_mesh::RadioSession& session);
void logRadioScanStatus(s1_mesh::RadioSession& session);
void serviceRadioLive(s1_mesh::RadioSession& session, uint32_t now);
void meshHeard(const uint8_t* bytes, size_t length, float rssi, float snr);
}
''' + drawing + r'''
''' + part + r'''
using namespace s1_scan;
static Packet packet(uint32_t from,uint8_t flags,uint8_t channel,size_t extra=10){
 Packet p;p.bytes={0xff,0xff,0xff,0xff,uint8_t(from),uint8_t(from>>8),uint8_t(from>>16),uint8_t(from>>24),1,2,3,4,flags,channel,0,0};
 p.bytes.resize(16+extra,0x55);return p;
}
static void render(){renderRadioScanUi(*gMeshSession);}
static bool printedStarting(const std::string& p){for(const auto& t:gCanvas.printed)if(t.rfind(p,0)==0)return true;return false;}
static void enter(){Keyboard_Class::KeysState k;k.enter=true;assert(handleRadioScanKey('\n',k));}
static void key(char c){Keyboard_Class::KeysState k;assert(handleRadioScanKey(c,k));}
static size_t count(s1_mesh::Sound want){size_t n=0;for(auto q:sounds)n+=q==want;return n;}
// The borrowed radio's service loop over simulated time, a reading every 20 ms as the firmware does.
static void listenFor(uint32_t ms){auto&s=*gMeshSession;for(uint32_t t=0;t<ms;t+=10){gNow+=10;serviceRadioScan(s);}}
// The chat's own radio: the live reading between the library's work.
static void liveFor(uint32_t ms){auto&s=*gMeshSession;for(uint32_t t=0;t<ms;t+=10){gNow+=10;serviceRadioLive(s,gNow);}}
static void sweepOnce(){auto&s=*gMeshSession;const uint32_t passes=s.scan.sweep.passes;
 for(int i=0;i<200&&s.scan.sweep.passes==passes;++i)serviceRadioScan(s);assert(s.scan.sweep.passes==passes+1);}
static void heard(const Packet& p){meshHeard(p.result?nullptr:p.bytes.data(),p.result?0:p.bytes.size(),p.rssi,p.snr);}

int main(){
 // The sums. US is 104 LongFast channels, with its public channel exactly on one of them.
 Plan us=planFor(902,928);
 assert(us.points==104&&std::fabs(us.start-902)<1e-4&&std::fabs(us.step-0.25f)<1e-5);
 assert(std::fabs(us.frequency(19)-906.875f)<1e-3&&us.pointAt(906.875f)==19&&us.pointAt(901.9f)==104&&us.pointAt(928.0f)==104);
 assert(us.pointAt(927.99f)==103&&us.pointAt(std::nanf(""))==104);
 Plan eu=planFor(869.4f,869.65f);  // One channel wide, so its neighbours are shown too.
 assert(eu.points==32&&std::fabs(eu.start-865.525f)<1e-3&&std::fabs(eu.end()-873.525f)<1e-3);
 Plan odd=planFor(std::nanf(""),5);assert(odd.points==32);
 Plan wide=planFor(400,500);assert(wide.points==kMaxPoints);
 assert(dbmFrom(-97.4f)==-97&&dbmFrom(0.0f)==kNone&&dbmFrom(-0.4f)==kNone&&dbmFrom(std::nanf(""))==kNone&&dbmFrom(-250)==kNone);
 Header h;uint8_t bytes[16]={1,0,0,0,0xd4,0xc3,0xb2,0xa1,9,9,9,9,uint8_t((3<<5)|0x10|1),8,0,0};
 assert(!parseHeader(bytes,15,h)&&!parseHeader(nullptr,16,h)&&parseHeader(bytes,16,h));
 assert(h.to==1&&h.from==0xa1b2c3d4&&h.id==0x09090909&&h.hopLimit==1&&h.hopStart==3&&h.viaMqtt&&h.channel==8&&hopsTaken(h)==2);
 h.hopStart=0;assert(hopsTaken(h)==-1);h.hopStart=1;h.hopLimit=3;assert(hopsTaken(h)==-1);h.hopLimit=1;assert(hopsTaken(h)==0);
 {Heard d;assert(!describe(bytes,15,1,-90,5,8,-1,d)&&describe(bytes,16,1,-90.4f,5.04f,8,-1,d));
  assert(d.from==0xa1b2c3d4&&d.rssi==-90&&d.snrTenths==50&&d.hops==2&&d.publicChannel&&!d.privateChannel);
  assert(describe(bytes,16,1,-90,5,9,8,d)&&!d.publicChannel&&d.privateChannel);
  assert(describe(bytes,16,1,-90,5,9,-1,d)&&!d.publicChannel&&!d.privateChannel);}

 // Live, on the chat's own channel: the radio stays the chat's, so nothing is borrowed or tuned.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& s=*gMeshSession;
 const size_t tunesBefore=s.radio.tunes.size();
 liveFor(3000);
 assert(!s.scanning&&!s.mesh.lent&&s.radio.tunes.size()==tunesBefore&&s.line.head==6&&s.line.noise()==-119);
 // Not while it sends: then the reading would only be its own signal.
 s.mesh.tx=true;{const uint8_t head=s.line.head;const auto at=s.line.peak[head];liveFor(400);assert(s.line.peak[s.line.head]==kNone||s.line.head==head);(void)at;}
 s.mesh.tx=false;liveFor(600);
 render();assert(gCanvas.printed.count("US live")&&gCanvas.printed.count("Listening...")&&gCanvas.printed.count("Nothing yet")&&gCanvas.printed.count("0:04"));
 assert(printedStarting("Noise -119")&&printedStarting("LongFast"));
 // Every packet the library reads: the public channel, the private one, another, too short and damaged.
 ticks.clear();
 heard(packet(0x11223344,(3<<5)|1,8));assert(s.heard.packets==1&&s.heard.recent[0].publicChannel&&ticks.size()==1);
 s.mesh.secondary=true;heard(packet(0x55,(3<<5)|3,77));assert(s.heard.recent[0].privateChannel&&!s.heard.recent[0].publicChannel);
 heard(packet(0x66,(3<<5)|2,23));assert(!s.heard.recent[0].privateChannel&&!s.heard.recent[0].publicChannel);
 {Packet tiny;tiny.bytes={1,2,3};heard(tiny);Packet bad=packet(0x77,0,8);bad.result=RADIOLIB_ERR_CRC_MISMATCH;heard(bad);}
 assert(s.heard.packets==3&&s.heard.damaged==2&&s.heard.nodeCount==3&&ticks.size()==3);
 assert(s.line.mark[s.line.head]==kPacketMark);
 render();assert(gCanvas.printed.count("3 packets, 3 nodes")&&gCanvas.printed.count("pub")&&gCanvas.printed.count("priv")&&gCanvas.printed.count("other"));
 // It ticks only while Radio Scan is on the screen: the chat counts quietly.
 gMeshView=MeshView::Chat;heard(packet(0x88,(3<<5)|1,8));assert(ticks.size()==3&&s.heard.packets==4);gMeshView=MeshView::Live;

 // It won't borrow the radio while Help repeats or a message is still going out, and says so.
 sounds.clear();s.help.on=true;enter();assert(!s.scanning&&!s.mesh.lent&&gMeshNotice=="Not while Help is on"&&count(s1_mesh::Sound::Nope)==1);
 s.help.on=false;s.outbox.busy=true;key(2);assert(!s.scanning&&gMeshNotice=="Wait: a message is going out");
 s.outbox.busy=false;s.mesh.pendingAck=true;enter();assert(!s.scanning);s.mesh.pendingAck=false;
 render();assert(gCanvas.printed.count("Wait: a message is going out"));gMeshNotice="";

 // Enter: the sweep, on the borrowed radio. The bar says the chat is paused.
 sounds.clear();enter();assert(s.scanning&&s.mesh.lent&&!s.scan.listening&&s.radio.bw==250.0f&&count(s1_mesh::Sound::Removed)==1);
 assert(logs.back().find("[MESH] scan sweep US 902.000-928.000 MHz points=104")==0);
 sweepOnce();
 assert(s.radio.tunes.size()>=tunesBefore+104);
 for(uint8_t i=0;i<104;++i)assert(std::fabs(s.radio.tunes[tunesBefore+i]-us.frequency(i))<1e-3);
 const uint8_t busy=us.pointAt(915.125f);
 assert(s.scan.sweep.now[busy]==-72&&s.scan.sweep.floor(s.scan.plan)==-119&&s.scan.sweep.strongest(s.scan.plan)==busy);
 render();assert(printedStarting("Chat pa")&&printedStarting("Floor -119")&&gCanvas.printed.count("Top -72 at 915.125"));
 assert(gCanvas.printed.count("Enter or Esc: back to the chat"));
 // The burst goes; its peak stays, then fades 1 dB each half second, never below the reading.
 s.radio.signalAt=0;sweepOnce();assert(s.scan.sweep.now[busy]==-119&&s.scan.sweep.peak[busy]==-72);
 gNow+=500;serviceRadioScan(s);assert(s.scan.sweep.peak[busy]==-73);
 for(int i=0;i<200;++i){gNow+=500;serviceRadioScan(s);}assert(s.scan.sweep.peak[busy]==-119);
 // The sweep has no presets; Enter gives the radio back, tuned for the chat again.
 key(2);assert(s.scanning&&!s.scan.listening);
 sounds.clear();enter();assert(!s.scanning&&!s.mesh.lent&&s.mesh.resumes==1&&std::fabs(s.radio.freq-906.875f)<1e-3&&count(s1_mesh::Sound::Saved)==1);
 assert(logs.back()=="[MESH] chat back on");

 // Up/Down: another preset, borrowed, with Meshtastic's sync word and preamble and its own modem and channel.
 key(2);assert(s.scanning&&s.scan.listening&&s.scan.preset==1&&s.radio.receiving&&s.radio.sync==0x2B&&s.radio.preamble==16);
 assert(s.radio.sf==8&&s.radio.bw==250.0f&&s.radio.cr==5&&std::fabs(s.radio.freq-907.875f)<1e-3);
 const uint8_t pub=uint8_t(std::strlen("MediumFast"));assert(s.scan.publicHash==pub);
 render();assert(printedStarting("Chat pa")&&gCanvas.printed.count("Listening...")&&gCanvas.printed.count("0:00")&&printedStarting("MediumFast"));
 // Nothing is read until DIO1 says a packet arrived.
 const int reads=s.radio.reads;for(int i=0;i<5;++i)serviceRadioScan(s);assert(s.radio.reads==reads);
 render();assert(printedStarting("Noise -119"));
 {
  // A column every half second, each the strongest reading in it, read about every 20 ms.
  auto& st=s.scan.strip;listenFor(3000);
  assert(st.head==6&&!st.wrapped&&st.peak[0]==-119&&st.mean[5]==-119&&st.noise()==-119&&s.scan.failures==0);
  assert(st.shown(0)&&st.shown(6)&&!st.shown(7));
  render();assert(gCanvas.printed.count("0:03"));
  // A second of signal on the channel shows as a rise there, and doesn't move the noise.
  s.radio.signalAt=907.875f;listenFor(1000);s.radio.signalAt=0;listenFor(500);
  assert(st.peak[6]==-72&&st.peak[7]==-72&&st.peak[st.head]==-119&&st.noise()==-119);
  // Past the right edge it starts again at the left, leaving a gap ahead of the newest column.
  listenFor(kStripColumnMs*kStripColumns);
  assert(st.wrapped&&st.shown(st.head)&&!st.shown(uint8_t((st.head+1)%kStripColumns))&&
         !st.shown(uint8_t((st.head+kStripGap)%kStripColumns))&&st.shown(uint8_t((st.head+kStripGap+1)%kStripColumns)));
  render();assert(gCanvas.printed.count("1:00"));
  // Unserved longer than the chart holds, it starts the chart afresh but keeps counting time.
  const uint32_t began=st.since;st.advance(gNow+kStripColumnMs*(kStripColumns+2));
  assert(st.head==0&&!st.wrapped&&st.since==began&&st.peak[1]==kNone);
  gNow+=kStripColumnMs*(kStripColumns+2);listenFor(600);assert(st.head==1&&st.noise()==-119);
 }
 auto arrive=[&](Packet p){s.radio.packets.push_back(p);dio1=HIGH;serviceRadioScan(s);assert(dio1==LOW&&s.radio.receiving);};
 ticks.clear();arrive(packet(0xa1b2c3d4,(3<<5)|1,pub));assert(ticks.size()==1&&ticks[0]==-90);  // A tick, at its strength.
 assert(s.scan.strip.mark[s.scan.strip.head]==kPacketMark);  // ...and a mark on the chart.
 auto& l=s.scan.listen;
 assert(l.packets==1&&l.nodeCount==1&&l.recent[0].from==0xa1b2c3d4&&l.recent[0].hops==2&&l.recent[0].publicChannel);
 assert(l.recent[0].rssi==-90&&l.recent[0].snrTenths==50);
 Packet bad=packet(0x11,0,pub);bad.result=RADIOLIB_ERR_CRC_MISMATCH;arrive(bad);
 Packet tiny;tiny.bytes={1,2,3};arrive(tiny);
 assert(s.scan.strip.mark[s.scan.strip.head]==kPacketMark);  // A packet outranks a garbled one in its column.
 listenFor(500);{Packet late=packet(0x11,0,pub);late.result=RADIOLIB_ERR_CRC_MISMATCH;arrive(late);}
 assert(s.scan.strip.mark[s.scan.strip.head]==kGarbledMark);
 assert(l.packets==1&&l.damaged==3&&ticks.size()==1);  // Broken ones don't tick.
 render();assert(gCanvas.printed.count("1 packet, 1 node"));
 // Extremes still fit on the screen: a weak, noisy packet seven hops out on another channel.
 Packet far=packet(0xffffffff,(7<<5),99);far.rssi=-200;far.snr=-120;arrive(far);
 Packet odd2=packet(0x22,0,pub);odd2.rssi=0;arrive(odd2);
 assert(l.recent[0].rssi==kNone&&l.recent[1].hops==7&&!l.recent[1].publicChannel&&l.recent[1].snrTenths==-999);
 assert(l.nodeCount==2);  // !ffffffff isn't a sender
 for(uint32_t n=0;n<40;++n)arrive(packet(1000+n,(2<<5)|1,pub));
 assert(l.nodeCount==kNodes&&l.moreNodes&&l.recentCount==kRecent&&l.recent[0].from==1039&&l.recent[3].from==1036);
 l.packets=4000000000u;gNow+=3600000;render();
 // The line shortens to fit: here, with widths wider than the real font's, down to when.
 assert(gCanvas.printed.count("99999+ packets, 32+ nodes")&&gCanvas.printed.count("Noise -119")&&gCanvas.printed.count("60m ago"));
 // The clock: minutes and seconds, then hours after the first.
 {char c[16];scanClock(0,c,sizeof(c));assert(!std::strcmp(c,"0:00"));scanClock(65999,c,sizeof(c));assert(!std::strcmp(c,"1:05"));
  scanClock(3599000,c,sizeof(c));assert(!std::strcmp(c,"59:59"));scanClock(3725000,c,sizeof(c));assert(!std::strcmp(c,"1:02:05"));
  scanClock(gNow-s.scan.strip.since,c,sizeof(c));assert(gCanvas.printed.count(c)&&std::strlen(c)==7);}
 // The latest three packets, under a short chart.
 assert(gCanvas.printed.count("!0000040f")&&gCanvas.printed.count("!0000040d")&&!gCanvas.printed.count("!0000040c"));
 assert(gCanvas.printed.count("direct")==0&&gCanvas.printed.count("1 hop"));
 // Up/Down change preset, wrapping, each counting afresh on its channel; round to LongFast, the
 // chat's own, gives the radio back.
 key(2);assert(s.scan.preset==2&&s.scan.listen.packets==0&&std::fabs(s.radio.freq-908.875f)<1e-3&&s.radio.receiving&&s.scanning);
 key('.');key('/');assert(s.scan.preset==4);key(';');key(',');assert(s.scan.preset==2);
 key(1);key(1);assert(!s.scanning&&s.mesh.resumes==2);
 key(1);assert(s.scanning&&s.scan.preset==5);key(1);key(1);assert(s.scan.preset==3&&s.radio.bw==125.0f);render();
 // Esc gives it back too; on the chat's own channel Esc is Mesh's, back to the first screen.
 assert(handleRadioScanEscape(s)&&!s.scanning&&s.mesh.resumes==3);
 assert(!handleRadioScanEscape(s)&&!s.scanning);
 // Status for USB: the borrowed radio's, then the live counts.
 logRadioScanStatus(s);
 assert(logs[logs.size()-2].find("[MESH] scan lent=0 listening=0 preset=LongModerate passes=")==0);
 assert(logs.back().find("[MESH] live packets=4 nodes=4 damaged=2 noise=-119")==0);

 // A narrow region's sweep still fits.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();gMeshSession->region=15;
 enter();assert(gMeshSession->scan.plan.points==32);sweepOnce();render();
 assert(gCanvas.printed.count("865.5-873.5 MHz"));

 // A radio that stops answering while borrowed: say so after a run of failures, then leave it alone.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& b=*gMeshSession;
 enter();b.radio.broken=true;sounds.clear();
 for(int i=0;i<10;++i)serviceRadioScan(b);
 assert(b.scan.failures==kScanGiveUp);const size_t tunes=b.radio.tunes.size();
 for(int i=0;i<10;++i)serviceRadioScan(b);enter();key(2);assert(b.radio.tunes.size()==tunes&&!b.scan.listening&&b.scanning);
 // Giving up knocks once, not on every pass after.
 assert(count(s1_mesh::Sound::Failed)==1);
 render();assert(gCanvas.printed.count("Radio not answering")&&gCanvas.printed.count("Esc back"));
 // Esc there gives the radio back to the chat, which retunes it.
 b.radio.broken=false;assert(handleRadioScanEscape(b)&&!b.scanning&&b.mesh.resumes==1);
 // Listening notices a radio that stops answering too, from its readings.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& q=*gMeshSession;
 key(2);listenFor(200);assert(q.scan.listening&&q.scan.failures==0);
 q.radio.broken=true;sounds.clear();listenFor(1000);
 assert(q.scan.failures==kScanGiveUp&&count(s1_mesh::Sound::Failed)==1);
 // A radio the chat can't have back stops, saying so.
 q.radio.broken=false;q.mesh.resumeOk=false;sounds.clear();
 assert(handleRadioScanEscape(q)&&stops==1&&gMeshNotice=="Radio stopped answering"&&count(s1_mesh::Sound::Failed)==1);
 // One good reading clears the count.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& c=*gMeshSession;
 enter();c.radio.broken=true;serviceRadioScan(c);assert(c.scan.failures>0);c.radio.broken=false;serviceRadioScan(c);assert(c.scan.failures==0);
 assert(gCanvas.calls>300);
}
'''
with tempfile.TemporaryDirectory(prefix='s1-radio-scan-') as directory:
    p = Path(directory)
    (p / 'scan.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'bots/CardputerADV/src'), str(p / 'scan.cpp'), '-o', str(p / 'scan')], check=True)
    subprocess.run([str(p / 'scan')], check=True)
print('PASS Radio Scan: live on the chat\'s own channel without borrowing the radio (readings, every packet the library '
      'reads, ticks only while it shows), borrowing refused while Help repeats or a message goes out, the sweep and other '
      'presets on the borrowed radio with "Chat paused", the radio given back by Enter, Up/Down round to LongFast or Esc, '
      'a radio that won\'t come back stopping, US plan on the LongFast grid, narrow regions widened, every point tuned in order without '
      'recalibrating, floor/top/fading peaks, Meshtastic listen setup, packets read only on DIO1, header/hops/channel, '
      'garbled and short packets, bounded nodes and history, preset wrap, the live chart while '
      'listening (columns, peaks, noise, marks, wrapping with a gap, a clock), Esc back to the sweep, a dead radio given up on, 240x135 bounds '
      '(ASan/UBSan), and no way to transmit')
