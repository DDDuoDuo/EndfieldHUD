#pragma once
#include "modules/hypergryph_account_canvas.hpp"
#ifdef _WIN32
#include "native/layer_group.hpp"
#include "native/layer_scene.hpp"

// Retained renderer for the Account module face and its shaped menus on the
// shared LayerScene/LayerRasterizer. The source leaves are placed in canvas
// space inside one retained native group that blends in encoded sRGB, as the
// Mac Core Animation layer tree does (translucent plates, menu rows and
// feedback tints otherwise brighten under linear-light blending). Content
// events (presentation, language, style, menu open/close) re-plan and upload
// the group; pointer feedback, menu fade/slide (0.16 s open, 0.14 s close)
// and row scrolling (0.08 s) only redraw the retained group target; module
// poses (tilt, fade, owner clips) change only the group's output constants.
// Reduce Motion makes every transition immediate. No timer, worker, provider
// or second renderer is created.
namespace endfield::native {
struct NativeAccountSceneStats {
    std::uint64_t structureUpdates{},localUpdates{},poseUpdates{},feedbackChanges{};
    std::uint64_t groupUploads{},groupRedraws{}; // content uploads / local target redraws
};
class NativeAccountCanvasScene final {
public:
    NativeAccountCanvasScene(modules::hypergryph::AccountCanvasModel&,LayerRasterizer&,LayerRasterOptions,modules::hypergryph::CanvasStyle={});
    ~NativeAccountCanvasScene();
    NativeAccountCanvasScene(const NativeAccountCanvasScene&)=delete;
    NativeAccountCanvasScene& operator=(const NativeAccountCanvasScene&)=delete;
    bool setStyle(modules::hypergryph::CanvasStyle);
    // Re-plans after a model content/menu revision; starts menu animations.
    bool syncContent(double time,bool reduceMotion);
    bool setFeedback(std::optional<core::Point>,bool pressed,bool reduceMotion,double time);
    bool updatePose(const core::Matrix4& contentWorld,float opacity,double time,
        std::span<const PlaneMask> ownerMasks={},const PlaneShutter* ownerShutter=nullptr);
    bool requiresFrames(double time) const;
    // Content events upload the group; local motion redraws its retained
    // target; otherwise this only refreshes the output pose (no allocation).
    bool uploadResources(Renderer&);
    // The group's carrier entry once uploaded (empty before the first upload).
    std::optional<LayerCompositionEntry> entry();
    // False while the entry is still published in a composition.
    bool releaseResources(Renderer&);
    const LayerScene& localScene() const noexcept;
    NativeAccountSceneStats stats() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
