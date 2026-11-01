#!/usr/bin/env python3
"""Exercise the real clock sync on host, and the real timezone rules."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parts = root / 'bots/CardputerADV/src/main_parts'
everything = '\n'.join(p.read_text() for p in sorted(parts.glob('*.inc')))
assert 'Bangkok' not in everything
zone = re.search(r'constexpr const char\* kDeviceTz = "[^"]+";', everything).group()
start = everything.index('void syncClockFromNtp(bool force) {')
sync = everything[start:everything.index('\n}\n', start) + 3]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
using String=std::string;
''' + zone + r'''
constexpr uint32_t kTimeSyncRetryMs=15000,kTimeResyncMs=6UL*60UL*60UL*1000UL;
bool gWifiReady=false,gTimeValid=false,validTime=false;
uint32_t gLastTimeSyncAttemptMs=0,gLastTimeSyncMs=0,nowMs=1000;
int asks=0,saves=0;std::string askedZone;String status;
uint32_t millis(){return nowMs;}
bool hasValidSystemTime(){return validTime;}
void ensureDeviceTimezone(){}
void persistRtcFromSystemTime(){++saves;}
void configTzTime(const char*zone,const char*,const char*,const char*){++asks;askedZone=zone;}
void setStatus(const String&text,uint32_t){status=text;}
void logf(const char*,...){}
''' + sync + r'''
void fresh(bool valid){gLastTimeSyncAttemptMs=gLastTimeSyncMs=0;gTimeValid=validTime=valid;asks=saves=0;status.clear();}
time_t utc(int year,int month,int day,int hour,int minute,int second){
 tm t={};t.tm_year=year-1900;t.tm_mon=month-1;t.tm_mday=day;t.tm_hour=hour;t.tm_min=minute;t.tm_sec=second;return timegm(&t);
}
void local(time_t when,int hour,int minute,int second,bool daylight){
 tm t={};localtime_r(&when,&t);assert(t.tm_hour==hour&&t.tm_min==minute&&t.tm_sec==second&&(t.tm_isdst>0)==daylight);
}
int main(){
 // Nothing can be asked without Wi-Fi.
 fresh(true);syncClockFromNtp(true);assert(asks==0);
 // A clock kept across a restart looks valid, and the first connect still asks.
 gWifiReady=true;syncClockFromNtp(false);assert(asks==1&&askedZone==kDeviceTz&&status.empty());
 // Then it rests for six hours, unless Wi-Fi reconnects.
 nowMs+=kTimeResyncMs-1;syncClockFromNtp(false);assert(asks==1);
 nowMs+=1;syncClockFromNtp(false);assert(asks==2);
 nowMs+=1000;syncClockFromNtp(true);assert(asks==3);
 // With no clock at all it asks at once, and an answer is noticed and saved.
 fresh(false);syncClockFromNtp(false);assert(asks==1&&status=="Clock sync...");
 nowMs+=7999;syncClockFromNtp(true);assert(asks==1);
 validTime=true;syncClockFromNtp(false);assert(gTimeValid&&status=="Clock synced"&&saves==1&&asks==1);
 // No answer means asking again.
 fresh(false);syncClockFromNtp(false);assert(asks==1);
 nowMs+=kTimeSyncRetryMs-1;syncClockFromNtp(false);assert(asks==1);
 nowMs+=1;syncClockFromNtp(false);assert(asks==2);
 // Central time, on both sides of both daylight saving changes.
 setenv("TZ",kDeviceTz,1);tzset();
 local(utc(2026,1,15,18,0,0),12,0,0,false);
 local(utc(2026,7,15,18,0,0),13,0,0,true);
 local(utc(2026,3,8,7,59,59),1,59,59,false);
 local(utc(2026,3,8,8,0,0),3,0,0,true);
 local(utc(2026,11,1,6,59,59),1,59,59,true);
 local(utc(2026,11,1,7,0,0),1,0,0,false);
 std::cout<<"PASS actual clock sync: asks on every connect even with a valid clock, six-hour rest, retry and Central daylight saving\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-clock-sync-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
