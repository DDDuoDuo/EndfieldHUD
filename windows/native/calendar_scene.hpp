#pragma once
#include "modules/calendar_presentation.hpp"
#include "native/layer_group.hpp"
namespace endfield::native {
struct CalendarSceneDraw {
    std::string sourceID;std::size_t asset{};core::Matrix4 local;
    float opacity{1};std::optional<std::size_t>feedback;bool rim{};
};
struct CalendarScenePlan {
    ehud::data::Json assets;std::vector<CalendarSceneDraw>draws;
    std::vector<modules::CalendarFeedback>feedback;core::Rect bounds;
    std::size_t assetCount{};
};
// Exact source paint order, contiguous ordinary spans and shared identical
// highlight rasters. Day controls retain independent numeric hover/press state;
// their repeated 50x26 paths do not consume one bitmap per date.
CalendarScenePlan prepareCalendarScene(const modules::CalendarArtwork&);
#ifdef _WIN32
struct NativeCalendarPose {
    core::Matrix4 world;float opacity{1};double time{};
    std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;
};
struct NativeCalendarSceneStats {std::uint64_t builds{},poses{};std::size_t assets{},draws{},outgoingAssets{},retiredParts{};};
class NativeCalendarScene final {
public:
    NativeCalendarScene(LayerRasterizer&,LayerRasterOptions);~NativeCalendarScene();
    bool syncContent(modules::CalendarArtwork,std::uint64_t revision,double time,
        bool animated=false,bool reduceMotion=false);
    bool setFeedback(std::optional<core::Point>,bool pressed,double time,bool reduceMotion=false);
    // Source close captures the three already-rendered field faces above the
    // menu, then fades that completed menu once. These encoded leaf groups must
    // stay alive until this menu is detached; no bitmap readback is performed.
    void appendCapturedFields(std::span<const DrawObject>);
    bool uploadResources(Renderer&);void updatePose(const NativeCalendarPose&);
    bool uploadAnimations(Renderer&);bool requiresFrames(double)const;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    const modules::CalendarArtwork&artwork()const noexcept;NativeCalendarSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
