#pragma once
#include "core/data/json.hpp"
#include "core/scene.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::core::packet {
using Json = ehud::data::Json;
enum class ErrorCode { invalid, unsupportedVersion, missing, tooLarge, integrity, unavailable };
class Error final : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};
struct Blob {
    std::string file, sha256;
    std::size_t bytes{};
    bool operator==(const Blob&) const = default;
};
struct MeshDescriptor {
    std::string id, sourceMesh;
    std::size_t vertexCount{}, indexCount{};
    Blob vertices, indices, originalVertexBuffer;
    // Original stride/offsets and additive source fields are retained here.
    Json metadata;
};
struct SourceVertex {
    std::array<float,4> position{}, color{};
    std::array<float,2> uv{};
};
struct MeshData {
    std::vector<SourceVertex> vertices;
    std::vector<std::uint32_t> indices;
};
struct MipDescriptor { unsigned level{}, width{}, height{}, rowBytes{}; Blob data; };
struct TextureDescriptor {
    std::string id, pixelFormat;
    unsigned width{}, height{};
    bool sRGB{};
    std::vector<MipDescriptor> mips;
    Json metadata; // exact sampler enum values and all source metadata
};
struct FrameDescriptor { std::string name; Blob data; };
struct UniformDescriptor {
    std::string passID, shaderKey, stage, bufferName;
    unsigned index{};
    std::size_t byteCount{};
    bool needsWorld{}, needsCamera{}, needsTime{};
    Blob data;
    Json metadata; // exact field offsets, values, dynamic plans and color flags
};
struct FrameData {
    Json metadata; // complete nodes/hits/batches/nativeLayers, including unknown fields
    Point viewport, canvasSize;
    Matrix4 projection, view, worldRoot;
    Matrix4 gpuViewProjection, gpuViewNoTranslationProjection;
    // Same order as metadata.batches; one descriptor vector per draw batch.
    std::vector<std::vector<UniformDescriptor>> uniforms;
};
struct BinaryData {
    // Existing bounded file primitive transfers its allocation without a second
    // texture-sized copy. Ownership stays with the caller during GPU upload.
    std::string storage;
    std::span<const std::uint8_t> bytes() const noexcept {
        return {reinterpret_cast<const std::uint8_t*>(storage.data()), storage.size()};
    }
};
// SHA-256 for integrity, not authenticity: native BCrypt on Windows,
// CommonCrypto on Mac, small FIPS-180-4 fallback for dependency-free Linux CI.
std::string sha256(std::span<const std::uint8_t> bytes);

// Read-only source package. No global root, user files, writes, clock, watcher,
// renderer, texture conversion, generic shader mapping, or retained binary cache.
// Constructor validates metadata/references, not unopened binary files. Content
// hashes, geometry values and indices are checked on each explicit load call.
class Package final {
public:
    static constexpr std::size_t maximumJSONBytes = 16*1024*1024;
    static constexpr std::size_t maximumBlobBytes = 128*1024*1024;
    static constexpr std::size_t maximumPackageBytes = 1024ull*1024*1024;
    explicit Package(std::filesystem::path explicitPackageRoot);
    const Json& metadata() const noexcept { return manifest_; }
    const std::map<std::string,MeshDescriptor,std::less<>>& meshes() const noexcept { return meshes_; }
    const std::map<std::string,TextureDescriptor,std::less<>>& textures() const noexcept { return textures_; }
    const std::map<std::string,Json,std::less<>>& materials() const noexcept { return materials_; }
    const std::map<std::string,FrameDescriptor,std::less<>>& frames() const noexcept { return frames_; }
    MeshData loadMesh(std::string_view id) const;
    BinaryData loadOriginalVertexBuffer(std::string_view id) const;
    BinaryData loadTextureMip(std::string_view id, unsigned level) const;
    FrameData loadFrame(std::string_view name) const;
    BinaryData loadUniformPayload(const UniformDescriptor&) const;
    Json loadAnimation() const;
    BinaryData loadShaderAsset(std::string_view file) const;
    BinaryData loadNativeRaster(std::string_view file) const;
    // Native renderer must explicitly support/map these exact source modes.
    // The loader never substitutes a simple textured quad for an unknown mode.
    std::vector<std::string> requiredPixelFormats() const;
    std::vector<std::string> requiredShaderKeys() const;
private:
    std::filesystem::path root_;
    Json manifest_;
    std::map<std::string,Blob,std::less<>> blobs_,shaderAssets_,nativeRasters_;
    std::set<std::string,std::less<>> portablePaths_;
    std::map<std::string,MeshDescriptor,std::less<>> meshes_;
    std::map<std::string,TextureDescriptor,std::less<>> textures_;
    std::map<std::string,Json,std::less<>> materials_;
    std::map<std::string,FrameDescriptor,std::less<>> frames_;
    Blob animation_;
    std::size_t referencedBytes_{};
    Blob registerBlob(const Json& object, std::size_t maximum = maximumBlobBytes);
    std::filesystem::path path(std::string_view relative) const;
    BinaryData read(const Blob&) const;
};
} // namespace endfield::core::packet
