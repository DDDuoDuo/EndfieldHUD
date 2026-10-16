// Renderer::releaseFrameTargets (the concealed HUD's GPU release, app/
// application.cpp releaseConcealedGraphics): the linear composition target,
// the offscreen output and every native group target are dropped and the
// driver trims; the next draw creates again only what it draws, groups are
// re-rendered once, and the pixels are identical to the frame before the
// release. Hidden WARP offscreen renderer on a never-shown window; synthetic
// quads only.
#include "native/renderer.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <string>

namespace gpu = endfield::native;
namespace core = endfield::core;
namespace {
std::size_t checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
struct Window {
    ATOM atom{}; HWND handle{};
    Window() {
        WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"EndfieldFrameTargetsTest";
        atom = RegisterClassW(&type); check(atom != 0, "Frame target test class registered");
        handle = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, type.lpszClassName, L"Frame target test", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, type.hInstance, nullptr);
        check(handle != nullptr, "Hidden frame target window created");
    }
    ~Window() { if (handle) DestroyWindow(handle); if (atom) UnregisterClassW(MAKEINTATOM(atom), GetModuleHandleW(nullptr)); }
};
core::Matrix4 camera() { core::Matrix4 m; m.values = {2. / 64, 0, 0, 0, 0, -2. / 64, 0, 0, 0, 0, 0, 0, -1, 1, 0, 1}; return m; }
constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
constexpr std::array<gpu::Vertex, 4> quad{{{{0, 0, 0}, {0, 0}, {1, 1, 1, 1}}, {{16, 0, 0}, {1, 0}, {1, 1, 1, 1}}, {{16, 16, 0}, {1, 1}, {1, 1, 1, 1}}, {{0, 16, 0}, {0, 1}, {1, 1, 1, 1}}}};

void run(gpu::Renderer& renderer) {
    renderer.setCamera(camera());
    renderer.setMesh("quad", 1, {quad, indices});
    // A translucent texel so the group's premultiplied linear pass matters.
    const std::array<std::uint8_t, 16> texels{255, 255, 255, 255, 40, 180, 90, 128, 0, 0, 0, 0, 200, 30, 60, 255};
    renderer.setTexture("texels", 1, {2, 2, texels});
    std::array<gpu::DrawObject, 2> children;
    children[0].sourceID = "child/0"; children[0].meshID = "quad"; children[0].textureID = "texels"; children[0].linearTint = {1, .5f, .25f, 1};
    children[1].sourceID = "child/1"; children[1].meshID = "quad"; children[1].textureID = "texels"; children[1].world = core::Matrix4::translation(10, 6); children[1].opacity = .6f;
    check(renderer.configureNativeGroup("shown", {{0, 0, 30, 24}, 1.5}, children), "The shown group installs");
    check(renderer.configureNativeGroup("hidden", {{0, 0, 20, 20}, 1}, children), "A group no frame draws installs");
    std::array<gpu::DrawObject, 3> list;
    list[0] = renderer.nativeGroupOutput("shown"); list[0].world = core::Matrix4::translation(20, 12); list[0].opacity = .8f;
    list[1] = renderer.nativeGroupOutput("hidden"); list[1].opacity = 0; // published, never drawn
    list[2].sourceID = "plain"; list[2].meshID = "quad"; list[2].textureID = "texels"; list[2].world = core::Matrix4::translation(2, 40);
    renderer.setDrawList(list);
    renderer.draw(false);
    const auto before = renderer.readback().pixels;
    const auto drawn = renderer.stats();
    check(drawn.nativeGroupRenders == 1 && drawn.releasedTargetBytes == 0, "Only the drawn group renders before any release");
    const std::size_t frameBytes = std::size_t(64) * 64 * 12;
    const std::size_t shownBytes = std::size_t(45) * 36 * 8, hiddenBytes = std::size_t(20) * 20 * 8;

    // After a frame (a finished module switch): only the group it did not draw gives its target back.
    check(renderer.releaseHiddenGroupTargets() == hiddenBytes && renderer.stats().releasedTargetBytes == hiddenBytes &&
          renderer.stats().hiddenGroupTargetReleases == 1, "After a frame, only the group it did not draw gives its target back");
    renderer.draw(false);
    check(renderer.readback().pixels == before && renderer.stats().nativeGroupRenders == drawn.nativeGroupRenders &&
          renderer.stats().nativeGroupTargetRestores == 0, "The drawn group keeps its cached target and the frame is unchanged");
    check(renderer.releaseHiddenGroupTargets() == 0, "A released group is not released twice");

    // Concealed: the linear and offscreen output targets (64 x 64 x 12) and the remaining group target.
    const auto released = renderer.releaseFrameTargets();
    const auto afterRelease = renderer.stats();
    check(released == frameBytes + shownBytes && afterRelease.releasedTargetBytes == released + hiddenBytes,
          "The release drops the frame targets and every native group target");
    check(afterRelease.frameTargetReleases == 1 && afterRelease.driverTrimmed, "The driver trims its internal buffers");
    check(afterRelease.resourceBytes == drawn.resourceBytes && afterRelease.textures == drawn.textures && afterRelease.meshes == drawn.meshes,
          "Retained meshes, textures and the group reservations stay");
    check(renderer.releaseFrameTargets() == 0 && renderer.stats().frameTargetReleases == 2, "A second release finds nothing left to drop");

    // The next frame creates again only what it draws and paints the same pixels.
    renderer.draw(false);
    const auto restored = renderer.stats();
    check(renderer.readback().pixels == before, "The frame after the release is identical to the frame before it");
    check(restored.frameTargetRestores == 1 && restored.nativeGroupTargetRestores == 1 && restored.nativeGroupRenders == drawn.nativeGroupRenders + 1,
          "The draw restores the frame targets and re-renders only the drawn group once");
    check(restored.releasedTargetBytes == hiddenBytes, "The group no frame draws stays released");
    renderer.draw(false);
    check(renderer.stats().nativeGroupRenders == restored.nativeGroupRenders && renderer.stats().frameTargetRestores == 1,
          "Later frames reuse the restored targets without re-rendering");

    // A released group drawn again later is created on that frame.
    list[1].opacity = 1; renderer.setDrawList(list); renderer.draw(false);
    check(renderer.stats().releasedTargetBytes == 0 && renderer.stats().nativeGroupTargetRestores == 2, "A group drawn again is restored on that frame");

    // Reconfiguring or removing a released group keeps the accounting exact.
    (void)renderer.releaseFrameTargets();
    check(renderer.configureNativeGroup("hidden", {{0, 0, 10, 10}, 1}, children) &&
          renderer.stats().releasedTargetBytes == frameBytes + shownBytes, "Reconfiguring a released group allocates its new target");
    renderer.setDrawList(std::span(list.data(), 1));
    check(renderer.removeNativeGroup("hidden") && renderer.stats().releasedTargetBytes == frameBytes + shownBytes, "Removing a group keeps the released bytes exact");
    renderer.resize(48, 48);
    check(renderer.stats().releasedTargetBytes == shownBytes, "A resize creates the frame targets again");
    renderer.clearDrawList(); check(renderer.removeNativeGroup("shown"), "The released group retires");
    check(renderer.stats().releasedTargetBytes == 0, "Nothing released remains counted");
    renderer.clearResources();
}
// The HUD closed for a while (Application::releaseIdleGraphics ->
// Renderer::suspendDevice): the device itself goes, every retained mesh,
// texture, group and draw keeps its identity in system memory, removals need
// no device, and the first call that needs the GPU creates it again - the
// frame after it is identical to the frame before. A media owner holding the
// device keeps it.
void suspension(gpu::Renderer& renderer) {
    renderer.resize(64, 64);
    renderer.setCamera(camera());
    renderer.setMesh("quad", 1, {quad, indices});
    const std::array<std::uint8_t, 16> texels{255, 255, 255, 255, 40, 180, 90, 128, 0, 0, 0, 0, 200, 30, 60, 255};
    renderer.setTexture("texels", 1, {2, 2, texels});
    renderer.setTexture("linear", 1, {2, 2, texels, gpu::TextureColorSpace::linear});
    gpu::TextureData paintable{2, 2, texels}; paintable.updatable = true;
    renderer.setTexture("paintable", 1, paintable);
    renderer.setTexture("unused", 1, {2, 2, texels});
    std::array<gpu::DrawObject, 2> children;
    children[0].sourceID = "child/0"; children[0].meshID = "quad"; children[0].textureID = "texels"; children[0].linearTint = {1, .5f, .25f, 1};
    children[1].sourceID = "child/1"; children[1].meshID = "quad"; children[1].textureID = "linear"; children[1].world = core::Matrix4::translation(10, 6); children[1].opacity = .6f;
    check(renderer.configureNativeGroup("shown", {{0, 0, 30, 24}, 1.5}, children), "The suspended scene's group installs");
    std::array<gpu::DrawObject, 3> list;
    list[0] = renderer.nativeGroupOutput("shown"); list[0].world = core::Matrix4::translation(20, 12); list[0].opacity = .8f;
    list[1].sourceID = "plain"; list[1].meshID = "quad"; list[1].textureID = "paintable"; list[1].world = core::Matrix4::translation(2, 40);
    list[2].sourceID = "white"; list[2].meshID = "quad"; list[2].world = core::Matrix4::translation(40, 40); list[2].linearTint = {0, 1, 0, .5f};
    renderer.setDrawList(list);
    renderer.draw(false);
    const auto before = renderer.readback().pixels;
    const auto drawn = renderer.stats();

    check(renderer.suspendDevice() && renderer.deviceSuspended() && !renderer.suspendDevice(), "The closed renderer releases its device, once");
    const auto suspended = renderer.stats();
    check(suspended.deviceSuspended && suspended.deviceSuspensions == drawn.deviceSuspensions + 1 && suspended.suspendedCopyBytes > 0,
          "The retained resources are kept in system memory");
    check(suspended.meshes == drawn.meshes && suspended.textures == drawn.textures && suspended.objects == drawn.objects &&
          suspended.nativeGroups == drawn.nativeGroups && suspended.resourceBytes == drawn.resourceBytes,
          "Every mesh, texture, group and draw keeps its identity and accounting");
    check(!renderer.deviceInfo().name.empty() && renderer.adapterCurrent(), "The adapter stays known without a device");
    check(renderer.releaseFrameTargets() == 0 && renderer.releaseHiddenGroupTargets() == 0, "A suspended renderer has no targets to release");
    check(renderer.removeTexture("unused") && renderer.deviceSuspended() && renderer.stats().textures == drawn.textures - 1,
          "A removal while suspended needs no device");
    renderer.setCamera(camera());
    check(renderer.deviceSuspended(), "Unchanged camera and stats need no device");

    // The first frame after the opening: the device again, identical pixels.
    renderer.draw(false);
    const auto resumed = renderer.stats();
    check(!renderer.deviceSuspended() && resumed.deviceResumes == drawn.deviceResumes + 1 && !resumed.deviceSuspended && resumed.suspendedCopyBytes == 0,
          "The first draw creates the device again");
    check(renderer.readback().pixels == before, "The frame after the device came back is identical to the frame before it");
    check(resumed.nativeGroupRenders == drawn.nativeGroupRenders + 1, "The drawn group re-renders once on the new device");

    // Painting in place, uploads and a second cycle keep working.
    const std::array<std::uint8_t, 4> red{255, 0, 0, 255};
    check(renderer.updateTexture("paintable", 2, 1, 1, {1, 1, red}), "An updatable texture is painted in place on the new device");
    renderer.draw(false); const auto painted = renderer.readback().pixels;
    check(painted != before, "The painted texel shows");
    check(renderer.suspendDevice(), "A second suspension");
    renderer.resize(64, 64);
    check(renderer.deviceSuspended(), "An unchanged resize needs no device");
    check(renderer.resumeDevice() && !renderer.resumeDevice(), "The device can be created again explicitly, once");
    renderer.draw(false);
    check(renderer.readback().pixels == painted, "An explicit resumption draws the same frame");
    // The opening HUD: the device is created on a worker while the frame's
    // CPU work runs, and taken by the first call that needs it.
    check(renderer.setGpuThreadPriority(3) >= 0, "The GPU thread priority applies");
    check(renderer.suspendDevice() && renderer.prepareDevice() && !renderer.prepareDevice(), "The opening starts creating the device, once");
    check(renderer.setGpuThreadPriority(5) >= 0 && renderer.deviceSuspended(), "A priority asked for meanwhile waits for the device");
    renderer.draw(false);
    check(!renderer.deviceSuspended() && renderer.stats().devicePreparations == 1, "The first draw takes the prepared device");
    check(renderer.readback().pixels == painted, "A texture painted in place keeps its texels across a suspension");
    // A resize while suspended takes effect when the device comes back.
    check(renderer.suspendDevice(), "A third suspension");
    renderer.resize(56, 40);
    check(renderer.deviceSuspended(), "A resize while suspended needs no device");
    renderer.draw(false);
    const auto resized = renderer.readback();
    check(!renderer.deviceSuspended() && resized.width == 56 && resized.height == 40, "The device comes back at the new size");
    renderer.resize(64, 64);
    check(renderer.setTexture("later", 1, {2, 2, texels}) && renderer.stats().textures == drawn.textures, "Uploads continue on the new device");
    renderer.clearDrawList(); check(renderer.removeNativeGroup("shown"), "The group retires");
    renderer.clearResources();
}
// A media owner holding the device (Renderer::mediaDevice) keeps it.
void suspensionLease(const wchar_t* shader) {
    Window window; gpu::Renderer renderer;
    gpu::RendererOptions options{gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests, true};
    renderer.initialize(window.handle, 16, 16, options);
    auto lease = renderer.mediaDevice();
    check(lease && !renderer.suspendDevice() && !renderer.deviceSuspended(), "A lent device is not released");
    check(renderer.mediaDevice() == lease, "Holders share one lease");
    lease.reset();
    check(renderer.suspendDevice() && renderer.deviceSuspended(), "Once every holder dropped the device, it is released");
    lease = renderer.mediaDevice();
    check(lease && !renderer.deviceSuspended(), "A media owner asking for the device creates it again");
    lease.reset(); renderer.reset();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Pass the native HUD shader path");
        check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Owned COM fixture initializes");
        {
            Window window; gpu::Renderer renderer;
            renderer.initialize(window.handle, 64, 64, {gpu::Driver::warpForTests, argv[1], gpu::RenderTarget::offscreenForTests});
            run(renderer);
            suspension(renderer);
            check(!IsWindowVisible(window.handle), "The fixture never shows a window");
            renderer.reset();
        }
        suspensionLease(argv[1]);
        CoUninitialize();
        std::cout << "application_frame_targets_contracts: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Frame target contract failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
