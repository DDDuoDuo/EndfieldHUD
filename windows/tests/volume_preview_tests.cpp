#ifdef _WIN32
#include "tools/volume_preview.hpp"
#include "native/module_scene.hpp"
#include "core/module_presentation.hpp"
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace tools=endfield::tools;namespace app=endfield::app;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Window {ATOM atom{};HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldVolumePreviewOwnedFixture";atom=RegisterClassW(&c);check(atom!=0,"Register hidden fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Volume preview",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned Volume fixture starts and stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
void run(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;gpu::VolumeSnapshot data;data.outputs={{"endpoint-1","Speakers"},{"endpoint-2","Headphones",true}};data.outputID="endpoint-1";data.volume=.55;data.muted=false;data.canSetVolume=data.canSetMute=true;
    tools::VolumePreview*owner{};unsigned volumeWrites{},muteWrites{},activations{};tools::VolumePreviewOptions options;options.initial=data;options.rasterDensity=1;
    options.actions.setActive=[&](bool){++activations;};options.actions.setVolume=[&](auto endpoint,double value){check(endpoint==data.outputID,"Injected master write carries the bound endpoint");++volumeWrites;data.volume=value;owner->receiveSnapshot(data);return true;};options.actions.setMute=[&](auto endpoint,bool value){check(endpoint==data.outputID,"Injected mute write carries the bound endpoint");++muteWrites;data.muted=value;owner->receiveSnapshot(data);return true;};
    tools::VolumePreview preview(raster,std::move(options));owner=&preview;preview.resize({1280,800,96,1,1280,800});core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::volume;core::ModulePresentation modules(core::Module::volume);double time{};
    gpu::LayerComposition composition;auto sample=modules.sample(time).presentation;preview.update({},settings,sample,1,time);preview.upload(renderer);composition.setEntries(renderer,preview.entries());composition.present(renderer);renderer.draw(false);check(activations==1&&preview.state().active(),"Shared owner activates audio once for incoming Volume");
    // Compute the same source module plane via the existing projection adapter;
    // no screen-space approximation is used by the input regression.
    gpu::LayerScene geometry(raster);gpu::LayerRasterOptions fixtureOptions;geometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},fixtureOptions);gpu::NativeModuleSurface surface(geometry,core::Module::volume);surface.update({},settings,sample.current,1);core::Projection plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*surface.pose().contentWorld,1280,800);
    const auto point=[&](double x,double y){const auto p=plane.project({x,y});check(p.has_value(),"Source plane projects fixture point");return *p;};
    auto p=point(199.5,128);check(preview.covers(p)&&preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},++time)&&preview.pointerLocked(),"Actual projected volume track takes one owned drag");check(volumeWrites==1&&std::abs(*preview.state().snapshot().volume-.5)<1e-9,"Projected click maps exact original track scalar");
    p=point(311,128);check(preview.pointer({app::PointerKind::move,app::PointerButton::none,p.x,p.y},++time)&&volumeWrites==2&&std::abs(*data.volume-1)<1e-9,"Captured pointer maps the projected track endpoint within round-trip precision");
    p=point(330,128);check(preview.pointer({app::PointerKind::move,app::PointerButton::none,p.x,p.y},++time)&&data.volume==1,"Captured drag outside the track clamps exactly to the native maximum");const auto dragWrites=volumeWrites;check(preview.wheel({p.x,p.y,-1,false,0,3},time)&&volumeWrites==dragWrites,"Wheel during captured drag is consumed without retargeting");
    check(preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},++time)&&!preview.pointerLocked(),"Pointer up releases local slider gesture");preview.pointer({app::PointerKind::captureLost},time);check(preview.key({app::KeyKind::down,VK_LEFT},false,++time)&&data.volume==.98,"Source keyboard nudge works after ordinary release-capture callback");
    p=point(355,127);check(preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},++time)&&muteWrites==1&&data.muted==true,"Mute uses same source-projected artwork control");preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},time);
    p=point(100,55);preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},++time);check(!preview.state().choosing(),"Unsupported default-device switching is not faked by endpoint control selection");preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},time);
    preview.update({},settings,modules.sample(++time).presentation,1,time);preview.upload(renderer);composition.setEntries(renderer,preview.entries());composition.present(renderer);const auto beforeRaster=raster.stats();const auto beforeGPU=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<120;++n){auto center=core::Matrix4{};center.values[3]=n*.000001;preview.update(center,settings,modules.sample(time+=1./60).presentation,1,time);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 steady changing tilt frames allocate no owner storage");check(raster.stats().rasterizations==beforeRaster.rasterizations&&raster.stats().textLayoutsCreated==beforeRaster.textLayoutsCreated&&renderer.stats().textureUploads==beforeGPU.textureUploads,"Changing tilt never rebuilds module content/text or uploads pixels");
    settings.module=core::Module::power;modules.select(core::Module::power,++time);sample=modules.sample(time+.05).presentation;preview.update({},settings,sample,1,time+.05);check(!preview.state().active()&&activations==2&&!preview.pointerLocked()&&!preview.key({app::KeyKind::down,VK_LEFT},false,time+.05),"Leaving Volume immediately disables provider/input while retaining outgoing artwork");preview.upload(renderer);composition.present(renderer);
    preview.update({},settings,modules.sample(time+1).presentation,1,time+1);check(preview.entries().empty()&&!preview.requiresFrames(time+1),"Finished departure removes module entries and all local frame demand");composition.setEntries(renderer,preview.entries());preview.collected(renderer);preview.release(renderer);composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0&&!IsWindowVisible(window.hwnd),"Fixture retires all resources and never opens a real UI/audio provider");renderer.reset();
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass shader to isolated Volume integration fixture");run(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Volume preview checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
