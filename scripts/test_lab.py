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
        ('023_main.cpp.inc', 'lab::ruleFromJson(action, rule)'),
        ('063_main.cpp.inc', 'return labAllowsNetwork();'),
        ('004_main.cpp.inc', '{"Devices", UiMode::LabDevices}'),
        ('004_main.cpp.inc', '{"Rules", UiMode::LabRules}'),
        ('022_main.cpp.inc', 'mode == "lab"'),
        ('024_main.cpp.inc', 'if (modeChanged && labMode(mode)) beginLab();'),
        ('024_main.cpp.inc', 'if (modeChanged && labMode(gUiMode)) stopLab();'),
        ('043_main.cpp.inc', 'cmd == "/lab"'),
        ('043_main.cpp.inc', 'cmd == "/rules"'),
        ('048_main.cpp.inc', 'handleLabKey(key, keys)'),
        ('048_main.cpp.inc', 'routeLabEscape()'),
        ('049_main.cpp.inc', 'routeLabEscape()'),
        ('061_main.cpp.inc', 'renderLabUi();'),
        ('061_main.cpp.inc', 'serviceLab();'),
        ('063_main.cpp.inc', 'case UiMode::LabRules:')]:
    assert text in (parts / file).read_text(), (file, text)
for part in ('081', '082'):
    assert f'main_parts/{part}_main.cpp.inc' in (src / 'main.cpp').read_text()

# The Grove port changes hands the same way it does for the games.
loop = (parts / '061_main.cpp.inc').read_text()
assert loop.index('serviceNfcNotes();') < loop.index('serviceLab();')
change = (parts / '024_main.cpp.inc').read_text()
assert change.index('stopLab();') < change.index('gUiMode = mode;')

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
   uint8_t old[kFileBytes];std::memcpy(old,c,kOldHeaderBytes);std::memcpy(old+kOldHeaderBytes,c+kHeaderBytes,kRuleBytes);old[3]=1;
   const size_t oldSize=n-2;const uint16_t k=checksum(old,oldSize-2);old[oldSize-2]=uint8_t(k&255);old[oldSize-1]=uint8_t(k>>8);
   Book o;assert(decode(old,oldSize,o)&&o.count==1&&!std::strcmp(o.rules[0].name,"Wave lights")&&o.capPercent==50&&!o.direct.any());}
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

int main(){
  testHub();testGps();testRanges();testInputs();testRules();testWiring();testCloseness();testStrip();testRelays();testDirect();testPractice();testRecipes();testRemotes();testDrafts();testSaving();testTables();JSON_TEST
  std::puts("PASS Lab: hub commands, GPS lines, ranges, change detection, rules with hold and priority, wiring, the strip cap, the relay guard, and saved files that refuse every kind of damage");
}
'''
# The real JSON library, once PlatformIO has fetched it. CI's firmware job has it, and the logic
# above doesn't depend on it.
library = root / 'bots/CardputerADV/.pio/libdeps/cardputer_adv/ArduinoJson/src'
json_test = r'''
#include "lab_json.h"
bool parse(const char* text, Rule& out){StaticJsonDocument<2048> doc;assert(!deserializeJson(doc,text));return ruleFromJson(doc.as<JsonObjectConst>(),out);}
void testJson(){
  Rule r;
  assert(parse(R"({"type":"lab.rule","name":"Wave red","any":true,"hold_ms":2000,"when":[{"sensor":"distance","test":"under","value":30}],"do":[{"output":"strip_color","set":0},{"output":"servo","follow":"knob"}]})",r));
  assert(!std::strcmp(r.name,"Wave red")&&r.any&&r.holdMs==2000&&r.conds==1&&r.cond[0].value==30&&r.actions==2);
  assert(r.action[0].out==Out::StripColor&&r.action[0].how==How::Set&&r.action[1].how==How::Follow&&r.action[1].source==Sig::Knob);
  assert(parse(R"({"any":false,"when":[{"sensor":"motion","test":"on"}],"do":[{"output":"servo","oppose":"knob"}]})",r));
  assert(!r.any&&!std::strcmp(r.name,"Spoken rule")&&r.holdMs==2000&&r.action[0].how==How::FollowBack);
  // The largest rule fits the device's buffer.
  assert(parse(R"({"type":"lab.rule","name":"Fifteen letters","any":false,"hold_ms":30000,"when":[{"sensor":"temperature","test":"over","value":45},{"sensor":"stick_button","test":"off"},{"sensor":"satellites","test":"changes"}],"do":[{"output":"strip_bright","oppose":"stick_x"},{"output":"lamp_power","set":1},{"output":"strip_length","follow":"temperature"}]})",r));
  assert(r.conds==3&&r.actions==3);
  for(const char* bad:{R"({})",R"({"when":[],"do":[]})",R"({"when":[{"sensor":"knob","test":"changes"}]})",R"({"do":[{"output":"servo","set":1}]})",
      R"({"when":[{"sensor":"ghost","test":"changes"}],"do":[{"output":"servo","set":1}]})",R"({"when":[{"sensor":"knob","test":"changes"}],"do":[{"output":"servo"}]})",
      R"({"when":[{"sensor":"knob","test":"changes"}],"do":[{"output":"relay_1","follow":"knob"}]})",R"({"when":"knob","do":"servo"})"}){Rule x;assert(!parse(bad,x));}
}
'''
if (library / 'ArduinoJson.h').exists():
    code = code.replace('void testHub(){', json_test + 'void testHub(){', 1).replace('JSON_TEST', 'testJson();')
else:
    code = code.replace('JSON_TEST', '')
with tempfile.TemporaryDirectory(prefix='s1-lab-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(src), '-I', str(library), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
