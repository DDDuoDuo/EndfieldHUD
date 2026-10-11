#pragma once
#include "core/localization.hpp"
#include "app/overlay_host.hpp"
#include "native/clipboard_scene.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"

namespace endfield::tools {
#ifdef _WIN32
// Injected history actions are the only connection to clipboard data. This
// owner never constructs SystemServices or calls native clipboard APIs. The
// host delivers one refresh() after a coalesced service notification, and owns
// the shared renderer, module selection, HWND, frame clock and composition.
class ClipboardPreview final {
public:
    ClipboardPreview(native::LayerRasterizer&,native::LayerRasterOptions,native::ClipboardActions,
        native::ClipboardStrings={},native::ClipboardAppearance={},native::ClipboardImages={},
        std::shared_ptr<const core::SubsectionMaskSampler> revealSamples={});
    ~ClipboardPreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language); // preference event; retains the model and current interaction
    void refresh();void setAppearance(native::ClipboardAppearance,native::ClipboardImages);void setReduceMotion(bool);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    // Source HUDClipboardInteraction.keyDown takes unmodified keys only
    // (command/control/option/shift). The first form is for the HUD window's
    // key dispatch: it reads GetKeyState, which Win32 synchronizes with the
    // key message being dispatched. Any other caller (tests, synthetic input)
    // passes the modifier state explicitly, as ReaderPreview::key does: off
    // that dispatch a thread's GetKeyState follows the live keyboard of the
    // interactive session.
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    void cancelInteraction();bool pointerLocked()const;
    const native::ClipboardState&state()const;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
    native::ClipboardSceneStats stats()const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
