// The interactive components: ports of Sources/HUDDesignRows.swift (HUDRowPlate
// :8-134, HUDToggle :138-202, HUDSegmentedControl :205-283, HUDSheen :288-328),
// Sources/HUDDesignButtons.swift (HUDButton :21-121, HUDIconButton :124-178,
// HUDPill :181-241, HUDSwatch :246-283, HUDDragGrip :290-349),
// Sources/HUDDesignSlider.swift (HUDSlider :8-170, HUDSliderBubble :174-221),
// Sources/HUDDesignMenu.swift (HUDDropdown :27-100), Sources/HUDDesignTabs.swift
// (HUDTabs :7-152) and Sources/HUDDesignBadges.swift (HUDKeycapGroup :97-202,
// HUDFieldFrame :206-238).
//
// Surface split (LIBRARY-DESIGN section 3): every layer the Swift animates or
// changes on hover/press is animatable; colours that change on hover are
// white-authored tinted surfaces (notch, knob, segment fill/ring/text, the
// secondary plate, icon-button fill/ring/glyph, pill plate, grip dots, tab
// text, list text); show/hide on hover is an opacity number (the Mac toggles
// isHidden on a few of them: the sheen layer). Recorded adaptations:
//  - HUDToggle's grey and dark OFF/ON copies (cross-faded .14 s) are ONE tinted
//    label per word above the knob: identical at rest, a colour lerp in between;
//  - HUDButton's secondary bar -> ▶ path morph (.2 s ease) is a cross-fade of
//    two prepared shapes over the same duration and easing (lead answer Q7);
//  - HUDSwatch's conic custom-colour wheel is drawn as 90 wedges of the
//    interpolated colour (the shared rasterizer has axial and radial gradients only);
//  - HUDSlider's fill keeps one gradient surface the width of the track and
//    is placed by translateX/scaleX (a horizontal two-stop gradient stretched
//    to the fill is the gradient of the fill), so a drag never re-rasterizes it;
//  - HUDTabs keeps its separators and only hides the two beside the active tab
//    (the Mac removes and re-adds them on every layout; same pixels, no layer churn);
//  - HUDIconButton's first colours animate in like the Mac's init (fill from
//    no colour, border from CALayer's default black); every other component
//    renders its initial state without animation, as the Mac does.
#include "modules/hud_design_controls.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace endfield::modules::design {
namespace {
constexpr double pi = 3.14159265358979323846;
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
bool inRect(Rect r, Point p) noexcept { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
void setOpacity(Layer& l, double v) { if (l.value(Channel::opacity) != v) l.setValue(Channel::opacity, v); }
void setHiddenIf(Layer& l, bool hidden) { if (l.hidden() != hidden) l.setHidden(hidden); }
}

// ---------------------------------------------------------------------------
// HUDSheen (HUDDesignRows.swift:288-328)
// ---------------------------------------------------------------------------
Sheen::Sheen(Builder& b, std::string_view id, double alpha) : layer_(&b.layer(id)), band_(&b.layer(sub(id, "band"))) {
    band_->setAnimatable(SurfaceRole::lazy);
    band_->setValue(Channel::opacity, 0); // layer.isHidden = true (:295): the band is shown only while it moves
    layer_->add(*band_);
    alpha_ = alpha;
}
void Sheen::setShape(const Layer& plate, Size size) {
    plate_ = &plate;
    layer_->setFrame({0, 0, size.width, size.height});
    band_->setFrame({0, 0, size.width, size.height});
    const std::array<Color, 3> colors{white(0), white(alpha_), white(0)};
    const std::array<double, 3> locations{.35, .5, .65};
    band_->setContent(gradient(band_->id(), localBounds(*band_), colors, locations, {0, .3}, {1, .7}));
    // The clip is the plate's own silhouette (the Mac's temporary chamfer mask).
    band_->setAlphaClip(plate_);
}
void Sheen::play(bool on, const motion::Clock& clock) {
    ++serial_;
    band_->removeMotion(MotionSlot::effect);
    setOpacity(*band_, 0);
    if (!on || clock.reduceMotion || !plate_) return;
    const double w = band_->bounds().width;
    // The sweep is a plain animation (the model keeps the end, off the plate).
    band_->setValue(Channel::translateX, 1.1 * w);
    band_->setValue(Channel::opacity, 1);
    const std::array<double, 2> values{-1.1 * w, 1.1 * w}, times{0, 1};
    motion::keyframes(*band_, Channel::translateX, values, times, duration::sheen, clock, easing::standard);
    motion::after(*band_, clock.now + duration::sheen, clock, [this, serial = serial_] {
        if (serial == serial_) setOpacity(*band_, 0);
    });
}

// ---------------------------------------------------------------------------
// HUDRowPlate (HUDDesignRows.swift:8-134)
// ---------------------------------------------------------------------------
RowPlate::RowPlate(Builder& b, std::string id, std::string title, std::optional<std::string> sublabel, Variant variant,
    std::string chevron, double width, std::optional<double> height)
    : Control(b.env, id, title, b.layer(id)), variant_(variant) {
    const double h = height.value_or(variant == Variant::standard || variant == Variant::nav ? metric::rowHeight : metric::compactRowHeight);
    const Rect bounds{0, 0, width, h};
    Layer& l = layer();
    l.setAnimatable();                                   // translateX on hover
    l.setPartRoot(true);                                 // a list unit: its own part (scroll visibility window, reuse)
    l.setBounds({width, h});
    plate_ = &b.layer(sub(this->id(), "row.plate"));
    brighten_ = &b.layer(sub(this->id(), "row.brighten"));
    sheen_ = std::make_unique<Sheen>(b, sub(this->id(), "sheen"));
    notch_ = &b.layer(sub(this->id(), "row.notch"));
    tag_ = &b.layer(sub(this->id(), "row.tag"));
    title_ = &makeText(b, sub(this->id(), "row.title"), std::move(title), type::rowLabel(), {18, 0, 10, 18.6});
    sublabel_ = &makeText(b, sub(this->id(), "row.sublabel"), sublabel.value_or(""), type::rowSublabel(), {18, 0, 10, 13.2});
    cluster_ = &b.layer(sub(this->id(), "row.cluster"));
    chevron_ = &makeText(b, sub(this->id(), "row.chevron"), variant == Variant::nav ? std::move(chevron) : std::string(),
        TextStyle{16, FontWeight::bold, rgb(0x333333)}, {0, 0, 20, 16}, TextAlignment::right);
    for (Layer* child : {plate_, brighten_, &sheen_->layer(), notch_, tag_, title_, sublabel_, cluster_, chevron_}) l.add(*child);
    plate_->setFrame(bounds);
    const std::array<double, 3> stops{0, .55, 1};
    plate_->setContent(plate(plate_->id(), bounds, fixed::rowPlate, stops, metric::rowChamfer, topRight, true));
    const auto shapePath = chamfer(bounds, metric::rowChamfer);
    brighten_->setFrame(bounds); brighten_->setAnimatable();
    brighten_->setContent(shape(brighten_->id(), bounds, shapePath, white(.07)));
    brighten_->setValue(Channel::opacity, 0);
    sheen_->setShape(*plate_, {width, h});
    notch_->setFrame({0, 9, 3, h - 18});
    setTintedBackground(*notch_, fixed::notch);
    const Size tag = microTagSize(env().text);
    tag_->setFrame({18, 4, tag.width, tag.height});
    tag_->setContent(microTag(tag_->id(), {0, 0}, false, env().text, palette()));
    cluster_->setFrame(bounds);
    if (variant != Variant::nav) chevron_->setHidden(true);
    sublabel_->setHidden(!sublabel.has_value());
    setTarget(bounds, chamfer(bounds, metric::rowChamfer), variant != Variant::nav);
    layoutContents();
}
void RowPlate::setTitle(std::string text) {
    label = text;
    editText(*title_, [&](TextBlock& block) { block.setText(std::move(text)); });
    layoutContents();
}
void RowPlate::setSublabel(std::optional<std::string> text) {
    editText(*sublabel_, [&](TextBlock& block) { block.setText(text.value_or("")); });
    sublabel_->setHidden(!text.has_value());
    layoutContents();
}
void RowPlate::setTitleInset(double inset) { if (inset != titleInset_) { titleInset_ = inset; layoutContents(); } }
Rect RowPlate::titleFrame() const noexcept { return textOf(*title_).lineFrame; }
void RowPlate::setCluster(std::vector<Layer*> layers) {
    for (Layer* item : clusterItems_) if (item->parent() == cluster_) item->removeFromParent();
    clusterItems_ = std::move(layers);
    for (Layer* item : clusterItems_) cluster_->add(*item);
    layoutContents();
}
// controlArea (:76-81).
Rect RowPlate::controlArea() const noexcept {
    const auto size = layer().bounds();
    const double centerY = 10 + (size.height - 18) / 2;
    const double left = variant_ == Variant::slider ? 18 + metric::sliderLabelColumn + 14 : 18;
    const double right = size.width - 12 - (variant_ == Variant::nav ? 22 : 0);
    return {left, centerY - 15, right - left, 30};
}
// layoutContents (:83-107) and clusterLeft (:109-113).
void RowPlate::layoutContents() {
    const auto size = layer().bounds();
    const bool nav = variant_ == Variant::nav;
    const double contentTop = 10, contentHeight = size.height - 18;
    const double labelHeight = 15.5 * 1.2, subHeight = 11 * 1.2;
    const double column = 3 + labelHeight + (sublabel_->hidden() ? 0 : 1 + subHeight);
    const double top = contentTop + (contentHeight - column) / 2 + 3;
    double total = 0;
    for (const Layer* item : clusterItems_) total += item->bounds().width;
    total += double(std::max<std::size_t>(1, clusterItems_.size()) - 1) * 8;
    const double clusterLeft = size.width - 12 - (nav ? 22 : 0) - total;
    const double right = variant_ == Variant::slider ? 18 + metric::sliderLabelColumn : clusterLeft - 14;
    const double left = 18 + titleInset_;
    setLine(*title_, {left, top, std::max(10.0, right - left), labelHeight});
    setLine(*sublabel_, {left, top + labelHeight + 1, std::max(10.0, right - left), subHeight});
    if (nav) setLine(*chevron_, {size.width - 12 - 20, contentTop + contentHeight / 2 - 8, 20, 16});
    double x = size.width - 12 - (nav ? 22 : 0);
    const double centerY = contentTop + contentHeight / 2;
    for (auto it = clusterItems_.rbegin(); it != clusterItems_.rend(); ++it) {
        Layer& item = **it;
        const auto s = item.bounds(); const auto a = item.anchorPoint();
        x -= s.width;
        item.setPosition({x + s.width * a.x, centerY - s.height / 2 + s.height * a.y});
        x -= 8;
    }
}
// hoverChanged (:115-124).
void RowPlate::hoverChanged() {
    const bool on = hovered() && enabled();
    const auto c = clock();
    motion::animate(layer(), Channel::translateX, on ? 3 : 0, duration::hover, c);
    motion::animate(*brighten_, Channel::opacity, on ? 1 : 0, duration::hover, c, easing::ease);
    motion::animateTint(*notch_, on ? palette().yellowLow() : fixed::notch, duration::hover, c, easing::ease);
    const double h = layer().bounds().height, grown = (h - 10) / std::max(1.0, h - 18);
    motion::animate(*notch_, Channel::scaleY, on ? grown : 1, duration::hover, c);
    sheen_->play(on, c);
}
bool RowPlate::pointerDown(Point) { return variant_ == Variant::nav && enabled(); }
std::optional<AccessibilityRole> RowPlate::accessibilityRole() const {
    if (variant_ == Variant::nav) return AccessibilityRole::button;
    if (readout) return AccessibilityRole::text;
    return std::nullopt;
}
void RowPlate::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    const auto role = accessibilityRole();
    if (!role || label.empty()) return;
    auto item = accessibilityItem(root, *role);
    if (*role == AccessibilityRole::text) item.value = *readout;
    out.push_back(std::move(item));
}

// ---------------------------------------------------------------------------
// HUDToggle (HUDDesignRows.swift:138-202)
// ---------------------------------------------------------------------------
Toggle::Toggle(Builder& b, std::string id, std::string text, bool isOn) : Control(b.env, id, std::move(text), b.layer(id)), on_(isOn) {
    Layer& l = layer();
    l.setBounds(size);
    setBorder(l, palette().token(ThemeToken::toggleTrack), palette().token(ThemeToken::ringToggle), 1);
    knob_ = &b.layer(sub(this->id(), "toggle.knob"));
    knobFill_ = &b.layer(sub(this->id(), "toggle.knob.fill"));
    marks_ = &b.layer(sub(this->id(), "toggle.marks"));
    knob_->setAnimatable();
    knob_->setFrame({2, 2, 42, 22});
    knobFill_->setFrame(localBounds(*knob_));
    setTintedBackground(*knobFill_, fixed::knobOff);
    marks_->setFrame(localBounds(*knob_));
    PathBuilder marks;
    marks.append(cornerMark({-2, -2, 5, 5}, 5, 1, topLeft)).append(cornerMark({39, -2, 5, 5}, 5, 1, topRight));
    setPath(*marks_, marks, fixed::nearBlack);
    knob_->add(*knobFill_); knob_->add(*marks_);
    l.add(*knob_);
    const char* words[] = {"OFF", "ON"};
    for (int index = 0; index < 2; ++index) {
        // One white-authored label per word, tinted grey or dark (see the file comment).
        Layer& word = makeText(b, sub(this->id(), index ? "toggle.on" : "toggle.off"), words[index],
            type::toggleLabel(palette()).colored(white(1)), {double(index) * 44, 8, 44, 10}, TextAlignment::center);
        word.setAnimatable();
        labels_[std::size_t(index)] = &word;
        l.add(word);
    }
    setTarget(localBounds(l));
    render(false);
}
void Toggle::setOn(bool value, bool animated) { if (value != on_) { on_ = value; render(animated); } }
// render(animated:) (:182-192).
void Toggle::render(bool animated) {
    const auto c = clock();
    const double knob = animated ? duration::knob : 0, labelTime = animated ? duration::toggleLabel : 0;
    motion::animate(*knob_, Channel::translateX, on_ ? 42 : 0, knob, c, easing::snap);
    motion::animateTint(*knobFill_, on_ ? palette().yellow() : fixed::knobOff, knob, c, easing::ease);
    for (std::size_t index = 0; index < 2; ++index) {
        const bool active = (index == 1) == on_;
        motion::animateTint(*labels_[index], active ? fixed::nearBlack : palette().token(ThemeToken::muted), labelTime, c, easing::ease);
    }
}
void Toggle::activate() {
    if (!enabled()) return;
    setOn(!on_, true);
    if (onChange) onChange(on_);
    if (onActivate) onActivate();
}
void Toggle::enabledChanged() { layer().setValue(Channel::opacity, enabled() ? 1 : .35); }
void Toggle::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, AccessibilityRole::toggle); item.on = on_;
    out.push_back(std::move(item));
}

// ---------------------------------------------------------------------------
// HUDSegmentedControl (HUDDesignRows.swift:205-283)
// ---------------------------------------------------------------------------
class SegmentedControl::Segment final : public Control {
public:
    Segment(Builder& b, std::string id, std::string title, int index, SegmentedControl& owner)
        : Control(b.env, id, title, b.layer(id)), index_(index), owner_(&owner) {
        fill_ = &b.layer(sub(this->id(), "segment.fill"));
        text_ = &makeText(b, sub(this->id(), "segment.text"), std::move(title), type::segmented(palette()).colored(white(1)), {},
            TextAlignment::center);
        bar_ = &b.layer(sub(this->id(), "segment.bar"));
        ring_ = &b.layer(sub(this->id(), "segment.ring"));
        text_->setAnimatable();
        // The layer's border draws above its sublayers on the Mac: the ring is the top child.
        for (Layer* child : {fill_, text_, bar_, ring_}) layer().add(*child);
    }
    void place(Rect frame) {
        layer().setFrame(frame);
        const Rect local{0, 0, frame.width, frame.height};
        fill_->setFrame(local); setTintedBackground(*fill_, palette().token(ThemeToken::control));
        ring_->setFrame(local); setTintedBorder(*ring_, palette().token(ThemeToken::ringField), 1);
        setLine(*text_, {6, 8.75, frame.width - 12, 12.5});
        bar_->setFrame({0, 0, 3, 30}); setBackground(*bar_, fixed::nearBlack);
        setTarget(local);
    }
    void render(bool selected, bool yellow, bool animated) {
        const auto& p = palette(); const auto c = clock();
        const bool hover = hovered() && !selected;
        const Color fill = selected ? (yellow ? p.yellow() : fixed::plate) : p.token(ThemeToken::control);
        const Color ring = selected ? clear : (hover ? p.token(ThemeToken::muted) : p.token(ThemeToken::ringField));
        const double time = animated ? duration::hover : 0;
        motion::animateTint(*fill_, fill, time, c, easing::ease);
        motion::animateTint(*ring_, ring, time, c, easing::ease);
        text_->setTint(selected ? fixed::nearBlack : (hover ? p.foreground(1) : p.token(ThemeToken::segmentedText)));
        setHiddenIf(*bar_, !selected || yellow);
    }
    void hoverChanged() override { owner_->renderSegments(true); }
    void activate() override {
        if (!enabled()) return;
        if (owner_->selected_ == index_) { if (owner_->onReselect) owner_->onReselect(index_); }
        else owner_->select(index_, true);
    }
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::tab; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, AccessibilityRole::tab); item.on = owner_->selected_ == index_;
        out.push_back(std::move(item));
    }
    int index_;
private:
    SegmentedControl* owner_;
    Layer *fill_, *text_, *bar_, *ring_;
};

SegmentedControl::SegmentedControl(Builder& b, std::string id, std::string text, std::vector<std::string> options,
    int selectedIndex, double width, bool yellow)
    : Control(b.env, id, std::move(text), b.layer(id)), selected_(selectedIndex), yellow_(yellow) {
    const double gap = 4, count = double(std::max<std::size_t>(1, options.size()));
    const double segmentWidth = (width - gap * (count - 1)) / count;
    layer().setBounds({width, 30});
    for (std::size_t index = 0; index < options.size(); ++index) {
        auto& segment = b.make<Segment>(this->id() + ":" + std::to_string(index), std::move(options[index]), int(index), *this);
        segment.place({double(index) * (segmentWidth + gap), 0, segmentWidth, 30});
        layer().add(segment.layer());
        segments_.push_back(&segment);
    }
    renderSegments(false);
}
void SegmentedControl::select(int index, bool notify) {
    if (index < 0 || std::size_t(index) >= segments_.size() || index == selected_) return;
    selected_ = index;
    renderSegments(false);
    if (notify && onChange) onChange(index);
}
void SegmentedControl::renderSegments(bool animated) {
    for (Control* control : segments_) {
        auto& segment = static_cast<Segment&>(*control);
        segment.render(segment.index_ == selected_, yellow_, animated);
    }
}

// ---------------------------------------------------------------------------
// HUDButton (HUDDesignButtons.swift:21-121)
// ---------------------------------------------------------------------------
namespace {
double buttonHeight(Button::Height h) noexcept { return h == Button::Height::compact ? 26 : h == Button::Height::large ? 40 : 32; }
double buttonPadding(Button::Height h) noexcept { return h == Button::Height::compact ? 11 : h == Button::Height::large ? 22 : 16; }
double buttonFont(Button::Height h) noexcept { return h == Button::Height::compact ? 10.5 : h == Button::Height::large ? 13 : 12; }
}
Button::Button(Builder& b, std::string id, std::string title, std::optional<Icon> icon, Kind kind, Height height)
    : Control(b.env, id, title, b.layer(id)), kind_(kind), height_(height), icon_(icon) {
    const Color color = kind == Kind::secondary ? palette().token(ThemeToken::textOnDark) : (kind == Kind::light ? fixed::counterBackground : fixed::nearBlack);
    Layer& l = layer();
    l.setAnimatable();                                   // lift, press scale, disabled opacity
    plate_ = &b.layer(sub(this->id(), "button.plate"));
    brighten_ = &b.layer(sub(this->id(), "button.brighten"));
    l.add(*plate_); l.add(*brighten_);
    brighten_->setAnimatable(); brighten_->setValue(Channel::opacity, 0);
    if (icon) { glyph_ = &makeIcon(b, sub(this->id(), "button.icon"), *icon, 15, color); l.add(*glyph_); }
    text_ = &makeText(b, sub(this->id(), "button.text"), std::move(title), type::button().sized(buttonFont(height)).colored(color), {});
    l.add(*text_);
    if (kind == Kind::secondary) {
        // The bar and the ▶ it morphs into (barPath :101-107), cross-faded.
        bar_ = &b.layer(sub(this->id(), "button.bar"));
        arrow_ = &b.layer(sub(this->id(), "button.bar.arrow"));
        for (Layer* shapeLayer : {bar_, arrow_}) { shapeLayer->setAnimatable(); l.add(*shapeLayer); }
        plate_->setAnimatable();
    }
    if (kind == Kind::primary) { corner_ = &b.layer(sub(this->id(), "button.pixel")); l.add(*corner_); }
    layoutContents();
}
std::string Button::title() const { return textOf(*text_).text(); }
void Button::setTitle(std::string value) {
    if (value == title()) return;
    label = value;
    editText(*text_, [&](TextBlock& block) { block.setText(std::move(value)); });
    layoutContents();
}
void Button::setIcon(Icon icon) {
    if (!glyph_ || icon_ == icon) return;
    icon_ = icon;
    design::setIcon(*glyph_, icon, 15, kind_ == Kind::secondary ? palette().token(ThemeToken::textOnDark) : (kind_ == Kind::light ? fixed::counterBackground : fixed::nearBlack));
}
double Button::preferredWidth() const {
    const double padding = buttonPadding(height_);
    const double leading = kind_ == Kind::secondary ? (height_ == Height::compact ? 20 : 22) : padding;
    const auto& block = textOf(*text_);
    // An icon-only button has no gap after its icon (flex gap).
    return std::ceil(leading + (icon_ ? 15 + (block.text().empty() ? 0 : 8) : 0) + env().text.width(block.text(), block.style()) + padding);
}
void Button::place(Point origin, std::optional<double> width) {
    layer().setFrame({origin.x, origin.y, std::max(preferredWidth(), width.value_or(0)), buttonHeight(height_)});
    layoutContents();
}
// layoutContents (:68-98).
void Button::layoutContents() {
    const double h = buttonHeight(height_), padding = buttonPadding(height_), font = buttonFont(height_), preferred = preferredWidth();
    const Rect bounds{0, 0, std::max(layer().bounds().width, preferred), h};
    layer().setBounds({bounds.width, bounds.height});
    plate_->setFrame(bounds); brighten_->setFrame(bounds);
    const bool hover = hovered() && enabled();
    switch (kind_) {
    case Kind::primary: {
        const auto colors = palette().accent().primaryButton();
        const std::array<double, 3> stops{0, .38, 1};
        plate_->setContent(plate(plate_->id(), bounds, colors, stops, metric::buttonChamfer));
        brighten_->setContent(shape(brighten_->id(), bounds, chamfer(bounds, metric::buttonChamfer), white(.07)));
        corner_->setFrame({bounds.width - 6 - 4, h - 5 - 4, 4, 4}); setBackground(*corner_, fixed::nearBlack);
        break;
    }
    case Kind::light:
        plate_->setContent(plate(plate_->id(), bounds, fixed::lightButton, {}, metric::buttonChamfer));
        brighten_->setContent(shape(brighten_->id(), bounds, chamfer(bounds, metric::buttonChamfer), white(.07)));
        break;
    case Kind::secondary:
        setTintedBackground(*plate_, palette().token(hover ? ThemeToken::secondaryButtonHover : ThemeToken::secondaryButton));
        brighten_->setContent(shape(brighten_->id(), bounds, PathBuilder().rect(bounds), white(.07)));
        for (Layer* shapeLayer : {bar_, arrow_}) {
            shapeLayer->setFrame({8, (h - 12) / 2, 7, 12});
            setPath(*shapeLayer, buttonBar(shapeLayer == arrow_), palette().accentInk());
        }
        bar_->setValue(Channel::opacity, hover ? 0 : 1); arrow_->setValue(Channel::opacity, hover ? 1 : 0);
        break;
    }
    const double leading = kind_ == Kind::secondary ? (height_ == Height::compact ? 20 : 22) : padding;
    double x = leading + std::round(std::max(0.0, (bounds.width - preferred) / 2));
    if (glyph_) { glyph_->setFrame({x, (h - 15) / 2, 15, 15}); x += 15 + 8; }
    // An icon-only button's empty line keeps a zero width (the rasterizer refuses negative rects; CA ignores them).
    setLine(*text_, {x, (h - font) / 2, std::max(0.0, bounds.width - x - padding + 2), font});
    layer().setValue(Channel::opacity, enabled() ? 1 : .35);
    setTarget(bounds, kind_ == Kind::secondary ? std::nullopt : std::optional<PathBuilder>(chamfer(bounds, metric::buttonChamfer)));
}
void Button::hoverChanged() {
    applyHoverMotion(*this, 1, 1, .965, brighten_);
    if (kind_ != Kind::secondary) return;
    const bool on = hovered() && enabled();
    const auto c = clock();
    motion::animateTint(*plate_, palette().token(on ? ThemeToken::secondaryButtonHover : ThemeToken::secondaryButton), duration::hover, c, easing::ease);
    motion::animate(*bar_, Channel::opacity, on ? 0 : 1, .2, c, easing::ease);
    motion::animate(*arrow_, Channel::opacity, on ? 1 : 0, .2, c, easing::ease);
}
void Button::pressChanged() { applyHoverMotion(*this, 1, 1, .965, brighten_); }
void Button::enabledChanged() { layer().setValue(Channel::opacity, enabled() ? 1 : .35); hoverChanged(); }

// ---------------------------------------------------------------------------
// HUDIconButton (HUDDesignButtons.swift:124-178)
// ---------------------------------------------------------------------------
IconButton::IconButton(Builder& b, std::string id, std::string text, Icon icon, Style style, bool round, bool big,
    std::optional<double> side, std::optional<double> iconSize)
    : Control(b.env, id, std::move(text), b.layer(id)), style_(style), side_(side.value_or(big ? 44 : 30)),
      iconSize_(iconSize.value_or(style == Style::back ? 14 : (big ? 20 : 15))), round_(round), icon_(icon) {
    Layer& l = layer();
    l.setAnimatable();                                   // hover/press scale, disabled opacity
    l.setBounds({side_, side_});
    fill_ = &b.layer(sub(this->id(), "icon.plate"));
    ring_ = &b.layer(sub(this->id(), "icon.plate.ring"));
    glyph_ = &makeIcon(b, sub(this->id(), "icon.glyph"), icon, iconSize_, white(1));
    glyph_->setAnimatable();
    glyph_->setPosition({side_ / 2, side_ / 2});
    const Rect bounds = localBounds(l);
    const double radius = style == Style::back || round_ ? side_ / 2 : 0;
    const double border = style == Style::back ? (side_ < 30 ? 1.5 : 2) : (style == Style::on ? 0 : 1);
    fill_->setFrame(bounds); ring_->setFrame(bounds);
    setTintedBackground(*fill_, clear, radius);
    if (border > 0) setTintedBorder(*ring_, clear, border, radius); else { ring_->setAnimatable(); ring_->setTint(clear); }
    l.add(*fill_); l.add(*ring_); l.add(*glyph_);
    playCreationFade();
    setTarget(bounds, radius > 0 ? std::optional<PathBuilder>(PathBuilder().ellipse(bounds)) : std::nullopt);
}
// HUDIconButton.init calls applyStyle while the plate already has a
// superlayer, so its first colours animate (the oracle records them): the fill
// from no colour (clear) and the border from CALayer's default black, over .22
// (back .12) ease. Visible: header and pop-over buttons are not in a page
// stagger, and an in-place re-render rebuilds every button.
void IconButton::playCreationFade() {
    fill_->setTint(clear);
    ring_->setTint(black(1));
    applyStyle(true);
}
void IconButton::setOn(bool value) { if (value != on_) { on_ = value; applyStyle(true); } }
void IconButton::setIcon(Icon icon) { icon_ = icon; design::setIcon(*glyph_, icon, iconSize_, white(1)); }
// applyStyle (:155-169).
void IconButton::applyStyle(bool animated) {
    const auto& p = palette();
    const bool hover = hovered() && enabled();
    Color fill, ring, icon;
    if (style_ == Style::back) { fill = hover ? p.foreground(1) : clear; ring = p.tone(0xDDDDDD); icon = hover ? p.tone(0x111111) : p.foreground(1); }
    else if (on_) { fill = p.yellow(); ring = clear; icon = fixed::nearBlack; }
    else if (style_ == Style::light) { fill = fixed::lightButton[0]; ring = rgb(0xB9BAB5); icon = fixed::counterBackground; }
    else { fill = p.token(ThemeToken::iconButton); ring = hover ? p.tone(0x888888) : p.token(ThemeToken::ringIcon); icon = p.token(ThemeToken::textOnDark); }
    const double time = animated ? (style_ == Style::back ? .12 : duration::hover) : 0;
    const auto c = clock();
    motion::animateTint(*fill_, fill, time, c, easing::ease);
    motion::animateTint(*ring_, style_ == Style::on || on_ ? clear : ring, time, c, easing::ease);
    glyph_->setTint(icon);
    layer().setValue(Channel::opacity, enabled() ? 1 : .35);
}
void IconButton::hoverChanged() {
    applyStyle(true);
    if (style_ != Style::back) applyHoverMotion(*this, 0, 1.06, .94, nullptr);
}
void IconButton::pressChanged() { if (style_ != Style::back) applyHoverMotion(*this, 0, 1.06, .94, nullptr); }
void IconButton::enabledChanged() { applyStyle(true); }

// ---------------------------------------------------------------------------
// HUDPill (HUDDesignButtons.swift:181-241)
// ---------------------------------------------------------------------------
Pill::Pill(Builder& b, std::string id, std::string title, Icon icon, Kind kind) : Control(b.env, id, title, b.layer(id)), kind_(kind) {
    const auto& p = palette();
    // On the confirm band: Light inverts the neutrals with the band.
    const Color foreground = kind == Kind::cancel ? p.tone(0xECECEC) : p.tone(0x161616);
    const Color glyphColor = kind == Kind::cancel ? p.tone(0x2A2A2A) : (kind == Kind::danger ? white(1) : p.foreground(1));
    const double labelWidth = env().text.width(title, type::pill());
    const double width = std::max(150.0, 16 + 22 + 8 + labelWidth + 10 + 28 + 5);
    const Rect bounds{0, 0, width, 38};
    Layer& l = layer();
    l.setAnimatable();                                   // press scale
    l.setBounds({width, 38});
    if (kind != Kind::cancel) {
        ring_ = &b.layer(sub(this->id(), "pill.ring"));
        ring_->setFrame({-3, -3, width + 6, 44});
        setBorder(*ring_, std::nullopt, kind == Kind::danger ? rgb(0xE5383B, .9) : p.tone(0xE2E0E1, .9), 1, 22);
        Layer& gap = b.layer(sub(this->id(), "pill.ring.gap"));
        gap.setFrame({1, 1, width + 4, 42});
        setBorder(gap, std::nullopt, p.tone(0x1D1D1D), 2, 21);
        ring_->add(gap); l.add(*ring_);
    }
    plate_ = &b.layer(sub(this->id(), "pill.plate"));
    plate_->setFrame(bounds);
    setTintedBackground(*plate_, kind == Kind::cancel ? p.tone(0x4A4A4A, .9) : p.tone(0xE2E0E1), 19);
    l.add(*plate_);
    circle_ = &b.layer(sub(this->id(), "pill.glyph"));
    const Rect glyphRect{width - 5 - 28, 5, 28, 28};
    circle_->setAnimatable();                            // scale 1.1 and -8 deg on hover
    circle_->setFrame(glyphRect);
    setBackground(*circle_, kind == Kind::cancel ? p.tone(0xEDEDED) : (kind == Kind::danger ? fixed::red : p.tone(0x1B1B1B)), 14);
    glyph_ = &makeIcon(b, sub(this->id(), "pill.glyph.icon"), icon, 14, glyphColor);
    glyph_->setPosition({14, 14});
    circle_->add(*glyph_);
    const double labelX = glyphRect.x - 10 - labelWidth;
    Layer& text = makeText(b, sub(this->id(), "pill.text"), std::move(title), type::pill().colored(foreground), {labelX, 12, labelWidth + 2, 14});
    line_ = &b.layer(sub(this->id(), "pill.line"));
    line_->setFrame({16, 18.5, std::max(22.0, labelX - 8 - 16), 1});
    setBackground(*line_, foreground);
    line_->setAnimatable(); line_->setValue(Channel::opacity, .4);
    l.add(*line_); l.add(text); l.add(*circle_);
    setTarget(bounds, roundedRect(bounds, 19));
}
void Pill::hoverChanged() {
    const bool on = hovered() && enabled();
    const auto& p = palette(); const auto c = clock();
    const Color fill = kind_ == Kind::cancel ? (on ? p.tone(0x606060, .95) : p.tone(0x4A4A4A, .9)) : (on ? p.foreground(1) : p.tone(0xE2E0E1));
    motion::animateTint(*plate_, fill, .12, c, easing::ease);
    motion::animate(*line_, Channel::opacity, on ? .75 : .4, duration::hover, c, easing::ease);
    // "transform" to scale(1.1) rotate(-8 deg) over .25 s E: one channel per component.
    motion::animate(*circle_, Channel::scaleX, on ? 1.1 : 1, .25, c);
    motion::animate(*circle_, Channel::scaleY, on ? 1.1 : 1, .25, c);
    motion::animate(*circle_, Channel::rotation, on ? -8 * pi / 180 : 0, .25, c);
}
void Pill::pressChanged() {
    const auto c = clock();
    motion::animate(layer(), Channel::scaleX, pressed() ? .97 : 1, duration::press, c, easing::ease);
    motion::animate(layer(), Channel::scaleY, pressed() ? .97 : 1, duration::press, c, easing::ease);
}

// ---------------------------------------------------------------------------
// HUDSwatch (HUDDesignButtons.swift:246-283)
// ---------------------------------------------------------------------------
namespace {
// The conic custom-colour wheel (:255-258): FF5A5A FFD84A 7BE37B 4AC6FF A77BFF FF5A5A
// evenly round the circle from 12 o'clock, clockwise in the top-down layer.
Json conicWheel(std::string id, double size) {
    static constexpr std::array<Hex, 6> stops{0xFF5A5A, 0xFFD84A, 0x7BE37B, 0x4AC6FF, 0xA77BFF, 0xFF5A5A};
    constexpr int wedges = 90;
    Json node = frameNode(id, {0, 0, size, size});
    Json::Array children;
    const double r = size / 2;
    for (int i = 0; i < wedges; ++i) {
        const double t0 = double(i) / wedges, t1 = double(i + 1) / wedges, mid = (t0 + t1) / 2 * 5;
        const auto k = std::min<std::size_t>(4, std::size_t(mid));
        const Color a = rgb(stops[k]), b = rgb(stops[k + 1]);
        const double f = mid - double(k);
        const Color c{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1};
        // Overlap each wedge slightly so the antialiased seams close.
        const double a0 = (t0 - .002) * 2 * pi - pi / 2, a1 = (t1 + .002) * 2 * pi - pi / 2;
        PathBuilder p;
        p.move({r, r}).line({r + 2 * r * std::cos(a0), r + 2 * r * std::sin(a0)}).line({r + 2 * r * std::cos(a1), r + 2 * r * std::sin(a1)}).close();
        children.push_back(shape(id + ".wedge" + std::to_string(i), {0, 0, size, size}, p, c));
    }
    addChildren(node, std::move(children));
    addShapeMask(node, PathBuilder().ellipse({0, 0, size, size}));
    return node;
}
}
Swatch::Swatch(Builder& b, std::string id, std::string text, std::optional<Color> color, double size)
    : Control(b.env, id, std::move(text), b.layer(id)) {
    Layer& l = layer();
    l.setAnimatable();                                   // hover scale
    l.setBounds({size, size});
    const Rect bounds{0, 0, size, size};
    ring_ = &b.layer(sub(this->id(), color ? "swatch.fill" : "swatch.wheel"));
    ring_->setFrame(bounds);
    if (color) setBorder(*ring_, *color, fixed::nearBlack, 1);
    else {
        Json wheel = conicWheel(ring_->id(), size);
        addChildren(wheel, {bordered(ring_->id() + ".ring", bounds, std::nullopt, fixed::nearBlack, 1, size / 2)});
        ring_->setContent(std::move(wheel));
        ring_->setMasksToBounds(true, size / 2);
    }
    selection_ = &b.layer(sub(this->id(), "swatch.ring"));
    selection_->setFrame({-5, -5, size + 10, size + 10});
    Json ring = bordered(selection_->id(), {0, 0, size + 10, size + 10}, std::nullopt, palette().yellow(), 3);
    addChildren(ring, {bordered(selection_->id() + ".inner", {3, 3, size + 4, size + 4}, std::nullopt, fixed::nearBlack, 2)});
    selection_->setContent(std::move(ring));
    selection_->setHidden(true);
    l.add(*ring_); l.add(*selection_);
    setTarget(bounds);
}
void Swatch::setSelected(bool value) { if (value != selected_) { selected_ = value; selection_->setHidden(!value); } }
void Swatch::hoverChanged() {
    const double s = hovered() && enabled() ? 1.08 : 1;
    motion::animate(layer(), Channel::scaleX, s, duration::hover, clock());
    motion::animate(layer(), Channel::scaleY, s, duration::hover, clock());
}
void Swatch::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, AccessibilityRole::tab); item.on = selected_;
    out.push_back(std::move(item));
}

// ---------------------------------------------------------------------------
// HUDDragGrip (HUDDesignButtons.swift:290-349)
// ---------------------------------------------------------------------------
DragGrip::DragGrip(Builder& b, std::string id, std::string text, Size size, Point dotsAt) : Control(b.env, id, std::move(text), b.layer(id)) {
    layer().setBounds(size);
    dots_ = &b.layer(sub(this->id(), "grip.dots"));
    dots_->setFrame({dotsAt.x, dotsAt.y, 6, 10});
    setTintedPath(*dots_, gripDots(2, 3), fixed::bracket);
    layer().add(*dots_);
    setTarget(localBounds(layer()));
}
void DragGrip::hoverChanged() {
    motion::animateTint(*dots_, hovered() || dragging_ ? fixed::nearBlack : fixed::bracket, duration::hover, clock(), easing::ease);
}
bool DragGrip::pointerDown(Point p) {
    if (!enabled()) return false;
    pressPoint_ = p; dragging_ = false;
    return true;
}
void DragGrip::pointerDragged(Point p) {
    if (!pressPoint_) return;
    if (!dragging_) {
        if (std::hypot(p.x - pressPoint_->x, p.y - pressPoint_->y) <= 3) return;
        dragging_ = true;
        if (onDrag) onDrag(Phase::began, *pressPoint_);
    }
    if (onDrag) onDrag(Phase::moved, p);
}
void DragGrip::pointerUp(Point p) {
    if (dragging_) { if (onDrag) onDrag(Phase::ended, p); }
    else if (contains(p)) activate();
    pressPoint_.reset(); dragging_ = false; hoverChanged();
}
void DragGrip::clearInteractionState() {
    if (dragging_ && pressPoint_ && onDrag) onDrag(Phase::ended, *pressPoint_);
    pressPoint_.reset(); dragging_ = false;
    Control::clearInteractionState();
}
std::optional<AccessibilityRole> DragGrip::accessibilityRole() const {
    return label.empty() ? std::nullopt : std::optional<AccessibilityRole>(AccessibilityRole::button);
}

// ---------------------------------------------------------------------------
// HUDSlider (HUDDesignSlider.swift:8-170)
// ---------------------------------------------------------------------------
double Slider::Range::clamp(double v) const noexcept {
    const double snapped = step > 0 ? std::round((v - minimum) / step) * step + minimum : v;
    return std::min(maximum, std::max(minimum, snapped));
}
double Slider::Range::fraction(double v) const noexcept {
    return maximum > minimum ? (std::min(maximum, std::max(minimum, v)) - minimum) / (maximum - minimum) : 0;
}
Slider::Slider(Builder& b, std::string id, std::string text, double value, Range range, ValueFormat format, double width,
    bool dark, bool showsReadout, bool showsTicks)
    : Control(b.env, id, std::move(text), b.layer(id)), range_(range), format_(format),
      dark_(dark && !b.env.palette.isLight()), value_(range.clamp(value)), builder_(&b) {
    // Light theme: the surfaces under a `dark` slider turn pale, so it takes the plate variant (:44-46).
    trackWidth_ = std::max(40.0, width - (showsReadout ? readoutWidth + 8 : 0));
    const auto& p = palette();
    Layer& l = layer();
    l.setBounds({width, height});
    track_ = &b.layer(sub(this->id(), "slider.track"));
    track_->setFrame({0, 11, trackWidth_, 5});
    if (dark_) setBackground(*track_, rgb(0x3A3A3A)); else setBorder(*track_, fixed::sliderTrack, fixed::nearBlack, 1);
    fill_ = &b.layer(sub(this->id(), "slider.fill"));
    fill_->setAnimatable();
    fill_->setFrame({0, 11, trackWidth_, 5});
    fill_->setAnchorPoint({0, .5}); fill_->setPosition({0, 13.5});
    const std::array<Color, 2> colors{p.yellowLow(), p.yellow()};
    fill_->setContent(gradient(fill_->id(), localBounds(*fill_), colors, {}, {0, .5}, {1, .5}));
    zero_ = &b.layer(sub(this->id(), "slider.zero"));
    zero_->setHidden(!range.zero);
    ticks_ = &b.layer(sub(this->id(), "slider.ticks"));
    ticks_->setFrame({0, 21, trackWidth_, 4});
    setPath(*ticks_, ticks(11, trackWidth_, 4), fixed::sliderTick);
    ticks_->setHidden(!showsTicks);
    thumb_ = &b.layer(sub(this->id(), "slider.thumb"));
    thumb_->setAnimatable();                             // follows the value; scaleY on hover/drag
    thumb_->setBounds({9, 17});
    setBackground(*thumb_, dark_ ? rgb(0xEDEDED) : fixed::counterBackground);
    core_ = &b.layer(sub(this->id(), "slider.core"));
    core_->setFrame({3.5, 3, 2, 11});
    setBackground(*core_, dark_ ? fixed::nearBlack : p.yellow());
    thumb_->add(*core_);
    readout_ = &makeText(b, sub(this->id(), "slider.readout"), "", type::readout(), {width - readoutWidth, 9, readoutWidth, 12}, TextAlignment::right);
    readout_->setHidden(!showsReadout);
    for (Layer* child : {track_, fill_, zero_, ticks_, thumb_, readout_}) l.add(*child);
    setTarget({0, 0, trackWidth_, height});
    render();
}
Slider::~Slider() = default;
bool Slider::contains(Point p) const { return inRect({-4.5, 0, trackWidth_ + 9, height}, p); }
void Slider::setValue(double v) { const double next = range_.clamp(v); if (next != value_) { value_ = next; render(); } }
void Slider::nudge(double direction) {
    const double next = range_.clamp(value_ + direction * range_.step);
    if (next == value_) return;
    value_ = next; render();
    if (onChange) onChange(value_, true);
}
// render (:92-108).
void Slider::render() {
    const double x = range_.fraction(value_) * trackWidth_;
    double left = 0, width = x;
    if (range_.zero) {
        const double z = range_.fraction(*range_.zero) * trackWidth_;
        left = std::min(x, z); width = std::abs(x - z);
        zero_->setFrame({z - .5, 8, 1, 11}); setBackground(*zero_, fixed::nearBlack);
    }
    fill_->setValue(Channel::translateX, left);
    fill_->setValue(Channel::scaleX, trackWidth_ > 0 ? width / trackWidth_ : 0);
    thumb_->setPosition({x, 13.5});
    const auto& p = palette();
    editText(*readout_, [&](TextBlock& block) {
        block.runs = {{"[ ", type::readoutBracket()},
                      {formatValue(format_, value_), dark_ ? type::readout().colored(p.token(ThemeToken::textOnDark)) : type::readout()},
                      {" ]", type::readoutBracket()}};
    });
    updateBubble();
}
double Slider::valueAt(Point p) const {
    return range_.clamp(range_.minimum + std::min(1.0, std::max(0.0, p.x / std::max(1.0, trackWidth_))) * (range_.maximum - range_.minimum));
}
bool Slider::pointerDown(Point p) {
    if (!enabled()) return false;
    dragging_ = true;
    renderThumbGrowth();
    const double next = valueAt(p);
    if (next != value_) { value_ = next; render(); if (onChange) onChange(value_, false); }
    showBubble();
    return true;
}
void Slider::pointerDragged(Point p) {
    if (!dragging_) return;
    const double next = valueAt(p);
    if (next == value_) return;
    value_ = next; render();
    if (onChange) onChange(value_, false);
}
void Slider::pointerUp(Point) {
    if (!dragging_) return;
    dragging_ = false;
    renderThumbGrowth(); if (bubble_) bubble_->setVisible(false, clock());
    if (onChange) onChange(value_, true);
}
void Slider::cancelDrag() {
    if (!dragging_) return;
    dragging_ = false; renderThumbGrowth();
    if (bubble_) bubble_->setVisible(false, clock());
}
void Slider::hoverChanged() { renderThumbGrowth(); }
void Slider::enabledChanged() { layer().setValue(Channel::opacity, enabled() ? 1 : .35); }
void Slider::renderThumbGrowth() {
    const bool grown = (hovered() || dragging_) && enabled();
    motion::animate(*thumb_, Channel::scaleY, grown ? 21.0 / 17.0 : 1, duration::thumb, clock(), easing::ease);
}
void Slider::showBubble() {
    if (!bubbleHost_) return;
    if (!bubble_) bubble_ = std::make_unique<SliderBubble>(*builder_, sub(id(), "slider.bubble"));
    if (bubble_->layer().parent() != bubbleHost_) { bubble_->layer().removeFromParent(); bubbleHost_->add(bubble_->layer()); }
    updateBubble();
    bubble_->setVisible(true, clock());
}
void Slider::updateBubble() {
    if (!bubble_ || !bubbleHost_ || !dragging_) return;
    const auto anchor = design::convert(Point{range_.fraction(value_) * trackWidth_, 2}, layer(), *bubbleHost_);
    if (anchor) bubble_->show(formatValue(format_, value_), *anchor);
}
void Slider::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, AccessibilityRole::slider);
    item.number = value_; item.minimum = range_.minimum; item.maximum = range_.maximum; item.step = range_.step;
    item.value = formatValue(format_, value_);
    out.push_back(std::move(item));
}

// HUDSliderBubble (HUDDesignSlider.swift:174-221).
SliderBubble::SliderBubble(Builder& b, std::string_view id) : builder_(&b) {
    layer_ = &b.layer(id);
    shadow_ = &b.layer(sub(id, "shadow")); body_ = &b.layer(sub(id, "body")); arrow_ = &b.layer(sub(id, "arrow"));
    text_ = &makeText(b, sub(id, "text"), "", type::sliderBubble(), {}, TextAlignment::center);
    layer_->setAnimatable();
    layer_->setPartRoot(true);                           // hosted in the canvas's top layer
    layer_->setZPosition(order::bubble);
    layer_->setAnchorPoint({.5, 1});
    layer_->setValue(Channel::opacity, 0);
    layer_->setValue(Channel::scaleX, .9); layer_->setValue(Channel::scaleY, .9);
    for (Layer* child : {shadow_, body_, arrow_, text_}) layer_->add(*child);
}
void SliderBubble::show(std::string value, Point anchor) {
    editText(*text_, [&](TextBlock& block) { block.setText(std::move(value)); });
    const double width = std::ceil(preferredWidth(textOf(*text_), builder_->env.text)) + 16, height = 21;
    // translate(-50%, -100% - 6px): bottom-centre 6 px above the anchor.
    layer_->setBounds({width, height + 4});
    layer_->setPosition({anchor.x, anchor.y - 6 + 4});
    shadow_->setFrame({0, 2, width, height}); setBackground(*shadow_, fixed::nearBlack, 10);
    body_->setFrame({0, 0, width, height});
    Json body = solid(body_->id(), {0, 0, width, height}, rgb(0xFAFAFA), 10);
    addShadow(body, Shadow{black(1), .45, 8, {0, 6}, roundedRect({0, 0, width, height}, 10)});
    body_->setContent(std::move(body));
    arrow_->setFrame({width / 2 - 4, height, 8, 4}); setPath(*arrow_, triangleDown(8, 4), rgb(0xFAFAFA));
    setLine(*text_, {0, 5, width, 11});
}
void SliderBubble::setVisible(bool visible, const motion::Clock& c) {
    if (visible == visible_) return;
    visible_ = visible;
    motion::animate(*layer_, Channel::opacity, visible ? 1 : 0, duration::bubbleFade, c, easing::ease);
    motion::animate(*layer_, Channel::scaleX, visible ? 1 : .9, duration::bubbleScale, c, easing::snap);
    motion::animate(*layer_, Channel::scaleY, visible ? 1 : .9, duration::bubbleScale, c, easing::snap);
}

// ---------------------------------------------------------------------------
// HUDDropdown (HUDDesignMenu.swift:27-100)
// ---------------------------------------------------------------------------
Dropdown::Dropdown(Builder& b, std::string id, std::string text, std::string value, std::optional<TextRegion> valueLanguage,
    Height height, std::optional<double> width)
    : Control(b.env, id, std::move(text), b.layer(id)), builder_(&b), value_(value) {
    const auto& p = palette();
    const TextStyle style = height == Height::compact ? type::dropdownValue(p).sized(12) : type::dropdownValue(p);
    const double h = height == Height::compact ? 26 : 30;
    const double natural = 12 + env().text.width(value, style, valueLanguage) + 14 + 9 + 10;
    const double fieldWidth = width.value_or(height == Height::compact ? std::max(96.0, natural) : 178);
    Layer& l = layer();
    l.setBounds({fieldWidth, h});
    setBorder(l, p.token(ThemeToken::control), p.token(ThemeToken::ringField), 1);
    bar_ = &b.layer(sub(this->id(), "dropdown.bar"));
    bar_->setFrame({0, 0, 2, h});
    bar_->setAnimatable();
    setBackground(*bar_, p.accentInk());
    bar_->setValue(Channel::scaleY, 0);
    text_ = &makeText(b, sub(this->id(), "dropdown.value"), "", style, {});
    caret_ = &b.layer(sub(this->id(), "dropdown.caret"));
    caret_->setAnimatable();
    caret_->setBounds({9, 6});
    caret_->setPosition({fieldWidth - 10 - 4.5, h / 2});
    setPath(*caret_, triangleDown(9, 6), p.accentInk());
    l.add(*bar_); l.add(*text_); l.add(*caret_);
    setValue(std::move(value), valueLanguage);
    setTarget(localBounds(l));
}
void Dropdown::setValue(std::string value, std::optional<TextRegion> language) {
    value_ = value;
    const auto size = layer().bounds();
    editText(*text_, [&](TextBlock& block) {
        block.setText(std::move(value)); block.language = language;
        const double font = block.style().size;
        block.setLineFrame({12, (size.height - font) / 2, size.width - 12 - 14 - 9 - 10, font});
    });
}
// setArtwork (:77-86): `imageSize` is the source's pixel size (the Mac reads it from the CGImage).
void Dropdown::setArtwork(std::optional<ImageSource> source, Size imageSize, double height) {
    if (!artwork_) { artwork_ = &builder_->layer(sub(id(), "dropdown.artwork")); layer().insertBelow(*artwork_, *caret_); }
    const auto size = layer().bounds();
    const Size art = source ? Size{height * imageSize.width / std::max(1.0, imageSize.height), height} : Size{};
    artwork_->setFrame({12, (size.height - art.height) / 2, std::min(art.width, size.width - 12 - 14 - 9 - 10), art.height});
    if (source) artwork_->setContent(image(artwork_->id(), localBounds(*artwork_), *source, "resizeAspect")); else artwork_->clearContent();
    artwork_->setHidden(!source);
    text_->setHidden(source.has_value());
}
void Dropdown::setOpen(bool open) { if (open != open_) { open_ = open; renderState(); } }
// renderState (:88-95). The caret's transform is R(open ? pi : 0) then a 2 px
// move toward the field's bottom while hovered (T(0, -2) in the rotated frame
// when open is the same +2 px in the field): rotation and translateY channels.
void Dropdown::renderState() {
    const bool hover = hovered() && enabled(), engaged = open_ || hover;
    const auto c = clock();
    motion::animate(*bar_, Channel::scaleY, engaged ? 1 : 0, duration::accentBar, c);
    motion::animate(*caret_, Channel::rotation, open_ ? pi : 0, duration::caret, c);
    motion::animate(*caret_, Channel::translateY, hover ? 2 : 0, duration::caret, c);
}
void Dropdown::hoverChanged() { renderState(); }
void Dropdown::enabledChanged() { layer().setValue(Channel::opacity, enabled() ? 1 : .35); }
void Dropdown::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (label.empty()) return;
    auto item = accessibilityItem(root, AccessibilityRole::popUp); item.value = value_;
    out.push_back(std::move(item));
}

// ---------------------------------------------------------------------------
// HUDTabs (HUDDesignTabs.swift:7-152)
// ---------------------------------------------------------------------------
class Tabs::Tab final : public Control {
public:
    Tab(Builder& b, std::string id, std::string title, std::optional<Icon> icon, int index, bool compact, Tabs& bar)
        : Control(b.env, id, title, b.layer(id)), index_(index), bar_(&bar) {
        const auto& p = palette();
        // Compact (:23) is a fresh 11 px medium style: no tracking.
        style_ = compact ? TextStyle{11, FontWeight::medium, p.token(ThemeToken::tabText)} : type::tab(p).sized(13).weighted(FontWeight::medium);
        Layer& l = layer();
        l.setAnimatable();                               // 1 px lift on hover
        hoverFill_ = &b.layer(sub(this->id(), "tab.hover"));
        fill_ = &b.layer(sub(this->id(), "tab.fill"));
        underline_ = &b.layer(sub(this->id(), "tab.underline"));
        dot_ = &b.layer(sub(this->id(), "tab.dot"));
        text_ = &makeText(b, sub(this->id(), "tab.text"), std::move(title), style_.colored(white(1)), {});
        text_->setAnimatable();
        hoverFill_->setAnimatable(); hoverFill_->setValue(Channel::opacity, 0);
        fill_->setAnimatable();                          // the tabOn wipe
        underline_->setHidden(true);
        dot_->setBounds({7, 7}); setBackground(*dot_, fixed::orange);
        dot_->setBaseTransform(Affine::rotation(pi / 4)); dot_->setHidden(true);
        l.add(*hoverFill_); l.add(*fill_); l.add(*underline_);
        if (icon) { glyph_ = &makeIcon(b, sub(this->id(), "tab.icon"), *icon, 14, white(1)); glyph_->setAnimatable(); l.add(*glyph_); }
        l.add(*text_); l.add(*dot_);
    }
    // layout(width:active:) (:37-59).
    void layout(double width, bool active) {
        const auto& p = palette();
        const double h = active ? 37 : 34;
        const Rect bounds{0, 0, width, h};
        layer().setBounds({width, h});
        fill_->setFrame(bounds); hoverFill_->setFrame(bounds);
        setBackground(*hoverFill_, p.foreground(.06));
        const std::array<Color, 3> colors{p.yellowHigh(), p.yellow(), p.yellow()};
        const std::array<double, 3> stops{0, .45, 1};
        if (active) fill_->setContent(plate(fill_->id(), bounds, colors, stops, 0)); else fill_->clearContent();
        setHiddenIf(*fill_, !active);
        const TextStyle style = style_.weighted(active ? FontWeight::bold : FontWeight::medium);
        const Color ink = active ? fixed::nearBlack : p.token(ThemeToken::tabText);
        editText(*text_, [&](TextBlock& block) { block.setStyle(style.colored(white(1))); });
        const double textWidth = preferredWidth(textOf(*text_), env().text), iconWidth = glyph_ ? 14 + 7 : 0;
        const double start = (width - textWidth - iconWidth) / 2;
        if (glyph_) { glyph_->setFrame({start, (h - 14) / 2, 14, 14}); glyph_->setTint(ink); }
        text_->setTint(ink);
        setLine(*text_, {start + iconWidth, (h - style_.size) / 2, textWidth + 1, style_.size});
        underline_->setFrame({width * .32, h + 3, width * .36, 3}); setBackground(*underline_, p.accentInk());
        setHiddenIf(*underline_, !active);
        dot_->setPosition({width - 6 - 3.5, 6 + 3.5});
        setTarget(bounds);
    }
    void hoverChanged() override {
        if (bar_->selected_ == index_) return;
        const bool on = hovered() && enabled();
        const auto c = clock();
        motion::animate(*hoverFill_, Channel::opacity, on ? 1 : 0, duration::hover, c, easing::ease);
        motion::animate(layer(), Channel::translateY, on ? -1 : 0, duration::hover, c);
        text_->setTint(on ? palette().foreground(1) : palette().token(ThemeToken::tabText));
    }
    void activate() override { if (enabled()) bar_->select(index_, true); }
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::tab; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, AccessibilityRole::tab); item.on = bar_->selected_ == index_;
        out.push_back(std::move(item));
    }
    void setNews(bool on) { setHiddenIf(*dot_, !on); }
    Layer& fill() const noexcept { return *fill_; }
    int index_;
private:
    Tabs* bar_; TextStyle style_;
    Layer *hoverFill_, *fill_, *underline_, *dot_, *text_, *glyph_{};
};

Tabs::Tabs(Builder& b, std::string id, std::vector<Title> titles, int selectedIndex, double width, bool keycaps, bool compact)
    : builder_(&b), layer_(&b.layer(id)), selected_(selectedIndex), keycaps_(keycaps) {
    const auto& p = b.env.palette;
    layer_->setBounds({width, 34});
    setBackground(*layer_, p.token(ThemeToken::tabBar));
    // A layer's own border draws above its sublayers and would cross the raised
    // active tab; this frame sits beneath the tabs instead (round-1 fix, :89-93).
    Layer& frame = b.layer(sub(id, "tabs.frame"));
    frame.setFrame({0, 0, width, 34});
    setBorder(frame, std::nullopt, p.token(ThemeToken::ringTab), 1);
    layer_->add(frame);
    if (keycaps) {
        const char* keys[] = {"Q", "E"};
        for (int index = 0; index < 2; ++index) {
            Layer& cell = b.layer(sub(id, index ? "tabs.keycap.e" : "tabs.keycap.q"));
            cell.setFrame({index == 0 ? 6.5 : width - 21.5, 9.5, 15, 15});
            setBorder(cell, std::nullopt, p.tone(0x6A6A6A), 1);
            cell.add(makeText(b, sub(cell.id(), "text"), keys[index], type::tabKeycap(p), {0, 3, 15, 9}, TextAlignment::center));
            layer_->add(cell);
        }
    }
    for (std::size_t index = 0; index < titles.size(); ++index) {
        auto& tab = b.make<Tab>(id + ":" + std::to_string(index), std::move(titles[index].text), titles[index].icon, int(index), compact, *this);
        layer_->add(tab.layer());
        tabs_.push_back(&tab);
    }
    for (std::size_t index = 1; index < tabs_.size(); ++index) {
        Layer& line = b.layer(sub(id, "tabs.separator." + std::to_string(index)));
        setBackground(line, p.tone(0x4A4A4A));
        line.setBounds({1, 14}); line.setFrame({0, 10, 1, 14}); setBackground(line, p.tone(0x4A4A4A));
        layer_->add(line);
        separators_.push_back(&line);
    }
    layoutTabs();
}
// layoutTabs (:110-128).
void Tabs::layoutTabs() {
    const double inset = keycaps_ ? 28 : 0;
    const double width = (layer_->bounds().width - inset * 2) / double(std::max<std::size_t>(1, tabs_.size()));
    for (Control* control : tabs_) {
        auto& tab = static_cast<Tab&>(*control);
        const bool active = tab.index_ == selected_;
        tab.layout(width, active);
        tab.layer().setPosition({inset + width * (double(tab.index_) + .5), active ? 34.0 / 2 - 1.5 : 17});
        tab.layer().setZPosition(active ? 1 : 0);
        if (tab.index_ > 0) {
            Layer& line = *separators_[std::size_t(tab.index_ - 1)];
            line.setFrame({inset + width * double(tab.index_), 10, 1, 14});
            setHiddenIf(line, tab.index_ == selected_ || tab.index_ - 1 == selected_);
        }
    }
}
void Tabs::select(int index, bool notify) {
    if (index < 0 || std::size_t(index) >= tabs_.size() || index == selected_) return;
    const double direction = index > selected_ ? 1 : -1;
    selected_ = index;
    layoutTabs();
    motion::wipe(static_cast<Tab&>(*tabs_[std::size_t(index)]).fill(), WipeOrigin::centerX, duration::tabOn, builder_->env.clock());
    if (notify && onSelect) onSelect(index, direction);
}
// Q = previous, E = next, wrapping around (:140-149).
void Tabs::cycle(int step) {
    if (tabs_.empty()) return;
    const int count = int(tabs_.size());
    const int next = ((selected_ + step) % count + count) % count;
    if (next == selected_) return;
    const double direction = step > 0 ? 1 : -1;
    selected_ = next;
    layoutTabs();
    motion::wipe(static_cast<Tab&>(*tabs_[std::size_t(next)]).fill(), WipeOrigin::centerX, duration::tabOn, builder_->env.clock());
    if (onSelect) onSelect(next, direction);
}
void Tabs::setNews(int index, bool on) {
    if (index >= 0 && std::size_t(index) < tabs_.size()) static_cast<Tab&>(*tabs_[std::size_t(index)]).setNews(on);
}

// ---------------------------------------------------------------------------
// HUDKeycapGroup (HUDDesignBadges.swift:97-202)
// ---------------------------------------------------------------------------
KeycapGroup::KeycapGroup(Builder& b, std::string id, std::string text, std::vector<std::string> keys, bool interactive, double minimumWidth)
    : Control(b.env, id, std::move(text), b.layer(id)), builder_(&b), interactive_(interactive), minimumWidth_(minimumWidth) {
    const auto& p = palette();
    scan_ = &b.layer(sub(this->id(), "keys.scan"));
    keys_ = &b.layer(sub(this->id(), "keys.caps"));
    prompt_ = &makeText(b, sub(this->id(), "keys.prompt"), "", TextStyle{10.5, FontWeight::bold, p.accentInk(), 1, .1, true}, {});
    cursor_ = &makeText(b, sub(this->id(), "keys.cursor"), "_", TextStyle{10.5, FontWeight::bold, p.accentInk()}, {});
    cursor_->setAnimatable();                            // the `_` blink while recording
    scan_->setHidden(true); prompt_->setHidden(true); cursor_->setHidden(true);
    scan_->setMasksToBounds(true);
    for (Layer* child : {scan_, keys_, prompt_, cursor_}) layer().add(*child);
    setKeys(std::move(keys));
}
// keycap (:144-158).
Layer& KeycapGroup::keycap(Builder& b, std::string_view id, const std::string& key) {
    Layer& cap = b.layer(id);
    TextBlock block(key, type::keycap(), TextAlignment::center);
    const double width = std::max(34.0, std::ceil(preferredWidth(block, b.env.text)) + 16);
    Layer& shadow = b.layer(sub(id, "shadow"));
    shadow.setFrame({0, 2, width, 26}); setBackground(shadow, fixed::bracket);
    Layer& plateLayer = b.layer(sub(id, "plate"));
    plateLayer.setFrame({0, 0, width, 26});
    const std::array<Color, 2> colors{rgb(0xF2F3EE), rgb(0xCFD0CB)};
    Json node = plate(plateLayer.id(), {0, 0, width, 26}, colors, {}, 0);
    addChildren(node, {bordered(plateLayer.id() + ".ring", {0, 0, width, 26}, std::nullopt, rgb(0xB4B5B0), 1)});
    plateLayer.setContent(std::move(node));
    Layer& text = makeText(b, sub(id, "text"), key, type::keycap(), {0, 7.5, width, 11}, TextAlignment::center);
    cap.setBounds({width, 28});
    cap.add(shadow); cap.add(plateLayer); cap.add(text);
    return cap;
}
void KeycapGroup::setKeys(std::vector<std::string> keys) {
    Builder& b = *builder_;
    const auto& p = palette();
    for (Layer* child : std::vector<Layer*>(keys_->children())) child->removeFromParent();
    const std::string generation = std::to_string(++generation_);
    double x = 10;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (index > 0) {
            keys_->add(makeText(b, sub(id(), "keys.plus." + generation + "." + std::to_string(index)), "+",
                TextStyle{11, FontWeight::bold, p.tone(0x888888)}, {x, 11.5, 8, 11}));
            x += 8 + 6;
        }
        Layer& cap = keycap(b, sub(id(), "keycap." + generation + "." + std::to_string(index)), keys[index]);
        cap.setOrigin({x, 4});
        keys_->add(cap);
        x += cap.bounds().width + 6;
    }
    const double width = std::max(interactive_ ? minimumWidth_ : 0, x + 4);
    layer().setBounds({width, 34});
    keys_->setFrame({0, 0, width, 34}); scan_->setFrame({0, 0, width, 34});
    setBorder(layer(), p.token(ThemeToken::tabBar), recording_ ? p.accentInk() : p.token(ThemeToken::ringField), 1);
    editText(*prompt_, [&](TextBlock& block) { block.setLineFrame({10, 11.75, width - 30, 10.5}); });
    setTarget(localBounds(layer()));
}
// setRecording (:162-198). The stripes layer is made once and resized; the two
// loops (scan, blink) exist only while recording and never under Reduce Motion.
void KeycapGroup::setRecording(bool recording, std::string prompt) {
    if (recording == recording_ && textOf(*prompt_).text() == prompt) return;
    recording_ = recording;
    const auto& p = palette();
    const double width = layer().bounds().width;
    setBorder(layer(), p.token(ThemeToken::tabBar), recording ? p.accentInk() : p.token(ThemeToken::ringField), 1);
    keys_->setHidden(recording);
    scan_->setHidden(!recording); prompt_->setHidden(!recording); cursor_->setHidden(!recording);
    editText(*prompt_, [&](TextBlock& block) { block.setText(std::move(prompt)); });
    editText(*cursor_, [&](TextBlock& block) { block.setLineFrame({10 + preferredWidth(textOf(*prompt_), env().text) + 2, 11.75, 8, 10.5}); });
    if (stripes_) { stripes_->removeAllMotion(); stripes_->setHidden(true); }
    cursor_->removeAllMotion();
    if (!recording) return;
    if (!stripes_) { stripes_ = &builder_->layer(sub(id(), "keys.scan.stripes")); stripes_->setAnimatable(); scan_->add(*stripes_); }
    stripes_->setHidden(false);
    stripes_->setValue(Channel::translateX, 0);
    stripes_->setFrame({0, 0, width * 2, 34});
    PathBuilder path;
    const double period = 12 * std::sqrt(2.0);
    for (double x = -34; x < width * 2 + 34; x += period)
        path.move({x, 34}).line({x + period / 2, 34}).line({x + period / 2 + 34, 0}).line({x + 34, 0}).close();
    Json node = solid(stripes_->id(), {0, 0, width * 2, 34}, p.token(ThemeToken::tabBar));
    addChildren(node, {shape(stripes_->id() + ".band", {0, 0, width * 2, 34}, path, p.tone(0x262414))});
    stripes_->setContent(std::move(node));
    const auto c = clock();
    if (c.reduceMotion) return;
    const std::array<double, 2> move{0, -34}, moveTimes{0, 1};
    motion::keyframes(*stripes_, Channel::translateX, move, moveTimes, 1, c, easing::linear, std::numeric_limits<double>::infinity());
    const std::array<double, 3> blink{1, .2, 1}, blinkTimes{0, .5, 1};
    motion::keyframes(*cursor_, Channel::opacity, blink, blinkTimes, 1.2, c, easing::easeInOut, std::numeric_limits<double>::infinity());
}
bool KeycapGroup::pointerDown(Point) { return interactive_ && enabled(); }
std::optional<AccessibilityRole> KeycapGroup::accessibilityRole() const {
    return interactive_ ? std::optional<AccessibilityRole>(AccessibilityRole::button) : std::nullopt;
}

// ---------------------------------------------------------------------------
// HUDFieldFrame (HUDDesignBadges.swift:206-238)
// ---------------------------------------------------------------------------
FieldFrame::FieldFrame(Builder& b, std::string_view id, Size size, bool light, bool plain)
    : builder_(&b), layer_(&b.layer(id)), bar_(&b.layer(sub(id, "field.bar"))), light_(light), plain_(plain) {
    layer_->setBounds(size);
    bar_->setFrame({0, 0, 3, size.height});
    layer_->add(*bar_);
    setState(State::normal);
}
void FieldFrame::setSize(Size size) {
    layer_->setBounds(size);
    bar_->setFrame({0, 0, 3, size.height});
    setState(state_);
}
void FieldFrame::setState(State state) {
    state_ = state;
    const auto& p = builder_->env.palette;
    const Color focus = focusColor.value_or(p.accentInk());
    // backgroundColor: the caller's (Personal ID), else none when plain, else the field.
    const std::optional<Color> fill = background ? background : (plain_ ? std::nullopt : std::optional<Color>(light_ ? rgb(0xF2F3EE) : p.token(ThemeToken::field)));
    if (plain_) {
        if (fill) setBackground(*layer_, *fill); else layer_->clearContent();
        setHiddenIf(*bar_, true);
        return;
    }
    const Color border = state == State::normal ? (light_ ? rgb(0xB4B5B0) : p.token(ThemeToken::ringField)) : (state == State::focused ? focus : fixed::red);
    setBorder(*layer_, fill, border, 1);
    if (state != State::normal) setBackground(*bar_, state == State::focused ? focus : fixed::red);
    setHiddenIf(*bar_, state == State::normal);
}
} // namespace endfield::modules::design
