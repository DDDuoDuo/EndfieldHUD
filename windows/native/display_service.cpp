#include "native/display_service.hpp"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <system_error>

namespace endfield::native {
bool DisplayBounds::valid()const noexcept{return right>left&&bottom>top;}
bool DisplayBounds::contains(DisplayPoint p)const noexcept{
    return valid()&&p.x>=left&&p.x<right&&p.y>=top&&p.y<bottom;
}
namespace {
bool sameID(std::u16string_view a,std::u16string_view b)noexcept{
    if(a.empty()||a.size()!=b.size())return false;
    // PnP device paths use ASCII GUID/hardware identifiers; non-ASCII provider
    // suffixes are retained literally rather than passed through a locale.
    const auto fold=[](char16_t c){return c>=u'A'&&c<=u'Z'?char16_t(c+u'a'-u'A'):c;};
    for(std::size_t i=0;i<a.size();++i)if(fold(a[i])!=fold(b[i]))return false;
    return true;
}
}
std::optional<std::size_t> resolveDisplay(const DisplayPreference& preference,
    std::span<const DisplayDescriptor> displays,DisplayPoint pointer)noexcept{
    if(preference.persistentID)for(std::size_t i=0;i<displays.size();++i)
        if(displays[i].bounds.valid()&&sameID(*preference.persistentID,displays[i].persistentID))return i;
    if(preference.persistentID||preference.pointerDisplay)for(std::size_t i=0;i<displays.size();++i)
        if(displays[i].bounds.contains(pointer))return i;
    for(std::size_t i=0;i<displays.size();++i)if(displays[i].bounds.valid()&&displays[i].primary)return i;
    for(std::size_t i=0;i<displays.size();++i)if(displays[i].bounds.valid())return i;
    return {};
}
} // namespace endfield::native

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace endfield::native {
namespace {
std::u16string text(std::wstring_view value){
    static_assert(sizeof(wchar_t)==sizeof(char16_t));
    return {reinterpret_cast<const char16_t*>(value.data()),value.size()};
}
struct DisplayPath {std::wstring source;std::u16string identity,name;};
std::vector<DisplayPath> activePaths(){
    constexpr UINT32 flags=QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE;
    for(unsigned attempt=0;attempt<3;++attempt){
        UINT32 pathCount{},modeCount{};
        auto result=GetDisplayConfigBufferSizes(flags,&pathCount,&modeCount);
        // Non-WDDM and remote sessions may not expose CCD. EnumDisplayMonitors
        // is still the authority for their usable surface.
        if(result!=ERROR_SUCCESS)return {};
        if(pathCount>256||modeCount>2048)throw std::runtime_error("Display topology exceeds bounds");
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        result=QueryDisplayConfig(flags,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER)continue; // unplug race; bounded retry
        if(result!=ERROR_SUCCESS)return {};
        std::vector<DisplayPath> out;out.reserve(pathCount);
        for(UINT32 i=0;i<pathCount;++i){
            const auto& path=paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||
               DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS)continue;
            source.viewGdiDeviceName[CCHDEVICENAME-1]=0;
            target.monitorDevicePath[127]=0;target.monitorFriendlyDeviceName[63]=0;
            out.push_back({source.viewGdiDeviceName,text(target.monitorDevicePath),text(target.monitorFriendlyDeviceName)});
        }
        return out;
    }
    return {};
}
DisplayBounds bounds(const RECT& r){return {r.left,r.top,r.right,r.bottom};}
struct Enumeration {
    const std::vector<DisplayPath>& paths;
    std::vector<DisplayDescriptor> displays;
    std::exception_ptr failure;
};
BOOL CALLBACK visit(HMONITOR monitor,HDC,LPRECT,LPARAM context)noexcept{
    auto& state=*reinterpret_cast<Enumeration*>(context);
    try{
        if(state.displays.size()>=64)throw std::runtime_error("Display count exceeds bounds");
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(!GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&info)))return TRUE; // disconnected during enumeration
        DisplayDescriptor display;
        display.handle=reinterpret_cast<std::uintptr_t>(monitor);
        display.bounds=bounds(info.rcMonitor);display.workArea=bounds(info.rcWork);
        display.primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;
        if(!display.bounds.valid())return TRUE;
        const auto path=std::find_if(state.paths.begin(),state.paths.end(),[&](const auto& p){
            return CompareStringOrdinal(p.source.c_str(),-1,info.szDevice,-1,TRUE)==CSTR_EQUAL;});
        if(path!=state.paths.end()){display.persistentID=path->identity;display.name=path->name;}
        if(display.name.empty())display.name=text(info.szDevice);
        state.displays.push_back(std::move(display));return TRUE;
    }catch(...){state.failure=std::current_exception();return FALSE;}
}
}
std::vector<DisplayDescriptor> readConnectedDisplays(){
    if(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())!=DPI_AWARENESS_PER_MONITOR_AWARE)
        throw std::runtime_error("Display pixels require a per-monitor-aware caller");
    const auto paths=activePaths();Enumeration state{paths,{},{}};
    if(!EnumDisplayMonitors(nullptr,nullptr,visit,reinterpret_cast<LPARAM>(&state))){
        if(state.failure)std::rethrow_exception(state.failure);
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Enumerate displays");
    }
    return std::move(state.displays);
}
} // namespace endfield::native
#else
namespace endfield::native {
std::vector<DisplayDescriptor> readConnectedDisplays(){throw std::runtime_error("Native display enumeration requires Windows");}
}
#endif
