#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/orbipom_scene.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
namespace endfield::tools {
struct OrbiPomPreviewOptions {native::LayerRasterOptions raster;modules::OrbiPomAppearance appearance;};
// Session is app-owned and outlives this owner. It supplies the single lazy
// original JS VM and injected best-score persistence. Only update() advances
// it, with the shared owner frame clock while visible/active/foreground.
class OrbiPomPreview final {
public:
    OrbiPomPreview(modules::OrbiPomSession&,native::LayerRasterizer&,OrbiPomPreviewOptions);~OrbiPomPreview();
    void resize(const app::ClientMetrics&);void setAppearance(modules::OrbiPomAppearance,double time);void setLanguage(core::Language,double time);
    void setOverlayVisible(bool,double time);void setForeground(bool,double time);
    // Mac OrbiPomCanvas observes NSWorkspace willSleep/didWake: sleep clears
    // its foreground flag and wake restores the app's current activation.
    // Forward WM_POWERBROADCAST PBT_APMSUSPEND as true and PBT_APMRESUME* as
    // false; setForeground keeps tracking WM_ACTIVATEAPP meanwhile. Suspension
    // pauses the original VM and either edge discards the elapsed baseline, so
    // time spent asleep never reaches the game's virtual timers or countdown.
    void setSystemSuspended(bool,double time);
    // Mac reconcileClock runs a 1/30 s timer in low-power visual mode (1/60 s
    // otherwise). Forward ModuleAppearance::lowPower here; frames presented
    // for other reasons then add no simulation tick between 1/30 s ticks.
    void setLowPowerVisualMode(bool,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;bool covers(core::Point)const;
    bool pointer(const app::PointerEvent&,double time);bool wheel(const app::WheelEvent&,double time);bool key(const app::KeyEvent&,double time);
    bool perform(modules::OrbiPomAction,double time);void cancelInteraction(double time);
    bool pointerLocked()const noexcept{return false;} // Aiming/buttons never freeze the source HUD gyro.
    const modules::OrbiPomState&state()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();void collected(native::Renderer&);void release(native::Renderer&);
    native::OrbiPomSceneStats stats()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
