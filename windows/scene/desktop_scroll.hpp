#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace ehud::scene {

// Wheel/spring portion of HUDSourceDesktopScrollMotion at canonical baseline
// 4036174a3facf935260f4d0a9c63bfff33b98c37. The caller supplies normalized delta
// and a monotonic clock. No Windows touchpad gesture/momentum phases are inferred.
class DesktopScrollMotion {
  public:
    static constexpr double gestureEdgeTravel = 240.0;
    static constexpr double wheelEdgeTravel = 180.0;

    static double presentationPosition(double value, double hiddenLength) {
        const double limit = hiddenLength > 0 ? edgeLimit(gestureEdgeTravel, hiddenLength) : 0;
        return std::clamp(value, -limit, 1 + limit);
    }

    double position() const { return position_; }
    double target() const { return target_; }
    bool requiresFrames() const {
        return std::abs(position_ - target_) > epsilon_ || std::abs(velocity_) > epsilon_ * 18;
    }
    bool canScroll(int direction) const {
        return direction < 0 ? target_ < 1 - 1e-9 : target_ > 1e-9;
    }

    void reset(double value, double time) {
        position_ = std::clamp(std::isfinite(value) ? value : 1, 0.0, 1.0);
        target_ = position_;
        velocity_ = 0;
        lastTime_ = std::isfinite(time) ? std::optional<double>(time) : std::nullopt;
    }

    void scroll(double delta, double hiddenLength, double time, bool reduceMotion = false) {
        if (!std::isfinite(delta) || !std::isfinite(hiddenLength) || hiddenLength <= 0 ||
            !std::isfinite(time))
            return;
        advance(time);
        edgeLimit_ = edgeLimit(wheelEdgeTravel, hiddenLength);
        epsilon_ = std::min(.0001, .25 / hiddenLength);
        const double requested = target_ + delta;
        target_ = std::clamp(requested, 0.0, 1.0);
        if (reduceMotion) {
            position_ = target_;
            velocity_ = 0;
            return;
        }
        const double overflow = requested - target_;
        if (overflow != 0) {
            position_ += std::clamp(overflow * .55, -edgeLimit_ * .5, edgeLimit_ * .5);
            position_ = std::clamp(position_, -edgeLimit_, 1 + edgeLimit_);
        }
    }

    double advance(double time) {
        if (!std::isfinite(time))
            return position_;
        const auto previous = lastTime_;
        lastTime_ = time;
        if (!previous || time <= *previous || !requiresFrames())
            return position_;
        const double dt = std::min(2.0, time - *previous), decay = 11, frequency = 15;
        const double offset = position_ - target_, b = (velocity_ + decay * offset) / frequency;
        const double e = std::exp(-decay * dt), c = std::cos(frequency * dt),
                     s = std::sin(frequency * dt);
        position_ = target_ + e * (offset * c + b * s);
        velocity_ =
            e * ((b * frequency - decay * offset) * c - (offset * frequency + decay * b) * s);
        position_ = std::clamp(position_, -edgeLimit_, 1 + edgeLimit_);
        if (!requiresFrames() || dt >= 1) {
            position_ = target_;
            velocity_ = 0;
        }
        return position_;
    }

  private:
    static double edgeLimit(double travel, double hiddenLength) {
        return travel / std::max(1.0, hiddenLength);
    }
    double position_{1}, target_{1}, velocity_{}, edgeLimit_{}, epsilon_{.0001};
    std::optional<double> lastTime_;
};

} // namespace ehud::scene
