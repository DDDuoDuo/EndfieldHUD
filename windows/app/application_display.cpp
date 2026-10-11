// Settings > Display host routes of the Application owner: the center logo
// (HUDSourceWatchView.setDesktopCenterLogo, HUDCenterLogoStore import), the
// Theme color picker (HUDSettingsInteraction.chooseColor) and the desktop
// clock's style indicators and Work Mode click (SystemHUDView.clockPoint).
// Content events only: nothing here runs per frame except one retirement
// check after a logo was removed.
#include "app/application_impl.hpp"
#include "native/display_center_logo.hpp"
#include "native/profile_pickers.hpp"
#include <array>
#include <cmath>
#include <iostream>

namespace endfield::app {
namespace {
constexpr std::string_view centerLogoTexture = "desktop.center-logo";
constexpr std::array<std::string_view, 5> clockStyles{"digital", "split", "dial", "rail", "stacked"};
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
        gpu::ShelfPickerMode::singleFile, core::localized("Images", "图片", l), narrow(gpu::profileImageFilterSpec())});
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
        if (failure->has_value()) settingsUI->showImportError(modules::centerLogoErrorText((*failure)->first, settingsLanguage(configuration), (*failure)->second), time);
        else if (!revision->empty()) settingsUI->setCustomLogo(*revision, time);
        refresh(time);
    });
    if (!accepted) settingsUI->showImportError(core::localized("File picker is busy", "文件选择器正忙", settingsLanguage(configuration)), areaClock());
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
    const auto live = [this](std::array<double, 3> c) { if (settingsUI && !stopping) { settingsUI->setCustomColor(c, areaClock()); refresh(areaClock()); } };
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
    if (chosen) settingsUI->setCustomColor(*chosen, areaClock());
    else if (!dismissed) settingsUI->setCustomColor(initial, areaClock());
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
    const auto camera = gpu::layerViewportProjection(metrics.pixelWidth, metrics.pixelHeight) * core::Matrix4::scale(metrics.scale, metrics.scale);
    return chrome->statusPoint(client, camera, metrics.pixelWidth, metrics.pixelHeight, metrics.scale);
}
// SystemHUDView.allowsModuleInput for the chrome controls.
bool Application::Impl::clockAcceptsInput() const {
    return chrome && ready && !stopping && !closing && !quitRequested && notes && session->inputEnabled() &&
        session->phase() == core::VisibilityPhase::visible &&
        !notes->modulePresentation().transitioning && !(quitConfirmation && quitConfirmation->presented());
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
    settingsUI->controller().update(std::move(next), time);
}
// scrollWheel over the clock: a horizontal swipe of 28 points advances the
// style once per gesture (HUDClockStyleArtwork.scroll); wheel input over the
// clock never reaches navigation.
bool Application::Impl::clockWheel(const WheelEvent& e, double time) {
    if (!clockAcceptsInput() || !clockPoint({e.x, e.y})) return false;
    if (!e.horizontal) return true;
    // Notches carry the source's non-precise 12-point line delta.
    const double delta = e.steps * 12;
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
    selectClockStyle((current + clockStyles.size() + direction) % clockStyles.size(), time);
    refresh(time);
    return true;
}
} // namespace endfield::app
