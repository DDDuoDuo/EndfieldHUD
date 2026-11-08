// Display > HUD elements geometry (Mac HUDOverlayGeometry and
// SystemHUDView.overlayElement(at:), Sources/HUDOverlayLayout.swift and
// Sources/SystemHUDView.swift at 2c047e0): the authored anchors, the frame of
// a placement, the clamp onto the 1000 x 640 core plane (centred when larger
// than the plane) and the editor's hit order. Expected values follow the Mac
// formulas by hand; no files are read.
#include "core/hud_overlay_geometry.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using endfield::core::OverlayGeometry;
using endfield::core::OverlaySize;
using endfield::core::Point;
using endfield::core::Rect;
using Element = OverlayGeometry::Element;
using Placement = OverlayGeometry::Placement;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
bool closeTo(double a, double b) { return std::abs(a - b) < 1e-9; }
bool same(Rect a, Rect b) { return closeTo(a.x, b.x) && closeTo(a.y, b.y) && closeTo(a.width, b.width) && closeTo(a.height, b.height); }
bool same(const Placement& a, const Placement& b) { return closeTo(a.x, b.x) && closeTo(a.y, b.y) && closeTo(a.size, b.size); }

void anchors() {
    check(OverlayGeometry::anchor(Element::title) == Point{270, 2}, "The title's top-left is the header's origin");
    check(OverlayGeometry::anchor(Element::sanity) == Point{562, 2}, "The gauge sits at header + (292, 0)");
    check(OverlayGeometry::anchor(Element::alerts) == Point{262, 52}, "The alert column's top-right is (header.minX - 8, header.minY + 50)");
    check(OverlayGeometry::gauge == OverlaySize{168, 42}, "The gauge's natural size is HUDAccountGauge.size");
}
void frames() {
    check(same(OverlayGeometry::frame(Element::title, {10, 20, 1.5}, {200, 49}), {280, 22, 300, 73.5}), "Frame = anchor + offset, size = natural x size");
    check(same(OverlayGeometry::frame(Element::alerts, {}, {120, 30}), {142, 52, 120, 30}), "The alerts grow left from their top-right anchor");
    check(same(OverlayGeometry::frame(Element::sanity, {-2, 4, .5}, OverlayGeometry::gauge), {560, 6, 84, 21}), "The gauge's frame at half size");
}
void clamps() {
    // Off the right and the top edges: pulled back onto the plane.
    check(same(OverlayGeometry::clamped(Element::title, {800, -100, 1}, {200, 49}), {530, -2, 1}), "A frame past the plane's right and top edges is pulled back");
    check(same(OverlayGeometry::drawnFrame(Element::title, {800, -100, 1}, {200, 49}), {800, 0, 200, 49}), "The drawn frame touches the plane's edges");
    // Larger than the plane on both axes: centred on it.
    check(same(OverlayGeometry::clamped(Element::alerts, {0, 0, 2}, {600, 400}), {838, -132, 2}), "An element larger than the plane is centred on it");
    check(same(OverlayGeometry::drawnFrame(Element::alerts, {0, 0, 2}, {600, 400}), {-100, -80, 1200, 800}), "The centred frame overhangs both sides equally");
    // On the plane: unchanged; the placement is normalized first.
    check(same(OverlayGeometry::clamped(Element::sanity, {5, 6, 1}, OverlayGeometry::gauge), {5, 6, 1}), "A placement on the plane is kept");
    check(same(OverlayGeometry::clamped(Element::sanity, {std::numeric_limits<double>::quiet_NaN(), 0, 9}, OverlayGeometry::gauge), {0, 0, 2}),
          "Clamping normalizes the placement first (non-finite offsets 0, size at most 2)");
    // A wide gauge near the right edge (size 2): 336 wide from x 562 + 200 ends past 1000.
    check(same(OverlayGeometry::clamped(Element::sanity, {200, 0, 2}, OverlayGeometry::gauge), {102, 0, 2}), "A doubled gauge stays on the plane");
}
void hits() {
    const std::array<Rect, 3> frames{Rect{270, 2, 300, 49}, Rect{300, 10, 168, 42}, Rect{142, 52, 120, 30}};
    check(OverlayGeometry::elementAt(frames, {350, 20}) == Element::sanity, "The gauge is hit first where it overlaps the title");
    check(OverlayGeometry::elementAt(frames, {280, 5}) == Element::title, "The title is hit outside the gauge");
    check(OverlayGeometry::elementAt(frames, {265, 2}) == Element::title, "The hit frame grows by 6 (the title's left edge)");
    check(OverlayGeometry::elementAt(frames, {267, 87}) == Element::alerts, "The hit frame grows by 6 (the alerts' bottom-right)");
    check(!OverlayGeometry::elementAt(frames, {576, 20}), "The grown right edge is outside (CGRect.contains)");
    check(!OverlayGeometry::elementAt(frames, {900, 500}), "Nothing is hit away from the elements");
    check(OverlayGeometry::hitOrder == std::array<Element, 3>{Element::sanity, Element::title, Element::alerts}, "Hit order: gauge, title, alerts");
}
} // namespace

int main() {
    try {
        anchors();
        frames();
        clamps();
        hits();
        std::cout << checks << " HUD elements geometry checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HUD elements geometry failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
