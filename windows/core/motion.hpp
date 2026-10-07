#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace endfield::core {

// Keep this order equal to HUDModule.allCases: same-group transition direction
// uses declaration order, not navigation order or translated labels.
enum class Module {
    notes, fileShelf, clipboard, volume, workMode, eventLog, map, addApp,
    nowPlaying, account, projection, reader, archive, mediaAssembly, calendar, minigame,
    system, display, hotkeys, about, storage, activityMonitor, power, profile
};
enum class ModuleGroup { left, right, bottom, power };
struct MotionPoint { double x{}, y{}; };
struct MotionRect { double x{}, y{}, width{}, height{}; };
ModuleGroup moduleGroup(Module module);
std::string_view moduleIdentifier(Module module);
MotionRect moduleContentFrame(Module module);
MotionRect moduleLocalContentFrame(Module module);
MotionPoint moduleDirection(Module from, Module to);

struct ModuleTransition {
    Module from{}, to{};
    std::uint64_t generation{};
    bool operator==(const ModuleTransition &) const = default;
};

// Direct state-machine port of Sources/HUDModule.swift. It owns no callbacks,
// views, timer, storage, or provider work; the renderer owns the two live screens.
class ModuleSelection {
public:
    explicit ModuleSelection(Module selected = Module::power) : selected_(selected) {}
    Module selected() const { return selected_; }
    std::optional<Module> transitioningTo() const { return transitioning_; }
    std::optional<Module> pending() const { return pending_; }
    std::uint64_t generation() const { return generation_; }
    bool isTransitioning() const { return transitioning_.has_value(); }
    Module requested() const;
    std::optional<ModuleTransition> request(Module module);
    std::optional<ModuleTransition> complete(std::uint64_t generation);
    Module cancel();
    void settle(Module module);
    void settle() { settle(requested()); }
private:
    Module selected_;
    std::optional<Module> transitioning_, pending_;
    std::uint64_t generation_{};
};

struct CubicTiming {
    double x1{}, y1{}, x2{}, y2{};
    double value(double normalizedTime) const;
};
struct ModuleOffset {
    double x{}, y{}, z{}, rotationX{}, rotationY{}, perspective{};
};
using ShutterPath = std::array<std::array<MotionPoint, 5>, 6>;
using RegistrationPath = std::array<std::array<MotionPoint, 4>, 6>;

// Geometry and animation parameters transcribed from HUDModuleContent.swift.
// Offsets describe the source transform construction order: perspective,
// translation, X rotation, Y rotation. They are endpoints, not a claim that
// linear component interpolation reproduces Core Animation transform blending.
struct ModuleTransitionStyle {
    static constexpr double duration = .30;
    static constexpr MotionRect viewport{280, 100, 440, 440};
    static constexpr CubicTiming timing{.18, .72, .26, 1};
    static constexpr std::array<double, 5> keyTimes{0, .25, .5, .75, 1};
    static constexpr std::array<double, 6> shutterLags{.025, .13, 0, .08, .035, .105};
    static constexpr std::array<double, 4> registrationOpacityTimes{0, .18, .7, 1};
    static constexpr std::array<double, 4> registrationOpacities{0, .36, .18, 0};
    static constexpr double registrationLineWidth = .7;
    static constexpr double registrationColorAlpha = .38;
    static double registrationWhite(bool dark) { return dark ? .77 : .22; }
    static ModuleOffset incomingOffset(MotionPoint direction);
    static ModuleOffset outgoingOffset(MotionPoint direction);
    static ShutterPath shutterKeyframe(double progress, MotionPoint direction);
    static RegistrationPath registrationKeyframe(double progress, MotionPoint direction);
    // Path keyframes use the one global timing function and linear path morphs,
    // matching the source's equal-topology CAKeyframeAnimation tracks.
    static ShutterPath shutterAt(double elapsed, MotionPoint direction, bool revealing);
    static RegistrationPath registrationAt(double elapsed, MotionPoint direction);
    static double registrationOpacityAt(double elapsed);
};

enum class GesturePhase { none, began, changed, ended, cancelled };

// Sources/HUDSourceDesktopNavigationLayout.swift: HUDSourceDesktopScrollMotion.
// Delta is normalized content travel, already converted by the input adapter.
class DesktopScrollMotion {
public:
    static constexpr double gestureEdgeTravel = 240;
    static constexpr double wheelEdgeTravel = 180;
    static double presentationPosition(double position, double hiddenLength);
    double position() const { return position_; }
    double target() const { return target_; }
    bool isGestureActive() const { return gestureActive_; }
    bool ownsMomentum() const { return ownsMomentum_; }
    bool acceptsGestureContinuation() const { return gestureActive_ || ownsMomentum_; }
    bool isAnimating() const;
    bool requiresFrames() const { return gestureActive_ || isAnimating(); }
    bool canScroll(int direction) const;
    void reset(double value, double time);
    void scroll(double delta, double hiddenLength, double time, bool reduceMotion = false);
    void gesture(double delta, double hiddenLength, double time, GesturePhase phase,
                 GesturePhase momentum = GesturePhase::none, bool reduceMotion = false);
    double advance(double time);
private:
    double position_{1}, target_{1}, velocity_{}, edgeLimit_{}, epsilon_{.0001}, rawPosition_{1};
    std::optional<double> lastTime_;
    bool gestureActive_{}, ownsMomentum_{}, suppressesMomentum_{};
};

enum class VisibilityPhase { concealed, opening, visible, closing };
struct VisibilitySample {
    VisibilityPhase phase{VisibilityPhase::concealed};
    std::uint64_t generation{};
    double entranceTime{};
    std::optional<double> ambientTime, exitTime;
    // Delivered only by the sample that completes a finite transition. For
    // reduced-motion open/close the caller observes the immediate phase change.
    std::optional<std::uint64_t> completedGeneration;
};

// Source finite clip timing only. Actual clip transforms remain renderer-owned.
class VisibilityClock {
public:
    VisibilityClock(double entranceDuration, double exitDuration);
    VisibilityPhase phase() const { return phase_; }
    std::uint64_t generation() const { return generation_; }
    void open(double time, bool reduceMotion = false);
    void close(double time, bool reduceMotion = false);
    void conceal();
    void showStable(double time);
    std::optional<VisibilitySample> sample(double time, bool reduceMotion = false,
                                         bool ambientEnabled = true);
    static double clipTime(double elapsed, double length);
private:
    double entranceDuration_, exitDuration_, phaseStart_{}, loopStart_{};
    VisibilityPhase phase_{VisibilityPhase::concealed};
    std::uint64_t generation_{};
};

struct FrameDemand {
    VisibilityPhase phase{VisibilityPhase::concealed};
    bool presented{}, onScreen{}, canAdvanceTransition{}, pendingOpening{};
    bool finiteAnimation{}, ambientEnabled{}, reduceMotion{}, lowPower{};
};
struct FrameDemandResult {
    bool present{};
    std::optional<double> timerInterval;
};

// HUDSourceWatchView.refreshPlaybackScheduling's demand/cadence, without owning
// a timer. Refresh is event-driven; tick is called only by the host's one clock.
// Normal finite motion presents at60 Hz; idle ambient at30 Hz. Low-power finite
// motion uses30 Hz and disables ambient, as HUDRuntimeAppearance does.
class FrameDemandGate {
public:
    FrameDemandResult refresh(const FrameDemand &demand);
    FrameDemandResult tick(const FrameDemand &demand);
    void reset() { idleTick_ = false; }
private:
    static bool allowed(const FrameDemand &demand);
    static std::optional<double> interval(const FrameDemand &demand);
    bool idleTick_{};
};

} // namespace endfield::core
