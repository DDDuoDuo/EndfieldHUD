#ifdef _WIN32
#include "tools/map_preview.hpp"
#include "tools/map_automation.hpp"
#include "tests/screen_reader_peer_probe.hpp"
#include "modules/map_geography.hpp"
#include "native/module_scene.hpp"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace{std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace c=endfield::core;namespace m=endfield::modules;namespace n=endfield::native;namespace a=endfield::app;namespace t=endfield::tools;using J=ehud::data::Json;
unsigned checks{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
template<class F>void rejects(F f,const char*s){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,s);}
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"OwnedMapOwnerFixture";check(RegisterClassW(&c)!=0,"Register isolated Map fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"Synthetic Map",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Map fixture never shows or activates a window");}~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"OwnedMapOwnerFixture",GetModuleHandleW(nullptr));}};
J blank(){return J::Object{{"bounds",J::Array{0,0,440,440}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
std::shared_ptr<const n::MapPaintImage>image(unsigned size){auto p=std::make_shared<n::MapPaintImage>();p->width=p->height=size;p->straightRGBA.assign(std::size_t(size)*size*4,255);return p;}
std::shared_ptr<const m::MapGeography>geography(){auto p=std::make_shared<m::MapGeography>();p->terrain=m::MapTerrain{};return p;}
n::MapPaintResult paint(const n::MapPaintRequest&q,const n::MapPaintCancelled&cancel){if(cancel())return {};const auto g=*m::mapRasterGeometry(q.viewport,q.contentsScale);auto pixels=std::make_shared<n::MapPaintImage>();pixels->width=pixels->height=g.pixelDimension;pixels->straightRGBA.assign(std::size_t(g.pixelDimension)*g.pixelDimension*4,127);n::MapPaintResult result{pixels,{},m::MapRasterFrame{g.viewport,g.screenRect,g.pixelsPerPoint},.001};if(q.backdrop){auto b=std::make_shared<n::MapPaintImage>();b->width=1024;b->height=512;b->straightRGBA.assign(1024*512*4,96);result.backdrop=b;}return result;}
void reentry(){
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};bool armed{},destroy{};std::unique_ptr<t::MapPreview>owner;
    const auto options=[&]{t::MapPreviewOptions o;o.geography=geography();o.clock=[&]{return now;};o.paint=paint;o.raster.pixelsPerPoint=1;o.player={image(2),image(2),image(2),image(880)};o.appearance.ambient=false;o.changed=[&]{if(!armed)return;armed=false;if(destroy)owner.reset();else owner->setOverlayVisible(false,now);};return o;};
    owner=std::make_unique<t::MapPreview>(raster,utility,options());owner->resize({1280,800,96,1,1280,800});c::ModulePresentation modules(c::Module::map);c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::map,true};const auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
    armed=true;owner->update(center,settings,modules.sample(now).presentation,1,now);check(!owner->state().active()&&!owner->rasterStats().active&&!owner->nextWakeTime()&&utility.stats().accepted==0,"Reentrant hide supersedes stale outer activation before any terrain task");
    now+=.1;owner->update(center,settings,modules.sample(now).presentation,1,now);check(!owner->state().active()&&!owner->rasterStats().active,"Following source presentation cannot reopen a caller-hidden owner");owner.reset();
    owner=std::make_unique<t::MapPreview>(raster,utility,options());auto appearance=n::MapAppearance{};appearance.dark=false;armed=true;destroy=true;check(owner->setAppearance(appearance,now)&&!owner,"Appearance invalidation may destroy owner without continuing on retired state");utility.waitIdle();utility.drain();utility.shutdown();
}
// Launch prewarm (ModuleOwner::prewarm): while the HUD is still closed the
// Map builds and poses its first opening's content at opacity 0, the prewarm
// publication uploads it, and the first opening frame only re-poses it. The
// prewarm activates nothing; geography that arrives after it re-syncs the
// caption at once instead of on the opening frame.
void prewarmFirstOpening(n::Renderer&r){
    using Clock=std::chrono::steady_clock;const auto ms=[](Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();};
    struct Run{double prewarm{},open{};std::uint64_t rasterizations{},layouts{},uploads{},targets{},chromeBuilds{},residentBytes{};std::size_t resourceBytes{};};
    const auto measure=[&](bool warm,bool lateGeography){
        n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};
        t::MapPreviewOptions o;if(!lateGeography)o.geography=geography();o.clock=[&]{return now;};o.paint=paint;o.raster.pixelsPerPoint=2;o.appearance.ambient=false;o.player={image(2),image(2),image(2),image(880)};
        t::MapPreview owner(raster,utility,std::move(o));owner.resize({1280,800,96,1,1280,800});c::ModulePresentation modules(c::Module::map);c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::map,true};const auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
        n::LayerComposition composition;Run result;const auto publish=[&]{owner.upload(r);composition.setEntries(r,owner.entries());composition.present(r);};
        if(warm){const auto begin=Clock::now();owner.prewarm(center,settings,modules.sample(now).presentation,now);publish();r.draw(false);result.prewarm=ms(Clock::now()-begin);
            check(!owner.state().active()&&!owner.rasterStats().active&&utility.stats().accepted==0&&!owner.nextWakeTime()&&!owner.requiresFrames(now)&&owner.accessibility().status.empty()&&!owner.clientRect({0,0,440,440})&&owner.sceneStats().chromeBuilds==1,
                "The launch prewarm builds the Map content and activates nothing: no input, pulses, terrain paint, deadline, frame demand or peers");
            owner.prewarm(center,settings,modules.sample(now).presentation,now);check(owner.sceneStats().chromeBuilds==1,"The launch prewarm runs once");}
        if(lateGeography){now+=.2;owner.setGeography(geography(),now);check(!owner.rasterStats().active&&utility.stats().accepted==0&&owner.sceneStats().chromeBuilds==2,"Geography arriving while closed rebuilds the prewarmed caption and paints nothing");}
        const auto text=raster.stats();const auto gpu=r.stats();const auto scene=owner.sceneStats();
        now+=.5;const auto begin=Clock::now();owner.setOverlayVisible(true,now);owner.update(center,settings,modules.sample(now).presentation,1,now);publish();result.open=ms(Clock::now()-begin);r.draw(false);
        result.rasterizations=raster.stats().rasterizations-text.rasterizations;result.layouts=raster.stats().textLayoutsCreated-text.textLayoutsCreated;result.uploads=r.stats().textureUploads-gpu.textureUploads;
        result.targets=r.stats().nativeGroupTargetAllocations-gpu.nativeGroupTargetAllocations;result.chromeBuilds=owner.sceneStats().chromeBuilds-scene.chromeBuilds;result.resourceBytes=r.stats().resourceBytes;
        check(owner.rasterStats().active&&owner.state().active()&&!owner.entries().empty(),"The opening shows the Map, accepts input and starts its terrain paint");
        utility.waitIdle();utility.drain();owner.utilityCompleted(now);result.residentBytes=owner.residency().cpuBytes;check(result.residentBytes>880u*880*4&&owner.residency().threads==0,"The Map reports its retained images and geography and no thread of its own");composition.detach(r);owner.release(r);check(r.stats().textures==0,"The prewarm fixture releases every Map resource");utility.shutdown();
        return result;};
    const auto cold=measure(false,false),warm=measure(true,false),late=measure(true,true);
    check(cold.rasterizations>0&&cold.uploads>0&&cold.targets>0,"Without the prewarm the first opening frame rasterizes the Map and uploads its resources");
    check(warm.rasterizations==0&&warm.layouts==0&&warm.uploads==0&&warm.targets==0&&warm.chromeBuilds==0,"After the prewarm the first opening frame rasterizes, shapes, uploads and allocates nothing");
    check(late.rasterizations==0&&late.layouts==0&&late.chromeBuilds==0&&late.targets==0,"Geography arriving after the prewarm leaves no rasterization on the opening frame");
    std::cout<<"MAP_FIRST_OPEN cold="<<cold.open<<" ms prewarmed="<<warm.open<<" ms (prewarm "<<warm.prewarm<<" ms) late-geography="<<late.open<<" ms; texture uploads cold="<<cold.uploads<<" prewarmed="<<warm.uploads<<" late="<<late.uploads<<"; renderer bytes shown="<<warm.resourceBytes<<"; owner CPU bytes after the first paint="<<warm.residentBytes<<"\n";
}
// Desktop Map occlusion (core/map_source_cutout.hpp; RC 131: "the map should
// be under the storage and activity monitor buttons, not on top of it"). With
// the presented source frame, the Storage and Activity Monitor plates below
// the circle stay above the Map, as on the Mac (HUDSourceWatchView desktop
// map occlusion). The source shell is drawn below every native layer, so a
// stand-in for it is composed first: the readback shows it inside the plates
// and the Map elsewhere, including the plates' transparent corners. Presses
// inside the plates are left to the navigation (covers() and pointer()
// decline them, the host's source hit filter then accepts the buttons).
void occlusion(n::Renderer&r){
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};bool shell{true};
    std::vector<c::source::Node>nodes(2);nodes[0].id="storage";nodes[0].path="WatchPanel_PC/Content/WatchNode/canvas_watch/HudBgShdow/TechtreeNode/TechtreeBtn/Icon";nodes[1].id="activity";nodes[1].path="WatchPanel_PC/Content/WatchNode/canvas_watch/HudBgShdow/ReportNode/ReportBtn/Icon";
    const c::source::SourceRect plateRect{{-138,-55},{276,110}};std::vector<c::source::ResolvedNode>resolved{{&nodes[0],{},{},plateRect,true},{&nodes[1],{},{},plateRect,true}};std::vector<double>alpha{0,0};const c::source::CameraFrame source{}; // plates faded out until placed
    t::MapPreviewOptions o;o.geography=geography();o.clock=[&]{return now;};o.paint=paint;o.raster.pixelsPerPoint=2;o.appearance.ambient=false;o.player={image(2),image(2),image(2),image(880)};
    o.sourceFrame=[&]()->std::optional<m::MapSourceView>{if(!shell)return std::nullopt;return m::MapSourceView{resolved,alpha,&source};};
    t::MapPreview owner(raster,utility,std::move(o));owner.resize({1280,800,96,1,1280,800});c::ModulePresentation modules{c::Module::power};n::LayerScene geometry(raster);geometry.load(blank(),{});n::NativeModuleSurface plane(geometry,c::Module::map);
    c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::power,true};auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto camera=n::layerViewportProjection(1280,800);
    n::LayerScene shellStandIn(raster);shellStandIn.load(J(J::Object{{"bounds",J::Array{0,0,1280,800}},{"children",J::Array{J(J::Object{{"id","source.shell"},{"kind","layer"},{"class","CALayer"},{"bounds",J::Array{0,0,1280,800}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{0,1,0,1}}}}})}}}),{});
    n::LayerComposition composition;std::vector<n::LayerCompositionEntry>ordered;ordered.reserve(8);bool released{};
    struct Cleanup{n::Renderer&r;n::LayerComposition&c;n::LayerScene&s;t::MapPreview&o;bool&done;~Cleanup(){if(done)return;try{c.detach(r);(void)s.releaseResources(r);o.release(r);}catch(...){r.reset();}}}cleanup{r,composition,shellStandIn,owner,released};
    const auto frame=[&](double dt=.01){now+=dt;const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(r);ordered.clear();ordered.push_back({&shellStandIn,{}});for(const auto&e:owner.entries())ordered.push_back(e);composition.setEntries(r,ordered);composition.present(r);r.setCamera(camera);};
    const auto point=[&](c::Point p){const auto sample=modules.sample(now).presentation;const auto*shown=sample.current.module==c::Module::map?&sample.current:&*sample.incoming;plane.update(center,settings,*shown);return *c::Projection::viewport(camera*plane.pose().contentWorld,1280,800).project(p);};
    const auto drain=[&]{utility.waitIdle();utility.drain();owner.utilityCompleted(now);};
    const auto settle=[&]{for(unsigned k=0;k<16;++k){drain();if(const auto due=owner.nextWakeTime()){now=std::max(now,*due)+.000001;owner.deadline(now);continue;}if(!utility.stats().running&&!utility.stats().pending&&!utility.stats().completed)break;}frame(.21);};
    modules.select(c::Module::map,now);frame(.02);drain();frame(1);settle();check(owner.state().active(),"The occlusion fixture shows a stable Map");
    // The untilted plane maps canvas points to client points by one scale.
    const auto o0=point({0,0}),o1=point({440,440});const double k=(o1.x-o0.x)/440;check(k>0&&std::abs((o1.y-o0.y)/440-k)<1e-9,"The untilted Map canvas maps to client points by one uniform scale");
    // A plate's normalized (u, v) lands on canvas (x0 + 140u, y1 - 56v): its
    // rect-local Unity point (y up) goes through the node world to NDC.
    std::array<c::Point,2>origins{};
    const auto place=[&](std::size_t index,double x0,double y1){origins[index]={o0.x+k*x0,o0.y+k*y1};const double kx=k*140/276,ky=k*56/110,cx=origins[index].x+kx*138,cy=origins[index].y-ky*55;
        resolved[index].worldMatrix=c::Matrix4::translation(2*cx/1280-1,1-2*cy/800,0)*c::Matrix4::scale(2*kx/1280,2*ky/800,1);};
    const auto plateClient=[&](std::size_t index,double u,double v){return c::Point{origins[index].x+k*140*u,origins[index].y-k*56*v};};
    std::vector<std::uint8_t>pixels;std::uint32_t rowBytes{};
    const auto draw=[&]{r.draw(false);auto image=r.readback();rowBytes=image.rowBytes;pixels=std::move(image.pixels);};
    const auto at=[&](c::Point p){const auto x=std::size_t(std::lround(p.x)),y=std::size_t(std::lround(p.y));const auto i=y*rowBytes+x*4;return std::array<int,3>{pixels.at(i),pixels.at(i+1),pixels.at(i+2)};};
    const auto shellShows=[&](c::Point p){const auto v=at(p);return v[1]>=250&&v[0]<=5&&v[2]<=5;};
    const auto mapShows=[&](c::Point p){return at(p)[1]<200;};
    const auto down=[&](c::Point p,a::PointerButton b=a::PointerButton::left){return owner.pointer({a::PointerKind::down,b,p.x,p.y},now);};
    const auto up=[&](c::Point p){return owner.pointer({a::PointerKind::up,a::PointerButton::left,p.x,p.y},now);};
    check(owner.sourceCutouts().empty()&&owner.sceneStats().cutoutMasks==0,"Faded-out plates cut nothing");
    place(0,80,400);place(1,230,400);alpha={1,1};frame();draw();
    const auto storage=plateClient(0,.6,.8),activity=plateClient(1,.3,.6),corner=plateClient(0,.15,.15),interior=point({220,220});
    check(owner.sourceCutouts().count==4&&owner.sceneStats().cutoutPolygons==4&&owner.sceneStats().cutoutMasks==1,"The presented plates cut the Map's mask once");
    check(shellShows(storage)&&shellShows(activity),"The Storage and Activity Monitor plates are drawn above the Map");
    check(mapShows(interior)&&mapShows(corner),"The Map still covers its circle and the plates' transparent corners");
    check(!owner.covers(storage)&&!owner.covers(activity),"The Map leaves the plates to the navigation's hit test (the source hit filter accepts them)");
    check(owner.covers(corner)&&owner.covers(interior),"The Map keeps the plates' transparent corners and the rest of its circle");
    const auto pins=owner.state().pins().size();const auto viewport=owner.state().viewport();
    check(!down(storage)&&!owner.pointerLocked()&&!owner.state().dragging(),"A press on the Storage plate starts no Map drag");check(!up(storage)&&owner.state().viewport()==viewport,"Its release is not the Map's and the camera stays");
    check(!down(activity,a::PointerButton::right)&&owner.state().pins().size()==pins,"A right press on the Activity Monitor plate adds no pin");
    check(!owner.pointer({a::PointerKind::move,a::PointerButton::none,storage.x,storage.y},now),"Hover over a plate stays with the navigation");
    check(!owner.magnify(storage,.2,false,now)&&owner.state().viewport()==viewport,"A pinch on a plate leaves the Map");
    // Stable frames repaint nothing and allocate nothing.
    const auto masks=owner.sceneStats().cutoutMasks;const auto gpu=r.stats();allocations=0;counting=true;try{for(unsigned n=0;n<60;++n)frame(1./60.);}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&owner.sceneStats().cutoutMasks==masks&&r.stats().textureRegionUploads==gpu.textureRegionUploads&&r.stats().textureUploads==gpu.textureUploads,"Unchanged plates repaint and allocate nothing on warm frames");
    check(down(corner)&&owner.pointerLocked(),"A press in a plate's transparent corner still drags the Map");const auto across=plateClient(0,.6,.8);
    check(owner.pointer({a::PointerKind::move,a::PointerButton::none,across.x,across.y},now),"An owned Map drag continues across a plate");check(up(across)&&!owner.pointerLocked(),"The Map drag ends normally");settle();
    // The entrance or the tilt moves a plate: only its texels are repainted in place.
    draw();const auto before=plateClient(0,.05,.95);check(shellShows(before)&&owner.sceneStats().cutoutMasks==masks,"The plate's top-left is drawn above the dragged Map");
    const auto regions=r.stats().textureRegionUploads;place(0,80,380);frame();draw();const auto moved=plateClient(0,.6,.8);
    check(owner.sceneStats().cutoutMasks==masks+1&&r.stats().textureRegionUploads==regions+1,"A moved plate repaints one mask region in place");
    check(shellShows(moved)&&mapShows(before)&&!owner.covers(moved)&&owner.covers(before),"The cut and the hit precedence follow the moved plate");
    // A tilted Map plane: the plates stay where the source draws them.
    center=center*c::Matrix4::rotation(.04,-.07,0);frame();draw();
    check(shellShows(moved)&&shellShows(activity)&&!owner.covers(moved)&&mapShows(point({220,220})),"Under tilt the cut follows the source plates in client space");
    // A faded-out plate (inherited alpha .001) no longer cuts the Map.
    alpha[0]=.0005;frame();draw();check(owner.sourceCutouts().count==3&&mapShows(moved)&&owner.covers(moved)&&shellShows(activity),"A hidden Storage plate leaves the Map whole there");
    // Without a source frame (or off the source shell) nothing is cut.
    alpha[0]=1;shell=false;frame();draw();check(owner.sourceCutouts().empty()&&owner.sceneStats().cutoutPolygons==0&&mapShows(activity)&&owner.covers(activity),"Without the source frame the Map covers its whole circle");
    shell=true;settings.sourceShell=false;frame();check(owner.sourceCutouts().empty(),"Off the source shell no plate is cut");settings.sourceShell=true;frame();check(owner.sourceCutouts().count==4,"Back on the source shell the plates are cut again");
    composition.detach(r);check(shellStandIn.releaseResources(r),"The shell stand-in releases its resources");owner.release(r);released=true;check(r.stats().textures==0&&r.stats().meshes==0,"The occluded Map releases its updatable mask with every resource");utility.shutdown();
}
// The host's cut of the other module (map-transition-host.patch: a
// LayerCompositionEntry::cut applied by LayerComposition::present). Without
// that patch the stand-in scene carries the cut as its group alpha mask.
template<class Entry>bool compositionCut(Entry&entry,const n::PlaneAlphaMask*cut){if constexpr(requires(Entry&e){e.cut;}){entry.cut=cut;return true;}else{(void)entry;(void)cut;return false;}}
// Desktop Map occlusion through module switches. The Mac masks the whole
// module host while the Map is presented, current or incoming
// (SystemHUDView.updateMapOcclusionPresentation: isPresenting(.map)): the
// outgoing and incoming content and the incoming wrapper's registration
// brackets. Into the Map the other module is drawn on the Map's 1.14 host
// and reaches over the plates; the Map's transition cut (an alpha mask of
// the module host plane) keeps them above it. A white full-height module
// (Calendar's 400 x 440 frame) stands in for the other owner, cut through
// its scene's alpha mask as the host applies the cut to its entries.
void transitions(n::Renderer&r){
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};
    std::vector<c::source::Node>nodes(2);nodes[0].id="storage";nodes[0].path="WatchPanel_PC/Content/WatchNode/canvas_watch/HudBgShdow/TechtreeNode/TechtreeBtn/Icon";nodes[1].id="activity";nodes[1].path="WatchPanel_PC/Content/WatchNode/canvas_watch/HudBgShdow/ReportNode/ReportBtn/Icon";
    const c::source::SourceRect plateRect{{-138,-55},{276,110}};std::vector<c::source::ResolvedNode>resolved{{&nodes[0],{},{},plateRect,true},{&nodes[1],{},{},plateRect,true}};std::vector<double>alpha{0,0};const c::source::CameraFrame source{};
    t::MapPreviewOptions o;o.geography=geography();o.clock=[&]{return now;};o.paint=paint;o.raster.pixelsPerPoint=2;o.appearance.ambient=false;o.player={image(2),image(2),image(2),image(880)};
    o.sourceFrame=[&]()->std::optional<m::MapSourceView>{return m::MapSourceView{resolved,alpha,&source};};
    t::MapPreview owner(raster,utility,std::move(o));owner.resize({1280,800,96,1,1280,800});c::ModulePresentation modules{c::Module::map};n::LayerScene geometry(raster);geometry.load(blank(),{});n::NativeModuleSurface plane(geometry,c::Module::map);
    c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::map,true};const auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto camera=n::layerViewportProjection(1280,800);
    const auto layer=[](const char*id,double w,double h,std::array<double,4>rgba){return J(J::Object{{"bounds",J::Array{0,0,w,h}},{"children",J::Array{J(J::Object{{"id",id},{"kind","layer"},{"class","CALayer"},{"bounds",J::Array{0,0,w,h}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{rgba[0],rgba[1],rgba[2],rgba[3]}}}}})}}});};
    n::LayerScene shellStandIn(raster);shellStandIn.load(layer("source.shell",1280,800,{0,1,0,1}),{});
    constexpr auto other=c::Module::calendar;const auto otherFrame=c::moduleLocalContentFrame(other);
    n::LayerScene otherScene(raster);otherScene.load(layer("other.content",otherFrame.width,otherFrame.height,{1,1,1,1}),{});n::NativeModuleSurface otherPlane(otherScene,other);
    n::LayerComposition composition;std::vector<n::LayerCompositionEntry>ordered;ordered.reserve(8);bool released{},applyCut{true};
    struct Cleanup{n::Renderer&r;n::LayerComposition&c;n::LayerScene&s,&x;t::MapPreview&o;bool&done;~Cleanup(){if(done)return;try{c.detach(r);(void)s.releaseResources(r);(void)x.releaseResources(r);o.release(r);}catch(...){r.reset();}}}cleanup{r,composition,shellStandIn,otherScene,owner,released};
    // The host's publication: the current module's entries, then the incoming
    // module's; the other module carries the Map's transition cut.
    const auto frame=[&](double dt){now+=dt;const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(r);
        const auto*otherSurface=sample.current.module==other?&sample.current:sample.incoming&&sample.incoming->module==other?&*sample.incoming:nullptr;
        const auto*cut=applyCut?owner.transitionCut():nullptr;n::LayerCompositionEntry otherEntry{&otherScene,{}};
        if(otherSurface){otherPlane.update(center,settings,*otherSurface);if(!compositionCut(otherEntry,cut))otherScene.setGroupAlphaMask(cut?std::optional<n::PlaneAlphaMask>(*cut):std::nullopt);}
        ordered.clear();ordered.push_back({&shellStandIn,{}});
        const auto append=[&](c::Module module){if(module==other&&otherSurface)ordered.push_back(otherEntry);else if(module==c::Module::map)for(const auto&e:owner.entries())ordered.push_back(e);};
        append(sample.current.module);if(sample.incoming)append(sample.incoming->module);
        composition.setEntries(r,ordered);composition.present(r);r.setCamera(camera);return sample;};
    const auto point=[&](c::Point p){plane.update(center,settings,modules.sample(now).presentation.current);return *c::Projection::viewport(camera*plane.pose().contentWorld,1280,800).project(p);};
    const auto settle=[&]{for(unsigned k=0;k<16;++k){utility.waitIdle();utility.drain();owner.utilityCompleted(now);if(const auto due=owner.nextWakeTime()){now=std::max(now,*due)+.000001;owner.deadline(now);continue;}if(!utility.stats().running&&!utility.stats().pending&&!utility.stats().completed)break;}frame(.21);};
    frame(.02);settle();check(owner.state().active(),"The switch fixture shows a stable Map");
    // The plates below the circle, placed through the settled Map plane as in occlusion().
    const auto o0=point({0,0}),o1=point({440,440});const double k=(o1.x-o0.x)/440;
    const auto place=[&](std::size_t index,double x0,double y1){const c::Point origin{o0.x+k*x0,o0.y+k*y1};const double kx=k*140/276,ky=k*56/110,cx=origin.x+kx*138,cy=origin.y-ky*55;
        resolved[index].worldMatrix=c::Matrix4::translation(2*cx/1280-1,1-2*cy/800,0)*c::Matrix4::scale(2*kx/1280,2*ky/800,1);};
    place(0,80,400);place(1,230,400);alpha={1,1};frame(.01);check(owner.sourceCutouts().count==4,"The plates are cut at rest");
    check(!owner.transitionCut()&&owner.sceneStats().transitionCutBytes==0,"Settled, the Map keeps no transition cut and no texture for it");
    // Points well inside the plates (3 points of margin), on a 3-point grid.
    std::vector<c::Point>deep;{const auto&plates=owner.sourceCutouts();for(std::size_t n=0;n<plates.count;++n){double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;for(const auto p:plates.polygons[n].view()){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}
        for(double y=std::ceil(y0);y<=y1;y+=3)for(double x=std::ceil(x0);x<=x1;x+=3){bool inside=plates.contains({x,y});for(const auto d:{c::Point{-3,0},c::Point{3,0},c::Point{0,-3},c::Point{0,3}})inside=inside&&plates.contains({x+d.x,y+d.y});if(inside)deep.push_back({x,y});}}}
    check(deep.size()>200,"The plates hold enough interior sample points");
    n::Readback image;const auto draw=[&]{r.draw(false);image=r.readback();};
    const auto pixel=[&](c::Point p){const auto i=std::size_t(std::lround(p.y))*image.rowBytes+std::size_t(std::lround(p.x))*4;return std::array<int,3>{image.pixels.at(i),image.pixels.at(i+1),image.pixels.at(i+2)};};
    const auto shellShows=[&](c::Point p){const auto v=pixel(p);return v[1]>=250&&v[0]<=5&&v[2]<=5;};
    const auto platesShown=[&]{std::size_t shown{};for(const auto p:deep)shown+=shellShows(p)?1:0;return shown;};
    draw();check(platesShown()==deep.size(),"At rest the plates show above the Map");
    // Out of the Map: Map -> Calendar (the Map outgoing, Calendar incoming at its own host).
    modules.select(other,now);unsigned cutFrames{};std::size_t worst=deep.size();
    for(unsigned f=0;f<14;++f){const auto sample=frame(.025);if(!sample.transitioning)break;const auto*cut=owner.transitionCut();cutFrames+=cut?1:0;
        check(cut&&cut->textureID==owner.entries().back().after[0].alphaMask.value_or(n::PlaneAlphaMask{}).textureID,"Leaving the Map, its transition cut is published (the brackets carry it)");
        draw();worst=std::min(worst,platesShown());}
    check(cutFrames>=10&&worst==deep.size(),"While the Map leaves, every plate point shows above the outgoing Map and the incoming module");
    frame(.025);check(!modules.sample(now).presentation.transitioning&&!owner.transitionCut(),"On Calendar the Map publishes no cut");
    frame(.02);check(owner.sceneStats().transitionCutBytes==0,"Once no published draw samples it, the hidden Map releases the transition cut texture");
    // Into the Map: Calendar (outgoing, now on the Map's 1.14 host) -> Map.
    modules.select(c::Module::map,now);applyCut=false;frame(.02);draw();const auto uncut=platesShown();
    applyCut=true;frame(.0001);draw();const auto first=platesShown();
    {n::LayerCompositionEntry probe{};std::printf("MAP_SWITCH stand-in (cut through %s): entering the Map the outgoing module covers %zu of %zu plate points without the cut, %zu with it\n",compositionCut(probe,nullptr)?"the composition entry":"the scene alpha mask",deep.size()-uncut,deep.size(),deep.size()-first);}
    check(uncut<deep.size(),"Without the cut the outgoing module, on the Map's larger host, covers the plates (the switch flicker)");
    check(first==deep.size()&&owner.sceneStats().transitionCutBytes==880u*880*4,"With the transition cut the plates stay above it (one 880 x 880 texture, two texels per point)");
    cutFrames=0;worst=deep.size();bool brackets{};
    for(unsigned f=0;f<14;++f){const auto sample=frame(.025);if(!sample.transitioning)break;const auto*cut=owner.transitionCut();cutFrames+=cut?1:0;
        const auto&seam=owner.entries().back().after[0];brackets=brackets||(seam.opacity>0&&seam.alphaMask&&cut&&seam.alphaMask->textureID==cut->textureID&&seam.alphaMask->worldToLocal==cut->worldToLocal);
        draw();worst=std::min(worst,platesShown());}
    check(cutFrames>=10&&worst==deep.size(),"While the Map enters, every plate point shows above the outgoing module and the incoming Map");
    check(brackets,"The incoming Map's registration brackets are drawn through the transition cut");
    frame(.025);check(!modules.sample(now).presentation.transitioning&&!owner.transitionCut()&&!owner.entries().back().after[0].alphaMask,"Settled on the Map no cut is published");
    draw();check(platesShown()==deep.size(),"Settled, the plates show above the Map");
    composition.detach(r);check(shellStandIn.releaseResources(r)&&otherScene.releaseResources(r),"The stand-ins release their resources");owner.release(r);released=true;
    check(r.stats().textures==0&&r.stats().meshes==0,"The Map releases its transition cut with every resource");utility.shutdown();
}
void run(n::Renderer&r,HWND window){
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});double now{};unsigned saves{},changes{},styles{},recenters{},nextID{1};std::atomic<unsigned>paints{},clears{};m::MapSnapshot saved;
    t::MapPreviewOptions options;options.geography=geography();options.clock=[&]{return now;};options.changed=[&]{++changes;};options.pinStyleChanged=[&](auto){++styles;};options.recentered=[&]{++recenters;};options.paint=[&](const auto&q,const auto&cancel){++paints;return paint(q,cancel);};options.releaseWorkerCaches=[&]{++clears;};options.raster.pixelsPerPoint=1;options.appearance.ambient=false;options.player={image(2),image(2),image(2),image(880)};options.persistence={ [&](const auto&s){saved=s;++saves;},[&]{return std::string("00000000-0000-0000-0000-")+std::string(11,'0')+char('0'+nextID++);},[]{return 1.;}};
    t::MapPreview owner(raster,utility,std::move(options));owner.resize({1280,800,96,1,1280,800});n::LayerComposition composition;c::ModulePresentation modules{c::Module::power};n::LayerScene geometry(raster);geometry.load(blank(),{});n::NativeModuleSurface plane(geometry,c::Module::map);c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::power,true};auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto camera=n::layerViewportProjection(1280,800);bool released{};
    struct Cleanup{n::Renderer&r;n::LayerComposition&c;t::MapPreview&o;bool&done;~Cleanup(){if(done)return;try{c.detach(r);o.release(r);}catch(...){r.reset();}}}cleanup{r,composition,owner,released};
    const auto frame=[&](double dt=.01){now+=dt;const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(r);composition.setEntries(r,owner.entries());composition.present(r);r.setCamera(camera);};
    const auto point=[&](c::Point p){const auto sample=modules.sample(now).presentation;const auto*shown=sample.current.module==c::Module::map?&sample.current:&*sample.incoming;plane.update(center,settings,*shown);return *c::Projection::viewport(camera*plane.pose().contentWorld,1280,800).project(p);};
    const auto drain=[&]{utility.waitIdle();utility.drain();owner.utilityCompleted(now);};
    const auto settle=[&]{for(unsigned k=0;k<16;++k){drain();if(const auto due=owner.nextWakeTime()){now=std::max(now,*due)+.000001;owner.deadline(now);continue;}if(!utility.stats().running&&!utility.stats().pending&&!utility.stats().completed)break;}frame(.21);};
    const auto press=[&](c::Point p,a::PointerButton b=a::PointerButton::left){const auto q=point(p);return owner.pointer({a::PointerKind::down,b,q.x,q.y},now);};
    const auto release=[&](c::Point p){const auto q=point(p);return owner.pointer({a::PointerKind::up,a::PointerButton::left,q.x,q.y},now);};
    frame();check(owner.entries().empty()&&utility.stats().accepted==0&&!owner.nextWakeTime(),"Hidden initial Map does no worker, raster or deadline work");
    modules.select(c::Module::map,now);frame(.02);check(owner.rasterStats().active&&!owner.state().active(),"Incoming Map prepares source terrain before input and pin pulses activate");check(!owner.covers(point({220,220}))&&!press({220,220}),"Mechanical module transition rejects Map input");drain();frame(1);settle();check(owner.state().active()&&owner.rasterStats().published==1,"Stable Map activates and retains one prepared geography frame");check(owner.covers(point({220,5}))&&!owner.covers(point({2,2})),"Map uses original440-point circle, not the standard334-point central clip");
    r.draw(false);check(!r.readback().pixels.empty(),"Shared composition renders synthetic Map without a new window or renderer");
    const auto accepted=utility.stats().accepted;const auto notices=changes;const auto text=raster.stats();const auto gpu=r.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k)frame(1./60.);}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"Warm Map owner frames allocate nothing");check(utility.stats().accepted==accepted&&changes==notices&&raster.stats().rasterizations==text.rasterizations&&r.stats().textureUploads==gpu.textureUploads,"Stable Map frames schedule, notify and repaint nothing");
    const auto firstZoom=owner.state().viewport().zoom;check(press({37,195})&&!owner.pointerLocked(),"Toolbar press reaches source control without freezing gyro as a drag");check(release({37,195}),"Toolbar release is owned");check(owner.state().viewport().zoom>firstZoom&&owner.requiresFrames(now),"Zoom action starts original finite camera motion");settle();
    const auto beforeDrag=saves;check(press({260,250})&&owner.pointerLocked(),"Terrain press begins exact Map drag lock");auto outside=point({490,270});check(owner.pointer({a::PointerKind::move,a::PointerButton::none,outside.x,outside.y},now),"An owned Map drag continues outside the circular hit boundary");check(saves==beforeDrag,"Pointer samples never persist camera");check(release({490,270})&&!owner.pointerLocked()&&saves==beforeDrag+1,"Mouse-up persists changed camera exactly once");settle();check(saved.viewport==owner.state().viewport(),"Committed camera matches final drag pose");
    const auto beforeCancel=owner.state().viewport();const auto savedBeforeCancel=saves;check(press({270,270}),"Synthetic cancellation begins inside Map");owner.cancelInteraction(now);check(owner.state().viewport()==beforeCancel&&saves==savedBeforeCancel&&!owner.pointerLocked(),"Cancelled untouched click does not recenter or persist");
    const auto zoom=owner.state().viewport().zoom;check(press({270,260})&&release({270,260}),"Clicking terrain is owned");check(recenters==1&&owner.state().viewport().zoom==zoom,"Source recenter preserves zoom and emits one event");settle();
    check(owner.perform(m::MapAction::addPin,{},now),"Source center-pin action is available");frame();check(owner.state().pins().size()==1&&owner.state().selection(),"Pin edit preserves retained state and selection");const auto id=owner.state().pins()[0].id;check(owner.perform(m::MapAction::pin,id,now),"Source pin cycles style");frame();check(styles==1&&owner.state().pins()[0].style==m::MapPinStyle::green,"Style event publishes only after successful injected persistence");frame(.17);
    // UI Automation peers (HUDWorldMapInteraction.layoutAccessibility) and the
    // source requestAccessibilityLayout cadence while a drag locks input.
    unsigned peerChanges{};auto mapPeers=std::make_unique<n::NativeScreenReaderPeers>(t::mapAutomation(owner,{window,[&]{return now;},[&]{++peerChanges;}}));
    {namespace probe=endfield::tests::peers;const auto expected=m::mapAccessibility(owner.state(),c::Language::english,owner.state().error());
     check(mapPeers->refresh(nullptr)&&owner.accessibility()==expected&&mapPeers->size()==expected.elements.size()+1&&expected.status=="Map","The Map lists its status and one peer per accessible action");
     const auto status=probe::find(*mapPeers,"map:status");check(status&&probe::integer(status,UIA_ControlTypePropertyId)==UIA_TextControlTypeId&&probe::name(status)==L"Map"&&probe::help(status)==probe::wide(expected.help)&&probe::sameRect(probe::bounds(status),n::screenReaderClientToScreen(window,1,owner.clientRect({0,0,440,440}))),"The status is static text over the projected canvas with the source help");
     for(const auto&e:expected.elements){const auto f=probe::find(*mapPeers,e.id);check(f&&probe::name(f)==probe::wide(e.label)&&probe::help(f)==probe::wide(expected.help)&&probe::integer(f,UIA_ControlTypePropertyId)==UIA_ButtonControlTypeId&&probe::boolean(f,UIA_IsEnabledPropertyId)==e.enabled&&probe::sameRect(probe::bounds(f),n::screenReaderClientToScreen(window,1,owner.clientRect(e.rect))),"Each action peer carries its source label, help, state and projected rect");}
     const std::string pinID="map:pin:"+owner.state().pins()[0].id;const auto pin=probe::find(*mapPeers,pinID);check(pin&&probe::name(pin).starts_with(L"Green pin 1, "),"The pin reads its style, index and coordinates");
     const auto zoomBefore=owner.state().viewport().zoom;check(probe::invoke(probe::find(*mapPeers,"map:zoomIn"))==S_OK&&owner.state().viewport().zoom>zoomBefore&&peerChanges==1,"Zoom in presses like its click");settle();
     check(probe::invoke(pin)==S_OK&&owner.state().pins()[0].style==m::MapPinStyle::player&&styles==2&&peerChanges==2,"A pin press cycles its style like its click");frame();
     check(!owner.perform(std::string_view("map:bogus"),now)&&!owner.perform(std::string_view("map:pin:00000000-0000-0000-0000-000000000099"),now),"Only listed accessible actions perform");settle();
     const auto pinRect=[&]{for(const auto&e:owner.accessibility().elements)if(e.id==pinID)return std::optional<c::Rect>(e.rect);return std::optional<c::Rect>{};};
     (void)mapPeers->refresh(nullptr);const auto before=pinRect();check(before&&!owner.nextWakeTime(),"An idle Map publishes at once and schedules nothing");
     const auto drag=[&](double x){now+=.01;const auto q=point({x,300});owner.pointer({a::PointerKind::move,a::PointerButton::none,q.x,q.y},now);(void)mapPeers->refresh(nullptr);return pinRect();};
     const auto shifted=[&](const std::optional<c::Rect>&r,double dx){return r&&std::abs(r->x-before->x-dx)<1e-4&&std::abs(r->y-before->y)<1e-4;};
     check(press({300,300})&&owner.pointerLocked(),"A terrain drag locks input");
     check(shifted(drag(320),20),"The first drag sample after a quiet period publishes at once");const auto published=now;
     check(shifted(drag(340),20)&&owner.nextWakeTime()&&*owner.nextWakeTime()<=published+1./12+1e-9,"A sample within 1/12 s keeps the last publication and waits for the 12 Hz cadence");
     now=published+1./12+.001;check(owner.deadline(now),"The due publication reports changed artwork through the shared deadline");(void)mapPeers->refresh(nullptr);check(shifted(pinRect(),40),"The deferred publication follows the camera");
     check(shifted(drag(360),40),"A further sample within 1/12 s is deferred again");
     check(release({360,300})&&!owner.pointerLocked(),"Releasing ends the drag");(void)mapPeers->refresh(nullptr);check(shifted(pinRect(),60),"The end of the gesture publishes the final pin rect at once");settle();check(!owner.nextWakeTime(),"No accessibility wake remains after the gesture");}
    // A bundled geography dataset that fails to load (WorldMapCanvas.requestTerrain,
    // one Result per dataset): the Map stays with its pins and input, the other
    // dataset still renders, and the source message is drawn and read as the
    // status until a pin edit clears it.
    {const auto pins=owner.state().pins().size();const auto visible=owner.sceneStats().visiblePins;const auto builds=owner.sceneStats().chromeBuilds;const auto published=owner.rasterStats().published;
     auto countries=std::make_shared<m::MapGeography>();countries->terrain=m::MapTerrain{};countries->countriesFailed=true;owner.setGeography(countries,now);frame();
     check(owner.state().error()==std::optional<std::string>("Country shapes could not be loaded.")&&owner.state().active()&&owner.state().pins().size()==pins&&owner.sceneStats().visiblePins==visible,"A country-shape failure keeps the Map, its input and its pins with the source message");
     check(owner.sceneStats().chromeBuilds>builds&&owner.accessibility().status=="Country shapes could not be loaded."&&owner.covers(point({220,220})),"The message is drawn over the canvas and read as the Map status");settle();
     check(owner.rasterStats().published>published,"The terrain that did load still renders");
     auto both=std::make_shared<m::MapGeography>();both->terrainFailed=both->countriesFailed=true;owner.setGeography(both,now);frame();
     check(owner.state().error()==std::optional<std::string>("Country shapes could not be loaded.")&&owner.state().pins().size()==pins,"With both datasets failed the country message is shown last, as in the source");
     auto terrain=std::make_shared<m::MapGeography>();terrain->terrainFailed=true;terrain->countries=m::MapCountries{};owner.setGeography(terrain,now);frame();
     check(owner.state().error()==std::optional<std::string>("Terrain data could not be loaded.")&&owner.accessibility().status=="Terrain data could not be loaded.","A terrain failure shows the terrain message");
     check(owner.perform(m::MapAction::pin,owner.state().pins()[0].id,now)&&!owner.state().error()&&owner.accessibility().status=="Map","A pin edit clears the message as in the source");
     owner.setGeography(geography(),now);frame();settle();check(!owner.state().error()&&owner.state().active(),"Complete geography shows no message");}
    const auto beforeWheel=owner.state().viewport();const auto wheelSaves=saves;const auto q=point({300,220});check(owner.wheel({q.x,q.y,.5,false,0,99},now)&&owner.pointerLocked(),"Fractional detent zoom uses source gesture lock, independent of OS text lines");const auto expected=m::mapZoomed(beforeWheel,{300,220},std::exp(.5*.12));check(std::abs(owner.state().viewport().zoom-expected.zoom)<1e-12&&saves==wheelSaves,"Wheel retains fractional source exponential sensitivity without per-sample saves");check(owner.nextWakeTime().has_value(),"Wheel end/refinement uses existing shared deadline");now+=.181;owner.deadline(now);check(!owner.pointerLocked()&&saves==wheelSaves+1,"Source180ms wheel completion saves once and releases lock");settle();
    center=center*c::Matrix4::rotation(.04,-.07,0);frame();check(owner.covers(point({220,5})),"Tilted Map uses its current exact source plane for hits");const auto v=owner.state().viewport();const auto selected=owner.state().selection();owner.setLanguage(c::Language::korean,now);frame();check(owner.state().viewport()==v&&owner.state().selection()==selected&&owner.state().pins().size()==1,"Locale replacement preserves camera, pins and selection");settle();const auto locale=raster.stats();owner.setLanguage(c::Language::korean,now);frame();check(raster.stats().rasterizations==locale.rasterizations,"Equal locale event shapes no text");
    auto appearance=n::MapAppearance{};appearance.dark=false;appearance.ambient=false;appearance.accent={.2,.5,.8,1};appearance.strings={"지도","지형 불러오는 중","핀"};check(owner.setAppearance(appearance,now),"Global theme/accent event is accepted");settle();check(owner.state().viewport()==v&&owner.state().selection()==selected,"Appearance keeps original durable Map identity");
    // Synthetic keys carry their modifier state: off the HUD's key dispatch the
    // thread's GetKeyState follows the live keyboard of the interactive session.
    check(!owner.key({a::KeyKind::down,VK_ESCAPE},true,now)&&owner.state().selection()==selected,"A modified Escape stays with host shortcuts and keeps the selection");
    check(owner.key({a::KeyKind::down,VK_ESCAPE},false,now)&&!owner.state().selection(),"Map consumes first Escape to clear selection");check(!owner.key({a::KeyKind::down,VK_ESCAPE},false,now),"Unselected Map leaves Escape for the shared HUD");
    check(press({280,270}),"Begin source gesture before closing");const auto dragged=point({300,270});owner.pointer({a::PointerKind::move,a::PointerButton::none,dragged.x,dragged.y},now);const auto closeSaves=saves;owner.setOverlayVisible(false,now);check(saves==closeSaves+1&&!owner.pointerLocked()&&!owner.state().active()&&!owner.rasterStats().active&&!owner.nextWakeTime(),"Closing commits changed gesture, cancels refinement and disables interaction");frame();drain();check(!owner.requiresFrames(now)&&!owner.covers(point({220,220})),"Closed HUD retains outgoing pixels without Map frame demand or hits");const auto hiddenAccepted=utility.stats().accepted;for(unsigned k=0;k<20;++k)frame();check(utility.stats().accepted==hiddenAccepted,"Closed pose frames do not paint geography");
    owner.setOverlayVisible(true,now);frame();settle();check(owner.state().active()&&owner.covers(point({220,220})),"Reopen reuses one retained Map owner and current camera");modules.select(c::Module::power,now);frame(.02);check(!owner.state().active()&&!owner.rasterStats().active&&!owner.entries().empty(),"Outgoing module preserves artwork and immediately disables source interaction");check(mapPeers->refresh(nullptr)&&mapPeers->size()==0&&!owner.clientRect({0,0,440,440})&&owner.accessibility().status.empty(),"A departed Map lists no peers and projects nothing");mapPeers.reset();frame(1);drain();check(owner.entries().empty()&&!owner.requiresFrames(now),"Departed Map detaches from combined composition");
    rejects([&]{owner.deadline(std::numeric_limits<double>::quiet_NaN());},"Nonfinite owner time is rejected");composition.detach(r);owner.release(r);released=true;check(r.stats().textures==0&&r.stats().meshes==0,"Map owner releases all local resources after shared detach");check(clears>0,"Hidden Map releases working caches on the existing utility worker");utility.shutdown();
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass original HUD shader");reentry();Window window;n::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests});run(renderer,window.hwnd);prewarmFirstOpening(renderer);occlusion(renderer);transitions(renderer);renderer.reset();CoUninitialize();std::cout<<"PASS "<<checks<<" Map owner checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
