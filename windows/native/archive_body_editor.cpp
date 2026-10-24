#include "native/archive_body_editor.hpp"
#ifdef _WIN32
#include "native/hud_design_scene.hpp"
#include "modules/archive_rich_text.hpp"
#include "native/notes_text_navigation.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace d = modules::design;
using Matrix = core::Matrix4;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
bool locked(HRESULT hr) { return hr == TS_E_NOLOCK; }
std::array<double, 4> rgba(d::Color c) { return {c.r, c.g, c.b, c.a}; }
// The short editor's painted leaf bound (v1.2.0 ArchivePreviewOptions::editorCapacity).
constexpr std::uint32_t shortCapacity = 65536, maximumUnits = 2 * 1024 * 1024;
std::optional<core::Point> project(const Matrix& m, core::Point p) {
    const auto& v = m.values;
    const double w = v[3] * p.x + v[7] * p.y + v[15];
    if (!std::isfinite(w) || std::abs(w) < 1e-12) return std::nullopt;
    return core::Point{(v[0] * p.x + v[4] * p.y + v[12]) / w, (v[1] * p.x + v[5] * p.y + v[13]) / w};
}
UINT_PTR nextGeneration() { static UINT_PTR value{0x41420000}; return ++value; }
}

struct NativeArchiveBodyEditor::Impl {
    struct Editor {
        LayerScene glyphs;
        std::unique_ptr<NativeNotesEditor> editor;
        UINT_PTR generation{nextGeneration()};
        core::Rect frame;               // the text rect it was made for (area layer coordinates)
        bool published{};
        explicit Editor(LayerRasterizer& r) : glyphs(r) {}
    };
    LayerRasterizer& raster;
    NativeArchiveBodyEditorOptions options;
    modules::ArchiveBodyArea* area{};
    std::unique_ptr<Editor> active;
    std::vector<std::unique_ptr<Editor>> retired;
    NativeArchiveBodyPose pose;
    bool posed{}, visible{}, focused{}, windowFocused{true}, dragging{}, shownCaret{};
    std::uint64_t revision{};
    core::text::Selection selection;
    d::CaretBlink blink;
    NotesClickCount clicks;
    Matrix local;                       // text rect -> screen this frame
    float opacity{1};
    std::size_t promoted{};
    Impl(LayerRasterizer& r, NativeArchiveBodyEditorOptions o) : raster(r), options(std::move(o)) {
        need(!options.threadManager || options.clientID != TF_CLIENTID_NULL, "A borrowed TSF manager needs its client id");
        need(options.ownerMessage >= WM_APP && options.ownerMessage <= 0xbfff, "The Archive body needs a private change message");
        need(options.chunkedUnits > 0 && options.chunkedUnits <= shortCapacity, "Invalid Archive body chunking bound");
        retired.reserve(4);
    }
    bool editing() const noexcept { return active && active->editor && area; }
    core::notes::RichDocument& document() const { return area->document(); }
    ProjectedEditorStyle style(core::Rect frame) const {
        // HUDProjectedTextEditor over ArchiveBodyTextView: the standard faces
        // (nil fontName = HarmonyOS Sans with the region's Noto cascade), the
        // #EEEEEE ink for runs without a colour, the accentInk caret and the
        // yellow .35 selection (:1561-1566).
        ProjectedEditorStyle s;
        s.width = std::max(1., frame.width); s.height = std::max(1., frame.height);
        s.fontSize = modules::archive_rich_text::newDocumentSize;
        s.fontFamily = ".AppleSystemUIFont"; s.fontFace = ".AppleSystemUIFont";
        s.textColor = s.compositionColor = rgba(pose.text);
        s.caretColor = rgba(pose.caret); s.selectionColor = rgba(pose.selection);
        s.wrapped = true;
        s.naturalParagraphSpacingOne = true; // see the header: no lineHeightMultiple 1.3
        return s;
    }
    std::unique_ptr<Editor> make(core::Rect frame, bool chunked) {
        need(options.window && IsWindow(static_cast<HWND>(options.window)), "The Archive body needs the HUD window");
        auto e = std::make_unique<Editor>(raster);
        e->frame = frame;
        auto& doc = document();
        const auto hwnd = static_cast<HWND>(options.window);
        if (chunked) {
            if (doc.maximumUnits() < maximumUnits) doc.setMaximumUnits(maximumUnits);
            e->editor = std::make_unique<NativeNotesEditor>(std::make_unique<NativeNotesLongEditor>(hwnd, doc, e->glyphs, raster, style(frame), options.raster,
                options.ownerMessage, e->generation, ProjectedEditorTextMode::rich, options.longEditor));
        } else {
            e->editor = std::make_unique<NativeNotesEditor>(std::make_unique<NativeProjectedEditor>(hwnd, doc, e->glyphs, style(frame), options.raster,
                PlainEditorFixtureCapacity{shortCapacity}, options.ownerMessage, e->generation));
        }
        if (options.threadManager) {
            const auto hr = e->editor->connect(*options.threadManager, options.clientID);
            need(SUCCEEDED(hr), "Cannot connect the Archive body to the shared text manager");
        }
        e->editor->syncContent();
        return e;
    }
    // Ends input and keeps a published scene until a publication without it.
    void retire() noexcept {
        if (!active) return;
        try {
            if (active->editor && options.threadManager) { (void)active->editor->commitComposition(); (void)active->editor->focus(false); (void)active->editor->stop(); }
        } catch (...) {}
        active->editor.reset();
        if (active->published) retired.push_back(std::move(active)); else active.reset();
        focused = false; dragging = false; shownCaret = false;
    }
    void setTextFocus(bool value) {
        if (!editing()) return;
        if (options.threadManager) {
            const auto hr = active->editor->focus(value && windowFocused);
            need(SUCCEEDED(hr) || locked(hr), "Cannot update the Archive body focus");
        }
    }
    // After any edit, selection, scroll or composition change: the area hears
    // about text and selection changes; the caret restarts solid.
    void changed() {
        if (!editing()) return;
        active->editor->syncContent();
        auto& doc = document();
        const bool text = doc.revision() != revision;
        const auto now = doc.selection();
        const bool moved = now != selection;
        revision = doc.revision(); selection = now;
        if (text || moved) blink.restart(pose.time);
        auto* a = area;
        if (text && !doc.composition()) a->editorChanged();     // may dispose (a classified relock)
        if (moved && editing() && area == a) a->editorSelectionChanged();
        if (editing()) applyPose();
    }
    float caretOpacity() const { return float(blink.opacity(pose.time, pose.reduceMotion)); }
    void applyPose() {
        if (!editing() || !visible) return;
        active->editor->setPose({local, pose.screenToClip, pose.pixelWidth, pose.pixelHeight, opacity, true, pose.windowForeground && windowFocused && focused, true, caretOpacity()});
        // The area's indicator follows the text view's scroll (numbers only;
        // the area ignores a report that changes nothing).
        area->editorScrolled(active->editor->scrollOffset(), active->editor->maximumScrollOffset());
    }
    bool insidePhysical(core::Point physical) const {
        if (!editing() || !visible) return false;
        const auto& placement = active->editor->placement();
        const auto q = placement.projection.unproject(physical);
        if (!q) return false;
        const auto& v = placement.viewport;
        return q->x >= v.x && q->y >= v.y && q->x < v.x + v.width && q->y < v.y + v.height;
    }
    // A paste that grows a short body past the single-layout field reopens
    // it in the chunked editor first (v1.2.0 promoteBody): the same document
    // with its text, runs, selection, typing style and undo; scroll and focus kept.
    bool promote(std::size_t resulting) {
        if (!editing() || active->editor->chunked() || resulting <= shortCapacity || resulting > maximumUnits || document().composition() || retired.size() >= 3) return false;
        const auto previous = document().maximumUnits();
        const auto scroll = active->editor->scrollOffset();
        const bool wasFocused = focused;
        std::unique_ptr<Editor> next;
        try { next = make(active->frame, true); }
        catch (const NativeNotesLongParagraphError&) { document().setMaximumUnits(previous); return false; }
        catch (...) { document().setMaximumUnits(previous); throw; }
        retire();
        active = std::move(next);
        active->editor->setScrollOffset(scroll);
        if (wasFocused) { focused = true; setTextFocus(true); }
        ++promoted;
        return true;
    }
    void clipboardKey(char key) {
        if (!editing() || !options.clipboard || document().composition()) return;
        auto& doc = document();
        if (key == 'V') {
            const auto text = options.clipboard->readText(notesClipboardMaximumUnits);
            if (!text) return;
            const auto range = doc.selection().range;
            (void)promote(doc.text().size() - (range.end - range.start) + notesClipboardPastedText(*text, true).size());
            if (!editing()) return;
            (void)notesClipboardPaste(*active->editor, doc, *text, true);
            return;
        }
        if (const auto copied = notesClipboardSelection(doc, active->editor->richLayoutEnabled()); copied && options.clipboard->writeText(*copied) && key == 'X')
            (void)notesClipboardDeleteSelection(*active->editor, doc);
    }
};

NativeArchiveBodyEditor::NativeArchiveBodyEditor(LayerRasterizer& r, NativeArchiveBodyEditorOptions o) : impl_(std::make_unique<Impl>(r, std::move(o))) {}
NativeArchiveBodyEditor::~NativeArchiveBodyEditor() { impl_->retire(); }

bool NativeArchiveBodyEditor::attach(modules::ArchiveBodyArea& a) {
    auto& i = *impl_;
    const auto frame = a.textRect();
    if (i.area == &a && i.editing() && i.active->frame.width == frame.width && i.active->frame.height == frame.height) return true;
    if (i.area != &a) { i.retire(); i.area = &a; }
    if (!(frame.width > 0 && frame.height > 0)) return false;
    if (i.editing()) { layoutChanged(a); return i.editing(); }
    auto& doc = a.document();
    const bool chunked = doc.text().size() > i.options.chunkedUnits;
    if (!chunked && doc.maximumUnits() > shortCapacity && doc.text().size() <= shortCapacity) doc.setMaximumUnits(shortCapacity);
    i.active = i.make(frame, chunked);
    i.revision = doc.revision(); i.selection = doc.selection();
    if (i.posed) i.applyPose();
    return true;
}
void NativeArchiveBodyEditor::suspend(modules::ArchiveBodyArea& a) {
    auto& i = *impl_;
    if (i.area != &a) return;
    if (i.focused) blur(a);
    i.dragging = false;
}
void NativeArchiveBodyEditor::dispose(modules::ArchiveBodyArea& a) noexcept {
    auto& i = *impl_;
    if (i.area != &a) return;
    i.retire();
    i.area = nullptr;
    i.visible = false;
}
bool NativeArchiveBodyEditor::attached(const modules::ArchiveBodyArea& a) const noexcept { return impl_->area == &a && impl_->editing(); }
bool NativeArchiveBodyEditor::focused(const modules::ArchiveBodyArea& a) const noexcept { return impl_->area == &a && impl_->editing() && impl_->focused; }
// makeFirstResponder + setSelectedRange + scrollRangeToVisible (:1642-1650).
void NativeArchiveBodyEditor::focus(modules::ArchiveBodyArea& a, core::text::Range range) {
    auto& i = *impl_;
    if (!attach(a)) return;
    (void)i.active->editor->setSelectionFromHost({range});
    (void)i.active->editor->revealCaret();
    const bool was = i.focused;
    i.focused = true;
    i.setTextFocus(true);
    i.blink.restart(i.pose.time);
    i.changed();
    if (!was && i.area == &a) a.editorFocusChanged(true);
}
// blur (:1652-1657): the first responder goes back to the host; ending
// editing commits marked text and flushes (textDidEndEditing).
void NativeArchiveBodyEditor::blur(modules::ArchiveBodyArea& a) {
    auto& i = *impl_;
    if (i.area != &a || !i.editing() || !i.focused) return;
    if (i.options.threadManager) { const auto hr = i.active->editor->commitComposition(); (void)hr; }
    i.changed();
    if (!i.editing() || i.area != &a) return;
    i.setTextFocus(false);
    i.focused = false; i.dragging = false;
    if (i.posed) i.applyPose();
    a.editorFocusChanged(false);
    a.editorEndedEditing();
}
bool NativeArchiveBodyEditor::hasMarkedText(const modules::ArchiveBodyArea& a) const noexcept {
    const auto& i = *impl_;
    return i.area == &a && i.editing() && a.document().composition().has_value();
}
bool NativeArchiveBodyEditor::applyFormat(modules::ArchiveBodyArea& a, const core::notes::FormatChange& change, core::text::Range range, bool keepSelection) {
    auto& i = *impl_;
    if (i.area != &a || !i.editing() || a.document().composition()) return false;
    auto& e = *i.active->editor;
    const auto before = a.document().selection();
    if (before.range != range) (void)e.setSelectionFromHost({range});
    const auto result = e.applyFormat(change);
    if (!keepSelection && before.range != range) (void)e.setSelectionFromHost(before);
    e.syncContent();
    // The canvas captures the runs itself (bodyChanged follows).
    i.revision = a.document().revision(); i.selection = a.document().selection();
    if (i.posed) i.applyPose();
    return result.changed;
}
bool NativeArchiveBodyEditor::replace(modules::ArchiveBodyArea& a, core::text::Range range, std::u16string_view text) {
    auto& i = *impl_;
    if (i.area != &a || !i.editing()) return false;
    const auto result = i.active->editor->replaceTextFromHost(range, text);
    i.active->editor->syncContent();
    i.revision = a.document().revision(); i.selection = a.document().selection();
    if (i.posed) i.applyPose();
    return result.changed;
}
void NativeArchiveBodyEditor::scroll(modules::ArchiveBodyArea& a, double delta) {
    auto& i = *impl_;
    if (i.area != &a || !i.editing() || !std::isfinite(delta)) return;
    i.active->editor->syncContent();
    (void)i.active->editor->scrollBy(delta);
    if (i.posed) i.applyPose();
}
// A new size (the media strip came or went) replaces the editor over the same
// document: text, runs, selection, undo, scroll and focus survive (the Mac
// keeps its text view; v1.2.0 reused the TextKit view the same way).
void NativeArchiveBodyEditor::layoutChanged(modules::ArchiveBodyArea& a) {
    auto& i = *impl_;
    if (i.area != &a || !i.editing()) return;
    const auto frame = a.textRect();
    if (frame.width == i.active->frame.width && frame.height == i.active->frame.height) return;
    if (a.document().composition()) return;
    const bool chunked = i.active->editor->chunked(), was = i.focused;
    const auto scroll = i.active->editor->scrollOffset();
    i.retire();
    i.active = i.make(frame, chunked);
    (void)i.active->editor->setScrollOffset(scroll);
    if (was) { i.focused = true; i.setTextFocus(true); }
    i.revision = a.document().revision(); i.selection = a.document().selection();
    if (i.posed) i.applyPose();
}

void NativeArchiveBodyEditor::setPose(const NativeArchiveBodyPose& p) {
    auto& i = *impl_;
    need(p.boxWorld.finite() && p.screenToClip.finite() && p.pixelWidth && p.pixelHeight && std::isfinite(p.time), "Invalid Archive body pose");
    const bool restyle = i.posed && (p.text != i.pose.text || p.caret != i.pose.caret || p.selection != i.pose.selection);
    i.pose = p; i.posed = true;
    i.visible = false;
    if (!i.editing() || !p.root || !d::isVisible(i.area->layer(), *p.root)) { i.shownCaret = false; return; }
    if (restyle) (void)i.active->editor->setStyle(i.style(i.active->frame));
    const auto layer = d::sampleLayer(i.area->layer(), *p.root, p.time);
    if (!layer.visible) { i.shownCaret = false; return; }
    const auto text = i.area->textRect();
    i.local = p.boxWorld * designMatrix(layer.world) * Matrix::translation(text.x, text.y);
    i.opacity = float(std::clamp(double(p.opacity) * layer.opacity, 0.0, 1.0));
    i.visible = true;
    const bool caret = i.focused && p.windowForeground && i.windowFocused;
    if (caret && !i.shownCaret) i.blink.restart(p.time);
    i.shownCaret = caret;
    i.applyPose();
}
bool NativeArchiveBodyEditor::shown() const noexcept { return impl_->editing() && impl_->visible; }
bool NativeArchiveBodyEditor::filterKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam) {
    auto& i = *impl_;
    return i.editing() && i.focused && i.windowFocused && i.active->editor->filterKeyMessage(message, wParam, lParam);
}
// The text view's keys (ArchiveBodyTextView.keyDown / performKeyEquivalent,
// doCommandBy): the format shortcuts, the edit and history shortcuts, the
// Windows word and line navigation, Esc (blur), then caret movement.
bool NativeArchiveBodyEditor::key(const d::KeyInput& k) {
    auto& i = *impl_;
    if (!i.editing() || !i.focused) return false;
    auto* a = i.area;
    auto& e = *i.active->editor;
    auto& doc = a->document();
    e.syncContent();
    const auto letter = (k.virtualKey >= 'A' && k.virtualKey <= 'Z') ? char(k.virtualKey) : '\0';
    const bool control = k.control && !k.alt && !k.win;
    if (control && !doc.composition()) {
        if (!k.shift && (letter == 'B' || letter == 'I' || letter == 'U')) {
            if (a->onFormatShortcut) a->onFormatShortcut(letter == 'B' ? core::notes::FormatKind::bold : letter == 'I' ? core::notes::FormatKind::italic : core::notes::FormatKind::underline);
            return true;
        }
        if (!k.shift && letter == 'A') { (void)e.command(ProjectedEditorCommand::selectAll); i.changed(); return true; }
        if (!k.shift && letter == 'Z') { (void)e.undo(); i.changed(); return true; }
        if ((!k.shift && letter == 'Y') || (k.shift && letter == 'Z')) { (void)e.redo(); i.changed(); return true; }
        if (!k.shift && (letter == 'X' || letter == 'C' || letter == 'V')) { i.clipboardKey(letter); i.changed(); return true; }
        if (k.virtualKey == d::key::left || k.virtualKey == d::key::right || k.virtualKey == d::key::back || k.virtualKey == d::key::deleteForward
            || k.virtualKey == d::key::home || k.virtualKey == d::key::end) {
            (void)notesNavigationKey(e, doc, k.virtualKey, true, k.shift); i.changed(); return true;
        }
        return false;
    }
    if (doc.composition() || k.alt || k.win) return false;                 // the IME has these keys
    if (k.virtualKey == d::key::escape) { blur(*a); return true; }          // doCommandBy cancelOperation (:681)
    if (k.virtualKey == d::key::home || k.virtualKey == d::key::end) { (void)notesNavigationKey(e, doc, k.virtualKey, false, k.shift); i.changed(); return true; }
    std::optional<ProjectedEditorCommand> command;
    switch (k.virtualKey) {
    case d::key::left: command = ProjectedEditorCommand::left; break;
    case d::key::right: command = ProjectedEditorCommand::right; break;
    case d::key::up: command = ProjectedEditorCommand::up; break;
    case d::key::down: command = ProjectedEditorCommand::down; break;
    case d::key::back: command = ProjectedEditorCommand::backspace; break;
    case d::key::deleteForward: command = ProjectedEditorCommand::deleteForward; break;
    default: break;
    }
    if (!command) return k.virtualKey == d::key::enter || k.virtualKey == d::key::tab; // their characters follow
    (void)e.command(*command, k.shift);
    i.changed();
    return true;
}
bool NativeArchiveBodyEditor::character(std::uint32_t value, bool unicodeScalar) {
    auto& i = *impl_;
    if (!i.editing() || !i.focused) return false;
    // Return, line feed and Tab type into the text view (NSTextView inserts a
    // tab); other control characters followed their keys.
    if ((value < 0x20 && value != '\r' && value != '\n' && value != '\t') || value == 0x7f) return true;
    const auto result = i.active->editor->character(value, unicodeScalar);
    if (result.changed) i.changed();
    return result.handled;
}
bool NativeArchiveBodyEditor::message(std::uint32_t message, std::uintptr_t wParam, std::intptr_t) {
    auto& i = *impl_;
    if (message != i.options.ownerMessage || !i.editing() || wParam != i.active->generation) return message == i.options.ownerMessage;
    (void)i.active->editor->takeChanges(UINT_PTR(wParam));
    i.changed();
    return true;
}
bool NativeArchiveBodyEditor::pointerDown(core::Point physical, core::Point logical, bool extend, bool doubleClick) {
    auto& i = *impl_;
    if (!i.insidePhysical(physical)) return false;
    auto* a = i.area;
    const bool was = i.focused;
    if (!was) { i.focused = true; i.setTextFocus(true); }
    auto& e = *i.active->editor;
    e.syncContent();
    const auto count = extend ? 1u : i.clicks.press(doubleClick, logical, i.pose.time);
    i.dragging = e.pointerDown(physical, extend).handled;
    // The source double-click selects the word, a triple-click the paragraph.
    if (count > 1) {
        auto& doc = a->document();
        const auto s = doc.selection();
        const auto at = s.activeEnd == core::text::ActiveEnd::start ? s.range.start : s.range.end;
        (void)notesSelectRange(e, doc, count == 2 ? notesWordAt(doc.text(), at) : notesParagraphAt(doc.text(), at));
        i.dragging = false;
    }
    i.changed();
    if (!was && i.area == a) a->editorFocusChanged(true);
    return true;
}
void NativeArchiveBodyEditor::pointerDrag(core::Point physical) {
    auto& i = *impl_;
    if (!i.editing() || !i.dragging) return;
    if (i.active->editor->pointerDrag(physical).changed) i.changed();
}
void NativeArchiveBodyEditor::pointerUp() { auto& i = *impl_; if (i.editing()) i.active->editor->pointerUp(); i.dragging = false; }
// Right-click (the text view's menu: Cut / Copy / Paste / Select All, as the
// design fields' LIB-90): the caret moves where the click landed unless it is
// inside the selection, then the menu opens at the pointer.
bool NativeArchiveBodyEditor::rightPointerDown(core::Point physical, core::Point logical) {
    auto& i = *impl_;
    if (!i.insidePhysical(physical)) return false;
    auto* a = i.area;
    if (!i.focused) { i.focused = true; i.setTextFocus(true); a->editorFocusChanged(true); }
    if (!i.editing()) return true;
    auto& e = *i.active->editor;
    auto& doc = a->document();
    e.syncContent();
    if (!doc.composition()) {
        const auto r = doc.selection().range;
        const auto at = core::text::projectedInsertion(doc, e.geometry(), physical, e.placement());
        if (at && !(r.start != r.end && *at >= std::min(r.start, r.end) && *at <= std::max(r.start, r.end))) { (void)e.setSelectionFromHost({{*at, *at}}); i.changed(); }
    }
    if (!i.editing() || !i.options.editMenu) return true;
    const auto r = doc.selection().range;
    const bool any = r.start != r.end;
    bool canPaste{};
    if (i.options.clipboard) { const auto value = i.options.clipboard->readText(notesClipboardMaximumUnits); canPaste = value && !value->text.empty(); }
    const auto chosen = i.options.editMenu(logical, any && i.options.clipboard, any && i.options.clipboard, canPaste);
    if (!chosen || !i.editing()) return true;
    switch (*chosen) {
    case d::EditCommand::cut: i.clipboardKey('X'); break;
    case d::EditCommand::copy: i.clipboardKey('C'); break;
    case d::EditCommand::paste: i.clipboardKey('V'); break;
    case d::EditCommand::selectAll: if (i.editing()) (void)i.active->editor->command(ProjectedEditorCommand::selectAll); break;
    default: break;
    }
    i.changed();
    return true;
}
bool NativeArchiveBodyEditor::wheel(core::Point physical, double delta) {
    auto& i = *impl_;
    if (!std::isfinite(delta) || !i.insidePhysical(physical)) return false;
    i.active->editor->syncContent();
    (void)i.active->editor->scrollBy(delta);
    i.changed();
    return true;
}
bool NativeArchiveBodyEditor::contains(core::Point physical) const noexcept {
    try { return impl_->insidePhysical(physical); } catch (...) { return false; }
}
bool NativeArchiveBodyEditor::textCursorAt(core::Point logical) const noexcept {
    const auto& i = *impl_;
    if (!i.editing() || !i.visible || !i.pose.pixelWidth) return false;
    // The pose maps to physical pixels; the cursor asks in logical points.
    const double scale = double(GetDpiForWindow(static_cast<HWND>(i.options.window))) / 96;
    try { return i.insidePhysical({logical.x * scale, logical.y * scale}); } catch (...) { return false; }
}
void NativeArchiveBodyEditor::setFocused(bool value) {
    auto& i = *impl_;
    if (i.windowFocused == value) return;
    i.windowFocused = value;
    if (i.focused) { i.setTextFocus(true); if (value) i.blink.restart(i.pose.time); }
    if (i.posed) i.applyPose();
}
bool NativeArchiveBodyEditor::requiresFrames(double now) const {
    const auto& i = *impl_;
    return i.editing() && i.visible && i.shownCaret && i.blink.ramping(now, i.pose.reduceMotion);
}
std::optional<double> NativeArchiveBodyEditor::nextWakeTime(double now) const {
    const auto& i = *impl_;
    if (!i.editing() || !i.visible || !i.shownCaret) return std::nullopt;
    return i.blink.nextChange(now, i.pose.reduceMotion);
}
std::optional<LayerCompositionEntry> NativeArchiveBodyEditor::entry() const {
    auto& i = *impl_;
    if (!i.editing() || !i.visible) return std::nullopt;
    i.active->published = true;
    return LayerCompositionEntry{&i.active->glyphs, {}};
}
bool NativeArchiveBodyEditor::collectRetired(Renderer& r) {
    auto& i = *impl_;
    for (auto& e : i.retired) if (!e->glyphs.releaseResources(r)) return false;
    i.retired.clear();
    return true;
}
bool NativeArchiveBodyEditor::releaseResources(Renderer& r) {
    auto& i = *impl_;
    bool ok = collectRetired(r);
    if (i.active) ok = i.active->glyphs.releaseResources(r) && ok;
    return ok;
}
std::size_t NativeArchiveBodyEditor::promotions() const noexcept { return impl_->promoted; }
} // namespace endfield::native
#endif
