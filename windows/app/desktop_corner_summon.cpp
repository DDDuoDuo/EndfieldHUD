#include "app/desktop_corner_summon.hpp"
#include <windows.h>
#include <algorithm>
#include <string_view>
#include <thread>
#include <stdexcept>

namespace endfield::app {
namespace {
bool desktopCorner(POINT point) noexcept {
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONULL),&monitor))return false;
    const auto& r=monitor.rcWork;
    if(!desktopCornerContains(point.x,point.y,r.left,r.top,r.right,r.bottom))return false;
    const auto hit=WindowFromPoint(point);if(!hit)return false;
    const auto root=GetAncestor(hit,GA_ROOT);wchar_t name[64]{};
    if(!GetClassNameW(root,name,64))return false;
    const std::wstring_view rootClass(name);
    if(rootClass!=L"Progman" && rootClass!=L"WorkerW")return false;
    if(hit==root)return true;
    if(!GetClassNameW(hit,name,64))return false;
    const std::wstring_view hitClass(name);
    return hitClass==L"SysListView32" || hitClass==L"SHELLDLL_DefView";
}
}
struct DesktopCornerSummon::Impl {
    HWND window;unsigned message;std::thread thread;DWORD threadID{};HANDLE ready{};bool installed{};
    DesktopCornerGesture gesture;unsigned interval{500};int toleranceX{4},toleranceY{4};static thread_local Impl* current;
    Impl(HWND w,unsigned m):window(w),message(m){
        if(!IsWindow(window)||!message)throw std::invalid_argument("Desktop corner needs a host window");
        ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready)throw std::runtime_error("Desktop corner could not start");
        try{thread=std::thread([this]{run();});}catch(...){CloseHandle(ready);throw;}
        WaitForSingleObject(ready,INFINITE);CloseHandle(ready);ready=nullptr;
        if(!installed){thread.join();throw std::runtime_error("Desktop corner mouse hook unavailable");}
    }
    ~Impl(){if(thread.joinable()){PostThreadMessageW(threadID,WM_QUIT,0,0);thread.join();}}
    static LRESULT CALLBACK mouse(int code,WPARAM message,LPARAM value) noexcept {
        if(code==HC_ACTION && current){
            const auto& event=*reinterpret_cast<const MSLLHOOKSTRUCT*>(value);
            using E=DesktopCornerGesture::Event;
            const auto kind=message==WM_LBUTTONDOWN?E::down:message==WM_LBUTTONUP?E::up:message==WM_MOUSEMOVE?E::move:E::cancel;
            if(kind==E::down){current->interval=GetDoubleClickTime();current->toleranceX=std::max(1,GetSystemMetrics(SM_CXDOUBLECLK)/2);current->toleranceY=std::max(1,GetSystemMetrics(SM_CYDOUBLECLK)/2);}
            const bool eligible=(kind==E::down||kind==E::up) && !(event.flags&LLMHF_INJECTED) && desktopCorner(event.pt);
            if(current->gesture.event((event.flags&LLMHF_INJECTED)?E::cancel:kind,event.pt.x,event.pt.y,event.time,eligible,
                    current->interval,current->toleranceX,current->toleranceY))
                PostMessageW(current->window,current->message,0,0);
        }
        return CallNextHookEx(nullptr,code,message,value);
    }
    void run() noexcept {
        current=this;threadID=GetCurrentThreadId();SetThreadDescription(GetCurrentThread(),L"EndfieldHUD desktop corner");
        MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        const auto hook=SetWindowsHookExW(WH_MOUSE_LL,mouse,GetModuleHandleW(nullptr),0);
        installed=hook!=nullptr;SetEvent(ready);
        if(hook){while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}UnhookWindowsHookEx(hook);}
        current=nullptr;
    }
};
thread_local DesktopCornerSummon::Impl* DesktopCornerSummon::Impl::current=nullptr;
DesktopCornerSummon::DesktopCornerSummon(void* window,unsigned message):impl_(std::make_unique<Impl>(static_cast<HWND>(window),message)){}
DesktopCornerSummon::~DesktopCornerSummon()=default;
}
