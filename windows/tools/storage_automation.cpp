#include "tools/storage_automation.hpp"
#ifdef _WIN32
#include <stdexcept>
#include <utility>

namespace endfield::tools {
native::ScreenReaderPeerSource storageAutomation(StoragePreview& storage, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Storage accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&storage] {
        std::vector<native::ScreenReaderPeer> out;
        constexpr auto canvas = modules::StoragePresentation::bounds();
        if (!storage.clientRect(canvas)) return out; // not accepting input
        const auto actions = storage.accessibleActions();
        out.reserve(actions.size() + 1);
        out.push_back({"storage:status", storage.presentation().labels().status, {}, {}, canvas, native::ScreenReaderRole::text, true});
        for (const auto& a : actions) out.push_back({a.id, a.label, {}, {}, a.rect, native::ScreenReaderRole::button, true});
        return out;
    };
    source.toScreen = [&storage, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, storage.clientMetrics().scale, storage.clientRect(canvas));
    };
    source.invoke = [&storage, now = hooks.now](std::string_view id) { return storage.perform(id, now()); };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
