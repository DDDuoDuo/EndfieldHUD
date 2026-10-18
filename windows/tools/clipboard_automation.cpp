#include "tools/clipboard_automation.hpp"
#ifdef _WIN32
#include "core/localization.hpp"
#include <stdexcept>
#include <utility>

namespace endfield::tools {
native::ScreenReaderPeerSource clipboardAutomation(ClipboardPreview& clipboard, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Clipboard accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&clipboard] {
        std::vector<native::ScreenReaderPeer> out;
        if (!clipboard.clientRect(native::ClipboardState::bounds())) return out; // not accepting input
        const auto& state = clipboard.state();
        const auto help = state.status() + core::localized(". Arrow keys select; Return copies; Delete removes the cached item.",
            "。方向键选择，回车复制，Delete 删除缓存项目。", clipboard.language());
        const auto actions = state.actions();
        out.reserve(actions.size());
        for (const auto& a : actions) out.push_back({a.id, a.label, help, {}, a.rect, native::ScreenReaderRole::button, true});
        return out;
    };
    source.toScreen = [&clipboard, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, clipboard.clientMetrics().scale, clipboard.clientRect(canvas));
    };
    source.invoke = [&clipboard, now = hooks.now](std::string_view id) { return clipboard.perform(id, now()); };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
