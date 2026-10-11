#include "tools/orbipom_automation.hpp"
#ifdef _WIN32
#include "modules/orbipom_accessibility.hpp"
#include <stdexcept>
#include <utility>

namespace endfield::tools {
native::ScreenReaderPeerSource orbiPomAutomation(OrbiPomPreview& game, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Minigame accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&game] {
        std::vector<native::ScreenReaderPeer> out;
        if (!game.clientRect({0, 0, 440, 440})) return out; // not accepting input
        auto a = modules::orbiPomAccessibility(game.session(), game.state(), game.language());
        out.reserve(a.buttons.size() + 2);
        if (a.rules) {
            const auto menu = game.rulesBounds();
            out.push_back({"rules:menu", std::move(a.rulesLabel), {}, std::move(a.rulesValue), menu, native::ScreenReaderRole::group, true});
            out.push_back({"rules:close", std::move(a.rulesCloseLabel), {}, {},
                           {menu.x + a.rulesClose.x, menu.y + a.rulesClose.y, a.rulesClose.width, a.rulesClose.height}, native::ScreenReaderRole::button, true});
        }
        for (auto& b : a.buttons) out.push_back({std::move(b.id), std::move(b.label), {}, {}, b.rect, native::ScreenReaderRole::button, b.enabled});
        return out;
    };
    source.toScreen = [&game, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, game.clientMetrics().scale, game.clientRect(canvas));
    };
    // OrbiPomAXButton.accessibilityPerformPress: enabled and not hidden (the
    // canvas buttons hide while the rules are open), then canvas.perform.
    source.invoke = [&game, now = hooks.now](std::string_view id) {
        if (id == "rules:close") return game.closeRules(now());
        if (game.state().rulesPresented()) return false;
        const auto actions = game.state().actions();
        for (std::size_t n = 0; n < actions.count; ++n) {
            const auto& hit = actions.items[n];
            if (modules::orbiPomActionID(hit.action) == id) return hit.enabled && game.perform(hit.action, now());
        }
        return false;
    };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
