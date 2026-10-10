// Development harness over the production Application owner. Every input is
// an explicit synthetic export/cache or a NEW temporary data root; visible
// launch is opt-in and the benchmark never shows its owned HWND. The command
// line is unchanged: the integration validator runs this executable with the
// exact --module-coverage arguments of verify-modules-121.ps1.
#include "app/application.hpp"
#include <memory>
#include <cmath>
#include <filesystem>
#include <iostream>
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
namespace {
void need(bool value,const char*message){if(!value)throw std::runtime_error(message);}
std::string utf8(const fs::path&value){const auto bytes=value.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
struct Options {fs::path packet,cache,shader,chrome,cursor,report,snapshots,watchBlur,notesAssets,notesData,notesFormatAssets,shelfAssets,shelfData,shelfMask,clipboardAssets,liveDiagnostics,residentIcons,settingsAssets,archiveAssets,storageAssets,activityAssets,mapGeography,mapPlayerAssets,orbipomAssets;std::string pin,notesAssetsSHA;bool visible{},warp{},runtimeInput{},coverage{},moduleCoverage{},sessionEndCoverage{},nativeClipboard{},nativeActivity{},readerScrollTrace{},readerModule{},calendarModule{},projectionModule{},volumeFixture{},eventLogFixture{},workModeFixture{},batteryFixture{};std::uint32_t benchmarkWidth{1280},benchmarkHeight{800};double benchmarkEpoch{};};
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
    need(!o.sessionEndCoverage||o.moduleCoverage,"Session-end coverage extends hidden module coverage");
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


// Hidden coverage registers one inert module through the public factory seam
// (ApplicationOptions::modules) and checks that the owner delivered every
// shared lifecycle event to it, including the final GPU release.
struct ProbeCounts {unsigned resize{},appearance{},opened{},closed{},updates{},uploads{},releases{};bool developmentResources{};};
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
app::ApplicationOptions applicationOptions(const Options&o){
    app::ApplicationOptions a;
    a.mode=o.visible?app::ApplicationMode::visiblePreview:app::ApplicationMode::hiddenBenchmark;
    a.packet=o.packet;a.cache=o.cache;a.shader=o.shader;a.chrome=o.chrome;a.cursor=o.cursor;a.watchBlur=o.watchBlur;a.cachePin=o.pin;
    a.runtimeInput=o.runtimeInput;a.warp=o.warp;
    a.notesAssets=o.notesAssets;a.notesFormatAssets=o.notesFormatAssets;a.shelfAssets=o.shelfAssets;a.shelfMask=o.shelfMask;a.clipboardAssets=o.clipboardAssets;
    a.residentIcons=o.residentIcons;a.settingsAssets=o.settingsAssets;a.archiveAssets=o.archiveAssets;a.storageAssets=o.storageAssets;a.activityAssets=o.activityAssets;
    a.mapGeography=o.mapGeography;a.mapPlayerAssets=o.mapPlayerAssets;a.orbipomAssets=o.orbipomAssets;a.notesAssetsSHA=o.notesAssetsSHA;
    a.dataRoot=o.notesData;a.shelfDataRoot=o.shelfData;
    a.reader=o.readerModule;a.calendar=o.calendarModule;a.projection=o.projectionModule;
    a.nativeClipboard=o.nativeClipboard;a.nativeActivity=o.nativeActivity;
    a.volumeFixture=o.volumeFixture;a.eventLogFixture=o.eventLogFixture;a.workModeFixture=o.workModeFixture;a.batteryFixture=o.batteryFixture;
    a.moduleCoverage=o.moduleCoverage;a.coverage=o.coverage;a.readerScrollTrace=o.readerScrollTrace;a.sessionEndCoverage=o.sessionEndCoverage;
    a.report=o.report;a.snapshots=o.snapshots;a.liveDiagnostics=o.liveDiagnostics;
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
    std::shared_ptr<ProbeCounts>probe;
    if(args.moduleCoverage){probe=std::make_shared<ProbeCounts>();settings.modules.emplace_back("coverage probe",[probe](const app::ApplicationModuleContext&context){
        // Development previews have no verified package: resources resolve empty.
        probe->developmentResources=context.resource&&context.resource("shell.shader").empty()&&context.utility&&context.window;
        return std::make_unique<CoverageProbe>(probe);});}
    int result{};
    {app::Application application(std::move(settings));result=args.visible?application.run():application.runHiddenVerification();}
    if(probe)need(probe->resize>0&&probe->appearance>0&&probe->opened>0&&probe->closed>0&&probe->updates>0&&probe->uploads>0&&probe->releases==1&&probe->developmentResources,
        "A factory-added module owner did not receive its context and every shared lifecycle event");
    return result;
}catch(const winrt::hresult_error&e){std::cerr<<"Source preview failed: HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
catch(const std::exception&e){std::cerr<<"Source preview failed: "<<e.what()<<'\n';return 1;}}
