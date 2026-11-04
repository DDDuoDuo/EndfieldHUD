#pragma once
// Numeric motion of the redesign: the timing model of Sources/HUDDesignMotion.swift
// (HUDMotion), the shell positions (HUDShellPosition, :188-235), the idle-
// hiding scroll bar state (HUDScrollBarVisibility, HUDDesignBadges.swift:
// 305-368), the projected caret blink (HUDProjectedTextEditor.swift:238-275)
// and the deadline list that replaces the Mac's DispatchQueue.asyncAfter work
// items. LIB-11..13, LIB-40, LIB-87.
//
// Core Animation semantics reproduced (LIB-12, LIB-19):
//  - model values always hold the END state; an animation runs from the value
//    on screen now (the presentation) to the model, so Reduce Motion simply
//    skips the animation and an interrupted one never leaves a layer half-way;
//  - nothing animates implicitly (every HUDLayer is actionless); only the
//    verbs in hud_design_layer.hpp start tracks;
//  - a CAAnimationGroup's timing function maps the group's time and each
//    child applies its own on top: HUDMotion.enter gives translation
//    E(E(t)) and the opacity keyframes [0, o, o] at [0, .6, 1] linear on E(t);
//    leave and pop apply only the group curve (children are linear). Track
//    therefore carries an outer (group) and an inner (child) timing;
//  - a delay uses a backwards fill (the from value shows during the delay);
//  - fill-forwards tracks (leave, hold, bandOut) hold their end until removed.
// Everything is sampled from the caller's clock (the owner's frame time):
// no timer, no thread, no allocation while sampling.
#include "modules/hud_design_tokens.hpp"
#include <functional>
#include <limits>
#include <vector>

namespace endfield::modules::design {

// One channel animation: keyframes (2...6) over [start, start + duration),
// optionally repeating. The normalized time is inner(outer(t)); values are
// interpolated linearly between keyframes at keyTimes (CAKeyframeAnimation
// calculationMode linear; a CABasicAnimation is two keyframes).
struct Track {
    static constexpr std::size_t maximumKeys = 6;
    std::array<double, maximumKeys> values{}, keyTimes{};
    std::uint8_t keys{};          // 0: no track
    double start{}, duration{};   // start includes the delay
    core::CubicTiming outer{easing::linear}, inner{easing::linear};
    double repeat{1};             // infinity: forever (pulse, scan, blink)
    bool additive{}, fillForwards{}, fillBackwards{};
    // enter / leave dispatch to core::DesignMotion::enterAt / leaveAt (one
    // implementation of those curves); `offset` is their offset component.
    enum class Kind : std::uint8_t { keyframes, enterOpacity, enterTranslation, leaveOpacity, leaveTranslation } kind{Kind::keyframes};
    double offset{}, fromOpacity{1};
    // Order of addition (CALayer.add): a later absolute track overrides an
    // earlier one and the additive tracks added before it; 0 = no track.
    std::uint64_t serial{};
    bool present() const noexcept { return keys > 0 || kind != Kind::keyframes; }
    double end() const noexcept { return start + duration * repeat; } // infinity for endless loops
    bool active(double now) const noexcept;    // shows a value at `now`
    bool finished(double now) const noexcept;  // ran out (and does not hold)
    bool running(double now) const noexcept;  // present and not yet at its end (delays included): frames needed
    double sample(double now) const noexcept;  // requires active(now)
    static Track basic(double from, double to, double start, double duration, core::CubicTiming timing);
};
// Colour channel (tint): sRGB components lerped like CA interpolates CGColor
// in its own space, then converted to linear for the renderer's multiply.
struct ColorTrack {
    static constexpr std::size_t maximumKeys = 3;
    std::array<Color, maximumKeys> values{};
    std::array<double, maximumKeys> keyTimes{};
    std::uint8_t keys{};
    double start{}, duration{}, repeat{1};
    core::CubicTiming timing{easing::linear};
    bool fillBackwards{};
    std::uint64_t serial{};
    bool active(double now) const noexcept;
    bool running(double now) const noexcept;
    Color sample(double now) const noexcept;
};

// HUDMotion.staggerDelay, HUDShellPosition: the ONE definitions are
// core::DesignMotion::staggerDelay and core::ShellPosition (core/motion.hpp,
// shared with the module switch). enter / leave tracks (motion::enter,
// motion::leave in hud_design_layer.hpp) are SAMPLED through
// core::DesignMotion::enterAt / leaveAt, so the E(E(t)) composition exists
// once; the generic Track serves every other verb.
inline double staggerDelay(int index) noexcept { return core::DesignMotion::staggerDelay(index); }
inline Point toPoint(core::MotionPoint p) noexcept { return {p.x, p.y}; }
inline Point entranceOffset(std::optional<core::Module> previous, core::Module next) {
    return toPoint(core::ShellPosition::entranceOffset(previous, next));
}
inline Point subpageOffset(double dx, double dy = 0) noexcept { return toPoint(core::ShellPosition::subpageOffset(dx, dy)); }

// ---------------------------------------------------------------------------
// Deadlines (DispatchQueue.main.asyncAfter / CATransaction completion blocks)
// ---------------------------------------------------------------------------
// One list per design::Context. Owners report next() through nextWakeTime /
// requiresFrames and call advance() from update()/deadline(); due work runs
// in deadline order on the UI thread. A token groups the work of one page,
// menu or overlay generation so its destruction cancels it (the Mac's weak
// self captures). Scheduling allocates (content events only).
class Deadlines final {
public:
    using Token = std::uint64_t;
    Token token();                                   // a new cancellation group
    void after(double time, Token, std::function<void()>);
    void cancel(Token) noexcept;
    std::optional<double> next() const noexcept;
    // Runs every item due at `now` (items scheduled meanwhile for <= now run
    // too, bounded by `limit` to stop a runaway loop). True when one ran.
    bool advance(double now, std::size_t limit = 256);
    bool empty() const noexcept { return items_.empty(); }
private:
    struct Item { double time; Token token; std::uint64_t serial; std::function<void()> work; };
    std::vector<Item> items_;
    Token tokens_{};
    std::uint64_t serial_{};
};

// HUDScrollBarVisibility (LIB-40): hidden at rest; noteScroll (wheel, drag,
// keyboard scroll) shows the bar and arms ONE hide 1 s after the last
// activity (minimum delay .05 s); hovering the bar's strip shows it, leaving
// arms the hide; a drag shows it and its end counts as a scroll; never hides
// while hovered or dragging; nothing armed while idle. Fade in .15 s easeOut,
// fade out .30 s easeIn; Reduce Motion shows and hides at once. reset() hides
// immediately (the content left the screen; a fade-out already running keeps
// running, `guard isShown`). advance() is the armed work item: it re-arms for
// the rest of a burst (> .05 s left) or hides. The owner samples opacity().
class ScrollBarVisibility final {
public:
    static constexpr double linger = 1, minimumDelay = .05, fadeIn = .15, fadeOut = .30;
    void noteScroll(double now, bool reduceMotion);
    void setHovered(bool, double now, bool reduceMotion);
    void setDragging(bool, double now, bool reduceMotion);
    void reset() noexcept;
    // Fires the armed hide when due (the owner's deadline path).
    bool advance(double now, bool reduceMotion);
    std::optional<double> nextDeadline() const noexcept;
    bool shown() const noexcept { return shown_; }
    double opacity(double now) const noexcept;
    bool animating(double now) const noexcept;
private:
    bool shown_{}, hovered_{}, dragging_{};
    double lastActivity_{}, fadeStart_{-1e300}, fadeFrom_{}, fadeTo_{};
    std::optional<double> hideAt_;
    void setShown(bool, double now, bool reduceMotion) noexcept;
};

// The projected caret (LIB-87): visible only while the HUD window is the
// foreground window, the field has focus, the selection is empty and the
// field is editable. Blink = opacity keyframes 1, 1, 0, 0, 1 at key times
// 0, .5, .58, .92, 1 over 1.06 s, repeating, linear between keys (so the
// caret fades over 80 ms twice a cycle); solid again after every caret move
// (restart); Reduce Motion keeps it solid. Between the fades the owner
// sleeps until nextChange(); it asks for frames only during the two 80 ms
// ramps, so an idle focused field costs two short bursts per 1.06 s and
// nothing at all when hidden, unfocused or reduced.
class CaretBlink final {
public:
    static constexpr double period = 1.06;
    static constexpr std::array<double, 5> keyTimes{0, .5, .58, .92, 1}, values{1, 1, 0, 0, 1};
    void restart(double now) noexcept { origin_ = now; }
    double opacity(double now, bool reduceMotion) const noexcept;
    bool ramping(double now, bool reduceMotion) const noexcept;
    // The next key time after `now` (a ramp's start or end); nullopt reduced.
    std::optional<double> nextChange(double now, bool reduceMotion) const noexcept;
private:
    double origin_{};
};
} // namespace endfield::modules::design
