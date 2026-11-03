#!/usr/bin/env python3
"""Check the actual Cap radio setup, without transmitting or touching hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
RADIO=r'''
#pragma once
#include <cassert>
#include <cstdint>
constexpr int RADIOLIB_ERR_UNKNOWN=-1,RADIOLIB_SX126X_SYNC_WORD_PRIVATE=0x12;
inline int step=0,failure=0,sleeps=0,meshBegins=0;inline float wanted=0;inline int wantedRegion=0;
inline int result(){return ++step==failure ? -19 : 0;}
struct SPIClass{};
struct Module {
 struct {int timeout=1000;}spiConfig;SPIClass* spi;
 Module(int cs,int irq,int rst,int busy,SPIClass& bus):spi(&bus){assert(cs==5&&irq==4&&rst==3&&busy==6);}
};
struct SX1262 {
 Module* module;explicit SX1262(Module*m):module(m){}
 int begin(float f,float bw,int sf,int cr,int sync,int power,int preamble,float tcxo) {
  assert(f==wanted&&bw==250&&sf==11&&cr==5&&sync==0x12&&power==10&&preamble==16&&tcxo==1.8f);return result();
 }
 int setDio2AsRfSwitch(bool b){assert(b);return result();}
 int setCurrentLimit(float n){assert(n==140);return result();}
 int setCRC(int n){assert(n==2);return result();}
 int sleep(){++sleeps;return 0;}
};
'''
# Only what mesh_region.h asks of the library; test_mesh_region.py checks the real table.
REGION=r'''
#pragma once
#include <cstdint>
namespace libmeshtastic_leaf {
enum RegionCode : uint8_t {REGION_US=1,REGION_EU_868=3,REGION_JP=5,REGION_ANZ=6,REGION_KR=7,REGION_TW=8,REGION_RU=9,
 REGION_TH=12,REGION_UA_868=15,REGION_MY_919=17,REGION_SG_923=18,REGION_PH_868=20,REGION_PH_915=21,REGION_BR_902=26};
enum ModemPreset : uint8_t {PRESET_LONG_FAST=0,PRESET_MEDIUM_SLOW=3,PRESET_MEDIUM_FAST=4,PRESET_SHORT_SLOW=5,
 PRESET_SHORT_FAST=6,PRESET_LONG_MODERATE=7};
struct RegionInfo {float freqStart,freqEnd;};
struct ModemParams {uint8_t sf;float bw;uint8_t cr;};
struct MeshRegion {
 static const RegionInfo* getRegion(RegionCode){static RegionInfo us{902,928};return &us;}
 static const char* getPresetName(ModemPreset){return "LongFast";}
 static ModemParams getModemParams(ModemPreset,bool){return {11,250,5};}
 static const char* getRegionName(RegionCode c){return c==REGION_US?"US":c==REGION_TH?"TH":"other";}
 static float getFrequency(RegionCode c,ModemPreset p,const char* name){
  assert(p==PRESET_LONG_FAST&&name&&!*name);return c==REGION_US?906.875f:c==REGION_TH?923.875f:869.525f;
 }
};
}
'''
LEAF=r'''
#pragma once
namespace libmeshtastic_leaf {
struct MeshConfig {struct{int region=-1,preset=-1,txPower=0;}radio;int hopLimit=0;};
struct libmeshtastic_leaf {
 void setOwner(const char* a,const char*b){assert(std::string(a)=="S1-ADV"&&std::string(b)=="S1");}
 bool begin(const MeshConfig&c,SX1262*){++meshBegins;assert(step==4&&c.radio.region==wantedRegion&&c.radio.preset==0&&c.radio.txPower==10&&c.hopLimit==3);return result()==0;}
};
}
'''
CODE=r'''
#include <string>
#include "mesh_cap_radio.h"
int main(){
 SPIClass spi;
 // Nothing saved (or a value from other firmware) is US; a saved region is used as saved.
 const struct {int saved,region;float frequency;} cases[]={{1,1,906.875f},{12,12,923.875f},{0,1,906.875f},{255,1,906.875f},{3,3,869.525f}};
 for(const auto& c:cases)for(failure=0;failure<=5;++failure){
  step=0;wanted=c.frequency;wantedRegion=c.region;s1_mesh::RadioSession session(spi,uint8_t(c.saved),s1_mesh::Use::Chat);
  assert(session.region==c.region);
  assert(session.module.spi==&spi&&session.module.spiConfig.timeout==250);
  assert(session.initResult.load()==s1_mesh::kInitPending);
  int e=session.initialize();
  if(!failure)assert(!e&&step==5);
  else assert(e==(failure==5?-1:-19)&&step==failure);
 }
 // Chat leaves sleeping to the mesh. Scan sets the radio up the same way, never starts the mesh,
 // and puts the chip to sleep itself when it ends, but only after a start that worked.
 assert(sleeps==0);
 for(failure=0;failure<=4;++failure){
  step=0;sleeps=0;meshBegins=0;wanted=906.875f;wantedRegion=1;
  {
   s1_mesh::RadioSession session(spi,1,s1_mesh::Use::Scan);
   const int e=session.initialize();session.initResult.store(e);
   if(!failure)assert(!e&&step==4);else assert(e==-19&&step==failure);
   assert(!meshBegins&&!sleeps);
  }
  assert(sleeps==(failure==0?1:0));
 }
 // A session freed while init is still pending (the lifecycle never does this) doesn't touch the bus.
 sleeps=0;{s1_mesh::RadioSession session(spi,1,s1_mesh::Use::Scan);}assert(!sleeps);
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-radio-') as directory:
    p=Path(directory);(p/'RadioLib.h').write_text(RADIO);(p/'MeshRegion.h').write_text('#include <cassert>\n'+REGION);(p/'libmeshtastic_leaf.h').write_text(LEAF)
    (p/'radio.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    '-I'+str(p),'-I'+str(ROOT/'bots/CardputerADV/src'),str(p/'radio.cpp'),'-o',str(p/'radio')],check=True)
    subprocess.run([str(p/'radio')],check=True)
print('PASS actual Cap setup: SPI identity/pins, bounded BUSY timeout, TCXO, RF switch, CRC, saved region (US when unset) on LongFast at 10 dBm, starting on that region channel, every initialization error propagated, and Scan never starting the mesh but sleeping the chip when done')
