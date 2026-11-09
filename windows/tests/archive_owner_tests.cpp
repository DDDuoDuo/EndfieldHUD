// Archive behind tools::ArchiveOwner (the owner app/application.cpp builds)
// on a hidden window and WARP: the canvas over the shared overlay owner's
// Context, the real ArchiveService (SQLite in a temporary folder, the shared
// utility executor, an in-memory vault), the persistent body editor, the
// media binding over the real decoder and broker (a pinned PNG copied into
// the temporary folder), recovery's owner proof injected (no prompt).
// Checks: the list composes and idles without rasterizing; New opens the
// editor at once with the body editor drawn in the canvas scene after the
// body part; typed body text and the UIA title persist; an imported picture
// becomes the strip's preview record and the gallery tile's poster; the
// stand-by of a module switch inside the open HUD paints nothing again and
// shows identical pixels, the body keeping its focus and selection; a
// classified document opens locked and relocks when left; the end of the HUD
// session discards the editor and releases the media. Prints the frame costs.
// No input injection, no visible window, no text service, no prompt.
#include "tools/archive_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#ifdef _WIN32
#include "native/archive_text_rules.hpp"
#include "native/notes_image_playback.hpp"
#include "native/notes_video_playback.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace endfield;
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
double ms(auto&& f) { const auto s = std::chrono::steady_clock::now(); f(); return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count(); }
constexpr UINT mediaMessage = WM_APP + 310, fieldMessage = WM_APP + 311, bodyMessage = WM_APP + 312, authenticationMessage = WM_APP + 313;
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldArchiveOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldArchiveOwnerHidden", GetModuleHandleW(nullptr)); }
};
struct Temporary {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("EndfieldHUD-Archive-Owner-" + ehud::data::makeUUID());
    Temporary() { std::filesystem::create_directories(root); }
    ~Temporary() { std::error_code e; std::filesystem::remove_all(root, e); }
};
struct Clipboard final : gpu::NotesClipboard {
    std::optional<gpu::NotesClipboardText> value;
    std::optional<gpu::NotesClipboardText> readText(std::uint32_t) override { return value; }
    std::vector<std::string> readFiles() override { return {}; }
    bool writeText(const gpu::NotesClipboardText& v) override { value = v; return true; }
};
std::string utf8(const std::filesystem::path& p) { const auto b = p.u8string(); return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }
// An injected movie engine (no decode, no sound): the real worker still of
// the synthetic movie is the poster; play, pause and seek are recorded.
struct MovieProbe { gpu::NativeNotesVideoPlayback::Notify notify; double position{}; bool playing{}, fresh{}; unsigned plays{}, seeks{}; };
class MovieEngine final : public gpu::NativeNotesVideoPlayback::Engine {
    std::shared_ptr<MovieProbe> p_;
public:
    explicit MovieEngine(std::shared_ptr<MovieProbe> p) : p_(std::move(p)) {}
    ~MovieEngine() override { stop(); }
    HRESULT open(const gpu::NotesVideoRequest&) override { p_->notify(gpu::NativeNotesVideoPlayback::ready | gpu::NativeNotesVideoPlayback::firstFrame, S_OK); return S_OK; }
    HRESULT metadata(gpu::NotesVideoMetadata& out) override { out = {64, 64, 1.2}; return S_OK; }
    HRESULT play() override { p_->playing = true; p_->fresh = true; ++p_->plays; return S_OK; }
    HRESULT pause() override { p_->playing = false; return S_OK; }
    HRESULT seek(double t) override { p_->position = t; ++p_->seeks; p_->fresh = true; p_->notify(gpu::NativeNotesVideoPlayback::seeked, S_OK); return S_OK; }
    double currentTime() const override { return p_->position; }
    HRESULT tick(std::int64_t& pts) override { pts = static_cast<std::int64_t>(p_->position * 10000000); if (!p_->fresh) return S_FALSE; p_->fresh = false; return S_OK; }
    HRESULT transfer(void*, unsigned, unsigned) override { return S_OK; }
    void stop() noexcept override { p_->playing = false; }
};

struct Harness {
    Window window;
    Temporary temporary;
    gpu::Renderer renderer;
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    gpu::LayerImageSource images{gpu::LayerImageSourceLimits{30, 48, 12 * 1024 * 1024}};
    app::UtilityExecutor utility{[] {}};
    std::unique_ptr<app::ArchiveService> service;
    std::unique_ptr<gpu::NativeNotesImageDecoder> decoder;
    std::unique_ptr<gpu::NativeNotesImagePlayback> playback;
    std::vector<std::shared_ptr<MovieProbe>> probes;           // one per movie engine, in order
    std::unique_ptr<gpu::NativeNotesVideoPlayback> video;      // movies: injected engines only
    std::unique_ptr<gpu::NativeMediaRequestBroker> broker;
    gpu::NativeMediaRequestBroker::Client client{};
    std::unique_ptr<tools::DesignOverlayOwner> overlay;
    std::unique_ptr<tools::ArchiveOwner> owner;
    std::shared_ptr<Clipboard> clipboard = std::make_shared<Clipboard>();
    core::ModulePresentation modules{core::Module::power};
    core::source::DesktopChromeSettings chrome;
    core::Matrix4 center;
    std::vector<m::ArchiveMediaRequest> requests;
    bool approve{true}, asked{};
    double time{};
    // nativeAuthentication: the host's setup (the Windows Hello / credential
    // proof is built, never asked: no recovery runs here). movies: the broker
    // gets video playback over injected engines (MovieEngine).
    explicit Harness(const std::filesystem::path& shader, const std::filesystem::path& notesFormat, bool nativeAuthentication = false, bool movies = false) {
        renderer.initialize(window.hwnd, 1280, 800, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests, movies});
        renderer.setCamera(gpu::layerViewportProjection(1280, 800));
        service = std::make_unique<app::ArchiveService>(temporary.root / "Archive", utility, gpu::nativeArchiveTextRules(), [] {}, std::make_shared<m::ArchiveMemoryPasswordVault>());
        const gpu::NotesImageRoute route{window.hwnd, mediaMessage, 1};
        decoder = std::make_unique<gpu::NativeNotesImageDecoder>([](const gpu::NotesImageRequest& r) { return gpu::NotesImageAccess{std::filesystem::u8path(r.path), r.accessLease}; }, route);
        playback = std::make_unique<gpu::NativeNotesImagePlayback>(*decoder);
        broker = std::make_unique<gpu::NativeMediaRequestBroker>(*decoder, *playback, route);
        if (movies) {
            video = std::make_unique<gpu::NativeNotesVideoPlayback>(renderer, gpu::NotesVideoRoute{window.hwnd, mediaMessage, 1},
                [this](std::shared_ptr<void>, gpu::NativeNotesVideoPlayback::Notify notify) -> std::unique_ptr<gpu::NativeNotesVideoPlayback::Engine> {
                    auto p = std::make_shared<MovieProbe>(); p->notify = std::move(notify); probes.push_back(p); return std::make_unique<MovieEngine>(p); });
            broker->connectVideoPlayback(*video);
        }
        client = broker->attachClient();
        tools::DesignOverlayOptions oo; oo.raster.pixelsPerPoint = 2; oo.window = window.hwnd;
        overlay = std::make_unique<tools::DesignOverlayOwner>(raster, oo);
        chrome.viewport = {0, 0, 1280, 800};
        center = core::source::DesktopChromeLayout::make(chrome, {}, {}).designToScreen;
        const app::ClientMetrics metrics{1280, 800, 96, 1, 1280, 800};
        overlay->resize(metrics);
        app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255}; appearance.reduceMotion = true;
        overlay->setAppearance(appearance, 0); overlay->setOverlayVisible(true, 0);
        modules.setDesignScreen(core::Module::archive, true);
        tools::ArchiveOwnerOptions o;
        o.owner.name = "archive"; o.owner.routing = tools::ArchiveOwner::routing0(); o.owner.raster.pixelsPerPoint = 2;
        o.owner.raster.assetRoot = notesFormat;
        o.owner.images[std::string(m::notesColorWheelImageKey)] = m::notesColorWheelArtwork();
        o.owner.fields.window = window.hwnd; o.owner.fields.ownerMessage = fieldMessage; o.owner.fields.raster = o.owner.raster;
        o.owner.fields.clipboard.read = [] { return std::optional<std::u16string>{}; };
        o.owner.fields.clipboard.write = [](std::u16string_view) { return true; };
        o.owner.now = [this] { return time; };
        o.newID = ehud::data::makeUUID; o.foundationNow = [this] { return 811692800. + time; }; o.dotted = tools::archiveDottedDate;
        o.fonts = tools::archiveFontServices(raster);
        o.shelfChoices = [] { return std::vector<m::NotesShelfChoice>{}; };
        o.requestMedia = [this](const m::ArchiveMediaRequest& r) { requests.push_back(r); return true; };
        o.media.resolve = [](const ehud::data::Json& r) { return gpu::NotesImageAccess{std::filesystem::u8path(r["windowsPath"].string()), {}}; };
        o.media.errorText = [](HRESULT hr) { return "Media error " + std::to_string(hr); };
        if (nativeAuthentication) o.authenticationMessage = authenticationMessage;
        else o.ownerAuthentication = [this](std::string, std::function<void(bool)> done) { asked = true; done(approve); };
        o.bodyMessage = bodyMessage; o.clipboard = clipboard;
        owner = std::make_unique<tools::ArchiveOwner>(raster, overlay->context(), *service, *broker, client, renderer, images, std::move(o));
        owner->resize(metrics);
        owner->setAppearance(appearance, 0); owner->setOverlayVisible(true, 0);
    }
    ~Harness() {
        composition.detach(renderer);
        owner->release(renderer); owner.reset();
        overlay->release(renderer);
        utility.waitIdle(); utility.drain();
    }
    // The host's message loop for the broker's notices and the editors'.
    void pump() {
        MSG msg{};
        while (PeekMessageW(&msg, window.hwnd, 0, 0, PM_REMOVE)) {
            if (msg.message == mediaMessage) (void)broker->accept(msg.wParam, time);
            else if (msg.message == bodyMessage || msg.message == fieldMessage) (void)owner->message({window.hwnd, msg.message, msg.wParam, msg.lParam}, time);
        }
    }
    double frame(double advance = 1. / 60) {
        return ms([&] {
            time += advance;
            pump();
            utility.waitIdle();
            if (utility.drain()) owner->stateChanged(time);
            (void)broker->sample(time); (void)owner->refreshMedia(time);
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
        });
    }
    void settle(unsigned frames = 30) { for (unsigned n = 0; n < frames; ++n) frame(); }
    template <class F> void await(const char* what, F ready) {
        for (unsigned n = 0; n < 600; ++n) { frame(); if (ready()) return; Sleep(5); }
        throw std::runtime_error(std::string("Timed out: ") + what);
    }
    std::vector<std::uint8_t> capture() { renderer.draw(false); return renderer.readback().pixels; }
    // The body's glyph scene is among the owner's entries, after a part the
    // scene found its anchor on.
    bool insertedBody() const {
        auto& o = *owner;
        const auto stats = o.core().sceneStats(core::Module::archive);
        const auto glyphs = o.body().entry();
        if (!stats || stats->unanchoredInsertions || !glyphs || !o.body().shown()) return false;
        const auto entries = o.entries(core::Module::archive);
        return std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.scene == glyphs->scene; });
    }
};

void run(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    Harness h(shader, notesFormat);
    auto& canvas = h.owner->canvas();
    h.modules.select(core::Module::archive, h.time);
    const double first = h.frame();
    h.settle();
    check(canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::list && h.owner->state().active(), "Archive opens on its list over the activated service");
    check(!h.owner->requiresFrames(h.time), "The settled list asks for no frames");
    const auto idle0 = h.raster.stats();
    double idle{};
    for (unsigned n = 0; n < 60; ++n) idle += h.frame();
    check(h.raster.stats().rasterizations == idle0.rasterizations, "Idle Archive frames rasterize nothing");
    // New opens the editor at once (DOCS-416) with the persistent body editor.
    check(canvas.performAccessibilityAction("new", h.time), "New is an accessible control");
    h.await("the created document", [&] { return canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::editor && h.owner->body().shown(); });
    check(canvas.bodyArea() && h.owner->body().attached(*canvas.bodyArea()) && h.insertedBody(), "The body editor is drawn in the canvas scene after the body part");
    check(canvas.setAccessibilityText("title", "Synthetic report", h.time), "The title takes generated text (UIA SetValue)");
    check(canvas.performAccessibilityAction("body", h.time) && canvas.bodyArea()->isFocused(), "The body takes the focus");
    for (const char c : std::string("Body text")) check(h.owner->key({app::KeyKind::character, std::uint32_t(c)}, h.time), "The body takes typed characters");
    h.settle(5);
    check(h.owner->state().selected()->body == "Body text" && h.owner->state().selected()->title == "Synthetic report" && !h.owner->state().selected()->titleRichText,
        "Typed body text and the plain title reach the state");
    check(h.owner->capturesKeys(), "Keys reach Archive while its body is focused");
    // Switched away inside the open HUD and back: the stand-by paints nothing again.
    canvas.bodyArea()->document().setSelection({{2, 6}});
    h.settle(10);
    const auto shown = h.capture();
    const auto before = h.raster.stats();
    // The slowest frame of each animated switch (60 Hz, until settled).
    const auto slowest = [&] { double v{}; for (unsigned n = 0; n < 60; ++n) v = std::max(v, h.frame()); return v; };
    h.modules.select(core::Module::power, h.time); const double away = slowest();
    check(!canvas.isActive() && !canvas.bodyArea()->isFocused(), "Away, the canvas is inactive and the body has no focus");
    h.modules.select(core::Module::archive, h.time); const double back = slowest();
    check(h.raster.stats().pixelRepaints == before.pixelRepaints, "The switch back paints nothing again (stand-by)");
    check(canvas.bodyArea() && canvas.bodyArea()->isFocused() && canvas.bodyArea()->document().selection().range == core::text::Range{2, 6}, "The body keeps its focus and selection across the switch (B14)");
    // textDidEndEditing (ArchiveCanvas.swift:674-677): a press the shell or
    // another owner takes ends the body's editing and flushes what was typed.
    check(h.owner->key({app::KeyKind::character, std::uint32_t('!')}, h.time), "The focused body replaces its selection");
    canvas.context().editorFocusMoved();
    check(!canvas.bodyArea()->isFocused() && h.owner->state().selected()->body == "Bo!ext", "A press elsewhere blurs the body; the typed text is kept and flushed");
    canvas.performAccessibilityAction("body", h.time);
    check(canvas.bodyArea()->isFocused(), "The body takes the focus again");
    (void)shown;
    // A picture: the import inspection, the strip's preview record, the poster.
    const auto picture = h.temporary.root / "picture.png";
    std::filesystem::copy_file(notesFormat / "raster" / (std::string(m::notesColorWheelSHA256) + ".png"), picture);
    // SHELL-6 (documentDropURLs: centreContains): the host routes a drop to
    // Archive only inside the canvas's hitBounds.
    const auto centre = h.owner->core().clientRect({295, 255, 0, 0}), below = h.owner->core().clientRect({295, 560, 0, 0});
    check(centre && below && h.owner->acceptsDrop({centre->x, centre->y}) && !h.owner->acceptsDrop({below->x, below->y}), "Archive takes a drop inside its centre only");
    const auto request = h.owner->dropRequest(h.time);
    check(request.has_value(), "A drop on the open document is accepted");
    const std::array files{tools::ArchiveMediaImportFile{utf8(picture), "picture.png", {}}};
    check(h.owner->importFiles(*request, files, h.time), "The import inspection starts");
    h.await("the imported picture", [&] { return h.owner->state().selected()->media.size() == 1; });
    h.await("the strip's preview", [&] { return h.owner->media().frame().has_value(); });
    h.settle(5);
    check(std::abs(canvas.bodyRect().height - 163) < 1e-6, "With media the body is 163 tall");
    // Back to the list: the tile's poster.
    check(canvas.performAccessibilityAction("back", h.time), "Back");
    h.await("the poster", [&] { return canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::list && h.owner->media().posters() == 1; });
    // Classified: locked, then relocked when left.
    const auto id = h.owner->state().entries().front().id;
    check(canvas.performAccessibilityAction("doc:" + id, h.time), "The tile opens its document");
    h.await("the reopened document", [&] { return canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::editor; });
    const_cast<m::ArchiveState&>(h.owner->state()).classifySelected("correct horse");
    h.await("the sealed document", [&] { return canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::locked; });
    check(!h.owner->body().shown(), "A locked document draws no body editor");
    // The locked panel fades as one image (CALayer group opacity, its stagger,
    // module switches, the HUD's fades): its plate and the bars, stamp and
    // password bar after the revealed text's own part draw in one group (one
    // entry), not the plate faded apart under them.
    {
        h.settle(5);
        const std::function<d::Layer*(d::Layer&)> find = [&](d::Layer& l) -> d::Layer* {
            if (l.id().ends_with(".archive.classified")) return &l;   // the arena prefixes its ids
            for (auto* child : l.children()) if (auto* found = find(*child)) return found;
            return nullptr;
        };
        auto* panel = find(canvas.root());
        check(panel != nullptr, "The locked page shows its panel");
        const auto rest = h.owner->entries(core::Module::archive).size();
        panel->setValue(d::Channel::opacity, .5); h.frame();
        const auto faded = h.owner->entries(core::Module::archive).size();
        // Apart, the plate keeps its own entry and the bars' part takes a group: as many entries as at rest.
        check(faded < rest && h.owner->core().sceneStats(core::Module::archive)->groups == 1, "Fading, one group draws the locked panel's plate with its bars, stamp and password bar");
        panel->setValue(d::Channel::opacity, 1); h.frame();
        check(h.owner->entries(core::Module::archive).size() == rest && h.owner->core().sceneStats(core::Module::archive)->groups == 0, "At rest the panel's parts draw directly");
    }
    // The HUD closes in the host's order (overlayClosing, frames, then the
    // hidden HUD): through the close the page stays active and drawn with no
    // input and its media stay; the hidden HUD ends the session, the canvas
    // discards its document and the media go.
    {
        const auto posters = h.owner->media().posters();
        h.owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time); h.settle(5);
        check(canvas.isActive() && canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::locked && !h.owner->capturesPointer()
            && !h.owner->entries(core::Module::archive).empty() && h.owner->media().posters() == posters,
            "Through the close the page stays active and drawn with its media, taking no input");
    }
    (void)h.owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); (void)h.overlay->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); h.settle(5);
    check(!canvas.isActive(), "The hidden HUD leaves the page");
    check(!canvas.bodyArea() && h.owner->media().posters() == 0, "The closed HUD keeps no document editor or posters");
    std::printf("Archive owner: first showing %.1f ms | idle %.3f ms/frame | switch away %.1f ms, back %.1f ms (slowest frames) | %zu raster entries\n",
        first, idle / 60, away, back, h.raster.stats().entries);
}
}
// The host's order when the HUD opens on another module and closes before
// Archive was ever shown (application_owner_coverage's benchmark phase), then
// a later opening that shows it and a close after that, with the native owner
// proof the host builds.
void unseen(const std::filesystem::path& shader, const std::filesystem::path& notesFormat) {
    Harness h(shader, notesFormat, true);
    // overlayClosing, the close's frames, then the hidden HUD (no
    // setOverlayVisible(false): the host never sends it on a close).
    const auto closeHUD = [&](bool shown) {
        h.owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time); h.settle(30);
        if (shown) check(h.owner->canvas().isActive() && h.owner->canvas().bodyArea() && h.owner->state().active() && !h.owner->capturesPointer(),
            "Through the close the shown Archive stays active with its document, taking no input");
        (void)h.owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); (void)h.overlay->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); h.settle(10);
    };
    h.settle(20);
    closeHUD(false);
    check(!h.owner->canvas().bodyArea() && !h.owner->state().active(), "Closing before Archive was shown touches nothing");
    h.owner->setOverlayVisible(true, h.time); h.overlay->setOverlayVisible(true, h.time);
    h.modules.select(core::Module::archive, h.time); h.settle(40);
    check(h.owner->canvas().displayedPage().kind == m::ArchiveCanvas::PageKind::list, "A later opening shows the list");
    check(h.owner->canvas().performAccessibilityAction("new", h.time), "New after an unseen session");
    h.settle(20);
    closeHUD(true);
    check(!h.owner->canvas().bodyArea() && !h.owner->canvas().isActive(), "The hidden HUD after showing discards the document");
}
// Mac 815b343 with db7c5c0's media half: a movie playing when Archive is
// switched away comes back paused where it was left, its frame seeked there
// and no new play; a new HUD opening starts it again at zero, paused.
void movieReturn(const std::filesystem::path& shader, const std::filesystem::path& notesFormat, const std::filesystem::path& movieFile) {
    Harness h(shader, notesFormat, false, true);
    auto& canvas = h.owner->canvas();
    auto& media = h.owner->media();
    const auto texture = [&](std::string_view part) { const auto f = media.frame(); return f && f->texture.find(part) != std::string::npos; };
    const auto label = [&] { std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items); for (const auto& i : items) if (i.id == "mediaPlay") return i.label; return std::string{}; };
    const auto away = [&] { h.modules.select(core::Module::power, h.time); h.settle(60); };
    const auto back = [&] { h.modules.select(core::Module::archive, h.time); h.settle(10); };
    h.modules.select(core::Module::archive, h.time); h.settle();
    check(canvas.performAccessibilityAction("new", h.time), "New");
    h.await("the created document", [&] { return canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::editor; });
    const auto movie = h.temporary.root / "movie.mp4";
    std::filesystem::copy_file(movieFile, movie);
    const auto request = h.owner->dropRequest(h.time);
    check(request.has_value(), "A drop on the open document is accepted");
    const std::array files{tools::ArchiveMediaImportFile{utf8(movie), "movie.mp4", {}}};
    check(h.owner->importFiles(*request, files, h.time), "The movie's import inspection starts");
    h.await("the imported movie", [&] { return h.owner->state().selected()->media.size() == 1; });
    check(h.owner->state().selected()->media[0]["kind"] == m::ArchiveJson("video"), "The import is a video");
    h.await("the movie's still", [&] { return h.probes.size() == 1 && texture(".poster"); });
    check(!media.playing() && h.probes[0]->plays == 0, "The movie opens paused");
    check(canvas.performAccessibilityAction("mediaPlay", h.time), "Play is an accessible control");
    h.await("the playing movie", [&] { return media.playing() && h.probes[0]->playing; });
    h.probes[0]->position = .6; h.settle(5);
    away();
    check(!canvas.isActive() && !media.playing() && !h.probes[0]->playing, "Switched away, the movie is paused and its engine stopped");
    back();
    h.await("the returned movie's seek", [&] { return h.probes.size() == 2 && h.probes[1]->seeks == 1; });
    h.await("the seeked frame", [&] { return texture(".live"); });
    h.settle(10);
    check(canvas.isActive() && !media.playing() && !h.probes[1]->playing && h.probes[1]->plays == 0 && std::abs(h.probes[1]->position - .6) < 1e-9
        && std::abs(media.currentTime() - .6) < 1e-9 && label() == "Play", "Back, the movie shows its frame where it was left, paused, with Play");
    // The HUD closes and opens again: a new presentation at zero, paused.
    h.owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time); h.settle(10);
    (void)h.owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); (void)h.overlay->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time); h.settle(10);
    const auto engines = h.probes.size();
    h.owner->setOverlayVisible(true, h.time); h.overlay->setOverlayVisible(true, h.time);
    away(); back();
    h.await("the next opening's Archive", [&] { return canvas.isActive() && h.owner->state().active() && canvas.displayedPage().kind != m::ArchiveCanvas::PageKind::locked; });
    h.settle(30);
    // The stored selection may open the document at once; from the list its tile does.
    if (canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::list)
        check(canvas.performAccessibilityAction("doc:" + h.owner->state().entries().front().id, h.time), "The tile opens its document again");
    h.await("the reopened movie's still", [&] { return canvas.displayedPage().kind == m::ArchiveCanvas::PageKind::editor && h.probes.size() == engines + 1 && texture(".poster"); });
    h.settle(10);
    check(!media.playing() && h.probes.back()->seeks == 0 && h.probes.back()->plays == 0 && media.currentTime() == 0, "A new HUD opening shows the movie at zero, paused");
    // A still that fails on the return does not block the movie (NotesMedia.swift:370-371).
    check(canvas.performAccessibilityAction("mediaPlay", h.time), "Play the reopened movie");
    h.await("the reopened movie playing", [&] { return media.playing() && h.probes.back()->playing; });
    h.probes.back()->position = .6; h.settle(5);
    away();
    std::filesystem::remove(movie);
    back();
    h.await("the movie's return without its still", [&] { return h.probes.size() == engines + 2 && h.probes.back()->seeks == 1; });
    h.settle(10);
    check(!media.playing() && h.probes.back()->plays == 0, "The movie whose still failed comes back paused where it was left");
    check(canvas.performAccessibilityAction("mediaPlay", h.time), "Play is pressed");
    h.await("the movie playing without its still", [&] { return media.playing() && h.probes.back()->playing && h.probes.back()->plays == 1; });
}
int wmain(int argc, wchar_t** argv) {
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(hr) && argc == 4, "Pass the shader, resources/notes-format and the synthetic movie");
        run(argv[1], argv[2]);
        unseen(argv[1], argv[2]);
        movieReturn(argv[1], argv[2], argv[3]);
        CoUninitialize();
        std::cout << "PASS " << checks << " Archive owner checks\n";
        return 0;
    } catch (const std::exception& e) { if (SUCCEEDED(hr)) CoUninitialize(); std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
#else
int main() { return 0; }
#endif
