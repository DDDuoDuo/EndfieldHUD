// Hidden offscreen benchmark and module coverage over the production owner.
// Linked only into watch_session_preview (hud_application_verification); the
// production EndfieldHUD.exe never contains this harness. Every input is an
// explicit synthetic fixture in a new temporary data root; nothing is shown.
#include "app/application_impl.hpp"
#include "app/process_diagnostics.hpp"
#include "app/areas/calendar_reminder_module.hpp"
#include "modules/archive_presentation.hpp"
#include "native/display_center_logo.hpp"
#include "native/work_mode_automation.hpp"
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
        Json::Array moduleResults,areaModuleResults,areaPresentedModules;Json displayAdaptation;
        // Module areas (app/module_owner.hpp): coverage steps and their checks.
        struct AreaCounts{unsigned steps{},checks{};};std::map<std::string,AreaCounts,std::less<>> areaCounts;
        double areaLatest{};
        const auto areaFor=[&](const ModuleOwner*owner)->ExternalModule*{for(auto&area:externalModules)if(area.owner.get()==owner)return &area;return nullptr;};
        const auto runArea=[&](ExternalModule&area,std::optional<core::Module> module,unsigned cycle,double begin){
            if(!registry.enabled(*area.owner))return;
            // Coverage steps run on the one host time (ModuleRegistry::hostTime).
            auto&counts=areaCounts[area.name];double last=hostTime(std::max(begin,areaLatest));
            ModuleCoverage step;step.module=module;step.cycle=cycle;step.time=last;
            step.present=[&](double at){need(at>=last,"Area coverage presents on a non-decreasing clock");last=at;present(at,true);};
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
            // The Map adapter forwards the prewarm (MapPreview::prewarm): the first
            // opening on Map only re-poses its built chrome; nothing is activated.
            if(map&&mapModuleOwner&&registry.enabled(*mapModuleOwner)&&notes->selected()==core::Module::map)
                hostCheck(map->sceneStats().chromeBuilds>=1&&!map->state().active()&&!map->rasterStats().active,"The launch prewarm builds the Map without activating it");
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
                    present(time,true);time+=.5;(void)probe.refresh();}
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
                            const ehud::data::SettingsStore verified(args.dataRoot);
                            need(verified.value().fields["orbipom.bestScore.v1"].integer()==71&&gameSession->bestScore()==71,"Settings reset preserves saved game progress and the live session");
                        }else need(gameSession->manuallyPaused(),"Returning to Minigame preserves manual pause");
                    }
                    if(module==core::Module::map&&map){
                        const auto drainMap=[&]{for(unsigned n=0;n<24;++n){utility->waitIdle();utility->drain();map->utilityCompleted(mapTime);if(const auto due=map->nextWakeTime()){mapTime=std::max(mapTime,*due)+.000001;map->deadline(mapTime);}present(mapTime,true);if(!utility->stats().running&&!utility->stats().pending&&!utility->stats().completed&&!map->nextWakeTime())break;}};
                        mapTime=time+.7;drainMap();need(map->state().active()&&map->rasterStats().published>0,"Original bundled Map geography renders only after isolated selection");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,440,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const auto ps=chromeSettings(module);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                        const auto point=[&](core::Point p){const auto screen=plane.project(p);need(bool(screen),"Map input projects through the source440-point plane");return core::Point{screen->x/metrics.scale,screen->y/metrics.scale};};
                        const auto center=point({220,220});const auto initialCount=map->state().pins().size();
                        need(map->pointer({app::PointerKind::down,app::PointerButton::right,center.x,center.y},mapTime),"Original right-click adds a projected Map marker");map->pointer({app::PointerKind::up,app::PointerButton::right,center.x,center.y},mapTime);present(mapTime+.17,true);
                        need(map->state().pins().size()==initialCount+1&&mapStore->value().pins.size()==initialCount+1,"Marker writes only the explicitly isolated Map store");
                        need(map->pointer({app::PointerKind::down,app::PointerButton::right,center.x,center.y},mapTime),"Original marker right-click is consumed");present(mapTime+.17,true);need(map->state().pins().size()==initialCount,"Second right-click removes that marker");
                        need(map->wheel({center.x,center.y,1,false,0,3},mapTime),"Map wheel reaches the native continuous camera owner");mapTime+=.181;map->deadline(mapTime);drainMap();need(map->state().viewport().zoom>3,"Wheel zoom preserves source sensitivity");
                        need(map->perform(endfield::modules::MapAction::reset,{},mapTime),"Original reset action is available");drainMap();need(map->state().viewport().zoom==3,"Map reset preserves the authoritative3x default");
                        const auto before=rasterizer.stats();const auto accepted=utility->stats().accepted;const auto uploads=renderer.stats().textureUploads;const auto poseStart=mapTime;
                        for(unsigned n=0;n<12;++n)present(poseStart+.3+double(n)/60,true);
                        need(before.rasterizations==rasterizer.stats().rasterizations&&accepted==utility->stats().accepted&&uploads==renderer.stats().textureUploads,"Settled Map poses reuse geography, text and GPU textures");
                    }
                    if(module==core::Module::reader&&reader){
                        const auto drainReader=[&]{for(unsigned n=0;n<8;++n){utility->waitIdle();utility->drain();readerOwner->queueCapacityAvailable();}present(readerTime,true);};
                        readerTime=time+.7;drainReader();need(readerOwner->state().active()&&readerOwner->state().loaded(),"Reader loads only its isolated library on selection");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const auto ps=chromeSettings(module);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                        const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                        const auto click=[&](core::Point local,double t){const auto p=plane.project(local);need(p.has_value(),"Reader action projects through source module plane");readerTime=t;need(reader->pointer({app::PointerKind::down,app::PointerButton::left,p->x/metrics.scale,p->y/metrics.scale},t),"Reader consumes its original projected control");reader->pointer({app::PointerKind::up,app::PointerButton::left,p->x/metrics.scale,p->y/metrics.scale},t+.001);present(t+.001,true);};
                        if(cycle<2){
                            // The only file opened in this hidden path is generated here.
                            const auto fixture=args.dataRoot/(cycle?"Reader fixture.pdf":"Reader fixture.txt");std::string text;
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
                        const auto ps=chromeSettings(module);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
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
                            const auto ps=chromeSettings(module);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                            const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                            const auto click=[&](core::Point local,double t){const auto q=plane.project(local);need(bool(q),"Calendar uses the same drawn source module plane");const auto x=q->x/metrics.scale,y=q->y/metrics.scale;need(calendar->pointer({app::PointerKind::down,app::PointerButton::left,x,y},t),"Calendar consumes its source action");calendar->pointer({app::PointerKind::up,app::PointerButton::left,x,y},t);present(t,true);};
                            click({373,289},time+.72);present(time+.94,true);need(calendar->editing(),"Calendar opens its three real projected fields");
                            need(calendar->key({app::KeyKind::unicodeCharacter,'C'},time+.96,endfield::tools::CalendarKeyModifiers{}),"Calendar title receives synthetic Unicode through the shared editor");
                            click({342,357},time+1.0);need(flushCalendar(),"Calendar durable event and reminder receipts settle on the shared FIFO");present(time+1.22,true);
                            need(calendarState->events().size()==1&&calendarState->events()[0].title=="C"&&!calendar->editing(),"Calendar saves its generated draft and closes only after persistence success");
                            need(fs::is_regular_file(args.dataRoot/"Calendar"/"calendar.json")&&!calendarFixture->pending.empty(),"Calendar writes only the explicit temporary store and synthetic reminder plan");
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
                            const auto ps=chromeSettings(module);probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                            const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                            const auto click=[&](core::Point local,double t){const auto projected=plane.project(local);need(projected.has_value(),"Archive source input projects through the shared module plane");const auto x=projected->x/metrics.scale,y=projected->y/metrics.scale;need(archive->pointer({app::PointerKind::down,app::PointerButton::left,x,y},t),"Archive consumes its source action");archive->pointer({app::PointerKind::up,app::PointerButton::left,x,y},t);present(t,true);};
                            click({29,316},time+.72); // Original '+' → category menu.
                            const auto origin=endfield::modules::archiveCategoryMenuOrigin(false);click({origin.x+32,origin.y+57},time+.90);
                            utility->waitIdle();utility->drain();archiveService->queueCapacityAvailable();present(time+1.1,true);
                            need(archiveService->state().selected().has_value(),"Original category chooser creates an Uncategorized document");
                            click({50,64},time+1.12);need(archive->activeField()==std::optional<std::string>("title"),"Archive title uses the existing projected editor");
                            // The generated key states its modifiers: off the HUD window's key
                            // dispatch the thread's GetKeyState follows the live keyboard of the
                            // interactive session, so a key held on the machine would make it a
                            // modified, untyped key (README, App shortcuts item 3).
                            need(archive->key({app::KeyKind::character,'A'},time+1.14,endfield::tools::ArchiveKeyModifiers{}),"Archive accepts generated plain fixture text");need(archive->finishEditing(time+1.16),"Hidden Archive field commits without a native text manager");need(archiveService->flush(),"Archive fixture writes finish through the shared utility executor");
                            present(time+1.3,true);need(archiveService->state().selected()->title=="A","Archive persisted state retains generated title text");
                        }
                    }
                    if(module==core::Module::clipboard&&clipboard){
                        need(chromePlan&&chromePlan->projection().center,"Clipboard coverage needs the actual source plane");
                        gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,module);
                        const auto probeSettings=chromeSettings(module);
                        probePlane.update(*chromePlan->projection().center,probeSettings,notes->modulePresentation().current,1);
                        const auto probeCamera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                        const auto target=core::Projection::viewport(probeCamera*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight).project({200,160});need(target.has_value(),"Clipboard scroll target projects");
                        for(unsigned step=0;step<24;++step){const double atTime=time+.7+double(step)*.04;const auto previous=clipboard->state().scrollOffset();need(clipboard->wheel({target->x/metrics.scale,target->y/metrics.scale,step<12?-1.:1.,false,0,3},atTime),"Clipboard consumes the projected wheel input");present(atTime,true);scrollChanges+=clipboard->state().scrollOffset()!=previous;const auto usage=rasterizer.stats();peakEntries=std::max(peakEntries,usage.entries);peakBytes=std::max(peakBytes,usage.resourceBytes);}
                        need(scrollChanges>=12&&clipboard->state().scrollOffset()==0,"Clipboard moves through rows and returns to the top");
                    }
                    // Reader peers in the first cycle, with the plain-text book: a
                    // PDF page rendered late in the run leaves Windows.Data.Pdf's
                    // own D3D device to an idle timer, which still runs at process
                    // exit (README checkpoint 124, "Found while integrating").
                    if(module==core::Module::reader&&reader&&cycle==0)
                        peerProbe(core::Module::reader,readerModuleOwner.get(),{{"settings",[&]{return reader->capturesPointer();}},{"menu:close",[&]{return !reader->capturesPointer();}}});
                    if(module==core::Module::workMode&&workMode&&cycle==0){
                        // The cursor over Work Mode's custom duration field: the I-beam
                        // while it is open (HUDProjectedTextView.projectedCursor), through
                        // the owner and the host's WM_SETCURSOR route; the source cursor
                        // beside it and once it closes.
                        double start=time+.66;present(start,true);
                        need(workMode->perform("work:countdown",start)&&workMode->perform("work:custom",start+.005)&&workMode->editing(),"Work Mode opens its custom duration field through its accessible controls");
                        for(unsigned n=1;n<=6;++n)present(start+.005+double(n)/60,true);
                        const auto field=workMode->clientRect(endfield::modules::WorkModePresentation::durationEditorRect());
                        const auto beside=workMode->clientRect({40,150,0,0});
                        const core::Point center=field?core::Point{field->x+field->width/2,field->y+field->height/2}:core::Point{};
                        hostCheck(field&&workModeModuleOwner&&workModeModuleOwner->cursorAt(center)==std::optional(ModuleCursor::text),"Work Mode answers the I-beam over its open duration field");
                        // A pinned Notes card above the field would rightly keep the source cursor.
                        if(field&&!notes->covers(center))hostCheck(cursorAt(center)==ModuleCursor::text,"The host's cursor route shows the I-beam over the open duration field");
                        hostCheck(beside&&!workModeModuleOwner->cursorAt({beside->x,beside->y})&&cursorAt({beside->x,beside->y})==ModuleCursor::source,"The source cursor stays beside the open field");
                        // HUDWorkModeInteraction.layoutAccessibility: the open
                        // HUDWorkDurationField is the focused Edit peer under the HUD
                        // root; its value is the draft and SetValue replaces it.
                        {HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                         ComPtr<IRawElementProviderFragment>editor;
                         for(std::size_t n=0;n<workModeModuleOwner->automationCount()&&!editor;++n){ComPtr<IRawElementProviderFragment>f;
                             if(SUCCEEDED(workModeModuleOwner->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)==gpu::workModeDurationEditorID)editor=f;}
                         ComPtr<IRawElementProviderSimple>simple;VARIANT type,focus;VariantInit(&type);VariantInit(&focus);
                         const bool edit=editor&&SUCCEEDED(editor.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId,&type))&&type.vt==VT_I4&&type.lVal==UIA_EditControlTypeId&&
                             SUCCEEDED(simple->GetPropertyValue(UIA_HasKeyboardFocusPropertyId,&focus))&&focus.vt==VT_BOOL&&focus.boolVal==VARIANT_TRUE;
                         VariantClear(&type);VariantClear(&focus);
                         hostCheck(edit,"Work Mode's open duration field is the focused Edit peer under the HUD root");
                         ComPtr<IUnknown>pattern;ComPtr<IValueProvider>value;BSTR text=nullptr;
                         const bool read=simple&&SUCCEEDED(simple->GetPatternProvider(UIA_ValuePatternId,&pattern))&&pattern&&SUCCEEDED(pattern.As(&value))&&SUCCEEDED(value->get_Value(&text))&&text&&narrow(text)==workMode->editingText().value_or(std::string{});
                         if(text)SysFreeString(text);
                         hostCheck(read,"The duration field's UI Automation value is its draft text");
                         hostCheck(value&&value->SetValue(L"0:07")==S_OK&&workMode->editingText()==std::optional<std::string>("0:07")&&workMode->editing(),"Setting the duration field's value replaces the draft and keeps it open");
                         probe.disconnectOwners();probe.shutdown();}
                        need(workMode->finishEditing(false,start+.12)&&!workMode->editing(),"Cancelling closes the duration field");
                        present(start+.13,true);
                        hostCheck(!workModeModuleOwner->cursorAt(center)&&cursorAt(center)==ModuleCursor::source,"Closing the field restores the source cursor");
                        start+=.14;
                        // Drive the host's one Work Mode controller through its accessible
                        // controls so areas observing it (Personal Profile lifetime hours)
                        // receive real checkpoints in hidden coverage.
                        present(start,true);
                        need(workMode->perform("work:stopwatch",start)&&workMode->perform("work:start",start+.01),"Work Mode starts a stopwatch through its accessible controls");
                        for(unsigned n=1;n<=30;++n)present(start+.01+double(n)/60,true);
                        const double stop=start+.6;
                        need(workMode->perform("work:pause",stop)&&workMode->controller().snapshot(stop).phase==endfield::modules::WorkModePhase::paused&&workMode->controller().trackedWorkSeconds(stop)>0,"The stopwatch pauses with tracked time");
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
                    if(retainedCycle)need(usage.entries==retainedCycle->entries&&usage.resourceBytes==retainedCycle->resourceBytes,"Repeated combined module cycles retain no extra raster resources");
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
                    const auto*system=settingsUI->view(core::Module::system);
                    if(system&&system->view().page==modules::SettingsPage::main)
                        hostCheck(std::any_of(system->view().rows.begin(),system->view().rows.end(),[](const auto&r){return r.id=="macImport";}),"System lists Import macOS data from the macImport area");
                }
                if(volumeModuleOwner&&registry.enabled(*volumeModuleOwner)){
                    selectModule(core::Module::volume,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                    hostCheck(probe.groups()>=1&&volumeModuleOwner->automationCount()>0,"The HUD's UI Automation root lists the projected Volume controls");
                    Microsoft::WRL::ComPtr<IRawElementProviderFragment> rootFragment,group,child,parent,hit;
                    hostCheck(SUCCEEDED(probe.root()->QueryInterface(IID_PPV_ARGS(&rootFragment)))&&rootFragment,"The UIA root is a fragment");
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
                    hostCheck(rect.width>0&&rect.height>0&&SUCCEEDED(probe.root()->ElementProviderFromPoint(rect.left+rect.width/2,rect.top+rect.height/2,&hit))&&hit,"The root's hit test reaches a projected Volume control");
                    // The user-requested app list (five synthetic apps): three rows
                    // (95 points), and a wheel step moves the retained rows without
                    // laying the module out again.
                    {const auto view=volume->state().applicationViewport();const auto r=volume->clientRect({view.x+20,view.y+view.height*.5,0,0});const auto before=rasterizer.stats();
                     hostCheck(view.height==95&&r&&volume->wheel({r->x,r->y,-1,false,0,3},time)&&volume->state().applicationScroll()==36,"The Volume app list shows three rows and scrolls by one wheel detent");
                     for(unsigned frame=0;frame<10;++frame)present(time+double(frame)/60,true);time+=1;
                     hostCheck(rasterizer.stats().textLayoutsCreated-before.textLayoutsCreated<=3,"Scrolling the Volume app list lays out at most the row entering its window");}
                    // RC 128 user report (the page switch shook): Headphones / Bluetooth and
                    // back, frame by frame through the production owner.
                    {const auto s0=volume->sceneStats();
                     hostCheck(volume->perform("audio:headphones",time)&&volume->state().headphones()&&volume->requiresFrames(time),"Headphones / Bluetooth starts the source page motion");
                     unsigned frames{};while(frames<40&&volume->requiresFrames(time)){time+=1./60;present(time,true);++frames;}
                     const auto s1=volume->sceneStats();
                     hostCheck(frames>=10&&frames<=14&&s1.structureUpdates==s0.structureUpdates&&s1.detailBuilds==s0.detailBuilds+1&&s1.rowParks>s0.rowParks,"The Volume page switch plays the source 0.18 s motion, keeps the header and parks the app rows");
                     hostCheck(volume->perform("audio:applications",time)&&!volume->state().headphones(),"App volume returns");
                     frames=0;while(frames<40&&volume->requiresFrames(time)){time+=1./60;present(time,true);++frames;}
                     const auto s2=volume->sceneStats();
                     hostCheck(frames>=10&&frames<=14&&s2.rowRevivals>s1.rowRevivals&&s2.rowBuilds==s1.rowBuilds&&s2.structureUpdates==s1.structureUpdates,"Returning to App volume replays the motion and composes the parked rows again unchanged");time+=1;}
                    // App icons (Mac VolumeCanvas.renderAppIcon) through the shared
                    // utility queue: the retained rows' icons arrive with its
                    // completion, an executable without an icon resource keeps the
                    // source nil icon, Volume starts no worker of its own and the
                    // Shelf's Shell icon worker is not used.
                    if(volumeIcons&&utility){
                        const auto shelfIcons=shelf?shelf->resources().iconWorkerStarts:0;const auto workers=utility->stats().workersStarted;
                        utility->waitIdle();utilityCompleted();for(unsigned frame=0;frame<4;++frame)present(time+double(frame)/60,true);time+=1;
                        const auto rows=volume->visibleIcons();const auto&apps=volume->state().snapshot().applications;
                        std::size_t withIcon{},without{};bool exact=!rows.empty();
                        for(const auto&row:rows){const auto app=std::find_if(apps.begin(),apps.end(),[&](const auto&a){return a.id==row.applicationID;});
                            if(app==apps.end()||!row.executable){exact=false;continue;}
                            if(row.executable->identity.objectID[0]!=3){++withIcon;exact=exact&&app->icon&&volumeIcons->images().acquire(app->icon->imageKey,app->icon->revision)!=nullptr;}
                            else{++without;exact=exact&&!app->icon;}}
                        hostCheck(exact&&withIcon>=2&&without==1,"Volume's retained app rows show their executables' icons from the shared queue; one without an icon resource keeps the nil icon");
                        const auto loaded=volumeIcons->stats();
                        hostCheck(loaded.jobs>=1&&loaded.published>=withIcon&&!loaded.inFlight&&utility->stats().workersStarted==workers&&(!shelf||shelf->resources().iconWorkerStarts==shelfIcons),
                            "Volume icons load on the shared utility worker: no thread of their own, and the Shelf's Shell icon worker is untouched");
                        hostCheck(volume->perform("audio:headphones",time)&&!volumeIcons->plan().active()&&volume->visibleIcons().empty(),"Leaving App volume withdraws its icon requests");
                        for(unsigned frame=0;frame<20;++frame)present(time+double(frame)/60,true);time+=1;
                        const bool back=volume->perform("audio:applications",time);const auto&again=volume->state().snapshot().applications;
                        hostCheck(back&&volumeIcons->plan().active()&&volumeIcons->stats().jobs==loaded.jobs&&!volume->visibleIcons().empty()&&
                            std::all_of(volume->visibleIcons().begin(),volume->visibleIcons().end(),[&](const auto&row){const auto app=std::find_if(again.begin(),again.end(),[&](const auto&a){return a.id==row.applicationID;});
                                return app!=again.end()&&(row.executable&&row.executable->identity.objectID[0]==3?!app->icon:app->icon.has_value());}),
                            "Returning to App volume binds the cached icons at once, without loading them again");
                        for(unsigned frame=0;frame<20;++frame)present(time+double(frame)/60,true);time+=1;
                    }
                    probe.disconnectOwners();hostCheck(volumeModuleOwner->automationCount()==0&&probe.groups()==0,"Concealing disconnects every fragment");
                    probe.shutdown();
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
                groupProbe(core::Module::nowPlaying,areaOwner("nowPlaying"),std::size_t{6});
                groupProbe(core::Module::account,areaOwner("account"),std::nullopt);
                groupProbe(core::Module::profile,areaOwner("profile"),std::nullopt);
                // The provider modules' Mac accessibility buttons (and the telemetry
                // status text) through their built-in adapters.
                groupProbe(core::Module::clipboard,clipboardModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::eventLog,eventLogModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::activityMonitor,activityModuleOwner.get(),std::nullopt);
                groupProbe(core::Module::storage,storageModuleOwner.get(),std::nullopt);
                // Keyboard focus under the real root (GetFocus): the open Notes
                // editor is the focused element of the root and of its group;
                // finishing it leaves no Notes element focused.
                if(notesModuleOwner&&registry.enabled(*notesModuleOwner)){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    std::vector<gpu::NotesAutomationElement>elements;notes->automationElements(elements);std::string edit;
                    for(const auto&e:elements)if(e.enabled&&e.id.starts_with("note:")&&e.id.ends_with(":edit")){edit=e.id;break;}
                    hostCheck(!edit.empty()&&notes->perform(edit,time),"A Notes text card opens its editor through its accessible Edit action");
                    for(unsigned frame=1;frame<=20;++frame)present(time+double(frame)/60,true);time+=.5;
                    HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                    ComPtr<IRawElementProviderFragment>focusedRoot,focusedGroup;ComPtr<IRawElementProviderFragmentRoot>groupRoot;
                    const auto group=findGroup(probe,moduleCaption("notes",watchAppearance.language,"notes"));
                    hostCheck(SUCCEEDED(probe.root()->GetFocus(&focusedRoot))&&focusedRoot&&textProperty(focusedRoot.Get(),UIA_AutomationIdPropertyId).starts_with("notes:editor:"),
                        "The HUD root's GetFocus returns the open Notes editor");
                    hostCheck(group&&SUCCEEDED(group.As(&groupRoot))&&SUCCEEDED(groupRoot->GetFocus(&focusedGroup))&&same(focusedGroup.Get(),focusedRoot.Get()),
                        "The Notes group's GetFocus returns the same editor");
                    hostCheck(notes->finish(),"The Notes editor finishes");present(time,true);time+=.5;(void)probe.refresh();
                    ComPtr<IRawElementProviderFragment>afterwards;
                    hostCheck(SUCCEEDED(probe.root()->GetFocus(&afterwards))&&(!afterwards||!textProperty(afterwards.Get(),UIA_AutomationIdPropertyId).starts_with("notes:editor:")),
                        "No Notes editor holds the focus once it finished");
                    probe.disconnectOwners();probe.shutdown();
                }
                // The source navigation's accessibility buttons (HUDSourceWatchView):
                // bound entries carry their titles and the selected module the
                // value Selected, the red power button is listed, the root's hit
                // test reaches an entry, and Scroll modules down scrolls like the
                // Mac scrollDesktopNavigation (with source input enabled, restored afterwards).
                if(navigationAutomation&&registry.enabled(*navigationAutomation)){
                    selectModule(core::Module::notes,time);session->setScrollPosition(1,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    const bool inputBefore=session->inputEnabled();session->setInputEnabled(true,time);present(time,true);
                    HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
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
                    hostCheck(r.width>0&&r.height>0&&SUCCEEDED(probe.root()->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&found))&&same(found.Get(),notesEntry.Get()),
                        "The root's hit test finds the Notes navigation entry");
                    const auto enabled=[&](const ComPtr<IRawElementProviderFragment>&f){ComPtr<IRawElementProviderSimple>simple;VARIANT v;VariantInit(&v);
                        const bool on=f&&SUCCEEDED(f.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_IsEnabledPropertyId,&v))&&v.vt==VT_BOOL&&v.boolVal==VARIANT_TRUE;VariantClear(&v);return on;};
                    hostCheck(scrollUp&&scrollDown&&!enabled(scrollUp)&&enabled(scrollDown),"At the top only Scroll modules down is enabled");
                    const double before=session->scrollPosition();ComPtr<IInvokeProvider>invoke;
                    hostCheck(scrollDown&&SUCCEEDED(scrollDown.As(&invoke))&&invoke->Invoke()==S_OK&&session->scrollPosition()<before-1e-9,"Scroll modules down scrolls the navigation like the Mac button");
                    for(unsigned frame=1;frame<=20;++frame)present(time+double(frame)/60,true);time+=.5;(void)probe.refresh();
                    hostCheck(enabled(scrollUp),"After scrolling, Scroll modules up is enabled");
                    session->setScrollPosition(1,time);session->setInputEnabled(inputBefore,time);present(time,true);time+=.5;
                    probe.disconnectOwners();hostCheck(navigationAutomation->automationCount()==0,"Concealing disconnects the navigation buttons");probe.shutdown();
                }
                // Map, Calendar and the minigame (the Reader's ran in cycle 0).
                const auto month=calendar?calendar->view().month():endfield::modules::CalendarDay{};
                peerProbe(core::Module::map,mapModuleOwner.get(),{{"map:reset",[&]{return map->state().viewport().zoom==endfield::modules::MapViewport::defaultZoom;}}});
                peerProbe(core::Module::calendar,calendarModuleOwner.get(),{{"nextMonth",[&]{return calendar->view().month()!=month;}},{"previousMonth",[&]{return calendar->view().month()==month;}}});
                peerProbe(core::Module::minigame,gameModuleOwner.get(),{{"rules",[&]{return game->state().rulesPresented();}},{"rules:close",[&]{return !game->state().rulesPresented();}}});
                // The Power navigation entry (powerCharge presents no module):
                // its group is named Power and its help is the live reading.
                if(auto*power=areaOwner("powerCharge");power&&registry.enabled(*power)&&powerArea()){
                    selectModule(core::Module::notes,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                    ComPtr<IRawElementProviderFragment>entry;
                    for(std::size_t n=0;n<power->automationCount()&&!entry;++n){ComPtr<IRawElementProviderFragment>f;if(SUCCEEDED(power->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)=="power")entry=f;}
                    hostCheck(entry&&textProperty(entry.Get(),UIA_HelpTextPropertyId)==powerArea()->powerAccessibilityLabel(),"The Power navigation entry's help is the live charge reading");
                    UiaRect r{};ComPtr<IRawElementProviderFragment>found;if(entry)(void)entry->get_BoundingRectangle(&r);
                    hostCheck(r.width>0&&r.height>0&&SUCCEEDED(probe.root()->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&found))&&same(found.Get(),entry.Get()),"The root's hit test finds the Power navigation entry");
                    probe.disconnectOwners();probe.shutdown();
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
                    const auto camera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                    const auto client=[&](core::Point local)->std::optional<core::Point>{if(!world)return std::nullopt;const auto p=core::Projection::viewport(camera**world,metrics.pixelWidth,metrics.pixelHeight).project(local);
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
                        if(clockAutomation){HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                            ComPtr<IRawElementProviderFragment>style;std::string selected;
                            for(std::size_t n=0;n<clockAutomation->automationCount();++n){ComPtr<IRawElementProviderFragment>f;
                                if(SUCCEEDED(clockAutomation->automationChild(n,&f))&&f){const auto id=textProperty(f.Get(),UIA_AutomationIdPropertyId);
                                    if(id=="clock:style:2")style=f;if(!textProperty(f.Get(),UIA_ValueValuePropertyId).empty())selected=id;}}
                            hostCheck(clockAutomation->automationCount()==6&&selected=="clock:style:4","The clock lists Work Mode and five style buttons; the selected style's value is Selected");
                            ComPtr<IInvokeProvider>invoke;
                            hostCheck(style&&SUCCEEDED(style.As(&invoke))&&invoke->Invoke()==S_OK&&settingsUI->controller().committed().string("clockStyle")=="dial","Invoking a clock style button selects that style");
                            present(time,true);time+=.1;probe.disconnectOwners();hostCheck(clockAutomation->automationCount()==0,"Concealing disconnects the clock buttons");probe.shutdown();}
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
                    const auto camera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
                    std::optional<core::Point> body;
                    if(world)if(const auto p=core::Projection::viewport(camera**world,metrics.pixelWidth,metrics.pixelHeight).project({170,50}))body=core::Point{p->x/metrics.scale,p->y/metrics.scale};
                    hostCheck(body&&clockPoint(*body),"The clock body is hit-tested for the wheel");
                    if(body){
                        constexpr std::array<std::string_view,5> styles{"digital","split","dial","rail","stacked"};
                        const auto original=configuration.string("clockStyle");
                        const auto at=std::find(styles.begin(),styles.end(),original);const auto index=at==styles.end()?0u:unsigned(at-styles.begin());
                        const auto next=std::string(styles[(index+1)%5]);
                        // Three wheel notches down within the gesture window: 36 points.
                        for(unsigned n=0;n<3;++n)(void)clockWheel({body->x,body->y,-1,false,0,3},time+double(n)*.05);
                        hostCheck(settingsUI->controller().committed().string("clockStyle")==next,"A mouse wheel turned down over the clock advances its style once");
                        const double start=time+.2;present(start,true);
                        auto*const pages=chrome->transitionScene();
                        hostCheck(chrome->styleTransitionActive()&&pages&&std::find_if(ordered.begin(),ordered.end(),[&](const auto&e){return e.scene==pages;})!=ordered.end(),
                            "The style change starts the clock page push and composes its two pages");
                        present(start+source::DesktopChromeTiming::styleDuration/2,true);
                        const auto offsets=chrome->transitionOffsets();
                        hostCheck(offsets&&std::abs((*offsets)[0]+170)<.5&&std::abs((*offsets)[1]-170)<.5,"Half way (easeInEaseOut) the outgoing page left by 170 points and the incoming one is 170 points from the right");
                        present(start+.3,true);
                        hostCheck(!chrome->styleTransitionActive()&&!chrome->transitionScene()&&std::none_of(ordered.begin(),ordered.end(),[&](const auto&e){return e.scene==pages;}),"After 0.26 s the push ends and the status panel shows its page again");
                        // A wheel turned up goes back; one gesture changes the style once.
                        time=start+1;for(unsigned n=0;n<3;++n)(void)clockWheel({body->x,body->y,1,false,0,3},time+double(n)*.05);
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
                        const double start=time+.01;
                        hostCheck(logoPointer({PointerKind::down,PointerButton::left,center->x,center->y},time)&&logoPointer({PointerKind::up,PointerButton::left,center->x,center->y},start)&&session->logoFlickering(),
                            "Clicking the industry logo starts its flicker");
                        present(start+.07,true);const auto first=session->logoOpacity();
                        present(start+.15,true);const auto between=session->logoOpacity();
                        present(start+.22,true);const auto second=session->logoOpacity();
                        hostCheck(session->demand(start+.22).finiteAnimation,"The flicker requests frames while it runs");
                        present(start+.5,true);
                        hostCheck(first==.22f&&between==1.f&&second==.38f&&session->logoOpacity()==1.f&&!session->logoFlickering(),"The logo dips to .22 and .38 and settles after .46 s");
                        time=start+1;present(time,true);
                        if(navigationAutomation){HudAutomation probe(host.hwnd(),automationSource());(void)probe.refresh();
                            ComPtr<IRawElementProviderFragment>logo;
                            for(std::size_t n=0;n<navigationAutomation->automationCount()&&!logo;++n){ComPtr<IRawElementProviderFragment>f;
                                if(SUCCEEDED(navigationAutomation->automationChild(n,&f))&&f&&textProperty(f.Get(),UIA_AutomationIdPropertyId)=="navigation:industryLogo")logo=f;}
                            hostCheck(logo&&textProperty(logo.Get(),UIA_NamePropertyId)=="ENDFIELD INDUSTRIES","The navigation lists the ENDFIELD INDUSTRIES logo button");
                            ComPtr<IInvokeProvider>invoke;
                            hostCheck(logo&&SUCCEEDED(logo.As(&invoke))&&invoke->Invoke()==S_OK&&session->logoFlickering(),"Invoking the logo button flickers the logo");
                            present(time+.6,true);time+=1;probe.disconnectOwners();probe.shutdown();}
                    }
                }
                // Display adaptation (core/source_desktop_chrome.hpp desktopContentScale):
                // on the reference Mac display the centre plane is the exact Mac
                // layout (factor 1); on larger displays the centre content keeps the
                // reference proportion to the source's main ring, in the ring's plane.
                if(chromeBindings&&camera&&animation){
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
                        const auto reference=source::desktopReferenceViewport;const auto referenceFactor=source::desktopContentScale(*camera,reference);
                        hostCheck(referenceFactor==1,"The reference Mac display keeps the exact Mac design scale");
                        hostCheck(contentScale==source::desktopContentScale(*camera,{0,0,metrics.width,metrics.height}),"The host planes use the display adaptation of the HUD window");
                        const auto referenceMeasure=measureDisplay(reference,1);const double referenceRatio=referenceMeasure.content/referenceMeasure.mainRing;
                        Json::Array displays;
                        for(const core::Rect v:{core::Rect{0,0,2560./1.5,1600./1.5},core::Rect{0,0,1920,1080},core::Rect{0,0,1536,864},core::Rect{0,0,1920,1200},core::Rect{0,0,2560,1440},core::Rect{0,0,3440,1440},core::Rect{0,0,1280,800}}){
                            const auto factor=source::desktopContentScale(*camera,v);const auto adapted=measureDisplay(v,factor),mac=measureDisplay(v,1);
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
                        const std::vector<std::string>document{"C:\\Synthetic\\Notes.txt"};
                        hostCheck(dropTarget(p)==std::optional<std::string>("area")&&!dropHover(document,p)&&dropHoverOwner==apps,"A file drag over Add App reaches the area, which refuses a document before the drop");
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
            // The production adapter forwards the HUD visibility and the end of
            // the close: a Volume that is not the shown module keeps no artwork.
            if(volume&&volumeModuleOwner&&registry.enabled(*volumeModuleOwner)&&notes->selected()!=core::Module::volume)
                hostCheck(volume->sceneStats().parts==0&&volume->sceneStats().pixelBytes==0&&volume->sceneStats().parks>=1,"Closing the HUD on another module releases every Volume raster");
            // Work Mode (visited by the module cycles and the UIA probes) releases its
            // rasters and GPU resources once the HUD closed on another module.
            if(workMode&&workModeModuleOwner&&registry.enabled(*workModeModuleOwner)&&notes->selected()!=core::Module::workMode)
                hostCheck(workMode->resources().sceneParts==0&&!workMode->resources().gpuResident&&workMode->resources().parks>=1,"Closing the HUD on another module releases the Work Mode page");
            // The provider and Shelf adapters forward the HUD visibility and the
            // end of the close (overlayConcealed) through this production route:
            // the modules visited by the cycles that are not shown at the close
            // keep no raster artwork.
            {const auto hidden=[&](const std::unique_ptr<ModuleOwner>&owner,core::Module module){return owner&&registry.enabled(*owner)&&notes->selected()!=module;};
             if(clipboard&&hidden(clipboardModuleOwner,core::Module::clipboard))
                 hostCheck(clipboard->stats().residentPixelBytes==0&&clipboard->stats().parks>=1,"Closing the HUD on another module releases the Clipboard artwork");
             if(eventLog&&hidden(eventLogModuleOwner,core::Module::eventLog))
                 hostCheck(eventLog->stats().residentPixelBytes==0&&eventLog->stats().parks>=1,"Closing the HUD on another module releases the Event Log artwork");
             if(activity&&hidden(activityModuleOwner,core::Module::activityMonitor))
                 hostCheck(activity->stats().residentPixelBytes==0&&activity->stats().parks>=1,"Closing the HUD on another module releases the Activity Monitor artwork");
             if(storage&&hidden(storageModuleOwner,core::Module::storage))
                 hostCheck(storage->resources().pixelBytes==0&&storage->resources().parks>=1,"Closing the HUD on another module releases the Storage artwork");
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
            if(quitConfirmation&&!quitButtons.empty()){
                const auto settle=[&](double t){
                    if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();perform(lifecycle.didOpen(token),t);}
                    if(lifecycleClosing&&session->phase()==core::VisibilityPhase::concealed){const auto token=*lifecycleClosing;lifecycleClosing.reset();quitConfirmation->hide(t);concealed(t);perform(lifecycle.didClose(token),t);}
                };
                const auto run=[&](double from,double seconds){double t=from;for(unsigned n=0;n<=unsigned(seconds*60);++n){t=from+double(n)/60;
                    try{present(t,true);settle(t);}catch(const std::exception&e){const auto&shown=notes->modulePresentation();
                        throw std::runtime_error("Lifecycle frame at "+std::to_string(t)+" phase="+std::to_string(int(lifecycle.phase()))+" visibility="+std::to_string(int(session->phase()))+
                            " selected="+std::string(core::moduleIdentifier(notes->selected()))+" current="+std::string(core::moduleIdentifier(shown.current.module))+
                            " incoming="+(shown.incoming?std::string(core::moduleIdentifier(shown.incoming->module)):std::string("none"))+": "+e.what());}
                    if(stopping)break;}return t;};
                const auto key=[&](std::uint32_t value){need(onKey({KeyKind::down,value}),"The modal card consumes the key");(void)onKey({KeyKind::up,value});};
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
                        const double moved=session->scrollPosition();
                        (void)onPointer({PointerKind::down,PointerButton::left,arrow.x,arrow.y});(void)onPointer({PointerKind::up,PointerButton::left,metrics.width*.5,metrics.height*.5});t=run(t,.4);
                        outsideCheck(session->scrollPosition()==moved&&lifecycle.phase()==core::SystemOverlayPhase::open,"A press released off the arrow scrolls nothing");
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
                press(t+.1);t=run(t+.1,.4);lifecycleCheck(quitConfirmation->presented()&&quitConfirmation->state().focused()==0,"QuitBtn presents the card with Cancel focused");
                lifecycleCheck(registry.covers({metrics.width*.5,metrics.height*.5})&&registry.covers({1,1}),"The card covers every underlying control");
                key(VK_ESCAPE);t=run(t,.4);lifecycleCheck(!quitConfirmation->presented()&&lifecycle.phase()==core::SystemOverlayPhase::open&&!quitRequested,"Escape cancels and keeps the HUD open");
                press(t+1.1);t=run(t+1.1,.4);const auto cancel=quitConfirmation->actionPoint(false);need(cancel.has_value(),"Cancel projects through the tilted card");
                // A modal card unwinds first: a press outside the circle stays with it.
                (void)onPointer({PointerKind::down,PointerButton::left,2,metrics.height*.5});(void)onPointer({PointerKind::up,PointerButton::left,2,metrics.height*.5});t=run(t,.2);
                restartCheck(quitConfirmation->presented()&&lifecycle.phase()==core::SystemOverlayPhase::open&&!closing,"A press outside the circle while the quit card is up stays with the card");
                const auto click=[&](core::Point p){need(onPointer({PointerKind::down,PointerButton::left,p.x,p.y}),"The card consumes the press");need(onPointer({PointerKind::up,PointerButton::left,p.x,p.y}),"The card consumes the release");};
                click({cancel->x,cancel->y});t=run(t,.4);lifecycleCheck(!quitConfirmation->presented()&&!quitRequested,"Projected Cancel dismisses the card");
                press(t+1.1);t=run(t+1.1,.4);key(VK_RETURN);t=run(t,.4);lifecycleCheck(!quitConfirmation->presented()&&!quitRequested,"Return activates the initially focused Cancel");
                press(t+1.1);t=run(t+1.1,.4);key(VK_TAB);lifecycleCheck(quitConfirmation->state().focused()==1,"Tab focuses Quit");
                // A restart pending at the confirmed quit is accepted: its work is handed out once.
                auto restartRuns=std::make_shared<unsigned>(0);
                restartCheck(restart.restartWith([restartRuns]{++*restartRuns;},[]{})&&restartPending,"A data-root restart can be requested while the card is up");
                key(VK_RETURN);
                lifecycleCheck(quitRequested&&lifecycle.terminating()&&lifecycle.phase()==core::SystemOverlayPhase::closing&&closing,"Confirmed quit plays the closing animation");
                lifecycleCheck(lifecycle.toggle().empty()&&lifecycle.reopen().empty(),"Quitting ignores summons");
                t=run(t,1.5);
                {const auto state="The ordered drain runs after concealment, then the owner stops (stopping="+std::to_string(stopping)+" phase="+std::to_string(int(lifecycle.phase()))+
                    " card="+std::to_string(quitConfirmation->presented())+" quit="+std::to_string(quitRequested)+")";
                 lifecycleCheck(stopping&&lifecycle.phase()==core::SystemOverlayPhase::closed&&!quitConfirmation->presented(),state.c_str());}
                {auto work=takeRestartWork();restartCheck(restartAccepted==false&&work&&!takeRestartWork(),"The accepted restart's work is handed out exactly once");
                 work();restartCheck(*restartRuns==1,"The handed-out work is the requested one");}
                }
            }
        }
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"moduleResults",moduleResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog->variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent?nativeContent->stats().updates:nativeAppearance->stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent?nativeContent->stats().surfaceUpdates:nativeAppearance->stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"productionLifecycleChecks",int(lifecycleChecks)},{"hostIntegrationChecks",int(hostChecks)},{"displayAdaptation",displayAdaptation},
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
