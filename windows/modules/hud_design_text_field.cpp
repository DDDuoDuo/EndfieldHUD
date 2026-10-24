// The redesign's text field, portable half: the port of
// Sources/HUDDesignTextField.swift (HUDDesignTextField :9-294). The native
// editor (TSF, IME, caret, selection, undo, the projected editor of
// HUDProjectedTextEditor) is behind TextEditorHost; the canvas keeps one
// editing field (willBeginEditing ends the others with commit) and refreshes
// input state and the parallax lock (editingStateChanged).
//
// Adaptations: the accessibility role of an idle field is `edit` (a value and
// SetFocus to begin editing, LIBRARY-DESIGN section 9) where the Mac exposes a
// button that begins editing; while editing the native editor is the element
// (nothing here, as on the Mac). A secure field exposes no value. A rich
// (Options.attributes) field's idle face needs the owner's document: this
// portable label draws its plain text top-aligned in the field style; the
// Notes canvas supplies the styled idle surface (Phase C4).
#include "modules/hud_design_text_field.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::design {
namespace {
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
bool inRect(Rect r, Point p) noexcept { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
const Layer& topmost(const Layer& layer) { const Layer* l = &layer; while (l->parent()) l = l->parent(); return *l; }
std::size_t utf16Length(std::string_view s) {
    std::size_t units = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) units += c >= 0xF0 ? 2 : 1;
    }
    return units;
}
}
// CharacterSet.newlines (LF, VT, FF, CR, NEL, LS, PS): each one becomes a space.
std::string joinedLines(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 0x0A && c <= 0x0D) { out += ' '; continue; }
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x85) { out += ' '; ++i; continue; }
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80
            && (static_cast<unsigned char>(s[i + 2]) == 0xA8 || static_cast<unsigned char>(s[i + 2]) == 0xA9)) { out += ' '; i += 2; continue; }
        out += char(c);
    }
    return out;
}

TextField::TextField(Builder& b, std::string id, std::string label, std::string text, std::string placeholder, Size size, Options options)
    : TextField(b, std::make_unique<FieldFrame>(b, id, size, options.light, options.plain), id, std::move(label), std::move(text),
          std::move(placeholder), std::move(options)) {}
TextField::TextField(Builder& b, std::unique_ptr<FieldFrame> frame, std::string id, std::string label, std::string text, std::string placeholder,
    Options options)
    : Control(b.env, std::move(id), std::move(label), frame->layer()), options_(std::move(options)), frame_(std::move(frame)),
      text_(std::move(text)), placeholder_(std::move(placeholder)) {
    if (options_.accent) frame_->focusColor = options_.accent;
    if (options_.background) { frame_->background = options_.background; frame_->setState(FieldFrame::State::normal); }
    Layer& l = layer();
    l.setAnimatable();                                   // the invalid shake
    idle_ = &makeText(b, sub(this->id(), "field.idle"), "", textStyle(), {});
    l.add(*idle_);
    l.setMasksToBounds(true);
    setTarget(localBounds(l));
    renderIdle();
}
TextField::~TextField() { if (editing_ && host_) host_->end(*this); }
// textRect (:84-88).
Rect TextField::textRect() const {
    const auto size = layer().bounds();
    if (options_.multiline) {
        const double dy = std::max(6.0, options_.inset.height);
        return {options_.inset.width, dy, size.width - 2 * options_.inset.width, size.height - 2 * dy};
    }
    return {options_.inset.width, 0, size.width - options_.inset.width * 2, size.height};
}
// textStyle (:90-92): the field style; light fields use the ink.
TextStyle TextField::textStyle() const {
    const TextStyle style = options_.style.value_or(type::field(palette()));
    return options_.light ? style.colored(fixed::ink) : style;
}
void TextField::setText(std::string value) {
    text_ = bounded(std::move(value));
    if (editing_ && host_ && !host_->hasMarkedText(*this) && host_->text(*this) != text_) host_->setText(*this, text_);
    renderIdle();
}
void TextField::setPlaceholder(std::string value) { placeholder_ = std::move(value); renderIdle(); }
void TextField::setSize(Size size) {
    const auto bounds = layer().bounds();
    if (bounds.width == size.width && bounds.height == size.height) return;
    frame_->setSize(size);
    setTarget(localBounds(layer()));
    renderIdle();
    if (editing_) refreshProjection();
}
// setInvalid (:100-108): red state; the shake is additive x 0,-6,6,-6,6,0 over .32 s.
void TextField::setInvalid(bool value, bool shake) {
    invalid_ = value;
    frame_->setState(value ? FieldFrame::State::invalid : (editing_ ? FieldFrame::State::focused : FieldFrame::State::normal));
    const auto c = clock();
    if (!shake || c.reduceMotion) return;
    const std::array<double, 6> values{0, -6, 6, -6, 6, 0}, times{0, .2, .4, .6, .8, 1};
    motion::keyframes(layer(), Channel::translateX, values, times, .32, c, easing::linear, 1, true);
}
std::string TextField::displayed() const {
    return options_.secure ? [&] { std::string bullets; for (std::size_t i = env().characters.count(text_); i > 0; --i) bullets += "•"; return bullets; }() : text_;
}
// renderIdle (:110-140).
void TextField::renderIdle() {
    const Rect rect = textRect();
    const TextStyle style = textStyle();
    const std::string shown = displayed();
    editText(*idle_, [&](TextBlock& block) {
        block.runs = shown.empty() ? std::vector<TextRun>{{placeholder_, style.colored(options_.placeholderColor.value_or(palette().token(ThemeToken::muted)))}}
                                   : std::vector<TextRun>{{shown, style}};
        const double lineHeight = style.size * std::max(1.2, style.lineHeight);
        block.alignment = options_.alignment;
        block.maximumLines = options_.multiline || options_.rich ? std::max(1, int(rect.height / (style.size * 1.5))) : 1;
        block.setLineFrame(options_.multiline || options_.rich ? rect : Rect{rect.x, (rect.height - lineHeight) / 2, rect.width, lineHeight});
    });
    if (idle_->hidden() != editing_) idle_->setHidden(editing_);
}
void TextField::activate() {
    if (!enabled()) return;
    beginEditing();
    if (onActivate) onActivate();
}
// beginEditing(selection:) (:151-211).
void TextField::beginEditing(std::optional<std::pair<std::uint32_t, std::uint32_t>> selection) {
    if (!enabled() || editing_) return;
    // The page may have been built while another owner's editors were bound:
    // the edit runs on the editors bound now (the owner that took the press).
    if (currentEditorHost) host_ = currentEditorHost();
    if (!host_) return;
    if (willBeginEditing) willBeginEditing(*this);      // the canvas ends any other editing field (commit)
    original_ = text_;
    const Layer& root = topmost(layer());
    TextEditRequest request;
    request.field = this;
    request.frame = convert(textRect(), layer(), root);
    request.style = textStyle();
    request.multiline = options_.multiline; request.secure = options_.secure; request.light = options_.light;
    request.alignment = options_.alignment;
    request.caretColor = options_.light ? fixed::ink : options_.accent.value_or(palette().accentInk());
    request.selectionColor = withAlpha(palette().yellow(), .35);
    request.text = text_;
    const auto length = std::uint32_t(utf16Length(text_));
    if (selection && selection->first <= length) request.restoredSelection = std::pair{selection->first, std::min(selection->second, length - selection->first)};
    else if (options_.selectsAllOnBegin) request.selectAll = true;
    else if (pressPoint_ && inRect(localBounds(layer()), *pressPoint_)) request.click = convert(*pressPoint_, layer(), root);
    if (!host_->begin(request)) return;
    editing_ = true;
    frame_->setState(invalid_ ? FieldFrame::State::invalid : FieldFrame::State::focused);
    renderIdle();
    if (editingStateChanged) editingStateChanged(*this);
    if (onBegin) onBegin();
}
void TextField::refreshProjection() {
    if (editing_ && host_) host_->reproject(*this, convert(textRect(), layer(), topmost(layer())));
}
std::optional<std::pair<std::uint32_t, std::uint32_t>> TextField::selectedRange() const {
    return editing_ && host_ ? host_->selection(*this) : std::nullopt;
}
// endEditing(commit:) (:216-228): the host's text includes any composition (unmarkText).
void TextField::endEditing(bool commit) {
    if (finishing_ || !editing_) return;
    finishing_ = true;
    text_ = commit && host_ ? bounded(host_->text(*this)) : original_;
    if (host_) host_->end(*this);
    editing_ = false;
    frame_->setState(invalid_ ? FieldFrame::State::invalid : FieldFrame::State::normal);
    renderIdle();
    if (editingStateChanged) editingStateChanged(*this);
    finishing_ = false;
    if (commit) { if (onCommit) onCommit(text_); }
    else if (onCancel) onCancel();
}
// bounded (:244-248).
std::string TextField::bounded(std::string value) const {
    std::string result = options_.multiline ? std::move(value) : joinedLines(value);
    const auto& rules = env().characters;
    if (options_.maximumLength && rules.count(result) > *options_.maximumLength) result = rules.prefix(result, *options_.maximumLength);
    return result;
}
// handleEditorKey (:250-263): never while IME marked text exists.
bool TextField::handleEditingKey(const KeyInput& input) {
    if (!editing_ || !host_ || host_->hasMarkedText(*this)) return false;
    switch (input.virtualKey) {
    case key::escape: endEditing(options_.escapeCommits); return true;
    case key::enter:
        if (!options_.multiline || (options_.returnCommits && !input.shift)) { endEditing(true); if (onReturn) onReturn(); return true; }
        return false;
    case key::tab:
        if (options_.multiline) return false;
        endEditing(true); if (onTab) onTab(input.shift); return true;
    default: return false;
    }
}
// textDidChange (:265-279).
void TextField::editorChanged(std::string value) {
    if (!editing_ || !host_) return;
    std::string reported = std::move(value);
    if (!host_->hasMarkedText(*this)) {
        std::string limited = bounded(reported);
        if (limited != reported) { host_->setText(*this, limited); reported = limited; }
        text_ = std::move(limited);
    }
    if (invalid_) setInvalid(false);
    if (onChange) onChange(reported);
}
// textDidEndEditing (:281-287): the host calls this once the focus has settled elsewhere.
void TextField::editorFocusLost() { if (editing_ && !finishing_) endEditing(true); }
bool TextField::pointerDown(Point) { return enabled() && !editing_; }
void TextField::pointerUp(Point p) { pressPoint_ = p; Control::pointerUp(p); pressPoint_.reset(); }
std::optional<AccessibilityRole> TextField::accessibilityRole() const { return editing_ ? std::nullopt : std::optional<AccessibilityRole>(AccessibilityRole::edit); }
void TextField::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (editing_ || label.empty()) return;
    auto item = accessibilityItem(root, AccessibilityRole::edit);
    if (!options_.secure) item.value = text_;
    out.push_back(std::move(item));
}
std::string TextField::idleText() const { return textOf(*idle_).text(); }
} // namespace endfield::modules::design
