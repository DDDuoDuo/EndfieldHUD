#pragma once
#include "native/renderer.hpp"
#include "native/layer_scene.hpp"
#include <memory>
namespace endfield::native {
struct ProjectionGridMesh {std::vector<Vertex>vertices;std::vector<std::uint32_t>indices;};
// Repeats the original36-point dot cell; final cells crop at the source window.
// This is geometry only. No filesystem, providers, fonts, timers or renderer.
ProjectionGridMesh projectionGridMesh(core::Point workspace);
ehud::data::Json projectionDotCell();
ehud::data::Json projectionCursor(double width,std::array<double,4>color,bool erasing);
#ifdef _WIN32
struct ProjectionPlaneStats {std::uint64_t geometryUpdates{},cursorRasters{},poseUpdates{};};
// Source black plate/dots and the small source brush ring. Media/strokes belong
// BETWEEN backgroundEntry() and cursorEntry(); toolbar/messages follow cursor.
// All resources use the borrowed application renderer and local rasterizer.
class NativeProjectionPlane final {
public:
    NativeProjectionPlane(LayerRasterizer&,LayerRasterOptions);~NativeProjectionPlane();
    bool resize(core::Point workspace,double pixelScale);
    // Owner supplies the original.18s background fade separately from panel
    // entrance/dismiss. Darkness changes never upload/rasterize the viewport.
    bool setBackground(double darkness,double backgroundOpacity);
    bool setCursor(std::optional<core::Point>,double width,std::array<double,4>color,bool erasing);
    // Source panel entrance/dismiss belongs to the host final composition;
    // pass opacity1 here while that single whole-window fade is active.
    bool setPose(const core::Matrix4&logicalToPhysical,float panelOpacity);
    bool upload(Renderer&);
    LayerCompositionEntry backgroundEntry();LayerCompositionEntry cursorEntry();
    bool releaseResources(Renderer&);ProjectionPlaneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
