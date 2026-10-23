// The facility alert banner: the port of Sources/HUDFacilityBanner.swift
// (:21-351). Timers are Environment::deadlines entries on the caller's clock.
#include "modules/facility_banner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace endfield::modules::facility {
namespace {
using d::Channel;
// The banner's text styles (:135-141).
d::TextStyle labelStyle() { return {10, d::FontWeight::bold, d::fixed::orange, 1, .18}; }
d::TextStyle titleStyle() { return {17, d::FontWeight::bold, d::rgb(0xF4F4F4), 1, .08}; }
d::TextStyle detailStyle() { return {11, d::FontWeight::regular, d::rgb(0x9A9A9A)}; }
d::TextStyle valueStyle() { auto s = d::TextStyle{34, d::FontWeight::light, d::fixed::orange}; s.tabular = true; return s; }
d::TextStyle unitStyle() { return {9, d::FontWeight::bold, d::rgb(0x9A9A9A), 1.4, .14}; }
d::PathBuilder rectPath(d::Rect r) { return d::PathBuilder{}.rect(r); }
}

bool FacilityControl::contains(d::Point p) const {
    if (!Control::contains(p)) return false;
    return !hitPath || hitPath->contains(p);
}

// ---------------------------------------------------------------------------
// HUDHazardStripes (:38-89)
// ---------------------------------------------------------------------------
// The band covers one extra period on the left of the clip, so its drift of
// up to one period never uncovers the edge (the Mac's path reaches past the
// layer's bounds; the rasterizer paints a node inside its frame only).
HazardStripes::HazardStripes(d::Builder& b, std::string_view id, d::Size size, double period, d::Color color, double cycle)
    : layer_(&b.layer(id)), band_(&b.layer(std::string(id) + ".band")), period_(period), cycle_(cycle) {
    layer_->setFrame({0, 0, size.width, size.height});
    layer_->setMasksToBounds(true);
    d::setBackground(*layer_, d::fixed::nearBlack);
    band_->setFrame({-period, 0, size.width + period, size.height});
    band_->setAnimatable();                              // hzMove: translateX 0 -> period, linear, repeating
    d::setPath(*band_, path(size, period).transformed(1, 0, 0, 1, period, 0), color);
    layer_->add(*band_);
}
bool HazardStripes::isRunning() const noexcept {
    const auto& slot = band_->slots[std::size_t(d::MotionSlot::effect)];
    const auto& channels = band_->slotChannels[std::size_t(d::MotionSlot::effect)];
    for (std::size_t i = 0; i < slot.size(); ++i) if (slot[i].present() && channels[i] == Channel::translateX) return true;
    return false;
}
void HazardStripes::start(d::Environment& env) {
    if (env.reduceMotion) { stop(); return; }
    if (isRunning()) return;
    const std::array<double, 2> values{0, period_};
    d::motion::keyframes(*band_, Channel::translateX, values, {}, cycle_, env.clock(), d::easing::linear, std::numeric_limits<double>::infinity());
}
void HazardStripes::stop() { d::motion::release(*band_, d::MotionSlot::effect); }
d::PathBuilder HazardStripes::path(d::Size size, double p) {
    d::PathBuilder out;
    const double h = size.height;
    for (int k = -2; double(k) * p + p / 2 - h <= size.width + p; ++k) {
        const double x0 = double(k) * p + p / 2, x1 = double(k + 1) * p;
        out.move({x0, 0}).line({x1, 0}).line({x1 - h, h}).line({x0 - h, h}).close();
    }
    return out;
}

// ---------------------------------------------------------------------------
// HUDFacilityBanner (:97-351)
// ---------------------------------------------------------------------------
FacilityBanner::FacilityBanner(d::Environment& env)
    : env_(&env), arena_("facility", &env.deadlines), builder_{env, arena_, controls_} {
    const d::Rect bounds{0, 0, size.width, size.height};
    layer_ = &builder_.layer("banner");
    plate_ = &builder_.layer("banner.plate");
    fill_ = &builder_.layer("banner.fill");
    hazard_ = std::make_unique<HazardStripes>(builder_, "banner.hazard", d::Size{78, size.height}, 12 * std::sqrt(2.0), d::fixed::orange, 1.2);
    label_ = &d::makeText(builder_, "banner.label", "", labelStyle(), {});
    name_ = &d::makeText(builder_, "banner.name", "", labelStyle().colored(d::rgb(0x777777)), {});
    title_ = &d::makeText(builder_, "banner.title", "", titleStyle(), {});
    detail_ = &d::makeText(builder_, "banner.detail", "", detailStyle(), {});
    value_ = &d::makeText(builder_, "banner.value", "", valueStyle(), {}, d::TextAlignment::right);
    unit_ = &d::makeText(builder_, "banner.unit", "", unitStyle(), {}, d::TextAlignment::right);
    rule_ = &builder_.layer("banner.countdown");
    layer_->setAnimatable();                             // shown and hidden as a pose
    layer_->setBounds(size); layer_->setHidden(true);
    plate_->setAnimatable();                             // fbIn drop + unfold (wipe), fbOut rise + fade
    plate_->setFrame(bounds);
    fill_->setFrame(bounds);
    // linear-gradient(#1d1d1d, #121212) with inset 0 0 0 1px #4a3216 around the whole plate.
    const std::array<d::Color, 2> colors{d::rgb(0x1D1D1D), d::rgb(0x121212)};
    auto node = d::gradient(fill_->id(), bounds, colors, {}, {.5, 0}, {.5, 1});
    d::addChildren(node, {d::bordered(fill_->id() + ".border", bounds, std::nullopt, d::rgb(0x4A3216), 1)});
    fill_->setContent(std::move(node));
    hazard_->layer().setOrigin({10, 0});
    auto& ring = builder_.layer("banner.hazard.ring");
    ring.setFrame({17, 13, 44, 44}); d::setBackground(ring, d::fixed::orange);
    auto& box = builder_.layer("banner.hazard.box");
    box.setFrame({19, 15, 40, 40}); d::setBackground(box, d::fixed::nearBlack);
    auto& warn = d::makeIcon(builder_, "banner.hazard.warn", d::Icon::warn, 22, d::fixed::orange);
    warn.setPosition({20, 20});
    box.add(warn);
    hazard_->layer().add(ring); hazard_->layer().add(box);
    rule_->setAnimatable();                              // the 5 s countdown: scaleX from the left edge
    rule_->setAnchorPoint({0, .5});
    rule_->setFrame({28, size.height - 2, size.width - 56, 2});
    d::setBackground(*rule_, d::fixed::orange);
    for (d::Layer* child : {fill_, &hazard_->layer(), label_, name_, title_, detail_, value_, unit_, rule_}) plate_->add(*child);
    layer_->add(*plate_);
    hit_ = &controls_.make<FacilityControl>(env, "facility.banner", *layer_);
    hit_->setEnabled(false);
    hit_->setTarget(plateRect(1), rectPath(plateRect(1)));
    hit_->hitPath = rectPath(plateRect(1));
    hit_->onHover = [this](bool on) { setHovered(on); };
    hit_->onActivate = [this] { click(); };
}
FacilityBanner::~FacilityBanner() { cancel(expiry_); cancel(removal_); }
void FacilityBanner::cancel(d::Deadlines::Token& token) noexcept { if (token) env_->deadlines.cancel(token); token = 0; }
d::Rect FacilityBanner::plateRect(double unfolded) noexcept { return {0, 0, size.width * std::clamp(unfolded, 0.0, 1.0), size.height}; }

// show (:180-187).
void FacilityBanner::show(const FacilityAlert& alert) {
    if (current_ && current_->kind() == alert.kind() && !leaving_) { current_ = alert; render(alert); return; }
    for (auto& queued : queue_) if (queued.kind() == alert.kind()) { queued = alert; return; }
    if (showing_) { queue_.push_back(alert); return; }
    present(alert);
}
// hide (:190-194).
void FacilityBanner::hide(bool animated) {
    queue_.clear();
    if (!showing_) return;
    leave(animated);
}
// withdraw (:198-201).
void FacilityBanner::withdraw(FacilityAlertKind kind) {
    std::erase_if(queue_, [kind](const FacilityAlert& a) { return a.kind() == kind; });
    if (current_ && current_->kind() == kind && !leaving_) leave(true);
}
// present (:205-230): fbIn drops from -90 px over 60 % of the run while the
// clip unfolds from the left edge (.42 s SNAP).
void FacilityBanner::present(const FacilityAlert& alert) {
    cancel(removal_);
    current_ = alert; showing_ = true; leaving_ = false;
    render(alert);
    plate_->removeAllMotion(); rule_->removeAllMotion();
    layer_->setHidden(false);
    plate_->setValue(Channel::opacity, 1); plate_->setValue(Channel::translateY, 0);
    rule_->setValue(Channel::scaleX, 1);
    hit_->setEnabled(true);
    hazard_->start(*env_);
    const auto clock = env_->clock();
    if (!clock.reduceMotion) {
        plate_->setValue(Channel::translateY, -90);
        d::motion::animate(*plate_, Channel::translateY, 0, .42 * .6, clock, d::easing::snap);
        d::motion::wipe(*plate_, d::WipeOrigin::left, .42, clock, d::easing::snap);
    }
    remaining_ = lifetime; runningSince_.reset();
    if (!hovered_) resumeCountdown();
}
// render (:232-259): outside English the game's bilingual pattern (the English
// name beside the label, the UI-language title below); English shows English only.
void FacilityBanner::render(const FacilityAlert& alert) {
    const auto language = env_->language;
    const bool english = language == core::Language::english;
    const auto& text = env_->text;
    const auto labelText = core::localized("FACILITY ALERT", "设施警报", language);
    const auto detail = alert.detail(language), value = alert.value(language), unit = alert.unit(language);
    const auto width = [&](const std::string& s, const d::TextStyle& style, d::TextAlignment a = d::TextAlignment::left) {
        return d::preferredWidth(d::TextBlock(s, style, a), text);
    };
    const double right = size.width - 40;
    const double column = std::ceil(std::max(width(value, valueStyle()), width(unit, unitStyle())));
    const double x = 104, available = std::max(40.0, right - column - 16 - x);
    const double top = detail.empty() ? 18.5 : 10.5;
    const double labelWidth = std::min(available, width(labelText, labelStyle()));
    const auto set = [](d::Layer& l, std::string s, d::Rect line) { d::editText(l, [&](d::TextBlock& b) { b.setText(std::move(s)); b.setLineFrame(line); }); };
    set(*label_, labelText, {x, top, labelWidth, 10});
    set(*name_, english ? std::string{} : alert.englishTitle(), {x + labelWidth + 10, top, std::max(0.0, available - labelWidth - 10), 10});
    set(*title_, english ? alert.englishTitle() : alert.title(language), {x, top + 16, available, 17});
    set(*detail_, detail, {x, top + 38, available, 11});
    set(*value_, value, {right - column, 11.7, column, 34});
    set(*unit_, unit, {right - column, 45.7, column, 12.6});
    detail_->setHidden(detail.empty());
    hit_->label = labelText + ", " + alert.title(language) + ", " + value + " " + unit;
}
// click (:261-265).
void FacilityBanner::click() {
    if (!current_ || !showing_ || leaving_) return;
    const auto kind = current_->kind();
    leave(true);
    if (onClick) onClick(kind);
}
// leave (:267-286): fbOut rises 90 px and fades (.26 s ease-in), removed at 260 ms.
void FacilityBanner::leave(bool animated) {
    if (!showing_ || leaving_) {
        if (leaving_ && !animated) finishLeaving();
        return;
    }
    leaving_ = true;
    pauseCountdown(); remaining_ = 0;
    hit_->setEnabled(false);
    hit_->clearInteractionState();
    const auto clock = env_->clock();
    if (!animated || clock.reduceMotion) { finishLeaving(); return; }
    d::motion::animate(*plate_, Channel::translateY, -90, .26, clock, d::easing::easeIn);
    d::motion::animate(*plate_, Channel::opacity, 0, .26, clock, d::easing::easeIn);
    removal_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + .26, removal_, [this] { removal_ = 0; finishLeaving(); });
}
// finishLeaving (:288-297): then the next queued alert, or idle.
void FacilityBanner::finishLeaving() {
    cancel(removal_);
    hazard_->stop();
    plate_->removeAllMotion(); rule_->removeAllMotion();
    layer_->setHidden(true);
    leaving_ = false; current_.reset();
    if (!queue_.empty()) {
        const auto next = queue_.front();
        queue_.erase(queue_.begin());
        present(next);
        return;
    }
    showing_ = false;
    if (onHidden) onHidden();
}
// Countdown (:299-332): fbCd 5 s linear, paused while hovered.
void FacilityBanner::setHovered(bool on) {
    if (on == hovered_) return;
    hovered_ = on;
    if (!showing_ || leaving_) return;
    if (on) pauseCountdown(); else resumeCountdown();
}
void FacilityBanner::resumeCountdown() {
    if (runningSince_ || !(remaining_ > 0)) return;
    const auto clock = env_->clock();
    runningSince_ = clock.now;
    expiry_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + remaining_, expiry_, [this] {
        expiry_ = 0; runningSince_.reset(); remaining_ = 0;
        leave(true);
    });
    // Reduce Motion keeps the rule whole: it is a readout, not a motion.
    if (clock.reduceMotion) return;
    // From the rule's current scale (remaining / lifetime) to 0 over what is left.
    d::motion::animate(*rule_, Channel::scaleX, 0, remaining_, clock, d::easing::linear);
}
void FacilityBanner::pauseCountdown() {
    cancel(expiry_);
    if (!runningSince_) return;
    const double since = *runningSince_;
    runningSince_.reset();
    remaining_ = std::max(0.0, remaining_ - (env_->now - since));
    // The countdown animation is removed; the rule keeps the remaining share
    // (unchanged under Reduce Motion).
    rule_->setValue(Channel::scaleX, env_->reduceMotion ? rule_->value(Channel::scaleX) : remaining_ / lifetime);
}
double FacilityBanner::countdownRemaining() const noexcept {
    return runningSince_ ? std::max(0.0, remaining_ - (env_->now - *runningSince_)) : remaining_;
}
std::array<std::string, 6> FacilityBanner::texts() const {
    return {d::textOf(*label_).text(), d::textOf(*name_).text(), d::textOf(*title_).text(), d::textOf(*detail_).text(),
        d::textOf(*value_).text(), d::textOf(*unit_).text()};
}
} // namespace endfield::modules::facility
