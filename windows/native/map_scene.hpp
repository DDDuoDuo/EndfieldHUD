#pragma once
#include "native/map_raster.hpp"
#include "core/data/json.hpp"
#include "core/map_source_cutout.hpp"
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
// cutoutMasks: in-place feather mask region updates for the source plates
// above the Map (setCutouts); cutoutPolygons: the plates' polygons now cut;
// cutoutMaskBytes: the retained CPU staging of those updates.
// transitionCutUploads: creations and in-place region updates of the module
// host's transition cut (setTransitionCut); transitionCutBytes: its texels
// resident now (0 when released).
struct MapSceneStats {std::uint64_t chromeBuilds{},statusRasters{},imageUploads{},pinBuilds{},poses{},localUpdates{},cutoutMasks{},transitionCutUploads{};std::size_t visiblePins{},pulsingPins{},cutoutPolygons{},cutoutMaskBytes{},transitionCutBytes{};};
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
    // Desktop Map occlusion (core/map_source_cutout.hpp): the source plates
    // drawn above the Map, as polygons of its 440-point canvas. The feather
    // mask the output samples is an updatable texture: uploadResources
    // replaces in place only the texels the old and new plates cover (alpha
    // x (1 - even-odd plate coverage)); equal plates do nothing. True when
    // the plates changed. No allocation once the staging has grown.
    bool setCutouts(const modules::MapCutouts&);
    // Module transition cut (SystemHUDView.applySourceMapOcclusion masks the
    // whole module host - the outgoing and incoming content and the incoming
    // wrapper's registration brackets - while the Map is presented, its
    // switches included): the plates as polygons of the 440-point module
    // host plane, which the transition transforms do not move. While `wanted`
    // (a switch into or out of the Map) uploadResources keeps an updatable
    // 880 x 880 alpha texture (two texels per point like the feather, so the
    // same plate edges; alpha 1 - even-odd plate coverage, no feather; 3.1 MB)
    // in step, repainting in place only the texels that change. It is created
    // by the first switch and kept until releaseTransitionCut (false while a
    // published draw still samples it) or releaseResources. transitionCutTexture() names it while it holds
    // the wanted plates (after uploadResources), otherwise null.
    bool setTransitionCut(const modules::MapCutouts& host,bool wanted);
    const std::string* transitionCutTexture()const noexcept;
    bool releaseTransitionCut(Renderer&);
    // Finite motion only - camera moves, toolbar feedback and marker style
    // transitions - which the host presents at the display rate. The pin
    // pulse is ambient motion (the Mac runs it as a repeating compositor
    // animation): pulsing() reports it, and it is sampled at whatever frame
    // presents it, on the same clock, so its curve is unchanged.
    bool requiresFrames(double time)const;void settle();
    bool pulsing()const noexcept;
    std::span<const LayerCompositionEntry>entries()const noexcept;
    bool releaseResources(Renderer&);MapSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
