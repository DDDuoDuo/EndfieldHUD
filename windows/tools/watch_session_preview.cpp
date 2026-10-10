// Build-only source-shell feasibility tool. Every input is an explicit synthetic
// export/cache. Visible launch is opt-in; benchmark never shows its owned HWND.
#include "app/overlay_host.hpp"
#include "app/event_log_save_queue.hpp"
#include "app/settings_save_queue.hpp"
#include "app/archive_service.hpp"
#include "modules/hud_clock.hpp"
#include "native/event_log_owner.hpp"
#include "tools/notes_preview.hpp"
#include "tools/archive_options.hpp"
#include "tools/archive_media_binding.hpp"
#include "native/archive_text_rules.hpp"
#include "tools/settings_preview.hpp"
#include "native/watch_appearance.hpp"
#include "core/source_color.hpp"
#include "tools/shelf_preview.hpp"
#include "tools/clipboard_preview.hpp"
#include "tools/volume_preview.hpp"
#include "tools/battery_preview.hpp"
#include "tools/storage_options.hpp"
#include "tools/activity_preview.hpp"
#include "tools/reader_preview.hpp"
#include "tools/calendar_preview.hpp"
#include "native/calendar_notifications.hpp"
#include "tools/map_preview.hpp"
#include "tools/orbipom_preview.hpp"
#include "modules/orbipom_runtime.hpp"
#include "native/map_assets.hpp"
#include "modules/map_geography.hpp"
#include "core/data/map_store.hpp"
#include "tools/projection_workspace.hpp"
#include "app/projection_handoff.hpp"
#include "native/reader_owner.hpp"
#include "native/activity_assets.hpp"
#include "native/activity_provider.hpp"
#include "native/activity_catalog.hpp"
#include "native/activity_disk.hpp"
#include "tools/event_log_preview.hpp"
#include "tools/work_mode_preview.hpp"
#include "native/clipboard_assets.hpp"
#include "native/clipboard_provider.hpp"
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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <memory>
#include <set>
#include <functional>
#include <deque>
#include <utility>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
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
std::string narrow(std::wstring_view value){if(value.empty())return {};const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);need(count>0,"Invalid Windows text");std::string out(count,'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr)==count,"Windows text conversion failed");return out;}
core::Language settingsLanguage(const ehud::data::Settings&value){const auto language=core::languageFromSetting(value.string("language")).value_or(core::Language::system);if(language!=core::Language::system)return language;wchar_t name[LOCALE_NAME_MAX_LENGTH]{};if(!GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH))return core::Language::english;const auto text=narrow(name);const std::array<std::string_view,1>preferred{text};return core::resolveLanguage(preferred);}
std::string moduleCaption(std::string_view target,core::Language language,std::string_view fallback){
    if(target=="notes")return core::localized("Notes","便笺",language);
    if(target=="fileShelf")return core::localized("Temporary File Shelf","文件暂存架",language);
    if(target=="clipboard")return core::localized("Clipboard Cache","剪贴板",language);
    if(target=="volume")return core::localized("Volume","音量",language);
    if(target=="account")return core::localized("Account Linking","账户绑定",language);
    if(target=="nowPlaying")return core::localized("Now Playing","当前播放",language);
    if(target=="projection")return core::localized("Projection","投影",language);
    if(target=="reader")return core::localized("E-Reader","阅读器",language);
    if(target=="archive")return core::localized("Archive","档案库",language);
    if(target=="mediaAssembly")return core::localized("Media Assembly","影像加工",language);
    if(target=="calendar")return core::localized("Calendar","日历",language);
    if(target=="minigame")return core::localized("Closure's Minigame","可露希尔的小游戏",language);
    if(target=="workMode")return core::localized("Work Mode","工作模式",language);
    if(target=="eventLog")return core::localized("Event Log","事件日志",language);
    if(target=="map")return core::localized("Map","地图",language);
    if(target=="addApp")return core::localized("+ Add App","+ 添加应用",language);
    if(target=="system")return core::localized("System","系统",language);
    if(target=="display")return core::localized("Display","显示",language);
    if(target=="hotkeys")return core::localized("Hotkeys","快捷键",language);
    if(target=="about")return core::localized("About","关于",language);
    if(target=="storage")return core::localized("Storage","存储",language);
    if(target=="activityMonitor")return core::localized("Activity Monitor","活动监视器",language);
    if(target=="power")return core::localized("Power","电源",language);
    if(target=="profile")return core::localized("Personal Profile","个人名片",language);
    return std::string(fallback);
}
endfield::tools::NotesPreviewPreferences notesPreferences(core::Language language,bool dark,bool reduceMotion,std::array<double,4>accent){
    endfield::tools::NotesPreviewPreferences out;out.dark=dark;out.reduceMotion=reduceMotion;
    auto&w=out.workspace;w.palette=endfield::modules::NotesPalette::source(dark,accent);w.editor={{dark?.12:.96,dark?.12:.96,dark?.12:.96,1},accent};w.compositionColor=w.palette.primary;w.eraserColor=dark?std::array<double,4>{1,69./255.,58./255.,1}:std::array<double,4>{1,59./255.,48./255.,1};
    const auto t=[language](std::string_view en,std::string_view zh){return core::localized(en,zh,language);};auto&s=w.strings;
    s.textTitle=t("TEXT","文字");s.placeholder=t("Double-click to write…","双击输入…");s.pin=t("Pin note","固定便笺");s.unpin=t("Unpin note","取消固定");s.remove=t("Delete note","删除便笺");s.edit=t("Edit text","编辑文字");s.select=t("Text","文字");s.grow=t("Enlarge note","放大便笺");s.shrink=t("Reduce note size","缩小便笺");s.format={t("Font size","字号"),t("Font","字体"),t("Color","颜色"),t("Text style","特殊")};
    s.todoTitle=t("TODO","待办");s.itemPlaceholder=t("New item…","新事项…");s.addItem=t("+ Add item","+ 添加事项");s.checkItem=t("Check","勾选");s.uncheckItem=t("Uncheck","取消勾选");s.editItem=t("Edit item","编辑事项");s.moveUp=t("Move up","上移");s.moveDown=t("Move down","下移");s.removeItem=t("Delete item","删除事项");s.addItemAction=t("Add item","添加事项");s.imageTitle=t("IMAGE/VIDEO","图片/视频");s.drawingTitle=t("DRAWING","画画");s.drawingColor=t("Drawing color","画笔颜色");out.controls=endfield::tools::archivePreviewStrings(language).menus;return out;
}
endfield::modules::ReaderStrings readerStrings(core::Language language){
    const auto t=[language](std::string_view en,std::string_view zh){return core::localized(en,zh,language);};
    endfield::modules::ReaderStrings out;
    out.open=t("Open","打开");out.library=t("Library","书库");out.reading=t("Reading","阅读设置");out.bookmarks=t("Bookmarks","书签");
    out.title=t("E-Reader","阅读器");out.empty=t("Open a book to begin reading.","打开书籍开始阅读。");out.loading=t("Loading book…","正在载入书籍…");
    out.chooseFile=t("Choose file","选择文件");out.chooseShelf=t("Choose from Shelf","从暂存架选择");out.shelfTitle=out.chooseShelf;
    out.font=t("Font","字体");out.fontSize=t("Font size","字号");out.lineSpacing=t("Line spacing","行间距");out.margin=t("Margins","页边距");
    out.horizontal=t("Left to right","从左到右");out.vertical=t("Top to bottom","从上到下");return out;
}
endfield::modules::StorageAppearance storageAppearance(core::Language language,bool dark,std::array<double,4>accent){
    const auto available=source::sourceWhiteBlend({accent[0],accent[1],accent[2]},.42);
    return {dark,2,language,accent,std::array<double,4>{available[0],available[1],available[2],accent[3]}};
}
double now(){return app::OverlayHost::clockNow();}
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
struct COM {
    COM(){need(SUCCEEDED(OleInitialize(nullptr)),"OLE initialization failed");}
    ~COM(){
        // The outer app owner is last: all workers, documents, media and
        // composition objects have already retired. Drop cached WinRT factories
        // while COM is still usable instead of deferring them to DLL teardown.
        winrt::clear_factory_cache();CoFreeUnusedLibrariesEx(INFINITE,0);OleUninitialize();
    }
};
// Development preview only: record graphics-fault module offsets, never document
// memory or typed text. Keep normal Windows exception handling/termination.
class PreviewFaultTrace final {
    static void write(const char* bytes,DWORD size)noexcept{DWORD written{};WriteFile(GetStdHandle(STD_ERROR_HANDLE),bytes,size,&written,nullptr);}
    static LONG CALLBACK report(EXCEPTION_POINTERS* event) {
        if(!event||!event->ExceptionRecord||event->ExceptionRecord->ExceptionCode!=0x87a)return EXCEPTION_CONTINUE_SEARCH;
        constexpr char heading[]="Preview graphics fault 0x87a; native module offsets follow\n";write(heading,sizeof(heading)-1);
        void* frames[32]{};const auto count=CaptureStackBackTrace(0,32,frames,nullptr);
        for(USHORT n=0;n<count;++n){HMODULE module{};wchar_t path[MAX_PATH]{};
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frames[n]),&module)&&GetModuleFileNameW(module,path,MAX_PATH)){
                path[MAX_PATH-1]=0;
                const wchar_t*base=path;for(auto*p=path;*p;++p)if(*p==L'\\')base=p+1;
                char line[MAX_PATH*3+24]{};const auto length=WideCharToMultiByte(CP_UTF8,0,base,-1,line,MAX_PATH*3,nullptr,nullptr);if(length<=0)continue;
                DWORD size=static_cast<DWORD>(length-1);line[size++]='+';line[size++]='0';line[size++]='x';
                const auto offset=reinterpret_cast<std::uintptr_t>(frames[n])-reinterpret_cast<std::uintptr_t>(module);bool leading=true;
                for(int digit=int(sizeof(offset)*2)-1;digit>=0;--digit){const auto value=(offset>>(digit*4))&15;if(!value&&leading&&digit)continue;leading=false;line[size++]="0123456789abcdef"[value];}
                line[size++]='\n';write(line,size);
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
public:explicit PreviewFaultTrace(bool enabled){
        // Keep this one development-only callback through DLL/process teardown:
        // the observed graphics failure can happen after main has returned.
        // Windows retires the registration with the process. It never changes
        // exception handling and owns no document, service, clock or worker.
        if(enabled)AddVectoredExceptionHandler(1,report);
    }
};
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
struct PreviewLifetime final {
    std::function<void()>cleanup;
    ~PreviewLifetime(){try{cleanup();}catch(...) {}}
};
// Opt-in isolated preview trace: bounded numeric samples only. No text, file
// names, IDs, image readback, extra clock or per-frame allocation/file writes.
class ReaderScrollTrace final {
    struct Row {double time{},steps{},cpuMs{};std::uint32_t lines{};bool wheel{};endfield::tools::ReaderScrollSnapshot value;};
    static constexpr std::size_t limit=6144;std::vector<Row>rows_;std::size_t cursor_{};double activeUntil_{};
public:
    ReaderScrollTrace(){rows_.reserve(limit);}
    void record(bool wheel,double time,const endfield::tools::ReaderScrollSnapshot&value,double steps=0,std::uint32_t lines=0,double cpuMs=0){
        if(wheel)activeUntil_=time+2;if(!wheel&&time>activeUntil_)return;
        const Row row{time,steps,cpuMs,lines,wheel,value};if(rows_.size()<limit)rows_.push_back(row);else{rows_[cursor_]=row;cursor_=(cursor_+1)%limit;}
    }
    void save(const fs::path&path)const{
        Json::Array samples;samples.reserve(rows_.size());for(std::size_t n=0;n<rows_.size();++n){const auto&r=rows_[(cursor_+n)%rows_.size()];const auto&v=r.value;Json::Array rects,ready;for(unsigned j=0;j<3;++j){const auto&p=v.scene.displayed[j];rects.emplace_back(Json::Array{p.x,p.y,p.width,p.height});ready.emplace_back(v.scene.ready[j]);}
            samples.emplace_back(Json::Object{{"event",r.wheel?"wheel":"frame"},{"time",r.time},{"steps",r.steps},{"lines",std::int64_t(r.lines)},{"frameCpuMs",r.cpuMs},{"stateTime",v.time},{"offset",v.targetOffset},{"zoom",v.zoom},{"panY",v.panY},{"deferred",v.deferred},{"pending",std::int64_t(v.pendingDirection)},{"revision",std::int64_t(v.stateRevision)},{"busy",v.providerBusy},{"epoch",std::int64_t(v.scene.epoch)},{"origin",v.scene.pageOrigin},{"animationStart",v.scene.animationStart},{"animationDuration",v.scene.animationDuration},{"rects",std::move(rects)},{"ready",std::move(ready)}});
        }
        const Json data=Json::Object{{"schema",1},{"numericOnly",true},{"samples",std::move(samples)}};ehud::data::detail::replaceFile(path,std::nullopt,data.encode(),8*1024*1024);
    }
};
// Preview-only notification injection. The Calendar adapter still executes on
// the app's single FIFO, but this fixture never registers an app identity or
// contacts Windows' real notification center, even in explicit visible mode.
struct CalendarFixtureNotifications {
    endfield::modules::CalendarPermission permission{endfield::modules::CalendarPermission::unavailable};
    std::vector<gpu::CalendarScheduledNotification>pending;
};
class CalendarFixtureProvider final:public gpu::CalendarNotificationProvider {
    std::shared_ptr<CalendarFixtureNotifications>state_;
public:
    explicit CalendarFixtureProvider(std::shared_ptr<CalendarFixtureNotifications>state):state_(std::move(state)){}
    endfield::modules::CalendarPermission authorization(bool)override{return state_->permission;}
    std::vector<gpu::CalendarScheduledNotification>pending()override{return state_->pending;}
    void remove(std::string_view id)override{std::erase_if(state_->pending,[id](const auto&v){return v.identifier==id;});}
    void add(const gpu::CalendarScheduledNotification&v)override{remove(v.identifier);state_->pending.push_back(v);}
};
struct Options {fs::path packet,cache,shader,chrome,cursor,report,snapshots,watchBlur,notesAssets,notesData,notesFormatAssets,shelfAssets,shelfData,shelfMask,clipboardAssets,liveDiagnostics,residentIcons,settingsAssets,archiveAssets,storageAssets,activityAssets,mapGeography,mapPlayerAssets,orbipomAssets;std::string pin,notesAssetsSHA;bool visible{},warp{},runtimeInput{},coverage{},moduleCoverage{},nativeClipboard{},nativeActivity{},readerScrollTrace{},readerModule{},calendarModule{},projectionModule{},volumeFixture{},eventLogFixture{},workModeFixture{},batteryFixture{};std::uint32_t benchmarkWidth{1280},benchmarkHeight{800};double benchmarkEpoch{};};
Options options(int argc,wchar_t**argv){
    need(argc>=5,"Usage: watch_session_preview packet-root compiled-scene hud.hlsl --benchmark new-report.json | --visible --watch-blur original-watch-blur.json [--chrome chrome.json] [--cursor-png original.png] [--compiled-sha sha256] [--snapshots new-directory] [--warp] [--runtime-input] [--benchmark-size width height] [--benchmark-epoch seconds] [--coverage] [--storage-assets common-resources (development preview)]");
    Options o;o.packet=fs::absolute(argv[1]);o.cache=fs::absolute(argv[2]);o.shader=fs::absolute(argv[3]);
    for(int i=4;i<argc;++i){const std::wstring_view arg=argv[i];
        if(arg==L"--archive-assets"&&i+1<argc){need(o.archiveAssets.empty(),"Duplicate Archive assets");o.archiveAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--activity-assets"&&i+1<argc){need(o.activityAssets.empty(),"Duplicate Activity assets");o.activityAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--storage-assets"&&i+1<argc){need(o.storageAssets.empty(),"Duplicate Storage assets");o.storageAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--settings-assets"&&i+1<argc){need(o.settingsAssets.empty(),"Duplicate Settings assets");o.settingsAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--visible"){need(!o.visible,"Duplicate visible mode");o.visible=true;}
        else if(arg==L"--live-diagnostics"&&i+1<argc){need(o.liveDiagnostics.empty(),"Duplicate live diagnostic output");o.liveDiagnostics=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-assets"&&i+1<argc){need(o.shelfAssets.empty(),"Duplicate shelf assets");o.shelfAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-data"&&i+1<argc){need(o.shelfData.empty(),"Duplicate shelf data");o.shelfData=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-mask"&&i+1<argc){need(o.shelfMask.empty(),"Duplicate shelf reveal");o.shelfMask=fs::absolute(argv[++i]);}
        else if(arg==L"--event-log-fixture"){need(!o.eventLogFixture,"Duplicate Event Log fixture");o.eventLogFixture=true;}
        else if(arg==L"--resident-preview-icons"&&i+1<argc){need(o.residentIcons.empty(),"Duplicate resident preview icon directory");o.residentIcons=fs::absolute(argv[++i]);}
        else if(arg==L"--projection"){need(!o.projectionModule,"Duplicate Projection mode");o.projectionModule=true;}
        else if(arg==L"--reader-scroll-trace"){need(!o.readerScrollTrace,"Duplicate Reader trace");o.readerScrollTrace=true;}
        else if(arg==L"--orbipom-assets"&&i+1<argc){need(o.orbipomAssets.empty(),"Duplicate Minigame artwork");o.orbipomAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--map-geography"&&i+1<argc){need(o.mapGeography.empty(),"Duplicate Map geography");o.mapGeography=fs::absolute(argv[++i]);}
        else if(arg==L"--map-player-assets"&&i+1<argc){need(o.mapPlayerAssets.empty(),"Duplicate Map player assets");o.mapPlayerAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--reader"){need(!o.readerModule,"Duplicate Reader mode");o.readerModule=true;}
        else if(arg==L"--calendar"){need(!o.calendarModule,"Duplicate Calendar mode");o.calendarModule=true;}
        else if(arg==L"--native-activity"){need(!o.nativeActivity,"Duplicate native Activity mode");o.nativeActivity=true;}
        else if(arg==L"--native-clipboard"){need(!o.nativeClipboard,"Duplicate native Clipboard mode");o.nativeClipboard=true;}
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
    need(!o.nativeClipboard||(o.visible&&!o.moduleCoverage&&!o.coverage&&!o.clipboardAssets.empty()&&!o.notesAssets.empty()),"Native Clipboard requires explicit visible mode and module assets; hidden tests never access the clipboard");
    need(!o.nativeActivity||(o.visible&&!o.moduleCoverage&&!o.coverage&&!o.activityAssets.empty()&&!o.notesAssets.empty()),"Native Activity requires explicit visible mode and module assets; hidden tests never enumerate applications");
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
    need(o.settingsAssets.empty()||!o.notesAssets.empty(),"Settings needs the shared module owner");
    need(o.activityAssets.empty()||!o.notesAssets.empty(),"Activity needs the shared module owner");
    need(!o.projectionModule||(o.visible&&!o.notesAssets.empty()&&!o.notesFormatAssets.empty()),"Projection needs the visible shared HUD and source controls");
    need(o.orbipomAssets.empty()||(!o.notesAssets.empty()&&!o.settingsAssets.empty()),"Minigame needs the shared module owner and an isolated Settings store");
    need(o.mapGeography.empty()==o.mapPlayerAssets.empty(),"Map needs geography and player assets together");
    need(o.mapGeography.empty()||!o.notesAssets.empty(),"Map needs the shared module owner and a new temporary data root");
    need(!o.readerModule||!o.notesAssets.empty(),"Reader needs the shared module owner and a new temporary data root");
    need(!o.calendarModule||!o.notesAssets.empty(),"Calendar needs the shared module owner and a new temporary data root");
    need(!o.readerScrollTrace||(o.visible&&o.readerModule&&!o.notesData.empty()),"Reader trace requires explicit visible isolated Reader data");
    need(o.storageAssets.empty()||!o.notesAssets.empty(),"Development Storage preview requires shared module ownership");
    need(o.archiveAssets.empty()||!o.notesAssets.empty(),"Archive needs explicit Notes assets and a new shared test data root");
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
    const auto args=options(argc,argv);PreviewFaultTrace faults(args.visible||args.moduleCoverage);COM com;const auto preparation=Clock::now();StartupStages startup;
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
    std::unique_ptr<gpu::NativeWatchContent>nativeContent;std::unique_ptr<gpu::WatchAppearanceTemplates>appearanceTemplates;std::unique_ptr<gpu::NativeWatchAppearance>nativeAppearance;gpu::WatchAppearance watchAppearance;
    if(args.settingsAssets.empty())nativeContent=std::make_unique<gpu::NativeWatchContent>(contentCatalog,layers,rasterOptions);
    else{const auto file=args.settingsAssets/"watch-appearance"/"manifest.json";const auto bytes=ehud::data::detail::readFile(file,1024*1024);need(bytes&&packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()))=="1c623124d48d094e8897b89598bc6791a8d369bc51ef8bc87c928ef5915444de","Original desktop appearance pin differs");appearanceTemplates=std::make_unique<gpu::WatchAppearanceTemplates>(Json::parse(*bytes));auto ro=rasterOptions;ro.assetRoot=file.parent_path();nativeAppearance=std::make_unique<gpu::NativeWatchAppearance>(*appearanceTemplates,scene,labelPlan.bindings(),layers,rasterizer,ro);}startup.mark("native-bindings-content-catalog");
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
    app::OverlayHost host;gpu::Renderer renderer;gpu::DesktopBackdrop backdrop;std::unique_ptr<gpu::NativeNotesControlsAssets> notesAssets;std::unique_ptr<endfield::tools::NotesPreview> notes;std::unique_ptr<gpu::NativeShelfAssets>shelfAssets;std::unique_ptr<endfield::tools::ShelfPreview>shelf;std::optional<ehud::data::ShelfFileAccess>pendingShelfReveal;std::unique_ptr<gpu::NativeClipboardAssets>clipboardAssets;std::unique_ptr<endfield::tools::ClipboardPreview>clipboard;std::unique_ptr<endfield::tools::VolumePreview>volume;std::unique_ptr<endfield::tools::EventLogPreview>eventLog;std::unique_ptr<endfield::tools::WorkModePreview>workMode;std::unique_ptr<endfield::tools::BatteryPreview>battery;std::unique_ptr<endfield::tools::SettingsPreview>settingsUI;std::unique_ptr<endfield::tools::StoragePreview>storage;std::unique_ptr<endfield::tools::ActivityPreview>activity;gpu::LayerComposition composition;app::ClientMetrics metrics;bool ready=false,closing=false,pendingClose=false,focused=!args.visible;
    struct StorageFixture {std::atomic<double>time{};std::atomic<unsigned>capacityReads{},detailScans{};unsigned settingsRequests{};};
    std::shared_ptr<StorageFixture>storageFixture;
    std::unique_ptr<gpu::EventLogOwner>eventOwner;
    std::unique_ptr<app::UtilityExecutor>utility;
    endfield::modules::ActivitySamplingPlan activityPlan;
    std::shared_ptr<gpu::ActivityCatalogSampler>activityCatalog;
    std::shared_ptr<gpu::WindowsActivityDisk>activityDisk;
    std::unique_ptr<gpu::ActivityProbe>activityProbe;
    std::unique_ptr<gpu::SystemServices>systemServices;
    std::unique_ptr<gpu::NativeClipboardProvider>clipboardProvider;
    std::unique_ptr<endfield::modules::OrbiPomSession>gameSession;
    std::unique_ptr<endfield::tools::OrbiPomPreview>game;
    std::int64_t gameBest{};std::function<void(double)>ensureGame;
    std::unique_ptr<ehud::data::MapStore>mapStore;
    std::unique_ptr<endfield::tools::MapPreview>map;
    app::UtilityExecutor::Route mapLoadRoute{};bool mapLoadRequested{},mapRefreshQueued{};double mapTime{};
    std::function<void()>requestMapGeography;
    std::unique_ptr<gpu::CalendarCivilContext>calendarCivil;
    std::shared_ptr<endfield::modules::CalendarJSONRepository>calendarRepository;
    std::shared_ptr<CalendarFixtureNotifications>calendarFixture;
    std::unique_ptr<gpu::NativeCalendarNotifications>calendarNotifications;
    std::unique_ptr<endfield::modules::CalendarState>calendarState;
    std::unique_ptr<endfield::tools::CalendarPreview>calendar;
    app::UtilityExecutor::Route calendarRoute{};bool calendarRefreshQueued{};double calendarTime{};
    std::optional<double>calendarNextWake;bool calendarWakeActive{};core::Language calendarLanguage{core::Language::system};
    std::unique_ptr<gpu::ReaderOwner>readerOwner;
    std::unique_ptr<ReaderScrollTrace>readerTrace;if(args.readerScrollTrace)readerTrace=std::make_unique<ReaderScrollTrace>();
    std::unique_ptr<endfield::tools::ReaderPreview>reader;
    bool readerRefreshQueued{},readerActionQueued{};double readerTime{};
    std::optional<endfield::tools::ReaderImportAction>pendingReaderAction,pendingReaderPicker;
    bool clipboardDirty{},clipboardRefreshQueued{};
    std::unique_ptr<app::EventLogSaveQueue>eventSaves;
    std::unique_ptr<app::SettingsSaveQueue>settingsSaves;
    std::unique_ptr<gpu::NativeShelfFilePicker>mediaPicker;
    // Archive opt-in shares these app-lifetime providers with pinned Notes.
    // The legacy Notes-only owner keeps its existing private-provider path.
    std::unique_ptr<gpu::NativeNotesImageDecoder>mediaDecoder;
    std::unique_ptr<gpu::NativeNotesImagePlayback>mediaImages;
    std::unique_ptr<gpu::NativeNotesVideoPlayback>mediaVideos;
    std::unique_ptr<gpu::NativeMediaRequestBroker>mediaBroker;
    gpu::NativeMediaRequestBroker::Client notesMediaClient{},archiveMediaClient{};
    std::unique_ptr<endfield::tools::ProjectionWorkspace>projection;
    app::ProjectionHandoff projectionHandoff;
    std::uintptr_t hudDisplay{},previousApplication{};double projectionWorkTopPixels{};
    struct ProjectionDropInbox {HWND hwnd{};bool enabled{};double scale{1};core::Point point;std::vector<std::string>paths;};
    const auto projectionDrop=std::make_shared<ProjectionDropInbox>();
    bool projectionActionsQueued{},projectionPainting{},projectionDisplayPending{};
    std::optional<app::ClientMetrics>projectionResizePending;
    std::deque<endfield::tools::ProjectionPreviewAction>projectionActions;
    std::optional<gpu::TrayAction>projectionTrayAction;
    std::optional<endfield::tools::ProjectionPreviewAction>pendingProjectionPicker;
    std::function<void(const app::ProjectionHandoff::Command&,double)>projectionCommand;

    gpu::LayerImageSource archiveImages;
    std::shared_ptr<gpu::ArchiveDateFormatter>archiveDates;
    std::unique_ptr<app::ArchiveService>archiveService;
    std::unique_ptr<endfield::tools::ArchivePreview>archive;
    std::unique_ptr<endfield::tools::ArchiveMediaBinding>archiveMedia;
    gpu::ApplicationIcon applicationIcon;std::unique_ptr<gpu::TrayController>tray;bool quitRequested{};
    core::Point mediaInsertionPoint;bool eventRefreshQueued{},archiveRefreshQueued{},stopping{},cleaned{};
    enum class PickerOwner {none,notes,archive,reader,projection};PickerOwner pickerOwner{PickerOwner::none};
    std::optional<endfield::tools::ArchiveMediaAction>pendingArchivePicker;
    std::optional<core::Module>pendingModule;
    constexpr UINT eventChangedMessage=WM_APP+194,utilityMessage=WM_APP+195,mediaPickerMessage=WM_APP+196,
        sharedMediaMessage=WM_APP+223,archiveChangedMessage=WM_APP+224,
        systemServiceMessage=WM_APP+225,clipboardChangedMessage=WM_APP+226,
        readerChangedMessage=WM_APP+227,readerActionMessage=WM_APP+228,projectionActionMessage=WM_APP+229,projectionDropMessage=WM_APP+230,mapChangedMessage=WM_APP+231,calendarChangedMessage=WM_APP+232;
    constexpr UINT_PTR serviceGeneration=1;
    const auto cleanup=[&]{if(cleaned)return;cleaned=true;ready=false;stopping=true;tray.reset();
        if(mediaPicker)mediaPicker->cancel();mediaPicker.reset();
        projectionHandoff.cancel();projectionDrop->enabled=false;if(shelf)shelf->setExternalDropCallbacks({},now());projection.reset();
        try{composition.detach(renderer);}catch(...){renderer.reset();}
        const auto release=[&](auto&owner){if(owner){try{if(renderer.stats().initialized)owner->release(renderer);}catch(...){}owner.reset();}};
        // Archive fields borrow Notes' activated TSF manager. Its media groups
        // must detach before their textures and before either borrowed owner.
        if(archive){try{if(renderer.stats().initialized)archive->release(renderer);}catch(...) {}}
        if(archiveMedia){try{archiveMedia->releaseResources(now());}catch(...){}archiveMedia.reset();}
        archive.reset();release(calendar);calendarState.reset();calendarNotifications.reset();if(utility&&calendarRoute)utility->invalidate(calendarRoute,false);calendarRepository.reset();calendarCivil.reset();calendarFixture.reset();release(settingsUI);release(battery);release(workMode);release(notes);
        activityPlan.stop();activityProbe.reset();
        release(shelf);release(clipboard);release(volume);release(eventLog);release(storage);release(activity);release(reader);readerOwner.reset();release(game);gameSession.reset();release(map);mapStore.reset();if(utility&&mapLoadRoute)utility->invalidate(mapLoadRoute);
        // Retire state routes before the shared file executor; accepted immutable
        // writes finish on its existing shutdown barrier, with callbacks dead.
        if(clipboardProvider)clipboardProvider->close();clipboardProvider.reset();
        if(systemServices)systemServices->stop();systemServices.reset();
        archiveService.reset();settingsSaves.reset();eventSaves.reset();utility.reset();activityCatalog.reset();activityDisk.reset();
        if(mediaBroker){try{mediaBroker->collectRetired();if(archiveMediaClient)mediaBroker->detachClient(archiveMediaClient);if(notesMediaClient)mediaBroker->detachClient(notesMediaClient);}catch(...){}mediaBroker.reset();}
        mediaVideos.reset();mediaImages.reset();if(mediaDecoder)mediaDecoder->stop();mediaDecoder.reset();
        archiveDates.reset();backdrop.reset();renderer.reset();try{host.setCursor(nullptr);host.destroy();}catch(...){}
    };
    PreviewLifetime lifetime{cleanup};
    const auto selectModule=[&](core::Module module,double time){
        if(archive&&notes&&notes->selected()==core::Module::archive&&module!=core::Module::archive&&!archive->finishEditing(time)){pendingModule=module;return;}
        if(calendar&&notes&&notes->selected()==core::Module::calendar&&module!=core::Module::calendar&&!calendar->dismissMenu(false,time)){pendingModule=module;return;}
        pendingModule.reset();if(mediaPicker){mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();}
        if(readerOwner&&module!=core::Module::reader)readerOwner->cancelImport();
        pendingReaderPicker.reset();pendingReaderAction.reset();
        if(notes)notes->select(module,time);
    };
    const auto pointerLocked=[&]{return (notes&&notes->pointerLocked())||(archive&&archive->pointerLocked())||(shelf&&shelf->pointerLocked())||(clipboard&&clipboard->pointerLocked())||(volume&&volume->pointerLocked())||(eventLog&&eventLog->pointerLocked())||(workMode&&workMode->pointerLocked())||(settingsUI&&settingsUI->pointerLocked())||(storage&&storage->pointerLocked())||(activity&&activity->pointerLocked())||(map&&map->pointerLocked());};
    const auto shelfChoices=[&]{std::vector<endfield::modules::NotesShelfChoice>choices;
        if(shelf)for(const auto&item:shelf->state().items()){
            auto extension=utf8(fs::u8path(item.lastKnownPath).extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);});
            constexpr std::array supported{".png",".jpg",".jpeg",".gif",".heic",".heif",".tif",".tiff",".bmp",".webp",".jp2",".mov",".mp4",".m4v",".avi",".mpeg",".mpg"};
            choices.push_back({item.id,item.name,item.typeDescription,!item.isDirectory&&std::find(supported.begin(),supported.end(),extension)!=supported.end(),!item.availabilityError});
        }return choices;
    };
    struct PublishedEntry{gpu::LayerScene*scene;std::uint64_t content,resources;const gpu::DrawObject*after;std::size_t count;};
    std::vector<gpu::LayerCompositionEntry>ordered;std::vector<PublishedEntry>published;ordered.reserve(134);published.reserve(134);std::uint64_t publishedNotesRevision{};
    auto publishNative=[&]{
        ordered.clear();ordered.push_back({&layers,{}});
        if(settingsUI)settingsUI->upload(renderer);if(battery)battery->upload(renderer);if(workMode)workMode->upload(renderer);if(eventLog)eventLog->upload(renderer);if(volume)volume->upload(renderer);if(clipboard)clipboard->upload(renderer);if(shelf)shelf->upload(renderer);if(archive)archive->upload(renderer);if(storage)storage->upload(renderer);if(activity)activity->upload(renderer);if(reader)reader->upload(renderer);if(calendar)calendar->upload(renderer);if(map)map->upload(renderer);if(game)game->upload(renderer);
        bool settingsAppended{};const auto appendModule=[&](core::Module module){const auto append=[&](auto&owner){if(owner)for(const auto&e:owner->entries())ordered.push_back(e);};switch(module){
            case core::Module::system:case core::Module::display:case core::Module::hotkeys:case core::Module::about:if(settingsUI&&!settingsAppended){for(const auto&e:settingsUI->entries(false))ordered.push_back(e);settingsAppended=true;}break;
            case core::Module::power:append(battery);break;case core::Module::workMode:append(workMode);break;case core::Module::eventLog:append(eventLog);break;case core::Module::volume:append(volume);break;case core::Module::clipboard:append(clipboard);break;case core::Module::fileShelf:append(shelf);break;case core::Module::archive:append(archive);break;case core::Module::storage:append(storage);break;case core::Module::activityMonitor:append(activity);break;case core::Module::reader:append(reader);break;case core::Module::calendar:append(calendar);break;case core::Module::map:append(map);break;case core::Module::minigame:append(game);break;default:break;}};
        // The old wrapper is below the incoming wrapper, independently of the
        // owner declaration order. Floating Notes stay above center modules.
        if(notes){const auto&sample=notes->modulePresentation();appendModule(sample.current.module);if(sample.incoming)appendModule(sample.incoming->module);notes->upload(renderer);for(const auto&e:notes->entries())ordered.push_back(e);}
        if(settingsUI)for(const auto&e:settingsUI->modalEntries())ordered.push_back(e);
        // A language event can leave equal captions/numbers in retained scenes.
        // Refresh only an older font generation; group carriers forward this to
        // their cached local text, while editor owners keep layout/TSF together.
        // The same check catches an inactive scene when it is next published.
        const auto fontRevision=rasterizer.fontRevision();
        for(const auto&e:ordered)if(e.scene->fontRevision()!=fontRevision)e.scene->refreshTypography();
        const auto notesRevision=notes?notes->compositionRevision():0;
        bool changed=ordered.size()!=published.size()||notesRevision!=publishedNotesRevision;if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        // Skill selection and sprite swaps reuse their draw buffer. Pointer and
        // count alone cannot detect those resource-identity changes.
        if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});if(settingsUI)settingsUI->collected(renderer);if(notes)notes->collected(renderer);if(shelf)shelf->collected(renderer);if(clipboard)clipboard->collected(renderer);if(volume)volume->collected(renderer);if(eventLog)eventLog->collected(renderer);if(workMode)workMode->collected(renderer);if(battery)battery->collected(renderer);if(archive)archive->collected(renderer);if(storage)storage->collected(renderer);if(activity)activity->collected(renderer);if(reader)reader->collected(renderer);if(calendar)calendar->collected(renderer);if(game)game->collected(renderer);if(archiveMedia)archiveMedia->collectRetired();if(mediaBroker)mediaBroker->collectRetired();}
        publishedNotesRevision=notesRevision;
    };
    app::WatchSessionEnvironment environment{{1280,800},true,true,true,false,{}};app::WatchSessionSettings settings;auto configuration=ehud::data::Settings::defaults();bool configurationPending=!args.settingsAssets.empty()||!args.storageAssets.empty()||!args.activityAssets.empty()||args.readerModule||args.calendarModule||!args.mapGeography.empty();configuration.set("launchAtLogin",false);session.setSettings(settings,0);session.setEnvironment(environment,0);
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
    // Same existing one-shot host deadline, armed only at explicit Calendar
    // state/system events. Civil midnight uses date arithmetic across DST.
    const auto updateCalendarWake=[&]{calendarWakeActive=calendarState&&calendarState->active();calendarNextWake.reset();if(!args.visible||!calendarState||!calendarState->loaded()||(!calendarState->active()&&calendarState->events().empty()))return;
        const auto zone=calendarCivil->timeZone();const auto utc=ehud::data::foundationNow();const auto tomorrow=zone.day(utc).advanced(1);if(!tomorrow)return;
        for(unsigned minute=0;minute<1440;++minute)if(const auto at=zone.timestamp(*tomorrow,minute/60,minute%60);at&&*at>utc){calendarNextWake=now()+(*at-utc);break;}
    };
    const auto flushCalendar=[&]{if(!calendarState)return true;for(unsigned step=0;step<128;++step){calendarState->queueCapacityAvailable();calendarNotifications->queueCapacityAvailable();utility->waitIdle();utility->drain();const auto&status=calendarNotifications->status();if(!calendarState->hasPendingWork()&&!status.busy&&!status.pending)return !calendarState->hasPersistenceFailure();}return false;};
    auto applyConfiguration=[&](double time){
        if(!configurationPending)return;configurationPending=false;
        settings={configuration.boolean("reduceMotion"),configuration.boolean("ambientAnimation"),configuration.boolean("lowPowerVisualMode"),configuration.number("parallaxIntensity"),configuration.number("perspectiveIntensity"),configuration.number("hudScale"),{configuration.number("hudOffsetX"),configuration.number("hudOffsetY")}};
        session.setSettings(settings,time);watchAppearance.language=settingsLanguage(configuration);
        rasterizer.setDefaultFontLanguage(watchAppearance.language==core::Language::korean?gpu::LayerFontLanguage::korean:gpu::LayerFontLanguage::simplifiedChinese);
        watchAppearance.dark=configuration.string("theme")!="light";
        if(configuration.string("theme")=="system"){DWORD light=0,size=sizeof(light);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size)==ERROR_SUCCESS)watchAppearance.dark=light==0;}
        const auto hex=configuration.string("accentHex");for(unsigned n=0;n<3;++n)watchAppearance.accentSRGB[n]=std::stoul(hex.substr(n*2,2),nullptr,16)/255.;
        const auto a=watchAppearance.accentSRGB;const auto effective=watchAppearance.dark?a:source::sourceBlackBlend(a,.35);const std::array<double,4>accent{effective[0],effective[1],effective[2],1};
        backdropState.dark=watchAppearance.dark;backdropState.blurAmount=configuration.number("blurAmount");backdropState.backgroundDarkness=configuration.number("backgroundDarkness");
        if(nativeAppearance){std::vector<gpu::WatchAppearanceEntry>entries;for(const auto&e:contentCatalog.entries())entries.push_back({e.action,e.target,moduleCaption(e.target,watchAppearance.language,e.title),"module:"+e.target,true});nativeAppearance->setEntries(entries);}
        headerClock.setFormat(configuration.string("clockFormat")=="twelveHour"?endfield::modules::HUDClockFormat::twelveHour:endfield::modules::HUDClockFormat::twentyFourHour,time);
        const auto clock=configuration.string("clockStyle");chromeContent.style=clock=="split"?source::DesktopClockStyle::split:clock=="dial"?source::DesktopClockStyle::dial:clock=="rail"?source::DesktopClockStyle::rail:clock=="stacked"?source::DesktopClockStyle::stacked:source::DesktopClockStyle::digital;
        chromeContent.localizedClose=core::localized("Click outside to close","点击外侧关闭",watchAppearance.language);chromeContent.uppercaseShortcut=endfield::modules::settingsShortcutTitle(endfield::modules::settingsShortcut(configuration));
        if(chrome){chrome->setAppearance({watchAppearance.dark,watchAppearance.dark?a:source::sourceBlackBlend(a,.35)});updateClock();}
        if(tray&&settingsUI&&!settingsUI->controller().capturingShortcut()){const auto key=endfield::modules::settingsShortcut(configuration);const gpu::TrayHotkey desired{key.modifiers,key.key};if(tray->hotkey()!=desired&&!tray->setHotkey(desired))settingsUI->controller().setExternalStatus({},core::localized("Shortcut unavailable","快捷键不可用",watchAppearance.language));}
        if(settingsUI){settingsUI->setAppearance({watchAppearance.dark,2,accent});settingsUI->setLanguage(watchAppearance.language);settingsUI->setReduceMotion(settings.reduceMotion,time);}
        if(battery){battery->setAppearance({watchAppearance.dark,2,watchAppearance.language,accent});battery->setReduceMotion(settings.reduceMotion,time);}
        if(workMode){workMode->setLanguage(watchAppearance.language);workMode->setAppearance({watchAppearance.dark,2,accent});workMode->setReduceMotion(settings.reduceMotion,time);}
        if(eventLog){eventLog->setLanguage(watchAppearance.language);eventLog->setAppearance({watchAppearance.dark,2,accent});eventLog->setReduceMotion(settings.reduceMotion);}
        if(clipboard){clipboard->setLanguage(watchAppearance.language);clipboard->setAppearance({watchAppearance.dark,2,accent},clipboardAssets->images());clipboard->setReduceMotion(settings.reduceMotion);}
        if(shelf){shelf->setLanguage(watchAppearance.language);shelf->setAppearance(watchAppearance.dark,accent);}
        if(storage){storage->setAppearance(storageAppearance(watchAppearance.language,watchAppearance.dark,accent),time);storage->setReduceMotion(settings.reduceMotion,time);}
        if(activity){activity->setAppearance({watchAppearance.dark,2,watchAppearance.language,accent});activity->setReduceMotion(settings.reduceMotion);}
        if(calendar){calendar->setAppearance({watchAppearance.dark,accent},time);calendar->setLanguage(watchAppearance.language,time);calendar->setReduceMotion(settings.reduceMotion,time);if(calendarLanguage!=watchAppearance.language){calendarLanguage=watchAppearance.language;calendarNotifications->setLanguage(calendarLanguage);calendarState->refreshForSystemChange();updateCalendarWake();}}
        if(reader){reader->setAppearance({watchAppearance.dark,accent});reader->setStrings(readerStrings(watchAppearance.language));reader->setReduceMotion(settings.reduceMotion);readerOwner->setFonts(rasterizer.retainedFontResources());}
        if(game){endfield::modules::OrbiPomAppearance appearance;appearance.dark=watchAppearance.dark;appearance.reducedMotion=settings.reduceMotion;appearance.language=watchAppearance.language;appearance.accent={accent[0],accent[1],accent[2],accent[3]};game->setAppearance(std::move(appearance),time);}
        if(map){gpu::MapAppearance appearance;appearance.dark=watchAppearance.dark;appearance.reducedMotion=settings.reduceMotion;appearance.ambient=settings.ambientEnabled;appearance.accent={accent[0],accent[1],accent[2],accent[3]};map->setAppearance(std::move(appearance),time);map->setLanguage(watchAppearance.language,time);}
        if(volume){volume->setStyle({watchAppearance.dark,accent});volume->setReduceMotion(settings.reduceMotion);}
        if(notes)notes->applyPreferences(notesPreferences(watchAppearance.language,watchAppearance.dark,settings.reduceMotion,accent),time);
        if(archive){archive->setAppearance({watchAppearance.dark,2,accent,endfield::modules::ArchiveColor{1,159./255.,10./255.,1}});auto strings=endfield::tools::archivePreviewStrings(watchAppearance.language);archive->setStrings(std::move(strings.view),std::move(strings.menus),std::move(strings.categoryNamePlaceholder),time);archive->setReduceMotion(settings.reduceMotion,time);}
    };
    auto open=[&](double time){closing=false;canvasOpenedAt=time;session.open(time,0x5eed);if(settingsUI)settingsUI->setOverlayVisible(true,time);if(notes)notes->setMediaActive(true,time);if(archive)archive->setOverlayVisible(true,time);if(workMode)workMode->setOverlayVisible(true,time);if(storage)storage->setVisible(true,time);if(activity)activity->setOverlayVisible(true,time);if(reader)reader->setOverlayVisible(true,time);if(calendar)calendar->setOverlayVisible(true,time);if(map)map->setOverlayVisible(true,time);if(game)game->setOverlayVisible(true,time);if(args.visible&&headerClock.setActive(true,time))updateClock();};
    auto demand=[&](double time){if(projection&&projection->presented())return projection->demand(time);auto result=session.demand(time);if(result.phase==core::VisibilityPhase::visible)result.finiteAnimation=result.finiteAnimation||(notes&&notes->requiresFrames(time))||(archive&&archive->requiresFrames(time))||(mediaBroker&&mediaBroker->requiresFrames())||(shelf&&shelf->requiresFrames(time))||(clipboard&&clipboard->requiresFrames(time))||(volume&&volume->requiresFrames(time))||(eventLog&&eventLog->requiresFrames(time))||(workMode&&workMode->requiresFrames(time))||(battery&&battery->requiresFrames(time))||(settingsUI&&settingsUI->requiresFrames(time))||(storage&&storage->requiresFrames(time))||(activity&&activity->requiresFrames(time))||(reader&&reader->requiresFrames(time))||(calendar&&calendar->requiresFrames(time))||(map&&map->requiresFrames(time))||(game&&game->requiresFrames(time));return result;};
    auto scheduleDeadline=[&]{if(!args.visible||!ready||stopping)return;std::optional<double>next;
        auto include=[&](std::optional<double>value){if(value&&(!next||*value<*next))next=value;};
        include(calendarNextWake);if(projection)include(projection->dismissalDeadline());include(headerClock.nextDeadline());if(reader)include(reader->nextWakeTime());if(map)include(map->nextWakeTime());if(settingsUI)include(settingsUI->nextWakeTime(now()));if(workMode)include(workMode->nextWakeTime());if(notes)include(notes->nextWakeTime());if(archive)include(archive->nextWakeTime());if(mediaBroker){const auto wake=mediaBroker->nextWakeTime();if(wake&&*wake>now())include(wake);}if(eventOwner)include(eventOwner->saveDeadline());if(storage)include(storage->nextWakeTime());if(activityProbe)include(activityPlan.nextWakeTime());host.setDeadline(next);
    };
    auto refresh=[&](double time){if(args.visible&&ready&&!stopping){
        projectionDrop->enabled=projection&&projection->presented()&&projectionHandoff.acceptsInput()&&!projection->preview()->importBusy()&&pickerOwner==PickerOwner::none;
        projectionDrop->scale=metrics.scale;
        if(projection&&projection->presented())while(auto action=projection->preview()->takeAction())projectionActions.push_back(std::move(*action));
        if((!projectionActions.empty()||projectionTrayAction||projectionDisplayPending||projectionResizePending||(projection&&projection->dismissalComplete(time)))&&!projectionActionsQueued&&!projectionPainting){need(PostMessageW(static_cast<HWND>(host.hwnd()),projectionActionMessage,serviceGeneration,0)!=FALSE,"Post Projection actions");projectionActionsQueued=true;}
        if(reader&&!readerActionQueued)if(auto action=reader->takeImportAction()){pendingReaderAction=std::move(*action);need(PostMessageW(static_cast<HWND>(host.hwnd()),readerActionMessage,serviceGeneration,0)!=FALSE,"Post Reader import action");readerActionQueued=true;}if(workMode)updateClock();host.setFrameDemand(demand(time));scheduleDeadline();host.invalidate();}};
    const auto requestMediaPicker=[&]{
        if(pickerOwner!=PickerOwner::none||!mediaPicker)return false;
        mediaPicker->setLabels({core::localized("Add image/video","添加图片/视频",watchAppearance.language),core::localized("Add","添加",watchAppearance.language),core::localized("Add selected files","添加所选文件",watchAppearance.language),gpu::ShelfPickerMode::mixedReferences});
        return mediaPicker->request();
    };
    const auto importReaderReference=[&](std::uint64_t generation,std::string path,std::shared_ptr<void>lease,double time){
        if(!reader||!readerOwner||!reader->beginImport(generation,time))return;
        try {auto book=endfield::modules::windowsReaderReference(ehud::data::makeUUID(),path,utf8(fs::u8path(path).filename()));readerOwner->import(generation,std::move(book),std::move(lease));}
        catch(const std::exception&){reader->receiveImportError(generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}
    };
    auto close=[&](double time){if(!closing){std::cout<<"Preview close requested at "<<time<<std::endl;if(archive&&!archive->finishEditing(time)){pendingClose=true;return;}if(calendar&&!calendar->dismissMenu(false,time)){pendingClose=true;return;}if(notes&&!notes->finish()){pendingClose=true;return;}if(workMode&&!workMode->finishEditing(false,time)){pendingClose=true;return;}pendingClose=false;pendingModule.reset();headerClock.setActive(false,time);if(mediaPicker)mediaPicker->cancel();pickerOwner=PickerOwner::none;pendingArchivePicker.reset();if(archiveMedia)archiveMedia->cancelImport();if(readerOwner)readerOwner->cancelImport();pendingReaderPicker.reset();pendingReaderAction.reset();if(map){map->cancelInteraction(time);map->setOverlayVisible(false,time);}if(game){game->cancelInteraction(time);game->setOverlayVisible(false,time);}if(reader){reader->cancelInteraction(time);reader->setOverlayVisible(false,time);}if(calendar){calendar->setOverlayVisible(false,time);updateCalendarWake();}if(archive){archive->cancelInteraction(time);archive->setOverlayVisible(false,time);}if(notes)notes->setMediaActive(false,time,true);if(shelf){shelf->cancelPanels();shelf->cancelInteraction();}if(clipboard)clipboard->cancelInteraction();if(volume)volume->cancelInteraction(time);if(battery)battery->cancelInteraction(time);if(settingsUI)settingsUI->setOverlayVisible(false,time);if(eventLog)eventLog->cancelInteraction();if(storage){storage->cancelInteraction(time);storage->setVisible(false,time);}if(activity){activity->cancelInteraction();activity->setOverlayVisible(false,time);}if(workMode){workMode->cancelInteraction(time);workMode->setOverlayVisible(false,time);}host.capturePointer(false);environment.pointerLocked=false;session.setEnvironment(environment,time);canvasCapturedOpacity=canvasOpacity(time);canvasClosedAt=time;closing=true;session.close(time);refresh(time);}};
    auto finishQuit=[&](double time){
        if(archive&&(!archive->finishEditing(time)||(archiveService&&!archiveService->flush()))){
            quitRequested=false;selectModule(core::Module::archive,time);open(time);host.show();refresh(time);return;
        }
        if(!flushCalendar()){quitRequested=false;selectModule(core::Module::calendar,time);open(time);host.show();refresh(time);return;}
        if(readerOwner&&!readerOwner->flush()){quitRequested=false;selectModule(core::Module::reader,time);open(time);host.show();refresh(time);return;}
        // Flush only at an explicit shutdown boundary. A failed save retains
        // its record and restores the Settings UI instead of silently quitting.
        if(settingsSaves&&!settingsSaves->flush()){
            quitRequested=false;if(settingsUI)settingsUI->controller().setStatus(settingsSaves->status().error.value_or("Unable to save settings"));selectModule(core::Module::system,time);open(time);host.show();refresh(time);return;
        }
        projectionHandoff.cancel();if(projection)projection->close(time);
        stopping=true;host.setDeadline({});host.setFrameDemand({});host.requestStop();
    };
    const auto projectionDisplay=[&](std::uintptr_t preferred)->std::optional<gpu::DisplayDescriptor>{
        const auto displays=gpu::readConnectedDisplays();for(const auto&display:displays)if(display.handle==preferred)return display;
        POINT pointer{};GetCursorPos(&pointer);if(const auto index=gpu::resolveDisplay({},displays,{pointer.x,pointer.y}))return displays[*index];return {};
    };
    const auto moveProjection=[&](const gpu::DisplayDescriptor&display){
        hudDisplay=display.handle;projectionWorkTopPixels=std::max(0,display.workArea.top-display.bounds.top);
        const auto&bounds=display.bounds;need(SetWindowPos(static_cast<HWND>(host.hwnd()),HWND_TOPMOST,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE)!=FALSE,"Position Projection on its retained display");
        if(projection)projection->resize(host.metrics(),projectionWorkTopPixels/host.metrics().scale);
    };
    const auto projectionDropRoute=[&](bool enabled,double time){
        projectionDrop->enabled=false;projectionDrop->paths.clear();if(!shelf)return;
        if(!enabled){shelf->setExternalDropCallbacks({},time);return;}
        projectionDrop->hwnd=static_cast<HWND>(host.hwnd());projectionDrop->scale=metrics.scale;
        const std::weak_ptr<ProjectionDropInbox>weak=projectionDrop;
        shelf->setExternalDropCallbacks(gpu::ShelfDropTarget::Callbacks{
            [weak](POINTL point){auto state=weak.lock();if(!state||!state->enabled||!state->paths.empty())return false;POINT p{point.x,point.y};RECT rect{};return ScreenToClient(state->hwnd,&p)&&GetClientRect(state->hwnd,&rect)&&PtInRect(&rect,p);},
            [weak](std::span<const std::string>paths){auto state=weak.lock();if(!state||!state->enabled||!state->paths.empty()||paths.empty()||paths.size()>endfield::modules::ProjectionModel::maximumMedia)return false;POINT point{};if(!GetCursorPos(&point)||!ScreenToClient(state->hwnd,&point))return false;state->point={point.x/state->scale,point.y/state->scale};state->paths.assign(paths.begin(),paths.end());if(!PostMessageW(state->hwnd,projectionDropMessage,serviceGeneration,0)){state->paths.clear();return false;}return true;}
        },time);
    };
    const auto cancelProjectionPicker=[&]{if(pickerOwner==PickerOwner::projection){mediaPicker->cancel();pickerOwner=PickerOwner::none;}pendingProjectionPicker.reset();};
    const auto projectionOptions=[&]{
        endfield::tools::ProjectionPreviewOptions value;value.language=watchAppearance.language;value.reduceMotion=settings.reduceMotion;
        const auto accent=watchAppearance.accentSRGB;value.accent={accent[0],accent[1],accent[2],1};
        value.raster.assetRoot=args.notesFormatAssets;value.raster.pixelsPerPoint=2;value.raster.paddingPoints=1;
        value.menuStrings=endfield::tools::archivePreviewStrings(watchAppearance.language).menus;value.shelfChoices=shelfChoices;
        constexpr std::string_view wheel="244c34ad474b15c242f9bd62cbf195272cbf7b880acbeaf0021e237d6a22abd2";
        value.colorWheel=gpu::NativeNotesControlsImage{{"notes.menu/wheel","source-generated:NotesColorWheelView.wheel",{8,8,166,166},{1,1,1,1},192,false,false},Json::Object{{"asset","raster/"+std::string(wheel)+".png"},{"sha256",std::string(wheel)}}};
        return value;
    };
    projectionCommand=[&](const app::ProjectionHandoff::Command&command,double time){
        using A=app::ProjectionHandoff::Action;
        if(command.generation!=projectionHandoff.generation())return;
        switch(command.action){
        case A::closeHUD:close(time);if(!closing)projectionHandoff.cancel();break;
        case A::showProjection:{
            host.hide();composition.detach(renderer);published.clear();publishedNotesRevision=0;
            if(!projection){const auto a=watchAppearance.accentSRGB;projection=std::make_unique<endfield::tools::ProjectionWorkspace>(host,renderer,rasterizer,backdrop,*mediaBroker,endfield::modules::NotesColor{a[0],a[1],a[2],1},configuration.number("backgroundDarkness"),configuration.number("blurAmount"));}
            endfield::tools::ProjectionMediaBindingOptions binding;
            binding.resolve=[](const endfield::modules::ProjectionMediaReference&ref){return gpu::NotesImageAccess{fs::u8path(ref.path()),ref.accessLease()};};
            binding.errorText=[&](HRESULT){return core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language);};
            projectionPainting=true;try{projection->resize(metrics,projectionWorkTopPixels/metrics.scale);projection->show(projectionOptions(),std::move(binding),time);projectionDropRoute(true,time);projectionPainting=false;}catch(...){projectionPainting=false;projectionHandoff.cancel();throw;}
            refresh(now());break;
        }
        case A::closeProjection:projectionDropRoute(false,time);cancelProjectionPicker();if(projection)projection->dismiss(time,settings.reduceMotion);refresh(time);break;
        case A::showHUD:projectionDropRoute(false,time);if(projection)projection->close(time);published.clear();publishedNotesRevision=0;open(time);host.show();refresh(time);break;
        case A::external:{projectionDropRoute(false,time);if(projection)projection->close(time);published.clear();publishedNotesRevision=0;
            if(command.externalID){const auto action=static_cast<gpu::TrayAction>(*command.externalID);if(action==gpu::TrayAction::settings)selectModule(core::Module::system,time);else if(action==gpu::TrayAction::about)selectModule(core::Module::about,time);else if(action==gpu::TrayAction::workMode)selectModule(core::Module::workMode,time);}
            open(time);host.show();refresh(time);break;
        }
        case A::closeImmediately:projectionDropRoute(false,time);cancelProjectionPicker();if(projection)projection->close(time);published.clear();publishedNotesRevision=0;break;
        }
    };
    auto activate=[&](const app::WatchActivation&event){const auto entries=contentCatalog.entries();const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto&value){return value.action==event.action;});need(entry!=entries.end(),"Source activation exceeds exported actions");
        if(args.projectionModule&&entry->target=="projection"){
            if(auto command=projectionHandoff.open({session.phase()==core::VisibilityPhase::visible,quitRequested,pendingClose, bool(shelf&&shelf->preservesFocusOnLoss())},{hudDisplay,previousApplication}))projectionCommand(*command,now());return;
        }
        if(notes){for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);if(core::moduleIdentifier(module)==entry->target){selectModule(module,now());break;}}}
        std::cout<<"Source action: "<<entry->target<<(notes&&entry->target=="notes"?" (Notes preview)":shelf&&entry->target=="fileShelf"?" (File Shelf preview)":clipboard&&entry->target=="clipboard"?(args.nativeClipboard?" (Windows Clipboard preview)":" (synthetic Clipboard preview)"):storage&&entry->target=="storage"?" (development Storage preview)":activity&&entry->target=="activityMonitor"?(args.nativeActivity?" (Windows Activity preview)":" (synthetic Activity preview)"):archive&&entry->target=="archive"?" (Archive preview)":reader&&entry->target=="reader"?" (Reader preview)":map&&entry->target=="map"?" (Map preview)":game&&entry->target=="minigame"?" (Minigame preview)":volume&&entry->target=="volume"?" (synthetic Volume preview)":eventLog&&entry->target=="eventLog"?" (synthetic Event Log preview)":workMode&&entry->target=="workMode"?" (Work Mode preview)":battery&&entry->target=="power"?" (synthetic Battery preview)":settingsUI&&(entry->target=="system"||entry->target=="display"||entry->target=="hotkeys"||entry->target=="about")?" (Settings preview)":" (module body is not installed)")<<'\n';};
    LiveProbe probe;probe.enabled=!args.liveDiagnostics.empty();
    auto present=[&](double time,bool submit){
        const auto traceFrameStart=readerTrace?Clock::now():Clock::time_point{};
        if(projection&&projection->presented()){
            projectionPainting=true;try{mediaBroker->sample(time);projection->mediaChanged(time);projection->render(time,submit);projectionPainting=false;}catch(...){projectionPainting=false;throw;}return true;
        }
        readerTime=time;mapTime=time;calendarTime=time;
        if(!game&&ensureGame&&notes&&notes->selected()==core::Module::minigame)ensureGame(time);
        if(map&&notes&&notes->selected()==core::Module::map&&requestMapGeography)requestMapGeography();
        if(storageFixture)storageFixture->time.store(time,std::memory_order_relaxed);
        auto frameProbe=probe.measure(LiveProbe::frame);applyConfiguration(time);
        if(clipboardDirty&&clipboard&&notes&&notes->selected()==core::Module::clipboard){clipboard->refresh();clipboardDirty=false;}
        // Backdrop COM calls can dispatch nested input. Finish them before
        // borrowing a source frame, then sample the current live event time.
        // Offscreen comparisons retain their explicit synthetic timestamps.
        {auto stage=probe.measure(LiveProbe::backdrop);updateBackdrop(time);}if(args.visible)time=std::max(time,now());
        const app::WatchSessionFrame*sample=nullptr;{auto stage=probe.measure(LiveProbe::sourcePose);sample=session.sample(time);}if(!sample)return false;
        if(focused&&sample->visibility.phase==core::VisibilityPhase::visible&&!session.inputEnabled())session.setInputEnabled(true,time);
        auto parameters=materials.parameters();parameters.camera=sample->gpuCamera;parameters.timeSeconds=sample->shaderTime;parameters.width=metrics.pixelWidth;parameters.height=metrics.pixelHeight;if(nativeAppearance){std::array<float,3>linear;for(unsigned n=0;n<3;++n){const auto value=watchAppearance.accentSRGB[n];linear[n]=static_cast<float>(value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4));}parameters.desktopAccentLinear=linear;}
        {auto stage=probe.measure(LiveProbe::materials);materialPresentation.update(*sample->sourceFrame,parameters);materials.flush(renderer.sourceGraphics());}
        {auto stage=probe.measure(LiveProbe::nativeLabels);if(nativeContent)nativeContent->update(session.actions());
        for(auto&button:available){const auto found=session.actions().find(button.buttonID);button.enabled=found!=session.actions().end();button.expandFileShelfCaption=button.enabled&&nativeAppearance&&nativeAppearance->expandsFileShelfCaption(found->second,watchAppearance.language);}
        const core::Rect viewport{0,0,metrics.width,metrics.height};labels.update(*sample->sourceFrame,sample->camera,viewport,available);
        if(nativeAppearance){watchAppearance.selectedAction=notes?contentCatalog.actionForTarget(core::moduleIdentifier(notes->selected())).value_or(0):0;nativeAppearance->update(session.actions(),watchAppearance,labels.placements());}
        // Chrome samples once per visible second; opacity follows the same
        // ready/close timestamps as the source outer controller.
        const source::DesktopChromeSettings chromeSettings{viewport,settings.hudScale,settings.hudOffset,notes?notes->selected():core::Module::power,true};
        if(chrome)chrome->update(*sample->sourceFrame,sample->camera,chromeSettings,static_cast<float>(canvasOpacity(time)));
        }
        const source::DesktopChromeSettings noteChromeSettings{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,notes?notes->selected():core::Module::power,true};
        {auto stage=probe.measure(LiveProbe::notesPose);if(notes&&chromePlan&&chromePlan->projection().center)notes->update(*chromePlan->projection().center,noteChromeSettings,static_cast<float>(canvasOpacity(time)),time,focused);if(settingsUI&&notes&&chromePlan&&chromePlan->projection().center)settingsUI->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(workMode&&notes&&chromePlan&&chromePlan->projection().center)workMode->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(battery&&notes&&chromePlan&&chromePlan->projection().center)battery->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(eventLog&&notes&&chromePlan&&chromePlan->projection().center)eventLog->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(volume&&notes&&chromePlan&&chromePlan->projection().center)volume->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(shelf&&notes&&chromePlan&&chromePlan->projection().center)shelf->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(clipboard&&notes&&chromePlan&&chromePlan->projection().center)clipboard->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);}
        if(archive&&notes&&chromePlan&&chromePlan->projection().center)archive->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);
        if(storage&&notes&&chromePlan&&chromePlan->projection().center)storage->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);
        if(activity&&notes&&chromePlan&&chromePlan->projection().center)activity->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);
        if(reader&&notes&&chromePlan&&chromePlan->projection().center)reader->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);
        if(calendar&&notes&&chromePlan&&chromePlan->projection().center){calendar->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);if(calendarWakeActive!=calendarState->active())updateCalendarWake();}
        if(map&&notes&&chromePlan&&chromePlan->projection().center)map->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);
        if(game&&notes&&chromePlan&&chromePlan->projection().center)game->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);
        // Visibility unions are established first. Providers tick exactly once
        // on this existing frame clock; module owners only reread their clients.
        if(archiveMedia)archiveMedia->sync(time);
        if(mediaBroker){mediaBroker->sample(time);if(notes)notes->refreshSharedMedia(time);if(archiveMedia&&archiveMedia->refresh(time)&&archive&&notes&&chromePlan&&chromePlan->projection().center)archive->update(*chromePlan->projection().center,noteChromeSettings,notes->modulePresentation(),static_cast<float>(canvasOpacity(time)),time);}
        {auto stage=probe.measure(LiveProbe::publication);publishNative();composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale,1));}
        if(submit){auto stage=probe.measure(LiveProbe::draw);renderer.draw(args.visible);}
        if(readerTrace&&reader&&notes&&notes->selected()==core::Module::reader)readerTrace->record(false,time,reader->scrollSnapshot(),0,0,std::chrono::duration<double,std::milli>(Clock::now()-traceFrameStart).count());return true;
    };
    app::OverlayCallbacks callbacks;
    callbacks.resize=[&](const auto&value){if(projection&&projection->presented()&&projectionPainting){projectionResizePending=value;return;}metrics=value;if(notes)notes->resize(value);if(archive&&value.pixelWidth&&value.pixelHeight)archive->resize(value);if(shelf)shelf->resize(value);if(clipboard)clipboard->resize(value);if(volume)volume->resize(value);if(eventLog)eventLog->resize(value);if(workMode)workMode->resize(value);if(battery)battery->resize(value);if(settingsUI)settingsUI->resize(value);if(activity&&value.pixelWidth&&value.pixelHeight)activity->resize(value);if(reader&&value.pixelWidth&&value.pixelHeight)reader->resize(value);if(calendar&&value.pixelWidth&&value.pixelHeight)calendar->resize(value);if(map&&value.pixelWidth&&value.pixelHeight)map->resize(value);if(game&&value.pixelWidth&&value.pixelHeight)game->resize(value);if(storage){if(value.pixelWidth&&value.pixelHeight)storage->resize(value);storage->setVisible(value.pixelWidth&&value.pixelHeight&&!closing&&session.phase()!=core::VisibilityPhase::concealed,storageFixture?storageFixture->time.load(std::memory_order_relaxed):now());}if(!ready)return;const auto time=now();environment.viewport={value.width,value.height};environment.onScreen=value.pixelWidth>0&&value.pixelHeight>0;session.setEnvironment(environment,time);if(environment.onScreen)renderer.resize(value.pixelWidth,value.pixelHeight);if(projection&&projection->presented())projection->resize(value,projectionWorkTopPixels/value.scale);refresh(time);};
    callbacks.pointer=[&](const app::PointerEvent&e){auto stage=probe.measure(LiveProbe::pointer);if(!ready)return false;const auto time=now();const core::Point p{e.x,e.y};
        if(projection&&projection->presented()){
            if(projectionHandoff.acceptsInput()){projection->pointer(e,time);host.capturePointer(projection->preview()->pointerLocked());refresh(time);}return true;
        }
        const auto settingsPointer=[&]{if(!settingsUI||!session.inputEnabled())return false;const bool handled=settingsUI->pointer(e,time);if(handled){environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);}return handled;};
        const auto archivePointer=[&]{if(!archive||!session.inputEnabled())return false;const bool handled=archive->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);}return handled;};
        const auto calendarPointer=[&]{if(!calendar||!session.inputEnabled())return false;const bool handled=calendar->pointer(e,time);if(handled){environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);}return handled;};
        const auto readerPointer=[&]{if(!reader||!session.inputEnabled())return false;const bool handled=reader->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);}return handled;};
        const auto mapPointer=[&]{if(!map||!session.inputEnabled())return false;const bool handled=map->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.button==app::PointerButton::left){if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);}refresh(time);}return handled;};
        const auto gamePointer=[&]{if(!game||!session.inputEnabled())return false;const bool handled=game->pointer(e,time);if(handled){environment.pointer=p;session.setEnvironment(environment,time);if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.button==app::PointerButton::left){if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);}refresh(time);}return handled;};
        if(settingsUI&&(settingsUI->modalActive()||settingsUI->pointerLocked())&&settingsPointer())return true;
        if(game&&game->state().rulesPresented()&&gamePointer())return true;
        if(calendar&&calendar->capturesPointer()&&calendarPointer())return true;
        if(reader&&reader->capturesPointer()&&readerPointer())return true;
        if(map&&map->state().dragging()&&mapPointer())return true;
        if(archive&&archive->pointerLocked()&&archivePointer())return true;
        if(notes&&session.inputEnabled()){
            const bool handled=notes->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(settingsPointer())return true;
        if(archivePointer())return true;
        if(calendarPointer())return true;
        if(readerPointer())return true;
        if(mapPointer())return true;
        if(gamePointer())return true;
        if(shelf&&session.inputEnabled()){
            const bool handled=shelf->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(clipboard&&session.inputEnabled()){
            const bool handled=clipboard->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(volume&&session.inputEnabled()){
            const bool handled=volume->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(storage&&session.inputEnabled()){
            const bool handled=storage->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(activity&&session.inputEnabled()){
            const bool handled=activity->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(eventLog&&session.inputEnabled()){
            const bool handled=eventLog->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
            if(handled){if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        }
        if(battery&&session.inputEnabled()&&battery->pointer(e,time)){environment.pointer=p;session.setEnvironment(environment,time);if(e.kind==app::PointerKind::move)session.pointerMove(p,time);if(e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)host.capturePointer(true);if(e.kind==app::PointerKind::up)host.capturePointer(false);refresh(time);return true;}
        if(workMode&&session.inputEnabled()){
            const bool handled=workMode->pointer(e,time);environment.pointerLocked=pointerLocked();environment.pointer=p;session.setEnvironment(environment,time);
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
    callbacks.wheel=[&](const app::WheelEvent&e){if(!ready)return false;const auto time=now();if(projection&&projection->presented()){if(projectionHandoff.acceptsInput()){projection->wheel(e,time);refresh(time);}return true;}if(settingsUI&&settingsUI->modalActive()&&session.inputEnabled()&&settingsUI->wheel(e,time)){refresh(time);return true;}if(game&&game->state().rulesPresented()&&session.inputEnabled()&&game->wheel(e,time)){refresh(time);return true;}if(calendar&&calendar->capturesPointer()&&session.inputEnabled()&&calendar->wheel(e,time)){refresh(time);return true;}if(notes&&session.inputEnabled()&&notes->wheel(e,time)){refresh(time);return true;}if(settingsUI&&session.inputEnabled()&&settingsUI->wheel(e,time)){refresh(time);return true;}if(archive&&session.inputEnabled()&&archive->wheel(e,time)){refresh(time);return true;}if(shelf&&session.inputEnabled()&&shelf->wheel(e,time)){refresh(time);return true;}if(clipboard&&session.inputEnabled()&&clipboard->wheel(e,time)){refresh(time);return true;}if(volume&&session.inputEnabled()&&volume->wheel(e,time)){refresh(time);return true;}if(eventLog&&session.inputEnabled()&&eventLog->wheel(e,time)){refresh(time);return true;}if(activity&&session.inputEnabled()&&activity->wheel(e,time)){refresh(time);return true;}if(calendar&&session.inputEnabled()&&calendar->wheel(e,time)){refresh(time);return true;}if(reader&&session.inputEnabled()&&reader->wheel(e,time)){if(readerTrace)readerTrace->record(true,time,reader->scrollSnapshot(),e.steps,e.linesPerStep);refresh(time);return true;}if(map&&session.inputEnabled()&&map->wheel(e,time)){environment.pointerLocked=pointerLocked();session.setEnvironment(environment,time);refresh(time);return true;}if(game&&session.inputEnabled()&&game->wheel(e,time)){refresh(time);return true;}if(e.horizontal)return false;const bool handled=session.wheel({e.x,e.y},e.steps,e.linesPerStep,time);if(handled)refresh(time);return handled;};
    callbacks.beforeKeyTranslation=[&](const app::NativeMessage&m){if(!ready||(projection&&projection->presented()))return false;if(settingsUI&&(settingsUI->modalActive()||settingsUI->controller().capturingShortcut()))return false;if(shelf&&shelf->filterKey(m))return true;if(workMode&&workMode->filterKey(m))return true;if(archive&&archive->filterKey(m))return true;const auto target=static_cast<HWND>(m.window),owner=static_cast<HWND>(host.hwnd());if(target!=owner&&!IsChild(owner,target))return false;if(calendar&&calendar->filterKey(m))return true;return notes&&notes->filterKey(m);};
    callbacks.appMessage=[&](const app::NativeMessage&m)->std::optional<std::intptr_t>{auto stage=probe.measure(LiveProbe::message);
        if(!stopping&&systemServices)systemServices->handle_message(m.message,m.wParam,m.lParam);
        if(ready&&tray&&tray->message(m.message,m.wParam,m.lParam)){
            if(const auto action=tray->takeAction()){
                const auto time=now();
                if(projectionHandoff.active()){projectionTrayAction=*action;refresh(time);return 0;}
                if(*action==gpu::TrayAction::quit){quitRequested=true;if(session.phase()==core::VisibilityPhase::concealed)finishQuit(time);else close(time);}
                else if(*action==gpu::TrayAction::openOverlay||*action==gpu::TrayAction::workMode||*action==gpu::TrayAction::settings||*action==gpu::TrayAction::about){
                    if(*action==gpu::TrayAction::settings)selectModule(core::Module::system,time);if(*action==gpu::TrayAction::about)selectModule(core::Module::about,time);
                    if(*action==gpu::TrayAction::workMode)selectModule(core::Module::workMode,time);
                    if(m.message==WM_HOTKEY&&focused&&session.phase()!=core::VisibilityPhase::concealed&&!closing)close(time);
                    else {if(session.phase()==core::VisibilityPhase::concealed||closing)open(time);host.show();refresh(time);}
                }
            }return 0;
        }
        if(ready&&m.wParam==serviceGeneration){
            if(m.message==utilityMessage){if(utility)utility->drain();if(activityProbe)activityProbe->submitPending();if(systemServices)systemServices->clipboard_queue_capacity_available();if(storage)storage->utilityCompleted(storageFixture?storageFixture->time.load(std::memory_order_relaxed):now());if(archiveService)archiveService->queueCapacityAvailable();if(readerOwner)readerOwner->queueCapacityAvailable();if(calendarState){calendarState->queueCapacityAvailable();calendarNotifications->queueCapacityAvailable();}if(map){if(requestMapGeography&&notes&&notes->selected()==core::Module::map)requestMapGeography();map->utilityCompleted(args.visible?now():mapTime);}if(settingsSaves){settingsSaves->queueCapacityAvailable();if(settingsSaves->status().error&&settingsUI)settingsUI->controller().setStatus(*settingsSaves->status().error);}if(eventSaves)eventSaves->retry();refresh(now());return 0;}
            if(m.message==projectionDropMessage){
                auto paths=std::exchange(projectionDrop->paths,{});const auto time=now();
                if(!paths.empty()&&projection&&projection->presented()&&projectionHandoff.acceptsInput())if(const auto token=projection->preview()->prepareImportRequest()){
                    try{std::vector<endfield::tools::ProjectionImportFile>files;files.reserve(paths.size());for(auto&path:paths){auto title=utf8(fs::u8path(path).filename());files.push_back({std::move(path),std::move(title),{}});}
                        projection->media()->beginImport(*token,files,projectionDrop->point,time);
                    }catch(const std::exception&){projection->preview()->receiveImportError(*token,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}
                }
                refresh(time);return 0;
            }
            if(m.message==projectionActionMessage){
                projectionActionsQueued=false;const auto time=now();if(projectionPainting)return 0;
                if(auto trayAction=std::exchange(projectionTrayAction,{})){
                    if(*trayAction==gpu::TrayAction::quit){projectionActions.clear();cancelProjectionPicker();if(auto command=projectionHandoff.cancel())projectionCommand(*command,time);quitRequested=true;finishQuit(time);return 0;}
                    if(projectionHandoff.active()){
                        if(auto command=projectionHandoff.external(static_cast<std::uint64_t>(*trayAction)))projectionCommand(*command,time);
                    }
                }
                if(auto resized=std::exchange(projectionResizePending,{}))callbacks.resize(*resized);
                if(std::exchange(projectionDisplayPending,false)&&projectionHandoff.phase()==app::ProjectionHandoff::Phase::projection){
                    if(const auto display=projectionDisplay(hudDisplay)){moveProjection(*display);projectionHandoff.reposition(display->handle);}
                    else{if(auto command=projectionHandoff.cancel())projectionCommand(*command,time);host.hide();host.setFrameDemand({});}
                }
                while(!projectionActions.empty()){
                    auto action=std::move(projectionActions.front());projectionActions.pop_front();
                    if(!projection||!projection->presented()||!projectionHandoff.acceptsInput())continue;
                    auto*view=projection->preview();using K=endfield::tools::ProjectionPreviewAction::Kind;
                    if(action.kind==K::returnToHUD){if(auto command=projectionHandoff.returnToHUD())projectionCommand(*command,time);continue;}
                    if(action.kind==K::togglePlayback||action.kind==K::seek){projection->media()->action(action,time);continue;}
                    if(!view->importRequestCurrent(action.generation))continue;
                    try{
                        if(action.kind==K::chooseLocal){
                            if(requestMediaPicker()){pickerOwner=PickerOwner::projection;pendingProjectionPicker=action;host.capturePointer(false);}
                            else view->receiveImportError(action.generation,core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);
                        }else if(action.kind==K::useShelf&&shelf){
                            auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action.shelfID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf Projection reference is unavailable");
                            const std::array files{endfield::tools::ProjectionImportFile{lease->metadata().windowsPath,lease->metadata().name,lease}};
                            projection->media()->beginImport(action.generation,files,{},time);
                        }
                    }catch(const std::exception&){view->receiveImportError(action.generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}
                }
                if(projection&&projection->dismissalComplete(time))if(auto command=projectionHandoff.projectionClosed(projectionHandoff.generation()))projectionCommand(*command,time);
                refresh(time);return 0;
            }
            if(m.message==readerActionMessage){
                readerActionQueued=false;auto action=std::exchange(pendingReaderAction,{});const auto time=now();
                if(action&&reader&&reader->importRequestCurrent(action->generation))try {
                    if(action->kind==endfield::tools::ReaderImportAction::Kind::chooseLocal){
                        if(pickerOwner!=PickerOwner::none||!mediaPicker)reader->receiveImportError(action->generation,core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);
                        else {const auto strings=readerStrings(watchAppearance.language);mediaPicker->setLabels({strings.open,strings.open,strings.open,gpu::ShelfPickerMode::singleReaderFile});
                            if(mediaPicker->request()){pickerOwner=PickerOwner::reader;pendingReaderPicker=*action;host.capturePointer(false);}
                            else reader->receiveImportError(action->generation,core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);}
                    }else if(shelf){auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action->shelfID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf book reference is unavailable");importReaderReference(action->generation,lease->metadata().windowsPath,lease,time);}
                }catch(const std::exception&){reader->receiveImportError(action->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}
                refresh(time);return 0;
            }
            if(m.message==calendarChangedMessage){calendarRefreshQueued=false;updateCalendarWake();refresh(now());return 0;}
            if(m.message==mapChangedMessage){mapRefreshQueued=false;refresh(now());return 0;}
            if(m.message==readerChangedMessage){readerRefreshQueued=false;refresh(now());return 0;}
            if(m.message==clipboardChangedMessage){clipboardRefreshQueued=false;if(clipboardDirty&&session.phase()!=core::VisibilityPhase::concealed&&notes&&notes->selected()==core::Module::clipboard)refresh(now());return 0;}
            if(m.message==eventChangedMessage){eventRefreshQueued=false;if(eventLog)eventLog->refresh();refresh(now());return 0;}
            if(m.message==archiveChangedMessage){archiveRefreshQueued=false;refresh(now());return 0;}
            if(m.message==sharedMediaMessage&&mediaBroker){const auto time=now();mediaBroker->accept(m.wParam,time);if(notes)notes->refreshSharedMedia(time);if(archiveMedia)archiveMedia->refresh(time);if(projection&&projection->presented())projection->mediaChanged(time);refresh(time);return 0;}
            if(m.message==mediaPickerMessage&&mediaPicker){
                host.suspendCursor(true);mediaPicker->handleMessage(m.wParam,m.lParam);host.suspendCursor(false);
                if(auto result=mediaPicker->drain(m.wParam)){
                    const auto owner=std::exchange(pickerOwner,PickerOwner::none);auto action=std::move(pendingArchivePicker);pendingArchivePicker.reset();auto readerAction=std::move(pendingReaderPicker);pendingReaderPicker.reset();auto projectionAction=std::exchange(pendingProjectionPicker,{});const auto time=now();
                    if(owner==PickerOwner::projection&&projectionAction&&projection&&projection->presented()&&projection->preview()->importRequestCurrent(projectionAction->generation)){
                        auto*view=projection->preview();if(result->canceled())view->receiveImportError(projectionAction->generation,{},time);
                        else if(SUCCEEDED(result->result)){try{std::vector<endfield::tools::ProjectionImportFile>files;files.reserve(result->paths.size());for(const auto&path:result->paths)files.push_back({path,utf8(fs::u8path(path).filename()),{}});projection->media()->beginImport(projectionAction->generation,files,{},time);}catch(const std::exception&){view->receiveImportError(projectionAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}}
                        else view->receiveImportError(projectionAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                    }else if(owner==PickerOwner::reader&&readerAction&&reader&&reader->importRequestCurrent(readerAction->generation)){
                        if(result->canceled())reader->receiveImportError(readerAction->generation,{},time);
                        else if(SUCCEEDED(result->result)&&result->paths.size()==1)importReaderReference(readerAction->generation,result->paths.front(),{},time);
                        else reader->receiveImportError(readerAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                    }else if(!result->canceled()&&owner==PickerOwner::notes&&notes&&!closing&&notes->selected()==core::Module::notes){
                        if(SUCCEEDED(result->result))notes->importMedia(result->paths,mediaInsertionPoint,time);else notes->showMediaError(core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                    }else if(!result->canceled()&&owner==PickerOwner::archive&&action&&archive&&archiveMedia&&archive->mediaRequestCurrent(action->documentID,action->generation)){
                        if(SUCCEEDED(result->result)){try{std::vector<endfield::tools::ArchiveMediaImportFile>files;files.reserve(result->paths.size());for(const auto&path:result->paths)files.push_back({path,utf8(fs::u8path(path).filename()),{}});archiveMedia->beginImport(*action,files,time);}catch(const std::exception&e){archive->receiveMedia(action->documentID,action->generation,{},e.what(),time);}}
                        else archive->receiveMedia(action->documentID,action->generation,{},core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                    }
                }refresh(now());return 0;
            }
        }
        if(ready&&(m.message==WM_TIMECHANGE||m.message==WM_SETTINGCHANGE||(m.message==WM_POWERBROADCAST&&(m.wParam==PBT_APMRESUMEAUTOMATIC||m.wParam==PBT_APMRESUMESUSPEND)))){const auto time=now();if(m.message==WM_TIMECHANGE&&archiveDates)archiveDates->refreshSystemTimeZone();if(calendar){calendar->systemChanged(time);updateCalendarWake();}refresh(time);return 0;}
        if(ready&&archive&&m.message==WM_APP+221&&m.wParam==archive->mediaRouteGeneration()){
            if(auto action=archive->takeMediaAction())try{using K=endfield::tools::ArchiveMediaAction::Kind;const auto time=now();
                if(action->kind==K::chooseLocal){if(!requestMediaPicker())archive->receiveMedia(action->documentID,action->generation,{},core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);else{pickerOwner=PickerOwner::archive;pendingArchivePicker=*action;host.capturePointer(false);}}
                else if(action->kind==K::chooseShelf)archive->presentShelfMedia(shelfChoices(),time);
                else if(action->kind==K::useShelf&&shelf){auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action->itemID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf media reference is unavailable");const std::array files{endfield::tools::ArchiveMediaImportFile{lease->metadata().windowsPath,lease->metadata().name,lease}};archiveMedia->beginImport(*action,files,time);}
                else if(archiveMedia)archiveMedia->action(*action,time);
            }catch(const std::exception&e){if(archiveService&&archiveService->state().selected())archive->setMediaPlayback(archive->mediaIndex(),false,0,e.what(),now());}
            refresh(now());return 0;
        }
        if(ready&&notes&&m.message==endfield::tools::NotesPreview::mediaActionMessage){
            if(auto action=notes->takeMediaAction(m.wParam))try{using K=endfield::tools::NotesMediaAction::Kind;
                if(action->kind==K::chooseLocal){mediaInsertionPoint=action->workspacePoint;if(!requestMediaPicker())notes->showMediaError("文件选择器正忙",now());else{pickerOwner=PickerOwner::notes;host.capturePointer(false);}}
                else if(action->kind==K::chooseShelf){
                    notes->presentShelfMedia(shelfChoices(),action->workspacePoint,now());
                }else if(shelf)notes->importMedia(shelf->access(action->itemID),action->workspacePoint,now());
            }catch(const std::exception&e){notes->showMediaError(e.what(),now());}
            refresh(now());return 0;
        }
        if(ready&&calendar&&calendar->message(m,now())){if(pendingModule)selectModule(*pendingModule,now());if(pendingClose)close(now());refresh(now());return 0;}
        if(ready&&archive&&archive->message(m,now())){if(pendingModule)selectModule(*pendingModule,now());if(pendingClose)close(now());refresh(now());return 0;}
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
    callbacks.key=[&](const app::KeyEvent&e){auto stage=probe.measure(LiveProbe::key);if(ready&&projection&&projection->presented()){if(projectionHandoff.acceptsInput()){projection->key(e,now());refresh(now());}return true;}if(ready&&settingsUI&&session.inputEnabled()&&(settingsUI->modalActive()||settingsUI->controller().capturingShortcut())&&settingsUI->key(e,now())){refresh(now());return true;}if(ready&&calendar&&calendar->capturesPointer()&&session.inputEnabled()&&calendar->key(e,now())){refresh(now());return true;}if(ready&&notes&&session.inputEnabled()&&notes->key(e,now())){refresh(now());return true;}if(ready&&settingsUI&&session.inputEnabled()&&settingsUI->key(e,now())){refresh(now());return true;}if(ready&&archive&&session.inputEnabled()&&archive->key(e,now())){refresh(now());return true;}if(ready&&shelf&&session.inputEnabled()&&shelf->key(e,now())){refresh(now());return true;}if(ready&&clipboard&&session.inputEnabled()&&clipboard->key(e,now())){refresh(now());return true;}if(ready&&volume&&session.inputEnabled()&&volume->key(e,now())){refresh(now());return true;}if(ready&&eventLog&&session.inputEnabled()&&eventLog->key(e,now())){refresh(now());return true;}if(ready&&workMode&&session.inputEnabled()&&workMode->key(e,now())){refresh(now());return true;}if(ready&&activity&&session.inputEnabled()&&activity->key(e,now())){refresh(now());return true;}if(ready&&calendar&&session.inputEnabled()&&calendar->key(e,now())){refresh(now());return true;}if(ready&&reader&&session.inputEnabled()&&reader->key(e,(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)||(GetKeyState(VK_SHIFT)&0x8000),now())){refresh(now());return true;}if(ready&&map&&session.inputEnabled()&&map->key(e,now())){refresh(now());return true;}if(ready&&game&&session.inputEnabled()&&game->key(e,now())){refresh(now());return true;}if(ready&&e.kind==app::KeyKind::down&&e.value==VK_ESCAPE){std::cout<<"Preview unhandled Escape"<<std::endl;close(now());return true;}return false;};
    callbacks.focus=[&](bool value){std::cout<<"Preview focus: "<<value<<std::endl;focused=value;if(projection&&projection->presented()){if(!value)projection->cancelInteraction(now());if(ready)refresh(now());return;}if(notes)notes->focus(value);if(archive)archive->focus(value,now());if(calendar)calendar->focus(value,now());if(workMode)workMode->focus(value,now());if(shelf&&!value)shelf->cancelInteraction();if(clipboard&&!value)clipboard->cancelInteraction();if(volume&&!value)volume->cancelInteraction(now());if(eventLog&&!value)eventLog->cancelInteraction();if(activity&&!value)activity->cancelInteraction();if(reader&&!value)reader->cancelInteraction(now());if(map&&!value)map->cancelInteraction(now());if(game){game->setForeground(value,now());if(!value)game->cancelInteraction(now());}if(storage&&!value)storage->cancelInteraction(storageFixture?storageFixture->time.load(std::memory_order_relaxed):now());if(settingsUI&&!value)settingsUI->cancelInteraction(now());if(ready){const auto time=now();if(!focused){environment.pointer.reset();session.pointerMove({},time);session.setInputEnabled(false,time);}else if(session.phase()==core::VisibilityPhase::visible)session.setInputEnabled(true,time);refresh(time);}};
    callbacks.applicationActive=[&](bool active){
        if(ready&&active&&calendar){calendar->systemChanged(now());updateCalendarWake();}
        if(!ready||!args.visible||active||projectionHandoff.active()||closing||session.phase()==core::VisibilityPhase::concealed||!configuration.boolean("closeOnFocusLost"))return;
        // Source exempts its own file panels and active shelf drag/drop. Moving
        // keyboard focus between owned windows is not an application switch.
        const auto picker=mediaPicker?mediaPicker->stats():gpu::ShelfPickerStats{};
        if(picker.queued||picker.presenting||(shelf&&shelf->preservesFocusOnLoss()))return;
        close(now());
    };
    callbacks.displayChanged=[&]{if(!ready||!projectionHandoff.active()||projectionHandoff.phase()!=app::ProjectionHandoff::Phase::projection)return;
        // Compositor calls may dispatch this notification. Retain only the
        // latest topology signal, then resize/release after the current paint.
        projectionDisplayPending=true;refresh(now());
    };
    callbacks.closeRequested=[&]{std::cout<<"Preview native close request"<<std::endl;if(!ready)return;if(projectionHandoff.active()){projectionTrayAction=gpu::TrayAction::openOverlay;refresh(now());}else close(now());};
    callbacks.deadline=[&](double time){if(!ready||stopping)return;bool artwork=projection&&projection->dismissalComplete(time);if(calendarNextWake&&*calendarNextWake<=time){calendar->systemChanged(time);updateCalendarWake();artwork=true;}artwork=(notes&&notes->deadline(time))||artwork;if(archive)artwork=archive->deadline(time)||artwork;if(reader)artwork=reader->deadline(time)||artwork;if(map){artwork=map->deadline(time)||artwork;environment.pointerLocked=pointerLocked();session.setEnvironment(environment,time);}if(mediaBroker){const auto wake=mediaBroker->nextWakeTime();const bool due=wake&&*wake<=time;if(session.phase()==core::VisibilityPhase::concealed){mediaBroker->sample(time);if(notes)notes->refreshSharedMedia(time);if(archiveMedia)archiveMedia->refresh(time);if(projection&&projection->presented()){projection->mediaChanged(time);artwork=artwork||due;}}else artwork=artwork||due;}if(settingsUI){settingsUI->wake(time);artwork=true;}if(workMode){const auto revision=workMode->state().revision();workMode->wake(time);artwork=artwork||revision!=workMode->state().revision();}if(headerClock.wake(time)||workMode)artwork=updateClock()||artwork;if(eventSaves)eventSaves->capture(time);if(storage)artwork=storage->deadline(time)||artwork;if(activityProbe)activityProbe->update(time);scheduleDeadline();if(artwork)refresh(time);};
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
        const bool active=present(time,true);if(stopping)return;
        if(projection&&projection->presented()){
            if(projection->dismissalComplete(time))if(auto command=projectionHandoff.projectionClosed(projectionHandoff.generation()))projectionCommand(*command,time);
            if(!projectionActions.empty()||projectionTrayAction||projectionDisplayPending||projectionResizePending)refresh(now());
            host.setFrameDemand(demand(now()));scheduleDeadline();return;
        }
        host.setFrameDemand(demand(time));scheduleDeadline();
        if(!active&&session.phase()==core::VisibilityPhase::concealed){
            if(notes)notes->setMediaActive(false,time);host.hide();
            if(projectionHandoff.phase()==app::ProjectionHandoff::Phase::closingHUD){
                const auto display=projectionDisplay(hudDisplay);if(display)moveProjection(*display);
                if(auto command=projectionHandoff.hudClosed(projectionHandoff.generation(),display.has_value(),display?std::optional(display->handle):std::nullopt))projectionCommand(*command,now());return;
            }
            if(tray&&!quitRequested){scheduleDeadline();}else finishQuit(time);
        }};
    app::OverlayOptions windowOptions{!args.notesData.empty()?L"EndfieldHUD Notes preview — temporary sample data":L"EndfieldHUD source shell feasibility — synthetic data",0,0,1280,800,{}};
    if(args.visible){
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
        endfield::tools::NotesPreviewMediaOptions sharedNotesMedia;
        if(!args.archiveAssets.empty()||args.projectionModule){
            const gpu::NotesImageRoute route{static_cast<HWND>(host.hwnd()),sharedMediaMessage,serviceGeneration};
            mediaDecoder=std::make_unique<gpu::NativeNotesImageDecoder>([](const gpu::NotesImageRequest&r){return gpu::NotesImageAccess{fs::u8path(r.path),r.accessLease};},route);
            mediaImages=std::make_unique<gpu::NativeNotesImagePlayback>(*mediaDecoder);
            mediaVideos=std::make_unique<gpu::NativeNotesVideoPlayback>(renderer,gpu::NotesVideoRoute{route.owner,route.message,route.generation});
            mediaBroker=std::make_unique<gpu::NativeMediaRequestBroker>(*mediaDecoder,*mediaImages,route,mediaVideos.get());
            notesMediaClient=mediaBroker->attachClient();if(!args.archiveAssets.empty())archiveMediaClient=mediaBroker->attachClient();sharedNotesMedia={mediaBroker.get(),notesMediaClient};
        }
        notes=std::make_unique<endfield::tools::NotesPreview>(static_cast<HWND>(host.hwnd()),rasterizer,args.notesData,*notesAssets,args.visible,args.notesFormatAssets,std::span<const ehud::data::Note>{},sharedNotesMedia);notes->resize(metrics);mediaPicker=std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{static_cast<HWND>(host.hwnd()),mediaPickerMessage,serviceGeneration},gpu::ShelfPickerLabels{"添加图片/视频","添加","添加所选文件"});session.setHitFilter([&](std::string_view,core::Point p){return (!notes||!notes->covers(p))&&(!archive||!archive->covers(p))&&(!shelf||!shelf->covers(p))&&(!clipboard||!clipboard->covers(p))&&(!volume||!volume->covers(p))&&(!eventLog||!eventLog->covers(p))&&(!workMode||!workMode->covers(p))&&(!battery||!battery->covers(p))&&(!settingsUI||!settingsUI->covers(p))&&(!storage||!storage->covers(p))&&(!activity||!activity->covers(p))&&(!reader||!reader->covers(p))&&(!calendar||!calendar->covers(p))&&(!map||!map->covers(p))&&(!game||!game->covers(p));},0);startup.mark("isolated-notes-owner");}
    if(!args.shelfAssets.empty()){
        const auto bytes=ehud::data::detail::readFile(args.shelfMask,128*1024);need(bytes.has_value(),"Missing original Shelf reveal samples");
        auto masks=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),core::SubsectionMaskSampler::assetSHA256);
        shelfAssets=std::make_unique<gpu::NativeShelfAssets>(args.shelfAssets);
        shelf=std::make_unique<endfield::tools::ShelfPreview>(static_cast<HWND>(host.hwnd()),rasterizer,*shelfAssets,endfield::tools::ShelfPreviewOptions{args.shelfData,args.visible,args.visible,std::move(masks)});shelf->resize(metrics);startup.mark("isolated-shelf-owner");
    }
    materials.upload(renderer.sourceGraphics());publishNative();host.setCursor(cursor.handle());ready=true;startup.mark("initial-gpu-upload");
    environment.viewport={metrics.width,metrics.height};const auto start=args.visible?now():args.benchmarkEpoch;session.setEnvironment(environment,start);
    if(args.nativeClipboard||args.nativeActivity||args.readerModule||args.calendarModule||!args.mapGeography.empty()||args.eventLogFixture||!args.settingsAssets.empty()||!args.archiveAssets.empty()||!args.storageAssets.empty()){
        const auto window=static_cast<HWND>(host.hwnd());utility=std::make_unique<app::UtilityExecutor>([window]{need(PostMessageW(window,utilityMessage,serviceGeneration,0)!=FALSE,"Post utility completion");});
    }
    if(!args.mapGeography.empty()){
        need(bool(utility),"Map requires the shared utility executor");
        mapStore=std::make_unique<ehud::data::MapStore>(args.notesData);
        mapLoadRoute=utility->makeRoute();
        const auto window=static_cast<HWND>(host.hwnd());
        const auto changed=[&,window]{if(ready&&!stopping&&!mapRefreshQueued){need(PostMessageW(window,mapChangedMessage,serviceGeneration,0)!=FALSE,"Post Map revision");mapRefreshQueued=true;}};
        auto painter=std::make_shared<gpu::NativeMapPainter>();
        endfield::tools::MapPreviewOptions options;options.initial=mapStore->value();
        options.persistence.commit=[&](const auto&snapshot){mapStore->replace(snapshot);};
        options.persistence.newID=[]{return ehud::data::makeUUID();};
        options.persistence.foundationNow=[]{return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()-978307200.;};
        options.clock=[&]{return args.visible?now():mapTime;};options.changed=changed;
        // Source Event Log records only explicit marker-style/reset actions;
        // coordinates, pointer movement and navigation never become history.
        options.pinStyleChanged=[&](endfield::modules::MapPinStyle style){if(eventOwner){const char*name=style==endfield::modules::MapPinStyle::yellow?"yellow":style==endfield::modules::MapPinStyle::green?"green":"player";eventOwner->record({ehud::data::makeUUID(),endfield::modules::EventKind::mapPinStyleChanged,std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()-978307200.,{{"style",name}}});}};
        options.recentered=[&]{if(eventOwner)eventOwner->record({ehud::data::makeUUID(),endfield::modules::EventKind::mapRecentered,std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()-978307200.,{}});};
        options.paint=[painter](const auto&request,const auto&cancel){return painter->paint(request,cancel);};options.releaseWorkerCaches=[painter]{painter->clear();};
        options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;
        options.player=gpu::loadMapPlayerImages(args.mapPlayerAssets,{"3903dcef9be0a32e24b7d6e5ff06235f107df7ae56b2c0d29351b1f20facc83a","ca04f142185c7de40acd8523bdb563195d90a1d1"});
        map=std::make_unique<endfield::tools::MapPreview>(rasterizer,*utility,std::move(options));map->resize(metrics);
        // Decode the immutable bundled geography once on first selection. A
        // full queue retries on its next completion, never from an idle timer.
        requestMapGeography=[&,root=args.mapGeography]{
            if(mapLoadRequested||stopping)return;
            auto result=std::make_shared<endfield::modules::MapGeography>();
            mapLoadRequested=utility->submit(mapLoadRoute,[root,result]{
                const auto read=[&](const char*name,std::size_t bound,const char*pin){const auto bytes=ehud::data::detail::readFile(root/name,bound);need(bytes.has_value(),"Original Map geography is unavailable");need(packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()))==pin,"Original Map geography pin differs");return *bytes;};
                const auto terrain=read("Terrain.bin",endfield::modules::MapTerrain::maximumBytes,"5356727bab11c5f96d69a94658d4f884a2e56b01e89f837670d57660e66635dd");
                const auto countries=read("Countries.bin",endfield::modules::MapCountries::maximumBytes,"557e4e92f0fedb7e3f80e067cb0207655c9ecaedd9f5b29d730aa363842d0dd5");
                result->terrain=endfield::modules::MapTerrain::decode(std::span(reinterpret_cast<const std::uint8_t*>(terrain.data()),terrain.size()));
                result->countries=endfield::modules::MapCountries::decode(std::span(reinterpret_cast<const std::uint8_t*>(countries.data()),countries.size()));
            },[&,result,changed](std::exception_ptr error){if(stopping||!map)return;if(error)std::rethrow_exception(error);map->setGeography(result,args.visible?now():mapTime);changed();});
        };
        startup.mark("lazy-map-owner");
    }
    if(args.readerModule){
        const auto window=static_cast<HWND>(host.hwnd());
        const auto changed=[&,window]{if(ready&&!stopping&&!readerRefreshQueued){need(PostMessageW(window,readerChangedMessage,serviceGeneration,0)!=FALSE,"Post Reader revision");readerRefreshQueued=true;}};
        readerOwner=std::make_unique<gpu::ReaderOwner>(args.notesData/"Reader",*utility,rasterizer.retainedFontResources(),
            [&,changed](std::uint64_t generation,std::optional<endfield::modules::ReaderBook>book,std::exception_ptr error){
                if(stopping||!reader||!reader->importRequestCurrent(generation))return;const auto time=args.visible?now():readerTime;
                if(error||!book)reader->receiveImportError(generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                else reader->receiveImport(generation,std::move(*book),time);changed();
            },changed);
        endfield::tools::ReaderPreviewOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.newID=[] {return ehud::data::makeUUID();};
        options.strings=readerStrings(watchAppearance.language);options.fontFamilies=[&]{return rasterizer.installedFontFamilies();};
        options.shelfChoices=[&]{std::vector<endfield::modules::ReaderMenuChoice>choices;if(shelf)for(const auto&item:shelf->state().items()){
            if(!item.isDirectory&&!item.availabilityError&&endfield::modules::readerSupportedExtension(utf8(fs::u8path(item.lastKnownPath).extension())))choices.push_back({item.id,item.name});
        }return choices;};
        reader=std::make_unique<endfield::tools::ReaderPreview>(readerOwner->state(),rasterizer,std::move(options));reader->resize(metrics);
        startup.mark("lazy-reader-owner");
    }
    if(args.calendarModule){
        const auto window=static_cast<HWND>(host.hwnd());calendarRoute=utility->makeRoute();calendarTime=start;
        calendarCivil=args.visible?std::make_unique<gpu::CalendarCivilContext>():std::make_unique<gpu::CalendarCivilContext>(u"UTC","en_US","en_US");
        const auto text=gpu::nativeCalendarTextRules();calendarRepository=std::make_shared<endfield::modules::CalendarJSONRepository>(args.notesData/"Calendar",text);
        calendarFixture=std::make_shared<CalendarFixtureNotifications>();if(!args.visible)calendarFixture->permission=endfield::modules::CalendarPermission::authorized;
        gpu::CalendarNotificationOptions notifications;notifications.language=watchAppearance.language;
        calendarNotifications=std::make_unique<gpu::NativeCalendarNotifications>(*utility,std::move(notifications),[fixture=calendarFixture](const auto&){return std::make_unique<CalendarFixtureProvider>(fixture);});
        endfield::modules::CalendarExecutor executor{[&](auto work,auto complete){return utility->submit(calendarRoute,std::move(work),std::move(complete));}};
        endfield::modules::CalendarStateOptions state;state.text=text;state.now=[&]{return args.visible?ehud::data::foundationNow():*endfield::modules::calendarUTC().timestamp({2026,10,4},10,0)+calendarTime;};
        state.zone=[&]{return calendarCivil->timeZone();};state.newID=[]{return ehud::data::makeUUID();};state.scheduling=calendarNotifications->scheduling();
        state.changed=[&,window]{if(ready&&!stopping&&!calendarRefreshQueued){need(PostMessageW(window,calendarChangedMessage,serviceGeneration,0)!=FALSE,"Post Calendar revision");calendarRefreshQueued=true;}};
        state.event=[&](std::string_view action){if(eventOwner)eventOwner->record({ehud::data::makeUUID(),endfield::modules::EventKind::calendarAction,ehud::data::foundationNow(),{{"action",std::string(action)}}});};
        calendarState=std::make_unique<endfield::modules::CalendarState>(calendarRepository,std::move(executor),std::move(state));
        endfield::tools::CalendarPreviewOptions options;options.text=text;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;
        calendar=std::make_unique<endfield::tools::CalendarPreview>(window,*calendarState,*calendarCivil,rasterizer,std::move(options),notes->activatedTextManager(),notes->textClient());calendar->resize(metrics);calendar->focus(focused,start);calendarState->startIfExisting();startup.mark("isolated-calendar-owner");
    }
    if(args.nativeClipboard){
        // Explicit development opt-in only. Hidden tests never construct this
        // service, and startup does not read the user's existing clipboard.
        const auto window=static_cast<HWND>(host.hwnd());systemServices=std::make_unique<gpu::SystemServices>();
        const auto status=systemServices->start(window,systemServiceMessage,[&,window](gpu::ServiceChange event){
            if(event!=gpu::ServiceChange::clipboard||stopping)return;clipboardDirty=true;
            if(ready&&!clipboardRefreshQueued&&session.phase()!=core::VisibilityPhase::concealed&&notes&&notes->selected()==core::Module::clipboard){
                need(PostMessageW(window,clipboardChangedMessage,serviceGeneration,0)!=FALSE,"Post Clipboard revision");clipboardRefreshQueued=true;
            }
        },false,utility.get());
        need(SUCCEEDED(status),"Cannot start explicit Windows service preview");
        clipboardProvider=std::make_unique<gpu::NativeClipboardProvider>(gpu::clipboardProviderSource(*systemServices,[&](std::int32_t status)->std::optional<std::string>{
            if(status>=0)return {};return core::localized("Clipboard unavailable","剪贴板不可用",watchAppearance.language);
        }));
    }
    if(!args.clipboardAssets.empty()){
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
        clipboard=std::make_unique<endfield::tools::ClipboardPreview>(rasterizer,ro,std::move(actions),gpu::ClipboardStrings{},gpu::ClipboardAppearance{},clipboardAssets->images(),clipboardAssets->revealSamples());clipboard->resize(metrics);
    }
    if(!args.archiveAssets.empty()){
        const auto window=static_cast<HWND>(host.hwnd());
        const auto changed=[&,window]{if(ready&&!stopping&&!archiveRefreshQueued){need(PostMessageW(window,archiveChangedMessage,serviceGeneration,0)!=FALSE,"Post Archive revision");archiveRefreshQueued=true;}};
        archiveService=std::make_unique<app::ArchiveService>(args.notesData/"Archive",*utility,gpu::nativeArchiveTextRules(),changed);
        archiveDates=std::make_shared<gpu::ArchiveDateFormatter>();
        const endfield::modules::ArchiveAppearance appearance{true,2,{250./255.,212./255.,31./255.,1},endfield::modules::ArchiveColor{1,159./255.,10./255.,1}};
        auto options=endfield::tools::makeArchivePreviewOptions(args.archiveAssets,rasterizer,core::Language::simplifiedChinese,appearance,archiveDates);options.raster.memoryImages=&archiveImages;
        archive=std::make_unique<endfield::tools::ArchivePreview>(archiveService->state(),window,rasterizer,notes->activatedTextManager(),notes->textClient(),std::move(options));archive->resize(metrics);archive->focus(focused,start);
        endfield::tools::ArchiveMediaBindingOptions media;
        media.resolve=[](const Json&ref){need(ref["referencePlatform"]==Json("windows"),"Imported Mac media needs explicit relinking on Windows");const auto&path=ref["windowsPath"].string();need(ehud::data::validWindowsFilePath(path),"Invalid Archive Windows reference");return gpu::NotesImageAccess{fs::u8path(path),{}};};
        media.errorText=[&](HRESULT){return core::localized("Media unavailable","媒体不可用",watchAppearance.language);};
        media.loadThumbnail=[&](std::string id,auto completion){archiveService->state().loadThumbnail(std::move(id),std::move(completion));};media.changed=changed;
        archiveMedia=std::make_unique<endfield::tools::ArchiveMediaBinding>(*archive,*mediaBroker,archiveMediaClient,renderer,archiveImages,std::move(media));
        startup.mark("shared-media-archive-owner");
    }
    if(!args.storageAssets.empty()){
        const auto window=static_cast<HWND>(host.hwnd());
        auto options=endfield::tools::makeStoragePreviewOptions(args.storageAssets,*utility,[window]{
            // Explicit original Storage Settings action only; never startup or a test.
            const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",L"ms-settings:storagesense",nullptr,nullptr,SW_SHOWNORMAL));
            if(result<=32)std::cerr<<"Windows Storage Settings could not open: "<<result<<'\n';
        });
        if(args.moduleCoverage){
            // Replace ALL OS providers before constructing the owner. The
            // common factory only reads pinned application artwork at startup.
            storageFixture=std::make_shared<StorageFixture>();storageFixture->time=args.benchmarkEpoch;const auto fixture=storageFixture;
            options.readCapacity=[fixture]()->std::optional<endfield::modules::StorageCapacity>{++fixture->capacityReads;return endfield::modules::StorageCapacity{"Synthetic startup volume",1000000000000LL,375000000000LL,fixture->time.load(std::memory_order_relaxed)};};
            options.scanDetails=[fixture](const endfield::modules::StorageScanCancellation&cancel){++fixture->detailScans;endfield::modules::StorageDetailsSnapshot result;if(!cancel.cancelled()){result.categories={{"documents","Synthetic Documents",123000000LL,false}};result.updatedAt=fixture->time.load(std::memory_order_relaxed);}return result;};
            options.completionClock=[fixture]{return fixture->time.load(std::memory_order_relaxed);};
            options.openSettings=[fixture]{++fixture->settingsRequests;};
        }
        storage=std::make_unique<endfield::tools::StoragePreview>(rasterizer,std::move(options));storage->resize(metrics);
        startup.mark(args.moduleCoverage?"synthetic-storage-owner":"development-storage-capacity-owner");
    }
    if(!args.activityAssets.empty()){
        // Hidden coverage and ordinary isolated preview keep injected data.
        // OS reads are confined to an explicitly visible native opt-in.
        gpu::NativeActivityAssets assets(args.activityAssets);
        endfield::tools::ActivityPreviewOptions options;options.sortSamples=assets.sortSamples();
        options.appearance.language=core::Language::simplifiedChinese;
        options.compareNames=[](std::string_view a,std::string_view b){return a.compare(b);};
        options.initial.timestamp=1;options.initial.uptime=1;options.initial.cpuPercent=12;
        options.initial.memory=endfield::modules::ActivityMemory{4000000000ULL,16000000000ULL,{}};
        options.initial.download=400000;options.initial.upload=12000;
        options.initial.diskRead=150000;options.initial.diskWrite=34000;
        for(unsigned n=0;n<18;++n){endfield::modules::ActivityApp item;
            item.identity={"synthetic-"+std::to_string(n),"Synthetic App "+std::to_string(n+1),"",{100+n}};
            item.cpuPercent=double(18-n);item.memoryBytes=200000000ULL+n*10000000ULL;
            options.apps.items.push_back(std::move(item));}
        if(args.nativeActivity){
            options.initial={};options.apps={};options.compareNames=gpu::compareWindowsActivityNames;
            options.demandChanged=[&](bool visible,bool apps,double time){
                if(stopping)return;const bool wasApps=activityPlan.appsActive();activityPlan.setVisible(visible,false,apps,time);
                if(activityCatalog&&wasApps!=activityPlan.appsActive())activityCatalog->invalidate();
                if(activityProbe)activityProbe->update(time);scheduleDeadline();
            };
        }
        activity=std::make_unique<endfield::tools::ActivityPreview>(rasterizer,std::move(options));activity->resize(metrics);
        if(args.nativeActivity){
            activityCatalog=std::make_shared<gpu::ActivityCatalogSampler>(gpu::windowsActivityCatalogReaders());
            activityDisk=std::make_shared<gpu::WindowsActivityDisk>();
            const auto sampleClock=[]{const auto stamp=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()-978307200.;return std::pair{stamp,double(GetTickCount64())*.001};};
            gpu::ActivityProbe::Readers readers;
            readers.system=[disk=activityDisk,sampleClock]{const auto [stamp,uptime]=sampleClock();auto result=gpu::readWindowsActivity(stamp,uptime);result.disk=disk->sample();return result;};
            readers.apps=[catalog=activityCatalog,sampleClock]{const auto [stamp,uptime]=sampleClock();return catalog->sample(stamp,uptime);};
            activityProbe=std::make_unique<gpu::ActivityProbe>(activity->providerState(),activityPlan,*utility,std::move(readers));
        }
        startup.mark(args.nativeActivity?"native-activity-owner":"synthetic-activity-owner");
    }
    if(args.eventLogFixture){
        const auto window=static_cast<HWND>(host.hwnd());
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
        battery=std::make_unique<endfield::tools::BatteryPreview>(rasterizer,options,value,appearance,[&]{selectModule(core::Module::display,now());});
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
    if(!args.settingsAssets.empty()){
        // Only this explicitly NEW preview data root is opened. The real app's
        // preferences, startup registration and user icons are never touched.
        const auto createSettings=[&](const ehud::data::Settings&loaded){
            if(stopping)return;configuration=loaded;configuration.set("launchAtLogin",false);configurationPending=true;
            if(!args.orbipomAssets.empty()){
                const auto&saved=loaded.fields["orbipom.bestScore.v1"];if(saved.isNumber())gameBest=std::max<std::int64_t>(0,saved.integer());
                // First selection prepares original art; only Start creates the VM.
                ensureGame=[&](double time){if(game||stopping)return;
                endfield::modules::OrbiPomSessionCallbacks callbacks;
                callbacks.makeRuntime=[root=args.orbipomAssets]{return std::make_unique<endfield::modules::OrbiPomRuntime>(endfield::modules::OrbiPomRuntimeOptions{root});};
                callbacks.saveBest=[&](std::int64_t best){gameBest=std::max(gameBest,best);configuration.set("orbipom.bestScore.v1",gameBest);if(settingsSaves){auto committed=settingsUI?settingsUI->controller().committed():configuration;committed.set("orbipom.bestScore.v1",gameBest);settingsSaves->save(committed);}};
                callbacks.event=[&](endfield::modules::OrbiPomEvent event){if(eventOwner){const char*action=event==endfield::modules::OrbiPomEvent::started?"started":event==endfield::modules::OrbiPomEvent::restarted?"restarted":"finished";eventOwner->record({ehud::data::makeUUID(),endfield::modules::EventKind::minigameAction,ehud::data::foundationNow(),{{"action",action}}});}};
                gameSession=std::make_unique<endfield::modules::OrbiPomSession>(gameBest,std::move(callbacks));
                endfield::tools::OrbiPomPreviewOptions gameOptions;gameOptions.raster.assetRoot=args.orbipomAssets;gameOptions.raster.pixelsPerPoint=2;gameOptions.raster.paddingPoints=1;
                game=std::make_unique<endfield::tools::OrbiPomPreview>(*gameSession,rasterizer,std::move(gameOptions));game->resize(metrics);game->setForeground(focused,time);game->setOverlayVisible(session.phase()!=core::VisibilityPhase::concealed,time);configurationPending=true;
                };
            }
            endfield::tools::SettingsPreviewOptions options;options.initial=configuration;options.language=settingsLanguage(configuration);
            const auto iconRoot=args.settingsAssets/"application-icons";const auto catalog=loadJSON(iconRoot/"manifest.json");
            need(catalog["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Settings icon source differs");
            for(const auto&icon:catalog["icons"].array())if(icon["offered"].boolean()){options.icons.push_back({icon["id"].string(),icon["title"].string()});options.images.emplace(icon["id"].string(),Json::Object{{"asset",icon["app"]["file"]},{"sha256",icon["app"]["sha256"]}});}
            options.raster.assetRoot=iconRoot;
            options.viewHooks.displays=[](){std::vector<endfield::modules::SettingsDisplay>result;for(const auto&d:gpu::readConnectedDisplays()){const auto id=narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.persistentID.data()),d.persistentID.size()));const auto name=narrow(std::wstring_view(reinterpret_cast<const wchar_t*>(d.name.data()),d.name.size()));if(!id.empty())result.push_back({id,name,std::to_string(d.bounds.right-d.bounds.left)+" × "+std::to_string(d.bounds.bottom-d.bounds.top)});}return result;};
            options.callbacks.configurationChanged=[&](const auto&value){configuration=value;if(!args.orbipomAssets.empty())configuration.set("orbipom.bestScore.v1",gameBest);configurationPending=true;};
            options.callbacks.persist=[&](const auto&value){auto committed=value;if(!args.orbipomAssets.empty())committed.set("orbipom.bestScore.v1",gameBest);settingsSaves->save(committed);};
            options.callbacks.changed=[&]{if(ready&&!stopping)host.invalidate();};
            options.callbacks.registerShortcut=[&](auto value)->std::optional<std::string>{if(tray&&tray->setHotkey(gpu::TrayHotkey{value.modifiers,value.key}))return {};return core::localized("Shortcut unavailable","快捷键不可用",settingsLanguage(configuration));};
            options.callbacks.shortcutCapture=[&](bool capture){if(!tray)return;const auto key=endfield::modules::settingsShortcut(configuration);if(capture)tray->setHotkey({});else tray->setHotkey(gpu::TrayHotkey{key.modifiers,key.key});};
            options.callbacks.launchAtLogin=[](bool)->std::optional<std::string>{return "Startup registration is unavailable in this isolated preview";};
            options.callbacks.editBatteryPosition=[&]{selectModule(core::Module::power,now());};
            options.about.repository="https://github.com/DDDuoDuo/EndfieldHUD";
            settingsUI=std::make_unique<endfield::tools::SettingsPreview>(rasterizer,std::move(options));settingsUI->resize(metrics);settingsUI->setOverlayVisible(session.phase()!=core::VisibilityPhase::concealed,args.visible?now():args.benchmarkEpoch);
        };
        if(args.moduleCoverage&&!args.orbipomAssets.empty()){
            // This explicit hidden path owns a parser-validated NEW data root.
            // Seed a nonzero original preference to detect accidental reset loss.
            ehud::data::SettingsStore fixture(args.notesData);auto value=fixture.value();value.set("orbipom.bestScore.v1",std::int64_t{71});fixture.update(value);
        }
        settingsSaves=std::make_unique<app::SettingsSaveQueue>(args.notesData,*utility,app::SettingsSaveCallbacks{createSettings,[&]{refresh(now());}});settingsSaves->start();
        // Hidden integration owns no frame/message loop. This explicit test
        // preparation barrier drains the same asynchronous read before input.
        if(!args.visible){utility->waitIdle();utility->drain();need(settingsSaves->status().loaded,"Hidden Settings initial read failed");}
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
        if(!args.settingsAssets.empty())menu.push_back({gpu::TrayAction::settings,L"设置…"});if(workMode)menu.push_back({gpu::TrayAction::workMode,L"工作模式"});menu.push_back({gpu::TrayAction::none,{},false,false,true});menu.push_back({gpu::TrayAction::quit,L"退出 EndfieldHUD"});tray->setMenu(std::move(menu));
        if(!shortcut)std::cout<<"Ctrl + ` is already registered; reopen this isolated preview from its tray icon.\n";
    }
    std::cout<<(archive?"Synthetic module preview with Archive, rich Notes and shared media; all files remain references in the explicit temporary data root. ESC finishes editing, then animates closing.\n":notes?"Synthetic shell with rich Notes, File Shelf and isolated Clipboard history; other modules and remaining Notes tools are not connected yet. ESC finishes editing, then animates closing.\n":"Synthetic source-shell feasibility only: no module bodies/providers; font substitutions and explicit source-content variant coverage remain. ESC animates closing.\n");
    if(activity)std::cout<<(args.nativeActivity?"Native Activity reads bounded Windows counters on the shared worker; per-app disk/network remain unavailable.\n":"Activity preview uses synthetic app and system readings only.\n");
    if(calendar)std::cout<<"Calendar preview keeps events in the explicit temporary root; notification delivery is injected and never contacts the OS notification center.\n";
    if(storage)std::cout<<(args.moduleCoverage?"Storage coverage uses injected synthetic capacity/details and a no-op settings action.\n":"Development Storage preview: startup-volume capacity is read only while selected; Settings opens only on explicit click; no automatic folder scan.\n");
    startup.mark("all-preview-owners-ready");const auto preparedMS=milliseconds(preparation);
    // Initialization must not consume the opening timeline while the HWND is
    // still hidden. Hotkey openings already use their actual event timestamp.
    if(args.visible){const auto openingTime=now();open(openingTime);host.show();refresh(openingTime);host.run();}
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
            std::vector modules{core::Module::notes,core::Module::fileShelf,core::Module::clipboard,core::Module::volume,core::Module::eventLog,core::Module::workMode,core::Module::power};if(archive)modules.push_back(core::Module::archive);if(storage)modules.push_back(core::Module::storage);if(activity)modules.push_back(core::Module::activityMonitor);if(reader)modules.push_back(core::Module::reader);if(calendar)modules.push_back(core::Module::calendar);if(map)modules.push_back(core::Module::map);if(!args.orbipomAssets.empty())modules.push_back(core::Module::minigame);if(settingsUI)modules.insert(modules.end(),{core::Module::system,core::Module::display,core::Module::hotkeys,core::Module::about});
            std::optional<gpu::LayerRasterStats> retainedCycle;
            for(unsigned cycle=0;cycle<3;++cycle)for(const auto module:modules){
                std::size_t scrollChanges{},peakEntries{},peakBytes{};
                try{selectModule(module,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);
                    if(module==core::Module::minigame&&game){
                        need(gameSession&&game->state().active(),"Minigame shares the selected original module surface");
                        if(cycle==0){
                            need(!gameSession->hasRuntime(),"Opening original Minigame artwork alone does not start physics");
                            need(game->perform(endfield::modules::OrbiPomAction::start,time+.71),"Original Start action starts the single lazy runtime");
                            need(gameSession->hasRuntime()&&gameSession->snapshot().isPlaying()&&!gameSession->error(),"Bundled unchanged game engine starts through the module owner");
                            need(gameSession->bestScore()==71&&gameSession->snapshot().highScore==71,"Original best-score key loads from the isolated shared preferences");
                            const auto began=gameSession->snapshot().simulationTime;
                            for(unsigned n=0;n<30;++n)present(time+.72+double(n)/60,true);
                            need(gameSession->snapshot().simulationTime>began,"Only shared presented HUD frames advance original physics");
                            need(game->perform(endfield::modules::OrbiPomAction::pause,time+1.3),"Original pause control is available");
                            const auto stopped=gameSession->snapshot().simulationTime;
                            present(time+1.5,true);present(time+1.7,true);
                            need(gameSession->manuallyPaused()&&gameSession->snapshot().simulationTime==stopped,"Manual pause advances no game steps");
                            settingsUI->controller().restoreDefaults();need(settingsSaves->flush(),"Isolated settings reset saves through the shared executor");
                            const ehud::data::SettingsStore verified(args.notesData);
                            need(verified.value().fields["orbipom.bestScore.v1"].integer()==71&&gameSession->bestScore()==71,"Settings reset preserves saved game progress and the live session");
                        }else need(gameSession->manuallyPaused(),"Returning to Minigame preserves manual pause");
                    }
                    if(module==core::Module::map&&map){
                        const auto drainMap=[&]{for(unsigned n=0;n<24;++n){utility->waitIdle();utility->drain();map->utilityCompleted(mapTime);if(const auto due=map->nextWakeTime()){mapTime=std::max(mapTime,*due)+.000001;map->deadline(mapTime);}present(mapTime,true);if(!utility->stats().running&&!utility->stats().pending&&!utility->stats().completed&&!map->nextWakeTime())break;}};
                        mapTime=time+.7;drainMap();need(map->state().active()&&map->rasterStats().published>0,"Original bundled Map geography renders only after isolated selection");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,440,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,module,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                        const auto point=[&](core::Point p){const auto screen=plane.project(p);need(bool(screen),"Map input projects through the source440-point plane");return core::Point{screen->x/metrics.scale,screen->y/metrics.scale};};
                        const auto center=point({220,220});const auto initialCount=map->state().pins().size();
                        need(map->pointer({app::PointerKind::down,app::PointerButton::right,center.x,center.y},mapTime),"Original right-click adds a projected Map marker");map->pointer({app::PointerKind::up,app::PointerButton::right,center.x,center.y},mapTime);present(mapTime+.17,true);
                        need(map->state().pins().size()==initialCount+1&&mapStore->value().pins.size()==initialCount+1,"Marker writes only the explicitly isolated Map store");
                        need(map->pointer({app::PointerKind::down,app::PointerButton::right,center.x,center.y},mapTime),"Original marker right-click is consumed");present(mapTime+.17,true);need(map->state().pins().size()==initialCount,"Second right-click removes that marker");
                        need(map->wheel({center.x,center.y,1,false,0,3},mapTime),"Map wheel reaches the native continuous camera owner");mapTime+=.181;map->deadline(mapTime);drainMap();need(map->state().viewport().zoom>3,"Wheel zoom preserves source sensitivity");
                        need(map->perform(endfield::modules::MapAction::reset,{},mapTime),"Original reset action is available");drainMap();need(map->state().viewport().zoom==3,"Map reset preserves the authoritative3x default");
                        const auto before=rasterizer.stats();const auto accepted=utility->stats().accepted;const auto images=renderer.stats().textureUploads;const auto poseStart=mapTime;
                        for(unsigned n=0;n<12;++n)present(poseStart+.3+double(n)/60,true);
                        need(before.rasterizations==rasterizer.stats().rasterizations&&accepted==utility->stats().accepted&&images==renderer.stats().textureUploads,"Settled Map poses reuse geography, text and GPU textures");
                    }
                    if(module==core::Module::reader&&reader){
                        const auto drainReader=[&]{for(unsigned n=0;n<8;++n){utility->waitIdle();utility->drain();readerOwner->queueCapacityAvailable();}present(readerTime,true);};
                        readerTime=time+.7;drainReader();need(readerOwner->state().active()&&readerOwner->state().loaded(),"Reader loads only its isolated library on selection");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,module,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                        const auto click=[&](core::Point local,double t){const auto p=plane.project(local);need(p.has_value(),"Reader action projects through source module plane");readerTime=t;need(reader->pointer({app::PointerKind::down,app::PointerButton::left,p->x/metrics.scale,p->y/metrics.scale},t),"Reader consumes its original projected control");reader->pointer({app::PointerKind::up,app::PointerButton::left,p->x/metrics.scale,p->y/metrics.scale},t+.001);present(t+.001,true);};
                        if(cycle<2){
                            // The only file opened in this hidden path is generated here.
                            const auto fixture=args.notesData/(cycle?"Reader fixture.pdf":"Reader fixture.txt");std::string text;
                            if(cycle)text=ownedReaderPDF();else for(unsigned n=0;n<500;++n)text+="EndfieldHUD 阅读器 · Temporary isolated sample " +std::to_string(n)+".\n";
                            ehud::data::detail::replaceFile(fixture,std::nullopt,text,128*1024);
                            click({40,20},time+.72);present(time+.93,true);click({30,60},time+.94);auto action=reader->takeImportAction();
                            need(action&&action->kind==endfield::tools::ReaderImportAction::Kind::chooseLocal,"Reader Open queues the existing native-picker route without opening a dialog in hidden coverage");
                            importReaderReference(action->generation,utf8(fixture),{},time+.95);readerTime=time+.96;drainReader();
                        }
                        need(readerOwner->state().book()&&readerOwner->state().current()&&!readerOwner->state().error(),"Real TXT/PDF rendering publishes only the owned generated document");
                        need(readerOwner->state().current()->illustration==(cycle!=0),"Shared Reader provider replaces TXT with the generated PDF and retains it across module switches");
                        need(readerOwner->state().library().preferences.fontName=="System","Fresh Reader follows shared SC/KR defaults without overwriting saved explicit fonts");
                        click({268,20},time+1.02);readerTime=time+1.03;drainReader();
                        const auto marks=readerOwner->state().book()->bookmarks.size();need(marks==(cycle==2?0u:1u),"Original bookmark control persists through the shared Reader owner");
                        present(time+1.5,true);const auto before=rasterizer.stats();const auto accepted=utility->stats().accepted;
                        for(unsigned frame=0;frame<12;++frame)present(time+1.6+double(frame)/60,true);
                        const auto after=rasterizer.stats();need(before.rasterizations==after.rasterizations&&before.textLayoutsCreated==after.textLayoutsCreated&&utility->stats().accepted==accepted,"Settled Reader frames reuse page pixels and never enqueue provider work");
                    }
                    if(module==core::Module::activityMonitor&&activity){
                        need(activity->state().appSnapshot().items.size()==18,"Activity coverage retains only its synthetic app catalog");
                        need(activity->perform("activity:apps",time+.7),"Original Activity Apps action is routed");present(time+1,true);
                        need(activity->state().showingApps(),"Activity Apps selection keeps the source transition");
                        need(activity->perform("activity:sort:memory",time+1.01),"Original Activity RAM sort is routed");present(time+1.3,true);
                        need(activity->state().sortKey()==endfield::modules::ActivitySort::memory,"Activity RAM sort reaches shared state");
                        const auto retained=activity->stats();for(unsigned frame=0;frame<12;++frame)present(time+1.35+double(frame)/60,true);
                        const auto after=activity->stats();need(after.builds==retained.builds&&after.graphRasters==retained.graphRasters&&after.maskRasters==retained.maskRasters,"Settled Activity frames retain text, graph and sort resources");
                        need(activity->perform("activity:overview",time+1.55),"Original overview action is routed");present(time+1.9,true);
                    }
                    if(module==core::Module::storage&&storage){
                        need(storageFixture!=nullptr,"Hidden Storage must never retain OS capacity/scanner callbacks");
                        utility->waitIdle();utility->drain();storage->utilityCompleted(time+.7);present(time+.7,true);
                        need(storage->state().active()&&storage->state().snapshot().capacity&&storage->state().snapshot().capacity->volumeName=="Synthetic startup volume"&&!storage->state().snapshot().isLoading,"Storage activation publishes only the injected capacity snapshot");
                        need(storageFixture->detailScans.load()==(cycle?1u:0u),"Storage activation and frames never launch a folder scan");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,module,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                        const auto click=[&](core::Point local,double t){const auto projected=plane.project(local);need(projected.has_value(),"Storage action projects through its actual shared source plane");const auto x=projected->x/metrics.scale,y=projected->y/metrics.scale;need(storage->pointer({app::PointerKind::down,app::PointerButton::left,x,y},t),"Storage consumes its original projected control");storage->pointer({app::PointerKind::up,app::PointerButton::left,x,y},t);present(t,true);};
                        const auto settingsBefore=storageFixture->settingsRequests;click({95,266.5},time+.72);need(storageFixture->settingsRequests==settingsBefore+1,"Original Storage Settings action reaches only the injected hidden callback");
                        const auto reads=storageFixture->capacityReads.load();click({370,266.5},time+.74);utility->waitIdle();utility->drain();storage->utilityCompleted(time+.8);present(time+.8,true);need(storageFixture->capacityReads.load()==reads+1,"Manual refresh uses the shared executor once");
                        if(!cycle){need(storage->requestDetails(false,time+.82),"Explicit isolated model probe can request details without inventing a UI button");utility->waitIdle();utility->drain();storage->utilityCompleted(time+.84);need(storageFixture->detailScans.load()==1&&!storage->state().details().isLoading&&storage->state().details().categories.size()==1,"Explicit details request uses only its synthetic bounded callback");}
                        need(storage->nextWakeTime().has_value(),"Visible Storage shares the original 60-second deadline");storage->setVisible(false,time+.9);need(!storage->nextWakeTime()&&!storage->state().active(),"Hidden Storage removes its capacity deadline");
                        const auto hiddenReads=storageFixture->capacityReads.load();storage->deadline(time+.92);need(storageFixture->capacityReads.load()==hiddenReads,"Hidden deadline dispatch never queries a provider");storage->setVisible(true,time+.94);present(time+.94,true);
                        const auto cached=storageFixture->capacityReads.load();for(unsigned frame=0;frame<20;++frame)present(time+1+double(frame)/60,true);need(storageFixture->capacityReads.load()==cached,"Warm Storage frames do not poll capacity");
                    }
                    if(module==core::Module::calendar&&calendar){
                        // The hidden path owns a NEW parser-validated root and
                        // synthetic notifications. All repository work and
                        // scheduling run through the existing app FIFO.
                        need(flushCalendar(),"Calendar initial load settles through the shared executor");present(time+.7,true);
                        need(calendarState->loaded()&&calendarState->active()&&!calendarState->error(),"Calendar activates its isolated lazy repository");
                        if(!cycle){
                            gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                            const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,module,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                            const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                            const auto click=[&](core::Point local,double t){const auto q=plane.project(local);need(bool(q),"Calendar uses the same drawn source module plane");const auto x=q->x/metrics.scale,y=q->y/metrics.scale;need(calendar->pointer({app::PointerKind::down,app::PointerButton::left,x,y},t),"Calendar consumes its source action");calendar->pointer({app::PointerKind::up,app::PointerButton::left,x,y},t);present(t,true);};
                            click({373,289},time+.72);present(time+.94,true);need(calendar->editing(),"Calendar opens its three real projected fields");
                            need(calendar->key({app::KeyKind::unicodeCharacter,'C'},time+.96,endfield::tools::CalendarKeyModifiers{}),"Calendar title receives synthetic Unicode through the shared editor");
                            click({342,357},time+1.0);need(flushCalendar(),"Calendar durable event and reminder receipts settle on the shared FIFO");present(time+1.22,true);
                            need(calendarState->events().size()==1&&calendarState->events()[0].title=="C"&&!calendar->editing(),"Calendar saves its generated draft and closes only after persistence success");
                            need(fs::is_regular_file(args.notesData/"Calendar"/"calendar.json")&&!calendarFixture->pending.empty(),"Calendar writes only the explicit temporary store and synthetic reminder plan");
                        }
                        present(time+1.4,true);const auto beforeRaster=rasterizer.stats();const auto beforeGPU=renderer.stats();const auto accepted=utility->stats().accepted;
                        for(unsigned n=0;n<12;++n)present(time+1.41+double(n)/60,true);
                        need(rasterizer.stats().rasterizations==beforeRaster.rasterizations&&rasterizer.stats().textLayoutsCreated==beforeRaster.textLayoutsCreated&&renderer.stats().textureUploads==beforeGPU.textureUploads&&utility->stats().accepted==accepted,"Settled Calendar frames reuse resources and schedule no reminder or file work");
                        need(!calendar->requiresFrames(time+1.7),"Calendar's source transitions become idle on the shared frame clock");
                    }
                    if(module==core::Module::archive&&archive){
                        // Hidden-only barrier for the real shared async database.
                        // No native service/IME/file picker is activated by it.
                        utility->waitIdle();utility->drain();archiveService->queueCapacityAvailable();present(time+.7,true);
                        need(archiveService->state().active()&&!archiveService->state().error(),"Archive source activation loads its isolated database");
                        if(!cycle){
                            gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                            const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,module,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                            const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                            const auto click=[&](core::Point local,double t){const auto projected=plane.project(local);need(projected.has_value(),"Archive source input projects through the shared module plane");const auto x=projected->x/metrics.scale,y=projected->y/metrics.scale;need(archive->pointer({app::PointerKind::down,app::PointerButton::left,x,y},t),"Archive consumes its source action");archive->pointer({app::PointerKind::up,app::PointerButton::left,x,y},t);present(t,true);};
                            click({29,316},time+.72); // Original '+' → category menu.
                            const auto origin=endfield::modules::archiveCategoryMenuOrigin(false);click({origin.x+32,origin.y+57},time+.90);
                            utility->waitIdle();utility->drain();archiveService->queueCapacityAvailable();present(time+1.1,true);
                            need(archiveService->state().selected().has_value(),"Original category chooser creates an Uncategorized document");
                            click({50,64},time+1.12);need(archive->activeField()==std::optional<std::string>("title"),"Archive title uses the existing projected editor");
                            need(archive->key({app::KeyKind::character,'A'},time+1.14),"Archive accepts generated plain fixture text");need(archive->finishEditing(time+1.16),"Hidden Archive field commits without a native text manager");need(archiveService->flush(),"Archive fixture writes finish through the shared utility executor");
                            present(time+1.3,true);need(archiveService->state().selected()->title=="A","Archive persisted state retains generated title text");
                        }
                    }
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
                if(module==modules.back()){
                    if(retainedCycle)need(usage.entries==retainedCycle->entries&&usage.resourceBytes==retainedCycle->resourceBytes,"Repeated combined module cycles retain no extra raster resources");
                    // Reader intentionally adds a second (PDF) book on cycle1.
                    // Compare after that fixed input set has been exercised;
                    // the extra book changes library/control text legitimately.
                    if(!reader||cycle>0)retainedCycle=usage;
                }time+=2;
            }
            close(time);present(time+.7,true);
        }
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"moduleResults",moduleResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog.variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent?nativeContent->stats().updates:nativeAppearance->stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent?nativeContent->stats().surfaceUpdates:nativeAppearance->stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"archiveIncluded",bool(archive)},{"storageIncluded",bool(storage)},{"activityIncluded",bool(activity)},{"readerIncluded",bool(reader)},{"calendarIncluded",bool(calendar)},{"calendarUsesSyntheticNotifications",bool(calendarFixture)},{"mapIncluded",bool(map)},{"minigameIncluded",bool(game)},{"storageUsesSyntheticProviders",bool(storageFixture)},{"storageCapacityReads",storageFixture?int(storageFixture->capacityReads.load()):0},{"storageDetailScans",storageFixture?int(storageFixture->detailScans.load()):0},{"storageSettingsRequests",storageFixture?int(storageFixture->settingsRequests):0},{"appSharedMediaProvider",bool(mediaBroker)},{"limitations",Json::Array{args.moduleCoverage?"Module owners use synthetic data and hidden generated input; real clipboard/audio providers and desktop backdrop are excluded":"No module bodies, providers, persistence, user input or screen capture; hidden offscreen benchmark excludes the system backdrop","Visible preview uses the native system backdrop with original source fade/default opacity; exact blur/radial appearance is unverified","Fixed synthetic clock strings; original canvas fade does not extend source clip completion","Native caption/icon variants are limited to exact exported action-slot pairs; missing variants reject instead of fabricating artwork","CPU timings include submission/driver stalls; no GPU completion timestamp or frame-rate claim","Forced stable-idle draws are benchmark samples; the actual host schedules no ambient-off idle frames"}}};
        ehud::data::detail::replaceFile(args.report,std::nullopt,report.encode(),8*1024*1024);std::cout<<"Wrote hidden source-shell benchmark report\n";
    }
    if(!args.liveDiagnostics.empty()){
        if(probe.enabled){probe.flush(now());probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);}
        Json report=Json::Object{{"scope","Isolated synthetic visible HUD, actual text service, inclusive CPU stage durations; no document text or desktop capture"},{"phases","0 opening/idle; 1 focused editor; 2 focused editor with controlled tilt; 3 after editing"},{"samples",probe.rows}};
        ehud::data::detail::replaceFile(args.liveDiagnostics,std::nullopt,report.encode(),1024*1024);
    }
    ready=false;stopping=true;tray.reset();if(workMode)workMode->shutdown(now());host.requestStop();if(mediaPicker)mediaPicker->cancel();
    if(archive){need(archive->finishEditing(now()),"Archive text transaction is still active at shutdown");need(archiveService->flush(),"Archive changes could not be saved; draft retained until explicit shutdown failure");}
    if(calendar)need(calendar->dismissMenu(false,now()),"Calendar text transaction is still active at shutdown");need(flushCalendar(),"Calendar changes could not be saved");
    if(readerOwner)need(readerOwner->flush(),"Reader changes could not be saved");
    if(settingsSaves)need(settingsSaves->flush(),"Settings changes could not be saved");if(eventSaves)eventSaves->flush(now());projectionHandoff.cancel();projectionDropRoute(false,now());projection.reset();backdrop.reset();
    if(args.visible)need(backdrop.stats().hostAttributeRestored,"Source preview could not restore its original host-backdrop flag");
    if(readerTrace)readerTrace->save(args.notesData/"reader-scroll-trace.json");cleanup();if(backdropQueue)backdropQueue->finish();if(pendingShelfReveal)need(SUCCEEDED(gpu::revealShelfReference(std::move(*pendingShelfReveal))),"Cannot reveal Shelf reference in Explorer");return 0;
}catch(const winrt::hresult_error&e){std::cerr<<"Source preview failed: HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
catch(const std::exception&e){std::cerr<<"Source preview failed: "<<e.what()<<'\n';return 1;}}
