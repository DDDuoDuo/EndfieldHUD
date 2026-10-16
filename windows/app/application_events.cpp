// Native event routes of the Application owner. Module input follows the
// ModuleRegistry chains (app/module_owner.hpp); OS/service/picker messages keep
// their established WM_APP routes and order.
#include "app/application_impl.hpp"
#include "app/fatal_stage.hpp"
#include <cmath>
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
    value.getObject=[this](const NativeMessage&m){return automationObject(m);};
    value.cursorAt=[this](double x,double y){return onCursor({x,y});};
    // Every presented frame (the HUD and Projection) waits for the swap
    // chain's admission in the host's pump, with its input, instead of
    // queueing behind or blocking on the previous one (app/overlay_host.hpp).
    value.presentationWaitable=[this]{return renderer.presentationWaitable();};
    value.presentationHeld=[this]{return renderer.presentationHeld();};
    value.presentationAdmitted=[this]{renderer.presentationAdmitted();};
    return value;
}

// SystemHUDView.cursorUpdate -> HUDSourceWatchView.refreshSourceCursor: the
// source cursor everywhere, the I-beam over an open projected text editor
// (HUDProjectedTextView.projectedCursor). Answered from WM_SETCURSOR, so
// nothing here allocates or changes state.
OverlayCursor Application::Impl::onCursor(core::Point p){
    if(!ready||stopping||!session->inputEnabled())return OverlayCursor::source;
    // The Projection workspace shares this window and keeps the source cursor.
    if(projectionShown())return OverlayCursor::source;
    return cursorAt(p)==ModuleCursor::text?OverlayCursor::text:OverlayCursor::source;
}
// The pointer routing order: an exclusive capture (modal card or menu) answers
// for every point; otherwise the first owner of the pointer chain that answers
// or covers the point decides (a pinned Notes card above an editor keeps the
// source cursor).
ModuleCursor Application::Impl::cursorAt(core::Point p){
    using C=ModuleRegistry::Chain;std::optional<ModuleCursor> chosen;
    if(registry.first(C::pointerCapture,[&](ModuleOwner&o,const ModuleRouting&){if(!o.capturesPointer())return false;chosen=o.cursorAt(p);return true;},"cursor"))
        return chosen.value_or(ModuleCursor::source);
    chosen.reset();
    (void)registry.first(C::pointer,[&](ModuleOwner&o,const ModuleRouting&){chosen=o.cursorAt(p);return chosen.has_value()||o.covers(p);},"cursor");
    return chosen.value_or(ModuleCursor::source);
}

void Application::Impl::onResize(const ClientMetrics&value){
    if(projectionShown()&&projectionPainting){projectionResizePending=value;return;}
    metrics=value;updateContentScale();registry.forEach([&](ModuleOwner&o){o.resize(value);},"resize");
    if(!ready||stopping)return;const auto time=now();environment.viewport={value.width,value.height};environment.onScreen=value.pixelWidth>0&&value.pixelHeight>0;session->setEnvironment(environment,time);
    if(environment.onScreen)try{renderer.resize(value.pixelWidth,value.pixelHeight);}catch(const std::exception&e){if(recoverDevice(e,time))return;throw;}
    if(projectionShown())(void)projectionCall("resize",[&]{projection->resize(value,projectionWorkTopPixels/value.scale);});automationChanged();refresh(time);
}

bool Application::Impl::routePointer(ModuleOwner&owner,const PointerPolicy&policy,const PointerEvent&e,double time,core::Point p){
    if(!session->inputEnabled())return false;
    bool handled{};
    if(!registry.guard(owner,[&]{handled=owner.pointer(e,hostTime(time));},"pointer"))return false;
    time=hostTime(time);
    if(policy.alwaysUpdateEnvironment||handled){if(policy.updatesPointerLock)environment.pointerLocked=pointerLocked();environment.pointer=p;session->setEnvironment(environment,time);}
    if(handled){
        automationChanged();
        if(e.kind==PointerKind::move)session->pointerMove(p,time);
        if(!policy.leftButtonCaptureOnly||e.button==PointerButton::left){if(e.kind==PointerKind::down||e.kind==PointerKind::doubleClick)host.capturePointer(true);if(e.kind==PointerKind::up)host.capturePointer(false);}
        refresh(time);
        // A press may open or close a projected editor under a still pointer.
        if(e.kind!=PointerKind::move)host.refreshCursor();
    }
    return handled;
}

bool Application::Impl::onPointer(const PointerEvent&e){
    auto stage=probe.measure(LiveProbe::pointer);if(!ready)return false;const auto time=now();const core::Point p{e.x,e.y};
    if(projectionShown()){
        // A consumed press, release or drag can change the toolbar, menu or media state its UIA group lists.
        // Releasing the capture sends a nested WM_CAPTURECHANGED (handed a later
        // time), so the refresh after it takes the host time again.
        if(projectionHandoff.acceptsInput()){bool handled{},locked{};
            (void)projectionCall("pointer",[&]{handled=projection->pointer(e,hostTime(time));locked=projection->preview()->pointerLocked();});
            if(handled&&e.kind!=PointerKind::move)automationChanged();host.capturePointer(locked);refresh(hostTime(time));}return true;
    }
    using C=ModuleRegistry::Chain;
    // SystemHUDView.updateControlHighlights: the desktop clock's hover plate.
    if(e.kind==PointerKind::move||e.kind==PointerKind::leave)clockHover(e);
    // Exclusive captures (modal Settings/quit card, Minigame rules, Calendar
    // and Reader menus, Map drag, Archive selection) run before floating Notes.
    // A guarded failure inside routePointer leaves the chain to continue.
    if(registry.first(C::pointerCapture,[&](ModuleOwner&o,const ModuleRouting&r){return o.capturesPointer()&&routePointer(o,r.pointerPolicy,e,hostTime(time),p);},"pointer"))return true;
    if(registry.first(C::pointer,[&](ModuleOwner&o,const ModuleRouting&r){return routePointer(o,r.pointerPolicy,e,hostTime(time),p);},"pointer"))return true;
    // The clock sits below popovers and workspace notes, beside navigation:
    // a style indicator selects that style, the rest opens Work Mode.
    if(clockPress(e,hostTime(time)))return true;
    // The industry logo owns its press and release (flickerIndustryLogo).
    if(logoPointer(e,hostTime(time)))return true;
    const auto at=hostTime(time);
    if(e.kind==PointerKind::move){environment.pointer=p;session->pointerMove(p,at);}
    else if((e.kind==PointerKind::down||e.kind==PointerKind::doubleClick)&&e.button==PointerButton::left){environment.pointer=p;
        // HUDSourceWatchView.mouseDown forwards a press outside every source
        // control to SystemHUDView.mouseDown; nothing above consumed it, so
        // empty space outside the displayed circle closes the HUD with its
        // normal closing animation (OverlayController.closeSystemOverlay).
        if(backgroundPressCloses(p)){perform(lifecycle.close(),at);return true;}
        session->pointerDown(p,at);if(session->navigationPointerActive())host.capturePointer(true);}
    else if(e.kind==PointerKind::up&&e.button==PointerButton::left){environment.pointer=p;if(const auto action=session->pointerUp(p,at))activate(*action);host.capturePointer(false);}
    else if(e.kind==PointerKind::leave){environment.pointer.reset();session->pointerMove({},at);}
    else if(e.kind==PointerKind::captureLost){
        // Normal pointerUp has already cleared its drag/press before our
        // intentional ReleaseCapture sends this notification. Preserve its
        // hover; cancel only unfinished ownership lost to another action.
        if(session->navigationPointerActive()||session->pressed()){
            session->setInputEnabled(false,at);if(focused&&session->phase()==core::VisibilityPhase::visible)session->setInputEnabled(true,at);
        }
    }
    else return false;refresh(at);return true;
}

bool Application::Impl::onWheel(const WheelEvent&e){
    if(!ready)return false;const auto time=now();
    if(projectionShown()){if(projectionHandoff.acceptsInput()){bool handled{};(void)projectionCall("wheel",[&]{handled=projection->wheel(e,hostTime(time));});if(handled)automationChanged();refresh(time);}return true;}
    using C=ModuleRegistry::Chain;
    if(registry.first(C::wheelCapture,[&](ModuleOwner&o,const ModuleRouting&){return o.capturesWheel()&&session->inputEnabled()&&o.wheel(e,hostTime(time));},"wheel")){automationChanged();refresh(time);return true;}
    if(auto*owner=registry.first(C::wheel,[&](ModuleOwner&o,const ModuleRouting&){return session->inputEnabled()&&o.wheel(e,hostTime(time));},"wheel")){
        automationChanged();
        if(owner->routing().wheelRefreshesPointerLock){environment.pointerLocked=pointerLocked();session->setEnvironment(environment,hostTime(time));}
        refresh(time);return true;
    }
    // Wheel input over the clock changes its style (horizontal) and never scrolls navigation.
    if(clockWheel(e,hostTime(time)))return true;
    if(e.horizontal)return false;const bool handled=session->wheel({e.x,e.y},e.steps,e.linesPerStep,hostTime(time));if(handled)refresh(time);return handled;
}

bool Application::Impl::onBeforeKeyTranslation(const NativeMessage&m){
    if(!ready||projectionShown())return false;
    if(registry.any([](ModuleOwner&o){return o.suppressesKeyFilters();},"key filter"))return false;
    const auto target=static_cast<HWND>(m.window),owner=static_cast<HWND>(host.hwnd());
    const bool ownerTarget=target==owner||IsChild(owner,target);bool stop=false;
    return registry.first(ModuleRegistry::Chain::filterKey,[&](ModuleOwner&o,const ModuleRouting&r){
        if(stop)return false;if(r.filterRequiresOwnerTarget&&!ownerTarget){stop=true;return false;}return o.filterKey(m);},"key filter")!=nullptr;
}

bool Application::Impl::onKey(const KeyEvent&e){
    auto stage=probe.measure(LiveProbe::key);
    if(ready&&projectionShown()){if(projectionHandoff.acceptsInput()){bool handled{};(void)projectionCall("key",[&]{handled=projection->key(e,now());});if(handled)automationChanged();refresh(now());}return true;}
    if(ready){
        using C=ModuleRegistry::Chain;
        // A handled key may open or close an editor (Return, Escape) under the pointer.
        if(registry.first(C::keyCapture,[&](ModuleOwner&o,const ModuleRouting&){return session->inputEnabled()&&o.capturesKeys()&&o.key(e,now());},"key")){automationChanged();refresh(now());if(e.kind==KeyKind::down)host.refreshCursor();return true;}
        if(registry.first(C::key,[&](ModuleOwner&o,const ModuleRouting&){return session->inputEnabled()&&o.key(e,now());},"key")){automationChanged();refresh(now());if(e.kind==KeyKind::down)host.refreshCursor();return true;}
    }
    if(ready&&e.kind==KeyKind::down&&e.value==VK_ESCAPE){if(!production())std::cout<<"Preview unhandled Escape"<<std::endl;perform(lifecycle.close(),now());return true;}
    return false;
}

void Application::Impl::onFocus(bool value){
    if(!production())std::cout<<"Preview focus: "<<value<<std::endl;focused=value;
    if(projectionShown()){if(!value)(void)projectionCall("focus",[&]{projection->cancelInteraction(now());});if(ready)refresh(now());return;}
    registry.forEach([&](ModuleOwner&o){o.focus(value,now());},"focus");
    if(ready){const auto time=now();if(!focused){environment.pointer.reset();session->pointerMove({},time);session->setInputEnabled(false,time);}else if(session->phase()==core::VisibilityPhase::visible)session->setInputEnabled(true,time);refresh(time);}
}

void Application::Impl::onApplicationActive(bool active){
    if(ready)broadcast(active?HostEvent::Kind::applicationActivated:HostEvent::Kind::applicationDeactivated,now());
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
    // A display change can move the default adapter: recreate the renderer on it.
    if(ready&&production()){checkAdapter(now());if(stopping)return;}
    if(ready)broadcast(HostEvent::Kind::displaysChanged,now());
    // OverlayController.reposition while the HUD is presented (hot-plug,
    // resolution or arrangement): keep or move the covered display, or close.
    if(ready&&!stopping&&!projectionHandoff.active())repositionHUD(now());
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
    const FatalStage stage("deadline");
    if(!ready||stopping)return;
    // The wake's sample, clamped: owners' deadline() callbacks record at the
    // owner clock (Media Assembly's debounced "edited", Work Mode completion),
    // so every later hand-off below takes the host time again.
    time=hostTime(time);
    bool artwork=projectionShown()&&projection->dismissalComplete(time);
    if(calendarNextWake&&*calendarNextWake<=time){isolate(calendarModuleOwner,"midnight",[&]{calendar->systemChanged(hostTime(time));updateCalendarWake();});artwork=true;}
    if(graphicsIdleAt&&*graphicsIdleAt<=time)releaseIdleGraphics(time);
    // The tray icon was pointed at but no opening came: the device prepared
    // for it goes again (Renderer::discardPreparedDevice).
    if(preparedGraphicsUntil&&*preparedGraphicsUntil<=time){preparedGraphicsUntil.reset();
        if(session->phase()==core::VisibilityPhase::concealed)(void)renderer.discardPreparedDevice();}
    registry.forEach([&](ModuleOwner&o){artwork=o.deadline(hostTime(time))||artwork;if(o.routing().deadlineRefreshesPointerLock){environment.pointerLocked=pointerLocked();session->setEnvironment(environment,hostTime(time));}},"deadline");
    time=hostTime(time);
    if(artwork)automationChanged();
    if(mediaBroker){const auto wake=mediaBroker->nextWakeTime();const bool due=wake&&*wake<=time;if(session->phase()==core::VisibilityPhase::concealed){mediaBroker->sample(time);if(notes)notes->refreshSharedMedia(hostTime(time));if(archiveMedia)isolate(archiveModuleOwner,"deadline",[&]{archiveMedia->refresh(hostTime(time));});if(projectionShown()){(void)projectionCall("media",[&]{projection->mediaChanged(hostTime(time));});artwork=artwork||due;}}else artwork=artwork||due;}
    time=hostTime(time);
    if(headerClock.wake(time)||workMode)artwork=updateClock()||artwork;
    if(workMode&&production())updateTrayMenu();
    if(eventRecorder&&workMode)eventRecorder->receiveWork(workMode->controller().snapshot(hostTime(time)));
    if(eventSaves){const FatalStage save("event log save");eventSaves->capture(hostTime(time));}syncActivityTelemetry(time);if(activityProbe)activityProbe->update(hostTime(time));
    time=hostTime(time);perform(lifecycle.advance(time),time);
    scheduleDeadline();if(artwork)refresh(time);
}

void Application::Impl::onFrame(double time){
    const FatalStage stage("frame");
    if(!ready)return;
    time=hostTime(time);
    if(probe.enabled&&!diagnosticFinished){
        if(diagnosticStart==0)diagnosticStart=time;const auto elapsed=time-diagnosticStart;
        const int phase=elapsed<3?0:elapsed<8?1:elapsed<13?2:3;
        if(phase!=probe.phase)probe.next(phase,time);
        if(phase>=1&&!diagnosticEditing&&phase<3){auto stage=probe.measure(LiveProbe::editTransition);diagnosticEditing=notes->diagnosticEditing(true,time);}
        if(phase==2){environment.pointer=core::Point{metrics.width*.5+200*std::sin(elapsed*2),metrics.height*.5+120*std::cos(elapsed*2)};session->pointerMove(environment.pointer,time);}
        if(phase==3&&diagnosticEditing){auto stage=probe.measure(LiveProbe::editTransition);if(notes->diagnosticEditing(false,time))diagnosticEditing=false;}
        if(elapsed>=16){probe.flush(time);probe.enabled=false;gpu::setTextInputDiagnosticsEnabled(false);diagnosticFinished=true;perform(lifecycle.close(),time);}
    }
    bool active{};
    // A lost GPU device ends this run; main.cpp starts a fresh owner.
    const auto frameStart=Clock::now();
    try{active=present(time,true);}catch(const std::exception&e){if(recoverDevice(e,time))return;throw;}
    if(startupTrace)startupTrace->frame(frameStart,Clock::now());
    if(stopping)return;
    time=hostTime(time);
    if(projectionShown()){
        if(projection->dismissalComplete(time))if(auto command=projectionHandoff.projectionClosed(projectionHandoff.generation()))projectionCommand(*command,time);
        if(!projectionActions.empty()||projectionTrayAction||projectionDisplayPending||projectionResizePending)refresh(now());
        host.setFrameDemand(demand(now()));scheduleDeadline();return;
    }
    host.setFrameDemand(demand(time));scheduleDeadline();
    // OverlayController.finishSystemOpening: the source entrance is complete
    // once the session is visible; a focus loss queued during opening closes now.
    if(lifecycleOpening&&!closing&&session->phase()==core::VisibilityPhase::visible){const auto token=*lifecycleOpening;lifecycleOpening.reset();traceOpened(time);perform(lifecycle.didOpen(token),time);
        if(startupTrace&&startupTrace->complete())writeStartupTrace();
        if(memoryTrace&&production()&&memoryTrace->openings<8)recordMemory("opened-"+std::to_string(++memoryTrace->openings),false);
        // Entries enabled only once opening completes (the Power navigation entry) re-read their state.
        automationChanged();refreshAutomation();if(stopping)return;}
    if(!active&&session->phase()==core::VisibilityPhase::concealed){
        if(notes)notes->setMediaActive(false,hostTime(time));host.hide();if(quitConfirmation)(void)registry.guard(*quitConfirmation,[&]{quitConfirmation->hide(hostTime(time));},"close");concealed(time);
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
                isolate(shelfModuleOwner,"reveal",[&]{shelf->showError(tools::shelfText(tools::ShelfText::revealFailed,watchAppearance.language),hostTime(time));});
        }
        // The properties card's Open: the identity is revalidated, then the
        // Shell opens the item with its default verb (a cancelled choice is S_FALSE).
        if(pendingShelfOpen&&visibleMode()&&!quitRequested){
            auto access=std::move(*pendingShelfOpen);pendingShelfOpen.reset();
            if(FAILED(gpu::openShelfReference(std::move(access)))&&shelf)
                isolate(shelfModuleOwner,"open",[&]{shelf->showError(tools::shelfText(tools::ShelfText::openFailed,watchAppearance.language),hostTime(time));});
        }
        // Settings links and the GitHub releases page (overlay.afterSystemClose).
        if(pendingLink)openPendingLink();
        // A development preview quits with its window; the hidden idle trace keeps running closed.
        if(!production()&&!tray&&!quitRequested&&args.idleTrace.empty())finishQuit(time);else if(!stopping)scheduleDeadline();
    }
}

// The shared executors' coalesced completion notice. Completions an owner
// submitted run under that owner's isolation (UtilityExecutor attribution);
// each module's queue-capacity callback runs under its own.
void Application::Impl::utilityCompleted(){
    const FatalStage stage("utility completion");
    if(utility)utility->drain();if(centerLogoRetry)updateCenterLogo();if(longWork)longWork->drain();
    if(activityProbe)activityProbe->submitPending();syncActivityTelemetry(now());if(systemServices)systemServices->clipboard_queue_capacity_available();
    if(storage)isolate(storageModuleOwner,"utility completion",[&]{storage->utilityCompleted(storageFixture?storageFixture->time.load(std::memory_order_relaxed):now());});
    if(archiveService)isolate(archiveModuleOwner,"utility completion",[&]{archiveService->queueCapacityAvailable();});
    if(readerOwner)isolate(readerModuleOwner,"utility completion",[&]{readerOwner->queueCapacityAvailable();});
    if(calendarState)isolate(calendarModuleOwner,"utility completion",[&]{calendarState->queueCapacityAvailable();calendarNotifications->queueCapacityAvailable();});
    if(map)isolate(mapModuleOwner,"utility completion",[&]{if(notes&&notes->selected()==core::Module::map)requestMapGeography();map->utilityCompleted(visibleMode()?now():mapTime);});
    if(settingsSaves){settingsSaves->queueCapacityAvailable();if(settingsSaves->status().error&&settingsUI)isolate(settingsModuleOwner,"utility completion",[&]{settingsUI->controller().setStatus(*settingsSaves->status().error);});}
    if(eventSaves)eventSaves->retry();broadcast(HostEvent::Kind::utilityCompleted,now());
}

// SystemHUDView.coreDesignPoint: the client point on the source center plane
// (the 1000 x 640 design canvas the module plane, notes and editors share);
// none when the plane does not invert there.
std::optional<core::Point> Application::Impl::coreDesignPoint(core::Point client)const{
    if(!chromePlan||!chromePlan->projection().center||!metrics.pixelWidth||!metrics.pixelHeight||!(metrics.scale>0)||!std::isfinite(client.x)||!std::isfinite(client.y))return std::nullopt;
    const auto plane=core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale)**chromePlan->projection().center,metrics.pixelWidth,metrics.pixelHeight);
    return plane.unproject({client.x*metrics.scale,client.y*metrics.scale});
}
// HUDSourceWatchView.mouseDown: a press over a source control (a button,
// disabled or deploying cards included, the scroll strip and the gaps of the
// navigation viewport, the industry logo) stays with the source; any other
// press is the native host's (SystemHUDView.mouseDown). The host's pointer
// chains, Notes and the clock already took theirs (popovers and menus unwind
// first, a modal card covers everything), so what reaches this point is empty
// space: inside the displayed circle (radius 310 around the design center
// 500,320, through the same live projection that draws it) it belongs to the
// HUD, outside it - the sides of the screen and the rest of the transparent
// overlay - it closes the HUD with the normal closing animation. Accepted
// while input is enabled and during the entrance (the controller queues the
// close until the opening completes); never while quitting or closing.
bool Application::Impl::backgroundPressCloses(core::Point client){
    if(!ready||stopping||closing||quitRequested||!notes)return false;
    const auto phase=session->phase();
    if(phase==core::VisibilityPhase::concealed||!(session->inputEnabled()||phase==core::VisibilityPhase::opening))return false;
    if(quitConfirmation&&quitConfirmation->presented())return false;
    if(session->sourceControlAt(client,centerLogoMainID))return false;
    // A press over a shown owner control (ModuleOwner::covers, e.g. the header
    // sanity wallet at core design 562,2 168x42, almost entirely outside the
    // displayed circle) is never a background press. During the entrance the
    // owners' pointer routes refuse input (routePointer needs inputEnabled), so
    // without this the press would queue a close.
    if(registry.covers(client))return false;
    std::optional<core::Point> design=coreDesignPoint(client);
    if(!design){
        // SystemHUDView: `coreDesignPoint(p) ?? p` (the flat design point).
        const auto layout=source::DesktopChromeLayout::make(chromeSettings(notes->selected()),core::Matrix4{},core::Matrix4{});
        if(!(layout.designScale>0))return false;
        design=core::Point{(client.x-layout.designOrigin.x)/layout.designScale,(client.y-layout.designOrigin.y)/layout.designScale};
    }
    const double dx=design->x-500,dy=design->y-320;
    return !(dx*dx+dy*dy<310.*310.);
}

std::optional<std::intptr_t> Application::Impl::onAppMessage(const NativeMessage&m){
    auto stage=probe.measure(LiveProbe::message);
    if(!stopping&&systemServices)systemServices->handle_message(m.message,m.wParam,m.lParam);
    // The summon hook (app/summon_hotkey.hpp) recognised the chord: the same
    // toggle as WM_HOTKEY. A Windows-key chord is reported by both paths;
    // RegisterHotKey's copy right after the hook's is the same press.
    if(ready&&m.message==summonHookMessage){
        if(m.wParam==summonHookGeneration&&summonHook&&summonHook->running()&&summonHotkeyEnabled()&&!quitRequested&&!stopping){
            const auto time=now();lastHookSummon=time;trayAction(gpu::TrayAction::openOverlay,true,time);}
        return 0;
    }
    // Pointing at or pressing the tray icon precedes an opening from its
    // menu by a few hundred milliseconds at least: a device released after
    // a long close starts being created now (discarded again if no opening
    // follows). Version 4 notifications carry the event in the low word.
    if(ready&&tray&&m.message==trayMessage&&session->phase()==core::VisibilityPhase::concealed){
        const auto event=LOWORD(static_cast<DWORD>(m.lParam));
        if(event==WM_MOUSEMOVE||event==WM_LBUTTONDOWN||event==WM_RBUTTONDOWN||event==WM_CONTEXTMENU||event==NIN_SELECT||event==NIN_KEYSELECT)prepareGraphics(true);
    }
    if(ready&&tray&&tray->message(m.message,m.wParam,m.lParam)){
        if(const auto action=tray->takeAction()){
            const bool hotkey=m.message==WM_HOTKEY;const auto time=now();
            if(!(hotkey&&lastHookSummon&&time-*lastHookSummon<.3))trayAction(*action,hotkey,time);
        }
        return 0;
    }
    if(ready&&m.wParam==serviceGeneration){
        // Utility completions run under their submitting owner's isolation.
        if(m.message==utilityMessage){utilityCompleted();refresh(now());return 0;}
        if(m.message==audioMessage&&audio){
            // The shared AudioRuntime posted one coalesced notice: adopt the
            // publication, feed the Event Log device list and the areas.
            const FatalStage stage("audio");const auto changes=audio->drain();const auto time=now();
            if(changes.devices&&eventRecorder)if(const auto labels=audio->deviceLabels())eventRecorder->receiveAudioDevices(*labels);
            std::uint32_t flags{};
            if(changes.snapshot)flags|=HostEvent::audioSnapshot;if(changes.volume)flags|=HostEvent::audioVolume;
            if(changes.applications)flags|=HostEvent::audioApplications;if(changes.devices)flags|=HostEvent::audioDevices;
            if(flags)broadcast(HostEvent::Kind::audioChanged,time,flags);
            if(changes.volume)refresh(time);return 0;
        }
        if(m.message==hudDropMessage){receiveDrop(now());return 0;}
        if(m.message==prewarmMessage){prewarm();return 0;}
        // The ID card feed changed while the HUD was concealed (connectAreas).
        if(m.message==idCardMessage){idCardQueued=false;const FatalStage stage("id card");
            if(idCard&&!stopping&&session->phase()==core::VisibilityPhase::concealed)if(auto next=idCard->apply())session->setDesktopSettings(std::move(*next));
            return 0;}
        if(m.message==themeColorMessage){chooseThemeColor();return 0;}
        if(m.message==restartMessage){
            // DataRootRestart: the red power button's confirmed quit (close, the
            // flush chain, every save queue), then a fresh run (main.cpp).
            if(restartPending&&!stopping&&!quitRequested)perform(lifecycle.requestQuit(),now());
            return 0;
        }
        if(m.message==restartDeclinedMessage){runRestartDeclined();refresh(now());return 0;}
        if(m.message==projectionDropMessage){
            auto paths=std::exchange(projectionDrop->paths,{});const auto time=now();
            if(!paths.empty()&&projectionShown()&&projectionHandoff.acceptsInput())(void)projectionCall("drop",[&]{if(const auto token=projection->preview()->prepareImportRequest()){
                try{std::vector<tools::ProjectionImportFile>files;files.reserve(paths.size());for(auto&path:paths){auto title=utf8(utf8Path(path).filename());files.push_back({std::move(path),std::move(title),{}});}
                    projection->media()->beginImport(*token,files,projectionDrop->point,hostTime(time));
                }catch(const std::exception&){projection->preview()->receiveImportError(*token,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),hostTime(time));}
            }});
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
                if(!projectionShown()||!projectionHandoff.acceptsInput())continue;
                using K=tools::ProjectionPreviewAction::Kind;
                if(action.kind==K::returnToHUD){if(auto command=projectionHandoff.returnToHUD())projectionCommand(*command,time);continue;}
                (void)projectionCall("action",[&]{auto*view=projection->preview();
                    if(action.kind==K::togglePlayback||action.kind==K::seek){projection->media()->action(action,hostTime(time));return;}
                    if(!view->importRequestCurrent(action.generation))return;
                    try{
                        if(action.kind==K::chooseLocal){
                            if(requestMediaPicker()){pickerOwner=PickerOwner::projection;pendingProjectionPicker=action;host.capturePointer(false);}
                            else view->receiveImportError(action.generation,core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),hostTime(time));
                        }else if(action.kind==K::useShelf&&shelf){
                            auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action.shelfID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf Projection reference is unavailable");
                            const std::array files{tools::ProjectionImportFile{lease->metadata().windowsPath,lease->metadata().name,lease}};
                            projection->media()->beginImport(action.generation,files,{},hostTime(time));
                        }
                    }catch(const std::exception&){view->receiveImportError(action.generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),hostTime(time));}});
            }
            if(projectionShown()&&projection->dismissalComplete(time))if(auto command=projectionHandoff.projectionClosed(projectionHandoff.generation()))projectionCommand(*command,time);
            refresh(time);return 0;
        }
        if(m.message==readerActionMessage){
            readerActionQueued=false;auto action=std::exchange(pendingReaderAction,{});const auto time=now();
            if(action&&reader)isolate(readerModuleOwner,"import",[&]{if(!reader->importRequestCurrent(action->generation))return;try {
                if(action->kind==tools::ReaderImportAction::Kind::chooseLocal){
                    if(pickerOwner!=PickerOwner::none||!mediaPicker)reader->receiveImportError(action->generation,modules::readerTranslation(modules::readerWindowsPickerBusy,watchAppearance.language),hostTime(time));
                    // The Library's Relink… titles the same single-book picker.
                    else {const auto strings=readerStrings(watchAppearance.language);const auto&caption=action->relinkBookID.empty()?strings.open:strings.relink;mediaPicker->setLabels({caption,caption,caption,gpu::ShelfPickerMode::singleReaderFile});
                        if(mediaPicker->request()){pickerOwner=PickerOwner::reader;pendingReaderPicker=*action;host.capturePointer(false);}
                        else reader->receiveImportError(action->generation,modules::readerTranslation(modules::readerWindowsPickerBusy,watchAppearance.language),hostTime(time));}
                }else if(shelf){auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action->shelfID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf book reference is unavailable");importReaderReference(action->generation,lease->metadata().windowsPath,lease,time);}
            }catch(const std::exception&){reader->receiveImportError(action->generation,modules::readerTranslation(modules::readerWindowsOpenFailed,watchAppearance.language),hostTime(time));}});
            refresh(time);return 0;
        }
        if(m.message==calendarChangedMessage){calendarRefreshQueued=false;updateCalendarWake();refresh(now());return 0;}
        if(m.message==mapChangedMessage){mapRefreshQueued=false;refresh(now());return 0;}
        if(m.message==readerChangedMessage){readerRefreshQueued=false;refresh(now());return 0;}
        if(m.message==clipboardChangedMessage){clipboardRefreshQueued=false;if(clipboardDirty&&session->phase()!=core::VisibilityPhase::concealed&&notes&&notes->selected()==core::Module::clipboard)refresh(now());return 0;}
        if(m.message==eventChangedMessage){eventRefreshQueued=false;if(eventLog)isolate(eventLogModuleOwner,"event log",[&]{eventLog->refresh();});refresh(now());return 0;}
        if(m.message==archiveChangedMessage){archiveRefreshQueued=false;refresh(now());return 0;}
        if(m.message==sharedMediaMessage&&mediaBroker){const FatalStage stage("shared media");const auto time=now();mediaBroker->accept(m.wParam,time);if(notes)notes->refreshSharedMedia(hostTime(time));if(archiveMedia)isolate(archiveModuleOwner,"shared media",[&]{archiveMedia->refresh(hostTime(time));});if(projectionShown())(void)projectionCall("media",[&]{projection->mediaChanged(hostTime(time));});refresh(time);return 0;}
        if(m.message==mediaPickerMessage&&mediaPicker){
            host.suspendCursor(true);mediaPicker->handleMessage(m.wParam,m.lParam);host.suspendCursor(false);
            if(auto result=mediaPicker->drain(m.wParam)){
                const auto owner=std::exchange(pickerOwner,PickerOwner::none);auto action=std::move(pendingArchivePicker);pendingArchivePicker.reset();auto readerAction=std::move(pendingReaderPicker);pendingReaderPicker.reset();auto projectionAction=std::exchange(pendingProjectionPicker,{});const auto time=now();
                if(owner==PickerOwner::logo){
                    // HUDSettingsInteraction.chooseLogo: one image, imported on the shared executor.
                    auto request=std::exchange(pendingLogoImport,std::nullopt);
                    if(request&&request->generation==logoImportGeneration&&!result->canceled()&&SUCCEEDED(result->result)&&result->paths.size()==1)importCenterLogoFile(result->paths.front(),std::move(*request));
                    else if(request&&!result->canceled()&&FAILED(result->result)&&settingsUI)isolate(settingsModuleOwner,"center logo",[&]{settingsUI->showImportError(modules::centerLogoErrorText(modules::CenterLogoError::invalidImage,settingsLanguage(configuration)),hostTime(time));});
                }else if(owner==PickerOwner::area){
                    ApplicationFileResult area;area.cancelled=result->canceled();area.failed=!area.cancelled&&FAILED(result->result);
                    if(!area.cancelled&&!area.failed)area.paths=result->paths;
                    finishAreaPicker(std::move(area));
                }else if(owner==PickerOwner::projection&&projectionAction&&projectionShown()&&projection->preview()->importRequestCurrent(projectionAction->generation)){
                    (void)projectionCall("file dialog",[&]{auto*view=projection->preview();if(result->canceled())view->receiveImportError(projectionAction->generation,{},hostTime(time));
                    else if(SUCCEEDED(result->result)){try{std::vector<tools::ProjectionImportFile>files;files.reserve(result->paths.size());for(const auto&path:result->paths)files.push_back({path,utf8(utf8Path(path).filename()),{}});projection->media()->beginImport(projectionAction->generation,files,{},hostTime(time));}catch(const std::exception&){view->receiveImportError(projectionAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),hostTime(time));}}
                    else view->receiveImportError(projectionAction->generation,core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),hostTime(time));});
                }else if(owner==PickerOwner::reader&&readerAction&&reader){
                    isolate(readerModuleOwner,"file dialog",[&]{if(!reader->importRequestCurrent(readerAction->generation))return;
                    if(result->canceled())reader->receiveImportError(readerAction->generation,{},hostTime(time));
                    else if(SUCCEEDED(result->result)&&result->paths.size()==1)importReaderReference(readerAction->generation,result->paths.front(),{},time);
                    else reader->receiveImportError(readerAction->generation,modules::readerTranslation(modules::readerWindowsOpenFailed,watchAppearance.language),hostTime(time));});
                }else if(!result->canceled()&&owner==PickerOwner::notes&&notes&&!closing&&notes->selected()==core::Module::notes){
                    if(SUCCEEDED(result->result))notes->importMedia(result->paths,mediaInsertionPoint,hostTime(time));else notes->showMediaError(core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),hostTime(time));
                }else if(!result->canceled()&&owner==PickerOwner::archive&&action&&archive&&archiveMedia){
                    isolate(archiveModuleOwner,"file dialog",[&]{if(!archive->mediaRequestCurrent(action->documentID,action->generation))return;
                    if(SUCCEEDED(result->result)){try{std::vector<tools::ArchiveMediaImportFile>files;files.reserve(result->paths.size());for(const auto&path:result->paths)files.push_back({path,utf8(utf8Path(path).filename()),{}});archiveMedia->beginImport(*action,files,hostTime(time));}catch(const std::exception&e){archive->receiveMedia(action->documentID,action->generation,{},e.what(),hostTime(time));}}
                    else archive->receiveMedia(action->documentID,action->generation,{},core::localized("Unable to open selected files","无法打开所选文件",watchAppearance.language),hostTime(time));});
                }
            }refresh(now());return 0;
        }
    }
    if(ready&&production()&&m.message==WM_POWERBROADCAST){
        if(m.wParam==PBT_APMSUSPEND){suspend(Suspension::system,now());return TRUE;}
        if(m.wParam==PBT_APMRESUMEAUTOMATIC||m.wParam==PBT_APMRESUMESUSPEND)resume(Suspension::system,now());
        if(m.wParam==PBT_POWERSETTINGCHANGE&&m.lParam){
            const auto*setting=reinterpret_cast<const POWERBROADCAST_SETTING*>(m.lParam);
            if(setting->PowerSetting==consoleDisplayState&&setting->DataLength>=sizeof(DWORD)){
                DWORD state{};std::memcpy(&state,setting->Data,sizeof(state));
                if(state==0)suspend(Suspension::display,now());else if(state==1)resume(Suspension::display,now()); // dimmed (2) keeps the HUD
            }
        }
    }
    if(ready&&production()&&m.message==WM_WTSSESSION_CHANGE){
        if(m.wParam==WTS_SESSION_LOCK||m.wParam==WTS_CONSOLE_DISCONNECT||m.wParam==WTS_REMOTE_DISCONNECT)suspend(Suspension::session,now());
        else if(m.wParam==WTS_SESSION_UNLOCK||m.wParam==WTS_CONSOLE_CONNECT||m.wParam==WTS_REMOTE_CONNECT)resume(Suspension::session,now());
        return 0;
    }
    if(ready&&(m.message==WM_TIMECHANGE||m.message==WM_SETTINGCHANGE||(m.message==WM_POWERBROADCAST&&(m.wParam==PBT_APMRESUMEAUTOMATIC||m.wParam==PBT_APMRESUMESUSPEND)))){systemChange(m.message,now());return 0;}
    if(ready&&archive&&m.message==WM_APP+221&&m.wParam==archive->mediaRouteGeneration()){
        isolate(archiveModuleOwner,"media action",[&]{if(auto action=archive->takeMediaAction())try{using K=tools::ArchiveMediaAction::Kind;const auto time=now();
            if(action->kind==K::chooseLocal){if(!requestMediaPicker())archive->receiveMedia(action->documentID,action->generation,{},core::localized("File picker is busy","文件选择器正忙",watchAppearance.language),time);else{pickerOwner=PickerOwner::archive;pendingArchivePicker=*action;host.capturePointer(false);}}
            else if(action->kind==K::chooseShelf)archive->presentShelfMedia(shelfChoices(),time);
            else if(action->kind==K::useShelf&&shelf){auto lease=std::make_shared<ehud::data::ShelfFileAccess>(shelf->access(action->itemID));need(lease->open()&&!lease->metadata().isDirectory,"Shelf media reference is unavailable");const std::array files{tools::ArchiveMediaImportFile{lease->metadata().windowsPath,lease->metadata().name,lease}};archiveMedia->beginImport(*action,files,hostTime(time));}
            else if(archiveMedia)archiveMedia->action(*action,hostTime(time));
        }catch(const std::exception&e){if(archiveService&&archiveService->state().selected())archive->setMediaPlayback(archive->mediaIndex(),false,0,e.what(),now());}});
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
            automationChanged();
            const auto routing=owner->routing();
            if(routing.messageRetriesPendingModule&&pendingModule)selectModule(*pendingModule,now());
            if(routing.messageRetriesPendingClose&&pendingClose)close(now());
            refresh(now());return 0;
        }
    }
    if(ready&&shelf&&m.message==tools::ShelfPreview::actionMessage){
        bool closeAfter{};
        isolate(shelfModuleOwner,"action",[&]{if(auto action=shelf->takeAction())try{using K=tools::ShelfPreviewAction::Kind;
            switch(action->kind){
            case K::choose:host.capturePointer(false);shelf->requestFiles();break;
            // HUDFileShelfInteraction.importPasteboard (virtual-file-only
            // clipboards explain themselves; others are ignored silently).
            case K::paste:shelf->paste(now());break;
            case K::reveal:pendingShelfReveal=shelf->access(action->itemID);closeAfter=true;break;
            // The properties card's Open (default application) waits for the close like Reveal.
            case K::open:pendingShelfOpen=shelf->access(action->itemID);closeAfter=true;break;
            case K::preview:host.capturePointer(false);shelf->requestPreview(action->itemID);break;
            // The OLE drag loop dispatches nested messages (frames, timers) with later times.
            case K::drag:{auto transfer=shelf->prepareDrag(action->itemID);shelf->setNativeDragActive(true);host.capturePointer(false);environment.pointerLocked=false;session->setEnvironment(environment,now());DWORD effect{};const auto result=transfer->run(&effect);shelf->setNativeDragActive(false);if(result==DRAGDROP_S_DROP&&(effect&DROPEFFECT_COPY))closeAfter=true;break;}
            }
        }catch(const std::exception&e){shelf->setNativeDragActive(false);shelf->showError(e,now());}});
        if(closeAfter)perform(lifecycle.close(),now());
        refresh(now());return 0;
    }
    return {};
}
} // namespace endfield::app

namespace endfield::app {
void Application::Impl::suspend(Suspension reason,double time){
    // Source: system sleep stops owned per-app routes (levels restored),
    // whatever else already suspended the HUD.
    if(reason==Suspension::system&&!(suspensions&static_cast<unsigned>(reason))&&audio)(void)audio->powerSuspend();
    const bool first=suspensions==0;suspensions|=static_cast<unsigned>(reason);
    lifecycle.setSuspended(true);if(!first)return;
    // AppDelegate.suspend: activity.shutdown(), appActivity.deactivate().
    if(activityProbe){activityPlan.stop();if(activityCatalog)activityCatalog->invalidate();}
    // AppDelegate.suspend: clipboard capture pauses (ClipboardWatcher.setSuspended).
    if(systemServices)systemServices->set_clipboard_suspended(true);
    (void)lifecycle.forceClose();forceConceal(time);
    if(workMode)isolate(workModeModuleOwner,"suspend",[&]{workMode->setSuspended(true,hostTime(time));});
    broadcast(HostEvent::Kind::suspended,time,static_cast<std::uint32_t>(reason));
}
void Application::Impl::resume(Suspension reason,double time){
    // Ignore duplicate or unmatched wake notifications.
    if(!(suspensions&static_cast<unsigned>(reason)))return;
    suspensions&=~static_cast<unsigned>(reason);
    if(reason==Suspension::system&&audio)(void)audio->powerResume(); // re-reads the device list once
    if(suspensions)return;
    // The latest clipboard change made while suspended is captured once.
    if(systemServices)systemServices->set_clipboard_suspended(false);
    // A fresh Activity baseline after sleep/lock (no rate across the gap).
    time=hostTime(time);
    if(activityProbe){activityPlan.start(time);activityProbe->update(time);scheduleDeadline();}
    lifecycle.setSuspended(false);
    if(workMode)isolate(workModeModuleOwner,"resume",[&]{workMode->setSuspended(false,hostTime(time));});
    if(systemServices)systemServices->refresh_battery();recordBattery();updateTrayMenu();
    broadcast(HostEvent::Kind::resumed,time,static_cast<std::uint32_t>(reason));
}
} // namespace endfield::app
