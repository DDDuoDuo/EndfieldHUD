// Contract suite of native/hud_design_scene (LIBRARY-DESIGN.md section 14,
// Native): part reuse by byte-equal JSON, one-surface updates, a structural
// load with the old part retired after publication, the shared raster budget
// refused before anything changes, lazy surfaces painted on show and released
// after their fade, the scroll visibility window, release (textures parked)
// and park (artwork dropped, repainted by the next sync), the tint bridge, an
// allocation-free pose and group opacity while a part fades (one composite,
// no target at rest). WARP on a hidden window; synthetic layers only.
#include "native/hud_design_scene.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
using namespace endfield;
using namespace native;
namespace d = modules::design;
namespace {
unsigned checks{};
std::atomic<std::size_t> allocations{};
bool countAllocations{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
// LIBRARY-DESIGN.md R4: whether LayerPlacement carries the linear tint yet.
template <class P> constexpr bool placementTint = requires(P p) { p.linearTint; };
template <class F> void rejects(F f, const char* s) { bool failed{}; try { f(); } catch (const std::exception&) { failed = true; } check(failed, s); }
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldDesignSceneHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 590, 510, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldDesignSceneHidden", GetModuleHandleW(nullptr)); }
};
// BGRA8 readback -> RGBA within 2.
std::array<int, 4> rgba(const Readback& frame, unsigned x, unsigned y) {
    const auto at = std::size_t(y) * frame.rowBytes + x * 4;
    return {frame.pixels[at + 2], frame.pixels[at + 1], frame.pixels[at], frame.pixels[at + 3]};
}
bool similar(int a, int b) { return std::abs(a - b) <= 2; }
// A canvas-shaped tree: three rows (plate + an animatable notch with a tint
// + a lazy sheen) and a clipped scroll area of twenty rows.
struct Tree {
    d::LayerArena arena{"t"};
    d::Layer &root, &content;
    std::vector<d::Layer*> rows, sheens, notches;
    Tree() : root(arena.make("root")), content(arena.make("content")) {
        root.setFrame({0, 0, 590, 510});
        for (int n = 0; n < 3; ++n) {
            auto& row = arena.make("row" + std::to_string(n));
            row.setFrame({0, 81 + 56. * n, 590, 50}); row.setPartRoot(true);
            row.setContent(d::solid("plate", {0, 0, 590, 50}, d::rgb(0x2A2A2A)));
            auto& notch = arena.make("notch" + std::to_string(n));
            notch.setFrame({0, 10, 4, 30}); notch.setContent(d::solid("notch", {0, 0, 4, 30}, d::white(1)));
            notch.setAnimatable(); notch.setTint(d::rgb(0x2A2A2A));
            auto& sheen = arena.make("sheen" + std::to_string(n));
            sheen.setFrame({0, 0, 120, 50}); sheen.setContent(d::solid("sheen", {0, 0, 120, 50}, d::white(.3)));
            sheen.setAnimatable(d::SurfaceRole::lazy); sheen.setValue(d::Channel::opacity, 0);
            row.add(notch); row.add(sheen); root.add(row);
            rows.push_back(&row); notches.push_back(&notch); sheens.push_back(&sheen);
        }
        auto& clip = arena.make("clip");
        clip.setFrame({0, 260, 590, 100}); clip.setMasksToBounds(true);
        content.setFrame({0, 0, 590, 1000}); content.setAnimatable();
        for (int n = 0; n < 20; ++n) {
            auto& row = arena.make("scroll" + std::to_string(n));
            row.setFrame({0, 50. * n, 590, 44}); row.setPartRoot(true);
            row.setContent(d::solid("cell", {0, 0, 590, 44}, d::rgb(0x161616)));
            content.add(row);
        }
        clip.add(content); root.add(clip);
    }
};
void run(const std::filesystem::path& shader) {
    Window window;
    Renderer renderer;
    renderer.initialize(window.hwnd, 590, 510, {Driver::warpForTests, shader, RenderTarget::offscreenForTests});
    renderer.setCamera(layerViewportProjection(590, 510));
    LayerRasterizer raster;
    LayerRasterOptions options; options.pixelsPerPoint = 1;
    NativeDesignScene scene(raster, options, "design.test");
    LayerComposition composition;
    d::TextServices text;
    Tree tree;
    std::uint64_t revision = 1;
    double time{};
    bool detached{};
    struct Cleanup { Renderer& r; LayerComposition& c; NativeDesignScene& s; bool& done; ~Cleanup() { if (!done) try { c.detach(r); s.releaseResources(r); } catch (...) { r.reset(); } } } cleanup{renderer, composition, scene, detached};
    const auto publish = [&](double t) {
        time = t;
        scene.syncContent(tree.root, text, revision, 1, t);
        scene.updatePose({core::Matrix4{}, 1, t, {}, {}});
        scene.uploadAnimations(renderer);
        composition.setEntries(renderer, scene.entries());
        composition.present(renderer);
        renderer.draw(false);
        check(scene.collectRetired(renderer), "Old parts retire after publication");
    };
    // --- first generation ----------------------------------------------------
    publish(0);
    const auto first = scene.stats();
    check(first.generations == 1 && first.parts == 23 && first.partsLoaded == scene.entries().size(), "Every resident part loads once");
    check(first.partsLoaded == 7, "The three rows and the four scroll rows inside the window plus one viewport of slack");
    check(first.lazyPaints == 0, "Lazy surfaces keep no artwork while they do not show");
    // The scroll window: the clip (100 tall) plus one viewport each side.
    std::size_t resident{};
    for (const auto& e : scene.entries()) resident += e.scene != nullptr;
    check(resident < first.parts, "Scroll rows two viewports away are not rasterized");
    const auto image = renderer.readback();
    check(std::any_of(image.pixels.begin(), image.pixels.end(), [](auto b) { return b != 0; }), "The parts paint the target");
    // --- reuse / update / load --------------------------------------------------
    const auto rasters = raster.stats().rasterizations;
    check(!scene.syncContent(tree.root, text, revision, 1, .01), "An unchanged revision does nothing");
    ++revision;
    publish(.02);
    check(raster.stats().rasterizations == rasters && scene.stats().partsReused > 0, "Byte-equal parts are reused without a raster");
    tree.rows[1]->setContent(d::solid("plate", {0, 0, 590, 50}, d::rgb(0x3A3A3A)));
    ++revision;
    publish(.03);
    check(scene.stats().surfacesUpdated == 1 && raster.stats().rasterizations == rasters + 1, "One changed surface repaints alone");
    auto& extra = tree.arena.make("extra");
    extra.setFrame({500, 10, 20, 20}); extra.setContent(d::solid("dot", {0, 0, 20, 20}, d::rgb(0xFF0000))); extra.setAnimatable();
    tree.rows[0]->add(extra);
    ++revision;
    const auto loads = scene.stats().partsLoaded;
    publish(.04);
    check(scene.stats().partsLoaded == loads + 1 && scene.stats().retiredParts == 0, "A structural change loads the part; the old one is released after publication");
    // --- lazy surfaces --------------------------------------------------------------
    tree.sheens[2]->setValue(d::Channel::opacity, 1);
    publish(.05);
    check(scene.stats().lazyPaints == 1, "A lazy surface paints when it first shows");
    tree.sheens[2]->setValue(d::Channel::opacity, 0);
    publish(.06);
    check(scene.stats().lazyReleases == 1, "It gives its artwork back once faded");
    // --- the tint bridge (R4) ---------------------------------------------------------
    bool tinted{};
    for (const auto& draw : composition.draws()) tinted |= draw.linearTint[0] < .1f;
    if constexpr (placementTint<LayerPlacement>) check(tinted, "Tinted surfaces multiply in linear light");
    else check(!tinted, "Without the placement tint the white artwork shows (recorded fallback)");
    // --- the scroll window follows the content layer without a content event ---------------
    const auto beforeScroll = scene.stats().partsLoaded;
    tree.content.setValue(d::Channel::translateY, -600);
    check(scene.syncContent(tree.root, text, revision, 1, .07), "A scroll step changes the resident rows");
    publish(.07);
    check(scene.stats().partsLoaded > beforeScroll, "Rows entering the window are rasterized");
    // --- allocation-free pose ------------------------------------------------------------------
    allocations = 0; countAllocations = true;
    try {
        for (unsigned n = 0; n < 100; ++n) scene.updatePose({core::Matrix4::rotation(0, .0001 * n, 0), 1, .08 + n * .001, {}, {}});
    } catch (...) { countAllocations = false; throw; }
    countAllocations = false;
    check(allocations == 0, "Warm poses allocate nothing");
    time = .2;
    // --- the shared budget is refused before anything changes ---------------------------------
    {
        std::vector<std::string> fillers;
        const auto json = ehud::data::Json::Object{{"id", "f"}, {"bounds", ehud::data::Json::Array{0, 0, 1, 1}}, {"position", ehud::data::Json::Array{0, 0}},
            {"anchorPoint", ehud::data::Json::Array{0, 0}}, {"backgroundColor", ehud::data::Json::Object{{"sRGB", ehud::data::Json::Array{1, 1, 1, 1}}}}, {"children", ehud::data::Json::Array{}}};
        while (raster.stats().entries < LayerRasterizer::maximumEntries) { fillers.push_back("filler." + std::to_string(fillers.size())); (void)raster.rasterize(fillers.back(), 1, json, options); }
        const auto entries = scene.entries().size();
        const auto stats = scene.stats();
        tree.rows[2]->setContent(d::solid("plate", {0, 0, 590, 50}, d::rgb(0x111111)));
        auto& another = tree.arena.make("another");
        another.setFrame({500, 10, 20, 20}); another.setContent(d::solid("dot", {0, 0, 20, 20}, d::rgb(0x00FF00))); another.setAnimatable();
        tree.rows[2]->add(another); // a new surface: the part must load
        ++revision;
        rejects([&] { scene.syncContent(tree.root, text, revision, 1, .2); }, "A generation beyond the shared budget is refused");
        check(scene.entries().size() == entries && scene.stats().partsLoaded == stats.partsLoaded, "The refusal touched nothing");
        for (const auto& id : fillers) raster.remove(id);
    }
    publish(.21);
    // --- release (switched away) and park (closed elsewhere) -------------------------------------
    composition.setEntries(renderer, {});
    composition.present(renderer);
    check(scene.releaseResources(renderer), "An uncomposed scene releases its resources");
    publish(.22);
    composition.setEntries(renderer, {});
    composition.present(renderer);
    check(scene.park(renderer) && scene.stats().parks == 1 && scene.entries().empty(), "Park drops every part");
    check(!scene.park(renderer), "Nothing left to park");
    const auto parked = scene.stats().partsLoaded;
    publish(.23);
    check(scene.stats().partsLoaded > parked && !scene.entries().empty(), "The next sync rebuilds parked artwork");
    rejects([&] { scene.syncContent(tree.root, text, revision, 1, .1); }, "Time runs forward");
    rejects([&] { scene.syncContent(tree.root, text, revision, 5, .3); }, "Density stays within 1...4");
    composition.detach(renderer);
    check(scene.releaseResources(renderer), "Detached scene releases everything");
    detached = true;
    // --- group opacity: a label over its plate fades as one image -------------------------------
    {
        NativeDesignScene faded(raster, options, "design.group");
        LayerComposition published;
        d::LayerArena arena{"g"};
        auto& root = arena.make("root");
        root.setFrame({0, 0, 590, 510});
        auto& card = arena.make("card");
        card.setFrame({100, 100, 200, 100}); card.setPartRoot(true); card.setAnimatable();
        card.setContent(d::solid("plate", {0, 0, 200, 100}, d::rgb(0xFF0000)));
        auto& label = arena.make("label");
        label.setFrame({50, 25, 100, 50}); label.setContent(d::solid("ink", {0, 0, 100, 50}, d::white(1))); label.setAnimatable();
        card.add(label);
        auto& solo = arena.make("solo"); // one surface: a group would change nothing
        solo.setFrame({400, 100, 50, 50}); solo.setPartRoot(true); solo.setContent(d::solid("dot", {0, 0, 50, 50}, d::rgb(0x00FF00)));
        root.add(card); root.add(solo);
        const auto frame = [&](double at, float module = 1) {
            faded.syncContent(root, text, 1, 1, at);
            faded.updatePose({core::Matrix4{}, module, at, {}, {}});
            faded.uploadAnimations(renderer);
            published.setEntries(renderer, faded.entries());
            published.present(renderer);
            renderer.draw(false);
            check(faded.collectRetired(renderer), "Detached group targets go after the publication");
        };
        frame(1);
        check(faded.stats().groups == 0 && renderer.stats().nativeGroups == 0, "At rest every part draws directly and holds no target");
        card.setValue(d::Channel::opacity, .5);
        frame(1.1);
        check(faded.stats().groups == 1 && renderer.stats().nativeGroups == 1 && faded.stats().groupSwitches == 1, "The fading card draws through one pooled group");
        auto shot = renderer.readback();
        const auto ink = rgba(shot, 200, 150), plate = rgba(shot, 110, 110);
        check(plate[0] > 0 && plate[1] == 0 && similar(plate[3], ink[3]), "The plate fades to half");
        check(similar(ink[0], ink[1]) && similar(ink[1], ink[2]) && similar(ink[0], plate[0]),
            "Over the plate the label shows alone at half (group composite), not over a half plate (per-surface product)");
        // Fading frames move numbers only.
        allocations = 0; countAllocations = true;
        try {
            for (unsigned n = 0; n < 20; ++n) {
                card.setValue(d::Channel::opacity, .5 + .02 * n);
                faded.updatePose({core::Matrix4{}, 1, 1.2 + n * .01, {}, {}});
                faded.uploadAnimations(renderer);
                published.present(renderer);
            }
        } catch (...) { countAllocations = false; throw; }
        countAllocations = false;
        check(allocations == 0, "Fading group frames allocate nothing");
        card.setValue(d::Channel::opacity, 1);
        frame(1.5);
        check(faded.stats().groups == 0 && renderer.stats().nativeGroups == 0 && faded.stats().groupTargets == 0, "Back at rest the target is gone after the publication");
        shot = renderer.readback();
        check(rgba(shot, 200, 150) == std::array<int, 4>{255, 255, 255, 255} && rgba(shot, 110, 110)[0] == 255, "At rest the label and plate draw directly at full opacity");
        // The module's own fade groups every part of two or more surfaces; the
        // pooled group is reused (no new allocation of its target at rest).
        frame(1.6, .5f);
        check(faded.stats().groups == 1 && renderer.stats().nativeGroups == 1 && faded.stats().groupSwitches == 3, "A module fade groups the card, not the one-surface dot");
        shot = renderer.readback();
        check(similar(rgba(shot, 425, 125)[1], rgba(shot, 110, 110)[0]), "The dot fades with the module directly");
        frame(1.7);
        published.detach(renderer);
        check(faded.releaseResources(renderer) && renderer.stats().nativeGroups == 0, "The faded scene releases everything");
    }
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "The fixture never shows a window");
}
}
void* operator new(std::size_t n) { if (countAllocations) ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the shader");
        run(argv[1]);
        CoUninitialize();
        std::cout << "HUD design scene: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        countAllocations = false;
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "HUD design scene after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
