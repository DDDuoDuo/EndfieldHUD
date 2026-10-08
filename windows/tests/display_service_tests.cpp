#include "native/display_service.hpp"
#include <iostream>
#include <stdexcept>
using namespace endfield::native;
namespace {
unsigned checks{};
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void run(){
    // Synthetic pixel coordinates only: tests do not enumerate or change the
    // user's real monitors, DPI awareness, window placement or preferences.
    std::vector<DisplayDescriptor> displays{
        {u"DISPLAY#LEFT#GUID",u"左屏",1,{-2560,0,0,1440},{-2560,0,0,1400},false},
        {u"DISPLAY#MAIN#GUID",u"Main",2,{0,0,3840,2160},{0,0,3840,2120},true},
        {{},u"Remote",3,{0,-1080,1920,0},{0,-1080,1920,0},false}};
    DisplayPreference preference;
    check(resolveDisplay(preference,displays,{-1,30})==0,"Negative desktop origins select the pointer monitor");
    check(resolveDisplay(preference,displays,{0,30})==1,"A shared edge belongs to exactly one monitor");
    check(resolveDisplay(preference,displays,{10,-100})==2,"A provider without a persistent ID remains usable");
    check(resolveDisplay(preference,displays,{9999,9999})==1,"Pointer outside all surfaces falls back to primary");
    preference.pointerDisplay=false;
    check(resolveDisplay(preference,displays,{-1,30})==1,"Main-display preference ignores the pointer");
    preference.persistentID=u"display#left#guid";
    check(resolveDisplay(preference,displays,{10,30})==0,"Saved identity comparison ignores ASCII device-path case");
    const auto saved=preference.persistentID;displays.erase(displays.begin());
    check(resolveDisplay(preference,displays,{10,-100})==1,"Unplugged fixed display falls back to pointer despite main mode");
    check(preference.persistentID==saved,"Temporary fallback never overwrites saved choice");
    check(resolveDisplay(preference,displays,{9999,9999})==0,"Unplugged fixed display falls back to primary outside desktop");
    displays.push_back({u"DISPLAY#LEFT#GUID",u"Left reconnected",45,{-1920,0,0,1080},{-1920,0,0,1040},false});
    check(resolveDisplay(preference,displays,{10,30})==2,"Reconnect restores fixed choice even with a new handle and dimensions");
    displays[0].primary=false;preference.persistentID.reset();
    check(resolveDisplay(preference,displays,{9999,9999})==0,"Missing primary selects first usable surface");
    displays[0].bounds={0,0,0,0};
    check(resolveDisplay(preference,displays,{0,0})==1,"Invalid or disconnected surface cannot block opening");
    check(!resolveDisplay(preference,{},{}),"No display returns no target without fabricated dimensions");
    check(!DisplayBounds{0,0,100,100}.contains({100,50})&&DisplayBounds{0,0,100,100}.contains({99,50}),"Right and bottom edges follow native half-open rectangles");
}
}
int main(){try{run();std::cout<<checks<<" display policy checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
