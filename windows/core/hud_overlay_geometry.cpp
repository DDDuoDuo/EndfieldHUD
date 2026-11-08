#include "core/hud_overlay_geometry.hpp"
#include <algorithm>

namespace endfield::core {
Point OverlayGeometry::anchor(Element element) noexcept {
    switch (element) {
    case Element::title: return {header.x, header.y};
    case Element::sanity: return {header.x + gaugeHeaderPosition.x, header.y + gaugeHeaderPosition.y};
    case Element::alerts: return {header.x - 8, header.y + 50};
    }
    return {header.x, header.y};
}
Rect OverlayGeometry::frame(Element element, const Placement& placement, OverlaySize natural) noexcept {
    const auto origin = anchor(element);
    const double width = natural.width * placement.size, height = natural.height * placement.size;
    const double x = origin.x + placement.x, y = origin.y + placement.y;
    return {element == Element::alerts ? x - width : x, y, width, height};
}
OverlayGeometry::Placement OverlayGeometry::clamped(Element element, const Placement& placement, OverlaySize natural) noexcept {
    auto result = placement.normalized();
    const auto drawn = frame(element, result, natural);
    const auto shift = [](double minimum, double length, double lower, double upper) {
        if (length > upper - lower) return (lower + upper - length) / 2 - minimum;
        return std::max(lower, std::min(minimum, upper - length)) - minimum;
    };
    result.x += shift(drawn.x, drawn.width, plane.x, plane.x + plane.width);
    result.y += shift(drawn.y, drawn.height, plane.y, plane.y + plane.height);
    return result;
}
Rect OverlayGeometry::grown(Rect r, double by) noexcept { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }
std::optional<OverlayGeometry::Element> OverlayGeometry::elementAt(const std::array<Rect, 3>& frames, Point p) noexcept {
    for (const auto element : hitOrder) {
        const auto r = grown(frames[index(element)], hitSlop);
        if (r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height) return element;
    }
    return std::nullopt;
}
} // namespace endfield::core
