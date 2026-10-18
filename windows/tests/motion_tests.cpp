#include "core/motion.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace endfield::core;
namespace {
unsigned checks{};
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double value, double expected, double tolerance, const char *message) {
    ++checks;
    if (!std::isfinite(value) || std::abs(value - expected) > tolerance)
        throw std::runtime_error(std::string(message) + ": " + std::to_string(value));
}
void selection() {
    ModuleSelection state;
    check(state.selected() == Module::power && state.requested() == Module::power && !state.isTransitioning(),
          "Fresh selection is the real Power module");
    check(!state.request(Module::power) && state.generation() == 0, "Selecting the current module is a no-op");
    const auto first = state.request(Module::notes);
    check(first && first->from == Module::power && first->to == Module::notes && first->generation == 1,
          "A selection starts one generation-owned swap");
    state.request(Module::map);
    state.request(Module::calendar);
    check(state.pending() == Module::calendar && state.selected() == Module::power,
          "Only the latest pending request survives and incoming content is not committed early");
    check(!state.complete(0) && state.isTransitioning(), "A stale completion cannot commit the active swap");
    auto second = state.complete(first->generation);
    check(second && second->from == Module::notes && second->to == Module::calendar && second->generation == 2,
          "Completing the first swap starts the latest queued destination");
    check(!state.complete(first->generation) && state.transitioningTo() == Module::calendar,
          "A repeated old completion cannot finish the queued transition");
    state.request(Module::map);
    state.request(Module::calendar);
    check(!state.pending(), "Re-requesting the incoming destination clears a queued detour");
    check(!state.complete(second->generation) && state.selected() == Module::calendar && !state.isTransitioning(),
          "The final completion leaves one committed module");
    auto cancelled = state.request(Module::reader);
    state.request(Module::archive);
    check(state.cancel() == Module::calendar && !state.pending() && !state.isTransitioning(),
          "Close cancellation restores the committed screen and drops queued requests");
    state.complete(cancelled->generation);
    check(state.selected() == Module::calendar, "A delayed cancelled completion cannot restore hidden content");
    state.request(Module::notes);
    state.request(Module::map);
    state.settle();
    check(state.selected() == Module::map && !state.isTransitioning(), "Reduce Motion settles directly on the latest request");
    state.settle(Module::power);
    for (int a = 0; a < 24; ++a)
        for (int b = 0; b < 24; ++b) {
            const auto from = static_cast<Module>(a), to = static_cast<Module>(b);
            ModuleSelection pair(from);
            auto transition = pair.request(to);
            check(transition.has_value() == (from != to), "Every module pair has consistent no-op/swap behavior");
            if (transition) pair.complete(transition->generation);
            check(pair.selected() == to && !pair.isTransitioning(), "Every module can commit from every other module");
        }
}
void moduleGeometry() {
    const auto notes = moduleContentFrame(Module::notes), reader = moduleContentFrame(Module::reader);
    const auto map = moduleContentFrame(Module::map), local = moduleLocalContentFrame(Module::notes);
    check(notes.x == 300 && notes.y == 152 && notes.width == 400 && notes.height == 334,
          "Standard content retains Mac400x334 geometry");
    check(reader.x == 300 && reader.y == 100 && reader.width == 400 && reader.height == 440,
          "Reader/archive/calendar use the tall Mac frame");
    check(map.x == 280 && map.y == 100 && map.width == 440 && map.height == 440,
          "Map/work/media/game instruments use the complete dial frame");
    check(local.x == 20 && local.y == 52 && local.width == 400 && local.height == 334,
          "Content frame is offset into the shared440x440 module viewport");
    check(moduleIdentifier(Module::fileShelf) == "fileShelf" && moduleIdentifier(Module::activityMonitor) == "activityMonitor",
          "Stable identifiers retain current Mac raw values");
    auto direction = moduleDirection(Module::notes, Module::map);
    check(direction.x == 0 && direction.y == 1, "Same-group forward selection moves down in Mac declaration order");
    direction = moduleDirection(Module::map, Module::notes);
    check(direction.x == 0 && direction.y == -1, "Same-group reverse selection moves up");
    for (auto from : {Module::notes, Module::system, Module::storage, Module::power})
        for (auto to : {Module::notes, Module::system, Module::storage, Module::power}) {
            direction = moduleDirection(from, to);
            check(std::abs(direction.x) + std::abs(direction.y) == 1, "Each transition has one cardinal direction");
        }
    direction = moduleDirection(Module::notes, Module::power);
    check(direction.x == -1 && direction.y == 0, "Returning from the right group to Power moves left");
    direction = moduleDirection(Module::system, Module::power);
    check(direction.x == 1 && direction.y == 0, "Returning from the left group to Power moves right");
    direction = moduleDirection(Module::storage, Module::power);
    check(direction.x == 0 && direction.y == -1, "Returning from the bottom group to Power moves up");
}
void transitions() {
    near(ModuleTransitionStyle::duration, .30, 0, "Module swap duration matches Mac");
    near(ModuleTransitionStyle::timing.value(.29), .77, 1e-12,
         "Timing solves Bezier X: parameter0.5 corresponds to time0.29 and progress0.77");
    near(ModuleTransitionStyle::timing.value(-1), 0, 0, "Transition starts at an exact bounded endpoint");
    near(ModuleTransitionStyle::timing.value(2), 1, 0, "Transition ends at an exact bounded endpoint");
    double previous = 0;
    for (int i = 0; i <= 100; ++i) {
        const double value = ModuleTransitionStyle::timing.value(i / 100.0);
        check(value >= previous && value <= 1, "Mechanical easing remains continuous and monotonic");
        previous = value;
    }
    const auto incoming = ModuleTransitionStyle::incomingOffset({1, 0});
    const auto outgoing = ModuleTransitionStyle::outgoingOffset({1, 0});
    check(incoming.x == 32 && incoming.y == 0 && incoming.z == -54 && incoming.rotationY == .16,
          "Incoming content retains Mac displacement, depth and tilt");
    check(outgoing.x == -25 && outgoing.y == 0 && outgoing.z == -55 && outgoing.rotationY == -.16,
          "Outgoing content retracts in the opposite direction");
    near(incoming.perspective, -1.0 / 600, 0, "Module transform preserves its Mac perspective");
    for (auto direction : {MotionPoint{-1, 0}, MotionPoint{1, 0}, MotionPoint{0, -1}, MotionPoint{0, 1}}) {
        const auto collapsed = ModuleTransitionStyle::shutterKeyframe(0, direction);
        const auto full = ModuleTransitionStyle::shutterKeyframe(1, direction);
        for (std::size_t strip = 0; strip < full.size(); ++strip) {
            double area = 0;
            for (std::size_t i = 0; i < collapsed[strip].size(); ++i) {
                const auto a = collapsed[strip][i], b = collapsed[strip][(i + 1) % collapsed[strip].size()];
                area += a.x * b.y - a.y * b.x;
                check(full[strip][i].x >= 0 && full[strip][i].x <= 440 && full[strip][i].y >= 0 && full[strip][i].y <= 440,
                      "Every complete shutter is inside the viewport for all four directions");
            }
            near(area, 0, 1e-9, "Closed shutters have exactly zero area");
            const auto a = full[strip][0], b = full[strip][1];
            near(std::hypot(a.x - b.x, a.y - b.y), 440, 1e-9, "Open shutters span the entire viewport");
        }
        const auto revealed = ModuleTransitionStyle::shutterAt(.30, direction, true);
        const auto retracted = ModuleTransitionStyle::shutterAt(.30, direction, false);
        const auto seam = ModuleTransitionStyle::registrationAt(.30, direction);
        const auto seamEndpoint = ModuleTransitionStyle::registrationKeyframe(1, direction);
        for (std::size_t strip = 0; strip < full.size(); ++strip) {
            for (std::size_t i = 0; i < full[strip].size(); ++i) {
                near(revealed[strip][i].x, full[strip][i].x, 0, "Reveal samples its exact final mask");
                near(retracted[strip][i].y, collapsed[strip][i].y, 0, "Retraction samples its exact collapsed mask");
            }
            for (std::size_t i = 0; i < seam[strip].size(); ++i) {
                near(seam[strip][i].x, seamEndpoint[strip][i].x, 0, "Registration and shutter finish together");
                near(seam[strip][i].y, seamEndpoint[strip][i].y, 0, "Registration geometry preserves orientation");
            }
        }
    }
    const auto halfway = ModuleTransitionStyle::shutterKeyframe(.5, {-1, 0});
    near(halfway[1][1].x, 191.4, 1e-10, "Second shutter uses the Mac0.13 lag");
    near(halfway[2][1].x, 220, 1e-10, "The third shutter has no lag");
    near(ModuleTransitionStyle::registrationOpacityAt(.054), .36, 1e-12, "Registration reaches its original opacity peak");
    near(ModuleTransitionStyle::registrationOpacityAt(.21), .18, 1e-12, "Registration retains its late opacity keyframe");
    near(ModuleTransitionStyle::registrationOpacityAt(.30), 0, 0, "Registration disappears completely on completion");
}
void scrolling() {
    DesktopScrollMotion scroll;
    check(scroll.position() == 1 && scroll.target() == 1 && !scroll.requiresFrames(), "Navigation starts settled at the top");
    check(!scroll.canScroll(-1) && scroll.canScroll(1), "Arrow availability follows the bounded target");
    scroll.reset(1, 0);
    scroll.scroll(-.5, 100, 0);
    near(scroll.position(), 1, 0, "Wheel input changes its target without jumping the current pose");
    near(scroll.advance(.05), .8552733733030645, 1e-12, "First closed-form spring sample matches the Mac equation");
    near(scroll.advance(.15), .49447024824534547, 1e-12, "The authored spring crosses its target");
    near(scroll.advance(.2), .45088625126458476, 1e-12, "The spring carries rebound velocity");
    DesktopScrollMotion single;
    single.reset(1, 0); single.scroll(-.5, 100, 0);
    near(single.advance(.2), scroll.position(), 1e-12, "Scroll sampling is independent of frame subdivision");
    scroll.scroll(-.2, 100, .2);
    near(scroll.target(), .3, 1e-12, "Repeated wheel input accumulates against the target");
    scroll.advance(1.2);
    check(scroll.position() == scroll.target() && !scroll.requiresFrames(), "A one-second gap seeks the exact settled endpoint");
    for (double hidden : {1.5, 100.0, 100000.0}) {
        scroll.reset(1, 0);
        scroll.scroll(100, hidden, 0);
        check((scroll.position() - 1) * hidden <= 180 + 1e-9, "Wheel rebound stays inside180 source units");
        scroll.reset(1, 0);
        scroll.gesture(100, hidden, 0, GesturePhase::began);
        check(scroll.isGestureActive() && scroll.requiresFrames(), "Precise input owns the active gesture");
        check((scroll.position() - 1) * hidden < 240, "Gesture rubber band stays inside240 source units");
        near(DesktopScrollMotion::presentationPosition(scroll.position(), hidden), scroll.position(), 0,
             "The layout clamp does not discard visible gesture rebound");
        scroll.gesture(0, hidden, .1, GesturePhase::ended);
        const double edge = scroll.position();
        scroll.gesture(10, hidden, .11, GesturePhase::none, GesturePhase::began);
        near(scroll.position(), edge, 0, "Released edge rebound suppresses a competing momentum stream");
        scroll.gesture(0, hidden, .12, GesturePhase::none, GesturePhase::ended);
        check(!scroll.ownsMomentum(), "The final momentum event releases ownership");
        scroll.advance(1.2);
        check(scroll.position() == 1 && !scroll.requiresFrames(), "All list lengths settle and park after edge release");
    }
    scroll.reset(.5, 0);
    scroll.gesture(.1, 100, 0, GesturePhase::began);
    near(scroll.position(), .6, 1e-12, "In-bounds precise input follows the finger directly");
    scroll.gesture(0, 100, .1, GesturePhase::cancelled);
    check(!scroll.isGestureActive() && !scroll.ownsMomentum() && !scroll.requiresFrames(), "Gesture cancellation clears ownership and motion");
    scroll.scroll(-.4, 100, .2, true);
    near(scroll.position(), .2, 1e-12, "Reduce Motion seeks the bounded wheel destination immediately");
    check(!scroll.requiresFrames(), "Reduced wheel input needs no animation clock");
    const double before = scroll.position();
    scroll.scroll(std::numeric_limits<double>::quiet_NaN(), 100, .3);
    scroll.scroll(.1, 0, .3);
    scroll.gesture(.1, 100, std::numeric_limits<double>::infinity(), GesturePhase::began);
    near(scroll.position(), before, 0, "Invalid input cannot corrupt the retained scroll state");
}
void visibilityAndDemand() {
    VisibilityClock clock(.8, .4);
    check(!clock.sample(0), "A concealed clock has no sample or ambient work");
    clock.open(10);
    const auto openingGeneration = clock.generation();
    auto sample = clock.sample(10.4);
    check(sample && sample->phase == VisibilityPhase::opening && !sample->ambientTime, "Ambient cannot begin during deployment");
    near(sample->entranceTime, .6, 1e-12, "Finite clip sampling uses source OutQuad, not linear time");
    sample = clock.sample(10.81);
    check(sample && sample->phase == VisibilityPhase::visible && sample->completedGeneration == openingGeneration,
          "The source deadline commits visible once with its generation");
    near(*sample->ambientTime, .01, 1e-12, "Ambient retains the authored entrance deadline as its epoch");
    check(!clock.sample(10.9)->completedGeneration, "Settled sampling does not repeat completion");
    clock.close(11);
    const auto closingGeneration = clock.generation();
    sample = clock.sample(11.2);
    check(sample && sample->phase == VisibilityPhase::closing && sample->ambientTime, "Closing preserves the same ambient clock beneath the finite exit");
    near(*sample->exitTime, .3, 1e-12, "Exit clip retains its own OutQuad clock");
    clock.open(11.21);
    check(clock.generation() != closingGeneration, "Reopening invalidates the interrupted closing generation");
    sample = clock.sample(11.5);
    check(sample && sample->phase == VisibilityPhase::opening, "A former close deadline cannot conceal a reopened HUD");
    clock.close(12, true);
    check(clock.phase() == VisibilityPhase::concealed && !clock.sample(13), "Reduced close immediately parks all work");
    clock.open(14, true);
    sample = clock.sample(14, true);
    check(sample && sample->phase == VisibilityPhase::visible && !sample->ambientTime,
          "Reduced opening seeks stable geometry without ambient tracks");
    near(sample->entranceTime, .8, 0, "Reduced opening retains the exact entrance endpoint");
    clock.close(15);
    sample = clock.sample(15.5);
    check(sample && sample->phase == VisibilityPhase::concealed && sample->completedGeneration == clock.generation(),
          "Closing completion is reported without a renderable source pose");
    check(!clock.sample(16), "No closed tick revives the clock");

    FrameDemandGate gate;
    FrameDemand demand;
    demand.presented = demand.onScreen = true;
    demand.finiteAnimation = demand.ambientEnabled = true;
    check(!gate.refresh(demand).present && !gate.tick(demand).timerInterval, "Closed presentation rejects every animation demand");
    demand.phase = VisibilityPhase::opening;
    auto result = gate.refresh(demand);
    check(result.present && result.timerInterval.has_value(), "Opening immediately presents and starts one host clock");
    near(*result.timerInterval, 1.0 / 60, 0, "Normal finite presentation uses the Mac60Hz interval");
    for (int i = 0; i < 60; ++i) check(gate.tick(demand).present, "Every finite animation tick presents");
    demand.phase = VisibilityPhase::visible;
    demand.finiteAnimation = false;
    unsigned ambientPresents = 0;
    for (int i = 0; i < 60; ++i) ambientPresents += gate.tick(demand).present;
    check(ambientPresents == 30, "Sixty clock ticks produce exactly30 settled ambient presentations");
    demand.lowPower = true;
    check(!gate.tick(demand).present && !gate.tick(demand).timerInterval, "Low power disables ambient-only scheduling");
    demand.finiteAnimation = true;
    result = gate.tick(demand);
    check(result.present, "Low power retains finite animation demand");
    near(*result.timerInterval, 1.0 / 30, 0, "Low-power finite animation uses the current Mac30Hz interval");
    demand.reduceMotion = true;
    check(gate.refresh(demand).present && !gate.tick(demand).timerInterval,
          "Reduced Motion presents an event-driven endpoint and owns no recurring clock");
    demand.reduceMotion = false;
    demand.pendingOpening = true;
    check(gate.refresh(demand).present && !gate.tick(demand).present, "Prepared opening waits for its owner before recurring work");
    demand.pendingOpening = false;
    demand.onScreen = false;
    check(!gate.refresh(demand).present && !gate.tick(demand).present, "Offscreen detached content produces no work");
    demand.canAdvanceTransition = true;
    check(gate.tick(demand).present, "An explicitly owned offscreen transition may finish on its existing clock");
}
} // namespace

int main() {
    try {
        selection(); moduleGeometry(); transitions(); scrolling(); visibilityAndDemand();
        std::cout << "Passed " << checks << " fresh Mac-derived motion checks (synthetic only)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Motion check failed: " << error.what() << '\n';
        return 1;
    }
}
