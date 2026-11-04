// Media Assembly's panel components (Sources/MediaAssemblyControls.swift) and
// stage (Sources/MediaAssemblyViewport.swift:181-620), statement by statement;
// line numbers cite those files.
#include "modules/media_assembly_controls.hpp"
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace endfield::modules {
namespace d = design;
namespace {
using d::Point; using d::Rect; using d::Size; using d::Channel;
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
Rect boundsOf(Size s) { return {0, 0, s.width, s.height}; }
constexpr double radians(double degrees) { return degrees * std::numbers::pi / 180; }
}

// MARK: MediaAssemblyTool (:5-27)
std::string_view mediaAssemblyToolID(MediaAssemblyTool tool) noexcept {
    switch (tool) {
    case MediaAssemblyTool::crop: return "crop";
    case MediaAssemblyTool::adjust: return "adjust";
    case MediaAssemblyTool::curves: return "curves";
    case MediaAssemblyTool::levels: return "levels";
    case MediaAssemblyTool::trim: return "trim";
    }
    return "crop";
}
std::optional<MediaAssemblyTool> mediaAssemblyTool(std::string_view id) noexcept {
    for (const auto tool : {MediaAssemblyTool::crop, MediaAssemblyTool::adjust, MediaAssemblyTool::curves, MediaAssemblyTool::levels, MediaAssemblyTool::trim})
        if (mediaAssemblyToolID(tool) == id) return tool;
    return std::nullopt;
}
std::string mediaAssemblyToolTitle(MediaAssemblyTool tool, core::Language l) {
    switch (tool) {
    case MediaAssemblyTool::crop: return core::localized("Crop", "裁剪", l);
    case MediaAssemblyTool::adjust: return core::localized("Adjust", "调整", l);
    case MediaAssemblyTool::curves: return core::localized("Curves", "曲线", l);
    case MediaAssemblyTool::levels: return core::localized("Levels", "色阶", l);
    case MediaAssemblyTool::trim: return core::localized("Trim", "剪辑", l);
    }
    return {};
}
d::Icon mediaAssemblyToolIcon(MediaAssemblyTool tool) noexcept {
    switch (tool) {
    case MediaAssemblyTool::crop: return d::Icon::grid;
    case MediaAssemblyTool::adjust: return d::Icon::gear;
    case MediaAssemblyTool::curves: return d::Icon::lyric;
    case MediaAssemblyTool::levels: return d::Icon::apps;
    case MediaAssemblyTool::trim: return d::Icon::timer;
    }
    return d::Icon::grid;
}

// MARK: MediaAssemblyParameter (:31-89)
std::vector<MediaAssemblyParameter> MediaAssemblyParameter::parameters(MediaAssemblyTool tool, double duration, core::Language l) {
    using A = MediaAssemblyAdjustments;
    const auto make = [&](std::string id, std::string_view english, std::string_view chinese, double minimum, double maximum, double step, double value,
                          std::function<double(const A&)> read, std::function<void(A&, double)> write) {
        MediaAssemblyParameter p;
        p.id = std::move(id); p.title = core::localized(english, chinese, l);
        p.range = {minimum, maximum, step, minimum < 0 ? std::optional<double>(0) : std::nullopt};
        p.defaultValue = value; p.read = std::move(read); p.write = std::move(write);
        return p;
    };
    std::vector<MediaAssemblyParameter> out;
    switch (tool) {
    case MediaAssemblyTool::adjust:
        out.push_back(make("brightness", "Brightness", "亮度", -1, 1, .01, 0, [](const A& a) { return a.brightness; }, [](A& a, double v) { a.brightness = v; }));
        out.push_back(make("contrast", "Contrast", "对比度", 0, 4, .01, 1, [](const A& a) { return a.contrast; }, [](A& a, double v) { a.contrast = v; }));
        out.push_back(make("saturation", "Saturation", "饱和度", 0, 2, .01, 1, [](const A& a) { return a.saturation; }, [](A& a, double v) { a.saturation = v; }));
        out.push_back(make("temperature", "Temperature", "色温", 2000, 12000, 100, 6500, [](const A& a) { return a.temperature; }, [](A& a, double v) { a.temperature = v; }));
        out.push_back(make("tint", "Tint", "色调", -200, 200, 1, 0, [](const A& a) { return a.tint; }, [](A& a, double v) { a.tint = v; }));
        out.push_back(make("highlights", "Highlights", "高光", 0, 1, .01, 1, [](const A& a) { return a.highlights; }, [](A& a, double v) { a.highlights = v; }));
        out.push_back(make("shadows", "Shadows", "阴影", 0, 1, .01, 0, [](const A& a) { return a.shadows; }, [](A& a, double v) { a.shadows = v; }));
        out.push_back(make("exposure", "Exposure", "曝光度", -4, 4, .01, 0, [](const A& a) { return a.exposure; }, [](A& a, double v) { a.exposure = v; }));
        break;
    case MediaAssemblyTool::curves:
        // "\(index * 25)%" titles (no translation), defaults 0, .25, .5, .75, 1.
        for (std::size_t index = 0; index < 5; ++index) {
            const auto title = std::to_string(index * 25) + "%";
            out.push_back(make("curve" + std::to_string(index), title, title, 0, 1, .01, double(index) / 4,
                [index](const A& a) { return a.curve[index]; }, [index](A& a, double v) { a.curve[index] = v; }));
        }
        break;
    case MediaAssemblyTool::levels:
        // The engine needs the white point at least .01 above the black point.
        out.push_back(make("black", "Black point", "黑场", 0, 1, .01, 0, [](const A& a) { return a.levelsBlack; },
            [](A& a, double v) { a.levelsBlack = std::min({v, .99, a.levelsWhite - .01}); }));
        out.push_back(make("gamma", "Midtones", "中间调", .1, 4, .01, 1, [](const A& a) { return a.levelsGamma; }, [](A& a, double v) { a.levelsGamma = v; }));
        out.push_back(make("white", "White point", "白场", 0, 1, .01, 1, [](const A& a) { return a.levelsWhite; },
            [](A& a, double v) { a.levelsWhite = std::max({v, .01, a.levelsBlack + .01}); }));
        break;
    case MediaAssemblyTool::trim: {
        const double length = std::max(.1, duration);
        out.push_back(make("trimStart", "Start", "开始", 0, length, .05, 0, [](const A& a) { return a.trimStart; },
            [length](A& a, double v) { a.trimStart = std::min(std::max(0., v), a.trimEnd.value_or(length) - .0501); }));
        out.push_back(make("trimEnd", "End", "结束", 0, length, .05, length, [length](const A& a) { return a.trimEnd.value_or(length); },
            [length](A& a, double v) { a.trimEnd = std::min(length, std::max(v, a.trimStart + .0501)); }));
        break;
    }
    case MediaAssemblyTool::crop: break;
    }
    return out;
}

// MARK: MediaAssemblyToolTile (:94-142)
MediaAssemblyToolTile::MediaAssemblyToolTile(d::Builder& b, std::string id, std::string title, d::Icon icon, Size size)
    : Control(b.env, id, title, b.layer(id)), builder_(&b), size_(size) {
    const Rect bounds = boundsOf(size);
    d::Layer& l = layer();
    l.setAnimatable();                                   // lift and press scale
    l.setBounds(size);
    plate_ = &b.layer(sub(this->id(), "tile.plate"));
    plate_->setFrame(bounds);
    brighten_ = &b.layer(sub(this->id(), "tile.brighten"));
    brighten_->setFrame(bounds);
    brighten_->setAnimatable(); brighten_->setValue(Channel::opacity, 0);
    d::setPath(*brighten_, d::chamfer(bounds, 8), d::white(.06));
    d::Layer& glyph = d::makeIcon(b, sub(this->id(), "tile.icon"), icon, 18, d::fixed::nearBlack);
    d::Layer& text = d::makeText(b, sub(this->id(), "tile.text"), std::move(title), {12, d::FontWeight::medium, d::fixed::nearBlack}, {});
    // Long translations shrink a little before they truncate.
    d::editText(text, [&](d::TextBlock& block) {
        block.minimumScale = .75;
        const double width = std::min(d::preferredWidth(block, b.env.text), size.width - 8);
        if (size.height > 40) {
            glyph.setFrame({(size.width - 18) / 2, 12, 18, 18});
            block.alignment = d::TextAlignment::center;
            block.setLineFrame({4, 37, size.width - 8, 12});
        } else {
            const double start = (size.width - 18 - 6 - width) / 2;
            glyph.setFrame({start, (size.height - 18) / 2, 18, 18});
            block.setLineFrame({start + 24, (size.height - 12) / 2, width + 1, 12});
        }
    });
    for (d::Layer* child : {plate_, brighten_, &glyph, &text}) l.add(*child);
    paint();
    setTarget(bounds, d::chamfer(bounds, 8));
}
void MediaAssemblyToolTile::setOn(bool value) { if (value == on_) return; on_ = value; paint(); }
void MediaAssemblyToolTile::paint() {
    const auto& p = palette();
    const std::array<d::Color, 3> yellow{p.yellowHigh(), p.yellow(), p.yellowLow()};
    constexpr std::array<double, 3> stops{0, .5, 1};
    if (on_) plate_->setContent(d::plate(plate_->id(), boundsOf(size_), yellow, stops, 8));
    else plate_->setContent(d::plate(plate_->id(), boundsOf(size_), d::fixed::lightButton, {}, 8));
}
void MediaAssemblyToolTile::hoverChanged() { d::applyHoverMotion(*this, 2, 1, .965, brighten_); }
void MediaAssemblyToolTile::pressChanged() { d::applyHoverMotion(*this, 2, 1, .965, brighten_); }
std::optional<d::AccessibilityRole> MediaAssemblyToolTile::accessibilityRole() const {
    return id() == "tool:mirror" ? d::AccessibilityRole::toggle : d::AccessibilityRole::button;
}
void MediaAssemblyToolTile::accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, *accessibilityRole());
    item.on = on_ && item.role == d::AccessibilityRole::toggle;
    out.push_back(std::move(item));
}

// MARK: MediaAssemblyPresetCell (:147-205)
MediaAssemblyPresetCell::MediaAssemblyPresetCell(d::Builder& b, std::string id, std::string label, std::optional<std::string> title,
    std::optional<d::ImageSource> image, Size size)
    : Control(b.env, id, std::move(label), b.layer(id)), builder_(&b), filter_(title.has_value()) {
    const Rect bounds = boundsOf(size);
    const bool sticker = !filter_;
    d::Layer& l = layer();
    l.setAnimatable();                                   // the hover lift
    l.setPartRoot(true);                                 // a grid unit (scroll visibility window, reuse)
    l.setBounds(size); l.setMasksToBounds(true);
    d::setBackground(l, sticker ? d::fixed::charcoal : d::rgb(0x1E1E1E));
    const double side = sticker ? 42 : 44;
    if (image) {
        d::Layer& thumbnail = b.layer(sub(this->id(), "cell.image"));
        thumbnail.setFrame({(size.width - side) / 2, sticker ? 5. : 6., side, side});
        thumbnail.setContent(d::image(thumbnail.id(), boundsOf({side, side}), *image, "resizeAspect", std::nullopt, true));
        l.add(thumbnail);
    } else if (!sticker) {
        l.add(d::makeText(b, sub(this->id(), "cell.none"), "∅", {22, d::FontWeight::regular, d::fixed::onDarkTag},
            {0, (size.height - 18 - 22) / 2, size.width, 22}, d::TextAlignment::center));
    }
    // The inset ring paints above the thumbnail (the Mac adds it after the image).
    ring_ = &b.layer(sub(this->id(), "cell.ring"));
    ring_->setFrame(bounds);
    d::setTintedBorder(*ring_, d::rgb(0x363636), 1);
    ringSelected_ = &b.layer(sub(this->id(), "cell.ring.selected"));
    ringSelected_->setFrame(bounds);
    ringSelected_->setAnimatable(); ringSelected_->setValue(Channel::opacity, 0);
    d::setBorder(*ringSelected_, std::nullopt, palette().yellow(), 2);
    l.add(*ring_); l.add(*ringSelected_);
    if (title) {
        strip_ = &b.layer(sub(this->id(), "cell.strip"));
        strip_->setFrame({0, size.height - 18, size.width, 18});
        name_ = &d::makeText(b, sub(this->id(), "cell.name"), std::move(*title), {10, d::FontWeight::medium, d::rgb(0xDDDDDD)},
            {4, 4, size.width - 8, 10}, d::TextAlignment::center);
        strip_->add(*name_); l.add(*strip_);
    }
    paint(false);
    setTarget(bounds);
}
void MediaAssemblyPresetCell::setSelected(bool value, bool animated) { if (value == selected_) return; selected_ = value; paint(animated); }
void MediaAssemblyPresetCell::paint(bool animated) {
    const bool hovered = this->hovered() && enabled();
    const double duration = animated ? d::duration::hover : 0;
    const auto clock = env().clock();
    const auto& p = palette();
    // borderWidth 1 -> 2 and borderColor (.22 ease): the 2 px yellow ring fades over the tinted 1 px one,
    // which leaves once covered (at rest a selected cell draws the one 2 px ring, like the Mac).
    d::motion::animate(*ringSelected_, Channel::opacity, selected_ ? 1 : 0, duration, clock, d::easing::ease);
    d::motion::animateTint(*ring_, selected_ ? p.yellow() : (hovered ? d::rgb(0x777777) : d::rgb(0x363636)), duration, clock, d::easing::ease);
    if (selected_ && duration > 0) d::motion::animate(*ring_, Channel::opacity, 0, 1e-3, clock, d::easing::linear, duration);
    else d::motion::animate(*ring_, Channel::opacity, selected_ ? 0 : 1, 0, clock);
    if (strip_ && stripSelected_ != selected_) {
        stripSelected_ = selected_;
        d::setBackground(*strip_, selected_ ? p.yellow() : d::black(.5));
        d::editText(*name_, [&](d::TextBlock& block) { block.setColor(selected_ ? d::fixed::nearBlack : d::rgb(0xDDDDDD)); });
    }
}
void MediaAssemblyPresetCell::hoverChanged() {
    paint(true);
    d::motion::animate(layer(), Channel::translateY, hovered() && enabled() ? -2 : 0, d::duration::hover, env().clock());
}
std::optional<d::AccessibilityRole> MediaAssemblyPresetCell::accessibilityRole() const { return filter_ ? d::AccessibilityRole::tab : d::AccessibilityRole::button; }
void MediaAssemblyPresetCell::accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, *accessibilityRole());
    item.on = filter_ && selected_;
    out.push_back(std::move(item));
}

// MARK: MediaAssemblyZoomValue (:209-232)
MediaAssemblyZoomValue::MediaAssemblyZoomValue(d::Builder& b, std::string label) : Control(b.env, "zoomReset", std::move(label), b.layer("zoomReset")) {
    const auto& p = palette();
    const Rect bounds{0, 0, 58, 30};
    d::Layer& l = layer();
    l.setBounds({58, 30});
    d::setBackground(l, p.token(d::ThemeToken::field));
    ring_ = &b.layer(sub(id(), "zoom.ring"));
    ring_->setFrame(bounds);
    d::setTintedBorder(*ring_, p.token(d::ThemeToken::ringField), 1);
    text_ = &d::makeText(b, sub(id(), "zoom.text"), "", {12, d::FontWeight::bold, p.token(d::ThemeToken::textOnDark), 1, 0, false, true},
        {0, 9, 58, 12}, d::TextAlignment::center);
    l.add(*ring_); l.add(*text_);
    setTarget(bounds);
    setZoom(1);
}
void MediaAssemblyZoomValue::setZoom(double zoom) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f×", zoom);
    if (value_ == text) return;
    value_ = text;
    d::editText(*text_, [&](d::TextBlock& block) { block.setText(value_); });
}
void MediaAssemblyZoomValue::hoverChanged() {
    d::motion::animateTint(*ring_, hovered() && enabled() ? palette().tone(0x888888) : palette().token(d::ThemeToken::ringField), .2, env().clock(), d::easing::ease);
}

// MARK: MediaAssemblyParameterRow (:236-269)
MediaAssemblyParameterRow::MediaAssemblyParameterRow(d::Builder& b, MediaAssemblyParameter parameter, double current, double width)
    : parameter_(std::move(parameter)) {
    const auto& p = b.env.palette;
    layer_ = &b.layer("media.parameter." + parameter_.id);
    layer_->setBounds({width, height});
    layer_->setPartRoot(true);                           // a list unit of its scroll area
    slider_ = &b.make<d::Slider>("parameter:" + parameter_.id, parameter_.title, current, parameter_.range, parameter_.format(), width, true, false);
    // Both texts share the 14 px label line's baseline.
    d::Layer& name = d::makeText(b, sub(layer_->id(), "name"), parameter_.title, {12, d::FontWeight::regular, p.tone(0xDDDDDD)}, {0, 5, width - 44, 12});
    value_ = &d::makeText(b, sub(layer_->id(), "value"), "", {11, d::FontWeight::bold, p.token(d::ThemeToken::textOnDark), 1, 0, false, true},
        {width - 60, 5.75, 60, 11}, d::TextAlignment::right);
    // The slider's track sits 2 px higher than the shared one (top 9).
    slider_->layer().setOrigin({0, 18 - 2});
    for (d::Layer* child : {&name, value_, &slider_->layer()}) layer_->add(*child);
    setValue(current);
}
void MediaAssemblyParameterRow::setValue(double current) {
    if (!slider_->dragging()) slider_->setValue(current);
    showDragValue(slider_->dragging() ? slider_->value() : current);
}
void MediaAssemblyParameterRow::showDragValue(double current) {
    const auto text = d::formatValue(parameter_.format(), current);
    if (d::textOf(*value_).text() != text) d::editText(*value_, [&](d::TextBlock& block) { block.setText(text); });
}
std::string MediaAssemblyParameterRow::valueText() const { return d::textOf(*value_).text(); }

// MARK: MediaAssemblyStage (:186-312)
MediaAssemblyStage::MediaAssemblyStage(d::Environment& env, std::string prefix, std::function<std::optional<d::ImageSource>(MediaAssemblyStickerKind)> stickerImage)
    : arena_(std::move(prefix), &env.deadlines), builder_{env, arena_, set_}, palette_(env.palette.generation()), stickerImage_(std::move(stickerImage)) {
    auto& b = builder_;
    const auto& p = env.palette;
    const auto text = [&](std::string_view english, std::string_view chinese) { return core::localized(english, chinese, env.language); };
    zoomOut_ = &b.make<d::IconButton>("zoomOut", text("Zoom out", "缩小"), d::Icon::minus);
    zoomIn_ = &b.make<d::IconButton>("zoomIn", text("Zoom in", "放大"), d::Icon::plus);
    zoomValue_ = &b.make<MediaAssemblyZoomValue>(text("Reset zoom", "重置缩放"));
    play_ = &b.make<d::IconButton>("play", text("Play", "播放"), d::Icon::play);
    pause_ = &b.make<d::IconButton>("pause", text("Pause", "暂停"), d::Icon::pause);
    open_ = &b.make<d::Button>("emptyOpen", text("Open", "打开"), d::Icon::open, d::Button::Kind::primary, d::Button::Height::compact);

    layer_ = &b.layer("media.stage");
    layer_->setPartRoot(true);
    layer_->setBounds(size); layer_->setMasksToBounds(true);
    d::setBackground(*layer_, p.tone(0x0C0C0C));

    // Dot grid: radial-gradient(#2a2a2a 1px, transparent 1.4px) on a 16 px pitch.
    grid_ = &b.layer("media.grid");
    grid_->setFrame(bounds);
    d::PathBuilder dots;
    for (int row = 0; row < int(std::ceil(size.height / 16)); ++row)
        for (int column = 0; column < int(std::ceil(size.width / 16)); ++column) dots.ellipse({column * 16 + 8 - 1.2, row * 16 + 8 - 1.2, 2.4, 2.4});
    d::setPath(*grid_, dots, p.tone(0x2A2A2A));

    content_ = &b.layer("media.content");
    content_->setFrame(bounds); content_->setAnimatable(); content_->setDecorative(true);
    // The owner's records (picture, fade, video) compose right after this part.
    slot_ = &b.layer("media.picture.slot");
    slot_->setFrame({0, 0, 1, 1}); slot_->setPartRoot(true);
    slot_->setContent(d::solid(slot_->id(), {0, 0, 1, 1}, d::clear));
    rotator_ = &b.layer("media.rotator");
    rotator_->setAnimatable(); rotator_->setBounds({1, 1}); rotator_->setPosition({0, 0});
    rotator_->setValue(Channel::scaleX, 0); rotator_->setValue(Channel::scaleY, 0);
    picture_ = &b.layer("media.picture");
    picture_->setAnimatable(); picture_->setBounds({1, 1}); picture_->setPosition({.5, .5});
    fade_ = &b.layer("media.picture.fade");
    fade_->setAnimatable(); fade_->setBounds({1, 1}); fade_->setPosition({.5, .5});
    picture_->add(*fade_); rotator_->add(*picture_);
    stickers_ = &b.layer("media.stickers");
    stickers_->setFrame(bounds); stickers_->setDecorative(true);
    for (d::Layer* child : {slot_, rotator_, stickers_}) content_->add(*child);

    crop_ = &b.layer("media.crop");
    crop_->setFrame(bounds); crop_->setAnimatable(); crop_->setPartRoot(true); crop_->setHidden(true);
    const std::array<d::Corner, 4> corners{d::topLeft, d::topRight, d::bottomLeft, d::bottomRight};
    for (std::size_t index = 0; index < cropHandles_.size(); ++index) {
        auto& handle = b.layer("media.crop.handle." + std::to_string(index));
        handle.setAnimatable(); handle.setAnchorPoint({0, 0}); handle.setBounds({14, 14}); handle.setZPosition(2);
        d::setPath(handle, d::cornerMark({0, 0, 14, 14}, 14, 3, corners[index]), p.yellow());
        crop_->add(handle); cropHandles_[index] = &handle;
    }

    selection_ = &b.layer("media.selection");
    selection_->setAnimatable(); selection_->setPartRoot(true); selection_->setHidden(true); selection_->setPosition({0, 0});
    outline_ = &b.layer("media.selection.outline");
    selection_->add(*outline_);
    // buildHandles (:459-484).
    for (std::size_t index = 0; index < handles_.size(); ++index) {
        const auto kind = MediaAssemblyViewport::handleCentres({}).at(index).first;
        auto& handle = b.layer("media.handle." + std::to_string(index));
        handle.setAnimatable();
        if (kind.kind == MediaAssemblyHandle::Kind::corner) {
            // 7 x 7 white with a 1 px black ring outside it.
            handle.setBounds({9, 9}); d::setBackground(handle, d::black(1));
            auto& face = b.layer(sub(handle.id(), "face"));
            face.setFrame({1, 1, 7, 7}); d::setBackground(face, d::white(1));
            handle.add(face);
        } else {
            // 18 px rgba(0,0,0,.85) circle with a 1 px #555 ring outside it.
            handle.setBounds({20, 20}); d::setBorder(handle, std::nullopt, d::rgb(0x555555), 1, 10);
            auto& face = b.layer(sub(handle.id(), "face"));
            face.setFrame({1, 1, 18, 18}); d::setBackground(face, d::black(.85), 9);
            face.add(d::makeText(b, sub(handle.id(), "glyph"), kind.kind == MediaAssemblyHandle::Kind::rotate ? "↻" : "×",
                {12, d::FontWeight::regular, d::white(1), 1.5}, {0, 0, 18, 18}, d::TextAlignment::center));
            handle.add(face);
        }
        selection_->add(handle);
        handles_[index] = {kind, &handle};
    }

    chips_ = &b.layer("media.chips");
    chips_->setFrame({12, 12, size.width - 24, 18}); chips_->setPartRoot(true);
    const std::array<d::Chip::Variant, 3> variants{d::Chip::Variant::dark, d::Chip::Variant::yellow, d::Chip::Variant::outline};
    for (std::size_t index = 0; index < chipViews_.size(); ++index) {
        chipViews_[index] = std::make_unique<d::Chip>(b, "media.chip." + std::to_string(index), "", variants[index]);
        chipViews_[index]->layer().setHidden(true);
        chips_->add(chipViews_[index]->layer());
    }

    const d::Color bar = p.tone(0x0E0E0E, .82), ring = p.token(d::ThemeToken::ringField);
    zoomGroup_ = &b.layer("media.zoom");
    zoomGroup_->setFrame({10, size.height - 10 - 38, 134, 38});
    d::setBorder(*zoomGroup_, bar, ring, 1);
    zoomOut_->layer().setOrigin({4, 4});
    zoomValue_->layer().setOrigin({38, 4});
    zoomIn_->layer().setOrigin({100, 4});
    for (d::Layer* child : {&zoomOut_->layer(), &zoomValue_->layer(), &zoomIn_->layer()}) zoomGroup_->add(*child);

    playback_ = &b.layer("media.playback");
    playback_->setFrame({150, size.height - 10 - 38, size.width - 160, 38});
    d::setBorder(*playback_, bar, ring, 1);
    play_->layer().setOrigin({4, 4}); pause_->layer().setOrigin({4, 4});
    time_ = &d::makeText(b, "media.playback.time", "", {11, d::FontWeight::bold, p.token(d::ThemeToken::textOnDark), 1, 0, false, true},
        {playback_->bounds().width - 10 - 76, 13.5, 76, 11}, d::TextAlignment::right);
    for (d::Layer* child : {&play_->layer(), &pause_->layer(), time_}) playback_->add(*child);
    playback_->setHidden(true);

    exportGroup_ = &b.layer("media.export");
    exportText_ = &d::makeText(b, "media.export.text", "", {11, d::FontWeight::medium, p.accentInk(), 1, .06, true, true}, {});
    exportGroup_->add(*exportText_);
    for (std::size_t index = 0; index < exportCells_.size(); ++index) {
        exportCells_[index] = &b.layer("media.export.cell." + std::to_string(index));
        exportGroup_->add(*exportCells_[index]);
    }
    exportGroup_->setHidden(true);

    emptyLayer_ = &b.layer("media.empty");
    emptyLayer_->setFrame(bounds); emptyLayer_->setAnimatable(); emptyLayer_->setHidden(true);
    emptyLayer_->setContent(d::hatch(emptyLayer_->id(), bounds, d::Hatch::light, p));
    emptyTitle_ = &d::makeText(b, "media.empty.title", "", d::type::emptyTitle(p), {0, 165.25, size.width, 20}, d::TextAlignment::center);
    emptySubtitle_ = &d::makeText(b, "media.empty.subtitle", "", d::type::labelCJK(p), {0, 195.25, size.width, 11.5}, d::TextAlignment::center);
    for (d::Layer* child : {emptyTitle_, emptySubtitle_, &open_->layer()}) emptyLayer_->add(*child);

    // A layer border paints above the sublayers: the inset 1 px ring.
    ring_ = &b.layer("media.stage.ring");
    ring_->setFrame(bounds);
    d::setBorder(*ring_, std::nullopt, ring, 1);
    for (d::Layer* child : {grid_, content_, crop_, selection_, chips_, zoomGroup_, playback_, exportGroup_, emptyLayer_, ring_}) layer_->add(*child);
    open_->place({0, 0});
    open_->layer().setOrigin({std::round((size.width - open_->layer().bounds().width) / 2), 220.75});
    zoomValue_->setZoom(1);
}
void MediaAssemblyStage::adopt(const MediaAssemblyStage& old) {
    image_ = old.image_; previous_.reset();
    videoShown_ = old.videoShown_;
    if (old.pictureRect_) layoutPicture(*old.pictureRect_, old.transform_, false);
}
std::vector<d::Control*> MediaAssemblyStage::controls() const {
    std::vector<d::Control*> out{zoomOut_, zoomValue_, zoomIn_, play_, pause_};
    if (auto* slider = pendingSeekSet_ ? pendingSeek_ : seek_) out.push_back(slider);
    out.push_back(open_);
    return out;
}

// MARK: Empty state (:320-336)
void MediaAssemblyStage::setEmpty(bool value, std::string subtitle) {
    empty_ = value;
    const auto& p = env().palette;
    emptyLayer_->setHidden(!value);
    for (d::Layer* l : {grid_, content_, chips_, zoomGroup_}) l->setHidden(value);
    // The canvas lays out crop, selection, playback and export again after this.
    if (value) for (d::Layer* l : {crop_, selection_, playback_, exportGroup_}) l->setHidden(true);
    const auto style = d::type::emptyTitle(p), bracket = style.colored(p.accentInk());
    std::vector<d::TextRun> runs{{"[ ", bracket}, {"NO SOURCE", style}, {" ]", bracket}};
    if (d::textOf(*emptyTitle_).runs != runs) d::editText(*emptyTitle_, [&](d::TextBlock& block) { block.runs = std::move(runs); });
    if (d::textOf(*emptySubtitle_).text() != subtitle) d::editText(*emptySubtitle_, [&](d::TextBlock& block) { block.setText(std::move(subtitle)); });
}

// MARK: Picture (:341-389)
void MediaAssemblyStage::setImage(Image next, bool crossfade) {
    Image previous = std::move(image_);
    image_ = std::move(next);
    // A new contents replaces a running cross-fade.
    ++fadeSerial_;
    previous_.reset();
    fade_->setValue(Channel::opacity, 1);
    const auto clock = env().clock();
    if (!crossfade || !previous || !image_ || clock.reduceMotion) return;
    previous_ = std::move(previous);
    fade_->setValue(Channel::opacity, 0);
    d::motion::animate(*fade_, Channel::opacity, 1, .12, clock, d::easing::ease);
    d::motion::after(*fade_, clock.now + .12, clock, [this, serial = fadeSerial_] { if (serial == fadeSerial_) previous_.reset(); });
}
void MediaAssemblyStage::layoutPicture(Rect rect, const MediaAssemblyPictureTransform& next, bool animated) {
    // Unchanged geometry must not cut a running transition short.
    if (pictureRect_ && rect == *pictureRect_ && next == transform_ && !animated) return;
    transform_ = next; pictureRect_ = rect;
    const Size local = next.swapsAxes() ? Size{rect.height, rect.width} : Size{rect.width, rect.height};
    const double duration = animated ? .25 : 0;
    const auto clock = env().clock();
    // The rotator's scale is the Mac's rotator / picture bounds; its rotation the cumulative angle.
    d::motion::animate(*rotator_, Channel::translateX, rect.x + rect.width / 2, duration, clock);
    d::motion::animate(*rotator_, Channel::translateY, rect.y + rect.height / 2, duration, clock);
    d::motion::animate(*rotator_, Channel::scaleX, local.width, duration, clock);
    d::motion::animate(*rotator_, Channel::scaleY, local.height, duration, clock);
    d::motion::animate(*picture_, Channel::scaleX, next.scaleX, duration, clock);
    d::motion::animate(*picture_, Channel::scaleY, next.scaleY, duration, clock);
    // Rotation keeps its own cumulative angle (the channel is never wrapped).
    d::motion::animate(*rotator_, Channel::rotation, radians(next.angle), duration, clock);
}
// setContentOpacity (:412-416).
void MediaAssemblyStage::setContentOpacity(double value, bool animated) {
    for (d::Layer* l : {content_, selection_})
        d::motion::animate(*l, Channel::opacity, value, animated ? .16 : 0, env().clock(), d::easing::ease);
}

// MARK: Stickers, selection and crop (:421-522)
void MediaAssemblyStage::setStickers(std::span<const MediaAssemblySticker> items, Rect base) {
    for (auto& slot : stickerSlots_) {
        if (!slot.used || std::any_of(items.begin(), items.end(), [&](const auto& item) { return item.id == slot.id; })) continue;
        slot.used = false; slot.id.clear(); slot.layer->setHidden(true); slot.layer->clearContent();
    }
    // One texel per source pixel (MediaAssemblyAssetCatalog.stickerImage decodes at pixelSize).
    const double density = std::clamp(env().text.boxScale, 1., 4.);
    for (std::size_t index = 0; index < items.size() && index < stickerSlots_.size(); ++index) {
        const auto& item = items[index];
        auto it = std::find_if(stickerSlots_.begin(), stickerSlots_.end(), [&](const auto& slot) { return slot.used && slot.id == item.id; });
        if (it == stickerSlots_.end()) {
            it = std::find_if(stickerSlots_.begin(), stickerSlots_.end(), [](const auto& slot) { return !slot.used; });
            if (!it->layer) {
                it->layer = &builder_.layer("media.sticker." + std::to_string(it - stickerSlots_.begin()));
                it->layer->setAnimatable(); it->layer->setPartRoot(true); it->layer->setPosition({0, 0});
                stickers_->add(*it->layer);
            }
            it->used = true; it->id = item.id; it->density = 0;
        }
        auto& slot = *it;
        const auto pixels = mediaAssemblyStickerPixelSize(item.kind);
        const Size extent{pixels.x / density, pixels.y / density};
        if (slot.density != density || slot.kind != item.kind) {
            slot.kind = item.kind; slot.density = density;
            slot.layer->setBounds(extent);
            if (const auto image = stickerImage_ ? stickerImage_(item.kind) : std::nullopt)
                slot.layer->setContent(d::image(slot.layer->id(), boundsOf(extent), *image, "resizeAspect", std::nullopt, true));
            else slot.layer->clearContent();
        }
        const auto rect = MediaAssemblyViewport::stickerRect(item, base);
        slot.layer->setHidden(false);
        slot.layer->setValue(Channel::translateX, rect.x + rect.width / 2);
        slot.layer->setValue(Channel::translateY, rect.y + rect.height / 2);
        slot.layer->setValue(Channel::rotation, radians(item.rotation));
        slot.layer->setValue(Channel::scaleX, extent.width > 0 ? rect.width / extent.width : 0);
        slot.layer->setValue(Channel::scaleY, extent.height > 0 ? rect.height / extent.height : 0);
        // Later stickers paint on top, in the model's order.
        slot.layer->setZPosition(double(index));
    }
}
void MediaAssemblyStage::setSelection(const MediaAssemblySticker* item, Rect base) {
    if (!item) { selection_->setHidden(true); setHovered(std::nullopt); return; }
    const auto rect = MediaAssemblyViewport::stickerRect(*item, base);
    selection_->setHidden(empty_);
    selection_->setBounds({rect.width, rect.height});
    selection_->setPosition({rect.x + rect.width / 2, rect.y + rect.height / 2});
    selection_->setValue(Channel::rotation, radians(item->rotation));
    if (outline_->frame() != boundsOf({rect.width, rect.height})) {
        outline_->setFrame(boundsOf({rect.width, rect.height}));
        d::setBorder(*outline_, std::nullopt, env().palette.yellow(), 1);
    }
    const auto centres = MediaAssemblyViewport::handleCentres({rect.width, rect.height});
    for (std::size_t index = 0; index < handles_.size(); ++index) handles_[index].second->setPosition(centres[index].second);
}
bool MediaAssemblyStage::isSelectionVisible() const noexcept { return !selection_->hidden(); }
void MediaAssemblyStage::setHovered(std::optional<MediaAssemblyHandle> handle) {
    if (handle == hoveredHandle_) return;
    hoveredHandle_ = handle;
    const auto clock = env().clock();
    for (auto& [kind, layer] : handles_) {
        const double scale = handle && kind == *handle ? 1.12 : 1;
        d::motion::animate(*layer, Channel::scaleX, scale, .2, clock);
        d::motion::animate(*layer, Channel::scaleY, scale, .2, clock);
    }
}
void MediaAssemblyStage::setCrop(std::optional<Rect> rect, Rect image) {
    crop_->setHidden(!rect || empty_);
    if (!rect) return;
    const auto& p = env().palette;
    const Rect box{image.x + rect->x * image.width, image.y + rect->y * image.height, rect->width * image.width, rect->height * image.height};
    d::PathBuilder shade; shade.rect(bounds).rect(box);
    d::PathBuilder border;
    border.rect({box.x, box.y, box.width, 1}).rect({box.x, box.y + box.height - 1, box.width, 1});
    border.rect({box.x, box.y, 1, box.height}).rect({box.x + box.width - 1, box.y, 1, box.height});
    d::PathBuilder thirds;
    for (const double fraction : {1. / 3, 2. / 3}) {
        thirds.rect({box.x + box.width * fraction, box.y + 1, 1, std::max(0., box.height - 2)});
        thirds.rect({box.x + 1, box.y + box.height * fraction, std::max(0., box.width - 2), 1});
    }
    auto node = d::frameNode(crop_->id(), bounds);
    d::addChildren(node, {d::shape(crop_->id() + ".shade", bounds, shade, d::black(.5), std::nullopt, {}, true),
        d::shape(crop_->id() + ".thirds", bounds, thirds, d::withAlpha(p.yellow(), .5)), d::shape(crop_->id() + ".box", bounds, border, p.yellow())});
    crop_->setContent(std::move(node));
    const auto origins = MediaAssemblyViewport::cropHandleOrigins(*rect, image, {size.width, size.height});
    for (std::size_t index = 0; index < cropHandles_.size(); ++index) cropHandles_[index]->setPosition(origins[index]);
}
bool MediaAssemblyStage::isCropVisible() const noexcept { return !crop_->hidden(); }

// MARK: Chips, zoom, playback, export (:527-613)
void MediaAssemblyStage::setChips(std::string source, std::optional<std::string> filter, std::size_t count) {
    const std::array<std::string, 3> titles{std::move(source), filter.value_or(""), count > 0 ? std::to_string(count) + "/16" : ""};
    if (titles == chipTitles_) return;
    chipTitles_ = titles;
    const auto& text = env().text;
    double room = chips_->bounds().width - 14;
    for (std::size_t index = 1; index < titles.size(); ++index)
        if (!titles[index].empty()) room -= text.width(titles[index], d::type::chip()) + 14 + 6;
    double x = 0;
    for (std::size_t index = 0; index < titles.size(); ++index) {
        auto& chip = *chipViews_[index];
        chip.layer().setHidden(titles[index].empty());
        if (titles[index].empty()) continue;
        chip.setTitle(index == 0 ? fitted(titles[index], room, env()) : titles[index]);
        chip.layer().setOrigin({x, 0});
        x += chip.layer().bounds().width + 6;
    }
}
std::vector<std::string> MediaAssemblyStage::chipTitles() const {
    std::vector<std::string> out;
    for (const auto& title : chipTitles_) if (!title.empty()) out.push_back(title);
    return out;
}
std::string MediaAssemblyStage::fitted(const std::string& name, double width, const d::Environment& env) {
    const auto style = d::type::chip();
    const auto fits = [&](const std::string& value) { return env.text.width(value, style) <= width; };
    if (fits(name)) return name;
    // NSString.pathExtension / deletingPathExtension: the last non-leading dot.
    const auto dot = name.rfind('.');
    const std::string ext = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot + 1);
    const std::string stem = ext.empty() ? name : name.substr(0, dot);
    const std::string suffix = ext.empty() ? std::string() : "." + ext;
    // Binary search over the stem's Characters (extended grapheme clusters).
    std::size_t low = 0, high = env.characters.count(stem);
    while (low < high) {
        const std::size_t mid = (low + high + 1) / 2;
        if (fits(env.characters.prefix(stem, mid) + "…" + suffix)) low = mid; else high = mid - 1;
    }
    return env.characters.prefix(stem, low) + "…" + suffix;
}
d::Slider* MediaAssemblyStage::configurePlayback(d::Builder& page, std::optional<double> duration, std::string label) {
    pendingSeekSet_ = true;
    if (!duration) { pendingSeek_ = nullptr; playback_->setHidden(true); return nullptr; }
    auto& slider = page.make<d::Slider>("seek", std::move(label), 0, d::Slider::Range{0, std::max(.1, *duration), .01}, d::ValueFormat::clock,
        playback_->bounds().width - 42 - 8 - 76 - 10, true, false);
    slider.layer().setOrigin({42, 4});
    pendingSeek_ = &slider;
    return &slider;
}
void MediaAssemblyStage::installPlayback() {
    if (!pendingSeekSet_) return;
    pendingSeekSet_ = false;
    if (seek_ && seek_ != pendingSeek_ && seek_->layer().parent() == playback_) seek_->layer().removeFromParent();
    seek_ = std::exchange(pendingSeek_, nullptr);
    if (seek_) playback_->add(seek_->layer());
}
void MediaAssemblyStage::setPlayback(bool visible, bool playing, double time, double duration) {
    // While the seek thumb is dragged the readout follows it.
    const double current = seek_ && seek_->dragging() ? seek_->value() : time;
    playback_->setHidden(!visible || empty_);
    play_->layer().setHidden(playing); pause_->layer().setHidden(!playing);
    const auto readout = d::format::clock(std::int64_t(std::floor(current))) + " / " + d::format::clock(std::int64_t(std::ceil(duration)));
    if (d::textOf(*time_).text() != readout) d::editText(*time_, [&](d::TextBlock& block) { block.setText(readout); });
    if (seek_ && !seek_->dragging()) seek_->setValue(current);
}
void MediaAssemblyStage::setExport(std::optional<double> progress, const std::string& title) {
    exportGroup_->setHidden(!progress || empty_);
    if (!progress) return;
    const auto& p = env().palette;
    const int percent = int(std::floor(std::clamp(*progress, 0., 1.) * 100));
    const auto label = title + " " + std::to_string(percent) + "%";
    const auto style = d::textOf(*exportText_).style();
    const double textWidth = std::ceil(env().text.width(title + " 100%", style));
    const double width = 10 + textWidth + 8 + 110 + 10;
    exportGroup_->setFrame({size.width - 10 - width, size.height - 10 - 38 + 6.75, width, 24.5});
    d::setBorder(*exportGroup_, p.tone(0x0E0E0E, .86), p.token(d::ThemeToken::ringField), 1);
    d::editText(*exportText_, [&](d::TextBlock& block) { block.setText(label); block.setLineFrame({10, 6.75, textWidth + 1, 11}); });
    const double cell = (110 - 9 * 2) / 10.;
    for (std::size_t index = 0; index < exportCells_.size(); ++index) {
        exportCells_[index]->setFrame({10 + textWidth + 8 + double(index) * (cell + 2), 10.25, cell, 4});
        d::setBackground(*exportCells_[index], double(index) < double(percent) / 10 ? p.yellow() : p.token(d::ThemeToken::separator));
    }
}
std::optional<std::string> MediaAssemblyStage::exportText() const {
    if (exportGroup_->hidden()) return std::nullopt;
    return d::textOf(*exportText_).text();
}
} // namespace endfield::modules
