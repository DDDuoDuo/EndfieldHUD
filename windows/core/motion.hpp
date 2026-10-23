#pragma once

#include <array>
#include <cmath>
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
struct MotionPoint { double x{}, y{}; bool operator==(const MotionPoint &) const = default; };
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

// The redesigned content box (Sources/HUDDesignScreen.swift HUDDesignGeometry,
// HUDDesignTokens.swift HUDMetric.box): 590 x 510 content px whose top-left is
// the disc centre (500, 320) + (-295, -305) px; one px is baseUnit design units
// (the demo stage is the deployed HUD on the reference 1470 x 956 pt display at
// 1.15 pt per unit, captured 1920 px wide). The one definition: the library,
// the module host and the overlay read it here; the plane matrix is
// source::DesktopChromeLayout::boxWorld. The display adaptation lives in the
// centre calibration, never in the unit (the Mac keeps baseUnit constant too).
struct DesignBox {
    static constexpr double width = 590, height = 510;
    static constexpr MotionPoint discCentre{500, 320}, boxOffset{-295, -305};
    static constexpr double baseUnit = 1470.0 / (1920 * 1.15);
    // HUDDesignGeometry.boxRect(unit:) in design units (x 303.60, y 116.94,
    // 392.80 x 339.54 at baseUnit).
    static constexpr MotionRect rect(double unit = baseUnit) {
        return {discCentre.x + boxOffset.x * unit, discCentre.y + boxOffset.y * unit, width * unit, height * unit};
    }
};

// Sources/HUDDesignTokens.swift HUDEasing (spec §0, §1.9), shared by the module
// switch and the design library (which must not redefine them).
struct DesignEasing {
    static constexpr CubicTiming standard{.2, .8, .2, 1}; // E
    static constexpr CubicTiming snap{.2, .9, .1, 1};     // SNAP
    static constexpr CubicTiming ease{.25, .1, .25, 1};
    static constexpr CubicTiming easeIn{.42, 0, 1, 1};
    static constexpr CubicTiming easeOut{0, 0, .58, 1};
    static constexpr CubicTiming easeInOut{.42, 0, .58, 1};
};
struct DesignMotionSample {
    double opacity{1};
    MotionPoint translation{}; // Additive, in the caller's units (the offset's).
    bool operator==(const DesignMotionSample &) const = default;
};
// Caller-clock samplers of Sources/HUDDesignMotion.swift HUDMotion.enter /
// staggerDelay / leave, checked against paused Core Animation presentation
// layers. Both are CAAnimationGroups whose children inherit the group's
// duration and see the group's eased clock: enter's opacity keyframes
// [0, o, o] at [0, .6, 1] are linear in E(t), while its translation carries E
// again (E(E(t))); leave's fade and move are linear in ease(t). Model values
// hold the end state, so elapsed >= duration (or Reduce Motion) is the rest
// pose for enter; a delay fills backwards (the start pose). leave holds its
// end (fill forwards) until the caller removes the layer at the swap, and is
// the untouched model (opacity fromOpacity, no move) under Reduce Motion.
// No wipe/grow/pop/hold here: those belong to the design library's motion.
struct DesignMotion {
    static constexpr double entrance = .38, leave = .13, staggerBase = .06, staggerStep = .036;
    static constexpr int staggerCap = 14;
    // .06 + min(max(0, i), 14) x .036 (spec §1.10).
    static double staggerDelay(int index) noexcept;
    static DesignMotionSample enterAt(double elapsed, MotionPoint offset, double opacity = 1, double delay = 0,
                                      bool reduceMotion = false, double duration = entrance,
                                      CubicTiming timing = DesignEasing::standard) noexcept;
    // Moves toward -0.6 x offset while fading to 0.
    static DesignMotionSample leaveAt(double elapsed, MotionPoint offset, double fromOpacity = 1,
                                      bool reduceMotion = false) noexcept;
};

// Sources/HUDDesignOverlay.swift HUDCenterOverlay.setContentDimmed (LIB-64):
// while a confirm bar or a toast shows, the module content gets CIGaussianBlur
// inputRadius 1.5 and CIExposureAdjust inputEV -2.2 (the demo's brightness(.5)
// in linear light), both eased from their presentation values with E over
// .25 s (HUDDuration.dimFilter). The radius is in the module host layer's
// space, i.e. centre-plane design units (Core Animation applies filter
// parameters in layer space); Renderer::ContentFilter takes it in output
// pixels. The filters exist only while needed(): from set(true) until an
// ease-out completes without a new dim. Reduce Motion jumps. Caller clock,
// no timer; the radial dim (.22 s) is the overlay's own track.
class ContentDimTrack {
public:
    static constexpr double blurRadius = 1.5, exposure = -2.2, duration = .25;
    // Returns whether the target changed (HUDCenterOverlay.setDimmed ignores repeats).
    bool set(bool dimmed, double time, bool reduceMotion = false);
    double amount(double time) const noexcept; // 0..1, scales radius and EV together
    bool dimmed() const noexcept { return target_ > 0; }
    bool animating(double time) const noexcept { return std::isfinite(time) && time < start_ + duration && from_ != target_; }
    bool needed(double time) const noexcept { return dimmed() || animating(time); }
    void reset() noexcept { from_ = target_ = 0; start_ = 0; } // HUDCenterOverlay.reset: everything off at once
private:
    double from_{}, target_{}, start_{};
};

// Sources/HUDDesignMotion.swift HUDShellPosition: module positions on the shell
// in stage px (spec Appendix A, the default layout; a reorder in System >
// Modules does not move them, as on the Mac). A switch enters from
// normalize(B - A) x 44 content px; the first module of a session (no
// previous) from the disc centre; a zero-length vector is (0, 0).
struct ShellPosition {
    static constexpr MotionPoint discCentre{975, 575};
    static constexpr double travel = 44;
    static MotionPoint position(Module module);
    static MotionPoint entranceOffset(std::optional<Module> previous, Module next);
    static MotionPoint offset(MotionPoint from, MotionPoint to) noexcept;
    // Inside a module: forward (+dx) enters from the right, back from the left, +dy from below.
    static MotionPoint subpageOffset(double dx, double dy = 0) noexcept { return offset({}, {dx, dy}); }
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
