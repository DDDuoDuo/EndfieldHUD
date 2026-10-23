#pragma once
// Menus and pop-overs: the port of Sources/HUDDesignMenu.swift (HUDPanelChrome,
// HUDMenu with icon grids and long-menu tracks, HUDPopover). LIB-50, LIB-52..55.
// A menu or pop-over owns its own LayerArena and ControlSet (it is created on
// open and destroyed on close, like the Mac's), is attached to the canvas's
// top layer (z 30) as one part, and enters with a top-down wipe (.18 E, fade
// from .4): one mask number per frame, nothing rasterized after the open.
#include "modules/hud_design_controls.hpp"
#include "modules/hud_design_scroll.hpp"

namespace endfield::modules::design {

// HUDPanelChrome (LIB-50): fill panel, 1 px ringMenu, baked shadow black .6
// (Light .25) radius 20 offset y +18 on the rect path, a yellow L (10 px legs,
// 2 px, accentInk) at (-1, -1) above the content. The ring and the L are
// children made in `panel`'s arena: call it once per panel, before adding content.
void applyPanelChrome(Builder&, Layer& panel, Size);

class Menu final {
public:
    // HUDMenu.Icon: the caller's cached artwork shown aspect-fit in `frame`
    // (default inset 6) of a `size` tile; `fill` nullopt = the dark field.
    struct IconTile {
        ImageSource image;
        Size size{40, 40};
        std::optional<Rect> frame;
        std::optional<Color> fill;
        bool operator==(const IconTile&) const = default;
    };
    struct Item {
        std::string id, title;
        std::optional<TextRegion> language;
        bool selected{}, danger{}, separatorBefore{}, enabled{true};
        std::optional<IconTile> icon;     // icon entries lead the menu
        bool operator==(const Item&) const = default;
    };
    static constexpr double rowHeight = 30, maximumListHeight = 210, tileGap = 6, gridWidthLimit = 300;
    static constexpr std::size_t longMenuThreshold = 7;

    // Width ceil(max(minimumWidth, 150, natural + (long ? 12 : 0), grid + (long
    // ? 12 : 0))), natural = 14 + title + 12 + 24; panel height list + 8.
    // Opens with the keyboard index on the selected item scrolled to
    // rowTop(selected) - 60. Row control ids are "<menu id>:<item id>".
    Menu(Environment&, std::string id, std::vector<Item>, double minimumWidth, Deadlines::Token parentToken = 0);
    ~Menu();
    const std::string& id() const noexcept { return id_; }
    Layer& layer() const noexcept { return *layer_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    bool isLong() const noexcept { return long_; }
    // Below the anchor + 4, left-aligned; shifted left to stay in the box;
    // flipped above (anchor top - 4 - height) past the box bottom by 20.
    void place(Rect anchor, Size box);
    void playEntrance(const motion::Clock&);
    // Up/Down wrap over enabled items (Left/Right linear too; in an icon grid
    // Up/Down move a row, leaving the grid goes to the text rows below, or
    // wraps within the column); Return / keypad Enter picks; Esc closes;
    // EVERY other key is swallowed (returns true).
    bool handleKey(const KeyInput&);
    // Wheel inside a long menu (delta box px, + down); notes the scroll.
    void scroll(double delta);
    void setScroll(double);
    double scrollOffset() const noexcept { return offset_; }
    std::optional<std::size_t> keyboardIndex() const noexcept { return keyboard_; }
    std::vector<Control*> controls();     // rows + the long menu's track
    std::function<void(const Item&)> onPick;   // the canvas closes the menu first
    std::function<void()> onClose;
private:
    class Row; class Track;
    Environment* env_; std::string id_; std::vector<Item> items_;
    LayerArena arena_; ControlSet controls_;
    Layer *layer_{}, *list_{}, *content_{}, *track_{}, *rail_{}, *thumb_{};
    std::vector<Control*> rows_; Control* trackControl_{};
    std::unique_ptr<ScrollBarDriver> bar_;    // long menus: the rail's idle-hiding visibility
    std::optional<std::size_t> keyboard_;
    double offset_{}, contentHeight_{}; bool long_{}; std::size_t tiles_{}, columns_{};
    void renderHighlight();
    void pick(const Item&);
    std::optional<std::size_t> nextIndex(std::uint32_t virtualKey) const;
    double rowTop(std::size_t) const;
    double maximumScroll() const;
    void scrollIntoView(std::size_t);
    void scrollTrack(double y);
};

// HUDPopover (LIB-55): width 270; height (headerless ? 12 : 34) + content +
// 12; heading 12 bold textOnDark tracking .16 at (12, 10), optional labelCJK
// subtitle 8 px right; a back-style 24 x 24 close (10 px x, "Close"/关闭) at
// (w - 36, 6); content at (12, 34) (12 when headerless: empty title, no
// subtitle, not closable - the E-Reader settings). Placement: right edge on
// the anchor's right, above it (-8), below (+8) without room, clamped to the
// box. The caller fills content() through builder() and passes its controls
// to Canvas::openPopover.
class Popover final {
public:
    Popover(Environment&, std::string title, std::optional<std::string> subtitle = {}, double width = 270,
        double contentHeight = 0, bool closable = true);
    ~Popover();
    Layer& layer() const noexcept { return *layer_; }
    Layer& content() const noexcept { return *content_; }
    Builder& builder() noexcept { return builder_; }
    Control* closeButton() const noexcept { return close_; }
    void place(Rect anchor, Size box);
    void playEntrance(const motion::Clock&);
    std::function<void()> onClose;
private:
    LayerArena arena_; ControlSet controls_; Builder builder_;
    Layer *layer_, *content_; Control* close_{};
};
} // namespace endfield::modules::design
