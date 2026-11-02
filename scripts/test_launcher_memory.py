#!/usr/bin/env python3
"""The menu reopens on what you last used, after coming back to Pulse or chat and after a restart."""
from pathlib import Path
import os
import re
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


def block(text, marker):
    start = text.index(marker)
    end, depth = text.index('{', start) + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + ';\n'


table = (parts / '004_main.cpp.inc').read_text()
setui = function('void setUiMode(')
tail = setui[setui.index('if (gLauncherSelection >= 0 && gLauncherSelection < kLauncherGroupCount'):setui.index('setStatus(String("Mode: ")')]
load = function('void loadDeviceSettings()')
restore = load[load.index('gLauncherSelection = min<int>(gPrefs.getUChar("menu_group"'):load.index('gLauncherLastSub[gLauncherSelection] = gLauncherSubSelection;') + len('gLauncherLastSub[gLauncherSelection] = gLauncherSubSelection;')]

code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
using std::min;using std::max;
constexpr uint16_t rgb565_local(int,int,int){return 0;}
constexpr uint8_t kLauncherGroupCount=8,kLauncherMaxSubItems=8;
''' + block((parts / '002_main.cpp.inc').read_text(), 'enum class UiMode : uint8_t') + \
    block(table, 'struct LauncherSubItem {') + block(table, 'struct LauncherGroup {') + block(table, 'constexpr LauncherGroup kLauncherGroups') + r'''
template<class T>T clampValue(T x,T lo,T hi){return std::max(lo,std::min(x,hi));}
int gLauncherSelection=0,gLauncherSubSelection=0;uint8_t gLauncherLastSub[kLauncherGroupCount]={};
bool gRealtimeActive=false,gRecording=false,gMeshBusOwned=false,gPendingRealtimeStart=false,gRealtimeScreenVisible=false,gRealtimeExclusiveMode=false;
UiMode gUiMode=UiMode::Home;std::map<std::string,int> flash;int writes=0;
struct{void begin(const char*,bool){}void end(){}void putUChar(const char*k,uint8_t v){++writes;flash[k]=v;}
 uint8_t getUChar(const char*k,uint8_t d){return flash.count(k)?flash[k]:d;}}gPrefs;
constexpr const char* kPrefsDeviceNs="device_cfg";
void setStatus(const char*,int){}void closeRealtimeSession(const char*){}void cancelRecordingDiscard(){}void startRealtimeSession(bool){}
''' + function('bool findLauncherEntryForMode(') + function('bool launcherKeepsPlaceFor(') + function('void syncLauncherToMode(') + \
    function('void saveLauncherSelection()') + r'''
void setUiMode(UiMode mode){gUiMode=mode;
''' + tail + r'''}
''' + function('void applyLauncherSelection()') + r'''
void pick(int group,int item){gLauncherSelection=group;gLauncherSubSelection=item;applyLauncherSelection();}
const char* chosen(){return kLauncherGroups[gLauncherSelection].items[gLauncherSubSelection].label;}
bool on(const char*label){return !std::strcmp(chosen(),label);}
void reboot(){gLauncherSelection=0;gLauncherSubSelection=0;std::fill(std::begin(gLauncherLastSub),std::end(gLauncherLastSub),0);
 ''' + restore + r'''
}
int main(){
 // Open Snake, go back to Pulse, and the menu is still on Snake.
 pick(4,4);assert(gUiMode==UiMode::Snake&&on("Snake"));
 setUiMode(UiMode::Home);assert(on("Snake"));
 // Switching between Pulse and chat isn't choosing from the menu either.
 setUiMode(UiMode::ChatFull);assert(on("Snake"));setUiMode(UiMode::Home);assert(on("Snake"));
 setUiMode(UiMode::Clock);assert(on("Snake"));
 // But picking chat from the menu is.
 pick(0,2);assert(on("AI Chat"));setUiMode(UiMode::Home);assert(on("AI Chat"));
 // An app opened some other way, like a chat command, counts as used.
 setUiMode(UiMode::WifiSettings);assert(on("Wi-Fi"));setUiMode(UiMode::Home);assert(on("Wi-Fi"));
 // A restart keeps it.
 pick(5,3);assert(on("Ball Catch"));reboot();assert(on("Ball Catch")&&gLauncherLastSub[5]==3);
 // Reopening the same thing doesn't wear out flash.
 const int before=writes;pick(5,3);pick(5,3);assert(writes==before);pick(5,2);assert(writes==before+2);
 // A first boot starts at the top, and a saved row that no longer exists can't crash the menu.
 flash.clear();reboot();assert(gLauncherSelection==0&&gLauncherSubSelection==0);
 flash["menu_group"]=200;flash["menu_item"]=200;reboot();
 assert(gLauncherSelection==kLauncherGroupCount-1&&gLauncherSubSelection==kLauncherGroups[gLauncherSelection].count-1);
 std::cout<<"PASS menu memory: survives Pulse, chat and restarts, without wearing flash\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-launcher-memory-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
