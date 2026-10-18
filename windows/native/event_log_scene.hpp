#pragma once
#include "modules/event_log_presentation.hpp"
#include "native/layer_scene.hpp"
namespace endfield::native {
#ifdef _WIN32
struct NativeEventLogPose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;};
// residentPixelBytes: the retained parts' rasters (current, outgoing and retired).
// feedbackPaints/Releases: hover or press tints and unframed rims that painted
// their source shape when their feedback began to show, and gave it back
// after fading out.
struct EventLogSceneStats {std::size_t rows{},outgoingRows{},retiredParts{};std::uint64_t builds{},poses{},outlineRasters{},parks{},feedbackPaints{},feedbackReleases{};std::size_t residentPixelBytes{};};
// Retained original header/toolbar and two bounded row pages. All finite
// animations consume the existing caller clock; no publisher/timer/service.
class NativeEventLogScene final {
public:
    NativeEventLogScene(modules::EventLogState&,LayerRasterizer&,LayerRasterOptions,modules::EventLogAppearance={});
    ~NativeEventLogScene();
    void setAppearance(modules::EventLogAppearance);
    bool syncContent(double);bool setFeedback(std::optional<std::string_view>,bool pressed,double);
    void updatePose(const NativeEventLogPose&);bool uploadAnimations(Renderer&);bool requiresFrames(double)const;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    // An Event Log that is no longer composed (the HUD closed while another
    // module was shown) keeps no artwork: every part and its rasters (shared
    // rasterizer entries) are released, and the next syncContent rebuilds
    // them from the current rows, as Mac HUDModuleContent makes the content
    // again when the module is selected. Pending motion settles. True when
    // something was released; false when there was nothing to release or a
    // part is still composed (then everything stays resident). Never throws.
    bool park(Renderer&)noexcept;
    EventLogSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
