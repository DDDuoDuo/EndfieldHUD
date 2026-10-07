#pragma once

#include "core/scene.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::native {

enum class Driver { hardware, warpForTests };
enum class TextureColorSpace { sRGB, linear };
enum class TextureFilter { nearest, linear };
enum class RenderTarget { composition, offscreenForTests };
struct RendererOptions {
    Driver driver{Driver::hardware};
    std::filesystem::path shaderPath;
    RenderTarget target{RenderTarget::composition};
};
struct Vertex {
    std::array<float, 3> position{};
    std::array<float, 2> uv{}; // top-left texture origin
    std::array<float, 4> linearColor{1, 1, 1, 1}; // straight linear RGBA
};
struct MeshData {
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;
};
struct TextureData {
    std::uint32_t width{}, height{};
    std::span<const std::uint8_t> straightRGBA;
    TextureColorSpace colorSpace{TextureColorSpace::sRGB};
    TextureFilter filter{TextureFilter::linear};
};
struct PlaneMask {
    core::Matrix4 worldToLocal;
    core::Rect bounds;
};
struct DrawObject {
    std::string sourceID, meshID, textureID; // empty textureID selects opaque white
    core::Matrix4 world;
    std::array<float, 4> linearTint{1, 1, 1, 1};
    float opacity{1};
    // Up to eight intersecting plane-local rectangular masks. Shutter polygons
    // and source stencil/soft-mask programs are deliberately outside this API.
    std::vector<PlaneMask> masks;
};
struct RendererStats {
    std::size_t meshes{}, textures{}, objects{}, resourceBytes{};
    std::uint64_t meshUploads{}, textureUploads{}, objectUploads{}, objectBufferAllocations{}, cameraUploads{}, drawCalls{}, presents{};
    bool initialized{};
};
struct Readback {
    std::uint32_t width{}, height{}, rowBytes{};
    // BGRA8 encoded-sRGB premultiplied bytes from this application's own target.
    std::vector<std::uint8_t> pixels;
};
class RendererError : public std::runtime_error {
public:
    RendererError(std::string operation, std::int32_t code);
    std::int32_t code() const noexcept { return code_; }
private:
    std::int32_t code_;
};

// A native presentation foundation, not an implementation of original game
// materials, source HDR tone mapping, text, or module layout. Geometry and
// textures stay resident until explicitly replaced/removed. All calls belong
// to the creating UI thread. The class starts no clock, worker, or capture API.
class Renderer final {
public:
    static constexpr std::size_t maximumMeshes = 4096, maximumTextures = 2048, maximumObjects = 16384;
    static constexpr std::size_t maximumResourceBytes = 512 * 1024 * 1024;
    static constexpr std::size_t maximumRenderPixels = 4096 * 4096;
    Renderer();
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    // HWND is passed opaquely to keep the public draw API independent of Win32
    // headers. The caller creates its own WS_EX_NOREDIRECTIONBITMAP window.
    // No fallback to WARP occurs unless warpForTests was explicitly requested.
    // Failed initialization leaves an existing renderer untouched.
    // Call reset() before initializing again on the same HWND: DirectComposition
    // permits only one target at that window's chosen composition layer.
    void initialize(void *window, std::uint32_t width, std::uint32_t height, const RendererOptions &options);
    void resize(std::uint32_t width, std::uint32_t height);
    bool setMesh(std::string sourceID, std::uint64_t revision, MeshData mesh);
    bool setTexture(std::string sourceID, std::uint64_t revision, TextureData texture);
    // Uploads changed object constants only here. Pointer/camera updates never
    // recreate mesh/texture/object buffers or run layout/bitmap generation.
    // Failure while updating existing uniforms resets the renderer; callers
    // recreate from their retained model. Invalid data fails before any upload.
    void setDrawList(std::span<const DrawObject> objects);
    void setCamera(const core::Matrix4 &viewProjection);
    // false is a test-only render without presentation, required for readback.
    // offscreenForTests explicitly disables all composition/presentation APIs;
    // it uses the same shader pipeline in noninteractive Windows CI sessions.
    void draw(bool present = true);
    Readback readback();
    // An in-use resource cannot be removed until its draw list reference is
    // removed. Replacing an existing ID updates all current references.
    bool removeMesh(const std::string &sourceID);
    bool removeTexture(const std::string &sourceID);
    void clearDrawList();
    void clearResources(); // also clears draw list
    void reset() noexcept;
    RendererStats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace endfield::native
