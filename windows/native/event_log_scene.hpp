#pragma once
#include "modules/event_log_presentation.hpp"
#include "native/layer_scene.hpp"
namespace endfield::native {
#ifdef _WIN32
struct NativeEventLogPose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;};
struct EventLogSceneStats {std::size_t rows{},outgoingRows{},retiredParts{};std::uint64_t builds{},poses{},outlineRasters{};};
// Retained original header/toolbar and two bounded row pages. All finite
// animations consume the existing caller clock; no publisher/timer/service.
class NativeEventLogScene final {
public:
    NativeEventLogScene(modules::EventLogState&,LayerRasterizer&,LayerRasterOptions,modules::EventLogAppearance={});
    ~NativeEventLogScene();
    void setAppearance(modules::EventLogAppearance);
    bool syncContent(double);bool setFeedback(std::optional<std::string_view>,bool pressed,double);
    void updatePose(const NativeEventLogPose&);bool uploadAnimations(Renderer&);bool requiresFrames(double)const;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);EventLogSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
