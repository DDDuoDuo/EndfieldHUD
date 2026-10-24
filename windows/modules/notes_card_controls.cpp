// The redesign controls of a desktop note card (see the header). Line
// references are Sources/NotesWorkspace.swift's (Mac 1.3.0).
#include "modules/notes_card_controls.hpp"
#include <numbers>

namespace endfield::modules {
namespace d = design;
namespace {
d::Color color(const NotesColor& c) noexcept { return {c[0], c[1], c[2], c[3]}; }
d::Rect rect(const core::Rect& r) noexcept { return {r.x, r.y, r.width, r.height}; }
d::Icon icon(NotesControlGlyph g) noexcept {
    switch (g) {
    case NotesControlGlyph::pin: return d::Icon::pin;
    case NotesControlGlyph::up: return d::Icon::left;     // the back chevron, turned to point up
    case NotesControlGlyph::down: return d::Icon::right;  // the forward chevron, turned to point down
    case NotesControlGlyph::play: return d::Icon::play;
    case NotesControlGlyph::pause: return d::Icon::pause;
    default: return d::Icon::x;
    }
}
NotesFormatToggle::Glyph toggleGlyph(NotesControlGlyph g) noexcept {
    switch (g) {
    case NotesControlGlyph::bold: return NotesFormatToggle::Glyph::bold;
    case NotesControlGlyph::check: return NotesFormatToggle::Glyph::check;
    case NotesControlGlyph::chip: return NotesFormatToggle::Glyph::chip;
    default: return NotesFormatToggle::Glyph::label;
    }
}
}

// One build's layers and controls (the Mac's node.controls / row.controls).
struct NotesCardControls::Build {
    d::LayerArena arena; d::ControlSet set; d::Builder builder;
    std::vector<d::Control*> controls;
    std::vector<d::Layer*> placed;                       // this build's layers under the kept ones
    Build(d::Environment& env, const std::string& prefix) : arena(prefix, &env.deadlines), builder{env, arena, set} {}
    void detach() noexcept { for (d::Layer* l : placed) l->removeFromParent(); }
};

NotesCardControls::NotesCardControls(d::Environment& env, std::string id)
    : env_(&env), id_(std::move(id)), arena_("notes.card." + id_, &env.deadlines) {
    root_ = &arena_.make("controls");
    root_->setAnchorPoint({0, 0});
    root_->setMasksToBounds(true);                       // card.masksToBounds (:1162)
    root_->setPartRoot(true);
    viewport_ = &arena_.make("viewport");                // notes.content.viewport
    viewport_->setMasksToBounds(true);
    thumb_ = &arena_.make("viewport.scrollThumb");       // notes.content.scrollThumb
    bar_ = std::make_unique<d::ScrollBarDriver>(env, *thumb_, arena_.token());
    root_->add(*viewport_);
    viewport_->add(*thumb_);
}
NotesCardControls::~NotesCardControls() { if (build_) build_->detach(); }

// render(_:) (:1158-1278) and makeChecklistControls (:1438-1463), settled.
void NotesCardControls::update(const NotesCardPresentation& card) {
    auto& env = *env_;
    const bool reduce = env.reduceMotion;
    env.reduceMotion = true;                             // BuildGate (:168-169)
    struct Restore { d::Environment& env; bool value; ~Restore() { env.reduceMotion = value; } } restore{env, reduce};
    const auto& palette = card.palette();
    const auto size = card.layers().front().bounds;
    root_->setFrame({0, 0, size.width, size.height});
    const auto viewport = rect(card.contentViewport());
    viewport_->setFrame(viewport);
    if (build_) build_->detach();
    auto next = std::make_unique<Build>(env, "notes.card." + id_ + ".build" + std::to_string(++builds_));
    auto& b = next->builder;
    // The title strip's caption: 9.5 bold muted, tracking .12, uppercase.
    const auto& title = card.title();
    d::Layer& caption = d::makeText(b, "title", title.text, d::TextStyle{9.5, d::FontWeight::bold, color(palette.muted), 1, .12, true}, rect(title.line));
    root_->insert(caption, 0);
    next->placed.push_back(&caption);
    next->controls.reserve(card.controls().size());
    for (const auto& spec : card.controls()) {
        const d::Rect frame = rect(spec.frame);
        d::Control* control{};
        if (spec.kind == NotesControlKind::iconButton) {
            auto& button = b.make<d::IconButton>(spec.id, spec.label, icon(spec.glyph), d::IconButton::Style::dark, false, false, frame.width, spec.iconSize);
            button.setOn(spec.on);
            // The back / forward chevrons turned +90 degrees (as in Settings > Modules).
            if (spec.glyph == NotesControlGlyph::up || spec.glyph == NotesControlGlyph::down) button.layer().setBaseTransform(d::Affine::rotation(std::numbers::pi / 2));
            control = &button;
        } else {
            auto& toggle = b.make<NotesFormatToggle>(spec.id, spec.label, toggleGlyph(spec.glyph), spec.on, d::Size{frame.width, frame.height}, spec.text);
            if (spec.chip) toggle.setChip(color(*spec.chip));
            control = &toggle;
        }
        control->setEnabled(spec.enabled);
        control->layer().setHidden(spec.hidden);
        // Rows: bounds and position (rotation about the centre); the rest: frame.
        const d::Rect placed = spec.inViewport ? d::Rect{frame.x - viewport.x, frame.y - viewport.y, frame.width, frame.height} : frame;
        control->layer().setFrame(placed);
        if (spec.inViewport) viewport_->insertBelow(control->layer(), *thumb_); else root_->add(control->layer());
        next->controls.push_back(control);
        next->placed.push_back(&control->layer());
    }
    // The previous build lives until the next one: the native scene holding
    // its emitted surfaces re-emits from the new tree first.
    previous_ = std::move(build_);
    build_ = std::move(next);
    // The thumb (theme yellow, square); hidden without overflow.
    const auto& thumb = card.thumb();
    scrollable_ = thumb.scrollable;
    thumb_->setHidden(!thumb.scrollable);
    if (thumb.scrollable) { thumb_->setFrame(rect(thumb.frame)); d::setBackground(*thumb_, color(palette.accent)); }
    // The highlight follows the content under a still pointer.
    if (pointer_) d::updateHighlights(*root_, pointer_, pressed_);
}
std::uint64_t NotesCardControls::revision() const noexcept {
    return arena_.revision() + (build_ ? build_->arena.revision() : 0) + (builds_ << 32);
}
void NotesCardControls::pointer(std::optional<d::Point> point, bool pressed) {
    pointer_ = point; pressed_ = pressed && point.has_value();
    d::updateHighlights(*root_, point, pressed_);
}
void NotesCardControls::noteScroll() { if (scrollable_) bar_->noteScroll(); }
void NotesCardControls::setScrollHovered(bool hovered) { bar_->setHovered(hovered); }
bool NotesCardControls::requiresFrames(double now) const {
    if (thumb_->animating(now)) return true;
    if (build_) for (const d::Control* control : build_->controls) {
        const auto busy = [&](const d::Layer& l, const auto& self) -> bool {
            if (l.animating(now)) return true;
            for (const d::Layer* child : l.children()) if (self(*child, self)) return true;
            return false;
        };
        if (busy(control->layer(), busy)) return true;
    }
    return false;
}
std::span<d::Control* const> NotesCardControls::controls() const noexcept {
    return build_ ? std::span<d::Control* const>(build_->controls) : std::span<d::Control* const>{};
}
const d::Control* NotesCardControls::control(std::string_view id) const noexcept {
    for (const d::Control* c : controls()) if (c->id() == id) return c;
    return nullptr;
}
} // namespace endfield::modules
