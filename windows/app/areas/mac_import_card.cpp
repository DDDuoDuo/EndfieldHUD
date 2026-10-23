#include "app/areas/mac_import_card.hpp"
#include "modules/hud_design_menu.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::areas {
namespace {
namespace d = modules::design;
using Rect = core::Rect;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
void finite(double t) { need(std::isfinite(t), "The import card needs finite caller time"); }
constexpr double fade = .14;                    // the dismissal (input consumed until it ends)
constexpr double inset = 22, rowHeight = 18, listTop = 140, buttonsY = 384, buttonGap = 10;
// The panel in the content box: centred.
constexpr Rect card{(d::metric::boxWidth - macImportCardWidth) / 2, (d::metric::boxHeight - macImportCardHeight) / 2, macImportCardWidth, macImportCardHeight};
constexpr Rect list{inset, listTop, macImportCardWidth - 2 * inset, rowHeight * double(macImportCardRows) + 12};
constexpr double rowTop = listTop + 6;
double split() { return std::round((list.width - 24) * .56); }
d::Color tone(const d::Palette& p, MacImportTone t) {
    switch (t) {
    case MacImportTone::primary: return p.token(d::ThemeToken::textOnDark);
    case MacImportTone::secondary: return p.token(d::ThemeToken::steel);
    case MacImportTone::accent: return p.accentInk();
    case MacImportTone::alert: return p.token(d::ThemeToken::danger);
    }
    return p.token(d::ThemeToken::textOnDark);
}
void setString(d::Layer& text, const std::string& value, std::optional<d::Color> color = {}) {
    const auto& block = d::textOf(text);
    if (block.runs.size() == 1 && block.runs[0].text == value && (!color || block.runs[0].style.color == *color)) return;
    d::editText(text, [&](d::TextBlock& b) { b.setText(value); if (color) b.setColor(*color); });
}
}

// --- State -----------------------------------------------------------------------
void MacImportCardState::setReduceMotion(bool value, double now) {
    finite(now);
    if (reduced_ == value) return;
    reduced_ = value;
    if (value && dismissing_) shown_ = dismissing_ = false;
    changed();
}
void MacImportCardState::setEnabled(std::array<bool, 2> value, double now) {
    finite(now);
    if (value == enabled_) return;
    enabled_ = value;
    if (!enabled_[focused_]) focused_ = enabled_[0] ? 0 : enabled_[1] ? 1 : focused_;
    changed();
}
void MacImportCardState::resetFocus(double now) {
    finite(now);
    const int next = enabled_[0] || !enabled_[1] ? 0 : 1;
    if (next == focused_) return;
    focused_ = next; changed();
}
bool MacImportCardState::focus(int button, double now) {
    finite(now); need(button == 0 || button == 1, "Invalid import card focus");
    if (!shown_ || dismissing_ || !enabled_[static_cast<std::size_t>(button)]) return false;
    if (focused_ != button) { focused_ = button; changed(); }
    return true;
}
void MacImportCardState::show(double now) {
    finite(now);
    if (!shown_) ++showings_;               // re-showing a fading card restores it without a new entrance
    shown_ = true; dismissing_ = false;
    focused_ = enabled_[0] || !enabled_[1] ? 0 : 1;
    changed();
}
void MacImportCardState::dismiss(bool animated, double now) {
    finite(now);
    if (!shown_) return;
    if (!animated || reduced_) { shown_ = dismissing_ = false; changed(); return; }
    if (!dismissing_) { dismissing_ = true; start_ = now; }
    changed();
}
void MacImportCardState::hideImmediately(double now) {
    finite(now);
    if (!shown_ && !dismissing_) return;
    shown_ = dismissing_ = false; changed();
}
bool MacImportCardState::refresh(double now) {
    finite(now);
    if (dismissing_ && now >= start_ + fade) { shown_ = dismissing_ = false; changed(); return true; }
    return false;
}
double MacImportCardState::opacity(double now) const {
    finite(now);
    if (!shown_) return 0;
    if (!dismissing_) return 1;
    return 1 - std::clamp((now - start_) / fade, 0., 1.);
}
bool MacImportCardState::requiresFrames(double now) const { finite(now); return dismissing_ && now < start_ + fade; }
std::optional<MacImportCardAction> MacImportCardState::key(unsigned key, bool repeated, double now) {
    finite(now);
    if (!shown_) return {};
    if (dismissing_) return MacImportCardAction::none;
    switch (key) {
    case 0x26: return MacImportCardAction::scrollUp;   // Up
    case 0x28: return MacImportCardAction::scrollDown; // Down
    case 0x21: return MacImportCardAction::pageUp;     // Page Up
    case 0x22: return MacImportCardAction::pageDown;   // Page Down
    default: break;
    }
    if (repeated) return MacImportCardAction::none;
    switch (key) {
    case 0x1B: return MacImportCardAction::escape;     // Escape
    case 0x09:                                         // Tab
        if (enabled_[1 - focused_]) { focused_ = 1 - focused_; changed(); }
        return MacImportCardAction::none;
    case 0x25: if (enabled_[0] && focused_ != 0) { focused_ = 0; changed(); } return MacImportCardAction::none; // Left
    case 0x27: if (enabled_[1] && focused_ != 1) { focused_ = 1; changed(); } return MacImportCardAction::none; // Right
    case 0x0D: case 0x20:                              // Return/Enter, Space
        if (!enabled_[focused_]) return MacImportCardAction::none;
        return focused_ == 1 ? MacImportCardAction::right : MacImportCardAction::left;
    default: return MacImportCardAction::none;
    }
}

// --- Panel -----------------------------------------------------------------------
MacImportPanel::MacImportPanel(d::Environment& env)
    : env_(env), arena_("macImport", &env.deadlines), builder_{env, arena_, controls_} {
    const auto& p = env.palette;
    root_ = &builder_.layer("root");
    root_->setFrame(d::metric::box);
    root_->setAnimatable();                              // the dismissal fade (pose only)
    panel_ = &builder_.layer("panel");
    d::applyPanelChrome(builder_, *panel_, {card.width, card.height});
    panel_->setOrigin({card.x, card.y});
    panel_->setAnimatable();                             // the pop-over entrance wipe
    panel_->setPartRoot(true);
    root_->add(*panel_);
    // The 62 x 2 accent line, the caption and the step position.
    d::Layer& line = builder_.layer("line");
    line.setFrame({inset, 0, 62, 2});
    d::setBackground(line, p.yellow());
    panel_->add(line);
    panel_->add(d::makeText(builder_, "caption", "ENDFIELDHUD / SYSTEM", d::type::label(p), {inset, 16, card.width / 2 - inset, 12}));
    position_ = &d::makeText(builder_, "position", "", d::type::label(p), {card.width / 2, 16, card.width / 2 - inset, 12}, d::TextAlignment::right);
    panel_->add(*position_);
    title_ = &d::makeText(builder_, "title", "", d::type::confirmTitle(p), {inset, 38, card.width - 2 * inset, 21.25});
    message_ = &d::makeText(builder_, "message", "", d::type::confirmDescription(p), {inset, 66, card.width - 2 * inset, 54});
    d::editText(*message_, [](d::TextBlock& b) { b.maximumLines = 3; });
    panel_->add(*title_); panel_->add(*message_);
    // Progress: a track and an accent fill scaled from the left (pose only).
    track_ = &builder_.layer("track");
    track_->setFrame({inset, 128, card.width - 2 * inset, 2});
    d::setBackground(*track_, p.foreground(.12));
    track_->setAnimatable();
    track_->setValue(d::Channel::opacity, 0);
    fill_ = &builder_.layer("fill");
    fill_->setAnchorPoint({0, .5});
    fill_->setFrame({inset, 128, card.width - 2 * inset, 2});
    d::setBackground(*fill_, p.yellow());
    fill_->setAnimatable();
    fill_->setValue(d::Channel::opacity, 0);
    panel_->add(*track_); panel_->add(*fill_);
    // The list field, its rows and the detail text that replaces them.
    list_ = &builder_.layer("list");
    list_->setFrame(list);
    d::setBorder(*list_, p.token(d::ThemeToken::field), p.token(d::ThemeToken::ringField), 1);
    panel_->add(*list_);
    const double labelWidth = split();
    for (std::size_t n = 0; n < macImportCardRows; ++n) {
        const double y = rowTop + rowHeight * double(n);
        const auto suffix = std::to_string(n);
        rows_[n][0] = &d::makeText(builder_, "row" + suffix, "", d::type::menuItem(p), {list.x + 12, y, labelWidth - 6, rowHeight});
        rows_[n][1] = &d::makeText(builder_, "value" + suffix, "", d::type::dropdownValue(p), {list.x + 12 + labelWidth, y, list.width - 24 - labelWidth, rowHeight},
                                   d::TextAlignment::right);
        panel_->add(*rows_[n][0]); panel_->add(*rows_[n][1]);
    }
    detail_ = &d::makeText(builder_, "detail", "", d::type::confirmDescription(p), {list.x + 12, rowTop, list.width - 24, rowHeight * double(macImportCardRows)});
    d::editText(*detail_, [](d::TextBlock& b) { b.maximumLines = int(macImportCardRows); });
    detail_->setHidden(true);
    panel_->add(*detail_);
    // The two buttons and their keyboard focus rings.
    for (int n = 0; n < 2; ++n) {
        auto& button = builder_.make<d::Button>(n ? "confirm" : "cancel", "", std::nullopt, n ? d::Button::Kind::primary : d::Button::Kind::secondary);
        button.onActivate = [this, n] { if (onActivate) onActivate(n); };
        buttons_[std::size_t(n)] = &button;
        d::Layer& ring = builder_.layer(n ? "confirm.focus" : "cancel.focus");
        ring.setAnimatable();
        ring.setValue(d::Channel::opacity, 0);
        rings_[std::size_t(n)] = &ring;
        panel_->add(ring); panel_->add(button.layer());
    }
    router_.set({buttons_[0], buttons_[1]});
    placeButtons();
}
MacImportPanel::~MacImportPanel() = default;

// Right-aligned on the panel: primary at the right inset, secondary before it.
void MacImportPanel::placeButtons() {
    const double confirmWidth = buttons_[1]->preferredWidth(), cancelWidth = buttons_[0]->preferredWidth();
    const double confirmX = card.width - inset - confirmWidth;
    buttons_[1]->place({confirmX, buttonsY});
    buttons_[0]->place({confirmX - buttonGap - cancelWidth, buttonsY});
    for (std::size_t n = 0; n < 2; ++n) {
        const auto f = buttons_[n]->layer().frame();
        rings_[n]->setFrame({f.x - 3, f.y - 3, f.width + 6, f.height + 6});
        d::setBorder(*rings_[n], std::nullopt, env_.palette.accentInk(), 1);
    }
}

void MacImportPanel::setContent(const MacImportCardContent& c) {
    if (content_ && *content_ == c) return;
    const auto& p = env_.palette;
    const bool first = !content_;
    setString(*title_, c.title);
    setString(*message_, c.message);
    setString(*position_, c.position);
    setString(*detail_, c.detail);
    const bool detail = !c.detail.empty();
    if (detail_->hidden() == detail) detail_->setHidden(!detail);
    for (std::size_t n = 0; n < macImportCardRows; ++n) {
        const auto& row = c.rows[n];
        setString(*rows_[n][0], row.label);
        setString(*rows_[n][1], row.value, tone(p, row.tone));
        for (auto* text : rows_[n]) if (text->hidden() != detail) text->setHidden(detail);
    }
    bool titles = first;
    for (std::size_t n = 0; n < 2; ++n) {
        if (buttons_[n]->title() != c.buttons[n]) { buttons_[n]->setTitle(c.buttons[n]); titles = true; }
        if (buttons_[n]->enabled() != c.enabled[n]) buttons_[n]->setEnabled(c.enabled[n]);
    }
    if (titles) placeButtons();
    progress_ = c.progress;
    content_ = c;
}

void MacImportPanel::pose(const MacImportCardState& s, double now) {
    root_->setValue(d::Channel::opacity, s.opacity(now));
    const bool shows = progress_.has_value();
    track_->setValue(d::Channel::opacity, shows ? 1 : 0);
    const double value = shows ? std::clamp(*progress_, 0., 1.) : 0;
    fill_->setValue(d::Channel::opacity, shows && value > 0 ? 1 : 0);
    fill_->setValue(d::Channel::scaleX, std::max(value, 1e-3));
    for (int n = 0; n < 2; ++n)
        rings_[std::size_t(n)]->setValue(d::Channel::opacity, s.presented() && !s.dismissing() && s.focused() == n && s.enabled()[std::size_t(n)] ? 1 : 0);
}

void MacImportPanel::reveal(const d::motion::Clock& clock) { d::motion::wipe(*panel_, d::WipeOrigin::top, d::duration::menuWipe, clock, d::easing::standard, 0, .4); }

MacImportCardGeometry MacImportPanel::geometry() const {
    MacImportCardGeometry g;
    g.card = card;
    g.cancel = buttons_[0]->layer().frame();
    g.confirm = buttons_[1]->layer().frame();
    g.list = list;
    g.rowTop = rowTop;
    g.rowHeight = rowHeight;
    g.title = d::textOf(*title_).lineFrame;
    g.message = d::textOf(*message_).lineFrame;
    g.bar = track_->frame();
    g.detail = d::textOf(*detail_).lineFrame;
    g.position = d::textOf(*position_).lineFrame;
    return g;
}
} // namespace endfield::areas
