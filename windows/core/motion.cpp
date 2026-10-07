#include "motion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::core {
namespace {
double unit(double value) { return std::clamp(value, 0.0, 1.0); }
double extent(double progress, std::size_t index) {
    const double lag = ModuleTransitionStyle::shutterLags[index];
    return unit(progress * (1 + lag) - lag) * ModuleTransitionStyle::viewport.width;
}
MotionPoint oriented(MotionPoint point, MotionPoint direction) {
    const double length = ModuleTransitionStyle::viewport.width;
    if (direction.x != 0)
        return {direction.x < 0 ? point.x : length - point.x, point.y};
    return {point.y, direction.y < 0 ? point.x : length - point.x};
}
ModuleOffset offset(MotionPoint direction, double distance, double depth) {
    return {direction.x * distance, direction.y * distance, depth,
            -direction.y * .13, direction.x * .16, -1.0 / 600};
}
template<class Path, class Make>
Path interpolatePath(double elapsed, Make make) {
    const double eased = ModuleTransitionStyle::timing.value(elapsed / ModuleTransitionStyle::duration);
    const double index = eased * 4;
    const auto lower = std::min<std::size_t>(3, static_cast<std::size_t>(index));
    const double fraction = index - static_cast<double>(lower);
    auto result = make(ModuleTransitionStyle::keyTimes[lower]);
    const auto next = make(ModuleTransitionStyle::keyTimes[lower + 1]);
    for (std::size_t strip = 0; strip < result.size(); ++strip)
        for (std::size_t vertex = 0; vertex < result[strip].size(); ++vertex) {
            auto &point = result[strip][vertex];
            point.x += (next[strip][vertex].x - point.x) * fraction;
            point.y += (next[strip][vertex].y - point.y) * fraction;
        }
    return result;
}
bool validScroll(double delta, double hiddenLength, double time) {
    return std::isfinite(delta) && std::isfinite(hiddenLength) && hiddenLength > 0 && std::isfinite(time);
}
} // namespace

ModuleGroup moduleGroup(Module module) {
    switch (module) {
    case Module::notes: case Module::fileShelf: case Module::clipboard: case Module::volume:
    case Module::workMode: case Module::eventLog: case Module::map: case Module::addApp:
    case Module::nowPlaying: case Module::account: case Module::projection: case Module::reader:
    case Module::archive: case Module::mediaAssembly: case Module::calendar: case Module::minigame:
        return ModuleGroup::right;
    case Module::system: case Module::display: case Module::hotkeys: case Module::about:
        return ModuleGroup::left;
    case Module::storage: case Module::activityMonitor:
        return ModuleGroup::bottom;
    case Module::power: case Module::profile:
        return ModuleGroup::power;
    }
    throw std::invalid_argument("Unknown module");
}
std::string_view moduleIdentifier(Module module) {
    constexpr std::array<std::string_view, 24> identifiers{
        "notes", "fileShelf", "clipboard", "volume", "workMode", "eventLog", "map", "addApp",
        "nowPlaying", "account", "projection", "reader", "archive", "mediaAssembly", "calendar", "minigame",
        "system", "display", "hotkeys", "about", "storage", "activityMonitor", "power", "profile"};
    return identifiers.at(static_cast<std::size_t>(module));
}
MotionRect moduleContentFrame(Module module) {
    switch (module) {
    case Module::reader: case Module::archive: case Module::calendar:
        return {300, 100, 400, 440};
    case Module::nowPlaying: case Module::mediaAssembly: case Module::minigame:
    case Module::workMode: case Module::map:
        return {280, 100, 440, 440};
    default: return {300, 152, 400, 334};
    }
}
MotionRect moduleLocalContentFrame(Module module) {
    auto result = moduleContentFrame(module);
    result.x -= ModuleTransitionStyle::viewport.x;
    result.y -= ModuleTransitionStyle::viewport.y;
    return result;
}
MotionPoint moduleDirection(Module from, Module to) {
    if (moduleGroup(from) == moduleGroup(to))
        return {0, static_cast<int>(to) > static_cast<int>(from) ? 1.0 : -1.0};
    switch (moduleGroup(to)) {
    case ModuleGroup::left: return {-1, 0};
    case ModuleGroup::right: return {1, 0};
    case ModuleGroup::bottom: return {0, 1};
    case ModuleGroup::power:
        switch (moduleGroup(from)) {
        case ModuleGroup::left: return {1, 0};
        case ModuleGroup::right: return {-1, 0};
        case ModuleGroup::bottom: case ModuleGroup::power: return {0, -1};
        }
    }
    throw std::invalid_argument("Unknown module direction");
}

Module ModuleSelection::requested() const {
    return pending_.value_or(transitioning_.value_or(selected_));
}
std::optional<ModuleTransition> ModuleSelection::request(Module module) {
    if (transitioning_) {
        pending_ = module == *transitioning_ ? std::optional<Module>{} : module;
        return {};
    }
    if (module == selected_)
        return {};
    ++generation_;
    transitioning_ = module;
    return ModuleTransition{selected_, module, generation_};
}
std::optional<ModuleTransition> ModuleSelection::complete(std::uint64_t generation) {
    if (generation != generation_ || !transitioning_)
        return {};
    selected_ = *transitioning_;
    transitioning_.reset();
    const auto next = pending_;
    pending_.reset();
    return next ? request(*next) : std::optional<ModuleTransition>{};
}
Module ModuleSelection::cancel() {
    ++generation_;
    transitioning_.reset();
    pending_.reset();
    return selected_;
}
void ModuleSelection::settle(Module module) {
    ++generation_;
    selected_ = module;
    transitioning_.reset();
    pending_.reset();
}

double CubicTiming::value(double normalizedTime) const {
    if (!std::isfinite(normalizedTime))
        return normalizedTime > 0 ? 1 : 0;
    const double x = unit(normalizedTime);
    if (x == 0 || x == 1)
        return x;
    auto curve = [](double t, double a, double b) {
        const double u = 1 - t;
        return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
    };
    // Solve the Bezier X clock, rather than treating elapsed time as its
    // parameter. Bisection is bounded and handles a zero endpoint derivative.
    double low = 0, high = 1;
    for (int i = 0; i < 48; ++i) {
        const double midpoint = (low + high) * .5;
        if (curve(midpoint, x1, x2) < x) low = midpoint;
        else high = midpoint;
    }
    return curve((low + high) * .5, y1, y2);
}
ModuleOffset ModuleTransitionStyle::incomingOffset(MotionPoint direction) {
    return offset(direction, 32, -54);
}
ModuleOffset ModuleTransitionStyle::outgoingOffset(MotionPoint direction) {
    return offset({-direction.x, -direction.y}, 25, -55);
}
ShutterPath ModuleTransitionStyle::shutterKeyframe(double progress, MotionPoint direction) {
    ShutterPath path{};
    const double length = viewport.width, step = length / shutterLags.size();
    for (std::size_t i = 0; i < path.size(); ++i) {
        const double edge = extent(progress, i);
        const double low = std::max(0.0, i * step - .3);
        const double high = std::min(length, (i + 1) * step + .3);
        const double bevel = std::min({5.0, edge * .15, (length - edge) * .15});
        path[i] = {{{0, low}, {edge, low}, {edge, high - bevel}, {std::max(0.0, edge - bevel), high}, {0, high}}};
        for (auto &point : path[i]) point = oriented(point, direction);
    }
    return path;
}
RegistrationPath ModuleTransitionStyle::registrationKeyframe(double progress, MotionPoint direction) {
    RegistrationPath path{};
    const double step = viewport.width / shutterLags.size();
    for (std::size_t i = 0; i < path.size(); ++i) {
        const double edge = std::max(0.0, extent(progress, i) - .8);
        const double low = i * step + 12, high = (i + 1) * step - 12;
        path[i] = {{{std::max(0.0, edge - 4), low}, {edge, low}, {edge, high}, {std::max(0.0, edge - 4), high}}};
        for (auto &point : path[i]) point = oriented(point, direction);
    }
    return path;
}
ShutterPath ModuleTransitionStyle::shutterAt(double elapsed, MotionPoint direction, bool revealing) {
    return interpolatePath<ShutterPath>(elapsed, [&](double progress) {
        return shutterKeyframe(revealing ? progress : 1 - progress, direction);
    });
}
RegistrationPath ModuleTransitionStyle::registrationAt(double elapsed, MotionPoint direction) {
    return interpolatePath<RegistrationPath>(elapsed, [&](double progress) {
        return registrationKeyframe(progress, direction);
    });
}
double ModuleTransitionStyle::registrationOpacityAt(double elapsed) {
    const double progress = std::isfinite(elapsed) ? unit(elapsed / duration) : 0;
    for (std::size_t i = 1; i < registrationOpacityTimes.size(); ++i)
        if (progress <= registrationOpacityTimes[i]) {
            const double fraction = (progress - registrationOpacityTimes[i - 1]) /
                                    (registrationOpacityTimes[i] - registrationOpacityTimes[i - 1]);
            return registrationOpacities[i - 1] +
                   fraction * (registrationOpacities[i] - registrationOpacities[i - 1]);
        }
    return 0;
}

double DesktopScrollMotion::presentationPosition(double position, double hiddenLength) {
    if (!std::isfinite(position)) return 1;
    const double limit = std::isfinite(hiddenLength) && hiddenLength > 0
                           ? gestureEdgeTravel / std::max(1.0, hiddenLength) : 0;
    return std::clamp(position, -limit, 1 + limit);
}
bool DesktopScrollMotion::isAnimating() const {
    return !gestureActive_ && (std::abs(position_ - target_) > epsilon_ || std::abs(velocity_) > epsilon_ * 18);
}
bool DesktopScrollMotion::canScroll(int direction) const {
    return direction < 0 ? target_ < 1 - 1e-9 : target_ > 1e-9;
}
void DesktopScrollMotion::reset(double value, double time) {
    position_ = unit(std::isfinite(value) ? value : 1);
    target_ = position_;
    velocity_ = 0;
    lastTime_ = std::isfinite(time) ? std::optional<double>{time} : std::nullopt;
    rawPosition_ = position_;
    gestureActive_ = ownsMomentum_ = suppressesMomentum_ = false;
}
void DesktopScrollMotion::scroll(double delta, double hiddenLength, double time, bool reduceMotion) {
    if (!validScroll(delta, hiddenLength, time)) return;
    advance(time);
    gestureActive_ = ownsMomentum_ = suppressesMomentum_ = false;
    edgeLimit_ = wheelEdgeTravel / std::max(1.0, hiddenLength);
    epsilon_ = std::min(.0001, .25 / hiddenLength);
    const double requested = target_ + delta;
    target_ = unit(requested);
    if (reduceMotion) { position_ = target_; velocity_ = 0; return; }
    const double overflow = requested - target_;
    if (overflow != 0) {
        position_ += std::clamp(overflow * .55, -edgeLimit_ * .5, edgeLimit_ * .5);
        position_ = std::clamp(position_, -edgeLimit_, 1 + edgeLimit_);
    }
}
void DesktopScrollMotion::gesture(double delta, double hiddenLength, double time, GesturePhase phase,
                                  GesturePhase momentum, bool reduceMotion) {
    if (!validScroll(delta, hiddenLength, time)) return;
    if (phase == GesturePhase::none && momentum == GesturePhase::none) {
        scroll(delta, hiddenLength, time, reduceMotion); return;
    }
    const bool isMomentum = momentum != GesturePhase::none;
    const bool ended = isMomentum ? momentum == GesturePhase::ended : phase == GesturePhase::ended;
    if (phase == GesturePhase::cancelled || momentum == GesturePhase::cancelled) { reset(position_, time); return; }
    if (phase == GesturePhase::began && !isMomentum) { ownsMomentum_ = false; suppressesMomentum_ = false; }
    if (isMomentum && suppressesMomentum_) {
        if (ended) { ownsMomentum_ = false; suppressesMomentum_ = false; }
        return;
    }
    edgeLimit_ = gestureEdgeTravel / std::max(1.0, hiddenLength);
    epsilon_ = std::min(.0001, .25 / hiddenLength);
    if (!gestureActive_) {
        advance(time);
        const double bound = unit(position_), excess = position_ - bound;
        rawPosition_ = bound + excess / std::max(.001, 1 - std::abs(excess) / edgeLimit_);
        gestureActive_ = true;
        velocity_ = 0;
    }
    ownsMomentum_ = true;
    lastTime_ = time;
    const double rawLimit = 10000 / std::max(1.0, hiddenLength);
    rawPosition_ = std::clamp(rawPosition_ + delta, -rawLimit, 1 + rawLimit);
    target_ = unit(rawPosition_);
    const double excess = rawPosition_ - target_;
    position_ = reduceMotion ? target_ : target_ + excess / (1 + std::abs(excess) / edgeLimit_);
    const bool beyond = position_ < 0 || position_ > 1;
    if (ended || (isMomentum && beyond)) {
        gestureActive_ = false;
        rawPosition_ = target_;
        if (beyond) suppressesMomentum_ = !isMomentum || !ended;
    }
    if (isMomentum && ended) { ownsMomentum_ = false; suppressesMomentum_ = false; }
}
double DesktopScrollMotion::advance(double time) {
    if (!std::isfinite(time)) return position_;
    const auto previous = lastTime_;
    lastTime_ = time;
    if (!previous || time <= *previous || !isAnimating()) return position_;
    const double dt = std::min(2.0, time - *previous), decay = 11, frequency = 15;
    const double displacement = position_ - target_, b = (velocity_ + decay * displacement) / frequency;
    const double e = std::exp(-decay * dt), c = std::cos(frequency * dt), s = std::sin(frequency * dt);
    position_ = target_ + e * (displacement * c + b * s);
    velocity_ = e * ((b * frequency - decay * displacement) * c - (displacement * frequency + decay * b) * s);
    position_ = std::clamp(position_, -edgeLimit_, 1 + edgeLimit_);
    if (!isAnimating() || dt >= 1) { position_ = target_; velocity_ = 0; }
    return position_;
}

VisibilityClock::VisibilityClock(double entranceDuration, double exitDuration)
    : entranceDuration_(entranceDuration), exitDuration_(exitDuration) {
    if (!std::isfinite(entranceDuration) || entranceDuration < 0 || !std::isfinite(exitDuration) || exitDuration < 0)
        throw std::invalid_argument("Invalid source clip duration");
}
void VisibilityClock::open(double time, bool reduceMotion) {
    if (!std::isfinite(time)) return;
    ++generation_;
    phaseStart_ = time;
    loopStart_ = time + (reduceMotion ? 0 : entranceDuration_);
    phase_ = reduceMotion ? VisibilityPhase::visible : VisibilityPhase::opening;
}
void VisibilityClock::close(double time, bool reduceMotion) {
    if (!std::isfinite(time)) return;
    ++generation_;
    phaseStart_ = time;
    phase_ = reduceMotion ? VisibilityPhase::concealed : VisibilityPhase::closing;
}
void VisibilityClock::conceal() { ++generation_; phase_ = VisibilityPhase::concealed; }
void VisibilityClock::showStable(double time) {
    if (!std::isfinite(time)) return;
    ++generation_;
    phase_ = VisibilityPhase::visible;
    phaseStart_ = loopStart_ = time;
}
double VisibilityClock::clipTime(double elapsed, double length) {
    if (!(length > 0)) return 0;
    const double progress = unit(elapsed / length);
    return (2 * progress - progress * progress) * length;
}
std::optional<VisibilitySample> VisibilityClock::sample(double time, bool reduceMotion, bool ambientEnabled) {
    if (!std::isfinite(time) || phase_ == VisibilityPhase::concealed) return {};
    VisibilitySample result;
    result.generation = generation_;
    if (phase_ == VisibilityPhase::opening && (reduceMotion || time - phaseStart_ >= entranceDuration_)) {
        phase_ = VisibilityPhase::visible;
        result.completedGeneration = generation_;
    } else if (phase_ == VisibilityPhase::closing && (reduceMotion || time - phaseStart_ >= exitDuration_)) {
        phase_ = VisibilityPhase::concealed;
        result.phase = phase_;
        result.completedGeneration = generation_;
        return result;
    }
    result.phase = phase_;
    result.entranceTime = phase_ == VisibilityPhase::opening ? clipTime(time - phaseStart_, entranceDuration_) : entranceDuration_;
    if (phase_ != VisibilityPhase::opening && !reduceMotion && ambientEnabled)
        result.ambientTime = std::max(0.0, time - loopStart_);
    if (phase_ == VisibilityPhase::closing)
        result.exitTime = clipTime(time - phaseStart_, exitDuration_);
    return result;
}

bool FrameDemandGate::allowed(const FrameDemand &demand) {
    return demand.presented && demand.phase != VisibilityPhase::concealed &&
           (demand.onScreen || demand.canAdvanceTransition);
}
std::optional<double> FrameDemandGate::interval(const FrameDemand &demand) {
    if (!allowed(demand) || demand.pendingOpening || demand.reduceMotion) return {};
    const bool finite = demand.phase != VisibilityPhase::visible || demand.finiteAnimation;
    if (!finite && !(demand.ambientEnabled && !demand.lowPower)) return {};
    return demand.lowPower ? 1.0 / 30 : 1.0 / 60;
}
FrameDemandResult FrameDemandGate::refresh(const FrameDemand &demand) {
    if (!allowed(demand)) { reset(); return {}; }
    return {true, interval(demand)};
}
FrameDemandResult FrameDemandGate::tick(const FrameDemand &demand) {
    const auto timer = interval(demand);
    if (!timer) return {};
    idleTick_ = !idleTick_;
    return {demand.phase != VisibilityPhase::visible || demand.finiteAnimation || idleTick_, timer};
}

} // namespace endfield::core
