#pragma once
#include "modules/charge_indicator.hpp"
#include <array>
#include <cstdint>
#include <optional>

// Portable port of Sources/HUDChargeBadge.swift (the HUD's persistent charge
// control that adopts the notification renderer at scale 0.82) and of the
// seeded HUDDeploymentFlicker.sequence it applies on deployment/retraction.
// Deadlines are explicit values folded into the host's single frame clock or
// one-shot wake; no scheduler, timer, window or renderer is owned here.
namespace endfield::modules {
struct DeploymentFlickerSequence {
    static constexpr std::size_t capacity = 14; // three pulses at most
    std::array<double, capacity> keyTimes{}, offsets{};
    std::size_t count{};
    double duration{}, delay{};
    // Additive opacity offset (baseline 1) at `elapsed` since application:
    // backward fill before the delay, removed after completion.
    double offset(double elapsed) const noexcept;
    double end() const noexcept { return delay + duration; }
};
// HUDDeploymentFlicker.sequence(opening:duration:delay:seed:). A nil duration
// selects the source fallback; nonfinite values are sanitized identically.
DeploymentFlickerSequence deploymentFlickerSequence(bool opening, std::optional<double> duration, double delay,
                                                    std::uint64_t seed) noexcept;

class ChargeBadgeState final {
public:
    enum class Phase : std::uint8_t { hidden, waiting, entering, presented, exiting };
    struct Events {
        bool entranceCompleted{}, exitCompleted{}, hitRegionChanged{};
        bool any() const noexcept { return entranceCompleted || exitCompleted || hitRegionChanged; }
    };
    static constexpr core::Point center{500, 446};
    static constexpr double rendererScale = 0.82, entranceDelay = 0.58, compactHoldDuration = 3;
    static constexpr double entranceDuration = entranceDelay + ChargeIndicatorMetrics::entranceDuration;
    static constexpr double exitDuration = ChargeIndicatorMetrics::exitDuration;
    // HUD design coordinates of the adopted 300x84 canvas and its capsule hits.
    static constexpr core::Rect frame() noexcept {
        return {center.x - ChargeIndicatorMetrics::canvasWidth * rendererScale / 2,
                center.y - ChargeIndicatorMetrics::canvasHeight * rendererScale / 2,
                ChargeIndicatorMetrics::canvasWidth * rendererScale, ChargeIndicatorMetrics::canvasHeight * rendererScale};
    }
    static constexpr core::Rect compactHitRect() noexcept {
        return {center.x - 264 * rendererScale / 2, center.y - 38 * rendererScale / 2, 264 * rendererScale, 38 * rendererScale};
    }
    static constexpr core::Rect circleHitRect() noexcept {
        return {center.x - 19 * rendererScale, center.y - 19 * rendererScale, 38 * rendererScale, 38 * rendererScale};
    }

    ChargeBadgeState();
    void setReduceMotion(bool) noexcept;
    // Appearance-dependent ChargeIndicatorView.bodyBorder for both hover states.
    void setBorder(double width, const ChargeColor& normal, double hoveredWidth, const ChargeColor& hovered, double time) noexcept;
    void setStable(bool visible, double time) noexcept;
    // The caller supplies a fresh flicker seed per deployment (the source
    // draws UInt64.random); tests pass deterministic seeds.
    void animateEntrance(double time, std::uint64_t flickerSeed) noexcept;
    void animateExit(double time, std::uint64_t flickerSeed) noexcept;
    void setHovered(bool hovered, bool animated, double time) noexcept;
    void cancel(double time) noexcept;
    // Runs due badge and renderer steps at their scheduled times, in order.
    Events advance(double time) noexcept;
    std::optional<double> nextDeadline() const noexcept;
    bool requiresFrames(double time) const noexcept;
    ChargeIndicatorPose sample(double time) const noexcept;
    // Presentation capsule in HUD design coordinates; empty while hidden,
    // waiting for the delayed reveal, or retracting.
    core::Rect hitRect(double time) const noexcept;
    bool contains(core::Point designPoint, double time) const noexcept;
    Phase phase() const noexcept { return phase_; }
    bool hovered() const noexcept { return hovered_; }
    ChargeStage stage() const noexcept { return renderer_.stage(); }
    std::uint64_t generation() const noexcept { return generation_; }
    const ChargeIndicatorTimeline& renderer() const noexcept { return renderer_; }
private:
    ChargeIndicatorTimeline renderer_;
    Phase phase_{Phase::hidden};
    bool compactDeadlinePassed_{}, hovered_{}, reduced_{};
    std::optional<double> entranceAt_, compactAt_;
    std::optional<DeploymentFlickerSequence> flicker_;
    double flickerStart_{};
    std::uint64_t generation_{}, pendingSeed_{};
    double borderWidth_{0.5}, hoveredBorderWidth_{1.5};
    ChargeColor border_{}, hoveredBorder_{};
    void cancelAnimations(double time) noexcept;
    void scheduleCompactDeadline(double time) noexcept;
};
} // namespace endfield::modules
