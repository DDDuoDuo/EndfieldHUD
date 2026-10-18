#pragma once
#include "native/layer_raster.hpp"
#include "native/renderer.hpp"
#include <optional>

namespace endfield::native {
struct LayerMaskProjection {
    std::optional<PlaneMask> plane;
    bool clipsAll{};
    std::vector<LayerRasterIssue> unsupported;
};
// Converts an opaque filled convex quadrilateral into an exact unit-rectangle
// plane mask. ownerWorld is the masked layer's local-to-world transform;
// the mask's own position, anchor, bounds and transform are composed here.
// No raster, device, window, clock or renderer call occurs. Malformed numbers
// throw; valid but unsupported mask effects return explicit diagnostics.
// An empty/hidden/fully transparent mask sets clipsAll. Partial alpha is not
// treated as a rectangular clip because it requires group compositing.
LayerMaskProjection projectLayerMask(const ehud::data::Json& mask,
                                    const core::Matrix4& ownerWorld = {});
} // namespace endfield::native
