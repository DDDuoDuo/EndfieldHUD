#include "tools/calendar_automation.hpp"
#ifdef _WIN32
#include <stdexcept>
#include <utility>

namespace endfield::tools {
std::optional<modules::CalendarEditorField> calendarAutomationField(std::string_view id) noexcept {
    if (id == "field:title") return modules::CalendarEditorField::title;
    if (id == "field:date") return modules::CalendarEditorField::date;
    if (id == "field:details") return modules::CalendarEditorField::details;
    return std::nullopt;
}
native::ScreenReaderPeerSource calendarAutomation(CalendarPreview& calendar, native::ScreenReaderPeerHooks hooks) {
    if (!hooks.now) throw std::invalid_argument("Calendar accessibility needs the host clock");
    native::ScreenReaderPeerSource source;
    source.peers = [&calendar] {
        auto items = calendar.accessibility();
        std::vector<native::ScreenReaderPeer> out;
        out.reserve(items.size());
        const auto focused = calendar.focusedField();
        for (auto& e : items) {
            if (e.role == modules::CalendarAccessibleRole::textField) {
                const auto field = calendarAutomationField(e.id);
                std::optional<std::string> text;
                if (field) text = calendar.fieldText(*field);
                out.push_back({std::move(e.id), std::move(e.label), {}, text.value_or(std::string{}), e.rect, native::ScreenReaderRole::edit,
                               e.enabled && text.has_value(), field && focused == field});
            } else {
                out.push_back({std::move(e.id), std::move(e.label), {}, {}, e.rect, native::ScreenReaderRole::button, e.enabled});
            }
        }
        return out;
    };
    source.toScreen = [&calendar, window = hooks.window](core::Rect canvas) {
        return native::screenReaderClientToScreen(window, calendar.clientMetrics().scale, calendar.clientRect(canvas));
    };
    source.invoke = [&calendar, now = hooks.now](std::string_view id) { return calendar.perform(id, now()); };
    source.setText = [&calendar, now = hooks.now](std::string_view id, std::string_view text) {
        const auto field = calendarAutomationField(id);
        return field && calendar.setFieldText(*field, text, now());
    };
    source.focus = [&calendar, now = hooks.now](std::string_view id) {
        const auto field = calendarAutomationField(id);
        return field && calendar.focusField(*field, now());
    };
    source.changed = std::move(hooks.changed);
    return source;
}
} // namespace endfield::tools
#endif
