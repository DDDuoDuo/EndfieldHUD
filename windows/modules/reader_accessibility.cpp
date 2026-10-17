#include "modules/reader_accessibility.hpp"

namespace endfield::modules {
std::string readerAccessibilityText(const std::optional<std::string>& error, const std::optional<std::string>& summary, const ReaderStrings& strings) {
    if (error) return *error;
    if (summary) return *summary;
    return strings.empty;
}

std::vector<ReaderAccessible> readerCanvasAccessibility(std::span<const ReaderAction> actions, core::Language language) {
    std::vector<ReaderAccessible> out;
    out.reserve(actions.size());
    for (const auto& a : actions) {
        std::string label;
        if (a.id == "bookmark") label = core::localized("Toggle bookmark", "切换书签", language);
        else if (a.id == "zoomIn") label = core::localized("Zoom in", "放大", language);
        else if (a.id == "zoomOut") label = core::localized("Zoom out", "缩小", language);
        else if (a.id == "zoomReset") label = core::localized("Reset zoom", "重置缩放", language);
        else label = a.label;
        out.push_back({a.id, std::move(label), a.rect, a.enabled, a.selected});
    }
    return out;
}

std::vector<ReaderAccessible> readerMenuAccessibility(std::span<const ReaderAction> actions, core::Language language) {
    std::vector<ReaderAccessible> out;
    out.reserve(actions.size());
    for (const auto& a : actions)
        out.push_back({a.id, a.id == "close" ? core::localized("Close", "关闭", language) : a.label, a.rect, a.enabled, a.selected});
    return out;
}
} // namespace endfield::modules
