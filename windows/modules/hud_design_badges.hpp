#pragma once
// Static badges, readouts and empty states: the ports of the non-interactive
// parts of Sources/HUDDesignBadges.swift (HUDChip, HUDBadge, HUDEmptyState,
// HUDBigNumeral, HUDStatPlate, HUDCells). LIB-35, LIB-36, LIB-39, LIB-42..44.
// They build layers in the caller's arena; updates in place (setTitle, set,
// setCounter) change only the affected text/content and therefore only the
// surface that holds it.
#include "modules/hud_design_control.hpp"
#include "modules/hud_design_icons.hpp"
#include <tuple>

namespace endfield::modules::design {

// HUDChip (LIB-35): height 18, padding 7; optional 10 px icon at (7, 4) then
// +15; chip style text at y 4.25; width ceil(x + text + 7).
class Chip final {
public:
    enum class Variant : std::uint8_t { yellow, dark, light, outline, cyan, custom };
    Chip(Builder&, std::string_view id, std::string title, Variant, std::optional<Icon> = {},
        std::optional<std::pair<Color, Color>> custom = {}); // custom: fill, text
    Layer& layer() const noexcept { return *layer_; }
    void setTitle(std::string);
private:
    Builder* builder_; Layer *layer_, *text_{}; Variant variant_; std::optional<Icon> icon_;
    Layer* glyph_{}; std::optional<std::pair<Color, Color>> custom_;
    std::tuple<Color, Color, std::optional<Color>> colors() const; // fill, text, ring
};

// HUDBadge (LIB-36). NEW: yellow, 1.5 px #111 ring, height 12, width text +
// 10, skewed -8 deg (m21 = tan(-8 deg)), z 2. Stamp: 2 px accentInk border,
// height 20, width text + 20, rotated -4 deg; callers pop it in. Diamond: a
// 7 px (or given) orange square rotated 45 deg.
namespace badge {
Layer& newBadge(Builder&, std::string_view id);
Layer& stamp(Builder&, std::string_view id, std::string title);
Layer& diamond(Builder&, std::string_view id, double size = 7);
}

// HUDEmptyState (LIB-39): the light hatch panel (default 590 x 250) with a 1 px
// ringField, `[ TITLE ]` (accentInk brackets, emptyTitle) centred, optional
// labelCJK subtitle 30 px below; the block centred in height - reserving.
namespace emptyState {
// English inside the brackets ALWAYS; a non-English UI adds the pair's own
// translation (core::localized) as the small grey line; English shows the
// English title alone (round 5 owner decision; LIB-L8 pairs).
Layer& bilingual(Builder&, std::string_view id, std::string_view english, std::string_view simplifiedChinese,
    Size size = {metric::boxWidth, 250}, double reserving = 0);
bool bilingualHasSubtitle(core::Language) noexcept;
Layer& make(Builder&, std::string_view id, std::string title, std::optional<std::string> subtitle = {},
    std::optional<TextRegion> subtitleLanguage = {}, Size size = {metric::boxWidth, 250}, double reserving = 0);
double textHeight(bool subtitle) noexcept;                 // 20, or 20 + 10 + 11.5
double textBottom(Size, bool subtitle, double reserving) noexcept;
Layer& crosshair(Builder&, std::string_view id);           // 11 x 11 +, foreground(.45)
}

// HUDBigNumeral (LIB-42): numeral (64 light, lh .9, tracking -.02, tabular)
// with a unit at size x .34 medium in legible(tone), top-aligned at x width +
// 4, y size x .08; width number + 4 + unit, height size x .9.
class BigNumeral final {
public:
    BigNumeral(Builder&, std::string_view id, double size = 64);
    Layer& layer() const noexcept { return *layer_; }
    void set(std::string number, std::string unit, std::optional<Color> tone = {}, std::optional<Color> numberTone = {});
private:
    Builder* builder_; Layer *layer_, *value_, *unit_; double size_;
};

// HUDStatPlate (LIB-43): height 9 + 10 + 6 + valueSize + 10 (53 for 18 px),
// tone(232323) -> tone(1B1B1B), 1 px ringTab, 8 px L-corner tone(6A6A6A);
// label (12, 9); value (12, 25) in valueStyle; highlighted -> accentInk; tone
// -> legible(tone); " unit" in statUnit; valueMinimumScale shrink-to-fit.
class StatPlate final {
public:
    StatPlate(Builder&, std::string_view id, double width, std::optional<TextStyle> valueStyle = {});
    Layer& layer() const noexcept { return *layer_; }
    void setValueMinimumScale(double);
    void set(std::string label, std::string value, std::optional<std::string> unit = {}, bool highlighted = false,
        std::optional<Color> tone = {});
    static std::vector<Rect> gridFrames(int count, double width, int columns = 3, double rowHeight = 53);
private:
    Builder* builder_; Layer *layer_, *label_, *value_; TextStyle valueStyle_;
};

// HUDCells (LIB-44): gap 3; segment bar (8) or capacity cells (22); empty
// capacity tone(2A2A2A) + ringField, segment iconButton + tone(4C4C4C);
// filled capacity = vertical high -> low gradient without border, segment =
// solid; fill changes .3 (capacity) / .2 (segment) ease. Charging pulse: the
// next pulseCount empty cells pulse empty -> mid -> empty over 2.4 s
// easeInOut, infinite, phase 80 ms per cell; mid = caller's, else 3A3826 with
// a tone, else 4D4A1C; Light: mixed(empty, high, .35). Not under Reduce
// Motion. Each cell is an animatable tinted surface; a filled capacity cell
// swaps to a gradient surface (content event). The pulse is a live loop: the
// owner keeps frames only while pulseCount() > 0 and the module shows.
class Cells final {
public:
    Cells(Builder&, std::string_view id, int count, double width, bool capacity, std::optional<double> height = {});
    Layer& layer() const noexcept { return *layer_; }
    struct Tone { Color high, low; };
    void set(int filled, std::optional<Tone> = {}, bool pulsing = false, bool animated = true,
        std::optional<Color> pulse = {}, std::optional<int> pulseCount = {});
    int pulseCount(double now) const noexcept;
private:
    Builder* builder_; Layer* layer_; std::vector<Layer*> cells_, rings_; bool capacity_; // cells_: the tinted fills
};
} // namespace endfield::modules::design
