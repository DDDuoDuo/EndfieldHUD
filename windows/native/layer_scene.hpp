#pragma once
#include "core/data/json.hpp"
#include "native/layer_raster.hpp"
#include "native/renderer.hpp"
#include <optional>
#include <string_view>

namespace endfield::native {
struct LayerSceneReport {
    std::size_t sourceNodes{},surfaces{},pixelBytes{};
    std::vector<LayerRasterIssue> unsupported;
    std::vector<LayerFontSubstitution> fontSubstitutions;
};
struct LayerPlacement {
    std::size_t surface{};
    core::Matrix4 world;
    float opacity{1};
    std::span<const PlaneMask> masks;
};
// Model-layer bridge above the original material pass. Local content is cached
// once; the exported projective placement stays in GPU object matrices. This
// does not install an editor, a timer, a screen capture or a second renderer.
class LayerScene final {
public:
    explicit LayerScene(LayerRasterizer& rasterizer);
    ~LayerScene();
    LayerScene(const LayerScene&)=delete;
    LayerScene& operator=(const LayerScene&)=delete;
    void load(const ehud::data::Json& sourceRoot,const LayerRasterOptions&);
    void upload(Renderer&);
    // This scene owns the native draw list. Detach before replacing that list
    // with another owner; no original-material resources are removed.
    void detach(Renderer&);
    // Resolve source IDs once when a binding plan changes, then supply numeric
    // indices on the animation path. Content and local raster bounds stay fixed.
    std::optional<std::size_t> surfaceIndex(std::string_view sourceID) const noexcept;
    void setPlacements(std::span<const LayerPlacement>);
    // Reuses all local surfaces. Caller supplies source-derived placement,
    // including a changed camera or module transition, in top-left screen space.
    void present(Renderer&,const core::Matrix4& screenTransform={});
    const LayerSceneReport& report() const noexcept {return report_;}
    std::uint64_t contentRevision() const noexcept {return revision_;}
    std::span<const DrawObject> draws() const noexcept {return draws_;}
private:
    struct Surface {std::string id;std::shared_ptr<const LayerRasterImage> image;DrawObject draw;};
    LayerRasterizer* rasterizer_;
    std::vector<Surface> surfaces_;
    std::vector<DrawObject> draws_;
    LayerSceneReport report_;
    std::string namespace_;
    std::vector<std::string> rasterIDs_;
    std::vector<std::string> uploaded_;
    std::uint64_t revision_{},attempt_{};
    void append(const ehud::data::Json&,const core::Matrix4&,float,
                const std::vector<PlaneMask>&,const LayerRasterOptions&,unsigned);
};
// The source native layers already project into top-left viewport points.
// This final matrix changes only those coordinates to D3D's clip space.
core::Matrix4 layerViewportProjection(unsigned width,unsigned height);
} // namespace endfield::native
