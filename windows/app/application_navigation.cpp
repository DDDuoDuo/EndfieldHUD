// UI Automation buttons of the source navigation (HUDSourceWatchView):
//   - one button per bound desktop button (main and supplemental): label the
//     entry's title, help the module's title or "Open <app>", value
//     "Selected" / "Not selected" (bindDesktopButtons, updateDesktopSelection);
//   - the red power button ("Quit EndfieldHUD") and the profile card (its
//     hit buttons share one element; HUDModule.profile.title);
//   - "Scroll modules up" / "Scroll modules down" over the 20-point top and
//     bottom strips of the navigation viewport while rows are hidden.
// Elements follow HUDSourceWatchView's order (button ids, then the two scroll
// buttons). Frames come from the presented source frame when a client asks
// (updateAccessibility: hit quads projected, clipped to the view and masks);
// a hidden button is offscreen and refuses the press. A press runs the
// click's own route (performClick: the clipped source hit and _clickCd
// cooldown, then the activation) or scrollDesktopNavigation(direction).
// Last, the "ENDFIELD INDUSTRIES" logo button (logoAccessibility): framed by
// the industry logo's projected quad (industryLogoPolygon, unclipped), hidden
// without that quad, enabled while input is; a press runs the signature
// flicker (flickerIndustryLogo; Reduce Motion accepts it and stays steady).
#include "app/application_impl.hpp"
#include <algorithm>

namespace endfield::app {
namespace {
core::Language peerLanguage(core::Language language) { return language == core::Language::system ? core::Language::english : language; }
}

void Application::Impl::constructNavigationAutomation() {
    if (!session) return;
    HudButtonSource buttons;
    buttons.name = "sourceNavigation";
    buttons.peers = [this](std::vector<HudButtonPeer>& out) { navigationPeers(out); };
    buttons.frame = [this](const HudButtonPeer& peer) { return navigationFrame(peer); };
    buttons.enabled = [this](const HudButtonPeer& peer) {
        // updateNavigationButtonStates: allowsInputBesidePlacement.
        if (!session->inputEnabled() || placingOverlay() || peer.slot >= navigationSlots.size()) return false;
        const auto& slot = navigationSlots[peer.slot];
        if (slot.kind == NavigationSlot::Kind::logo) return session->phase() == core::VisibilityPhase::visible;
        return slot.kind != NavigationSlot::Kind::scroll || session->canScroll(slot.direction);
    };
    buttons.invoke = [this](const HudButtonPeer& peer) { return navigationPress(peer); };
    buttons.changed = [this] { automationChanged(); refresh(now()); };
    buttons.window = host.hwnd();
    buttons.scale = [this] { return metrics.scale; };
    navigationAutomation = std::make_unique<HudButtonAutomation>(std::move(buttons));
    registry.add(*navigationAutomation);
}

// Content event: the bound buttons and their texts (allocates; never per frame).
void Application::Impl::navigationPeers(std::vector<HudButtonPeer>& out) {
    navigationSlots.clear();
    if (!session || session->phase() == core::VisibilityPhase::concealed || !session->currentFrame()) return;
    const auto language = peerLanguage(watchAppearance.language);
    const std::string selected = notes ? std::string(core::moduleIdentifier(notes->selected())) : std::string{};
    std::span<const gpu::WatchContentEntry> catalog;
    if (contentCatalog) catalog = contentCatalog->entries();
    struct Listed { std::string key; HudButtonPeer peer; NavigationSlot slot; };
    std::vector<Listed> listed;
    bool profileListed = false;
    for (const auto& [id, action] : session->actions()) {
        if (action == closeAction) continue; // the close regions have no accessibility element
        Listed item;
        item.key = id;
        if (action == quitAction) {
            item.peer.id = "navigation:quit:" + id;
            item.peer.name = core::localized("Quit EndfieldHUD", "退出 EndfieldHUD", language);
            item.slot = {NavigationSlot::Kind::quit, {id}, 0};
        } else if (!profileCardRoot.empty() && (id == profileCardRoot || std::find(profileCardButtons.begin(), profileCardButtons.end(), id) != profileCardButtons.end())) {
            if (profileListed) continue;
            profileListed = true;
            item.key = profileCardRoot;
            item.peer.id = "navigation:profile";
            item.peer.name = moduleCaption("profile", language, "profile");
            std::vector<std::string> ids(profileCardButtons.begin(), profileCardButtons.end());
            if (ids.empty()) ids.push_back(profileCardRoot);
            item.slot = {NavigationSlot::Kind::profile, std::move(ids), 0};
        } else if (baseNavigation.managedButtons.contains(id)) {
            // A desktop button bound to a navigation entry (module or saved app).
            std::string target, title;
            bool module = true, found = false;
            for (const auto& e : navigationEntries) if (e.action == action) { target = e.target; title = e.title; module = e.module; found = true; break; }
            if (!found) for (const auto& e : catalog) if (e.action == action) { target = e.target; title = e.title; found = true; break; }
            if (!found) continue;
            if (module) title = moduleCaption(target, language, title);
            item.peer.id = "navigation:" + id;
            item.peer.name = title;
            item.peer.help = module ? title : core::localized("Open ", "打开 ", language) + title;
            // Arrange on HUD (HUDModuleLayout.arrangeHelp): the module tiles are
            // dragged; saved apps and Add App do nothing until arranging ends.
            if (arrangingModules && session->rowAction(id)) {
                const bool draggable = module && std::find(ehud::data::ModuleLayout::configurable.begin(), ehud::data::ModuleLayout::configurable.end(), target) != ehud::data::ModuleLayout::configurable.end();
                item.peer.help = draggable ? core::localized("Drag to reorder", "拖动以调整顺序", language) : core::localized("Unavailable while arranging", "排列时不可用", language);
            }
            item.peer.hasValue = true;
            item.peer.value = module && target == selected ? core::localized("Selected", "已选择", language) : core::localized("Not selected", "未选择", language);
            item.slot = {NavigationSlot::Kind::entry, {id}, 0};
        } else continue; // auxiliary source actions have no accessibility element
        listed.push_back(std::move(item));
    }
    // setAccessibilityChildren(accessibilityButtons sorted by id) + scroll buttons.
    std::stable_sort(listed.begin(), listed.end(), [](const Listed& a, const Listed& b) { return a.key < b.key; });
    for (auto& item : listed) {
        item.peer.slot = static_cast<std::uint32_t>(navigationSlots.size());
        navigationSlots.push_back(std::move(item.slot));
        out.push_back(std::move(item.peer));
    }
    if (session->scrollFrame()) for (const int direction : {-1, 1}) {
        HudButtonPeer peer;
        peer.id = direction < 0 ? "navigation:scrollUp" : "navigation:scrollDown";
        peer.name = direction < 0 ? core::localized("Scroll modules up", "向上滚动模块", language) : core::localized("Scroll modules down", "向下滚动模块", language);
        peer.slot = static_cast<std::uint32_t>(navigationSlots.size());
        navigationSlots.push_back({NavigationSlot::Kind::scroll, {}, direction});
        out.push_back(std::move(peer));
    }
    if (!centerLogoMainID.empty() && session->logoFrame()) {
        HudButtonPeer peer;
        peer.id = "navigation:industryLogo";
        peer.name = "ENDFIELD INDUSTRIES"; // setAccessibilityLabel, not localized on the Mac
        peer.slot = static_cast<std::uint32_t>(navigationSlots.size());
        navigationSlots.push_back({NavigationSlot::Kind::logo, {}, 0});
        out.push_back(std::move(peer));
    }
}

// HUDDesktopWatchNavigation.entries(shortcuts:layout:) (Mac 1.3.0): the
// right-hand list is `layout.visible` (hidden modules get no entry: no row,
// pointer, accessibility element or recycled source button), then the
// actions that are not configurable modules (Add App) in their source order;
// saved apps join before Add App afterwards (AppShortcutNavigation::plan).
// Content event (applied configuration).
std::vector<std::uint64_t> Application::Impl::layoutRightActions(std::span<const std::uint64_t> right) const {
    std::span<const gpu::WatchContentEntry> catalog;
    if (contentCatalog) catalog = contentCatalog->entries();
    const auto target = [&](std::uint64_t action) -> std::string_view {
        for (const auto& e : catalog) if (e.action == action) return e.target;
        return {};
    };
    const auto configurable = [](std::string_view name) {
        return std::find(ehud::data::ModuleLayout::configurable.begin(), ehud::data::ModuleLayout::configurable.end(), name) != ehud::data::ModuleLayout::configurable.end();
    };
    std::vector<std::uint64_t> out;
    out.reserve(right.size());
    for (const auto& module : arrangedLayout().visible())
        for (const auto action : right) if (target(action) == module) { out.push_back(action); break; }
    for (const auto action : right) if (!configurable(target(action))) out.push_back(action);
    return out;
}
// OverlayController: the HUD reopens on moduleLayout.restorable(lastSystemModule).
core::Module Application::Impl::restorableModule(core::Module last) const {
    return core::moduleNamed(moduleLayout.restorable(core::moduleIdentifier(last))).value_or(last);
}

// Read when a client asks (no allocation): the current frame's geometry.
std::optional<core::Rect> Application::Impl::navigationFrame(const HudButtonPeer& peer) const {
    if (!session || peer.slot >= navigationSlots.size()) return std::nullopt;
    const auto& slot = navigationSlots[peer.slot];
    if (slot.kind == NavigationSlot::Kind::logo) return session->logoFrame();
    if (slot.kind != NavigationSlot::Kind::scroll) return session->buttonFrame(slot.ids);
    const auto area = session->scrollFrame();
    if (!area) return std::nullopt;
    const double height = std::min(20., area->height);
    return core::Rect{area->x, slot.direction < 0 ? area->y : area->y + area->height - height, area->width, height};
}

// accessibilityPerformPress: performClick on the button (any hit button of the
// profile card), or scrollDesktopNavigation(direction, animated: false).
bool Application::Impl::navigationPress(const HudButtonPeer& peer) {
    if (!session || !ready || stopping || placingOverlay() || peer.slot >= navigationSlots.size()) return false;
    const auto time = areaClock();
    const auto slot = navigationSlots[peer.slot]; // the press may re-list the buttons
    if (slot.kind == NavigationSlot::Kind::scroll) {
        if (!session->scrollDirection(slot.direction, false, time)) return false;
        host.invalidate();
        return true;
    }
    if (slot.kind == NavigationSlot::Kind::logo) {
        if (!session->flickerLogo(time)) return false;
        host.invalidate();
        return true;
    }
    // Arrange on HUD: the right-hand tiles are dragged, not opened.
    if (arrangingModules && std::any_of(slot.ids.begin(), slot.ids.end(), [&](const std::string& id) { return session->rowAction(id).has_value(); })) return false;
    for (const auto& id : slot.ids)
        if (const auto activation = session->activate(id, time)) { activate(*activation); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Module arrangement (Display > Modules > Arrange on HUD; Mac SystemHUDView
// 2c047e0 setModuleArrangement ... endModuleArrangeDrag).
const ehud::data::ModuleLayout& Application::Impl::arrangedLayout() const noexcept {
    return arrangeDrag && arrangeDrag->lifted ? arrangeDrag->preview : moduleLayout;
}
const gpu::WatchAppearanceEntry* Application::Impl::navigationEntry(std::uint64_t action) const noexcept {
    for (const auto& e : navigationEntries) if (e.action == action) return &e;
    return nullptr;
}
std::optional<std::uint64_t> Application::Impl::moduleAction(std::string_view module) const noexcept {
    for (const auto& e : navigationEntries) if (e.module && e.target == module) return e.action;
    return std::nullopt;
}
std::optional<std::string_view> Application::Impl::rightRowTarget(core::Point p) {
    using C = ModuleRegistry::Chain;
    if (!arrangingModules || !session || !allowsInputBesidePlacement()) return std::nullopt;
    if (registry.any([](ModuleOwner& o) { return o.capturesPointer(); }, "arrange")) return std::nullopt;
    // A pinned note, the charge capsule, the gauge or the facility banner over
    // a tile keeps its press. The shown module's own controls do not: on the
    // Mac arranging comes before designControlContains, and only the
    // canvas's open text editor (hitTest's textView) wins.
    const auto* canvas = notes ? registry.presenter(notes->selected()) : nullptr;
    if (registry.first(C::pointer, [&](ModuleOwner& o, const ModuleRouting&) {
            return &o == canvas ? o.cursorAt(p) == std::optional(ModuleCursor::text) : o.covers(p);
        }, "arrange")) return std::nullopt;
    // While a tile is dragged, the slots at rest decide where it goes, so the
    // tiles sliding to make room cannot flip the order back.
    const auto action = session->rowActionAt(p, arrangeDrag && arrangeDrag->lifted);
    const auto* entry = action ? navigationEntry(*action) : nullptr;
    if (!entry) return std::nullopt;
    const std::string_view target = entry->module ? std::string_view(entry->target) : std::string_view();
    return ehud::data::ModuleLayout::listsTarget(target) ? std::optional(target) : std::nullopt;
}
std::optional<std::string_view> Application::Impl::arrangeableModule(core::Point p) {
    const auto target = rightRowTarget(p);
    if (!target || target->empty() || *target == "addApp") return std::nullopt;
    return target;
}
void Application::Impl::setModuleArrangement(bool on, double time) {
    time = hostTime(time);
    // Arranging ending while the button is held: the rest of that press does nothing.
    if (arrangeDrag) swallowsArrangePress = true;
    endModuleArrangeDrag(false, session && session->inputEnabled() && !closing && !shelfRetracting, time);
    if (on == arrangingModules) return;
    arrangingModules = on;
    automationChanged();
    host.refreshCursor();
    refresh(time);
}
// The pointer for a press on the list while arranging: a press on a module
// tile may start its drag, the other tiles do nothing; past the lift
// distance the tile follows; the release saves.
bool Application::Impl::arrangePointer(const PointerEvent& e, core::Point p, double time) {
    if (e.kind == PointerKind::move && arrangeDrag && (e.modifiers & MK_LBUTTON)) {
        environment.pointer = p; session->setEnvironment(environment, time);
        dragArrangedModule(p, true, time); refresh(time); return true;
    }
    if (e.kind == PointerKind::up && e.button == PointerButton::left && arrangeDrag) {
        // The order saved is the slot under the tile where it is let go.
        if (arrangeDrag->lifted) dragArrangedModule(p, false, time);
        endModuleArrangeDrag(true, true, time);
        host.capturePointer(false); host.refreshCursor(); refresh(time); return true;
    }
    if (e.kind == PointerKind::captureLost && arrangeDrag) {
        // Owner's choice (2026-11-08): losing the pointer capture mid-drag
        // cancels it: the tiles slide back and nothing is saved. Change this
        // one branch to keep the tile instead.
        endModuleArrangeDrag(false, true, time);
        swallowsArrangePress = true; host.refreshCursor(); refresh(time); return true;
    }
    if ((e.kind == PointerKind::down || e.kind == PointerKind::doubleClick) && e.button == PointerButton::left && rightRowTarget(p)) {
        if (const auto module = arrangeableModule(p)) {
            arrangeDrag = ArrangeDrag{std::string(*module), p, false, moduleLayout};
            host.capturePointer(true); host.refreshCursor();
        }
        refresh(time); return true;
    }
    return false;
}
// Esc during a press on a module tile drops it back in its saved place; the
// rest of that press does nothing.
bool Application::Impl::arrangeKey(const KeyEvent& e, double time) {
    if (!arrangeDrag || e.kind != KeyKind::down || e.value != VK_ESCAPE || e.previouslyDown || !moduleInputAllowed()) return false;
    const auto held = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
    if (e.alt || held(VK_CONTROL) || held(VK_SHIFT) || held(VK_LWIN) || held(VK_RWIN)) return false;
    endModuleArrangeDrag(false, true, time);
    swallowsArrangePress = true; host.refreshCursor(); refresh(time);
    return true;
}
// Past the lift distance the pressed module lifts and follows the pointer:
// over another module it takes that place, over an app or Add App it goes
// after the last visible module, and the others slide to make room. The
// Modules page follows. `edgeScroll` false: a wheel or the list's own motion
// already scrolled.
void Application::Impl::dragArrangedModule(core::Point p, bool edgeScroll, double time) {
    if (!arrangeDrag) return;
    if (!arrangeDrag->lifted) {
        if (std::hypot(p.x - arrangeDrag->start.x, p.y - arrangeDrag->start.y) <= core::DragGripMotion::liftDistance) return;
        arrangeDrag->lifted = true;
        environment.pointerLocked = pointerLocked(); session->setEnvironment(environment, time);
        // Held where it was pressed.
        moveArrangedTile(arrangeDrag->start, time);
    }
    // Near the list's edges it scrolls toward the pointer (on pointer events only).
    if (edgeScroll) (void)session->scrollTowardEdge(p, time);
    const auto target = rightRowTarget(p);
    if (auto next = arrangeDrag->preview.arranging(arrangeDrag->module, target)) {
        arrangeDrag->preview = std::move(*next);
        bindNavigation(time, !settings.reduceMotion);
        if (settingsUI) isolate(settingsModuleOwner, "arrange", [&] { settingsUI->canvas(core::Module::display).previewModuleOrder(arrangeDrag->preview.order); });
    }
    moveArrangedTile(p, time);
}
void Application::Impl::moveArrangedTile(core::Point p, double time) {
    if (!arrangeDrag || !arrangeDrag->lifted) return;
    if (const auto action = moduleAction(arrangeDrag->module)) session->setArrangeDrag(*action, p, true, time);
}
// onDesktopArrangeScroll: a frame drew the list scrolled under the lifted
// tile (the wheel's spring or an edge scroll), so the slot under the still
// pointer previews.
void Application::Impl::followArrangeScroll(double time) {
    if (!arrangeDrag || !arrangeDrag->lifted || !environment.pointer) return;
    dragArrangedModule(*environment.pointer, false, time);
}
// Ends a press on a module tile. A lifted drag saves its order (`save`, one
// write and only if it changed) or the list returns to the saved one; a press
// without a lift does nothing. The dropped tile settles into its slot, and on
// a cancel every tile slides back, unless `animated` is false (input stopped
// outside the close) or under Reduce Motion; the close drops it animated.
void Application::Impl::endModuleArrangeDrag(bool save, bool animated, double time) {
    if (!arrangeDrag) return;
    const auto drag = std::move(*arrangeDrag);
    arrangeDrag.reset();
    if (!drag.lifted) return;
    const bool animate = animated && !settings.reduceMotion;
    // The settle comes from the drop itself: saving the order already shown
    // changes nothing more in the list.
    session->setArrangeDrag(std::nullopt, std::nullopt, animate, time);
    const auto saved = configuration.moduleLayout();
    if (save && drag.preview.order != saved.order && settingsUI) {
        // The saved layout comes back through the applied record, which rebinds the list.
        isolate(settingsModuleOwner, "arrange", [&] {
            auto next = settingsUI->controller().committed();
            const auto layout = next.moduleLayout();
            next.setModuleLayout(ehud::data::ModuleLayout::normalized(drag.preview.order, layout.hidden));
            settingsUI->controller().update(std::move(next), hostTime(time));
        });
    } else bindNavigation(time, animate);
    if (settingsUI) isolate(settingsModuleOwner, "arrange", [&] { settingsUI->canvas(core::Module::display).previewModuleOrder(std::nullopt); });
    environment.pointerLocked = pointerLocked(); session->setEnvironment(environment, time);
}
} // namespace endfield::app
