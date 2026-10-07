#include "frozen_desktop_snapshot.h"

#include <cstring>
#include <new>

namespace endfield::platform {
bool FrozenDesktopSnapshot::valid_bounds(MonitorRectangle bounds) noexcept {
    if(!bounds.valid())return false;
    const std::uint64_t width=std::int64_t(bounds.right)-bounds.left,height=std::int64_t(bounds.bottom)-bounds.top;
    return width<=maximum_side&&height<=maximum_side&&width*height<=maximum_bytes/4;
}
HRESULT FrozenDesktopSnapshot::from_bgra(MonitorRectangle bounds,std::vector<std::uint8_t> pixels,FrozenSnapshot& output,FrozenPixelOrigin origin) {
    output.reset();
    if(!valid_bounds(bounds))return E_INVALIDARG;
    if(origin!=FrozenPixelOrigin::syntheticEncodedSdr&&origin!=FrozenPixelOrigin::unverifiedGdiSdr)return E_INVALIDARG;
    const std::size_t expected=static_cast<std::size_t>(std::int64_t(bounds.right)-bounds.left)*
        static_cast<std::size_t>(std::int64_t(bounds.bottom)-bounds.top)*4;
    if(pixels.size()!=expected)return E_INVALIDARG;
    for(std::size_t index=3;index<pixels.size();index+=4)if(pixels[index]!=255)return E_INVALIDARG;
    try {output=FrozenSnapshot(new FrozenDesktopSnapshot(bounds,std::move(pixels),origin));return S_OK;}
    catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
}
namespace {
HRESULT failure() {const auto error=GetLastError();return HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE);}
bool current_monitor_matches(MonitorRectangle bounds) {
    const RECT rectangle{bounds.left,bounds.top,bounds.right,bounds.bottom};
    const HMONITOR monitor=MonitorFromRect(&rectangle,MONITOR_DEFAULTTONULL);
    MONITORINFO info{sizeof(info)};
    return monitor&&GetMonitorInfoW(monitor,&info)&&info.rcMonitor.left==bounds.left&&info.rcMonitor.top==bounds.top&&
        info.rcMonitor.right==bounds.right&&info.rcMonitor.bottom==bounds.bottom;
}
HRESULT require_sdr_monitor(MonitorRectangle bounds) {
    const RECT rectangle{bounds.left,bounds.top,bounds.right,bounds.bottom};
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    const HMONITOR handle=MonitorFromRect(&rectangle,MONITOR_DEFAULTTONULL);
    if(!handle||!GetMonitorInfoW(handle,&monitor))return HRESULT_FROM_WIN32(ERROR_INVALID_MONITOR_HANDLE);
    try {
        for(unsigned retry=0;retry<3;++retry) {
            UINT32 pathCount{},modeCount{};LONG status=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);
            if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
            if(!pathCount||pathCount>64||modeCount>256)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
            if(status==ERROR_INSUFFICIENT_BUFFER)continue;
            if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
            unsigned matches{};
            for(UINT32 index=0;index<pathCount;++index) {
                const auto& path=paths[index];DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
                if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||!MonitorPolicy::same_id(source.viewGdiDeviceName,monitor.szDevice))continue;
                ++matches;
                if(!path.targetInfo.targetAvailable||path.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER||
                   path.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST||
                   path.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED||
                   path.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
                color.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,sizeof(color),path.targetInfo.adapterId,path.targetInfo.id};
                status=DisplayConfigGetDeviceInfo(&color.header);
                if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
                if(color.advancedColorEnabled)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            // Clone, virtual or unmapped providers cannot establish this
            // selected opaque SDR surface. Fail to the explicit tint fallback.
            return matches==1?S_OK:HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        return HRESULT_FROM_WIN32(ERROR_RETRY);
    }catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
}
struct DpiScope {
    DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}
};
struct DisplayDC {HDC value=GetDC(nullptr);~DisplayDC(){if(value)ReleaseDC(nullptr,value);}};
struct MemoryDC {HDC value{};explicit MemoryDC(HDC source):value(CreateCompatibleDC(source)){}~MemoryDC(){if(value)DeleteDC(value);}};
struct Bitmap {HBITMAP value{};~Bitmap(){if(value)DeleteObject(value);}};
struct Selection {
    HDC dc{};HGDIOBJ previous{};
    ~Selection(){if(previous&&previous!=HGDI_ERROR)SelectObject(dc,previous);}
};
} // namespace
HRESULT capture_desktop_pre_open(HWND hiddenHud,MonitorRectangle bounds,std::stop_token cancellation,FrozenSnapshot& output) {
    output.reset();
    if(cancellation.stop_requested())return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if(!FrozenDesktopSnapshot::valid_bounds(bounds)||!hiddenHud||!IsWindow(hiddenHud))return E_INVALIDARG;
    if(IsWindowVisible(hiddenHud))return HRESULT_FROM_WIN32(ERROR_BUSY);
    DpiScope dpi;if(!dpi.previous)return failure();
    if(!current_monitor_matches(bounds))return HRESULT_FROM_WIN32(ERROR_INVALID_MONITOR_HANDLE);
    HRESULT colorStatus=require_sdr_monitor(bounds);if(FAILED(colorStatus))return colorStatus;
    DisplayDC screen;if(!screen.value)return failure();
    MemoryDC memory(screen.value);if(!memory.value)return failure();
    const auto width=static_cast<unsigned>(std::int64_t(bounds.right)-bounds.left);
    const auto height=static_cast<unsigned>(std::int64_t(bounds.bottom)-bounds.top);
    BITMAPINFO description{};description.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    description.bmiHeader.biWidth=static_cast<LONG>(width);description.bmiHeader.biHeight=-static_cast<LONG>(height);
    description.bmiHeader.biPlanes=1;description.bmiHeader.biBitCount=32;description.bmiHeader.biCompression=BI_RGB;
    void* bits{};Bitmap bitmap;bitmap.value=CreateDIBSection(screen.value,&description,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap.value||!bits)return failure();
    Selection selected{memory.value,SelectObject(memory.value,bitmap.value)};
    if(!selected.previous||selected.previous==HGDI_ERROR)return failure();
    if(cancellation.stop_requested())return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if(IsWindowVisible(hiddenHud))return HRESULT_FROM_WIN32(ERROR_BUSY);
    if(!BitBlt(memory.value,0,0,static_cast<int>(width),static_cast<int>(height),screen.value,bounds.left,bounds.top,SRCCOPY|CAPTUREBLT))return failure();
    if(!GdiFlush())return failure();
    if(cancellation.stop_requested())return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if(!IsWindow(hiddenHud)||IsWindowVisible(hiddenHud))return HRESULT_FROM_WIN32(ERROR_BUSY);
    if(!current_monitor_matches(bounds))return HRESULT_FROM_WIN32(ERROR_INVALID_MONITOR_HANDLE);
    colorStatus=require_sdr_monitor(bounds);if(FAILED(colorStatus))return colorStatus;
    try {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
        std::memcpy(pixels.data(),bits,pixels.size());
        // GDI's BI_RGB reserved byte is not alpha. The desktop snapshot is an
        // opaque surface; normalize coverage rather than interpreting garbage.
        for(unsigned row=0;row<height;++row) {
            if(cancellation.stop_requested())return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            for(unsigned column=0;column<width;++column)pixels[(static_cast<std::size_t>(row)*width+column)*4+3]=255;
        }
        if(!IsWindow(hiddenHud)||IsWindowVisible(hiddenHud)||!current_monitor_matches(bounds))return HRESULT_FROM_WIN32(ERROR_RETRY);
        if(cancellation.stop_requested())return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        const HRESULT result=FrozenDesktopSnapshot::from_bgra(bounds,std::move(pixels),output,FrozenPixelOrigin::unverifiedGdiSdr);
        if(cancellation.stop_requested()){output.reset();return HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        return result;
    } catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
}
} // namespace endfield::platform
