#pragma once
#include "core/localization.hpp"
#include "app/overlay_host.hpp"
#include "native/event_log_scene.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"

namespace endfield::tools {
#ifdef _WIN32
// Injected bounded metadata and clear are the only service connection. The
// caller supplies local timestamps and coalesced refresh notifications while
// active; no recorder, file, service, HWND, clock or publisher is created here.
class EventLogPreview final {
public:
    // The host publishes its built-in Event Log page under this key for
    // module areas (ApplicationHost::find), e.g. the hidden coverage memory checks.
    static constexpr std::string_view serviceKey="eventLog";
    EventLogPreview(native::LayerRasterizer&,native::LayerRasterOptions,modules::EventLogCallbacks,
        modules::EventLogStrings={},modules::EventLogAppearance={},modules::EventNameCompactor={});
    ~EventLogPreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language); // preference event; retains the model and current interaction
    void refresh();void setAppearance(modules::EventLogAppearance);void setReduceMotion(bool);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    // Source HUDEventLogInteraction.keyDown takes unmodified keys only
    // (command/control/option/shift). The first form is for the HUD window's
    // key dispatch: it reads GetKeyState, which Win32 synchronizes with the
    // key message being dispatched. Any other caller (tests, synthetic input)
    // passes the modifier state explicitly, as ReaderPreview::key does: off
    // that dispatch a thread's GetKeyState follows the live keyboard of the
    // interactive session.
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    void cancelInteraction();bool pointerLocked()const;
    // HUD visibility (ModuleOwner setOverlayVisible / overlayClosing). A close
    // while Event Log is not the shown module releases its artwork at once
    // (park); the module shown at the close keeps it, so reopening on it only
    // re-uploads. A switch inside the open HUD never releases it.
    void setOverlayVisible(bool,double time);
    // The HUD finished closing (HostEvent::overlayConcealed): an Event Log
    // that is not shown keeps no raster artwork. Never throws.
    void conceal()noexcept;
    const modules::EventLogState&state()const;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
    native::EventLogSceneStats stats()const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
