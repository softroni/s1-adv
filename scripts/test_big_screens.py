#!/usr/bin/env python3
"""Draw the real list and information screens at every selection, checking fit and what shows."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
joined = ''.join((parts / f'{n:03d}_main.cpp.inc').read_text() for n in range(1, 90) if (parts / f'{n:03d}_main.cpp.inc').exists())


def function(signature):
    start = joined.index(signature)
    while joined.find(';', start) < joined.find('{', start):  # Skip forward declarations.
        start = joined.index(signature, start + 1)
    end, depth = joined.index('{', start) + 1, 1
    while depth:
        depth += (joined[end] == '{') - (joined[end] == '}')
        end += 1
    return joined[start:end] + '\n'


big = 'constexpr int16_t kBigListY = 24, kBigRowH = 22, kBigLeft = 8, kBigRight = 229;\nconstexpr int kBigVisibleRows = 5;\n' + ''.join(function(signature) for signature in [
    'int16_t useBigTextFont(', 'int bigListFirstRow(', 'void drawBigHeader(', 'void drawBigRow(', 'void drawBigScrollbar(', 'void drawBigNotice('])
big = big.replace('const String& right, const String& hint, const String& hint2', 'const String& right = "", const String& hint = "", const String& hint2 = ""')
big = big.replace('uint16_t labelInk, uint16_t valueInk', 'uint16_t labelInk = TFT_WHITE, uint16_t valueInk = rgb565_local(47, 227, 255)')
code = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>
using std::min;using std::max;
struct String:std::string{using std::string::string;String()=default;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){}String(unsigned long n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}size_t length()const{return size();}void trim(){}
 String operator+(const char*o)const{return String(std::string(*this)+o);}String operator+(const String&o)const{return String(std::string(*this)+std::string(o));}};
String operator+(const char*a,const String&b){return String(std::string(a)+std::string(b));}
namespace fonts{const int Font0=0,Font2=2,Font4=4,Font6=6,Font7=7,efontCN_12=12,FreeSansBold9pt7b=9;}
const uint16_t TFT_BLACK=0,TFT_WHITE=0xffff,TFT_LIGHTGREY=0xD69A;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
template<class T>T clampValue(T n,T lo,T hi){return max(lo,min(n,hi));}
static int widthOf(int f,unsigned char){return f==9?9:f==7?18:f==6?26:f==4?14:6;}
struct Text{std::string s;int x,y,font;uint16_t color;};
struct Canvas{int x=0,y=0,font=2;uint16_t fg=0;std::vector<Text> printed;
 static void inside(int a,int b,int w,int h){assert(a>=0&&b>=0&&a+w<=240&&b+h<=135);}
 void fillScreen(uint16_t){printed.clear();}
 // A solid fill paints over whatever was there, as the message bar does to the last row.
 void fillRoundRect(int a,int b,int w,int h,int,uint16_t){inside(a,b,w,h);
  printed.erase(std::remove_if(printed.begin(),printed.end(),[&](const Text&t){return t.x>=a&&t.x<a+w&&t.y>=b&&t.y<b+h;}),printed.end());}
 void drawRoundRect(int a,int b,int w,int h,int r,uint16_t c){fillRoundRect(a,b,w,h,r,c);}
 void fillRect(int a,int b,int w,int h,uint16_t){inside(a,b,w,h);}
 void drawFastHLine(int a,int b,int w,uint16_t){inside(a,b,w,1);}
 void fillTriangle(int a,int b,int e,int f,int g,int h,uint16_t){inside(min({a,e,g}),min({b,f,h}),1,1);inside(max({a,e,g}),max({b,f,h}),1,1);}
 void setTextSize(int){}void setTextWrap(bool){}
 void setFont(const int*f){font=*f;}void setTextColor(uint16_t c,uint16_t=0){fg=c;}void setCursor(int a,int b){x=a;y=b;}
 int textWidth(const std::string&s){int w=0;for(unsigned char c:s)w+=widthOf(font,c);return w;}
 void print(const std::string&s){if(s.empty())return;inside(x,y,textWidth(s),font==9?19:font==0?8:font==6?48:font==4?26:16);
  for(const auto&t:printed)assert(y>=t.y+6||t.y>=y+6||x>=t.x+int(t.s.size())*5||t.x>=x+int(s.size())*5);printed.push_back({s,x,y,font,fg});x+=textWidth(s);}
}gCanvas;
String fitCurrentFontToWidth(const String&s,int w){std::string t=s;if(gCanvas.textWidth(t)<=w)return String(t);while(t.size()>1&&gCanvas.textWidth(t+"..")>w)t.pop_back();return String(t+"..");}
String tailUtf8ToFit(const String&s,int){return s;}
String marqueeText(const String&s,int w,int){return fitCurrentFontToWidth(s,w);}
void renderLauncherOverlay(){}void renderDebugOverlay(){}
''' + big + r'''
struct Rom{String title;};std::vector<Rom> gNesGames={{"Super Mario Bros"},{"Contra"},{"The Legend of Zelda"},{"Mega Man 2"},{"Tetris"},{"Dr. Mario"},{"Kirby's Adventure"},{"Punch-Out!!"}};
int gNesSelection=3;bool gNesWide=false;String gNesMessage;
struct Entry{String name;bool directory;unsigned long size;};std::vector<Entry> gFileEntries={{"audio",true,0},{"nes",true,0},{"pomodoro",true,0},{"meeting-notes-2026-10-30.wav",false,184320},{"weather.json",false,812},{"firmware-backup.bin",false,2406496},{"readme.txt",false,2048}};
int gSelectedFile=3;String gFilesDir="/voice/2026/10";String gFilesStatus="7 items";bool gSdReady=true;
struct{bool pendingV=false;std::string path="/voice/2026/10/meeting-notes-2026-10-30.wav";unsigned long size=184320;bool pending(){return pendingV;}}gFileDelete;
String filenameFromPath(const String&p){return p.substr(p.rfind('/')+1);}
struct NoteFile{String name;int lineCount;unsigned size;};std::vector<NoteFile> gNotes={{"shopping list",8,212},{"bridge ideas",34,1840},{"car service",3,96},{"\xD0\xB7\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB8",12,512},{"books to read",21,780},{"trip packing",16,420}};
int gSelectedNote=1,gNoteListScrollOffset=0,gNoteScrollOffset=0;String gNotesStatus="6 notes";enum class NotesMode{List,Read,Edit};NotesMode gNotesMode=NotesMode::List;bool gNotesScanned=true;
void*activeNotesFsValue=(void*)1;void* activeNotesFs(){return activeNotesFsValue;}void scanNotes(){}void clampNoteSelection(){}
String compactBytes(unsigned n){return n>=1024?String(int(n/1024))+"k":String(int(n))+"b";}String compactFlashKb(unsigned n){return String(int(n/1024))+"k";}
void renderSdRequiredBanner(const char*){}void drawNotesHeader(const char*,uint16_t,uint16_t,uint16_t){}
String gActiveNoteName,gActiveNotePath,gNoteInputBuffer;
namespace fs{struct FS{};}uint32_t countNoteLines(void*,const String&){return 0;}String readNoteLineAt(void*,const String&,uint32_t){return "";}
const char* kAppVersion="0.2.233-dev";bool candidate=true,gOtaPendingVerify=false,gOtaConfirmedThisBoot=false,gPendingOtaCheckAction=false,gInboxHttpBusy=false;
struct{bool parsed=true;String version="0.2.234-dev",notes="",error;int minBattery=40;unsigned size=2490000;}gOtaManifest;
bool homeFirmwareUpdateAvailable(){return candidate;}String gOtaRunningSlot="app1",gOtaBootState="valid";
struct FirmwareCapacityInfo{unsigned usableBytes=3145728,freeBytes=655000,reserveBytes=65536;};FirmwareCapacityInfo firmwareCapacityInfo(){return {};}
struct{int level=82;}gBatterySnapshot;void updateBatterySnapshot(){}bool batteryExternalPowerPresent(){return false;}
struct{uint8_t minBatteryForUpdate=40;bool clockTwelveHour=true;}gDeviceSettings;
namespace release_delivery{bool otaPowerAllowed(int,bool,int){return true;}}
namespace clock_display{void format(char*p,size_t n,int,int,bool){snprintf(p,n,"6:45");}}
namespace radio_catalog{const char* kGenres[]={"Jazz & Blues"};const char* languageName(int){return "EN";}struct St{const char* name="WBGO Jazz 88.3 Newark";int language=0;};}
namespace internet_radio{enum class State{Idle,Playing};State state(){return State::Playing;}String status(){return "Playing 128 kbps";}}
struct{int genre=0,index=2,language=0;radio_catalog::St st;const radio_catalog::St* station(){return &st;}int count(int){return 14;}}gRadioSelection;
bool gRadioStartPending=false;String gRadioNotice;bool radioBusy(){return true;}uint8_t gSpeakerVolume=170;
struct{bool enabled=true;int hour=6,minute=30;}gAlarmSettings;bool gAlarmRinging=false;String gAlarmEditBuffer,gAlarmStatus="Rings in 11h 45m";
String alarmClockText(){return "6:45 PM";}String alarmSnoozeUntilText(){return "";}String alarmTimeString(int,int){return "06:30";}
uint32_t millis(){return 100000;}
namespace vlc_remote{struct S{bool connected=true;char title[40]="Blade Runner 2049 (2017).mkv";const char* playback="playing";int seconds=3725,length=9840,volume=256;const char* route="Mac speakers";};S snapshot(){return {};}}
String vlcTime(int s){char b[16];snprintf(b,16,"%d:%02d:%02d",s/3600,s/60%60,s%60);return b;}String gVlcNotice="Connected to VLC";
'''
for signature in ['void renderNesMenu()', 'void renderFileManagerUi()', 'void renderOtaUi()', 'void renderInternetRadioUi()', 'void renderAlarmUi()', 'void renderVlcRemoteUi()']:
    code += function(signature)
notes = function('void renderNotesUi()')
code += notes[:notes.index('  const bool edit = gNotesMode == NotesMode::Edit;')] + '}\n' + r"""
bool shows(const char*text){for(const auto&t:gCanvas.printed)if(t.s==text)return true;return false;}
bool anyRow(const std::string&text){for(const auto&t:gCanvas.printed)if(t.y>=24&&t.s==text)return true;return false;}
int rows(){int n=0;for(const auto&t:gCanvas.printed)if(t.font==9&&t.y>=24&&t.x==8)++n;return n;}
int main(){
 // Lists: five rows at most, and the selected one is always among them.
 for(int i=0;i<int(gNesGames.size());++i){gNesSelection=i;renderNesMenu();assert(rows()==5);assert(anyRow(gNesGames[i].title));}
 gNesMessage="Wait for the current request";renderNesMenu();assert(shows("Wait for the current request"));gNesMessage="";
 {auto kept=gNesGames;gNesGames.clear();gNesSelection=0;renderNesMenu();assert(shows("No ROM files on the SD card."));gNesGames=kept;}
 for(int i=0;i<int(gFileEntries.size());++i){gSelectedFile=i;renderFileManagerUi();assert(rows()==5);
  const auto&e=gFileEntries[i];bool found=false;for(const auto&t:gCanvas.printed)found=found||(t.y>=24&&t.color==TFT_BLACK&&t.x==8&&(t.s.rfind((e.directory?"/":"")+e.name.substr(0,6),0)==0));assert(found);}
 gFileDelete.pendingV=true;renderFileManagerUi();assert(shows("Delete file?")&&shows("Y delete   any other key cancels"));gFileDelete.pendingV=false;
 for(int i=0;i<int(gNotes.size());++i){gSelectedNote=i;renderNotesUi();assert(anyRow(gNotes[i].name));}
 // Firmware offers one thing to press, and only with an update to install.
 candidate=true;renderOtaUi();assert(shows("Enter  Install update")&&shows("New"));
 // It says what's new, in words that fit, in green so they stand out from the facts.
 gOtaManifest.notes="Bigger screens, menu letters";renderOtaUi();assert(shows("Bigger screens, menu letters"));
 for(const auto&t:gCanvas.printed)if(t.s=="Bigger screens, menu letters")assert(t.color==rgb565_local(88,240,141));
 gOtaManifest.notes="";
 gOtaPendingVerify=true;renderOtaUi();assert(!shows("Enter  Install update")&&shows("Verifying installed update"));gOtaPendingVerify=false;
 candidate=false;renderOtaUi();assert(!shows("Enter  Install update")&&shows("Up to date; checks every 5s"));
 gOtaManifest.error="network unavailable";renderOtaUi();assert(shows("network unavailable")&&shows("Check failed; retrying"));gOtaManifest.error="";
 // The rest show their main facts in the bigger fonts.
 renderInternetRadioUi();assert(shows("Internet Radio")&&shows("Playing 128 kbps"));
 for(const auto&t:gCanvas.printed)if(t.s=="Jazz & Blues")assert(t.font==2);
 renderAlarmUi();assert(shows("Alarm")&&shows("06:30")&&shows("6:45 PM")&&shows("Daily alarm is on"));
 gAlarmEditBuffer="07";renderAlarmUi();assert(shows("07"));gAlarmEditBuffer="";
 gAlarmRinging=true;renderAlarmUi();assert(shows("Alarm ringing")&&shows("RING"));gAlarmRinging=false;
 renderVlcRemoteUi();assert(shows("VLC Remote")&&shows("Connected to VLC"));
 std::cout<<"PASS big screens: lists keep the selection in view, every selection fits, and each screen shows its facts\n";
}
"""
with tempfile.TemporaryDirectory(prefix='s1-big-screens-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-unused-function',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
