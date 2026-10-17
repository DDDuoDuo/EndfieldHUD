#pragma once
#include "modules/notes_drawing.hpp"
#include "modules/notes_drawing_ink.hpp"
#include "native/layer_scene.hpp"
#include <array>

namespace endfield::native {
// wholeRasters: the whole picture painted (the first paint, a new card size,
// a lost texture, or most of the picture changed at once); regionRasters and
// regionPixels: changed regions painted (a growing live stroke's newest
// segments, an erased or cancelled stroke's footprint) and their pixels;
// clearedRegions: changed regions no ink reaches any more (no raster);
// regionUploads: rectangles replaced in place in the retained texture;
// textureCreations: the picture's texture created (a new card size);
// paints: upload() calls that painted; paintSeconds: their CPU time.
struct NativeNotesDrawingStats {
    std::uint64_t wholeRasters{},regionRasters{},regionPixels{},clearedRegions{},regionUploads{},textureCreations{},paints{};
    std::uint64_t brushRasters{},poseUpdates{};
    double paintSeconds{};
    modules::NotesInkPlanStats plan;
};
// One drawing card's ink and brush ring, appended immediately after the
// owning card. The ink - completed source strokes in order with the live
// stroke on top, as the source NotesCanvas layers them - is ONE retained,
// updatable texture over the drawing viewport (modules/notes_drawing_ink.hpp).
// setCompleted/setLive only plan which pixels change (every pointer sample);
// upload() paints those regions in place once per frame, so a stroke follows
// the pointer without rasterizing it whole for each sample, a committed stroke
// is already painted, and an erased one repaints its own footprint only.
// Pointer/tilt changes only numeric poses. Caller owns the gesture,
// persistence, renderer publication and retirement.
class NativeNotesDrawingScene final {
public:
    NativeNotesDrawingScene(LayerRasterizer&,LayerRasterOptions);
    ~NativeNotesDrawingScene();
    NativeNotesDrawingScene(const NativeNotesDrawingScene&)=delete;
    NativeNotesDrawingScene&operator=(const NativeNotesDrawingScene&)=delete;
    bool setCompleted(const modules::NotesDrawing&,std::uint64_t revision,core::Point cardSize);
    bool setLive(const modules::DrawingStroke*,std::uint64_t revision);
    // Source system-red is supplied by the owner from the detached Mac fixture;
    // normal preview uses the same primary color as the owning card.
    bool setBrush(std::optional<core::Point> cardLocal,double width,std::array<double,4> color);
    bool updatePose(const core::Matrix4& cardToScreen,float opacity);
    // Paints every change planned since the previous upload into the retained
    // texture (creating it at a new size). Call before publishing entries():
    // the ink's draw joins entries() once its texture is resident, and then
    // true is returned (the owner republishes its entry list).
    bool upload(Renderer&);
    std::span<const LayerCompositionEntry> entries()const noexcept;
    bool releaseResources(Renderer&); // only after composition detaches
    NativeNotesDrawingStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
