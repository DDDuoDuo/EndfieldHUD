#pragma once
// HUDCentreBackdrop.mask (Sources/HUDDesignDrawing.swift:247-264): the radial
// ramp of the centre disc (430 px around box (295, 304): opaque to 52 %, .55
// at 74 %, clear at 100 %), drawn once at half resolution and shared by the
// owner-drawn content that fills the disc (the Map's surface, Personal ID's
// background art). Only its alpha is sampled: white stops keep the texture at
// one byte per texel (the renderer's A8 alpha masks). The raster's bounds are
// the node's own, its frame's origin at 0: a box point p maps to ramp-local
// p - (centre - radius). Content events only; header-only so the Map (hud
// native) and the profile owner share the one definition.
#include "modules/hud_design_nodes.hpp"
#include "native/layer_raster.hpp"
#include <memory>
#include <stdexcept>
#include <string>

namespace endfield::native {
inline std::shared_ptr<const LayerRasterImage> rasterizeCentreBackdropRamp(LayerRasterizer& raster, LayerRasterOptions options, const std::string& id) {
    options.pixelsPerPoint = .5; options.paddingPoints = 0;
    auto ramp = modules::design::centreBackdropMask(id);
    ehud::data::Json::Array white;
    for (const auto& stop : ramp["gradient"]["colors"].array()) white.push_back(modules::design::color({1, 1, 1, stop["sRGB"].array()[3].number()}));
    ramp["gradient"]["colors"] = std::move(white);
    auto image = raster.rasterize(id, 1, ramp, options);
    if (!image || !image->complete() || !image->width || !image->height) throw std::runtime_error("The shared centre backdrop ramp did not rasterize");
    return image;
}
}
