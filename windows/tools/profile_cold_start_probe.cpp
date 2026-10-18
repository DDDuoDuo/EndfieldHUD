// Opt-in Personal Profile cold-start trace (diagnostics, not a contract test).
// Wall milliseconds of what the profile area runs on the owner thread at
// launch, when its record loads, when a returning user's large photo avatar
// loads, on the first frames with another module and with Personal Profile
// shown, and in the host's ID card presenter apply; then the process memory
// (private bytes, threads) a returning user's photo avatar and photo
// background keep resident, and the owner-thread cost of a portrait slider
// step. Hidden HWND, WARP, a NEW temporary data root and a synthetic gradient
// photo only; prints one JSON object of timings and counts, never profile
// content.
//   profile_cold_start_probe <hud.hlsl> <resources/profile> <id_card-native-captions.json> [<shell catalog.ehscene>]
// With the packaged shell catalog it also reports what the source shell's
// SourceScene keeps resident after loading it (CPU bytes and private bytes).
#include "tools/profile_owner.hpp"
#include "tools/work_mode_preview.hpp"
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
    gpu::LayerRasterizer raster;gpu::LayerImageSource images;app::UtilityExecutor executor([]{},32);m::WorkModeController work;
    tools::ProfileOwnerOptions o;o.appRoot=t.root;o.resources=resources;
    const app::ClientMetrics metrics{1280,800,96,1,1280,800};const tools::ProfileOwnerAppearance appearance{core::Language::english,true,{250./255,212./255,31./255},2,false};
    double ownerThread{},longest{};
    const auto settle=[&](auto done){for(int n=0;n<256&&!done();++n){executor.waitIdle();const double d=timed([&]{executor.drain();});ownerThread+=d;longest=std::max(longest,d);}need(done(),"The probe's worker results arrive");};
    const auto measure=[&](const char*total,const char*peak,auto done){ownerThread=longest=0;settle(done);set(total,ownerThread);set(peak,longest);};
    core::source::DesktopChromeSettings chrome;chrome.viewport={0,0,1280,800};gpu::LayerComposition composition;double time{};
    // One presented frame of the owner with the shared module presentation.
    const auto frame=[&](tools::ProfileOwner&owner,core::ModulePresentation&modules,double advance){
        return timed([&]{time+=advance;const auto sample=modules.sample(time).presentation;chrome.module=sample.requested;owner.update({},chrome,sample,1,time);
            owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);owner.collected(renderer);});
    };
    // Frames after the HUD opens on another module, then Personal Profile opened.
    const auto frames=[&](tools::ProfileOwner&owner,const std::string&prefix){
        core::ModulePresentation modules(core::Module::notes);
        set((prefix+".firstFrameOtherModuleMS").c_str(),frame(owner,modules,1./60));set((prefix+".warmFrameOtherModuleMS").c_str(),frame(owner,modules,1./60));
        modules.select(core::Module::profile,time);double first{},peak{};
        for(unsigned n=0;n<36;++n){const double d=frame(owner,modules,1./60);if(!n)first=d;peak=std::max(peak,d);}
        need(owner.page()&&owner.page()->active(),"Personal Profile is shown");
        set((prefix+".firstProfileFrameMS").c_str(),first);set((prefix+".profileOpeningPeakFrameMS").c_str(),peak);
        composition.detach(renderer);
    };
    // 2. A fresh record: owner construction (warm ICU), worker load, card textures, first frames.
    std::unique_ptr<tools::ProfileOwner>owner;
    set("fresh.ownerMS",timed([&]{owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,images,work,o);owner->resize(metrics);owner->setAppearance(appearance,0);}));
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
    need(owner->flush(time),"The probe record flushes");settle([&]{const auto*s=owner->serviceStatus();return s&&!s->busy&&!s->dirty;});
    owner->release(renderer);owner.reset();
    {
        const auto decoded=gpu::decodeProfileImage(t.root/"Profile"/"Images"/stored,m::ProfileImageKind::avatar);
        report["photo.decodedWidth"]=std::int64_t(decoded.image.width);report["photo.decodedHeight"]=std::int64_t(decoded.image.height);
        // The resample the page used to run on the owner thread when the
        // decoded photo arrived (now on the worker, ProfileDecodedImage::working).
        m::ProfileImage working;set("photo.workingCopyMS",timed([&]{working=m::profileWorkingCopy(decoded.image);}));
        // What the page and the card then crop from it (HUDPortraitArtwork at 55 pt and 136 pt, 2x).
        set("photo.pagePortraitCropMS",timed([&]{(void)m::profileStraight(m::profileRenderedImage(working,decoded.orientation,{55,55},1,{0,0},2));}));
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
        report[key+"ImageSourceMB"]=megabytes(images.stats().liveBytes+images.stats().cachedBytes);
        if(owner&&owner->page()){const auto r=owner->page()->resources();
            report[key+"PagePixelMB"]=megabytes(r.scenePixelBytes);report[key+"PageOwnedPhotoMB"]=megabytes(r.ownedPhotoBytes);report[key+"PageGPU"]=r.gpuResident;report[key+"PageBuilt"]=r.built;}
        return v;};
    const auto before=memory("memory.relaunch.before");
    set("relaunch.ownerMS",timed([&]{owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,images,work,o);owner->resize(metrics);owner->setAppearance(appearance,time);}));
    const auto relaunch=Clock::now();owner->start(time);
    measure("relaunch.loadOwnerThreadMS","relaunch.loadLongestDrainMS",[&]{return owner->loaded();});
    measure("relaunch.photoOwnerThreadMS","relaunch.photoLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::avatar)!=nullptr&&owner->cardTexture(tools::IdCardTextureKind::background)!=nullptr;});
    quiesce();set("relaunch.photoReadyWallMS",since(relaunch));
    // Launched with the HUD closed: decoded images and card textures only.
    const auto decoded=memory("memory.relaunch.decoded");
    report["memory.relaunch.decodedResidentMB"]=decoded.privateMB-before.privateMB;
    frames(*owner,"relaunch");
    // The page's portrait crop and backdrop are published, then measured.
    {core::ModulePresentation modules(core::Module::profile);for(unsigned n=0;n<3;++n){quiesce();(void)frame(*owner,modules,1./60);}composition.detach(renderer);}
    const auto shown=memory("memory.relaunch.shown");
    report["memory.relaunch.photoProfileResidentMB"]=shown.privateMB-before.privateMB;
    // 6. Adjust portrait: one committed zoom step at a time (the slider's
    // accessible value or a nudge), owner-thread wall time per step; the
    // crop itself runs on the worker.
    {
        core::ModulePresentation modules(core::Module::profile);(void)frame(*owner,modules,1./60);
        need(owner->perform("profile:menu",time)&&owner->perform("profile:portraitMenu",time),"The portrait popover opens");(void)frame(*owner,modules,1./60);
        double total{},slowest{};unsigned steps{};
        for(unsigned n=1;n<=12;++n){const double d=timed([&]{need(owner->setSlider(m::ProfileField::avatarZoom,1+.25*n,time),"A portrait zoom step");});total+=d;slowest=std::max(slowest,d);++steps;(void)frame(*owner,modules,1./60);}
        set("slider.avatarZoomStepOwnerMS",total/steps);set("slider.avatarZoomStepLongestMS",slowest);
        double drains{},drainPeak{};
        for(int n=0;n<256;++n){executor.waitIdle();const double d=timed([&]{(void)executor.drain();});drains+=d;drainPeak=std::max(drainPeak,d);if(!executor.stats().pending&&!executor.stats().running&&!executor.stats().completed)break;}
        set("slider.completionOwnerMS",drains);set("slider.completionLongestDrainMS",drainPeak);
        (void)frame(*owner,modules,1./60);
        need(owner->perform("profile:popoverClose",time),"The portrait popover closes");(void)frame(*owner,modules,1./60);
        if(const auto*page=owner->page()){const auto r=page->resources();report["slider.cropJobs"]=std::int64_t(r.cropJobs);report["slider.cropPublications"]=std::int64_t(r.cropPublications);}
        // 7. The HUD closes while another module is shown (HUDModuleContent
        // keeps only the shown screen): the page parks; showing it again
        // rebuilds it.
        modules.select(core::Module::notes,time);for(unsigned n=0;n<40;++n)(void)frame(*owner,modules,1./60);
        memory("memory.relaunch.otherModule");
        owner->overlayClosing(time);(void)frame(*owner,modules,1./60);owner->conceal();quiesce();
        memory("memory.relaunch.parked");
        owner->setOverlayVisible(true,time);(void)frame(*owner,modules,1./60);
        // Rebuilding the parked page's artwork (what its next first frame
        // otherwise does before uploading), then the frames showing it.
        {core::ModulePresentation wanted(core::Module::profile);const auto sample=wanted.sample(time).presentation;set("reshow.buildMS",timed([&]{owner->prewarm(sample);}));}
        modules.select(core::Module::profile,time);
        double first{},peak{};for(unsigned n=0;n<40;++n){const double d=frame(*owner,modules,1./60);if(!n)first=d;peak=std::max(peak,d);}
        set("reshow.firstProfileFrameMS",first);set("reshow.peakFrameMS",peak);
        memory("memory.relaunch.reshown");
        composition.detach(renderer);
    }
    owner->release(renderer);owner.reset();quiesce();
    memory("memory.relaunch.released");
    // 8. The Work Mode page (this area's scene and owner): what it keeps
    // while shown, switched away inside the open HUD and once the HUD closed
    // on another module, and its first frames.
    {
        tools::WorkModePreviewOptions wo;wo.raster.pixelsPerPoint=2;wo.clipboard=nullptr;
        tools::WorkModePreview page(w.hwnd,raster,nullptr,TF_CLIENTID_NULL,wo);page.resize(metrics);page.setAppearance({true,2,{250./255,212./255,31./255,1}});page.focus(true,time);
        core::ModulePresentation modules(core::Module::workMode);
        const auto paint=[&](double advance){time+=advance;const auto sample=modules.sample(time).presentation;chrome.module=sample.requested;
            return timed([&]{page.update({},chrome,sample,1,time);page.upload(renderer);composition.setEntries(renderer,page.entries());composition.present(renderer);page.collected(renderer);});};
        const auto rasterBase=raster.stats().resourceBytes;const auto rendererBase=renderer.stats().resourceBytes;
        const auto sample=[&](const std::string&key){report[key+"RasterMB"]=megabytes(raster.stats().resourceBytes-rasterBase);report[key+"RendererMB"]=megabytes(renderer.stats().resourceBytes-rendererBase);
            report[key+"PagePixelMB"]=megabytes(page.resources().scenePixelBytes);report[key+"GPU"]=page.resources().gpuResident;};
        set("workMode.firstFrameMS",paint(1./60));for(unsigned n=0;n<30;++n)(void)paint(1./60);sample("workMode.shown");
        modules.select(core::Module::notes,time);for(unsigned n=0;n<40;++n)(void)paint(1./60);sample("workMode.otherModule");
        page.cancelInteraction(time);page.setOverlayVisible(false,time);(void)paint(1./60);sample("workMode.parked");
        page.setOverlayVisible(true,time);(void)paint(1./60);modules.select(core::Module::workMode,time);
        set("workMode.reshowFirstFrameMS",paint(1./60));for(unsigned n=0;n<30;++n)(void)paint(1./60);sample("workMode.reshown");
        composition.detach(renderer);page.shutdown(time);page.release(renderer);
        // What the source ring and ring base (440 pt circle strokes) would
        // keep as rasters at this scale, against the GPU meshes that replace them.
        gpu::LayerRasterizer local;gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;m::WorkModeController c;m::WorkModePresentation s(c);s.refresh(time);
        const auto art=m::prepareWorkModeArtwork(s,{true,2,{250./255,212./255,31./255,1}});std::size_t rasterBytes{},meshBytes{};
        std::vector<std::unique_ptr<gpu::LayerScene>>rings;std::array<std::vector<gpu::Vertex>,2>vertices;std::array<std::vector<std::uint32_t>,2>indices;
        const auto textureBase=renderer.stats().resourceBytes;
        for(const auto*part:{&art.ringBase,&art.ring}){
            auto ring=std::make_unique<gpu::LayerScene>(local);ring->load(part->layers,ro);rasterBytes+=ring->report().pixelBytes;ring->uploadResources(renderer);rings.push_back(std::move(ring));}
        const auto textureBytes=renderer.stats().resourceBytes-textureBase;for(auto&ring:rings)(void)ring->releaseResources(renderer);
        const auto meshBase=renderer.stats().resourceBytes;
        for(std::size_t n=0;n<2;++n){const auto*part=n?&art.ring:&art.ringBase;gpu::workModeRingMesh(part->layers["children"].array()[0]["shape"],2,vertices[n],indices[n]);
            meshBytes+=vertices[n].size()*sizeof(gpu::Vertex)+indices[n].size()*sizeof(std::uint32_t);(void)renderer.setMesh("probe.ring."+std::to_string(n),1,{vertices[n],indices[n]});}
        const auto meshGPU=renderer.stats().resourceBytes-meshBase;for(std::size_t n=0;n<2;++n)(void)renderer.removeMesh("probe.ring."+std::to_string(n));
        report["workMode.ringRasterEquivalentMB"]=megabytes(rasterBytes);report["workMode.ringTextureEquivalentMB"]=megabytes(textureBytes);
        report["workMode.ringMeshMB"]=megabytes(meshBytes);report["workMode.ringMeshGPUMB"]=megabytes(meshGPU);
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
