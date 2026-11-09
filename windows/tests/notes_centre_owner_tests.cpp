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
// The first showing lays the note out once (no restyle), a long (chunked)
// note too. While the note unfolds (.32 s), its text is clipped by the fold's
// wipe like the note's parts (Mac: the text view under the clip's
// masksToBounds): the frame read back with and without the editor's glyphs
// differs above the wipe's edge only.
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
        publish();
    }
    // The publication of the owners' current entries and the collection
    // after it (the app's publishNative), with or without an update before.
    void publish() {
        const auto s = modules.sample(time).presentation;
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
    void setReduceMotion(bool reduce) {
        appearance.reduceMotion = reduce;
        overlay->setAppearance(appearance, time); owner->setAppearance(appearance, time);
    }
    // The host's close: overlayClosing, the close's frames, then the hidden
    // HUD (it never sends setOverlayVisible(false) on a close). The shown
    // note stays active and drawn through the close, taking no input.
    void closeHUD() {
        auto* canvas = owner->core().canvas(core::Module::notes);
        const bool wasActive = canvas && canvas->isActive();
        owner->overlayClosing(time); overlay->overlayClosing(time); settle(30);
        check(!canvas || canvas->isActive() == wasActive, "The close keeps the note canvas as it was until the hide");
        check(!owner->capturesPointer() && !owner->capturesKeys(), "The close takes no input");
        (void)owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, time); (void)overlay->hostEvent({app::HostEvent::Kind::overlayConcealed}, time); settle(10);
        check(!canvas || !canvas->isActive(), "The hidden HUD leaves the note canvas");
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

// The unfolding note, read back mid-animation with and without the editor's
// glyph entry (the same frame recomposed): their difference is the glyphs.
// Below the fold's wipe edge the note's parts are clipped away, and so must
// the glyphs be; above it they draw.
void unfold(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    std::string lines;
    for (unsigned n = 0; n < 40; ++n) lines += "MMMM WWWW MMMM\n";
    Harness h(shader, notesFormat, lines);
    auto& canvas = h.owner->canvas();
    h.setReduceMotion(false);
    h.modules.select(core::Module::notes, h.time);
    h.settle(60);
    check(h.composed() && h.owner->body().shown(), "The many-line note shows");
    canvas.setFolded(true); h.settle(40);
    canvas.setFolded(false);
    const auto* area = canvas.noteArea();
    check(area != nullptr, "The note shows its area");
    // The fold's clip: an ancestor of the area with a wipe (top origin).
    const d::Layer* clip{};
    for (const auto* l = area->layer().parent(); l && !clip; l = l->parent())
        if (l->animatable() && l->presented(d::Channel::wipe, h.time) < 1) clip = l;
    check(clip && clip->wipeOrigin == d::WipeOrigin::top, "The note unfolds under a running top wipe");
    // The wipe's edge and the text rows (presented: the content also rises
    // 8 px) in client pixels at the last frame.
    double wipe{};
    int edge{}, x0{}, x1{}, top{}, bottom{};
    const auto measure = [&] {
        wipe = clip->presented(d::Channel::wipe, h.time);
        const auto shown = h.owner->core().clientRect(d::convert(core::Rect{0, 0, clip->bounds().width, clip->bounds().height * wipe}, *clip, canvas.root()));
        const auto placed = d::sampleLayer(area->layer(), canvas.root(), h.time);
        const auto rect = area->textRect();
        const auto a = placed.world.apply({rect.x, rect.y}), b = placed.world.apply({rect.x + rect.width, rect.y + rect.height});
        const auto text = h.owner->core().clientRect({std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)});
        check(shown && text, "The wipe and the text rect map to the client");
        edge = int(std::lround(shown->y + shown->height));
        x0 = int(std::ceil(text->x)) + 2; x1 = int(std::floor(text->x + text->width)) - 2;
        top = int(std::ceil(text->y)) + 2; bottom = int(std::floor(text->y + text->height)) - 2;
    };
    // Frames of 1/240 s until the edge reaches the middle of the text rows.
    for (unsigned n = 0; n < 120; ++n) {
        h.time += 1. / 240 - 1. / 60;
        h.frame();
        measure();
        if (edge >= (top + bottom) / 2 || wipe >= 1) break;
    }
    check(h.owner->body().shown(), "The unfolding note's editor shows");
    std::printf("  wipe %.3f: edge %d in the text rows %d-%d\n", wipe, edge, top, bottom);
    check(wipe < 1 && edge - top > 20 && bottom - edge > 20, "Mid-unfold, the wipe's edge crosses the note's text rect");
    h.renderer.draw(false);
    const auto with = h.renderer.readback();
    const auto glyphs = h.owner->body().entry();
    check(glyphs.has_value(), "The editor's glyph entry is composed");
    std::vector<gpu::LayerCompositionEntry> entries;
    for (const auto& e : h.owner->entries(core::Module::notes)) if (e.scene != glyphs->scene) entries.push_back(e);
    for (const auto& e : h.overlay->modalEntries()) entries.push_back(e);
    h.composition.setEntries(h.renderer, entries);
    h.composition.present(h.renderer);
    h.renderer.draw(false);
    const auto without = h.renderer.readback();
    check(with.width == without.width && with.height == without.height && with.rowBytes == without.rowBytes, "Two reads of one frame");
    const auto differs = [&](int y0, int y1) {
        std::size_t n{};
        for (int y = std::max(0, y0); y < std::min(int(with.height), y1); ++y)
            for (int x = std::max(0, x0); x < std::min(int(with.width), x1); ++x) {
                const auto at = std::size_t(y) * with.rowBytes + std::size_t(x) * 4;
                int delta{};
                for (int c = 0; c < 3; ++c) delta = std::max(delta, std::abs(int(with.pixels[at + c]) - int(without.pixels[at + c])));
                n += delta > 24;
            }
        return n;
    };
    const auto above = differs(top, edge - 2), below = differs(edge + 2, bottom);
    std::printf("  glyph pixels above the edge %zu, below %zu\n", above, below);
    check(above >= drawn, "Above the wipe's edge the note's text draws");
    check(below == 0, "Below the wipe's edge the note's text is clipped like its parts");
    h.frame(); // the full list again
}
// A long note (chunked, past 4096 units) is laid out once at its first
// showing: the editor is made with the palette's ink (setInk), not restyled.
void longFirstShowing(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    std::string text;
    while (text.size() < 6000) text += "MMMM WWWW MMMM\n";
    Harness h(shader, notesFormat, text);
    h.modules.select(core::Module::notes, h.time);
    h.settle(40);
    check(h.composed(), "The long note's glyph scene is composed");
    check(h.owner->body().restyles() == 0, "The long note's first showing lays it out once (no restyle)");
    check(h.inked() >= drawn, "The long note's first showing draws it in the pose's ink");
}

void run(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    Harness h(shader, notesFormat, "MMMM WWWW MMMM");
    auto& canvas = h.owner->canvas();
    // The first showing opens Notes directly on TEXT: the editor is made at
    // the canvas's activation, before the owner's first pose of it.
    h.modules.select(core::Module::notes, h.time);
    h.settle(40);
    check(canvas.document().tab == ehud::data::NotesCentreDocument::Tab::text && canvas.noteArea() && h.owner->body().attached(*canvas.noteArea()), "Notes opens on its TEXT note with the editor attached");
    check(h.composed(), "The note's glyph scene is composed after the note area's part");
    check(h.owner->body().restyles() == 0, "The first showing lays the note out once (no restyle)");
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
    const auto restyled = h.owner->body().restyles();
    h.setLight(false); h.settle(20);
    check(h.composed() && h.inked() >= drawn, "A palette change while the note shows repaints its ink");
    check(h.owner->body().restyles() == restyled + 1, "A palette change while the note shows restyles it once");
    // The concealment's session reset reloads the note (its editor ends)
    // after the owner's last pose; a publication with no update between (a
    // hidden frame of the quit's drain) still composes the ended editor's
    // glyphs: they are collected only after a publication without them.
    h.owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time); h.settle(30);
    const auto ended = h.owner->body().entry();
    check(ended.has_value() && h.composed(), "Through the close the note's editor is still composed");
    (void)h.owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); (void)h.overlay->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time);
    check(!h.owner->body().entry(), "The concealment's session reset ends the note's editor");
    h.composition.detach(h.renderer);
    h.publish();
    h.settle(10);
    h.openHUD();
    check(h.composed() && h.inked() >= drawn, "The next session's note shows after an ended editor was collected late");
}
}
// 8e3de69 has nothing to port (the caret is gated on every pose: the window
// foreground, the owner's focus and the field's), locked here: Notes shown as
// the HUD opens with its note focused; the HUD losing key focus hides the
// caret (no blink wake, no frame), focus back shows it again.
void caretFollowsFocus(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    Harness h(shader, notesFormat, "Focus");
    h.setReduceMotion(false);
    h.modules.select(core::Module::notes, h.time);
    h.settle(60);
    h.closeHUD();
    h.openHUD();
    auto* area = h.owner->canvas().noteArea();
    check(area != nullptr && h.owner->canvas().isActive(), "Notes is active on the HUD's opening");
    area->focus(std::nullopt);
    h.settle(2);
    check(area->isFocused() && h.owner->body().nextWakeTime(h.time).has_value(), "The focused note's caret blinks (its wake)");
    h.owner->focus(false, h.time);
    h.settle(2);
    check(!h.owner->body().nextWakeTime(h.time) && !h.owner->body().requiresFrames(h.time), "Key focus lost hides the caret: no blink wake or frame");
    h.owner->focus(true, h.time);
    h.settle(2);
    check(area->isFocused() && h.owner->body().nextWakeTime(h.time).has_value(), "Key focus back shows the caret: its blink wakes again");
}
int wmain(int argc, wchar_t** argv) {
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(hr) && argc == 3, "Pass the shader and resources/notes-format");
        run(argv[1], argv[2]);
        longFirstShowing(argv[1], argv[2]);
        unfold(argv[1], argv[2]);
        caretFollowsFocus(argv[1], argv[2]);
        CoUninitialize();
        std::cout << "PASS " << checks << " Notes centre owner checks\n";
        return 0;
    } catch (const std::exception& e) { if (SUCCEEDED(hr)) CoUninitialize(); std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
#else
int main() { return 0; }
#endif
