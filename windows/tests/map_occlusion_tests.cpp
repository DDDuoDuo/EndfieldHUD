// Desktop Map occlusion over the real source shell (RC 131: "the map should be
// under the storage and activity monitor buttons, not on top of it").
//
// The packaged runtime scene, camera and chrome bindings pose the source shell
// at rest on several displays exactly as Application::Impl::poseShell does
// (neutral camera, last entrance key, the centre plane and display content
// scale). The Map is posed on that centre plane twice: as before the fix (no
// source frame) and with the presented source frame. On every display the
// Map's circle reaches over the Storage and Activity Monitor plates below it;
// with the frame those plates are found by their source paths, cut out of the
// Map's artwork (readback over a stand-in for the source pass, which draws
// below every native layer) and left to the navigation's hit test. Hidden
// window, offscreen WARP renderer, synthetic Map data; no input is injected.
#ifdef _WIN32
#include "tools/map_preview.hpp"
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
J blank(){return J::Object{{"bounds",J::Array{0,0,440,440}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}

// Application::Impl::poseShell geometry at rest (the verification probe's pose).
struct Shell {s::CameraFrame frame;std::vector<s::ResolvedNode>nodes;c::Matrix4 center;double contentScale{1};};
Shell rest(const s::WatchRuntimeInput&runtime,const s::WatchAnimation&animation,c::Rect viewport){
    Shell shell;shell.frame=runtime.camera().frame({viewport.width,viewport.height},runtime.camera().rootRotation());
    const auto pose=animation.pose(animation.entrance().lastKeyTime,{},{},shell.frame.layout.canvasSize);shell.nodes=s::SourceLayout(runtime.scene()).resolve({},pose.transforms);
    s::DesktopChromeProjectionPlan plan(runtime.scene(),runtime.camera(),animation,s::DesktopChromeBindings::fromJson(runtime.chrome()["bindings"]));
    shell.contentScale=plan.contentScale(viewport);(void)plan.update(shell.nodes,shell.frame,viewport,shell.contentScale);
    check(plan.projection().center.has_value(),"The source centre plane projects at rest");shell.center=*plan.projection().center;return shell;
}

// The host's cut of the other module (map-transition-host.patch: a
// LayerCompositionEntry::cut applied by LayerComposition::present). Without
// that patch the stand-in scene carries the cut as its group alpha mask.
template<class Entry>bool compositionCut(Entry&entry,const n::PlaneAlphaMask*cut){if constexpr(requires(Entry&e){e.cut;}){entry.cut=cut;return true;}else{(void)entry;(void)cut;return false;}}
void display(n::Renderer*renderer,const s::WatchRuntimeInput&runtime,const s::WatchAnimation&animation,c::Rect viewport,double scale){
    const auto shell=rest(runtime,animation,viewport);
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};
    const a::ClientMetrics metrics{std::uint32_t(std::lround(viewport.width*scale)),std::uint32_t(std::lround(viewport.height*scale)),96,scale,viewport.width,viewport.height};
    const auto make=[&](bool occluded){t::MapPreviewOptions o;o.geography=geography();o.clock=[&]{return now;};o.paint=paint;o.raster.pixelsPerPoint=2;o.appearance.ambient=false;o.player={image(2),image(2),image(2),image(880)};
        if(occluded)o.sourceFrame=[&shell]()->std::optional<m::MapSourceView>{return m::MapSourceView{shell.nodes,{},&shell.frame};};
        auto owner=std::make_unique<t::MapPreview>(raster,utility,std::move(o));owner->resize(metrics);return owner;};
    auto map=make(true),uncut=make(false);
    c::ModulePresentation modules(c::Module::map);s::DesktopChromeSettings settings{viewport,1,{},c::Module::map,true};settings.contentScale=shell.contentScale;
    const auto present=[&]{const auto sample=modules.sample(now).presentation;map->update(shell.center,settings,sample,1,now);uncut->update(shell.center,settings,sample,1,now);};
    present();now+=1;present();check(map->state().active()&&uncut->state().active(),"The Map is shown at rest and accepts input");
    const auto&plates=map->sourceCutouts();check(plates.count==4,"The packaged scene's Storage and Activity Monitor plates are found by their source paths and visible at rest");
    check(uncut->sourceCutouts().empty(),"Without the source frame no plate is known (the RC 131 layering)");
    // The Map canvas under a client point (its own plane, as MapPreview projects it).
    n::LayerScene geometry(raster);geometry.load(blank(),{});n::NativeModuleSurface plane(geometry,c::Module::map);
    const auto sample=modules.sample(now).presentation;plane.update(shell.center,settings,sample.current);
    const auto camera=n::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*c::Matrix4::scale(scale,scale);
    const auto projection=c::Projection::viewport(camera*plane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
    const auto canvas=[&](c::Point p){return projection.unproject({p.x*scale,p.y*scale});};
    // Sample each plate on a one-point grid: where the uncut Map covers it
    // (hit test) and where its feathered circle draws over it (radius 225).
    std::array<std::size_t,2>covered{},drawn{},left{};std::vector<c::Point>deep[2],all,interior;
    for(std::size_t k=0;k<plates.count;++k){const auto group=k==0?0u:1u;const auto polygon=plates.polygons[k].view();
        double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;for(const auto p:polygon){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}
        for(double y=std::ceil(y0);y<=y1;++y)for(double x=std::ceil(x0);x<=x1;++x){const c::Point p{x,y};if(!c::polygonContains(polygon,p))continue;
            const auto q=canvas(p);if(!q)continue;const double r=std::hypot(q->x-220,q->y-220);
            if(r<=225)++drawn[group];all.push_back(p);
            {bool margin=true;for(const auto d:{c::Point{-3,0},c::Point{3,0},c::Point{0,-3},c::Point{0,3}})margin=margin&&c::polygonContains(polygon,{p.x+d.x,p.y+d.y});if(margin)interior.push_back(p);}
            if(uncut->covers(p)){++covered[group];if(!map->covers(p))++left[group];}
            // At least three pixels inside the plate, on the canvas (the fixture's feather is opaque over it).
            bool inside=q->x>=4&&q->x<=436&&q->y>=4&&q->y<=436;for(const auto d:{c::Point{-3,0},c::Point{3,0},c::Point{0,-3},c::Point{0,3}})inside=inside&&c::polygonContains(polygon,{p.x+d.x,p.y+d.y});
            if(inside)deep[group].push_back(p);}
    }
    std::cout<<"MAP_OCCLUSION "<<viewport.width<<"x"<<viewport.height<<"@"<<scale<<": Storage drawn over "<<drawn[0]<<" pt, hit "<<covered[0]<<"; Activity Monitor drawn over "<<drawn[1]<<" pt, hit "<<covered[1]<<"; content scale "<<shell.contentScale<<"\n";
    check(drawn[0]>0&&drawn[1]>0,"The Map's feathered circle reaches over both plates (the reported layering)");
    check(covered[0]+covered[1]>0,"Before the fix the Map took presses on the plates");
    check(left[0]==covered[0]&&left[1]==covered[1],"With the source frame every plate point is left to the navigation (the source hit filter accepts the buttons)");
    // Module switches (SystemHUDView masks the whole module host while the Map
    // is presented, current or incoming). Entering the Map, the outgoing
    // module is drawn on the Map's 1.14 host from the first frame; leaving
    // it, the incoming module ends on its own host. Plate points each
    // module's content frame covers at those two poses (identity transition
    // transform; the host clip is the 440 square containing every frame).
    {const auto covers=[&](c::Module module,c::Module requested){auto at=settings;at.module=requested;const auto layout=s::DesktopChromeLayout::make(at,shell.center,{});const auto frame=c::moduleLocalContentFrame(module);
        const auto world=shell.center*c::Matrix4::translation(500,layout.reportCenterY)*c::Matrix4::scale(layout.reportScale,layout.reportScale)*c::Matrix4::translation(frame.x-220,frame.y-220);
        const auto plane=c::Projection::viewport(n::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*c::Matrix4::scale(scale,scale)*world,metrics.pixelWidth,metrics.pixelHeight);
        std::size_t count{};for(const auto p:all){const auto q=plane.unproject({p.x*scale,p.y*scale});count+=q&&q->x>=0&&q->x<=frame.width&&q->y>=0&&q->y<=frame.height?1:0;}return count;};
     std::string into,out;std::size_t fullInto{},defaultInto{};
     for(int k=0;k<=int(c::Module::profile);++k){const auto module=static_cast<c::Module>(k);if(module==c::Module::map)continue;const auto a=covers(module,c::Module::map),b=covers(module,module);
        const auto frame=c::moduleLocalContentFrame(module);(frame.height>=440?fullInto:defaultInto)+=a;
        if(a)into+=" "+std::string(c::moduleIdentifier(module))+" "+std::to_string(a);if(b)out+=" "+std::string(c::moduleIdentifier(module))+" "+std::to_string(b);}
     std::cout<<"MAP_SWITCH_OVERLAP "<<viewport.width<<"x"<<viewport.height<<"@"<<scale<<" of "<<all.size()<<" plate points: entering the Map, the outgoing module covers"<<(into.empty()?" none":into)<<"; leaving it, the incoming module ends over"<<(out.empty()?" none":out)<<"\n";
     check(fullInto>0,"Entering the Map a full-height module is drawn over the plates (the switch flicker the transition cut removes)");}
    const auto finish=[&]{utility.waitIdle();utility.drain();utility.shutdown();};
    if(!renderer){finish();return;}
    // Readback: the source pass draws below every native layer; a green
    // stand-in for it is composed first, then the Map.
    check(metrics.pixelWidth==1280&&metrics.pixelHeight==800,"The readback display matches the offscreen target");
    check(!deep[0].empty()&&!deep[1].empty(),"Both plates have points well inside the Map canvas");
    n::LayerScene shellStandIn(raster);shellStandIn.load(J(J::Object{{"bounds",J::Array{0,0,1280,800}},{"children",J::Array{J(J::Object{{"id","source.shell"},{"kind","layer"},{"class","CALayer"},{"bounds",J::Array{0,0,1280,800}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{0,1,0,1}}}}})}}}),{});
    n::LayerComposition composition;std::vector<n::LayerCompositionEntry>ordered;
    const auto readback=[&](t::MapPreview&owner){owner.upload(*renderer);ordered.assign(1,{&shellStandIn,{}});for(const auto&e:owner.entries())ordered.push_back(e);composition.setEntries(*renderer,ordered);composition.present(*renderer);
        renderer->setCamera(n::layerViewportProjection(1280,800));renderer->draw(false);return renderer->readback();};
    const auto green=[](const n::Readback&image,c::Point p){const auto i=std::size_t(std::lround(p.y))*image.rowBytes+std::size_t(std::lround(p.x))*4;return image.pixels.at(i+1)>=250&&image.pixels.at(i)<=5&&image.pixels.at(i+2)<=5;};
    const auto before=readback(*uncut);composition.detach(*renderer);uncut->release(*renderer);
    const auto after=readback(*map);
    std::size_t hidden{},shown{},total{};for(const auto&points:deep)for(const auto p:points){++total;hidden+=green(before,p)?0:1;shown+=green(after,p)?1:0;}
    std::cout<<"MAP_OCCLUSION readback: "<<total<<" plate points inside the canvas, Map over them before "<<hidden<<", plates shown after "<<shown<<"\n";
    {unsigned printed{};for(std::size_t g=0;g<2;++g)for(const auto p:deep[g])if(!green(after,p)&&printed++<5){const auto q=canvas(p);std::cout<<"  Map still over plate "<<g<<" at client "<<p.x<<","<<p.y<<" (canvas "<<(q?q->x:-1)<<","<<(q?q->y:-1)<<")\n";}}
    check(hidden==total,"Before the fix the Map was drawn over the plates");
    check(shown==total,"With the source frame the Storage and Activity Monitor plates show above the Map");
    // The same plates through switches out of and into the Map, every frame:
    // a white Calendar-frame stand-in for the other module carries the Map's
    // transition cut (the host applies it to the other module's entries).
    {constexpr auto other=c::Module::calendar;const auto frameRect=c::moduleLocalContentFrame(other);
     n::LayerScene otherScene(raster);otherScene.load(J(J::Object{{"bounds",J::Array{0,0,frameRect.width,frameRect.height}},{"children",J::Array{J(J::Object{{"id","other.content"},{"kind","layer"},{"class","CALayer"},{"bounds",J::Array{0,0,frameRect.width,frameRect.height}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{1,1,1,1}}}}})}}}),{});
     n::NativeModuleSurface otherPlane(otherScene,other);bool applyCut{true};
     const auto switchFrame=[&](double dt){now+=dt;const auto sample=modules.sample(now).presentation;auto at=settings;at.module=sample.requested;map->update(shell.center,at,sample,1,now);map->upload(*renderer);
        const auto*surface=sample.current.module==other?&sample.current:sample.incoming&&sample.incoming->module==other?&*sample.incoming:nullptr;
        const auto*cut=applyCut?map->transitionCut():nullptr;n::LayerCompositionEntry otherEntry{&otherScene,{}};
        if(surface){otherPlane.update(shell.center,at,*surface);if(!compositionCut(otherEntry,cut))otherScene.setGroupAlphaMask(cut?std::optional<n::PlaneAlphaMask>(*cut):std::nullopt);}
        ordered.assign(1,{&shellStandIn,{}});const auto append=[&](c::Module module){if(module==other&&surface)ordered.push_back(otherEntry);else if(module==c::Module::map)for(const auto&e:map->entries())ordered.push_back(e);};
        append(sample.current.module);if(sample.incoming)append(sample.incoming->module);
        composition.setEntries(*renderer,ordered);composition.present(*renderer);renderer->setCamera(n::layerViewportProjection(1280,800));renderer->draw(false);return std::pair{sample,renderer->readback()};};
     const auto platesShown=[&](const n::Readback&image){std::size_t count{};for(const auto p:interior)count+=green(image,p)?1:0;return count;};
     std::size_t leaving=interior.size(),entering=interior.size();unsigned leavingFrames{},enteringFrames{};
     modules.select(other,now);for(unsigned f=0;f<14;++f){const auto [sample,image]=switchFrame(.025);if(!sample.transitioning)break;++leavingFrames;leaving=std::min(leaving,platesShown(image));}
     (void)switchFrame(.025);(void)switchFrame(.025);
     modules.select(c::Module::map,now);applyCut=false;const auto uncut=interior.size()-platesShown(switchFrame(.02).second);applyCut=true;
     for(unsigned f=0;f<14;++f){const auto [sample,image]=switchFrame(.025);if(!sample.transitioning)break;++enteringFrames;const auto shownNow=platesShown(image);entering=std::min(entering,shownNow);
        if(shownNow<interior.size()){unsigned printed{};for(const auto p:interior)if(!green(image,p)&&printed++<6){const auto i=std::size_t(std::lround(p.y))*image.rowBytes+std::size_t(std::lround(p.x))*4;
            std::cout<<"  entering frame "<<f<<": plate point "<<p.x<<","<<p.y<<" shows BGR "<<int(image.pixels.at(i))<<","<<int(image.pixels.at(i+1))<<","<<int(image.pixels.at(i+2))<<"\n";}}}
     std::cout<<"MAP_SWITCH readback "<<interior.size()<<" plate points: leaving the Map ("<<leavingFrames<<" frames) at least "<<leaving<<" shown; entering it, the outgoing module covers "<<uncut<<" without the cut, at least "<<entering<<" shown with it ("<<enteringFrames<<" frames)\n";
     check(leavingFrames>=10&&enteringFrames>=10,"Both switches are sampled through their .3 s");
     check(uncut>0,"Without the cut the outgoing module covers the plates while the Map enters (the reported flicker)");
     check(leaving==interior.size()&&entering==interior.size(),"With the transition cut every plate point shows above the Map and the other module on every frame of both switches");
     composition.detach(*renderer);check(otherScene.releaseResources(*renderer),"The other-module stand-in releases its resources");}
    composition.detach(*renderer);check(shellStandIn.releaseResources(*renderer),"The stand-in releases its resources");map->release(*renderer);
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
