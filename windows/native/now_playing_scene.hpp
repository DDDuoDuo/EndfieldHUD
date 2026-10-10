#pragma once
namespace endfield::native {
// Actual detached Core Animation source hierarchy: own opacity quantized to
// opaque passes inherited false-group opacity to each leaf; own fades isolate.
// Portable so the immutable source samples exercise the production decision.
struct NowPlayingGroupOpacity {double leaf{1},output{1};};
NowPlayingGroupOpacity nowPlayingGroupOpacity(double inherited,double own);
}
#ifdef _WIN32
#include "modules/now_playing_artwork.hpp"
#include "native/layer_scene.hpp"
#include "native/notes_image_decoder.hpp"
namespace endfield::native {
struct NowPlayingScenePose {
    core::Matrix4 world;float opacity{1};double time{};
    std::span<const PlaneMask>masks;const PlaneShutter*shutter{};
};
struct NowPlayingSceneStats {
    std::uint64_t contentEvents{},localRasterUpdates{},coverUploads{},poses{},feedbackChanges{};
    std::size_t rasterSurfaces{};
};
// Borrowed presentation/rasterizer outlive this retained scene. Artwork is a
// source-bounded immutable <=512px thumbnail decoded on the shared utility
// queue; a null image preserves the actual missing-cover state. No decoder,
// provider, service, clock, file access or worker is owned here.
//
// One encoded local composition preserves the source transparent square cover
// and per-child opacity. Its depth-one leaf groups provide exact weighted
// cover/caption transitions, clipped lyrics and one volume-menu fade. Root
// tilt only updates the retained output; progress/feedback stays numeric.
class NativeNowPlayingScene final {
public:
    NativeNowPlayingScene(modules::NowPlayingPresentation&,LayerRasterizer&,LayerRasterOptions);
    ~NativeNowPlayingScene();
    NativeNowPlayingScene(const NativeNowPlayingScene&)=delete;
    NativeNowPlayingScene&operator=(const NativeNowPlayingScene&)=delete;
    // Content/font event only. The owner publishes matching coverAvailable and
    // coverRevision in the presentation before calling this method.
    bool syncContent(std::shared_ptr<const NotesImageFrame>artwork,double time);
    bool setFeedback(std::optional<modules::NowPlayingAction>,bool pressed,double time);
    bool updatePose(const NowPlayingScenePose&);
    void uploadResources(Renderer&);
    LayerCompositionEntry entry();
    bool requiresFrames(double time)const noexcept;
    // Detach entry from the owning composition before release/destruction.
    bool releaseResources(Renderer&);
    NowPlayingSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
