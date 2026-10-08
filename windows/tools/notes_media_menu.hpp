#pragma once
#include "app/overlay_host.hpp"
#include "native/notes_controls_scene.hpp"
#include "native/layer_group.hpp"
#include <functional>
#ifdef _WIN32
namespace endfield::tools {
struct NotesMediaAction {
    enum class Kind {chooseLocal,chooseShelf,useShelf};Kind kind;
    core::Point workspacePoint;std::string itemID;
};
// Source NotesMediaSourceChooser/NotesShelfMediaPicker on the existing Notes
// workspace. Metadata only: no file lookup, provider, dialog, HWND or timer.
class NotesMediaMenu final {
public:
    using Choose=std::function<void(NotesMediaAction)>;
    NotesMediaMenu(native::LayerRasterizer&,Choose);
    ~NotesMediaMenu();
    void openSource(core::Point,double time);
    void openShelf(std::vector<modules::NotesShelfChoice>,core::Point,double time);
    void close(double time,bool immediate=false);
    bool acceptsInput()const noexcept;
    void update(const core::Matrix4& moduleToScreen,const core::Matrix4& workspaceToScreen,
        const core::Matrix4& camera,unsigned pixelWidth,unsigned pixelHeight,float opacity,double time);
    bool contains(core::Point)const;
    bool pointer(const app::PointerEvent&,double scale,double time);
    bool wheel(const app::WheelEvent&,double scale,double time);
    bool key(const app::KeyEvent&,double time);
    bool requiresFrames(double time)const;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
