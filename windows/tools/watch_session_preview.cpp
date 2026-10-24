// Development harness over the production Application owner. Every input is
// an explicit synthetic export/cache or a NEW temporary data root; visible
// launch is opt-in and the benchmark never shows its owned HWND. The command
// line is unchanged: the integration validator runs this executable with the
// exact --module-coverage arguments of verify-modules-122.ps1. Module areas
// registered by the CMake fragments (app/application_areas.hpp) join the
// module owner automatically; hidden module coverage audits each of them, and
// the optional --package <staged resources> lets them resolve the build's
// verified package (application_owner_coverage passes it).
#include "app/application.hpp"
#include "app/application_areas.hpp"
#include "core/resource_locator.hpp"
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winrt/base.h>

namespace fs=std::filesystem;
namespace app=endfield::app;
namespace gpu=endfield::native;
namespace core=endfield::core;
namespace {
void need(bool value,const char*message){if(!value)throw std::runtime_error(message);}
std::string utf8(const fs::path&value){const auto bytes=value.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
struct Options {bool startupTraceCold{};double idleTraceSeconds{30},idleTraceRefresh{165};std::optional<double>idleTraceGrace,idleTraceClosed;fs::path idleTrace,memoryTrace,package,startupTrace,contentionTrace,packet,cache,shader,chrome,cursor,report,snapshots,watchBlur,notesData,notesFormatAssets,shelfData,liveDiagnostics,residentIcons,settingsAssets,mapGeography,mapPlayerAssets,orbipomAssets;std::string pin;bool visible{},warp{},runtimeInput{},coverage{},moduleCoverage{},sessionEndCoverage{},nativeClipboard{},clipboardFixture{},nativeActivity{},activityFixture{},nativeStorage{},storageFixture{},readerScrollTrace{},readerModule{},archiveModule{},calendarModule{},projectionModule{},volumeFixture{},eventLogFixture{},workModeFixture{},batteryFixture{};std::uint32_t benchmarkWidth{1280},benchmarkHeight{800};double benchmarkEpoch{};};
Options options(int argc,wchar_t**argv){
    need(argc>=5,"Usage: watch_session_preview packet-root compiled-scene hud.hlsl --benchmark new-report.json | --visible --watch-blur original-watch-blur.json [--chrome chrome.json] [--cursor-png original.png] [--compiled-sha sha256] [--snapshots new-directory] [--warp] [--runtime-input] [--benchmark-size width height] [--benchmark-epoch seconds] [--coverage] [--storage-fixture | --native-storage (visible)] [--activity-fixture | --native-activity (visible)]");
    Options o;o.packet=fs::absolute(argv[1]);o.cache=fs::absolute(argv[2]);o.shader=fs::absolute(argv[3]);
    for(int i=4;i<argc;++i){const std::wstring_view arg=argv[i];
        if(arg==L"--archive"){need(!o.archiveModule,"Duplicate Archive mode");o.archiveModule=true;}
        else if(arg==L"--activity-fixture"){need(!o.activityFixture,"Duplicate Activity fixture");o.activityFixture=true;}
        else if(arg==L"--storage-fixture"){need(!o.storageFixture,"Duplicate Storage fixture");o.storageFixture=true;}
        else if(arg==L"--native-storage"){need(!o.nativeStorage,"Duplicate native Storage mode");o.nativeStorage=true;}
        else if(arg==L"--settings-assets"&&i+1<argc){need(o.settingsAssets.empty(),"Duplicate Settings assets");o.settingsAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--visible"){need(!o.visible,"Duplicate visible mode");o.visible=true;}
        else if(arg==L"--live-diagnostics"&&i+1<argc){need(o.liveDiagnostics.empty(),"Duplicate live diagnostic output");o.liveDiagnostics=fs::absolute(argv[++i]);}
        else if(arg==L"--shelf-data"&&i+1<argc){need(o.shelfData.empty(),"Duplicate shelf data");o.shelfData=fs::absolute(argv[++i]);}
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
        else if(arg==L"--session-end-coverage"){need(!o.sessionEndCoverage,"Duplicate session-end coverage");o.sessionEndCoverage=true;}
        else if(arg==L"--benchmark-size"&&i+2<argc){auto dimension=[&]{const std::wstring token=argv[++i];std::size_t used{};const auto value=std::stoul(token,&used);need(used==token.size()&&value>=64&&value<=4096,"Benchmark dimensions must be 64...4096 pixels");return static_cast<std::uint32_t>(value);};o.benchmarkWidth=dimension();o.benchmarkHeight=dimension();}
        else if(arg==L"--benchmark-epoch"&&i+1<argc){const std::wstring token=argv[++i];std::size_t used{};o.benchmarkEpoch=std::stod(token,&used);need(used==token.size()&&std::isfinite(o.benchmarkEpoch)&&o.benchmarkEpoch>=0&&o.benchmarkEpoch<=1e9,"Invalid bounded synthetic epoch");}
        else if(arg==L"--runtime-input"){need(!o.runtimeInput,"Duplicate runtime-input mode");o.runtimeInput=true;}
        else if(arg==L"--warp"){need(!o.warp,"Duplicate WARP mode");o.warp=true;}
        else if(arg==L"--benchmark"&&i+1<argc){need(o.report.empty(),"Duplicate benchmark output");o.report=fs::absolute(argv[++i]);}
        else if(arg==L"--snapshots"&&i+1<argc){need(o.snapshots.empty(),"Duplicate snapshot directory");o.snapshots=fs::absolute(argv[++i]);}
        else if(arg==L"--chrome"&&i+1<argc){need(o.chrome.empty(),"Duplicate chrome reference");o.chrome=fs::absolute(argv[++i]);}
        else if(arg==L"--cursor-png"&&i+1<argc){need(o.cursor.empty(),"Duplicate cursor image");o.cursor=fs::absolute(argv[++i]);}
        else if(arg==L"--watch-blur"&&i+1<argc){need(o.watchBlur.empty(),"Duplicate original WatchBlur source");o.watchBlur=fs::absolute(argv[++i]);}
        else if(arg==L"--clipboard-fixture"){need(!o.clipboardFixture,"Duplicate Clipboard fixture");o.clipboardFixture=true;}
        else if(arg==L"--notes-format-assets"&&i+1<argc){need(o.notesFormatAssets.empty(),"Duplicate Notes format assets");o.notesFormatAssets=fs::absolute(argv[++i]);}
        else if(arg==L"--notes-data"&&i+1<argc){need(o.notesData.empty(),"Duplicate Notes fixture data");o.notesData=fs::absolute(argv[++i]);}
        else if(arg==L"--compiled-sha"&&i+1<argc){need(o.pin.empty(),"Duplicate cache pin");o.pin=utf8(argv[++i]);}
        else if(arg==L"--package"&&i+1<argc){need(o.package.empty(),"Duplicate staged package");o.package=fs::absolute(argv[++i]);}
        // Hidden only: time the cold and warm openings of the same owner instead of the module coverage.
        else if(arg==L"--startup-trace"&&i+1<argc){need(o.startupTrace.empty(),"Duplicate startup trace output");o.startupTrace=fs::absolute(argv[++i]);}
        else if(arg==L"--startup-trace-cold"){need(!o.startupTraceCold,"Duplicate cold startup trace");o.startupTraceCold=true;}
        else if(arg==L"--contention-trace"&&i+1<argc){need(o.contentionTrace.empty(),"Duplicate contention trace output");o.contentionTrace=fs::absolute(argv[++i]);}
        // Hidden only: the memory and thread census of the same owner (launch, every module, closing, cycles).
        else if(arg==L"--memory-trace"&&i+1<argc){need(o.memoryTrace.empty(),"Duplicate memory trace output");o.memoryTrace=fs::absolute(argv[++i]);}
        // Hidden only: closed and open idle of the same owner on the real clock (wake-ups, CPU and frames per window).
        else if(arg==L"--idle-trace"&&i+1<argc){need(o.idleTrace.empty(),"Duplicate idle trace output");o.idleTrace=fs::absolute(argv[++i]);}
        // --idle-trace-grace: the closed HUD's graphics grace (production: ten
        // minutes, app/application.hpp); --idle-trace-closed: how long the HUD
        // stays closed before the closed windows and the first opening.
        else if((arg==L"--idle-trace-seconds"||arg==L"--idle-trace-refresh"||arg==L"--idle-trace-grace"||arg==L"--idle-trace-closed")&&i+1<argc){const std::wstring token=argv[++i];std::size_t used{};const double value=std::stod(token,&used);
            need(used==token.size()&&std::isfinite(value),"Invalid idle trace number");if(arg==L"--idle-trace-seconds"){need(value>=1&&value<=600,"Idle trace windows are 1...600 seconds");o.idleTraceSeconds=value;}
            else if(arg==L"--idle-trace-grace"){need(!o.idleTraceGrace&&value>=1&&value<=3600,"The idle trace grace is 1...3600 seconds, once");o.idleTraceGrace=value;}
            else if(arg==L"--idle-trace-closed"){need(!o.idleTraceClosed&&value>=1&&value<=3600,"The idle trace closed time is 1...3600 seconds, once");o.idleTraceClosed=value;}
            else{need(value>=20&&value<=1000,"Idle trace refresh is 20...1000 Hz");o.idleTraceRefresh=value;}}
        else need(false,"Unknown or incomplete preview argument");
    }
    need(!o.nativeClipboard||(o.visible&&!o.moduleCoverage&&!o.coverage&&!o.clipboardFixture&&!o.notesFormatAssets.empty()),"Native Clipboard requires explicit visible mode and module assets; hidden tests never access the clipboard");
    need(!o.nativeActivity||(o.visible&&!o.moduleCoverage&&!o.coverage&&!o.activityFixture&&!o.notesFormatAssets.empty()),"Native Activity requires explicit visible mode and module assets; hidden tests never enumerate applications");
    need(!o.nativeStorage||(o.visible&&!o.moduleCoverage&&!o.coverage&&!o.storageFixture&&!o.notesFormatAssets.empty()),"Native Storage requires explicit visible mode and module assets; hidden tests never read disks");
    need(o.notesFormatAssets.empty()==o.notesData.empty(),"Notes preview requires its format assets and a new data root together");
    need(o.visible||o.notesFormatAssets.empty()||o.moduleCoverage,"Hidden module integration requires explicit --module-coverage");
    need(!o.moduleCoverage||(!o.visible&&!o.notesFormatAssets.empty()),"Module coverage requires hidden mode and fresh isolated Notes data");
    need(!o.sessionEndCoverage||o.moduleCoverage,"Session-end coverage extends hidden module coverage");
    need(o.package.empty()||(o.moduleCoverage&&fs::is_directory(o.package)),"The staged resource package is an existing hidden module coverage input");
    need(o.notesData.empty()||!fs::exists(o.notesData),"Notes preview data root must be new");
    need(o.startupTrace.empty()||(o.moduleCoverage&&o.report.empty()&&!o.sessionEndCoverage&&!fs::exists(o.startupTrace)),"A startup trace replaces the module coverage report with a new output");
    need(!o.startupTraceCold||!o.startupTrace.empty(),"A cold startup trace (without the launch prewarm) needs --startup-trace");
    need(o.contentionTrace.empty()||(o.report.empty()&&o.startupTrace.empty()&&o.memoryTrace.empty()&&!o.moduleCoverage&&!o.coverage&&!o.warp&&!fs::exists(o.contentionTrace)),"A contention trace is its own hidden hardware run with a new output");
    need(o.memoryTrace.empty()||(o.moduleCoverage&&o.report.empty()&&o.startupTrace.empty()&&!o.sessionEndCoverage&&!fs::exists(o.memoryTrace)),"A memory trace replaces the module coverage report with a new output");
    need(!o.idleTrace.empty()||(!o.idleTraceGrace&&!o.idleTraceClosed),"The graphics grace and closed time are idle trace options");
    need(o.idleTrace.empty()||(o.moduleCoverage&&o.report.empty()&&o.startupTrace.empty()&&o.memoryTrace.empty()&&!o.sessionEndCoverage&&!fs::exists(o.idleTrace)),"An idle trace replaces the module coverage report with a new output");
    need(o.visible!=(!o.report.empty()||!o.startupTrace.empty()||!o.contentionTrace.empty()||!o.memoryTrace.empty()||!o.idleTrace.empty()),"Choose exactly one explicit visible or benchmark mode");need(!o.visible||!o.warp,"WARP is a hidden test mode only");
    need(!o.visible||!o.watchBlur.empty(),"Visible preview requires the explicit source-pinned --watch-blur animation");
    need(!o.visible||(!o.coverage&&o.benchmarkEpoch==0&&o.benchmarkWidth==1280&&o.benchmarkHeight==800),"Coverage/epoch/size overrides are hidden-test only");
    need(o.liveDiagnostics.empty()||(o.visible&&!o.notesFormatAssets.empty()&&!fs::exists(o.liveDiagnostics)),"Live diagnostics require visible isolated Notes and a new output");
    need(!o.eventLogFixture||!o.notesFormatAssets.empty(),"Event Log fixture requires the shared module owner");
    need(o.residentIcons.empty()||(o.visible&&!o.notesFormatAssets.empty()),"Resident test requires the explicitly visible isolated module fixture");
    need(!o.batteryFixture||!o.notesFormatAssets.empty(),"Battery fixture requires shared module ownership");
    need(!o.workModeFixture||!o.notesFormatAssets.empty(),"Work Mode fixture needs shared module/text ownership");
    need(o.settingsAssets.empty()||!o.notesFormatAssets.empty(),"Settings needs the shared module owner");
    need(!o.activityFixture||!o.notesFormatAssets.empty(),"Activity fixture requires the shared module owner");
    need(!o.projectionModule||(o.visible&&!o.notesFormatAssets.empty()),"Projection needs the visible shared HUD and source controls");
    need(o.orbipomAssets.empty()||(!o.notesFormatAssets.empty()&&!o.settingsAssets.empty()),"Minigame needs the shared module owner and an isolated Settings store");
    need(o.mapGeography.empty()==o.mapPlayerAssets.empty(),"Map needs geography and player assets together");
    need(o.mapGeography.empty()||!o.notesFormatAssets.empty(),"Map needs the shared module owner and a new temporary data root");
    need(!o.readerModule||!o.notesFormatAssets.empty(),"Reader needs the shared module owner and a new temporary data root");
    need(!o.calendarModule||!o.notesFormatAssets.empty(),"Calendar needs the shared module owner and a new temporary data root");
    need(!o.readerScrollTrace||(o.visible&&o.readerModule&&!o.notesData.empty()),"Reader trace requires explicit visible isolated Reader data");
    need(!o.storageFixture||!o.notesFormatAssets.empty(),"Storage fixture requires the shared module owner");
    need(!o.archiveModule||!o.notesFormatAssets.empty(),"Archive needs explicit Notes assets and a new shared test data root");
    need(!o.volumeFixture||!o.notesFormatAssets.empty(),"Volume fixture requires the shared module owner");
    need(!o.clipboardFixture||!o.notesFormatAssets.empty(),"Clipboard fixture requires the shared module owner");
    // --shelf-data (a NEW directory) shows the File Shelf on the shared module owner.
    need(o.shelfData.empty()||!o.notesFormatAssets.empty(),"Shelf uses the shared Notes module transition owner");
    if(!o.report.empty())need(!fs::exists(o.report),"Benchmark output already exists");
    need(o.snapshots.empty()||!o.visible,"Owned-target snapshots are hidden benchmark only");
    if(!o.snapshots.empty())need(!fs::exists(o.snapshots),"Snapshot directory already exists");return o;
}


// Hidden coverage registers one inert module through the public factory seam
// (ApplicationOptions::modules) and checks that the owner delivered every
// shared lifecycle event to it, including the final GPU release.
struct ProbeCounts {unsigned resize{},appearance{},opened{},closed{},updates{},uploads{},releases{};bool contextResources{};};
class CoverageProbe final:public app::ModuleOwner {
    std::shared_ptr<ProbeCounts>counts_;
public:
    explicit CoverageProbe(std::shared_ptr<ProbeCounts>counts):counts_(std::move(counts)){}
    std::string_view name()const noexcept override{return "coverageProbe";}
    void resize(const app::ClientMetrics&)override{++counts_->resize;}
    void setAppearance(const app::ModuleAppearance&,double)override{++counts_->appearance;}
    void setOverlayVisible(bool visible,double)override{if(visible)++counts_->opened;}
    void overlayClosing(double)override{++counts_->closed;}
    void update(const app::ModuleFrame&)override{++counts_->updates;}
    void upload(endfield::native::Renderer&)override{++counts_->uploads;}
    void release(endfield::native::Renderer&)override{++counts_->releases;}
};

// Hidden coverage audit of every registered module area. The harness wraps the
// area's owner transparently and requires, once the Application is gone, that
// it received its context, every shared event and host broadcast the coverage
// run delivers, its coverage steps and exactly one GPU release.
struct AreaAudit {
    std::string name;bool constructed{},destroyed{},context{};
    unsigned resize{},appearance{},opened{},closed{},updates{},uploads{},releases{},coverage{};
    std::array<unsigned,static_cast<std::size_t>(app::HostEvent::Kind::count)>events{};
    // The one host time (ModuleRegistry::hostTime): every time the host hands
    // this area must be finite and never earlier than one it already handed.
    double latestTime{-std::numeric_limits<double>::infinity()};unsigned timedCalls{},timeRegressions{};std::string firstRegression;
    void time(double t,const char*operation){++timedCalls;
        if(!std::isfinite(t)||t<latestTime){if(!timeRegressions++)firstRegression=std::string(operation)+" at "+std::to_string(t)+" after "+std::to_string(latestTime);return;}
        latestTime=t;}
};
class AuditedArea final:public app::ModuleOwner {
    std::unique_ptr<app::ModuleOwner>inner_;std::shared_ptr<AreaAudit>audit_;
public:
    AuditedArea(std::unique_ptr<app::ModuleOwner>inner,std::shared_ptr<AreaAudit>audit):inner_(std::move(inner)),audit_(std::move(audit)){audit_->constructed=true;}
    ~AuditedArea()override{inner_.reset();audit_->destroyed=true;}
    AuditedArea(const AuditedArea&)=delete;AuditedArea&operator=(const AuditedArea&)=delete;
    std::string_view name()const noexcept override{return inner_->name();}
    app::ModuleRouting routing()const noexcept override{return inner_->routing();}
    bool essential()const noexcept override{return inner_->essential();}
    bool presents(core::Module m)const noexcept override{return inner_->presents(m);}
    void resize(const app::ClientMetrics&m)override{++audit_->resize;inner_->resize(m);}
    void setAppearance(const app::ModuleAppearance&a,double t)override{++audit_->appearance;audit_->time(t,"appearance");inner_->setAppearance(a,t);}
    void setOverlayVisible(bool v,double t)override{if(v)++audit_->opened;audit_->time(t,"open");inner_->setOverlayVisible(v,t);}
    void overlayClosing(double t)override{++audit_->closed;audit_->time(t,"close");inner_->overlayClosing(t);}
    void update(const app::ModuleFrame&f)override{++audit_->updates;audit_->time(f.time,"frame");inner_->update(f);}
    void prewarm(const app::ModuleFrame&f)override{audit_->time(f.time,"prewarm");inner_->prewarm(f);}
    bool requiresFrames(double t)const override{audit_->time(t,"frame demand");return inner_->requiresFrames(t);}
    std::optional<double>nextWakeTime(double t)const override{audit_->time(t,"wake");return inner_->nextWakeTime(t);}
    bool deadline(double t)override{audit_->time(t,"deadline");return inner_->deadline(t);}
    void upload(gpu::Renderer&r)override{++audit_->uploads;inner_->upload(r);}
    std::span<const gpu::LayerCompositionEntry>entries(core::Module m)override{return inner_->entries(m);}
    std::span<const gpu::LayerCompositionEntry>planeEntries()override{return inner_->planeEntries();}
    std::span<const gpu::LayerCompositionEntry>floatingEntries()override{return inner_->floatingEntries();}
    std::span<const gpu::LayerCompositionEntry>modalEntries()override{return inner_->modalEntries();}
    void collected(gpu::Renderer&r)override{inner_->collected(r);}
    void release(gpu::Renderer&r)override{++audit_->releases;inner_->release(r);}
    bool covers(core::Point p)const override{return inner_->covers(p);}
    bool pointerLocked()const override{return inner_->pointerLocked();}
    bool capturesPointer()const override{return inner_->capturesPointer();}
    bool capturesWheel()const override{return inner_->capturesWheel();}
    bool capturesKeys()const override{return inner_->capturesKeys();}
    bool suppressesKeyFilters()const override{return inner_->suppressesKeyFilters();}
    bool pointer(const app::PointerEvent&e,double t)override{audit_->time(t,"pointer");return inner_->pointer(e,t);}
    bool wheel(const app::WheelEvent&e,double t)override{audit_->time(t,"wheel");return inner_->wheel(e,t);}
    bool key(const app::KeyEvent&e,double t)override{audit_->time(t,"key");return inner_->key(e,t);}
    bool filterKey(const app::NativeMessage&m)override{return inner_->filterKey(m);}
    bool message(const app::NativeMessage&m,double t)override{audit_->time(t,"message");return inner_->message(m,t);}
    void focus(bool v,double t)override{audit_->time(t,"focus");inner_->focus(v,t);}
    void cancelInteraction(double t)override{audit_->time(t,"cancel");inner_->cancelInteraction(t);}
    bool releasesSelection(double t)override{audit_->time(t,"select");return inner_->releasesSelection(t);}
    bool finishEditing(double t)override{audit_->time(t,"finish editing");return inner_->finishEditing(t);}
    bool flush(double t)override{audit_->time(t,"flush");return inner_->flush(t);}
    bool hostEvent(const app::HostEvent&e,double t)override{++audit_->events[static_cast<std::size_t>(e.kind)];audit_->time(t,"host event");return inner_->hostEvent(e,t);}
    void coverage(app::ModuleCoverage&c)override{++audit_->coverage;audit_->time(c.time,"coverage step");inner_->coverage(c);}
    bool drop(std::span<const std::string>paths,core::Point p,double t)override{audit_->time(t,"drop");return inner_->drop(paths,p,t);}
    bool dropHover(std::span<const std::string>paths,std::optional<core::Point>p,double t)override{audit_->time(t,"drop hover");return inner_->dropHover(paths,p,t);}
    std::optional<app::ModuleCursor>cursorAt(core::Point p)const override{return inner_->cursorAt(p);}
    bool automationRefresh(IRawElementProviderFragmentRoot*root)override{return inner_->automationRefresh(root);}
    std::size_t automationCount()const override{return inner_->automationCount();}
    long automationChild(std::size_t i,IRawElementProviderFragment**out)const override{return inner_->automationChild(i,out);}
    long automationFromPoint(double x,double y,IRawElementProviderFragment**out)const override{return inner_->automationFromPoint(x,y,out);}
    void automationDisconnect()noexcept override{inner_->automationDisconnect();}
    std::optional<core::Module>automationModule()const noexcept override{return inner_->automationModule();}
    app::ModuleResidency residency()const override{return inner_->residency();}
};
void requireAudit(const AreaAudit&a,bool packaged){
    if(!a.constructed){if(packaged)throw std::runtime_error("Module area "+a.name+" returned no owner although the staged package was available");return;}
    const auto event=[&](app::HostEvent::Kind kind){return a.events[static_cast<std::size_t>(kind)];};
    using K=app::HostEvent::Kind;
    const std::array<K,7> required{K::utilityCompleted,K::overlayConcealed,K::suspended,K::resumed,K::timeChanged,K::systemSettingsChanged,K::settingsChanged};
    std::string missing;
    if(!a.context)missing+=" context";if(!a.resize)missing+=" resize";if(!a.appearance)missing+=" appearance";if(!a.opened)missing+=" open";if(!a.closed)missing+=" close";
    if(!a.updates)missing+=" frames";if(!a.uploads)missing+=" upload";if(a.releases!=1)missing+=" single-release";if(!a.destroyed)missing+=" destruction";if(a.coverage<3)missing+=" coverage-steps";
    if(a.timeRegressions)missing+=" non-decreasing-host-time("+std::to_string(a.timeRegressions)+" of "+std::to_string(a.timedCalls)+", first: "+a.firstRegression+")";
    for(const auto kind:required)if(!event(kind))missing+=" "+std::string(app::hostEventName(kind));
    if(!missing.empty())throw std::runtime_error("Module area "+a.name+" did not receive:"+missing);
}
app::ApplicationOptions applicationOptions(const Options&o){
    app::ApplicationOptions a;
    a.mode=o.visible?app::ApplicationMode::visiblePreview:app::ApplicationMode::hiddenBenchmark;
    a.packet=o.packet;a.cache=o.cache;a.shader=o.shader;a.chrome=o.chrome;a.cursor=o.cursor;a.watchBlur=o.watchBlur;a.cachePin=o.pin;
    a.runtimeInput=o.runtimeInput;a.warp=o.warp;
    a.notesFormatAssets=o.notesFormatAssets;a.fileShelf=!o.shelfData.empty();
    a.residentIcons=o.residentIcons;a.settingsAssets=o.settingsAssets;a.archive=o.archiveModule;
    a.mapGeography=o.mapGeography;a.mapPlayerAssets=o.mapPlayerAssets;a.orbipomAssets=o.orbipomAssets;
    a.dataRoot=o.notesData;a.shelfDataRoot=o.shelfData;
    a.reader=o.readerModule;a.calendar=o.calendarModule;a.projection=o.projectionModule;
    a.nativeClipboard=o.nativeClipboard;a.clipboardFixture=o.clipboardFixture;a.nativeActivity=o.nativeActivity;a.activityFixture=o.activityFixture;a.nativeStorage=o.nativeStorage;a.storageFixture=o.storageFixture;
    a.volumeFixture=o.volumeFixture;a.eventLogFixture=o.eventLogFixture;a.workModeFixture=o.workModeFixture;a.batteryFixture=o.batteryFixture;
    a.moduleCoverage=o.moduleCoverage;a.coverage=o.coverage;a.readerScrollTrace=o.readerScrollTrace;a.sessionEndCoverage=o.sessionEndCoverage;
    a.report=o.report;a.snapshots=o.snapshots;a.liveDiagnostics=o.liveDiagnostics;a.startupTrace=o.startupTrace;a.startupTraceWithoutPrewarm=o.startupTraceCold;a.contentionTrace=o.contentionTrace;a.memoryTrace=o.memoryTrace;a.idleTrace=o.idleTrace;a.idleTraceSeconds=o.idleTraceSeconds;a.idleTraceRefreshRate=o.idleTraceRefresh;a.idleTraceClosedSeconds=o.idleTraceClosed;if(o.idleTraceGrace)a.closedGraphicsGrace=*o.idleTraceGrace;
    a.benchmarkWidth=o.benchmarkWidth;a.benchmarkHeight=o.benchmarkHeight;a.benchmarkEpoch=o.benchmarkEpoch;
    // Development previews keep failures fatal so hidden coverage cannot pass
    // with a silently disabled module.
    a.isolateModuleFailures=false;a.persistLaunchMarker=false;
    return a;
}
}
int wmain(int argc,wchar_t**argv){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;try{
    const auto args=options(argc,argv);
    auto settings=applicationOptions(args);
    std::optional<endfield::core::ResourceLocator>package;
    if(!args.package.empty()){
        // The build's staged EndfieldHUD package: areas resolve the same
        // SHA-256 verified resources as in production (read only).
        package.emplace(endfield::core::ResourceLocator::open(args.package));
        settings.resources=[&locator=*package](std::string_view id)->fs::path{return locator.available(id)?locator.resolve(id).path:fs::path{};};
    }
    std::shared_ptr<ProbeCounts>probe;
    if(args.moduleCoverage){probe=std::make_shared<ProbeCounts>();settings.modules.emplace_back("coverage probe",[probe,packaged=package.has_value()](const app::ApplicationModuleContext&context){
        // Without --package there is no verified package: resources resolve empty.
        probe->contextResources=context.resource&&context.resource("shell.shader").empty()!=packaged&&context.utility&&context.window&&context.host&&
            context.mode==app::ApplicationAreaMode::hiddenCoverage&&fs::is_directory(context.coverageRoot);
        return std::make_unique<CoverageProbe>(probe);});}
    // Registered module areas join whenever the module owner exists.
    std::vector<std::shared_ptr<AreaAudit>>audits;
    if(!args.notesFormatAssets.empty()){
        std::vector<std::pair<std::string,app::ApplicationModuleFactory>>areas;app::appendRegisteredAreas(areas,args.moduleCoverage);
        for(auto&[name,factory]:areas){
            if(!args.moduleCoverage){settings.modules.emplace_back(name,std::move(factory));continue;}
            auto audit=std::make_shared<AreaAudit>();audit->name=name;audits.push_back(audit);
            settings.modules.emplace_back(name,[audit,factory=std::move(factory)](const app::ApplicationModuleContext&context)->std::unique_ptr<app::ModuleOwner>{
                audit->context=context.name==audit->name&&context.mode==app::ApplicationAreaMode::hiddenCoverage&&context.host&&context.utility&&context.window&&
                    context.resource&&context.fixture&&fs::is_directory(context.coverageRoot)&&fs::is_empty(context.coverageRoot);
                auto owner=factory(context);if(!owner)return nullptr;
                return std::make_unique<AuditedArea>(std::move(owner),audit);});
        }
        settings.fixtures=[](std::string_view id){return app::registeredCoverageFixture(id);};
    }
    int result{};
    {app::Application application(std::move(settings));result=args.visible?application.run():application.runHiddenVerification();}
    // A startup or memory trace runs no coverage steps: the coverage audits do not apply.
    if(!args.startupTrace.empty()||!args.memoryTrace.empty()||!args.idleTrace.empty())return result;
    if(probe)need(probe->resize>0&&probe->appearance>0&&probe->opened>0&&probe->closed>0&&probe->updates>0&&probe->uploads>0&&probe->releases==1&&probe->contextResources,
        "A factory-added module owner did not receive its context and every shared lifecycle event");
    unsigned constructed{};
    for(const auto&audit:audits){requireAudit(*audit,package.has_value());constructed+=audit->constructed;}
    if(args.moduleCoverage)std::cout<<"Module areas: "<<constructed<<" of "<<audits.size()<<" constructed; each received its context, every shared event, its coverage steps and one release\n";
    return result;
}catch(const winrt::hresult_error&e){std::cerr<<"Source preview failed: HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
catch(const std::exception&e){std::cerr<<"Source preview failed: "<<e.what()<<'\n';return 1;}}
