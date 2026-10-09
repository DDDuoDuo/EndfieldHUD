#pragma once
#include "core/scene.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
// Original archive coordinates, in a 1024x512 equirectangular world. Preserve
// every decoded vertex and closed flag; holes use even-odd fill, not winding.
struct MapRing {std::vector<core::Point>points;bool closed{};core::Rect bounds;};
using MapPath=std::vector<MapRing>;
struct MapTerrainBand {int elevation{};MapPath contourPath;std::optional<MapPath>fillPath;};
struct MapTerrain {
    static constexpr std::size_t maximumBytes=3*1024*1024,maximumVertices=150000;
    std::vector<MapTerrainBand>bands;std::size_t vertexCount{};
    static MapTerrain decode(std::span<const std::uint8_t>);
};
struct MapCountryComponent {MapPath path;core::Rect bounds;};
struct MapCountry {std::string id,name;MapPath path;core::Rect bounds;std::vector<MapCountryComponent>components;};
struct MapCountries {
    static constexpr std::size_t maximumBytes=1024*1024,maximumVertices=100000;
    std::vector<MapCountry>countries;std::size_t vertexCount{};
    static MapCountries decode(std::span<const std::uint8_t>);
};
// Each optional dataset independently supports the source's partial fallback.
// Owners publish as shared_ptr<const MapGeography>; decoding performs no IO.
struct MapGeography {std::optional<MapTerrain>terrain;std::optional<MapCountries>countries;};
bool mapEvenOddContains(const MapPath&,core::Point)noexcept;
struct MapClippedGeometry {MapPath fillPath,linePath;};
// Original64-segment spatial chunks and two-level LRU simplification. Paint
// worker owns each cache serially. Clipped line paths contain only geographic
// edges; viewport-created polygon sides never become coastline strokes.
class MapPathGeometry final {
public:
    explicit MapPathGeometry(const MapPath&);
    ~MapPathGeometry();
    MapPathGeometry(MapPathGeometry&&)noexcept;
    MapPathGeometry&operator=(MapPathGeometry&&)noexcept;
    MapPathGeometry(const MapPathGeometry&)=delete;
    MapPathGeometry&operator=(const MapPathGeometry&)=delete;
    MapPath clippedLines(core::Rect,double tolerance)const;
    MapClippedGeometry clippedPolygon(core::Rect,double tolerance);
    std::size_t cachedLevels()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}
