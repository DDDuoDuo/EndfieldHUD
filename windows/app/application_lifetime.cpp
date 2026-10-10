// Visible run loop, ordered shutdown and teardown of the Application owner.
#include "app/application_impl.hpp"
#include <iostream>
#include <wtsapi32.h>

namespace endfield::app {
void Application::Impl::cleanup(){if(cleaned)return;cleaned=true;ready=false;stopping=true;tray.reset();
    if(displayNotification){UnregisterPowerSettingNotification(static_cast<HPOWERNOTIFY>(displayNotification));displayNotification=nullptr;}
    if(sessionNotification&&host.hwnd()){WTSUnRegisterSessionNotification(static_cast<HWND>(host.hwnd()));sessionNotification=false;}
    if(mediaPicker)mediaPicker->cancel();mediaPicker.reset();
    projectionHandoff.cancel();projectionDrop->enabled=false;if(shelf)shelf->setExternalDropCallbacks({},now());projection.reset();
    try{composition.detach(renderer);}catch(...){renderer.reset();}
    // GPU release in reverse registration order: Archive/Calendar/Work Mode
    // fields borrow Notes' activated TSF manager, so Notes releases last.
    if(renderer.stats().initialized)registry.releaseAll(renderer);
    if(archiveMedia){try{archiveMedia->releaseResources(now());}catch(...){}}
    // Owners are destroyed in the established order after every GPU release.
    const auto drop=[&](std::unique_ptr<ModuleOwner>&adapter){unregisterModule(adapter);};
    if(quitConfirmation){registry.remove(*quitConfirmation);quitConfirmation.reset();}
    // Added module owners may borrow Notes' TSF manager: destroy them first.
    for(auto it=externalModules.rbegin();it!=externalModules.rend();++it){registry.remove(**it);it->reset();}externalModules.clear();
    drop(archiveModuleOwner);archiveMedia.reset();archive.reset();
    drop(calendarModuleOwner);calendar.reset();calendarState.reset();calendarNotifications.reset();if(utility&&calendarRoute)utility->invalidate(calendarRoute,false);calendarRepository.reset();calendarCivil.reset();calendarFixture.reset();
    drop(settingsModuleOwner);settingsUI.reset();drop(batteryModuleOwner);battery.reset();drop(workModeModuleOwner);workMode.reset();drop(notesModuleOwner);notes.reset();
    activityPlan.stop();activityProbe.reset();
    drop(shelfModuleOwner);shelf.reset();drop(clipboardModuleOwner);clipboard.reset();drop(volumeModuleOwner);volume.reset();drop(eventLogModuleOwner);eventLog.reset();drop(storageModuleOwner);storage.reset();drop(activityModuleOwner);activity.reset();
    drop(readerModuleOwner);reader.reset();readerOwner.reset();drop(gameModuleOwner);game.reset();gameSession.reset();drop(mapModuleOwner);map.reset();mapStore.reset();if(utility&&mapLoadRoute)utility->invalidate(mapLoadRoute);
    // Retire state routes before the shared file executor; accepted immutable
    // writes finish on its existing shutdown barrier, with callbacks dead.
    if(volumeProvider)volumeProvider->close();volumeProvider.reset();
    if(clipboardProvider)clipboardProvider->close();clipboardProvider.reset();
    if(systemServices)systemServices->stop();systemServices.reset();
    archiveService.reset();settingsSaves.reset();eventSaves.reset();utility.reset();activityCatalog.reset();activityDisk.reset();
    if(mediaBroker){try{mediaBroker->collectRetired();if(archiveMediaClient)mediaBroker->detachClient(archiveMediaClient);if(notesMediaClient)mediaBroker->detachClient(notesMediaClient);}catch(...){}mediaBroker.reset();}
    mediaVideos.reset();mediaImages.reset();if(mediaDecoder)mediaDecoder->stop();mediaDecoder.reset();
    archiveDates.reset();backdrop.reset();renderer.reset();try{host.setCursor(nullptr);host.destroy();}catch(...){}
}

int Application::Impl::runVisible(){
    // Initialization must not consume the opening timeline while the HWND is
    // still hidden. Hotkey openings already use their actual event timestamp.
    const auto openingTime=now();
    if(production()){
        // First run waits for the preference record and its launch marker.
        launchPending=true;
        if(settingsLoaded){launchPending=false;perform(lifecycle.launch(args.startup,settingsSaves&&settingsSaves->status().launched,!args.persistLaunchMarker),openingTime);}
        else if(!settingsSaves){launchPending=false;perform(lifecycle.launch(args.startup,false,true),openingTime);}
    }else perform(lifecycle.toggle(),openingTime); // An explicit development launch opens at once.
    scheduleDeadline();
    host.run();
    shutdownVisible();
    return 0;
}

void Application::Impl::shutdownVisible(){
    if(!args.liveDiagnostics.empty()){
        if(probe.enabled){probe.flush(now());probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);}
        Json report=Json::Object{{"scope","Isolated synthetic visible HUD, actual text service, inclusive CPU stage durations; no document text or desktop capture"},{"phases","0 opening/idle; 1 focused editor; 2 focused editor with controlled tilt; 3 after editing"},{"samples",probe.rows}};
        ehud::data::detail::replaceFile(args.liveDiagnostics,std::nullopt,report.encode(),1024*1024);
    }
    ready=false;stopping=true;tray.reset();if(workMode)workMode->shutdown(now());host.requestStop();if(mediaPicker)mediaPicker->cancel();
    if(!sessionEnded){
        if(archive){need(archive->finishEditing(now()),"Archive text transaction is still active at shutdown");need(archiveService->flush(),"Archive changes could not be saved; draft retained until explicit shutdown failure");}
        if(calendar)need(calendar->dismissMenu(false,now()),"Calendar text transaction is still active at shutdown");need(flushCalendar(),"Calendar changes could not be saved");
        if(readerOwner)need(readerOwner->flush(),"Reader changes could not be saved");
        if(settingsSaves)need(settingsSaves->flush(),"Settings changes could not be saved");if(eventSaves)eventSaves->flush(now());
    }
    projectionHandoff.cancel();projectionDropRoute(false,now());projection.reset();backdrop.reset();
    if(visibleMode())need(backdrop.stats().hostAttributeRestored,"Source preview could not restore its original host-backdrop flag");
    if(readerTrace)readerTrace->save(args.dataRoot/"reader-scroll-trace.json");cleanup();if(backdropQueue)backdropQueue->finish();pendingShelfReveal.reset(); // A reveal still pending at quit is dropped (OverlayController).
}

Application::Application(ApplicationOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
Application::~Application()=default;
int Application::run(){
    if(impl_->args.mode==ApplicationMode::hiddenBenchmark)throw std::logic_error("Hidden benchmark runs through runHiddenVerification");
    return impl_->runVisible();
}
void Application::activate(std::span<const std::string>arguments){impl_->forwardedActivation(arguments);}
void* Application::window()const noexcept{return impl_->host.hwnd();}
} // namespace endfield::app
