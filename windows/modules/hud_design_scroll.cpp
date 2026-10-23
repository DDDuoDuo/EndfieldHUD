// HUDScrollArea (Sources/HUDDesignBadges.swift:373-458) and the idle-hiding
// bar of HUDScrollBarVisibility (:305-368, LIB-40) bound to a layer.
//
// The visibility state is the shared ScrollBarVisibility (hud_design_motion.hpp);
// ScrollBarDriver turns its transitions into the bar layer's opacity animation
// exactly as setShown does on the Mac and arms the one delayed hide on the
// context's deadlines (the Mac's DispatchWorkItem), tied to the owning arena's
// token so a rebuilt page or a closed menu never runs a stale hide.
//
// The offset is the content layer's model position (frame origin y = -offset,
// as on the Mac), so hit testing through the model tree sees the scrolled
// rows; the content layer is animatable, so moving it is a pose number and
// never re-rasterizes a row. The .2 s glide (a position.y animation on the
// Mac) is an additive translateY track from the on-screen offset to 0.
#include "modules/hud_design_scroll.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::design {
namespace {
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
}

ScrollBarDriver::ScrollBarDriver(Environment& env, Layer& bar, Deadlines::Token token) : env_(&env), bar_(&bar), token_(token) {
    bar.setAnimatable();
    bar.setValue(Channel::opacity, 0);                   // init: hidden at rest (:314-317)
}
void ScrollBarDriver::noteScroll() { const bool was = state_.shown(); state_.noteScroll(env_->now, env_->reduceMotion); sync(was); }
void ScrollBarDriver::setHovered(bool v) { const bool was = state_.shown(); state_.setHovered(v, env_->now, env_->reduceMotion); sync(was); }
void ScrollBarDriver::setDragging(bool v) { const bool was = state_.shown(); state_.setDragging(v, env_->now, env_->reduceMotion); sync(was); }
// reset (:339-346): the opacity snaps to 0 (the running fade is removed).
void ScrollBarDriver::reset() { state_.reset(); armed_.reset(); bar_->setValue(Channel::opacity, 0); }
// setShown (:362-367) and scheduleHide (:348-360).
void ScrollBarDriver::sync(bool wasShown) {
    const bool shown = state_.shown();
    if (shown != wasShown)
        motion::animate(*bar_, Channel::opacity, shown ? 1 : 0, shown ? ScrollBarVisibility::fadeIn : ScrollBarVisibility::fadeOut,
            env_->clock(), shown ? easing::easeOut : easing::easeIn);
    const auto next = state_.nextDeadline();
    if (!next || armed_ == next) return;
    armed_ = next;
    env_->deadlines.after(*next, token_, [this] {
        armed_.reset();
        const bool was = state_.shown();
        state_.advance(env_->now, env_->reduceMotion);
        sync(was);
    });
}

void placeThumb(Layer& thumb, double width, double top, double length, Color fill) {
    constexpr double raster = 18;
    if (thumb.bounds() != Size{width, raster}) {
        thumb.setAnchorPoint({.5, 0});
        thumb.setBounds({width, raster});
        setBackground(thumb, fill);
    }
    thumb.setPosition({width / 2, top});
    thumb.setValue(Channel::scaleY, length / raster);
}
double thumbLength(const Layer& thumb) noexcept { return thumb.bounds().height * thumb.value(Channel::scaleY); }

ScrollArea::ScrollArea(Builder& b, std::string_view id, Rect frame)
    : env_(&b.env), id_(id), layer_(&b.layer(id)), content_(&b.layer(sub(id, "scroll.content"))),
      track_(&b.layer(sub(id, "scroll.track"))), thumb_(&b.layer(sub(id, "scroll.thumb"))), bar_(b.env, *track_, b.layers.token()) {
    layer_->setFrame(frame);
    layer_->setMasksToBounds(true);
    content_->setAnimatable(); thumb_->setAnimatable();
    layer_->add(*content_); layer_->add(*track_); track_->add(*thumb_);
    // The bar's strip: a hover-only container target (never a press, never the
    // hover of the controls under it).
    strip_ = &layer_->addTarget(stripRect(), std::nullopt, true, [this](bool hovered, bool) { bar_.setHovered(hovered); });
    setOffset(0);
}
ScrollArea::~ScrollArea() = default;
Rect ScrollArea::stripRect() const noexcept {
    const auto size = layer_->bounds();
    return {size.width - stripWidth, 0, stripWidth, size.height};
}
double ScrollArea::maximumOffset() const noexcept { return std::max(0.0, contentHeight_ - layer_->bounds().height); }
double ScrollArea::contentWidth() const noexcept { return layer_->bounds().width - (maximumOffset() > 0 ? 10 : 0); }
void ScrollArea::setContentHeight(double height) { contentHeight_ = height; setOffset(offset_); }
// setOffset (:414-427).
void ScrollArea::setOffset(double value) {
    offset_ = std::min(maximumOffset(), std::max(0.0, value));
    const auto size = layer_->bounds();
    const bool scrollable = maximumOffset() > 0;
    content_->setFrame({0, -offset_, size.width, std::max(contentHeight_, size.height)});
    if (track_->hidden() == scrollable) track_->setHidden(!scrollable);
    const Rect track{size.width - 4, 0, 4, size.height};
    if (track_->frame() != track) { track_->setFrame(track); setBackground(*track_, env_->palette.tone(0x222222)); }
    strip_->hidden = !scrollable;
    strip_->rect = stripRect();
    const double thumbHeight = std::max(18.0, size.height * size.height / std::max(1.0, contentHeight_));
    const double fraction = scrollable ? offset_ / maximumOffset() : 0;
    placeThumb(*thumb_, 4, fraction * (size.height - thumbHeight), thumbHeight, env_->palette.accentInk());
}
// setOffset(_:animated:env:) (:438-451): content and thumb glide from where they are on screen.
void ScrollArea::setOffset(double value, bool animated) {
    const auto clock = env_->clock();
    Layer* items[] = {content_, thumb_};
    double starts[2];
    for (int i = 0; i < 2; ++i) starts[i] = items[i]->frame().y + items[i]->presented(Channel::translateY, clock.now);
    const double before = offset_;
    setOffset(value);
    if (offset_ != before) bar_.noteScroll();
    for (int i = 0; i < 2; ++i) {
        Layer& item = *items[i];
        item.removeMotion(MotionSlot::effect);
        const double delta = starts[i] - item.frame().y;
        if (!animated || clock.reduceMotion || std::abs(delta) <= .01) continue;
        const std::array<double, 2> values{delta, 0}, times{0, 1};
        motion::keyframes(item, Channel::translateY, values, times, glide, clock, easing::standard, 1, true);
    }
}
bool ScrollArea::scroll(double delta) {
    const double before = offset_;
    setOffset(offset_ + delta);
    if (offset_ != before) bar_.noteScroll();
    return offset_ != before;
}
// reveal (:454-457).
void ScrollArea::reveal(Rect rect) {
    const double height = layer_->bounds().height;
    if (rect.y < offset_) setOffset(rect.y);
    else if (rect.y + rect.height > offset_ + height) setOffset(rect.y + rect.height - height);
}
void ScrollArea::noteScroll() { if (maximumOffset() > 0) bar_.noteScroll(); }
void ScrollArea::setDragging(bool dragging) { bar_.setDragging(dragging); }
void ScrollArea::resetBar() { bar_.reset(); }
Rect ScrollArea::viewport(double atOffset) const noexcept { const auto size = layer_->bounds(); return {0, atOffset, size.width, size.height}; }
} // namespace endfield::modules::design
