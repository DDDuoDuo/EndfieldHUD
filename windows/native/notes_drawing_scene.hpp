#pragma once
#include "modules/notes_drawing.hpp"
#include "native/layer_scene.hpp"
#include <array>

namespace endfield::native {
struct NativeNotesDrawingStats {
    std::uint64_t completedRasters{},liveRasters{},brushRasters{},poseUpdates{};
};
// Three retained local surfaces, appended immediately after the owning card.
// Completed source strokes are rebuilt only on document/size changes; an active
// stroke never visits that document. Pointer/tilt changes only numeric poses.
// Caller owns the gesture, persistence, renderer publication and retirement.
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
    std::span<const LayerCompositionEntry> entries()const noexcept;
    bool releaseResources(Renderer&); // only after composition detaches
    NativeNotesDrawingStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
