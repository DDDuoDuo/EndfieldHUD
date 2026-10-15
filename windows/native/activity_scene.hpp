#pragma once
#include "modules/activity_presentation.hpp"
#include "core/motion.hpp"
#include "core/subsection_mask.hpp"
#include "core/subsection_transition.hpp"
#include <memory>

namespace endfield::native {
// Pure retained source tracks. Graph coverage and latest-point marker change
// immediately; only the sixty line/area vertices interpolate, including on an
// interrupted sample. Caller owns time, visibility and graph sampling.
struct ActivityPagePose {bool visible{};double x{};core::Rect clip;};
struct ActivityPageSample {std::array<ActivityPagePose,2>pages;bool animating{};};
class ActivityMotion final {
public:
    void select(bool apps,double time,bool animated);
    void setGraphs(const std::array<modules::ActivityGraphPlan,modules::ActivityArtwork::overviewRows>&,double time,bool animated);
    std::array<modules::ActivityGraphPlan,modules::ActivityArtwork::overviewRows>graphs(double time)const;
    ActivityPageSample pages(double time)const;
    void settle()noexcept;bool requiresFrames(double time)const;
private:bool apps_{},graphsSet_{};std::optional<double>pageStart_,graphStart_;std::array<modules::ActivityGraphPlan,modules::ActivityArtwork::overviewRows>from_{},to_{};
};
struct ActivityIcon {std::string memoryKey;std::uint64_t revision{};bool operator==(const ActivityIcon&)const=default;};
using ActivityIcons=std::map<std::string,ActivityIcon,std::less<>>;
enum class ActivityPlane {chrome,overview,apps,rows};
struct ActivitySurface {std::string id;ehud::data::Json content;core::Matrix4 local;ActivityPlane plane{};int row{-1},graph{-1};std::string action;bool rim{};unsigned paintOrder{};};
// Pure content-event decomposition. Exact source local artwork is kept apart
// from graph paths and control opacity, never rebuilt by ordinary tilt.
std::vector<ActivitySurface>prepareActivitySurfaces(const modules::ActivityArtwork&,const modules::ActivityAppearance&,const ActivityIcons& icons={});
ehud::data::Json activityGraphContent(unsigned,const modules::ActivityGraphPlan&);

// ActivityMonitorCanvas sort -> HUDSubsectionTransition.reveal(direction:
// descending ? 1 : -1) over appRows.bounds (376x226, rows local): the four
// authored shutter(progress:) keyframes (progress 0/.38/.72/1 at key times
// 0/.3/.68/1, each segment eased out), interpolated point by point. Finite
// elapsed seconds clamp to [0,.26]; no allocation. Core Animation's morph of
// these paths (the formerly sampled sort-mask.bin) is deliberately not
// reproduced: CA drops the zero-length segments of the collapsed first and the
// full last keyframe, so its path sweeps curved wedges back over rows it
// already revealed during the first and last ~80 ms (the user-reported sort
// glitch). Between the two well-formed keyframes CA's path equals this one.
core::SubsectionMaskPath activitySortShutter(double direction,double elapsed);
#ifdef _WIN32
class LayerRasterizer;class Renderer;class LayerScene;struct LayerRasterOptions;struct LayerCompositionEntry;struct PlaneMask;struct PlaneShutter;
// residentPixelBytes: the retained surfaces' rasters plus a live sort mask now.
// feedbackPaints/Releases: tab and column-header tints and rims that painted
// their source shape when their feedback began to show, and gave it back
// after fading out (they rest without a bitmap).
struct ActivitySceneStats {std::uint64_t builds{},localUpdates{},poses{},graphRasters{},graphPixelBytes{},maskRasters{},maskPixelBytes{},parks{},feedbackPaints{},feedbackReleases{};double graphRasterSeconds{},maximumGraphRasterSeconds{};std::size_t surfaces{},residentPixelBytes{};};
struct ActivityScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;const PlaneShutter*shutter{};};
class NativeActivityScene final {
public:
    // The trailing sampler argument is ignored (kept so existing callers
    // compile): the sort reveal is activitySortShutter.
    NativeActivityScene(modules::ActivityState&,LayerRasterizer&,LayerRasterOptions,modules::ActivityAppearance={},
        std::shared_ptr<const core::SubsectionMaskSampler>unusedSortSamples={});
    ~NativeActivityScene();
    // State reception is external; sync reads only already captured values.
    bool syncContent(double time,bool animateGraphs=false);
    bool setAppearance(modules::ActivityAppearance);bool setIcons(ActivityIcons);
    void selectPage(bool apps,double time,bool animated);void revealSort(double direction,double time,bool animated);
    void settle()noexcept;bool setFeedback(std::optional<core::Point>,bool pressed,bool reduced,double time);
    bool updatePose(const ActivityScenePose&);bool uploadAnimations(Renderer&);
    bool requiresFrames(double time)const;std::span<const LayerCompositionEntry>entries()const noexcept;
    std::span<const modules::ActivityAction>actions()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    // An Activity Monitor that is no longer composed (the HUD closed while
    // another module was shown) keeps no artwork: every surface, its raster
    // (shared rasterizer entries) and the sort mask are released, and the
    // next syncContent rebuilds them, as Mac HUDModuleContent makes the
    // content again when the module is selected. True when something was
    // released; false when there was nothing to release or a surface is
    // still composed (then everything stays resident). Never throws.
    bool park(Renderer&)noexcept;
    ActivitySceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
