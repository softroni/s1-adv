#!/usr/bin/env python3
"""Exercise weather on the Pulse screen: when it's asked for, what happens to the answer, and the ZIP setting."""
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


pulse = function('void renderAssistantPulseUi()')
assert 'if (state == AssistantPulseState::Ready && !working) {\n    drawHomeWeather();' in pulse, 'the orb still shows while anything is happening'
assert 'kDeviceSettingsItemCount = 9;' in joined and 'kWeatherZipSetting = 8;' in joined
assert 'gPrefs.getString("wx_zip", weather_view::kDefaultZip)' in joined and 'gPrefs.putString("wx_zip", gDeviceSettings.weatherZip)' in joined
assert 'if (!weather_view::validZip(gDeviceSettings.weatherZip.c_str())) gDeviceSettings.weatherZip = weather_view::kDefaultZip;' in joined

code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include "weather_view.h"
using namespace weather_view;
using std::min;using std::max;
struct String:std::string{using std::string::string;String()=default;String(const std::string&s):std::string(s){}
 explicit String(int n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}size_t length()const{return size();}void remove(size_t at){erase(at);}
 int indexOf(const char*s)const{auto p=find(s);return p==npos?-1:int(p);}
 String operator+(const char*o)const{return String(std::string(*this)+o);}String operator+(const String&o)const{return String(std::string(*this)+std::string(o));}};
String operator+(const char*a,const String&b){return String(std::string(a)+std::string(b));}
uint32_t nowMs=100000,gLastRenderMs=99;uint32_t millis(){return nowMs;}
time_t clockNow=1800000000;
#define time(x) clockNow
namespace fonts{const int Font0=0,Font2=2,Font4=4;}
const uint16_t TFT_WHITE=0xffff;
constexpr uint16_t rgb565_local(int r,int g,int b){return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
struct WeatherReport{bool valid=false;int temp=0,high=0,low=0,rain=0;Icon icon=Icon::Cloud;String condition,place;char updated[10]="";uint32_t receivedMs=0;};
WeatherReport gWeather;String gWeatherNote="Getting weather";Schedule gWeatherSchedule;
bool gZipEditing=false;String gZipDraft;int saves=0;std::string statusText;
struct{String weatherZip="10001";bool clockTwelveHour=true;}gDeviceSettings;
void saveDeviceSettings(){++saves;}void setStatus(const String&s,int){statusText=s;}void logf(const char*,...){}
namespace clock_display{void format(char*p,size_t n,int,int,bool){snprintf(p,n,"6:30 PM");}}
// The parts of ArduinoJson this uses, reading a fake document.
struct Field{bool isInt=false;int value=0;const char* text=nullptr;template<class T>bool is()const{return isInt;}
 int operator|(int d)const{return isInt?value:d;}const char* operator|(const char* d)const{return text?text:d;}};
struct DynamicJsonDocument{explicit DynamicJsonDocument(int){}Field operator[](const char*key)const;};
bool jsonBroken=false,noTemp=false;
bool deserializeJson(DynamicJsonDocument&,const String&){return jsonBroken;}
Field DynamicJsonDocument::operator[](const char*k)const{std::string s=k;
 if(s=="temp")return noTemp?Field{}:Field{true,70};if(s=="high")return {true,78};if(s=="low")return {true,61};if(s=="rain")return {true,20};
 if(s=="icon"){Field f;f.text="storm";return f;}if(s=="condition"){Field f;f.text="Thunderstorm";return f;}if(s=="place"){Field f;f.text="Salem";return f;}return {};}
struct Text{std::string s;int x,y;};
struct Canvas{int x=0,y=0,font=2;std::vector<Text> printed;int fills=0;
 void setTextSize(int){}void setFont(const int*f){font=*f;}void setTextColor(uint16_t){}void setCursor(int a,int b){x=a;y=b;}
 int textWidth(const std::string&s){return int(s.size())*(font==4?14:font==0?6:6);}
 void check(int a,int b,int w,int h){assert(a>=0&&b>=0&&a+w<=116&&b+h<=106);}
 void print(const std::string&s){check(x,y,textWidth(s),font==4?26:font==0?8:16);printed.push_back({s,x,y});x+=textWidth(s);}
 void fillRect(int a,int b,int w,int h,uint16_t){check(a,b,w,h);++fills;}void drawCircle(int a,int b,int r,uint16_t){check(a-r,b-r,2*r,2*r);}
}gCanvas;
String fitCurrentFontToWidth(const String&s,int w){return String(s.substr(0,size_t(w/6)));}
''' + function('void applyWeatherResponse(') + function('bool handleZipEditKey(') + function('void drawHomeWeather()') + r'''
bool printed(const char*t){for(auto&p:gCanvas.printed)if(p.s==t)return true;return false;}
void type(const char*keys){for(;*keys;++keys)handleZipEditKey(*keys);}
int main(){
 // ZIPs.
 assert(validZip("10001")&&validZip("00501"));
 for(const char*bad:{"1000","100011","1000a",""," 1000"})assert(!validZip(bad));assert(!validZip(nullptr));
 // Pictures and colours.
 std::set<const uint8_t*> pictures;std::set<int> tints;
 for(const char*name:{"sun","moon","partly","cloud","fog","rain","snow","storm"}){
  Icon icon=iconFromWire(name);pictures.insert(bitmap(icon));uint16_t c=tint(icon);
  tints.insert(((c>>8)&0xE0)|((c>>6)&0x1C)|((c>>3)&0x03));assert(c!=0);}
 assert(pictures.size()==8&&tints.size()>=6);
 assert(iconFromWire("hail")==Icon::Cloud&&iconFromWire(nullptr)==Icon::Cloud);

 // After boot it's due at once, and then not for 30 minutes.
 {Schedule s;assert(s.due(5));s.asked(1000);assert(!s.due(1000)&&!s.due(1000+kRefreshMs-1)&&s.due(1000+kRefreshMs));
  assert(kRefreshMs==1800000U);
  s.retrySoon(1000);assert(!s.due(1000+kRetryMs-1)&&s.due(1000+kRetryMs));assert(kRetryMs<kRefreshMs);
  s.reset();assert(s.due(1));
  // Millis wrapping around doesn't stop it.
  s.asked(0xFFFFF000U);assert(!s.due(0xFFFFF001U));assert(s.due(0xFFFFF000U+kRefreshMs));}

 // A good answer shows, and the next one is 30 minutes away.
 applyWeatherResponse(200,"{}");
 assert(gWeather.valid&&gWeather.temp==70&&gWeather.high==78&&gWeather.icon==Icon::Storm&&gWeather.place=="Salem");
 assert(std::string(gWeather.updated)=="6:30 PM"&&gLastRenderMs==0);
 assert(!gWeatherSchedule.due(nowMs+kRefreshMs-1)&&gWeatherSchedule.due(nowMs+kRefreshMs));
 // Losing the network keeps the last weather and tries again soon.
 applyWeatherResponse(-1,"");assert(gWeather.valid&&gWeatherSchedule.due(nowMs+kRetryMs));
 // So does a mangled answer.
 jsonBroken=true;applyWeatherResponse(200,"junk");assert(gWeather.valid&&gWeatherSchedule.due(nowMs+kRetryMs));jsonBroken=false;
 noTemp=true;applyWeatherResponse(200,"{}");assert(gWeather.valid);noTemp=false;
 // The bridge being unable to help waits for the next turn, keeping what's there.
 applyWeatherResponse(503,"{}");assert(gWeather.valid&&!gWeatherSchedule.due(nowMs+kRetryMs));
 // An older bridge has no such route, and that isn't a fault with the ZIP.
 gWeather=WeatherReport();gWeatherNote="";applyWeatherResponse(404,"{\"error\":\"not_found\"}");assert(gWeatherNote=="No weather yet");
 // A ZIP that doesn't exist says where to fix it, and drops the old place's weather.
 applyWeatherResponse(200,"{}");applyWeatherResponse(404,"{\"error\":\"unknown_zip\"}");
 assert(!gWeather.valid&&gWeatherNote=="Check your ZIP");
 // Time unknown: still shows, without a made-up time.
 clockNow=5;applyWeatherResponse(200,"{}");assert(std::string(gWeather.updated).empty());clockNow=1800000000;

 // The panel shows the date, temperature and forecast, inside the orb's space.
 applyWeatherResponse(200,"{}");drawHomeWeather();
 assert(printed("70")&&printed("Thunderstorm")&&printed("H78 L61")&&printed("Rain 20%  Salem")&&gCanvas.fills>40);
 // The time shows up only once a refresh has been missed.
 assert(!printed("As of 6:30 PM"));nowMs+=kRefreshMs;gCanvas=Canvas();drawHomeWeather();assert(printed("Rain 20%  Salem"));
 nowMs+=kRefreshMs;gCanvas=Canvas();drawHomeWeather();assert(printed("As of 6:30 PM")&&!printed("Rain 20%  Salem"));
 // Without a clock it can't say, so it keeps the forecast line.
 gWeather.updated[0]=0;gCanvas=Canvas();drawHomeWeather();assert(printed("Rain 20%  Salem"));
 applyWeatherResponse(200,"{}");gCanvas=Canvas();drawHomeWeather();
 bool dated=false;for(auto&p:gCanvas.printed)dated=dated||p.y==12;assert(dated);
 // The widest values still fit.
 gWeather.temp=-100;gWeather.high=112;gWeather.low=-20;gWeather.rain=100;gWeather.condition="Freezing drizzle";gWeather.place="Rancho Santa Margarita";
 gCanvas=Canvas();drawHomeWeather();
 // Nothing yet: a note, and no invented numbers. Before the clock is set there's no date either.
 gWeather=WeatherReport();gWeatherNote="Getting weather";gCanvas=Canvas();clockNow=5;drawHomeWeather();
 assert(gCanvas.printed.size()==1&&gCanvas.printed[0].s=="Getting weather");clockNow=1800000000;

 // Typing a ZIP: digits only, five at most, and every other key is ignored.
 gZipEditing=true;gZipDraft="";type("7a5;0.");assert(gZipDraft=="750");
 handleZipEditKey('\b');assert(gZipDraft=="75");type("0123456");assert(gZipDraft=="75012");
 // Too short can't be saved.
 gZipDraft="100";int before=saves;handleZipEditKey('\n');assert(gZipEditing&&saves==before&&gDeviceSettings.weatherZip=="10001");
 assert(statusText=="A ZIP has 5 digits");
 // Saving a new one fetches straight away and clears the old place.
 applyWeatherResponse(200,"{}");gZipDraft="10001";handleZipEditKey('\n');
 assert(!gZipEditing&&gDeviceSettings.weatherZip=="10001"&&saves==before+1&&!gWeather.valid&&gWeatherSchedule.due(nowMs));
 // The same ZIP changes nothing.
 applyWeatherResponse(200,"{}");gZipEditing=true;gZipDraft="10001";before=saves;handleZipEditKey('\n');
 assert(!gZipEditing&&saves==before&&gWeather.valid&&!gWeatherSchedule.due(nowMs));
 std::cout<<"PASS weather: schedule, failures keep the last report, ZIP entry, panel bounds\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-weather-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(src), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
