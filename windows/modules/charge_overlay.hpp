#pragma once
#include "modules/charge_indicator.hpp"
#include "modules/hud_design_controls.hpp"
#include "modules/power_policy.hpp"
#include <cstdint>
#include <optional>
#include <span>

// Portable port of OverlayController's charge-indicator presentation: the
// show/hide generations, replay rules, dismissal deadline (clamped 1...60 s,
// non-finite 5 s), the finite entrance/exit sequence, the bar's length and
// the bar editor (beginPositionEditing, the Size and Length drafts, the
// dragged draft, the strip beside the bar, confirm/discard). All deadlines
// are explicit values folded into the host's frame clock or one-shot wake.
// The native panel owns the window and the editor's strip
// (design::PlacementEditor and its drag); this state owns no timer, window or
// renderer.
namespace endfield::modules {
// ChargeBarLayout: what the bar editor commits. position is none when the bar
// was not dragged, so the placement (Top centre or the saved custom point)
// stays as it was; scale is the Size; length none keeps the classic bar.
struct ChargeBarLayout {
    std::optional<ChargePosition> position;
    double scale{1};
    std::optional<double> length;
    bool operator==(const ChargeBarLayout&) const = default;
};
// The editor's Length slider: whole percentages from the rounded-up share of
// the 200-point minimum at the draft Size up to 100, and the value it shows
// (snapped by range(), the slider's own HUDSlider.Range).
struct ChargeLengthSlider {
    double minimum{100}, value{100};
    design::Slider::Range range() const noexcept { return {minimum, 100, 1, {}}; }
    bool operator==(const ChargeLengthSlider&) const = default;
};
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
    bool reduceMotion() const noexcept { return timeline_.reduceMotion(); }
    void setSettings(const ChargeAlertSettings&);

    // Runs due sequence steps and the dismissal deadline at their exact times.
    Events advance(double time);
    // The presentation (sequence and dismissal) moves `seconds` later, as if
    // shown that much later (ChargeIndicatorTimeline::postpone). The native
    // panel calls it once per presentation with the time its first frame
    // took (content raster, and the window and device it may have had to
    // create), so the entrance plays from its first frame once it shows.
    void postpone(double seconds) noexcept;
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
    // OverlayController.layout: the bar's canvas centred on the anchor, cut
    // at the display's sides, plus the editor's strip while editing (held
    // still while one of its sliders has the pointer).
    std::optional<ChargePanelLayout> layout() const;
    // layout(), and the timeline takes its bar width (ChargeIndicatorView.
    // setBarWidth, redrawn in place). The panel calls it when it positions.
    std::optional<ChargePanelLayout> place(double time);
    // The settings the bar draws with: the editor's Size and Length drafts
    // while editing, the saved ones otherwise (normalized).
    ChargeAlertSettings barSettings() const;

    // The bar editor. The host closes the HUD/projection first; begin
    // presents the demo or current reading compact, persistent and editable,
    // with the saved Size and Length as drafts. Nothing is saved until
    // Confirm; a settings change meanwhile does not overwrite the drafts.
    bool beginPositionEditing(const BatteryReading& previewValue, double time);
    // The strip's size in strip points (design::PlacementEditor::size; none
    // without a strip). It is drawn at chargeEditorStripScale.
    void setEditorStrip(std::optional<design::Size> stripPoints) noexcept;
    // A strip slider holds the pointer: the strip stays where it is (also
    // when Size moves the bar) until it is released, then settles.
    void setStripAdjusting(bool) noexcept;
    bool stripAdjusting() const noexcept { return adjusting_; }
    // The Size slider (a fraction) and the Length slider (its percentage).
    // A Length report equal to the value the slider showed changes nothing
    // (chargeDraftLength) and returns false.
    void setDraftScale(double scale);
    bool setDraftLength(double percent);
    // The Length slider's range and value for the drafts on the draft display
    // (recomputed when the Size or the display changes).
    std::optional<ChargeLengthSlider> lengthSlider() const noexcept { return lengthSlider_; }
    // A drag (design::PlacementDrag's proposed anchor, pixels) with the pointer
    // at `mouse`: the draft moves to the display under the pointer, clamped
    // for the drafts' Size and Length. True when the display changed (the
    // Length slider then has a new range).
    bool moveDraft(core::Point proposed, core::Point mouse) noexcept;
    bool draftMoved() const noexcept { return moved_; }
    // Confirm returns the bar layout to persist; discard returns none.
    std::optional<ChargeBarLayout> confirmPosition(double time);
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
    std::optional<core::Point> draftAnchor_;
    std::optional<std::uint32_t> draftScreen_;
    double draftScale_{1};
    std::optional<double> draftLength_;
    bool moved_{}, adjusting_{};
    std::optional<ChargeLengthSlider> lengthSlider_;
    std::optional<design::Size> stripPoints_;
    std::optional<core::Rect> heldStrip_;
    void scheduleDismissal(std::uint64_t token, double time);
    void finishEditing(double time);
    // OverlayController.reposition while editing: the draft where Confirm
    // puts it (chargeEditorAnchor) on its display, and the Length slider.
    void syncDraft() noexcept;
    void syncLengthSlider(const ChargeWorkArea&) noexcept;
};
} // namespace endfield::modules
