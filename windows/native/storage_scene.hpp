#pragma once
#include "modules/storage_presentation.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"
namespace endfield::native {
struct NativeStorageSceneStats {std::uint64_t structures{},localUpdates{},poses{},feedbackChanges{};};
// One retained original Storage artwork tree. Borrowed presentation/rasterizer
// outlive this adapter. Pointer, source refresh rotation and module placement
// update only numeric draw data, with no private timer or GPU publisher.
class NativeStorageScene final {
public:
    NativeStorageScene(modules::StoragePresentation&,LayerRasterizer&,
        LayerRasterOptions,modules::StorageSourcePaths);
    ~NativeStorageScene();
    bool syncContent(double time);
    void setActivity(bool active,bool reduceMotion,double time);
    void manualRefreshTurn(double time); // call before submitting source refresh
    bool setFeedback(std::optional<core::Point>,bool pressed,double time);
    void updatePose(const core::Matrix4&,float opacity,double time,
        std::span<const PlaneMask> masks={},const PlaneShutter* shutter=nullptr);
    bool requiresFrames(double time)const;
    LayerScene&scene()noexcept;
    NativeStorageSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
