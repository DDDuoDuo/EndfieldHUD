// EndfieldHUD application owner. The lifecycle, module wiring and event routes
// below were extracted from the former build-only watch_session_preview wmain
// without changing their behavior; the copy-pasted if(owner) chains became the
// ModuleRegistry (app/module_owner.hpp). Production mode adds the Mac
// AppDelegate/OverlayController policy (core/system_overlay_state.hpp), the
// persistent versioned data root, real providers, tray, first run, session-end
// durability and per-module failure isolation.
#include "app/application_impl.hpp"
#include "app/application_persistence.hpp"
#include "core/application_arguments.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <typeinfo>
#include <type_traits>
#include <cwchar>
#include <iostream>
#include <shellapi.h>
#include <wrl/client.h>
#include <psapi.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <wtsapi32.h>

namespace endfield::app {
// ---------------------------------------------------------------------------
// Shared helpers (unchanged from the preview)
void need(bool value,const char*message){if(!value)throw std::runtime_error(message);}
// Generated only inside the isolated module-coverage data root. Exercising the
// real PDF provider with the shared renderer catches lifetime bugs that a TXT
// document or a provider-only test cannot expose.
std::string ownedReaderPDF(){
    const std::array<std::string,2>streams={"1 0 0 rg 0 0 200 300 re f\n","0 0 1 rg 0 0 200 300 re f\n"};
    const auto content=[](const std::string&value){return "<< /Length "+std::to_string(value.size())+" >>\nstream\n"+value+"endstream";};
    const std::array<std::string,6>objects={"<< /Type /Catalog /Pages 2 0 R >>","<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Resources << >> /Contents 4 0 R >>",content(streams[0]),
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Resources << >> /Contents 6 0 R >>",content(streams[1])};
    std::string out="%PDF-1.4\n";std::array<std::size_t,6>offsets{};
    for(std::size_t n=0;n<objects.size();++n){offsets[n]=out.size();out+=std::to_string(n+1)+" 0 obj\n"+objects[n]+"\nendobj\n";}
    const auto xref=out.size();out+="xref\n0 7\n0000000000 65535 f \n";
    for(const auto offset:offsets){char row[32];std::snprintf(row,sizeof(row),"%010llu 00000 n \n",static_cast<unsigned long long>(offset));out+=row;}
    return out+"trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
}
std::string utf8(const fs::path&value){const auto bytes=value.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
fs::path utf8Path(std::string_view value){return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));}
std::string narrow(std::wstring_view value){if(value.empty())return {};const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);need(count>0,"Invalid Windows text");std::string out(count,'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr)==count,"Windows text conversion failed");return out;}
core::Language settingsLanguage(const ehud::data::Settings&value){const auto language=core::languageFromSetting(value.string("language")).value_or(core::Language::system);if(language!=core::Language::system)return language;wchar_t name[LOCALE_NAME_MAX_LENGTH]{};if(!GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH))return core::Language::english;const auto text=narrow(name);const std::array<std::string_view,1>preferred{text};return core::resolveLanguage(preferred);}
double now(){return OverlayHost::clockNow();}
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
namespace {
std::wstring wide(std::string_view value){if(value.empty())return {};const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);need(count>0,"Invalid UTF-8 text");std::wstring out(count,L'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count)==count,"UTF-8 conversion failed");return out;}
Json loadJSON(const fs::path&file){const auto bytes=ehud::data::detail::readFile(file,32*1024*1024);need(bytes.has_value(),"Explicit reference JSON is missing");return Json::parse(*bytes,32*1024*1024);}
gpu::DesktopBackdropAnimation loadWatchBlur(const fs::path&file){
    const auto bytes=ehud::data::detail::readFile(file,1024*1024);need(bytes.has_value(),"Explicit original WatchBlur source is missing");
    const std::span<const std::uint8_t> raw{reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()};
    // Authoritative Mac Resources/WatchSource/Scene/watch-blur.json, unmodified.
    need(packet::sha256(raw)=="87767f77fe5845150e0dc675771cc6adc792075cbbd8fb6d41f405e6db931464","WatchBlur source hash differs from the authoritative Mac resource");
    return gpu::DesktopBackdropAnimation::fromSource(Json::parse(*bytes,1024*1024));
}
class CalendarFixtureProvider final:public gpu::CalendarNotificationProvider {
    std::shared_ptr<CalendarFixtureNotifications>state_;
public:
    explicit CalendarFixtureProvider(std::shared_ptr<CalendarFixtureNotifications>state):state_(std::move(state)){}
    modules::CalendarPermission authorization(bool)override{return state_->permission;}
    std::vector<gpu::CalendarScheduledNotification>pending()override{return state_->pending;}
    void remove(std::string_view id)override{std::erase_if(state_->pending,[id](const auto&v){return v.identifier==id;});}
    void add(const gpu::CalendarScheduledNotification&v)override{remove(v.identifier);state_->pending.push_back(v);}
};
double foundationSeconds(){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()-978307200.;}
}

ApplicationCOM::ApplicationCOM(){need(SUCCEEDED(OleInitialize(nullptr)),"OLE initialization failed");}
ApplicationCOM::~ApplicationCOM(){
    // The outer app owner is last: all workers, documents, media and
    // composition objects have already retired. Drop cached WinRT factories
    // while COM is still usable instead of deferring them to DLL teardown.
    winrt::clear_factory_cache();CoFreeUnusedLibrariesEx(INFINITE,0);OleUninitialize();
}
namespace {
void writeError(const char* bytes,DWORD size)noexcept{DWORD written{};WriteFile(GetStdHandle(STD_ERROR_HANDLE),bytes,size,&written,nullptr);}
LONG CALLBACK reportFault(EXCEPTION_POINTERS* event) {
    if(!event||!event->ExceptionRecord||event->ExceptionRecord->ExceptionCode!=0x87a)return EXCEPTION_CONTINUE_SEARCH;
    constexpr char heading[]="Preview graphics fault 0x87a; native module offsets follow\n";writeError(heading,sizeof(heading)-1);
    void* frames[32]{};const auto count=CaptureStackBackTrace(0,32,frames,nullptr);
    for(USHORT n=0;n<count;++n){HMODULE module{};wchar_t path[MAX_PATH]{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frames[n]),&module)&&GetModuleFileNameW(module,path,MAX_PATH)){
            path[MAX_PATH-1]=0;
            const wchar_t*base=path;for(auto*p=path;*p;++p)if(*p==L'\\')base=p+1;
            char line[MAX_PATH*3+24]{};const auto length=WideCharToMultiByte(CP_UTF8,0,base,-1,line,MAX_PATH*3,nullptr,nullptr);if(length<=0)continue;
            DWORD size=static_cast<DWORD>(length-1);line[size++]='+';line[size++]='0';line[size++]='x';
            const auto offset=reinterpret_cast<std::uintptr_t>(frames[n])-reinterpret_cast<std::uintptr_t>(module);bool leading=true;
            for(int digit=int(sizeof(offset)*2)-1;digit>=0;--digit){const auto value=(offset>>(digit*4))&15;if(!value&&leading&&digit)continue;leading=false;line[size++]="0123456789abcdef"[value];}
            line[size++]='\n';writeError(line,size);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}
ApplicationFaultTrace::ApplicationFaultTrace(bool enabled){
    // Keep this one development-only callback through DLL/process teardown:
    // the observed graphics failure can happen after main has returned.
    if(enabled)AddVectoredExceptionHandler(1,reportFault);
}
BackdropQueue::BackdropQueue(){
    if(winrt::Windows::System::DispatcherQueue::GetForCurrentThread())return;
    DispatcherQueueOptions value{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
    const auto result=CreateDispatcherQueueController(value,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller_)));
    if(FAILED(result))throw winrt::hresult_error(result,L"Source preview: create caller-owned backdrop DispatcherQueue");
}
BackdropQueue::~BackdropQueue(){try{finish();}catch(...) {}}
void BackdropQueue::finish(){
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
ReaderScrollTrace::ReaderScrollTrace(){rows_.reserve(limit);}
void ReaderScrollTrace::record(bool wheel,double time,const tools::ReaderScrollSnapshot&value,double steps,std::uint32_t lines,double cpuMs){
    if(wheel)activeUntil_=time+2;if(!wheel&&time>activeUntil_)return;
    const Row row{time,steps,cpuMs,lines,wheel,value};if(rows_.size()<limit)rows_.push_back(row);else{rows_[cursor_]=row;cursor_=(cursor_+1)%limit;}
}
void ReaderScrollTrace::save(const fs::path&path)const{
    Json::Array samples;samples.reserve(rows_.size());for(std::size_t n=0;n<rows_.size();++n){const auto&r=rows_[(cursor_+n)%rows_.size()];const auto&v=r.value;Json::Array rects,ready;for(unsigned j=0;j<3;++j){const auto&p=v.scene.displayed[j];rects.emplace_back(Json::Array{p.x,p.y,p.width,p.height});ready.emplace_back(v.scene.ready[j]);}
        samples.emplace_back(Json::Object{{"event",r.wheel?"wheel":"frame"},{"time",r.time},{"steps",r.steps},{"lines",std::int64_t(r.lines)},{"frameCpuMs",r.cpuMs},{"stateTime",v.time},{"offset",v.targetOffset},{"zoom",v.zoom},{"panY",v.panY},{"deferred",v.deferred},{"pending",std::int64_t(v.pendingDirection)},{"revision",std::int64_t(v.stateRevision)},{"busy",v.providerBusy},{"epoch",std::int64_t(v.scene.epoch)},{"origin",v.scene.pageOrigin},{"animationStart",v.scene.animationStart},{"animationDuration",v.scene.animationDuration},{"rects",std::move(rects)},{"ready",std::move(ready)}});
    }
    const Json data=Json::Object{{"schema",1},{"numericOnly",true},{"samples",std::move(samples)}};ehud::data::detail::replaceFile(path,std::nullopt,data.encode(),8*1024*1024);
}
ProcessUsage processUsage(){
    FILETIME created,exited,kernel,user;need(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"Process CPU counters unavailable");
    auto seconds=[](FILETIME t){return (double(t.dwHighDateTime)*4294967296.+t.dwLowDateTime)*1e-7;};
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);need(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=0,"Process memory counters unavailable");
    return {seconds(kernel)+seconds(user),std::uint64_t(memory.PrivateUsage),std::uint64_t(memory.WorkingSetSize),std::uint64_t(memory.PeakWorkingSetSize)};
}
Json memoryJSON(ProcessUsage u){return Json::Object{{"privateBytes",std::int64_t(u.privateBytes)},{"workingSetBytes",std::int64_t(u.workingBytes)},{"peakWorkingSetBytes",std::int64_t(u.peakWorkingBytes)}};}
StartupStages::StartupStages():previousTime_(Clock::now()),previousUsage_(processUsage()){}
void StartupStages::mark(const char*name){const auto time=Clock::now();const auto usage=processUsage();
    rows_.push_back(Json::Object{{"stage",name},{"wallMilliseconds",std::chrono::duration<double,std::milli>(time-previousTime_).count()},
        {"processCPUSeconds",usage.cpuSeconds-previousUsage_.cpuSeconds},{"memoryBefore",memoryJSON(previousUsage_)},{"memoryAfter",memoryJSON(usage)}});
    previousTime_=time;previousUsage_=usage;
}
void LiveProbe::flush(double time){if(phase<0)return;Json::Object stages;
    constexpr std::array names{"frame","backdrop","sourcePose","materials","nativeLabels","notesPose","publication","draw","pointer","key","message","editTransition"};
    for(unsigned i=0;i<count;++i){const auto&t=timings[i];stages[names[i]]=Json::Object{{"calls",std::int64_t(t.calls)},{"totalMS",t.total},{"meanMS",t.calls?t.total/t.calls:0},{"maxMS",t.maximum}};}
    const auto input=gpu::textInputDiagnostics();auto timing=[](const auto&t){return Json::Object{{"calls",std::int64_t(t.calls)},{"totalMS",t.totalMilliseconds},{"maxMS",t.maximumMilliseconds}};};
    rows.push_back(Json::Object{{"phase",phase},{"elapsedSeconds",time-phaseStart},{"stages",std::move(stages)},
      {"input",Json::Object{{"notifyLayout",timing(input.notifyLayout)},{"requestLock",timing(input.requestLock)},{"getTextExt",timing(input.getTextExt)},
      {"postAttempts",std::int64_t(input.postAttempts)},{"newPosts",std::int64_t(input.newPosts)},{"layoutOnlyPosts",std::int64_t(input.layoutOnlyPostAttempts)},
      {"placementChanged",std::int64_t(input.placementChanged)},{"placementEqual",std::int64_t(input.placementEqual)},{"locksDuringLayout",std::int64_t(input.locksDuringLayout)},{"extentsDuringLayout",std::int64_t(input.extentsDuringLayout)}}}});
}
void LiveProbe::next(int value,double time){flush(time);phase=value;phaseStart=time;timings={};gpu::setTextInputDiagnosticsEnabled(true);}

// ---------------------------------------------------------------------------
// Construction
Application::Impl::Impl(ApplicationOptions options)
    :args(std::move(options)),faults(args.mode!=ApplicationMode::production&&(args.mode==ApplicationMode::visiblePreview||args.moduleCoverage)),
     headerClock([]{SYSTEMTIME value{};GetLocalTime(&value);return modules::LocalClockFields{value.wMonth,value.wDay,value.wDayOfWeek,value.wHour,value.wMinute,value.wSecond};}),
     registry(args.isolateModuleFailures){
    focused=!visibleMode();
    if(args.readerScrollTrace)readerTrace=std::make_unique<ReaderScrollTrace>();
    probe.enabled=!args.liveDiagnostics.empty();
    // A failed startup still tears owners down in the established order
    // before the HWND, renderer and COM members are destroyed.
    try{construct();}catch(...){try{cleanup();}catch(...){}throw;}
}
Application::Impl::~Impl(){try{cleanup();}catch(...){}}

void Application::Impl::registerModule(std::unique_ptr<ModuleOwner>&owner){registry.add(*owner);}
void Application::Impl::unregisterModule(std::unique_ptr<ModuleOwner>&owner)noexcept{if(owner){registry.remove(*owner);owner.reset();}}

void Application::Impl::construct(){
    if(!args.watchBlur.empty())backdropAnimation=loadWatchBlur(args.watchBlur);startup.mark("original-backdrop-animation");
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
    scene=runtime?&runtime->scene():&*legacyScene;document=runtime?&runtime->document():&*legacyDocument;
    library=runtime?&runtime->library():&*legacyLibrary;camera=runtime?&runtime->camera():&*legacyCamera;
    const auto&resources=runtime?runtime->resources():*legacyResources;const auto&profile=runtime?runtime->profile():legacyProfile;
    const auto&animators=runtime?runtime->animators():legacyAnimators;const auto&desktop=runtime?runtime->desktopSettings():legacyDesktop;
    const auto&transitions=runtime?runtime->controllerTransitions():metadata["controllerTransitions"];
    const auto&top=runtime?runtime->nativeTop():legacyTop;const auto&bottom=runtime?runtime->nativeBottom():legacyBottom;
    const auto&nativeButtons=runtime?runtime->nativeButtons():metadata["mountedDocument"]["buttons"];
    animation=std::make_unique<source::WatchAnimation>(*scene,*library);
    session=std::make_unique<SourceWatchSession>(*scene,*document,*library,*camera,resources,animators,transitions,profile,desktop);startup.mark("session-construction");
    const auto sourceManifestSHA256=runtime?runtime->sourceManifestSHA256():package->manifestSHA256();
    materials=std::make_unique<gpu::SourceScene>(gpu::CompiledSourceScene{args.cache,args.cachePin});
    need(materials->provenance().packetSHA256==sourceManifestSHA256,"Compiled material cache source manifest SHA-256 differs from the input package");
    materialPresentation=std::make_unique<gpu::WatchMaterialPresentation>(*materials);startup.mark("compiled-material-cache");
    if(package)for(const auto&asset:package->metadata()["nativeRasterAssets"].array())(void)package->loadNativeRaster(asset["file"].string());
    startup.mark("native-raster-integrity");rasterOptions.assetRoot=args.packet;
    if(!args.chrome.empty())chromeJSON=loadJSON(args.chrome);else if(runtime)chromeJSON=runtime->chrome();const bool includesChrome=!chromeJSON.isNull();
    layers.load(!includesChrome?top["nativeLayers"]:gpu::NativeChromePresentation::combinedReferenceRoot(top["nativeLayers"],chromeJSON),rasterOptions);startup.mark("initial-native-rasterization");
    labelPlan=std::make_unique<source::NativeLabelPlan>(*scene,source::nativeLabelBindingsFromExport(*scene,nativeButtons,top["nativeLayers"],top["nativeProfileBindings"]));
    const std::array contentSnapshots{gpu::WatchContentSnapshot{&top,1},gpu::WatchContentSnapshot{&bottom,0}};
    contentCatalog=std::make_unique<gpu::WatchContentCatalog>(*scene,*document,labelPlan->bindings(),contentSnapshots);
    const auto&catalogNavigation=contentCatalog->navigation();
    WatchSessionNavigation sessionNavigation{catalogNavigation.fixedActions,catalogNavigation.rightActions,catalogNavigation.managedButtons};
    if(profile)if(const auto action=contentCatalog->actionForTarget("profile")){sessionNavigation.fixedActions[profile->rootID]=*action;for(const auto&id:profile->buttonIDs)sessionNavigation.fixedActions[id]=*action;}
    // HUDSourceWatchView: desktop QuitBtn nodes and close buttons are fixed
    // source buttons with the same clipped hit test and _clickCd cooldown.
    for(const auto&node:scene->nodes()){
        if(!document->component("UIButton",node.id))continue;
        const std::string_view path=node.path;
        if(node.name=="QuitBtn"){quitButtons.insert(node.id);sessionNavigation.fixedActions.try_emplace(node.id,quitAction);}
        else if(path.ends_with("/CloseButtonNode/Btn_BackNode")||path.ends_with("/FullScreenCloseBtn"))sessionNavigation.fixedActions.try_emplace(node.id,closeAction);
    }
    session->setNavigation(std::move(sessionNavigation),0);
    if(args.settingsAssets.empty())nativeContent=std::make_unique<gpu::NativeWatchContent>(*contentCatalog,layers,rasterOptions);
    else{const auto file=args.settingsAssets/"watch-appearance"/"manifest.json";const auto bytes=ehud::data::detail::readFile(file,1024*1024);need(bytes&&packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()))=="1c623124d48d094e8897b89598bc6791a8d369bc51ef8bc87c928ef5915444de","Original desktop appearance pin differs");appearanceTemplates=std::make_unique<gpu::WatchAppearanceTemplates>(Json::parse(*bytes));auto ro=rasterOptions;ro.assetRoot=file.parent_path();nativeAppearance=std::make_unique<gpu::NativeWatchAppearance>(*appearanceTemplates,*scene,labelPlan->bindings(),layers,rasterizer,ro);}startup.mark("native-bindings-content-catalog");
    labels=std::make_unique<gpu::WatchLabelPresentation>(*labelPlan,layers);for(const auto&id:labelPlan->buttonIDs())available.push_back({id,false,false});
    chromeContent.reading=source::DesktopClockReading{"12:34:56","WED Oct 7"};chromeContent.uppercaseShortcut="CTRL + `";chromeContent.localizedClose="Click outside to close";
    if(includesChrome){chromePlan=std::make_unique<source::DesktopChromeProjectionPlan>(*scene,*camera,*animation,source::DesktopChromeBindings::fromJson(chromeJSON["bindings"]));chrome=std::make_unique<gpu::NativeChromePresentation>(*chromePlan,layers,chromeJSON,rasterOptions);
        chrome->setContent(chromeContent);}startup.mark("chrome-artwork");
    metadata=Json{};legacyTop=Json{};legacyBottom=Json{};chromeJSON=Json{};package.reset();if(runtime)runtime->releaseSetupJSON();startup.mark("release-full-reference-json");
    if(!args.cursor.empty())cursor=gpu::SourceCursor::fromOriginalPNG(args.cursor);startup.mark("native-cursor");
    configurationPending=!args.settingsAssets.empty()||!args.storageAssets.empty()||!args.activityAssets.empty()||args.reader||args.calendar||!args.mapGeography.empty();
    configuration.set("launchAtLogin",false);session->setSettings(settings,0);session->setEnvironment(environment,0);
    OverlayOptions windowOptions{!args.windowTitle.empty()?args.windowTitle:!args.dataRoot.empty()?L"EndfieldHUD Notes preview — temporary sample data":L"EndfieldHUD source shell feasibility — synthetic data",0,0,1280,800,{}};
    if(visibleMode()){
        const auto displays=gpu::readConnectedDisplays();POINT pointer{};
        need(GetCursorPos(&pointer)!=FALSE,"Cannot locate the current display");
        const auto selected=gpu::resolveDisplay({},displays,{pointer.x,pointer.y});
        need(selected.has_value(),"No usable display for the source shell preview");
        hudDisplay=displays[*selected].handle;previousApplication=reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
        const auto bounds=displays[*selected].bounds;
        windowOptions.x=bounds.left;windowOptions.y=bounds.top;
        windowOptions.pixelWidth=static_cast<std::uint32_t>(bounds.right-bounds.left);
        windowOptions.pixelHeight=static_cast<std::uint32_t>(bounds.bottom-bounds.top);
    }
    if(!visibleMode()){windowOptions.pixelWidth=args.benchmarkWidth;windowOptions.pixelHeight=args.benchmarkHeight;}
    host.create(windowOptions,callbacks());metrics=host.metrics();startup.mark("hidden-window");
    renderer.initialize(host.hwnd(),metrics.pixelWidth,metrics.pixelHeight,{args.warp?gpu::Driver::warpForTests:gpu::Driver::hardware,args.shader,visibleMode()?gpu::RenderTarget::composition:gpu::RenderTarget::offscreenForTests,!args.dataRoot.empty()});startup.mark("renderer-device-target");
    if(visibleMode()){
        backdropQueue=std::make_unique<BackdropQueue>();
        const BOOL known=FALSE;const auto result=DwmSetWindowAttribute(static_cast<HWND>(host.hwnd()),DWMWA_USE_HOSTBACKDROPBRUSH,&known,sizeof(known));
        if(FAILED(result))throw winrt::hresult_error(result,L"Source preview: establish owned HWND's disabled original host-backdrop flag");
        backdropState.pixelWidth=metrics.pixelWidth;backdropState.pixelHeight=metrics.pixelHeight;backdrop.initialize(host.hwnd(),backdropState,{false});
    }
    startup.mark("lower-system-backdrop");
    // Production isolates each optional module's startup: a failure leaves
    // that module absent (logged without user content) and the HUD running.
    // Development previews keep construction failures fatal. Notes is
    // essential (module selection and the shared text services).
    const auto optional=[&](const char*name,auto&&build){
        if(!production()){build();return;}
        try{build();}
        catch(const std::exception&e){const auto line=std::string("EndfieldHUD module ")+name+" could not start ("+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
    };
    if(!args.notesAssets.empty())constructNotes();
    if(!args.shelfAssets.empty()&&notes)optional("file shelf",[&]{constructShelf();});
    materials->upload(renderer.sourceGraphics());publishNative();host.setCursor(cursor.handle());ready=true;startup.mark("initial-gpu-upload");
    environment.viewport={metrics.width,metrics.height};const auto start=visibleMode()?now():args.benchmarkEpoch;session->setEnvironment(environment,start);
    if(args.nativeClipboard||args.nativeActivity||args.reader||args.calendar||!args.mapGeography.empty()||args.eventLogFixture||args.nativeEventLog||!args.settingsAssets.empty()||!args.archiveAssets.empty()||!args.storageAssets.empty()){
        const auto window=static_cast<HWND>(host.hwnd());utility=std::make_unique<UtilityExecutor>([window]{need(PostMessageW(window,utilityMessage,serviceGeneration,0)!=FALSE,"Post utility completion");});
    }
    if(!args.mapGeography.empty())optional("map",[&]{constructMap();});
    if(args.reader)optional("reader",[&]{constructReader();});
    if(args.calendar&&notes)optional("calendar",[&]{constructCalendar();});
    optional("system services",[&]{constructServices();});
    if(!args.clipboardAssets.empty())optional("clipboard",[&]{constructClipboard();});
    if(!args.archiveAssets.empty()&&notes&&mediaBroker)optional("archive",[&]{constructArchive();});
    if(!args.storageAssets.empty())optional("storage",[&]{constructStorage();});
    if(!args.activityAssets.empty())optional("activity",[&]{constructActivity();});
    if(args.eventLogFixture||args.nativeEventLog)optional("event log",[&]{constructEventLog();});
    if(args.batteryFixture||args.nativeBattery)optional("power",[&]{constructBattery();});
    if((args.workModeFixture||production())&&notes)optional("work mode",[&]{constructWorkMode();});
    if(args.volumeFixture||args.nativeVolume)optional("volume",[&]{constructVolume();});
    if(!args.settingsAssets.empty())optional("settings",[&]{constructSettings();});
    for(auto&[name,factory]:args.modules){
        if(!factory)continue;
        optional(name.c_str(),[&]{
            const ApplicationModuleContext context{rasterizer,host.hwnd(),utility.get(),args.dataRoot,
                notes?static_cast<void*>(notes->activatedTextManager()):nullptr,notes?static_cast<std::uint32_t>(notes->textClient()):0u,metrics,
                []{return now();},[this]{if(ready&&!stopping)host.invalidate();},
                [this](modules::EventKind kind,modules::EventMetadata metadata){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),kind,ehud::data::foundationNow(),std::move(metadata)});},
                [this](std::string_view id)->fs::path{return args.resources?args.resources(id):fs::path{};}};
            auto owner=factory(context);if(!owner)return;
            owner->resize(metrics);registry.add(*owner);externalModules.push_back(std::move(owner));configurationPending=true;
        });
    }
    if(!args.residentIcons.empty())constructTray();
    if(production()){
        // Display-off and session lock/disconnect notifications for suspend.
        const auto window=static_cast<HWND>(host.hwnd());
        displayNotification=RegisterPowerSettingNotification(window,&consoleDisplayState,DEVICE_NOTIFY_WINDOW_HANDLE);
        sessionNotification=WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION)!=FALSE;
    }
    if(production()){std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(length&&length<path.size()){path.resize(length);loginItem=std::make_unique<LoginItemRegistration>(LoginItemLocation::production(fs::path(path)));}}
    if(notes&&chromePlan){quitConfirmation=makeQuitConfirmation(rasterizer,QuitConfirmationCallbacks{
        [this](double time){perform(lifecycle.requestQuit(),time);},[this](double time){refresh(time);}});quitConfirmation->resize(metrics);registry.add(*quitConfirmation);}
    if(!production()){
        std::cout<<(archive?"Synthetic module preview with Archive, rich Notes and shared media; all files remain references in the explicit temporary data root. ESC finishes editing, then animates closing.\n":notes?"Synthetic shell with rich Notes, File Shelf and isolated Clipboard history; other modules and remaining Notes tools are not connected yet. ESC finishes editing, then animates closing.\n":"Synthetic source-shell feasibility only: no module bodies/providers; font substitutions and explicit source-content variant coverage remain. ESC animates closing.\n");
        if(activity)std::cout<<(args.nativeActivity?"Native Activity reads bounded Windows counters on the shared worker; per-app disk/network remain unavailable.\n":"Activity preview uses synthetic app and system readings only.\n");
        if(calendar)std::cout<<"Calendar preview keeps events in the explicit temporary root; notification delivery is injected and never contacts the OS notification center.\n";
        if(storage)std::cout<<(args.moduleCoverage?"Storage coverage uses injected synthetic capacity/details and a no-op settings action.\n":"Development Storage preview: startup-volume capacity is read only while selected; Settings opens only on explicit click; no automatic folder scan.\n");
    }
    startup.mark("all-preview-owners-ready");preparedMS=milliseconds(preparation);
}

void Application::Impl::constructNotes(){
    need(bool(chromePlan),"Notes integration requires the source chrome projection");
    notesAssets=std::make_unique<gpu::NativeNotesControlsAssets>(args.notesAssets,gpu::NativeNotesControlsAssetPins{args.notesAssetsSHA,"ca04f142185c7de40acd8523bdb563195d90a1d1"});
    tools::NotesPreviewMediaOptions sharedNotesMedia;
    if(!args.archiveAssets.empty()||args.projection){
        const gpu::NotesImageRoute route{static_cast<HWND>(host.hwnd()),sharedMediaMessage,serviceGeneration};
        mediaDecoder=std::make_unique<gpu::NativeNotesImageDecoder>([](const gpu::NotesImageRequest&r){return gpu::NotesImageAccess{utf8Path(r.path),r.accessLease};},route);
        mediaImages=std::make_unique<gpu::NativeNotesImagePlayback>(*mediaDecoder);
        mediaVideos=std::make_unique<gpu::NativeNotesVideoPlayback>(renderer,gpu::NotesVideoRoute{route.owner,route.message,route.generation});
        mediaBroker=std::make_unique<gpu::NativeMediaRequestBroker>(*mediaDecoder,*mediaImages,route,mediaVideos.get());
        notesMediaClient=mediaBroker->attachClient();if(!args.archiveAssets.empty())archiveMediaClient=mediaBroker->attachClient();sharedNotesMedia={mediaBroker.get(),notesMediaClient};
    }
    notes=makeNotesOwner<tools::NotesPreview>(production(),static_cast<HWND>(host.hwnd()),rasterizer,args.dataRoot,*notesAssets,visibleMode(),args.notesFormatAssets,std::span<const ehud::data::Note>{},sharedNotesMedia);notes->resize(metrics);
    // OverlayController.lastSystemModule starts at Map. The fresh owner has
    // had no event yet, so its first selection completes before any opening.
    if(production())notes->select(core::Module::map,now()-1);
    mediaPicker=std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{static_cast<HWND>(host.hwnd()),mediaPickerMessage,serviceGeneration},gpu::ShelfPickerLabels{"添加图片/视频","添加","添加所选文件"});
    session->setHitFilter([this](std::string_view,core::Point p){return !registry.covers(p);},0);
    notesModuleOwner=notesModule(*notes);registerModule(notesModuleOwner);
    startup.mark("isolated-notes-owner");
}
void Application::Impl::constructShelf(){
    const auto bytes=ehud::data::detail::readFile(args.shelfMask,128*1024);need(bytes.has_value(),"Missing original Shelf reveal samples");
    auto masks=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),core::SubsectionMaskSampler::assetSHA256);
    shelfAssets=std::make_unique<gpu::NativeShelfAssets>(args.shelfAssets);
    tools::ShelfPreviewOptions shelfOptions{args.shelfDataRoot,visibleMode(),visibleMode(),std::move(masks)};
    if(production())persistentShelfRoot(shelfOptions);
    shelf=std::make_unique<tools::ShelfPreview>(static_cast<HWND>(host.hwnd()),rasterizer,*shelfAssets,std::move(shelfOptions));shelf->resize(metrics);
    shelfModuleOwner=shelfModule(*shelf);registerModule(shelfModuleOwner);
    startup.mark("isolated-shelf-owner");
}
void Application::Impl::constructMap(){
    need(bool(utility),"Map requires the shared utility executor");
    mapStore=std::make_unique<ehud::data::MapStore>(args.dataRoot);
    mapLoadRoute=utility->makeRoute();
    const auto window=static_cast<HWND>(host.hwnd());
    const auto changed=[this,window]{if(ready&&!stopping&&!mapRefreshQueued){need(PostMessageW(window,mapChangedMessage,serviceGeneration,0)!=FALSE,"Post Map revision");mapRefreshQueued=true;}};
    auto painter=std::make_shared<gpu::NativeMapPainter>();
    tools::MapPreviewOptions options;options.initial=mapStore->value();
    options.persistence.commit=[this](const auto&snapshot){mapStore->replace(snapshot);};
    options.persistence.newID=[]{return ehud::data::makeUUID();};
    options.persistence.foundationNow=[]{return foundationSeconds();};
    options.clock=[this]{return visibleMode()?now():mapTime;};options.changed=changed;
    // Source Event Log records only explicit marker-style/reset actions;
    // coordinates, pointer movement and navigation never become history.
    options.pinStyleChanged=[this](modules::MapPinStyle style){if(eventOwner){const char*name=style==modules::MapPinStyle::yellow?"yellow":style==modules::MapPinStyle::green?"green":"player";eventOwner->record({ehud::data::makeUUID(),modules::EventKind::mapPinStyleChanged,foundationSeconds(),{{"style",name}}});}};
    options.recentered=[this]{if(eventOwner)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::mapRecentered,foundationSeconds(),{}});};
    options.paint=[painter](const auto&request,const auto&cancel){return painter->paint(request,cancel);};options.releaseWorkerCaches=[painter]{painter->clear();};
    options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;
    options.player=gpu::loadMapPlayerImages(args.mapPlayerAssets,{"3903dcef9be0a32e24b7d6e5ff06235f107df7ae56b2c0d29351b1f20facc83a","ca04f142185c7de40acd8523bdb563195d90a1d1"});
    map=std::make_unique<tools::MapPreview>(rasterizer,*utility,std::move(options));map->resize(metrics);
    mapModuleOwner=mapModule(*map);registerModule(mapModuleOwner);
    startup.mark("lazy-map-owner");
}
// Decode the immutable bundled geography once on first selection. A full
// queue retries on its next completion, never from an idle timer.
void Application::Impl::requestMapGeography(){
    if(mapLoadRequested||stopping||!map)return;
    const auto root=args.mapGeography;
    auto result=std::make_shared<modules::MapGeography>();
    const auto window=static_cast<HWND>(host.hwnd());
    mapLoadRequested=utility->submit(mapLoadRoute,[root,result]{
        const auto read=[&](const char*name,std::size_t bound,const char*pin){const auto bytes=ehud::data::detail::readFile(root/name,bound);need(bytes.has_value(),"Original Map geography is unavailable");need(packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()))==pin,"Original Map geography pin differs");return *bytes;};
        const auto terrain=read("Terrain.bin",modules::MapTerrain::maximumBytes,"5356727bab11c5f96d69a94658d4f884a2e56b01e89f837670d57660e66635dd");
        const auto countries=read("Countries.bin",modules::MapCountries::maximumBytes,"557e4e92f0fedb7e3f80e067cb0207655c9ecaedd9f5b29d730aa363842d0dd5");
        result->terrain=modules::MapTerrain::decode(std::span(reinterpret_cast<const std::uint8_t*>(terrain.data()),terrain.size()));
        result->countries=modules::MapCountries::decode(std::span(reinterpret_cast<const std::uint8_t*>(countries.data()),countries.size()));
    },[this,result,window](std::exception_ptr error){if(stopping||!map)return;
        bool delivered{};isolate(mapModuleOwner,"geography",[&]{if(error)std::rethrow_exception(error);map->setGeography(result,visibleMode()?now():mapTime);delivered=true;});if(!delivered)return;
        if(ready&&!stopping&&!mapRefreshQueued){need(PostMessageW(window,mapChangedMessage,serviceGeneration,0)!=FALSE,"Post Map revision");mapRefreshQueued=true;}});
}
void Application::Impl::constructReader(){
    need(bool(utility),"Reader requires the shared utility executor");
    const auto window=static_cast<HWND>(host.hwnd());
    const auto changed=[this,window]{if(ready&&!stopping&&!readerRefreshQueued){need(PostMessageW(window,readerChangedMessage,serviceGeneration,0)!=FALSE,"Post Reader revision");readerRefreshQueued=true;}};
    readerOwner=std::make_unique<gpu::ReaderOwner>(args.dataRoot/"Reader",*utility,rasterizer.retainedFontResources(),
        [this,changed](std::uint64_t generation,std::optional<modules::ReaderBook>book,std::exception_ptr error){
            if(stopping||!reader||!reader->importRequestCurrent(generation))return;const auto time=visibleMode()?now():readerTime;
            if(error||!book)reader->receiveImportError(generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
            else reader->receiveImport(generation,std::move(*book),time);changed();
        },changed);
    tools::ReaderPreviewOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.newID=[] {return ehud::data::makeUUID();};
    options.strings=readerStrings(watchAppearance.language);options.fontFamilies=[this]{return rasterizer.installedFontFamilies();};
    options.shelfChoices=[this]{std::vector<modules::ReaderMenuChoice>choices;if(shelf)for(const auto&item:shelf->state().items()){
        if(!item.isDirectory&&!item.availabilityError&&modules::readerSupportedExtension(utf8(utf8Path(item.lastKnownPath).extension())))choices.push_back({item.id,item.name});
    }return choices;};
    reader=std::make_unique<tools::ReaderPreview>(readerOwner->state(),rasterizer,std::move(options));reader->resize(metrics);
    readerModuleOwner=readerModule(*reader,*readerOwner,rasterizer,[this](const WheelEvent&e,double time){if(readerTrace)readerTrace->record(true,time,reader->scrollSnapshot(),e.steps,e.linesPerStep);});
    registerModule(readerModuleOwner);
    startup.mark("lazy-reader-owner");
}
void Application::Impl::constructCalendar(){
    need(bool(utility)&&bool(notes),"Calendar needs the shared module owner and executor");
    const auto start=visibleMode()?now():args.benchmarkEpoch;
    const auto window=static_cast<HWND>(host.hwnd());calendarRoute=utility->makeRoute();calendarTime=start;
    calendarCivil=visibleMode()?std::make_unique<gpu::CalendarCivilContext>():std::make_unique<gpu::CalendarCivilContext>(u"UTC","en_US","en_US");
    const auto text=gpu::nativeCalendarTextRules();calendarRepository=std::make_shared<modules::CalendarJSONRepository>(args.dataRoot/"Calendar",text);
    gpu::CalendarNotificationOptions notifications;notifications.language=watchAppearance.language;
    if(production()){
        // The installer's established identity is required before the real OS
        // notification center is contacted; without it reminders are reported
        // unavailable rather than faked.
        calendarNotifications=std::make_unique<gpu::NativeCalendarNotifications>(*utility,std::move(notifications));
    }else{
        calendarFixture=std::make_shared<CalendarFixtureNotifications>();if(!visibleMode())calendarFixture->permission=modules::CalendarPermission::authorized;
        calendarNotifications=std::make_unique<gpu::NativeCalendarNotifications>(*utility,std::move(notifications),[fixture=calendarFixture](const auto&){return std::make_unique<CalendarFixtureProvider>(fixture);});
    }
    modules::CalendarExecutor executor{[this](auto work,auto complete){return utility->submit(calendarRoute,std::move(work),std::move(complete));}};
    modules::CalendarStateOptions state;state.text=text;state.now=[this]{return visibleMode()?ehud::data::foundationNow():*modules::calendarUTC().timestamp({2026,10,4},10,0)+calendarTime;};
    state.zone=[this]{return calendarCivil->timeZone();};state.newID=[]{return ehud::data::makeUUID();};state.scheduling=calendarNotifications->scheduling();
    state.changed=[this,window]{if(ready&&!stopping&&!calendarRefreshQueued){need(PostMessageW(window,calendarChangedMessage,serviceGeneration,0)!=FALSE,"Post Calendar revision");calendarRefreshQueued=true;}};
    state.event=[this](std::string_view action){if(eventOwner)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::calendarAction,ehud::data::foundationNow(),{{"action",std::string(action)}}});};
    calendarState=std::make_unique<modules::CalendarState>(calendarRepository,std::move(executor),std::move(state));
    tools::CalendarPreviewOptions options;options.text=text;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;
    calendar=std::make_unique<tools::CalendarPreview>(window,*calendarState,*calendarCivil,rasterizer,std::move(options),notes->activatedTextManager(),notes->textClient());calendar->resize(metrics);calendar->focus(focused,start);calendarState->startIfExisting();
    CalendarModuleHooks hooks;
    hooks.languageChanged=[this](core::Language language){if(calendarLanguage!=language){calendarLanguage=language;calendarNotifications->setLanguage(calendarLanguage);calendarState->refreshForSystemChange();updateCalendarWake();}};
    hooks.afterUpdate=[this]{if(calendarWakeActive!=calendarState->active())updateCalendarWake();};
    hooks.afterClosing=[this]{updateCalendarWake();};
    hooks.flush=[this]{return flushCalendar();};
    calendarModuleOwner=calendarModule(*calendar,std::move(hooks));registerModule(calendarModuleOwner);
    startup.mark("isolated-calendar-owner");
}
void Application::Impl::constructServices(){
    if(!args.nativeClipboard&&!args.nativeBattery&&!args.nativeVolume)return;
    // One app-lifetime SystemServices: AC/battery power notifications and the
    // clipboard format listener are event-driven; audio listeners exist only
    // while a Volume vote is active. Hidden tests never construct this.
    const auto window=static_cast<HWND>(host.hwnd());systemServices=std::make_unique<gpu::SystemServices>();
    const auto status=systemServices->start(window,systemServiceMessage,[this,window](gpu::ServiceChange event){
        if(stopping)return;
        if(event==gpu::ServiceChange::clipboard){clipboardDirty=true;
            if(ready&&!clipboardRefreshQueued&&session->phase()!=core::VisibilityPhase::concealed&&notes&&notes->selected()==core::Module::clipboard){
                need(PostMessageW(window,clipboardChangedMessage,serviceGeneration,0)!=FALSE,"Post Clipboard revision");clipboardRefreshQueued=true;
            }}
        if(event==gpu::ServiceChange::battery&&systemServices){if(battery&&battery->receive(systemServices->battery())&&ready)host.invalidate();recordBattery();updateTrayMenu();}
        if(event==gpu::ServiceChange::audio&&volumeProvider){volumeProvider->receive(event);}
    },false,args.nativeClipboard?utility.get():nullptr);
    need(SUCCEEDED(status),"Cannot start explicit Windows service preview");
    if(args.nativeClipboard)clipboardProvider=std::make_unique<gpu::NativeClipboardProvider>(gpu::clipboardProviderSource(*systemServices,[this](std::int32_t status)->std::optional<std::string>{
        if(status>=0)return {};return core::localized("Clipboard unavailable","剪贴板不可用",watchAppearance.language);
    }));
}
void Application::Impl::constructClipboard(){
    clipboardAssets=std::make_unique<gpu::NativeClipboardAssets>(args.clipboardAssets);
    gpu::ClipboardActions actions;
    if(clipboardProvider)actions=clipboardProvider->actions();else{
    auto synthetic=std::make_shared<gpu::ClipboardSnapshot>();synthetic->capacity=20;
    for(unsigned n=0;n<12;++n)synthetic->rows.push_back({n+1,n==0,gpu::ClipboardKind::text,"临时剪贴板测试 · "+std::to_string(n+1)+" · EndfieldHUD",{}});
    actions.snapshot=[synthetic]{return *synthetic;};
    actions.copy=[synthetic](std::uint64_t id){return std::any_of(synthetic->rows.begin(),synthetic->rows.end(),[&](const auto&r){return r.id==id;});};
    actions.togglePin=[synthetic](std::uint64_t id){for(auto&r:synthetic->rows)if(r.id==id){r.pinned=!r.pinned;return true;}return false;};
    actions.remove=[synthetic](std::uint64_t id){return std::erase_if(synthetic->rows,[&](const auto&r){return r.id==id;})!=0;};
    actions.clearUnpinned=[synthetic]{std::erase_if(synthetic->rows,[](const auto&r){return !r.pinned;});return true;};
    }
    gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.paddingPoints=1;ro.assetRoot=clipboardAssets->root();if(clipboardProvider)ro.memoryImages=&clipboardProvider->images();
    clipboard=std::make_unique<tools::ClipboardPreview>(rasterizer,ro,std::move(actions),gpu::ClipboardStrings{},gpu::ClipboardAppearance{},clipboardAssets->images(),clipboardAssets->revealSamples());clipboard->resize(metrics);
    clipboardModuleOwner=clipboardModule(*clipboard,*clipboardAssets);registerModule(clipboardModuleOwner);
}
void Application::Impl::constructArchive(){
    const auto window=static_cast<HWND>(host.hwnd());const auto start=visibleMode()?now():args.benchmarkEpoch;
    const auto changed=[this,window]{if(ready&&!stopping&&!archiveRefreshQueued){need(PostMessageW(window,archiveChangedMessage,serviceGeneration,0)!=FALSE,"Post Archive revision");archiveRefreshQueued=true;}};
    archiveService=std::make_unique<ArchiveService>(args.dataRoot/"Archive",*utility,gpu::nativeArchiveTextRules(),changed);
    archiveDates=std::make_shared<gpu::ArchiveDateFormatter>();
    const modules::ArchiveAppearance appearance{true,2,{250./255.,212./255.,31./255.,1},modules::ArchiveColor{1,159./255.,10./255.,1}};
    auto options=tools::makeArchivePreviewOptions(args.archiveAssets,rasterizer,core::Language::simplifiedChinese,appearance,archiveDates);options.raster.memoryImages=&archiveImages;
    archive=std::make_unique<tools::ArchivePreview>(archiveService->state(),window,rasterizer,notes->activatedTextManager(),notes->textClient(),std::move(options));archive->resize(metrics);archive->focus(focused,start);
    tools::ArchiveMediaBindingOptions media;
    media.resolve=[](const Json&ref){need(ref["referencePlatform"]==Json("windows"),"Imported Mac media needs explicit relinking on Windows");const auto&path=ref["windowsPath"].string();need(ehud::data::validWindowsFilePath(path),"Invalid Archive Windows reference");return gpu::NotesImageAccess{utf8Path(path),{}};};
    media.errorText=[this](HRESULT){return core::localized("Media unavailable","媒体不可用",watchAppearance.language);};
    media.loadThumbnail=[this](std::string id,auto completion){archiveService->state().loadThumbnail(std::move(id),std::move(completion));};media.changed=changed;
    archiveMedia=std::make_unique<tools::ArchiveMediaBinding>(*archive,*mediaBroker,archiveMediaClient,renderer,archiveImages,std::move(media));
    archiveModuleOwner=archiveModule(*archive,archiveService.get(),archiveMedia.get());registerModule(archiveModuleOwner);
    startup.mark("shared-media-archive-owner");
}
void Application::Impl::constructStorage(){
    const auto window=static_cast<HWND>(host.hwnd());
    auto options=tools::makeStoragePreviewOptions(args.storageAssets,*utility,[window]{
        // Explicit original Storage Settings action only; never startup or a test.
        const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",L"ms-settings:storagesense",nullptr,nullptr,SW_SHOWNORMAL));
        if(result<=32)std::cerr<<"Windows Storage Settings could not open: "<<result<<'\n';
    });
    if(args.moduleCoverage){
        // Replace ALL OS providers before constructing the owner.
        storageFixture=std::make_shared<StorageFixture>();storageFixture->time=args.benchmarkEpoch;const auto fixture=storageFixture;
        options.readCapacity=[fixture]()->std::optional<modules::StorageCapacity>{++fixture->capacityReads;return modules::StorageCapacity{"Synthetic startup volume",1000000000000LL,375000000000LL,fixture->time.load(std::memory_order_relaxed)};};
        options.scanDetails=[fixture](const modules::StorageScanCancellation&cancel){++fixture->detailScans;modules::StorageDetailsSnapshot result;if(!cancel.cancelled()){result.categories={{"documents","Synthetic Documents",123000000LL,false}};result.updatedAt=fixture->time.load(std::memory_order_relaxed);}return result;};
        options.completionClock=[fixture]{return fixture->time.load(std::memory_order_relaxed);};
        options.openSettings=[fixture]{++fixture->settingsRequests;};
    }
    storage=std::make_unique<tools::StoragePreview>(rasterizer,std::move(options));storage->resize(metrics);
    storageModuleOwner=storageModule(*storage,StorageModuleHooks{
        [this]{return storageFixture?storageFixture->time.load(std::memory_order_relaxed):now();},
        [this]{return !closing&&session->phase()!=core::VisibilityPhase::concealed;}});
    registerModule(storageModuleOwner);
    startup.mark(args.moduleCoverage?"synthetic-storage-owner":"development-storage-capacity-owner");
}
void Application::Impl::constructActivity(){
    // Hidden coverage and ordinary isolated preview keep injected data.
    gpu::NativeActivityAssets assets(args.activityAssets);
    tools::ActivityPreviewOptions options;options.sortSamples=assets.sortSamples();
    options.appearance.language=core::Language::simplifiedChinese;
    options.compareNames=[](std::string_view a,std::string_view b){return a.compare(b);};
    if(!args.nativeActivity){
        options.initial.timestamp=1;options.initial.uptime=1;options.initial.cpuPercent=12;
        options.initial.memory=modules::ActivityMemory{4000000000ULL,16000000000ULL,{}};
        options.initial.download=400000;options.initial.upload=12000;
        options.initial.diskRead=150000;options.initial.diskWrite=34000;
        for(unsigned n=0;n<18;++n){modules::ActivityApp item;
            item.identity={"synthetic-"+std::to_string(n),"Synthetic App "+std::to_string(n+1),"",{100+n}};
            item.cpuPercent=double(18-n);item.memoryBytes=200000000ULL+n*10000000ULL;
            options.apps.items.push_back(std::move(item));}
    }else{
        options.compareNames=gpu::compareWindowsActivityNames;
        options.demandChanged=[this](bool visible,bool apps,double time){
            if(stopping)return;const bool wasApps=activityPlan.appsActive();activityPlan.setVisible(visible,false,apps,time);
            if(activityCatalog&&wasApps!=activityPlan.appsActive())activityCatalog->invalidate();
            if(activityProbe)activityProbe->update(time);scheduleDeadline();
        };
    }
    activity=std::make_unique<tools::ActivityPreview>(rasterizer,std::move(options));activity->resize(metrics);
    if(args.nativeActivity){
        activityCatalog=std::make_shared<gpu::ActivityCatalogSampler>(gpu::windowsActivityCatalogReaders());
        activityDisk=std::make_shared<gpu::WindowsActivityDisk>();
        const auto sampleClock=[]{const auto stamp=foundationSeconds();return std::pair{stamp,double(GetTickCount64())*.001};};
        gpu::ActivityProbe::Readers readers;
        readers.system=[disk=activityDisk,sampleClock]{const auto [stamp,uptime]=sampleClock();auto result=gpu::readWindowsActivity(stamp,uptime);result.disk=disk->sample();return result;};
        readers.apps=[catalog=activityCatalog,sampleClock]{const auto [stamp,uptime]=sampleClock();return catalog->sample(stamp,uptime);};
        activityProbe=std::make_unique<gpu::ActivityProbe>(activity->providerState(),activityPlan,*utility,std::move(readers));
    }
    activityModuleOwner=activityModule(*activity);registerModule(activityModuleOwner);
    startup.mark(args.nativeActivity?"native-activity-owner":"synthetic-activity-owner");
}
void Application::Impl::constructEventLog(){
    const auto window=static_cast<HWND>(host.hwnd());
    eventOwner=std::make_unique<gpu::EventLogOwner>(args.dataRoot,gpu::EventLogOwnerCallbacks{now,[this,window]{
        if(!eventRefreshQueued){need(PostMessageW(window,eventChangedMessage,serviceGeneration,0)!=FALSE,"Post Event Log revision");eventRefreshQueued=true;}
    }});
    eventSaves=std::make_unique<EventLogSaveQueue>(*eventOwner,*utility);
    if(args.eventLogFixture)for(unsigned n=0;n<32;++n){using K=modules::EventKind;eventOwner->record({ehud::data::makeUUID(),n%2?K::shelfAdded:K::clipboardCopied,812000000.-n*30,n%2?modules::EventMetadata{{"filename","Synthetic.pdf"}}:modules::EventMetadata{{"kind","text"}}});}
    if(production()){
        // Baselines only: the first battery/display/configuration/work values
        // invent no event (SystemEventRecorder).
        eventRecorder=std::make_unique<core::ApplicationEventRecorder>([this](modules::EventKind kind,modules::EventMetadata metadata){
            if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),kind,ehud::data::foundationNow(),std::move(metadata)});});
        recordDisplays();recordBattery();
    }
    auto actions=eventOwner->callbacks([](double){return std::string("10-08 12:34:56");});
    eventLog=std::make_unique<tools::EventLogPreview>(rasterizer,gpu::LayerRasterOptions{},std::move(actions),modules::EventLogStrings{},modules::EventLogAppearance{},eventOwner->nameCompactor());eventLog->resize(metrics);
    eventLogModuleOwner=eventLogModule(*eventLog);registerModule(eventLogModuleOwner);
}
void Application::Impl::constructBattery(){
    modules::BatteryReading value;value.present=true;value.percentage=63;
    modules::BatteryAppearance appearance;appearance.language=core::Language::simplifiedChinese;
    gpu::LayerRasterOptions options;options.pixelsPerPoint=2;
    if(args.nativeBattery&&!args.batteryFixture)value={};
    battery=std::make_unique<tools::BatteryPreview>(rasterizer,options,value,appearance,[this]{selectModule(core::Module::display,now());});
    if(args.nativeBattery&&systemServices)battery->receive(systemServices->battery());
    battery->resize(metrics);
    batteryModuleOwner=batteryModule(*battery);registerModule(batteryModuleOwner);
}
void Application::Impl::constructWorkMode(){
    tools::WorkModePreviewOptions options;
    if(production()&&utility){
        // Lifetime hours belong to the personal profile. Each absolute
        // checkpoint rereads the current record on the shared executor so a
        // concurrent profile edit is never overwritten from a stale copy.
        try{options.restoredWorkSeconds=ehud::data::ProfileStore(args.dataRoot).value().accumulatedWorkSeconds;}catch(const std::exception&){}
        const auto root=args.dataRoot;const auto route=utility->makeRoute();
        options.saveWorkSeconds=[this,root,route](double seconds){if(stopping||!utility)return;(void)utility->submit(route,[root,seconds]{ehud::data::ProfileStore store(root);auto value=store.value();if(value.accumulatedWorkSeconds==seconds)return;value.accumulatedWorkSeconds=seconds;store.update(value);},[](std::exception_ptr){});};
    }else options.saveWorkSeconds=[](double){}; // This explicit fixture never writes a real personal card.
    workMode=std::make_unique<tools::WorkModePreview>(static_cast<HWND>(host.hwnd()),rasterizer,notes->activatedTextManager(),notes->textClient(),std::move(options));
    workMode->resize(metrics);workMode->focus(focused,now());
    workModeModuleOwner=workModeModule(*workMode);registerModule(workModeModuleOwner);
}
void Application::Impl::constructVolume(){
    tools::VolumePreviewOptions options;
    if(args.nativeVolume&&systemServices){
        // The binding's vote activates SystemServices audio listeners only while
        // Volume is visible; nothing switches the Windows default device.
        // Volume is the only audio-topology voter today; other owners that
        // need audio observation must aggregate their votes here.
        auto access=gpu::volumeProviderAccess(*systemServices,[this](bool active)->std::int32_t{
            if(!systemServices)return E_FAIL;audioVotes=active?1u:0u;return systemServices->set_audio_active(audioVotes>0);});
        volumeProvider=std::make_unique<gpu::VolumeProviderBinding>(std::move(access),[this](gpu::VolumeProviderFailure){return core::localized("Unavailable","不可用",watchAppearance.language);});
        options.initial=volumeProvider->snapshot();options.actions=volumeProvider->callbacks();
        volume=std::make_unique<tools::VolumePreview>(rasterizer,std::move(options));
        volumeProvider->setReceiver([this](const gpu::VolumeSnapshot&value){if(volume)volume->receiveSnapshot(value);if(ready&&!stopping)host.invalidate();});
    }else{
        auto sample=std::make_shared<gpu::VolumeSnapshot>();
        sample->outputs={{"test-speaker","测试扬声器"},{"test-headphones","测试耳机",true,false}};
        sample->inputs={{"test-microphone","测试麦克风"}};sample->outputID="test-speaker";sample->inputID="test-microphone";
        sample->volume=.45;sample->balance=0;sample->muted=false;
        sample->canSetVolume=sample->canSetMute=sample->canSetBalance=sample->canSetDefaultOutput=sample->canSetDefaultInput=true;
        options.initial=*sample;
        const auto changed=[this,sample]{if(volume)volume->receiveSnapshot(*sample);};
        options.actions.setVolume=[sample,changed](std::string_view id,double value){if(id!=sample->outputID)return false;sample->volume=value;changed();return true;};
        options.actions.setBalance=[sample,changed](std::string_view id,double value){if(id!=sample->outputID)return false;sample->balance=value;changed();return true;};
        options.actions.setMute=[sample,changed](std::string_view id,bool value){if(id!=sample->outputID)return false;sample->muted=value;changed();return true;};
        options.actions.setDefaultOutput=[sample,changed](std::string_view id){if(std::none_of(sample->outputs.begin(),sample->outputs.end(),[&](const auto&d){return d.id==id;}))return false;sample->outputID=id;changed();return true;};
        options.actions.setDefaultInput=[sample,changed](std::string_view id){if(std::none_of(sample->inputs.begin(),sample->inputs.end(),[&](const auto&d){return d.id==id;}))return false;sample->inputID=id;changed();return true;};
        volume=std::make_unique<tools::VolumePreview>(rasterizer,std::move(options));
    }
    volume->resize(metrics);
    volumeModuleOwner=volumeModule(*volume);registerModule(volumeModuleOwner);
}
void Application::Impl::constructSettings(){
    if(args.moduleCoverage&&!args.orbipomAssets.empty()){
        // This explicit hidden path owns a parser-validated NEW data root.
        // Seed a nonzero original preference to detect accidental reset loss.
        ehud::data::SettingsStore fixture(args.dataRoot);auto value=fixture.value();value.set("orbipom.bestScore.v1",std::int64_t{71});fixture.update(value);
    }
    settingsSaves=std::make_unique<SettingsSaveQueue>(args.dataRoot,*utility,SettingsSaveCallbacks{[this](const ehud::data::Settings&loaded){
        if(!production()){createSettings(loaded);return;}
        // A Settings UI failure must not cancel first-run launch or the HUD.
        try{createSettings(loaded);}catch(const std::exception&e){
            const auto line=std::string("EndfieldHUD module settings could not start (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
            settingsLoaded=true;if(launchPending){launchPending=false;perform(lifecycle.launch(args.startup,settingsSaves->status().launched,!args.persistLaunchMarker),now());}
        }},[this]{refresh(now());}});settingsSaves->start();
    // Hidden integration owns no frame/message loop. This explicit test
    // preparation barrier drains the same asynchronous read before input.
    if(!visibleMode()){utility->waitIdle();utility->drain();need(settingsSaves->status().loaded,"Hidden Settings initial read failed");}
}
void Application::Impl::createSettings(const ehud::data::Settings&loaded){
    if(stopping)return;configuration=loaded;if(!production())configuration.set("launchAtLogin",false);configurationPending=true;
    if(!args.orbipomAssets.empty()){
        const auto&saved=loaded.fields["orbipom.bestScore.v1"];if(saved.isNumber())gameBest=std::max<std::int64_t>(0,saved.integer());
        gameEnabled=true; // First selection prepares original art; only Start creates the VM.
    }
    tools::SettingsPreviewOptions options;options.initial=configuration;options.language=settingsLanguage(configuration);
    const auto iconRoot=args.settingsAssets/"application-icons";const auto catalog=loadJSON(iconRoot/"manifest.json");
    need(catalog["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Settings icon source differs");
    for(const auto&icon:catalog["icons"].array())if(icon["offered"].boolean()){options.icons.push_back({icon["id"].string(),icon["title"].string()});options.images.emplace(icon["id"].string(),Json::Object{{"asset",icon["app"]["file"]},{"sha256",icon["app"]["sha256"]}});}
    options.raster.assetRoot=iconRoot;
    options.viewHooks.displays=[](){std::vector<modules::SettingsDisplay>result;for(const auto&d:gpu::readConnectedDisplays()){const auto id=narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.persistentID.data()),d.persistentID.size()));const auto name=narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.name.data()),d.name.size()));if(!id.empty())result.push_back({id,name,std::to_string(d.bounds.right-d.bounds.left)+" × "+std::to_string(d.bounds.bottom-d.bounds.top)});}return result;};
    options.callbacks.configurationChanged=[this](const auto&value){configuration=value;if(!args.orbipomAssets.empty())configuration.set("orbipom.bestScore.v1",gameBest);configurationPending=true;};
    options.callbacks.persist=[this](const auto&value){auto committed=value;if(!args.orbipomAssets.empty())committed.set("orbipom.bestScore.v1",gameBest);settingsSaves->save(committed);};
    options.callbacks.changed=[this]{if(ready&&!stopping)host.invalidate();};
    options.callbacks.registerShortcut=[this](auto value)->std::optional<std::string>{if(tray&&tray->setHotkey(gpu::TrayHotkey{value.modifiers,value.key})){updateTrayMenu();return {};}return core::localized("Shortcut unavailable","快捷键不可用",settingsLanguage(configuration));};
    options.callbacks.shortcutCapture=[this](bool capture){if(!tray)return;const auto key=modules::settingsShortcut(configuration);if(capture)tray->setHotkey({});else tray->setHotkey(gpu::TrayHotkey{key.modifiers,key.key});};
    options.callbacks.launchAtLogin=[this](bool enabled)->std::optional<std::string>{
        if(!loginItem)return "Startup registration is unavailable in this isolated preview";
        // Transactional: an error is returned before the preference commits.
        auto error=loginItem->setEnabled(enabled,settingsLanguage(configuration));refreshExternalStatus();return error;};
    options.callbacks.editBatteryPosition=[this]{selectModule(core::Module::power,now());};
    options.about.repository="https://github.com/DDDuoDuo/EndfieldHUD";
    settingsUI=std::make_unique<tools::SettingsPreview>(rasterizer,std::move(options));settingsUI->resize(metrics);settingsUI->setOverlayVisible(session->phase()!=core::VisibilityPhase::concealed,visibleMode()?now():args.benchmarkEpoch);
    settingsModuleOwner=settingsModule(*settingsUI);registerModule(settingsModuleOwner);
    if(loginItem){
        // LoginItemManager.start(ensureEnabled:): only the saved preference
        // registers; a user-disabled startup entry is reported, never forced.
        std::optional<std::string>error;if(configuration.boolean("launchAtLogin"))error=loginItem->ensureEnabled(settingsLanguage(configuration));
        refreshExternalStatus();if(error){loginStatus=*error;settingsUI->controller().setExternalStatus(loginStatus,shortcutStatus);}
    }
    // The quit card stays the topmost modal layer above the Settings modal.
    if(quitConfirmation){registry.remove(*quitConfirmation);registry.add(*quitConfirmation);}
    settingsLoaded=true;
    // A closed HUD presents no frame, so apply the loaded record now: the
    // saved summon shortcut, language and tray labels must not wait for the
    // first opening (e.g. a --login launch that stays closed).
    if(production())applyConfiguration(now());
    if(production()&&launchPending){
        launchPending=false;
        // AppDelegate first-run onboarding, after the preference record and its
        // launch marker are known. --login/--no-onboarding suppress it.
        perform(lifecycle.launch(args.startup,settingsSaves->status().launched,!args.persistLaunchMarker),now());
    }
}
void Application::Impl::ensureGame(double time){
    if(game||stopping||!gameEnabled)return;
    modules::OrbiPomSessionCallbacks callbacks;
    callbacks.makeRuntime=[root=args.orbipomAssets]{return std::make_unique<modules::OrbiPomRuntime>(modules::OrbiPomRuntimeOptions{root});};
    callbacks.saveBest=[this](std::int64_t best){gameBest=std::max(gameBest,best);configuration.set("orbipom.bestScore.v1",gameBest);if(settingsSaves){auto committed=settingsUI?settingsUI->controller().committed():configuration;committed.set("orbipom.bestScore.v1",gameBest);settingsSaves->save(committed);}};
    callbacks.event=[this](modules::OrbiPomEvent event){if(eventOwner){const char*action=event==modules::OrbiPomEvent::started?"started":event==modules::OrbiPomEvent::restarted?"restarted":"finished";eventOwner->record({ehud::data::makeUUID(),modules::EventKind::minigameAction,ehud::data::foundationNow(),{{"action",action}}});}};
    gameSession=std::make_unique<modules::OrbiPomSession>(gameBest,std::move(callbacks));
    tools::OrbiPomPreviewOptions gameOptions;gameOptions.raster.assetRoot=args.orbipomAssets;gameOptions.raster.pixelsPerPoint=2;gameOptions.raster.paddingPoints=1;
    game=std::make_unique<tools::OrbiPomPreview>(*gameSession,rasterizer,std::move(gameOptions));game->resize(metrics);game->setForeground(focused,time);game->setOverlayVisible(session->phase()!=core::VisibilityPhase::concealed,time);configurationPending=true;
    gameModuleOwner=minigameModule(*game);registerModule(gameModuleOwner);
}
void Application::Impl::constructTray(){
    const auto bytes=ehud::data::detail::readFile(args.residentIcons/"manifest.json",512*1024);need(bytes.has_value(),"Missing original icon roster");
    const auto catalog=Json::parse(*bytes);need(catalog["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Icon source differs from this app migration");
    const auto&icons=catalog["icons"].array();const auto icon=std::find_if(icons.begin(),icons.end(),[](const auto&i){return i["id"].string()=="endfield";});need(icon!=icons.end(),"Missing original Endfield icon");
    const auto&art=(*icon)["app"];const auto name=art["file"].string();need(name=="endfield-app.png","Unexpected icon path");
    // The original color app artwork remains visible on both Windows taskbar
    // themes. Adaptive template preference is connected with Display settings.
    applicationIcon=gpu::ApplicationIcon::fromPNG(args.residentIcons/name,art["sha256"].string(),32);
    tray=std::make_unique<gpu::TrayController>(static_cast<HWND>(host.hwnd()),trayMessage,RegisterWindowMessageW(L"TaskbarCreated"));
    // Explorer may be absent or hide the icon; hotkey and relaunch activation
    // remain available, so a production tray failure is not fatal.
    const bool installed=tray->start(static_cast<HICON>(applicationIcon.handle()),production()?L"EndfieldHUD":L"EndfieldHUD migration preview");
    if(!production())need(installed,"Windows could not add the preview tray icon");
    const auto key=modules::settingsShortcut(configuration);
    const bool shortcut=tray->setHotkey(production()?gpu::TrayHotkey{key.modifiers,key.key}:gpu::TrayHotkey{MOD_CONTROL,VK_OEM_3});
    updateTrayMenu();
    if(!shortcut&&!production())std::cout<<"Ctrl + ` is already registered; reopen this isolated preview from its tray icon.\n";
}
void Application::Impl::recordDisplays(){
    if(!eventRecorder)return;
    std::map<std::string,std::string>devices;
    for(const auto&d:gpu::readConnectedDisplays()){
        const auto id=d.persistentID.empty()?std::to_string(d.handle):narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.persistentID.data()),d.persistentID.size()));
        devices.emplace(id,narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.name.data()),d.name.size())));
    }
    eventRecorder->receiveDisplays(devices);
}
void Application::Impl::recordBattery(){
    if(!eventRecorder||!systemServices)return;
    const auto&b=systemServices->battery();
    eventRecorder->receiveBattery({b.percent?std::optional<int>(int(*b.percent)):std::nullopt,b.ac_connected.value_or(false),b.charging.value_or(false),b.fully_charged.value_or(false),b.available&&b.present.value_or(false)});
}
void Application::Impl::refreshExternalStatus(){
    if(!loginItem)return;
    loginStatus=LoginItemRegistration::statusDescription(loginItem->read(),settingsLanguage(configuration));
    if(settingsUI)settingsUI->controller().setExternalStatus(loginStatus,shortcutStatus);
}
void Application::Impl::updateTrayMenu(){
    if(!tray)return;
    const auto language=watchAppearance.language==core::Language::system?core::Language::english:watchAppearance.language;
    const auto t=[&](std::string_view en,std::string_view zh){return wide(core::localized(en,zh,language));};
    std::vector<gpu::TrayMenuItem>menu;
    if(!production()){
        const bool shortcut=tray->hotkey().has_value();
        menu.push_back({gpu::TrayAction::openOverlay,shortcut?L"打开浮层\tCtrl + `":L"打开浮层"});
        if(!args.settingsAssets.empty())menu.push_back({gpu::TrayAction::settings,L"设置…"});if(workMode)menu.push_back({gpu::TrayAction::workMode,L"工作模式"});menu.push_back({gpu::TrayAction::none,{},false,false,true});menu.push_back({gpu::TrayAction::quit,L"退出 EndfieldHUD"});
        tray->setMenu(std::move(menu));return;
    }
    // AppDelegate.makeMenu: status, Open overlay (shortcut), Work Mode,
    // Settings…, About, Quit. Updater/charge-preview items join with their owners.
    if(systemServices&&systemServices->battery().available){
        const auto&b=systemServices->battery();
        const auto percent=b.percent?std::to_string(*b.percent)+"%":core::localized("Unknown charge","电量未知",language);
        const auto state=b.charging.value_or(false)?core::localized("Charging","正在充电",language):b.ac_connected.value_or(false)?core::localized("Power connected","已连接电源",language):core::localized("On battery","使用电池",language);
        const auto text=b.present.value_or(false)?percent+" · "+state:core::localized("No internal battery · Preview available","无内置电池 · 可预览效果",language);
        menu.push_back({gpu::TrayAction::none,wide(text),false});menu.push_back({gpu::TrayAction::none,{},false,false,true});
        // AppDelegate.updateMenu: "EndfieldHUD — <description>" tooltip.
        auto tip=L"EndfieldHUD \u2014 "+wide(text);
        if(tip!=trayTooltip&&applicationIcon.handle()){trayTooltip=std::move(tip);(void)tray->updateIcon(static_cast<HICON>(applicationIcon.handle()),trayTooltip);}
    }
    auto open=t("Open overlay","打开浮层");if(tray->hotkey())open+=L"\t"+wide(modules::settingsShortcutTitle(modules::settingsShortcut(configuration)));
    menu.push_back({gpu::TrayAction::openOverlay,std::move(open)});
    std::wstring work=t("Work Mode","工作模式");
    if(workMode){using P=modules::WorkModePhase;const auto phase=workMode->controller().snapshot(now()).phase;
        const auto state=phase==P::running?core::localized("Active","进行中",language):phase==P::paused?core::localized("Paused","已暂停",language):phase==P::stopped?core::localized("Stopped","已停止",language):phase==P::completed?core::localized("Countdown complete","倒计时结束",language):std::string{};
        if(!state.empty())work+=L" · "+wide(state);}
    menu.push_back({gpu::TrayAction::workMode,std::move(work),bool(workMode)});
    menu.push_back({gpu::TrayAction::settings,t("Settings…","设置…"),bool(settingsUI)});
    menu.push_back({gpu::TrayAction::none,{},false,false,true});
    menu.push_back({gpu::TrayAction::about,t("About EndfieldHUD","关于 EndfieldHUD"),bool(settingsUI)});
    menu.push_back({gpu::TrayAction::quit,t("Quit EndfieldHUD","退出 EndfieldHUD")});
    tray->setMenu(std::move(menu));
}

// ---------------------------------------------------------------------------
// Lifecycle
void Application::Impl::perform(const core::SystemOverlayEffects&effects,double time){
    using K=core::SystemOverlayEffect::Kind;
    for(const auto&e:effects){
        switch(e.kind){
        case K::open:lifecycleOpening=e.token;lifecycleClosing.reset();openPresentation(e.module,time);break;
        case K::close:lifecycleClosing=e.token;lifecycleOpening.reset();close(time);break;
        case K::select:selectModule(e.module,time);break;
        case K::preview:break; // Charging preview belongs to the charge indicator owner.
        case K::markLaunched:if(settingsSaves)settingsSaves->markLaunched();break;
        case K::quitAccepted:quitRequested=true;if(tray)tray->setHotkey({});break;
        case K::terminate:finishQuit(time);break;
        case K::restartShortcut:if(tray){const auto key=modules::settingsShortcut(configuration);tray->setHotkey(gpu::TrayHotkey{key.modifiers,key.key});updateTrayMenu();}break;
        }
    }
}
void Application::Impl::targetDisplay(){
    // OverlayController: every opening chooses HUDDisplayPolicy.targetScreen
    // (pointer display, or the saved display with pointer/primary fallback).
    // Topology is read only at this opening; the window keeps its pixels.
    const auto displays=gpu::readConnectedDisplays();
    gpu::DisplayPreference preference;preference.pointerDisplay=configuration.boolean("openOnActiveDisplay");
    if(const auto&id=configuration.fields["hudDisplayUUID"];!preference.pointerDisplay&&id.isString()){
        const auto text=wide(id.string());preference.persistentID=std::u16string(text.begin(),text.end());}
    POINT pointer{};if(!GetCursorPos(&pointer))return;
    const auto index=gpu::resolveDisplay(preference,displays,{pointer.x,pointer.y});if(!index)return;
    const auto&display=displays[*index];hudDisplay=display.handle;previousApplication=reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
    const auto window=static_cast<HWND>(host.hwnd());const auto&b=display.bounds;
    for(unsigned attempt=0;attempt<2;++attempt){
        RECT current{};if(!GetWindowRect(window,&current))return;
        if(current.left==b.left&&current.top==b.top&&current.right==b.right&&current.bottom==b.bottom)return;
        // A per-monitor DPI change can apply Windows' suggested rectangle;
        // the second pass restores the exact display bounds.
        if(!SetWindowPos(window,nullptr,b.left,b.top,b.right-b.left,b.bottom-b.top,SWP_NOZORDER|SWP_NOACTIVATE))return;
    }
}
void Application::Impl::openPresentation(core::Module initial,double time){
    if(production()&&session->phase()==core::VisibilityPhase::concealed)targetDisplay();
    if(notes&&notes->selected()!=initial)selectModule(initial,time);
    open(time);if(visibleMode())host.show();refresh(time);
}
void Application::Impl::selectModule(core::Module module,double time){
    if(notes){const auto current=notes->selected();
        if(module!=current)if(auto*owner=registry.presenter(current);owner&&!owner->releasesSelection(time)){pendingModule=module;return;}}
    pendingModule.reset();if(mediaPicker){mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();}
    if(readerOwner&&module!=core::Module::reader)readerOwner->cancelImport();
    pendingReaderPicker.reset();pendingReaderAction.reset();
    if(notes)notes->select(module,time);
}
void Application::Impl::open(double time){
    closing=false;canvasOpenedAt=time;session->open(time,0x5eed);
    registry.forEach([&](ModuleOwner&o){o.setOverlayVisible(true,time);},"open");
    if(volumeProvider)volumeProvider->setVisible(true);
    if(visibleMode()&&headerClock.setActive(true,time))updateClock();
}
void Application::Impl::close(double time){
    if(closing)return;
    if(!production())std::cout<<"Preview close requested at "<<time<<std::endl;
    if(registry.first(ModuleRegistry::Chain::finishEditing,[&](ModuleOwner&o,const ModuleRouting&){return !o.finishEditing(time);},"close")){pendingClose=true;return;}
    pendingClose=false;pendingModule.reset();headerClock.setActive(false,time);if(mediaPicker)mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();if(archiveMedia)archiveMedia->cancelImport();if(readerOwner)readerOwner->cancelImport();pendingReaderPicker.reset();pendingReaderAction.reset();
    registry.forEach([&](ModuleOwner&o){o.overlayClosing(time);},"close");if(volumeProvider)volumeProvider->setVisible(false);
    host.capturePointer(false);environment.pointerLocked=false;session->setEnvironment(environment,time);canvasCapturedOpacity=canvasOpacity(time);canvasClosedAt=time;closing=true;session->close(time);refresh(time);
}
void Application::Impl::forceConceal(double time){
    // OverlayController.forceCloseSystemOverlay: sleep, session end and
    // termination cannot wait for visible animations or editor retries.
    pendingClose=false;pendingModule.reset();headerClock.setActive(false,time);pendingShelfReveal.reset();
    if(mediaPicker)mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();if(archiveMedia)archiveMedia->cancelImport();if(readerOwner)readerOwner->cancelImport();pendingReaderPicker.reset();pendingReaderAction.reset();
    if(session->phase()!=core::VisibilityPhase::concealed||closing){
        registry.forEach([&](ModuleOwner&o){o.overlayClosing(time);},"force close");if(volumeProvider)volumeProvider->setVisible(false);
        host.capturePointer(false);environment.pointerLocked=false;session->setEnvironment(environment,time);
        closing=false;session->conceal(time);if(notes)notes->setMediaActive(false,time);
    }
    lifecycleOpening.reset();lifecycleClosing.reset();
    host.hide();if(!stopping){host.setFrameDemand(demand(time));scheduleDeadline();}
}
bool Application::Impl::flushDocuments(double time,bool reopenOnFailure){
    // Archive, Calendar and Reader drains share the executor; then Settings.
    ModuleOwner*failed=registry.first(ModuleRegistry::Chain::flush,[&](ModuleOwner&o,const ModuleRouting&){return !o.flush(time);},"flush");
    if(failed){
        if(reopenOnFailure){
            const auto module=failed==archiveModuleOwner.get()?core::Module::archive:failed==calendarModuleOwner.get()?core::Module::calendar:core::Module::reader;
            reopenAfterSaveFailure(module,time);
        }
        return false;
    }
    // Flush only at an explicit shutdown boundary. A failed save retains its
    // record and restores the Settings UI instead of silently quitting.
    if(settingsSaves&&!settingsSaves->flush()){
        if(reopenOnFailure){if(settingsUI)settingsUI->controller().setStatus(settingsSaves->status().error.value_or("Unable to save settings"));reopenAfterSaveFailure(core::Module::system,time);}
        return false;
    }
    // Event Log persistence uses the owner clock, which only moves forward.
    if(eventSaves)eventSaves->flush(now());
    return true;
}
void Application::Impl::reopenAfterSaveFailure(core::Module module,double time){
    quitRequested=false;
    perform(lifecycle.cancelTermination(),time);
    // Windows keeps the failing module visible (Mac only re-enables input):
    // a silent no-op quit would hide the unsaved record from the user.
    if(lifecycle.phase()==core::SystemOverlayPhase::open){selectModule(module,time);return;}
    perform(lifecycle.openSettingsModule(module),time);
}
void Application::Impl::finishQuit(double time){
    if(!flushDocuments(time,true))return;
    projectionHandoff.cancel();if(projection)projection->close(time);
    stopping=true;host.setDeadline({});host.setFrameDemand({});host.requestStop();
}
void Application::Impl::trayAction(gpu::TrayAction action,bool hotkey,double time){
    if(projectionHandoff.active()){projectionTrayAction=action;refresh(time);return;}
    switch(action){
    case gpu::TrayAction::quit:perform(lifecycle.requestTermination(),time);break;
    case gpu::TrayAction::openOverlay:perform(hotkey?lifecycle.toggle():lifecycle.openOverlay(),time);break;
    case gpu::TrayAction::settings:perform(lifecycle.openSettingsModule(core::Module::system),time);break;
    case gpu::TrayAction::about:perform(lifecycle.openSettingsModule(core::Module::about),time);break;
    case gpu::TrayAction::workMode:perform(lifecycle.openWorkMode(time),time);break;
    default:break;
    }
    if(visibleMode()&&session->phase()!=core::VisibilityPhase::concealed&&!closing&&!stopping){host.show();refresh(time);}
}
void Application::Impl::forwardedActivation(std::span<const std::string>values){
    if(!ready||stopping)return;
    std::vector<std::string_view>views(values.begin(),values.end());
    core::ApplicationArguments parsed;
    try{parsed=core::parseApplicationArguments(views);}catch(const std::exception&){return;}
    const auto time=now();
    switch(core::forwardedActivation(parsed)){
    case core::ForwardedActivation::settings:trayAction(gpu::TrayAction::settings,false,time);break;
    case core::ForwardedActivation::reopen:
        if(projectionHandoff.active()){projectionTrayAction=gpu::TrayAction::openOverlay;refresh(time);break;}
        perform(lifecycle.reopen(),time);
        if(visibleMode()&&session->phase()!=core::VisibilityPhase::concealed&&!closing){host.show();refresh(time);}
        break;
    case core::ForwardedActivation::none:break;
    }
}
void Application::Impl::sessionEnding(SessionEnd phase,std::uint32_t reason){
    // WM_QUERYENDSESSION/WM_ENDSESSION: logoff, restart or Restart Manager.
    // The query drains dirty documents while a shutdown block reason names
    // the work; the end conceals the HUD without animation, drains again and
    // stops. The user-quit cancel-on-failure path is not used: Windows ends
    // the session regardless, and every failed record stays on disk unchanged.
    if(!ready||sessionEnded||phase==SessionEnd::cancelled)return;
    const auto time=now();const auto window=static_cast<HWND>(host.hwnd());
    const auto reasonText=wide(core::localized("Saving EndfieldHUD data…","正在保存 EndfieldHUD 数据…",watchAppearance.language==core::Language::system?core::Language::english:watchAppearance.language));
    const bool blocked=ShutdownBlockReasonCreate(window,reasonText.c_str())!=FALSE;
    try{
        if(phase==SessionEnd::ending){
            sessionEnded=true;(void)lifecycle.forceClose();forceConceal(time);
            if(archive)(void)archive->finishEditing(time);
            if(calendar)(void)calendar->dismissMenu(false,time);
            if(workMode)workMode->shutdown(time);
        }
        (void)flushDocuments(time,false);
    }catch(...){if(blocked)ShutdownBlockReasonDestroy(window);throw;}
    if(blocked)ShutdownBlockReasonDestroy(window);
    if(phase==SessionEnd::ending){
        if(tray){tray->setHotkey({});tray->stop();}
        // Restart Manager (ENDSESSION_CLOSEAPP) and logoff both expect exit.
        (void)reason;stopping=true;host.setDeadline({});host.setFrameDemand({});host.requestStop();
    }
}

// ---------------------------------------------------------------------------
// Frame, scheduling and publication
double Application::Impl::canvasOpacity(double time){return source::DesktopChromeTiming::opacity(!closing,time-(closing?canvasClosedAt:canvasOpenedAt),canvasCapturedOpacity,settings.reduceMotion);}
double Application::Impl::backdropOpacity(double time){
    if(session->phase()==core::VisibilityPhase::concealed)return 0.;
    if(settings.reduceMotion)return closing?0.:1.;
    need(backdropAnimation.has_value(),"Visible backdrop lost its immutable original timing");
    return closing?backdropAnimation->exit.alpha(time-canvasClosedAt):backdropAnimation->entrance.alpha(time-canvasOpenedAt);
}
void Application::Impl::updateBackdrop(double time){
    if(!visibleMode())return; // Visible initialization must complete before any frame.
    if(metrics.pixelWidth&&metrics.pixelHeight){backdropState.pixelWidth=metrics.pixelWidth;backdropState.pixelHeight=metrics.pixelHeight;}
    backdropState.lowPower=settings.lowPower;backdropState.sourceOpacity=backdropOpacity(time);backdrop.update(backdropState);
}
bool Application::Impl::updateClock(){if(chrome){chromeContent.reading=headerClock.reading();if(workMode){
        using P=modules::WorkModePhase;const auto phase=workMode->controller().snapshot(now()).phase;
        chromeContent.workPhase=phase==P::running?source::DesktopWorkPhase::running:phase==P::paused?source::DesktopWorkPhase::paused:source::DesktopWorkPhase::idle;
    }return chrome->setContent(chromeContent);}return false;}
// Same existing one-shot host deadline, armed only at explicit Calendar
// state/system events. Civil midnight uses date arithmetic across DST.
void Application::Impl::updateCalendarWake(){calendarWakeActive=calendarState&&calendarState->active();calendarNextWake.reset();if(!visibleMode()||!calendarState||!calendarState->loaded()||(!calendarState->active()&&calendarState->events().empty()))return;
    const auto zone=calendarCivil->timeZone();const auto utc=ehud::data::foundationNow();const auto tomorrow=zone.day(utc).advanced(1);if(!tomorrow)return;
    for(unsigned minute=0;minute<1440;++minute)if(const auto at=zone.timestamp(*tomorrow,minute/60,minute%60);at&&*at>utc){calendarNextWake=now()+(*at-utc);break;}
}
bool Application::Impl::flushCalendar(){if(!calendarState)return true;for(unsigned step=0;step<128;++step){calendarState->queueCapacityAvailable();calendarNotifications->queueCapacityAvailable();utility->waitIdle();utility->drain();const auto&status=calendarNotifications->status();if(!calendarState->hasPendingWork()&&!status.busy&&!status.pending)return !calendarState->hasPersistenceFailure();}return false;}
void Application::Impl::applyConfiguration(double time){
    if(!configurationPending)return;configurationPending=false;
    settings={configuration.boolean("reduceMotion"),configuration.boolean("ambientAnimation"),configuration.boolean("lowPowerVisualMode"),configuration.number("parallaxIntensity"),configuration.number("perspectiveIntensity"),configuration.number("hudScale"),{configuration.number("hudOffsetX"),configuration.number("hudOffsetY")}};
    session->setSettings(settings,time);watchAppearance.language=settingsLanguage(configuration);
    rasterizer.setDefaultFontLanguage(watchAppearance.language==core::Language::korean?gpu::LayerFontLanguage::korean:gpu::LayerFontLanguage::simplifiedChinese);
    watchAppearance.dark=configuration.string("theme")!="light";
    if(configuration.string("theme")=="system"){DWORD light=0,size=sizeof(light);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size)==ERROR_SUCCESS)watchAppearance.dark=light==0;}
    const auto hex=configuration.string("accentHex");for(unsigned n=0;n<3;++n)watchAppearance.accentSRGB[n]=std::stoul(hex.substr(n*2,2),nullptr,16)/255.;
    const auto a=watchAppearance.accentSRGB;const auto effective=watchAppearance.dark?a:source::sourceBlackBlend(a,.35);const std::array<double,4>accent{effective[0],effective[1],effective[2],1};
    backdropState.dark=watchAppearance.dark;backdropState.blurAmount=configuration.number("blurAmount");backdropState.backgroundDarkness=configuration.number("backgroundDarkness");
    if(nativeAppearance){std::vector<gpu::WatchAppearanceEntry>entries;for(const auto&e:contentCatalog->entries())entries.push_back({e.action,e.target,moduleCaption(e.target,watchAppearance.language,e.title),"module:"+e.target,true});nativeAppearance->setEntries(entries);}
    headerClock.setFormat(configuration.string("clockFormat")=="twelveHour"?modules::HUDClockFormat::twelveHour:modules::HUDClockFormat::twentyFourHour,time);
    const auto clock=configuration.string("clockStyle");chromeContent.style=clock=="split"?source::DesktopClockStyle::split:clock=="dial"?source::DesktopClockStyle::dial:clock=="rail"?source::DesktopClockStyle::rail:clock=="stacked"?source::DesktopClockStyle::stacked:source::DesktopClockStyle::digital;
    chromeContent.localizedClose=core::localized("Click outside to close","点击外侧关闭",watchAppearance.language);chromeContent.uppercaseShortcut=modules::settingsShortcutTitle(modules::settingsShortcut(configuration));
    if(chrome){chrome->setAppearance({watchAppearance.dark,watchAppearance.dark?a:source::sourceBlackBlend(a,.35)});updateClock();}
    if(tray&&settingsUI&&!settingsUI->controller().capturingShortcut()&&!quitRequested){const auto key=modules::settingsShortcut(configuration);const gpu::TrayHotkey desired{key.modifiers,key.key};
        if(tray->hotkey()!=desired){shortcutStatus=tray->setHotkey(desired)?std::string{}:core::localized("Shortcut unavailable","快捷键不可用",watchAppearance.language);settingsUI->controller().setExternalStatus(loginStatus,shortcutStatus);}}
    moduleAppearance={watchAppearance.language,watchAppearance.dark,accent,{a[0],a[1],a[2]},settings.reduceMotion,settings.ambientEnabled,settings.lowPower};
    registry.forEach([&](ModuleOwner&o){o.setAppearance(moduleAppearance,time);},"appearance");
    if(production())updateTrayMenu();
    // Baseline is the loaded record (AppDelegate.applicationDidFinishLaunching).
    if(eventRecorder&&settingsLoaded)eventRecorder->receiveConfiguration(core::RecorderDisplaySettings::from(configuration));
}
core::FrameDemand Application::Impl::demand(double time){
    if(projection&&projection->presented())return projection->demand(time);
    auto result=session->demand(time);
    if(result.phase==core::VisibilityPhase::visible)result.finiteAnimation=result.finiteAnimation||(mediaBroker&&mediaBroker->requiresFrames())||registry.requiresFrames(time);
    return result;
}
void Application::Impl::scheduleDeadline(){if(!visibleMode()||!ready||stopping)return;std::optional<double>next;
    auto include=[&](std::optional<double>value){if(value&&(!next||*value<*next))next=value;};
    include(calendarNextWake);if(projection)include(projection->dismissalDeadline());include(headerClock.nextDeadline());include(registry.nextWakeTime(now()));
    if(mediaBroker){const auto wake=mediaBroker->nextWakeTime();if(wake&&*wake>now())include(wake);}if(eventOwner)include(eventOwner->saveDeadline());if(activityProbe)include(activityPlan.nextWakeTime());include(lifecycle.nextWakeTime());host.setDeadline(next);
}
void Application::Impl::refresh(double time){if(visibleMode()&&ready&&!stopping){
    projectionDrop->enabled=projection&&projection->presented()&&projectionHandoff.acceptsInput()&&!projection->preview()->importBusy()&&pickerOwner==PickerOwner::none;
    projectionDrop->scale=metrics.scale;
    if(projection&&projection->presented())while(auto action=projection->preview()->takeAction())projectionActions.push_back(std::move(*action));
    if((!projectionActions.empty()||projectionTrayAction||projectionDisplayPending||projectionResizePending||(projection&&projection->dismissalComplete(time)))&&!projectionActionsQueued&&!projectionPainting){need(PostMessageW(static_cast<HWND>(host.hwnd()),projectionActionMessage,serviceGeneration,0)!=FALSE,"Post Projection actions");projectionActionsQueued=true;}
    if(eventRecorder&&workMode)eventRecorder->receiveWork(workMode->controller().snapshot(time));
    if(reader&&!readerActionQueued)if(auto action=reader->takeImportAction()){pendingReaderAction=std::move(*action);need(PostMessageW(static_cast<HWND>(host.hwnd()),readerActionMessage,serviceGeneration,0)!=FALSE,"Post Reader import action");readerActionQueued=true;}if(workMode)updateClock();host.setFrameDemand(demand(time));scheduleDeadline();host.invalidate();}}
bool Application::Impl::pointerLocked(){return registry.pointerLocked();}
std::vector<modules::NotesShelfChoice> Application::Impl::shelfChoices(){std::vector<modules::NotesShelfChoice>choices;
    if(shelf)for(const auto&item:shelf->state().items()){
        auto extension=utf8(utf8Path(item.lastKnownPath).extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);});
        constexpr std::array supported{".png",".jpg",".jpeg",".gif",".heic",".heif",".tif",".tiff",".bmp",".webp",".jp2",".mov",".mp4",".m4v",".avi",".mpeg",".mpg"};
        choices.push_back({item.id,item.name,item.typeDescription,!item.isDirectory&&std::find(supported.begin(),supported.end(),extension)!=supported.end(),!item.availabilityError});
    }return choices;
}
void Application::Impl::publishNative(){
    ordered.clear();ordered.push_back({&layers,{}});appendedOwners.clear();
    registry.forEach([&](ModuleOwner&o){o.upload(renderer);},"upload");
    // The old wrapper is below the incoming wrapper, independently of owner
    // registration order. Floating Notes stay above center modules; modal
    // confirmations stay above everything.
    const auto appendModule=[&](core::Module module){auto*owner=registry.presenter(module);if(!owner||std::find(appendedOwners.begin(),appendedOwners.end(),owner)!=appendedOwners.end())return;appendedOwners.push_back(owner);
        registry.guard(*owner,[&]{for(const auto&e:owner->entries(module))ordered.push_back(e);},"publish");};
    if(notes){const auto&sample=notes->modulePresentation();appendModule(sample.current.module);if(sample.incoming)appendModule(sample.incoming->module);}
    registry.forEach([&](ModuleOwner&o){for(const auto&e:o.floatingEntries())ordered.push_back(e);},"publish");
    registry.forEach([&](ModuleOwner&o){for(const auto&e:o.modalEntries())ordered.push_back(e);},"publish");
    // A language event can leave equal captions/numbers in retained scenes.
    // Refresh only an older font generation; group carriers forward this to
    // their cached local text, while editor owners keep layout/TSF together.
    const auto fontRevision=rasterizer.fontRevision();
    for(const auto&e:ordered)if(e.scene->fontRevision()!=fontRevision)e.scene->refreshTypography();
    const auto notesRevision=notes?notes->compositionRevision():0;
    bool changed=ordered.size()!=published.size()||notesRevision!=publishedNotesRevision;if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
    // Skill selection and sprite swaps reuse their draw buffer. Pointer and
    // count alone cannot detect those resource-identity changes.
    if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
    if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});
        registry.forEach([&](ModuleOwner&o){o.collected(renderer);},"collect");if(archiveMedia)archiveMedia->collectRetired();if(mediaBroker)mediaBroker->collectRetired();}
    publishedNotesRevision=notesRevision;
}
bool Application::Impl::present(double time,bool submit){
    const auto traceFrameStart=readerTrace?Clock::now():Clock::time_point{};
    if(projection&&projection->presented()){
        projectionPainting=true;try{mediaBroker->sample(time);projection->mediaChanged(time);projection->render(time,submit);projectionPainting=false;}catch(...){projectionPainting=false;throw;}return true;
    }
    readerTime=time;mapTime=time;calendarTime=time;
    if(!game&&gameEnabled&&notes&&notes->selected()==core::Module::minigame)ensureGame(time);
    if(map&&notes&&notes->selected()==core::Module::map)requestMapGeography();
    if(storageFixture)storageFixture->time.store(time,std::memory_order_relaxed);
    auto frameProbe=probe.measure(LiveProbe::frame);applyConfiguration(time);
    if(clipboardDirty&&clipboard&&notes&&notes->selected()==core::Module::clipboard){isolate(clipboardModuleOwner,"refresh",[&]{clipboard->refresh();});clipboardDirty=false;}
    // Backdrop COM calls can dispatch nested input. Finish them before
    // borrowing a source frame, then sample the current live event time.
    {auto stage=probe.measure(LiveProbe::backdrop);updateBackdrop(time);}if(visibleMode())time=std::max(time,now());
    const WatchSessionFrame*sample=nullptr;{auto stage=probe.measure(LiveProbe::sourcePose);sample=session->sample(time);}if(!sample)return false;
    if(focused&&sample->visibility.phase==core::VisibilityPhase::visible&&!session->inputEnabled())session->setInputEnabled(true,time);
    auto parameters=materials->parameters();parameters.camera=sample->gpuCamera;parameters.timeSeconds=sample->shaderTime;parameters.width=metrics.pixelWidth;parameters.height=metrics.pixelHeight;if(nativeAppearance){std::array<float,3>linear;for(unsigned n=0;n<3;++n){const auto value=watchAppearance.accentSRGB[n];linear[n]=static_cast<float>(value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4));}parameters.desktopAccentLinear=linear;}
    {auto stage=probe.measure(LiveProbe::materials);materialPresentation->update(*sample->sourceFrame,parameters);materials->flush(renderer.sourceGraphics());}
    {auto stage=probe.measure(LiveProbe::nativeLabels);if(nativeContent)nativeContent->update(session->actions());
    for(auto&button:available){const auto found=session->actions().find(button.buttonID);button.enabled=found!=session->actions().end();button.expandFileShelfCaption=button.enabled&&nativeAppearance&&nativeAppearance->expandsFileShelfCaption(found->second,watchAppearance.language);}
    const core::Rect viewport{0,0,metrics.width,metrics.height};labels->update(*sample->sourceFrame,sample->camera,viewport,available);
    if(nativeAppearance){watchAppearance.selectedAction=notes?contentCatalog->actionForTarget(core::moduleIdentifier(notes->selected())).value_or(0):0;nativeAppearance->update(session->actions(),watchAppearance,labels->placements());}
    // Chrome samples once per visible second; opacity follows the same
    // ready/close timestamps as the source outer controller.
    const source::DesktopChromeSettings chromeSettings{viewport,settings.hudScale,settings.hudOffset,notes?notes->selected():core::Module::power,true};
    if(chrome)chrome->update(*sample->sourceFrame,sample->camera,chromeSettings,static_cast<float>(canvasOpacity(time)));
    }
    const source::DesktopChromeSettings noteChromeSettings{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,notes?notes->selected():core::Module::power,true};
    if(notes&&chromePlan&&chromePlan->projection().center){
        auto stage=probe.measure(LiveProbe::notesPose);
        // Notes first: it owns the module presentation sample every other
        // owner reads in this same frame.
        const ModuleFrame frame{*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time,focused};
        registry.forEach([&](ModuleOwner&o){o.update(frame);},"frame");
    }
    // Visibility unions are established first. Providers tick exactly once
    // on this existing frame clock; module owners only reread their clients.
    if(archiveMedia)archiveMedia->sync(time);
    if(mediaBroker){mediaBroker->sample(time);if(notes)notes->refreshSharedMedia(time);if(archiveMedia&&archiveMedia->refresh(time)&&archive&&notes&&chromePlan&&chromePlan->projection().center&&archiveModuleOwner)registry.guard(*archiveModuleOwner,[&]{archive->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);},"frame");}
    {auto stage=probe.measure(LiveProbe::publication);publishNative();composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale,1));}
    if(submit){auto stage=probe.measure(LiveProbe::draw);renderer.draw(visibleMode());}
    if(readerTrace&&reader&&notes&&notes->selected()==core::Module::reader)readerTrace->record(false,time,reader->scrollSnapshot(),0,0,std::chrono::duration<double,std::milli>(Clock::now()-traceFrameStart).count());return true;
}
bool Application::Impl::requestMediaPicker(){
    if(pickerOwner!=PickerOwner::none||!mediaPicker)return false;
    mediaPicker->setLabels({core::localized("Add image/video","添加图片/视频",watchAppearance.language),core::localized("Add","添加",watchAppearance.language),core::localized("Add selected files","添加所选文件",watchAppearance.language),gpu::ShelfPickerMode::mixedReferences});
    return mediaPicker->request();
}
void Application::Impl::importReaderReference(std::uint64_t generation,std::string path,std::shared_ptr<void>lease,double time){
    if(!reader||!readerOwner||!reader->beginImport(generation,time))return;
    try {auto book=modules::windowsReaderReference(ehud::data::makeUUID(),path,utf8(utf8Path(path).filename()));readerOwner->import(generation,std::move(book),std::move(lease));}
    catch(const std::exception&){reader->receiveImportError(generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}
}
void Application::Impl::activate(const WatchActivation&event){
    if(event.action==quitAction){
        // SystemHUDView.presentQuitConfirmation: dims the HUD, follows its
        // tilt and owns input until Cancel or Quit.
        // SystemHUDView.allowsModuleInput: not during a module swap, a pending
        // confirmation or an accepted quit.
        if(quitConfirmation&&lifecycle.phase()==core::SystemOverlayPhase::open&&!quitRequested&&!quitConfirmation->presented()&&
           !(notes&&notes->modulePresentation().transitioning)){const auto time=now();quitConfirmation->present(time);session->pointerMove(environment.pointer,time);refresh(time);}
        else if(!quitConfirmation)perform(lifecycle.requestQuit(),now());
        return;
    }
    if(event.action==closeAction){perform(lifecycle.close(),now());return;}
    const auto entries=contentCatalog->entries();const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto&value){return value.action==event.action;});need(entry!=entries.end(),"Source activation exceeds exported actions");
    if(args.projection&&entry->target=="projection"){
        if(auto command=projectionHandoff.open({session->phase()==core::VisibilityPhase::visible,quitRequested,pendingClose, bool(shelf&&shelf->preservesFocusOnLoss())},{hudDisplay,previousApplication}))projectionCommand(*command,now());return;
    }
    if(notes){for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);if(core::moduleIdentifier(module)==entry->target){
        const auto effects=lifecycle.selectModule(module);if(effects.empty())selectModule(module,now());else perform(effects,now());break;}}}
    if(!production())std::cout<<"Source action: "<<entry->target<<'\n';
}

// ---------------------------------------------------------------------------
// Projection (unchanged handoff)
std::optional<gpu::DisplayDescriptor> Application::Impl::projectionDisplay(std::uintptr_t preferred){
    const auto displays=gpu::readConnectedDisplays();for(const auto&display:displays)if(display.handle==preferred)return display;
    POINT pointer{};GetCursorPos(&pointer);if(const auto index=gpu::resolveDisplay({},displays,{pointer.x,pointer.y}))return displays[*index];return {};
}
void Application::Impl::moveProjection(const gpu::DisplayDescriptor&display){
    hudDisplay=display.handle;projectionWorkTopPixels=std::max(0,display.workArea.top-display.bounds.top);
    const auto&bounds=display.bounds;need(SetWindowPos(static_cast<HWND>(host.hwnd()),HWND_TOPMOST,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE)!=FALSE,"Position Projection on its retained display");
    if(projection)projection->resize(host.metrics(),projectionWorkTopPixels/host.metrics().scale);
}
void Application::Impl::projectionDropRoute(bool enabled,double time){
    projectionDrop->enabled=false;projectionDrop->paths.clear();if(!shelf)return;
    if(!enabled){shelf->setExternalDropCallbacks({},time);return;}
    projectionDrop->hwnd=static_cast<HWND>(host.hwnd());projectionDrop->scale=metrics.scale;
    const std::weak_ptr<ProjectionDropInbox>weak=projectionDrop;
    shelf->setExternalDropCallbacks(gpu::ShelfDropTarget::Callbacks{
        [weak](POINTL point){auto state=weak.lock();if(!state||!state->enabled||!state->paths.empty())return false;POINT p{point.x,point.y};RECT rect{};return ScreenToClient(state->hwnd,&p)&&GetClientRect(state->hwnd,&rect)&&PtInRect(&rect,p);},
        [weak](std::span<const std::string>paths){auto state=weak.lock();if(!state||!state->enabled||!state->paths.empty()||paths.empty()||paths.size()>modules::ProjectionModel::maximumMedia)return false;POINT point{};if(!GetCursorPos(&point)||!ScreenToClient(state->hwnd,&point))return false;state->point={point.x/state->scale,point.y/state->scale};state->paths.assign(paths.begin(),paths.end());if(!PostMessageW(state->hwnd,projectionDropMessage,serviceGeneration,0)){state->paths.clear();return false;}return true;}
    },time);
}
void Application::Impl::cancelProjectionPicker(){if(pickerOwner==PickerOwner::projection){mediaPicker->cancel();pickerOwner=PickerOwner::none;}pendingProjectionPicker.reset();}
tools::ProjectionPreviewOptions Application::Impl::projectionOptions(){
    tools::ProjectionPreviewOptions value;value.language=watchAppearance.language;value.reduceMotion=settings.reduceMotion;
    const auto accent=watchAppearance.accentSRGB;value.accent={accent[0],accent[1],accent[2],1};
    value.raster.assetRoot=args.notesFormatAssets;value.raster.pixelsPerPoint=2;value.raster.paddingPoints=1;
    value.menuStrings=tools::archivePreviewStrings(watchAppearance.language).menus;value.shelfChoices=[this]{return shelfChoices();};
    constexpr std::string_view wheel="244c34ad474b15c242f9bd62cbf195272cbf7b880acbeaf0021e237d6a22abd2";
    value.colorWheel=gpu::NativeNotesControlsImage{{"notes.menu/wheel","source-generated:NotesColorWheelView.wheel",{8,8,166,166},{1,1,1,1},192,false,false},Json::Object{{"asset","raster/"+std::string(wheel)+".png"},{"sha256",std::string(wheel)}}};
    return value;
}
void Application::Impl::projectionCommand(const ProjectionHandoff::Command&command,double time){
    using A=ProjectionHandoff::Action;
    if(command.generation!=projectionHandoff.generation())return;
    switch(command.action){
    case A::closeHUD:perform(lifecycle.close(true),time);if(!closing)projectionHandoff.cancel();break;
    case A::showProjection:{
        host.hide();composition.detach(renderer);published.clear();publishedNotesRevision=0;
        if(!projection){const auto a=watchAppearance.accentSRGB;projection=std::make_unique<tools::ProjectionWorkspace>(host,renderer,rasterizer,backdrop,*mediaBroker,modules::NotesColor{a[0],a[1],a[2],1},configuration.number("backgroundDarkness"),configuration.number("blurAmount"));}
        tools::ProjectionMediaBindingOptions binding;
        binding.resolve=[](const modules::ProjectionMediaReference&ref){return gpu::NotesImageAccess{utf8Path(ref.path()),ref.accessLease()};};
        binding.errorText=[this](HRESULT){return core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language);};
        projectionPainting=true;try{projection->resize(metrics,projectionWorkTopPixels/metrics.scale);projection->show(projectionOptions(),std::move(binding),time);projectionDropRoute(true,time);projectionPainting=false;}catch(...){projectionPainting=false;projectionHandoff.cancel();throw;}
        refresh(now());break;
    }
    case A::closeProjection:projectionDropRoute(false,time);cancelProjectionPicker();if(projection)projection->dismiss(time,settings.reduceMotion);refresh(time);break;
    case A::showHUD:projectionDropRoute(false,time);if(projection)projection->close(time);published.clear();publishedNotesRevision=0;perform(lifecycle.openOverlay(),time);break;
    case A::external:{projectionDropRoute(false,time);if(projection)projection->close(time);published.clear();publishedNotesRevision=0;
        const auto action=command.externalID?static_cast<gpu::TrayAction>(*command.externalID):gpu::TrayAction::openOverlay;
        if(action==gpu::TrayAction::quit){perform(lifecycle.requestTermination(),time);break;}
        trayAction(action==gpu::TrayAction::none?gpu::TrayAction::openOverlay:action,false,time);
        break;
    }
    case A::closeImmediately:projectionDropRoute(false,time);cancelProjectionPicker();if(projection)projection->close(time);published.clear();publishedNotesRevision=0;break;
    }
}
} // namespace endfield::app
