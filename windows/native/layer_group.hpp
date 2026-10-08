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
class NativeLayerGroup final {
public:
    // sourceID is a diagnostic label (at most 450 bytes). Each adapter gets a
    // distinct retained identity so an outgoing and incoming same-kind menu
    // can overlap without replacing each other's targets.
    NativeLayerGroup(LayerScene& localScene,std::string sourceID,double pixelsPerPoint);
    ~NativeLayerGroup();
    NativeLayerGroup(const NativeLayerGroup&)=delete;
    NativeLayerGroup& operator=(const NativeLayerGroup&)=delete;
    // Content event. Includes actual local raster ink/shadow/AA padding, even
    // for zero-bounds source shapes. Optional coverage may expand that union
    // to reserve space for local animated motion; it may not crop the union.
    bool uploadResources(Renderer&,std::optional<core::Rect> coverage={});
    // Local feedback event/frame. Content changes require uploadResources;
    // this method accepts only the existing retained child resource bindings.
    bool updateLocal(Renderer&);
    // Caller supplies group-root transform/alpha and outer clipping. No child
    // placement or target repaint happens here. Storage stays fixed after load.
    void setPose(const core::Matrix4&,float opacity,std::span<const PlaneMask> masks={},
        std::optional<PlaneShutter> shutter={});
    LayerCompositionEntry entry();
    core::Rect localCoverageBounds()const;
    std::span<const DrawObject> draws()const noexcept{return output_;}
    // Remove the borrowed entry from the owner's composition before release
    // or destruction. False preserves still-published group/local resources.
    bool releaseResources(Renderer&);
private:
    LayerScene* local_{};LayerScene carrier_;std::string id_;double density_{};
    Renderer* renderer_{};std::uint64_t structureRevision_{},resourceRevision_{};
    bool registered_{};
    std::array<DrawObject,1> output_;DrawObject staged_;
};
} // namespace endfield::native
