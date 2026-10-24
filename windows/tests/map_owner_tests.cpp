// The redesigned Map through its owner (tools/map_owner: the canvas on a
// DesignOwnerCore with its native surface) on a hidden window and WARP:
// the launch prewarm (nothing started), presentation and activation (the
// painter and the geography request), composition (the surface below the
// canvas), input in box px and outside the box (the input circle's square),
// one wheel detent per Mac notch and the .18 s gesture hold, the pinch,
// keys with Shift allowed and Ctrl passing, the cursor, UI Automation, the
// pulse fallback wake, the working set released on another module, the
// close and the release. Synthetic geography, painter and store only; no
// visible window, no input injection, no user data.
#include "tools/map_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "modules/map_geography.hpp"
#include "native/map_assets.hpp"
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
using namespace endfield;
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
namespace {
using G = m::MapSurfaceGeometry;
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldMapOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic Map", WS_POPUP, 0, 0, 1000, 640, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldMapOwnerHidden", GetModuleHandleW(nullptr)); }
};
core::ModulePresentationSample sample(core::Module module) {
    core::ModulePresentationSample s;
    s.selected = s.requested = module;
    s.current.module = module; s.current.contentFrame = core::moduleLocalContentFrame(module);
    s.current.designScreen = true; s.current.hostClip = false; s.current.attached = true; s.current.opacity = 1;
    s.acceptsModuleInput = true;
    return s;
}
std::string id(unsigned k) { char b[37]; std::snprintf(b, sizeof b, "00000000-0000-4000-8000-%012u", k); return b; }
std::shared_ptr<const gpu::MapPaintImage> image(unsigned size) { auto p = std::make_shared<gpu::MapPaintImage>(); p->width = p->height = size; p->straightRGBA.assign(std::size_t(size) * size * 4, 255); return p; }
std::shared_ptr<const m::MapGeography> geography() { auto p = std::make_shared<m::MapGeography>(); p->terrain = m::MapTerrain{}; return p; }
gpu::MapPaintResult paint(const gpu::MapPaintRequest& q, const gpu::MapPaintCancelled& cancel) {
    if (cancel()) return {};
    const auto g = *m::mapRasterGeometry(q.viewport, q.contentsScale);
    auto pixels = std::make_shared<gpu::MapPaintImage>(); pixels->width = pixels->height = g.pixelDimension; pixels->straightRGBA.assign(std::size_t(g.pixelDimension) * g.pixelDimension * 4, 127);
    gpu::MapPaintResult result{pixels, {}, m::MapRasterFrame{g.viewport, g.screenRect, g.pixelsPerPoint}, .001};
    if (q.backdrop) { auto b = std::make_shared<gpu::MapPaintImage>(); b->width = 1024; b->height = 512; b->straightRGBA.assign(1024 * 512 * 4, 96); result.backdrop = b; }
    return result;
}

void run(const std::filesystem::path& shader) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.hwnd, 1000, 640, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    app::UtilityExecutor executor([] {});
    tools::DesignOverlayOptions overlayOptions;
    overlayOptions.raster.pixelsPerPoint = 1; overlayOptions.window = window.hwnd;
    tools::DesignOverlayOwner overlay(raster, overlayOptions);
    double clock{};
    m::MapSnapshot saved; unsigned writes{}, serial{1}, requests{}, changed{};
    std::vector<m::MapPinStyle> styles; unsigned recentered{};
    tools::MapOwnerOptions o;
    o.persistence = {[&](const m::MapSnapshot& v) { saved = v; ++writes; }, [&] { return id(serial++); }, [] { return 800000000.; }};
    o.paint = paint; o.clock = [&] { return clock; }; o.changed = [&] { ++changed; };
    o.requestGeography = [&] { ++requests; };
    o.pinStyleChanged = [&](m::MapPinStyle s) { styles.push_back(s); };
    o.recentered = [&] { ++recentered; };
    o.player = {image(2), image(2), image(2)};
    app::ModuleRouting routing; routing.pointerCapture = 5; routing.pointer = 6; routing.wheel = 11; routing.key = 12;
    o.owner.name = "map"; o.owner.routing = routing; o.owner.raster.pixelsPerPoint = 1;
    tools::MapOwner map(raster, executor, overlay.context(), std::move(o));
    std::vector<app::ModuleOwner*> owners{&overlay, &map};
    struct Detach { gpu::LayerComposition& composition; gpu::Renderer& renderer; bool done{}; ~Detach() { if (!done) composition.detach(renderer); } } detach{composition, renderer};
    const app::ClientMetrics metrics{1000, 640, 96, 1, 1000, 640};
    app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255}; appearance.ambient = false;
    for (auto* owner : owners) { owner->resize(metrics); owner->setAppearance(appearance, 0); owner->setOverlayVisible(true, 0); }
    core::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, 1000, 640};
    const auto center = core::Matrix4{};
    std::size_t entryCount{};
    const auto frame = [&](core::Module module, double t) {
        clock = t;
        chrome.module = module;
        const auto s = sample(module);
        const app::ModuleFrame f{center, chrome, s, 1, t, true};
        for (auto* owner : owners) { owner->update(f); owner->upload(renderer); }
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : map.entries(module)) entries.push_back(e);
        entryCount = entries.size();
        for (const auto& e : overlay.modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        for (auto* owner : owners) owner->collected(renderer);
    };
    const auto settle = [&](core::Module module, double from, double seconds) {
        double t = from;
        for (; t < from + seconds; t += 1. / 30) { for (auto* owner : owners) (void)owner->deadline(t); frame(module, t); }
        return t;
    };
    const auto drain = [&](double t) { executor.waitIdle(); executor.drain(); return map.utilityCompleted(t); };
    const auto client = [&](d::Point box) { const auto r = map.core().clientRect({box.x, box.y, 0, 0}); check(bool(r), "A box point projects to the client"); return core::Point{r->x, r->y}; };

    // --- The launch prewarm: the page and the surface, nothing started ------------------------
    {
        const auto s = sample(core::Module::map);
        map.prewarm({center, chrome, s, 1, 0, true});
        check(!map.state().active() && !map.rasterStats().active && executor.stats().accepted == 0 && requests == 0 && map.sceneStats().pinBuilds >= 1,
            "The launch prewarm builds the Map without activating it, painting or loading geography");
    }

    // --- Shown: the painter, the geography request, the surface under the canvas -----------------
    frame(core::Module::map, .01);
    check(map.canvas().isActive() && map.state().active() && map.rasterStats().active && requests >= 1, "Shown, the Map activates, starts the painter and asks for geography");
    check(entryCount >= 2, "The surface is composed under the canvas's design scene");
    map.setGeography(geography(), .02);
    (void)drain(.02);
    double t = settle(core::Module::map, .03, .5);
    (void)drain(t); t = settle(core::Module::map, t, .3);
    check(map.rasterStats().published > 0, "The synthetic geography paints through the shared executor");
    check(map.sceneStats().groupPixels > 0 && map.sceneStats().groupPixels <= gpu::Renderer::maximumNativeGroupPixels, "The disc's group stays within the renderer's limit");
    {
        // Memory trace of the settled Map (compare v1.2.0: 10.7 MiB of textures, 18.2 MB CPU, README checkpoint 134).
        const auto gpuStats = renderer.stats(); const auto residency = map.residency(); const auto scene = map.sceneStats();
        std::printf("MAP_MEMORY density %.1f: group %zu px (%.2f MiB), renderer %zu textures %.2f MiB, native groups %.2f MiB, owner CPU %.2f MiB, raster cache %.2f MiB\n",
            overlay.context().boxScale(), scene.groupPixels, scene.groupPixels * 4 / 1048576., gpuStats.textures, gpuStats.resourceBytes / 1048576.,
            gpuStats.nativeGroupBytes / 1048576., residency.cpuBytes / 1048576., raster.stats().resourceBytes / 1048576.);
    }

    // --- Input: box px, the input circle's square, the zoom column ------------------------------
    const auto centre = client(m::MapCanvas::mapCenter);
    check(map.covers(centre) && map.cursorAt(centre) == app::ModuleCursor::openHand, "The disc centre is terrain: covered, open hand");
    const auto westOfBox = client({-40, 330});
    check(map.covers(westOfBox), "Presses reach the Map left of the content box, inside the input circle's square");
    check(!map.covers(client({-120, -120})), "Outside that square the shell keeps them");
    const auto zoomIn = client({m::MapCanvas::v1(37, 223).x, m::MapCanvas::v1(37, 223).y - 34});
    check(!map.cursorAt(zoomIn).has_value() || *map.cursorAt(zoomIn) == app::ModuleCursor::source, "The zoom column keeps the HUD pointer");
    check(map.pointer({app::PointerKind::down, app::PointerButton::right, centre.x, centre.y}, t) && map.state().pins().size() == 1 && writes >= 1,
        "A right press on terrain places a selected yellow pin, saved");
    (void)map.pointer({app::PointerKind::up, app::PointerButton::right, centre.x, centre.y}, t);
    t = settle(core::Module::map, t, .2);
    check(map.pointer({app::PointerKind::down, app::PointerButton::left, centre.x, centre.y}, t) && map.capturesPointer(), "A press on the pin is a map press");
    check(map.pointer({app::PointerKind::up, app::PointerButton::left, centre.x, centre.y}, t) && styles.size() == 1 && styles[0] == m::MapPinStyle::green,
        "Released on the selected pin it cycles to green (release-time action)");
    // One detent: one Mac notch (x1.12) at the pointer; committed .18 s later.
    const auto before = map.state().camera();
    const auto writesBefore = writes;
    check(map.wheel({centre.x, centre.y, 1, false, 0, 3}, t), "The wheel zooms inside the circle");
    check(std::abs(map.state().camera().zoom - before.zoom * 1.12) < 1e-9 && writes == writesBefore && map.pointerLocked(), "One detent is one notch: x1.12, held");
    const auto wake = map.nextWakeTime(t);
    check(wake && std::abs(*wake - (t + .18)) < 1e-6, "The gesture hold is the owner's one wake");
    t += .19; (void)map.deadline(t);
    check(writes == writesBefore + 1 && saved.viewport == m::mapNormalized(map.state().camera()) && !map.pointerLocked(), "After .18 s idle the camera is saved once");
    // The pinch: Ctrl+wheel magnifies (factor 1 + amount, clamped .5...2).
    const auto pinched = map.state().camera().zoom;
    check(map.wheel({centre.x, centre.y, 2, false, app::wheelControlKey, 3}, t), "A touchpad pinch reaches the Map");
    check(std::abs(map.state().camera().zoom - pinched * std::exp(.24)) < 1e-9, "Pinch: 1 + magnification");
    t += .2; (void)map.deadline(t);
    // Keys: arrows pan 40 px; Shift allowed; Ctrl passes.
    const auto panned = map.state().camera();
    check(map.key({app::KeyKind::down, VK_LEFT}, 4u, t) && map.state().camera() != panned, "Shift+Left pans (Shift is allowed now)");
    const auto afterShift = map.state().camera();
    check(!map.key({app::KeyKind::down, VK_LEFT}, 2u, t) && map.state().camera() == afterShift, "Ctrl+Left passes to the host");
    check(map.key({app::KeyKind::down, VK_ESCAPE}, 0u, t) && !map.state().selection(), "Esc deselects the pin");
    check(!map.key({app::KeyKind::down, VK_ESCAPE}, 0u, t), "Without a selection Esc falls through (back / HUD)");
    t += .2; (void)map.deadline(t);
    // UI Automation: the library's tree.
    check(map.automationRefresh(nullptr) && map.automationCount() >= 5, "The zoom column, reset, placement and the pin are listed");

    // --- The pulse fallback: without frames the pulse still moves ------------------------------
    appearance.ambient = true;
    map.setAppearance(appearance, t);
    // The camera edits above repaint the terrain: let the painter settle first (its own wakes).
    for (unsigned n = 0; n < 40; ++n) { (void)drain(t); t = settle(core::Module::map, t, .1); const auto r = map.rasterStats(); if (!r.painting && !r.waiting) break; }
    check(!map.rasterStats().painting && !map.rasterStats().waiting, "The painter settles");
    check(map.pulsing() && !map.requiresFrames(t), "Pins pulse as ambient motion (no display-rate demand)");
    const auto fallback = map.nextWakeTime(t);
    check(fallback && *fallback > t && *fallback <= t + .05, "The pulse fallback wakes a tick and a quarter after the last frame");
    check(map.deadline(*fallback) && !map.nextWakeTime(*fallback + .001), "Its deadline asks for one frame, then waits for it");
    appearance.lowPower = true;
    map.setAppearance(appearance, *fallback + .01);
    t = settle(core::Module::map, *fallback + .01, .1);
    check(!map.pulsing(), "Low-power visual mode stops the pin pulse (the Mac's ambient flag)");

    // --- Another module: the working set goes, nothing is composed ----------------------------
    t += .1;
    frame(core::Module::notes, t);
    check(!map.canvas().isActive() && !map.state().active() && !map.rasterStats().active && entryCount == 0 && !map.hostCut(),
        "Switched away, the Map deactivates, releases the painter's working set and composes nothing");
    t = settle(core::Module::map, t + .1, .3);
    check(map.canvas().isActive() && map.rasterStats().active, "Back on the Map the painter runs again");

    // --- Close and release --------------------------------------------------------------------
    for (auto* owner : owners) owner->setOverlayVisible(false, t);
    frame(core::Module::notes, t + .01);
    check(!map.canvas().isActive() && !map.state().active(), "Closing deactivates the Map");
    composition.detach(renderer); detach.done = true;
    for (auto* owner : owners) owner->release(renderer);
    executor.shutdown();
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "The fixture never shows a window");
    (void)changed; (void)recentered;
}
// Reopen cost with the bundled geography and the real painter (WARP): a
// close that keeps the terrain (as v1.2.0 did) against the Mac 1.3.0 close
// that releases it (overlayConcealed: geography, images and textures go; the
// next show decodes and paints on the shared worker).
void reopen(const std::filesystem::path& shader, const std::filesystem::path& mapDir) {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.hwnd, 1000, 640, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    app::UtilityExecutor executor([] {});
    const auto route = executor.makeRoute();
    tools::DesignOverlayOptions overlayOptions; overlayOptions.raster.pixelsPerPoint = 1; overlayOptions.window = window.hwnd;
    tools::DesignOverlayOwner overlay(raster, overlayOptions);
    auto painter = std::make_shared<gpu::NativeMapPainter>();
    double clock{}; bool loading{};
    tools::MapOwner* map{};
    tools::MapOwnerOptions o;
    o.paint = [painter](const auto& q, const auto& c) { return painter->paint(q, c); };
    o.releaseWorkerCaches = [painter] { painter->clear(); };
    o.clock = [&] { return clock; }; o.changed = [] {};
    o.requestGeography = [&] {
        if (loading || !map || !map->needsGeography()) return;
        auto result = std::make_shared<m::MapGeography>();
        loading = executor.submit(route, [mapDir, result] { *result = gpu::loadMapGeography(mapDir); },
            [&, result](std::exception_ptr) { loading = false; map->setGeography(result, clock); });
    };
    o.player = {image(2), image(2), image(2)};
    o.owner.name = "map"; o.owner.raster.pixelsPerPoint = 1;
    tools::MapOwner owner(raster, executor, overlay.context(), std::move(o));
    map = &owner;
    std::vector<app::ModuleOwner*> owners{&overlay, &owner};
    struct Detach { gpu::LayerComposition& composition; gpu::Renderer& renderer; bool done{}; ~Detach() { if (!done) composition.detach(renderer); } } detach{composition, renderer};
    const app::ClientMetrics metrics{1000, 640, 96, 1, 1000, 640};
    app::ModuleAppearance appearance; appearance.ambient = false;
    for (auto* x : owners) { x->resize(metrics); x->setAppearance(appearance, 0); x->setOverlayVisible(true, 0); }
    core::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, 1000, 640};
    const auto center = core::Matrix4{};
    const auto frame = [&](core::Module module, double t) {
        clock = t; chrome.module = module;
        const auto s = sample(module);
        const app::ModuleFrame f{center, chrome, s, 1, t, true};
        for (auto* x : owners) { x->update(f); x->upload(renderer); }
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : owner.entries(module)) entries.push_back(e);
        for (const auto& e : overlay.modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries); composition.present(renderer); renderer.draw(false);
        for (auto* x : owners) x->collected(renderer);
    };
    // Frames at 60 Hz with the worker drained until a terrain image is published.
    const auto terrain = [&](double& t, std::uint64_t before) {
        for (unsigned n = 0; n < 600 && owner.rasterStats().published <= before; ++n) {
            executor.waitIdle(); executor.drain(); (void)owner.utilityCompleted(t);
            if (const auto due = owner.nextWakeTime(t); due && *due <= t) (void)owner.deadline(t);
            t += 1. / 60; frame(core::Module::map, t);
        }
        check(owner.rasterStats().published > before, "The bundled terrain paints");
    };
    const auto close = [&](double& t, bool conceal) {
        for (auto* x : owners) x->setOverlayVisible(false, t);
        t += .1; frame(core::Module::notes, t);
        if (conceal) { for (auto* x : owners) (void)x->hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, t); t += .01; frame(core::Module::notes, t); }
    };
    struct Reopen { double firstFrame{}, toTerrain{}; };
    const auto open = [&](double& t, bool expectTerrain) {
        for (auto* x : owners) x->setOverlayVisible(true, t);
        const auto before = owner.rasterStats().published;
        const auto begin = Clock::now();
        t += .1; frame(core::Module::map, t);
        Reopen r{ms(Clock::now() - begin), 0};
        if (!expectTerrain) { terrain(t, before); r.toTerrain = ms(Clock::now() - begin); }
        return r;
    };
    double t = 1;
    const auto cold = open(t, false);
    // Keep (v1.2.0): the image is on screen at the first frame.
    close(t, false);
    const auto keptBytes = owner.residency().cpuBytes;
    const auto kept = open(t, true);
    check(owner.rasterStats().published > 0 && keptBytes > 0, "A close without the release keeps the terrain image");
    // Release (Mac 1.3.0).
    close(t, true);
    const auto releasedBytes = owner.residency().cpuBytes;
    check(owner.needsGeography() && releasedBytes < keptBytes, "The closed HUD keeps neither geography nor terrain images");
    const auto released = open(t, false);
    std::printf("MAP_REOPEN WARP: cold open first frame %.1f ms, terrain after %.0f ms; reopen keeping the terrain: first frame %.1f ms (terrain on it); "
                "reopen after the release: first frame %.1f ms, terrain after %.0f ms; owner CPU while closed %.2f -> %.2f MiB\n",
        cold.firstFrame, cold.toTerrain, kept.firstFrame, released.firstFrame, released.toTerrain, keptBytes / 1048576., releasedBytes / 1048576.);
    close(t, true);
    composition.detach(renderer); detach.done = true;
    for (auto* x : owners) x->release(renderer);
    executor.shutdown();
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(hr) && argc == 3, "Pass native/hud.hlsl and the bundled WorldMap directory");
        run(argv[1]);
        reopen(argv[1], std::filesystem::path(argv[2]));
        CoUninitialize();
        std::cout << "PASS " << checks << " Map owner checks\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(hr)) CoUninitialize();
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
