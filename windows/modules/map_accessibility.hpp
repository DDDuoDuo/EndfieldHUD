#pragma once
// Source accessibility contract of the Map for a native UI Automation
// provider (HUDWorldMapInteraction.layoutAccessibility over
// WorldMapCanvas.accessibilityStatus/accessibleActions): one status element
// for the whole 440x440 canvas, then one button per accessible action in
// source order (zoom in, zoom out, reset, place pin, remove the selected pin,
// then every visible pin not covered by a control). Rects are canvas points;
// the host projects them like pointer hit tests and invokes a button through
// MapState::perform. tools/map_accessibility_reference.py exports the oracle.
#include "modules/map_state.hpp"
#include "core/localization.hpp"
#include <optional>
#include <string>
#include <vector>

namespace endfield::modules {
struct MapAccessible {
    std::string id, label; // source ids: map:zoomIn ... map:pin:<UUID>
    core::Rect rect;
    bool enabled{};
    bool operator==(const MapAccessible&) const = default;
};
struct MapAccessibility {
    std::string status, help; // help is shared by the status and every button
    core::Rect bounds{0, 0, 440, 440};
    std::vector<MapAccessible> elements;
    bool operator==(const MapAccessibility&) const = default;
};
// `message` is the canvas's visible failure text (WorldMapCanvas.message).
MapAccessibility mapAccessibility(const MapState&, core::Language, const std::optional<std::string>& message = {});
// Source pin title (WorldMapPinArtwork.title(for:)).
std::string mapPinTitle(MapPinStyle, core::Language);

// Source requestAccessibilityLayout: while a gesture locks input, proxies
// follow at most 12 times per second (the visual camera stays immediate);
// otherwise, and when the gesture ends, they update at once. The owner asks
// on every camera/content change and wakes the shared timer at the returned
// deadline; no timer of its own.
class MapAccessibilityCadence {
public:
    static constexpr double interval = 1.0 / 12;
    // True: publish now. False: one publication is pending at deadline().
    bool request(double time, bool gestureLocked) noexcept;
    std::optional<double> deadline() const noexcept { return pending_; }
    // The pending publication is due (or the gesture ended): publish now.
    bool due(double time, bool gestureLocked) noexcept;
    void published(double time) noexcept { last_ = time; pending_.reset(); }
    void cancel() noexcept { pending_.reset(); }
private:
    std::optional<double> last_, pending_;
};
} // namespace endfield::modules
