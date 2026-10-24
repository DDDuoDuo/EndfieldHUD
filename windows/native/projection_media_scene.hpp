#pragma once
#include "modules/projection_model.hpp"
#include "modules/hud_design_text.hpp"
#include "native/layer_scene.hpp"
#include <memory>
namespace endfield::native {
struct ProjectionMediaAppearance {
    modules::NotesColor accent{.98,.83,.12,1};bool playing{};double seconds{};
    std::optional<std::string> error;
};
// The Mac 1.3.0 ProjectionMediaNode face and overlay (ProjectionWorkspaceView
// .swift:633-721): the dark #141414 .9 face with a 1 px #3D3D3D ring in both
// themes, the theme-coloured corner mark, the 10.5 px bold #EEEEEE title
// (tracking .04), the x / play / pause icons, the 10 px bold #D9D9D9 tabular
// time and the theme-coloured progress fill, in the design library's text
// and icon vocabulary (`text`: the HUD's text services). Decoded content
// remains a borrowed provider draw between them. No image/codec or file access.
struct ProjectionMediaArtwork {ehud::data::Json face,overlay;};
ProjectionMediaArtwork projectionMediaArtwork(const modules::ProjectionMediaItem&,const ProjectionMediaAppearance&,const modules::design::TextServices&);
// Background first, then four nonoverlapping inside border strips. The face
// stays a fixed 20-vertex mesh at every card size; no card-sized bitmap exists.
struct ProjectionMediaFaceMesh {std::array<Vertex,20>vertices;std::array<std::uint32_t,30>indices;};
ProjectionMediaFaceMesh projectionMediaFaceMesh(core::Point size);
#ifdef _WIN32
class NativeProjectionMediaScene final {
public:
    NativeProjectionMediaScene(LayerRasterizer&,LayerRasterOptions,modules::design::TextServices);
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
