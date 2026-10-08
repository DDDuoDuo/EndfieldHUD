#pragma once
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
    EventLogPreview(native::LayerRasterizer&,native::LayerRasterOptions,modules::EventLogCallbacks,
        modules::EventLogStrings={},modules::EventLogAppearance={},modules::EventNameCompactor={});
    ~EventLogPreview();
    void resize(const app::ClientMetrics&);
    void refresh();void setAppearance(modules::EventLogAppearance);void setReduceMotion(bool);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);bool key(const app::KeyEvent&,double);
    void cancelInteraction();bool pointerLocked()const;
    const modules::EventLogState&state()const;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
    native::EventLogSceneStats stats()const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
