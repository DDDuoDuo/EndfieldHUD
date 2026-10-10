#include "modules/charge_overlay.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace {
constexpr double deadlineAllowance = 1e-9;
bool contains(const core::Rect& r, core::Point p) noexcept {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
} // namespace

ChargeOverlayState::ChargeOverlayState() { timeline_.setStage(ChargeStage::hidden, false, 0); }

void ChargeOverlayState::setReduceMotion(bool value) noexcept { timeline_.setReduceMotion(value); }
void ChargeOverlayState::setSettings(const ChargeAlertSettings& settings) {
    auto next = settings.normalized();
    if (next == settings_) return;
    settings_ = std::move(next);
    ++contentRevision_;
}
void ChargeOverlayState::update(const BatteryReading& reading, bool preview, double) {
    if (reading_ == reading && preview_ == preview) return;
    reading_ = reading;
    preview_ = preview;
    ++contentRevision_;
}

void ChargeOverlayState::show(bool persistent, double duration, bool replay, double time) {
    if (editing_ || systemOverlay_ || projection_) return;
    dismissalAt_.reset();
    const auto token = ++generation_;
    persistent_ = persistent;
    requestedDuration_ = std::isfinite(duration) ? std::min(60.0, std::max(1.0, duration)) : 5;
    if (!visible_ || replay || !completed_) {
        visible_ = true;
        completed_ = false;
        window_ = true;
        timeline_.animateEntrance(time);
        pending_ = Pending::entrance;
        pendingToken_ = token;
    } else {
        scheduleDismissal(token, time);
    }
}
void ChargeOverlayState::scheduleDismissal(std::uint64_t token, double time) {
    if (persistent_ || editing_) return;
    dismissalAt_ = time + requestedDuration_;
    dismissalToken_ = token;
}
void ChargeOverlayState::hide(bool animated, double time) {
    if (editing_ || systemOverlay_) return;
    dismissalAt_.reset();
    const auto token = ++generation_;
    persistent_ = false;
    completed_ = false;
    if (!visible_) return;
    if (!animated) {
        timeline_.cancel();
        timeline_.setStage(ChargeStage::hidden, false, time);
        pending_ = Pending::none;
        window_ = false;
        visible_ = false;
        return;
    }
    timeline_.animateExit(time);
    pending_ = Pending::exit;
    pendingToken_ = token;
}

ChargeOverlayState::Events ChargeOverlayState::advance(double time) {
    Events events;
    const bool windowBefore = window_;
    for (;;) {
        const auto sequence = timeline_.nextDeadline();
        double next = INFINITY;
        if (sequence) next = *sequence;
        if (dismissalAt_) next = std::min(next, *dismissalAt_);
        if (!(next <= time + deadlineAllowance)) break;
        if (sequence && *sequence == next) {
            const auto event = timeline_.advance(next);
            const auto pending = pending_;
            if (event != ChargeIndicatorTimeline::Event::none) pending_ = Pending::none;
            if (event == ChargeIndicatorTimeline::Event::entranceCompleted && pending == Pending::entrance &&
                pendingToken_ == generation_ && !editing_) {
                completed_ = true;
                events.presentationCompleted = true;
                scheduleDismissal(pendingToken_, next);
            } else if (event == ChargeIndicatorTimeline::Event::exitCompleted && pending == Pending::exit &&
                       pendingToken_ == generation_) {
                window_ = false;
                visible_ = false;
            }
        } else {
            const auto token = dismissalToken_;
            dismissalAt_.reset();
            if (token == generation_) { events.dismissed = true; hide(true, next); }
        }
    }
    events.windowShown = !windowBefore && window_;
    events.windowHidden = windowBefore && !window_;
    return events;
}
std::optional<double> ChargeOverlayState::nextDeadline() const noexcept {
    auto next = timeline_.nextDeadline();
    if (dismissalAt_ && (!next || *dismissalAt_ < *next)) next = dismissalAt_;
    return next;
}
bool ChargeOverlayState::requiresFrames(double time) const noexcept { return window_ && timeline_.animating(time); }

void ChargeOverlayState::setScreens(std::span<const ChargeWorkArea> screens, std::optional<std::size_t> preferred) {
    screens_.assign(screens.begin(), screens.end());
    preferred_ = preferred;
    // OverlayController.reposition while editing: keep the draft on a
    // connected screen and clamped to its current usable area.
    if (editing_ && draftAnchor_) {
        if (const auto screen = selectedScreen()) {
            draftAnchor_ = chargeClampedAnchor(*draftAnchor_, screen->bounds, settings_.scale, true, screen->pixelsPerPoint);
            draftScreen_ = screen->id;
        }
    }
}
std::optional<ChargeWorkArea> ChargeOverlayState::selectedScreen() const {
    const auto requested = editing_ ? draftScreen_ : settings_.customPlacement ? settings_.customScreenID : std::nullopt;
    const auto index = chargeSelectedScreen(screens_, requested, preferred_);
    return index ? std::optional<ChargeWorkArea>(screens_[*index]) : std::nullopt;
}
std::optional<core::Point> ChargeOverlayState::anchor() const {
    const auto screen = selectedScreen();
    if (!screen) return std::nullopt;
    if (editing_ && draftAnchor_)
        return chargeClampedAnchor(*draftAnchor_, screen->bounds, settings_.scale, true, screen->pixelsPerPoint);
    return chargeAnchor(settings_, screen->bounds, false, screen->pixelsPerPoint);
}
std::optional<ChargePanelLayout> ChargeOverlayState::layout() const {
    const auto screen = selectedScreen();
    const auto point = anchor();
    if (!screen || !point) return std::nullopt;
    return chargePanelLayout(*point, settings_.scale, editing_, screen->pixelsPerPoint);
}

bool ChargeOverlayState::beginPositionEditing(const BatteryReading& previewValue, double time) {
    if (systemOverlay_ || projection_) return false;
    if (editing_) return true; // the host refocuses the existing editor
    update(previewValue, true, time);
    dismissalAt_.reset();
    ++generation_;
    timeline_.cancel();
    pending_ = Pending::none;
    const auto requested = settings_.customPlacement ? settings_.customScreenID : std::nullopt;
    const auto index = chargeSelectedScreen(screens_, requested, preferred_);
    if (!index) return false;
    const auto& screen = screens_[*index];
    draftScreen_ = screen.id;
    draftAnchor_ = chargeAnchor(settings_, screen.bounds, true, screen.pixelsPerPoint);
    editing_ = visible_ = persistent_ = completed_ = window_ = true;
    timeline_.setStage(ChargeStage::compact, false, time);
    return true;
}
void ChargeOverlayState::beginDrag(core::Point mouse) noexcept {
    if (!editing_ || !draftAnchor_) return;
    dragMouse_ = mouse;
    dragOrigin_ = draftAnchor_;
}
bool ChargeOverlayState::drag(core::Point mouse) noexcept {
    if (!editing_ || !dragOrigin_ || !dragMouse_ || screens_.empty()) return false;
    if (!std::isfinite(mouse.x) || !std::isfinite(mouse.y)) return false;
    const core::Point proposed{dragOrigin_->x + mouse.x - dragMouse_->x, dragOrigin_->y + mouse.y - dragMouse_->y};
    const ChargeWorkArea* target = nullptr;
    for (const auto& screen : screens_) if (contains(screen.bounds, mouse)) { target = &screen; break; }
    if (!target) for (const auto& screen : screens_) if (draftScreen_ && screen.id == *draftScreen_) { target = &screen; break; }
    if (!target) target = &screens_.front();
    draftScreen_ = target->id;
    const auto next = chargeClampedAnchor(proposed, target->bounds, settings_.scale, true, target->pixelsPerPoint);
    const bool moved = !draftAnchor_ || *draftAnchor_ != next;
    draftAnchor_ = next;
    return moved;
}
void ChargeOverlayState::finishEditing(double time) {
    editing_ = false;
    draftAnchor_.reset();
    draftScreen_.reset();
    endDrag();
    hide(false, time);
}
std::optional<ChargePosition> ChargeOverlayState::confirmPosition(double time) {
    if (!editing_ || !draftAnchor_) return std::nullopt;
    const auto screen = selectedScreen();
    if (!screen) return std::nullopt;
    const auto position = chargeNormalizedPosition(*draftAnchor_, *screen);
    finishEditing(time);
    return position;
}
void ChargeOverlayState::discardPosition(double time) {
    if (editing_) finishEditing(time);
}
} // namespace endfield::modules
