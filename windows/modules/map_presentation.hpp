#pragma once
#include "modules/map_state.hpp"
#include <array>
#include <vector>

namespace endfield::modules {
using MapColor=std::array<double,4>; // authored straight color components
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

// WorldMapSurface.updatePins (:428-464): the pins on the disc (out to its
// 430 px rim, in the raster's unit space mapScreen) are drawn and fade with
// it; while active (and not Reduce Motion, ambient motion on) up to 12 pulse,
// the selected one first, then the newest; the others' ring .65 selected /
// .25. The selected marker is drawn above the others.
struct MapPinVisual {
    std::string_view id;core::Point position;MapPinStyle style{};MapColor color;
    bool selected{},pulses{};double staticHaloOpacity{};
};
struct MapPinVisuals {std::array<MapPinVisual,128>items{};std::size_t count{};};
bool mapPinVisible(core::Point unitPoint)noexcept;
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
// The header scrim (WorldMapSurface.setHeaderScrim, :183-195): one cached
// 1-px gray column from the square's top to `bottom` map px at gray .04
// alpha .5, then fading to 0 over `feather` px; straight sRGB rows, top down.
struct MapScrim {unsigned rows{};std::vector<std::uint8_t>straightRGBA;double height{};};
MapScrim mapHeaderScrim(double bottom,double feather=32,double alpha=.5);
}
