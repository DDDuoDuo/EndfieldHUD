#pragma once
#include "modules/work_mode_presentation.hpp"
#include "native/layer_group.hpp"
namespace endfield::native {
#ifdef _WIN32
struct NativeWorkModePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;};
struct WorkModeSceneStats {
    std::uint64_t builds{},clockRasters{},poses{},parks{};std::size_t retiredParts{};
    std::size_t parts{},pixelBytes{}; // built parts and their raster pixels
    bool gpuResident{};               // the ring texture or a group target is on the renderer
};
// Retained original ring/labels/control artwork; one local GPU configuration
// group preserves its source fade. Clock ticks replace only the glyph surface.
// Ring/feedback/layout frames change numeric GPU constants only. No publisher,
// timer/thread, controller ownership, Focus API or persistence service exists.
class NativeWorkModeScene final {
public:
    NativeWorkModeScene(modules::WorkModePresentation&,LayerRasterizer&,LayerRasterOptions,modules::WorkModeAppearance={});
    ~NativeWorkModeScene();
    void setAppearance(modules::WorkModeAppearance);
    bool syncContent(double);bool setFeedback(std::optional<std::string_view>,bool pressed,double);
    void updatePose(const NativeWorkModePose&);bool uploadAnimations(Renderer&);bool requiresFrames(double)const;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);WorkModeSceneStats stats()const noexcept;
    // HUDModuleContent drops a screen's layers once it is no longer shown: a
    // page that is not composed releases its GPU resources (the ring texture
    // and the configuration/focus group targets); the parts keep their
    // rasters, so the next uploadAnimations only uploads. True when something
    // was released; false when nothing is on the renderer or a resource is
    // still published (then everything stays). Never throws.
    bool park(Renderer&)noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
