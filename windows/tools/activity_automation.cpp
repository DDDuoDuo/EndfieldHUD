#include "tools/activity_automation.hpp"
#ifdef _WIN32
#include "modules/activity_presentation.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace endfield::tools {
native::ScreenReaderPeerSource activityAutomation(ActivityPreview& activity, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Activity Monitor accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&activity] {
        std::vector<native::ScreenReaderPeer> out;
        constexpr core::Rect canvas{0, 0, 400, 334};
        if (!activity.clientRect(canvas)) return out; // not accepting input
        const auto actions = activity.actions();
        out.reserve(actions.size() + 1);
        out.push_back({"activity:status", modules::activityAccessibilityStatus(activity.state(), activity.language()), {}, {}, canvas, native::ScreenReaderRole::text, true});
        for (const auto& a : actions) out.push_back({a.id, a.label, {}, {}, a.rect, native::ScreenReaderRole::button, true});
        return out;
    };
    source.toScreen = [&activity, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, activity.clientMetrics().scale, activity.clientRect(canvas));
    };
    // canvas.perform(actionID:) takes only a listed action.
    source.invoke = [&activity, now = hooks.now](std::string_view id) {
        if (!activity.clientRect({0, 0, 400, 334})) return false;
        const auto actions = activity.actions();
        if (std::none_of(actions.begin(), actions.end(), [&](const auto& a) { return a.id == id; })) return false;
        return activity.perform(id, now());
    };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
