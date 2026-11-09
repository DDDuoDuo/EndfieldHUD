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

ChargeAlertSettings ChargeOverlayState::barSettings() const {
    if (!editing_) return settings_;
    auto value = settings_;
    value.scale = draftScale_;
    value.length = draftLength_;
    return value.normalized();
}

ChargeOverlayState::ChargeOverlayState() { timeline_.setStage(ChargeStage::hidden, false, 0); }

void ChargeOverlayState::setReduceMotion(bool value) noexcept { timeline_.setReduceMotion(value); }
void ChargeOverlayState::setSettings(const ChargeAlertSettings& settings) {
    auto next = settings.normalized();
    if (next == settings_) return;
    settings_ = std::move(next);
    ++contentRevision_;
    syncDraft();
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
    // reposition() before the entrance: the bar takes its width (Length on the
    // current display) first, so the entrance plays in full at that width.
    place(time);
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
void ChargeOverlayState::postpone(double seconds) noexcept {
    if (!(seconds > 0) || !std::isfinite(seconds)) return;
    timeline_.postpone(seconds);
    if (dismissalAt_) *dismissalAt_ += seconds;
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
    // connected screen, where Confirm puts it on that display's usable area.
    syncDraft();
}
void ChargeOverlayState::syncDraft() noexcept {
    if (!editing_ || !draftAnchor_) return;
    const auto screen = selectedScreen();
    if (!screen) return;
    draftAnchor_ = chargeEditorAnchor(*draftAnchor_, moved_, barSettings(), screen->bounds, screen->pixelsPerPoint);
    draftScreen_ = screen->id;
    syncLengthSlider(*screen);
}
// syncLengthSlider: the range follows the display and Size (200-point minimum).
void ChargeOverlayState::syncLengthSlider(const ChargeWorkArea& screen) noexcept {
    const auto bar = barSettings();
    const auto range = chargeLengthRange(screen.bounds, bar.scale, screen.pixelsPerPoint);
    ChargeLengthSlider slider{std::min(100.0, std::ceil(range.minimum * 100)), 100};
    slider.value = slider.range().clamp(chargeLength(bar, screen.bounds, screen.pixelsPerPoint) * 100);
    lengthSlider_ = slider;
}
std::optional<ChargeWorkArea> ChargeOverlayState::selectedScreen() const {
    const auto requested = editing_ ? draftScreen_ : settings_.customPlacement ? settings_.customScreenID : std::nullopt;
    const auto index = chargeSelectedScreen(screens_, requested, preferred_);
    return index ? std::optional<ChargeWorkArea>(screens_[*index]) : std::nullopt;
}
std::optional<core::Point> ChargeOverlayState::anchor() const {
    const auto screen = selectedScreen();
    if (!screen) return std::nullopt;
    if (editing_ && draftAnchor_) return draftAnchor_;
    return chargeAnchor(settings_, screen->bounds, false, screen->pixelsPerPoint);
}
std::optional<ChargePanelLayout> ChargeOverlayState::layout() const {
    const auto screen = selectedScreen();
    const auto point = anchor();
    if (!screen || !point) return std::nullopt;
    const auto bar = barSettings();
    const double ppp = screen->pixelsPerPoint;
    const double width = chargeBarWidth(bar.length, screen->bounds, bar.scale, ppp);
    std::optional<core::Rect> strip;
    if (editing_ && stripPoints_) {
        // Spaced for the largest Size, so a Size change never moves the strip
        // vertically or slides the bar under it; held while a slider is held.
        if (adjusting_ && heldStrip_) strip = heldStrip_;
        else strip = chargeEditorStripFrame(*point, bar.scale, width, chargeEditorStripSize(*stripPoints_, ppp), screen->bounds, ppp);
    }
    return chargePanelLayout(*point, bar.scale, width, screen->bounds, ppp, strip);
}
std::optional<ChargePanelLayout> ChargeOverlayState::place(double time) {
    auto next = layout();
    if (next) timeline_.setBarWidth(next->barWidth, time);
    return next;
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
    // The strip flips above the bar near the bottom edge, so the editor no
    // longer reserves room below it (editing: false).
    draftAnchor_ = chargeAnchor(settings_, screen.bounds, false, screen.pixelsPerPoint);
    draftScale_ = settings_.scale;
    draftLength_ = settings_.length;
    moved_ = adjusting_ = false;
    heldStrip_.reset();
    editing_ = visible_ = persistent_ = completed_ = window_ = true;
    timeline_.setStage(ChargeStage::compact, false, time);
    syncDraft();
    return true;
}
void ChargeOverlayState::setEditorStrip(std::optional<design::Size> stripPoints) noexcept {
    stripPoints_ = stripPoints;
    heldStrip_.reset();
}
void ChargeOverlayState::setStripAdjusting(bool adjusting) noexcept {
    if (adjusting && !adjusting_) {
        const auto current = layout();
        heldStrip_ = current ? current->strip : std::nullopt;
    }
    adjusting_ = adjusting;
    if (!adjusting) heldStrip_.reset();
}
// editorChanged: Size is a fraction of 100 %; Length a share of the display.
void ChargeOverlayState::setDraftScale(double scale) {
    if (!editing_) return;
    draftScale_ = scale;
    ++contentRevision_;
    syncDraft();
}
bool ChargeOverlayState::setDraftLength(double percent) {
    if (!editing_) return false;
    const auto next = chargeDraftLength(percent, lengthSlider_ ? std::optional<double>(lengthSlider_->value) : std::nullopt);
    if (!next) return false;
    draftLength_ = next;
    ++contentRevision_;
    syncDraft();
    return true;
}
// editorPointerDragged: the display under the pointer, else the draft's, else the first.
bool ChargeOverlayState::moveDraft(core::Point proposed, core::Point mouse) noexcept {
    if (!editing_ || screens_.empty()) return false;
    if (!std::isfinite(proposed.x) || !std::isfinite(proposed.y)) return false;
    const ChargeWorkArea* target = nullptr;
    for (const auto& screen : screens_) if (contains(screen.bounds, mouse)) { target = &screen; break; }
    if (!target) for (const auto& screen : screens_) if (draftScreen_ && screen.id == *draftScreen_) { target = &screen; break; }
    if (!target) target = &screens_.front();
    const bool changedScreen = !draftScreen_ || target->id != *draftScreen_;
    draftScreen_ = target->id;
    draftAnchor_ = chargeClampedAnchor(proposed, barSettings(), target->bounds, false, target->pixelsPerPoint);
    moved_ = true;
    if (changedScreen) {
        // ChargeIndicatorView.setBarWidth -> updateContent: the bar takes the
        // new display's width (and density), so its labels are fitted again.
        ++contentRevision_;
        syncLengthSlider(*target);
    }
    return changedScreen;
}
void ChargeOverlayState::finishEditing(double time) {
    editing_ = false;
    draftAnchor_.reset();
    draftScreen_.reset();
    draftLength_.reset();
    moved_ = adjusting_ = false;
    lengthSlider_.reset();
    stripPoints_.reset();
    heldStrip_.reset();
    hide(false, time);
    // The drafts' Size and Length are dropped; a confirmed layout arrives as settings.
    ++contentRevision_;
}
std::optional<ChargeBarLayout> ChargeOverlayState::confirmPosition(double time) {
    if (!editing_ || !draftAnchor_) return std::nullopt;
    const auto screen = selectedScreen();
    if (!screen) return std::nullopt;
    const auto bar = barSettings();
    ChargeBarLayout layout{moved_ ? std::optional<ChargePosition>(chargeNormalizedPosition(*draftAnchor_, *screen)) : std::nullopt,
                           bar.scale, bar.length};
    finishEditing(time);
    return layout;
}
void ChargeOverlayState::discardPosition(double time) {
    if (editing_) finishEditing(time);
}
} // namespace endfield::modules
