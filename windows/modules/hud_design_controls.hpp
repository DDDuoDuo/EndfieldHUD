#pragma once
// The interactive components of the redesign: the ports of
// Sources/HUDDesignRows.swift (HUDRowPlate, HUDToggle, HUDSegmentedControl,
// HUDSheen), Sources/HUDDesignButtons.swift (HUDButton, HUDIconButton, HUDPill,
// HUDSwatch, HUDDragGrip), Sources/HUDDesignSlider.swift (HUDSlider,
// HUDSliderBubble), Sources/HUDDesignMenu.swift (HUDDropdown),
// Sources/HUDDesignTabs.swift (HUDTabs) and the keycap group / field frame of
// Sources/HUDDesignBadges.swift (HUDKeycapGroup, HUDFieldFrame). LIB-24..34,
// LIB-37, LIB-38, LIB-51, LIB-56..58.
//
// Every component is constructed with a Builder (hud_design_control.hpp),
// makes its layers in the builder's arena with the Mac's layer structure and
// marks the ones it animates (feedback overlays, knobs, notches, thumbs,
// carets, underlines) animatable; everything else of the component is static
// content compacted with its neighbours. Geometry, colours and durations are
// the Mac's (inventory-LIB.md has the full numbers; the implementation cites
// the Swift line it ports). Hover and press motion runs through motion::*
// from hoverChanged()/pressChanged(), exactly like the Swift overrides.
#include "modules/hud_design_control.hpp"
#include "modules/hud_design_format.hpp"
#include "modules/hud_design_icons.hpp"

namespace endfield::modules::design {

// HUDSheen (LIB-28): one pass of a white band (white 0 -> alpha .28 -> 0 at
// .35/.5/.65, direction (0,.3) -> (1,.7)) from -1.1w to +1.1w over .70 s E,
// clipped to the plate; hidden at once on exit; nothing under Reduce Motion.
// The band is a lazy surface clipped by the plate's own alpha (setAlphaClip):
// it exists only while it moves.
class Sheen final {
public:
    Sheen(Builder&, std::string_view id, double alpha = .28);
    Layer& layer() const noexcept { return *layer_; }
    void setShape(const Layer& plate, Size bounds);
    void play(bool on, const motion::Clock&);
private:
    Layer* layer_; Layer* band_; const Layer* plate_{};
    double alpha_{}; std::uint64_t serial_{};   // a newer pass keeps the band (the Mac's completion check)
};

// HUDRowPlate (LIB-24, LIB-25): standard 50, compact 44, slider 44, nav 50
// (chevron "›" or "↗"), custom height (App Shortcuts 56), width default 590.
// Non-nav rows are container targets that never take a press; nav rows are
// clickable as a whole. Hover: translateX 0 -> 3 (.22 E), brighten .22 ease,
// notch #2A2A2A -> yellowLow (.22 ease, tint) and scaleY (h-10)/(h-18), one sheen.
class RowPlate final : public Control {
public:
    enum class Variant : std::uint8_t { standard, compact, slider, nav };
    RowPlate(Builder&, std::string id, std::string title, std::optional<std::string> sublabel = {},
        Variant = Variant::standard, std::string chevron = "›", double width = metric::boxWidth,
        std::optional<double> height = {});
    Variant variant() const noexcept { return variant_; }
    void setTitle(std::string);
    void setSublabel(std::optional<std::string>);
    void setTitleInset(double);                 // room for a leading grip or glyph (System > Modules: 56)
    Rect titleFrame() const noexcept;           // the label's line box (redaction, tests)
    void setCluster(std::vector<Layer*>);       // right-aligned at w - 12 (- 22 nav), 8 apart
    Rect controlArea() const noexcept;          // slider rows: x 202 to w - 12, 30 tall
    Layer& cluster() const noexcept { return *cluster_; }
    std::optional<std::string> readout;         // a static row read as text (Event Log)
    void hoverChanged() override;
    bool pointerDown(Point) override;
    std::optional<AccessibilityRole> accessibilityRole() const override;
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    Variant variant_; double titleInset_{};
    Layer *plate_, *brighten_, *notch_, *tag_, *title_, *sublabel_, *chevron_, *cluster_;
    std::unique_ptr<Sheen> sheen_;
    std::vector<Layer*> clusterItems_;
    void layoutContents();
};

// HUDToggle (LIB-26): 88 x 26 OFF | ON (always English); knob 42 x 22 slides
// 0 <-> 42 in .18 SNAP with knobOff <-> yellow (.18 ease); labels .14 ease;
// registration corner marks on the knob; disabled .35. activate toggles,
// then onChange(isOn), then onActivate. Animated only when user-toggled.
class Toggle final : public Control {
public:
    static constexpr Size size{88, 26};
    Toggle(Builder&, std::string id, std::string label, bool isOn);
    bool isOn() const noexcept { return on_; }
    void setOn(bool, bool animated);
    std::function<void(bool)> onChange;
    void activate() override;
    void enabledChanged() override;
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::toggle; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    bool on_; Layer *knob_, *knobFill_, *marks_; std::array<Layer*, 2> labels_{};
    void render(bool animated);
};

// HUDSegmentedControl (LIB-27): height 30, gap 4, equal columns; fill plate
// when selected (yellow in the Work Mode variant) else control; ring clear /
// muted on hover / ringField; 3 px #111 left bar on the selection (not in the
// yellow variant); hover colours .22 ease; selection change unanimated.
// Pressing the selected segment calls onReselect. The group is not a target;
// segments are tabs for accessibility.
class SegmentedControl final : public Control {
public:
    class Segment;
    SegmentedControl(Builder&, std::string id, std::string label, std::vector<std::string> options,
        int selectedIndex, double width, bool yellow = false);
    int selectedIndex() const noexcept { return selected_; }
    void select(int, bool notify);
    std::function<void(int)> onChange, onReselect;
    std::span<Control* const> segments() const noexcept { return segments_; }
    bool contains(Point) const override { return false; }
    std::optional<AccessibilityRole> accessibilityRole() const override { return std::nullopt; }
    void renderSegments(bool animated);
private:
    std::vector<Control*> segments_; int selected_; bool yellow_;
};

// HUDButton (LIB-30): small 26/11/10.5, regular 32/16/12, large 40/22/13.
// primary = the accent gradient in chamfer 9 with a 4 x 4 #111 pixel; light =
// ECEDE8 -> CDCEC9; secondary = flat secondaryButton (hover secondaryButtonHover)
// with an accent-ink bar that becomes a ▶ on hover (.2 ease; Windows cross-
// fades the two shapes: a path morph is not a numeric pose - recorded gap).
// Width ceil(leading + [15 + 8 if titled] + text + padding); hover lift 1 and
// brighten .07; press .965; disabled .35 and inert.
class Button final : public Control {
public:
    enum class Kind : std::uint8_t { primary, secondary, light };
    enum class Height : std::uint8_t { small, regular, large };
    Button(Builder&, std::string id, std::string title, std::optional<Icon> icon, Kind, Height = Height::regular);
    std::string title() const;
    void setTitle(std::string);
    double preferredWidth() const;
    void place(Point origin, std::optional<double> width = {}); // flex width centres icon and title
    void hoverChanged() override;
    void pressChanged() override;
    void enabledChanged() override;
private:
    Kind kind_; Height height_; std::optional<Icon> icon_;
    Layer *plate_, *brighten_, *text_, *glyph_{}, *bar_{}, *arrow_{}, *corner_{};
    void layoutContents();
};

// HUDIconButton (LIB-31): side 30 (big 44), glyph 15 (big 20, back 14); styles
// dark / on / light / back (round, border 2 or 1.5 when side < 30, fill
// foreground(1) on hover with glyph tone(111111), colour change .12; others
// .22); `round` makes any style circular (ellipse hit path); hover 1.06,
// press .94 (not back); disabled .35. setIcon swaps the glyph in place.
class IconButton final : public Control {
public:
    enum class Style : std::uint8_t { dark, on, light, back };
    IconButton(Builder&, std::string id, std::string label, Icon, Style = Style::dark, bool round = false,
        bool big = false, std::optional<double> side = {}, std::optional<double> iconSize = {});
    bool isOn() const noexcept { return on_; }
    void setOn(bool);
    void setIcon(Icon);
    // The init colour fade (fill from clear, border from black, .22 / back .12
    // ease); runs on construction and when a kept button is shown again.
    void playCreationFade();
    void hoverChanged() override;
    void pressChanged() override;
    void enabledChanged() override;
private:
    Style style_; double side_, iconSize_; bool round_, on_{}; Icon icon_;
    Layer *fill_, *ring_, *glyph_;
    void applyStyle(bool animated);
};

// HUDPill (LIB-32): the confirm-bar pills; height 38, width max(150, 16 + 22 +
// 8 + label + 10 + 28 + 5), radius 19; cancel / confirm / danger colours;
// glyph circle 28 at (w - 33, 5) scales 1.1 and rotates -8 deg on hover (.25
// E); the 1 px line opacity .4 -> .75; press .97 (.10 ease).
class Pill final : public Control {
public:
    enum class Kind : std::uint8_t { cancel, confirm, danger };
    Pill(Builder&, std::string id, std::string title, Icon, Kind);
    Kind kind() const noexcept { return kind_; }
    void hoverChanged() override;
    void pressChanged() override;
private:
    Kind kind_; Layer *plate_, *circle_, *glyph_, *line_, *ring_{};
};

// HUDSwatch (LIB-33): 24 square, 1 px #111 ring; selected adds a 3 px yellow
// ring at inset -5 with a 2 px #111 ring inside; nullopt colour = the conic
// custom-colour circle (90 interpolated wedges: the rasterizer has no conic
// gradient); hover 1.08 (.22 E); accessibility tab(selected).
class Swatch final : public Control {
public:
    Swatch(Builder&, std::string id, std::string label, std::optional<Color>, double size = 24);
    bool selected() const noexcept { return selected_; }
    void setSelected(bool);
    void hoverChanged() override;
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::tab; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    bool selected_{}; Layer *ring_, *selection_;
};

// HUDDragGrip (LIB-34): 2 x 3 dots of 2 px, 4 apart (6 x 10) in bracket ->
// #111 while hovered or dragging (.22 ease, tint). A drag starts after 3 px
// (began reports the press point), then moved, then ended; a press without a
// drag activates; clearing mid-drag ends at the start point. No label: out of
// the accessibility tree (owners expose move actions instead).
class DragGrip final : public Control {
public:
    enum class Phase : std::uint8_t { began, moved, ended };
    DragGrip(Builder&, std::string id, std::string label, Size size, Point dotsAt);
    std::function<void(Phase, Point)> onDrag;
    bool dragging() const noexcept { return dragging_; }
    bool pointerDown(Point) override;
    void pointerDragged(Point) override;
    void pointerUp(Point) override;
    void clearInteractionState() override;           // a drag in progress ends at its start point
    void hoverChanged() override;
    std::optional<AccessibilityRole> accessibilityRole() const override;
private:
    Layer* dots_; std::optional<Point> pressPoint_; bool dragging_{};
};

// HUDSlider (LIB-56): height 30; readout column 58 (+8) unless hidden; track
// max(40, w - 66); dark variant only in dark theme (`dark && !light`); fill
// yellowLow -> yellow from 0 or from `zero`; 11 ticks; thumb 9 x 17 with a
// 2 x 11 core, scaleY 21/17 on hover/drag (.12 ease); `[ value ]` readout.
// Press jumps (onChange(v, false)), drag follows, release commits (v, true);
// nudge(±1) and accessibility steps commit; cancelDrag() ends without commit.
class SliderBubble;
class Slider final : public Control {
public:
    struct Range {
        double minimum{}, maximum{1}, step{};
        std::optional<double> zero;
        double clamp(double) const noexcept;    // snaps to minimum + k x step
        double fraction(double) const noexcept;
        bool operator==(const Range&) const = default;
    };
    static constexpr double readoutWidth = 58, height = 30;
    Slider(Builder&, std::string id, std::string label, double value, Range, ValueFormat, double width,
        bool dark = false, bool showsReadout = true, bool showsTicks = true);
    ~Slider() override;
    double value() const noexcept { return value_; }
    const Range& range() const noexcept { return range_; }
    bool dragging() const noexcept { return dragging_; }
    void setValue(double);                      // no callback
    void nudge(double direction);               // one step, commits
    void cancelDrag();
    std::function<void(double value, bool final)> onChange;
    // The canvas's top layer (z 40) that hosts the bubble (Canvas::hostBubble).
    void setBubbleHost(Layer* top) noexcept { bubbleHost_ = top; }
    bool contains(Point) const override;        // x -4.5 ... track + 4.5, full height
    bool pointerDown(Point) override;
    void pointerDragged(Point) override;
    void pointerUp(Point) override;
    void hoverChanged() override;
    void enabledChanged() override;
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::slider; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    Range range_; ValueFormat format_; bool dark_, dragging_{};
    double value_, trackWidth_{};
    Builder* builder_;
    Layer *track_, *fill_, *zero_, *ticks_, *thumb_, *core_, *readout_;
    Layer* bubbleHost_{};
    std::unique_ptr<SliderBubble> bubble_;
    void render();
    double valueAt(Point) const;
    void renderThumbGrowth();
    void showBubble();
    void updateBubble();
};
// HUDSliderBubble (LIB-57): only while dragging, in the top layer (z 40);
// body FAFAFA radius 10, height 21, width text + 16, shadow .45 r 8 y 6, a
// #111 plate 2 px lower, an 8 x 4 arrow; bottom-centred 6 px above the thumb.
// Show/hide: opacity .14 ease and scale .9 <-> 1 .18 SNAP. Its text changes
// with the value while dragging: one small surface updated per value step
// (an input event, not a frame).
class SliderBubble final {
public:
    SliderBubble(Builder&, std::string_view id);
    Layer& layer() const noexcept { return *layer_; }
    void show(std::string value, Point anchor);
    void setVisible(bool, const motion::Clock&);
private:
    Builder* builder_; Layer *layer_, *shadow_, *body_, *arrow_, *text_; bool visible_{};
};

// HUDDropdown (LIB-51): regular 178 x 30 (or width), small 26 tall and
// max(96, 12 + text + 14 + 9 + 10) wide; value ellipsised at x 12 (w - 45);
// accent bar 2 x h scaleY 0 -> 1 (.14 E) on hover or open; caret 9 x 6 at
// w - 14.5, open rotates 180 deg, hover moves 2 px down (.16 E); optional
// value language; setArtwork replaces the value text with an aspect-fit
// image (the icon-grid choosers), the text stays the spoken value.
class Dropdown final : public Control {
public:
    enum class Height : std::uint8_t { regular, small };
    Dropdown(Builder&, std::string id, std::string label, std::string value, std::optional<TextRegion> valueLanguage = {},
        Height = Height::regular, std::optional<double> width = {});
    const std::string& valueText() const noexcept { return value_; }
    void setValue(std::string, std::optional<TextRegion> language = {});
    void setArtwork(std::optional<ImageSource>, Size imageSize, double height);
    bool isOpen() const noexcept { return open_; }
    void setOpen(bool);
    void hoverChanged() override;
    void enabledChanged() override;
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::popUp; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    Builder* builder_; std::string value_; bool open_{};
    Layer *text_, *bar_, *caret_, *artwork_{};
    void renderState();
};

// HUDTabs (LIB-58): bar 34 tabBar with a 1 px ringTab frame drawn BENEATH the
// tabs; Q/E keycaps (15 x 15) unless keycaps = false (then no inset 28);
// equal widths; active tab 37 tall raised 3 px, yellowHigh -> yellow ->
// yellow, bold #111 text, underline 36 % x 3 accentInk at y h + 3; separators
// hidden beside the active tab; news diamond; inactive hover fill
// foreground(.06), lift 1, text foreground(1). Selection wipes the new fill
// open from the centre (.20 E); onSelect(index, direction +1 / -1); cycle(±1)
// for Q/E wraps around. The separators are made once and the two beside the
// active tab hidden (the Mac rebuilds them per layout: same pixels).
class Tabs final {
public:
    class Tab;
    struct Title { std::string text; std::optional<Icon> icon; };
    Tabs(Builder&, std::string id, std::vector<Title>, int selectedIndex, double width, bool keycaps = true, bool compact = false);
    Layer& layer() const noexcept { return *layer_; }
    int selectedIndex() const noexcept { return selected_; }
    void select(int, bool notify);
    void cycle(int step);
    void setNews(int index, bool);
    std::function<void(int index, double direction)> onSelect;
    std::span<Control* const> controls() const noexcept { return tabs_; }
private:
    Builder* builder_; Layer* layer_; std::vector<Control*> tabs_; std::vector<Layer*> separators_; int selected_; bool keycaps_;
    void layoutTabs();
};

// HUDKeycapGroup (LIB-37): field tabBar with 1 px ringField, height 34;
// keycaps from x 10, 6 apart, `+` between; width max(interactive ? 178 : 0,
// x + 4). Recording: ring accentInk, keys hidden, the scanning hatch (bands
// tone(262414), period 12 sqrt2, a 2x strip translating 0 -> -34 per 1 s
// linear, infinite), the prompt and a `_` cursor blinking 1 -> .2 -> 1 over
// 1.2 s easeInOut, infinite; static and solid under Reduce Motion; both loops
// only while recording (the only live loops besides Cells' pulse and the caret).
class KeycapGroup final : public Control {
public:
    KeycapGroup(Builder&, std::string id, std::string label, std::vector<std::string> keys, bool interactive,
        double minimumWidth = 178);
    void setKeys(std::vector<std::string>);
    void setRecording(bool, std::string prompt);
    bool recording() const noexcept { return recording_; }
    bool pointerDown(Point) override;
    std::optional<AccessibilityRole> accessibilityRole() const override;
private:
    Builder* builder_; bool interactive_, recording_{}; double minimumWidth_;
    Layer *keys_, *scan_, *prompt_, *cursor_, *stripes_{};
    std::uint64_t generation_{};                 // keycap layer ids of the current setKeys
    static Layer& keycap(Builder&, std::string_view id, const std::string& key);
};

// HUDFieldFrame (LIB-38): normal (1 px ringField; light B4B5B0), focused (ring
// and 3 px left bar in focusColor = accentInk or the caller's accent),
// invalid (red ring and bar); light fill F2F3EE, dark field; `plain`: none.
class FieldFrame final {
public:
    enum class State : std::uint8_t { normal, focused, invalid };
    FieldFrame(Builder&, std::string_view id, Size, bool light = false, bool plain = false);
    Layer& layer() const noexcept { return *layer_; }
    void setState(State);
    State state() const noexcept { return state_; }
    std::optional<Color> focusColor;            // nullopt: accentInk
    std::optional<Color> background;            // Personal ID's #212121 inline editor
private:
    Builder* builder_; Layer *layer_, *bar_; bool light_, plain_; State state_{State::normal};
};
} // namespace endfield::modules::design
