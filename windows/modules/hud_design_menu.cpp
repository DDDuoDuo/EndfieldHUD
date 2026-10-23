// Menus and pop-overs: ports of Sources/HUDDesignMenu.swift (HUDPanelChrome
// :6-23, HUDMenu :106-446 with Row :150-234 and Track :238-248, HUDPopover
// :450-499). The dropdown field (:27-100) lives in hud_design_controls.cpp.
//
// A text row's highlight (the plate with its black bar, the dark text) shows
// on hover and keyboard moves: the plate is an opacity number and the text a
// tint, so moving through a menu never re-rasterizes (the Mac toggles
// plate.isHidden and the text colour). The panel's 1 px ring is a child above
// the list (a CALayer border draws above its sublayers, over a highlighted
// row's left edge).
#include "modules/hud_design_menu.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::design {
namespace {
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
}

// HUDPanelChrome.apply (:7-22).
void applyPanelChrome(Builder& b, Layer& panel, Size size) {
    const auto& p = b.env.palette;
    panel.setBounds(size);
    const Rect bounds{0, 0, size.width, size.height};
    Json fill = solid(panel.id(), bounds, p.token(ThemeToken::panel));
    addShadow(fill, Shadow{black(1), p.isLight() ? .25 : .6, 20, {0, 18}, PathBuilder().rect(bounds)});
    panel.setContent(std::move(fill));
    Layer& ring = b.layer(sub(panel.id(), "panel.ring"));
    ring.setFrame(bounds);
    setBorder(ring, std::nullopt, p.token(ThemeToken::ringMenu), 1);
    panel.add(ring);
    Layer& corner = b.layer(sub(panel.id(), "panel.corner"));
    corner.setFrame({-1, -1, 10, 10}); corner.setZPosition(1);
    setPath(corner, cornerMark({0, 0, 10, 10}, 10, 2, topLeft), p.accentInk());
    panel.add(corner);
}

// ---------------------------------------------------------------------------
// HUDMenu.Row (:150-234)
// ---------------------------------------------------------------------------
class Menu::Row final : public Control {
public:
    Row(Builder& b, const Item& item, const std::string& menuID, double width, Menu& menu)
        : Control(b.env, menuID + ":" + item.id, item.title, b.layer(menuID + ":" + item.id)), item_(item), menu_(&menu) {
        if (item.icon) { buildTile(b, *item.icon); return; }
        const auto& p = palette();
        const Rect bounds{0, 0, width, rowHeight};
        Layer& l = layer();
        l.setBounds({width, rowHeight});
        plate_ = &b.layer(sub(this->id(), "menu.item.plate"));
        plate_->setFrame(bounds); plate_->setAnimatable();
        setBackground(*plate_, fixed::plate);
        plate_->setValue(Channel::opacity, 0);           // plate.isHidden = true
        Layer& bar = b.layer(sub(this->id(), "menu.item.bar"));
        bar.setFrame({0, 5, 3, 20}); setBackground(bar, fixed::nearBlack);
        plate_->add(bar);
        TextBlock block(item.title, type::menuItem(p).colored(white(1)));
        block.language = item.language;
        block.setLineFrame({14, 8.75, width - 14 - 12 - (item.selected ? 24 : 0), 12.5});
        text_ = &b.layer(sub(this->id(), "menu.item.text"));
        text_->setText(std::move(block));
        text_->setAnimatable(); text_->setTint(restingInk());
        l.add(*plate_); l.add(*text_);
        if (item.selected) {
            Layer& mark = b.layer(sub(this->id(), "menu.item.mark"));
            mark.setFrame({width - 12 - 12, 9, 12, 12});
            Json node = solid(mark.id(), {0, 0, 12, 12}, p.accentInk());
            addChildren(node, {solid(mark.id() + ".gap", {1, 1, 10, 10}, p.token(ThemeToken::panel)), solid(mark.id() + ".core", {3, 3, 6, 6}, p.accentInk())});
            mark.setContent(std::move(node));
            l.add(mark);
        }
        l.setValue(Channel::opacity, item.enabled ? 1 : .35);
        setEnabled(item.enabled);
        setTarget(bounds);
    }
    // setHighlighted (:215-225).
    void setHighlighted(bool on) {
        if (item_.icon) {
            if (item_.selected) return;
            motion::animate(*ring_, Channel::opacity, on ? .7 : 0, duration::hover, clock(), easing::ease);
            return;
        }
        plate_->setValue(Channel::opacity, on ? 1 : 0);
        text_->setTint(on ? fixed::nearBlack : restingInk());
    }
    void hoverChanged() override {
        if (hovered()) menu_->keyboard_.reset();
        menu_->renderHighlight();
        if (item_.icon) applyHoverMotion(*this, 1, 1, .965, brighten_);
    }
    void pressChanged() override { if (item_.icon) applyHoverMotion(*this, 1, 1, .965, brighten_); }
    void activate() override { if (enabled()) menu_->pick(item_); }
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::menuItem; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto entry = accessibilityItem(root, AccessibilityRole::menuItem); entry.on = item_.selected;
        out.push_back(std::move(entry));
    }
    const Item& item() const noexcept { return item_; }
private:
    Item item_; Menu* menu_;
    Layer *plate_{}, *text_{}, *ring_{}, *brighten_{};
    Color restingInk() const { return item_.danger ? palette().token(ThemeToken::danger) : palette().token(ThemeToken::menuText); }
    // The icon tile (:193-213): fill, artwork, a 1 px edge, the hover brighten
    // and the ring 3 px outside it (accent on the selection, light while highlighted).
    void buildTile(Builder& b, const IconTile& icon) {
        const auto& p = palette();
        const Rect bounds{0, 0, icon.size.width, icon.size.height};
        Layer& l = layer();
        l.setAnimatable();                               // lift 1 / press .965
        l.setBounds(icon.size);
        setBackground(l, icon.fill.value_or(p.token(ThemeToken::control)));
        Layer& picture = b.layer(sub(id(), "menu.tile.icon"));
        picture.setFrame(icon.frame.value_or(Rect{6, 6, icon.size.width - 12, icon.size.height - 12}));
        picture.setContent(image(picture.id(), localBounds(picture), icon.image, "resizeAspect", std::nullopt, true));
        Layer& edge = b.layer(sub(id(), "menu.tile.edge"));
        edge.setFrame(bounds);
        setBorder(edge, std::nullopt, icon.fill ? black(.25) : p.token(ThemeToken::ringField), 1);
        brighten_ = &b.layer(sub(id(), "menu.tile.brighten"));
        brighten_->setFrame(bounds); brighten_->setAnimatable();
        setBackground(*brighten_, p.foreground(.08)); brighten_->setValue(Channel::opacity, 0);
        ring_ = &b.layer(sub(id(), "menu.tile.ring"));
        ring_->setFrame({-3, -3, icon.size.width + 6, icon.size.height + 6}); ring_->setAnimatable();
        setBorder(*ring_, std::nullopt, item_.selected ? p.accentInk() : p.token(ThemeToken::menuText), 1.5);
        ring_->setValue(Channel::opacity, item_.selected ? 1 : 0);
        for (Layer* child : {&picture, brighten_, &edge, ring_}) l.add(*child);
        l.setValue(Channel::opacity, item_.enabled ? 1 : .35);
        setEnabled(item_.enabled);
        setTarget(bounds);
    }
};

// HUDMenu.Track (:238-248): press or drag moves the thumb there; its bar hides while idle, the strip stays live.
class Menu::Track final : public Control {
public:
    Track(Builder& b, std::string id, Layer& layer, Menu& menu) : Control(b.env, std::move(id), "", layer), menu_(&menu) {}
    bool pointerDown(Point p) override { menu_->bar_->setDragging(true); menu_->scrollTrack(p.y); return enabled(); }
    void pointerDragged(Point p) override { menu_->scrollTrack(p.y); }
    void pointerUp(Point) override { menu_->bar_->setDragging(false); }
    void hoverChanged() override { menu_->bar_->setHovered(hovered()); }
    void clearInteractionState() override { Control::clearInteractionState(); menu_->bar_->reset(); }
    std::optional<AccessibilityRole> accessibilityRole() const override { return std::nullopt; }
private:
    Menu* menu_;
};

// ---------------------------------------------------------------------------
// HUDMenu (:279-346)
// ---------------------------------------------------------------------------
Menu::Menu(Environment& env, std::string id, std::vector<Item> items, double minimumWidth, Deadlines::Token)
    : env_(&env), id_(std::move(id)), items_(std::move(items)), arena_(id_, &env.deadlines) {
    Builder b{env, arena_, controls_};
    const auto& p = env.palette;
    layer_ = &b.layer("menu");
    list_ = &b.layer("menu.list");
    content_ = &b.layer("menu.content");
    std::size_t tiles = 0;
    Size tile{};
    while (tiles < items_.size() && items_[tiles].icon) {
        tile = {std::max(tile.width, items_[tiles].icon->size.width), std::max(tile.height, items_[tiles].icon->size.height)};
        ++tiles;
    }
    // Balanced rows: as few as fit the width limit, the columns evened out.
    const std::size_t fit = std::max<std::size_t>(1, std::size_t((gridWidthLimit - 16 + tileGap) / (tile.width + tileGap)));
    const std::size_t lines = (tiles + fit - 1) / fit;
    const std::size_t columns = lines == 0 ? 0 : (tiles + lines - 1) / lines;
    tiles_ = tiles; columns_ = columns;
    // 8 px around the grid (the list adds 4 above and below).
    const Size grid{columns == 0 ? 0 : double(columns) * (tile.width + tileGap) - tileGap + 16, lines == 0 ? 0 : double(lines) * (tile.height + tileGap) - tileGap + 8};
    double textHeight = 0, natural = 0;
    for (std::size_t i = tiles; i < items_.size(); ++i) {
        textHeight += rowHeight + (items_[i].separatorBefore ? 9 : 0);
        natural = std::max(natural, 14 + env.text.width(items_[i].title, type::menuItem(p), items_[i].language) + 12 + 24);
    }
    if (tiles != 0 && tiles != items_.size()) textHeight += 9;
    long_ = tiles == 0 ? items_.size() > longMenuThreshold : grid.height + textHeight > maximumListHeight;
    const double extra = long_ ? 12 : 0;
    const double width = std::ceil(std::max({minimumWidth, 150.0, natural + extra, grid.width + extra}));
    const double rowWidth = width - extra;
    double y = grid.height;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const Item& item = items_[index];
        if (index < tiles) {
            const Size size = item.icon->size;
            const double left = (rowWidth - grid.width) / 2 + 8;
            const double column = double(index % columns), line = double(index / columns);
            auto& row = b.make<Row>(item, id_, size.width, *this);
            row.layer().setFrame({left + column * (tile.width + tileGap) + (tile.width - size.width) / 2,
                4 + line * (tile.height + tileGap) + (tile.height - size.height) / 2, size.width, size.height});
            content_->add(row.layer()); rows_.push_back(&row);
            continue;
        }
        if (item.separatorBefore || (index == tiles && index > 0)) {
            Layer& separator = b.layer("menu.separator." + std::to_string(index));
            separator.setFrame({10, y + 4, rowWidth - 20, 1}); setBackground(separator, p.token(ThemeToken::separator));
            content_->add(separator); y += 9;
        }
        auto& row = b.make<Row>(item, id_, rowWidth, *this);
        row.layer().setFrame({0, y, rowWidth, rowHeight});
        content_->add(row.layer()); rows_.push_back(&row);
        y += rowHeight;
    }
    contentHeight_ = y;
    const double listHeight = long_ ? std::min(maximumListHeight, y) : y;
    applyPanelChrome(b, *layer_, {width, listHeight + 8});
    layer_->setAnimatable();                             // the entrance wipe and fade
    layer_->setPartRoot(true);
    layer_->setZPosition(order::menu);
    list_->setFrame({0, 4, rowWidth, listHeight}); list_->setMasksToBounds(true);
    content_->setAnimatable();                           // the scroll offset
    content_->setFrame({0, 0, rowWidth, y});
    list_->add(*content_); layer_->insert(*list_, 0);
    if (long_) {
        track_ = &b.layer("menu.track");
        rail_ = &b.layer("menu.rail");
        thumb_ = &b.layer("menu.thumb");
        track_->setFrame({width - 4 - 5, 8, 5, layer_->bounds().height - 16});
        rail_->setFrame(localBounds(*track_)); setBackground(*rail_, p.tone(0x2C2C2C));
        thumb_->setAnimatable();
        rail_->add(*thumb_); track_->add(*rail_); layer_->add(*track_);
        bar_ = std::make_unique<ScrollBarDriver>(env, *rail_, arena_.token());
        auto& control = b.make<Track>(id_ + ".track", *track_, *this);
        control.setTarget(localBounds(*track_));
        trackControl_ = &control;
    }
    const auto selected = std::find_if(items_.begin(), items_.end(), [](const Item& item) { return item.selected; });
    if (selected != items_.end()) {
        const auto index = std::size_t(selected - items_.begin());
        keyboard_ = index;                               // set in init: Swift runs no didSet, so the row is not highlighted yet
        setScroll(rowTop(index) - 60);
    } else setScroll(0);
}
Menu::~Menu() = default;
std::vector<Control*> Menu::controls() {
    std::vector<Control*> out(rows_.begin(), rows_.end());
    if (trackControl_) out.push_back(trackControl_);
    return out;
}
// place (:353-359).
void Menu::place(Rect anchor, Size box) {
    const auto size = layer_->bounds();
    double x = anchor.x, y = anchor.y + anchor.height + 4;
    if (x + size.width > box.width) x = std::max(0.0, box.width - size.width);
    if (y + size.height > box.height + 20) y = anchor.y - 4 - size.height;
    layer_->setFrame({x, y, size.width, size.height});
}
void Menu::playEntrance(const motion::Clock& clock) { motion::wipe(*layer_, WipeOrigin::top, duration::menuWipe, clock, easing::standard, 0, .4); }
void Menu::pick(const Item& item) { if (item.enabled && onPick) onPick(item); }
// handleKey (:369-382): arrows move, Return / keypad Enter picks, Esc closes, nothing else passes.
bool Menu::handleKey(const KeyInput& input) {
    switch (input.virtualKey) {
    case key::left: case key::right: case key::up: case key::down:
        if (const auto next = nextIndex(input.virtualKey)) { keyboard_ = *next; renderHighlight(); scrollIntoView(*next); }
        break;
    case key::enter:
        if (keyboard_) pick(items_[*keyboard_]);
        break;
    case key::escape:
        if (onClose) onClose();
        break;
    default: break;
    }
    return true;
}
// nextIndex (:384-402).
std::optional<std::size_t> Menu::nextIndex(std::uint32_t virtualKey) const {
    std::vector<std::size_t> enabled;
    for (std::size_t i = 0; i < items_.size(); ++i) if (items_[i].enabled) enabled.push_back(i);
    if (enabled.empty()) return std::nullopt;
    const bool horizontal = virtualKey == key::left || virtualKey == key::right, forward = virtualKey == key::right || virtualKey == key::down;
    const auto linear = [&] {
        const long step = forward ? 1 : -1, count = long(enabled.size());
        long current = step > 0 ? -1 : count;
        if (keyboard_) { const auto it = std::find(enabled.begin(), enabled.end(), *keyboard_); if (it != enabled.end()) current = long(it - enabled.begin()); }
        return enabled[std::size_t(((current + step) % count + count) % count)];
    };
    if (!keyboard_ || *keyboard_ >= tiles_) {
        if (horizontal && (tiles_ == 0 || keyboard_)) return std::nullopt;
        return linear();
    }
    if (horizontal) return linear();
    const std::size_t current = *keyboard_;
    const long target = long(current) + (forward ? long(columns_) : -long(columns_));
    if (target >= 0 && std::size_t(target) < tiles_ && items_[std::size_t(target)].enabled) return std::size_t(target);
    // Off the grid: on to the text rows below it, or round to the column's other end.
    std::vector<std::size_t> column, below;
    for (std::size_t i = current % columns_; i < tiles_; i += columns_) if (items_[i].enabled) column.push_back(i);
    for (std::size_t i : enabled) if (i >= tiles_) below.push_back(i);
    if (forward) return !below.empty() ? below.front() : column.front();
    return !below.empty() ? below.back() : column.back();
}
// scroll(by:) (:405-410).
void Menu::scroll(double delta) {
    if (!long_) return;
    const double before = offset_;
    setScroll(offset_ + delta);
    if (offset_ != before) bar_->noteScroll();
}
void Menu::renderHighlight() {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = static_cast<Row&>(*rows_[i]);
        row.setHighlighted(row.hovered() || keyboard_ == i);
    }
}
double Menu::rowTop(std::size_t index) const { return index < rows_.size() ? rows_[index]->layer().frame().y : 0; }
double Menu::maximumScroll() const { return std::max(0.0, contentHeight_ - list_->bounds().height); }
void Menu::scrollIntoView(std::size_t index) {
    const double top = rowTop(index), height = index < rows_.size() ? rows_[index]->layer().frame().height : rowHeight;
    const double before = offset_;
    if (top < offset_) setScroll(top);
    else if (top + height > offset_ + list_->bounds().height) setScroll(top + height - list_->bounds().height);
    if (offset_ != before && bar_) bar_->noteScroll();
}
// setScroll (:428-438).
void Menu::setScroll(double value) {
    offset_ = std::min(maximumScroll(), std::max(0.0, value));
    const auto frame = content_->frame();
    content_->setFrame({frame.x, -offset_, frame.width, frame.height});
    if (!long_) return;
    const double trackHeight = track_->bounds().height;
    const double thumbHeight = std::max(18.0, trackHeight * list_->bounds().height / std::max(1.0, contentHeight_));
    const double fraction = maximumScroll() > 0 ? offset_ / maximumScroll() : 0;
    const bool resized = thumb_->bounds().height != thumbHeight;
    thumb_->setFrame({0, fraction * (trackHeight - thumbHeight), 5, thumbHeight});
    if (resized) setBackground(*thumb_, env_->palette.accentInk());
}
// The thumb centre follows the pointer: (y - thumb/2) / (track - thumb) (:441-445).
void Menu::scrollTrack(double y) {
    const double trackHeight = track_->bounds().height, thumbHeight = thumb_->bounds().height;
    const double fraction = std::min(1.0, std::max(0.0, (y - thumbHeight / 2) / std::max(1.0, trackHeight - thumbHeight)));
    setScroll(fraction * maximumScroll());
}

// ---------------------------------------------------------------------------
// HUDPopover (:450-499)
// ---------------------------------------------------------------------------
Popover::Popover(Environment& env, std::string title, std::optional<std::string> subtitle, double width, double contentHeight, bool closable)
    : arena_("popover", &env.deadlines), builder_{env, arena_, controls_} {
    const auto& p = env.palette;
    if (subtitle && subtitle->empty()) subtitle.reset();
    const bool headerless = title.empty() && !subtitle && !closable;
    const double height = (headerless ? 12 : 12 + 12 + 10) + contentHeight + 12;
    layer_ = &builder_.layer("popover");
    content_ = &builder_.layer("popover.content");
    applyPanelChrome(builder_, *layer_, {width, height});
    layer_->setAnimatable();                             // the entrance wipe and fade
    layer_->setPartRoot(true);
    layer_->setZPosition(order::menu);
    TextBlock heading(std::move(title), TextStyle{12, FontWeight::bold, p.token(ThemeToken::textOnDark), 1, .16});
    const double headingWidth = preferredWidth(heading, env.text);
    heading.setLineFrame({12, 10, headingWidth + 1, 12});
    if (!headerless) { Layer& text = builder_.layer("popover.heading"); text.setText(std::move(heading)); layer_->add(text); }
    if (subtitle) {
        TextBlock cjk(std::move(*subtitle), type::labelCJK(p));
        cjk.setLineFrame({12 + headingWidth + 1 + 8, 10.25, preferredWidth(cjk, env.text) + 1, 11.5});
        Layer& text = builder_.layer("popover.subtitle"); text.setText(std::move(cjk)); layer_->add(text);
    }
    content_->setFrame({12, headerless ? 12.0 : 34.0, width - 24, contentHeight});
    layer_->add(*content_);
    if (closable) {
        auto& button = builder_.make<IconButton>("popover.close", core::localized("Close", "关闭", env.language), Icon::x, IconButton::Style::back,
            false, false, 24, 10);
        button.layer().setFrame({width - 12 - 24, 6, 24, 24});
        button.onActivate = [this] { if (onClose) onClose(); };
        close_ = &button;
        layer_->add(button.layer());
    }
}
Popover::~Popover() = default;
// place (:490-496): right-aligned to the anchor's right edge, above it (-8); below when there is no room.
void Popover::place(Rect anchor, Size box) {
    const auto size = layer_->bounds();
    const double x = std::min(std::max(0.0, anchor.x + anchor.width - size.width), box.width - size.width);
    double y = anchor.y - 8 - size.height;
    if (y < 0) y = anchor.y + anchor.height + 8;
    layer_->setFrame({x, y, size.width, size.height});
}
void Popover::playEntrance(const motion::Clock& clock) { motion::wipe(*layer_, WipeOrigin::top, duration::menuWipe, clock, easing::standard, 0, .4); }
} // namespace endfield::modules::design
