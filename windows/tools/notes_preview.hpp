#pragma once
#include "app/overlay_host.hpp"
#include "native/notes_controls_assets.hpp"
#include "native/notes_workspace.hpp"
#include "core/source_desktop_chrome.hpp"

namespace endfield::tools {
// Build-only integration owner. Requires an explicit NEW temporary data root;
// never opens the installed application's data or any account/service.
// Its scenes enter the caller's one composition and existing frame clock.
class NotesPreview final {
public:
    NotesPreview(HWND,native::LayerRasterizer&,const std::filesystem::path& newDataRoot,
        const native::NativeNotesControlsAssets&,bool activateTextServices);
    ~NotesPreview();
    void select(core::Module,double time);
    core::Module selected()const noexcept;
    void resize(const app::ClientMetrics&);
    void update(const core::Matrix4& sourceCenter,const core::source::DesktopChromeSettings&,
        float opacity,double time,bool focused);
    bool requiresFrames(double time)const;
    bool pointerLocked()const;
    bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);
    bool key(const app::KeyEvent&,double time);
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&);
    void focus(bool);
    bool finish(); // false: active TSF lock; owner retries after its queued message
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&); // only after shared composition is detached
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
