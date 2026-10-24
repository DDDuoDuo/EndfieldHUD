#pragma once
#include "modules/notes_presentation.hpp"
#include "modules/notes_media_presentation.hpp"
#include "native/layer_scene.hpp"
#include <memory>

namespace endfield::native {
// The source draws a note at any size. A large card (or its editor viewport)
// keeps its geometry and rasters its surfaces at a lower density, so that one
// surface stays within the shared rasterizer's pixel budget
// (LayerRasterizer::maximumPixels) and 8192-pixel sides. Unchanged otherwise.
LayerRasterOptions notesRasterOptions(LayerRasterOptions,double width,double height)noexcept;
struct NativeNotesSceneStats {
    std::uint64_t contentUpdates{},placementUpdates{};
    std::uint64_t frameLoads{}; // large-card plate/header scene loads
    std::uint64_t lineLoads{};  // large text card body-line scene loads
};
struct NativeNotesExternalEditorAppearance {
    modules::NotesColor background,border;
    // HUDNotesInteraction.layoutEditor (Mac 1.3.0): opaque HUDColor.field
    // #1D1D1D / plateHigh #E4E5E0, the theme yellow border, radius 0.
    // Caller supplies appearance explicitly rather than guessing it from text.
};
struct NativeNotesExternalEditorSlot {
    core::Rect localRect; // relative to card, NOT the center module
    double cornerRadius{},borderWidth{1};
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
    // A card beyond LayerScene's local group area (1024x1024 points) keeps its
    // plate and header in this separate scene, composed immediately BEFORE
    // scene(); null for ordinary cards. It reloads only when its own artwork
    // changes (size, palette, pin state, title, typography), never for
    // selection, editing entry/exit, scrolling or text edits: the plate's
    // CALayer border is a GPU ring mesh among frameAfterDraws().
    LayerScene* frameScene()noexcept;
    const LayerScene* frameScene()const noexcept;
    // A large TEXT card's body lines (its visible "content/line/N" leaves) in
    // their own scene in document coordinates, composed between frameScene()
    // and scene(); null otherwise. It reloads only when those lines' own
    // content changes (text, formatting, width, palette, the visible window,
    // typography). While the card's field is open the lines stay loaded and
    // published at zero opacity (the opaque field covers them), so closing the
    // field over unchanged text, and every other body change, rasters no line
    // again. Placement adds the viewport origin, the scroll offset and the
    // card's and viewport's clips.
    LayerScene* linesScene()noexcept;
    const LayerScene* linesScene()const noexcept;
    // Its LayerCompositionEntry.after: [the card's border ring (once
    // uploadFrameBorder made it resident), while its field is open the opaque
    // field backing as one tinted quad on the indicator's unit mesh (once
    // uploadEditorIndicator made that resident)]. The ring is one antialiased
    // mesh of the source border (accent and 1.1 points when selected): a
    // selection retints and reshapes it, never rasters the plate.
    std::span<const DrawObject> frameAfterDraws()const noexcept;
    // A large card's plate (its CALayer background) is one filled, antialiased
    // GPU mesh instead of a card-sized bitmap: plateAfterDraws() is the only
    // draw of the empty plateScene(), composed immediately BEFORE frameScene()
    // (null/empty for ordinary cards; empty until uploadPlate made the mesh
    // resident). A resize reshapes it; nothing is rastered for it. Released
    // like the ring.
    LayerScene* plateScene()noexcept;
    std::span<const DrawObject> plateAfterDraws()const noexcept;
    bool uploadPlate(Renderer&); // true when plateAfterDraws() grew
    // Uploads (or replaces, after a size or border change) the ring mesh;
    // true when frameAfterDraws() grew. Released by releaseResources(), or by
    // collectRetired() once the card no longer needs it.
    bool uploadFrameBorder(Renderer&);
    NativeNotesSceneStats stats()const noexcept;
    // Releases every scene's resources once unpublished (false: still referenced).
    bool releaseResources(Renderer&);
    // After publication: a frame or lines scene left behind by a shrunken card
    // releases its resident surfaces (false while they are still published).
    bool collectRetired(Renderer&);
    // Explicit external-editor mode only. Compose this stable card scene, then
    // the caller's editor scene with this span in LayerCompositionEntry.after.
    // The final border borrows this card scene's already resident resources;
    // keep the card present in the SAME composition until the editor is removed.
    // Header/format feedback and grip are disjoint from the supported viewport.
    // All content uses ONE transactional LayerScene load, never two partial
    // scene replacements. Draw identity/count changes need setEntries().
    // With the editor indicator resident: [scroll indicator, border]; before
    // uploadEditorIndicator, only the border. A large card's field border is a
    // GPU ring mesh instead of a slot-sized bitmap: it and the indicator are
    // published together once uploadEditorIndicator made both resident.
    std::span<const DrawObject> externalEditorAfterDraws()const noexcept;
    // Source HUDProjectedTextEditor scrollIndicator: card-local thumb of the
    // open external editor, or nullopt without overflow. Numeric only (no
    // raster); painted after the editor glyphs and before its border. The one
    // reusable quad is uploaded explicitly and retired after unpublication.
    bool setEditorScrollIndicator(std::optional<core::Rect> thumb);
    bool uploadEditorIndicator(Renderer&); // true when the after-draw list grew
    bool releaseEditorIndicator(Renderer&);
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
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
