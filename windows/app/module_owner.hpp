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
//   hostEvent(event, time)              application broadcasts (HostEvent below)
//   coverage(step)                      hidden coverage steps (ModuleCoverage below)
//   planeEntries()                      core-plane layers (after the center module,
//                                       below every floatingEntries(), e.g. the
//                                       Power charge badge under pinned Notes)
//   drop(paths, point, time)            a file drop on the HUD while this owner
//                                       presents the selected module (the host's
//                                       one OLE drop target; copy effect only)
//   automationRefresh / automationCount / automationChild /
//   automationFromPoint / automationDisconnect
//                                       UI Automation fragments under the HUD
//                                       window's root provider (below)
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
// Host broadcasts (hostEvent, below) carry everything else an owner may need
// from the Application without a host edit: shared utility completions, audio
// publications, the end of a close, suspend/resume, clock/time-zone and system
// setting changes, display topology, application activation, preference
// changes and an accepted quit.
//
// ---------------------------------------------------------------------------
// Adding a module area (stage-2 contract; no shared file is edited)
// ---------------------------------------------------------------------------
//   1. Owner adapter. In windows/app/areas/<area>_module.{hpp,cpp} write a
//      ModuleOwner over the area's owner object (pattern: app/application_
//      modules.cpp). Forward exactly the events above; return the HUD modules
//      it draws from presents() (the first registered presenter of a module
//      wins, so an area cannot take over a built-in module); report a stable
//      camelCase name() equal to its registered name; choose
//      ModuleRouting priorities relative to the built-in owners (they use
//      0...19: Notes pointer/key 1, modal captures 0...6) - areas normally use
//      20...99, equal priorities keep registration order. Override hostEvent()
//      for the broadcasts it needs and coverage() for hidden coverage (6).
//   2. Factory. Declare in that header and define in the .cpp
//        std::unique_ptr<endfield::app::ModuleOwner>
//        <namespace>::<area>Module(const endfield::app::ApplicationModuleContext&);
//      It runs once on the UI thread after every built-in owner exists and
//      returns null to leave the module absent. The context
//      (app/application.hpp) carries the mode (production, development preview
//      or hidden coverage), the shared rasterizer, HWND, UtilityExecutor,
//      versioned data root, Notes' TSF manager/client, metrics, clock,
//      invalidate, Event Log record, verified package resources and, in hidden
//      coverage, a NEW per-area coverage root plus declared fixtures. Its
//      `host` (app/application_host.hpp) gives the remaining shared services
//      (renderer, settings, appearance, SystemServices, AudioRuntime, Work Mode,
//      shared media broker, Event Log recorder), host actions (select or open a
//      module, close the HUD, update preferences, the shared file dialog, a
//      private WM_APP message routed to message()) and a typed directory for
//      area-to-area services. Production failures in a factory or any owner
//      call disable only that area.
//   3. Library and registration. In windows/cmake/modules/<area>.cmake build
//      the adapter into a library linking the area's owner libraries,
//      hud_module_registry (headers of app/) and hud_utility when it calls the
//      UtilityExecutor (ApplicationHost needs no link), then register it
//      (fragments load in sorted order;
//      a GLOBAL property needs no helper, so the fragment may sort before or
//      after app.cmake):
//        if(WIN32)
//          set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
//            "<name>|<library target>|<header relative to windows/>|<qualified factory>")
//        endif()
//      A fifth field "|coverage" registers a test-only area (hidden coverage
//      only; never linked into EndfieldHUD.exe). app.cmake generates, once all
//      fragments are read, the translation units behind
//      app/application_areas.hpp: EndfieldHUD.exe (app/main.cpp) and the
//      development harness (tools/watch_session_preview.cpp) append every
//      registered factory to ApplicationOptions::modules and link the listed
//      libraries. Nothing in app/ or tools/ is edited per area.
//   4. Runtime inputs. Declare packaged files from the same fragment with
//        ehud_app_resource(<id> file|directory <absolute source> <relative destination>)
//      (written after all fragments). The factory resolves them with
//      context.resource(id): the SHA-256 verified package in production, and
//      in hidden coverage the staged package of the same build (passed to the
//      harness with --package; empty without it, as in development previews).
//      Coverage-only fixtures (windows/tests/fixtures/...) are declared with
//        set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES "<id>|<absolute path>")
//      and resolved with context.fixture(id) in hidden coverage only.
//   5. Lifetime. Owners are registered after the built-in owners in
//      registration order, receive every shared event automatically, are
//      released (GPU) in reverse order and destroyed before the built-in owners
//      (they may borrow Notes' TSF manager), all before the UtilityExecutor,
//      the AudioRuntime and the HWND. Never create a timer, thread or polling
//      loop: use nextWakeTime()/deadline(), requiresFrames(), hostEvent() and
//      the UtilityExecutor; warm frames must not allocate.
//   6. Hidden coverage. watch_session_preview --module-coverage (and the
//      application_owner_coverage CMake test, which adds --package) constructs
//      every registered area in ApplicationAreaMode::hiddenCoverage. Inside the
//      combined module cycles each presented module is selected and rendered
//      like a built-in one (its raster usage must repeat exactly across the
//      three cycles: bounded caches) and coverage() is called with that
//      module; once per cycle every area also receives coverage() without a
//      module. Given the staged package the factory must return an owner in
//      hidden coverage, with synthetic providers instead of OS services. Use
//      ModuleCoverage::present/drain/project/message/check with synthetic
//      inputs from the coverage root and fixtures only: never real user data,
//      clipboard, audio, accounts, installed apps, startup entries or the
//      registry. Coverage runs on a synthetic clock (context.clock and
//      host->now() follow it): never SendMessage to the HUD HWND or read the
//      wall clock there, and drain the UtilityExecutor with drain(). The
//      harness additionally audits every area owner: constructed, resized,
//      appearance, open/close, frames, upload, the host broadcasts and exactly
//      one release, and the CMake test checks every registered name reported.
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
// UI Automation provider interfaces (uiautomationcore.h), used by pointer only.
struct IRawElementProviderFragment;
struct IRawElementProviderFragmentRoot;

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

// Application broadcasts delivered to every enabled owner in registration
// order (ModuleRegistry::broadcast). hostEvent() returns true when the owner's
// artwork changed so the host presents one frame.
struct HostEvent {
    enum class Kind : std::uint8_t {
        utilityCompleted,      // the shared UtilityExecutor drained: completions ran, queue capacity is free
        audioChanged,          // the shared AudioRuntime adopted a publication (flags: Audio bits)
        overlayConcealed,      // the HUD finished closing (or was force-concealed) and is hidden
        suspended, resumed,    // sleep, display off or session lock began / every such reason ended (flags: Suspension reason)
        timeChanged,           // WM_TIMECHANGE: wall clock or time zone
        systemSettingsChanged, // WM_SETTINGCHANGE: theme, locale, work area, ...
        displaysChanged,       // display topology or resolution
        applicationActivated, applicationDeactivated, // WM_ACTIVATEAPP
        settingsChanged,       // the applied preference record (ApplicationHost::settings()) changed
        quitting,              // quit or session end accepted, before the flush chain (a failed save may cancel a quit)
        count
    };
    enum Audio : std::uint32_t { audioSnapshot = 1, audioVolume = 2, audioApplications = 4, audioDevices = 8 };
    enum Suspension : std::uint32_t { suspendSystem = 1, suspendDisplay = 2, suspendSession = 4 };
    Kind kind{};
    std::uint32_t flags{};
};
std::string_view hostEventName(HostEvent::Kind) noexcept;

// One hidden-coverage step (ApplicationAreaMode::hiddenCoverage only). The
// harness owns the synthetic clock; every helper runs on the UI thread.
struct ModuleCoverage {
    std::optional<core::Module> module; // presented module selected for this step; nullopt: per-cycle area step
    unsigned cycle{};                    // 0...2 combined module cycles
    double time{};                       // synthetic coverage time of the step
    std::function<void(double)> present; // present one hidden frame at that (non-decreasing) time
    std::function<void()> drain;         // finish queued UtilityExecutor work, then broadcast utilityCompleted
    // A point on `module`'s width x height point canvas (top-left origin) ->
    // logical client point through the current module plane; empty without a
    // module or when the point does not project.
    std::function<std::optional<core::Point>(core::Point, double width, double height)> project;
    std::function<void(bool, std::string_view)> check; // counted assertion; throws on failure
    // Routes a native message (e.g. an ApplicationHost::registerMessage id)
    // through the owners' message() chain exactly as the host routes a WM_APP
    // notice, but on the coverage clock; true when an owner consumed it. Do not
    // SendMessage to the HWND in coverage: the real route reads the wall clock.
    std::function<bool(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam)> message;
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
    // Core-plane slot: composed after the center module entries and before
    // every owner's floatingEntries() (Mac SystemHUDView canvas order: the
    // charge plane sits inside the canvas, below notesWorkspace).
    virtual std::span<const native::LayerCompositionEntry> planeEntries() { return {}; }
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

    virtual bool hostEvent(const HostEvent&, double) { return false; } // true: artwork changed
    virtual void coverage(ModuleCoverage&) {}                          // hidden coverage only

    // File drop on the HUD (Mac SystemHUDView.performDragOperation). The host
    // owns the window's one OLE drop target; while this owner presents the
    // selected module and the HUD accepts input, a copy drop inside the client
    // area is offered here after the COM callback returned (UTF-8 absolute
    // paths, logical client point). True when the owner took the drop.
    virtual bool drop(std::span<const std::string>, core::Point, double) { return false; }

    // UI Automation (UIA). The HUD window answers WM_GETOBJECT with one root
    // provider (IRawElementProviderFragmentRoot, hosted by the HWND). Every
    // enabled owner with fragments appears under it as one group, in
    // registration order; `root` is that owner's group, stable for the
    // owner's registration: use it as the fragments' parent and fragment root.
    // automationRefresh runs on content events (after an owner call reported
    // changed artwork, module selection, opening and closing), never per
    // frame; true when the owner's child set or order changed (the host then
    // raises StructureChanged). automationChild/automationFromPoint return
    // AddRef'd fragments (HRESULTs as long; screen pixels for the hit test,
    // null out for none). automationDisconnect disconnects every fragment
    // (HUD hidden, owner removed); the next refresh may connect new ones.
    virtual bool automationRefresh(IRawElementProviderFragmentRoot*) { return false; }
    virtual std::size_t automationCount() const { return 0; }
    virtual long automationChild(std::size_t, IRawElementProviderFragment** out) const { if (out) *out = nullptr; return 0x80070057L; } // E_INVALIDARG
    virtual long automationFromPoint(double, double, IRawElementProviderFragment** out) const { if (out) *out = nullptr; return 0; }
    virtual void automationDisconnect() noexcept {}
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
    // Delivers one host broadcast to every enabled owner (isolated like any
    // event); true when any owner's artwork changed.
    bool broadcast(const HostEvent&, double time);
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
