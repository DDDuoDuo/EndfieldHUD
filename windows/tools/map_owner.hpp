#pragma once
// The Map module's owner: the redesigned canvas (modules/map_canvas, Mac
// WorldMapCanvas) on a tools::DesignOwnerCore, plus the platform services
// and pixels the library does not draw: the surface (native/map_scene: the
// retained raster of native/map_raster, the scrim and v1.2.0's pins, faded
// by the shared centre backdrop ramp), the bundled geography it is handed,
// and the desktop Map occlusion (core/map_source_cutout.hpp). It replaces
// tools/map_preview, tools/map_automation and the app/application_modules.cpp
// adapter of v1.2.0.
//
// Composition: the surface first (nothing of the canvas draws below it),
// then the canvas's design scene (crosshair, readout and coordinates plates,
// buttons, header). SystemHUDView.applySourceMapOcclusion masks the whole
// module host and its contrast whenever the Map is presented - at rest and
// during switches into or out of it - by the Storage and Activity Monitor
// plates: both of this owner's entries carry the host cut, and the host
// applies hostCut() to the contrast backdrop and to the other module of a
// switch. Presses inside a plate belong to the navigation button above the
// Map (HUDSourceWatchView.mapFilteredButton): covers() and pointer() decline
// them, a pinch there leaves the Map; the wheel still zooms it.
//
// Input: logical client -> box px through the canvas's design pose (the
// core); one wheel detent is one Mac notch (x1.12), a touchpad pinch
// (Ctrl+wheel, app::pinchMagnification) the Mac magnify. The 30 Hz ambient
// cadence presents the pin pulse; without it (low-power visual mode, where
// the Mac stops it, as the appearance's ambient flag does here too) a
// fallback wake keeps it moving while it can run. No thread, timer, window
// or IO of its own: geography decoding, persistence and the painter are
// injected; utility completions only invalidate.
#ifdef _WIN32
#include "app/module_owner.hpp"
#include "app/utility_executor.hpp"
#include "modules/map_canvas.hpp"
#include "native/map_scene.hpp"
#include "tools/hud_design_owner.hpp"

namespace endfield::tools {
struct MapOwnerOptions {
    modules::MapSnapshot initial;
    modules::MapPersistence persistence;   // none: the store could not be opened
    std::optional<std::string> storeError; // shown once as a notice (WorldMapCanvas.message)
    std::shared_ptr<const modules::MapGeography> geography;
    native::MapPaintFunction paint;
    std::function<void()> releaseWorkerCaches;
    native::NativeMapRaster::Clock clock;
    std::function<void()> changed;          // a raster completion: one frame
    std::function<void()> requestGeography; // WorldMapSurface.requestData: the host's one-time load
    std::function<void(modules::MapPinStyle)> pinStyleChanged;
    std::function<void()> recentered;
    native::MapPlayerImages player;
    // The presented source shell frame (the plates above the Map), read on
    // every update while the Map is posed on the source shell; nullopt: no plates.
    std::function<std::optional<modules::MapSourceView>()> sourceFrame;
    DesignOwnerOptions owner;               // name "map", routing, raster, clock, text field services
};
class MapOwner final : public app::ModuleOwner {
public:
    MapOwner(native::LayerRasterizer&, app::UtilityExecutor&, modules::design::Context&, MapOwnerOptions);
    ~MapOwner() override;
    // Geography decoded by the host (both datasets, each possibly failed:
    // the canvas toasts the Mac's message and keeps its pins).
    void setGeography(std::shared_ptr<const modules::MapGeography>, double time);
    bool utilityCompleted(double time);
    // WorldMapSurface.releaseSharedData (OverlayController's closed-heap
    // cleanup): the closed HUD keeps no map (overlayConcealed drops the
    // decoded geography, the terrain images and the surface's textures); the
    // host decodes the geography again when the Map is next shown
    // (requestGeography) and the worker paints. True while none is held.
    bool needsGeography() const noexcept;
    // A UI Automation action (map.zoomIn, map.reset, map.addPin, map.pin:<UUID>,
    // map.removePin, ...) performed as its click, the surface following at once.
    bool perform(std::string_view accessibleID, double time);
    // The host cut over the static box plane while the Map is presented (at
    // rest and during switches) with plates, for the contrast backdrop and
    // the other module of a switch. Valid after upload() until the next
    // update() or release(); null otherwise.
    const native::PlaneAlphaMask* hostCut() const noexcept;
    const modules::MapState& state() const noexcept;
    modules::MapCanvas& canvas() noexcept;
    DesignOwnerCore& core() noexcept;
    native::NativeMapRaster::Stats rasterStats() const noexcept;
    native::MapSceneStats sceneStats() const noexcept;
    bool pulsing() const noexcept;
    // The plates above the Map in logical client points (empty unless posed
    // on the source shell with a source frame).
    const modules::MapCutouts& sourceCutouts() const noexcept;

    std::string_view name() const noexcept override;
    app::ModuleRouting routing() const noexcept override;
    bool presents(core::Module m) const noexcept override { return m == core::Module::map; }
    void resize(const app::ClientMetrics&) override;
    void setAppearance(const app::ModuleAppearance&, double) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;
    void update(const app::ModuleFrame&) override;
    void prewarm(const app::ModuleFrame&) override;
    bool requiresFrames(double) const override;
    std::optional<double> nextWakeTime(double) const override;
    bool deadline(double) override;
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> entries(core::Module) override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool pointerLocked() const override;
    bool capturesPointer() const override;
    bool capturesWheel() const override;
    bool capturesKeys() const override;
    bool pointer(const app::PointerEvent&, double) override;
    bool wheel(const app::WheelEvent&, double) override;
    bool key(const app::KeyEvent&, double) override;
    bool key(const app::KeyEvent&, std::uint32_t nativeModifiers, double); // explicit modifiers (tests)
    bool message(const app::NativeMessage&, double) override;
    void focus(bool, double) override;
    void cancelInteraction(double) override;
    bool hostEvent(const app::HostEvent&, double) override;
    std::optional<app::ModuleCursor> cursorAt(core::Point) const override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
    std::optional<core::Module> automationModule() const noexcept override;
    // Memory trace: the player images, the terrain detail and backdrop and
    // the decoded geography it shares with the worker (none of the three
    // after a close) and the host cut's CPU staging; the group target and
    // textures are the renderer's.
    app::ModuleResidency residency() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
