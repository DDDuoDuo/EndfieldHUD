#pragma once
#include "native/map_raster.hpp"
#include "core/data/json.hpp"
#include <string>

namespace endfield::native {
struct MapAppearance {
    bool dark{true},reducedMotion{},ambient{true},terrainReady{},loadsTerrain{true};
    modules::MapColor accent{.98,.87,.13,1};
    modules::MapChromeText strings{"MAP","LOADING TERRAIN","PINS"};
};
enum class MapChromeRole {artwork,tint,rim};
struct MapChromeSurface {
    std::string id;ehud::data::Json content;core::Matrix4 local;
    MapChromeRole role{};std::optional<modules::MapAction>action;
};
// Content-event source decomposition. Feedback remains separate from text and
// plates; ordinary pointer/tilt frames never create or traverse JSON.
std::vector<MapChromeSurface>prepareMapChrome(const modules::MapState&,const MapAppearance&);
struct MapPlayerImages {
    std::shared_ptr<const MapPaintImage>halo,beam,glyph;
    // Original detached 440-point CA mask at2x. Only alpha is sampled.
    std::shared_ptr<const MapPaintImage>feather;
};
#ifdef _WIN32
class LayerRasterizer;class LayerScene;class Renderer;struct LayerRasterOptions;struct LayerCompositionEntry;struct PlaneMask;struct PlaneShutter;
struct MapScenePose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>masks;const PlaneShutter*shutter{};};
struct MapSceneStats {std::uint64_t chromeBuilds{},statusRasters{},imageUploads{},pinBuilds{},poses{},localUpdates{};std::size_t visiblePins{},pulsingPins{};};
class NativeMapScene final {
public:
    NativeMapScene(modules::MapState&,NativeMapRaster&,LayerRasterizer&,LayerRasterOptions,MapPlayerImages,MapAppearance={});
    ~NativeMapScene();
    bool setAppearance(MapAppearance);
    // Each visible marker owns a bounded48x48 or48x132 local encoded group;
    // screen blending and .16s rotation/group opacity precede Map composition.
    // State/raster callbacks only. Animated camera transitions use the actual
    // current numeric pose as interruption origin. No IO/worker is performed.
    bool syncContent(double time,bool cameraAnimated=false);
    bool setFeedback(std::optional<core::Point>,bool pressed,double time);
    bool updatePose(const MapScenePose&);bool uploadResources(Renderer&);
    bool requiresFrames(double time)const;void settle();
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool releaseResources(Renderer&);MapSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
