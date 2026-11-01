#!/usr/bin/env python3
"""Exercise production Flappy Bird rules, controls, timing and renderer on host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "flappy.h"'),
        ('004_main.cpp.inc', '{"Flappy Bird", UiMode::Flappy}'),
        ('004_main.cpp.inc', '{"KeyRush", UiMode::KeyRush}'),
        ('022_main.cpp.inc', 'mode == "flappy"'),
        ('024_main.cpp.inc', 'mode == UiMode::Flappy) beginFlappy();'),
        ('024_main.cpp.inc', 'gUiMode == UiMode::Flappy) stopFlappy();'),
        ('043_main.cpp.inc', 'cmd == "/flappy"'),
        ('048_main.cpp.inc', 'handleFlappyKey(key, keys)'),
        ('061_main.cpp.inc', 'renderFlappyUi();'),
        ('063_main.cpp.inc', 'case UiMode::Flappy:')]:
    assert text in (parts / file).read_text(), (file, text)
assert 'main_parts/077_main.cpp.inc' in (src / 'main.cpp').read_text()

# The Grove port changes hands the same way it does for Air Music.
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceFlappy();')
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('stopFlappy();') < change.index('gUiMode = mode;')

code = r'''
#include "flappy.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
struct String:std::string {
 using std::string::string;String(std::string s):std::string(s){}
 template<class T,typename=std::enable_if_t<std::is_integral_v<T>>>String(T n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
};
namespace fonts{const int Font0=0,Font2=2;}
constexpr uint16_t TFT_WHITE=0xffff,TFT_BLACK=0;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
struct Canvas{
 const int*font=&fonts::Font0;int x=0,y=0;std::vector<std::string> text;
 void onScreen(int xx,int yy){assert(xx>=0&&yy>=0&&xx<240&&yy<135);}
 void fillScreen(uint16_t){text.clear();}
 // Pipes slide off either side, so only the height has to fit.
 void fillRect(int,int yy,int w,int h,uint16_t){assert(w>0&&h>0&&yy>=0&&yy+h<=135);}
 void fillRoundRect(int xx,int yy,int w,int h,int,uint16_t){onScreen(xx,yy);onScreen(xx+w-1,yy+h-1);}
 void fillCircle(int xx,int yy,int r,uint16_t){onScreen(xx-r,yy-r);onScreen(xx+r,yy+r);}
 void fillTriangle(int a,int b,int c,int d,int e,int f,uint16_t){onScreen(a,b);onScreen(c,d);onScreen(e,f);}
 void drawPixel(int xx,int yy,uint16_t){onScreen(xx,yy);}
 void setFont(const int*f){font=f;}void setTextSize(int){}
 void setTextColor(uint16_t){}void setTextColor(uint16_t,uint16_t){}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 int textWidth(const String&s){return int(s.size())*(font==&fonts::Font0?6:8);}
 void print(const String&s){onScreen(x,y);onScreen(x+textWidth(s)-1,y+7);text.push_back(s);}
}gCanvas;
struct Keyboard_Class{struct KeysState{bool ctrl=false,alt=false,opt=false;};};
struct FakeSensor{
 bool present=true;uint32_t um=0;int opens=0,closes=0;
 void open(){++opens;}void close(){++closes;}
 bool trigger(){return present;}
 bool read(uint32_t&out){if(!present)return false;out=um;return true;}
}gAirSensor;
enum class UiMode{Home,Flappy};UiMode gUiMode=UiMode::Flappy;
bool gLauncherVisible=false;
uint32_t nowMs=1000,gLastRenderMs=999,seed=99;int wakes=0;std::vector<int> tones;
uint32_t millis(){return nowMs;}uint32_t esp_random(){return ++seed;}
void focusTone(uint16_t hz,uint16_t){tones.push_back(hz);}
bool handleScreensaverWake(){++wakes;return false;}
struct{int changes=0;void invalidate(){++changes;}}gUiFramePolicy;
void renderLauncherOverlay(){}void renderDebugOverlay(){}
''' + (parts / '077_main.cpp.inc').read_text() + r'''
using namespace flappy;
int centerOf(const Pipe&p){return p.gapTop+p.gapSize/2;}
const Pipe& nextPipe(const Game&g){
 const Pipe* next=nullptr;
 for(const auto&p:g.pipes)if((p.x>>8)+kPipeWidth>=kBirdX-kBirdHalf&&(!next||p.x<next->x))next=&p;
 return *next;
}
uint32_t mmFor(int y){return kHandLowMm+uint32_t(kHandBottomY-y)*(kHandHighMm-kHandLowMm)/(kHandBottomY-kHandTopY);}
// A hand that aims for each gap, seen only once per sensor reading.
int handPilot(uint32_t s){
 Game g;g.reset(s);uint32_t mm=180;
 for(int frame=0;frame<9000&&g.phase!=Phase::Lost;++frame){
  if(frame%4==0)mm=mmFor(centerOf(nextPipe(g)));
  g.step(true,mm);
 }
 assert(g.phase==Phase::Playing);
 return g.score;
}
// A player tapping a key, with no sensor.
int flapPilot(uint32_t s){
 Game g;g.reset(s);
 for(int frame=0;frame<9000&&g.phase!=Phase::Lost;++frame){
  if(g.phase==Phase::Ready||((g.y>>8)>centerOf(nextPipe(g))+4&&g.vy>=0))g.flap();
  g.step(false,0);
 }
 return g.score;
}
void key(char k,bool ctrl=false){Keyboard_Class::KeysState keys;keys.ctrl=ctrl;assert(handleFlappyKey(k,keys));}
int main(){
 Game g;g.reset(0);
 assert(g.phase==Phase::Ready&&g.y==60*kOne&&g.score==0);
 for(int i=0;i<kPipes;++i){assert((g.pipes[i].x>>8)>=kFirstPipeX&&g.pipes[i].gapSize==kLargestGap);
  assert(g.pipes[i].gapTop>=kGapMargin&&g.pipes[i].gapTop+g.pipes[i].gapSize<=kGroundY-kGapMargin);}
 // Nothing moves until someone plays.
 const int32_t waiting=g.pipes[0].x;for(int i=0;i<100;++i)g.step(false,0);assert(g.phase==Phase::Ready&&g.pipes[0].x==waiting&&g.y==60*kOne);
 // A hand starts it, and so does a key.
 g.step(true,180);assert(g.phase==Phase::Playing&&g.pipes[0].x<waiting);
 g.reset(5);g.flap();assert(g.phase==Phase::Playing&&g.vy==kFlap&&(g.events&kFlapped));
 // Flapping rises, falling is capped, and the ceiling doesn't kill.
 const int32_t before=g.y;g.step(false,0);assert(g.y<before);
 g.reset(5);g.flap();for(int i=0;i<400&&g.phase==Phase::Playing;++i){g.flap();g.step(false,0);assert(g.y>=kBirdTopY*kOne);if(i>60)break;}
 assert(g.phase==Phase::Playing&&g.y==kBirdTopY*kOne);
 g.reset(5);g.flap();g.vy=0;for(int i=0;i<30;++i){const int32_t y=g.y;g.step(false,0);if(g.phase!=Phase::Playing)break;assert(g.vy<=kFallLimit&&g.y-y<=kFallLimit);}
 // The ground ends it once, with one sound, then a moment later the next game is ready.
 g.reset(5);g.score=9;g.best=9;g.flap();g.y=120*kOne;g.vy=kFallLimit;g.events=0;g.step(false,0);
 assert(g.phase==Phase::Lost&&(g.events&kCrashed));g.events=0;
 for(int i=0;i<kLostSteps-1;++i){g.flap();g.step(true,180);assert(g.phase==Phase::Lost&&g.events==0&&g.score==9);}
 g.step(false,0);assert(g.phase==Phase::Ready&&g.score==0&&g.best==9);
 // The hand maps to the height, further is higher, and stray readings stay on screen.
 assert(heightFor(0)==kHandBottomY*kOne&&heightFor(kHandLowMm)==kHandBottomY*kOne);
 assert(heightFor(kHandHighMm)==kHandTopY*kOne&&heightFor(4000)==kHandTopY*kOne);
 for(uint32_t mm=kHandLowMm;mm<kHandHighMm;++mm)assert(heightFor(mm+1)<=heightFor(mm));
 // The bird follows at a limited speed, and arrives exactly.
 g.reset(5);g.step(true,kHandHighMm);assert(g.y==60*kOne-kHandSpeed);
 for(int i=0;i<30;++i)g.step(true,kHandHighMm);assert(g.y==kHandTopY*kOne&&g.vy==0);
 // The hand can never steer into the ground.
 assert(kHandBottomY+kBirdHalf<kGroundY);
 // Every gap can be reached by hand.
 assert(kGapMargin+kSmallestGap/2>=kHandTopY&&kGroundY-kGapMargin-kSmallestGap/2<=kHandBottomY);
 // One lost echo keeps the hand, two lose it, and the bird then falls.
 {Hand h;h.hear(200);assert(h.here&&h.mm==200);h.hear(4500);assert(h.here&&h.mm==200);h.hear(150);assert(h.mm==150);
  h.hear(4500);h.hear(0);assert(!h.here&&h.misses==0);h.hear(4500);assert(!h.here);h.hear(kHandGoneMm-1);assert(h.here);}
 g.reset(5);g.step(true,180);const int32_t hovering=g.y;g.step(false,0);assert(g.y>hovering);
 // Both pipe edges kill, and the middle of the gap is safe.
 for(int side=0;side<3;++side){g.reset(5);g.flap();Pipe&p=g.pipes[0];p.x=kBirdX*kOne;p.gapTop=50;p.gapSize=50;
  g.y=(side==0?50:side==1?100:75)*kOne;g.vy=0;g.pipes[1].x=g.pipes[2].x=300*kOne;g.step(true,mmFor(g.y>>8));
  assert((g.phase==Phase::Lost)==(side!=2));}
 // Each pipe scores once, and beeps, then comes back round further on with a new gap.
 g.best=0;g.reset(5);g.flap();g.pipes[0].x=(kBirdX-kBirdHalf-kPipeWidth)*kOne;g.pipes[0].gapTop=30;g.pipes[0].gapSize=60;g.y=60*kOne;g.events=0;
 for(int i=0;i<8;++i)g.step(true,mmFor(60));assert(g.score==1&&g.best==1&&(g.events&kScored));
 for(int i=0;i<100;++i)g.step(true,mmFor(60));
 // Pipes stay evenly spaced forever, and the game gets harder, up to a limit.
 g.reset(7);g.flap();for(int frame=0;frame<20000;++frame){g.y=60*kOne;g.vy=0;g.phase=Phase::Playing;g.step(true,mmFor(60));
  int32_t xs[kPipes];for(int i=0;i<kPipes;++i)xs[i]=g.pipes[i].x;std::sort(xs,xs+kPipes);
  assert(xs[2]-xs[0]<=2*kPipeSpacing*kOne+2*g.speed());assert((xs[0]>>8)+kPipeWidth>=-5);}
 assert(g.score>100&&g.speed()==kTopSpeed&&g.currentGap()==kSmallestGap);
 // Real play: the hand and the keys can both get a long way, on any layout.
 int handLow=100000,flapLow=100000;
 for(uint32_t s=1;s<=100;++s){handLow=std::min(handLow,handPilot(s*2654435761U));flapLow=std::min(flapLow,flapPilot(s*2654435761U));}
 assert(handLow>=50&&flapLow>=20);
 // Production service: steady timing, hand steering and sounds.
 beginFlappy();assert(gFlappy.phase==Phase::Ready&&gFlappyHand.unit==air_music::Unit::Closed);
 serviceFlappy();assert(gAirSensor.opens==1&&gFlappyHand.unit==air_music::Unit::Ready&&gFlappyHand.waiting);
 const int changes=gUiFramePolicy.changes;nowMs+=kStepMs-1;serviceFlappy();assert(gUiFramePolicy.changes==changes);
 nowMs+=1;serviceFlappy();assert(gUiFramePolicy.changes==changes+1);
 gAirSensor.um=200*1000;nowMs+=air_music::kMeasureMs;serviceFlappy();
 assert(gFlappyHand.here&&gFlappyHand.mm==200&&gFlappy.phase==Phase::Playing&&wakes>0);
 // A long stall moves at most a few steps, then carries on from now.
 int32_t pipeX=gFlappy.pipes[0].x;nowMs+=5000;serviceFlappy();
 assert(pipeX-gFlappy.pipes[0].x<=kFlappyMostSteps*gFlappy.speed()&&gFlappyStepMs==nowMs);
 // The menu freezes the game, and closing it doesn't jump.
 gLauncherVisible=true;pipeX=gFlappy.pipes[0].x;nowMs+=5000;serviceFlappy();assert(gFlappy.pipes[0].x==pipeX);
 gLauncherVisible=false;nowMs+=1;serviceFlappy();assert(gFlappy.pipes[0].x==pipeX);
 // With no sensor at all it says so, and keys still play.
 beginFlappy();gAirSensor.present=false;serviceFlappy();assert(gFlappyHand.unit==air_music::Unit::Missing);
 renderFlappyUi();assert(gCanvas.text.back()=="Press Space to flap");
 tones.clear();key(' ');assert(gFlappy.phase==Phase::Playing&&tones==std::vector<int>{880}&&gFlappy.events==0);
 beginFlappy();key(' ',true);assert(gFlappy.phase==Phase::Ready);gAirSensor.present=true;
 // A crash and a point each make their own sound, and the crash wins.
 tones.clear();gFlappy.events=kCrashed|kScored;flappySounds();assert(tones==std::vector<int>{196});
 tones.clear();gFlappy.events=kScored|kFlapped;flappySounds();assert(tones==std::vector<int>{1319});
 // Leaving frees the port.
 const int closes=gAirSensor.closes;stopFlappy();assert(gAirSensor.closes==closes+1);
 gUiMode=UiMode::Home;const int opens=gAirSensor.opens;serviceFlappy();assert(gAirSensor.opens==opens);gUiMode=UiMode::Flappy;
 // The renderer stays on screen in every phase, with the bird and pipes anywhere.
 for(int phase=0;phase<3;++phase)for(int y:{7,60,122,127})for(int x=-30;x<300;x+=7)for(int score:{0,7,999}){
  gFlappy.reset(3);gFlappy.phase=Phase(phase);gFlappy.y=y*kOne;gFlappy.score=uint16_t(score);gFlappy.best=9999;
  gFlappy.pipes[0].x=x*kOne;gFlappy.pipes[0].gapTop=kGapMargin;gFlappy.pipes[0].gapSize=kSmallestGap;
  gFlappy.pipes[1].x=(x+60)*kOne;gFlappy.pipes[1].gapTop=kGroundY-kGapMargin-kSmallestGap;gFlappy.pipes[1].gapSize=kSmallestGap;
  renderFlappyUi();}
 std::cout<<"PASS Flappy Bird: hand steering, flapping, crashes, scoring, difficulty, pilots reached "<<handLow<<" by hand and "<<flapLow<<" by key on 100 layouts, timing, menu freeze, no sensor, sounds, port handover, renderer bounds\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-flappy-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
