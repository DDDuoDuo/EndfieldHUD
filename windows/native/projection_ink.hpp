#pragma once
#include "modules/notes_drawing.hpp"
#include <memory>
#include <span>
namespace endfield::native {
struct ProjectionInkTileChange {unsigned index{};bool live{},empty{};core::Rect bounds;};
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
    core::Point workspace()const noexcept;double pixelScale()const noexcept;
    std::size_t completedTiles()const noexcept;std::size_t liveTiles()const noexcept;
    std::size_t maximumTilesPerPlane()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#ifdef _WIN32
class LayerRasterizer;class Renderer;struct LayerRasterOptions;struct LayerCompositionEntry;
struct ProjectionInkStats {std::uint64_t tileRasters{},rasterPixelBytes{},poses{};std::size_t completedTiles{},liveTiles{},retiredTiles{};};
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
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    ProjectionInkStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
