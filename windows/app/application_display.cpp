// Settings > Display host routes of the Application owner: the center logo
// (HUDSourceWatchView.setDesktopCenterLogo, HUDCenterLogoStore import), the
// Theme color picker (HUDSettingsInteraction.chooseColor) and the desktop
// clock's style indicators and Work Mode click (SystemHUDView.clockPoint).
// Content events only: nothing here runs per frame except one retirement
// check after a logo was removed.
#include "app/application_impl.hpp"
#include "native/display_center_logo.hpp"
#include "native/profile_pickers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace endfield::app {
namespace {
constexpr std::string_view centerLogoTexture = "desktop.center-logo";
constexpr auto& clockStyles = ehud::data::SettingsChoices::clockStyles;
constexpr std::string_view clockWorkModeID = "clock:workMode", clockStylePrefix = "clock:style:";
}

// --- Center logo ---------------------------------------------------------------
// HUDSourceWatchView.logoNodeIDs: EndfieldText / EndfieldTextGlow under the
// MiddleDecoNode of the desktop scene.
void Application::Impl::findCenterLogoNodes() {
    for (const auto& node : scene->nodes()) {
        if (std::string_view(node.path).find("/MiddleDecoNode/") == std::string_view::npos) continue;
        if (node.name == "EndfieldText" && centerLogoMainID.empty()) centerLogoMainID = node.id;
        else if (node.name == "EndfieldTextGlow" && centerLogoGlowID.empty()) centerLogoGlowID = node.id;
    }
    // refreshIndustryLogo / flickerIndustryLogo run on the session's frame clock.
    if (session) session->setLogoNodes(centerLogoMainID, centerLogoGlowID);
}
// HUDSourceWatchView.mouseDown/mouseUp over the industry logo: a press inside
// it belongs to the logo (never closes the HUD, never reaches a card); the
// release inside it flickers the logo (flickerIndustryLogo).
bool Application::Impl::logoPointer(const PointerEvent& e, double time) {
    if (centerLogoMainID.empty()) return false;
    const core::Point p{e.x, e.y};
    if (e.kind == PointerKind::captureLost) { logoPressed = false; return false; }
    if ((e.kind == PointerKind::down || e.kind == PointerKind::doubleClick) && e.button == PointerButton::left) {
        logoPressed = session->inputEnabled() && session->phase() == core::VisibilityPhase::visible && session->logoContains(p);
        if (logoPressed) host.capturePointer(true);
        return logoPressed;
    }
    if (e.kind == PointerKind::up && e.button == PointerButton::left && logoPressed) {
        logoPressed = false; host.capturePointer(false);
        if (session->logoContains(p) && session->flickerLogo(time)) refresh(time);
        return true;
    }
    return false;
}
// HUDSourceWatchView.mouseDown/mouseUp over a scroll indicator: the press is
// the indicator's (pressedScrollDirection); a release over the same one
// scrolls 32 points with the spring, anywhere else it does nothing. Moves keep
// updating the list's hover, as on the Mac (no hover or pressed look).
bool Application::Impl::scrollIndicatorPointer(const PointerEvent& e, double time) {
    if (e.kind == PointerKind::captureLost) { scrollIndicatorPressed.reset(); return false; }
    const core::Point p{e.x, e.y};
    if ((e.kind == PointerKind::down || e.kind == PointerKind::doubleClick) && e.button == PointerButton::left) {
        scrollIndicatorPressed = session->scrollIndicatorAt(p);
        if (scrollIndicatorPressed) host.capturePointer(true);
        return scrollIndicatorPressed.has_value();
    }
    if (e.kind == PointerKind::up && e.button == PointerButton::left && scrollIndicatorPressed) {
        const int direction = *scrollIndicatorPressed;
        scrollIndicatorPressed.reset(); host.capturePointer(false);
        if (session->scrollIndicatorAt(p) == direction && session->scrollDirection(direction, true, time)) refresh(time);
        return true;
    }
    return false;
}
// setDesktopCenterLogo(choice, revision): on a choice/revision change the
// artwork is decoded and composed on the shared executor, then bound.
void Application::Impl::updateCenterLogo() {
    if (!materials || centerLogoMainID.empty() || stopping) return;
    const auto choice = modules::centerLogoChoice(configuration.string("centerLogo")).value_or(modules::CenterLogoChoice::endfield);
    const auto& stored = configuration.fields["centerLogoRevision"];
    const std::string revision = stored.isString() ? stored.string() : std::string{};
    auto key = std::string(modules::centerLogoIdentifier(choice)) + ":" + revision;
    if (key == centerLogoKey) return;
    if (choice == modules::CenterLogoChoice::endfield || !utility) {
        centerLogoKey = std::move(key); ++centerLogoGeneration;
        applyCenterLogo(nullptr);
        return;
    }
    if (!centerLogoRoute) centerLogoRoute = utility->makeRoute();
    const auto presets = args.resources ? args.resources("common.centerLogos") : fs::path{};
    const auto store = args.dataRoot / "CenterLogo";
    auto result = std::make_shared<std::optional<modules::CenterLogoArtwork>>();
    const auto generation = centerLogoGeneration + 1;
    const bool accepted = utility->submit(centerLogoRoute, [choice, revision, presets, store, result] {
        std::optional<modules::CenterLogoBitmap> bitmap;
        if (choice == modules::CenterLogoChoice::custom) bitmap = gpu::decodeCenterLogoRevision(store, revision);
        else if (!presets.empty()) bitmap = gpu::decodeCenterLogoPreset(presets, choice);
        if (bitmap) *result = modules::centerLogoArtwork(*bitmap);
    }, [this, generation, result](std::exception_ptr error) {
        if (stopping || generation != centerLogoGeneration) return;
        // A missing or damaged logo keeps the packet artwork (the source's nil image).
        applyCenterLogo(!error && result->has_value() ? &**result : nullptr);
    });
    // A full queue retries on the next utility completion, never on a timer.
    if (!accepted) { centerLogoRetry = true; return; }
    centerLogoKey = std::move(key); centerLogoGeneration = generation; centerLogoRetry = false;
}
void Application::Impl::applyCenterLogo(const modules::CenterLogoArtwork* art) {
    if (centerLogoMainID.empty()) return;
    auto base = idCard ? idCard->baseline() : desktopBaseline.value_or(source::SourceDesktopFrameSettings{});
    if (art) {
        (void)materials->registerTexture(centerLogoTexture, art->width, art->height, art->texturePixels, ++centerLogoTextureRevision, gpu::SourceRuntimeSampler{1, 1, 1});
        base.images[centerLogoMainID] = source::SourceDesktopImage{std::string(centerLogoTexture), {float(art->width), float(art->height)}, source::Vec2{art->displayWidth, art->displayHeight}};
        base.normalMaterialNodes.insert(centerLogoMainID);
        if (!centerLogoGlowID.empty()) base.hiddenNodes.insert(centerLogoGlowID);
        centerLogoBound = true; centerLogoRetire = false;
    } else {
        base.images.erase(centerLogoMainID); base.normalMaterialNodes.erase(centerLogoMainID);
        if (!centerLogoGlowID.empty()) base.hiddenNodes.erase(centerLogoGlowID);
        if (centerLogoBound) { centerLogoBound = false; centerLogoRetire = true; }
    }
    if (idCard) idCard->setBaseline(std::move(base)); // applied by the next present()
    else { desktopBaseline = base; session->setDesktopSettings(std::move(base)); }
    if (ready && !stopping) host.invalidate();
}
// The registered pixels go once a frame without the override was assembled.
void Application::Impl::retireCenterLogo() {
    if (centerLogoRetire && materials && materials->unregisterTexture(centerLogoTexture)) centerLogoRetire = false;
}
// HUDSettingsInteraction.chooseLogo: one image through the shared dialog,
// imported on the shared executor; the result applies only while the same
// selection and revision are still shown.
void Application::Impl::chooseCenterLogo() {
    if (!settingsUI || stopping || closing || !ready) return;
    if (!visibleMode()) { ++logoRequests; return; } // hidden coverage never opens a dialog
    if (pickerOwner != PickerOwner::none || !mediaPicker || themeColorSession.open()) return;
    const auto l = settingsLanguage(configuration);
    mediaPicker->setLabels({core::localized("Center logo", "中心标志", l), core::localized("Open", "打开", l), core::localized("Open", "打开", l),
        gpu::ShelfPickerMode::singleFile, std::string(core::translated(core::imageFilesFilter, l)), narrow(gpu::profileImageFilterSpec())});
    if (!mediaPicker->request()) return;
    const auto& stored = configuration.fields["centerLogoRevision"];
    pendingLogoImport = LogoImport{++logoImportGeneration, configuration.string("centerLogo"), stored.isString() ? stored.string() : std::string{}};
    pickerOwner = PickerOwner::logo; host.capturePointer(false);
}
void Application::Impl::importCenterLogoFile(std::string path, LogoImport request) {
    if (!utility || stopping) return;
    if (!logoImportRoute) logoImportRoute = utility->makeRoute();
    auto revision = std::make_shared<std::string>();
    auto failure = std::make_shared<std::optional<std::pair<modules::CenterLogoError, std::string>>>();
    const auto store = args.dataRoot / "CenterLogo";
    const bool accepted = utility->submit(logoImportRoute, [path = std::move(path), store, revision, failure] {
        try { *revision = gpu::importCenterLogo(utf8Path(path), store); }
        catch (const gpu::CenterLogoImportError& e) { *failure = std::pair{e.error, e.detail}; }
        catch (const std::exception&) { *failure = std::pair{modules::CenterLogoError::invalidImage, std::string{}}; }
    }, [this, request, revision, failure](std::exception_ptr) {
        if (stopping || !settingsUI || request.generation != logoImportGeneration) return;
        const auto& stored = configuration.fields["centerLogoRevision"];
        if (configuration.string("centerLogo") != request.selection || (stored.isString() ? stored.string() : std::string{}) != request.previous) return;
        const auto time = areaClock();
        isolate(settingsModuleOwner, "center logo", [&] {
            if (failure->has_value()) settingsUI->showImportError(modules::centerLogoErrorText((*failure)->first, settingsLanguage(configuration), (*failure)->second), time);
            else if (!revision->empty()) settingsUI->setCustomLogo(*revision, time);
        });
        refresh(time);
    });
    if (!accepted) isolate(settingsModuleOwner, "center logo", [&] { settingsUI->showImportError(std::string(core::translated(core::filePickerBusy, settingsLanguage(configuration))), areaClock()); });
}

// --- Theme color -----------------------------------------------------------------
// The modal ChooseColorW never opens inside a pointer dispatch: the Settings
// hook posts a private notice and the picker runs from the message route.
void Application::Impl::requestThemeColor() {
    if (!settingsUI || stopping || !ready) return;
    if (!visibleMode()) { ++colorRequests; return; }
    if (themeColorQueued || themeColorSession.open()) return;
    themeColorQueued = PostMessageW(static_cast<HWND>(host.hwnd()), themeColorMessage, serviceGeneration, 0) != FALSE;
}
void Application::Impl::chooseThemeColor() {
    themeColorQueued = false;
    if (!settingsUI || stopping || closing || !ready || session->phase() == core::VisibilityPhase::concealed || themeColorSession.open()) return;
    if (!notes || notes->selected() != core::Module::display || pickerOwner != PickerOwner::none) return;
    const auto initial = watchAppearance.accentSRGB;
    const auto l = settingsLanguage(configuration);
    // NSColorPanel(continuous): every complete change applies at once.
    const auto live = [this](std::array<double, 3> c) { if (settingsUI && !stopping) { isolate(settingsModuleOwner, "theme color", [&] { settingsUI->setCustomColor(c, areaClock()); }); refresh(areaClock()); } };
    std::optional<std::array<double, 3>> chosen;
    host.suspendCursor(true);
    try { chosen = gpu::chooseProfileColor(static_cast<HWND>(host.hwnd()), initial, core::localized("Theme color", "主题颜色", l), live, &themeColorSession); }
    catch (...) { host.suspendCursor(false); throw; }
    host.suspendCursor(false);
    const bool dismissed = std::exchange(themeColorDismissed, false);
    if (!settingsUI || stopping) return;
    // OK keeps the chosen colour; Cancel restores the one the picker started
    // from; a dismissal (module switch, HUD closing) keeps the last live
    // colour like the source's orderOut of the continuous panel.
    isolate(settingsModuleOwner, "theme color", [&] {
        if (chosen) settingsUI->setCustomColor(*chosen, areaClock());
        else if (!dismissed) settingsUI->setCustomColor(initial, areaClock());
    });
    refresh(areaClock());
}
// HUDSettingsInteraction.deactivate: the colour panel closes and a pending
// logo import no longer applies (module switch, HUD closing).
void Application::Impl::deactivateDisplayPanels() {
    if (themeColorSession.open()) { themeColorDismissed = true; themeColorSession.cancel(); }
    ++logoImportGeneration; pendingLogoImport.reset();
}

// --- Desktop clock -------------------------------------------------------------
std::optional<core::Point> Application::Impl::clockPoint(core::Point client) const {
    if (!chrome || !metrics.pixelWidth || !metrics.pixelHeight) return std::nullopt;
    const auto viewProjection = gpu::layerViewportProjection(metrics.pixelWidth, metrics.pixelHeight) * core::Matrix4::scale(metrics.scale, metrics.scale);
    return chrome->statusPoint(client, viewProjection, metrics.pixelWidth, metrics.pixelHeight, metrics.scale);
}
// SystemHUDView.allowsModuleInput for the chrome controls.
bool Application::Impl::moduleInputAllowed() const {
    return chrome && ready && !stopping && !closing && !quitRequested && notes && session->inputEnabled() &&
        session->phase() == core::VisibilityPhase::visible &&
        !notes->modulePresentation().transitioning && !(designOverlay && designOverlay->capturesPointer());
}
// Mac fe3f046: the clock's controls (hover, press, wheel and its UI
// Automation peers) take allowsInputBesidePlacement.
bool Application::Impl::clockAcceptsInput() const { return allowsInputBesidePlacement(); }

// --- Display > HUD elements ------------------------------------------------------
const ehud::data::OverlayLayout& Application::Impl::shownOverlayLayout() const noexcept {
    if (hudElements) if (const auto* draft = hudElements->draft()) return *draft;
    return appliedOverlayLayout;
}
bool Application::Impl::placingOverlay() const noexcept { return hudElements && hudElements->placing(); }
bool Application::Impl::allowsInputBesidePlacement() const { return moduleInputAllowed() && !placingOverlay(); }
// beginOverlayPlacement: only while module input is allowed, nothing is
// placed and the confirm bar is down. A design field being edited commits,
// the Sanity recovery menu closes, the source watch's buttons wait.
void Application::Impl::beginOverlayPlacement(double time) {
    if (!hudElements || placingOverlay() || !moduleInputAllowed()) return;
    time = hostTime(time);
    designOverlay->context().editorFocusMoved();
    if (areaHost) if (auto* preempt = areaHost->find<QuitPreemption>(quitPreemptionService))
        (void)isolateService(quitPreemptionService, "HUD elements", [&] { (void)(*preempt)(time); });
    setClockHovered(false);
    if (!hudElements->begin(appliedOverlayLayout, time)) return;
    // SystemHUDView.updateNavigationButtonStates: the source watch's buttons
    // and the navigation peers wait (allowsInputBesidePlacement). The editor
    // takes every pointer event meanwhile, so the watch only drops its hover
    // and press here; the peers check placingOverlay().
    if (session->inputEnabled()) { session->setInputEnabled(false, time); session->setInputEnabled(true, time); }
    environment.pointerLocked = pointerLocked(); session->setEnvironment(environment, time);
    if (automation) automation->disconnectOwners();
    automationChanged(); refresh(time);
}
void Application::Impl::overlayLayoutChanged() {
    ++overlayLayoutRevision;
    applyOverlayTitle();
    if (!placingOverlay() && ready && !stopping && session) {
        // Placing ended: the parallax and the source watch's buttons return.
        environment.pointerLocked = pointerLocked(); session->setEnvironment(environment, hostTime(now()));
        automationChanged();
    }
    if (ready && !stopping) host.invalidate();
}
// overlayNatural: the title measured as it is drawn at its size (the
// chrome's measurement), the gauge's 168 x 42, the alert column the facility
// area publishes (one tag's slot without it).
core::OverlaySize Application::Impl::overlayNatural(core::OverlayGeometry::Element element, const ehud::data::OverlayLayout& layout) {
    using G = core::OverlayGeometry;
    switch (element) {
    case G::Element::title: {
        if (!chrome) return {300, G::titleHeight};
        const double size = layout.title.normalized().size;
        const auto title = layout.shownTitle(), subtitle = layout.shownSubtitle();
        if (!titleNatural || titleNatural->title != title || titleNatural->subtitle != subtitle || titleNatural->size != size || titleNatural->language != watchAppearance.language) {
            const auto r = chrome->titleNatural(title, subtitle, size);
            titleNatural = TitleNatural{std::string(title), std::string(subtitle), size, watchAppearance.language, {r.width, r.height}};
        }
        return titleNatural->natural;
    }
    case G::Element::sanity: return G::gauge;
    case G::Element::alerts:
        if (areaHost) if (const auto* source = areaHost->find<core::OverlayAlertsSource>(core::overlayAlertsKey); source && source->natural) return source->natural();
        return {272 * modules::design::geometry::baseUnit, 28 * modules::design::geometry::baseUnit};
    }
    return {};
}
// applyOverlayLayout for the title: its text, size, Show switch and drawn
// place on the core plane (the chrome's header surface).
void Application::Impl::applyOverlayTitle() {
    if (!chrome) return;
    using G = core::OverlayGeometry;
    const auto& layout = shownOverlayLayout();
    const auto natural = overlayNatural(G::Element::title, layout);
    const auto placed = G::clamped(G::Element::title, layout.title, natural);
    const auto frame = G::frame(G::Element::title, placed, natural);
    auto& title = chromeContent.title;
    title.title = layout.shownTitle(); title.subtitle = layout.shownSubtitle();
    title.size = placed.size; title.origin = {frame.x, frame.y}; title.shown = layout.showsTitle;
    if (chrome->setContent(chromeContent) && ready && !stopping) host.invalidate();
}
void Application::Impl::setClockHovered(bool value) {
    if (!chrome || chromeContent.clockHovered == value) return;
    chromeContent.clockHovered = value;
    if (chrome->setContent(chromeContent) && ready && !stopping) host.invalidate();
}
// updateControlHighlights: the clock is hovered when no menu captures the
// pointer and no Notes card covers it.
void Application::Impl::clockHover(const PointerEvent& e) {
    if (!chrome) return;
    const core::Point p{e.x, e.y};
    const bool hover = e.kind == PointerKind::move && clockAcceptsInput() &&
        !registry.any([](ModuleOwner& o) { return o.capturesPointer(); }, "clock hover") && !notes->covers(p) && clockPoint(p).has_value();
    setClockHovered(hover);
}
// mouseDown: a style indicator selects that clock style, the rest of the
// clock opens Work Mode.
bool Application::Impl::clockPress(const PointerEvent& e, double time) {
    if ((e.kind != PointerKind::down && e.kind != PointerKind::doubleClick) || e.button != PointerButton::left || !clockAcceptsInput()) return false;
    time = hostTime(time);
    const auto local = clockPoint({e.x, e.y});
    if (!local) return false;
    if (const auto index = source::DesktopClockArtworkPlan::indicatorAt(*local)) selectClockStyle(*index, time);
    else { const auto effects = lifecycle.selectModule(core::Module::workMode); if (effects.empty()) selectModule(core::Module::workMode, time); else perform(effects, time); }
    refresh(time);
    return true;
}
void Application::Impl::selectClockStyle(unsigned index, double time) {
    if (!settingsUI || index >= clockStyles.size()) return;
    auto next = settingsUI->controller().committed();
    if (next.string("clockStyle") == clockStyles[index]) return;
    next.set("clockStyle", std::string(clockStyles[index]));
    isolate(settingsModuleOwner, "clock style", [&] { settingsUI->controller().update(std::move(next), hostTime(time)); });
}
// scrollWheel over the clock: a horizontal swipe of 28 points advances the
// style once per gesture (HUDClockStyleArtwork.scroll); wheel input over the
// clock never reaches navigation.
// layoutClockControls: "Work Mode" over the clock body and "Clock style N"
// over each indicator (the selected one's accessibilityValue is "Selected"),
// shown only while the clock accepts input. Presses run the same routes as
// clicks. The buttons are UI Automation peers of a host owner that draws
// nothing and routes no input (app/hud_button_automation.hpp).
void Application::Impl::constructClockAutomation() {
    if (!chrome) return;
    HudButtonSource buttons;
    buttons.name = "desktopClock";
    buttons.peers = [this](std::vector<HudButtonPeer>& out) {
        if (!clockAcceptsInput() || !chrome->statusWorld()) return;
        const auto l = watchAppearance.language == core::Language::system ? core::Language::english : watchAppearance.language;
        HudButtonPeer work;
        work.id = std::string(clockWorkModeID); work.name = core::localized("Work Mode", "工作模式", l); work.slot = 0;
        out.push_back(std::move(work));
        const auto selected = static_cast<unsigned>(chromeContent.style);
        for (unsigned n = 0; n < clockStyles.size(); ++n) {
            HudButtonPeer style;
            style.id = std::string(clockStylePrefix) + std::to_string(n);
            style.name = core::localized("Clock style", "时钟样式", l) + " " + std::to_string(n + 1);
            style.hasValue = true;
            style.value = n == selected ? core::localized("Selected", "已选择", l) : std::string{};
            style.slot = n + 1;
            out.push_back(std::move(style));
        }
    };
    // HUDClockStyleArtwork.projectedBounds of the body or an indicator through
    // the status panel (logical client coordinates).
    buttons.frame = [this](const HudButtonPeer& peer) -> std::optional<core::Rect> {
        const auto world = chrome->statusWorld();
        if (!world || !metrics.pixelWidth || !metrics.pixelHeight || peer.slot > clockStyles.size()) return std::nullopt;
        const auto local = peer.slot == 0 ? core::Rect{0, 0, 340, 122} : source::DesktopClockArtworkPlan::indicatorRect(peer.slot - 1);
        const auto plane = core::Projection::viewport(gpu::layerViewportProjection(metrics.pixelWidth, metrics.pixelHeight) * core::Matrix4::scale(metrics.scale, metrics.scale) * *world,
            metrics.pixelWidth, metrics.pixelHeight);
        double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        for (const core::Point c : {core::Point{local.x, local.y}, core::Point{local.x + local.width, local.y}, core::Point{local.x, local.y + local.height}, core::Point{local.x + local.width, local.y + local.height}}) {
            const auto p = plane.project(c);
            if (!p || !std::isfinite(p->x) || !std::isfinite(p->y)) return std::nullopt;
            x0 = std::min(x0, p->x); y0 = std::min(y0, p->y); x1 = std::max(x1, p->x); y1 = std::max(y1, p->y);
        }
        const double s = metrics.scale;
        return core::Rect{x0 / s, y0 / s, (x1 - x0) / s, (y1 - y0) / s};
    };
    buttons.enabled = [this](const HudButtonPeer&) { return clockAcceptsInput(); };
    buttons.invoke = [this](const HudButtonPeer& peer) {
        if (!clockAcceptsInput()) return false;
        const auto time = areaClock();
        if (peer.slot == 0) {
            const auto effects = lifecycle.selectModule(core::Module::workMode);
            if (effects.empty()) selectModule(core::Module::workMode, time); else perform(effects, time);
            return true;
        }
        if (peer.slot > clockStyles.size()) return false;
        selectClockStyle(peer.slot - 1, time);
        return true;
    };
    buttons.changed = [this] { automationChanged(); refresh(now()); };
    buttons.window = host.hwnd();
    buttons.scale = [this] { return metrics.scale; };
    clockAutomation = std::make_unique<HudButtonAutomation>(std::move(buttons));
    registry.add(*clockAutomation);
}
bool Application::Impl::clockWheel(const WheelEvent& e, double time) {
    if (!clockAcceptsInput() || !clockPoint({e.x, e.y})) return false;
    // Notches carry the source's non-precise 12-point line delta. The Mac
    // reads the horizontal swipe; a Windows mouse has only the vertical wheel,
    // so a wheel turned down over the clock advances like a leftward swipe
    // (and up goes back), through the same once-per-gesture threshold.
    const double delta = e.horizontal ? e.steps * 12 : -e.steps * 12;
    if (!std::isfinite(delta)) return true;
    if (time - clockSwipeTime > .25) { clockSwipe = 0; clockSwipeConsumed = false; }
    clockSwipeTime = time;
    if (delta == 0 || clockSwipeConsumed) return true;
    clockSwipe += delta;
    if (std::abs(clockSwipe) < 28) return true;
    const int direction = clockSwipe > 0 ? 1 : -1;
    clockSwipe = 0; clockSwipeConsumed = true;
    unsigned current = 0;
    for (unsigned n = 0; n < clockStyles.size(); ++n) if (configuration.string("clockStyle") == clockStyles[n]) current = n;
    selectClockStyle(static_cast<unsigned>((current + clockStyles.size() + direction) % clockStyles.size()), time);
    refresh(time);
    return true;
}
// SystemHUDView.update(configuration:): clockPage.transition(forward:,
// animated: usesSourceShell && interactionEnabled && !reduceMotion), right
// after the new style's artwork was applied (updateClock).
void Application::Impl::clockStyleChanged(source::DesktopClockStyle previous, double time) {
    if (!chrome) return;
    const auto forward = source::DesktopChromeTiming::styleMovesForward(static_cast<unsigned>(previous), static_cast<unsigned>(chromeContent.style));
    // interactionEnabled: the open, interactive HUD (not while opening or closing).
    const bool interactive = ready && !stopping && !closing && session->inputEnabled() && session->phase() == core::VisibilityPhase::visible;
    if (!interactive || settings.reduceMotion || !chrome->beginStyleTransition(forward, hostTime(time))) { chrome->cancelStyleTransition(); return; }
    if (ready && !stopping) host.invalidate();
}

// --- Display adaptation ---------------------------------------------------------
void Application::Impl::updateContentScale() {
    contentScale = chromePlan && metrics.width > 0 && metrics.height > 0 ? chromePlan->contentScale({0, 0, metrics.width, metrics.height}) : 1;
}
source::DesktopChromeSettings Application::Impl::chromeSettings(core::Module module) const {
    source::DesktopChromeSettings value{{0, 0, metrics.width, metrics.height}, settings.hudScale, settings.hudOffset, module, true};
    value.contentScale = contentScale;
    return value;
}

// --- Summon shortcut --------------------------------------------------------------
bool Application::Impl::summonHotkeyEnabled() const {
    return configuration.summonHotkeyEnabled();
}
// RegisterHotKey and the low-level hook both follow the preference; while a
// shortcut is being recorded (capturing) neither may take the chord.
void Application::Impl::applySummonShortcut(bool capturing) {
    if (!tray) return;
    const auto key = modules::settingsShortcut(configuration);
    const bool enabled = summonHotkeyEnabled() && !capturing && !quitRequested;
    const std::optional<gpu::TrayHotkey> desired = enabled ? std::optional(gpu::TrayHotkey{key.modifiers, key.key}) : std::nullopt;
    bool changed = false, registered = tray->hotkey() == desired;
    if (!registered) { registered = tray->setHotkey(desired); changed = true; }
    bool hooked = false;
    if (summonHook) {
        const auto chord = SummonHotkeyHook::chord(key.key, key.modifiers);
        if (!enabled) { if (summonHook->running()) { summonHook->stop(); changed = true; } }
        else if (summonHook->running() && lastHookChord == chord) hooked = true;
        else { hooked = summonHook->start(chord, ++summonHookGeneration); changed = true; }
        lastHookChord = enabled && hooked ? std::optional(chord) : std::nullopt;
    }
    // GlobalShortcutController.statusDescription (.unavailable) only when
    // neither path can deliver it, with the chord's title (LIB-L13; the
    // notification area for the Mac's menu bar).
    if (!capturing) {
        const auto title = modules::settingsShortcutTitle(key);const std::array<std::string_view, 1> name{title};
        auto status = enabled && !registered && !hooked ? core::renderTranslation(core::translated(core::summonShortcutUnavailable, watchAppearance.language), name).value_or(title) : std::string{};
        if (status != shortcutStatus) { shortcutStatus = std::move(status); if (settingsUI) settingsUI->controller().setExternalStatus(loginStatus, shortcutStatus); }
    }
    if (changed) updateTrayMenu();
}
} // namespace endfield::app
