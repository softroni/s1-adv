#!/usr/bin/env python3
"""Exercise production Air Music rules, sensor timing, sound and renderer on host."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "air_music.h"'),
        ('004_main.cpp.inc', '{"Air Music", UiMode::AirMusic}'),
        ('004_main.cpp.inc', '{"BrainFlow", UiMode::BrainFlow}'),
        ('022_main.cpp.inc', 'mode == "air_music"'),
        ('024_main.cpp.inc', 'mode == UiMode::AirMusic) beginAirMusic();'),
        ('024_main.cpp.inc', 'gUiMode == UiMode::AirMusic) stopAirMusic();'),
        ('043_main.cpp.inc', 'cmd == "/music"'),
        ('048_main.cpp.inc', 'gUiMode == UiMode::AirMusic'),
        ('061_main.cpp.inc', 'renderAirMusicUi();'),
        ('063_main.cpp.inc', 'case UiMode::AirMusic:')]:
    assert text in (parts / file).read_text(), (file, text)
assert 'main_parts/076_main.cpp.inc' in (src / 'main.cpp').read_text()

# Leaving must free the port before the next app can claim it, and NFC Notes lets go in
# its own service, so Air Music has to be serviced after it.
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceAirMusic();')
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('stopAirMusic();') < change.index('gUiMode = mode;')

# Every launcher group must still fit, because a ninth item has nowhere to go.
menu = (parts / '004_main.cpp.inc').read_text()
table = menu[menu.index('kLauncherGroups[kLauncherGroupCount] = {'):menu.index('Preferences gPrefs;')]
groups = re.findall(r'rgb565_local\([^)]*\), (\d+),\s*\{(.*?)\}\}\}', table, re.S)
assert len(groups) == 9
for count, items in groups:
    assert int(count) == items.count('UiMode::') <= 9, (count, items)

code = r'''
#include "air_music.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
namespace fonts{const int Font0=0;}
constexpr uint16_t TFT_WHITE=0xffff;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
struct Rect{int x,y,w,h;};
struct Canvas{
 int x=0,y=0;std::vector<std::string> text;std::vector<Rect> bars;
 void fillScreen(uint16_t){text.clear();bars.clear();}
 void fillRoundRect(int xx,int yy,int w,int h,int,uint16_t){assert(xx>=0&&yy>=0&&w>0&&h>0&&xx+w<=240&&yy+h<=135);bars.push_back({xx,yy,w,h});}
 void drawRoundRect(int xx,int yy,int w,int h,int r,uint16_t c){fillRoundRect(xx,yy,w,h,r,c);bars.pop_back();}
 void setFont(const int*){}void setTextSize(int){}void setTextColor(uint16_t,uint16_t){}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 void print(const char*s){const int w=int(std::string(s).size())*6;assert(x>=0&&y>=0&&x+w<=240&&y+8<=135);text.push_back(s);}
}gCanvas;
struct Tone{float hz;uint32_t ms;int channel;bool cut;};
struct Speaker{
 std::vector<Tone> tones;int stops=0;size_t queued=0;
 bool tone(float hz,uint32_t ms,int channel,bool cut){tones.push_back({hz,ms,channel,cut});return true;}
 size_t isPlaying(uint8_t)const{return queued;}
 void stop(uint8_t){++stops;}
};
struct{Speaker Speaker;}M5Cardputer;
struct FakeWire{
 bool present=true;uint32_t um=0;int begins=0,ends=0,triggers=0,reads=0,lastByte=-1,left=0;
 std::vector<int> out;
 void begin(uint8_t sda,uint8_t scl,uint32_t hz){assert(sda==2&&scl==1&&hz==100000U);++begins;}
 void end(){++ends;}
 void beginTransmission(uint8_t a){assert(a==0x57);}
 void write(uint8_t b){lastByte=b;}
 int endTransmission(){if(!present)return 2;assert(lastByte==1);++triggers;return 0;}
 int requestFrom(uint8_t a,uint8_t n){assert(a==0x57&&n==3);if(!present)return 0;++reads;out={int(um>>16)&255,int(um>>8)&255,int(um)&255};return 3;}
 int read(){const int b=out.front();out.erase(out.begin());return b;}
}Wire;
enum class UiMode{Home,AirMusic};UiMode gUiMode=UiMode::AirMusic;
bool gLauncherVisible=false,toneAllowed=true;
uint32_t nowMs=1000,gLastRenderMs=999;int wakes=0;
uint32_t millis(){return nowMs;}
bool focusToneAllowed(){return toneAllowed;}
bool handleScreensaverWake(){++wakes;return false;}
struct{int changes=0;void invalidate(){++changes;}}gUiFramePolicy;
void renderLauncherOverlay(){}void renderDebugOverlay(){}
''' + (parts / '076_main.cpp.inc').read_text() + r'''
using namespace air_music;
struct Fake{bool present=true;uint32_t um=0;int triggers=0,reads=0;
 bool trigger(){++triggers;return present;}
 bool read(uint32_t&out){++reads;if(!present)return false;out=um;return true;}};
void at(App&a,uint32_t mm){a.hear(mm);}
// One whole measurement at this distance.
void sample(uint32_t mm){Wire.um=mm*1000;nowMs+=kMeasureMs;serviceAirMusic();}
int main(){
 // Closer is higher, and every band is the width promised.
 assert(noteFor(0)==-1&&noteFor(1)==9&&noteFor(39)==9&&noteFor(40)==9);
 assert(noteFor(79)==9&&noteFor(80)==8&&noteFor(399)==1&&noteFor(400)==0);
 assert(noteFor(439)==0&&noteFor(kFarMm)==-1&&noteFor(4500)==-1);
 for(int note=0;note<kNotes;++note){assert(noteFor(bandFar(note)-1)==note);if(note<kNotes-1)assert(noteFor(bandNear(note))==note);}
 for(int note=1;note<kNotes;++note)assert(kHz[note]>kHz[note-1]);
 // A note holds through a wobble, then moves.
 {App a;at(a,120);assert(a.note==7);at(a,159);assert(a.note==7);at(a,167);assert(a.note==7);
  at(a,168);assert(a.note==6);at(a,152);assert(a.note==6);at(a,151);assert(a.note==7);}
 // One lost echo keeps the note, and two end it.
 {App a;at(a,200);assert(a.note==5);at(a,4500);assert(a.note==5);at(a,200);at(a,4500);assert(a.note==5);
  at(a,0);assert(a.note==-1&&a.misses==0);at(a,4500);at(a,4500);assert(a.note==-1&&a.misses==0);}
 // The sensor gets its full 120 ms, without ever blocking, and the reading becomes millimeters.
 {App a;Fake s;poll(a,s,1000);assert(a.unit==Unit::Ready&&a.waiting&&s.triggers==1&&s.reads==0);
  s.um=200000;poll(a,s,1000+kMeasureMs-1);assert(s.reads==0&&s.triggers==1&&a.note==-1);
  poll(a,s,1000+kMeasureMs);assert(s.reads==1&&s.triggers==2&&a.note==5);
  // Unplugged in the middle: quiet at once, then a slow retry that notices its return.
  s.present=false;poll(a,s,1000+2*kMeasureMs);assert(a.unit==Unit::Missing&&a.note==-1&&!a.waiting);
  const uint32_t lost=1000+2*kMeasureMs;const int tried=s.triggers;
  poll(a,s,lost+kRetryMs-1);assert(s.triggers==tried);
  poll(a,s,lost+kRetryMs);assert(s.triggers==tried+1&&a.unit==Unit::Missing);
  s.present=true;poll(a,s,lost+2*kRetryMs);assert(a.unit==Unit::Ready&&a.waiting);}
 // Millis wrapping around doesn't stall it.
 {App a;Fake s;poll(a,s,0xffffffffU-10);s.um=200000;poll(a,s,kMeasureMs);assert(s.reads==1&&a.note==5);}
 // The real adapter: opens once, asks the real address, and plays what it hears.
 beginAirMusic();auto& speaker=M5Cardputer.Speaker;
 serviceAirMusic();assert(Wire.begins==1&&Wire.triggers==1);
 sample(200);assert(gAirMusic.note==5&&Wire.begins==1&&wakes==1);
 assert(speaker.tones.size()==1&&speaker.tones[0].hz==kHz[5]&&speaker.tones[0].cut&&speaker.tones[0].channel==kAirSpeakerChannel);
 // A held note queues more of itself without cutting, and never overfills the queue.
 speaker.queued=2;nowMs+=1;serviceAirMusic();assert(speaker.tones.size()==1);
 speaker.queued=1;nowMs+=1;serviceAirMusic();assert(speaker.tones.size()==2&&!speaker.tones[1].cut&&speaker.tones[1].hz==kHz[5]);
 // A new note cuts in at once.
 speaker.queued=2;sample(120);assert(gAirMusic.note==7&&speaker.tones.back().hz==kHz[7]&&speaker.tones.back().cut);
 // Taking the hand away stops the sound, and asks for a new frame.
 sample(4500);assert(gAirMusic.note==7);int changes=gUiFramePolicy.changes,stops=speaker.stops;
 sample(4500);assert(gAirMusic.note==-1&&speaker.stops==stops+1&&gUiFramePolicy.changes==changes+1);
 // Nothing plays over the assistant, or behind the menu.
 sample(200);assert(gAirMusic.note==5);toneAllowed=false;stops=speaker.stops;int sent=int(speaker.tones.size());
 sample(200);assert(gAirMusic.note==-1&&speaker.stops==stops+1&&int(speaker.tones.size())==sent);toneAllowed=true;
 sample(200);sample(200);assert(gAirMusic.note==5);gLauncherVisible=true;nowMs+=1;serviceAirMusic();assert(gAirMusic.note==-1);gLauncherVisible=false;
 // Leaving frees the port and the speaker, and coming back reopens it.
 sample(200);sample(200);assert(gAirMusic.note==5);stops=speaker.stops;int ends=Wire.ends;
 stopAirMusic();assert(Wire.ends==ends+1&&speaker.stops==stops+1&&gAirMusic.unit==Unit::Closed);
 stopAirMusic();assert(Wire.ends==ends+1);
 gUiMode=UiMode::Home;const int opens=Wire.begins;serviceAirMusic();assert(Wire.begins==opens);gUiMode=UiMode::AirMusic;
 beginAirMusic();serviceAirMusic();assert(Wire.begins==opens+1);
 // A missing sensor says so, quietly.
 Wire.present=false;sample(200);renderAirMusicUi();assert(gCanvas.text.back()=="Plug the ultrasonic into Grove");Wire.present=true;
 // The renderer stays on screen for every note and state, and shows every note's bar.
 for(int note=-1;note<kNotes;++note)for(Unit unit:{Unit::Closed,Unit::Ready,Unit::Missing}){
  gAirMusic.note=int8_t(note);gAirMusic.unit=unit;renderAirMusicUi();assert(int(gCanvas.bars.size())==kNotes);
  for(int i=1;i<kNotes;++i)assert(gCanvas.bars[i].h>gCanvas.bars[i-1].h);}
 gAirMusic.note=-1;gAirMusic.unit=Unit::Ready;renderAirMusicUi();assert(gCanvas.text.back()=="Wave a hand over the sensor");
 std::cout<<"PASS Air Music: note bands, wobble, lost echoes, 120 ms sensor timing, unplug and return, real I2C adapter, held and new notes, assistant and menu silence, port handover, renderer bounds\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-air-music-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
