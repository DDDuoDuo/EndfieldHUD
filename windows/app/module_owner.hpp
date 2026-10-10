#pragma once
// EndfieldHUD module-owner interface and registry.
//
// Every HUD module (Notes, File Shelf, Clipboard, Volume, Event Log, Work Mode,
// Power, Settings, Storage, Activity Monitor, Reader, Calendar, Map, Minigame,
// Archive, and later Now Playing, Media Assembly, Profile, Account Linking,
// App shortcuts) joins the single Application owner through ONE ModuleOwner.
// The Application constructs the module's owner object, wraps it in a
// ModuleOwner adapter and calls ModuleRegistry::add(). From then on the module
// automatically receives every shared event; none can be silently skipped:
//
//   resize(metrics)                     window/DPI change (zero-size while hidden)
//   setAppearance(appearance, time)     language, theme, accent, reduce-motion,
//                                       ambient and low-power (one event, no poll)
//   setOverlayVisible(true, time)       HUD opening
//   overlayClosing(time)                HUD closing: cancel gestures, then hide
//   update(frame)                       once per presented frame, after Notes,
//                                       with the source center plane, chrome
//                                       settings, module presentation and opacity
//   requiresFrames(time)                finite animation demand on the shared clock
//   nextWakeTime(now) / deadline(time)  one shared waitable timer; no module timer
//   upload / entries(module) / floatingEntries / modalEntries / collected
//                                       one LayerComposition, ordered by the owner
//   release(renderer)                   GPU release, reverse registration order
//   covers / pointerLocked / capturesPointer|Wheel|Keys / pointer / wheel /
//   key / filterKey / message           input, routed by ModuleRouting priority
//   focus(bool, time)                   keyboard focus gained/lost
//   releasesSelection(time)             may the selected module be switched away?
//   finishEditing(time)                 commit text transactions before close
//   flush(time)                         bounded durable drain at quit/session end
//
// Threading: every method runs on the Application's UI thread. Owners use the
// shared UtilityExecutor for file work and the shared frame clock for motion;
// they must not create timers, threads or polling loops. Warm frames must not
// allocate. Each call is isolated: when ModuleRegistry isolates failures, an
// exception disables only that owner (its entries disappear, input skips it,
// its GPU resources are still released) and is logged without user content.
// Essential owners (Notes, which owns module selection and the TSF manager)
// are never isolated: their failure ends the run as before.
//
// Wiring a module area into EndfieldHUD.exe (and the hidden coverage harness):
//   1. Implement a ModuleOwner adapter over the area's owner (the pattern of
//      app/application_modules.cpp): forward exactly the events above, return
//      the module from presents(), pick ModuleRouting priorities relative to
//      the built-in owners (Notes pointer/key 1, modal captures 0..2) and
//      report a stable name() (it appears in failure diagnostics).
//   2. Provide an ApplicationModuleFactory (app/application.hpp). It receives
//      the shared rasterizer, HWND, UtilityExecutor, data root, Notes' TSF
//      manager, metrics, clock, invalidate, Event Log and the verified
//      package resources; returning null leaves the module absent.
//   3. In the area's CMake fragment: `if(TARGET EndfieldHUD)` link its
//      libraries to EndfieldHUD, and declare runtime inputs with
//      ehud_app_resource(<id> file|directory <source> <destination>).
//   4. The host adds the factory to ApplicationOptions::modules in app/main.cpp
//      (and, for hidden coverage, in tools/watch_session_preview.cpp).
// A factory-added owner is registered after the built-in owners, so it
// receives every shared event automatically and is released first.
#include "app/overlay_host.hpp"
#include "core/localization.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "native/layer_scene.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace endfield::native { class Renderer; }

namespace endfield::app {
struct ModuleAppearance {
    core::Language language{core::Language::english};
    bool dark{true};
    std::array<double, 4> accent{};     // effective accent: dark ? configured : black-blend .35; alpha 1
    std::array<double, 3> accentSRGB{}; // configured accentHex
    bool reduceMotion{}, ambient{true}, lowPower{};
    bool operator==(const ModuleAppearance&) const = default;
};
struct ModuleFrame {
    const core::Matrix4& center;
    const core::source::DesktopChromeSettings& chrome;
    const core::ModulePresentationSample& presentation;
    float opacity{};
    double time{};
    bool focused{};
};
// How the Application applies a consumed pointer event to the source session:
// whether the HUD pointer/lock environment updates even when unhandled, whether
// it includes the aggregated pointerLocked() state, and whether pointer capture
// follows only the left button (Map, Minigame).
struct PointerPolicy {
    bool alwaysUpdateEnvironment{true}, updatesPointerLock{true}, leftButtonCaptureOnly{};
};
// Lower numbers route first; noRoute excludes the owner from that chain. The
// capture chains run before the floating Notes layer; the main chains follow.
struct ModuleRouting {
    static constexpr int noRoute = -1;
    int pointerCapture{noRoute}, pointer{noRoute};
    int wheelCapture{noRoute}, wheel{noRoute};
    int keyCapture{noRoute}, key{noRoute};
    int filterKey{noRoute}, message{noRoute};
    int finishEditing{noRoute}, flush{noRoute};
    PointerPolicy pointerPolicy;
    bool filterRequiresOwnerTarget{};   // only keys queued for the HUD HWND/children
    bool messageRetriesPendingModule{}; // a consumed message may complete a deferred switch
    bool messageRetriesPendingClose{};  // a consumed message may complete a deferred close
    bool wheelRefreshesPointerLock{}, deadlineRefreshesPointerLock{};
};

class ModuleOwner {
public:
    virtual ~ModuleOwner() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual ModuleRouting routing() const noexcept { return {}; }
    virtual bool essential() const noexcept { return false; }
    // HUD modules whose center content this owner draws (Settings draws four).
    virtual bool presents(core::Module) const noexcept { return false; }

    virtual void resize(const ClientMetrics&) {}
    virtual void setAppearance(const ModuleAppearance&, double) {}
    virtual void setOverlayVisible(bool, double) {}
    virtual void overlayClosing(double time) { cancelInteraction(time); setOverlayVisible(false, time); }
    virtual void update(const ModuleFrame&) {}
    virtual bool requiresFrames(double) const { return false; }
    virtual std::optional<double> nextWakeTime(double) const { return {}; }
    virtual bool deadline(double) { return false; } // true: artwork changed

    virtual void upload(native::Renderer&) {}
    virtual std::span<const native::LayerCompositionEntry> entries(core::Module) { return {}; }
    virtual std::span<const native::LayerCompositionEntry> floatingEntries() { return {}; }
    virtual std::span<const native::LayerCompositionEntry> modalEntries() { return {}; }
    virtual void collected(native::Renderer&) {}
    virtual void release(native::Renderer&) {}

    virtual bool covers(core::Point) const { return false; }
    virtual bool pointerLocked() const { return false; }
    virtual bool capturesPointer() const { return false; }
    virtual bool capturesWheel() const { return false; }
    virtual bool capturesKeys() const { return false; }
    virtual bool suppressesKeyFilters() const { return false; } // modal capture of raw keys
    virtual bool pointer(const PointerEvent&, double) { return false; }
    virtual bool wheel(const WheelEvent&, double) { return false; }
    virtual bool key(const KeyEvent&, double) { return false; }
    virtual bool filterKey(const NativeMessage&) { return false; }
    virtual bool message(const NativeMessage&, double) { return false; }
    virtual void focus(bool, double) {}
    virtual void cancelInteraction(double) {}

    virtual bool releasesSelection(double) { return true; }
    virtual bool finishEditing(double) { return true; }
    virtual bool flush(double) { return true; }
};

// Failure report delivered once per disabled owner. `what` is the exception's
// dynamic type name only, never its message (messages can contain paths).
struct ModuleFailure { std::string owner, what, operation; };

// Owner-thread registry. Registration allocates; routing on warm frames does
// not. Owners must outlive their registration (remove() before destruction).
class ModuleRegistry final {
public:
    enum class Chain { pointerCapture, pointer, wheelCapture, wheel, keyCapture, key, filterKey, message, finishEditing, flush, count };
    explicit ModuleRegistry(bool isolateFailures = false) : isolate_(isolateFailures) {}
    void setFailureObserver(std::function<void(const ModuleFailure&)> observer) { observer_ = std::move(observer); }
    void setIsolation(bool value) noexcept { isolate_ = value; }
    bool isolating() const noexcept { return isolate_; }

    void add(ModuleOwner&);
    void remove(ModuleOwner&) noexcept;
    bool contains(const ModuleOwner&) const noexcept;
    bool enabled(const ModuleOwner&) const noexcept;
    std::size_t size() const noexcept { return slots_.size(); }
    std::span<const ModuleFailure> failures() const noexcept { return failures_; }

    // Registration order (construction order). Disabled owners are skipped.
    template <class F> void forEach(F&& f, std::string_view operation = "event") {
        const Iteration scope(*this);
        for (auto& slot : slots_) if (!slot.failed) run(slot, [&] { f(*slot.owner); }, operation);
    }
    template <class F> bool any(F&& f, std::string_view operation = "query") {
        const Iteration scope(*this);
        for (auto& slot : slots_) {
            if (slot.failed) continue;
            bool result{};
            run(slot, [&] { result = f(*slot.owner); }, operation);
            if (result && !slot.failed) return true;
        }
        return false;
    }
    // Ordered chain; stops at the first owner whose callback returns true.
    template <class F> ModuleOwner* first(Chain chain, F&& f, std::string_view operation = "input") {
        const Iteration scope(*this);
        for (auto* slot : chains_[static_cast<std::size_t>(chain)]) {
            if (slot->failed) continue;
            bool result{};
            run(*slot, [&] { result = f(*slot->owner, slot->routing); }, operation);
            if (result && !slot->failed) return slot->owner;
        }
        return nullptr;
    }
    // The owner presenting a HUD module, if any (first registered wins).
    ModuleOwner* presenter(core::Module) const noexcept;
    bool covers(core::Point);
    bool pointerLocked();
    bool requiresFrames(double time);
    std::optional<double> nextWakeTime(double now);
    void releaseAll(native::Renderer&) noexcept; // reverse registration order, failed owners included
    // Run one call with the registry's isolation policy (for owner-specific calls).
    template <class F> bool guard(ModuleOwner& owner, F&& f, std::string_view operation) {
        auto* slot = find(owner);
        if (!slot || slot->failed) return false;
        run(*slot, std::forward<F>(f), operation);
        return !slot->failed;
    }

private:
    struct Slot { ModuleOwner* owner{}; ModuleRouting routing; bool failed{}; };
    std::vector<Slot> slots_;
    std::array<std::vector<Slot*>, static_cast<std::size_t>(Chain::count)> chains_;
    std::vector<ModuleFailure> failures_;
    std::function<void(const ModuleFailure&)> observer_;
    bool isolate_{};
    unsigned iterating_{};
    struct Iteration {
        ModuleRegistry& registry;
        explicit Iteration(ModuleRegistry& r) noexcept : registry(r) { ++registry.iterating_; }
        ~Iteration() { --registry.iterating_; }
        Iteration(const Iteration&) = delete;
        Iteration& operator=(const Iteration&) = delete;
    };
    Slot* find(const ModuleOwner&) noexcept;
    const Slot* find(const ModuleOwner&) const noexcept;
    void rebuild();
    void disable(Slot&, const char* what, std::string_view operation);
    template <class F> void run(Slot& slot, F&& f, std::string_view operation) {
        if (!isolate_ || slot.owner->essential()) { f(); return; }
        try { f(); }
        catch (const std::exception& e) { disable(slot, typeid(e).name(), operation); }
        catch (...) { disable(slot, "unknown", operation); }
    }
};
} // namespace endfield::app
