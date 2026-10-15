#ifdef _WIN32
#include "tools/volume_preview.hpp"
#include "native/module_scene.hpp"
#include "core/module_presentation.hpp"
#include <objbase.h>
#include <atomic>
#include <chrono>
#include "core/source_camera.hpp"
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
// User-reported: the app list (more than two apps) did not scroll smoothly.
// Through the owner: three rows show, the wheel and a list drag move retained
// rows, and settled frames after a scroll do no work.
void scrolling(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;
    gpu::VolumeSnapshot data;data.outputs={{"endpoint-1","Speakers"}};data.outputID="endpoint-1";data.volume=.55;data.muted=false;data.canSetVolume=data.canSetMute=true;data.applicationActivitySupported=true;
    for(unsigned n=0;n<6;++n)data.applications.push_back({std::to_string(200+n)+":1","Synthetic app "+std::to_string(n),200+n,true,gpu::VolumeAppState::direct,{},{},{},{}});
    unsigned gains{};tools::VolumePreviewOptions options;options.initial=data;options.rasterDensity=1;options.actions.setAppGain=[&](auto,double){++gains;return true;};
    tools::VolumePreview preview(raster,std::move(options));preview.resize({1280,800,96,1,1280,800});core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::volume;core::ModulePresentation modules(core::Module::volume);double time{};
    gpu::LayerComposition composition;const auto frame=[&](double t){preview.update({},settings,modules.sample(t).presentation,1,t);preview.upload(renderer);composition.setEntries(renderer,preview.entries());preview.collected(renderer);composition.present(renderer);};
    frame(time);check(preview.state().applicationViewport().height==95&&preview.state().maximumApplicationScroll()==6*31-95.,"The owner's app list shows three rows of six");
    check(preview.entries().size()==7&&!preview.entries().back().after.empty(),"The module's header and detail scenes and its retained rows compose with the registration strokes last");
    gpu::LayerScene geometry(raster);gpu::LayerRasterOptions fixtureOptions;geometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},fixtureOptions);gpu::NativeModuleSurface surface(geometry,core::Module::volume);surface.update({},settings,modules.sample(time).presentation.current,1);core::Projection plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*surface.pose().contentWorld,1280,800);
    const auto point=[&](double x,double y){const auto p=plane.project({x,y});check(p.has_value(),"Source plane projects fixture point");return *p;};
    auto p=point(60,280);const auto raster0=raster.stats();
    check(preview.wheel({p.x,p.y,-1,false,0,3},time+=1./60)&&preview.state().applicationScroll()==36,"One wheel detent scrolls the list three source lines (36 points)");frame(time);
    check(raster.stats().textLayoutsCreated-raster0.textLayoutsCreated<=3,"A wheel detent lays out at most the one row entering the retained window");
    const auto settledRaster=raster.stats();const auto settledGPU=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<60;++n){auto center=core::Matrix4{};center.values[3]=n*.000001;preview.update(center,settings,modules.sample(time+=1./60).presentation,1,time);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==settledRaster.rasterizations&&renderer.stats().textureUploads==settledGPU.textureUploads&&!preview.requiresFrames(time),"Frames after a scroll allocate, rasterize and upload nothing and demand no more frames");
    frame(time+=1./60); // back to the untilted plane the fixture projects through
    p=point(60,290);check(preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},time+=1./60)&&preview.pointerLocked()&&preview.state().panning(),"A press on the list outside its sliders captures a list drag");
    p=point(60,270);check(preview.pointer({app::PointerKind::move,app::PointerButton::none,p.x,p.y},time+=1./60)&&std::abs(preview.state().applicationScroll()-56)<1e-6,"The list follows the captured pointer");frame(time);
    const auto wheelWhileDragging=preview.state().applicationScroll();check(preview.wheel({p.x,p.y,-1,false,0,3},time)&&preview.state().applicationScroll()==wheelWhileDragging,"The wheel does not fight a list drag");
    check(preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},time+=1./60)&&!preview.pointerLocked()&&gains==0,"Pointer up ends the drag; scrolling never writes an app gain");
    p=point(60,280);preview.wheel({p.x,p.y,10,false,0,3},time+=1./60);check(preview.state().applicationScroll()==0,"Wheel up clamps at the top of the list");frame(time);
    composition.setEntries(renderer,{});preview.collected(renderer);preview.release(renderer);composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"The owner retires the module scene and every retained row");renderer.reset();
}
// User-reported: switching between Headphones / Bluetooth and App volume
// shook. Reproduced frame by frame through the owner on the real module
// plane: the source motion (VolumeCanvas.animateAction) slides the content 11
// points and back with a slight depth about the content's centre, starting
// at the transaction commit. The port applied it about the canvas's top-left
// corner (the page drifted ~2.4 points up and ~2.8 left and back, a diagonal
// jolt) and started it at the press, so the first frame after the switch's
// whole-module rebuild landed mid-motion.
std::array<double,2>contentPoint(const core::Matrix4&inverseContent,const gpu::DrawObject&d,double x,double y){const auto m=inverseContent*d.world;const auto&v=m.values;const double w=v[3]*x+v[7]*y+v[15];return {(v[0]*x+v[4]*y+v[12])/w,(v[1]*x+v[5]*y+v[13])/w};}
const gpu::DrawObject&draw(const gpu::LayerComposition&c,std::string_view suffix){for(const auto&d:c.draws())if(d.sourceID.ends_with(suffix))return d;throw std::runtime_error("Missing composed Volume surface");}
void pageSwitch(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;
    gpu::VolumeSnapshot data;data.outputs={{"endpoint-1","Speakers"},{"endpoint-2","Headphones",true},{"endpoint-3","Buds",true,true}};data.outputID="endpoint-1";data.volume=.55;data.balance=0;data.muted=false;data.canSetVolume=data.canSetMute=data.canSetBalance=true;data.applicationActivitySupported=true;
    for(unsigned n=0;n<6;++n)data.applications.push_back({std::to_string(300+n)+":1","Synthetic app "+std::to_string(n),300+n,true,gpu::VolumeAppState::direct,{},{},{},{}});
    tools::VolumePreviewOptions options;options.initial=data;options.rasterDensity=2; // the production raster density
    const auto building=std::chrono::steady_clock::now();
    tools::VolumePreview preview(raster,std::move(options));preview.resize({1280,800,96,1,1280,800});core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::volume;core::ModulePresentation modules(core::Module::volume);double time{};
    gpu::LayerComposition composition;const auto frame=[&](double t){preview.update({},settings,modules.sample(t).presentation,1,t);preview.upload(renderer);composition.setEntries(renderer,preview.entries());preview.collected(renderer);composition.present(renderer);};
    frame(time);
    std::cout<<"Volume full build and first upload: "<<raster.stats().rasterizations<<" rasterizations, "<<raster.stats().textLayoutsCreated<<" text layouts, "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-building).count()<<" ms (WARP fixture)\n";
    frame(time+=.5);
    gpu::LayerScene geometry(raster);gpu::LayerRasterOptions fixtureOptions;geometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},fixtureOptions);gpu::NativeModuleSurface surface(geometry,core::Module::volume);surface.update({},settings,modules.sample(time).presentation.current,1);
    const auto contentWorld=surface.pose().contentWorld;const auto inverseContent=core::source::inverseSourceMatrix(contentWorld);core::Projection plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*contentWorld,1280,800);
    const auto point=[&](double x,double y){const auto p=plane.project({x,y});check(p.has_value(),"Source plane projects fixture point");return *p;};
    const auto bytes=[&](const char*state){std::cout<<"Volume resident ("<<state<<"): raster cache "<<raster.stats().resourceBytes<<" B in "<<raster.stats().entries<<" entries, GPU "<<renderer.stats().resourceBytes<<" B in "<<renderer.stats().textures<<" textures\n";};
    bytes("App volume, three rows");
    // The title's local origin is canvas (12,0): canvas centre (200,167) is local (188,167).
    const auto centre=[&]{return contentPoint(inverseContent,draw(composition,"volume/title"),188,167);};
    const double w0=1+10./700;
    const auto press=[&](double x,double y,std::string_view expected){const auto p=point(x,y);const auto raster0=raster.stats();const auto begin=std::chrono::steady_clock::now();
        check(preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},time+=.5)&&preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},time),"A projected detail button takes the press");
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();const auto rasters=raster.stats().rasterizations-raster0.rasterizations,layouts=raster.stats().textLayoutsCreated-raster0.textLayoutsCreated;
        std::cout<<"Volume owner switch to "<<expected<<": "<<rasters<<" rasterizations, "<<layouts<<" text layouts, "<<ms<<" ms on the press (WARP fixture)\n";return std::pair{rasters,layouts};};
    // Every frame: the page centre slides straight back to rest (no vertical
    // motion, no reversal), and header, detail and rows move as one.
    const auto motion=[&](const char*what,bool rows){
        frame(time+=.05); // the first frame comes late after the switch
        const auto first=preview.state().actionSample(time);check(first.active&&first.transform.values[12]==11&&first.transform.values[14]==-10,"The first frame after the switch shows the source start pose");
        auto c=centre();check(std::abs(c[0]-(200+11/w0))<1e-6&&std::abs(c[1]-167)<1e-6,"The page centre starts 11 points to the side at the source depth, not lifted or pulled to a corner");
        double previous=c[0];unsigned frames{};
        do{frame(time+=1./60);++frames;c=centre();
            check(std::abs(c[1]-167)<1e-6&&c[0]<=previous+1e-9&&c[0]>=200-1e-9,what);previous=c[0];
            const auto title=inverseContent*draw(composition,"volume/title").world*core::Matrix4::translation(-12,0);
            const auto detailButton=inverseContent*draw(composition,"volume/action/4/plate").world*core::Matrix4::translation(-200,-203); // its compact raster origin: two points above-left of the 202,205 plate
            for(std::size_t n=0;n<16;++n)check(std::abs(title.values[n]-detailButton.values[n])<1e-9,"Header surfaces share one content motion");
            if(rows){const auto row=inverseContent*draw(composition,"volume/app-row/name").world*core::Matrix4::translation(-18,-235);for(std::size_t n=0;n<16;++n)check(std::abs(title.values[n]-row.values[n])<1e-9,"App rows move rigidly with the page");}
            check(frames<40,"The motion settles");}while(preview.requiresFrames(time));
        std::cout<<what<<": "<<frames<<" frames after the first, rest offset "<<c[0]-200<<'\n';
        check(std::abs(c[0]-200)<1e-9&&std::abs(c[1]-167)<1e-9,"The motion rests exactly in place");check(frames>=10&&frames<=12,"The motion lasts the source 0.18 s at 60 frames per second");
    };
    // App volume -> Headphones / Bluetooth (canvas 12..193 x 205..231).
    const auto toHeadphones=press(100,218,"Headphones / Bluetooth");check(preview.state().headphones(),"Headphones page shown");
    motion("Each frame moves the Headphones page straight back to rest",false);
    check(toHeadphones.first<=13&&toHeadphones.second<=6,"Switching repaints only the two highlights, the device rows and the page controls");
    bytes("Headphones, rows parked");
    // Headphones / Bluetooth -> App volume (canvas 202..388 x 205..231).
    const auto parkedStats=preview.sceneStats();check(parkedStats.rowParks==5&&parkedStats.detailBuilds>=2,"The owner's Headphones page parks its five retained rows");
    const auto toApps=press(295,218,"App volume");check(!preview.state().headphones()&&preview.entries().size()==7,"App volume composes its parked rows again");
    check(preview.sceneStats().rowRevivals==parkedStats.rowRevivals+5&&preview.sceneStats().rowBuilds==parkedStats.rowBuilds&&preview.sceneStats().structureUpdates==parkedStats.structureUpdates,"Returning revives the parked rows without building a row or the header again");
    motion("Each frame moves the App volume page straight back to rest",true);
    check(toApps.first<=5&&toApps.second<=1,"Returning repaints only the two highlights and the list's status line, track and thumb");
    // Settled: no motion, no allocation, no raster, no upload.
    const auto settledRaster=raster.stats();const auto settledGPU=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<30;++n){preview.update({},settings,modules.sample(time+=1./60).presentation,1,time);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==settledRaster.rasterizations&&renderer.stats().textureUploads==settledGPU.textureUploads&&!preview.requiresFrames(time),"Settled frames after a switch allocate, rasterize and upload nothing");
    // Leave Volume from the Headphones page: once the departure is published
    // its GPU textures go with the composition and its parked rows are released.
    press(100,218,"Headphones / Bluetooth");frame(time+=.3);bytes("Headphones before leaving");
    settings.module=core::Module::power;modules.select(core::Module::power,time+=.1);for(unsigned n=0;n<90;++n)frame(time+=1./60);
    check(preview.entries().empty(),"A departed Volume composes nothing");bytes("Volume hidden");
    check(renderer.stats().textures==0,"The composition releases the hidden Volume's textures");
    composition.setEntries(renderer,{});preview.collected(renderer);preview.release(renderer);composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Every Volume resource is retired");renderer.reset();
}
}
// Memory: the RC measured 426-599 MB private bytes. Volume used to rasterize
// its whole artwork at construction (EndfieldHUD launch) and to keep it for the
// process lifetime. Now nothing is built before Volume is first shown (or
// prewarmed as the selected module), and once the HUD closes on another
// module every raster, rasterizer entry, GPU texture and mesh is released;
// showing Volume again paints the same pixels. The module shown at the close
// keeps its artwork, as the Mac keeps its layers.
void residency(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;
    gpu::VolumeSnapshot data;data.outputs={{"endpoint-1","Speakers"},{"endpoint-2","Buds",true,true}};data.outputID="endpoint-1";data.volume=.55;data.balance=0;data.muted=false;data.canSetVolume=data.canSetMute=data.canSetBalance=true;data.applicationActivitySupported=true;
    for(unsigned n=0;n<6;++n)data.applications.push_back({std::to_string(400+n)+":1","Synthetic app "+std::to_string(n),400+n,true,gpu::VolumeAppState::direct,{},{},{},{}});
    unsigned activations{};tools::VolumePreviewOptions options;options.initial=data;options.rasterDensity=2;options.actions.setActive=[&](bool){++activations;}; // the production raster density
    tools::VolumePreview preview(raster,options);preview.resize({1280,800,96,1,1280,800});core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::power;core::ModulePresentation modules(core::Module::power);double time{};
    check(raster.stats().entries==0&&raster.stats().resourceBytes==0&&preview.sceneStats().parts==0&&preview.sceneStats().pixelBytes==0,"Constructing Volume rasterizes nothing");
    gpu::LayerComposition composition;const auto frame=[&](double t){preview.update({},settings,modules.sample(t).presentation,1,t);preview.upload(renderer);composition.setEntries(renderer,preview.entries());preview.collected(renderer);composition.present(renderer);};
    for(unsigned n=0;n<10;++n)frame(time+=1./60);check(raster.stats().entries==0&&activations==0&&preview.entries().empty(),"Frames of another module build no Volume artwork");
    const auto select=[&](core::Module m){settings.module=m;modules.select(m,time+=.1);for(unsigned n=0;n<90;++n)frame(time+=1./60);};
    const auto bytes=[&](const char*state){const auto s=preview.sceneStats();std::cout<<"Volume resident ("<<state<<"): "<<s.parts<<" parts, "<<s.pixelBytes<<" B of rasters; shared raster cache "<<raster.stats().resourceBytes<<" B in "<<raster.stats().entries<<" entries; GPU "<<renderer.stats().resourceBytes<<" B in "<<renderer.stats().textures<<" textures, "<<renderer.stats().meshes<<" meshes\n";return s;};
    const auto pixels=[&]{renderer.draw(false);return renderer.readback().pixels;};
    select(core::Module::volume);check(activations==1&&preview.state().active()&&!preview.requiresFrames(time),"Showing Volume builds and activates it");
    const auto shown=bytes("App volume shown; before this change, held from launch");check(shown.parts==7&&shown.pixelBytes>0&&shown.pixelBytes==raster.stats().resourceBytes,"The shown App volume page holds its header, detail and five retained rows (four meet the viewport, one below), and nothing else in the rasterizer");
    const auto painted=pixels();check(!painted.empty(),"The settled Volume paints");
    // Switching away inside an open HUD keeps the artwork (switching back is free).
    select(core::Module::power);check(preview.entries().empty()&&preview.sceneStats().pixelBytes==shown.pixelBytes&&renderer.stats().textures==0,"Switched away with the HUD open, Volume keeps its rasters (its GPU textures leave with the composition)");
    bytes("switched away, HUD open");
    // The HUD closes on another module: everything is released.
    const auto before=raster.stats();preview.setOverlayVisible(false,time+=.1);preview.conceal();
    const auto parked=bytes("HUD closed on another module");
    check(parked.parts==0&&parked.pixelBytes==0&&parked.parks==1&&raster.stats().entries==0&&raster.stats().resourceBytes==0&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Closing the HUD on another module releases every Volume raster, rasterizer entry, texture and mesh");
    std::cout<<"Volume released on close: "<<before.resourceBytes<<" B of shared raster cache in "<<before.entries<<" entries\n";
    // Reopening on another module builds nothing; showing Volume rebuilds the same artwork.
    preview.setOverlayVisible(true,time+=.1);for(unsigned n=0;n<10;++n)frame(time+=1./60);check(raster.stats().entries==0,"Reopening on another module builds no Volume artwork");
    select(core::Module::volume);const auto rebuilt=bytes("App volume shown again");
    check(rebuilt.parts==shown.parts&&rebuilt.pixelBytes==shown.pixelBytes&&pixels()==painted,"Showing Volume again rebuilds artwork that paints identically");
    // The HUD closes on Volume itself: the shown module keeps its artwork.
    preview.setOverlayVisible(false,time+=.1);preview.conceal();for(unsigned n=0;n<5;++n)frame(time+=1./60);
    check(preview.sceneStats().pixelBytes==shown.pixelBytes&&preview.sceneStats().parks==1,"The module shown at the close keeps its artwork (the Mac keeps its layers)");
    preview.setOverlayVisible(true,time+=.1);
    // Closing while Volume is still leaving: released by the publication without it.
    settings.module=core::Module::power;modules.select(core::Module::power,time+=.1);frame(time+=1./60);check(!preview.entries().empty(),"Volume is still leaving");
    preview.setOverlayVisible(false,time+=.01);check(preview.sceneStats().parks==1&&preview.sceneStats().pixelBytes>0,"A composed Volume is not released mid-departure");
    for(unsigned n=0;n<90&&preview.sceneStats().parks==1;++n)frame(time+=1./60);check(preview.sceneStats().parks==2&&preview.sceneStats().pixelBytes==0&&raster.stats().entries==0,"The first publication without the departed Volume releases it");
    composition.setEntries(renderer,{});preview.collected(renderer);preview.release(renderer);composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Every Volume resource is retired");
    // Launch prewarm: the selected Volume is built while the HUD is closed, with
    // no activation (no audio vote), pose or frame; its first opening reuses it.
    {gpu::LayerRasterizer warmRaster;unsigned warmActivations{};auto warmOptions=options;warmOptions.actions.setActive=[&](bool){++warmActivations;};tools::VolumePreview warm(warmRaster,warmOptions);warm.resize({1280,800,96,1,1280,800});
        core::ModulePresentation warmModules(core::Module::volume);settings.module=core::Module::volume;
        warm.prewarm(core::ModulePresentation(core::Module::power).sample(0).presentation);check(warmRaster.stats().entries==0,"Prewarm builds nothing when another module is selected");
        warm.prewarm(warmModules.sample(0).presentation);const auto warmed=warmRaster.stats();check(warm.sceneStats().pixelBytes>0&&warmActivations==0&&!warm.state().active()&&warm.entries().empty()&&!warm.requiresFrames(0),"Prewarm builds the selected Volume without activating, posing or animating it");
        gpu::LayerComposition warmComposition;warm.update({},settings,warmModules.sample(1).presentation,1,1);warm.upload(renderer);warmComposition.setEntries(renderer,warm.entries());warm.collected(renderer);warmComposition.present(renderer);
        check(warmActivations==1&&warmRaster.stats().rasterizations==warmed.rasterizations&&warmRaster.stats().textLayoutsCreated==warmed.textLayoutsCreated,"The first opening reuses the prewarmed artwork: no raster or text layout");
        warmComposition.setEntries(renderer,{});warm.collected(renderer);warm.release(renderer);warmComposition.detach(renderer);}
    renderer.reset();
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass shader to isolated Volume integration fixture");run(argv[1]);scrolling(argv[1]);pageSwitch(argv[1]);residency(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Volume preview checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
