#pragma once
#include "app/overlay_host.hpp"
#include "native/work_mode_scene.hpp"
#include "native/projected_editor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#ifdef _WIN32
namespace endfield::tools {
struct WorkModePreviewOptions {
    native::LayerRasterOptions raster;
    modules::WorkModeAppearance appearance;
    modules::WorkModeStrings strings;
    double restoredWorkSeconds{};
    std::function<void(double)> saveWorkSeconds; // absolute checkpoint; owner schedules persistence
    std::function<void(bool)> focusDesired; // active includes pause; no implied platform support
    std::function<void()> requestFocusAccess;
    UINT textMessage{WM_APP+208};
};
// Shared module owner, driven by the shell's continuous QPC time and deadline.
// Borrows HWND, activated manager/client and rasterizer. It never creates a
// window, renderer, clock, timer, thread, Focus service, clipboard or store.
// Callbacks must not synchronously destroy/reenter this owner. All fields must
// be destroyed before the caller deactivates the one shared TSF manager.
// Custom duration uses the existing <=65536-unit plain editor leaf, source
// inset/finite horizontal growth and one painted layout for glyphs/input/IME.
// Platform font substitution remains explicit; no data truncation is performed.
// Only single-line values parse.
class WorkModePreview final {
public:
    WorkModePreview(HWND,native::LayerRasterizer&,ITfThreadMgr* borrowedManager,
        TfClientId,WorkModePreviewOptions={});
    ~WorkModePreview();
    void resize(const app::ClientMetrics&);
    void setAppearance(modules::WorkModeAppearance);void setReduceMotion(bool,double);
    void setFocusStatus(std::string,bool permissionAction,double);
    // HUD visibility is separate from OS suspension. False cancels the draft
    // and visual demand but leaves a running countdown completion deadline.
    // Last artwork remains available for the caller's finite closing fade.
    void setOverlayVisible(bool,double); // initially true
    void setSuspended(bool,double);void wake(double);void shutdown(double);
    std::optional<double>nextWakeTime()const noexcept;
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double continuousTime);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);bool key(const app::KeyEvent&,double);
    bool key(const app::KeyEvent&,bool modified,double);
    bool filterKey(const app::NativeMessage&);bool message(const app::NativeMessage&,double);
    void focus(bool,double);void cancelInteraction(double);bool pointerLocked()const noexcept;
    bool editing()const noexcept;bool finishEditing(bool commit,double);
    const modules::WorkModeController&controller()const noexcept;
    const modules::WorkModePresentation&state()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
