#include "core/system_overlay_state.hpp"
#include <cmath>
#include <stdexcept>

namespace endfield::core {
std::string_view systemOverlayPhaseName(SystemOverlayPhase value) noexcept {
    switch (value) {
    case SystemOverlayPhase::closed: return "closed";
    case SystemOverlayPhase::opening: return "opening";
    case SystemOverlayPhase::open: return "open";
    case SystemOverlayPhase::closing: return "closing";
    }
    return "closed";
}

// --- Sources/SystemOverlayState.swift -------------------------------------
SystemOverlayAction SystemOverlayState::toggle() noexcept {
    switch (phase_) {
    case SystemOverlayPhase::closed:
        generation_ += 1;
        closeAfterOpening_ = false;
        phase_ = SystemOverlayPhase::opening;
        return {SystemOverlayAction::Kind::open, generation_};
    case SystemOverlayPhase::open: return requestClose();
    case SystemOverlayPhase::opening: case SystemOverlayPhase::closing: return {};
    }
    return {};
}
SystemOverlayAction SystemOverlayState::requestClose(bool interruptOpening) noexcept {
    switch (phase_) {
    case SystemOverlayPhase::opening:
        if (interruptOpening) {
            closeAfterOpening_ = false;
            phase_ = SystemOverlayPhase::closing;
            return {SystemOverlayAction::Kind::close, generation_};
        }
        closeAfterOpening_ = true;
        return {};
    case SystemOverlayPhase::open:
        phase_ = SystemOverlayPhase::closing;
        return {SystemOverlayAction::Kind::close, generation_};
    case SystemOverlayPhase::closed: case SystemOverlayPhase::closing: return {};
    }
    return {};
}
SystemOverlayAction SystemOverlayState::didOpen(std::int64_t token) noexcept {
    if (token != generation_ || phase_ != SystemOverlayPhase::opening) return {};
    phase_ = SystemOverlayPhase::open;
    if (closeAfterOpening_) return requestClose();
    return {};
}
bool SystemOverlayState::didClose(std::int64_t token) noexcept {
    if (token != generation_ || phase_ != SystemOverlayPhase::closing) return false;
    phase_ = SystemOverlayPhase::closed;
    closeAfterOpening_ = false;
    return true;
}
void SystemOverlayState::forceClose() noexcept {
    generation_ += 1;
    phase_ = SystemOverlayPhase::closed;
    closeAfterOpening_ = false;
}

void SystemOverlayEffects::push(SystemOverlayEffect value) {
    if (count_ == items_.size()) throw std::logic_error("System overlay event produced too many effects");
    items_[count_++] = value;
}
const SystemOverlayEffect& SystemOverlayEffects::operator[](std::size_t index) const {
    if (index >= count_) throw std::out_of_range("System overlay effect index");
    return items_[index];
}
void SystemOverlayEffects::append(const SystemOverlayEffects& other) {
    for (const auto& value : other) push(value);
}

// --- AppDelegate / OverlayController policy --------------------------------
using Kind = SystemOverlayEffect::Kind;

void SystemOverlayLifecycle::perform(SystemOverlayAction action, SystemOverlayEffects& out) {
    switch (action.kind) {
    case SystemOverlayAction::Kind::none: return;
    case SystemOverlayAction::Kind::open: {
        // initialModule: initialModuleRequest ?? lastSystemModule; then the
        // request is consumed by this presentation only.
        const auto initial = initialModuleRequest_.value_or(lastSystemModule_);
        view_ = initial;
        interaction_ = false;
        initialModuleRequest_.reset();
        out.push({Kind::open, action.token, initial});
        return;
    }
    case SystemOverlayAction::Kind::close:
        if (view_) interaction_ = false;
        out.push({Kind::close, action.token, view_.value_or(lastSystemModule_)});
        return;
    }
}
bool SystemOverlayLifecycle::overlayToggle(SystemOverlayEffects& out) {
    // OverlayController.toggleSystemOverlay. Projection return and Shelf drag
    // deferral are handled by the Windows owner before it reaches this policy.
    if (quitRequested_ || editingPosition_ || appLaunchInFlight_) return false;
    perform(state_.toggle(), out);
    return true;
}
bool SystemOverlayLifecycle::appToggle(SystemOverlayEffects& out) {
    // AppDelegate.toggleSystemOverlay
    if (suspended_ || terminating_ || editingPosition_) return false;
    return overlayToggle(out);
}
void SystemOverlayLifecycle::select(Module module, SystemOverlayEffects& out) {
    if (!view_) return; // systemView?.selectModule
    view_ = module;
    out.push({Kind::select, state_.generation(), module});
}
void SystemOverlayLifecycle::settingsModule(Module module, SystemOverlayEffects& out) {
    if (suspended_ || terminating_ || editingPosition_) return;
    if (state_.phase() == SystemOverlayPhase::open) { select(module, out); return; }
    if (state_.phase() != SystemOverlayPhase::closed) return;
    initialModuleRequest_ = module;
    (void)appToggle(out);
}
void SystemOverlayLifecycle::closeOverlay(bool interruptOpening, SystemOverlayEffects& out) {
    perform(state_.requestClose(interruptOpening), out);
}
void SystemOverlayLifecycle::tearDown(SystemOverlayEffects& out) {
    const bool shouldQuit = quitAfterSystemClose_;
    quitAfterSystemClose_ = false;
    lastSystemModule_ = view_.value_or(lastSystemModule_);
    view_.reset();
    interaction_ = false;
    if (shouldQuit) out.push({Kind::terminate, state_.generation(), lastSystemModule_});
}

SystemOverlayEffects SystemOverlayLifecycle::launch(const SystemOverlayStartup& args, bool hasLaunched, bool diagnostic) {
    SystemOverlayEffects out;
    hasLaunched_ = hasLaunched;
    const bool firstRun = !hasLaunched_;
    if (!args.login && !args.noOnboarding && (firstRun || args.settings)) {
        if (!diagnostic) { hasLaunched_ = true; out.push({Kind::markLaunched}); }
        if (args.settings) settingsModule(Module::system, out);
        else if (!args.power) (void)appToggle(out);
    }
    if (args.preview) out.push({Kind::preview});
    if (args.power) (void)appToggle(out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::toggle() { SystemOverlayEffects out; (void)appToggle(out); return out; }
SystemOverlayEffects SystemOverlayLifecycle::openOverlay() {
    SystemOverlayEffects out;
    if (!state_.active()) (void)appToggle(out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::openSettingsModule(Module module) {
    SystemOverlayEffects out; settingsModule(module, out); return out;
}
SystemOverlayEffects SystemOverlayLifecycle::openWorkMode(double now) {
    if (!std::isfinite(now)) throw std::invalid_argument("System overlay requires a finite caller clock");
    SystemOverlayEffects out;
    if (suspended_ || terminating_ || editingPosition_) return out;
    if (state_.phase() == SystemOverlayPhase::open) { select(Module::workMode, out); return out; }
    if (state_.phase() != SystemOverlayPhase::closed || !appToggle(out)) return out;
    if (timerCount_ == timers_.size()) throw std::logic_error("Too many delayed Work Mode selections");
    // DispatchQueue.main.asyncAfter(deadline: .now() + entranceDuration + 0.3)
    timers_[timerCount_++] = {(now + entranceDuration) + 0.3, ++timerSerial_, state_.generation()};
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::reopen() {
    SystemOverlayEffects out;
    if (terminating_) return out;
    if (!state_.active()) (void)appToggle(out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::close(bool interruptOpening) {
    SystemOverlayEffects out; closeOverlay(interruptOpening, out); return out;
}
SystemOverlayEffects SystemOverlayLifecycle::requestQuit() {
    SystemOverlayEffects out;
    if (quitRequested_ || state_.phase() != SystemOverlayPhase::open) return out;
    quitRequested_ = true;
    quitAfterSystemClose_ = true;
    // onQuitAccepted: terminating = true; shortcut.stop()
    terminating_ = true;
    out.push({Kind::quitAccepted, state_.generation()});
    closeOverlay(false, out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::forceClose() {
    SystemOverlayEffects out;
    if (!state_.active()) return out;
    state_.forceClose();
    interaction_ = false;
    tearDown(out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::selectModule(Module module) {
    SystemOverlayEffects out; select(module, out); return out;
}
SystemOverlayEffects SystemOverlayLifecycle::didOpen(std::int64_t token) {
    SystemOverlayEffects out;
    if (state_.phase() != SystemOverlayPhase::opening || state_.generation() != token) return out;
    const auto action = state_.didOpen(token);
    interaction_ = state_.phase() == SystemOverlayPhase::open && !documentTermination_;
    perform(action, out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::didClose(std::int64_t token) {
    SystemOverlayEffects out;
    if (!state_.didClose(token)) return out;
    tearDown(out);
    return out;
}
SystemOverlayEffects SystemOverlayLifecycle::advance(double now) {
    if (!std::isfinite(now)) throw std::invalid_argument("System overlay requires a finite caller clock");
    SystemOverlayEffects out;
    for (;;) {
        std::size_t next = timerCount_;
        for (std::size_t n = 0; n < timerCount_; ++n) {
            if (timers_[n].due > now) continue;
            if (next == timerCount_ || timers_[n].due < timers_[next].due ||
                (timers_[n].due == timers_[next].due && timers_[n].serial < timers_[next].serial)) next = n;
        }
        if (next == timerCount_) return out;
        const auto timer = timers_[next];
        for (std::size_t n = next + 1; n < timerCount_; ++n) timers_[n - 1] = timers_[n];
        --timerCount_;
        if (terminating_ || suspended_ || state_.phase() != SystemOverlayPhase::open ||
            state_.generation() != timer.presentation) continue;
        select(Module::workMode, out);
    }
}
std::optional<double> SystemOverlayLifecycle::nextWakeTime() const noexcept {
    std::optional<double> result;
    for (std::size_t n = 0; n < timerCount_; ++n)
        if (!result || timers_[n].due < *result) result = timers_[n].due;
    return result;
}
std::optional<double> SystemOverlayLifecycle::pendingSelectionDue(std::size_t index) const noexcept {
    if (index >= timerCount_) return {};
    return timers_[index].due;
}
SystemOverlayEffects SystemOverlayLifecycle::requestTermination() {
    SystemOverlayEffects out;
    if (terminating_ && quitAfterSystemClose_) return out;
    quitRequested_ = true;
    terminating_ = true;
    out.push({Kind::quitAccepted, state_.generation()});
    if (!state_.active()) { out.push({Kind::terminate, state_.generation(), lastSystemModule_}); return out; }
    quitAfterSystemClose_ = true;
    closeOverlay(false, out);
    return out;
}
void SystemOverlayLifecycle::prepareForDocumentTermination() noexcept {
    documentTermination_ = true;
    interaction_ = false;
}
SystemOverlayEffects SystemOverlayLifecycle::cancelTermination() {
    SystemOverlayEffects out;
    documentTermination_ = false;
    quitRequested_ = false;
    quitAfterSystemClose_ = false;
    terminating_ = false;
    if (state_.phase() == SystemOverlayPhase::open && view_) interaction_ = true;
    out.push({Kind::restartShortcut, state_.generation()});
    return out;
}
} // namespace endfield::core
