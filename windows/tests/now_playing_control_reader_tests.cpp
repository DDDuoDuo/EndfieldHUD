// The MSAA reader of a player's own controls (native/now_playing_player_
// controls.*) against this test's own hidden windows, which answer
// WM_GETOBJECT with in-process accessible objects shaped like a Chromium
// player page (an empty web document until an assistive client asks for its
// tree). The second page is NetEase Cloud Music 3.1's shape as read live
// (windows/tests/fixtures/now-playing-netease-tree.json): a hidden player
// window whose Chrome_RenderWidgetHostHWND child holds the page, built only
// after Chromium's screen-reader probe and a name read; its bar has the
// pre/play/next buttons, a progress bar whose "track" part is the played
// width and a cover whose source embeds the catalog address (IAccessible2
// object attributes, marshaled by Windows' own ia2comproxy.dll). Readings and
// presses run on the OS thread pool while this thread pumps messages. No
// other process, window or accessibility client is touched and nothing is
// shown.
#include "native/now_playing_player_controls.hpp"
#include <windows.h>
#include <oleacc.h>
#include <servprov.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
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

// IAccessible2's layout (no SDK header; Windows ships its proxy).
struct IA2LocaleValue {
    BSTR language;
    BSTR country;
    BSTR variant;
};
struct __declspec(uuid("E89F726E-C4F4-4C19-BB19-B647D7FA8478")) TestAccessible2 : public IAccessible {
    virtual HRESULT STDMETHODCALLTYPE get_nRelations(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relation(long, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relations(long, IUnknown**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE role(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollTo(long) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollToPoint(long, long, long) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_groupPosition(long*, long*, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_states(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedRole(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedRole(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_nExtendedStates(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedStates(long, BSTR**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedStates(long, BSTR**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_uniqueID(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_windowHandle(HWND*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_indexInParent(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_locale(IA2LocaleValue*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_attributes(BSTR*) = 0;
};
// A web page element: IAccessible with IAccessible2 object attributes and bounds.
class WebNode final : public TestAccessible2, public IServiceProvider {
public:
    WebNode(long role, std::string name, std::string description = {}, std::string attributes = {}, RECT bounds = {})
        : role_(role), name_(wide(name)), description_(wide(description)), attributes_(wide(attributes)), bounds_(bounds) {}
    std::vector<WebNode*> children;
    std::atomic<unsigned> presses{}, names{};
    void rename(std::string name) { std::lock_guard lock(mutex_); name_ = wide(name); }
    void describeAs(std::string description) { std::lock_guard lock(mutex_); description_ = wide(description); }
    void attribute(std::string attributes) { std::lock_guard lock(mutex_); attributes_ = wide(attributes); }
    void place(RECT bounds) { std::lock_guard lock(mutex_); bounds_ = bounds; }
    std::function<void()> onName; // the document's name read (Chromium's second probe condition)
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) *out = static_cast<IAccessible*>(static_cast<TestAccessible2*>(this));
        else if (riid == iidAccessible2) *out = static_cast<TestAccessible2*>(this);
        else if (riid == IID_IServiceProvider) *out = static_cast<IServiceProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs_; } // owned by the test's page
    // IDispatch
    IFACEMETHODIMP GetTypeInfoCount(UINT* count) override { if (count) *count = 0; return S_OK; }
    IFACEMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    IFACEMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    // IAccessible
    IFACEMETHODIMP get_accParent(IDispatch** out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accChildCount(long* out) override { if (!out) return E_POINTER; std::lock_guard lock(mutex_); *out = static_cast<long>(children.size()); return S_OK; }
    IFACEMETHODIMP get_accChild(VARIANT child, IDispatch** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        std::lock_guard lock(mutex_);
        if (child.vt != VT_I4 || child.lVal < 1 || static_cast<std::size_t>(child.lVal) > children.size()) return E_INVALIDARG;
        *out = static_cast<IAccessible*>(static_cast<TestAccessible2*>(children[static_cast<std::size_t>(child.lVal - 1)]));
        (*out)->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP get_accName(VARIANT child, BSTR* out) override {
        if (!self(child)) return E_INVALIDARG;
        ++names;
        if (onName) onName();
        std::lock_guard lock(mutex_);
        return string(name_, out);
    }
    IFACEMETHODIMP get_accValue(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accDescription(VARIANT child, BSTR* out) override { if (!self(child)) return E_INVALIDARG; std::lock_guard lock(mutex_); return string(description_, out); }
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
    IFACEMETHODIMP get_accDefaultAction(VARIANT, BSTR* out) override { return string(L"press", out); }
    IFACEMETHODIMP accSelect(long, VARIANT) override { return E_NOTIMPL; }
    IFACEMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
        if (!left || !top || !width || !height || !self(child)) return E_INVALIDARG;
        std::lock_guard lock(mutex_);
        *left = bounds_.left; *top = bounds_.top; *width = bounds_.right - bounds_.left; *height = bounds_.bottom - bounds_.top;
        return S_OK;
    }
    IFACEMETHODIMP accNavigate(long, VARIANT, VARIANT* out) override { if (out) VariantInit(out); return E_NOTIMPL; }
    IFACEMETHODIMP accHitTest(long, long, VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP accDoDefaultAction(VARIANT child) override { if (!self(child)) return E_INVALIDARG; ++presses; return S_OK; }
    IFACEMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    IFACEMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
    // IAccessible2: only its role and object attributes are answered.
    IFACEMETHODIMP get_nRelations(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_relation(long, IUnknown**) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_relations(long, IUnknown**, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP role(long* out) override { if (!out) return E_POINTER; *out = role_; return S_OK; }
    IFACEMETHODIMP scrollTo(long) override { return E_NOTIMPL; }
    IFACEMETHODIMP scrollToPoint(long, long, long) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_groupPosition(long*, long*, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_states(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_extendedRole(BSTR*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_localizedExtendedRole(BSTR*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_nExtendedStates(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_extendedStates(long, BSTR**, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_localizedExtendedStates(long, BSTR**, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_uniqueID(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_windowHandle(HWND*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_indexInParent(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_locale(IA2LocaleValue*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_attributes(BSTR* out) override { std::lock_guard lock(mutex_); return string(attributes_, out); }
    // IServiceProvider: IAccessible2 for this element.
    IFACEMETHODIMP QueryService(REFGUID, REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid != iidAccessible2) return E_NOINTERFACE;
        *out = static_cast<TestAccessible2*>(this);
        AddRef();
        return S_OK;
    }
private:
    static bool self(const VARIANT& child) { return child.vt == VT_I4 && child.lVal == CHILDID_SELF; }
    static HRESULT string(const std::wstring& value, BSTR* out) {
        if (!out) return E_POINTER;
        *out = value.empty() ? nullptr : SysAllocString(value.c_str());
        return value.empty() ? S_FALSE : S_OK;
    }
    long role_;
    std::wstring name_, description_, attributes_;
    RECT bounds_;
    std::mutex mutex_;
    std::atomic<ULONG> refs_{1};
};
// NetEase Cloud Music 3.1's page (fixture shape): the bar and, above it in
// the page, a playlist with its own play button and track length.
struct NeteasePage {
    WebNode document{ROLE_SYSTEM_DOCUMENT, "Page"}, body{ROLE_SYSTEM_GROUPING, ""}, playlist{ROLE_SYSTEM_GROUPING, ""},
        listPlay{ROLE_SYSTEM_GRAPHIC, "play", "\xe6\x92\xad\xe6\x94\xbe"}, listLength{ROLE_SYSTEM_STATICTEXT, "03:33"},
        footer{ROLE_SYSTEM_GROUPING, "", "", "display:block;tag:footer;class:should-hide-under-vinyl-mode;", {0, 1462, 2561, 1601}},
        miniBar{ROLE_SYSTEM_GROUPING, "", "", "id:page_pc_mini_bar;", {0, 1462, 2561, 1601}},
        row{ROLE_SYSTEM_GROUPING, "", "", "class:default-bar-wrapper;", {0, 1481, 2561, 1601}},
        side{ROLE_SYSTEM_GROUPING, "", "", "class:side;", {45, 1481, 323, 1601}},
        cover{ROLE_SYSTEM_GRAPHIC, "", "", "display:block;tag:img;src:orpheus\\://cache/?http\\://p3.music.126.net/Test\\=\\=/1.jpg?imageView\\=&thumbnail\\=60y60&type\\=webp;", {48, 1499, 132, 1583}},
        title{ROLE_SYSTEM_STATICTEXT, "Synthetic Song"}, artist{ROLE_SYSTEM_STATICTEXT, "Artist A"},
        pre{ROLE_SYSTEM_PUSHBUTTON, "pre"}, preIcon{ROLE_SYSTEM_GRAPHIC, "pre", "\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa6\x96\xef\xbc\x88""Ctrl + Left\xef\xbc\x89"},
        play{ROLE_SYSTEM_BUTTONMENU, "play"}, playIcon{ROLE_SYSTEM_GRAPHIC, "play", "\xe6\x92\xad\xe6\x94\xbe\xef\xbc\x88""Ctrl + P\xef\xbc\x89"},
        next{ROLE_SYSTEM_PUSHBUTTON, "next"}, nextIcon{ROLE_SYSTEM_GRAPHIC, "next", "\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa6\x96\xef\xbc\x88""Ctrl + Right\xef\xbc\x89"},
        progress{ROLE_SYSTEM_GROUPING, "\xe6\x92\xad\xe6\x94\xbe\xe8\xbf\x9b\xe5\xba\xa6\xe8\xb0\x83\xe8\x8a\x82", "", "class:slider-default;", {0, 1476, 2561, 1481}},
        hotzone{ROLE_SYSTEM_GROUPING, "", "", "class:hotzone-overlay;", {0, 1461, 2561, 1496}},
        cache{ROLE_SYSTEM_GROUPING, "", "", "class:cache;", {0, 1476, 398, 1481}},
        track{ROLE_SYSTEM_GROUPING, "", "", "class:track;", {0, 1476, 80, 1481}};
    std::atomic<bool> probed{}, named{}, built{};
    NeteasePage() {
        playlist.children = {&listPlay, &listLength};
        side.children = {&cover, &title, &artist};
        pre.children = {&preIcon};
        play.children = {&playIcon};
        next.children = {&nextIcon};
        row.children = {&side, &pre, &play, &next};
        progress.children = {&hotzone, &cache, &track};
        miniBar.children = {&progress, &row};
        footer.children = {&miniBar};
        body.children = {&playlist, &footer};
        // Chromium builds the page tree once its screen-reader probe was
        // queried and the document's name was read.
        document.onName = [this] { named = true; build(); };
    }
    void build() {
        if (probed && named && !built.exchange(true)) document.children = {&body};
    }
};
NeteasePage* netease{};
WebNode* hostRoot{}; // the views host's client (a pane above the document)
LRESULT CALLBACK pageProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_GETOBJECT && netease) {
        if (static_cast<DWORD>(l) == static_cast<DWORD>(OBJID_CLIENT)) {
            IAccessible* root = hostRoot ? static_cast<IAccessible*>(static_cast<TestAccessible2*>(hostRoot))
                                         : static_cast<IAccessible*>(static_cast<TestAccessible2*>(&netease->document));
            return LresultFromObject(IID_IAccessible, w, root);
        }
        if (l == 1) {
            netease->probed = true;
            netease->build();
            return 0;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

void cefPage() {
    NeteasePage shapes;
    netease = &shapes;
    WNDCLASSW host{}, page{};
    host.lpfnWndProc = DefWindowProcW;
    host.hInstance = page.hInstance = GetModuleHandleW(nullptr);
    host.lpszClassName = L"EndfieldHUD.NowPlayingReaderHost";
    page.lpfnWndProc = pageProcedure;
    page.lpszClassName = L"Chrome_RenderWidgetHostHWND";
    check(RegisterClassW(&host) != 0 && RegisterClassW(&page) != 0, "The synthetic player and page window classes register");
    const HWND window = CreateWindowExW(0, host.lpszClassName, L"Synthetic Song - Artist A", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, host.hInstance, nullptr);
    const HWND view = CreateWindowExW(0, page.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 200, 100, window, nullptr, page.hInstance, nullptr);
    check(window && view && !IsWindowVisible(window), "A hidden synthetic player window with its page child exists (never shown)");
    const auto handle = reinterpret_cast<std::uintptr_t>(window);
    const auto pid = GetCurrentProcessId();
    std::atomic<unsigned> notified{};
    double clock = 20;
    n::NowPlayingControlReader reader([&] { return clock; });
    reader.setNotify([&] { ++notified; });

    reader.reading(handle, pid, true);
    check(pump([&] { return notified.load() >= 1; }), "The first reading of the page completes");
    check(!reader.reading(handle, pid, false) && shapes.probed && shapes.named && shapes.built && reader.stats().activations == 1 && reader.awaiting(),
          "An empty page is probed the way Chromium detects screen readers, which builds its tree");
    check(reader.treeChanged(pid) && pump([&] { return notified.load() >= 2; }), "Its first event reads it again");
    auto reading = reader.reading(handle, pid, false);
    check(reading && reading->playPause && reading->previous && reading->next && reading->playing == false && !reading->position && !reading->duration,
          "The bar's pre/play/next buttons and the paused state; the playlist's play button and length never count");
    if (!reading || !reading->fraction)
        std::cout << "fraction: reading=" << bool(reading) << " details=" << reader.stats().details << " walks=" << reader.stats().walks << '\n';
    check(reading && reading->fraction && std::abs(*reading->fraction - 80. / 2561.) < 1e-9, "The played fraction is the track part's width over the bar's");
    check(reading && reading->artwork == "https://p3.music.126.net/Test==/1.jpg?param=600y600", "The cover is the catalog address its image source embeds");
    check(reading && reading->title == "Synthetic Song - Artist A" && !reader.awaiting(), "The bar shows the window title's track");
    check(reading && !reading->visible, "A hidden window is flagged (its bar may stop updating)");

    // The play button itself is pressed (its default action), never its icon.
    std::atomic<int> pressed{-1};
    check(reader.press(handle, pid, m::NowPlayingCommandKind::playPause, [&](bool ok) { pressed = ok ? 1 : 0; }), "The bar's play button can be pressed");
    check(pump([&] { return pressed.load() >= 0; }) && pressed == 1 && shapes.play.presses == 1 && shapes.playIcon.presses == 0 && shapes.listPlay.presses == 0,
          "Play/pause presses the bar's button, never the playlist's");
    pressed = -1;
    check(reader.press(handle, pid, m::NowPlayingCommandKind::previous, [&](bool ok) { pressed = ok ? 1 : 0; }) && pump([&] { return pressed.load() >= 0; }) &&
              shapes.pre.presses == 1 && shapes.preIcon.presses == 0, "Previous presses the button its icon's tooltip names");

    // Playing: the same button is renamed and the track part grows; the
    // found elements are re-read without a walk.
    shapes.play.rename("pause");
    shapes.track.place({0, 1476, 93, 1481});
    clock = 21;
    const auto walks = reader.stats().walks;
    reader.reading(handle, pid, true);
    check(pump([&] { return notified.load() >= 3; }), "The re-read completes");
    reading = reader.reading(handle, pid, false);
    check(reading && reading->playing == true && std::abs(*reading->fraction - 93. / 2561.) < 1e-9 && reader.stats().walks == walks && reader.stats().rereads >= 1,
          "The re-read states playing and the new fraction");

    // The window title moves on before the bar does: unconfirmed, awaited.
    SetWindowTextW(window, L"Next Song - Artist A");
    clock = 22;
    reader.reading(handle, pid, true);
    check(pump([&] { return notified.load() >= 4; }), "The reading after the title change completes");
    reading = reader.reading(handle, pid, false);
    check(reading && !reading->title && reader.awaiting(), "A bar still showing the previous track confirms nothing and is awaited");
    shapes.title.rename("Next Song");
    shapes.cover.attribute("tag:img;src:orpheus\\://cache/?http\\://p4.music.126.net/Next\\=\\=/2.jpg?thumbnail\\=60y60;");
    shapes.track.place({0, 1476, 0, 1481});
    check(reader.treeChanged(pid) && pump([&] { return notified.load() >= 5; }), "The page's next event reads it again");
    reading = reader.reading(handle, pid, false);
    check(reading && reading->title == "Next Song - Artist A" && reading->artwork == "https://p4.music.126.net/Next==/2.jpg?param=600y600" && reading->fraction == 0. &&
              !reader.awaiting(), "Then the bar confirms the new track, its cover and its start");

    // A confirmed bar is re-read through its progress bounds and play control
    // only (the once-a-second re-anchor); every eighth re-read reads all
    // found controls again, and a new window title does at once.
    {
        const auto before = reader.stats();
        for (unsigned k = 0; k < 8; ++k) {
            const auto count = notified.load();
            shapes.track.place({0, 1476, static_cast<LONG>(10 + k), 1481});
            reader.reading(handle, pid, true);
            check(pump([&] { return notified.load() > count; }), "A re-read of the confirmed bar completes");
            reading = reader.reading(handle, pid, false);
            check(reading && reading->fraction && std::abs(*reading->fraction - (10. + k) / 2561.) < 1e-9 && reading->title == "Next Song - Artist A" &&
                      reading->artwork == "https://p4.music.126.net/Next==/2.jpg?param=600y600" && reading->playPause && reading->previous && reading->next &&
                      reading->playing == true, "Each re-read states the bar's fraction and play state under the confirmed title, with its cover and controls");
        }
        const auto after = reader.stats();
        check(after.quick - before.quick == 7 && after.rereads - before.rereads == 8 && after.walks == before.walks && after.details - before.details == 3,
              "Seven re-reads ask only the bar's bounds and play control; the eighth reads all found controls again (attributes and cover)");
        const auto count = notified.load();
        SetWindowTextW(window, L"Synthetic Song - Artist A");
        reader.reading(handle, pid, true);
        check(pump([&] { return notified.load() > count; }), "The re-read after another title change completes");
        reading = reader.reading(handle, pid, false);
        check(reading && !reading->title && reader.stats().quick == after.quick, "A new window title is never confirmed by a quick re-read");
    }

    reader.shutdown();
    pump([] { return false; }, 0.5);
    DestroyWindow(view);
    DestroyWindow(window);
    UnregisterClassW(page.lpszClassName, page.hInstance);
    UnregisterClassW(host.lpszClassName, host.hInstance);
    netease = nullptr;
}

// Chromium may drop its render widget window (live: NetEase 3.1 had none
// after a while). The page document is then found a few levels into the
// player window's tree under its views host (Chrome_WidgetWin_0 > pane >
// document), and the screen-reader probe goes to that host.
void viewsHost() {
    NeteasePage shapes;
    WebNode pane{ROLE_SYSTEM_PANE, ""};
    pane.children = {&shapes.document};
    netease = &shapes;
    hostRoot = &pane;
    WNDCLASSW top{}, host{};
    top.lpfnWndProc = DefWindowProcW;
    top.hInstance = host.hInstance = GetModuleHandleW(nullptr);
    top.lpszClassName = L"EndfieldHUD.NowPlayingReaderViews";
    host.lpfnWndProc = pageProcedure;
    host.lpszClassName = L"Chrome_WidgetWin_0";
    check(RegisterClassW(&top) != 0 && RegisterClassW(&host) != 0, "The synthetic player and views host classes register");
    const HWND window = CreateWindowExW(0, top.lpszClassName, L"Synthetic Song - Artist A", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, top.hInstance, nullptr);
    const HWND view = CreateWindowExW(0, host.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 200, 100, window, nullptr, host.hInstance, nullptr);
    check(window && view && !IsWindowVisible(window), "A hidden synthetic player window with its views host exists (never shown)");
    const auto handle = reinterpret_cast<std::uintptr_t>(window);
    const auto pid = GetCurrentProcessId();
    std::atomic<unsigned> notified{};
    n::NowPlayingControlReader reader([] { return 30.; });
    reader.setNotify([&] { ++notified; });
    reader.reading(handle, pid, true);
    check(pump([&] { return notified.load() >= 1; }), "The first reading completes");
    check(shapes.probed && shapes.built && reader.stats().activations == 1, "The empty document under the views host is found and probed through the host");
    check(reader.treeChanged(pid) && pump([&] { return notified.load() >= 2; }), "Its first event reads it again");
    const auto reading = reader.reading(handle, pid, false);
    check(reading && reading->playPause && reading->previous && reading->next && reading->fraction && reading->artwork && reading->title,
          "The bar is read from the document found under the host");
    reader.shutdown();
    pump([] { return false; }, 0.5);
    DestroyWindow(view);
    DestroyWindow(window);
    UnregisterClassW(host.lpszClassName, host.hInstance);
    UnregisterClassW(top.lpszClassName, top.hInstance);
    hostRoot = nullptr;
    netease = nullptr;
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
    check(!reader.treeChanged(pid + 1) && reader.treeChanged(pid), "The page's first event reads it again");
    check(pump([&] { return notified.load() >= 2; }), "The second reading completes");
    check(!reader.awaiting() && !reader.treeChanged(pid), "A page whose controls were found is no longer awaited");
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
    check(reading && !reading->playPause && !reading->previous && !reading->next && reading->position == 84. && reader.stats().walks == 3,
          "A changed page is walked again: without its play/pause control it has no player bar (its clocks still read)");
    check(!reader.press(handle, pid, m::NowPlayingCommandKind::playPause, [](bool) {}) && !reader.press(handle, pid, m::NowPlayingCommandKind::next, [](bool) {}),
          "Controls the page no longer has as a bar are never pressed");

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
        cefPage();
        viewsHost();
        CoUninitialize();
        std::cout << "Now Playing control reader: " << checks << " checks passed (own hidden window, in-process accessible objects)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Now Playing control reader failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
