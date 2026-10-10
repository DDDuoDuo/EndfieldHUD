#include "modules/charge_badge.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace {
// HUDDeploymentFlicker.Generator (SplitMix64).
struct Generator {
    std::uint64_t state;
    double unit() noexcept {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t value = state;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        value ^= value >> 31;
        return static_cast<double>(value >> 11) / static_cast<double>(std::uint64_t(1) << 53);
    }
};
constexpr double deadlineAllowance = 1e-9;
} // namespace

DeploymentFlickerSequence deploymentFlickerSequence(bool opening, std::optional<double> duration, double delay,
                                                    std::uint64_t seed) noexcept {
    Generator random{seed};
    const double maximum = opening ? 0.5 : 0.3, fallback = opening ? 0.42 : 0.23;
    const double requested = duration.value_or(fallback);
    const double total = std::isfinite(requested) ? std::min(maximum, std::max(0.08, requested)) : fallback;
    const double initialDelay = std::isfinite(delay) ? std::max(0.0, delay) : 0;
    const double stagger = random.unit() * total * 0.14;
    const int additionalPulse = static_cast<int>(random.unit() * 2);
    const int pulseCount = opening ? 2 + (total >= 0.25 ? additionalPulse : 0) : 1 + additionalPulse;
    const double cell = 0.84 / pulseCount;
    DeploymentFlickerSequence result;
    auto push = [&](double time, double offset) { result.keyTimes[result.count] = time; result.offsets[result.count] = offset; ++result.count; };
    push(0, 0);
    for (int pulse = 0; pulse < pulseCount; ++pulse) {
        const double start = 0.04 + pulse * cell + random.unit() * cell * 0.12;
        const double attack = cell * (0.05 + random.unit() * 0.025);
        const double hold = cell * (0.45 + random.unit() * 0.10);
        const double release = cell * (0.06 + random.unit() * 0.03);
        const double depth = opening && pulse > 0 ? 0.23 + random.unit() * 0.38 : 0.42 + random.unit() * 0.36;
        push(start, 0); push(start + attack, -depth); push(start + attack + hold, -depth); push(start + attack + hold + release, 0);
    }
    push(1, 0);
    result.duration = total - stagger;
    result.delay = initialDelay + stagger;
    return result;
}
double DeploymentFlickerSequence::offset(double elapsed) const noexcept {
    if (count < 2 || !(duration > 0) || elapsed >= delay + duration) return 0;
    const double local = std::clamp((elapsed - delay) / duration, 0.0, 1.0);
    for (std::size_t i = 0; i + 1 < count; ++i) {
        if (local <= keyTimes[i + 1]) {
            const double span = keyTimes[i + 1] - keyTimes[i];
            return span > 0 ? offsets[i] + (offsets[i + 1] - offsets[i]) * (local - keyTimes[i]) / span : offsets[i + 1];
        }
    }
    return offsets[count - 1];
}

ChargeBadgeState::ChargeBadgeState() { renderer_.setStage(ChargeStage::hidden, false, 0); }
void ChargeBadgeState::setReduceMotion(bool value) noexcept { reduced_ = value; renderer_.setReduceMotion(value); }
void ChargeBadgeState::setBorder(double width, const ChargeColor& normal, double hoveredWidth, const ChargeColor& hovered,
                                 double time) noexcept {
    borderWidth_ = width; border_ = normal; hoveredBorderWidth_ = hoveredWidth; hoveredBorder_ = hovered;
    renderer_.setBorderModel(hovered_ ? hoveredWidth : width, hovered_ ? hovered : normal, time);
}
void ChargeBadgeState::cancelAnimations(double time) noexcept {
    ++generation_;
    entranceAt_.reset(); compactAt_.reset();
    renderer_.cancel();
    flicker_.reset(); // the renderer removes every canvas animation
    if (hovered_) { hovered_ = false; renderer_.animateBorder(borderWidth_, border_, false, time); }
}
void ChargeBadgeState::scheduleCompactDeadline(double time) noexcept { compactAt_ = time + compactHoldDuration; }
void ChargeBadgeState::setStable(bool visible, double time) noexcept {
    cancelAnimations(time);
    phase_ = visible ? Phase::presented : Phase::hidden;
    compactDeadlinePassed_ = false;
    renderer_.setStage(visible ? ChargeStage::compact : ChargeStage::hidden, false, time);
    if (visible) scheduleCompactDeadline(time);
}
void ChargeBadgeState::animateEntrance(double time, std::uint64_t flickerSeed) noexcept {
    cancelAnimations(time);
    compactDeadlinePassed_ = false;
    if (reduced_) {
        phase_ = Phase::presented;
        renderer_.setStage(ChargeStage::compact, false, time);
        scheduleCompactDeadline(time);
        return;
    }
    phase_ = Phase::waiting;
    renderer_.setStage(ChargeStage::hidden, false, time);
    pendingSeed_ = flickerSeed;
    entranceAt_ = time + entranceDelay;
}
void ChargeBadgeState::animateExit(double time, std::uint64_t flickerSeed) noexcept {
    const bool hadVisibleContent = phase_ != Phase::hidden && phase_ != Phase::waiting;
    cancelAnimations(time);
    phase_ = Phase::exiting;
    // Closing before the delayed reveal must not summon a new circle.
    if (!hadVisibleContent) { phase_ = Phase::hidden; renderer_.setStage(ChargeStage::hidden, false, time); return; }
    renderer_.animateExit(time);
    if (!reduced_) { flicker_ = deploymentFlickerSequence(false, 0.11, 0.07, flickerSeed); flickerStart_ = time; }
}
void ChargeBadgeState::setHovered(bool hovered, bool animated, double time) noexcept {
    if (phase_ == Phase::hidden || phase_ == Phase::waiting || phase_ == Phase::exiting || hovered_ == hovered) return;
    hovered_ = hovered;
    if (phase_ == Phase::presented && compactDeadlinePassed_)
        renderer_.morphEmbedded(hovered ? ChargeStage::compact : ChargeStage::circle, animated, time);
    renderer_.animateBorder(hovered ? hoveredBorderWidth_ : borderWidth_, hovered ? hoveredBorder_ : border_, animated, time);
}
void ChargeBadgeState::cancel(double time) noexcept { cancelAnimations(time); }
ChargeBadgeState::Events ChargeBadgeState::advance(double time) noexcept {
    Events events;
    for (;;) {
        const auto renderer = renderer_.nextDeadline();
        double next = INFINITY;
        for (const auto& candidate : {entranceAt_, compactAt_, renderer}) if (candidate) next = std::min(next, *candidate);
        if (!(next <= time + deadlineAllowance)) break;
        if (entranceAt_ && *entranceAt_ == next) {
            entranceAt_.reset();
            phase_ = Phase::entering;
            renderer_.animateEntrance(next);
            if (!reduced_) { flicker_ = deploymentFlickerSequence(true, 0.18, 0.14, pendingSeed_); flickerStart_ = next; }
            events.hitRegionChanged = true;
        } else if (renderer && *renderer == next) {
            const auto event = renderer_.advance(next);
            if (event == ChargeIndicatorTimeline::Event::entranceCompleted && phase_ == Phase::entering) {
                phase_ = Phase::presented;
                scheduleCompactDeadline(next);
                events.entranceCompleted = events.hitRegionChanged = true;
            } else if (event == ChargeIndicatorTimeline::Event::exitCompleted && phase_ == Phase::exiting) {
                phase_ = Phase::hidden;
                events.exitCompleted = events.hitRegionChanged = true;
            }
        } else {
            compactAt_.reset();
            if (phase_ == Phase::presented) {
                compactDeadlinePassed_ = true;
                if (!hovered_) renderer_.morphEmbedded(ChargeStage::circle, true, next);
                events.hitRegionChanged = true;
            }
        }
    }
    if (flicker_ && time >= flickerStart_ + flicker_->end()) flicker_.reset();
    return events;
}
std::optional<double> ChargeBadgeState::nextDeadline() const noexcept {
    std::optional<double> next;
    for (const auto& candidate : {entranceAt_, compactAt_, renderer_.nextDeadline()})
        if (candidate && (!next || *candidate < *next)) next = candidate;
    return next;
}
bool ChargeBadgeState::requiresFrames(double time) const noexcept {
    return renderer_.animating(time) || (flicker_ && time < flickerStart_ + flicker_->end());
}
ChargeIndicatorPose ChargeBadgeState::sample(double time) const noexcept {
    auto pose = renderer_.sample(time);
    if (flicker_) pose.canvasOpacity = std::clamp(1 + flicker_->offset(time - flickerStart_), 0.0, 1.0);
    return pose;
}
core::Rect ChargeBadgeState::hitRect(double time) const noexcept {
    if (phase_ == Phase::hidden || phase_ == Phase::waiting || phase_ == Phase::exiting) return {};
    const auto body = renderer_.sample(time).bodyRect();
    const auto f = frame();
    return {f.x + body.x * rendererScale, f.y + body.y * rendererScale, body.width * rendererScale, body.height * rendererScale};
}
bool ChargeBadgeState::contains(core::Point p, double time) const noexcept {
    if (phase_ == Phase::hidden || phase_ == Phase::waiting || phase_ == Phase::exiting) return false;
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    // Once acquired, the complete capsule remains a hover target during its
    // expansion; the 2.5-point allowance prevents edge jitter reversals.
    auto rect = hovered_ ? compactHitRect() : hitRect(time);
    if (hovered_) rect = {rect.x - 2.5, rect.y - 2.5, rect.width + 5, rect.height + 5};
    if (!(rect.width > 0 && rect.height > 0)) return false;
    const double radius = std::min(rect.height, rect.width) / 2;
    const double qx = std::abs(p.x - (rect.x + rect.width / 2)) - (rect.width / 2 - radius);
    const double qy = std::abs(p.y - (rect.y + rect.height / 2)) - (rect.height / 2 - radius);
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) <= radius;
}
} // namespace endfield::modules
