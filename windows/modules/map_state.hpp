#pragma once
#include "core/scene.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
// Original WorldMapStore/Geometry/Canvas metadata. Geographic coordinates are
// normalized equirectangular values, never pixels or platform file locators.
enum class MapPinStyle {yellow,green,player};
MapPinStyle nextMapPinStyle(MapPinStyle)noexcept;
struct MapPin {
    std::string id;double x{},y{},createdAt{};MapPinStyle style{MapPinStyle::yellow};
    bool operator==(const MapPin&)const=default;
};
struct MapViewport {
    static constexpr double minimumZoom=2.1,maximumZoom=128,defaultZoom=3;
    double centerX{(114.0579+180)/360},centerY{(90-22.5431)/180},zoom{defaultZoom};
    bool operator==(const MapViewport&)const=default;
};
double mapWrappedX(double)noexcept;
MapViewport mapNormalized(MapViewport); // rejects nonfinite archive values
MapViewport mapConstrained(MapViewport)noexcept; // source geometry fallback
core::Point mapWorld(core::Point,MapViewport)noexcept;
core::Point mapScreen(double x,double y,MapViewport)noexcept;
MapViewport mapPanned(MapViewport,core::Point delta)noexcept;
MapViewport mapZoomed(MapViewport,core::Point,double factor)noexcept;
MapViewport mapRecentered(MapViewport,core::Point)noexcept;
bool mapContains(core::Point)noexcept;
core::Rect mapMarkerHitRect(MapPinStyle,core::Point)noexcept;
std::string mapCoordinateDescription(double x,double y);
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
enum class MapAction {zoomIn,zoomOut,reset,addPin,deletePin,pin};
struct MapActionHit {MapAction action;std::string_view pinID;core::Rect rect;bool enabled;};
struct MapActions {std::array<MapActionHit,133>items{};std::size_t count{};};
enum class MapKey {left,right,down,up,zoomIn,zoomOut,erase,escape};
struct MapChange {
    bool handled{},changed{},cameraAnimated{},recentered{};
    std::optional<MapPinStyle>styleChanged{};
};
// Retained input/persistence state. Owner supplies clock events and routes the
// optional wheel deadline through the shared host; no thread/timer/service.
// Action IDs returned by actions() borrow pins until the next successful edit.
class MapState final {
public:
    static constexpr std::size_t maximumPins=128;
    explicit MapState(MapSnapshot={},MapPersistence={});
    const MapViewport&viewport()const noexcept{return viewport_;}
    std::span<const MapPin>pins()const noexcept{return persisted_.pins;}
    const MapSnapshot&persisted()const noexcept{return persisted_;}
    const std::optional<std::string>&selection()const noexcept{return selected_;}
    const std::optional<std::string>&error()const noexcept{return error_;}
    bool showsCoordinates()const noexcept{return coordinates_;}
    bool active()const noexcept{return active_;}
    bool dragging()const noexcept{return drag_.has_value();}
    bool pointerLocked()const noexcept{return dragging()||wheelDeadline_.has_value();}
    bool editable()const noexcept{return bool(persistence_.commit);}
    std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t cameraRevision()const noexcept{return cameraRevision_;}
    // Advances even when already current: owner flushes an exact raster after
    // gesture completion without coupling geography work to pointer hover.
    std::uint64_t gestureRevision()const noexcept{return gestureRevision_;}
    MapActions actions()const noexcept;
    MapChange setActive(bool);
    void setReducedMotion(bool value)noexcept{reduced_=value;}
    MapChange down(core::Point);MapChange drag(core::Point);MapChange up(bool cancelled=false);
    MapChange rightDown(core::Point);
    MapChange perform(MapAction,std::string_view pinID={});
    MapChange key(MapKey);
    MapChange wheel(core::Point,double delta,bool precise,bool ended,double time);
    MapChange magnify(core::Point,double amount,bool ended,double time);
    MapChange advance(double time);
    MapChange endGesture();
    std::optional<double>nextWakeTime()const noexcept{return wheelDeadline_;}
private:
    MapSnapshot persisted_;MapPersistence persistence_;MapViewport viewport_;
    std::optional<std::string>selected_,error_;bool coordinates_{},active_{},reduced_{};
    struct Drag {core::Point start;MapViewport viewport;bool moved{};};
    std::optional<Drag>drag_;std::optional<double>wheelDeadline_;double time_{};
    std::uint64_t revision_{1},cameraRevision_{1},gestureRevision_{};
    void clock(double);bool commit(MapSnapshot);bool camera(MapViewport);
    void failure(std::string);bool hideCoordinates();MapChange finishWheel();
    MapChange action(MapAction,std::string_view);
    MapChange add(core::Point);MapChange remove(std::string_view);MapChange recenter(core::Point,MapViewport);
};
}
