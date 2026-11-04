// The redesigned Map's native surface (native/map_scene: WorldMapSurface's
// layers in the raster's unit space over the backdrop disc's square): the
// group over the disc at the design density (capped by the renderer's group
// limit), every child faded by the shared centre backdrop ramp (the Mac's
// half-resolution HUDCentreBackdrop.mask), the header scrim, pins drawn out
// to the disc's rim, the pulse and the cycled pin's turn-in, the host cut of
// the bottom plates over the static box plane, and allocation-free poses.
#include "native/map_scene.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include "native/layer_scene.hpp"
#endif
namespace{std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace{namespace c=endfield::core;namespace m=endfield::modules;namespace n=endfield::native;using G=m::MapSurfaceGeometry;unsigned checks{};
void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}template<class F>void rejects(F f,const char*s){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,s);}
std::string id(unsigned k){char b[37];std::snprintf(b,sizeof b,"00000000-0000-4000-8000-%012u",k);return b;}
m::MapState state(){m::MapSnapshot initial;
    // One pin at the centre and one 240 units east: outside v1.2.0's 216-unit circle, inside the disc's 251.12-unit rim.
    initial.pins.push_back({id(1),initial.viewport.centerX,initial.viewport.centerY,1,m::MapPinStyle::yellow});
    initial.pins.push_back({id(2),m::mapWrappedX(initial.viewport.centerX+240./(440*3)),initial.viewport.centerY,2,m::MapPinStyle::green});
    unsigned serial=3;return m::MapState(std::move(initial),{[](const auto&){},[serial]()mutable{return id(serial++);},[]{return 3.;}});}
void portable(){
    check(n::mapHostCutSide()==unsigned(std::ceil(860*2*c::DesignBox::baseUnit))&&n::mapHostCutSide()==1146,"The host cut keeps v1.2.0's two texels per design unit over the 860 px square");
    const auto b=n::mapHostCutBounds();check(b.x==-135&&b.y==-126&&std::abs(b.width-1146/(2*c::DesignBox::baseUnit))<1e-9&&b.width>=860,"The host cut covers the map square of the box plane");
    check(std::abs(n::mapDiscUnits-430/G::unitScale)<1e-12&&std::abs(n::mapSceneBounds.x-(220-n::mapDiscUnits))<1e-12&&std::abs(n::mapSceneBounds.width-860/G::unitScale)<1e-9,"The scene square is the disc in v1.2.0 units");
}
#ifdef _WIN32
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"OwnedMapSceneFixture";check(RegisterClassW(&c)!=0,"Register own hidden Map fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"Synthetic Map",WS_POPUP,0,0,800,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Map fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"OwnedMapSceneFixture",GetModuleHandleW(nullptr));}};
std::shared_ptr<const n::MapPaintImage>image(){auto p=std::make_shared<n::MapPaintImage>();p->width=p->height=2;p->straightRGBA.assign(16,255);return p;}
void native(const std::filesystem::path&shader){Window window;n::Renderer renderer;renderer.initialize(window.hwnd,800,800,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});renderer.setCamera(n::layerViewportProjection(800,800));
    n::LayerRasterizer raster;n::LayerRasterOptions options;options.pixelsPerPoint=1;endfield::app::UtilityExecutor utility([]{});double time{};
    n::NativeMapRaster terrain(utility,[&]{return time;},[]{},[](const auto&,const auto&){return n::MapPaintResult{};});auto model=state();model.setActive(true);model.setReducedMotion(false);
    n::MapAppearance appearance;appearance.ambient=false;n::NativeMapScene scene(model,terrain,raster,options,{image(),image(),image()},appearance);n::LayerComposition composition;bool released{};
    struct Cleanup{n::Renderer&r;n::LayerComposition&c;n::NativeMapScene&s;bool&done;~Cleanup(){if(done)return;try{c.detach(r);s.releaseResources(r);}catch(...){r.reset();}}}cleanup{renderer,composition,scene,released};
    // Units -> screen: the disc's 502 units at 1.5 px, 20 px in.
    const double s=1.5;const auto place=c::Matrix4::translation(20-n::mapSceneBounds.x*s,20-n::mapSceneBounds.y*s)*c::Matrix4::scale(s,s);
    const auto frame=[&](double t,c::Matrix4 world){time=t;scene.syncContent(t);scene.updatePose({world,1,t,{}});scene.uploadResources(renderer);composition.setEntries(renderer,scene.entries());composition.present(renderer);};
    check(scene.setDensity(1)==false&&scene.density()==1,"Density 1: one px per box px");
    frame(0,place);check(raster.stats().entries==5,"Four reusable pin paths and the one shared ramp are the scene's only rasters");
    check(scene.stats().visiblePins==2&&scene.stats().pulsingPins==0,"Pins outside v1.2.0's circle are drawn out to the disc's rim");
    check(scene.stats().groupPixels==std::size_t(std::ceil(n::mapSceneBounds.width*G::unitScale))*std::size_t(std::ceil(n::mapSceneBounds.height*G::unitScale)),"The group covers the 860 px disc at the density");
    renderer.draw(false);{const auto back=renderer.readback();check(back.width==800&&!back.pixels.empty(),"The retained Map draws offscreen");
        const auto alpha=[&](double u,double v){const auto x=std::size_t(20+(u-n::mapSceneBounds.x)*s),y=std::size_t(20+(v-n::mapSceneBounds.y)*s);return int(back.pixels[y*back.rowBytes+x*4+3]);};
        const double r=n::mapDiscUnits;const int centre=alpha(220,330),rim=alpha(220+.9*r,220),outside=alpha(220+.71*r,220+.71*r);
        std::printf("MAP_RAMP alpha centre %d, 90 %% radius %d, outside %d\n",centre,rim,outside);
        check(centre>200&&rim>0&&rim<centre/3&&outside==0,"The well fades with the backdrop ramp: opaque inside 52 %, faint near the rim, nothing past it");}
    // Density: follows the design density, 1...1.5 px per box px.
    check(scene.setDensity(2)&&scene.density()==1.5,"Above 1.5 the group stays at 1.5 (the terrain detail tops out near 1.3)");frame(1,place);
    check(scene.stats().groupPixels==std::size_t(std::ceil(n::mapSceneBounds.width*1.5*G::unitScale))*std::size_t(std::ceil(n::mapSceneBounds.height*1.5*G::unitScale)),"Density 1.5: 1290 px per side");
    check(!scene.setDensity(3.5)&&scene.density()==1.5,"Any higher density keeps it");check(scene.setDensity(1)&&scene.density()==1,"Below it the group follows");frame(2,place);
    // Tilt frames: numbers only.
    auto gpu=renderer.stats();auto paint=raster.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k)frame(3+k/60.,c::Matrix4::rotation(.02,.01*k/120,0)*place);}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&renderer.stats().textureUploads==gpu.textureUploads&&raster.stats().rasterizations==paint.rasterizations,"Warm tilt frames allocate, rasterize and upload nothing");check(!scene.requiresFrames(5),"A settled Map has no frame demand");
    // The camera eases .18 s; the cycled pin turns in .16 s (only it).
    (void)model.zoom(G::center,1.5,true);frame(6,place);check(scene.requiresFrames(6)&&!scene.requiresFrames(6.2),"An animated camera is finite motion");frame(6.2,place);
    (void)model.activatePin(id(1));frame(6.3,place);check(!scene.requiresFrames(6.3),"Selecting a pin does not turn it");(void)model.activatePin(id(1));frame(6.4,place);check(scene.requiresFrames(6.4)&&!scene.requiresFrames(6.57),"The cycled pin turns in for .16 s");
    gpu=renderer.stats();allocations=0;counting=true;try{for(unsigned k=1;k<=12;++k)frame(6.4+k/60.,place);}catch(...){counting=false;throw;}counting=false;check(allocations==0&&renderer.stats().nativeGroupTargetAllocations==gpu.nativeGroupTargetAllocations,"The turn-in only re-poses retained marker groups");
    // The pulse: ambient motion, sampled by whatever frame presents it.
    appearance.ambient=true;scene.setAppearance(appearance);frame(7,place);check(scene.pulsing()&&scene.stats().pulsingPins>=1&&scene.stats().pulsingPins==scene.stats().visiblePins&&!scene.requiresFrames(7),"The visible pins (up to 12) pulse without display-rate demand");
    {const auto updates=scene.stats().localUpdates;frame(8,place);frame(8+1./30,place);check(scene.stats().localUpdates==updates+2,"Each ambient frame samples the pulse at its own time");}
    model.setReducedMotion(true);appearance.reducedMotion=true;scene.setAppearance(appearance);frame(9,place);check(!scene.pulsing(),"Reduce Motion stops the pulse");appearance.reducedMotion=false;scene.setAppearance(appearance);
    (void)model.setActive(false);frame(10,place);check(!scene.pulsing(),"An inactive Map does not pulse");
    // The host cut: created with the first plates, repainted in place, released when no draw samples it.
    m::MapCutouts plates;plates.count=1;auto&poly=plates.polygons[0];poly.count=4;poly.points[0]={200,560};poly.points[1]={300,560};poly.points[2]={300,640};poly.points[3]={200,640};
    check(scene.setHostCut(plates,true)&&!scene.hostCutTexture(),"Wanted plates are not resident before an upload");frame(11,place);
    check(scene.hostCutTexture()&&scene.stats().hostCutBytes==std::size_t(n::mapHostCutSide())*n::mapHostCutSide()&&scene.stats().hostCutUploads==1,"The host cut is one updatable texture over the map square");
    poly.points[0].x=210;check(scene.setHostCut(plates,true),"Moved plates change the cut");frame(12,place);check(scene.stats().hostCutUploads==2&&scene.hostCutTexture(),"Only the changed texels are repainted");
    check(!scene.setHostCut(plates,true),"Equal plates do nothing");rejects([&]{m::MapCutouts bad=plates;bad.polygons[0].points[1].x=NAN;scene.setHostCut(bad,true);},"A nonfinite plate rejects");
    check(scene.setHostCut({},false)&&!scene.hostCutTexture(),"No plates: no cut");check(scene.releaseHostCut(renderer)&&scene.stats().hostCutBytes==0,"The unused cut is released");
    const auto old=scene.entries()[0].after[0].world;rejects([&]{scene.updatePose({{},NAN,13,{}});},"Malformed opacity rejects before publication");check(scene.entries()[0].after[0].world==old,"Rejected pose preserves published placement");
    check(!scene.releaseResources(renderer),"Published carrier cannot release live resources");composition.detach(renderer);check(scene.releaseResources(renderer),"Map resources release after composition detach");
    check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Map retires its images, ramp, scrim, shapes and group");released=true;renderer.reset();utility.shutdown();}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass original HUD shader");portable();native(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Map retained scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Map portable scene checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
