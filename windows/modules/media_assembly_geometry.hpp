#pragma once
#include "modules/media_assembly_model.hpp"

namespace endfield::modules {
// Pixel-space editing geometry shared by bounded previews and explicit exports.
// WIC supplies orientation-correct top-left pixels before this plan is made.
// Source crop rounding is performed in Core Image's bottom-left coordinates,
// then converted once; flooring top-left y independently changes the crop.
struct MediaAssemblyPixelPlan {
    unsigned sourceWidth{},sourceHeight{},cropWidth{},cropHeight{};
    int cropX{},cropY{};
    unsigned orientedWidth{},orientedHeight{},outputWidth{},outputHeight{};
    int quarterTurns{};bool mirrored{};
    // Map normalized output coordinates to normalized original-image UVs.
    // Outside [0,1] is retained for the source's transparent-border sampler.
    core::Point sourceUV(core::Point output)const noexcept;
};
MediaAssemblyPixelPlan mediaAssemblyPixelPlan(unsigned width,unsigned height,
    const MediaAssemblyAdjustments&,std::optional<unsigned>maximumDimension={},bool even=false);
}
