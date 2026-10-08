#pragma once
#include "core/source_camera.hpp"
#include "native/source_graphics.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
struct SourceFrameParameters {
    core::source::GPUCamera camera;
    float timeSeconds{},renderPathInjected{1},flipX{},flipY{};
    unsigned width{},height{};
    std::optional<std::array<float,3>> desktopAccentLinear;
};
struct SourceUniformOverride {
    std::string name;
    std::vector<float> value;
    bool operator==(const SourceUniformOverride&) const = default;
};
struct SourceBatchState {
    // Source identities are immutable; a new structure requires explicit load.
    std::string sourceNodeID,sourceMesh,material;
    core::Matrix4 world;
    std::array<float,4> vertexColor{1,1,1,1}; // exact submitted linear source tint
    bool visible{true};
    // Fixed names/widths from the source frame; values may change in-place,
    // including the circle's world-to-local clip matrix during gyro motion.
    std::vector<SourceUniformOverride> uniformOverrides;
};
struct SourceSceneStats {
    // Payload buffers/field values; excludes allocator/container overhead.
    std::size_t batches{},draws{},meshes{},textures{},pipelines{},uniforms{},retainedCPUBytes{},inactiveUniforms{};
    std::uint64_t updates{},uniformEncodes{},changedUniforms{},changedMeshes{},visibilityChanges{};
};
struct SourceSceneProvenance {
    std::string packetSHA256,frameSHA256,shaderManifestSHA256,frameName;
    bool operator==(const SourceSceneProvenance&) const = default;
};
struct CompiledSourceScene {
    std::filesystem::path file;
    // Optional shipping-manifest pin for the complete compiled artifact.
    // Embedded payload hashes provide integrity, not authenticity.
    std::string expectedSHA256;
};
struct SourceUniformPayload {
    std::string_view id;
    std::span<const std::uint8_t> bytes;
    bool active{true};
};
struct SourceGeometryPayload {
    std::string_view id;unsigned stride{};std::uint64_t revision{};
    std::span<const std::uint8_t> vertices;std::span<const std::uint32_t> indices;
};
struct SourceFieldSchema {std::string name;unsigned offset{},components{};bool isColor{},dynamic{};};
struct SourceBufferSchema {SourceStage stage;unsigned slot{};std::size_t byteCount{};std::vector<SourceFieldSchema> fields;};
struct SourceTextureProperty {std::string name,textureID;SourceStage stage;unsigned textureSlot{},samplerSlot{};};
struct SourcePassTemplate {
    std::string pipelineID;unsigned vertexStride{},stencilReference{};
    std::vector<SourceAttribute> attributes;
    std::vector<SourceBufferSchema> buffers;
    std::vector<SourceTextureProperty> textures;
};
struct SourceGeometryView {
    unsigned stride{80};std::span<const std::uint8_t> vertices;
    std::span<const std::uint32_t> indices;
    // If empty, use the native color field at byte32. No tint is baked here.
    std::span<const std::array<float,4>> originalColors;
};
struct SourceImageGeometryView {
    std::span<const std::array<float,4>> positions;
    std::span<const std::array<float,2>> uv;
    std::span<const std::uint32_t> indices;
};
struct SourceBatchTemplate {
    std::string id,material;SourceBatchState originalState;
    SourceGeometryView originalGeometry;
    std::vector<SourcePassTemplate> passes;
    bool logicalMetadataComplete{},appliesDesktopAccent{true};
};
struct SourceIndexRange {unsigned firstIndex{},indexCount{};};
struct SourceTextureOverride {std::string name,textureID;};
struct SourceAssembledBatch {
    // Host identity is stable and independent of source node/mesh names.
    // prototypeID explicitly selects an already validated exact source pass
    // group. No automatic material approximation or unknown resource fallback.
    std::string stateID,prototypeID;SourceBatchState state;
    std::optional<SourceGeometryView> geometry;
    std::optional<SourceImageGeometryView> imageGeometry;
    std::optional<SourceIndexRange> indexRange; // null: complete source triangles
    std::optional<unsigned> stencilReference; // null: original pass references
    bool appliesDesktopAccent{true};
    std::vector<SourceTextureOverride> textureOverrides;
};
// Source directLDR materials and field writers, compiled once from the current
// Mac package. Explicit roots only. Constructor validates all used hashes and
// initial reconstructed UBOs; update/flush perform no I/O, JSON, hashing, clocks,
// watcher/service calls, shader conversion or texture allocation.
class SourceScene final {
public:
    static constexpr std::size_t maximumCompiledBytes=128*1024*1024;
    static constexpr unsigned compiledSchemaVersion=4;
    SourceScene(std::filesystem::path explicitPacketRoot,std::filesystem::path compiledShaderManifest,std::string frameName);
    explicit SourceScene(CompiledSourceScene);
    ~SourceScene();
    SourceScene(const SourceScene&)=delete;
    SourceScene& operator=(const SourceScene&)=delete;
    const SourceFrameParameters& parameters() const noexcept;
    std::span<const SourceBatchState> batches() const noexcept;
    const std::vector<std::string>& shaderKeys() const noexcept;
    const SourceSceneProvenance& provenance() const noexcept;
    // Exact source frame/root/compiler chain for every installed catalog.
    std::span<const SourceSceneProvenance> catalogProvenance() const noexcept;
    std::span<const SourceBatchTemplate> templates() const noexcept;
    // Retained slot order, used by update(). Inactive slots remain bounded and
    // reusable; frame draw order is supplied independently by assembleFrame().
    std::span<const std::string> stateIDs() const noexcept;
    // Initial resource install only, before upload/submission. Every input is
    // another validated SourceScene; its prototype namespace is explicit.
    // No runtime disk read or guessed unknown-material fallback occurs here.
    void includeTemplates(const SourceScene&,std::string explicitNamespace);
    // Build-only texture closure for the exported desktop resource inventory.
    // Reads source frameBuilder image/sprite and mounted raw-image /
    // soft-mask references, independent of a sampled frame's alpha/visibility.
    // Shader-bound material defaults already belong to the exact templates;
    // inactive material properties do not become extra texture dependencies.
    // Desktop replacement images/sprites must exist when used as GPU textures.
    // Broader original game-only
    // catalog references are included only when exported in this packet. This
    // does not authorize making unexported game widgets visible at runtime.
    // The packet manifest must match this scene's primary provenance exactly.
    // Stages and validates all new raw mips before mutation; returns the sorted
    // exported dependency IDs (including any already installed). No new poses,
    // templates, pipelines or runtime I/O are introduced.
    std::vector<std::string> includeDesktopResources(const std::filesystem::path& explicitPacketRoot);
    // Complete typed submission. Input validation finishes before mutation.
    // Existing slots/resources are reused; adding a stable identity may create
    // retained mesh/UBO slots, but never compiles/creates a shader or texture.
    // The available prototype/material/texture set is explicit, not universal.
    bool assembleFrame(std::span<const SourceAssembledBatch>,const SourceFrameParameters&);
    bool updateGeometry(std::string_view stateID,const SourceGeometryView&,std::optional<SourceIndexRange> = {});
    // Same80-byte registerGeometry defaults as Mac: white original colors,
    // zero padding/normal/uv1; original image winding/UV/positions untouched.
    bool updateGeometry(std::string_view stateID,const SourceImageGeometryView&,std::optional<SourceIndexRange> = {});
    // Build-only explicit export. Existing output is rejected; used raw mips,
    // programs, geometry and ordered field plans are preserved losslessly,
    // including explicitly installed catalogs. Runtime snapshots are rejected.
    void writeCompiled(const std::filesystem::path& newAbsoluteOutput) const;
    // CPU-only retained update. A complete, stable batch span is required.
    // World/color/visibility and submitted GPU camera/time are authoritative.
    // Invalid input is rejected before changing any scene state. No per-update
    // geometry/texture read or upload occurs. Native layers belong to the host.
    bool update(std::span<const SourceBatchState>,const SourceFrameParameters&);
    // Initial upload; then flush only dirty buffers and draw visibility.
    // The caller owns the renderer lifecycle and calls on its owner thread.
    // A fresh/reset graphics target needs upload again, explicitly.
    void upload(SourceGraphics&);
    void flush(SourceGraphics&);
    SourceSceneStats stats() const noexcept;
    std::vector<SourceUniformPayload> uniformPayloads() const; // diagnostics only
    std::vector<SourceGeometryPayload> geometryPayloads() const; // diagnostics only
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
