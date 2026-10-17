#include "tools/map_automation.hpp"
#ifdef _WIN32
#include <stdexcept>
#include <utility>

namespace endfield::tools {
native::ScreenReaderPeerSource mapAutomation(MapPreview& map, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Map accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&map] {
        const auto& a = map.accessibility();
        std::vector<native::ScreenReaderPeer> out;
        if (a.status.empty()) return out; // not accepting input
        out.reserve(a.elements.size() + 1);
        out.push_back({"map:status", a.status, a.help, {}, a.bounds, native::ScreenReaderRole::text, true});
        for (const auto& e : a.elements) out.push_back({e.id, e.label, a.help, {}, e.rect, native::ScreenReaderRole::button, e.enabled});
        return out;
    };
    source.toScreen = [&map, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, map.clientMetrics().scale, map.clientRect(canvas));
    };
    source.invoke = [&map, now = hooks.now](std::string_view id) { return map.perform(id, now()); };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
