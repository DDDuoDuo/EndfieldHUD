#pragma once
#include "modules/media_assembly_processor.hpp"
#include <memory>
#include <span>

#ifdef _WIN32
struct ID3D11Device;struct IDXGISurface;
namespace endfield::native {
struct MediaAssemblyGpuStats {
    std::uint64_t renders{},inPlace{},readbacks{},sourceUploads{},sourceAllocations{},lookupUploads{},targetAllocations{};
};
// GPU evaluation of the MediaAssemblyEngine.apply colour chain and geometry
// (no stickers) on an existing D3D11 device. It runs exactly the portable
// MediaAssemblyColorPipeline constants (modules/media_assembly_processor):
// the integer crop/mirror/quarter-turn permutation, exposure, colour
// controls, the temperature/tint matrix, CIHighlightShadowAdjust, the tone
// spline, levels, gamma and the exact trilinear 32^3 lookup, in the encoded
// (still/preview) or linear (video compositor) working space. Results match
// the CPU reference within one RGBA8 level (WARP test).
//
// Threading/resources: every call belongs to the thread that owns the
// device's immediate context (the UI thread for Renderer::mediaDevice(), or
// the explicit export job's own device on the utility worker). It creates no
// thread, timer or device. Textures are allocated only when a size/format
// changes (source, scratch, export targets) and reused for later frames; the
// context state is cleared after each pass, matching the renderer's own
// passes. Shader bytecode is compiled once per process.
class NativeMediaAssemblyGpuProcessor final {
public:
    // device: AddRef-owned ID3D11Device*.
    explicit NativeMediaAssemblyGpuProcessor(std::shared_ptr<void>device);
    ~NativeMediaAssemblyGpuProcessor();
    NativeMediaAssemblyGpuProcessor(const NativeMediaAssemblyGpuProcessor&)=delete;
    NativeMediaAssemblyGpuProcessor&operator=(const NativeMediaAssemblyGpuProcessor&)=delete;
    // RGBA8 rows (top-left origin, encoded sRGB). rotationDegrees is the Media
    // Foundation display rotation (0/90/180/270), folded into the geometry so
    // movie frames need no CPU rotation copy. Same-size uploads reuse the
    // texture. The edit plan is computed on the oriented size.
    void setSource(unsigned width,unsigned height,std::span<const std::uint8_t>rgba,std::size_t stride,bool premultiplied,unsigned rotationDegrees=0);
    bool hasSource()const noexcept;
    // Edits for the following passes (stickers are ignored: they are retained
    // scene leaves in previews and composited by the CPU on image export).
    // Returns the processed output plan for the current source.
    modules::MediaAssemblyPixelPlan configure(const modules::MediaAssemblyAdjustments&,std::span<const std::uint8_t>lookup,bool linearWorkingSpace);
    // Renders the configured edit of the source into an RTV-capable 8-bit
    // UNORM surface of exactly the plan's output size, as straight encoded
    // pixels (the Renderer media-texture/frame-server contract).
    void render(IDXGISurface*target);
    // Colour-only edit of a straight BGRA8 frame-server surface in place
    // (played movie frames; geometry stays in texture coordinates). The
    // configured edits are used; no source is required.
    void processInPlace(IDXGISurface*surface);
    // Export: renders associated pixels, bilinearly resamples them to
    // width x height when that differs from the plan (the even composition
    // size, mediaAssemblyResample semantics) and reads BGRA8 rows back.
    void renderToMemory(unsigned width,unsigned height,std::span<std::uint8_t>bgra,std::size_t stride);
    // Drops borrowed external-surface views (call when the frame-server or
    // media texture they belong to is retired).
    void releaseTargets()noexcept;
    MediaAssemblyGpuStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
// Compiles the processor's shaders once per process (thread-safe; throws on
// failure). Hosts run it on the utility worker so the UI thread never pays
// the HLSL compile; processors created afterwards only create device objects.
void prepareMediaAssemblyGpuShaders();
bool mediaAssemblyGpuShadersReady()noexcept;
// A hardware D3D11 device for one explicit export job on the utility worker
// (the source AVAssetExportSession likewise renders its composition on the
// GPU). Released when the job ends; null when no hardware device exists, in
// which case the caller uses the CPU processor.
std::shared_ptr<void>createMediaAssemblyExportDevice()noexcept;
}
#endif
