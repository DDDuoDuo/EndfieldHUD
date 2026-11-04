// Opt-in Personal Profile cold-start trace (diagnostics, not a contract test).
// Wall milliseconds of what the profile area runs on the owner thread at
// launch, when its record loads, when a returning user's large photo avatar
// loads, on the first frames with another module and with Personal Profile
// shown, and in the host's ID card presenter apply, and the Work Mode page's
// (design owner) frames and residency; then the process memory
// (private bytes, threads) a returning user's photo avatar and photo
// background keep resident, and the owner-thread cost of a portrait slider
// step (Personal ID on the design library: the canvas in its owner core). Hidden HWND, WARP, a NEW temporary data root and a synthetic gradient
// photo only; prints one JSON object of timings and counts, never profile
// content.
//   profile_cold_start_probe <hud.hlsl> <resources/profile> <id_card-native-captions.json> [<shell catalog.ehscene>]
// With the packaged shell catalog it also reports what the source shell's
// SourceScene keeps resident after loading it (CPU bytes and private bytes).
// The design headers first: rpcndr.h (through objbase) defines `small`.
#include "tools/hud_design_owner.hpp"
#include "modules/work_mode_canvas.hpp"
#include "native/hud_design_text_metrics.hpp"
#include "tools/profile_owner.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "modules/id_card_artwork.hpp"
#include "native/id_card_presenter.hpp"
#include "native/profile_text.hpp"
#include "native/source_scene.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;namespace fs=std::filesystem;using J=ehud::data::Json;using Clock=std::chrono::steady_clock;using Microsoft::WRL::ComPtr;
void need(bool v,const char*why){if(!v)throw std::runtime_error(why);}
double since(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
template<class F>double timed(F&&f){const auto start=Clock::now();f();return since(start);}
// Process private bytes (commit charge) and thread count: what the live RC's
// memory report measures.
struct ProcessMemory{double privateMB{};std::int64_t threads{};};
ProcessMemory processMemory(){
    ProcessMemory out;PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters)))out.privateMB=double(counters.PrivateUsage)/(1024*1024);
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot!=INVALID_HANDLE_VALUE){THREADENTRY32 entry{};entry.dwSize=sizeof(entry);const auto self=GetCurrentProcessId();
        for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==self)++out.threads;CloseHandle(snapshot);}
    return out;
}
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Hidden Personal Profile cold-start probe",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);need(hwnd&&!IsWindowVisible(hwnd),"The probe window stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
struct Temporary{fs::path root=fs::canonical(fs::temp_directory_path())/("EndfieldHUD-Profile-Probe-"+ehud::data::makeUUID());Temporary(){fs::create_directories(root);}~Temporary(){std::error_code e;fs::remove_all(root,e);}};
// A synthetic gradient photo written only into the temporary root.
std::vector<std::uint8_t>photo(const fs::path&path,UINT width,UINT height){
    ComPtr<IWICImagingFactory>f;need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))),"WIC factory");
    ComPtr<IWICStream>stream;need(SUCCEEDED(f->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Photo stream");
    ComPtr<IWICBitmapEncoder>encoder;need(SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Photo encoder");
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>bag;need(SUCCEEDED(encoder->CreateNewFrame(&frame,&bag))&&SUCCEEDED(frame->Initialize(bag.Get()))&&SUCCEEDED(frame->SetSize(width,height)),"Photo frame");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;need(SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat24bppBGR,"Photo format");
    const UINT stride=(width*3+3)&~3u;std::vector<BYTE>pixels(std::size_t(stride)*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto*p=&pixels[std::size_t(y)*stride+x*3];p[0]=BYTE(x/16);p[1]=BYTE(y/12);p[2]=BYTE((x+y)/28);}
    need(SUCCEEDED(frame->WritePixels(height,stride,UINT(pixels.size()),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Photo commit");
    stream.Reset();encoder.Reset();frame.Reset();
    const auto bytes=ehud::data::detail::readFile(path,128*1024*1024);need(bytes.has_value(),"Photo reads back");
    return {bytes->begin(),bytes->end()};
}
void run(const fs::path&shader,const fs::path&resources,const fs::path&captionFixture,const fs::path&catalog){
    Window w;Temporary t;J report=J::Object{};
    const auto set=[&](const char*key,double value){report[key]=value;};
    report["memory.startPrivateMB"]=processMemory().privateMB;report["memory.startThreads"]=processMemory().threads;
    if(!catalog.empty()){
        // The host's shell scene (application.cpp: SourceScene over the packaged catalog).
        const auto before=processMemory();std::unique_ptr<gpu::SourceScene>scene;
        set("source.loadMS",timed([&]{scene=std::make_unique<gpu::SourceScene>(gpu::CompiledSourceScene{catalog,{}});}));
        const auto stats=scene->stats();report["source.retainedCPUMB"]=double(stats.retainedCPUBytes)/(1024*1024);
        report["source.textureCPUMB"]=double(stats.textureCPUBytes)/(1024*1024);report["source.meshCPUMB"]=double(stats.meshCPUBytes)/(1024*1024);
        report["source.geometryCPUMB"]=double(stats.geometryCPUBytes)/(1024*1024);report["source.programCPUMB"]=double(stats.programCPUBytes)/(1024*1024);
        report["source.uniformCPUMB"]=double(stats.uniformCPUBytes)/(1024*1024);
        report["source.textures"]=std::int64_t(stats.textures);report["source.meshes"]=std::int64_t(stats.meshes);report["source.pipelines"]=std::int64_t(stats.pipelines);
        report["source.privateMB"]=processMemory().privateMB-before.privateMB;
        scene.reset();report["source.releasedPrivateMB"]=processMemory().privateMB-before.privateMB;
    }
    // 1. Launch, cold in this process: what ProfileOwner's constructor runs on
    // the owner thread (ICU text and date rules, packaged artwork digests,
    // the pinned card source, the card artwork).
    set("launch.textRulesMS",timed([&]{(void)gpu::nativeProfileTextRules();}));
    set("launch.dateRulesMS",timed([&]{(void)gpu::nativeProfileDateRules(u"");}));
    m::ProfileSourceArtwork artwork;set("launch.sourceArtworkMS",timed([&]{artwork=m::loadProfileSourceArtwork(resources);}));
    set("launch.idCardSourceMS",timed([&]{(void)m::loadIdCardSource(resources);}));
    set("launch.idCardArtworkMS",timed([&]{(void)std::make_shared<const m::IdCardArtwork>(artwork.card);}));
    gpu::Renderer renderer;renderer.initialize(w.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));
    gpu::LayerRasterizer raster;app::UtilityExecutor executor([]{},32);m::WorkModeController work;
    gpu::NativeDesignTextMeasurer measurer(raster);m::design::Context context(measurer,gpu::nativeUppercase());context.env().characters=gpu::nativeCharacterRules();
    double time{};
    tools::ProfileOwnerOptions o;o.appRoot=t.root;o.resources=resources;o.design.name="profile";o.design.raster.pixelsPerPoint=2;o.design.now=[&]{return time;};
    const app::ClientMetrics metrics{1280,800,96,1,1280,800};
    const app::ModuleAppearance appearance{core::Language::english,true,{250./255,212./255,31./255,1},{250./255,212./255,31./255},false};
    const auto open=[&](tools::ProfileOwner&owner,double at){owner.core().resize(metrics);owner.core().setAppearance(appearance,at);owner.core().setOverlayVisible(true,at);owner.setOverlayVisible(true,at);};
    double ownerThread{},longest{};
    const auto settle=[&](auto done){for(int n=0;n<256&&!done();++n){executor.waitIdle();const double d=timed([&]{executor.drain();});ownerThread+=d;longest=std::max(longest,d);}need(done(),"The probe's worker results arrive");};
    const auto measure=[&](const char*total,const char*peak,auto done){ownerThread=longest=0;settle(done);set(total,ownerThread);set(peak,longest);};
    core::source::DesktopChromeSettings chrome;chrome.viewport={0,0,1280,800};gpu::LayerComposition composition;
    const auto designScreen=[](core::ModulePresentation&modules){modules.setDesignScreen(core::Module::profile,true);return &modules;};
    // One presented frame of the owner with the shared module presentation.
    const auto frame=[&](tools::ProfileOwner&owner,core::ModulePresentation&modules,double advance){
        return timed([&]{time+=advance;(void)context.advance(time);const auto sample=modules.sample(time).presentation;chrome.module=sample.requested;
            owner.core().update(app::ModuleFrame{core::Matrix4{},chrome,sample,1,time,true});
            owner.upload(renderer);composition.setEntries(renderer,owner.core().entries(core::Module::profile));composition.present(renderer);owner.collected(renderer);});
    };
    // Frames after the HUD opens on another module, then Personal Profile opened.
    const auto frames=[&](tools::ProfileOwner&owner,const std::string&prefix){
        core::ModulePresentation modules(core::Module::notes);designScreen(modules);
        set((prefix+".firstFrameOtherModuleMS").c_str(),frame(owner,modules,1./60));set((prefix+".warmFrameOtherModuleMS").c_str(),frame(owner,modules,1./60));
        modules.select(core::Module::profile,time);double first{},peak{};
        for(unsigned n=0;n<36;++n){const double d=frame(owner,modules,1./60);if(!n)first=d;peak=std::max(peak,d);}
        need(owner.canvas()->isActive(),"Personal ID is shown");
        set((prefix+".firstProfileFrameMS").c_str(),first);set((prefix+".profileOpeningPeakFrameMS").c_str(),peak);
        composition.detach(renderer);
    };
    // 2. A fresh record: owner construction (warm ICU), worker load, card textures, first frames.
    std::unique_ptr<tools::ProfileOwner>owner;
    set("fresh.ownerMS",timed([&]{owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,context,work,o);open(*owner,0);}));
    owner->start(0);measure("fresh.loadOwnerThreadMS","fresh.loadLongestDrainMS",[&]{return owner->loaded();});
    measure("fresh.cardOwnerThreadMS","fresh.cardLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::background)&&owner->cardTexture(tools::IdCardTextureKind::hover);});
    frames(*owner,"fresh");
    // 3. The host's bottom-left card presenter over the exported caption
    // leaves: the first apply after the feed published (captions repaint,
    // texture replacement copies like SourceScene::replaceTexture).
    {
        const auto bytes=ehud::data::detail::readFile(captionFixture,8*1024*1024);need(bytes.has_value(),"Caption fixture");
        const auto exported=J::parse(*bytes,8*1024*1024);gpu::LayerScene labels(raster);gpu::LayerRasterOptions options;
        set("presenter.labelsLoadMS",timed([&]{labels.load(exported["nativeLayers"],options);}));
        std::vector<std::vector<std::uint8_t>>copies;
        gpu::IdCardTextureTarget target{[&](std::string_view,unsigned,unsigned,std::span<const std::uint8_t>rgba,std::uint64_t){copies.emplace_back(rgba.begin(),rgba.end());return true;},[&](std::string_view){return true;}};
        const auto*card=owner->card();need(card,"The loaded card");
        core::source::SourceDesktopFrameSettings baseline;
        baseline.images[card->source().playerHead]={"desktop.profile.avatar",{272,272},{}};baseline.images[card->source().background]={"desktop.profile.background",{530,204},{}};
        baseline.images["profile-hover-plate"]={"desktop.profile.hover",{530,204},{}};
        gpu::IdCardPresenter presenter(labels,gpu::idCardCaptionLeaves(exported["nativeLayers"]),options,target,baseline);gpu::IdCardFeed feed;presenter.connect(feed,{});
        std::array<std::optional<gpu::IdCardTextureView>,3>views;
        for(std::size_t k=0;k<3;++k)if(const auto*v=owner->cardTexture(static_cast<tools::IdCardTextureKind>(k)))views[k]=gpu::IdCardTextureView{v->pixels.width,v->pixels.height,v->pixels.rgba,v->revision};
        feed.publish(card,views);set("presenter.firstApplyMS",timed([&]{(void)presenter.apply();}));
        report["presenter.textureCopies"]=std::int64_t(copies.size());presenter.disconnect();
    }
    // 4. A returning user with a large photo avatar: stored once, then a new
    // owner over the same record (the next launch).
    const auto png=photo(t.root/"photo.png",4000,3000);const auto stored=owner->importGameAvatar(png,time);
    measure("photo.firstOwnerThreadMS","photo.firstLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::avatar)!=nullptr;});
    need(owner->core().flush(time),"The probe record flushes");settle([&]{const auto*s=owner->serviceStatus();return s&&!s->busy&&!s->dirty;});
    owner->release(renderer);owner.reset();
    {
        const auto decoded=gpu::decodeProfileImage(t.root/"Profile"/"Images"/stored,m::ProfileImageKind::avatar);
        report["photo.decodedWidth"]=std::int64_t(decoded.image.width);report["photo.decodedHeight"]=std::int64_t(decoded.image.height);
        // The resample the page used to run on the owner thread when the
        // decoded photo arrived (now on the worker, ProfileDecodedImage::working).
        m::ProfileImage working;set("photo.workingCopyMS",timed([&]{working=m::profileWorkingCopy(decoded.image);}));
        // What the page and the card then make from it (the page's panning image of zoom step 1 at 2x, the card's 136 pt crop).
        set("photo.pagePanningImageMS",timed([&]{(void)m::profileStraight(m::profilePanningImage(working,decoded.orientation,82,1,2).image);}));
        set("photo.cardAvatarCropMS",timed([&]{(void)m::IdCardArtwork::avatar(working,decoded.orientation,1,{0,0});}));
    }
    // 5. The next launch of a returning user with that photo avatar and a
    // photo background (stored like the chooser stores one: an oriented PNG of
    // at most 2048 px): the process memory the profile area then keeps.
    const auto backgroundName=gpu::importProfileImage(t.root/"photo.png",m::ProfileImageKind::background,t.root/"Profile"/"Images");
    {
        const auto path=ehud::data::ProfileStore(t.root).path();const auto bytes=ehud::data::detail::readFile(path,1024*1024);need(bytes.has_value(),"The probe record reads back");
        auto record=J::parse(*bytes);record["profile"]["backgroundFilename"]=backgroundName;
        ehud::data::detail::replaceFile(path,*bytes,record.encode(),1024*1024);
        need(ehud::data::ProfileStore(t.root).value().backgroundFilename==std::optional<std::string>(backgroundName),"The probe record keeps the background");
    }
    // Every queued worker job and the completions it schedules, until quiet.
    const auto quiesce=[&]{for(int n=0;n<256;++n){executor.waitIdle();if(!executor.drain())return;}need(false,"The probe's worker settles");};
    const auto megabytes=[](std::size_t bytes){return double(bytes)/(1024*1024);};
    // Private bytes and threads of the process, the renderer's GPU resource
    // bytes (system memory on WARP), the shared rasterizer's bytes and what
    // the page keeps built.
    const auto memory=[&](const std::string&key){
        const auto v=processMemory();report[key+"PrivateMB"]=v.privateMB;report[key+"Threads"]=v.threads;
        report[key+"RendererMB"]=megabytes(renderer.stats().resourceBytes);report[key+"RasterMB"]=megabytes(raster.stats().resourceBytes);
        if(owner){const auto scene=owner->core().sceneStats(core::Module::profile);const auto stats=owner->stats();
            report[key+"PagePixelMB"]=megabytes(scene?scene->residentPixelBytes:0);report[key+"PageRasterEntries"]=double(scene?scene->rasterEntries:0);
            report[key+"PortraitMB"]=megabytes(stats.portraitBytes);report[key+"ArtGPU"]=stats.artResident;report[key+"ArtMB"]=megabytes(stats.artBytes);}
        return v;};
    const auto before=memory("memory.relaunch.before");
    set("relaunch.ownerMS",timed([&]{owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,context,work,o);open(*owner,time);}));
    const auto relaunch=Clock::now();owner->start(time);
    measure("relaunch.loadOwnerThreadMS","relaunch.loadLongestDrainMS",[&]{return owner->loaded();});
    measure("relaunch.photoOwnerThreadMS","relaunch.photoLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::avatar)!=nullptr&&owner->cardTexture(tools::IdCardTextureKind::background)!=nullptr;});
    quiesce();set("relaunch.photoReadyWallMS",since(relaunch));
    // Launched with the HUD closed: decoded images and card textures only.
    const auto decoded=memory("memory.relaunch.decoded");
    report["memory.relaunch.decodedResidentMB"]=decoded.privateMB-before.privateMB;
    frames(*owner,"relaunch");
    // The page's portrait and the art are published, then measured.
    {core::ModulePresentation modules(core::Module::profile);designScreen(modules);for(unsigned n=0;n<3;++n){quiesce();(void)frame(*owner,modules,1./60);}composition.detach(renderer);}
    const auto shown=memory("memory.relaunch.shown");
    report["memory.relaunch.photoProfileResidentMB"]=shown.privateMB-before.privateMB;
    // 6. Adjust portrait: one committed zoom step at a time (the slider's
    // accessible value or a nudge), owner-thread wall time per step; the
    // crop itself runs on the worker.
    {
        core::ModulePresentation modules(core::Module::profile);designScreen(modules);(void)frame(*owner,modules,1./60);
        auto&page=*owner->canvas();
        need(page.perform("profile:menu",time)&&page.perform("profile.more:portrait",time),"The portrait popover opens");(void)frame(*owner,modules,1./60);
        double total{},slowest{};unsigned steps{};
        for(unsigned n=1;n<=12;++n){const double d=timed([&]{need(page.adjustAccessibilitySlider("profile:avatarZoom",1+.25*n,time),"A portrait zoom step");});total+=d;slowest=std::max(slowest,d);++steps;(void)frame(*owner,modules,1./60);}
        set("slider.avatarZoomStepOwnerMS",total/steps);set("slider.avatarZoomStepLongestMS",slowest);
        double drains{},drainPeak{};
        for(int n=0;n<256;++n){executor.waitIdle();const double d=timed([&]{(void)executor.drain();});drains+=d;drainPeak=std::max(drainPeak,d);if(!executor.stats().pending&&!executor.stats().running&&!executor.stats().completed)break;}
        set("slider.completionOwnerMS",drains);set("slider.completionLongestDrainMS",drainPeak);
        (void)frame(*owner,modules,1./60);
        page.closePopover();(void)frame(*owner,modules,1./60);
        report["slider.portraitJobs"]=std::int64_t(owner->stats().portraitJobs);report["slider.portraitPublications"]=std::int64_t(owner->stats().portraitPublications);
        // 7. The HUD closes while another module is shown (HUDModuleContent
        // keeps only the shown screen): the page parks; showing it again
        // rebuilds it.
        modules.select(core::Module::notes,time);for(unsigned n=0;n<40;++n)(void)frame(*owner,modules,1./60);
        memory("memory.relaunch.otherModule");
        owner->setOverlayVisible(false,time);owner->core().overlayClosing(time);(void)frame(*owner,modules,1./60);
        (void)owner->core().hostEvent({app::HostEvent::Kind::overlayConcealed},time);quiesce();(void)frame(*owner,modules,1./60);
        memory("memory.relaunch.parked");
        owner->core().setOverlayVisible(true,time);owner->setOverlayVisible(true,time);(void)frame(*owner,modules,1./60);
        // Rebuilding the parked page's artwork (what its next first frame
        // otherwise does before uploading), then the frames showing it.
        {core::ModulePresentation wanted(core::Module::profile);designScreen(wanted);const auto sample=wanted.sample(time).presentation;
         set("reshow.buildMS",timed([&]{owner->core().prewarm(app::ModuleFrame{core::Matrix4{},chrome,sample,0,time,true});}));}
        modules.select(core::Module::profile,time);
        double first{},peak{};for(unsigned n=0;n<40;++n){const double d=frame(*owner,modules,1./60);if(!n)first=d;peak=std::max(peak,d);}
        set("reshow.firstProfileFrameMS",first);set("reshow.peakFrameMS",peak);
        memory("memory.relaunch.reshown");
        composition.detach(renderer);
    }
    owner->release(renderer);owner.reset();quiesce();
    memory("memory.relaunch.released");
    // 8. The Work Mode page on the design library (the owner app/application.cpp
    // constructWorkMode builds): what it keeps while shown, while a session runs
    // (a one-second tick repaints the digits and at most one tick chunk),
    // switched away inside the open HUD and once the HUD closed on another
    // module, and its first frames.
    {
        // The Work Mode page shares the probe's one design Context (the host's).
        m::WorkModeCanvas*workShown{};m::WorkModeController controller({[&]{if(workShown)workShown->controllerChanged();},{}});
        tools::DesignOwnerOptions wo;wo.name="workMode";wo.raster.pixelsPerPoint=2;tools::DesignOwnerCore page(raster,context,std::move(wo));
        m::WorkModeCanvasOptions co;co.clock=[&]{return time;};
        auto canvas=std::make_unique<m::WorkModeCanvas>(context,controller,std::move(co));auto&workPage=*canvas;workShown=canvas.get();page.add(std::move(canvas));page.resize(metrics);
        core::ModulePresentation modules(core::Module::workMode);modules.setDesignScreen(core::Module::workMode,true);
        const auto center=core::source::DesktopChromeLayout::make(chrome,{},{}).designToScreen;
        const auto paint=[&](double advance){return timed([&]{time+=advance;(void)context.advance(time);controller.wake(time);const auto presented=modules.sample(time).presentation;chrome.module=presented.requested;
            page.update({center,chrome,presented,1.f,time,true});page.upload(renderer);composition.setEntries(renderer,page.entries(core::Module::workMode));composition.present(renderer);page.collected(renderer);});};
        const auto rasterBase=raster.stats();const auto rendererBase=renderer.stats().resourceBytes;
        const auto sample=[&](const std::string&key){const auto r=raster.stats();const auto scene=page.sceneStats(core::Module::workMode);
            report[key+"RasterMB"]=megabytes(r.resourceBytes-rasterBase.resourceBytes);report[key+"RendererMB"]=megabytes(renderer.stats().resourceBytes-rendererBase);
            report[key+"PagePixelMB"]=megabytes(scene?scene->residentPixelBytes:0);report[key+"Surfaces"]=double(scene?scene->surfaces:0);report[key+"RasterEntries"]=double(scene?scene->rasterEntries:0);};
        set("workMode.firstFrameMS",paint(1./60));for(unsigned n=0;n<30;++n)(void)paint(1./60);sample("workMode.shown");
        {const auto idleStart=raster.stats().rasterizations;double peak{};for(unsigned n=0;n<60;++n)peak=std::max(peak,paint(1./60));
         report["workMode.idleRasterizations"]=double(raster.stats().rasterizations-idleStart);set("workMode.idlePeakFrameMS",peak);}
        workPage.primaryAction();(void)paint(1./60);
        {const auto runStart=raster.stats().rasterizations;double total{},peak{};for(unsigned n=0;n<180;++n){const double d=paint(1./60);total+=d;peak=std::max(peak,d);}
         report["workMode.runningRasterizationsPerSecond"]=double(raster.stats().rasterizations-runStart)/3;set("workMode.runningFrameMS",total/180);set("workMode.runningPeakFrameMS",peak);}
        workPage.reset();modules.select(core::Module::notes,time);for(unsigned n=0;n<40;++n)(void)paint(1./60);sample("workMode.otherModule");
        page.cancelInteraction(time);page.setOverlayVisible(false,time);(void)paint(1./60);sample("workMode.parked");
        page.setOverlayVisible(true,time);(void)paint(1./60);modules.select(core::Module::workMode,time);
        set("workMode.reshowFirstFrameMS",paint(1./60));for(unsigned n=0;n<30;++n)(void)paint(1./60);sample("workMode.reshown");
        composition.detach(renderer);page.release(renderer);workShown=nullptr;controller.shutdown(time);
    }
    executor.shutdown();
    std::cout<<report.encode()<<'\n';
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{need(SUCCEEDED(hr)&&(argc==4||argc==5),"Pass hud.hlsl, resources/profile, id_card-native-captions.json and optionally the shell catalog");
        run(argv[1],argv[2],argv[3],argc==5?fs::path(argv[4]):fs::path{});CoUninitialize();return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"Profile cold-start probe: "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
