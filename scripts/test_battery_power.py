#!/usr/bin/env python3
"""Run the real power tracker through what a Cardputer's battery reading actually does.

The noise and the charging slope are measured on the device. The steps at plugging in and unplugging
are worked out from the schematic, so these cases also try steps smaller than expected.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'

code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include "battery_power.h"
using namespace battery_power;

const char* gCase="";
struct Sim {
 Tracker t;uint32_t now;uint32_t seed=7;
 explicit Sim(uint32_t start=1000):now(start){}
 // Readings wander about 6 mV either way and come in even numbers.
 int noise(){seed=seed*1103515245u+12345u;return int((seed>>16)%7)*2-6;}
 int spin(){seed=seed*1103515245u+12345u;return int((seed>>16)&0x7fff);}
 void step(int mv,bool computer){now+=1000;t.update(now,int16_t(mv+noise()),computer);}
 void hold(int seconds,int mv,bool computer){for(int i=0;i<seconds;++i)step(mv,computer);}
 void walk(int seconds,std::function<int(int)> mv,bool computer){for(int i=0;i<seconds;++i)step(mv(i),computer);}
 // Seconds until the state arrives, or -1.
 int until(State want,int limit,std::function<int(int)> mv,bool computer){
  for(int i=0;i<limit;++i){step(mv(i),computer);if(t.state()==want)return i+1;}
  return -1;
 }
 void always(State want,int seconds,std::function<int(int)> mv,bool computer){
  for(int i=0;i<seconds;++i){
   step(mv(i),computer);
   if(t.state()!=want)std::fprintf(stderr,"%s: at %ds wanted %s, got %s, reading %d\n",gCase,i+1,name(want),name(t.state()),mv(i));
   assert(t.state()==want);
  }
 }
 // Boots and sits on battery long enough for its first readings to settle.
 void settle(int mv){always(State::Battery,60,[=](int){return mv;},false);}
};
std::function<int(int)> rising(int base,double mvPerMinute){return [=](int s){return base+int(s*mvPerMinute/60.0);};}
std::function<int(int)> flat(int mv){return [=](int){return mv;};}

// Radio bursts land on the reading as dips.
std::function<int(int)> onBattery(Sim& sim,int base,double mvPerMinute){
 return [=,&sim](int s){return base-int(s*mvPerMinute/60.0)-(sim.spin()%5==0?40:0);};
}

void batteryNeverClaimsPower(){
 Sim sim;
 sim.always(State::Battery,40,flat(3790),false);  // Boot's load.
 sim.always(State::Battery,8*3600,onBattery(sim,3880,0.4),false);
 Sim idle;idle.always(State::Battery,8*3600,[](int s){return 3880-s/120;},false);
}

void loadComingAndGoing(){
 Sim sim;sim.settle(3900);
 for(int i=0;i<40;++i){
  sim.always(State::Battery,12,flat(3850),false);  // Speech or a download.
  sim.always(State::Battery,30,flat(3900),false);  // And it ends, 50 mV back up.
 }
}

void wallCharger(){
 for(int stepMv:{70,100,140}){
  Sim sim;sim.settle(3700);
  const int on=sim.until(State::Charging,6,rising(3700+stepMv,3.5),false);
  assert(on>=1&&on<=4);
  assert(sim.t.noteVisible(sim.now));
  sim.always(State::Charging,1800,rising(3700+stepMv,3.5),false);
  const int mv=3700+stepMv+105;
  const int off=sim.until(State::Battery,6,flat(mv-stepMv),false);
  assert(off>=1&&off<=4);
  assert(sim.t.noteVisible(sim.now));  // Says so for a moment.
  sim.hold(kUnpluggedNoteMs/1000+1,mv-stepMv,false);
  assert(!sim.t.noteVisible(sim.now));
 }
}

void strayReadingAfterPluggingIn(){
 // The step leaves its own marks behind, and they mustn't count towards load on the battery.
 Sim sim;sim.settle(3700);
 assert(sim.until(State::Charging,6,flat(3800),false)>0);
 sim.always(State::Charging,2,flat(3800),false);
 sim.always(State::Charging,1,flat(3760),false);
 sim.always(State::Charging,600,rising(3800,3.5),false);
}

void computerIsCertain(){
 Sim sim;sim.settle(3700);
 // No voltage step at all, and it still knows at once.
 assert(sim.until(State::Charging,1,flat(3700),true)==1);
 sim.always(State::Charging,1200,rising(3700,3.5),true);
 const int mv=3700+70;
 sim.always(State::Charging,3,flat(mv),true);
 assert(sim.until(State::Battery,6,flat(mv-100),false)<=4);
 assert(!sim.t.computerSeen(sim.now+kComputerGoneMs+1));
}

void computerGoingToSleepKeepsCharging(){
 Sim sim;sim.settle(3700);
 sim.hold(5,3800,true);
 sim.always(State::Charging,3600,rising(3800,3.5),false);
}

void oneMissedFrameReadChangesNothing(){
 Sim sim;sim.hold(5,3800,true);
 for(int i=0;i<3600;++i){sim.step(3800+i/20,i%100!=0);assert(sim.t.state()==State::Charging);}
}

void chargingToFull(bool computer){
 Sim sim;sim.settle(3900);
 sim.hold(6,4000,computer);assert(sim.t.state()==State::Charging);
 sim.always(State::Charging,3430,rising(4000,3.5),computer);  // Up to the charger's 4200.
 const int after=sim.until(State::Full,int(kFullHoldMs/1000)+120,flat(4200),computer);
 assert(after>=int(kFullHoldMs/1000)-120&&after<=int(kFullHoldMs/1000)+120);
 // A finished cell relaxes a little and stays Full.
 sim.always(State::Full,3600,[](int s){return 4200-s/120;},computer);
 sim.always(State::Full,600,flat(4170),computer);
 assert(sim.until(State::Battery,6,flat(4135),false)<=4);
}

void fullThenRecharge(){
 Sim sim;sim.hold(3,4200,true);
 assert(sim.until(State::Full,int(kFullHoldMs/1000)+5,flat(4200),true)>0);
 sim.always(State::Full,3600,[](int s){return 4200-s/40;},true);
 assert(sim.until(State::Charging,2000,[](int s){return 4110-s/2;},true)>0);
 assert(sim.t.state()==State::Charging);
 // Topping up from there with the computer asleep is still charging, not a fall from Full's peak.
 sim.always(State::Charging,120,flat(3975),true);
 sim.always(State::Charging,900,rising(3975,3.5),false);
}

void smallestSagsFromFull(){
 // A full battery barely sags when its charger goes: no charge current to lose, only the load.
 Sim wall;wall.hold(3,4200,false);
 assert(wall.until(State::Charging,int(kSettleMs/1000)+6,flat(4200),false)>0);
 assert(wall.until(State::Full,int(kFullHoldMs/1000)+60,flat(4200),false)>0);
 assert(wall.until(State::Battery,6,flat(4165),false)>0);
 // With frames just gone, a smaller one is enough.
 Sim usb;usb.hold(3,4200,true);
 assert(usb.until(State::Full,int(kFullHoldMs/1000)+60,flat(4200),true)>0);
 assert(usb.until(State::Battery,6,flat(4182),false)>0);
 // The same small dip with no reason to expect it isn't a sag.
 Sim held;held.hold(3,4200,true);
 assert(held.until(State::Full,int(kFullHoldMs/1000)+60,flat(4200),true)>0);
 held.always(State::Full,600,flat(4182),true);
}

void readingHighOnArrival(){
 // Booted on a charger with the cell already at the top: no step to see.
 Sim sim;
 assert(sim.until(State::Charging,int(kSettleMs/1000)+6,flat(4200),false)>0);
 assert(sim.until(State::Full,int(kFullHoldMs/1000)+60,flat(4200),false)>0);
}

void powerSwitchOff(){
 // The charger has no cell to hold up, so its output saws between its two thresholds.
 Sim sim;sim.settle(3800);
 std::function<int(int)> saw=[&sim](int){return 4050+sim.spin()%150;};
 const int on=sim.until(State::NotCharging,40,saw,true);
 assert(on>0&&on<=30);
 sim.always(State::NotCharging,3*3600,saw,true);  // Never Full, however long it runs.
 // Switched on: back to the real battery, charging.
 assert(sim.until(State::Charging,30,rising(3800,3.5),true)>0);
 sim.always(State::Charging,1200,rising(3830,3.5),true);
 // Saw with no computer can't be told from a loaded battery, so it says nothing.
 Sim wall;wall.settle(3800);wall.always(State::Battery,600,flat(3790),false);
}

void noBattery(){
 Sim sim;sim.hold(5,1800,true);
 assert(sim.t.state()==State::NotCharging);
}

void bootingOnAWallCharger(){
 Sim sim;
 const int fast=sim.until(State::Charging,31*60,rising(3780,3.5),false);
 assert(fast>0&&fast<=31*60);
 Sim slow;
 assert(slow.until(State::Charging,41*60,rising(3780,1.2),false)>0);
}

void wrongGuessesAreTakenBack(){
 // A big load ends and looks like a charger, but the radio keeps landing on the reading.
 Sim sim;sim.settle(3820);
 assert(sim.until(State::Charging,6,flat(3890),false)>0);
 assert(sim.until(State::Battery,60,[](int s){return 3890-(s%5==0?45:0);},false)>0);
 // Same with a quiet device: the slow fall gives it away, and a charger never lets it fall.
 Sim quiet;quiet.settle(3820);
 assert(quiet.until(State::Charging,6,flat(3890),false)>0);
 assert(quiet.until(State::Battery,45*60,[](int s){return 3890-s/60;},false)>0);
 // Going nowhere for half an hour with no computer to vouch for it.
 Sim still;still.settle(3820);
 assert(still.until(State::Charging,6,flat(3890),false)>0);
 assert(still.until(State::Battery,40*60,flat(3890),false)>0);
}

void stalledCharger(){
 Sim sim;sim.settle(3800);
 assert(sim.until(State::Charging,2,flat(3800),true)>0);
 const int stopped=sim.until(State::NotCharging,40*60,flat(3800),true);
 assert(stopped>=25*60&&stopped<=40*60);
 // The slowest stretch of a real charge never gets that far.
 Sim plateau;plateau.settle(3700);
 plateau.hold(3,3700,true);
 plateau.always(State::Charging,3*3600,rising(3700,0.5),true);
}

void gapsInTheReadings(){
 // NES holds the loop. A different reading afterwards isn't a step.
 Sim sim;sim.settle(3800);
 sim.now+=5*60*1000;
 sim.always(State::Battery,60,flat(3870),false);
 // Unplugged during the gap.
 Sim out;out.settle(3800);out.hold(5,3900,true);
 out.now+=5*60*1000;
 assert(out.until(State::Battery,10,flat(3800),false)>0);
}

void wrappingClock(){
 Sim sim(0xFFFFFFFFu-90000u);
 sim.always(State::Battery,60,flat(3800),false);
 assert(sim.until(State::Charging,6,rising(3900,3.5),false)>0);
 sim.always(State::Charging,120,rising(3905,3.5),false);
 assert(sim.now<1000000u);
 assert(sim.until(State::Battery,6,flat(3810),false)>0);
}

void badReadingsAreIgnored(){
 Sim sim;
 for(int mv:{0,-1,-32000}){assert(!sim.t.update(sim.now+=1000,int16_t(mv),true));assert(sim.t.state()==State::Battery);}
 // Forced reads can arrive close together, and count once.
 sim.settle(3800);assert(!sim.t.update(sim.now+100,3900,true));assert(sim.t.state()==State::Battery);
}

void quickReplug(){
 Sim sim;sim.settle(3800);
 assert(sim.until(State::Charging,6,flat(3900),false)>0);
 sim.always(State::Charging,60,flat(3900),false);
 assert(sim.until(State::Battery,6,flat(3800),false)>0);
 const int back=sim.until(State::Charging,6,flat(3900),false);
 assert(back>0&&back<=4);
}

void percentShown(){
 assert(shownLevel(100,State::Charging)==99&&shownLevel(100,State::Full)==100);
 assert(shownLevel(72,State::Full)==100&&shownLevel(-1,State::Full)==100);
 assert(shownLevel(100,State::Battery)==100&&shownLevel(100,State::NotCharging)==100);
 assert(shownLevel(150,State::Battery)==100&&shownLevel(-1,State::Charging)==-1);
 assert(shownLevel(71,State::Charging)==71);
 assert(look(State::NotCharging).hint&&!look(State::Charging).hint);
 assert(look(State::Charging).moving&&!look(State::Full).moving&&!look(State::NotCharging).moving&&!look(State::Battery).moving);
}

#define RUN(c,...) gCase=#c;c(__VA_ARGS__)
int main(){
 RUN(batteryNeverClaimsPower);RUN(loadComingAndGoing);RUN(wallCharger);RUN(computerIsCertain);
 RUN(computerGoingToSleepKeepsCharging);RUN(oneMissedFrameReadChangesNothing);
 RUN(chargingToFull,false);RUN(chargingToFull,true);RUN(fullThenRecharge);RUN(smallestSagsFromFull);RUN(strayReadingAfterPluggingIn);RUN(readingHighOnArrival);
 RUN(powerSwitchOff);RUN(noBattery);RUN(bootingOnAWallCharger);RUN(wrongGuessesAreTakenBack);RUN(stalledCharger);
 RUN(gapsInTheReadings);RUN(wrappingClock);RUN(badReadingsAreIgnored);RUN(quickReplug);RUN(percentShown);
}
'''

with tempfile.TemporaryDirectory(prefix='s1-battery-power-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-I', str(src), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

# The real firmware has to feed it and show it.
power = (parts / '037_main.cpp.inc').read_text()
home = (parts / '059_main.cpp.inc').read_text()
assert 'gBatteryPower.update(now, voltageMv, computer)' in power
assert 'USB_SERIAL_JTAG.fram_num.sof_frame_index' in power
assert 'drawHomeChargeCard(gBatterySnapshot.level, gBatteryPower.state(), nowMs)' in home
assert 'drawHomeBatteryStatus(gBatterySnapshot.level, gBatteryPower.state(), nowMs)' in home
print('PASS power tracker: computer certain, chargers judged from the voltage, wrong guesses taken back, switch off seen, full only when flat')
