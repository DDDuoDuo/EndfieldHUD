#pragma once
#include "app/overlay_host.hpp"
#include "native/settings_scene.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
namespace endfield::tools {
#ifdef _WIN32
struct SettingsPreviewOptions {
 ehud::data::Settings initial=ehud::data::Settings::defaults();modules::SettingsCallbacks callbacks;
 modules::SettingsViewHooks viewHooks;std::vector<modules::SettingsIcon>icons;modules::SettingsAbout about;
 native::SettingsImages images;native::LayerRasterOptions raster;modules::SettingsAppearance appearance;
 core::Language language{core::Language::english};
};
// Shared renderer/composition owner for the four original Settings modules.
// All OS/IO work is injected. The host owns one deadline and transactional
// RegisterHotKey/launch-at-login operations. Callback lifetimes may not reenter
// or destroy this owner. No service, window, TSF instance or timer is created.
class SettingsPreview final {
public:
 SettingsPreview(native::LayerRasterizer&,SettingsPreviewOptions);~SettingsPreview();
 void resize(const app::ClientMetrics&);void setAppearance(modules::SettingsAppearance);void setLanguage(core::Language);
 void setReduceMotion(bool,double);void setOverlayVisible(bool,double);void wake(double);
 std::optional<double>nextWakeTime(double)const;
 void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,const core::ModulePresentationSample&,float,double);
 bool requiresFrames(double)const;bool covers(core::Point)const;
 bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);bool key(const app::KeyEvent&,double);
 // Explicit modifiers for hidden fixture and the native host's key snapshot.
 bool key(const app::KeyEvent&,std::uint32_t nativeModifiers,double);
 void cancelInteraction(double);bool pointerLocked()const noexcept;
 modules::SettingsController&controller()noexcept;const modules::SettingsPresentation*view(core::Module)const noexcept;
 void setCustomColor(std::array<double,3>,double);void setCustomLogo(std::string,double);void showImportError(std::string,double);
 // Legacy combined order remains the default. The shared shell uses
 // entries(false) below pinned Notes, then modalEntries() above every module.
 bool modalActive()const noexcept;
 void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries(bool includeModal=true);
 std::span<const native::LayerCompositionEntry>modalEntries();void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
