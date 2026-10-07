#include "monitor_snapshot.h"
#include <algorithm>
#include <map>
#include <string>

namespace endfield::platform {
namespace {
std::map<std::wstring,std::pair<std::wstring,std::wstring>> display_identities() {
    std::map<std::wstring,std::pair<std::wstring,std::wstring>> result;
    // Topology can change between sizing and reading. Bound retries and use
    // automatic monitor fallback if a virtual display does not expose an ID.
    for(unsigned attempt=0;attempt<3;++attempt) {
        UINT32 pathCount{},modeCount{};
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS ||
           pathCount>256 || modeCount>1024) return result;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER) continue;
        if(status!=ERROR_SUCCESS) return result;
        paths.resize(pathCount);
        for(const auto& path:paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS ||
               DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS || !target.monitorDevicePath[0]) continue;
            // A mirrored source can describe several physical targets. Keep one
            // deterministic identity for the single desktop monitor rectangle.
            auto value=std::make_pair(std::wstring(target.monitorDevicePath),std::wstring(target.monitorFriendlyDeviceName));
            auto [found,inserted]=result.emplace(source.viewGdiDeviceName,value);
            if(!inserted && value.first<found->second.first) found->second=std::move(value);
        }
        break;
    }
    return result;
}
struct Inventory {
    const std::map<std::wstring,std::pair<std::wstring,std::wstring>>& identities;
    std::vector<NativeMonitor> monitors;
};
BOOL CALLBACK enumerate(HMONITOR monitor,HDC,LPRECT,LPARAM parameter) {
    auto& inventory=*reinterpret_cast<Inventory*>(parameter);
    MONITORINFOEXW info{};info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&info))) return TRUE;
    NativeMonitor entry;entry.handle=monitor;
    auto& descriptor=entry.descriptor;
    descriptor.bounds={info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right,info.rcMonitor.bottom};
    descriptor.work={info.rcWork.left,info.rcWork.top,info.rcWork.right,info.rcWork.bottom};
    descriptor.primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;
    auto identity=inventory.identities.find(info.szDevice);
    if(identity!=inventory.identities.end()) {
        descriptor.stable_id=identity->second.first;descriptor.name=identity->second.second;entry.persistent_identity=true;
    } else {
        descriptor.stable_id=L"transient:"+std::wstring(info.szDevice);descriptor.name=info.szDevice;
    }
    // GetDpiForMonitor is not valid on a PMv2 thread. A hidden owned PMv2 HWND
    // obtains the actual per-monitor value without showing or capturing pixels.
    HWND dpiWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"EndfieldHUD DPI inventory",
        WS_POPUP,info.rcMonitor.left,info.rcMonitor.top,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const UINT dpi=dpiWindow?GetDpiForWindow(dpiWindow):0;
    if(dpiWindow) DestroyWindow(dpiWindow);
    descriptor.dpi_x=descriptor.dpi_y=dpi; // Zero explicitly means unavailable.
    inventory.monitors.push_back(std::move(entry));return TRUE;
}
}
std::vector<NativeMonitor> collect_monitors() {
    const auto identities=display_identities();Inventory inventory{identities,{}};
    if(!EnumDisplayMonitors(nullptr,nullptr,enumerate,reinterpret_cast<LPARAM>(&inventory))) return {};
    return inventory.monitors;
}
}
