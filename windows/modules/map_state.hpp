#pragma once
#include "core/scene.hpp"
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
// Original WorldMapStore/Geometry metadata. Geographic coordinates are
// normalized equirectangular values, never pixels or platform file locators.
enum class MapPinStyle {yellow,green,player};
MapPinStyle nextMapPinStyle(MapPinStyle)noexcept;
struct MapPin {
    std::string id;double x{},y{},createdAt{};MapPinStyle style{MapPinStyle::yellow};
    bool operator==(const MapPin&)const=default;
};
struct MapViewport {
    // Mac 1.3.0 WorldMapViewport (WorldMapStore.swift:35-44): the map zooms
    // out to the whole world (1x); 2.1 is v1.2.0's floor, which that release
    // still requires in a saved viewport (core/data/map_store keeps it there
    // and stores a wider camera beside it as wideZoom).
    static constexpr double minimumZoom=1,legacyMinimumZoom=2.1,maximumZoom=128,defaultZoom=3;
    double centerX{(114.0579+180)/360},centerY{(90-22.5431)/180},zoom{defaultZoom};
    bool operator==(const MapViewport&)const=default;
};
double mapWrappedX(double)noexcept;
MapViewport mapNormalized(MapViewport); // rejects nonfinite archive values
// WorldMapGeometry (unchanged in 1.3.0): v1.2.0's 440-unit viewport, centre
// 220, circle 216. It survives as the raster's camera space (the painter,
// its retained image placement and the pins' native positions).
MapViewport mapConstrained(MapViewport)noexcept;
core::Point mapScreen(double x,double y,MapViewport)noexcept;
std::string mapCoordinateDescription(double x,double y);

// WorldMapSurface (Sources/WorldMapSurface.swift:17-160): the redesigned map
// fills the centre backdrop disc (HUDCentreBackdrop, 860 x 860 content-box
// px at (-135, -126), modules/hud_design_nodes.hpp centreBackdropMask) and
// keeps v1.2.0's scale: one v1.2.0 unit is mapUnitScale box px (its
// 440-unit viewport was scaled 1.14 on the shell plane, one box px being
// HUDDesignGeometry.baseUnit design units). Map px are box px from the
// square's corner; the camera centre is the disc's centre. Presses, the
// wheel and pins count inside v1.2.0's round map (216 units, 369.86 px).
struct MapSurfaceGeometry {
    static constexpr double unitScale=1.14*1920*1.15/1470;
    static constexpr double side=860;                       // HUDCentreBackdrop.frame.size
    static constexpr core::Point center{430,430};
    static constexpr double radius=216*unitScale;           // the input circle
    static constexpr double visibleRadius=430;              // pins are drawn out to the disc's rim
    static constexpr double rasterScale=unitScale;          // map px per raster point
    static constexpr double dragThreshold=3;                // Manhattan px before a press pans
    static constexpr std::size_t pulsingPinLimit=12;
    static double pinHitRadius(MapPinStyle style)noexcept{return (style==MapPinStyle::player?13:9)*unitScale;}
    // Map px per world unit at `zoom` (the world is 2 : 1).
    struct Unit {double width{},height{};};
    static Unit unit(double zoom)noexcept{return {440*zoom*rasterScale,220*zoom*rasterScale};}
    // Zoom within [1, 128], west/east wrap; the poles stay inside v1.2.0's
    // viewport square once the world is taller than it (centerY 0.5 up to 2x).
    static MapViewport constrained(MapViewport)noexcept;
    static core::Point screen(double x,double y,MapViewport)noexcept;
    static core::Point world(core::Point,MapViewport)noexcept;
    static MapViewport panned(MapViewport,core::Point delta)noexcept;
    // Keeps the world point under `point` in place (unless a pole stops it).
    static MapViewport zoomed(MapViewport,core::Point,double factor)noexcept;
    static bool contains(core::Point)noexcept;                // the input circle
    static bool contains(core::Point,double limit)noexcept;
};

struct MapSnapshot {std::vector<MapPin>pins;MapViewport viewport;bool operator==(const MapSnapshot&)const=default;};
void validateMapSnapshot(const MapSnapshot&);
// Version-1 cameras always migrate. Only the old untouched2/3 starting cameras
// migrate; custom cameras and all version4 values remain intact.
MapSnapshot migrateMapSnapshot(MapSnapshot,unsigned version);
struct MapPersistence {
    // Atomic replacement/CAS belongs to the caller. Throw on failure; pin and
    // recenter edits publish only after success. No IO occurs during a drag.
    std::function<void(const MapSnapshot&)>commit;
    std::function<std::string()>newID;
    std::function<double()>foundationNow;
};
// What one surface call did. `error` is the message WorldMapSurface.onError
// reports (a failed save; the canvas toasts it).
struct MapChange {
    bool changed{},cameraAnimated{},recentered{},pinsChanged{};
    std::optional<MapPinStyle>styleChanged{};
    std::optional<std::string>error{};
};
// The surface's model (WorldMapSurface minus its layers, statement by
// statement): the camera, the pins and the selection over the store
// (MapPersistence; none = WorldMapCanvas's store-less fallback), the press
// that may become a pan or a click, and the camera edits in map px. Camera
// events only move numbers; the owner follows cameraRevision (raster, scene)
// and gestureRevision (WorldMapRasterController.finishGesture: the exact
// detail). No thread, timer, clock or IO of its own.
class MapState final {
public:
    static constexpr std::size_t maximumPins=128;
    explicit MapState(MapSnapshot={},MapPersistence={});
    const MapViewport&camera()const noexcept{return camera_;}
    std::span<const MapPin>pins()const noexcept{return persisted_.pins;}
    const MapSnapshot&persisted()const noexcept{return persisted_;}
    const std::optional<std::string>&selection()const noexcept{return selected_;}
    const MapPin*selectedPin()const noexcept;
    bool editable()const noexcept{return bool(persistence_.commit);}
    bool active()const noexcept{return active_;}
    bool dragging()const noexcept{return drag_.has_value();}
    bool canZoomIn()const noexcept{return camera_.zoom<MapViewport::maximumZoom;}
    bool canZoomOut()const noexcept{return camera_.zoom>MapViewport::minimumZoom;}
    // Revisions: any visible change, the camera (with whether its last write
    // animates: an eased 0.18 s move from what is on screen), the pins and
    // selection, and each requested final raster detail.
    std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t cameraRevision()const noexcept{return cameraRevision_;}
    bool cameraAnimated()const noexcept{return cameraAnimated_;}
    std::uint64_t pinRevision()const noexcept{return pinRevision_;}
    std::uint64_t gestureRevision()const noexcept{return gestureRevision_;}
    // The id of the pin whose style changed last and the change count (the
    // native scene plays v1.2.0's 0.16 s turn-in for it while active).
    const std::string&styledPin()const noexcept{return styledPin_;}
    std::uint64_t styleRevision()const noexcept{return styleRevision_;}
    void setReducedMotion(bool value)noexcept{reduced_=value;}
    bool reducedMotion()const noexcept{return reduced_;}
    // activate / deactivate (WorldMapSurface.swift:205-217): deactivating
    // cancels a press (a moved pan is committed; a pending click never
    // recenters) and commits the camera.
    MapChange setActive(bool);
    void beginDrag(core::Point mapPoint);
    MapChange drag(core::Point mapPoint);
    // Ends a press: its start point when it was a click, not a pan.
    std::optional<core::Point>endDrag(MapChange* = nullptr);
    MapChange cancelDrag();
    MapChange zoom(core::Point mapPoint,double factor,bool animated=false);
    MapChange pan(core::Point delta);
    MapChange reset();                 // default zoom and centre
    MapChange recenter(core::Point);   // keeps zoom; commits first
    MapChange endGesture();            // final detail and one archive write
    // Pins (map px). pinAt: the nearest pin within its hit radius whose
    // point lies in the input circle.
    const MapPin*pinAt(core::Point)const noexcept;
    MapChange addPin(core::Point);     // a yellow selected pin; refused beyond the poles
    MapChange removePin(std::string_view id);
    MapChange activatePin(std::string_view id); // select, or cycle the selected one
    MapChange deselect();
private:
    MapSnapshot persisted_;MapPersistence persistence_;MapViewport camera_;
    std::optional<std::string>selected_;bool active_{},reduced_{},cameraAnimated_{};
    struct Drag {core::Point start;MapViewport camera;bool moved{};};
    std::optional<Drag>drag_;std::string styledPin_;
    std::uint64_t revision_{1},cameraRevision_{1},pinRevision_{1},gestureRevision_{},styleRevision_{};
    bool commit(MapSnapshot,MapChange&);void apply(MapViewport,bool animated);void pinsChanged(MapChange&);
};
}
