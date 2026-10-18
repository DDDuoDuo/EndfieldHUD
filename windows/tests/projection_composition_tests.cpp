#include "native/desktop_backdrop.hpp"
#include "native/renderer.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
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
namespace {
unsigned checks{};void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F call,const char*message){bool rejected{};try{call();}catch(...){rejected=true;}check(rejected,message);}
void portable(){
    static_assert(!std::is_copy_constructible_v<RendererCompositionSurface>);
    const DesktopBackdropState state;check(!state.projectionPlane,"Existing HUD remains the default backdrop mode");
    const DesktopBackdropStats stats;check(!stats.foregroundAttached&&stats.panelOpacity==1&&!stats.groupOpacityVisualParityVerified,"Default graph has no bridge/fade or invented group pixel proof");
    const RendererStats renderer;check(!renderer.compositionSuspended&&renderer.compositionSurfaceBorrows==0&&renderer.compositionSuspends==0&&renderer.compositionRestores==0,"Ordinary renderer starts without a borrowed presentation binding");
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
void hr(HRESULT result,const wchar_t*stage){if(FAILED(result))throw winrt::hresult_error(result,stage);}
struct Queue {
    winrt::Windows::System::DispatcherQueueController controller{nullptr};
    Queue(){check(!winrt::Windows::System::DispatcherQueue::GetForCurrentThread(),"Hidden fixture owns its one isolated caller queue");const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};hr(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))),L"Projection fixture: create caller queue");}
    void finish(){if(!controller)return;const auto operation=controller.ShutdownQueueAsync();const auto deadline=GetTickCount64()+10000;while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){check(GetTickCount64()<deadline,"Fixture queue shutdown is bounded");MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){check(GetTickCount64()<deadline,"Fixture queue drain is bounded");TranslateMessage(&message);DispatchMessageW(&message);}if(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started)MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}operation.GetResults();controller=nullptr;}
    ~Queue(){try{finish();}catch(...) {}}
};
struct Window {
    HWND value{};
    Window(){const auto instance=GetModuleHandleW(nullptr);WNDCLASSW kind{};kind.hInstance=instance;kind.lpfnWndProc=DefWindowProcW;kind.lpszClassName=L"EndfieldHUD.HiddenProjectionComposition";check(RegisterClassW(&kind)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Register hidden Projection composition fixture");value=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,kind.lpszClassName,L"Hidden synthetic Projection composition",WS_POPUP,0,0,640,480,nullptr,nullptr,instance,nullptr);check(value!=nullptr,"Create hidden owned Projection HWND");const BOOL disabled=FALSE;hr(DwmSetWindowAttribute(value,DWMWA_USE_HOSTBACKDROPBRUSH,&disabled,sizeof(disabled)),L"Projection fixture: establish disabled original host flag");}
    ~Window(){if(value)DestroyWindow(value);}
};
void api(const wchar_t*shader){
    Queue queue;Window window,otherWindow;Renderer renderer,other,offscreen;const RendererOptions options{Driver::warpForTests,shader,RenderTarget::composition};renderer.initialize(window.value,640,480,options);other.initialize(otherWindow.value,640,480,options);
    offscreen.initialize(window.value,640,480,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});rejects([&]{offscreen.borrowCompositionSurface();},"Offscreen fixtures cannot masquerade as desktop composition surfaces");offscreen.reset();
    auto surface=renderer.borrowCompositionSurface();const auto foreign=other.borrowCompositionSurface();check(surface->valid()&&surface->swapChain()&&surface->window()==window.value&&surface->width()==640&&surface->height()==480,"Borrow exposes exact existing swapchain HWND and physical dimensions");
    allocations=0;counting=true;bool same=true;for(unsigned n=0;n<1000;++n)same&=renderer.borrowCompositionSurface()==surface;counting=false;check(same&&allocations==0&&renderer.stats().compositionSurfaceBorrows==1,"Repeated borrowing reuses the same AddRef-owned handle without allocation");
    rejects([&]{renderer.suspendComposition(*foreign);},"Foreign epoch rejects before touching upper binding");check(!renderer.stats().compositionSuspended,"Rejected suspension leaves original renderer binding attached");
    DesktopBackdrop backdrop;DesktopBackdropState state{640,480,true,false,.6,.2,1,true};backdrop.initialize(window.value,state,{false});
    rejects([&]{backdrop.setPanelOpacity(.5);},"Panel fade requires an attached foreground group");rejects([&]{backdrop.attachForeground(other);},"Different HWND rejects before a binding handoff");check(backdrop.stats().initialized&&!renderer.stats().compositionSuspended&&!other.stats().compositionSuspended,"Rejected foreign bridge preserves both renderers and backdrop");
    renderer.resize(641,480);rejects([&]{backdrop.attachForeground(renderer);},"Mismatched physical extents reject before handoff");check(!renderer.stats().compositionSuspended&&!backdrop.stats().foregroundAttached,"Size validation has no partial bridge");renderer.resize(640,480);
    check(backdrop.attachForeground(renderer)&&!backdrop.attachForeground(renderer),"Foreground handoff is retained and idempotent");const auto attached=backdrop.stats();check(attached.foregroundAttached&&attached.foregroundSurfaceAllocations==1&&attached.visualAllocations==7&&attached.brushAllocations==4,"Bridge owns one fixed surface, sprite, layer and brush above the existing backdrop");check(renderer.stats().compositionSuspended&&renderer.stats().compositionSuspends==1,"Old upper target is suspended while its swapchain paints inside the lower group");
    // An empty upper tree still reserves the exact original target slot.
    ComPtr<IDCompositionDevice>probeDevice;ComPtr<IDCompositionTarget>probeTarget;hr(DCompositionCreateDevice(nullptr,IID_PPV_ARGS(&probeDevice)),L"Projection fixture: target-slot probe device");check(FAILED(probeDevice->CreateTargetForHwnd(window.value,TRUE,&probeTarget)),"Suspending preserves the original upper slot instead of replacing targets");probeTarget.Reset();probeDevice.Reset();
    rejects([&]{backdrop.attachForeground(other);},"Attached foreground cannot be replaced by another renderer");rejects([&]{backdrop.setPanelOpacity(std::numeric_limits<double>::quiet_NaN());},"Invalid final alpha rejects transactionally");check(backdrop.stats().panelOpacity==1&&renderer.stats().compositionSuspended,"Rejected alpha preserves the retained binding and opacity");
    renderer.draw(false);const auto original=renderer.readback();const auto before=renderer.stats();const auto graph=backdrop.stats();
    allocations=0;counting=true;bool unchanged=true;for(unsigned n=0;n<1000;++n)unchanged&=!backdrop.setPanelOpacity(1);counting=false;check(unchanged&&allocations==0&&backdrop.stats().propertyWrites==graph.propertyWrites,"Unchanged panel frames do not allocate or write composition properties");
    for(unsigned n=0;n<121;++n)backdrop.setPanelOpacity(double(n)/120);check(backdrop.setPanelOpacity(.37),"Final panel alpha accepts partial group fade");renderer.draw(false);const auto after=renderer.readback();check(after.pixels==original.pixels,"Group alpha does not repaint or multiply renderer source pixels (not a final desktop pixel comparison)");check(renderer.stats().textureUploads==before.textureUploads&&renderer.stats().meshUploads==before.meshUploads&&renderer.stats().resourceBytes==before.resourceBytes,"Composition fade allocates no application render texture or mesh");
    const auto faded=backdrop.stats();check(faded.visualAllocations==graph.visualAllocations&&faded.brushAllocations==graph.brushAllocations&&faded.foregroundSurfaceAllocations==graph.foregroundSurfaceAllocations&&!faded.groupOpacityVisualParityVerified,"Finite opacity frames reuse the graph and make no unmeasured DWM pixel claim");
    auto resized=state;resized.pixelWidth=800;resized.pixelHeight=600;rejects([&]{backdrop.update(resized);},"Backdrop resize waits for matching swapchain buffers");check(backdrop.stats().foregroundAttached&&backdrop.stats().panelOpacity==.37,"Rejected resize preserves final alpha and bridge");renderer.resize(800,600);check(surface->width()==800&&surface->height()==600&&surface->valid(),"Resize retains the live swapchain wrapper and updates borrowed physical extents");backdrop.update(resized);check(backdrop.stats().foregroundSurfaceAllocations==1&&backdrop.stats().visualAllocations==7,"Resize updates retained group/sprite sizes without rebuilding bridge");
    check(backdrop.detachForeground()&&!backdrop.detachForeground(),"Hidden detach restores once and is idempotent");check(!renderer.stats().compositionSuspended&&renderer.stats().compositionRestores==1&&backdrop.stats().foregroundRestored&&surface->valid(),"Original upper target/visual return without destroying the existing swapchain");
    resized.projectionPlane=false;backdrop.update(resized);rejects([&]{backdrop.attachForeground(renderer);},"Ordinary HUD backdrop cannot implicitly become the Projection group");check(backdrop.stats().visualAllocations==7&&backdrop.stats().panelOpacity==1,"Ordinary HUD defaults survive Projection detachment");resized.projectionPlane=true;backdrop.update(resized);
    check(backdrop.attachForeground(renderer),"Same live renderer may reattach on a later hidden lifecycle");backdrop.setPanelOpacity(.2);backdrop.reset();check(!renderer.stats().compositionSuspended&&backdrop.stats().foregroundRestored&&backdrop.stats().hostAttributeRestored&&!backdrop.stats().initialized,"Backdrop reset releases bridge then restores exact upper binding and original host flag");
    backdrop.initialize(window.value,resized,{false});backdrop.attachForeground(renderer);renderer.reset();check(!surface->valid()&&surface->swapChain()==nullptr&&!backdrop.stats().foregroundAttached&&!backdrop.stats().initialized&&backdrop.stats().rendererInvalidations==1&&backdrop.stats().hostAttributeRestored,"Renderer reset invalidates the epoch and detaches the bridge before releasing swapchain/device");check(!renderer.restoreComposition(*surface),"Stale handles cannot resurrect a reset renderer");
    renderer.initialize(window.value,800,600,options);const auto replacement=renderer.borrowCompositionSurface();check(replacement!=surface&&replacement->valid(),"Reset releases target slots and a replacement renderer gets a fresh epoch");rejects([&]{renderer.suspendComposition(*surface);},"Old handle cannot suspend replacement renderer");check(!renderer.stats().compositionSuspended,"Rejected stale epoch preserves new renderer binding");
    check(!IsWindowVisible(window.value)&&!IsWindowVisible(otherWindow.value),"Entire fixture remains hidden, with no capture or user-data access");renderer.reset();other.reset();backdrop.reset();queue.finish();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{hr(com,L"Projection fixture: caller STA");check(argc==2,"Pass shader path to hidden composition API fixture");portable();api(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Projection composition checks; hidden API/lifetime proof only, final group/backdrop pixels unverified\n";return 0;}catch(const winrt::hresult_error&e){counting=false;if(SUCCEEDED(com))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}catch(const std::exception&e){counting=false;if(SUCCEEDED(com))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Projection composition defaults checks; Windows bridge unexecuted\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
#endif
