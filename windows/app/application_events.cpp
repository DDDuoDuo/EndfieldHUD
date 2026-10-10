// Native event routes of the Application owner. Module input follows the
// ModuleRegistry chains (app/module_owner.hpp); OS/service/picker messages keep
// their established WM_APP routes and order.
#include "app/application_impl.hpp"
#include <cstring>
#include <iostream>
#include <wrl/client.h>
#include <wtsapi32.h>

namespace endfield::app {
OverlayCallbacks Application::Impl::callbacks(){
    OverlayCallbacks value;
    value.resize=[this](const ClientMetrics&m){onResize(m);};
    value.pointer=[this](const PointerEvent&e){return onPointer(e);};
    value.wheel=[this](const WheelEvent&e){return onWheel(e);};
    value.beforeKeyTranslation=[this](const NativeMessage&m){return onBeforeKeyTranslation(m);};
    value.appMessage=[this](const NativeMessage&m){return onAppMessage(m);};
    value.key=[this](const KeyEvent&e){return onKey(e);};
    value.focus=[this](bool v){onFocus(v);};
    value.applicationActive=[this](bool v){onApplicationActive(v);};
    value.displayChanged=[this]{onDisplayChanged();};
    value.closeRequested=[this]{onCloseRequested();};
    value.deadline=[this](double t){onDeadline(t);};
    value.frame=[this](double t){onFrame(t);};
    value.sessionEnding=[this](SessionEnd phase,std::uint32_t reason){sessionEnding(phase,reason);};
    return value;
}

void Application::Impl::onResize(const ClientMetrics&value){
    if(projection&&projection->presented()&&projectionPainting){projectionResizePending=value;return;}
    metrics=value;registry.forEach([&](ModuleOwner&o){o.resize(value);},"resize");
    if(!ready)return;const auto time=now();environment.viewport={value.width,value.height};environment.onScreen=value.pixelWidth>0&&value.pixelHeight>0;session->setEnvironment(environment,time);if(environment.onScreen)renderer.resize(value.pixelWidth,value.pixelHeight);if(projection&&projection->presented())projection->resize(value,projectionWorkTopPixels/value.scale);refresh(time);
}

bool Application::Impl::routePointer(ModuleOwner&owner,const PointerPolicy&policy,const PointerEvent&e,double time,core::Point p){
    if(!session->inputEnabled())return false;
    bool handled{};
    if(!registry.guard(owner,[&]{handled=owner.pointer(e,time);},"pointer"))return false;
    if(policy.alwaysUpdateEnvironment||handled){if(policy.updatesPointerLock)environment.pointerLocked=pointerLocked();environment.pointer=p;session->setEnvironment(environment,time);}
    if(handled){
        if(e.kind==PointerKind::move)session->pointerMove(p,time);
        if(!policy.leftButtonCaptureOnly||e.button==PointerButton::left){if(e.kind==PointerKind::down||e.kind==PointerKind::doubleClick)host.capturePointer(true);if(e.kind==PointerKind::up)host.capturePointer(false);}
        refresh(time);
    }
    return handled;
}

bool Application::Impl::onPointer(const PointerEvent&e){
    auto stage=probe.measure(LiveProbe::pointer);if(!ready)return false;const auto time=now();const core::Point p{e.x,e.y};
    if(projection&&projection->presented()){
        if(projectionHandoff.acceptsInput()){projection->pointer(e,time);host.capturePointer(projection->preview()->pointerLocked());refresh(time);}return true;
    }
    using C=ModuleRegistry::Chain;
    // Exclusive captures (modal Settings/quit card, Minigame rules, Calendar
    // and Reader menus, Map drag, Archive selection) run before floating Notes.
    // A guarded failure inside routePointer leaves the chain to continue.
    if(registry.first(C::pointerCapture,[&](ModuleOwner&o,const ModuleRouting&r){return o.capturesPointer()&&routePointer(o,r.pointerPolicy,e,time,p);},"pointer"))return true;
    if(registry.first(C::pointer,[&](ModuleOwner&o,const ModuleRouting&r){return routePointer(o,r.pointerPolicy,e,time,p);},"pointer"))return true;
    if(e.kind==PointerKind::move){environment.pointer=p;session->pointerMove(p,time);}
    else if((e.kind==PointerKind::down||e.kind==PointerKind::doubleClick)&&e.button==PointerButton::left){environment.pointer=p;session->pointerDown(p,time);if(session->navigationPointerActive())host.capturePointer(true);}
    else if(e.kind==PointerKind::up&&e.button==PointerButton::left){environment.pointer=p;if(const auto action=session->pointerUp(p,time))activate(*action);host.capturePointer(false);}
    else if(e.kind==PointerKind::leave){environment.pointer.reset();session->pointerMove({},time);}
    else if(e.kind==PointerKind::captureLost){
        // Normal pointerUp has already cleared its drag/press before our
        // intentional ReleaseCapture sends this notification. Preserve its
        // hover; cancel only unfinished ownership lost to another action.
        if(session->navigationPointerActive()||session->pressed()){
            session->setInputEnabled(false,time);if(focused&&session->phase()==core::VisibilityPhase::visible)session->setInputEnabled(true,time);
        }
    }
    else return false;refresh(time);return true;
}

bool Application::Impl::onWheel(const WheelEvent&e){
    if(!ready)return false;const auto time=now();
    if(projection&&projection->presented()){if(projectionHandoff.acceptsInput()){projection->wheel(e,time);refresh(time);}return true;}
    using C=ModuleRegistry::Chain;
    if(registry.first(C::wheelCapture,[&](ModuleOwner&o,const ModuleRouting&){return o.capturesWheel()&&session->inputEnabled()&&o.wheel(e,time);},"wheel")){refresh(time);return true;}
    if(auto*owner=registry.first(C::wheel,[&](ModuleOwner&o,const ModuleRouting&){return session->inputEnabled()&&o.wheel(e,time);},"wheel")){
        if(owner->routing().wheelRefreshesPointerLock){environment.pointerLocked=pointerLocked();session->setEnvironment(environment,time);}
        refresh(time);return true;
    }
    if(e.horizontal)return false;const bool handled=session->wheel({e.x,e.y},e.steps,e.linesPerStep,time);if(handled)refresh(time);return handled;
}

bool Application::Impl::onBeforeKeyTranslation(const NativeMessage&m){
    if(!ready||(projection&&projection->presented()))return false;
    if(registry.any([](ModuleOwner&o){return o.suppressesKeyFilters();},"key filter"))return false;
    const auto target=static_cast<HWND>(m.window),owner=static_cast<HWND>(host.hwnd());
    const bool ownerTarget=target==owner||IsChild(owner,target);bool stop=false;
    return registry.first(ModuleRegistry::Chain::filterKey,[&](ModuleOwner&o,const ModuleRouting&r){
        if(stop)return false;if(r.filterRequiresOwnerTarget&&!ownerTarget){stop=true;return false;}return o.filterKey(m);},"key filter")!=nullptr;
}

bool Application::Impl::onKey(const KeyEvent&e){
    auto stage=probe.measure(LiveProbe::key);
    if(ready&&projection&&projection->presented()){if(projectionHandoff.acceptsInput()){projection->key(e,now());refresh(now());}return true;}
    if(ready){
        using C=ModuleRegistry::Chain;
        if(registry.first(C::keyCapture,[&](ModuleOwner&o,const ModuleRouting&){return session->inputEnabled()&&o.capturesKeys()&&o.key(e,now());},"key")){refresh(now());return true;}
        if(registry.first(C::key,[&](ModuleOwner&o,const ModuleRouting&){return session->inputEnabled()&&o.key(e,now());},"key")){refresh(now());return true;}
    }
    if(ready&&e.kind==KeyKind::down&&e.value==VK_ESCAPE){if(!production())std::cout<<"Preview unhandled Escape"<<std::endl;perform(lifecycle.close(),now());return true;}
    return false;
}

void Application::Impl::onFocus(bool value){
    if(!production())std::cout<<"Preview focus: "<<value<<std::endl;focused=value;
    if(projection&&projection->presented()){if(!value)projection->cancelInteraction(now());if(ready)refresh(now());return;}
    registry.forEach([&](ModuleOwner&o){o.focus(value,now());},"focus");
    if(ready){const auto time=now();if(!focused){environment.pointer.reset();session->pointerMove({},time);session->setInputEnabled(false,time);}else if(session->phase()==core::VisibilityPhase::visible)session->setInputEnabled(true,time);refresh(time);}
}

void Application::Impl::onApplicationActive(bool active){
    if(ready&&active&&calendar)isolate(calendarModuleOwner,"system change",[&]{calendar->systemChanged(now());updateCalendarWake();});
    if(ready&&active)refreshExternalStatus(); // AppDelegate refreshes login status on activation
    if(!ready||!visibleMode()||active||projectionHandoff.active()||closing||session->phase()==core::VisibilityPhase::concealed||!configuration.boolean("closeOnFocusLost"))return;
    // Source exempts its own file panels and active shelf drag/drop. Moving
    // keyboard focus between owned windows is not an application switch.
    const auto picker=mediaPicker?mediaPicker->stats():gpu::ShelfPickerStats{};
    if(picker.queued||picker.presenting||(shelf&&shelf->preservesFocusOnLoss()))return;
    perform(lifecycle.close(),now());
}

void Application::Impl::onDisplayChanged(){
    if(ready&&production())recordDisplays(); // AppDelegate: screen parameter change refreshes displays
    if(!ready||!projectionHandoff.active()||projectionHandoff.phase()!=ProjectionHandoff::Phase::projection)return;
    // Compositor calls may dispatch this notification. Retain only the
    // latest topology signal, then resize/release after the current paint.
    projectionDisplayPending=true;refresh(now());
}

void Application::Impl::onCloseRequested(){
    if(!production())std::cout<<"Preview native close request"<<std::endl;if(!ready)return;
    if(projectionHandoff.active()){projectionTrayAction=gpu::TrayAction::openOverlay;refresh(now());}else perform(lifecycle.close(),now());
}

void Application::Impl::onDeadline(double time){
    if(!ready||stopping)return;bool artwork=projection&&projection->dismissalComplete(time);
    if(calendarNextWake&&*calendarNextWake<=time){isolate(calendarModuleOwner,"midnight",[&]{calendar->systemChanged(time);updateCalendarWake();});artwork=true;}
    registry.forEach([&](ModuleOwner&o){artwork=o.deadline(time)||artwork;if(o.routing().deadlineRefreshesPointerLock){environment.pointerLocked=pointerLocked();session->setEnvironment(environment,time);}},"deadline");
    if(mediaBroker){const auto wake=mediaBroker->nextWakeTime();const bool due=wake&&*wake<=time;if(session->phase()==core::VisibilityPhase::concealed){mediaBroker->sample(time);if(notes)notes->refreshSharedMedia(time);if(archiveMedia)archiveMedia->refresh(time);if(projection&&projection->presented()){projection->mediaChanged(time);artwork=artwork||due;}}else artwork=artwork||due;}
    if(headerClock.wake(time)||workMode)artwork=updateClock()||artwork;
    if(workMode&&production())updateTrayMenu();
    if(eventRecorder&&workMode)eventRecorder->receiveWork(workMode->controller().snapshot(time));
    if(eventSaves)eventSaves->capture(time);if(activityProbe)activityProbe->update(time);
    perform(lifecycle.advance(time),time);
    scheduleDeadline();if(artwork)refresh(time);
}

void Application::Impl::onFrame(double time){
    if(!ready)return;
    if(probe.enabled&&!diagnosticFinished){
        if(diagnosticStart==0)diagnosticStart=time;const auto elapsed=time-diagnosticStart;
        const int phase=elapsed<3?0:elapsed<8?1:elapsed<13?2:3;
        if(phase!=probe.phase)probe.next(phase,time);
        if(phase>=1&&!diagnosticEditing&&phase<3){auto stage=probe.measure(LiveProbe::editTransition);diagnosticEditing=notes->diagnosticEditing(true,time);}
        if(phase==2){environment.pointer=core::Point{metrics.width*.5+200*std::sin(elapsed*2),metrics.height*.5+120*std::cos(elapsed*2)};session->pointerMove(environment.pointer,time);}
        if(phase==3&&diagnosticEditing){auto stage=probe.measure(LiveProbe::editTransition);if(notes->diagnosticEditing(false,time))diagnosticEditing=false;}
        if(elapsed>=16){probe.flush(time);probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);diagnosticFinished=true;perform(lifecycle.close(),time);}
    }
    const bool active=present(time,true);if(stopping)return;
    if(projection&&projection->presented()){
        if(projection->dismissalComplete(time))if(auto command=projectionHandoff.projectionClosed(projectionHandoff.generation()))projectionCommand(*command,time);
        if(!projectionActions.empty()||projectionTrayAction||projectionDisplayPending||projectionResizePending)refresh(now());
        host.setFrameDemand(demand(now()));scheduleDeadline();return;
    }
    host.setFrameDemand(demand(time));scheduleDeadline();
    // OverlayController.finishSystemOpening: the source entrance is complete
    // once the session is visible; a focus loss queued during opening closes now.
    if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();perform(lifecycle.didOpen(token),time);if(stopping)return;}
    if(!active&&session->phase()==core::VisibilityPhase::concealed){
        if(notes)notes->setMediaActive(false,time);host.hide();if(quitConfirmation)quitConfirmation->hide(time);
        core::SystemOverlayEffects closed;
        if(lifecycleClosing){const auto token=*lifecycleClosing;lifecycleClosing.reset();closed=lifecycle.didClose(token);}
        if(projectionHandoff.phase()==ProjectionHandoff::Phase::closingHUD){
            const auto display=projectionDisplay(hudDisplay);if(display)moveProjection(*display);
            if(auto command=projectionHandoff.hudClosed(projectionHandoff.generation(),display.has_value(),display?std::optional(display->handle):std::nullopt))projectionCommand(*command,now());return;
        }
        if(!closed.empty()){perform(closed,time);if(stopping)return;}
        // OverlayController.tearDownSystemPresentation: a Shelf "Reveal" waits
        // for the normal close to finish; a quit or forced close drops it.
        if(pendingShelfReveal&&visibleMode()&&!quitRequested){
            auto access=std::move(*pendingShelfReveal);pendingShelfReveal.reset();
            if(FAILED(gpu::revealShelfReference(std::move(access)))&&shelf)
                isolate(shelfModuleOwner,"reveal",[&]{shelf->showError(core::localized("Unable to show the file in File Explorer","无法在文件资源管理器中显示文件",watchAppearance.language),time);});
        }
        if(!production()&&!tray&&!quitRequested)finishQuit(time);else if(!stopping)scheduleDeadline();
    }
}

std::optional<std::intptr_t> Application::Impl::onAppMessage(const NativeMessage&m){
    auto stage=probe.measure(LiveProbe::message);
    if(!stopping&&systemServices)systemServices->handle_message(m.message,m.wParam,m.lParam);
    if(ready&&tray&&tray->message(m.message,m.wParam,m.lParam)){
        if(const auto action=tray->takeAction())trayAction(*action,m.message==WM_HOTKEY,now());
        return 0;
    }
    if(ready&&m.wParam==serviceGeneration){
        if(m.message==utilityMessage){if(utility)utility->drain();if(activityProbe)activityProbe->submitPending();if(systemServices)systemServices->clipboard_queue_capacity_available();if(storage)isolate(storageModuleOwner,"utility completion",[&]{storage->utilityCompleted(storageFixture?storageFixture->time.load(std::memory_order_relaxed):now());});if(archiveService)archiveService->queueCapacityAvailable();if(readerOwner)readerOwner->queueCapacityAvailable();if(calendarState){calendarState->queueCapacityAvailable();calendarNotifications->queueCapacityAvailable();}if(map)isolate(mapModuleOwner,"utility completion",[&]{if(notes&&notes->selected()==core::Module::map)requestMapGeography();map->utilityCompleted(visibleMode()?now():mapTime);});if(settingsSaves){settingsSaves->queueCapacityAvailable();if(settingsSaves->status().error&&settingsUI)settingsUI->controller().setStatus(*settingsSaves->status().error);}if(eventSaves)eventSaves->retry();refresh(now());return 0;}
        if(m.message==projectionDropMessage){
            auto paths=std::exchange(projectionDrop->paths,{});const auto time=now();
            if(!paths.empty()&&projection&&projection->presented()&&projectionHandoff.acceptsInput())if(const auto token=projection->preview()->prepareImportRequest()){
                try{std::vector<tools::ProjectionImportFile>files;files.reserve(paths.size());for(auto&path:paths){auto title=utf8(utf8Path(path).filename());files.push_back({std::move(path),std::move(title),{}});}
                    projection->media()->beginImport(*token,files,projectionDrop->point,time);
                }catch(const std::exception&){projection->preview()->receiveImportError(*token,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}
            }
            refresh(time);return 0;
        }
        if(m.message==projectionActionMessage){
            projectionActionsQueued=false;const auto time=now();if(projectionPainting)return 0;
            if(auto trayRequest=std::exchange(projectionTrayAction,{})){
                if(*trayRequest==gpu::TrayAction::quit){projectionActions.clear();cancelProjectionPicker();if(auto command=projectionHandoff.cancel())projectionCommand(*command,time);perform(lifecycle.requestTermination(),time);return 0;}
                if(projectionHandoff.active()){
                    if(auto command=projectionHandoff.external(static_cast<std::uint64_t>(*trayRequest)))projectionCommand(*command,time);
                }
            }
            if(auto resized=std::exchange(projectionResizePending,{}))onResize(*resized);
            if(std::exchange(projectionDisplayPending,false)&&projectionHandoff.phase()==ProjectionHandoff::Phase::projection){
                if(const auto display=projectionDisplay(hudDisplay)){moveProjection(*display);projectionHandoff.reposition(display->handle);}
                else{if(auto command=projectionHandoff.cancel())projectionCommand(*command,time);host.hide();host.setFrameDemand({});}
            }
            while(!projectionActions.empty()){
                auto action=std::move(projectionActions.front());projectionActions.pop_front();
                if(!projection||!projection->presented()||!projectionHandoff.acceptsInput())continue;
                auto*view=projection->preview();using K=tools::ProjectionPreviewAction::Kind;
                if(action.kind==K::returnToHUD){if(auto command=projectionHandoff.returnToHUD())projectionCommand(*command,time);continue;}
                if(action.kind==K::togglePlayback||action.kind==K::seek){projection->media()->action(action,time);continue;}
                if(!view->importRequestCurrent(action.generation))continue;
                try{
                    if(action.kind==K::chooseLocal){
                        if(requestMediaPicker()){pickerOwner=PickerOwner::projection;pendingProjectionPicker=action;host.capturePointer(false);}
                        else view->receiveImportError(action.generation,core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);
                    }else if(action.kind==K::useShelf&&shelf){
                        auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action.shelfID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf Projection reference is unavailable");
                        const std::array files{tools::ProjectionImportFile{lease->metadata().windowsPath,lease->metadata().name,lease}};
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
                if(action->kind==tools::ReaderImportAction::Kind::chooseLocal){
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
        if(m.message==clipboardChangedMessage){clipboardRefreshQueued=false;if(clipboardDirty&&session->phase()!=core::VisibilityPhase::concealed&&notes&&notes->selected()==core::Module::clipboard)refresh(now());return 0;}
        if(m.message==eventChangedMessage){eventRefreshQueued=false;if(eventLog)eventLog->refresh();refresh(now());return 0;}
        if(m.message==archiveChangedMessage){archiveRefreshQueued=false;refresh(now());return 0;}
        if(m.message==sharedMediaMessage&&mediaBroker){const auto time=now();mediaBroker->accept(m.wParam,time);if(notes)notes->refreshSharedMedia(time);if(archiveMedia)archiveMedia->refresh(time);if(projection&&projection->presented())projection->mediaChanged(time);refresh(time);return 0;}
        if(m.message==mediaPickerMessage&&mediaPicker){
            host.suspendCursor(true);mediaPicker->handleMessage(m.wParam,m.lParam);host.suspendCursor(false);
            if(auto result=mediaPicker->drain(m.wParam)){
                const auto owner=std::exchange(pickerOwner,PickerOwner::none);auto action=std::move(pendingArchivePicker);pendingArchivePicker.reset();auto readerAction=std::move(pendingReaderPicker);pendingReaderPicker.reset();auto projectionAction=std::exchange(pendingProjectionPicker,{});const auto time=now();
                if(owner==PickerOwner::projection&&projectionAction&&projection&&projection->presented()&&projection->preview()->importRequestCurrent(projectionAction->generation)){
                    auto*view=projection->preview();if(result->canceled())view->receiveImportError(projectionAction->generation,{},time);
                    else if(SUCCEEDED(result->result)){try{std::vector<tools::ProjectionImportFile>files;files.reserve(result->paths.size());for(const auto&path:result->paths)files.push_back({path,utf8(utf8Path(path).filename()),{}});projection->media()->beginImport(projectionAction->generation,files,{},time);}catch(const std::exception&){view->receiveImportError(projectionAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);}}
                    else view->receiveImportError(projectionAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                }else if(owner==PickerOwner::reader&&readerAction&&reader&&reader->importRequestCurrent(readerAction->generation)){
                    if(result->canceled())reader->receiveImportError(readerAction->generation,{},time);
                    else if(SUCCEEDED(result->result)&&result->paths.size()==1)importReaderReference(readerAction->generation,result->paths.front(),{},time);
                    else reader->receiveImportError(readerAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                }else if(!result->canceled()&&owner==PickerOwner::notes&&notes&&!closing&&notes->selected()==core::Module::notes){
                    if(SUCCEEDED(result->result))notes->importMedia(result->paths,mediaInsertionPoint,time);else notes->showMediaError(core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                }else if(!result->canceled()&&owner==PickerOwner::archive&&action&&archive&&archiveMedia&&archive->mediaRequestCurrent(action->documentID,action->generation)){
                    if(SUCCEEDED(result->result)){try{std::vector<tools::ArchiveMediaImportFile>files;files.reserve(result->paths.size());for(const auto&path:result->paths)files.push_back({path,utf8(utf8Path(path).filename()),{}});archiveMedia->beginImport(*action,files,time);}catch(const std::exception&e){archive->receiveMedia(action->documentID,action->generation,{},e.what(),time);}}
                    else archive->receiveMedia(action->documentID,action->generation,{},core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),time);
                }
            }refresh(now());return 0;
        }
    }
    if(ready&&production()&&m.message==WM_POWERBROADCAST){
        if(m.wParam==PBT_APMSUSPEND){suspend(Suspension::system);return TRUE;}
        if(m.wParam==PBT_APMRESUMEAUTOMATIC||m.wParam==PBT_APMRESUMESUSPEND)resume(Suspension::system);
        if(m.wParam==PBT_POWERSETTINGCHANGE&&m.lParam){
            const auto*setting=reinterpret_cast<const POWERBROADCAST_SETTING*>(m.lParam);
            if(setting->PowerSetting==consoleDisplayState&&setting->DataLength>=sizeof(DWORD)){
                DWORD state{};std::memcpy(&state,setting->Data,sizeof(state));
                if(state==0)suspend(Suspension::display);else if(state==1)resume(Suspension::display); // dimmed (2) keeps the HUD
            }
        }
    }
    if(ready&&production()&&m.message==WM_WTSSESSION_CHANGE){
        if(m.wParam==WTS_SESSION_LOCK||m.wParam==WTS_CONSOLE_DISCONNECT||m.wParam==WTS_REMOTE_DISCONNECT)suspend(Suspension::session);
        else if(m.wParam==WTS_SESSION_UNLOCK||m.wParam==WTS_CONSOLE_CONNECT||m.wParam==WTS_REMOTE_CONNECT)resume(Suspension::session);
        return 0;
    }
    if(ready&&(m.message==WM_TIMECHANGE||m.message==WM_SETTINGCHANGE||(m.message==WM_POWERBROADCAST&&(m.wParam==PBT_APMRESUMEAUTOMATIC||m.wParam==PBT_APMRESUMESUSPEND)))){const auto time=now();if(m.message==WM_TIMECHANGE&&archiveDates)archiveDates->refreshSystemTimeZone();if(calendar)isolate(calendarModuleOwner,"system change",[&]{calendar->systemChanged(time);updateCalendarWake();});if(m.message==WM_SETTINGCHANGE&&configuration.string("theme")=="system")configurationPending=true;refresh(time);return 0;}
    if(ready&&archive&&m.message==WM_APP+221&&m.wParam==archive->mediaRouteGeneration()){
        if(auto action=archive->takeMediaAction())try{using K=tools::ArchiveMediaAction::Kind;const auto time=now();
            if(action->kind==K::chooseLocal){if(!requestMediaPicker())archive->receiveMedia(action->documentID,action->generation,{},core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);else{pickerOwner=PickerOwner::archive;pendingArchivePicker=*action;host.capturePointer(false);}}
            else if(action->kind==K::chooseShelf)archive->presentShelfMedia(shelfChoices(),time);
            else if(action->kind==K::useShelf&&shelf){auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action->itemID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf media reference is unavailable");const std::array files{tools::ArchiveMediaImportFile{lease->metadata().windowsPath,lease->metadata().name,lease}};archiveMedia->beginImport(*action,files,time);}
            else if(archiveMedia)archiveMedia->action(*action,time);
        }catch(const std::exception&e){if(archiveService&&archiveService->state().selected())archive->setMediaPlayback(archive->mediaIndex(),false,0,e.what(),now());}
        refresh(now());return 0;
    }
    if(ready&&notes&&m.message==tools::NotesPreview::mediaActionMessage){
        if(auto action=notes->takeMediaAction(m.wParam))try{using K=tools::NotesMediaAction::Kind;
            if(action->kind==K::chooseLocal){mediaInsertionPoint=action->workspacePoint;if(!requestMediaPicker())notes->showMediaError(core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),now());else{pickerOwner=PickerOwner::notes;host.capturePointer(false);}}
            else if(action->kind==K::chooseShelf){
                notes->presentShelfMedia(shelfChoices(),action->workspacePoint,now());
            }else if(shelf)notes->importMedia(shelf->access(action->itemID),action->workspacePoint,now());
        }catch(const std::exception&e){notes->showMediaError(e.what(),now());}
        refresh(now());return 0;
    }
    if(ready){
        if(auto*owner=registry.first(ModuleRegistry::Chain::message,[&](ModuleOwner&o,const ModuleRouting&){return o.message(m,now());},"message")){
            const auto routing=owner->routing();
            if(routing.messageRetriesPendingModule&&pendingModule)selectModule(*pendingModule,now());
            if(routing.messageRetriesPendingClose&&pendingClose)close(now());
            refresh(now());return 0;
        }
    }
    if(ready&&shelf&&m.message==tools::ShelfPreview::actionMessage){
        if(auto action=shelf->takeAction())try{using K=tools::ShelfPreviewAction::Kind;
            switch(action->kind){
            case K::choose:host.capturePointer(false);shelf->requestFiles();break;
            case K::paste:{Microsoft::WRL::ComPtr<IDataObject>source;need(SUCCEEDED(OleGetClipboard(&source)),"Clipboard is unavailable");const auto paths=gpu::readShelfTransferPaths(*source.Get());source.Reset();shelf->importFiles(paths,now());break;}
            case K::reveal:pendingShelfReveal=shelf->access(action->itemID);perform(lifecycle.close(),now());break;
            case K::preview:host.capturePointer(false);shelf->requestPreview(action->itemID);break;
            case K::drag:{auto transfer=shelf->prepareDrag(action->itemID);shelf->setNativeDragActive(true);host.capturePointer(false);environment.pointerLocked=false;session->setEnvironment(environment,now());DWORD effect{};const auto result=transfer->run(&effect);shelf->setNativeDragActive(false);if(result==DRAGDROP_S_DROP&&(effect&DROPEFFECT_COPY))perform(lifecycle.close(),now());break;}
            }
        }catch(const std::exception&e){shelf->setNativeDragActive(false);shelf->showError(e.what(),now());}
        refresh(now());return 0;
    }
    return {};
}
} // namespace endfield::app

namespace endfield::app {
void Application::Impl::suspend(Suspension reason){
    const bool first=suspensions==0;suspensions|=static_cast<unsigned>(reason);
    lifecycle.setSuspended(true);if(!first)return;
    const auto time=now();
    (void)lifecycle.forceClose();forceConceal(time);
    if(workMode)workMode->setSuspended(true,time);
}
void Application::Impl::resume(Suspension reason){
    // Ignore duplicate or unmatched wake notifications.
    if(!(suspensions&static_cast<unsigned>(reason)))return;
    suspensions&=~static_cast<unsigned>(reason);if(suspensions)return;
    lifecycle.setSuspended(false);
    if(workMode)workMode->setSuspended(false,now());
    if(systemServices)systemServices->refresh_battery();recordBattery();updateTrayMenu();
}
} // namespace endfield::app
