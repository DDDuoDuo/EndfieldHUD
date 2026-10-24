#include "modules/archive_body_area.hpp"
#include "modules/archive_rich_text.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
using design::Point;
using design::Rect;
namespace {
std::string utf8(std::u16string_view text) { return archiveUTF8(text); }
}

// init (:1552-1574): the layer's #1C1C1C fill and ringField border, clipped,
// with the placeholder; the editor's own appearance is set by the host.
ArchiveBodyArea::ArchiveBodyArea(d::Builder& b, std::string id, std::string label, std::string placeholder, std::uint32_t capacity)
    : Control(b.env, id, std::move(label), b.layer(id)),
      document_(std::make_unique<core::notes::RichDocument>(std::u16string{}, std::nullopt, capacity)), capacity_(capacity),
      frame_(&b.layer(id + ".frame")),
      placeholder_(&d::makeText(b, id + ".placeholder", std::move(placeholder),
          d::TextStyle{placeholderSize, d::FontWeight::regular, b.env.palette.token(d::ThemeToken::muted)}, {})),
      track_(&b.layer(id + ".indicator")), thumb_(&b.layer(id + ".indicator.thumb")),
      bar_(std::make_unique<d::ScrollBarDriver>(b.env, *track_, b.layers.token())) {
    auto& l = layer();
    l.setMasksToBounds(true);
    l.add(*frame_);
    l.add(*placeholder_);
    // The indicator is its own part above the host's glyph insertion (the Mac
    // adds it to the artwork after the glyphs).
    thumb_->setAnimatable();
    track_->add(*thumb_); track_->setPartRoot(true); track_->setHidden(true);
    l.add(*track_);
}
ArchiveBodyArea::~ArchiveBodyArea() { dispose(); }

void ArchiveBodyArea::load(std::u16string text, core::notes::RichText runs, const core::notes::TextStyle& typing) {
    dispose();
    auto rich = runs.runs.empty() ? std::nullopt : std::optional(std::move(runs));
    document_ = std::make_unique<core::notes::RichDocument>(std::move(text), std::move(rich), capacity_);
    document_->setSelection({});
    document_->restoreTypingStyle(typing);
    textChanged();
}

// setSize (:1597-1608): the frame and placeholder follow; an attached
// editor is re-projected (the Mac disposes and attaches it again, keeping
// the first responder).
// Also every page build: the colours follow the palette (the area outlives
// in-place re-renders of its document).
void ArchiveBodyArea::setSize(d::Size size) {
    auto& l = layer();
    const bool moved = l.bounds() != size;
    l.setBounds(size);
    frame_->setFrame({0, 0, size.width, size.height});
    if (framed_) d::setBorder(*frame_, env().palette.tone(0x1C1C1C), env().palette.token(d::ThemeToken::ringField));
    else frame_->clearContent();
    const auto text = textRect();
    d::setLine(*placeholder_, {text.x, text.y, text.width, placeholderSize * archive_rich_text::lineHeightMultiple});
    d::editText(*placeholder_, [&](d::TextBlock& block) { block.setColor(env().palette.token(d::ThemeToken::muted)); });
    setTarget({0, 0, size.width, size.height});
    placeIndicator();
    if (moved && host_ && host_->attached(*this)) host_->layoutChanged(*this);
}
// The Notes centre's note box draws its own frame (NotesCanvas.swift:539:
// the area's borderWidth 0 and no background).
void ArchiveBodyArea::setFramed(bool framed) {
    if (framed == framed_) return;
    framed_ = framed;
    if (framed_) d::setBorder(*frame_, env().palette.tone(0x1C1C1C), env().palette.token(d::ThemeToken::ringField));
    else frame_->clearContent();
}
Rect ArchiveBodyArea::textRect() const noexcept {
    const auto b = layer().bounds();
    return {inset.width, inset.height, std::max(0.0, b.width - 2 * inset.width), std::max(0.0, b.height - 2 * inset.height)};
}
bool ArchiveBodyArea::attach() { return host_ && host_->attach(*this); }
void ArchiveBodyArea::suspend() { if (host_) { blur(); host_->suspend(*this); } }
void ArchiveBodyArea::dispose() noexcept {
    if (host_) host_->dispose(*this);
    // dispose (:101-103): scrollBar.reset(); the next editor reports again.
    bar_->reset(); scrollOffset_ = overflow_ = -1; track_->setHidden(true);
}
void ArchiveBodyArea::editorScrolled(double offset, double overflow) {
    if (!std::isfinite(offset) || !std::isfinite(overflow) || (offset == scrollOffset_ && overflow == overflow_)) return;
    const bool scrolled = scrollOffset_ >= 0 && offset != scrollOffset_;
    scrollOffset_ = offset; overflow_ = overflow;
    placeIndicator();
    if (scrolled) bar_->noteScroll();                           // boundsDidChange -> noteScroll (:90-93)
}
void ArchiveBodyArea::placeIndicator() {
    const auto text = textRect();
    const bool shown = overflow_ > 0 && text.height > 0;
    if (track_->hidden() != !shown) track_->setHidden(!shown);
    if (!shown) return;
    track_->setFrame({std::max(0.0, text.x + text.width - 4), text.y, 3, text.height});
    const double thumb = std::max(16.0, text.height * text.height / std::max(1.0, text.height + overflow_));
    d::placeThumb(*thumb_, 3, (text.height - thumb) * std::clamp(scrollOffset_, 0.0, overflow_) / overflow_, thumb, {.5, .5, .5, .65});
}
void ArchiveBodyArea::focus(std::optional<core::text::Range> selection) {
    if (!attach()) return;
    const auto length = std::uint32_t(document_->text().size());
    const auto start = std::min(selection ? selection->start : length, length);
    const auto end = std::min(selection ? std::max(selection->end, start) : start, length);
    host_->focus(*this, {start, end});
}
void ArchiveBodyArea::blur() { if (host_ && host_->focused(*this)) host_->blur(*this); }
void ArchiveBodyArea::scroll(double delta) { if (host_ && host_->attached(*this)) host_->scroll(*this, delta); }
bool ArchiveBodyArea::applyFormat(const core::notes::FormatChange& change, core::text::Range range, bool keepSelection) {
    return host_ && host_->attached(*this) ? host_->applyFormat(*this, change, range, keepSelection) : false;
}
bool ArchiveBodyArea::replace(core::text::Range range, std::u16string_view text) {
    return host_ && host_->attached(*this) && host_->replace(*this, range, text);
}
void ArchiveBodyArea::textChanged() { placeholder_->setHidden(!document_->text().empty()); }
void ArchiveBodyArea::editorFocusChanged(bool focused) { if (onFocusChange) onFocusChange(focused); }
void ArchiveBodyArea::editorChanged() { if (onChange) onChange(); }
void ArchiveBodyArea::editorSelectionChanged() { if (onSelectionChange) onSelectionChange(); }
void ArchiveBodyArea::editorEndedEditing() { if (onEndEditing) onEndEditing(); }
void ArchiveBodyArea::activate() { if (enabled()) focus(std::nullopt); }
void ArchiveBodyArea::accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, d::AccessibilityRole::edit);
    item.value = utf8(document_->text());
    item.focused = isFocused();
    out.push_back(std::move(item));
}
} // namespace endfield::modules
