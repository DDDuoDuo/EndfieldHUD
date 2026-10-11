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
        if (!session->inputEnabled() || peer.slot >= navigationSlots.size()) return false;
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
    if (!session || !ready || stopping || peer.slot >= navigationSlots.size()) return false;
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
    for (const auto& id : slot.ids)
        if (const auto activation = session->activate(id, time)) { activate(*activation); return true; }
    return false;
}
} // namespace endfield::app
