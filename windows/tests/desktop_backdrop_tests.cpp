#include "native/desktop_backdrop.hpp"
#include "core/source_animation.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <wrl/client.h>
#endif

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::native;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char*message){bool rejected{};try{f();}catch(...){rejected=true;}check(rejected,message);}
void nearValue(double a,double b,double tolerance,const char*message){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance,message);}
Json read(const std::filesystem::path&path){std::ifstream f(path,std::ios::binary);check(bool(f),"Explicit original WatchBlur source opens");const std::string raw((std::istreambuf_iterator<char>(f)),{});return Json::parse(raw,1024*1024);}
void source(const Json&raw){
    const auto animation=DesktopBackdropAnimation::fromSource(raw);
    for(const auto&entry:{std::pair{&animation.entrance,"entrance"},std::pair{&animation.exit,"exit"}}){
        const auto&t=*entry.first;const auto&body=raw[entry.second]["curve"]["raw"]["curve"];
        const auto curve=endfield::core::source::ScalarCurve::fromJson(body);
        nearValue(t.duration,.13333334028720856,0,"Blur uses its own original finite duration");
        check(t.alpha(-1)==t.startAlpha&&t.alpha(1)==t.endAlpha,"Before/after track clamps to authored endpoints");
        double previous=t.alpha(0);
        for(unsigned i=0;i<=2000;++i){const auto time=t.duration*i/2000.;const auto alpha=t.alpha(time);nearValue(alpha,*curve.sample(time),7e-8,"Float Core Animation controls preserve original two-key alpha");check(alpha>=0&&alpha<=1,"Source opacity stays bounded");check(t.endAlpha>t.startAlpha?alpha>=previous:alpha<=previous,"Source fade remains monotonic");previous=alpha;}
        rejects([&]{t.alpha(std::numeric_limits<double>::quiet_NaN());},"Nonfinite transition time rejected");
    }
    check(animation.entrance.controlPoints==std::array<float,4>{1.f/3.f,0,2.f/3.f,1},"Entrance retains exact source Float Bezier control points");
    const auto dark=desktopBackdropStyle(true),light=desktopBackdropStyle(false);
    check(dark.tintWhite==.015&&light.tintWhite==.90,"Source dark/light backdrop whites");
    check(dark.vignetteAlpha==std::array<double,3>{0,.06,.38}&&light.vignetteAlpha==std::array<double,3>{0,.02,.14},"Source dark/light vignette alpha");
    check(dark.vignetteStart==std::array<double,2>{.5,.46}&&dark.vignetteEnd==std::array<double,2>{1,1}&&dark.vignetteLocations==std::array<double,3>{0,.5,1},"Source radial descriptors remain explicit");
    auto invalid=raw;invalid["default_speed"]=2;rejects([&]{DesktopBackdropAnimation::fromSource(invalid);},"Changed wrapper speed is rejected rather than guessed");
    invalid=raw;invalid["entrance"]["curve"]["path"]="other";rejects([&]{DesktopBackdropAnimation::fromSource(invalid);},"Wrong source alpha binding rejected");
    invalid=raw;auto keys=invalid["exit"]["curve"]["raw"]["curve"]["m_Curve"].array();keys[0]["weightedMode"]=1;invalid["exit"]["curve"]["raw"]["curve"]["m_Curve"]=keys;rejects([&]{DesktopBackdropAnimation::fromSource(invalid);},"Weighted tracks cannot be silently converted to unweighted CA controls");
    allocations=0;counting=true;double total{};for(unsigned i=0;i<10000;++i)total+=animation.entrance.alpha((i%1000)*.0002);counting=false;
    check(total>0&&allocations==0,"Caller-clock source sampling allocates no memory");
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
void hr(HRESULT result,const wchar_t*stage=L"Backdrop fixture: COM initialization"){if(FAILED(result))throw winrt::hresult_error(result,stage);}
struct Queue final {
    winrt::Windows::System::DispatcherQueueController controller{nullptr};
    Queue(){check(!winrt::Windows::System::DispatcherQueue::GetForCurrentThread(),"Hidden proof owns its one isolated UI queue");DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};hr(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))),L"Backdrop fixture: create caller-owned DispatcherQueue");}
    void finish(){if(!controller)return;const auto operation=controller.ShutdownQueueAsync();const auto deadline=GetTickCount64()+10000;
        while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){
            check(GetTickCount64()<deadline,"Caller queue shutdown is bounded");MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
            if(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started)MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        operation.GetResults();controller=nullptr;
    }
    ~Queue(){try{finish();}catch(...) {}}
};
struct Window final {
    HWND value{};
    Window(){const auto instance=GetModuleHandleW(nullptr);WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=DefWindowProcW;type.lpszClassName=L"EndfieldHUD.HiddenBackdropProof";
        const auto registered=RegisterClassW(&type);check(registered||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Register hidden backdrop test class");
        value=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Synthetic hidden backdrop API proof",WS_POPUP,0,0,640,480,nullptr,nullptr,instance,nullptr);check(value!=nullptr,"Create hidden owned backdrop HWND");}
    ~Window(){if(value)DestroyWindow(value);}
};
void native(){
    Window window;DesktopBackdropState state{640,480,true,false,.7,.35,1};
    // Fresh HWND creation does not imply a readable/original flag. Establish
    // the baseline with its documented setter, then pass that exact known value.
    const BOOL original=FALSE;hr(DwmSetWindowAttribute(window.value,DWMWA_USE_HOSTBACKDROPBRUSH,&original,sizeof(original)),L"Backdrop fixture: establish owned HWND's disabled host flag");
    const DesktopBackdropInitialization baseline{false};
    {DesktopBackdrop beforeQueue;rejects([&]{beforeQueue.initialize(window.value,state,baseline);},"Absent dispatcher queue rejected without creating a hidden service");}
    Queue queue;DesktopBackdrop backdrop;
    ComPtr<IDCompositionDevice>upperDevice;ComPtr<IDCompositionTarget>upper;ComPtr<IDCompositionVisual>upperRoot;
    hr(DCompositionCreateDevice(nullptr,IID_PPV_ARGS(&upperDevice)),L"Backdrop fixture: create renderer upper composition device");hr(upperDevice->CreateTargetForHwnd(window.value,TRUE,&upper),L"Backdrop fixture: create renderer upper target");hr(upperDevice->CreateVisual(&upperRoot),L"Backdrop fixture: create renderer upper visual");hr(upper->SetRoot(upperRoot.Get()),L"Backdrop fixture: attach renderer upper root");hr(upperDevice->Commit(),L"Backdrop fixture: commit renderer upper target");
    backdrop.initialize(window.value,state,baseline);const auto first=backdrop.stats();
    check(first.initialized&&first.lowerCompositionTarget&&first.hostBackdropEnabled,"Lower host-brush target coexists with renderer's existing upper target");
    check(!first.originalHostBackdropEnabled&&!first.hostAttributeRestorationAttempted,"Explicit disabled baseline is retained without presumed retrieval");
    check(first.visualAllocations==5&&first.brushAllocations==3,"Backdrop owns a fixed bounded compositor graph");
    check(!first.materialVisualParityVerified&&!first.radialVisualParityVerified,"API proof makes no source pixel parity claim");
    check(!IsWindowVisible(window.value),"Initialization never shows or activates the test HWND");
    rejects([&]{backdrop.initialize(window.value,state,baseline);},"Duplicate target initialization requires reset");
    auto invalid=state;invalid.sourceOpacity=std::numeric_limits<double>::quiet_NaN();rejects([&]{backdrop.update(invalid);},"Invalid opacity rejected transactionally");
    check(backdrop.stats().updates==first.updates&&backdrop.stats().propertyWrites==first.propertyWrites,"Invalid update leaves valid retained graph unchanged");
    allocations=0;counting=true;bool changed{};for(unsigned i=0;i<1000;++i)changed|=backdrop.update(state);counting=false;
    check(!changed&&allocations==0,"Unchanged backdrop frames allocate nothing");
    check(backdrop.stats().propertyWrites==first.propertyWrites,"Unchanged frames perform no compositor property writes");
    for(unsigned i=0;i<120;++i){state.sourceOpacity=double(i)/119;check(backdrop.update(state),"Caller-driven finite fade updates retained opacity");}
    state.lowPower=true;backdrop.update(state);state.dark=false;backdrop.update(state);state.pixelWidth=1280;state.pixelHeight=800;backdrop.update(state);
    const auto last=backdrop.stats();check(last.visualAllocations==first.visualAllocations&&last.brushAllocations==first.brushAllocations,"Fade/theme/size changes keep the same visuals and brushes");
    check(!IsWindowVisible(window.value),"All proof updates remain hidden");
    backdrop.reset();check(!backdrop.stats().initialized,"Explicit reset removes the lower target");
    auto restored=backdrop.stats();check(restored.hostAttributeRestorationAttempted&&restored.hostAttributeRestored&&restored.hostAttributeRestoreResult==S_OK&&!restored.originalHostBackdropEnabled,"Reset's exact original disabled flag setter succeeds; no unsupported readback is claimed");
    backdrop.reset();check(backdrop.stats().hostAttributeRestoreResult==restored.hostAttributeRestoreResult&&backdrop.stats().hostAttributeRestored,"Repeated reset preserves the completed restoration evidence");
    for(unsigned i=0;i<5;++i){
        const bool enabled=i%2!=0;const BOOL known=enabled?TRUE:FALSE;
        hr(DwmSetWindowAttribute(window.value,DWMWA_USE_HOSTBACKDROPBRUSH,&known,sizeof(known)),L"Backdrop fixture: establish repeated lifecycle original host flag");
        backdrop.initialize(window.value,state,{enabled});check(backdrop.stats().originalHostBackdropEnabled==enabled,"Caller-established enabled and disabled originals remain distinct");backdrop.reset();restored=backdrop.stats();
        check(restored.hostAttributeRestorationAttempted&&restored.hostAttributeRestored&&restored.hostAttributeRestoreResult==S_OK&&restored.originalHostBackdropEnabled==enabled,"Both explicit original flag values restore through the documented setter");
    }
    check(!IsWindowVisible(window.value),"Repeated lifecycle tests never display synthetic windows");
    upper->SetRoot(nullptr);upperRoot.Reset();upper.Reset();upperDevice.Reset();queue.finish();
}
#endif
}
int main(int argc,char**argv){
#ifdef _WIN32
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
#endif
    try{
        check(argc==2,"Pass explicit Resources/WatchSource/Scene/watch-blur.json");
#ifdef _WIN32
        hr(com);
#endif
        source(read(argv[1]));
#ifdef _WIN32
        native();CoUninitialize();
#endif
        std::cout<<"PASS "<<checks<<
#ifdef _WIN32
            " source/native backdrop checks; hidden setter/restoration API proof only, flag readback, visual parity and capture behavior unverified\n";
#else
            " portable source backdrop checks; Windows compositor and visual parity unverified\n";
#endif
        return 0;
    }
#ifdef _WIN32
    catch(const winrt::hresult_error&e){counting=false;if(SUCCEEDED(com))CoUninitialize();std::cerr<<"FAIL after "<<checks<<" checks: HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
#endif
    catch(const std::exception&e){counting=false;
#ifdef _WIN32
        if(SUCCEEDED(com))CoUninitialize();
#endif
        std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;
    }
}
