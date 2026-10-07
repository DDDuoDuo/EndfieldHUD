#include "app/frame_schedule.h"
#include "scene/desktop_scroll.hpp"
#include "scene/watch_scene.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ehud::scene;
namespace {
int checks{};
void check(bool ok, const char *reason) {
    ++checks;
    if (!ok)
        throw std::runtime_error(reason);
}
void near(double a, double b, double tolerance, const char *reason) {
    ++checks;
    if (!(std::abs(a - b) <= tolerance))
        throw std::runtime_error(std::string(reason) + ": got " + std::to_string(a) +
                                 ", expected " + std::to_string(b));
}
template <class F> void rejects(F &&operation, const char *reason) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, reason);
}
void desktopScroll() {
    DesktopScrollMotion motion;
    check(motion.position() == 1 && motion.target() == 1 && !motion.requiresFrames(),
          "Desktop wheel motion starts parked at source normalized top");
    check(!motion.canScroll(-1) && motion.canScroll(1),
          "Source direction availability reads bounded target instead of animated position");
    motion.reset(1, 10);
    motion.scroll(-.5, 100, 10);
    near(motion.position(), 1, 0,
         "Ordinary wheel input retargets without jumping current position");
    near(motion.target(), .5, 0, "Wheel input accumulates normalized delta in the source target");
    check(motion.requiresFrames(), "Unsettled source spring requests frame clock");
    near(motion.advance(10.05), .8552733733030645, 1e-12,
         "Source spring first reference sample uses decay11/frequency15");
    near(motion.advance(10.15), .49447024824534547, 1e-12,
         "Original damped spring crosses target without Euler cadence dependence");
    near(motion.advance(10.2), .45088625126458476, 1e-12,
         "Original spring carries velocity between clock samples");
    DesktopScrollMotion single;
    single.reset(1, 10);
    single.scroll(-.5, 100, 10);
    near(single.advance(10.2), motion.position(), 1e-12,
         "Closed-form scroll is independent of subdivided frame cadence");
    const double prior = motion.position();
    motion.scroll(-.2, 100, 10.2);
    near(motion.target(), .3, 1e-12,
         "Repeated wheel deltas accumulate in target rather than lagging pose");
    near(motion.position(), prior, 0,
         "Wheel retarget preserves continuous current spring position");
    motion.advance(12.2);
    check(motion.position() == motion.target() && !motion.requiresFrames(),
          "Two-second advance cap ends finite motion at exact target");
    motion.reset(1, 0);
    motion.scroll(.1, 100, 0);
    near(motion.position(), 1.055, 1e-12, "Edge overflow uses the exact source0.55 gain");
    near(motion.target(), 1, 0, "Wheel edge target stays bounded while presentation rebounds");
    motion.reset(1, 0);
    for (int i = 0; i < 10; ++i)
        motion.scroll(1000, 100, 0);
    near(motion.position(), 2.8, 1e-12, "Repeated wheel overflow stays within180 source units");
    check(!motion.canScroll(-1) && motion.canScroll(1),
          "Overscroll cannot enable navigation past bounded source target");
    motion.advance(1);
    check(motion.position() == 1 && !motion.requiresFrames(),
          "One-second delayed wheel frame settles exactly without persistent demand");
    motion.reset(0, 0);
    motion.scroll(-1000, 100, 0);
    near(motion.position(), -.9, 1e-12,
         "One lower-edge wheel impulse is capped at half180-unit travel");
    for (int i = 0; i < 10; ++i)
        motion.scroll(-1000, 100, 0);
    near(motion.position(), -1.8, 1e-12,
         "Lower wheel rebound has the same original bounded travel");
    motion.reset(1, 0);
    motion.scroll(1000, .25, 0);
    near(motion.position(), 91, 0, "Short lists use a one-unit divisor floor for edge travel");
    near(DesktopScrollMotion::presentationPosition(10000, .25), 241, 0,
         "Source layout shares240-unit rebound allowance with precision-gesture provider");
    near(DesktopScrollMotion::presentationPosition(-10000, 100), -2.4, 0,
         "Source presentation lower edge preserves full original240-unit allowance");
    near(DesktopScrollMotion::presentationPosition(10000, 100), 3.4, 0,
         "Source presentation upper edge preserves full original240-unit allowance");
    near(DesktopScrollMotion::presentationPosition(2, 0), 1, 0,
         "No-overflow layout permits no rebound");
    motion.reset(1, 0);
    motion.scroll(-.5, 100, 0, true);
    check(motion.position() == .5 && motion.target() == .5 && !motion.requiresFrames(),
          "Reduced motion immediately seeks source bounded target");
    motion.scroll(10, 100, .1, true);
    check(motion.position() == 1 && !motion.requiresFrames(),
          "Reduced-motion edges do not create a rebound or animation timer");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    motion.reset(nan, nan);
    check(motion.position() == 1 && !motion.requiresFrames(),
          "Source reset sanitizes nonfinite position and unsets invalid clock");
    motion.scroll(-.5, 100, 0);
    near(motion.advance(.05), .8552733733030645, 1e-12,
         "First valid wheel clock initializes time after an invalid reset time");
    const auto before = motion.position(), target = motion.target();
    motion.scroll(nan, 100, .2);
    motion.scroll(-.5, nan, .2);
    motion.scroll(-.5, 0, .2);
    motion.scroll(-.5, 100, nan);
    motion.advance(nan);
    check(motion.position() == before && motion.target() == target,
          "Invalid wheel/time/overflow inputs leave source motion and clock untouched");
    near(motion.advance(.15), .49447024824534547, 1e-12,
         "Ignored invalid input cannot alter later source clock integration");
    motion.reset(1, 0);
    motion.scroll(-.5, 100, 0);
    constexpr double targetCrossing = .146903010786472;
    near(motion.advance(targetCrossing), .5, 1e-12,
         "Source spring reference reaches zero displacement before stopping");
    check(motion.requiresFrames(), "Spring velocity keeps frame demand through target crossing");
    DesktopScrollMotion shortList, longList;
    shortList.reset(1, 0);
    longList.reset(1, 0);
    shortList.scroll(-.5, 100, 0);
    longList.scroll(-.5, 10000000, 0);
    shortList.advance(.9);
    longList.advance(.9);
    check(!shortList.requiresFrames() && shortList.position() == .5 && longList.requiresFrames(),
          "Source epsilon tightens to0.25 source units for very long lists");
    longList.advance(1.9);
    check(longList.position() == .5 && !longList.requiresFrames(),
          "Even tight long-list epsilon parks after a delayed source frame");
    motion.reset(-1, 0);
    check(motion.position() == 0 && motion.canScroll(-1) && !motion.canScroll(1),
          "Reset clamps lower boundary and exposes only the permitted direction");
    motion.reset(2, 0);
    check(motion.position() == 1 && !motion.canScroll(-1) && motion.canScroll(1),
          "Reset clamps upper boundary and exposes only the permitted direction");
    Frame arrows;
    arrows.camera.viewport = {200, 100};
    arrows.camera.viewProjection = Mat4::identity();
    arrows.scrollIndicators.push_back(
        {-1, "arrow", {{-.5, -.5}, {1, 1}}, Mat4::identity(), Mat4::identity(), {}, true, false});
    check(arrows.scrollDirectionAt({100, 50}) == -1,
          "Disabled active source arrows consume their authored hit plane");
    arrows.scrollIndicators.front().masks.push_back(
        {{{.1, .1}, {.2, .2}}, Mat4::identity(), Mat4::identity(), "mask"});
    check(!arrows.scrollDirectionAt({100, 50}),
          "Arrow input shares the actual source mask intersection");
    arrows.scrollIndicators.front().masks.clear();
    arrows.scrollIndicators.front().active = false;
    check(!arrows.scrollDirectionAt({100, 50}),
          "Opening/closing source arrows cannot receive input");
    arrows.scrollIndicators.front().active = true;
    check(!arrows.scrollDirectionAt({180, 50}),
          "Arrow input uses the bounded local rectangle rather than a screen strip");
}
void curves() {
    ScalarCurve hermite({{0, 2, 0, 3}, {2, 8, 3, 0}});
    near(hermite.sample(1), 5, 1e-12, "Source Hermite slope units use seconds");
    near(hermite.sample(-1), 2, 0, "Curve pre-infinity clamps");
    near(hermite.sample(3), 8, 0, "Curve post-infinity clamps");
    ScalarCurve stepped({{0, 0, 0, std::numeric_limits<double>::infinity()}, {1, 1, 0, 0}});
    near(stepped.sample(0.999), 0, 0, "Infinite tangent is a stepped segment");
    near(stepped.sample(1), 1, 0, "Step includes endpoint key");
    ScalarCurve weighted({{0, 0, 0, 0, 2, 1.0 / 3, 0.2}, {1, 1, 0, 0, 1, 0.2, 1.0 / 3}});
    near(weighted.sample(0.5), 0.5, 1e-12, "Weighted Bezier solves original time handles");
    rejects([] { ScalarCurve invalid({{1, 0, 0, 0}, {1, 1, 0, 0}}); },
            "Duplicate curve times reject");
    rejects([&] { hermite.sample(std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite sampling rejects");
}
void lifecycle() {
    Playback playback(0.75, 0.5);
    check(playback.phase() == Phase::concealed, "Initial phase closed");
    playback.open(10);
    auto sample = playback.sample(10.375);
    check(sample.phase == Phase::opening, "Opening duration stays source length");
    near(sample.entranceTime, 0.5625, 1e-12, "Finite wrapper applies OutQuad exactly once");
    check(!sample.ambientTime, "No ambient during opening");
    sample = playback.sample(10.75);
    check(sample.phase == Phase::visible, "Entrance completion visible");
    near(*sample.ambientTime, 0, 0, "Ambient starts after finite entrance");
    auto generation = playback.generation();
    playback.close(11);
    sample = playback.sample(11.25);
    near(*sample.exitTime, 0.375, 1e-12, "Closing wrapper OutQuad");
    playback.open(11.3);
    check(playback.generation() == generation + 2, "Interrupted transitions carry new generation");
    check(playback.sample(11.6).phase == Phase::opening,
          "Old closing deadline cannot conceal reopen");
    playback.close(12, true);
    check(playback.sample(12).phase == Phase::concealed,
          "Reduced motion close has no submitted frame");
    playback.open(13, true);
    sample = playback.sample(13, true);
    check(sample.phase == Phase::visible && !sample.ambientTime,
          "Reduced motion seeks source endpoint");
    near(Playback::clipTime(-1, 0.75), 0, 0, "Finite clock lower clamp");
    near(Playback::clipTime(100, 0.75), 0.75, 0, "Finite clock upper clamp");
}
void frameScheduling() {
    ehud::app::FrameSchedule schedule;
    Playback playback(0.2, 0.2);
    playback.open(0);
    auto phase = playback.sample(0.184).phase;
    auto decision = schedule.next(phase, false, false, false);
    check(decision.submit && decision.rebuild, "Opening submits source geometry");
    schedule.submitted(phase, false, false);
    phase = playback.sample(0.216).phase;
    decision = schedule.next(phase, false, false, false);
    check(decision.submit && decision.rebuild,
          "Entrance deadline draws exact source endpoint before parking");
    schedule.submitted(phase, false, false);
    check(!schedule.next(phase, false, false, false).submit, "Settled entrance parks on next tick");

    GyroMotion motion;
    motion.retarget({2, 3, 0}, 1, 0.2);
    motion.finishIfNeeded(1.184);
    decision = schedule.next(phase, false, motion.animating(), false);
    check(decision.submit && !decision.rebuild, "Pointer-only motion reuses source geometry");
    schedule.submitted(phase, motion.animating(), false);
    motion.finishIfNeeded(1.216);
    decision = schedule.next(phase, false, motion.animating(), false);
    check(decision.submit && !decision.rebuild, "Gyro deadline reprojects exact settled transform");
    schedule.submitted(phase, motion.animating(), false);
    check(!schedule.next(phase, false, false, false).submit, "Settled gyro parks after endpoint");

    decision = schedule.next(phase, true, false, true);
    check(decision.submit && decision.rebuild, "Button animation rebuilds changed source geometry");
    schedule.submitted(phase, false, true);
    decision = schedule.next(phase, false, false, false);
    check(decision.submit && decision.rebuild, "Button deadline rebuilds its final tint and depth");
    schedule.submitted(phase, false, false);
    for (int i = 0; i < 100; ++i)
        check(!schedule.next(phase, false, false, false).submit,
              "Settled demand does not restart idle frames");
    schedule.submitted(phase, true, true);
    check(!schedule.next(Phase::concealed, true, true, true).submit,
          "Closed state cancels all animation work");
    check(!schedule.next(Phase::concealed, false, false, false).submit,
          "Closed state remains parked");
    decision = schedule.next(Phase::visible, true, false, false);
    check(decision.submit && decision.rebuild, "Reduced-motion reopen still draws full endpoint");
    schedule.submitted(Phase::visible, false, false);
    check(!schedule.next(Phase::visible, false, false, false).submit,
          "Reopen forgets interrupted animation demand");
}
void projection() {
    Camera camera;
    camera.viewport = {1920, 1080};
    camera.viewProjection = {};
    double y = 1 / std::tan(0.4), aspect = 1920.0 / 1080;
    camera.viewProjection.values[0] = y / aspect;
    camera.viewProjection.values[5] = y;
    camera.viewProjection.values[10] = 100.0 / 99;
    camera.viewProjection.values[11] = 1;
    camera.viewProjection.values[14] = -100.0 / 99;
    Mat4 world = Mat4::identity();
    double angle = 0.17;
    world.values[0] = std::cos(angle);
    world.values[2] = -std::sin(angle);
    world.values[8] = std::sin(angle);
    world.values[10] = std::cos(angle);
    world.values[12] = 0.4;
    world.values[14] = 10;
    Vec3 local{0.7, -0.3, 0};
    auto pixel = camera.project(local, world);
    check(pixel.has_value(), "Tilted source point projects");
    auto point = camera.hit(*pixel, world, {{-2, -2}, {4, 4}});
    check(point.has_value(), "Hit uses identical drawn camera/world");
    near(point->x, local.x, 1e-11, "Inverse raycast X");
    near(point->y, local.y, 1e-11, "Inverse raycast Y");
    check(!camera.hit(*pixel, world, {{-1, -1}, {1, 1}}),
          "Local bounds reject despite projected bounding box");
    Frame frame;
    frame.camera = camera;
    frame.hits.push_back(
        {"CAB:-9007199254740993", "CAB:9223372036854775807", {{-2, -2}, {4, 4}}, world, {}});
    check(frame.buttonAt(*pixel) == "CAB:9223372036854775807",
          "Signed large IDs preserve exact decimal digits");
    frame.hits.push_back(
        {"front", "frontButton", {{-2, -2}, {4, 4}}, world, {{{{-1, -1}, {1, 1}}, world}}});
    check(frame.buttonAt(*pixel) == "CAB:9223372036854775807",
          "Top graphic clipping mask rejects hit-through");
    frame.hits.back().masks.clear();
    check(frame.buttonAt(*pixel) == "frontButton", "Frontmost source graphic consumes input");
    Mat4 singular{};
    check(!inverse(singular), "Singular transform cannot receive input");
    check(Rect{{1, 1}, {-2, -2}}.contains({0, 0}), "Signed source rects preserve inverted sizes");
    check(!Rect{{0, 0}, {0, 1}}.contains({0, 0}), "Zero-size rect cannot receive input");
}
void gyro() {
    GyroMotion motion;
    check(motion.retarget({2, 3, 0}, 0, 0.5), "Gyro target changes");
    auto zero = motion.rotation(0);
    near(zero.w, 1, 1e-15, "Retarget snapshots current quaternion");
    auto half = motion.rotation(0.25);
    check(half.w < 1 && half.w > 0.99, "Quaternion OutQuad interpolation remains normalized");
    near(half.x * half.x + half.y * half.y + half.z * half.z + half.w * half.w, 1, 1e-12,
         "Gyro interpolation normalized");
    motion.retarget({-2, -3, 0}, 0.25, 0.5);
    auto retained = motion.rotation(0.25);
    near(retained.x, half.x, 1e-15, "Retarget keeps interpolated pose");
    motion.stop(0.3);
    check(!motion.animating(), "Closing can park gyro without another timer");
    motion.retarget({}, 1, 0.5, true);
    check(!motion.animating(), "Reduced-motion gyro settles immediately");
    near(motion.rotation(1).w, 1, 1e-15, "Reduced-motion identity pose");
}
void flicker() {
    auto opening = flickerSequence(true, 0.13, 0.20, 42),
         repeat = flickerSequence(true, 0.13, 0.20, 42),
         closing = flickerSequence(false, 0.11, 0.01, 42);
    check(opening.keyTimes == repeat.keyTimes && opening.opacityOffsets == repeat.opacityOffsets,
          "Deployment flicker seeds are deterministic");
    near(opening.duration + opening.delay, 0.33, 1e-15,
         "Random initial stagger stays inside requested total");
    near(opening.opacity(0, true), 0, 0, "Opening gate hides before group turn");
    near(opening.opacity(10, true), 1, 0, "Opening gate restores after finite dropout");
    near(closing.opacity(0, false), 1, 0, "Closing remains visible before group turn");
    near(closing.opacity(10, false), 0, 0, "Closing gate retains disappearance after track");
    near(sweepDelay(0, 0, 640, true, 0.25), 0, 0, "Opening starts at top");
    near(sweepDelay(640, 0, 640, true, 0.25), 0.25, 0, "Opening reaches bottom last");
    near(sweepDelay(0, 0, 640, false, 0.23), 0.23, 0, "Closing reaches top last");
    near(sweepDelay(640, 0, 640, false, 0.23), 0, 0, "Closing starts at bottom");
    near(opening.keyTimes[1], .05404149696485899, 1e-15,
         "Source SplitMix64 seeded first pulse start");
    near(opening.opacityOffsets[2], -.49862586973638634, 1e-15,
         "Source SplitMix64 seeded first pulse depth");
    near(opening.offset(0, true, false), 0, 0, "Ungated source backward fill is additive zero");
    near(closing.offset(10, false, false), 0, 0, "Ungated source completion removes dropout");
    near(sweepDelay(-20, 0, 640, true, .25), 0, 0, "Sweep clamps above settled range");
    near(sweepDelay(900, 0, 640, true, .25), .25, 0, "Sweep clamps below settled range");
    near(sweepDelay(320, 640, 640, true, .25), 0, 0, "Empty sweep range has no delay");
    near(sweepDelay(320, 0, 640, true, -1), 0, 0, "Negative sweep span has no delay");
    near(sweepDelay(std::numeric_limits<double>::infinity(), 0, 640, true, .25), 0, 0,
         "Nonfinite settled center has no directional delay");
}
void deployment() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<DeploymentGroup> groups{{"hiddenParent", {}, true, 1, 0},
                                              {"childHidden", {"hiddenParent"}, false, 1, 0},
                                              {"card", {}, false, .8, 160},
                                              {"label", {"card"}, false, .9, 160},
                                              {"card", {}, false, .1, 640},
                                              {"duplicate", {}, true, 1, 0},
                                              {"duplicate", {}, false, 1, 0},
                                              {"invalid", {}, false, nan, 0},
                                              {"zero", {}, false, 0, 0},
                                              {"standalone", {}, false, .5, 640},
                                              {"capped", {}, false, 1.5, 0},
                                              {"overflow", {}, false, 1e300, 0},
                                              {"negative", {}, false, -.25, 0},
                                              {"childZero", {"zero"}, false, 1, 0},
                                              {"nonfiniteCenter", {}, false, 1, nan}};
    DeploymentFlicker motion;
    motion.begin(groups, true, 100, 42, DeploymentOptions::legacySweep(true));
    const auto tracks = motion.tracks();
    check(tracks.size() == 4,
          "Source grouping suppresses duplicate/hidden/invalid/ancestor layers");
    check(tracks[0].id == "card" && tracks[0].candidateIndex == 2 &&
              tracks[0].seed == 4354685564936845396ull,
          "Filtering preserves original ordered index and unsigned wrapped seed");
    near(tracks[0].sequence.keyTimes[1], .04191672049442841, 1e-15,
         "Filtered group retains independently verified source seed sequence");
    near(tracks[0].sequence.opacityOffsets[2], -.5423751740101274, 1e-15,
         "Filtered group retains independently verified dropout depth");
    near(tracks[0].baseline, static_cast<double>(.8f), 0,
         "Additive baseline preserves source CALayer Float model opacity");
    near(tracks[2].baseline, 1, 0, "Source group baseline caps model opacity at one");
    near(tracks[0].sequence.delay + tracks[0].sequence.duration, .3925, 1e-15,
         "Settled quarter-height opening delay remains outside random stagger");
    near(tracks[1].sequence.delay + tracks[1].sequence.duration, .58, 1e-15,
         "Bottom settled opening completes last");
    near(tracks[3].sequence.delay + tracks[3].sequence.duration, .33, 1e-15,
         "Invalid settled center preserves source zero sweep delay");
    near(motion.opacity("card", .8f, 100), 0, 0, "Opening backward fill hides the whole card");
    near(motion.additiveOffset("label", 100), 0, 0,
         "Selected descendant has no second dropout track");
    near(motion.opacity("unknown", .35, 100), .35, 0,
         "Unselected artwork receives no inferred Watch grouping");
    const double pulse = 100 + tracks[0].sequence.delay +
                         tracks[0].sequence.duration *
                             (tracks[0].sequence.keyTimes[2] + tracks[0].sequence.keyTimes[3]) / 2;
    near(motion.opacity("card", .9, pulse), .9 - .5423751740101274 * static_cast<double>(.8f),
         1e-13, "Dropout adds the captured baseline to the current independent reveal opacity");
    check(motion.requiresFrames(100.579) && !motion.requiresFrames(100.58),
          "Opening demand stops at the directional finite endpoint");
    near(motion.opacity("card", .9, 101), .9, 0, "Completed opening removes additive fill");
    auto changedCenters = groups;
    changedCenters[2].settledCenterY = 640;
    near(motion.tracks()[0].settledCenterY, 160, 0,
         "Deployment captures settled centers before later folding mutates geometry");
    const auto previousGeneration = motion.generation();
    motion.begin(groups, false, 102, 42, DeploymentOptions::legacySweep(false));
    check(motion.generation() > previousGeneration && motion.tracks().size() == 4,
          "Retraction replaces selected deployment tracks without stacking");
    near(motion.tracks()[0].sequence.delay + motion.tracks()[0].sequence.duration, .2925, 1e-15,
         "Closing reverses the frozen quarter-height sweep");
    near(motion.tracks()[1].sequence.delay + motion.tracks()[1].sequence.duration, .12, 1e-15,
         "Closing starts at the frozen bottom");
    check(!motion.requiresFrames(103), "Held closing fill requires no idle timer");
    near(motion.opacity("card", .8f, 103), 0, 0, "Closing endpoint holds additive disappearance");
    motion.begin({{"card", {}, false, .8, 160}}, true, 104, 8, {}, true);
    near(motion.additiveOffset("card", 104), 0, 0,
         "Reduced-motion apply cancels selected completed forward fill before guard");
    check(motion.tracks().size() == 3,
          "Reduced-motion selected cancellation retains unselected layers");
    motion.cancel({"standalone"});
    check(motion.tracks().size() == 2, "Source selected cancellation removes only supplied layers");
    motion.cancel();
    check(motion.tracks().empty() && !motion.requiresFrames(105),
          "Conceal/reuse clears all source dropout ownership");

    DeploymentOptions ordinary;
    ordinary.duration = nan;
    ordinary.delay = -10;
    motion.begin({{"badge", {}, false, .5, 0, nan}}, true, 106, 42, ordinary);
    near(motion.tracks()[0].sequence.delay + motion.tracks()[0].sequence.duration, .42, 1e-15,
         "Source nonfinite duration/delay uses opening fallback and zero delay");
    near(motion.opacity("badge", .5, 106), .5, 0, "Ordinary source apply has no visibility gate");
    ordinary.duration = 2;
    motion.begin({{"badge", {}, false, .5, 0}}, false, 107, 42, ordinary);
    near(motion.tracks()[0].sequence.delay + motion.tracks()[0].sequence.duration, .3, 1e-15,
         "Source retraction duration caps at three tenths");
    near(motion.opacity("badge", .5, 108), .5, 0,
         "Ungated completed retraction removes its additive track");
    rejects([&] { motion.requiresFrames(nan); }, "Nonfinite deployment frame clock rejects");
}
// An independent exported-format fixture, with no OS or real application data.
using FixtureTransform = std::function<void(std::string &, std::string_view)>;
void replaceOnce(std::string &bytes, std::string_view from, std::string_view to) {
    const auto position = bytes.find(from);
    if (position == std::string::npos)
        throw std::runtime_error("Missing fixture replacement marker");
    bytes.replace(position, from.size(), to);
}
Document fixture(FixtureTransform transform = {}) {
    static const std::map<std::string, std::string> resources = {
        {"scene", R"fixture({
  "root_node_id": "CAB:root",
  "nodes": [
    {
      "id": "CAB:root",
      "name": "CAB:root",
      "path": "fixture/CAB:root",
      "parent_id": null,
      "child_ids": [
        "CAB:group",
        "CAB:view",
        "CAB:mesh"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 100,
            "y": 100
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:Canvas",
          "script": "Canvas",
          "type": "MonoBehaviour",
          "data": {
            "m_SortingOrder": 1000,
            "m_OverrideSorting": false,
            "m_VertexColorAlwaysGammaSpace": true
          }
        },
        {
          "id": "component:RectMask2D",
          "script": "RectMask2D",
          "type": "MonoBehaviour",
          "data": {}
        }
      ]
    },
    {
      "id": "CAB:group",
      "name": "CAB:group",
      "path": "fixture/CAB:group",
      "parent_id": "CAB:root",
      "child_ids": [
        "CAB:-9007199254740993",
        "CAB:ignored"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 200,
            "y": 80
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:Canvas",
          "script": "Canvas",
          "type": "MonoBehaviour",
          "data": {
            "m_SortingOrder": 777,
            "m_OverrideSorting": false,
            "m_VertexColorAlwaysGammaSpace": true
          }
        },
        {
          "id": "component:UISortingOrder",
          "script": "UISortingOrder",
          "type": "MonoBehaviour",
          "data": {
            "m_Enabled": false,
            "_renderType": 1,
            "_sortingOrderOffset": 7
          }
        },
        {
          "id": "component:HorizontalLayoutGroup",
          "script": "HorizontalLayoutGroup",
          "type": "MonoBehaviour",
          "data": {
            "m_ChildControlWidth": true,
            "m_ChildControlHeight": false,
            "m_ChildAlignment": 0,
            "m_Spacing": 10,
            "m_Padding": {
              "m_Left": 5,
              "m_Right": 5,
              "m_Top": 0,
              "m_Bottom": 0
            }
          }
        }
      ]
    },
    {
      "id": "CAB:-9007199254740993",
      "name": "CAB:-9007199254740993",
      "path": "fixture/CAB:-9007199254740993",
      "parent_id": "CAB:group",
      "child_ids": [
        "CAB:hover"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 20,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "CAB:image",
          "script": "UIImage",
          "type": "MonoBehaviour",
          "data": {
            "m_Color": {
              "r": 1,
              "g": 1,
              "b": 1,
              "a": 1
            },
            "m_RaycastTarget": true
          }
        },
        {
          "id": "component:UIButton",
          "script": "UIButton",
          "type": "MonoBehaviour",
          "data": {
            "m_Transition": 1,
            "m_Interactable": true,
            "m_TargetGraphic": {
              "target_id": "CAB:image"
            },
            "m_Colors": {
              "m_NormalColor": {
                "r": 0.6,
                "g": 0.6,
                "b": 0.6,
                "a": 1
              },
              "m_HighlightedColor": {
                "r": 1,
                "g": 1,
                "b": 1,
                "a": 1
              },
              "m_PressedColor": {
                "r": 0.2,
                "g": 0.2,
                "b": 0.2,
                "a": 1
              },
              "m_SelectedColor": {
                "r": 0.4,
                "g": 0.4,
                "b": 0.4,
                "a": 1
              },
              "m_DisabledColor": {
                "r": 0,
                "g": 0,
                "b": 0,
                "a": 1
              },
              "m_ColorMultiplier": 1,
              "m_FadeDuration": 0.2
            }
          }
        },
        {
          "id": "component:LayoutElement",
          "script": "LayoutElement",
          "type": "MonoBehaviour",
          "data": {
            "m_MinWidth": 10,
            "m_PreferredWidth": 20,
            "m_FlexibleWidth": 0
          }
        }
      ]
    },
    {
      "id": "CAB:hover",
      "name": "CAB:hover",
      "path": "fixture/CAB:hover",
      "parent_id": "CAB:-9007199254740993",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": false
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 20,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": []
    },
    {
      "id": "CAB:ignored",
      "name": "CAB:ignored",
      "path": "fixture/CAB:ignored",
      "parent_id": "CAB:group",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 40,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:LayoutElement",
          "script": "LayoutElement",
          "type": "MonoBehaviour",
          "data": {
            "m_Enabled": false,
            "m_IgnoreLayout": true
          }
        }
      ]
    },
    {
      "id": "CAB:view",
      "name": "CAB:view",
      "path": "fixture/CAB:view",
      "parent_id": "CAB:root",
      "child_ids": [
        "CAB:content"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 80,
            "y": 50
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:UIScrollRect",
          "script": "UIScrollRect",
          "type": "MonoBehaviour",
          "data": {
            "m_Vertical": true,
            "m_Content": {
              "target_id": "CAB:content"
            },
            "m_Viewport": {
              "target_id": "CAB:view"
            },
            "m_ScrollSensitivity": 2
          }
        },
        {
          "id": "component:UIScrollCellSlantEffect",
          "script": "UIScrollCellSlantEffect",
          "type": "MonoBehaviour",
          "data": {
            "_bottomY": -100,
            "_topY": 100,
            "_leftX": 5,
            "_maxWidth": 20,
            "_cells": [
              {
                "target_id": "CAB:content"
              }
            ],
            "_curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 0,
                  "inSlope": 1,
                  "outSlope": 1,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 1,
                  "inSlope": 1,
                  "outSlope": 1,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ]
            }
          }
        }
      ]
    },
    {
      "id": "CAB:content",
      "name": "CAB:content",
      "path": "fixture/CAB:content",
      "parent_id": "CAB:view",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 80,
            "y": 150
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "CAB:scrollimage",
          "script": "UIImage",
          "type": "MonoBehaviour",
          "data": {
            "m_Color": {
              "r": 1,
              "g": 1,
              "b": 1,
              "a": 1
            }
          }
        }
      ]
    },
    {
      "id": "CAB:mesh",
      "name": "CAB:mesh",
      "path": "fixture/CAB:mesh",
      "parent_id": "CAB:root",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 20,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:MeshFilter",
          "script": "MeshFilter",
          "type": "MonoBehaviour",
          "data": {
            "m_Mesh": {
              "target_id": "CAB:meshdata"
            }
          }
        },
        {
          "id": "component:MeshRenderer",
          "script": "MeshRenderer",
          "type": "MonoBehaviour",
          "data": {
            "m_SortingOrder": 99,
            "m_Materials": [
              {
                "target_id": "CAB:material"
              }
            ]
          }
        },
        {
          "id": "component:UISortingOrder",
          "script": "UISortingOrder",
          "type": "MonoBehaviour",
          "data": {
            "m_Enabled": false,
            "_renderType": 0,
            "_sortingOrderOffset": -5
          }
        }
      ]
    }
  ]
})fixture"},
        {"clips", R"fixture({
  "clips": [
    {
      "binding": "_animationIn",
      "id": "_animationIn",
      "last_key_time": 1,
      "curves": []
    },
    {
      "binding": "_animationLoop",
      "id": "_animationLoop",
      "last_key_time": 1,
      "curves": []
    },
    {
      "binding": "_animationOut",
      "id": "_animationOut",
      "last_key_time": 1,
      "curves": []
    },
    {
      "binding": "Animator",
      "id": "Normal",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 0,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 0,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    },
    {
      "binding": "Animator",
      "id": "Highlighted",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 0,
                  "inSlope": -10,
                  "outSlope": -10,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": -10,
                  "inSlope": -10,
                  "outSlope": -10,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    },
    {
      "binding": "Animator",
      "id": "Pressed",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": -20,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": -20,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    },
    {
      "binding": "Animator",
      "id": "Disabled",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 30,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 30,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    }
  ],
  "controller_instances": [
    {
      "root_node_id": "CAB:-9007199254740993",
      "states": [
        {
          "name": "Normal",
          "bound_clip_id": "Normal",
          "source_clip_id": "Normal"
        },
        {
          "name": "Highlighted",
          "bound_clip_id": "Highlighted",
          "source_clip_id": "Highlighted"
        },
        {
          "name": "Pressed",
          "bound_clip_id": "Pressed",
          "source_clip_id": "Pressed"
        },
        {
          "name": "Disabled",
          "bound_clip_id": "Disabled",
          "source_clip_id": "Disabled"
        }
      ]
    }
  ]
})fixture"},
        {"controller-transitions", R"fixture({
  "controllers": [
    {
      "id": "controller",
      "state_machines": [
        {
          "index": 0,
          "states": [
            {
              "index": 0,
              "name": "Normal",
              "source_clip_id": "Normal",
              "speed": 1,
              "cycle_offset": 0
            },
            {
              "index": 1,
              "name": "Highlighted",
              "source_clip_id": "Highlighted",
              "speed": 2,
              "cycle_offset": 0
            },
            {
              "index": 2,
              "name": "Pressed",
              "source_clip_id": "Pressed",
              "speed": 1,
              "cycle_offset": 0
            },
            {
              "index": 3,
              "name": "Disabled",
              "source_clip_id": "Disabled",
              "speed": 1,
              "cycle_offset": 0
            }
          ]
        }
      ]
    }
  ],
  "instances": [
    {
      "root_node_id": "CAB:-9007199254740993",
      "controller_id": "controller",
      "source_interactable": true,
      "hover_enable_node_id": "CAB:hover",
      "transitions": [
        {
          "source_state_index": null,
          "destination_name": "Normal",
          "destination_bound_clip_id": "Normal",
          "destination_source_clip_id": "Normal",
          "duration": 0.2,
          "has_fixed_duration": true,
          "can_transition_to_self": false
        },
        {
          "source_state_index": null,
          "destination_name": "Highlighted",
          "destination_bound_clip_id": "Highlighted",
          "destination_source_clip_id": "Highlighted",
          "duration": 0,
          "has_fixed_duration": true,
          "can_transition_to_self": false
        },
        {
          "source_state_index": null,
          "destination_name": "Pressed",
          "destination_bound_clip_id": "Pressed",
          "destination_source_clip_id": "Pressed",
          "duration": 0.25,
          "has_fixed_duration": false,
          "can_transition_to_self": false
        },
        {
          "source_state_index": null,
          "destination_name": "Disabled",
          "destination_bound_clip_id": "Disabled",
          "destination_source_clip_id": "Disabled",
          "duration": 0,
          "has_fixed_duration": true,
          "can_transition_to_self": true
        }
      ]
    }
  ]
})fixture"},
        {"runtime-root-camera", R"fixture([
  {
    "type": "RectTransform",
    "cab": "Runtime",
    "path_id": "1",
    "data": {
      "m_LocalPosition": {
        "x": 0,
        "y": 0,
        "z": 100
      },
      "m_LocalScale": {
        "x": 1,
        "y": 1,
        "z": 1
      },
      "m_LocalRotation": {
        "x": 0,
        "y": 0,
        "z": 0,
        "w": 1
      },
      "m_AnchorMin": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchorMax": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchoredPosition": {
        "x": 0,
        "y": 0
      },
      "m_SizeDelta": {
        "x": 20,
        "y": 10
      },
      "m_Pivot": {
        "x": 0.5,
        "y": 0.5
      },
      "m_Father": {
        "m_FileID": 0,
        "m_PathID": "0"
      },
      "m_GameObject": {
        "m_PathID": "10"
      }
    }
  },
  {
    "type": "Transform",
    "cab": "Runtime",
    "path_id": "2",
    "data": {
      "m_LocalPosition": {
        "x": 0,
        "y": 0,
        "z": 0
      },
      "m_LocalScale": {
        "x": 1,
        "y": 1,
        "z": 1
      },
      "m_LocalRotation": {
        "x": 0,
        "y": 0,
        "z": 0,
        "w": 1
      },
      "m_AnchorMin": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchorMax": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchoredPosition": {
        "x": 0,
        "y": 0
      },
      "m_SizeDelta": {
        "x": 20,
        "y": 10
      },
      "m_Pivot": {
        "x": 0.5,
        "y": 0.5
      },
      "m_Father": {
        "m_FileID": 0,
        "m_PathID": "0"
      },
      "m_GameObject": {
        "m_PathID": "20"
      }
    }
  },
  {
    "type": "MonoBehaviour",
    "cab": "Runtime",
    "script": {
      "m_ClassName": "UIGyroscopeEffect"
    },
    "data": {
      "m_GameObject": {
        "m_PathID": "10"
      },
      "x": {
        "valueCurve": {
          "m_Curve": [
            {
              "time": 0,
              "value": -1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        },
        "maxAngle": 2
      },
      "y": {
        "valueCurve": {
          "m_Curve": [
            {
              "time": 0,
              "value": -1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        },
        "maxAngle": 3
      },
      "time": 0.2,
      "ease": 6,
      "enableDetect": true
    }
  },
  {
    "type": "MonoBehaviour",
    "cab": "Runtime",
    "script": {
      "m_ClassName": "UICanvasScaleHelper"
    },
    "data": {
      "m_GameObject": {
        "m_PathID": "10"
      }
    }
  },
  {
    "type": "Camera",
    "cab": "Runtime",
    "data": {
      "m_GameObject": {
        "m_PathID": "20"
      },
      "field of view": 60,
      "near clip plane": 0.3,
      "far clip plane": 1000,
      "m_NormalizedViewPortRect": {
        "x": 0,
        "y": 0,
        "width": 1,
        "height": 1
      },
      "m_LensShift": {
        "x": 0,
        "y": 0
      }
    }
  }
])fixture"},
        {"watch-blur", R"fixture({
  "wrapper": {
    "_options": {
      "animEase": 1
    }
  },
  "default_speed": 1,
  "entrance": {
    "curve": {
      "raw": {
        "curve": {
          "m_Curve": [
            {
              "time": 0,
              "value": 0,
              "inSlope": 1,
              "outSlope": 1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 1,
              "inSlope": 1,
              "outSlope": 1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        }
      }
    }
  },
  "exit": {
    "curve": {
      "raw": {
        "curve": {
          "m_Curve": [
            {
              "time": 0,
              "value": 1,
              "inSlope": -1,
              "outSlope": -1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 0,
              "inSlope": -1,
              "outSlope": -1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        }
      }
    }
  }
})fixture"},
        {"sprites", R"fixture({
  "sprites": [],
  "source_textures": []
})fixture"},
        {"materials", R"fixture({
  "materials": [
    {
      "id": "CAB:material",
      "data": {
        "m_SavedProperties": {
          "m_TexEnvs": [
            [
              "_MainTex",
              {
                "m_Texture": {
                  "target_id": "CAB:-17"
                }
              }
            ]
          ]
        }
      }
    }
  ]
})fixture"},
        {"Equipring", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
        {"watchline", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
        {"Plane", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
        {"Cylinder", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
    };
    return Document::load("fixture/Scene", [transform](const auto &path) {
        const auto name = path.stem().string();
        auto bytes = resources.at(name);
        if (transform)
            transform(bytes, name);
        return bytes;
    });
}
double area(const FilledGeometry &geometry) {
    double result = 0;
    for (const auto &q : geometry.quads) {
        double twice = 0;
        for (int i = 0; i < 4; ++i)
            twice += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
        result += std::abs(twice) * 0.5;
    }
    return result;
}
void fills() {
    const Rect rect{{-4, 2}, {12, 8}};
    const std::array<Vec2, 4> uv = {Vec2{.2, .3}, Vec2{.2, .9}, Vec2{.8, .9}, Vec2{.8, .3}};
    for (int method = 0; method <= 4; ++method)
        for (int origin = 0; origin < (method < 2 ? 2 : 4); ++origin)
            for (bool clockwise : {false, true}) {
                auto empty = filledGeometry(rect, uv, method, origin, clockwise, .0009);
                check(empty.quads.empty(), "Source filled mesh threshold omits tiny fills");
                auto full = filledGeometry(rect, uv, method, origin, clockwise, 1);
                check(full.quads.size() == 1, "Full source fill retains one quad");
                near(area(full), 96, 1e-12, "Full source fill covers authored rectangle");
                auto half = filledGeometry(rect, uv, method, origin, clockwise, .5);
                near(area(half), 48, 1e-10, "Source cardinal half fill covers half rectangle");
                for (std::size_t i = 0; i < half.quads.size(); ++i)
                    for (int vertex = 0; vertex < 4; ++vertex) {
                        near(half.uvQuads[i][vertex].x,
                             .2 + (half.quads[i][vertex].x + 4) / 12 * .6, 1e-12,
                             "Radial cutting preserves source U mapping");
                        near(half.uvQuads[i][vertex].y, .3 + (half.quads[i][vertex].y - 2) / 8 * .6,
                             1e-12, "Radial cutting preserves source V mapping");
                    }
            }
    auto quarter = filledGeometry(rect, uv, 4, 0, true, .25);
    near(area(quarter), 24, 1e-10, "Source radial360 quarter fill");
    auto right = filledGeometry(rect, uv, 0, 1, true, .25);
    near(right.quads.front()[0].x, 5, 1e-12,
         "Horizontal right-origin crop starts at original fraction");
    rejects([&] { filledGeometry(rect, uv, 4, 0, true, std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite fill cannot enter vertex stream");
}
void layoutAndButtons() {
    auto doc = fixture();
    ButtonMotion motion(doc);
    const SourceId button = "CAB:-9007199254740993";
    auto input = [&](double time, double scroll = 1) {
        FrameInput in{{1920, 1080}, {Phase::visible, 1, {}, {}, 0}, {}};
        in.interaction = &motion;
        in.time = time;
        in.verticalNormalizedPosition = scroll;
        return in;
    };
    auto graphic = [&](const Frame &frame) -> const Graphic & {
        for (const auto &g : frame.graphics)
            if (g.componentId == "CAB:image")
                return g;
        throw std::runtime_error("Missing fixture Graphic");
    };
    check(motion.instanceIds().size() == 1,
          "Per-instance controller and tint share original button ID");
    motion.reset(10);
    auto normal = doc.frame(input(10));
    const auto *node = normal.node(button);
    check(node && node->rect, "Signed button instance retained in frame");
    near(node->rect->size.x, 20, 1e-12, "LayoutElement preferred width controls source layout");
    near(node->sceneWorld.values[12], -85, 1e-12,
         "Layout ignores disabled ILayoutIgnorer child and drives controller X");
    near(node->sceneWorld.values[13], 35, 1e-12,
         "Source group top-origin anchoring drives controller Y");
    check(node->sortingOrder == 1007,
          "Disabled registered canvas sorting writer receives panel base directly");
    check(graphic(normal).masks.empty(), "Override canvas clears inherited mask chain");
    check(graphic(normal).vertexColorReady, "UI vertex tint/color-space policy finalized once");
    near(graphic(normal).color[0], static_cast<float>(.6), 1e-8,
         "Default source normal ColorTint applies independently of Graphic color");
    check(normal.graphics.front().sortingOrder == -5 &&
              normal.graphics.front().textureId == "CAB:-17",
          "Renderer writer uses standalone order and original material texture ID");
    check(normal.scroll.has_value(), "Source ScrollRect produces bounds for native wheel owner");
    near(normal.scroll->hiddenLength, 100, 1e-12, "Original viewport/content scroll hidden length");
    near(normal.node("CAB:content")->sceneWorld.values[13], -50, 1e-12,
         "Normalized top aligns source upper bounds");
    near(normal.node("CAB:content")->sceneWorld.values[12], 10, 1e-12,
         "Source slant writer evaluates original Y curve");
    auto bottom = doc.frame(input(10, 0));
    near(bottom.node("CAB:content")->sceneWorld.values[13], 50, 1e-12,
         "Normalized bottom aligns source lower bounds");
    near(bottom.node("CAB:content")->sceneWorld.values[12], 20, 1e-12,
         "Scroll updates slant position from finalized Y");
    auto rawOverscroll = doc.frame(input(10, -.5));
    near(rawOverscroll.scroll->normalizedPosition, 0, 0,
         "Raw reference scroll keeps original normalized clamp outside desktop navigation");
    near(rawOverscroll.node("CAB:content")->sceneWorld.values[13],
         bottom.node("CAB:content")->sceneWorld.values[13], 0,
         "Desktop rebound allowance cannot change raw reference scroll geometry");
    auto baseInput = input(10);
    baseInput.panelBase = 2000;
    check(doc.frame(baseInput).node(button)->sortingOrder == 2007,
          "Panel base never accumulates parent sorting offset");
    motion.setHovered(true, button, 10);
    auto halfTint = doc.frame(input(10.1));
    near(graphic(halfTint).color[0], .8, 1e-7, "Source ColorTween uses linear Float channel fade");
    motion.setHovered(true, button, 10.1);
    auto endpoint = doc.frame(input(11));
    near(endpoint.node(button)->sceneWorld.values[14], -10, 1e-12,
         "Highlighted finite endpoint ignores source clip loop flag");
    check(!motion.requiresFrames(11), "Settled Highlighted and tint stop demand scheduling");
    check(endpoint.node("CAB:hover")->active, "Highlighted enables original hover transform");
    motion.setState(ButtonState::selected, button, 11, true);
    near(graphic(doc.frame(input(11))).color[0], .4, 1e-7,
         "Selectable selected color has independent renderer channel");
    motion.setEnabled(false, button, 11);
    near(graphic(doc.frame(input(11))).color[0], 1, 1e-12,
         "Component disable clears CanvasRenderer tint to white");
    motion.setEnabled(true, button, 11);
    near(graphic(doc.frame(input(11))).color[0], .6, 1e-7,
         "Component enable resets source selection instantly");
    motion.reset(100);
    motion.setHovered(true, button, 100);
    near(doc.frame(input(100.125)).node(button)->sceneWorld.values[14], -2.5, 1e-10,
         "Controller speed samples source finite local time without wrapper easing");
    motion.setState(ButtonState::pressed, button, 100.125);
    auto pressed = doc.frame(input(100.1875));
    near(pressed.node(button)->sceneWorld.values[14], -11.875, 1e-10,
         "Normalized transition scales by previous length/speed and samples moving origin");
    motion.setHovered(false, button, 100.1875);
    check(motion.state(button) == ButtonState::pressed &&
              !doc.frame(input(100.1875)).node("CAB:hover")->active,
          "Leaving while pressed clears hover separately from state");
    motion.setState(ButtonState::normal, button, 100.1875);
    near(doc.frame(input(100.1875)).node(button)->sceneWorld.values[14], -11.875, 1e-10,
         "Interrupted blend snapshots current channels without snapping");
    near(doc.frame(input(100.2875)).node(button)->sceneWorld.values[14], -5.9375, 1e-10,
         "Interrupted transition blends from snapshot channels");
    near(doc.frame(input(100.2)).node(button)->sceneWorld.values[14], -5.9375, 1e-10,
         "Controller sampler monotonic clock cannot regress");
    motion.setState(ButtonState::highlighted, button, 200, true);
    auto reduced = input(200);
    reduced.reduceMotion = true;
    near(doc.frame(reduced).node(button)->sceneWorld.values[14], -10, 1e-12,
         "Reduced motion seeks source controller endpoint");
    check(!motion.requiresFrames(200), "Reduced controller/tint states require no perpetual timer");
    auto projected = doc.frame(reduced);
    const auto *allocation = projected.graphics.front().quads.data();
    Quaternion tilt{0, std::sin(.05), 0, std::cos(.05)};
    doc.reproject(projected, tilt);
    reduced.rootRotation = tilt;
    auto rebuilt = doc.frame(reduced);
    check(projected.graphics.front().quads.data() == allocation,
          "Slant reproject retains finalized geometry allocation");
    for (const auto &n : projected.nodes)
        for (int axis = 0; axis < 16; ++axis)
            near(n.world.values[axis], rebuilt.node(n.id)->world.values[axis], 1e-9,
                 "Gyro-only slant path equals full source layout frame");
    auto other = fixture();
    rejects([&] { other.frame(reduced); },
            "Button state cannot cross immutable document ownership");
}
const Graphic *findGraphic(const Frame &frame, std::string_view id) {
    for (const auto &graphic : frame.graphics)
        if (graphic.componentId == id)
            return &graphic;
    return nullptr;
}
void shaderChannelsAndDeployment() {
    FrameInput input{{1920, 1080}, {Phase::visible, 1, {}, {}, 0}, {}};
    input.time = 12.375;
    auto ordinary = fixture();
    auto original = ordinary.frame(input);
    near(original.sceneTime, input.time, 0, "FX uniforms share the submitted source frame clock");
    near(original.nodes.front().localOpacity, 1, 0,
         "Source node snapshot exposes local group opacity independently of inheritance");
    check(original.graphics.front().colorQuads.empty(),
          "Missing source mesh colors preserves implicit raw white channels");
    auto colored = fixture([](auto &bytes, auto name) {
        if (name == "Equipring" || name == "watchline" || name == "Plane" || name == "Cylinder")
            replaceOnce(bytes, R"("indices": [)",
                        R"("colors": [[1,0,0,0.25],[0.2,0.4,0.6,0.5],[0,0,1,0.75]], "indices": [)");
    });
    auto colors = colored.frame(input);
    const auto &mesh = colors.graphics.front();
    check(mesh.kind == "MeshRenderer" && mesh.colorQuads.size() == mesh.quads.size(),
          "Original mesh color channels align with emitted source triangle quads");
    near(mesh.colorQuads[0][0][3], .25, 0, "Mesh first source vertex retains authored alpha");
    near(mesh.colorQuads[0][1][0], .2, 0,
         "Mesh raw RGB bypasses UI Color32 quantization and Canvas gamma conversion");
    check(mesh.colorQuads[0][2] == mesh.colorQuads[0][3],
          "Degenerate triangle fourth vertex repeats original color and UV corner");
    rejects(
        [] {
            fixture([](auto &bytes, auto name) {
                if (name == "Equipring")
                    replaceOnce(bytes, R"("indices": [)", R"("colors": [[1,1,1,1]], "indices": [)");
            });
        },
        "Nonempty source color channel must match bounded position count");
    rejects(
        [] {
            fixture([](auto &bytes, auto name) {
                if (name == "Equipring")
                    replaceOnce(bytes, R"("indices": [)",
                                R"("colors": [[1,1,1,2],[1,1,1,1],[1,1,1,1]], "indices": [)");
            });
        },
        "Unbounded source color values cannot enter the native FX vertex stream");
    rejects(
        [] {
            fixture([](auto &bytes, auto name) {
                if (name == "Equipring")
                    replaceOnce(bytes, R"("indices": [)",
                                R"("colors": [[1,1,1],[1,1,1,1],[1,1,1,1]], "indices": [)");
            });
        },
        "Original color channels must preserve all four RGBA components");

    const std::string graphicAnimation = R"("id": "CAB:animation", "script": "UIGraphicAnimation",
      "data": {"_scale":{"x":2,"y":0.5},"_alpha":0.4,"_material":{"target_id":"CAB:cloned"}}},
      {"id": "CAB:image",)";
    auto animated = fixture([&](auto &bytes, auto name) {
        if (name == "scene")
            replaceOnce(bytes, R"("id": "CAB:image",)", graphicAnimation);
    });
    auto scaled = animated.frame(input);
    const auto *image = findGraphic(scaled, "CAB:image");
    check(image != nullptr, "Original UIGraphicAnimation retains UI geometry batch");
    near(image->sampledProperties.at("material._VFXMainTex_ST.x"), .5, 0,
         "Source animated material X writes inverse scale");
    near(image->sampledProperties.at("material._VFXMainTex_ST.y"), 2, 0,
         "Source animated material Y writes independent inverse scale");
    near(image->sampledProperties.at("material._VFXMainTex_ST.z"), .25, 0,
         "Source animated material U centers inverse scale");
    near(image->sampledProperties.at("material._VFXMainTex_ST.w"), -.5, 0,
         "Source animated material V centers inverse scale");
    near(image->sampledProperties.at("material._TintColorAlpha"), static_cast<double>(.4f), 0,
         "Source animated material alpha preserves final Float uniform boundary");
    check(image->world.values == findGraphic(original, "CAB:image")->world.values,
          "UIGraphicAnimation material scale never changes source scene transform");
    auto curve = [](std::string attribute, double first, double last, double slope) {
        auto key = [&](double time, double value) {
            return std::string("{\"time\":") + std::to_string(time) +
                   ",\"value\":" + std::to_string(value) + ",\"inSlope\":" + std::to_string(slope) +
                   ",\"outSlope\":" + std::to_string(slope) +
                   ",\"inWeight\":0.3333333333333333,\"outWeight\":0.3333333333333333}";
        };
        return std::string("{\"group\":\"m_FloatCurves\",\"attribute\":\"") + attribute +
               "\",\"class_id\":114,\"node_matches\":[\"CAB:-9007199254740993\"],\"raw\":{"
               "\"curve\":{"
               "\"m_PreInfinity\":2,\"m_PostInfinity\":2,\"m_Curve\":[" +
               key(0, first) + "," + key(1, last) + "]}}}";
    };
    auto animatedPose = fixture([&](auto &bytes, auto name) {
        if (name == "scene")
            replaceOnce(bytes, R"("id": "CAB:image",)", graphicAnimation);
        else if (name == "clips")
            replaceOnce(bytes, R"("curves": [])",
                        "\"curves\":[" + curve("_scale.x", 1, 3, 2) + "," +
                            curve("_alpha", 0, 1, 1) + "]");
    });
    auto poseInput = input;
    poseInput.playback.entranceTime = .5;
    auto midPose = animatedPose.frame(poseInput);
    near(findGraphic(midPose, "CAB:image")->sampledProperties.at("material._VFXMainTex_ST.x"), .5,
         0, "UIGraphicAnimation inverse samples the current source Float curve pose");
    near(findGraphic(midPose, "CAB:image")->sampledProperties.at("material._TintColorAlpha"), .5, 0,
         "UIGraphicAnimation alpha samples the same source animation pose");
    poseInput.playback.entranceTime = 1;
    auto endPose = animatedPose.frame(poseInput);
    near(findGraphic(endPose, "CAB:image")->sampledProperties.at("material._VFXMainTex_ST.x"),
         static_cast<double>(1.0f / 3), 0,
         "Material inverse retains source final Float precision at a nonintegral endpoint");
    auto raw = fixture([&](auto &bytes, auto name) {
        if (name == "scene") {
            replaceOnce(bytes, R"("id": "CAB:image",)", graphicAnimation);
            replaceOnce(bytes, R"("script": "UIImage")", R"("script": "UIRawImage")");
        }
    });
    check(findGraphic(raw.frame(input), "CAB:image")->materialId == "CAB:cloned",
          "Raw source image takes UIGraphicAnimation cloned material binding");
    auto zero = fixture([&](auto &bytes, auto name) {
        if (name == "scene") {
            auto animation = graphicAnimation;
            replaceOnce(animation, R"("x":2,"y":0.5)", R"("x":0,"y":1e-45)");
            replaceOnce(bytes, R"("id": "CAB:image",)", animation);
        }
    });
    auto tiny = zero.frame(input);
    near(findGraphic(tiny, "CAB:image")->sampledProperties.at("material._VFXMainTex_ST.x"), 0, 0,
         "Unity approximately-zero material scale uses exact subnormal threshold");
    near(findGraphic(tiny, "CAB:image")->sampledProperties.at("material._VFXMainTex_ST.y"), 0, 0,
         "Subnormal material Y follows zero inverse policy without transform collapse");
    auto disabled = fixture([&](auto &bytes, auto name) {
        if (name == "scene") {
            auto animation = graphicAnimation;
            replaceOnce(animation, R"("data": {)", R"("data": {"m_Enabled":false,)");
            replaceOnce(bytes, R"("id": "CAB:image",)", animation);
        }
    });
    auto disabledFrame = disabled.frame(input);
    check(!findGraphic(disabledFrame, "CAB:image")
               ->sampledProperties.contains("material._TintColorAlpha"),
          "Disabled UIGraphicAnimation never refreshes native material uniforms");

    auto groupDocument = fixture([](auto &bytes, auto name) {
        if (name == "scene")
            replaceOnce(bytes, R"("id": "component:HorizontalLayoutGroup",)",
                        R"("id":"CAB:groupOpacity","script":"CanvasGroup","data":{"m_Alpha":0.8}},
                           {"id": "component:HorizontalLayoutGroup",)");
    });
    auto settled = groupDocument.frame(input);
    near(settled.node("CAB:group")->localOpacity, .8, 0,
         "Deployment captures local source group model opacity before fold");
    auto groups = groupDocument.deploymentGroups(
        settled, {{"CAB:group", 160}, {"CAB:-9007199254740993", 170}});
    check(groups.size() == 2 && groups[1].ancestors.front() == "CAB:group" &&
              groups[0].settledCenterY == 160,
          "Explicit scene candidate API derives source ancestry and preserves caller "
          "coordinates/order");
    DeploymentFlicker deployment;
    deployment.begin(groups, true, input.time, 42, DeploymentOptions::legacySweep(true));
    check(deployment.tracks().size() == 1,
          "Scene ancestor plus its label emits one additive source group track");
    input.deployment = &deployment;
    auto gated = groupDocument.frame(input);
    check(findGraphic(gated, "CAB:image") == nullptr,
          "Explicit opening gate applies to all descendant graphics of selected source group");
    check(gated.hits.size() == settled.hits.size(),
          "Flicker appearance leaves source input/controller contracts independent");
    check(gated.graphics.front().kind == "MeshRenderer" && gated.graphics.front().color[3] == 1,
          "Unselected artwork and backdrop receive no invented deployment group");
    input.reduceMotion = true;
    auto reduced = groupDocument.frame(input);
    near(findGraphic(reduced, "CAB:image")->color[3], findGraphic(settled, "CAB:image")->color[3],
         0, "Reduced-motion source frame bypasses finite deployment dropout");
    input.reduceMotion = false;
    input.time += 1;
    auto completed = groupDocument.frame(input);
    near(findGraphic(completed, "CAB:image")->color[3], findGraphic(settled, "CAB:image")->color[3],
         0, "Exact completed deployment restores original reveal alpha without modifying curves");
    const auto clock = completed.sceneTime;
    groupDocument.reproject(completed, {});
    near(completed.sceneTime, clock, 0,
         "Pointer-only reproject preserves submitted material clock");
    rejects([&] { ordinary.deploymentGroups(settled, {{"CAB:group", 0}}); },
            "Settled grouping snapshot cannot cross immutable document ownership");
    rejects([&] { groupDocument.deploymentGroups(settled, {{"unknown", 0}}); },
            "Explicit scene grouping rejects unknown source identities");
    input.playback.phase = Phase::concealed;
    auto concealed = groupDocument.frame(input);
    near(concealed.sceneTime, input.time, 0,
         "Concealed source snapshot still preserves explicit clock");
    rejects([&] { groupDocument.deploymentGroups(concealed, {{"CAB:group", 0}}); },
            "Concealed scene has no settled geometry candidate snapshot");
    input.time = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { groupDocument.frame(input); },
            "Nonfinite scene material clock rejects before shader upload");
}
const Graphic *findNodeGraphic(const Frame &frame, std::string_view id) {
    for (const auto &graphic : frame.graphics)
        if (graphic.nodeId == id)
            return &graphic;
    return nullptr;
}
void desktopSource(const std::filesystem::path &path) {
    auto raw = Document::load(path);
    auto doc = Document::loadDesktop(path);
    const auto *info = doc.desktopInfo();
    check(info != nullptr && raw.desktopInfo() == nullptr,
          "Desktop mounting is explicit; the reference document stays raw");
    check(raw.nodeCount() == 789 && doc.nodeCount() == 834,
          "Canonical desktop graph mounts the exact 45-node BP13 card");
    check(raw.buttons().size() == 22 && doc.buttons().size() == 24,
          "Original main button order is retained with two explicit desktop supplements");
    check(info->profileRootId == "CAB-7979328e8a85d73c8b989cdca5a79bf8:-8536182027554143077" &&
              info->profileParentId == "CAB-194e41a66c2317b9df19269f505210be:7686435424458337495",
          "Canonical signed profile identities are preserved");
    FrameInput input{{1920, 1080}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, {}};
    input.reduceMotion = true;
    auto frame = doc.frame(input);
    auto reference = raw.frame(input);
    check(frame.scrollIndicators.size() == 2 && reference.scrollIndicators.empty(),
          "Only desktop frames expose the two canonical authored scroll arrow planes");
    check(info->scrollIndicatorIds.at(-1) ==
                  "CAB-194e41a66c2317b9df19269f505210be:3878725756700159191" &&
              info->scrollIndicatorIds.at(1) ==
                  "CAB-194e41a66c2317b9df19269f505210be:6424409055855602903",
          "Desktop uses outer30-unit arrow lines, excluding nested13-unit source duplicates");
    for (const auto &arrow : frame.scrollIndicators) {
        near(arrow.rect.size.y, 30, 0, "Canonical arrow hit plane retains source height");
        check(arrow.active && arrow.enabled == (arrow.direction > 0),
              "Top-of-list arrow availability follows bounded source target");
        const auto *graphic = findNodeGraphic(frame, arrow.nodeId);
        const auto *original = findNodeGraphic(reference, arrow.nodeId);
        check(graphic && original, "Source arrow artwork stays in the original render stream");
        near(graphic->color[0], arrow.enabled ? 1 : static_cast<double>(.32f), 0,
             "Arrow disabled tint retains source linear Float0.32");
        near(graphic->color[3],
             original->color[3] * (arrow.enabled ? 1 : static_cast<double>(.65f)), 1e-8,
             "Arrow limit opacity multiplies authored source alpha without replacing it");
        const Vec3 center{arrow.rect.origin.x + arrow.rect.size.x / 2,
                          arrow.rect.origin.y + arrow.rect.size.y / 2, 0};
        auto pixel = frame.camera.project(center, arrow.world);
        check(pixel && frame.scrollDirectionAt(*pixel) == arrow.direction,
              "Both enabled and disabled actual source arrows share projective input planes");
    }
    auto arrowInput = input;
    arrowInput.verticalNormalizedPosition = 1.2;
    arrowInput.desktopScrollTarget = .5;
    auto middleArrows = doc.frame(arrowInput);
    check(std::all_of(middleArrows.scrollIndicators.begin(), middleArrows.scrollIndicators.end(),
                      [](const auto &arrow) { return arrow.enabled; }),
          "Arrow availability follows motion target rather than overscrolling presentation");
    arrowInput.desktopScrollTarget = 0;
    auto endArrows = doc.frame(arrowInput);
    check(std::all_of(endArrows.scrollIndicators.begin(), endArrows.scrollIndicators.end(),
                      [](const auto &arrow) { return arrow.enabled == (arrow.direction < 0); }),
          "Bottom target enables up and dims down independently of lagging scroll position");
    Playback arrowPlayback(doc.entranceDuration(), doc.exitDuration());
    arrowPlayback.open(0);
    arrowInput.playback = arrowPlayback.sample(doc.entranceDuration() / 2);
    arrowInput.desktopScrollTarget = 1;
    auto enteringArrows = doc.frame(arrowInput);
    auto rawEnteringArrows = raw.frame(arrowInput);
    check(std::none_of(enteringArrows.scrollIndicators.begin(),
                       enteringArrows.scrollIndicators.end(),
                       [](const auto &arrow) { return arrow.active; }),
          "Mid-entrance arrow geometry is presented without enabling source input");
    const auto &up = info->scrollIndicatorIds.at(-1);
    const auto *entering = findNodeGraphic(enteringArrows, up);
    const auto *rawEntering = findNodeGraphic(rawEnteringArrows, up);
    check(entering && rawEntering, "Original arrow artwork is exercised during finite entrance");
    near(entering->color[3], rawEntering->color[3] * static_cast<double>(.65f), 1e-8,
         "Middle-transition disabled arrow opacity retains sampled source CanvasGroup alpha");
    arrowInput.playback = input.playback;
    arrowInput.desktopScrollTarget = std::numeric_limits<double>::infinity();
    rejects([&] { doc.frame(arrowInput); },
            "Nonfinite source scroll target cannot drive arrow states");
    near(doc.initialRootRotation().w, 1, 0, "Canonical initial camera root uses authored identity");
    const auto *profile = frame.node(info->profileRootId);
    const auto *parent = frame.node(info->profileParentId);
    check(profile && parent && profile->active && parent->active && profile->rect,
          "Desktop profile is mounted under the original visible deployment parent");
    check(profile->path.starts_with(parent->path + "/PlayInfoBp13Cell"),
          "Mounted source paths retain the original parent and selected card subtree");
    near(profile->rect->size.x, 364, 0, "Selected source profile width is unchanged");
    near(profile->rect->size.y, 128, 0, "Selected source profile height is unchanged");
    check(std::none_of(frame.graphics.begin(), frame.graphics.end(),
                       [](const auto &g) { return g.kind == "UIText"; }) &&
              std::any_of(reference.graphics.begin(), reference.graphics.end(),
                          [](const auto &g) { return g.kind == "UIText"; }),
          "Desktop suppresses raw game text while preserving the reference path");
    check(!info->hiddenNodeIds.empty() && !info->centerNodeId.empty() &&
              !info->statusNodeId.empty(),
          "Desktop retains explicit hidden-game, center and status source anchors");
    for (const auto &id : info->hiddenNodeIds) {
        const auto *node = frame.node(id);
        check(node && !node->active, "Game-only desktop nodes are inactive but queryable");
        check(std::none_of(frame.graphics.begin(), frame.graphics.end(),
                           [&](const auto &g) {
                               return g.path == node->path || g.path.starts_with(node->path + "/");
                           }),
              "Hidden game-only subtrees emit no source artwork");
    }
    for (const auto &button : doc.buttons()) {
        check(frame.node(button.captionNodeId) && frame.node(button.iconNodeId),
              "Native navigation caption and icon bindings use real source nodes");
        check(std::any_of(frame.hits.begin(), frame.hits.end(),
                          [&](const auto &hit) { return hit.buttonId == button.id; }),
              "Available desktop source buttons retain authored hit regions");
    }
    for (const auto *binding : {"button", "playerHeadBtn", "rightBtn"}) {
        const auto &id = info->profileBindings.at(binding);
        check(std::any_of(frame.hits.begin(), frame.hits.end(),
                          [&](const auto &hit) { return hit.buttonId == id; }),
              "Original profile hit regions remain available to the grouped native action");
    }
    const auto *background = findNodeGraphic(frame, info->profileBackgroundId);
    check(background &&
              background->textureId == "CAB-4a2267055b2c5652681adcf56a5c9ac3:-4827637915678035611",
          "Desktop selects the canonical business_card_topic_normal_1 sprite texture");
    for (const auto &id : info->profileGlowIds)
        if (id != info->profileHighlightId)
            check(findNodeGraphic(frame, id) == nullptr,
                  "Broad profile/portrait additive source glow is suppressed");
    std::size_t defaultStyles{};
    for (const auto &g : frame.graphics) {
        check(!g.normalMaterial,
              "Original desktop materials remain unchanged until replacement ready");
        if (g.path.find("/MiddleDecoNode/") == std::string::npos)
            continue;
        const double opacity = g.path.ends_with("/triangle_fx1") || g.path.ends_with("/RingFoMesh")
                                   ? .88
                               : g.path.ends_with("/EndfieldTextGlow") ? .78
                                                                       : 1;
        if (opacity == 1)
            continue;
        const auto *original = findNodeGraphic(reference, g.nodeId);
        check(original != nullptr, "Desktop restrained glow retains its original source graphic");
        near(g.color[3], original->color[3] * opacity, 1e-8,
             "Desktop glow opacity follows the exact source view constants");
        ++defaultStyles;
    }
    check(defaultStyles >= 2, "Actual desktop center glow policies are exercised");
    check(std::any_of(frame.nodes.begin(), frame.nodes.end(),
                      [](const auto &n) { return !n.masks.empty(); }),
          "Native source-plane bindings inherit real clipping masks");
    const auto &firstButton = doc.buttons().front();
    for (int i = 0; i < 16; ++i)
        near(frame.node(firstButton.id)->world.values[i],
             reference.node(firstButton.id)->world.values[i], 1e-9,
             "Desktop mounting leaves original left deployment geometry unchanged");
    check(info->navigationRows.size() == 9 && frame.desktopRightAssignments.size() == 18,
          "Fixed desktop fixture uses the exact nine-row two-column source pool");
    for (std::size_t row = 0; row < info->navigationRows.size(); ++row)
        for (std::size_t column = 0; column < 2; ++column)
            check(frame.desktopRightAssignments.at(info->navigationRows[row].buttons[column]) ==
                      row * 2 + column,
                  "Desktop entry assignments retain authored physical order");
    input.desktopEntryCount = 19;
    input.verticalNormalizedPosition = 0;
    auto partial = doc.frame(input);
    const auto &lastPhysical = info->navigationRows[0]; // logical row 9 wraps to slot zero.
    check(partial.desktopRightAssignments.at(lastPhysical.buttons[0]) == 18 &&
              !partial.desktopRightAssignments.contains(lastPhysical.buttons[1]),
          "Lone final entry keeps source first column in the recycled physical slot");
    const auto *partialRow = partial.node(lastPhysical.id);
    near(partialRow->rect->size.x, lastPhysical.size.x / 2, 1e-9,
         "Partial source row contracts width to avoid recentering the lone item");
    near(partialRow->rect->origin.x, -lastPhysical.size.x / 2, 1e-9,
         "Partial source row pivot retains original first-column origin");
    input.desktopEntryCount = 64;
    input.verticalNormalizedPosition = 1;
    auto top = doc.frame(input);
    input.verticalNormalizedPosition = 0;
    auto bottom = doc.frame(input);
    check(top.nodes.size() == bottom.nodes.size() && bottom.nodes.size() == 834 &&
              top.desktopRightAssignments.size() == 18 &&
              bottom.desktopRightAssignments.size() == 18,
          "Long desktop lists reuse bounded source nodes instead of cloning artwork");
    check(std::all_of(bottom.desktopRightAssignments.begin(), bottom.desktopRightAssignments.end(),
                      [](const auto &assignment) { return assignment.second >= 46; }),
          "Bottom scrolling binds the final logical desktop entries to the existing pool");
    check(top.scroll && bottom.scroll && top.scroll->contentId == info->navigationContentId &&
              top.scroll->hiddenLength > 0,
          "Desktop scroll bounds come from the canonical recycled content and viewport");
    input.verticalNormalizedPosition = 1.1;
    auto topRebound = doc.frame(input);
    const double topPresentation =
        DesktopScrollMotion::presentationPosition(1.1, top.scroll->hiddenLength);
    near(topRebound.scroll->normalizedPosition, topPresentation, 0,
         "Desktop layout preserves bounded spring presentation beyond normalized top");
    check(topRebound.desktopRightAssignments == top.desktopRightAssignments,
          "Overscroll keeps bounded logical-row assignment while source content rebounds");
    auto viewInverse = inverse(top.node(top.scroll->viewportId)->sceneWorld);
    check(viewInverse.has_value(), "Original scroll viewport inverse remains available");
    const auto topContent = *viewInverse * top.node(info->navigationContentId)->sceneWorld;
    const auto reboundContent =
        *viewInverse * topRebound.node(info->navigationContentId)->sceneWorld;
    near(reboundContent.values[13] - topContent.values[13],
         -(topPresentation - 1) * top.scroll->hiddenLength, 1e-4,
         "Visible source rebound retains exact normalized-position times hidden-length "
         "displacement");
    input.verticalNormalizedPosition = -.1;
    auto bottomRebound = doc.frame(input);
    const double bottomPresentation =
        DesktopScrollMotion::presentationPosition(-.1, bottom.scroll->hiddenLength);
    near(bottomRebound.scroll->normalizedPosition, bottomPresentation, 0,
         "Desktop layout preserves bounded spring presentation beyond normalized bottom");
    check(bottomRebound.desktopRightAssignments == bottom.desktopRightAssignments,
          "Bottom rebound never recycles away the final visible logical entries");
    input.desktopEntryCount = 18;
    input.verticalNormalizedPosition = 0;
    auto reachedBottom = doc.frame(input);
    const auto &lastButton = info->navigationRows.back().buttons.back();
    bool reachable = false;
    for (const auto &hit : reachedBottom.hits)
        if (hit.buttonId == lastButton) {
            Vec3 center{hit.rect.origin.x + hit.rect.size.x / 2,
                        hit.rect.origin.y + hit.rect.size.y / 2, 0};
            if (auto pixel = reachedBottom.camera.project(center, hit.world))
                reachable = reachable || reachedBottom.buttonAt(*pixel) == lastButton;
        }
    check(reachable,
          "Scrolled source bottom makes the last desktop slot inverse-hit-test reachable");
    input.desktopEntryCount = 64;
    input.verticalNormalizedPosition = std::numeric_limits<double>::quiet_NaN();
    check(doc.frame(input).desktopRightAssignments == top.desktopRightAssignments,
          "Desktop row sampling uses source finite-scroll fallback");
    input.desktopEntryCount = 0;
    input.verticalNormalizedPosition = 1;
    check(doc.frame(input).desktopRightAssignments.empty(),
          "Empty desktop lists disable all source pool slots safely");
    input.desktopEntryCount = 18;
    DesktopPresentation presentation;
    presentation.hiddenNodes.push_back(firstButton.iconNodeId);
    presentation.properties[info->profileBindings.at("levelSlider")]["m_FillAmount"] = .5;
    presentation.normalMaterialNodes.push_back(info->profileHighlightId);
    input.desktopPresentation = &presentation;
    ButtonMotion motion(doc);
    motion.reset(0, true);
    motion.setHovered(true, info->profileRootId, 1, true);
    input.interaction = &motion;
    input.time = 1;
    auto native = doc.frame(input);
    check(native.node(firstButton.iconNodeId) && !native.node(firstButton.iconNodeId)->active &&
              !findNodeGraphic(native, firstButton.iconNodeId),
          "Explicit native replacements hide source art while keeping authored geometry");
    const auto *highlight = findNodeGraphic(native, info->profileHighlightId);
    check(highlight && highlight->normalMaterial,
          "Normal alpha is explicit only for the caller's prepared profile hover replacement");
    // Exercise the same calibrated local mapping native overlays attach to an
    // existing source node, independently of their artwork/text provider.
    Graphic overlay;
    overlay.nodeId = firstButton.captionNodeId;
    overlay.nodeLocal = Mat4::identity();
    overlay.nodeLocal.values[0] = .4;
    overlay.nodeLocal.values[5] = .7;
    overlay.nodeLocal.values[12] = 12;
    overlay.nodeLocal.values[13] = -8;
    overlay.sceneWorld = native.node(overlay.nodeId)->sceneWorld * overlay.nodeLocal;
    overlay.world = native.worldRoot * overlay.sceneWorld;
    overlay.masks = native.node(overlay.nodeId)->masks;
    native.graphics.push_back(overlay);
    Graphic footer = overlay;
    footer.fixedWorld = true;
    native.graphics.push_back(footer);
    Quaternion tilt{0, std::sin(.025), 0, std::cos(.025)};
    doc.reproject(native, tilt);
    for (const auto &arrow : native.scrollIndicators) {
        const auto *node = native.node(arrow.nodeId);
        check(node && arrow.world.values == node->world.values,
              "Arrow draw/input metadata shares the reprojected source node transform");
        const Vec3 center{arrow.rect.origin.x + arrow.rect.size.x / 2,
                          arrow.rect.origin.y + arrow.rect.size.y / 2, 0};
        auto pixel = native.camera.project(center, arrow.world);
        check(pixel && native.scrollDirectionAt(*pixel) == arrow.direction,
              "Arrow projective hits survive camera-only live tilt");
    }
    const auto expected = native.node(overlay.nodeId)->world * overlay.nodeLocal;
    for (int i = 0; i < 16; ++i) {
        near(native.graphics[native.graphics.size() - 2].world.values[i], expected.values[i], 1e-9,
             "Source-plane local calibration survives slant and gyro reprojection");
        near(native.graphics.back().world.values[i], footer.world.values[i], 0,
             "Fixed footer remains in its original screen/design plane");
    }
    for (const auto &node : native.nodes)
        for (const auto &mask : node.masks)
            check(mask.world.values == (native.worldRoot * mask.sceneWorld).values,
                  "Native label clipping masks follow the reprojected source root");
    presentation.properties[firstButton.id]["m_Color.a"] = std::numeric_limits<double>::infinity();
    rejects([&] { doc.frame(input); }, "Nonfinite native desktop properties reject");
    auto reader = [&](const auto &file) {
        std::ifstream stream(file, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        check(!bytes.empty(), "Fresh actual source fixture files are available");
        if (file.filename() == "desktop-profile-card.json") {
            const auto position = bytes.find(info->profileParentId);
            check(position != std::string::npos, "Actual profile fixture has canonical parent");
            bytes.replace(position, info->profileParentId.size(), raw.buttons().front().id);
        }
        return bytes;
    };
    rejects([&] { Document::loadDesktop(path, reader); },
            "Mounting rejects occupied authored parents instead of discarding source children");
}
void source(const std::filesystem::path &path) {
    auto doc = Document::load(path);
    check(doc.nodeCount() > 700, "Actual scene graph loaded");
    check(doc.buttons().size() >= 22, "All original main source buttons retained");
    check(doc.buttons().front().id == "CAB-194e41a66c2317b9df19269f505210be:2283844765343771863",
          "JSON source IDs above 2^53 retain all original digits");
    near(doc.entranceDuration(), 0.75, 1e-15, "Source entrance duration read from clip");
    check(doc.ambientDuration() > 13, "Full ambient last-key hold retained");
    auto right = doc.pointerEuler({1920, 540}, {1920, 1080});
    near(right.y, 3, 1e-7, "Source yaw curve/max-angle endpoint");
    auto top = doc.pointerEuler({960, 0}, {1920, 1080});
    near(top.x, -2, 1e-7, "Windows Y flips exactly once for source pitch");
    Playback playback(doc.entranceDuration(), doc.exitDuration());
    playback.open(0, true);
    auto frame = doc.frame({{1920, 1080}, playback.sample(0, true), {}});
    check(!frame.graphics.empty() && !frame.hits.empty(),
          "Actual source graphics and hit regions share snapshot");
    check(!frame.diagnostics.empty(), "Prototype capability gaps remain explicit");
    std::size_t valid{};
    for (const auto &hit : frame.hits) {
        Vec3 center{hit.rect.origin.x + hit.rect.size.x / 2,
                    hit.rect.origin.y + hit.rect.size.y / 2, 0};
        if (auto pixel = frame.camera.project(center, hit.world);
            pixel && frame.camera.hit(*pixel, hit.world, hit.rect))
            ++valid;
    }
    check(valid > 20, "Actual authored hit geometry inverse-projects");
    for (const auto &g : frame.graphics) {
        check(g.quads.size() == g.uvQuads.size(), "Mesh and source UV counts agree");
        check(g.colorQuads.empty() || g.quads.size() == g.colorQuads.size(),
              "Optional original mesh vertex colors stay aligned with source triangles");
        if (!g.texturePath.empty())
            check(!g.textureId.empty(), "Original sprite retains exact native texture ID");
    }
    check(frame.nodes.size() == doc.nodeCount(),
          "Finalized actual hierarchy available to renderer and presentation adapters");
    for (std::size_t i = 1; i < frame.graphics.size(); ++i)
        check(frame.graphics[i - 1].sortingOrder <= frame.graphics[i].sortingOrder,
              "Actual source render sequence respects effective sorting writers");
    ButtonMotion buttons(doc);
    buttons.reset(1, true);
    check(buttons.instanceIds().size() >= 32,
          "All source Animator instances and Selectable bindings loaded");
    for (const auto &id : buttons.instanceIds())
        buttons.setState(ButtonState::highlighted, id, 2, true);
    FrameInput interactive{{1920, 1080}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, {}};
    interactive.interaction = &buttons;
    interactive.time = 2;
    interactive.reduceMotion = true;
    check(!doc.frame(interactive).graphics.empty(),
          "All actual source controller Highlighted endpoints sample together");
    check(!buttons.requiresFrames(2),
          "Actual reduced controller endpoints settle without idle timers");
    std::size_t meshes{};
    for (const auto &g : frame.graphics)
        if (g.kind == "MeshRenderer")
            ++meshes;
    check(meshes > 0, "Original circle meshes loaded from approved metadata");
    for (int i = 0; i < 100; ++i) {
        playback.open(i, true);
        auto opened = doc.frame({{1920, 1080}, playback.sample(i, true), {}});
        check(opened.hits.size() == frame.hits.size(),
              "Repeated immutable opens retain geometry count");
        playback.close(i + 0.1, true);
        check(doc.frame({{1920, 1080}, playback.sample(i + 0.1, true), {}}).graphics.empty(),
              "Closed snapshot emits no rendering work");
    }
    auto narrow =
        doc.frame({{1080, 1920}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, {}});
    check(narrow.canvasSize.y > frame.canvasSize.y,
          "Source narrow-screen FOV/canvas rule retained");
    auto graphicsAddress = frame.graphics.data();
    auto geometryAddress = frame.graphics.front().quads.data();
    Quaternion tilt{0, std::sin(0.025), 0, std::cos(0.025)};
    doc.reproject(frame, tilt);
    check(frame.graphics.data() == graphicsAddress &&
              frame.graphics.front().quads.data() == geometryAddress,
          "Pointer-only reproject retains static geometry allocations");
    auto rebuilt =
        doc.frame({{1920, 1080}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, tilt});
    for (int i = 0; i < 16; ++i)
        near(frame.hits.front().world.values[i], rebuilt.hits.front().world.values[i], 1e-9,
             "Pointer fast path shares exact fresh-frame transform");
    rejects(
        [&] {
            Document::load(path, [](const auto &) {
                return std::string("{\"nodes\":[],\"root_node_id\":\"CAB:-9007199254740993\"}");
            });
        },
        "Malformed graph rejects without app data");
}
} // namespace
int main(int argc, char **argv) {
    try {
        curves();
        desktopScroll();
        lifecycle();
        frameScheduling();
        projection();
        gyro();
        flicker();
        deployment();
        fills();
        layoutAndButtons();
        shaderChannelsAndDeployment();
        if (argc > 1) {
            source(std::filesystem::path(argv[1]));
            desktopSource(std::filesystem::path(argv[1]));
        }
        std::cout << "Passed " << checks << " scene contract checks"
                  << (argc > 1 ? " including actual source resources" : " (synthetic fixtures)")
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Scene contract failed: " << error.what() << '\n';
        return 1;
    }
}
