#pragma once
#include "modules/notes_drawing.hpp"
#include <memory>
#include <span>
namespace endfield::native {
// A rectangle of one tile's physical pixels, tile-local.
struct ProjectionInkRegion {unsigned x{},y{},width{},height{};bool operator==(const ProjectionInkRegion&)const=default;};
// One tile whose pixels change. `whole`: paint the tile again from its
// complete content (new geometry: a resized workspace, a replaced live
// stroke). Otherwise only `region` changes - the pixels the added or removed
// stroke segments can reach; every other pixel keeps its value, so painting
// `region` from regionContent leaves the tile as a whole repaint would.
struct ProjectionInkTileChange {unsigned index{};bool live{},empty{};core::Rect bounds;ProjectionInkRegion region;bool whole{true};};
// Retains immutable completed stroke payloads and one capacity-reused live path.
// Only source document events compare paths. A pointer-only brush pose never
// enters this planner. Sparse256physical-pixel tiles, at most two viewports.
class ProjectionInkPlan final {
public:
    ProjectionInkPlan();~ProjectionInkPlan();
    bool update(std::span<const modules::DrawingStroke>completed,std::uint64_t revision,
        const modules::DrawingStroke*live,std::uint64_t liveRevision,core::Point workspace,double pixelScale);
    std::span<const ProjectionInkTileChange>changes()const noexcept;
    // Source round-cap/join paths clipped only by the exact physical tile.
    ehud::data::Json content(const ProjectionInkTileChange&,std::string sourceID)const;
    // The same source paths, in paint order, clipped to the change's region
    // only, with the region's top-left corner at the layer origin.
    ehud::data::Json regionContent(const ProjectionInkTileChange&,std::string sourceID)const;
    // A tile's physical pixel size (x and y are zero).
    ProjectionInkRegion tilePixels(unsigned index)const;
    core::Point workspace()const noexcept;double pixelScale()const noexcept;
    std::size_t completedTiles()const noexcept;std::size_t liveTiles()const noexcept;
    std::size_t maximumTilesPerPlane()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#ifdef _WIN32
class LayerRasterizer;class Renderer;struct LayerRasterOptions;struct LayerCompositionEntry;
// tileRasters: whole tiles painted; patchRasters/patchPixels: changed
// regions painted (a live stroke's newest segments, a committed or erased
// stroke) and their pixels; regionUploads: rectangles replaced in place in
// the retained tile textures. updateSeconds: CPU time inside update().
struct ProjectionInkStats {std::uint64_t tileRasters{},rasterPixelBytes{},poses{};std::size_t completedTiles{},liveTiles{},retiredTiles{};double updateSeconds{};
    std::uint64_t patchRasters{},patchPixels{},regionUploads{};};
// Each tile is one retained, updatable texture painted in place: a stroke
// that grows by a few segments paints only the pixels those segments reach,
// and a committed stroke only its own footprint, instead of rasterizing and
// re-creating whole tiles (the live stroke's tiles used to be painted again
// in full for every pointer event, with every segment of the stroke).
class NativeProjectionInk final {
public:
    NativeProjectionInk(LayerRasterizer&,LayerRasterOptions);~NativeProjectionInk();
    bool update(std::span<const modules::DrawingStroke>,std::uint64_t,
        const modules::DrawingStroke*,std::uint64_t,core::Point,double pixelScale);
    // Original Projection is a flat physical-pixel-aligned workspace: only
    // uniform logical-to-physical DPI scaling and integral pixel translation.
    // General/fractional transforms require tile gutters and are rejected.
    // Keep opacity1 for source panel fades: the host fades the final composited
    // window once, not overlapping completed/live tiles independently.
    bool setPose(const core::Matrix4&,float opacity);
    // Installs new tile textures and replaces the painted regions in place.
    // Call before publishing entries() (their draws refer to these textures).
    bool upload(Renderer&);
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    ProjectionInkStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
