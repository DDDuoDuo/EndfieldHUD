#pragma once
#include "modules/shelf_presentation.hpp"
#include "native/layer_scene.hpp"

namespace endfield::native {
// Explicit source-prepared artwork or caller-supplied native icon snapshots. This bridge never obtains file icons,
// resolves locators, reads the shelf's files, or substitutes an arbitrary image.
struct NativeShelfImage {
    modules::ShelfPresentationImage dependency;
    ehud::data::Json contents; // confined {asset,sha256}, or native icon {memoryImage,revision}
};
struct ShelfSceneSurface {
    static constexpr std::size_t none=static_cast<std::size_t>(-1);
    std::string id;core::Matrix4 local;float opacity{1};
    std::size_t feedback{none};bool rim{},toolbar{};
};
struct ShelfScenePart {
    std::string itemID; // empty for collection, scrollbar and foreground
    ehud::data::Json layers;
    std::vector<ShelfSceneSurface> surfaces;
    std::vector<modules::ShelfPresentationFeedback> feedback;
};
struct ShelfScenePlan {
    ShelfScenePart collection,scrollbar,foreground;
    std::vector<ShelfScenePart> cards; // original visible collection order, at most eight
};
// Pure content-event compiler. Empty model bounds do not erase CAShapeLayer
// path/stroke ink. Leaves retain original coordinates and paint order; only
// root placement and feedback opacity become numeric retained surface data.
ShelfScenePlan prepareShelfScene(const modules::ShelfPresentation&,
    std::span<const NativeShelfImage> images={});

struct ShelfSelectionPose {std::string_view itemID;double y{},z{};};
struct NativeShelfPose {
    core::Matrix4 contentWorld;float opacity{1};double time{};
    // Sample original .18s source translations on the owner's clock. Omitted
    // cards use the descriptor's current selected Y=-1.5/Z=5 (or zero).
    std::span<const ShelfSelectionPose> selections;
    double toolbarY{},toolbarZ{}; // original reveal from (6,-6) to (0,0)
    std::span<const PlaneMask> ownerMasks; // typically ModuleSurfacePose.hostClip
    std::optional<PlaneShutter> moduleShutter;
    // Explicit next-adapter boundary. Active subsection and drop-path inputs
    // currently reject BEFORE mutation; never substitute a fade/rectangle.
    core::Matrix4 collectionSublayerTransform;
    std::optional<SubsectionShutterPath> collectionReveal;
    double dropStrokeEnd{1};
};
struct NativeShelfSceneStats {
    std::size_t cards{},retiredParts{};
    std::uint64_t contentSynchronizations{},partBuilds{},poseUpdates{},feedbackChanges{};
};
#ifdef _WIN32
// Retained settled-collection bridge. The caller supplies one already-updated
// ShelfPresentation, prepared images and numeric source animation samples.
// No service, file icon lookup, clock, window, renderer publisher or state copy.
// entries() paints collection/cards, scrollbar, then drop/header/status/toolbar.
// Owner composes these entries once and appends the module registration seam
// LAST. This adapter does not replace ModuleSurface or its registration art.
// Removed/replaced parts survive until collectRetired after owner publication.
// One uncollected generation is permitted, bounding retained resources. Detach
// all entries and releaseResources BEFORE destruction; raster/source outlive it.
// The full .26s four-strip collection reveal needs a local GPU group so its
// inner mask coexists with the module's independent outer shutter. That group
// and the .20s drop stroke trace are explicitly not implemented in this slice.
class NativeShelfScene final {
public:
    NativeShelfScene(modules::ShelfPresentation&,LayerRasterizer&,LayerRasterOptions);
    ~NativeShelfScene();
    NativeShelfScene(const NativeShelfScene&)=delete;
    NativeShelfScene&operator=(const NativeShelfScene&)=delete;
    // true means content or placement changed. Image revision is owner-managed;
    // unchanged images need not be resupplied on pure scroll/pose events.
    bool syncContent(std::span<const NativeShelfImage> images={},std::uint64_t imageRevision=0);
    bool setFeedback(std::optional<std::string_view> action,bool pressed,bool reduced,double time);
    bool updatePose(const NativeShelfPose&);
    bool requiresFrames(double time)const;
    std::span<const LayerCompositionEntry> entries()const noexcept;
    std::uint64_t compositionRevision()const noexcept;
    LayerScene* cardScene(std::string_view itemID)noexcept;
    LayerScene& foregroundScene();
    bool collectRetired(Renderer&);
    bool releaseResources(Renderer&);
    NativeShelfSceneStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
} // namespace endfield::native
