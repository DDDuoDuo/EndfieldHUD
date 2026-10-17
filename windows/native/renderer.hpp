#pragma once

#include "core/scene.hpp"
#include "core/motion.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <type_traits>

namespace endfield::native {
class SourceGraphics;
struct SourceGraphicsStats;
class DesktopBackdrop;

enum class Driver { hardware, warpForTests };
// encodedSRGB retains associated encoded bytes for source Core Animation
// groups. The default sRGB path continues to filter/blend in linear space.
// Every texture takes four bytes per texel on the GPU: sRGB and linear ones
// hold premultiplied linear values in sRGB encoding (R8G8B8A8_UNORM_SRGB),
// encoded ones premultiplied encoded bytes (R8G8B8A8_UNORM). One that is
// white wherever it covers (an alpha mask, white text) keeps one byte, its
// alpha (A8_UNORM), and reads as premultiplied white. Encoded native groups
// likewise keep 8-bit targets; linear groups keep half floats.
enum class TextureColorSpace { sRGB, linear, encodedSRGB };
enum class NativeGroupColorSpace { linear, encodedSRGB };
enum class NativeBlend { sourceOver, screen, weightedAdd };
enum class TextureFilter { nearest, linear };
enum class RenderTarget { composition, offscreenForTests };
struct RendererOptions {
    Driver driver{Driver::hardware};
    std::filesystem::path shaderPath;
    RenderTarget target{RenderTarget::composition};
    // Same-device media targets/thread protection. Hardware requests video
    // support; explicit WARP tests cover conversion/lifetime only, not decode.
    bool mediaVideo{};
    // Small surfaces only (e.g. the floating charge alert): create the device
    // with D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, which
    // halves a hardware device's driver worker threads.
    bool preventDriverThreads{};
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
    // Painted in place (Renderer::updateTexture): one GPU texture keeps its
    // identity while rectangles of its texels are replaced. Otherwise the
    // texture is immutable and every revision creates it again.
    bool updatable{};
};
struct NativeGroupTarget {
    core::Rect localBounds;
    double pixelsPerPoint{1};
    NativeGroupColorSpace colorSpace{NativeGroupColorSpace::linear};
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
    // Screen and weightedAdd are supported only inside an encoded native group.
    // weightedAdd is a bounded crossfade: every child must use it, at most two
    // regular leaf draws, and their opacity weights sum to <=1. Their associated
    // encoded RGBA contributions add before one normal output composition.
    // Root/default linear groups and nested weighted inputs reject it.
    NativeBlend blend{NativeBlend::sourceOver};
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
    // DXGI 1.3 frame latency object in use, and the frame-start waits on it.
    bool frameLatencyWaitable{};
    std::uint64_t presentationWaits{},presentationWaitTimeouts{};
    bool sourcePassEnabled{true};
    // initialize() created its programs from the build-time bytecode of an
    // identical hud.hlsl (native/renderer.shaders.cmake), not D3DCompile.
    bool embeddedShaders{};
    // releaseFrameTargets(): its calls, the target bytes released now (the
    // linear composition target, an offscreen output and native group
    // targets not yet drawn again), the targets later draws created again,
    // and whether the driver accepted the trim (IDXGIDevice3::Trim).
    std::uint64_t frameTargetReleases{},frameTargetRestores{},nativeGroupTargetRestores{},hiddenGroupTargetReleases{};
    std::size_t releasedTargetBytes{};
    bool driverTrimmed{};std::uint64_t driverTrims{};
    std::uint64_t sourceTargetBytesReleased{}; // the original shell pass's targets, summed over releases
    // Presentation timing, kept as numbers only (no work on their account):
    // seconds spent inside IDXGISwapChain::Present, the monotonic time
    // (OverlayHost::clockNow epoch, QueryPerformanceCounter) at which the
    // last Present returned, and that Present's swap-chain present count.
    double presentSeconds{},lastPresentReturned{};
    std::uint64_t lastPresentCount{};
    // CPU seconds inside draw() (Present excluded), setDrawList and setTexture
    // (restoreTexture included).
    double drawSeconds{},drawListSeconds{},textureUploadSeconds{};
    // parkTexture/restoreTexture: copies held now (textures and their bytes),
    // textures parked, restored, copies dropped (ten-minute release, clear,
    // reset, new device), and the CPU seconds parking and restoring took.
    std::size_t parkedTextures{},parkedBytes{};
    std::uint64_t textureParks{},parkedRestores{},parkedDrops{};
    double parkSeconds{},parkedRestoreSeconds{};
    // Of drawSeconds: the native passes (groups and the root list); the root
    // passes drawn, and those skipped because nothing they draw changed (the
    // retained linear target was composited as it was).
    double nativePassSeconds{};
    std::uint64_t nativePasses{},nativePassesSkipped{};
    // updateTexture(): rectangles replaced in place and their texel bytes.
    std::uint64_t textureRegionUploads{},textureRegionBytes{};
    // Admissions a message loop took from the frame latency object
    // (presentationAdmitted) instead of a blocking frame-start wait, and
    // presents made without any admission (each one may queue a frame ahead
    // of the display: Projection RC 128 queued ~35 of them, 200 ms at 165 Hz).
    std::uint64_t presentationAdmissions{},unadmittedPresents{};
    // releasePresentation()/restorePresentation(): the swap chain and the
    // composition target released while concealed, and created again.
    std::uint64_t presentationReleases{},presentationRestores{};
    bool presentationReleased{};
    // suspendDevice()/resumeDevice(): suspensions and resumptions, whether
    // the device is released now, the system-memory copies it keeps meanwhile
    // and the seconds spent creating devices again.
    std::uint64_t deviceSuspensions{},deviceResumes{},devicePreparations{};
    bool deviceSuspended{};
    std::size_t suspendedCopyBytes{};
    // A media owner holds the device now (mediaDevice()), and the leases
    // handed out since initialize (a lease lives until its last holder drops it).
    bool mediaDeviceLent{};
    std::uint64_t mediaDeviceLeases{};
    // The D3D11 devices created (initialize, resumptions) and those actually
    // destroyed: one is alive while the renderer holds it, and a suspension
    // that leaves an outside holder keeps it alive (its driver thread with it).
    std::uint64_t devicesCreated{},devicesDestroyed{};
    // prepareDevice() creations given up because no opening took them.
    std::uint64_t preparedDeviceDiscards{};
    // Of deviceResumeSeconds: creating the device, its programs and fixed
    // states, the retained resources, and the original pass's resources.
    double deviceResumeSeconds{},deviceCreateSeconds{},pipelineCreateSeconds{},resourceRestoreSeconds{},sourceRestoreSeconds{};
};
// IDXGISwapChain::GetFrameStatistics for the most recent present the
// compositor displayed: its present count, the vertical blank it was
// displayed on and one vertical blank's monotonic time (clockNow epoch).
struct RendererFrameStatistics {
    std::uint64_t presentCount{},presentRefreshCount{},syncRefreshCount{};
    double syncTime{};
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
// A texture taken off the GPU while its owner may need it again soon
// (Renderer::parkTexture: a module that left the composition while the HUD
// stays open): its texels exactly as the GPU stored them, in a system-memory
// staging copy the GPU writes without the CPU waiting, and copies back into a
// new texture when restored. The owner holds it; the renderer drops its
// contents at the ten-minute release (suspendDevice), on a clear, a reset and
// a new device, after which restoreTexture declines it.
class RendererParkedTexture final {
public:
    ~RendererParkedTexture();
    RendererParkedTexture(const RendererParkedTexture&)=delete;
    RendererParkedTexture&operator=(const RendererParkedTexture&)=delete;
    bool held()const noexcept;           // restorable (not yet dropped or restored)
    std::uint64_t revision()const noexcept;
    std::size_t bytes()const noexcept;   // system memory it holds now
private:
    friend class Renderer;struct Impl;explicit RendererParkedTexture(std::shared_ptr<Impl>);
    std::shared_ptr<Impl>impl_;
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
// One retained resource as the memory trace sees it (Renderer::forEachResource).
struct RendererResourceUsage {
    enum class Kind : std::uint8_t { texture, mesh, nativeGroup, media };
    std::string_view id, owner; // owner: the module owner whose call installed it ("" for the host)
    std::size_t bytes{};
    Kind kind{};
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
    static constexpr std::size_t maximumNativeGroups=256,maximumNativeGroupPixels=4*1024*1024,
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
    // Whether this build embeds hud.hlsl bytecode (Windows SDK fxc at build time).
    static bool shadersEmbedded() noexcept;
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
    // Replaces the texels of one rectangle of an updatable texture (created
    // by setTexture with TextureData::updatable) in place: same identity,
    // same GPU resource, no draw-list or group rebinding beyond marking the
    // groups that sample it dirty. `region` is straight RGBA8 of the
    // rectangle at (x, y) in texels, in the texture's own color space.
    // Equal revisions are no-ops; invalid rectangles reject before mutation.
    bool updateTexture(const std::string& sourceID, std::uint64_t revision,
        std::uint32_t x, std::uint32_t y, TextureData region);
    // AddRef-owned ID3D11Device, for the owner's MF DXGI manager. Requires the
    // explicit mediaVideo option; multithread protection is enabled at creation.
    // This is the existing device, never a new graphics context or clock.
    // Holders share one lease, and while any holder keeps it suspendDevice()
    // refuses: a media owner takes it only while media needs the GPU (an
    // open engine, an edit pass) and drops it when idle, so a closed HUD can
    // release its device. Creates the device again when it was suspended.
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
    // Encoded groups may contain encoded leaf-group outputs (maximum depth2).
    // Matching owned mesh/texture pairs are required; cycles/alpha inputs reject.
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
    // Frame start of a presented frame (before input/time are sampled): waits
    // up to the timeout for the swap chain to accept a frame, so the following
    // Present does not block. True at once offscreen, without the waitable
    // object, or when this frame already holds the admission; false on
    // timeout (the frame may still render; Present then bounds latency).
    bool waitForPresentation(std::uint32_t timeoutMilliseconds);
    // The same admission for a message loop that waits on it together with
    // its input (OverlayCallbacks::presentationWaitable): the borrowed frame
    // latency object HANDLE, or null offscreen or without one; whether the
    // next presented frame already holds an admission; and the report that a
    // wait on that handle succeeded (it consumed one admission, which the
    // next frame's Present uses without waiting again).
    void* presentationWaitable() const noexcept;
    bool presentationHeld() const noexcept;
    void presentationAdmitted() noexcept;
    // IDXGIDevice::SetGPUThreadPriority (-7...7) on this device; the HRESULT.
    long setGpuThreadPriority(int priority) noexcept;
    // Explicit diagnostic only (contention trace), never per frame: waits
    // until the GPU finished every command submitted so far; milliseconds.
    double waitForGpuIdle();
    // Explicit diagnostic only (the latency probe): the compositor's
    // statistics of the last displayed present; none offscreen, before the
    // first displayed present or while the statistics are disjoint.
    std::optional<RendererFrameStatistics> frameStatistics() const;
    // The HUD was concealed: nothing is drawn until it opens again. Releases
    // the GPU memory only a drawn frame uses - the full-size linear
    // composition target, an offscreen output, the original shell pass's
    // attachment and depth/stencil target, and every native group's cached
    // target - then clears the context and lets the driver trim its internal
    // buffers (IDXGIDevice3::Trim). Retained meshes, textures,
    // group draw lists and the composition swap chain stay; the next draw()
    // creates again what it uses (a group the next time it is drawn
    // visibly, re-rendered). No-op while already released. Returns the
    // bytes released. On the owner thread, never per frame.
    std::size_t releaseFrameTargets();
    // The concealed HUD presents nothing until it opens again: releases the
    // composition swap chain (its two full-size buffers and its frame latency
    // object) and the window's DirectComposition device, target and visual,
    // so neither the driver nor the compositor keeps attending to a window
    // that shows nothing; the frame targets go with them. The device, the
    // retained meshes, textures and groups stay. The next presented draw,
    // restorePresentation() or borrowCompositionSurface() creates them again
    // at the current size. False (nothing done) offscreen, when already
    // released, while suspended, or while a borrowed composition surface is
    // bridged (the Projection foreground). On the owner thread, never per frame.
    bool releasePresentation();
    // Creates a released presentation again (true when it was released): the
    // HUD's opening calls it before its first frame, so the message loop
    // waits on the new frame latency object, whose first admission belongs
    // to that frame.
    bool restorePresentation();
    bool presentationReleased() const noexcept;
    // The HUD has stayed closed for a while: releases the D3D11 device itself,
    // since an idle device keeps a driver thread waking about 70 times a
    // second (NVIDIA, measured hidden on the laptop: 67-83/s with or without
    // PREVENT_INTERNAL_THREADING_OPTIMIZATIONS or VIDEO_SUPPORT and after
    // Trim, 0.1-0.7/s once the device is released). Releases the frame
    // targets and the presentation first; every retained mesh, texture,
    // group, draw list, the camera and the original material pass keep their
    // identities, revisions and values, with their GPU contents copied into
    // system memory. The first later call that needs the GPU (a draw, an
    // upload, sourceGraphics(), restorePresentation(), mediaDevice(), ...)
    // creates a device again on the current default adapter and every
    // resource from its copy; removals and stats need none. False (nothing
    // done) when already suspended, while a media owner holds the device
    // (mediaDevice()), a media target exists, or a borrowed composition
    // surface is bridged or suspended. On the owner thread, never per frame.
    bool suspendDevice();
    // Creates the device again now (true when it was suspended).
    bool resumeDevice();
    // The opening HUD after a long close: starts creating the new device on
    // a worker thread (D3D11CreateDevice alone takes 110-230 ms on the
    // laptop's NVIDIA driver) while the first frame's CPU work runs; the
    // first call that needs the GPU takes it and creates the resources
    // again on this thread. True when it started (only while suspended).
    bool prepareDevice();
    bool deviceSuspended() const noexcept;
    // A device started by prepareDevice() that no call has taken yet.
    bool devicePrepared() const noexcept;
    // An opening that was anticipated (the tray icon was pointed at) but did
    // not come: the prepared device is released again, so it neither stays
    // alive (its driver thread waking the closed HUD) nor ages. Waits for a
    // creation still running. True when there was one.
    bool discardPreparedDevice();
    // suspendDevice() would release the device now: it exists, no media
    // owner holds it (mediaDevice()), no media target exists and no
    // composition surface is bridged or suspended.
    bool deviceReleasable() const noexcept;
    // After a drawn frame, on a content event (e.g. a finished module switch,
    // never per frame): every native group target that frame did not draw
    // visibly gives its GPU memory back; the group keeps its draw list and is
    // created again and re-rendered the next time it is drawn visibly.
    // Returns the bytes released.
    std::size_t releaseHiddenGroupTargets();
    // Lets the driver give back its internal buffers (IDXGIDevice3::Trim,
    // after clearing the context): upload staging and command pools grown by
    // a burst of uploads. A content event only (the end of a module switch, a
    // concealed HUD); the next frame rebinds everything it draws with.
    void trim();
    Readback readback();
    // An in-use resource cannot be removed until its draw list reference is
    // removed. Replacing an existing ID updates all current references.
    bool removeMesh(const std::string &sourceID);
    bool removeTexture(const std::string &sourceID);
    // The revision of the texture this renderer holds under `sourceID` (on
    // its device, or in system memory while the device is suspended), or
    // none. A lookup only: it never creates a suspended device again. An
    // owner that gave back its own copy of the pixels (LayerScene) asks it
    // before uploading, and paints them again only when the texture is gone.
    std::optional<std::uint64_t> textureRevision(const std::string &sourceID) const;
    // Removes an unreferenced texture like removeTexture, keeping its texels
    // in a caller-owned system-memory copy that restoreTexture installs again
    // as a GPU copy: same bytes, format, revision and identity, no CPU
    // upload, conversion or raster. Null (nothing removed) when the texture
    // is missing, still referenced or owned by a group or media, or the
    // device is suspended. Never waits for the GPU.
    std::shared_ptr<RendererParkedTexture> parkTexture(const std::string &sourceID);
    // Installs a parked texture again under its ID (true). False, changing
    // nothing, when its copy was dropped or another texture holds the ID.
    bool restoreTexture(const std::string &sourceID, RendererParkedTexture &parked);
    void clearDrawList();
    void clearResources(); // also clears draw list
    void reset() noexcept;
    RendererStats stats() const noexcept;
    // Explicit diagnostic query only; never called by the frame loop. The
    // selected adapter may differ from another GPU installed in the laptop.
    RendererDeviceInfo deviceInfo() const;
    // Diagnostics only (memory trace; never on the frame path): every retained
    // texture, mesh, native group target and media target with its counted
    // GPU bytes (the same accounting as RendererStats::resourceBytes).
    void forEachResource(const std::function<void(const RendererResourceUsage&)>&) const;
    // Diagnostics only (the idle trace; never on the frame path): every draw
    // of the published root list with the number of times its object
    // constants were uploaded since it was published.
    void forEachDrawUpload(const std::function<void(std::string_view sourceID, std::uint64_t uploads)>&) const;
    // Display/adapter change (WM_DISPLAYCHANGE): false when the adapter set
    // changed (IDXGIFactory1::IsCurrent) and the system's default adapter is
    // no longer the one this hardware device runs on. The owner then recreates
    // its renderer (the device-loss path). Always true for WARP.
    bool adapterCurrent() const noexcept;
    // Original material path shares this exact device and presentation target.
    // No new window, renderer timer or duplicate GPU device is created.
    SourceGraphics& sourceGraphics();
    // Its counters without creating a suspended device again (diagnostics).
    SourceGraphicsStats sourceGraphicsStats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace endfield::native
