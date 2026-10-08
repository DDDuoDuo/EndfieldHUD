#pragma once
#include "app/overlay_host.hpp"
#include "core/notes_rich_text.hpp"
#include "native/layer_group.hpp"
#include "native/notes_controls_scene.hpp"
#include <functional>
#ifdef _WIN32
namespace endfield::tools {
// Original retained Notes menus on the caller's workspace plane. Owns no HWND,
// clock, editor, persistence or renderer. Color artwork is an explicit pinned
// build asset; installed fonts are enumerated once, only on first font request.
class NotesFormatMenu final {
public:
    using Apply=std::function<bool(const core::notes::FormatChange&)>;
    NotesFormatMenu(native::LayerRasterizer&,std::filesystem::path assetRoot,Apply);
    ~NotesFormatMenu();
    bool open(std::string_view verb,const core::notes::TextStyle&,double time);
    void close(double time);
    bool acceptsInput()const noexcept;
    bool dragging()const noexcept;
    void update(const core::Matrix4&workspace,const core::Matrix4&camera,
        unsigned pixelWidth,unsigned pixelHeight,core::Rect workspaceBounds,
        core::Rect noteRect,float opacity,double time);
    bool contains(core::Point physicalPoint)const;
    bool pointer(const app::PointerEvent&,double scale,double time);
    bool wheel(const app::WheelEvent&,double scale,double time);
    bool key(const app::KeyEvent&,double time);
    bool requiresFrames(double time)const;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
