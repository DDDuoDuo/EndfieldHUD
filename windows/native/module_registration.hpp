#pragma once
#include "native/module_scene.hpp"

namespace endfield::native {
struct ModuleRegistrationMesh {
    // Six original brackets, five disjoint quads each. Degenerate quads retain
    // fixed topology at collapsed endpoints; no per-frame storage growth.
    std::array<Vertex,120> vertices{};
    std::array<std::uint32_t,180> indices{};
};
// Original open four-point, axis-aligned bracket strokes only. Butt caps and
// 90-degree miter joins (well below the source's default miter limit). Returns
// non-overlapping filled triangles; unsupported stroke topology is rejected.
// Geometric coverage is distinct from the still-unverified CA antialiasing.
ModuleRegistrationMesh moduleRegistrationMesh(const core::RegistrationPath&,double lineWidth);

struct ModuleRegistrationStats {std::uint64_t geometryRevision{1},geometryUpdates{},meshUploads{},numericUpdates{};};
// Caller owns the Renderer and one LayerComposition. Upload this object's mesh
// before setEntries, then use draws() as its associated scene's `after` span.
// The one DrawObject's storage/IDs are stable even when inactive (opacity0).
// Parent/wrapper/opacity/clip changes never rebuild or upload mesh geometry.
// Only finite local path/width changes require uploadGeometry(). No texture,
// rasterization, publisher, clock, service or device is created here.
// Retire by removing its borrowed draw span from the composition BEFORE calling
// releaseResources; both Renderer and adapter must outlive the borrowed entry.
class NativeModuleRegistration final {
public:
    explicit NativeModuleRegistration(std::string sourceID);
    NativeModuleRegistration(const NativeModuleRegistration&)=delete;
    NativeModuleRegistration& operator=(const NativeModuleRegistration&)=delete;
    bool update(const std::optional<ModuleRegistrationPlacement>&); // true iff mesh geometry changed
    bool uploadGeometry(Renderer&); // safe explicit retry; Renderer checks revision
    bool releaseResources(Renderer&); // false while the one mesh is published
    std::span<const DrawObject> draws()const noexcept{return draws_;}
    const ModuleRegistrationMesh& mesh()const noexcept{return mesh_;}
    ModuleRegistrationStats stats()const noexcept{return stats_;}
private:
    std::array<DrawObject,1> draws_;
    ModuleRegistrationMesh mesh_;
    std::optional<core::RegistrationPath> previousPath_;
    double previousWidth_{};
    Renderer* resourceOwner_{};
    ModuleRegistrationStats stats_;
};
} // namespace endfield::native
