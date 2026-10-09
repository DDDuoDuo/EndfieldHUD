#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/storage_scene.hpp"
#include "native/storage_probe.hpp"
#include "native/storage_details_probe.hpp"
#include "native/module_scene.hpp"
namespace endfield::tools {
struct StoragePreviewOptions {
    app::UtilityExecutor*utility{};native::StorageCapacityProbe::Reader readCapacity;
    std::function<void()>openSettings;
    modules::StorageAppearance appearance;modules::StorageSourcePaths paths;
    double rasterDensity{2};bool reduceMotion{};
    native::StorageDetailsProbe::Scanner scanDetails;
    native::StorageDetailsProbe::Clock completionClock;
};
// Shared-shell adapter only: no window, scan, private executor, clock or
// publication point. The app's utility executor and rasterizer outlive it.
// Settings action is explicit and injected. Owner aggregates nextWakeTime into
// its existing deadline and calls utilityCompleted after the shared drain.
class StoragePreview final {
public:
    StoragePreview(native::LayerRasterizer&,StoragePreviewOptions);
    ~StoragePreview();
    void resize(const app::ClientMetrics&);
    void setVisible(bool,double time);void setReduceMotion(bool,double time);
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
    bool perform(std::string_view action,double time); // source AX action route
    const modules::StorageController&state()const noexcept;
    const modules::StoragePresentation&presentation()const noexcept;
    native::StorageCapacityProbe::Stats probeStats()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
