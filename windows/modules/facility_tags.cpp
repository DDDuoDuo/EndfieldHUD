// The facility tag stack: the port of Sources/HUDFacilityTags.swift (:12-325).
// The stack's timers (collapse at 300 ms, removal at 560 ms) are
// Environment::deadlines entries under the stack's token; they find their tag
// by serial, so a tag freed meanwhile is simply gone (the Mac's weak tag).
#include "modules/facility_tags.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::facility {
namespace {
using d::Channel;
// FacilityTag.textStyle (:168): 11 bold #F2F2F2, tracking .06.
d::TextStyle tagStyle() { return {11, d::FontWeight::bold, d::rgb(0xF2F2F2), 1, .06}; }
enum class State : std::uint8_t { live, holding, collapsing, removed };
}

// One tag of the stack (FacilityTag, :164-325).
class FacilityTagStack::Tag final {
public:
    Tag(FacilityAlertKind k, d::Environment& env, std::uint64_t n)
        : kind(k), serial(n), env_(&env), arena_("facility.tag." + std::to_string(n), &env.deadlines), builder_{env, arena_, controls_} {
        slot = &builder_.layer("slot");
        layer = &builder_.layer("tag");
        content = &builder_.layer("content");
        plate = &builder_.layer("plate");
        ring = &builder_.layer("ring");
        hazard = std::make_unique<HazardStripes>(builder_, "hazard", d::Size{12, tagHeight}, 8 * std::sqrt(2.0), d::fixed::orange, .6);
        glyph = &d::makeIcon(builder_, "glyph", d::Icon::warn, 13, d::fixed::orange);
        text = &d::makeText(builder_, "text", "", tagStyle(), {});
        brighten = &builder_.layer("brighten");
        cross = &builder_.layer("cross");
        auto& closeLayer = builder_.layer("close");
        closeGlyph = &d::makeIcon(builder_, "close.glyph", d::Icon::x, 10, d::rgb(0xAAAAAA));
        closeHover = &d::makeIcon(builder_, "close.hover", d::Icon::x, 10, d::white(1));
        const std::string id = "facility.tag." + std::string(facilityAlertID(k));
        control = &controls_.make<FacilityControl>(env, id, *layer);
        close = &controls_.make<FacilityControl>(env, id + ".dismiss", closeLayer);
        slot->setAnimatable();                           // the column place and its .25 s move
        slot->setAnchorPoint({0, 0}); slot->setPartRoot(true);
        layer->setAnimatable();                          // hover, ftIn, collapse
        layer->setAnchorPoint({.5, 0});
        layer->setMasksToBounds(true);
        layer->wipeOrigin = d::WipeOrigin::top;          // the collapsing height
        content->setAnimatable();                        // stays centred while collapsing
        // The strip's chamfered #111 background is its own surface: its alpha
        // clips the drifting band (the Mac's hazardClip mask).
        hazard->layer().setAnimatable();
        d::setPath(hazard->layer(), d::chamfer({0, 0, 12, tagHeight}, 10, d::bottomLeft), d::fixed::nearBlack);
        hazard->band().setAlphaClip(&hazard->layer());
        brighten->setAnimatable(d::SurfaceRole::lazy);   // hover only
        brighten->setValue(Channel::opacity, 0);
        cross->setAnimatable();                          // the strike scales in from the left
        cross->setAnchorPoint({0, .5});
        cross->setValue(Channel::scaleX, 0);
        for (d::Layer* child : {plate, ring, &hazard->layer(), glyph, text}) content->add(*child);
        closeLayer.setAnimatable();                      // x hover scale 1.15
        closeLayer.setBounds({20, 20});
        for (d::Layer* icon : {closeGlyph, closeHover}) { icon->setPosition({10, 10}); closeLayer.add(*icon); }
        closeHover->setAnimatable();                     // #aaa -> #fff
        closeHover->setValue(Channel::opacity, 0);
        content->add(closeLayer); content->add(*cross); content->add(*brighten);
        layer->add(*content);
        slot->add(*layer);
        control->onHover = [this](bool on) { hover(on); };
        close->onHover = [this](bool on) { hoverClose(on); };
    }
    const FacilityAlertKind kind;
    const std::uint64_t serial;
    State state{State::live};
    bool isNew{}, isStruck{};
    double width{};
    d::Point origin;
    d::Layer *slot, *layer, *content, *plate, *ring, *glyph, *text, *brighten, *cross, *closeGlyph, *closeHover;
    std::unique_ptr<HazardStripes> hazard;
    FacilityControl *control, *close;
    std::uint64_t revision() const noexcept { return arena_.revision(); }

    // render (:220-229): DISK 8% FREE: label, bold orange value, optional suffix.
    void render(const FacilityAlert& alert) {
        const auto language = env_->language;
        std::vector<d::TextRun> runs{{alert.tagLabel(language) + " ", tagStyle()}, {alert.value(language), tagStyle().colored(d::fixed::orange)}};
        if (const auto suffix = alert.tagSuffix(language)) runs.push_back({" " + *suffix, tagStyle()});
        const auto title = alert.title(language);
        control->label = title + ", " + alert.tagText(language);
        close->label = core::localized("Dismiss", "忽略", language) + ", " + title;
        if (runs == d::textOf(*text).runs && width != 0) return;
        d::editText(*text, [&](d::TextBlock& block) { block.runs = std::move(runs); });
        layout();
    }
    // layout (:231-262): flex row, gap 8, padding-right 4: strip 12, glyph, text, then x (20).
    void layout() {
        const double h = tagHeight, glyphWidth = 13, textX = 12 + 8 + glyphWidth + 8, trailing = 8 + 20 + 4;
        const double natural = std::ceil(d::preferredWidth(d::textOf(*text), env_->text));
        width = textX + natural + trailing;
        const double w = std::min(FacilityTagStack::width, width), textWidth = std::max(0.0, w - textX - trailing);
        const d::Rect bounds{0, 0, w, h};
        slot->setBounds({w, h});
        layer->setFrame(bounds); content->setFrame(bounds);
        const auto silhouette = d::chamfer(bounds, 10, d::bottomLeft);
        plate->setFrame(bounds); d::setPath(*plate, silhouette, d::rgb(0x121212, .92));
        brighten->setFrame(bounds); d::setPath(*brighten, silhouette, d::white(.05));
        // inset 0 0 0 1px, clipped by the chamfer: no ring along the cut.
        d::PathBuilder edges;
        edges.rect({0, 0, w, 1}).rect({w - 1, 0, 1, h}).rect({10, h - 1, w - 10, 1}).rect({0, 0, 1, h - 10});
        ring->setFrame(bounds); d::setPath(*ring, edges, d::rgb(0x4A3216));
        glyph->setPosition({20 + glyphWidth / 2, h / 2});
        d::setLine(*text, {textX, (h - 11) / 2, textWidth, 11});
        close->layer().setPosition({w - 4 - 10, h / 2});
        cross->setBounds({std::max(0.0, w - 14 - 28), 2});
        cross->setPosition({14, h / 2});
        d::setBackground(*cross, d::fixed::orange);      // a layer's fill follows its bounds
        control->setTarget(bounds, silhouette, true);
        control->hitPath = silhouette;
        close->setTarget({0, 0, 20, 20});
    }
    void place(d::Point point) { origin = point; slot->setPosition(point); }
    // Slides to a new place (position .25 s ease): the slot moves at once and
    // its translation runs from the old presented place to zero.
    void move(d::Point point, bool animated) {
        const auto now = env_->now;
        const d::Point from{slot->position().x + slot->presented(Channel::translateX, now), slot->position().y + slot->presented(Channel::translateY, now)};
        origin = point;
        slot->setPosition(point);
        slot->setValue(Channel::translateX, animated ? from.x - point.x : 0);
        slot->setValue(Channel::translateY, animated ? from.y - point.y : 0);
        if (!animated) return;
        const auto clock = env_->clock();
        d::motion::animate(*slot, Channel::translateX, 0, .25, clock, d::easing::ease);
        d::motion::animate(*slot, Channel::translateY, 0, .25, clock, d::easing::ease);
    }
    // ftIn .35 s E: from +16 px x and transparent (a group of linear children).
    void enter() {
        const auto clock = env_->clock();
        const std::array<double, 2> fade{0, 1}, slide{16, 0};
        d::motion::keyframes(*layer, Channel::opacity, fade, {}, .35, clock, d::easing::standard);
        d::motion::keyframes(*layer, Channel::translateX, slide, {}, .35, clock, d::easing::standard, 1, true);
    }
    bool isLooping() const noexcept { return hazard->isRunning(); }
    void setLooping(bool on) { if (on && state != State::removed) hazard->start(*env_); else hazard->stop(); }
    // Hover: translateX(-3px) and brightness 1.1 (.22 s).
    void hover(bool on) {
        const bool lift = on && state == State::live;
        const auto clock = env_->clock();
        d::motion::animate(*layer, Channel::translateX, lift ? -3 : 0, d::duration::hover, clock);
        d::motion::animate(*brighten, Channel::opacity, lift ? 1 : 0, d::duration::hover, clock, d::easing::ease);
    }
    // x: #aaa -> #fff and scale 1.15 (.2 s).
    void hoverClose(bool on) {
        const auto clock = env_->clock();
        d::motion::animate(*closeHover, Channel::opacity, on ? 1 : 0, .2, clock, d::easing::ease);
        for (const auto c : {Channel::scaleX, Channel::scaleY}) d::motion::animate(close->layer(), c, on ? 1.15 : 1, .2, clock);
    }
    void disable() {
        control->setEnabled(false); close->setEnabled(false);
        control->clearInteractionState(); close->clearInteractionState();
    }
    // .cross: the 2 px orange strike scales in from the left (.28 s E).
    void strike() {
        isStruck = true;
        d::motion::animate(*cross, Channel::scaleX, 1, .28, env_->clock());
    }
    // .gone: opacity 0, height 0 and margin-top -6 (.25 s); the content stays
    // centred in the shrinking box.
    void collapse() {
        const auto clock = env_->clock();
        constexpr double duration = .25;
        d::motion::animate(*layer, Channel::opacity, 0, duration, clock, d::easing::ease);
        d::motion::animate(*layer, Channel::wipe, 0, duration, clock, d::easing::ease);
        d::motion::animate(*layer, Channel::translateY, -FacilityTagStack::gap, duration, clock, d::easing::ease);
        d::motion::animate(*content, Channel::translateY, -tagHeight / 2, duration, clock, d::easing::ease);
    }
private:
    d::Environment* env_;
    d::LayerArena arena_;
    d::ControlSet controls_;
    d::Builder builder_;
};

FacilityTagStack::FacilityTagStack(d::Environment& env) : env_(&env), arena_("facility", &env.deadlines), layer_(&arena_.make("tags")), token_(env.deadlines.token()) {
    layer_->setAnchorPoint({0, 0});
    layer_->setBounds({width, 0});
}
FacilityTagStack::~FacilityTagStack() {
    env_->deadlines.cancel(token_);
    tags_.clear(); // each arena detaches its layers from the stack first
}
void FacilityTagStack::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    for (auto& tag : tags_) tag->setLooping(active_ && tag->state != State::collapsing);
}
std::vector<d::Control*> FacilityTagStack::controls() const {
    std::vector<d::Control*> out;
    for (const auto& tag : tags_) if (tag->state == State::live) { out.push_back(tag->control); out.push_back(tag->close); }
    return out;
}
std::uint64_t FacilityTagStack::contentRevision() const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(arena_.revision()); mix(tags_.size());
    for (const auto& tag : tags_) { mix(tag->serial); mix(tag->revision()); }
    return h;
}
FacilityTagStack::Tag* FacilityTagStack::find(std::uint64_t serial) const noexcept {
    for (const auto& tag : tags_) if (tag->serial == serial) return tag.get();
    return nullptr;
}
void FacilityTagStack::dropRemoved() { std::erase_if(tags_, [](const auto& tag) { return tag->state == State::removed; }); }

// update (:46-70).
void FacilityTagStack::update(const std::vector<FacilityAlert>& alerts, bool animated) {
    const bool animate = animated && !env_->reduceMotion;
    std::vector<const FacilityAlert*> wanted;
    for (const auto& alert : alerts)
        if (std::none_of(wanted.begin(), wanted.end(), [&](const FacilityAlert* w) { return w->kind() == alert.kind(); })) wanted.push_back(&alert);
    const auto isWanted = [&](FacilityAlertKind k) { return std::any_of(wanted.begin(), wanted.end(), [k](const FacilityAlert* w) { return w->kind() == k; }); };
    for (auto& tag : tags_) if (tag->state == State::live && !isWanted(tag->kind)) collapse(*tag, animate);
    dropRemoved();
    std::vector<std::unique_ptr<Tag>> live;
    for (const auto* alert : wanted) {
        const auto existing = std::find_if(tags_.begin(), tags_.end(), [&](const auto& t) { return t && t->kind == alert->kind() && t->state == State::live; });
        if (existing != tags_.end()) { (*existing)->render(*alert); live.push_back(std::move(*existing)); }
        else { auto tag = makeTag(*alert); tag->isNew = true; live.push_back(std::move(tag)); }
    }
    // Leaving tags keep their index among the others until they are removed.
    auto order = std::move(live);
    for (std::size_t index = 0; index < tags_.size(); ++index)
        if (tags_[index] && tags_[index]->state != State::live) order.insert(order.begin() + std::ptrdiff_t(std::min(index, order.size())), std::move(tags_[index]));
    tags_ = std::move(order);
    layout(animate);
}
// makeTag (:72-87).
std::unique_ptr<FacilityTagStack::Tag> FacilityTagStack::makeTag(const FacilityAlert& alert) {
    auto tag = std::make_unique<Tag>(alert.kind(), *env_, ++serial_);
    tag->render(alert);
    const auto serial = tag->serial;
    tag->control->onActivate = [this, serial] {
        const auto* t = find(serial);
        if (!t || t->state != State::live) return;
        if (onOpen) onOpen(t->kind);
    };
    tag->close->onActivate = [this, serial] { if (auto* t = find(serial)) dismiss(*t); };
    layer_->add(*tag->slot);
    tag->setLooping(active_);
    return tag;
}
// layout (:91-111): leaving tags in their strike-through hold keep their
// place; collapsing ones give it up.
void FacilityTagStack::layout(bool animated) {
    double y = 0;
    for (auto& tag : tags_) {
        if (tag->state == State::collapsing || tag->state == State::removed) continue;
        const double w = std::min(width, tag->width);
        const d::Point origin{width - w, y};
        if (tag->isNew) {
            tag->isNew = false;
            tag->place(origin);
            if (animated) tag->enter();
        } else if (tag->origin.y != origin.y) {
            tag->move(origin, animated);
        } else if (tag->origin.x != origin.x) {
            tag->place(origin);
        }
        y += tagHeight + gap;
    }
    preferredHeight_ = std::max(0.0, y - gap);
    layer_->setBounds({width, preferredHeight_});
}
// dismiss (:114-133): x: strike-through (.28 s), collapse at 300 ms, removed at 560 ms.
void FacilityTagStack::dismiss(Tag& tag) {
    if (tag.state != State::live) return;
    const auto kind = tag.kind;
    tag.state = State::holding;
    tag.disable();
    if (env_->reduceMotion) {
        collapse(tag, false);
        dropRemoved();
        layout(false);
    } else {
        tag.strike();
        const auto serial = tag.serial;
        env_->deadlines.after(env_->now + .3, token_, [this, serial] {
            auto* t = find(serial);
            if (!t || t->state != State::holding) return;
            collapse(*t, true);
            layout(true);
        });
    }
    if (onDismiss) onDismiss(kind);
}
// collapse (:136-147): .gone (.25 s), then removed.
void FacilityTagStack::collapse(Tag& tag, bool animated) {
    tag.state = State::collapsing;
    tag.disable();
    tag.setLooping(false);
    if (!animated) { remove(tag); return; }
    tag.collapse();
    const auto serial = tag.serial;
    env_->deadlines.after(env_->now + .26, token_, [this, serial] {
        auto* t = find(serial);
        if (!t) return;
        remove(*t);
        std::erase_if(tags_, [serial](const auto& x) { return x->serial == serial; });
    });
}
void FacilityTagStack::remove(Tag& tag) {
    tag.state = State::removed;
    tag.setLooping(false);
    tag.slot->removeFromParent();
}
std::vector<FacilityAlertKind> FacilityTagStack::kinds() const {
    std::vector<FacilityAlertKind> out;
    for (const auto& tag : tags_) if (tag->state == State::live) out.push_back(tag->kind);
    return out;
}
std::vector<FacilityAlertKind> FacilityTagStack::shownKinds() const {
    std::vector<FacilityAlertKind> out;
    for (const auto& tag : tags_) out.push_back(tag->kind);
    return out;
}
std::optional<FacilityTagStack::TagState> FacilityTagStack::tag(FacilityAlertKind kind) const {
    for (const auto& tag : tags_)
        if (tag->kind == kind && tag->state != State::removed)
            return TagState{d::textOf(*tag->text).text(), {tag->origin.x, tag->origin.y, tag->slot->bounds().width, tag->slot->bounds().height}, tag->isLooping(), tag->isStruck};
    return std::nullopt;
}
const d::Layer* FacilityTagStack::tagLayer(FacilityAlertKind kind) const {
    for (const auto& tag : tags_) if (tag->kind == kind && tag->state != State::removed) return tag->layer;
    return nullptr;
}
} // namespace endfield::modules::facility
