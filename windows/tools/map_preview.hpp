#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/map_scene.hpp"
#include "native/layer_scene.hpp"
#include "modules/map_accessibility.hpp"
#include "core/localization.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"

namespace endfield::tools {
struct MapPreviewOptions {
    modules::MapSnapshot initial;
    modules::MapPersistence persistence;
    std::shared_ptr<const modules::MapGeography> geography;
    native::MapPaintFunction paint;
    std::function<void()> releaseWorkerCaches;
    native::NativeMapRaster::Clock clock;
    std::function<void()> changed;
    std::function<void(modules::MapPinStyle)> pinStyleChanged;
    std::function<void()> recentered;
    native::LayerRasterOptions raster;
    native::MapPlayerImages player;
    native::MapAppearance appearance;
    // Desktop Map occlusion (core/map_source_cutout.hpp): the presented
    // source shell frame, read on every update/prewarm while the Map is posed
    // on the source shell. The Storage and Activity Monitor plates below the
    // circle then stay above the Map: its artwork is cut under them and a
    // press there is left to the navigation (covers() and pointer() decline
    // it). Empty or nullopt: no plates (the Map covers its whole circle).
    std::function<std::optional<modules::MapSourceView>()> sourceFrame;
};
// Retained source Map canvas on the shared440-point module plane. Geography
// decoding, persistence and worker painter are injected; this owner performs
// no file IO and creates no thread, timer, window, renderer or publication.
// Executor/rasterizer outlive it. Caller detaches entries then calls release
// before destroying the shared Renderer. Utility completion only invalidates;
// call utilityCompleted after the app's drain, deadline at nextWakeTime, and
// update/upload/entries through the existing one HUD composition.
class MapPreview final {
public:
    MapPreview(native::LayerRasterizer&,app::UtilityExecutor&,MapPreviewOptions);
    ~MapPreview();
    void resize(const app::ClientMetrics&);
    void setGeography(std::shared_ptr<const modules::MapGeography>,double time);
    bool setAppearance(native::MapAppearance,double time);
    void setLanguage(core::Language,double time);
    void setOverlayVisible(bool,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    // Launch prewarm (ModuleOwner::prewarm; the host forwards it to a preview
    // that declares it): called once while the HUD is still closed, with the
    // first opening's frame. When that frame shows the Map, the first
    // update's content work - chrome captions, pin shapes and markers, the
    // retained group - is built and posed at opacity 0, so the prewarm
    // publication uploads it (textures, feather mask, group targets) and the
    // first opening frame only re-poses it. Nothing is activated: input, pin
    // pulses and the terrain paint still start with the opening; it presents
    // nothing, schedules nothing and demands no frame. A later geography
    // completion re-syncs the posed content at once, so the "LOADING
    // TERRAIN" caption is replaced before the opening rather than on it.
    void prewarm(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,double time);
    // Frame cadence. requiresFrames() is the Map's finite motion - camera
    // moves (zoom, reset, recentre), toolbar feedback and marker style
    // transitions - which the host presents at the display rate; a drag
    // presents per pointer event. The pin pulse is ambient motion: on the
    // Mac a repeating Core Animation compositor animation, here sampled from
    // the shared clock by every frame that presents it, so its 1.7 s curve
    // is unchanged. It asks for no display-rate frames: the host's 30 Hz
    // ambient cadence (on whenever the pulse can run - ambient animation on,
    // reduce motion off) presents it. Without that cadence (low-power visual
    // mode, which stops it on the Mac but not in this adapter) nextWakeTime
    // carries a pulse wake one ambient tick and a quarter after the last
    // presented frame (1/24 s, so it never races an ambient tick), and
    // deadline() then reports changed artwork once; the next presented frame
    // re-arms it. A frame the host does not present leaves no further wake.
    bool utilityCompleted(double time);bool deadline(double time);
    std::optional<double>nextWakeTime()const noexcept;
    bool requiresFrames(double time)const;bool covers(core::Point logicalClientPoint)const;
    bool pulsing()const noexcept;
    bool pointer(const app::PointerEvent&,double time);
    // Win32 detents keep fractional steps and source discrete sensitivity .12;
    // a future precise/pinch route may use scroll/magnify without a new timer.
    bool wheel(const app::WheelEvent&,double time);
    bool scroll(core::Point logicalClientPoint,double delta,bool precise,bool ended,double time);
    bool magnify(core::Point logicalClientPoint,double amount,bool ended,double time);
    // Source HUDWorldMapInteraction.keyDown takes unmodified keys only
    // (command/control/option/shift). The first form is for the HUD window's
    // key dispatch: it reads GetKeyState, which Win32 synchronizes with the
    // key message being dispatched. Any other caller (tests, synthetic input)
    // passes the modifier state explicitly, as ReaderPreview::key does: off
    // that dispatch a thread's GetKeyState follows the live keyboard of the
    // interactive session.
    bool key(const app::KeyEvent&,double time);
    bool key(const app::KeyEvent&,bool modified,double time);
    bool perform(modules::MapAction,std::string_view pinID,double time);
    // HUDWorldMapInteraction.activateAction: a listed, enabled accessible
    // action id (map:zoomIn, map:zoomOut, map:reset, map:addPin,
    // map:deletePin, map:pin:<UUID>) ends a wheel gesture, then performs
    // exactly as its click. False otherwise or while input is not accepted.
    bool perform(std::string_view accessibleID,double time);
    // Source HUDWorldMapInteraction.layoutAccessibility (modules::
    // mapAccessibility: the status over the 440 x 440 canvas, then one button
    // per accessible action) under requestAccessibilityLayout's cadence:
    // while a gesture locks input a change is published at most 12 times per
    // second (the pending publication joins nextWakeTime, and deadline()
    // then reports changed artwork so the host refreshes its peers); a change
    // outside a gesture, and the end of a gesture, publish at once. Empty
    // (no status) unless the canvas accepts input. Content-event query: an
    // unchanged map returns the published snapshot without allocating, and
    // nothing is scheduled until an automation client first asks.
    const modules::MapAccessibility&accessibility();
    // Source accessibility projection: a canvas rect -> the logical client
    // bounding rect of its projected corners while input is accepted.
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    const app::ClientMetrics&clientMetrics()const noexcept;
    void cancelInteraction(double time);bool pointerLocked()const noexcept;
    const modules::MapState&state()const noexcept;
    // Memory trace (ModuleOwner::residency): what this owner keeps beyond the
    // shared raster cache and the renderer's textures - the bundled player
    // images and feather mask, the decoded geography it shares with the host
    // worker, and the last painted terrain detail and backdrop it keeps for
    // the closing and reopening animation, as the source does. It starts no
    // thread. Counters only: no allocation, IO or OS call.
    struct Residency {std::uint64_t cpuBytes{},gpuBytes{};std::uint32_t threads{};};
    Residency residency()const noexcept;
    // The source plates above the Map in logical client points (empty unless
    // posed with a source frame).
    const modules::MapCutouts&sourceCutouts()const noexcept;
    // Desktop Map occlusion during a module switch into or out of the Map
    // (SystemHUDView.applySourceMapOcclusion masks the whole module host
    // while the Map is presented): the Storage and Activity Monitor plates
    // as an alpha mask of the module host plane (world -> host-local 440
    // points, alpha 0 under the plates; native::NativeMapScene::
    // setTransitionCut). The Map applies it to its own registration
    // brackets (its feathered artwork is cut through its own mask); the host
    // applies it to the other module's content and brackets while both are
    // presented. Valid after upload() and until the next update() or
    // release(); null when the Map is settled, hidden or has no plates.
    const native::PlaneAlphaMask*transitionCut()const noexcept;
    native::NativeMapRaster::Stats rasterStats()const noexcept;
    native::MapSceneStats sceneStats()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
