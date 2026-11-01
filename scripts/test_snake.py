#!/usr/bin/env python3
"""Exercise production Snake rules, controls, pausing and renderer on host."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
src=root/'bots/CardputerADV/src'
parts=src/'main_parts'
app=(parts/'074_main.cpp.inc').read_text()
for file,text in [
    ('001_main.cpp.inc','#include "snake.h"'),
    ('004_main.cpp.inc','{"Snake", UiMode::Snake}'),
    ('004_main.cpp.inc','{"Pocket Synth", UiMode::PocketSynth}'),
    ('022_main.cpp.inc','mode == "snake"'),
    ('024_main.cpp.inc','mode == UiMode::Snake) beginSnake();'),
    ('048_main.cpp.inc','handleSnakeKey(key, keys)'),
    ('049_main.cpp.inc','gUiMode == UiMode::Snake ||'),
    ('061_main.cpp.inc','renderSnakeUi();'),
    ('061_main.cpp.inc','serviceSnake();'),
    ('063_main.cpp.inc','case UiMode::Snake:')]:
    assert text in (parts/file).read_text(),(file,text)
assert 'main_parts/074_main.cpp.inc' in (src/'main.cpp').read_text()
code=r'''
#include "snake.h"
#include <cassert>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>
#include <set>
#include <iostream>
struct String:std::string {
 using std::string::string;String(std::string s):std::string(s){}
 template<class T,typename=std::enable_if_t<std::is_integral_v<T>>>String(T n):std::string(std::to_string(n)){}
};
namespace fonts{const int Font0=0,Font2=2;}
constexpr uint16_t TFT_WHITE=0xffff;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
struct Rect{int x,y,w,h;};
struct Canvas{
 const int*font=&fonts::Font0;int x=0,y=0;std::vector<Rect>text;
 void bounds(int xx,int yy,int w,int h){assert(xx>=0&&yy>=0&&w>0&&h>0&&xx+w<=240&&yy+h<=135);}
 void fillScreen(uint16_t){text.clear();}
 void fillRect(int xx,int yy,int w,int h,uint16_t){bounds(xx,yy,w,h);if(w<=5&&h<=5)assert(xx+w<=127&&yy+h<=121);}
 void drawRect(int xx,int yy,int w,int h,uint16_t c){fillRect(xx,yy,w,h,c);}
 void fillCircle(int xx,int yy,int r,uint16_t){bounds(xx-r,yy-r,2*r+1,2*r+1);assert(xx+r<127&&yy+r<121);}
 void setFont(const int*f){font=f;}void setTextColor(uint16_t,uint16_t){}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 int textWidth(const String&s){return int(s.size())*(font==&fonts::Font0?6:10);}
 void print(const String&s){Rect r{x,y,textWidth(s),font==&fonts::Font0?8:16};bounds(x,y,r.w,r.h);assert(y+r.h<=129&&x+r.w<=234&&x>=127);
  for(auto p:text)assert(x+r.w<=p.x||p.x+p.w<=x||y+r.h<=p.y||p.y+p.h<=y);text.push_back(r);}
}gCanvas;
struct Keyboard_Class{struct KeysState{bool ctrl=false,alt=false,opt=false,del=false,enter=false;};};
enum class UiMode{Home,Snake};UiMode gUiMode=UiMode::Snake;
bool gLauncherVisible=false;
uint32_t nowMs=1000,gLastRenderMs=999,seed=12345;
uint32_t millis(){return nowMs;}uint32_t esp_random(){return ++seed;}
struct{int changes=0;void invalidate(){++changes;}}gUiFramePolicy;
void renderLauncherOverlay(){}void renderDebugOverlay(){}
''' + app + r'''
using namespace snake;
void key(char k){Keyboard_Class::KeysState keys;keys.enter=k=='\n';assert(handleSnakeKey(k,keys));}
// Every cell the ring buffer names must be marked, and nothing else.
void consistent(const Game&g){
 std::set<int> cells;for(int i=0;i<g.length;++i)cells.insert(g.cellOf(i));
 assert(int(cells.size())==g.length);
 int marked=0;for(int c=0;c<kCells;++c)if(g.occupied[c]){++marked;assert(cells.count(c));}
 assert(marked==g.length);
 if(g.phase!=Phase::Won)assert(!g.occupied[g.food]&&g.food<kCells);
}
// Lay a snake along the given cells, tail first.
void shape(Game&g,std::vector<int> cells,Dir heading,int food){
 g.reset(7);for(auto&c:g.occupied)c=false;g.tail=0;g.length=0;
 for(int c:cells){g.body[g.length++]=uint16_t(c);g.occupied[c]=true;}
 g.heading=heading;g.food=uint16_t(food);g.play(0);
}
int main(){
 Game g;g.reset(1);consistent(g);
 assert(g.phase==Phase::Ready&&g.length==kStartLength&&g.head()==cellAt(kCols/2,kRows/2));
 // Steps do nothing until play begins.
 const int start=g.head();g.step();assert(g.head()==start);
 assert(!g.advance(100000));
 g.play(0);g.step();assert(g.head()==start+1&&g.length==kStartLength);consistent(g);
 // Reversals and repeats are refused, including against a queued turn.
 assert(!g.turn(Dir::Left));assert(!g.turn(Dir::Right));
 assert(g.turn(Dir::Up));assert(!g.turn(Dir::Down));assert(!g.turn(Dir::Up));
 assert(g.turn(Dir::Left));assert(!g.turn(Dir::Down));g.step();assert(g.heading==Dir::Up);g.step();assert(g.heading==Dir::Left);
 consistent(g);
 // Every wall ends the game without moving the snake off the board.
 for(int wall=0;wall<4;++wall){
  const int cells[4]={cellAt(5,0),cellAt(kCols-1,5),cellAt(5,kRows-1),cellAt(0,5)};
  shape(g,{cells[wall]},Dir(wall),cellAt(9,9));
  const int head=g.head();g.step();assert(g.phase==Phase::Lost&&g.head()==head&&g.length==1);
 }
 // Biting the body loses; stepping into the tail that is leaving does not.
 shape(g,{cellAt(5,5),cellAt(6,5),cellAt(7,5),cellAt(7,6),cellAt(6,6),cellAt(5,6)},Dir::Up,cellAt(9,9));
 g.step();assert(g.phase==Phase::Playing&&g.head()==cellAt(5,5)&&g.length==6);consistent(g);
 shape(g,{cellAt(4,5),cellAt(5,5),cellAt(6,5),cellAt(7,5),cellAt(7,6),cellAt(6,6),cellAt(5,6)},Dir::Up,cellAt(9,9));
 g.step();assert(g.phase==Phase::Lost&&g.length==7);consistent(g);
 // Food grows the snake and speeds it up, down to the limit.
 shape(g,{cellAt(1,1),cellAt(2,1)},Dir::Right,cellAt(3,1));
 assert(g.stepMs()==kStartStepMs);g.step();
 assert(g.length==3&&g.score==1&&g.best==1&&g.stepMs()==kStartStepMs-kStepGainMs);consistent(g);
 g.score=1000;assert(g.stepMs()==kFastestStepMs);g.score=0;
 assert(!g.advance(g.lastStepMs+kStartStepMs-1));assert(g.advance(g.lastStepMs+kStartStepMs));
 // Best survives a new game.
 g.best=9;g.reset(3);assert(g.best==9&&g.score==0);
 // Filling the last cell wins and needs no more food.
 {std::vector<int> cells;for(int y=0;y<kRows;++y)for(int x=0;x<kCols;++x)cells.push_back(cellAt(y%2?kCols-1-x:x,y));
  const int last=cells.back();cells.pop_back();
  shape(g,cells,Dir::Left,last);g.step();assert(g.phase==Phase::Won&&g.length==kCells);consistent(g);}
 // Food never lands on the snake, even with one free cell left.
 for(int run=0;run<2000;++run){shape(g,{},Dir::Right,0);int freeCell=run%kCells;
  for(int c=0;c<kCells;++c)if(c!=freeCell){g.body[g.length++]=uint16_t(c);g.occupied[c]=true;}
  g.placeFood();assert(g.food==freeCell);}
 // Production controls.
 gSnake=Game{};beginSnake();assert(gSnake.phase==Phase::Ready);
 key('\n');assert(gSnake.phase==Phase::Playing);
 key(' ');assert(gSnake.phase==Phase::Paused);
 key('w');assert(gSnake.phase==Phase::Playing&&gSnake.queuedCount==1&&gSnake.queued[0]==Dir::Up);
 for(char k:{';','.',',','/'}){newSnakeGame();key(k);assert(gSnake.phase==Phase::Playing);}
 newSnakeGame();key('n');assert(gSnake.phase==Phase::Ready);
 Keyboard_Class::KeysState ctrl;ctrl.ctrl=true;assert(handleSnakeKey('w',ctrl));assert(gSnake.phase==Phase::Ready);
 // Timing only moves it while the game is on screen, and each step asks for a frame.
 key('\n');int head=gSnake.head(),changes=gUiFramePolicy.changes;
 serviceSnake();assert(gSnake.head()==head&&gUiFramePolicy.changes==changes);
 nowMs+=kStartStepMs;serviceSnake();assert(gSnake.head()==head+1&&gUiFramePolicy.changes==changes+1);
 gLauncherVisible=true;nowMs+=5000;serviceSnake();assert(gSnake.phase==Phase::Paused);gLauncherVisible=false;
 head=gSnake.head();serviceSnake();assert(gSnake.head()==head);
 key('\n');gUiMode=UiMode::Home;nowMs+=5000;serviceSnake();assert(gSnake.phase==Phase::Paused);gUiMode=UiMode::Snake;
 head=gSnake.head();key('\n');serviceSnake();assert(gSnake.head()==head&&gSnake.phase==Phase::Playing);
 // A finished game starts over on Enter, and steering can't revive it.
 gSnake.phase=Phase::Lost;key('w');assert(gSnake.phase==Phase::Lost);key('\n');assert(gSnake.phase==Phase::Ready&&gSnake.score==0);
 // Renderer stays in bounds in every state, at every food cell, at any size.
 for(int phase=0;phase<5;++phase){gSnake.reset(9);gSnake.phase=Phase(phase);gSnake.best=999;gSnake.score=357;
  for(int c=0;c<kCells;++c){gSnake.food=uint16_t(c);renderSnakeUi();}}
 {std::vector<int> cells;for(int c=0;c<kCells;++c)cells.push_back(c);shape(gSnake,cells,Dir::Right,0);gSnake.phase=Phase::Won;renderSnakeUi();}
 // Random play always keeps the board consistent and ends properly.
 for(int run=0;run<300;++run){Game r;r.reset(uint32_t(run)*2654435761U+1);r.play(0);
  for(int i=0;i<4000&&r.phase==Phase::Playing;++i){r.turn(Dir(r.random()%4));
   if(r.random()%3==0){const int h=r.head(),f=r.food;const int dx=f%kCols-h%kCols,dy=f/kCols-h/kCols;
    r.turn(dx>0?Dir::Right:dx<0?Dir::Left:dy>0?Dir::Down:Dir::Up);}
   r.step();consistent(r);}
 }
 std::cout<<"PASS Snake: walls, biting, tail chase, food, speed, win, input queue, production controls, pause on leave, renderer bounds and 300 random games\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-snake-') as d:
    p=Path(d);(p/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(src),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
