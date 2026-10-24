#pragma once
#include "native/map_raster.hpp"
#include "core/data/json.hpp"
#include "core/map_source_cutout.hpp"
#include "core/motion.hpp"
#include <string>

namespace endfield::native {
// The surface's pixels of the redesigned Map (Sources/WorldMapSurface.swift
// layers): the well (#1F1F1F at .89), the retained raster (backdrop wraps and
// the padded detail), the header scrim and v1.2.0's pins, in the raster's
// unit space (v1.2.0's 440-unit viewport, one unit = MapSurfaceGeometry::
// unitScale box px) over the backdrop disc's square, faded by the shared
// centre backdrop ramp (modules/hud_design_nodes.hpp centreBackdropMask, the
// mask Personal ID's art uses: rasterized once at the Mac's half resolution).
// The canvas chrome (modules/map_canvas) is a design scene above it.
struct MapAppearance {
    bool reducedMotion{},ambient{true};
};
struct MapPlayerImages {std::shared_ptr<const MapPaintImage>halo,beam,glyph;};
// The disc square in units (centre 220): 860 box px at unitScale.
inline constexpr double mapDiscUnits=modules::MapSurfaceGeometry::visibleRadius/modules::MapSurfaceGeometry::unitScale;
inline constexpr core::Rect mapSceneBounds{220-mapDiscUnits,220-mapDiscUnits,2*mapDiscUnits,2*mapDiscUnits};
// The host cut: the source plates above the Map as an alpha mask of the
// static content-box plane (the box world without the switch motion, which
// moves the module content but not the host), over the map square, at
// v1.2.0's two texels per design unit (its plate edges).
inline constexpr double mapHostCutScale=2*core::DesignBox::baseUnit; // texels per box px
std::uint32_t mapHostCutSide()noexcept;                                // texels per side
core::Rect mapHostCutBounds()noexcept;                                 // box px
#ifdef _WIN32
class LayerRasterizer;class LayerScene;class Renderer;struct LayerRasterOptions;struct LayerCompositionEntry;struct PlaneMask;struct PlaneAlphaMask;
// world: scene units -> screen (the canvas's surface layer world x units).
struct MapScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>masks;};
// hostCutUploads: creations and in-place region updates of the host cut;
// hostCutBytes: its GPU bytes resident now (an A8 alpha mask; 0 when released). groupPixels: the
// retained group target's pixels at the current density.
struct MapSceneStats {std::uint64_t imageUploads{},pinBuilds{},poses{},localUpdates{},hostCutUploads{},densityChanges{};std::size_t visiblePins{},pulsingPins{},hostCutBytes{},cutoutMaskBytes{},groupPixels{};};
class NativeMapScene final {
public:
    NativeMapScene(modules::MapState&,NativeMapRaster&,LayerRasterizer&,LayerRasterOptions,MapPlayerImages,MapAppearance={});
    ~NativeMapScene();
    bool setAppearance(MapAppearance);
    // Physical px per box px of the retained group (the design density,
    // 1...1.5); true when changed.
    bool setDensity(double boxDensity);
    double density()const noexcept;
    // Each visible marker owns a bounded48x48 or48x132 local encoded group;
    // screen blending and .16s rotation/group opacity precede Map composition.
    // State/raster callbacks only. Animated camera transitions use the actual
    // current numeric pose as interruption origin. No IO/worker is performed.
    bool syncContent(double time);
    bool updatePose(const MapScenePose&);bool uploadResources(Renderer&);
    // SystemHUDView.applySourceMapOcclusion masks the whole module host while
    // the Map is presented, at rest and during its switches (the redesigned
    // map overflows the host, so the kept region spans the map square): the
    // plates as polygons of the static box plane (box px), which switch
    // motion does not move. While `wanted` uploadResources keeps an updatable alpha
    // texture over mapHostCutBounds (alpha 1 - even-odd plate coverage, no
    // feather) in step, repainting in place only the texels that change. It
    // is created by the first presentation with plates and kept until
    // releaseHostCut (false while a published draw still samples it) or
    // releaseResources. hostCutTexture() names it while it holds the wanted
    // plates (after uploadResources), otherwise null.
    bool setHostCut(const modules::MapCutouts& boxPlates,bool wanted);
    const std::string*hostCutTexture()const noexcept;
    bool releaseHostCut(Renderer&);
    // Finite motion only - camera moves and marker style transitions - which
    // the host presents at the display rate. The pin pulse is ambient motion
    // (the Mac runs it as a repeating compositor animation): pulsing()
    // reports it, and it is sampled at whatever frame presents it, on the
    // same clock, so its curve is unchanged.
    bool requiresFrames(double time)const;void settle();
    bool pulsing()const noexcept;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool releaseResources(Renderer&);MapSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
