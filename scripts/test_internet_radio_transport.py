#!/usr/bin/env python3
"""Compile production HTTP/ICY parsers and pinned MP3 decoder under ASan/UBSan."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1];src=root/'bots/CardputerADV/src'
code=r'''
#include "internet_radio_transport.h"
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "vendor/minimp3.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>
using namespace internet_radio;using namespace internet_radio::detail;
Headers header(const std::vector<std::string>&lines){Headers h;for(auto&s:lines)assert(h.line(s.c_str())==Error::None);return h;}
std::string parse(const std::string&wire,const Headers&h){Body body(h);std::string audio;for(unsigned char c:wire){int r=body.feed(c);assert(r>=0);if(r)audio+=char(c);}assert(body.complete());return audio;}
std::string chunk(const std::string&entity,size_t size){std::string out;for(size_t p=0;p<entity.size();p+=size){auto s=entity.substr(p,size);char hex[20];snprintf(hex,sizeof(hex),"%zx\r\n",s.size());out+=hex;out+=s;out+="\r\n";}return out+"0\r\n\r\n";}
int main(int argc,char**argv){
 assert(argc==3);Url u;
 assert(parseUrl("http://ice2.somafm.com/a?b=1",u)&&std::string(u.host)=="ice2.somafm.com"&&std::string(u.path)=="/a?b=1"&&u.port==80);
 assert(parseUrl("http://example.org:8000/stream",u)&&u.port==8000);
 for(auto s:{"https://example.com/x","http://u:p@example.com/x","http://example.com:0/x","http://example.com:65536/x","http://[::1]/x","http://a/evil\r\nHost:x","http://a/x#y","http://a/x y","http:///x","http://a/x\\y"})assert(!parseUrl(s,u));
 assert(publicIpv4(0x08080808)&&publicIpv4(0x5db8d822));
 for(uint32_t ip:{0U,0x7f000001U,0x0a000001U,0xc0a80101U,0xac100001U,0xa9fe0101U,0xe0000001U,0x64400001U,0xc6336401U})assert(!publicIpv4(ip));
 auto h=header({"HTTP/1.1 200 OK","Content-Type: audio/mpeg",""});
 assert(parse("audio123",h)=="audio123");
 auto c=header({"ICY 200 OK","Content-Type: audio/mpeg","Transfer-Encoding: chunked",""});
 for(unsigned n=1;n<100;++n)assert(parse(chunk("abcdefghijklmno",n),c)=="abcdefghijklmno");
 // Metadata length and content may cross any TCP/chunk boundary. Produce
 // exact audio and never leak arbitrary station metadata into decoder bytes.
 auto m=header({"HTTP/1.0 200 OK","Content-Type: audio/mpeg","icy-metaint: 3",""});
 std::string entity="abc";entity+=char(1);entity+="StreamTitle=xxxx";assert(entity.size()==20);entity+="def";entity+=char(0);entity+="gh";
 assert(parse(entity,m)=="abcdefgh");
 auto cm=header({"HTTP/1.1 200 OK","Content-Type: audio/mpeg","Transfer-Encoding: chunked","icy-metaint: 3",""});
 for(unsigned n=1;n<40;++n)assert(parse(chunk(entity,n),cm)=="abcdefgh");
 Headers length=header({"HTTP/1.1 200 OK","Content-Type: audio/mp3","Content-Length: 3",""});
 Body bounded(length);assert(bounded.feed('a')==1&&!bounded.complete());assert(bounded.feed('b')==1);assert(bounded.feed('c')==1&&bounded.complete()&&bounded.done());assert(bounded.feed('d')<0);
 for(auto lines:std::vector<std::vector<std::string>>{{"HTTP/1.1 200 OK","Content-Type: text/html"},{"HTTP/1.1 200 OK","Content-Type: audio/aac"},{"HTTP/1.1 200 OK","Content-Type: audio/mpeg","Transfer-Encoding: gzip"},{"HTTP/1.1 200 OK","Content-Type: audio/mpeg","Content-Length: -1"},{"HTTP/1.1 200 OK","Content-Type: audio/mpeg","icy-metaint: 0"},{"HTTP/1.1 200 OK","Content-Type: audio/mpeg","Content-Length: 1","Transfer-Encoding: chunked",""},{"HTTP/1.1 200 OK",""},{"HTTP/1.1 403 Nope",""}}){
  Headers bad;Error e=Error::None;for(auto&s:lines){e=bad.line(s.c_str());if(e!=Error::None)break;}assert(e!=Error::None);
 }
 // Redirects can legitimately return HTML; no HTML is decoded. The worker
 // validates each next URL and destination's public IP before reconnecting.
 auto redirect=header({"HTTP/1.1 302 Found","Content-Type: text/html","Location: http://example.org/radio",""});assert(redirect.status==302);
 for(auto wire:{"X\r\n","1;extension=x\r\na\r\n0\r\n\r\n","1\rXa","1000001\r\n"}){Body b(c);bool bad=false;for(unsigned char ch:std::string(wire))if(b.feed(ch)<0){bad=true;break;}assert(bad);}
 Body incomplete(c);for(unsigned char ch:std::string("3\r\nab"))assert(incomplete.feed(ch)>=0);assert(!incomplete.complete());
 Body metadata(m);for(unsigned char ch:std::string("abc\1xx",6))assert(metadata.feed(ch)>=0);assert(!metadata.complete());
 // Malformed byte-level payload fuzz cannot escape bounded parser state.
 uint32_t random=49;for(int test=0;test<20000;++test){Body b(test&1?cm:c);for(int i=0;i<500;++i){random=random*1664525U+1013904223U;if(b.feed(random>>24)<0||b.done())break;}}
 // Song titles ride in the metadata blocks, which can cross any chunk or read boundary. A
 // block without a title keeps the last; a title with an apostrophe ends at "';", not "'".
 {
  auto meta=[](const std::string&text){std::string b=text;b.resize((b.size()+15)/16*16,'\0');return std::string(1,char(b.size()/16))+b;};
  // Audio, a block with a title, audio, a block without one, audio, an empty block, audio.
  std::string wire="abc"+meta("StreamTitle='Shock - Let Your Body Do The Talkin'';StreamUrl='';")+"def"+meta("StreamUrl='x';")+"ghi"+std::string(1,'\0')+"j";
  auto t=header({"HTTP/1.0 200 OK","Content-Type: audio/mpeg","icy-metaint: 3",""});
  {Body b(t);std::string audio;for(unsigned char ch:wire){int r=b.feed(ch);assert(r>=0);if(r)audio+=char(ch);}
   assert(audio=="abcdefghij"&&b.titles()==1&&std::string(b.title())=="Shock - Let Your Body Do The Talkin'");}
  auto tc=header({"HTTP/1.1 200 OK","Content-Type: audio/mpeg","Transfer-Encoding: chunked","icy-metaint: 3",""});
  for(unsigned n=1;n<60;n+=3){Body cb(tc);std::string audio;for(unsigned char ch:chunk(wire,n)){int r=cb.feed(ch);assert(r>=0);if(r)audio+=char(ch);}
   assert(cb.complete()&&audio=="abcdefghij"&&std::string(cb.title())=="Shock - Let Your Body Do The Talkin'");}
  // Cyrillic in UTF-8 stays as it is; an older station's Latin-1 becomes UTF-8; control
  // characters become spaces, and a long title is cut at a whole letter.
  Body ru(t);for(unsigned char ch:"abc"+meta("StreamTitle='\xD0\x9C\xD0\x98\xD0\x9B\xD0\x95\xD0\x9D\xD0\x90 - \xD0\x92 \xD0\xAD\xD1\x82\xD1\x83';"))ru.feed(ch);
  assert(std::string(ru.title())=="МИЛЕНА - В Эту");
  Body latin(t);for(unsigned char ch:"abc"+meta("StreamTitle='Caf\xE9\t del Mar';"))latin.feed(ch);
  assert(std::string(latin.title())=="Café del Mar");
  std::string longTitle="StreamTitle='";for(int i=0;i<200;++i)longTitle+="\xD0\x96";longTitle+="';";
  Body cut(t);for(unsigned char ch:"abc"+meta(longTitle))cut.feed(ch);
  assert(strlen(cut.title())<128&&strlen(cut.title())%2==0&&cut.titles()==1);
  char out[8];assert(cleanText("  a\x01  b  ",10,out,sizeof(out))==3&&std::string(out)=="a b");
  assert(cleanText("abcdefghij",10,out,sizeof(out))==7&&std::string(out)=="abcdefg");
  assert(cleanText("\xD0\x96\xD0\x96\xD0\x96\xD0\x96",8,out,sizeof(out))==6);
  assert(cleanText("",0,out,sizeof(out))==0&&!out[0]);
 }
 // A station's own bitrate, when it gives one. An odd value is just unknown, never an error.
 assert(header({"ICY 200 OK","icy-br: 128","Content-Type: audio/mpeg",""}).bitrate==128);
 assert(header({"ICY 200 OK","icy-br:192,192","Content-Type: audio/mpeg",""}).bitrate==192);
 assert(header({"ICY 200 OK","icy-br: fast","Content-Type: audio/mpeg",""}).bitrate==0);
 assert(header({"ICY 200 OK","icy-br: 99999","Content-Type: audio/mpeg",""}).bitrate==0);
 assert(header({"ICY 200 OK","Content-Type: audio/mpeg",""}).bitrate==0);
 // The search asks only for what this player can play, with the words safely escaped.
 {
  char url[kUrlSize];
  assert(searchUrl(url,sizeof(url),true," smooth jazz ")&&std::string(url)=="http://all.api.radio-browser.info/json/stations/search?tag=smooth%20jazz&codec=MP3&is_https=false&hidebroken=true&order=clickcount&reverse=true&limit=30");
  assert(searchUrl(url,sizeof(url),false,"R&B/1?#")&&std::string(url).find("?name=R%26B%2F1%3F%23&")!=std::string::npos);
  Url u;assert(parseUrl(url,u)&&std::string(u.host)=="all.api.radio-browser.info"&&u.port==80);
  // Nothing typed asks for no tag or name: the directory's most listened, anywhere or in a country.
  assert(searchUrl(url,sizeof(url),true,"   ")&&std::string(url)=="http://all.api.radio-browser.info/json/stations/search?codec=MP3&is_https=false&hidebroken=true&order=clickcount&reverse=true&limit=30");
  assert(searchUrl(url,sizeof(url),false,"","US")&&std::string(url)=="http://all.api.radio-browser.info/json/stations/search?countrycode=US&codec=MP3&is_https=false&hidebroken=true&order=clickcount&reverse=true&limit=30");
  assert(!searchUrl(url,40,true,"jazz"));
  assert(!searchUrl(url,sizeof(url),true,std::string(65,'a').c_str())&&searchUrl(url,sizeof(url),true,std::string(64,'%').c_str()));
  assert(parseUrl(url,u));
  // A country is two capital letters and goes in as the directory's countrycode.
  assert(searchUrl(url,sizeof(url),true,"jazz","GB")&&std::string(url)=="http://all.api.radio-browser.info/json/stations/search?tag=jazz&countrycode=GB&codec=MP3&is_https=false&hidebroken=true&order=clickcount&reverse=true&limit=30");
  assert(searchUrl(url,sizeof(url),false,"bbc","")&&std::string(url).find("countrycode")==std::string::npos);
  assert(searchUrl(url,sizeof(url),false,"bbc",nullptr)&&std::string(url).find("countrycode")==std::string::npos);
  for(const char* bad:{"gb","G","GBR","G1","&x"})assert(!searchUrl(url,sizeof(url),true,"jazz",bad));
  assert(searchUrl(url,sizeof(url),true,std::string(64,'%').c_str(),"US")&&parseUrl(url,u));
 }
 // The directory's answer: keys in any order, nested values, escapes and surrogate pairs. It
 // keeps MP3 on plain HTTP once each, and skips HLS, AAC, https, cut-off and empty entries.
 {
  const std::string json=R"([
 {"changeuuid":"x","name":"Jazz \"Radio\" \u00e9\ud83c\udfb7","url":"http://a/pls","url_resolved":"http:\/\/jazz.example\/live",
  "tags":"jazz,smooth jazz","countrycode":"FR","language":"french","codec":"MP3","bitrate":128,"hls":0,"geo_lat":48.85,
  "lastcheckok":1,"extra":{"a":[1,2,{"b":"}]"}]},"votes":-3},
 {"name":"Same address","url_resolved":"http://jazz.example/live","codec":"MP3","bitrate":64,"hls":0},
 {"name":"HLS","url_resolved":"http://hls.example/x.m3u8","codec":"MP3","hls":1},
 {"name":"AAC","url_resolved":"http://aac.example/x","codec":"AAC+","hls":0},
 {"name":"Secure","url_resolved":"https://secure.example/x","codec":"MP3"},
 {"name":"","url_resolved":"http://noname.example/x","codec":"MP3"},
 {"name":"Long","url_resolved":"http://long.example/)"+std::string(300,'a')+R"(","codec":"MP3"},
 {"name":"Радио Рекорд","url_resolved":"http://record.example/","codec":"mp3","bitrate":3200000,"language":"russian,english","countrycode":"RU","hls":false},
 {"name":"No language","url_resolved":"http://ru.example/","countrycode":"RU","codec":"MP3","bitrate":1.5e2,"hls":null},
 {"name":"lone \ud83c half","url_resolved":"http://half.example/","codec":"","tags":"a\nb"}
])";
  Found found[kMaxFound];StationJson j(found,kMaxFound);
  for(unsigned char ch:json)assert(j.feed(ch));
  assert(j.finished()&&j.count()==4);
  assert(std::string(found[0].name)=="Jazz \"Radio\" é🎷"&&std::string(found[0].url)=="http://jazz.example/live");
  assert(std::string(found[0].tags)=="jazz,smooth jazz"&&std::string(found[0].country)=="FR"&&found[0].bitrate==128&&!found[0].russian);
  assert(std::string(found[1].name)=="Радио Рекорд"&&found[1].russian&&found[1].bitrate==65535);
  assert(std::string(found[2].name)=="No language"&&found[2].russian&&found[2].bitrate==1);
  assert(std::string(found[3].name)=="lone ? half"&&std::string(found[3].tags)=="a b");
  // Only room for what was asked; anything past the array is ignored.
  Found two[2];StationJson k(two,2);for(unsigned char ch:json+"garbage")assert(k.feed(ch));assert(k.finished()&&k.count()==2);
  Found none[1];
  for(const char* empty:{"[]"," [ ] "}){StationJson e(none,1);for(const char*q=empty;*q;++q)assert(e.feed(*q));assert(e.finished()&&!e.count());}
  for(const char* bad:{"{}","[1]","[{]","[{\"a\" 1}]","[{\"a\":1,}]","[{\"a\":\"\\x\"}]","[{\"a\":\"\\u12g4\"}]","[,]","[{},]x"}){
   StationJson e(none,1);bool ok=true;for(const char*q=bad;*q&&ok;++q)ok=e.feed(*q);assert(!ok||!e.finished());
  }
  // Cut off anywhere, it's never finished, so a short read can't pass as a whole answer.
  for(size_t cut=0;cut<json.size();cut+=13){StationJson e(found,kMaxFound);bool ok=true;for(size_t i=0;i<cut&&ok;++i)ok=e.feed(json[i]);assert(!e.finished());}
  // Random bytes and mutated answers stay inside their buffers.
  uint32_t random=7;
  for(int test=0;test<20000;++test){
   StationJson e(found,kMaxFound);std::string m=json;
   for(int k2=0;k2<4;++k2){random=random*1664525U+1013904223U;m[random%m.size()]=char(random>>24);}
   for(unsigned char ch:m)if(!e.feed(ch))break;
   assert(e.count()<=kMaxFound);for(size_t i=0;i<e.count();++i){assert(strlen(found[i].name)<sizeof(found[i].name)&&strlen(found[i].url)<sizeof(found[i].url));Url v;assert(parseUrl(found[i].url,v));}
  }
  for(int test=0;test<2000;++test){StationJson e(found,kMaxFound);for(int i=0;i<600;++i){random=random*1664525U+1013904223U;if(!e.feed(uint8_t(random>>24)))break;}}
  // A search's answer must be JSON; a stream's must be MP3.
  Headers jh;jh.json=true;assert(jh.line("HTTP/1.1 200 OK")==Error::None&&jh.line("Content-Type: application/json; charset=utf-8")==Error::None);
  Headers mh;mh.json=true;assert(mh.line("HTTP/1.1 200 OK")==Error::None&&mh.line("Content-Type: audio/mpeg")==Error::Unsupported);
  Headers ah;assert(ah.line("HTTP/1.1 200 OK")==Error::None&&ah.line("Content-Type: application/json")==Error::Unsupported);
 }
 // The visuals: a tone lights the band its frequency is in, at its level, and bands well
 // away from it stay low. Silence is all zeros. It works at any rate and frame length.
 {
  auto tone=[](double f,double amp,int hz,int n,int ch){std::vector<int16_t>v(size_t(n*ch));for(int i=0;i<n;++i)for(int c=0;c<ch;++c)v[size_t(i*ch+c)]=int16_t(amp*sin(2*M_PI*f*i/hz));return v;};
  auto bandOf=[](double f){int b=0;for(;b<16;++b)if(f<60.0*pow(16000.0/60.0,(b+1)/16.0))break;return b;};
  for(int hz:{44100,48000,32000,22050})for(int ch:{1,2})for(double f:{100.0,440.0,1000.0,5000.0}){
   if(f*2>=hz)continue;
   auto v=tone(f,16000,hz,1152,ch);internet_radio::Visuals out{};analyse(v.data(),1152,ch,hz,out);
   int best=0;for(int b=1;b<16;++b)if(out.bands[b]>out.bands[best])best=b;
   assert(std::abs(best-bandOf(f))<=1);
   assert(out.bands[best]>205&&out.bands[best]<=240);  // A sine at -6 dBFS reads about 229.
   for(int b=0;b<16;++b)if(std::abs(b-best)>3)assert(out.bands[b]<out.bands[best]-60);
   int top=0;for(int8_t w:out.wave)top=std::max(top,std::abs(int(w)));assert(top<=63&&(f>1000||top>40));
  }
  internet_radio::Visuals quiet{};std::vector<int16_t>zero(2304,0);analyse(zero.data(),1152,2,44100,quiet);
  for(auto b:quiet.bands)assert(b==0);for(auto w:quiet.wave)assert(w==0);
  auto shortFrame=tone(1000,8000,24000,576,1);internet_radio::Visuals sf{};analyse(shortFrame.data(),576,1,24000,sf);
  int best=0;for(int b=1;b<16;++b)if(sf.bands[b]>sf.bands[best])best=b;assert(std::abs(best-bandOf(1000))<=1);
  auto lowRate=tone(1000,8000,8000,576,1);internet_radio::Visuals lr{};analyse(lowRate.data(),576,1,8000,lr);
  for(int b=bandOf(4000)+1;b<16;++b)assert(lr.bands[b]==0);  // Nothing above what 8 kHz can hold.
  internet_radio::Visuals none{};analyse(nullptr,0,2,44100,none);for(auto b:none.bands)assert(b==0);
 }
 // Actual pinned decoder, not a decoder stub, on generated mono/stereo MP3.
 for(int file=1;file<argc;++file){std::ifstream in(argv[file],std::ios::binary);std::vector<uint8_t>data((std::istreambuf_iterator<char>(in)),{});assert(data.size()>2000);
  mp3dec_t decoder;mp3dec_init(&decoder);size_t pos=0,frames=0;long long energy=0;int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  while(pos<data.size()){mp3dec_frame_info_t info{};int n=mp3dec_decode_frame(&decoder,data.data()+pos,int(data.size()-pos),pcm,&info);
   assert(info.frame_bytes>=0&&pos+size_t(info.frame_bytes)<=data.size());if(!info.frame_bytes)break;pos+=info.frame_bytes;
   if(n){assert(info.layer==3&&info.hz==44100&&info.channels==file&&n<=1152);++frames;for(int i=0;i<n*info.channels;++i)energy+=pcm[i]<0?-int(pcm[i]):pcm[i];}
  }assert(frames>30&&energy>10000);
 }
 puts("PASS actual radio URL/public-IP/HTTP/chunked/ICY parsers, song titles, search address and JSON answers, malformed input and fuzz cases; pinned MP3 decoder mono/stereo fixtures (ASan/UBSan)");
}
'''
with tempfile.TemporaryDirectory(prefix='s1-radio-transport-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 for channels in [1,2]:
  subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:duration=1.2:sample_rate=44100','-ac',str(channels),'-c:a','libmp3lame','-b:a','128k',str(p/f'{channels}.mp3')],check=True)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-DINTERNET_RADIO_HOST_TEST','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(src),str(p/'test.cpp'),str(src/'internet_radio_transport.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'1.mp3'),str(p/'2.mp3')],check=True,timeout=60)
