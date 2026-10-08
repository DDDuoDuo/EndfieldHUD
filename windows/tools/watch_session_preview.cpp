// Build-only source-shell feasibility tool. Every input is an explicit synthetic
// export/cache. Visible launch is opt-in; benchmark never shows its owned HWND.
#include "app/overlay_host.hpp"
#include "app/event_log_save_queue.hpp"
#include "modules/hud_clock.hpp"
#include "native/event_log_owner.hpp"
#include "tools/notes_preview.hpp"
#include "tools/shelf_preview.hpp"
#include "tools/clipboard_preview.hpp"
#include "tools/volume_preview.hpp"
#include "tools/battery_preview.hpp"
#include "tools/event_log_preview.hpp"
#include "tools/work_mode_preview.hpp"
#include "native/clipboard_assets.hpp"
#include "native/shelf_file_picker.hpp"
#include "app/source_watch_session.hpp"
#include "native/watch_presentation.hpp"
#include "native/chrome_presentation.hpp"
#include "native/watch_content.hpp"
#include "native/source_cursor.hpp"
#include "native/tray_controller.hpp"
#include "native/application_icon.hpp"
#include "native/display_service.hpp"
#include "native/desktop_backdrop.hpp"
#include "native/text_input_diagnostics.hpp"
#include "core/shell_packet.hpp"
#include "core/watch_runtime_input.hpp"
#include "core/data/file_io.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <psapi.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace fs=std::filesystem;
namespace core=endfield::core;
namespace source=core::source;
namespace app=endfield::app;
namespace gpu=endfield::native;
namespace packet=core::packet;
using ehud::data::Json;
namespace {
void need(bool value,const char*message){if(!value)throw std::runtime_error(message);}
std::string utf8(const fs::path&value){const auto bytes=value.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
double now(){return app::OverlayHost::clockNow();}
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
struct COM {COM(){need(SUCCEEDED(OleInitialize(nullptr)),"OLE initialization failed");}~COM(){OleUninitialize();}};
// The native backdrop borrows the caller's one UI queue. Creating its controller
// on this UI thread adds no worker or private animation clock. It outlives HWND
// and composition cleanup, including exception unwinding.
class BackdropQueue final {
public:
    BackdropQueue(){
        if(winrt::Windows::System::DispatcherQueue::GetForCurrentThread())return;
        DispatcherQueueOptions value{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        const auto result=CreateDispatcherQueueController(value,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller_)));
        if(FAILED(result))throw winrt::hresult_error(result,L"Source preview: create caller-owned backdrop DispatcherQueue");
    }
    ~BackdropQueue(){try{finish();}catch(...) {}}
    void finish(){
        if(!controller_)return;const auto operation=controller_.ShutdownQueueAsync();const auto deadline=GetTickCount64()+10000;
        while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){
            need(GetTickCount64()<deadline,"Source preview backdrop queue shutdown exceeded its finite deadline");
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                // Other publishers can keep the queue nonempty during shutdown.
                need(GetTickCount64()<deadline,"Source preview backdrop queue shutdown exceeded its finite deadline");
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started)MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        operation.GetResults();controller_=nullptr;
    }
private:
    winrt::Windows::System::DispatcherQueueController controller_{nullptr};
};
// Keep all synchronous HWND teardown callbacks behind ready=false, before the
// locals they normally borrow leave scope. Also restores DWM state on failure.
struct PreviewWindowLifetime final {
    bool&ready;app::OverlayHost&host;gpu::Renderer&renderer;gpu::DesktopBackdrop&backdrop;gpu::LayerComposition&composition;std::unique_ptr<endfield::tools::NotesPreview>&notes;std::unique_ptr<endfield::tools::ShelfPreview>&shelf;std::unique_ptr<endfield::tools::ClipboardPreview>&clipboard;std::unique_ptr<endfield::tools::VolumePreview>&volume;std::unique_ptr<endfield::tools::EventLogPreview>&eventLog;std::unique_ptr<endfield::tools::WorkModePreview>&workMode;std::unique_ptr<endfield::tools::BatteryPreview>&battery;
    ~PreviewWindowLifetime(){ready=false;bool detached=false;try{composition.detach(renderer);detached=true;if(workMode&&renderer.stats().initialized)workMode->release(renderer);if(battery&&renderer.stats().initialized)battery->release(renderer);if(notes&&renderer.stats().initialized)notes->release(renderer);if(shelf&&renderer.stats().initialized)shelf->release(renderer);if(clipboard&&renderer.stats().initialized)clipboard->release(renderer);if(volume&&renderer.stats().initialized)volume->release(renderer);if(eventLog&&renderer.stats().initialized)eventLog->release(renderer);}catch(...) {} if(detached){battery.reset();workMode.reset();notes.reset();shelf.reset();clipboard.reset();volume.reset();eventLog.reset();}backdrop.reset();renderer.reset();try{host.destroy();}catch(...) {}}
};
// Accepted immutable writes finish before the HWND is destroyed. Completion
// routes are detached first, including during exception unwinding. The picker
// is UI-owned and never survives its window; none of these services polls.
struct PreviewServicesLifetime final {
    bool&ready;std::unique_ptr<app::EventLogSaveQueue>&saves;
    std::unique_ptr<app::UtilityExecutor>&utility;std::unique_ptr<gpu::NativeShelfFilePicker>&picker;std::unique_ptr<gpu::TrayController>&tray;
    ~PreviewServicesLifetime(){ready=false;tray.reset();picker.reset();saves.reset();utility.reset();}
};
struct Options {fs::path packet,cache,shader,chrome,cursor,report,snapshots,watchBlur,notesAssets,notesData,notesFormatAssets,shelfAssets,shelfData,shelfMask,clipboardAssets,liveDiagnostics,residentIcons;std::string pin,notesAssetsSHA;bool visible{},warp{},runtimeInput{},coverage{},moduleCoverage{},volumeFixture{},eventLogFixture{},workModeFixture{},batteryFixture{};std::uint32_t benchmarkWidth{1280},benchmarkHeight{800};double benchmarkEpoch{};};
Options options(int argc,wchar_t**argv){
    need(argc>=5,"Usage: watch_session_preview packet-root compiled-scene hud.hlsl --benchmark new-report.json | --visible --watch-blur original-watch-blur.json [--chrome chrome.json] [--cursor-png original.png] [--compiled-sha sha256] [--snapshots new-directory] [--warp] [--runtime-input] [--benchmark-size width height] [--benchmark-epoch seconds] [--coverage]");
    Options o;o.packet=fs::absolute(argv[1]);o.cache=fs::absolute(argv[2]);o.shader=fs::absolute(argv[3]);
    for(int i=4;i<argc;++i){const std::wstring_view arg=argv[i];
        if(arg==L"--visible"){need(!o.visible,"Duplicate visible mode");o.visible=true;}
        else if(arg==L"--live-diagnostics"&&i+1<argc){need(o.liveDiagnostics.empty(),"Duplicate live diagnostic output");o.liveDiagnostics=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-assets"&&i+1<argc){need(o.shelfAssets.empty(),"Duplicate shelf assets");o.shelfAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-data"&&i+1<argc){need(o.shelfData.empty(),"Duplicate shelf data");o.shelfData=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-mask"&&i+1<argc){need(o.shelfMask.empty(),"Duplicate shelf reveal");o.shelfMask=fs::absolute(argv[++i]);}
        else if(arg==L"--event-log-fixture"){need(!o.eventLogFixture,"Duplicate Event Log fixture");o.eventLogFixture=true;}
        else if(arg==L"--resident-preview-icons"&&i+1<argc){need(o.residentIcons.empty(),"Duplicate resident preview icon directory");o.residentIcons=fs::absolute(argv[++i]);}
        else if(arg==L"--module-coverage"){need(!o.moduleCoverage,"Duplicate module coverage");o.moduleCoverage=true;}
        else if(arg==L"--battery-fixture"){need(!o.batteryFixture,"Duplicate battery fixture");o.batteryFixture=true;}
        else if(arg==L"--work-mode-fixture"){need(!o.workModeFixture,"Duplicate Work Mode fixture");o.workModeFixture=true;}
        else if(arg==L"--volume-fixture"){need(!o.volumeFixture,"Duplicate Volume fixture");o.volumeFixture=true;}
        else if(arg==L"--coverage"){need(!o.coverage,"Duplicate coverage mode");o.coverage=true;}
        else if(arg==L"--benchmark-size"&&i+2<argc){auto dimension=[&]{const std::wstring token=argv[++i];std::size_t used{};const auto value=std::stoul(token,&used);need(used==token.size()&&value>=64&&value<=4096,"Benchmark dimensions must be 64...4096 pixels");return static_cast<std::uint32_t>(value);};o.benchmarkWidth=dimension();o.benchmarkHeight=dimension();}
        else if(arg==L"--benchmark-epoch"&&i+1<argc){const std::wstring token=argv[++i];std::size_t used{};o.benchmarkEpoch=std::stod(token,&used);need(used==token.size()&&std::isfinite(o.benchmarkEpoch)&&o.benchmarkEpoch>=0&&o.benchmarkEpoch<=1e9,"Invalid bounded synthetic epoch");}
        else if(arg==L"--runtime-input"){need(!o.runtimeInput,"Duplicate runtime-input mode");o.runtimeInput=true;}
        else if(arg==L"--warp"){need(!o.warp,"Duplicate WARP mode");o.warp=true;}
        else if(arg==L"--benchmark"&&i+1<argc){need(o.report.empty(),"Duplicate benchmark output");o.report=fs::absolute(argv[++i]);}
        else if(arg==L"--snapshots"&&i+1<argc){need(o.snapshots.empty(),"Duplicate snapshot directory");o.snapshots=fs::absolute(argv[++i]);}
        else if(arg==L"--chrome"&&i+1<argc){need(o.chrome.empty(),"Duplicate chrome reference");o.chrome=fs::absolute(argv[++i]);}
        else if(arg==L"--cursor-png"&&i+1<argc){need(o.cursor.empty(),"Duplicate cursor image");o.cursor=fs::absolute(argv[++i]);}
        else if(arg==L"--watch-blur"&&i+1<argc){need(o.watchBlur.empty(),"Duplicate original WatchBlur source");o.watchBlur=fs::absolute(argv[++i]);}
        else if(arg==L"--notes-assets"&&i+1<argc){need(o.notesAssets.empty(),"Duplicate Notes assets");o.notesAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--clipboard-assets"&&i+1<argc){need(o.clipboardAssets.empty(),"Duplicate Clipboard assets");o.clipboardAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--notes-format-assets"&&i+1<argc){need(o.notesFormatAssets.empty(),"Duplicate Notes format assets");o.notesFormatAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--notes-data"&&i+1<argc){need(o.notesData.empty(),"Duplicate Notes fixture data");o.notesData=fs::absolute(argv[++i]);}
        else if(arg==L"--notes-assets-sha"&&i+1<argc){need(o.notesAssetsSHA.empty(),"Duplicate Notes asset pin");o.notesAssetsSHA=utf8(argv[++i]);}
        else if(arg==L"--compiled-sha"&&i+1<argc){need(o.pin.empty(),"Duplicate cache pin");o.pin=utf8(argv[++i]);}
        else need(false,"Unknown or incomplete preview argument");
    }
    need(o.notesAssets.empty()==o.notesData.empty()&&o.notesAssets.empty()==o.notesAssetsSHA.empty(),"Notes preview requires assets, independent SHA and a new data root together");
    need(o.visible||o.notesAssets.empty()||o.moduleCoverage,"Hidden module integration requires explicit --module-coverage");
    need(!o.moduleCoverage||(!o.visible&&!o.notesAssets.empty()),"Module coverage requires hidden mode and fresh isolated Notes data");
    need(o.notesData.empty()||!fs::exists(o.notesData),"Notes preview data root must be new");
    need(o.visible!=!o.report.empty(),"Choose exactly one explicit visible or benchmark mode");need(!o.visible||!o.warp,"WARP is a hidden test mode only");
    need(!o.visible||!o.watchBlur.empty(),"Visible preview requires the explicit source-pinned --watch-blur animation");
    need(!o.visible||(!o.coverage&&o.benchmarkEpoch==0&&o.benchmarkWidth==1280&&o.benchmarkHeight==800),"Coverage/epoch/size overrides are hidden-test only");
    need(o.liveDiagnostics.empty()||(o.visible&&!o.notesAssets.empty()&&!fs::exists(o.liveDiagnostics)),"Live diagnostics require visible isolated Notes and a new output");
    need(o.shelfAssets.empty()==o.shelfData.empty()&&o.shelfAssets.empty()==o.shelfMask.empty(),"Shelf preview needs assets, fresh data and original reveal samples");
    need(!o.eventLogFixture||!o.notesAssets.empty(),"Event Log fixture requires the shared module owner");
    need(o.residentIcons.empty()||(o.visible&&!o.notesAssets.empty()),"Resident test requires the explicitly visible isolated module fixture");
    need(!o.batteryFixture||!o.notesAssets.empty(),"Battery fixture requires shared module ownership");
    need(!o.workModeFixture||!o.notesAssets.empty(),"Work Mode fixture needs shared module/text ownership");
    need(!o.volumeFixture||!o.notesAssets.empty(),"Volume fixture requires the shared module owner");
    need(o.clipboardAssets.empty()||!o.notesAssets.empty(),"Clipboard needs the shared module owner");
    need(o.shelfAssets.empty()||!o.notesAssets.empty(),"Shelf uses the shared Notes module transition owner");
    if(!o.report.empty())need(!fs::exists(o.report),"Benchmark output already exists");
    need(o.snapshots.empty()||!o.visible,"Owned-target snapshots are hidden benchmark only");
    if(!o.snapshots.empty())need(!fs::exists(o.snapshots),"Snapshot directory already exists");return o;
}
Json loadJSON(const fs::path&file){const auto bytes=ehud::data::detail::readFile(file,32*1024*1024);need(bytes.has_value(),"Explicit reference JSON is missing");return Json::parse(*bytes,32*1024*1024);}
gpu::DesktopBackdropAnimation loadWatchBlur(const fs::path&file){
    const auto bytes=ehud::data::detail::readFile(file,1024*1024);need(bytes.has_value(),"Explicit original WatchBlur source is missing");
    const std::span<const std::uint8_t> raw{reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()};
    // Authoritative Mac Resources/WatchSource/Scene/watch-blur.json, unmodified.
    // No oracle sample, screenshot or hand-authored approximation is substituted.
    need(packet::sha256(raw)=="87767f77fe5845150e0dc675771cc6adc792075cbbd8fb6d41f405e6db931464","WatchBlur source hash differs from the authoritative Mac resource");
    return gpu::DesktopBackdropAnimation::fromSource(Json::parse(*bytes,1024*1024));
}
struct ProcessUsage {double cpuSeconds{};std::uint64_t privateBytes{},workingBytes{},peakWorkingBytes{};};
ProcessUsage processUsage(){
    FILETIME created,exited,kernel,user;need(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"Process CPU counters unavailable");
    auto seconds=[](FILETIME t){return (double(t.dwHighDateTime)*4294967296.+t.dwLowDateTime)*1e-7;};
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);need(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=0,"Process memory counters unavailable");
    return {seconds(kernel)+seconds(user),std::uint64_t(memory.PrivateUsage),std::uint64_t(memory.WorkingSetSize),std::uint64_t(memory.PeakWorkingSetSize)};
}
Json memoryJSON(ProcessUsage u){return Json::Object{{"privateBytes",std::int64_t(u.privateBytes)},{"workingSetBytes",std::int64_t(u.workingBytes)},{"peakWorkingSetBytes",std::int64_t(u.peakWorkingBytes)}};}
class StartupStages {
public:
    StartupStages():previousTime_(Clock::now()),previousUsage_(processUsage()){}
    void mark(const char*name){const auto time=Clock::now();const auto usage=processUsage();
        rows_.push_back(Json::Object{{"stage",name},{"wallMilliseconds",std::chrono::duration<double,std::milli>(time-previousTime_).count()},
            {"processCPUSeconds",usage.cpuSeconds-previousUsage_.cpuSeconds},{"memoryBefore",memoryJSON(previousUsage_)},{"memoryAfter",memoryJSON(usage)}});
        previousTime_=time;previousUsage_=usage;
    }
    const Json::Array&rows()const noexcept{return rows_;}
private:
    Clock::time_point previousTime_;ProcessUsage previousUsage_;Json::Array rows_;
};
struct LiveProbe {
    enum Stage {frame,backdrop,sourcePose,materials,nativeLabels,notesPose,publication,draw,pointer,key,message,editTransition,count};
    struct Timing{std::uint64_t calls{};double total{},maximum{};};
    struct Scope {
        Timing*value{};Clock::time_point start{};
        Scope()noexcept=default;
        Scope(Timing*target,Clock::time_point at)noexcept:value(target),start(at){}
        Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
        Scope(Scope&&other)noexcept:value(std::exchange(other.value,nullptr)),start(other.start){}
        Scope&operator=(Scope&&)=delete;
        ~Scope(){if(value){const auto ms=milliseconds(start);++value->calls;value->total+=ms;value->maximum=std::max(value->maximum,ms);}}
    };
    bool enabled{};int phase{-1};double phaseStart{};std::array<Timing,count> timings{};Json::Array rows;
    Scope measure(Stage stage){return enabled?Scope{&timings[stage],Clock::now()}:Scope{};}
    void flush(double time){if(phase<0)return;Json::Object stages;
        constexpr std::array names{"frame","backdrop","sourcePose","materials","nativeLabels","notesPose","publication","draw","pointer","key","message","editTransition"};
        for(unsigned i=0;i<count;++i){const auto&t=timings[i];stages[names[i]]=Json::Object{{"calls",std::int64_t(t.calls)},{"totalMS",t.total},{"meanMS",t.calls?t.total/t.calls:0},{"maxMS",t.maximum}};}
        const auto input=gpu::textInputDiagnostics();auto timing=[](const auto&t){return Json::Object{{"calls",std::int64_t(t.calls)},{"totalMS",t.totalMilliseconds},{"maxMS",t.maximumMilliseconds}};};
        rows.push_back(Json::Object{{"phase",phase},{"elapsedSeconds",time-phaseStart},{"stages",std::move(stages)},
          {"input",Json::Object{{"notifyLayout",timing(input.notifyLayout)},{"requestLock",timing(input.requestLock)},{"getTextExt",timing(input.getTextExt)},
          {"postAttempts",std::int64_t(input.postAttempts)},{"newPosts",std::int64_t(input.newPosts)},{"layoutOnlyPosts",std::int64_t(input.layoutOnlyPostAttempts)},
          {"placementChanged",std::int64_t(input.placementChanged)},{"placementEqual",std::int64_t(input.placementEqual)},{"locksDuringLayout",std::int64_t(input.locksDuringLayout)},{"extentsDuringLayout",std::int64_t(input.extentsDuringLayout)}}}});
    }
    void next(int value,double time){flush(time);phase=value;phaseStart=time;timings={};gpu::setTextInputDiagnosticsEnabled(true);}
};
struct Snapshot {
    gpu::RendererStats renderer;gpu::SourceGraphicsStats graphics;gpu::LayerRasterStats raster;
    source::SourceWatchFrameStats frame;app::WatchSessionStats session;
};
Json counters(const Snapshot&a,const Snapshot&b){return Json::Object{
    {"sourceGeometryUploads",std::int64_t(b.graphics.geometryUploads-a.graphics.geometryUploads)},
    {"sourceUniformUploads",std::int64_t(b.graphics.uniformUploads-a.graphics.uniformUploads)},
    {"sourceTextureUploads",std::int64_t(b.graphics.textureUploads-a.graphics.textureUploads)},
    {"sourceMeshAllocations",std::int64_t(b.graphics.meshBufferAllocations-a.graphics.meshBufferAllocations)},
    {"nativeTextureUploads",std::int64_t(b.renderer.textureUploads-a.renderer.textureUploads)},
    {"nativeMeshUploads",std::int64_t(b.renderer.meshUploads-a.renderer.meshUploads)},
    {"nativeObjectUploads",std::int64_t(b.renderer.objectUploads-a.renderer.objectUploads)},
    {"nativeObjectAllocations",std::int64_t(b.renderer.objectBufferAllocations-a.renderer.objectBufferAllocations)},
    {"localRasterizations",std::int64_t(b.raster.rasterizations-a.raster.rasterizations)},
    {"textLayouts",std::int64_t(b.raster.textLayoutsCreated-a.raster.textLayoutsCreated)},
    {"fullPoses",std::int64_t(b.session.fullPoses-a.session.fullPoses)},
    {"worldOnlyFrames",std::int64_t(b.frame.worldOnlyFrames-a.frame.worldOnlyFrames)},
    {"layoutBuilds",std::int64_t(b.frame.layoutBuilds-a.frame.layoutBuilds)},
    {"localImageBuilds",std::int64_t(b.frame.localImageBuilds-a.frame.localImageBuilds)}};}
}
int wmain(int argc,wchar_t**argv){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;try{
    const auto args=options(argc,argv);COM com;const auto preparation=Clock::now();StartupStages startup;
    std::optional<gpu::DesktopBackdropAnimation> backdropAnimation;if(!args.watchBlur.empty())backdropAnimation=loadWatchBlur(args.watchBlur);startup.mark("original-backdrop-animation");
    std::unique_ptr<source::WatchRuntimeInput> runtime;std::unique_ptr<packet::Package> package;
    Json metadata,legacyTop,legacyBottom,chromeJSON;std::optional<source::SceneDefinition> legacyScene;
    std::optional<source::MountedLayoutDocument> legacyDocument;std::optional<source::Library> legacyLibrary;
    std::optional<source::SourceCamera> legacyCamera;std::optional<source::SourceWatchFrameResources> legacyResources;
    std::optional<source::DesktopHoverProfile> legacyProfile;std::vector<source::AnimatorBinding> legacyAnimators;
    source::SourceDesktopFrameSettings legacyDesktop;
    if(args.runtimeInput){runtime=std::make_unique<source::WatchRuntimeInput>(args.packet,[&](std::string_view name){const std::string label(name);startup.mark(label.c_str());});}
    else {
        package=std::make_unique<packet::Package>(args.packet);startup.mark("packet-manifest");metadata=package->loadAnimation();startup.mark("animation-json");
        legacyScene.emplace(source::SceneDefinition::fromJson(metadata["scene"]));startup.mark("scene-definition");
        legacyDocument.emplace(source::MountedLayoutDocument::fromJson(metadata["mountedDocument"]));startup.mark("mounted-runtime-document");
        legacyLibrary.emplace(source::Library::fromJson(metadata["library"]));startup.mark("animation-library");legacyCamera.emplace(metadata["runtimeRoot"]);startup.mark("camera-and-wrapper");
        legacyResources.emplace(source::SourceWatchFrameResources::fromJson(metadata["frameBuilder"]));legacyProfile=source::DesktopHoverProfile::fromJson(metadata["frameBuilder"]["profileHover"]);startup.mark("frame-resources-profile");
        legacyAnimators=source::AnimatorBinding::fromJson(metadata["mountedDocument"]["animators"]);
        legacyTop=package->loadFrame("desktop-shell-1280x800-top").metadata;legacyBottom=package->loadFrame("desktop-shell-1280x800-bottom").metadata;
        const auto stable=package->loadFrame("desktop-shell-1280x800-opening-4");legacyDesktop=source::SourceDesktopFrameSettings::fromJson(stable.metadata["builderInput"]["desktopSettings"]);startup.mark("reference-frames-settings");
    }
    const auto&scene=runtime?runtime->scene():*legacyScene;const auto&document=runtime?runtime->document():*legacyDocument;
    const auto&library=runtime?runtime->library():*legacyLibrary;const auto&camera=runtime?runtime->camera():*legacyCamera;
    const auto&resources=runtime?runtime->resources():*legacyResources;const auto&profile=runtime?runtime->profile():legacyProfile;
    const auto&animators=runtime?runtime->animators():legacyAnimators;const auto&desktop=runtime?runtime->desktopSettings():legacyDesktop;
    const auto&transitions=runtime?runtime->controllerTransitions():metadata["controllerTransitions"];
    const auto&top=runtime?runtime->nativeTop():legacyTop;const auto&bottom=runtime?runtime->nativeBottom():legacyBottom;
    const auto&nativeButtons=runtime?runtime->nativeButtons():metadata["mountedDocument"]["buttons"];
    const source::WatchAnimation animation(scene,library);
    app::SourceWatchSession session(scene,document,library,camera,resources,animators,transitions,profile,desktop);startup.mark("session-construction");
    const auto sourceManifestSHA256=runtime?runtime->sourceManifestSHA256():package->manifestSHA256();
    gpu::SourceScene materials(gpu::CompiledSourceScene{args.cache,args.pin});
    need(materials.provenance().packetSHA256==sourceManifestSHA256,"Compiled material cache source manifest SHA-256 differs from the input package");
    gpu::WatchMaterialPresentation materialPresentation(materials);startup.mark("compiled-material-cache");
    if(package)for(const auto&asset:package->metadata()["nativeRasterAssets"].array())(void)package->loadNativeRaster(asset["file"].string());
    startup.mark("native-raster-integrity");gpu::LayerRasterizer rasterizer;gpu::LayerScene layers(rasterizer);gpu::LayerRasterOptions rasterOptions;rasterOptions.assetRoot=args.packet;
    if(!args.chrome.empty())chromeJSON=loadJSON(args.chrome);else if(runtime)chromeJSON=runtime->chrome();const bool includesChrome=!chromeJSON.isNull();
    layers.load(!includesChrome?top["nativeLayers"]:gpu::NativeChromePresentation::combinedReferenceRoot(top["nativeLayers"],chromeJSON),rasterOptions);startup.mark("initial-native-rasterization");
    source::NativeLabelPlan labelPlan(scene,source::nativeLabelBindingsFromExport(scene,nativeButtons,top["nativeLayers"],top["nativeProfileBindings"]));
    const std::array contentSnapshots{gpu::WatchContentSnapshot{&top,1},gpu::WatchContentSnapshot{&bottom,0}};
    gpu::WatchContentCatalog contentCatalog(scene,document,labelPlan.bindings(),contentSnapshots);
    const auto&catalogNavigation=contentCatalog.navigation();
    app::WatchSessionNavigation sessionNavigation{catalogNavigation.fixedActions,catalogNavigation.rightActions,catalogNavigation.managedButtons};
    if(profile)if(const auto action=contentCatalog.actionForTarget("profile")){sessionNavigation.fixedActions[profile->rootID]=*action;for(const auto&id:profile->buttonIDs)sessionNavigation.fixedActions[id]=*action;}
    session.setNavigation(std::move(sessionNavigation),0);
    gpu::NativeWatchContent nativeContent(contentCatalog,layers,rasterOptions);startup.mark("native-bindings-content-catalog");
    gpu::WatchLabelPresentation labels(labelPlan,layers);std::vector<gpu::WatchButtonAvailability> available;for(const auto&id:labelPlan.buttonIDs())available.push_back({id,false,false});
    std::unique_ptr<source::DesktopChromeProjectionPlan> chromePlan;std::unique_ptr<gpu::NativeChromePresentation> chrome;
    gpu::DesktopChromeContent chromeContent;chromeContent.reading=source::DesktopClockReading{"12:34:56","WED Oct 7"};chromeContent.uppercaseShortcut="CTRL + `";chromeContent.localizedClose="Click outside to close";
    endfield::modules::HUDClock headerClock([]{SYSTEMTIME value{};GetLocalTime(&value);return endfield::modules::LocalClockFields{value.wMonth,value.wDay,value.wDayOfWeek,value.wHour,value.wMinute,value.wSecond};});
    if(includesChrome){chromePlan=std::make_unique<source::DesktopChromeProjectionPlan>(scene,camera,animation,source::DesktopChromeBindings::fromJson(chromeJSON["bindings"]));chrome=std::make_unique<gpu::NativeChromePresentation>(*chromePlan,layers,chromeJSON,rasterOptions);
        chrome->setContent(chromeContent);}startup.mark("chrome-artwork");
    // SourceCursor outlives OverlayHost; the host must release its borrowed
    // cursor before that application-local handle is destroyed.
    metadata=Json{};legacyTop=Json{};legacyBottom=Json{};chromeJSON=Json{};package.reset();if(runtime)runtime->releaseSetupJSON();startup.mark("release-full-reference-json");
    gpu::SourceCursor cursor;if(!args.cursor.empty())cursor=gpu::SourceCursor::fromOriginalPNG(args.cursor);startup.mark("native-cursor");
    std::unique_ptr<BackdropQueue> backdropQueue;
    app::OverlayHost host;gpu::Renderer renderer;gpu::DesktopBackdrop backdrop;std::unique_ptr<gpu::NativeNotesControlsAssets> notesAssets;std::unique_ptr<endfield::tools::NotesPreview> notes;std::unique_ptr<gpu::NativeShelfAssets>shelfAssets;std::unique_ptr<endfield::tools::ShelfPreview>shelf;std::optional<ehud::data::ShelfFileAccess>pendingShelfReveal;std::unique_ptr<gpu::NativeClipboardAssets>clipboardAssets;std::unique_ptr<endfield::tools::ClipboardPreview>clipboard;std::unique_ptr<endfield::tools::VolumePreview>volume;std::unique_ptr<endfield::tools::EventLogPreview>eventLog;std::unique_ptr<endfield::tools::WorkModePreview>workMode;std::unique_ptr<endfield::tools::BatteryPreview>battery;gpu::LayerComposition composition;app::ClientMetrics metrics;bool ready=false,closing=false,pendingClose=false,focused=!args.visible;
    std::unique_ptr<gpu::EventLogOwner>eventOwner;
    std::unique_ptr<app::UtilityExecutor>utility;
    std::unique_ptr<app::EventLogSaveQueue>eventSaves;
    std::unique_ptr<gpu::NativeShelfFilePicker>mediaPicker;
    gpu::ApplicationIcon applicationIcon;std::unique_ptr<gpu::TrayController>tray;bool quitRequested{};
    core::Point mediaInsertionPoint;bool eventRefreshQueued{},stopping{};
    constexpr UINT eventChangedMessage=WM_APP+194,utilityMessage=WM_APP+195,mediaPickerMessage=WM_APP+196;
    constexpr UINT_PTR serviceGeneration=1;
    PreviewWindowLifetime windowLifetime{ready,host,renderer,backdrop,composition,notes,shelf,clipboard,volume,eventLog,workMode,battery};
    PreviewServicesLifetime servicesLifetime{ready,eventSaves,utility,mediaPicker,tray};
    struct PublishedEntry{gpu::LayerScene*scene;std::uint64_t content,resources;const gpu::DrawObject*after;std::size_t count;};
    std::vector<gpu::LayerCompositionEntry>ordered;std::vector<PublishedEntry>published;ordered.reserve(134);published.reserve(134);std::uint64_t publishedNotesRevision{};
    auto publishNative=[&]{ordered.clear();ordered.push_back({&layers,{}});if(battery){battery->upload(renderer);for(const auto&e:battery->entries())ordered.push_back(e);}if(workMode){workMode->upload(renderer);for(const auto&e:workMode->entries())ordered.push_back(e);}if(eventLog){eventLog->upload(renderer);for(const auto&e:eventLog->entries())ordered.push_back(e);}if(volume){volume->upload(renderer);for(const auto&e:volume->entries())ordered.push_back(e);}if(clipboard){clipboard->upload(renderer);for(const auto&e:clipboard->entries())ordered.push_back(e);}if(shelf){shelf->upload(renderer);for(const auto&e:shelf->entries())ordered.push_back(e);}if(notes){notes->upload(renderer);for(const auto&e:notes->entries())ordered.push_back(e);}
        const auto notesRevision=notes?notes->compositionRevision():0;
        bool changed=ordered.size()!=published.size()||notesRevision!=publishedNotesRevision;if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});if(notes)notes->collected(renderer);if(shelf)shelf->collected(renderer);if(clipboard)clipboard->collected(renderer);if(volume)volume->collected(renderer);if(eventLog)eventLog->collected(renderer);if(workMode)workMode->collected(renderer);if(battery)battery->collected(renderer);}
        publishedNotesRevision=notesRevision;
    };
    app::WatchSessionEnvironment environment{{1280,800},true,true,true,false,{}};app::WatchSessionSettings settings;auto configuration=ehud::data::Settings::defaults();session.setSettings(settings,0);session.setEnvironment(environment,0);
    // Exact independent SystemHUDView canvas opacity. SourceWatch is a
    // sibling view, so this fades only native chrome, never source triangles
    // or SourceWatch's labels. Source completion still owns window lifetime.
    double canvasOpenedAt{},canvasClosedAt{},canvasCapturedOpacity{1};
    auto canvasOpacity=[&](double time){return source::DesktopChromeTiming::opacity(!closing,time-(closing?canvasClosedAt:canvasOpenedAt),canvasCapturedOpacity,settings.reduceMotion);};
    auto backdropOpacity=[&](double time){
        if(session.phase()==core::VisibilityPhase::concealed)return 0.;
        if(settings.reduceMotion)return closing?0.:1.;
        need(backdropAnimation.has_value(),"Visible backdrop lost its immutable original timing");
        return closing?backdropAnimation->exit.alpha(time-canvasClosedAt):backdropAnimation->entrance.alpha(time-canvasOpenedAt);
    };
    // Current Mac AppConfiguration defaults: blur75%, brightness37% (darkness
    // 63%). The native adapter retains the original tint/radial descriptor; its
    // Windows system host material remains an explicit visual-parity gate.
    gpu::DesktopBackdropState backdropState{0,0,true,false,.75,.63,0};
    auto updateBackdrop=[&](double time){
        if(!args.visible)return; // Visible initialization must complete before any frame.
        if(metrics.pixelWidth&&metrics.pixelHeight){backdropState.pixelWidth=metrics.pixelWidth;backdropState.pixelHeight=metrics.pixelHeight;}
        backdropState.lowPower=settings.lowPower;backdropState.sourceOpacity=backdropOpacity(time);backdrop.update(backdropState);
    };
    auto updateClock=[&]{if(chrome){chromeContent.reading=headerClock.reading();if(workMode){
            using P=endfield::modules::WorkModePhase;const auto phase=workMode->controller().snapshot(now()).phase;
            chromeContent.workPhase=phase==P::running?source::DesktopWorkPhase::running:phase==P::paused?source::DesktopWorkPhase::paused:source::DesktopWorkPhase::idle;
        }return chrome->setContent(chromeContent);}return false;};
    auto open=[&](double time){closing=false;canvasOpenedAt=time;session.open(time,0x5eed);if(notes)notes->setMediaActive(true,time);if(workMode)workMode->setOverlayVisible(true,time);if(args.visible&&headerClock.setActive(true,time))updateClock();};
    auto demand=[&](double time){auto result=session.demand(time);if(result.phase==core::VisibilityPhase::visible)result.finiteAnimation=result.finiteAnimation||(notes&&notes->requiresFrames(time))||(shelf&&shelf->requiresFrames(time))||(clipboard&&clipboard->requiresFrames(time))||(volume&&volume->requiresFrames(time))||(eventLog&&eventLog->requiresFrames(time))||(workMode&&workMode->requiresFrames(time))||(battery&&battery->requiresFrames(time));return result;};
    auto scheduleDeadline=[&]{if(!args.visible||!ready||stopping)return;std::optional<double>next;
        auto include=[&](std::optional<double>value){if(value&&(!next||*value<*next))next=value;};
        include(headerClock.nextDeadline());if(workMode)include(workMode->nextWakeTime());if(notes)include(notes->nextWakeTime());if(eventOwner)include(eventOwner->saveDeadline());host.setDeadline(next);
    };
    auto refresh=[&](double time){if(args.visible&&ready&&!stopping){if(workMode)updateClock();host.setFrameDemand(demand(time));scheduleDeadline();host.invalidate();}};
    auto close=[&](double time){if(!closing){std::cout<<"Preview close requested at "<<time<<std::endl;if(notes&&!notes->finish()){pendingClose=true;return;}if(workMode&&!workMode->finishEditing(false,time)){pendingClose=true;return;}pendingClose=false;headerClock.setActive(false,time);if(mediaPicker)mediaPicker->cancel();if(notes)notes->setMediaActive(false,time,true);if(shelf){shelf->cancelPanels();shelf->cancelInteraction();}if(clipboard)clipboard->cancelInteraction();if(volume)volume->cancelInteraction(time);if(battery)battery->cancelInteraction(time);if(eventLog)eventLog->cancelInteraction();if(workMode){workMode->cancelInteraction(time);workMode->setOverlayVisible(false,time);}host.capturePointer(false);environment.pointerLocked=false;session.setEnvironment(environment,time);canvasCapturedOpacity=canvasOpacity(time);canvasClosedAt=time;closing=true;session.close(time);refresh(time);}};
    auto activate=[&](const app::WatchActivation&event){const auto entries=contentCatalog.entries();const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto&value){return value.action==event.action;});need(entry!=entries.end(),"Source activation exceeds exported actions");if(notes){for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);if(core::moduleIdentifier(module)==entry->target){notes->select(module,now());break;}}}
        std::cout<<"Source action: "<<entry->target<<(notes&&entry->target=="notes"?" (Notes preview)":shelf&&entry->target=="fileShelf"?" (File Shelf preview)":clipboard&&entry->target=="clipboard"?" (synthetic Clipboard preview)":" (module body is not installed)")<<'\n';};
    LiveProbe probe;probe.enabled=!args.liveDiagnostics.empty();
    auto present=[&](double time,bool submit){
        auto frameProbe=probe.measure(LiveProbe::frame);
        // Backdrop COM calls can dispatch nested input. Finish them before
        // borrowing a source frame, then sample the current live event time.
        // Offscreen comparisons retain their explicit synthetic timestamps.
        {auto stage=probe.measure(LiveProbe::backdrop);updateBackdrop(time);}if(args.visible)time=std::max(time,now());
        const app::WatchSessionFrame*sample=nullptr;{auto stage=probe.measure(LiveProbe::sourcePose);sample=session.sample(time);}if(!sample)return false;
        if(focused&&sample->visibility.phase==core::VisibilityPhase::visible&&!session.inputEnabled())session.setInputEnabled(true,time);
        auto parameters=materials.parameters();parameters.camera=sample->gpuCamera;parameters.timeSeconds=sample->shaderTime;parameters.width=metrics.pixelWidth;parameters.height=metrics.pixelHeight;
        {auto stage=probe.measure(LiveProbe::materials);materialPresentation.update(*sample->sourceFrame,parameters);materials.flush(renderer.sourceGraphics());}
        {auto stage=probe.measure(LiveProbe::nativeLabels);nativeContent.update(session.actions());
        for(auto&button:available)button.enabled=session.actions().contains(button.buttonID);
        const core::Rect viewport{0,0,metrics.width,metrics.height};labels.update(*sample->sourceFrame,sample->camera,viewport,available);
        // Chrome samples once per visible second; opacity follows the same
        // ready/close timestamps as the source outer controller.
        const source::DesktopChromeSettings chromeSettings{viewport,settings.hudScale,settings.hudOffset,notes?notes->selected():core::Module::power,true};
        if(chrome)chrome->update(*sample->sourceFrame,sample->camera,chromeSettings,static_cast<float>(canvasOpacity(time)));
        }
        const source::DesktopChromeSettings noteChromeSettings{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,notes?notes->selected():core::Module::power,true};
        {auto stage=probe.measure(LiveProbe::notesPose);if(notes&&chromePlan&&chromePlan->projection().center)notes->update(*chromePlan->projection().center,noteChromeSettings,static_cast<float>(canvasOpacity(time)),time,focused);if(workMode&&notes&&chromePlan&&chromePlan->projection().center)workMode->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(battery&&notes&&chromePlan&&chromePlan->projection().center)battery->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(eventLog&&notes&&chromePlan&&chromePlan->projection().center)eventLog->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(volume&&notes&&chromePlan&&chromePlan->projection().center)volume->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(shelf&&notes&&chromePlan&&chromePlan->projection().center)shelf->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(clipboard&&notes&&chromePlan&&chromePlan->projection().center)clipboard->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);}
        {auto stage=probe.measure(LiveProbe::publication);publishNative();composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale,1));}
        if(submit){auto stage=probe.measure(LiveProbe::draw);renderer.draw(args.visible);}return true;
    };
    app::OverlayCallbacks callbacks;
    callbacks.resize=[&](const auto&value){metrics=value;if(notes)notes->resize(value);if(shelf)shelf->resize(value);if(clipboard)clipboard->resize(value);if(volume)volume->resize(value);if(eventLog)eventLog->resize(value);if(workMode)workMode->resize(value);if(battery)battery->resize(value);if(!ready)return;const auto time=now();environment.viewport={value.width,value.height};environment.onScreen=value.pixelWidth>0&&value.pixelHeight>0;session.setEnvironment(environment,time);if(environment.onScreen)renderer.resize(value.pixelWidth,value.pixelHeight);refresh(time);};
    callbacks.pointer=[&](const app::PointerEvent&e){auto stage=probe.measure(LiveProbe::pointer);if(!ready)return false;const auto time=now();const core::Point p{e.x,e.y};
        if(notes&&session.inputEnabled()){
            const bool handled=notes->pointer(e,time);environment.pointerLocked=notes->pointerLocked()||(shelf&&shelf->pointerLocked());environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(shelf&&session.inputEnabled()){
            const bool handled=shelf->pointer(e,time);environment.pointerLocked=shelf->pointerLocked()||(notes&&notes->pointerLocked());environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(clipboard&&session.inputEnabled()){
            const bool handled=clipboard->pointer(e,time);environment.pointerLocked=clipboard->pointerLocked()||(notes&&notes->pointerLocked())||(shelf&&shelf->pointerLocked());environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(volume&&session.inputEnabled()){
            const bool handled=volume->pointer(e,time);environment.pointerLocked=volume->pointerLocked()||(notes&&notes->pointerLocked())||(shelf&&shelf->pointerLocked())||(clipboard&&clipboard->pointerLocked());environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(eventLog&&session.inputEnabled()){
            const bool handled=eventLog->pointer(e,time);environment.pointerLocked=eventLog->pointerLocked()||(volume&&volume->pointerLocked())||(notes&&notes->pointerLocked())||(shelf&&shelf->pointerLocked())||(clipboard&&clipboard->pointerLocked());environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(battery&&session.inputEnabled()&&battery->pointer(e,time)){environment.pointer=p;session.setEnvironment(environment,time);if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        if(workMode&&session.inputEnabled()){
            const bool handled=workMode->pointer(e,time);environment.pointerLocked=workMode->pointerLocked()||(notes&&notes->pointerLocked())||(shelf&&shelf->pointerLocked())||(clipboard&&clipboard->pointerLocked())||(volume&&volume->pointerLocked())||(eventLog&&eventLog->pointerLocked());environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(e.kind==app::PointerKind::move){environment.pointer=p;session.pointerMove(p,time);}
        else if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left){environment.pointer=p;session.pointerDown(p,time);if(session.navigationPointerActive())host.capturePointer(true);}
        else if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){environment.pointer=p;if(const auto action=session.pointerUp(p,time))activate(*action);host.capturePointer(false);}
        else if(e.kind==app::PointerKind::leave){environment.pointer.reset();session.pointerMove({},time);}
        else if(e.kind==app::PointerKind::captureLost){
            // Normal pointerUp has already cleared its drag/press before our
            // intentional ReleaseCapture sends this notification. Preserve its
            // hover; cancel only unfinished ownership lost to another action.
            if(session.navigationPointerActive()||session.pressed()){
                session.setInputEnabled(false,time);if(focused&&session.phase()==core::VisibilityPhase::visible)session.setInputEnabled(true,time);
            }
        }
        else return false;refresh(time);return true;};
    callbacks.wheel=[&](const app::WheelEvent&e){if(!ready)return false;const auto time=now();if(notes&&session.inputEnabled()&&notes->wheel(e,time)){refresh(time);return true;}if(shelf&&session.inputEnabled()&&shelf->wheel(e,time)){refresh(time);return true;}if(clipboard&&session.inputEnabled()&&clipboard->wheel(e,time)){refresh(time);return true;}if(volume&&session.inputEnabled()&&volume->wheel(e,time)){refresh(time);return true;}if(eventLog&&session.inputEnabled()&&eventLog->wheel(e,time)){refresh(time);return true;}if(e.horizontal)return false;const bool handled=session.wheel({e.x,e.y},e.steps,e.linesPerStep,time);if(handled)refresh(time);return handled;};
    callbacks.beforeKeyTranslation=[&](const app::NativeMessage&m){if(!ready)return false;if(shelf&&shelf->filterKey(m))return true;if(workMode&&workMode->filterKey(m))return true;const auto target=static_cast<HWND>(m.window),owner=static_cast<HWND>(host.hwnd());return (target==owner||IsChild(owner,target))&&notes&&notes->filterKey(m);};
    callbacks.appMessage=[&](const app::NativeMessage&m)->std::optional<std::intptr_t>{auto stage=probe.measure(LiveProbe::message);
        if(ready&&tray&&tray->message(m.message,m.wParam,m.lParam)){
            if(const auto action=tray->takeAction()){
                const auto time=now();
                if(*action==gpu::TrayAction::quit){quitRequested=true;if(session.phase()==core::VisibilityPhase::concealed){stopping=true;host.setDeadline({});host.setFrameDemand({});host.requestStop();}else close(time);}
                else if(*action==gpu::TrayAction::openOverlay||*action==gpu::TrayAction::workMode){
                    if(*action==gpu::TrayAction::workMode&&notes)notes->select(core::Module::workMode,time);
                    if(m.message==WM_HOTKEY&&focused&&session.phase()!=core::VisibilityPhase::concealed&&!closing)close(time);
                    else {if(session.phase()==core::VisibilityPhase::concealed||closing)open(time);host.show();refresh(time);}
                }
            }return 0;
        }
        if(ready&&m.wParam==serviceGeneration){
            if(m.message==utilityMessage){if(utility)utility->drain();if(eventSaves)eventSaves->retry();refresh(now());return 0;}
            if(m.message==eventChangedMessage){eventRefreshQueued=false;if(eventLog)eventLog->refresh();refresh(now());return 0;}
            if(m.message==mediaPickerMessage&&mediaPicker){
                host.suspendCursor(true);mediaPicker->handleMessage(m.wParam,m.lParam);host.suspendCursor(false);
                if(auto result=mediaPicker->drain(m.wParam);result&&notes&&!result->canceled()){
                    if(SUCCEEDED(result->result))notes->importMedia(result->paths,mediaInsertionPoint,now());
                    else notes->showMediaError("无法打开所选文件",now());
                }refresh(now());return 0;
            }
        }
        if(ready&&notes&&m.message==endfield::tools::NotesPreview::mediaActionMessage){
            if(auto action=notes->takeMediaAction(m.wParam))try{using K=endfield::tools::NotesMediaAction::Kind;
                if(action->kind==K::chooseLocal){mediaInsertionPoint=action->workspacePoint;host.capturePointer(false);if(!mediaPicker->request())notes->showMediaError("文件选择器正忙",now());}
                else if(action->kind==K::chooseShelf){
                    std::vector<endfield::modules::NotesShelfChoice>choices;
                    if(shelf)for(const auto&item:shelf->state().items()){
                        auto extension=utf8(fs::u8path(item.lastKnownPath).extension());
                        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);});
                        constexpr std::array supported{".png",".jpg",".jpeg",".gif",".heic",".heif",".tif",".tiff",".bmp",".webp",".jp2"};
                        const bool image=!item.isDirectory&&std::find(supported.begin(),supported.end(),extension)!=supported.end();
                        choices.push_back({item.id,item.name,item.typeDescription,image,!item.availabilityError});
                    }
                    notes->presentShelfMedia(std::move(choices),action->workspacePoint,now());
                }else if(shelf)notes->importMedia(shelf->access(action->itemID),action->workspacePoint,now());
            }catch(const std::exception&e){notes->showMediaError(e.what(),now());}
            refresh(now());return 0;
        }
        if(ready&&workMode&&workMode->message(m,now())){if(pendingClose)close(now());refresh(now());return 0;}
        if(ready&&notes&&notes->message(m,now())){if(pendingClose)close(now());refresh(now());return 0;}
        if(ready&&shelf){
            if(shelf->message(m,now())){refresh(now());return 0;}
            if(m.message==endfield::tools::ShelfPreview::actionMessage){
                if(auto action=shelf->takeAction())try{using K=endfield::tools::ShelfPreviewAction::Kind;
                    switch(action->kind){
                    case K::choose:host.capturePointer(false);shelf->requestFiles();break;
                    case K::paste:{Microsoft::WRL::ComPtr<IDataObject>source;need(SUCCEEDED(OleGetClipboard(&source)),"Clipboard is unavailable");const auto paths=gpu::readShelfTransferPaths(*source.Get());source.Reset();shelf->importFiles(paths,now());break;}
                    case K::reveal:pendingShelfReveal=shelf->access(action->itemID);close(now());break;
                    case K::preview:host.capturePointer(false);shelf->requestPreview(action->itemID);break;
                    case K::drag:{auto transfer=shelf->prepareDrag(action->itemID);shelf->setNativeDragActive(true);host.capturePointer(false);environment.pointerLocked=false;session.setEnvironment(environment,now());DWORD effect{};const auto result=transfer->run(&effect);shelf->setNativeDragActive(false);if(result==DRAGDROP_S_DROP&&(effect&DROPEFFECT_COPY))close(now());break;}
                    }
                }catch(const std::exception&e){shelf->setNativeDragActive(false);shelf->showError(e.what(),now());}
                refresh(now());return 0;
            }
        }return {};};
    callbacks.key=[&](const app::KeyEvent&e){auto stage=probe.measure(LiveProbe::key);if(ready&&notes&&session.inputEnabled()&&notes->key(e,now())){refresh(now());return true;}if(ready&&shelf&&session.inputEnabled()&&shelf->key(e,now())){refresh(now());return true;}if(ready&&clipboard&&session.inputEnabled()&&clipboard->key(e,now())){refresh(now());return true;}if(ready&&volume&&session.inputEnabled()&&volume->key(e,now())){refresh(now());return true;}if(ready&&eventLog&&session.inputEnabled()&&eventLog->key(e,now())){refresh(now());return true;}if(ready&&workMode&&session.inputEnabled()&&workMode->key(e,now())){refresh(now());return true;}if(ready&&e.kind==app::KeyKind::down&&e.value==VK_ESCAPE){std::cout<<"Preview unhandled Escape"<<std::endl;close(now());return true;}return false;};
    callbacks.focus=[&](bool value){std::cout<<"Preview focus: "<<value<<std::endl;focused=value;if(notes)notes->focus(value);if(workMode)workMode->focus(value,now());if(shelf&&!value)shelf->cancelInteraction();if(clipboard&&!value)clipboard->cancelInteraction();if(volume&&!value)volume->cancelInteraction(now());if(eventLog&&!value)eventLog->cancelInteraction();if(ready){const auto time=now();if(!focused){environment.pointer.reset();session.pointerMove({},time);session.setInputEnabled(false,time);}else if(session.phase()==core::VisibilityPhase::visible)session.setInputEnabled(true,time);refresh(time);}};
    callbacks.applicationActive=[&](bool active){
        if(!ready||!args.visible||active||closing||session.phase()==core::VisibilityPhase::concealed||!configuration.boolean("closeOnFocusLost"))return;
        // Source exempts its own file panels and active shelf drag/drop. Moving
        // keyboard focus between owned windows is not an application switch.
        const auto picker=mediaPicker?mediaPicker->stats():gpu::ShelfPickerStats{};
        if(picker.queued||picker.presenting||(shelf&&shelf->preservesFocusOnLoss()))return;
        close(now());
    };
    callbacks.closeRequested=[&]{std::cout<<"Preview native close request"<<std::endl;if(ready)close(now());};
    callbacks.deadline=[&](double time){if(!ready||stopping)return;bool artwork=notes&&notes->deadline(time);if(workMode){const auto revision=workMode->state().revision();workMode->wake(time);artwork=artwork||revision!=workMode->state().revision();}if(headerClock.wake(time)||workMode)artwork=updateClock()||artwork;if(eventSaves)eventSaves->capture(time);scheduleDeadline();if(artwork)refresh(time);};
    double diagnosticStart{};bool diagnosticEditing{},diagnosticFinished{};
    callbacks.frame=[&](double time){if(!ready)return;
        if(probe.enabled&&!diagnosticFinished){
            if(diagnosticStart==0)diagnosticStart=time;const auto elapsed=time-diagnosticStart;
            const int phase=elapsed<3?0:elapsed<8?1:elapsed<13?2:3;
            if(phase!=probe.phase)probe.next(phase,time);
            if(phase>=1&&!diagnosticEditing&&phase<3){auto stage=probe.measure(LiveProbe::editTransition);diagnosticEditing=notes->diagnosticEditing(true,time);}
            if(phase==2){environment.pointer=core::Point{metrics.width*.5+200*std::sin(elapsed*2),metrics.height*.5+120*std::cos(elapsed*2)};session.pointerMove(environment.pointer,time);}
            if(phase==3&&diagnosticEditing){auto stage=probe.measure(LiveProbe::editTransition);if(notes->diagnosticEditing(false,time))diagnosticEditing=false;}
            if(elapsed>=16){probe.flush(time);probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);diagnosticFinished=true;close(time);}
        }
        const bool active=present(time,true);if(stopping)return;host.setFrameDemand(demand(time));scheduleDeadline();if(!active&&session.phase()==core::VisibilityPhase::concealed){if(notes)notes->setMediaActive(false,time);host.hide();if(tray&&!quitRequested){scheduleDeadline();}else{stopping=true;host.setDeadline({});host.requestStop();}}};
    app::OverlayOptions windowOptions{!args.notesData.empty()?L"EndfieldHUD Notes preview — temporary sample data":L"EndfieldHUD source shell feasibility — synthetic data",0,0,1280,800,{}};
    if(args.visible){
        const auto displays=gpu::readConnectedDisplays();POINT pointer{};
        need(GetCursorPos(&pointer)!=FALSE,"Cannot locate the current display");
        const auto selected=gpu::resolveDisplay({},displays,{pointer.x,pointer.y});
        need(selected.has_value(),"No usable display for the source shell preview");
        const auto bounds=displays[*selected].bounds;
        windowOptions.x=bounds.left;windowOptions.y=bounds.top;
        windowOptions.pixelWidth=static_cast<std::uint32_t>(bounds.right-bounds.left);
        windowOptions.pixelHeight=static_cast<std::uint32_t>(bounds.bottom-bounds.top);
    }
    if(!args.visible){windowOptions.pixelWidth=args.benchmarkWidth;windowOptions.pixelHeight=args.benchmarkHeight;}
    host.create(windowOptions,std::move(callbacks));metrics=host.metrics();startup.mark("hidden-window");
    renderer.initialize(host.hwnd(),metrics.pixelWidth,metrics.pixelHeight,{args.warp?gpu::Driver::warpForTests:gpu::Driver::hardware,args.shader,args.visible?gpu::RenderTarget::composition:gpu::RenderTarget::offscreenForTests,!args.notesData.empty()});startup.mark("renderer-device-target");const auto deviceInfo=renderer.deviceInfo();
    if(args.visible){
        backdropQueue=std::make_unique<BackdropQueue>();
        const BOOL known=FALSE;const auto result=DwmSetWindowAttribute(static_cast<HWND>(host.hwnd()),DWMWA_USE_HOSTBACKDROPBRUSH,&known,sizeof(known));
        if(FAILED(result))throw winrt::hresult_error(result,L"Source preview: establish owned HWND's disabled original host-backdrop flag");
        backdropState.pixelWidth=metrics.pixelWidth;backdropState.pixelHeight=metrics.pixelHeight;backdrop.initialize(host.hwnd(),backdropState,{false});
    }
    startup.mark("lower-system-backdrop");
    if(!args.notesAssets.empty()){need(bool(chromePlan),"Notes integration requires the source chrome projection");
        notesAssets=std::make_unique<gpu::NativeNotesControlsAssets>(args.notesAssets,gpu::NativeNotesControlsAssetPins{args.notesAssetsSHA,"ca04f142185c7de40acd8523bdb563195d90a1d1"});
        notes=std::make_unique<endfield::tools::NotesPreview>(static_cast<HWND>(host.hwnd()),rasterizer,args.notesData,*notesAssets,args.visible,args.notesFormatAssets);notes->resize(metrics);mediaPicker=std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{static_cast<HWND>(host.hwnd()),mediaPickerMessage,serviceGeneration},gpu::ShelfPickerLabels{"添加图片/视频","添加","添加所选文件"});session.setHitFilter([&](std::string_view,core::Point p){return (!notes||!notes->covers(p))&&(!shelf||!shelf->covers(p))&&(!clipboard||!clipboard->covers(p))&&(!volume||!volume->covers(p))&&(!eventLog||!eventLog->covers(p))&&(!workMode||!workMode->covers(p))&&(!battery||!battery->covers(p));},0);startup.mark("isolated-notes-owner");}
    if(!args.shelfAssets.empty()){
        const auto bytes=ehud::data::detail::readFile(args.shelfMask,128*1024);need(bytes.has_value(),"Missing original Shelf reveal samples");
        auto masks=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),core::SubsectionMaskSampler::assetSHA256);
        shelfAssets=std::make_unique<gpu::NativeShelfAssets>(args.shelfAssets);
        shelf=std::make_unique<endfield::tools::ShelfPreview>(static_cast<HWND>(host.hwnd()),rasterizer,*shelfAssets,endfield::tools::ShelfPreviewOptions{args.shelfData,args.visible,args.visible,std::move(masks)});shelf->resize(metrics);startup.mark("isolated-shelf-owner");
    }
    materials.upload(renderer.sourceGraphics());publishNative();host.setCursor(cursor.handle());ready=true;startup.mark("initial-gpu-upload");
    environment.viewport={metrics.width,metrics.height};const auto start=args.visible?now():args.benchmarkEpoch;session.setEnvironment(environment,start);const auto preparedMS=milliseconds(preparation);
    if(!args.clipboardAssets.empty()){
        clipboardAssets=std::make_unique<gpu::NativeClipboardAssets>(args.clipboardAssets);
        auto synthetic=std::make_shared<gpu::ClipboardSnapshot>();synthetic->capacity=20;
        for(unsigned n=0;n<12;++n)synthetic->rows.push_back({n+1,n==0,gpu::ClipboardKind::text,"临时剪贴板测试 · "+std::to_string(n+1)+" · EndfieldHUD",{}});
        gpu::ClipboardActions actions;actions.snapshot=[synthetic]{return *synthetic;};
        actions.copy=[synthetic](std::uint64_t id){return std::any_of(synthetic->rows.begin(),synthetic->rows.end(),[&](const auto&r){return r.id==id;});};
        actions.togglePin=[synthetic](std::uint64_t id){for(auto&r:synthetic->rows)if(r.id==id){r.pinned=!r.pinned;return true;}return false;};
        actions.remove=[synthetic](std::uint64_t id){return std::erase_if(synthetic->rows,[&](const auto&r){return r.id==id;})!=0;};
        actions.clearUnpinned=[synthetic]{std::erase_if(synthetic->rows,[](const auto&r){return !r.pinned;});return true;};
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.paddingPoints=1;ro.assetRoot=clipboardAssets->root();
        clipboard=std::make_unique<endfield::tools::ClipboardPreview>(rasterizer,ro,std::move(actions),gpu::ClipboardStrings{},gpu::ClipboardAppearance{},clipboardAssets->images(),clipboardAssets->revealSamples());clipboard->resize(metrics);
    }
    if(args.eventLogFixture){
        const auto window=static_cast<HWND>(host.hwnd());
        utility=std::make_unique<app::UtilityExecutor>([window]{need(PostMessageW(window,utilityMessage,serviceGeneration,0)!=FALSE,"Post utility completion");});
        eventOwner=std::make_unique<gpu::EventLogOwner>(args.notesData,gpu::EventLogOwnerCallbacks{now,[&,window]{
            if(!eventRefreshQueued){need(PostMessageW(window,eventChangedMessage,serviceGeneration,0)!=FALSE,"Post Event Log revision");eventRefreshQueued=true;}
        }});
        eventSaves=std::make_unique<app::EventLogSaveQueue>(*eventOwner,*utility);
        for(unsigned n=0;n<32;++n){using K=endfield::modules::EventKind;eventOwner->record({ehud::data::makeUUID(),n%2?K::shelfAdded:K::clipboardCopied,812000000.-n*30,n%2?endfield::modules::EventMetadata{{"filename","Synthetic.pdf"}}:endfield::modules::EventMetadata{{"kind","text"}}});}
        auto actions=eventOwner->callbacks([](double){return std::string("10-08 12:34:56");});
        eventLog=std::make_unique<endfield::tools::EventLogPreview>(rasterizer,gpu::LayerRasterOptions{},std::move(actions),endfield::modules::EventLogStrings{},endfield::modules::EventLogAppearance{},eventOwner->nameCompactor());eventLog->resize(metrics);
    }
    if(args.batteryFixture){
        endfield::modules::BatteryReading value;value.present=true;value.percentage=63;
        endfield::modules::BatteryAppearance appearance;appearance.language=core::Language::simplifiedChinese;
        gpu::LayerRasterOptions options;options.pixelsPerPoint=2;
        battery=std::make_unique<endfield::tools::BatteryPreview>(rasterizer,options,value,appearance,[&]{if(notes)notes->select(core::Module::display,now());});
        battery->resize(metrics);
    }
    if(args.workModeFixture){
        endfield::tools::WorkModePreviewOptions options;
        options.saveWorkSeconds=[](double){}; // This explicit fixture never writes a real personal card.
        workMode=std::make_unique<endfield::tools::WorkModePreview>(static_cast<HWND>(host.hwnd()),rasterizer,notes->activatedTextManager(),notes->textClient(),std::move(options));
        workMode->resize(metrics);workMode->focus(focused,now());
    }
    if(args.volumeFixture){
        auto sample=std::make_shared<gpu::VolumeSnapshot>();
        sample->outputs={{"test-speaker","测试扬声器"},{"test-headphones","测试耳机",true,false}};
        sample->inputs={{"test-microphone","测试麦克风"}};sample->outputID="test-speaker";sample->inputID="test-microphone";
        sample->volume=.45;sample->balance=0;sample->muted=false;
        sample->canSetVolume=sample->canSetMute=sample->canSetBalance=sample->canSetDefaultOutput=sample->canSetDefaultInput=true;
        endfield::tools::VolumePreviewOptions options;options.initial=*sample;
        const auto changed=[&volume,sample]{if(volume)volume->receiveSnapshot(*sample);};
        options.actions.setVolume=[sample,changed](std::string_view id,double value){if(id!=sample->outputID)return false;sample->volume=value;changed();return true;};
        options.actions.setBalance=[sample,changed](std::string_view id,double value){if(id!=sample->outputID)return false;sample->balance=value;changed();return true;};
        options.actions.setMute=[sample,changed](std::string_view id,bool value){if(id!=sample->outputID)return false;sample->muted=value;changed();return true;};
        options.actions.setDefaultOutput=[sample,changed](std::string_view id){if(std::none_of(sample->outputs.begin(),sample->outputs.end(),[&](const auto&d){return d.id==id;}))return false;sample->outputID=id;changed();return true;};
        options.actions.setDefaultInput=[sample,changed](std::string_view id){if(std::none_of(sample->inputs.begin(),sample->inputs.end(),[&](const auto&d){return d.id==id;}))return false;sample->inputID=id;changed();return true;};
        volume=std::make_unique<endfield::tools::VolumePreview>(rasterizer,std::move(options));volume->resize(metrics);
    }
    if(!args.residentIcons.empty()){
        const auto bytes=ehud::data::detail::readFile(args.residentIcons/"manifest.json",512*1024);need(bytes.has_value(),"Missing original icon roster");
        const auto catalog=Json::parse(*bytes);need(catalog["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Icon source differs from this app migration");
        const auto&icons=catalog["icons"].array();const auto icon=std::find_if(icons.begin(),icons.end(),[](const auto&i){return i["id"].string()=="endfield";});need(icon!=icons.end(),"Missing original Endfield icon");
        const auto&art=(*icon)["app"];const auto name=art["file"].string();need(name=="endfield-app.png","Unexpected icon path");
        // The original color app artwork remains visible on both Windows taskbar
        // themes. Adaptive template preference is connected with Display settings.
        applicationIcon=gpu::ApplicationIcon::fromPNG(args.residentIcons/name,art["sha256"].string(),32);
        tray=std::make_unique<gpu::TrayController>(static_cast<HWND>(host.hwnd()),WM_APP+210,RegisterWindowMessageW(L"TaskbarCreated"));
        need(tray->start(static_cast<HICON>(applicationIcon.handle()),L"EndfieldHUD migration preview"),"Windows could not add the preview tray icon");
        const bool shortcut=tray->setHotkey(gpu::TrayHotkey{MOD_CONTROL,VK_OEM_3});
        std::vector<gpu::TrayMenuItem>menu{{gpu::TrayAction::openOverlay,shortcut?L"打开浮层\tCtrl + `":L"打开浮层"}};
        if(workMode)menu.push_back({gpu::TrayAction::workMode,L"工作模式"});menu.push_back({gpu::TrayAction::none,{},false,false,true});menu.push_back({gpu::TrayAction::quit,L"退出 EndfieldHUD"});tray->setMenu(std::move(menu));
        if(!shortcut)std::cout<<"Ctrl + ` is already registered; reopen this isolated preview from its tray icon.\n";
    }
    std::cout<<(notes?"Synthetic shell with rich Notes, File Shelf and isolated Clipboard history; other modules and remaining Notes tools are not connected yet. ESC finishes editing, then animates closing.\n":"Synthetic source-shell feasibility only: no module bodies/providers; font substitutions and explicit source-content variant coverage remain. ESC animates closing.\n");
    if(args.visible){open(start);host.show();refresh(start);host.run();}
    else {
        auto snapshot=[&]{return Snapshot{renderer.stats(),renderer.sourceGraphics().stats(),rasterizer.stats(),session.frameStats(),session.stats()};};Json::Array rows,images;
        if(!args.snapshots.empty()){need(fs::create_directory(args.snapshots),"Cannot create new snapshot directory");ehud::data::detail::validateRoot(args.snapshots);}
        auto saveTarget=[&](const char*name,double time){if(args.snapshots.empty())return;const auto image=renderer.readback();const auto file=std::string(name)+".raw-bgra.bin";
            const std::string bytes(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());
            ehud::data::detail::replaceFile(args.snapshots/file,std::nullopt,bytes,128*1024*1024);
            images.push_back(Json::Object{{"state",name},{"file",file},{"width",int(image.width)},{"height",int(image.height)},{"rowBytes",int(image.rowBytes)},{"bytes",std::int64_t(bytes.size())},{"sha256",packet::sha256(image.pixels)},{"time",time},{"canvasOpacity",canvasOpacity(time)},{"encoding","BGRA8 encoded-sRGB owned target; original blend output retains emissive RGB beyond alpha"}});};
        auto measure=[&](const char*name,unsigned count,auto&&step){const auto before=snapshot();const auto processBefore=processUsage();double cpu=0,maximum=0;for(unsigned i=0;i<count;++i){const auto begin=Clock::now();step(i);const double ms=milliseconds(begin);cpu+=ms;maximum=std::max(maximum,ms);}const auto processAfter=processUsage();auto result=counters(before,snapshot());result["processCPUSeconds"]=processAfter.cpuSeconds-processBefore.cpuSeconds;result["processMemoryBefore"]=memoryJSON(processBefore);result["processMemoryAfter"]=memoryJSON(processAfter);result["name"]=name;result["samples"]=int(count);result["cpuMilliseconds"]=cpu;result["meanCPUMilliseconds"]=cpu/count;result["maxCPUMilliseconds"]=maximum;rows.push_back(std::move(result));};
        auto at=[&](double elapsed){return start+elapsed;};
        open(at(0));measure("opening",61,[&](unsigned i){present(at(i/60.),true);});
        settings.ambientEnabled=false;session.setSettings(settings,at(2));session.pointerMove({},at(2));present(at(3),true);saveTarget("stable",at(3));
        measure("forced-stable-idle",120,[&](unsigned i){present(at(4+i/60.),true);});
        need(!session.demand(at(6)).finiteAnimation&&!session.demand(at(6)).ambientEnabled,"Stable ambient-off session requested animation");
        session.pointerMove(core::Point{-10,20},at(7));present(at(7),true);
        measure("changing-pointer-outside-controls",120,[&](unsigned i){session.pointerMove(core::Point{-10,20.+i*4},at(8+i/60.));present(at(8+i/60.),true);});
        saveTarget("tilted",at(8+119/60.));
        session.pointerMove({},at(11));present(at(12),true);const double scrollBefore=session.scrollPosition();need(session.scrollDirection(1,true,at(12)),"Top-position navigation did not accept downward scroll");
        measure("navigation-scroll",90,[&](unsigned i){present(at(12+i/60.),true);});
        const double scrollAfter=session.scrollPosition();need(scrollAfter<scrollBefore-1e-6,"Navigation scroll benchmark did not move downward");
        close(at(14));measure("closing",31,[&](unsigned i){present(at(14+i/60.),true);});
        measure("concealed",120,[&](unsigned i){need(!session.sample(at(15+i/60.)),"Concealed session emitted geometry");need(!session.demand(at(15+i/60.)).presented,"Concealed session requested presentation");});
        unsigned coverageOpening{},coverageButtons{};Json::Array coverageResults;
        if(args.coverage){
            settings.ambientEnabled=true;session.setSettings(settings,at(20));session.setScrollPosition(1,at(20));open(at(20));
            // Deliberately non-cadence samples: live first paint need not occur
            // at time zero or an exact 60 Hz fraction. No global input is sent.
            for(const double elapsed:{0.,.000001,.00013,.0037,.0091,.0173,.0239,.0371,.0613,.0977,.1331,.1719,.2197,.2713,.3337,.4191,.5373,.6817,.8193,1.}){
                try{present(at(20+elapsed),true);}catch(const std::exception&e){throw std::runtime_error("Hidden irregular opening at "+std::to_string(elapsed)+": "+e.what());}++coverageOpening;
            }
            session.setInputEnabled(true,at(21));environment.pointerLocked=true;session.setEnvironment(environment,at(21));present(at(21.5),true);const auto*sample=session.currentFrame();need(sample!=nullptr,"Coverage has no stable source frame");
            std::vector<std::pair<std::string,core::Point>> points;std::set<std::string,std::less<>> seen;
            for(const auto&hit:sample->sourceFrame->hits){
                if(!session.actions().contains(hit.buttonID)||seen.contains(hit.buttonID))continue;
                const core::Point center{hit.rect.origin[0]+hit.rect.size[0]*.5,hit.rect.origin[1]+hit.rect.size[1]*.5};
                const auto p=source::projectNativePoint({center.x,center.y,0},hit.world,sample->camera.projection*sample->camera.view,{0,0,metrics.width,metrics.height});
                if(!p)continue;const auto winner=sample->sourceFrame->buttonAt(*p,sample->camera.projection*sample->camera.view,{0,0,metrics.width,metrics.height});
                if(winner&&*winner==hit.buttonID){seen.insert(hit.buttonID);points.emplace_back(hit.buttonID,*p);}
            }
            need(!points.empty(),"Coverage found no actual visible source hit targets");
            for(std::size_t index=0;index<points.size();++index){const auto time=at(22+static_cast<double>(index));const auto&[id,point]=points[index];
                try{session.pointerMove(point,time);present(time,true);present(time+.0713,true);present(time+.2501,true);session.pointerDown(point,time+.3);present(time+.30001,true);present(time+.4713,true);(void)session.pointerUp(point,time+.6);present(time+.8001,true);}
                catch(const std::exception&e){throw std::runtime_error("Hidden hovered/pressed source button "+id+": "+e.what());}coverageResults.push_back(Json::Object{{"sourceHitButton",id},{"hoveredAfterRelease",session.hovered()?std::string(*session.hovered()):std::string{}}});++coverageButtons;
            }
            const auto finish=at(23+static_cast<double>(points.size()));session.pointerMove({},finish);close(finish);present(finish+.7,true);
        }
        Json::Array moduleResults;
        if(args.moduleCoverage){
            double time=at(100);open(time);present(time+1,true);time+=2;
            const std::array modules{core::Module::notes,core::Module::fileShelf,core::Module::clipboard,core::Module::volume,core::Module::eventLog,core::Module::workMode,core::Module::power};
            std::optional<gpu::LayerRasterStats> retainedCycle;
            for(unsigned cycle=0;cycle<3;++cycle)for(const auto module:modules){
                std::size_t scrollChanges{},peakEntries{},peakBytes{};
                try{notes->select(module,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);
                    if(module==core::Module::clipboard&&clipboard){
                        need(chromePlan&&chromePlan->projection().center,"Clipboard coverage needs the actual source plane");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const source::DesktopChromeSettings probeSettings{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,module,true};
                        probePlane.update(*chromePlan->projection().center,probeSettings,notes->modulePresentation().current,1);
                        const auto probeCamera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                        const auto target=core::Projection::viewport(probeCamera*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight).project({200,160});need(target.has_value(),"Clipboard scroll target projects");
                        for(unsigned step=0;step<24;++step){const double atTime=time+.7+double(step)*.04;const auto previous=clipboard->state().scrollOffset();need(clipboard->wheel({target->x/metrics.scale,target->y/metrics.scale,step<12?-1.:1.,false,0,3},atTime),"Clipboard consumes the projected wheel input");present(atTime,true);scrollChanges+=clipboard->state().scrollOffset()!=previous;const auto usage=rasterizer.stats();peakEntries=std::max(peakEntries,usage.entries);peakBytes=std::max(peakBytes,usage.resourceBytes);}
                        need(scrollChanges>=12&&clipboard->state().scrollOffset()==0,"Clipboard moves through rows and returns to the top");
                    }
                }
                catch(const std::exception&e){const auto usage=rasterizer.stats();throw std::runtime_error("Combined isolated module "+std::string(core::moduleIdentifier(module))+" entries="+std::to_string(usage.entries)+" bytes="+std::to_string(usage.resourceBytes)+": "+e.what());}
                const auto usage=rasterizer.stats();moduleResults.push_back(Json::Object{{"module",std::string(core::moduleIdentifier(module))},{"cycle",int(cycle)},{"rasterEntries",std::int64_t(usage.entries)},{"rasterBytes",std::int64_t(usage.resourceBytes)},{"scrollChanges",std::int64_t(scrollChanges)},{"scrollPeakEntries",std::int64_t(peakEntries)},{"scrollPeakBytes",std::int64_t(peakBytes)}});
                if(module==modules.back()){if(retainedCycle)need(usage.entries==retainedCycle->entries&&usage.resourceBytes==retainedCycle->resourceBytes,"Repeated combined module cycles retain no extra raster resources");retainedCycle=usage;}time+=2;
            }
            close(time);present(time+.7,true);
        }
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"moduleResults",moduleResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog.variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent.stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent.stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"limitations",Json::Array{args.moduleCoverage?"Seven module owners use synthetic data and hidden generated input; real clipboard/audio providers and desktop backdrop are excluded":"No module bodies, providers, persistence, user input or screen capture; hidden offscreen benchmark excludes the system backdrop","Visible preview uses the native system backdrop with original source fade/default opacity; exact blur/radial appearance is unverified","Fixed synthetic clock strings; original canvas fade does not extend source clip completion","Native caption/icon variants are limited to exact exported action-slot pairs; missing variants reject instead of fabricating artwork","CPU timings include submission/driver stalls; no GPU completion timestamp or frame-rate claim","Forced stable-idle draws are benchmark samples; the actual host schedules no ambient-off idle frames"}}};
        ehud::data::detail::replaceFile(args.report,std::nullopt,report.encode(),8*1024*1024);std::cout<<"Wrote hidden source-shell benchmark report\n";
    }
    if(!args.liveDiagnostics.empty()){
        if(probe.enabled){probe.flush(now());probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);}
        Json report=Json::Object{{"scope","Isolated synthetic visible HUD, actual text service, inclusive CPU stage durations; no document text or desktop capture"},{"phases","0 opening/idle; 1 focused editor; 2 focused editor with controlled tilt; 3 after editing"},{"samples",probe.rows}};
        ehud::data::detail::replaceFile(args.liveDiagnostics,std::nullopt,report.encode(),1024*1024);
    }
    ready=false;stopping=true;tray.reset();if(workMode)workMode->shutdown(now());host.setDeadline({});host.setFrameDemand({});if(mediaPicker)mediaPicker->cancel();if(eventSaves)eventSaves->flush(now());eventSaves.reset();utility.reset();mediaPicker.reset();host.setCursor(nullptr);backdrop.reset();
    if(args.visible)need(backdrop.stats().hostAttributeRestored,"Source preview could not restore its original host-backdrop flag");
    composition.detach(renderer);if(battery){battery->release(renderer);battery.reset();}if(workMode){workMode->release(renderer);workMode.reset();}if(notes){notes->finish();notes->release(renderer);notes.reset();}if(shelf){shelf->release(renderer);shelf.reset();}if(clipboard){clipboard->release(renderer);clipboard.reset();}if(volume){volume->release(renderer);volume.reset();}if(eventLog){eventLog->release(renderer);eventLog.reset();}renderer.reset();host.destroy();if(backdropQueue)backdropQueue->finish();if(pendingShelfReveal)need(SUCCEEDED(gpu::revealShelfReference(std::move(*pendingShelfReveal))),"Cannot reveal Shelf reference in Explorer");return 0;
}catch(const winrt::hresult_error&e){std::cerr<<"Source preview failed: HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
catch(const std::exception&e){std::cerr<<"Source preview failed: "<<e.what()<<'\n';return 1;}}
