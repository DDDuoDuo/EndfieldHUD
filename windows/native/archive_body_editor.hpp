#pragma once
// The native half of ArchiveBodyArea (modules/archive_body_area.hpp): the
// persistent projected rich editor of the Archive body (Mac
// ArchiveBodyTextView + HUDProjectedTextEditor, ArchiveCanvas.swift:1495-
// 1671), also usable by the Notes centre TEXT note.
//
// Unlike the design text fields (native/hud_design_text_field.hpp: one
// editor while a field edits, 64 Ki units, plain paste) the body's editor
// stays attached while its page shows, so the text is drawn by the editor
// also when another control has the focus, and it keeps the v1.2.0 Windows
// Archive body features: the single-layout NativeProjectedEditor for short
// bodies and the paragraph-chunked NativeNotesLongEditor past
// `chunkedUnits` (a paste that grows a short body promotes it, keeping its
// undo), the 2 MiB document, RTF / CF_HTML / private-runs paste and RTF copy
// through NotesClipboard, Ctrl+word navigation, double-click word and
// triple-click paragraph selection, Home/End on the visual line. Keys reach
// it before the canvas while it has the focus (TSF first: filterKeyMessage),
// presses inside its text rect place the caret, the wheel scrolls it.
// Ctrl+B / Ctrl+I / Ctrl+U report to the area (onFormatShortcut); Esc blurs.
//
// Pose: the owner passes the canvas's design pose every frame; the editor
// follows the body layer's presented world (its entrance and the page
// leave, modules::design::sampleLayer) and opacity. Composition: one entry,
// the editor's glyph scene, which the owner inserts into the canvas's
// scene after the body (or the media strip) part (NativeDesignScene::
// setInsertion), so menus and pop-overs draw over the text as on the Mac.
// Suspend (module switched away inside the open HUD): the focus ends, the
// editor, its layout and pixels stay (the v1.2.0 stand-by); dispose (another
// document, the list page, a relock, the HUD closing): the editor stops.
// Caret: accentInk, solid after every move, blinking like the design fields
// (modules::design::CaretBlink; LIB-87), only in the foreground HUD. Every
// pose reports the editor's scroll offset and overflow to the area, which
// draws the Mac's auto-hiding scroll indicator (ArchiveBodyArea::editorScrolled).
//
// Gap: the Mac paragraph style's lineHeightMultiple 1.3 has no counterpart
// in the projected editor's layout (A1 raster); the body keeps the v1.2.0
// natural line height plus one point between lines.
#include "modules/archive_body_area.hpp"
#include "modules/hud_design_text_field.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"
#include "native/notes_clipboard.hpp"
#include "native/notes_long_editor.hpp"
#include <functional>
#include <memory>

struct ITfThreadMgr;

namespace endfield::native {
struct NativeArchiveBodyEditorOptions {
    void* window{};                 // the HUD HWND
    ITfThreadMgr* threadManager{};  // Notes' activated manager (borrowed)
    std::uint32_t clientID{};
    std::uint32_t ownerMessage{};   // private change-notice message (WM_APP range)
    LayerRasterOptions raster;
    std::shared_ptr<NotesClipboard> clipboard; // null: no Ctrl+X / C / V
    // The right-click edit menu (Cut / Copy / Paste / Select All); tests inject
    // a fake; null: no menu.
    std::function<std::optional<modules::design::EditCommand>(core::Point client, bool canCut, bool canCopy, bool canPaste)> editMenu;
    std::uint32_t chunkedUnits{4096};
    NativeNotesLongEditorOptions longEditor;
};
// The owner's per-frame placement of the canvas (DesignOwnerCore::pose plus
// the composition camera).
struct NativeArchiveBodyPose {
    core::Matrix4 boxWorld, screenToClip;
    unsigned pixelWidth{}, pixelHeight{};
    float opacity{1};
    const modules::design::Layer* root{};   // the canvas root (box px)
    bool windowForeground{}, reduceMotion{};
    double time{};
    modules::design::Color text, caret, selection; // #EEEEEE ink (tone), accentInk, yellow .35
};
class NativeArchiveBodyEditor final : public modules::ArchiveBodyEditorHost {
public:
    NativeArchiveBodyEditor(LayerRasterizer&, NativeArchiveBodyEditorOptions);
    ~NativeArchiveBodyEditor() override;
    NativeArchiveBodyEditor(const NativeArchiveBodyEditor&) = delete;
    NativeArchiveBodyEditor& operator=(const NativeArchiveBodyEditor&) = delete;
    // ArchiveBodyEditorHost
    bool attach(modules::ArchiveBodyArea&) override;
    void suspend(modules::ArchiveBodyArea&) override;
    void dispose(modules::ArchiveBodyArea&) noexcept override;
    bool attached(const modules::ArchiveBodyArea&) const noexcept override;
    bool focused(const modules::ArchiveBodyArea&) const noexcept override;
    void focus(modules::ArchiveBodyArea&, core::text::Range) override;
    void blur(modules::ArchiveBodyArea&) override;
    bool hasMarkedText(const modules::ArchiveBodyArea&) const noexcept override;
    bool applyFormat(modules::ArchiveBodyArea&, const core::notes::FormatChange&, core::text::Range, bool keepSelection) override;
    bool replace(modules::ArchiveBodyArea&, core::text::Range, std::u16string_view) override;
    void scroll(modules::ArchiveBodyArea&, double) override;
    void layoutChanged(modules::ArchiveBodyArea&) override;
    // Owner routing (client points: physical for pointer and wheel, logical for the cursor).
    void setPose(const NativeArchiveBodyPose&);
    bool shown() const noexcept;                          // attached, its layer visible, posed
    bool filterKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);
    bool key(const modules::design::KeyInput&);
    bool character(std::uint32_t value, bool unicodeScalar);
    bool message(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);
    bool pointerDown(core::Point clientPhysical, core::Point clientLogical, bool extend, bool doubleClick);
    void pointerDrag(core::Point clientPhysical);
    void pointerUp();
    bool rightPointerDown(core::Point clientPhysical, core::Point clientLogical);
    bool wheel(core::Point clientPhysical, double deltaPoints);
    bool textCursorAt(core::Point clientLogical) const noexcept;
    bool contains(core::Point clientPhysical) const noexcept; // inside the projected text rect
    void setFocused(bool windowFocused);
    bool requiresFrames(double now) const;
    std::optional<double> nextWakeTime(double now) const;
    // The insertion entry (the glyph scene) while shown; nullopt otherwise.
    std::optional<LayerCompositionEntry> entry() const;
    bool collectRetired(Renderer&);
    bool releaseResources(Renderer&);
    std::size_t promotions() const noexcept;              // short bodies reopened in the chunked editor (tests)
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif
