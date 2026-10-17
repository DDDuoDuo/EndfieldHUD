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
// surfaces: the planned leaves of the current and outgoing screens.
// Resident artwork (memory audit): parts (current, outgoing and retired
// screens), their rasterized leaves and pixelBytes, the CPU copies behind the
// GPU textures (leaf rasters plus a live handoff mask); parks: canvases
// released by park(). A hover/press tint or rim keeps a transparent point
// until its feedback shows: feedbackPaints/feedbackReleases count the leaves
// that painted their source shape and that gave it back after fading out.
// meshPlates: card, button and preset tile plates drawn as GPU meshes (no
// bitmap); textMeasures: captions measured to rasterize only the width their
// glyphs take (one measurement per caption and typography generation).
struct AppShortcutSceneStats {std::uint64_t builds{},poses{},maskRasters{},maskBytes{},replacements{},leafRepaints{},parks{},feedbackPaints{},feedbackReleases{},textMeasures{};std::size_t surfaces{},retired{},parts{},rasterSurfaces{},pixelBytes{},meshPlates{};};
// A CALayer plate's GPU meshes in plate-local points (the + Add App plates and
// the name field): its rounded background fill, and its inside border as a
// ring, each with the coverage a one-pixel box filter gives the shape at
// pixelsPerPoint (the raster's analytic coverage on pixel-aligned edges).
// Vertex alpha carries the coverage, DrawObject.linearTint the colour
// (appShortcutLinearTint). The ring needs radius >= border.
void appShortcutPlateFill(double width,double height,double radius,double pixelsPerPoint,std::vector<Vertex>&,std::vector<std::uint32_t>&);
void appShortcutPlateRing(double width,double height,double radius,double border,double pixelsPerPoint,std::vector<Vertex>&,std::vector<std::uint32_t>&);
std::array<float,4>appShortcutLinearTint(const std::array<double,4>&sRGB);
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
 // Mac OverlayController releases SystemHUDView (systemView = nil) when the
 // HUD closes: releases every retained part - its GPU resources, its raster
 // leaves in the shared rasterizer and its handoff mask - so the next
 // syncContent builds the same canvas again (a preset outline it already
 // traced is not replayed). Only while no part is published (the caller
 // stopped composing it); false releases nothing more (a published part, or
 // nothing retained).
 bool park(Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
