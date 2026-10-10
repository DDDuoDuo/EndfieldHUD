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
    check(scene.setContent(content), "First content event installs resources");
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
    // Settled compact capsule, dark theme.
    timeline.setStage(m::ChargeStage::compact, false, 0);
    image = frame(0, false);
    dump(image, "compact-dark");
    const auto plate = at(image, 190, 26);
    check(plate.a >= 250 && std::abs(plate.r - 27) <= 3 && plate.r == plate.g, "Dark plate is the source 0.105 white at 0.99 alpha");
    check(at(image, 5, 5).a == 0 && at(image, 150, 75).a == 0, "Outside the capsule and its shadow stays clear");
    const auto shadow = at(image, 150, 62.5);
    check(shadow.a > 2 && shadow.a < 90 && shadow.r <= 6, "Soft black shadow below the capsule");
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
