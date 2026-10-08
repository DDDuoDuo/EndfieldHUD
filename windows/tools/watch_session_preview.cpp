// Build-only source-shell feasibility tool. Every input is an explicit synthetic
// export/cache. Visible launch is opt-in; benchmark never shows its owned HWND.
#include "app/overlay_host.hpp"
#include "app/source_watch_session.hpp"
#include "native/watch_presentation.hpp"
#include "native/chrome_presentation.hpp"
#include "native/watch_content.hpp"
#include "native/source_cursor.hpp"
#include "native/display_service.hpp"
#include "native/desktop_backdrop.hpp"
#include "core/shell_packet.hpp"
#include "core/watch_runtime_input.hpp"
#include "core/data/file_io.hpp"
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
double now(){static const auto frequency=[] {LARGE_INTEGER value;need(QueryPerformanceFrequency(&value)!=0,"Performance frequency unavailable");return double(value.QuadPart);}();LARGE_INTEGER value;need(QueryPerformanceCounter(&value)!=0,"Performance counter unavailable");return double(value.QuadPart)/frequency;}
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
struct COM {COM(){need(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization failed");}~COM(){CoUninitialize();}};
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
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
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
    bool&ready;app::OverlayHost&host;gpu::Renderer&renderer;gpu::DesktopBackdrop&backdrop;
    ~PreviewWindowLifetime(){ready=false;backdrop.reset();renderer.reset();try{host.destroy();}catch(...) {}}
};
struct Options {fs::path packet,cache,shader,chrome,cursor,report,snapshots,watchBlur;std::string pin;bool visible{},warp{},runtimeInput{},coverage{};std::uint32_t benchmarkWidth{1280},benchmarkHeight{800};double benchmarkEpoch{};};
Options options(int argc,wchar_t**argv){
    need(argc>=5,"Usage: watch_session_preview packet-root compiled-scene hud.hlsl --benchmark new-report.json | --visible --watch-blur original-watch-blur.json [--chrome chrome.json] [--cursor-png original.png] [--compiled-sha sha256] [--snapshots new-directory] [--warp] [--runtime-input] [--benchmark-size width height] [--benchmark-epoch seconds] [--coverage]");
    Options o;o.packet=fs::absolute(argv[1]);o.cache=fs::absolute(argv[2]);o.shader=fs::absolute(argv[3]);
    for(int i=4;i<argc;++i){const std::wstring_view arg=argv[i];
        if(arg==L"--visible"){need(!o.visible,"Duplicate visible mode");o.visible=true;}
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
        else if(arg==L"--compiled-sha"&&i+1<argc){need(o.pin.empty(),"Duplicate cache pin");o.pin=utf8(argv[++i]);}
        else need(false,"Unknown or incomplete preview argument");
    }
    need(o.visible!=!o.report.empty(),"Choose exactly one explicit visible or benchmark mode");need(!o.visible||!o.warp,"WARP is a hidden test mode only");
    need(!o.visible||!o.watchBlur.empty(),"Visible preview requires the explicit source-pinned --watch-blur animation");
    need(!o.visible||(!o.coverage&&o.benchmarkEpoch==0&&o.benchmarkWidth==1280&&o.benchmarkHeight==800),"Coverage/epoch/size overrides are hidden-test only");
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
int wmain(int argc,wchar_t**argv){try{
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
    if(includesChrome){chromePlan=std::make_unique<source::DesktopChromeProjectionPlan>(scene,camera,animation,source::DesktopChromeBindings::fromJson(chromeJSON["bindings"]));chrome=std::make_unique<gpu::NativeChromePresentation>(*chromePlan,layers,chromeJSON,rasterOptions);
        gpu::DesktopChromeContent content;content.reading=source::DesktopClockReading{"12:34:56","WED Oct 7"};content.uppercaseShortcut="CTRL + `";content.localizedClose="Click outside to close";chrome->setContent(content);}startup.mark("chrome-artwork");
    // SourceCursor outlives OverlayHost; the host must release its borrowed
    // cursor before that application-local handle is destroyed.
    metadata=Json{};legacyTop=Json{};legacyBottom=Json{};chromeJSON=Json{};package.reset();if(runtime)runtime->releaseSetupJSON();startup.mark("release-full-reference-json");
    gpu::SourceCursor cursor;if(!args.cursor.empty())cursor=gpu::SourceCursor::fromOriginalPNG(args.cursor);startup.mark("native-cursor");
    std::unique_ptr<BackdropQueue> backdropQueue;
    app::OverlayHost host;gpu::Renderer renderer;gpu::DesktopBackdrop backdrop;app::ClientMetrics metrics;bool ready=false,closing=false,focused=!args.visible;
    PreviewWindowLifetime windowLifetime{ready,host,renderer,backdrop};
    app::WatchSessionEnvironment environment{{1280,800},true,true,true,false,{}};app::WatchSessionSettings settings;session.setSettings(settings,0);session.setEnvironment(environment,0);
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
    auto open=[&](double time){closing=false;canvasOpenedAt=time;session.open(time,0x5eed);};
    auto refresh=[&](double time){if(args.visible){host.setFrameDemand(session.demand(time));host.invalidate();}};
    auto close=[&](double time){if(!closing){canvasCapturedOpacity=canvasOpacity(time);canvasClosedAt=time;closing=true;session.close(time);refresh(time);}};
    auto activate=[&](const app::WatchActivation&event){const auto entries=contentCatalog.entries();const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto&value){return value.action==event.action;});need(entry!=entries.end(),"Source activation exceeds exported actions");std::cout<<"Source action: "<<entry->target<<" (module body is not installed)\n";};
    auto present=[&](double time,bool submit){
        const auto*sample=session.sample(time);updateBackdrop(time);if(!sample)return false;
        if(focused&&sample->visibility.phase==core::VisibilityPhase::visible&&!session.inputEnabled())session.setInputEnabled(true,time);
        auto parameters=materials.parameters();parameters.camera=sample->gpuCamera;parameters.timeSeconds=sample->shaderTime;parameters.width=metrics.pixelWidth;parameters.height=metrics.pixelHeight;
        materialPresentation.update(*sample->sourceFrame,parameters);materials.flush(renderer.sourceGraphics());
        if(nativeContent.update(session.actions()))layers.upload(renderer);
        for(auto&button:available)button.enabled=session.actions().contains(button.buttonID);
        const core::Rect viewport{0,0,metrics.width,metrics.height};labels.update(*sample->sourceFrame,sample->camera,viewport,available);
        // Chrome content is fixed synthetic input; opacity follows the same
        // ready/close timestamps as the source outer controller.
        if(chrome)chrome->update(*sample->sourceFrame,sample->camera,{viewport,settings.hudScale,settings.hudOffset,core::Module::power,true},static_cast<float>(canvasOpacity(time)));
        layers.present(renderer);renderer.setCamera(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale,1));
        if(submit)renderer.draw(args.visible);return true;
    };
    app::OverlayCallbacks callbacks;
    callbacks.resize=[&](const auto&value){metrics=value;if(!ready)return;const auto time=now();environment.viewport={value.width,value.height};environment.onScreen=value.pixelWidth>0&&value.pixelHeight>0;session.setEnvironment(environment,time);if(environment.onScreen)renderer.resize(value.pixelWidth,value.pixelHeight);refresh(time);};
    callbacks.pointer=[&](const app::PointerEvent&e){if(!ready)return false;const auto time=now();const core::Point p{e.x,e.y};
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
    callbacks.wheel=[&](const app::WheelEvent&e){if(!ready||e.horizontal)return false;const auto time=now();const bool handled=session.wheel({e.x,e.y},e.steps,e.linesPerStep,time);if(handled)refresh(time);return handled;};
    callbacks.key=[&](const app::KeyEvent&e){if(ready&&e.kind==app::KeyKind::down&&e.value==VK_ESCAPE){close(now());return true;}return false;};
    callbacks.focus=[&](bool value){focused=value;if(ready){const auto time=now();if(!focused){environment.pointer.reset();session.pointerMove({},time);session.setInputEnabled(false,time);}else if(session.phase()==core::VisibilityPhase::visible)session.setInputEnabled(true,time);refresh(time);}};
    callbacks.closeRequested=[&]{if(ready)close(now());};
    callbacks.frame=[&](double time){if(!ready)return;const bool active=present(time,true);host.setFrameDemand(session.demand(time));if(!active&&session.phase()==core::VisibilityPhase::concealed){host.hide();host.requestStop();}};
    app::OverlayOptions windowOptions{L"EndfieldHUD source shell feasibility — synthetic data",0,0,1280,800,{}};
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
    renderer.initialize(host.hwnd(),metrics.pixelWidth,metrics.pixelHeight,{args.warp?gpu::Driver::warpForTests:gpu::Driver::hardware,args.shader,args.visible?gpu::RenderTarget::composition:gpu::RenderTarget::offscreenForTests});startup.mark("renderer-device-target");const auto deviceInfo=renderer.deviceInfo();
    if(args.visible){
        backdropQueue=std::make_unique<BackdropQueue>();
        const BOOL known=FALSE;const auto result=DwmSetWindowAttribute(static_cast<HWND>(host.hwnd()),DWMWA_USE_HOSTBACKDROPBRUSH,&known,sizeof(known));
        if(FAILED(result))throw winrt::hresult_error(result,L"Source preview: establish owned HWND's disabled original host-backdrop flag");
        backdropState.pixelWidth=metrics.pixelWidth;backdropState.pixelHeight=metrics.pixelHeight;backdrop.initialize(host.hwnd(),backdropState,{false});
    }
    startup.mark("lower-system-backdrop");
    materials.upload(renderer.sourceGraphics());layers.upload(renderer);host.setCursor(cursor.handle());ready=true;startup.mark("initial-gpu-upload");
    environment.viewport={metrics.width,metrics.height};const auto start=args.visible?now():args.benchmarkEpoch;session.setEnvironment(environment,start);const auto preparedMS=milliseconds(preparation);
    std::cout<<"Synthetic source-shell feasibility only: no module bodies/providers; font substitutions and explicit source-content variant coverage remain. ESC animates closing.\n";
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
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog.variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent.stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent.stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"limitations",Json::Array{"No module bodies, providers, persistence, user input or screen capture; hidden offscreen benchmark excludes the system backdrop","Visible preview uses the native system backdrop with original source fade/default opacity; exact blur/radial appearance is unverified","Fixed synthetic clock strings; original canvas fade does not extend source clip completion","Native caption/icon variants are limited to exact exported action-slot pairs; missing variants reject instead of fabricating artwork","CPU timings include submission/driver stalls; no GPU completion timestamp or frame-rate claim","Forced stable-idle draws are benchmark samples; the actual host schedules no ambient-off idle frames"}}};
        ehud::data::detail::replaceFile(args.report,std::nullopt,report.encode(),8*1024*1024);std::cout<<"Wrote hidden source-shell benchmark report\n";
    }
    ready=false;host.setFrameDemand({});host.setCursor(nullptr);backdrop.reset();
    if(args.visible)need(backdrop.stats().hostAttributeRestored,"Source preview could not restore its original host-backdrop flag");
    layers.detach(renderer);renderer.reset();host.destroy();if(backdropQueue)backdropQueue->finish();return 0;
}catch(const winrt::hresult_error&e){std::cerr<<"Source preview failed: HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
catch(const std::exception&e){std::cerr<<"Source preview failed: "<<e.what()<<'\n';return 1;}}
