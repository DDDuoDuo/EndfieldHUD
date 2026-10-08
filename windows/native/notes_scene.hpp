#pragma once
#include "modules/notes_presentation.hpp"
#include "native/layer_scene.hpp"
#include <memory>

namespace endfield::native {
struct NativeNotesSceneStats {
    std::uint64_t contentUpdates{},placementUpdates{},feedbackChanges{};
};
// Retained native artwork for one source-authored plain-text workspace card.
// The caller owns NotesState/NotesCardPresentation, one LayerComposition and
// its existing Renderer/clock. Nothing here publishes a draw list, creates a
// device/window/editor, measures text, reads data or schedules frames.
// Explicit measured short-fixture text comes from NotesCardPresentation;
// rich/TODO/media/drawing and actual editor glyphs are not silently flattened.
// Borrowed presentation/rasterizer outlive this adapter; remove scene() from its
// composition before destroying it. Its stable scene/resource IDs let sibling
// cards retain their textures when one card changes or is removed.
class NativeNotesCardScene final {
public:
    NativeNotesCardScene(modules::NotesCardPresentation&,LayerRasterizer&,LayerRasterOptions);
    ~NativeNotesCardScene();
    NativeNotesCardScene(const NativeNotesCardScene&)=delete;
    NativeNotesCardScene&operator=(const NativeNotesCardScene&)=delete;
    // Content event only. true means caller must replace/upload its combined
    // list before presenting; no resource is retired while still referenced.
    bool syncContent();
    // Source feedback targets use the caller's current monotonic time and the
    // exact .06 pressed/.14 hover easeOut. No JSON/raster/resource update here.
    bool setFeedback(std::optional<std::string_view> verb,bool pressed,bool reduceMotion,double time);
    // The matrix maps saved workspace logical points to owner logical points.
    // The caller's final renderer camera applies physical DPI exactly once.
    // Ordinary pointer/closing/move samples allocate nothing after syncContent.
    bool updatePose(const core::Matrix4& workspaceToScreen,float canvasOpacity,double time);
    bool requiresFrames(double time)const;
    LayerScene& scene()noexcept;
    const LayerScene& scene()const noexcept;
    NativeNotesSceneStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
