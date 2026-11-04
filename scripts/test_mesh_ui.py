#!/usr/bin/env python3
"""Run the actual 240x135 renderer across empty/full/UTF-8/scroll states."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'bots/CardputerADV/src/main_parts/068_main.cpp.inc').read_text()
render=source[source.index('String meshClip('):source.rindex('} // namespace')]
# The globals at the top, and the pure helpers the screens use: who a target is, and the People list.
globals_=source[source.index('namespace {')+len('namespace {'):source.index('bool handleMeshEscape(')]
target=source[source.index('const s1_mesh::Contact* meshTargetContact('):source.index('// Puts a text in the outbox')]
people=source[source.index('struct MeshPerson'):source.index('void meshSaveContact(')]
code=r'''
#include "mesh_contacts.h"
#include "mesh_help.h"
#include "mesh_leaf_state.h"
#include "mesh_outbox.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <set>
#include <type_traits>
using std::min;using std::max;
struct String:std::string {
 using std::string::string;using std::string::operator=;
 String(const std::string&s):std::string(s){}
 template<class T,typename=std::enable_if_t<std::is_integral_v<T>>>String(T n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
 void remove(size_t p){erase(p);}void remove(size_t p,size_t n){erase(p,n);}
 String substring(size_t p,size_t n)const{return substr(p,n-p);}
};
void removeLastUtf8Char(String&s){if(s.empty())return;size_t n=s.size()-1;while(n&&(uint8_t(s[n])&0xc0)==0x80)--n;s.erase(n);}
namespace fonts {int Font0=0,Font2=2,efontCN_12=12;}
constexpr int TFT_WHITE=65535;int rgb565_local(int,int,int){return 0;}
struct Canvas {
 int font=0,x=0,y=0,calls=0;bool clipped=false;
 void fillScreen(int){++calls;}
 void setTextSize(int n){assert(n==1);}void setFont(int*p){font=*p;}
 void setTextColor(int,int){}void setCursor(int a,int b){x=a;y=b;}
 int textWidth(const String&s)const {
  int w=0;for(size_t i=0;i<s.size();++i)if((uint8_t(s[i])&0xc0)!=0x80)w+=font==0?6:font==2?8:uint8_t(s[i])<128?6:12;
  return w;
 }
 std::set<std::string> printed;
 template<class T>void print(T value){String s(value);printed.insert(s);int w=textWidth(s),h=font==0?8:font==2?16:12;
  if(w){assert(x>=0&&x+w<=240&&y>=0&&y+h<=135);if(clipped)assert(x>=8&&x+w<=232&&y>=54&&y+h<=126);}
  x+=w;++calls;
 }
 void printf(const char*f,...){char b[128];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);print(b);}
 void drawRect(int a,int b,int w,int h,int){assert(a>=0&&b>=0&&a+w<=240&&b+h<=135);++calls;}
 void drawFastHLine(int a,int b,int w,int){assert(a>=0&&b>=0&&a+w<=240&&b<135);++calls;}
 void setClipRect(int a,int b,int w,int h){assert(a==8&&b==54&&w==224&&h==72);clipped=true;}
 void clearClipRect(){clipped=false;}
}gCanvas;
namespace s1_mesh {
constexpr int kInitPending=-32768;
enum class Use:uint8_t{Chat,Scan};
struct FakeMesh {uint32_t getNodeNum()const{return 42;}};
struct RadioSession {State state;std::atomic<int>initResult{0};uint8_t region=1;Use use=Use::Chat;
 FakeMesh mesh;Contacts contacts;Outbox outbox;Help help;Where where;};
// UA_868 is as long as a region's name gets.
inline const char* regionName(uint8_t c){return c==1?"US":"UA_868";}
inline float regionFrequency(uint8_t c){return c==1?906.875f:868.375f;}
}
struct {uint8_t meshRegion=1;}gDeviceSettings;
int scanRenders=0;
void renderRadioScanUi(s1_mesh::RadioSession&){++scanRenders;}

std::unique_ptr<s1_mesh::RadioSession>gMeshSession;
bool gMeshBusOwned=false,gSdReady=true;String gMeshNotice;
uint32_t clockMs=1234567890;uint32_t millis(){return clockMs;}
struct {int32_t level=-1;}gBatterySnapshot;
bool gpsHeard=false;bool meshGpsHearing(uint32_t){return gpsHeard;}
void meshSavedName(char* l,char* s){strcpy(l,"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");strcpy(s,"WWWW");}
uint32_t gMeshNoticeUntilMs=0;
'''+globals_+target+people+render+r'''
int main() {
 using namespace s1_mesh;
 (void)kMeshStopConfirmMs;(void)kMeshPublicLocationMs;  // The keys' timers, not the screens'.
 for(uint8_t region:{1,15})for(bool sd:{false,true}){gDeviceSettings.meshRegion=region;gSdReady=sd;renderMeshUi();assert(!gCanvas.clipped);}
 // A running radio shows the region it started with, not one picked since.
 gMeshSession=std::make_unique<s1_mesh::RadioSession>();gMeshSession->region=15;gDeviceSettings.meshRegion=1;gMeshBusOwned=true;gSdReady=false;gCanvas.printed.clear();
 for(int init:{0,s1_mesh::kInitPending,-19}) {gMeshSession->initResult=init;renderMeshUi();assert(!gCanvas.clipped);}
 assert(gCanvas.printed.count("UA_868 / LongFast")&&!gCanvas.printed.count("US / LongFast"));
 // A scan session: its start screen while the radio starts, then the scanner's own screen.
 gMeshSession->use=s1_mesh::Use::Scan;gCanvas.printed.clear();
 for(int init:{s1_mesh::kInitPending,-19}){gMeshSession->initResult=init;renderMeshUi();assert(!gCanvas.clipped&&!scanRenders);}
 assert(gCanvas.printed.count("Radio Scan")&&gCanvas.printed.count("Listening only, nothing sent")&&!gCanvas.printed.count("Meshtastic leaf"));
 gMeshSession->initResult=0;renderMeshUi();assert(scanRenders==1);
 gMeshSession->use=s1_mesh::Use::Chat;gMeshSession->initResult=0;
 gMeshSession->initResult=0;auto& s=gMeshSession->state;
 for(uint32_t n=1;n<=16;++n){auto* p=s.hear(n,UINT32_MAX-n,int16_t(n%2?-32768:32767));memset(p->name,'W',40);p->name[40]=0;}
 for(unsigned type=0;type<3;++type) {
  std::string text;
  for(unsigned n=0;n<200;++n)text+=type==0?"W":type==1?u8"Я":u8"😀";
  for(unsigned m=0;m<8;++m)s.message(m+1,type*100+m,m%2,reinterpret_cast<const uint8_t*>(text.data()),text.size());
  s.append(std::string(200,'W').c_str());
  for(size_t scroll=0;scroll<s.messageCount;++scroll)for(bool detail:{false,true}) {
   s.historyOffset=scroll;s.detailView=detail;s.nodeView=false;renderMeshUi();assert(!gCanvas.clipped);
  }
 }
 // Every delivery state, a direct and a channel one, at the longest a name gets.
 uint8_t key[32];memset(key,1,32);
 for(uint32_t c=1;c<=kContacts;++c){std::string name(39,'W');gMeshSession->contacts.add(c,name.c_str(),key);}
 for(uint8_t d=0;d<kDeliveryCount;++d)for(bool direct:{false,true}) {
  auto& m=s.queued(42,direct?2:kBroadcast,direct,d+1,std::string(200,'W').c_str());m.status=Delivery(d);m.reason=uint8_t(d*40);
  s.historyOffset=0;s.detailView=false;renderMeshUi();assert(!gCanvas.clipped);s.detailView=true;renderMeshUi();
 }
 // A direct message received, and every send target, at the longest a name gets.
 s.message(3,9999,false,reinterpret_cast<const uint8_t*>("hi"),2)->to=42;s.detailView=false;
 for(uint8_t t=0;t<=kContacts;++t){s.target=t;for(uint32_t ms:{0u,4000u}){clockMs=ms;renderMeshUi();}}
 assert(gCanvas.printed.count(std::string(12,'W')+">")&&gCanvas.printed.count("Public>"));
 // The People list: contacts first, heard or not, then the radios heard, with and without a key.
 s.nodeView=true;s.detailView=false;
 for(size_t sel=0;sel<kContacts+kNodes+1;++sel){s.nodeSel=sel;renderMeshUi();assert(!gCanvas.clipped);}
 s.nodes[0].hasKey=true;gMeshSession->contacts=Contacts{};s.nodeSel=0;renderMeshUi();
 State empty;empty.nodeView=true;std::swap(s,empty);renderMeshUi();assert(gCanvas.printed.count("No radios heard yet"));std::swap(s,empty);
 s.nodeView=false;
 // Help: choosing, with no contacts and with four, then sending, stopped every way, GPS every way.
 s.helpView=true;gMeshSession->contacts=Contacts{};
 for(int32_t level:{-1,5,100})for(bool heard:{false,true})for(int fix=0;fix<3;++fix) {
  gBatterySnapshot.level=level;gpsHeard=heard;clockMs=10000000;
  gMeshSession->where=Where{};if(fix){gMeshSession->where.known=true;gMeshSession->where.satellites=12;gMeshSession->where.atMs=clockMs-(fix==1?1000:30*60000);}
  for(bool pub:{false,true}){gMeshSession->help=Help{};gMeshSession->help.alsoPublic=pub;renderMeshUi();assert(!gCanvas.clipped);}
 }
 assert(gCanvas.printed.count("Help")&&gCanvas.printed.count("To: public channel (no contacts)")&&gCanvas.printed.count("GPS: not heard"));
 for(uint32_t c=1;c<=kContacts;++c){std::string name(39,'W');gMeshSession->contacts.add(c,name.c_str(),key);}
 renderMeshUi();
 auto& h=gMeshSession->help;
 for(uint8_t choice=0;choice<kHelpChoices;++choice)for(int stop=0;stop<4;++stop)for(int round:{1,12}) {
  h.start(choice,clockMs);h.alsoPublic=true;h.rounds=uint16_t(round);h.roundMs=clockMs;h.stop=HelpStop(stop);
  for(size_t i=0;i<=kContacts;++i){h.seq[i]=uint32_t(i+1);}
  renderMeshUi();assert(!gCanvas.clipped);
  h.seq[2]=0;h.seq[0]=777;renderMeshUi();
 }
 assert(gCanvas.printed.count("Arrived. Stopped repeating")&&gCanvas.printed.count("Again in 5:00"));
 // While Help repeats, the chat's top line says so.
 s.helpView=false;h.start(0,clockMs);h.rounds=1;h.roundMs=clockMs;gMeshNotice="Listening / SD paused";renderMeshUi();
 assert(gCanvas.printed.count("Help on, again in 5:00"));
 // The start screen with the name, and the name editor, empty and full.
 gMeshSession.reset();gMeshBusOwned=false;gMeshNotice="";renderMeshUi();
 gMeshNaming=true;for(const char* typed:{"","Alex Car","WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"}){strcpy(gMeshNameInput,typed);renderMeshUi();}
 assert(gCanvas.printed.count("Short name: AC")&&gCanvas.printed.count("Short name: S1"));
 assert(gCanvas.calls>500);
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-ui-') as directory:
    p=Path(directory);(p/'ui.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(ROOT/'bots/CardputerADV/src'),str(p/'ui.cpp'),'-o',str(p/'ui')],check=True)
    subprocess.run([str(p/'ui')],check=True)
print('PASS actual Mesh renderer: 240x135 bounds, both region names at their longest, the running radio region shown, a scan session handed to the scanner screen, startup/failure/empty, 16 nodes, every history/page, every delivery state, every send target, the People list, Help choosing and sending, the name editor, long ASCII/Cyrillic/emoji and clip-state restoration (ASan/UBSan)')
