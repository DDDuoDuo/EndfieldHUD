#pragma once
#include "modules/profile_presentation.hpp"
#include "native/layer_scene.hpp"
#include <memory>
#ifdef _WIN32
namespace endfield::native {
struct NativeProfilePose {
    core::Matrix4 world;                  // module content-local -> world
    core::Matrix4 backdropWorld;          // unclipped background host content-local -> world
    float opacity{1},backdropOpacity{1};  // module transition alpha; section fade of the host
    double time{};
    std::span<const PlaneMask>ownerMasks; // module host clip for the card parts (<=6)
    std::optional<PlaneShutter>shutter;
};
// One content event's source animations, decided by the owner from
// ProfileState counters and refreshFromStore (active, not dragging).
struct NativeProfileChange {
    modules::ProfileUpdateAnimation update;
    bool popoverOpened{},popoverDismissed{},visibilityToggled{},backdropMoved{};
};
// Decoded background thumbnail (straight sRGB RGBA8). The owner replaces it
// on an image event only; the pixels go into one GPU texture. With `owner`
// (whatever keeps straightRGBA alive, e.g. the decode service's shared
// image) the scene borrows the pixels instead of copying them, so a 2048 px
// photo is resident once; without it the scene keeps its own copy.
struct NativeProfileBackdropImage {
    std::uint64_t revision{};unsigned width{},height{};std::span<const std::uint8_t>straightRGBA;
    std::shared_ptr<const void>owner;
};
struct NativeProfileSceneStats {
    std::uint64_t loads{},localUpdates{},poses{},textureUploads{},groupDraws{},parks{};std::size_t surfaces{};
    // What the scene keeps resident: loaded parts and their raster pixels,
    // the backdrop photo bytes it owns (0 while borrowed) and its GPU
    // resources (textures, the backdrop group) on the renderer.
    std::size_t parts{},pixelBytes{},ownedPhotoBytes{};bool gpuResident{};
};
// PersonalProfileCanvas fields/toolbar/popover plus its separately placed
// background plane. Every top-level artwork leaf is one retained surface: a
// content event re-rasterizes only changed leaves; tilt, feedback and finite
// animations change numeric placements only. The background photo, shade and
// gradient-masked text contrast use fixed textures and a GPU native group;
// slider drags move UV/world numbers, never re-rasterize. No window, timer,
// clock, worker, store or publisher is created here.
class NativeProfileScene final {
public:
    NativeProfileScene(LayerRasterizer&,LayerRasterOptions);
    ~NativeProfileScene();
    NativeProfileScene(const NativeProfileScene&)=delete;
    NativeProfileScene&operator=(const NativeProfileScene&)=delete;
    bool syncContent(const modules::ProfileArtwork&,std::uint64_t revision,double time,const NativeProfileChange&,bool reduceMotion);
    // refreshWorkDuration: the hours caption content changed while active.
    bool animateWork(double time,bool reduceMotion);
    bool setBackdropImage(std::optional<NativeProfileBackdropImage>);
    // Module-local pointer (nullopt when outside/inactive).
    bool setFeedback(std::optional<core::Point>,bool pressed,double time,bool reduceMotion);
    void updatePose(const NativeProfilePose&);
    bool uploadResources(Renderer&);
    bool requiresFrames(double time)const;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);
    bool releaseResources(Renderer&); // owner detaches the entries first
    // HUDModuleContent drops a screen's layers once it is no longer shown: a
    // page that is no longer composed releases its GPU resources (the parts'
    // textures, the backdrop photo and group target, the fixed gradients)
    // and its parts (their rasters in the shared rasterizer). The backdrop
    // image and the fixed gradient bitmaps stay; the next syncContent rebuilds
    // the parts from the same artwork, painting identically. `renderer` is
    // the one the scene uploaded to (null before any upload). True when
    // something was released; false when nothing is built or uploaded, or a
    // resource is still published (then everything stays). Never throws.
    bool park(Renderer* renderer)noexcept;
    const modules::ProfileArtwork&artwork()const noexcept;
    NativeProfileSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
