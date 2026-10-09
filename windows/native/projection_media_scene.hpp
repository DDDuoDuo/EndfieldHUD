#pragma once
#include "modules/projection_model.hpp"
#include "native/layer_scene.hpp"
#include <memory>
namespace endfield::native {
struct ProjectionMediaAppearance {
    modules::NotesColor accent{.98,.83,.12,1};bool playing{};double seconds{};
    std::optional<std::string> error;
};
// The original ProjectionMediaNode face and overlay; decoded content remains
// a borrowed provider draw between them. No image/codec or file access here.
struct ProjectionMediaArtwork {ehud::data::Json face,overlay;};
ProjectionMediaArtwork projectionMediaArtwork(const modules::ProjectionMediaItem&,const ProjectionMediaAppearance&);
// Background first, then four nonoverlapping inside border strips. The face
// stays a fixed 20-vertex mesh at every card size; no card-sized bitmap exists.
struct ProjectionMediaFaceMesh {std::array<Vertex,20>vertices;std::array<std::uint32_t,30>indices;};
ProjectionMediaFaceMesh projectionMediaFaceMesh(core::Point size,modules::NotesColor accent);
#ifdef _WIN32
class NativeProjectionMediaScene final {
public:
    NativeProjectionMediaScene(LayerRasterizer&,LayerRasterOptions);
    ~NativeProjectionMediaScene();
    bool sync(const modules::ProjectionMediaItem&,const ProjectionMediaAppearance&);
    // Borrowed resident provider geometry, including masks, is card-local.
    // This scene never uploads/removes the provider's mesh or texture.
    void setDraw(std::optional<DrawObject>);
    void setPose(const core::Matrix4& logicalToPhysical);
    void setFeedback(std::optional<std::string_view>,bool pressed,bool reduced,double time);
    void update(double time);bool requiresFrames(double time)const;
    void upload(Renderer&);std::array<LayerCompositionEntry,2>entries();
    bool releaseResources(Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
