#pragma once
#include "core/motion.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace endfield::core {
enum class SystemOverlayPhase { closed, opening, open, closing };
std::string_view systemOverlayPhaseName(SystemOverlayPhase) noexcept;

struct SystemOverlayAction {
    enum class Kind { none, open, close };
    Kind kind{Kind::none};
    std::int64_t token{};
    bool operator==(const SystemOverlayAction&) const = default;
};

// Exact port of Sources/SystemOverlayState.swift. Serialized transitions:
// repeated toggles during opening/closing never start a second animation, and
// focus loss during an opening is queued until that opening completes.
class SystemOverlayState final {
public:
    SystemOverlayAction toggle() noexcept;
    SystemOverlayAction requestClose(bool interruptOpening = false) noexcept;
    SystemOverlayAction didOpen(std::int64_t token) noexcept;
    bool didClose(std::int64_t token) noexcept;
    void forceClose() noexcept;
    SystemOverlayPhase phase() const noexcept { return phase_; }
    std::int64_t generation() const noexcept { return generation_; }
    bool closeAfterOpening() const noexcept { return closeAfterOpening_; }
    bool active() const noexcept { return phase_ != SystemOverlayPhase::closed; }
private:
    SystemOverlayPhase phase_{SystemOverlayPhase::closed};
    std::int64_t generation_{};
    bool closeAfterOpening_{};
};

// Launch arguments understood by AppDelegate.applicationDidFinishLaunching.
struct SystemOverlayStartup {
    bool login{}, noOnboarding{}, settings{}, power{}, preview{};
    bool operator==(const SystemOverlayStartup&) const = default;
};

struct SystemOverlayEffect {
    enum class Kind {
        open,          // create the HUD presentation with `module` and start opening `token`
        close,         // start the closing animation of `token`
        select,        // select `module` in the live HUD
        preview,       // AppDelegate.preview (charge indicator preview)
        markLaunched,  // persist hasLaunched=true
        quitAccepted,  // onQuitAccepted: stop the summon shortcut
        terminate,     // onQuitAfterSystemClose: run the ordered document drain, then exit
        restartShortcut // termination cancelled after a save failure
    };
    Kind kind{};
    std::int64_t token{};
    Module module{Module::map};
    bool operator==(const SystemOverlayEffect&) const = default;
};
// Bounded, allocation-free effect list for one lifecycle event.
class SystemOverlayEffects final {
public:
    static constexpr std::size_t capacity = 6;
    void push(SystemOverlayEffect value);
    std::size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    const SystemOverlayEffect& operator[](std::size_t index) const;
    const SystemOverlayEffect* begin() const noexcept { return items_.data(); }
    const SystemOverlayEffect* end() const noexcept { return items_.data() + count_; }
    void append(const SystemOverlayEffects& other);
private:
    std::array<SystemOverlayEffect, capacity> items_{};
    std::size_t count_{};
};

// Port of the AppDelegate + OverlayController system-overlay policy:
// toggle/summon guards, the Map-first initial module, last-module retention
// across reopen, tray Settings/About/Work Mode requests, relaunch activation,
// first-run onboarding, the confirmed red power button and forced close.
// Pure caller-clock state; it owns no window, timer, shortcut or storage.
// The caller performs each returned effect in order, reports the end of the
// opening/closing animation with didOpen/didClose, and dispatches advance()
// at nextWakeTime() through its existing single deadline.
class SystemOverlayLifecycle final {
public:
    static constexpr double entranceDuration = 0.75; // SystemHUDView.entranceDuration
    static constexpr std::size_t maximumPendingSelections = 4;

    // AppDelegate entry points.
    SystemOverlayEffects launch(const SystemOverlayStartup&, bool hasLaunched, bool diagnostic = false);
    SystemOverlayEffects toggle();                     // summon hotkey: toggleSystemOverlay
    SystemOverlayEffects openOverlay();                // tray Open: openSystemOverlay
    SystemOverlayEffects openSettingsModule(Module);   // tray Settings (system) / About (about)
    SystemOverlayEffects openWorkMode(double now);     // tray Work Mode
    SystemOverlayEffects reopen();                     // relaunch: applicationShouldHandleReopen
    // OverlayController entry points.
    SystemOverlayEffects close(bool interruptOpening = false); // closeSystemOverlay
    SystemOverlayEffects requestQuit();                // confirmed red power button
    SystemOverlayEffects forceClose();                 // sleep, session end, termination
    SystemOverlayEffects selectModule(Module);         // selectSystemModule
    SystemOverlayEffects didOpen(std::int64_t token);  // finishSystemOpening
    SystemOverlayEffects didClose(std::int64_t token); // finishSystemClosing
    SystemOverlayEffects advance(double now);          // delayed Work Mode selection
    std::optional<double> nextWakeTime() const noexcept;
    // Windows tray Quit: request termination, closing the open HUD first with
    // its normal animation. A closed HUD terminates immediately.
    SystemOverlayEffects requestTermination();
    // AppDelegate.cancelPendingTerminationAfterSaveFailure.
    SystemOverlayEffects cancelTermination();
    void prepareForDocumentTermination() noexcept;

    void setSuspended(bool value) noexcept { suspended_ = value; }
    void setEditingPosition(bool value) noexcept { editingPosition_ = value; }
    void setAppLaunchInFlight(bool value) noexcept { appLaunchInFlight_ = value; }

    SystemOverlayPhase phase() const noexcept { return state_.phase(); }
    std::int64_t generation() const noexcept { return state_.generation(); }
    bool active() const noexcept { return state_.active(); }
    const SystemOverlayState& state() const noexcept { return state_; }
    std::optional<Module> viewModule() const noexcept { return view_; }
    bool interactionEnabled() const noexcept { return view_.has_value() && interaction_; }
    Module lastModule() const noexcept { return lastSystemModule_; }
    std::optional<Module> initialModuleRequest() const noexcept { return initialModuleRequest_; }
    bool quitRequested() const noexcept { return quitRequested_; }
    bool terminating() const noexcept { return terminating_; }
    bool suspended() const noexcept { return suspended_; }
    bool editingPosition() const noexcept { return editingPosition_; }
    bool hasLaunched() const noexcept { return hasLaunched_; }
    std::size_t pendingSelections() const noexcept { return timerCount_; }
    std::optional<double> pendingSelectionDue(std::size_t index) const noexcept;

private:
    struct Timer { double due{}; std::uint64_t serial{}; std::int64_t presentation{}; };
    SystemOverlayState state_;
    Module lastSystemModule_{Module::map};
    std::optional<Module> initialModuleRequest_, view_;
    bool interaction_{}, quitRequested_{}, quitAfterSystemClose_{}, editingPosition_{}, appLaunchInFlight_{};
    bool documentTermination_{}, suspended_{}, terminating_{}, hasLaunched_{};
    std::array<Timer, maximumPendingSelections> timers_{};
    std::size_t timerCount_{};
    std::uint64_t timerSerial_{};

    bool appToggle(SystemOverlayEffects&);
    bool overlayToggle(SystemOverlayEffects&);
    void perform(SystemOverlayAction, SystemOverlayEffects&);
    void closeOverlay(bool interruptOpening, SystemOverlayEffects&);
    void select(Module, SystemOverlayEffects&);
    void settingsModule(Module, SystemOverlayEffects&);
    void tearDown(SystemOverlayEffects&);
};
} // namespace endfield::core
