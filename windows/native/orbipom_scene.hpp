#pragma once
#ifdef _WIN32
#include "modules/orbipom_presentation.hpp"
#include "native/layer_scene.hpp"
namespace endfield::native {
struct OrbiPomScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>masks;const PlaneShutter*shutter{};};
struct OrbiPomSceneStats {std::uint64_t contentBuilds{},localUpdates{},spriteUploads{},ringRasters{},poses{};std::size_t bodies{},surfaces{};};
class NativeOrbiPomScene final {
public:
    NativeOrbiPomScene(modules::OrbiPomSession&,modules::OrbiPomState&,LayerRasterizer&,LayerRasterOptions,modules::OrbiPomAppearance={});
    ~NativeOrbiPomScene();
    bool setAppearance(modules::OrbiPomAppearance);
    // Current snapshot is borrowed, never cloned. Only discrete text/control
    // changes make descriptors; bodies keep source insertion order and textures.
    bool syncContent(double time);
    bool setFeedback(std::optional<core::Point>,bool pressed,double time);
    bool scrollRules(double delta,double time);
    core::Rect rulesBounds()const noexcept;double rulesScroll()const noexcept;
    bool updatePose(const OrbiPomScenePose&);
    void uploadResources(Renderer&);bool collectRetired(Renderer&);
    std::span<const LayerCompositionEntry>entries();
    bool requiresFrames(double time)const;void settle();
    bool releaseResources(Renderer&);OrbiPomSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
