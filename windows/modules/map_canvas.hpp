#pragma once
// The Map on the design library: the port of WorldMapCanvas
// (Sources/WorldMapCanvas.swift, MODS-605..648) over the portable surface
// model (modules/map_state.hpp MapState: WorldMapSurface's camera, pins,
// selection and press). The live world map fills the whole centre backdrop
// disc (HUDCentreBackdrop: 860 px around (295, 304), the disc Personal ID's
// art fills) and fades out with its ramp; it runs under the header (behind
// a subtle scrim) and under the source shell's bottom plates (the owner cuts
// their contours out of the whole module, at rest and during switches).
// Presses, the wheel and pinch count inside v1.2.0's round map, a 369.86 px
// circle concentric with the disc; the zoom column, the centre placement,
// the zoom readout and the selected pin's coordinates sit where v1.2.0 had
// its controls. No country names, labels or region chip (owner 2026-10-21).
//
// It replaces the v1.2.0 Windows canvas (modules/map_presentation chrome,
// modules/map_accessibility, native/map_scene chrome and toolbar feedback,
// tools/map_preview, tools/map_automation). The surface's pixels (raster,
// scrim, pins) stay native (native/map_scene) and are posed under this
// canvas by its owner (tools/map_owner) through surfaceLayer(): the canvas
// builds the chrome, routes input and keeps the camera numbers.
#include "modules/hud_design_page.hpp"
#include "modules/map_state.hpp"
#include <array>
#include <map>

namespace endfield::modules {
class MapCanvas final : public design::Canvas {
public:
    // `storeError`: why the map archive could not be opened (the state then
    // has no persistence); shown once as a notice on the first activation.
    MapCanvas(design::Context&, MapState&, std::optional<std::string> storeError = {});
    ~MapCanvas() override;
    std::function<void(MapPinStyle)> onPinStyleChanged;  // event log mapPinStyleChanged
    std::function<void()> onRecenter;                    // event log mapRecentered
    // WorldMapSurface.prepareForPresentation / releaseWorkingSet: the owner
    // starts the painter (and the one-time geography load) before input,
    // and releases its working set when module input stops.
    std::function<void()> onPrepare, onRelease;

    // The camera centre, the map square (the backdrop disc's; it overflows
    // the box on every side) and the input circle's bounding square, box px.
    static constexpr design::Point mapCenter{295, 304};
    static constexpr design::Rect mapFrame{-135, -126, 860, 860};
    static constexpr design::Rect circleFrame{mapCenter.x - MapSurfaceGeometry::radius, mapCenter.y - MapSurfaceGeometry::radius,
        2 * MapSurfaceGeometry::radius, 2 * MapSurfaceGeometry::radius};
    // A point of v1.2.0's map viewport (its 440 units) in box px: the source
    // shell drew its centre 23 design units above the disc centre.
    static design::Point v1(double x, double y) noexcept;
    // Inside the input circle (box px): only there do presses, the wheel and pinch count.
    static bool onMap(design::Point) noexcept;
    static design::Point local(design::Point box) noexcept { return {box.x - mapFrame.x, box.y - mapFrame.y}; }

    MapState& state() const noexcept { return *state_; }
    // The map square in the page (frame mapFrame inside the staggered well):
    // the owner poses the native surface by its presented world and opacity.
    const design::Layer* surfaceLayer() const noexcept { return surface_; }
    bool gestureHeld() const noexcept { return gestureHeld_; }
    // WorldMapSurface.onError: a failed dataset (the owner's geography) or save.
    void reportError(const std::string& text);
    // The owner changed the state outside the canvas's own calls: chrome in place.
    void stateChanged();

    void buildPage(design::Page&) override;
    void show(double now) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override { didDeactivate(); }
    void recordingModeChanged() override {}  // nothing on the map is private (spec §3.26)
    // WorldMapCanvas.renderScaleChanged: no re-render (the scene re-rasterizes
    // the chrome at the new density; the owner moves the surface to it).
    void renderScaleChanged() override {}
    design::Rect hitBounds() const override;
    bool inputLocked() const override { return Canvas::inputLocked() || gestureHeld_ || state_->dragging(); }
    bool handlePointerDown(design::Point, const design::PointerInput&) override;
    void handlePointerDragged(design::Point) override;
    void handlePointerUp(design::Point) override;
    bool handleRightPointerDown(design::Point) override;
    bool handleScroll(design::Point, design::Point delta) override;
    bool handleMagnify(design::Point, double) override;
    bool handleKey(const design::KeyInput&) override;
    std::optional<design::Cursor> cursor(std::optional<design::Point>) const override;
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const override;
    bool accessibilityAction(std::string_view) override;
    // The gesture hold's one deadline (0.18 s after the last wheel, pinch or arrow step).
    std::optional<double> nextWakeTime(double) const override;
    void wake(double now) override;
private:
    MapState* state_;
    std::optional<std::string> message_;
    bool messageShown_{}, gestureHeld_{};
    double gestureDeadline_{};
    design::IconButton *zoomIn_{}, *zoomOut_{}, *placePin_{};
    design::Layer *surface_{}, *readoutPlate_{}, *readoutText_{}, *coordinatePlate_{};
    std::array<design::Layer*, 3> coordinateLines_{};
    std::vector<design::Layer*> chrome_;          // buttons and plates drawn on the map
    std::map<std::size_t, double> readoutWidths_; // tabular figures: by length
    bool onTerrain(design::Point) const noexcept;
    std::string counterText() const;
    bool canPlacePin() const noexcept;
    void cameraChanged();
    void pinsChanged();
    void updateCoordinates();
    void apply(const MapChange&);
    void zoomStep(double factor);
    void resetView();
    void deletePin(std::string id);
    void addCenterPin();
    void addPin(design::Point mapPoint);
    void holdGesture();
    void releaseGesture(bool commit);
};
} // namespace endfield::modules
