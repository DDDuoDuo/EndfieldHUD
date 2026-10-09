#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/activity_scene.hpp"
#include "native/layer_scene.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
namespace endfield::tools {
struct ActivityPreviewOptions {
    modules::ActivityNameCompare compareNames;
    modules::ActivitySnapshot initial;modules::ActivityAppsSnapshot apps;
    modules::ActivityAppearance appearance;native::LayerRasterOptions raster;
    std::shared_ptr<const core::SubsectionMaskSampler>sortSamples;bool reduceMotion{};
    // Exact visibility demand only. A shared provider owner applies this to
    // ActivitySamplingPlan along with charge-alert demand and its one deadline.
    // No native reader, worker, polling, app enumeration or icon lookup here.
    std::function<void(bool visible,bool apps,double ownerTime)>demandChanged;
};
class ActivityPreview final {
public:
    ActivityPreview(native::LayerRasterizer&,ActivityPreviewOptions);~ActivityPreview();
    void resize(const app::ClientMetrics&);void receive(modules::ActivitySnapshot);void receiveApps(modules::ActivityAppsSnapshot);
    void setAppearance(modules::ActivityAppearance);void setLanguage(core::Language);void setIcons(native::ActivityIcons);
    void setReduceMotion(bool);void setOverlayVisible(bool,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);bool key(const app::KeyEvent&,double);
    bool perform(std::string_view,double);void cancelInteraction();bool pointerLocked()const noexcept;
    const modules::ActivityState&state()const noexcept;
    // Optional shared ActivityProbe binding. Probe lifetime must end before
    // this owner; delivery remains on the existing UI executor drain only.
    modules::ActivityState&providerState()noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);native::ActivitySceneStats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
