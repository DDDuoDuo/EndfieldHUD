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
struct WatchButtonAvailability {std::string_view buttonID;bool enabled{true},expandFileShelfCaption{};};
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
} // namespace endfield::native
