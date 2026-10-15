#pragma once
#include "app/overlay_host.hpp"
#include "modules/charge_badge.hpp"
#include "native/charge_indicator_scene.hpp"
#include "native/layer_scene.hpp"
#include <cstdint>
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
    // Client metrics and the HUD design scale at the core plane's rest pose
    // (DesktopChromeLayout.designScale: min(width / 1100, height / 740, 1.15)
    // x hudScale). Content event: the artwork is rasterized once for that
    // rest pose (designScale x 0.82 x the client DPI scale, quarter steps
    // within 1...4). The frames' deployment, tilt and parallax transforms
    // then scale it the way Core Animation scales layer contents, so no
    // opening, closing or hover frame re-rasterizes; only a size, DPI or HUD
    // scale change does.
    void resize(const app::ClientMetrics&, double designScale);
    // Device pixels per canvas point of the rasterized artwork, and how many
    // times a density change re-rasterized existing content.
    double rasterDensity() const noexcept;
    std::uint64_t densityChanges() const noexcept;
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
    // Launch prewarm (ModuleOwner::prewarm, the HUD still closed): places the
    // current pose (hidden: nothing composes) once on the first opening's
    // core plane, so the reveal's first placement is warm. Never shows,
    // starts motion or changes the badge state.
    void prewarm(const core::Matrix4& center, double time);
    // Installs pending artwork while the badge is still hidden (the launch
    // prewarm's upload), so its reveal 0.58 s into an opening uploads nothing.
    void preload(native::Renderer&);
    // CPU pixels the artwork still holds (none once installed) and whether
    // artwork waits for an upload.
    std::size_t retainedBytes() const noexcept;
    bool uploadPending() const noexcept;
    bool requiresFrames(double time) const;
    std::optional<double> nextWakeTime() const;
    bool exitFinished() const noexcept;
    bool covers(core::Point) const;
    // Hover over the visible capsule expands it (and highlights Power); a
    // left click selects Power. Leave/capture loss clears hover.
    bool pointer(const app::PointerEvent&, double time);
    bool hovered() const noexcept;
    // The source navigation entry under the pointer is Power (the host's
    // hovered-module query after it routed the pointer). SystemHUDView.
    // updateHover sets chargeBadge.setHovered(hoveredTarget == .module(.power))
    // (the target is the badge itself or that entry), and setHovered's
    // onHitRegionChanged runs refreshChargeHover, which re-reads the badge
    // geometry under the pointer. Net source behaviour, ported exactly: the
    // Power entry keeps an acquired hover (a move from the badge straight onto
    // the entry) but never acquires one. Once reported, a move that leaves the
    // hovered badge waits for this target instead of un-hovering at once (the
    // host's session resolves the entry only after the badge saw the move).
    // Call it before update(); true when the hover changed.
    bool setNavigationHovered(bool power, double time);
    // Logical client bounds of the displayed capsule hit rect (Mac
    // displayedChargeHitRect projected through the charge plane: the Power
    // navigation entry's accessibility frame); none while hidden, waiting for
    // the reveal or retracting, or before the first placed frame.
    std::optional<core::Rect> clientHitBounds() const;
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
