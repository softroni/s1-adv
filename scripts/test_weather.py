#!/usr/bin/env python3
"""Exercise weather on the Pulse screen: when it's asked for, what happens to the answer, and the city setting."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
joined = ''.join((parts / f'{n:03d}_main.cpp.inc').read_text() for n in range(1, 100) if (parts / f'{n:03d}_main.cpp.inc').exists())
# Pulse's GPS weather, read from its part, counting each answer it's told about.
gps_weather = (parts / '089_main.cpp.inc').read_text()
assert gps_weather.count('void noteWeatherAnswered() { gWeatherShownAt = gWeatherAsking; }') == 1
gps_weather = gps_weather.replace('void noteWeatherAnswered() { gWeatherShownAt = gWeatherAsking; }',
                      'void noteWeatherAnswered() { ++answered; gWeatherShownAt = gWeatherAsking; }')


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
assert 'kDeviceSettingsItemCount = 12;' in joined and 'kWeatherPlaceSetting = 8;' in joined
assert 'gPrefs.getString("wx_place", weather_view::kDefaultPlace)' in joined and 'gPrefs.putString("wx_place", gDeviceSettings.weatherPlace)' in joined
assert 'if (!weather_view::validPlace(gDeviceSettings.weatherPlace.c_str())) gDeviceSettings.weatherPlace = weather_view::kDefaultPlace;' in joined
# A comma and a space have to survive the trip to the bridge.
assert '"/api/cardputer/weather?" + weatherQuery(now)' in joined
assert 'return "place=" + urlEncodePathSegment(gDeviceSettings.weatherPlace) + unitsQuery;' in joined
# Typed keys go to the box ahead of every shortcut, as with a Wi-Fi password, or "s" would save settings.
typing = function('void handleTypingKey(')
assert typing.index('if (gPlaceEditing && !gLauncherVisible && !keys.tab) {') < typing.index('handleFocusShortcut(lowerKey,keys)')
assert 'if(!isSettingsMode(gUiMode) || gLauncherVisible || gPlaceEditing) return false;' in joined, 'arrows must not move rows behind the box'
assert 'if (gPlaceEditing && !gLauncherVisible) {\n    gPlaceEditing = false;\n    return true;' in joined, 'Esc must only close the box'
assert 'if (gPlaceEditing) drawWeatherPlaceEditor();' in joined

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
#include "weather_gps.h"
#include "units.h"
using namespace weather_view;
using std::min;using std::max;
struct String:std::string{using std::string::string;String()=default;String(const std::string&s):std::string(s){}
 explicit String(int n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}size_t length()const{return size();}void remove(size_t at){erase(at);}
 void trim(){const auto a=find_first_not_of(' ');if(a==npos){clear();return;}const auto b=find_last_not_of(' ');*this=String(substr(a,b-a+1));}
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
bool gPlaceEditing=false;String gPlaceDraft;int saves=0;std::string statusText;
struct{String weatherPlace="Salem, MA";bool clockTwelveHour=true;}gDeviceSettings;
void saveDeviceSettings(){++saves;}void setStatus(const String&s,int){statusText=s;}void logf(const char*,...){}
int answered=0;namespace{void noteWeatherAnswered();}  // As 005 declares it.
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
''' + function('void applyWeatherResponse(') + function('bool handlePlaceEditKey(') + function('void drawHomeWeather()') + r'''
// Pulse's GPS weather, against a pretend GPS port.
enum class UiMode{Home,LabDevices,Mesh};UiMode gUiMode=UiMode::Home;bool gMeshBusOwned=false,gLabOpen=false;
constexpr uint8_t kGpsForPulse=4;uint8_t gGpsHolders=0;int holds=0,releases=0,feeds=0;
void gpsHold(uint8_t w){gGpsHolders|=w;++holds;}void gpsRelease(uint8_t w){gGpsHolders&=uint8_t(~w);++releases;}
lab_gps::Parser gLabGps;void gpsFeed(uint32_t){++feeds;}
String urlEncodePathSegment(const String&){return String("Salem%2C%20MA");}
units::System gUnits=units::System::US;units::System displayUnits(){return gUnits;}
''' + gps_weather + r'''
void hear(int32_t lat,int32_t lon,bool valid=true){gLabGps.lines=1;gLabGps.lastMs=nowMs;gLabGps.fix.valid=valid;gLabGps.fix.latE5=lat;gLabGps.fix.lonE5=lon;}
bool printed(const char*t){for(auto&p:gCanvas.printed)if(p.s==t)return true;return false;}
void type(const char*keys){for(;*keys;++keys)handlePlaceEditKey(*keys);}
void testGpsWeather(){
 // Two decimals, rounded half away from zero, and never minus nothing.
 char t[48];weather_gps::hundredths(t,sizeof(t),4250833);assert(std::string(t)=="42.51");
 weather_gps::hundredths(t,sizeof(t),-7090000);assert(std::string(t)=="-70.90");
 weather_gps::hundredths(t,sizeof(t),-499);assert(std::string(t)=="0.00");
 weather_gps::hundredths(t,sizeof(t),-500);assert(std::string(t)=="-0.01");
 weather_gps::hundredths(t,sizeof(t),-18000000);assert(std::string(t)=="-180.00");
 weather_gps::hundredths(t,sizeof(t),5);assert(std::string(t)=="0.00");
 weather_gps::Spot at{true,4250833,-7090000,1};weather_gps::query(t,sizeof(t),at);assert(std::string(t)=="lat=42.51&lon=-70.90");
 assert(weather_gps::usable(at,1+weather_gps::kFreshMs-1)&&!weather_gps::usable(at,1+weather_gps::kFreshMs)&&!weather_gps::usable({},5));
 // Asks sooner when the weather shown is the city's or 3 km away, but not more than every 2 minutes.
 const weather_gps::Spot city{},near{true,4251733,-7090000,1},far{true,4253533,-7090000,1};
 assert(weather_gps::askSooner(at,city,10,false,0)&&!weather_gps::askSooner(at,near,10,false,0)&&weather_gps::askSooner(at,far,10,false,0));
 assert(!weather_gps::askSooner(at,city,10,true,5)&&weather_gps::askSooner(at,city,5+weather_gps::kNudgeGapMs,true,5));
 assert(!weather_gps::askSooner({},city,10,false,0));

 // Pulse holds the GPS while it's on screen, and not while the Lab or Mesh has the Cap.
 gWeatherSchedule.asked(nowMs);servicePulseGps(nowMs);assert(gGpsHolders==kGpsForPulse&&holds==1&&feeds==1);
 servicePulseGps(nowMs);assert(holds==1&&feeds==2);
 gUiMode=UiMode::LabDevices;gLabOpen=true;servicePulseGps(nowMs);assert(!gGpsHolders&&releases==1&&feeds==2);
 gLabOpen=false;gUiMode=UiMode::Mesh;gMeshBusOwned=true;servicePulseGps(nowMs);assert(holds==1);gMeshBusOwned=false;
 gUiMode=UiMode::Home;servicePulseGps(nowMs);assert(gGpsHolders==kGpsForPulse&&holds==2);
 // No fix: the saved city as before, and nothing hurried.
 hear(4250833,-7090000,false);servicePulseGps(nowMs);assert(!gWeatherSchedule.due(nowMs+1));
 assert(weatherQuery(nowMs)=="place=Salem%2C%20MA");
 // A fix: ask now, by position, and once it's answered, stop hurrying.
 hear(4250833,-7090000);servicePulseGps(nowMs);assert(gWeatherSchedule.due(nowMs));
 assert(weatherQuery(nowMs)=="lat=42.51&lon=-70.90");
 gUnits=units::System::Metric;assert(weatherQuery(nowMs)=="lat=42.51&lon=-70.90&units=metric");gUnits=units::System::US;answered=0;gWeather=WeatherReport();applyWeatherResponse(200,"{}");
 assert(answered==1&&gWeather.valid&&!gWeatherSchedule.due(nowMs+1));
 nowMs+=weather_gps::kNudgeGapMs;hear(4251733,-7090000);servicePulseGps(nowMs);assert(!gWeatherSchedule.due(nowMs+1));  // 1 km: not worth it
 hear(4253533,-7090000);servicePulseGps(nowMs);assert(gWeatherSchedule.due(nowMs));            // 3 km: worth it
 // A failed answer isn't for anywhere, and hurrying waits its 2 minutes.
 assert(weatherQuery(nowMs)=="lat=42.54&lon=-70.90");applyWeatherResponse(-1,"");assert(answered==1);
 servicePulseGps(nowMs+1);assert(!gWeatherSchedule.due(nowMs+2));
 // An old position gives way to the city again.
 nowMs+=weather_gps::kFreshMs+1;gUiMode=UiMode::LabDevices;servicePulseGps(nowMs);assert(!gGpsHolders);
 assert(weatherQuery(nowMs)=="place=Salem%2C%20MA");gUiMode=UiMode::Home;
 gUnits=units::System::Metric;assert(weatherQuery(nowMs)=="place=Salem%2C%20MA&units=metric");gUnits=units::System::US;
 // Back to the plain city state for the tests below.
 gWeatherHere=weather_gps::Spot{};gWeatherShownAt=weather_gps::Spot{};gWeatherAsking=weather_gps::Spot{};gWeather=WeatherReport();gWeatherNote="Getting weather";
 gWeatherSchedule=Schedule{};nowMs=100000;
}
int main(){
 testGpsWeather();
 // Places.
 assert(std::string(kDefaultPlace)=="Salem, MA"&&validPlace(kDefaultPlace));
 for(const char*good:{"Salem","St. Louis, MO","O'Fallon, IL","Winston-Salem","New York","10001"})assert(validPlace(good));
 for(const char*bad:{"","a"," Salem",",MA","123","Sal;em","x<y","a/b"})assert(!validPlace(bad));
 assert(!validPlace(nullptr));assert(!validPlace(std::string(kMaxPlaceChars+1,'a').c_str()));
 // Pictures: full size, more than one colour, and colours the 8-bit canvas shows unchanged.
 std::set<const void*> pictures;std::set<int> tints;std::set<char> known;
 for(const char*name:{"sun","moon","partly","cloud","fog","rain","snow","storm"}){
  Icon icon=iconFromWire(name);const char*const*rows=picture(icon);pictures.insert(rows);std::set<char> used;
  for(int r=0;r<16;++r){assert(std::string(rows[r]).size()==16);for(int c=0;c<16;++c)if(rows[r][c]!='.'){used.insert(rows[r][c]);known.insert(rows[r][c]);}}
  assert(used.size()>=2);uint16_t t=tint(icon);
  tints.insert(((t>>8)&0xE0)|((t>>6)&0x1C)|((t>>3)&0x03));assert(t!=0);}
 assert(pictures.size()==8&&tints.size()>=6&&known.size()==9);
 std::set<uint16_t> inks;for(char name:known)inks.insert(ink(name));assert(inks.size()==known.size());
 // The 8-bit canvas keeps three bits of red and green and two of blue. Each colour comes back
 // unchanged from that, so what's drawn here is what's on screen.
 const auto wide=[](int v){return (((v<<3)+v)<<2)+(v>>1);};
 for(uint16_t c:inks){assert(color(wide(c>>13),wide((c>>8)&7),((c>>3)&3)*0x55)==c);}
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
 applyWeatherResponse(200,"{}");applyWeatherResponse(404,"{\"error\":\"unknown_place\"}");
 assert(!gWeather.valid&&gWeatherNote=="Check your city");
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

 // Typing a city: capitals, spaces and commas are text, and anything else is ignored.
 gPlaceEditing=true;gPlaceDraft="";type("Bos;ton, M<A");assert(gPlaceDraft=="Boston, MA");
 handlePlaceEditKey('\b');assert(gPlaceDraft=="Boston, M");handlePlaceEditKey('A');
 gPlaceDraft="";for(int i=0;i<60;++i)handlePlaceEditKey('a');assert(gPlaceDraft.size()==kMaxPlaceChars);
 // Keys that are shortcuts elsewhere are just letters here.
 gPlaceDraft="";int before=saves;type("rstzx");assert(gPlaceDraft=="rstzx"&&saves==before&&gPlaceEditing);
 // Nothing usable can't be saved, and it says what's wanted.
 gPlaceDraft="   ";handlePlaceEditKey('\n');assert(gPlaceEditing&&saves==before&&gDeviceSettings.weatherPlace=="Salem, MA");
 assert(statusText=="Type a city, like Salem, MA");
 // Saving a new one trims it, fetches straight away and clears the old place.
 applyWeatherResponse(200,"{}");gPlaceDraft="  Boston, MA ";handlePlaceEditKey('\n');
 assert(!gPlaceEditing&&gDeviceSettings.weatherPlace=="Boston, MA"&&saves==before+1&&!gWeather.valid&&gWeatherSchedule.due(nowMs));
 assert(gWeatherNote=="Getting weather");
 // The same city changes nothing.
 applyWeatherResponse(200,"{}");gPlaceEditing=true;gPlaceDraft="Boston, MA";before=saves;handlePlaceEditKey('\n');
 assert(!gPlaceEditing&&saves==before&&gWeather.valid&&!gWeatherSchedule.due(nowMs));
 std::cout<<"PASS weather: schedule, failures keep the last report, city entry, colour pictures, panel bounds, and weather at the Cap's GPS position\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-weather-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(src), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
