// The list cards: ports of Sources/HUDDesignCards.swift (HUDTaskCard :8-174,
// HUDListCard :180-315, HUDArchiveTile :320-503).
//
// Recorded adaptation: HUDArchiveTile's selection brackets animate their path
// from inset -5 to -4 (.16 s E) under an opacity fade (.1 s ease); a path is
// not a numeric pose, so the two brackets are prepared surfaces cross-faded
// over the same .16 s E inside a container that carries the .1 s fade (lead
// answer Q7). The task card's action plate changes colour on hover through a
// tint (white authored), the rest of renderAction runs on content events only.
#include "modules/hud_design_cards.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_redaction.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::design {
namespace {
constexpr double pi = 3.14159265358979323846;
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
bool inRect(Rect r, Point p) noexcept { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
std::string tr(const Environment& env, std::string_view english, std::string_view chinese) { return core::localized(english, chinese, env.language); }
}

// ---------------------------------------------------------------------------
// HUDTaskCard (:8-174)
// ---------------------------------------------------------------------------
namespace {
const TextStyle& taskTitleStyle() { static const TextStyle style{14.5, FontWeight::medium, fixed::nearBlack, 1.15}; return style; }
}
class TaskCard::Action final : public Control {
public:
    Action(Builder& b, std::string id, TaskCard& card) : Control(b.env, id, "", b.layer(id)), card_(&card) {
        plate = &b.layer(sub(this->id(), "task.action"));
        glyph = &makeIcon(b, sub(this->id(), "task.action.check"), Icon::check, 12, b.env.palette.yellow());
        text = &makeText(b, sub(this->id(), "task.action.text"), "", TextStyle{11.5, FontWeight::medium, rgb(0x222222)}, {});
        layer().add(*plate); layer().add(*glyph); layer().add(*text);
    }
    void hoverChanged() override { card_->renderActionPlate(); }
    Layer *plate, *glyph, *text;
private:
    TaskCard* card_;
};
class TaskCard::Delete final : public Control {
public:
    Delete(Builder& b, std::string id, std::string text) : Control(b.env, id, std::move(text), b.layer(id)) {
        glyph = &makeIcon(b, sub(this->id(), "task.delete.glyph"), Icon::x, 12, rgb(0x555555));
        layer().add(*glyph);
    }
    Layer* glyph;
};

TaskCard::TaskCard(Builder& b, std::string id, std::string title, bool done, bool attention, bool redacted, double width)
    : Control(b.env, id, redacted ? tr(b.env, "Redacted", "已隐藏") : title, b.layer(id)), done_(done), attention_(attention) {
    const auto& p = palette();
    titleText_ = label;
    action_ = &b.make<Action>(this->id() + ":toggle", *this);
    delete_ = &b.make<Delete>(this->id() + ":delete", tr(env(), "Delete", "删除") + " " + titleText_);
    const Rect bounds{0, 0, width, height};
    const double bodyWidth = width - sideWidth;
    Layer& l = layer();
    l.setPartRoot(true);                                 // a list unit: its own part
    l.setBounds({width, height});
    body_ = &b.layer(sub(this->id(), "task.body"));
    bodyDone_ = &b.layer(sub(this->id(), "task.body.done"));
    const std::array<Color, 2> open{rgb(0xE0E1DC), rgb(0xD0D1CC)}, closed{rgb(0x5E5E5E), rgb(0x525252)};
    for (auto [plateLayer, colors] : {std::pair{body_, open}, std::pair{bodyDone_, closed}}) {
        plateLayer->setFrame({0, 0, bodyWidth, height});
        plateLayer->setContent(plate(plateLayer->id(), {0, 0, bodyWidth, height}, colors, {}, 0));
        l.add(*plateLayer);
    }
    bodyDone_->setAnimatable();
    side_ = &b.layer(sub(this->id(), "task.side"));
    side_->setFrame({bodyWidth, 0, sideWidth, height});
    side_->setMasksToBounds(true);
    sideLight_ = &b.layer(sub(this->id(), "task.side.open"));
    sideDone_ = &b.layer(sub(this->id(), "task.side.done"));
    const std::array<Color, 2> sideOpen{rgb(0xBEBFBA), rgb(0xAFB0AB)};
    const std::array<Color, 3> sideClosed{p.yellowHigh(), p.yellow(), p.yellowDeep()};
    const std::array<double, 2> openStops{0, 1};
    const std::array<double, 3> closedStops{0, .5, 1};
    const Rect sideBounds{0, 0, sideWidth, height};
    sideLight_->setFrame(sideBounds); sideLight_->setContent(plate(sideLight_->id(), sideBounds, sideOpen, openStops, 10));
    sideDone_->setFrame(sideBounds); sideDone_->setContent(plate(sideDone_->id(), sideBounds, sideClosed, closedStops, 10));
    sideDone_->setAnimatable();
    side_->add(*sideLight_); side_->add(*sideDone_);
    watermark_ = &makeText(b, sub(this->id(), "task.watermark"), "COMPLETED", TextStyle{33, FontWeight::black, black(.08), 1, -.02},
        {-6, height + 9 - 33, 220, 33});
    watermark_->setAnimatable();
    check_ = &b.layer(sub(this->id(), "task.ring"));
    check_->setAnimatable();
    check_->setFrame({18, 7, 46, 46});
    setBorder(*check_, std::nullopt, black(.08), 5, 23);
    side_->add(*watermark_); side_->add(*check_);
    l.add(*side_);
    tag_ = &b.layer(sub(this->id(), "task.tag"));
    tagDone_ = &b.layer(sub(this->id(), "task.tag.done"));
    const Size tag = microTagSize(env().text);
    for (auto [tagLayer, inverted] : {std::pair{tag_, false}, std::pair{tagDone_, true}}) {
        tagLayer->setFrame({14, 4, tag.width, tag.height});
        tagLayer->setContent(microTag(tagLayer->id(), {0, 0}, inverted, env().text, p));
        l.add(*tagLayer);
    }
    tagDone_->setAnimatable();
    const Rect titleLine{18, 14, bodyWidth - 34, 14.5 * 1.15};
    title_ = &makeText(b, sub(this->id(), "task.title"), title, taskTitleStyle(), titleLine);
    barTrack_ = &b.layer(sub(this->id(), "task.bar"));
    barTrack_->setFrame({18, 14 + 16.7 + 8 + 4, 150, 3});
    bar_ = &b.layer(sub(this->id(), "task.bar.fill"));
    bar_->setAnimatable();
    bar_->setFrame({0, 0, 150, 3}); bar_->setAnchorPoint({0, .5}); bar_->setPosition({0, 1.5});
    barTrack_->add(*bar_);
    count_ = &makeText(b, sub(this->id(), "task.count"), "", TextStyle{10.5, FontWeight::bold, fixed::steel2, 1, 0, false, true},
        {178, 14 + 16.7 + 8 - .5, 40, 10.5});
    if (redacted) l.add(redaction::bar(b, sub(this->id(), "task.title.redacted"), titleLine, taskTitleStyle(), redaction::width(title, env().characters)));
    else l.add(*title_);
    l.add(*barTrack_); l.add(*count_);
    auto& action = static_cast<Action&>(*action_);
    auto& remove = static_cast<Delete&>(*delete_);
    remove.layer().setAnimatable();                      // fades in with the card's hover
    remove.layer().setBounds({22, 22});
    remove.layer().setValue(Channel::opacity, 0);
    remove.glyph->setPosition({11, 11});
    side_->add(remove.layer()); side_->add(action.layer());
    diamond_ = &badge::diamond(b, sub(this->id(), "task.attention"), 8);
    diamond_->setPosition({width + 1, 1}); diamond_->setZPosition(2);
    l.add(*diamond_);
    remove.setTarget(localBounds(remove.layer()));
    remove.onActivate = [this] { if (onDelete) onDelete(); };
    action.onActivate = [this] { toggle(); };
    setTarget(bounds, std::nullopt, true);
    render(false);
}
const TextStyle& TaskCard::titleStyle() { return taskTitleStyle(); }
std::vector<Control*> TaskCard::controls() { return {this, action_, delete_}; }
void TaskCard::toggle() { setDone(!done_, true); if (onToggle) onToggle(done_); }
void TaskCard::setDone(bool done, bool animated) { if (done != done_) { done_ = done; render(animated); } }
void TaskCard::setAttention(bool on) { attention_ = on; diamond_->setHidden(!attention_ || done_); }
// renderAction (:121-141): content on state changes; the plate colour follows hover.
void TaskCard::renderAction() {
    auto& action = static_cast<Action&>(*action_);
    auto& remove = static_cast<Delete&>(*delete_);
    const std::string state = done_ ? tr(env(), "Done", "已完成") : tr(env(), "In progress", "进行中");
    action.label = titleText_ + ", " + state;
    editText(*action.text, [&](TextBlock& block) { block.setText(state); block.setColor(done_ ? palette().yellow() : rgb(0x222222)); });
    action.glyph->setHidden(!done_);
    const double icon = done_ ? 12 + 6 : 0;
    const double width = 13 + icon + std::ceil(preferredWidth(textOf(*action.text), env().text)) + 13;
    action.layer().setFrame({sideWidth - 12 - width, 17, width, 26});
    action.plate->setFrame(localBounds(action.layer()));
    setTintedBackground(*action.plate, clear, 13);
    action.glyph->setFrame({13, 7, 12, 12});
    setLine(*action.text, {13 + icon, 7.25, width - 26 - icon + 1, 11.5});
    remove.layer().setFrame({action.layer().frame().x - 8 - 22, 19, 22, 22});
    setIcon(*remove.glyph, Icon::x, 12, done_ ? rgb(0x333333) : rgb(0x555555));
    action.setTarget(localBounds(action.layer()), roundedRect(localBounds(action.layer()), 13));
    renderActionPlate();
}
void TaskCard::renderActionPlate() {
    auto& action = static_cast<Action&>(*action_);
    action.plate->setTint(done_ ? fixed::counterBackground : (action.hovered() ? white(1) : rgb(0xDCDCDA)));
}
// render(animated:) (:143-162).
void TaskCard::render(bool animated) {
    const auto c = clock();
    const double flip = animated ? duration::taskFlip : 0;
    for (Layer* layer : {bodyDone_, sideDone_, tagDone_}) motion::animate(*layer, Channel::opacity, done_ ? 1 : 0, flip, c, easing::ease);
    motion::animate(*watermark_, Channel::translateX, done_ ? 0 : 30, animated ? .45 : 0, c);
    motion::animate(*watermark_, Channel::opacity, done_ ? 1 : 0, animated ? .3 : 0, c, easing::ease);
    motion::animate(*check_, Channel::scaleX, done_ ? 1 : .6, animated ? .35 : 0, c);
    motion::animate(*check_, Channel::scaleY, done_ ? 1 : .6, animated ? .35 : 0, c);
    motion::animate(*check_, Channel::opacity, done_ ? 1 : 0, animated ? .35 : 0, c);
    motion::animate(*bar_, Channel::scaleX, done_ ? 1 : .0001, animated ? .35 : 0, c);
    editText(*title_, [&](TextBlock& block) { block.setColor(done_ ? rgb(0xF2F2F2) : fixed::nearBlack); });
    setBackground(*barTrack_, done_ ? rgb(0x3F3F3F) : rgb(0xA9AAA5));
    setBackground(*bar_, done_ ? palette().yellow() : fixed::counterBackground);
    editText(*count_, [&](TextBlock& block) { block.setText(done_ ? "1/1" : "0/1"); block.setColor(done_ ? rgb(0xCFCFCF) : fixed::steel2); });
    diamond_->setHidden(!attention_ || done_);
    renderAction();
}
void TaskCard::hoverChanged() {
    motion::animate(delete_->layer(), Channel::opacity, hovered() ? 1 : 0, .12, clock(), easing::ease);
}
// The x is transparent until the card is hovered; screen readers reach it anyway (:169-173).
void TaskCard::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    if (delete_->layer().value(Channel::opacity) >= .5) return;
    out.push_back({delete_->id(), delete_->label, "", AccessibilityRole::button, false, "", 0, 0, 1, 1, delete_->rect(root), true, false});
}

// ---------------------------------------------------------------------------
// HUDListCard (:180-315)
// ---------------------------------------------------------------------------
ListCard::ListCard(Builder& b, std::string id, Kind kind, std::string title, std::string meta, std::string age, bool pinned, double width,
    std::optional<ImageSource> thumbnail, std::string copyTitle, bool redacted, std::vector<Control*> actions)
    : Control(b.env, id, title, b.layer(id)), actions_(std::move(actions)) {
    const auto& p = palette();
    const Rect bounds{0, 0, width, height};
    const auto shapePath = chamfer(bounds, 10);
    double stripWidth = 0;
    for (const Control* action : actions_) stripWidth += action->layer().bounds().width;
    stripWidth += double(std::max<std::size_t>(1, actions_.size()) - 1) * 6;
    const double textWidth = width - 64 - 80 - (actions_.empty() ? 0 : stripWidth + 8);
    const TextStyle titleStyle{13.5, FontWeight::medium, fixed::nearBlack, 1.25}, metaStyle{10.5, FontWeight::regular, fixed::steel2, 1.2};
    const TextStyle metaColumn{10, FontWeight::bold, rgb(0x55564F)};
    Layer& l = layer();
    l.setAnimatable();                                   // translateX 3 on hover/focus
    l.setPartRoot(true);                                 // a list unit: its own part
    l.setBounds({width, height});
    plate_ = &b.layer(sub(this->id(), "card.plate"));
    plate_->setFrame(bounds);
    const std::array<Color, 2> plateColors{rgb(0xE2E3DE), rgb(0xCFD0CB)};
    plate_->setContent(plate(plate_->id(), bounds, plateColors, {}, 10));
    brighten_ = &b.layer(sub(this->id(), "card.brighten"));
    brighten_->setFrame(bounds); brighten_->setAnimatable();
    brighten_->setContent(shape(brighten_->id(), bounds, shapePath, white(.07))); brighten_->setValue(Channel::opacity, 0);
    flash_ = &b.layer(sub(this->id(), "card.flash"));
    flash_->setFrame(bounds); flash_->setAnimatable(SurfaceRole::lazy);
    flash_->setContent(shape(flash_->id(), bounds, shapePath, withAlpha(p.yellow(), .55))); flash_->setValue(Channel::opacity, 0);
    sheen_ = std::make_unique<Sheen>(b, sub(this->id(), "sheen"));
    sheen_->setShape(*plate_, {width, height});
    leftBar_ = &b.layer(sub(this->id(), "card.bar"));
    leftBar_->setFrame({0, 0, 3, height});
    setTintedBackground(*leftBar_, clear);
    Layer& block = b.layer(sub(this->id(), "card.type"));
    block.setFrame({8, 7, 44, 44}); setBackground(block, rgb(0x1B1B1B));
    if (thumbnail) {
        Layer& picture = b.layer(sub(this->id(), "card.thumbnail"));
        picture.setFrame({0, 0, 44, 35}); picture.setMasksToBounds(true);
        picture.setContent(image(picture.id(), {0, 0, 44, 35}, *thumbnail, "resizeAspectFill"));
        block.add(picture);
    } else {
        const Icon icon = kind == Kind::text ? Icon::text : kind == Kind::link ? Icon::link : kind == Kind::image ? Icon::image : Icon::file;
        Layer& glyph = makeIcon(b, sub(this->id(), "card.type.glyph"), icon, 18, rgb(0xF2F2F2));
        glyph.setPosition({22, 20});
        block.add(glyph);
    }
    Layer& strip = b.layer(sub(this->id(), "card.type.strip"));
    strip.setFrame({0, 35, 44, 10}); setBackground(strip, p.yellow());
    const char* word = kind == Kind::text ? "TEXT" : kind == Kind::link ? "LINK" : kind == Kind::image ? "IMAGE" : "FILE";
    strip.add(makeText(b, sub(this->id(), "card.type.word"), word, TextStyle{6.5, FontWeight::black, fixed::nearBlack, 10 / 6.5, .1},
        {0, 0, 44, 10}, TextAlignment::center));
    block.add(strip);
    for (Layer* child : {plate_, brighten_, flash_, &sheen_->layer(), leftBar_, &block}) l.add(*child);
    const Rect titleLine{64, 13, textWidth, 16.9}, metaLine{64, 13 + 16.9 + 3, textWidth, 12.6};
    if (redacted) {
        l.add(redaction::bar(b, sub(this->id(), "card.title.redacted"), titleLine, titleStyle, redaction::width(title, env().characters)));
        l.add(redaction::bar(b, sub(this->id(), "card.meta.redacted"), metaLine, metaStyle, 96));
    } else {
        l.add(makeText(b, sub(this->id(), "card.title"), title, titleStyle, titleLine));
        l.add(makeText(b, sub(this->id(), "card.meta"), std::move(meta), metaStyle, metaLine));
    }
    // `.mt`: two 16 px lines (age, then the COPY reveal), right-aligned and centred.
    age_ = &makeText(b, sub(this->id(), "card.age"), std::move(age), metaColumn, {width - 10 - 70, 13, 70, 16}, TextAlignment::right);
    TextBlock copyBlock(copyTitle, metaColumn);
    const double copyText = std::ceil(preferredWidth(copyBlock, env().text)), copyWidth = 14 + 4 + copyText;
    copy_ = &b.layer(sub(this->id(), "card.copy"));
    copy_->setAnimatable();
    copy_->setFrame({width - 10 - copyWidth, 29, copyWidth, 16});
    copy_->setValue(Channel::opacity, 0); copy_->setValue(Channel::translateX, 6);
    Layer& copyIcon = makeIcon(b, sub(this->id(), "card.copy.icon"), Icon::copy, 14, rgb(0x55564F));
    copyIcon.setFrame({0, 2, 14, 14});
    copy_->add(copyIcon);
    copy_->add(makeText(b, sub(this->id(), "card.copy.text"), std::move(copyTitle), metaColumn, {18, 0, copyText + 1, 16}));
    l.add(*age_); l.add(*copy_);
    if (!actions_.empty()) {
        strip_ = &b.layer(sub(this->id(), "card.actions"));
        strip_->setAnimatable();
        strip_->setFrame({width - 80 - stripWidth, 0, stripWidth, height});
        strip_->setValue(Channel::opacity, 0);
        double x = 0;
        for (Control* action : actions_) {
            const auto size = action->layer().bounds();
            action->layer().setFrame({x, std::round((height - size.height) / 2), size.width, size.height});
            strip_->add(action->layer());
            x += size.width + 6;
        }
        l.add(*strip_);
    }
    if (pinned) {
        Layer& corner = b.layer(sub(this->id(), "card.pinned"));
        corner.setFrame({width - 14, 0, 14, 14});
        setPath(corner, PathBuilder().move({0, 0}).line({14, 0}).line({14, 14}).close(), p.yellow());
        corner.setZPosition(1);
        l.add(corner);
    }
    // The card stays hovered while one of its actions is.
    setTarget(bounds, shapePath, !actions_.empty());
}
std::vector<Control*> ListCard::controls() {
    std::vector<Control*> out{this};
    out.insert(out.end(), actions_.begin(), actions_.end());
    return out;
}
// A press on the action strip (between the actions, or on a disabled one) never copies.
bool ListCard::contains(Point p) const { return Control::contains(p) && (actions_.empty() || !inRect(strip_->frame(), p)); }
void ListCard::accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const {
    Control::accessibilityItems(root, out);
    if (strip_ && strip_->value(Channel::opacity) < .5) for (const Control* action : actions_) action->accessibilityItems(root, out);
}
void ListCard::setAge(std::string text) {
    if (textOf(*age_).text() == text) return;
    editText(*age_, [&](TextBlock& block) { block.setText(std::move(text)); });
}
void ListCard::setFocused(bool focused) { if (focused != focused_) { focused_ = focused; renderHighlight(); } }
void ListCard::hoverChanged() { renderHighlight(); sheen_->play(hovered() && enabled(), clock()); }
// renderHighlight (:297-305).
void ListCard::renderHighlight() {
    const bool on = (hovered() || focused_) && enabled();
    const auto c = clock();
    motion::animate(layer(), Channel::translateX, on ? 3 : 0, duration::hover, c);
    motion::animate(*brighten_, Channel::opacity, on ? 1 : 0, duration::hover, c, easing::ease);
    motion::animateTint(*leftBar_, on ? fixed::nearBlack : clear, .1, c, easing::ease);
    motion::animate(*copy_, Channel::opacity, on ? 1 : 0, .14, c);
    motion::animate(*copy_, Channel::translateX, on ? 0 : 6, .14, c);
    if (strip_) motion::animate(*strip_, Channel::opacity, on ? 1 : 0, .14, c);
}
// okflash (:308-314): the inset yellow fill fades out over .6 s.
void ListCard::flashCopied() {
    flash_->setValue(Channel::opacity, 0);
    const auto c = clock();
    if (c.reduceMotion) return;
    const std::array<double, 2> values{1, 0}, times{0, 1};
    motion::keyframes(*flash_, Channel::opacity, values, times, .6, c, easing::easeOut);
}

// ---------------------------------------------------------------------------
// HUDArchiveTile (:320-503)
// ---------------------------------------------------------------------------
namespace {
Color extensionColor(ArchiveTile::Extension e) {
    switch (e) {
    case ArchiveTile::Extension::pdf: return fixed::red;
    case ArchiveTile::Extension::img: return fixed::cyan;
    case ArchiveTile::Extension::zip: return rgb(0x8A6D00);
    case ArchiveTile::Extension::vid: return rgb(0x7A3CC9);
    case ArchiveTile::Extension::txt: break;
    }
    return rgb(0x333333);
}
const char* extensionName(ArchiveTile::Extension e) {
    switch (e) {
    case ArchiveTile::Extension::pdf: return "pdf";
    case ArchiveTile::Extension::img: return "img";
    case ArchiveTile::Extension::zip: return "zip";
    case ArchiveTile::Extension::vid: return "vid";
    case ArchiveTile::Extension::txt: break;
    }
    return "txt";
}
}
// bracketPath (:428-437): two 9 px wide vertical brackets, 2 px, open toward the centre.
PathBuilder ArchiveTile::bracketPath(Rect bounds, double inset) {
    const Rect r{bounds.x + inset, bounds.y + inset, bounds.width - 2 * inset, bounds.height - 2 * inset}; // CGRect.insetBy
    PathBuilder p;
    for (const bool left : {true, false}) {
        const double x = left ? r.x : r.x + r.width - 9;
        p.rect({x, r.y, 9, 2}).rect({x, r.y + r.height - 2, 9, 2}).rect({left ? r.x : r.x + r.width - 2, r.y, 2, r.height});
    }
    return p;
}
ArchiveTile::ArchiveTile(Builder& b, std::string id, int number, std::string title, Extension extension, std::optional<std::string> extensionLabel,
    std::optional<std::string> size, std::optional<std::string> date, double width, double wellHeight, bool redacted, Style style)
    : Control(b.env, id, title, b.layer(id)), style_(style), builder_(&b) {
    const bool classified = style == Style::classified;
    const Rect bounds{0, 0, width, wellHeight + 24};
    Layer& l = layer();
    l.setAnimatable();                                   // playRemoval
    l.setPartRoot(true);                                 // a grid unit: its own part
    l.setBounds({width, wellHeight + 24});
    well_ = &b.layer(sub(this->id(), "tile.well"));
    well_->setAnimatable();                              // -2 px on hover
    well_->setFrame({0, 0, width, wellHeight});
    const Rect wellBounds{0, 0, width, wellHeight};
    if (classified) {
        const std::array<Color, 2> colors{rgb(0x2A2A2A), rgb(0x151515)};
        well_->setContent(plate(well_->id(), wellBounds, colors, {}, 0));
    } else {
        const std::array<Color, 3> colors{rgb(0x8A8885), rgb(0xBDBDBB), rgb(0xD9D9D8)};
        const std::array<double, 3> stops{0, .55, 1};
        well_->setContent(plate(well_->id(), wellBounds, colors, stops, 0));
    }
    well_->setMasksToBounds(true);
    brighten_ = &b.layer(sub(this->id(), "tile.well.brighten"));
    brighten_->setFrame(wellBounds); brighten_->setAnimatable();
    setBackground(*brighten_, white(.12)); brighten_->setValue(Channel::opacity, 0);
    Layer& codeBar = b.layer(sub(this->id(), "tile.code.bar"));
    codeBar.setFrame({0, 0, 34, 4}); setBackground(codeBar, fixed::counterBackground);
    Layer& code = makeText(b, sub(this->id(), "tile.code"), "#" + format::pad2(number), TextStyle{6, FontWeight::bold, rgb(0x222222)}, {38, -1, 30, 6});
    well_->add(*brighten_); well_->add(codeBar); well_->add(code);
    doc_ = &docGlyph(b, sub(this->id(), "tile.doc"), extension, extensionLabel);
    doc_->setPosition({width / 2, wellHeight / 2});
    if (classified) well_->add(classifiedStamp(b, sub(this->id(), "tile.stamp"), {width / 2, wellHeight / 2}));
    else well_->add(*doc_);
    if (size) {
        const TextStyle tagStyle{8.5, FontWeight::bold, fixed::onDarkTag};
        TextBlock tag(*size, tagStyle);
        const double w = std::ceil(preferredWidth(tag, env().text)) + 8;
        Layer& sizePlate = b.layer(sub(this->id(), "tile.size"));
        sizePlate.setFrame({width - 4 - w, wellHeight - 4 - 12, w, 12});
        setBackground(sizePlate, rgb(0x141414, .75));
        sizePlate.add(makeText(b, sub(this->id(), "tile.size.text"), std::move(*size), tagStyle, {4, 1.75, w - 8 + 1, 8.5}));
        well_->add(sizePlate);
    }
    if (date) well_->add(makeText(b, sub(this->id(), "tile.date"), std::move(*date), TextStyle{8.5, FontWeight::bold, rgb(classified ? 0x777777 : 0x222222)},
        {6, wellHeight - 4 - 9, width / 2, 8.5}));
    strip_ = &b.layer(sub(this->id(), "tile.label"));
    strip_->setFrame({0, wellHeight, width, 24});
    const TextStyle labelStyle{11.5, FontWeight::medium, fixed::nearBlack};
    const Rect labelLine{6, 6.25, width - 12, 11.5};
    label_ = &makeText(b, sub(this->id(), "tile.label.text"), std::move(title),
        classified ? TextStyle{10, FontWeight::bold, fixed::red, 1, .12} : labelStyle, labelLine, TextAlignment::center);
    if (redacted) strip_->add(redaction::bar(b, sub(this->id(), "tile.label.redacted"), labelLine, labelStyle, 84, TextAlignment::center));
    else strip_->add(*label_);
    // Selection brackets: the .1 s fade on the container, the inset -5 -> -4 morph as a cross-fade (file comment).
    brackets_ = &b.layer(sub(this->id(), "tile.brackets"));
    bracketsRest_ = &b.layer(sub(this->id(), "tile.brackets.rest"));
    bracketsSelected_ = &b.layer(sub(this->id(), "tile.brackets.selected"));
    for (Layer* layer : {brackets_, bracketsRest_, bracketsSelected_}) { layer->setFrame(bounds); layer->setAnimatable(); }
    setPath(*bracketsRest_, bracketPath(bounds, -5), palette().yellow());
    setPath(*bracketsSelected_, bracketPath(bounds, -4), palette().yellow());
    brackets_->add(*bracketsRest_); brackets_->add(*bracketsSelected_);
    brackets_->setValue(Channel::opacity, 0);
    l.add(*well_); l.add(*strip_); l.add(*brackets_);
    renderSelection(false);
    setTarget(bounds);
}
// docGlyph (:404-425): 40 x 50, ruled lines, the extension in the kind's colour, -6 deg.
Layer& ArchiveTile::docGlyph(Builder& b, std::string_view id, Extension extension, const std::optional<std::string>& label) {
    Layer& doc = b.layer(id);
    doc.setAnimatable();                                 // straightens and lifts on hover
    doc.setBounds({40, 50});
    const auto fold = PathBuilder().move({0, 0}).line({28, 0}).line({40, 11}).line({40, 50}).line({0, 50}).close();
    Layer& page = b.layer(sub(id, "page"));
    page.setFrame({0, 0, 40, 50});
    Json pageNode = shape(page.id(), {0, 0, 40, 50}, fold, rgb(0xF4F4F2));
    addShadow(pageNode, Shadow{black(1), .25, 6, {0, 6}, fold});   // the doc layer's shadow, drawn under its content
    page.setContent(std::move(pageNode));
    Layer& lines = b.layer(sub(id, "lines"));
    lines.setFrame({6, 10, 24, 14});
    PathBuilder rules;
    for (double y = 0; y < 14; y += 4) rules.rect({0, y, 24, 1});
    setPath(lines, rules, rgb(0x9B9B9B));
    const std::string name = label.value_or(extensionName(extension));
    const std::string upper = b.env.text.uppercase ? b.env.text.uppercase(name) : asciiUppercase(name);
    Layer& ext = makeText(b, sub(id, "ext"), upper, TextStyle{8, FontWeight::black, extensionColor(extension), 1, .06}, {0, 36, 40, 8}, TextAlignment::center);
    doc.add(page); doc.add(lines); doc.add(ext);
    doc.setValue(Channel::rotation, -6 * pi / 180);
    return doc;
}
// classifiedStamp (:440-455): padding 3 x 7, 2 px ring, 900 10 px, -10 deg.
Layer& ArchiveTile::classifiedStamp(Builder& b, std::string_view id, Point center) {
    Layer& stamp = b.layer(id);
    const TextStyle style{10, FontWeight::black, fixed::red, 1, .18};
    const std::string word = tr(b.env, "CLASSIFIED", "机密");
    TextBlock block(word, style, TextAlignment::center);
    const double width = std::ceil(preferredWidth(block, b.env.text)) + 14 + 4;
    stamp.setBounds({width, 10 + 6 + 4});
    stamp.setPosition(center);
    setBorder(stamp, std::nullopt, fixed::red, 2);
    stamp.add(makeText(b, sub(id, "text"), word, style, {0, 5, width, 10}, TextAlignment::center));
    stamp.setBaseTransform(Affine::rotation(-10 * pi / 180));
    return stamp;
}
// setPoster (:458-469): a thumbnail filling the well in place of the doc glyph (nullopt restores it).
void ArchiveTile::setPoster(std::optional<ImageSource> source) {
    if (style_ != Style::standard || (!source && !poster_)) return;
    if (!poster_ && source) {
        poster_ = &builder_->layer(sub(id(), "tile.poster"));
        poster_->setFrame(localBounds(*well_)); poster_->setMasksToBounds(true);
        well_->insert(*poster_, 0);
    }
    if (source) poster_->setContent(image(poster_->id(), localBounds(*poster_), *source, "resizeAspectFill")); else poster_->clearContent();
    poster_->setHidden(!source);
    doc_->setHidden(source.has_value());
}
void ArchiveTile::setSelected(bool selected, bool animated) { if (selected != selected_) { selected_ = selected; renderSelection(animated); } }
// renderSelection (:477-484).
void ArchiveTile::renderSelection(bool animated) {
    const auto c = clock();
    motion::animate(*brackets_, Channel::opacity, selected_ ? 1 : 0, animated ? .1 : 0, c, easing::ease);
    motion::animate(*bracketsRest_, Channel::opacity, selected_ ? 0 : 1, animated ? .16 : 0, c);
    motion::animate(*bracketsSelected_, Channel::opacity, selected_ ? 1 : 0, animated ? .16 : 0, c);
    const bool classified = style_ == Style::classified;
    setBackground(*strip_, selected_ ? fixed::counterBackground : rgb(classified ? 0x262626 : 0xC4C4C6));
    editText(*label_, [&](TextBlock& block) { block.setColor(selected_ ? palette().yellow() : (classified ? fixed::red : fixed::nearBlack)); });
}
// hoverChanged (:486-492): the doc's transform is R(-2 or -6 deg) then T(0, -3 or 0).
void ArchiveTile::hoverChanged() {
    const bool on = hovered() && enabled();
    const auto c = clock();
    motion::animate(*brighten_, Channel::opacity, on ? 1 : 0, duration::hover, c, easing::ease);
    motion::animate(*well_, Channel::translateY, on ? -2 : 0, .25, c);
    motion::animate(*doc_, Channel::rotation, (on ? -2 : -6) * pi / 180, .3, c);
    motion::animate(*doc_, Channel::translateY, on ? -3 : 0, .3, c);
}
// gone (:495-502): fade and scale to .85 in .28 s, then the caller removes the tile.
void ArchiveTile::playRemoval(std::function<void()> completion) {
    const auto c = clock();
    if (c.reduceMotion) { if (completion) completion(); return; }
    motion::animate(layer(), Channel::opacity, 0, .28, c);
    motion::animate(layer(), Channel::scaleX, .85, .28, c);
    motion::animate(layer(), Channel::scaleY, .85, .28, c);
    motion::after(layer(), c.now + .28, c, std::move(completion));
}
} // namespace endfield::modules::design
