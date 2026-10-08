#pragma once
#include "modules/notes_controls.hpp"
#include "native/layer_scene.hpp"
#include <memory>

namespace endfield::native {
// The owner supplies a pinned, already source-prepared raster (including the
// requested source-in tint/size). Raw untinted icons are not interchangeable.
// LayerRasterizer verifies the confined asset path and SHA before publication.
struct NativeNotesControlsImage {
    modules::NotesControlsImage dependency;
    ehud::data::Json contents; // exact {asset, sha256} LayerRasterizer metadata
};
struct NotesControlSurface {
    static constexpr std::size_t none=static_cast<std::size_t>(-1);
    std::string id;core::Matrix4 local;float opacity{1};
    std::size_t toolbar{none},feedback{none};bool rim{};
};
struct NotesControlsScenePlan {
    ehud::data::Json layers;std::vector<NotesControlSurface> surfaces;
    bool requiresGroupOpacity{}; // menu root, including its overflowing shadow
};
// Pure content-event preparation. Original own artwork, highlight and border
// paint order is retained; toolbar descendants share one numeric motion index.
// Does not read assets, fonts, OS state, time, or mutate the caller descriptor.
NotesControlsScenePlan prepareNotesControlsScene(const modules::NotesControls&,
    std::span<const NativeNotesControlsImage> images={});

struct NativeNotesControlsSceneStats {std::uint64_t contentUpdates{},poseUpdates{},feedbackChanges{};};
// One caller-composed scene; borrowed controls/rasterizer outlive this adapter.
// The owner supplies the source module/workspace plane and optional host masks;
// menus never acquire the center clip automatically. Toolbar offsets are in
// tool:text/todo/image/drawing order and include all of each plate's descendants.
// No window, publisher, renderer, timer, services, picker or real data access.
// Partial menu opacity requires a retained GPU group pass. Until supplied,
// updatePose rejects it explicitly. Use identity world/opacity1 to feed ordered
// local surfaces into that future pass; content and feedback remain independent.
class NativeNotesControlsScene final {
public:
    NativeNotesControlsScene(modules::NotesControls&,LayerRasterizer&,LayerRasterOptions);
    ~NativeNotesControlsScene();
    NativeNotesControlsScene(const NativeNotesControlsScene&)=delete;
    NativeNotesControlsScene&operator=(const NativeNotesControlsScene&)=delete;
    bool syncContent(std::span<const NativeNotesControlsImage> images={},std::uint64_t imageRevision=0);
    bool setFeedback(std::optional<std::string_view> action,bool pressed,bool reduceMotion,double time);
    bool updatePose(const core::Matrix4& localToScreen,float opacity,double time,
        std::array<double,4> toolbarOffsets={},std::span<const PlaneMask> ownerMasks={},
        std::optional<PlaneShutter> ownerShutter={});
    bool requiresFrames(double time)const;
    LayerScene& scene()noexcept;
    const LayerScene& scene()const noexcept;
    bool requiresGroupOpacity()const noexcept;
    NativeNotesControlsSceneStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
