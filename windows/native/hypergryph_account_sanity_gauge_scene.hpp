#pragma once
#include "modules/hypergryph_account_sanity_gauge.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"

// Header wallet (HUDAccountGauge) on the shared renderer: retained textures for
// the static wallet artwork (re-rasterized only on scale change), the value
// numerals (only on value/scale change), the silhouette hover tint, and the
// recovery popover (LayerScene background/refresh arrow + numeral textures).
// Hover (0.14 s) and popover (0.16 s fade/slide) are numeric tracks on the
// host's frame clock; Reduce Motion makes them immediate. Paint order matches
// the Mac sublayers: back, deco, icon, value, highlight, popover (z 10). The
// wallet stack and the popover card are retained native groups that blend in
// encoded sRGB like Core Animation; tilt, fade and the popover slide change
// only their output constants, hover feedback redraws only the small target.
namespace endfield::native {
struct NativeSanityGaugeStats {std::uint64_t artworkRasters{},numberRasters{},tooltipRasters{},popoverLoads{},poseUpdates{},groupUploads{},groupRedraws{};};
class NativeSanityGaugeScene final {
public:
    NativeSanityGaugeScene(std::shared_ptr<const modules::hypergryph::GaugeAssets>,LayerRasterizer&,LayerRasterOptions);
    ~NativeSanityGaugeScene();
    NativeSanityGaugeScene(const NativeSanityGaugeScene&)=delete;
    NativeSanityGaugeScene& operator=(const NativeSanityGaugeScene&)=delete;
    // Content event: retained rasters follow the model's value/scale/tooltip.
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
    NativeSanityGaugeStats stats() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
