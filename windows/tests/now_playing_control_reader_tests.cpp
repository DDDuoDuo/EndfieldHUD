// The MSAA reader of a player's own controls (native/now_playing_player_
// controls.*) against this test's own hidden window, which answers
// WM_GETOBJECT with in-process accessible objects shaped like a Chromium
// player page (an empty web document until an assistive client asks for its
// tree). Readings and presses run on the OS thread pool while this thread
// pumps messages. No other process, window or accessibility client is touched
// and nothing is shown.
#include "native/now_playing_player_controls.hpp"
#include <windows.h>
#include <oleacc.h>
#include <servprov.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace n = endfield::native;
namespace m = endfield::modules;
namespace {
unsigned checks{};
void check(bool value, const char* what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
constexpr IID iidAccessible2{0xE89F726E, 0xC4F4, 0x4C19, {0xBB, 0x19, 0xB6, 0x47, 0xD7, 0xFA, 0x84, 0x78}};
std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

// A minimal accessible element (IAccessible over IDispatch, plus
// IServiceProvider for the web document).
class Node final : public IAccessible, public IServiceProvider {
public:
    Node(long role, std::string name) : role_(role), name_(wide(name)) {}
    std::vector<Node*> children;
    std::atomic<unsigned> presses{}, services{};
    std::function<void()> onService; // the "page tree" request
    void rename(std::string name) { name_ = wide(name); }
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) *out = static_cast<IAccessible*>(this);
        else if (riid == IID_IServiceProvider && role_ == ROLE_SYSTEM_DOCUMENT) *out = static_cast<IServiceProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs_; } // owned by the test's tree
    // IDispatch
    IFACEMETHODIMP GetTypeInfoCount(UINT* count) override { if (count) *count = 0; return S_OK; }
    IFACEMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    IFACEMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    // IAccessible
    IFACEMETHODIMP get_accParent(IDispatch** out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accChildCount(long* out) override { if (!out) return E_POINTER; *out = static_cast<long>(children.size()); return S_OK; }
    IFACEMETHODIMP get_accChild(VARIANT child, IDispatch** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (child.vt != VT_I4 || child.lVal < 1 || static_cast<std::size_t>(child.lVal) > children.size()) return E_INVALIDARG;
        *out = static_cast<IAccessible*>(children[static_cast<std::size_t>(child.lVal - 1)]);
        (*out)->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP get_accName(VARIANT child, BSTR* out) override { return self(child) ? string(name_, out) : E_INVALIDARG; }
    IFACEMETHODIMP get_accValue(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accDescription(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accRole(VARIANT child, VARIANT* out) override {
        if (!out || !self(child)) return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = role_;
        return S_OK;
    }
    IFACEMETHODIMP get_accState(VARIANT, VARIANT* out) override { if (out) { VariantInit(out); out->vt = VT_I4; out->lVal = 0; } return S_OK; }
    IFACEMETHODIMP get_accHelp(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accHelpTopic(BSTR*, VARIANT, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accFocus(VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP get_accSelection(VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP get_accDefaultAction(VARIANT, BSTR* out) override { return string(L"Press", out); }
    IFACEMETHODIMP accSelect(long, VARIANT) override { return E_NOTIMPL; }
    IFACEMETHODIMP accLocation(long*, long*, long*, long*, VARIANT) override { return E_NOTIMPL; }
    IFACEMETHODIMP accNavigate(long, VARIANT, VARIANT* out) override { if (out) VariantInit(out); return E_NOTIMPL; }
    IFACEMETHODIMP accHitTest(long, long, VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP accDoDefaultAction(VARIANT child) override {
        if (!self(child)) return E_INVALIDARG;
        ++presses;
        return S_OK;
    }
    IFACEMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    IFACEMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
    // IServiceProvider (the web document)
    IFACEMETHODIMP QueryService(REFGUID, REFIID riid, void** out) override {
        if (out) *out = nullptr;
        if (riid == iidAccessible2) {
            ++services;
            if (onService) onService();
        }
        return E_NOINTERFACE; // like a page whose IA2 proxy is not registered
    }
private:
    static bool self(const VARIANT& child) { return child.vt == VT_I4 && child.lVal == CHILDID_SELF; }
    static HRESULT string(const std::wstring& value, BSTR* out) {
        if (!out) return E_POINTER;
        *out = value.empty() ? nullptr : SysAllocString(value.c_str());
        return value.empty() ? S_FALSE : S_OK;
    }
    long role_;
    std::wstring name_;
    std::atomic<ULONG> refs_{1};
};

// The page: a client with a library group and the web document that gets
// its player bar only once its tree was requested.
struct Page {
    Node client{ROLE_SYSTEM_CLIENT, ""}, library{ROLE_SYSTEM_GROUPING, "Library"}, song{ROLE_SYSTEM_STATICTEXT, "Some Song"},
        document{ROLE_SYSTEM_DOCUMENT, ""}, bar{ROLE_SYSTEM_GROUPING, ""}, previous{ROLE_SYSTEM_PUSHBUTTON, "\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa6\x96"},
        toggle{ROLE_SYSTEM_PUSHBUTTON, "\xe6\x9a\x82\xe5\x81\x9c"}, next{ROLE_SYSTEM_PUSHBUTTON, "\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa6\x96"},
        elapsed{ROLE_SYSTEM_STATICTEXT, "01:23 /"}, total{ROLE_SYSTEM_STATICTEXT, "04:56"};
    Page() {
        client.children = {&library, &document};
        library.children = {&song};
        bar.children = {&previous, &toggle, &next, &elapsed, &total};
        document.onService = [this] { document.children = {&bar}; };
    }
};
Page* page{};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_GETOBJECT && static_cast<DWORD>(l) == static_cast<DWORD>(OBJID_CLIENT) && page)
        return LresultFromObject(IID_IAccessible, w, static_cast<IAccessible*>(&page->client));
    return DefWindowProcW(window, message, w, l);
}
// Pumps this thread's messages (the pool's calls into the page) until `done`.
template <class Done> bool pump(Done done, double seconds = 15) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return true;
}

void reader() {
    Page shapes;
    page = &shapes;
    WNDCLASSW type{};
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"EndfieldHUD.NowPlayingReaderTest";
    check(RegisterClassW(&type) != 0, "The test window class registers");
    const HWND window = CreateWindowExW(0, type.lpszClassName, L"Synthetic Player", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, type.hInstance, nullptr);
    check(window != nullptr && !IsWindowVisible(window), "A hidden synthetic player window exists (never shown)");
    const auto handle = reinterpret_cast<std::uintptr_t>(window);
    const auto pid = GetCurrentProcessId();
    std::atomic<unsigned> notified{};
    double clock = 10;
    n::NowPlayingControlReader reader([&] { return clock; });
    reader.setNotify([&] { ++notified; });

    check(!reader.reading(handle, pid, true), "No reading exists before the first one completes");
    check(pump([&] { return notified.load() >= 1; }), "The first reading completes on the pool");
    auto stats = reader.stats();
    check(!reader.reading(handle, pid, false) && stats.walks == 1 && stats.activations == 1 && shapes.document.services == 1 && reader.awaiting(),
          "An empty web document is asked for its tree once (as screen readers do); nothing is read yet");
    check(!reader.treeChanged(pid + 1) && reader.treeChanged(pid) && !reader.awaiting() && !reader.treeChanged(pid),
          "The page's first event reads it again, exactly once");
    check(pump([&] { return notified.load() >= 2; }), "The second reading completes");
    auto reading = reader.reading(handle, pid, false);
    check(reading && reading->position == 83. && reading->duration == 296. && reading->playing == true && reading->playPause && reading->previous &&
              reading->next && !reading->visible && reading->startedAt == 10 && reading->sampledAt == 10,
          "The player's elapsed and total labels, play state and controls (a hidden window is flagged)");
    check(reader.stats().walks == 2 && shapes.document.services == 1, "The tree is requested only once per process");

    // Later readings re-read the found elements (the source caches them).
    shapes.elapsed.rename("01:24 /");
    shapes.toggle.rename("\xe6\x92\xad\xe6\x94\xbe");
    clock = 11;
    check(reader.reading(handle, pid, true) == reading && pump([&] { return notified.load() >= 3; }), "A refresh returns the last reading and reads anew");
    reading = reader.reading(handle, pid, false);
    stats = reader.stats();
    if (!reading || reading->position != 84. || reading->playing != false || reading->sampledAt != 11 || stats.rereads != 1 || stats.walks != 2)
        std::cout << "reread: reading=" << bool(reading) << " position=" << (reading && reading->position ? *reading->position : -1)
                  << " playing=" << (reading && reading->playing ? int(*reading->playing) : -1) << " sampledAt=" << (reading ? reading->sampledAt : -1)
                  << " rereads=" << stats.rereads << " walks=" << stats.walks << " readings=" << stats.readings << '\n';
    check(reading && reading->position == 84. && reading->playing == false && reading->sampledAt == 11, "The found labels are read again");
    check(stats.rereads == 1 && stats.walks == 2, "Found elements are re-read without a walk");

    // Presses go to the player's own control.
    std::atomic<int> pressed{-1};
    check(reader.press(handle, pid, m::NowPlayingCommandKind::next, [&](bool ok) { pressed = ok ? 1 : 0; }), "A known control can be pressed");
    check(pump([&] { return pressed.load() >= 0; }) && pressed == 1 && shapes.next.presses == 1 && shapes.toggle.presses == 0,
          "Next presses exactly the player's next control (its default action)");
    check(!reader.press(handle, pid, m::NowPlayingCommandKind::seek, [](bool) {}) && !reader.press(handle + 1, pid, m::NowPlayingCommandKind::next, [](bool) {}),
          "No seek control; another window's controls are unknown");

    // The page changed: the cached elements no longer match, so it walks again.
    shapes.bar.children = {&shapes.previous, &shapes.next, &shapes.elapsed, &shapes.total};
    shapes.toggle.rename("Unrelated");
    clock = 12;
    reader.reading(handle, pid, true);
    check(pump([&] { return notified.load() >= 4; }), "The reading after a page change completes");
    reading = reader.reading(handle, pid, false);
    check(reading && !reading->playPause && reading->previous && reader.stats().walks == 3, "A changed page is walked again");
    check(!reader.press(handle, pid, m::NowPlayingCommandKind::playPause, [](bool) {}), "A control the page no longer has is never pressed");

    reader.retain(std::vector<std::uint32_t>{});
    check(pump([&] { return reader.stats().processes == 0; }) && !reader.reading(handle, pid, false), "A player that left is forgotten");
    reader.reading(handle, pid, true);
    check(pump([&] { return notified.load() >= 5; }), "It is read again when it returns");
    reader.shutdown();
    check(!reader.reading(handle, pid, true) && !reader.press(handle, pid, m::NowPlayingCommandKind::next, [](bool) {}), "A stopped reader reads and presses nothing");
    // Let the pool release the elements before the page goes away.
    pump([] { return false; }, 0.5);
    DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, type.hInstance);
    page = nullptr;
}
} // namespace

int main() {
    try {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) throw std::runtime_error("COM");
        reader();
        CoUninitialize();
        std::cout << "Now Playing control reader: " << checks << " checks passed (own hidden window, in-process accessible objects)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Now Playing control reader failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
