#!/usr/bin/env python3
"""The menu reopens on what you last used, after coming back to Pulse or chat and after a restart, Favorites too.

Tab opens it on the last app used, never one in Settings or System."""
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
# Tab and Ctrl+L open the menu on the last app, from every screen.
typing = function('void handleTypingKey(')
tab_block = typing[typing.index('  if (keys.tab) {'):]
assert re.search(r'\} else \{\s+openLauncherOnLastApp\(\);', tab_block[:tab_block.index('return;')]), 'Tab'
assert re.search(r'if \(gLauncherVisible\) gLauncherVisible = false;\s+else openLauncherOnLastApp\(\);', typing), 'Ctrl+L'
assistant = function('bool handleAssistantMenuKey(')
assert 'else openLauncherOnLastApp();' in assistant[assistant.index("lower=='l'"):] and assistant.count('openLauncherOnLastApp();') == 2
setui = function('void setUiMode(')
tail = setui[setui.index('if (gLauncherSelection >= 0 && gLauncherSelection < kLauncherGroupCount'):setui.index('setStatus(String("Mode: ")')]
load = function('void loadDeviceSettings()')
favorites = table[table.index('static_assert(launcher_favorites::kMax'):table.index('Preferences gPrefs;')]
restore = load[load.index('launcher_favorites::read(gFavorites'):load.index('rememberLauncherApp();') + len('rememberLauncherApp();')]

code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <new>
#include "launcher_favorites.h"
#include "launcher_order.h"
using std::min;using std::max;
constexpr uint16_t rgb565_local(int,int,int){return 0;}
constexpr uint8_t kLauncherGroupCount=11,kLauncherMaxSubItems=9,kFavoritesGroup=0,kSettingsGroup=8,kSystemGroup=9;
''' + block((parts / '002_main.cpp.inc').read_text(), 'enum class UiMode : uint8_t') + \
    block(table, 'struct LauncherSubItem {') + block(table, 'struct LauncherGroup {') + block(table, 'constexpr LauncherGroup kLauncherGroups') + \
    favorites + r'''
template<class T>T clampValue(T x,T lo,T hi){return std::max(lo,std::min(x,hi));}
int gLauncherSelection=0,gLauncherSubSelection=0;uint8_t gLauncherLastSub[kLauncherGroupCount]={};
int gLauncherLastUsedTab=-1;LauncherSubItem gLauncherLastUsed;bool gLauncherVisible=false,gScreensaverActive=false;
bool gRealtimeActive=false,gRecording=false,gMeshBusOwned=false,gPendingRealtimeStart=false,gRealtimeScreenVisible=false,gRealtimeExclusiveMode=false;
UiMode gUiMode=UiMode::Home;std::map<std::string,int> flash;std::map<std::string,std::string> texts;int writes=0;
struct{void begin(const char*,bool){}void end(){}void putUChar(const char*k,uint8_t v){++writes;flash[k]=v;}
 void putString(const char*k,const char*v){++writes;texts[k]=v;}
 uint8_t getUChar(const char*k,uint8_t d){return flash.count(k)?flash[k]:d;}
 std::string getString(const char*k,const char*d){return texts.count(k)?texts[k]:d;}}gPrefs;
constexpr const char* kPrefsDeviceNs="device_cfg";
void setStatus(const char*,int){}void closeRealtimeSession(const char*){}void cancelRecordingDiscard(){}void startRealtimeSession(bool){gRealtimeActive=true;}
''' + function('bool findLauncherEntryForMode(') + function('bool launcherKeepsPlaceFor(') + \
    function('bool launcherAppInSettingsOrSystem(') + function('int launcherRowOf(') + function('void rememberLauncherApp()') + \
    function('bool findLastLauncherApp(') + function('void syncLauncherToMode(') + \
    function('void saveLauncherSelection()') + function('void saveLauncherOrder()') + \
    function('int launcherRowFor(') + function('void showLauncherGroup(') + function('void openLauncherOnLastApp()') + \
    function('void saveFavorites()') + function('void favoritesChanged()') + function('void moveLauncherRow(') + \
    function('void launcherPlaceFromLayout1(') + r'''
void setUiMode(UiMode mode){gUiMode=mode;
''' + tail + r'''}
''' + function('void applyLauncherSelection()') + r'''
void moveRow(int g,int row,int d){assert(launcher_order::move(gLauncherOrder[g],launcherGroup(g).count,row,d));rebuildLauncherTab(g);saveLauncherOrder();}
void pick(int group,int item){gLauncherSelection=group;gLauncherSubSelection=item;applyLauncherSelection();}
const char* chosen(){return launcherGroup(gLauncherSelection).items[gLauncherSubSelection].label;}
bool on(const char*label){return !std::strcmp(chosen(),label);}
void tab(){gLauncherVisible=false;openLauncherOnLastApp();assert(gLauncherVisible);}
void reboot(){gLauncherSelection=0;gLauncherSubSelection=0;gLauncherLastUsedTab=-1;std::fill(std::begin(gLauncherLastSub),std::end(gLauncherLastSub),0);
 gFavorites=launcher_favorites::List();rebuildFavoritesTab();
 ''' + restore + r'''
}
void tabSkipsSettingsAndSystem(){
 texts.clear();flash.clear();reboot();
 // Tab opens on the last app used, wherever the menu was moved to since.
 pick(2,6);assert(on("Internet Radio"));gLauncherSelection=6;gLauncherSubSelection=1;tab();assert(gLauncherSelection==2&&on("Internet Radio"));
 // Not on Settings or System, from the menu or another way, but on the app before them.
 pick(8,2);assert(gUiMode==UiMode::WifiSettings&&on("Wi-Fi"));tab();assert(on("Internet Radio"));
 pick(9,1);assert(on("Logs"));setUiMode(UiMode::Home);tab();assert(on("Internet Radio"));
 setUiMode(UiMode::ScreenSettings);assert(on("Display"));tab();assert(on("Internet Radio")&&gLauncherLastSub[2]==6);
 // Coming back to Pulse doesn't count, but opening it from the menu does.
 setUiMode(UiMode::Home);tab();assert(on("Internet Radio"));pick(1,1);setUiMode(UiMode::Snake);setUiMode(UiMode::Home);tab();assert(on("Snake"));
 // A screensaver isn't using an app, Chess Mind's included.
 gScreensaverActive=true;setUiMode(UiMode::ChessMind);gScreensaverActive=false;setUiMode(UiMode::Clock);tab();assert(on("Snake"));
 // The last app is what a restart brings back, never Settings: opening one writes nothing.
 pick(6,4);int before=writes;pick(8,0);assert(writes==before);reboot();assert(on("Snake"));tab();assert(on("Snake"));
 // An older firmware saved Settings or System: the menu starts there, but Tab opens on AI Agent.
 flash.erase("menu_layout");flash["menu_tab"]=8;flash["menu_item"]=0;reboot();assert(gLauncherSelection==9);tab();assert(gLauncherSelection==1);
 flash.erase("menu_layout");flash["menu_tab"]=7;reboot();setUiMode(UiMode::Home);tab();assert(gLauncherSelection==1);
 // 0.2.315 put House after Tools: a place saved before it moves to where its app is now.
 flash.clear();flash["menu_tab"]=4;flash["menu_item"]=8;reboot();assert(gLauncherSelection==5&&on("TV Remote"));tab();assert(on("TV Remote"));
 flash.clear();flash["menu_tab"]=4;flash["menu_item"]=4;reboot();assert(gLauncherSelection==5&&on("smartLamp"));
 flash.clear();flash["menu_tab"]=4;flash["menu_item"]=5;reboot();assert(gLauncherSelection==4&&on("Sonar"));
 flash.clear();flash["menu_tab"]=5;flash["menu_item"]=1;reboot();assert(gLauncherSelection==6&&on("NES"));
 flash.clear();flash["menu_tab"]=2;flash["menu_item"]=7;reboot();assert(gLauncherSelection==2&&on("Podcasts"));
 // One saved by this firmware is taken as it is, and saving marks it so.
 flash.clear();flash["menu_layout"]=2;flash["menu_tab"]=5;flash["menu_item"]=0;reboot();assert(on("Cameras"));
 flash.clear();pick(5,1);assert(flash["menu_layout"]==2&&flash["menu_tab"]==5&&flash["menu_item"]==1);
 flash.clear();texts.clear();reboot();
 // From Favorites: on that row, and a Settings app there counts as Settings.
 texts["favs"]="Settings/Wi-Fi\nGames/Snake\n";flash.clear();reboot();
 pick(0,1);assert(on("Snake"));pick(0,0);assert(gUiMode==UiMode::WifiSettings);tab();assert(gLauncherSelection==0&&on("Snake"));
 // A favorite, used and then taken off Favorites, is found in its own tab.
 texts["favs"]="Settings/Wi-Fi\n";reboot();gLauncherLastUsedTab=0;gLauncherLastUsed=kLauncherGroups[6].items[4];tab();assert(gLauncherSelection==6&&on("Snake"));
 // Saved on a favorite that is now a Settings one, the menu starts on it, and Tab opens on AI Agent.
 reboot();assert(gLauncherSelection==0&&on("Wi-Fi")&&gLauncherLastUsedTab==-1);tab();assert(gLauncherSelection==1);
 // An app moved within its tab is still found by name.
 texts.clear();flash.clear();reboot();pick(6,4);moveRow(6,4,-1);tab();assert(gLauncherSubSelection==3&&on("Snake"));
 moveRow(6,3,1);texts.clear();
 // Moving the last app with Fn saves its new row too, so a restart finds it, not its neighbour.
 flash.clear();reboot();pick(6,4);gLauncherSubSelection=4;moveLauncherRow(-1);assert(on("Snake")&&flash["menu_item"]==3);
 reboot();assert(on("Snake"));tab();assert(gLauncherSelection==6&&on("Snake"));
 // Moving its neighbour does too, and moving apps in another tab writes nothing.
 gLauncherSubSelection=2;moveLauncherRow(1);assert(launcherRowOf(6,kLauncherGroups[6].items[4])==2&&flash["menu_item"]==2);
 int was=writes;gLauncherSelection=7;gLauncherSubSelection=0;moveLauncherRow(1);assert(writes==was+1);  // Only the order.
 reboot();tab();assert(gLauncherSelection==6&&on("Snake"));
 // The same on Favorites: moved, then taken off, it comes back where it is after a restart.
 texts.clear();flash.clear();reboot();texts["favs"]="Games/Tetris\nGames/Snake\n";reboot();
 pick(0,1);assert(on("Snake"));gLauncherSubSelection=1;moveLauncherRow(-1);assert(flash["menu_tab"]==0&&flash["menu_item"]==0);
 reboot();tab();assert(gLauncherSelection==0&&on("Snake"));
 gFavorites.removeAt(0);favoritesChanged();assert(flash["menu_tab"]==6);reboot();tab();assert(gLauncherSelection==6&&on("Snake"));
 texts.clear();
}
int main(){
 tabSkipsSettingsAndSystem();texts.clear();flash.clear();reboot();writes=0;
 // Open Snake, go back to Pulse, and the menu is still on Snake.
 pick(6,4);assert(gUiMode==UiMode::Snake&&on("Snake"));
 setUiMode(UiMode::Home);assert(on("Snake"));
 // Switching between Pulse and chat isn't choosing from the menu either.
 setUiMode(UiMode::ChatFull);assert(on("Snake"));setUiMode(UiMode::Home);assert(on("Snake"));
 setUiMode(UiMode::Clock);assert(on("Snake"));
 // But picking chat from the menu is.
 pick(1,2);assert(on("AI Chat"));setUiMode(UiMode::Home);assert(on("AI Chat"));
 // An app opened some other way, like a chat command, counts as used.
 setUiMode(UiMode::WifiSettings);assert(on("Wi-Fi"));setUiMode(UiMode::Home);assert(on("Wi-Fi"));
 // A restart keeps it.
 pick(7,3);assert(on("Ball Catch"));reboot();assert(on("Ball Catch")&&gLauncherLastSub[7]==3);
 // Reopening the same thing doesn't wear out flash.
 const int before=writes;pick(7,3);pick(7,3);assert(writes==before);pick(7,2);assert(writes==before+2);
 // A first boot starts on AI Agent, and a saved row that no longer exists can't crash the menu.
 flash.clear();reboot();assert(gLauncherSelection==1&&gLauncherSubSelection==0);
 flash["menu_tab"]=200;flash["menu_item"]=200;reboot();
 assert(gLauncherSelection==kLauncherGroupCount-1&&gLauncherSubSelection==kLauncherGroups[gLauncherSelection].count-1);
 // Before Favorites, the tab was saved counting from AI Agent: Games was 4 and is 5 now.
 flash.clear();flash["menu_group"]=4;flash["menu_item"]=2;reboot();assert(gLauncherSelection==6&&on("Checkers"));
 flash["menu_group"]=200;reboot();assert(gLauncherSelection==kLauncherGroupCount-1);
 pick(6,1);assert(flash["menu_tab"]==6);flash["menu_group"]=0;reboot();assert(on("NES"));

 // Favorites come back after a restart, in their order, from their names.
 flash.clear();texts["favs"]="Games/Snake\nMedia/Internet Radio\nSettings/Pomodoro\nAI Agent/Realtime\n";reboot();
 assert(gFavorites.count==4&&gFavoritesTab.count==4&&gLauncherSelection==1);
 assert(!std::strcmp(gFavoritesTab.items[0].label,"Snake")&&gFavoritesTab.items[2].mode==UiMode::FocusSettings);
 // Opening one keeps the menu on Favorites, after the app opens and after going back to Pulse.
 pick(0,1);assert(gUiMode==UiMode::Radio&&gLauncherSelection==0&&on("Internet Radio"));
 setUiMode(UiMode::Home);assert(gLauncherSelection==0&&on("Internet Radio"));
 pick(0,2);assert(gUiMode==UiMode::FocusSettings&&gLauncherSelection==0&&gLauncherSubSelection==2);
 // Realtime from Favorites starts the session and the menu stays where it was used.
 pick(0,3);assert(gUiMode==UiMode::Face&&gRealtimeActive&&gLauncherSelection==0&&gLauncherSubSelection==3);
 gRealtimeActive=false;
 // And it's still there after a restart.
 pick(0,0);reboot();assert(gLauncherSelection==0&&on("Snake"));
 // The same app opened from its own tab moves the menu there.
 pick(6,4);assert(gLauncherSelection==6&&on("Snake"));
 // Something that isn't a favorite, opened some other way, finds its own tab.
 pick(0,0);setUiMode(UiMode::Calculator);assert(gLauncherSelection==4&&on("Calculator"));
 // A saved Favorites row that has gone, or an empty Favorites tab, can't crash the menu.
 flash["menu_tab"]=0;flash["menu_item"]=7;reboot();assert(gLauncherSelection==0&&gLauncherSubSelection==3);
 texts.clear();reboot();assert(gLauncherSelection==0&&gLauncherSubSelection==0&&gFavoritesTab.count==0);
 const UiMode was=gUiMode;applyLauncherSelection();assert(gUiMode==was);

 // A tab's new order comes back after a restart, and the menu reopens on the same app.
 texts.clear();flash.clear();reboot();
 moveRow(6,4,-1);moveRow(6,3,-1);  // Snake up two, to third in Games
 assert(texts["menu_order"]=="Games/Tetris\nGames/NES\nGames/Snake\nGames/Checkers\nGames/Minesweeper\n");
 pick(6,2);assert(gUiMode==UiMode::Snake&&on("Snake"));
 reboot();assert(gLauncherSelection==6&&gLauncherSubSelection==2&&on("Snake"));
 assert(!std::strcmp(launcherGroup(6).items[3].label,"Checkers")&&!std::strcmp(kLauncherGroups[6].items[2].label,"Checkers"));
 // An app opened some other way finds its row as it is now.
 pick(1,0);gRealtimeActive=false;setUiMode(UiMode::Minesweeper);assert(gLauncherSelection==6&&gLauncherSubSelection==4&&on("Minesweeper"));
 setUiMode(UiMode::Checkers);assert(gLauncherSubSelection==3&&on("Checkers"));
 // Favorites name apps by the table, so moving one in its tab changes nothing there.
 texts["favs"]="Games/Checkers\n";reboot();assert(gFavorites.count==1&&!std::strcmp(gFavoritesTab.items[0].label,"Checkers"));
 moveRow(6,3,-1);assert(!std::strcmp(gFavoritesTab.items[0].label,"Checkers")&&!std::strcmp(launcherGroup(6).items[2].label,"Checkers"));
 // Back in the table's order, nothing is kept for that tab.
 moveRow(6,2,1);moveRow(6,2,1);moveRow(6,3,1);assert(texts["menu_order"]=="");
 // Two tabs at once, each its own lines.
 moveRow(7,0,1);moveRow(10,2,-1);reboot();
 assert(!std::strcmp(launcherGroup(7).items[0].label,"Flappy Bird")&&!std::strcmp(launcherGroup(10).items[1].label,"Sequences"));
 assert(!std::strcmp(launcherGroup(6).items[0].label,"Tetris"));
 std::cout<<"PASS menu memory: survives Pulse, chat and restarts, Favorites and tab orders too, without wearing flash; Tab skips Settings and System\n";
}
'''
with tempfile.TemporaryDirectory(prefix='s1-launcher-memory-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(src), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
