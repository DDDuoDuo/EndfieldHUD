#pragma once
// The facility alert banner and its shared pieces: the port of
// Sources/HUDFacilityBanner.swift (HUDTimers, HUDFacilityControl,
// HUDHazardStripes, HUDFacilityBanner). SHELL-70, SHELL-71.
//
// Built on the design library (hud_design_layer / hud_design_control): one
// retained layer tree, motion as numbers. HUDTimers' asyncAfter work items
// are Environment::deadlines entries (the shared design::Context's list,
// advanced by the overlay owner), so tests drive a manual clock the same way.
// Coordinates are content px; the host places the root layer flat in shell
// space at the top of the screen (native/facility_owner.hpp), scaled by the
// design unit, and routes hover and presses to control().
//
// Substitutions (pose-only motion, recorded): the fbIn clip-path unfold from
// the left edge is the plate's left wipe over the same .42 s SNAP (the path
// is a plain rectangle growing in width, so the visible area is identical);
// the drop and the rise are the plate's translateY.
#include "modules/facility_alerts.hpp"
#include "modules/hud_design_control.hpp"

namespace endfield::modules::facility {
namespace d = design;

// HUDFacilityControl (:21-32): hover and press drive closures; an optional
// visible silhouette (layer coordinates) outside which presses miss.
class FacilityControl final : public d::Control {
public:
    FacilityControl(d::Environment& env, std::string id, d::Layer& layer) : Control(env, std::move(id), "", layer) {}
    std::function<void(bool)> onHover;
    std::optional<d::PathBuilder> hitPath;
    void hoverChanged() override { if (onHover) onHover(hovered() && enabled()); }
    bool contains(d::Point p) const override;
};

// HUDHazardStripes (:34-89): repeating-linear-gradient(-45deg, color 0 p/2,
// #111 p/2 p) drifting right, bands every `period` px horizontally; the loop
// runs between start and stop only, never under Reduce Motion. The band is
// an animatable layer (translateX keyframes 0 -> period, linear, repeating);
// the hazard layer clips it (masksToBounds).
class HazardStripes final {
public:
    HazardStripes(d::Builder&, std::string_view id, d::Size, double period, d::Color, double cycle);
    d::Layer& layer() const noexcept { return *layer_; }
    d::Layer& band() const noexcept { return *band_; }
    bool isRunning() const noexcept;
    void start(d::Environment&);
    void stop();
    // Bands where (x + y) mod p lies in [p/2, p); one extra period on the left covers the drift.
    static d::PathBuilder path(d::Size, double period);
private:
    d::Layer *layer_, *band_;
    double period_, cycle_;
};

// HUDFacilityBanner (:91-351): 540 x 70, a plain rectangle (owner round 2:
// no cut corners) with the moving hazard block, FACILITY ALERT and (outside
// English) the English name, title, detail and the big orange value. It
// drops in and unfolds (fbIn), counts down 5 s (paused while hovered), then
// rises out (fbOut). Alerts raised while one shows queue and follow one by one.
class FacilityBanner final {
public:
    static constexpr d::Size size{540, 70};
    static constexpr double lifetime = 5;
    explicit FacilityBanner(d::Environment&);
    ~FacilityBanner();
    FacilityBanner(const FacilityBanner&) = delete;
    FacilityBanner& operator=(const FacilityBanner&) = delete;
    d::Layer& layer() const noexcept { return *layer_; }
    // The host routes pointer and hover here: hover pauses the countdown, a
    // click opens the alert's module and hides the banner.
    d::Control& control() noexcept { return *hit_; }
    std::function<void(FacilityAlertKind)> onClick;
    // Called when the last banner has left (after its countdown, a click or hide).
    std::function<void()> onHidden;
    bool isShowing() const noexcept { return showing_; }
    const std::optional<FacilityAlert>& current() const noexcept { return current_; }
    const std::vector<FacilityAlert>& queue() const noexcept { return queue_; }
    // Shows `alert`, or queues it behind the banner on screen. The same alert
    // again refreshes its banner (on screen or queued) in place.
    void show(const FacilityAlert&);
    // Hides the banner on screen and drops the queue.
    void hide(bool animated);
    // Drops a queued banner of `kind`, and lets it leave if it is on screen.
    void withdraw(FacilityAlertKind);
    // Plate geometry (:339-341): the rectangle at `unfolded` 0...1 of its width.
    static d::Rect plateRect(double unfolded) noexcept;
    // Content revision of the banner's arena (the owner re-emits on change).
    std::uint64_t contentRevision() const noexcept { return arena_.revision(); }
    // Verification.
    std::array<std::string, 6> texts() const;  // label, name, title, detail, value, unit
    double countdownRemaining() const noexcept;
    bool isCountingDown() const noexcept { return runningSince_.has_value(); }
    bool isHazardRunning() const noexcept { return hazard_->isRunning(); }
    const d::Layer& plate() const noexcept { return *plate_; }
    const d::Layer& rule() const noexcept { return *rule_; }
private:
    d::Environment* env_;
    d::LayerArena arena_;
    d::ControlSet controls_;
    d::Builder builder_;
    d::Layer *layer_, *plate_, *fill_, *label_, *name_, *title_, *detail_, *value_, *unit_, *rule_;
    std::unique_ptr<HazardStripes> hazard_;
    FacilityControl* hit_{};
    std::optional<FacilityAlert> current_;
    std::vector<FacilityAlert> queue_;
    bool showing_{}, leaving_{}, hovered_{};
    double remaining_{};
    std::optional<double> runningSince_;
    d::Deadlines::Token expiry_{}, removal_{};
    void present(const FacilityAlert&);
    void render(const FacilityAlert&);
    void click();
    void leave(bool animated);
    void finishLeaving();
    void setHovered(bool);
    void resumeCountdown();
    void pauseCountdown();
    void cancel(d::Deadlines::Token&) noexcept;
};
} // namespace endfield::modules::facility
