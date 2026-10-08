#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace endfield::native {
// Original Mac material programs. These types deliberately keep texture bytes,
// shader constants and vertex attributes separate from the plain UI pipeline.
struct SourceAttribute { unsigned location{}, components{}, byteOffset{}; };
struct SourceStencilFace {
    unsigned compare{7}, fail{}, depthFail{}, pass{};
    unsigned readMask{255}, writeMask{255};
};
struct SourcePipeline {
    std::span<const std::uint8_t> vertexBytecode, fragmentBytecode;
    std::vector<SourceAttribute> attributes;
    bool blendEnabled{true};
    unsigned sourceRGB{1}, destinationRGB{5}, sourceAlpha{1}, destinationAlpha{5};
    unsigned rgbOperation{}, alphaOperation{}, writeMask{15};
    bool depthWrite{}, stencilEnabled{};
    unsigned depthCompare{7}, cull{};
    SourceStencilFace front, back;
    // Numeric pipeline enums above are Metal values, explicitly mapped by the
    // native implementation. They are never passed through to D3D enums.
};
struct SourceMip { unsigned width{}, height{}, rowBytes{}; std::span<const std::uint8_t> bytes; };
struct SourceTexture {
    std::string pixelFormat;
    std::vector<SourceMip> mips;
    unsigned minFilter{1}, magFilter{1}, mipFilter{1}, addressU{}, addressV{}, addressW{}, maxAnisotropy{1};
};
enum class SourceStage { vertex, fragment };
struct SourceUniformBinding { SourceStage stage; unsigned slot{}; std::string id; };
struct SourceTextureBinding { SourceStage stage; unsigned slot{}, samplerSlot{}; std::string id,propertyName; };
struct SourceDraw {
    std::string mesh, pipeline;
    unsigned firstIndex{}, indexCount{}, stencilReference{};
    std::vector<SourceUniformBinding> uniforms;
    std::vector<SourceTextureBinding> textures;
};
struct SourceGraphicsStats {
    std::size_t meshes{}, textures{}, pipelines{}, uniforms{}, draws{}, payloadBytes{};
    std::uint64_t geometryUploads{}, textureUploads{}, uniformUploads{}, uniformAllocations{}, frames{};
    std::uint64_t meshBufferAllocations{},vertexUploads{},indexUploads{};
};
class SourceGraphics final {
public:
    // Internal native renderer device/context; no HWND or independent device.
    SourceGraphics(void* device, void* context);
    ~SourceGraphics();
    SourceGraphics(const SourceGraphics&) = delete;
    SourceGraphics& operator=(const SourceGraphics&) = delete;
    void setMesh(const std::string& id, std::uint64_t revision, unsigned stride,
                 std::span<const std::uint8_t> vertices, std::span<const std::uint32_t> indices);
    void setTexture(std::string id, std::uint64_t revision, const SourceTexture& texture);
    void setPipeline(std::string id, const SourcePipeline& pipeline);
    void setUniform(const std::string& id, std::span<const std::uint8_t> bytes);
    void setDraws(std::span<const SourceDraw> draws);
    // Source directLDR path: original sRGB attachment, depth32/stencil8 and
    // exact ordered original passes. HDR/postprocess is rejected by the loader
    // until implemented; this class never substitutes another tone mapping.
    void renderTo(void* outputTexture, unsigned width, unsigned height);
    bool active() const noexcept;
    void clear();
    SourceGraphicsStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
