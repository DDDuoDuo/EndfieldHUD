#pragma once
#include "modules/activity_presentation.hpp"
#include "core/motion.hpp"
#include "core/subsection_mask.hpp"
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
    void setGraphs(const std::array<modules::ActivityGraphPlan,4>&,double time,bool animated);
    std::array<modules::ActivityGraphPlan,4>graphs(double time)const;
    ActivityPageSample pages(double time)const;
    void settle()noexcept;bool requiresFrames(double time)const;
private:bool apps_{},graphsSet_{};std::optional<double>pageStart_,graphStart_;std::array<modules::ActivityGraphPlan,4>from_{},to_{};
};
struct ActivityIcon {std::string memoryKey;std::uint64_t revision{};bool operator==(const ActivityIcon&)const=default;};
using ActivityIcons=std::map<std::string,ActivityIcon,std::less<>>;
enum class ActivityPlane {chrome,overview,apps,rows};
struct ActivitySurface {std::string id;ehud::data::Json content;core::Matrix4 local;ActivityPlane plane{};int row{-1},graph{-1};std::string action;bool rim{};unsigned paintOrder{};};
// Pure content-event decomposition. Exact source local artwork is kept apart
// from graph paths and control opacity, never rebuilt by ordinary tilt.
std::vector<ActivitySurface>prepareActivitySurfaces(const modules::ActivityArtwork&,const modules::ActivityAppearance&,const ActivityIcons& icons={});
ehud::data::Json activityGraphContent(unsigned,const modules::ActivityGraphPlan&);

#ifdef _WIN32
class LayerRasterizer;class Renderer;class LayerScene;struct LayerRasterOptions;struct LayerCompositionEntry;struct PlaneMask;struct PlaneShutter;
struct ActivitySceneStats {std::uint64_t builds{},localUpdates{},poses{},graphRasters{},graphPixelBytes{},maskRasters{},maskPixelBytes{};double graphRasterSeconds{},maximumGraphRasterSeconds{};std::size_t surfaces{};};
struct ActivityScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;const PlaneShutter*shutter{};};
class NativeActivityScene final {
public:
    NativeActivityScene(modules::ActivityState&,LayerRasterizer&,LayerRasterOptions,modules::ActivityAppearance={},
        std::shared_ptr<const core::SubsectionMaskSampler>sortSamples={});
    ~NativeActivityScene();
    // State reception is external; sync reads only already captured values.
    bool syncContent(double time,bool animateGraphs=false);
    bool setAppearance(modules::ActivityAppearance);bool setIcons(ActivityIcons);
    void selectPage(bool apps,double time,bool animated);void revealSort(double direction,double time,bool animated);
    void settle()noexcept;bool setFeedback(std::optional<core::Point>,bool pressed,bool reduced,double time);
    bool updatePose(const ActivityScenePose&);bool uploadAnimations(Renderer&);
    bool requiresFrames(double time)const;std::span<const LayerCompositionEntry>entries()const noexcept;
    std::span<const modules::ActivityAction>actions()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);ActivitySceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
