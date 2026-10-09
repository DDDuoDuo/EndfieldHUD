#pragma once

#include "core/scene.hpp"
#include "core/motion.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <variant>
#include <type_traits>

namespace endfield::native {
class SourceGraphics;
class DesktopBackdrop;

enum class Driver { hardware, warpForTests };
enum class TextureColorSpace { sRGB, linear };
enum class TextureFilter { nearest, linear };
enum class RenderTarget { composition, offscreenForTests };
struct RendererOptions {
    Driver driver{Driver::hardware};
    std::filesystem::path shaderPath;
    RenderTarget target{RenderTarget::composition};
    // Same-device media targets/thread protection. Hardware requests video
    // support; explicit WARP tests cover conversion/lifetime only, not decode.
    bool mediaVideo{};
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
struct NativeGroupTarget {
    core::Rect localBounds;
    double pixelsPerPoint{1};
    bool operator==(const NativeGroupTarget&)const=default;
};
struct PlaneMask {
    core::Matrix4 worldToLocal;
    core::Rect bounds;
    // Circular source corner radius in the mask's own logical coordinates.
    // Zero preserves the exact rectangle path; no per-frame mask texture.
    double cornerRadius{};
};
// The two original mask topologies share a fixed-size retained value. A
// variant stores only the active path; there is no heap or duplicate geometry.
using SubsectionShutterPath = std::array<std::array<core::MotionPoint,6>,4>;
struct PlaneShutter {
    core::Matrix4 worldToLocal;
    std::variant<core::ShutterPath,SubsectionShutterPath> path{core::ShutterPath{}};
    PlaneShutter()=default;
    PlaneShutter(core::Matrix4 world,core::ShutterPath value):worldToLocal(world),path(value){}
    PlaneShutter(core::Matrix4 world,SubsectionShutterPath value):worldToLocal(world),path(value){}
    core::ShutterPath& moduleStrips(){return std::get<core::ShutterPath>(path);}
    const core::ShutterPath& moduleStrips()const{return std::get<core::ShutterPath>(path);}
    // Exact six pentagons for HUDModuleContent, or four hexagons for
    // HUDSubsectionTransition. Their union intersects ancestor rectangle masks.
    // Both windings and repeated/collinear vertices are supported; zero-area
    // strips contribute no coverage. Edge antialiasing parity is separate.
    bool operator==(const PlaneShutter& other)const noexcept{
        if(worldToLocal!=other.worldToLocal||path.index()!=other.path.index())return false;
        return std::visit([&](const auto& value){const auto& rhs=std::get<std::decay_t<decltype(value)>>(other.path);
            for(std::size_t n=0;n<value.size();++n)for(std::size_t k=0;k<value[n].size();++k)
                if(value[n][k].x!=rhs[n][k].x||value[n][k].y!=rhs[n][k].y)return false;
            return true;},path);
    }
};
// Shared CPU validation for retained scene setters and final GPU publication.
// No device, resource or allocation is needed for a valid source mask.
void validatePlaneShutter(const PlaneShutter&);
struct PlaneAlphaMask {
    core::Matrix4 worldToLocal;
    core::Rect bounds; // texture edges in mask-local points
    std::string textureID; // caller-owned resident texture; sampled alpha only
    bool operator==(const PlaneAlphaMask&)const=default;
};
// Numeric angular clip for a resident path/raster. Angles use the mask-local
// +X axis with positive rotation toward +Y (clockwise in flipped HUD space).
// Sweep is [0, 2*pi]; endpoints are straight radial butt cuts, antialiased in
// screen derivatives. This is a clip, not a substitute stroke generator.
struct AngularMask {
    core::Matrix4 worldToLocal;
    core::Point center;
    double startAngle{},sweepAngle{};
    // Optional exact source curve tangent: n.x*(x-center.x) +
    // n.y*(y-center.y) + offset >= 0 keeps the endpoint's inside half-plane.
    // Normal must be finite/nonzero. Omitted uses the radial end butt cut.
    std::optional<std::array<double,3>> endPlane;
    bool operator==(const AngularMask&)const=default;
};
struct DrawObject {
    std::string sourceID, meshID, textureID; // empty textureID selects opaque white
    core::Matrix4 world;
    std::array<float, 4> linearTint{1, 1, 1, 1};
    float opacity{1};
    // Up to eight intersecting plane-local rectangle/rounded-rectangle masks. Original source
    // stencil/soft-mask programs remain outside this plain-surface API.
    std::vector<PlaneMask> masks;
    std::optional<PlaneShutter> shutter;
    std::optional<PlaneAlphaMask> alphaMask; // intersects ordinary masks/shutter
    std::optional<AngularMask> angularMask; // no texture or tessellation
};
// Checks retained numeric/identity data with the exact GPU-uniform rules,
// without querying or allocating device resources. Resource existence remains
// the owning Renderer publication check.
void validateDrawObject(const DrawObject&);
struct RendererStats {
    std::size_t meshes{}, textures{}, objects{}, resourceBytes{};
    std::uint64_t meshUploads{}, textureUploads{}, objectUploads{}, objectBufferAllocations{}, cameraUploads{}, drawCalls{}, presents{};
    bool initialized{};
    std::size_t nativeGroups{},nativeGroupBytes{};
    std::uint64_t nativeGroupTargetAllocations{},nativeGroupRenders{},nativeGroupDrawCalls{};
    std::size_t mediaTargets{},mediaLiveBytes{};
    std::uint64_t mediaTargetAllocations{},mediaFrameCommits{};
    std::uint64_t compositionSurfaceBorrows{},compositionSuspends{},compositionRestores{};
    bool compositionSuspended{};
    bool sourcePassEnabled{true};
};
// AddRef-owned existing composition swap chain, never a copied bitmap/device.
// The raw pointer is borrowed IDXGISwapChain1 and valid only while valid().
// Renderer reset invalidates the epoch and detaches a registered backdrop
// bridge before releasing its device/target. All use stays on the owner thread.
class RendererCompositionSurface final {
public:
    ~RendererCompositionSurface();
    RendererCompositionSurface(const RendererCompositionSurface&)=delete;
    RendererCompositionSurface&operator=(const RendererCompositionSurface&)=delete;
    bool valid()const noexcept;
    void*swapChain()const noexcept;
    void*window()const noexcept;
    unsigned width()const noexcept;unsigned height()const noexcept;
private:
    friend class Renderer;friend class DesktopBackdrop;
    struct Impl;explicit RendererCompositionSurface(std::unique_ptr<Impl>);
    void bindResetObserver(void*,void(*)(void*)noexcept);
    void unbindResetObserver(void*)noexcept;
    std::unique_ptr<Impl>impl_;
};
// A same-device frame-server surface. The handle retains its COM resources
// through reset/removal, but valid() then becomes false and further commits
// reject. Owner must stop its MF engine before releasing the last handle.
// targetSurface is a borrowed IDXGISurface*, used only on the owning UI thread
// by IMFMediaEngine::TransferVideoFrame. No worker callback may render/commit.
class RendererMediaTexture final {
public:
    ~RendererMediaTexture();
    RendererMediaTexture(const RendererMediaTexture&)=delete;
    RendererMediaTexture&operator=(const RendererMediaTexture&)=delete;
    bool valid()const noexcept;
    unsigned width()const noexcept;unsigned height()const noexcept;
    const std::string& sourceID()const noexcept;
    void* targetSurface()const noexcept;
private:
    friend class Renderer;struct Impl;explicit RendererMediaTexture(std::unique_ptr<Impl>);
    std::unique_ptr<Impl>impl_;
};
struct RendererDeviceInfo {
    std::string name;
    std::uint32_t vendorID{}, deviceID{};
    std::uint64_t dedicatedVideoBytes{}, sharedSystemBytes{}; // adapter capacities, not this app's usage
};
struct Readback {
    std::uint32_t width{}, height{}, rowBytes{};
    // BGRA8 encoded-sRGB bytes from this application's own target. Native
    // surfaces are premultiplied; original-source passes retain their authored
    // blend output, which may include emissive RGB beyond alpha. Do not clamp
    // or unpremultiply that source output when comparing raw references.
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
    static constexpr std::size_t maximumNativeGroups=64,maximumNativeGroupPixels=4*1024*1024,
        maximumNativeGroupBytes=64*1024*1024;
    static constexpr std::size_t maximumMediaTargets=16,maximumMediaPixels=4*1024*1024,
        maximumMediaBytes=128*1024*1024;
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
    // Same live handle is reused. Offscreen renderers cannot be bridged.
    std::shared_ptr<RendererCompositionSurface>borrowCompositionSurface();
    // Suspend only while the HWND is hidden. Retains the upper target slot and
    // exact original visual, so restore never allocates a replacement target.
    // Invalid/foreign handles reject before mutation. Stale restoration is a
    // harmless false after device loss/reset; no old device is resurrected.
    bool suspendComposition(const RendererCompositionSurface&);
    bool restoreComposition(const RendererCompositionSurface&);
    // Event-only presentation gate for the retained original material pass.
    // Native draws keep rendering. No source resources/draws are removed or
    // reuploaded; equal values are no-ops. A fresh/reset renderer defaults true.
    bool setSourcePassEnabled(bool);
    bool sourcePassEnabled()const noexcept;
    bool setMesh(std::string sourceID, std::uint64_t revision, MeshData mesh);
    bool setTexture(std::string sourceID, std::uint64_t revision, TextureData texture);
    // AddRef-owned ID3D11Device, for the owner's MF DXGI manager. Requires the
    // explicit mediaVideo option; multithread protection is enabled at creation.
    // This is the existing device, never a new graphics context or clock.
    std::shared_ptr<void> mediaDevice()const;
    std::shared_ptr<RendererMediaTexture>createMediaTexture(std::string sourceID,unsigned width,unsigned height);
    // GPU-only conversion from transferred encoded straight BGRA8 to retained
    // linear-premultiplied RGBA16. No CPU copy/readback, resource allocation,
    // mesh upload or draw-list replacement. Invoke only for a new engine frame.
    void commitMediaTexture(const RendererMediaTexture&);
    // Keep the already committed linear pixels under the same texture ID,
    // without copying/recreating them. Retires the frame-server target slot and
    // invalidates future commits. The caller stops its engine first, then drops
    // its borrowed target handle. Pixels and any still-borrowed encoded surface
    // remain charged to the existing media byte ceiling until individually
    // released. Safe while the texture is published; no draw-list replacement.
    void retainMediaPoster(const RendererMediaTexture&);
    // A local premultiplied-linear GPU pass on this same device. Its rounded
    // pixel coverage includes the caller's overflowing shadow/ink bounds. No
    // CPU bitmap/readback is generated; root pose/fade belongs to outputDraw.
    // Configuration and child content are caller-owned content events. All
    // registered IDs and object slots remain retained until explicitly removed.
    bool configureNativeGroup(std::string id,const NativeGroupTarget&);
    // Atomic bounds+children content transaction. Invalid children/bounds leave
    // the previous group, output identity and published target unchanged.
    bool configureNativeGroup(std::string id,const NativeGroupTarget&,std::span<const DrawObject>);
    // Whole-list validation precedes mutation. Changed local constants or
    // referenced mesh/texture replacements dirty the cached target. A group's
    // own output resources and every other group's output are forbidden inputs
    // (no nesting/cycles/source-material passes in this plain native API).
    bool setNativeGroupDraws(const std::string& id,std::span<const DrawObject>);
    // Borrowed stable IDs/local quad. Caller copies once into retained ordered
    // publication, then changes only numeric world/opacity/masks/shutter. The
    // reference remains valid until removeNativeGroup/clearResources/reset.
    const DrawObject& nativeGroupOutput(const std::string& id)const;
    // In-use output cannot be removed. Group-held local resource references
    // likewise prevent direct mesh/texture removal until replaced/cleared.
    bool removeNativeGroup(const std::string& id);
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
    // Explicit diagnostic query only; never called by the frame loop. The
    // selected adapter may differ from another GPU installed in the laptop.
    RendererDeviceInfo deviceInfo() const;
    // Original material path shares this exact device and presentation target.
    // No new window, renderer timer or duplicate GPU device is created.
    SourceGraphics& sourceGraphics();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace endfield::native
