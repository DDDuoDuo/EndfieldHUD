// Hidden offscreen benchmark and module coverage over the production owner.
// Linked only into watch_session_preview (hud_application_verification); the
// production EndfieldHUD.exe never contains this harness. Every input is an
// explicit synthetic fixture in a new temporary data root; nothing is shown.
#include "app/application_impl.hpp"
#include "app/areas/calendar_reminder_module.hpp"
#include "modules/archive_presentation.hpp"
#include "native/display_center_logo.hpp"
#include <algorithm>
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
        Json::Array moduleResults,areaModuleResults,areaPresentedModules;
        // Module areas (app/module_owner.hpp): coverage steps and their checks.
        struct AreaCounts{unsigned steps{},checks{};};std::map<std::string,AreaCounts,std::less<>> areaCounts;
        double areaLatest{};
        const auto areaFor=[&](const ModuleOwner*owner)->ExternalModule*{for(auto&area:externalModules)if(area.owner.get()==owner)return &area;return nullptr;};
        const auto runArea=[&](ExternalModule&area,std::optional<core::Module> module,unsigned cycle,double begin){
            if(!registry.enabled(*area.owner))return;
            auto&counts=areaCounts[area.name];double last=std::max(begin,areaLatest);
            ModuleCoverage step;step.module=module;step.cycle=cycle;step.time=last;
            step.present=[&](double at){need(at>=last,"Area coverage presents on a non-decreasing clock");last=at;present(at,true);};
            step.drain=[&]{drainUtility(last);};
            step.project=[&,module](core::Point local,double width,double height)->std::optional<core::Point>{
                if(!module||!notes||!chromePlan||!chromePlan->projection().center||!(width>0)||!(height>0))return std::nullopt;
                gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,width,height}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,*module);
                const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,*module,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
                const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)*probePlane.pose().contentWorld,metrics.pixelWidth,metrics.pixelHeight);
                const auto screen=plane.project(local);if(!screen)return std::nullopt;return core::Point{screen->x/metrics.scale,screen->y/metrics.scale};};
            step.check=[&](bool value,std::string_view why){++counts.checks;if(!value)throw std::runtime_error(std::string(why));};
            step.message=[&](std::uint32_t id,std::uintptr_t wParam,std::intptr_t lParam){
                const NativeMessage m{host.hwnd(),id,wParam,lParam};
                return registry.first(ModuleRegistry::Chain::message,[&](ModuleOwner&o,const ModuleRouting&){return o.message(m,last);},"message")!=nullptr;};
            ++counts.steps;
            try{area.owner->coverage(step);}catch(const std::exception&e){throw std::runtime_error("Module area "+area.name+" coverage: "+e.what());}
            areaLatest=std::max(areaLatest,last);
        };
        if(args.moduleCoverage){
            double time=at(100);open(time);present(time+1,true);time+=2;
            // Host integration checks (counted in hostIntegrationChecks) and the
            // UI Automation helpers they share.
            const auto hostCheck=[&](bool value,const std::string&why){need(value,why.c_str());++hostChecks;};
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
                    // Reader peers in the first cycle, with the plain-text book: a
                    // PDF page rendered late in the run leaves Windows.Data.Pdf's
                    // own D3D device to an idle timer, which still runs at process
                    // exit (README checkpoint 124, "Found while integrating").
                    if(module==core::Module::reader&&reader&&cycle==0)
                        peerProbe(core::Module::reader,readerModuleOwner.get(),{{"settings",[&]{return reader->capturesPointer();}},{"menu:close",[&]{return !reader->capturesPointer();}}});
                    if(module==core::Module::workMode&&workMode&&cycle==0){
                        // Drive the host's one Work Mode controller through its accessible
                        // controls so areas observing it (Personal Profile lifetime hours)
                        // receive real checkpoints in hidden coverage.
                        const double start=time+.66;present(start,true);
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
                                    if(id=="clock:style:2")style=f;if(!textProperty(f.Get(),UIA_HelpTextPropertyId).empty())selected=id;}}
                            hostCheck(clockAutomation->automationCount()==6&&selected=="clock:style:4","The clock lists Work Mode and five style buttons; the selected style reports Selected");
                            ComPtr<IInvokeProvider>invoke;
                            hostCheck(style&&SUCCEEDED(style.As(&invoke))&&invoke->Invoke()==S_OK&&settingsUI->controller().committed().string("clockStyle")=="dial","Invoking a clock style button selects that style");
                            present(time,true);time+=.1;probe.disconnectOwners();hostCheck(clockAutomation->automationCount()==0,"Concealing disconnects the clock buttons");probe.shutdown();}
                        auto restore=settingsUI->controller().committed();restore.set("clockStyle",original);settingsUI->controller().update(std::move(restore),time);present(time,true);time+=.1;
                    }
                }
                // Drag feedback over an area (ModuleOwner::dropHover): the HUD's
                // drop route reaches the owner presenting the selected module,
                // which refuses a document; a leave ends its highlight.
                if(auto*apps=areaOwner("appShortcuts");apps&&registry.enabled(*apps)&&apps->presents(core::Module::addApp)&&chromePlan&&chromePlan->projection().center){
                    selectModule(core::Module::addApp,time);for(unsigned frame=0;frame<40;++frame)present(time+double(frame)/60,true);time+=1;
                    gpu::LayerScene probeGeometry(rasterizer);probeGeometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface probePlane(probeGeometry,core::Module::addApp);
                    const source::DesktopChromeSettings ps{{0,0,metrics.width,metrics.height},settings.hudScale,settings.hudOffset,core::Module::addApp,true};probePlane.update(*chromePlan->projection().center,ps,notes->modulePresentation().current,1);
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
            // Host broadcasts the module areas rely on, through the same routes
            // the visible owner uses, on the synthetic clock: suspend/resume,
            // clock/time-zone and system settings changes, utility completion.
            {const double hostTime=time+1;suspend(Suspension::system,hostTime);resume(Suspension::system,hostTime+.1);
             systemChange(WM_TIMECHANGE,hostTime+.2);systemChange(WM_SETTINGCHANGE,hostTime+.3);drainUtility(hostTime+.4);time=hostTime+.5;}
            // Production lifecycle and the red power button over the same owner.
            // Real event routes (onKey/onPointer) read the host clock, so this
            // phase continues on that clock after the synthetic module timeline.
            if(quitConfirmation&&!quitButtons.empty()){
                const auto settle=[&](double t){
                    if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();perform(lifecycle.didOpen(token),t);}
                    if(lifecycleClosing&&session->phase()==core::VisibilityPhase::concealed){const auto token=*lifecycleClosing;lifecycleClosing.reset();quitConfirmation->hide(t);concealed(t);perform(lifecycle.didClose(token),t);}
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
                t=run(t,1.5);lifecycleCheck(stopping&&lifecycle.phase()==core::SystemOverlayPhase::closed&&!quitConfirmation->presented(),"The ordered drain runs after concealment, then the owner stops");
                {auto work=takeRestartWork();restartCheck(restartAccepted==false&&work&&!takeRestartWork(),"The accepted restart's work is handed out exactly once");
                 work();restartCheck(*restartRuns==1,"The handed-out work is the requested one");}
                }
            }
        }
        need(!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Hidden benchmark window became visible");need(host.stats().frames==0&&!host.stats().timerArmed,"Hidden benchmark scheduled native frame work");
        Json::Array unsupported,substitutions;for(const auto&v:layers.report().unsupported)unsupported.push_back(Json::Object{{"node",v.node},{"feature",v.feature}});for(const auto&v:layers.report().fontSubstitutions)substitutions.push_back(Json::Object{{"node",v.node},{"requested",v.requestedFamily},{"selected",v.selectedFamily}});
        Json report=Json::Object{{"scope","Synthetic source-shell CPU preparation and GPU submission, not GPU duration/FPS or whole-app usage"},{"visible",false},{"desktopCaptured",false},{"userDataRead",false},{"driver",args.warp?"WARP":"hardware"},{"runtimeInput",args.runtimeInput},{"benchmarkEpoch",args.benchmarkEpoch},{"pixelWidth",std::int64_t(metrics.pixelWidth)},{"pixelHeight",std::int64_t(metrics.pixelHeight)},{"logicalWidth",metrics.width},{"logicalHeight",metrics.height},{"scale",metrics.scale},{"coverageOpeningSamples",int(coverageOpening)},{"coverageHoveredPressedHitPoints",int(coverageButtons)},{"coverageResults",coverageResults},{"moduleResults",moduleResults},{"preparationMilliseconds",preparedMS},{"device",Json::Object{{"name",deviceInfo.name},{"vendorID",std::int64_t(deviceInfo.vendorID)},{"deviceID",std::int64_t(deviceInfo.deviceID)},{"dedicatedVideoCapacityBytes",std::int64_t(deviceInfo.dedicatedVideoBytes)},{"sharedSystemCapacityBytes",std::int64_t(deviceInfo.sharedSystemBytes)}}},{"processMemoryScope","This test process including typed source models, D3D driver and benchmark report data; temporary source/setup JSON and any development Package released before sampling"},{"startupStages",startup.rows()},{"samples",rows},{"ownedTargetSnapshots",images},{"snapshotDirectory",args.snapshots.empty()?std::string{}:utf8(args.snapshots)},{"scrollBefore",scrollBefore},{"scrollAfter",scrollAfter},{"scrollDirection",1},{"nativeCanvasFade","Original .20/.24 opening and .35/.06 closing with(.20,.72,.22,1); source clip completion owns concealment"},{"chromeIncluded",bool(chrome)},{"customCursorLoaded",cursor.handle()!=nullptr},{"unsupportedNativeLayers",unsupported},{"fontSubstitutions",substitutions},{"nativeContentVariants",std::int64_t(contentCatalog->variantCount())},{"nativeContentUpdates",std::int64_t(nativeContent?nativeContent->stats().updates:nativeAppearance->stats().updates)},{"nativeContentSurfaceUpdates",std::int64_t(nativeContent?nativeContent->stats().surfaceUpdates:nativeAppearance->stats().surfaceUpdates)},{"nativeTimerArmed",host.stats().timerArmed},{"nativeFrameCallbacks",std::int64_t(host.stats().frames)},
            {"systemBackdropIncluded",false},{"productionLifecycleChecks",int(lifecycleChecks)},{"hostIntegrationChecks",int(hostChecks)},
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
