#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tools/notes_preview.hpp"
#include "core/data/data_store.hpp"
#include "core/module_presentation.hpp"
#include <objbase.h>
#include <dwrite.h>
#include <winsqlite/winsqlite3.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

namespace { std::atomic<bool> counting{}; std::atomic<std::size_t> allocations{}; }
void* operator new(std::size_t n) {
    if (counting) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace gpu = endfield::native;
namespace core = endfield::core;
namespace app = endfield::app;
namespace source = core::source;
namespace data = ehud::data;
namespace tools = endfield::tools;

namespace {
std::size_t checks{};
void check(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
bool coordinatesNear(double a, double b) { return std::abs(a - b) < 1e-8; }

struct Apartment final {
    Apartment() { check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Owned fixture COM apartment initializes"); }
    ~Apartment() { CoUninitialize(); }
};
struct Window final {
    ATOM atom{};
    HWND hwnd{};
    Window() {
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"EndfieldNotesPreviewFixture";
        atom = RegisterClassW(&c);
        check(atom != 0, "Owned hidden preview class registers");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
            c.lpszClassName, L"Owned Notes preview integration", WS_POPUP,
            0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd != nullptr, "Owned hidden preview HWND creates");
        check(!IsWindowVisible(hwnd), "Fixture HWND starts hidden");
    }
    ~Window() {
        if (hwnd) DestroyWindow(hwnd);
        if (atom) UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)), GetModuleHandleW(nullptr));
    }
};

// The directory is deliberately NOT created here: NotesPreview requires a new
// injected root and creates its own store. Never resolve an application default.
struct TempRoot final {
    std::filesystem::path path = std::filesystem::absolute(std::filesystem::temp_directory_path()) /
        ("endfield-notes-preview-" + data::makeUUID());
    TempRoot() { check(!std::filesystem::exists(path), "Injected fixture root is new"); }
    ~TempRoot() { std::error_code error; std::filesystem::remove_all(path, error); }
};

std::vector<data::Note> savedNotes(const std::filesystem::path& root) {
    // Reopen only this fixture's real SQLite store, rather than observing the
    // preview's in-memory state. This scope closes before the next input event.
    data::NotesStore store(root);
    check(store.path() == root / "Notes" / "notes.sqlite3", "Fixture uses the actual Notes SQLite path");
    return store.notes();
}
void schema(const std::filesystem::path& root) {
    const auto path = (root / "Notes" / "notes.sqlite3").u8string();
    sqlite3* db{};
    const auto opened = sqlite3_open_v2(reinterpret_cast<const char*>(path.c_str()), &db,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
    struct Close { sqlite3* db; ~Close() { if (db) sqlite3_close_v2(db); } } close{db};
    check(opened == SQLITE_OK, "Owned SQLite file opens read-only for verification");
    sqlite3_stmt* statement{};
    const auto prepared = sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &statement, nullptr);
    struct Finalize { sqlite3_stmt* statement; ~Finalize() { if (statement) sqlite3_finalize(statement); } } finalize{statement};
    check(prepared == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 2,
        "End-to-end fixture persists the original Notes schema v2");
}

// Same publication rule as the shared shell owner: local text updates can
// advance resourceRevision without changing structural contentRevision.
struct Fixture final {
    struct Published {
        gpu::LayerScene* scene{};
        std::uint64_t content{}, resources{};
        const gpu::DrawObject* after{};
        std::size_t afterCount{};
    };
    gpu::Renderer& renderer;
    HWND ownerWindow{};
    TempRoot root;
    std::unique_ptr<tools::NotesPreview> preview;
    gpu::LayerComposition composition;
    std::vector<Published> published;
    source::DesktopChromeSettings settings;
    core::Matrix4 design, camera{gpu::layerViewportProjection(1280, 800)}, workspace;
    double time{};
    std::size_t publications{};
    bool released{};

    Fixture(HWND hwnd, gpu::Renderer& r, gpu::LayerRasterizer& raster,
            const gpu::NativeNotesControlsAssets& assets) : renderer(r), ownerWindow(hwnd) {
        settings.viewport = {0, 0, 1280, 800};
        settings.module = core::Module::notes;
        settings.sourceShell = true;
        design = source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
        published.reserve(132);
        preview = std::make_unique<tools::NotesPreview>(hwnd, raster, root.path, assets, false);
        preview->resize({1280, 800, 96, 1, 1280, 800});
        frame();
    }
    ~Fixture() {
        counting = false;
        if (released) return;
        try {
            composition.detach(renderer);
            if (preview && renderer.stats().initialized) preview->release(renderer);
        } catch (...) {
            // The renderer's failure contract permits explicit device reset;
            // no externally owned/user window or store is involved here.
            renderer.reset();
        }
        preview.reset();
    }
    void frame() {
        preview->update(workspace * design, settings, 1, time, true);
        preview->upload(renderer);
        const auto entries = preview->entries();
        bool changed = entries.size() != published.size();
        if (!changed) for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            const auto& previous = published[i];
            if (entry.scene != previous.scene || entry.scene->contentRevision() != previous.content ||
                entry.scene->resourceRevision() != previous.resources || entry.after.data() != previous.after ||
                entry.after.size() != previous.afterCount) { changed = true; break; }
        }
        if (changed) {
            composition.setEntries(renderer, entries);
            published.clear();
            for (const auto& entry : entries) published.push_back({entry.scene,
                entry.scene->contentRevision(), entry.scene->resourceRevision(), entry.after.data(), entry.after.size()});
            preview->collected(renderer);
            ++publications;
        }
        composition.present(renderer);
        renderer.setCamera(camera);
    }
    void tick(double elapsed) { time += elapsed; frame(); }
    bool pointer(app::PointerKind kind, core::Point point, app::PointerButton button = app::PointerButton::left) {
        tick(.01);
        const bool handled = preview->pointer({kind, button, point.x, point.y, 0}, time);
        frame();
        return handled;
    }
    bool key(app::KeyKind kind, std::uint32_t value) {
        tick(.01);
        const bool handled = preview->key({kind, value}, time);
        frame();
        return handled;
    }
    unsigned editorNotifications() {
        unsigned count{};
        MSG message{};
        // Process only this owned field's actual posted notifications. No
        // real TSF manager, language profile or external input is activated.
        while (PeekMessageW(&message, ownerWindow, WM_APP + 181, WM_APP + 181, PM_REMOVE)) {
            check(++count <= 64, "Owned editor notifications remain a finite batch");
            check(preview->message({message.hwnd, message.message, message.wParam, message.lParam}),
                "Actual queued editor notification reaches its preview owner");
        }
        frame();
        return count;
    }
    core::Point caretPoint(std::uint32_t acp) const {
        std::shared_ptr<const gpu::PaintedTextLayout> layout;
        for (const auto& entry : preview->entries()) if ((layout = entry.scene->paintedTextLayout("projected-editor-glyphs"))) break;
        check(bool(layout), "Editing fixture exposes its exact retained painted text layout");
        auto* native = reinterpret_cast<IDWriteTextLayout*>(layout->layoutIdentity());
        FLOAT x{}, y{};
        DWRITE_HIT_TEST_METRICS hit{};
        check(SUCCEEDED(native->HitTestTextPosition(acp, FALSE, &x, &y, &hit)),
            "Synthetic selection point comes from the same painted DirectWrite object");
        const auto* glyph = draw("projected-editor-glyphs");
        check(glyph != nullptr, "Actual editing glyph plane is published");
        return project(glyph->world, {double(x) + .1, double(y) + std::max(1., double(hit.height) * .5)});
    }
    const gpu::DrawObject* draw(std::string_view suffix) const noexcept {
        const auto draws = composition.draws();
        const auto found = std::find_if(draws.begin(), draws.end(), [&](const auto& d) { return d.sourceID.ends_with(suffix); });
        return found == draws.end() ? nullptr : &*found;
    }
    std::size_t matching(std::string_view suffix, bool onlyVisible = false) const noexcept {
        return static_cast<std::size_t>(std::count_if(composition.draws().begin(), composition.draws().end(), [&](const auto& d) {
            return d.sourceID.ends_with(suffix) && (!onlyVisible || d.opacity > 0);
        }));
    }
    core::Point project(const core::Matrix4& world, core::Point local) const {
        const auto value = core::Projection::viewport(camera * world, 1280, 800).project(local);
        check(value.has_value(), "Synthetic input projects through the same retained plane");
        return *value;
    }
    void release() {
        check(preview->finish(), "Synthetic plain editor is unlocked at teardown");
        frame();
        composition.detach(renderer);
        preview->release(renderer);
        preview.reset();
        released = true;
        check(renderer.stats().objects == 0 && renderer.stats().textures == 0 &&
            renderer.stats().meshes == 0 && renderer.stats().nativeGroups == 0 && renderer.stats().resourceBytes == 0,
            "One composition teardown releases Notes, editor, registration and grouped popup resources");
    }
};

core::Point pinPoint(const data::Note& note) { return {note.x + note.width - 35.5, note.y + 12}; }
core::Point deletePoint(const data::Note& note) { return {note.x + note.width - 13.5, note.y + 12}; }
core::Point confirmationPoint(const data::Note& note, bool confirm) {
    // Actual NotesCanvas/NotesState rule: 25pt buttons at (right-59, top+27)
    // and +31pt. These fixture cards are away from the workspace clamp edges.
    return {note.x + note.width - 59 + (confirm ? 31 : 0) + 12.5, note.y + 27 + 12.5};
}
std::string cardID(const data::Note& note) { return "note/" + note.id + "/card"; }

// Deliver the capture notification synchronously through our own hidden HWND,
// as ReleaseCapture does. Do not take the user's real pointer capture/focus.
struct CaptureRoute final {
    HWND hwnd;
    WNDPROC previous{};
    tools::NotesPreview& preview;
    double time;
    unsigned delivered{};
    std::exception_ptr failure;
    static constexpr wchar_t property[] = L"EndfieldNotesClockRegression";
    static LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = static_cast<CaptureRoute*>(GetPropW(hwnd, property));
        if (message == WM_CAPTURECHANGED && self) {
            try {
                ++self->delivered;
                self->preview.pointer({app::PointerKind::captureLost, app::PointerButton::none, 0, 0, 0}, self->time);
            } catch (...) { self->failure = std::current_exception(); }
            return 0;
        }
        return self ? CallWindowProcW(self->previous, hwnd, message, w, l) : DefWindowProcW(hwnd, message, w, l);
    }
    CaptureRoute(HWND h, tools::NotesPreview& p, double t) : hwnd(h), preview(p), time(t) {
        check(SetPropW(hwnd, property, this) != FALSE, "Owned capture callback installs");
        previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dispatch)));
        check(previous != nullptr, "Owned hidden window accepts its temporary callback");
    }
    ~CaptureRoute() {
        if (previous) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        RemovePropW(hwnd, property);
    }
    void send() {
        SendMessageW(hwnd, WM_CAPTURECHANGED, 0, 0);
        if (failure) std::rethrow_exception(failure);
        check(delivered == 1, "Capture-loss callback finishes synchronously before the outer callback resumes");
    }
};

// Mirror the shell owner's posted frame/notification route on this fixture's
// hidden HWND. The real NotesPreview and text store produce/consume the editor
// notifications; no TSF manager, timer, visible host or hardware input is used.
struct EditorQueueRoute final {
    static constexpr UINT frameMessage = WM_APP + 0x37b;
    static constexpr wchar_t property[] = L"EndfieldNotesPostedFrameRegression";
    HWND hwnd;
    Fixture& fixture;
    WNDPROC previous{};
    std::exception_ptr failure;
    bool pending{}, advancePose{};
    unsigned frames{}, notifications{}, keys{};

    EditorQueueRoute(HWND h, Fixture& f) : hwnd(h), fixture(f) {
        check(SetPropW(hwnd, property, this) != FALSE, "Owned queue callback installs");
        previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(dispatch)));
        check(previous != nullptr, "Hidden queue fixture accepts its temporary callback");
    }
    ~EditorQueueRoute() {
        if (previous) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        RemovePropW(hwnd, property);
        MSG message{};
        while (PeekMessageW(&message, hwnd, frameMessage, frameMessage, PM_REMOVE)) {}
        while (PeekMessageW(&message, hwnd, WM_APP + 181, WM_APP + 181, PM_REMOVE)) {}
    }
    void requestFrame() {
        if (pending) return;
        check(PostMessageW(hwnd, frameMessage, 0, 0) != FALSE, "Owned invalidation posts one frame");
        pending = true;
    }
    void postKey(UINT message, WPARAM value) {
        check(PostMessageW(hwnd, message, value, 0) != FALSE, "Owned synthetic keyboard event queues");
    }
    static LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = static_cast<EditorQueueRoute*>(GetPropW(hwnd, property));
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        try {
            if (message == frameMessage) {
                self->pending = false; // Same consume-before-callback order as OverlayHost.
                ++self->frames;
                if (self->advancePose) {
                    // A finite deterministic sequence models sampling an active
                    // gyro transition. Only the first frame is requested from
                    // outside this route; a second one requires an owner echo.
                    self->fixture.workspace.values[12] = double(self->frames) * .125;
                    self->fixture.time += .001;
                }
                self->fixture.frame();
                return 0;
            }
            if (message == WM_APP + 181) {
                ++self->notifications;
                check(self->fixture.preview->message({hwnd, message, w, l}),
                    "Actual queued editor notification reaches its owner");
                self->requestFrame(); // Current shell's handled-message refresh.
                return 0;
            }
            if (message == WM_KEYDOWN || message == WM_CHAR) {
                ++self->keys;
                self->fixture.time += .001;
                const auto kind = message == WM_CHAR ? app::KeyKind::character : app::KeyKind::down;
                check(self->fixture.preview->key({kind, static_cast<std::uint32_t>(w)}, self->fixture.time),
                    "Queued synthetic keyboard event reaches the actual editor");
                self->requestFrame();
                return 0;
            }
        } catch (...) { self->failure = std::current_exception(); return 0; }
        return CallWindowProcW(self->previous, hwnd, message, w, l);
    }
    bool drain(unsigned& dispatched) {
        MSG message{};
        dispatched = 0;
        while (dispatched < 32 && PeekMessageW(&message, hwnd, 0, 0, PM_REMOVE)) {
            ++dispatched;
            // Explicit WM_CHAR is already queued: translating our synthetic
            // key messages would invent a second character event.
            DispatchMessageW(&message);
            if (failure) std::rethrow_exception(failure);
        }
        return !PeekMessageW(&message, hwnd, 0, 0, PM_NOREMOVE);
    }
    void resetCounts() { frames = notifications = keys = 0; }
};

void postedEditorFrames(HWND hwnd, gpu::Renderer& renderer, const gpu::NativeNotesControlsAssets& assets) {
    gpu::LayerRasterizer raster;
    Fixture f(hwnd, renderer, raster, assets); // activateTextServices=false
    const auto focus = GetFocus(), active = GetActiveWindow(), capture = GetCapture();
    const auto note = savedNotes(f.root.path).front();
    check(f.pointer(app::PointerKind::doubleClick, {note.x + 30, note.y + 45}),
        "Posted-frame regression enters the actual isolated editor");
    f.editorNotifications();
    auto painted = [&]() -> std::shared_ptr<const gpu::PaintedTextLayout> {
        for (const auto& entry : f.preview->entries())
            if (auto value = entry.scene->paintedTextLayout("projected-editor-glyphs")) return value;
        return {};
    };
    {
        EditorQueueRoute route(hwnd, f);
        unsigned dispatched{};
        check(route.drain(dispatched), "Editor setup notifications settle within 32 actual dispatches");
        route.resetCounts();
        route.postKey(WM_KEYDOWN, VK_HOME);
        route.postKey(WM_CHAR, '@');
        route.postKey(WM_CHAR, '?');
        route.postKey(WM_KEYDOWN, VK_LEFT);
        check(route.drain(dispatched), "Queued text and selection changes form a finite batch");
        check(route.keys == 4 && route.notifications == 1 && route.frames == 1,
            "Real text/selection notifications and invalidations coalesce into one owner frame");
        const auto edited = painted();
        check(edited && edited->text().starts_with(u"@?"), "Queued text reaches the exact painted editor layout");
        const auto editedText = std::u16string(edited->text());
        route.resetCounts();
        route.postKey(WM_KEYDOWN, VK_RIGHT);
        check(route.drain(dispatched) && route.keys == 1 && route.notifications == 1 && route.frames == 1,
            "A genuine selection-only notification still requests one coalesced owner frame");
        check(painted()->layoutIdentity() == edited->layoutIdentity() && painted()->text() == editedText,
            "Selection dispatch retains painted text and its DirectWrite layout");
        check(savedNotes(f.root.path).front().text == note.text, "Queued editor input does not save a draft");
        check(GetFocus() == focus && GetActiveWindow() == active && GetCapture() == capture && !IsWindowVisible(hwnd),
            "Posted regression never activates a window, takes capture or changes native focus");

        const auto* previousGlyph = f.draw("projected-editor-glyphs");
        check(previousGlyph != nullptr, "Queued regression retains its actual glyph plane");
        const auto previousWorld = previousGlyph->world;
        route.resetCounts();
        route.advancePose = true;
        route.requestFrame(); // Exactly one external invalidation.
        const bool settled = route.drain(dispatched);
        if (!settled || route.frames != 1 || route.notifications != 0)
            std::cerr << "Posted editor frame diagnostic: dispatches=" << dispatched << " frames=" << route.frames
                << " ownerNotices=" << route.notifications << " queueEmpty=" << settled << '\n';
        check(settled && route.frames == 1 && route.notifications == 0,
            "Host placement must not echo an owner notification and self-generate more frames (32-dispatch bound)");
        const auto* glyph = f.draw("projected-editor-glyphs");
        check(glyph && coordinatesNear(glyph->world.values[12], previousWorld.values[12] + .125) &&
            painted()->layoutIdentity() == edited->layoutIdentity(),
            "The isolated frame still updates projected geometry while retaining its painted layout");
    }
    f.release();
}

void reentrantClock(HWND hwnd, gpu::Renderer& renderer, const gpu::NativeNotesControlsAssets& assets) {
    gpu::LayerRasterizer raster;
    Fixture f(hwnd, renderer, raster, assets);
    const auto nativeCapture = GetCapture();
    auto note = savedNotes(f.root.path).front();
    const auto* tool = f.draw("tool:text");
    check(tool != nullptr, "Capture clock regression uses the actual Notes toolbar");
    const auto toolbar = f.project(tool->world, {46, 15.5});
    f.pointer(app::PointerKind::move, toolbar, app::PointerButton::none);
    f.tick(.2);
    // Outer mouse-up clears pressed state but keeps a hovered toolbar. The
    // nested capture notification removes hover at a later QPC timestamp.
    const double outer = f.time;
    f.preview->pointer({app::PointerKind::up, app::PointerButton::left, toolbar.x, toolbar.y, 0}, outer);
    {
        CaptureRoute nested(hwnd, *f.preview, outer + .001);
        nested.send();
    }
    check(f.preview->requiresFrames(outer), "Older outer refresh preserves the capture-loss fade instead of rejecting its timestamp");
    f.frame(); // an already-started older frame resumes after the newer input
    f.tick(.3);
    check(!f.preview->requiresFrames(outer), "A stale demand query cannot restart a settled feedback animation");

    const core::Point header{note.x + 32, note.y + 12};
    check(f.pointer(app::PointerKind::down, header) && f.preview->pointerLocked(), "Clock regression starts an actual card drag");
    const double beforeMove = f.time;
    f.time += .02;
    check(f.preview->pointer({app::PointerKind::move, app::PointerButton::none, header.x + 48, header.y + 31, 0}, f.time),
        "Newer input moves the captured card before the interrupted frame resumes");
    f.preview->update(f.workspace * f.design, f.settings, 1, beforeMove, true);
    check(f.preview->pointerLocked(), "Old frame does not cancel the active drag");
    check(savedNotes(f.root.path).front().x == note.x, "Reentrant drag frames keep the source commit-on-release rule");
    f.time += .02;
    f.preview->pointer({app::PointerKind::up, app::PointerButton::left, header.x + 48, header.y + 31, 0}, f.time);
    {
        CaptureRoute nested(hwnd, *f.preview, f.time + .001);
        nested.send();
    }
    f.frame();
    check(!f.preview->pointerLocked(), "Capture loss leaves no stuck card gesture");
    auto moved = savedNotes(f.root.path).front();
    check(moved.x == note.x + 48 && moved.y == note.y + 31 && moved.text == note.text,
        "Nested release commits the intended position without changing note text");
    f.time += .3;
    f.frame();
    f.preview->select(core::Module::map, beforeMove);
    f.tick(core::ModuleTransitionStyle::duration + .01);
    check(f.preview->selected() == core::Module::map && !f.preview->requiresFrames(beforeMove),
        "Queued older module request shares the monotonic owner clock and settles");
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        bool rejected{};
        try { (void)f.preview->requiresFrames(invalid); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Owner still rejects nonfinite animation timestamps");
    }
    check(GetCapture() == nativeCapture && !IsWindowVisible(hwnd), "Clock regression never takes real input capture or shows a window");
    f.release();
}

void scrollPreview(HWND hwnd, gpu::Renderer& renderer, const gpu::NativeNotesControlsAssets& assets) {
    gpu::LayerRasterizer raster;
    Fixture f(hwnd, renderer, raster, assets);
    const auto note = savedNotes(f.root.path).front();
    const core::Point body{note.x + 30, note.y + 45};
    auto wheel = [&](double steps) {
        f.time += .01;
        const bool handled = f.preview->wheel({body.x,body.y,steps,false,0,3},f.time);
        f.frame();
        return handled;
    };
    renderer.draw(false);const auto top = renderer.readback();
    check(wheel(-1), "Actual Notes wheel routes to the nonediting card");
    renderer.draw(false);const auto lower = renderer.readback();
    check(top.pixels != lower.pixels, "Forwarded wheel visibly reveals different note lines");
    check(savedNotes(f.root.path).front().text == note.text, "View scrolling never edits stored note text");
    check(f.pointer(app::PointerKind::doubleClick,body), "Scrolled note enters projected editing");
    auto painted = [&]() -> std::shared_ptr<const gpu::PaintedTextLayout> {
        for(const auto& entry:f.preview->entries())if(auto value=entry.scene->paintedTextLayout("projected-editor-glyphs"))return value;
        return {};
    };
    const auto initial = painted();check(bool(initial),"Scrollable editor publishes its measured text layout");
    const auto identity = initial->layoutIdentity();
    const auto layouts = raster.stats().textLayoutsCreated;
    check(wheel(-1),"Actual Notes wheel reaches the projected editor");
    check(painted()->layoutIdentity() == identity && raster.stats().textLayoutsCreated == layouts,
        "Editing scroll reuses the exact glyph/hit/IME layout without reshaping");
    // Moderate finite wheel events reach the lower bound without crossing the
    // horizon of a tilted plane or fabricating an unbounded input displacement.
    for(unsigned n=0;n<12;++n)check(wheel(-1),"Editor consumes scrolling through and at the lower limit");
    const auto atLimit=raster.stats().rasterizations;
    check(wheel(-1)&&raster.stats().rasterizations==atLimit,"Extra wheel at the limit creates no new artwork");
    check(wheel(1),"Editor can scroll back upward");
    check(savedNotes(f.root.path).front().text == note.text,"Editing viewport motion does not save the draft");
    check(f.key(app::KeyKind::down,VK_ESCAPE),"Scrolled editor finishes normally");
    check(savedNotes(f.root.path).front().text == note.text,"Finishing unchanged scrolled text preserves stored content");
    check(!f.preview->wheel({1270,790,-1,false,0,3},f.time),"Wheel outside Notes remains available to its owner");
    f.release();
}

void run(HWND hwnd, gpu::Renderer& renderer, const gpu::NativeNotesControlsAssets& assets) {
    gpu::LayerRasterizer raster;
    Fixture fixture(hwnd, renderer, raster, assets);
    schema(fixture.root.path);
    auto records = savedNotes(fixture.root.path);
    check(records.size() == 1 && data::validUUID(records[0].id), "Preview creates exactly one synthetic plain-text note");
    auto original = records[0];
    check(original.x == 180 && original.y == 245 && original.width == 240 && original.height == 145 &&
        original.kind == data::NoteKind::text && !original.richText && !original.media && !original.drawing,
        "Initial fixture has the specified source workspace geometry and plain payload");
    const auto sampleCard = cardID(original);
    const auto* initial = fixture.draw(sampleCard);
    check(initial && coordinatesNear(initial->world.values[12], 180) && coordinatesNear(initial->world.values[13], 245) &&
        coordinatesNear(initial->world.values[0], 1) && coordinatesNear(initial->world.values[5], 1),
        "Design-to-screen center calibration yields the same unscaled Notes workspace plane");
    check(fixture.composition.sceneCount() == 3 && fixture.matching("projected-editor-glyphs") == 0,
        "Initial toolbar, card and popup carrier share one composition without a duplicate editor");
    check(!fixture.preview->requiresFrames(fixture.time), "Settled initial Notes starts no permanent frame demand");
    check(!fixture.key(app::KeyKind::character, 'x'), "Keyboard input outside editing does not alter a note");
    renderer.draw(false);
    const auto firstPixels = renderer.readback();
    const auto sampleOffset = std::size_t(260) * firstPixels.rowBytes + std::size_t(210) * 4;
    check(firstPixels.width == 1280 && firstPixels.height == 800 && firstPixels.pixels[sampleOffset + 3] > 0,
        "Hidden software renderer paints the actual retained source card");

    // Route Win32-shaped events through NotesPreview; no host/editor/state
    // method is bypassed and no actual TSF manager or IME is activated.
    const core::Point textPoint{original.x + 30, original.y + 45};
    check(fixture.preview->covers(textPoint), "Projected note body participates in shell input occlusion");
    check(fixture.pointer(app::PointerKind::doubleClick, textPoint), "Actual double-click enters the plain projected field");
    check(fixture.key(app::KeyKind::down, VK_HOME), "Explicit Home moves the source end-selected editor to the insertion test start");
    check(fixture.composition.sceneCount() == 4 && fixture.matching("projected-editor-glyphs", true) == 1,
        "Entering edit publishes one actual glyph surface after its card");
    check(fixture.matching("external-editor-border", true) == 1,
        "Source editor border publishes once after selection, glyphs and caret");
    fixture.pointer(app::PointerKind::up, textPoint);
    fixture.pointer(app::PointerKind::captureLost, textPoint, app::PointerButton::none);
    check(fixture.matching("projected-editor-glyphs", true) == 1,
        "Mouse release/capture loss ends pointer selection without closing the editor");
    check(fixture.key(app::KeyKind::character, 'A') && fixture.key(app::KeyKind::unicodeCharacter, 0x2605) &&
        fixture.key(app::KeyKind::character, ' '), "Actual character and scalar events edit the caller-owned field");
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].text == original.text, "Draft input leaves persisted SQLite text unchanged until finish");
    check(fixture.key(app::KeyKind::down, VK_ESCAPE), "Actual finish command exits the projected editor");
    auto edited = std::string("A★ ") + original.text;
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].text == edited && records[0].createdAt == original.createdAt &&
        records[0].id == original.id, "Editor finish persists exact new Unicode and unchanged original metadata");
    check(fixture.composition.sceneCount() == 3 && fixture.matching("projected-editor-glyphs") == 0,
        "Leaving edit replaces one combined list before retiring its field");

    // A normal pointer-down posts selectionChange. Dispatch that WM_APP before
    // the next move, as the real host does; routine refocusing used to clear
    // NativeProjectedEditor's drag even though the preview still owned it.
    check(fixture.pointer(app::PointerKind::doubleClick, textPoint), "Selection regression reopens the actual field");
    fixture.editorNotifications();
    const auto selectionStart = fixture.caretPoint(0);
    const auto selectionEnd = fixture.caretPoint(2); // exact ACP after A and ★
    check(fixture.pointer(app::PointerKind::down, selectionStart), "Actual editor pointer-down begins text selection");
    check(fixture.editorNotifications() > 0, "Selection notification is dispatched between down and drag");
    check(fixture.pointer(app::PointerKind::move, selectionEnd, app::PointerButton::none),
        "Actual next pointer move extends selection after notification dispatch");
    fixture.editorNotifications();
    check(fixture.matching("projected-editor-selection", true) == 1,
        "Queued layout/selection notifications preserve the active nonempty drag selection");
    fixture.pointer(app::PointerKind::up, selectionEnd);
    fixture.pointer(app::PointerKind::captureLost, selectionEnd, app::PointerButton::none);
    check(fixture.key(app::KeyKind::character, '#'), "Typing replaces the actually dragged selection");
    fixture.editorNotifications();
    check(fixture.key(app::KeyKind::down, VK_ESCAPE), "Dragged-selection replacement finishes through actual editor input");
    fixture.editorNotifications();
    edited = "#" + edited.substr(std::string("A★").size());
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].text == edited && records[0].id == original.id &&
        records[0].createdAt == original.createdAt,
        "Notification-interleaved selection replaces exactly the chosen UTF-16 range and preserves persisted metadata");
    original = records[0];

    const core::Point header{original.x + 32, original.y + 12};
    check(fixture.pointer(app::PointerKind::down, header) && fixture.preview->pointerLocked(),
        "Header down starts the actual captured-card gesture");
    check(fixture.pointer(app::PointerKind::move, {header.x + 40, header.y + 25}), "Actual pointer move changes retained card placement");
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].x == original.x && records[0].y == original.y,
        "Drag preview does not write SQLite on pointer frames");
    check(fixture.pointer(app::PointerKind::up, {header.x + 40, header.y + 25}) && !fixture.preview->pointerLocked(),
        "Pointer release finishes the source move transaction");
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].x == original.x + 40 && records[0].y == original.y + 25 && records[0].text == edited,
        "Completed drag persists only the new workspace position");
    original = records[0];

    const core::Point grip{original.x + original.width - 7, original.y + original.height - 7};
    check(fixture.pointer(app::PointerKind::down, grip) && fixture.preview->pointerLocked(), "Original corner grip begins resize through actual hit testing");
    check(fixture.pointer(app::PointerKind::move, {grip.x + 25, grip.y + 15}), "Resize gesture updates measured source geometry");
    fixture.pointer(app::PointerKind::up, {grip.x + 25, grip.y + 15});
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].width == original.width + 25 && records[0].height == original.height + 15,
        "Resize completion preserves actual source size changes in SQLite");
    original = records[0];
    check(fixture.pointer(app::PointerKind::down, pinPoint(original)), "Original header pin control is reachable");
    fixture.pointer(app::PointerKind::up, pinPoint(original));
    fixture.tick(.25);
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].isPinned && records[0].text == edited,
        "Actual pin input persists its flag without changing text");
    original = records[0];

    // A pinned card remains painted while the source module host temporarily
    // rejects input. Test both pointer and keyboard gates during the swap.
    const core::Point pinnedTextPoint{original.x + 30, original.y + 45};
    check(fixture.preview->covers(pinnedTextPoint), "Settled pinned card is reachable before requesting a tab swap");
    fixture.time += .01;
    fixture.preview->select(core::Module::map, fixture.time);
    check(!fixture.preview->covers(pinnedTextPoint) &&
        !fixture.preview->pointer({app::PointerKind::doubleClick, app::PointerButton::left,
            pinnedTextPoint.x, pinnedTextPoint.y, 0}, fixture.time),
        "Tab selection blocks pinned-card input synchronously before the next frame");
    fixture.tick(.05);
    check(fixture.preview->selected() == core::Module::map && fixture.preview->requiresFrames(fixture.time),
        "Tab request starts the finite source transition");
    check(fixture.draw(sampleCard) && fixture.draw(sampleCard)->opacity > 0 &&
        !fixture.preview->covers(pinnedTextPoint), "Pinned card stays visible while mechanical swap blocks its input");
    check(!fixture.pointer(app::PointerKind::doubleClick, {original.x + 30, original.y + 45}) &&
        !fixture.key(app::KeyKind::character, 'z'), "Mechanical tab transition rejects body editing and characters");
    fixture.tick(core::ModuleTransitionStyle::duration + .01);
    check(!fixture.preview->requiresFrames(fixture.time) && fixture.preview->covers({original.x + 30, original.y + 45}),
        "Completed tab swap restores pinned-card hits and settles all finite demand");
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].text == edited, "Rejected transition input never reaches persisted notes");
    fixture.time += .01;
    fixture.preview->select(core::Module::notes, fixture.time);
    fixture.tick(core::ModuleTransitionStyle::duration + .01);

    const auto* tool = fixture.draw("tool:text");
    check(tool && tool->opacity > 0, "Source-prepared Text toolbar plate is retained after returning to Notes");
    const auto createPoint = fixture.project(tool->world, {46, 15.5});
    check(fixture.pointer(app::PointerKind::down, createPoint), "Original toolbar creates a new text panel through actual input");
    fixture.pointer(app::PointerKind::up, createPoint);
    fixture.tick(.22);
    records = savedNotes(fixture.root.path);
    check(records.size() == 2 && fixture.matching("projected-editor-glyphs", true) == 1,
        "Text creation writes one new UUID and opens its own projected field");
    check(fixture.key(app::KeyKind::character, 'N') && fixture.key(app::KeyKind::unicodeCharacter, 0x1f600),
        "New panel accepts ASCII and a non-BMP scalar without truncation");
    check(fixture.key(app::KeyKind::down, VK_ESCAPE), "New panel finish returns to settled card composition");
    records = savedNotes(fixture.root.path);
    const auto created = std::find_if(records.begin(), records.end(), [&](const auto& n) { return n.id != original.id; });
    check(created != records.end() && created->text == "N😀" && !created->isPinned, "New panel text and independent pin state persist exactly");
    const auto second = *created;
    const auto secondCard = cardID(second);

    check(fixture.pointer(app::PointerKind::down, deletePoint(second)), "Original delete icon opens confirmation instead of removing the note");
    fixture.pointer(app::PointerKind::up, deletePoint(second));
    fixture.tick(.17);
    check(savedNotes(fixture.root.path).size() == 2, "Delete request alone leaves SQLite rows intact");
    check(fixture.pointer(app::PointerKind::down, confirmationPoint(second, false)), "Actual anchored cancellation button is reachable");
    fixture.pointer(app::PointerKind::up, confirmationPoint(second, false));
    check(savedNotes(fixture.root.path).size() == 2, "Cancel confirmation preserves both notes");
    check(fixture.pointer(app::PointerKind::down, deletePoint(second)), "Delete can be requested again after cancellation");
    fixture.pointer(app::PointerKind::up, deletePoint(second));
    fixture.tick(.17);
    check(fixture.pointer(app::PointerKind::down, confirmationPoint(second, true)), "Actual anchored confirm button removes the intended row");
    fixture.pointer(app::PointerKind::up, confirmationPoint(second, true));
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].id == original.id && records[0].text == edited && records[0].isPinned,
        "Confirmed deletion preserves the sibling note and its original data");
    check(fixture.draw(secondCard) && fixture.draw(secondCard)->opacity > 0,
        "Removed row retains outgoing artwork until the original finite delete animation completes");
    fixture.tick(.22);
    check(!fixture.draw(secondCard) && fixture.composition.sceneCount() == 3,
        "Deletion completion safely removes only the outgoing scene from the shared publication");
    fixture.pointer(app::PointerKind::move, pinPoint(original), app::PointerButton::none);
    fixture.tick(.20);
    check(!fixture.preview->requiresFrames(fixture.time), "Settled hover and deletion leave no permanent animation demand");

    // Warm the maximum retained mask/constant capacities, then move the same
    // hovered control with the tilted plane. This catches repeated feedback
    // reset/UUID copying as well as per-frame composition replacement.
    core::Matrix4 finalWorkspace;
    auto movingFrame = [&](unsigned frame) {
        core::Matrix4 world;
        world.values[3] = double(frame + 1) * .0000002;
        world.values[7] = -double(frame + 1) * .0000001;
        world.values[12] = double(frame + 1) * .025;
        fixture.workspace = world;
        fixture.tick(1. / 60);
        const auto point = fixture.project(world, pinPoint(original));
        check(fixture.preview->pointer({app::PointerKind::move, app::PointerButton::none, point.x, point.y, 0}, fixture.time),
            "Steady hovered control follows shared tilted hit plane");
        fixture.frame();
        renderer.draw(false);
        finalWorkspace = world;
    };
    movingFrame(0);
    const auto rasterBefore = raster.stats();
    const auto gpuBefore = renderer.stats();
    const auto publicationBefore = fixture.publications;
    allocations = 0;
    counting = true;
    try { for (unsigned frame = 0; frame < 120; ++frame) movingFrame(frame); }
    catch (...) { counting = false; throw; }
    counting = false;
    check(allocations == 0, "120 complete steady tilt/hover/render frames allocate no C++ storage");
    const auto rasterAfter = raster.stats();
    const auto gpuAfter = renderer.stats();
    check(fixture.publications == publicationBefore && rasterAfter.rasterizations == rasterBefore.rasterizations &&
        rasterAfter.textLayoutsCreated == rasterBefore.textLayoutsCreated &&
        rasterAfter.textAnalysisFormatsCreated == rasterBefore.textAnalysisFormatsCreated &&
        gpuAfter.textureUploads == gpuBefore.textureUploads && gpuAfter.meshUploads == gpuBefore.meshUploads &&
        gpuAfter.objectBufferAllocations == gpuBefore.objectBufferAllocations &&
        gpuAfter.nativeGroupTargetAllocations == gpuBefore.nativeGroupTargetAllocations &&
        gpuAfter.nativeGroupRenders == gpuBefore.nativeGroupRenders,
        "Steady preview frames retain publication, text measurement, artwork, GPU buffers and popup target");
    const auto* finalCard = fixture.draw(sampleCard);
    const auto expectedWorld = finalWorkspace * core::Matrix4::translation(original.x, original.y);
    check(finalCard != nullptr, "Sibling source card remains retained after edit/create/delete interactions");
    for (std::size_t i = 0; i < 16; ++i) check(coordinatesNear(finalCard->world.values[i], expectedWorld.values[i]),
        "Card geometry follows the same workspace matrix used for input");
    records = savedNotes(fixture.root.path);
    check(records.size() == 1 && records[0].text == edited, "Tilt and hover frames never alter persisted text");
    check(!IsWindowVisible(hwnd), "End-to-end fixture never shows its owned HWND or requests real user input");
    fixture.release();
    check(raster.stats().entries == 0, "Teardown removes every preview-owned raster entry");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 5, "Pass HUD shader, prepared Notes bundle root, pinned manifest SHA and pinned source commit");
        const auto ascii = [](const wchar_t* value) {
            std::string result;
            for (; *value; ++value) {
                check(*value <= 127, "Fixture build pins are ASCII");
                result.push_back(static_cast<char>(*value));
            }
            return result;
        };
        Apartment apartment;
        Window window;
        const gpu::NativeNotesControlsAssets assets(std::filesystem::absolute(argv[2]), {ascii(argv[3]), ascii(argv[4])});
        gpu::Renderer renderer;
        renderer.initialize(window.hwnd, 1280, 800,
            {gpu::Driver::warpForTests, argv[1], gpu::RenderTarget::offscreenForTests});
        postedEditorFrames(window.hwnd, renderer, assets);
        reentrantClock(window.hwnd, renderer, assets);
        scrollPreview(window.hwnd, renderer, assets);
        run(window.hwnd, renderer, assets);
        renderer.reset();
        std::cout << "Native Notes preview integration: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        counting = false;
        std::cerr << "Native Notes preview integration failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
#endif
