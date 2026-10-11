#pragma once
#include "modules/shelf_presentation.hpp"
#include "native/layer_scene.hpp"
#include "core/subsection_transition.hpp"

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
    std::size_t feedback{none};bool rim{},toolbar{},drop{};
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
    std::optional<ShelfScenePart> properties; // the properties card, painted above everything
};
// Pure content-event compiler. Empty model bounds do not erase CAShapeLayer
// path/stroke ink. Leaves retain original coordinates and paint order; only
// root placement and feedback opacity become numeric retained surface data.
ShelfScenePlan prepareShelfScene(const modules::ShelfPresentation&,
    std::span<const NativeShelfImage> images={});

struct ShelfSelectionPose {std::string_view itemID;double y{},z{};};
struct ShelfRevealSample {double direction{1},elapsed{};};
// HUDSubsectionTransition.reveal's collection mask as authored: the four
// shutter(progress:) keyframes (progress 0/.38/.72/1 at key times 0/.3/.68/1,
// each segment eased out) over the Shelf viewport, interpolated point by
// point. direction<0 opens from the left edge (removal and Clear), otherwise
// from the right (additions). Finite elapsed seconds clamp to [0,.26]; no
// allocation. Core Animation's own morph of these paths is deliberately not
// reproduced: CA drops the zero-length segments of the collapsed first and
// the full last keyframe, so the element lists no longer correspond and its
// interpolated path (measured from paused presentation layers) sweeps curved
// wedges across the cards during the first and last ~80 ms of every reveal.
// Between the two well-formed keyframes CA's path equals this one.
core::SubsectionMaskPath shelfRevealShutter(double direction,double elapsed);
// Original rounded drop outline, trimmed by true cubic arc length. Retains
// authored curve segments (no flattened polygon or generic fade).
ehud::data::Json shelfDropStrokePath(double strokeEnd);
struct NativeShelfPose {
    core::Matrix4 contentWorld;float opacity{1};double time{};
    // Sample original .18s source translations on the owner's clock. Omitted
    // cards use the descriptor's current selected Y=-1.5/Z=5 (or zero).
    std::span<const ShelfSelectionPose> selections;
    double toolbarY{},toolbarZ{}; // original reveal from (6,-6) to (0,0)
    std::span<const PlaneMask> ownerMasks; // typically ModuleSurfacePose.hostClip
    std::optional<PlaneShutter> moduleShutter;
    // Explicit authored polygon input; no point interpolation is implied. For
    // intermediate source CA curves supply reveal below instead.
    core::Matrix4 collectionSublayerTransform;
    std::optional<SubsectionShutterPath> collectionReveal;
    double dropStrokeEnd{1};
    std::optional<ShelfRevealSample> reveal; // finite caller-clock .26s animation (shelfRevealShutter)
    // The properties card's fade (NotesRetainedMenu: opacity .16 s in and out),
    // multiplied with opacity. The card sits on the Shelf plane at its origin,
    // outside the collection clip and reveal.
    float propertiesOpacity{1};
};
struct NativeShelfSceneStats {
    std::size_t cards{},retiredParts{};
    std::uint64_t contentSynchronizations{},partBuilds{},poseUpdates{},feedbackChanges{};
    std::uint64_t maskRasters{},maskUploads{},dropRasters{},topologyGapSamples{};
};
#ifdef _WIN32
// Retained settled-collection bridge. The caller supplies one already-updated
// ShelfPresentation, prepared images and numeric source animation samples.
// No service, file icon lookup, clock, window, renderer publisher or state copy.
// entries() paints collection/cards, scrollbar, then drop/header/status/toolbar,
// then the properties card.
// Owner composes these entries once and appends the module registration seam
// LAST. This adapter does not replace ModuleSurface or its registration art.
// Removed/replaced parts survive until collectRetired after owner publication.
// One uncollected generation is permitted, bounding retained resources. Detach
// all entries and releaseResources BEFORE destruction; raster/source outlive it.
// The reveal mask uses one bounded planar alpha texture during the finite .26s
// transition, intersecting the independent outer module shutter. D2D raster
// antialiasing is an explicit parity limit.
// Call collectRetired after composition.present to retire a completed mask.
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
    // After pose, before combined publication. Raster/upload only changed finite
    // mask or stroke samples; steady poses do no bitmap or resource work.
    bool uploadAnimations(Renderer&);
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
