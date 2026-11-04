#!/usr/bin/env python3
"""Run Mesh's private channel setup screen (part 091): every key, what's saved, the radio side, bounds.

The key must never reach a log, so every line logged is checked for it.
"""
import base64
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
part = (ROOT / 'bots/CardputerADV/src/main_parts/091_main.cpp.inc').read_text()
# Mesh's drawing helpers, which the screen shares with the rest of Mesh.
mesh = (ROOT / 'bots/CardputerADV/src/main_parts/068_main.cpp.inc').read_text()
drawing = mesh[mesh.index('// --- Mesh drawing'):mesh.index('// --- end of Mesh drawing')]
typed_key = bytes(random.Random(11).randrange(256) for _ in range(32))
typed_text = base64.b64encode(typed_key).decode()

CODE = r'''
#include "launcher_icons.h"
#include "mesh_private_channel.h"
#include "mesh_sounds.h"
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
using std::min;
struct String:std::string {
 using std::string::string;
 String(const std::string&s):std::string(s){}
 bool isEmpty()const{return empty();}
 void remove(size_t p,size_t n){erase(p,n);}
 String substring(size_t a)const{return substr(a);}
 String substring(size_t a,size_t b)const{return substr(a,b-a);}
};
String operator+(const String&a,const char*b){return String(std::string(a)+b);}
namespace fonts{int Font0=0,Font2=2,FreeSansBold9pt7b=9,efontCN_12=12;}
constexpr int TFT_WHITE=65535;int rgb565_local(int,int,int){return 1;}
uint32_t gNow=1000;uint32_t millis(){return gNow;}
std::vector<std::string> logs;
void logf(const char*f,...){char b[256];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);logs.push_back(b);}
struct Keyboard_Class{struct KeysState{bool ctrl=false,enter=false,del=false;};};
// NVS in memory, as Preferences sees it.
std::map<std::string,std::vector<uint8_t>> gNvs;bool gNvsWritable=true;
struct Preferences{bool ro=true;
 bool begin(const char*ns,bool readOnly){assert(std::string(ns)=="s1mesh");ro=readOnly;return true;}
 void end(){}
 size_t getBytesLength(const char*k){return gNvs.count(k)?gNvs[k].size():0;}
 size_t getBytes(const char*k,void*out,size_t n){if(!gNvs.count(k))return 0;size_t m=std::min(n,gNvs[k].size());memcpy(out,gNvs[k].data(),m);return m;}
 size_t putBytes(const char*k,const void*in,size_t n){assert(!ro);if(!gNvsWritable)return 0;auto p=static_cast<const uint8_t*>(in);gNvs[k].assign(p,p+n);return n;}
 bool remove(const char*k){assert(!ro);return gNvs.erase(k)>0;}
 bool isKey(const char*k){return gNvs.count(k)>0;}
};
uint8_t gFill=3;void esp_fill_random(void*out,size_t n){auto p=static_cast<uint8_t*>(out);for(size_t i=0;i<n;++i)p[i]=uint8_t(gFill+=29);}
struct Canvas{int font=0,x=0,y=0;std::set<std::string> printed;std::vector<std::string> order;
 void setFont(int*f){font=*f;}void setTextColor(int,int=0){}void setCursor(int a,int b){x=a;y=b;}void setTextSize(int){}
 void fillScreen(int){}
 int textWidth(const String&s)const{return int(s.size())*(font==0||font==12?6:font==9?12:9);}
 template<class T>void print(T v){String s(v);if(s.empty())return;const int w=textWidth(s);
  assert(x>=0&&y>=0&&x+w<=(font==0?232:240)&&y+(font==0?8:font==9?18:font==12?12:16)<=135);printed.insert(s);order.push_back(s);x+=w;}
 static void box(int a,int b,int w,int h){assert(a>=0&&b>=0&&a+w<=240&&b+h<=135);}
 void drawRect(int a,int b,int w,int h,int){box(a,b,w,h);}
 void drawRoundRect(int a,int b,int w,int h,int,int){box(a,b,w,h);}void fillRoundRect(int a,int b,int w,int h,int,int){box(a,b,w,h);}
 void fillRect(int a,int b,int w,int h,int){box(a,b,w,h);}
 void drawFastHLine(int a,int b,int w,int){box(a,b,w,1);}void drawFastVLine(int a,int b,int h,int){box(a,b,1,h);}
 void drawBitmap(int a,int b,const uint8_t*,int w,int h,int){box(a,b,w,h);}
 std::string qr;void qrcode(const char*t,int a,int b,int w,int,bool margin){assert(a>=0&&b>=0&&a+w<=240&&b+w<=135&&margin);qr=t;}
}gCanvas;
// What the radio session is told when Mesh starts.
namespace libmeshtastic_leaf{struct Fake{int calls=0;bool ok=true;uint8_t key[32]={};size_t len=0;std::string name;
 bool setSecondaryChannel(const uint8_t*k,size_t n,const char*nm){++calls;memcpy(key,k,n);len=n;name=nm;return ok;}};}
namespace s1_mesh{struct RadioSession{libmeshtastic_leaf::Fake mesh;PrivateChannel privateChannel;};}
std::vector<s1_mesh::Sound> sounds;void meshSound(s1_mesh::Sound s){sounds.push_back(s);}
''' + drawing + r'''
''' + part + r'''
using namespace s1_mesh;
Keyboard_Class::KeysState plain,enterKey,delKey;
void key(char c){assert(meshChannelKey(c,plain));}
void enter(){assert(meshChannelKey('\n',enterKey));}
void type(const char*t){for(;*t;++t)key(*t);}
bool shows(const char*t){gCanvas=Canvas();renderMeshChannelUi();return gCanvas.printed.count(t)>0;}
void render(){gCanvas=Canvas();renderMeshChannelUi();}
std::string joined(){render();std::string all;for(auto&s:gCanvas.order)all+=s+"\n";return all;}
int main(){
 enterKey.enter=true;delKey.del=true;
 const std::string typedKey="''' + typed_text + r'''";
 // Nothing saved: the first screen says what C does, and the setup shows nothing yet.
 assert(meshChannelLabel()=="none");
 meshChannelOpen();assert(gMeshChannelSetup&&shows("none yet")&&shows("N name K key G new V show X del"));
 // A name first: refused when it isn't one, then taken. With no key, nothing is saved yet.
 key('n');type("Fam!ly");sounds.clear();enter();assert(shows("Letters, digits, - and _, up to 11")&&shows("Enter save  Esc cancel"));
 assert(sounds.size()==1&&sounds[0]==Sound::Nope);
 for(int i=0;i<6;++i)assert(meshChannelKey('\b',delKey));type("Family");enter();
 assert(std::string(gMeshChannel.name)=="Family"&&gNvs.empty()&&shows("Now G for a new key, or K to type one"));
 // G makes a key, saves at once and shows it, because it's no use until it's shared: as text in three
 // lines, and as a QR code holding exactly the key.
 sounds.clear();key('g');assert(gNvs.count("privch")&&gNvs["privch"].size()==kChannelSavedBytes&&gMeshChannel.keyLength==32);
 assert(sounds.size()==1&&sounds[0]==Sound::Saved);
 char made[kChannelKeyText];keyToBase64(gMeshChannel.key,32,made,sizeof(made));
 {const std::string all=joined();std::string flat;for(char c:all)if(c!='\n')flat+=c;assert(flat.find(made)!=std::string::npos);}
 assert(gCanvas.qr==made&&shows("New key saved")&&shows("V hide  Esc back"));
 gNow+=3000;assert(shows("Scan or type it"));
 key('v');assert(shows("AES-256, set")&&gCanvas.qr.empty());key('v');
 // V hides it again; G on a shared key asks first, then replaces it.
 key('v');{const std::string all=joined();assert(all.find(std::string(made).substr(0,22))==std::string::npos);}
 uint8_t before[32];memcpy(before,gMeshChannel.key,32);
 sounds.clear();key('g');assert(!memcmp(before,gMeshChannel.key,32)&&shows("G again replaces the shared key")&&sounds.back()==Sound::Ask);
 key('g');assert(memcmp(before,gMeshChannel.key,32)!=0);
 // Another key between the two presses cancels the question, so G only asks again.
 memcpy(before,gMeshChannel.key,32);key('g');key('v');key('g');assert(!memcmp(before,gMeshChannel.key,32)&&shows("G again replaces the shared key"));
 key('n');meshChannelEscape();
 // K types a key from a phone: junk, Meshtastic's public key and the shorthand are refused.
 key('k');type("not a key");enter();assert(shows("That isn't a 16 or 32 byte key"));
 for(size_t i=0;i<9;++i)meshChannelKey('\b',delKey);type("1PG7OiApB1nwvP+rz05pAQ==");enter();assert(shows("That's Meshtastic's public key"));
 for(size_t i=0;i<24;++i)meshChannelKey('\b',delKey);type("AQ==");enter();assert(shows("That isn't a 16 or 32 byte key"));
 for(size_t i=0;i<4;++i)meshChannelKey('\b',delKey);type(typedKey.c_str());render();  // A long key keeps its end in view.
 enter();assert(gMeshChannel.keyLength==32&&shows("Saved. Mesh hears it from now on"));
 PrivateChannel stored;assert(loadChannel(gNvs["privch"].data(),gNvs["privch"].size(),stored)&&std::string(stored.name)=="Family");
 assert(meshChannelLabel()=="#Family");
 // Esc leaves an edit first, then the screen.
 key('n');assert(meshChannelEscape()&&gMeshChannelSetup&&gMeshChannelEdit==MeshChannelEdit::None);
 assert(meshChannelEscape()&&!gMeshChannelSetup&&!meshChannelEscape());
 // Starting Mesh hands the saved channel to the radio.
 RadioSession s;meshLoadPrivateChannel(s);
 assert(s.mesh.calls==1&&s.mesh.len==32&&s.mesh.name=="Family"&&!memcmp(s.mesh.key,stored.key,32)&&s.privateChannel.set());
 RadioSession refused;refused.mesh.ok=false;meshLoadPrivateChannel(refused);assert(!refused.privateChannel.set());
 // A name typed but no key yet is dropped when the screen closes, and nothing saved changes.
 meshChannelOpen();key('x');assert(shows("X again removes it"));key('n');meshChannelEscape();
 assert(gNvs.count("privch"));
 // X twice removes it from the screen and from NVS.
 sounds.clear();key('x');key('x');assert(!gNvs.count("privch")&&!gMeshChannel.set()&&shows("Removed"));
 assert(sounds.size()==2&&sounds[0]==Sound::Ask&&sounds[1]==Sound::Removed);
 assert(meshChannelLabel()=="none");
 RadioSession none;meshLoadPrivateChannel(none);assert(!none.mesh.calls&&!none.privateChannel.set());
 // A key typed first, then the name, saves once both are there.
 key('k');type(typedKey.c_str());enter();assert(shows("Now N to give it a name")&&!gNvs.count("privch"));
 key('n');type("Club");enter();assert(gNvs.count("privch"));
 // Storage that won't take it says so.
 gNvsWritable=false;key('n');for(int i=0;i<4;++i)meshChannelKey('\b',delKey);type("Club2");sounds.clear();enter();assert(shows("Couldn't save it"));gNvsWritable=true;
 assert(sounds.size()==1&&sounds[0]==Sound::Nope);
 // Ctrl keys do nothing here; notes fade.
 Keyboard_Class::KeysState ctrl;ctrl.ctrl=true;assert(meshChannelKey('n',ctrl)&&gMeshChannelEdit==MeshChannelEdit::None);
 gNow+=3000;render();
 // The key is never in a log, in any form typed or made.
 for(const auto& line:logs){assert(line.find(typedKey.substr(0,20))==std::string::npos&&line.find(std::string(made).substr(0,20))==std::string::npos);}
 assert(!logs.empty());
}
'''
with tempfile.TemporaryDirectory(prefix='s1-mesh-private-ui-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'bots/CardputerADV/src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS Mesh private channel screen: name and key checked on Enter, the public and junk keys refused, G making, showing '
      'and asking before replacing a key, X asking before removing, Esc order, NVS that refuses, the radio told at start, '
      'bounds, and the key never logged')
