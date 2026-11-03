#!/usr/bin/env python3
"""Exercise the Lab's hub protocol, GPS reader, rule engine, saving and output limits on host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
for file, text in [
        ('001_main.cpp.inc', '#include "lab.h"'),
        ('001_main.cpp.inc', '#include "lab_gps.h"'),
        ('001_main.cpp.inc', 'kLauncherGroupCount = 9'),
        ('001_main.cpp.inc', '#include "lab_json.h"'),
        ('023_main.cpp.inc', 'lab::ruleFromJson(action, rule, labSequences(), labSequenceCount())'),
        ('023_main.cpp.inc', 'labOnceStart(lab::onceFromJson(action, once) ? &once : nullptr);'),
        ('024_main.cpp.inc', 'if (modeChanged) labOnceLeaving(mode);'),
        ('061_main.cpp.inc', 'serviceLabOnce();'),
        ('063_main.cpp.inc', 'return labAllowsNetwork();'),
        ('004_main.cpp.inc', '{"Devices", UiMode::LabDevices}'),
        ('004_main.cpp.inc', '{"Rules", UiMode::LabRules}'),
        ('004_main.cpp.inc', '{"Sequences", UiMode::LabSequences}'),
        ('022_main.cpp.inc', 'mode == "lab"'),
        ('024_main.cpp.inc', 'if (modeChanged && labMode(mode)) beginLab();'),
        ('024_main.cpp.inc', 'if (modeChanged && labMode(gUiMode)) stopLab();'),
        ('043_main.cpp.inc', 'cmd == "/lab"'),
        ('043_main.cpp.inc', 'cmd == "/rules"'),
        ('043_main.cpp.inc', 'cmd == "/sequences"'),
        ('048_main.cpp.inc', 'handleLabKey(key, keys)'),
        ('048_main.cpp.inc', 'routeLabEscape()'),
        ('049_main.cpp.inc', 'routeLabEscape()'),
        ('061_main.cpp.inc', 'renderLabUi();'),
        ('061_main.cpp.inc', 'serviceLab();'),
        ('063_main.cpp.inc', 'case UiMode::LabRules:'),
        ('063_main.cpp.inc', 'case UiMode::LabSequences:')]:
    assert text in (parts / file).read_text(), (file, text)
for part in ('081', '082', '084', '085'):
    assert f'main_parts/{part}_main.cpp.inc' in (src / 'main.cpp').read_text()

# The Grove port changes hands the same way it does for the games.
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceLab();')
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('stopLab();') < change.index('gUiMode = mode;')
# A check gives the port back before anything that could want it starts or stops.
assert change.index('labOnceLeaving(mode);') < change.index('stopAirMusic();') < change.index('beginAirMusic();')
# Ahead of every early return in the loop, so a check can always time out and give the port back.
assert loop.index('serviceLabOnce();') < loop.index('if (gRealtimeExclusiveMode)') < loop.index('if (gUiMode == UiMode::Nes)')

code = r'''
#include "lab.h"
#include "lab_gps.h"
#include "launcher_shortcuts.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace lab;

struct Sent { uint8_t address; std::vector<uint8_t> bytes; };
struct FakeBus {
  std::vector<Sent> sent; std::map<uint8_t,std::vector<uint8_t>> replies; std::vector<uint8_t> asked; bool here=true;
  bool send(uint8_t a,const uint8_t*b,uint8_t n){if(!here)return false;sent.push_back({a,std::vector<uint8_t>(b,b+n)});return true;}
  bool fetch(uint8_t a,uint8_t reg,uint8_t*out,uint8_t n){if(!here)return false;assert(a==pbhub::kAddress);asked.push_back(reg);
    auto& r=replies[reg];assert(r.size()==n);std::memcpy(out,r.data(),n);return true;}
  std::vector<uint8_t> last()const{return sent.back().bytes;}
};
std::vector<uint8_t> B(std::initializer_list<int> v){std::vector<uint8_t> o;for(int x:v)o.push_back(uint8_t(x));return o;}

void testHub(){
  // The sixth port really does skip 0x90.
  const uint8_t bases[6]={0x40,0x50,0x60,0x70,0x80,0xA0};
  for(int i=0;i<6;++i)assert(pbhub::channel(uint8_t(i))==bases[i]);
  FakeBus bus;pbhub::Hub<FakeBus> hub(bus);
  assert(hub.digitalWrite(0,0,true)&&bus.last()==B({0x40,1}));
  assert(hub.digitalWrite(3,1,false)&&bus.last()==B({0x71,0}));
  assert(hub.servo(4,1,90)&&bus.last()==B({0x8D,90}));
  // Past 180 the hub drops the command, which would leave the servo stuck, so it's clamped here.
  assert(hub.servo(5,0,200)&&bus.last()==B({0xAC,180}));
  assert(hub.ledCount(5,30)&&bus.last()==B({0xA8,30,0}));
  assert(hub.oneLed(2,300,1,2,3)&&bus.last()==B({0x69,0x2C,0x01,1,2,3}));
  assert(hub.fillLeds(1,2,28,9,8,7)&&bus.last()==B({0x5A,2,0,28,0,9,8,7}));
  assert(hub.showMode(1)&&bus.last()==B({0xFA,1}));
  assert(hub.showMode(0)&&bus.last()==B({0xFA,0}));
  bus.replies[0x46]={0x34,0x0A};uint16_t raw=0;assert(hub.analogRead(0,raw)&&raw==0x0A34);
  bus.replies[0xA6]={0xFF,0x0F};assert(hub.analogRead(5,raw)&&raw==4095);
  bus.replies[0x54]={1};bool high=false;assert(hub.digitalRead(1,0,high)&&high);
  bus.replies[0x55]={0};assert(hub.digitalRead(1,1,high)&&!high);
  bus.replies[0xFE]={2};uint8_t version=0;assert(hub.firmware(version)&&version==2);
  bus.here=false;assert(!hub.digitalWrite(0,0,true)&&!hub.analogRead(0,raw)&&!hub.digitalRead(0,0,high)&&!hub.firmware(version));
}

void line(lab_gps::Parser& p,const char* body,uint32_t now=1000){
  uint8_t sum=0;for(const char* c=body;*c;++c)sum^=uint8_t(*c);
  char text[120];std::snprintf(text,sizeof(text),"$%s*%02X\r\n",body,sum);
  for(const char* c=text;*c;++c)p.feed(*c,now);
}
void testGps(){
  lab_gps::Parser p;assert(!p.hearing(0));
  line(p,"GNGGA,123519,4230.5000,N,07054.0000,W,1,08,0.9,180.0,M,0,M,,");
  assert(p.lines==1&&p.fix.satellites==8&&!p.fix.valid);
  line(p,"GNRMC,123519,A,4230.5000,N,07054.0000,W,10.0,084.4,230394,,,A");
  assert(p.fix.valid&&p.fix.latE5==4250833&&p.fix.lonE5==-7090000&&p.fix.speedKmhE1==185);
  assert(p.hearing(1000)&&p.hearing(3999)&&!p.hearing(4000));
  // Other satellite systems' names work too, and the south and east signs.
  line(p,"GPRMC,1,A,0130.0000,S,10030.0000,E,0,0,1,,,A");
  assert(p.fix.latE5==-150000&&p.fix.lonE5==10050000&&p.fix.speedKmhE1==0);
  // No fix clears the position, because an old one would look current.
  line(p,"GNRMC,1,V,,,,,,,1,,,N");assert(!p.fix.valid);
  // A damaged line changes nothing.
  lab_gps::Parser q;
  for(const char* c="$GNGGA,1,4230.5,N,07054.0,W,1,08,0.9,1,M,0,M,,*00\r\n";*c;++c)q.feed(*c,5);
  assert(q.lines==0&&q.fix.satellites==0);
  // Nonsense, overlong lines and cut-off fields can't break it.
  std::string longLine="$";longLine.append(300,'A');longLine+="\r\n";
  for(char c:longLine)q.feed(c,5);
  for(const char* text:{"$\r\n","$*\r\n","$GNRMC*\r\n","$A*41\r\n","$,,,,*2C\r\n","garbage\r\n"})for(const char* c=text;*c;++c)q.feed(*c,5);
  line(q,"GNRMC");line(q,"GNGGA");line(q,"GNRMC,1,A");line(q,"GNRMC,1,A,,,,,0");assert(!q.fix.valid);
  int32_t out=0;assert(!lab_gps::parseCoordinate("",'N',out)&&!lab_gps::parseCoordinate("12",'N',out)&&
                       !lab_gps::parseCoordinate("1234.5",'Q',out)&&!lab_gps::parseCoordinate("9999.5",'N',out)&&
                       !lab_gps::parseCoordinate("12x4.5",'N',out));
  // A line can arrive split across many passes of the main loop.
  lab_gps::Parser s;const char* body="$GNGGA,1,,,,,1,12,,,,,,,*XX";(void)body;
  char text[80];uint8_t sum=0;const char* inner="GNGGA,1,,,,,1,12,,,,,,,";for(const char* c=inner;*c;++c)sum^=uint8_t(*c);
  std::snprintf(text,sizeof(text),"$%s*%02X\n",inner,sum);for(const char* c=text;*c;++c)s.feed(*c,7);
  assert(s.fix.satellites==12);
}

void testRanges(){
  assert(mapRange(0,0,100,0,180)==0&&mapRange(100,0,100,0,180)==180&&mapRange(50,0,100,0,180)==90);
  assert(mapRange(-5,0,100,0,180)==0&&mapRange(500,0,100,0,180)==180);
  assert(mapRange(25,0,100,180,0)==135);
  assert(mapRange(5,5,5,1,9)==1);
  // The range it comes from can run backwards too.
  assert(mapRange(100,100,30,0,100)==0&&mapRange(30,100,30,0,100)==100&&mapRange(65,100,30,0,100)==50);
  assert(mapRange(500,100,30,0,100)==0&&mapRange(1,100,30,0,100)==100);
  // Negative inputs and backwards outputs round the same way.
  assert(mapRange(-100,-100,100,0,180)==0&&mapRange(100,-100,100,0,180)==180&&mapRange(0,-90,90,0,100)==50);
  for(int v=-200;v<=300;++v){const int out=mapRange(v,-100,100,0,359);assert(out>=0&&out<=359);}
  AnalogRange range;assert(range.percent(0)==0&&range.percent(256)==50&&range.top==512);
  assert(range.percent(3000)==100&&range.top==3000&&range.percent(1500)==50&&range.percent(3000)==100);
  for(uint32_t raw:{0u,1u,511u,512u,4095u,65535u}){AnalogRange r;assert(r.percent(uint16_t(raw))<=100);}
  // The Mini Angle runs backwards: full voltage is its left end, so the number goes up clockwise.
  AnalogRange knob;knob.top=2800;assert(knobPercent(knob,2800)==0&&knobPercent(knob,0)==100&&knobPercent(knob,1400)==50&&knobPercent(knob,700)==75);
  assert(knobPercent(knob,3100)==0&&knob.top==3100);
  for(uint32_t raw:{0u,1u,2799u,4095u,65535u}){AnalogRange r;const int v=knobPercent(r,uint16_t(raw));assert(v>=0&&v<=100);}
}

void testInputs(){
  Inputs in;assert(!in.get(Sig::Knob).live);
  // The first reading isn't a change, or plugging a sensor in would fire its rules.
  in.set(Sig::Knob,50,100);assert(in.get(Sig::Knob).live&&!in.get(Sig::Knob).moved);
  in.set(Sig::Knob,51,110);assert(!in.get(Sig::Knob).moved&&in.get(Sig::Knob).value==51);
  in.set(Sig::Knob,52,120);assert(in.get(Sig::Knob).moved&&in.get(Sig::Knob).changedMs==120);
  // Slow drift still counts, because it's measured from the last change.
  in.set(Sig::Knob,53,130);assert(in.get(Sig::Knob).changedMs==120);
  in.set(Sig::Knob,54,140);assert(in.get(Sig::Knob).changedMs==140);
  in.set(Sig::Knob,50,150);assert(in.get(Sig::Knob).changedMs==150);
  // Going away and coming back isn't a change either.
  in.lose(Sig::Knob);assert(!in.get(Sig::Knob).live&&!in.get(Sig::Knob).moved);
  in.set(Sig::Knob,90,200);assert(!in.get(Sig::Knob).moved);
}

Rule rule(const char* name){Rule r;std::strcpy(r.name,name);return r;}
void testRules(){
  Inputs in;Drive drive[kOutCount];RuleState st[kMaxRules];
  const auto at=[&](Out o){return drive[uint8_t(o)];};
  // The owner's example: knob turning or something close, then strip color and servo.
  Rule w=starterRule();assert(std::strcmp(w.name,"Wave lights")==0&&w.any&&w.conds==2&&w.actions==2);
  in.set(Sig::Knob,40,1000);in.set(Sig::Distance,80,1000);
  step(&w,1,st,in,1010,drive);assert(!st[0].active&&!at(Out::Servo).on&&!at(Out::StripColor).on);
  // A hand comes close: it fires once, and both outputs follow.
  in.set(Sig::Distance,20,1100);step(&w,1,st,in,1100,drive);
  assert(st[0].active&&st[0].fires==1&&st[0].firedMs==1100);
  assert(at(Out::Servo).on&&at(Out::Servo).value==72);          // Knob 40% of 180.
  assert(at(Out::StripColor).on&&at(Out::StripColor).value==66); // Distance 20 in 2 to 100.
  // Staying close isn't a second firing.
  in.set(Sig::Distance,18,1200);step(&w,1,st,in,1200,drive);assert(st[0].fires==1);
  // It keeps going for the hold after the hand leaves, then stops.
  in.set(Sig::Distance,80,1300);step(&w,1,st,in,1300,drive);
  step(&w,1,st,in,1200+w.holdMs-1,drive);assert(st[0].active&&at(Out::Servo).on);
  step(&w,1,st,in,1200+w.holdMs,drive);assert(!st[0].active&&!at(Out::Servo).on&&!at(Out::StripColor).on);
  // Now the other way in: turning the knob, and it lasts as long as "just changed" plus the hold.
  in.set(Sig::Knob,60,5000);step(&w,1,st,in,5000,drive);assert(st[0].active&&st[0].fires==2&&at(Out::Servo).value==108);
  step(&w,1,st,in,5000+kChangedMs-1,drive);assert(st[0].heldMs==5000+kChangedMs-1);
  step(&w,1,st,in,5000+kChangedMs,drive);assert(st[0].heldMs==5000+kChangedMs-1);
  // ALL needs every one, and ANY only one.
  Rule a=rule("a");a.any=false;a.conds=2;a.cond[0]={Sig::Humidity,Op::Above,60};a.cond[1]={Sig::Light,Op::Below,20};
  a.actions=1;a.action[0].out=Out::Relay1;a.action[0].how=How::Set;a.action[0].value=1;a.holdMs=0;
  Inputs b;RuleState s2[kMaxRules];b.set(Sig::Humidity,70,1);b.set(Sig::Light,50,1);
  step(&a,1,s2,b,2,drive);assert(!s2[0].active);
  b.set(Sig::Light,10,3);step(&a,1,s2,b,3,drive);assert(s2[0].active&&at(Out::Relay1).on&&at(Out::Relay1).value==1);
  // With no hold it stops the moment it stops being true.
  b.set(Sig::Light,50,4);step(&a,1,s2,b,4,drive);assert(!s2[0].active&&!at(Out::Relay1).on);
  // A missing sensor makes a condition false, not an accident.
  Inputs none;Cond below{Sig::Distance,Op::Below,30};assert(!condHolds(below,none,0));
  Rule noConds=rule("n");assert(!ruleHolds(noConds,none,0));
  // Both on/off directions.
  Cond on{Sig::Motion,Op::Below,0},off{Sig::Motion,Op::Below,0};setOnOff(on,true);setOnOff(off,false);
  Inputs m;m.set(Sig::Motion,1,1);assert(condHolds(on,m,1)&&!condHolds(off,m,1));
  m.set(Sig::Motion,0,2);assert(!condHolds(on,m,2)&&condHolds(off,m,2));
  // A follow with no reading leaves the output alone, and the rule's other action still runs.
  Rule f=rule("f");f.conds=1;f.cond[0]={Sig::Motion,Op::Above,0};f.actions=2;
  f.action[0].out=Out::Servo;f.action[0].how=How::Follow;f.action[0].source=Sig::Knob;
  f.action[1].out=Out::Led;f.action[1].how=How::Set;f.action[1].value=120;
  Inputs n;RuleState s3[kMaxRules];n.set(Sig::Motion,0,1);n.set(Sig::Motion,1,2);
  step(&f,1,s3,n,3,drive);assert(s3[0].active&&!at(Out::Servo).on&&at(Out::Led).on);
  // The opposite direction.
  Action back;back.out=Out::Servo;back.how=How::FollowBack;back.source=Sig::Knob;int16_t v=0;
  Inputs k;k.set(Sig::Knob,25,1);assert(actionValue(back,k,v)&&v==135);
  // The later rule wins an output, and a rule that's off does nothing.
  Rule two[2]={rule("x"),rule("y")};
  for(Rule& r:two){r.conds=1;r.cond[0]={Sig::Motion,Op::Above,0};r.actions=1;r.action[0].out=Out::Servo;r.action[0].how=How::Set;}
  two[0].action[0].value=10;two[1].action[0].value=170;
  Inputs t;RuleState s4[kMaxRules];t.set(Sig::Motion,0,1);t.set(Sig::Motion,1,2);
  step(two,2,s4,t,3,drive);assert(at(Out::Servo).value==170);
  two[1].on=false;step(two,2,s4,t,4,drive);assert(at(Out::Servo).value==10&&!s4[1].active);
  two[0].on=false;step(two,2,s4,t,5,drive);assert(!at(Out::Servo).on&&!s4[0].active);
  // Time running past the 32-bit wrap doesn't stick a rule on.
  Rule h=rule("h");h.conds=1;h.cond[0]={Sig::Motion,Op::Above,0};h.holdMs=1000;
  Inputs u;RuleState s5[kMaxRules];u.set(Sig::Motion,0,1);u.set(Sig::Motion,1,2);
  const uint32_t nearWrap=0xFFFFFF00U;step(&h,1,s5,u,nearWrap,drive);assert(s5[0].active);
  u.set(Sig::Motion,0,nearWrap+1);step(&h,1,s5,u,nearWrap+999,drive);assert(s5[0].active);
  step(&h,1,s5,u,nearWrap+1000,drive);assert(!s5[0].active);
}

void testWiring(){
  Wiring w;assert(w.portOf(Part::Knob)==0&&w.portOf(Part::Strip)==5);
  for(int i=1;i<kPartCount;++i)assert(w.portOf(Part(i))>=0);
  // With every part placed, a port can only go empty and come back. Other rows never change.
  w.cycle(0,1);assert(w.port[0]==Part::None&&w.portOf(Part::Knob)==-1&&w.port[1]==Part::Light);
  w.cycle(0,1);assert(w.port[0]==Part::Knob);
  // Swapping two: empty one, then each takes the other's part.
  w.cycle(0,-1);assert(w.port[0]==Part::None);w.cycle(5,1);assert(w.port[5]==Part::None);w.cycle(5,1);assert(w.port[5]==Part::Knob);w.cycle(0,-1);assert(w.port[0]==Part::Strip);
  // Moving a part straight to a port swaps it, and is the same as doing nothing onto itself.
  Wiring m;m.moveTo(3,Part::Knob);assert(m.port[3]==Part::Knob&&m.port[0]==Part::Relay&&m.port[1]==Part::Light);
  m.moveTo(3,Part::Knob);assert(m.port[3]==Part::Knob&&m.port[0]==Part::Relay);
  m.moveTo(2,Part::None);assert(m.port[2]==Part::None&&m.portOf(Part::Motion)==-1);
  m.moveTo(5,Part::Motion);assert(m.port[5]==Part::Motion&&m.portOf(Part::Strip)==-1);
  Wiring a;
  // Cycling one port forever can't lose or move anything on the others.
  for(int round=0;round<500;++round){const Part before=a.port[1];a.cycle(0,round%3?1:-1);assert(a.port[1]==before);}
  // And every port every way never duplicates a part, whatever the order.
  for(int round=0;round<5000;++round){a.cycle(uint8_t(round*7%6),round%3?1:-1);
    bool seen[kPartCount]={};for(Part p:a.port){if(p!=Part::None){assert(!seen[int(p)]);seen[int(p)]=true;}}}
}

void testCloseness(){
  // Full means it's about to fire, from either side.
  Cond under{Sig::Distance,Op::Below,30};assert(closeness(under,100)==0&&closeness(under,30)==100&&closeness(under,65)==50);
  assert(closeness(under,500)==0&&closeness(under,5)==100);
  Cond over{Sig::Humidity,Op::Above,60};assert(closeness(over,0)==0&&closeness(over,30)==50&&closeness(over,60)==100&&closeness(over,90)==100);
  Cond floor{Sig::Humidity,Op::Below,0};assert(closeness(floor,50)>=0&&closeness(floor,50)<=100);
  Cond top{Sig::Humidity,Op::Above,100};assert(closeness(top,50)==50);
  Cond change{Sig::Knob,Op::Changes,0};assert(closeness(change,50)==0);
}

void testStrip(){
  Drive d[kOutCount];
  assert(!stripFrame(d,25).on);
  d[uint8_t(Out::StripColor)]={true,0};
  StripFrame f=stripFrame(d,25);assert(f.on&&f.lit==kStripLeds&&f.r==63&&f.g==0&&f.b==0);
  // The cap holds whatever a rule asks for.
  d[uint8_t(Out::StripBright)]={true,100};assert(stripFrame(d,25).r==63);
  d[uint8_t(Out::StripBright)]={true,500};assert(stripFrame(d,25).r==63);
  assert(stripFrame(d,100).r==255);
  d[uint8_t(Out::StripBright)]={true,50};assert(stripFrame(d,100).r==127);
  d[uint8_t(Out::StripBright)]={true,0};assert(!stripFrame(d,100).on);
  d[uint8_t(Out::StripBright)]={};
  d[uint8_t(Out::StripLength)]={true,12};assert(stripFrame(d,25).lit==12);
  d[uint8_t(Out::StripLength)]={true,99};assert(stripFrame(d,25).lit==kStripLeds);
  d[uint8_t(Out::StripLength)]={true,0};assert(!stripFrame(d,25).on);
  // Length alone still lights it, in the resting color.
  Drive e[kOutCount];e[uint8_t(Out::StripLength)]={true,5};f=stripFrame(e,100);assert(f.on&&f.lit==5&&(f.r||f.g||f.b));
  for(int hue=-720;hue<=1080;++hue){uint8_t r,g,b;hueToRgb(hue,r,g,b);assert(r==255||g==255||b==255);}
  uint8_t r,g,b;hueToRgb(120,r,g,b);assert(r==0&&g==255&&b==0);hueToRgb(240,r,g,b);assert(r==0&&g==0&&b==255);
}

void testRelays(){
  RelayGuard g;
  // The first command always goes out, because the relay's state isn't known.
  assert(g.allow(false,1000));assert(!g.allow(false,1001));
  // A flicker can't chatter it.
  assert(!g.allow(true,1100));assert(!g.state());
  assert(g.allow(true,1000+kRelayGapMs));assert(g.state());
  int switches=0;for(uint32_t t=2000;t<4000;++t)switches+=g.allow(t%2==0,t);
  assert(switches<=int(2000/kRelayGapMs)+1);
  // After the hub comes back, the state is sent again straight away.
  g.forget();assert(!g.state());assert(g.allow(false,4001));
}

void testSaving(){
  Book book;book.count=3;book.rules[0]=starterRule();
  book.rules[1]=rule("Too humid");book.rules[1].any=false;book.rules[1].on=false;book.rules[1].conds=3;book.rules[1].actions=3;
  book.rules[1].holdMs=30000;book.rules[1].cond[2]={Sig::Tilt,Op::Above,-45};
  book.rules[1].action[2].out=Out::Tone;book.rules[1].action[2].how=How::FollowBack;book.rules[1].action[2].value=-7;book.rules[1].action[2].source=Sig::Satellites;
  book.rules[2]=rule("");book.capPercent=50;book.lightFlipped=false;book.showMode=1;book.wiring.port[0]=Part::Strip;book.wiring.port[5]=Part::Knob;
  uint8_t bytes[kFileBytes];const size_t size=encode(book,bytes);
  assert(size==kHeaderBytes+3*kRuleBytes+2);
  Book back;assert(decode(bytes,size,back));
  assert(back.count==3&&back.capPercent==50&&!back.lightFlipped&&back.showMode==1);
  assert(back.wiring.port[0]==Part::Strip&&back.wiring.port[5]==Part::Knob);
  assert(std::strcmp(back.rules[0].name,"Wave lights")==0&&back.rules[0].conds==2&&back.rules[0].holdMs==2000);
  const Rule& r=back.rules[1];assert(!r.any&&!r.on&&r.holdMs==30000&&r.cond[2].sig==Sig::Tilt&&r.cond[2].value==-45);
  assert(r.action[2].out==Out::Tone&&r.action[2].how==How::FollowBack&&r.action[2].value==-7&&r.action[2].source==Sig::Satellites);
  // Full up, and it fits the buffer exactly.
  Book full;full.count=kMaxRules;for(Rule& x:full.rules){std::memset(x.name,'x',kNameChars);x.name[kNameChars]=0;}
  assert(encode(full,bytes)==kFileBytes);assert(decode(bytes,kFileBytes,back)&&back.count==kMaxRules);
  // Empty is fine: every rule deleted stays deleted.
  Book empty;const size_t small=encode(empty,bytes);assert(decode(bytes,small,back)&&back.count==0);
  // Which parts are on the Grove wires comes back too.
  {Book b;b.direct.claim(Part::Knob);b.direct.claim(Part::Servo);uint8_t c[kFileBytes];const size_t n=encode(b,c);Book o;assert(decode(c,n,o)&&o.direct.white==Part::Knob&&o.direct.yellow==Part::Servo);
   b.direct.claim(Part::Relay);const size_t m=encode(b,c);assert(decode(c,m,o)&&o.direct.white==Part::Relay&&o.direct.yellow==Part::Relay);
   const auto seal=[](uint8_t* x,size_t l){const uint16_t k=checksum(x,l-2);x[l-2]=uint8_t(k&255);x[l-1]=uint8_t(k>>8);};
   // A part can't be on a wire that doesn't suit it, or half a relay on one.
   Book e;const size_t l=encode(e,c);c[14]=uint8_t(Part::Servo);seal(c,l);assert(!decode(c,l,o));
   encode(e,c);c[15]=uint8_t(Part::Knob);seal(c,l);assert(!decode(c,l,o));
   encode(e,c);c[14]=uint8_t(Part::Relay);seal(c,l);assert(!decode(c,l,o));
   encode(e,c);c[14]=uint8_t(kPartCount);seal(c,l);assert(!decode(c,l,o));}
  // A file from 0.2.232 to 0.2.235, with no such bytes, still loads.
  {Book b;b.count=1;b.rules[0]=starterRule();b.capPercent=50;uint8_t c[kFileBytes];const size_t n=encode(b,c);
   (void)n;uint8_t old[kFileBytes];std::memcpy(old,c,kOldHeaderBytes);std::memcpy(old+kOldHeaderBytes,c+kHeaderBytes,kOldRuleBytes);old[3]=1;
   const size_t oldSize=kOldHeaderBytes+kOldRuleBytes+2;const uint16_t k=checksum(old,oldSize-2);old[oldSize-2]=uint8_t(k&255);old[oldSize-1]=uint8_t(k>>8);
   Book o;assert(decode(old,oldSize,o)&&o.count==1&&!std::strcmp(o.rules[0].name,"Wave lights")&&o.capPercent==50&&!o.direct.any());}
  // A file from 0.2.236 to 0.2.244, with no phrases, still loads.
  {Book b;b.count=2;b.rules[0]=starterRule();b.rules[1]=recipeRule(3);b.capPercent=50;b.direct.claim(Part::Knob);uint8_t c[kFileBytes];encode(b,c);
   uint8_t old[kFileBytes];std::memcpy(old,c,kHeaderBytes);std::memcpy(old+kHeaderBytes,c+kHeaderBytes,kOldRuleBytes);std::memcpy(old+kHeaderBytes+kOldRuleBytes,c+kHeaderBytes+kRuleBytes,kOldRuleBytes);old[3]=2;
   const size_t oldSize=kHeaderBytes+2*kOldRuleBytes+2;const uint16_t k=checksum(old,oldSize-2);old[oldSize-2]=uint8_t(k&255);old[oldSize-1]=uint8_t(k>>8);
   Book o;assert(decode(old,oldSize,o)&&o.count==2&&!std::strcmp(o.rules[1].name,"Air theremin")&&o.direct.white==Part::Knob&&o.rules[0].say[0]==0&&o.rules[1].say[0]==0);}
  // The numbers behind the device pages.
  {assert(parkingGapMs(100)==1000&&parkingGapMs(300)==1000&&parkingGapMs(9)==90&&parkingGapMs(8)==0&&parkingGapMs(1)==0);
   assert(!strcmp(parkingWord(14),"Stop!")&&!strcmp(parkingWord(15),"Getting close")&&!strcmp(parkingWord(40),"All clear"));
   assert(!strcmp(comfortWord(22,45),"Comfortable")&&!strcmp(comfortWord(17,45),"Chilly")&&!strcmp(comfortWord(28,45),"Warm")&&!strcmp(comfortWord(22,29),"Dry air")&&!strcmp(comfortWord(22,61),"Damp"));
   assert(dewPoint(25,100)==25&&dewPoint(25,50)==15&&dewPoint(25,-5)==5);
   assert(!strcmp(skyWord(14),"Night")&&!strcmp(skyWord(39),"Dim")&&!strcmp(skyWord(74),"Daylight")&&!strcmp(skyWord(75),"Bright sun"));
   // A trend keeps the newest points, oldest first, and shows where a sensor was gone.
   Trends t;Inputs in;for(int i=0;i<kTrendPoints;++i)assert(t.point(Sig::Knob,i)==0);
   in.set(Sig::Knob,0,0);t.sample(in);in.set(Sig::Knob,100,0);t.sample(in);in.lose(Sig::Knob);t.sample(in);
   assert(t.point(Sig::Knob,kTrendPoints-1)==0&&t.point(Sig::Knob,kTrendPoints-2)==255&&t.point(Sig::Knob,kTrendPoints-3)==1);
   for(int i=0;i<kTrendPoints+3;++i){in.set(Sig::Knob,50,0);t.sample(in);}
   for(int i=0;i<kTrendPoints;++i)assert(t.point(Sig::Knob,i)==128);
   // A reading past its range stays on the graph.
   in.set(Sig::Distance,450,0);t.sample(in);assert(t.point(Sig::Distance,kTrendPoints-1)==255);
   t.clear();assert(t.point(Sig::Knob,0)==0);}
  // The diary keeps an hour in a line, and only what was really read.
  {Diary d;assert(d.empty());Inputs in;in.set(Sig::Distance,40,0);in.set(Sig::Motion,1,0);d.note(in);assert(!d.empty());in.set(Sig::Distance,12,100);d.note(in);in.set(Sig::Temperature,-3,100);d.note(in);in.lose(Sig::Distance);d.note(in);d.count(1,0);d.count(2,5);
   char line[200];Text t(line,sizeof(line));d.write("Nov 02 03h",t);assert(!strcmp(line,"Nov 02 03h: motion 3, rules fired 5, distance 12 to 40, temperature -3 to -3\n"));
   d.clear();assert(d.empty());Text u(line,sizeof(line));d.write("Nov 02 04h",u);assert(!strcmp(line,"Nov 02 04h: motion 0, rules fired 0\n"));
   // The longest line fits the buffer that writes it, with every sensor live at its widest.
   Inputs all;for(uint8_t i=0;i<kSigCount;++i)all.set(static_cast<Sig>(i),-30000,0);Diary w;w.note(all);w.count(65535,65535);char big[kDiaryLineChars];Text b(big,sizeof(big));w.write("Nov 02 03h",b);assert(big[strlen(big)-1]=='\n');}
  // A rule listens for its name: on once said, for that rule only, and off when said again.
  {Rule movie;strcpy(movie.name,"Movie time");Draft d;d.addCond("spoken","on",0);d.addSet("strip_color",240);assert(d.usable());std::memcpy(movie.cond,d.rule.cond,sizeof(movie.cond));std::memcpy(movie.action,d.rule.action,sizeof(movie.action));movie.conds=1;movie.actions=1;movie.holdMs=0;
   Rule lights=movie;strcpy(lights.name,"Lights");Rule off=movie;strcpy(off.name,"Time");Rule deaf=starterRule();strcpy(deaf.name,"Deaf");
   Rule rules[4]={movie,lights,off,deaf};RuleState st[4];Drive out[kOutCount];Inputs none;
   step(rules,4,st,none,1000,out,0);assert(!st[0].active&&!st[1].active);
   step(rules,4,st,none,1010,out,1);assert(st[0].active&&!st[1].active&&out[uint8_t(Out::StripColor)].on);
   step(rules,4,st,none,1020,out,0);assert(!st[0].active);
   assert(listensForName(movie)&&!listensForName(deaf));
   // A "not said" line is the opposite, and needs no reading to work.
   Cond notSaid;notSaid.sig=Sig::Spoken;setOnOff(notSaid,false);assert(condHolds(notSaid,none,0,false)&&!condHolds(notSaid,none,0,true));
   // However speech recognition writes it. The longest name wins, and a rule that doesn't listen can't be named.
   assert(spokenRule("Movie time.",rules,4)==0);assert(spokenRule("  MOVIE-TIME!",rules,4)==0);assert(spokenRule("start movie time please",rules,4)==0);
   assert(spokenRule("lights",rules,4)==1);assert(spokenRule("time",rules,4)==2);assert(spokenRule("Deaf",rules,4)==-1);assert(spokenRule("nothing here",rules,4)==-1);assert(spokenRule("",rules,4)==-1);
   assert(spokenRule("movie",rules,4)==-1);
   Rule blank=movie;blank.name[0]=0;assert(spokenRule("anything",&blank,1)==-1);
   // Only saying it or not saying it makes sense.
   Draft e;e.addCond("spoken","changes",0);assert(!e.ok);Draft f;f.addCond("spoken","over",1);assert(!f.ok);
   Draft g;g.addFollow("servo","spoken",false);assert(!g.ok);  // A name has no value to follow.
   // A saved file with it still loads.
   Book b;b.count=1;b.rules[0]=movie;uint8_t c[kFileBytes];const size_t n=encode(b,c);Book o;assert(decode(c,n,o)&&o.rules[0].cond[0].sig==Sig::Spoken);}
  // A phrase saves with its rule, and a file with anything strange in one is refused.
  {Draft d;d.name("Welcome");d.addCond("motion","on",0);d.addSay("  Welcome back, friend! ");d.addSet("led",30);assert(d.usable()&&!std::strcmp(d.rule.say,"Welcome back, friend!")&&hasSay(d.rule));
   Book b;b.count=1;b.rules[0]=d.rule;uint8_t c[kFileBytes];const size_t n=encode(b,c);assert(c[3]==3&&n==kHeaderBytes+kRuleBytes+2);
   Book o;assert(decode(c,n,o)&&!std::strcmp(o.rules[0].say,d.rule.say)&&hasSay(o.rules[0])&&o.rules[0].action[0].out==Out::Say);
   c[kHeaderBytes+kOldRuleBytes+2]=0x07;const uint16_t k=checksum(c,n-2);c[n-2]=uint8_t(k&255);c[n-1]=uint8_t(k>>8);Book x;assert(!decode(c,n,x));}
  // What a rule can say is kept plain, and a rule can't say two things or nothing.
  {Draft d;d.addSay("Say \"hello\"\\ \xD0\xBF{}");assert(!std::strcmp(d.rule.say,"Say hello")&&d.ok);d.addSay("again");assert(!d.ok);
   Draft e;e.addSay("  \x01");assert(!e.ok&&e.rule.actions==0);
   Draft f;f.addCond("motion","on",0);f.addSet("say",1);assert(!f.ok);
   Draft g;g.addCond("motion","on",0);g.addFollow("say","knob",false);assert(!g.ok);
   Draft h;h.addSay("This phrase goes on far longer than anyone would want to listen to");assert(std::strlen(h.rule.say)==kSayChars);
   Rule silent;silent.conds=1;silent.actions=1;std::strcpy(silent.say,"Left over");assert(!hasSay(silent));
   assert(sayHash("welcome")==sayHash("welcome")&&sayHash("welcome")!=sayHash("welcome!"));}
  // Every flipped byte and every short read is refused, and leaves the book alone.
  const size_t good=encode(book,bytes);
  for(size_t i=0;i<good;++i)for(int bit=0;bit<8;++bit){
    uint8_t copy[kFileBytes];std::memcpy(copy,bytes,good);copy[i]^=uint8_t(1<<bit);
    Book out;out.count=7;assert(!decode(copy,good,out)&&out.count==7);}
  for(size_t i=0;i<good;++i){Book out;out.count=7;assert(!decode(bytes,i,out)&&out.count==7);}
  // A file with a good checksum but impossible numbers can't get in.
  const auto reseal=[](uint8_t* b,size_t n){const uint16_t s=checksum(b,n-2);b[n-2]=uint8_t(s&255);b[n-1]=uint8_t(s>>8);};
  const auto refused=[&](size_t at,uint8_t value){uint8_t copy[kFileBytes];std::memcpy(copy,bytes,good);copy[at]=value;reseal(copy,good);Book out;return !decode(copy,good,out);};
  const size_t first=kHeaderBytes+kNameChars+1;
  assert(refused(3,9));                                     // A newer file.
  assert(refused(5,uint8_t(kPartCount)));                   // A part that doesn't exist.
  assert(refused(6,bytes[5]));                              // The same part on two ports.
  assert(refused(11,33));                                   // A cap that isn't one of the three.
  assert(refused(first+2,kMaxConds+1));                     // More lines than a rule has room for.
  assert(refused(first+3,kMaxActions+1));
  assert(refused(first+6,kSigCount));                       // A sensor past the end.
  assert(refused(first+7,3));                               // A comparison past the end.
  assert(refused(first+6+kMaxConds*4,kOutCount));           // An output past the end.
  assert(refused(first+7+kMaxConds*4,3));
  assert(refused(first+10+kMaxConds*4,kSigCount));
  {uint8_t copy[kFileBytes];std::memcpy(copy,bytes,good);copy[first+4]=0xFF;copy[first+5]=0xFF;reseal(copy,good);Book out;assert(!decode(copy,good,out));}
  // A name with no end of its own still comes back ended.
  {Book n;n.count=1;std::memset(n.rules[0].name,'a',sizeof(n.rules[0].name));uint8_t b[kFileBytes];const size_t s=encode(n,b);Book o;assert(decode(b,s,o)&&std::strlen(o.rules[0].name)==kNameChars);}
}

void testDirect(){
  Direct d;assert(!d.any()&&!d.has(Part::Knob)&&!d.has(Part::None));
  // Each wire has one job, so a sensor and an output can share the port.
  d.claim(Part::Knob);assert(d.white==Part::Knob&&d.yellow==Part::None&&d.any());
  d.claim(Part::Servo);assert(d.white==Part::Knob&&d.yellow==Part::Servo&&d.has(Part::Knob)&&d.has(Part::Servo));
  // The newest part on a wire takes it.
  d.claim(Part::Motion);assert(d.white==Part::Motion&&d.yellow==Part::Servo&&!d.has(Part::Knob));
  d.claim(Part::Strip);assert(d.yellow==Part::Strip&&!d.has(Part::Servo));
  // The relay unit needs both, and anything taking either half removes it.
  d.claim(Part::Relay);assert(d.white==Part::Relay&&d.yellow==Part::Relay);
  d.claim(Part::Light);assert(d.white==Part::Light&&d.yellow==Part::None&&!d.has(Part::Relay));
  d.claim(Part::Relay);d.claim(Part::Servo);assert(d.yellow==Part::Servo&&d.white==Part::None);
  // The servo pulse is the one the hub's chip makes: 500 to 2500 microseconds at 50 Hz.
  assert(servoDuty(0)==409&&servoDuty(180)==2048&&servoDuty(90)==1228);
  assert(servoDuty(-5)==servoDuty(0)&&servoDuty(999)==servoDuty(180));
  // Strip data is green first, high bit first, and every bit lasts the same 1.2 microseconds.
  struct Sym{uint32_t level0,duration0,level1,duration1;}bits[48];
  const uint8_t rgb[2][3]={{0x80,0x01,0xFF},{0,0,0}};encodeStrip(rgb,2,bits);
  for(const Sym& b:bits)assert(b.level0==1&&b.level1==0&&b.duration0+b.duration1==12);
  const auto one=[&](int i){return bits[i].duration0==7;};
  for(int i=0;i<7;++i)assert(!one(i));assert(one(7));                  // Green 0x01.
  assert(one(8));for(int i=9;i<16;++i)assert(!one(i));                 // Red 0x80.
  for(int i=16;i<24;++i)assert(one(i));                                // Blue 0xFF.
  for(int i=24;i<48;++i)assert(!one(i));
}

void testPractice(){
  Inputs real;Practice p;assert(!p.any()&&!p.faking(Sig::Knob));
  // With nothing plugged in it starts from the middle, and with a reading from that.
  p.nudge(Sig::Knob,1,real,100);assert(p.faking(Sig::Knob)&&p.any());
  Inputs view=real;p.overlay(view);assert(view.get(Sig::Knob).live&&view.get(Sig::Knob).value==55);
  real.set(Sig::Light,20,100);p.nudge(Sig::Light,-1,real,100);view=real;p.overlay(view);assert(view.get(Sig::Light).value==15);
  // Only what's being faked is covered.
  real.set(Sig::Humidity,40,100);view=real;p.overlay(view);assert(view.get(Sig::Humidity).value==40);
  // A real sensor moving underneath doesn't touch a faked one.
  real.set(Sig::Knob,3,200);real.set(Sig::Knob,90,300);view=real;p.overlay(view);assert(view.get(Sig::Knob).value==55);
  // It stops at the ends, and an on or off sensor flips.
  for(int i=0;i<60;++i)p.nudge(Sig::Knob,1,real,400);view=real;p.overlay(view);assert(view.get(Sig::Knob).value==100);
  for(int i=0;i<60;++i)p.nudge(Sig::Knob,-1,real,400);view=real;p.overlay(view);assert(view.get(Sig::Knob).value==0);
  p.nudge(Sig::Motion,1,real,500);view=real;p.overlay(view);const int16_t first=view.get(Sig::Motion).value;
  p.nudge(Sig::Motion,1,real,501);view=real;p.overlay(view);assert(view.get(Sig::Motion).value!=first);
  // A press counts as a change, so a "changes" rule can be tried.
  Practice c;Inputs none;c.nudge(Sig::Knob,1,none,1000);Inputs v;c.overlay(v);
  Cond turns{Sig::Knob,Op::Changes,0};assert(condHolds(turns,v,1000));assert(condHolds(turns,v,1000+kChangedMs-1));assert(!condHolds(turns,v,1000+kChangedMs));
  // Every signal can be pushed past its step in one press.
  for(uint8_t i=0;i<kSigCount;++i){Practice q;Inputs n;const Sig sig=Sig(i);q.nudge(sig,1,n,1);q.nudge(sig,-1,n,2);Inputs w;q.overlay(w);assert(w.get(sig).moved);}
  p.clear();assert(!p.any());view=real;p.overlay(view);assert(view.get(Sig::Knob).value==90&&!view.get(Sig::Motion).live);
}

void testRecipes(){
  std::map<std::string,int> names;
  for(int i=0;i<kRecipeCount;++i){
    const Rule r=recipeRule(i);
    assert(r.name[0]&&std::strlen(r.name)<=kNameChars&&++names[r.name]==1);
    assert(r.on&&r.conds==1&&(r.actions==1||r.actions==2)&&r.holdMs<=kMaxHoldMs);
    assert(std::strlen(kRecipes[i].about)*6<=232);
    for(uint8_t a=0;a<r.actions;++a){const Action& x=r.action[a];const OutInfo& o=info(x.out);
      if(x.how==How::Set)assert(x.value>=o.lo&&x.value<=o.hi);else assert(!o.onOff);}
    const Cond& c=r.cond[0];if(c.op!=Op::Changes&&!info(c.sig).onOff)assert(c.value>=info(c.sig).lo&&c.value<=info(c.sig).hi);
    // Each one fires when its sensor does what it describes, and passes through saving unchanged.
    Book b;b.count=1;b.rules[0]=r;uint8_t bytes[kFileBytes];const size_t n=encode(b,bytes);Book back;assert(decode(bytes,n,back));
    Inputs in;const SigInfo& s=info(c.sig);in.set(c.sig,c.op==Op::Above?s.lo:s.hi,1);
    if(c.op==Op::Changes)in.set(c.sig,int16_t((s.lo+s.hi)/2),1000);
    else in.set(c.sig,c.op==Op::Above?int16_t(c.value+1):int16_t(c.value-1),1000);
    Drive d[kOutCount];RuleState st[1];step(&back.rules[0],1,st,in,1000,d);
    assert(st[0].active);
    for(uint8_t a=0;a<r.actions;++a)assert(d[uint8_t(r.action[a].out)].on);
  }
  // Rules saved before the lamp and AC outputs existed still load.
  Book old;old.count=1;old.rules[0]=starterRule();uint8_t bytes[kFileBytes];const size_t n=encode(old,bytes);Book back;assert(decode(bytes,n,back)&&back.rules[0].action[1].out==Out::Servo);
  assert(uint8_t(Out::Tone)==7&&uint8_t(Out::Servo)==0);
}

void testRemotes(){
  RemoteSwitch s;bool send=true;const uint32_t gap=500;
  // Starting up, and a rule that isn't asking, send nothing: the lamp may be in use.
  assert(!s.update(false,false,1000,gap,send));assert(!s.release());
  // A rule turning it on is sent once, and repeats are not.
  assert(s.update(true,true,1000,gap,send)&&send);assert(!s.update(true,true,2000,gap,send));
  // When the rule stops, its "on" is undone, but not before the gap.
  s=RemoteSwitch{};assert(s.update(true,true,1000,gap,send));assert(!s.update(false,false,1100,gap,send));
  assert(s.update(false,false,1500,gap,send)&&!send);assert(!s.update(false,false,3000,gap,send));
  // A rule that turns it off leaves it off afterwards, and doesn't turn it back on.
  RemoteSwitch t;assert(t.update(true,false,1000,gap,send)&&!send);assert(!t.update(false,false,2000,gap,send));assert(!t.release());
  // Leaving undoes an "on" at once, whatever the gap.
  RemoteSwitch u;assert(u.update(true,true,1000,gap,send));assert(u.release());assert(!u.release());
  // A rule flickering can't flood it.
  RemoteSwitch v;int sends=0;for(uint32_t ms=0;ms<10000;++ms)sends+=v.update(ms%2==0,true,ms,gap,send);assert(sends<=21);
}

void testDrafts(){
  Sig sig;Out out;assert(sigFromKey("stick_x",sig)&&sig==Sig::StickX&&outFromKey("ac_daikin",out)&&out==Out::AcDaikin);
  assert(!sigFromKey("Distance",sig)&&!sigFromKey("",sig)&&!sigFromKey(nullptr,sig)&&!outFromKey("servo ",out)&&!outFromKey(nullptr,out));
  for(const SigInfo& s:kSigs)assert(sigFromKey(s.key,sig));
  for(const OutInfo& o:kOuts)assert(outFromKey(o.key,out));
  Draft d;d.name("Wave!! red; ok");d.hold(999999);d.rule.any=false;
  d.addCond("distance","under",30);d.addCond("motion","on",0);d.addCond("knob","changes",0);
  d.addSet("strip_color",0);d.addFollow("servo","knob",true);d.addSet("relay_1",7);
  assert(d.usable());const Rule& r=d.rule;
  assert(!std::strcmp(r.name,"Wave red ok")&&r.holdMs==kMaxHoldMs&&r.conds==3&&r.actions==3);
  assert(r.cond[0].op==Op::Below&&r.cond[0].value==30&&r.cond[1].op==Op::Above&&r.cond[1].value==0&&r.cond[2].op==Op::Changes);
  assert(r.action[1].how==How::FollowBack&&r.action[1].source==Sig::Knob&&r.action[2].value==1);
  // A fourth line, a made-up word, or a test that doesn't suit the sensor all refuse the rule.
  const auto refused=[](void(*f)(Draft&)){Draft x;x.addCond("knob","changes",0);x.addSet("servo",1);f(x);return !x.usable();};
  assert(refused([](Draft& x){x.addCond("ghost","under",1);}));
  assert(refused([](Draft& x){x.addCond("motion","under",1);}));
  assert(refused([](Draft& x){x.addCond("knob","on",0);}));
  assert(refused([](Draft& x){x.addCond("knob","sometimes",0);}));
  assert(refused([](Draft& x){x.addCond("knob",nullptr,0);}));
  assert(refused([](Draft& x){x.addSet("toaster",1);}));
  assert(refused([](Draft& x){x.addFollow("relay_1","knob",false);}));   // A relay can't follow.
  assert(refused([](Draft& x){x.addFollow("servo","ghost",false);}));
  assert(refused([](Draft& x){x.addFollow("toaster","knob",false);}));
  assert(refused([](Draft& x){x.addCond("knob","changes",0);x.addCond("knob","changes",0);x.addCond("knob","changes",0);}));
  assert(refused([](Draft& x){x.addSet("led",0);x.addSet("led",0);x.addSet("led",0);}));
  // Nothing to watch or nothing to do isn't a rule.
  Draft e;assert(!e.usable());e.addSet("servo",1);assert(!e.usable());Draft f;f.addCond("knob","changes",0);assert(!f.usable());
  // Out-of-range numbers come back in range, even huge ones.
  Draft g;g.addCond("distance","under",99999999);g.addCond("tilt","over",-99999999);g.addSet("servo",999999);g.addSet("speaker",-5);
  assert(g.usable()&&g.rule.cond[0].value==100&&g.rule.cond[1].value==-90&&g.rule.action[0].value==180&&g.rule.action[1].value==200);
  // Names can't smuggle anything into a screen or a file.
  Draft n;n.name("[[x]]\n\t%s\xf8""ab");assert(!std::strcmp(n.rule.name,"xsab"));n.name(nullptr);assert(!n.rule.name[0]);
  std::string big(200,'a');n.name(big.c_str());assert(std::strlen(n.rule.name)==kNameChars);
}


Sequence seqOf(std::initializer_list<SeqStep> steps,bool loop=false){Sequence s;strcpy(s.name,"Test");s.loop=loop;for(const SeqStep& t:steps)s.steps[s.count++]=t;return s;}
constexpr SeqStep setStep(Out o,int v){return SeqStep{SeqKind::Set,o,int16_t(v)};}
constexpr SeqStep offStep(Out o){return SeqStep{SeqKind::Off,o,0};}
constexpr SeqStep waitStep(int tenths){return SeqStep{SeqKind::Wait,Out::Servo,int16_t(tenths)};}
Drive at(const Drive* d,Out o){return d[uint8_t(o)];}
// One pass of the Lab's loop for a sequence by itself.
void tick(SeqRun& run,const Sequence* seqs,uint8_t count,uint32_t now,Drive* d){for(uint8_t i=0;i<kOutCount;++i)d[i]=Drive{};run.service(seqs,count,now);run.apply(d);}
bool seqRefused(uint8_t* bytes,size_t n){const uint16_t sum=checksum(bytes,n-2);bytes[n-2]=uint8_t(sum&0xFF);bytes[n-1]=uint8_t(sum>>8);Sequence out[kMaxSequences];uint8_t count=99;const bool ok=decodeSequences(bytes,n,out,count);return !ok&&count==99;}

void testSequences(){
  Drive d[kOutCount];
  // Steps happen in order, waits hold them up, and what's set stays set until it changes.
  {Sequence s=seqOf({setStep(Out::Servo,10),setStep(Out::Relay1,1),waitStep(10),setStep(Out::Servo,170),waitStep(5),offStep(Out::Relay1)});SeqRun r;
   assert(!r.running());tick(r,&s,1,1000,d);assert(!at(d,Out::Servo).on);
   r.start(0,-1,1000);assert(r.running()&&r.owner()==-1&&r.index()==0);
   tick(r,&s,1,1000,d);assert(at(d,Out::Servo).on&&at(d,Out::Servo).value==10&&at(d,Out::Relay1).value==1&&r.shown()==2&&r.leftMs(1000)==1000);
   tick(r,&s,1,1999,d);assert(at(d,Out::Servo).value==10&&r.leftMs(1999)==1);
   tick(r,&s,1,2000,d);assert(at(d,Out::Servo).value==170&&at(d,Out::Relay1).value==1&&r.shown()==4);
   tick(r,&s,1,2500,d);assert(at(d,Out::Servo).value==170&&!at(d,Out::Relay1).on&&r.running());
   // A last step with no wait still gets out, for long enough to reach its part, and then everything lets go.
   tick(r,&s,1,2500+kSeqEndMs-1,d);assert(r.running()&&at(d,Out::Servo).on);tick(r,&s,1,2500+kSeqEndMs,d);assert(!r.running()&&!at(d,Out::Servo).on);
   tick(r,&s,1,9000,d);assert(!at(d,Out::Servo).on);}
  // Ending on a wait ends when the wait does, and it works across the clock wrapping.
  {Sequence s=seqOf({setStep(Out::Led,5),waitStep(1)});SeqRun r;const uint32_t start=0xFFFFFFF0;r.start(0,-1,start);tick(r,&s,1,start,d);assert(at(d,Out::Led).on);tick(r,&s,1,start+99,d);assert(r.running());tick(r,&s,1,start+100,d);assert(!r.running()&&!at(d,Out::Led).on);}
  // Nothing to do, or a place in the list that's gone, ends quietly.
  {Sequence s;SeqRun r;r.start(0,-1,0);tick(r,&s,1,0,d);assert(!r.running());r.start(3,-1,0);tick(r,&s,1,0,d);assert(!r.running());}
  // A loop goes round, keeping what it set, and with no waits it still gives the Lab its turn.
  {Sequence s=seqOf({setStep(Out::Led,1),waitStep(1),setStep(Out::Led,2),waitStep(1)},true);SeqRun r;r.start(0,-1,0);uint32_t t=0;
   for(int i=0;i<6;++i){tick(r,&s,1,t,d);assert(r.running()&&at(d,Out::Led).on&&at(d,Out::Led).value==(i%2?2:1));t+=100;}
   Sequence q=seqOf({setStep(Out::Led,1),setStep(Out::Led,2)},true);r.start(0,-1,0);tick(r,&q,1,0,d);assert(at(d,Out::Led).value==2&&r.leftMs(0)==kSeqEndMs);tick(r,&q,1,1,d);assert(r.leftMs(1)==kSeqEndMs-1);tick(r,&q,1,kSeqEndMs,d);assert(r.running());
   r.stop();assert(!r.running());tick(r,&q,1,kSeqEndMs+1,d);assert(!at(d,Out::Led).on);}
  // Off turns the whole strip off, and a sequence's off beats a rule's on, while rules keep the rest.
  {Sequence s=seqOf({setStep(Out::StripColor,120),setStep(Out::StripBright,50),waitStep(1),offStep(Out::StripColor),waitStep(1)});SeqRun r;r.start(0,-1,0);Drive rule[kOutCount];rule[uint8_t(Out::StripLength)]={true,5};rule[uint8_t(Out::Relay2)]={true,1};
   r.service(&s,1,0);r.apply(rule);assert(rule[uint8_t(Out::StripColor)].value==120&&rule[uint8_t(Out::StripBright)].value==50&&rule[uint8_t(Out::StripLength)].value==5&&rule[uint8_t(Out::Relay2)].value==1);
   assert(stripFrame(rule,100).on);
   r.service(&s,1,100);r.apply(rule);assert(!rule[uint8_t(Out::StripColor)].on&&!rule[uint8_t(Out::StripBright)].on&&!rule[uint8_t(Out::StripLength)].on&&rule[uint8_t(Out::Relay2)].on&&!stripFrame(rule,100).on);}
  // Rules start sequences.
  {Sequence seqs[2];seqs[0]=seqOf({setStep(Out::Led,1),waitStep(10)});seqs[1]=seqOf({setStep(Out::Led,2),waitStep(1)},true);
   Rule rules[2];rules[0].conds=rules[1].conds=1;rules[0].cond[0]={Sig::Knob,Op::Above,50};rules[1].cond[0]={Sig::Light,Op::Above,50};rules[0].holdMs=rules[1].holdMs=0;
   rules[0].actions=rules[1].actions=1;rules[0].action[0]={Out::Sequence,How::Set,0,Sig::Knob};rules[1].action[0]={Out::Sequence,How::Set,1,Sig::Knob};
   RuleState st[2];uint16_t seen[2]={};SeqRun r;Inputs in;Drive out[kOutCount];uint32_t t=1000;
   const auto pass=[&](int knob,int light){in.set(Sig::Knob,int16_t(knob),t);in.set(Sig::Light,int16_t(light),t);step(rules,2,st,in,t,out);runSequences(rules,2,st,seen,seqs,2,r,t);r.service(seqs,2,t);r.apply(out);t+=10;};
   pass(0,0);assert(!r.running());
   // Once, when its rule starts firing, and staying true doesn't start it again.
   pass(60,0);assert(r.running()&&r.owner()==0&&r.index()==0&&at(out,Out::Led).value==1);
   for(int i=0;i<30;++i)pass(60,0);assert(r.running()&&st[0].fires==1);for(int i=0;i<80;++i)pass(60,0);assert(!r.running()&&st[0].active&&st[0].fires==1);
   // Its output isn't left set just because the rule's still firing.
   assert(!at(out,Out::Led).on);
   // Firing again starts it again, and the rule ending doesn't cut a play-once sequence short.
   pass(0,0);pass(60,0);assert(r.running()&&st[0].fires==2);pass(0,0);assert(!st[0].active&&r.running());for(int i=0;i<110;++i)pass(0,0);assert(!r.running());
   // A new start replaces whatever's running, and only one ever runs.
   pass(60,0);assert(r.index()==0);pass(60,60);assert(r.running()&&r.index()==1&&r.owner()==1);
   // A loop ends with its rule, and a play-once rule that took over gives way back to it.
   pass(0,60);pass(0,0);assert(!r.running());
   pass(0,60);assert(r.index()==1);pass(60,60);assert(r.index()==0&&r.owner()==0);for(int i=0;i<120;++i)pass(60,60);assert(r.running()&&r.index()==1&&r.owner()==1);
   // After a pause stops it, a loop whose rule is still firing picks up again, and a play-once one doesn't.
   r.stop();pass(60,60);assert(r.running()&&r.index()==1);rules[1].on=false;pass(60,60);assert(!r.running());pass(60,60);assert(!r.running());rules[1].on=true;pass(0,0);
   // Two rules starting together: the later one wins, as it does for an output.
   pass(60,60);assert(r.index()==1);pass(0,0);
   // A place in the list that's gone is ignored, with the running one left alone.
   rules[0].action[0].value=7;pass(60,0);assert(!r.running());rules[0].action[0].value=-1;pass(0,0);pass(60,0);assert(!r.running());}
  // Deleting moves the rules along with the list, and drops what can't be played any more.
  {Sequence seqs[3];strcpy(seqs[0].name,"A");strcpy(seqs[1].name,"B");strcpy(seqs[2].name,"C");uint8_t count=3;Rule rules[2];
   rules[0].actions=3;rules[0].action[0]=setTo(Out::Led,5);rules[0].action[1]={Out::Sequence,How::Set,1,Sig::Knob};rules[0].action[2]={Out::Sequence,How::Set,2,Sig::Knob};
   rules[1].actions=1;rules[1].action[0]={Out::Sequence,How::Set,0,Sig::Knob};
   assert(sequenceUsers(rules,2,0)==1&&sequenceUsers(rules,2,1)==1&&sequenceUsers(rules,2,2)==1);
   removeSequence(seqs,count,rules,2,1);assert(count==2&&!strcmp(seqs[1].name,"C"));
   assert(rules[0].actions==2&&rules[0].action[0].out==Out::Led&&rules[0].action[1].value==1&&rules[1].action[0].value==0);
   removeSequence(seqs,count,rules,2,0);assert(count==1&&!strcmp(seqs[0].name,"C")&&rules[1].actions==0&&rules[0].action[1].value==0);
   removeSequence(seqs,count,rules,2,0);assert(count==0&&rules[0].actions==1);}
  // The starter needs nothing plugged in, and is a sequence like any other.
  {Sequence s=starterSequence();assert(s.count>0&&!s.loop&&s.name[0]);for(uint8_t i=0;i<s.count;++i){assert(stepAllowed(s.steps[i]));assert(s.steps[i].kind==SeqKind::Wait||s.steps[i].out==Out::Led||s.steps[i].out==Out::Tone);}}
  // Waits: fine when short, coarse when long, and going back retraces the way up.
  {int v=1,n=0;std::vector<int> up;while(v<kMaxWaitTenths&&n<500){up.push_back(v);v=nextWait(v,1);++n;}assert(v==kMaxWaitTenths&&n<80);for(size_t i=up.size();i-->0;){v=nextWait(v,-1);assert(v==up[i]);}
   assert(nextWait(1,-1)==1&&nextWait(kMaxWaitTenths,1)==kMaxWaitTenths&&nextWait(9,1)==10&&nextWait(10,1)==15&&nextWait(100,1)==150&&nextWait(600,1)==900);}
  // A step the file can't hold is one the editor can't make.
  {assert(stepAllowed(setStep(Out::Servo,180))&&!stepAllowed(setStep(Out::Servo,181))&&!stepAllowed(setStep(Out::Servo,-1)));
   assert(!stepAllowed(setStep(Out::Say,1))&&!stepAllowed(setStep(Out::Sequence,1))&&!stepAllowed(offStep(Out::Sequence)));
   assert(stepAllowed(waitStep(1))&&stepAllowed(waitStep(kMaxWaitTenths))&&!stepAllowed(waitStep(0))&&!stepAllowed(waitStep(kMaxWaitTenths+1)));
   assert(kSeqOutCount==uint8_t(Out::Say)&&isStrip(Out::StripLength)&&!isStrip(Out::Led)&&!canTurnOff(Out::Servo)&&canTurnOff(Out::Tone));}
  // Saving.
  {Sequence seqs[kMaxSequences];for(int i=0;i<kMaxSequences;++i){std::string name=std::string("Fifteen letter")+char('a'+i);strcpy(seqs[i].name,name.c_str());seqs[i].loop=i%2;for(int j=0;j<kMaxSeqSteps;++j)seqs[i].steps[seqs[i].count++]=j%2?waitStep(kMaxWaitTenths-j):setStep(Out::Tone,200+j*50);}
   uint8_t big[kSeqFileBytes+8]={};size_t n=encodeSequences(seqs,kMaxSequences,big);assert(n==kSeqFileBytes);
   Sequence back[kMaxSequences];uint8_t count=0;assert(decodeSequences(big,n,back,count)&&count==kMaxSequences);
   for(int i=0;i<kMaxSequences;++i){assert(!strcmp(back[i].name,seqs[i].name)&&back[i].loop==seqs[i].loop&&back[i].count==kMaxSeqSteps);for(int j=0;j<kMaxSeqSteps;++j)assert(back[i].steps[j].kind==seqs[i].steps[j].kind&&back[i].steps[j].value==seqs[i].steps[j].value&&back[i].steps[j].out==seqs[i].steps[j].out);}
   // Only the steps in use are stored, and none at all is a file too.
   Sequence one=seqOf({setStep(Out::Servo,10)});uint8_t small[kSeqFileBytes];n=encodeSequences(&one,1,small);assert(n==5+kNameChars+3+kSeqStepBytes+2);
   count=0;assert(decodeSequences(small,n,back,count)&&count==1&&back[0].count==1);n=encodeSequences(nullptr,0,small);assert(n==7);count=9;assert(decodeSequences(small,n,back,count)&&count==0);
   // Every kind of damage is refused, and leaves what was there.
   Sequence s=seqOf({setStep(Out::Servo,10),waitStep(5)},true);uint8_t file[kSeqFileBytes];const size_t size=encodeSequences(&s,1,file);const size_t step0=5+kNameChars+3;
   const auto damaged=[&](size_t where,uint8_t value){uint8_t copy[kSeqFileBytes];std::memcpy(copy,file,size);copy[where]=value;return seqRefused(copy,size);};
   assert(damaged(0,'X'));assert(damaged(3,kSeqFileVersion+1));assert(damaged(4,kMaxSequences+1));assert(damaged(4,2));assert(damaged(4,0));
   assert(damaged(5+kNameChars+2,kMaxSeqSteps+1));assert(damaged(5+kNameChars+2,3));assert(damaged(5+kNameChars+2,1));
   assert(damaged(step0,3));assert(damaged(step0+1,uint8_t(Out::Say)));assert(damaged(step0+1,uint8_t(Out::Sequence)));assert(damaged(step0+1,kOutCount));assert(damaged(step0+2,181));
   assert(damaged(step0+kSeqStepBytes+2,0));assert(damaged(step0+kSeqStepBytes+3,0x7F));
   {uint8_t copy[kSeqFileBytes];std::memcpy(copy,file,size);copy[7]^=1;Sequence out[kMaxSequences];count=9;assert(!decodeSequences(copy,size,out,count)&&count==9);}  // A bad checksum.
   for(size_t cut=0;cut<size;++cut){Sequence out[kMaxSequences];count=9;assert(!decodeSequences(file,cut,out,count)&&count==9);}
   {Sequence out[kMaxSequences];count=9;assert(!decodeSequences(file,kSeqFileBytes+1,out,count));}
   // A name that lost its end still ends.
   {uint8_t copy[kSeqFileBytes];std::memcpy(copy,file,size);std::memset(copy+5,'a',kNameChars+1);const uint16_t sum=checksum(copy,size-2);copy[size-2]=uint8_t(sum&0xFF);copy[size-1]=uint8_t(sum>>8);Sequence out[kMaxSequences];count=0;assert(decodeSequences(copy,size,out,count)&&std::strlen(out[0].name)==kNameChars);}}
  // A number can't stand in for a name, in either direction.
  {Draft a;a.addCond("motion","on",0);a.addSet("sequence",1);assert(!a.usable());Draft b;b.addCond("motion","on",0);b.addFollow("sequence","knob",false);assert(!b.usable());
   Sequence s;strcpy(s.name,"Wave");Draft c;c.addCond("motion","on",0);c.addPlay("wave",&s,1);c.addPlay("WAVE",&s,1);c.addPlay("Wave",&s,1);assert(c.usable()&&c.rule.actions==3);c.addPlay("Wave",&s,1);assert(!c.ok);
   Draft e;e.addPlay("",&s,1);assert(!e.ok);}
}

// What the chat is told on every message. The room is worked out for each message, so the longest
// possible Lab has to fit in it, and a rule or a step that grows has to fail here first.
void testChatState(){
  Rule rules[kMaxRules];Sequence seqs[kMaxSequences];
  char small[200];
  { Text out(small,sizeof(small));chatState(rules,0,seqs,0,out);assert(!strcmp(small,"saved rules: none\nsaved sequences: none\n"));}
  assert(chatChars(0,seqs,0)>strlen(small));
  for(int i=0;i<kMaxRules;++i){
    Rule& r=rules[i];r=Rule{};r.on=false;strcpy(r.name,"Fifteen letters");r.holdMs=kMaxHoldMs;r.conds=3;
    for(auto& c:r.cond)c=Cond{Sig::Temperature,Op::Below,-40};
    r.actions=3;r.action[0]=follow(Out::StripBright,Sig::Temperature);r.action[1]=oppose(Out::StripLength,Sig::Temperature);
    r.action[2]=Action{};r.action[2].out=i%2?Out::Say:Out::Sequence;r.action[2].how=How::Set;r.action[2].value=i%2?1:0;
    std::memset(r.say,'x',kSayChars);r.say[kSayChars]=0;
  }
  for(auto& s:seqs){s=Sequence{};strcpy(s.name,"Fifteen letters");s.loop=true;s.count=kMaxSeqSteps;for(int i=0;i<kMaxSeqSteps;++i)s.steps[i]=SeqStep{i%2?SeqKind::Wait:SeqKind::Set,Out::StripBright,static_cast<int16_t>(i%2?kMaxWaitTenths:100)};}
  const size_t room=chatChars(kMaxRules,seqs,kMaxSequences);
  std::vector<char> text(room+1,'#');Text out(text.data(),room);chatState(rules,kMaxRules,seqs,kMaxSequences,out);
  assert(text[room]=='#');
  assert(out.length()+1<room);  // Stopped by what it had to say, not by the end of the room.
  std::string said(text.data());assert(said.compare(said.size()-1,1,"\n")==0);
  assert(said.find("saved sequences:\n")!=std::string::npos);
  size_t lines=0;for(char c:said)lines+=c=='\n';assert(lines==size_t(2+kMaxRules+kMaxSequences));
  assert(out.length()<6000);  // What the bridge keeps.
  // A usual Lab costs far less than a full one, which is the point of working it out.
  Sequence two[2];two[0].count=6;two[1].count=4;assert(chatChars(3,two,2)<1400);
  // Nothing about a name's case or spacing changes which one it is, and a part of a name isn't it.
  strcpy(seqs[0].name,"Door wave");strcpy(seqs[1].name,"Hello");
  assert(findByName(seqs,2,"door WAVE")==0&&findByName(seqs,2,"hello")==1&&findByName(seqs,2,"Door")<0&&findByName(seqs,2,"")<0&&findByName(seqs,2,"Door wave again")<0&&findByName(seqs,0,"Hello")<0);
  strcpy(rules[3].name,"Wave red");assert(findByName(rules,4,"wave red")==3&&findByName(rules,3,"wave red")<0);
}
void testTables(){
  for(const SigInfo& s:kSigs)assert(s.key&&std::strlen(s.name)<=12);
  for(const OutInfo& o:kOuts)assert(o.key&&std::strlen(o.name)<=12);  // Both fit a flow box.
  for(const char* name:kDevNames)assert(std::strlen(name)<=13);
  // The launcher's number keys reach the new tab, and the menu stays as it was without it.
  {using namespace launcher_shortcuts;assert(resolve('9',false,9,2).kind==Kind::Group&&resolve('9',false,9,2).index==8);
   assert(resolve('9',false,8,2).kind==Kind::None);assert(resolve('9',true,9,2).kind==Kind::None);}
  for(const SigInfo& s:kSigs)assert(s.name&&s.lo<s.hi&&s.step>0&&s.step<=s.hi-s.lo);
  for(const OutInfo& o:kOuts)assert(o.name&&o.lo<o.hi&&o.step>0);
  std::map<uint8_t,int> seen;for(uint8_t a:kUnitAddresses)assert(++seen[a]==1&&a>=8&&a<0x78);
  assert(info(Out::Servo).hi==pbhub::kMaxAngle&&kStripLeds<=pbhub::kMaxLeds);
  assert(partFor(Dev::Servo)==Part::Servo&&partFor(Dev::Strip)==Part::Strip&&partFor(Dev::Relay)==Part::Relay&&
         partFor(Dev::Knob)==Part::Knob&&partFor(Dev::Light)==Part::Light&&partFor(Dev::Motion)==Part::Motion&&
         partFor(Dev::Ultrasonic)==Part::None&&partFor(Dev::Gps)==Part::None);
}

void testOnce() {
  // Only what a wrong answer can't hurt. Everything that can switch mains or speak stays out.
  for (uint8_t i = 0; i < kOutCount; ++i) {
    const Out out = static_cast<Out>(i);
    const bool safe = out == Out::Servo || out == Out::StripColor || out == Out::StripBright || out == Out::StripLength || out == Out::Led || out == Out::Tone;
    assert(onceCanDrive(out) == safe);
  }
  // It only reaches for the port when it has to.
  Once look; look.reads = 1; look.read[0] = Sig::Tilt; assert(look.usable() && !look.needsPort());
  look.read[0] = Sig::Satellites; assert(!look.needsPort());
  look.read[0] = Sig::Knob; assert(look.needsPort());
  Once led; led.yes = 1; led.then[0] = {SeqKind::Set, Out::Led, 120}; assert(led.usable() && !led.needsPort());
  Once strip; strip.no = 1; strip.otherwise[0] = {SeqKind::Set, Out::StripColor, 0}; strip.conds = 1; strip.cond[0] = {Sig::Tilt, Op::Above, 10}; assert(strip.needsPort());
  assert(!Once{}.usable());
  Once elseOnly; elseOnly.no = 1; assert(!elseOnly.usable());
  // Any and all, the same as a rule.
  Inputs in; in.set(Sig::Knob, 67, 0); in.set(Sig::Light, 10, 0);
  Once both; both.conds = 2; both.cond[0] = {Sig::Knob, Op::Above, 50}; both.cond[1] = {Sig::Light, Op::Above, 50};
  assert(onceHolds(both, in)); both.any = false; assert(!onceHolds(both, in)); both.cond[1].op = Op::Below; assert(onceHolds(both, in));
  assert(onceHolds(look, in));  // With nothing to test, there's nothing stopping it.
  // A sensor with no reading is never true.
  Once gone; gone.conds = 1; gone.cond[0] = {Sig::Distance, Op::Below, 500}; assert(!onceHolds(gone, in)); gone.any = false; assert(!onceHolds(gone, in));
  // Off clears the whole strip, because its three outputs are one strip.
  Drive drive[kOutCount];
  onceApply({SeqKind::Set, Out::StripColor, 240}, drive); onceApply({SeqKind::Set, Out::StripBright, 40}, drive);
  assert(drive[int(Out::StripColor)].on && drive[int(Out::StripColor)].value == 240 && drive[int(Out::StripBright)].value == 40);
  onceApply({SeqKind::Off, Out::StripBright, 0}, drive); assert(!stripFrame(drive, 100).on && !drive[int(Out::StripColor)].on);
  // The colors the prompt names, and an honest number between them.
  const struct { int hue; const char* word; } kHues[] = {{0, "red"}, {359, "red"}, {30, "orange"}, {60, "yellow"}, {120, "green"}, {180, "cyan"}, {240, "blue"}, {280, "purple"}, {330, "pink"}, {90, nullptr}, {210, nullptr}};
  for (const auto& hue : kHues) { const char* word = hueWord(hue.hue); assert(hue.word ? word && !strcmp(word, hue.word) : !word); }
  // What the owner reads.
  const auto says = [](const Once& once, const Inputs& inputs) { static char line[kOnceTextChars]; Text text(line, sizeof(line)); onceReadings(once, inputs, text); return std::string(line); };
  Once knob; knob.conds = 1; knob.cond[0] = {Sig::Knob, Op::Above, 50}; assert(says(knob, in) == "Knob 67%, over 50.");
  knob.cond[0] = {Sig::Knob, Op::Below, 50}; assert(says(knob, in) == "Knob 67%, not under 50.");
  in.set(Sig::Motion, 1, 0); in.set(Sig::Temperature, 24, 0); in.set(Sig::Tilt, -3, 0); in.set(Sig::Distance, 40, 0);
  Once on; on.conds = 1; setOnOff(on.cond[0], true); on.cond[0].sig = Sig::Motion; assert(says(on, in) == "Motion on.");
  Once some; some.reads = 3; some.read[0] = Sig::Temperature; some.read[1] = Sig::Tilt; some.read[2] = Sig::Distance; assert(says(some, in) == "Temperature 24 C; Tilt -3 deg; Distance 40 cm.");
  // Something tested and also listed is said once.
  Once twice; twice.conds = 1; twice.cond[0] = {Sig::Knob, Op::Above, 50}; twice.reads = 3; twice.read[0] = Sig::Knob; twice.read[1] = Sig::Tilt; twice.read[2] = Sig::Tilt; assert(says(twice, in) == "Knob 67%, over 50; Tilt -3 deg.");
  const auto did = [](std::initializer_list<SeqStep> steps) { static char line[kOnceTextChars]; Text text(line, sizeof(line)); onceDone(steps.begin(), static_cast<uint8_t>(steps.size()), text); return std::string(line); };
  assert(did({{SeqKind::Set, Out::StripColor, 0}}) == "Strip set to red."); assert(did({{SeqKind::Set, Out::Led, 90}}) == "LED set to hue 90.");
  assert(did({{SeqKind::Off, Out::StripColor, 0}}) == "Strip off."); assert(did({{SeqKind::Off, Out::Led, 0}}) == "LED off."); assert(did({}) == "Nothing changed.");
  assert(did({{SeqKind::Set, Out::Servo, 90}, {SeqKind::Set, Out::Tone, 880}, {SeqKind::Set, Out::StripBright, 40}}) == "Servo moved to 90 deg, beeped at 880 Hz, strip at 40% brightness.");
  assert(did({{SeqKind::Set, Out::StripLength, 10}}) == "Strip lit to 10 LEDs.");
  // The longest report still fits.
  Inputs far; far.set(Sig::Temperature, -10, 0); far.set(Sig::StickPress, 0, 0); far.set(Sig::Satellites, 32, 0); far.set(Sig::Humidity, 100, 0); far.set(Sig::Distance, 450, 0); far.set(Sig::StickX, -100, 0);
  Once full; full.conds = 3; for (auto& cond : full.cond) cond = {Sig::Temperature, Op::Below, -10}; full.cond[1].sig = Sig::Humidity; full.cond[1].value = 50; full.cond[2].sig = Sig::Distance; full.cond[2].value = 100;
  full.reads = 3; full.read[0] = Sig::StickPress; full.read[1] = Sig::Satellites; full.read[2] = Sig::StickX;
  static char line[kOnceTextChars]; Text text(line, sizeof(line)); onceReadings(full, far, text); text.add(" "); const SeqStep longest[3] = {{SeqKind::Set, Out::StripBright, 100}, {SeqKind::Set, Out::StripLength, 30}, {SeqKind::Set, Out::Servo, 180}}; onceDone(longest, 3, text); assert(text.room() > 0);
  // The Lab's hub range: a number before it's been turned means nothing.
  AnalogRange range; assert(!range.learned()); range.percent(2000); assert(!range.learned()); range.percent(1500); assert(!range.learned()); range.percent(300); assert(!range.learned()); range.percent(100); assert(range.learned());
  assert(AnalogRange::fixedAt(2800).learned() && AnalogRange::fixedAt(2800).top == 2800 && !AnalogRange{}.fixed);
  AnalogRange light; light.percent(3000); light.percent(1400); assert(!light.learned() && light.learned(kLightRangeShare));
}

int main(){
  testHub();testGps();testRanges();testInputs();testRules();testWiring();testCloseness();testStrip();testRelays();testDirect();testPractice();testRecipes();testRemotes();testDrafts();testSaving();testSequences();testChatState();testOnce();testTables();JSON_TEST
  std::puts("PASS Lab: hub commands, GPS lines, ranges, change detection, rules with hold and priority, sequences, wiring, the strip cap, the relay guard, and saved files that refuse every kind of damage");
}
'''
# The real JSON library, once PlatformIO has fetched it. CI's firmware job has it, and the logic
# above doesn't depend on it.
library = root / 'bots/CardputerADV/.pio/libdeps/cardputer_adv/ArduinoJson/src'
json_test = r'''
#include "lab_json.h"
Sequence gSeqs[2];uint8_t gSeqCount=0;
bool parse(const char* text, Rule& out){StaticJsonDocument<2048> doc;assert(!deserializeJson(doc,text));return ruleFromJson(doc.as<JsonObjectConst>(),out,gSeqs,gSeqCount);}
size_t slotsIn(JsonVariantConst value){size_t n=0;
  if(value.is<JsonObjectConst>())for(JsonPairConst p:value.as<JsonObjectConst>())n+=1+slotsIn(p.value());
  else if(value.is<JsonArrayConst>())for(JsonVariantConst v:value.as<JsonArrayConst>())n+=1+slotsIn(v);
  return n;}
bool parseSeq(const char* text, Sequence& out){DynamicJsonDocument doc(8192);assert(!deserializeJson(doc,text));return sequenceFromJson(doc.as<JsonObjectConst>(),out);}
bool parseOnce(const char* text, Once& out){DynamicJsonDocument doc(2048);assert(!deserializeJson(doc,text));return onceFromJson(doc.as<JsonObjectConst>(),out);}
void testOnceJson(){
  // The examples in the bridge's prompt.
  Once o;assert(parseOnce(R"({"type":"lab.once","any":true,"when":[{"sensor":"knob","test":"over","value":50}],"do":[{"output":"strip_color","set":0}],"else":[{"output":"strip_color","off":true}]})",o));
  assert(o.any&&o.conds==1&&o.cond[0].sig==Sig::Knob&&o.cond[0].op==Op::Above&&o.cond[0].value==50&&o.yes==1&&o.then[0].out==Out::StripColor&&o.then[0].kind==SeqKind::Set&&o.then[0].value==0&&o.no==1&&o.otherwise[0].kind==SeqKind::Off);
  assert(parseOnce(R"({"type":"lab.once","read":["temperature","humidity"]})",o));assert(o.reads==2&&o.read[1]==Sig::Humidity&&!o.conds&&!o.yes&&o.usable());
  // Just an action, every part it may touch, and numbers held to what the part can do.
  assert(parseOnce(R"({"do":[{"output":"servo","set":999},{"output":"speaker","set":10},{"output":"led","off":true}]})",o));assert(o.yes==3&&o.then[0].value==180&&o.then[1].value==200&&o.then[2].kind==SeqKind::Off);
  assert(parseOnce(R"({"do":[{"output":"strip_bright","set":50},{"output":"strip_length","set":10}]})",o));
  assert(parseOnce(R"({"any":false,"when":[{"sensor":"motion","test":"on"},{"sensor":"light","test":"under","value":20}]})",o));assert(!o.any&&o.conds==2);
  // Numbers mean what they say, or the check is refused: nothing will look at it before it runs.
  assert(parseOnce(R"({"when":[{"sensor":"temperature","test":"over","value":22.5}],"do":[{"output":"servo","set":90.5},{"output":"strip_color","set":120.0}]})",o));
  assert(o.cond[0].value==22&&o.then[0].value==91&&o.then[1].value==120);  // Over 22.5 means 23 and up, the same as over 22.
  assert(parseOnce(R"({"when":[{"sensor":"distance","test":"over","value":200}]})",o));assert(o.cond[0].value==200);  // A reading can pass a rule's range.
  assert(parseOnce(R"({"when":[{"sensor":"tilt","test":"under","value":-20.5}]})",o));assert(o.cond[0].value==-20);  // Under -20.5 means -21 and down.
  assert(parseOnce(R"({"when":[{"sensor":"humidity","test":"under","value":22.3},{"sensor":"knob","test":"over","value":22.7}]})",o));assert(o.cond[0].value==23&&o.cond[1].value==22);
  Inputs whole;whole.set(Sig::Humidity,22,0);whole.set(Sig::Knob,23,0);o.any=false;assert(onceHolds(o,whole));
  // What the bridge sends when a field has nothing in it.
  assert(parseOnce(R"({"type":"lab.once","any":true,"read":["knob"],"when":[],"do":[],"else":[]})",o));assert(o.reads==1&&!o.conds&&!o.yes&&!o.no);
  // Anything else is refused whole, and leaves what was there alone.
  o=Once{};o.reads=2;o.read[1]=Sig::Tilt;
  for(const char* refused:{
      R"({})",R"({"any":true})",R"({"read":[]})",
      R"({"do":[{"output":"relay_1","set":1}]})",R"({"do":[{"output":"relay_2","off":true}]})",R"({"do":[{"output":"lamp_power","set":1}]})",R"({"do":[{"output":"lamp_bright","set":50}]})",
      R"({"do":[{"output":"ac_tcl","set":1}]})",R"({"do":[{"output":"ac_daikin","set":1}]})",R"({"do":[{"output":"say","text":"hi"}]})",R"({"do":[{"output":"sequence","play":"Hello"}]})",
      R"({"when":[{"sensor":"knob","test":"over","value":50}],"do":[{"output":"strip_color","set":0}],"else":[{"output":"relay_1","set":1}]})",
      R"({"do":[{"wait_ms":1000}]})",R"({"do":[{"output":"servo","off":true}]})",R"({"do":[{"output":"servo","follow":"knob"}]})",R"({"do":[{"output":"servo"}]})",R"({"do":[{"output":"nothing","set":1}]})",
      R"({"when":[{"sensor":"knob","test":"changes"}]})",R"({"when":[{"sensor":"spoken","test":"on"}]})",R"({"when":[{"sensor":"nothing","test":"over","value":1}]})",R"({"when":[{"sensor":"knob","test":"on"}]})",
      R"({"read":["spoken"]})",R"({"read":["nothing"]})",R"({"read":[1]})",R"({"read":["knob","light","tilt","motion"]})",R"({"read":"knob"})",
      R"({"else":[{"output":"led","set":1}]})",R"({"read":["knob"],"else":[{"output":"led","set":1}]})",
      R"({"do":{"output":"led","set":1}})",R"({"when":{"sensor":"knob","test":"over","value":1}})",R"({"read":["knob"],"do":"led"})",
      R"({"do":[{"output":"led","set":1},{"output":"led","set":2},{"output":"led","set":3},{"output":"led","set":4}]})",
      R"({"when":[{"sensor":"knob","test":"over","value":"50"}]})",R"({"when":[{"sensor":"knob","test":"over"}]})",R"({"when":[{"sensor":"knob","test":"over","value":99999}]})",
      R"({"do":[{"output":"servo","set":"90"}]})",R"({"do":[{"output":"servo","set":true}]})",R"({"do":[{"output":"led","set":null}]})",R"({"do":[{"output":"led","off":"yes"}]})",
      R"({"do":[{"output":"led","set":1,"wait_ms":100}]})",R"({"do":[7]})",R"({"when":[7]})",R"({"when":[{"sensor":"motion","test":"over","value":0}]})",
      R"({"when":[{"sensor":"knob","test":"over","value":1},{"sensor":"knob","test":"over","value":2},{"sensor":"knob","test":"over","value":3},{"sensor":"knob","test":"over","value":4}]})"})
    {assert(!parseOnce(refused,o));assert(o.reads==2&&o.read[1]==Sig::Tilt);}
}
void testSequenceJson(){
  // The example in the bridge's prompt, and it runs the way it reads.
  Sequence s;assert(parseSeq(R"({"type":"lab.sequence","name":"Door wave","loop":false,"steps":[{"output":"strip_color","set":120},{"wait_ms":1000},{"output":"servo","set":90},{"wait_ms":2000},{"output":"servo","set":0},{"output":"strip_color","off":true}]})",s));
  assert(!strcmp(s.name,"Door wave")&&!s.loop&&s.count==6);
  assert(s.steps[0].kind==SeqKind::Set&&s.steps[0].out==Out::StripColor&&s.steps[0].value==120);
  assert(s.steps[1].kind==SeqKind::Wait&&s.steps[1].value==10&&s.steps[3].value==20);
  assert(s.steps[5].kind==SeqKind::Off&&s.steps[5].out==Out::StripColor);
  for(uint8_t i=0;i<s.count;++i)assert(stepAllowed(s.steps[i]));
  // On or off outputs read the way the screens write them, so "off" is a value and not a release.
  assert(parseSeq(R"({"name":"Lamp","loop":true,"steps":[{"output":"relay_1","set":1},{"wait_ms":250},{"output":"relay_1","off":true},{"output":"ac_daikin","off":true}]})",s));
  assert(s.loop&&s.steps[0].value==1&&s.steps[2].kind==SeqKind::Set&&s.steps[2].value==0&&s.steps[3].kind==SeqKind::Set&&s.steps[3].out==Out::AcDaikin);
  assert(s.steps[1].value==3);  // Waits round to the tenth.
  // Numbers are held to what the part can do, and a wait stays a wait.
  assert(parseSeq(R"({"name":"Far","steps":[{"output":"servo","set":999},{"output":"speaker","set":10},{"wait_ms":1},{"wait_ms":0},{"wait_ms":99999999}]})",s));
  assert(s.steps[0].value==180&&s.steps[1].value==200&&s.steps[2].value==1&&s.steps[3].value==1&&s.steps[4].value==kMaxWaitTenths);
  // A name is cleaned like a rule's, and an empty one gets something to show.
  assert(parseSeq(R"({"name":"<b>Wave!</b> and a very long name","steps":[{"wait_ms":100}]})",s));assert(strlen(s.name)<=kNameChars&&!strchr(s.name,'<'));
  assert(parseSeq(R"({"steps":[{"wait_ms":100}]})",s));assert(!strcmp(s.name,"New sequence"));
  // Anything it can't run refuses all of it, and leaves what was there alone.
  strcpy(s.name,"Kept");
  for(const char* bad:{R"({})",R"({"name":"x","steps":[]})",R"({"name":"x","steps":"wave"})",R"({"name":"x","steps":[{}]})",
      R"({"name":"x","steps":[{"output":"ghost","set":1}]})",R"({"name":"x","steps":[{"output":"servo"}]})",
      R"({"name":"x","steps":[{"output":"servo","off":true}]})",R"({"name":"x","steps":[{"output":"say","set":1}]})",
      R"({"name":"x","steps":[{"output":"sequence","set":0}]})",R"({"name":"x","steps":[{"wait_ms":100},{"output":"say","text":"hi"}]})"}){assert(!parseSeq(bad,s));}
  assert(!strcmp(s.name,"Kept"));
  // Seventeen steps is one too many, and sixteen isn't.
  std::string steps="{\"name\":\"Full\",\"steps\":[";for(int i=0;i<kMaxSeqSteps;++i)steps+=i?",{\"wait_ms\":100}":"{\"wait_ms\":100}";
  assert(parseSeq((steps+"]}").c_str(),s)&&s.count==kMaxSeqSteps);
  DynamicJsonDocument big(4096);assert(!deserializeJson(big,steps+",{\"wait_ms\":100}]}"));assert(!sequenceFromJson(big.as<JsonObjectConst>(),s));
  // The device reads a reply's actions with 2048 bytes, so the longest sequence has to fit, with the
  // longest rule beside it in case Claude sends both.
  const char* worst=R"([{"type":"lab.sequence","name":"Fifteen letters","loop":true,"replaces":"Fifteen letter2","steps":[{"output":"strip_bright","set":100},{"wait_ms":600000},{"output":"strip_length","set":30},{"wait_ms":600000},{"output":"strip_color","set":359},{"wait_ms":600000},{"output":"lamp_bright","set":100},{"wait_ms":600000},{"output":"lamp_power","set":1},{"wait_ms":600000},{"output":"strip_color","off":true},{"wait_ms":600000},{"output":"ac_daikin","set":1},{"wait_ms":600000},{"output":"speaker","set":2000},{"output":"strip_bright","off":true}]},{"type":"lab.rule","name":"Fifteen letters","replaces":"Fifteen letter2","any":false,"hold_ms":30000,"when":[{"sensor":"temperature","test":"over","value":45},{"sensor":"stick_button","test":"off"},{"sensor":"satellites","test":"changes"}],"do":[{"output":"strip_bright","oppose":"stick_x"},{"output":"lamp_power","set":1},{"output":"strip_length","follow":"temperature"}]}])";
  // Slots are 16 bytes on the device and larger here, so this works out what the device would use.
  DynamicJsonDocument reply(8192);assert(!deserializeJson(reply,std::string(worst)));assert(sequenceFromJson(reply[0].as<JsonObjectConst>(),s)&&s.count==16);
  const size_t onDevice=reply.memoryUsage()-slotsIn(reply.as<JsonVariantConst>())*(JSON_ARRAY_SIZE(1)-16);
  assert(onDevice<2048);
  Rule r;assert(ruleFromJson(reply[1].as<JsonObjectConst>(),r,gSeqs,gSeqCount)&&r.conds==3&&r.actions==3);
  // What the chat sees reads back as what's saved: every line a rule or step can be.
  { strcpy(gSeqs[0].name,"Hello");gSeqCount=1;Rule rule;
    assert(parse(R"({"name":"Door","any":false,"hold_ms":1500,"when":[{"sensor":"distance","test":"under","value":30},{"sensor":"motion","test":"on"},{"sensor":"knob","test":"changes"}],"do":[{"output":"servo","follow":"knob"},{"output":"say","text":"hi there, friend"},{"output":"sequence","play":"Hello"}]})",rule));
    rule.on=false;char text[400];Text out(text,sizeof(text));ruleLine(rule,out,gSeqs,gSeqCount);
    assert(!strcmp(text,"Door (switched off): all distance under 30, motion on, knob changes; do servo follow knob, say text \"hi there, friend\", sequence play Hello; hold 1500"));
    Rule other=recipeRule(5);other.action[1]=oppose(Out::Servo,Sig::Light);char more[400];Text line(more,sizeof(more));ruleLine(other,line,gSeqs,gSeqCount);assert(strstr(more,"oppose light")&&strstr(more,": any ")!=nullptr);
    Sequence seq;assert(parseSeq(R"({"name":"Wave","loop":true,"steps":[{"output":"strip_color","set":120},{"wait_ms":1500},{"output":"strip_bright","off":true}]})",seq));
    char third[200];Text t(third,sizeof(third));sequenceLine(seq,t);assert(!strcmp(third,"Wave (loops): strip_color set 120, wait_ms 1500, strip_bright off"));
    gSeqCount=0;}
}
void testJson(){
  Rule r;
  assert(parse(R"({"type":"lab.rule","name":"Wave red","any":true,"hold_ms":2000,"when":[{"sensor":"distance","test":"under","value":30}],"do":[{"output":"strip_color","set":0},{"output":"servo","follow":"knob"}]})",r));
  assert(!std::strcmp(r.name,"Wave red")&&r.any&&r.holdMs==2000&&r.conds==1&&r.cond[0].value==30&&r.actions==2);
  assert(r.action[0].out==Out::StripColor&&r.action[0].how==How::Set&&r.action[1].how==How::Follow&&r.action[1].source==Sig::Knob);
  assert(parse(R"({"any":false,"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"servo","oppose":"knob"}]})",r));
  assert(!r.any&&!std::strcmp(r.name,"Spoken rule")&&r.holdMs==2000&&r.action[0].how==How::FollowBack);
  // A phrase comes back the same way, in place of a value.
  { Rule r;assert(parse(R"({"name":"Hello","when":[{"sensor":"motion","test":"on"}],"do":[{"output":"led","set":30},{"output":"say","text":"welcome back"}]})",r));
    assert(r.actions==2&&r.action[1].out==Out::Say&&!strcmp(r.say,"welcome back"));
    char text[600];Text out(text,sizeof(text));ruleJson(r,out);Rule back;assert(parse(text,back)&&!strcmp(back.say,"welcome back")&&back.action[1].out==Out::Say);
    assert(strstr(text,"\"text\":\"welcome back\""));
    Rule none;assert(!parse(R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"say","text":"  "}]})",none));}
  // A sequence goes by name, because that's all Claude knows it by, and only a saved one gets through.
  { strcpy(gSeqs[0].name,"Hello");strcpy(gSeqs[1].name,"Door wave");gSeqCount=2;Rule r;
    assert(parse(R"({"name":"Door","when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","play":"door wave"}]})",r));
    assert(r.actions==1&&r.action[0].out==Out::Sequence&&r.action[0].how==How::Set&&r.action[0].value==1);
    char text[600];Text out(text,sizeof(text));ruleJson(r,out,gSeqs,gSeqCount);assert(strstr(text,"{\"output\":\"sequence\",\"play\":\"Door wave\"}"));Rule back;assert(parse(text,back)&&back.action[0].value==1);
    for(const char* bad:{R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","play":"Door"}]})",R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","play":"Door wave again"}]})",
        R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","play":""}]})",R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","set":1}]})",R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","follow":"knob"}]})"}){Rule x;assert(!parse(bad,x));}
    gSeqCount=0;Rule x;assert(!parse(R"({"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"sequence","play":"Hello"}]})",x));}
  // A rule written out reads back the same, because Claude gets it back to change.
  { char text[600]; Text out(text,sizeof(text)); Rule start=recipeRule(5); start.any=false; start.action[1]=oppose(Out::Servo,Sig::Light); ruleJson(start,out);
    Rule back; assert(parse(text,back)); assert(!strcmp(back.name,start.name)&&back.any==start.any&&back.holdMs==start.holdMs&&back.conds==start.conds&&back.actions==start.actions);
    assert(back.cond[0].sig==start.cond[0].sig&&back.cond[0].op==start.cond[0].op&&back.cond[0].value==start.cond[0].value);
    assert(back.action[0].out==start.action[0].out&&back.action[0].value==start.action[0].value&&back.action[1].how==How::FollowBack&&back.action[1].source==Sig::Light);
    for(int i=0;i<kRecipeCount;++i){char more[600];Text t(more,sizeof(more));ruleJson(recipeRule(i),t);Rule again;assert(parse(more,again));}
    Rule live;live.conds=3;live.cond[0]=Cond{Sig::Knob,Op::Changes,0};setOnOff(live.cond[1],false);live.cond[1].sig=Sig::Motion;live.cond[2]=Cond{Sig::Humidity,Op::Above,60};live.actions=1;live.action[0]=setTo(Out::Relay2,1);
    char third[600];Text t(third,sizeof(third));ruleJson(live,t);Rule again;assert(parse(third,again));assert(again.cond[0].op==Op::Changes&&again.cond[1].op==Op::Below&&again.cond[1].value==1&&again.cond[2].value==60&&again.action[0].value==1);
      // Running out of room leaves a shorter text, never an overrun.
    char tiny[8];Text cut(tiny,sizeof(tiny));ruleJson(start,cut);assert(strlen(tiny)==7&&cut.length()==7);
    // It says how much is left, so a long list can give way from the end that matters least.
    char some[8];Text left(some,sizeof(some));assert(left.room()==7);left.add("abc");assert(left.room()==4);left.add("0123456789");assert(left.room()==0&&left.length()==7);Text one(some,1);assert(one.room()==0);Text none(some,0);assert(none.room()==0);}
  // The largest rule fits the device's buffer.
  assert(parse(R"({"type":"lab.rule","name":"Fifteen letters","any":false,"hold_ms":30000,"when":[{"sensor":"temperature","test":"over","value":45},{"sensor":"stick_button","test":"off"},{"sensor":"satellites","test":"changes"}],"do":[{"output":"strip_bright","oppose":"stick_x"},{"output":"lamp_power","set":1},{"output":"strip_length","follow":"temperature"}]})",r));
  assert(r.conds==3&&r.actions==3);
  for(const char* bad:{R"({})",R"({"when":[],"do":[]})",R"({"when":[{"sensor":"knob","test":"changes"}]})",R"({"do":[{"output":"servo","set":1}]})",
      R"({"when":[{"sensor":"ghost","test":"changes"}],"do":[{"output":"servo","set":1}]})",R"({"when":[{"sensor":"knob","test":"changes"}],"do":[{"output":"servo"}]})",
      R"({"when":[{"sensor":"knob","test":"changes"}],"do":[{"output":"relay_1","follow":"knob"}]})",R"({"when":"knob","do":"servo"})"}){Rule x;assert(!parse(bad,x));}
}
'''
if (library / 'ArduinoJson.h').exists():
    code = code.replace('void testHub(){', json_test + 'void testHub(){', 1).replace('JSON_TEST', 'testJson();testSequenceJson();testOnceJson();')
else:
    code = code.replace('JSON_TEST', '')
with tempfile.TemporaryDirectory(prefix='s1-lab-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), '-I', str(library), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
