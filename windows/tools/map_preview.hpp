#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/map_scene.hpp"
#include "native/layer_scene.hpp"
#include "core/localization.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"

namespace endfield::tools {
struct MapPreviewOptions {
    modules::MapSnapshot initial;
    modules::MapPersistence persistence;
    std::shared_ptr<const modules::MapGeography> geography;
    native::MapPaintFunction paint;
    std::function<void()> releaseWorkerCaches;
    native::NativeMapRaster::Clock clock;
    std::function<void()> changed;
    std::function<void(modules::MapPinStyle)> pinStyleChanged;
    std::function<void()> recentered;
    native::LayerRasterOptions raster;
    native::MapPlayerImages player;
    native::MapAppearance appearance;
};
// Retained source Map canvas on the shared440-point module plane. Geography
// decoding, persistence and worker painter are injected; this owner performs
// no file IO and creates no thread, timer, window, renderer or publication.
// Executor/rasterizer outlive it. Caller detaches entries then calls release
// before destroying the shared Renderer. Utility completion only invalidates;
// call utilityCompleted after the app's drain, deadline at nextWakeTime, and
// update/upload/entries through the existing one HUD composition.
class MapPreview final {
public:
    MapPreview(native::LayerRasterizer&,app::UtilityExecutor&,MapPreviewOptions);
    ~MapPreview();
    void resize(const app::ClientMetrics&);
    void setGeography(std::shared_ptr<const modules::MapGeography>,double time);
    bool setAppearance(native::MapAppearance,double time);
    void setLanguage(core::Language,double time);
    void setOverlayVisible(bool,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool utilityCompleted(double time);bool deadline(double time);
    std::optional<double>nextWakeTime()const noexcept;
    bool requiresFrames(double time)const;bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);
    // Win32 detents keep fractional steps and source discrete sensitivity .12;
    // a future precise/pinch route may use scroll/magnify without a new timer.
    bool wheel(const app::WheelEvent&,double time);
    bool scroll(core::Point logicalClientPoint,double delta,bool precise,bool ended,double time);
    bool magnify(core::Point logicalClientPoint,double amount,bool ended,double time);
    bool key(const app::KeyEvent&,double time);
    bool perform(modules::MapAction,std::string_view pinID,double time);
    void cancelInteraction(double time);bool pointerLocked()const noexcept;
    const modules::MapState&state()const noexcept;
    native::NativeMapRaster::Stats rasterStats()const noexcept;
    native::MapSceneStats sceneStats()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
