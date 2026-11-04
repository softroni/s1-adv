#!/usr/bin/env python3
"""Actual worker/decoder lifecycle against deterministic TCP, scheduler and PCM slots."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1];src=root/'bots/CardputerADV/src'
production=(src/'internet_radio_transport.cpp').read_text()
start=production.index('#include <Arduino.h>');end=production.index('namespace internet_radio {',start)
stubs=r'''
#include <vector>
#include <deque>
#include <string>
#include <fstream>
#include <iterator>
#include <functional>
#include <cassert>
#include <algorithm>
#include <cstdio>
uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
constexpr int MALLOC_CAP_8BIT=1;
bool allocateFails=false,createFails=false,connectFails=false;
void*heap_caps_malloc(size_t n,int){return allocateFails?nullptr:malloc(n);}
size_t heap_caps_get_largest_free_block(int){return 100000;}
struct{int getFreeHeap(){return 150000;}}ESP;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
constexpr int pdPASS=1;
void(*scheduled)(void*)=nullptr;
int xTaskCreate(void(*f)(void*),const char*,int n,void*,int,void*){assert(n==24576||n==12288);if(createFails)return 0;assert(!scheduled);scheduled=f;return pdPASS;}
void vTaskDelete(void*){}
struct IPAddress{
 uint8_t a[4]={};IPAddress()=default;IPAddress(uint8_t x,uint8_t y,uint8_t z,uint8_t w){a[0]=x;a[1]=y;a[2]=z;a[3]=w;}
 bool fromString(const char*s){unsigned x,y,z,w;char extra;if(sscanf(s,"%u.%u.%u.%u%c",&x,&y,&z,&w,&extra)!=4||x>255||y>255||z>255||w>255)return false;*this=IPAddress(x,y,z,w);return true;}
 uint8_t operator[](int i)const{return a[i];}
};
using err_t=int;constexpr int ERR_OK=0,ERR_INPROGRESS=1;
struct ip_addr_t{uint32_t value=0x08080808;};
#define IP_IS_V4(x) true
#define ip_2_ip4(x) (x)
#define ip4_addr_get_u32(x) ((x)->value)
uint32_t lwip_ntohl(uint32_t x){return x;}
int dns_gethostbyname(const char*,ip_addr_t*addr,void(*)(const char*,const ip_addr_t*,void*),void*){addr->value=0x08080808;return ERR_OK;}
int tcpip_callback(void(*f)(void*),void*p){f(p);return ERR_OK;}
std::string response,request;std::function<void()>readHook;
std::deque<std::string> replies;std::vector<std::string> requests;  // One reply per connection, when set.
struct WiFiClient{
 size_t pos=0;bool open=false;
 int fd(){return 123;}
 void setTimeout(int){}
 bool connect(IPAddress,uint16_t,int timeout){assert(timeout==600);pos=0;open=!connectFails;if(open&&!replies.empty()){response=replies.front();replies.pop_front();}return open;}
 size_t write(const uint8_t*p,size_t n){request.assign((const char*)p,n);assert(request.find("Authorization:")==std::string::npos);return n;}
 int available(){return open?int(response.size()-pos):0;}
 bool connected(){return open&&pos<response.size();}
 int read(uint8_t*p,size_t n){n=std::min(n,response.size()-pos);memcpy(p,response.data()+pos,n);pos+=n;if(readHook)readHook();return int(n);}
 void stop(){open=false;}
};
constexpr int MSG_DONTWAIT=1;
int send(int fd,const void*p,size_t n,int flags){assert(fd==123&&flags==MSG_DONTWAIT);request.assign((const char*)p,n);requests.push_back(request);assert(request.find("Authorization:")==std::string::npos);return int(n);}
uint64_t hashPcm(const int16_t*p,size_t n){uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;++i){h^=uint16_t(p[i]);h*=1099511628211ULL;}return h;}
struct Frame{const int16_t*data;size_t n;uint64_t hash;uint32_t end;};
struct FakeSpeaker{
 std::deque<Frame>queue;unsigned rendered=0;size_t maxQueued=0;
 size_t isPlaying(uint8_t channel){assert(channel==7);return queue.size();}
 uint16_t loudest=0;internet_radio::Visuals seen{};
 bool playRaw(const int16_t*p,size_t n,uint32_t hz,bool stereo,int repeat,uint8_t channel,bool stop){
  loudest=std::max(loudest,internet_radio::level());internet_radio::Visuals v{};if(internet_radio::visuals(v))seen=v;
  assert(channel==7&&!stop&&repeat==1&&queue.size()<2&&hz==44100&&stereo);
  assert(n>0&&n<=2304);for(auto&f:queue)assert(f.data!=p);
  uint32_t end=(queue.empty()?clockMs:queue.back().end)+uint32_t(n*1000/(hz*2));queue.push_back({p,n,hashPcm(p,n),end});maxQueued=std::max(maxQueued,queue.size());return true;
 }
 void advance(){while(!queue.empty()&&clockMs>=queue.front().end){auto&f=queue.front();assert(f.hash==hashPcm(f.data,f.n));queue.pop_front();++rendered;}}
};
struct{FakeSpeaker Speaker;}M5Cardputer;
void vTaskDelay(unsigned n){clockMs+=n;M5Cardputer.Speaker.advance();}
'''
# The production worker is compiled unchanged except platform includes are
# replaced with test doubles. Parser/decoder/cancel/cleanup logic stays actual.
code=production[:start]+stubs+production[end:]+r'''
std::string chunks(const std::string&s){std::string w;for(size_t pos=0;pos<s.size();pos+=333){auto b=s.substr(pos,333);char h[16];snprintf(h,sizeof(h),"%zx\r\n",b.size());w+=h;w+=b;w+="\r\n";}return w+"0\r\n\r\n";}
void execute(){assert(scheduled);auto f=scheduled;scheduled=nullptr;f(nullptr);assert(!internet_radio::busy()&&M5Cardputer.Speaker.queue.empty());}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream in(argv[1],std::ios::binary);std::string mp3((std::istreambuf_iterator<char>(in)),{});assert(mp3.size()>10000);
 const std::string header="HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n";
 response=header+"\r\n"+mp3;
 assert(internet_radio::start("http://radio.example/stream"));assert(internet_radio::busy());assert(!internet_radio::start("http://radio.example/second"));execute();
 assert(internet_radio::error()==internet_radio::Error::None&&internet_radio::bytes()==mp3.size());
 assert(M5Cardputer.Speaker.rendered>30&&M5Cardputer.Speaker.maxQueued==2);
 // Real MP3 carried inside ICY metadata inside HTTP chunks; parser order must
 // dechunk first and strip metadata second, byte-perfect to the real decoder.
 std::string icy;for(size_t pos=0;pos<mp3.size();pos+=777){auto b=mp3.substr(pos,777);icy+=b;if(b.size()==777){icy+=char(1);icy+="StreamTitle=xxxx";}}
 response=header+"Transfer-Encoding: chunked\r\nicy-metaint: 777\r\n\r\n"+chunks(icy);
 assert(internet_radio::start("http://radio.example/stream"));execute();assert(internet_radio::error()==internet_radio::Error::None&&internet_radio::bytes()==mp3.size());
 // Cancellation before worker begins, and while TCP data arrives, both drain
 // any live PCM before worker-owned buffers and ownership can disappear.
 assert(internet_radio::start("http://radio.example/stream"));internet_radio::stop();assert(internet_radio::busy());execute();assert(internet_radio::state()==internet_radio::State::Idle);
 readHook=[](){internet_radio::stop();};assert(internet_radio::start("http://radio.example/stream"));execute();readHook={};assert(internet_radio::state()==internet_radio::State::Idle);
 allocateFails=true;assert(internet_radio::start("http://radio.example/stream"));execute();assert(internet_radio::error()==internet_radio::Error::NoMemory);allocateFails=false;
 createFails=true;assert(!internet_radio::start("http://radio.example/stream")&&!internet_radio::busy());createFails=false;
 connectFails=true;assert(internet_radio::start("http://radio.example/stream"));execute();assert(internet_radio::error()==internet_radio::Error::Connect);connectFails=false;
 assert(!internet_radio::start("https://radio.example/stream"));
 assert(internet_radio::start("http://127.0.0.1/stream"));execute();assert(internet_radio::error()==internet_radio::Error::PublicHttpOnly);
 for(auto wire:{std::string("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nx"),header+"\r\nnot mp3",header+"Transfer-Encoding: chunked\r\n\r\n3\r\nab"}){
  response=wire;assert(internet_radio::start("http://radio.example/stream"));execute();assert(internet_radio::state()==internet_radio::State::Error);
 }
 using internet_radio::Error;using internet_radio::State;const auto npos=std::string::npos;
 // A song title in the stream reaches the screen, and a new stream starts without one.
 std::string titled;for(size_t pos=0;pos<mp3.size();pos+=777){auto b=mp3.substr(pos,777);titled+=b;if(b.size()==777){std::string m="StreamTitle='Artist - Song';";m.resize(32,'\0');titled+=char(2);titled+=m;}}
 response=header+"icy-metaint: 777\r\nicy-br: 128\r\n\r\n"+titled;M5Cardputer.Speaker.loudest=0;
 assert(internet_radio::start("http://radio.example/stream"));execute();
 assert(M5Cardputer.Speaker.loudest>500&&M5Cardputer.Speaker.loudest<32768&&!internet_radio::level()&&internet_radio::bitrate()==128);
 assert(request.find("Icy-MetaData: 1\r\n")!=npos&&request.find("Accept: audio/mpeg\r\n")!=npos);
 char song[64];assert(internet_radio::title(song,sizeof(song))==13&&std::string(song)=="Artist - Song");
 assert(internet_radio::title(song,5)==4&&std::string(song)=="Arti");
 // Off, no frame is analysed. On, they are, and the fixture's 440 Hz tone tops its own band.
 assert(!M5Cardputer.Speaker.seen.frames);
 internet_radio::wantVisuals(true);response=header+"\r\n"+mp3;assert(internet_radio::start("http://radio.example/stream"));execute();internet_radio::wantVisuals(false);
 { const auto&s=M5Cardputer.Speaker.seen;assert(s.frames>20);int best=0;for(int b=1;b<16;++b)if(s.bands[b]>s.bands[best])best=b;assert(best==4&&s.bands[4]>150);
   internet_radio::Visuals after{};assert(!internet_radio::visuals(after)); }
 response=header+"\r\n"+mp3;assert(internet_radio::start("http://radio.example/stream"));assert(!internet_radio::title(song,sizeof(song)));execute();
 // A search: JSON in chunks, read into results that outlast the worker, on the one worker.
 const std::string body=R"([{"name":"One","url_resolved":"http://one.example/","codec":"MP3","bitrate":128,"tags":"jazz"},{"name":"Two","url_resolved":"http://two.example/","codec":"MP3"}])";
 response="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"+chunks(body);
 assert(internet_radio::startSearch(true,"smooth jazz","US"));assert(internet_radio::searching()&&!internet_radio::foundCount());
 assert(!internet_radio::start("http://radio.example/stream")&&!internet_radio::startSearch(true,"rock"));
 execute();assert(!internet_radio::searching());
 assert(request.rfind("GET /json/stations/search?tag=smooth%20jazz&countrycode=US&codec=MP3",0)==0&&request.find("Host: all.api.radio-browser.info:80\r\n")!=npos);
 assert(request.find("Accept: application/json\r\n")!=npos&&request.find("Icy-MetaData")==npos);
 assert(internet_radio::error()==Error::None&&internet_radio::foundCount()==2);
 assert(std::string(internet_radio::found(0)->name)=="One"&&internet_radio::found(0)->bitrate==128&&!internet_radio::found(2));
 // Playing a found station leaves the list alone; clearing it frees it.
 response=header+"\r\n"+mp3;assert(internet_radio::start("http://one.example/"));execute();assert(internet_radio::foundCount()==2);
 internet_radio::clearFound();assert(!internet_radio::foundCount()&&!internet_radio::found(0));
 // Odd answers are errors with nothing found: MP3 instead of JSON, a cut answer, a busy server.
 for(auto wire:{std::string("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n[]"),std::string("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n[{\"name\":"),std::string("HTTP/1.1 503 Busy\r\n\r\n")}){
  response=wire;assert(internet_radio::startSearch(false,"x"));execute();assert(internet_radio::state()==State::Error&&!internet_radio::foundCount());
 }
 // A stopped search finds nothing. No memory for results, or nothing to search for: refused.
 response="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"+body;
 readHook=[](){internet_radio::stop();};assert(internet_radio::startSearch(true,"jazz"));execute();readHook={};
 assert(internet_radio::state()==State::Idle&&!internet_radio::foundCount());
 internet_radio::clearFound();allocateFails=true;assert(!internet_radio::startSearch(true,"jazz")&&!internet_radio::busy()&&internet_radio::error()==Error::NoMemory);allocateFails=false;
 assert(!internet_radio::startSearch(true,std::string(65,'x').c_str())&&internet_radio::error()==Error::InvalidUrl);
 assert(!internet_radio::startSearch(true,"jazz","usa")&&!internet_radio::busy()&&internet_radio::error()==Error::InvalidUrl);
 assert(internet_radio::startSearch(true,"jazz"));execute();assert(internet_radio::foundCount()==2);internet_radio::clearFound();
 // Logos. Given an address, it asks the image service for a small JPEG of it.
 using internet_radio::LogoResult;
 const std::string jpeg(1700,'\xAB');
 const std::string image="HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: 1700\r\n\r\n"+jpeg;
 auto jsonReply=[](const std::string&b){return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"+b;};
 const char* stream="http://ice2.somafm.com/groovesalad-128-mp3";
 requests.clear();replies={image};assert(internet_radio::startLogo(stream,"Groove Salad","https://radioparadise.com/apple-touch-icon.png"));
 assert(internet_radio::fetchingLogo()&&internet_radio::logoResult()==LogoResult::None&&!internet_radio::start("http://radio.example/stream"));
 execute();{size_t n=0;const uint8_t*p=internet_radio::logoJpeg(n);assert(internet_radio::logoResult()==LogoResult::Found&&p&&n==1700&&p[0]==0xAB);}
 assert(requests.size()==1&&requests[0].rfind("GET /?url=https://radioparadise.com/apple-touch-icon.png&w=60&h=60&fit=cover&output=jpg&q=85 ",0)==0);
 assert(requests[0].find("Host: wsrv.nl:80\r\n")!=npos&&requests[0].find("Accept: image/jpeg\r\n")!=npos&&requests[0].find("Icy-MetaData")==npos);
 internet_radio::clearLogo();{size_t n=1;assert(!internet_radio::logoJpeg(n)&&!n&&internet_radio::logoResult()==LogoResult::None);}
 // Not given: by the stream's address first.
 requests.clear();replies={jsonReply(R"([{"name":"x","url_resolved":"http://ice2.somafm.com/groovesalad-128-mp3","favicon":"https://somafm.com/img/gs.png"}])"),image};
 assert(internet_radio::startLogo(stream,"Groove Salad",""));execute();assert(internet_radio::logoResult()==LogoResult::Found&&requests.size()==2);
 assert(requests[0].rfind("GET /json/stations/byurl?url=http://ice2.somafm.com/groovesalad-128-mp3 ",0)==0&&requests[1].find("url=https://somafm.com/img/gs.png&")!=npos);
 // Then by name, only on the same website: another site's Rock FM isn't this one.
 requests.clear();replies={jsonReply("[]"),jsonReply(R"([{"name":"Rock FM","url_resolved":"https://rockfm-cope.flumotion.com/x","favicon":"https://wrong.example/w.png"},
   {"name":"Rock FM","url_resolved":"http://nashe2.hostingradio.ru/rock","favicon":"https://lh3.example/rock.png"}])"),image};
 assert(internet_radio::startLogo("http://nashe1.hostingradio.ru/rock-128.mp3","Rock FM",""));execute();
 assert(internet_radio::logoResult()==LogoResult::Found&&requests.size()==3&&requests[1].rfind("GET /json/stations/search?name=Rock%20FM&limit=10 ",0)==0&&requests[2].find("url=https://lh3.example/rock.png&")!=npos);
 // Nothing anywhere, or a logo the service can't fetch: Missing, so it isn't asked again.
 replies={jsonReply("[]"),jsonReply(R"([{"name":"Rock FM","url_resolved":"https://other.example/","favicon":"https://other.example/o.png"}])")};
 assert(internet_radio::startLogo("http://nashe1.hostingradio.ru/rock-128.mp3","Rock FM",""));execute();assert(internet_radio::logoResult()==LogoResult::Missing);
 replies={"HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{}"};
 assert(internet_radio::startLogo(stream,"G","https://dead.example/x.png"));execute();assert(internet_radio::logoResult()==LogoResult::Missing);
 // A busy service, a lost connection, an answer too big or not a JPEG: Failed, to try later.
 for(const std::string&wire:{std::string("HTTP/1.1 503 Busy\r\n\r\n"),std::string("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: 20000\r\n\r\n")+std::string(20000,'x'),
   std::string("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html>"),std::string("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: 1700\r\n\r\nshort")}){
  replies={wire};assert(internet_radio::startLogo(stream,"G","https://a.example/x.png"));execute();assert(internet_radio::logoResult()==LogoResult::Failed);
 }
 connectFails=true;assert(internet_radio::startLogo(stream,"G",""));execute();assert(internet_radio::logoResult()==LogoResult::Failed);connectFails=false;
 // Stopped, it's undecided. Bad input is refused before anything is sent.
 replies={image};readHook=[](){internet_radio::stop();};assert(internet_radio::startLogo(stream,"G","https://a.example/x.png"));execute();readHook={};
 assert(internet_radio::logoResult()==LogoResult::None);
 assert(!internet_radio::startLogo(stream,"G","ftp://a.example/x.png")&&internet_radio::error()==Error::InvalidUrl);
 assert(!internet_radio::startLogo("","G","")&&!internet_radio::startLogo(stream,std::string(80,'n').c_str(),""));
 replies.clear();response=header+"\r\n"+mp3;
 puts("PASS actual radio worker: bounded TCP/ICY/chunked MP3 decode, song titles, searches and logos on the same worker, two safe PCM slots, async cancel and release, allocation/task/connect/malformed/public-IP failures (ASan/UBSan)");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-radio-worker-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:duration=1.2:sample_rate=44100','-ac','2','-c:a','libmp3lame','-b:a','128k',str(p/'test.mp3')],check=True)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(src),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'test.mp3')],check=True,timeout=90)
