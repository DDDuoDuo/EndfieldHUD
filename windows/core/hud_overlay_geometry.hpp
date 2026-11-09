#pragma once
// Mac HUDOverlayGeometry (Sources/HUDOverlayLayout.swift): where each of the
// HUD's own overlays (Display > HUD elements: the title, the Sanity gauge and
// the facility alert tags) sits on the core plane, the 1000 x 640 design
// plane with y down: its authored anchor, its frame for a placement and the
// placement pulled back so the element stays on the plane. The host, the
// account area (the gauge) and the facility area (the tags) share it.
#include "core/data/data_store.hpp"
#include "core/scene.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string_view>

namespace endfield::core {
struct OverlaySize {
    double width{}, height{};
    bool operator==(const OverlaySize&) const = default;
};
struct OverlayGeometry {
    using Element = ehud::data::OverlayLayout::Element;
    using Placement = ehud::data::OverlayLayout::Placement;
    static constexpr Rect plane{0, 0, 1000, 640};
    // The header group holding the title, the Sanity gauge and the legacy
    // clock (core::source::DesktopChromeLayout::header).
    static constexpr Rect header{270, 2, 600, 74};
    // HUDAccountGauge.size and headerPosition (the gauge's place in the header).
    static constexpr OverlaySize gauge{168, 42};
    static constexpr Point gaugeHeaderPosition{292, 0};
    // The title's natural height (the title line 27 and the subtitle 18 at +31).
    static constexpr double titleHeight = 49;
    // The editor's hit and mark frames grow each element by this much.
    static constexpr double hitSlop = 6;
    static constexpr std::array<Element, 3> elements{Element::title, Element::sanity, Element::alerts};
    // overlayElement(at:): the gauge first (the smallest), then the title, then the alerts.
    static constexpr std::array<Element, 3> hitOrder{Element::sanity, Element::title, Element::alerts};
    // The title's and the gauge's top-left corners, and the alert column's
    // top-right corner (it grows down and to the left from it).
    static Point anchor(Element) noexcept;
    // The element's frame for `placement`, where `natural` is its size at 100 %.
    static Rect frame(Element, const Placement&, OverlaySize natural) noexcept;
    // `placement` (normalized) with its offset pulled in so the frame stays on
    // the plane (centred on it when the element is larger than the plane).
    static Placement clamped(Element, const Placement&, OverlaySize natural) noexcept;
    // The element as drawn: its clamped placement's frame.
    static Rect drawnFrame(Element element, const Placement& placement, OverlaySize natural) noexcept {
        return frame(element, clamped(element, placement, natural), natural);
    }
    // `frame` grown by `by` on every side (CGRect.insetBy(-by, -by)).
    static Rect grown(Rect, double by) noexcept;
    // The first element in hitOrder whose frame (grown by hitSlop) holds
    // `point` (CGRect.contains: the right and bottom edges are outside).
    // `frames` are the drawn frames in `elements` order.
    static std::optional<Element> elementAt(const std::array<Rect, 3>& frames, Point) noexcept;
    static std::size_t index(Element element) noexcept { return static_cast<std::size_t>(element); }
};
// The alert column's natural size right now (overlayNatural(.alerts): one
// tag's slot while none is shown), published by the facility area under
// overlayAlertsKey (ApplicationHost::publish) for the host's placement editor.
struct OverlayAlertsSource {
    std::function<OverlaySize()> natural;
};
inline constexpr std::string_view overlayAlertsKey{"hud.overlay.alerts"};
} // namespace endfield::core
