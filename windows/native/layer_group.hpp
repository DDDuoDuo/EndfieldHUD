#pragma once
#include "native/layer_scene.hpp"

namespace endfield::native {
// One bounded native GPU group above a borrowed, locally placed scene. This
// adapter never publishes a renderer draw list. Its empty carrier and single
// supplemental output enter the owner's existing LayerComposition, preserving
// sibling paint order. The local scene/rasterizer/Renderer must outlive it.
//
// Root fade and tilt change only output constants. Child feedback redraws the
// retained GPU target only when local constants/content changed; no CPU raster,
// target readback, second device, window or clock is introduced.
//
// Borrowed mode (group opacity of the design library, native/hud_design_scene.cpp:
// CALayer opacity composites a layer's subtree as one image). A pooled group
// constructed without a scene draws a scene another owner has uploaded, and
// composes directly at rest, through its own target only while it fades:
// attach() (start of the fade: the target is allocated), publish entry() in
// place of the scene (the composition that drops the scene leaves its
// resources to the group), feedback and pose per frame, detach() (end of the
// fade: publish the scene again), then releaseTarget() once no published list
// holds the output. The scene's resources are never released by the group.
struct NativeGroupInsertion {
    // Exact paint slot in the borrowed local scene. Bounds include all supplied
    // mesh ink in the same local plane; the caller owns resources through group
    // retirement. Renderer validation rejects absent/removed resource IDs.
    std::size_t before{};std::span<const DrawObject> draws;core::Rect bounds;
};
class NativeLayerGroup final {
public:
    // sourceID is a diagnostic label (at most 450 bytes). Each adapter gets a
    // distinct retained identity so an outgoing and incoming same-kind menu
    // can overlap without replacing each other's targets.
    NativeLayerGroup(LayerScene& localScene,std::string sourceID,double pixelsPerPoint,
        NativeGroupColorSpace colorSpace=NativeGroupColorSpace::linear);
    // Borrowed mode: no scene until attach().
    NativeLayerGroup(LayerRasterizer&,std::string sourceID,NativeGroupColorSpace colorSpace=NativeGroupColorSpace::linear);
    ~NativeLayerGroup();
    NativeLayerGroup(const NativeLayerGroup&)=delete;
    NativeLayerGroup& operator=(const NativeLayerGroup&)=delete;
    // Content event. Includes actual local raster ink/shadow/AA padding, even
    // for zero-bounds source shapes. Optional coverage may expand that union
    // to reserve space for local animated motion; it may not crop the union.
    bool uploadResources(Renderer&,std::optional<core::Rect> coverage={},
        std::optional<NativeGroupInsertion> insertion={});
    // Local feedback event/frame. Content changes require uploadResources;
    // this method accepts only the existing retained child resource bindings.
    // Borrowed mode configures again instead when the scene's resources changed
    // (a lazy surface painted) or its local ink left the target (the target
    // grows to the union for the rest of the fade).
    bool updateLocal(Renderer&,std::optional<NativeGroupInsertion> insertion={});
    // Caller supplies group-root transform/alpha and outer clipping. No child
    // placement or target repaint happens here. Storage stays fixed after load.
    void setPose(const core::Matrix4&,float opacity,std::span<const PlaneMask> masks={},
        std::optional<PlaneShutter> shutter={});
    // Explicit default-font event, including borrowed local text surfaces.
    // Reuses the last accepted bounded coverage/insertion and output identity.
    bool refreshTypography();
    LayerCompositionEntry entry();
    core::Rect localCoverageBounds()const;
    std::span<const DrawObject> draws()const noexcept{return output_;}
    // Remove the borrowed entry from the owner's composition before release
    // or destruction. False preserves still-published group/local resources.
    // Borrowed mode: the target only (releaseTarget).
    bool releaseResources(Renderer&);
    // Borrowed mode. attach: the scene is uploaded on `renderer` (or not yet
    // uploaded) and in no group; this group holds no scene and no target. The
    // ink of the scene's current placements (2D, local plane) must fit
    // Renderer's group limits; a refusal (std::invalid_argument) leaves the
    // scene and this group as they were. detach: the scene leaves the group at
    // once (no GPU work); the target stays until releaseTarget, which is false
    // while the output is still published.
    void attach(Renderer&,LayerScene& uploadedScene,double pixelsPerPoint,std::optional<core::Rect> coverage={});
    void detach()noexcept;
    bool releaseTarget(Renderer&);
    bool attached()const noexcept{return local_!=nullptr;}
    bool holdsTarget()const noexcept{return registered_;}
    bool published()const noexcept;
private:
    LayerScene* local_{};LayerScene carrier_;std::string id_;double density_{};
    NativeGroupColorSpace colorSpace_{NativeGroupColorSpace::linear};
    Renderer* renderer_{};std::uint64_t structureRevision_{},resourceRevision_{};
    bool registered_{},borrowed_{};core::Rect bounds_{};
    std::optional<core::Rect> retainedCoverage_;std::optional<NativeGroupInsertion> retainedInsertion_;
    std::array<DrawObject,1> output_;DrawObject staged_;
    std::vector<DrawObject> children_;std::size_t insertedCount_{},insertionIndex_{};
    std::span<const DrawObject> prepareChildren(std::optional<NativeGroupInsertion>,bool installing);
};
} // namespace endfield::native
