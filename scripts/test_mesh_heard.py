#!/usr/bin/env python3
"""Check what Mesh keeps of the radios it hears for the Lab: positions, the bounded list and names."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CODE=r'''
#include "mesh_heard.h"
#include "mesh_help.h"
#include <cassert>
#include <random>
#include <string>
#include <vector>
using namespace s1_mesh;
static_assert(sizeof(Heard)<512,"kept for as long as the firmware runs, so it stays small");
int main() {
  // A position from Mesh's own encoder reads back, exact or rounded the way the phone apps round it.
  uint8_t packet[64];int32_t lat=0,lon=0;uint8_t bits=0;
  size_t n=encodePosition(packet,sizeof(packet),4250833,-7090000,9);
  assert(n && decodePosition(packet,n,lat,lon,bits) && lat==4250833 && lon==-7090000 && bits==32);
  n=encodePosition(packet,sizeof(packet),4250833,-7090000,9,13);
  assert(decodePosition(packet,n,lat,lon,bits) && bits==13 && lat!=4250833);
  assert(lat==(roughE7(425083300,13)+50)/100 && lon==(roughE7(-709000000,13)-50)/100);
  // Southern and western, and the far corners of the map.
  n=encodePosition(packet,sizeof(packet),-3386880,15120930,4);
  assert(decodePosition(packet,n,lat,lon,bits) && lat==-3386880 && lon==15120930);
  // Fields it doesn't use are skipped by their wire type: a varint, a length, 64 bits.
  const std::vector<uint8_t> extra={0x18,0xAC,0x02, 0x0D,0x10,0x27,0x00,0x00, 0x22,0x03,'a','b','c', 0x15,0x20,0x4E,0x00,0x00,
                                    0x29,1,2,3,4,5,6,7,8, 0xB8,0x01,0x10};
  assert(decodePosition(extra.data(),extra.size(),lat,lon,bits) && lat==100 && lon==200 && bits==16);
  // 0,0 is what a radio with no fix sends, and half a position isn't one.
  const std::vector<uint8_t> nowhere={0x0D,0,0,0,0,0x15,0,0,0,0};
  assert(!decodePosition(nowhere.data(),nowhere.size(),lat,lon,bits));
  const std::vector<uint8_t> half={0x0D,0x10,0x27,0x00,0x00};
  assert(!decodePosition(half.data(),half.size(),lat,lon,bits));
  // Off the map, and every cut-off or broken packet, are refused.
  const std::vector<uint8_t> off={0x0D,0x01,0xE9,0xA4,0x35,0x15,0x01,0,0,0};  // 90.000001 degrees.
  assert(!decodePosition(off.data(),off.size(),lat,lon,bits));
  n=encodePosition(packet,sizeof(packet),4250833,-7090000,9);
  for(size_t cut=0;cut<n;++cut)if(cut!=10 && cut!=12 && cut!=15)assert(!decodePosition(packet,cut,lat,lon,bits));
  const std::vector<uint8_t> group={0x0D,0x10,0x27,0,0,0x15,0x20,0x4E,0,0,0x0B};  // Wire type 3.
  assert(!decodePosition(group.data(),group.size(),lat,lon,bits));
  const std::vector<uint8_t> tooLong={0x0D,0x10,0x27,0,0,0x15,0x20,0x4E,0,0,0x22,0x7F,1};
  assert(!decodePosition(tooLong.data(),tooLong.size(),lat,lon,bits));
  std::mt19937 rng(4242);
  for(int trial=0;trial<50000;++trial) {
    uint8_t junk[40];const size_t length=rng()%sizeof(junk);for(size_t i=0;i<length;++i)junk[i]=uint8_t(rng());
    if(decodePosition(junk,length,lat,lon,bits))assert(lat>=-9000000 && lat<=9000000 && lon>=-18000000 && lon<=18000000 && bits>=1 && bits<=32);
  }

  // The list holds eight, the one heard longest ago makes room, and it survives the clock wrapping.
  Heard heard;
  assert(!heard.hear(0,1) && !heard.hear(kBroadcast,1) && heard.count==0);
  for(uint32_t id=1;id<=kHeardNodes;++id)assert(heard.hear(id,id*10));
  assert(heard.count==kHeardNodes);
  heard.hear(1,500);
  HeardNode* fresh=heard.hear(99,600);
  assert(fresh && fresh->id==99 && fresh->hops==kHopsUnknown && !fresh->placed() && heard.count==kHeardNodes);
  bool one=false,two=false;for(const auto& node:heard.nodes){one|=node.id==1;two|=node.id==2;}
  assert(one && !two);
  // A radio already on the list keeps what it said.
  fresh->precisionBits=32;fresh->name[0]='Z';
  assert(heard.hear(99,700)==fresh && fresh->placed() && fresh->name[0]=='Z' && fresh->seenMs==700);
  const HeardNode* order[kHeardNodes];
  assert(heard.newestFirst(order,kHeardNodes)==kHeardNodes && order[0]->id==99 && order[1]->id==1 && order[2]->id==8 && order[kHeardNodes-1]->id==3);
  for(size_t i=1;i<kHeardNodes;++i)assert(int32_t(order[i-1]->seenMs-order[i]->seenMs)>=0);
  assert(heard.newestFirst(order,2)==2 && heard.placedCount()==1);
  Heard wrap;
  for(uint32_t id=1;id<=kHeardNodes;++id)wrap.hear(id,UINT32_MAX-100+id);
  wrap.hear(50,20);
  for(const auto& node:wrap.nodes)assert(node.id!=1);
  assert(wrap.newestFirst(order,kHeardNodes)==kHeardNodes && order[0]->id==50);

  // Rough means the sender rounded it to more than about 45 m.
  HeardNode spot;spot.precisionBits=13;assert(spot.placed() && spot.rough());
  spot.precisionBits=kPointBits;assert(!spot.rough());spot.precisionBits=0;assert(!spot.placed() && !spot.rough());
  assert(hopsTaken(3,3)==0 && hopsTaken(3,1)==2 && hopsTaken(0,3)==kHopsUnknown && hopsTaken(2,3)==kHopsUnknown);

  // Names the Lab's fonts can draw, and the radio's number when nothing in a name can be.
  char shown[32];HeardNode named;named.id=0x1234beef;
  strcpy(named.name,"Hilltop Relay");shownName(named,shown,sizeof(shown));assert(std::string(shown)=="Hilltop Relay");
  strcpy(named.name,u8"\U0001F419 Octo  Base ");shownName(named,shown,sizeof(shown));assert(std::string(shown)=="Octo Base");
  strcpy(named.name,u8"\U0001F419\U0001F680");shownName(named,shown,sizeof(shown));assert(std::string(shown)=="!beef");
  named.name[0]=0;shownName(named,shown,sizeof(shown));assert(std::string(shown)=="!beef");
  strcpy(named.name,"Long name that goes on");shownName(named,shown,6);assert(std::string(shown)=="Long");
  shownName(named,shown,1);assert(shown[0]==0);
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-heard-') as directory:
    cpp=Path(directory)/'heard.cpp';exe=Path(directory)/'heard';cpp.write_text(CODE)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(ROOT/'bots/CardputerADV/src'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS Mesh heard list: positions from Mesh\'s own encoder, skipped and broken fields, 50000 junk packets, eight radios with the oldest making room, and names the Lab can draw (ASan/UBSan)')
