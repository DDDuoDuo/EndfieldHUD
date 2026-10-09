#pragma once
#include "modules/archive_presentation.hpp"
#include "native/layer_group.hpp"

namespace endfield::native {
struct ArchiveSurface {
    std::string id;core::Matrix4 local;float opacity{1};std::vector<core::Rect>clips;
    std::optional<std::size_t>feedback;bool rim{};
};
struct ArchiveScenePlan {
    ehud::data::Json layers;std::vector<ArchiveSurface>surfaces;
    std::vector<modules::ArchiveFeedback>feedback;core::Rect bounds;
};
// Content-event compiler for source-local translations. Static document cards
// and category caption plates remain single cached rasters; independent hover
// tint/rim surfaces preserve exact paint order. Clips stay source-plane masks.
// The returned zero-bounds container prevents a whole-module CPU bitmap.
ArchiveScenePlan prepareArchiveScene(const modules::ArchiveArtwork&,bool faceOnly=false);
#ifdef _WIN32
struct NativeArchivePose {
    core::Matrix4 world;float opacity{1};double time{};
    std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;
};
struct NativeArchiveSceneStats {std::uint64_t builds{},poses{},feedbackChanges{};std::size_t surfaces{},outgoingSurfaces{},retiredParts{};};
// Caller-composed main canvas or secondary menu. This owns no renderer/device,
// window, publisher, executor, media decoder, TSF manager or clock. It borrows
// the existing rasterizer and renderer. Remove its entries from the one owner
// composition, then releaseResources before destruction or renderer teardown.
// Content is staged before replacement. At most two live source faces exist;
// retired resources remain referenced until replacement publication completes.
class NativeArchiveScene final {
public:
    NativeArchiveScene(LayerRasterizer&,LayerRasterOptions);
    ~NativeArchiveScene();
    NativeArchiveScene(const NativeArchiveScene&)=delete;
    NativeArchiveScene&operator=(const NativeArchiveScene&)=delete;
    bool syncContent(modules::ArchiveArtwork,std::uint64_t revision,double time,
        bool animated=false,bool reduceMotion=false);
    bool setFeedback(std::optional<core::Point>,bool pressed,double time,bool reduceMotion=false);
    // Content binding event, exact local media slot BEFORE footer/error artwork.
    // Mesh/texture are borrowed from the app media owner until the outgoing
    // face has retired. Empty removes only the current face's media reference.
    bool setMediaDraw(std::optional<DrawObject>);
    void setMediaProgress(double fraction); // retained numeric fill, no raster
    // Content event before pose/publication; the group output must exist first.
    bool uploadResources(Renderer&);
    void updatePose(const NativeArchivePose&);
    bool uploadAnimations(Renderer&);bool requiresFrames(double time)const;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    const modules::ArchiveArtwork&artwork()const noexcept;
    NativeArchiveSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
} // namespace endfield::native
