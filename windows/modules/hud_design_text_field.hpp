#pragma once
// The redesign's text field, portable half: the port of
// Sources/HUDDesignTextField.swift (HUDDesignTextField, its options, idle
// label, begin/commit/cancel rules) and of the projected editor rules of
// Sources/HUDProjectedTextEditor.swift that are not TextKit (caret blink
// LIB-87, edit shortcuts LIB-89, right-click menu LIB-90, auto-hiding
// indicator LIB-91). LIB-83..92.
//
// While idle a field is a drawn label (placeholder in placeholderColor when
// empty; `•` x count when secure; single line vertically centred at
// size x max(1.2, lh); multi-line max lines = height / (size x 1.5)). Editing
// attaches the native editor (TSF, IME, selection, undo, screen reader) on the
// plane through TextEditorHost, implemented by native/hud_design_text_field.hpp
// over NativeProjectedEditor; tests use a fake host.
#include "modules/hud_design_controls.hpp"

namespace endfield::modules::design {
class TextField;

// What the native editor needs to attach (one field edits per canvas).
struct TextEditRequest {
    TextField* field{};
    Rect frame;                          // the field's text rect in box px (inset applied)
    TextStyle style;                     // or `attributed` below
    bool multiline{}, secure{}, light{};
    TextAlignment alignment{TextAlignment::left};
    Color caretColor, selectionColor;    // caret accentInk (or the field's accent; light: ink); selection yellow .35
    std::string text;
    // Caret placement (LIB-84): an explicit restored selection (UTF-16 range),
    // else select all (selectsAllOnBegin), else where the activating click
    // landed (box px), else the end.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> restoredSelection;
    bool selectAll{};
    std::optional<Point> click;
};
class TextEditorHost {
public:
    virtual ~TextEditorHost() = default;
    virtual bool begin(const TextEditRequest&) = 0;                 // false: no editor available (field stays idle)
    virtual void end(TextField&) = 0;                               // detach; the field already took the text
    virtual std::string text(const TextField&) const = 0;           // current editor text (an IME composition counts as typed: unmarkText)
    virtual std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const TextField&) const = 0;
    virtual bool hasMarkedText(const TextField&) const = 0;         // IME composition in progress
    virtual void setText(TextField&, std::string_view) = 0;         // keeps the caret where possible
    virtual void reproject(TextField&, Rect frame) = 0;             // layout / scroll moved the field
};

// The right-click edit menu (LIB-90): Cut / Copy / Paste / Select All
// ("Cut"/剪切, "Copy"/复制, "Paste"/粘贴, "Select All"/全选), NO Undo; a
// click outside the selection moves the caret there first; secure fields
// refuse Cut and Copy. Undo/redo are keyboard only (Ctrl+Z, Ctrl+Y and
// Ctrl+Shift+Z; LIB-89). The native host decides how the menu is shown.
enum class EditCommand : std::uint8_t { cut, copy, paste, selectAll, undo, redo };

struct TextFieldOptions {
    bool light{}, multiline{}, returnCommits{true}, secure{};
    std::optional<std::size_t> maximumLength;  // characters
    bool selectsAllOnBegin{};
    std::optional<TextStyle> style;            // default type::field (light: ink 121212)
    std::optional<Color> placeholderColor;     // default muted
    Size inset{10, 0};                         // multi-line: at least 6 vertically
    bool escapeCommits{};                      // a note: Esc keeps the text
    bool plain{};                              // no fill, ring or bar
    std::optional<Color> accent, background;   // Personal ID
    TextAlignment alignment{TextAlignment::left};
    // Options.attributes (user-styled text: the Notes centre note): drawn and
    // edited with the user's runs instead of `style`, idle text top-aligned
    // (HUDStyledTextLayer). The owner supplies the core::notes::RichDocument
    // to the native host (NativeProjectedEditor's rich mode); the idle label
    // is the rasterizer's Notes rich line mode.
    bool rich{};
};
// CharacterSet.newlines (LF, VT, FF, CR, NEL, LS, PS), each replaced by a
// space: `components(separatedBy: .newlines).joined(separator: " ")`, the
// single-line field rule (bounded) and Calendar's event plate details.
std::string joinedLines(std::string_view);

class TextField final : public Control {
public:
    using Options = TextFieldOptions;
    TextField(Builder&, std::string id, std::string label, std::string text, std::string placeholder, Size, Options = {});
    ~TextField() override;                         // detaches a running editor (no callbacks)
    const Options& options() const noexcept { return options_; }
    FieldFrame& frame() noexcept { return *frame_; }
    const std::string& text() const noexcept { return text_; }
    void setText(std::string);                     // bounded (single line joins newlines with a space; maximumLength)
    void setPlaceholder(std::string);
    // A new size keeps the text, the focus and the running editor (the Notes
    // add field follows the width the Add button leaves it; the Mac makes a
    // new field with the same draft when its width changes).
    void setSize(Size);
    void setInvalid(bool, bool shake = false);     // shake: additive x 0,-6,6,-6,6,0 over .32 (not reduced)
    bool editing() const noexcept { return editing_; }
    void beginEditing(std::optional<std::pair<std::uint32_t, std::uint32_t>> selection = {});
    void endEditing(bool commit);                  // commit keeps the text; cancel restores the original
    // refreshProjection (:237): the editing field moved (a re-render placed it
    // elsewhere); the native editor follows (TextEditorHost::reproject).
    void refreshProjection();
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selectedRange() const;
    // Keys while editing, never while marked text exists (LIB-85): Esc ends
    // (restore + onCancel, or commit when escapeCommits); Return: single-line
    // commits then onReturn; multi-line commits only with returnCommits and
    // no Shift; Tab (single-line) commits then onTab(shift). True when used.
    bool handleEditingKey(const KeyInput&);
    void editorChanged(std::string text);          // the native editor reported an edit (bounded, onChange)
    void editorFocusLost();                        // commits (checked after the focus settles)
    std::function<void()> onBegin, onCancel, onReturn;
    // Set by Canvas::registerField: willBeginEditing runs before the editor
    // attaches (the canvas ends every other editing field WITH commit: one
    // editing field per canvas); editingStateChanged after begin and end (the
    // canvas calls Context::onLockParallax on begin and onInputStateChange).
    std::function<void(TextField&)> willBeginEditing, editingStateChanged;
    std::function<void(const std::string&)> onChange, onCommit;
    std::function<void(bool shift)> onTab;
    void activate() override;                      // beginEditing at the press point, then onActivate
    bool pointerDown(Point) override;              // enabled && !editing
    void pointerUp(Point) override;
    std::optional<AccessibilityRole> accessibilityRole() const override; // edit; none while editing (the editor is the element)
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
    void setEditorHost(TextEditorHost* host) noexcept { host_ = host; }
    // The editor host a new edit begins on (the canvas: its Context's current
    // one, which every owner entry point binds to its own editors), asked at
    // each beginEditing; the field keeps that host until the edit ends. Unset:
    // the host given by setEditorHost.
    std::function<TextEditorHost*()> currentEditorHost;
    std::string idleText() const;                  // test hook (idleTextForTesting)
private:
    TextField(Builder&, std::unique_ptr<FieldFrame>, std::string id, std::string label, std::string text, std::string placeholder, Options);
    Options options_; std::unique_ptr<FieldFrame> frame_; Layer* idle_{};
    std::string text_, placeholder_, original_;
    bool editing_{}, invalid_{}, finishing_{};
    std::optional<Point> pressPoint_;
    TextEditorHost* host_{};
    Rect textRect() const;
    TextStyle textStyle() const;
    std::string displayed() const;                 // the text, or a bullet per character when secure
    void renderIdle();
    std::string bounded(std::string) const;
};
} // namespace endfield::modules::design
