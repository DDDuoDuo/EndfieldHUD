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
    // Additional module owners (Now Playing, Media Assembly, Profile, Account,
    // App shortcuts, ...), constructed in order after the built-in modules.
    // Hosts append app/application_areas.hpp's registered areas here.
    std::vector<std::pair<std::string, ApplicationModuleFactory>> modules;
    // The verified resource package (ApplicationModuleContext::resource):
    // production, and hidden coverage given the staged package.
    std::function<std::filesystem::path(std::string_view id)> resources;
    // Hidden coverage: declared area fixtures (ApplicationModuleContext::fixture).
    std::function<std::filesystem::path(std::string_view id)> fixtures;
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
    void* window() const noexcept;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::app
