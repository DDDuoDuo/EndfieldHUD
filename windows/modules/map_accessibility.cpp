#include "modules/map_accessibility.hpp"

namespace endfield::modules {
std::string mapPinTitle(MapPinStyle style, core::Language language) {
    switch (style) {
    case MapPinStyle::green: return core::localized("Green pin", "绿色标记", language);
    case MapPinStyle::player: return core::localized("Player marker", "玩家标记", language);
    case MapPinStyle::yellow: break;
    }
    return core::localized("Yellow pin", "黄色标记", language);
}

MapAccessibility mapAccessibility(const MapState& state, core::Language language, const std::optional<std::string>& message) {
    MapAccessibility out;
    out.status = message ? *message : core::localized("Map", "地图", language);
    out.help = out.status + core::localized(
        ". Drag to pan. Scroll or pinch to zoom. Right-click to place or remove a pin. Click a pin to cycle yellow, green and player markers. Click terrain to recenter without changing zoom. Arrow keys move the map.",
        "。拖动平移，滚动或捏合缩放，右键放置或移除标记，点击标记切换黄色、绿色和玩家样式，点击地形居中并保持缩放，方向键移动地图。", language);
    const auto actions = state.actions();
    const auto pins = state.pins();
    out.elements.reserve(actions.count);
    for (std::size_t n = 0; n < actions.count; ++n) {
        const auto& hit = actions.items[n];
        MapAccessible element{{}, {}, hit.rect, hit.enabled};
        switch (hit.action) {
        case MapAction::zoomIn: element.id = "map:zoomIn"; element.label = core::localized("Zoom in", "放大", language); break;
        case MapAction::zoomOut: element.id = "map:zoomOut"; element.label = core::localized("Zoom out", "缩小", language); break;
        case MapAction::reset: element.id = "map:reset"; element.label = core::localized("Reset zoom to 3×", "重置缩放至3×", language); break;
        case MapAction::addPin: element.id = "map:addPin"; element.label = core::localized("Place pin at map center", "在地图中心放置标记", language); break;
        case MapAction::deletePin: element.id = "map:deletePin"; element.label = core::localized("Remove selected pin", "移除选中标记", language); break;
        case MapAction::pin: {
            // Source pinLabels: "<style title> <1-based index>, <coordinates>".
            element.id = "map:pin:" + std::string(hit.pinID);
            for (std::size_t index = 0; index < pins.size(); ++index)
                if (pins[index].id == hit.pinID)
                    element.label = mapPinTitle(pins[index].style, language) + " " + std::to_string(index + 1) + ", " +
                                    mapCoordinateDescription(pins[index].x, pins[index].y);
            break;
        }
        }
        out.elements.push_back(std::move(element));
    }
    return out;
}

bool MapAccessibilityCadence::request(double time, bool gestureLocked) noexcept {
    if (!gestureLocked || !last_ || time - *last_ >= interval) return true;
    if (!pending_) pending_ = *last_ + interval;
    return false;
}

bool MapAccessibilityCadence::due(double time, bool gestureLocked) noexcept {
    return pending_ && (time >= *pending_ || !gestureLocked);
}
} // namespace endfield::modules
