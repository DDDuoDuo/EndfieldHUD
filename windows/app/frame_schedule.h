#pragma once
#include "scene/watch_scene.hpp"

namespace ehud::app {
struct FrameDecision { bool submit{}; bool rebuild{}; };

// Remember the last submitted animation demand so each finite animation draws
// its settled endpoint once before the shell parks its frame timer.
class FrameSchedule {
public:
    FrameDecision next(scene::Phase phase, bool invalidated, bool gyro, bool buttons) {
        if (phase == scene::Phase::concealed) { reset(); return {}; }
        const bool rebuild = invalidated || phase != scene::Phase::visible ||
            phase != lastPhase_ || buttons || lastButtons_;
        const bool submit = rebuild || gyro || lastGyro_;
        return {submit, rebuild};
    }
    void submitted(scene::Phase phase, bool gyro, bool buttons) {
        lastPhase_ = phase; lastGyro_ = gyro; lastButtons_ = buttons;
    }
    void reset() {
        lastPhase_ = scene::Phase::concealed; lastGyro_ = false; lastButtons_ = false;
    }
private:
    scene::Phase lastPhase_{scene::Phase::concealed};
    bool lastGyro_{}, lastButtons_{};
};
} // namespace ehud::app
