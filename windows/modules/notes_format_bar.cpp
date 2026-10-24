// The shared format bar, format toggle, colour wheel and colour pop-over: the
// port of Sources/NotesFormattingControls.swift (see the header). Line
// references are that file's (Mac 1.3.0).
//
// Surface split: the toggle's fill and ring are white-authored tinted surfaces
// (their colours ease on hover and on/off), its glyph and rule one tinted ink
// surface (the Mac sets their colour without animation: a tint set at once);
// the wheel's marker moves by translation and its fill is a tint, so a drag
// never re-rasterizes anything. Not ported: NotesFormatChange.init?(shortcut:)
// (each editor host maps Ctrl+B / I / U itself) and NotesPressForwarder (an
// AppKit first-responder workaround; the canvas keeps the editor focused by
// leaving presses on the bar alone, DOCS-236).
#include "modules/notes_format_bar.hpp"
#include "core/source_color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace endfield::modules {
namespace d = design;
namespace {
using d::Channel; using d::Color; using d::Point; using d::Rect;
constexpr double pi = std::numbers::pi;
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
std::string code(std::uint32_t hex) { char buffer[8]; std::snprintf(buffer, sizeof buffer, "%06X", unsigned(hex & 0xFFFFFF)); return buffer; }
std::uint32_t channel(double v) { return std::uint32_t(std::round(std::clamp(v, 0.0, 1.0) * 255)); }
// NSColor.getHue(_:saturation:brightness:alpha:) of an sRGB colour.
std::array<double, 3> hsb(Color c) {
    const double hi = std::max({c.r, c.g, c.b}), lo = std::min({c.r, c.g, c.b}), delta = hi - lo;
    double h = 0;
    if (delta > 0) {
        if (hi == c.r) h = std::fmod((c.g - c.b) / delta + 6, 6) / 6;
        else if (hi == c.g) h = ((c.b - c.r) / delta + 2) / 6;
        else h = ((c.r - c.g) / delta + 4) / 6;
    }
    return {h, hi > 0 ? delta / hi : 0, hi};
}
}

// --- NotesFormatBar statics (:93-105, :183-190) ---------------------------------
namespace notesFormat {
std::array<std::uint32_t, 5> colors(const d::Palette& p) { return {0xEEEEEE, hex(p.yellow()), 0x22A6F2, 0xE5383B, 0x8CE03C}; }
std::uint32_t hex(Color c) noexcept { return channel(c.r) << 16 | channel(c.g) << 8 | channel(c.b); }
std::uint32_t hex(const core::notes::RGBA& c) noexcept { return channel(c.red) << 16 | channel(c.green) << 8 | channel(c.blue); }
Color color(std::uint32_t v) noexcept { return d::rgb(v & 0xFFFFFF); }
core::notes::RGBA rgba(std::uint32_t v) noexcept { const Color c = color(v); return {c.r, c.g, c.b, 1}; }
std::string sizeTitle(double size) { return std::to_string(static_cast<long long>(std::round(size))); }
std::vector<std::string> families(std::span<const std::string> bundled, std::span<const std::string> installed) {
    static constexpr std::array<std::string_view, 17> demo{"HarmonyOS Sans", "Noto Sans SC", "Noto Sans JP", "Noto Sans KR", "Segoe UI", "Arial",
        "Georgia", "Times New Roman", "Courier New", "Consolas", "Verdana", "Tahoma", "Trebuchet MS", "Palatino", "Garamond", "Helvetica", "Impact"};
    const auto has = [](std::span<const std::string> list, std::string_view name) { return std::find(list.begin(), list.end(), name) != list.end(); };
    std::vector<std::string> out;
    for (const auto name : demo) if (name == defaultFamily || has(bundled, name) || has(installed, name)) out.emplace_back(name);
    return out;
}
// NSColor(calibratedHue:saturation:brightness:) is HSB in Generic RGB.
Color calibratedHSB(double hue, double saturation, double brightness) {
    const double h = std::fmod(std::fmod(hue, 1) + 1, 1) * 6, s = std::clamp(saturation, 0.0, 1.0), v = std::clamp(brightness, 0.0, 1.0);
    const int sector = std::min(5, int(h));
    const double f = h - sector, a = v * (1 - s), b = v * (1 - f * s), c = v * (1 - (1 - f) * s);
    core::source::SourceRGB rgb;
    switch (sector) {
    case 0: rgb = {v, c, a}; break;
    case 1: rgb = {b, v, a}; break;
    case 2: rgb = {a, v, c}; break;
    case 3: rgb = {a, b, v}; break;
    case 4: rgb = {c, a, v}; break;
    default: rgb = {v, a, b}; break;
    }
    const auto out = core::source::genericRGBToSRGB(rgb);
    return {out[0], out[1], out[2], 1};
}
}
ehud::data::Json notesColorWheelArtwork() {
    return ehud::data::Json::Object{{"asset", "raster/" + std::string(notesColorWheelSHA256) + ".png"}, {"sha256", std::string(notesColorWheelSHA256)}};
}

// ---------------------------------------------------------------------------
// NotesFormatToggle (:266-340)
// ---------------------------------------------------------------------------
NotesFormatToggle::NotesFormatToggle(d::Builder& b, std::string id, std::string text, Glyph glyph, bool isOn, d::Size size, std::string letterText)
    : Control(b.env, id, std::move(text), b.layer(id)), glyph_(glyph), on_(isOn) {
    std::string letter;
    switch (glyph) {
    case Glyph::bold: letter = "B"; break;
    case Glyph::italic: letter = "I"; break;
    case Glyph::underline: letter = "U"; break;
    case Glyph::strikethrough: letter = "S"; break;
    case Glyph::label: letter = std::move(letterText); break;
    case Glyph::check: case Glyph::chip: break;
    }
    const double fontSize = size.height >= 26 ? 12 : 10;
    const Rect bounds{0, 0, size.width, size.height};
    d::Layer& l = layer();
    l.setAnimatable();                                   // press scale, disabled opacity
    l.setBounds(size);
    fill_ = &b.layer(sub(this->id(), "format.fill"));
    fill_->setFrame(bounds);
    d::setTintedBackground(*fill_, palette().tone(0x262626));
    // The glyph and its rule share one colour (rule.backgroundColor = text.color).
    ink_ = &b.layer(sub(this->id(), "format.ink"));
    ink_->setFrame(bounds);
    ink_->setAnimatable();
    d::Layer& letterLayer = d::makeText(b, sub(this->id(), "format.text"), letter, d::TextStyle{fontSize, d::FontWeight::bold, d::white(1)},
        {0, std::round((size.height - fontSize) / 2), size.width, fontSize}, d::TextAlignment::center);
    // HarmonyOS Sans has no italic face; the browser's synthetic slant (m21 = -0.2).
    if (glyph == Glyph::italic) letterLayer.setBaseTransform(d::Affine::skew(-0.2));
    ink_->add(letterLayer);
    if (glyph == Glyph::underline || glyph == Glyph::strikethrough) {
        const double ruleWidth = glyph == Glyph::strikethrough ? 9 : 7;
        d::Layer& rule = b.layer(sub(this->id(), "format.rule"));
        rule.setFrame({std::round((size.width - ruleWidth) / 2) + .5,
            glyph == Glyph::strikethrough ? std::round(size.height / 2) - .5 : std::round(size.height * .73), ruleWidth, 1});
        d::setBackground(rule, d::white(1));
        ink_->add(rule);
    }
    l.add(*fill_); l.add(*ink_);
    if (glyph == Glyph::check) {
        tick_ = &d::makeIcon(b, sub(this->id(), "format.tick"), d::Icon::check, std::min(size.width, size.height) * .62, d::fixed::nearBlack);
        tick_->setPosition({size.width / 2, size.height / 2});
        l.add(*tick_);
    }
    if (glyph == Glyph::chip) {
        chip_ = &b.layer(sub(this->id(), "format.chip"));
        chip_->setFrame({4, 4, size.width - 8, size.height - 8});
        d::setBorder(*chip_, std::nullopt, d::fixed::nearBlack, 1);
        l.add(*chip_);
    }
    // CALayer draws its border above its sublayers: the ring is the top surface.
    ring_ = &b.layer(sub(this->id(), "format.ring"));
    ring_->setFrame(bounds);
    d::setTintedBorder(*ring_, palette().token(d::ThemeToken::ringField), 1);
    l.add(*ring_);
    render(false);
    setTarget(bounds);
}
void NotesFormatToggle::setOn(bool value) { if (value == on_) return; on_ = value; render(true); }
// setChip (:318): the colour a chip toggle shows (no animation).
void NotesFormatToggle::setChip(Color color) { if (chip_) d::setBorder(*chip_, color, d::fixed::nearBlack, 1); }
// render(animated:) (:320-331).
void NotesFormatToggle::render(bool animated) {
    const auto& p = palette();
    const Color ring = on_ ? d::clear : (hovered() && enabled() ? p.tone(0x888888) : p.token(d::ThemeToken::ringField));
    const auto c = clock();
    d::motion::animateTint(*fill_, on_ ? p.yellow() : p.tone(0x262626), animated ? .12 : 0, c, d::easing::ease);
    d::motion::animateTint(*ring_, ring, animated ? d::duration::hover : 0, c, d::easing::ease);
    ink_->setTint(on_ ? d::fixed::nearBlack : p.tone(0xDDDDDD));
    if (tick_ && tick_->hidden() == on_) tick_->setHidden(!on_);
    layer().setValue(Channel::opacity, enabled() ? 1 : .35);
}
void NotesFormatToggle::hoverChanged() { render(true); }
void NotesFormatToggle::pressChanged() { d::applyHoverMotion(*this, 0, 1, .94, nullptr); }
void NotesFormatToggle::enabledChanged() { render(false); }
std::optional<d::AccessibilityRole> NotesFormatToggle::accessibilityRole() const {
    return glyph_ == Glyph::label || glyph_ == Glyph::chip ? d::AccessibilityRole::button : d::AccessibilityRole::toggle;
}
void NotesFormatToggle::accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const {
    if (label.empty()) return;
    const auto role = *accessibilityRole();
    auto item = accessibilityItem(root, role);
    if (role == d::AccessibilityRole::toggle) item.on = on_;
    out.push_back(std::move(item));
}

// ---------------------------------------------------------------------------
// NotesColorWheelControl (:346-433)
// ---------------------------------------------------------------------------
NotesColorWheel::NotesColorWheel(d::Builder& b, std::string id, std::string text, double side, double hue, double saturation, double brightness)
    : Control(b.env, id, std::move(text), b.layer(id)), side_(side), hue_(hue), saturation_(saturation), brightness_(brightness) {
    const Rect bounds{0, 0, side, side};
    d::Layer& l = layer();
    l.setBounds({side, side});
    d::Layer& raster = b.layer(sub(this->id(), "wheel.raster"));
    raster.setFrame(bounds);
    raster.setContent(d::image(raster.id(), bounds, {d::ImageSource::Kind::required, std::string(notesColorWheelImageKey), {}, 0}, "resize"));
    // The marker moves (translation) and takes the picked colour (a tint of
    // its white fill); its #111 border and the white .9 ring stay as drawn.
    marker_ = &b.layer(sub(this->id(), "wheel.marker"));
    marker_->setAnimatable();
    marker_->setBounds({14, 14});
    marker_->setPosition({0, 0});
    markerFill_ = &b.layer(sub(this->id(), "wheel.marker.fill"));
    markerFill_->setFrame({0, 0, 14, 14});
    d::setTintedBackground(*markerFill_, d::white(1), 7);
    d::Layer& rim = b.layer(sub(this->id(), "wheel.marker.border"));
    rim.setFrame({0, 0, 14, 14});
    d::setBorder(rim, std::nullopt, d::fixed::nearBlack, 2, 7);
    d::Layer& ring = b.layer(sub(this->id(), "wheel.marker.ring"));
    ring.setFrame({-1.5, -1.5, 17, 17});
    d::setBorder(ring, std::nullopt, d::white(.9), 1.5, 8.5);
    rim.add(ring);
    marker_->add(*markerFill_); marker_->add(rim);
    l.add(raster); l.add(*marker_);
    setTarget(bounds, d::PathBuilder().ellipse(bounds));
    render();
}
Color NotesColorWheel::color() const { return notesFormat::calibratedHSB(hue_, saturation_, brightness_); }
Point NotesColorWheel::unit(double hue, double saturation) {
    const double angle = hue * 2 * pi;
    return {(std::cos(angle) * saturation + 1) / 2, (std::sin(angle) * saturation + 1) / 2};
}
NotesColorWheel::Pick NotesColorWheel::pick(Point u) {
    const double dx = u.x * 2 - 1, dy = u.y * 2 - 1;
    const double radius = std::max(1.0, std::hypot(dx, dy));
    const double hue = std::fmod(std::atan2(dy, dx) / (2 * pi) + 1, 1);
    return {hue, std::min(1.0, std::hypot(dx, dy)), {(dx / radius + 1) / 2, (dy / radius + 1) / 2}};
}
void NotesColorWheel::setBrightness(double value) { brightness_ = std::clamp(value, 0.0, 1.0); render(); }
void NotesColorWheel::render() {
    const Point u = unit(hue_, saturation_);
    marker_->setValue(Channel::translateX, u.x * side_);
    marker_->setValue(Channel::translateY, u.y * side_);
    markerFill_->setTint(color());
}
void NotesColorWheel::pickAt(Point p, bool final) {
    const auto picked = pick({p.x / side_, p.y / side_});
    hue_ = picked.hue; saturation_ = picked.saturation;
    render();
    if (onPick) onPick(final);
}
bool NotesColorWheel::pointerDown(Point p) { if (!enabled()) return false; pickAt(p, false); return true; }
void NotesColorWheel::pointerDragged(Point p) { pickAt(p, false); }
void NotesColorWheel::pointerUp(Point p) { pickAt(p, true); }
void NotesColorWheel::activate() {
    hue_ = std::fmod(hue_ + 1.0 / 12, 1); saturation_ = std::max(saturation_, .5);
    render();
    if (onPick) onPick(true);
}

// ---------------------------------------------------------------------------
// NotesFormatBar.colorPicker (:232-257)
// ---------------------------------------------------------------------------
NotesColorPicker notesColorPicker(d::Environment& env, std::string id, std::string title, std::uint32_t color, d::Layer* bubbleHost,
    std::function<void(std::uint32_t)> onColor) {
    constexpr double side = 156;
    const auto [hue, saturation, brightness] = hsb(notesFormat::color(color));
    NotesColorPicker picker;
    picker.popover = std::make_unique<d::Popover>(env, std::move(title), std::nullopt, side + 24, side + 8 + d::Slider::height);
    auto& builder = picker.popover->builder();
    auto& wheel = builder.make<NotesColorWheel>(sub(id, "wheel"), core::localized("Color wheel", "颜色轮盘", env.language), side, hue, saturation, brightness);
    auto& light = builder.make<d::Slider>(sub(id, "brightness"), core::localized("Brightness", "亮度", env.language), brightness * 100,
        d::Slider::Range{10, 100, 1, std::nullopt}, d::ValueFormat::percent, side, true, true, false);
    light.setBubbleHost(bubbleHost);
    // The colour applies when a press or drag ends (wheel release, slider release).
    const auto apply = [onColor, &wheel] { if (onColor) onColor(notesFormat::hex(wheel.color())); };
    wheel.onPick = [apply](bool final) { if (final) apply(); };
    light.onChange = [apply, &wheel](double value, bool final) { wheel.setBrightness(value / 100); if (final) apply(); };
    wheel.layer().setOrigin({0, 0});
    light.layer().setOrigin({0, side + 8});
    picker.popover->content().add(wheel.layer());
    picker.popover->content().add(light.layer());
    picker.controls = {&wheel, &light};
    return picker;
}

// ---------------------------------------------------------------------------
// NotesFormatBar (:92-227)
// ---------------------------------------------------------------------------
// What the bar's menus and pop-over need after the build: the shown state
// (the pop-over compares against it on release) and the owner's onFormat.
struct NotesFormatBar::Shared {
    NotesFormatState state;
    std::function<void(const core::notes::FormatChange&)> onFormat;
    void report(const core::notes::FormatChange& change) const { if (onFormat) onFormat(change); }
};
NotesFormatBar::NotesFormatBar(d::Builder& b, d::Canvas& canvas, std::string id, NotesFormatState state, std::vector<std::string> families,
    double fontWidth, double sizeWidth, std::vector<d::Control*> trailing)
    : shared_(std::make_shared<Shared>()), canvas_(&canvas), id_(std::move(id)), families_(std::move(families)), trailing_(trailing.size()) {
    shared_->state = std::move(state);
    const auto& s = shared_->state;
    const auto t = [&](std::string_view en, std::string_view zh) { return core::localized(en, zh, b.env.language); };
    layer_ = &b.layer(sub(id_, "formatBar"));
    font_ = &b.make<d::Dropdown>(sub(id_, "font"), t("Font", "字体"), s.family, std::nullopt, d::Dropdown::Height::compact, fontWidth);
    size_ = &b.make<d::Dropdown>(sub(id_, "size"), t("Font size", "字号"), notesFormat::sizeTitle(s.size), std::nullopt, d::Dropdown::Height::compact, sizeWidth);
    custom_ = &b.make<d::Swatch>(sub(id_, "color.custom"), t("Custom color", "自定义颜色"), std::nullopt, 16);
    font_->onActivate = [this] {
        std::vector<d::Menu::Item> items;
        for (const auto& family : families_) { d::Menu::Item item; item.id = family; item.title = family; item.selected = family == shared_->state.family; items.push_back(std::move(item)); }
        toggleMenu(*font_, std::move(items), std::max(font_->layer().bounds().width, 200.0),
            [](const std::string& value) { core::notes::FormatChange c; c.kind = core::notes::FormatKind::font; c.fontName = value; return std::optional(c); });
    };
    size_->onActivate = [this] {
        std::vector<d::Menu::Item> items;
        for (const double value : notesFormat::sizes) { d::Menu::Item item; item.id = item.title = notesFormat::sizeTitle(value); item.selected = value == shared_->state.size; items.push_back(std::move(item)); }
        toggleMenu(*size_, std::move(items), std::nullopt, [](const std::string& value) -> std::optional<core::notes::FormatChange> {
            char* end{}; const double size = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || !std::isfinite(size)) return std::nullopt;
            core::notes::FormatChange c; c.kind = core::notes::FormatKind::size; c.fontSize = size; return c;
        });
    };
    custom_->onActivate = [this] { openColorWheel(); };
    double x = 0;
    const auto place = [&](d::Control& control) {
        const auto bounds = control.layer().bounds();
        control.layer().setFrame({x, (notesFormat::height - bounds.height) / 2, bounds.width, bounds.height});
        layer_->add(control.layer());
        x += bounds.width + 6;
        controls_.push_back(&control);
    };
    place(*font_); place(*size_);
    struct Trait { NotesFormatToggle::Glyph glyph; const char* name; std::string label; bool NotesFormatState::*key; core::notes::FormatKind kind; };
    const std::array<Trait, 4> traits{{
        {NotesFormatToggle::Glyph::bold, "bold", t("Bold", "粗体"), &NotesFormatState::bold, core::notes::FormatKind::bold},
        {NotesFormatToggle::Glyph::italic, "italic", t("Italic", "斜体"), &NotesFormatState::italic, core::notes::FormatKind::italic},
        {NotesFormatToggle::Glyph::underline, "underline", t("Underline", "下划线"), &NotesFormatState::underline, core::notes::FormatKind::underline},
        {NotesFormatToggle::Glyph::strikethrough, "strikethrough", t("Strikethrough", "删除线"), &NotesFormatState::strikethrough, core::notes::FormatKind::strikethrough}}};
    for (const auto& trait : traits) {
        auto& toggle = b.make<NotesFormatToggle>(sub(id_, trait.name), trait.label, trait.glyph, s.*trait.key);
        const auto kind = trait.kind;
        toggle.onActivate = [shared = shared_, kind] { core::notes::FormatChange c; c.kind = kind; shared->report(c); };
        toggles_.emplace_back(&toggle, trait.key);
        place(toggle);
    }
    for (const auto hex : notesFormat::colors(b.env.palette)) {
        const auto name = code(hex);
        auto& swatch = b.make<d::Swatch>(sub(id_, "color." + name), t("Text color", "文字颜色") + " #" + name, notesFormat::color(hex), 16);
        swatch.onActivate = [shared = shared_, hex] {
            core::notes::FormatChange c; c.kind = core::notes::FormatKind::color; c.color = notesFormat::rgba(hex); shared->report(c);
        };
        swatches_.emplace_back(&swatch, hex);
        place(swatch);
    }
    place(*custom_);
    for (d::Control* control : trailing) place(*control);
    layer_->setBounds({std::max(0.0, x - 6), notesFormat::height});
    renderColor();
}
NotesFormatBar::~NotesFormatBar() = default;
const NotesFormatState& NotesFormatBar::state() const noexcept { return shared_->state; }
void NotesFormatBar::setOnFormat(std::function<void(const core::notes::FormatChange&)> value) { shared_->onFormat = std::move(value); }
// show(_:) (:169-175).
void NotesFormatBar::show(const NotesFormatState& next) {
    if (next == shared_->state) return;
    shared_->state = next;
    font_->setValue(next.family);
    size_->setValue(notesFormat::sizeTitle(next.size));
    for (auto& [toggle, key] : toggles_) toggle->setOn(next.*key);
    renderColor();
}
// formatsSelection(at:in:) (:179-181): every control but the trailing ones.
bool NotesFormatBar::formatsSelection(Point point, const d::Layer& root) const {
    const auto count = controls_.size() - std::min(trailing_, controls_.size());
    for (std::size_t n = 0; n < count; ++n) {
        const d::Control& control = *controls_[n];
        if (!d::isVisible(control.layer(), root)) continue;
        const Rect r = control.rect(root);
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) return true;
    }
    return false;
}
// renderColor (:192-196).
void NotesFormatBar::renderColor() {
    bool matched = false;
    for (auto& [swatch, hex] : swatches_) { swatch->setSelected(hex == shared_->state.color); matched = matched || hex == shared_->state.color; }
    custom_->setSelected(!matched);
}
// toggleMenu (:208-214): a press while ANY menu is open only closes it.
void NotesFormatBar::toggleMenu(d::Dropdown& anchor, std::vector<d::Menu::Item> items, std::optional<double> minimumWidth,
    std::function<std::optional<core::notes::FormatChange>(const std::string&)> change) {
    if (canvas_->menuOpen()) { canvas_->closeMenu(); return; }
    canvas_->openMenu(anchor.id(), std::move(items), anchor, minimumWidth, [shared = shared_, change = std::move(change)](const d::Menu::Item& item) {
        if (const auto value = change(item.id)) shared->report(*value);
    });
}
// openColorWheel (:218-227): a second press closes the pop-over; a colour
// is reported only when it differs from the one shown.
void NotesFormatBar::openColorWheel() {
    if (canvas_->popoverOpen()) { canvas_->closePopover(); return; }
    auto picker = notesColorPicker(custom_->env(), id_, core::localized("TEXT COLOR", "文字颜色", custom_->env().language), shared_->state.color,
        &canvas_->top(), [shared = shared_](std::uint32_t hex) {
            if (hex == shared->state.color) return;
            core::notes::FormatChange c; c.kind = core::notes::FormatKind::color; c.color = notesFormat::rgba(hex); shared->report(c);
        });
    canvas_->openPopover(std::move(picker.popover), custom_, std::nullopt, std::move(picker.controls));
}
} // namespace endfield::modules
