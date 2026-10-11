#pragma once
#include "native/app_shortcut_assets.hpp"
#include "modules/app_shortcut_motion.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"
namespace endfield::native {
struct AppShortcutScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;const PlaneShutter*shutter{};};
// builds: new canvases (a screen change, a preset trace or a changed leaf
// set); replacements: content events that kept the canvas and repainted
// leafRepaints changed leaves in place (a scroll step only re-places them).
struct AppShortcutSceneStats {std::uint64_t builds{},poses{},maskRasters{},maskBytes{},replacements{},leafRepaints{};std::size_t surfaces{},retired{};};
// Source list/draft artwork, six retained virtual row cards, independent feedback,
// source .26s local two-screen transition. No provider, clock, HWND or publisher.
// State/artwork/rasterizer must outlive the adapter. Current/outgoing content is
// retired only after caller publishes entries() through its shared composition.
class NativeAppShortcutScene final {
public:
 NativeAppShortcutScene(modules::AppShortcutState&,const NativeAppShortcutAssets&,LayerRasterizer&,LayerRasterOptions,modules::ShortcutAppearance={});
 ~NativeAppShortcutScene();
 bool setAppearance(modules::ShortcutAppearance);bool setOriginalImages(modules::ShortcutOriginalImages);
 bool syncContent(double time);bool setFeedback(std::optional<core::Point>,bool pressed,bool reduced,double time);
 void updatePose(const AppShortcutScenePose&);bool uploadAnimations(Renderer&);
 bool requiresFrames(double time)const;std::span<const LayerCompositionEntry>entries()const noexcept;
 bool collectRetired(Renderer&);bool releaseResources(Renderer&);AppShortcutSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
