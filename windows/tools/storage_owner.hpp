#pragma once
// Storage's platform services around its design canvas
// (modules/storage_canvas.hpp): the portable StorageController (the Mac
// StorageController's rules), its capacity read on the shared executor
// (native::StorageCapacityProbe: every listed disk with its numbers), the
// explicit details scan, the remembered disk (the preference record's
// "storageVolume", the Windows key of checklist A5; the Mac names it
// storageVolumeIdentifier) and mount events. The module itself is a
// tools::DesignModuleOwner over one StorageCanvas (makeOwner); this replaces
// tools/storage_preview, tools/storage_options and their adapter.
// No window, timer, thread or provider of its own.
#ifdef _WIN32
#include "modules/storage_canvas.hpp"
#include "native/storage_details_probe.hpp"
#include "native/storage_probe.hpp"
#include "tools/hud_design_owner.hpp"

namespace endfield::tools {
struct StorageServiceOptions {
    app::UtilityExecutor* utility{};
    // Worker: one capacity read (production: native::readWindowsStorage with
    // native::compareWindowsActivityNames; hidden coverage: synthetic).
    native::StorageCapacityProbe::Reader read;
    // UI thread, at each request: the startup disk's fallback name in the HUD
    // language ("Startup disk" / 启动磁盘).
    std::function<std::string()> startupName;
    std::function<double()> clock;            // owner time of a completion
    // Optional explicit details scan (no control shows it; kept with the
    // Mac controller's requestDetails).
    native::StorageDetailsProbe::Scanner scanDetails;
    native::StorageDetailsProbe::Clock completionClock;
    // Storage Settings: the host closes the HUD, then opens Windows Settings.
    std::function<void()> openSettings;
    // The remembered disk in the preference record, read at construction and
    // whenever the applied record changes (it may load later or be restored to
    // defaults); written on every choice. Without them the choice lasts for
    // the session.
    std::function<std::string()> loadVolume;
    std::function<void(const std::string&)> saveVolume;
};
class StorageService final {
public:
    // The host publishes its built-in Storage page under this key for module
    // areas (ApplicationHost::find), e.g. the hidden coverage checks.
    static constexpr std::string_view serviceKey = "storage";
    explicit StorageService(StorageServiceOptions);
    ~StorageService();
    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;
    // The module's owner (routing below; the volume menu captures pointer,
    // wheel and keys while open). Destroy it before this service. Its
    // DesignOwnerOptions::hostEvent feeds completions (utilityCompleted) and
    // the remembered disk (settingsChanged) to the canvas.
    std::unique_ptr<DesignModuleOwner> makeOwner(native::LayerRasterizer&, modules::design::Context&, DesignOwnerOptions);
    static app::ModuleRouting routing() noexcept;
    // StorageController.volumesDidChange: WM_DEVICECHANGE volume arrival or
    // removal (the Mac's mount / unmount / rename notifications). True when
    // the shown page changed (the host presents a frame).
    bool volumesChanged(double now);
    // Explicit model operation only (the canvas has no details control).
    bool requestDetails(bool refresh, double now);
    const modules::StorageController& controller() const noexcept;
    modules::StorageCanvas* canvas() const noexcept;   // while its owner lives
    native::StorageCapacityProbe::Stats probeStats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
