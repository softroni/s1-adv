#!/usr/bin/env python3
"""Exercise the actual Settings screen: menu-sized rows, five at a time, scrolling with the arrow keys."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
joined = ''.join((parts / f'{n:03d}_main.cpp.inc').read_text() for n in range(1, 82) if (parts / f'{n:03d}_main.cpp.inc').exists())


def function(signature):
    start = joined.index(signature)
    while joined.find(';', start) < joined.find('{', start):  # Skip forward declarations.
        start = joined.index(signature, start + 1)
    end, depth = joined.index('{', start) + 1, 1
    while depth:
        depth += (joined[end] == '{') - (joined[end] == '}')
        end += 1
    return joined[start:end] + '\n'


settings = function('void renderSettingsUi()')
assert 'd.setFont(&fonts::FreeSansBold9pt7b);' in settings and 'fonts::Font0);\n  d.setCursor(placeX' in settings
assert 'fonts::Font0' not in settings[settings.index('const int first'):], 'rows must stay at the menu size'

code = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using std::min;using std::max;
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}explicit String(int n):std::string(std::to_string(n)){}
 String operator+(const char*o)const{return String(std::string(*this)+o);}String operator+(const String&o)const{return String(std::string(*this)+std::string(o));}};
namespace fonts{const int Font0=0,Font2=2,FreeSansBold9pt7b=9;}
const uint16_t TFT_BLACK=0,TFT_WHITE=0xffff;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
template<class T>T clampValue(T x,T lo,T hi){return std::min(hi,std::max(lo,x));}
enum class UiMode{Settings,FocusSettings,ScreenSettings};UiMode gUiMode=UiMode::Settings;
constexpr uint8_t kDeviceSettingsItemCount=9,kFocusSettingsItemCount=11,kScreenSettingsItemCount=11,kWeatherPlaceSetting=8;
int gSelectedSetting=0;bool gPlaceEditing=false;int editors=0,overlays=0,adjusted=0,toggles=0;
uint8_t currentSettingsItemCount(){return gUiMode==UiMode::Settings?kDeviceSettingsItemCount:gUiMode==UiMode::FocusSettings?kFocusSettingsItemCount:kScreenSettingsItemCount;}
const char* currentSettingsTitle(){return "Device";}
std::string longest="Rancho Santa Margarita, CA";
String settingLabel(uint8_t i);
String settingValue(uint8_t i){return i==kWeatherPlaceSetting?String(longest):String("12h AM/PM");}
struct Text{std::string s;int x,y,font;uint16_t color;};struct Fill{int x,y,w,h;uint16_t color;};
struct Canvas{int x=0,y=0,font=2;uint16_t ink=0;std::vector<Text> printed;std::vector<Fill> fills;
 static void check(int a,int b,int w,int h){assert(a>=0&&b>=0&&a+w<=240&&b+h<=135);}
 void fillScreen(uint16_t){}void setFont(const int*f){font=*f;}void setTextColor(uint16_t c){ink=c;}void setCursor(int a,int b){x=a;y=b;}
 // An upper bound for the bold font: real rows run about 8.5 px per character.
 int textWidth(const std::string&s){return int(s.size())*(font==9?10:font==0?6:8);}
 void print(const std::string&s){check(x,y,textWidth(s),font==9?19:font==0?8:16);printed.push_back({s,x,y,font,ink});x+=textWidth(s);}
 void fillRoundRect(int a,int b,int w,int h,int,uint16_t c){check(a,b,w,h);fills.push_back({a,b,w,h,c});}
 void fillRect(int a,int b,int w,int h,uint16_t c){fillRoundRect(a,b,w,h,0,c);}
 void drawFastHLine(int a,int b,int w,uint16_t){check(a,b,w,1);}
}gCanvas;
String fitCurrentFontToWidth(const String&s,int w){return String(s.substr(0,size_t(max(0,w)/10)));}
void drawWeatherPlaceEditor(){++editors;}void renderLauncherOverlay(){++overlays;}void renderDebugOverlay(){}
''' + function('String settingLabel(uint8_t index)') + function('void renderSettingsUi()') + r'''
void adjustCurrentSetting(int delta){adjusted+=delta;}void toggleCurrentSetting(){++toggles;}
void loadDeviceSettings(){}void loadFocusSettings(){}void applyDisplayBrightness(){}void applySpeakerVolumeSetting(){}
void saveDeviceSettings(){}void saveFocusSettings(){}void setStatus(const char*,int){}
''' + function('bool handleSettingsKey(char key)') + r'''
int main(){
 for(UiMode mode:{UiMode::Settings,UiMode::FocusSettings,UiMode::ScreenSettings}){
  gUiMode=mode;const int count=currentSettingsItemCount();
  for(int sel=0;sel<count;++sel){
   gSelectedSetting=sel;gCanvas=Canvas();renderSettingsUi();
   assert(gCanvas.font==2);  // what's drawn next expects the default font
   // Five rows of label and value, all at the menu's size.
   std::vector<Text> rows;for(const auto&t:gCanvas.printed)if(t.font==9&&t.y>=27)rows.push_back(t);
   assert(rows.size()==10);
   bool selectedShown=false;
   for(size_t i=0;i<rows.size();i+=2){
    const Text&label=rows[i],&value=rows[i+1];
    assert(label.y==value.y&&label.y==27+int(i/2)*22&&label.x==8);
    assert(label.x+gCanvas.textWidth(label.s)<value.x||value.s.empty());
    assert(value.x+gCanvas.textWidth(value.s)<=229);
    if(label.s==std::string(settingLabel(sel))){selectedShown=true;assert(label.color==TFT_BLACK&&value.color==TFT_BLACK);}
   }
   assert(selectedShown);
   // One selected bar, on that row.
   int bars=0;for(const auto&f:gCanvas.fills)if(f.w==233)++bars;assert(bars==1);
   // The bar at the side moves from top to bottom with the list.
   const Fill&thumb=gCanvas.fills.back();assert(thumb.x==236&&thumb.y>=24&&thumb.y+thumb.h<=133);
   if(sel<=2){assert(thumb.y==24);}
   if(sel>=count-3){assert(thumb.y+thumb.h>=132);}
   // The next row is in view, except at the very end.
   if(sel+1<count){bool next=false;for(const auto&t:rows)next=next||t.s==std::string(settingLabel(sel+1));assert(next);}
   assert(overlays>0);
  }
 }
 // The box for typing a city only appears while typing.
 gUiMode=UiMode::Settings;gSelectedSetting=0;editors=0;renderSettingsUi();assert(editors==0);
 gPlaceEditing=true;renderSettingsUi();assert(editors==1);gPlaceEditing=false;
 // Keys: up and down pick and wrap, left and right change.
 gSelectedSetting=0;assert(handleSettingsKey('.')&&gSelectedSetting==1&&adjusted==0);
 assert(handleSettingsKey(';')&&gSelectedSetting==0);assert(handleSettingsKey(';')&&gSelectedSetting==8);
 assert(handleSettingsKey('.')&&gSelectedSetting==0);
 assert(handleSettingsKey('/')&&adjusted==1&&gSelectedSetting==0);
 assert(handleSettingsKey(',')&&adjusted==0&&gSelectedSetting==0);
 assert(handleSettingsKey('\n')&&toggles==1);
 std::cout<<"PASS Settings: menu-sized rows, five at a time, selected row in view, arrows scroll and change\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-settings-layout-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
