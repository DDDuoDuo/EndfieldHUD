#pragma once
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "modules/hypergryph_account_model.hpp"
#include <filesystem>
#include <functional>
#include <vector>

// Port of HUDAccountGauge: the header's original MoneyCell wallet (sanity, or
// Work Mode minutes when unlinked) and its recovery popover. Rendering inputs
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
inline constexpr core::Rect popover{168-224,40,224,65},refresh{195,19,23,25};
inline constexpr core::Point headerPosition{292,0},headerOrigin{270,2};
inline constexpr core::Rect nextLabel{9,6,99,25},fullLabel{9,33,99,25},nextValue{109,6,83,25},fullValue{109,33,83,25};
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
std::string gaugeCountdown(std::optional<Time> deadline,Time date,bool hours);
std::string prefixGaugeValue(std::string_view,std::size_t characters=24);

struct GaugeAction {std::string id,label;core::Rect rect;bool enabled{};bool operator==(const GaugeAction&) const=default;};
struct GaugeTooltip {std::array<std::string,4> text;bool refreshEnabled{};bool operator==(const GaugeTooltip&) const=default;};
// HUDAccountGauge state without layers. update() reports which retained
// rasters must change; the scene owns textures and animation tracks.
class SanityGaugeModel final {
public:
    struct Changes {bool number{},visibility{},tooltip{},availability{},popover{};};
    std::function<void()> onRefresh;
    Changes update(std::string value,std::string accessibilityLabel,bool visible,double scale,
                   std::optional<SanityPresentation>,Time date,core::Language);
    bool perform(std::string_view id);              // "toggle"/"refresh"
    bool mouseDown(std::optional<core::Point>);     // gauge-local points
    std::optional<std::string_view> hover(std::optional<core::Point>) const; // "toggle"/"refresh" control under pointer
    bool dismiss();
    bool canOpen() const noexcept;
    bool popoverOpen() const noexcept {return open_;}
    bool hidden() const noexcept {return hidden_;}
    double scale() const noexcept {return scale_;}
    const std::string& value() const noexcept {return value_;}
    const std::string& accessibilityLabel() const noexcept {return label_;}
    std::vector<GaugeAction> accessibleActions(core::Language) const;
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
