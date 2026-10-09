#pragma once
#include "core/scene.hpp"
#include <array>
namespace endfield::core::source {
using SourceRGB=std::array<double,3>;
using SourceRGBA=std::array<double,4>;
// Calibrated source wheel colors use the original Generic RGB colorants/TRC.
// This portable profile conversion preserves the existing verified wheel math;
// it does not claim bit-identical ColorSync interpolation (<1/255 on its oracle).
SourceRGB genericRGBToSRGB(SourceRGB);
// Source NSColor.blended(.5, .black): convert into Generic RGB, clip that
// gamut, blend, and convert back. 1,033 original AppKit probes bound the
// portable matrix/TRC approximation to <.006 per sRGB channel at .35/.5.
SourceRGB sourceBlackBlend(SourceRGB accentSRGB,double blackFraction);
// TelemetryArtwork.cyan: NSColor.blended(.42, .white), using the same
// Generic RGB conversion as the black/selected-caption source path. Original
// AppKit probes at .42 (including light-theme .35 black first) bound every
// sRGB channel difference below .004; this is not bit-identical ColorSync.
SourceRGB sourceWhiteBlend(SourceRGB accentSRGB,double whiteFraction);
SourceRGB selectedCaptionColor(SourceRGB accentSRGB);
// Coordinates are normalized to the source wheel square; out-of-circle drag
// values retain hue and clamp saturation, exactly like the Mac control.
SourceRGBA colorAtWheel(Point normalized);
// Source selection indicator uses the displayed sRGB HSV coordinates; it is
// intentionally not an inverse color-profile transform.
Point colorWheelPoint(SourceRGBA);
}
