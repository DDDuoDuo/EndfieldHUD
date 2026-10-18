#pragma once
// Shared services and host actions the single Application owner offers to
// factory-added module areas (ApplicationModuleContext::host). Implemented by
// the Application; valid for the whole registration of the area's owner. All
// calls belong to the UI thread. See app/module_owner.hpp for the area
// contract and app/application.hpp for the context.
#include "app/module_owner.hpp"
#include "core/system_overlay_state.hpp"
#include "modules/event_log.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace ehud::data { struct Settings; }
namespace endfield::core { class ApplicationEventRecorder; }
namespace endfield::modules { class WorkModeController; }
namespace endfield::native {
class Renderer; class LayerRasterizer; class SystemServices; class AudioRuntime; class NativeMediaRequestBroker;
}
namespace endfield::app {
class UtilityExecutor;

enum class ApplicationAreaMode {
    production,         // EndfieldHUD.exe: real providers, persistent data root, verified package
    developmentPreview, // explicit visible watch_session_preview launch with synthetic inputs
    hiddenCoverage      // hidden watch_session_preview verification: synthetic inputs, never shown
};

// The shared native file dialog (one IFileOpenDialog owned by the host).
// By default multiple files and folders may be selected; the area validates
// and filters the returned paths itself. Captions are caller-localized.
// singleFile asks for exactly one existing file (Mac NSOpenPanel without
// multiple selection), listed through the filter "filterName (filterSpec)",
// e.g. "Images" / "*.png;*.jpg" (empty spec: every file).
// applications asks for one application (Mac: any installed .app): an
// existing .exe/.lnk through the filter, or a packaged application from
// AppsFolder returned as "shell:AppsFolder\\<AppUserModelID>".
struct ApplicationFileRequest {
    std::string title, acceptLabel, selectionLabel;
    bool singleFile{};
    std::string filterName, filterSpec;
    bool applications{}; // one application: an existing .exe/.lnk through the filter, or a packaged application from AppsFolder
};
struct ApplicationFileResult {
    bool cancelled{};               // the user, a module switch or the HUD closing cancelled it
    bool failed{};                  // the dialog could not run (the area shows its own error)
    std::vector<std::string> paths; // UTF-8 absolute Windows paths (validated)
};

class ApplicationHost {
public:
    // --- State ---------------------------------------------------------------
    virtual ApplicationAreaMode mode() const noexcept = 0;
    virtual double now() const = 0;                                      // shared monotonic clock (seconds); hidden coverage: the synthetic clock
    virtual const ehud::data::Settings& settings() const noexcept = 0;   // applied preference record
    virtual const ModuleAppearance& appearance() const noexcept = 0;     // last setAppearance value
    virtual core::Module selectedModule() const noexcept = 0;
    virtual core::SystemOverlayPhase overlayPhase() const noexcept = 0;  // production lifecycle phase
    virtual bool overlayVisible() const noexcept = 0;                    // HUD presented or animating (not concealed)
    virtual bool focused() const noexcept = 0;
    virtual const ClientMetrics& metrics() const noexcept = 0;
    // The HUD module whose source navigation entry is under the pointer
    // (HUDSourceWatchView hoverTarget == .module(m)); none for other buttons.
    virtual std::optional<core::Module> hoveredModule() const noexcept { return std::nullopt; }
    // True for a run over an explicit isolated data root (EndfieldHUD.exe
    // --data-root, the section 9 performance gate): no startup entry, no
    // shared notification identity, no restart registration.
    virtual bool isolatedDataRoot() const noexcept { return false; }
    // The Windows display adaptation of the design canvas and centre plane
    // (core/source_desktop_chrome.hpp desktopContentScale): ModuleFrame::chrome
    // carries it per frame; an area sizing a rest pose outside a frame (e.g.
    // a raster density) multiplies its DesktopChromeSettings with it. 1 on
    // the reference Mac display and wherever the Mac content covers the ring.
    virtual double contentScale() const noexcept { return 1; }

    // --- Shared services (null when this mode or configuration runs none) ---
    virtual native::Renderer& renderer() noexcept = 0;            // also passed to upload/collected/release
    virtual native::LayerRasterizer& rasterizer() noexcept = 0;
    virtual UtilityExecutor* utility() noexcept = 0;               // completions: hostEvent utilityCompleted
    virtual native::SystemServices* systemServices() noexcept = 0; // battery/clipboard notifications (production)
    virtual native::AudioRuntime* audio() noexcept = 0;            // the ONE audio worker (production Volume); hostEvent audioChanged
    virtual native::NativeMediaRequestBroker* mediaBroker() noexcept = 0; // shared image/video decode (Archive/Projection)
    virtual const modules::WorkModeController* workMode() const noexcept = 0;
    virtual core::ApplicationEventRecorder* eventRecorder() noexcept = 0; // system transitions into the Event Log (production)

    // --- Host actions ----------------------------------------------------------
    virtual void requestFrame() = 0;                                       // one coalesced frame
    virtual void recordEvent(modules::EventKind, modules::EventMetadata) = 0; // Event Log (no-op without it)
    // Through the production lifecycle (SystemOverlayLifecycle): selectModule
    // switches the open HUD (deferred while a module refuses to release the
    // selection); openModule opens the HUD on that module (tray Settings path);
    // closeOverlay plays the normal close.
    virtual void selectModule(core::Module) = 0;
    virtual void openModule(core::Module) = 0;
    virtual void closeOverlay() = 0;
    // Opens an explicit https:// page in the user's browser once the HUD has
    // closed (Mac overlay.afterSystemClose + closeSystemOverlay), at once while
    // it is already closed: another application never opens behind the shown
    // HUD's topmost band. Dropped by a quit or a forced close. Hidden runs
    // count it and open nothing. Default: ignored (hosts without a browser).
    virtual void openLinkAfterClose(std::string /*url*/) {}
    // Mutates the committed preference record through the Settings owner
    // (normalized, persisted on the shared executor, then applied as a
    // settingsChanged broadcast). False without the Settings owner.
    virtual bool updateSettings(const std::function<void(ehud::data::Settings&)>&) = 0;
    // Queues the shared dialog; the completion runs later on the UI thread
    // (also when a module switch, the HUD closing or cancelFileChoice() ends
    // it). False when the dialog is unavailable or busy (no completion).
    virtual bool chooseFiles(const ApplicationFileRequest&, std::function<void(ApplicationFileResult)>) = 0;
    virtual void cancelFileChoice() = 0;
    // A private WM_APP message id. Messages with it, posted or sent to the HUD
    // HWND (context.window), reach the owners' message() chain: give the owner
    // a ModuleRouting::message priority. At most 0x3000 ids per run.
    virtual std::uint32_t registerMessage() = 0;

    // --- Area-to-area services -------------------------------------------------
    // Non-owning, exact-type directory: the publisher keeps the object alive
    // for its owner's registration and withdraws it from its destructor. The
    // host clears every entry before any area owner is destroyed. Areas are
    // destroyed in reverse registration order, so look a service up when it is
    // used rather than caching one published by a later area.
    template <class T> void publish(std::string_view key, T& value) { publishService(key, &value, typeid(T)); }
    void withdraw(std::string_view key) { publishService(key, nullptr, typeid(void)); }
    template <class T> T* find(std::string_view key) const { return static_cast<T*>(findService(key, typeid(T))); }

protected:
    ~ApplicationHost() = default;
    // Null value withdraws. Publishing an existing key with another object throws.
    virtual void publishService(std::string_view key, void* value, const std::type_info& type) = 0;
    // Null when absent or published with another type.
    virtual void* findService(std::string_view key, const std::type_info& type) const = 0;
};
} // namespace endfield::app
