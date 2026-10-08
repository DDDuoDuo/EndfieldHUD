#pragma once
#include "core/source_watch_layout.hpp"

namespace endfield::core::source {
struct SourceSprite {
    Vec2 size{};
    std::array<double,4> padding{},border{},outer{},inner{}; // left,bottom,right,top
    double pixelsPerUnit{100};
    std::string textureID;
    static SourceSprite fromSource(const Json& sprite,const Json& texture);
    bool operator==(const SourceSprite&) const=default;
};
struct SourceImageParameters {
    int type{},fillMethod{},fillOrigin{};
    bool useSpriteMesh{},preserveAspect{},fillCenter{true},fillClockwise{true};
    double pixelsPerUnitMultiplier{1},fillAmount{1};
    static SourceImageParameters fromComponent(const WatchComponent&);
    bool operator==(const SourceImageParameters&) const=default;
};
struct SourceImageMesh {
    std::vector<std::array<float,4>> positions;
    std::vector<std::array<float,2>> uv;
    std::vector<std::uint32_t> indices;
    void reset() noexcept; // retains scratch storage
    void release() noexcept;
};
struct ImageGeometryStats {std::uint64_t builds{},reuses{};std::size_t retainedBytes{};};
// Direct HUDSourceImageGeometry.swift port: Double calculations, Float storage,
// original trim/UV/winding, and the source's16,250-quad limit for each tile cell.
// Typed input and retained scratch only; no source JSON parsing during build,
// GPU, clock, services or implicit scene rebuild. Invalid/nonfinite geometry
// throws and clears the current result; a successful result lasts until the
// next changed build/reset/release. Identical input returns it without work.
class ImageGeometryBuilder final {
public:
    static constexpr std::size_t maximumCellQuads=16250,maximumQuads=9*maximumCellQuads;
    const SourceImageMesh& build(const SourceImageParameters&,const SourceSprite*,const SourceRect&,
        Vec2 pivot,double canvasReferencePPU=100,std::optional<double> fillAmount={});
    const SourceImageMesh& mesh() const noexcept {return mesh_;}
    void reset() noexcept;
    void release() noexcept;
    ImageGeometryStats stats() const noexcept;
private:
    struct Key {
        SourceImageParameters image;std::optional<SourceSprite> sprite;SourceRect rect;Vec2 pivot;
        double canvasPPU{};std::optional<double> fill;
        bool operator==(const Key&) const=default;
    };
    SourceImageMesh mesh_;
    std::optional<Key> previous_;
    ImageGeometryStats counts_;
    void quad(const std::array<Vec2,4>& positions,const std::array<Vec2,4>& texture);
    void radial(const std::array<Vec2,4>& positions,const std::array<Vec2,4>& texture,double amount,bool invert,int corner);
};
} // namespace endfield::core::source
