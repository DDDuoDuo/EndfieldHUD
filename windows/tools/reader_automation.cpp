#include "tools/reader_automation.hpp"
#ifdef _WIN32
#include <stdexcept>
#include <utility>

namespace endfield::tools {
namespace {
constexpr std::string_view menuPrefix = "menu:";
}
native::ScreenReaderPeerSource readerAutomation(ReaderPreview& reader, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Reader accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&reader] {
        auto a = reader.accessibility();
        std::vector<native::ScreenReaderPeer> out;
        out.reserve(a.canvas.size() + a.menu.size());
        for (auto& e : a.canvas) out.push_back({std::move(e.id), std::move(e.label), a.help, {}, e.rect, native::ScreenReaderRole::button, e.enabled});
        for (auto& e : a.menu) out.push_back({std::string(menuPrefix) + e.id, std::move(e.label), {}, {}, e.rect, native::ScreenReaderRole::button, e.enabled});
        return out;
    };
    source.toScreen = [&reader, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, reader.clientMetrics().scale, reader.clientRect(canvas));
    };
    source.invoke = [&reader, now = hooks.now](std::string_view id) {
        if (id.starts_with(menuPrefix)) return reader.performMenu(id.substr(menuPrefix.size()), now());
        return reader.perform(id, now());
    };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
