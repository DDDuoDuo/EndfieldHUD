// The control base, the router and the redesign target pass: ports of
// Sources/HUDDesignControl.swift (HUDControl :45-108, HUDReadout :27-39,
// HUDInteractionRouter :112-145), Sources/HUDControlHighlightLayer.swift
// (addTarget :94-107, setTarget :110-115, update :119-160, setHighlighted
// :168-176) and HUDControl.applyHoverMotion (Sources/HUDDesignButtons.swift:6-18).
//
// Deliberately not ported: the legacy tint/rim highlight (HUDControlHighlightLayer
// .add, refreshColor, animateOpacity) stays in the unported v1.2.0 modules; the
// redesign targets draw nothing and only report (highlighted, pressed). The
// temporary wipe mask of a layer (HUDMotion.wipe) is not a hit clip here: a
// Windows wipe is a numeric channel, so a press during the .18 s menu wipe hits
// the model rect exactly like a press after it (the Mac's mask model is the
// full bounds as well; only points outside the bounds differ for 180 ms).
#include "modules/hud_design_control.hpp"
#include <algorithm>

namespace endfield::modules::design {
namespace {
// CGRect.contains: half-open [min, max) on both axes, empty rects contain nothing.
bool inside(Rect r, Point p) noexcept { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
Rect boundsOf(const Layer& l) noexcept { return {0, 0, l.bounds().width, l.bounds().height}; }
double area(const HitTarget& t) noexcept { return t.rect.width * t.rect.height; }
// HUDControlHighlightLayer.setHighlighted for a target with a callback (:168-176).
void setHighlighted(HitTarget& t, bool value, bool pressed, bool force = false) {
    const bool changed = t.highlighted != value || t.pressed != (pressed && value);
    if (!changed && !force) return;
    t.highlighted = value; t.pressed = pressed && value;
    if (t.onChange) t.onChange(t.highlighted, t.pressed);
}
}

Control::Control(Environment& env, std::string id, std::string text, Layer& layer)
    : label(std::move(text)), env_(&env), id_(std::move(id)), layer_(&layer) {}
Control::~Control() = default;

// isEnabled.didSet (:54-60): the target clears its state (forced report), then enabledChanged.
void Control::setEnabled(bool value) {
    if (enabled_ == value) return;
    enabled_ = value;
    if (target_) { target_->enabled = value; setHighlighted(*target_, false, false, true); }
    enabledChanged();
}
// setTarget (:68-74): moving keeps the target (and its container flag), like setTarget(rect:path:).
void Control::setTarget(Rect rect, std::optional<PathBuilder> path, bool container) {
    if (target_) { target_->rect = rect; target_->path = std::move(path); return; }
    target_ = &layer_->addTarget(rect, std::move(path), container, [this](bool hovered, bool pressed) { receive(hovered, pressed); });
    target_->enabled = enabled_;
}
void Control::receive(bool hovered, bool pressed) {
    const bool hoverChange = hovered != hovered_, pressChange = pressed != pressed_;
    hovered_ = hovered; pressed_ = pressed;
    if (hoverChange) hoverChanged();
    if (pressChange) pressChanged();
}
void Control::clearInteractionState() { receive(false, false); }
bool Control::contains(Point p) const { return inside(target_ ? target_->rect : boundsOf(*layer_), p); }
bool Control::pointerDown(Point) { return enabled_; }
void Control::pointerUp(Point p) { if (contains(p)) activate(); }
void Control::activate() { if (enabled_ && onActivate) onActivate(); }
Rect Control::rect(const Layer& root) const { return convert(target_ ? target_->rect : boundsOf(*layer_), *layer_, root); }
AccessibilityItem Control::accessibilityItem(const Layer& root, AccessibilityRole role) const {
    AccessibilityItem item; item.id = id_; item.label = label; item.role = role; item.rect = rect(root); item.enabled = enabled_;
    return item;
}
void Control::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    const auto role = accessibilityRole();
    if (!role || label.empty()) return;
    out.push_back(accessibilityItem(root, *role));
}

Readout::Readout(Environment& env, std::string id, std::string text, std::string current, Layer& frameLayer)
    : Control(env, std::move(id), std::move(text), frameLayer), value(std::move(current)) {}
void Readout::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, AccessibilityRole::text); item.value = value;
    out.push_back(std::move(item));
}

Control* ControlSet::find(std::string_view id) const noexcept {
    for (const auto& c : controls_) if (c->id() == id) return c.get();
    return nullptr;
}
void ControlSet::clearInteractionState() noexcept { for (const auto& c : controls_) c->clearInteractionState(); }

// HUDInteractionRouter (:112-145): the TOPMOST (last registered) enabled,
// visible, unclipped control containing the point takes the press.
void Router::set(std::vector<Control*> controls) {
    controls_ = std::move(controls);
    if (tracking_ && std::find(controls_.begin(), controls_.end(), tracking_) == controls_.end()) tracking_ = nullptr;
}
Control* Router::control(Point point, const Layer& root) const {
    for (auto it = controls_.rbegin(); it != controls_.rend(); ++it) {
        Control& c = **it;
        if (!c.enabled() || !isVisible(c.layer(), root) || !isUnclipped(c.layer(), point, root)) continue;
        const auto local = design::convert(point, root, c.layer());
        if (local && c.contains(*local)) return &c;
    }
    return nullptr;
}
bool Router::pointerDown(Point point, const Layer& root) {
    Control* c = control(point, root);
    if (!c) { tracking_ = nullptr; return false; }
    const auto local = design::convert(point, root, c->layer());
    tracking_ = local && c->pointerDown(*local) ? c : nullptr;
    return true;
}
void Router::pointerDragged(Point point, const Layer& root) {
    if (!tracking_) return;
    if (const auto local = design::convert(point, root, tracking_->layer())) tracking_->pointerDragged(*local);
}
void Router::pointerUp(Point point, const Layer& root) {
    Control* c = tracking_;
    if (!c) return;
    tracking_ = nullptr;
    if (const auto local = design::convert(point, root, c->layer())) c->pointerUp(*local);
}
void Router::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    for (Control* c : controls_) if (isVisible(c->layer(), root)) c->accessibilityItems(root, out);
}

// HUDControlHighlightLayer.update(in:point:pressed:) (:119-160). A target is a
// sublayer of its host added when the component installs it, i.e. after the
// host's content sublayers: the walk visits a layer's children first and its
// targets last, so equal-area ties go to the later target as on the Mac.
void updateHighlights(const Layer& root, std::optional<Point> point, bool pressed) {
    struct Walk {
        const Layer& root; std::optional<Point> point;
        std::vector<HitTarget*> candidates; std::vector<std::pair<HitTarget*, bool>> containers; HitTarget* hit{};
        void visit(const Layer& item, bool visible, bool pointVisible) {
            if (item.decorative()) return;
            const bool shown = visible && !item.hidden() && item.value(Channel::opacity) > .01;
            bool in = pointVisible && shown;
            // Convert only at targets and clip boundaries (:126-131).
            std::optional<Point> local;
            if (in && point && (!item.targets().empty() || item.masksToBounds())) local = design::convert(*point, root, item);
            const bool childrenIn = in && !(item.masksToBounds() && (!local || !inside(boundsOf(item), *local)));
            for (const Layer* child : item.children()) visit(*child, shown, childrenIn);
            for (const auto& owned : item.targets()) {
                HitTarget& t = *owned;
                const bool targetIn = childrenIn && !t.hidden && local.has_value();
                if (t.container) { containers.emplace_back(&t, targetIn && t.enabled && t.contains(*local)); continue; }
                candidates.push_back(&t);
                if (targetIn && t.enabled && t.contains(*local) && (!hit || area(t) <= area(*hit))) hit = &t;
            }
        }
    } walk{root, point, {}, {}, nullptr};
    walk.visit(root, true, point.has_value());
    for (HitTarget* t : walk.candidates) setHighlighted(*t, t == walk.hit, pressed);
    for (auto [t, in] : walk.containers) setHighlighted(*t, in, pressed && in && walk.hit == nullptr);
}

Rect localBounds(const Layer& l) noexcept { return boundsOf(l); }
void setBackground(Layer& l, Color fill, double radius) { l.setContent(solid(l.id(), boundsOf(l), fill, radius)); }
void setBorder(Layer& l, std::optional<Color> fill, Color border, double width, double radius) {
    l.setContent(bordered(l.id(), boundsOf(l), fill, border, width, radius));
}
void setPath(Layer& l, const PathBuilder& path, Color fill) { l.setContent(shape(l.id(), boundsOf(l), path, fill)); }
void setTintedBackground(Layer& l, Color c, double radius) {
    l.setAnimatable(); l.setContent(solid(l.id(), boundsOf(l), white(1), radius)); l.setTint(c);
}
void setTintedBorder(Layer& l, Color c, double width, double radius) {
    l.setAnimatable(); l.setContent(bordered(l.id(), boundsOf(l), std::nullopt, white(1), width, radius)); l.setTint(c);
}
void setTintedPath(Layer& l, const PathBuilder& path, Color c) {
    l.setAnimatable(); l.setContent(shape(l.id(), boundsOf(l), path, white(1))); l.setTint(c);
}
Layer& makeText(Builder& b, std::string_view id, std::string text, const TextStyle& style, Rect line, TextAlignment alignment) {
    Layer& l = b.layer(id);
    TextBlock block(std::move(text), style, alignment); block.setLineFrame(line);
    l.setText(std::move(block));
    return l;
}
const TextBlock& textOf(const Layer& l) {
    static const TextBlock empty;
    const TextBlock* block = l.text();
    return block ? *block : empty;
}
void setLine(Layer& l, Rect line) { editText(l, [&](TextBlock& block) { block.setLineFrame(line); }); }
Layer& makeIcon(Builder& b, std::string_view id, Icon icon, double size, Color tint) {
    Layer& l = b.layer(id);
    l.setBounds({size, size});
    setIcon(l, icon, size, tint);
    return l;
}
void setIcon(Layer& l, Icon icon, double size, Color tint) { l.setContent(iconNode(l.id(), icon, size, tint, {size / 2, size / 2})); }
PathBuilder roundedRect(Rect r, double radius) {
    // CGPath(roundedRect:) clamps the radius to half the shorter side; quarter arcs as cubics.
    const double k = .5522847498307936, rx = std::min(radius, r.width / 2), ry = std::min(radius, r.height / 2);
    const double x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
    PathBuilder p;
    p.move({x1, y0 + ry}).line({x1, y1 - ry}).cubic({x1, y1 - ry + k * ry}, {x1 - rx + k * rx, y1}, {x1 - rx, y1})
        .line({x0 + rx, y1}).cubic({x0 + rx - k * rx, y1}, {x0, y1 - ry + k * ry}, {x0, y1 - ry})
        .line({x0, y0 + ry}).cubic({x0, y0 + ry - k * ry}, {x0 + rx - k * rx, y0}, {x0 + rx, y0})
        .line({x1 - rx, y0}).cubic({x1 - rx + k * rx, y0}, {x1, y0 + ry - k * ry}, {x1, y0 + ry}).close();
    return p;
}

namespace {
// portableCharacterRules: one UTF-8 scalar at `at` (invalid bytes count as one scalar each).
char32_t decode(std::string_view s, std::size_t& at) noexcept {
    const auto b = static_cast<unsigned char>(s[at]);
    const int length = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
    if (at + std::size_t(length) > s.size() || length == 1) { ++at; return b; }
    char32_t c = b & (0x7F >> length);
    for (int i = 1; i < length; ++i) c = (c << 6) | (static_cast<unsigned char>(s[at + std::size_t(i)]) & 0x3F);
    at += std::size_t(length);
    return c;
}
bool in(char32_t c, char32_t a, char32_t b) noexcept { return c >= a && c <= b; }
bool extend(char32_t c) noexcept {
    return in(c, 0x300, 0x36F) || in(c, 0x483, 0x489) || in(c, 0x591, 0x5BD) || in(c, 0x610, 0x61A) || in(c, 0x64B, 0x65F) || c == 0x670
        || in(c, 0x6D6, 0x6DC) || in(c, 0x6DF, 0x6E4) || in(c, 0x900, 0x903) || in(c, 0x93A, 0x94F) || in(c, 0x951, 0x957)
        || in(c, 0x1AB0, 0x1AFF) || in(c, 0x1DC0, 0x1DFF) || c == 0x200C || c == 0x200D || in(c, 0x20D0, 0x20FF) || in(c, 0x302A, 0x302F)
        || in(c, 0x3099, 0x309A) || in(c, 0xFE00, 0xFE0F) || in(c, 0xFE20, 0xFE2F) || in(c, 0x1F3FB, 0x1F3FF) || in(c, 0xE0020, 0xE007F)
        || in(c, 0xE0100, 0xE01EF);
}
bool pictographic(char32_t c) noexcept { return in(c, 0x2600, 0x27BF) || in(c, 0x1F000, 0x1FAFF) || c == 0x2764; }
bool regional(char32_t c) noexcept { return in(c, 0x1F1E6, 0x1F1FF); }
// Hangul: L 1100-115F, V 1160-11A7, T 11A8-11FF; LV/LVT syllables AC00-D7A3.
int jamo(char32_t c) noexcept {
    if (in(c, 0x1100, 0x115F) || in(c, 0xA960, 0xA97F)) return 1;
    if (in(c, 0x1160, 0x11A7) || in(c, 0xD7B0, 0xD7C6)) return 2;
    if (in(c, 0x11A8, 0x11FF) || in(c, 0xD7CB, 0xD7FB)) return 3;
    if (in(c, 0xAC00, 0xD7A3)) return (c - 0xAC00) % 28 == 0 ? 4 : 5; // LV, LVT
    return 0;
}
// Byte offsets where each cluster starts, plus the end.
template <class F> void clusters(std::string_view s, F&& boundary) {
    std::size_t at = 0;
    char32_t previous = 0; bool first = true; int regionals = 0;
    while (at < s.size()) {
        const std::size_t start = at;
        const char32_t c = decode(s, at);
        bool join = !first && ((previous == '\r' && c == '\n') || extend(c) || (previous == 0x200D && pictographic(c)));
        if (!first && !join) {
            const int p = jamo(previous), n = jamo(c);
            join = (p == 1 && (n == 1 || n == 2 || n == 4 || n == 5)) || ((p == 2 || p == 4) && (n == 2 || n == 3)) || ((p == 3 || p == 5) && n == 3);
        }
        if (!first && !join && regional(c) && regional(previous) && regionals % 2 == 1) join = true;
        regionals = regional(c) ? regionals + 1 : 0;
        if (!join && !boundary(start)) return;
        first = false; previous = c;
    }
    boundary(s.size());
}
}
// The portable default only (tests without ICU); production injects ICU.
CharacterRules portableCharacterRules() {
    CharacterRules rules;
    rules.count = [](std::string_view s) {
        std::size_t count = 0;
        clusters(s, [&](std::size_t at) { if (at < s.size()) ++count; return true; });
        return count;
    };
    rules.prefix = [](std::string_view s, std::size_t count) {
        std::size_t end = s.size(), seen = 0;
        clusters(s, [&](std::size_t at) { if (seen == count) { end = at; return false; } ++seen; return true; });
        return std::string(s.substr(0, end));
    };
    return rules;
}

// HUDControl.applyHoverMotion (HUDDesignButtons.swift:6-18).
void applyHoverMotion(Control& control, double lift, double hoverScale, double pressScale, Layer* brighten) {
    const bool pressed = control.pressed() && control.enabled(), hovered = control.hovered() && control.enabled();
    const auto clock = control.env().clock();
    Layer& layer = control.layer();
    motion::animate(layer, Channel::translateY, hovered && !pressed ? -lift : 0, duration::hover, clock);
    const double scale = pressed ? pressScale : (hovered ? hoverScale : 1), time = pressed ? duration::press : duration::hover;
    motion::animate(layer, Channel::scaleX, scale, time, clock);
    motion::animate(layer, Channel::scaleY, scale, time, clock);
    if (brighten) motion::animate(*brighten, Channel::opacity, hovered ? 1 : 0, duration::hover, clock, easing::ease);
}
} // namespace endfield::modules::design
