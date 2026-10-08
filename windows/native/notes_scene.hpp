#pragma once
#include "modules/notes_presentation.hpp"
#include "modules/notes_media_presentation.hpp"
#include "native/layer_scene.hpp"
#include <memory>

namespace endfield::native {
struct NativeNotesSceneStats {
    std::uint64_t contentUpdates{},placementUpdates{},feedbackChanges{};
};
struct NativeNotesExternalEditorAppearance {
    modules::NotesColor background,border;
    // Source HUDNotesInteraction: opaque .12 dark / .96 light; accent border.
    // Caller supplies appearance explicitly rather than guessing it from text.
};
struct NativeNotesExternalEditorSlot {
    core::Rect localRect; // relative to card, NOT the center module
    double cornerRadius{3},borderWidth{1};
    // Actual editor glyph/selection clip must honor this radius. The card
    // backing alone does not establish rounded editor-clip parity.
};
struct NativeNotesMediaSlot { core::Rect content; bool hasProgress{}; };
// Retained native artwork for one source-authored text, checklist or media card.
// The caller owns NotesState/NotesCardPresentation, one LayerComposition and
// its existing Renderer/clock. Nothing here publishes a draw list, creates a
// device/window/editor, measures text, reads data or schedules frames.
// Explicit measured short-fixture text comes from NotesCardPresentation;
// Rich runs/checklist rows retain their descriptors; media/drawing are not flattened.
// Borrowed presentation/rasterizer outlive this adapter; remove scene() from its
// composition before destroying it. Its stable scene/resource IDs let sibling
// cards retain their textures when one card changes or is removed.
class NativeNotesCardScene final {
public:
    NativeNotesCardScene(modules::NotesCardPresentation&,LayerRasterizer&,LayerRasterOptions,
        std::optional<NativeNotesExternalEditorAppearance> externalEditor={});
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
    // A sampled outgoing section/card transition can explicitly remain visible
    // while NotesState already targets hidden. No override follows target state.
    bool updatePose(const core::Matrix4& workspaceToScreen,float canvasOpacity,double time,
        std::optional<bool> visibilityOverride={});
    bool requiresFrames(double time)const;
    LayerScene& scene()noexcept;
    const LayerScene& scene()const noexcept;
    // Explicit external-editor mode only. Compose this stable card scene, then
    // the caller's editor scene with this span in LayerCompositionEntry.after.
    // The final border borrows this card scene's already resident resources;
    // keep the card present in the SAME composition until the editor is removed.
    // Header/format feedback and grip are disjoint from the supported viewport.
    // All content uses ONE transactional LayerScene load, never two partial
    // scene replacements. Draw identity/count changes need setEntries().
    std::span<const DrawObject> externalEditorAfterDraws()const noexcept;
    const std::optional<NativeNotesExternalEditorSlot>& externalEditorSlot()const noexcept;
    // The media owner retains the resident texture; this card owns only one
    // reusable quad mesh. Append mediaDraws() after scene() in the SAME publisher.
    // Content/progress changes update identity/numeric pose, never decode/raster.
    void setMediaTexture(std::string borrowedTextureID,unsigned pixelWidth,unsigned pixelHeight);
    void setMediaProgress(modules::NotesMediaProgressPose);
    void uploadMedia(Renderer&);
    std::span<const DrawObject> mediaDraws()const noexcept;
    const std::optional<NativeNotesMediaSlot>& mediaSlot()const noexcept;
    bool releaseMedia(Renderer&); // after removal from shared composition
    NativeNotesSceneStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
