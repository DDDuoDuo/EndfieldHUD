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
    // The host publishes its built-in Clipboard under this key for module
    // areas (ApplicationHost::find), e.g. the hidden coverage memory checks.
    static constexpr std::string_view serviceKey="clipboard";
    ClipboardPreview(native::LayerRasterizer&,native::LayerRasterOptions,native::ClipboardActions,
        native::ClipboardStrings={},native::ClipboardAppearance={},native::ClipboardImages={},
        // Ignored (the rows reveal is native::clipboardRevealShutter); kept so
        // existing callers compile.
        std::shared_ptr<const core::SubsectionMaskSampler> unusedRevealSamples={});
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
    // HUD visibility (ModuleOwner setOverlayVisible / overlayClosing). A close
    // while Clipboard is not the shown module releases its artwork and the
    // provider's cached row thumbnails at once (park); the module shown at
    // the close keeps them, so reopening on it only re-uploads. A switch
    // inside the open HUD never releases them.
    void setOverlayVisible(bool,double time);
    // The HUD finished closing (HostEvent::overlayConcealed): a Clipboard that
    // is not shown keeps no raster artwork. Never throws.
    void conceal()noexcept;
    const native::ClipboardState&state()const;
    // UI Automation (tools/clipboard_automation.hpp): a canvas rect -> the
    // logical client rect of its projected corners (none while the Clipboard
    // does not accept input), the viewport metrics, the HUD language
    // (setLanguage), and a listed action (ClipboardState::actions) performed
    // like its click (canvas.perform(actionID:)); false for an id that is
    // not listed now.
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    const app::ClientMetrics&clientMetrics()const noexcept;
    core::Language language()const noexcept;
    bool perform(std::string_view actionID,double time);
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
    native::ClipboardSceneStats stats()const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
