#ifdef _WIN32
// What the production Temporary File Shelf owner (tools/shelf_preview.hpp)
// keeps resident: the memory audit of the Shelf area. Published exactly as the
// HUD host publishes owners (app/application.cpp publishNative: setEntries and
// collected() only when a scene, its revisions or its supplemental draws
// changed; present() every frame), on WARP, with the Shell icon worker of
// EndfieldHUD.exe, synthetic files in a NEW temporary directory and a hidden
// window only.
//   - The Shell icon worker does not exist at launch: it starts with the first
//     shown Shelf and its thread ends while the Shelf is switched away or the
//     HUD closes with every visible icon extracted; the 24-icon cache stays,
//     and only a missing icon starts a worker again.
//   - A module switch inside the open HUD keeps the Shelf's rasters (the
//     composition drops only its GPU textures; switching back re-uploads
//     them, no rebuild stall). Once the HUD closes while another module is
//     shown, the Shelf keeps no artwork: its rasters (the CPU copies in the
//     shared LayerRasterizer) are released too, as Mac HUDModuleContent makes
//     the content again when the module is selected, and showing it again
//     rebuilds pixel-identical artwork. The selected Shelf keeps its artwork
//     across a close.
//   - A hover/press tint or rim that does not show keeps one transparent
//     point instead of its bitmap: a hover paints it, and it is given back
//     once its fade-out finished (the resting frame is pixel-identical).
// Prints the measured numbers (raster bytes, textures, threads, private
// bytes, rebuild time) before and after.
#include "tools/shelf_preview.hpp"
#include "native/module_scene.hpp"
#include "core/data/file_io.hpp"
#include <ole2.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace data=ehud::data;namespace tools=endfield::tools;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
std::string utf8(const std::filesystem::path&p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
struct Ole{Ole(){check(SUCCEEDED(OleInitialize(nullptr)),"Create owned OLE apartment");}~Ole(){OleUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldShelfResourcesFixture";check(RegisterClassW(&c)!=0,"Register owned fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Shelf resources fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldShelfResourcesFixture",GetModuleHandleW(nullptr));}};
struct Temp{std::filesystem::path root=std::filesystem::temp_directory_path()/("ehud-shelf-resources-"+data::makeUUID());Temp(){check(std::filesystem::create_directory(root),"Create unique owned fixture parent");}~Temp(){std::error_code error;std::filesystem::remove_all(root,error);}};
unsigned threads(){const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);check(snapshot!=INVALID_HANDLE_VALUE,"Read own threads");THREADENTRY32 entry{};entry.dwSize=sizeof(entry);unsigned count{};
    for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==GetCurrentProcessId())++count;CloseHandle(snapshot);return count;}
std::size_t privateBytes(){PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))!=FALSE,"Read own private bytes");return counters.PrivateUsage;}
double kib(std::size_t bytes){return double(bytes)/1024.;}
struct Box{unsigned x{},y{},width{},height{};};
std::vector<std::uint8_t>crop(const gpu::Readback&r,Box b){std::vector<std::uint8_t>out(std::size_t(b.width)*b.height*4);
    for(unsigned y=0;y<b.height;++y)std::copy_n(r.pixels.data()+std::size_t(b.y+y)*r.rowBytes+std::size_t(b.x)*4,std::size_t(b.width)*4,out.data()+std::size_t(y)*b.width*4);return out;}

void run(HWND hwnd,gpu::Renderer&renderer,const gpu::NativeShelfAssets&assets){
    Temp temp;gpu::LayerRasterizer raster;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::clipboard};
    const auto threadsAtLaunch=threads();const auto privateAtLaunch=privateBytes();
    // Production services: Shell icons and the drag images, as in EndfieldHUD.exe.
    tools::ShelfPreview shelf(hwnd,raster,assets,{temp.root/"metadata",true,false,{}});shelf.resize({1280,800,96,1,1280,800});
    const auto launched=shelf.resources();
    check(!launched.iconProvider&&launched.iconWorkers==0&&launched.sceneParts==0&&launched.scenePixelBytes==0,"At launch the Shelf has no icon worker and no artwork");
    std::cout<<"launch: threads "<<threadsAtLaunch<<" -> "<<threads()<<" after constructing the Shelf (icon worker deferred)\n";
    gpu::LayerScene reference(raster);reference.load(data::Json::Object{{"bounds",data::Json::Array{0,0,400,334}},{"position",data::Json::Array{0,0}},{"anchorPoint",data::Json::Array{0,0}},{"children",data::Json::Array{}}},{});
    gpu::NativeModuleSurface placement(reference,core::Module::fileShelf);
    core::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},core::Module::clipboard,true};
    const auto center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    struct Cleanup{gpu::Renderer&r;gpu::LayerComposition&c;tools::ShelfPreview&s;bool&done;~Cleanup(){if(done)return;try{c.detach(r);s.release(r);}catch(...){r.reset();}}}cleanup{renderer,composition,shelf,released};
    struct Published{gpu::LayerScene*scene{};std::uint64_t content{},resources{};const gpu::DrawObject*after{};std::size_t count{};};
    std::vector<Published>published;std::vector<gpu::LayerCompositionEntry>ordered;bool collect=true;
    // app/application.cpp Impl::publishNative, for this one owner.
    const auto publish=[&]{
        shelf.upload(renderer);ordered.clear();for(const auto&e:shelf.entries())ordered.push_back(e);
        bool changed=ordered.size()!=published.size();if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});if(collect)shelf.collected(renderer);}
        composition.present(renderer);renderer.setCamera(camera);};
    const auto frame=[&](double delta){time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;shelf.update(center,settings,sample,1,time);publish();};
    const auto pump=[&]{MSG m{};while(PeekMessageW(&m,hwnd,WM_APP,WM_APP+0x3FFF,PM_REMOVE))(void)shelf.message({hwnd,m.message,m.wParam,m.lParam},time);};
    constexpr double tick=1./60;
    // Another module first: the Shelf receives frames but is never shown.
    for(unsigned n=0;n<10;++n)frame(tick);
    check(!shelf.resources().iconProvider&&shelf.resources().sceneParts==0,"A Shelf that was never shown starts no worker and builds no artwork");
    const auto rasterBefore=raster.stats();const auto renderBefore=renderer.stats();const auto privateBefore=privateBytes();
    std::vector<std::string>paths;
    for(const auto*name:{"Alpha notes.txt","Beta photo.png","Gamma report.pdf","Delta draft.docx","Epsilon archive.zip","Zeta clip.mp4","Eta sheet.csv"}){
        const auto file=temp.root/name;std::ofstream out(file,std::ios::binary);out<<"Synthetic Shelf resources fixture "<<name;out.close();paths.push_back(utf8(file));}
    {const auto folder=temp.root/"Theta folder";std::filesystem::create_directory(folder);paths.push_back(utf8(folder));}
    check(shelf.importFiles(paths,time),"Synthetic references are added");
    // Show the Shelf: the icon worker starts and delivers every visible icon.
    (void)modules.select(core::Module::fileShelf,time);
    for(unsigned n=0;n<30;++n){pump();frame(tick);}
    // The import revealed its last card; the first row shows every card.
    check(shelf.key({app::KeyKind::down,VK_HOME},time),"Home scrolls the collection");
    // (The cache also holds the Shelf's 1 px pending-icon placeholder.)
    const auto icons=[&]{return shelf.resources().cachedIcons-launched.cachedIcons;};
    for(unsigned n=0;n<300&&icons()<paths.size();++n){pump();frame(tick);Sleep(10);}
    for(unsigned n=0;n<40;++n){pump();frame(tick);}
    auto shown=shelf.resources();
    check(shown.iconProvider&&shown.iconWorkers==1&&shown.iconWorkerStarts==1&&icons()==8&&shown.cachedIconBytes-launched.cachedIconBytes==8u*64u*64u*4u,"The first shown Shelf starts one icon worker; eight 64 px icons are cached");
    check(shown.sceneParts>0&&shown.sceneSurfaces>0&&shown.scenePixelBytes>0&&!shelf.requiresFrames(time),"The shown Shelf holds its settled artwork");
    const auto rasterShown=raster.stats();const auto renderShown=renderer.stats();const auto threadsShown=threads();const auto privateShown=privateBytes();
    // Per composed part (collection, cards, scrollbar, foreground; the
    // module geometry last): retained surfaces and raster bytes.
    std::cout<<"parts:";for(const auto&e:shelf.entries())std::cout<<' '<<e.scene->report().surfaces<<'/'<<kib(e.scene->report().pixelBytes)<<"KiB";std::cout<<'\n';
    std::cout<<"Shelf shown:    scene "<<shown.sceneParts<<" parts, "<<shown.sceneSurfaces<<" surfaces, "<<kib(shown.scenePixelBytes)<<" KiB rasters; shared rasterizer +"<<kib(rasterShown.resourceBytes-rasterBefore.resourceBytes)
             <<" KiB in "<<(rasterShown.entries-rasterBefore.entries)<<" entries; renderer +"<<(renderShown.textures-renderBefore.textures)<<" textures, +"<<kib(renderShown.resourceBytes-renderBefore.resourceBytes)
             <<" KiB; icon cache "<<shown.cachedIcons-launched.cachedIcons<<" icons, "<<kib(shown.cachedIconBytes-launched.cachedIconBytes)<<" KiB; threads "<<threadsShown<<"; private "<<kib(privateShown)<<" KiB\n";
    check(rasterShown.resourceBytes-rasterBefore.resourceBytes>=shown.scenePixelBytes,"The Shelf's artwork lives in the shared rasterizer");
    // The projected Shelf canvas.
    const auto canvas=[&](core::Point p){auto sample=modules.sample(time).presentation;placement.update(center,settings,sample.current);return *core::Projection::viewport(camera*placement.pose().contentWorld,1280,800).project(p);};
    double left=1e9,top=1e9,right=-1e9,bottom=-1e9;for(const auto p:{core::Point{0,0},core::Point{400,0},core::Point{0,334},core::Point{400,334}}){const auto q=canvas(p);left=std::min(left,q.x);right=std::max(right,q.x);top=std::min(top,q.y);bottom=std::max(bottom,q.y);}
    const Box box{unsigned(std::max(0.,std::floor(left)-4)),unsigned(std::max(0.,std::floor(top)-4)),0,0};const Box crops{box.x,box.y,unsigned(std::min(1280.,std::ceil(right)+4))-box.x,unsigned(std::min(800.,std::ceil(bottom)+4))-box.y};
    const auto capture=[&]{renderer.draw(false);return crop(renderer.readback(),crops);};
    const auto before=capture();bool ink{};for(std::size_t n=3;n<before.size();n+=4)ink|=before[n]!=0;check(ink,"The Shelf is drawn");
    using Clock=std::chrono::steady_clock;const auto steadyBegin=Clock::now();frame(tick);const auto steady=std::chrono::duration<double,std::milli>(Clock::now()-steadyBegin).count();
    const auto timed=[&](unsigned frames){double first{};for(unsigned n=0;n<frames;++n){pump();const auto begin=Clock::now();frame(tick);if(!n)first=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}return first;};
    // Another module inside the open HUD: the composition drops the Shelf's
    // GPU textures, its rasters stay, and its idle icon worker ends.
    (void)modules.select(core::Module::clipboard,time);timed(45);
    const auto switched=shelf.resources();const auto renderSwitched=renderer.stats();
    check(switched.sceneParts==shown.sceneParts&&switched.scenePixelBytes==shown.scenePixelBytes&&switched.sceneParks==0&&raster.stats().entries==rasterShown.entries,"A module switch inside the open HUD keeps the Shelf's rasters");
    check(renderSwitched.textures==renderBefore.textures&&renderSwitched.resourceBytes<=renderBefore.resourceBytes+64*1024,"The composition released the hidden Shelf's GPU textures");
    for(unsigned n=0;n<500&&shelf.resources().iconWorkers;++n)Sleep(2); // bounded: the parked worker ends its thread
    const auto afterSwitch=shelf.resources();
    check(afterSwitch.iconWorkers==0&&afterSwitch.iconParks==1&&afterSwitch.iconWorkerStarts==1&&icons()==8,"The icon worker's thread ends while the Shelf is hidden; the icons stay cached");
    // Switching back only re-uploads the retained rasters.
    (void)modules.select(core::Module::fileShelf,time);const double upload=timed(45);timed(20);
    check(capture()==before&&raster.stats().entries==rasterShown.entries&&raster.stats().resourceBytes==rasterShown.resourceBytes&&shelf.resources().iconWorkerStarts==1,"Switched back, the Shelf is unchanged and starts no icon worker for cached icons");
    // A hover/press tint or rim that does not show keeps one transparent
    // point instead of its bitmap: moving onto a card paints its highlight
    // (the framed card rim, .28 at rest, is always painted); once it faded
    // out it draws nothing and gives the bitmap back at the next pointer
    // event (no raster work inside the frames), or at once when the HUD
    // finishes closing over the shown Shelf (conceal settles the fade).
    {const auto firstID=shelf.state().items().front().id;const auto placed=shelf.state().card(firstID);check(placed.has_value(),"The first card is shown");
        const auto at=canvas({placed->full.x+placed->full.width*.3,placed->full.y+placed->full.height*.5});const auto rest=shelf.resources();
        const auto hover=[&]{check(shelf.pointer({app::PointerKind::move,app::PointerButton::none,at.x,at.y,0},time),"The pointer moves over a card");timed(20);};
        hover();const auto hovered=shelf.resources();const auto painted=hovered.highlightPaints-rest.highlightPaints;const bool shows=capture()!=before;
        std::cout<<"Shelf hover:    "<<painted<<" highlight(s) painted while shown, +"<<kib(hovered.scenePixelBytes-rest.scenePixelBytes)<<" KiB\n";
        check(painted>0&&hovered.highlightReleases==rest.highlightReleases&&hovered.scenePixelBytes>rest.scenePixelBytes&&shows,"Hovering a card paints its highlight");
        (void)shelf.pointer({app::PointerKind::leave,app::PointerButton::none,0,0,0},time);timed(20);
        const auto faded=shelf.resources();
        check(faded.highlightReleases==rest.highlightReleases&&faded.scenePixelBytes==hovered.scenePixelBytes&&!shelf.requiresFrames(time)&&capture()==before,"A faded highlight draws nothing and does no raster work inside the frames");
        (void)shelf.pointer({app::PointerKind::move,app::PointerButton::none,2,2,0},time);timed(2);
        const auto returned=shelf.resources();
        check(returned.highlightReleases-rest.highlightReleases==painted&&returned.scenePixelBytes==rest.scenePixelBytes&&returned.sceneSurfaces==rest.sceneSurfaces&&capture()==before,
              "The next pointer event gives every faded highlight bitmap back; the frame is the resting one");
        hover();check(shelf.resources().highlightPaints-rest.highlightPaints==2*painted,"Hovering again paints the highlight again");
        (void)shelf.pointer({app::PointerKind::leave,app::PointerButton::none,0,0,0},time);timed(1);check(shelf.requiresFrames(time),"The highlight fades");
        shelf.cancelPanels();shelf.conceal();const auto closed=shelf.resources();
        check(closed.highlightReleases-rest.highlightReleases==2*painted&&closed.scenePixelBytes==rest.scenePixelBytes&&closed.sceneParks==rest.sceneParks&&!shelf.requiresFrames(time),
              "Closed mid-fade, the shown Shelf settles its highlights and keeps only its resting artwork");
        timed(5);check(capture()==before,"Reopened, the settled Shelf draws its resting frame");}
    // The HUD closes while another module is shown (overlayClosing, then the
    // close finishes: overlayConcealed): the Shelf releases its rasters.
    (void)modules.select(core::Module::clipboard,time);timed(45);shelf.cancelPanels();shelf.conceal();
    const auto parked=shelf.resources();const auto rasterParked=raster.stats();const auto renderParked=renderer.stats();
    check(parked.sceneParts==0&&parked.sceneSurfaces==0&&parked.scenePixelBytes==0&&parked.sceneParks==1,"Closed while another module is shown, the Shelf keeps no artwork");
    // Every retained raster entry goes; only the shared rasterizer's decoded
    // Depot artwork (its bounded intrinsic image cache) remains.
    check(rasterParked.entries==rasterBefore.entries&&rasterParked.resourceBytes>=rasterBefore.resourceBytes&&rasterParked.resourceBytes-rasterBefore.resourceBytes<=256u*1024u,"Its rasters leave the shared rasterizer");
    check(renderParked.textures==renderBefore.textures,"No Shelf texture remains");
    const auto threadsParked=threads();const auto privateParked=privateBytes();
    std::cout<<"Shelf hidden:   scene "<<parked.sceneParts<<" parts, "<<kib(parked.scenePixelBytes)<<" KiB rasters; shared rasterizer "<<kib(rasterParked.resourceBytes)<<" KiB ("<<rasterParked.entries<<" entries, "<<rasterParked.decodedImages<<" decoded images; before showing "<<kib(rasterBefore.resourceBytes)<<" KiB, "<<rasterBefore.entries<<" entries, "<<rasterBefore.decodedImages<<" decoded images"
             <<"); renderer "<<renderParked.textures<<" textures ("<<renderBefore.textures<<" before); icon workers "<<afterSwitch.iconWorkers<<"; threads "<<threadsParked<<" (shown "<<threadsShown<<"); private "<<kib(privateParked)<<" KiB (shown "<<kib(privateShown)<<" KiB, before "<<kib(privateBefore)<<" KiB, launch "<<kib(privateAtLaunch)<<" KiB)\n";
    // Shown again after the close: rebuilt pixel-identical from cached icons
    // (no worker); the shared rasterizer holds exactly what it held.
    (void)modules.select(core::Module::fileShelf,time);const double rebuild=timed(45);timed(20);
    std::cout<<"Shelf reshown:  first switch-in frame "<<rebuild<<" ms after a close released the artwork (rebuild and upload), "<<upload<<" ms after a switch inside the open HUD (upload only); steady frame "<<steady<<" ms\n";
    const auto again=shelf.resources();const auto rasterAgain=raster.stats();
    check(again.sceneParts==shown.sceneParts&&again.sceneSurfaces==shown.sceneSurfaces&&again.scenePixelBytes==shown.scenePixelBytes,"Showing the Shelf again rebuilds the same artwork");
    check(rasterAgain.entries==rasterShown.entries&&rasterAgain.resourceBytes==rasterShown.resourceBytes,"The shared rasterizer holds exactly what it held before");
    check(again.iconWorkerStarts==1&&again.iconWorkers==0,"Cached icons start no worker");
    check(capture()==before,"The rebuilt Shelf is pixel-identical");
    // Again, through the publication path: the close begins (cancelPanels)
    // while another module is shown, and the host's next publication (another
    // owner's entries changed; this harness composes the Shelf only) calls
    // collected(), which releases it.
    (void)modules.select(core::Module::clipboard,time);timed(45);shelf.cancelPanels();
    check(shelf.resources().sceneParks==1,"Nothing is released before the next publication");shelf.collected(renderer);
    check(shelf.resources().sceneParks==2&&shelf.resources().scenePixelBytes==0&&raster.stats().entries==rasterParked.entries&&raster.stats().resourceBytes==rasterParked.resourceBytes,"Releasing repeats exactly");
    (void)modules.select(core::Module::fileShelf,time);timed(65);
    check(shelf.resources().scenePixelBytes==shown.scenePixelBytes&&capture()==before,"Shown again, the Shelf is rebuilt pixel-identical");
    // The selected Shelf keeps its artwork across a close.
    shelf.cancelPanels();frame(tick);shelf.conceal();
    check(shelf.resources().sceneParks==2&&shelf.resources().scenePixelBytes==shown.scenePixelBytes,"The selected Shelf keeps its artwork across a close");
    // The HUD closes (overlayClosing -> cancelPanels) with every icon cached:
    // the worker stays parked; a newly added item's missing icon starts it.
    shelf.cancelPanels();check(shelf.resources().iconWorkers==0,"Closing the HUD keeps the icon worker parked");
    {const auto file=temp.root/"Iota later.ini";std::ofstream out(file,std::ios::binary);out<<"Synthetic later item";out.close();check(shelf.importFiles(std::vector<std::string>{utf8(file)},time),"A later item is added");}
    for(unsigned n=0;n<200&&icons()<9;++n){pump();frame(tick);Sleep(10);}
    const auto later=shelf.resources();check(icons()==9&&later.iconWorkerStarts==2,"A missing icon starts the worker again on the same provider");
    composition.detach(renderer);shelf.release(renderer);released=true;check(!IsWindowVisible(hwnd),"Fixture never displayed a window");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==3,"Usage: shelf_resources_tests shader assets");Ole ole;Window window;gpu::Renderer renderer;
    renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});gpu::NativeShelfAssets assets(std::filesystem::absolute(argv[2]));
    run(window.hwnd,renderer,assets);std::cout<<"Shelf resources: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Shelf resources failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
