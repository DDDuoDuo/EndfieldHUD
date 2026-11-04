// Desktop Map occlusion over the real source shell (RC 131: "the map should be
// under the storage and activity monitor buttons, not on top of it"), for the
// redesigned Map (Mac 1.3.0: the map fills the 860 px backdrop disc, so much
// more of each plate overlaps it; SystemHUDView.applySourceMapOcclusion masks
// the whole module host and its contrast whenever the Map is presented).
//
// The packaged runtime scene, camera and chrome bindings pose the source shell
// at rest on several displays exactly as Application::Impl::poseShell does
// (neutral camera, last entrance key, the centre plane and display content
// scale). The Map owner is posed on that centre plane twice: without the
// source frame and with it. On every display the disc reaches over the
// Storage and Activity Monitor plates below it; with the frame those plates
// are found by their source paths, cut out of the Map at rest (readback over
// a stand-in for the source pass, which draws below every native layer) and
// left to the navigation's hit test; through switches out of and into the
// Map the other module (a design-screen stand-in) is cut by the same host
// cut on every frame. Hidden window, offscreen WARP renderer, synthetic Map
// data; no input is injected.
#ifdef _WIN32
#include "tools/map_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/source_layout.hpp"
#include "core/watch_runtime_input.hpp"
#include "modules/map_geography.hpp"
#include "native/module_scene.hpp"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>

namespace {
namespace c=endfield::core;namespace s=endfield::core::source;namespace m=endfield::modules;namespace n=endfield::native;namespace a=endfield::app;namespace t=endfield::tools;using J=ehud::data::Json;
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
struct Window{HWND hwnd{};Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"OwnedMapOcclusionFixture";check(RegisterClassW(&w)!=0,"Register the hidden Map occlusion fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,w.lpszClassName,L"Synthetic Map occlusion",WS_POPUP,0,0,1280,800,nullptr,nullptr,w.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"The fixture never shows or activates a window");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"OwnedMapOcclusionFixture",GetModuleHandleW(nullptr));}};
std::shared_ptr<const n::MapPaintImage>image(unsigned size){auto p=std::make_shared<n::MapPaintImage>();p->width=p->height=size;p->straightRGBA.assign(std::size_t(size)*size*4,255);return p;}
std::shared_ptr<const m::MapGeography>geography(){auto p=std::make_shared<m::MapGeography>();p->terrain=m::MapTerrain{};return p;}
n::MapPaintResult paint(const n::MapPaintRequest&q,const n::MapPaintCancelled&cancel){if(cancel())return {};const auto g=*m::mapRasterGeometry(q.viewport,q.contentsScale);auto pixels=std::make_shared<n::MapPaintImage>();pixels->width=pixels->height=g.pixelDimension;pixels->straightRGBA.assign(std::size_t(g.pixelDimension)*g.pixelDimension*4,127);return n::MapPaintResult{pixels,{},m::MapRasterFrame{g.viewport,g.screenRect,g.pixelsPerPoint},.001};}

// Application::Impl::poseShell geometry at rest (the verification probe's pose).
struct Shell {s::CameraFrame frame;std::vector<s::ResolvedNode>nodes;c::Matrix4 center;double contentScale{1};};
Shell rest(const s::WatchRuntimeInput&runtime,const s::WatchAnimation&animation,c::Rect viewport){
    Shell shell;shell.frame=runtime.camera().frame({viewport.width,viewport.height},runtime.camera().rootRotation());
    const auto pose=animation.pose(animation.entrance().lastKeyTime,{},{},shell.frame.layout.canvasSize);shell.nodes=s::SourceLayout(runtime.scene()).resolve({},pose.transforms);
    s::DesktopChromeProjectionPlan plan(runtime.scene(),runtime.camera(),animation,s::DesktopChromeBindings::fromJson(runtime.chrome()["bindings"]));
    shell.contentScale=plan.contentScale(viewport);(void)plan.update(shell.nodes,shell.frame,viewport,shell.contentScale);
    check(plan.projection().center.has_value(),"The source centre plane projects at rest");shell.center=*plan.projection().center;return shell;
}
void display(n::Renderer*renderer,const s::WatchRuntimeInput&runtime,const s::WatchAnimation&animation,c::Rect viewport,double scale){
    const auto shell=rest(runtime,animation,viewport);
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};
    const a::ClientMetrics metrics{std::uint32_t(std::lround(viewport.width*scale)),std::uint32_t(std::lround(viewport.height*scale)),96,scale,viewport.width,viewport.height};
    t::DesignOverlayOptions overlayOptions;overlayOptions.raster.pixelsPerPoint=1;t::DesignOverlayOwner overlay(raster,overlayOptions);overlay.resize(metrics);
    const auto make=[&](bool occluded){t::MapOwnerOptions o;o.geography=geography();o.clock=[&]{return now;};o.paint=paint;o.player={image(2),image(2),image(2)};
        if(occluded)o.sourceFrame=[&shell]()->std::optional<m::MapSourceView>{return m::MapSourceView{shell.nodes,{},&shell.frame};};
        o.owner.name="map";o.owner.raster.pixelsPerPoint=1;
        auto owner=std::make_unique<t::MapOwner>(raster,utility,overlay.context(),std::move(o));owner->resize(metrics);return owner;};
    auto map=make(true),uncut=make(false);
    a::ModuleAppearance appearance;appearance.ambient=false;for(a::ModuleOwner*o:{static_cast<a::ModuleOwner*>(&overlay),static_cast<a::ModuleOwner*>(map.get()),static_cast<a::ModuleOwner*>(uncut.get())}){o->setAppearance(appearance,0);o->setOverlayVisible(true,0);}
    constexpr auto other=c::Module::calendar;
    c::ModulePresentation modules(c::Module::map);modules.setDesignScreen(c::Module::map,true);modules.setDesignScreen(other,true);
    s::DesktopChromeSettings settings{viewport,1,{},c::Module::map,true};settings.contentScale=shell.contentScale;
    const auto pose=[&](t::MapOwner&owner,const c::ModulePresentationSample&sample,const s::DesktopChromeSettings&at){const a::ModuleFrame f{shell.center,at,sample,1,now,true};overlay.update(f);owner.update(f);};
    const auto present=[&]{const auto sample=modules.sample(now).presentation;pose(*map,sample,settings);pose(*uncut,sample,settings);};
    present();now+=1;present();check(map->state().active()&&uncut->state().active(),"The Map is shown at rest and accepts input");
    const auto&plates=map->sourceCutouts();check(plates.count==4,"The packaged scene's Storage and Activity Monitor plates are found by their source paths and visible at rest");
    check(uncut->sourceCutouts().empty(),"Without the source frame no plate is known (the RC 131 layering)");
    // Each plate on a one-point grid: where the uncut Map covers it (hit
    // test), where its disc draws over it and where the ramp leaves it
    // clearly visible (within 80 % of the 430 px radius).
    std::array<std::size_t,2>covered{},drawn{},left{};std::vector<c::Point>deep[2],interior;
    const auto boxRadius=[&](c::Point p)->std::optional<double>{const auto q=map->core().boxPoint(p);if(!q)return std::nullopt;return std::hypot(q->x-m::MapCanvas::mapCenter.x,q->y-m::MapCanvas::mapCenter.y);};
    for(std::size_t k=0;k<plates.count;++k){const auto group=k==0?0u:1u;const auto polygon=plates.polygons[k].view();
        double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;for(const auto p:polygon){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}
        for(double y=std::ceil(y0);y<=y1;++y)for(double x=std::ceil(x0);x<=x1;++x){const c::Point p{x,y};if(!c::polygonContains(polygon,p))continue;
            const auto r=boxRadius(p);if(!r)continue;if(*r<=430)++drawn[group];
            bool margin=true;for(const auto d:{c::Point{-3,0},c::Point{3,0},c::Point{0,-3},c::Point{0,3}})margin=margin&&c::polygonContains(polygon,{p.x+d.x,p.y+d.y});
            if(margin&&*r<=.8*430){interior.push_back(p);deep[group].push_back(p);}
            if(uncut->covers(p)){++covered[group];if(!map->covers(p))++left[group];}}
    }
    std::cout<<"MAP_OCCLUSION "<<viewport.width<<"x"<<viewport.height<<"@"<<scale<<": Storage under the disc "<<drawn[0]<<" pt, hit "<<covered[0]<<", clearly drawn over "<<deep[0].size()<<"; Activity Monitor under the disc "<<drawn[1]<<" pt, hit "<<covered[1]<<", clearly drawn over "<<deep[1].size()<<"; content scale "<<shell.contentScale<<"\n";
    check(drawn[0]>0&&drawn[1]>0,"The Map's disc reaches over both plates (the reported layering)");
    check(covered[0]+covered[1]>0,"Without the plates the Map takes presses on them (its input circle reaches over them)");
    check(left[0]==covered[0]&&left[1]==covered[1],"With the source frame every plate point is left to the navigation (the source hit filter accepts the buttons)");
    const auto finish=[&]{utility.waitIdle();utility.drain();utility.shutdown();};
    if(!renderer){finish();return;}
    check(metrics.pixelWidth==1280&&metrics.pixelHeight==800,"The readback display matches the offscreen target");
    check(!deep[0].empty()||!deep[1].empty(),"A plate lies where the disc draws clearly");
    // Readback: the source pass draws below every native layer; a green
    // stand-in for it is composed first, then the Map.
    n::LayerScene shellStandIn(raster);shellStandIn.load(J(J::Object{{"bounds",J::Array{0,0,1280,800}},{"children",J::Array{J(J::Object{{"id","source.shell"},{"kind","layer"},{"class","CALayer"},{"bounds",J::Array{0,0,1280,800}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{0,1,0,1}}}}})}}}),{});
    n::LayerComposition composition;std::vector<n::LayerCompositionEntry>ordered;
    const auto readback=[&](t::MapOwner&owner){owner.upload(*renderer);ordered.assign(1,{&shellStandIn,{}});for(const auto&e:owner.entries(c::Module::map))ordered.push_back(e);composition.setEntries(*renderer,ordered);composition.present(*renderer);
        renderer->setCamera(n::layerViewportProjection(1280,800));renderer->draw(false);return renderer->readback();};
    const auto green=[](const n::Readback&image,c::Point p){const auto i=std::size_t(std::lround(p.y))*image.rowBytes+std::size_t(std::lround(p.x))*4;return image.pixels.at(i+1)>=250&&image.pixels.at(i)<=5&&image.pixels.at(i+2)<=5;};
    const auto before=readback(*uncut);composition.detach(*renderer);uncut->release(*renderer);
    const auto after=readback(*map);
    check(map->hostCut()!=nullptr,"At rest the Map publishes the host cut (the contrast and switches use it)");
    std::cout<<"MAP_HOST_CUT "<<map->sceneStats().hostCutBytes/1048576.<<" MiB texture, "<<map->sceneStats().hostCutUploads<<" uploads, CPU staging "<<map->sceneStats().cutoutMaskBytes/1048576.<<" MiB\n";
    std::size_t hidden{},shown{},total{};for(const auto&points:deep)for(const auto p:points){++total;hidden+=green(before,p)?0:1;shown+=green(after,p)?1:0;}
    std::cout<<"MAP_OCCLUSION readback: "<<total<<" plate points where the disc draws clearly, Map over them before "<<hidden<<", plates shown after "<<shown<<"\n";
    {unsigned printed{};for(std::size_t g=0;g<2;++g)for(const auto p:deep[g])if(!green(after,p)&&printed++<5)std::cout<<"  Map still over plate "<<g<<" at client "<<p.x<<","<<p.y<<"\n";}
    check(hidden==total,"Without the plates the Map is drawn over them");
    check(shown==total,"With the source frame the Storage and Activity Monitor plates show above the Map at rest");
    // Switches out of and into the Map, every frame: a white design-screen
    // stand-in for the other module carries the Map's host cut (the host
    // applies it to the other module's entries while the Map is presented).
    {n::LayerScene otherScene(raster);otherScene.load(J(J::Object{{"bounds",J::Array{0,0,590,510}},{"children",J::Array{J(J::Object{{"id","other.content"},{"kind","layer"},{"class","CALayer"},{"bounds",J::Array{0,0,590,510}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{1,1,1,1}}}}})}}}),{});
     n::NativeModuleSurface otherPlane(otherScene,other);bool applyCut{true};
     const auto switchFrame=[&](double dt){now+=dt;const auto sample=modules.sample(now).presentation;auto at=settings;at.module=sample.requested;pose(*map,sample,at);map->upload(*renderer);
        const auto*surface=sample.current.module==other?&sample.current:sample.incoming&&sample.incoming->module==other?&*sample.incoming:nullptr;
        const auto*cut=applyCut?map->hostCut():nullptr;n::LayerCompositionEntry otherEntry{&otherScene,{},cut};
        if(surface)otherPlane.update(shell.center,at,*surface);
        ordered.assign(1,{&shellStandIn,{}});const auto append=[&](c::Module module){if(module==other&&surface)ordered.push_back(otherEntry);else if(module==c::Module::map)for(const auto&e:map->entries(c::Module::map))ordered.push_back(e);};
        append(sample.current.module);if(sample.incoming)append(sample.incoming->module);
        composition.setEntries(*renderer,ordered);composition.present(*renderer);renderer->setCamera(n::layerViewportProjection(1280,800));renderer->draw(false);return std::pair{sample,renderer->readback()};};
     const auto platesShown=[&](const n::Readback&image){std::size_t count{};for(const auto p:interior)count+=green(image,p)?1:0;return count;};
     std::size_t leaving=interior.size(),entering=interior.size();unsigned leavingFrames{},enteringFrames{};
     modules.select(other,now);for(unsigned f=0;f<20;++f){const auto [sample,image]=switchFrame(.025);if(!sample.transitioning)break;++leavingFrames;leaving=std::min(leaving,platesShown(image));}
     (void)switchFrame(.025);(void)switchFrame(.025);
     modules.select(c::Module::map,now);applyCut=false;const auto uncutPlates=interior.size()-platesShown(switchFrame(.02).second);applyCut=true;
     for(unsigned f=0;f<20;++f){const auto [sample,image]=switchFrame(.025);if(!sample.transitioning)break;++enteringFrames;entering=std::min(entering,platesShown(image));}
     std::cout<<"MAP_SWITCH readback "<<interior.size()<<" plate points: leaving the Map ("<<leavingFrames<<" frames) at least "<<leaving<<" shown; entering it, the outgoing module covers "<<uncutPlates<<" without the cut, at least "<<entering<<" shown with it ("<<enteringFrames<<" frames)\n";
     check(leavingFrames>=3&&enteringFrames>=3,"Both switches are sampled through their motion (the directional swap)");
     check(leaving==interior.size()&&entering==interior.size(),"With the host cut every plate point shows above the Map and the other module on every frame of both switches");
     composition.detach(*renderer);check(otherScene.releaseResources(*renderer),"The other-module stand-in releases its resources");}
    composition.detach(*renderer);check(shellStandIn.releaseResources(*renderer),"The stand-in releases its resources");map->release(*renderer);overlay.release(*renderer);
    check(renderer->stats().textures==0&&renderer->stats().meshes==0,"Every fixture resource is released");finish();
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass the HUD shader and the packaged runtime-compiled-animation directory");
        const s::WatchRuntimeInput runtime(argv[2]);const s::WatchAnimation animation(runtime.scene(),runtime.library());
        Window window;n::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests});
        display(&renderer,runtime,animation,{0,0,1280,800},1);
        // The Mac reference display and common laptop displays (125 % and 150 %).
        for(const auto&[viewport,scale]:std::array<std::pair<c::Rect,double>,4>{{{{0,0,1470,956},2},{{0,0,1536,864},1.25},{{0,0,1280,720},1.5},{{0,0,1707,1067},1.5}}})display(nullptr,runtime,animation,viewport,scale);
        renderer.reset();CoUninitialize();std::cout<<"PASS "<<checks<<" Map occlusion checks over the packaged source shell\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
