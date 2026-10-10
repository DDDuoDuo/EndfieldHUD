#pragma once
#include "modules/charge_indicator.hpp"
#include "native/layer_raster.hpp"
#include "native/renderer.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

// Native renderer for one ChargeIndicatorView canvas (300 x 84 points), shared
// by the HUD charge badge and the floating desktop alert. Content events
// rasterize the four fitted labels and the fixed vector leaves through the
// shared LayerRasterizer, and generate the capsule's antialiased nine-slice
// textures (plate + Core Animation shadow, 0.5/1.5-point borders, emblem).
// Frames change only numeric data: the morphing capsule is a nine-slice whose
// corners keep their source radius, so width/height/radius animate without
// re-rasterizing, stretching text or allocating. The progress ring is the
// stroked track with the renderer's angular clip plus two round caps.
namespace endfield::native {
struct ChargeIndicatorSceneContent {
    modules::ChargeIndicatorAppearance appearance;
    modules::BatteryReading battery;
    modules::ChargeMetric metric{modules::ChargeMetric::battery};
    std::optional<modules::ChargeTelemetry> telemetry;
    bool preview{};
    // Device pixels per canvas point at the current placement; rasters use
    // it clamped to the shared rasterizer's 1...4 range.
    double pixelsPerPoint{2};
    bool operator==(const ChargeIndicatorSceneContent&) const = default;
};
struct ChargeIndicatorPlacement {
    core::Matrix4 canvasToWorld;             // canvas points (top-left) -> caller world
    modules::ChargeIndicatorPose pose;
    modules::ChargeStage stage{modules::ChargeStage::hidden};
    bool embeddedBorder{};                   // badge: animated pose border; panel: model border
    bool hovered{};                          // embedded hover colours (badge only)
    float opacity{1};                        // canvas/module opacity, multiplied in
    std::span<const PlaneMask> clips;        // caller clips, at most six
};

#ifdef _WIN32
class ChargeIndicatorScene final {
public:
    static constexpr std::size_t drawCount = 58;
    // prefix makes every renderer/rasterizer ID unique per owner.
    ChargeIndicatorScene(LayerRasterizer&, std::string prefix);
    ~ChargeIndicatorScene();
    ChargeIndicatorScene(const ChargeIndicatorScene&) = delete;
    ChargeIndicatorScene& operator=(const ChargeIndicatorScene&) = delete;
    // Content event. Returns true when resources changed (upload before the
    // next present). Draw count and IDs never change after construction.
    bool setContent(const ChargeIndicatorSceneContent&);
    // Numeric frame update; no allocation, raster or resource work.
    void place(const ChargeIndicatorPlacement&);
    // Installs changed textures/meshes (content events only).
    void upload(Renderer&);
    bool uploadPending() const noexcept;
    std::span<const DrawObject> draws() const noexcept;
    // Removes this scene's renderer resources and rasterizer entries. The
    // caller must already have removed the draws from its published list.
    bool release(Renderer&) noexcept;
    const modules::ChargeIndicatorContent& content() const noexcept;
    const ChargeIndicatorSceneContent& contentKey() const noexcept;
    std::uint64_t contentRevision() const noexcept;
    // Last placed capsule in canvas points (embeddedBodyRect).
    core::Rect bodyRect() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
} // namespace endfield::native
