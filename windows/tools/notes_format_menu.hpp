#pragma once
#include "app/overlay_host.hpp"
#include "core/notes_rich_text.hpp"
#include "native/layer_group.hpp"
#include "native/notes_controls_scene.hpp"
#include "native/notes_automation.hpp"
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
    // Caller-localized source captions (close, trait names, current color):
    // the menus' accessibility titles. Applies to open and later menus.
    void setStrings(const modules::NotesControlsStrings&);
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
    // UI Automation peers of the open menu (native/notes_automation.hpp):
    // each NotesRetainedMenu item as a button "<prefix><item id>" labelled by
    // its accessibility title (enabled/selected as painted) and, in the color
    // menu, NotesColorWheelView as a slider "<prefix>wheel" whose value is the
    // marker's x on the 166-point wheel in steps of 4 (the source increment
    // and decrement). Physical client bounds; nothing before the first pose.
    // The font/size scroll bar (when the values exceed seven rows) is a
    // slider "<prefix>scroll": its value is the first shown row (0 ... count -
    // 7), small change one row, large change the seven shown rows.
    struct AutomationStrings {std::string wheel{"Color wheel"},wheelHelp{"Use arrow keys to adjust hue and saturation."},scroll{"Scroll"};};
    void automationElements(std::vector<native::NotesAutomationElement>&,std::string_view prefix,const AutomationStrings&)const;
    // NotesRetainedMenu.perform(id) for an enabled item of the open menu.
    bool perform(std::string_view item,double time);
    // The color wheel at x (0...166) on the marker's current row.
    bool setWheel(double x,double time);
    // The font/size menu's first shown row (the scroll slider's value); the
    // scroll bar's first row while one is shown, else none.
    bool setScroll(double row,double time);
    std::optional<std::size_t> firstRow()const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
