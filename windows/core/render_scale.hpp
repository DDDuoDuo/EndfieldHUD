#pragma once
#include <cmath>

// HUDRenderScale (Sources/HUDRenderScale.swift) parts shared by the design
// tokens (modules/hud_design_tokens: textContentsScale, layerContentsScale)
// and the rasterizer (LayerRasterOptions::magnification), so the cap and the
// magnification rule live in one place.
namespace endfield::core {
// HUDRenderScale.maximum: the highest pixels per on-screen point any HUD
// buffer is drawn at.
inline constexpr double maximumRenderScale = 4;
// HUDRenderScale.factor: how much a layer transform above a buffer enlarges it
// on screen (a Display > HUD elements size). Non-finite or not above 0 is 1.
inline double renderMagnification(double magnification) noexcept {
    return std::isfinite(magnification) && magnification > 0 ? magnification : 1;
}
} // namespace endfield::core
