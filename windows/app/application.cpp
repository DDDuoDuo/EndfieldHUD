// EndfieldHUD application owner. The lifecycle, module wiring and event routes
// below were extracted from the former build-only watch_session_preview wmain
// without changing their behavior; the copy-pasted if(owner) chains became the
// ModuleRegistry (app/module_owner.hpp). Production mode adds the Mac
// AppDelegate/OverlayController policy (core/system_overlay_state.hpp), the
// persistent versioned data root, real providers, tray, first run, session-end
// durability and per-module failure isolation.
#include "app/application_impl.hpp"
#include "app/application_persistence.hpp"
#include "app/areas/projection_automation.hpp"
#include "app/areas/calendar_reminder_module.hpp"
#include "app/areas/update_service.hpp"
#include <cctype>
#include "core/application_arguments.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <cwchar>
#include <iostream>
#include <shellapi.h>
#include <wrl/client.h>
#include <psapi.h>
#include <dwmapi.h>
#include <dxgi.h>
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
// The Projection workspace's failure-isolation slot (Impl::projectionCall):
// it draws, routes and lists nothing of its own.
class ProjectionIsolation final:public ModuleOwner{public:std::string_view name()const noexcept override{return "projectionWorkspace";}};
constexpr std::string_view repositoryURL="https://github.com/DDDuoDuo/EndfieldHUD",releasesURL="https://github.com/DDDuoDuo/EndfieldHUD/releases";
// Windows-only text in the five source languages (the Mac catalog has no entry).
std::string text5(core::Language l,std::string_view en,std::string_view hans,std::string_view hant,std::string_view ja,std::string_view ko){
    switch(l){case core::Language::simplifiedChinese:return std::string(hans);case core::Language::traditionalChinese:return std::string(hant);
    case core::Language::japanese:return std::string(ja);case core::Language::korean:return std::string(ko);default:return std::string(en);}
}
// Fallback without the windowsUpdates area (development previews, a disabled
// area): the About page's update row opens the GitHub releases page, and
// automatic installation is reported unavailable (canSetAutomatic=false).
modules::SettingsUpdateStatus updateStatus(core::Language l){
    modules::SettingsUpdateStatus u;
    u.title=text5(l,"Open GitHub releases ↗","打开 GitHub 发布页 ↗","打開 GitHub 發佈頁 ↗","GitHub のリリースを開く ↗","GitHub 릴리스 열기 ↗");
    u.detail=text5(l,"Automatic updates are not available on Windows yet. New versions are published on GitHub.","Windows 版暂不支持自动更新，新版本发布在 GitHub。",
        "Windows 版暫不支援自動更新，新版本發佈於 GitHub。","Windows 版ではまだ自動アップデートを利用できません。新しいバージョンは GitHub で公開されます。","Windows 버전은 아직 자동 업데이트를 지원하지 않습니다. 새 버전은 GitHub에 게시됩니다.");
    u.releaseURL=std::string(releasesURL);u.canCheck=true;u.canSetAutomatic=false;u.automaticallyInstalls=false;
    return u;
}
}
// Stable display identity across topology changes (HMONITOR handles are not).
std::string displayKey(const gpu::DisplayDescriptor&d){
    if(d.persistentID.empty())return "handle:"+std::to_string(d.handle);
    return narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.persistentID.data()),d.persistentID.size()));
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
    constexpr std::array names{"frame","backdrop","sourcePose","materials","nativeLabels","notesPose","publication","draw","pointer","key","message","editTransition","prepare"};
    for(unsigned i=0;i<count;++i){const auto&t=timings[i];stages[names[i]]=Json::Object{{"calls",std::int64_t(t.calls)},{"totalMS",t.total},{"meanMS",t.calls?t.total/t.calls:0},{"maxMS",t.maximum}};}
    const auto input=gpu::textInputDiagnostics();auto timing=[](const auto&t){return Json::Object{{"calls",std::int64_t(t.calls)},{"totalMS",t.totalMilliseconds},{"maxMS",t.maximumMilliseconds}};};
    // The owners whose frame update or upload took longest in this phase.
    std::array<std::size_t,64> order{};for(std::size_t n=0;n<ownerCount;++n)order[n]=n;
    std::sort(order.begin(),order.begin()+ownerCount,[&](std::size_t a,std::size_t b){return owners[a].update.maximum+owners[a].upload.maximum>owners[b].update.maximum+owners[b].upload.maximum;});
    Json::Array slowest;for(std::size_t n=0;n<ownerCount&&n<8;++n){const auto&o=owners[order[n]];
        slowest.push_back(Json::Object{{"owner",std::string(o.name)},{"updateTotalMS",o.update.total},{"updateMaxMS",o.update.maximum},{"uploadTotalMS",o.upload.total},{"uploadMaxMS",o.upload.maximum}});}
    rows.push_back(Json::Object{{"phase",phase},{"elapsedSeconds",time-phaseStart},{"stages",std::move(stages)},{"slowestOwners",std::move(slowest)},
      {"input",Json::Object{{"notifyLayout",timing(input.notifyLayout)},{"requestLock",timing(input.requestLock)},{"getTextExt",timing(input.getTextExt)},
      {"postAttempts",std::int64_t(input.postAttempts)},{"newPosts",std::int64_t(input.newPosts)},{"layoutOnlyPosts",std::int64_t(input.layoutOnlyPostAttempts)},
      {"placementChanged",std::int64_t(input.placementChanged)},{"placementEqual",std::int64_t(input.placementEqual)},{"locksDuringLayout",std::int64_t(input.locksDuringLayout)},{"extentsDuringLayout",std::int64_t(input.extentsDuringLayout)}}}});
}
void LiveProbe::next(int value,double time){flush(time);phase=value;phaseStart=time;timings={};owners={};ownerCount=0;gpu::setTextInputDiagnosticsEnabled(true);}
namespace {
double sinceMS(Clock::time_point from,Clock::time_point to){return std::chrono::duration<double,std::milli>(to-from).count();}
// Milliseconds since this process was created (GetProcessTimes, system time).
std::optional<double> processAgeMS(){
    FILETIME created,exited,kernel,user,now;if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return std::nullopt;
    GetSystemTimePreciseAsFileTime(&now);
    const auto value=[](FILETIME t){return (double(t.dwHighDateTime)*4294967296.+t.dwLowDateTime)*1e-4;};
    return value(now)-value(created);
}
}
StartupTrace::StartupTrace(Clock::time_point launch):launch_(launch){for(auto&o:openings_)o.frames.reserve(frameLimit);}
void StartupTrace::opening(Clock::time_point at){
    if(count_>=openings_.size())return;
    auto&o=openings_[count_++];o.requestedMS=sinceMS(launch_,at);recording_=true;
}
void StartupTrace::frame(Clock::time_point begin,Clock::time_point end){
    if(!launchToFirstFrameMS_){launchToFirstFrameMS_=sinceMS(launch_,end);processToFirstFrameMS_=processAgeMS();}
    if(!recording_||!count_)return;
    auto&o=openings_[count_-1];if(o.firstFrameMS<0)o.firstFrameMS=sinceMS(launch_,end)-o.requestedMS;
    if(o.frames.size()<frameLimit)o.frames.push_back({sinceMS(launch_,end)-o.requestedMS,sinceMS(begin,end)});
}
void StartupTrace::opened(Clock::time_point at){
    if(!recording_||!count_)return;
    auto&o=openings_[count_-1];o.openedMS=sinceMS(launch_,at)-o.requestedMS;recording_=false;
}
void StartupTrace::stage(const char*name,Clock::time_point begin,Clock::time_point end){
    if(stages_.size()<64)stages_.push_back(Json::Object{{"stage",name},{"startMilliseconds",sinceMS(launch_,begin)},{"milliseconds",sinceMS(begin,end)}});
}
Json StartupTrace::report(const Json::Array&startupStages,double preparedMS)const{
    Json::Array openings;
    for(std::size_t n=0;n<count_&&n<openings_.size();++n){const auto&o=openings_[n];
        double cpu=0,maxCPU=0,maxInterval=0,previous=0;std::int64_t over16{},over25{},late{};Json::Array intervals,costs;
        for(const auto&f:o.frames){cpu+=f.cpuMS;maxCPU=std::max(maxCPU,f.cpuMS);const double interval=f.atMS-previous;previous=f.atMS;
            maxInterval=std::max(maxInterval,interval);over16+=f.cpuMS>1000./60;over25+=interval>25;late+=interval>1000./60*1.5;
            intervals.push_back(interval);costs.push_back(f.cpuMS);}
        openings.push_back(Json::Object{{"index",std::int64_t(n)},{"cold",n==0},{"requestedAtMilliseconds",o.requestedMS},
            {"requestToFirstFrameMilliseconds",o.firstFrameMS},{"requestToOpenedMilliseconds",o.openedMS},{"frames",std::int64_t(o.frames.size())},
            {"meanFrameCPUMilliseconds",o.frames.empty()?0.:cpu/double(o.frames.size())},{"maxFrameCPUMilliseconds",maxCPU},
            {"framesOverOneRefreshCPU",over16},{"maxFrameIntervalMilliseconds",maxInterval},{"intervalsOver25Milliseconds",over25},{"intervalsOverOneAndHalfRefresh",late},
            {"frameIntervalsMilliseconds",std::move(intervals)},{"frameCPUMilliseconds",std::move(costs)}});}
    return Json::Object{{"schema",1},{"numericOnly",true},{"scope","EndfieldHUD startup and opening timing; no text, paths or document content"},
        {"preparationMilliseconds",preparedMS},{"launchToFirstFrameMilliseconds",launchToFirstFrameMS_.value_or(-1)},{"processToFirstFrameMilliseconds",processToFirstFrameMS_.value_or(-1)},
        {"startupStages",startupStages},{"prewarm",stages_},{"openings",std::move(openings)}};
}

// ---------------------------------------------------------------------------
// Construction
Application::Impl::Impl(ApplicationOptions options)
    :args(std::move(options)),faults(args.mode!=ApplicationMode::production&&(args.mode==ApplicationMode::visiblePreview||args.moduleCoverage)),
     headerClock([]{SYSTEMTIME value{};GetLocalTime(&value);return modules::LocalClockFields{value.wMonth,value.wDay,value.wDayOfWeek,value.wHour,value.wMinute,value.wSecond};}),
     registry(args.isolateModuleFailures){
    focused=!visibleMode();
    if(args.readerScrollTrace)readerTrace=std::make_unique<ReaderScrollTrace>();
    probe.enabled=!args.liveDiagnostics.empty();
    if(!args.startupTrace.empty())startupTrace=std::make_unique<StartupTrace>(preparation);
    if(!args.memoryTrace.empty())memoryTrace=std::make_unique<MemoryTrace>(args.memoryTrace);
    // A failed startup still tears owners down in the established order
    // before the HWND, renderer and COM members are destroyed.
    try{construct();}catch(...){try{cleanup();}catch(...){}throw;}
}
Application::Impl::~Impl(){try{cleanup();}catch(...){}}

void Application::Impl::registerModule(std::unique_ptr<ModuleOwner>&owner){registry.add(*owner);}
// Startup trace openings: the frame stages (LiveProbe) of each traced opening
// are recorded with it, unless live diagnostics own the probe.
void Application::Impl::traceOpening(double time){
    const bool recording=startupTrace->recordsOpening();startupTrace->opening(Clock::now());
    if(recording&&args.liveDiagnostics.empty()){probe.enabled=true;probe.next(static_cast<int>(probe.rows.size()),time);}
}
void Application::Impl::traceOpened(double time){
    if(!startupTrace)return;startupTrace->opened(Clock::now());
    if(args.liveDiagnostics.empty()&&probe.enabled){probe.flush(time);probe.phase=-1;probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);}
}
// Completions of work an owner submitted from its own call run under that
// owner's isolation (the drain runs outside any owner call).
void Application::Impl::attributeCompletions(UtilityExecutor&executor){
    executor.attribute([this]()->UtilityExecutor::Attribution{return registry.current();},
        [this](UtilityExecutor::Attribution tag,const std::function<void()>&call){
            if(auto*owner=registry.registered(tag))(void)registry.guard(*owner,call,"utility completion");else call();});
}
void Application::Impl::unregisterModule(std::unique_ptr<ModuleOwner>&owner)noexcept{if(owner){registry.remove(*owner);owner.reset();}}
// Projected UI Automation of the built-in owners: a press uses the shared
// clock like a click; the content event presents and runs the host refresh
// (which also takes actions a press queued, e.g. the Reader's file chooser).
native::ScreenReaderPeerHooks Application::Impl::peerHooks(){return {host.hwnd(),[this]{return areaClock();},[this]{automationChanged();refresh(now());}};}
VolumeModuleHooks Application::Impl::screenHooks(){
    VolumeModuleHooks hooks;hooks.clock=[this]{return areaClock();};hooks.changed=[this]{automationChanged();refresh(now());};
    hooks.toScreen=[this](core::Rect r)->std::optional<core::Rect>{
        POINT origin{};if(!ClientToScreen(static_cast<HWND>(host.hwnd()),&origin))return std::nullopt;
        const double scale=metrics.scale;return core::Rect{origin.x+r.x*scale,origin.y+r.y*scale,r.width*scale,r.height*scale};};
    return hooks;
}

void Application::Impl::construct(){
    // The closed HUD's graphics grace: production keeps the ten minutes,
    // except an isolated --data-root run (the section 9 gate) that asked for
    // a shorter one through ENDFIELDHUD_CLOSED_GRAPHICS_GRACE (app/main.cpp).
    need(std::isfinite(args.closedGraphicsGrace)&&args.closedGraphicsGrace>=1&&args.closedGraphicsGrace<=86400&&
         (!production()||args.isolatedDataRoot||args.closedGraphicsGrace==closedGraphicsGraceSeconds),
         "The closed graphics grace is 1 s...1 day, ten minutes in production outside an isolated data root");
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
    idCardBaseline=desktop; // the bottom-left ID card's packet baseline (IdCardPresenter)
    desktopBaseline=desktop;findCenterLogoNodes(); // center logo overrides without the presenter
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
    if(profile)if(const auto action=contentCatalog->actionForTarget("profile")){sessionNavigation.fixedActions[profile->rootID]=*action;for(const auto&id:profile->buttonIDs)sessionNavigation.fixedActions[id]=*action;
        // The profile card's one accessibility button (HUDSourceWatchView.accessibilityHitIDs).
        profileCardRoot=profile->rootID;profileCardButtons.assign(profile->buttonIDs.begin(),profile->buttonIDs.end());}
    // HUDSourceWatchView: desktop QuitBtn nodes and close buttons are fixed
    // source buttons with the same clipped hit test and _clickCd cooldown.
    for(const auto&node:scene->nodes()){
        if(!document->component("UIButton",node.id))continue;
        const std::string_view path=node.path;
        if(node.name=="QuitBtn"){quitButtons.insert(node.id);sessionNavigation.fixedActions.try_emplace(node.id,quitAction);}
        else if(path.ends_with("/CloseButtonNode/Btn_BackNode")||path.ends_with("/FullScreenCloseBtn"))sessionNavigation.fixedActions.try_emplace(node.id,closeAction);
    }
    baseNavigation=sessionNavigation; // saved app shortcuts join it (bindNavigation)
    session->setNavigation(std::move(sessionNavigation),0);
    if(args.settingsAssets.empty())nativeContent=std::make_unique<gpu::NativeWatchContent>(*contentCatalog,layers,rasterOptions);
    else{const auto file=args.settingsAssets/"watch-appearance"/"manifest.json";const auto bytes=ehud::data::detail::readFile(file,1024*1024);need(bytes&&packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()))=="1c623124d48d094e8897b89598bc6791a8d369bc51ef8bc87c928ef5915444de","Original desktop appearance pin differs");appearanceTemplates=std::make_unique<gpu::WatchAppearanceTemplates>(Json::parse(*bytes));auto ro=rasterOptions;ro.assetRoot=file.parent_path();nativeAppearance=std::make_unique<gpu::NativeWatchAppearance>(*appearanceTemplates,*scene,labelPlan->bindings(),layers,rasterizer,ro);}startup.mark("native-bindings-content-catalog");
    labels=std::make_unique<gpu::WatchLabelPresentation>(*labelPlan,layers);for(const auto&id:labelPlan->buttonIDs())available.push_back({id,false,false});
    chromeContent.reading=source::DesktopClockReading{"12:34:56","WED Oct 7"};chromeContent.uppercaseShortcut="CTRL + `";chromeContent.localizedClose="Click outside to close";
    if(includesChrome){chromeBindings=source::DesktopChromeBindings::fromJson(chromeJSON["bindings"]);chromePlan=std::make_unique<source::DesktopChromeProjectionPlan>(*scene,*camera,*animation,*chromeBindings);chrome=std::make_unique<gpu::NativeChromePresentation>(*chromePlan,layers,chromeJSON,rasterOptions,rasterizer);
        chrome->setContent(chromeContent);}startup.mark("chrome-artwork");
    // A packet without the exported profile captions keeps the packet card.
    try{idCardLeaves=gpu::idCardCaptionLeaves(top["nativeLayers"]);}catch(const std::exception&){}
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
        hudDisplay=displays[*selected].handle;hudDisplayID=displayKey(displays[*selected]);previousApplication=reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
        windowOptions.coverMonitor=true;
        const auto bounds=displays[*selected].bounds;
        windowOptions.x=bounds.left;windowOptions.y=bounds.top;
        windowOptions.pixelWidth=static_cast<std::uint32_t>(bounds.right-bounds.left);
        windowOptions.pixelHeight=static_cast<std::uint32_t>(bounds.bottom-bounds.top);
    }
    if(!visibleMode()){windowOptions.pixelWidth=args.benchmarkWidth;windowOptions.pixelHeight=args.benchmarkHeight;
        // The idle trace schedules frames for its never-shown window at the traced display refresh.
        if(!args.idleTrace.empty()){windowOptions.offscreenScheduling=true;windowOptions.refreshRate=args.idleTraceRefreshRate;}}
    host.create(windowOptions,callbacks());metrics=host.metrics();updateContentScale();startup.mark("hidden-window");
    {gpu::RendererOptions rendererOptions{args.warp?gpu::Driver::warpForTests:gpu::Driver::hardware,args.shader,visibleMode()?gpu::RenderTarget::composition:gpu::RenderTarget::offscreenForTests,!args.dataRoot.empty()};
     // The HUD submits a few hundred quads per frame from one thread: the
     // driver's internal threading optimizations bring it nothing, while on
     // the measured NVIDIA driver they keep a worker pool (41 threads at
     // launch, 59 after use) and large command queues for the device.
     rendererOptions.preventDriverThreads=true;
     renderer.initialize(host.hwnd(),metrics.pixelWidth,metrics.pixelHeight,rendererOptions);}startup.mark("renderer-device-target");
    if(visibleMode()){
        backdropQueue=std::make_unique<BackdropQueue>();
        const BOOL known=FALSE;const auto result=DwmSetWindowAttribute(static_cast<HWND>(host.hwnd()),DWMWA_USE_HOSTBACKDROPBRUSH,&known,sizeof(known));
        // Without it (before Windows 11 build 22000) the HUD cannot draw its
        // blurred background: EndfieldHUD.exe reports that Windows 11 is required.
        if(FAILED(result))throw SystemBackdropUnavailable(static_cast<std::int32_t>(result));
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
    // The source navigation's accessibility buttons lie beneath every module
    // canvas (HUDSourceWatchView under the module views): registered first,
    // they answer UI Automation hit tests last.
    constructNavigationAutomation();
    if(!args.notesAssets.empty())constructNotes();
    if(!args.shelfAssets.empty()&&notes)optional("file shelf",[&]{constructShelf();});
    materials->upload(renderer.sourceGraphics());publishNative();host.setCursor(cursor.handle());ready=true;startup.mark("initial-gpu-upload");
    environment.viewport={metrics.width,metrics.height};const auto start=visibleMode()?now():hostTime(args.benchmarkEpoch);session->setEnvironment(environment,start);
    if(args.nativeClipboard||args.nativeActivity||args.reader||args.calendar||!args.mapGeography.empty()||args.eventLogFixture||args.nativeEventLog||!args.settingsAssets.empty()||!args.archiveAssets.empty()||!args.storageAssets.empty()||!args.modules.empty()){
        const auto window=static_cast<HWND>(host.hwnd());utility=std::make_unique<UtilityExecutor>([window]{need(PostMessageW(window,utilityMessage,serviceGeneration,0)!=FALSE,"Post utility completion");},32,utilityIdleExit,"EndfieldHUD utility");
        attributeCompletions(*utility);
        // The Shelf reads item properties and writes Migration/relink.json on the
        // shared executor (tools/shelf_preview.hpp); it was constructed before it.
        if(shelf)isolate(shelfModuleOwner,"utility",[&]{shelf->setUtility(utility.get());});
        // Notes' Mac import relink ledger writes (relinks, deletions) run on the
        // shared executor; the teardown destroys Notes before the executor.
        if(notes)notes->setUtilityExecutor(utility.get());
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
    if(args.nativeVolume)optional("audio",[&]{constructAudio();});
    if(args.volumeFixture||args.nativeVolume)optional("volume",[&]{constructVolume();});
    if(!args.settingsAssets.empty())optional("settings",[&]{constructSettings();});
    if(args.projection){projectionIsolation=std::make_unique<ProjectionIsolation>();registry.add(*projectionIsolation);}
    constructAreas();
    connectAreas();
    if(!args.residentIcons.empty())constructTray();
    if(production()){
        // Display-off and session lock/disconnect notifications for suspend.
        const auto window=static_cast<HWND>(host.hwnd());
        displayNotification=RegisterPowerSettingNotification(window,&consoleDisplayState,DEVICE_NOTIFY_WINDOW_HANDLE);
        sessionNotification=WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION)!=FALSE;
    }
    // An isolated data root (--data-root, the section 9 gate) never writes a startup entry.
    if(production()&&!args.isolatedDataRoot){std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(length&&length<path.size()){path.resize(length);loginItem=std::make_unique<LoginItemRegistration>(LoginItemLocation::production(fs::path(path)));}}
    if(notes&&chromePlan){quitConfirmation=makeQuitConfirmation(rasterizer,QuitConfirmationCallbacks{
        [this](double time){perform(lifecycle.requestQuit(),time);},[this](double time){refresh(time);}});quitConfirmation->resize(metrics);registry.add(*quitConfirmation);}
    if(notes)constructClockAutomation(); // SystemHUDView.layoutClockControls
    if(!production()){
        std::cout<<(archive?"Synthetic module preview with Archive, rich Notes and shared media; all files remain references in the explicit temporary data root. ESC finishes editing, then animates closing.\n":notes?"Synthetic shell with rich Notes, File Shelf and isolated Clipboard history; other modules and remaining Notes tools are not connected yet. ESC finishes editing, then animates closing.\n":"Synthetic source-shell feasibility only: no module bodies/providers; font substitutions and explicit source-content variant coverage remain. ESC animates closing.\n");
        if(activity)std::cout<<(args.nativeActivity?"Native Activity reads bounded Windows counters on the shared worker; per-app disk/network remain unavailable.\n":"Activity preview uses synthetic app and system readings only.\n");
        if(calendar)std::cout<<"Calendar preview keeps events in the explicit temporary root; notification delivery is injected and never contacts the OS notification center.\n";
        if(storage)std::cout<<(args.moduleCoverage?"Storage coverage uses injected synthetic capacity/details and a no-op settings action.\n":"Development Storage preview: startup-volume capacity is read only while selected; Settings opens only on explicit click; no automatic folder scan.\n");
    }
    // Without the Settings store the launch prewarm follows construction.
    if(production()&&!settingsSaves)(void)PostMessageW(static_cast<HWND>(host.hwnd()),prewarmMessage,serviceGeneration,0);
    startup.mark("all-preview-owners-ready");preparedMS=milliseconds(preparation);
    recordMemory("constructed",true);
}

// ---------------------------------------------------------------------------
// Module areas (app/module_owner.hpp, "Adding a module area")
ApplicationAreaMode Application::Impl::areaMode()const noexcept{
    return args.mode==ApplicationMode::production?ApplicationAreaMode::production:
        args.mode==ApplicationMode::visiblePreview?ApplicationAreaMode::developmentPreview:ApplicationAreaMode::hiddenCoverage;
}
void Application::Impl::constructAreas(){
    if(args.modules.empty())return;
    areaHost=std::make_unique<AreaHost>(*this);
    const auto mode=areaMode();
    // Host services published before any area factory runs (exact types; the
    // area headers name the keys). clearServices() withdraws them at teardown.
    if(shelf){
        areaHost->publish<tools::ShelfPreview>(tools::ShelfPreview::serviceKey,*shelf);
        // Source HUDMediaAssemblyInteraction.showShelf: list and open Shelf rows;
        // the area releases the lease after its import.
        areaShelfChoices=[this]{return shelfChoices();};
        areaShelfOpen=[this](std::string_view id)->std::optional<std::pair<std::string,std::shared_ptr<void>>>{
            if(!shelf)return std::nullopt;
            auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(id));
            if(!lease->open()||lease->metadata().isDirectory)return std::nullopt;
            auto path=lease->metadata().windowsPath;
            return std::pair<std::string,std::shared_ptr<void>>{std::move(path),std::move(lease)};};
        areaHost->publish("fileShelf.mediaChoices",areaShelfChoices);
        areaHost->publish("fileShelf.openReference",areaShelfOpen);
    }
    // The Mac runs Media Assembly exports on their own queue: explicit long
    // jobs get a second host executor so Notes/Settings/Event Log saves never
    // wait behind an export. Completions post the same utility notice.
    if(utility){
        const auto window=static_cast<HWND>(host.hwnd());
        longWork=std::make_unique<UtilityExecutor>([window]{PostMessageW(window,utilityMessage,serviceGeneration,0);},32,utilityIdleExit,"EndfieldHUD long work");attributeCompletions(*longWork);
        areaHost->publish("app.longWorkExecutor",*longWork);
    }
    // The Power page shows the area's merged reading (with the mWh pair).
    if(battery)areaHost->publish<power::PowerPageSink>(power::powerPageSinkKey,batteryPageSink);
    // HUDCalendarController didBecomeActive -> refreshForSystemChange: a
    // consumed Calendar reminder click refreshes Calendar once.
    if(calendar){
        calendarReminderRefresh=[this](double time){if(calendar)isolate(calendarModuleOwner,"system change",[&]{calendar->systemChanged(hostTime(time));updateCalendarWake();});};
        areaHost->publish<endfield::areas::CalendarReminderActivationSink>(endfield::areas::calendarReminderActivationKey,calendarReminderRefresh);
    }
    // Committing a Mac import needs every store closed: the accepted-quit
    // teardown, then a fresh run (main.cpp). Production only.
    if(production())areaHost->publish<DataRootRestart>(dataRootRestartService,restart);
    // Read-only Event Log for areas: systemProviders hidden coverage checks every host action hook's recorded vocabulary.
    if(eventOwner)areaHost->publish<gpu::EventLogOwner>(gpu::eventLogOwnerServiceKey,*eventOwner);
    for(auto&[name,factory]:args.modules){
        if(!factory)continue;
        // A gate run over an isolated root never touches the shared toast
        // identity: scheduled reminders of the user's copy share its AUMID.
        if(args.isolatedDataRoot&&name=="calendarReminders"){absentModules.push_back(name);continue;}
        const auto build=[&]{
            fs::path coverageRoot;
            if(mode==ApplicationAreaMode::hiddenCoverage){
                // A NEW directory per area inside the temporary data root.
                need(!args.dataRoot.empty(),"Area coverage needs the temporary data root");
                std::string folder;for(const char c:name)folder.push_back(std::isalnum(static_cast<unsigned char>(c))?c:'-');
                coverageRoot=args.dataRoot/"Coverage"/utf8Path(folder);
                need(!fs::exists(coverageRoot),"Area coverage roots are new");fs::create_directories(coverageRoot);
            }
            const ApplicationModuleContext context{rasterizer,host.hwnd(),utility.get(),args.dataRoot,
                notes?static_cast<void*>(notes->activatedTextManager()):nullptr,notes?static_cast<std::uint32_t>(notes->textClient()):0u,metrics,
                [this]{return areaClock();},[this]{if(ready&&!stopping)host.invalidate();},
                [this](modules::EventKind kind,modules::EventMetadata metadata){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),kind,ehud::data::foundationNow(),std::move(metadata)});},
                [this](std::string_view id)->fs::path{return args.resources?args.resources(id):fs::path{};},
                name,mode,areaHost.get(),coverageRoot,
                [this,mode](std::string_view id)->fs::path{return mode==ApplicationAreaMode::hiddenCoverage&&args.fixtures?args.fixtures(id):fs::path{};}};
            areaHost->constructing(name);
            auto owner=factory(context);
            areaHost->constructing({});
            if(!owner){areaHost->abandon(name);absentModules.push_back(name);return;}
            owner->resize(metrics);registry.add(*owner);externalModules.push_back({name,std::move(owner)});configurationPending=true;
        };
        // Each area's construction is its own startup stage (the startup trace).
        const auto stage="area:"+name;
        if(!production()){build();startup.mark(stage.c_str());continue;}
        // Production isolates each area's startup like the optional built-ins.
        try{build();}
        catch(const std::exception&e){
            areaHost->constructing({});areaHost->abandon(name);absentModules.push_back(name);
            const auto line=std::string("EndfieldHUD module ")+name+" could not start ("+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
        }
        startup.mark(stage.c_str());
    }
    startup.mark("module-areas");
}
// Host wiring that needs the constructed areas (each optional).
void Application::Impl::connectAreas(){
    if(!areaHost)return;
    const auto optional=[&](const char*name,auto&&build){
        if(!production()){build();return;}
        try{build();}catch(const std::exception&e){const auto line=std::string("EndfieldHUD module ")+name+" could not start ("+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
    };
    // Bottom-left ID card (HUDSourceWatchView.setDesktopProfile): the profile
    // area publishes the feed; the host owns the label scene, SourceScene and
    // the source session's desktop settings.
    if(idCardLeaves&&idCardBaseline)optional("id card",[&]{
        if(auto*feed=areaHost->find<gpu::IdCardFeed>(gpu::IdCardFeed::serviceKey)){
            idCard=std::make_unique<gpu::IdCardPresenter>(layers,std::move(*idCardLeaves),rasterOptions,*materials,std::move(*idCardBaseline));
            // The presenter's callback only schedules work. While the HUD is
            // concealed the change is applied as its own content event (captions
            // repainted, textures replaced for the next SourceScene flush), so a
            // profile published after launch never lands in the first opening's
            // first frame; while shown, the next frame applies it.
            idCard->connect(*feed,[this]{if(!ready||stopping)return;
                if(visibleMode()&&session->phase()==core::VisibilityPhase::concealed){
                    if(!idCardQueued&&PostMessageW(static_cast<HWND>(host.hwnd()),idCardMessage,serviceGeneration,0))idCardQueued=true;}
                else host.invalidate();});
        }});
    idCardLeaves.reset();idCardBaseline.reset();
    // Saved app shortcuts between Power and Add App (app_shortcut_navigation.hpp).
    if(auto*apps=appNavigation()){apps->setObserver([this]{navigationDirty=true;if(ready&&!stopping)host.invalidate();});navigationDirty=true;}
    // Mac toggleSystemOverlay/openSettingsModule refuse while the battery
    // position editor is open.
    if(auto*p=powerArea())p->setEditingObserver([this](bool editing){lifecycle.setEditingPosition(editing);});
    // Areas are constructed after Settings: the System page's area rows
    // ("Import macOS data…") appear now.
    if(settingsUI)settingsUI->refreshViews(areaClock());
    // The windowsUpdates area owns About's update rows and the tray item
    // (AppDelegate.configureUpdater); its observer runs after any change.
    if(auto*u=updates()){u->setObserver([this]{refreshUpdateStatus();if(production())updateTrayMenu();});refreshUpdateStatus();}
}
UpdateService* Application::Impl::updates()const noexcept{return areaHost?areaHost->find<UpdateService>(UpdateService::key):nullptr;}
ModuleOwner* Application::Impl::serviceOwner(std::string_view key)const noexcept{
    if(!areaHost)return nullptr;const auto name=areaHost->publisher(key);if(name.empty())return nullptr;
    for(const auto&area:externalModules)if(area.name==name)return area.owner.get();
    return nullptr;
}
void Application::Impl::refreshUpdateStatus(){
    if(!settingsUI)return;const auto l=settingsLanguage(configuration);
    std::optional<modules::SettingsUpdateStatus> status;
    if(auto*u=updates())(void)isolateService(UpdateService::key,"update status",[&]{auto value=u->settingsStatus(l);value.automaticTitle=u->automaticTitle(l);
        // The area's toggle label meets the same bound SettingsController::setUpdateStatus applies to the other strings.
        need(value.automaticTitle.size()<=16*1024&&Json::validUtf8(value.automaticTitle),"Invalid bounded Settings status");
        status=std::move(value);});
    settingsUI->controller().setUpdateStatus(status?std::move(*status):updateStatus(l));
}
power::PowerAreaService* Application::Impl::powerArea()const noexcept{return areaHost?areaHost->find<power::PowerAreaService>(power::PowerAreaService::key):nullptr;}
AppShortcutNavigation* Application::Impl::appNavigation()const noexcept{return areaHost?areaHost->find<AppShortcutNavigation>(appShortcutNavigationService):nullptr;}
bool Application::Impl::broadcast(HostEvent::Kind kind,double time,std::uint32_t flags){
    time=hostTime(time);if(!visibleMode())hiddenClock=std::max(hiddenClock,time);
    const bool changed=registry.broadcast({kind,flags},time);
    if(changed)automationChanged();
    if(changed&&ready&&!stopping)refresh(time);
    return changed;
}
void Application::Impl::concealed(double time){
    presentationPriority.restore(); // a closed HUD never competes with the foreground app
    if(concealedBroadcast)return;concealedBroadcast=true;
    // The hidden HUD has no projected controls for UI Automation.
    if(automation)automation->disconnectOwners();
    broadcast(HostEvent::Kind::overlayConcealed,time);
    releaseConcealedGraphics();
    // ENDFIELDHUD_MEMORY_TRACE: the first closings (the hidden run records its own stages).
    if(memoryTrace&&production()&&memoryTrace->closings<8)recordMemory("closed-"+std::to_string(++memoryTrace->closings),true);
}
// Nothing is drawn until the next opening. One publication lets the owners
// that released their artwork on the overlayConcealed broadcast retire its GPU
// resources now (instead of at the next opening), then the renderer drops
// what only a drawn frame uses (the linear composition target, the original
// shell pass's attachment and depth target and the native group targets,
// created again by the frames that draw them) and the driver trims its
// internal buffers. Video memory is charged to this process's commit, so this
// is what the closed HUD gives back (hidden memory trace on the laptop's
// NVIDIA GPU, with the four-byte textures: 229 -> 189 MiB private at launch
// idle, 543 -> 335 MiB after the first close, 548 -> 255 MiB after ten more
// open/close cycles).
void Application::Impl::releaseConcealedGraphics(){
    if(!ready||stopping||!renderer.stats().initialized)return;
    try{publishNative();(void)renderer.releaseFrameTargets();}
    catch(const std::exception&e){
        if(!production())throw;
        const auto line=std::string("EndfieldHUD could not release the closed HUD's graphics (")+typeid(e).name()+")\n";
        std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
    }
    // The process heaps keep the blocks freed by the owners' releases (raster
    // and decode buffers, document models) committed: decommit them as well.
    HEAP_OPTIMIZE_RESOURCES_INFORMATION heaps{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};
    (void)HeapSetInformation(nullptr,HeapOptimizeResources,&heaps,sizeof(heaps));
    // Still closed after the grace: the presentation and the device go too.
    idleGraphicsAttempted=idleGraphicsRefused=false;graphicsIdleAt=now()+closedGraphicsGrace();scheduleDeadline();
}
// The HUD stayed closed for closedGraphicsGrace() seconds (a host deadline,
// ten minutes in production): nothing presents until the next opening, so
// neither the device (the driver thread that wakes ~70 times a second while
// a D3D11 device merely exists) nor the swap chain and composition target
// stay alive. Every retained resource keeps its identity in system memory;
// the opening starts creating the device again and its first frame takes
// it. The Projection workspace presents through the same renderer and keeps
// them. So does a media owner still holding the device (Renderer::
// suspendDevice refuses): the media owners take it only while media plays
// or is edited and drop it once idle (Notes, Archive and Projection movies
// through the shared video owner, Media Assembly's processors), and the
// release follows shortly after the last one let go (scheduleDeadline).
void Application::Impl::releaseIdleGraphics(double time){
    (void)time;graphicsIdleAt.reset();
    if(!ready||stopping||!renderer.stats().initialized||session->phase()!=core::VisibilityPhase::concealed||
       projectionHandoff.active()||projectionShown())return;
    idleGraphicsAttempted=true;idleGraphicsRefused=false;
    try{
        // Media the closed owners retired after the concealment (a movie
        // poster kept for its fade) leaves the composition and the renderer
        // first; nothing of it is copied aside.
        if(!renderer.deviceSuspended())publishNative();
        // Only together: a device a media owner still holds keeps its swap chain too.
        idleDeviceSuspended=renderer.suspendDevice()||renderer.deviceSuspended();
        idleGraphicsRefused=!idleDeviceSuspended;
    }
    catch(const std::exception&e){
        if(!production())throw;
        const auto line=std::string("EndfieldHUD could not release the closed HUD's graphics device (")+typeid(e).name()+")\n";
        std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
    }
    if(memoryTrace&&production()&&memoryTrace->closings<8)recordMemory("closed-idle-"+std::to_string(memoryTrace->closings),true);
}
void Application::Impl::prepareGraphics(bool anticipated){
    if(!ready||stopping||!renderer.stats().initialized||!renderer.deviceSuspended())return;
    try{if(renderer.prepareDevice()&&anticipated&&session->phase()==core::VisibilityPhase::concealed){preparedGraphicsUntil=now()+preparedGraphicsLifetime;scheduleDeadline();}}
    catch(const std::exception&e){
        if(!production())throw;
        const auto line=std::string("EndfieldHUD could not prepare the HUD's graphics device (")+typeid(e).name()+")\n";
        std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
    }
}
void Application::Impl::systemChange(std::uint32_t message,double time){
    time=hostTime(time);if(message==WM_TIMECHANGE&&archiveDates)archiveDates->refreshSystemTimeZone();
    if(calendar)isolate(calendarModuleOwner,"system change",[&]{calendar->systemChanged(hostTime(time));updateCalendarWake();});
    if(message==WM_SETTINGCHANGE&&configuration.string("theme")=="system")configurationPending=true;
    if(message==WM_SETTINGCHANGE)applyTrayIcon(); // ImmersiveColorSet: the taskbar theme inks the template
    if(message==WM_SETTINGCHANGE&&visibleMode()){BOOL animations=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0)&&systemAnimationsOff!=(animations==FALSE))configurationPending=true;}
    if(message==WM_TIMECHANGE)broadcast(HostEvent::Kind::timeChanged,time);
    else if(message==WM_SETTINGCHANGE)broadcast(HostEvent::Kind::systemSettingsChanged,time);
    refresh(time);
}
void Application::Impl::drainUtility(double time){
    if(!utility)return;time=hostTime(time);
    // The long-job executor (area exports) settles with the shared one.
    utility->waitIdle();if(longWork)longWork->waitIdle();utility->drain();if(longWork)longWork->drain();
    if(centerLogoRetry)updateCenterLogo();
    broadcast(HostEvent::Kind::utilityCompleted,time);
}
void Application::Impl::finishAreaPicker(ApplicationFileResult result){
    auto completion=std::exchange(areaPicker,{});if(!completion)return;
    if(!production()){completion(std::move(result));return;}
    try{completion(std::move(result));}
    catch(const std::exception&e){const auto line=std::string("EndfieldHUD module area file dialog completion failed (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
}

ApplicationAreaMode Application::Impl::AreaHost::mode()const noexcept{return impl_.areaMode();}
double Application::Impl::AreaHost::now()const{return impl_.areaClock();}
const ehud::data::Settings& Application::Impl::AreaHost::settings()const noexcept{return impl_.configuration;}
const ModuleAppearance& Application::Impl::AreaHost::appearance()const noexcept{return impl_.moduleAppearance;}
core::Module Application::Impl::AreaHost::selectedModule()const noexcept{return impl_.notes?impl_.notes->selected():impl_.lifecycle.lastModule();}
core::SystemOverlayPhase Application::Impl::AreaHost::overlayPhase()const noexcept{return impl_.lifecycle.phase();}
bool Application::Impl::AreaHost::overlayVisible()const noexcept{return impl_.session&&impl_.session->phase()!=core::VisibilityPhase::concealed;}
bool Application::Impl::AreaHost::focused()const noexcept{return impl_.focused;}
const ClientMetrics& Application::Impl::AreaHost::metrics()const noexcept{return impl_.metrics;}
double Application::Impl::AreaHost::contentScale()const noexcept{return impl_.contentScale;}
std::optional<core::Module> Application::Impl::AreaHost::hoveredModule()const noexcept{
    // HUDSourceWatchView hoverTarget: the hovered navigation button's action,
    // mapped to its source module target through the content catalog.
    if(!impl_.session||!impl_.contentCatalog)return std::nullopt;
    const auto hovered=impl_.session->hovered();if(!hovered)return std::nullopt;
    const auto&actions=impl_.session->actions();const auto action=actions.find(*hovered);if(action==actions.end())return std::nullopt;
    for(const auto&e:impl_.contentCatalog->entries())if(e.action==action->second){
        for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);if(core::moduleIdentifier(module)==e.target)return module;}
        return std::nullopt;
    }
    return std::nullopt;
}
bool Application::Impl::AreaHost::isolatedDataRoot()const noexcept{return impl_.args.isolatedDataRoot;}
native::Renderer& Application::Impl::AreaHost::renderer()noexcept{return impl_.renderer;}
native::LayerRasterizer& Application::Impl::AreaHost::rasterizer()noexcept{return impl_.rasterizer;}
UtilityExecutor* Application::Impl::AreaHost::utility()noexcept{return impl_.utility.get();}
native::SystemServices* Application::Impl::AreaHost::systemServices()noexcept{return impl_.systemServices.get();}
native::AudioRuntime* Application::Impl::AreaHost::audio()noexcept{return impl_.audio.get();}
native::NativeMediaRequestBroker* Application::Impl::AreaHost::mediaBroker()noexcept{return impl_.mediaBroker.get();}
const modules::WorkModeController* Application::Impl::AreaHost::workMode()const noexcept{return impl_.workMode?&impl_.workMode->controller():nullptr;}
core::ApplicationEventRecorder* Application::Impl::AreaHost::eventRecorder()noexcept{return impl_.eventRecorder.get();}
void Application::Impl::AreaHost::requestFrame(){if(impl_.ready&&!impl_.stopping)impl_.host.invalidate();}
void Application::Impl::AreaHost::recordEvent(modules::EventKind kind,modules::EventMetadata metadata){
    if(impl_.eventOwner&&!impl_.stopping)impl_.eventOwner->record({ehud::data::makeUUID(),kind,ehud::data::foundationNow(),std::move(metadata)});
}
void Application::Impl::AreaHost::selectModule(core::Module module){
    if(!impl_.ready||impl_.stopping)return;
    // Same path as a source navigation button (Application::Impl::activate).
    const auto time=impl_.areaClock();const auto effects=impl_.lifecycle.selectModule(module);
    if(effects.empty())impl_.selectModule(module,time);else impl_.perform(effects,time);
}
void Application::Impl::AreaHost::openModule(core::Module module){
    if(!impl_.ready||impl_.stopping)return;
    // Same path as the tray Settings/About entries (Application::Impl::trayAction).
    const auto time=impl_.areaClock();impl_.perform(impl_.lifecycle.openSettingsModule(module),time);
    if(impl_.visibleMode()&&impl_.session->phase()!=core::VisibilityPhase::concealed&&!impl_.closing&&!impl_.stopping){impl_.host.show();impl_.refresh(time);}
}
void Application::Impl::AreaHost::closeOverlay(){if(impl_.ready&&!impl_.stopping)impl_.perform(impl_.lifecycle.close(),impl_.areaClock());}
bool Application::Impl::AreaHost::updateSettings(const std::function<void(ehud::data::Settings&)>&mutate){
    if(!impl_.settingsUI||impl_.stopping||!mutate)return false;
    auto next=impl_.settingsUI->controller().committed();mutate(next);
    impl_.settingsUI->controller().update(std::move(next),impl_.areaClock());return true;
}
bool Application::Impl::AreaHost::chooseFiles(const ApplicationFileRequest&request,std::function<void(ApplicationFileResult)>completion){
    if(!completion||!impl_.mediaPicker||impl_.pickerOwner!=PickerOwner::none||impl_.stopping||!impl_.ready)return false;
    // singleFile: one existing file through the caller's filter (Mac
    // NSOpenPanel with content types and no multiple selection).
    if(request.applications)impl_.mediaPicker->setLabels({request.title,request.acceptLabel,request.selectionLabel,gpu::ShelfPickerMode::applications,request.filterName,request.filterSpec});
    else if(request.singleFile)impl_.mediaPicker->setLabels({request.title,request.acceptLabel,request.selectionLabel,gpu::ShelfPickerMode::singleFile,request.filterName,request.filterSpec});
    else impl_.mediaPicker->setLabels({request.title,request.acceptLabel,request.selectionLabel,gpu::ShelfPickerMode::mixedReferences});
    if(!impl_.mediaPicker->request())return false;
    impl_.pickerOwner=PickerOwner::area;impl_.areaPicker=std::move(completion);impl_.host.capturePointer(false);return true;
}
void Application::Impl::AreaHost::cancelFileChoice(){
    if(impl_.pickerOwner!=PickerOwner::area)return;
    impl_.mediaPicker->cancel();impl_.pickerOwner=PickerOwner::none;impl_.finishAreaPicker({true,false,{}});
}
std::uint32_t Application::Impl::AreaHost::registerMessage(){
    need(registeredMessages_<areaMessageLimit,"Module areas registered too many messages");
    return areaMessageFirst+registeredMessages_++;
}
void Application::Impl::AreaHost::publishService(std::string_view key,void*value,const std::type_info&type){
    const auto found=services_.find(key);
    if(!value){if(found!=services_.end())services_.erase(found);return;}
    need(!key.empty()&&key.size()<=256,"Area service keys are short and non-empty");
    if(found!=services_.end()){need(found->second.value==value&&*found->second.type==type,"An area service key is already published");return;}
    services_.emplace(std::string(key),Service{value,&type,constructing_});
}
void* Application::Impl::AreaHost::findService(std::string_view key,const std::type_info&type)const{
    const auto found=services_.find(key);
    return found==services_.end()||*found->second.type!=type?nullptr:found->second.value;
}
void Application::Impl::AreaHost::abandon(std::string_view area)noexcept{std::erase_if(services_,[&](const auto&entry){return entry.second.publisher==area;});}
void Application::Impl::constructAudio(){
    // The ONE production audio worker: the Volume binding, the Event Log device
    // list and Now Playing routes vote on it. Nothing opens COM or an audio
    // interface until the first vote; hidden tests never construct it.
    const auto window=static_cast<HWND>(host.hwnd());
    audio=std::make_unique<gpu::AudioRuntime>(gpu::nativeAudioServiceOptions(args.dataRoot),settingsLanguage(configuration),
        [window]{PostMessageW(window,audioMessage,serviceGeneration,0);});
    (void)audio->recover(); // crash-recovery journal of owned per-app attenuation
    if(eventRecorder)(void)audio->setDeviceEvents(true); // Event Log device list, app lifetime
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
    // NotesCanvas.onAction -> Event Log noteAction (the action name only, never content).
    notes->setActionSink([this](std::string_view action){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::noteAction,ehud::data::foundationNow(),{{"action",std::string(action)}}});});
    // OverlayController.lastSystemModule starts at Map. The fresh owner has
    // had no event yet, so its first selection completes before any opening.
    if(production())notes->select(core::Module::map,hostTime(endfield::app::now()-1));
    mediaPicker=std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{static_cast<HWND>(host.hwnd()),mediaPickerMessage,serviceGeneration},gpu::ShelfPickerLabels{"添加图片/视频","添加","添加所选文件"});
    session->setHitFilter([this](std::string_view,core::Point p){return !registry.covers(p);},0);
    notesModuleOwner=notesModule(*notes,[this]{return areaClock();},[this]{automationChanged();refresh(now());});registerModule(notesModuleOwner);
    startup.mark("isolated-notes-owner");
}
void Application::Impl::constructShelf(){
    // The Shelf reveal draws the authored HUDSubsectionTransition keyframes
    // (native/shelf_scene.hpp shelfRevealShutter); the measured Core Animation
    // samples (args.shelfMask, still packaged and pinned) are not read.
    shelfAssets=std::make_unique<gpu::NativeShelfAssets>(args.shelfAssets);
    tools::ShelfPreviewOptions shelfOptions{args.shelfDataRoot,visibleMode(),visibleMode(),nullptr};
    // SystemHUDView: shelfAdded {filename} per new item, shelfRemoved {filename},
    // shelfCleared {count}. eventOwner is checked when the record is made.
    shelfOptions.recordEvent=[this](modules::EventKind kind,modules::EventMetadata metadata){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),kind,ehud::data::foundationNow(),std::move(metadata)});};
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
    // HUDSourceWatchView desktop Map occlusion (core/map_source_cutout.hpp):
    // the Map reads the source frame poseShell presented, so the Storage and
    // Activity Monitor plates below the circle stay above it - cut out of its
    // artwork, and their presses left to the navigation (MapPreview::covers
    // feeds the session's source hit filter set in constructNotes).
    options.sourceFrame=[this]()->std::optional<modules::MapSourceView>{const auto*frame=session?session->currentFrame():nullptr;
        if(!frame||!frame->sourceFrame)return std::nullopt;return modules::MapSourceView{frame->sourceFrame->resolved,frame->sourceFrame->inheritedAlpha,&frame->camera};};
    map=std::make_unique<tools::MapPreview>(rasterizer,*utility,std::move(options));map->resize(metrics);
    mapModuleOwner=mapModule(*map,peerHooks());registerModule(mapModuleOwner);
    startup.mark("lazy-map-owner");
}
// Decode the immutable bundled geography once on first selection. A full
// queue retries on its next completion, never from an idle timer.
// Terrain.bin and Countries.bin load separately (native/map_assets.hpp
// loadMapGeography, pinned by native::MapGeographyPins): a missing, differing
// or undecodable dataset is flagged instead of thrown, and
// MapPreview::setGeography keeps the Map with its pins and the Mac message.
void Application::Impl::requestMapGeography(){
    if(mapLoadRequested||stopping||!map)return;
    const auto root=args.mapGeography;
    auto result=std::make_shared<modules::MapGeography>();
    const auto window=static_cast<HWND>(host.hwnd());
    mapLoadRequested=utility->submit(mapLoadRoute,[root,result]{*result=gpu::loadMapGeography(root);},
    [this,result,window](std::exception_ptr error){if(stopping||!map)return;
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
            // ReaderController.fail: the typed ReaderError description.
            isolate(readerModuleOwner,"import",[&]{
                if(error||!book)reader->receiveImportError(generation,modules::readerImportFailure(error,watchAppearance.language),time);
                else reader->receiveImport(generation,std::move(*book),time);});changed();
        },changed);
    tools::ReaderPreviewOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.newID=[] {return ehud::data::makeUUID();};
    options.strings=readerStrings(watchAppearance.language);options.fontFamilies=[this]{return rasterizer.installedFontFamilies();};
    options.shelfChoices=[this]{std::vector<modules::ReaderMenuChoice>choices;if(shelf)for(const auto&item:shelf->state().items()){
        if(!item.isDirectory&&!item.availabilityError&&modules::readerSupportedExtension(utf8(utf8Path(item.lastKnownPath).extension())))choices.push_back({item.id,item.name});
    }return choices;};
    // SystemHUDView ReaderCanvas.onEvent: readerAction imported/deleted/bookmarked/settings/progress.
    readerOwner->state().setEventObserver([this](std::string_view action){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::readerAction,ehud::data::foundationNow(),{{"action",std::string(action)}}});});
    reader=std::make_unique<tools::ReaderPreview>(readerOwner->state(),rasterizer,std::move(options));reader->resize(metrics);
    readerModuleOwner=readerModule(*reader,*readerOwner,rasterizer,[this](const WheelEvent&e,double time){if(readerTrace)readerTrace->record(true,time,reader->scrollSnapshot(),e.steps,e.linesPerStep);},peerHooks());
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
    calendarModuleOwner=calendarModule(*calendar,std::move(hooks),peerHooks());registerModule(calendarModuleOwner);
    startup.mark("isolated-calendar-owner");
}
void Application::Impl::constructServices(){
    if(!args.nativeClipboard&&!args.nativeBattery)return;
    // One app-lifetime SystemServices: AC/battery power notifications and the
    // clipboard format listener are event-driven. Audio belongs to the shared
    // AudioRuntime (constructAudio). Hidden tests never construct this.
    const auto window=static_cast<HWND>(host.hwnd());systemServices=std::make_unique<gpu::SystemServices>();
    const auto status=systemServices->start(window,systemServiceMessage,[this,window](gpu::ServiceChange event){
        if(stopping)return;
        if(event==gpu::ServiceChange::clipboard){clipboardDirty=true;
            if(ready&&!clipboardRefreshQueued&&session->phase()!=core::VisibilityPhase::concealed&&notes&&notes->selected()==core::Module::clipboard){
                need(PostMessageW(window,clipboardChangedMessage,serviceGeneration,0)!=FALSE,"Post Clipboard revision");clipboardRefreshQueued=true;
            }}
        // With the Power area its merged reading (battery-class mWh pair) feeds
        // the Power page through the published page sink instead.
        if(event==gpu::ServiceChange::battery&&systemServices){bool changed{};if(!powerArea()&&battery)isolate(batteryModuleOwner,"battery",[&]{changed=battery->receive(systemServices->battery());});if(changed&&ready)host.invalidate();recordBattery();updateTrayMenu();}
    },false,args.nativeClipboard?utility.get():nullptr);
    need(SUCCEEDED(status),"Cannot start explicit Windows service preview");
    // ClipboardStore.statusMessage per capture status (none while an image
    // validates) and SystemHUDView clipboardCanvas.onCopy: Event Log
    // clipboardCopied {kind} after a successful copy-back.
    if(args.nativeClipboard)clipboardProvider=std::make_unique<gpu::NativeClipboardProvider>(gpu::clipboardProviderSource(*systemServices,
        [this](std::int32_t status){return gpu::clipboardStatusText(status,watchAppearance.language);},
        [this](gpu::ClipboardKind kind){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::clipboardCopied,ehud::data::foundationNow(),{{"kind",std::string(gpu::clipboardEventKind(kind))}}});}));
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
    clipboardModuleOwner=clipboardModule(*clipboard,*clipboardAssets,peerHooks());registerModule(clipboardModuleOwner);
}
void Application::Impl::constructArchive(){
    const auto window=static_cast<HWND>(host.hwnd());const auto start=visibleMode()?now():args.benchmarkEpoch;
    const auto changed=[this,window]{if(ready&&!stopping&&!archiveRefreshQueued){need(PostMessageW(window,archiveChangedMessage,serviceGeneration,0)!=FALSE,"Post Archive revision");archiveRefreshQueued=true;}};
    archiveService=std::make_unique<ArchiveService>(args.dataRoot/"Archive",*utility,gpu::nativeArchiveTextRules(),changed);
    archiveDates=std::make_shared<gpu::ArchiveDateFormatter>();
    // The first Archive frame already uses the current language, theme and
    // accent (the archive adapter's setAppearance mapping).
    const bool dark=configuration.string("theme")!="light";std::array<double,3>configured{250./255.,212./255.,31./255.};
    if(const auto hex=configuration.string("accentHex");hex.size()==6)try{for(unsigned n=0;n<3;++n)configured[n]=std::stoul(hex.substr(n*2,2),nullptr,16)/255.;}catch(const std::exception&){}
    const auto effective=dark?configured:source::sourceBlackBlend(configured,.35);
    const modules::ArchiveAppearance appearance{dark,2,{effective[0],effective[1],effective[2],1},modules::ArchiveColor{1,159./255.,10./255.,1}};
    auto options=tools::makeArchivePreviewOptions(args.archiveAssets,rasterizer,settingsLanguage(configuration),appearance,archiveDates);options.raster.memoryImages=&archiveImages;
    archive=std::make_unique<tools::ArchivePreview>(archiveService->state(),window,rasterizer,notes->activatedTextManager(),notes->textClient(),std::move(options));archive->resize(metrics);archive->focus(focused,start);
    tools::ArchiveMediaBindingOptions media;
    media.resolve=[](const Json&ref){need(ref["referencePlatform"]==Json("windows"),"Imported Mac media needs explicit relinking on Windows");const auto&path=ref["windowsPath"].string();need(ehud::data::validWindowsFilePath(path),"Invalid Archive Windows reference");return gpu::NotesImageAccess{utf8Path(path),{}};};
    // NotesMediaError.localizedDescription (unavailable, unsupported format,
    // too large, too many frames, decoding failed) in five languages.
    media.errorText=[this](HRESULT hr){return tools::notesMediaErrorDescription(hr,watchAppearance.language);};
    media.loadThumbnail=[this](std::string id,auto completion){archiveService->state().loadThumbnail(std::move(id),std::move(completion));};media.changed=changed;
    archiveMedia=std::make_unique<tools::ArchiveMediaBinding>(*archive,*mediaBroker,archiveMediaClient,renderer,archiveImages,std::move(media));
    archiveModuleOwner=archiveModule(*archive,archiveService.get(),archiveMedia.get(),[this]{return areaClock();},[this]{automationChanged();refresh(now());});registerModule(archiveModuleOwner);
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
    }else{
        // Storage volume choice (Windows addition, user request): remembered in the
        // preference record as "storageVolume" (unknown keys survive the store;
        // Restore Defaults clears it, which shows the startup volume).
        options.loadVolume=[this]{const auto&value=std::as_const(configuration).fields["storageVolume"];return value.isString()?value.string():std::string{};};
        options.saveVolume=[this](const std::string&id){if(areaHost)(void)areaHost->updateSettings([&id](ehud::data::Settings&s){s.set("storageVolume",Json(id));});};
    }
    storage=std::make_unique<tools::StoragePreview>(rasterizer,std::move(options));storage->resize(metrics);
    storageModuleOwner=storageModule(*storage,StorageModuleHooks{
        [this]{return storageFixture?storageFixture->time.load(std::memory_order_relaxed):now();},
        [this]{return !closing&&session->phase()!=core::VisibilityPhase::concealed;}},peerHooks());
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
        options.iconExecutor=utility.get(); // visible-row icons on the shared executor
        options.demandChanged=[this](bool visible,bool apps,double time){
            if(stopping)return;const bool wasApps=activityPlan.appsActive();activityPlan.setVisible(visible,activityPlan.alertActive(),apps,time);
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
        // The Power charge metric's 1 Hz demand and telemetry (OverlayController.reconcileChargeMetricTelemetry).
        activityTelemetry=std::make_unique<modules::ActivityTelemetryLink>(activityPlan,activity->providerState());
        activityPlan.start(now()); // AppDelegate.swift:216 activity.start(): 5 s session history from launch
    }
    activityModuleOwner=activityModule(*activity,peerHooks());registerModule(activityModuleOwner);
    startup.mark(args.nativeActivity?"native-activity-owner":"synthetic-activity-owner");
}
void Application::Impl::constructEventLog(){
    const auto window=static_cast<HWND>(host.hwnd());
    // The host time: an event recorded inside an owner call is never later
    // than the save capture that follows it (onDeadline).
    eventOwner=std::make_unique<gpu::EventLogOwner>(args.dataRoot,gpu::EventLogOwnerCallbacks{[this]{return now();},[this,window]{
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
    // EventLogCanvas.timestamp: en_US_POSIX "MM-dd HH:mm:ss" in the current
    // Windows time zone; the synthetic fixture keeps its constant row text.
    auto actions=args.eventLogFixture?eventOwner->callbacks([](double){return std::string("10-08 12:34:56");}):eventOwner->callbacks(gpu::localEventLogTimestamp);
    eventLog=std::make_unique<tools::EventLogPreview>(rasterizer,gpu::LayerRasterOptions{},std::move(actions),modules::EventLogStrings{},modules::EventLogAppearance{},eventOwner->nameCompactor());eventLog->resize(metrics);
    eventLogModuleOwner=eventLogModule(*eventLog,peerHooks());registerModule(eventLogModuleOwner);
}
void Application::Impl::constructBattery(){
    modules::BatteryReading value;value.present=true;value.percentage=63;
    modules::BatteryAppearance appearance;appearance.language=core::Language::simplifiedChinese;
    gpu::LayerRasterOptions options;options.pixelsPerPoint=2;
    if(args.nativeBattery&&!args.batteryFixture)value={};
    // Power page "Battery alert ›" opens Display > Battery alert (Mac openBatterySettings).
    battery=std::make_unique<tools::BatteryPreview>(rasterizer,options,value,appearance,[this]{const auto t=now();if(settingsUI)isolate(settingsModuleOwner,"battery settings",[&]{settingsUI->showBatterySettings(hostTime(t));});selectModule(core::Module::display,t);});
    batteryPageSink=[this](const modules::BatteryReading&r){return battery&&battery->receive(r);};
    if(args.nativeBattery&&systemServices)battery->receive(systemServices->battery());
    battery->resize(metrics);
    batteryModuleOwner=batteryModule(*battery);registerModule(batteryModuleOwner);
}
void Application::Impl::constructWorkMode(){
    tools::WorkModePreviewOptions options;
    // Lifetime hours belong to the personal profile: the Personal Profile area
    // (app/areas/profile_module) observes this controller's absolute
    // checkpoints (WorkModeController::listenCheckpoints) and is the single
    // profile.json writer.
    options.saveWorkSeconds=[](double){};
    workMode=std::make_unique<tools::WorkModePreview>(static_cast<HWND>(host.hwnd()),rasterizer,notes->activatedTextManager(),notes->textClient(),std::move(options));
    // The area clock: hidden coverage never moves the owner's time to the wall clock.
    workMode->resize(metrics);workMode->focus(focused,areaClock());
    // HUDWorkModeInteraction accessibility buttons under the HUD root.
    workModeModuleOwner=workModeModule(*workMode,screenHooks());registerModule(workModeModuleOwner);
}
// Volume app-row icons (Mac VolumeCanvas.renderAppIcon from
// NSWorkspace.icon of the app bundle): one bounded job at a time on the
// shared utility queue (native/volume_icon_provider.hpp). The process's one
// Shell icon worker stays the Shelf's, and Volume starts no thread of its
// own. A completion that changed a shown row asks for one frame.
gpu::VolumeQueuedIconProvider* Application::Impl::constructVolumeIcons(gpu::VolumeQueuedIconProvider::Extractor extractor){
    if(!utility)return nullptr;
    volumeIconRoute=utility->makeRoute();
    auto submit=[this](std::function<void()>work,gpu::VolumeQueuedIconProvider::Done done){
        return utility&&utility->submit(volumeIconRoute,std::move(work),[done=std::move(done)](std::exception_ptr error){done(bool(error));});};
    auto changed=[this]{bool redraw{};if(volume)isolate(volumeModuleOwner,"icons",[&]{redraw=volume->iconsChanged();});if(redraw&&ready&&!stopping)host.invalidate();};
    volumeIcons=std::make_unique<gpu::VolumeQueuedIconProvider>(std::move(submit),std::move(changed),std::move(extractor));
    return volumeIcons.get();
}
void Application::Impl::constructVolume(){
    tools::VolumePreviewOptions options;
    const auto bindIcons=[&](gpu::VolumeQueuedIconProvider::Extractor extractor){
        auto*icons=constructVolumeIcons(std::move(extractor));if(!icons)return;
        options.memoryImages=&icons->images();
        options.icons={[icons](std::span<const gpu::VolumeIconApplication>apps){return icons->setVisible(apps);},[icons]{return &icons->plan();},[icons]{(void)icons->hide();}};
    };
    if(args.nativeVolume){
        // The shared AudioRuntime binding: Volume's vote (reveal/conceal and the
        // controller's own selection) observes endpoints and per-app sessions
        // only while Volume is presented; nothing switches the default device.
        // Without the runtime Volume stays absent (never the synthetic fixture).
        need(bool(audio),"Volume needs the shared audio runtime");
        // The executables' own icon resources, opened by their file identity.
        bindIcons({});
        options.strings=gpu::volumeStrings(settingsLanguage(configuration));
        options.initial=audio->volume().snapshot();options.actions=audio->volume().callbacks();
        volume=std::make_unique<tools::VolumePreview>(rasterizer,std::move(options));
        audio->volume().setReceiver([this](const gpu::VolumeSnapshot&value){bool changed{};if(volume)isolate(volumeModuleOwner,"audio",[&]{changed=volume->receiveSnapshot(value);});if(changed&&ready&&!stopping)host.invalidate();});
    }else{
        auto sample=std::make_shared<gpu::VolumeSnapshot>();
        sample->outputs={{"test-speaker","测试扬声器"},{"test-headphones","测试耳机",true,false}};
        sample->inputs={{"test-microphone","测试麦克风"}};sample->outputID="test-speaker";sample->inputID="test-microphone";
        sample->volume=.45;sample->balance=0;sample->muted=false;
        sample->canSetVolume=sample->canSetMute=sample->canSetBalance=sample->canSetDefaultOutput=sample->canSetDefaultInput=true;
        // Five synthetic adjustable apps (no real audio): the three-row app
        // list scrolls (maximum offset 5 x 31 - 95 = 60 points).
        sample->applicationActivitySupported=true;
        for(unsigned n=0;n<5;++n){sample->applications.push_back({std::to_string(9100+n)+":1","测试应用 "+std::to_string(n+1),9100u+n,true});
            // Synthetic executables (never opened): the icons come from the
            // synthetic extractor below through the same shared queue.
            gpu::AudioApplicationExecutable executable;executable.path="C:\\EndfieldHUD-synthetic\\app-"+std::to_string(n+1)+".exe";
            executable.identity.objectID[0]=static_cast<std::uint8_t>(n+1);executable.identity.volumeSerial=0x5eed;sample->applications.back().executable=std::move(executable);}
        // Apps 1, 2, 4 and 5 have an icon resource; app 3 has none and keeps
        // the source's nil icon.
        bindIcons([](const gpu::ShelfIconRequest&r){gpu::ShelfIconPixels p;
            if(r.identity.objectID[0]==3){p.typeFallback=true;p.result=HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND);return p;}
            constexpr unsigned size=gpu::LayerImageSource::shelfRequestedPixels;p.width=p.height=size;p.result=S_OK;p.straightRGBA.assign(std::size_t(size)*size*4,0);
            for(unsigned y=8;y<size-8;++y)for(unsigned x=8;x<size-8;++x){auto*px=p.straightRGBA.data()+(std::size_t(y)*size+x)*4;px[0]=static_cast<std::uint8_t>(40*r.identity.objectID[0]);px[1]=160;px[2]=220;px[3]=255;}
            return p;});
        options.initial=*sample;
        const auto changed=[this,sample]{if(volume)volume->receiveSnapshot(*sample);};
        options.actions.setVolume=[sample,changed](std::string_view id,double value){if(id!=sample->outputID)return false;sample->volume=value;changed();return true;};
        options.actions.setBalance=[sample,changed](std::string_view id,double value){if(id!=sample->outputID)return false;sample->balance=value;changed();return true;};
        options.actions.setMute=[sample,changed](std::string_view id,bool value){if(id!=sample->outputID)return false;sample->muted=value;changed();return true;};
        options.actions.setDefaultOutput=[sample,changed](std::string_view id){if(std::none_of(sample->outputs.begin(),sample->outputs.end(),[&](const auto&d){return d.id==id;}))return false;sample->outputID=id;changed();return true;};
        options.actions.setDefaultInput=[sample,changed](std::string_view id){if(std::none_of(sample->inputs.begin(),sample->inputs.end(),[&](const auto&d){return d.id==id;}))return false;sample->inputID=id;changed();return true;};
        options.actions.setAppGain=[sample,changed](std::string_view id,double value){const auto app=std::find_if(sample->applications.begin(),sample->applications.end(),[&](const auto&a){return a.id==id;});
            if(app==sample->applications.end())return false;app->state=gpu::VolumeAppState::active;app->gain=value;changed();return true;};
        options.actions.stopApp=[sample,changed](std::string_view id){const auto app=std::find_if(sample->applications.begin(),sample->applications.end(),[&](const auto&a){return a.id==id;});
            if(app==sample->applications.end())return false;app->state=gpu::VolumeAppState::direct;app->gain.reset();changed();return true;};
        volume=std::make_unique<tools::VolumePreview>(rasterizer,std::move(options));
    }
    volume->resize(metrics);
    volumeModuleOwner=volumeModule(*volume,screenHooks());registerModule(volumeModuleOwner);
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
            settingsLoaded=true;if(launchPending){launchPending=false;perform(lifecycle.launch(args.startup,settingsSaves->status().launched,!args.persistLaunchMarker),now());reopenAfterLaunch(now());}
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
    // Opening by hotkey switched off: a recorded shortcut is kept for later
    // and registers nothing now. Otherwise RegisterHotKey must accept it,
    // unless the low-level hook path can deliver it on its own.
    options.callbacks.registerShortcut=[this](auto value)->std::optional<std::string>{
        if(!tray)return core::localized("Shortcut unavailable","快捷键不可用",settingsLanguage(configuration));
        if(!summonHotkeyEnabled()){updateTrayMenu();return {};}
        const bool registered=tray->setHotkey(gpu::TrayHotkey{value.modifiers,value.key});
        bool hooked=false;if(summonHook){const auto chord=SummonHotkeyHook::chord(value.key,value.modifiers);hooked=summonHook->start(chord,++summonHookGeneration);lastHookChord=hooked?std::optional(chord):std::nullopt;}
        if(registered||hooked){updateTrayMenu();return {};}
        return core::localized("Shortcut unavailable","快捷键不可用",settingsLanguage(configuration));};
    // Recording a shortcut must see the current chord too: both paths pause.
    options.callbacks.shortcutCapture=[this](bool capture){if(!tray)return;applySummonShortcut(capture);};
    options.callbacks.launchAtLogin=[this](bool enabled)->std::optional<std::string>{
        if(!loginItem)return "Startup registration is unavailable in this isolated preview";
        // Transactional: an error is returned before the preference commits.
        auto error=loginItem->setEnabled(enabled,settingsLanguage(configuration));refreshExternalStatus();return error;};
    // Mac onEditBatteryPosition: the Power area posts its notice, closes the
    // HUD, edits after concealment and reopens Display afterwards.
    options.callbacks.editBatteryPosition=[this]{if(auto*p=powerArea())(void)isolateService(power::PowerAreaService::key,"battery position",[&]{(void)p->editPosition(now());});};
    // overlay.afterSystemClose + closeSystemOverlay: links open once the HUD closed.
    options.callbacks.openLink=[this](std::string_view url){openAfterClose(std::string(url),now());};
    // AppDelegate.checkForAppUpdates through the windowsUpdates area (consented
    // download and Setup hand-off); its returned page opens after the HUD
    // closed. Without the area the row opens the GitHub releases page.
    options.callbacks.checkUpdates=[this]{const auto time=now();if(auto*u=updates()){std::optional<std::string>url;(void)isolateService(UpdateService::key,"update check",[&]{url=u->activate(hostTime(time));});if(url)openAfterClose(*url,time);}else openAfterClose(std::string(releasesURL),time);};
    options.callbacks.automaticUpdates=[this](bool enabled){if(auto*u=updates())(void)isolateService(UpdateService::key,"update preference",[&]{u->setAutomaticChecks(enabled,now());});};
    options.about.repository=std::string(repositoryURL);
#if defined(EHUD_APP_VERSION_NAME)&&defined(EHUD_APP_BUILD_NUMBER)
    options.about.version=std::string(EHUD_APP_VERSION_NAME)+" ("+EHUD_APP_BUILD_NUMBER+")"; // HUDAboutInformation: "<version> (<build>)"
#endif
    // System page "Import macOS data…" (Windows only; the macImport area's entry).
    options.viewHooks.macImportTitle=[this](core::Language l)->std::optional<std::string>{
        auto*e=areaHost?areaHost->find<MacImportEntry>(macImportEntryService):nullptr;if(!e)return std::nullopt;return e->title(l);};
    options.viewHooks.macImport=[this]{if(auto*e=areaHost?areaHost->find<MacImportEntry>(macImportEntryService):nullptr)(void)isolateService(macImportEntryService,"mac import",[&]{(void)e->present(areaClock());});};
    // Display > Theme color / Center logo (HUDSettingsInteraction.chooseColor,
    // chooseLogo; a logo selection invalidates a pending import).
    // Windows-only Hotkeys toggle: switch opening by the summon shortcut off.
    options.viewHooks.summonHotkeyToggle=true;
    options.viewHooks.chooseColor=[this]{requestThemeColor();};
    options.viewHooks.chooseLogo=[this]{chooseCenterLogo();};
    options.viewHooks.logoSelection=[this]{++logoImportGeneration;};
    settingsUI=std::make_unique<tools::SettingsPreview>(rasterizer,std::move(options));settingsUI->resize(metrics);settingsUI->setOverlayVisible(session->phase()!=core::VisibilityPhase::concealed,visibleMode()?now():hostTime(args.benchmarkEpoch));
    refreshUpdateStatus();
    settingsModuleOwner=settingsModule(*settingsUI,peerHooks());registerModule(settingsModuleOwner);
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
        reopenAfterLaunch(now());
    }
    // The launch prewarm runs once the run is idle, in the applied language and theme.
    if(production()&&!prewarmed)(void)PostMessageW(static_cast<HWND>(host.hwnd()),prewarmMessage,serviceGeneration,0);
}
void Application::Impl::ensureGame(double time){
    if(game||stopping||!gameEnabled)return;
    if(!production()){constructGame(hostTime(time));return;}
    // Production isolates the lazy Minigame start like the optional built-ins.
    try{constructGame(hostTime(time));}catch(const std::exception&e){
        gameEnabled=false;unregisterModule(gameModuleOwner);game.reset();gameSession.reset();
        const auto line=std::string("EndfieldHUD module minigame could not start (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
}
void Application::Impl::constructGame(double time){
    modules::OrbiPomSessionCallbacks callbacks;
    callbacks.makeRuntime=[root=args.orbipomAssets]{return std::make_unique<modules::OrbiPomRuntime>(modules::OrbiPomRuntimeOptions{root});};
    callbacks.saveBest=[this](std::int64_t best){gameBest=std::max(gameBest,best);configuration.set("orbipom.bestScore.v1",gameBest);if(settingsSaves){auto committed=settingsUI?settingsUI->controller().committed():configuration;committed.set("orbipom.bestScore.v1",gameBest);settingsSaves->save(committed);}};
    callbacks.event=[this](modules::OrbiPomEvent event){if(eventOwner){const char*action=event==modules::OrbiPomEvent::started?"started":event==modules::OrbiPomEvent::restarted?"restarted":"finished";eventOwner->record({ehud::data::makeUUID(),modules::EventKind::minigameAction,ehud::data::foundationNow(),{{"action",action}}});}};
    gameSession=std::make_unique<modules::OrbiPomSession>(gameBest,std::move(callbacks));
    tools::OrbiPomPreviewOptions gameOptions;gameOptions.raster.assetRoot=args.orbipomAssets;gameOptions.raster.pixelsPerPoint=2;gameOptions.raster.paddingPoints=1;
    game=std::make_unique<tools::OrbiPomPreview>(*gameSession,rasterizer,std::move(gameOptions));game->resize(metrics);game->setForeground(focused,time);game->setOverlayVisible(session->phase()!=core::VisibilityPhase::concealed,time);configurationPending=true;
    gameModuleOwner=minigameModule(*game,peerHooks());registerModule(gameModuleOwner);
}
void Application::Impl::constructTray(){
    const auto bytes=ehud::data::detail::readFile(args.residentIcons/"manifest.json",512*1024);need(bytes.has_value(),"Missing original icon roster");
    const auto catalog=Json::parse(*bytes);need(catalog["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Icon source differs from this app migration");
    // HUDApplicationIcon.menuBarImage per offered icon: the tray art, drawn
    // as a template (tinted for the taskbar theme) except Perlica.
    for(const auto&icon:catalog["icons"].array()){
        if(!icon["offered"].boolean())continue;const auto&art=icon["tray"];
        trayIcons.push_back({icon["id"].string(),art["file"].string(),art["sha256"].string(),art["template"].boolean()&&icon["id"].string()!="perlica"});
    }
    need(std::any_of(trayIcons.begin(),trayIcons.end(),[](const auto&i){return i.id=="endfield";}),"Missing original Endfield icon");
    tray=std::make_unique<gpu::TrayController>(static_cast<HWND>(host.hwnd()),trayMessage,RegisterWindowMessageW(L"TaskbarCreated"));
    loadTrayIcon();
    // Explorer may be absent or hide the icon; hotkey and relaunch activation
    // remain available, so a production tray failure is not fatal.
    const bool installed=tray->start(static_cast<HICON>(applicationIcon.handle()),production()?L"EndfieldHUD":L"EndfieldHUD migration preview");
    if(!production())need(installed,"Windows could not add the preview tray icon");
    // The low-level hook path (production only: a development preview never
    // installs a global keyboard hook).
    if(production())summonHook=std::make_unique<SummonHotkeyHook>(host.hwnd(),summonHookMessage);
    bool shortcut=true;
    if(production())applySummonShortcut(false);else shortcut=tray->setHotkey(gpu::TrayHotkey{MOD_CONTROL,VK_OEM_3});
    updateTrayMenu();
    if(!shortcut&&!production())std::cout<<"Ctrl + ` is already registered; reopen this isolated preview from its tray icon.\n";
}
// AppDelegate.updateApplicationIcon: the chosen icon's tray image at the
// system small-icon size; a template follows the taskbar theme
// (SystemUsesLightTheme: black ink on a light taskbar, white on a dark one).
// Rebuilt only on the preference, a theme setting change or the first start.
bool Application::Impl::loadTrayIcon(){
    if(trayIcons.empty())return false;
    const auto chosen=configuration.string("applicationIcon");
    auto icon=std::find_if(trayIcons.begin(),trayIcons.end(),[&](const auto&i){return i.id==chosen;});
    if(icon==trayIcons.end())icon=std::find_if(trayIcons.begin(),trayIcons.end(),[](const auto&i){return i.id=="endfield";});
    DWORD light=0,size=sizeof(light);
    const bool lightTaskbar=RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size)==ERROR_SUCCESS&&light!=0;
    const auto ink=!icon->templateArt?gpu::IconInk::original:lightTaskbar?gpu::IconInk::blackTemplate:gpu::IconInk::whiteTemplate;
    const auto pixels=static_cast<unsigned>(std::max(16,GetSystemMetricsForDpi(SM_CXSMICON,GetDpiForSystem())));
    auto key=icon->id+"|"+std::to_string(static_cast<int>(ink))+"|"+std::to_string(pixels);
    if(key==trayIconKey&&applicationIcon.handle())return false;
    applicationIcon=gpu::ApplicationIcon::fromPNG(args.residentIcons/utf8Path(icon->file),icon->sha256,pixels,ink);trayIconKey=std::move(key);
    return true;
}
void Application::Impl::applyTrayIcon(){
    if(!tray)return;
    try{if(loadTrayIcon())(void)tray->updateIcon(static_cast<HICON>(applicationIcon.handle()),trayTooltip.empty()?std::wstring(production()?L"EndfieldHUD":L"EndfieldHUD migration preview"):trayTooltip);}
    catch(const std::exception&e){if(!production())throw;const auto line=std::string("EndfieldHUD tray icon could not change (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
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
    // AppDelegate.makeMenu: status, separator, Open overlay (shortcut), Work
    // Mode, Settings…, Preview charging effect, separator, About, Check for
    // Updates…, Quit. With the Power area its AppDelegate.updateMenu text is
    // used for the status line and the tooltip (also without a battery).
    std::wstring powerStatus,powerTip,powerPreview;
    auto*power=powerArea();
    if(power&&!isolateService(power::PowerAreaService::key,"tray",[&]{powerStatus=wide(power->trayStatus(language));powerTip=wide(power->trayTooltip(language));powerPreview=wide(power->previewMenuTitle(language));}))power=nullptr;
    if(power){
        menu.push_back({gpu::TrayAction::none,powerStatus,false});menu.push_back({gpu::TrayAction::none,{},false,false,true});
        auto tip=std::move(powerTip);if(tip.size()>127)tip.resize(127); // NOTIFYICONDATA szTip bound
        if(tip!=trayTooltip&&applicationIcon.handle()){trayTooltip=std::move(tip);(void)tray->updateIcon(static_cast<HICON>(applicationIcon.handle()),trayTooltip);}
    }else if(systemServices&&systemServices->battery().available){
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
    if(power)menu.push_back({gpu::TrayAction::previewCharging,std::move(powerPreview),true});
    menu.push_back({gpu::TrayAction::none,{},false,false,true});
    menu.push_back({gpu::TrayAction::about,t("About EndfieldHUD","关于 EndfieldHUD"),bool(settingsUI)});
    // AppDelegate.checkForAppUpdates: the windowsUpdates area's state title
    // ("Update available · …", "Checking for updates…") or the plain item.
    std::wstring updateTitle=t("Check for Updates…","检查更新…");
    if(auto*u=updates())(void)isolateService(UpdateService::key,"tray",[&]{updateTitle=wide(u->trayTitle(language));});
    menu.push_back({gpu::TrayAction::checkUpdates,std::move(updateTitle),bool(settingsUI)});
    menu.push_back({gpu::TrayAction::quit,t("Quit EndfieldHUD","退出 EndfieldHUD")});
    tray->setMenu(std::move(menu));
}

// ---------------------------------------------------------------------------
// Lifecycle
void Application::Impl::perform(const core::SystemOverlayEffects&effects,double time){
    using K=core::SystemOverlayEffect::Kind;
    for(const auto&e:effects){time=hostTime(time);
        switch(e.kind){
        case K::open:lifecycleOpening=e.token;lifecycleClosing.reset();openPresentation(e.module,time);break;
        case K::close:lifecycleClosing=e.token;lifecycleOpening.reset();close(time);break;
        case K::select:selectModule(e.module,time);break;
        case K::preview:if(auto*p=powerArea())(void)isolateService(power::PowerAreaService::key,"preview",[&]{p->preview(hostTime(time));});break; // launch --preview (AppDelegate.preview)
        case K::markLaunched:if(settingsSaves)settingsSaves->markLaunched();break;
        case K::quitAccepted:quitRequested=true;if(summonHook)summonHook->stop();if(tray)tray->setHotkey({});broadcast(HostEvent::Kind::quitting,time);break;
        case K::terminate:finishQuit(time);break;
        case K::restartShortcut:if(tray)applySummonShortcut(false);break;
        }
    }
}
// HUDDisplayPolicy preference: the Windows display chosen in Settings
// (windowsDisplayID; the Mac UUID never names a Windows display), else the
// pointer's display when openOnActiveDisplay, else the primary display. A
// disconnected fixed choice falls back to the pointer, then the primary.
gpu::DisplayPreference Application::Impl::displayPreference()const{
    gpu::DisplayPreference preference;preference.pointerDisplay=configuration.boolean("openOnActiveDisplay");
    if(const auto&id=configuration.fields["windowsDisplayID"];id.isString()&&!id.string().empty()){
        const auto text=wide(id.string());preference.persistentID=std::u16string(text.begin(),text.end());}
    return preference;
}
// Covers the display with the HUD window. A per-monitor DPI change can apply
// Windows' suggested rectangle; the second pass restores the exact bounds.
void Application::Impl::placeOnDisplay(const gpu::DisplayDescriptor&display){
    hudDisplay=display.handle;hudDisplayID=displayKey(display);
    const auto window=static_cast<HWND>(host.hwnd());const auto&b=display.bounds;
    for(unsigned attempt=0;attempt<2;++attempt){
        RECT current{};if(!GetWindowRect(window,&current))return;
        if(current.left==b.left&&current.top==b.top&&current.right==b.right&&current.bottom==b.bottom)return;
        if(!SetWindowPos(window,nullptr,b.left,b.top,b.right-b.left,b.bottom-b.top,SWP_NOZORDER|SWP_NOACTIVATE))return;
    }
}
void Application::Impl::targetDisplay(){
    // OverlayController: every opening chooses HUDDisplayPolicy.targetScreen.
    // Topology is read only at this opening; the window keeps its pixels.
    const auto displays=gpu::readConnectedDisplays();
    POINT pointer{};if(!GetCursorPos(&pointer))return;
    const auto index=gpu::resolveDisplay(displayPreference(),displays,{pointer.x,pointer.y});if(!index)return;
    previousApplication=reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
    placeOnDisplay(displays[*index]);
}
// OverlayController.reposition (system overlay active): on a topology change
// the HUD stays on its current display unless a fixed display is chosen; with
// no usable display left it closes without animation.
void Application::Impl::repositionHUD(double time){
    if(!visibleMode()||!ready||stopping||session->phase()==core::VisibilityPhase::concealed)return;
    const auto displays=gpu::readConnectedDisplays();
    std::optional<std::size_t> index;
    const auto preference=displayPreference();
    if(!preference.persistentID)for(std::size_t i=0;i<displays.size();++i)if(displays[i].bounds.valid()&&displayKey(displays[i])==hudDisplayID){index=i;break;}
    if(!index){POINT pointer{};(void)GetCursorPos(&pointer);index=gpu::resolveDisplay(preference,displays,{pointer.x,pointer.y});}
    if(!index){(void)lifecycle.forceClose();forceConceal(time);return;}
    placeOnDisplay(displays[*index]);
}
void Application::Impl::openPresentation(core::Module initial,double time){
    // First of all: a device released after a long close starts being
    // created on a worker while the display is chosen and the module selected.
    prepareGraphics(false);
    if(startupTrace&&session->phase()==core::VisibilityPhase::concealed)traceOpening(time);
    if(production()&&session->phase()==core::VisibilityPhase::concealed)targetDisplay();
    if(notes&&notes->selected()!=initial)selectModule(initial,time);
    open(time);if(visibleMode())host.show();refresh(time);
}
void Application::Impl::selectModule(core::Module module,double time){
    if(notes){const auto current=notes->selected();
        if(module!=current)if(auto*owner=registry.presenter(current)){bool releases=true;
            if(registry.guard(*owner,[&]{releases=owner->releasesSelection(hostTime(time));},"select")&&!releases){pendingModule=module;return;}}}
    pendingModule.reset();if(mediaPicker){mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();finishAreaPicker({true,false,{}});}
    if(module!=core::Module::display)deactivateDisplayPanels(); // the Display page's colour panel and logo import
    if(readerOwner&&module!=core::Module::reader)isolate(readerModuleOwner,"select",[&]{readerOwner->cancelImport();});
    pendingReaderPicker.reset();pendingReaderAction.reset();
    if(notes)notes->select(module,hostTime(time));
    automationChanged();
}
void Application::Impl::open(double time){
    time=hostTime(time);closing=false;canvasOpenedAt=time;session->open(time,0x5eed);
    concealedBroadcast=false;
    // The swap chain and composition target released while the HUD stayed
    // closed come back before the first frame, which the message loop gates
    // on their new frame latency object. A released device is created on a
    // worker meanwhile: the first frame's CPU work (pose, captions, owners)
    // runs before it takes the device for its uploads and draw, which then
    // also creates the presentation. A failure here is retried by that
    // frame (and then handled like any frame failure).
    graphicsIdleAt.reset();preparedGraphicsUntil.reset();idleGraphicsAttempted=idleDeviceSuspended=idleGraphicsRefused=false;
    try{if(!renderer.prepareDevice()&&!renderer.deviceSuspended()&&visibleMode())(void)renderer.restorePresentation();}catch(const std::exception&e){
        if(!production())throw;
        const auto line=std::string("EndfieldHUD could not restore the HUD's graphics (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
    // A game keeps the CPU and GPU busy: the shown HUD schedules ahead of it.
    if(visibleMode())presentationPriority.raise(&renderer);
    registry.forEach([&](ModuleOwner&o){o.setOverlayVisible(true,hostTime(time));},"open");
    automationChanged();
    if(audio)audio->volume().setVisible(true);
    if(visibleMode()&&headerClock.setActive(true,time))updateClock();
}
void Application::Impl::close(double time){
    if(closing)return;
    time=hostTime(time);
    if(!production())std::cout<<"Preview close requested at "<<time<<std::endl;
    if(registry.first(ModuleRegistry::Chain::finishEditing,[&](ModuleOwner&o,const ModuleRouting&){return !o.finishEditing(hostTime(time));},"close")){pendingClose=true;return;}
    pendingClose=false;pendingModule.reset();headerClock.setActive(false,time);if(mediaPicker)mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();if(archiveMedia)isolate(archiveModuleOwner,"close",[&]{archiveMedia->cancelImport();});if(readerOwner)isolate(readerModuleOwner,"close",[&]{readerOwner->cancelImport();});pendingReaderPicker.reset();pendingReaderAction.reset();
    finishAreaPicker({true,false,{}});deactivateDisplayPanels();setClockHovered(false);
    // SystemHUDView deactivation: clockStyleArtwork.resetGesture(), clockPage.cancelTransition().
    clockSwipe=0;clockSwipeConsumed=false;clockSwipeTime=-1;if(chrome)chrome->cancelStyleTransition();
    registry.forEach([&](ModuleOwner&o){o.overlayClosing(hostTime(time));},"close");if(audio)audio->volume().setVisible(false);automationChanged();
    time=hostTime(time);
    host.capturePointer(false);environment.pointerLocked=false;session->setEnvironment(environment,time);canvasCapturedOpacity=canvasOpacity(time);canvasClosedAt=time;closing=true;session->close(time);refresh(time);
}
void Application::Impl::forceConceal(double time){
    // OverlayController.forceCloseSystemOverlay: sleep, session end and
    // termination cannot wait for visible animations or editor retries.
    time=hostTime(time);
    pendingClose=false;pendingModule.reset();headerClock.setActive(false,time);pendingShelfReveal.reset();pendingShelfOpen.reset();pendingLink.reset();
    if(mediaPicker)mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();if(archiveMedia)isolate(archiveModuleOwner,"force close",[&]{archiveMedia->cancelImport();});if(readerOwner)isolate(readerModuleOwner,"force close",[&]{readerOwner->cancelImport();});pendingReaderPicker.reset();pendingReaderAction.reset();
    finishAreaPicker({true,false,{}});deactivateDisplayPanels();setClockHovered(false);
    clockSwipe=0;clockSwipeConsumed=false;clockSwipeTime=-1;if(chrome)chrome->cancelStyleTransition();
    if(session->phase()!=core::VisibilityPhase::concealed||closing){
        registry.forEach([&](ModuleOwner&o){o.overlayClosing(hostTime(time));},"force close");if(audio)audio->volume().setVisible(false);
        time=hostTime(time);host.capturePointer(false);environment.pointerLocked=false;session->setEnvironment(environment,time);
        closing=false;session->conceal(time);if(notes)notes->setMediaActive(false,hostTime(time));
    }
    lifecycleOpening.reset();lifecycleClosing.reset();
    host.hide();concealed(time);if(!stopping){host.setFrameDemand(demand(time));scheduleDeadline();}
}
bool Application::Impl::flushDocuments(double time,bool reopenOnFailure){
    // Archive, Calendar and Reader drains share the executor; then Settings.
    ModuleOwner*failed=registry.first(ModuleRegistry::Chain::flush,[&](ModuleOwner&o,const ModuleRouting&){return !o.flush(hostTime(time));},"flush");
    if(failed){
        if(!production())std::cerr<<"Document flush declined by "<<failed->name()<<'\n';
        if(reopenOnFailure){
            const auto module=failed==archiveModuleOwner.get()?core::Module::archive:failed==calendarModuleOwner.get()?core::Module::calendar:core::Module::reader;
            reopenAfterSaveFailure(module,time);
        }
        return false;
    }
    // Flush only at an explicit shutdown boundary. A failed save retains its
    // record and restores the Settings UI instead of silently quitting.
    if(settingsSaves&&!settingsSaves->flush()){
        if(!production())std::cerr<<"Document flush declined by the preference store\n";
        if(reopenOnFailure){if(settingsUI)settingsUI->controller().setStatus(settingsSaves->status().error.value_or("Unable to save settings"));reopenAfterSaveFailure(core::Module::system,time);}
        return false;
    }
    // Event Log persistence uses the owner clock, which only moves forward.
    if(eventSaves)eventSaves->flush(now());
    return true;
}
void Application::Impl::reopenAfterSaveFailure(core::Module module,double time){
    quitRequested=false;
    declineRestart(); // a failed save cancels a pending restart as it cancels the quit
    perform(lifecycle.cancelTermination(),time);
    // Windows keeps the failing module visible (Mac only re-enables input):
    // a silent no-op quit would hide the unsaved record from the user.
    if(lifecycle.phase()==core::SystemOverlayPhase::open){selectModule(module,time);return;}
    perform(lifecycle.openSettingsModule(module),time);
}
void Application::Impl::finishQuit(double time){
    if(!flushDocuments(time,true))return;
    // An accepted DataRootRestart: the work runs after this owner is gone.
    if(restartPending){restartPending=false;restartAccepted=true;restartDeclined={};}
    projectionHandoff.cancel();(void)projectionCall("quit",[&]{projection->close(hostTime(time));});
    stopping=true;host.setDeadline({});host.setFrameDemand({});host.requestStop();
}
void Application::Impl::trayAction(gpu::TrayAction action,bool hotkey,double time){
    if(projectionHandoff.active()){projectionTrayAction=action;refresh(time);return;}
    switch(action){
    case gpu::TrayAction::quit:perform(lifecycle.requestTermination(),time);break;
    case gpu::TrayAction::openOverlay:perform(hotkey?lifecycle.toggle():lifecycle.openOverlay(),time);break;
    case gpu::TrayAction::settings:perform(lifecycle.openSettingsModule(core::Module::system),time);break;
    case gpu::TrayAction::about:perform(lifecycle.openSettingsModule(core::Module::about),time);break;
    case gpu::TrayAction::checkUpdates:perform(lifecycle.openSettingsModule(core::Module::about),time);if(auto*u=updates())(void)isolateService(UpdateService::key,"update check",[&]{(void)u->check(hostTime(time));});break;
    case gpu::TrayAction::previewCharging:if(auto*p=powerArea())(void)isolateService(power::PowerAreaService::key,"preview",[&]{p->preview(hostTime(time));});break;
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
    case core::ForwardedActivation::toggle:
        // The summon hotkey's open/close toggle (the section 9 gate drives it).
        if(projectionHandoff.active()){projectionTrayAction=gpu::TrayAction::openOverlay;refresh(time);break;}
        perform(lifecycle.toggle(),time);
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
    // A session end never runs a pending restart's work (Mac import commit);
    // its decline runs now, on this UI thread, before the loop stops.
    declineRestart();runRestartDeclined();
    const auto time=now();const auto window=static_cast<HWND>(host.hwnd());
    const auto reasonText=wide(core::localized("Saving EndfieldHUD data…","正在保存 EndfieldHUD 数据…",watchAppearance.language==core::Language::system?core::Language::english:watchAppearance.language));
    const bool blocked=ShutdownBlockReasonCreate(window,reasonText.c_str())!=FALSE;
    try{
        if(phase==SessionEnd::ending){
            sessionEnded=true;(void)lifecycle.forceClose();forceConceal(time);broadcast(HostEvent::Kind::quitting,time);
            if(archive)isolate(archiveModuleOwner,"session end",[&]{(void)archive->finishEditing(hostTime(time));});
            if(calendar)isolate(calendarModuleOwner,"session end",[&]{(void)calendar->dismissMenu(false,hostTime(time));});
            if(workMode)isolate(workModeModuleOwner,"session end",[&]{workMode->shutdown(hostTime(time));});
        }
        (void)flushDocuments(time,false);
    }catch(...){if(blocked)ShutdownBlockReasonDestroy(window);throw;}
    if(blocked)ShutdownBlockReasonDestroy(window);
    if(phase==SessionEnd::ending){
        if(summonHook)summonHook->stop();
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
    if(!configurationPending)return;configurationPending=false;time=hostTime(time);
    // HUDRuntimeAppearance.reduceMotion: the preference or the system's
    // reduce-motion setting (Windows: "Animation effects" off,
    // SPI_GETCLIENTAREAANIMATION), re-read on WM_SETTINGCHANGE; never saved.
    if(visibleMode()){BOOL animations=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0))systemAnimationsOff=animations==FALSE;}
    settings={configuration.boolean("reduceMotion")||systemAnimationsOff,configuration.boolean("ambientAnimation"),configuration.boolean("lowPowerVisualMode"),configuration.number("parallaxIntensity"),configuration.number("perspectiveIntensity"),configuration.number("hudScale"),{configuration.number("hudOffsetX"),configuration.number("hudOffsetY")}};
    session->setSettings(settings,time);watchAppearance.language=settingsLanguage(configuration);if(audio)audio->setLanguage(watchAppearance.language);
    rasterizer.setDefaultFontLanguage(watchAppearance.language==core::Language::korean?gpu::LayerFontLanguage::korean:gpu::LayerFontLanguage::simplifiedChinese);
    watchAppearance.dark=configuration.string("theme")!="light";
    if(configuration.string("theme")=="system"){DWORD light=0,size=sizeof(light);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size)==ERROR_SUCCESS)watchAppearance.dark=light==0;}
    const auto hex=configuration.string("accentHex");for(unsigned n=0;n<3;++n)watchAppearance.accentSRGB[n]=std::stoul(hex.substr(n*2,2),nullptr,16)/255.;
    const auto a=watchAppearance.accentSRGB;const auto effective=watchAppearance.dark?a:source::sourceBlackBlend(a,.35);const std::array<double,4>accent{effective[0],effective[1],effective[2],1};
    backdropState.dark=watchAppearance.dark;backdropState.blurAmount=configuration.number("blurAmount");backdropState.backgroundDarkness=configuration.number("backgroundDarkness");
    bindNavigation(time);
    headerClock.setFormat(configuration.string("clockFormat")=="twelveHour"?modules::HUDClockFormat::twelveHour:modules::HUDClockFormat::twentyFourHour,time);
    const auto previousClockStyle=chromeContent.style;
    const auto clock=configuration.string("clockStyle");chromeContent.style=clock=="split"?source::DesktopClockStyle::split:clock=="dial"?source::DesktopClockStyle::dial:clock=="rail"?source::DesktopClockStyle::rail:clock=="stacked"?source::DesktopClockStyle::stacked:source::DesktopClockStyle::digital;
    chromeContent.localizedClose=core::localized("Click outside to close","点击外侧关闭",watchAppearance.language);chromeContent.uppercaseShortcut=summonHotkeyEnabled()?modules::settingsShortcutTitle(modules::settingsShortcut(configuration)):std::string{};
    if(chrome){chrome->setAppearance({watchAppearance.dark,watchAppearance.dark?a:source::sourceBlackBlend(a,.35)});updateClock();if(chromeContent.style!=previousClockStyle)clockStyleChanged(previousClockStyle,time);}
    updateCenterLogo(); // setDesktopCenterLogo(configuration.centerLogo, revision)
    if(tray&&settingsUI&&!settingsUI->controller().capturingShortcut()&&!quitRequested)applySummonShortcut(false);
    moduleAppearance={watchAppearance.language,watchAppearance.dark,accent,{a[0],a[1],a[2]},settings.reduceMotion,settings.ambientEnabled,settings.lowPower};
    registry.forEach([&](ModuleOwner&o){o.setAppearance(moduleAppearance,hostTime(time));},"appearance");
    // Applied inside the frame being prepared (or before the first one): no extra refresh.
    (void)registry.broadcast({HostEvent::Kind::settingsChanged,0},time);
    refreshUpdateStatus();
    applyTrayIcon();
    if(production())updateTrayMenu();
    // Baseline is the loaded record (AppDelegate.applicationDidFinishLaunching).
    if(eventRecorder&&settingsLoaded)eventRecorder->receiveConfiguration(core::RecorderDisplaySettings::from(configuration));
}
core::FrameDemand Application::Impl::demand(double time){
    time=hostTime(time);
    if(projectionShown()){core::FrameDemand value;if(projectionCall("demand",[&]{value=projection->demand(hostTime(time));}))return value;}
    auto result=session->demand(time);
    if(result.phase==core::VisibilityPhase::visible)result.finiteAnimation=result.finiteAnimation||(mediaBroker&&mediaBroker->requiresFrames())||registry.requiresFrames(time)||(chrome&&chrome->styleTransitionActive());
    return result;
}
void Application::Impl::scheduleDeadline(){if(!liveScheduling()||!ready||stopping)return;std::optional<double>next;
    auto include=[&](std::optional<double>value){if(value&&(!next||*value<*next))next=value;};
    // After the grace, while the HUD stays closed: graphics that came back
    // (a call that needed the GPU) or that a media owner kept and has now
    // let go are released again shortly.
    if(!graphicsIdleAt&&idleGraphicsAttempted&&session->phase()==core::VisibilityPhase::concealed&&renderer.stats().initialized&&!renderer.deviceSuspended()&&
       !projectionHandoff.active()&&!projectionShown()&&(idleDeviceSuspended||(idleGraphicsRefused&&renderer.deviceReleasable()))){
        idleGraphicsAttempted=false;graphicsIdleAt=now()+std::min(closedGraphicsRelease,closedGraphicsGrace());}
    include(calendarNextWake);include(graphicsIdleAt);include(preparedGraphicsUntil);if(projection)include(projection->dismissalDeadline());include(headerClock.nextDeadline());include(registry.nextWakeTime(now()));
    if(mediaBroker){const auto wake=mediaBroker->nextWakeTime();if(wake&&*wake>now())include(wake);}if(eventOwner)include(eventOwner->saveDeadline());if(activityProbe)include(activityPlan.nextWakeTime());include(lifecycle.nextWakeTime());host.setDeadline(next);
}
void Application::Impl::refresh(double time){if(liveScheduling()&&ready&&!stopping){
    // A content event: its sample may predate a time an owner already saw
    // inside the event (a nested capture-lost message, an owner clock read).
    time=hostTime(time);
    syncActivityTelemetry(time);
    hudDropRoute(time);
    refreshAutomation();
    projectionDrop->enabled=projectionShown()&&projectionHandoff.acceptsInput()&&!projection->preview()->importBusy()&&pickerOwner==PickerOwner::none;
    projectionDrop->scale=metrics.scale;
    if(projectionShown())(void)projectionCall("actions",[&]{while(auto action=projection->preview()->takeAction())projectionActions.push_back(std::move(*action));});
    if((!projectionActions.empty()||projectionTrayAction||projectionDisplayPending||projectionResizePending||(projectionShown()&&projection->dismissalComplete(time)))&&!projectionActionsQueued&&!projectionPainting){need(PostMessageW(static_cast<HWND>(host.hwnd()),projectionActionMessage,serviceGeneration,0)!=FALSE,"Post Projection actions");projectionActionsQueued=true;}
    if(eventRecorder&&workMode)eventRecorder->receiveWork(workMode->controller().snapshot(hostTime(time)));
    if(reader&&!readerActionQueued){std::optional<tools::ReaderImportAction>action;isolate(readerModuleOwner,"import",[&]{action=reader->takeImportAction();});
        if(action){pendingReaderAction=std::move(*action);need(PostMessageW(static_cast<HWND>(host.hwnd()),readerActionMessage,serviceGeneration,0)!=FALSE,"Post Reader import action");readerActionQueued=true;}}
    if(workMode)updateClock();host.setFrameDemand(demand(time));scheduleDeadline();host.invalidate();}}
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
    // The clock style push's two pages sit right above the status panel.
    if(chrome)if(auto*pages=chrome->transitionScene())ordered.push_back({pages,{}});
    registry.forEach([&](ModuleOwner&o){const auto begin=probe.enabled?Clock::now():Clock::time_point{};o.upload(renderer);if(probe.enabled)probe.owner(o.name(),true,begin);},"upload");
    // The old wrapper is below the incoming wrapper, independently of owner
    // registration order. Floating Notes stay above center modules; modal
    // confirmations stay above everything.
    const auto appendModule=[&](core::Module module,const gpu::PlaneAlphaMask*cut){auto*owner=registry.presenter(module);if(!owner||std::find(appendedOwners.begin(),appendedOwners.end(),owner)!=appendedOwners.end())return;appendedOwners.push_back(owner);
        registry.guard(*owner,[&]{for(const auto&e:owner->entries(module)){ordered.push_back(e);if(cut&&!ordered.back().cut)ordered.back().cut=cut;}},"publish");};
    // SystemHUDView.applySourceMapOcclusion masks the whole module host while
    // the Map is presented, its switches included: during a switch into or
    // out of the Map the other module's content and registration brackets are
    // cut by the Storage and Activity Monitor plates too (the Map cuts its own
    // artwork and brackets; MapPreview::transitionCut, valid after upload).
    if(notes){const auto&sample=notes->modulePresentation();
        const bool mapSwitch=map&&sample.incoming&&(sample.current.module==core::Module::map||sample.incoming->module==core::Module::map);
        const auto*cut=mapSwitch?map->transitionCut():nullptr;
        appendModule(sample.current.module,sample.current.module==core::Module::map?nullptr:cut);
        if(sample.incoming)appendModule(sample.incoming->module,sample.incoming->module==core::Module::map?nullptr:cut);}
    // Core-plane layers (e.g. the Power charge badge) sit inside the canvas,
    // below the floating Notes workspace (SystemHUDView view order).
    registry.forEach([&](ModuleOwner&o){for(const auto&e:o.planeEntries())ordered.push_back(e);},"publish");
    registry.forEach([&](ModuleOwner&o){for(const auto&e:o.floatingEntries())ordered.push_back(e);},"publish");
    registry.forEach([&](ModuleOwner&o){for(const auto&e:o.modalEntries())ordered.push_back(e);},"publish");
    // A language event can leave equal captions/numbers in retained scenes.
    // Refresh only an older font generation; group carriers forward this to
    // their cached local text, while editor owners keep layout/TSF together.
    const auto fontRevision=rasterizer.fontRevision();
    for(const auto&e:ordered)if(e.scene->fontRevision()!=fontRevision)e.scene->refreshTypography();
    const auto notesRevision=notes?notes->compositionRevision():0;
    bool changed=ordered.size()!=published.size()||notesRevision!=publishedNotesRevision;if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count||e.cut!=p.cut){changed=true;break;}}
    // Skill selection and sprite swaps reuse their draw buffer. Pointer and
    // count alone cannot detect those resource-identity changes.
    if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
    if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size(),e.cut});
        registry.forEach([&](ModuleOwner&o){o.collected(renderer);},"collect");if(archiveMedia)archiveMedia->collectRetired();if(mediaBroker)mediaBroker->collectRetired();}
    publishedNotesRevision=notesRevision;
}
// The source shell's pose for one frame (present() and the launch prewarm):
// the sampled source frame, its compiled materials, the navigation captions,
// the bound appearance and the chrome. Null when nothing is shown.
const WatchSessionFrame* Application::Impl::poseShell(double time){
    const WatchSessionFrame*sample=nullptr;{auto stage=probe.measure(LiveProbe::sourcePose);sample=session->sample(time);}if(!sample)return nullptr;
    if(focused&&sample->visibility.phase==core::VisibilityPhase::visible&&!session->inputEnabled())session->setInputEnabled(true,time);
    // Scrolling rebinds the navigation's source buttons: their UI Automation
    // names follow once a client asked (one integer compare per frame otherwise).
    if(automation){const auto bindings=session->stats().bindingChanges;if(bindings!=navigationBindingChanges){navigationBindingChanges=bindings;automationChanged();refreshAutomation();}}
    auto parameters=materials->parameters();parameters.camera=sample->gpuCamera;parameters.timeSeconds=sample->shaderTime;parameters.width=metrics.pixelWidth;parameters.height=metrics.pixelHeight;if(nativeAppearance){std::array<float,3>linear;for(unsigned n=0;n<3;++n){const auto value=watchAppearance.accentSRGB[n];linear[n]=static_cast<float>(value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4));}parameters.desktopAccentLinear=linear;}
    // The upload (materials->flush) waits for the frame's publication, so a
    // device created again after a long close (Renderer::prepareDevice)
    // overlaps the CPU work before it.
    {auto stage=probe.measure(LiveProbe::materials);materialPresentation->update(*sample->sourceFrame,parameters);}
    {auto stage=probe.measure(LiveProbe::nativeLabels);if(navigationDirty)bindNavigation(time);if(nativeContent)nativeContent->update(session->actions());
    for(auto&button:available){const auto found=session->actions().find(button.buttonID);button.enabled=found!=session->actions().end();button.expandFileShelfCaption=button.enabled&&nativeAppearance&&nativeAppearance->expandsFileShelfCaption(found->second,watchAppearance.language);}
    const core::Rect viewport{0,0,metrics.width,metrics.height};labels->update(*sample->sourceFrame,sample->camera,viewport,available);
    if(nativeAppearance){watchAppearance.selectedAction=notes?contentCatalog->actionForTarget(core::moduleIdentifier(notes->selected())).value_or(0):0;nativeAppearance->update(session->actions(),watchAppearance,labels->placements());}
    // Chrome samples once per visible second; opacity follows the same
    // ready/close timestamps as the source outer controller.
    const auto frameChrome=chromeSettings(notes?notes->selected():core::Module::power);
    // HUDClockPageViewport: the style push advances on this frame clock.
    if(chrome){chrome->advanceStyleTransition(time);chrome->update(*sample->sourceFrame,sample->camera,frameChrome,static_cast<float>(canvasOpacity(time)));}
    }
    return sample;
}
// Launch prewarm (Mac AppDelegate prewarmDesktop/prewarmDesktopResources, on
// the utility queue after launch): once, while the HUD is still closed and
// the run is idle, the first opening's work moves off the first frame. The
// source shell poses its held first opening pose - navigation captions,
// chrome text, bound appearance and compiled materials rasterized and
// uploaded, one draw submitted to the hidden back buffer and never presented
// - then conceals again; Map's bundled geography decode (the first summon
// opens on Map) is queued on the shared executor. Owners are not opened.
// Posted once after the preference record applied (no timer, no polling);
// skipped once anything opened the HUD. A production failure only skips it.
void Application::Impl::prewarm(){
    if(prewarmed||!ready||stopping||!session||session->phase()!=core::VisibilityPhase::concealed||
       lifecycle.phase()!=core::SystemOverlayPhase::closed||projectionHandoff.active())return;
    prewarmed=true;const auto begin=Clock::now();const auto time=now();
    const auto work=[&]{
        if(map&&notes&&notes->selected()==core::Module::map)requestMapGeography();
        if(idCard)if(auto next=idCard->apply())session->setDesktopSettings(std::move(*next));
        (void)session->open(time,0x5eed,true);
        try{
            if(poseShell(time)){
                // The owners' first-update work (ModuleOwner::prewarm), on the first
                // opening's module presentation while they are still closed.
                if(notes&&chromePlan&&chromePlan->projection().center){
                    const auto prewarmChrome=chromeSettings(notes->selected());
                    registry.forEach([&](ModuleOwner&o){const ModuleFrame frame{*chromePlan->projection().center,prewarmChrome,notes->modulePresentation(),0.f,hostTime(time),false};o.prewarm(frame);},"prewarm");
                }
                materials->flush(renderer.sourceGraphics());publishNative();composition.present(renderer);
                renderer.setCamera(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale,1));
                renderer.draw(false);
            }
        }catch(...){session->conceal(hostTime(time));throw;}
        session->conceal(hostTime(time));
        // The prewarm frame drew into full-size frame targets and grew the
        // driver's pools; the HUD stays closed after launch, so give them back
        // as a close does (the first opening creates the targets again).
        releaseConcealedGraphics();
    };
    if(!production())work();
    else try{work();}catch(const std::exception&e){
        const auto line=std::string("EndfieldHUD launch prewarm was skipped (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
    if(startupTrace)startupTrace->stage("launch-prewarm",begin,Clock::now());
    if(memoryTrace&&production())recordMemory("launch-prewarm",true);
}
bool Application::Impl::present(double time,bool submit){
    const auto traceFrameStart=readerTrace?Clock::now():Clock::time_point{};
    // Frame start: the swap chain accepts this frame, so input and time are
    // sampled after it and Present neither blocks behind a GPU that a game
    // saturates nor queues frames ahead of the display. The host's pump
    // normally took that admission while it dispatched input (the wait below
    // then returns at once); a frame run from a nested modal loop waits here,
    // bounded. Projection frames too: without it they outran the display and
    // queued ~35 frames (200 ms) behind the pointer. Offscreen returns at once.
    if(submit&&visibleMode())(void)renderer.waitForPresentation(50);
    time=hostTime(time);
    if(projectionShown()){
        projectionPainting=true;try{mediaBroker->sample(time);(void)projectionCall("frame",[&]{projection->mediaChanged(hostTime(time));projection->render(hostTime(time),submit);});projectionPainting=false;}catch(...){projectionPainting=false;throw;}return true;
    }
    readerTime=time;mapTime=time;calendarTime=time;if(!visibleMode())hiddenClock=std::max(hiddenClock,time);
    if(!game&&gameEnabled&&notes&&notes->selected()==core::Module::minigame)ensureGame(time);
    if(map&&notes&&notes->selected()==core::Module::map)requestMapGeography();
    if(storageFixture)storageFixture->time.store(time,std::memory_order_relaxed);
    auto frameProbe=probe.measure(LiveProbe::frame);
    {auto stage=probe.measure(LiveProbe::prepare);applyConfiguration(time);
    if(clipboardDirty&&clipboard&&notes&&notes->selected()==core::Module::clipboard){isolate(clipboardModuleOwner,"refresh",[&]{clipboard->refresh();});clipboardDirty=false;}}
    // Backdrop COM calls can dispatch nested input. Finish them before
    // borrowing a source frame, then sample the current live event time.
    {auto stage=probe.measure(LiveProbe::backdrop);updateBackdrop(time);}time=visibleMode()?now():hostTime(time);
    // The ID card feed published: repaint captions/textures and rebind the
    // card's desktop settings (nothing happens and nothing allocates otherwise).
    if(idCard)if(auto next=idCard->apply())session->setDesktopSettings(std::move(*next));
    if(!poseShell(time))return false;
    const auto noteChromeSettings=chromeSettings(notes?notes->selected():core::Module::power);
    if(notes&&chromePlan&&chromePlan->projection().center){
        auto stage=probe.measure(LiveProbe::notesPose);
        // Notes first: it owns the module presentation sample every other
        // owner reads in this same frame.
        const auto opacity=static_cast<float>(canvasOpacity(time));
        // Each owner receives the host time again (an earlier owner's update
        // may dispatch a nested message with a later time).
        registry.forEach([&](ModuleOwner&o){const auto begin=probe.enabled?Clock::now():Clock::time_point{};
            const ModuleFrame frame{*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),opacity,hostTime(time),focused};o.update(frame);
            if(probe.enabled)probe.owner(o.name(),false,begin);},"frame");
    }
    // Visibility unions are established first. Providers tick exactly once
    // on this existing frame clock; module owners only reread their clients.
    if(archiveMedia)isolate(archiveModuleOwner,"frame",[&]{archiveMedia->sync(hostTime(time));});
    // ArchiveController actions (created/edited/deleted/category*/media*) ->
    // Event Log archiveAction; an empty drain allocates nothing.
    if(archiveService)isolate(archiveModuleOwner,"frame",[&]{auto actions=archiveService->state().takeEvents();if(eventOwner&&!stopping)for(const auto&e:actions)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::archiveAction,ehud::data::foundationNow(),{{"action",e.name}}});});
    if(mediaBroker){mediaBroker->sample(time);if(notes)notes->refreshSharedMedia(hostTime(time));
        // A hidden Archive is not updated (its adapter builds it when shown).
        if(archiveMedia)isolate(archiveModuleOwner,"frame",[&]{if(archiveMedia->refresh(hostTime(time))&&archive&&notes&&chromePlan&&chromePlan->projection().center&&presentationShows(notes->modulePresentation(),core::Module::archive))archive->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),hostTime(time));});}
    {auto stage=probe.measure(LiveProbe::materials);materials->flush(renderer.sourceGraphics());}
    {auto stage=probe.measure(LiveProbe::publication);publishNative();composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale,1));}
    if(submit){auto stage=probe.measure(LiveProbe::draw);renderer.draw(visibleMode());
        // A finished module switch: the outgoing module's native group targets
        // are no longer drawn and give their GPU memory back (created again and
        // re-rendered if that module is shown again). Once per switch. No
        // driver trim here: it waits for the GPU's queued frames (measured
        // 190-580 ms behind back-to-back 2560 x 1600 frames); the closed HUD
        // trims instead (releaseConcealedGraphics).
        if(notes){const bool switching=notes->modulePresentation().transitioning;if(switchPresented&&!switching)(void)renderer.releaseHiddenGroupTargets();switchPresented=switching;}}
    if(centerLogoRetire)retireCenterLogo();
    if(readerTrace&&reader&&notes&&notes->selected()==core::Module::reader)readerTrace->record(false,time,reader->scrollSnapshot(),0,0,std::chrono::duration<double,std::milli>(Clock::now()-traceFrameStart).count());return true;
}
bool Application::Impl::requestMediaPicker(){
    if(pickerOwner!=PickerOwner::none||!mediaPicker)return false;
    mediaPicker->setLabels({core::localized("Add image/video","添加图片/视频",watchAppearance.language),core::localized("Add","添加",watchAppearance.language),core::localized("Add selected files","添加所选文件",watchAppearance.language),gpu::ShelfPickerMode::mixedReferences});
    return mediaPicker->request();
}
void Application::Impl::importReaderReference(std::uint64_t generation,std::string path,std::shared_ptr<void>lease,double time){
    if(!reader||!readerOwner)return;
    isolate(readerModuleOwner,"import",[&]{if(!reader->beginImport(generation,hostTime(time)))return;
        try {auto book=modules::windowsReaderReference(ehud::data::makeUUID(),path,utf8(utf8Path(path).filename()));readerOwner->import(generation,std::move(book),std::move(lease));}
        catch(const std::exception&){reader->receiveImportError(generation,modules::readerImportFailure(std::current_exception(),watchAppearance.language),hostTime(time));}});
}
void Application::Impl::activate(const WatchActivation&event){
    if(event.action==quitAction){
        // SystemHUDView.presentQuitConfirmation: dims the HUD, follows its
        // tilt and owns input until Cancel or Quit.
        // SystemHUDView.allowsModuleInput: not during a module swap, a pending
        // confirmation or an accepted quit.
        if(quitConfirmation&&lifecycle.phase()==core::SystemOverlayPhase::open&&!quitRequested&&!quitConfirmation->presented()&&
           !(notes&&notes->modulePresentation().transitioning)){const auto time=now();(void)registry.guard(*quitConfirmation,[&]{quitConfirmation->present(hostTime(time));},"quit confirmation");session->pointerMove(environment.pointer,time);refresh(time);}
        else if(!quitConfirmation)perform(lifecycle.requestQuit(),now());
        return;
    }
    if(event.action==closeAction){perform(lifecycle.close(),now());return;}
    const auto entries=contentCatalog->entries();const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto&value){return value.action==event.action;});
    // Saved app shortcuts bound between Power and Add App (close-first launch).
    if(entry==entries.end()){if(auto*apps=appNavigation()){bool launched{};(void)isolateService(appShortcutNavigationService,"app shortcut",[&]{launched=apps->activate(event.action,now());});if(launched)return;}}
    need(entry!=entries.end(),"Source activation exceeds exported actions");
    if(args.projection&&entry->target=="projection"){
        // A Projection that failed in this run stays closed (Impl::projectionCall).
        if(!projectionAvailable())return;
        if(auto command=projectionHandoff.open({session->phase()==core::VisibilityPhase::visible,quitRequested,pendingClose, bool(shelf&&shelf->preservesFocusOnLoss())},{hudDisplay,previousApplication})){
            // Mac isProjectionActive: no charge alert over the handoff/Projection.
            setProjectionActive(true,now());projectionCommand(*command,now());}
        return;
    }
    if(notes){for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);if(core::moduleIdentifier(module)==entry->target){
        const auto effects=lifecycle.selectModule(module);if(effects.empty())selectModule(module,now());else perform(effects,now());break;}}}
    if(!production())std::cout<<"Source action: "<<entry->target<<'\n';
}

// ---------------------------------------------------------------------------
// Host routes into module areas
// Source desktop navigation with the saved app shortcuts (AppShortcutNavigation
// host contract): entries are bound before the session's right-side actions.
void Application::Impl::bindNavigation(double time){
    navigationDirty=false;if(!nativeAppearance)return;
    std::vector<gpu::WatchAppearanceEntry>entries;
    for(const auto&e:contentCatalog->entries())entries.push_back({e.action,e.target,moduleCaption(e.target,watchAppearance.language,e.title),"module:"+e.target,true});
    auto navigation=baseNavigation;
    if(auto*apps=appNavigation()){
        try{
            const auto plan=apps->plan(entries,baseNavigation.rightActions);
            std::vector<gpu::WatchAppearanceEntry>combined(plan.entries.begin(),plan.entries.end());
            navigation.rightActions.assign(plan.rightActions.begin(),plan.rightActions.end());
            entries=std::move(combined);
        }catch(const std::exception&e){
            if(!production())throw;
            const auto line=std::string("EndfieldHUD app shortcut navigation failed (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
            navigation=baseNavigation;
        }
    }
    navigationEntries=entries; // the navigation's UI Automation names (application_navigation.cpp)
    nativeAppearance->setEntries(entries);
    // SourceWatchSession relays out the rows and resets the scroll when the
    // right-side count changes (Mac setDesktopNavigation).
    session->setNavigation(std::move(navigation),time);
}
// OverlayController.reconcileChargeMetricTelemetry: the shared Activity
// sampler stays attached while the charge badge/alert shows a CPU/RAM/
// Network/Disk metric (or the Windows GPU total load), and each system
// sample reaches the Power area.
void Application::Impl::syncActivityTelemetry(double time){
    if(!activityTelemetry||stopping)return;
    time=hostTime(time);
    auto*charge=powerArea();bool wanted{};
    if(charge&&!isolateService(power::PowerAreaService::key,"telemetry",[&]{wanted=charge->telemetryDemand();}))charge=nullptr;
    const bool rescheduled=activityTelemetry->synchronize(charge&&wanted,time,[&](const modules::ActivitySnapshot*sample){
        if(!charge)return;
        std::optional<modules::ChargeTelemetry> value;
        // Every field, the Activity GPU total load (gpuPercent) included.
        if(sample)value=modules::chargeTelemetry(*sample);
        (void)isolateService(power::PowerAreaService::key,"telemetry",[&]{charge->setTelemetry(value,hostTime(time));});});
    if(rescheduled&&activityProbe){activityProbe->update(time);scheduleDeadline();}
}
// Mac OverlayController.isProjectionActive (handoff or Projection presented).
void Application::Impl::setProjectionActive(bool active,double time){if(auto*p=powerArea())(void)isolateService(power::PowerAreaService::key,"projection",[&]{p->setProjectionActive(active,hostTime(time));});}

// ---------------------------------------------------------------------------
// HUD file drops (SystemHUDView.performDragOperation). The window has ONE OLE
// drop target, owned by File Shelf; while another module is selected the host
// installs its external callbacks. The COM callbacks only hit-test and queue
// (no file reads, focus, navigation or mutation); hudDropMessage delivers the
// paths to the module after the callback returned. Precedence: Shelf
// navigation entry, then the selected module (an area owner's drop(): App
// shortcuts, Media Assembly; Reader; Archive with an open document; Notes).
void Application::Impl::hudDropRoute(double time){
    if(!shelf)return;
    // Projection installs its own route while it is presented.
    if(projectionHandoff.active()||projectionShown()){hudDropActive=false;return;}
    const bool wanted=visibleMode()&&ready&&!stopping&&notes&&!closing&&!quitRequested&&session->phase()==core::VisibilityPhase::visible&&
        notes->selected()!=core::Module::fileShelf&&pickerOwner==PickerOwner::none;
    if(wanted==hudDropActive)return;
    hudDropActive=wanted;hudDrop->paths.clear();hudDrop->posted=false;
    if(!wanted){if(dropHoverOwner)(void)dropHover({},std::nullopt);isolate(shelfModuleOwner,"drop route",[&]{shelf->setExternalDropCallbacks({},hostTime(time));});return;}
    hudDrop->hwnd=static_cast<HWND>(host.hwnd());hudDrop->scale=metrics.scale;hudDrop->owner=this;
    const std::weak_ptr<HudDropInbox>weak=hudDrop;
    const auto clientPoint=[](const HudDropInbox&state,POINT screen)->std::optional<core::Point>{
        if(!ScreenToClient(state.hwnd,&screen))return std::nullopt;RECT rect{};if(!GetClientRect(state.hwnd,&rect)||!PtInRect(&rect,screen))return std::nullopt;
        return core::Point{screen.x/state.scale,screen.y/state.scale};};
    isolate(shelfModuleOwner,"drop route",[&]{shelf->setExternalDropCallbacks(gpu::ShelfDropTarget::Callbacks{
        [weak,clientPoint](POINTL p){auto state=weak.lock();if(!state||!state->owner||state->posted)return false;
            const auto point=clientPoint(*state,POINT{p.x,p.y});return point&&state->owner->dropTarget(*point).has_value();},
        [weak,clientPoint](std::span<const std::string>paths){auto state=weak.lock();if(!state||!state->owner||state->posted||paths.empty())return false;
            POINT cursor{};if(!GetCursorPos(&cursor))return false;const auto point=clientPoint(*state,cursor);if(!point)return false;
            auto target=state->owner->dropTarget(*point);if(!target)return false;
            // Source documentDropURLs: only the files the target reads.
            std::vector<std::string>accepted;
            if(*target=="reader"){if(auto first=modules::readerDroppedReference(paths))accepted.push_back(std::move(*first));}
            else if(*target=="archive"||*target=="notes"){for(const auto&path:paths)if(modules::notesMediaFileName(path)&&accepted.size()<(*target=="archive"?16u:paths.size()))accepted.push_back(path);}
            else accepted.assign(paths.begin(),paths.end());
            if(accepted.empty())return false;
            state->target=std::move(*target);state->point=*point;state->paths=std::move(accepted);
            if(!PostMessageW(state->hwnd,hudDropMessage,serviceGeneration,0)){state->paths.clear();return false;}
            state->posted=true;return true;},
        // Drag feedback (draggingUpdated): area owners show their drop target
        // and refuse what they would not take before the drop.
        [weak,clientPoint](std::span<const std::string>paths,std::optional<POINTL>p){auto state=weak.lock();if(!state||!state->owner)return false;
            std::optional<core::Point>point;if(p)point=clientPoint(*state,POINT{p->x,p->y});return state->owner->dropHover(paths,point);}},hostTime(time));});
}
// Drag feedback over the HUD (UI thread, inside the COM callback): the area
// owner presenting the selected module flips its own drop-target state; a
// move off it, a leave or the drop ends that owner's highlight.
bool Application::Impl::dropHover(std::span<const std::string>paths,std::optional<core::Point>point){
    if(!notes||stopping)return false;
    const auto time=areaClock();
    ModuleOwner*owner=nullptr;
    if(point&&dropTarget(*point)==std::optional<std::string>("area"))owner=registry.presenter(notes->selected());
    if(dropHoverOwner&&dropHoverOwner!=owner){auto*previous=std::exchange(dropHoverOwner,nullptr);
        if(registry.contains(*previous))(void)registry.guard(*previous,[&]{(void)previous->dropHover(paths,std::nullopt,hostTime(time));},"drop hover");host.invalidate();}
    if(!owner)return true;
    bool accepted{};
    if(registry.guard(*owner,[&]{accepted=owner->dropHover(paths,point,hostTime(time));},"drop hover"))dropHoverOwner=owner;
    host.invalidate();
    return accepted;
}
// Read-only hit test of the drop route (UI thread, inside the COM callback).
std::optional<std::string> Application::Impl::dropTarget(core::Point point)const{
    if(!notes||stopping||closing||quitRequested||!session->inputEnabled()||session->phase()!=core::VisibilityPhase::visible)return std::nullopt;
    if(quitConfirmation&&quitConfirmation->presented())return std::nullopt;
    // A navigation button: only the File Shelf entry takes files (isShelfNavigationDrop).
    if(const auto id=session->buttonAt(point)){
        const auto&actions=session->actions();const auto action=actions.find(*id);if(action==actions.end())return std::nullopt;
        for(const auto&e:contentCatalog->entries())if(e.action==action->second&&e.target=="fileShelf")return std::string("shelfNavigation");
        return std::nullopt;
    }
    // Pinned Notes cards above the module keep the drop for themselves.
    const auto selected=notes->selected();
    const bool overNotes=selected!=core::Module::notes&&notes->covers(point);
    if(overNotes)return std::nullopt;
    if(auto*owner=registry.presenter(selected)){
        for(const auto&area:externalModules)if(area.owner.get()==owner)return registry.enabled(*owner)?std::optional<std::string>("area"):std::nullopt;
    }
    if(selected==core::Module::reader&&reader&&reader->acceptsDrop(point))return std::string("reader");
    if(selected==core::Module::archive&&archive&&archiveService&&archiveService->state().selected())return std::string("archive");
    if(selected==core::Module::notes&&notes->dropPoint(point))return std::string("notes");
    return std::nullopt;
}
void Application::Impl::receiveDrop(double time){
    auto paths=std::exchange(hudDrop->paths,{});const auto target=std::exchange(hudDrop->target,{});const auto point=hudDrop->point;hudDrop->posted=false;
    if(paths.empty()||!ready||stopping||!notes)return;
    time=hostTime(time);
    if(target=="shelfNavigation"&&shelf){
        // receiveShelfNavigationDrop: import, then select File Shelf.
        isolate(shelfModuleOwner,"drop",[&]{(void)shelf->importFiles(paths,hostTime(time));});
        const auto effects=lifecycle.selectModule(core::Module::fileShelf);if(effects.empty())selectModule(core::Module::fileShelf,time);else perform(effects,time);
    }else if(target=="area"){
        if(auto*owner=registry.presenter(notes->selected()))(void)registry.guard(*owner,[&]{(void)owner->drop(paths,point,hostTime(time));},"drop");
        automationChanged();
    }else if(target=="reader"&&reader&&notes->selected()==core::Module::reader){
        isolate(readerModuleOwner,"drop",[&]{if(auto generation=reader->beginDrop(hostTime(time)))importReaderReference(*generation,paths.front(),{},time);});
    }else if(target=="archive"&&archive&&archiveMedia&&notes->selected()==core::Module::archive){
        isolate(archiveModuleOwner,"drop",[&]{if(auto action=archive->dropMediaAction(hostTime(time))){
            std::vector<tools::ArchiveMediaImportFile>files;files.reserve(paths.size());
            for(const auto&path:paths)files.push_back({path,utf8(utf8Path(path).filename()),{}});
            (void)archiveMedia->beginImport(*action,files,hostTime(time));}});
    }else if(target=="notes"&&notes->selected()==core::Module::notes){
        (void)notes->dropMedia(paths,point,hostTime(time));
    }
    refresh(time);
}

// ---------------------------------------------------------------------------
// UI Automation root (app/hud_automation.hpp)
void Application::Impl::automationChanged(){automationDirty=true;}
// Content event: re-list the owners' fragments (only once a UIA client asked).
void Application::Impl::refreshAutomation(){if(automation&&automationDirty){automationDirty=false;automation->refresh();}}
HudAutomationSource Application::Impl::automationSource(){
    HudAutomationSource source;
    source.owners=[this](std::vector<ModuleOwner*>&out){registry.forEach([&](ModuleOwner&o){out.push_back(&o);},"automation");};
    source.refresh=[this](ModuleOwner&o,IRawElementProviderFragmentRoot*root){bool changed{};(void)registry.guard(o,[&]{changed=o.automationRefresh(root);},"automation");return changed;};
    // Groups are named after the module the owner presents (localized caption).
    source.name=[this](const ModuleOwner&o){
        // An owner that presents no module may name its group (powerCharge: Power).
        if(const auto m=o.automationModule()){const auto id=core::moduleIdentifier(*m);return moduleCaption(id,watchAppearance.language,id);}
        for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);
            if(o.presents(module)){const auto id=core::moduleIdentifier(module);return moduleCaption(id,watchAppearance.language,id);}}
        return std::string("EndfieldHUD");};
    return source;
}
std::optional<std::intptr_t> Application::Impl::automationObject(const NativeMessage&m){
    if(!HudAutomation::rootRequest(m.lParam)||!ready||stopping||!visibleMode())return std::nullopt;
    if(!automation){automation=std::make_unique<HudAutomation>(host.hwnd(),automationSource());automationDirty=true;}
    refreshAutomation();
    return automation->answer(m.wParam,m.lParam);
}

// ---------------------------------------------------------------------------
// DataRootRestart (app/areas/mac_import_handoff.hpp) and GPU device loss
bool Application::Impl::requestRestart(std::function<void()> work,std::function<void()> declined){
    // Production only; hidden coverage opens it for its own restart checks.
    if(!(production()||restartVerification)||!ready||stopping||quitRequested||sessionEnded||restartPending||restartAccepted||deviceLost||!work)return false;
    restartWork=std::move(work);restartDeclined=std::move(declined);restartPending=true;
    // The red power button's confirmed quit, outside the area's own call.
    if(!PostMessageW(static_cast<HWND>(host.hwnd()),restartMessage,serviceGeneration,0)){restartPending=false;restartWork={};restartDeclined={};return false;}
    return true;
}
void Application::Impl::declineRestart(){
    if(!restartPending)return;
    restartPending=false;restartWork={}; // destroyed unrun
    if(restartDeclined&&!restartDeclinePosted&&ready&&host.hwnd())restartDeclinePosted=PostMessageW(static_cast<HWND>(host.hwnd()),restartDeclinedMessage,serviceGeneration,0)!=FALSE;
}
void Application::Impl::runRestartDeclined(){
    restartDeclinePosted=false;
    if(auto declined=std::exchange(restartDeclined,{}))try{declined();}catch(const std::exception&e){
        const auto line=std::string("EndfieldHUD restart decline failed (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());}
}
bool Application::Impl::recoverDevice(const std::exception&e,double time){
    // DXGI device removal/reset (driver update or reset, adapter change,
    // TDR): a fresh run creates a new device and re-uploads everything
    // (main.cpp). Development previews and hidden coverage stay fatal.
    const auto*error=dynamic_cast<const gpu::RendererError*>(&e);
    if(!error||!production())return false;
    const auto code=static_cast<HRESULT>(error->code());
    if(code!=DXGI_ERROR_DEVICE_REMOVED&&code!=DXGI_ERROR_DEVICE_RESET&&code!=DXGI_ERROR_DEVICE_HUNG&&code!=DXGI_ERROR_DRIVER_INTERNAL_ERROR)return false;
    return restartDisplay("graphics device lost (HRESULT "+std::to_string(error->code())+")",time);
}
// A display change that moved the system's default adapter away from the
// device (IDXGIFactory1::IsCurrent, e.g. a monitor attached to another GPU):
// the same fresh run as a device loss, before the old device is removed.
void Application::Impl::checkAdapter(double time){
    if(!production()||!ready||stopping||deviceLost||renderer.adapterCurrent())return;
    (void)restartDisplay("graphics adapter changed",time);
}
bool Application::Impl::restartDisplay(const std::string&reason,double time){
    if(deviceLost||stopping)return true;
    deviceLost=true;
    if(notes&&session->phase()!=core::VisibilityPhase::concealed&&!closing)deviceLostModule=notes->selected();
    declineRestart();runRestartDeclined();
    const auto line="EndfieldHUD "+reason+"; restarting the display\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
    // The session-end drain: no animation, editors committed, every save queue flushed.
    (void)lifecycle.forceClose();forceConceal(time);broadcast(HostEvent::Kind::quitting,time);
    if(archive)isolate(archiveModuleOwner,"device loss",[&]{(void)archive->finishEditing(hostTime(time));});
    if(calendar)isolate(calendarModuleOwner,"device loss",[&]{(void)calendar->dismissMenu(false,hostTime(time));});
    (void)flushDocuments(time,false);
    if(summonHook)summonHook->stop();
    if(tray)tray->setHotkey({});
    stopping=true;host.setDeadline({});host.setFrameDemand({});host.requestStop();
    return true;
}
// Settings links and the release page open after the HUD closed
// (overlay.afterSystemClose); hidden coverage never opens a browser.
void Application::Impl::openAfterClose(std::string url,double time){
    if(url.rfind("https://",0)!=0||url.size()>2048||url.find_first_of(" \"<>\\^`{|}")!=std::string::npos)return;
    if(!visibleMode()){++linkRequests;return;}
    pendingLink=std::move(url);
    if(session->phase()==core::VisibilityPhase::concealed&&!closing){openPendingLink();return;}
    perform(lifecycle.close(),time);
}
void Application::Impl::openPendingLink(){
    auto link=std::exchange(pendingLink,std::nullopt);if(!link||!visibleMode()||quitRequested)return;
    const auto target=wide(*link);
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32&&!production())std::cerr<<"Link could not open: "<<result<<'\n';
}

// ---------------------------------------------------------------------------
// Projection (unchanged handoff)
std::optional<gpu::DisplayDescriptor> Application::Impl::projectionDisplay(std::uintptr_t preferred){
    const auto displays=gpu::readConnectedDisplays();for(const auto&display:displays)if(display.handle==preferred)return display;
    POINT pointer{};GetCursorPos(&pointer);if(const auto index=gpu::resolveDisplay({},displays,{pointer.x,pointer.y}))return displays[*index];return {};
}
void Application::Impl::moveProjection(const gpu::DisplayDescriptor&display){
    hudDisplay=display.handle;projectionWorkTopPixels=std::max(0,display.workArea.top-display.bounds.top);
    // Geometry only: the HUD hidden for the hand-off stays out of the topmost
    // band until Projection's show() enters it; a shown Projection keeps its place.
    const auto&bounds=display.bounds;need(host.place(bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top),"Position Projection on its retained display");
    (void)projectionCall("resize",[&]{projection->resize(host.metrics(),projectionWorkTopPixels/host.metrics().scale);});
}
void Application::Impl::projectionDropRoute(bool enabled,double time){
    projectionDrop->enabled=false;projectionDrop->paths.clear();if(!shelf)return;
    hudDropActive=false; // the HUD route is installed again by the next refresh
    if(!enabled){isolate(shelfModuleOwner,"drop route",[&]{shelf->setExternalDropCallbacks({},hostTime(time));});return;}
    projectionDrop->hwnd=static_cast<HWND>(host.hwnd());projectionDrop->scale=metrics.scale;
    const std::weak_ptr<ProjectionDropInbox>weak=projectionDrop;
    isolate(shelfModuleOwner,"drop route",[&]{shelf->setExternalDropCallbacks(gpu::ShelfDropTarget::Callbacks{
        [weak](POINTL point){auto state=weak.lock();if(!state||!state->enabled||!state->paths.empty())return false;POINT p{point.x,point.y};RECT rect{};return ScreenToClient(state->hwnd,&p)&&GetClientRect(state->hwnd,&rect)&&PtInRect(&rect,p);},
        [weak](std::span<const std::string>paths){auto state=weak.lock();if(!state||!state->enabled||!state->paths.empty()||paths.empty()||paths.size()>modules::ProjectionModel::maximumMedia)return false;POINT point{};if(!GetCursorPos(&point)||!ScreenToClient(state->hwnd,&point))return false;state->point={point.x/state->scale,point.y/state->scale};state->paths.assign(paths.begin(),paths.end());if(!PostMessageW(state->hwnd,projectionDropMessage,serviceGeneration,0)){state->paths.clear();return false;}return true;}
    },hostTime(time));});
}
// A production Projection failure (Impl::projectionCall, outside any
// workspace call): the registry disabled its slot and logged the failure
// type. Projection closes without animation, the HUD stays closed and its
// navigation entry does nothing for the rest of this run.
void Application::Impl::projectionFailed(){
    projectionBroken=true;const auto time=now();
    projectionActions.clear();projectionTrayAction.reset();projectionDisplayPending=false;projectionResizePending.reset();projectionPainting=false;
    cancelProjectionPicker();(void)projectionHandoff.cancel();
    try{projectionDropRoute(false,time);}catch(...){}
    if(projection){
        bool closed{};try{projection->close(time);closed=true;}catch(...){}
        presentationPriority.restore();
        if(areaHost)areaHost->withdraw(endfield::notes::projectionWorkspaceService);
        if(closed)projection.reset();
        else{
            // A workspace that cannot close is left unreleased instead of
            // ending the HUD from its destructor; the HUD takes its GPU state back.
            (void)projection.release();
            try{backdrop.detachForeground();}catch(...){}
            try{renderer.setSourcePassEnabled(true);}catch(...){}
        }
    }
    projectionBroken=false;
    try{host.hide();host.setFrameDemand({});}catch(...){}
    published.clear();publishedNotesRevision=0;automationChanged();
    setProjectionActive(false,time);
    if(!stopping)scheduleDeadline();
}
// The projection area looks the workspace up by service: withdraw it first.
void Application::Impl::releaseProjection()noexcept{if(areaHost)areaHost->withdraw(endfield::notes::projectionWorkspaceService);projection.reset();}
void Application::Impl::cancelProjectionPicker(){if(pickerOwner==PickerOwner::projection){mediaPicker->cancel();pickerOwner=PickerOwner::none;}pendingProjectionPicker.reset();}
tools::ProjectionPreviewOptions Application::Impl::projectionOptions(){
    tools::ProjectionPreviewOptions value;value.language=watchAppearance.language;value.reduceMotion=settings.reduceMotion;
    const auto accent=watchAppearance.accentSRGB;value.accent={accent[0],accent[1],accent[2],1};
    value.raster.assetRoot=args.notesFormatAssets;value.raster.pixelsPerPoint=2;value.raster.paddingPoints=1;
    value.menuStrings=tools::archivePreviewStrings(watchAppearance.language).menus;value.shelfChoices=[this]{return shelfChoices();};
    // OverlayController.recordProjectionEvent: Event Log projectionAction.
    value.event=[this](const tools::ProjectionPreviewEvent&e){if(eventOwner&&!stopping)eventOwner->record({ehud::data::makeUUID(),modules::EventKind::projectionAction,ehud::data::foundationNow(),{{"action",std::string(tools::projectionEventAction(e))}}});};
    constexpr std::string_view wheel="244c34ad474b15c242f9bd62cbf195272cbf7b880acbeaf0021e237d6a22abd2";
    value.colorWheel=gpu::NativeNotesControlsImage{{"notes.menu/wheel","source-generated:NotesColorWheelView.wheel",{8,8,166,166},{1,1,1,1},192,false,false},Json::Object{{"asset","raster/"+std::string(wheel)+".png"},{"sha256",std::string(wheel)}}};
    return value;
}
void Application::Impl::projectionCommand(const ProjectionHandoff::Command&command,double time){
    using A=ProjectionHandoff::Action;
    if(command.generation!=projectionHandoff.generation())return;
    time=hostTime(time);
    switch(command.action){
    case A::closeHUD:perform(lifecycle.close(true),time);if(!closing){projectionHandoff.cancel();setProjectionActive(false,time);}break;
    case A::showProjection:{
        host.hide();composition.detach(renderer);published.clear();publishedNotesRevision=0;
        if(!projectionAvailable()){(void)projectionHandoff.cancel();setProjectionActive(false,time);break;}
        if(!projection){const auto a=watchAppearance.accentSRGB;
            try{projection=std::make_unique<tools::ProjectionWorkspace>(host,renderer,rasterizer,backdrop,*mediaBroker,modules::NotesColor{a[0],a[1],a[2],1},configuration.number("backgroundDarkness"),configuration.number("blurAmount"));}
            catch(const std::exception&e){if(!production()||!projectionIsolation)throw;
                // A workspace that cannot be created fails like any isolated Projection call.
                (void)registry.guard(*projectionIsolation,[&]{throw;},"show");(void)projectionHandoff.cancel();setProjectionActive(false,time);(void)e;break;}
            // The projection area lists the workspace's controls under the UIA root (app/areas/projection_automation.hpp).
            if(areaHost)areaHost->publish(endfield::notes::projectionWorkspaceService,*projection);}
        tools::ProjectionMediaBindingOptions binding;
        binding.resolve=[](const modules::ProjectionMediaReference&ref){return gpu::NotesImageAccess{utf8Path(ref.path()),ref.accessLease()};};
        binding.errorText=[this](HRESULT hr){return tools::notesMediaErrorDescription(hr,watchAppearance.language);};
        // Nested messages while the workspace shows (activation, focus) are
        // handed later times; show() itself keeps its sampled one.
        projectionPainting=true;bool shown{};
        try{shown=projectionCall("show",[&]{projection->resize(metrics,projectionWorkTopPixels/metrics.scale);projection->show(projectionOptions(),std::move(binding),hostTime(time));});
            if(shown)projectionDropRoute(true,time);projectionPainting=false;}
        catch(...){projectionPainting=false;projectionHandoff.cancel();throw;}
        // Projection draws over the game the HUD was summoned over: its frames
        // schedule ahead of the game's as the shown HUD's do (open()).
        if(shown&&visibleMode())presentationPriority.raise(&renderer);
        automationChanged();refresh(now());break;
    }
    case A::closeProjection:projectionDropRoute(false,time);cancelProjectionPicker();(void)projectionCall("dismiss",[&]{projection->dismiss(hostTime(time),settings.reduceMotion);});automationChanged();refresh(time);break;
    case A::showHUD:projectionDropRoute(false,time);(void)projectionCall("close",[&]{projection->close(hostTime(time));});presentationPriority.restore();automationChanged();published.clear();publishedNotesRevision=0;setProjectionActive(false,time);perform(lifecycle.openOverlay(),time);break;
    case A::external:{projectionDropRoute(false,time);(void)projectionCall("close",[&]{projection->close(hostTime(time));});presentationPriority.restore();automationChanged();published.clear();publishedNotesRevision=0;setProjectionActive(false,time);
        const auto action=command.externalID?static_cast<gpu::TrayAction>(*command.externalID):gpu::TrayAction::openOverlay;
        if(action==gpu::TrayAction::quit){perform(lifecycle.requestTermination(),time);break;}
        trayAction(action==gpu::TrayAction::none?gpu::TrayAction::openOverlay:action,false,time);
        break;
    }
    case A::closeImmediately:projectionDropRoute(false,time);cancelProjectionPicker();(void)projectionCall("close",[&]{projection->close(hostTime(time));});presentationPriority.restore();automationChanged();published.clear();publishedNotesRevision=0;setProjectionActive(false,time);
        // The HUD stays closed: after the grace nothing keeps the graphics.
        if(session->phase()==core::VisibilityPhase::concealed&&!projectionShown()){idleGraphicsAttempted=idleGraphicsRefused=false;graphicsIdleAt=now()+closedGraphicsGrace();scheduleDeadline();}break;
    }
}
} // namespace endfield::app
