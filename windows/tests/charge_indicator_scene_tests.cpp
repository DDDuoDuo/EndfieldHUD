// Hidden WARP fixture: synthetic readings only, offscreen target, no visible
// window, power provider, settings file or user data.
#ifdef _WIN32
#include "native/charge_indicator_scene.hpp"
#include "native/layer_scene.hpp"
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace {
namespace gpu = endfield::native;
namespace c = endfield::core;
namespace m = endfield::modules;
using Json = ehud::data::Json;
unsigned checks{};
void check(bool v, const std::string& message) {
    ++checks;
    if (!v) throw std::runtime_error(message);
}
constexpr unsigned width = 640, height = 200;
struct Window {
    HWND h{};
    Window() {
        WNDCLASSW w{};
        w.lpfnWndProc = DefWindowProcW;
        w.hInstance = GetModuleHandleW(nullptr);
        w.lpszClassName = L"EndfieldChargeSceneFixture";
        check(RegisterClassW(&w) != 0, "Register own fixture");
        h = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, w.lpszClassName, L"Synthetic charge indicator only", WS_POPUP, 0, 0, width, height,
                            nullptr, nullptr, w.hInstance, nullptr);
        check(h && !IsWindowVisible(h), "Own fixture stays hidden");
    }
    ~Window() { if (h) DestroyWindow(h); UnregisterClassW(L"EndfieldChargeSceneFixture", GetModuleHandleW(nullptr)); }
};
struct Pixel { int b, g, r, a; };

std::filesystem::path dumpDirectory; // optional explicit review output (never registered)
void dump(const gpu::Readback& image, const char* name) {
    if (dumpDirectory.empty()) return;
    std::ofstream out(dumpDirectory / (std::string(name) + ".bgra"), std::ios::binary);
    const std::uint32_t header[3]{image.width, image.height, image.rowBytes};
    out.write(reinterpret_cast<const char*>(header), sizeof header);
    out.write(reinterpret_cast<const char*>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
}
void run(const std::filesystem::path& shader) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.h, width, height, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    renderer.setCamera(gpu::layerViewportProjection(width, height));
    gpu::LayerRasterizer raster;
    gpu::LayerScene anchor(raster);
    anchor.load(Json::Object{{"bounds", Json::Array{0, 0, 1, 1}}, {"children", Json::Array{}}}, {});
    gpu::LayerComposition composition;
    gpu::ChargeIndicatorScene scene(raster, "test.charge");
    bool released{};
    struct Cleanup {
        gpu::Renderer& r; gpu::LayerComposition& c; gpu::ChargeIndicatorScene& s; bool& done;
        ~Cleanup() { if (done) return; try { c.detach(r); s.release(r); } catch (...) { r.reset(); } }
    } cleanup{renderer, composition, scene, released};

    gpu::ChargeIndicatorSceneContent content;
    content.battery = m::BatteryReading{};
    content.battery.present = content.battery.pluggedIn = content.battery.charging = true;
    content.battery.percentage = 75;
    content.battery.capacity = m::BatteryReading::Capacity{3600, 4800, "mAh"};
    content.pixelsPerPoint = 2;
    check(raster.stats().entries == 0 && scene.retainedBytes() == 0, "Nothing is rasterized before the first content event");
    check(scene.setContent(content), "First content event installs resources");
    // Before the upload the scene holds every texture's pixels: the eight
    // procedural slices and the nine rasterized leaves (ripple, bolt, ring,
    // cap, laptop and four labels) in the shared rasterizer.
    const auto pending = scene.retainedBytes();
    check(pending > 640u * 640u * 4u && raster.stats().entries == 9, "The pending content holds its CPU pixels until the upload");
    check(!scene.setContent(content), "Identical content is not a content event");
    check(scene.content().reading.trailing == "75%" && scene.content().reading.primary == "3,600", "Source battery reading");
    check(scene.content().accessibility == "EndfieldHUD, Battery: 75%, Charging", "Source accessibility label");

    m::ChargeIndicatorTimeline timeline;
    const c::Matrix4 canvasToWorld = c::Matrix4::translation(20, 16) * c::Matrix4::scale(2, 2);
    auto frame = [&](double time, bool publish) {
        gpu::ChargeIndicatorPlacement p;
        p.canvasToWorld = canvasToWorld;
        p.pose = timeline.sample(time);
        p.stage = timeline.stage();
        scene.place(p);
        scene.upload(renderer);
        const std::array<gpu::LayerCompositionEntry, 1> entries{gpu::LayerCompositionEntry{&anchor, scene.draws()}};
        if (publish) composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        return renderer.readback();
    };
    auto at = [&](const gpu::Readback& image, double cx, double cy) {
        const auto x = static_cast<unsigned>(std::lround(20 + cx * 2)), y = static_cast<unsigned>(std::lround(16 + cy * 2));
        const auto* p = &image.pixels[static_cast<std::size_t>(y) * image.rowBytes + x * 4];
        return Pixel{p[0], p[1], p[2], p[3]};
    };
    // Hidden: nothing is painted.
    timeline.setStage(m::ChargeStage::hidden, false, 0);
    auto image = frame(0, true);
    check(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto v) { return v == 0; }), "Hidden indicator paints nothing");
    // The installed GPU textures are the only resident copy (Core Animation
    // keeps one backing per layer): the CPU pixels and rasterizer entries go.
    check(!scene.uploadPending() && scene.retainedBytes() == 0 && raster.stats().entries == 0 && scene.regenerations() == 0 &&
          renderer.stats().textures == 17, "The upload keeps no CPU copy of an installed texture (" + std::to_string(pending) + " bytes released)");
    std::cout << "Installing the 2x content released " << pending << " bytes of CPU pixels (" << renderer.stats().resourceBytes << " GPU resource bytes)\n";
    // Settled compact capsule, dark theme.
    timeline.setStage(m::ChargeStage::compact, false, 0);
    image = frame(0, false);
    dump(image, "compact-dark");
    const auto plate = at(image, 190, 26);
    check(plate.a >= 250 && std::abs(plate.r - 27) <= 3 && plate.r == plate.g, "Dark plate is the source 0.105 white at 0.99 alpha");
    check(at(image, 5, 5).a == 0 && at(image, 150, 75).a == 0, "Outside the capsule and its shadow stays clear");
    const auto shadow = at(image, 150, 62.5);
    check(shadow.a > 2 && shadow.a < 90 && shadow.r <= 6, "Soft black shadow below the capsule");
    // Shadow falloff off the straight edges (mid-width), in device rows
    // aligned to each edge: below (offset +1 point) and above the capsule.
    std::array<int, 6> compactShadow{};
    for (std::size_t n = 0; n < 4; ++n) compactShadow[n] = at(image, 150, 61.5 + static_cast<double>(n)).a;
    for (std::size_t n = 0; n < 2; ++n) compactShadow[4 + n] = at(image, 150, 22.5 - static_cast<double>(n)).a;
    const auto corner = at(image, 18.6, 23.6);
    check(corner.a < 40, "Rounded corner is not a square quad");
    // Ring: progress 75% clockwise from the top; the upper-left quarter is track only.
    const double ringX = 263, ringY = 42, k = 11 / std::sqrt(2.0);
    const auto progress = at(image, ringX + k, ringY - k), track = at(image, ringX - k, ringY - k);
    check(progress.g > 150 && progress.g > progress.r + 60, "Green level arc at 75%");
    check(track.g < progress.g - 80, "Track beyond the arc is the dim 0.19 level");
    // Bolt over the compact emblem, labels painted.
    check(at(image, 37, 42).r > 150, "Foreground bolt in the compact emblem");
    unsigned bright{};
    for (double x = 56; x < 196; x += 0.5)
        for (double y = 34; y < 51; y += 0.5) if (at(image, x, y).r > 150) ++bright;
    check(bright > 60, "Fitted capacity label is painted");
    // Supercharge banner: the localized title is painted inside a 240 x 48 capsule.
    timeline.setStage(m::ChargeStage::supercharge, false, 0);
    image = frame(0, false);
    dump(image, "supercharge-dark");
    bright = 0;
    for (double x = 125; x < 240; x += 0.5)
        for (double y = 37; y < 62; y += 0.5) if (at(image, x, y).r > 150) ++bright;
    check(bright > 80 && at(image, 33, 42).a > 200 && at(image, 15, 42).a == 0, "CHARGE MODE banner capsule");
    // Core Animation keeps shadowRadius 4 / offset (0, 1) at the banner's
    // 14-point corners: its straight-edge falloff equals the compact one.
    bool sameShadow = compactShadow[0] > compactShadow[3] && compactShadow[3] > 0;
    for (std::size_t n = 0; n < 4; ++n) sameShadow = sameShadow && std::abs(at(image, 150, 66.5 + static_cast<double>(n)).a - compactShadow[n]) <= 2;
    for (std::size_t n = 0; n < 2; ++n) sameShadow = sameShadow && std::abs(at(image, 150, 17.5 - static_cast<double>(n)).a - compactShadow[4 + n]) <= 2;
    if (!sameShadow) {
        std::cerr << "compact shadow";
        for (const auto v : compactShadow) std::cerr << ' ' << v;
        std::cerr << " / banner";
        for (std::size_t n = 0; n < 4; ++n) std::cerr << ' ' << int(at(image, 150, 66.5 + static_cast<double>(n)).a);
        for (std::size_t n = 0; n < 2; ++n) std::cerr << ' ' << int(at(image, 150, 17.5 - static_cast<double>(n)).a);
        std::cerr << '\n';
    }
    check(sameShadow, "The 14-point banner keeps the source's 4-point shadow (exact-radius set, not a scaled 19-point one)");
    // Morph and ripples: numeric only, no uploads or allocations of resources.
    const auto stats = renderer.stats();
    timeline.setStage(m::ChargeStage::hidden, false, 0);
    timeline.animateEntrance(1);
    for (double t = 1; t < 2.6; t += 1.0 / 60) { timeline.advance(t); frame(t, false); }
    check(renderer.stats().textureUploads == stats.textureUploads && renderer.stats().meshUploads == stats.meshUploads &&
          renderer.stats().textures == stats.textures, "Entrance, ripples and morph reuse every resource");
    check(timeline.stage() == m::ChargeStage::compact, "Entrance settles compact");
    // Light theme and Chinese: one content event re-rasterizes only what changed.
    content.appearance.dark = false;
    content.appearance.language = c::Language::simplifiedChinese;
    check(scene.setContent(content) && scene.uploadPending(), "Theme/language is a content event");
    image = frame(3, true);
    const auto light = at(image, 190, 26);
    check(light.a >= 250 && light.r >= 245, "Light plate is the source 0.975 white");
    timeline.setStage(m::ChargeStage::supercharge, false, 3);
    image = frame(3, false);
    dump(image, "supercharge-light-zh");
    check(scene.content().texts[1]["text"]["string"].string() == "超充模式", "Chinese charge mode title");
    // Not plugged in: POWER MODE, and the red level below 20%.
    content.battery.pluggedIn = content.battery.charging = false;
    content.battery.percentage = 19;
    scene.setContent(content);
    check(scene.content().texts[1]["text"]["string"].string() == "电源模式", "Power mode on battery");
    timeline.setStage(m::ChargeStage::compact, false, 4);
    image = frame(4, true);
    const auto red = at(image, ringX + 11 * std::sin(0.3), ringY - 11 * std::cos(0.3));
    check(red.r > 120 && red.r > red.g + 40, "Red level arc at 19% in light theme");
    // Windows GPU reading (the Activity GPU total load): the CPU layout, the
    // accent level ring at the load, the muted level without a sample.
    {
        auto gpuContent = content;
        gpuContent.metric = m::ChargeMetric::gpu;
        gpuContent.telemetry = m::ChargeTelemetry{};
        gpuContent.telemetry->gpuPercent = 75;
        gpuContent.telemetry->cpuPercent = 10; // never read by the GPU reading
        check(scene.setContent(gpuContent) && scene.uploadPending(), "The GPU reading is a content event");
        check(scene.content().reading.primary == "GPU" && scene.content().reading.trailing == "75%" && scene.content().reading.progress == 0.75 &&
              scene.content().texts[2]["text"]["string"].string() == "GPU" && scene.content().texts[3]["text"]["string"].string() == "75%",
              "The bar's labels read GPU and its total load");
        image = frame(4.1, true);
        dump(image, "compact-light-gpu");
        const auto lit = at(image, ringX + k, ringY - k), unlit = at(image, ringX - k, ringY - k);
        check(lit.r > 200 && lit.g > 150 && lit.r > lit.b + 60, "Accent (yellow) GPU level arc through 75%");
        check(unlit.b > lit.b + 60, "Track beyond the GPU load is the dim 0.19 level");
        bright = 0;
        for (double x = 56; x < 196; x += 0.5)
            for (double y = 34; y < 51; y += 0.5) if (at(image, x, y).r < 110) ++bright;
        check(bright > 20, "The GPU label is painted on the light plate");
        gpuContent.telemetry.reset();
        check(scene.setContent(gpuContent), "Losing the GPU sample is a content event");
        check(scene.content().reading.trailing == "—" && !scene.content().reading.progress, "No GPU sample reads unavailable");
        image = frame(4.2, true);
        const auto muted = at(image, ringX + k, ringY - k);
        check(std::abs(muted.r - muted.b) < 30, "Without a GPU sample the ring shows only the muted track");
        check(scene.setContent(content), "Back to the battery reading");
        image = frame(4.3, true);
        const auto again = at(image, ringX + 11 * std::sin(0.3), ringY - 11 * std::cos(0.3));
        check(again.r == red.r && again.g == red.g && again.b == red.b, "The battery level arc returns unchanged");
    }
    // Hover border (embedded control): accent capsule outline.
    gpu::ChargeIndicatorPlacement hover;
    hover.canvasToWorld = canvasToWorld;
    hover.pose = timeline.sample(4);
    hover.pose.borderWidth = 1.5;
    hover.pose.borderColor = {250. / 255, 212. / 255, 31. / 255, 0.98};
    hover.stage = m::ChargeStage::compact;
    hover.embeddedBorder = hover.hovered = true;
    scene.place(hover);
    composition.present(renderer);
    renderer.draw(false);
    image = renderer.readback();
    dump(image, "compact-light-hover");
    const auto edge = at(image, 150, 23.6);
    check(edge.r > 200 && edge.b < 150, "Hovered accent border on the capsule edge");
    check(scene.retainedBytes() == 0 && raster.stats().entries == 0, "Content events keep no CPU copy once installed");
    // Device loss or a parked alert window: after a release the next upload
    // regenerates every texture from the content key, pixel-identically.
    composition.detach(renderer);
    check(scene.release(renderer) && renderer.stats().textures == 0 && scene.uploadPending(), "A release leaves every texture to reinstall");
    const auto regenerated = scene.regenerations();
    scene.place(hover);
    scene.upload(renderer);
    {
        const std::array<gpu::LayerCompositionEntry, 1> entries{gpu::LayerCompositionEntry{&anchor, scene.draws()}};
        composition.setEntries(renderer, entries);
    }
    composition.present(renderer);
    renderer.draw(false);
    const auto again = renderer.readback();
    check(scene.regenerations() == regenerated + 17 && renderer.stats().textures == 17 && scene.retainedBytes() == 0 && raster.stats().entries == 0,
          "The reinstall regenerates all 17 textures and again keeps no CPU copy");
    check(again.width == image.width && again.height == image.height && again.pixels == image.pixels, "Regenerated textures render pixel-identically");
    composition.detach(renderer);
    check(scene.release(renderer), "Scene releases its own resources");
    released = true;
    check(renderer.stats().textures == 0 && renderer.stats().meshes == 0 && !IsWindowVisible(window.h), "No resources remain");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result) && (argc == 2 || argc == 3), "Pass HLSL (and optionally a review output directory)");
        if (argc == 3) dumpDirectory = argv[2];
        run(argv[1]);
        CoUninitialize();
        std::cout << "Charge indicator scene: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(result)) CoUninitialize();
        std::cerr << "Charge indicator scene after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
