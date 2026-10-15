#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/storage_scene.hpp"
#include "native/storage_probe.hpp"
#include "native/storage_details_probe.hpp"
#include "native/module_scene.hpp"
namespace endfield::tools {
// The production Windows readers (tools/storage_options.cpp), as distinct
// types so the owner can tell them from injected (synthetic) readers.
struct WindowsStorageCapacityReader {std::optional<modules::StorageCapacity>operator()()const;};
struct WindowsStorageVolumeList {std::vector<modules::StorageVolume>operator()()const;};
struct WindowsStorageVolumeCapacity {std::optional<modules::StorageCapacity>operator()(const std::string&volumeID)const;};
struct StoragePreviewOptions {
    app::UtilityExecutor*utility{};native::StorageCapacityProbe::Reader readCapacity;
    std::function<void()>openSettings;
    modules::StorageAppearance appearance;modules::StorageSourcePaths paths;
    double rasterDensity{2};bool reduceMotion{};
    native::StorageDetailsProbe::Scanner scanDetails;
    native::StorageDetailsProbe::Clock completionClock;
    // Volume choice (Windows addition, user requested). Worker readers on
    // `utility`: the volumes the menu lists, and the chosen volume's capacity
    // by id (empty id, or a volume that is absent now: the startup volume).
    // With both set the chooser replaces readCapacity. The production pair
    // (makeStoragePreviewOptions) is used only together with the production
    // readCapacity: an owner that injects another readCapacity (synthetic
    // coverage) and keeps them gets no chooser, so no real volume is read.
    std::function<std::vector<modules::StorageVolume>()>listVolumes;
    std::function<std::optional<modules::StorageCapacity>(const std::string&volumeID)>readVolumeCapacity;
    // The remembered choice, on the UI thread: read at construction and on
    // each activation (the settings record may load later or be restored to
    // defaults) and written on every choice (the host keeps it in the
    // settings record as "storageVolume"). Without them the choice lasts for
    // the session.
    std::function<std::string()>loadVolume;
    std::function<void(const std::string&)>saveVolume;
};
// Shared-shell adapter only: no window, scan, private executor, clock or
// publication point. The app's utility executor and rasterizer outlive it.
// Settings action is explicit and injected. Owner aggregates nextWakeTime into
// its existing deadline and calls utilityCompleted after the shared drain.
// What the Storage owner keeps resident: its retained Mac artwork and the
// Windows chooser/menu overlays (surfaces and raster bytes now) and how often
// a close parked them.
struct StorageResourceStats {std::size_t surfaces{},pixelBytes{};std::uint64_t parks{};bool loaded{};};
class StoragePreview final {
public:
    // The host publishes its built-in Storage page under this key for module
    // areas (ApplicationHost::find), e.g. the hidden coverage memory checks.
    static constexpr std::string_view serviceKey="storage";
    StoragePreview(native::LayerRasterizer&,StoragePreviewOptions);
    ~StoragePreview();
    void resize(const app::ClientMetrics&);
    // HUD visibility. Hiding while Storage is not the shown module releases
    // its artwork at once (park); the module shown at the close keeps it, so
    // reopening on it only re-uploads. The artwork is first rasterized when
    // Storage is shown, never at construction or on an appearance event while
    // it is not shown.
    void setVisible(bool,double time);void setReduceMotion(bool,double time);
    // The HUD finished closing (HostEvent::overlayConcealed): a Storage page
    // that is not shown keeps no raster artwork. Never throws.
    void conceal()noexcept;
    StorageResourceStats resources()const noexcept;
    bool setAppearance(modules::StorageAppearance,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool utilityCompleted(double time);bool deadline(double time);
    // Explicit model operation only: original capacity canvas has no details
    // control, and neither activation nor its60s deadline calls this method.
    bool requestDetails(bool refresh,double time);
    std::optional<double>nextWakeTime()const noexcept;
    bool requiresFrames(double time)const;
    bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);
    bool pointerLocked()const noexcept;void cancelInteraction(double time);
    bool perform(std::string_view action,double time); // source AX action route; also storage:volumes, storage:volume:<n>
    // Volume choice state (Windows addition).
    std::span<const modules::StorageVolume>volumes()const noexcept;
    const std::string&chosenVolume()const noexcept;      // the remembered id (may be absent now)
    bool volumeMenuOpen()const noexcept;
    bool volumeChooserAvailable()const noexcept;           // two or more volumes listed
    const modules::StorageController&state()const noexcept;
    const modules::StoragePresentation&presentation()const noexcept;
    native::StorageCapacityProbe::Stats probeStats()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
