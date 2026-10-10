#pragma once
#include "modules/charge_indicator.hpp"
#include "modules/power_policy.hpp"
#include <cstdint>
#include <optional>
#include <span>

// Portable port of OverlayController's charge-indicator presentation: the
// show/hide generations, replay rules, dismissal deadline (clamped 1...60 s,
// non-finite 5 s), the finite entrance/exit sequence, and position editing
// (beginPositionEditing/dragPosition/confirm/discard). All deadlines are
// explicit values folded into the host's frame clock or one-shot wake. The
// native panel owns the window; this state owns no timer, window or renderer.
namespace endfield::modules {
class ChargeOverlayState final : public ChargeAlertPresenter {
public:
    struct Events {
        bool windowShown{}, windowHidden{}, presentationCompleted{}, dismissed{};
        bool any() const noexcept { return windowShown || windowHidden || presentationCompleted || dismissed; }
    };
    ChargeOverlayState();

    // ChargeAlertPresenter (driven by PowerAlertCoordinator).
    bool visible() const override { return visible_; }
    bool persistent() const override { return persistent_; }
    bool editingPosition() const override { return editing_; }
    bool systemOverlayActive() const override { return systemOverlay_; }
    bool projectionActive() const override { return projection_; }
    void update(const BatteryReading&, bool preview, double time) override;
    // Explicit previews replay the entrance even when already visible.
    void show(bool persistent, double duration, bool replay, double time) override;
    void hide(bool animated, double time) override;

    // Host state. Opening the HUD first calls hide(false) (toggleSystemOverlay).
    void setSystemOverlayActive(bool value) noexcept { systemOverlay_ = value; }
    void setProjectionActive(bool value) noexcept { projection_ = value; }
    void setReduceMotion(bool) noexcept;
    void setSettings(const ChargeAlertSettings&);

    // Runs due sequence steps and the dismissal deadline at their exact times.
    Events advance(double time);
    std::optional<double> nextDeadline() const noexcept;
    bool requiresFrames(double time) const noexcept;
    ChargeIndicatorPose sample(double time) const noexcept { return timeline_.sample(time); }
    ChargeStage stage() const noexcept { return timeline_.stage(); }
    // orderFrontRegardless/orderOut state of the native panel.
    bool windowVisible() const noexcept { return window_; }
    // Shared-panel telemetry demand for a non-battery metric (Activity sampler).
    bool telemetryDemand() const noexcept { return window_ && chargeMetricRequiresTelemetry(settings_.metric); }

    const BatteryReading& reading() const noexcept { return reading_; }
    bool previewContent() const noexcept { return preview_; }
    const ChargeAlertSettings& settings() const noexcept { return settings_; }
    // Changes with the reading, preview flag or alert settings (content event).
    std::uint64_t contentRevision() const noexcept { return contentRevision_; }
    std::uint64_t generation() const noexcept { return generation_; }
    double requestedDuration() const noexcept { return requestedDuration_; }

    // Placement in virtual-desktop pixels. Screens come from the host's
    // monitor enumeration (work areas); preferred is the internal/primary one.
    void setScreens(std::span<const ChargeWorkArea>, std::optional<std::size_t> preferred);
    std::optional<ChargeWorkArea> selectedScreen() const;
    std::optional<core::Point> anchor() const;
    std::optional<ChargePanelLayout> layout() const;

    // Position editing. The host closes the HUD/projection first; begin
    // presents the demo or current reading compact, persistent and editable.
    bool beginPositionEditing(const BatteryReading& previewValue, double time);
    void beginDrag(core::Point mouse) noexcept;
    bool drag(core::Point mouse) noexcept; // true when the anchor moved
    void endDrag() noexcept { dragOrigin_.reset(); dragMouse_.reset(); }
    // Confirm returns the normalized position to persist; discard returns none.
    std::optional<ChargePosition> confirmPosition(double time);
    void discardPosition(double time);
    std::optional<core::Point> draftAnchor() const noexcept { return draftAnchor_; }
private:
    enum class Pending : std::uint8_t { none, entrance, exit };
    ChargeIndicatorTimeline timeline_;
    ChargeAlertSettings settings_;
    BatteryReading reading_;
    bool preview_{}, visible_{}, persistent_{}, editing_{}, completed_{}, window_{};
    bool systemOverlay_{}, projection_{};
    std::uint64_t generation_{}, contentRevision_{1}, pendingToken_{};
    Pending pending_{Pending::none};
    double requestedDuration_{3};
    std::optional<double> dismissalAt_;
    std::uint64_t dismissalToken_{};
    std::vector<ChargeWorkArea> screens_;
    std::optional<std::size_t> preferred_;
    std::optional<core::Point> draftAnchor_, dragOrigin_, dragMouse_;
    std::optional<std::uint32_t> draftScreen_;
    void scheduleDismissal(std::uint64_t token, double time);
    void finishEditing(double time);
};
} // namespace endfield::modules
