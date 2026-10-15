#ifdef _WIN32
// What the System provider modules keep resident (Clipboard, Activity
// Monitor, Event Log and Storage), measured through their owners
// (tools/*_preview.hpp) published exactly as the HUD host publishes owners
// (app/application.cpp publishNative: upload, setEntries and collected() only
// when a scene, its revisions or its supplemental draws changed).
//
// The Mac's HUDModuleContent drops a module's layers when another module is
// selected and makes the content again when it is selected. The Windows owners
// keep a switched-away module's artwork while the HUD stays open (a switch
// back only re-uploads), and release it once the HUD closes on another module
// (the owner's HUD-visibility call, the next publication after it, or the
// concealment): no raster bytes, rasterizer entries or GPU textures remain,
// and showing the module again rebuilds artwork that paints identically. The
// module shown at the close keeps its artwork, so reopening on it rasterizes
// nothing. Storage builds its artwork only when it is first shown.
//
// Mode "activityCounters": the Activity GPU engine query (open only while Activity is
// visible) and the background disk query, with this process's thread count
// and private bytes around them (printed; the query state is checked).
//
// Synthetic rows, apps, events and volumes only (no clipboard, process
// metadata, icon, event history or volume of this machine is read), a hidden
// window and WARP.
#include "tools/clipboard_preview.hpp"
#include "tools/activity_preview.hpp"
#include "tools/event_log_preview.hpp"
#include "tools/storage_options.hpp"
#include "native/module_scene.hpp"
#include "native/clipboard_assets.hpp"
#include "native/layer_image_source.hpp"
#include "native/activity_gpu.hpp"
#include "native/activity_disk.hpp"
#include "app/utility_executor.hpp"
#include <objbase.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace tools=endfield::tools;namespace mod=endfield::modules;using Json=ehud::data::Json;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Com{Com(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Create owned COM apartment");}~Com(){CoUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldProviderResourcesFixture";check(RegisterClassW(&c)!=0,"Register owned fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned provider resources fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldProviderResourcesFixture",GetModuleHandleW(nullptr));}};

// The hidden HUD plane of one module, starting on another module (Power), and
// its host-like publication.
template<class Owner>struct Stage {
    gpu::Renderer&renderer;Owner&owner;core::Module module;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::power};
    core::source::DesktopChromeSettings settings;core::Matrix4 center;core::Matrix4 camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    struct Published{gpu::LayerScene*scene{};std::uint64_t content{},resources{};const gpu::DrawObject*after{};std::size_t count{};};
    std::vector<Published>published;std::vector<gpu::LayerCompositionEntry>ordered;std::function<void()>drain;
    Stage(gpu::Renderer&r,Owner&o,core::Module m):renderer(r),owner(o),module(m),settings{{0,0,1280,800},1,{},core::Module::power,true}{center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;}
    ~Stage(){if(released)return;try{composition.detach(renderer);owner.release(renderer);}catch(...){renderer.reset();}}
    void publish(){
        owner.upload(renderer);ordered.clear();for(const auto&e:owner.entries())ordered.push_back(e);
        bool changed=ordered.size()!=published.size();if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});owner.collected(renderer);}
        composition.present(renderer);renderer.setCamera(camera);}
    void frame(double delta=1./60){time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,time);publish();}
    void settle(double seconds=1){for(double t=0;t<seconds;t+=1./60)frame();if(drain){drain();for(double t=0;t<seconds;t+=1./60)frame();}}
    void select(core::Module m,bool animated=false){modules.select(m,time,animated);settle();}
    std::vector<std::uint8_t>capture(){renderer.draw(false);return renderer.readback().pixels;}
    void finish(){composition.detach(renderer);owner.release(renderer);released=true;check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner resources retire through the shared composition");}
};
// One owner's view for the shared lifecycle.
struct Probe {
    std::function<std::size_t()>resident;std::function<std::uint64_t()>parks;
    std::function<void(double)>close,open; // the host's overlayClosing / opening calls
    std::function<void()>conceal;           // HostEvent::overlayConcealed
    std::function<void()>afterRebuild;      // module-specific checks once rebuilt
    bool reopenRasterizes{};                // Storage re-reads its capacity on activation
};
std::string kib(std::size_t bytes){char text[32];std::snprintf(text,sizeof(text),"%.1f KiB",double(bytes)/1024.);return text;}

template<class Owner>void lifecycle(const char*label,Stage<Owner>&stage,gpu::LayerRasterizer&raster,const Probe&p){
    auto&renderer=stage.renderer;
    // Constructed and offered frames while another module is shown: nothing is rasterized.
    stage.settle(.2);const auto baseline=raster.stats();const auto baseGPU=renderer.stats();
    check(p.resident()==0,"An owner that was never shown keeps no artwork");
    // Shown: artwork, rasterizer entries and GPU textures.
    stage.select(stage.module);const auto shown=p.resident();const auto shownRaster=raster.stats();const auto shownGPU=renderer.stats();const auto before=stage.capture();
    check(shown>0&&shownRaster.entries>baseline.entries&&shownGPU.textures>baseGPU.textures,"Showing the module builds and uploads its artwork");
    // A switch inside the open HUD keeps the artwork (a switch back only re-uploads).
    stage.select(core::Module::power);check(p.resident()==shown&&raster.stats().entries==shownRaster.entries&&p.parks()==0,"A switch inside the open HUD keeps the artwork");
    // The HUD closes on another module: everything is released at once.
    p.close(stage.time);const auto closed=raster.stats();const auto closedGPU=renderer.stats();
    check(p.resident()==0&&p.parks()==1,"A close on another module releases the artwork at once");
    check(closed.entries==baseline.entries,"No rasterizer entry of the module remains");
    // Only the rasterizer's shared decoded-asset cache (bundled icons decoded
    // once by SHA-256, bounded at 256 images) may keep what the module decoded.
    const auto kept=closed.resourceBytes-baseline.resourceBytes;
    check(kept==0||(closed.decodedImages>baseline.decodedImages&&kept<=64*1024),"Nothing but the shared decoded assets remains in the rasterizer");
    check(closedGPU.textures==baseGPU.textures&&closedGPU.resourceBytes==baseGPU.resourceBytes,"No GPU texture of the module remains");
    stage.frame();p.conceal();check(p.resident()==0&&p.parks()==1,"The concealment after it has nothing left to release");
    std::cout<<label<<": shown "<<kib(shown)<<" CPU raster in "<<shownRaster.entries-baseline.entries<<" rasterizer entries ("<<kib(shownRaster.resourceBytes-baseline.resourceBytes)<<"), "
        <<shownGPU.textures-baseGPU.textures<<" GPU textures ("<<kib(shownGPU.resourceBytes-baseGPU.resourceBytes)<<"); after a close on another module 0 / 0 / 0"
        <<(kept?" (the shared decoded asset cache keeps "+std::to_string(closed.decodedImages-baseline.decodedImages)+" bundled images, "+kib(kept)+")":std::string{})<<'\n';
    // Reopened on another module: still nothing (no rebuild while it is not shown).
    p.open(stage.time);stage.settle(.3);check(p.resident()==0&&raster.stats().entries==baseline.entries,"A hidden module stays released while the HUD is open on another module");
    // Shown again: rebuilt, and it paints exactly as before.
    stage.select(stage.module);check(p.resident()==shown&&raster.stats().entries==shownRaster.entries&&raster.stats().resourceBytes==shownRaster.resourceBytes&&raster.stats().decodedImages==shownRaster.decodedImages,"Showing it again rebuilds the same artwork (decoded assets reused)");
    check(stage.capture()==before,"The rebuilt artwork paints identically");if(p.afterRebuild)p.afterRebuild();
    // The HUD closes on this module: it keeps its artwork; reopening rasterizes nothing.
    const auto rasterizations=raster.stats().rasterizations;p.close(stage.time);for(unsigned n=0;n<20;++n)stage.frame();p.conceal();
    check(p.resident()==shown&&p.parks()==1,"The module shown at the close keeps its artwork");
    p.open(stage.time);stage.settle(.5);check(p.resident()==shown&&p.parks()==1&&stage.capture()==before,"Reopening on it shows the kept artwork");
    if(!p.reopenRasterizes)check(raster.stats().rasterizations==rasterizations,"Reopening on the kept module rasterizes nothing");
    // A close while it is the outgoing module of a switch: released by the
    // first publication that no longer composes it.
    stage.modules.select(core::Module::power,stage.time,true);stage.frame();stage.frame();p.close(stage.time);check(p.resident()==shown,"The outgoing module is still drawn");
    stage.settle(1.5);check(p.resident()==0&&p.parks()==2&&raster.stats().entries==baseline.entries&&raster.stats().resourceBytes==closed.resourceBytes,"The publication that drops the outgoing module releases it");
    p.open(stage.time);stage.select(stage.module);check(p.resident()==shown&&stage.capture()==before,"Shown again after the outgoing release");
    // Concealment alone (a forced conceal without the closing call).
    stage.select(core::Module::power);p.conceal();check(p.resident()==0&&p.parks()==3&&raster.stats().entries==baseline.entries&&renderer.stats().textures==baseGPU.textures,"The concealment releases a module that is not shown");
    stage.select(stage.module);check(p.resident()==shown&&stage.capture()==before,"Shown again after the concealment");
    stage.finish();
}

void clipboard(gpu::Renderer&renderer,const std::filesystem::path&root){
    gpu::NativeClipboardAssets assets(root);gpu::LayerRasterizer raster;gpu::LayerImageSource thumbnails;gpu::ClipboardSnapshot snapshot;snapshot.capacity=20;
    for(std::uint64_t n=1;n<=8;++n){gpu::ClipboardRow row{n,n==1,n%2?gpu::ClipboardKind::text:gpu::ClipboardKind::url,n%2?"Synthetic clipboard text "+std::to_string(n):"https://fixture.invalid/"+std::to_string(n),{}};
        if(n==3){row.kind=gpu::ClipboardKind::image;row.preview="Image · 8 × 8";row.thumbnail=Json::Object{{"memoryImage","fixture-thumbnail/3"},{"revision",1}};}snapshot.rows.push_back(std::move(row));}
    unsigned prepared{};std::vector<std::uint8_t>pixels(8*8*4);for(std::size_t k=0;k<pixels.size();k+=4){pixels[k]=200;pixels[k+1]=60;pixels[k+2]=30;pixels[k+3]=255;}
    gpu::ClipboardActions actions{[&]{return snapshot;},[](auto){return true;},[](auto){return false;},[](auto){return false;},[]{return false;},
        // ClipboardActions::prepareImageRows (NativeClipboardProvider): publish the visible rows' thumbnails.
        [&](std::span<const std::uint64_t>ids){for(const auto id:ids)if(id==3&&!thumbnails.acquire("fixture-thumbnail/3",1)){(void)thumbnails.publish("fixture-thumbnail/3",1,8,8,pixels);++prepared;}}};
    // The host's Clipboard raster options (app/application.cpp constructClipboard).
    gpu::LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;options.assetRoot=assets.root();options.memoryImages=&thumbnails;
    tools::ClipboardPreview owner(raster,options,std::move(actions),{},{},assets.images());owner.resize({1280,800,96,1,1280,800});
    Stage stage(renderer,owner,core::Module::clipboard);
    Probe probe;probe.resident=[&]{return owner.stats().residentPixelBytes;};probe.parks=[&]{return owner.stats().parks;};
    // ClipboardModule: overlayClosing -> setOverlayVisible(false), opening -> setOverlayVisible(true).
    probe.close=[&](double t){owner.setOverlayVisible(false,t);if(!owner.stats().residentPixelBytes)check(thumbnails.stats().cachedImages==0,"A released Clipboard keeps no cached row thumbnail");};
    probe.open=[&](double t){owner.setOverlayVisible(true,t);};probe.conceal=[&]{owner.conceal();};
    probe.afterRebuild=[&]{check(thumbnails.stats().cachedImages==1&&prepared>=2,"The rebuilt rows publish their thumbnail again");};
    lifecycle("Clipboard (8 rows, 6 visible, one image)",stage,raster,probe);
}

void activity(gpu::Renderer&renderer){
    gpu::LayerRasterizer raster;app::UtilityExecutor executor([]{});tools::ActivityPreviewOptions options;options.compareNames=[](auto a,auto b){return a.compare(b);};
    options.initial.timestamp=1;options.initial.uptime=1;options.initial.cpuPercent=10;options.initial.memory=mod::ActivityMemory{100,1000,10};
    for(unsigned k=0;k<8;++k){mod::ActivityApp item;item.identity={"app"+std::to_string(k),"Synthetic "+std::to_string(k),"fixture-icon-"+std::to_string(k),{100+k}};item.cpuPercent=double(3+k*7%11);item.memoryBytes=100000000ULL+k*7000000ULL;item.gpuPercent=double(k);options.apps.items.push_back(std::move(item));}
    // Synthetic app-row icons on the shared executor (no Shell icon is read).
    unsigned loads{};std::mutex mutex;options.iconExecutor=&executor;
    options.iconLoader=[&](const std::string&key,unsigned size)->std::optional<gpu::ActivityIconPixels>{{std::lock_guard lock(mutex);++loads;}gpu::ActivityIconPixels icon{size,size,std::vector<std::uint8_t>(std::size_t(size)*size*4,0)};
        for(std::size_t k=0;k<icon.straightRGBA.size();k+=4){icon.straightRGBA[k]=std::uint8_t(key.back()*20);icon.straightRGBA[k+1]=90;icon.straightRGBA[k+2]=160;icon.straightRGBA[k+3]=255;}return icon;};
    tools::ActivityPreview owner(raster,std::move(options));owner.resize({1280,800,96,1,1280,800});
    Stage stage(renderer,owner,core::Module::activityMonitor);stage.drain=[&]{executor.waitIdle();executor.drain();};
    // The Apps page with its icons is the larger artwork.
    stage.select(core::Module::activityMonitor);check(owner.perform("activity:apps",stage.time),"Apps tab");stage.settle();stage.select(core::Module::power);
    owner.setOverlayVisible(false,stage.time);owner.setOverlayVisible(true,stage.time);stage.settle(.2);check(owner.stats().residentPixelBytes==0,"Parked before the measured lifecycle");
    const auto parked=owner.stats().parks;const auto loaded=loads;check(loaded>0&&loaded<=gpu::ActivityIconProvider::maximumVisible,"The visible rows' icons loaded once");
    Probe probe;probe.resident=[&]{return owner.stats().residentPixelBytes;};probe.parks=[&]{return owner.stats().parks-parked;};
    // ActivityModule: overlayClosing -> cancelInteraction + setOverlayVisible(false).
    probe.close=[&](double t){owner.cancelInteraction();owner.setOverlayVisible(false,t);};probe.open=[&](double t){owner.setOverlayVisible(true,t);};probe.conceal=[&]{owner.conceal();};
    probe.afterRebuild=[&]{check(owner.state().showingApps()&&owner.iconStats()&&owner.iconStats()->loaded==loaded&&loads==loaded,"The rebuilt Apps rows bind their cached icons without loading them again");};
    lifecycle("Activity Monitor (Apps page, 8 apps with icons)",stage,raster,probe);
}

void eventLog(gpu::Renderer&renderer){
    gpu::LayerRasterizer raster;mod::EventLogSnapshot events;
    for(unsigned k=1;k<=24;++k){char id[64];std::snprintf(id,sizeof(id),"00000000-0000-0000-0000-%012u",k);events.events.push_back({id,k%2?mod::EventKind::clipboardCopied:mod::EventKind::shelfAdded,1700000000.-k*60,k%2?mod::EventMetadata{{"kind","text"}}:mod::EventMetadata{{"filename","Synthetic.pdf"}}});}
    mod::EventLogCallbacks callbacks{[&]{return events;},[]{},[](double){return std::string("11-14 22:13:19");}};
    // The host's Event Log raster options (app/application.cpp).
    tools::EventLogPreview owner(raster,gpu::LayerRasterOptions{},std::move(callbacks));owner.resize({1280,800,96,1,1280,800});
    Stage stage(renderer,owner,core::Module::eventLog);
    Probe probe;probe.resident=[&]{return owner.stats().residentPixelBytes;};probe.parks=[&]{return owner.stats().parks;};
    probe.close=[&](double t){owner.setOverlayVisible(false,t);};probe.open=[&](double t){owner.setOverlayVisible(true,t);};probe.conceal=[&]{owner.conceal();};
    lifecycle("Event Log (24 events, 4 visible)",stage,raster,probe);
}

struct Volumes {std::mutex mutex;std::vector<mod::StorageVolume>list;
    std::vector<mod::StorageVolume>get(){std::lock_guard lock(mutex);return list;}
    std::optional<mod::StorageCapacity>read(const std::string&id){std::lock_guard lock(mutex);for(const auto&v:list)if(v.id==id&&!id.empty())return mod::StorageCapacity{v.title,1000000000000,400000000000,0};return mod::StorageCapacity{"System",1000000000000,250000000000,0};}};
void storage(gpu::Renderer&renderer,const std::filesystem::path&resources){
    gpu::LayerRasterizer raster;app::UtilityExecutor executor([]{});auto volumes=std::make_shared<Volumes>();
    volumes->list={{"vol-system","C:\\","System",false,true},{"vol-data","D:\\","Data",false,false}};
    auto options=tools::makeStoragePreviewOptions(resources,executor,[]{});
    options.readCapacity=[]()->std::optional<mod::StorageCapacity>{throw std::logic_error("The chooser reads by id");};
    options.listVolumes=[volumes]{return volumes->get();};options.readVolumeCapacity=[volumes](const std::string&id){return volumes->read(id);};
    options.scanDetails={};options.completionClock={};std::string remembered="vol-data";options.loadVolume=[&]{return remembered;};options.saveVolume=[&](const std::string&id){remembered=id;};
    tools::StoragePreview owner(raster,std::move(options));owner.resize({1280,800,96,1,1280,800});
    check(owner.resources().pixelBytes==0&&!owner.resources().loaded&&raster.stats().entries==0,"Constructing Storage rasterizes nothing");
    owner.setAppearance({},0);owner.setAppearance(mod::StorageAppearance{false},0);check(raster.stats().entries==0,"An appearance event while Storage is not shown rasterizes nothing");
    Stage stage(renderer,owner,core::Module::storage);stage.drain=[&]{executor.waitIdle();executor.drain();owner.utilityCompleted(stage.time);};
    Probe probe;probe.resident=[&]{return owner.resources().pixelBytes;};probe.parks=[&]{return owner.resources().parks;};
    // StorageModule: overlayClosing -> cancelInteraction + setVisible(false); opening -> setVisible(true).
    probe.close=[&](double t){owner.cancelInteraction(t);owner.setVisible(false,t);};probe.open=[&](double t){owner.setVisible(true,t);};probe.conceal=[&]{owner.conceal();};
    probe.afterRebuild=[&]{check(owner.volumeChooserAvailable()&&owner.entries().size()==2&&owner.presentation().labels().caption=="Data","The rebuilt page shows the chosen volume and its chooser again");};
    probe.reopenRasterizes=true;
    lifecycle("Storage (Mac page and the volume chooser)",stage,raster,probe);
}

std::size_t threads(){const auto pid=GetCurrentProcessId();const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);check(snapshot!=INVALID_HANDLE_VALUE,"Thread snapshot");
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);std::size_t count{};for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==pid)++count;CloseHandle(snapshot);return count;}
std::size_t privateBytes(){PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))!=FALSE,"Process memory counters");return counters.PrivateUsage;}
void counters(){
    const auto t0=threads();const auto p0=privateBytes();
    // Activity visible: the GPU engine query opens on the sampling worker's first read.
    gpu::setWindowsActivityGPUDemand(true);const bool available=gpu::readWindowsActivityGPU().has_value();const auto open=gpu::windowsActivityGPUStats();const auto t1=threads();const auto p1=privateBytes();
    check(!available||open.open,"A visible Activity collects through one open query");
    // Hidden: closed at once (no sample is running), its buffers dropped.
    gpu::setWindowsActivityGPUDemand(false);const auto closed=gpu::windowsActivityGPUStats();check(!closed.open&&closed.instances==0&&(!available||closed.closes==open.closes+1),"Hiding Activity closes the GPU engine query at once");
    check(!gpu::readWindowsActivityGPU()&&!gpu::windowsActivityGPUStats().open,"A hidden Activity never reopens it");const auto t2=threads();const auto p2=privateBytes();
    for(unsigned n=0;n<5;++n){gpu::setWindowsActivityGPUDemand(true);(void)gpu::readWindowsActivityGPU();gpu::setWindowsActivityGPUDemand(false);}
    check(!gpu::windowsActivityGPUStats().open,"Repeated showing and hiding leaves the query closed");const auto t3=threads();const auto p3=privateBytes();
    std::cout<<"GPU engine query ("<<(available?"available":"unavailable")<<"): threads "<<t0<<" before, "<<t1<<" open, "<<t2<<" closed, "<<t3<<" after 5 more cycles; private bytes "
        <<kib(p0)<<", "<<kib(p1)<<", "<<kib(p2)<<", "<<kib(p3)<<'\n';
    // The background disk query (5 s system history while the HUD runs).
    const auto t4=threads();const auto p4=privateBytes();{gpu::WindowsActivityDisk disk;for(unsigned n=0;n<3;++n)(void)disk.sample();const auto t5=threads();const auto p5=privateBytes();
        std::cout<<"Disk query: threads "<<t4<<" before, "<<t5<<" open; private bytes "<<kib(p4)<<", "<<kib(p5);}
    std::cout<<"; "<<threads()<<" threads and "<<kib(privateBytes())<<" once it is destroyed\n";
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==4,"Usage: activity_clipboard_resources_tests shader resources clipboard|activity|eventLog|storage|activityCounters");Com com;
    const std::wstring mode=argv[3];if(mode==L"activityCounters"){counters();std::cout<<"Provider resources: "<<checks<<" checks passed\n";return 0;}
    Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});
    const auto resources=std::filesystem::absolute(argv[2]);
    if(mode==L"clipboard")clipboard(renderer,resources/"clipboard");
    else if(mode==L"activity")activity(renderer);
    else if(mode==L"eventLog")eventLog(renderer);
    else if(mode==L"storage")storage(renderer,resources);
    else check(false,"Unknown mode");
    renderer.reset();std::cout<<"Provider resources: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Provider resources failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
