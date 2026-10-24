// Hidden offscreen benchmark and module coverage over the production owner.
// Linked only into watch_session_preview (hud_application_verification); the
// production EndfieldHUD.exe never contains this harness. Every input is an
// explicit synthetic fixture in a new temporary data root; nothing is shown.
#include "app/application_impl.hpp"
#include "app/process_diagnostics.hpp"
#include "app/areas/calendar_reminder_module.hpp"
#include "native/display_center_logo.hpp"
#include "native/module_scene.hpp" // NativeModuleSurface: coverage projection probes
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <tlhelp32.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace endfield::app {
namespace {
struct Snapshot {
    gpu::RendererStats renderer;gpu::SourceGraphicsStats graphics;gpu::LayerRasterStats raster;
    source::SourceWatchFrameStats frame;WatchSessionStats session;
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

// watch_session_preview --startup-trace: the production owner with the module
// coverage inputs, hidden on the synthetic clock. After the post-launch idle
// work settled (a user summons after launch), the first opening (cold) and a
// second one (warm) run through the production lifecycle at 60 Hz; each
// present() is timed (CPU wall time; GPU submission included, no vsync
// wait), utility completions are delivered between frames as the message
// loop would, and the report is the production trace (numbers only).
int Application::Impl::runStartupTrace(){
    if(!startupTrace)throw std::logic_error("Startup trace requires its output");
    double t=hostTime(args.benchmarkEpoch+10);
    const auto settle=[&](double at){
        if(utility&&(utility->stats().completed||(longWork&&longWork->stats().completed)))utilityCompleted();
        if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();traceOpened(at);perform(lifecycle.didOpen(token),at);}
        if(lifecycleClosing&&session->phase()==core::VisibilityPhase::concealed){const auto token=*lifecycleClosing;lifecycleClosing.reset();concealed(at);perform(lifecycle.didClose(token),at);}
    };
    // Production selects Map at construction (OverlayController.lastSystemModule)
    // so the first opening shows it without a module transition, and applies
    // the loaded preference record before any opening.
    if(notes&&notes->selected()!=core::Module::map){notes->select(core::Module::map,hostTime(t));t+=1;}
    configurationPending=true;applyConfiguration(t);
    // The launch prewarm, then the run idles: queued post-launch work finishes.
    if(!args.startupTraceWithoutPrewarm)prewarm();
    if(utility){utility->waitIdle();if(longWork)longWork->waitIdle();utilityCompleted();}
    for(unsigned opening=0;opening<2;++opening){
        perform(opening?lifecycle.reopen():lifecycle.toggle(),t);
        for(unsigned frame=0;frame<150&&lifecycle.phase()!=core::SystemOverlayPhase::open;++frame){t=hostTime(t+1./60);
            const auto begin=Clock::now();present(t,true);startupTrace->frame(begin,Clock::now());settle(t);}
        need(lifecycle.phase()==core::SystemOverlayPhase::open,"Traced opening completes");
        for(unsigned frame=0;frame<30;++frame){t=hostTime(t+1./60);present(t,true);settle(t);}
        perform(lifecycle.toggle(),t);
        for(unsigned frame=0;frame<150&&lifecycle.phase()!=core::SystemOverlayPhase::closed;++frame){t=hostTime(t+1./60);present(t,true);settle(t);}
        need(lifecycle.phase()==core::SystemOverlayPhase::closed,"Traced closing completes");
        t+=2;
    }
    need(startupTrace->complete(),"Both traced openings completed");
    writeStartupTrace();std::cout<<"Wrote hidden startup trace\n";
    shutdownVisible();
    return 0;
}

// watch_session_preview --contention-trace: the opened HUD paced at 60 Hz on
// a high-resolution waitable timer (as the live frame clock), on the hardware
// GPU, in alternating blocks at normal priority and with PresentationPriority
// raised. Per frame: how late the thread woke for its tick (CPU scheduling),
// the CPU time of present() and the wait until the GPU finished it (GPU
// scheduling; the offscreen target has no vblank). Run it while
// tools/application_contention_stress loads the CPU and GPU like a game.
int Application::Impl::runContentionTrace(){
    need(!args.warp,"The contention trace measures the hardware GPU");
    double t=hostTime(args.benchmarkEpoch+10);
    settings.ambientEnabled=true;session->setSettings(settings,t);session->pointerMove(core::Point{-10,20},t);
    open(t);for(unsigned frame=0;frame<120;++frame){t=hostTime(t+1./60);present(t,true);}
    need(session->phase()==core::VisibilityPhase::visible,"The traced HUD finished opening");
    const auto timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    need(timer!=nullptr,"High-resolution frame timer");
    struct Series{std::vector<double>late,cpu,gpu,total;};
    std::array<Series,2> series; // 0 normal priority, 1 raised
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    const auto clock=[&]{LARGE_INTEGER v{};QueryPerformanceCounter(&v);return double(v.QuadPart)/double(frequency.QuadPart);};
    Json raisedReport;
    // Alternating blocks cancel slow drift of the external load.
    for(unsigned block=0;block<8;++block){
        const bool raised=block%2==1;auto&out=series[raised];
        if(raised){presentationPriority.raise(&renderer);const auto&r=presentationPriority.report();
            raisedReport=Json::Object{{"processRaised",r.processRaised},{"previousPriorityClass",std::int64_t(r.previousClass)},{"mmcssGamesTask",r.threadRegistered},
                {"powerThrottlingOff",r.powerThrottlingOff},{"gpuSchedulingClassHigh",r.gpuSchedulingRaised},{"gpuSchedulingStatus",std::int64_t(r.gpuSchedulingStatus)},{"gpuThreadPriority7",r.gpuThreadRaised},{"gpuThreadPriorityResult",std::int64_t(r.gpuThreadResult)}};}
        else presentationPriority.restore();
        double next=clock();
        for(unsigned frame=0;frame<150;++frame){
            next+=1./60;const double wait=next-clock();
            if(wait>0){LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(wait*1e7);SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0);WaitForSingleObject(timer,INFINITE);}
            const double woke=clock();
            t=hostTime(t+1./60);present(t,true);const double prepared=clock();
            const double gpu=renderer.waitForGpuIdle();const double done=clock();
            if(frame<10)continue; // the first frames after a priority change settle
            out.late.push_back(std::max(0.,(woke-next)*1000));out.cpu.push_back((prepared-woke)*1000);out.gpu.push_back(gpu);out.total.push_back((done-next)*1000);
            if(done>next+1./60)next=done; // no catch-up burst, as the live clock
        }
    }
    presentationPriority.restore();CloseHandle(timer);
    const auto stats=[](std::vector<double> v){std::sort(v.begin(),v.end());double sum{};for(const auto x:v)sum+=x;
        const auto at=[&](double q){return v.empty()?0.:v[std::min(v.size()-1,static_cast<std::size_t>(q*double(v.size()-1)+.5))];};
        return Json::Object{{"samples",std::int64_t(v.size())},{"mean",v.empty()?0.:sum/double(v.size())},{"p50",at(.5)},{"p95",at(.95)},{"p99",at(.99)},{"max",v.empty()?0.:v.back()}};};
    // A frame that finished after its 60 Hz tick (wake + CPU + GPU) is a dropped frame.
    const auto phase=[&](const Series&x){std::int64_t over{};for(const auto v:x.total)over+=v>1000./60;
        return Json::Object{{"wakeLatenessMilliseconds",stats(x.late)},{"cpuFrameMilliseconds",stats(x.cpu)},{"gpuCompletionMilliseconds",stats(x.gpu)},{"tickToGpuDoneMilliseconds",stats(x.total)},{"framesOverOneTick",over}};};
    const auto info=renderer.deviceInfo();
    Json report=Json::Object{{"scope","Hidden HUD frames paced at 60 Hz; wake lateness, CPU present() and GPU completion per frame; alternating 150-frame blocks"},
        {"device",info.name},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},
        {"normalPriority",phase(series[0])},{"raisedPriority",phase(series[1])},{"raisedReport",raisedReport}};
    const std::size_t bound=4*1024*1024;ehud::data::detail::replaceFile(args.contentionTrace,std::nullopt,report.encode(),bound);
    std::cout<<"Wrote hidden contention trace\n";
    shutdownVisible();
    return 0;
}

// watch_session_preview --memory-trace: the production owner with the module
// coverage inputs, hidden on the synthetic clock, measured where a user
// would look: after construction, at launch idle (after the launch prewarm
// and post-launch work), after the cold first opening, after visiting every
// presented module once (one row per visit with its deltas), once closed and
// after ten more open/close cycles. Hardware GPU unless --warp; synthetic
// providers (no audio, media sessions, clipboard, accounts or installed apps).
int Application::Impl::runMemoryTrace(){
    if(!memoryTrace)throw std::logic_error("Memory trace requires its output");
    double t=hostTime(args.benchmarkEpoch+10);
    const auto settle=[&](double at){
        if(utility&&(utility->stats().completed||(longWork&&longWork->stats().completed)))utilityCompleted();
        if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();perform(lifecycle.didOpen(token),at);}
        if(lifecycleClosing&&session->phase()==core::VisibilityPhase::concealed){const auto token=*lifecycleClosing;lifecycleClosing.reset();concealed(at);perform(lifecycle.didClose(token),at);}
    };
    // CPU time of each traced frame (the module visits report their slowest
    // frame and the frame that follows the end of the switch).
    double frameMax{},frameTotal{},afterSwitch{};unsigned frameCount{},afterSwitchFrames{};bool wasSwitching{};
    const auto frames=[&](unsigned count){for(unsigned n=0;n<count;++n){t=hostTime(t+1./60);const auto begin=Clock::now();present(t,true);const double ms=milliseconds(begin);
        frameMax=std::max(frameMax,ms);frameTotal+=ms;++frameCount;const bool switching=notes&&notes->modulePresentation().transitioning;
        // The frame that ends the switch (its group release and driver trim) and the two after it.
        if(wasSwitching&&!switching)afterSwitchFrames=3;if(afterSwitchFrames){afterSwitch=std::max(afterSwitch,ms);--afterSwitchFrames;}
        wasSwitching=switching;settle(t);}};
    const auto idle=[&]{if(utility){utility->waitIdle();if(longWork)longWork->waitIdle();utilityCompleted();}};
    const auto until=[&](core::SystemOverlayPhase phase,const char*what){for(unsigned n=0;n<240&&lifecycle.phase()!=phase;++n)frames(1);need(lifecycle.phase()==phase,what);};
    // The closed HUD presents nothing; the production waitable timer still
    // delivers the owners' deadlines (e.g. the Reader's departure release).
    const auto deadlines=[&](double seconds){for(const double end=t+seconds;t<end;){t=hostTime(t+.1);onDeadline(t);settle(t);}};
    // Production selects Map at construction and applies the preference record.
    if(notes&&notes->selected()!=core::Module::map){notes->select(core::Module::map,hostTime(t));t+=1;}
    configurationPending=true;applyConfiguration(t);
    prewarm();idle();
    recordMemory("launch-idle",true);
    perform(lifecycle.toggle(),t);until(core::SystemOverlayPhase::open,"Traced opening completes");frames(30);idle();frames(2);
    recordMemory("first-opening",true);
    // Every presented module, as a user visits them: one row of deltas each.
    std::vector<core::Module> modules;
    for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);
        if(auto*owner=registry.presenter(module);owner&&registry.enabled(*owner))modules.push_back(module);}
    const auto sample=[&]{struct Sample{ProcessDiagnostics process;gpu::LayerRasterStats raster;gpu::RendererStats rendered;std::uint32_t pool;};return Sample{captureProcessDiagnostics(false),rasterizer.stats(),renderer.stats(),countPoolWorkers()};};
    const auto gpuBytes=[](const ProcessDiagnostics&d){std::uint64_t sum{};for(const auto&a:d.adapters)sum+=a.localUsage+a.nonLocalUsage;return sum;};
    const auto delta=[](auto after,auto before){return Json(static_cast<std::int64_t>(after)-static_cast<std::int64_t>(before));};
    for(const auto module:modules){
        const auto before=sample();frameMax=frameTotal=afterSwitch=0;frameCount=0;
        selectModule(module,t);frames(45);idle();frames(5);
        const auto after=sample();auto*owner=registry.presenter(module);
        memoryTrace->visit(Json::Object{{"module",std::string(core::moduleIdentifier(module))},{"owner",owner?std::string(owner->name()):std::string()},
            {"privateBytes",Json(static_cast<std::int64_t>(after.process.memory.privateBytes))},
            {"privateBytesDelta",delta(after.process.memory.privateBytes,before.process.memory.privateBytes)},
            {"heapCommittedDelta",delta(after.process.heaps.committed,before.process.heaps.committed)},
            {"rasterEntriesDelta",delta(after.raster.entries,before.raster.entries)},{"rasterBytesDelta",delta(after.raster.resourceBytes,before.raster.resourceBytes)},
            {"rendererBytesDelta",delta(after.rendered.resourceBytes,before.rendered.resourceBytes)},
            {"gpuUsageDelta",delta(gpuBytes(after.process),gpuBytes(before.process))},
            {"threadsDelta",delta(after.process.memory.threads,before.process.memory.threads)},{"threads",Json(static_cast<std::int64_t>(after.process.memory.threads))},
            {"poolWorkersDelta",delta(after.pool,before.pool)},{"poolWorkers",Json(static_cast<std::int64_t>(after.pool))},
            {"handlesDelta",delta(after.process.memory.handles,before.process.memory.handles)},
            {"maxFrameCPUMilliseconds",frameMax},{"meanFrameCPUMilliseconds",frameCount?frameTotal/frameCount:0.},{"switchEndFrameCPUMilliseconds",afterSwitch}});
    }
    recordMemory("after-use",true);
    perform(lifecycle.toggle(),t);until(core::SystemOverlayPhase::closed,"Traced closing completes");deadlines(3);idle();
    recordMemory("closed",true);
    for(unsigned cycle=0;cycle<10;++cycle){
        perform(lifecycle.toggle(),t);until(core::SystemOverlayPhase::open,"Traced reopening completes");frames(10);
        perform(lifecycle.toggle(),t);until(core::SystemOverlayPhase::closed,"Traced reclosing completes");deadlines(1);
    }
    deadlines(2);idle();
    recordMemory("after-cycles",true);
    writeMemoryTrace();std::cout<<"Wrote hidden memory trace\n";
    shutdownVisible();
    return 0;
}

// watch_session_preview --idle-trace: the production owner with the module
// coverage inputs, hidden, on the REAL clock. Frames come from the production
// scheduling - OverlayHost's message pump, waitable timer, source frame gate
// and display pacing at the traced refresh (its never-shown window is
// presented for scheduling only) - and render offscreen on the hardware GPU
// (no swap chain, compositor or backdrop: those exist only on a desktop).
// Each window of idleTraceSeconds records what the section 9 idle gates
// measure (wake-ups = context switches, CPU of one core) per thread, plus the
// frames, their cadence, their CPU per stage and owner, the uploads they made
// and what demanded them: closed after launch, open idle with the ambient
// animation on and off, and closed again after a close.
int Application::Impl::runIdleTrace(){
    if(args.idleTrace.empty())throw std::logic_error("Idle trace requires its output");
    const double seconds=args.idleTraceSeconds;
    need(std::isfinite(seconds)&&seconds>=1&&seconds<=600,"Idle trace windows are 1...600 seconds");
    const auto clock=[&]{const double t=hostTime(OverlayHost::clockNow());hiddenClock=std::max(hiddenClock,t);return t;};
    // The frame stages and owner costs of every traced frame (LiveProbe),
    // without the live diagnostic script.
    diagnosticFinished=true;probe.enabled=true;
    // What asked for finite (display-paced) frames: counted after every
    // traced frame whose demand was finite, per requesting owner.
    std::map<std::string,unsigned,std::less<>> finiteRequests;
    const auto requesters=[&](double at,const auto&visit){
        registry.forEach([&](ModuleOwner&o){if(o.requiresFrames(hostTime(at)))visit(o.name());},"trace");
        if(mediaBroker&&mediaBroker->requiresFrames())visit("media");
        if(chrome&&chrome->styleTransitionActive())visit("clock style");
        if(session->demand(at).finiteAnimation)visit("source shell");
    };
    const auto pump=[&](double duration,const std::function<bool()>&until){
        const double end=OverlayHost::clockNow()+duration;
        for(;;){const double left=end-OverlayHost::clockNow();if(left<=0||(until&&until()))return;
            const auto frames=host.stats().frames;
            need(host.pumpOnce(static_cast<std::uint32_t>(std::ceil(left*1000))),"The traced host stopped");
            if(host.stats().frames!=frames&&session->phase()==core::VisibilityPhase::visible){const auto at=clock();
                if(demand(at).finiteAnimation)requesters(at,[&](std::string_view name){++finiteRequests[std::string(name)];});}}
    };
    // Production selects Map at construction and applies the preference
    // record; the launch prewarm and the post-launch work finish first.
    double t=clock();
    if(notes&&notes->selected()!=core::Module::map)notes->select(core::Module::map,t);
    configuration.set("ambientAnimation",true);configurationPending=true;applyConfiguration(t);
    prewarm();if(utility){utility->waitIdle();if(longWork)longWork->waitIdle();utilityCompleted();}
    // The closed HUD's graphics are released after the grace (production
    // host deadline, ten minutes; the trace may shorten it): the closed
    // windows and the first opening follow a close of closedFor seconds,
    // just past the grace unless the trace asks for a shorter close (a
    // closed HUD that still keeps its device, and a warm opening).
    const double grace=closedGraphicsGrace(),closedFor=args.idleTraceClosedSeconds.value_or(grace+1);const bool releaseExpected=closedFor>grace;
    need(std::isfinite(closedFor)&&closedFor>=1&&(!releaseExpected||closedFor>=grace+.5),"The traced close is past the grace by half a second, or within it");
    const auto deviceAlive=[&]{const auto s=renderer.stats();return std::int64_t(s.devicesCreated-std::min(s.devicesCreated,s.devicesDestroyed));};
    pump(closedFor,{});
    Json::Array windows;
    const auto measure=[&](const char*name){
        const auto snapshot=[&]{return Snapshot{renderer.stats(),renderer.sourceGraphicsStats(),rasterizer.stats(),session->frameStats(),session->stats()};};
        std::map<std::string,std::uint64_t,std::less<>> drawUploads;
        renderer.forEachDrawUpload([&](std::string_view id,std::uint64_t uploads){drawUploads[std::string(id)]=uploads;});
        const auto beforeHost=host.stats();const auto before=snapshot();const auto beforeThreads=captureThreadActivity();const auto beforeCPU=processUsage().cpuSeconds;
        probe.timings={};probe.owners={};probe.ownerCount=0;finiteRequests.clear();
        const double start=OverlayHost::clockNow();pump(seconds,{});const double elapsed=OverlayHost::clockNow()-start;
        const auto afterThreads=captureThreadActivity();const auto afterCPU=processUsage().cpuSeconds;const auto after=snapshot();const auto afterHost=host.stats();
        const auto frames=afterHost.frames-beforeHost.frames;
        constexpr std::array stageNames{"frame","backdrop","sourcePose","materials","nativeLabels","notesPose","publication","draw","pointer","key","message","editTransition","prepare"};
        Json::Object stages;
        for(unsigned n=0;n<LiveProbe::count;++n){const auto&s=probe.timings[n];if(!s.calls)continue;
            stages[stageNames[n]]=Json::Object{{"calls",std::int64_t(s.calls)},{"meanMS",s.total/double(s.calls)},{"maxMS",s.maximum},{"percentOfOneCore",s.total/10/elapsed}};}
        std::array<std::size_t,64>order{};for(std::size_t n=0;n<probe.ownerCount;++n)order[n]=n;
        std::sort(order.begin(),order.begin()+probe.ownerCount,[&](std::size_t a,std::size_t b){return probe.owners[a].update.total+probe.owners[a].upload.total>probe.owners[b].update.total+probe.owners[b].upload.total;});
        Json::Array owners;
        for(std::size_t n=0;n<probe.ownerCount&&n<10;++n){const auto&o=probe.owners[order[n]];
            owners.push_back(Json::Object{{"owner",std::string(o.name)},{"updateMeanMS",o.update.calls?o.update.total/double(o.update.calls):0.},{"uploadMeanMS",o.upload.calls?o.upload.total/double(o.upload.calls):0.},
                {"percentOfOneCore",(o.update.total+o.upload.total)/10/elapsed}});}
        // What asks for frames at the end of the window.
        const auto at=clock();const auto wanted=demand(at);Json::Array requesting;
        requesters(at,[&](std::string_view owner){requesting.push_back(std::string(owner));});
        Json::Object finiteFrames;for(const auto&[owner,count]:finiteRequests)finiteFrames[owner]=std::int64_t(count);
        // The published draws whose constants changed most often in the window.
        std::vector<std::pair<std::uint64_t,std::string>> changedDraws;
        renderer.forEachDrawUpload([&](std::string_view id,std::uint64_t uploads){const auto found=drawUploads.find(id);
            const auto delta=uploads-(found==drawUploads.end()?0:std::min(uploads,found->second));if(delta)changedDraws.emplace_back(delta,std::string(id));});
        std::sort(changedDraws.begin(),changedDraws.end(),std::greater<>());
        Json::Array changing;for(std::size_t n=0;n<changedDraws.size()&&n<12;++n)changing.push_back(Json::Object{{"draw",changedDraws[n].second},{"uploads",std::int64_t(changedDraws[n].first)}});
        const char*phases[]{"concealed","opening","visible","closing"};
        windows.push_back(Json::Object{{"window",name},{"seconds",elapsed},
            {"cpuPercentOfOneCore",(afterCPU-beforeCPU)*100/elapsed},
            {"activity",threadActivityJSON(beforeThreads,afterThreads,elapsed,12)},
            {"frames",Json::Object{{"count",std::int64_t(frames)},{"perSecond",double(frames)/elapsed},{"displayPaced",std::int64_t(afterHost.pacedFrames-beforeHost.pacedFrames)},
                {"timerWakesPerSecond",double(afterHost.timerWakes-beforeHost.timerWakes)/elapsed},
                {"deadlineWakesPerSecond",double(afterHost.externalWakes-beforeHost.externalWakes)/elapsed},
                {"frameTimerWakes",std::int64_t((afterHost.timerWakes-beforeHost.timerWakes)-std::min(afterHost.timerWakes-beforeHost.timerWakes,afterHost.externalWakes-beforeHost.externalWakes))},
                {"frameRequests",std::int64_t(afterHost.frameRequests-beforeHost.frameRequests)},
                {"timerArmedAtEnd",afterHost.timerArmed},{"framePendingAtEnd",afterHost.framePending},
                {"drawMeanMS",frames?(after.renderer.drawSeconds-before.renderer.drawSeconds)*1000/double(frames):0.},
                {"nativePassMeanMS",frames?(after.renderer.nativePassSeconds-before.renderer.nativePassSeconds)*1000/double(frames):0.},
                {"drawListMeanMS",frames?(after.renderer.drawListSeconds-before.renderer.drawListSeconds)*1000/double(frames):0.},
                {"sourcePassMeanMS",frames?(after.graphics.renderSeconds-before.graphics.renderSeconds)*1000/double(frames):0.},
                {"sourceUploadMeanMS",frames?(after.graphics.uploadSeconds-before.graphics.uploadSeconds)*1000/double(frames):0.},
                {"drawCallsPerFrame",frames?double(after.renderer.drawCalls-before.renderer.drawCalls)/double(frames):0.},
                {"nativeGroupRenders",std::int64_t(after.renderer.nativeGroupRenders-before.renderer.nativeGroupRenders)},
                {"sourceFrames",std::int64_t(after.graphics.frames-before.graphics.frames)}}},
            {"stages",std::move(stages)},{"owners",std::move(owners)},{"uploads",counters(before,after)},{"changingDraws",std::move(changing)},
            {"graphics",Json::Object{{"deviceSuspended",after.renderer.deviceSuspended},{"suspendedCopyBytes",std::int64_t(after.renderer.suspendedCopyBytes)},
                {"deviceSuspensions",std::int64_t(after.renderer.deviceSuspensions)},{"deviceResumes",std::int64_t(after.renderer.deviceResumes)},
                // Device objects alive (an outside holder keeps a suspended one alive) and the media holders.
                {"devicesAlive",deviceAlive()},{"mediaDeviceLent",after.renderer.mediaDeviceLent},{"mediaTargets",std::int64_t(after.renderer.mediaTargets)},
                {"notesVideoDeviceHeld",mediaBroker?mediaBroker->videoStats().deviceHeld:false},{"releaseRefused",idleGraphicsRefused},
                {"privateBytes",std::int64_t(processUsage().privateBytes)}}},
            {"demand",Json::Object{{"phase",phases[static_cast<unsigned>(wanted.phase)]},{"finiteAnimation",wanted.finiteAnimation},{"ambientEnabled",wanted.ambientEnabled},
                {"lowPower",wanted.lowPower},{"reduceMotion",wanted.reduceMotion},{"requesting",std::move(requesting)},{"finiteFramesBy",std::move(finiteFrames)}}}});
        std::cout<<"Idle trace window "<<name<<": "<<double(frames)/elapsed<<" frames/s, "<<(afterCPU-beforeCPU)*100/elapsed<<"% of one core\n";
    };
    const auto opened=[&]{return lifecycle.phase()==core::SystemOverlayPhase::open;};
    const auto closed=[&]{return lifecycle.phase()==core::SystemOverlayPhase::closed&&session->phase()==core::VisibilityPhase::concealed;};
    const auto ambient=[&](bool on){configuration.set("ambientAnimation",on);configurationPending=true;applyConfiguration(clock());host.setFrameDemand(demand(clock()));host.invalidate();};
    const auto openHUD=[&]{perform(lifecycle.toggle(),clock());host.show(false);host.setFrameDemand(demand(clock()));pump(10,opened);need(opened(),"The traced HUD opened");};
    const auto closeHUD=[&]{perform(lifecycle.toggle(),clock());host.setFrameDemand(demand(clock()));host.invalidate();pump(10,closed);need(closed(),"The traced HUD closed");};
    // An opening as the summon hotkey asks for it (OverlayController toggle):
    // the time to its first frame and to the open phase.
    struct Opening {double firstFrame{-1},opened{},resume{};std::uint64_t resumes{};};
    const auto timedOpening=[&]{const auto before=renderer.stats();const auto frames=host.stats().frames;const auto at=OverlayHost::clockNow();Opening o;
        perform(lifecycle.toggle(),clock());host.show(false);host.setFrameDemand(demand(clock()));
        pump(10,[&]{if(o.firstFrame<0&&host.stats().frames!=frames)o.firstFrame=OverlayHost::clockNow()-at;return opened();});
        need(opened(),"The traced HUD opened");const auto after=renderer.stats();
        o.opened=OverlayHost::clockNow()-at;o.resume=after.deviceResumeSeconds-before.deviceResumeSeconds;o.resumes=after.deviceResumes-before.deviceResumes;return o;};
    measure("closed-after-launch");
    // The opening after that close: a released device comes back first
    // (prepared on a worker at the opening, Renderer::resumeDevice), then
    // the first frame; within the grace it is a warm opening.
    const auto first=timedOpening();
    need(first.resumes==(releaseExpected?1u:0u),releaseExpected?"The opening after a long close creates the device again":"An opening within the grace keeps the device");
    pump(2,{});measure("open-idle-ambient-on");
    ambient(false);pump(2,{});measure("open-idle-ambient-off");
    ambient(true);pump(1,{});
    closeHUD();pump(closedFor,{});measure("closed");
    // The reopening after that close (of closedFor seconds and the closed
    // window): past the grace it creates the device again, within the grace
    // it is as warm as a quick reopening.
    need(renderer.deviceSuspended()==releaseExpected&&(!releaseExpected||deviceAlive()==0),
         releaseExpected?"After the grace the closed HUD's device is released and destroyed (no media owner keeps it)":"Within the grace the closed HUD keeps its device");
    const auto reopening=timedOpening();
    need(reopening.resumes==(releaseExpected?1u:0u),releaseExpected?"The reopening after a long close creates the device again":"A reopening within the grace keeps the device");
    Json::Object anticipated,refusal,discard;
    if(releaseExpected){
        closeHUD();pump(closedFor,{});need(renderer.deviceSuspended(),"The device is released again after the grace");
        // The tray icon pointed at: the device is created on a worker while
        // the menu is used (a few hundred milliseconds), then the opening.
        prepareGraphics(true);need(renderer.devicePrepared()&&preparedGraphicsUntil.has_value(),"Pointing at the tray icon prepares the released device");
        pump(.35,{});const auto early=timedOpening();
        need(early.resumes==1&&!preparedGraphicsUntil,"The anticipated opening takes the prepared device");
        anticipated=Json::Object{{"firstFrameSeconds",early.firstFrame},{"openedSeconds",early.opened},{"deviceResumeSeconds",early.resume},{"anticipationSeconds",.35}};
        // A media owner still holding the device at the deadline (a movie
        // playing, an edit pass) refuses the release; it follows shortly
        // after the owner lets go. (Same-device media exists with the shared
        // media stack.)
        closeHUD();
        if(mediaBroker){auto lease=renderer.mediaDevice();pump(grace+1,{});
            need(!renderer.deviceSuspended()&&idleGraphicsRefused&&renderer.stats().mediaDeviceLent,"A media owner holding the device refuses the release");
            const auto letGo=OverlayHost::clockNow();lease.reset();scheduleDeadline(); // the owner's event ends with the host's scheduling
            pump(std::min(closedGraphicsRelease,grace)+1.5,[&]{return renderer.deviceSuspended();});
            need(renderer.deviceSuspended()&&deviceAlive()==0,"Once the media owner let go the device is released");
            refusal=Json::Object{{"refusedAtGrace",true},{"releasedSecondsAfterLetGo",OverlayHost::clockNow()-letGo}};}
        else{pump(grace+1,{});need(renderer.deviceSuspended(),"The closed HUD's device is released again after the grace");}
        // The tray icon pointed at without an opening: the prepared device
        // is discarded, so no device stays alive in the closed HUD.
        const auto discards=renderer.stats().preparedDeviceDiscards;prepareGraphics(true);need(renderer.devicePrepared(),"Pointing at the tray icon prepares the device");
        pump(preparedGraphicsLifetime+1,[&]{return !renderer.devicePrepared();});
        need(!renderer.devicePrepared()&&renderer.stats().preparedDeviceDiscards==discards+1&&renderer.deviceSuspended()&&deviceAlive()==0,"An anticipated device no opening took is discarded");
        discard=Json::Object{{"discarded",true},{"lifetimeSeconds",preparedGraphicsLifetime}};
        openHUD();
    }
    // A reopening within the grace keeps the device and the presentation.
    closeHUD();pump(1,{});
    const auto warm=timedOpening();
    need(warm.resumes==0,"A reopening within the grace keeps the device");
    closeHUD();pump(1,{});
    const auto stats=host.stats();
    const Json report=Json::Object{{"schema",2},
        {"scope","EndfieldHUD hidden idle trace: wake-ups, CPU and frames per window on the real clock through the production frame scheduling; numbers, owner and system module names only"},
        {"device",renderer.deviceInfo().name},{"warp",args.warp},{"refreshHz",stats.refreshRate},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},
        {"closedGraphicsGraceSeconds",grace},{"productionClosedGraphicsGraceSeconds",closedGraphicsGraceSeconds},{"closedSeconds",closedFor},{"releaseExpected",releaseExpected},
        {"sharedMedia",bool(mediaBroker)},
        {"openingSeconds",first.opened},{"openingFirstFrameSeconds",first.firstFrame},{"openingDeviceResumeSeconds",first.resume},
        {"deviceResume",Json::Object{{"resumes",std::int64_t(renderer.stats().deviceResumes)},{"deviceSeconds",renderer.stats().deviceCreateSeconds},{"programSeconds",renderer.stats().pipelineCreateSeconds},
            {"resourceSeconds",renderer.stats().resourceRestoreSeconds},{"sourceSeconds",renderer.stats().sourceRestoreSeconds}}},
        {"reopeningAfterClose",Json::Object{{"closedSeconds",closedFor+seconds},{"firstFrameSeconds",reopening.firstFrame},{"openedSeconds",reopening.opened},{"deviceResumeSeconds",reopening.resume},{"deviceResumes",std::int64_t(reopening.resumes)}}},
        {"anticipatedOpening",std::move(anticipated)},{"mediaRefusal",std::move(refusal)},{"anticipationDiscard",std::move(discard)},
        {"quickReopeningFirstFrameSeconds",warm.firstFrame},{"windows",std::move(windows)}};
    const std::size_t bound=8*1024*1024;ehud::data::detail::replaceFile(args.idleTrace,std::nullopt,report.encode(bound),bound);
    std::cout<<"Wrote hidden idle trace\n";
    shutdownVisible();
    return 0;
}

int Application::Impl::runHiddenVerification(){
    if(args.mode!=ApplicationMode::hiddenBenchmark)throw std::logic_error("Hidden verification requires the hidden benchmark mode");
    if(!args.idleTrace.empty())return runIdleTrace();
    if(!args.startupTrace.empty())return runStartupTrace();
    if(!args.contentionTrace.empty())return runContentionTrace();
    if(!args.memoryTrace.empty())return runMemoryTrace();
    const auto start=args.benchmarkEpoch;const auto deviceInfo=renderer.deviceInfo();unsigned lifecycleChecks{};
        auto snapshot=[&]{return Snapshot{renderer.stats(),renderer.sourceGraphics().stats(),rasterizer.stats(),session->frameStats(),session->stats()};};Json::Array rows,images;
        if(!args.snapshots.empty()){need(fs::create_directory(args.snapshots),"Cannot create new snapshot directory");ehud::data::detail::validateRoot(args.snapshots);}
        auto saveTarget=[&](const char*name,double time){if(args.snapshots.empty())return;const auto image=renderer.readback();const auto file=std::string(name)+".raw-bgra.bin";
            const std::string bytes(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());
            ehud::data::detail::replaceFile(args.snapshots/file,std::nullopt,bytes,128*1024*1024);
            images.push_back(Json::Object{{"state",name},{"file",file},{"width",int(image.width)},{"height",int(image.height)},{"rowBytes",int(image.rowBytes)},{"bytes",std::int64_t(bytes.size())},{"sha256",packet::sha256(image.pixels)},{"time",time},{"canvasOpacity",canvasOpacity(time)},{"encoding","BGRA8 encoded-sRGB owned target; original blend output retains emissive RGB beyond alpha"}});};
        auto measure=[&](const char*name,unsigned count,auto&&step){const auto before=snapshot();const auto processBefore=processUsage();double cpu=0,maximum=0;for(unsigned i=0;i<count;++i){const auto begin=Clock::now();step(i);const double ms=milliseconds(begin);cpu+=ms;maximum=std::max(maximum,ms);}const auto processAfter=processUsage();auto result=counters(before,snapshot());result["processCPUSeconds"]=processAfter.cpuSeconds-processBefore.cpuSeconds;result["processMemoryBefore"]=memoryJSON(processBefore);result["processMemoryAfter"]=memoryJSON(processAfter);result["name"]=name;result["samples"]=int(count);result["cpuMilliseconds"]=cpu;result["meanCPUMilliseconds"]=cpu/count;result["maxCPUMilliseconds"]=maximum;rows.push_back(std::move(result));};
        auto at=[&](double elapsed){return start+elapsed;};
        open(at(0));measure("opening",61,[&](unsigned i){present(at(i/60.),true);});
        settings.ambientEnabled=false;session->setSettings(settings,at(2));session->pointerMove({},at(2));present(at(3),true);saveTarget("stable",at(3));
        measure("forced-stable-idle",120,[&](unsigned i){present(at(4+i/60.),true);});
        need(!session->demand(at(6)).finiteAnimation&&!session->demand(at(6)).ambientEnabled,"Stable ambient-off session requested animation");
        session->pointerMove(core::Point{-10,20},at(7));present(at(7),true);
        measure("changing-pointer-outside-controls",120,[&](unsigned i){session->pointerMove(core::Point{-10,20.+i*4},at(8+i/60.));present(at(8+i/60.),true);});
        saveTarget("tilted",at(8+119/60.));
        session->pointerMove({},at(11));present(at(12),true);const double scrollBefore=session->scrollPosition();need(session->scrollDirection(1,true,at(12)),"Top-position navigation did not accept downward scroll");
        measure("navigation-scroll",90,[&](unsigned i){present(at(12+i/60.),true);});
        const double scrollAfter=session->scrollPosition();need(scrollAfter<scrollBefore-1e-6,"Navigation scroll benchmark did not move downward");
        close(at(14));measure("closing",31,[&](unsigned i){present(at(14+i/60.),true);});
        measure("concealed",120,[&](unsigned i){need(!session->sample(at(15+i/60.)),"Concealed session emitted geometry");need(!session->demand(at(15+i/60.)).presented,"Concealed session requested presentation");});
        unsigned coverageOpening{},coverageButtons{};Json::Array coverageResults;
        if(args.coverage){
            settings.ambientEnabled=true;session->setSettings(settings,at(20));session->setScrollPosition(1,at(20));open(at(20));
            // Deliberately non-cadence samples: live first paint need not occur
            // at time zero or an exact 60 Hz fraction. No global input is sent.
            for(const double elapsed:{0.,.000001,.00013,.0037,.0091,.0173,.0239,.0371,.0613,.0977,.1331,.1719,.2197,.2713,.3337,.4191,.5373,.6817,.8193,1.}){
                try{present(at(20+elapsed),true);}catch(const std::exception&e){throw std::runtime_error("Hidden irregular opening at "+std::to_string(elapsed)+": "+e.what());}++coverageOpening;
            }
            session->setInputEnabled(true,at(21));environment.pointerLocked=true;session->setEnvironment(environment,at(21));present(at(21.5),true);const auto*sample=session->currentFrame();need(sample!=nullptr,"Coverage has no stable source frame");
            std::vector<std::pair<std::string,core::Point>> points;std::set<std::string,std::less<>> seen;
            for(const auto&hit:sample->sourceFrame->hits){
                if(!session->actions().contains(hit.buttonID)||seen.contains(hit.buttonID))continue;
                const core::Point center{hit.rect.origin[0]+hit.rect.size[0]*.5,hit.rect.origin[1]+hit.rect.size[1]*.5};
                const auto p=source::projectNativePoint({center.x,center.y,0},hit.world,sample->camera.projection*sample->camera.view,{0,0,metrics.width,metrics.height});
                if(!p)continue;const auto winner=sample->sourceFrame->buttonAt(*p,sample->camera.projection*sample->camera.view,{0,0,metrics.width,metrics.height});
                if(winner&&*winner==hit.buttonID){seen.insert(hit.buttonID);points.emplace_back(hit.buttonID,*p);}
            }
            need(!points.empty(),"Coverage found no actual visible source hit targets");
            for(std::size_t index=0;index<points.size();++index){const auto time=at(22+static_cast<double>(index));const auto&[id,point]=points[index];
                try{session->pointerMove(point,time);present(time,true);present(time+.0713,true);present(time+.2501,true);session->pointerDown(point,time+.3);present(time+.30001,true);present(time+.4713,true);(void)session->pointerUp(point,time+.6);present(time+.8001,true);}
                catch(const std::exception&e){throw std::runtime_error("Hidden hovered/pressed source button "+id+": "+e.what());}coverageResults.push_back(Json::Object{{"sourceHitButton",id},{"hoveredAfterRelease",session->hovered()?std::string(*session->hovered()):std::string{}}});++coverageButtons;
            }
            const auto finish=at(23+static_cast<double>(points.size()));session->pointerMove({},finish);close(finish);present(finish+.7,true);
        }
        Json::Array moduleResults,areaModuleResults,areaPresentedModules;Json displayAdaptation,scrollArrow;
        // Module areas (app/module_owner.hpp): coverage steps and their checks.
        struct AreaCounts{unsigned steps{},checks{};};std::map<std::string,AreaCounts,std::less<>> areaCounts;
        double areaLatest{};
        const auto areaFor=[&](const ModuleOwner*owner)->ExternalModule*{for(auto&area:externalModules)if(area.owner.get()==owner)return &area;return nullptr;};
        const auto runArea=[&](ExternalModule&area,std::optional<core::Module> module,unsigned cycle,double begin){
            if(!registry.enabled(*area.owner))return;
            // Coverage steps run on the one host time (ModuleRegistry::hostTime).
            auto&counts=areaCounts[area.name];double last=hostTime(std::max(begin,areaLatest));
            ModuleCoverage step;step.module=module;step.cycle=cycle;step.time=last;
            // A host frame: the shared design deadlines due by then (sub-page
            // swaps, toasts; the overlay owner runs the Context's), then the draw.
            step.present=[&](double at){need(at>=last,"Area coverage presents on a non-decreasing clock");last=at;
                if(designOverlay)if(const auto wake=designOverlay->nextWakeTime(at);wake&&*wake<=at)(void)designOverlay->deadline(at);present(at,true);};
            step.drain=[&]{drainUtility(last);};
            step.project=[&,module](core::Point local,double width,double height)->std::optional<core::Point>{
                if(!module||!notes||!chromePlan||!chromePlan->projection().center||!(width>0)||!(height>0))return std::nullopt;
                gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,width,height}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,*module);
                const auto ps=chromeSettings(*module);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                const auto screen=plane.project(local);if(!screen)return std::nullopt;return core::Point{screen->x/metrics.scale,screen->y/metrics.scale};};
            step.check=[&](bool value,std::string_view why){++counts.checks;if(!value)throw std::runtime_error(std::string(why));};
            step.message=[&](std::uint32_t id,std::uintptr_t wParam,std::intptr_t lParam){
                const NativeMessage m{host.hwnd(),id,wParam,lParam};
                return registry.first(ModuleRegistry::Chain::message,[&](ModuleOwner&o,const ModuleRouting&){return o.message(m,hostTime(last));},"message")!=nullptr;};
            ++counts.steps;
            try{area.owner->coverage(step);}catch(const std::exception&e){throw std::runtime_error("Module area "+area.name+" coverage: "+e.what());}
            areaLatest=std::max(areaLatest,last);
        };
        if(args.moduleCoverage){
            double time=at(100);
            // Host integration checks (counted in hostIntegrationChecks) and the
            // UI Automation helpers they share.
            const auto hostCheck=[&](bool value,const std::string&why){need(value,why.c_str());++hostChecks;};
            // Build-time hud.hlsl bytecode (native/renderer.shaders.cmake) replaced the launch-time compilation.
            if(gpu::Renderer::shadersEmbedded())hostCheck(renderer.stats().embeddedShaders,"The renderer creates its programs from the build-time hud.hlsl bytecode");
            // The launch prewarm (run here while concealed, as after launch) poses
            // the first opening offscreen and leaves the HUD concealed and hidden.
            hostCheck(!prewarmed&&session->phase()==core::VisibilityPhase::concealed,"The HUD is concealed before the launch prewarm");
            prewarm();
            hostCheck(prewarmed&&session->phase()==core::VisibilityPhase::concealed&&!session->currentFrame()&&!host.stats().visible&&!IsWindowVisible(static_cast<HWND>(host.hwnd())),
                "The launch prewarm poses the first opening offscreen and conceals the HUD again");
            // The Map owner prewarms its canvas page and surface (MapOwner::prewarm):
            // nothing is activated and no terrain is painted before the opening.
            if(map&&mapModuleOwner&&registry.enabled(*mapModuleOwner)&&notes->selected()==core::Module::map)
                hostCheck(map->sceneStats().pinBuilds>=1&&!map->state().active()&&!map->rasterStats().active,"The launch prewarm builds the Map without activating it");
            // An Archive that is not the first opening's module builds nothing at
            // the launch prewarm or in the opening's frames (no rasters attributed
            // to it): its view is built when Archive is first shown.
            const auto archiveRasters=[&]{std::size_t entries{};rasterizer.forEachEntry([&](const gpu::LayerRasterEntryUsage&e){if(e.owner=="archive")++entries;});return entries;};
            const bool archiveLater=archive&&archiveModuleOwner&&registry.enabled(*archiveModuleOwner)&&notes->selected()!=core::Module::archive;
            if(archiveLater)hostCheck(archiveRasters()==0,"The launch prewarm builds no Archive artwork while another module opens first");
            open(time);present(time+1,true);time+=2;
            if(archiveLater)hostCheck(archiveRasters()==0,"The first opening on another module builds no Archive artwork");
            // Before any module is shown (every owner receives these real host
            // calls). The ONE host time (ModuleRegistry::hostTime): an owner whose
            // deadline() dispatches a nested event with a later time (a
            // capture-lost or activation message inside a live call, an
            // owner reading the clock) is followed by the host's later
            // calls with the earlier wake sample. No owner may then receive
            // a time earlier than one it already saw (the live RCs died with
            // "Event persistence requires finite monotonic owner time" and
            // "Projection controls require a finite monotonic owner clock").
            {
                struct ClockProbe final:ModuleOwner{
                    std::string id;ModuleRouting route;mutable double latest{-1};mutable unsigned calls{},regressions{};
                    std::function<void(double)>nestedDeadline,nestedMessage;std::uint32_t consumes{};
                    explicit ClockProbe(std::string n):id(std::move(n)){route.message=0;}
                    void see(double t)const{++calls;if(!std::isfinite(t)||t<latest)++regressions;latest=std::max(latest,t);}
                    std::string_view name()const noexcept override{return id;}
                    ModuleRouting routing()const noexcept override{return route;}
                    void setAppearance(const ModuleAppearance&,double t)override{see(t);}
                    void setOverlayVisible(bool,double t)override{see(t);}
                    void overlayClosing(double t)override{see(t);}
                    void update(const ModuleFrame&f)override{see(f.time);}
                    bool requiresFrames(double t)const override{see(t);return false;}
                    std::optional<double>nextWakeTime(double t)const override{see(t);return std::nullopt;}
                    bool deadline(double t)override{see(t);if(auto f=std::exchange(nestedDeadline,{}))f(t);return false;}
                    bool message(const NativeMessage&m,double t)override{see(t);if(auto f=std::exchange(nestedMessage,{}))f(t);return consumes&&m.message==consumes;}
                    void focus(bool,double t)override{see(t);}
                    bool hostEvent(const HostEvent&,double t)override{see(t);return false;}
                };
                ClockProbe first("hostClockProbeFirst"),second("hostClockProbeSecond");registry.add(first);registry.add(second);
                // The wake: the earlier owner's deadline dispatches a later host event.
                const double wake=hostTime(time);
                first.nestedDeadline=[&](double t){broadcast(HostEvent::Kind::settingsChanged,t+.25);};
                onDeadline(wake);
                hostCheck(first.regressions==0&&second.regressions==0&&second.calls>=2&&second.latest>=wake+.25,
                    "After a nested later event inside an owner's deadline, every later owner and the host's own calls receive the same or a later time");
                // A WM_APP notice: the first owner in the message chain dispatches a
                // later deadline, the second consumes the notice afterwards.
                const auto id=areaHost?areaHost->registerMessage():std::uint32_t(WM_APP+0x3fff);second.consumes=id;
                first.nestedMessage=[&](double t){onDeadline(t+.5);};
                const auto latest=second.latest;
                (void)onAppMessage(NativeMessage{host.hwnd(),id,0,0});
                hostCheck(first.regressions==0&&second.regressions==0&&second.latest>=latest+.5,
                    "A message routed after a nested later deadline is handed the later host time");
                // The registry's own clamps (frame demand, wake scheduling, broadcasts).
                (void)registry.requiresFrames(wake);(void)registry.nextWakeTime(wake);(void)registry.broadcast({HostEvent::Kind::settingsChanged,0},wake);
                hostCheck(first.regressions==0&&second.regressions==0,"Frame demand, wake scheduling and broadcasts never hand an earlier time");
                registry.remove(first);registry.remove(second);
                time=std::max(time,hostTime(time))+.5;
            }
            using Microsoft::WRL::ComPtr;
            const auto textProperty=[&](IUnknown*element,PROPERTYID id)->std::string{
                ComPtr<IRawElementProviderSimple>simple;std::string out;if(!element||FAILED(element->QueryInterface(IID_PPV_ARGS(&simple))))return out;
                VARIANT v;VariantInit(&v);if(SUCCEEDED(simple->GetPropertyValue(id,&v))&&v.vt==VT_BSTR&&v.bstrVal)out=narrow(v.bstrVal);VariantClear(&v);return out;};
            // Reader, Map, Calendar and the minigame: their peers press like a click.
            const auto peerProbe=[&](core::Module module,ModuleOwner*owner,std::initializer_list<std::pair<std::string_view,std::function<bool()>>>presses){
                if(!owner||!registry.enabled(*owner))return;
                selectModule(module,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                hostCheck(owner->automationCount()>0,"A local module lists its projected UI Automation peers under the HUD root");
                for(const auto&[id,after]:presses){
                    ComPtr<IRawElementProviderFragment>peer;
                    for(std::size_t n=0;n<owner->automationCount()&&!peer;++n){
                        ComPtr<IRawElementProviderFragment>f;
                        if(SUCCEEDED(owner->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)==id)peer=f;}
                    ComPtr<IInvokeProvider>invoke;
                    hostCheck(peer&&SUCCEEDED(peer.As(&invoke))&&invoke->Invoke()==S_OK&&after(),"A local module's peer "+std::string(id)+" presses like its click");
                    // A press that swaps the page (Calendar's month) re-lists the peers
                    // once the swap lands on the shared design deadlines: frames for .5 s.
                    for(unsigned frame=0;frame<=30;++frame){const double t=time+double(frame)/60;
                        if(designOverlay)if(const auto wake=designOverlay->nextWakeTime(t);wake&&*wake<=t)(void)designOverlay->deadline(t);present(t,true);}
                    time+=.5;(void)probe.refresh();}
                probe.disconnectOwners();hostCheck(owner->automationCount()==0,"Concealing disconnects the local module's peers");probe.shutdown();};
            std::vector modules{core::Module::notes,core::Module::fileShelf,core::Module::clipboard,core::Module::volume,core::Module::eventLog,core::Module::workMode,core::Module::power};if(archive)modules.push_back(core::Module::archive);if(storage)modules.push_back(core::Module::storage);if(activity)modules.push_back(core::Module::activityMonitor);if(reader)modules.push_back(core::Module::reader);if(calendar)modules.push_back(core::Module::calendar);if(map)modules.push_back(core::Module::map);if(!args.orbipomAssets.empty())modules.push_back(core::Module::minigame);if(settingsUI)modules.insert(modules.end(),{core::Module::system,core::Module::display,core::Module::hotkeys,core::Module::about});
            // Every HUD module an area presents joins the same combined cycles.
            std::vector<core::Module> areaModules;
            for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n){const auto module=static_cast<core::Module>(n);
                if(std::find(modules.begin(),modules.end(),module)==modules.end()&&areaFor(registry.presenter(module))){areaModules.push_back(module);areaPresentedModules.push_back(std::string(core::moduleIdentifier(module)));}}
            modules.insert(modules.end(),areaModules.begin(),areaModules.end());
            std::optional<gpu::LayerRasterStats> retainedCycle;
            for(unsigned cycle=0;cycle<3;++cycle)for(const auto module:modules){
                std::size_t scrollChanges{},peakEntries{},peakBytes{};
                try{selectModule(module,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);
                    if(module==core::Module::minigame&&game){
                        // OrbiPomHUDVerification (Mac 1.3.0): showing the game starts the
                        // original session on its clock; only the visible module's tick
                        // (the owner's deadline) advances it; manual pause stops it.
                        const auto tick=[&](double t){(void)gameModuleOwner->deadline(t);present(t,true);};
                        need(gameSession&&game->isActive(),"Minigame shares the selected design module surface");
                        if(cycle==0){
                            need(gameSession->hasRuntime()&&gameSession->snapshot().isPlaying()&&!gameSession->error()&&game->hasFrameTimer()&&!gameSession->manuallyPaused(),
                                 "Showing the minigame starts the bundled unchanged engine on its clock");
                            need(gameSession->bestScore()==71&&gameSession->snapshot().highScore==71,"Original best-score key loads from the isolated shared preferences");
                            const auto began=gameSession->snapshot().simulationTime;
                            for(unsigned n=0;n<30;++n)tick(time+.72+double(n)/60);
                            need(gameSession->snapshot().simulationTime>began,"Only the visible minigame's tick advances original physics");
                            game->perform("pause");
                            const auto stopped=gameSession->snapshot().simulationTime;
                            tick(time+1.5);tick(time+1.7);
                            need(gameSession->manuallyPaused()&&!game->hasFrameTimer()&&gameSession->snapshot().simulationTime==stopped,"Manual pause advances no game steps");
                            settingsUI->controller().restoreDefaults();need(settingsSaves->flush(),"Isolated settings reset saves through the shared executor");
                            const ehud::data::SettingsStore verified(args.dataRoot);
                            need(verified.value().fields["orbipom.bestScore.v1"].integer()==71&&gameSession->bestScore()==71,"Settings reset preserves saved game progress and the live session");
                        }else need(gameSession->manuallyPaused()&&!game->hasFrameTimer(),"Returning to Minigame preserves manual pause");
                    }
                    if(module==core::Module::map&&map){
                        const auto drainMap=[&]{for(unsigned n=0;n<24;++n){utility->waitIdle();utility->drain();map->utilityCompleted(mapTime);if(const auto due=map->nextWakeTime(mapTime)){mapTime=std::max(mapTime,*due)+.000001;map->deadline(mapTime);}present(mapTime,true);if(!utility->stats().running&&!utility->stats().pending&&!utility->stats().completed&&!map->nextWakeTime(mapTime))break;}};
                        mapTime=time+.7;drainMap();need(map->state().active()&&map->rasterStats().published>0,"Original bundled Map geography renders only after isolated selection");
                        // The disc centre (box px) through the Map canvas's design plane.
                        const auto point=[&](core::Point p){const auto r=map->core().clientRect({p.x,p.y,0,0});need(bool(r),"Map input projects through the design box plane");return core::Point{r->x,r->y};};
                        const auto center=point(endfield::modules::MapCanvas::mapCenter);const auto initialCount=map->state().pins().size();
                        need(map->pointer({app::PointerKind::down,app::PointerButton::right,center.x,center.y},mapTime),"Original right-click adds a projected Map marker");map->pointer({app::PointerKind::up,app::PointerButton::right,center.x,center.y},mapTime);present(mapTime+.17,true);
                        need(map->state().pins().size()==initialCount+1&&mapStore->value().pins.size()==initialCount+1,"Marker writes only the explicitly isolated Map store");
                        need(map->pointer({app::PointerKind::down,app::PointerButton::right,center.x,center.y},mapTime),"Original marker right-click is consumed");present(mapTime+.17,true);need(map->state().pins().size()==initialCount,"Second right-click removes that marker");
                        need(map->wheel({center.x,center.y,1,false,0,3},mapTime),"Map wheel reaches the native continuous camera owner");mapTime+=.181;map->deadline(mapTime);drainMap();need(map->state().camera().zoom>3,"Wheel zoom preserves source sensitivity");
                        need(map->perform("map.reset",mapTime),"The reset view action is available");drainMap();need(map->state().camera()==endfield::modules::MapSurfaceGeometry::constrained({}),"Map reset restores the default camera");
                        const auto before=rasterizer.stats();const auto accepted=utility->stats().accepted;const auto uploads=renderer.stats().textureUploads;const auto poseStart=mapTime;
                        for(unsigned n=0;n<12;++n)present(poseStart+.3+double(n)/60,true);
                        need(before.rasterizations==rasterizer.stats().rasterizations&&accepted==utility->stats().accepted&&uploads==renderer.stats().textureUploads,"Settled Map poses reuse geography, text and GPU textures");
                    }
                    if(module==core::Module::reader&&reader){
                        const auto drainReader=[&]{for(unsigned n=0;n<8;++n){utility->waitIdle();utility->drain();readerOwner->queueCapacityAvailable();}present(readerTime,true);};
                        readerTime=time+.7;drainReader();need(readerOwner->state().active()&&readerOwner->state().loaded(),"Reader loads only its isolated library on selection");
                        // Mac 1.3.0 ReaderCanvas controls through the design canvas's own actions (BatchSixHUDVerification).
                        auto&canvas=reader->canvas();
                        // Tab and page swaps land on the Context's shared deadlines.
                        const auto frames=[&](double t0,unsigned count){for(unsigned frame=0;frame<count;++frame){const double t=t0+double(frame)/60;(void)designOverlay->deadline(t);present(t,true);}};
                        const auto press=[&](std::string_view id,double t){readerTime=t;if(!canvas.performAccessibilityAction(id,t)){std::string shown;for(const auto*c:static_cast<endfield::modules::design::Canvas&>(canvas).page().controls())shown+=" "+c->id();throw std::runtime_error("Reader control "+std::string(id)+" presses like its click (tab "+std::to_string(int(canvas.tab()))+(canvas.transitioning()?", swapping":"")+";"+shown+")");}present(t+.001,true);};
                        if(cycle<2){
                            // The only file opened in this hidden path is generated here.
                            const auto fixture=args.dataRoot/(cycle?"Reader fixture.pdf":"Reader fixture.txt");std::string text;
                            if(cycle)text=ownedReaderPDF();else for(unsigned n=0;n<500;++n)text+="EndfieldHUD 阅读器 · Temporary isolated sample " +std::to_string(n)+".\n";
                            ehud::data::detail::replaceFile(fixture,std::nullopt,text,128*1024);
                            press("readerTabs:0",time+.72);frames(time+.73,12);
                            press("chooseFile",time+.94);auto action=reader->takeImportAction();
                            need(action&&action->kind==endfield::tools::ReaderImportAction::Kind::chooseLocal,"Reader Choose file queues the existing native-picker route without opening a dialog in hidden coverage");
                            importReaderReference(action->generation,utf8(fixture),{},time+.95);readerTime=time+.96;drainReader();
                            // The import switches to the Reading tab (handle(.imported)).
                            frames(time+.97,20);
                            need(canvas.tab()==modules::ReaderCanvas::Tab::reading,"An import lands on the Reading tab");
                        }
                        need(readerOwner->state().book()&&readerOwner->state().current()&&!readerOwner->state().error(),"Real TXT/PDF rendering publishes only the owned generated document");
                        need(readerOwner->state().current()->illustration==(cycle!=0),"Shared Reader provider replaces TXT with the generated PDF and retains it across module switches");
                        need(readerOwner->state().library().preferences.fontName=="Georgia","A fresh Reader library keeps the Mac default face (ReaderPreferences)");
                        if(canvas.tab()!=modules::ReaderCanvas::Tab::reading){press("readerTabs:2",time+1.);frames(time+1.001,20);}
                        press("bookmark",time+1.02);readerTime=time+1.03;drainReader();
                        const auto marks=readerOwner->state().book()->bookmarks.size();need(marks==(cycle==2?0u:1u),"The bookmark control persists through the shared Reader owner");
                        present(time+1.5,true);const auto before=rasterizer.stats();const auto accepted=utility->stats().accepted;
                        frames(time+1.6,12);
                        const auto after=rasterizer.stats();need(before.rasterizations==after.rasterizations&&before.textLayoutsCreated==after.textLayoutsCreated&&utility->stats().accepted==accepted,"Settled Reader frames reuse page pixels and never enqueue provider work");
                    }
                    if(module==core::Module::activityMonitor&&activity&&activity->canvas()){auto&canvas=*activity->canvas();
                        need(activity->state().appSnapshot().items.size()==18,"Activity coverage retains only its synthetic app catalog");
                        need(canvas.perform("activity:apps",time+.7),"The Activity Apps tab is routed");present(time+1,true);
                        need(canvas.isShowingApps(),"The Apps page follows the tab");
                        need(canvas.perform("activity:sort:memory",time+1.01),"The Activity RAM sort is routed");present(time+1.3,true);
                        need(activity->state().sortKey()==endfield::modules::ActivitySort::memory&&canvas.sortKey()==endfield::modules::ActivitySort::memory,"The RAM sort reaches the shared readings");
                        const auto retained=rasterizer.stats();for(unsigned frame=0;frame<12;++frame)present(time+1.35+double(frame)/60,true);
                        need(rasterizer.stats().rasterizations==retained.rasterizations,"Settled Activity frames (the rows' re-stagger included) rasterize nothing");
                        need(canvas.perform("activity:overview",time+1.55),"The Overview tab is routed");present(time+1.9,true);
                    }
                    if(module==core::Module::storage&&storage&&storage->canvas()){auto&canvas=*storage->canvas();
                        need(storageFixture!=nullptr,"Hidden Storage must never retain OS capacity/scanner callbacks");
                        const auto drain=[&]{utility->waitIdle();utilityCompleted();};
                        drain();present(time+.7,true);
                        need(storage->controller().active()&&storage->controller().snapshot().capacity&&storage->controller().snapshot().capacity->volumeName=="Synthetic startup volume"&&!storage->controller().snapshot().isLoading,"Storage activation publishes only the injected capacity snapshot");
                        need(storageFixture->detailScans.load()==(cycle?1u:0u),"Storage activation and frames never launch a folder scan");
                        const auto reads=storageFixture->capacityReads.load();need(canvas.perform("storage:refresh",time+.74),"Refresh is routed");drain();present(time+.8,true);need(storageFixture->capacityReads.load()==reads+1,"Manual refresh uses the shared executor once");
                        if(!cycle){need(storage->requestDetails(false,time+.82),"Explicit isolated model probe can request details without inventing a UI button");drain();need(storageFixture->detailScans.load()==1&&!storage->controller().details().isLoading&&storage->controller().details().categories.size()==1,"Explicit details request uses only its synthetic bounded callback");}
                        need(canvas.nextWakeTime(time+.85).has_value(),"Visible Storage shares the original 60-second deadline");storageModuleOwner->setOverlayVisible(false,time+.9);need(!canvas.nextWakeTime(time+.9)&&!storage->controller().active(),"Hidden Storage removes its capacity deadline");
                        const auto hiddenReads=storageFixture->capacityReads.load();(void)storageModuleOwner->deadline(time+.92);need(storageFixture->capacityReads.load()==hiddenReads,"Hidden deadline dispatch never queries a provider");storageModuleOwner->setOverlayVisible(true,time+.94);present(time+.94,true);
                        const auto cached=storageFixture->capacityReads.load();for(unsigned frame=0;frame<20;++frame)present(time+1+double(frame)/60,true);need(storageFixture->capacityReads.load()==cached,"Warm Storage frames do not poll capacity");
                    }
                    if(module==core::Module::calendar&&calendar){
                        // The hidden path owns a NEW parser-validated root and
                        // synthetic notifications. All repository work and
                        // scheduling run through the existing app FIFO.
                        need(flushCalendar(),"Calendar initial load settles through the shared executor");present(time+.7,true);
                        need(calendarState->loaded()&&calendarState->active()&&!calendarState->error(),"Calendar activates its isolated lazy repository");
                        if(!cycle){
                            // HUDCalendarCanvas.perform("add") with a generated title in its
                            // shared design field: the draft saves through the app FIFO.
                            calendar->titleField().setText("C");calendar->perform("add");
                            need(flushCalendar(),"Calendar durable event and reminder receipts settle on the shared FIFO");present(time+1.22,true);
                            need(calendarState->events().size()==1&&calendarState->events()[0].title=="C"&&calendar->titleField().text().empty(),"Calendar saves its generated draft and clears the fields only after persistence success");
                            need(fs::is_regular_file(args.dataRoot/"Calendar"/"calendar.json")&&!calendarFixture->pending.empty(),"Calendar writes only the explicit temporary store and synthetic reminder plan");
                        }
                        present(time+1.4,true);const auto beforeRaster=rasterizer.stats();const auto beforeGPU=renderer.stats();const auto accepted=utility->stats().accepted;
                        for(unsigned n=0;n<12;++n)present(time+1.41+double(n)/60,true);
                        need(rasterizer.stats().rasterizations==beforeRaster.rasterizations&&rasterizer.stats().textLayoutsCreated==beforeRaster.textLayoutsCreated&&renderer.stats().textureUploads==beforeGPU.textureUploads&&utility->stats().accepted==accepted,"Settled Calendar frames reuse resources and schedule no reminder or file work");
                        need(!calendarModuleOwner->requiresFrames(time+1.7),"Calendar's transitions become idle on the shared frame clock");
                    }
                    if(module==core::Module::archive&&archive){
                        // Hidden-only barrier for the real shared async database.
                        // No native service/IME/file picker is activated by it.
                        utility->waitIdle();utility->drain();archiveService->queueCapacityAvailable();present(time+.7,true);
                        need(archiveService->state().active()&&!archiveService->state().error(),"Archive source activation loads its isolated database");
                        if(!cycle){
                            // The redesigned canvas (BatchSix testEditor): New files a
                            // document at once and opens the editor; the title and the
                            // body take generated text (UIA SetValue and typed
                            // characters: no text service in hidden runs); the writes
                            // drain through the shared utility executor.
                            auto& canvas=archive->canvas();
                            need(canvas.displayedPage().kind==modules::ArchiveCanvas::PageKind::list,"Archive opens on its list page");
                            need(canvas.performAccessibilityAction("new",time+.72),"Archive New is an accessible control");present(time+.73,true);
                            utility->waitIdle();utility->drain();archiveService->queueCapacityAvailable();present(time+1.1,true);
                            need(archiveService->state().selected().has_value()&&canvas.displayedPage().kind==modules::ArchiveCanvas::PageKind::editor,"New creates an Uncategorized document and opens it without a category menu");
                            // The editor page lands on the Context's shared deadlines (the sub-page swap).
                            double editorAt=time+1.1;for(unsigned n=0;n<40&&canvas.transitioning();++n){editorAt+=1./60;(void)designOverlay->deadline(editorAt);present(editorAt,true);}
                            need(!canvas.transitioning(),"The Archive editor page lands");
                            need(canvas.setAccessibilityText("title","A",editorAt+.02),"The Archive title takes generated text");
                            need(canvas.performAccessibilityAction("body",editorAt+.03)&&canvas.bodyArea()&&canvas.bodyArea()->isFocused(),"The Archive body takes the focus");
                            need(archive->key({app::KeyKind::character,'B'},editorAt+.04),"The Archive body accepts generated plain text");
                            need(archive->finishEditing(editorAt+.06),"Hidden Archive editors commit without a native text manager");need(archiveService->flush(),"Archive fixture writes finish through the shared utility executor");
                            present(editorAt+.2,true);need(archiveService->state().selected()->title=="A"&&archiveService->state().selected()->body=="B","Archive persisted state retains the generated title and body");
                        }
                    }
                    if(module==core::Module::clipboard&&clipboardCanvas){
                        // The design owner projects the box; the wheel scrolls the cards (12 px a line).
                        const auto target=static_cast<tools::DesignModuleOwner&>(*clipboardModuleOwner).core().clientRect({295,300,0,0});need(target.has_value(),"Clipboard scroll target projects");
                        const auto offset=[&]{const auto areas=clipboardCanvas->page().scrollAreas();return areas.empty()?0.:areas.front()->offset();};
                        for(unsigned step=0;step<24;++step){const double atTime=time+.7+double(step)*.04;const auto previous=offset();need(clipboardModuleOwner->wheel({target->x,target->y,step<12?-1.:1.,false,0,3},atTime),"Clipboard consumes the projected wheel input");present(atTime,true);scrollChanges+=offset()!=previous;const auto usage=rasterizer.stats();peakEntries=std::max(peakEntries,usage.entries);peakBytes=std::max(peakBytes,usage.resourceBytes);}
                        need(scrollChanges>=12&&offset()==0,"Clipboard moves through cards and returns to the top");
                    }
                    // Reader peers in the first cycle, with the plain-text book: a
                    // PDF page rendered late in the run leaves Windows.Data.Pdf's
                    // own D3D device to an idle timer, which still runs at process
                    // exit (README checkpoint 124, "Found while integrating").
                    if(module==core::Module::reader&&reader&&cycle==0)
                        peerProbe(core::Module::reader,readerModuleOwner.get(),{{"settings",[&]{return reader->capturesPointer();}},{"settings",[&]{return !reader->capturesPointer();}}});
                    if(module==core::Module::workMode&&workMode&&workModeCanvas&&cycle==0){
                        // The custom duration field (a design text field): the I-beam over
                        // it while it edits, through the owner and the host's WM_SETCURSOR
                        // route; the source cursor beside it and once it closes.
                        double workStart=time+.66;present(workStart,true);
                        auto&owner=static_cast<tools::DesignModuleOwner&>(*workModeModuleOwner);
                        // A click on the Custom preset segment through the owner's own pointer route.
                        const endfield::modules::design::Control*customPreset{};
                        for(const auto*c:workModeCanvas->page().controls())if(c->id()=="workMode.preset:3")customPreset=c;
                        const auto preset=customPreset?owner.core().clientRect(customPreset->rect(workModeCanvas->root())):std::nullopt;
                        need(bool(preset),"Work Mode shows its Custom preset");
                        const double px=preset->x+preset->width/2,py=preset->y+preset->height/2;
                        need(workModeModuleOwner->pointer({PointerKind::down,PointerButton::left,px,py},workStart+.002),"The Custom preset takes the press");
                        (void)workModeModuleOwner->pointer({PointerKind::up,PointerButton::left,px,py},workStart+.004);
                        for(unsigned n=1;n<=6;++n)present(workStart+.005+double(n)/60,true);
                        auto&custom=workModeCanvas->customField();
                        need(workModeCanvas->editingCustom()&&custom.editing(),"Work Mode opens its custom duration field");
                        const auto field=owner.core().clientRect(custom.rect(workModeCanvas->root()));
                        const auto beside=owner.core().clientRect({20,240,0,0});
                        const core::Point center=field?core::Point{field->x+field->width/2,field->y+field->height/2}:core::Point{};
                        hostCheck(field&&workModeModuleOwner->cursorAt(center)==std::optional(ModuleCursor::text),"Work Mode answers the I-beam over its open duration field");
                        // A pinned Notes card above the field would rightly keep the source cursor.
                        if(field&&!notes->covers(center))hostCheck(cursorAt(center)==ModuleCursor::text,"The host's cursor route shows the I-beam over the open duration field");
                        hostCheck(beside&&!workModeModuleOwner->cursorAt({beside->x,beside->y})&&cursorAt({beside->x,beside->y})==ModuleCursor::source,"The source cursor stays beside the open field");
                        custom.endEditing(false);present(workStart+.13,true);
                        hostCheck(!custom.editing()&&!workModeModuleOwner->cursorAt(center)&&cursorAt(center)==ModuleCursor::source,"Closing the field restores the source cursor");
                        // Reopened through the Custom segment, the field is an Edit peer under
                        // the HUD root; SetValue commits it (MODS-734).
                        // A direct canvas call is no owner entry point: the context's editor host
                        // follows this owner first (as DesignOwnerCore's UIA invoke does), so the
                        // rebuilt field registers with Work Mode's editors, not the last owner's.
                        owner.core().bindEditorHost();
                        need(workModeCanvas->performAccessibilityAction("workMode.preset:3",workStart+.131)&&workModeCanvas->editingCustom(),"Work Mode reopens its custom duration field through the Custom segment");
                        present(workStart+.135,true);
                        {HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                         ComPtr<IRawElementProviderFragment>editor;
                         for(std::size_t n=0;n<workModeModuleOwner->automationCount()&&!editor;++n){ComPtr<IRawElementProviderFragment>f;
                             if(SUCCEEDED(workModeModuleOwner->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)=="workMode.custom")editor=f;}
                         ComPtr<IRawElementProviderSimple>simple;ComPtr<IUnknown>pattern;ComPtr<IValueProvider>value;VARIANT type;VariantInit(&type);
                         const bool edit=editor&&SUCCEEDED(editor.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId,&type))&&type.vt==VT_I4&&type.lVal==UIA_EditControlTypeId;
                         VariantClear(&type);
                         hostCheck(edit&&SUCCEEDED(simple->GetPatternProvider(UIA_ValuePatternId,&pattern))&&pattern&&SUCCEEDED(pattern.As(&value))&&value->SetValue(L"0:07")==S_OK,
                             "Work Mode's custom duration field is an Edit peer whose value commits");
                         uia.disconnectOwners();uia.shutdown();}
                        present(workStart+.14,true);
                        need(!workModeCanvas->editingCustom()&&workMode->snapshot(workStart+.14).duration==7,"A committed custom duration sets the countdown (Custom 0:07)");
                        workStart+=.2;
                        // Drive the host's one Work Mode controller through its accessible
                        // controls so areas observing it (Personal Profile lifetime hours)
                        // receive real checkpoints in hidden coverage.
                        // Frames run the shared design deadlines first (the page swap lands on one).
                        const auto frame=[&](double t){if(const auto wake=designOverlay->nextWakeTime(t);wake&&*wake<=t)onDeadline(t);present(t,true);};
                        frame(workStart);
                        need(workModeCanvas->performAccessibilityAction("workMode.kind:1",workStart),"Work Mode switches to the stopwatch through its mode tab");
                        for(unsigned n=1;n<=24;++n)frame(workStart+double(n)/60);
                        need(workModeCanvas->performAccessibilityAction("workMode.primary",workStart+.4),"Work Mode starts the stopwatch through its accessible primary button");
                        for(unsigned n=1;n<=30;++n)frame(workStart+.4+double(n)/60);
                        const double stop=workStart+1;
                        need(workModeCanvas->performAccessibilityAction("workMode.primary",stop)&&workMode->snapshot(stop).phase==endfield::modules::WorkModePhase::paused&&workMode->trackedWorkSeconds(stop)>0,"The stopwatch pauses with tracked time");
                        present(stop+.01,true);
                    }
                    if(auto*area=areaFor(registry.presenter(module)))runArea(*area,module,cycle,time+.7);
                }
                catch(const std::exception&e){const auto usage=rasterizer.stats();const auto&shown=notes->modulePresentation();
                    throw std::runtime_error("Combined isolated module "+std::string(core::moduleIdentifier(module))+" cycle="+std::to_string(cycle)+" selected="+std::string(core::moduleIdentifier(notes->selected()))+
                        " current="+std::string(core::moduleIdentifier(shown.current.module))+" transitioning="+std::to_string(shown.transitioning)+" pending="+(pendingModule?std::string(core::moduleIdentifier(*pendingModule)):std::string("none"))+
                        " visible="+std::to_string(session->phase()!=core::VisibilityPhase::concealed)+" entries="+std::to_string(usage.entries)+" bytes="+std::to_string(usage.resourceBytes)+": "+e.what());}
                const bool areaModule=std::find(areaModules.begin(),areaModules.end(),module)!=areaModules.end();
                const auto usage=rasterizer.stats();(areaModule?areaModuleResults:moduleResults).push_back(Json::Object{{"module",std::string(core::moduleIdentifier(module))},{"cycle",int(cycle)},{"rasterEntries",std::int64_t(usage.entries)},{"rasterBytes",std::int64_t(usage.resourceBytes)},{"scrollChanges",std::int64_t(scrollChanges)},{"scrollPeakEntries",std::int64_t(peakEntries)},{"scrollPeakBytes",std::int64_t(peakBytes)}});
                if(module==modules.back()){
                    if(retainedCycle&&!(usage.entries==retainedCycle->entries&&usage.resourceBytes==retainedCycle->resourceBytes)){
                        // Name the growth: every module step's usage of the last two cycles.
                        std::string steps;for(const auto&row:moduleResults)if(row["cycle"].integer()+1>=int(cycle))steps+=" "+row["module"].string()+"@"+std::to_string(row["cycle"].integer())+"="+std::to_string(row["rasterEntries"].integer())+"/"+std::to_string(row["rasterBytes"].integer());
                        need(false,("Repeated combined module cycles retain no extra raster resources (entries "+std::to_string(retainedCycle->entries)+"->"+std::to_string(usage.entries)+", bytes "+
                            std::to_string(retainedCycle->resourceBytes)+"->"+std::to_string(usage.resourceBytes)+";"+steps+")").c_str());}
                    // Reader intentionally adds a second (PDF) book on cycle1.
                    // Compare after that fixed input set has been exercised;
                    // the extra book changes library/control text legitimately.
                    if(!reader||cycle>0)retainedCycle=usage;
                    // Once per cycle every area also receives a module-less step.
                    for(auto&area:externalModules)runArea(area,std::nullopt,cycle,time+1.9);
                }time=std::max(time,areaLatest);time+=2;
            }
            // Host integration over the same owner: the restart service is
            // production-only, the System row of the macImport area, the UI
            // Automation root over the Volume fragments and the OLE drop
            // route's Shelf navigation hit test.
            {
                hostCheck(!restart.restartWith([]{},[]{})&&!restartPending,"A data-root restart cannot be requested outside production");
                if(settingsUI&&areaHost&&areaHost->find<MacImportEntry>(macImportEntryService)){
                    settingsUI->refreshViews(time);
                    const auto&system=settingsUI->canvas(core::Module::system);
                    if(system.renderedLanguage()&&system.section()=="main")
                        hostCheck(std::any_of(system.page().controls().begin(),system.page().controls().end(),[](const auto*c){return c->id()=="macImport";}),"System lists Import macOS data from the macImport area");
                }
                if(volumeModuleOwner&&registry.enabled(*volumeModuleOwner)){
                    selectModule(core::Module::volume,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                    hostCheck(uia.groups()>=1&&volumeModuleOwner->automationCount()>0,"The HUD's UI Automation root lists the projected Volume controls");
                    Microsoft::WRL::ComPtr<IRawElementProviderFragment> rootFragment,group,child,parent,hit;
                    hostCheck(SUCCEEDED(uia.root()->QueryInterface(IID_PPV_ARGS(&rootFragment)))&&rootFragment,"The UIA root is a fragment");
                    const auto caption=moduleCaption("volume",watchAppearance.language,"volume");
                    for(HRESULT hr=rootFragment->Navigate(NavigateDirection_FirstChild,&group);SUCCEEDED(hr)&&group;){
                        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;VARIANT name;VariantInit(&name);
                        if(SUCCEEDED(group.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId,&name))&&name.vt==VT_BSTR&&name.bstrVal&&narrow(name.bstrVal)==caption){VariantClear(&name);break;}
                        VariantClear(&name);Microsoft::WRL::ComPtr<IRawElementProviderFragment> next;hr=group->Navigate(NavigateDirection_NextSibling,&next);group=next;
                    }
                    hostCheck(group!=nullptr,"The Volume group is named after its module");
                    hostCheck(group&&SUCCEEDED(group->Navigate(NavigateDirection_FirstChild,&child))&&child&&SUCCEEDED(child->Navigate(NavigateDirection_Parent,&parent))&&parent,
                        "Volume fragments are children of their group and navigate back to it");
                    if(child&&parent){Microsoft::WRL::ComPtr<IUnknown> a,b;(void)parent.As(&a);(void)group.As(&b);hostCheck(a.Get()==b.Get(),"A fragment's parent is its owner's group");}
                    UiaRect rect{};if(child)(void)child->get_BoundingRectangle(&rect);
                    hostCheck(rect.width>0&&rect.height>0&&SUCCEEDED(uia.root()->ElementProviderFromPoint(rect.left+rect.width/2,rect.top+rect.height/2,&hit))&&hit,"The root's hit test reaches a projected Volume control");
                    // App volume (five synthetic apps): the tab switch is the shared
                    // sub-page motion (started at this frame time and its page swap dispatched
                    // as the host would);
                    // three 54 px rows (174 px) and a wheel detent
                    // glides the retained rows without laying the module out again.
                    {auto&tabs=*volumeCanvas->page().tabs;volumeCanvas->env().now=time;tabs.select(1,true);unsigned frames{};
                     while(frames<60&&(volumeCanvas->transitioning()||volumeModuleOwner->requiresFrames(time))){time+=1./60;(void)designOverlay->deadline(time);present(time,true);++frames;}
                     hostCheck(volumeCanvas->tab()==1&&frames>=8&&frames<60,"App volume arrives through the shared sub-page motion (tab "+std::to_string(volumeCanvas->tab())+", "+std::to_string(frames)+" frames)");
                     const auto areas=volumeCanvas->page().scrollAreas();hostCheck(!areas.empty()&&areas.front()->layer().bounds().height==174,"The Volume app list shows three rows ("+std::to_string(areas.size())+" areas, "+(areas.empty()?std::string("-"):std::to_string(areas.front()->layer().bounds().height))+" px, "+std::to_string(volumeCanvas->visibleApplicationRows())+" rows)");
                     const auto r=static_cast<tools::DesignModuleOwner&>(*volumeModuleOwner).core().clientRect({295,125+87,0,0});const auto before=rasterizer.stats();
                     hostCheck(r&&volumeModuleOwner->wheel({r->x,r->y,-1,false,0,3},time)&&volumeCanvas->applicationScrollOffset()==36,"A wheel detent glides the app list 36 px");
                     for(unsigned frame=0;frame<20;++frame)present(time+double(frame)/60,true);time+=1;
                     hostCheck(rasterizer.stats().textLayoutsCreated-before.textLayoutsCreated<=3,"Scrolling the Volume app list lays out at most the row entering its window");}
                    // App icons (Mac VolumeCanvas.renderAppIcon) through the shared
                    // utility queue: the rows in or next to the view get their
                    // executables' icons with its completion, an executable without an
                    // icon resource keeps the source nil icon (the grey square), Volume
                    // starts no worker of its own and the Shelf's Shell icon worker is
                    // not used.
                    if(volumeIcons&&utility){
                        const auto shelfIcons=shelf?shelf->resources().iconWorkerStarts:0;const auto workers=utility->stats().workersStarted;
                        // One job at a time: a request that changed meanwhile goes out when the previous one completes.
                        for(unsigned round=0;round<8;++round){utility->waitIdle();utilityCompleted();if(!volumeIcons->stats().inFlight)break;}
                        for(unsigned frame=0;frame<4;++frame)present(time+double(frame)/60,true);time+=1;
                        const auto&plan=volumeIcons->plan();std::size_t withIcon{},without{};bool exact=!plan.applications().empty();
                        for(const auto&row:plan.applications()){if(!row.executable){exact=false;continue;}const auto*binding=plan.binding(row.applicationID);
                            if(row.executable->identity.objectID[0]!=3){++withIcon;exact=exact&&binding&&binding->image&&volumeIcons->images().acquire(binding->imageKey,binding->revision)!=nullptr;}
                            else{++without;exact=exact&&(!binding||!binding->image||binding->typeFallback);}}
                        {const bool late=volumeCanvas->iconsChanged();hostCheck(exact&&withIcon>=2&&without==1&&!late,"Volume's app rows show their executables' icons from the shared queue; one without an icon resource keeps the nil icon ("+std::to_string(plan.applications().size())+" rows, "+std::to_string(withIcon)+" with, "+std::to_string(without)+" without, exact "+std::to_string(exact)+", late "+std::to_string(late)+")");}
                        const auto loaded=volumeIcons->stats();
                        hostCheck(loaded.jobs>=1&&loaded.published>=withIcon&&!loaded.inFlight&&utility->stats().workersStarted==workers&&(!shelf||shelf->resources().iconWorkerStarts==shelfIcons),
                            "Volume icons load on the shared utility worker: no thread of their own, and the Shelf's Shell icon worker is untouched");
                        volumeCanvas->env().now=time;volumeCanvas->page().tabs->select(0,true);for(unsigned frame=0;frame<40;++frame){(void)designOverlay->deadline(time+double(frame)/60);present(time+double(frame)/60,true);}time+=1;
                        hostCheck(volumeCanvas->tab()==0&&!volumeIcons->plan().active(),"Leaving App volume withdraws its icon requests");
                        volumeCanvas->env().now=time;volumeCanvas->page().tabs->select(1,true);for(unsigned frame=0;frame<40;++frame){(void)designOverlay->deadline(time+double(frame)/60);present(time+double(frame)/60,true);}time+=1;
                        hostCheck(volumeIcons->plan().active()&&volumeIcons->stats().jobs==loaded.jobs&&!volumeCanvas->iconsChanged(),"Returning to App volume binds the cached icons at once, without loading them again");
                        for(unsigned frame=0;frame<20;++frame)present(time+double(frame)/60,true);time+=1;
                    }
                    uia.disconnectOwners();hostCheck(volumeModuleOwner->automationCount()==0&&uia.groups()==0,"Concealing disconnects every fragment");
                    uia.shutdown();
                }
                // Every module's UI Automation group under the real HUD root:
                // named after its module, children navigate back to it, and the
                // root's hit test finds a projected control (built-in owners and
                // the module areas through the ModuleOwner hooks).
                const auto same=[](IUnknown*a,IUnknown*b){ComPtr<IUnknown>x,y;if(!a||!b||FAILED(a->QueryInterface(IID_PPV_ARGS(&x)))||FAILED(b->QueryInterface(IID_PPV_ARGS(&y))))return false;return x.Get()==y.Get();};
                const auto findGroup=[&](HudAutomation&probe,std::string_view caption)->ComPtr<IRawElementProviderFragment>{
                    ComPtr<IRawElementProviderFragment>root,group;if(FAILED(probe.root()->QueryInterface(IID_PPV_ARGS(&root))))return nullptr;
                    for(HRESULT hr=root->Navigate(NavigateDirection_FirstChild,&group);SUCCEEDED(hr)&&group;){
                        if(textProperty(group.Get(),UIA_NamePropertyId)==caption)return group;
                        ComPtr<IRawElementProviderFragment>next;hr=group->Navigate(NavigateDirection_NextSibling,&next);group=next;}
                    return nullptr;};
                const auto groupProbe=[&](core::Module module,ModuleOwner*owner,std::optional<std::size_t>children){
                    if(!owner||!registry.enabled(*owner))return;
                    const std::string id(core::moduleIdentifier(module));
                    selectModule(module,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                    const auto group=findGroup(probe,moduleCaption(id,watchAppearance.language,id));
                    hostCheck(group&&owner->automationCount()>0,"The "+id+" controls are listed in a UI Automation group named after the module");
                    if(children)hostCheck(owner->automationCount()==*children,"The "+id+" group lists "+std::to_string(*children)+" controls ("+std::to_string(owner->automationCount())+")");
                    ComPtr<IRawElementProviderFragment>first,parent;
                    hostCheck(group&&SUCCEEDED(group->Navigate(NavigateDirection_FirstChild,&first))&&first&&SUCCEEDED(first->Navigate(NavigateDirection_Parent,&parent))&&same(parent.Get(),group.Get()),
                        "The "+id+" controls navigate back to their group");
                    bool hit=false;
                    for(std::size_t n=0;n<owner->automationCount()&&!hit;++n){
                        ComPtr<IRawElementProviderFragment>child,found;UiaRect r{};
                        if(owner->automationChild(n,&child)<0||!child||FAILED(child->get_BoundingRectangle(&r))||!(r.width>0&&r.height>0))continue;
                        hit=SUCCEEDED(probe.root()->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&found))&&found;}
                    hostCheck(hit,"The root's hit test reaches a projected "+id+" control");
                    probe.disconnectOwners();hostCheck(owner->automationCount()==0,"Concealing disconnects the "+id+" controls");
                    probe.shutdown();};
                const auto areaOwner=[&](std::string_view name)->ModuleOwner*{for(auto&a:externalModules)if(a.name==name)return a.owner.get();return nullptr;};
                groupProbe(core::Module::workMode,workModeModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::fileShelf,shelfModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::archive,archiveModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::mediaAssembly,areaOwner("mediaAssembly"),std::nullopt);
                // Now Playing 1.3.0: track, seek, previous, play/pause, next, volume, lyrics.
                groupProbe(core::Module::nowPlaying,areaOwner("nowPlaying"),std::size_t{7});
                groupProbe(core::Module::account,areaOwner("account"),std::nullopt);
                groupProbe(core::Module::profile,areaOwner("profile"),std::nullopt);
                // The provider modules' Mac accessibility buttons (and the telemetry
                // status text) through their built-in adapters.
                groupProbe(core::Module::clipboard,clipboardModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::eventLog,eventLogModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::activityMonitor,activityModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::storage,storageModuleOwner.get(),std::nullopt);
                // Settings (the design library's UIA tree): each page's controls in
                // a group named after the page; a switch toggled through UI
                // Automation changes its preference exactly like its click and
                // reports the new state.
                if(settingsUI&&settingsModuleOwner&&registry.enabled(*settingsModuleOwner)){
                    for(const auto page:{core::Module::system,core::Module::display,core::Module::hotkeys,core::Module::about})groupProbe(page,settingsModuleOwner.get(),std::nullopt);
                    selectModule(core::Module::system,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                    ComPtr<IRawElementProviderFragment>focusLost;
                    for(std::size_t n=0;n<settingsModuleOwner->automationCount()&&!focusLost;++n){ComPtr<IRawElementProviderFragment>f;
                        if(SUCCEEDED(settingsModuleOwner->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)=="focus")focusLost=f;}
                    const bool original=settingsUI->controller().configuration().boolean("closeOnFocusLost");
                    ComPtr<IRawElementProviderSimple>simple;ComPtr<IUnknown>provider;ComPtr<IToggleProvider>toggle;ToggleState state{};
                    hostCheck(focusLost&&SUCCEEDED(focusLost.As(&simple))&&SUCCEEDED(simple->GetPatternProvider(UIA_TogglePatternId,&provider))&&provider&&SUCCEEDED(provider.As(&toggle))&&
                        SUCCEEDED(toggle->get_ToggleState(&state))&&(state==ToggleState_On)==original,"Settings > System lists Close when focus lost as a switch in its saved state");
                    hostCheck(toggle&&toggle->Toggle()==S_OK&&settingsUI->controller().configuration().boolean("closeOnFocusLost")!=original&&SUCCEEDED(toggle->get_ToggleState(&state))&&(state==ToggleState_On)!=original,
                        "Toggling the Settings switch through UI Automation changes the preference like its click and reports the new state");
                    present(time,true);time+=.5;
                    hostCheck(toggle&&toggle->Toggle()==S_OK&&settingsUI->controller().configuration().boolean("closeOnFocusLost")==original,"Toggling the Settings switch again restores the preference");
                    present(time,true);time+=.5;
                    uia.disconnectOwners();hostCheck(settingsModuleOwner->automationCount()==0,"Concealing disconnects the Settings controls");uia.shutdown();
                }
                // The host re-lists the peers when a module switch settles, and when
                // the active module's accept-input changes, once a client asked for
                // the root (Application::present): the module a click selected is
                // listed with no further input, though at the click it accepted none.
                if(settingsUI&&settingsModuleOwner&&registry.enabled(*settingsModuleOwner)&&volumeModuleOwner&&registry.enabled(*volumeModuleOwner)){
                    const auto settle=[&](unsigned frames){for(unsigned frame=0;frame<frames;++frame)present(time+double(frame)/60,true);time+=double(frames)/60+.1;};
                    const auto child=[&](ModuleOwner&owner,std::string_view id){ComPtr<IRawElementProviderFragment>found;
                        for(std::size_t n=0;n<owner.automationCount()&&!found;++n){ComPtr<IRawElementProviderFragment>f;if(SUCCEEDED(owner.automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)==id)found=f;}
                        return found;};
                    selectModule(core::Module::volume,time);settle(40);
                    // A UIA client asks the HUD window for its root (automationObject).
                    automation=std::make_unique<HudAutomation>(host.hwnd(),automationSource());automationChanged();refreshAutomation();
                    hostCheck(volumeModuleOwner->automationCount()>0&&settingsModuleOwner->automationCount()==0,"With a UIA client connected the shown Volume controls are listed");
                    // The click's content event: the switch has only begun.
                    selectModule(core::Module::system,time);refreshAutomation();
                    hostCheck(settingsModuleOwner->automationCount()==0,"At the click the incoming System page accepts no input yet and lists nothing");
                    settle(40);
                    hostCheck(findGroup(*automation,moduleCaption("system",watchAppearance.language,"system"))&&child(*settingsModuleOwner,"focus")&&volumeModuleOwner->automationCount()==0,
                        "Once the switch settles the host lists the System page's controls with no further input; Volume's are gone");
                    const auto refreshes=automation->stats().refreshes;settle(10);
                    hostCheck(automation->stats().refreshes==refreshes,"Settled frames only compare: nothing is listed again");
                    // The layout prompt: the UI scale slider's new value raises "Keep
                    // this UI scale?" on the shared confirm bar, whose pills the overlay
                    // lists; Revert restores the scale and the page stays listed.
                    selectModule(core::Module::display,time);refreshAutomation();settle(40);
                    const auto scale=child(*settingsModuleOwner,"uiScale");
                    ComPtr<IRawElementProviderSimple>simple;ComPtr<IUnknown>pattern;ComPtr<IRangeValueProvider>range;
                    hostCheck(scale&&SUCCEEDED(scale.As(&simple))&&SUCCEEDED(simple->GetPatternProvider(UIA_RangeValuePatternId,&pattern))&&pattern&&SUCCEEDED(pattern.As(&range))&&
                        range->SetValue(1.5)==S_OK&&settingsUI->controller().scalePending(),"The settled Display page is listed and its UI scale slider previews a new scale");
                    settle(12);
                    auto&overlay=designOverlay->context().overlay();
                    hostCheck(overlay.isConfirming()&&overlay.confirmState().request()->id=="layout"&&overlay.confirmState().request()->countdown.has_value()&&designOverlay->automationCount()>0,
                        "The layout prompt shows on the shared confirm bar with its countdown, and its pills are listed");
                    hostCheck(overlay.decide(modules::design::ConfirmState::Decision::cancel),"Revert on the bar restores the UI scale");
                    settle(40);
                    hostCheck(!overlay.isConfirming()&&!settingsUI->controller().scalePending()&&child(*settingsModuleOwner,"uiScale"),"Once the bar is gone the Display page is listed, with no further input");
                    selectModule(core::Module::volume,time);refreshAutomation();settle(40);
                    hostCheck(volumeModuleOwner->automationCount()>0&&settingsModuleOwner->automationCount()==0,"Switching back lists the Volume controls with no further input");
                    automation->disconnectOwners();automation->shutdown();automation.reset();
                }
                // Keyboard focus under the real root (GetFocus): the open Notes
                // editor is the focused element of the root and of its group;
                // finishing it leaves no Notes element focused.
                if(notesModuleOwner&&registry.enabled(*notesModuleOwner)){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    std::vector<gpu::NotesAutomationElement>elements;notes->automationElements(elements);std::string edit;
                    for(const auto&e:elements)if(e.enabled&&e.id.starts_with("note:")&&e.id.ends_with(":edit")){edit=e.id;break;}
                    hostCheck(!edit.empty()&&notes->perform(edit,time),"A Notes text card opens its editor through its accessible Edit action");
                    for(unsigned frame=1;frame<=20;++frame)present(time+double(frame)/60,true);time+=.5;
                    HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                    ComPtr<IRawElementProviderFragment>focusedRoot,focusedGroup;ComPtr<IRawElementProviderFragmentRoot>groupRoot;
                    const auto group=findGroup(uia,moduleCaption("notes",watchAppearance.language,"notes"));
                    hostCheck(SUCCEEDED(uia.root()->GetFocus(&focusedRoot))&&focusedRoot&&textProperty(focusedRoot.Get(),UIA_AutomationIdPropertyId).starts_with("notes:editor:"),
                        "The HUD root's GetFocus returns the open Notes editor");
                    hostCheck(group&&SUCCEEDED(group.As(&groupRoot))&&SUCCEEDED(groupRoot->GetFocus(&focusedGroup))&&same(focusedGroup.Get(),focusedRoot.Get()),
                        "The Notes group's GetFocus returns the same editor");
                    hostCheck(notes->finish(),"The Notes editor finishes");present(time,true);time+=.5;(void)uia.refresh();
                    ComPtr<IRawElementProviderFragment>afterwards;
                    hostCheck(SUCCEEDED(uia.root()->GetFocus(&afterwards))&&(!afterwards||!textProperty(afterwards.Get(),UIA_AutomationIdPropertyId).starts_with("notes:editor:")),
                        "No Notes editor holds the focus once it finished");
                    uia.disconnectOwners();uia.shutdown();
                }
                // The source navigation's accessibility buttons (HUDSourceWatchView):
                // bound entries carry their titles and the selected module the
                // value Selected, the red power button is listed, the root's hit
                // test reaches an entry, and Scroll modules down scrolls like the
                // Mac scrollDesktopNavigation (with source input enabled, restored afterwards).
                if(navigationAutomation&&registry.enabled(*navigationAutomation)){
                    selectModule(core::Module::notes,time);session->setScrollPosition(1,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    const bool inputBefore=session->inputEnabled();session->setInputEnabled(true,time);present(time,true);
                    HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                    const auto notesTitle=moduleCaption("notes",watchAppearance.language,"notes");
                    const auto selectedText=core::localized("Selected","已选择",watchAppearance.language==core::Language::system?core::Language::english:watchAppearance.language);
                    ComPtr<IRawElementProviderFragment>notesEntry,quitButton,scrollUp,scrollDown;std::size_t selectedEntries{};
                    for(std::size_t n=0;n<navigationAutomation->automationCount();++n){ComPtr<IRawElementProviderFragment>f;
                        if(FAILED(navigationAutomation->automationChild(n,&f))||!f)continue;
                        const auto id=textProperty(f.Get(),UIA_AutomationIdPropertyId);const auto value=textProperty(f.Get(),UIA_ValueValuePropertyId);
                        if(value==selectedText)++selectedEntries;
                        if(textProperty(f.Get(),UIA_NamePropertyId)==notesTitle&&id.starts_with("navigation:")&&!notesEntry)notesEntry=f;
                        if(id.starts_with("navigation:quit:"))quitButton=f;if(id=="navigation:scrollUp")scrollUp=f;if(id=="navigation:scrollDown")scrollDown=f;}
                    hostCheck(notesEntry&&selectedEntries==1&&textProperty(notesEntry.Get(),UIA_ValueValuePropertyId)==selectedText,
                        "The source navigation lists the selected Notes entry, the only one valued Selected");
                    hostCheck(quitButton!=nullptr,"The source navigation lists the red power button");
                    UiaRect r{};ComPtr<IRawElementProviderFragment>found;if(notesEntry)(void)notesEntry->get_BoundingRectangle(&r);
                    hostCheck(r.width>0&&r.height>0&&SUCCEEDED(uia.root()->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&found))&&same(found.Get(),notesEntry.Get()),
                        "The root's hit test finds the Notes navigation entry");
                    const auto enabled=[&](const ComPtr<IRawElementProviderFragment>&f){ComPtr<IRawElementProviderSimple>simple;VARIANT v;VariantInit(&v);
                        const bool on=f&&SUCCEEDED(f.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_IsEnabledPropertyId,&v))&&v.vt==VT_BOOL&&v.boolVal==VARIANT_TRUE;VariantClear(&v);return on;};
                    hostCheck(scrollUp&&scrollDown&&!enabled(scrollUp)&&enabled(scrollDown),"At the top only Scroll modules down is enabled");
                    const double before=session->scrollPosition();ComPtr<IInvokeProvider>invoke;
                    hostCheck(scrollDown&&SUCCEEDED(scrollDown.As(&invoke))&&invoke->Invoke()==S_OK&&session->scrollPosition()<before-1e-9,"Scroll modules down scrolls the navigation like the Mac button");
                    for(unsigned frame=1;frame<=20;++frame)present(time+double(frame)/60,true);time+=.5;(void)uia.refresh();
                    hostCheck(enabled(scrollUp),"After scrolling, Scroll modules up is enabled");
                    session->setScrollPosition(1,time);session->setInputEnabled(inputBefore,time);present(time,true);time+=.5;
                    uia.disconnectOwners();hostCheck(navigationAutomation->automationCount()==0,"Concealing disconnects the navigation buttons");uia.shutdown();
                }
                // Map, Calendar and the minigame (the Reader's ran in cycle 0).
                const auto month=calendar?calendar->month():endfield::modules::CalendarDay{};
                peerProbe(core::Module::map,mapModuleOwner.get(),{{"map.reset",[&]{return map->state().camera().zoom==endfield::modules::MapViewport::defaultZoom;}}});
                peerProbe(core::Module::calendar,calendarModuleOwner.get(),{{"calendar.nextMonth",[&]{return calendar->month()!=month;}},{"calendar.previousMonth",[&]{return calendar->month()==month;}}});
                peerProbe(core::Module::minigame,gameModuleOwner.get(),{{"rules",[&]{return game->rulesPresented();}},{"popover.close",[&]{return !game->rulesPresented();}}});
                // The Power navigation entry (powerCharge presents no module):
                // its group is named Power and its help is the live reading.
                if(auto*power=areaOwner("powerCharge");power&&registry.enabled(*power)&&powerArea()){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                    ComPtr<IRawElementProviderFragment>entry;
                    for(std::size_t n=0;n<power->automationCount()&&!entry;++n){ComPtr<IRawElementProviderFragment>f;if(SUCCEEDED(power->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)=="power")entry=f;}
                    hostCheck(entry&&textProperty(entry.Get(),UIA_HelpTextPropertyId)==powerArea()->powerAccessibilityLabel(),"The Power navigation entry's help is the live charge reading");
                    UiaRect r{};ComPtr<IRawElementProviderFragment>found;if(entry)(void)entry->get_BoundingRectangle(&r);
                    hostCheck(r.width>0&&r.height>0&&SUCCEEDED(uia.root()->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&found))&&same(found.Get(),entry.Get()),"The root's hit test finds the Power navigation entry");
                    uia.disconnectOwners();uia.shutdown();
                }
                // Services the host publishes for the areas.
                if(areaHost&&eventOwner)hostCheck(areaHost->find<gpu::EventLogOwner>(gpu::eventLogOwnerServiceKey)==eventOwner.get(),"The Event Log owner is published read-only for the areas");
                if(areaHost&&calendar)hostCheck(areaHost->find<endfield::areas::CalendarReminderActivationSink>(endfield::areas::calendarReminderActivationKey)!=nullptr,"Calendar publishes its reminder-click refresh");
                if(settingsUI&&updates())hostCheck(!settingsUI->controller().updateStatus().automaticTitle.empty()&&settingsUI->controller().updateStatus().automaticTitle==updates()->automaticTitle(settingsLanguage(configuration)),
                    "About's update rows come from the windowsUpdates area");
                // Settings > Display: the colour and logo dialogs are requested, never opened here.
                if(settingsUI){const auto colors=colorRequests,logos=logoRequests;requestThemeColor();chooseCenterLogo();
                    hostCheck(colorRequests==colors+1&&logoRequests==logos+1&&!themeColorQueued&&pickerOwner==PickerOwner::none,"Theme color and center logo dialogs are requested without opening in hidden coverage");}
                // The center logo (setDesktopCenterLogo): a preset and an imported
                // custom wordmark bind "desktop.center-logo" to EndfieldText and
                // hide its glow; the packet logo returns and the pixels retire.
                if(settingsUI&&materials&&!centerLogoMainID.empty()&&args.resources&&!args.resources("common.centerLogos").empty()){
                    const auto choose=[&](std::string_view choice,std::optional<std::string>revision){
                        auto next=settingsUI->controller().committed();next.set("centerLogo",std::string(choice));if(revision)next.set("centerLogoRevision",*revision);
                        settingsUI->controller().update(std::move(next),time);present(time,true);drainUtility(time);
                        for(unsigned frame=1;frame<=3;++frame)present(time+double(frame)/60,true);time+=.1;};
                    const auto bound=[&](unsigned width,unsigned height){
                        const auto&base=idCard?idCard->baseline():*desktopBaseline;const auto image=base.images.find(centerLogoMainID);const auto info=materials->textureInfo("desktop.center-logo");
                        return image!=base.images.end()&&image->second.texture=="desktop.center-logo"&&image->second.size==std::array<float,2>{float(width),float(height)}&&image->second.displaySize&&
                            std::abs((*image->second.displaySize)[1]-65)<1e-9&&base.normalMaterialNodes.contains(centerLogoMainID)&&(centerLogoGlowID.empty()||base.hiddenNodes.contains(centerLogoGlowID))&&info.has_value();};
                    choose("rhineLab",std::nullopt);
                    hostCheck(bound(692,97),"The Rhine Lab preset binds its 692 x 97 wordmark to EndfieldText and hides the glow");
                    const auto revision=gpu::importCenterLogo(args.resources("common.centerLogos")/"babel.png",args.dataRoot/"CenterLogo");
                    settingsUI->setCustomLogo(revision,time);present(time,true);drainUtility(time);for(unsigned frame=1;frame<=3;++frame)present(time+double(frame)/60,true);time+=.1;
                    hostCheck(configuration.string("centerLogo")=="custom"&&bound(648,176),"An imported custom logo is stored as CenterLogo/<revision>.png and bound");
                    choose("endfield",std::nullopt);
                    const auto&base=idCard?idCard->baseline():*desktopBaseline;
                    hostCheck(!base.images.contains(centerLogoMainID)&&!base.normalMaterialNodes.contains(centerLogoMainID)&&(centerLogoGlowID.empty()||!base.hiddenNodes.contains(centerLogoGlowID))&&!materials->textureInfo("desktop.center-logo"),
                        "The Endfield logo restores the packet artwork and retires the registered pixels");
                }
                // The desktop clock (SystemHUDView.clockPoint): hover, a style
                // indicator, a horizontal swipe and the Work Mode click.
                if(chrome&&settingsUI){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    const auto world=chrome->statusWorld();
                    const auto viewProjection=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                    const auto client=[&](core::Point local)->std::optional<core::Point>{if(!world)return std::nullopt;const auto p=core::Projection::viewport(viewProjection**world,metrics.pixelWidth,metrics.pixelHeight).project(local);
                        if(!p)return std::nullopt;return core::Point{p->x/metrics.scale,p->y/metrics.scale};};
                    const auto original=configuration.string("clockStyle");
                    const auto indicator=source::DesktopClockArtworkPlan::indicatorRect(3);
                    const auto rail=client({indicator.x+indicator.width/2,indicator.y+indicator.height/2});const auto body=client({170,50});
                    hostCheck(rail&&body&&clockPoint(*rail)&&source::DesktopClockArtworkPlan::indicatorAt(*clockPoint(*rail))==3u&&clockPoint(*body)&&!source::DesktopClockArtworkPlan::indicatorAt(*clockPoint(*body)),
                        "The status panel's projected hit test finds the clock and its style indicators");
                    if(rail&&body){
                        clockHover({PointerKind::move,PointerButton::none,body->x,body->y});const bool hovered=chromeContent.clockHovered;
                        clockHover({PointerKind::leave,PointerButton::none,body->x,body->y});
                        hostCheck(hovered&&!chromeContent.clockHovered,"Hovering the clock raises its plate; leaving restores it");
                        hostCheck(clockPress({PointerKind::down,PointerButton::left,rail->x,rail->y},time)&&settingsUI->controller().committed().string("clockStyle")=="rail","A style indicator selects that clock style");
                        present(time+.05,true);time+=.1;
                        (void)clockWheel({body->x,body->y,1,true,0,3},time);(void)clockWheel({body->x,body->y,1,true,0,3},time+.05);(void)clockWheel({body->x,body->y,1,true,0,3},time+.1);
                        hostCheck(settingsUI->controller().committed().string("clockStyle")=="stacked","A horizontal swipe over the clock advances its style once (28 points)");
                        (void)clockWheel({body->x,body->y,3,true,0,3},time+.15);
                        hostCheck(settingsUI->controller().committed().string("clockStyle")=="stacked","One swipe changes the style at most once");
                        present(time+.2,true);time+=.5;
                        hostCheck(clockPress({PointerKind::down,PointerButton::left,body->x,body->y},time)&&notes->selected()==core::Module::workMode,"Clicking the clock opens Work Mode");
                        for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                        // layoutClockControls: the clock's UIA buttons under the HUD root.
                        if(clockAutomation){HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                            ComPtr<IRawElementProviderFragment>style;std::string selected;
                            for(std::size_t n=0;n<clockAutomation->automationCount();++n){ComPtr<IRawElementProviderFragment>f;
                                if(SUCCEEDED(clockAutomation->automationChild(n,&f))&&f){const auto id=textProperty(f.Get(),UIA_AutomationIdPropertyId);
                                    if(id=="clock:style:2")style=f;if(!textProperty(f.Get(),UIA_ValueValuePropertyId).empty())selected=id;}}
                            hostCheck(clockAutomation->automationCount()==6&&selected=="clock:style:4","The clock lists Work Mode and five style buttons; the selected style's value is Selected");
                            ComPtr<IInvokeProvider>invoke;
                            hostCheck(style&&SUCCEEDED(style.As(&invoke))&&invoke->Invoke()==S_OK&&settingsUI->controller().committed().string("clockStyle")=="dial","Invoking a clock style button selects that style");
                            present(time,true);time+=.1;uia.disconnectOwners();hostCheck(clockAutomation->automationCount()==0,"Concealing disconnects the clock buttons");uia.shutdown();}
                        auto restore=settingsUI->controller().committed();restore.set("clockStyle",original);settingsUI->controller().update(std::move(restore),time);present(time,true);time+=.1;
                    }
                }
                // The clock section scrolls with a Windows mouse wheel too (the Mac
                // reads the horizontal swipe), and a style change pushes the inner
                // clipped page (HUDClockPageViewport.transition: 0.26 s,
                // easeInEaseOut, by the page width, from the right when forward).
                if(chrome&&settingsUI&&!settings.reduceMotion){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    const auto world=chrome->statusWorld();
                    const auto viewProjection=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                    std::optional<core::Point> body;
                    if(world)if(const auto p=core::Projection::viewport(viewProjection**world,metrics.pixelWidth,metrics.pixelHeight).project({170,50}))body=core::Point{p->x/metrics.scale,p->y/metrics.scale};
                    hostCheck(body&&clockPoint(*body),"The clock body is hit-tested for the wheel");
                    if(body){
                        constexpr std::array<std::string_view,5> styles{"digital","split","dial","rail","stacked"};
                        const auto original=configuration.string("clockStyle");
                        const auto styleAt=std::find(styles.begin(),styles.end(),original);const auto index=styleAt==styles.end()?0u:unsigned(styleAt-styles.begin());
                        const auto next=std::string(styles[(index+1)%5]);
                        // Three wheel notches down within the gesture window: 36 points.
                        for(unsigned n=0;n<3;++n)(void)clockWheel({body->x,body->y,-1,false,0,3},time+double(n)*.05);
                        hostCheck(settingsUI->controller().committed().string("clockStyle")==next,"A mouse wheel turned down over the clock advances its style once");
                        const double pushStart=time+.2;present(pushStart,true);
                        auto*const pages=chrome->transitionScene();
                        hostCheck(chrome->styleTransitionActive()&&pages&&std::find_if(ordered.begin(),ordered.end(),[&](const auto&e){return e.scene==pages;})!=ordered.end(),
                            "The style change starts the clock page push and composes its two pages");
                        present(pushStart+source::DesktopChromeTiming::styleDuration/2,true);
                        const auto offsets=chrome->transitionOffsets();
                        hostCheck(offsets&&std::abs((*offsets)[0]+170)<.5&&std::abs((*offsets)[1]-170)<.5,"Half way (easeInEaseOut) the outgoing page left by 170 points and the incoming one is 170 points from the right");
                        present(pushStart+.3,true);
                        hostCheck(!chrome->styleTransitionActive()&&!chrome->transitionScene()&&std::none_of(ordered.begin(),ordered.end(),[&](const auto&e){return e.scene==pages;}),"After 0.26 s the push ends and the status panel shows its page again");
                        // A wheel turned up goes back; one gesture changes the style once.
                        time=pushStart+1;for(unsigned n=0;n<3;++n)(void)clockWheel({body->x,body->y,1,false,0,3},time+double(n)*.05);
                        hostCheck(settingsUI->controller().committed().string("clockStyle")==original,"A mouse wheel turned up over the clock goes back one style");
                        present(time+.2,true);
                        hostCheck(chrome->styleTransitionActive()&&chrome->transitionOffsets()&&(*chrome->transitionOffsets())[1]<0,"Going back pushes the page in from the left");
                        // Closing cancels a running push (clockPage.cancelTransition).
                        close(time+.21);hostCheck(!chrome->styleTransitionActive(),"Closing the HUD ends the clock page push");
                        for(unsigned frame=0;frame<90&&session->phase()!=core::VisibilityPhase::concealed;++frame)present(time+.22+double(frame)/60,true);
                        hostCheck(session->phase()==core::VisibilityPhase::concealed,"The HUD closes after the push was cancelled");concealed(time+2);
                        time+=3;open(time);for(unsigned frame=0;frame<90;++frame)present(time+double(frame)/60,true);time+=2;
                        hostCheck(session->phase()==core::VisibilityPhase::visible&&session->inputEnabled(),"The HUD reopens after the clock checks");
                    }
                }
                // The industry logo (HUDSourceWatchView.flickerIndustryLogo): a click
                // flickers it (.22 over [.05,.09), .38 over [.20,.25), .46 s), its
                // press never closes the HUD, and its UI Automation button presses alike.
                if(!centerLogoMainID.empty()&&session->phase()==core::VisibilityPhase::visible){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    const auto frame=session->logoFrame();
                    const auto center=frame?std::optional(core::Point{frame->x+frame->width/2,frame->y+frame->height/2}):std::nullopt;
                    hostCheck(center&&session->logoContains(*center)&&session->sourceControlAt(*center,centerLogoMainID),"The industry logo's projected quad is a source control of the current frame");
                    if(center){
                        hostCheck(!backgroundPressCloses(*center),"A press on the industry logo never closes the HUD");
                        const double flickerStart=time+.01;
                        hostCheck(logoPointer({PointerKind::down,PointerButton::left,center->x,center->y},time)&&logoPointer({PointerKind::up,PointerButton::left,center->x,center->y},flickerStart)&&session->logoFlickering(),
                            "Clicking the industry logo starts its flicker");
                        present(flickerStart+.07,true);const auto first=session->logoOpacity();
                        present(flickerStart+.15,true);const auto between=session->logoOpacity();
                        present(flickerStart+.22,true);const auto second=session->logoOpacity();
                        hostCheck(session->demand(flickerStart+.22).finiteAnimation,"The flicker requests frames while it runs");
                        present(flickerStart+.5,true);
                        hostCheck(first==.22f&&between==1.f&&second==.38f&&session->logoOpacity()==1.f&&!session->logoFlickering(),"The logo dips to .22 and .38 and settles after .46 s");
                        time=flickerStart+1;present(time,true);
                        if(navigationAutomation){HudAutomation uia(host.hwnd(),automationSource());(void)uia.refresh();
                            ComPtr<IRawElementProviderFragment>logo;
                            for(std::size_t n=0;n<navigationAutomation->automationCount()&&!logo;++n){ComPtr<IRawElementProviderFragment>f;
                                if(SUCCEEDED(navigationAutomation->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)=="navigation:industryLogo")logo=f;}
                            hostCheck(logo&&textProperty(logo.Get(),UIA_NamePropertyId)=="ENDFIELD INDUSTRIES","The navigation lists the ENDFIELD INDUSTRIES logo button");
                            ComPtr<IInvokeProvider>invoke;
                            hostCheck(logo&&SUCCEEDED(logo.As(&invoke))&&invoke->Invoke()==S_OK&&session->logoFlickering(),"Invoking the logo button flickers the logo");
                            present(time+.6,true);time+=1;uia.disconnectOwners();uia.shutdown();}
                    }
                }
                // Display adaptation (core/source_desktop_chrome.hpp DesktopChromeProjectionPlan::contentScale):
                // on the reference Mac display the centre plane is the exact Mac
                // layout (factor 1); on larger displays the centre content keeps the
                // reference proportion to the source's main ring, in the ring's plane.
                if(chromeBindings&&chromePlan&&camera&&animation){
                    source::SourceLayout sourceLayout(*scene);
                    const auto findRing=[&](std::string_view name)->std::optional<std::size_t>{for(const auto&n:scene->nodes())if(n.name==name&&std::string_view(n.path).find("/MiddleDecoNode/")!=std::string_view::npos)return sourceLayout.nodeIndex(n.id);return std::nullopt;};
                    const auto mainRing=findRing("MainRing"),blueRing=findRing("Mid_Blue");
                    hostCheck(mainRing&&blueRing,"The source centre rings (MainRing, Mid_Blue) exist");
                    if(mainRing&&blueRing){
                        struct Measure{double content{},mainRing{},blueRing{};};
                        const auto apply=[](const core::Matrix4&m,core::Point p){const auto&v=m.values;const double w=v[3]*p.x+v[7]*p.y+v[15];return core::Point{(v[0]*p.x+v[4]*p.y+v[12])/w,(v[1]*p.x+v[5]*p.y+v[13])/w};};
                        const auto measureDisplay=[&](core::Rect viewport,double factor){
                            source::DesktopChromeProjectionPlan plan(*scene,*camera,*animation,*chromeBindings);
                            const auto frame=camera->frame({viewport.width,viewport.height},camera->rootRotation());
                            const auto pose=animation->pose(animation->entrance().lastKeyTime,{},{},frame.layout.canvasSize);
                            const auto nodes=sourceLayout.resolve({},pose.transforms);
                            (void)plan.update(nodes,frame,viewport,factor);need(plan.projection().center.has_value(),"The centre plane projects at the probe display");
                            source::DesktopChromeSettings probeSettings{viewport,1,{},core::Module::clipboard,true};probeSettings.contentScale=factor;
                            const auto layout=source::DesktopChromeLayout::make(probeSettings,*plan.projection().center,core::Matrix4{});
                            const auto hostWorld=*plan.projection().center*core::Matrix4::translation(500,layout.reportCenterY)*core::Matrix4::scale(layout.reportScale,layout.reportScale)*core::Matrix4::translation(-220,-220);
                            const auto a=apply(hostWorld,{0,220}),b=apply(hostWorld,{440,220});
                            const auto width=[&](std::size_t index){const auto&n=nodes[index];need(n.rect.has_value(),"A centre ring has a rect");const auto&r=*n.rect;const auto world=frame.worldRoot*n.worldMatrix,vp=frame.projection*frame.view;
                                const auto c=source::projectNativePoint({r.origin[0],r.origin[1]+r.size[1]*.5,0},world,vp,viewport),d=source::projectNativePoint({r.origin[0]+r.size[0],r.origin[1]+r.size[1]*.5,0},world,vp,viewport);
                                need(c&&d,"A centre ring projects at the probe display");return std::hypot(d->x-c->x,d->y-c->y);};
                            return Measure{std::hypot(b.x-a.x,b.y-a.y),width(*mainRing),width(*blueRing)};};
                        const auto reference=source::desktopReferenceViewport;const auto referenceFactor=chromePlan->contentScale(reference);
                        hostCheck(referenceFactor==1,"The reference Mac display keeps the exact Mac design scale");
                        hostCheck(contentScale==chromePlan->contentScale({0,0,metrics.width,metrics.height}),"The host planes use the display adaptation of the HUD window");
                        const auto referenceMeasure=measureDisplay(reference,1);const double referenceRatio=referenceMeasure.content/referenceMeasure.mainRing;
                        Json::Array displays;
                        for(const core::Rect v:{core::Rect{0,0,2560./1.5,1600./1.5},core::Rect{0,0,1920,1080},core::Rect{0,0,1536,864},core::Rect{0,0,1920,1200},core::Rect{0,0,2560,1440},core::Rect{0,0,3440,1440},core::Rect{0,0,1280,800}}){
                            const auto factor=chromePlan->contentScale(v);hostCheck(factor>=1&&factor<=3,"The display adaptation stays within the Mac's 1-3 clamp");const auto adapted=measureDisplay(v,factor),mac=measureDisplay(v,1);
                            const bool larger=source::desktopBaseDesignScale(v)*factor>source::desktopBaseDesignScale(v)+1e-9;
                            hostCheck(factor>=1&&(!larger||std::abs(adapted.content/adapted.mainRing/referenceRatio-1)<.01),
                                "On a "+std::to_string(int(std::lround(v.width)))+" x "+std::to_string(int(std::lround(v.height)))+" point display the centre content keeps the reference Mac proportion to the main ring");
                            displays.push_back(Json::Object{{"width",v.width},{"height",v.height},{"contentScale",factor},{"contentToMainRing",adapted.content/adapted.mainRing},{"contentToBlueRing",adapted.content/adapted.blueRing},
                                {"macFormulaContentToMainRing",mac.content/mac.mainRing},{"contentPoints",adapted.content}});
                        }
                        displayAdaptation=Json::Object{{"referenceDisplay",Json::Array{reference.width,reference.height}},{"referenceContentToMainRing",referenceRatio},{"referenceContentToBlueRing",referenceMeasure.content/referenceMeasure.blueRing},
                            {"hostContentScale",contentScale},{"displays",std::move(displays)}};
                    }
                }
                // Drag feedback over an area (ModuleOwner::dropHover): the HUD's
                // drop route reaches the owner presenting the selected module,
                // which refuses a document; a leave ends its highlight.
                if(auto*apps=areaOwner("appShortcuts");apps&&registry.enabled(*apps)&&apps->presents(core::Module::addApp)&&chromePlan&&chromePlan->projection().center){
                    selectModule(core::Module::addApp,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,core::Module::addApp);
                    const auto ps=chromeSettings(core::Module::addApp);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                    const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                    if(const auto center=plane.project({200,167})){const core::Point p{center->x/metrics.scale,center->y/metrics.scale};
                        const std::vector<std::string>documents{"C:\\Synthetic\\Notes.txt"};
                        hostCheck(dropTarget(p)==std::optional<std::string>("area")&&!dropHover(documents,p)&&dropHoverOwner==apps,"A file drag over Add App reaches the area, which refuses a document before the drop");
                        hostCheck(dropHover({},std::nullopt)&&!dropHoverOwner,"Leaving the HUD ends the area's drag feedback");}
                }
                // The Shelf navigation entry takes an OLE file drop from any other module.
                selectModule(core::Module::notes,time);session->setScrollPosition(1,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                if(const auto*sample=session->currentFrame()){
                    const auto vp=sample->camera.projection*sample->camera.view;const core::Rect viewport{0,0,metrics.width,metrics.height};
                    std::optional<core::Point> shelfPoint,otherPoint;
                    const auto entries=contentCatalog->entries();
                    for(const auto&hit:sample->sourceFrame->hits){
                        const auto action=session->actions().find(hit.buttonID);if(action==session->actions().end())continue;
                        const auto target=std::find_if(entries.begin(),entries.end(),[&](const auto&e){return e.action==action->second;});
                        if(target==entries.end())continue;
                        const auto p=source::projectNativePoint({hit.rect.origin[0]+hit.rect.size[0]*.5,hit.rect.origin[1]+hit.rect.size[1]*.5,0},hit.world,vp,viewport);
                        if(!p||session->buttonAt(*p)!=std::optional<std::string_view>(hit.buttonID))continue;
                        if(target->target=="fileShelf"&&!shelfPoint)shelfPoint=p;else if(target->target=="map"&&!otherPoint)otherPoint=p;
                    }
                    if(shelfPoint)hostCheck(dropTarget(*shelfPoint)==std::optional<std::string>("shelfNavigation"),"Dropping files on the File Shelf entry routes to the Shelf");
                    if(otherPoint)hostCheck(!dropTarget(*otherPoint),"Other navigation entries refuse file drops");
                }
            }
            close(time);present(time+.7,true);concealed(time+.7);
            // tearDownSystemPresentation: the hand-offs the cycles requested open
            // once the HUD is hidden. Storage Settings reaches only the hidden
            // fixture, exactly once however often the cycles pressed it.
            {const bool waiting=pendingStorageSettings;const auto launched=storageFixture?storageFixture->settingsRequests:0u;openAfterConcealed();
             if(storageFixture&&waiting)hostCheck(!pendingStorageSettings&&storageFixture->settingsRequests==launched+1,"Storage Settings waited for the close and opens once the HUD is hidden");}
            // The production adapter forwards the HUD visibility and the end of
            // the close: a Volume that is not the shown module keeps no artwork.

            // The provider and Shelf adapters forward the HUD visibility and the
            // end of the close (overlayConcealed) through this production route:
            // the modules visited by the cycles that are not shown at the close
            // keep no raster artwork.
            {const auto hidden=[&](const std::unique_ptr<ModuleOwner>&owner,core::Module module){return owner&&registry.enabled(*owner)&&notes->selected()!=module;};
             const auto designScene=[](const std::unique_ptr<ModuleOwner>&owner,core::Module module){return static_cast<tools::DesignModuleOwner&>(*owner).core().sceneStats(module);};
             // Clipboard, Event Log and Volume (design owners: the owner core parks a hidden canvas's scene).
             for(const auto&[owner,module,name]:{std::tuple{&clipboardModuleOwner,core::Module::clipboard,"Clipboard"},std::tuple{&eventLogModuleOwner,core::Module::eventLog,"Event Log"},
                     std::tuple{&volumeModuleOwner,core::Module::volume,"Volume"}})
                 if(hidden(*owner,module)){const auto parked=designScene(*owner,module);
                     hostCheck(parked&&parked->residentPixelBytes==0&&parked->parks>=1,std::string("Closing the HUD on another module releases the ")+name+" artwork");}
             if(activity&&hidden(activityModuleOwner,core::Module::activityMonitor)){const auto parked=designScene(activityModuleOwner,core::Module::activityMonitor);
                 hostCheck(parked&&parked->residentPixelBytes==0&&parked->parks>=1,"Closing the HUD on another module releases the Activity Monitor artwork");}
             if(storage&&hidden(storageModuleOwner,core::Module::storage)){const auto parked=designScene(storageModuleOwner,core::Module::storage);
                 hostCheck(parked&&parked->residentPixelBytes==0&&parked->parks>=1,"Closing the HUD on another module releases the Storage artwork");}
             // Work Mode and Calendar (visited by the module cycles and the UIA probes).
             if(workMode&&hidden(workModeModuleOwner,core::Module::workMode)){const auto parked=designScene(workModeModuleOwner,core::Module::workMode);
                 hostCheck(parked&&parked->residentPixelBytes==0&&parked->parks>=1,"Closing the HUD on another module releases the Work Mode artwork");}
             if(calendar&&hidden(calendarModuleOwner,core::Module::calendar)){const auto parked=designScene(calendarModuleOwner,core::Module::calendar);
                 hostCheck(parked&&parked->residentPixelBytes==0&&parked->parks>=1,"Closing the HUD on another module releases the Calendar artwork");}
             if(shelf&&hidden(shelfModuleOwner,core::Module::fileShelf))
                 hostCheck(shelf->resources().scenePixelBytes==0&&shelf->resources().sceneParks>=1,"Closing the HUD on another module releases the File Shelf artwork");}
            // Host broadcasts the module areas rely on, through the same routes
            // the visible owner uses, on the synthetic clock: suspend/resume,
            // clock/time-zone and system settings changes, utility completion.
            {const double hostTime=time+1;suspend(Suspension::system,hostTime);resume(Suspension::system,hostTime+.1);
             systemChange(WM_TIMECHANGE,hostTime+.2);systemChange(WM_SETTINGCHANGE,hostTime+.3);drainUtility(hostTime+.4);time=hostTime+.5;}
            // Production lifecycle and the red power button over the same owner.
            // Real event routes (onKey/onPointer) read the host time (here the
            // synthetic clock), so this phase continues on it.
            if(designOverlay&&!quitButtons.empty()){
                const auto settle=[&](double t){
                    if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();perform(lifecycle.didOpen(token),t);}
                    if(lifecycleClosing&&session->phase()==core::VisibilityPhase::concealed){const auto token=*lifecycleClosing;lifecycleClosing.reset();concealed(t);perform(lifecycle.didClose(token),t);openAfterConcealed();}
                };
                // Frames at 60 Hz; the shared design deadlines (the confirm
                // bar's 120 ms answer, its collapse) run between frames when
                // due, as the host's waitable timer would wake them.
                const auto run=[&](double from,double seconds){double t=from;for(unsigned n=0;n<=unsigned(seconds*60);++n){t=from+double(n)/60;
                    try{if(const auto wake=designOverlay->nextWakeTime(t);wake&&*wake<=t)(void)registry.guard(*designOverlay,[&]{(void)designOverlay->deadline(hostTime(t));},"deadline");present(t,true);settle(t);}catch(const std::exception&e){const auto&shown=notes->modulePresentation();
                        throw std::runtime_error("Lifecycle frame at "+std::to_string(t)+" phase="+std::to_string(int(lifecycle.phase()))+" visibility="+std::to_string(int(session->phase()))+
                            " selected="+std::string(core::moduleIdentifier(notes->selected()))+" current="+std::string(core::moduleIdentifier(shown.current.module))+
                            " incoming="+(shown.incoming?std::string(core::moduleIdentifier(shown.incoming->module)):std::string("none"))+": "+e.what());}
                    if(stopping)break;}return t;};
                const auto key=[&](std::uint32_t value){need(onKey({KeyKind::down,value}),"The confirm bar consumes the key");(void)onKey({KeyKind::up,value});};
                double t=std::max(now(),time+2);
                const auto lifecycleCheck=[&](bool value,const char*why){need(value,why);++lifecycleChecks;};
                lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::closed,"Lifecycle starts closed after the direct coverage close");
                perform(lifecycle.toggle(),t);lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::opening&&notes->selected()==core::Module::map,"First summon opens on Map");
                lifecycleCheck(lifecycle.toggle().empty()&&lifecycle.reopen().empty()&&lifecycle.openOverlay().empty(),"Summons during opening start nothing");
                t=run(t,1.2);lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::open,"Opening completes through the source session");
                perform(lifecycle.selectModule(core::Module::notes),t);t=run(t,.5);
                perform(lifecycle.toggle(),t);lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::closing&&closing,"Summon closes an open HUD");
                lifecycleCheck(lifecycle.toggle().empty()&&lifecycle.reopen().empty(),"Summons during closing never reopen");
                t=run(t,1.2);lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::closed&&lifecycle.lastModule()==core::Module::notes,"Close retains the last module");
                perform(lifecycle.reopen(),t);t=run(t,1.2);lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::open&&notes->selected()==core::Module::notes,"Relaunch reopens on the last module");
                // Hand-offs to other applications (OverlayController afterSystemClose,
                // openStorageAfterClose): requested while the HUD is open they close
                // it with the normal animation and open only once it is hidden, so
                // nothing opens behind its topmost band whatever "Close when focus
                // lost" says; while it is closed they open at once. The hidden run
                // counts them (links) or hands them to the Storage fixture.
                {
                    const auto handoffCheck=[&](bool value,const char*why){need(value,why);++hostChecks;};
                    using P=core::SystemOverlayPhase;
                    const auto link=[&](const char*what,const std::function<void()>&request){
                        const auto links=linkRequests;request();
                        handoffCheck(lifecycle.phase()==P::closing&&closing&&pendingLink&&linkRequests==links,what);
                        t=run(t,1.2);handoffCheck(lifecycle.phase()==P::closed&&!pendingLink&&linkRequests==links+1,"The browser opens once the HUD is hidden");
                        request();handoffCheck(lifecycle.phase()==P::closed&&!pendingLink&&linkRequests==links+2,"Requested while the HUD is closed, the browser opens at once");
                        perform(lifecycle.reopen(),t);t=run(t,1.2);};
                    if(settingsUI)link("A Settings > About link closes the HUD before the browser opens",[&]{settingsUI->controller().openLink("https://github.com/DDDuoDuo/EndfieldHUD");});
                    // The account login's WebView2 runtime download (Windows only) through the area host.
                    if(areaHost)link("The login's runtime download closes the HUD (and the login) before the browser opens",
                        [&]{areaHost->openLinkAfterClose("https://developer.microsoft.com/microsoft-edge/webview2/#download-section");});
                    if(storage&&storage->canvas()&&storageFixture&&storageModuleOwner&&registry.enabled(*storageModuleOwner)){
                        perform(lifecycle.selectModule(core::Module::storage),t);t=run(t,.8);
                        const auto before=storageFixture->settingsRequests;
                        handoffCheck(storage->canvas()->perform("storage:settings",t)&&lifecycle.phase()==P::closing&&closing&&pendingStorageSettings&&storageFixture->settingsRequests==before,
                            "Storage Settings closes the HUD before Windows Settings opens");
                        t=run(t,1.2);handoffCheck(lifecycle.phase()==P::closed&&!pendingStorageSettings&&storageFixture->settingsRequests==before+1,"Windows Storage Settings opens once the HUD is hidden");
                        perform(lifecycle.reopen(),t);t=run(t,1.2);
                        perform(lifecycle.selectModule(core::Module::notes),t);t=run(t,.8);
                        handoffCheck(lifecycle.phase()==P::open&&notes->selected()==core::Module::notes&&storageFixture->settingsRequests==before+1,"The HUD reopens after the hand-off");
                    }
                }
                // SystemHUDView.mouseDown fallthrough through the real pointer route:
                // a press on the side of the screen (the transparent overlay
                // outside the displayed circle) closes the HUD with the normal
                // closing animation; empty space inside the circle keeps it open.
                {
                    const auto outsideCheck=[&](bool value,const char*why){need(value,why);++hostChecks;};
                    const core::Point side{2,metrics.height*.5},corner{metrics.width-2,metrics.height-2};
                    outsideCheck(backgroundPressCloses(side)&&backgroundPressCloses(corner),"Presses on the sides and corners of the screen fall outside the displayed circle");
                    outsideCheck(onPointer({PointerKind::down,PointerButton::left,side.x,side.y})&&lifecycle.phase()==core::SystemOverlayPhase::closing&&closing&&session->phase()==core::VisibilityPhase::closing,
                        "A press on the side of the screen plays the normal closing animation");
                    (void)onPointer({PointerKind::up,PointerButton::left,side.x,side.y});
                    t=run(t,1.2);outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::closed&&session->phase()==core::VisibilityPhase::concealed,"The outside press finishes closing like Escape");
                    perform(lifecycle.reopen(),t);
                    // During the entrance the press is queued: the HUD closes once opening completes.
                    t=run(t,.05);outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::opening&&backgroundPressCloses(corner),"A press during the entrance is a background press");
                    (void)onPointer({PointerKind::down,PointerButton::left,corner.x,corner.y});(void)onPointer({PointerKind::up,PointerButton::left,corner.x,corner.y});
                    t=run(t,2.4);outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::closed,"A press during the entrance closes the HUD after it opened");
                    perform(lifecycle.reopen(),t);t=run(t,1.2);
                    outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::open&&notes->selected()==core::Module::notes,"The HUD reopens on its module after an outside close");
                    // The scroll arrows at the ends of the list (HUDSourceWatchView
                    // pressedScrollDirection): a press on one is the list's (the HUD
                    // stays open), its release over it scrolls with the spring, and a
                    // release elsewhere scrolls nothing; the arrows' tints follow.
                    const auto downArrow=session->scrollFrame()?session->scrollIndicatorFrame(1):std::nullopt;
                    outsideCheck(downArrow.has_value(),"The list hides rows and shows its down arrow");
                    if(downArrow){
                        const core::Point arrow{downArrow->x+downArrow->width/2,downArrow->y+downArrow->height/2};
                        session->setScrollPosition(1,t);t=run(t,.1);const double top=session->scrollPosition();
                        outsideCheck(session->scrollIndicatorEnabled(1)&&!session->scrollIndicatorEnabled(-1)&&session->scrollIndicatorAt(arrow)==1&&!backgroundPressCloses(arrow),
                            "At the top of the list the down arrow is lit, the up arrow dimmed, and a press on an arrow is no background press");
                        outsideCheck(onPointer({PointerKind::down,PointerButton::left,arrow.x,arrow.y})&&lifecycle.phase()==core::SystemOverlayPhase::open&&!closing&&session->scrollPosition()==top,
                            "A press on the down arrow is the list's: the HUD stays open and nothing moves before the release");
                        (void)onPointer({PointerKind::up,PointerButton::left,arrow.x,arrow.y});t=run(t,.6);
                        outsideCheck(session->scrollPosition()<top-1e-9&&lifecycle.phase()==core::SystemOverlayPhase::open&&session->scrollIndicatorEnabled(-1),"Its release scrolls the list down and lights the up arrow");
                        // The navigation spring (decay 11, 15 rad/s, as on the Mac) is still
                        // moving after .6 s on a short list; let it settle on its target
                        // before measuring the next press.
                        const double released=session->scrollPosition();
                        t=run(t,1.5);double moved=session->scrollPosition();unsigned windows{};
                        for(;windows<50;++windows){t=run(t,.1);const double next=session->scrollPosition();if(next==moved)break;moved=next;}
                        const bool taken=onPointer({PointerKind::down,PointerButton::left,arrow.x,arrow.y})&&scrollIndicatorPressed==std::optional(1);
                        (void)onPointer({PointerKind::up,PointerButton::left,metrics.width*.5,metrics.height*.5});t=run(t,.4);const double after=session->scrollPosition();
                        outsideCheck(taken&&after==moved&&lifecycle.phase()==core::SystemOverlayPhase::open,
                            ("A press released off the arrow scrolls nothing (settled "+std::to_string(moved)+", after "+std::to_string(after)+(taken?")":", press not taken by the arrow)")).c_str());
                        // Diagnostics: how far the step's spring still moved after .6 s.
                        const auto list=session->scrollFrame();
                        scrollArrow=Json::Object{{"listHeight",list?list->height:0.},{"top",top},{"afterSixTenths",released},{"settled",moved},
                            {"movedAfterSixTenths",released!=moved},{"extraSettleWindows",int(windows)},{"afterOffArrowRelease",after}};
                        session->setScrollPosition(1,t);t=run(t,.1);
                    }
                    // The header sanity wallet (account area; core design rect 562,2 168x42,
                    // centre 646,23, about 331 from the circle centre) is a control outside the
                    // displayed circle: its press stays with the wallet, open or during the entrance.
                    // The wallet's point through the core plane as currently posed (the
                    // entrance animates the plane, so it is projected again there).
                    const auto walletPoint=[&]()->std::optional<core::Point>{
                        if(!chromePlan||!chromePlan->projection().center)return std::nullopt;
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)**chromePlan->projection().center,metrics.pixelWidth,metrics.pixelHeight);
                        const auto q=plane.project({646,23});if(!q)return std::nullopt;return core::Point{q->x/metrics.scale,q->y/metrics.scale};};
                    if(const auto wallet=walletPoint();wallet&&registry.covers(*wallet)){
                        outsideCheck(onPointer({PointerKind::down,PointerButton::left,wallet->x,wallet->y})&&lifecycle.phase()==core::SystemOverlayPhase::open&&!closing,"A press on the header sanity wallet outside the displayed circle is the wallet's: the HUD stays open");
                        (void)onPointer({PointerKind::up,PointerButton::left,wallet->x,wallet->y});t=run(t,.4);
                        outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::open&&!closing,"Its release keeps the HUD open");
                        perform(lifecycle.toggle(),t);t=run(t,1.2);perform(lifecycle.reopen(),t);t=run(t,.05);
                        const auto entering=walletPoint();
                        outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::opening&&entering&&registry.covers(*entering)&&!backgroundPressCloses(*entering),
                            "During the entrance the wallet covers its posed point, which is no background press");
                        if(entering){(void)onPointer({PointerKind::down,PointerButton::left,entering->x,entering->y});(void)onPointer({PointerKind::up,PointerButton::left,entering->x,entering->y});}
                        t=run(t,2.4);
                        outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::open&&notes->selected()==core::Module::notes,"A press on the wallet during the entrance does not close the HUD once it opened");
                    }
                    // Empty space inside the circle, outside every control and owner.
                    std::optional<core::Point> inside;
                    if(chromePlan&&chromePlan->projection().center){
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)**chromePlan->projection().center,metrics.pixelWidth,metrics.pixelHeight);
                        for(double radius=300;radius>=180&&!inside;radius-=15)for(unsigned degrees=0;degrees<360&&!inside;degrees+=10){
                            const double a=degrees*3.14159265358979323846/180;const auto q=plane.project({500+radius*std::cos(a),320+radius*std::sin(a)});if(!q)continue;
                            const core::Point p{q->x/metrics.scale,q->y/metrics.scale};
                            if(p.x>0&&p.y>0&&p.x<metrics.width&&p.y<metrics.height&&!session->sourceControlAt(p,centerLogoMainID)&&!registry.covers(p)&&!clockPoint(p)&&!notes->covers(p))inside=p;}
                    }
                    outsideCheck(inside&&!backgroundPressCloses(*inside),"Empty space inside the displayed circle belongs to the HUD");
                    // The decision is the press's (SystemHUDView.mouseDown). Its release
                    // reaches every owner that takes releases, the hidden Reader
                    // included: after an appearance change marked it dirty, an inactive
                    // Reader ignores the release instead of re-syncing its unposed
                    // canvas (the next publish threw "Place Reader before upload").
                    if(inside){configurationPending=true;t=run(t,.05);
                        (void)onPointer({PointerKind::down,PointerButton::left,inside->x,inside->y});(void)onPointer({PointerKind::up,PointerButton::left,inside->x,inside->y});t=run(t,.4);
                        outsideCheck(lifecycle.phase()==core::SystemOverlayPhase::open&&!closing,"A press and release inside the displayed circle keep the HUD open");
                        if(readerModuleOwner)outsideCheck(registry.enabled(*readerModuleOwner),"A release over the HUD with the Reader hidden keeps the Reader owner working");}
                }
                // DataRootRestart through the host's own members (a gate opened
                // for hidden coverage only): a failed save declines it.
                const auto restartCheck=[&](bool value,const char*why){need(value,why);++hostChecks;};
                restartVerification=true;
                {auto ran=std::make_shared<unsigned>(0),declined=std::make_shared<unsigned>(0);auto token=std::make_shared<int>(0);const std::weak_ptr<int>alive=token;
                 restartCheck(restart.restartWith([ran,token]{++*ran;},[declined]{++*declined;})&&restartPending,"A data-root restart is requested through the host");
                 token.reset();reopenAfterSaveFailure(core::Module::notes,t);
                 restartCheck(!restartPending&&alive.expired()&&*ran==0&&restartDeclinePosted&&*declined==0,"A failed save destroys the pending restart work unrun and posts its decline");
                 (void)onAppMessage(NativeMessage{host.hwnd(),restartDeclinedMessage,serviceGeneration,0});
                 restartCheck(*declined==1&&!restartDeclinePosted,"The decline runs once from its notice");
                 (void)onAppMessage(NativeMessage{host.hwnd(),restartDeclinedMessage,serviceGeneration,0});
                 restartCheck(*declined==1,"A repeated notice runs no second decline");
                 t=run(t,.3);}
                // Repeated summon cycles retain no extra raster resources,
                // handles or threads once the first cycles have warmed caches.
                const auto handles=[]{DWORD count{};need(GetProcessHandleCount(GetCurrentProcess(),&count)!=FALSE,"Read own handle count");return count;};
                const auto threads=[]{const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);need(snapshot!=INVALID_HANDLE_VALUE,"Read own threads");THREADENTRY32 entry{};entry.dwSize=sizeof(entry);unsigned count{};
                    for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==GetCurrentProcessId())++count;CloseHandle(snapshot);return count;};
                const auto cycle=[&]{perform(lifecycle.toggle(),t);t=run(t,1.2);perform(lifecycle.toggle(),t);t=run(t,1.2);};
                for(unsigned n=0;n<3;++n)cycle();
                const auto warmRaster=rasterizer.stats();const auto warmHandles=handles();const auto warmThreads=threads();
                for(unsigned n=0;n<20;++n)cycle();
                lifecycleCheck(lifecycle.phase()==core::SystemOverlayPhase::open&&rasterizer.stats().entries==warmRaster.entries&&rasterizer.stats().resourceBytes==warmRaster.resourceBytes,"Twenty summon cycles retain the warmed raster cache exactly");
                lifecycleCheck(handles()<=warmHandles+8&&threads()<=warmThreads+2,"Twenty summon cycles leak no handles or threads");
                if(args.sessionEndCoverage){
                    // WM_QUERYENDSESSION drains dirty writes and keeps running;
                    // WM_ENDSESSION conceals without animation, drains and stops.
                    auto changed=settingsUI->controller().committed();changed.set("clockFormat","twelveHour");settingsSaves->save(changed);
                    lifecycleCheck(settingsSaves->status().dirty,"A preference change is pending before the session ends");
                    const auto window=static_cast<HWND>(host.hwnd());
                    lifecycleCheck(SendMessageW(window,WM_QUERYENDSESSION,0,ENDSESSION_LOGOFF)==TRUE&&!stopping&&lifecycle.phase()==core::SystemOverlayPhase::open,"Query allows the session end and keeps the HUD");
                    lifecycleCheck(!settingsSaves->status().dirty&&ehud::data::SettingsStore(args.dataRoot).value().string("clockFormat")=="twelveHour","Query drains the pending preference to disk");
                    (void)SendMessageW(window,WM_ENDSESSION,TRUE,ENDSESSION_LOGOFF);
                    lifecycleCheck(stopping&&sessionEnded&&lifecycle.phase()==core::SystemOverlayPhase::closed&&session->phase()==core::VisibilityPhase::concealed&&!host.stats().visible,"Session end conceals without animation and stops");
                    lifecycleCheck(SendMessageW(window,WM_QUERYENDSESSION,0,0)==TRUE,"Repeated session notifications stay harmless");
                }else{
                const auto quit=*quitButtons.begin();
                const auto press=[&](double at){const auto activation=session->activate(quit,at);need(activation.has_value()&&activation->action==quitAction,"QuitBtn is a clipped, cooled-down source button");activate(*activation);};
                auto&overlay=designOverlay->context().overlay();
                // A pill's centre in logical client points: box px through the
                // overlay's placement (DesktopChromeLayout::boxWorld of the centre plane).
                const auto pill=[&](std::string_view id)->core::Point{
                    for(const auto*control:overlay.router().controls())if(control->id()==id){
                        const auto r=control->rect(overlay.layer());
                        const auto camera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                        const auto plane=core::Projection::viewport(camera*source::designBoxWorld(*chromePlan->projection().center),metrics.pixelWidth,metrics.pixelHeight);
                        const auto q=plane.project({r.x+r.width*.5,r.y+r.height*.5});need(q.has_value(),"The pill projects through the tilted centre plane");
                        return {q->x/metrics.scale,q->y/metrics.scale};}
                    throw std::runtime_error("The confirm bar shows no such pill");};
                press(t+.1);t=run(t+.1,.4);
                {const auto&request=overlay.confirmState().request();
                 const auto state="QuitBtn asks on the shared confirm bar with the Mac's red quit request (prompt="+std::to_string(quitPromptOpen)+" captures="+std::to_string(overlay.capturesInput())+
                     " phase="+std::to_string(int(lifecycle.phase()))+" switching="+std::to_string(notes->modulePresentation().transitioning)+" quit="+std::to_string(quitRequested)+
                     " language="+std::string(core::languageSetting(designOverlay->context().env().language))+" id="+(request?request->id:std::string("none"))+" title="+(request?request->title:std::string())+")";
                 // The Mac pairs in the run's language (the coverage record's).
                 const auto language=designOverlay->context().env().language;
                 lifecycleCheck(quitPromptOpen&&overlay.capturesInput()&&request&&request->id=="quit"&&request->danger&&request->icon==modules::design::Icon::power&&
                     request->title==core::localized("Quit EndfieldHUD?","退出 EndfieldHUD？",language)&&
                     request->message==core::localized("Notes, shelf files and settings are saved.","便笺、暂存文件与设置均已保存。",language)&&
                     request->confirmTitle==core::localized("Quit","退出",language)&&request->cancelTitle.empty(),
                     state.c_str());}
                lifecycleCheck(registry.covers({metrics.width*.5,metrics.height*.5})&&registry.covers({1,1}),"The bar covers every underlying control");
                lifecycleCheck(designOverlay->contentFilter(t,0,1).has_value(),"The bar dims the module content");
                key(VK_ESCAPE);t=run(t,.4);lifecycleCheck(!overlay.capturesInput()&&!quitPromptOpen&&lifecycle.phase()==core::SystemOverlayPhase::open&&!quitRequested,"Escape cancels and keeps the HUD open");
                lifecycleCheck(!designOverlay->contentFilter(t,0,1).has_value(),"The dim eases out with the bar");
                press(t+1.1);t=run(t+1.1,.4);
                // The bar captures everything: a press outside the circle stays with it.
                (void)onPointer({PointerKind::down,PointerButton::left,2,metrics.height*.5});(void)onPointer({PointerKind::up,PointerButton::left,2,metrics.height*.5});t=run(t,.2);
                restartCheck(overlay.capturesInput()&&lifecycle.phase()==core::SystemOverlayPhase::open&&!closing,"A press outside the circle while the quit bar is up stays with the bar");
                const auto click=[&](core::Point p){need(onPointer({PointerKind::down,PointerButton::left,p.x,p.y}),"The bar consumes the press");need(onPointer({PointerKind::up,PointerButton::left,p.x,p.y}),"The bar consumes the release");};
                click(pill("quit:cancel"));t=run(t,.4);lifecycleCheck(!overlay.capturesInput()&&!quitPromptOpen&&!quitRequested,"Projected Cancel dismisses the bar");
                // A restart pending at the confirmed quit is accepted: its work is handed out once.
                auto restartRuns=std::make_shared<unsigned>(0);
                press(t+1.1);t=run(t+1.1,.4);
                restartCheck(restart.restartWith([restartRuns]{++*restartRuns;},[]{})&&restartPending,"A data-root restart can be requested while the bar is up");
                key(VK_RETURN);key(VK_ESCAPE);
                lifecycleCheck(!quitRequested&&lifecycle.phase()==core::SystemOverlayPhase::open&&overlay.capturesInput(),"Return answers Quit; the quit waits for the bar's 120 ms press and a second answer is swallowed");
                t=run(t,.1);lifecycleCheck(!quitRequested,"Nothing quits before 120 ms");
                t=run(t,.1);
                lifecycleCheck(quitRequested&&lifecycle.terminating()&&lifecycle.phase()==core::SystemOverlayPhase::closing&&closing,"Confirmed quit plays the closing animation");
                lifecycleCheck(lifecycle.toggle().empty()&&lifecycle.reopen().empty(),"Quitting ignores summons");
                t=run(t,1.5);
                {const auto state="The ordered drain runs after concealment, then the owner stops (stopping="+std::to_string(stopping)+" phase="+std::to_string(int(lifecycle.phase()))+
                    " bar="+std::to_string(overlay.capturesInput())+" quit="+std::to_string(quitRequested)+")";
                 lifecycleCheck(stopping&&lifecycle.phase()==core::SystemOverlayPhase::closed&&!overlay.capturesInput(),state.c_str());}
                {auto work=takeRestartWork();restartCheck(restartAccepted==false&&work&&!takeRestartWork(),"The accepted restart's work is handed out exactly once");
                 work();restartCheck(*restartRuns==1,"The handed-out work is the requested one");}
                }
            }
        }
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"moduleResults",moduleResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog->variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent?nativeContent->stats().updates:nativeAppearance->stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent?nativeContent->stats().surfaceUpdates:nativeAppearance->stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"productionLifecycleChecks",int(lifecycleChecks)},{"hostIntegrationChecks",int(hostChecks)},{"displayAdaptation",displayAdaptation},{"scrollArrow",scrollArrow},
            {"idCardConnected",bool(idCard)},{"idCardApplies",idCard?std::int64_t(idCard->stats().applies):std::int64_t{0}},{"idCardSettingsChanges",idCard?std::int64_t(idCard->stats().settingsChanges):std::int64_t{0}},
            {"settingsLinkRequests",std::int64_t(linkRequests)},{"archiveIncluded",bool(archive)},{"storageIncluded",bool(storage)},{"activityIncluded",bool(activity)},{"readerIncluded",bool(reader)},{"calendarIncluded",bool(calendar)},{"calendarUsesSyntheticNotifications",bool(calendarFixture)},{"mapIncluded",bool(map)},{"minigameIncluded",bool(game)},{"storageUsesSyntheticProviders",bool(storageFixture)},{"storageCapacityReads",storageFixture?int(storageFixture->capacityReads.load()):0},{"storageDetailScans",storageFixture?int(storageFixture->detailScans.load()):0},{"storageSettingsRequests",storageFixture?int(storageFixture->settingsRequests):0},{"appSharedMediaProvider",bool(mediaBroker)},{"limitations",Json::Array{args.moduleCoverage?"Module owners use synthetic data and hidden generated input; real clipboard/audio providers and desktop backdrop are excluded":"No module bodies, providers, persistence, user input or screen capture; hidden offscreen benchmark excludes the system backdrop","Visible preview uses the native system backdrop with original source fade/default opacity; exact blur/radial appearance is unverified","Fixed synthetic clock strings; original canvas fade does not extend source clip completion","Native caption/icon variants are limited to exact exported action-slot pairs; missing variants reject instead of fabricating artwork","CPU timings include submission/driver stalls; no GPU completion timestamp or frame-rate claim","Forced stable-idle draws are benchmark samples; the actual host schedules no ambient-off idle frames"}}};
        Json::Array areas;
        for(const auto&area:externalModules){const auto&counts=areaCounts[area.name];Json::Array presents;
            for(unsigned n=0;n<=static_cast<unsigned>(core::Module::profile);++n)if(area.owner->presents(static_cast<core::Module>(n)))presents.push_back(std::string(core::moduleIdentifier(static_cast<core::Module>(n))));
            areas.push_back(Json::Object{{"area",area.name},{"constructed",true},{"enabled",registry.enabled(*area.owner)},{"presents",std::move(presents)},{"coverageSteps",std::int64_t(counts.steps)},{"coverageChecks",std::int64_t(counts.checks)}});}
        for(const auto&name:absentModules)areas.push_back(Json::Object{{"area",name},{"constructed",false}});
        report["areaCoverage"]=std::move(areas);report["areaModuleResults"]=std::move(areaModuleResults);report["areaPresentedModules"]=std::move(areaPresentedModules);
        if(args.moduleCoverage)for(const auto&area:externalModules)need(areaCounts[area.name].steps>=3,"Every module area received its hidden coverage steps");
        ehud::data::detail::replaceFile(args.report,std::nullopt,report.encode(),8*1024*1024);std::cout<<"Wrote hidden source-shell benchmark report\n";
    shutdownVisible();
    return 0;
}
int Application::runHiddenVerification(){return impl_->runHiddenVerification();}
} // namespace endfield::app
