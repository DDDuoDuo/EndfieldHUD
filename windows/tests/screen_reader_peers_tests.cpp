// Projected UI Automation peers of the local modules (native/
// screen_reader_peers) over an injected source: roles, patterns, presses,
// value writes, focus, projection, hit testing, identity across refreshes,
// group changes and teardown; then under the HUD window's actual root
// provider (app/hud_automation.cpp, unchanged host source compiled in)
// through a ModuleOwner that forwards its automation hooks exactly as the
// built-in Reader, Map, Calendar and minigame owners do. No UIA client is
// attached, no window is shown and no user data is touched.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "app/hud_automation.hpp"
#include "app/module_owner.hpp"
#include "tests/screen_reader_peer_probe.hpp"
#include <objbase.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace n = endfield::native;
namespace c = endfield::core;
namespace probe = endfield::tests::peers;
using Microsoft::WRL::ComPtr;
using Role = n::ScreenReaderRole;
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Apartment { HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); } };

// A minimal owner group (what app/hud_automation.hpp gives each owner).
class Group final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    std::atomic<ULONG> refs{1};
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { const ULONG v = --refs; if (!v) delete this; return v; }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* o) override { *o = ProviderOptions_ServerSideProvider; return S_OK; }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* o) override { VariantInit(o); return S_OK; }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection, IRawElementProviderFragment** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* o) override { *o = {}; return S_OK; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** o) override { *o = this; AddRef(); return S_OK; }
    IFACEMETHODIMP ElementProviderFromPoint(double, double, IRawElementProviderFragment** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** o) override { *o = nullptr; return S_OK; }
private:
    ~Group() = default;
};
bool same(IUnknown* a, IUnknown* b) {
    ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}

void projection() {
    const auto hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Synthetic peer projection", WS_POPUP, 30, 40, 200, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(hwnd && !IsWindowVisible(hwnd), "The projection fixture window stays hidden");
    POINT origin{0, 0};
    check(ClientToScreen(hwnd, &origin) != FALSE, "The fixture has a client origin");
    const auto r = n::screenReaderClientToScreen(hwnd, 1.5, c::Rect{10, 20, 30, 40});
    check(r && r->x == origin.x + 15 && r->y == origin.y + 30 && r->width == 45 && r->height == 60, "Logical client rects scale to physical pixels from the client origin");
    check(!n::screenReaderClientToScreen(hwnd, 0, c::Rect{0, 0, 1, 1}) && !n::screenReaderClientToScreen(hwnd, 1, std::nullopt) &&
          !n::screenReaderClientToScreen(nullptr, 1, c::Rect{0, 0, 1, 1}), "No window, scale or projection: no screen rect");
    DestroyWindow(hwnd);
}

void peers() {
    std::vector<n::ScreenReaderPeer> list{
        {"map:status", "Map", "Map. Drag to pan.", {}, {0, 0, 440, 440}, Role::text, true},
        {"map:zoomIn", "Zoom in", "Map. Drag to pan.", {}, {26, 184, 22, 22}, Role::button, true},
        {"map:zoomOut", "Zoom out", "Map. Drag to pan.", {}, {26, 212, 22, 22}, Role::button, false},
        {"field:title", "Title", {}, "Draft", {44, 126, 312, 30}, Role::edit, true, true},
        {"rules", "Rules", {}, "Move to aim.\nMerge matching levels.", {100, 300, 300, 100}, Role::group, true}};
    std::vector<std::string> invoked, texts, focused;
    unsigned changes{}, lists{};
    bool hidden{}, refuse{}, fail{};
    std::function<void()> duringPress;
    n::ScreenReaderPeerSource source;
    source.peers = [&] { ++lists; return list; };
    source.toScreen = [&](c::Rect r) -> std::optional<c::Rect> { if (hidden) return std::nullopt; return c::Rect{100 + r.x * 2, 50 + r.y * 2, r.width * 2, r.height * 2}; };
    source.invoke = [&](std::string_view id) {
        if (fail) throw std::runtime_error("synthetic module failure");
        invoked.emplace_back(id);
        if (duringPress) duringPress();
        return !refuse;
    };
    source.setText = [&](std::string_view id, std::string_view value) { texts.push_back(std::string(id) + "=" + std::string(value)); return !refuse; };
    source.focus = [&](std::string_view id) { focused.emplace_back(id); return !refuse; };
    source.changed = [&] { ++changes; };
    auto* group = new Group;
    auto owned = std::make_unique<n::NativeScreenReaderPeers>(std::move(source));
    auto& peers = *owned;
    check(peers.refresh(group) && peers.size() == 5 && peers.stats().created == 5 && group->refs == 2, "One fragment per peer under the held group");
    check(probe::ids(peers) == std::vector<std::wstring>{L"map:status", L"map:zoomIn", L"map:zoomOut", L"field:title", L"rules"}, "Peers keep the source order");

    // Roles and patterns.
    const auto status = probe::find(peers, "map:status"), zoomIn = probe::find(peers, "map:zoomIn"), zoomOut = probe::find(peers, "map:zoomOut");
    const auto title = probe::find(peers, "field:title"), rules = probe::find(peers, "rules");
    check(probe::integer(status, UIA_ControlTypePropertyId) == UIA_TextControlTypeId && probe::name(status) == L"Map" && probe::help(status) == L"Map. Drag to pan." &&
          probe::invoke(status) == E_NOINTERFACE && !probe::value(status) && !probe::boolean(status, UIA_IsKeyboardFocusablePropertyId),
          "The status is static text with name and help only");
    check(probe::integer(zoomIn, UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId && probe::boolean(zoomIn, UIA_IsEnabledPropertyId) &&
          !probe::boolean(zoomIn, UIA_IsKeyboardFocusablePropertyId) && !probe::value(zoomIn) && probe::text(zoomIn, UIA_FrameworkIdPropertyId) == L"EndfieldHUD",
          "Buttons invoke and are not tab stops");
    check(probe::integer(title, UIA_ControlTypePropertyId) == UIA_EditControlTypeId && probe::value(title) == std::optional<std::wstring>(L"Draft") &&
          probe::readOnly(title) == std::optional(false) && probe::boolean(title, UIA_IsKeyboardFocusablePropertyId) && probe::boolean(title, UIA_HasKeyboardFocusPropertyId) &&
          probe::invoke(title) == E_NOINTERFACE, "An editor is a focusable, writable Edit holding its text");
    check(probe::integer(rules, UIA_ControlTypePropertyId) == UIA_GroupControlTypeId && probe::value(rules) == std::optional<std::wstring>(L"Move to aim.\nMerge matching levels.") &&
          probe::readOnly(rules) == std::optional(true) && probe::setValue(rules, L"x") == UIA_E_INVALIDOPERATION && texts.empty(),
          "A group exposes its value read-only");

    // Presses.
    const auto listsBefore = lists;
    check(probe::invoke(zoomIn) == S_OK && invoked == std::vector<std::string>{"map:zoomIn"} && changes == 1 && lists == listsBefore + 1,
          "Invoke performs the action, reports the content event and re-lists the peers");
    check(!probe::boolean(zoomOut, UIA_IsEnabledPropertyId) && probe::invoke(zoomOut) == UIA_E_ELEMENTNOTENABLED && invoked.size() == 1 && changes == 1,
          "A disabled button refuses the press like its NSButton");
    refuse = true;
    check(probe::invoke(zoomIn) == UIA_E_INVALIDOPERATION && changes == 1 && probe::setValue(title, L"x") == UIA_E_INVALIDOPERATION && probe::setFocus(title) == UIA_E_INVALIDOPERATION,
          "A refused press, write or focus reports an invalid operation and no content event");
    refuse = false;
    fail = true;
    check(probe::invoke(zoomIn) == E_FAIL && changes == 1, "A failing module never throws across the COM boundary");
    fail = false;
    invoked.clear();
    texts.clear();
    focused.clear();

    // Editors.
    check(probe::setValue(title, L"Endmin \u7ba1\u7406\u5458") == S_OK && texts == std::vector<std::string>{"field:title=Endmin \xe7\xae\xa1\xe7\x90\x86\xe5\x91\x98"} && changes == 2,
          "Value writes reach the editor as UTF-8");
    const wchar_t lone[] = {0xD800, 0};
    check(probe::setValue(title, nullptr) == E_INVALIDARG && probe::setValue(title, lone) == E_INVALIDARG && texts.size() == 1, "Null and malformed UTF-16 values are refused unchanged");
    check(probe::setFocus(title) == S_OK && focused == std::vector<std::string>{"field:title"} && probe::setFocus(zoomIn) == S_OK && focused.size() == 1,
          "SetFocus moves the HUD's text focus to an editor; other peers are not focusable");

    // Projection and hit testing (topmost = last listed).
    UiaRect r = probe::bounds(zoomIn);
    check(r.left == 152 && r.top == 418 && r.width == 44 && r.height == 44 && !probe::boolean(zoomIn, UIA_IsOffscreenPropertyId), "Bounds are the projected screen rect");
    ComPtr<IRawElementProviderFragment> hit;
    check(SUCCEEDED(peers.fromPoint(160, 420, &hit)) && hit && probe::id(hit) == L"map:zoomIn", "The hit test prefers the button above the status");
    hit.Reset();
    check(SUCCEEDED(peers.fromPoint(120, 60, &hit)) && hit && probe::id(hit) == L"map:status", "The status covers the rest of the canvas");
    hit.Reset();
    check(SUCCEEDED(peers.fromPoint(5, 5, &hit)) && !hit, "Points outside every peer hit nothing");

    // Navigation within the owner's group.
    ComPtr<IRawElementProviderFragment> next, parent, previous;
    ComPtr<IRawElementProviderFragmentRoot> root;
    check(SUCCEEDED(zoomIn->Navigate(NavigateDirection_NextSibling, &next)) && next && probe::id(next) == L"map:zoomOut" &&
          SUCCEEDED(zoomIn->Navigate(NavigateDirection_PreviousSibling, &previous)) && previous && probe::id(previous) == L"map:status",
          "Siblings follow the listed order");
    check(SUCCEEDED(zoomIn->Navigate(NavigateDirection_Parent, &parent)) && same(parent.Get(), static_cast<IRawElementProviderFragment*>(group)) &&
          SUCCEEDED(zoomIn->get_FragmentRoot(&root)) && same(root.Get(), static_cast<IRawElementProviderFragmentRoot*>(group)),
          "The owner's group is every peer's parent and fragment root");
    parent.Reset(); root.Reset(); next.Reset(); previous.Reset();
    SAFEARRAY* a{};
    SAFEARRAY* b{};
    check(SUCCEEDED(zoomIn->GetRuntimeId(&a)) && SUCCEEDED(zoomOut->GetRuntimeId(&b)) && a && b, "Peers have runtime ids");
    LONG i1 = 1;
    int x{}, y{};
    SafeArrayGetElement(a, &i1, &x);
    SafeArrayGetElement(b, &i1, &y);
    SafeArrayDestroy(a);
    SafeArrayDestroy(b);
    check(x != y, "Runtime ids are distinct");

    // Content events: identity per id and role; changes update in place.
    list[1].label = "Zoom in more";
    list[3].value = "Draft 2";
    list[3].focused = false;
    list[2].enabled = true;
    check(!peers.refresh(group) && probe::find(peers, "map:zoomIn").Get() == zoomIn.Get() && probe::name(zoomIn) == L"Zoom in more" &&
          probe::value(title) == std::optional<std::wstring>(L"Draft 2") && !probe::boolean(title, UIA_HasKeyboardFocusPropertyId) &&
          probe::boolean(zoomOut, UIA_IsEnabledPropertyId), "An unchanged set keeps each fragment and refreshes its properties");
    // A press that opens a menu replaces the peers at once.
    duringPress = [&] { list = {{"menu:close", "Close", {}, {}, {200, 40, 23, 23}, Role::button, true}}; };
    const auto structure = peers.stats().structureChanges;
    check(probe::invoke(zoomIn) == S_OK && peers.size() == 1 && peers.stats().structureChanges == structure + 1 && probe::find(peers, "menu:close"),
          "A press that changes the controls re-lists them immediately");
    duringPress = {};
    check(probe::available(zoomIn) == UIA_E_ELEMENTNOTAVAILABLE && probe::invoke(zoomIn) == UIA_E_ELEMENTNOTAVAILABLE && probe::setValue(title, L"x") == UIA_E_ELEMENTNOTAVAILABLE,
          "Vanished peers are no longer available");

    // Unprojected peers.
    hidden = true;
    const auto close = probe::find(peers, "menu:close");
    r = probe::bounds(close);
    check(close && probe::boolean(close, UIA_IsOffscreenPropertyId) && r.width == 0 && r.height == 0, "A peer that does not project is offscreen with empty bounds");
    hit.Reset();
    check(SUCCEEDED(peers.fromPoint(500, 120, &hit)) && !hit, "An unprojected peer is never hit");
    hidden = false;

    // A new group (another root provider) starts a new set; the old group is released.
    auto* other = new Group;
    check(peers.refresh(other) && group->refs == 1 && other->refs == 2 && probe::available(close) == UIA_E_ELEMENTNOTAVAILABLE && peers.size() == 1 &&
          probe::find(peers, "menu:close").Get() != close.Get(), "A different group replaces every peer");
    // Hiding the HUD disconnects every peer; the group stays.
    const auto again = probe::find(peers, "menu:close");
    peers.disconnect();
    check(peers.size() == 0 && probe::available(again) == UIA_E_ELEMENTNOTAVAILABLE && other->refs == 2, "Disconnecting empties the list and keeps the group");
    list.clear();
    check(!peers.refresh(other) && peers.size() == 0, "An inactive module lists nothing");
    list = {{"map:zoomIn", "Zoom in", {}, {}, {26, 184, 22, 22}, Role::button, true}};
    check(peers.refresh(other) && peers.size() == 1, "Reactivation lists again");
    const auto retained = probe::find(peers, "map:zoomIn");
    const auto pressesBefore = invoked.size();
    owned.reset();
    check(other->refs == 1 && probe::available(retained) == UIA_E_ELEMENTNOTAVAILABLE && probe::invoke(retained) == UIA_E_ELEMENTNOTAVAILABLE && invoked.size() == pressesBefore,
          "After teardown a retained peer never reaches the module and the group is released");
    group->Release();
    other->Release();
}
// The built-in owners' automation hooks (app/application_modules.cpp):
// one NativeScreenReaderPeers member, five forwarding overrides.
class PeerOwner final : public endfield::app::ModuleOwner {
public:
    explicit PeerOwner(n::ScreenReaderPeerSource source) : peers_(std::move(source)) {}
    std::string_view name() const noexcept override { return "map"; }
    bool presents(c::Module m) const noexcept override { return m == c::Module::map; }
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
private:
    n::NativeScreenReaderPeers peers_;
};
class PlainOwner final : public endfield::app::ModuleOwner {
public:
    std::string_view name() const noexcept override { return "notes"; }
};

void root() {
    const auto window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Synthetic HUD root", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window && !IsWindowVisible(window), "The root fixture window stays hidden");
    std::vector<n::ScreenReaderPeer> list{
        {"map:status", "Map", "Map. Drag to pan.", {}, {0, 0, 440, 440}, Role::text, true},
        {"map:zoomIn", "Zoom in", "Map. Drag to pan.", {}, {26, 184, 22, 22}, Role::button, true},
        {"map:addPin", "Place pin at map center", "Map. Drag to pan.", {}, {392, 207, 22, 22}, Role::button, true}};
    std::vector<std::string> pressed;
    unsigned changes{};
    n::ScreenReaderPeerSource source;
    source.peers = [&] { return list; };
    source.toScreen = [](c::Rect r) -> std::optional<c::Rect> { return c::Rect{100 + r.x, 50 + r.y, r.width, r.height}; };
    source.invoke = [&](std::string_view id) {
        pressed.emplace_back(id);
        // Placing a pin adds its peer, as the Mac re-lays out its buttons.
        if (id == "map:addPin") list.push_back({"map:pin:00000000-0000-4000-8000-000000000001", "Yellow pin 1, 0.0 N, 0.0 E", "Map. Drag to pan.", {}, {214, 206, 12, 16}, Role::button, true});
        return true;
    };
    source.changed = [&] { ++changes; };
    PlainOwner plain;
    PeerOwner map(std::move(source));
    std::vector<endfield::app::ModuleOwner*> owners{&plain, &map};
    {
        endfield::app::HudAutomationSource hudSource;
        hudSource.owners = [&](std::vector<endfield::app::ModuleOwner*>& out) { out.insert(out.end(), owners.begin(), owners.end()); };
        hudSource.refresh = [](endfield::app::ModuleOwner& o, IRawElementProviderFragmentRoot* group) { return o.automationRefresh(group); };
        hudSource.name = [](const endfield::app::ModuleOwner& o) { return o.presents(c::Module::map) ? std::string("Map") : std::string(o.name()); };
        endfield::app::HudAutomation hud(window, std::move(hudSource));
        check(hud.refresh() && hud.groups() == 1 && map.automationCount() == 3, "The HUD root lists the Map group with its peers");
        ComPtr<IRawElementProviderFragment> rootFragment, group, first, parent, last, hit;
        ComPtr<IRawElementProviderFragmentRoot> fragmentRoot;
        check(SUCCEEDED(hud.root()->QueryInterface(IID_PPV_ARGS(&rootFragment))) && SUCCEEDED(rootFragment->Navigate(NavigateDirection_FirstChild, &group)) && group &&
              probe::name(group) == L"Map" && probe::id(group) == L"module.map", "The group is named after the module it presents");
        check(SUCCEEDED(group->Navigate(NavigateDirection_FirstChild, &first)) && probe::id(first) == L"map:status" && SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) &&
              same(parent.Get(), group.Get()) && SUCCEEDED(first->get_FragmentRoot(&fragmentRoot)) && same(fragmentRoot.Get(), group.Get()),
              "Peers are the group's children and name it as parent and fragment root");
        check(SUCCEEDED(group->Navigate(NavigateDirection_LastChild, &last)) && probe::id(last) == L"map:addPin", "The group's last child is the last listed peer");
        check(SUCCEEDED(hud.root()->ElementProviderFromPoint(100 + 392 + 5, 50 + 207 + 5, &hit)) && hit && probe::id(hit) == L"map:addPin",
              "The root's hit test reaches the projected button above the status");
        check(probe::invoke(hit) == S_OK && pressed == std::vector<std::string>{"map:addPin"} && changes == 1 && map.automationCount() == 4,
              "A press through the root performs the action and re-lists the peers at once");
        check(!hud.refresh() && map.automationCount() == 4, "The host's following refresh finds the peers already current");
        list.pop_back();
        check(hud.refresh() && map.automationCount() == 3, "A content event that removes a peer restructures the group");
        hud.disconnectOwners();
        check(map.automationCount() == 0 && hud.groups() == 0 && probe::available(first) == UIA_E_ELEMENTNOTAVAILABLE, "Concealing disconnects every peer");
        check(hud.refresh() && hud.groups() == 1 && map.automationCount() == 3, "Showing again lists fresh peers under the same group");
        hud.remove(map);
        check(map.automationCount() == 0 && hud.groups() == 0, "Removing the owner disconnects its peers");
        hud.shutdown();
    }
    DestroyWindow(window);
}
} // namespace

int main() {
    Apartment apartment;
    try {
        check(SUCCEEDED(apartment.hr), "Single-threaded apartment");
        projection();
        peers();
        root();
        std::cout << "PASS " << checks << " screen reader peer checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
