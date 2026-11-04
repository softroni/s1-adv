#!/usr/bin/env python3
"""Run Radio Scan's actual sums, service loop, keys and 240x135 screen against a pretend radio.

The pretend radio has no way to transmit, so this only compiles while the scanner never sends.
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
namespace s1_mesh {
enum class Use:uint8_t{Chat,Scan};
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
 FakeRadio radio;uint8_t region=1;Use use=Use::Scan;s1_scan::Scanner scan;uint32_t lowHeap=1,lowLargest=1;
};
}
std::unique_ptr<s1_mesh::RadioSession> gMeshSession;
// Radio Scan's sounds: a tick for each packet, at its strength, and the knock when the radio gives up.
std::vector<int16_t> ticks;std::vector<s1_mesh::Sound> sounds;
void meshPacketTick(int16_t dbm){ticks.push_back(dbm);}
void meshSound(s1_mesh::Sound s){sounds.push_back(s);}
namespace {  // As in 005: declared in the same unnamed namespace the part defines them in.
void serviceRadioScan(s1_mesh::RadioSession& session);
bool handleRadioScanKey(char key, const Keyboard_Class::KeysState& keys);
void renderRadioScanUi(s1_mesh::RadioSession& session);
void logRadioScanStatus(s1_mesh::RadioSession& session);
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
static void sweepOnce(){auto&s=*gMeshSession;const uint32_t passes=s.scan.sweep.passes;
 for(int i=0;i<200&&s.scan.sweep.passes==passes;++i)serviceRadioScan(s);assert(s.scan.sweep.passes==passes+1);}

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

 // Sweep: every point read in order, tuned without recalibrating, with a 250 kHz receiver.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& s=*gMeshSession;
 serviceRadioScan(s);assert(s.scan.started&&!s.scan.listening&&s.radio.bw==250.0f&&logs.back().find("[MESH] scan ready US 902.000-928.000 MHz points=104")==0);
 sweepOnce();
 assert(s.radio.tunes.size()>=104);
 for(uint8_t i=0;i<104;++i)assert(std::fabs(s.radio.tunes[i]-us.frequency(i))<1e-3);
 const uint8_t busy=us.pointAt(915.125f);
 assert(s.scan.sweep.now[busy]==-72&&s.scan.sweep.floor(s.scan.plan)==-119&&s.scan.sweep.strongest(s.scan.plan)==busy);
 render();assert(printedStarting("Floor -119")&&gCanvas.printed.count("Top -72 at 915.125"));
 // The burst goes; its peak stays, then fades 1 dB each half second, never below the reading.
 s.radio.signalAt=0;sweepOnce();assert(s.scan.sweep.now[busy]==-119&&s.scan.sweep.peak[busy]==-72);
 gNow+=500;serviceRadioScan(s);assert(s.scan.sweep.peak[busy]==-73);
 for(int i=0;i<200;++i){gNow+=500;serviceRadioScan(s);}assert(s.scan.sweep.peak[busy]==-119);

 // Listen: Meshtastic's sync word and preamble, the preset's own modem and channel, receiving.
 enter();assert(s.scan.listening&&s.radio.receiving&&s.radio.sync==0x2B&&s.radio.preamble==16);
 assert(s.radio.sf==7&&s.radio.bw==250.0f&&s.radio.cr==5&&std::fabs(s.radio.freq-906.875f)<1e-3);
 const uint8_t pub=uint8_t(std::strlen("LongFast"));assert(s.scan.publicHash==pub);
 render();assert(gCanvas.printed.count("Listening...")&&gCanvas.printed.count("Nothing yet on this preset"));
 // Nothing is read until DIO1 says a packet arrived.
 const int reads=s.radio.reads;for(int i=0;i<5;++i)serviceRadioScan(s);assert(s.radio.reads==reads);
 auto arrive=[&](Packet p){s.radio.packets.push_back(p);dio1=HIGH;serviceRadioScan(s);assert(dio1==LOW&&s.radio.receiving);};
 ticks.clear();arrive(packet(0xa1b2c3d4,(3<<5)|1,pub));assert(ticks.size()==1&&ticks[0]==-90);  // A tick, at its strength.
 auto& l=s.scan.listen;
 assert(l.packets==1&&l.nodeCount==1&&l.recent[0].from==0xa1b2c3d4&&l.recent[0].hops==2&&l.recent[0].publicChannel);
 assert(l.recent[0].rssi==-90&&l.recent[0].snrTenths==50);
 Packet bad=packet(0x11,0,pub);bad.result=RADIOLIB_ERR_CRC_MISMATCH;arrive(bad);
 Packet tiny;tiny.bytes={1,2,3};arrive(tiny);
 assert(l.packets==1&&l.damaged==2&&ticks.size()==1);  // Broken ones don't tick.
 // Extremes still fit on the screen: a weak, noisy packet seven hops out on another channel.
 Packet far=packet(0xffffffff,(7<<5),99);far.rssi=-200;far.snr=-120;arrive(far);
 Packet odd2=packet(0x22,0,pub);odd2.rssi=0;arrive(odd2);
 assert(l.recent[0].rssi==kNone&&l.recent[1].hops==7&&!l.recent[1].publicChannel&&l.recent[1].snrTenths==-999);
 assert(l.nodeCount==2);  // !ffffffff isn't a sender
 for(uint32_t n=0;n<40;++n)arrive(packet(1000+n,(2<<5)|1,pub));
 assert(l.nodeCount==kNodes&&l.moreNodes&&l.recentCount==kRecent&&l.recent[0].from==1039&&l.recent[3].from==1036);
 l.packets=4000000000u;gNow+=3600000;render();
 assert(gCanvas.printed.count("99999+ packets, 32+ nodes")&&gCanvas.printed.count("2 garbled  last 60m ago"));
 assert(gCanvas.printed.count("!0000040f")&&gCanvas.printed.count("direct")==0&&gCanvas.printed.count("1 hop"));
 // Up/Down change preset, wrapping both ways, and start counting afresh on the new channel.
 key(2);assert(s.scan.preset==1&&s.scan.listen.packets==0&&std::fabs(s.radio.freq-907.875f)<1e-3&&s.radio.receiving);
 key(';');key(',');assert(s.scan.preset==5);key('.');key('/');assert(s.scan.preset==1);
 key(1);key(1);key(1);key(1);assert(s.scan.preset==3&&s.radio.bw==125.0f);render();
 // Enter goes back to sweeping, with a fresh chart and the sweep's receiver width.
 enter();assert(!s.scan.listening&&s.radio.bw==250.0f&&s.scan.sweep.passes==0);sweepOnce();
 // Status for USB.
 logRadioScanStatus(s);assert(logs.back().find("[MESH] scan listening=0 preset=LongModerate passes=1")==0);

 // A narrow region's sweep still fits, and the region's name is shown.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();gMeshSession->region=15;
 serviceRadioScan(*gMeshSession);assert(gMeshSession->scan.plan.points==32);sweepOnce();render();
 assert(printedStarting("UA_868")&&gCanvas.printed.count("865.5-873.5 MHz"));

 // A radio that stops answering: say so after a run of failures, then leave it alone.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& b=*gMeshSession;b.radio.broken=true;
 for(int i=0;i<10;++i)serviceRadioScan(b);
 assert(b.scan.failures==kScanGiveUp);const size_t tunes=b.radio.tunes.size();
 for(int i=0;i<10;++i)serviceRadioScan(b);enter();key(2);assert(b.radio.tunes.size()==tunes&&!b.scan.listening);
 // Giving up knocks once, not on every pass after.
 size_t knocks=0;for(auto q:sounds)knocks+=q==s1_mesh::Sound::Failed;assert(knocks==1);
 render();assert(gCanvas.printed.count("Radio not answering"));
 // One good reading clears the count.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();auto& c=*gMeshSession;
 c.radio.broken=true;serviceRadioScan(c);assert(c.scan.failures>0);c.radio.broken=false;serviceRadioScan(c);assert(c.scan.failures==0);
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
print('PASS Radio Scan: US plan on the LongFast grid, narrow regions widened, every point tuned in order without '
      'recalibrating, floor/top/fading peaks, Meshtastic listen setup, packets read only on DIO1, header/hops/channel, '
      'garbled and short packets, bounded nodes and history, preset wrap, a dead radio given up on, 240x135 bounds '
      '(ASan/UBSan), and no way to transmit')
