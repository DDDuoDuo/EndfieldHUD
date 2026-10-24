#pragma once
// ArchiveBodyArea (Mac Sources/ArchiveCanvas.swift:1495-1671): the document
// body of Archive, also the Notes centre TEXT note's editor on the Mac
// (NotesCanvas.swift:534-591). Spec section 3.9.3 textarea: #1C1C1C, a 1 px
// ringField border, clipped, text inset 14 x 12, the "Start writing"
// placeholder (14 Regular muted, line height 14 x 1.3, hidden when there is
// text), caret accentInk, selection yellow at .35, paragraphs at a 1.3 line
// height multiple.
//
// Unlike design::TextField the editor stays ATTACHED while the page shows: a
// click places the caret directly and per-run formatting, IME, undo and the
// screen reader stay native. The area owns the document (the NSTextView's
// text storage); the native host (native/archive_body_editor.hpp) projects
// one persistent editor over it, takes the presses inside the text rect,
// its keys (TSF first), the wheel and the I-beam, and reports focus,
// selection and edits back here. Suspending (the module is switched away)
// ends the focus and keeps the text, selection and undo; disposing (the page
// changes document, the HUD closes, a classified document relocks) drops it.
//
// Press forwarding (pressTarget :1576-1586, NotesPressForwarder): while the
// body is focused, a press on the owner's format bar (formatsSelection) or
// any press while a menu or pop-over is open must not end editing; the
// canvas leaves the focus alone for those (ArchiveCanvas::interceptPointerDown).
#include "core/notes_rich_text.hpp"
#include "modules/hud_design_control.hpp"
#include "modules/hud_design_scroll.hpp"
#include <memory>

namespace endfield::modules {
class ArchiveBodyArea;
// The persistent editor's native half (HUDProjectedTextEditor + the text view).
class ArchiveBodyEditorHost {
public:
    virtual ~ArchiveBodyEditorHost() = default;
    // Projects the editor over the area's text rect (box px through the
    // canvas root); a repeated call keeps the attached editor. False: no
    // editor available (the area then shows its drawn placeholder only).
    virtual bool attach(ArchiveBodyArea&) = 0;
    virtual void suspend(ArchiveBodyArea&) = 0;         // ends focus; text, selection, undo stay
    virtual void dispose(ArchiveBodyArea&) noexcept = 0;
    virtual bool attached(const ArchiveBodyArea&) const noexcept = 0;
    virtual bool focused(const ArchiveBodyArea&) const noexcept = 0;
    // makeFirstResponder + setSelectedRange + scrollRangeToVisible (:1642-1650).
    virtual void focus(ArchiveBodyArea&, core::text::Range selection) = 0;
    virtual void blur(ArchiveBodyArea&) = 0;
    virtual bool hasMarkedText(const ArchiveBodyArea&) const noexcept = 0;
    // NSTextView.setAttributesUndoably over `range` + the typing attributes
    // (ArchiveRichText.apply :1396-1428): one undo step; the selection is
    // kept when `keepSelection`, else restored. False: declined (marked
    // text) or nothing changed.
    virtual bool applyFormat(ArchiveBodyArea&, const core::notes::FormatChange&, core::text::Range, bool keepSelection) = 0;
    // An owner edit through the editor (the 2 MiB clamp): one undo group.
    virtual bool replace(ArchiveBodyArea&, core::text::Range, std::u16string_view) = 0;
    virtual void scroll(ArchiveBodyArea&, double delta) = 0;  // box px, + down
    virtual void layoutChanged(ArchiveBodyArea&) = 0;        // size or place changed: reproject
};

class ArchiveBodyArea final : public design::Control {
public:
    using Point = design::Point;
    using Rect = design::Rect;
    static constexpr design::Size inset{14, 12};
    static constexpr double placeholderSize = 14;
    // `capacity`: the document's UTF-16 capacity (Archive: 2 MiB, the body's
    // byte limit is applied by the owner's clamp).
    ArchiveBodyArea(design::Builder&, std::string id, std::string label, std::string placeholder, std::uint32_t capacity);
    ~ArchiveBodyArea() override;
    core::notes::RichDocument& document() noexcept { return *document_; }
    const core::notes::RichDocument& document() const noexcept { return *document_; }
    // load(_:) (:1590-1595): the text with its covering runs, the typing
    // style, the caret at 0; a new document (no undo history).
    void load(std::u16string text, core::notes::RichText runs, const core::notes::TextStyle& typing);
    void setSize(design::Size);                            // keeps focus and the attached editor
    void setFramed(bool);                                  // false: no fill, no ring (the Notes centre note)
    Rect textRect() const noexcept;                        // layer coordinates: bounds inset 14 x 12
    void setHost(ArchiveBodyEditorHost* host) noexcept { host_ = host; }
    ArchiveBodyEditorHost* host() const noexcept { return host_; }
    bool attach();
    void suspend();                                        // blur, the editor stays (stand-by)
    void dispose() noexcept;
    bool isAttached() const noexcept { return host_ && host_->attached(*this); }
    bool isFocused() const noexcept { return host_ && host_->focused(*this); }
    bool hasMarkedText() const noexcept { return host_ && host_->hasMarkedText(*this); }
    // focus(selection:) (:1642-1650): nullopt = the caret at the end.
    void focus(std::optional<core::text::Range> selection);
    void blur();
    void scroll(double delta);
    bool applyFormat(const core::notes::FormatChange&, core::text::Range, bool keepSelection);
    bool replace(core::text::Range, std::u16string_view);
    // The placeholder follows the text (textChanged :1637-1640).
    void textChanged();
    // The owner's format bar: a press on its formatting controls keeps the
    // editing (pressTarget's formatsSelection, root coordinates).
    std::function<bool(Point root)> formatsSelection;
    // Reports (the text view delegate and onFocusChange, delivered by the host).
    std::function<void(bool focused)> onFocusChange;
    std::function<void()> onChange, onSelectionChange, onEndEditing;
    // Ctrl+B / Ctrl+I / Ctrl+U in the focused body (the Mac's Cmd shortcuts,
    // NotesFormatChange(shortcut:) :19-27): the host reports them here and the
    // owner applies them like the bar's toggles (onFormatShortcut :1573).
    std::function<void(core::notes::FormatKind)> onFormatShortcut;
    // Escape in the body (doCommandBy cancelOperation :681): the host blurs
    // and reports it as an end of editing.
    void editorFocusChanged(bool focused);
    // The text view's scroll (offset and overflow = content - viewport, box
    // px), reported by the host whenever either may have changed: places the
    // auto-hiding indicator (HUDProjectedTextEditor.swift:16-18, :180-183:
    // grey .65, 3 px at the text rect's right edge - 4, max(16, h^2 /
    // content) long, hidden without overflow) and shows it while the offset
    // moves (HUDScrollBarVisibility). The library's fixed stretched thumb
    // raster draws it (Windows gap: square ends, the Mac's 1.5 px radius).
    void editorScrolled(double offset, double overflow);
    void editorChanged();
    void editorSelectionChanged();
    void editorEndedEditing();
    // A press on the padding focuses the text at its end (activate :1668).
    void activate() override;
    // The native editor is the element on the Mac; Windows lists the body as
    // an edit item (label, the text as its value) that UIA can focus and set.
    std::optional<design::AccessibilityRole> accessibilityRole() const override { return design::AccessibilityRole::edit; }
    void accessibilityItems(const design::Layer& root, std::vector<design::AccessibilityItem>&) const override;
private:
    std::unique_ptr<core::notes::RichDocument> document_;
    std::uint32_t capacity_;
    design::Layer *frame_, *placeholder_, *track_, *thumb_;
    std::unique_ptr<design::ScrollBarDriver> bar_;
    ArchiveBodyEditorHost* host_{};
    bool framed_{true};
    double scrollOffset_{-1}, overflow_{-1};                 // last reported (-1: none since attach)
    void placeIndicator();
};
} // namespace endfield::modules
