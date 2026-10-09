#pragma once
#include "modules/map_state.hpp"
#include <array>

namespace endfield::modules {
using MapColor=std::array<double,4>; // authored straight color components
inline constexpr double mapDiameter=440,mapRadius=216,mapEdgeFeather=9;
struct MapRasterFrame {MapViewport viewport;core::Rect screenRect;double pixelsPerPoint{};};
struct MapImagePlacement {core::Point position;double scale{1};};
MapImagePlacement mapRasterPlacement(const MapRasterFrame&,MapViewport)noexcept;
std::array<MapImagePlacement,3>mapBackdropPlacements(MapViewport)noexcept;
struct MapRasterGeometry {
    MapViewport viewport;core::Rect screenRect,worldRect;core::Point origin;
    unsigned pixelDimension{};double pixelsPerPoint{},worldScale{},tolerance{};
};
// Mirrors the original worker's request guard and allocation cap. A detailed
// image is at most1536x1536, plus one1024x512 shared three-wrap backdrop.
std::optional<MapRasterGeometry>mapRasterGeometry(MapViewport,double contentsScale,double padding=128)noexcept;
bool mapNeedsPaint(const MapRasterFrame*,MapViewport,bool sameDataRevision,
                   bool interacting,bool exactRequested,double contentsScale)noexcept;
double mapRenderScale(double requested,bool interacting)noexcept;
double mapPaintCooldown(double elapsed)noexcept;

struct MapPinVisual {
    std::string_view id;core::Point position;MapPinStyle style{};MapColor color;
    bool selected{},pulses{};double staticHaloOpacity{};
};
struct MapPinVisuals {std::array<MapPinVisual,128>items{};std::size_t count{};};
MapPinVisuals mapPinVisuals(std::span<const MapPin>,MapViewport,
    std::optional<std::string_view>selected,bool active,bool reducedMotion,bool ambient);
struct MapPulse {double scale{},opacity{};};
// Authored basic/keyframe group: easeOut,1.7seconds,0/.12/1 opacity keys.
// Owner supplies each marker's retained animation start; no clock is created.
MapPulse mapPulse(double elapsed);
struct MapStyleTransition {double rotation{},opacity{1};};
MapStyleTransition mapStyleTransition(double elapsed,bool reducedMotion=false);
struct MapPlayerAsset {std::string_view path;core::Rect rect;double opacity;bool screenBlend;};
std::array<MapPlayerAsset,3>mapPlayerAssets()noexcept;
MapColor mapPinColor(MapPinStyle)noexcept;
struct MapChromeGeometry {
    core::Rect heading{122,37,196,17},status{115,55,210,13};
    core::Rect coordinates{100,301,211,26},coordinateText{106,309,202,15};
    core::Rect message{97,330,246,22};
};
struct MapChromeStyle {MapColor background,ink,plate,coordinateBackground;};
MapChromeStyle mapChromeStyle(bool dark)noexcept;
std::string_view mapActionSymbol(MapAction)noexcept;
// Source paths and authored typography are preserved. User-facing labels are
// supplied by the shared locale catalog; no new text engine/font resolver.
struct MapChromeText {std::string heading,loading,pins;};
std::string mapZoomStatus(MapViewport,std::size_t pins,bool terrainReady,bool loadsTerrain,const MapChromeText&);
}
