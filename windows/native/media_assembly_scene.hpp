#pragma once
#include "modules/media_assembly_artwork.hpp"
#include "native/media_assembly_assets.hpp"
#include <memory>
#ifdef _WIN32
#include "native/layer_scene.hpp"
#endif

namespace endfield::native {
modules::MediaAssemblyArtworkAssets mediaAssemblyArtworkAssets(const NativeMediaAssemblyAssets&);
// Source finite tracks only. Time and visibility come from the existing owner.
class MediaAssemblyMotion final {
public:
    void revealDrawer(double time,bool animated);
    void scrollInline(double offset,double time,bool animated);
    void closeMedia(double time,bool animated);
    void settle()noexcept;
    double drawerOpacity(double time)const;
    double inlineOffset(double time)const;
    double closeOpacity(double time)const;
    bool requiresFrames(double time)const;
private:
    std::optional<double>drawerStart_,scrollStart_,closeStart_;
    double scrollFrom_{},scrollTo_{};
};
#ifdef _WIN32
struct MediaAssemblyScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;const PlaneShutter*shutter{};};
struct MediaAssemblyPreviewDraw {
    // Borrowed resident provider pixels/mesh. sourceBounds describes the
    // provider's local image plane before draw.world. The scene maps it into
    // the source viewport; the provider already applies media edits/UVs.
    DrawObject draw;core::Rect sourceBounds;
};
struct MediaAssemblySceneStats {std::uint64_t builds{},localUpdates{},poses{},uploads{};std::size_t surfaces{},retired{};};
class NativeMediaAssemblyScene final {
public:
    NativeMediaAssemblyScene(LayerRasterizer&,LayerRasterOptions,const NativeMediaAssemblyAssets&);
    ~NativeMediaAssemblyScene();
    // Explicit content/input event only; never call for ordinary camera tilt.
    // Layout-only deltas preserve every unchanged local raster and bitmap.
    bool syncContent(const modules::MediaAssemblyPresentation&,const modules::MediaAssemblyView&,
        const modules::MediaAssemblyAppearance&,std::string_view error,double progress,double time,
        bool animateInlineScroll=false,bool reducedMotion=false);
    void setPreview(std::optional<MediaAssemblyPreviewDraw>);
    void revealDrawer(double time,bool animated);
    // Call before clearing the source document. A poster is deliberately
    // separate from the live video draw: closing never keeps playback alive.
    // Its provider resources must remain borrowed until closingMedia()==false
    // and collectRetired() has succeeded after publication of the new entries.
    void beginClose(std::optional<MediaAssemblyPreviewDraw>poster,double time,bool animated);
    bool closingMedia()const noexcept;
    void settle()noexcept;
    bool setFeedback(std::optional<core::Point>,bool pressed,bool reduced,double time);
    void updatePose(const MediaAssemblyScenePose&);
    void uploadResources(Renderer&);
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool requiresFrames(double time)const;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    MediaAssemblySceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
