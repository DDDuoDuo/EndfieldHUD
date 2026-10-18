#pragma once
#include "core/source_canvas.hpp"
#include "core/source_image_geometry.hpp"
#include <memory>

namespace endfield::core::source {
using SourceFloat4=std::array<float,4>;
using SourceFrameTints=std::map<std::string,SourceFloat4,std::less<>>;
using SourceFrameUniforms=std::map<std::string,std::vector<float>,std::less<>>;
struct SourceDesktopImage {
    std::string texture;
    std::array<float,2> size{};
    std::optional<Vec2> displaySize;
    bool operator==(const SourceDesktopImage&)const=default;
};
struct SourceDesktopGraphicStyle {
    std::optional<std::array<float,3>> tint;
    float opacity{1};
    bool operator==(const SourceDesktopGraphicStyle&)const=default;
};
struct SourceDesktopFrameSettings {
    std::set<std::string,std::less<>> hiddenNodes,normalMaterialNodes;
    std::map<std::string,std::map<std::string,double,std::less<>>,std::less<>> properties;
    std::map<std::string,std::string,std::less<>> sprites;
    std::map<std::string,SourceDesktopImage,std::less<>> images;
    std::map<std::string,SourceDesktopGraphicStyle,std::less<>> graphicStyles;
    static SourceDesktopFrameSettings fromJson(const Json&);
    bool operator==(const SourceDesktopFrameSettings&)const=default;
};
// Immutable metadata compiled from animation.frameBuilder. No renderer, file,
// JSON parsing or platform service is required by frame presentation.
struct SourceWatchFrameResources {
    struct PropertyType {int type{},flags{};};
    std::map<std::string,SourceSprite,std::less<>> sprites,sourceSprites;
    std::map<std::string,std::array<float,2>,std::less<>> textureSizes;
    std::map<std::string,SourceFrameUniforms,std::less<>> materialVectors;
    std::set<std::string,std::less<>> namedMaterials,profileNodeIDs;
    // Exact authored ambient rotation channels plus desktop triangle bindings.
    // Missing in older packets: ordinary build remains supported.
    std::set<std::string,std::less<>> ambientRotationNodes;
    std::map<std::string,std::array<std::optional<std::string>,4>,std::less<>> materialVariants; // clip*2+soft
    std::map<std::string,std::map<std::string,PropertyType,std::less<>>,std::less<>> materialPropertyTypes;
    std::map<std::string,std::string,std::less<>> sourceMeshNames;
    SourceFrameTints defaultSelectableTints;
    static SourceWatchFrameResources fromJson(const Json&);
};
struct SourceFrameGeometry {
    SourceImageMesh mesh; // Canvas-local Float position4, original UV2 and winding
    std::uint64_t revision{};
};
struct SourceWatchBatch {
    std::string stateID,sourceNodeID,sourceMesh,material;
    Matrix4 world; // source double -> Float once, retained in a portable matrix
    SourceFloat4 color{1,1,1,1};
    SourceFrameUniforms uniformOverrides;
    std::map<std::string,std::string,std::less<>> textureOverrides;
    bool appliesDesktopAccent{true};
    // Null means an original MeshFilter mesh named by sourceMesh. UI geometry
    // belongs to the builder and has the original registerGeometry defaults.
    const SourceFrameGeometry* geometry{};
};
struct SourceWatchHit {
    struct Mask {SourceRect rect;Matrix4 world;};
    std::string graphicID,buttonID;
    SourceRect rect;
    Matrix4 world;
    std::vector<Mask> masks;
};
struct SourceWatchFrame {
    std::span<const ResolvedNode> resolved;
    std::span<const double> inheritedAlpha;
    std::vector<SourceWatchBatch> batches;
    std::vector<SourceWatchHit> hits;
    WatchLayout::Report layoutReport;
    std::vector<std::string> diagnostics;
    // Exact source near/far segment test, reverse draw order and mask stack.
    // Uses logical CPU projection*view (before the GPU-only Y conversion).
    std::optional<std::string_view> buttonAt(Point,const Matrix4& viewProjection,Rect viewport)const noexcept;
};
struct SourceWatchFrameStats {
    std::uint64_t builds{},reusedFrames{},worldOnlyFrames{},layoutBuilds{},layoutReuses{},localImageBuilds{},localImageReuses{},bakedGeometryBuilds{},ambientFrames{},directAmbientFrames{},ambientResolvedNodes{};
    std::size_t lastAmbientResolvedNodes{};
};
// Desktop-only original FrameBuilder: includeDomain=false, includeSourceText=
// false and widgets=nil. Scene/document/resources are borrowed and immutable.
// The result and its spans/geometry pointers last until the next changed build
// or settings update. Caller owns animation/scroll clocks and event demand.
class SourceWatchFrameBuilder final {
public:
    SourceWatchFrameBuilder(const SceneDefinition&,const MountedLayoutDocument&,const SourceWatchFrameResources&,
        SourceDesktopFrameSettings={});
    ~SourceWatchFrameBuilder();
    SourceWatchFrameBuilder(const SourceWatchFrameBuilder&)=delete;
    SourceWatchFrameBuilder& operator=(const SourceWatchFrameBuilder&)=delete;
    void setDesktopSettings(SourceDesktopFrameSettings);
    const SourceWatchFrame& build(const Pose&,const Matrix4& worldRoot,double scroll=1,
        const DesktopNavigationLayout* navigation=nullptr,const SourceFrameTints& selectableTints=emptyTints(),bool forceRebuild=false);
    // Binds a rotation-only sample to the last full/pointer presentation. A
    // changed size, root, scroll, navigation, tint, settings or token rejects
    // without mutation. Caller falls back to build() using its complete pose.
    // Success visits only authored ambient descendants and retained batches.
    const SourceWatchFrame* buildSettledAmbient(const Pose& rotationOnly,std::uint64_t expectedRevision,
        const Matrix4& worldRoot,Vec2 canvasResolution,double scroll=1,
        const DesktopNavigationLayout* navigation=nullptr,const SourceFrameTints& selectableTints=emptyTints());
    std::uint64_t presentationRevision()const noexcept;
    SourceWatchFrameStats stats()const noexcept;
    static const SourceFrameTints& emptyTints()noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::core::source
