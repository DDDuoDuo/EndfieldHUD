#pragma once
#include "modules/hypergryph_account_sanity_gauge.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"

// Header wallet (HUDAccountGauge) on the shared renderer: retained textures for
// the static wallet artwork (re-rasterized only on a render scale change), the
// vector Sanity icon (likewise), the value text (on a value or scale change),
// the silhouette hover tint, and the recovery popover (LayerScene background
// and refresh arrow at the arrow's scale, plus the four text cells).
// The popover card is loaded the first time it opens and composed only while
// open or fading out; park() releases it after the HUD closed.
// Hover (0.14 s) and popover (0.16 s fade/slide) are numeric tracks on the
// host's frame clock; Reduce Motion makes them immediate. Paint order matches
// the Mac sublayers: back, deco, icon, value, highlight, popover (z 10). The
// wallet stack and the popover card are retained native groups at the render
// scale that blend in encoded sRGB like Core Animation; tilt, fade and the
// popover slide change only their output constants, hover feedback redraws
// only the small target.
namespace endfield::native {
struct NativeSanityGaugeStats {
    std::uint64_t artworkRasters{},iconRasters{},numberRasters{},tooltipRasters{},popoverLoads{},poseUpdates{},groupUploads{},groupRedraws{};
    std::uint64_t popoverParks{};
    bool popoverLoaded{},popoverUploaded{}; // the recovery card's rasters / GPU group right now
};
// HUDAccountGauge.renderText through the shared rasterizer (content events):
// `text` in the gauge's style (modules::hypergryph::gaugeTextLayer) drawn at
// `scale` (with the Sanity size as `magnification`) into a cell-sized raster
// centred on its ink. Empty text gives an empty image.
modules::hypergryph::GaugeImage rasterGaugeText(LayerRasterizer&,std::string_view text,const modules::hypergryph::GaugeTextStyle&,
    modules::design::Size cell,double scale,double magnification,core::TextRegion);
// HUDApplicationIcon.sanityImage: the vector icon in a `size` point square.
modules::hypergryph::GaugeImage rasterSanityIcon(LayerRasterizer&,double size,double scale,double magnification);
class NativeSanityGaugeScene final {
public:
    NativeSanityGaugeScene(std::shared_ptr<const modules::hypergryph::GaugeAssets>,LayerRasterizer&,LayerRasterOptions);
    ~NativeSanityGaugeScene();
    NativeSanityGaugeScene(const NativeSanityGaugeScene&)=delete;
    NativeSanityGaugeScene& operator=(const NativeSanityGaugeScene&)=delete;
    // Content event: retained rasters follow the model's value, render scale,
    // Sanity size and tooltip.
    bool sync(const modules::hypergryph::SanityGaugeModel&,core::Language,double time,bool reduceMotion);
    bool setHover(std::optional<std::string_view> control,double time,bool reduceMotion);
    // gaugeWorld maps gauge-local points (origin at the wallet's top-left).
    bool updatePose(const core::Matrix4& gaugeWorld,float opacity,double time);
    bool requiresFrames(double time) const;
    bool visible() const noexcept;
    void upload(Renderer&); // after the first updatePose()
    std::span<const LayerCompositionEntry> entries();
    void collected(Renderer&);
    bool release(Renderer&); // false while still published
    // The HUD closed with the recovery popover shut: release its group
    // target, numeral textures and card rasters (`renderer` may be null when
    // nothing was uploaded). The wallet stays. Its next opening loads the
    // card again. False while it is open, fading out or still composed.
    bool park(Renderer* renderer);
    NativeSanityGaugeStats stats() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
