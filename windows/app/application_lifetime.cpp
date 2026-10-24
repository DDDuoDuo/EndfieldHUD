// Visible run loop, ordered shutdown and teardown of the Application owner.
#include "app/application_impl.hpp"
#include <cstdio>
#include <iostream>
#include <wtsapi32.h>
#include <uiautomation.h>

namespace endfield::app {
void Application::Impl::cleanup(){if(cleaned)return;cleaned=true;ready=false;stopping=true;tray.reset();
    if(displayNotification){UnregisterPowerSettingNotification(static_cast<HPOWERNOTIFY>(displayNotification));displayNotification=nullptr;}
    if(sessionNotification&&host.hwnd()){WTSUnRegisterSessionNotification(static_cast<HWND>(host.hwnd()));sessionNotification=false;}
    if(mediaPicker)mediaPicker->cancel();mediaPicker.reset();
    // The HUD drop route and the UI Automation tree stop before any owner
    // goes: clients holding fragments get UIA_E_ELEMENTNOTAVAILABLE.
    hudDrop->owner=nullptr;hudDrop->paths.clear();hudDropActive=false;dropHoverOwner=nullptr;
    if(automation){automation->shutdown();automation.reset();UiaDisconnectAllProviders();}
    projectionHandoff.cancel();projectionDrop->enabled=false;if(shelf)shelf->setExternalDropCallbacks({},now());releaseProjection();
    if(projectionIsolation){registry.remove(*projectionIsolation);projectionIsolation.reset();}
    try{composition.detach(renderer);}catch(...){renderer.reset();}
    // GPU release in reverse registration order: Archive/Calendar/Work Mode
    // fields borrow Notes' activated TSF manager, so Notes releases last.
    if(renderer.stats().initialized)registry.releaseAll(renderer);
    if(moduleContrast&&renderer.stats().initialized){try{(void)moduleContrast->scene.releaseResources(renderer);}catch(...){}}moduleContrast.reset();
    // Owners are destroyed in the established order after every GPU release.
    const auto drop=[&](std::unique_ptr<ModuleOwner>&adapter){unregisterModule(adapter);};
    if(clockAutomation){registry.remove(*clockAutomation);clockAutomation.reset();}
    if(navigationAutomation){registry.remove(*navigationAutomation);navigationAutomation.reset();}
    // Added module owners may borrow Notes' TSF manager: destroy them first,
    // after withdrawing every area service and any pending area dialog.
    // The ID card presenter borrows the label scene and SourceScene and is
    // connected to the profile area's feed: disconnect before the area goes.
    if(idCard){idCard->disconnect();idCard.reset();}
    areaPicker={};if(areaHost)areaHost->clearServices();
    facilityOwner=nullptr;
    for(auto it=externalModules.rbegin();it!=externalModules.rend();++it){registry.remove(*it->owner);it->owner.reset();}externalModules.clear();
    // Area exports finished or were cancelled with their owners; the second
    // executor joins before the shared one (its completions post the same notice).
    if(longWork){longWork->waitIdle();longWork->drain();longWork.reset();}
    archive=nullptr;drop(archiveModuleOwner);
    calendar=nullptr;drop(calendarModuleOwner);calendarState.reset();calendarNotifications.reset();if(utility&&calendarRoute)utility->invalidate(calendarRoute,false);calendarRepository.reset();calendarCivil.reset();calendarFixture.reset();
    drop(settingsModuleOwner);settingsUI.reset();drop(batteryModuleOwner);battery=nullptr;workModeCanvas=nullptr;drop(workModeModuleOwner);workMode.reset();drop(notesCentreModuleOwner);drop(notesModuleOwner);notes.reset();
    activityTelemetry.reset();activityPlan.stop();activityProbe.reset();
    drop(shelfModuleOwner);shelf.reset();clipboardCanvas=nullptr;drop(clipboardModuleOwner);volumeCanvas=nullptr;drop(volumeModuleOwner);if(utility&&volumeIconRoute)utility->invalidate(volumeIconRoute);volumeIcons.reset();eventLogCanvas=nullptr;drop(eventLogModuleOwner);drop(storageModuleOwner);storage.reset();drop(activityModuleOwner);activity.reset();
    reader=nullptr;drop(readerModuleOwner);readerOwner.reset();game=nullptr;drop(gameModuleOwner);gameSession.reset();map=nullptr;drop(mapModuleOwner);mapStore.reset();if(utility&&mapLoadRoute)utility->invalidate(mapLoadRoute);
    // The shared design Context outlives every design owner (they borrow it).
    if(designOverlay){registry.remove(*designOverlay);designOverlay.reset();}designClipboard.reset();
    // Retire state routes before the shared file executor; accepted immutable
    // writes finish on its existing shutdown barrier, with callbacks dead.
    // The audio worker restores owned per-app levels and joins before the HWND
    // that receives its notice is destroyed.
    if(audio)audio->stop();audio.reset();
    if(clipboardProvider)clipboardProvider->close();clipboardProvider.reset();
    if(systemServices)systemServices->stop();systemServices.reset();
    archiveService.reset();settingsSaves.reset();eventSaves.reset();utility.reset();activityCatalog.reset();activityDisk.reset();
    if(mediaBroker){try{mediaBroker->collectRetired();if(archiveMediaClient)mediaBroker->detachClient(archiveMediaClient);if(notesMediaClient)mediaBroker->detachClient(notesMediaClient);}catch(...){}mediaBroker.reset();}
    mediaVideos.reset();mediaImages.reset();if(mediaDecoder)mediaDecoder->stop();mediaDecoder.reset();
    backdrop.reset();renderer.reset();try{host.setCursor(nullptr);host.destroy();}catch(...){}
}

int Application::Impl::runVisible(){
    // Initialization must not consume the opening timeline while the HWND is
    // still hidden. Hotkey openings already use their actual event timestamp.
    const auto openingTime=now();
    if(production()){
        // First run waits for the preference record and its launch marker.
        launchPending=true;
        if(settingsLoaded){launchPending=false;perform(lifecycle.launch(args.startup,settingsSaves&&settingsSaves->status().launched,!args.persistLaunchMarker),openingTime);reopenAfterLaunch(openingTime);}
        else if(!settingsSaves){launchPending=false;perform(lifecycle.launch(args.startup,false,true),openingTime);reopenAfterLaunch(openingTime);}
    }else perform(lifecycle.toggle(),openingTime); // An explicit development launch opens at once.
    scheduleDeadline();
    host.run();
    shutdownVisible();
    return 0;
}

// A fresh run after GPU device loss reopens the module the HUD showed.
void Application::Impl::reopenAfterLaunch(double time){
    const auto module=std::exchange(args.reopenModule,std::nullopt);
    if(!module||!visibleMode())return;
    perform(lifecycle.openSettingsModule(*module),time);
    if(session->phase()!=core::VisibilityPhase::concealed&&!closing&&!stopping){host.show();refresh(time);}
}
// ENDFIELDHUD_STARTUP_TRACE / --startup-trace: written once, when both traced
// openings completed or at shutdown with what was recorded (numbers only).
void Application::Impl::writeStartupTrace(){
    if(!startupTrace||startupTraceWritten||args.startupTrace.empty())return;startupTraceWritten=true;
    try{
        std::error_code ignored;fs::create_directories(args.startupTrace.parent_path(),ignored);
        constexpr std::size_t bound=4*1024*1024;
        auto report=startupTrace->report(startup.rows(),preparedMS);
        report["embeddedShaders"]=renderer.stats().embeddedShaders;report["hiddenSyntheticClock"]=!visibleMode();
        if(args.liveDiagnostics.empty())report["openingStages"]=probe.rows; // per traced opening: frame stage calls, totals and maxima (ms)
        ehud::data::detail::replaceFile(args.startupTrace,ehud::data::detail::readFile(args.startupTrace,bound),report.encode(),bound);
    }catch(const std::exception&e){
        if(!production())throw;
        const auto line=std::string("EndfieldHUD startup trace could not be written (")+typeid(e).name()+")\n";std::fputs(line.c_str(),stderr);OutputDebugStringA(line.c_str());
    }
}
void Application::Impl::shutdownVisible(){
    writeStartupTrace();
    if(!args.liveDiagnostics.empty()){
        if(probe.enabled){probe.flush(now());probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);}
        Json report=Json::Object{{"scope","Isolated synthetic visible HUD, actual text service, inclusive CPU stage durations; no document text or desktop capture"},{"phases","0 opening/idle; 1 focused editor; 2 focused editor with controlled tilt; 3 after editing"},{"samples",probe.rows}};
        ehud::data::detail::replaceFile(args.liveDiagnostics,std::nullopt,report.encode(),1024*1024);
    }
    ready=false;stopping=true;tray.reset();if(workMode)isolate(workModeModuleOwner,"shutdown",[&]{workMode->shutdown(now());});host.requestStop();if(mediaPicker)mediaPicker->cancel();
    if(!sessionEnded){
        if(archive){bool finished=true;isolate(archiveModuleOwner,"shutdown",[&]{finished=archive->finishEditing(now());});need(finished,"Archive text transaction is still active at shutdown");need(archiveService->flush(),"Archive changes could not be saved; draft retained until explicit shutdown failure");}
        if(calendar){bool dismissed=true;isolate(calendarModuleOwner,"shutdown",[&]{dismissed=calendarModuleOwner->finishEditing(now());});need(dismissed,"Calendar text transaction is still active at shutdown");}need(flushCalendar(),"Calendar changes could not be saved");
        if(readerOwner)need(readerOwner->flush(),"Reader changes could not be saved");
        if(settingsSaves)need(settingsSaves->flush(),"Settings changes could not be saved");if(eventSaves)eventSaves->flush(now());
    }
    projectionHandoff.cancel();projectionDropRoute(false,now());releaseProjection();backdrop.reset();
    if(visibleMode())need(backdrop.stats().hostAttributeRestored,"Source preview could not restore its original host-backdrop flag");
    if(readerTrace)readerTrace->save(args.dataRoot/"reader-scroll-trace.json");cleanup();if(backdropQueue)backdropQueue->finish();pendingShelfReveal.reset();pendingStorageSettings=false;pendingLink.reset(); // A hand-off still pending at quit is dropped (OverlayController).
}

Application::Application(ApplicationOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
Application::~Application()=default;
int Application::run(){
    if(impl_->args.mode==ApplicationMode::hiddenBenchmark)throw std::logic_error("Hidden benchmark runs through runHiddenVerification");
    return impl_->runVisible();
}
void Application::activate(std::span<const std::string>arguments){impl_->forwardedActivation(arguments);}
std::function<void()> Application::Impl::takeRestartWork(){
    if(!restartAccepted)return {};
    restartAccepted=false;return std::exchange(restartWork,{});
}
std::function<void()> Application::takeRestartWork(){return impl_->takeRestartWork();}
std::optional<std::optional<core::Module>> Application::takeDeviceRecovery(){
    if(!impl_->deviceLost)return std::nullopt;
    impl_->deviceLost=false;return std::optional<std::optional<core::Module>>(std::in_place,std::exchange(impl_->deviceLostModule,std::nullopt));
}
void* Application::window()const noexcept{return impl_->host.hwnd();}
} // namespace endfield::app
