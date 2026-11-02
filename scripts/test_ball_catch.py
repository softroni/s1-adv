#!/usr/bin/env python3
"""Exercise production Ball Catch rules, controls, timing and renderer on host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "ball_catch.h"'),
        ('001_main.cpp.inc', 'kLauncherGroupCount = 9'),
        ('004_main.cpp.inc', '{"Ball Catch", UiMode::BallCatch}'),
        ('004_main.cpp.inc', '{"Arcade", "Sensor and joystick games"'),
        ('022_main.cpp.inc', 'mode == "ball_catch"'),
        ('024_main.cpp.inc', 'mode == UiMode::BallCatch) beginBallCatch();'),
        ('024_main.cpp.inc', 'gUiMode == UiMode::BallCatch) stopBallCatch();'),
        ('043_main.cpp.inc', 'cmd == "/catch"'),
        ('048_main.cpp.inc', 'handleBallCatchKey(key, keys)'),
        ('049_main.cpp.inc', 'gUiMode == UiMode::BallCatch ||'),
        ('061_main.cpp.inc', 'renderBallCatchUi();'),
        ('063_main.cpp.inc', 'case UiMode::BallCatch:')]:
    assert text in (parts / file).read_text(), (file, text)
assert 'main_parts/080_main.cpp.inc' in (src / 'main.cpp').read_text()

# The Grove port changes hands the same way it does for the other games.
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceBallCatch();')
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('stopBallCatch();') < change.index('gUiMode = mode;')

code = r'''
#include "air_music.h"
#include "balloon.h"
#include "balloon_audio.h"
#include "ball_catch.h"
#include "launcher_shortcuts.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
using std::max;
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
 void fillRect(int xx,int yy,int w,int h,uint16_t){assert(w>0&&h>0);onScreen(xx,yy);onScreen(xx+w-1,yy+h-1);}
 void fillRoundRect(int xx,int yy,int w,int h,int,uint16_t){onScreen(xx,yy);onScreen(xx+w-1,yy+h-1);}
 void fillCircle(int xx,int yy,int r,uint16_t){onScreen(xx-r,yy-r);onScreen(xx+r,yy+r);}
 void fillEllipse(int xx,int yy,int rx,int ry,uint16_t){onScreen(xx-rx,yy-ry);onScreen(xx+rx,yy+ry);}
 void fillTriangle(int a,int b,int c,int d,int e,int f,uint16_t){onScreen(a,b);onScreen(c,d);onScreen(e,f);}
 void drawLine(int a,int b,int c,int e,uint16_t){onScreen(a,b);onScreen(c,e);}
 void setFont(const int*f){font=f;}void setTextSize(int){}
 void setTextColor(uint16_t){}void setTextColor(uint16_t,uint16_t){}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 int textWidth(const String&s){return int(s.size())*(font==&fonts::Font0?6:8);}
 void print(const String&s){onScreen(x,y);onScreen(x+textWidth(s)-1,y+7);text.push_back(s);}
}gCanvas;
struct Keyboard_Class{struct KeysState{bool ctrl=false,alt=false,opt=false,enter=false;};};
// The pieces Ball Catch borrows from the games before it.
struct FakeBus{int opens=0,closes=0;void open(){++opens;}void close(){++closes;}}gAirSensor;
struct FakeStick{bool here=true,pressed=false;int8_t x=0;
 bool stickRead(int8_t&sx,bool&p){if(!here)return false;sx=x;p=pressed;return true;}}gBalloonSensors;
constexpr uint8_t kBalloonAudioChannel=4;constexpr int kBalloonStickRightSign=1;
struct Speaker{int stops=0;void stop(uint8_t c){assert(c==4);++stops;}};
struct{Speaker Speaker;}M5Cardputer;
balloon_audio::Player gBalloonAudio;int audioRuns=0;
void serviceBalloonAudio(){++audioRuns;}
void drawBalloonStar(int,int,int,uint16_t){}void drawBalloonHeart(int,int,int,uint16_t){}
enum class UiMode{Home,BallCatch};UiMode gUiMode=UiMode::BallCatch;
bool gLauncherVisible=false;
uint32_t nowMs=1000,gLastRenderMs=999,seed=99;int wakes=0;
uint32_t millis(){return nowMs;}uint32_t esp_random(){return ++seed;}
bool handleScreensaverWake(){++wakes;return false;}
struct{int changes=0;void invalidate(){++changes;}}gUiFramePolicy;
void renderLauncherOverlay(){}void renderDebugOverlay(){}
''' + (parts / '080_main.cpp.inc').read_text() + r'''
using namespace ball_catch;
void key(char k){Keyboard_Class::KeysState keys;keys.enter=k=='\n';assert(handleBallCatchKey(k,keys));}
// Line the basket up, then drop a real ball straight in.
void aimAndDrop(Game&g,int offset=0){g.bomb=false;g.basketX=g.ballX+offset*kOne;g.drop();g.driftVx=0;}
void land(Game&g){for(int i=0;i<400&&g.falling&&g.phase!=Phase::Over;++i){const int32_t b=g.basketX;g.step(0);g.basketX=b;}}
int main(){
 // The launcher's number keys reach the new group, and nothing past it.
 {using namespace launcher_shortcuts;assert(resolve('8',false,8,4).kind==Kind::Group&&resolve('8',false,8,4).index==7);
  assert(resolve('8',false,7,4).kind==Kind::None&&resolve('9',false,8,4).kind==Kind::None);
  assert(resolve('8',true,8,8).kind==Kind::Submenu&&resolve('8',true,8,4).kind==Kind::InvalidSubmenu);}
 // The ball slides and turns at both walls, and never leaves the screen.
 Game g;g.reset(0);assert(g.phase==Phase::Ready&&g.lives==kLives&&!g.falling&&g.ballY==kTrackY*kOne);
 bool left=false,right=false;
 for(int i=0;i<1000;++i){g.step(0);assert((g.ballX>>8)>=kBallRadius&&(g.ballX>>8)<=kWidth-kBallRadius&&g.ballY==kTrackY*kOne);
  left|=g.ballVx<0;right|=g.ballVx>0;}
 assert(left&&right&&g.phase==Phase::Ready);
 // Random starts land everywhere, in both directions.
 {int low=0,high=0,leftward=0;for(uint32_t s=1;s<=200;++s){Game r;r.reset(s*2654435761U);low+=(r.ballX>>8)<80;high+=(r.ballX>>8)>160;leftward+=r.ballVx<0;}
  assert(low>20&&high>20&&leftward>20&&leftward<180);}
 // A press drops it, faster and faster, still moving the way it slid, and a second press does nothing.
 g.reset(5);g.ballX=120*kOne;const int32_t x=g.ballX;g.drop();assert(g.falling&&g.phase==Phase::Playing&&g.driftVx==g.ballVx*g.speed()/kCarryDivisor);
 int32_t lastY=g.ballY,lastGap=0;for(int i=0;i<10;++i){const int32_t drift=g.driftVx;g.drop();assert(g.driftVx==drift);g.step(0);assert(g.falling);assert(g.ballY-lastY>lastGap);lastGap=g.ballY-lastY;lastY=g.ballY;}
 assert(g.ballX==x+10*g.driftVx);
 // A drifting ball bounces off the walls and stays on screen.
 g.reset(5);g.ballX=(kWidth-kBallRadius-2)*kOne;g.ballVx=1;g.drop();
 {bool bounced=false;for(int i=0;i<40&&g.falling;++i){g.step(0);bounced|=g.driftVx<0;assert((g.ballX>>8)>=kBallRadius&&(g.ballX>>8)<=kWidth-kBallRadius);}assert(bounced);}
 // Gusts start at the second level, stay within their strength, and blow both ways.
 g.reset(5);g.caught=4;assert(g.gust()==0);g.caught=5;assert(g.gust()==kGustStep);g.caught=60000;assert(g.gust()==kStrongestGust);
 {int up=0,down=0;g.caught=50;for(int i=0;i<300;++i){g.falling=false;g.phase=Phase::Playing;g.ballVx=1;g.drop();
   const int32_t blown=g.driftVx-g.speed()/kCarryDivisor;assert(std::abs(blown)<=g.gust());up+=blown>0;down+=blown<0;}assert(up>50&&down>50);}
 // Bombs: none at first, then more with each level, up to a limit, and they do come.
 g.reset(5);assert(g.bombChance()==0);g.caught=kSafeCatches-1;assert(g.bombChance()==0);g.caught=kSafeCatches;assert(g.bombChance()==kFirstBombChance);
 g.caught=5;assert(g.bombChance()==kFirstBombChance+kBombChanceStep);g.caught=60000;assert(g.bombChance()==kMostBombChance);
 {int bombs=0,balls=0;Game r;r.reset(77);r.caught=500;r.phase=Phase::Playing;
  for(int i=0;i<40000;++i){bool was=r.falling;r.lives=kLives;r.step(0);if(was&&!r.falling){++balls;bombs+=r.bomb;}}
  assert(balls>200&&bombs*100>balls*25&&bombs*100<balls*55);}
 // A bomb in the basket costs a heart and flashes, and scores nothing.
 g.reset(5);g.ballX=120*kOne;g.basketX=g.ballX;g.drop();g.driftVx=0;g.bomb=true;land(g);
 assert(g.caught==0&&g.lives==kLives-1&&(g.events&kBombed)&&g.flashSteps==kFlashSteps&&!(g.events&kMissed));
 for(int i=0;i<kFlashSteps;++i)g.step(0);assert(g.flashSteps==0);
 // A bomb that misses costs nothing, and scores nothing.
 g.reset(5);g.ballX=30*kOne;g.basketX=200*kOne;g.drop();g.driftVx=0;g.bomb=true;land(g);
 assert(g.caught==0&&g.lives==kLives&&(g.events&kDodged)&&!(g.events&kMissed));
 // A ball can't wait forever once the game is on, and the wait gets shorter, to a limit.
 g.reset(5);for(int i=0;i<1000;++i)g.step(0);assert(g.phase==Phase::Ready&&!g.falling);
 g.phase=Phase::Playing;for(int i=0;i<g.waitLimit()-1;++i)g.step(0);assert(!g.falling);g.step(0);assert(g.falling);
 g.caught=0;assert(g.waitLimit()==kLongestWait);g.caught=5;assert(g.waitLimit()==kLongestWait-kWaitStep);g.caught=60000;assert(g.waitLimit()==kShortestWait);
 g.reset(5);g.phase=Phase::Playing;g.waited=40;aimAndDrop(g);land(g);assert(g.waited==0);
 // A catch scores, keeps every heart and brings a new ball.
 g.reset(5);aimAndDrop(g);land(g);assert(g.caught==1&&g.best==1&&g.lives==kLives&&(g.events&kCaught)&&!g.falling&&g.ballY==kTrackY*kOne);
 // The basket's edge counts, and just past it doesn't.
 g.reset(5);{const int half=g.basketWidth()/2;aimAndDrop(g,half);land(g);assert(g.caught==1);
  g.reset(5);g.best=0;g.ballX=120*kOne;aimAndDrop(g,half+1);land(g);assert(g.caught==0&&g.lives==kLives-1&&(g.events&kMissed));}
 // Three misses end it, then the score shows, then a new game is ready and the best stays.
 g.reset(5);g.caught=7;g.best=7;for(int i=0;i<kLives;++i){g.bomb=false;g.ballX=30*kOne;g.basketX=200*kOne;g.drop();g.driftVx=0;land(g);}
 assert(g.phase==Phase::Over&&g.lives==0);g.events=0;
 for(int i=0;i<kOverSteps-1;++i){g.drop();g.nudge(1);g.step(100);assert(g.phase==Phase::Over&&g.caught==7&&!g.falling);}
 g.step(0);assert(g.phase==Phase::Ready&&g.caught==0&&g.lives==kLives&&g.best==7);
 // Every fifth catch is a level, and it sounds different.
 g.reset(5);for(int i=1;i<=10;++i){g.events=0;aimAndDrop(g);land(g);assert(g.caught==i&&bool(g.events&kLevelUp)==(i%kCatchesPerLevel==0));}
 // Speed climbs to triple and stops, and the basket shrinks to a limit.
 for(int caught=0;caught<=500;++caught){g.caught=uint16_t(caught);
  assert(g.speed()==std::min<int32_t>(kStartSpeed+kSpeedStep*(caught/2),kStartSpeed*3));
  assert(g.basketWidth()==std::max(kSmallestBasket,kLargestBasket-2*(caught/kCatchesPerLevel)));}
 g.caught=60000;assert(g.speed()==kStartSpeed*3&&g.basketWidth()==kSmallestBasket);
 // The basket has a dead zone and walls, at every size.
 g.reset(5);const int32_t still=g.basketX;g.step(kStickDead);g.step(-kStickDead);assert(g.basketX==still);
 g.step(kStickFull);assert(g.basketX==still+kBasketSpeed);
 for(int caught:{0,50}){g.caught=uint16_t(caught);for(int i=0;i<300;++i)g.step(127);assert(g.basketX==(kWidth-g.basketWidth()/2)*kOne);
  for(int i=0;i<300;++i)g.step(-128);assert(g.basketX==(g.basketWidth()/2)*kOne);}
 g.reset(5);g.nudge(1);for(int i=0;i<kNudgeSteps+5;++i)g.step(0);assert(g.basketX==(kWidth/2)*kOne+kNudgeSteps*kNudgeSpeed);
 // Real play stays possible: a player who looks ahead gets a long way, and one who never moves
 // the basket doesn't, however well they time the drop.
 int lowest=100000,total=0,lazy=0;
 for(uint32_t s=1;s<=40;++s){Game r;r.reset(s*2654435761U);
  for(int frame=0;frame<9000&&r.phase!=Phase::Over&&r.caught<60;++frame){
   Game look=r;if(!look.falling)look.drop();
   int landing=look.ballX>>8;
   for(int i=0;i<200&&look.falling;++i){landing=look.ballX>>8;look.step(0);}
   const int target=r.bomb?(landing<120?landing+80:landing-80):landing;
   const int gap=target-(r.basketX>>8);
   const int stick=gap>3?127:gap<-3?-127:0;
   if(!r.falling&&(r.bomb||std::abs(gap)<70))r.drop();
   r.step(stick);
   assert((r.basketX>>8)-r.basketWidth()/2>=0&&(r.basketX>>8)+r.basketWidth()/2<=kWidth);}
  lowest=std::min(lowest,int(r.best));total+=r.best;
  Game still;still.reset(s*2654435761U);
  for(int frame=0;frame<9000&&still.phase!=Phase::Over;++frame){
   const int gap=(still.ballX-still.basketX)>>8;
   if(!still.falling&&std::abs(gap)<8)still.drop();
   still.step(0);}
  lazy+=still.best;}
 assert(lowest>=20&&total/40>=40);assert(lazy/40<total/40/2);
 // Production service: a press drops once, however long it's held, and the stick moves the basket.
 beginBallCatch();assert(gBall.phase==Phase::Ready&&!gBalloonAudio.music);
 serviceBallCatch();assert(gAirSensor.opens==1&&audioRuns==1);
 nowMs+=ball_catch::kStepMs;gBalloonSensors.pressed=true;serviceBallCatch();assert(gBall.falling&&gBall.phase==Phase::Playing&&wakes>0);
 for(int i=0;i<300&&gBall.falling;++i){nowMs+=ball_catch::kStepMs;serviceBallCatch();}
 assert(!gBall.falling);
 for(int i=0;i<30;++i){nowMs+=ball_catch::kStepMs;serviceBallCatch();assert(!gBall.falling);}
 gBalloonSensors.pressed=false;nowMs+=ball_catch::kStepMs;serviceBallCatch();assert(!gBall.falling);
 gBalloonSensors.pressed=true;nowMs+=ball_catch::kStepMs;serviceBallCatch();assert(gBall.falling);gBalloonSensors.pressed=false;
 gBalloonSensors.x=127;int32_t basket=gBall.basketX;nowMs+=ball_catch::kStepMs;serviceBallCatch();assert(gBall.basketX>basket||gBall.basketX==(kWidth-gBall.basketWidth()/2)*kOne);gBalloonSensors.x=0;
 // A long stall moves at most a few steps, and the menu freezes it without a jump.
 beginBallCatch();serviceBallCatch();int32_t ball=gBall.ballX;nowMs+=5000;serviceBallCatch();
 assert(std::abs(gBall.ballX-ball)<=kBallMostSteps*gBall.speed()&&gBallStepMs==nowMs);
 gLauncherVisible=true;ball=gBall.ballX;nowMs+=5000;serviceBallCatch();assert(gBall.ballX==ball);
 gLauncherVisible=false;nowMs+=1;serviceBallCatch();assert(gBall.ballX==ball);
 // With no joystick it says so, and the keyboard plays the whole game.
 gBalloonSensors.here=false;beginBallCatch();serviceBallCatch();nowMs+=ball_catch::kStepMs;serviceBallCatch();
 assert(gBallStick.unit==air_music::Unit::Missing);renderBallCatchUi();assert(gCanvas.text.back()=="Space drops, A and D move");
 key('a');assert(gBall.nudgeDir==-1);key(',');key('d');assert(gBall.nudgeDir==1);key('/');
 for(char k:{' ','\n','s','.'}){beginBallCatch();key('z');assert(!gBall.falling);key(k);assert(gBall.falling);}
 {beginBallCatch();Keyboard_Class::KeysState ctrl;ctrl.ctrl=true;assert(handleBallCatchKey(' ',ctrl));assert(!gBall.falling);}
 gBalloonSensors.here=true;
 // Each event has its own sound, and the last miss is the worst.
 gBalloonAudio.reset();gBall.lives=1;gBall.events=kMissed|kCaught;ballSounds();const uint32_t buzz=gBalloonAudio.buzzLeft;assert(buzz>0&&gBall.events==0&&gBalloonAudio.chimeWait==0);
 gBalloonAudio.reset();gBall.lives=0;gBall.events=kMissed;ballSounds();assert(gBalloonAudio.buzzLeft>buzz*2);
 gBalloonAudio.reset();gBall.lives=1;gBall.events=kBombed;ballSounds();assert(gBalloonAudio.buzzLeft>buzz&&gBalloonAudio.buzzer.target==balloon_audio::kCrashLevel);
 gBalloonAudio.reset();gBall.lives=0;gBall.events=kBombed;ballSounds();assert(gBalloonAudio.buzzLeft>buzz*2);gBall.lives=1;
 gBalloonAudio.reset();gBall.events=kDodged;ballSounds();assert(gBalloonAudio.chimeHz==523);
 gBalloonAudio.reset();gBall.events=kLevelUp;ballSounds();assert(gBalloonAudio.chimeHz==1568);
 gBalloonAudio.reset();gBall.events=kCaught;ballSounds();assert(gBalloonAudio.chimeHz==1319);
 // Leaving frees the port and stops the sound.
 const int closes=gAirSensor.closes,stops=M5Cardputer.Speaker.stops;stopBallCatch();assert(gAirSensor.closes==closes+1&&M5Cardputer.Speaker.stops==stops+1);
 gUiMode=UiMode::Home;const int opens=gAirSensor.opens;serviceBallCatch();assert(gAirSensor.opens==opens);gUiMode=UiMode::BallCatch;
 // The renderer stays on screen in every phase, with the ball and basket anywhere.
 for(int phase=0;phase<3;++phase)for(int caught:{0,4,5,7,50,999})for(int lives=0;lives<=kLives;++lives)for(int i=0;i<40;++i){
  gBall.reset(3);gBall.phase=Phase(phase);gBall.caught=uint16_t(caught);gBall.best=9999;gBall.lives=uint8_t(lives);
  gBall.ballX=(kBallRadius+i*(kBallRightX-kBallRadius)/39)*kOne;gBall.ballY=(kTrackY+(i%2)*80)*kOne;
  gBall.bomb=i%3==0;gBall.flashSteps=uint8_t(i%2);gBall.falling=i%2;gBall.waited=uint16_t(i*4);
  const int half=gBall.basketWidth()/2;gBall.basketX=(i%2?half:kWidth-half)*kOne;renderBallCatchUi();}
 std::cout<<"PASS Ball Catch: sliding, drifting, bombs, gusts, the rail timer, edges, hearts, levels and limits, a careful player averaged "<<total/40<<" catches (worst "<<lowest<<") and a still one "<<lazy/40<<", timing, menu freeze, no joystick, sounds, port handover, renderer bounds\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-ball-catch-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
