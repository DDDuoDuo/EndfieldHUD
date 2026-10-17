#pragma once
#include "app/overlay_host.hpp"
#include "native/settings_scene.hpp"
#include "modules/settings_safety.hpp"
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
 // UI Automation (tools/settings_automation.hpp). inputModule: the page that
 // accepts input now (shown, requested, its module switch settled, the HUD
 // open); none otherwise. clientRect: one of that page's canvas rects (the
 // 400 x 334 page) through its current plane, the bounds of the projected
 // corners in logical client points (DPI scale removed); none while no page
 // accepts input. perform/setSlider: an accessible action or slider of that
 // page exactly as its click or drag (SettingsPresentation::perform /
 // setSlider, which take only listed enabled actions and visible sliders);
 // refused while the confirmation card is up or no page accepts input.
 std::optional<core::Module>inputModule()const noexcept;
 std::optional<core::Rect>clientRect(core::Rect canvas)const;
 const app::ClientMetrics&clientMetrics()const noexcept;
 bool perform(std::string_view action,double);bool setSlider(std::string_view slider,double value,double);
 // The layout confirmation card (HUDScaleSafetyView) while it is modal:
 // safety() its state (strings, keyboard focus, pose); safetyViewport() the
 // logical client viewport its geometry is laid out in; safetyClientRect()
 // a card-local rect through the posed card; performSafety(0 Revert, 1 Keep)
 // presses a button exactly as a click (down and up on it); focusSafety()
 // moves the card's keyboard focus exactly as Left/Right do.
 const modules::SettingsSafety&safety()const noexcept;core::Rect safetyViewport()const noexcept;
 std::optional<core::Rect>safetyClientRect(core::Rect cardLocal)const;
 bool performSafety(int button,double);bool focusSafety(int button,double);
 // Changes whenever what the accessibility view lists may have changed (the
 // page accepting input, its presentation, the confirmation card). Cheap;
 // no allocation.
 std::uint64_t accessibilityRevision()const noexcept;
 void setCustomColor(std::array<double,3>,double);void setCustomLogo(std::string,double);void showImportError(std::string,double);
 // Display > Battery alert (Mac HUDSettingsCanvas.showBatterySettings), for
 // the Power page's "Battery alert ›" before Display is selected.
 void showBatterySettings(double);
 // Rebuilds every page's rows (host hooks changed, e.g. a module area that
 // contributes a System row was constructed after Settings).
 void refreshViews(double);
 // Legacy combined order remains the default. The shared shell uses
 // entries(false) below pinned Notes, then modalEntries() above every module.
 bool modalActive()const noexcept;
 void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries(bool includeModal=true);
 std::span<const native::LayerCompositionEntry>modalEntries();void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
