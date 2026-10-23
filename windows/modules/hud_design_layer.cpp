// The retained design layer tree (CALayer + HUDLayer model, HUDMotion verbs of
// Sources/HUDDesignMotion.swift :42-183), its emission into Parts and the
// allocation-free pose sampler. Model geometry (hit tests, accessibility)
// never sees motion, as CALayer.convert uses the model tree.
#include "modules/hud_design_layer.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

// Swift never fuses multiply-adds; neither does this port, so its numbers
// stay bit-equal with the Mac oracle's.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules::design {
namespace {
// LayerScene/LayerRasterizer limits a part must respect (native/layer_scene.cpp
// validateIdentities: depth 64, LayerRasterizer::maximumNodes 4096 nodes).
constexpr std::size_t maximumDepth = 64, maximumNodes = 4096;
void need(bool v, const char* what) { if (!v) throw std::invalid_argument(what); }
void finite(double v, const char* what) { need(std::isfinite(v), what); }
std::uint64_t nextSerial() noexcept { static std::atomic<std::uint64_t> serial{}; return ++serial; }
Json point(Point p) { return Json::Array{p.x, p.y}; }
Json rectJson(Rect r) { return Json::Array{r.x, r.y, r.width, r.height}; }
// The CA transform of a layer (transform.translation, base, rotation.z, scale)
// and its full local -> parent matrix about the anchor point.
Affine caTransform(const Layer& l, double tx, double ty, double sx, double sy, double rotation) noexcept {
    return Affine::translation(tx, ty) * l.baseTransform() * Affine::rotation(rotation) * Affine::scale(sx, sy);
}
Affine placed(const Layer& l, const Affine& transform) noexcept {
    const auto b = l.bounds(); const auto a = l.anchorPoint(); const auto p = l.position();
    return Affine::translation(p.x, p.y) * transform * Affine::translation(-a.x * b.width, -a.y * b.height);
}
Affine modelLocal(const Layer& l) noexcept {
    return placed(l, caTransform(l, l.value(Channel::translateX), l.value(Channel::translateY), l.value(Channel::scaleX), l.value(Channel::scaleY), l.value(Channel::rotation)));
}
Affine presentedLocal(const Layer& l, double now) noexcept {
    return placed(l, caTransform(l, l.presented(Channel::translateX, now), l.presented(Channel::translateY, now), l.presented(Channel::scaleX, now),
        l.presented(Channel::scaleY, now), l.presented(Channel::rotation, now)));
}
bool identity(const Affine& a) noexcept { return a == Affine{}; }
Rect bounding(const Affine& m, Rect r) noexcept {
    const Point c[4]{m.apply({r.x, r.y}), m.apply({r.x + r.width, r.y}), m.apply({r.x, r.y + r.height}), m.apply({r.x + r.width, r.y + r.height})};
    double left = c[0].x, right = c[0].x, top = c[0].y, bottom = c[0].y;
    for (const auto& p : c) { left = std::min(left, p.x); right = std::max(right, p.x); top = std::min(top, p.y); bottom = std::max(bottom, p.y); }
    return {left, top, right - left, bottom - top};
}
Rect unite(std::optional<Rect> a, Rect b) noexcept {
    if (!a) return b;
    const double left = std::min(a->x, b.x), top = std::min(a->y, b.y);
    return {left, top, std::max(a->x + a->width, b.x + b.width) - left, std::max(a->y + a->height, b.y + b.height) - top};
}
Rect intersect(Rect a, Rect b) noexcept {
    const double left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    return {left, top, std::max(0.0, std::min(a.x + a.width, b.x + b.width) - left), std::max(0.0, std::min(a.y + a.height, b.y + b.height) - top)};
}
bool insideHalfOpen(Rect r, Point p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; } // CGRect.contains
std::vector<const Layer*> paintOrder(const Layer& l) {
    std::vector<const Layer*> out(l.children().begin(), l.children().end());
    std::stable_sort(out.begin(), out.end(), [](const Layer* a, const Layer* b) { return a->zPosition() < b->zPosition(); });
    return out;
}
// Content node ids become "<layer id>:<id>" so a part never repeats one.
void reid(Json& node, const std::string& prefix) {
    if (!node.isObject()) return;
    const auto old = node["id"].isString() ? node["id"].string() : std::string{};
    node["id"] = old.empty() ? prefix : prefix + ":" + old;
    if (node["children"].isArray()) {
        auto children = node["children"].array();
        for (auto& c : children) reid(c, prefix);
        node["children"] = std::move(children);
    }
    if (node["mask"].isObject()) { auto mask = node["mask"]; reid(mask, prefix); node["mask"] = std::move(mask); }
}
std::size_t countNodes(const Json& n, std::size_t depth) {
    need(depth <= maximumDepth, "Design part nesting exceeds the native scene's depth");
    std::size_t total = 1;
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) total += countNodes(c, depth + 1);
    if (n["mask"].isObject()) total += countNodes(n["mask"], depth + 1);
    return total;
}
Rect frameOf(const Json& n) {
    const auto& b = n["bounds"].array(); const auto& p = n["position"].array();
    return {p[0].number(), p[1].number(), b[2].number(), b[3].number()};
}
}

// ---------------------------------------------------------------------------
// Affine
// ---------------------------------------------------------------------------
Affine Affine::rotation(double r) noexcept { const double c = std::cos(r), s = std::sin(r); return {c, s, -s, c, 0, 0}; }
Point Affine::apply(Point p) const noexcept { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
std::optional<Affine> Affine::inverse() const noexcept {
    const double det = a * d - b * c;
    if (!(std::abs(det) > 1e-12) || !std::isfinite(det)) return std::nullopt;
    return Affine{d / det, -b / det, -c / det, a / det, (c * ty - d * tx) / det, (b * tx - a * ty) / det};
}
Affine operator*(const Affine& x, const Affine& y) noexcept {
    return {x.a * y.a + x.c * y.b, x.b * y.a + x.d * y.b, x.a * y.c + x.c * y.d, x.b * y.c + x.d * y.d,
        x.a * y.tx + x.c * y.ty + x.tx, x.b * y.tx + x.d * y.ty + x.ty};
}

// HUDControlHighlightLayer target: controlPath (target-local) or its bounds.
bool HitTarget::contains(Point p) const {
    const Point local{p.x - rect.x, p.y - rect.y};
    return path ? path->contains(local) : local.x >= 0 && local.y >= 0 && local.x <= rect.width && local.y <= rect.height;
}

// ---------------------------------------------------------------------------
// Layer
// ---------------------------------------------------------------------------
Layer::Layer(Key, LayerArena& arena, std::string id) : arena_(&arena), id_(std::move(id)), anchor_{.5, .5} {}
void Layer::touch() noexcept { ++arena_->revision_; }
Rect Layer::frame() const noexcept { return {position_.x - anchor_.x * bounds_.width, position_.y - anchor_.y * bounds_.height, bounds_.width, bounds_.height}; }
void Layer::setFrame(Rect r) {
    finite(r.x + r.y + r.width + r.height, "Layer frame must be finite");
    setBounds({r.width, r.height});
    setPosition({r.x + anchor_.x * r.width, r.y + anchor_.y * r.height});
}
void Layer::setBounds(Size s) {
    need(std::isfinite(s.width) && std::isfinite(s.height) && s.width >= 0 && s.height >= 0, "Layer bounds must be finite and non-negative");
    if (s == bounds_) return;
    bounds_ = s; touch();
}
// Placement of an animatable layer is pose-only (its surfaces are authored in
// its own space); a static layer's placement is baked into its part.
void Layer::setPosition(Point p) {
    finite(p.x + p.y, "Layer position must be finite");
    if (p == position_) return;
    position_ = p; if (!animatable()) touch();
}
void Layer::setAnchorPoint(Point p) {
    finite(p.x + p.y, "Layer anchor must be finite");
    if (p == anchor_) return;
    anchor_ = p; if (!animatable()) touch();
}
void Layer::setOrigin(Point p) { setFrame({p.x, p.y, bounds_.width, bounds_.height}); }
void Layer::setBaseTransform(const Affine& t) {
    finite(t.a + t.b + t.c + t.d + t.tx + t.ty, "Layer transform must be finite");
    if (t == base_) return;
    base_ = t; if (!animatable()) touch();
}
void Layer::setZPosition(double z) { finite(z, "Layer zPosition must be finite"); if (z == z_) return; z_ = z; touch(); }
void Layer::setHidden(bool v) { if (v == hidden_) return; hidden_ = v; if (!animatable()) touch(); }
void Layer::setMasksToBounds(bool v, double radius) {
    need(std::isfinite(radius) && radius >= 0, "Clip radius must be finite and non-negative");
    if (v == clips_ && radius == clipRadius_) return;
    clips_ = v; clipRadius_ = radius; touch();
}
double Layer::value(Channel c) const noexcept { return model_[std::size_t(c)]; }
void Layer::setValue(Channel c, double v) {
    finite(v, "Layer channel value must be finite");
    property[std::size_t(c)] = {};
    if (model_[std::size_t(c)] == v) return;
    model_[std::size_t(c)] = v; if (!animatable()) touch();
}
// A tint colours the white-authored surfaces this layer places; only an
// animatable layer places surfaces of its own.
void Layer::setTint(std::optional<Color> c) {
    if (c && !animatable()) throw std::logic_error("A design tint needs an animatable layer: " + id_);
    tintTrack = {};
    tint_ = c;
}
void Layer::setContent(Json node) {
    need(node.isObject() && node["bounds"].isArray() && node["position"].isArray(), "Layer content must be one framed node");
    content_ = std::move(node); text_.reset(); ++contentRevision_; touch();
}
void Layer::setText(TextBlock block) {
    const auto frame = block.layerFrame();
    text_ = std::make_unique<TextBlock>(std::move(block)); content_.reset(); ++contentRevision_; touch();
    setFrame(frame);
}
TextBlock* Layer::text() noexcept { if (text_) { ++contentRevision_; touch(); } return text_.get(); }
const TextBlock* Layer::text() const noexcept { return text_.get(); }
void Layer::clearContent() { if (!content_ && !text_) return; content_.reset(); text_.reset(); ++contentRevision_; touch(); }

void Layer::add(Layer& child) { insert(child, std::size_t(-1)); }
void Layer::insert(Layer& child, std::size_t index) {
    if (&child == this || isDescendant(child)) throw std::logic_error("A layer cannot contain itself: " + child.id_);
    child.removeFromParent();
    child.parent_ = this;
    children_.insert(children_.begin() + std::ptrdiff_t(std::min(index, children_.size())), &child);
    touch(); if (child.arena_ != arena_) child.touch();
}
void Layer::insertBelow(Layer& child, const Layer& sibling) {
    if (&child == &sibling) return;
    need(sibling.parent_ == this, "insertBelow needs a sibling in this layer");
    if (child.parent_ == this) { std::erase(children_, &child); child.parent_ = nullptr; }
    const auto at = std::size_t(std::find(children_.begin(), children_.end(), &sibling) - children_.begin());
    insert(child, at);
}
void Layer::removeFromParent() noexcept {
    if (!parent_) return;
    std::erase(parent_->children_, this);
    parent_->touch(); if (parent_->arena_ != arena_) touch();
    parent_ = nullptr;
}
bool Layer::isDescendant(const Layer& ancestor) const noexcept {
    for (auto* l = parent_; l; l = l->parent_) if (l == &ancestor) return true;
    return false;
}
void Layer::setAnimatable(SurfaceRole role) { if (role == role_) return; role_ = role; touch(); }
void Layer::setPartRoot(bool v) { if (v == partRoot_) return; partRoot_ = v; touch(); }
HitTarget& Layer::addTarget(Rect r, std::optional<PathBuilder> path, bool container, std::function<void(bool, bool)> onChange) {
    finite(r.x + r.y + r.width + r.height, "Hit target must be finite");
    auto target = std::make_unique<HitTarget>();
    target->rect = r; target->path = std::move(path); target->container = container; target->onChange = std::move(onChange);
    targets_.push_back(std::move(target));
    return *targets_.back();
}

// Core Animation's order: the model, replaced by the last-added active
// absolute track, plus the active additive tracks added after it.
double Layer::presented(Channel c, double now) const noexcept {
    const Track* absolute{};
    const auto consider = [&](const Track& t) { if (!t.additive && t.active(now) && (!absolute || t.serial > absolute->serial)) absolute = &t; };
    consider(property[std::size_t(c)]);
    for (std::size_t s = 0; s < slots.size(); ++s)
        for (std::size_t i = 0; i < slots[s].size(); ++i) if (slotChannels[s][i] == c) consider(slots[s][i]);
    double v = absolute ? absolute->sample(now) : model_[std::size_t(c)];
    const std::uint64_t after = absolute ? absolute->serial : 0;
    const auto add = [&](const Track& t) { if (t.additive && t.serial > after && t.active(now)) v += t.sample(now); };
    add(property[std::size_t(c)]);
    for (std::size_t s = 0; s < slots.size(); ++s)
        for (std::size_t i = 0; i < slots[s].size(); ++i) if (slotChannels[s][i] == c) add(slots[s][i]);
    return v;
}
std::optional<Color> Layer::presentedTint(double now) const noexcept {
    const ColorTrack* t{};
    if (tintTrack.active(now)) t = &tintTrack;
    if (tintEffect.active(now) && (!t || tintEffect.serial > t->serial)) t = &tintEffect;
    return t ? std::optional(t->sample(now)) : tint_;
}
bool Layer::animating(double now) const noexcept {
    for (const auto& t : property) if (t.running(now)) return true;
    for (const auto& slot : slots) for (const auto& t : slot) if (t.running(now)) return true;
    return tintTrack.running(now) || tintEffect.running(now);
}
void Layer::removeMotion(MotionSlot s) noexcept { slots[std::size_t(s)] = {}; }
void Layer::removeAllMotion() noexcept { property = {}; slots = {}; tintTrack = {}; tintEffect = {}; }

// ---------------------------------------------------------------------------
// LayerArena
// ---------------------------------------------------------------------------
LayerArena::LayerArena(std::string prefix, Deadlines* deadlines) : prefix_(std::move(prefix)), deadlines_(deadlines), token_(deadlines ? deadlines->token() : 0) {}
// Links into other arenas are cut (the bubble in a canvas's top layer, a
// foreign layer hosted here) and this arena's pending completions cancelled.
LayerArena::~LayerArena() {
    for (auto& l : layers_) {
        if (l.parent_ && l.parent_->arena_ != this) l.removeFromParent();
        for (auto* c : l.children_) if (c->arena_ != this) { c->parent_ = nullptr; c->touch(); }
    }
    if (deadlines_) deadlines_->cancel(token_);
}
Layer& LayerArena::make(std::string_view id) {
    need(!id.empty(), "A design layer needs an id");
    auto full = prefix_.empty() ? std::string(id) : prefix_ + "." + std::string(id);
    if (!ids_.insert(full).second) throw std::invalid_argument("Repeated design layer id: " + full);
    ++revision_;
    return layers_.emplace_back(Layer::Key{}, *this, std::move(full));
}

// ---------------------------------------------------------------------------
// HUDMotion verbs
// ---------------------------------------------------------------------------
namespace motion {
namespace {
void requireAnimatable(const Layer& l) { if (!l.animatable()) throw std::logic_error("Animating a static design layer (mark it setAnimatable at build time): " + l.id()); }
void requireTime(const Clock& c) { finite(c.now, "Motion needs a finite clock"); }
Track& entry(Layer& l, MotionSlot s, std::size_t i, Channel c) { l.slotChannels[std::size_t(s)][i] = c; return l.slots[std::size_t(s)][i]; }
Track timed(Track t, double begin, double delay) { t.start = begin + delay; t.fillBackwards = delay > 0; t.serial = nextSerial(); return t; }
}
// HUDMotion.animate (:42-55).
void animate(Layer& l, Channel c, double to, double duration, const Clock& clock, core::CubicTiming timing, double delay) {
    requireAnimatable(l); requireTime(clock); finite(to, "Animated value must be finite");
    const double from = l.presented(c, clock.now);
    l.setValue(c, to); // also removes the channel's running "hud.<keyPath>" track
    if (clock.reduceMotion || !(duration > 0) || from == to) return;
    l.property[std::size_t(c)] = timed(Track::basic(from, to, 0, duration, timing), clock.now, delay);
}
void animateTint(Layer& l, Color to, double duration, const Clock& clock, core::CubicTiming timing, double delay) {
    requireAnimatable(l); requireTime(clock);
    const Color from = l.presentedTint(clock.now).value_or(white(1));
    l.setTint(to);
    if (clock.reduceMotion || !(duration > 0) || from == to) return;
    ColorTrack t;
    t.values[0] = from; t.values[1] = to; t.keyTimes[1] = 1; t.keys = 2;
    t.start = clock.now + delay; t.duration = duration; t.timing = timing; t.fillBackwards = delay > 0; t.serial = nextSerial();
    l.tintTrack = t;
}
// HUDMotion.enter (:58-75).
void enter(Layer& l, Point offset, const Clock& clock, double duration, double delay, core::CubicTiming timing) {
    requireAnimatable(l); requireTime(clock);
    l.removeMotion(MotionSlot::enter);
    if (clock.reduceMotion) return;
    Track opacity;
    opacity.kind = Track::Kind::enterOpacity; opacity.fromOpacity = l.value(Channel::opacity); opacity.duration = duration; opacity.outer = timing;
    entry(l, MotionSlot::enter, 0, Channel::opacity) = timed(opacity, clock.now, delay);
    std::size_t i = 1;
    for (const auto& [channel, distance] : {std::pair{Channel::translateX, offset.x}, std::pair{Channel::translateY, offset.y}}) {
        if (distance == 0) continue;
        Track move;
        move.kind = Track::Kind::enterTranslation; move.offset = distance; move.additive = true; move.duration = duration; move.outer = timing;
        entry(l, MotionSlot::enter, i++, channel) = timed(move, clock.now, delay);
    }
}
// HUDMotion.stagger (:82-84).
void stagger(std::span<Layer* const> layers, Point offset, const Clock& clock) {
    for (std::size_t i = 0; i < layers.size(); ++i) enter(*layers[i], offset, clock, duration::entrance, staggerDelay(int(std::min<std::size_t>(i, 1 << 20))));
}
// HUDMotion.leave (:88-102): from the presented opacity, holds its end.
void leave(Layer& l, Point toward, const Clock& clock) {
    requireAnimatable(l); requireTime(clock);
    if (clock.reduceMotion) return;
    const double from = l.presented(Channel::opacity, clock.now);
    l.removeMotion(MotionSlot::leave);
    Track fade;
    fade.kind = Track::Kind::leaveOpacity; fade.fromOpacity = from; fade.duration = duration::leave; fade.fillForwards = true;
    entry(l, MotionSlot::leave, 0, Channel::opacity) = timed(fade, clock.now, 0);
    std::size_t i = 1;
    for (const auto& [channel, distance] : {std::pair{Channel::translateX, toward.x}, std::pair{Channel::translateY, toward.y}}) {
        if (-.6 * distance == 0) continue;
        Track move;
        move.kind = Track::Kind::leaveTranslation; move.offset = distance; move.additive = true; move.duration = duration::leave; move.fillForwards = true;
        entry(l, MotionSlot::leave, i++, channel) = timed(move, clock.now, 0);
    }
}
// HUDMotion.hold (:107-112): opacity 0 until released, under Reduce Motion too.
void hold(Layer& l) {
    requireAnimatable(l);
    l.removeMotion(MotionSlot::hold);
    auto t = Track::basic(0, 0, -std::numeric_limits<double>::max() / 4, 1, easing::linear);
    t.fillForwards = true; t.serial = nextSerial();
    entry(l, MotionSlot::hold, 0, Channel::opacity) = t;
}
void release(Layer& l, MotionSlot s) { l.removeMotion(s); }
// HUDMotion.wipe (:119-153): the mask grows 0 -> 1 and is gone when the track
// ends (the completion block's `layer.mask = nil`); the fade starts from
// fadeFrom x the MODEL opacity.
void wipe(Layer& l, WipeOrigin origin, double duration, const Clock& clock, core::CubicTiming timing, double delay, std::optional<double> fadeFrom) {
    requireAnimatable(l); requireTime(clock);
    removeWipe(l);
    if (clock.reduceMotion) return;
    l.wipeOrigin = origin;
    entry(l, MotionSlot::wipe, 0, Channel::wipe) = timed(Track::basic(0, 1, 0, duration, timing), clock.now, delay);
    if (fadeFrom) {
        const double o = l.value(Channel::opacity);
        entry(l, MotionSlot::fade, 0, Channel::opacity) = timed(Track::basic(*fadeFrom * o, o, 0, duration, timing), clock.now, delay);
    }
}
void removeWipe(Layer& l) { l.removeMotion(MotionSlot::fade); l.removeMotion(MotionSlot::wipe); }
// HUDMotion.grow (:161-169).
void grow(Layer& l, double duration, const Clock& clock, double delay, core::CubicTiming timing) {
    requireAnimatable(l); requireTime(clock);
    l.removeMotion(MotionSlot::grow);
    if (clock.reduceMotion) return;
    entry(l, MotionSlot::grow, 0, Channel::scaleX) = timed(Track::basic(0, 1, 0, duration, timing), clock.now, delay);
}
// HUDMotion.pop (:172-183): a SNAP group of linear children.
void pop(Layer& l, const Clock& clock, double duration, double delay) {
    requireAnimatable(l); requireTime(clock);
    l.removeMotion(MotionSlot::pop);
    if (clock.reduceMotion) return;
    const auto child = [&](double from, double to) { return timed(Track::basic(from, to, 0, duration, easing::snap), clock.now, delay); };
    entry(l, MotionSlot::pop, 0, Channel::scaleX) = child(.4, 1);
    entry(l, MotionSlot::pop, 1, Channel::scaleY) = child(.4, 1);
    auto rotate = child(-12 * std::numbers::pi / 180, 0); rotate.additive = true;
    entry(l, MotionSlot::pop, 2, Channel::rotation) = rotate;
    entry(l, MotionSlot::pop, 3, Channel::opacity) = child(0, l.value(Channel::opacity));
}
// Effects share the `effect` slot, one entry per channel (a new effect on a
// channel replaces the old one); release(effect) stops them all.
void keyframes(Layer& l, Channel c, std::span<const double> values, std::span<const double> keyTimes, double duration,
    const Clock& clock, core::CubicTiming timing, double repeat, bool additive, double delay) {
    requireAnimatable(l); requireTime(clock);
    need(values.size() >= 2 && values.size() <= Track::maximumKeys && (keyTimes.empty() || keyTimes.size() == values.size()), "Keyframes need 2...6 values and matching key times");
    need(duration > 0 && repeat > 0, "Keyframes need a positive duration and repeat count");
    auto& slot = l.slots[std::size_t(MotionSlot::effect)];
    auto& channels = l.slotChannels[std::size_t(MotionSlot::effect)];
    std::size_t at = slot.size();
    for (std::size_t i = 0; i < slot.size() && at == slot.size(); ++i) if (slot[i].present() && channels[i] == c) at = i;
    for (std::size_t i = 0; i < slot.size() && at == slot.size(); ++i) if (!slot[i].present()) at = i;
    need(at < slot.size(), "At most four keyframe effects per layer");
    slot[at] = {};
    if (clock.reduceMotion) return; // live loops stop under Reduce Motion
    Track t;
    t.keys = std::uint8_t(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        finite(values[i], "Keyframe values must be finite");
        t.values[i] = values[i];
        t.keyTimes[i] = keyTimes.empty() ? double(i) / double(values.size() - 1) : keyTimes[i];
    }
    t.duration = duration; t.outer = timing; t.repeat = repeat; t.additive = additive;
    channels[at] = c;
    slot[at] = timed(t, clock.now, delay);
}
void colorKeyframes(Layer& l, std::span<const Color> values, std::span<const double> keyTimes, double duration,
    const Clock& clock, core::CubicTiming timing, double repeat, double delay) {
    requireAnimatable(l); requireTime(clock);
    need(values.size() >= 2 && values.size() <= ColorTrack::maximumKeys && (keyTimes.empty() || keyTimes.size() == values.size()), "Colour keyframes need 2...3 values and matching key times");
    need(duration > 0 && repeat > 0, "Colour keyframes need a positive duration and repeat count");
    l.tintEffect = {};
    if (clock.reduceMotion) return;
    ColorTrack t;
    t.keys = std::uint8_t(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) { t.values[i] = values[i]; t.keyTimes[i] = keyTimes.empty() ? double(i) / double(values.size() - 1) : keyTimes[i]; }
    t.start = clock.now + delay; t.duration = duration; t.timing = timing; t.repeat = repeat; t.fillBackwards = delay > 0; t.serial = nextSerial();
    l.tintEffect = t;
}
// CATransaction completion blocks: the arena's token cancels them with it.
void after(Layer& l, double time, const Clock&, std::function<void()> work) {
    auto* deadlines = l.arena().deadlines();
    if (!deadlines) throw std::logic_error("Completions need an arena created with the context's deadlines: " + l.id());
    deadlines->after(time, l.arena().token(), std::move(work));
}
} // namespace motion

// ---------------------------------------------------------------------------
// Emission
// ---------------------------------------------------------------------------
namespace {
class Emitter {
public:
    Emitter(const Layer& root, const EmitOptions& o) : options_(o) { art_.root = &root; }
    Artwork run(const Layer& root) {
        part(root);
        // Drop parts left empty by a nested part root and renumber bindings.
        std::vector<std::size_t> index(art_.parts.size(), std::size_t(-1));
        std::vector<Part> parts;
        for (std::size_t i = 0; i < art_.parts.size(); ++i) {
            if (art_.parts[i].surfaces.empty()) continue;
            art_.parts[i].layers["children"] = std::move(children_[i]);
            index[i] = parts.size();
            parts.push_back(std::move(art_.parts[i]));
        }
        art_.parts = std::move(parts);
        for (auto& b : art_.bindings) b.part = index[b.part];
        return std::move(art_);
    }
private:
    struct Level { const Layer* layer; Json shell; Json::Array children; std::optional<Rect> extent; };
    struct Run { const Layer* placement; SurfaceRole role; bool first{true}; std::vector<Level> levels; };
    const EmitOptions& options_;
    Artwork art_;
    std::vector<Json::Array> children_;
    std::vector<std::size_t> nodes_;
    std::unordered_map<const Layer*, std::size_t> fragments_;
    std::map<std::string, std::size_t, std::less<>> statics_; // "<part base>" -> next static serial
    std::size_t part_{}, depth_{};
    std::string base_;
    std::size_t continuation_{};

    void openPart(std::string id) {
        art_.parts.push_back({id, frameNode(id, {}), {}});
        children_.emplace_back(); nodes_.push_back(1);
        part_ = art_.parts.size() - 1;
    }
    void part(const Layer& root) {
        const auto saved = std::tuple{part_, base_, continuation_};
        base_ = options_.partPrefix + root.id(); continuation_ = 0;
        openPart(base_);
        placement(root);
        std::tie(part_, base_, continuation_) = saved;
    }
    // The shell of a static layer: its frame, CA transform, model opacity,
    // hidden and clip baked; content and static children inside.
    Json shell(const Layer& l) {
        const auto fragment = fragments_[&l]++;
        auto n = frameNode(fragment ? l.id() + "~" + std::to_string(fragment) : l.id(), {0, 0, l.bounds().width, l.bounds().height});
        n["position"] = point(l.position());
        n["anchorPoint"] = point(l.anchorPoint());
        const auto t = caTransform(l, l.value(Channel::translateX), l.value(Channel::translateY), l.value(Channel::scaleX), l.value(Channel::scaleY), l.value(Channel::rotation));
        if (!identity(t)) n["transform"] = Json::Array{Json::Array{t.a, t.b, 0, 0}, Json::Array{t.c, t.d, 0, 0}, Json::Array{0, 0, 1, 0}, Json::Array{t.tx, t.ty, 0, 1}};
        if (l.value(Channel::opacity) != 1) n["opacity"] = l.value(Channel::opacity);
        if (l.hidden()) n["hidden"] = true;
        if (l.masksToBounds()) { n["masksToBounds"] = true; if (l.maskCornerRadius() > 0) n["cornerRadius"] = l.maskCornerRadius(); }
        return n;
    }
    // A layer's own drawing in its local space: its content node, or its text
    // line box at the padding offset (the layer frame is the padded box).
    void content(const Layer& l, Level& level) {
        if (const auto* text = l.text()) {
            need(options_.text, "Emitting design text needs TextServices");
            auto block = *text;
            const auto pad = block.padding();
            block.lineFrame = {pad.width, pad.height, text->lineFrame.width, text->lineFrame.height};
            level.children.push_back(textNode(l.id() + "/text", block, *options_.text));
            level.extent = unite(level.extent, {0, 0, block.lineFrame.width + 2 * pad.width, block.lineFrame.height + 2 * pad.height});
        } else if (const auto& node = l.content()) {
            auto copy = *node;
            reid(copy, l.id());
            level.extent = unite(level.extent, frameOf(copy));
            level.children.push_back(std::move(copy));
        }
    }
    void fold(Run& run) {
        auto level = std::move(run.levels.back());
        run.levels.pop_back();
        if (level.children.empty()) return;
        auto& parent = run.levels.back();
        const auto& l = *level.layer;
        auto extent = *level.extent;
        if (l.masksToBounds()) extent = intersect(extent, {0, 0, l.bounds().width, l.bounds().height});
        const auto t = caTransform(l, l.value(Channel::translateX), l.value(Channel::translateY), l.value(Channel::scaleX), l.value(Channel::scaleY), l.value(Channel::rotation));
        parent.extent = unite(parent.extent, bounding(placed(l, t), extent));
        level.shell["children"] = std::move(level.children);
        parent.children.push_back(std::move(level.shell));
    }
    // Ends the current static run as one surface and reopens fresh shells of
    // the open static chain for the content that follows.
    void close(Run& run) {
        std::vector<const Layer*> chain;
        for (std::size_t i = 1; i < run.levels.size(); ++i) chain.push_back(run.levels[i].layer);
        while (run.levels.size() > 1) fold(run);
        auto& root = run.levels.front();
        if (!root.children.empty()) surface(run, root);
        root.children = {}; root.extent.reset();
        run.first = false;
        for (const auto* l : chain) run.levels.push_back({l, shell(*l), {}, {}});
    }
    void surface(Run& run, Level& root) {
        const auto& p = *run.placement;
        auto& part = art_.parts[part_];
        std::string id;
        if (run.first && p.animatable()) id = p.id();
        else { auto& serial = statics_[part.id]; id = part.id + ".static." + std::to_string(serial++); }
        for (const auto& s : part.surfaces) need(s.id != id, "Repeated design surface id in one part");
        Rect r = root.extent.value_or(Rect{});
        if (p.masksToBounds()) r = {0, 0, p.bounds().width, p.bounds().height};
        r.width = std::max(r.width, 1.0); r.height = std::max(r.height, 1.0);
        Json group = Json::Object{{"id", id}, {"kind", "layer"}, {"bounds", rectJson(r)}, {"position", point({r.x, r.y})}, {"anchorPoint", point({0, 0})},
            {"allowsGroupOpacity", false}, {"children", std::move(root.children)}};
        if (p.masksToBounds()) { group["masksToBounds"] = true; if (p.maskCornerRadius() > 0) group["cornerRadius"] = p.maskCornerRadius(); }
        nodes_[part_] += countNodes(group, 1);
        need(nodes_[part_] <= maximumNodes, "A design part exceeds the rasterizer's node limit (split it with setPartRoot)");
        part.surfaces.push_back({id, run.role, r});
        children_[part_].push_back(std::move(group));
        art_.bindings.push_back({&p, part_, part.surfaces.size() - 1, run.role, p.alphaClip()});
    }
    // A placement layer: the emit root, a part root or a boundary. Its own
    // drawing and static descendants become runs placed by its pose.
    void placement(const Layer& p) {
        need(++depth_ <= maximumDepth, "Design layer tree is too deep");
        if (p.animatable()) art_.animatable.push_back(&p);
        Run run{&p, p.animatable() ? p.role() : SurfaceRole::staticContent, true, {}};
        run.levels.push_back({&p, {}, {}, {}});
        content(p, run.levels.front());
        for (const auto* child : paintOrder(p)) visit(*child, run);
        close(run);
        --depth_;
    }
    void visit(const Layer& l, Run& run) {
        if (l.partRoot()) {
            close(run);
            part(l);
            openPart(base_ + "~" + std::to_string(++continuation_));
            return;
        }
        if (l.animatable()) { close(run); placement(l); return; }
        need(++depth_ <= maximumDepth, "Design layer tree is too deep");
        run.levels.push_back({&l, shell(l), {}, {}});
        content(l, run.levels.back());
        for (const auto* child : paintOrder(l)) visit(*child, run);
        fold(run);
        --depth_;
    }
};
}

Artwork emit(const Layer& root, const EmitOptions& options) { return Emitter(root, options).run(root); }

// ---------------------------------------------------------------------------
// Sampling (allocation-free)
// ---------------------------------------------------------------------------
namespace {
Rect wipeRect(const Layer& l, double s) noexcept {
    const auto b = l.bounds();
    switch (l.wipeOrigin) {
    case WipeOrigin::top: return {0, 0, b.width, b.height * s};
    case WipeOrigin::centerX: return {b.width / 2 - b.width * s / 2, 0, b.width * s, b.height};
    case WipeOrigin::left: break;
    }
    return {0, 0, b.width * s, b.height};
}
void addMask(SurfacePose& pose, const Affine& world, Rect r, double radius) noexcept {
    if (pose.maskCount < pose.masks.size()) pose.masks[pose.maskCount++] = {world, r, radius};
}
}
void samplePoses(const Artwork& art, double now, std::span<SurfacePose> out) {
    need(out.size() == art.bindings.size(), "One pose per surface binding");
    std::array<const Layer*, maximumDepth + 1> chain{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto& pose = out[i];
        pose = {};
        const auto* p = art.bindings[i].placement;
        std::size_t n{};
        const Layer* l = p;
        for (; l && n < chain.size(); l = l == art.root ? nullptr : l->parent()) chain[n++] = l;
        if (!n || chain[n - 1] != art.root) { pose.visible = false; continue; } // detached since emission
        Affine world;
        double opacity = 1;
        for (std::size_t k = n; k-- > 0;) {
            const auto& layer = *chain[k];
            if (layer.hidden()) pose.visible = false;
            opacity *= layer.presented(Channel::opacity, now);
            if (&layer != art.root) world = world * presentedLocal(layer, now);
            if (k > 0 && layer.masksToBounds()) addMask(pose, world, {0, 0, layer.bounds().width, layer.bounds().height}, layer.maskCornerRadius());
            if (layer.animatable()) {
                const double w = std::clamp(layer.presented(Channel::wipe, now), 0.0, 1.0);
                if (w < 1) { if (w <= 0) pose.visible = false; addMask(pose, world, wipeRect(layer, w), 0); }
            }
        }
        pose.world = world;
        pose.opacity = float(std::clamp(opacity, 0.0, 1.0));
        if (!(pose.opacity > 0)) pose.visible = false;
        pose.tint = p->presentedTint(now);
    }
}
LayerPose sampleLayer(const Layer& layer, const Layer& root, double now) noexcept {
    std::array<const Layer*, maximumDepth + 1> chain{};
    std::size_t n{};
    for (const Layer* l = &layer; l && n < chain.size(); l = l == &root ? nullptr : l->parent()) chain[n++] = l;
    LayerPose pose;
    if (!n || chain[n - 1] != &root) { pose.visible = false; return pose; }
    for (std::size_t k = n; k-- > 0;) {
        const auto& l = *chain[k];
        if (l.hidden()) pose.visible = false;
        pose.opacity *= l.presented(Channel::opacity, now);
        if (&l != &root) pose.world = pose.world * presentedLocal(l, now);
    }
    pose.opacity = std::clamp(pose.opacity, 0.0, 1.0);
    if (!(pose.opacity > 0)) pose.visible = false;
    return pose;
}
bool requiresFrames(const Artwork& art, double now) noexcept {
    return std::any_of(art.animatable.begin(), art.animatable.end(), [&](const Layer* l) { return l->animating(now); });
}

// ---------------------------------------------------------------------------
// Model geometry
// ---------------------------------------------------------------------------
Affine modelWorld(const Layer& layer, const Layer& root) noexcept {
    Affine world;
    for (const Layer* l = &layer; l && l != &root; l = l->parent()) world = modelLocal(*l) * world;
    return world;
}
namespace {
const Layer& top(const Layer& l) noexcept { const Layer* t = &l; while (t->parent()) t = t->parent(); return *t; }
}
std::optional<Point> convert(Point p, const Layer& from, const Layer& to) noexcept {
    const auto& common = top(from);
    if (&top(to) != &common) return std::nullopt;
    const auto inverse = modelWorld(to, common).inverse();
    if (!inverse) return std::nullopt;
    return inverse->apply(modelWorld(from, common).apply(p));
}
Rect convert(Rect r, const Layer& from, const Layer& to) noexcept {
    const auto& common = top(from);
    if (&top(to) != &common) return {};
    const auto inverse = modelWorld(to, common).inverse();
    if (!inverse) return {};
    return bounding(*inverse * modelWorld(from, common), r);
}
// CALayer.isVisible(in:) (HUDDesignControl.swift:149-157).
bool isVisible(const Layer& layer, const Layer& root) noexcept {
    for (const Layer* l = &layer; l; l = l->parent()) {
        if (l->hidden() || l->value(Channel::opacity) <= .01) return false;
        if (l == &root) return true;
    }
    return false;
}
// CALayer.isUnclipped(_:in:) (HUDDesignScreen.swift:663-672): every clipping
// ancestor strictly between the layer and the root contains the point.
bool isUnclipped(const Layer& layer, Point rootPoint, const Layer& root) noexcept {
    for (const Layer* l = layer.parent(); l && l != &root; l = l->parent()) {
        if (!l->masksToBounds()) continue;
        const auto local = convert(rootPoint, root, *l);
        if (!local || !insideHalfOpen({0, 0, l->bounds().width, l->bounds().height}, *local)) return false;
    }
    return true;
}
} // namespace endfield::modules::design
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
