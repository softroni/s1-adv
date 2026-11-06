#!/usr/bin/env python3
"""Run the actual wake path. Face is only a screen while Realtime shows, so waking never lands on bare eyes."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'bots/CardputerADV/src/main_parts/009_main.cpp.inc').read_text()
def function(signature):
 a=source.index(signature);b=source.index('{',a);depth=1;end=b+1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[a:end]
code=r'''
#include <cassert>
#include <cstdint>
#include <string>
using String=std::string;
enum class UiMode {Home,Clock,Face,ChatFull,CMatrix,ChessMind,Tetris,TvRemote,Cameras};
UiMode gUiMode=UiMode::Tetris,gScreensaverPreviousMode=UiMode::Home;
bool gScreensaverActive=false,gScreensaverDimmed=false,gScreenOffActive=false,gRealtimeScreenVisible=false;
struct {bool screensaverClockOnWake=false;} gDeviceSettings;
void markUserActivity(){}
void applyDisplayBrightness(){}
void setStatus(const String&,uint32_t){}
void setUiMode(UiMode mode){gUiMode=mode;}
'''
code+='\n'+function('bool handleScreensaverWake()')
code+=r'''
UiMode wake(UiMode previous,bool realtime,bool saver=true){
 gUiMode=UiMode::Tetris;gScreensaverPreviousMode=previous;gRealtimeScreenVisible=realtime;
 gScreensaverActive=saver;gScreenOffActive=!saver;
 assert(handleScreensaverWake());
 assert(!gScreensaverActive&&!gScreenOffActive);
 return gUiMode;
}
int main(){
 // C-Matrix and Chess Mind clear the Realtime flag on the way in, leaving Face behind.
 assert(wake(UiMode::Face,false)==UiMode::Home);
 assert(wake(UiMode::Face,false,false)==UiMode::Home);
 // The Big Eyes saver and a dark screen keep Realtime showing underneath.
 assert(wake(UiMode::Face,true)==UiMode::Face);
 assert(wake(UiMode::Face,true,false)==UiMode::Face);
 assert(wake(UiMode::ChatFull,false)==UiMode::ChatFull);
 assert(wake(UiMode::CMatrix,false)==UiMode::Home);
 gDeviceSettings.screensaverClockOnWake=true;
 assert(wake(UiMode::Face,false)==UiMode::Clock);
 gDeviceSettings.screensaverClockOnWake=false;
 gUiMode=UiMode::Tetris;assert(!handleScreensaverWake()&&gUiMode==UiMode::Tetris);
}
'''
with tempfile.TemporaryDirectory() as folder:
 src=Path(folder)/'wake.cpp';exe=Path(folder)/'wake';src.write_text(code)
 subprocess.check_call([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)])
 subprocess.check_call([str(exe)])
print('screensaver wake: ok')
