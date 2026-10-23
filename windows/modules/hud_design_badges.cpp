// Static badges, readouts and empty states: ports of Sources/HUDDesignBadges.swift
// (HUDChip :5-47, HUDBadge :50-94, HUDEmptyState :242-298, HUDBigNumeral
// :461-488, HUDStatPlate :491-541, HUDCells :545-605).
//
// HUDCells' fill colour changes (.3 / .2 s ease) and the charging pulse are
// tints of a white cell surface; the border sits on its own static child above
// it, as a CALayer border draws above the layer's background. The stat plate's
// border likewise is a child above the corner mark (the Mac's border covers the
// L's outer pixel row).
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace endfield::modules::design {
namespace {
constexpr double pi = 3.14159265358979323846;
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
}

// ---------------------------------------------------------------------------
// HUDChip (:5-47)
// ---------------------------------------------------------------------------
Chip::Chip(Builder& b, std::string_view id, std::string title, Variant variant, std::optional<Icon> icon, std::optional<std::pair<Color, Color>> custom)
    : builder_(&b), layer_(&b.layer(id)), variant_(variant), icon_(icon), custom_(custom) {
    const auto [fill, ink, ring] = colors();
    text_ = &makeText(b, sub(id, "chip.text"), "", type::chip().colored(ink), {});
    layer_->add(*text_);
    if (icon) { glyph_ = &makeIcon(b, sub(id, "chip.icon"), *icon, 10, ink); layer_->add(*glyph_); }
    setTitle(std::move(title));
}
// Variant.colors (:9-18): fill, text, optional ring.
std::tuple<Color, Color, std::optional<Color>> Chip::colors() const {
    const auto& p = builder_->env.palette;
    switch (variant_) {
    case Variant::yellow: return {p.yellow(), fixed::nearBlack, std::nullopt};
    case Variant::dark: return {fixed::counterBackground, fixed::onDarkTag, std::nullopt};
    case Variant::light: return {fixed::plate, fixed::nearBlack, std::nullopt};
    case Variant::outline: return {clear, p.tone(0xCFCFCF), p.tone(0x5A5A5A)};
    case Variant::cyan: return {fixed::cyan, rgb(0x06263A), std::nullopt};
    case Variant::custom: break;
    }
    const auto pair = custom_.value_or(std::pair<Color, Color>{clear, fixed::nearBlack});
    return {pair.first, pair.second, std::nullopt};
}
void Chip::setTitle(std::string title) {
    editText(*text_, [&](TextBlock& block) { block.setText(std::move(title)); });
    double x = 7;
    if (glyph_) { glyph_->setFrame({x, 4, 10, 10}); x += 15; }
    const double width = preferredWidth(textOf(*text_), builder_->env.text);
    setLine(*text_, {x, 4.25, width + 1, 9.5});
    layer_->setBounds({std::ceil(x + width + 7), 18});
    const auto [fill, ink, ring] = colors();
    if (ring) setBorder(*layer_, fill, *ring, 1); else setBackground(*layer_, fill);
}

// ---------------------------------------------------------------------------
// HUDBadge (:50-94)
// ---------------------------------------------------------------------------
namespace badge {
Layer& newBadge(Builder& b, std::string_view id) {
    Layer& badge = b.layer(id);
    TextBlock block("NEW", type::newBadge(), TextAlignment::center);
    const double width = std::ceil(preferredWidth(block, b.env.text)) + 10;
    badge.setBounds({width, 12});
    setBorder(badge, b.env.palette.yellow(), fixed::nearBlack, 1.5);
    badge.add(makeText(b, sub(id, "text"), "NEW", type::newBadge(), {0, 2, width, 8}, TextAlignment::center));
    badge.setBaseTransform(Affine::skew(std::tan(-8 * pi / 180))); // skew.m21 = tan(-8 deg)
    badge.setZPosition(2);
    return badge;
}
// The bordered stamp rotated -4 deg; callers pop it in (HUDMotion.pop), so it is animatable.
Layer& stamp(Builder& b, std::string_view id, std::string title) {
    Layer& stamp = b.layer(id);
    const auto style = type::stamp(b.env.palette);
    TextBlock block(title, style, TextAlignment::center);
    const double width = std::ceil(preferredWidth(block, b.env.text)) + 16 + 4;
    stamp.setAnimatable();
    stamp.setBounds({width, 20});
    setBorder(stamp, std::nullopt, b.env.palette.accentInk(), 2);
    stamp.add(makeText(b, sub(id, "text"), std::move(title), style, {0, 5, width, 10}, TextAlignment::center));
    stamp.setBaseTransform(Affine::rotation(-4 * pi / 180));
    return stamp;
}
Layer& diamond(Builder& b, std::string_view id, double size) {
    Layer& dot = b.layer(id);
    dot.setBounds({size, size});
    setBackground(dot, fixed::orange);
    dot.setBaseTransform(Affine::rotation(pi / 4));
    return dot;
}
}

// ---------------------------------------------------------------------------
// HUDEmptyState (:242-298)
// ---------------------------------------------------------------------------
namespace emptyState {
// bilingual (:246-250, round 5 owner decision): English inside the brackets
// always; other UI languages add their own translation as the small line.
Layer& bilingual(Builder& b, std::string_view id, std::string_view english, std::string_view simplifiedChinese, Size size, double reserving) {
    const bool subtitle = bilingualHasSubtitle(b.env.language);
    return make(b, id, std::string(english), subtitle ? std::optional<std::string>(core::localized(english, simplifiedChinese, b.env.language)) : std::nullopt,
        std::nullopt, size, reserving);
}
bool bilingualHasSubtitle(core::Language language) noexcept { return language != core::Language::english; }
Layer& make(Builder& b, std::string_view id, std::string title, std::optional<std::string> subtitle, std::optional<TextRegion> subtitleLanguage,
    Size size, double reserving) {
    const auto& p = b.env.palette;
    Layer& root = b.layer(id);
    root.setBounds(size);
    Json panel = hatchPanel(root.id(), {0, 0, size.width, size.height}, Hatch::light, p);
    addChildren(panel, {bordered(root.id() + ".ring", {0, 0, size.width, size.height}, std::nullopt, p.token(ThemeToken::ringField), 1)});
    root.setContent(std::move(panel));
    const auto titleStyle = type::emptyTitle(p), bracket = titleStyle.colored(p.accentInk());
    const double top = (size.height - textHeight(subtitle.has_value()) - reserving) / 2;
    TextBlock block("", titleStyle, TextAlignment::center);
    block.runs = {{"[ ", bracket}, {std::move(title), titleStyle}, {" ]", bracket}};
    block.setLineFrame({0, top, size.width, 20});
    Layer& text = b.layer(sub(id, "empty.title"));
    text.setText(std::move(block));
    root.add(text);
    if (subtitle) {
        TextBlock line(std::move(*subtitle), type::labelCJK(p), TextAlignment::center);
        line.language = subtitleLanguage;
        line.setLineFrame({0, top + 30, size.width, 11.5});
        Layer& subtitleLayer = b.layer(sub(id, "empty.subtitle"));
        subtitleLayer.setText(std::move(line));
        root.add(subtitleLayer);
    }
    return root;
}
double textHeight(bool subtitle) noexcept { return subtitle ? 20 + 10 + 11.5 : 20; }
double textBottom(Size size, bool subtitle, double reserving) noexcept { return (size.height - textHeight(subtitle) - reserving) / 2 + textHeight(subtitle); }
Layer& crosshair(Builder& b, std::string_view id) {
    Layer& mark = b.layer(id);
    mark.setBounds({11, 11});
    setPath(mark, design::crosshair(), b.env.palette.foreground(.45));
    return mark;
}
}

// ---------------------------------------------------------------------------
// HUDBigNumeral (:461-488)
// ---------------------------------------------------------------------------
BigNumeral::BigNumeral(Builder& b, std::string_view id, double size) : builder_(&b), layer_(&b.layer(id)), size_(size) {
    const auto& p = b.env.palette;
    value_ = &makeText(b, sub(id, "numeral.value"), "", type::numeral(p).sized(size), {});
    unit_ = &makeText(b, sub(id, "numeral.unit"), "", type::numeralUnit(p).sized(size * .34), {});
    layer_->add(*value_); layer_->add(*unit_);
}
// `numberTone` colours the digits too (Battery's level tone); nullopt keeps titleText.
void BigNumeral::set(std::string number, std::string unit, std::optional<Color> tone, std::optional<Color> numberTone) {
    const auto& p = builder_->env.palette;
    const auto& text = builder_->env.text;
    const auto numeral = type::numeral(p).sized(size_);
    editText(*value_, [&](TextBlock& block) { block.setStyle(numeral.colored(numberTone ? p.legible(*numberTone) : numeral.color)); block.setText(std::move(number)); });
    editText(*unit_, [&](TextBlock& block) {
        block.setStyle(type::numeralUnit(p).sized(size_ * .34).colored(p.legible(tone.value_or(p.yellow())))); block.setText(std::move(unit));
    });
    const double width = preferredWidth(textOf(*value_), text), unitWidth = preferredWidth(textOf(*unit_), text);
    setLine(*value_, {0, 0, width + 1, size_ * .9});
    setLine(*unit_, {width + 4, size_ * .08, unitWidth + 1, size_ * .34});
    layer_->setBounds({width + 4 + unitWidth, size_ * .9});
}

// ---------------------------------------------------------------------------
// HUDStatPlate (:491-541)
// ---------------------------------------------------------------------------
StatPlate::StatPlate(Builder& b, std::string_view id, double width, std::optional<TextStyle> valueStyle)
    : builder_(&b), layer_(&b.layer(id)), valueStyle_(valueStyle.value_or(type::statValue(b.env.palette))) {
    const auto& p = b.env.palette;
    const double height = 9 + 10 + 6 + valueStyle_.size + 10;
    layer_->setBounds({width, height});
    const std::array<Color, 2> colors{p.tone(0x232323), p.tone(0x1B1B1B)};
    layer_->setContent(plate(layer_->id(), {0, 0, width, height}, colors, {}, 0));
    Layer& corner = b.layer(sub(id, "stat.corner"));
    corner.setFrame({0, 0, 8, 8});
    setPath(corner, cornerMark({0, 0, 8, 8}, 8, 2, topLeft), p.tone(0x6A6A6A));
    label_ = &makeText(b, sub(id, "stat.label"), "", type::label(p), {12, 9, width - 24, 10});
    value_ = &makeText(b, sub(id, "stat.value"), "", valueStyle_, {12, 25, width - 24, valueStyle_.size});
    Layer& ring = b.layer(sub(id, "stat.ring"));
    ring.setFrame({0, 0, width, height});
    setBorder(ring, std::nullopt, p.token(ThemeToken::ringTab), 1);
    for (Layer* child : {&corner, label_, value_, &ring}) layer_->add(*child);
}
void StatPlate::setValueMinimumScale(double scale) { editText(*value_, [&](TextBlock& block) { block.minimumScale = scale; }); }
void StatPlate::set(std::string label, std::string value, std::optional<std::string> unit, bool highlighted, std::optional<Color> tone) {
    const auto& p = builder_->env.palette;
    editText(*label_, [&](TextBlock& block) { block.setText(std::move(label)); });
    const Color color = tone ? p.legible(*tone) : (highlighted ? p.accentInk() : valueStyle_.color);
    editText(*value_, [&](TextBlock& block) {
        block.runs = {{std::move(value), valueStyle_.colored(color)}};
        if (unit) block.runs.push_back({" " + *unit, type::statUnit(p)});
    });
}
std::vector<Rect> StatPlate::gridFrames(int count, double width, int columns, double rowHeight) {
    const double cell = (width - double(columns - 1) * 8) / double(columns);
    std::vector<Rect> frames;
    for (int i = 0; i < count; ++i) frames.push_back({double(i % columns) * (cell + 8), double(i / columns) * (rowHeight + 8), cell, rowHeight});
    return frames;
}

// ---------------------------------------------------------------------------
// HUDCells (:545-605)
// ---------------------------------------------------------------------------
Cells::Cells(Builder& b, std::string_view id, int count, double width, bool capacity, std::optional<double> height)
    : builder_(&b), layer_(&b.layer(id)), capacity_(capacity) {
    const double cellHeight = height.value_or(capacity ? 22 : 8);
    const double cellWidth = (width - double(count - 1) * 3) / double(count);
    layer_->setBounds({width, cellHeight});
    for (int index = 0; index < count; ++index) {
        Layer& cell = b.layer(sub(id, "cell." + std::to_string(index)));
        cell.setFrame({double(index) * (cellWidth + 3), 0, cellWidth, cellHeight});
        Layer& fill = b.layer(sub(cell.id(), "fill"));
        Layer& ring = b.layer(sub(cell.id(), "ring"));
        fill.setFrame(localBounds(cell)); ring.setFrame(localBounds(cell));
        setTintedBackground(fill, clear);
        cell.add(fill); cell.add(ring);
        layer_->add(cell);
        cells_.push_back(&fill); rings_.push_back(&ring);
    }
}
int Cells::pulseCount(double) const noexcept {
    return int(std::count_if(cells_.begin(), cells_.end(), [](const Layer* cell) { return cell->tintEffect.keys != 0; }));
}
// set (:574-604): `pulse` is the pulse's mid colour (nullopt: 3A3826 with a
// tone, else 4D4A1C; Light: the tone mixed 35 % into the pale empty cell);
// `pulseCount` limits the pulse to the next N empty cells (Battery: 3).
void Cells::set(int filled, std::optional<Tone> tone, bool pulsing, bool animated, std::optional<Color> pulse, std::optional<int> pulseCount) {
    const auto& p = builder_->env.palette;
    const auto clock = builder_->env.clock();
    const Color empty = capacity_ ? p.tone(0x2A2A2A) : p.token(ThemeToken::iconButton);
    const Color ring = capacity_ ? p.token(ThemeToken::ringField) : p.tone(0x4C4C4C);
    const Color high = tone ? tone->high : (capacity_ ? p.yellowHigh() : p.yellow()), low = tone ? tone->low : p.yellowLow();
    const int count = int(cells_.size());
    for (int index = 0; index < count; ++index) {
        Layer& cell = *cells_[std::size_t(index)];
        const bool on = index < filled;
        Layer& border = *rings_[std::size_t(index)];
        if (on) border.clearContent(); else setBorder(border, std::nullopt, ring, 1);
        if (on && capacity_) {
            const std::array<Color, 2> colors{high, low};
            cell.setContent(plate(cell.id(), localBounds(cell), colors, {}, 0));
            cell.setTint(std::nullopt);
        } else {
            if (!cell.tint()) { cell.setContent(solid(cell.id(), localBounds(cell), white(1))); cell.setTint(clear); }
            motion::animateTint(cell, on ? high : empty, animated ? (capacity_ ? .3 : .2) : 0, clock, easing::ease);
        }
        cell.removeMotion(MotionSlot::effect); cell.tintEffect = {};
        if (!pulsing || on || clock.reduceMotion || index >= filled + pulseCount.value_or(count)) continue;
        const Color mid = p.isLight() ? mixed(empty, high, .35) : pulse.value_or(tone ? rgb(0x3A3826) : rgb(0x4D4A1C));
        const std::array<Color, 3> values{empty, mid, empty};
        const std::array<double, 3> times{0, .5, 1};
        motion::colorKeyframes(cell, values, times, 2.4, clock, easing::easeInOut, std::numeric_limits<double>::infinity(), double(index - filled) * .08);
    }
}
} // namespace endfield::modules::design
