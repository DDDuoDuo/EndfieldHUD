#include "../platform/monitor_policy.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace endfield::platform;
namespace {
int checks{};
void check(bool valid,const char* message) {
    ++checks; if (!valid) throw std::runtime_error(message);
}
void expect(const std::vector<MonitorDescriptor>& monitors,const MonitorPreference& preference,
            MonitorPoint pointer,std::optional<std::size_t> expected,const char* message,
            std::optional<std::wstring> current={},bool topology=false) {
    check(MonitorPolicy::resolve_index(monitors,preference,pointer,std::move(current),topology)==expected,message);
}
void source_policy() {
    const MonitorDescriptor primary{L"\\\\?\\DISPLAY#PRIMARY",L"Primary fixture",{0,0,1920,1080},{0,0,1920,1040},true,96,96};
    const MonitorDescriptor left{L"\\\\?\\DISPLAY#LEFT",L"Left 150% fixture",{-2560,-200,0,1240},{-2560,-200,0,1200},false,144,144};
    const MonitorDescriptor right{L"\\\\?\\DISPLAY#RIGHT",L"Right 200% fixture",{1920,0,3840,2160},{1920,0,3840,2100},false,192,192};
    std::vector<MonitorDescriptor> monitors{primary,left,right};
    const MonitorPreference active;
    check(active.open_on_active && !active.fixed_id,"Canonical source default selects pointer display");
    expect(monitors,active,{400,400},0,"Pointer resolves primary physical monitor");
    expect(monitors,active,{-1800,-100},1,"Negative physical monitor origin remains selectable");
    expect(monitors,active,{2200,1400},2,"200% monitor selection uses physical coordinates without applying DPI twice");
    expect(monitors,active,{-1800,1220},1,"Desktop uses full display frame below its work area");
    expect(monitors,active,{500,1070},0,"Taskbar work-area subtraction cannot alter full desktop monitor selection");
    expect(monitors,active,{6000,6000},0,"Pointer in display gap falls back to source primary rather than nearest monitor");
    expect(monitors,active,{0,20},0,"Half-open right edge of left monitor belongs to primary");
    expect(monitors,active,{1920,20},2,"Shared physical edge belongs to right monitor");
    expect(monitors,active,{3840,20},0,"Outside max edge falls back to primary");
    expect(monitors,active,{std::numeric_limits<double>::quiet_NaN(),20},0,"Invalid pointer coordinates safely fall back to primary");
    expect(monitors,active,{std::numeric_limits<double>::infinity(),20},0,"Infinite pointer cannot match physical display");
    const MonitorPreference main{{},false};
    expect(monitors,main,{-1800,20},0,"Explicit main-display mode ignores pointer monitor");
    const MonitorPreference fixed{left.stable_id,false};
    expect(monitors,fixed,{2200,1400},1,"Connected fixed display wins over active and primary");
    expect(monitors,{L"\\\\?\\display#left",true},{2200,1400},1,"Stable Windows display device path is case-insensitive");
    expect(monitors,{L"MISSING",false},{2200,1400},2,"Absent fixed display falls back to pointer even with active mode disabled");
    expect(monitors,{L"MISSING",false},{6000,6000},0,"Absent fixed display falls back to primary when pointer is in gap");
    expect(monitors,active,{2200,1400},1,"Visible topology update retains connected current screen",left.stable_id,true);
    expect(monitors,main,{2200,1400},1,"Source visible current screen survives topology update in main-display mode",left.stable_id,true);
    expect(monitors,fixed,{2200,1400},1,"Fixed preference wins over a different visible current screen",right.stable_id,true);
    expect(monitors,active,{2200,1400},2,"New opening resolves pointer instead of retained prior screen",left.stable_id,false);
    expect(monitors,active,{2200,1400},2,"Missing visible current screen recovers to pointer",L"MISSING",true);
    std::vector<MonitorDescriptor> disconnected{primary,right};
    expect(disconnected,fixed,{2200,1400},1,"Unplugged fixed monitor recovers to connected pointer display",left.stable_id,true);
    check(fixed.fixed_id==left.stable_id,"Recovery cannot erase unavailable fixed display preference");
    expect(monitors,fixed,{2200,1400},1,"Reconnecting restores original stable fixed monitor",right.stable_id,true);
    expect(disconnected,active,{500,500},0,"Unplugged active current monitor recovers to valid screen",left.stable_id,true);
    expect(monitors,active,{2200,1400},1,"Changed enumeration order is not stored as current identity",L"\\\\?\\display#left",true);
    std::vector<MonitorDescriptor> reordered{right,primary,left};
    expect(reordered,fixed,{500,500},2,"Fixed identity survives enumeration-order changes");
    expect(reordered,active,{500,500},2,"Visible current identity survives enumeration-order changes",left.stable_id,true);
    expect(reordered,main,{-1800,20},1,"Primary flag controls main mode independent of enumeration order");
    auto changed=monitors;changed[1].bounds={-1920,0,0,1080};changed[1].work={-1920,0,0,1040};changed[1].dpi_x=changed[1].dpi_y=192;
    expect(changed,active,{2200,1400},1,"Monitor move and DPI change retain connected visible source screen",left.stable_id,true);
    check(changed[*MonitorPolicy::resolve_index(changed,fixed,{2200,1400})].bounds.left==-1920,"Resolved viewport uses new topology full physical frame");
    check(changed[1].dpi_x==192 && changed[1].bounds.bottom!=changed[1].work.bottom,"DPI and work bounds stay metadata beside source full frame");
    auto no_primary=monitors;for(auto& monitor:no_primary) monitor.primary=false;
    expect(no_primary,main,{2200,1400},0,"Remote topology without primary falls back to first valid display");
    std::vector<MonitorDescriptor> virtual_display{{L"",L"Remote fixture",{0,0,1280,720},{0,0,1280,680},true,120,120}};
    expect(virtual_display,active,{600,600},0,"Automatic remote display without stable ID remains usable");
    expect(virtual_display,{L"",false},{600,600},0,"Empty saved identity cannot match an empty provider identity but uses pointer fallback");
    check(!MonitorPolicy::same_id(L"",L"") && !MonitorPolicy::same_id(L"LEFT",L"RIGHT"),"Unavailable IDs never become stable identity matches");
    expect({},active,{0,0},{},"No connected screens returns unavailable without invented viewport");
    expect({},fixed,{0,0},{},"No connected screens preserves unavailable fixed intent");
    std::vector<MonitorDescriptor> invalid{{L"INVALID",L"Invalid fixture",{0,0,0,720},{0,0,0,680},true,96,96},right};
    expect(invalid,main,{0,0},1,"Degenerate native snapshot cannot produce zero-size HUD target");
    expect({invalid.front()},active,{0,0},{},"All-invalid display snapshot returns unavailable");
}
} // namespace
int main() {
    try {
        source_policy();
        std::cout<<"PASS: "<<checks<<" isolated source monitor-policy contracts. Live mixed-DPI movement/hotplug remains unverified.\n";
        return 0;
    } catch(const std::exception& failure) {
        std::cerr<<"FAIL: "<<failure.what()<<'\n';return 1;
    }
}
