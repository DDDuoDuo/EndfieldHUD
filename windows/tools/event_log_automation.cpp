#include "tools/event_log_automation.hpp"
#ifdef _WIN32
#include <stdexcept>
#include <utility>

namespace endfield::tools {
native::ScreenReaderPeerSource eventLogAutomation(EventLogPreview& log, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Event Log accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&log] {
        std::vector<native::ScreenReaderPeer> out;
        if (!log.clientRect(modules::EventLogState::bounds())) return out; // not accepting input
        const auto& state = log.state();
        const auto help = state.status() + ". Arrow keys select events; Page Up and Page Down scroll.";
        const auto actions = state.actions();
        out.reserve(actions.size());
        for (const auto& a : actions) out.push_back({a.id, a.label, help, {}, a.rect, native::ScreenReaderRole::button, true});
        return out;
    };
    source.toScreen = [&log, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, log.clientMetrics().scale, log.clientRect(canvas));
    };
    source.invoke = [&log, now = hooks.now](std::string_view id) { return log.perform(id, now()); };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
