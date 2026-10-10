#pragma once
#include "app/overlay_host.hpp"
#include "modules/charge_badge.hpp"
#include "native/charge_indicator_scene.hpp"
#include "native/layer_scene.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#ifdef _WIN32
namespace endfield::tools {
// The HUD's persistent charge control (Sources/HUDChargeBadge.swift): the
// charge-indicator canvas at scale 0.82 centred at (500,446) + the source
// shell's 38-point offset on the core plane. Entrance waits 0.58 s, holds the
// compact capsule for 3 s, then becomes the circle unless hovered; hover
// expands it, a click selects Power, and the exit follows the HUD close.
// Deadlines are explicit (nextWakeTime); no timer, provider or window.
class ChargeBadgePreview final {
public:
    static constexpr double sourceOffset = 38; // SystemHUDView.chargeSourceOffset (source shell)
    ChargeBadgePreview(native::LayerRasterizer&, std::function<void()> selectPower);
    ~ChargeBadgePreview();
    ChargeBadgePreview(const ChargeBadgePreview&) = delete;
    ChargeBadgePreview& operator=(const ChargeBadgePreview&) = delete;
    void resize(const app::ClientMetrics&);
    // Content events (battery reading, resolved dark/accent/language, metric).
    void setContent(const modules::BatteryReading&, const modules::ChargeIndicatorAppearance&,
        modules::ChargeMetric = modules::ChargeMetric::battery, const std::optional<modules::ChargeTelemetry>& = std::nullopt);
    void setReduceMotion(bool, double time);
    // HUD lifecycle. The caller passes a fresh seed per deployment (the
    // source draws UInt64.random for HUDDeploymentFlicker).
    void animateEntrance(double time, std::uint64_t flickerSeed);
    void setStable(bool visible, double time);
    void animateExit(double time, std::uint64_t flickerSeed);
    void cancel(double time);
    // center: HUD design (1000 x 640) -> world points, sharing the core
    // plane's tilt/deployment (ModuleFrame.center). opacity: shell opacity.
    modules::ChargeBadgeState::Events update(const core::Matrix4& center, float opacity, double time);
    bool requiresFrames(double time) const;
    std::optional<double> nextWakeTime() const;
    bool exitFinished() const noexcept;
    bool covers(core::Point) const;
    // Hover over the visible capsule expands it (and highlights Power); a
    // left click selects Power. Leave/capture loss clears hover.
    bool pointer(const app::PointerEvent&, double time);
    bool hovered() const noexcept;
    // Accessibility help of the Power navigation entry.
    std::string accessibilityLabel() const;
    // Selected metric needs the shared Activity sampler while visible.
    bool telemetryDemand() const noexcept;
    const modules::ChargeBadgeState& state() const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
