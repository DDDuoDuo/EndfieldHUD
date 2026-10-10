#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/hypergryph_account_canvas.hpp"
#include "native/hypergryph_account_canvas_scene.hpp"
namespace endfield::tools {
struct AccountPreviewOptions {
    native::LayerRasterOptions raster;
    modules::hypergryph::CanvasStyle style;
    core::Language language{core::Language::english};
    bool reduceMotion{};
};
// Account Linking module owner: the shared HypergryphAccountController is
// app-owned (also used by the header gauge) and outlives this owner. This
// owner maps controller presentation to the source canvas, routes canvas
// actions back, and derives controller visibility from the module selection.
// It owns no network, vault, browser, timer, worker or window. The host calls
// deadline() at nextWakeTime() on its existing scheduler.
class AccountPreview final {
public:
    AccountPreview(modules::hypergryph::HypergryphAccountController&,native::LayerRasterizer&,AccountPreviewOptions);
    ~AccountPreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language,double time);
    void setStyle(modules::hypergryph::CanvasStyle,double time);
    void setReduceMotion(bool) noexcept;
    // HUD presented/retracting (Mac: window != nil && interactionEnabled && !retracting).
    void setOverlayVisible(bool,double time);
    // Host forwards the controller's `changed` callback (presentation/cache).
    void presentationChanged(double time);
    void update(const core::Matrix4& sourceCenter,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time) const;
    bool covers(core::Point logicalClientPoint) const;
    bool capturesPointer() const noexcept;      // an open account menu consumes all module input
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,bool modified,double time);
    bool dismissMenu(double time);
    void cancelInteraction(double time);
    // Controller refresh/recovery deadlines in host-clock seconds.
    std::optional<double> nextWakeTime(double hostTime) const;
    bool deadline(double hostTime);
    const modules::hypergryph::AccountCanvasModel& canvas() const noexcept;
    native::NativeAccountSceneStats sceneStats() const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
