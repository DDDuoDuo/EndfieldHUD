#include "native/hud_design_text_field.hpp"
#ifdef _WIN32
#include "native/projected_editor.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace d = modules::design;
using Matrix = core::Matrix4;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
// The field's editor bound: the painted editor leaf's UTF-16 capacity.
constexpr std::uint32_t capacity = 65536;
std::u16string utf16(std::string_view s) {
    if (s.empty()) return {};
    need(s.size() <= INT_MAX, "Design field text is too long");
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    need(n > 0, "Design field text is not UTF-8");
    std::u16string out(std::size_t(n), u'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), reinterpret_cast<wchar_t*>(out.data()), n);
    return out;
}
std::string utf8(std::u16string_view s) {
    if (s.empty()) return {};
    const auto* w = reinterpret_cast<const wchar_t*>(s.data());
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, int(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::array<double, 4> rgba(d::Color c) { return {c.r, c.g, c.b, c.a}; }
// The standard face at the style's weight (the rasterizer renders source UI
// faces in HarmonyOS Sans + the region's Noto cascade, weight from the name).
std::string face(d::FontWeight w) {
    switch (d::resolved(w)) {
    case d::FontWeight::light: return ".AppleSystemUIFontLight";
    case d::FontWeight::medium: return ".AppleSystemUIFontMedium";
    case d::FontWeight::bold: case d::FontWeight::semibold: return ".AppleSystemUIFontBold";
    case d::FontWeight::black: return ".AppleSystemUIFontBlack";
    default: return ".AppleSystemUIFont";
    }
}
std::optional<core::Point> project(const Matrix& m, core::Point p) {
    const auto& v = m.values;
    const double w = v[3] * p.x + v[7] * p.y + v[15];
    if (!std::isfinite(w) || std::abs(w) < 1e-12) return std::nullopt;
    return core::Point{(v[0] * p.x + v[4] * p.y + v[12]) / w, (v[1] * p.x + v[5] * p.y + v[13]) / w};
}
bool locked(HRESULT hr) { return hr == TS_E_NOLOCK; }
// Secure fields (LIB-86): the editor's document holds one U+2022 per
// character (the Mac's glyph substitution) and the text lives only here.
constexpr char16_t bullet = u'\u2022';
std::u32string scalars(std::u16string_view s) {
    std::u32string out;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const char32_t c = s[n];
        if (c >= 0xd800 && c <= 0xdbff && n + 1 < s.size() && s[n + 1] >= 0xdc00 && s[n + 1] <= 0xdfff) { out.push_back(0x10000 + ((c - 0xd800) << 10) + (s[n + 1] - 0xdc00)); ++n; }
        else out.push_back(c);
    }
    return out;
}
std::u16string units(std::u32string_view s) {
    std::u16string out;
    for (const auto c : s) {
        if (c > 0xffff) { out.push_back(char16_t(0xd800 + ((c - 0x10000) >> 10))); out.push_back(char16_t(0xdc00 + ((c - 0x10000) & 0x3ff))); }
        else out.push_back(char16_t(c));
    }
    return out;
}
}

struct NativeDesignTextFields::Impl {
    struct Editor {
        d::TextField* field{};
        d::TextEditRequest request;
        std::unique_ptr<core::notes::RichDocument> owned; // plain fields
        std::optional<std::u32string> secret;          // a secure field's text (the document shows bullets)
        std::optional<char16_t> high;                  // a secure field's pending high surrogate
        core::notes::RichDocument* document{};
        LayerScene glyphs, chrome;
        std::unique_ptr<NativeProjectedEditor> editor;
        UINT_PTR generation{};
        std::optional<d::Point> click;                 // placed at the first pose
        core::text::Selection selection;               // last seen (blink restart)
        std::uint64_t revision{};
        double scroll{};
        bool stopping{}, published{};
        std::array<LayerPlacement, 1> thumb{};
        Editor(LayerRasterizer& r) : glyphs(r), chrome(r) {}
    };
    LayerRasterizer& raster;
    NativeDesignTextFieldOptions options;
    std::unique_ptr<Editor> active;
    std::vector<std::unique_ptr<Editor>> retired;
    std::vector<LayerCompositionEntry> composed;
    NativeDesignTextFieldPose pose;
    bool posed{}, focused{true}, shown{};
    d::CaretBlink blink;
    d::ScrollBarVisibility indicator;
    UINT_PTR generations{0x4844};
    Impl(LayerRasterizer& r, NativeDesignTextFieldOptions o) : raster(r), options(std::move(o)) {
        need(!options.threadManager || options.clientID != TF_CLIENTID_NULL, "A borrowed TSF manager needs its client id");
        need(options.ownerMessage >= WM_APP && options.ownerMessage <= 0xbfff, "Design fields need a private change message");
        composed.reserve(2); retired.reserve(4);
    }
    bool editing() const noexcept { return active && active->editor && !active->stopping; }
    bool secure() const noexcept { return active && active->request.secure; }
    core::text::Range selected() const {
        const auto r = active->document->selection().range;
        return {std::min(r.start, r.end), std::max(r.start, r.end)};
    }
    ProjectedEditorStyle style(const d::TextEditRequest& q) const {
        ProjectedEditorStyle s;
        s.width = std::max(1., q.frame.width); s.height = std::max(1., q.frame.height);
        s.fontSize = q.style.size;
        s.fontFamily = ".AppleSystemUIFont"; s.fontFace = face(q.style.weight);
        s.textColor = s.compositionColor = rgba(q.style.color);
        s.caretColor = rgba(q.caretColor); s.selectionColor = rgba(q.selectionColor);
        s.alignment = q.alignment == d::TextAlignment::center ? ProjectedEditorAlignment::center : q.alignment == d::TextAlignment::right ? ProjectedEditorAlignment::right : ProjectedEditorAlignment::left;
        s.wrapped = q.multiline;
        s.sourceSingleLineField = !q.multiline; // HUDProjectedTextEditor.configureSingleLine
        return s;
    }
    // The auto-hiding scroll indicator of a multi-line field (LIB-91): grey
    // .65, radius 1.5, 3 px wide, its thumb max(16, h^2 / content).
    void loadChrome(Editor& e) {
        const auto& f = e.request.frame;
        e.chrome.load(ehud::data::Json::Object{{"id", "design.field.chrome"}, {"bounds", ehud::data::Json::Array{0, 0, 0, 0}}, {"position", ehud::data::Json::Array{0, 0}},
            {"anchorPoint", ehud::data::Json::Array{0, 0}}, {"children", ehud::data::Json::Array{ehud::data::Json::Object{{"id", "design.field.indicator"},
            {"bounds", ehud::data::Json::Array{0, 0, 3, 16}}, {"position", ehud::data::Json::Array{std::max(0., f.width - 4), 0}}, {"anchorPoint", ehud::data::Json::Array{0, 0}},
            {"backgroundColor", ehud::data::Json::Object{{"sRGB", ehud::data::Json::Array{.5, .5, .5, .65}}}}, {"cornerRadius", 1.5}, {"children", ehud::data::Json::Array{}}}}}}, options.raster);
        need(e.chrome.report().unsupported.empty() && e.chrome.draws().size() == 1, "Unsupported design field indicator");
    }
    std::unique_ptr<Editor> make(const d::TextEditRequest& q) {
        need(options.window && IsWindow(static_cast<HWND>(options.window)), "Design fields need the HUD window");
        need(q.field && std::isfinite(q.frame.x) && std::isfinite(q.frame.y) && q.frame.width > 0 && q.frame.height > 0 && q.style.size > 0, "Invalid design field request");
        auto e = std::make_unique<Editor>(raster);
        e->field = q.field; e->request = q; e->generation = ++generations;
        if (q.field->options().rich) {
            e->document = options.richDocument ? options.richDocument(*q.field) : nullptr;
            if (!e->document) return nullptr;
            e->editor = std::make_unique<NativeProjectedEditor>(static_cast<HWND>(options.window), *e->document, e->glyphs, style(q), options.raster,
                PlainEditorFixtureCapacity{capacity}, options.ownerMessage, e->generation, ProjectedEditorTextMode::rich);
        } else {
            auto text = utf16(q.text);
            if (text.size() > capacity) text.resize(capacity);
            if (q.secure) { e->secret = scalars(text); text.assign(e->secret->size(), bullet); }
            e->owned = std::make_unique<core::notes::RichDocument>(std::move(text), std::nullopt, capacity);
            e->document = e->owned.get();
            e->editor = std::make_unique<NativeProjectedEditor>(static_cast<HWND>(options.window), *e->document, e->glyphs, style(q), options.raster,
                PlainEditorFixtureCapacity{capacity}, options.ownerMessage, e->generation, ProjectedEditorTextMode::plainHistory);
        }
        e->editor->syncContent();
        if (q.multiline) loadChrome(*e);
        // Caret placement (LIB-84): a restored selection, else select all,
        // else where the activating click landed (at the first pose), else the end.
        const auto units = std::uint32_t(e->document->text().size());
        // Selections are NSRanges (location, length) like the Mac's.
        if (q.restoredSelection) {
            const auto start = std::min(q.restoredSelection->first, units);
            e->editor->setSelectionFromHost({{start, start + std::min(q.restoredSelection->second, units - start)}});
        }
        else if (q.selectAll) e->editor->command(ProjectedEditorCommand::selectAll);
        else if (q.click) e->click = q.click;
        else e->editor->setSelectionFromHost({{units, units}});
        // No text service reaches a secure field (the Mac's
        // EnableSecureEventInput); its keys arrive as plain key messages.
        if (options.threadManager && !q.secure) {
            const auto hr = e->editor->connect(*options.threadManager, options.clientID);
            need(SUCCEEDED(hr), "Cannot connect the design field to the shared text manager");
            if (focused) { const auto f = e->editor->focus(true); need(SUCCEEDED(f) || locked(f), "Cannot focus the design field"); }
        }
        e->editor->syncContent();
        e->selection = e->document->selection(); e->revision = e->document->revision();
        return e;
    }
    // Ends input and keeps the published scenes until a publication without
    // them (collectRetired). A locked TSF store retries on its change notice.
    bool stop(Editor& e) {
        if (!e.editor) return true;
        if (options.threadManager && !e.secret) {
            const auto committed = e.editor->commitComposition();
            if (locked(committed)) { e.stopping = true; return false; }
            const auto hr = e.editor->stop();
            if (locked(hr)) { e.stopping = true; return false; }
        }
        e.editor.reset();
        return true;
    }
    void retire() {
        if (!active) return;
        if (!stop(*active)) return; // finished by message() once the store unlocks
        if (active->published) retired.push_back(std::move(active)); else active.reset();
        shown = false;
    }
    // After any edit, selection, scroll or composition change: the field
    // bounds its text (it may write it back through setText), the blink
    // restarts when the caret moved, the indicator shows on a scroll.
    void changed(double now) {
        if (!editing()) return;
        auto& e = *active;
        e.editor->syncContent();
        const bool text = e.document->revision() != e.revision;
        const auto selection = e.document->selection();
        if (text || selection != e.selection) blink.restart(now);
        e.revision = e.document->revision(); e.selection = selection;
        if (e.editor->scrollOffset() != e.scroll) { e.scroll = e.editor->scrollOffset(); if (e.request.multiline) indicator.noteScroll(now, pose.reduceMotion); }
        if (text && !e.document->composition()) {
            auto* field = e.field;
            field->editorChanged(content(e)); // may end editing (and retire this editor)
        }
        if (editing() && posed) applyPose();
    }
    float caretOpacity() const { return float(blink.opacity(pose.time, pose.reduceMotion)); }
    std::string content(const Editor& e) const { return e.secret ? utf8(units(*e.secret)) : utf8(e.document->text()); }
    // Replaces `range` (document units = secure characters) by `value`: the
    // secret takes the characters, the document as many bullets.
    ProjectedEditorResult replaceSecure(core::text::Range range, std::u32string_view value) {
        auto& e = *active;
        const auto start = std::min<std::size_t>(range.start, e.secret->size()), end = std::min<std::size_t>(range.end, e.secret->size());
        const std::size_t room = e.secret->size() - (end - start) < capacity ? capacity - (e.secret->size() - (end - start)) : 0;
        const auto kept = value.substr(0, room);
        const auto result = e.editor->replaceTextFromHost({std::uint32_t(start), std::uint32_t(end)}, std::u16string(kept.size(), bullet));
        if (result.changed) e.secret->replace(start, end - start, kept);
        return result;
    }
    void applyPose() {
        auto& e = *active;
        const auto& f = e.request.frame;
        const auto local = pose.boxToScreen * Matrix::translation(f.x, f.y);
        e.editor->setPose({local, pose.screenToClip, pose.pixelWidth, pose.pixelHeight, pose.opacity, true, pose.windowForeground && focused, true, caretOpacity()});
        if (e.click) {
            // The activating click (box px) as a client pixel of this pose.
            const auto at = core::Projection::viewport(pose.screenToClip * pose.boxToScreen, pose.pixelWidth, pose.pixelHeight).project(*e.click);
            e.click.reset();
            if (at) { e.editor->pointerDown(*at); e.editor->pointerUp(); e.editor->syncContent(); e.selection = e.document->selection(); }
            e.editor->setPose({local, pose.screenToClip, pose.pixelWidth, pose.pixelHeight, pose.opacity, true, pose.windowForeground && focused, true, caretOpacity()});
        }
        if (e.request.multiline && e.chrome.draws().size() == 1) {
            const auto documentHeight = e.editor->layout().painted() ? e.editor->layout().painted()->documentHeight() : 0.;
            const auto overflow = std::max(0., documentHeight - f.height);
            const auto thumb = std::clamp(f.height * f.height / std::max(1., documentHeight), 16., std::max(16., f.height));
            const auto y = overflow > 0 ? (f.height - thumb) * e.editor->scrollOffset() / overflow : 0;
            e.thumb[0] = {0, local * Matrix::translation(std::max(0., f.width - 4), y) * Matrix::scale(1, thumb / 16), overflow > 0 ? float(pose.opacity * indicator.opacity(pose.time)) : 0.f, {}};
            e.chrome.setPlacements(e.thumb);
        }
    }
    // A logical client point inside the editing field's projected rect.
    bool inside(core::Point logical) const noexcept {
        if (!editing() || !posed) return false;
        const auto& f = active->request.frame;
        std::array<core::Point, 4> quad{};
        const std::array<core::Point, 4> corners{{{f.x, f.y}, {f.x + f.width, f.y}, {f.x + f.width, f.y + f.height}, {f.x, f.y + f.height}}};
        for (std::size_t n = 0; n < 4; ++n) { const auto q = project(pose.boxToScreen, corners[n]); if (!q) return false; quad[n] = *q; }
        return core::polygonContains(quad, logical);
    }
    // The physical point of the editor's viewport (its own placement).
    bool insidePhysical(core::Point physical) const {
        if (!editing() || !posed) return false;
        const auto& placement = active->editor->placement();
        const auto local = placement.projection.unproject(physical);
        if (!local) return false;
        const auto& v = placement.viewport;
        return local->x >= v.x && local->y >= v.y && local->x < v.x + v.width && local->y < v.y + v.height;
    }
    bool command(d::EditCommand c, double now) {
        if (!editing()) return false;
        auto& e = *active;
        if (e.document->composition()) return true;
        const auto range = selected();
        ProjectedEditorResult result{};
        switch (c) {
        case d::EditCommand::copy: case d::EditCommand::cut: {
            // A secure field never puts its text on the clipboard (LIB-86).
            if (e.request.secure || range.start == range.end) return true;
            const std::u16string chosen(e.document->text().substr(range.start, range.end - range.start));
            if (!options.clipboard.write || !options.clipboard.write(chosen)) return true;
            if (c == d::EditCommand::cut && editing()) result = active->editor->replaceTextFromHost(range, u"");
            break;
        }
        case d::EditCommand::paste: {
            auto value = options.clipboard.read ? options.clipboard.read() : std::nullopt;
            if (!editing() || !value || value->empty()) return true;
            const std::size_t kept = active->document->text().size() - (range.end - range.start), room = kept < capacity ? capacity - kept : 0;
            if (value->size() > room) { value->resize(room); if (!value->empty() && value->back() >= 0xd800 && value->back() <= 0xdbff) value->pop_back(); }
            if (value->empty()) return true;
            result = active->secret ? replaceSecure(range, scalars(*value)) : active->editor->replaceTextFromHost(range, *value);
            break;
        }
        case d::EditCommand::selectAll: result = e.editor->command(ProjectedEditorCommand::selectAll); break;
        // A secure field keeps no undo (LIB-86).
        case d::EditCommand::undo: if (e.request.secure) return true; result = e.editor->undo(); break;
        case d::EditCommand::redo: if (e.request.secure) return true; result = e.editor->redo(); break;
        }
        (void)result;
        changed(now);
        return true;
    }
};

NativeDesignTextFields::NativeDesignTextFields(LayerRasterizer& r, NativeDesignTextFieldOptions o) : impl_(std::make_unique<Impl>(r, std::move(o))) {}
NativeDesignTextFields::~NativeDesignTextFields() {
    auto& i = *impl_;
    if (i.active && i.active->editor && i.options.threadManager) (void)i.active->editor->stop();
}
bool NativeDesignTextFields::begin(const d::TextEditRequest& request) {
    auto& i = *impl_;
    // One editor per owner: another field's editor ends first (its field
    // already committed through the canvas).
    if (i.active) { i.retire(); if (i.active) return false; }
    auto next = i.make(request);
    if (!next) return false;
    i.active = std::move(next);
    i.shown = false;
    i.indicator.reset();
    i.blink.restart(i.pose.time);
    if (i.posed) i.applyPose();
    return true;
}
void NativeDesignTextFields::end(d::TextField& field) {
    auto& i = *impl_;
    if (i.active && i.active->field == &field) i.retire();
}
std::string NativeDesignTextFields::text(const d::TextField& field) const {
    const auto& i = *impl_;
    return i.active && i.active->field == &field ? i.content(*i.active) : field.text();
}
std::optional<std::pair<std::uint32_t, std::uint32_t>> NativeDesignTextFields::selection(const d::TextField& field) const {
    const auto& i = *impl_;
    if (!i.active || i.active->field != &field) return std::nullopt;
    const auto r = i.active->document->selection().range;
    const auto start = std::min(r.start, r.end);
    return std::pair{start, std::max(r.start, r.end) - start}; // NSRange: location, length
}
bool NativeDesignTextFields::hasMarkedText(const d::TextField& field) const {
    const auto& i = *impl_;
    return i.active && i.active->field == &field && i.active->document->composition().has_value();
}
// The field bounded its text (single line, maximum length): the editor takes
// it through the same document/TSF path as typing, the caret kept in place.
void NativeDesignTextFields::setText(d::TextField& field, std::string_view value) {
    auto& i = *impl_;
    if (!i.editing() || i.active->field != &field) return;
    auto& e = *i.active;
    const auto caret = e.document->selection().range.end;
    ProjectedEditorResult result{};
    std::size_t length{};
    if (e.secret) {
        const auto replacement = scalars(utf16(value));
        if (replacement == *e.secret) return;
        result = i.replaceSecure({0, std::uint32_t(e.document->text().size())}, replacement);
        length = replacement.size();
    } else {
        const auto replacement = utf16(value);
        if (replacement == e.document->text()) return;
        result = e.editor->replaceTextFromHost({0, std::uint32_t(e.document->text().size())}, replacement);
        length = replacement.size();
    }
    if (!result.changed || !i.editing()) return;
    const auto at = std::min<std::uint32_t>(caret, std::uint32_t(length));
    e.editor->setSelectionFromHost({{at, at}});
    e.editor->syncContent();
    e.revision = e.document->revision(); e.selection = e.document->selection();
    if (i.posed) i.applyPose();
}
// Layout or scroll moved the field: a new origin is numeric; a new size
// replaces the editor (the editor's viewport is fixed), keeping text and caret.
void NativeDesignTextFields::reproject(d::TextField& field, core::Rect frame) {
    auto& i = *impl_;
    if (!i.editing() || i.active->field != &field) return;
    auto& e = *i.active;
    if (frame.width == e.request.frame.width && frame.height == e.request.frame.height) {
        e.request.frame = frame;
        if (i.posed) i.applyPose();
        return;
    }
    auto request = e.request;
    request.frame = frame;
    request.text = i.content(e);
    const auto r = i.selected();
    request.restoredSelection = std::pair{r.start, r.end - r.start};
    request.selectAll = false; request.click.reset();
    i.retire();
    if (i.active) return;
    i.active = i.make(request);
    if (i.active && i.posed) i.applyPose();
}
bool NativeDesignTextFields::filterKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam) {
    auto& i = *impl_;
    return i.editing() && i.focused && i.active->editor->filterKeyMessage(message, wParam, lParam);
}
// Editing keys after TSF (LIB-85, LIB-89): the edit shortcuts, then Esc /
// Return / Tab by the field's rules, then caret movement and deletion.
bool NativeDesignTextFields::key(const d::KeyInput& k) {
    auto& i = *impl_;
    if (!i.editing()) return false;
    auto& e = *i.active;
    const double now = i.pose.time;
    const auto letter = k.character >= 'A' && k.character <= 'Z' ? char32_t(k.character - 'A' + 'a') : k.character;
    const auto keyLetter = (k.virtualKey >= 'A' && k.virtualKey <= 'Z') ? char32_t(k.virtualKey - 'A' + 'a') : letter;
    if (k.control && !k.alt && !k.win) {
        // Ctrl+X/C/V/A/Z, Ctrl+Y and Ctrl+Shift+Z (the Mac's ⌘ set).
        if (!k.shift && keyLetter == 'x') return i.command(d::EditCommand::cut, now);
        if (!k.shift && keyLetter == 'c') return i.command(d::EditCommand::copy, now);
        if (!k.shift && keyLetter == 'v') return i.command(d::EditCommand::paste, now);
        if (!k.shift && keyLetter == 'a') return i.command(d::EditCommand::selectAll, now);
        if (!k.shift && keyLetter == 'z') return i.command(d::EditCommand::undo, now);
        if ((!k.shift && keyLetter == 'y') || (k.shift && keyLetter == 'z')) return i.command(d::EditCommand::redo, now);
    }
    if (e.document->composition()) return false; // the IME has these keys
    if (k.virtualKey == d::key::escape || k.virtualKey == d::key::enter || k.virtualKey == d::key::tab) {
        auto* field = e.field;
        if (field->handleEditingKey(k)) return true;
        if (!i.editing() || k.commandModifiers()) return false;
        // Multi-line: Return (Shift-Return with returnCommits) inserts a line;
        // Tab inserts a tab like the text view.
        if (k.virtualKey == d::key::escape || !e.request.multiline) return false;
        (void)e.editor->character(k.virtualKey == d::key::tab ? U'\t' : U'\n');
        i.changed(now);
        return true;
    }
    std::optional<ProjectedEditorCommand> command;
    switch (k.virtualKey) {
    case d::key::left: command = ProjectedEditorCommand::left; break;
    case d::key::right: command = ProjectedEditorCommand::right; break;
    case d::key::up: command = e.request.multiline ? ProjectedEditorCommand::up : ProjectedEditorCommand::documentStart; break;
    case d::key::down: command = e.request.multiline ? ProjectedEditorCommand::down : ProjectedEditorCommand::documentEnd; break;
    case d::key::home: command = ProjectedEditorCommand::documentStart; break;
    case d::key::end: command = ProjectedEditorCommand::documentEnd; break;
    case d::key::back: command = ProjectedEditorCommand::backspace; break;
    case d::key::deleteForward: command = ProjectedEditorCommand::deleteForward; break;
    default: break;
    }
    if (!command || k.alt || k.win) return false;
    if (e.secret && (*command == ProjectedEditorCommand::backspace || *command == ProjectedEditorCommand::deleteForward)) {
        // The secret loses what the document loses: the selection, else the
        // character before / after the caret.
        auto range = i.selected();
        if (range.start == range.end) {
            if (*command == ProjectedEditorCommand::backspace) { if (!range.start) return true; --range.start; }
            else { if (range.end >= e.secret->size()) return true; ++range.end; }
        }
        (void)i.replaceSecure(range, {});
        i.changed(now);
        return true;
    }
    (void)e.editor->command(*command, k.shift);
    i.changed(now);
    return true;
}
bool NativeDesignTextFields::character(std::uint32_t value, bool unicodeScalar) {
    auto& i = *impl_;
    if (!i.editing()) return false;
    // Control characters arrive as WM_CHAR after their keys (Backspace,
    // Return, Tab, Ctrl+letters): the keys already acted.
    if (value < 0x20 || value == 0x7f) return true;
    if (auto& e = *i.active; e.secret) {
        char32_t scalar = value;
        if (!unicodeScalar && value >= 0xd800 && value <= 0xdbff) { e.high = char16_t(value); return true; }
        if (!unicodeScalar && value >= 0xdc00 && value <= 0xdfff) { if (!e.high) return true; scalar = 0x10000 + ((char32_t(*e.high) - 0xd800) << 10) + (value - 0xdc00); }
        e.high.reset();
        const std::u32string one(1, scalar);
        if (i.replaceSecure(i.selected(), one).changed) i.changed(i.pose.time);
        return true;
    }
    const auto result = i.active->editor->character(value, unicodeScalar);
    if (result.changed) i.changed(i.pose.time);
    return result.handled;
}
bool NativeDesignTextFields::message(std::uint32_t message, std::uintptr_t wParam, std::intptr_t) {
    auto& i = *impl_;
    if (message != i.options.ownerMessage || !i.active || wParam != i.active->generation) return false;
    auto& e = *i.active;
    if (e.editor) (void)e.editor->takeChanges(UINT_PTR(wParam));
    if (e.stopping) { e.stopping = false; i.retire(); return true; }
    i.changed(i.pose.time);
    return true;
}
bool NativeDesignTextFields::pointerDown(core::Point clientPhysical, bool extend) {
    auto& i = *impl_;
    if (!i.insidePhysical(clientPhysical)) return false;
    (void)i.active->editor->pointerDown(clientPhysical, extend);
    i.changed(i.pose.time);
    return true;
}
void NativeDesignTextFields::pointerDrag(core::Point clientPhysical) {
    auto& i = *impl_;
    if (!i.editing()) return;
    if (i.active->editor->pointerDrag(clientPhysical).changed) i.changed(i.pose.time);
}
void NativeDesignTextFields::pointerUp() { auto& i = *impl_; if (i.editing()) i.active->editor->pointerUp(); }
// Right-click (LIB-90): the caret moves where the click landed unless it is
// inside the selection, then the edit menu opens at the pointer.
bool NativeDesignTextFields::rightPointerDown(core::Point clientPhysical, core::Point clientLogical) {
    auto& i = *impl_;
    if (!i.insidePhysical(clientPhysical)) return false;
    auto& e = *i.active;
    const auto now = i.pose.time;
    if (!e.document->composition()) {
        const auto range = i.selected();
        const auto at = core::text::projectedInsertion(*e.document, e.editor->layout(), clientPhysical, e.editor->placement());
        if (at && !(range.start != range.end && *at >= range.start && *at <= range.end)) {
            (void)e.editor->setSelectionFromHost({{*at, *at}});
            i.changed(now);
        }
    }
    if (!i.editing() || !i.options.editMenu) return true;
    const auto range = i.selected();
    const bool any = range.start != range.end, plain = !i.secure();
    bool canPaste{};
    if (i.options.clipboard.read) { const auto value = i.options.clipboard.read(); canPaste = value && !value->empty(); }
    const auto chosen = i.options.editMenu(clientLogical, any && plain, any && plain, canPaste);
    if (chosen && i.editing() && *chosen != d::EditCommand::undo && *chosen != d::EditCommand::redo) (void)i.command(*chosen, now);
    return true;
}
bool NativeDesignTextFields::textCursorAt(core::Point clientLogical) const noexcept { return impl_->inside(clientLogical); }
bool NativeDesignTextFields::wheel(core::Point clientPhysical, double delta) {
    auto& i = *impl_;
    if (!std::isfinite(delta) || !i.insidePhysical(clientPhysical) || !i.active->request.multiline) return false;
    (void)i.active->editor->scrollBy(delta);
    i.changed(i.pose.time);
    return true;
}
void NativeDesignTextFields::setFocused(bool value) {
    auto& i = *impl_;
    if (i.focused == value) return;
    i.focused = value;
    if (!i.editing() || !i.options.threadManager) return;
    const auto hr = i.active->editor->focus(value);
    need(SUCCEEDED(hr) || locked(hr), "Cannot update the design field focus");
    if (value) i.blink.restart(i.pose.time);
}
bool NativeDesignTextFields::editing() const noexcept { return impl_->editing(); }
std::optional<d::AccessibilityItem> NativeDesignTextFields::accessibilityItem() const {
    const auto& i = *impl_;
    if (!i.editing() || i.active->field->label.empty()) return std::nullopt;
    d::AccessibilityItem item;
    item.id = i.active->field->id(); item.label = i.active->field->label; item.role = d::AccessibilityRole::edit;
    item.rect = i.active->request.frame; item.focused = i.focused; item.enabled = true;
    if (!i.active->secret) item.value = i.content(*i.active);
    return item;
}
const d::TextField* NativeDesignTextFields::field() const noexcept { return impl_->editing() ? impl_->active->field : nullptr; }
void NativeDesignTextFields::setPose(const NativeDesignTextFieldPose& pose) {
    auto& i = *impl_;
    need(pose.boxToScreen.finite() && pose.screenToClip.finite() && pose.pixelWidth && pose.pixelHeight && std::isfinite(pose.opacity) && pose.opacity >= 0 && pose.opacity <= 1 && std::isfinite(pose.time),
        "Invalid design field pose");
    // The caret shows only in a focused field of the foreground HUD; it is
    // solid again when it reappears (LIB-87).
    const bool shown = i.editing() && pose.windowForeground && i.focused;
    if (shown && !i.shown) i.blink.restart(pose.time);
    i.shown = shown;
    i.pose = pose;
    i.posed = true;
    if (i.editing()) i.applyPose();
}
bool NativeDesignTextFields::requiresFrames(double now) const {
    const auto& i = *impl_;
    if (!i.editing()) return false;
    return (i.shown && i.blink.ramping(now, i.pose.reduceMotion)) || (i.active->request.multiline && i.indicator.animating(now));
}
std::optional<double> NativeDesignTextFields::nextWakeTime(double now) const {
    const auto& i = *impl_;
    if (!i.editing()) return std::nullopt;
    std::optional<double> next;
    if (i.shown) next = i.blink.nextChange(now, i.pose.reduceMotion);
    if (i.active->request.multiline) if (const auto hide = i.indicator.nextDeadline()) next = next ? std::min(*next, *hide) : *hide;
    return next;
}
bool NativeDesignTextFields::upload(Renderer& r) {
    auto& i = *impl_;
    if (i.active && i.active->request.multiline) (void)i.indicator.advance(i.pose.time, i.pose.reduceMotion);
    // The composition uploads the published scenes; only the entry set is ours.
    const auto before = i.composed.size();
    const auto* first = before ? i.composed[0].scene : nullptr;
    i.composed.clear();
    if (i.active && i.active->editor && i.posed) {
        i.composed.push_back({&i.active->glyphs, {}});
        if (i.active->request.multiline && i.active->chrome.draws().size() == 1) i.composed.push_back({&i.active->chrome, {}});
        i.active->published = true;
    }
    (void)r;
    return before != i.composed.size() || (before && first != i.composed[0].scene);
}
std::span<const LayerCompositionEntry> NativeDesignTextFields::entries() const noexcept { return impl_->composed; }
bool NativeDesignTextFields::collectRetired(Renderer& r) {
    auto& i = *impl_;
    for (auto& e : i.retired) if (!e->glyphs.releaseResources(r) || !e->chrome.releaseResources(r)) return false;
    i.retired.clear();
    return true;
}
bool NativeDesignTextFields::releaseResources(Renderer& r) {
    auto& i = *impl_;
    bool ok = collectRetired(r);
    if (i.active) ok = i.active->glyphs.releaseResources(r) && i.active->chrome.releaseResources(r) && ok;
    return ok;
}

DesignEditMenu nativeEditMenu(void* window, std::function<core::Language()> language) {
    return [window, language = std::move(language)](core::Point client, bool canCut, bool canCopy, bool canPaste) -> std::optional<d::EditCommand> {
        const auto hwnd = static_cast<HWND>(window);
        if (!hwnd || !IsWindow(hwnd)) return std::nullopt;
        const auto lang = language ? language() : core::Language::english;
        HMENU menu = CreatePopupMenu();
        if (!menu) return std::nullopt;
        const std::array<std::tuple<UINT, const char*, const char*, bool>, 4> items{{{1, "Cut", "剪切", canCut}, {2, "Copy", "复制", canCopy}, {3, "Paste", "粘贴", canPaste}, {4, "Select All", "全选", true}}};
        for (const auto& [id, english, chinese, enabled] : items) {
            const auto title = utf16(core::localized(english, chinese, lang));
            AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, reinterpret_cast<const wchar_t*>(title.c_str()));
        }
        const double scale = double(GetDpiForWindow(hwnd)) / 96;
        POINT at{LONG(std::lround(client.x * scale)), LONG(std::lround(client.y * scale))};
        ClientToScreen(hwnd, &at);
        const auto chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, hwnd, nullptr);
        DestroyMenu(menu);
        switch (chosen) {
        case 1: return d::EditCommand::cut;
        case 2: return d::EditCommand::copy;
        case 3: return d::EditCommand::paste;
        case 4: return d::EditCommand::selectAll;
        default: return std::nullopt;
        }
    };
}
} // namespace endfield::native
#endif
