#pragma once
#include "modules/storage_presentation.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"
namespace endfield::native {
struct NativeStorageSceneStats {std::uint64_t structures{},localUpdates{},poses{},feedbackChanges{},parks{};};
// One retained original Storage artwork tree. Borrowed presentation/rasterizer
// outlive this adapter. Pointer, source refresh rotation and module placement
// update only numeric draw data, with no private timer or GPU publisher.
class NativeStorageScene final {
public:
    NativeStorageScene(modules::StoragePresentation&,LayerRasterizer&,
        LayerRasterOptions,modules::StorageSourcePaths);
    ~NativeStorageScene();
    bool syncContent(double time);
    void setActivity(bool active,bool reduceMotion,double time);
    void manualRefreshTurn(double time); // call before submitting source refresh
    bool setFeedback(std::optional<core::Point>,bool pressed,double time);
    void updatePose(const core::Matrix4&,float opacity,double time,
        std::span<const PlaneMask> masks={},const PlaneShutter* shutter=nullptr);
    bool requiresFrames(double time)const;
    LayerScene&scene()noexcept;
    // A Storage page that is no longer composed (the HUD closed while another
    // module was shown) keeps no artwork: the retained tree is unloaded, which
    // removes its raster entries from the shared rasterizer, after its GPU
    // resources were released; the next syncContent loads it again from the
    // current presentation, as Mac HUDModuleContent makes the content again
    // when the module is selected. True when something was released; false
    // when nothing was loaded or the tree is still composed (then it stays
    // resident). Never throws.
    bool park(Renderer&)noexcept;
    bool loaded()const noexcept; // a syncContent has built the tree (and no park dropped it since)
    NativeStorageSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
// One retained Storage overlay (Windows addition: the volume chooser button or
// its secondary menu) above the Mac artwork, which it never touches. show()
// loads new artwork into a fresh LayerScene (a content event); the previous
// one retires and its resources are released in collected() once the owner's
// composition no longer lists it. The menu enters like NotesRetainedMenu
// (opacity 0 -> 1 and y +6 -> 0 over .14 s, ease out) and leaves with an
// opacity fade and y 0 -> +4 over .12 s (ease in); input stops at once.
// Feedback is numeric (tint .62/1, rim 1; a framed rim rests at .28).
class NativeStorageOverlay final {
public:
    NativeStorageOverlay(LayerRasterizer&,LayerRasterOptions);
    ~NativeStorageOverlay();
    NativeStorageOverlay(const NativeStorageOverlay&)=delete;
    NativeStorageOverlay&operator=(const NativeStorageOverlay&)=delete;
    void show(modules::StorageOverlayArtwork,double time,bool animated);
    void hide(double time,bool animated);
    bool accepting()const noexcept;           // shown and not leaving
    LayerScene*scene()noexcept;               // null when nothing is drawn
    std::optional<std::string_view>actionAt(core::Point canvas)const noexcept;
    // The shown artwork's actions (canvas points) while it accepts input.
    std::span<const modules::StorageAction>actions()const noexcept;
    bool contains(core::Point canvas)const noexcept;
    bool setFeedback(std::optional<core::Point>canvas,bool pressed,bool reduced,double time);
    void updatePose(const core::Matrix4&contentWorld,float opacity,double time,std::span<const PlaneMask>masks={},const PlaneShutter*shutter=nullptr);
    bool requiresFrames(double time)const;
    void collected(Renderer&);bool release(Renderer&);
    // Not composed any more (see NativeStorageScene::park): drops the shown
    // and retired artwork after releasing their GPU resources, ending any
    // entrance or exit at once; the owner shows it again on its next
    // activation. True when something was released. Never throws.
    bool park(Renderer&)noexcept;
    std::uint64_t builds()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
