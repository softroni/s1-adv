#!/usr/bin/env python3
"""Production radio controller and screens: the station file on a fake card, Find and its
results, saving and removing, reconnecting, the last station, and resource boundaries."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1];src=root/'bots/CardputerADV/src';parts=src/'main_parts'
app=(parts/'070_main.cpp.inc').read_text()
for f,s in [('004_main.cpp.inc','{"Internet Radio", UiMode::Radio}'),('024_main.cpp.inc','if (deferInternetRadioMode(mode)) return;'),('033_main.cpp.inc','if (radioBusy())'),('042_main.cpp.inc','if (radioBusy())'),('017_main.cpp.inc','if (radioBusy())'),('022_main.cpp.inc','if (radioBusy())'),('010_main.cpp.inc','gInboxHttpBusy || radioBusy()'),('063_main.cpp.inc','if (radioBusy()) return false;'),('061_main.cpp.inc','serviceInternetRadio();'),('048_main.cpp.inc','handleInternetRadioKey(key, keys)')]:
 assert s in (parts/f).read_text(),(f,s)
loop=(parts/'061_main.cpp.inc').read_text().split('void loop() {',1)[1]
assert loop.index('if (radioBusy())')<loop.index('serviceVoiceReplySpeech')
assert 'main_parts/070_main.cpp.inc' in (src/'main.cpp').read_text()
# Typing in Find takes every key but Tab, ahead of the shortcuts, like the weather city does.
typing=(parts/'048_main.cpp.inc').read_text()
assert typing.index('if (radioTakesText() && !gLauncherVisible && !keys.tab)')<typing.index("if (key == '`' || key == '~'")
# The arrows move in Find and its results, ahead of the player's W/A/S/D mapping.
arrows=(parts/'049_main.cpp.inc').read_text()
assert arrows.index('radioTakesArrows() && (upDown')<arrows.index('gUiMode == UiMode::Radio || gUiMode == UiMode::VlcRemote')
# Escape steps back inside the radio before it stops the stream and leaves.
escape=(parts/'047_main.cpp.inc').read_text()
assert escape.index('gUiMode == UiMode::Radio && !gLauncherVisible && radioEscape()')<escape.index('if (radioBusy()) { stopInternetRadio(); setUiMode(UiMode::Home); return true; }')
code=r'''
#include "radio_catalog.h"
#include "radio_countries.h"
#include "internet_radio_transport.h"
#include "clock_display.h"
#include <cassert>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <type_traits>
#include <algorithm>
using std::min;using std::max;
struct String:std::string {using std::string::string;String()=default;String(const std::string&s):std::string(s){}
 template<class T,typename=std::enable_if_t<std::is_integral_v<T>>>String(T n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
 void remove(size_t i){if(i<size())erase(i);}void remove(size_t i,size_t n){if(i<size())erase(i,n);}
 String substring(size_t i)const{return i<size()?String(substr(i)):String();}
 String substring(size_t i,size_t j)const{return i<size()?String(substr(i,j>i?j-i:0)):String();}
 void trim(){size_t a=find_first_not_of(' ');if(a==npos){clear();return;}erase(0,a);erase(find_last_not_of(' ')+1);}
};
namespace fonts{const int Font0=0,Font2=2,Font4=4,efontCN_12=12,FreeSansBold9pt7b=9;}
const uint16_t TFT_WHITE=0xffff,TFT_BLACK=0;
namespace lgfx{struct rgb332_t{uint8_t raw;};}
int pushedImages=0;
struct Box{int x,y,w,h;};
struct Canvas{
 const int*font=&fonts::Font0;int x=0,y=0;std::vector<Box>texts;std::vector<std::string>printed;
 void bounds(int a,int b,int w,int h){assert(a>=0&&b>=0&&w>0&&h>0&&a+w<=240&&b+h<=135);}
 void fillScreen(uint16_t){texts.clear();printed.clear();}
 void fillRect(int a,int b,int w,int h,uint16_t){bounds(a,b,w,h);}
 void fillRoundRect(int a,int b,int w,int h,int,uint16_t c){fillRect(a,b,w,h,c);}
 void drawRoundRect(int a,int b,int w,int h,int r,uint16_t c){fillRoundRect(a,b,w,h,r,c);}
 void drawFastHLine(int a,int b,int w,uint16_t c){fillRect(a,b,w,1,c);}
 void drawLine(int a,int b,int c,int e,uint16_t){bounds(min(a,c),min(b,e),abs(c-a)+1,abs(e-b)+1);}
 void drawCircle(int a,int b,int r,uint16_t c){assert(r>0);fillRect(a-r,b-r,2*r+1,2*r+1,c);}
 void fillCircle(int a,int b,int r,uint16_t c){drawCircle(a,b,r,c);}
 template<class T>void pushImage(int a,int b,int w,int h,const T*p){assert(p);bounds(a,b,w,h);++pushedImages;}
 void fillTriangle(int a,int b,int c,int d,int e,int f,uint16_t color){fillRect(min({a,c,e}),min({b,d,f}),max({a,c,e})-min({a,c,e})+1,max({b,d,f})-min({b,d,f})+1,color);}
 void setFont(const int*f){font=f;}void setTextColor(uint16_t,uint16_t=0){}
 void setCursor(int a,int b){x=a;y=b;}
 // Roughly the real widths: wider letters outside ASCII, and a byte is never half a letter.
 int textWidth(const String&s){int w=0;for(size_t i=0;i<s.size();++i){unsigned char c=s[i];if((c&0xC0)==0x80)continue;
  w+=*font==4?14:*font==9?9:*font==2?7:*font==12?(c<0x80?6:12):6;}return w;}
 int height(){return *font==4?26:*font==2?16:*font==9?14:*font==12?13:8;}
 void print(const String&s){if(s.empty())return;
  for(size_t i=0;i<s.size();){unsigned char c=s[i];size_t k=c<0x80?1:(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:4;assert(c<0x80||(c&0xC0)==0xC0);
   assert(i+k<=s.size());for(size_t j=1;j<k;++j)assert((static_cast<unsigned char>(s[i+j])&0xC0)==0x80);i+=k;}  // Whole letters only.
  if(*font!=12)for(unsigned char c:s)assert(c<0x80);  // Other fonts have ASCII only.
  Box box{x,y,textWidth(s),height()};bounds(box.x,box.y,box.w,box.h);
  for(const auto&t:texts)assert(x+box.w<=t.x||t.x+t.w<=x||y+box.h<=t.y||t.y+t.h<=y);texts.push_back(box);printed.push_back(s);}
}gCanvas;
bool shows(const std::string&s){for(auto&t:gCanvas.printed)if(t==s)return true;return false;}
bool showsPart(const std::string&s){for(auto&t:gCanvas.printed)if(t.find(s)!=std::string::npos)return true;return false;}
uint16_t rgb565_local(int r,int g,int b){return ((r&248)<<8)|((g&252)<<3)|(b>>3);}
String fitCurrentFontToWidth(String s,int w){while(!s.empty()&&gCanvas.textWidth(s)>w)s.pop_back();return s;}
String marqueeText(const String&s,int w,uint16_t){return fitCurrentFontToWidth(s,w);}
constexpr int kBigVisibleRows=5;[[maybe_unused]] constexpr int16_t kBigListY=24,kBigRowH=22,kBigLeft=8,kBigRight=229;
void useBigTextFont(const String&s){bool wide=false;for(unsigned char c:s)wide=wide||c>=0x80;gCanvas.setFont(wide?&fonts::efontCN_12:&fonts::FreeSansBold9pt7b);}
int bigListFirstRow(int s,int c){return max<int>(0,min<int>(s-kBigVisibleRows/2,c-kBigVisibleRows));}
void drawBigHeader(const String&t,uint16_t,const String&r=String(),const String& =String(),const String& =String()){gCanvas.fillScreen(0);gCanvas.setFont(&fonts::Font2);gCanvas.setCursor(8,3);gCanvas.print(t);
 if(!r.empty()){gCanvas.setCursor(229-gCanvas.textWidth(r),3);gCanvas.print(r);}}
int selectedRow=-1;
void drawBigRow(int row,const String&l,const String&v,bool selected,uint16_t,uint16_t=0,uint16_t=0){assert(row>=0&&row<kBigVisibleRows);if(selected)selectedRow=row;
 useBigTextFont(l);gCanvas.setCursor(8,26+row*22);String shown=l;while(gCanvas.textWidth(shown)>130){size_t n=shown.size();while(n&&(static_cast<unsigned char>(shown[n-1])&0xC0)==0x80)--n;shown.erase(n-1);}gCanvas.print(shown);
 if(!v.empty()){gCanvas.setFont(&fonts::Font2);gCanvas.setCursor(150,26+row*22);gCanvas.print(fitCurrentFontToWidth(v,70));}}
void drawBigScrollbar(int,int,uint16_t){}
void drawBigNotice(const String&t,uint16_t){gCanvas.setFont(&fonts::Font2);gCanvas.setCursor(8,114);gCanvas.print(fitCurrentFontToWidth(t,220));}
void renderLauncherOverlay(){}void renderDebugOverlay(){}
struct Keyboard_Class{struct KeysState{bool ctrl=false,alt=false,opt=false,enter=false,fn=false;};};
enum class UiMode{Home,Radio,Voice,Mesh};UiMode gUiMode=UiMode::Radio;
struct{bool clockTwelveHour=false;}gDeviceSettings;
uint8_t gSpeakerVolume=128;uint32_t gLastRenderMs=0,tick=1000;
uint32_t millis(){return tick;}
struct{int changes=0;void invalidate(){++changes;}}gUiFramePolicy;
bool gWifiReady=true,gBootBackgroundActive=false,gCanvasSpriteActive=true,gRecording=false,gSubmitting=false,gThinking=false;
bool gPlaybackActive=false,gTransferUiActive=false,gOtaApplyInProgress=false,gInboxHttpBusy=false,gVoiceDialBusy=false;
bool gVoiceReplySpeechBusy=false,gRealtimeActive=false,gRealtimeDialBusy=false,gRealtimeExclusiveMode=false,gPendingRealtimeStart=false;
bool gMeshBusOwned=false,gG0VoicePending=false,gVoiceCapturePreparing=false;
void*gTextTurnTaskHandle=nullptr;void*gCompletedTextTurn=nullptr;std::deque<String>gPendingTextTurns;
String gActiveTextTurn,gQueuedVoiceReplySpeech;
struct{struct{bool occupied=false;bool isPlaying(){return occupied;}}Speaker;}M5Cardputer;
void initSpeaker(){assert(!internet_radio::busy());}
void setStatus(const String&,int){}
void adjustSpeakerVolume(int n){gSpeakerVolume=std::clamp(int(gSpeakerVolume)+n,0,255);}
// A card as FAT behaves: rename won't replace a file, and a card can stop answering.
bool gSdReady=true,cardAnswers=true,writeFails=false;
std::map<std::string,std::string> disk;
#define FILE_WRITE "w"
#define FILE_APPEND "a"
struct File{
 std::string path;size_t pos=0;bool ok=false,dir=false;
 explicit operator bool()const{return ok;}
 bool isDirectory()const{return dir;}
 size_t size()const{return disk[path].size();}
 size_t read(uint8_t*p,size_t n){auto&d=disk[path];n=std::min(n,d.size()-pos);memcpy(p,d.data()+pos,n);pos+=n;return n;}
 size_t write(const uint8_t*p,size_t n){if(writeFails)return 0;disk[path].append(reinterpret_cast<const char*>(p),n);return n;}
 size_t write(uint8_t c){return write(&c,1);}
 void close(){ok=false;}
};
struct{
 File open(const char*path,const char*mode="r"){File f;f.path=path;if(!cardAnswers)return f;
  if(f.path=="/"){f.ok=f.dir=true;return f;}
  std::string m=mode;if(m=="r"){f.ok=disk.count(path)>0;return f;}
  if(m=="w")disk[path]="";else if(!disk.count(path))disk[path]="";f.ok=true;return f;}
 bool exists(const char*p){return cardAnswers&&disk.count(p);}
 bool remove(const char*p){return cardAnswers&&disk.erase(p);}
 bool rename(const char*a,const char*b){if(!cardAnswers||!disk.count(a)||disk.count(b))return false;disk[b]=disk[a];disk.erase(a);return true;}
}SD;
template<class F>bool ensureDirRecursive(F&,const String&){return cardAnswers;}
// The JPEG decoder is the device's; here it fills the logo with one colour, or fails on cue.
#define RADIO_HOST_TEST
bool decodeFails=false;
bool radioDecodeLogo(const uint8_t*,size_t size,uint8_t*out){if(decodeFails||!size)return false;memset(out,0x1C,3600);return true;}
void setUiMode(UiMode);
namespace internet_radio{
 bool running=false,cancelling=false,fail=false,searchJob=false;int starts=0,stops=0,searches=0;String url,query,country;bool byGenre=false;
 State phase=State::Idle;Error err=Error::None;uint32_t got=0;std::string song;std::vector<Found> results;size_t shown=0;
 bool logosOn=false,logoJob=false;int logoStarts=0;String logoStream,logoName,logoGiven;LogoResult logoRes=LogoResult::None;std::string jpegBytes;
 bool start(const char*u){assert(!running);++starts;url=u;searchJob=false;logoJob=false;err=Error::None;got=0;song.clear();if(fail){phase=State::Error;err=Error::Timeout;return false;}running=true;phase=State::Connecting;return true;}
 bool startSearch(bool g,const char*t,const char*c){assert(!running);++searches;byGenre=g;query=t;country=c;searchJob=true;logoJob=false;err=Error::None;shown=0;running=true;phase=State::Connecting;return true;}
 void stop(){++stops;if(running){cancelling=true;phase=State::Stopping;}}
 bool busy(){return running;}bool searching(){return running&&searchJob;}
 State state(){return phase;}Error error(){return err;}uint32_t bytes(){return got;}
 const char*status(){return phase==State::Error?"Radio timed out":phase==State::Stopping?"Stopping":phase==State::Playing?"Playing":"Connecting";}
 size_t title(char*o,size_t room){size_t n=std::min(song.size(),room-1);memcpy(o,song.data(),n);o[n]=0;return n;}
 uint16_t loud=0,kbps=0;uint16_t level(){return phase==State::Playing?loud:0;}uint16_t bitrate(){return kbps;}
 bool wantV=false;Visuals vis{};void wantVisuals(bool on){wantV=on;}bool visuals(Visuals&o){if(phase!=State::Playing)return false;o=vis;return o.frames>0;}
 size_t foundCount(){return searching()?0:shown;}
 const Found* found(size_t i){return i<foundCount()?&results[i]:nullptr;}
 void clearFound(){if(!searching())shown=0;}
 void playing(uint32_t b){phase=State::Playing;got=b;}
 bool startLogo(const char*s,const char*n,const char*l){if(!logosOn||running)return false;++logoStarts;logoStream=s;logoName=n;logoGiven=l;logoJob=true;searchJob=false;running=true;phase=State::Connecting;logoRes=LogoResult::None;return true;}
 bool fetchingLogo(){return running&&logoJob;}
 LogoResult logoResult(){return fetchingLogo()?LogoResult::None:logoRes;}
 const uint8_t* logoJpeg(size_t&n){if(fetchingLogo()||logoRes!=LogoResult::Found){n=0;return nullptr;}n=jpegBytes.size();return reinterpret_cast<const uint8_t*>(jpegBytes.data());}
 void clearLogo(){if(!fetchingLogo())logoRes=LogoResult::None;}
 void endLogo(LogoResult r){assert(logoJob&&running);running=false;logoJob=false;phase=State::Idle;logoRes=cancelling?LogoResult::None:r;cancelling=false;}
 void finish(State s=State::Idle,Error e=Error::None){assert(!logoJob);if(searchJob&&!cancelling&&e==Error::None)shown=results.size();running=false;cancelling=false;phase=s;err=e;}
}
'''+app+r'''
void setUiMode(UiMode m){if(deferInternetRadioMode(m))return;gUiMode=m;}
void key(char c){Keyboard_Class::KeysState k;handleInternetRadioKey(c,k);}
void type(const char*s){for(;*s;++s)key(*s);}
void service(int n=1){for(int i=0;i<n;++i)serviceInternetRadio();}
void reopen(){if(radioBusy()){stopInternetRadio();internet_radio::finish();}gUiMode=UiMode::Home;service();assert(!gRadioList);gUiMode=UiMode::Radio;beginInternetRadio();}
void render(){selectedRow=-1;renderInternetRadioUi();}
internet_radio::Found make(const char*name,const char*url,const char*tags,const char*country,uint16_t bitrate,const char*logo=""){
 internet_radio::Found f{};snprintf(f.logo,sizeof(f.logo),"%s",logo);snprintf(f.name,sizeof(f.name),"%s",name);snprintf(f.url,sizeof(f.url),"%s",url);snprintf(f.tags,sizeof(f.tags),"%s",tags);
 snprintf(f.country,sizeof(f.country),"%s",country);f.bitrate=bitrate;return f;}
void search(const char*text,bool genre,std::vector<internet_radio::Found> results){
 key('f');assert(gRadioView==RadioView::Find);while(!gRadioQuery.isEmpty())key('\b');type(text);
 if(gRadioByGenre!=genre)key(char(3));assert(gRadioByGenre==genre);key('\n');service();
 if(radioBusy()&&!internet_radio::searching()){internet_radio::finish();service();}
 assert(internet_radio::searching()&&internet_radio::query==text&&internet_radio::byGenre==genre);
 internet_radio::results=results;render();assert(shows("Searching..."));internet_radio::finish();service();
}
const std::string kList="/radio/stations.txt";
int main(){
 using namespace radio_catalog;using internet_radio::State;using internet_radio::Error;
 // No card: the built-in stations, and nothing can change them.
 gSdReady=false;beginInternetRadio();
 assert(gRadioList&&gRadioList->count()==18&&!gRadioListOnCard&&gRadioNotice=="No SD card: built-in stations"&&disk.empty());
 key('\b');assert(gRadioView==RadioView::Player&&gRadioNotice=="No SD card: built-in stations");
 // A new card gets them as a file, and the list is read from it.
 gSdReady=true;reopen();
 assert(disk[kList]==kStarter&&!disk.count("/radio/stations.new")&&gRadioListOnCard&&gRadioNotice=="Enter to listen");
 // A file that's there is never replaced, even with no stations in it.
 disk[kList]="# nothing yet\n";reopen();assert(gRadioList->count()==0&&disk[kList]=="# nothing yet\n");
 render();assert(shows("No stations"));key('\n');service();assert(gRadioNotice=="No stations: F finds some"&&!internet_radio::starts);
 // A change cut short between its last two steps leaves the new file, which is used.
 disk.erase(kList);disk["/radio/stations.new"]=kStarter;reopen();assert(gRadioList->count()==18&&disk[kList]==kStarter&&!disk.count("/radio/stations.new"));
 // A card that doesn't answer: built-in stations, nothing written.
 cardAnswers=false;disk.clear();reopen();assert(!gRadioListOnCard&&disk.empty()&&gRadioList->count()==18&&gRadioNotice=="Can't open stations.txt");cardAnswers=true;
 // Lines it can't read are counted, so a typo doesn't just vanish.
 disk[kList]=std::string(kStarter)+"Bad line\nA | B | https://x.example/\n";reopen();assert(gRadioNotice=="2 lines in stations.txt skipped"&&gRadioList->count()==18);
 disk[kList]=std::string(17000,'#');reopen();assert(!gRadioListOnCard&&gRadioNotice=="stations.txt is over 16 KB");
 disk[kList]=kStarter;reopen();

 // Browsing never starts playback. Explicit Play starts exactly once and remembers the station.
 key('s');key('d');assert(internet_radio::starts==0);
 key('\n');assert(gRadioStartPending&&!radioBusy());service();assert(radioBusy()&&internet_radio::starts==1);
 assert(disk["/radio/last.txt"]==std::string(gRadioSelection.station()->url)+"\n");
 service();assert(internet_radio::starts==1);
 // Retune waits for the socket and PCM to be let go, then starts the newest choice once.
 key('s');key('d');key('d');assert(gRadioStartPending&&radioBusy()&&internet_radio::cancelling);
 service();assert(internet_radio::starts==1);
 internet_radio::finish();service();assert(internet_radio::starts==2&&internet_radio::url==gRadioSelection.station()->url);
 // Pause stays paused and language browsing stays silent.
 key(' ');assert(!gRadioWantPlaying&&radioBusy());internet_radio::finish();service();int n=internet_radio::starts;
 key('l');service();assert(internet_radio::starts==n);render();assert(gCanvas.printed[0].rfind("IT  ",0)==0);
 // L steps through the countries the list has, in order, then back to all of them.
 key('l');render();assert(gCanvas.printed[0].rfind("RU  ",0)==0);key('l');render();assert(gCanvas.printed[0].rfind("US  ",0)==0);
 key('l');render();assert(gCanvas.printed[0].find("  ")==std::string::npos&&!gRadioSelection.country[0]);
 for(int i=0;i<40;++i)key('+');assert(gSpeakerVolume==255);for(int i=0;i<40;++i)key('-');assert(gSpeakerVolume==0);
 // Five percent a press, on the nearest five, with or without Shift.
 for(int pct=5;pct<=100;pct+=5){key('=');assert((int(gSpeakerVolume)*100+127)/255==pct);render();assert(shows(std::to_string(pct)));}
 key('+');assert(gSpeakerVolume==255);for(int pct=95;pct>=0;pct-=5){key('_');assert((int(gSpeakerVolume)*100+127)/255==pct);}
 gSpeakerVolume=160;key('+');assert((int(gSpeakerVolume)*100+127)/255==65);gSpeakerVolume=160;key('-');assert((int(gSpeakerVolume)*100+127)/255==60);
 gSpeakerVolume=0;
 // Every audio/network owner blocks a new radio worker, without touching it.
 for(bool*p:{&gBootBackgroundActive,&gRecording,&gSubmitting,&gThinking,&gPlaybackActive,&gTransferUiActive,&gOtaApplyInProgress,&gInboxHttpBusy,&gVoiceDialBusy,&gVoiceReplySpeechBusy,&gRealtimeActive,&gRealtimeDialBusy,&gRealtimeExclusiveMode,&gPendingRealtimeStart,&gMeshBusOwned,&gG0VoicePending,&gVoiceCapturePreparing}){
  *p=true;key('\n');serviceInternetRadio();assert(!radioBusy()&&!gRadioWantPlaying);*p=false;
 }
 gWifiReady=false;key('\n');service();assert(!radioBusy()&&gRadioNotice=="Wi-Fi required");gWifiReady=true;
 // A station that never played stops at its error, with no retrying; Enter retries.
 internet_radio::fail=true;key('\n');service();assert(!gRadioWantPlaying&&!radioBusy());internet_radio::fail=false;
 key('\n');service();assert(radioBusy());internet_radio::finish(State::Error,Error::Connect);service();assert(!gRadioWantPlaying&&!gRadioRetryWaiting);
 n=internet_radio::starts;for(int i=0;i<50;++i){tick+=1000;service();}assert(internet_radio::starts==n);

 // A stream that played and dropped comes back after 3 s, 10 s and 30 s, then gives up.
 key('\n');service();internet_radio::playing(100000);internet_radio::song="Artist - Song";render();assert(shows("Artist - Song"));
 // The player: the station's place in its genre, its bitrate, a song over two lines, and a
 // meter that follows the audio's loudness, settling to a dot when it's quiet.
 internet_radio::kbps=128;render();assert(showsPart("128 kbps")&&showsPart(" of ")&&shows("Enter")&&shows("W/S")&&shows("- +"));
 assert(shows(std::to_string((int(gSpeakerVolume)*100+127)/255)));
 internet_radio::song="Robert Glasper Experiment featuring Erykah Badu - Afro Blue (Live at the Blue Note)";render();
 { bool cut=false;for(auto&t:gCanvas.printed)cut=cut||(t.size()>2&&t.compare(t.size()-2,2,"..")==0&&t.find("Robert")==std::string::npos);
   assert(showsPart("Robert Glasper")&&cut&&!shows(internet_radio::song)); }  // Whole words, two lines, the rest cut.
 internet_radio::loud=8000;for(int i=0;i<6;++i){tick+=100;render();}assert(gRadioMeter[4]==10&&gRadioMeter[0]==10);
 internet_radio::loud=0;tick+=100;render();assert(gRadioMeter[4]==1&&gRadioMeter[3]==10);
 internet_radio::loud=3000;tick+=100;render();assert(gRadioMeter[4]==6);internet_radio::loud=699;tick+=100;render();assert(gRadioMeter[4]==1);
 internet_radio::loud=32767;tick+=100;render();assert(gRadioMeter[4]==14);internet_radio::loud=0;internet_radio::kbps=0;internet_radio::song="Artist - Song";
 internet_radio::finish(State::Error,Error::Timeout);service();assert(gRadioWantPlaying&&gRadioRetryWaiting);
 render();assert(shows("Reconnecting in 3s")&&!gRadioMeter[4]);n=internet_radio::starts;
 tick+=2999;service();assert(internet_radio::starts==n);tick+=1;service();assert(internet_radio::starts==n+1&&radioBusy());
 internet_radio::finish(State::Error,Error::Connect);service();assert(gRadioRetryWaiting);tick+=10000;service();assert(internet_radio::starts==n+2);
 internet_radio::finish(State::Error,Error::Dns);service();assert(gRadioRetryWaiting);tick+=30000;service();assert(internet_radio::starts==n+3);
 internet_radio::finish(State::Error,Error::Connect);service();assert(!gRadioRetryWaiting&&!gRadioWantPlaying&&gRadioNotice=="Radio timed out");
 // The station ending its stream counts as a drop too. With Wi-Fi gone it waits for it,
 // up to a minute, without spending a try.
 key('\n');service();internet_radio::playing(100000);internet_radio::finish();service();assert(gRadioRetryWaiting);
 gWifiReady=false;n=internet_radio::starts;tick+=3000;service();tick+=20000;service();assert(gRadioRetryWaiting&&internet_radio::starts==n);
 render();assert(shows("Waiting for Wi-Fi..."));gWifiReady=true;tick+=1000;service();assert(internet_radio::starts==n+1);
 internet_radio::playing(100000);internet_radio::finish();service();gWifiReady=false;tick+=61000;service();service();
 assert(!gRadioRetryWaiting&&!gRadioWantPlaying&&gRadioNotice=="Wi-Fi required");gWifiReady=true;
 // Leaving while it waits to reconnect ends the wait, so opening the radio again stays quiet.
 key('\n');service();internet_radio::playing(100000);internet_radio::finish(State::Error,Error::Timeout);service();assert(gRadioRetryWaiting);
 n=internet_radio::starts;setUiMode(UiMode::Home);service();assert(!gRadioRetryWaiting&&!gRadioWantPlaying);service();assert(!gRadioList);
 gUiMode=UiMode::Radio;beginInternetRadio();tick+=60000;service(3);assert(internet_radio::starts==n&&!radioBusy());
 // Enter during the wait stops it for good. A lasting error never retries, even after playing.
 key('\n');service();internet_radio::playing(100000);internet_radio::finish(State::Error,Error::Timeout);service();assert(gRadioRetryWaiting);
 key('\n');assert(!gRadioRetryWaiting&&!gRadioWantPlaying);n=internet_radio::starts;tick+=60000;service();assert(internet_radio::starts==n);
 key('\n');service();internet_radio::playing(100000);internet_radio::finish(State::Error,Error::Unsupported);service();assert(!gRadioRetryWaiting&&!gRadioWantPlaying);

 // V opens the visuals, and the worker analyses frames only while they show. V again changes
 // the look; the player's keys still work; Esc goes back, and leaving the radio turns them off.
 key('\n');service();internet_radio::playing(100000);assert(gRadioView==RadioView::Player);service();assert(!internet_radio::wantV);
 // The card with the keys shows the first time only, and the card keeps that it has.
 assert(!disk.count("/radio/visuals.seen"));
 key('v');assert(gRadioView==RadioView::Visual&&gRadioLook==0);service();assert(internet_radio::wantV);render();assert(shows("Bars")&&shows("V next  Esc back"));
 assert(disk.count("/radio/visuals.seen"));tick+=3100;render();assert(!shows("Bars")&&!shows("V next  Esc back"));
 // Changing the look names it beside the clock for a moment, with no card.
 for(int i=1;i<=4;++i){key('V');assert(gRadioLook==i%4&&gRadioView==RadioView::Visual);render();assert(shows(kRadioLooks[i%4])&&!shows("V next  Esc back"));}
 tick+=1600;render();assert(!shows("Bars"));
 radioEscape();key('v');render();assert(!shows("V next  Esc back"));radioEscape();
 { const bool known=gRadioVisualsKnown;gRadioVisualsKnown=false;reopen();assert(gRadioVisualsKnown);gRadioVisualsKnown=known;
   key('\n');service();internet_radio::playing(100000);key('v'); }
 { const int s=gRadioSelection.stationIndex();key('d');assert(gRadioSelection.stationIndex()!=s&&gRadioStartPending&&gRadioView==RadioView::Visual);
   internet_radio::finish();service();assert(radioBusy());internet_radio::playing(100000);
   const int vol=gSpeakerVolume;key('=');assert(gSpeakerVolume>vol||vol==255);key('-'); }
 // Bars rise at once, fall back by time, and their caps fall slower.
 internet_radio::vis=internet_radio::Visuals{};for(auto&b:internet_radio::vis.bands)b=255;internet_radio::vis.frames=1;
 for(uint8_t look=0;look<4;++look){gRadioLook=look;
  for(int i=0;i<16;++i){internet_radio::vis.bands[i]=uint8_t(i*17);internet_radio::vis.wave[i*7]=int8_t(i%2?127:-128);}
  for(int t=0;t<30;++t){tick+=40;render();}}
 for(auto&b:internet_radio::vis.bands)b=255;tick+=40;render();for(int i=0;i<16;++i)assert(gRadioShown[i]==255&&gRadioPeaks[i]==255);
 for(auto&b:internet_radio::vis.bands)b=0;tick+=100;render();for(int i=0;i<16;++i)assert(gRadioShown[i]==225&&gRadioPeaks[i]==245);
 tick+=5000;render();for(int i=0;i<16;++i)assert(gRadioShown[i]==0&&gRadioPeaks[i]==145);  // One draw after a long gap falls only so far.
 for(int k=0;k<2;++k){tick+=1000;render();}for(int i=0;i<16;++i)assert(gRadioPeaks[i]==0);
 // Paused or never analysed, every look still draws, flat.
 key('\n');assert(gRadioView==RadioView::Visual&&!gRadioWantPlaying);internet_radio::finish();service();
 for(uint8_t look=0;look<4;++look){gRadioLook=look;render();}
 internet_radio::song="Очень длинное название песни, которое никак не помещается на экране";
 key('\n');service();internet_radio::playing(100000);internet_radio::vis.frames=0;for(uint8_t look=0;look<4;++look){gRadioLook=look;for(int t=0;t<12;++t){tick+=240;render();}}
 internet_radio::song="Artist - Song";gRadioLook=0;
 assert(radioEscape()&&gRadioView==RadioView::Player);service();assert(!internet_radio::wantV);
 key('v');service();assert(internet_radio::wantV);setUiMode(UiMode::Home);service();assert(!internet_radio::wantV);internet_radio::finish();service();service();
 gUiMode=UiMode::Radio;beginInternetRadio();assert(gRadioView==RadioView::Player);
 // The list opens on the last station played, and doesn't play it.
 gRadioSelection.select(12);key('\n');service();const std::string twelve=gRadioList->at(12).url;assert(disk["/radio/last.txt"]==twelve+"\n");
 n=internet_radio::starts;reopen();assert(gRadioSelection.stationIndex()==12&&!radioBusy()&&internet_radio::starts==n);
 // Navigation waits for the worker, then applies the mode and lets go of the list.
 key('\n');service();setUiMode(UiMode::Voice);assert(gUiMode==UiMode::Radio&&gRadioModePending&&radioBusy());
 service();assert(gUiMode==UiMode::Radio);internet_radio::finish();service();assert(gUiMode==UiMode::Voice&&!gRadioModePending);
 service();assert(!gRadioList&&!gRadioSelection.list);gUiMode=UiMode::Radio;beginInternetRadio();

 // Find: the player's letter keys type, and the arrows move instead of acting on the player.
 gRadioSelection.select(0);key('\n');service();assert(radioBusy());const int volume=gSpeakerVolume;
 key('f');assert(gRadioView==RadioView::Find&&radioTakesText()&&radioTakesArrows());
 render();assert(shows("Searching stops the radio")&&shows("By genre")&&shows("By name"));
 type("wasdl+-: ");assert(gRadioQuery=="wasdl+-: "&&gRadioSelection.stationIndex()==0&&gSpeakerVolume==volume&&radioBusy());
 // The arrow keys are ; . , / on this keyboard, with or without Fn. They move, never type.
 key(';');assert(gRadioOnCountry&&gRadioQuery=="wasdl+-: ");key('.');assert(!gRadioOnCountry);
 key(',');assert(!gRadioByGenre);key('/');assert(gRadioByGenre);
 {Keyboard_Class::KeysState fn;fn.fn=true;handleInternetRadioKey('.',fn);assert(gRadioOnCountry);handleInternetRadioKey(';',fn);assert(!gRadioOnCountry);
  handleInternetRadioKey(',',fn);assert(!gRadioByGenre);handleInternetRadioKey('/',fn);assert(gRadioByGenre&&gRadioQuery=="wasdl+-: ");}
 key(';');type(";.,/");assert(gRadioCountry.isEmpty());key('.');assert(!gRadioOnCountry&&gRadioByGenre);
 while(!gRadioQuery.isEmpty())key('\b');key('\b');
 for(int i=0;i<40;++i)key('x');assert(gRadioQuery.size()==kRadioQueryChars);render();assert(shows(std::string(29,'x')+"_"));
 while(!gRadioQuery.isEmpty())key('\b');
 key(char(3));assert(!gRadioByGenre);key(char(4));assert(gRadioByGenre);
 // Searching stops the stream first: one worker. Nothing found keeps the words to change.
 type("polka");key('\n');assert(gRadioSearchPending&&!gRadioWantPlaying);service();assert(!internet_radio::searches);
 internet_radio::finish();service();assert(internet_radio::searches==1&&internet_radio::query=="polka"&&gRadioSearching);
 internet_radio::results={};internet_radio::finish();service();assert(gRadioView==RadioView::Find&&gRadioNotice=="Nothing found for polka"&&gRadioQuery=="polka");
 // A directory that can't be reached says so.
 key('\n');service();internet_radio::finish(State::Error,Error::Dns);service();assert(gRadioNotice=="Search: can't find the directory");
 key('\n');service();internet_radio::finish(State::Error,Error::Timeout);service();assert(gRadioNotice=="Search: the directory didn't answer");
 // Escape while it searches stops the search, and its answer is never shown.
 key('\n');service();assert(internet_radio::searching());assert(radioEscape()&&gRadioView==RadioView::Player);
 internet_radio::results={make("Late","http://late.example/","","US",128)};internet_radio::finish();service();assert(gRadioView==RadioView::Player);

 // A country narrows any search. Up and down move between the words and its box, which takes
 // letters only, two at most, in capitals. UK means GB. It's checked before anything is sent.
 assert(internet_radio::country.empty()&&gRadioCountry.isEmpty());
 key('f');while(!gRadioQuery.isEmpty())key('\b');type("jazz");key(char(2));assert(gRadioOnCountry&&radioTakesText());
 render();assert(shows("Country")&&showsPart("like US"));
 type("1u-k9z");assert(gRadioCountry=="GB"&&gRadioQuery=="jazz");render();assert(shows("United Kingdom"));
 key('\b');assert(gRadioCountry=="G");key('\n');assert(gRadioNotice=="Type 2 letters, like US"&&gRadioOnCountry);
 key('q');assert(gRadioCountry=="GQ");key('\b');key('\b');type("qq");render();assert(shows("not a country"));
 n=internet_radio::searches;key('\n');assert(gRadioNotice=="No country QQ: try one like US"&&internet_radio::searches==n);
 key('\b');key('\b');type("gb");key(char(1));assert(!gRadioOnCountry);type("!");assert(gRadioQuery=="jazz!"&&gRadioCountry=="GB");key('\b');
 key('\n');service();assert(internet_radio::searching()&&internet_radio::country=="GB"&&disk["/radio/country.txt"]=="GB\n");
 internet_radio::results={};internet_radio::finish();service();assert(gRadioNotice=="Nothing found for jazz in GB");
 // It's there next time, and clearing it means anywhere again.
 radioEscape();reopen();assert(gRadioCountry=="GB");key('f');key(char(2));key('\b');key('\b');render();assert(shows("like US, GB, DE"));
 key(char(1));render();assert(shows("Anywhere"));key('\n');service();assert(internet_radio::country.empty()&&!disk.count("/radio/country.txt"));
 internet_radio::results={};internet_radio::finish();service();assert(gRadioNotice=="Nothing found for jazz");radioEscape();
 disk["/radio/country.txt"]="ZZ\n";reopen();assert(gRadioCountry.isEmpty());disk.erase("/radio/country.txt");
 // Nothing typed finds the most listened, in the country if there is one. Saved from such a
 // search, a station goes where its tags say, as from a search by name.
 key('f');while(!gRadioQuery.isEmpty())key('\b');render();assert(shows("Blank finds the most listened"));
 key('\n');service();assert(internet_radio::searching()&&internet_radio::query.empty()&&internet_radio::byGenre);
 internet_radio::results={};internet_radio::finish();service();assert(gRadioNotice=="Nothing found");
 key(char(2));type("us");key('\n');service();assert(internet_radio::country=="US"&&internet_radio::query.empty());
 internet_radio::results={make("Top Jazz","http://top.example/","smooth jazz","US",128),make("Top Talk","http://talk.example/","news","US",64)};
 internet_radio::finish();service();assert(gRadioView==RadioView::Results);key('s');assert(gRadioNotice=="Saved to Jazz");
 key(char(2));key('s');assert(gRadioNotice=="Saved to Other");radioEscape();
 key('f');if(!gRadioOnCountry)key(char(2));key('\b');key('\b');assert(gRadioCountry.isEmpty());key(char(1));assert(!gRadioOnCountry);
 internet_radio::results={};key('\n');service();internet_radio::finish();service();
 assert(gRadioNotice=="Nothing found"&&disk.count("/radio/country.txt")==0);radioEscape();disk[kList]=kStarter;reopen();
 // Results: a list that scrolls with its selection, saved and playing ones marked.
 std::vector<internet_radio::Found> jazz={
  make("Smooth One","http://one.example/","easy listening,smooth jazz","US",128),
  make("Радио Джаз Москва Плюс Ещё Длиннее Название","http://ru.example/","jazz","RU",0),
  make("Sonic Again",gRadioList->at(0).url,"jazz","US",128)};
 for(int i=0;i<17;++i)jazz.push_back(make(("Jazz Station "+std::to_string(i)).c_str(),("http://j"+std::to_string(i)+".example/").c_str(),"jazz","FR",192));
 search("jazz",true,jazz);assert(gRadioView==RadioView::Results&&internet_radio::foundCount()==20&&!radioTakesText()&&radioTakesArrows());
 for(int i=0;i<20;++i){gRadioFoundSel=i;render();assert(selectedRow>=0);gRadioNotice="Saved to Jazz";render();assert(selectedRow>=0&&selectedRow<4);gRadioNotice="";}
 gRadioFoundSel=0;key(char(1));assert(gRadioFoundSel==19);key(char(2));assert(gRadioFoundSel==0);key('.');key(';');assert(gRadioFoundSel==0);
 key(char(4));assert(gRadioFoundSel==1);key(char(3));assert(gRadioFoundSel==0);
 render();assert(shows("Found")&&shows("US"));gRadioFoundSel=2;render();assert(shows("saved"));gRadioFoundSel=0;
 // Saving puts it under the genre searched, in the list's own spelling, on the card.
 key('s');assert(gRadioNotice=="Saved to Jazz"&&gRadioNoticeGood);
 assert(disk[kList]==std::string(kStarter)+"Jazz | Smooth One | http://one.example/ | US\n"&&gRadioList->find("http://one.example/")==18);
 key('s');assert(gRadioNotice=="Already in Jazz"&&!gRadioNoticeGood);
 gRadioFoundSel=1;key('s');assert(gRadioNotice=="Saved to Jazz");
 assert(disk[kList].find("Jazz | Радио Джаз Москва Плюс Ещё Длиннее Наз | http://ru.example/ | RU\n")!=std::string::npos||disk[kList].find(" | http://ru.example/ | RU\n")!=std::string::npos);
 gRadioFoundSel=2;key('s');assert(gRadioNotice=="Already in Jazz");
 gRadioFoundSel=1;render();assert(showsPart("Радио"));
 // Enter plays a found one; Enter again pauses it.
 gRadioFoundSel=3;key('\n');service();assert(radioBusy()&&internet_radio::url=="http://j0.example/");render();assert(shows("playing"));
 key('\n');assert(!gRadioWantPlaying&&gRadioNotice=="Paused");internet_radio::finish();service();
 // Back to the player: one that isn't on the list stops; one that is keeps playing, chosen.
 key('\n');service();assert(radioBusy());assert(radioEscape()&&gRadioView==RadioView::Player&&!gRadioWantPlaying&&gRadioNotice=="Stopped: not on your list");
 assert(!internet_radio::foundCount());internet_radio::finish();service();
 search("jazz",true,jazz);gRadioFoundSel=0;key('\n');service();assert(radioBusy());
 key('f');assert(gRadioView==RadioView::Find&&gRadioWantPlaying);radioEscape();
 search("jazz",true,jazz);gRadioFoundSel=0;key('\n');service();assert(radioBusy());
 assert(radioEscape()&&gRadioWantPlaying&&radioBusy()&&std::string(gRadioSelection.station()->url)=="http://one.example/");
 key(' ');internet_radio::finish();service();
 // By name: the first genre a tag names, or Other. A new genre goes at the end.
 search("bbc",false,{make("BBC World","http://bbc.example/","news,talk","GB",56),make("Chill BBC","http://bbcchill.example/","chillout,lounge","GB",128)});
 key('s');assert(gRadioNotice=="Saved to Other"&&std::string(gRadioList->genre(gRadioList->genres()-1))=="Other");
 key(char(2));key('s');assert(gRadioNotice=="Saved to Chill");
 radioEscape();
 // By genre, words the list hasn't seen become a genre of their own.
 search("80s",true,{make("Eighties","http://80s.example/","80s,pop","US",128)});key('s');assert(gRadioNotice=="Saved to 80s");radioEscape();
 // Saving can't overflow the list or its genres, and a card that fails says so.
 { std::string full;for(int i=0;i<100;++i)full+="G | S"+std::to_string(i)+" | http://s"+std::to_string(i)+".example/\n";
   disk[kList]=full;reopen();search("x",true,{make("More","http://more.example/","","US",1)});key('s');assert(gRadioNotice=="The list is full: 100 stations");radioEscape();
   std::string genres;for(int i=0;i<24;++i)genres+="G"+std::to_string(i)+" | S | http://g"+std::to_string(i)+".example/\n";
   disk[kList]=genres;reopen();search("new",true,{make("More","http://more.example/","","US",1)});key('s');assert(gRadioNotice=="Too many genres: 24 at most");
   std::string big=kStarter;while(big.size()<16300)big+="# padding to fill the file up to its limit\n";big+="#"+std::string(16370-big.size()-2,'x')+"\n";assert(big.size()==16370);
   disk[kList]=big;reopen();assert(gRadioListOnCard);search("jazz",true,jazz);key('s');assert(gRadioNotice=="stations.txt is full: 16 KB"&&disk[kList]==big);radioEscape();
   key(char(2));writeFails=true;disk[kList]=kStarter;reopen();search("jazz",true,jazz);key('s');assert(gRadioNotice=="Couldn't save to the SD card");writeFails=false;radioEscape();
   gSdReady=false;reopen();search("jazz",true,jazz);key('s');assert(gRadioNotice=="No SD card: can't save");radioEscape();gSdReady=true; }

 // Removing asks first, keeps every other line and note, and stops the station if it's on.
 disk[kList]="# my notes\n"+std::string(kStarter);reopen();assert(gRadioList->count()==18);
 gRadioSelection.select(1);key('\b');assert(gRadioView==RadioView::Remove);render();assert(shows("Remove station?")&&shows("Instrumental Jazz"));
 key('n');assert(gRadioView==RadioView::Player&&gRadioNotice=="Kept Instrumental Jazz"&&disk[kList]=="# my notes\n"+std::string(kStarter));
 key('\b');assert(radioEscape()&&gRadioView==RadioView::Player&&gRadioNotice=="Kept Instrumental Jazz");
 key('\b');key('y');assert(gRadioList->count()==17&&gRadioNotice=="Removed Instrumental Jazz");
 { std::string want="# my notes\n"+std::string(kStarter);const std::string gone="Jazz | Instrumental Jazz | http://jfm1.hostingradio.ru:14536/ijstream.mp3 | RU\n";want.erase(want.find(gone),gone.size());assert(disk[kList]==want); }
 gRadioSelection.select(0);key('\n');service();assert(radioBusy());key('\b');key('y');assert(!gRadioWantPlaying&&gRadioList->count()==16);internet_radio::finish();service();
 { const std::string before=disk[kList];writeFails=true;key('\b');key('y');writeFails=false;
   assert(gRadioNotice=="Couldn't change the SD card"&&disk[kList]==before&&!disk.count("/radio/stations.new")&&gRadioList->count()==16); }
 // Removing every station leaves an empty list, which Find can fill again.
 while(gRadioList->count()){key('\b');key('y');}assert(gRadioView==RadioView::Player);render();assert(shows("No stations"));
 key('\b');assert(gRadioView==RadioView::Player);
 search("jazz",true,jazz);key('s');assert(gRadioList->count()==1&&std::string(gRadioList->genre(0))=="Jazz");radioEscape();
 disk[kList]=kStarter;reopen();

 // Logos. A second's rest on a station, with nothing playing, asks for its logo. A fetch isn't
 // a stream: the screen doesn't say Connecting and Enter plays rather than pauses.
 using internet_radio::LogoResult;
 internet_radio::logosOn=true;gRadioLogoTriedCount=0;
 auto logoPath=[](int i,const char*kind){return std::string(radioLogoPath(radioHash(gRadioList->at(size_t(i)).url),kind));};
 gRadioSelection.select(11);service();n=internet_radio::logoStarts;tick+=500;service();assert(internet_radio::logoStarts==n);
 tick+=600;service();assert(internet_radio::logoStarts==n+1&&internet_radio::fetchingLogo()&&!radioStreaming());
 assert(internet_radio::logoStream==gRadioList->at(11).url&&internet_radio::logoName=="Groove Salad"&&internet_radio::logoGiven.empty());
 render();assert(!showsPart("Connecting")&&!pushedImages);
 // Found: decoded once, kept on the card ready to draw, and shown.
 internet_radio::jpegBytes=std::string(1700,'j');internet_radio::endLogo(LogoResult::Found);service();
 assert(disk.count(logoPath(11,"332"))&&disk[logoPath(11,"332")].size()==3600&&gRadioLogo&&gRadioLogoFile==RadioLogoFile::Have);
 render();assert(pushedImages==1);tick+=5000;service();assert(internet_radio::logoStarts==n+1);
 reopen();gRadioSelection.select(11);service();assert(gRadioLogo&&internet_radio::logoStarts==n+1);  // From the card next time.
 { const int s0=internet_radio::starts;key('\n');service();assert(internet_radio::starts==s0+1);internet_radio::playing(100000);
   for(int k=0;k<6;++k){internet_radio::loud=9000;tick+=100;render();}assert(pushedImages>1);key(' ');internet_radio::finish();service(); }
 // Missing, or a JPEG that won't decode: marked, and never asked about again, even next time.
 gRadioSelection.select(12);service();tick+=1100;service();assert(internet_radio::logoStarts==n+2);internet_radio::endLogo(LogoResult::Missing);service();
 assert(disk.count(logoPath(12,"no"))&&!gRadioLogo&&gRadioLogoFile==RadioLogoFile::None);tick+=5000;service();assert(internet_radio::logoStarts==n+2);
 reopen();gRadioSelection.select(12);service();tick+=5000;service();assert(internet_radio::logoStarts==n+2);
 decodeFails=true;gRadioSelection.select(13);service();tick+=1100;service();internet_radio::endLogo(LogoResult::Found);service();decodeFails=false;
 assert(disk.count(logoPath(13,"no"))&&!disk.count(logoPath(13,"332"))&&!gRadioLogo);
 // The network failing: not again this session, but the next.
 gRadioSelection.select(14);service();tick+=1100;service();assert(internet_radio::logoStarts==n+4);internet_radio::endLogo(LogoResult::Failed);service();
 tick+=5000;service();assert(internet_radio::logoStarts==n+4&&!disk.count(logoPath(14,"no")));
 gRadioLogoTriedCount=0;tick+=100;service();assert(internet_radio::logoStarts==n+5);internet_radio::endLogo(LogoResult::Missing);service();
 // Enter during a fetch stops it, undecided, and plays as soon as the worker is free.
 gRadioSelection.select(15);service();tick+=1100;service();assert(internet_radio::fetchingLogo());
 { const int s0=internet_radio::starts;key('\n');assert(internet_radio::cancelling&&gRadioStartPending&&gRadioWantPlaying);service();assert(internet_radio::starts==s0);
   internet_radio::endLogo(LogoResult::Found);service();assert(internet_radio::starts==s0+1&&radioStreaming()&&!disk.count(logoPath(15,"332"))&&!disk.count(logoPath(15,"no")));
   const int l0=internet_radio::logoStarts;tick+=5000;service();assert(internet_radio::logoStarts==l0);  // Never while it plays.
   key(' ');internet_radio::finish();service();tick+=1100;service();assert(internet_radio::fetchingLogo()&&internet_radio::logoStream==gRadioList->at(15).url); }
 // A search stops one too. A station saved from it brings its logo's address, asked for first.
 key('f');while(!gRadioQuery.isEmpty())key('\b');key('\n');assert(internet_radio::cancelling);internet_radio::endLogo(LogoResult::Found);service();
 assert(internet_radio::searching()&&!disk.count(logoPath(15,"332")));
 internet_radio::results={make("With Logo","http://withlogo.example/","jazz","US",128,"https://withlogo.example/l.png")};internet_radio::finish();service();
 key('s');assert(gRadioNotice=="Saved to Jazz");radioEscape();service();
 assert(internet_radio::fetchingLogo()&&internet_radio::logoGiven=="https://withlogo.example/l.png"&&internet_radio::logoStream=="http://withlogo.example/");
 internet_radio::endLogo(LogoResult::Found);service();assert(disk.count(std::string(radioLogoPath(radioHash("http://withlogo.example/"),"332"))));
 // No card, no logos: there's nowhere to keep them. Leaving frees the one shown.
 gSdReady=false;reopen();gRadioSelection.select(4);{const int l0=internet_radio::logoStarts;tick+=5000;service();assert(internet_radio::logoStarts==l0&&!gRadioLogo);}
 gSdReady=true;disk[kList]=kStarter;reopen();gRadioSelection.select(11);service();assert(gRadioLogo);setUiMode(UiMode::Home);service();assert(!gRadioLogo);
 gUiMode=UiMode::Radio;beginInternetRadio();internet_radio::logosOn=false;
 // Every reachable view fits on screen, with long names, Cyrillic, and every state.
 internet_radio::song="Очень длинное название песни, которое не помещается на экране - Исполнитель";
 for(const char*filter:{"","IT","RU","US"}){memcpy(gRadioSelection.country,filter,strlen(filter)+1);
  for(unsigned group=0;group<gRadioList->genres();++group){gRadioSelection.genre=group;
   for(unsigned index=0;index<gRadioSelection.count(group);++index){gRadioSelection.index=index;
    for(auto state:{State::Idle,State::Playing,State::Error,State::Stopping}){
     internet_radio::phase=state;internet_radio::running=state!=State::Idle;gRadioNotice="Radio memory unavailable";
     for(int t=0;t<40;++t){tick+=240;render();}
    }}}}
 internet_radio::running=false;internet_radio::phase=State::Idle;
 for(int when=0;when<3;++when){gRadioRetryWaiting=when==1;gRadioStartPending=when==2;gWifiReady=when!=1;render();}
 gRadioRetryWaiting=gRadioStartPending=false;gWifiReady=true;
 gRadioView=RadioView::Find;for(bool genre:{true,false}){gRadioByGenre=genre;for(const std::string& q:{std::string(),std::string("jazz"),std::string(32,'W')}){gRadioQuery=q;
  for(bool box:{false,true}){gRadioOnCountry=box;for(const char*c:{"","G","GB","QQ","GS","US"}){gRadioCountry=c;render();}}}}
 gRadioOnCountry=false;gRadioCountry="";
 gRadioView=RadioView::Remove;gRadioRemoveName="Радио с очень длинным названием станции";gRadioRemoveGenre="Русская музыка и песни";gRadioRemoveUrl="http://example.org/"+std::string(200,'a');render();
 gRadioView=RadioView::Player;
 puts("PASS radio: stations file on the card (made, kept, recovered, limits), Find by genre or name, results that scroll, saving under the right genre, removing a line, reconnecting after a drop, the last station, resource guards and screens that fit");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-radio-ui-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DINTERNET_RADIO_HOST_TEST','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(src),str(p/'test.cpp'),str(src/'internet_radio_transport.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=120)
