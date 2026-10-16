#pragma once
// Desktop Map occlusion (Mac HUDSourceWatchView.desktopMapOcclusionEnabled,
// SystemHUDView.applySourceMapOcclusion / updateMapOcclusionPresentation).
//
// The HUD composes every module canvas above the source shell, as the Mac
// stacks its module layers above HUDSourceWatchView. The Map is the one
// module drawn larger than its plane (1.14x), so its circle reaches over the
// tops of the two navigation buttons below the circle, Storage
// (TechtreeBtn/Icon, sprite watchfactory_bg) and Activity Monitor
// (ReportBtn/Icon, watchfactory_r_bg). While the Map is presented the Mac
// keeps those plates ABOVE it:
//   * drawing: the module content is masked by the even-odd path of its
//     bounds and the plates' projected sprite-mesh boundaries, so the Map
//     disappears exactly under each plate (its transparent corners keep the
//     Map);
//   * input: a press inside a plate belongs to the button
//     (HUDSourceWatchView.mapFilteredButton), so no Map drag, pin or right
//     click starts there (SystemHUDView.mouseDown/rightMouseDown/magnify
//     navigationTargetAtDesignPoint), and the plates' transparent corners
//     stay the Map's. The wheel still zooms the Map (scrollWheel).
// The plates are HUDSourceWatchView.makeDesktopBottomSilhouettes: the
// boundary loops of each Icon's sprite mesh, normalized to its
// RectTransform. The Windows runtime package keeps no sprite meshes, so the
// loops are embedded here (tools/map_cutout_reference.py exports them from
// the pinned source; tests/fixtures/map_cutout_source.json).
//
// Everything here is allocation-free and pure: no clock, IO or OS call.
#include "core/source_camera.hpp"
#include "core/source_layout.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace endfield::modules {
// One closed polygon (no repeated closing vertex) in fixed storage.
struct MapCutoutPolygon {
    static constexpr std::size_t capacity = 24;
    std::array<core::Point, capacity> points{};
    std::size_t count{};
    std::span<const core::Point> view() const noexcept { return {points.data(), count}; }
    bool operator==(const MapCutoutPolygon&) const noexcept;
};
// The plates' polygons, either in logical client points (projected) or in
// the Map's 440-point canvas (for its mask).
struct MapCutouts {
    static constexpr std::size_t capacity = 8;
    std::array<MapCutoutPolygon, capacity> polygons{};
    std::size_t count{};
    bool empty() const noexcept { return count == 0; }
    bool contains(core::Point) const noexcept; // inside any polygon (the plate hit paths)
    bool operator==(const MapCutouts&) const noexcept;
};
// A source plate: the node whose path ends with `nodeSuffix` and its sprite
// mesh's boundary loops in its RectTransform's normalized coordinates
// (0...1 from the rect origin, Unity y up).
struct MapSourceSilhouette {
    std::string_view nodeSuffix, module, sprite;
    std::span<const MapCutoutPolygon> loops;
};
std::span<const MapSourceSilhouette> mapSourceSilhouettes() noexcept;

// The presented source frame (WatchSessionFrame: its resolved nodes, their
// inherited alpha and the camera it was drawn with).
struct MapSourceView {
    std::span<const core::source::ResolvedNode> resolved;
    std::span<const double> inheritedAlpha;
    const core::source::CameraFrame* camera{};
};
// HUDSourceWatchView.updateDesktopBottomSilhouettes: each plate whose node is
// active, has a rect and an inherited alpha above .001 is projected through
// the frame's camera (worldRoot * node world, projection * view) into
// logical client points; a loop with any unprojectable vertex is dropped.
// An unchanged key (visible nodes, their worlds and rects, the camera and the
// viewport) skips the projection (DesktopBottomProjectionKey). The nodes are
// found once by path suffix and kept by identity. No allocation.
class MapSourceCutoutPlan final {
public:
    bool update(const MapSourceView&, core::Rect viewport); // true when the plates changed
    bool clear() noexcept;                                    // true when plates were shown
    const MapCutouts& cutouts() const noexcept { return cutouts_; }
    bool contains(core::Point p) const noexcept { return cutouts_.contains(p); }
private:
    struct Slot { const core::source::Node* node{}; std::size_t index{}; };
    struct Key {
        std::array<bool, 2> visible{};
        std::array<core::Matrix4, 2> world{};
        std::array<core::source::SourceRect, 2> rect{};
        core::Matrix4 viewProjection;
        core::Rect viewport;
        bool operator==(const Key&) const = default;
    };
    std::array<Slot, 2> slots_{};
    // The resolved span last searched: a node missing from it is not searched
    // for again until the frame's node list changes.
    const core::source::ResolvedNode* searched_{};
    std::size_t searchedSize_{};
    std::optional<Key> key_;
    MapCutouts cutouts_;
};

// Texel rectangle of a mask texture.
struct MapCutoutTexels {
    std::uint32_t x{}, y{}, width{}, height{};
    bool empty() const noexcept { return !width || !height; }
    bool operator==(const MapCutoutTexels&) const = default;
};
// Texels a mask of `width` x `height` (texelsPerPoint of the Map canvas)
// changes under the cutouts' polygons, padded by one texel for edge
// coverage and clamped to the texture; empty when none fall on it.
MapCutoutTexels mapCutoutTexels(const MapCutouts& canvas, double texelsPerPoint, std::uint32_t width, std::uint32_t height) noexcept;
MapCutoutTexels mapCutoutUnion(MapCutoutTexels, MapCutoutTexels) noexcept;
// The masked texels of `region`: the base mask's straight RGBA (RGB kept)
// with alpha * (1 - even-odd coverage of the cutouts), the coverage sampled
// on four sub-rows per texel with exact horizontal span coverage. `out`
// holds region.width * region.height * 4 bytes; `row` region.width floats.
// No allocation.
void mapCutoutMask(const MapCutouts& canvas, double texelsPerPoint, std::span<const std::uint8_t> baseRGBA,
    std::uint32_t baseWidth, std::uint32_t baseHeight, MapCutoutTexels region, std::span<std::uint8_t> out, std::span<float> row);
} // namespace endfield::modules
