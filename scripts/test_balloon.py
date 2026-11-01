#!/usr/bin/env python3
"""Exercise production Hot Air Balloon rules, sensors, timing and renderer on host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "balloon.h"'),
        ('004_main.cpp.inc', '{"Hot Air Balloon", UiMode::Balloon}'),
        ('004_main.cpp.inc', '{"Chess", UiMode::Chess}'),
        ('022_main.cpp.inc', 'mode == "balloon"'),
        ('024_main.cpp.inc', 'mode == UiMode::Balloon) beginBalloon();'),
        ('024_main.cpp.inc', 'gUiMode == UiMode::Balloon) stopBalloon();'),
        ('043_main.cpp.inc', 'cmd == "/balloon"'),
        ('048_main.cpp.inc', 'handleBalloonKey(key, keys)'),
        ('049_main.cpp.inc', 'gUiMode == UiMode::Balloon ||'),
        ('061_main.cpp.inc', 'renderBalloonUi();'),
        ('063_main.cpp.inc', 'case UiMode::Balloon:')]:
    assert text in (parts / file).read_text(), (file, text)
assert 'main_parts/078_main.cpp.inc' in (src / 'main.cpp').read_text()

# The Grove port changes hands the same way it does for Air Music.
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceBalloon();')
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('stopBalloon();') < change.index('gUiMode = mode;')

code = r'''
#include "balloon.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
using std::min;using std::max;
struct String:std::string {
 using std::string::string;String(std::string s):std::string(s){}
 template<class T,typename=std::enable_if_t<std::is_integral_v<T>>>String(T n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
};
namespace fonts{const int Font0=0,Font2=2;}
constexpr uint16_t TFT_WHITE=0xffff;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
struct Canvas{
 const int*font=&fonts::Font0;int x=0,y=0;std::vector<std::string> text;
 void onScreen(int xx,int yy){assert(xx>=0&&yy>=0&&xx<240&&yy<135);}
 void fillScreen(uint16_t){text.clear();}
 // Things drift off the sides of the screen, which clips them.
 void fillRect(int,int,int w,int h,uint16_t){assert(w>0&&h>0);}
 void fillRoundRect(int xx,int yy,int w,int h,int,uint16_t){onScreen(xx,yy);onScreen(xx+w-1,yy+h-1);}
 void fillCircle(int,int,int r,uint16_t){assert(r>0);}
 void fillEllipse(int,int,int,int,uint16_t){}
 void fillTriangle(int,int,int,int,int,int,uint16_t){}
 void drawLine(int,int,int,int,uint16_t){}
 void drawPixel(int xx,int yy,uint16_t){onScreen(xx,yy);}
 void setFont(const int*f){font=f;}void setTextSize(int){}
 void setTextColor(uint16_t){}void setTextColor(uint16_t,uint16_t){}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 int textWidth(const String&s){return int(s.size())*(font==&fonts::Font0?6:8);}
 void print(const String&s){onScreen(x,y);onScreen(x+textWidth(s)-1,y+7);text.push_back(s);}
}gCanvas;
struct Keyboard_Class{struct KeysState{bool ctrl=false,alt=false,opt=false,enter=false;};};
// A fake Grove bus with both units on it.
struct FakeWire{
 bool envHere=true,stickHere=true;uint8_t reading[6]={};int8_t x=0;bool pressed=false;
 uint8_t address=0;std::vector<uint8_t> sent,out;int measures=0;bool stopped=true;
 void beginTransmission(uint8_t a){address=a;sent.clear();}
 void write(uint8_t b){sent.push_back(b);}
 void write(const uint8_t*b,size_t n){sent.insert(sent.end(),b,b+n);}
 int endTransmission(bool stop=true){
  stopped=stop;if(address==0x44){if(!envHere)return 2;assert(stop&&sent==std::vector<uint8_t>({0x24,0x00}));++measures;return 0;}
  assert(address==0x63&&!stop&&sent.size()==1);return stickHere?0:2;}
 int requestFrom(uint8_t a,uint8_t n){
  out.clear();
  if(a==0x44){assert(n==6);if(!envHere)return 0;out.assign(reading,reading+6);return 6;}
  assert(a==0x63);if(!stickHere)return 0;
  if(sent[0]==0x60){assert(n==2);out={uint8_t(x),0};}
  else{assert(sent[0]==0x20&&n==1);out={uint8_t(pressed?0:1)};}
  return n;}
 int read(){const int b=out.front();out.erase(out.begin());return b;}
}Wire;
struct FakeBus{int opens=0,closes=0;void open(){++opens;}void close(){++closes;}}gAirSensor;
enum class UiMode{Home,Balloon};UiMode gUiMode=UiMode::Balloon;
bool gLauncherVisible=false;
uint32_t nowMs=1000,gLastRenderMs=999,seed=99;int wakes=0;std::vector<int> tones;
uint32_t millis(){return nowMs;}uint32_t esp_random(){return ++seed;}
void focusTone(uint16_t hz,uint16_t){tones.push_back(hz);}
bool handleScreensaverWake(){++wakes;return false;}
struct{int changes=0;void invalidate(){++changes;}}gUiFramePolicy;
void renderLauncherOverlay(){}void renderDebugOverlay(){}
''' + (parts / '078_main.cpp.inc').read_text() + r'''
using namespace balloon;
// Fill in a real SHT30 reading for this humidity.
void setHumidity(double percent){
 const uint32_t raw=uint32_t(percent*65535.0/100.0+0.5);
 Wire.reading[0]=0x66;Wire.reading[1]=0x66;Wire.reading[2]=crc8(Wire.reading,2);
 Wire.reading[3]=uint8_t(raw>>8);Wire.reading[4]=uint8_t(raw);Wire.reading[5]=crc8(Wire.reading+3,2);
}
Thing& put(Game&g,Kind kind,int x,int y){Thing&t=g.things[0];t=Thing{};t.kind=kind;t.x=x*kOne;t.y=y*kOne;return t;}
void key(char k){Keyboard_Class::KeysState keys;keys.enter=k=='\n';assert(handleBalloonKey(k,keys));}
void healthy(const Game&g){
 assert(g.x>=kLeftX*kOne&&g.x<=kRightX*kOne&&g.y>=kTopY*kOne&&g.y<=kRestY*kOne);
 assert(g.heat>=0&&g.heat<=kMaxHeat&&g.lives<=kLives&&g.scroll()<=kTopScroll);
 for(const auto&t:g.things)if(t.kind!=Kind::None){assert((t.x>>8)>=-12&&(t.x>>8)<=250);
  assert((t.y>>8)>=kTopY+8&&(t.y>>8)<kRestY);}
}
int main(){
 // The checksum matches the SHT30 datasheet's example, and bad readings are refused.
 {const uint8_t sample[2]={0xBE,0xEF};assert(crc8(sample,2)==0x92);}
 setHumidity(50);uint16_t centi=0;assert(humidityCenti(Wire.reading,centi)&&centi>=4999&&centi<=5001);
 Wire.reading[4]^=1;assert(!humidityCenti(Wire.reading,centi));
 setHumidity(0);assert(humidityCenti(Wire.reading,centi)&&centi==0);
 setHumidity(100);assert(humidityCenti(Wire.reading,centi)&&centi==10000);
 // Only a quick rise is a breath. The first reading, room drift, sensor noise and a damp
 // sensor drying out never are.
 {Breath b;assert(b.hear(5000)==0);assert(b.hear(5000)==0);assert(b.hear(5015)==0);assert(b.hear(5005)==0);
  for(int i=0;i<500;++i)assert(b.hear(uint16_t(5005+i*10))==0);
  Breath wet;wet.hear(5000);const int32_t puff=wet.hear(5200);assert(puff==(200-kBreathRise)*kBreathHeat);
  assert(wet.hear(5100)==0);assert(wet.hear(9000)>kMaxHeat);for(int rh=8990;rh>5000;rh-=10)assert(wet.hear(uint16_t(rh))==0);}
 // Heat lifts it, cooling lowers it, and the ground never hurts.
 Game g;g.reset(0);assert(g.phase==Phase::Ready&&g.lives==kLives&&g.y==kRestY*kOne);
 g.step(0);assert(g.y==kRestY*kOne&&g.phase==Phase::Ready);
 g.addHeat(0);assert(g.phase==Phase::Ready);
 g.addHeat(kMaxHeat);assert(g.phase==Phase::Playing);
 for(int i=0;i<60;++i)g.step(0);assert(g.y<(kRestY-20)*kOne);
 const int high=g.y;for(int i=0;i<120;++i)g.step(0);assert(g.heat<kMaxHeat/3);
 for(int i=0;i<600;++i){g.step(0);for(auto&t:g.things)t=Thing{};}assert(g.y==kRestY*kOne&&g.y>high&&g.lives==kLives&&g.phase==Phase::Playing&&g.heat==0&&g.vy==0);
 // It can't leave the sky.
 for(int i=0;i<400;++i){g.addHeat(kMaxHeat);g.step(0);for(auto&t:g.things)t=Thing{};}assert(g.y==kTopY*kOne);
 // Heat stops at the top of the gauge.
 g.addHeat(kMaxHeat*3);assert(g.heat==kMaxHeat);
 // Steering has a dead zone and walls.
 g.reset(1);g.addHeat(500);const int32_t still=g.x;g.step(kStickDead);g.step(-kStickDead);assert(g.x==still);
 g.step(kStickFull);assert(g.x==still+kSteerSpeed);
 for(int i=0;i<300;++i){g.step(127);for(auto&t:g.things)t=Thing{};}assert(g.x==kRightX*kOne);
 for(int i=0;i<300;++i){g.step(-128);for(auto&t:g.things)t=Thing{};}assert(g.x==kLeftX*kOne);
 g.nudge(1);for(int i=0;i<kNudgeSteps+5;++i){g.step(0);for(auto&t:g.things)t=Thing{};}assert(g.x==(kLeftX*kOne)+kNudgeSteps*kNudgeSpeed);
 // A star scores and vanishes, and a tenth one changes the sky.
 g.reset(1);g.addHeat(500);put(g,Kind::Star,kStartX,g.y>>8);g.step(0);
 assert(g.stars==1&&g.best==1&&(g.events&kGotStar)&&g.things[0].kind==Kind::None);
 g.stars=9;g.events=0;put(g,Kind::Star,kStartX,g.y>>8);g.step(0);assert(g.stars==10&&g.level()==1&&(g.events&kLevelUp));
 // A far-off star is left alone.
 g.events=0;put(g,Kind::Star,kStartX+30,g.y>>8);g.step(0);assert(g.stars==10&&g.things[0].kind==Kind::Star);
 // A bird costs a heart, then there's a moment of safety, then it can hurt again.
 g.reset(1);g.addHeat(900);Thing&bird=put(g,Kind::Bird,kStartX,0);
 bird.y=(g.y>>8)*kOne;g.frame=15;bird.phase=0;g.step(0);assert(g.lives==kLives-1&&(g.events&kHurt)&&g.hurtSteps>0);
 for(int i=0;i<kHurtSteps/2;++i){put(g,Kind::Bird,kStartX,g.y>>8);g.heat=400;g.step(0);assert(g.lives==kLives-1);}
 // Hearts come back, up to the limit.
 g.reset(1);g.addHeat(500);g.lives=1;put(g,Kind::Heart,kStartX,g.y>>8);g.step(0);assert(g.lives==2&&(g.events&kGotHeart));
 g.lives=kLives;put(g,Kind::Heart,kStartX,g.y>>8);g.step(0);assert(g.lives==kLives);
 // Three bumps end the game, then the score shows, then a new game is ready, and the best stays.
 g.reset(1);g.addHeat(500);g.stars=7;g.best=7;
 for(int hit=0;hit<kLives;++hit){g.hurtSteps=0;g.heat=500;put(g,Kind::Bird,g.x>>8,g.y>>8);g.step(0);assert(g.lives==kLives-hit-1);}
 assert(g.phase==Phase::Over&&g.lives==0);g.events=0;
 for(int i=0;i<kOverSteps-1;++i){g.addHeat(500);g.step(100);assert(g.phase==Phase::Over&&g.stars==7);}
 g.step(0);assert(g.phase==Phase::Ready&&g.stars==0&&g.lives==kLives&&g.best==7);
 // The wind picks up and things come faster and birdier, up to a limit.
 g.reset(1);assert(g.scroll()==kStartScroll);g.stars=1000;assert(g.scroll()==kTopScroll);
 // Long random play stays sane, and a child chasing stars does well.
 long totalStars=0;
 for(uint32_t s=1;s<=100;++s){Game r;r.reset(s*2654435761U);r.addHeat(500);int stick=0;
  for(int frame=0;frame<9000&&r.phase!=Phase::Over;++frame){
   const Thing* star=nullptr;for(const auto&t:r.things)if(t.kind==Kind::Star&&(t.x>>8)>(r.x>>8)-10&&(!star||t.x<star->x))star=&t;
   const int target=star?(star->y>>8):50;
   if((r.y>>8)>target)r.addHeat(30);
   stick=star&&(star->x>>8)<(r.x>>8)?-127:0;
   r.step(stick);healthy(r);}
  totalStars+=r.best;}
 assert(totalStars/100>=10);
 // Sensor timing: a reading is collected after the measurement, four times a second.
 FakeWire& bus=Wire;BalloonSensors sensors;
 {Env env;uint32_t t=5000;setHumidity(50);assert(pollEnv(env,sensors,t)==0&&env.unit==air_music::Unit::Ready&&env.waiting&&bus.measures==1);
  assert(pollEnv(env,sensors,t+kEnvMeasureMs-1)==0&&env.waiting);
  assert(pollEnv(env,sensors,t+kEnvMeasureMs)==0&&!env.waiting&&env.breath.primed);
  assert(pollEnv(env,sensors,t+kEnvEveryMs-1)==0&&bus.measures==1);
  assert(pollEnv(env,sensors,t+kEnvEveryMs)==0&&bus.measures==2);
  setHumidity(53);assert(pollEnv(env,sensors,t+kEnvEveryMs+kEnvMeasureMs)>0);
  // A garbled reading isn't a breath.
  pollEnv(env,sensors,t+2*kEnvEveryMs);setHumidity(70);bus.reading[5]^=0xff;assert(pollEnv(env,sensors,t+2*kEnvEveryMs+kEnvMeasureMs)==0);
  // Unplugging it mid-reading stops cleanly, retries slowly, and notices it coming back.
  pollEnv(env,sensors,t+3*kEnvEveryMs);bus.envHere=false;const uint32_t lost=t+3*kEnvEveryMs+kEnvMeasureMs;
  assert(pollEnv(env,sensors,lost)==0&&env.unit==air_music::Unit::Missing&&!env.breath.primed);
  const int measures=bus.measures;bus.envHere=true;pollEnv(env,sensors,lost+kRetryMs-1);assert(bus.measures==measures);
  pollEnv(env,sensors,lost+kRetryMs);assert(bus.measures==measures+1&&env.unit==air_music::Unit::Ready);}
 // The joystick: signed X, a dead button at rest, and a slow retry when it's missing.
 {Stick stick;bus.x=-90;bus.pressed=false;pollStick(stick,sensors,100);assert(stick.unit==air_music::Unit::Ready&&stick.x==-90&&!stick.pressed);
  bus.x=127;bus.pressed=true;pollStick(stick,sensors,133);assert(stick.x==127&&stick.pressed);
  bus.stickHere=false;pollStick(stick,sensors,166);assert(stick.unit==air_music::Unit::Missing&&stick.x==0&&!stick.pressed);
  bus.stickHere=true;pollStick(stick,sensors,166+kRetryMs-1);assert(stick.unit==air_music::Unit::Missing);
  pollStick(stick,sensors,166+kRetryMs);assert(stick.unit==air_music::Unit::Ready);bus.x=0;bus.pressed=false;}
 // Production service: blowing starts it, the button burns, and the stick steers.
 setHumidity(50);beginBalloon();assert(gBalloon.phase==Phase::Ready);
 serviceBalloon();assert(gAirSensor.opens==1);
 nowMs+=kEnvMeasureMs;serviceBalloon();assert(gBalloonEnv.breath.primed&&gBalloon.phase==Phase::Ready);
 nowMs+=kEnvEveryMs;serviceBalloon();setHumidity(53);nowMs+=kEnvMeasureMs;const int woke=wakes;serviceBalloon();
 assert(gBalloon.phase==Phase::Playing&&gBalloon.heat>0&&wakes>woke);
 gBalloon.heat=100;bus.pressed=true;nowMs+=balloon::kStepMs;serviceBalloon();assert(gBalloon.heat==100+kBurnerHeat-kCooling);bus.pressed=false;
 bus.x=127;int32_t where=gBalloon.x;nowMs+=balloon::kStepMs;serviceBalloon();assert(gBalloon.x>where);bus.x=0;
 // A long stall moves at most a few steps, and the menu freezes it without a jump.
 uint16_t frame=gBalloon.frame;nowMs+=5000;serviceBalloon();assert(gBalloon.frame-frame<=kBalloonMostSteps&&gBalloonStepMs==nowMs);
 gLauncherVisible=true;frame=gBalloon.frame;nowMs+=5000;serviceBalloon();assert(gBalloon.frame==frame);
 gLauncherVisible=false;nowMs+=1;serviceBalloon();assert(gBalloon.frame==frame);
 // With nothing plugged in it says so, and the keyboard plays the whole game.
 bus.envHere=bus.stickHere=false;beginBalloon();serviceBalloon();assert(gBalloonEnv.unit==air_music::Unit::Missing);
 renderBalloonUi();assert(gCanvas.text.back()=="Press Space to fly");
 key(' ');assert(gBalloon.phase==Phase::Playing&&gBalloon.heat==kKeyHeat);key('w');key(';');key('\n');assert(gBalloon.heat==4*kKeyHeat);
 key('a');assert(gBalloon.nudgeDir==-1);key(',');key('d');assert(gBalloon.nudgeDir==1);key('/');key('z');assert(gBalloon.heat==4*kKeyHeat);
 {Keyboard_Class::KeysState ctrl;ctrl.ctrl=true;const int32_t held=gBalloon.heat;assert(handleBalloonKey(' ',ctrl));assert(gBalloon.heat==held);}
 bus.envHere=bus.stickHere=true;
 // Each event has its own sound, and a bump drowns out the rest.
 tones.clear();gBalloon.events=kHurt|kGotStar;balloonSounds();assert(tones==std::vector<int>{220});
 tones.clear();gBalloon.events=kLevelUp;balloonSounds();assert(tones==std::vector<int>{1760});
 tones.clear();gBalloon.events=kGotHeart;balloonSounds();assert(tones==std::vector<int>{1047});
 tones.clear();gBalloon.events=kGotStar;balloonSounds();assert(tones==std::vector<int>{1568});
 // Leaving frees the port.
 const int closes=gAirSensor.closes;stopBalloon();assert(gAirSensor.closes==closes+1);
 gUiMode=UiMode::Home;const int opens=gAirSensor.opens;serviceBalloon();assert(gAirSensor.opens==opens);gUiMode=UiMode::Balloon;
 // The renderer stays on screen in every phase, sky, score and heart count.
 for(int phase=0;phase<3;++phase)for(int stars:{0,9,10,20,30,999})for(int lives=0;lives<=kLives;++lives)for(int32_t h:{int32_t(0),int32_t(1),kMaxHeat}){
  gBalloon.reset(3);gBalloon.phase=Phase(phase);gBalloon.stars=uint16_t(stars);gBalloon.best=9999;gBalloon.lives=uint8_t(lives);gBalloon.heat=h;gBalloon.frame=uint16_t(stars*97);
  for(int i=0;i<kThings;++i){put(gBalloon,Kind(1+i%3),i*50-12,30+i*10);std::swap(gBalloon.things[0],gBalloon.things[i]);}
  renderBalloonUi();}
 std::cout<<"PASS Hot Air Balloon: SHT30 checksum, breath detection, lift and cooling, steering, stars, birds, hearts, levels, "<<totalStars/100<<" stars on average in 100 chases, sensor timing, unplugging, keyboard play, sounds, port handover, renderer bounds\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-balloon-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
