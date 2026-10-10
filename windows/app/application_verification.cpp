// Hidden offscreen benchmark and module coverage over the production owner.
// Linked only into watch_session_preview (hud_application_verification); the
// production EndfieldHUD.exe never contains this harness. Every input is an
// explicit synthetic fixture in a new temporary data root; nothing is shown.
#include "app/application_impl.hpp"
#include "modules/archive_presentation.hpp"
#include <iostream>
#include <set>
#include <tlhelp32.h>

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

int Application::Impl::runHiddenVerification(){
    if(args.mode!=ApplicationMode::hiddenBenchmark)throw std::logic_error("Hidden verification requires the hidden benchmark mode");
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
                            const ehud::data::SettingsStore verified(args.dataRoot);
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
                        const auto before=rasterizer.stats();const auto accepted=utility->stats().accepted;const auto uploads=renderer.stats().textureUploads;const auto poseStart=mapTime;
                        for(unsigned n=0;n<12;++n)present(poseStart+.3+double(n)/60,true);
                        need(before.rasterizations==rasterizer.stats().rasterizations&&accepted==utility->stats().accepted&&uploads==renderer.stats().textureUploads,"Settled Map poses reuse geography, text and GPU textures");
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
            // Production lifecycle and the red power button over the same owner.
            // Real event routes (onKey/onPointer) read the host clock, so this
            // phase continues on that clock after the synthetic module timeline.
            if(quitConfirmation&&!quitButtons.empty()){
                const auto settle=[&](double t){
                    if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();perform(lifecycle.didOpen(token),t);}
                    if(lifecycleClosing&&session->phase()==core::VisibilityPhase::concealed){const auto token=*lifecycleClosing;lifecycleClosing.reset();quitConfirmation->hide(t);perform(lifecycle.didClose(token),t);}
                };
                const auto run=[&](double from,double seconds){double t=from;for(unsigned n=0;n<=unsigned(seconds*60);++n){t=from+double(n)/60;present(t,true);settle(t);if(stopping)break;}return t;};
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
                const auto click=[&](core::Point p){need(onPointer({PointerKind::down,PointerButton::left,p.x,p.y}),"The card consumes the press");need(onPointer({PointerKind::up,PointerButton::left,p.x,p.y}),"The card consumes the release");};
                click({cancel->x,cancel->y});t=run(t,.4);lifecycleCheck(!quitConfirmation->presented()&&!quitRequested,"Projected Cancel dismisses the card");
                press(t+1.1);t=run(t+1.1,.4);key(VK_RETURN);t=run(t,.4);lifecycleCheck(!quitConfirmation->presented()&&!quitRequested,"Return activates the initially focused Cancel");
                press(t+1.1);t=run(t+1.1,.4);key(VK_TAB);lifecycleCheck(quitConfirmation->state().focused()==1,"Tab focuses Quit");key(VK_RETURN);
                lifecycleCheck(quitRequested&&lifecycle.terminating()&&lifecycle.phase()==core::SystemOverlayPhase::closing&&closing,"Confirmed quit plays the closing animation");
                lifecycleCheck(lifecycle.toggle().empty()&&lifecycle.reopen().empty(),"Quitting ignores summons");
                t=run(t,1.5);lifecycleCheck(stopping&&lifecycle.phase()==core::SystemOverlayPhase::closed&&!quitConfirmation->presented(),"The ordered drain runs after concealment, then the owner stops");
                }
            }
        }
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"moduleResults",moduleResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog->variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent?nativeContent->stats().updates:nativeAppearance->stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent?nativeContent->stats().surfaceUpdates:nativeAppearance->stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"productionLifecycleChecks",int(lifecycleChecks)},{"archiveIncluded",bool(archive)},{"storageIncluded",bool(storage)},{"activityIncluded",bool(activity)},{"readerIncluded",bool(reader)},{"calendarIncluded",bool(calendar)},{"calendarUsesSyntheticNotifications",bool(calendarFixture)},{"mapIncluded",bool(map)},{"minigameIncluded",bool(game)},{"storageUsesSyntheticProviders",bool(storageFixture)},{"storageCapacityReads",storageFixture?int(storageFixture->capacityReads.load()):0},{"storageDetailScans",storageFixture?int(storageFixture->detailScans.load()):0},{"storageSettingsRequests",storageFixture?int(storageFixture->settingsRequests):0},{"appSharedMediaProvider",bool(mediaBroker)},{"limitations",Json::Array{args.moduleCoverage?"Module owners use synthetic data and hidden generated input; real clipboard/audio providers and desktop backdrop are excluded":"No module bodies, providers, persistence, user input or screen capture; hidden offscreen benchmark excludes the system backdrop","Visible preview uses the native system backdrop with original source fade/default opacity; exact blur/radial appearance is unverified","Fixed synthetic clock strings; original canvas fade does not extend source clip completion","Native caption/icon variants are limited to exact exported action-slot pairs; missing variants reject instead of fabricating artwork","CPU timings include submission/driver stalls; no GPU completion timestamp or frame-rate claim","Forced stable-idle draws are benchmark samples; the actual host schedules no ambient-off idle frames"}}};
        ehud::data::detail::replaceFile(args.report,std::nullopt,report.encode(),8*1024*1024);std::cout<<"Wrote hidden source-shell benchmark report\n";
    shutdownVisible();
    return 0;
}
int Application::runHiddenVerification(){return impl_->runHiddenVerification();}
} // namespace endfield::app
