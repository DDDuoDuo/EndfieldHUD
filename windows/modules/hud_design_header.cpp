// The shared module header: the port of Sources/HUDDesignHeader.swift
// (HUDModuleHeader :31-167; HUDModuleTitle :7-25 is ModuleTitle in
// hud_design_text.cpp). The header outlives pages (configure runs on every
// buildPage), so its crumb layers are a pool reused by index instead of new
// layers per build, and the side actions (page controls whose layers the page
// arena detaches when the page dies) are removed through the actions layer
// rather than through possibly stale control pointers.
#include "modules/hud_design_header.hpp"
#include <algorithm>

namespace endfield::modules::design {
namespace {
constexpr double width = metric::boxWidth;
}

ModuleHeader::ModuleHeader(Environment& env, std::string_view idPrefix)
    : arena_(std::string(idPrefix), &env.deadlines), builder_{env, arena_, controls_} {
    Builder& b = builder_;
    layer_ = &b.layer("header");
    crumbs_ = &b.layer("header.crumbs");
    code_ = &makeText(b, "header.code", "", type::breadcrumb(env.palette), {}, TextAlignment::right);
    tail_ = &b.layer("header.tail");
    titleGroup_ = &b.layer("header.title");
    title_ = &makeText(b, "header.title.text", "", type::moduleTitle(env.palette), {});
    subtitle_ = &makeText(b, "header.title.subtitle", "", type::titleSubtitle(env.palette), {});
    actions_ = &b.layer("header.actions");
    rule_ = &b.layer("header.rule");
    ruleLine_ = &b.layer("header.rule.line");
    lead_ = &b.layer("header.rule.lead");
    ladder_ = &b.layer("header.rule.ladder");
    counterPlate_ = &b.layer("header.rule.plate");
    counter_ = &makeText(b, "header.rule.counter", "", type::ruleCounter(env.palette), {});
    layer_->setFrame({0, 0, width, metric::headerHeight});
    layer_->setPartRoot(true);                           // outlives pages: its own part
    for (Layer* child : {crumbs_, code_, tail_, titleGroup_, actions_, rule_}) layer_->add(*child);
    titleGroup_->add(*title_); titleGroup_->add(*subtitle_);
    titleGroup_->setAnimatable();                        // the title wipe with fade
    rule_->setAnimatable();                              // the rule grows from the left
    rule_->setAnchorPoint({0, .5});
    rule_->setFrame({0, 60, width, 9});
    ruleLine_->setFrame({0, 4, width, 1});
    lead_->setFrame({0, 3, 56, 3});
    ladder_->setFrame({width - 46 - 90, 0, 90, 9});
    for (Layer* child : {ruleLine_, lead_, ladder_, counterPlate_, counter_}) rule_->add(*child);
    tail_->setFrame({width - 34, 3.5, 34, 7});
    applyPalette();
}
ModuleHeader::~ModuleHeader() = default;

// configure (:74-128).
void ModuleHeader::configure(std::vector<std::string> crumbs, std::string code, const ModuleTitle& moduleTitle, std::string counter,
    bool showsBack, std::vector<Control*> actions) {
    Environment& env = builder_.env;
    const auto& p = env.palette;
    std::vector<std::string> parts{"// HUD"};
    for (auto& crumb : crumbs) { parts.emplace_back("/"); parts.push_back(std::move(crumb)); }
    double x = 0;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const bool current = index == parts.size() - 1 && index > 0;
        if (index == crumbPool_.size()) {
            crumbPool_.push_back(&makeText(builder_, "header.crumb." + std::to_string(index), "", type::breadcrumb(p), {}));
            crumbs_->add(*crumbPool_.back());
        }
        Layer& text = *crumbPool_[index];
        editText(text, [&](TextBlock& block) {
            block.runs = {{std::move(parts[index]), current ? type::breadcrumb(p).colored(p.token(ThemeToken::crumbCurrent)) : type::breadcrumb(p)}};
            const double w = preferredWidth(block, env.text);
            block.setLineFrame({x, 1.75, w + 1, 10.5});
            x += w + 6;
        });
        if (text.hidden()) text.setHidden(false);
    }
    crumbCount_ = parts.size();
    for (std::size_t index = crumbCount_; index < crumbPool_.size(); ++index) if (!crumbPool_[index]->hidden()) crumbPool_[index]->setHidden(true);
    crumbs_->setFrame({0, 0, width, 14});
    editText(*code_, [&](TextBlock& block) {
        block.setText(std::move(code));
        const double w = preferredWidth(block, env.text);
        block.setLineFrame({width - 34 - 6 - w - 1, 1.75, w + 1, 10.5});
    });
    if (showsBack && !back_) {
        // The Mac makes a new button here (with its init colour fade); the kept one replays it.
        if (!backButton_) backButton_ = &builder_.make<IconButton>("back", core::localized("Back", "返回", env.language), Icon::back, IconButton::Style::back);
        else { backButton_->label = core::localized("Back", "返回", env.language); backButton_->playCreationFade(); }
        back_ = backButton_;
        layer_->add(back_->layer());
    } else if (!showsBack && back_) {
        back_->layer().removeFromParent(); back_->clearInteractionState(); back_->onActivate = nullptr;
        back_ = nullptr;
    }
    if (back_) back_->layer().setFrame({0, 20, 30, 30});
    const auto lines = moduleTitle.lines(env.language);
    editText(*title_, [&](TextBlock& block) { block.setText(lines.title); block.language = lines.titleLanguage; });
    editText(*subtitle_, [&](TextBlock& block) { block.setText(lines.subtitle); block.language = std::nullopt; });
    const double titleX = showsBack ? 40 : 0;
    const double titleWidth = preferredWidth(textOf(*title_), env.text), subtitleWidth = preferredWidth(textOf(*subtitle_), env.text);
    // Baselines align: both line boxes share the title row's baseline.
    const auto titleMetrics = env.text.metrics(type::moduleTitle(p), lines.titleLanguage);
    const auto subMetrics = env.text.metrics(type::titleSubtitle(p), std::nullopt);
    const double baseline = 35 - titleMetrics.height / 2 + titleMetrics.baseline();
    titleGroup_->setFrame({titleX, 20, titleWidth + 12 + subtitleWidth + 4, 30});
    setLine(*title_, {0, 15 - titleMetrics.height / 2, titleWidth + 1, titleMetrics.height});
    setLine(*subtitle_, {titleWidth + 12, baseline - 20 - subMetrics.baseline(), subtitleWidth + 1, subMetrics.height});
    for (Layer* child : std::vector<Layer*>(actions_->children())) child->removeFromParent();
    actionControls_ = std::move(actions);
    double right = width;
    for (auto it = actionControls_.rbegin(); it != actionControls_.rend(); ++it) {
        Layer& control = (*it)->layer();
        const auto size = control.bounds();
        right -= size.width;
        control.setFrame({right, 35 - size.height / 2, size.width, size.height});
        actions_->add(control);
        right -= 8;
    }
    actions_->setFrame(localBounds(*layer_));
    setCounter(std::move(counter));
}
// applyPalette (:132-143): the breadcrumb path is recoloured by the next configure.
void ModuleHeader::applyPalette() {
    const auto& p = builder_.env.palette;
    setBackground(*ruleLine_, p.foreground(.22));
    setBackground(*lead_, p.yellow());                   // the fill, not the Light ink (LIB-23)
    setPath(*ladder_, tickLadder(90, 9, 9), p.foreground(.35));
    setBackground(*counterPlate_, fixed::counterBackground);
    editText(*counter_, [&](TextBlock& block) { block.setStyle(type::ruleCounter(p)); });
    editText(*code_, [&](TextBlock& block) { block.setStyle(type::breadcrumb(p)); });
    editText(*title_, [&](TextBlock& block) { block.setStyle(type::moduleTitle(p)); });
    editText(*subtitle_, [&](TextBlock& block) { block.setStyle(type::titleSubtitle(p)); });
    tail_->setContent(hatch(tail_->id(), {0, 0, 34, 7}, Hatch::light, p));
}
// setCounter (:146-154): in place (TODO count, volume, pins, export %).
void ModuleHeader::setCounter(std::string value) {
    const auto& text = builder_.env.text;
    editText(*counter_, [&](TextBlock& block) {
        block.setText(std::move(value));
        const double w = preferredWidth(block, text);
        block.setLineFrame({width - w - 1, -1.5, w + 1, 11});
        counterPlate_->setFrame({width - w - 6 - 1, -2, w + 6 + 1, 12});
    });
    setBackground(*counterPlate_, fixed::counterBackground);
}
std::vector<Control*> ModuleHeader::controls() const {
    std::vector<Control*> out;
    if (back_) out.push_back(back_);
    out.insert(out.end(), actionControls_.begin(), actionControls_.end());
    return out;
}
// playEntrance (:159-162).
void ModuleHeader::playEntrance(const motion::Clock& clock) {
    motion::wipe(*titleGroup_, WipeOrigin::left, duration::titleWipe, clock, easing::standard, 0, 0.0);
    motion::grow(*rule_, duration::ruleWipe, clock, duration::ruleDelay);
}
std::pair<std::string, std::string> ModuleHeader::titleTexts() const { return {textOf(*title_).text(), textOf(*subtitle_).text()}; }
std::string ModuleHeader::counterText() const { return textOf(*counter_).text(); }
std::vector<std::string> ModuleHeader::crumbTexts() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < crumbCount_; ++i) out.push_back(textOf(*crumbPool_[i]).text());
    return out;
}
} // namespace endfield::modules::design
