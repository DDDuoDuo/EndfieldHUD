#pragma once
#include "modules/work_mode_presentation.hpp"
#include "native/layer_group.hpp"
namespace endfield::native {
#ifdef _WIN32
struct NativeWorkModePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;};
struct WorkModeSceneStats {std::uint64_t builds{},clockRasters{},poses{};std::size_t retiredParts{};};
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
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
