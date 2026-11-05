// The Notes centre behind tools::NotesCentreOwner (the owner app/application.cpp
// builds) on a hidden window and WARP: the canvas over the shared overlay
// owner's Context, Notes/centre.json in a temporary folder that opens on the
// TEXT tab, the desktop notes owner without text services, the TEXT note's
// persistent body editor. Checks the note's text is drawn: the editor's glyph
// scene is composed after the note area's part, and its ink is the pose's
// (#EEEEEE tone) from the first showing (the editor is made at activation,
// before the owner's first pose), and after a change back from Light while
// the note is folded, while Notes is away, while the HUD is closed (the
// session's new editor), and while it shows, each without an edit. The ink
// is read back: the note's text rect has pixels on the ink's side of its fill.
// No input injection, no visible window, no text service, no prompt.
#include "tools/notes_centre_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#ifdef _WIN32
#include "tools/archive_owner.hpp"
#include "tools/notes_preview.hpp"
#include "core/data/data_store.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace endfield;
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
namespace {
unsigned checks{};
void check(bool v, const std::string& s) { ++checks; if (!v) throw std::runtime_error(s); }
constexpr UINT fieldMessage = WM_APP + 321, bodyMessage = WM_APP + 322;
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldNotesCentreOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldNotesCentreOwnerHidden", GetModuleHandleW(nullptr)); }
};
struct Temporary {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("EndfieldHUD-Notes-Centre-Owner-" + ehud::data::makeUUID());
    Temporary() { std::filesystem::create_directories(root / "Centre" / "Notes"); }
    ~Temporary() { std::error_code e; std::filesystem::remove_all(root, e); }
};
double luminance(double r, double g, double b) { return .2126 * r + .7152 * g + .0722 * b; }
double luminance(d::Color c) { return luminance(c.r, c.g, c.b); }

struct Harness {
    Window window;
    Temporary temporary;
    gpu::Renderer renderer;
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    std::unique_ptr<tools::NotesPreview> desktop;
    std::unique_ptr<tools::DesignOverlayOwner> overlay;
    std::unique_ptr<tools::NotesCentreOwner> owner;
    core::ModulePresentation modules{core::Module::power};
    core::source::DesktopChromeSettings chrome;
    core::Matrix4 center;
    app::ModuleAppearance appearance;
    double time{};
    Harness(const std::filesystem::path& shader, const std::filesystem::path& notesFormat, const std::string& text) {
        renderer.initialize(window.hwnd, 1280, 800, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
        renderer.setCamera(gpu::layerViewportProjection(1280, 800));
        // centre.json opens on the TEXT tab (the owner's first showing attaches the note's editor).
        ehud::data::NotesCentreDocument doc; doc.tab = ehud::data::NotesCentreDocument::Tab::text; doc.text = text;
        { std::ofstream file(temporary.root / "Centre" / "Notes" / "centre.json", std::ios::binary); file << ehud::data::encodeNotesCentre(doc); check(bool(file), "Write the synthetic centre.json"); }
        desktop = std::make_unique<tools::NotesPreview>(window.hwnd, raster, temporary.root / "Desktop", false, notesFormat);
        tools::DesignOverlayOptions oo; oo.raster.pixelsPerPoint = 2; oo.window = window.hwnd;
        overlay = std::make_unique<tools::DesignOverlayOwner>(raster, oo);
        chrome.viewport = {0, 0, 1280, 800};
        center = core::source::DesktopChromeLayout::make(chrome, {}, {}).designToScreen;
        const app::ClientMetrics metrics{1280, 800, 96, 1, 1280, 800};
        overlay->resize(metrics);
        appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255}; appearance.reduceMotion = true;
        overlay->setAppearance(appearance, 0); overlay->setOverlayVisible(true, 0);
        modules.setDesignScreen(core::Module::notes, true);
        tools::NotesCentreOwnerOptions o;
        o.owner.name = "notesCentre"; o.owner.routing = tools::NotesCentreOwner::routing0(); o.owner.raster.pixelsPerPoint = 2;
        o.owner.raster.assetRoot = notesFormat;
        o.owner.images[std::string(m::notesColorWheelImageKey)] = m::notesColorWheelArtwork();
        o.owner.fields.window = window.hwnd; o.owner.fields.ownerMessage = fieldMessage; o.owner.fields.raster = o.owner.raster;
        o.owner.fields.clipboard.read = [] { return std::optional<std::u16string>{}; };
        o.owner.fields.clipboard.write = [](std::u16string_view) { return true; };
        o.owner.now = [this] { return time; };
        o.dataRoot = temporary.root / "Centre";
        o.fonts = tools::archiveFontServices(raster);
        o.bodyMessage = bodyMessage;
        owner = std::make_unique<tools::NotesCentreOwner>(raster, overlay->context(), *desktop, std::move(o));
        owner->resize(metrics);
        owner->setAppearance(appearance, 0); owner->setOverlayVisible(true, 0);
    }
    ~Harness() {
        composition.detach(renderer);
        owner->release(renderer); owner.reset();
        overlay->release(renderer);
        desktop.reset();
    }
    void pump() {
        MSG msg{};
        while (PeekMessageW(&msg, window.hwnd, 0, 0, PM_REMOVE))
            if (msg.message == bodyMessage || msg.message == fieldMessage) (void)owner->message({window.hwnd, msg.message, msg.wParam, msg.lParam}, time);
    }
    void frame() {
        time += 1. / 60;
        pump();
        (void)overlay->deadline(time); (void)owner->deadline(time);
        const auto s = modules.sample(time).presentation;
        chrome.module = s.requested;
        const app::ModuleFrame f{center, chrome, s, 1, time, true};
        overlay->update(f); owner->update(f);
        overlay->upload(renderer); owner->upload(renderer);
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : owner->entries(s.current.module)) entries.push_back(e);
        if (s.incoming) for (const auto& e : owner->entries(s.incoming->module)) entries.push_back(e);
        for (const auto& e : overlay->modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        owner->collected(renderer); overlay->collected(renderer);
    }
    void settle(unsigned frames = 30) { for (unsigned n = 0; n < frames; ++n) frame(); }
    void setLight(bool light) {
        appearance.dark = !light;
        overlay->setAppearance(appearance, time); owner->setAppearance(appearance, time);
    }
    void closeHUD() {
        owner->overlayClosing(time); overlay->overlayClosing(time); settle(30);
        owner->setOverlayVisible(false, time); overlay->setOverlayVisible(false, time);
        (void)owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, time); settle(10);
    }
    void openHUD() { owner->setOverlayVisible(true, time); overlay->setOverlayVisible(true, time); settle(40); }
    // The note's glyph scene is among the owner's entries, after a part the
    // scene found its anchor on.
    bool composed() {
        const auto glyphs = owner->body().entry();
        const auto stats = owner->core().sceneStats(core::Module::notes);
        if (!glyphs || !stats || stats->unanchoredInsertions) return false;
        const auto entries = owner->entries(core::Module::notes);
        return std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.scene == glyphs->scene; });
    }
    // The note's text rect read back: the pixels nearer the pose's ink than
    // the rect's own fill (its median), which must stand apart from the ink.
    std::size_t inked() {
        auto& canvas = owner->canvas();
        const auto* area = canvas.noteArea();
        check(area && owner->body().shown(), "The TEXT note's editor shows");
        const auto& palette = overlay->context().env().palette;
        const double ink = luminance(palette.tone(0xEEEEEE));
        const auto client = owner->core().clientRect(d::convert(area->textRect(), area->layer(), canvas.root()));
        check(client.has_value(), "The note's text rect maps to the client");
        renderer.draw(false);
        const auto shot = renderer.readback();
        const auto x0 = std::max(0, int(std::ceil(client->x)) + 2), y0 = std::max(0, int(std::ceil(client->y)) + 2);
        const auto x1 = std::min(int(shot.width), int(std::floor(client->x + client->width)) - 2), y1 = std::min(int(shot.height), int(std::floor(client->y + client->height)) - 2);
        check(x1 - x0 > 40 && y1 - y0 > 20, "The note's text rect is on screen");
        std::vector<double> lum;
        lum.reserve(std::size_t(x1 - x0) * std::size_t(y1 - y0));
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const auto* p = &shot.pixels[std::size_t(y) * shot.rowBytes + std::size_t(x) * 4];
            lum.push_back(luminance(p[2] / 255., p[1] / 255., p[0] / 255.));
        }
        auto sorted = lum;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double fill = sorted[sorted.size() / 2];
        const auto n = std::size_t(std::count_if(lum.begin(), lum.end(), [&](double v) { return std::abs(v - ink) < std::abs(v - fill); }));
        std::printf("  ink %.2f on %.2f (light %d) in %d,%d %dx%d: %zu of %zu pixels\n", ink, fill, int(palette.isLight()), x0, y0, x1 - x0, y1 - y0, n, lum.size());
        check(std::abs(ink - fill) > .4, "The note's fill stands apart from its ink");
        return n;
    }
};
constexpr std::size_t drawn = 100; // inked pixels of the note's three words, at least

void run(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    Harness h(shader, notesFormat, "MMMM WWWW MMMM");
    auto& canvas = h.owner->canvas();
    // The first showing opens Notes directly on TEXT: the editor is made at
    // the canvas's activation, before the owner's first pose of it.
    h.modules.select(core::Module::notes, h.time);
    h.settle(40);
    check(canvas.document().tab == ehud::data::NotesCentreDocument::Tab::text && canvas.noteArea() && h.owner->body().attached(*canvas.noteArea()), "Notes opens on its TEXT note with the editor attached");
    check(h.composed(), "The note's glyph scene is composed after the note area's part");
    check(h.inked() >= drawn, "The first showing draws the note in the pose's ink");
    // Light: the note keeps its #1C1C1C box (NotesCanvas.swift:511) under the
    // #1A1A1A tone, so only the dark ink reads back. An edit there paints the
    // Light ink; each change back to dark must then be drawn without one.
    const auto paintLight = [&] {
        h.setLight(true); h.settle(20);
        check(canvas.noteArea() && canvas.noteArea()->replace({0, 0}, u"M"), "An edit in Light paints the note");
        h.settle(5);
    };
    // Folded, the note is posed hidden: a change then is applied when it unfolds.
    paintLight();
    canvas.setFolded(true); h.settle(20);
    h.setLight(false); h.settle(20);
    canvas.setFolded(false); h.settle(30);
    check(h.composed() && h.inked() >= drawn, "A palette change while the note was folded inks it when it unfolds");
    // Away inside the open HUD: the stand-by editor takes the change when Notes returns.
    paintLight();
    h.modules.select(core::Module::power, h.time); h.settle(40);
    h.setLight(false); h.settle(10);
    h.modules.select(core::Module::notes, h.time); h.settle(40);
    check(h.composed() && h.inked() >= drawn, "A palette change while Notes was away inks the note when it returns");
    // The HUD closes: the session's new editor is made from the last pose; the change then is applied at the first showing.
    paintLight();
    h.closeHUD();
    h.setLight(false);
    h.openHUD();
    check(h.composed() && h.inked() >= drawn, "A palette change while the HUD was closed inks the next session's note");
    // Shown: the next pose repaints it, without an edit.
    paintLight();
    h.setLight(false); h.settle(20);
    check(h.composed() && h.inked() >= drawn, "A palette change while the note shows repaints its ink");
}
}
int wmain(int argc, wchar_t** argv) {
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(hr) && argc == 3, "Pass the shader and resources/notes-format");
        run(argv[1], argv[2]);
        CoUninitialize();
        std::cout << "PASS " << checks << " Notes centre owner checks\n";
        return 0;
    } catch (const std::exception& e) { if (SUCCEEDED(hr)) CoUninitialize(); std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
#else
int main() { return 0; }
#endif
