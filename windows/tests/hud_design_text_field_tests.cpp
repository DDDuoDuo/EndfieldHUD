// Contract suite of native/hud_design_text_field (LIBRARY-DESIGN.md section
// 10): the portable TextField editing through NativeDesignTextFields over
// NativeProjectedEditor. The caret blink (1.06 s, 80 ms ramps, solid after a
// move, only in a focused field of a foreground HUD, solid under Reduce
// Motion, frames only during ramps), the edit shortcuts, the secure field's
// clipboard and undo refusal, the right-click menu through a FAKE (no menu or
// window ever shows), the I-beam, Esc / Return rules, the published scenes'
// retirement, and input that arrives between an IME write and its owner
// notice. Hidden window, WARP, a fake TSF manager (never the real one), no
// input injection.
#include "native/hud_design_text_field.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace endfield;
using namespace native;
using Microsoft::WRL::ComPtr;
namespace d = modules::design;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
void ok(HRESULT hr, const char* s) { check(SUCCEEDED(hr), s); }
// A fake text-services manager (the real TSF is never activated). Its only
// job is to hand the test the editing field's text store, so the test can
// write as an IME does: inside the store's lock, with the owner notice left
// queued and undispatched.
struct FakeDocument final : ITfDocumentMgr {
    std::atomic<ULONG> refs{1};
    ComPtr<ITextStoreACP> store;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(ITfDocumentMgr)) return E_NOINTERFACE;
        *out = static_cast<ITfDocumentMgr*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto r = --refs; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE CreateContext(TfClientId, DWORD, IUnknown* owner, ITfContext** out, TfEditCookie* cookie) override {
        if (!out || !cookie) return E_POINTER;
        if (owner) (void)owner->QueryInterface(IID_PPV_ARGS(&store));
        *out = nullptr; *cookie = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Push(ITfContext*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Pop(DWORD) override { store.Reset(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTop(ITfContext**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetBase(ITfContext**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumContexts(IEnumTfContexts**) override { return E_NOTIMPL; }
};
struct FakeManager final : ITfThreadMgr, ITfKeystrokeMgr {
    std::atomic<ULONG> refs{1};
    std::vector<ComPtr<FakeDocument>> documents;
    ComPtr<ITfDocumentMgr> current;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(ITfThreadMgr)) *out = static_cast<ITfThreadMgr*>(this);
        else if (id == __uuidof(ITfKeystrokeMgr)) *out = static_cast<ITfKeystrokeMgr*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto r = --refs; if (!r) delete this; return r; }
    // The first document a field's editor creates holds its context and store.
    ITextStoreACP* store() const { return documents.empty() ? nullptr : documents.front()->store.Get(); }
    HRESULT STDMETHODCALLTYPE Activate(TfClientId*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Deactivate() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateDocumentMgr(ITfDocumentMgr** out) override {
        if (!out) return E_POINTER;
        ComPtr<FakeDocument> document; document.Attach(new FakeDocument);
        *out = document.Get(); document->AddRef(); documents.push_back(std::move(document)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFocus(ITfDocumentMgr** out) override { if (!out) return E_POINTER; *out = current.Get(); if (*out) (*out)->AddRef(); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetFocus(ITfDocumentMgr* value) override { if (!value) return E_INVALIDARG; current = value; return S_OK; }
    HRESULT STDMETHODCALLTYPE AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId, ITfKeyEventSink*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForeground(CLSID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }
};
// Grants the test's write lock (the IME's edit session).
struct Sink final : ITextStoreACPSink {
    std::atomic<ULONG> refs{1};
    std::function<void()> onLock;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(ITextStoreACPSink)) return E_NOINTERFACE;
        *out = static_cast<ITextStoreACPSink*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto r = --refs; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD, const TS_TEXTCHANGE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSelectionChange() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode, TsViewCookie) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG, LONG, ULONG, const TS_ATTRID*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD) override { if (onLock) onLock(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction() override { return S_OK; }
};
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldDesignFieldHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 590, 510, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldDesignFieldHidden", GetModuleHandleW(nullptr)); }
};
d::KeyInput key(std::uint32_t vk, bool control = false, bool shift = false) {
    d::KeyInput k; k.virtualKey = vk; k.control = control; k.shift = shift;
    if (vk >= 'A' && vk <= 'Z') k.character = char32_t(vk - 'A' + 'a');
    return k;
}
// The caret surface's opacity as drawn (the editor's fourth retained leaf).
float caret(NativeDesignTextFields& host) {
    const auto entries = host.entries();
    check(!entries.empty() && entries[0].scene, "The editing field publishes its editor scene");
    const auto draws = entries[0].scene->prepareDraws();
    check(draws.size() == 4, "Selection, glyphs, composition and caret");
    return draws[3].opacity;
}
void run(const std::filesystem::path& shader) {
    Window window;
    Renderer renderer;
    renderer.initialize(window.hwnd, 590, 510, {Driver::warpForTests, shader, RenderTarget::offscreenForTests});
    LayerRasterizer raster;
    std::optional<std::u16string> clipboard;
    unsigned writes{};
    std::vector<std::array<bool, 3>> menus;
    std::optional<d::EditCommand> menuAnswer;
    NativeDesignTextFieldOptions options;
    options.window = window.hwnd;
    options.ownerMessage = WM_APP + 0x44;
    options.raster.pixelsPerPoint = 1;
    options.clipboard.read = [&] { return clipboard; };
    options.clipboard.write = [&](std::u16string_view v) { clipboard = std::u16string(v); ++writes; return true; };
    options.editMenu = [&](core::Point, bool cut, bool copy, bool paste) { menus.push_back({cut, copy, paste}); return menuAnswer; };
    NativeDesignTextFields host(raster, options);
    // The IME section's host, over a fake text-services manager. Like `host`
    // it outlives the fields, which end their editing on destruction.
    ComPtr<FakeManager> manager; manager.Attach(new FakeManager);
    auto imeOptions = options;
    imeOptions.threadManager = manager.Get();
    imeOptions.clientID = 7;
    imeOptions.ownerMessage = WM_APP + 0x45;
    NativeDesignTextFields ime(raster, imeOptions);
    d::Environment env;
    env.text.uppercase = d::asciiUppercase;
    d::LayerArena arena("f", &env.deadlines);
    d::ControlSet set;
    d::Builder builder{env, arena, set};
    auto& field = builder.make<d::TextField>("field", "Name", "hello", "Name", d::Size{200, 30});
    field.setEditorHost(&host);
    std::vector<std::string> changes;
    field.onChange = [&](const std::string& t) { changes.push_back(t); };
    unsigned cancels{};
    field.onCancel = [&] { ++cancels; };
    const auto camera = layerViewportProjection(590, 510);
    double time{};
    const auto pose = [&](double t, bool foreground = true, bool reduce = false) {
        time = t;
        host.setPose({core::Matrix4{}, camera, 590, 510, 1, foreground, t, reduce});
        (void)host.upload(renderer);
    };
    // --- begin editing; the caret blink -------------------------------------------
    field.beginEditing();
    check(field.editing() && host.editing() && host.field() == &field, "The field attaches the native editor");
    pose(0);
    check(caret(host) == 1, "The caret starts solid");
    check(!host.requiresFrames(.2) && host.nextWakeTime(.2) && std::abs(*host.nextWakeTime(.2) - .53) < 1e-9, "Between ramps the owner sleeps until the fade (.5 x 1.06 s)");
    check(host.requiresFrames(.55), "Frames only during the 80 ms fade");
    pose(.53 + .04);
    const auto fading = caret(host);
    check(fading > 0 && fading < 1, "The caret fades over 80 ms");
    pose(.8);
    check(caret(host) == 0 && !host.requiresFrames(.8), "Hidden half of the cycle, no frames");
    // Typing moves the caret: solid again, the cycle restarts.
    check(host.character('!', false), "Typing reaches the editor");
    check(!changes.empty() && changes.back() == "hello!", "Each edit reaches the field (bounded, onChange)");
    pose(.81);
    check(caret(host) == 1 && host.nextWakeTime(.81) && std::abs(*host.nextWakeTime(.81) - (.80 + .53)) < .02, "Solid after every move");
    // Only in a focused field of the foreground HUD; solid under Reduce Motion.
    pose(.9, false);
    check(caret(host) == 0 && !host.requiresFrames(.9) && !host.nextWakeTime(.9), "No caret and no wakes while the HUD is not foreground");
    pose(1, true, true);
    check(caret(host) == 1 && !host.requiresFrames(1.5) && !host.nextWakeTime(1.5), "Reduce Motion keeps it solid with no wakes");
    pose(1.6);
    // --- shortcuts and the clipboard ----------------------------------------------------
    check(host.key(key('A', true)) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{0, 6}, "Ctrl+A selects all");
    check(host.key(key('C', true)) && clipboard == u"hello!", "Ctrl+C copies the selection");
    check(host.key(key('X', true)) && host.text(field).empty() && clipboard == u"hello!", "Ctrl+X cuts it");
    check(host.key(key('V', true)) && host.text(field) == "hello!", "Ctrl+V pastes");
    check(host.key(key('Z', true)) && host.text(field).empty(), "Ctrl+Z undoes");
    check(host.key(key('Y', true)) && host.text(field) == "hello!", "Ctrl+Y redoes");
    check(host.key(key('Z', true)) && host.key(key('Z', true, true)) && host.text(field) == "hello!", "Ctrl+Shift+Z redoes too");
    check(host.key(key(d::key::left, false, true)) && host.selection(field)->second == 1, "Shift+arrow extends (NSRange location, length)");
    const auto item = host.accessibilityItem();
    check(item && item->role == d::AccessibilityRole::edit && item->focused && item->value == "hello!", "The editing field stays an accessible, focused edit");
    // --- the I-beam and the right-click menu (fake) -------------------------------------
    check(host.textCursorAt({100, 15}) && !host.textCursorAt({300, 200}), "The I-beam only over the open field");
    host.key(key('A', true));
    menuAnswer.reset();
    check(host.rightPointerDown({100, 15}, {100, 15}) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{0, 6}, "A right-click inside the selection keeps it");
    check(menus.size() == 1 && menus[0][0] && menus[0][1] && menus[0][2], "Cut, Copy and Paste are offered for a selection with a clipboard text");
    // HUDProjectedTextEditor.rightMouseDown: outside the selection the caret
    // moves to the click (behind the text's end here) before the menu opens.
    host.key(key(d::key::left));
    check(host.key(key(d::key::right, false, true)) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{0, 1}, "The first character is selected");
    check(host.rightPointerDown({150, 15}, {150, 15}) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{6, 0}, "A right-click outside the selection moves the caret there");
    check(menus.size() == 2 && !menus[1][0] && !menus[1][1] && menus[1][2], "The menu then offers no Cut or Copy");
    host.key(key('A', true));
    clipboard = u"X";
    menuAnswer = d::EditCommand::paste;
    check(host.rightPointerDown({100, 15}, {100, 15}) && host.text(field) == "X", "The menu's Paste replaces the selection");
    check(!host.rightPointerDown({400, 300}, {400, 300}), "A right-click beside the field is not the field's");
    // --- Return commits; Esc restores --------------------------------------------------------
    check(host.key(key(d::key::enter)) && !field.editing() && !host.editing() && field.text() == "X", "Return commits a single-line field");
    pose(2);
    (void)host.upload(renderer);
    check(host.entries().empty(), "An ended editor leaves the composition");
    check(host.collectRetired(renderer), "Its scenes release after the publication");
    field.beginEditing();
    pose(2.1);
    host.character('Y', false);
    check(host.key(key(d::key::escape)) && field.text() == "X" && cancels == 1, "Esc restores the original text and cancels");
    // --- a secure field never reaches the clipboard ----------------------------------------
    d::TextField::Options secureOptions; secureOptions.secure = true;
    auto& secret = builder.make<d::TextField>("secret", "Password", "", "Password", d::Size{200, 30}, secureOptions);
    secret.setEditorHost(&host);
    secret.beginEditing();
    pose(2.2);
    for (const char c : std::string("abc")) host.character(std::uint32_t(c), false);
    const auto written = writes;
    host.key(key('A', true));
    check(host.key(key('C', true)) && host.key(key('X', true)) && writes == written && host.text(secret) == "abc", "Cut and copy are refused in a secure field");
    menus.clear(); menuAnswer.reset();
    host.rightPointerDown({100, 15}, {100, 15});
    check(menus.size() == 1 && !menus[0][0] && !menus[0][1], "The menu offers no Cut or Copy for a secure field");
    host.key(key(d::key::right));
    host.character('d', false);
    check(host.key(key('Z', true)) && host.text(secret) == "abcd", "A secure field keeps no undo");
    secret.endEditing(true);
    pose(2.3);
    (void)host.upload(renderer);
    check(host.collectRetired(renderer) && host.releaseResources(renderer), "Everything releases");
    // --- input between an IME write and its owner notice ----------------------------------
    // The IME writes inside the store's lock; the field lays the text out only
    // when the queued owner notice is dispatched (never here). A key, wheel,
    // click or hover that arrives first lays out the new text itself.
    d::TextField::Options multiline; multiline.multiline = true;
    auto& notes = builder.make<d::TextField>("notes", "Notes", "line", "Notes", d::Size{200, 60}, multiline);
    notes.setEditorHost(&ime);
    const auto imePose = [&](double t) {
        time = t;
        ime.setPose({core::Matrix4{}, camera, 590, 510, 1, true, t, false});
        (void)ime.upload(renderer);
    };
    notes.beginEditing();
    imePose(3);
    auto* store = manager->store();
    check(ime.editing() && store, "The multi-line field connects to the fake text services");
    ComPtr<Sink> sink; sink.Attach(new Sink);
    ok(store->AdviseSink(__uuidof(ITextStoreACPSink), sink.Get(), TS_AS_ALL_SINKS), "Advise the IME's sink");
    const auto imeAppend = [&](std::wstring_view text) {
        sink->onLock = [&] {
            TS_TEXTCHANGE delta{};
            LONG end{};
            ok(store->GetEndACP(&end), "The IME reads the document end");
            ok(store->SetText(0, end, end, text.data(), static_cast<ULONG>(text.size()), &delta), "The IME appends text; its owner notice stays queued");
        };
        HRESULT session{};
        ok(store->RequestLock(TS_LF_READWRITE, &session), "Grant the IME write lock");
        ok(session, "The IME write lock completes");
        sink->onLock = {};
    };
    imeAppend(L"\u4e2d");
    check(ime.key(key('Z', true)) && ime.text(notes) == "line", "Ctrl+Z before the owner notice lays out the IME text, then undoes it");
    // The append leaves the caret at the end, so the new layout reveals the
    // last line; a wheel up then moves from there, not from the stale top
    // ("line", units 0 to 4), and a click near the top lands past line 2.
    imeAppend(L"\n2\n3\n4\n5\n6\n7\n8");
    check(ime.wheel({100, 30}, -20), "The wheel before the owner notice reaches the field");
    check(ime.pointerDown({100, 14}, false), "A click after the wheel reaches the field");
    ime.pointerUp();
    const auto wheeled = ime.selection(notes);
    check(wheeled && wheeled->second == 0 && wheeled->first > 6, "The wheel scrolls up from the revealed new lines, not from the stale offset");
    imeAppend(L"\u6587");
    const auto atEnd = ime.selection(notes);
    check(ime.pointerDown({100, 14}, false), "A click before the owner notice reaches the field");
    ime.pointerUp();
    const auto clicked = ime.selection(notes);
    check(atEnd && clicked && clicked->second == 0 && clicked->first < atEnd->first, "The click moves the caret back from the end of the newly laid-out text");
    imeAppend(L"\u5b57");
    const auto before = ime.text(notes);
    const auto selected = ime.selection(notes);
    ime.pointerDrag({100, 40});
    check(ime.selection(notes) == selected && ime.text(notes) == before, "A hover before the owner notice neither throws nor selects");
    // A drag whose press came before the IME write: the write moves the caret
    // to the end, and the drag lays out the new text and selects back from it.
    check(ime.pointerDown({100, 40}, false), "A press starts a drag");
    imeAppend(L"\u53e5");
    ime.pointerDrag({100, 14});
    ime.pointerUp();
    const auto dragged = ime.selection(notes);
    check(dragged && dragged->second > 0 && dragged->first + dragged->second == 21, "A drag before the owner notice selects through the newly laid-out text");
    ok(store->UnadviseSink(sink.Get()), "Detach the IME's sink");
    notes.endEditing(true);
    imePose(3.1);
    (void)ime.upload(renderer);
    check(ime.collectRetired(renderer) && ime.releaseResources(renderer), "The IME field releases everything");
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "No window or menu was shown");
}
}
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the shader");
        run(argv[1]);
        CoUninitialize();
        std::cout << "HUD design text field: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "HUD design text field after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
