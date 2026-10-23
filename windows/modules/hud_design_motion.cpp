// Numeric motion (Sources/HUDDesignMotion.swift HUDMotion timing, Core
// Animation keyframe/fill/repeat semantics; HUDDesignBadges.swift
// HUDScrollBarVisibility :305-368; HUDProjectedTextEditor.swift caret blink
// :251-271). Caller clock only: no timer, no thread, no allocation while
// sampling.
#include "modules/hud_design_motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Swift never fuses multiply-adds; neither does this port, so its numbers
// stay bit-equal with the Mac oracle's.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules::design {
namespace {
bool linear(const core::CubicTiming& t) noexcept { return t.x1 == 0 && t.y1 == 0 && t.x2 == 1 && t.y2 == 1; }
double timed(const core::CubicTiming& t, double x) noexcept { return linear(t) ? std::clamp(x, 0.0, 1.0) : t.value(x); }
// CAKeyframeAnimation calculationMode linear: piecewise linear in key time.
template <class V, class Lerp>
V interpolate(const V* values, const double* times, std::size_t keys, double x, Lerp lerp) noexcept {
    if (x <= times[0]) return values[0];
    for (std::size_t i = 1; i < keys; ++i)
        if (x <= times[i]) {
            const double span = times[i] - times[i - 1];
            return span > 0 ? lerp(values[i - 1], values[i], (x - times[i - 1]) / span) : values[i];
        }
    return values[keys - 1];
}
// The local progress of one cycle at `now` (0 before the start: backwards
// fill; the last cycle's end after it: forwards fill).
double progress(double start, double duration, double repeat, double now) noexcept {
    const double t = now - start;
    if (!(t > 0) || !(duration > 0)) return 0;
    if (t >= duration * repeat) { const double rest = repeat - std::floor(repeat); return rest > 0 ? rest : 1; }
    return std::fmod(t, duration) / duration;
}
}

bool Track::active(double now) const noexcept {
    if (!present()) return false;
    if (now < start) return fillBackwards;
    return now < end() || fillForwards;
}
bool Track::finished(double now) const noexcept { return present() && now >= end() && !fillForwards; }
bool Track::running(double now) const noexcept { return present() && now < end(); }
double Track::sample(double now) const noexcept {
    const double elapsed = now - start;
    switch (kind) {
    // HUDMotion.enter / leave through the one implementation of their curves.
    case Kind::enterOpacity: return core::DesignMotion::enterAt(elapsed, {}, fromOpacity, 0, false, duration, outer).opacity;
    case Kind::enterTranslation: return core::DesignMotion::enterAt(elapsed, {offset, 0}, 1, 0, false, duration, outer).translation.x;
    case Kind::leaveOpacity: return core::DesignMotion::leaveAt(elapsed, {}, fromOpacity).opacity;
    case Kind::leaveTranslation: return core::DesignMotion::leaveAt(elapsed, {offset, 0}).translation.x;
    case Kind::keyframes: break;
    }
    const double x = timed(inner, timed(outer, progress(start, duration, repeat, now)));
    return interpolate(values.data(), keyTimes.data(), keys, x, [](double a, double b, double t) { return a + (b - a) * t; });
}
Track Track::basic(double from, double to, double start, double duration, core::CubicTiming timing) {
    Track t;
    t.values[0] = from; t.values[1] = to; t.keyTimes[1] = 1; t.keys = 2;
    t.start = start; t.duration = duration; t.outer = timing;
    return t;
}

bool ColorTrack::active(double now) const noexcept { return keys > 0 && (now < start ? fillBackwards : now < start + duration * repeat); }
bool ColorTrack::running(double now) const noexcept { return keys > 0 && now < start + duration * repeat; }
Color ColorTrack::sample(double now) const noexcept {
    const double x = timed(timing, progress(start, duration, repeat, now));
    return interpolate(values.data(), keyTimes.data(), keys, x, [](Color a, Color b, double t) {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    });
}

Deadlines::Token Deadlines::token() { return ++tokens_; }
void Deadlines::after(double time, Token token, std::function<void()> work) {
    if (!std::isfinite(time) || !work) throw std::invalid_argument("A deadline needs a finite time and work");
    items_.push_back({time, token, serial_++, std::move(work)});
}
void Deadlines::cancel(Token token) noexcept { std::erase_if(items_, [&](const Item& i) { return i.token == token; }); }
std::optional<double> Deadlines::next() const noexcept {
    if (items_.empty()) return std::nullopt;
    return std::min_element(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.time < b.time; })->time;
}
// Due items in (time, scheduling order); each runs after it left the list,
// so its work may schedule or cancel freely.
bool Deadlines::advance(double now, std::size_t limit) {
    bool ran{};
    for (std::size_t n = 0; n < limit; ++n) {
        auto due = items_.end();
        for (auto it = items_.begin(); it != items_.end(); ++it)
            if (it->time <= now && (due == items_.end() || it->time < due->time || (it->time == due->time && it->serial < due->serial))) due = it;
        if (due == items_.end()) break;
        auto work = std::move(due->work);
        items_.erase(due);
        work();
        ran = true;
    }
    return ran;
}

// HUDScrollBarVisibility.noteScroll / setHovered / setDragging / reset / scheduleHide / setShown.
namespace {
constexpr double fadeDuration(double to) noexcept { return to > 0 ? ScrollBarVisibility::fadeIn : ScrollBarVisibility::fadeOut; }
}
double ScrollBarVisibility::opacity(double now) const noexcept {
    const double d = fadeDuration(fadeTo_);
    if (!(now < fadeStart_ + d)) return fadeTo_;
    const double t = std::max(0.0, now - fadeStart_) / d;
    return fadeFrom_ + (fadeTo_ - fadeFrom_) * (fadeTo_ > 0 ? easing::easeOut : easing::easeIn).value(t);
}
bool ScrollBarVisibility::animating(double now) const noexcept { return fadeFrom_ != fadeTo_ && now < fadeStart_ + fadeDuration(fadeTo_); }
std::optional<double> ScrollBarVisibility::nextDeadline() const noexcept { return hideAt_; }
void ScrollBarVisibility::noteScroll(double now, bool reduce) {
    lastActivity_ = now;
    setShown(true, now, reduce);
    if (!hideAt_) hideAt_ = now + std::max(minimumDelay, lastActivity_ + linger - now);
}
void ScrollBarVisibility::setHovered(bool value, double now, bool reduce) {
    if (value == hovered_) return;
    hovered_ = value;
    if (value) {
        setShown(true, now, reduce);
    } else {
        lastActivity_ = now;
        if (!hideAt_) hideAt_ = now + std::max(minimumDelay, lastActivity_ + linger - now);
    }
}
void ScrollBarVisibility::setDragging(bool value, double now, bool reduce) {
    if (value == dragging_) return;
    dragging_ = value;
    if (!value) { noteScroll(now, reduce); return; }
    setShown(true, now, reduce);
}
// Hidden at once; a fade-out already under way keeps running (`guard isShown`).
void ScrollBarVisibility::reset() noexcept {
    hideAt_.reset(); hovered_ = dragging_ = false;
    if (!shown_) return;
    shown_ = false; fadeFrom_ = fadeTo_ = 0; fadeStart_ = -1e300;
}
// The armed DispatchWorkItem: re-armed while the burst continues, never
// hides while hovered or dragging (and then arms nothing).
bool ScrollBarVisibility::advance(double now, bool reduce) {
    if (!hideAt_ || now < *hideAt_) return false;
    hideAt_.reset();
    if (hovered_ || dragging_) return true;
    if (lastActivity_ + linger - now > minimumDelay) hideAt_ = now + std::max(minimumDelay, lastActivity_ + linger - now);
    else setShown(false, now, reduce);
    return true;
}
// HUDMotion.animate of the bar's opacity from what shows now; Reduce Motion jumps.
void ScrollBarVisibility::setShown(bool value, double now, bool reduce) noexcept {
    if (value == shown_) return;
    const double from = opacity(now);
    shown_ = value; fadeTo_ = value ? 1 : 0; fadeFrom_ = reduce ? fadeTo_ : from; fadeStart_ = reduce ? -1e300 : now;
}

// The caret blink keyframes (HUDProjectedTextEditor.swift:267-270).
namespace {
double caretPhase(double origin, double now) noexcept {
    const double t = now - origin;
    if (!(t > 0)) return 0;
    return std::fmod(t, CaretBlink::period) / CaretBlink::period;
}
}
double CaretBlink::opacity(double now, bool reduce) const noexcept {
    if (reduce) return 1;
    return interpolate(values.data(), keyTimes.data(), values.size(), caretPhase(origin_, now), [](double a, double b, double t) { return a + (b - a) * t; });
}
bool CaretBlink::ramping(double now, bool reduce) const noexcept {
    if (reduce) return false;
    const double x = caretPhase(origin_, now);
    return (x > keyTimes[1] && x < keyTimes[2]) || (x > keyTimes[3] && x < keyTimes[4]);
}
// The next key time (a ramp's start or end) after `now`.
std::optional<double> CaretBlink::nextChange(double now, bool reduce) const noexcept {
    if (reduce) return std::nullopt;
    const double t = now - origin_;
    if (!(t >= 0)) return origin_ + keyTimes[1] * period;
    const double cycle = std::floor(t / period), x = (t - cycle * period) / period;
    for (std::size_t i = 1; i < keyTimes.size(); ++i)
        if (keyTimes[i] > x) return origin_ + (cycle + keyTimes[i]) * period;
    return origin_ + (cycle + 1 + keyTimes[1]) * period;
}
} // namespace endfield::modules::design
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
