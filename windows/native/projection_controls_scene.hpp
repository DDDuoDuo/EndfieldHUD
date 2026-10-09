#pragma once
#include "modules/projection_presentation.hpp"
#include "native/layer_group.hpp"
#include <memory>

namespace endfield::native {
struct ProjectionControlSurface {
    static constexpr std::size_t none=static_cast<std::size_t>(-1);
    std::string id;core::Matrix4 local;float opacity{1};
    std::size_t feedback{none},inkLabel{none};bool rim{};
};
struct ProjectionControlsScenePlan {
    ehud::data::Json layers;std::vector<ProjectionControlSurface> surfaces;
};
// Pure content-event compilation: plate contents, children, then its border.
// Ink labels are marked for event-only shared-font raster measurement below.
ProjectionControlsScenePlan prepareProjectionControlsScene(const modules::ProjectionControls&);
// Expand an unwrapped toolbar leaf from full shared-font measurement before
// scanning ink. Padding/line metrics only position its glyphs inside the probe;
// the alpha-derived placement removes that offset. The caller clips the final
// retained leaf with the original item rectangle, never this expanded extent.
ehud::data::Json prepareProjectionGlyphContent(const ehud::data::Json& leaf,
    const LayerSourceTextMeasurement&);
// Pixel alpha bounds in the supplied raster's local point space. Empty ink
// has no offset; malformed dimensions/storage fail before indexing the image.
std::optional<core::Point> projectionGlyphInkOffset(const LayerRasterImage&,core::Rect target);

struct NativeProjectionControlsSceneStats {
    std::uint64_t contentUpdates{},glyphMeasurements{},poseUpdates{},feedbackChanges{};
};
// Borrowed source/rasterizer outlive this adapter. All leaves remain local;
// NativeLayerGroup applies the one source fade/pose after their compositing.
// syncContent is the sole CPU raster path, including default-font events.
// Owner calls upload before publishing entry(), and after syncContent changes.
// updatePose refreshes numeric feedback through that borrowed renderer; root
// pose/fade only change the group's output. No independent clock or device.
class NativeProjectionControlsScene final {
public:
    NativeProjectionControlsScene(modules::ProjectionControls&,LayerRasterizer&,LayerRasterOptions);
    ~NativeProjectionControlsScene();
    NativeProjectionControlsScene(const NativeProjectionControlsScene&)=delete;
    NativeProjectionControlsScene&operator=(const NativeProjectionControlsScene&)=delete;
    bool syncContent();
    bool setFeedback(std::optional<std::string_view> action,bool pressed,bool reduced,double time);
    bool updatePose(const core::Matrix4& world,float opacity,double time);
    bool upload(Renderer&);
    LayerCompositionEntry entry();
    // Detach the borrowed entry from its composition before release/destruction.
    bool releaseResources(Renderer&);
    bool requiresFrames(double time)const;
    const LayerScene& scene()const noexcept;
    NativeProjectionControlsSceneStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
