#pragma once
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "modules/hypergryph_account_model.hpp"
#include "modules/hud_design_text.hpp"
#include <array>
#include <cmath>
#include <span>
#include <filesystem>
#include <functional>
#include <vector>

// Port of HUDAccountGauge (Mac 1.3.1): the header's original MoneyCell wallet
// (sanity, or Work Mode minutes when it has no sanity data, where the bar is a
// link to Work Mode), a vector Sanity icon and its recovery popover, sized to
// its text. The wallet sprites are prepared once by the unchanged Mac artwork
// code (windows/resources/account); the value and the recovery text are real
// text in the standard faces (HarmonyOS Sans Medium, the region's Noto for
// CJK) and the icon a vector shape, all drawn at the gauge's render scale
// (Mac 795b5cd, 51d9dac). One small raster per changed value; no timer, font
// atlas or render loop. The host's shared clock supplies time.
namespace endfield::modules::hypergryph {
struct GaugeImage {
    unsigned width{},height{};
    std::vector<std::uint8_t> rgba; // premultiplied RGBA8 sRGB, top-left rows
    bool operator==(const GaugeImage&) const=default;
};
struct GaugeAssets {
    GaugeImage back,deco,silhouette;
    // Explicit absolute windows/resources/account directory; every file is
    // pinned by its manifest SHA-256 and the manifest by the caller's pin.
    static GaugeAssets load(const std::filesystem::path& directory);
};
namespace gauge_geometry {
inline constexpr core::Rect bounds{0,0,168,42},bar{0,6,168,30},icon{3,-6,54,54},number{39,6,121,30};
inline constexpr core::Rect artwork{0,-6,168,54}; // union of the static artwork (icon overflows)
inline constexpr core::Point headerPosition{292,0},headerOrigin{270,2};
inline constexpr double popoverDuration=.16;
inline constexpr double highlightDuration=.14;
inline constexpr std::array<double,4> popoverColor{.065,.065,.065,.98};
inline constexpr double popoverCornerRadius=4;
}
// The pixels a cell of `size` points takes at `scale` (CGContext width and
// height ceil(size x scale), as HUDAccountGauge.renderText and
// HUDApplicationIcon.sanityImage allocate them).
inline unsigned cellPixels(double size,double scale) noexcept {return static_cast<unsigned>(std::ceil(size*scale));}
// CALayer contents compositing (contentsCenter nine-slice, resize, bilinear
// sampling, source-over) of the back and deco wallet bars at a render scale
// (1...16); covers gauge_geometry::artwork. The icon is its own vector draw.
GaugeImage composeGaugeArtwork(const GaugeAssets&,double scale);
// HUDControlHighlightLayer with useAlphaSilhouette: (0.75 white, alpha 0.30)
// clipped by the nine-sliced wallet silhouette; covers gauge_geometry::bar.
GaugeImage composeHighlight(const GaugeAssets&,double scale);
// HUDAccountGauge.renderText (Mac 795b5cd): one line of text in the standard
// faces (HarmonyOS Sans Medium, the region's Noto for CJK), white, shrunk to
// fit down to `minimumFit` and never truncated, drawn into a cell-sized
// raster centred on its ink. The value is right-aligned at 18 points and
// shrinks to 4/18; the recovery cells are 14.4 points, left-aligned, down to
// 0.6. Every cell is pure white (the CJK labels used to be white at 0.92).
struct GaugeTextStyle {double size{};bool rightAligned{};double minimumFit{};};
inline constexpr GaugeTextStyle gaugeValueText{18,true,4.0/18},gaugeRecoveryText{14.4,false,.6};
// The text leaf the rasterizer draws (a HUDTextLayer line box of the cell,
// design::textNode), in the given text region.
Json gaugeTextLayer(std::string id,std::string_view text,const GaugeTextStyle&,design::Size cell,core::TextRegion);
// A cell raster from the rasterizer's (straight sRGB RGBA, its top-left at
// `origin` points from the cell's, drawn at `scale`): whole pixels copied into
// ceil(cell x scale), premultiplied. `centreOnInk` (the text of
// gaugeTextLayer) also moves it vertically so its ink (alpha above 0) sits
// centred like renderText's; nothing inked gives a clear cell.
GaugeImage cellRaster(std::span<const std::uint8_t> straightRGBA,unsigned width,unsigned height,core::Point origin,design::Size cell,double scale,bool centreOnInk);
// HUDApplicationIcon.sanityImage (Mac 51d9dac): an original vector drawing of
// the Sanity item, a white faceted head outline with its tail running to the
// lower left, a grey head (101/255) inside it and a white bolt, traced
// top-down in the 80-unit frame of the sprite it replaces and scaled to a
// `size` point square, so the gauge's layout is unchanged.
struct SanityIconPolygon {std::vector<core::Point> points;double white{};};
const std::array<SanityIconPolygon,3>& sanityIconPolygons();
Json sanityIconLayer(std::string id,double size);
// HUDAccountGauge.Layout (Mac 1.3.0): the recovery menu, two rows of label
// and value with the refresh button at the right. Columns take their measured
// text; the menu widens from 224 up to 300 (growing left from the wallet's
// right edge, away from the clock) before labels shrink, so CJK text never
// leaves the panel. The value column is at least as wide as "000:00:00" so the
// menu keeps its width while the countdowns run. Popover-local rects.
struct GaugePopoverLayout {
    static constexpr double textSize=14.4,minimumWidth=224,maximumWidth=300,height=65,rowHeight=25;
    static constexpr double leading=10,columnGap=14,refreshGap=10,trailing=6,slack=2,refreshWidth=23,refreshHeight=25;
    static constexpr std::array<double,2> rows{6,33};
    static constexpr std::string_view valueTemplate="000:00:00";
    core::Rect frame{frameFor(minimumWidth)};double labelWidth{},valueWidth{};
    // labels/values: measured widths; templateWidth: the width of valueTemplate.
    static GaugePopoverLayout make(std::array<double,2> labels,std::array<double,2> values,double templateWidth);
    static core::Rect frameFor(double width) noexcept {return {168-width,40,width,height};}
    static core::Rect refreshFor(double width) noexcept {return {width-trailing-refreshWidth,19,refreshWidth,refreshHeight};}
    core::Rect refresh() const noexcept {return refreshFor(frame.width);}
    core::Rect cell(unsigned index) const noexcept; // label, label, value, value
    bool operator==(const GaugePopoverLayout&) const=default;
};
// The width the layout measures: HUDText.width in the standard faces
// (HarmonyOS Sans, the UI region's Noto; 14.4 medium, rounded up to .5) for
// the labels, the values and "000:00:00". The native owner supplies it from
// the shared rasterizer; content events only.
struct GaugeTextWidths {
    std::function<double(std::string_view)> standard;
};
std::string gaugeCountdown(std::optional<Time> deadline,Time date,bool hours);
std::string prefixGaugeValue(std::string_view,std::size_t characters=24);

struct GaugeAction {std::string id,label;core::Rect rect;bool enabled{};bool operator==(const GaugeAction&) const=default;};
// text: label, label, value, value.
struct GaugeTooltip {
    std::array<std::string,4> text;bool refreshEnabled{};GaugePopoverLayout layout;
    bool operator==(const GaugeTooltip&) const=default;
};
// HUDAccountGauge state without layers. update() reports which retained
// rasters must change; the scene owns textures and animation tracks.
class SanityGaugeModel final {
public:
    // icon: the vector icon redraws (its render scale changed); scale: the
    // render or refresh-arrow scale changed (the popover's vector parts and
    // the group targets follow).
    struct Changes {bool number{},icon{},visibility{},tooltip{},availability{},popover{},scale{};};
    std::function<void()> onRefresh;
    // SystemHUDView: accountGauge.onOpenWorkMode opens the Work Mode module
    // (also when the user hid it in Display > Modules).
    std::function<void()> onOpenWorkMode;
    GaugeTextWidths widths;
    // `scale`: the HUD's backing x design scale (HUDAccountGauge.update; a
    // non-finite one reads as 2). The value, icon and recovery text draw at
    // renderScale() = textContentsScale(scale, sizeFactor()).
    Changes update(std::string value,std::string accessibilityLabel,bool visible,double scale,
                   std::optional<SanityPresentation>,Time date,core::Language);
    // HUDAccountGauge.setScale: a HUD Scale, layout or display change between
    // clock ticks. Before the first update, or when the render scale stays,
    // nothing redraws (scale reports a refresh-arrow change alone).
    Changes setScale(double scale);
    // HUDAccountGauge.sizeFactor: the gauge's drawn size relative to its
    // 168 x 42 design (a placement size; 0.25...4, non-finite 1). A change
    // redraws the text and icon at the new size instead of stretching them.
    Changes setSizeFactor(double);
    double sizeFactor() const noexcept {return sizeFactor_;}
    // The raster scale for the current base scale and size (the glyph rule,
    // capped at 4 per on-screen point).
    double targetScale() const noexcept;
    // The refresh arrow and the menu's other vector parts: the HUD's rule for
    // a layer of that size (layerContentsScale), not the glyph scale.
    double refreshScale() const noexcept;
    bool perform(std::string_view id);              // "toggle"/"refresh" (toggle opens Work Mode on the link)
    bool mouseDown(std::optional<core::Point>);     // gauge-local points
    std::optional<std::string_view> hover(std::optional<core::Point>) const; // "toggle"/"refresh" control under pointer
    bool dismiss();
    bool canOpen() const noexcept;
    // Without sanity data the shown bar is a link to Work Mode (Mac 1.3.0,
    // also for a linked account still showing "— / —").
    bool opensWorkMode() const noexcept;
    bool isInteractive() const noexcept {return canOpen()||opensWorkMode();}
    // The recovery popover's frame in gauge points (its current layout).
    core::Rect popoverRect() const noexcept {return tooltip_.layout.frame;}
    bool popoverOpen() const noexcept {return open_;}
    bool hidden() const noexcept {return hidden_;}
    // Pixels per point of the drawn value, icon and recovery text (0 before
    // the first update).
    double renderScale() const noexcept {return scale_;}
    const std::string& value() const noexcept {return value_;}
    const std::string& accessibilityLabel() const noexcept {return label_;}
    std::vector<GaugeAction> accessibleActions(core::Language) const;
    // SystemHUDView.layoutAccountGaugeControls: the toggle's accessibility
    // value while the popover is open, "Next recovery 05:12; Full recovery
    // 23:41:07" (the popover's countdowns); none while it is closed.
    std::optional<std::string> accessibilityValue(core::Language) const;
    const GaugeTooltip& tooltip() const noexcept {return tooltip_;}
    const std::optional<SanityPresentation>& sanity() const noexcept {return sanity_;}
    std::uint64_t updateCount() const noexcept {return updates_;}
    std::uint64_t numberRenders() const noexcept {return numberRenders_;}
    std::uint64_t iconRenders() const noexcept {return iconRenders_;}
    std::uint64_t tooltipRenders() const noexcept {return tooltipRenders_;}
    // Next whole second at which the open popover's countdown text changes.
    std::optional<Time> nextTooltipChange() const;
private:
    std::optional<SanityPresentation> sanity_;Time date_{};std::string value_,label_;bool hidden_{true},open_{};
    double scale_{},iconScale_{},baseScale_{2},sizeFactor_{1};std::string renderedKey_;GaugeTooltip tooltip_;core::Language language_{core::Language::english};
    std::uint64_t updates_{},numberRenders_{},iconRenders_{},tooltipRenders_{};
    bool renderTooltip();
    bool drawIcon(double scale);
};
}
