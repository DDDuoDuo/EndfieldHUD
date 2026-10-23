#pragma once
// The native half of the design text field: modules::design::TextEditorHost
// over NativeProjectedEditor (native/projected_editor.*, TSF through
// native/projected_text_input.*), the pattern of native/calendar_editor_field.*
// and native/profile_editor_field.*. Mac: Sources/HUDDesignTextField.swift
// (begin/end editing) and Sources/HUDProjectedTextEditor.swift (caret, edit
// shortcuts, right-click menu, auto-hiding indicator). LIB-84..91.
//
// One editor at a time per owner core (the Mac allows one editing field per
// canvas and the HUD shows one canvas). It owns a dedicated empty LayerScene
// for the editor's four retained surfaces (selection, glyphs, composition,
// caret) and borrows Notes' activated ITfThreadMgr (ApplicationModuleContext::
// textManager/textClient). Editor keys reach it through the owner's filterKey
// chain BEFORE TranslateMessage (TSF first), then key().
//
// Caret (LIB-87): shown only while the HUD window is the foreground window,
// the field is focused, the selection is empty and the field editable; blink
// = modules::design::CaretBlink sampled into the caret surface's opacity
// (needs ProjectedEditorPose caret opacity: LIBRARY-DESIGN.md R8); solid again
// after every caret move; solid under Reduce Motion; removed when hidden.
// requiresFrames only during the two 80 ms ramps; nextWakeTime otherwise.
// Edit shortcuts without a menu bar (LIB-89): Ctrl+X/C/V/A/Z, Ctrl+Y and
// Ctrl+Shift+Z; secure fields refuse cut/copy and keep no undo; nothing a
// secure field holds ever reaches the clipboard. Right-click (LIB-90): the
// click moves the caret only when outside the selection, then the injected
// menu offers Cut / Copy / Paste / Select All (no Undo). The scroll indicator
// of a multi-line field auto-hides (ScrollBarVisibility, LIB-91).
#include "modules/hud_design_text_field.hpp"
#ifdef _WIN32
#include "core/notes_rich_text.hpp"
#include "native/layer_scene.hpp"
#include <functional>
#include <memory>

struct ITfThreadMgr;

namespace endfield::native {
// Clipboard access the owner injects (text only, UTF-16).
struct DesignTextClipboard {
    std::function<std::optional<std::u16string>()> read;
    std::function<bool(std::u16string_view)> write;
};
// Shows the four-item edit menu at a logical client point; returns the
// chosen command (nullopt: dismissed). Production: nativeEditMenu below; tests
// inject a fake (they never open a window or a menu).
using DesignEditMenu = std::function<std::optional<modules::design::EditCommand>(core::Point client, bool canCut, bool canCopy, bool canPaste)>;
// The production menu (Q5, the Mac's NSMenu at the pointer): a native popup
// (TrackPopupMenuEx, TPM_RETURNCMD) owned by `window` with the catalog pairs
// Cut/剪切, Copy/复制, Paste/粘贴, Select All/全选 in the context language;
// disabled items gray out like NSMenu's validation. No Undo item (LIB-90).
DesignEditMenu nativeEditMenu(void* window, std::function<core::Language()> language);
struct NativeDesignTextFieldOptions {
    void* window{};                 // the HUD HWND
    ITfThreadMgr* threadManager{};  // Notes' activated manager (borrowed)
    std::uint32_t clientID{};
    std::uint32_t ownerMessage{};   // ApplicationHost::registerMessage id for editor change notices
    LayerRasterOptions raster;
    DesignTextClipboard clipboard;
    DesignEditMenu editMenu;
    // TextFieldOptions::rich (the Notes centre TEXT note): the owner's
    // document, edited in NativeProjectedEditor's rich mode. Null or absent:
    // a rich field does not attach (it stays a drawn label).
    std::function<core::notes::RichDocument*(const modules::design::TextField&)> richDocument;
};
struct NativeDesignTextFieldPose {
    core::Matrix4 boxToScreen, screenToClip; // the matrices the composition uses
    unsigned pixelWidth{}, pixelHeight{};
    float opacity{1};
    bool windowForeground{};               // the HUD window is the foreground window
    double time{};
    bool reduceMotion{};
};
class NativeDesignTextFields final : public modules::design::TextEditorHost {
public:
    NativeDesignTextFields(LayerRasterizer&, NativeDesignTextFieldOptions);
    ~NativeDesignTextFields() override;
    NativeDesignTextFields(const NativeDesignTextFields&) = delete;
    NativeDesignTextFields& operator=(const NativeDesignTextFields&) = delete;
    // TextEditorHost
    bool begin(const modules::design::TextEditRequest&) override;
    void end(modules::design::TextField&) override;
    std::string text(const modules::design::TextField&) const override;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const modules::design::TextField&) const override;
    bool hasMarkedText(const modules::design::TextField&) const override;
    void setText(modules::design::TextField&, std::string_view) override;
    void reproject(modules::design::TextField&, core::Rect frame) override;
    // Owner routing (logical client points; the owner converts to physical).
    bool filterKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);
    bool key(const modules::design::KeyInput&);   // shortcuts and editing keys after TSF
    bool character(std::uint32_t value, bool unicodeScalar);
    bool message(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam); // ownerMessage change notices
    bool pointerDown(core::Point clientPhysical, bool extend);
    void pointerDrag(core::Point clientPhysical);
    void pointerUp();
    bool rightPointerDown(core::Point clientPhysical, core::Point clientLogical);
    bool textCursorAt(core::Point clientLogical) const noexcept; // ModuleOwner::cursorAt (I-beam)
    // The wheel over an editing multi-line field scrolls its text (the Mac
    // text view forwards scrollWheel to its projection owner); delta in
    // points, positive down. True when the field took it.
    bool wheel(core::Point clientPhysical, double deltaPoints);
    // The HUD window's activation: TSF focus follows it (the caret follows
    // NativeDesignTextFieldPose::windowForeground).
    void setFocused(bool);
    bool editing() const noexcept;
    const modules::design::TextField* field() const noexcept;
    // The editing field as an accessibility item (the Mac's native text view
    // is the element while it edits; TextField lists nothing then): `edit`,
    // keyboard focused while the HUD has the focus, no value when secure.
    std::optional<modules::design::AccessibilityItem> accessibilityItem() const;
    void setPose(const NativeDesignTextFieldPose&);
    bool requiresFrames(double now) const;
    std::optional<double> nextWakeTime(double now) const;
    bool upload(Renderer&);
    std::span<const LayerCompositionEntry> entries() const noexcept;
    // Ended editors' scenes, after a publication without them.
    bool collectRetired(Renderer&);
    bool releaseResources(Renderer&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif
