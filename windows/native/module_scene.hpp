#pragma once
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "native/layer_scene.hpp"

namespace endfield::native {
struct ModuleRegistrationPlacement {
    core::ModulePresentationRegistration local;
    core::Matrix4 world; // Wrapper coordinates, not content-frame coordinates.
    PlaneMask hostClip;
    std::optional<PlaneShutter> shutter;
    float parentOpacity{1}; // Caller also applies local.opacity and colorAlpha.
};
struct ModuleSurfacePose {
    std::uint64_t surfaceIdentity{};
    // A design screen's contentWorld is its box px to screen (boxWorld plus
    // the switch motion); an unported module's is its v1.2.0 content frame.
    core::Matrix4 hostWorld,wrapperWorld,contentWorld;
    PlaneMask hostClip;
    // The host's masksToBounds for this sample (ModulePresentationSurface::
    // hostClip). Design owners pass hostMasks(); unported owners that pass
    // hostClip directly keep the v1.2.0 clip, also during a directional leave.
    bool hostClipped{true};
    std::optional<PlaneShutter> shutter;
    std::optional<ModuleRegistrationPlacement> registration;
    float opacity{1};
    bool attached{true}; // false: prepare only, draw and hit nothing (opacity is 0).
    std::span<const PlaneMask> hostMasks()const noexcept{return hostClipped?std::span<const PlaneMask>(&hostClip,1):std::span<const PlaneMask>();}
};
struct ModuleSurfaceStats {std::uint64_t bindings{},updates{},unchangedUpdates{},placementUpdates{};};

// Projection-only adapter for one already loaded, module-local LayerScene.
// The content root must be normalized to (0,0); its authored module frame (or,
// for a design screen, the 590 x 510 px box at DesignBox, Sources/
// HUDModuleContent.swift place) is applied here. The directional leave's plane
// offset moves the wrapper before any placement, so an unported screen leaving
// toward a design screen keeps its own v1.2.0 report placement meanwhile (the
// Mac 1.3 host has no report scale to switch). Borrowed scene lifetime and GPU resource ownership stay with
// the caller. No renderer/publication/raster/timer/provider is owned or invoked.
// The caller MUST render pose().registration last above this wrapper's content,
// using its host clip and shutter. This adapter alone is not a complete source
// transition: the original six open butt-cap/miter-join seams need a retained
// stroke primitive in the caller's single composition.
// While bound, this adapter exclusively controls this scene's placements and
// group shutter; content-only updates remain caller-owned.
class NativeModuleSurface final {
public:
    NativeModuleSurface(LayerScene&,core::Module);
    // Only after a successful structural LayerScene::load of fresh local data.
    // Calling this on the same revision rejects instead of capturing a pose
    // already projected by this adapter. Local artwork updates need no rebind.
    void rebindLocalContent();
    // settings.module is the latest requested global selection. The surface's
    // own module/frame may differ during a queued/outgoing transition. Input
    // transforms already contain sampled progress; this adapter owns no clock.
    bool update(const core::Matrix4& sourceCenter,
        const core::source::DesktopChromeSettings& settings,
        const core::ModulePresentationSurface&,float canvasOpacity=1);
    const ModuleSurfacePose& pose()const noexcept{return pose_;}
    ModuleSurfaceStats stats()const noexcept{return stats_;}
private:
    struct Base {
        core::Matrix4 world;
        float opacity{1};
        std::array<PlaneMask,7> masks{};
        std::size_t maskCount{};
    };
    LayerScene* scene_;
    core::Module module_;
    std::uint64_t sceneRevision_{};
    bool bound_{},initialized_{};
    std::vector<Base> base_;
    std::vector<std::array<PlaneMask,8>> masks_;
    std::vector<LayerPlacement> placements_;
    ModuleSurfacePose pose_;
    ModuleSurfaceStats stats_;
};
} // namespace endfield::native
