#pragma once
#include "core/source_watch_frame.hpp"
#include "core/source_native_labels.hpp"
#include "native/source_scene.hpp"
#include "native/layer_scene.hpp"
#include <map>

namespace endfield::native {
struct WatchPresentationStats {
    std::uint64_t frames{},structuralCommits{},geometryUpdates{},labelUpdates{};
};
// Source frame -> exact original material resources. Pure CPU updates until the
// caller explicitly flushes SourceScene on its renderer thread. No source files,
// JSON, shader compilation or texture decode are used during presentation.
class WatchMaterialPresentation final {
public:
    explicit WatchMaterialPresentation(SourceScene&);
    void update(const core::source::SourceWatchFrame&,const SourceFrameParameters&);
    WatchPresentationStats stats()const noexcept{return stats_;}
    static std::array<float,4> vertexTint(std::array<float,4>,bool appliesAccent,
        const std::optional<std::array<float,3>>& accent)noexcept;
private:
    struct Signature {
        std::string stateID,sourceNodeID,mesh,material;
        bool appliesAccent{};
        std::vector<std::pair<std::string,std::size_t>> uniforms;
        std::map<std::string,std::string,std::less<>> textures;
        bool image{};
    };
    SourceScene* scene_;
    std::vector<Signature> signature_;
    std::vector<SourceBatchState> states_;
    std::vector<std::size_t> frameSlots_;
    std::vector<std::uint64_t> geometryRevisions_;
    WatchPresentationStats stats_;
    bool sameStructure(std::span<const core::source::SourceWatchBatch>)const;
    const SourceBatchTemplate& prototype(const core::source::SourceWatchBatch&)const;
    static void writeState(SourceBatchState&,const core::source::SourceWatchBatch&,const SourceFrameParameters&);
};
// arrangeMark / liftedMark / raised: Display > Modules > Arrange on HUD
// (core::source::NativeLabelButtonState): the tile's corner marks, widened
// while it is dragged, and the tile drawn above the others.
struct WatchButtonAvailability {std::string_view buttonID;bool enabled{true},expandFileShelfCaption{},arrangeMark{},liftedMark{},raised{};};
// Local text/icon surfaces stay rasterized. This bridge updates their source
// homographies and exact tilted clipping planes using the same frame as hits.
// Content revisions (language, text, glyphs or caption bounds) remain explicit
// caller work; contentBoundsChanged() reports those bounds without rerasterizing.
class WatchLabelPresentation final {
public:
    WatchLabelPresentation(core::source::NativeLabelPlan&,LayerScene&);
    bool update(const core::source::SourceWatchFrame&,const core::source::CameraFrame&,
        const core::Rect& viewport,std::span<const WatchButtonAvailability>);
    void present(Renderer&);
    std::span<const std::size_t> contentBoundsChanged()const noexcept{return changedBounds_;}
    std::span<const core::source::NativeLabelPlacement> placements()const noexcept{return plan_->placements();}
    // The Arrange on HUD marks of the last update (WatchArrangeMarks draws them).
    std::span<const core::source::NativeArrangeMark> marks()const noexcept{return plan_->marks();}
private:
    core::source::NativeLabelPlan* plan_;
    LayerScene* layers_;
    std::vector<core::source::NativeLabelButtonState> buttons_;
    std::vector<std::vector<core::source::NativeClipPlane>> masks_;
    std::vector<LayerPlacement> placements_;
    std::vector<std::vector<PlaneMask>> drawMasks_;
    std::vector<core::Rect> priorBounds_;
    std::vector<std::size_t> changedBounds_;
    std::uint64_t sceneRevision_{};
    bool initialized_{};
};
// Display > Modules > Arrange on HUD: the corner marks of the right-hand
// tiles (HUDArchiveTile.applyArrangeMark: six filled rectangles each, in the
// plate ink), drawn right after the watch labels (the label scene's `after`
// draws) in their tile's projected box and its button's masks. One retained
// unit quad mesh and a fixed pool of draws (stable IDs, count fixed at
// construction); a mark set that is not shown is drawn at opacity 0.
class WatchArrangeMarks final {
public:
    static constexpr std::size_t rectangles=6;
    explicit WatchArrangeMarks(std::size_t maximumMarks);
    // `rects`: per mark, its rectangles in the mark's box (points);
    // `linearTint`: the plate ink, linear straight RGBA. True when a draw changed.
    bool update(std::span<const core::source::NativeArrangeMark>,std::span<const std::array<core::Rect,rectangles>> rects,std::array<float,4> linearTint);
    bool upload(Renderer&);
    bool releaseResources(Renderer&); // false while the mesh is published
    std::span<const DrawObject> draws()const noexcept{return draws_;}
    std::size_t shown()const noexcept{return shown_;}
private:
    std::vector<DrawObject> draws_;
    Renderer* resourceOwner_{};
    std::size_t shown_{};
};
} // namespace endfield::native
