#pragma once
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "modules/hypergryph_account_model.hpp"
#include <filesystem>
#include <functional>
#include <vector>

// Port of HUDAccountGauge (Mac 1.3.0): the header's original MoneyCell wallet
// (sanity, or Work Mode minutes when it has no sanity data, where the bar is a
// link to Work Mode) and its recovery popover, sized to its text. Rendering inputs
// are the original wallet sprites/icon (prepared once by the unchanged Mac
// artwork code, windows/resources/account) and the original HarmonyOS Sans SC
// Medium SDF numerals. One small raster per changed value; no timer, font
// atlas or render loop. The host's shared clock supplies time.
namespace endfield::modules::hypergryph {
struct GaugeImage {
    unsigned width{},height{};
    std::vector<std::uint8_t> rgba; // premultiplied RGBA8 sRGB, top-left rows
    bool operator==(const GaugeImage&) const=default;
};
class AccountNumerals {
public:
    // Same validation as the Mac loader (schema, metrics, exact glyph set, bounds).
    static std::optional<AccountNumerals> parse(std::string_view json);
    bool covers(std::string_view text) const;
    // Numerals.width: advance width at `preferredSize`; nullopt when the text
    // is empty or a glyph is not in the source set.
    std::optional<double> width(std::string_view text,double preferredSize=14.4) const;
    // Numerals.render: white premultiplied coverage, right-aligned by default.
    std::optional<GaugeImage> render(std::string_view text,double width,double height,double scale,
                                     double preferredSize=18,bool rightAligned=true) const;
    const std::string& family() const noexcept {return family_;}
    const std::string& style() const noexcept {return style_;}
private:
    struct Glyph {char32_t character{};double width{},height{},bearingX{},bearingY{},advance{};int columns{},rows{};std::vector<std::uint8_t> samples;};
    std::string family_,style_;double pointSize_{},padding_{},gradientScale_{};std::vector<Glyph> glyphs_;
    const Glyph* glyph(char32_t) const;
};
struct GaugeAssets {
    AccountNumerals numerals;
    GaugeImage back,deco,icon,silhouette;
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
// CALayer contents compositing (contentsCenter nine-slice, resize/resizeAspect,
// bilinear sampling, source-over) of back, deco and icon at a render scale;
// covers gauge_geometry::artwork.
GaugeImage composeGaugeArtwork(const GaugeAssets&,double scale);
// Optional number raster placed into the same artwork space (tests compare
// the whole idle wallet with the Mac's layer rendering).
void composeNumber(GaugeImage& artwork,const GaugeImage& number,double scale);
// HUDControlHighlightLayer with useAlphaSilhouette: (0.75 white, alpha 0.30)
// clipped by the nine-sliced wallet silhouette; covers gauge_geometry::bar.
GaugeImage composeHighlight(const GaugeAssets&,double scale);
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
// The widths the layout measures: Numerals.width (source SDF advances at
// 14.4) and HUDText.width in the standard faces (HarmonyOS Sans, the UI
// region's Noto; 14.4 medium, rounded up to .5). The native owner supplies the
// second from the shared rasterizer; content events only.
struct GaugeTextWidths {
    std::function<std::optional<double>(std::string_view)> numerals;
    std::function<double(std::string_view)> standard;
};
std::string gaugeCountdown(std::optional<Time> deadline,Time date,bool hours);
std::string prefixGaugeValue(std::string_view,std::size_t characters=24);

struct GaugeAction {std::string id,label;core::Rect rect;bool enabled{};bool operator==(const GaugeAction&) const=default;};
// text: label, label, value, value. sourceLabels: the labels use the source
// SDF numerals (English); other languages draw them in the standard faces.
struct GaugeTooltip {
    std::array<std::string,4> text;bool refreshEnabled{},sourceLabels{true};GaugePopoverLayout layout;
    bool operator==(const GaugeTooltip&) const=default;
};
// HUDAccountGauge state without layers. update() reports which retained
// rasters must change; the scene owns textures and animation tracks.
class SanityGaugeModel final {
public:
    struct Changes {bool number{},visibility{},tooltip{},availability{},popover{};};
    std::function<void()> onRefresh;
    // SystemHUDView: accountGauge.onOpenWorkMode opens the Work Mode module
    // (also when the user hid it in System > Modules).
    std::function<void()> onOpenWorkMode;
    GaugeTextWidths widths;
    Changes update(std::string value,std::string accessibilityLabel,bool visible,double scale,
                   std::optional<SanityPresentation>,Time date,core::Language);
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
    double scale() const noexcept {return scale_;}
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
    std::uint64_t tooltipRenders() const noexcept {return tooltipRenders_;}
    // Next whole second at which the open popover's countdown text changes.
    std::optional<Time> nextTooltipChange() const;
private:
    std::optional<SanityPresentation> sanity_;Time date_{};std::string value_,label_;bool hidden_{true},open_{};
    double scale_{};std::string renderedKey_;GaugeTooltip tooltip_;core::Language language_{core::Language::english};
    std::uint64_t updates_{},numberRenders_{},tooltipRenders_{};
    bool renderTooltip();
};
}
