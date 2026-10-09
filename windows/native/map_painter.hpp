#pragma once
#include "modules/map_presentation.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace endfield::modules {struct MapGeography;}
namespace endfield::native {
struct MapPaintImage {unsigned width{},height{};std::vector<std::uint8_t>straightRGBA;};
struct MapPaintRequest {
    // Geography comes from the bounded source archive decoders. Accent is
    // already converted to sRGB, matching WorldMapRasterPainter.components.
    std::shared_ptr<const modules::MapGeography>geography;
    modules::MapViewport viewport;bool dark{true};modules::MapColor accent{.98,.87,.13,1};
    double contentsScale{2};bool backdrop{};std::uint64_t revision{};
};
struct MapPaintResult {
    std::shared_ptr<const MapPaintImage>detail,backdrop;
    std::optional<modules::MapRasterFrame>frame;
    double workSeconds{}; // measured entire paint, including optional backdrop
};
using MapPaintCancelled=std::function<bool()>;
using MapPaintFunction=std::function<MapPaintResult(const MapPaintRequest&,const MapPaintCancelled&)>;
// Confined to the application's existing UtilityExecutor worker. Holds only
// immutable input and portable indexed paths across calls; COM/bitmap objects
// never escape a paint's apartment. No device, window, queue or timer.
class NativeMapPainter final {
public:
    NativeMapPainter();~NativeMapPainter();
    NativeMapPainter(const NativeMapPainter&)=delete;
    NativeMapPainter&operator=(const NativeMapPainter&)=delete;
    MapPaintResult paint(const MapPaintRequest&,const MapPaintCancelled&);
    void clear(); // worker-only while running; safe destructor after last task
    struct Stats {std::size_t countryIndexes{},terrainIndexes{},fillIndexes{},cachedLevels{};std::uint64_t paints{},backdrops{};};
    Stats stats()const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
