#pragma once
// One retained native scene for a design layer tree (a canvas's root, the
// shared centre overlay, a module's floating panel). It generalizes
// NativeSettingsScene (native/settings_scene.cpp: parts reused by byte-equal
// JSON, compaction, feedback tracks, the generation budget check) and
// Clipboard's row-local placement and lazy feedback (native/clipboard_scene.*)
// for every redesigned module; no module writes its own scene any more.
//
// Content (syncContent, content events only): when the canvas's
// contentRevision moved, the tree is emitted (modules::design::emit) and each
// Part is matched to the retained one with the same id:
//   - byte-equal JSON: reused (no raster);
//   - same surfaces, some JSON differs: LayerScene::updateLocalContent per
//     changed surface;
//   - otherwise a new LayerScene is loaded; the old one is retired and
//     released after the next publication (collectRetired).
// Required images (requiredSourceImage keys) resolve through `images`
// before a load; a missing or malformed one aborts the generation (as Settings).
// Every generation first checks the shared budget
// (LayerRasterizer::maximumEntries minus the entries in use must cover the
// surfaces it would add) and throws before touching anything when it does
// not fit. Lazy surfaces (sheen, hover tints) are not counted until they show.
// Scroll content parts (parts under a masksToBounds ancestor) outside the
// visibility window (their clip's viewport plus one viewport of slack, model
// geometry: the scroll target) are not rasterized; parts that leave it by two
// viewports drop their raster. syncContent re-checks the window on every
// call, also without a content change (a scroll step moves only numbers).
//
// Pose (updatePose, every frame, allocation-free): samplePoses() gives each
// surface its box-px affine, opacity, sRGB tint and clip rects; the scene
// composes boxWorld * affine into LayerPlacement worlds, converts the tint to
// linear (LayerPlacement::linearTint, R4), turns clip rects into PlaneMasks
// (owner masks first, at most 8). An alpha clip (the sheen inside its
// chamfered plate, R4b) binds the texture of the surface of the same part
// that draws the clip layer (LayerScene::surfaceAlphaMask); a clip in another
// part falls back to its model rect.
//
// Group opacity (LIBRARY-DESIGN.md section 3, Q3: the Mac composites every
// layer's opacity as a group): while a part of two or more drawn surfaces
// fades as a whole (its group layer, the deepest layer above all its
// placements, times the module's opacity below 1: entrance, leave, hold,
// menu fade, toast hide, module switch), a pooled borrowed NativeLayerGroup
// draws its retained scene into one target (the part's surfaces placed in
// the group layer's plane, their opacity below it) and the target is drawn
// once with the group opacity, so a label over its plate fades as one image.
// The group attaches in uploadAnimations at the start of the fade (a target
// allocation, an entry change) and detaches when it rests at 1 or 0 (entry
// change; the target is dropped by the next collectRetired after the list
// without it is published): no target memory at rest. Adaptations: one group
// level per part (a translucent layer INSIDE a part, e.g. a disabled
// control's .35, still multiplies per surface; a part resting at a partial
// group opacity keeps its target while it rests); a part the renderer's group
// limits refuse (Renderer::maximumNativeGroupPixels at the density) draws
// directly for that fade.
//
// Requires the tree of the last syncContent: requiresFrames and updatePose
// read its layers, so an owner syncs after every content change first (the
// owner core asks the canvas, not the scene, while a change is pending).
//
// Content dim: not here. While the shared overlay dims, the HOST applies
// Renderer::setContentFilter over the range of module entries (A2), with the
// amount from DesignOverlayOwner::contentDim; scenes draw as usual.
//
// No window, timer, provider, clipboard or second renderer; UI thread only.
#include "modules/hud_design_layer.hpp"
#ifdef _WIN32
#include "native/layer_group.hpp"
#include "native/layer_scene.hpp"
#include <functional>
#include <map>

namespace endfield::native {
// requiredSourceImage key -> {asset, sha256} (pinned) or {memoryImage...}.
using DesignImages = std::map<std::string, ehud::data::Json, std::less<>>;
struct NativeDesignScenePose {
    core::Matrix4 boxWorld;                   // ModuleSurfacePose::contentWorld of a design screen (DesktopChromeLayout::boxWorld + switch motion)
    float opacity{1};                         // module presentation opacity
    double time{};
    std::span<const PlaneMask> ownerMasks;    // at most 4 (the host clip is OFF for design screens)
    std::optional<PlaneShutter> shutter;      // never for directional switches; kept for symmetry
};
struct NativeDesignSceneStats {
    std::uint64_t generations{}, emits{}, partsReused{}, surfacesUpdated{}, partsLoaded{}, poses{}, lazyPaints{}, lazyReleases{};
    std::uint64_t groupSwitches{}, parks{};   // groupSwitches: group attaches and detaches
    std::size_t parts{}, surfaces{}, retiredParts{}, rasterEntries{}, residentPixelBytes{};
    std::size_t groups{}, groupTargets{};      // parts drawn through a group; group targets held (incl. awaiting collectRetired)
};
class NativeDesignScene final {
public:
    NativeDesignScene(LayerRasterizer&, LayerRasterOptions, std::string sourcePrefix, DesignImages = {});
    ~NativeDesignScene();
    NativeDesignScene(const NativeDesignScene&) = delete;
    NativeDesignScene& operator=(const NativeDesignScene&) = delete;
    // `revision`: the canvas's contentRevision (or any monotonic content key).
    // pixelsPerPoint changes (render scale) re-rasterize every part.
    bool syncContent(const modules::design::Layer& root, const modules::design::TextServices&, std::uint64_t revision,
        double pixelsPerPoint, double now);
    void setImages(DesignImages);
    void updatePose(const NativeDesignScenePose&);
    // Uploads lazily painted surfaces and group targets (attaches the groups
    // of fades that start, updates their children); releases faded lazy
    // surfaces. Returns true when composition entries changed (also by a
    // group detached in updatePose).
    bool uploadAnimations(Renderer&);
    bool requiresFrames(double now) const;
    std::span<const LayerCompositionEntry> entries() const noexcept;
    bool collectRetired(Renderer&);
    bool releaseResources(Renderer&);
    // A module that is no longer composed keeps no artwork: every part's
    // rasters (shared rasterizer entries), groups and lazy surfaces are
    // released; the next syncContent rebuilds them (Clipboard's park). False
    // when nothing was released or a part is still composed. Never throws.
    bool park(Renderer&) noexcept;
    // Accessors for the owner core (cursor and UIA project through these).
    const core::Matrix4& boxWorld() const noexcept;
    NativeDesignSceneStats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif
