#pragma once
// EndfieldHUD application owner: the single lifecycle/state owner of the
// Windows HUD (Mac AppDelegate + OverlayController). It owns COM/OLE, the UI
// DispatcherQueue, OverlayHost, Renderer, desktop backdrop, the source watch
// session and chrome, the shared UtilityExecutor, settings/event-log saves,
// tray and hotkey, the shared media broker and every module owner, registered
// through ModuleRegistry (app/module_owner.hpp).
//
// EndfieldHUD.exe constructs it in production mode with the versioned data
// root and packaged resources. watch_session_preview constructs the SAME owner
// in its development modes with explicit synthetic inputs, so hidden coverage
// exercises production code.
#include "app/application_host.hpp"
#include "app/module_owner.hpp"
#include "core/system_overlay_state.hpp"
#include "modules/event_log.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native { class LayerRasterizer; }
namespace endfield::app {
class UtilityExecutor;
// Everything a module owner may borrow from the single Application owner.
// Valid for the owner's whole registration; all access on the UI thread.
// Area contract: app/module_owner.hpp ("Adding a module area").
struct ApplicationModuleContext {
    native::LayerRasterizer& rasterizer;
    void* window{};              // the HUD HWND (routes WM_APP notifications)
    UtilityExecutor* utility{};  // shared bounded file/work executor (never null for factories)
    std::filesystem::path dataRoot; // explicit appRoot (Mac relative layout below it); a NEW temporary root outside production
    void* textManager{};         // Notes' activated ITfThreadMgr* (borrow only)
    std::uint32_t textClient{};  // its TfClientId
    ClientMetrics metrics;       // current client metrics (resize() follows)
    std::function<double()> clock;                                       // shared monotonic clock (hidden coverage: the synthetic coverage clock)
    std::function<void()> invalidate;                                    // request one coalesced frame
    std::function<void(modules::EventKind, modules::EventMetadata)> recordEvent; // Event Log (no-op without it)
    // Verified root of a packaged resource the area declared with
    // ehud_app_resource() (cmake/modules/app.cmake); empty when it is missing
    // or damaged (the owner then stays absent). Hidden coverage resolves the
    // build's staged package when the harness is given --package; development
    // previews resolve nothing.
    std::function<std::filesystem::path(std::string_view id)> resource;
    // --- Area registration (app/module_owner.hpp) -------------------------------
    std::string_view name;          // registered area name (stable, camelCase)
    ApplicationAreaMode mode{};     // production, development preview or hidden coverage
    ApplicationHost* host{};        // shared services and host actions (never null)
    // Hidden coverage only: a NEW empty directory owned by this area for
    // synthetic fixture inputs (inside the temporary data root); empty otherwise.
    std::filesystem::path coverageRoot;
    // Hidden coverage only: an absolute fixture declared with
    // EHUD_APP_AREA_FIXTURES "<id>|<path>"; empty for unknown ids and outside coverage.
    std::function<std::filesystem::path(std::string_view id)> fixture;
};
// A module area provides a factory; the Application registers the result
// after its built-in owners. Returning null leaves the module absent.
using ApplicationModuleFactory = std::function<std::unique_ptr<ModuleOwner>(const ApplicationModuleContext&)>;

enum class ApplicationMode {
    production,     // EndfieldHUD.exe: real providers, tray, lifecycle, persistent root
    visiblePreview, // development: explicit visible launch with synthetic inputs
    hiddenBenchmark // development: hidden offscreen benchmark/coverage, never shown
};

struct ApplicationOptions {
    ApplicationMode mode{ApplicationMode::production};
    // Source shell: runtime input (or development packet), compiled material
    // catalog and its SHA-256 pin, shader, optional chrome/cursor/backdrop timing.
    std::filesystem::path packet, cache, shader, chrome, cursor, watchBlur;
    std::string cachePin;
    bool runtimeInput{}, warp{};
    // Module asset roots (empty: module not installed).
    std::filesystem::path notesAssets, notesFormatAssets, shelfAssets, shelfMask, clipboardAssets,
        residentIcons, settingsAssets, archiveAssets, storageAssets, activityAssets,
        mapGeography, mapPlayerAssets, orbipomAssets;
    std::string notesAssetsSHA;
    // Data: every store's explicit appRoot. Shelf may use a separate root.
    std::filesystem::path dataRoot, shelfDataRoot;
    // Optional modules and providers.
    bool reader{}, calendar{}, projection{};
    bool nativeClipboard{}, nativeActivity{};            // OS readers (visible only)
    bool volumeFixture{}, eventLogFixture{}, workModeFixture{}, batteryFixture{}; // synthetic previews
    bool nativeVolume{}, nativeBattery{}, nativeEventLog{}, nativeStorage{}; // production providers
    // Hidden verification only.
    bool moduleCoverage{}, coverage{}, readerScrollTrace{}, sessionEndCoverage{};
    std::filesystem::path report, snapshots, liveDiagnostics;
    std::uint32_t benchmarkWidth{1280}, benchmarkHeight{800};
    double benchmarkEpoch{};
    // Production lifecycle.
    core::SystemOverlayStartup startup;
    bool persistLaunchMarker{true};
    bool isolateModuleFailures{};
    std::wstring windowTitle;
    // EndfieldHUD.exe --data-root (section 9 gate): no startup entry, no
    // shared notification identity (the calendarReminders area stays absent).
    bool isolatedDataRoot{};
    // Production: after the launch effects, open the HUD on this module (a
    // fresh run after GPU device loss restores the module that was open).
    std::optional<core::Module> reopenModule;
    // Additional module owners (Now Playing, Media Assembly, Profile, Account,
    // App shortcuts, ...), constructed in order after the built-in modules.
    // Hosts append app/application_areas.hpp's registered areas here.
    std::vector<std::pair<std::string, ApplicationModuleFactory>> modules;
    // The verified resource package (ApplicationModuleContext::resource):
    // production, and hidden coverage given the staged package.
    std::function<std::filesystem::path(std::string_view id)> resources;
    // Hidden coverage: declared area fixtures (ApplicationModuleContext::fixture).
    std::function<std::filesystem::path(std::string_view id)> fixtures;
    // Opt-in startup timing trace (numbers only, never content): construction
    // stages, launch -> first frame and the frame pacing of the first and
    // second openings, written as JSON to this new file. Production enables
    // it with ENDFIELDHUD_STARTUP_TRACE=1 (Diagnostics/startup-trace.json in
    // the data root); watch_session_preview --startup-trace runs the same
    // owner hidden on the synthetic clock instead of the module coverage.
    std::filesystem::path startupTrace;
    // Hidden trace comparison only: skip the launch prewarm before the first opening.
    bool startupTraceWithoutPrewarm{};
    // Hidden scheduling trace (watch_session_preview --contention-trace): the
    // opened HUD paced at 60 Hz on the hardware GPU, alternately at normal
    // priority and with PresentationPriority raised, reporting the wake
    // lateness, CPU frame time and GPU completion per frame (numbers only).
    // Run it while a load (tools/application_contention_stress) is active.
    std::filesystem::path contentionTrace;
    // Opt-in memory trace (numbers, owner names and system module file names
    // only; app/process_diagnostics.hpp): process private bytes and working
    // set, heaps, address space, per-adapter GPU usage, the shared raster
    // cache and renderer resources by owner, owner-reported residency and
    // every thread by start address. Production enables it with
    // ENDFIELDHUD_MEMORY_TRACE=1 (Diagnostics/memory-trace.json in the data
    // root: after construction, at launch idle, after the first openings and
    // closings); watch_session_preview --memory-trace runs the same owner
    // hidden through launch, a cold opening, a visit of every module, closing
    // and ten more open/close cycles instead of the module coverage.
    std::filesystem::path memoryTrace;
    // Hidden idle trace (watch_session_preview --idle-trace): the same owner
    // on the REAL clock through the production frame scheduling (OverlayHost
    // presents the never-shown window for scheduling; offscreen hardware
    // renderer, no swap chain or compositor), measured in the section 9 idle
    // states - closed after launch, open idle with ambient animation on and
    // off, closed after a close - for idleTraceSeconds each: the process's
    // wake-ups and CPU per thread, the frames, their cadence and their CPU
    // per stage and owner, and what demanded them. Numbers and owner/module
    // names only. Finite motion is paced at idleTraceRefreshRate (the
    // laptop's 165 Hz display), which the hidden session lacks.
    std::filesystem::path idleTrace;
    double idleTraceSeconds{30}, idleTraceRefreshRate{165};
};

class Application final {
public:
    explicit Application(ApplicationOptions);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    // Visible/production: run the event loop until quit; returns the exit code.
    int run();
    // Hidden benchmark/coverage (watch_session_preview only; compiled in the
    // separate hud_application_verification library).
    int runHiddenVerification();
    // Second-launch activation forwarded by the single-instance guard.
    void activate(std::span<const std::string> arguments);
    // After run() returned: the work of an accepted DataRootRestart
    // (app/areas/mac_import_handoff.hpp), returned once; empty otherwise.
    // The host runs it with this Application destroyed, then a fresh run.
    std::function<void()> takeRestartWork();
    // After run() returned: set when a lost GPU device ended the run; the
    // module to reopen (the HUD's module when it was open, else none).
    std::optional<std::optional<core::Module>> takeDeviceRecovery();
    void* window() const noexcept;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::app
