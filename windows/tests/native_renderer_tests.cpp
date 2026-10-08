#include "native/renderer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace endfield::native;
namespace {
unsigned checks{};
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const char *message) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    check(rejected, message);
}
class OwnedWindow {
public:
    OwnedWindow() {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EndfieldFreshRendererSyntheticWindow";
        atom_ = RegisterClassW(&type);
        if (!atom_) throw std::runtime_error("Cannot register isolated render window");
        window_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, type.lpszClassName,
            L"Synthetic GPU fixture", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, type.hInstance, nullptr);
        if (!window_) throw std::runtime_error("Cannot create isolated render window");
        // It remains hidden. No foreground activation, global input, or user
        // screen capture occurs in this test.
    }
    ~OwnedWindow() {
        if (window_) DestroyWindow(window_);
        if (atom_) UnregisterClassW(MAKEINTATOM(atom_), GetModuleHandleW(nullptr));
    }
    HWND get() const { return window_; }
private:
    HWND window_{};
    ATOM atom_{};
};
constexpr std::array<Vertex, 4> quad{{
    {{-1, 1, .5f}, {0, 0}, {1, 1, 1, 1}},
    {{1, 1, .5f}, {1, 0}, {1, 1, 1, 1}},
    {{1, -1, .5f}, {1, 1}, {1, 1, 1, 1}},
    {{-1, -1, .5f}, {0, 1}, {1, 1, 1, 1}}
}};
constexpr std::array<std::uint32_t, 6> triangles{0, 1, 2, 0, 2, 3};
constexpr std::array<std::uint8_t, 16> checker{
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 255};
void pixel(const Readback &image, unsigned x, unsigned y, std::array<int, 4> bgra, int tolerance, const char *message) {
    check(x < image.width && y < image.height, "Readback sample is inside the owned target");
    const auto offset = std::size_t(y) * image.rowBytes + x * 4;
    for (std::size_t channel = 0; channel < 4; ++channel) {
        ++checks;
        if (std::abs(static_cast<int>(image.pixels.at(offset + channel)) - bgra[channel]) > tolerance)
            throw std::runtime_error(std::string(message) + " channel" + std::to_string(channel) +
                " got" + std::to_string(image.pixels.at(offset + channel)) + " expected" + std::to_string(bgra[channel]));
    }
}
double encoded(double linear) {
    return linear <= .0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - .055;
}
bool shutterContains(const endfield::core::ShutterPath& path, endfield::core::Point point) {
    // Independent polygon/ray crossing oracle, not the shader's half spaces.
    for (const auto& strip : path) {
        std::array<endfield::core::Point, 5> polygon{};
        for (std::size_t i = 0; i < polygon.size(); ++i) polygon[i] = {strip[i].x, strip[i].y};
        if (endfield::core::polygonContains(polygon, point)) return true;
    }
    return false;
}
void shutterCoverage(Renderer& renderer, DrawObject& object) {
    using namespace endfield::core;
    renderer.setCamera({}); object.world = {}; object.opacity = 1; object.textureID.clear();
    object.linearTint = {1, 0, 0, 1}; object.masks.clear();
    PlaneShutter shutter;
    shutter.worldToLocal.values[0] = 220; shutter.worldToLocal.values[5] = -220;
    shutter.worldToLocal.values[12] = shutter.worldToLocal.values[13] = 220;
    std::size_t coveredPixels{}, hiddenPixels{};
    for (const auto direction : {MotionPoint{-1,0}, MotionPoint{1,0}, MotionPoint{0,-1}, MotionPoint{0,1}})
        for (bool revealing : {false, true})
            for (double elapsed : {0., .003, .054, .125, .21, .299, .3}) {
                shutter.strips = ModuleTransitionStyle::shutterAt(elapsed, direction, revealing);
                object.shutter = shutter;
                renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
                const auto image = renderer.readback();
                for (unsigned y = 0; y < image.height; ++y) for (unsigned x = 0; x < image.width; ++x) {
                    const Point local{(x + .5) * 440 / image.width, (y + .5) * 440 / image.height};
                    const bool covered = shutterContains(shutter.strips, local);
                    pixel(image, x, y, covered ? std::array<int,4>{0,0,255,255} : std::array<int,4>{0,0,0,0}, 0,
                        "Original six-strip union matches polygon coverage at every owned pixel center");
                    coveredPixels += covered ? 1u : 0u; hiddenPixels += covered ? 0u : 1u;
                }
            }
    check(coveredPixels > 0 && hiddenPixels > 0, "Original reveal/retract exercise both coverage states in all directions");

    // Keep positive-area strips even when thinner than one device pixel; only
    // truly collapsed endpoints are absent from the union's active strip set.
    shutter = {}; shutter.strips[0] = {{{0,0},{1e-15,0},{1e-15,1},{0,1},{0,1}}};
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    const auto thin = renderer.readback();
    check(std::all_of(thin.pixels.begin(), thin.pixels.end(), [](auto c) { return c == 0; }),
        "A positive-area subpixel strip remains valid without opening the plane");

    // Explicit large bevel ensures this cannot pass as a rectangle-only mask.
    shutter = {}; shutter.strips[0] = {{{-1,-1},{.75,-1},{.75,.25},{0,1},{-1,1}}};
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    auto image = renderer.readback();
    pixel(image, 20, 8, {0,0,255,255}, 0, "Pentagonal bevel keeps its interior");
    pixel(image, 26, 4, {0,0,0,0}, 0, "Pentagonal bevel removes a point inside its bounding rectangle");
    std::reverse(shutter.strips[0].begin(), shutter.strips[0].end());
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    check(renderer.readback().pixels == image.pixels, "Reversed polygon winding preserves exact shader coverage");
    shutter.worldToLocal.values[12] = .5;
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    pixel(renderer.readback(), 26, 16, {0,0,0,0}, 0, "Shutter uses its own world-to-wrapper transform");
    shutter.worldToLocal = {}; shutter.worldToLocal.values[15] = -1;
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    image = renderer.readback();
    check(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto c) { return c == 0; }),
        "A shutter plane behind its homogeneous camera cannot reveal pixels");

    shutter = {}; shutter.strips[0] = {{{-1,-1},{1,-1},{1,1},{1,1},{-1,1}}};
    object.shutter = shutter; object.masks = {{Matrix4{}, {-1,-1,1,2}}};
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 16, {0,0,255,255}, 0, "Full-open repeated bevel edge intersects ancestor masks");
    pixel(image, 24, 16, {0,0,0,0}, 0, "Shutter cannot bypass an ancestor rectangle");
    object.masks.clear();
    const auto before = renderer.stats();
    for (unsigned i = 0; i < 120; ++i) {
        shutter.strips = ModuleTransitionStyle::shutterAt(static_cast<double>(i) * .3 / 120, {-1,0}, true);
        shutter.worldToLocal = {}; shutter.worldToLocal.values[0] = 220; shutter.worldToLocal.values[5] = -220;
        shutter.worldToLocal.values[12] = shutter.worldToLocal.values[13] = 220;
        object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    }
    const auto after = renderer.stats();
    check(after.objectUploads > before.objectUploads && after.objectBufferAllocations == before.objectBufferAllocations &&
        after.meshUploads == before.meshUploads && after.textureUploads == before.textureUploads && after.resourceBytes == before.resourceBytes,
        "Animated shutters only update retained object constants without mesh, texture or buffer allocation");

    // Validate a late invalid shutter before mutating an earlier retained slot.
    object.shutter.reset(); auto second = object; second.sourceID = "fixture.shutter.second";
    second.opacity = .25f; std::array batch{object, second};
    renderer.setDrawList(batch); renderer.draw(false); const auto preserved = renderer.readback().pixels;
    const auto priorUploads = renderer.stats().objectUploads;
    batch[0].opacity = .1f; batch[1].shutter = PlaneShutter{};
    batch[1].shutter->strips[0] = {{{0,0},{1,0},{.2,.2},{1,1},{0,1}}};
    rejects([&] { renderer.setDrawList(batch); }, "Concave shutter rejects the complete retained update before upload");
    check(renderer.stats().objectUploads == priorUploads, "A later invalid shutter leaves earlier object uniforms untouched");
    renderer.draw(false); check(renderer.readback().pixels == preserved, "Rejected shutter batch preserves prior target pixels");
    batch[1].shutter->strips = {}; batch[1].shutter->strips[0][0].x = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { renderer.setDrawList(batch); }, "Nonfinite shutter vertices fail before any upload");
    renderer.setDrawList(std::span(&object, 1));
}
void run(HWND window, const std::filesystem::path &shader, bool composition, bool hardware) {
    Renderer renderer;
    RendererOptions options{hardware ? Driver::hardware : Driver::warpForTests, shader,
                            composition ? RenderTarget::composition : RenderTarget::offscreenForTests};
    renderer.initialize(window, 32, 32, options);
    check(renderer.stats().initialized, "Explicit test renderer initializes");
    const auto beforeIdentity = renderer.stats();
    check(!renderer.deviceInfo().name.empty(), "Diagnostic identity reports the actual selected adapter");
    check(renderer.stats().resourceBytes == beforeIdentity.resourceBytes && renderer.stats().presents == beforeIdentity.presents,
        "Reading adapter identity creates no retained HUD resources or frames");
    check(!IsWindowVisible(window), "The synthetic fixture window stays hidden");
    check(renderer.setMesh("fixture.quad", 1, {quad, triangles}), "First mesh revision uploads retained geometry");
    check(!renderer.setMesh("fixture.quad", 1, {quad, triangles}), "Same mesh revision does not upload again");
    check(renderer.setTexture("fixture.checker", 1, {2, 2, checker, TextureColorSpace::sRGB, TextureFilter::nearest}),
          "First texture revision uploads the synthetic checker");
    check(!renderer.setTexture("fixture.checker", 1, {2, 2, checker}), "Same texture revision does not upload again");
    DrawObject object;
    object.sourceID = "fixture.object"; object.meshID = "fixture.quad"; object.textureID = "fixture.checker";
    renderer.setDrawList(std::span(&object, 1));
    const auto uploaded = renderer.stats();
    renderer.setDrawList(std::span(&object, 1));
    check(renderer.stats().objectUploads == uploaded.objectUploads, "Identical object constants retain their GPU buffer");
    renderer.draw(false);
    auto image = renderer.readback();
    pixel(image, 8, 8, {0, 0, 255, 255}, 1, "Top-left checker quadrant preserves RGBA texture orientation");
    pixel(image, 24, 8, {0, 255, 0, 255}, 1, "Top-right checker quadrant remains green");
    pixel(image, 8, 24, {255, 0, 0, 255}, 1, "Bottom-left checker quadrant remains blue");
    pixel(image, 24, 24, {255, 255, 255, 255}, 1, "Bottom-right checker quadrant remains white");

    object.opacity = .5f;
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 8, {0, 0, 128, 128}, 1, "Transparent red uses encoded-space premultiplication without a bright fringe");
    pixel(image, 24, 24, {128, 128, 128, 128}, 1, "Half-alpha white is128 RGB, not the sRGB transfer of0.5");

    const std::array<std::uint8_t, 8> transparentEdge{255, 0, 0, 255, 0, 0, 0, 0};
    renderer.setTexture("fixture.edge", 1, {2, 1, transparentEdge, TextureColorSpace::sRGB, TextureFilter::linear});
    object.opacity = 1; object.textureID = "fixture.edge";
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 16, 16, {0, 0, 120, 120}, 1,
          "Bilinear transparent-edge filtering uses premultiplied linear texels without a dark fringe");

    const std::array<std::uint8_t, 4> middleGray{128, 128, 128, 255};
    renderer.setTexture("fixture.gray", 1, {1, 1, middleGray, TextureColorSpace::sRGB, TextureFilter::nearest});
    object.opacity = 1; object.textureID = "fixture.gray";
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 16, 16, {128, 128, 128, 255}, 1, "An sRGB texture decodes to linear and encodes exactly once");
    renderer.setTexture("fixture.gray", 2, {1, 1, middleGray, TextureColorSpace::linear, TextureFilter::nearest});
    renderer.draw(false); image = renderer.readback();
    pixel(image, 16, 16, {188, 188, 188, 255}, 1, "Replacing an in-use linear texture updates retained draws without rebuilding them");

    auto red = object, blue = object;
    red.sourceID = "fixture.red"; blue.sourceID = "fixture.blue";
    red.textureID.clear(); blue.textureID.clear();
    red.linearTint = {1, 0, 0, 1}; blue.linearTint = {0, 0, 1, 1};
    red.opacity = blue.opacity = .5f;
    const std::array layers{red, blue};
    renderer.setDrawList(layers); renderer.draw(false); image = renderer.readback();
    const int expectedRed = static_cast<int>(std::lround(encoded(1.0 / 3) * .75 * 255));
    const int expectedBlue = static_cast<int>(std::lround(encoded(2.0 / 3) * .75 * 255));
    pixel(image, 16, 16, {expectedBlue, 0, expectedRed, 191}, 2,
          "Overlapping translucent triangles blend in linear space before presentation encoding");

    red.opacity = 1;
    red.masks = {{endfield::core::Matrix4{}, {-1, -1, 1, 2}}};
    renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 16, {0, 0, 255, 255}, 1, "Plane-local clipping keeps the covered half");
    pixel(image, 24, 16, {0, 0, 0, 0}, 0, "Clipped pixels stay fully transparent");
    red.masks[0].worldToLocal.values[12] = -.5;
    red.masks[0].bounds = {-.5, -1, 1, 2};
    renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 16, {0, 0, 0, 0}, 0, "Masks use their own world-to-plane transform");
    pixel(image, 24, 16, {0, 0, 255, 255}, 1, "The transformed mask covers its intended plane region");
    red.masks.push_back({endfield::core::Matrix4{}, {-1, 0, 2, 1}});
    renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 24, 8, {0, 0, 255, 255}, 1, "Nested plane masks intersect");
    pixel(image, 24, 24, {0, 0, 0, 0}, 0, "Nested masks cannot reveal outside either ancestor");

    shutterCoverage(renderer, red);

    const auto beforePointer = renderer.stats();
    for (int i = 0; i < 120; ++i) {
        endfield::core::Matrix4 camera;
        camera.values[12] = .1 * std::sin((i + 1) * .1);
        renderer.setCamera(camera); renderer.draw(false);
    }
    const auto afterPointer = renderer.stats();
    check(afterPointer.cameraUploads == beforePointer.cameraUploads + 120,
          "Every changed pointer camera uploads only its separate camera uniform");
    check(afterPointer.meshUploads == beforePointer.meshUploads && afterPointer.textureUploads == beforePointer.textureUploads &&
          afterPointer.objectUploads == beforePointer.objectUploads && afterPointer.resourceBytes == beforePointer.resourceBytes,
          "Pointer motion never recreates retained meshes, textures, object uniforms or layouts");
    const auto beforeObjects = renderer.stats();
    for (int i = 0; i < 120; ++i) {
        red.opacity = .25f + .5f * (static_cast<float>(i) / 119);
        red.world.values[13] = .05 * std::sin((i + 1) * .1);
        renderer.setDrawList(std::span(&red, 1)); renderer.draw(false);
    }
    const auto afterObjects = renderer.stats();
    check(afterObjects.objectUploads == beforeObjects.objectUploads + 120 &&
          afterObjects.objectBufferAllocations == beforeObjects.objectBufferAllocations,
          "Animated object transforms and opacity update retained uniforms without allocating new buffers");
    check(!renderer.removeMesh("fixture.quad"), "A draw cannot outlive its referenced mesh");
    check(renderer.removeTexture("fixture.checker"), "Unused textures are explicitly removable");
    rejects([&] { renderer.setMesh("fixture.quad", 2, {quad, std::array<std::uint32_t, 3>{0, 1, 99}}); },
            "Invalid replacement indices are rejected before replacing a live mesh");
    rejects([&] { renderer.setTexture("invalid", 1, {8193, 8193, middleGray}); }, "Oversized texture dimensions are rejected before allocation");
    rejects([&] { renderer.initialize(nullptr, 32, 32, options); }, "Invalid replacement initialization fails explicitly");
    rejects([&] { renderer.initialize(window, 32, 32, options); }, "Same-window device replacement explicitly requires reset before target recreation");
    check(renderer.stats().initialized && renderer.stats().objects == 1, "Failed replacement initialization retains the current renderer");
    renderer.setCamera({}); renderer.resize(48, 24); renderer.draw(false); image = renderer.readback();
    check(image.width == 48 && image.height == 24 && image.pixels.size() == 48 * 24 * 4,
          "Resize rebinds the owned target and exact readback dimensions");
    check(renderer.stats().meshUploads == afterObjects.meshUploads && renderer.stats().objectUploads == afterObjects.objectUploads,
          "Resize retains scene geometry and object constants");

    if (composition) {
        // Exercise real swap-chain flips with nonempty retained content. Read
        // only our target BEFORE each Present; never capture the desktop or
        // interpret an undefined post-Present back buffer as evidence.
        red.world = {}; red.masks.clear();
        const auto beforePresent = renderer.stats();
        for (unsigned frame = 0; frame < 3; ++frame) {
            const bool isBlue = frame == 1;
            red.linearTint = isBlue ? std::array<float, 4>{0, 0, 1, 1} : std::array<float, 4>{1, 0, 0, 1};
            red.opacity = .5f + .25f * static_cast<float>(frame);
            renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
            const int level = static_cast<int>(std::lround(red.opacity * 255));
            pixel(image, 24, 12, {isBlue ? level : 0, 0, isBlue ? 0 : level, level}, 1,
                  "The composition back buffer contains the expected nonempty retained fixture before presentation");
            std::size_t covered{};
            for (std::size_t offset = 3; offset < image.pixels.size(); offset += 4)
                covered += image.pixels[offset] != 0 ? 1u : 0u;
            check(covered == std::size_t(image.width) * image.height,
                  "Every owned fixture pixel has nonzero alpha before the native flip");
            renderer.draw(true);
            check(renderer.stats().presents == beforePresent.presents + frame + 1 && renderer.stats().objects == 1,
                  "The native composition path submits each nonempty retained frame");
            rejects([&] { renderer.readback(); }, "Readback rejects an undefined post-Present back buffer");
        }
        check(renderer.stats().meshUploads == beforePresent.meshUploads && renderer.stats().textureUploads == beforePresent.textureUploads,
              "Consecutive composition flips retain their mesh and texture resources");
        check(!IsWindowVisible(window), "Nonempty composition submission never shows the test window");
    }

    renderer.clearResources(); renderer.draw(false); image = renderer.readback();
    const auto empty = renderer.stats();
    check(empty.meshes == 0 && empty.textures == 0 && empty.objects == 0 && empty.resourceBytes == 0,
          "Explicit scene cleanup drops every cached application resource");
    pixel(image, 12, 12, {0, 0, 0, 0}, 0, "An empty frame clears stale content to transparent");
    if (!composition) {
        rejects([&] { renderer.draw(true); }, "Offscreen testing cannot present to the user desktop");
        check(renderer.stats().presents == 0, "Offscreen testing performs zero presentations");
    }
    renderer.reset(); renderer.reset();
    check(!renderer.stats().initialized && renderer.stats().resourceBytes == 0, "Reset is idempotent and releases renderer ownership");
    for (int cycle = 0; cycle < 3; ++cycle) {
        renderer.initialize(window, 16, 16, options);
        renderer.draw(false);
        pixel(renderer.readback(), 8, 8, {0, 0, 0, 0}, 0, "Recreated devices begin with transparent content");
        renderer.reset();
    }
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) { std::cerr << "COM initialization failed\n"; return 1; }
    int result = 0;
    try {
        check(argc >= 2 && argc <= 4, "Pass fresh native/hud.hlsl and optional --composition / --hardware");
        bool composition = false, hardware = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring flag(argv[i]);
            check(flag == L"--composition" || flag == L"--hardware", "Unknown native renderer test option");
            composition = composition || flag == L"--composition";
            hardware = hardware || flag == L"--hardware";
        }
        OwnedWindow window;
        run(window.get(), std::filesystem::path(argv[1]), composition, hardware);
        std::cout << "Passed " << checks << " synthetic native renderer checks; no screen capture or live HUD parity claim\n";
    } catch (const std::exception &error) {
        std::cerr << "Native renderer check failed: " << error.what() << '\n'; result = 1;
    }
    CoUninitialize();
    return result;
}
