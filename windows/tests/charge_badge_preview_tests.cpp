// Hidden WARP fixture for the HUD charge badge owner: synthetic reading,
// offscreen target, injected Power selection. No provider, window or user data.
#ifdef _WIN32
#include "tools/charge_badge_preview.hpp"
#include "core/source_desktop_chrome.hpp"
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>

namespace {
namespace gpu = endfield::native;
namespace c = endfield::core;
namespace a = endfield::app;
namespace t = endfield::tools;
namespace m = endfield::modules;
unsigned checks{};
void check(bool v, const std::string& message) {
    ++checks;
    if (!v) throw std::runtime_error(message);
}
constexpr unsigned width = 1280, height = 800;
struct Window {
    HWND h{};
    Window() {
        WNDCLASSW w{};
        w.lpfnWndProc = DefWindowProcW;
        w.hInstance = GetModuleHandleW(nullptr);
        w.lpszClassName = L"EndfieldChargeBadgeFixture";
        check(RegisterClassW(&w) != 0, "Register own fixture");
        h = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, w.lpszClassName, L"Synthetic charge badge only", WS_POPUP, 0, 0, width, height,
                            nullptr, nullptr, w.hInstance, nullptr);
        check(h && !IsWindowVisible(h), "Own fixture stays hidden");
    }
    ~Window() { if (h) DestroyWindow(h); UnregisterClassW(L"EndfieldChargeBadgeFixture", GetModuleHandleW(nullptr)); }
};

void run(const std::filesystem::path& shader) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.h, width, height, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    const auto camera = gpu::layerViewportProjection(width, height);
    renderer.setCamera(camera);
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    unsigned selected{};
    t::ChargeBadgePreview badge(raster, [&] { ++selected; });
    bool done{};
    struct Cleanup {
        gpu::Renderer& r; gpu::LayerComposition& c; t::ChargeBadgePreview& b; bool& done;
        ~Cleanup() { if (done) return; try { c.detach(r); b.release(r); } catch (...) { r.reset(); } }
    } cleanup{renderer, composition, badge, done};
    c::source::DesktopChromeSettings settings{{0, 0, width, height}, 1, {}, c::Module::power, true};
    const auto layout = c::source::DesktopChromeLayout::make(settings, {}, {});
    const auto center = layout.designToScreen;
    badge.resize({width, height, 96, 1, width, height}, layout.designScale);
    m::BatteryReading reading;
    reading.present = reading.pluggedIn = reading.charging = true;
    reading.percentage = 75;
    m::ChargeIndicatorAppearance appearance;
    badge.setContent(reading, appearance);
    // 1.081 design scale x 0.82 x 1 rounds up to the 1x quarter step.
    check(badge.rasterDensity() == 1 && badge.densityChanges() == 0, "The first content rasterizes once for the 1x rest pose");
    auto project = [&](double x, double y) {
        const auto q = c::Projection::viewport(camera * center, width, height).project({x, y});
        check(q.has_value(), "Design point projects");
        return *q;
    };
    double now{};
    std::size_t published{};
    auto frame = [&](double time) {
        now = time;
        const auto events = badge.update(center, 1, now);
        badge.upload(renderer);
        const auto entries = badge.entries();
        if (entries.size() != published) { composition.setEntries(renderer, entries); published = entries.size(); }
        composition.present(renderer);
        return events;
    };
    auto pixel = [&](c::Point p) {
        renderer.draw(false);
        const auto image = renderer.readback();
        const auto x = static_cast<unsigned>(std::lround(p.x)), y = static_cast<unsigned>(std::lround(p.y));
        const auto* q = &image.pixels[static_cast<std::size_t>(y) * image.rowBytes + x * 4];
        return std::array<int, 4>{q[0], q[1], q[2], q[3]};
    };
    check(badge.entries().empty() && !badge.nextWakeTime(), "Closed HUD: badge submits nothing and owns no deadline");
    // Launch prewarm while the HUD is closed: the hidden badge is placed once
    // on the first opening's plane and its artwork installed (the CPU pixels
    // go); nothing composes or wakes, and the reveal 0.58 s into the
    // opening uploads nothing.
    const auto pending = badge.retainedBytes();
    check(badge.uploadPending() && pending > 0, "The first content waits for its upload with its CPU pixels");
    badge.prewarm(center, 0);
    badge.upload(renderer);
    check(renderer.stats().textures == 0 && badge.entries().empty(), "The hidden badge's own upload installs nothing");
    badge.preload(renderer);
    const auto preloaded = renderer.stats().textureUploads;
    check(renderer.stats().textures == 17 && !badge.uploadPending() && badge.retainedBytes() == 0 && badge.entries().empty() &&
          !badge.nextWakeTime() && !badge.requiresFrames(0) && badge.state().phase() == m::ChargeBadgeState::Phase::hidden,
          "The prewarm installs the artwork without composing, waking or showing the badge");
    std::cout << "The 1x badge prewarm released " << pending << " bytes of CPU pixels\n";
    badge.animateEntrance(0, 7);
    frame(0);
    check(badge.entries().empty() && badge.nextWakeTime() == 0.58 && !badge.requiresFrames(0.3), "Delayed reveal is a single wake, not frames");
    auto events = frame(0.58);
    check(events.hitRegionChanged && badge.requiresFrames(0.6) && badge.entries().size() == 1, "Reveal starts the notification sequence");
    check(renderer.stats().textureUploads == preloaded, "After the prewarm the reveal uploads nothing");
    for (double time = 0.6; time < 2.2; time += 1.0 / 60) frame(time);
    check(badge.state().stage() == m::ChargeStage::compact && badge.state().phase() == m::ChargeBadgeState::Phase::presented,
          "Entrance settles compact");
    const auto uploads = renderer.stats().textureUploads;
    check(!badge.requiresFrames(now) && badge.nextWakeTime() && std::abs(*badge.nextWakeTime() - 5.08) < 1e-6,
          "Compact hold is one 3 s deadline after the 1.5 s entrance");
    // Capsule plate (design y offset by the source shell's 38 points).
    const auto plate = project(377 + 190 * 0.82, 411.56 + 38 + 26 * 0.82);
    const auto shown = pixel(plate);
    check(shown[3] > 200 && shown[2] < 60, "Dark capsule plate painted at the source badge position");
    // Collapse to the circle after the hold.
    frame(5.08);
    for (double time = 5.1; time < 5.5; time += 1.0 / 60) frame(time);
    check(badge.state().stage() == m::ChargeStage::circle && !badge.requiresFrames(now), "Collapses to the circle unless hovered");
    check(pixel(plate)[3] < 10, "The circle releases the capsule area");
    // Hover the circle: expands; click selects Power.
    const auto middle = project(500, 446 + 38);
    check(badge.covers(middle), "Circle hit region");
    check(badge.pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y}, now) && badge.hovered(), "Hover acquires");
    for (double start = now, time = now; time < start + 0.4; time += 1.0 / 60) frame(time);
    check(badge.state().stage() == m::ChargeStage::compact, "Hover expands to the compact capsule");
    const auto outer = project(500 + 100, 446 + 38);
    check(badge.covers(outer), "Expanded capsule keeps the whole compact target");
    check(badge.pointer({a::PointerKind::down, a::PointerButton::left, outer.x, outer.y}, now) &&
          badge.pointer({a::PointerKind::up, a::PointerButton::left, outer.x, outer.y}, now) && selected == 1, "Click selects Power");
    check(badge.pointer({a::PointerKind::leave, a::PointerButton::none, 0, 0}, now) && !badge.hovered(), "Leave clears hover");
    for (double start = now, time = now; time < start + 0.4; time += 1.0 / 60) frame(time);
    check(badge.state().stage() == m::ChargeStage::circle, "Unhover returns to the circle");
    // The Power navigation entry's accessibility frame: the circle's hit rect
    // projected through the charge plane (logical client points).
    const auto bounds = badge.clientHitBounds();
    check(bounds && bounds->width > 20 && bounds->width < 60 && std::abs(bounds->width - bounds->height) < 4 &&
          std::abs(bounds->x + bounds->width / 2 - middle.x) < 2.5 && std::abs(bounds->y + bounds->height / 2 - middle.y) < 2.5,
          "The circle's projected hit bounds centre on the badge");
    // Before the owner reports the navigation target, leaving un-hovers at once.
    const auto offBadge = project(500, 200);
    check(badge.pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y}, now) && badge.hovered() &&
          !badge.pointer({a::PointerKind::move, a::PointerButton::none, offBadge.x, offBadge.y}, now) && !badge.hovered(), "Without a navigation target, leaving un-hovers");
    // SystemHUDView.updateHover + refreshChargeHover: the Power entry alone
    // never acquires the hover, but holds one carried straight onto it.
    check(!badge.setNavigationHovered(true, now) && !badge.hovered(), "The Power navigation entry alone does not hover the badge");
    check(!badge.setNavigationHovered(false, now) && badge.pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y}, now) && badge.hovered(),
          "The badge itself acquires the hover");
    check(!badge.pointer({a::PointerKind::move, a::PointerButton::none, offBadge.x, offBadge.y}, now) && badge.hovered(), "A move off waits for the navigation target");
    check(!badge.setNavigationHovered(true, now) && badge.hovered(), "Landing on the Power entry holds the hover");
    check(badge.setNavigationHovered(false, now) && !badge.hovered(), "Leaving the Power entry releases it");
    check(badge.pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y}, now) &&
          !badge.pointer({a::PointerKind::move, a::PointerButton::none, offBadge.x, offBadge.y}, now) && badge.hovered() &&
          badge.setNavigationHovered(false, now) && !badge.hovered(), "No entry under the pointer resolves the pending move to un-hover");
    for (double start = now, time = now; time < start + 0.4; time += 1.0 / 60) frame(time);
    check(badge.state().stage() == m::ChargeStage::circle && !badge.requiresFrames(now), "The released badge settles to the circle");
    check(renderer.stats().textureUploads == uploads, "Morphs and hover reuse every texture");
    check(badge.accessibilityLabel() == "EndfieldHUD, Battery: 75%, Charging", "Power entry accessibility help");
    // Content event: unplugged reading updates the label once.
    reading.pluggedIn = reading.charging = false;
    badge.setContent(reading, appearance);
    frame(now + 0.01);
    check(badge.accessibilityLabel() == "EndfieldHUD, Battery: 75%, On battery", "Live power state");
    // Exit with the HUD close.
    badge.animateExit(now, 3);
    check(!badge.covers(middle), "Retraction releases input immediately");
    const double exitStart = now;
    for (double time = now; time <= exitStart + 0.66; time += 1.0 / 60) frame(time);
    frame(exitStart + 0.7);
    check(badge.exitFinished() && badge.entries().empty() && !badge.requiresFrames(now), "Exit completes with the source 0.64 s");
    // Rest-pose raster density, as Core Animation scales layer contents: a
    // DPI change re-rasterizes the artwork once; the deployment transforms of
    // an opening and a closing re-rasterize and re-upload nothing (a density
    // measured per frame would re-rasterize at every quarter step between 1
    // and 2 here, while opening and again while closing).
    {
        const auto rasterized = raster.stats().rasterizations;
        badge.resize({2 * width, 2 * height, 192, 2, width, height}, layout.designScale);
        check(badge.rasterDensity() == 2 && badge.densityChanges() == 1 && raster.stats().rasterizations > rasterized,
              "A 2x DPI change re-rasterizes the artwork once for its new rest pose");
        const auto redrawn = raster.stats().rasterizations;
        const auto lifted = [&](double s) {
            return center * c::Matrix4::translation(500, 484) * c::Matrix4::scale(s, s) * c::Matrix4::translation(-500, -484);
        };
        badge.animateEntrance(now, 11);
        const double opened = now;
        std::optional<std::uint64_t> revealUploads;
        for (double time = opened; time <= opened + 2.2; time += 1.0 / 60) {
            now = time;
            (void)badge.update(lifted(std::min(1.0, 0.2 + (time - opened) * 0.8 / 0.9)), 1, now); // the core plane grows into place
            badge.upload(renderer);
            if (!revealUploads && !badge.entries().empty()) revealUploads = renderer.stats().textureUploads;
        }
        check(revealUploads && raster.stats().rasterizations == redrawn && badge.densityChanges() == 1 && badge.rasterDensity() == 2 &&
              renderer.stats().textureUploads == *revealUploads, "The opening lift re-rasterizes and re-uploads nothing after the reveal");
        badge.animateExit(now, 5);
        const double closing = now;
        for (double time = closing; time <= closing + 0.7; time += 1.0 / 60) {
            now = time;
            (void)badge.update(lifted(std::max(0.08, 1 - (time - closing) * 0.92 / 0.64)), 1, now); // retracts inward
            badge.upload(renderer);
        }
        check(badge.exitFinished() && raster.stats().rasterizations == redrawn && renderer.stats().textureUploads == *revealUploads,
              "The closing retraction re-rasterizes and re-uploads nothing");
        badge.resize({width, height, 96, 1, width, height}, layout.designScale);
        check(badge.rasterDensity() == 1 && badge.densityChanges() == 2, "Returning to 1x re-rasterizes once more");
    }
    // Reduce Motion: compact immediately.
    badge.setReduceMotion(true, now);
    badge.animateEntrance(now, 9);
    frame(now + 0.01);
    check(badge.state().stage() == m::ChargeStage::compact && !badge.requiresFrames(now), "Reduce Motion presents compact without animation");
    check(!badge.entries().empty() && badge.covers(middle), "The stable badge is composed and hit-testable");
    badge.setStable(false, now + 0.02);
    check(badge.entries().empty() && !badge.covers(middle) && !badge.nextWakeTime(), "A concealed HUD hides the badge at once, before any frame");
    composition.detach(renderer);
    badge.release(renderer);
    done = true;
    check(renderer.stats().textures == 0 && renderer.stats().meshes == 0 && !IsWindowVisible(window.h), "Badge releases all resources");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result) && argc == 2, "Pass HLSL to the synthetic charge badge test");
        run(argv[1]);
        CoUninitialize();
        std::cout << "Charge badge owner: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(result)) CoUninitialize();
        std::cerr << "Charge badge owner after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
