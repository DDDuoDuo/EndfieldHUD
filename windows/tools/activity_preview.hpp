#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/activity_scene.hpp"
#include "native/activity_icons.hpp"
#include "app/utility_executor.hpp"
#include "native/layer_scene.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
namespace endfield::tools {
struct ActivityPreviewOptions {
    modules::ActivityNameCompare compareNames;
    modules::ActivitySnapshot initial;modules::ActivityAppsSnapshot apps;
    modules::ActivityAppearance appearance;native::LayerRasterOptions raster;
    // Ignored: the sort reveal is native::activitySortShutter (kept so
    // existing aggregate initialisers and assignments compile).
    std::shared_ptr<const core::SubsectionMaskSampler>sortSamples;bool reduceMotion{};
    // Exact visibility demand only. A shared provider owner applies this to
    // ActivitySamplingPlan along with charge-alert demand and its one deadline.
    // No native reader, worker, polling or app enumeration here. When set (the
    // native provider), the same visibility also gates the GPU engine
    // counters (native::setWindowsActivityGPUDemand).
    std::function<void(bool visible,bool apps,double ownerTime)>demandChanged;
    // Optional app-row icons (production): the visible Apps rows' icons load
    // on this shared executor with iconLoader (default: the Windows loader),
    // bounded by native::ActivityIconProvider and bound on content events only.
    // The executor must outlive this owner. Without it no icon is requested.
    app::UtilityExecutor*iconExecutor{};
    native::ActivityIconLoader iconLoader;
};
class ActivityPreview final {
public:
    ActivityPreview(native::LayerRasterizer&,ActivityPreviewOptions);~ActivityPreview();
    void resize(const app::ClientMetrics&);void receive(modules::ActivitySnapshot);void receiveApps(modules::ActivityAppsSnapshot);
    void setAppearance(modules::ActivityAppearance);void setLanguage(core::Language);void setIcons(native::ActivityIcons);
    void setReduceMotion(bool);void setOverlayVisible(bool,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    // Source HUDTelemetryInteraction.keyDown takes an unmodified Escape only
    // (command/control/option/shift). The first form is for the HUD window's
    // key dispatch: it reads GetKeyState, which Win32 synchronizes with the
    // key message being dispatched. Any other caller (tests, synthetic input)
    // passes the modifier state explicitly, as ReaderPreview::key does: off
    // that dispatch a thread's GetKeyState follows the live keyboard of the
    // interactive session.
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    bool perform(std::string_view,double);void cancelInteraction();bool pointerLocked()const noexcept;
    const modules::ActivityState&state()const noexcept;
    // Optional shared ActivityProbe binding. Probe lifetime must end before
    // this owner; delivery remains on the existing UI executor drain only.
    modules::ActivityState&providerState()noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);native::ActivitySceneStats stats()const;
    std::optional<native::ActivityIconStats>iconStats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
