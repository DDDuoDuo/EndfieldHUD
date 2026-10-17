#pragma once
#include "core/module_presentation.hpp"
#include "core/scene.hpp"

namespace endfield::core {
// The original module's (0.18,0.72,0.26,1) timing, as observed from genuine
// paused Core Animation presentation layers. CA rounds the normalized input,
// control points and result to Float, with bounded approximate curve inversion.
// This is intentionally not a general CAMediaTimingFunction implementation.
// Finite times are clamped; NaN/Infinity are rejected. No allocation or clock.
double moduleCoreAnimationProgress(double normalizedTime);

// Evaluates the original module's identity <-> cardinal-axis transform tracks.
// Rotation, translation and perspective interpolate before recomposition in
// the source P*T*Rx*Ry order. In particular m44 contains interpolated Z times
// interpolated perspective, not the linear interpolation of endpoint m44.
// track.easedProgress is consumed as-is; callers wanting the observed CA timing
// use moduleCoreAnimationProgress once for the shared transition clock.
// Supports only the exact authored incoming/outgoing endpoints and identity.
// Arbitrary combined-axis/nonidentity-to-nonidentity CA interpolation is not
// established by this oracle and is rejected instead of silently approximated.
Matrix4 sampleModuleTransform(const ModulePresentationTransform& track);
} // namespace endfield::core
