// UI Automation of the Settings pages (tools/settings_automation.hpp,
// native/settings_automation.hpp) over the real Settings owner on a hidden
// window and WARP: every page lists its controls with their UIA control
// types, localized names, patterns and states in the page's visual order and
// at the projected rects; presses, toggles, selections and slider values
// through UIA change the preferences exactly like the clicks; values follow
// changes made by other input; the confirmation card; nothing while Settings
// is hidden. Then the same fragments under the HUD window's actual root
// provider (app/hud_automation.cpp, unchanged host source compiled in)
// through a ModuleOwner that forwards the hooks as the built-in Settings
// adapter does. No UIA client is attached, no window is shown, no input is
// injected and no user data is touched (every callback is injected).
#ifdef _WIN32
#include "tools/settings_automation.hpp"
#include "tools/settings_preview.hpp"
#include "app/hud_automation.hpp"
#include "app/module_owner.hpp"
#include "native/module_scene.hpp"
#include "core/module_presentation.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace c = endfield::core;
namespace gpu = endfield::native;
namespace a = endfield::app;
namespace t = endfield::tools;
namespace m = endfield::modules;
using Json = ehud::data::Json;
using Microsoft::WRL::ComPtr;
using Fragment = ComPtr<IRawElementProviderFragment>;
using Node = gpu::SettingsAutomationNode;
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
void check(bool v, const std::string& s) { ++checks; if (!v) throw std::runtime_error(s); }
constexpr PROPERTYID headingLevelProperty = 30173, isDialogProperty = 30174;

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

// --- Provider probes, read exactly as a UIA client reads them ----------------
std::string narrow(const wchar_t* w, std::size_t n) {
    if (!w || !n) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), size, nullptr, nullptr);
    return s;
}
HRESULT property(const Fragment& f, PROPERTYID id, VARIANT& v) {
    VariantInit(&v);
    ComPtr<IRawElementProviderSimple> simple;
    if (!f || FAILED(f.As(&simple))) return E_NOINTERFACE;
    return simple->GetPropertyValue(id, &v);
}
std::string text(const Fragment& f, PROPERTYID id) {
    VARIANT v;
    std::string out;
    if (SUCCEEDED(property(f, id, v)) && v.vt == VT_BSTR && v.bstrVal) out = narrow(v.bstrVal, SysStringLen(v.bstrVal));
    VariantClear(&v);
    return out;
}
long integer(const Fragment& f, PROPERTYID id) {
    VARIANT v;
    long out = -1;
    if (SUCCEEDED(property(f, id, v)) && v.vt == VT_I4) out = v.lVal;
    VariantClear(&v);
    return out;
}
bool boolean(const Fragment& f, PROPERTYID id) {
    VARIANT v;
    bool out = false;
    if (SUCCEEDED(property(f, id, v)) && v.vt == VT_BOOL) out = v.boolVal == VARIANT_TRUE;
    VariantClear(&v);
    return out;
}
HRESULT available(const Fragment& f) { VARIANT v; const auto hr = property(f, UIA_NamePropertyId, v); VariantClear(&v); return hr; }
std::string id(const Fragment& f) { return text(f, UIA_AutomationIdPropertyId); }
std::string name(const Fragment& f) { return text(f, UIA_NamePropertyId); }
long type(const Fragment& f) { return integer(f, UIA_ControlTypePropertyId); }
template <class P> ComPtr<P> pattern(const Fragment& f, PATTERNID which) {
    ComPtr<IRawElementProviderSimple> simple;
    ComPtr<IUnknown> unknown;
    ComPtr<P> out;
    if (!f || FAILED(f.As(&simple)) || FAILED(simple->GetPatternProvider(which, &unknown)) || !unknown || FAILED(unknown.As(&out))) return nullptr;
    return out;
}
std::optional<std::string> value(const Fragment& f) {
    const auto p = pattern<IValueProvider>(f, UIA_ValuePatternId);
    BSTR v{};
    if (!p || FAILED(p->get_Value(&v))) return std::nullopt;
    auto out = narrow(v, v ? SysStringLen(v) : 0);
    SysFreeString(v);
    return out;
}
std::optional<ToggleState> toggled(const Fragment& f) {
    const auto p = pattern<IToggleProvider>(f, UIA_TogglePatternId);
    ToggleState s{};
    if (!p || FAILED(p->get_ToggleState(&s))) return std::nullopt;
    return s;
}
std::optional<bool> selected(const Fragment& f) {
    const auto p = pattern<ISelectionItemProvider>(f, UIA_SelectionItemPatternId);
    BOOL s{};
    if (!p || FAILED(p->get_IsSelected(&s))) return std::nullopt;
    return s != FALSE;
}
std::optional<double> range(const Fragment& f) {
    const auto p = pattern<IRangeValueProvider>(f, UIA_RangeValuePatternId);
    double v{};
    if (!p || FAILED(p->get_Value(&v))) return std::nullopt;
    return v;
}
HRESULT invoke(const Fragment& f) { const auto p = pattern<IInvokeProvider>(f, UIA_InvokePatternId); return p ? p->Invoke() : E_NOINTERFACE; }
HRESULT toggle(const Fragment& f) { const auto p = pattern<IToggleProvider>(f, UIA_TogglePatternId); return p ? p->Toggle() : E_NOINTERFACE; }
HRESULT choose(const Fragment& f) { const auto p = pattern<ISelectionItemProvider>(f, UIA_SelectionItemPatternId); return p ? p->Select() : E_NOINTERFACE; }
HRESULT setRange(const Fragment& f, double v) { const auto p = pattern<IRangeValueProvider>(f, UIA_RangeValuePatternId); return p ? p->SetValue(v) : E_NOINTERFACE; }
UiaRect bounds(const Fragment& f) { UiaRect r{}; if (f) (void)f->get_BoundingRectangle(&r); return r; }
bool same(IUnknown* x, IUnknown* y) {
    ComPtr<IUnknown> p, q;
    return x && y && SUCCEEDED(x->QueryInterface(IID_PPV_ARGS(&p))) && SUCCEEDED(y->QueryInterface(IID_PPV_ARGS(&q))) && p.Get() == q.Get();
}
void descend(const Fragment& f, std::vector<Fragment>& out) {
    out.push_back(f);
    Fragment child;
    if (FAILED(f->Navigate(NavigateDirection_FirstChild, &child))) return;
    while (child) {
        descend(child, out);
        Fragment next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, &next))) break;
        child = next;
    }
}
// Pre-order over the owner's fragments (top level through child(i), the rest by Navigate).
std::vector<Fragment> walk(const t::SettingsAutomation& b) {
    std::vector<Fragment> out;
    for (std::size_t i = 0; i < b.count(); ++i) { Fragment f; if (SUCCEEDED(b.child(i, &f)) && f) descend(f, out); }
    return out;
}
std::vector<std::string> topIds(const t::SettingsAutomation& b) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < b.count(); ++i) { Fragment f; if (SUCCEEDED(b.child(i, &f)) && f) out.push_back(id(f)); }
    return out;
}
Fragment find(const t::SettingsAutomation& b, std::string_view key) {
    for (const auto& f : walk(b)) if (id(f) == key) return f;
    return nullptr;
}
std::vector<Fragment> children(const Fragment& f) {
    std::vector<Fragment> out;
    Fragment child;
    if (!f || FAILED(f->Navigate(NavigateDirection_FirstChild, &child))) return out;
    while (child) { out.push_back(child); Fragment next; if (FAILED(child->Navigate(NavigateDirection_NextSibling, &next))) break; child = next; }
    return out;
}
std::string join(const std::vector<std::string>& v) { std::string s; for (const auto& x : v) s += (s.empty() ? "" : ",") + x; return s; }

// A ModuleOwner forwarding the hooks exactly as the built-in Settings adapter.
class SettingsOwner final : public a::ModuleOwner {
public:
    SettingsOwner(t::SettingsPreview& p, t::SettingsAutomation& b) : preview(p), binding(b) {}
    t::SettingsPreview& preview;
    t::SettingsAutomation& binding;
    std::string_view name() const noexcept override { return "settings"; }
    bool presents(c::Module m) const noexcept override { return m == c::Module::system || m == c::Module::display || m == c::Module::hotkeys || m == c::Module::about; }
    bool capturesPointer() const override { return preview.modalActive() || preview.pointerLocked(); }
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return binding.refresh(root); }
    std::size_t automationCount() const override { return binding.count(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return binding.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return binding.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { binding.disconnect(); }
    std::optional<c::Module> automationModule() const noexcept override { return preview.inputModule(); }
};

void run(const std::filesystem::path& shader) {
    constexpr double scale = 1.5;
    constexpr std::uint32_t pixelWidth = 1920, pixelHeight = 1200;
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"EndfieldSettingsAutomationHidden";
    check(RegisterClassW(&wc) != 0, "Register the owned hidden fixture class");
    const HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, pixelWidth, pixelHeight, nullptr, nullptr, wc.hInstance, nullptr);
    check(hwnd && !IsWindowVisible(hwnd), "The fixture window stays hidden");
    struct WindowCleanup { HWND h; ~WindowCleanup() { DestroyWindow(h); UnregisterClassW(L"EndfieldSettingsAutomationHidden", GetModuleHandleW(nullptr)); } } windowCleanup{hwnd};
    POINT origin{0, 0};
    check(ClientToScreen(hwnd, &origin) != FALSE, "The fixture has a client origin");

    gpu::Renderer renderer;
    renderer.initialize(hwnd, pixelWidth, pixelHeight, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    const auto camera = gpu::layerViewportProjection(pixelWidth, pixelHeight) * c::Matrix4::scale(scale, scale);
    renderer.setCamera(camera);
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    std::vector<ehud::data::Settings> saves;
    std::vector<bool> captures, automatic;
    std::vector<std::string> links;
    unsigned registrations{}, updateChecks{}, colours{}, imports{}, changes{};
    t::SettingsPreviewOptions options;
    options.raster.pixelsPerPoint = 1;
    options.initial.set("launchAtLogin", false);
    options.callbacks.persist = [&](const auto& s) { saves.push_back(s); };
    options.callbacks.shortcutCapture = [&](bool b) { captures.push_back(b); };
    options.callbacks.registerShortcut = [&](auto) -> std::optional<std::string> { ++registrations; return {}; };
    options.callbacks.launchAtLogin = [](bool) -> std::optional<std::string> { return {}; };
    options.callbacks.checkUpdates = [&] { ++updateChecks; };
    options.callbacks.automaticUpdates = [&](bool b) { automatic.push_back(b); };
    options.callbacks.openLink = [&](std::string_view url) { links.emplace_back(url); };
    options.viewHooks.displays = [] { return std::vector<m::SettingsDisplay>{{"fixture-display", "Synthetic Display", "1920 × 1080"}}; };
    options.viewHooks.macImportTitle = [](c::Language) -> std::optional<std::string> { return std::string("Import macOS data…"); };
    options.viewHooks.macImport = [&] { ++imports; };
    options.viewHooks.chooseColor = [&] { ++colours; };
    options.viewHooks.summonHotkeyToggle = true;
    options.about.repository = "https://github.com/DDDuoDuo/EndfieldHUD";
    t::SettingsPreview owner(raster, options);
    owner.resize({pixelWidth, pixelHeight, 144, scale, 1280, 800});
    owner.setReduceMotion(true, 0);
    m::SettingsUpdateStatus updates;
    updates.title = "Up to date"; updates.detail = "Checked just now"; updates.latestVersion = "v1.2.3";
    updates.releaseURL = "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.3";
    updates.canCheck = updates.canSetAutomatic = true; updates.automaticTitle = "Check for updates daily";
    owner.controller().setUpdateStatus(updates);
    c::ModulePresentation modules{c::Module::system};
    c::source::DesktopChromeSettings settings{{0, 0, 1280, 800}, 1, {}, c::Module::system, true};
    const auto center = c::source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
    gpu::LayerScene geometry(raster);
    geometry.load(Json::Object{{"bounds", Json::Array{0, 0, 400, 334}}, {"children", Json::Array{}}}, {});
    double now{};
    bool finished{};
    struct Cleanup { gpu::Renderer& r; gpu::LayerComposition& c; t::SettingsPreview& o; bool& done; ~Cleanup() { if (!done) try { c.detach(r); o.release(r); } catch (...) { r.reset(); } } } cleanup{renderer, composition, owner, finished};
    const auto frame = [&] {
        now += .01;
        const auto sample = modules.sample(now).presentation;
        settings.module = sample.requested;
        owner.update(center, settings, sample, 1, now);
        owner.upload(renderer);
        composition.setEntries(renderer, owner.entries());
        composition.present(renderer);
        owner.collected(renderer);
    };
    const auto show = [&](c::Module module) { modules.select(module, now, false, true); frame(); };
    // Oracle: a page canvas rect through an independently posed module plane,
    // in physical screen pixels from the client origin.
    const auto expected = [&](c::Rect r) {
        const auto sample = modules.sample(now).presentation;
        gpu::NativeModuleSurface plane(geometry, sample.current.module);
        plane.update(center, settings, sample.current, 1);
        const auto projection = c::Projection::viewport(camera * plane.pose().contentWorld, pixelWidth, pixelHeight);
        double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        for (const c::Point p : {c::Point{r.x, r.y}, c::Point{r.x + r.width, r.y}, c::Point{r.x, r.y + r.height}, c::Point{r.x + r.width, r.y + r.height}}) {
            const auto q = projection.project(p);
            check(q.has_value(), "The oracle plane projects");
            x0 = std::min(x0, q->x); y0 = std::min(y0, q->y); x1 = std::max(x1, q->x); y1 = std::max(y1, q->y);
        }
        return UiaRect{origin.x + x0, origin.y + y0, x1 - x0, y1 - y0};
    };
    const auto sameRect = [](UiaRect u, UiaRect v) { return std::abs(u.left - v.left) < 1e-6 && std::abs(u.top - v.top) < 1e-6 && std::abs(u.width - v.width) < 1e-6 && std::abs(u.height - v.height) < 1e-6; };
    // A click at the centre of a fragment's screen rect (logical client point).
    const auto click = [&](const Fragment& f) {
        const auto r = bounds(f);
        const c::Point q{(r.left + r.width / 2 - origin.x) / scale, (r.top + r.height / 2 - origin.y) / scale};
        check(r.width > 0 && owner.covers(q) && owner.pointer({a::PointerKind::down, a::PointerButton::left, q.x, q.y}, now), "A press at the fragment's centre reaches the projected Settings control");
        owner.pointer({a::PointerKind::up, a::PointerButton::left, q.x, q.y}, now);
        frame();
    };
    const auto key = [&](std::uint32_t vk, std::uint32_t modifiers = 0) { const bool used = owner.key({a::KeyKind::down, vk}, modifiers, now); frame(); return used; };
    const auto view = [&](c::Module module) -> const m::SettingsPageView& { return owner.view(module)->view(); };
    const auto actionRect = [&](c::Module module, std::string_view action) {
        for (const auto& x : view(module).actions) if (x.id == action) return x.rect;
        throw std::runtime_error("Missing Settings action " + std::string(action));
    };
    // The nodes are in the page's visual order: top-level page nodes and the
    // items of each list run top to bottom (left to right within a row).
    const auto visualOrder = [&] {
        const auto nodes = t::settingsAutomationNodes(owner, now);
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            if (nodes[i].space != Node::Space::page) continue;
            for (std::size_t j = i; j-- > 0;) if (nodes[j].parent == nodes[i].parent) {
                // q follows p: below it, or on the same line (overlapping
                // vertically) to its right.
                const auto& p = nodes[j].rect; const auto& q = nodes[i].rect;
                const bool sameLine = q.y < p.y + p.height && p.y < q.y + q.height;
                if (sameLine ? q.x + 1e-9 < p.x : q.y + 1e-9 < p.y) return false;
                break;
            }
        }
        return !nodes.empty();
    };

    Group* group = new Group();
    struct GroupRelease { Group* g; ~GroupRelease() { g->Release(); } } groupRelease{group};
    t::SettingsAutomation binding(owner, {hwnd, [&] { return now; }, [&] { ++changes; }});

    // --- Nothing before a page accepts input ---------------------------------
    check(!binding.refresh(group) && binding.count() == 0 && !owner.inputModule() && !owner.perform("focus", now), "Before Settings is presented no control is listed or performed");
    check(!binding.stale(), "Nothing changed since the empty listing: no content event is asked for");
    frame();
    check(owner.inputModule() == c::Module::system, "The System page accepts input once presented");
    check(binding.stale(), "Once System accepts input, the listing (still empty) asks for one content event");
    binding.requested();
    check(!binding.stale(), "A requested content event is asked for once");

    // --- System ---------------------------------------------------------------
    check(binding.refresh(group) && !binding.stale(), "The System page's controls are listed");
    check(join(topIds(binding)) == "settings:heading,language,login,focus,screen,batteryEnabled,restore",
          "System lists its title and the rows in view, top to bottom: " + join(topIds(binding)));
    check(visualOrder(), "System's nodes follow the page's visual order");
    {
        const auto heading = find(binding, "settings:heading");
        check(type(heading) == UIA_TextControlTypeId && integer(heading, headingLevelProperty) == 80051 && name(heading) == "System", "The page title is a heading 1 named System");
        const auto language = find(binding, "language");
        check(type(language) == UIA_ButtonControlTypeId && name(language) == "Language" && value(language) == "System" && pattern<IInvokeProvider>(language, UIA_InvokePatternId) &&
              !boolean(language, UIA_IsKeyboardFocusablePropertyId), "Language is a button named by its row and valued by what it shows");
        const auto login = find(binding, "login"), focus = find(binding, "focus");
        check(type(login) == UIA_ButtonControlTypeId && name(login) == "Launch at login" && toggled(login) == ToggleState_Off && !pattern<IInvokeProvider>(login, UIA_InvokePatternId),
              "Launch at login is a switch (Toggle, Off as saved), not a plain button");
        check(toggled(focus) == ToggleState_On && name(focus) == "Close when focus lost", "Close when focus lost is a switch, On as saved");
        const auto restore = find(binding, "restore");
        check(name(restore) == "Restore default settings" && !value(restore) && pattern<IInvokeProvider>(restore, UIA_InvokePatternId), "A row showing only a glyph is a button without a value");
        for (const auto* action : {"language", "login", "focus", "screen", "batteryEnabled", "restore"})
            check(sameRect(bounds(find(binding, action)), expected(actionRect(c::Module::system, action))), std::string("The ") + action + " fragment is at its projected action rect in screen pixels");
        check(sameRect(bounds(heading), expected({12, 1, 306, 23})), "The title is at its projected rect");
        const auto r = bounds(focus);
        Fragment hit;
        check(SUCCEEDED(binding.fromPoint(r.left + r.width / 2, r.top + r.height / 2, &hit)) && same(hit.Get(), focus.Get()), "Hit testing finds the switch under its centre");
        Fragment parent;
        check(SUCCEEDED(focus->Navigate(NavigateDirection_Parent, &parent)) && same(parent.Get(), static_cast<IRawElementProviderSimple*>(group)), "Top-level fragments navigate back to the owner's group");
    }

    // A switch toggled through UIA changes the preference exactly like its click.
    {
        auto focus = find(binding, "focus");
        const auto before = saves.size();
        const auto notified = changes;
        check(toggle(focus) == S_OK && !owner.controller().configuration().boolean("closeOnFocusLost") && saves.size() == before + 1 && changes == notified + 1,
              "Toggle switches Close when focus lost off, saves it and reports one content event");
        check(toggled(focus) == ToggleState_Off && same(find(binding, "focus").Get(), focus.Get()), "The switch reports Off at once and keeps its identity");
        const auto viaAutomation = saves.back();
        frame();
        click(focus);
        check(owner.controller().configuration().boolean("closeOnFocusLost") && saves.size() == before + 2, "A click on the switch's rect turns it back on");
        binding.refresh(group);
        check(toggled(focus) == ToggleState_On, "The switch's state follows a click");
        click(focus);
        check(saves.back() == viaAutomation, "The click's saved record equals the one Toggle saved");
        binding.refresh(group);
        check(toggled(focus) == ToggleState_Off && toggle(focus) == S_OK && toggled(focus) == ToggleState_On, "Toggling again restores the saved state");
        frame();
    }

    // Language: a choice page whose rows are a single-selection list.
    {
        check(invoke(find(binding, "language")) == S_OK, "Invoking Language opens its page");
        const auto heading = find(binding, "settings:heading");
        const auto back = find(binding, "back");
        check(name(heading) == "Language" && type(back) == UIA_ButtonControlTypeId && name(back) == "Back", "The Language page lists its title and Back");
        const auto list = find(binding, "settings:list");
        const auto items = children(list);
        const auto selection = pattern<ISelectionProvider>(list, UIA_SelectionPatternId);
        BOOL multiple{TRUE};
        check(type(list) == UIA_ListControlTypeId && name(list) == "Language" && selection && SUCCEEDED(selection->get_CanSelectMultiple(&multiple)) && !multiple && items.size() == 6,
              "The languages are a single-selection list of six items");
        std::vector<std::string> names;
        for (const auto& item : items) names.push_back(name(item));
        check(join(names) == "System,English,简体中文,繁體中文,日本語,한국어" && type(items[0]) == UIA_ListItemControlTypeId && selected(items[0]) == true && selected(items[1]) == false,
              "The items are named as shown and the saved language is selected: " + join(names));
        SAFEARRAY* chosen{};
        check(SUCCEEDED(selection->GetSelection(&chosen)) && chosen && chosen->rgsabound[0].cElements == 1, "The list's selection is the saved language");
        if (chosen) SafeArrayDestroy(chosen);
        ComPtr<IRawElementProviderSimple> container;
        check(SUCCEEDED(pattern<ISelectionItemProvider>(items[1], UIA_SelectionItemPatternId)->get_SelectionContainer(&container)) && same(container.Get(), list.Get()), "An item's selection container is its list");
        check(pattern<ISelectionItemProvider>(items[0], UIA_SelectionItemPatternId)->RemoveFromSelection() == UIA_E_INVALIDOPERATION &&
              pattern<ISelectionItemProvider>(items[1], UIA_SelectionItemPatternId)->AddToSelection() == UIA_E_INVALIDOPERATION, "The single selection cannot be emptied or doubled");
        check(sameRect(bounds(items[2]), expected(actionRect(c::Module::system, "language:simplifiedChinese"))), "An item is at its projected row");
        check(visualOrder(), "The Language page's nodes follow the visual order");
        const auto chinese = items[2];
        check(choose(chinese) == S_OK && owner.controller().configuration().string("language") == "simplifiedChinese", "Selecting a language saves it like the row's click");
        check(name(find(binding, "settings:heading")) == "System" && available(list) == UIA_E_ELEMENTNOTAVAILABLE && !find(binding, "back"), "The selection returns to System; the list is gone");
        frame();
        owner.setLanguage(c::Language::simplifiedChinese);
        frame();
        binding.refresh(group);
        check(name(find(binding, "settings:heading")) == "系统" && name(find(binding, "login")) == "登录时启动" && name(find(binding, "language")) == "语言" && value(find(binding, "language")) == "简体中文",
              "Names follow the Settings language (localized as the page shows them)");
        owner.setLanguage(c::Language::english);
        frame();
        binding.refresh(group);
    }

    // Display: the screens list with a disconnected-detail row, then Restore.
    {
        check(invoke(find(binding, "screen")) == S_OK && name(find(binding, "settings:heading")) == "Display", "Invoking Display opens the screen list");
        const auto items = children(find(binding, "settings:list"));
        std::vector<std::string> names;
        for (const auto& item : items) names.push_back(name(item));
        check(join(names) == "Pointer display,Main display,Synthetic Display, 1920 × 1080" && selected(items[0]) == true, "Screens are named with their details; the pointer display is selected: " + join(names));
        check(choose(items[2]) == S_OK && owner.controller().configuration().fields["windowsDisplayID"].string() == "fixture-display", "Selecting a display keeps its Windows identity like the click");
        check(value(find(binding, "screen")) == "Synthetic Display", "The Display row's value follows the selection");
        frame();
        check(invoke(find(binding, "restore")) == S_OK, "Invoking Restore default settings asks first");
        check(join(topIds(binding)) == "settings:heading,settings:restore:title,settings:restore:message,restore:cancel,restore:confirm" &&
              name(find(binding, "settings:restore:title")) == "Restore all preferences?" && name(find(binding, "restore:confirm")) == "Restore defaults",
              "The restore question lists its texts and its two buttons only: " + join(topIds(binding)));
        check(invoke(find(binding, "restore:cancel")) == S_OK && find(binding, "login"), "Cancel restore returns to the rows");
        frame();
    }

    // Import macOS data… once scrolled into view: a button that only presents the card.
    {
        check(!find(binding, "macImport") && key(VK_NEXT), "Import macOS data… is below the viewport; Page Down scrolls");
        binding.refresh(group);
        const auto import = find(binding, "macImport");
        const auto before = saves.size();
        check(import && !find(binding, "language") && name(import) == "Import macOS data…" && !value(import), "Scrolled, the Import macOS data… button is listed and Language is not");
        check(invoke(import) == S_OK && imports == 1 && saves.size() == before, "Invoking it presents the import card without changing a preference");
        key(VK_PRIOR);
    }

    // --- Display: page change after a module switch, sliders, the card -------------
    modules.select(c::Module::display, now, false, true);
    binding.refresh(group);
    check(name(find(binding, "settings:heading")) == "System", "Before the switch's frame the System page is still listed");
    frame();
    check(binding.stale(), "The settled Display page makes the listing stale (the owner asks for one content event)");
    binding.requested();
    check(!binding.stale(), "The request is asked for once");
    binding.refresh(group);
    check(!binding.stale() && name(find(binding, "settings:heading")) == "Display" && owner.inputModule() == c::Module::display, "The content event lists the Display page");
    check(visualOrder(), "Display's nodes follow the visual order");
    {
        const auto ui = find(binding, "uiScale"), parallax = find(binding, "parallax"), x = find(binding, "positionX");
        const auto rv = pattern<IRangeValueProvider>(ui, UIA_RangeValuePatternId);
        double low{}, high{}, smallStep{}, largeStep{};
        check(type(ui) == UIA_SliderControlTypeId && name(ui) == "UI scale" && rv && SUCCEEDED(rv->get_Minimum(&low)) && SUCCEEDED(rv->get_Maximum(&high)) && SUCCEEDED(rv->get_SmallChange(&smallStep)) &&
              SUCCEEDED(rv->get_LargeChange(&largeStep)) && low == .2 && high == 2 && smallStep == .05 && largeStep == .05 && range(ui) == 1 && value(ui) == "1.00×", "UI scale is a slider over the source range and step with its reading");
        check(sameRect(bounds(ui), expected(view(c::Module::display).sliders.front().rect)), "A slider is at its projected track rect");
        double step{};
        check(SUCCEEDED(pattern<IRangeValueProvider>(x, UIA_RangeValuePatternId)->get_SmallChange(&step)) && step == .01 && name(x) == "HUD position X", "The HUD position steps by 0.01");
        check(setRange(parallax, 1.5) == S_OK && owner.controller().configuration().number("parallaxIntensity") == 1.5 && range(parallax) == 1.5 && value(parallax) == "150%",
              "RangeValue sets Parallax intensity like the slider and the reading follows");
        check(setRange(parallax, 9) == S_OK && owner.controller().configuration().number("parallaxIntensity") == 2 && value(parallax) == "200%", "Values beyond the range are clamped like the source slider");
        check(setRange(parallax, NAN) == E_INVALIDARG, "A non-finite value is refused");
        check(key(VK_LEFT), "The left arrow nudges the selected slider");
        binding.refresh(group);
        check(std::abs(*range(parallax) - 1.96) < 1e-9 && value(parallax) == "196%", "The slider's value follows a keyboard nudge");
        // The UI scale opens the confirmation card: only the card is listed.
        const auto focusMoves = binding.provider()->stats().focusMoves;
        check(setRange(ui, 1.5) == S_OK && owner.controller().scalePending(), "Setting the UI scale previews it pending confirmation");
        frame();
        check(owner.modalActive() && binding.stale(), "The card makes the listing stale");
        binding.refresh(group);
        check(join(topIds(binding)) == "settings:safety" && available(parallax) == UIA_E_ELEMENTNOTAVAILABLE && !owner.perform("ambient", now) && !owner.setSlider("parallax", 1, now),
              "While the card is up only the card is listed and page controls are refused");
        const auto dialog = find(binding, "settings:safety"), keep = find(binding, "settings:safety:confirm"), revert = find(binding, "settings:safety:cancel");
        std::vector<std::string> parts;
        for (const auto& f : children(dialog)) parts.push_back(id(f));
        check(type(dialog) == UIA_PaneControlTypeId && boolean(dialog, isDialogProperty) && name(dialog) == "Keep this UI scale?" && text(dialog, UIA_HelpTextPropertyId).starts_with("Keep this UI scale? Reverts in"),
              "The card is a dialog named by its title with the countdown as help");
        check(join(parts) == "settings:safety:title,settings:safety:message,settings:safety:cancel,settings:safety:confirm", "The card holds its texts and Revert / Keep: " + join(parts));
        check(name(keep) == "Keep ↵" && boolean(keep, UIA_HasKeyboardFocusPropertyId) && boolean(keep, UIA_IsKeyboardFocusablePropertyId) && !boolean(revert, UIA_HasKeyboardFocusPropertyId) &&
              binding.provider()->stats().focusMoves == focusMoves + 1, "Keep holds the card's keyboard focus, announced once");
        const auto k = bounds(keep), d = bounds(dialog);
        Fragment hit;
        check(k.width > 0 && d.width > k.width && k.left >= d.left && k.top >= d.top && k.left + k.width <= d.left + d.width + 1e-6 &&
              SUCCEEDED(binding.fromPoint(k.left + k.width / 2, k.top + k.height / 2, &hit)) && same(hit.Get(), keep.Get()), "Keep lies inside the projected card and is hit at its centre");
        check(revert->SetFocus() == S_OK && owner.safety().focused() == 0 && boolean(revert, UIA_HasKeyboardFocusPropertyId) && !boolean(keep, UIA_HasKeyboardFocusPropertyId),
              "SetFocus moves the card's focus to Revert like the arrows");
        const auto before = saves.size();
        check(invoke(keep) == S_OK && !owner.controller().scalePending() && owner.controller().committed().number("hudScale") == 1.5 && saves.size() == before + 1,
              "Invoking Keep commits the UI scale like its click");
        check(available(dialog) == UIA_E_ELEMENTNOTAVAILABLE && find(binding, "uiScale") && range(find(binding, "uiScale")) == 1.5, "The card leaves; the page returns with the kept scale");
        frame();
    }
    // Theme, clock and the colour list further down.
    {
        check(key(VK_NEXT), "Page Down scrolls Display");
        binding.refresh(group);
        const auto theme = find(binding, "theme");
        check(theme && value(theme) == "Dark" && value(find(binding, "clockFormat")) == "24-hour" && value(find(binding, "clockStyle")) == "01 / 05" && !value(find(binding, "centerLogo")),
              "Theme and the clock rows are buttons valued as shown; Center logo › has no value");
        check(invoke(theme) == S_OK && owner.controller().configuration().string("theme") == "light" && value(theme) == "Light", "Invoking Theme cycles it like the click and the value follows");
        frame();
        check(key(VK_NEXT), "Page Down reaches the end of Display");
        binding.refresh(group);
        const auto palette = find(binding, "settings:palette");
        const auto colours5 = children(palette);
        check(type(palette) == UIA_ListControlTypeId && name(palette) == "Theme color" && colours5.size() == 5 && name(colours5[0]) == "Theme color #FAD41F" && selected(colours5[0]) == true,
              "Theme color is a list of the five colours with the current one selected");
        check(choose(colours5[1]) == S_OK && owner.controller().configuration().string("accentHex") == "6EDFE8" && selected(colours5[1]) == true && selected(colours5[0]) == false,
              "Selecting a colour saves it and moves the selection");
        const auto custom = find(binding, "customColor");
        check(name(custom) == "Custom color wheel" && invoke(custom) == S_OK && colours == 1, "The colour wheel is a button that opens the picker");
        check(toggled(find(binding, "ambient")) == ToggleState_On && toggled(find(binding, "motion")) == ToggleState_Off && toggled(find(binding, "lowPower")) == ToggleState_Off, "The three appearance switches report their states");
        check(visualOrder(), "The scrolled Display page follows the visual order");
        frame();
        check(invoke(find(binding, "battery")) == S_OK && name(find(binding, "settings:heading")) == "Battery alert", "Battery alert opens its page");
        const auto duration = find(binding, "duration");
        double step{};
        check(value(find(binding, "metric")) == "Battery" && value(find(binding, "method")) == "Power changes" && value(find(binding, "placement")) == "Top center" && !value(find(binding, "editPosition")) &&
              type(duration) == UIA_SliderControlTypeId && SUCCEEDED(pattern<IRangeValueProvider>(duration, UIA_RangeValuePatternId)->get_SmallChange(&step)) && step == 1 && find(binding, "batterySize"),
              "Battery alert lists its rows: valued choices, the duration slider (step 1) and the size");
        check(invoke(find(binding, "metric")) == S_OK, "Reading opens its list");
        const auto readings = children(find(binding, "settings:list"));
        check(readings.size() == 6 && name(readings[5]) == "GPU" && choose(readings[5]) == S_OK && owner.controller().configuration().string("alertMetric") == "gpu" &&
              name(find(binding, "settings:heading")) == "Battery alert" && value(find(binding, "metric")) == "GPU", "Selecting GPU saves it and returns to Battery alert");
        check(invoke(find(binding, "back")) == S_OK && name(find(binding, "settings:heading")) == "Display", "Back returns to Display");
        frame();
    }

    // --- Hotkeys: the recorder ------------------------------------------------------
    show(c::Module::hotkeys);
    binding.refresh(group);
    {
        const auto capture = find(binding, "capture");
        check(type(capture) == UIA_ButtonControlTypeId && name(capture) == "Summon HUD" && value(capture) == "Ctrl + `" && !boolean(capture, UIA_IsKeyboardFocusablePropertyId) &&
              value(find(binding, "keyReset")) == "Ctrl + `", "The recorder is a button valued by the shortcut; Restore default hotkey shows the default");
        const auto summon = find(binding, "summonHotkey");
        check(name(summon) == "Summon HUD with hotkey" && toggled(summon) == ToggleState_On, "Summon HUD with hotkey is a switch, On");
        const auto moves = binding.provider()->stats().focusMoves;
        check(invoke(capture) == S_OK && captures == std::vector<bool>{true} && value(capture) == "Press shortcut…" && boolean(capture, UIA_HasKeyboardFocusPropertyId) &&
              binding.provider()->stats().focusMoves == moves + 1, "Invoking the recorder records like its click; it holds the keyboard focus");
        const auto status = find(binding, "settings:status");
        check(type(status) == UIA_TextControlTypeId && integer(status, UIA_LiveSettingPropertyId) == Polite && name(status) == "Press up to 3 keys · Esc to cancel" &&
              name(find(binding, "capture:cancel")) == "Cancel shortcut recording", "The status line is live text and Cancel shortcut recording is listed");
        Fragment focused;
        check(SUCCEEDED(binding.provider()->focused(&focused)) && same(focused.Get(), capture.Get()), "The focused fragment is the recorder");
        check(key(0x77, 3) && registrations == 1 && captures == std::vector<bool>({true, false}), "The next chord is recorded");
        binding.refresh(group);
        check(value(capture) == "Ctrl + Alt + F8" && !boolean(capture, UIA_HasKeyboardFocusPropertyId) && !find(binding, "capture:cancel"), "The recorder's value follows the recorded chord");
        check(invoke(capture) == S_OK && invoke(find(binding, "capture:cancel")) == S_OK && captures == std::vector<bool>({true, false, true, false}) && value(capture) == "Ctrl + Alt + F8",
              "Cancel shortcut recording stops recording like its click");
        check(toggle(summon) == S_OK && owner.controller().configuration().fields["windowsSummonHotkeyDisabled"].boolean() && toggled(summon) == ToggleState_Off, "Toggling the hotkey switch saves it");
        check(toggle(summon) == S_OK && toggled(summon) == ToggleState_On, "Toggling it again turns opening by hotkey back on");
        check(visualOrder(), "Hotkeys follows the visual order");
        frame();
    }

    // --- About: updates, links, credits ----------------------------------------------
    show(c::Module::about);
    binding.refresh(group);
    {
        check(join(topIds(binding)) == "settings:heading,app,updates,latestRelease,automaticUpdates,author,github,settings:status", "About lists its rows in view and its status: " + join(topIds(binding)));
        const auto check_ = find(binding, "updates"), latest = find(binding, "latestRelease"), autoUpdates = find(binding, "automaticUpdates"), github = find(binding, "github");
        check(type(find(binding, "app")) == UIA_TextControlTypeId && name(find(binding, "app")) == "EndfieldHUD, Development" && name(find(binding, "author")) == "Author, DDDuoDuo",
              "About's information rows are text");
        check(name(check_) == "Check for updates" && value(check_) == "Up to date" && text(check_, UIA_HelpTextPropertyId) == "Checked just now" && boolean(check_, UIA_IsEnabledPropertyId),
              "Check for updates is a button valued by its state, with the status as help");
        check(type(latest) == UIA_HyperlinkControlTypeId && name(latest) == "Latest GitHub release, v1.2.3" && value(latest) == updates.releaseURL && type(github) == UIA_HyperlinkControlTypeId &&
              name(github) == "GitHub" && value(github) == "https://github.com/DDDuoDuo/EndfieldHUD", "The release and repository are hyperlinks valued by their addresses");
        check(invoke(check_) == S_OK && updateChecks == 1, "Invoking Check for updates checks like its click");
        check(invoke(latest) == S_OK && links.size() == 1 && links.back() == updates.releaseURL, "Invoking the release link opens it like its click");
        check(name(autoUpdates) == "Check for updates daily" && toggled(autoUpdates) == ToggleState_Off && toggle(autoUpdates) == S_OK && automatic == std::vector<bool>{true},
              "The update switch asks the service to turn daily checks on");
        updates.automaticallyInstalls = true;
        owner.controller().setUpdateStatus(updates);
        frame();
        binding.refresh(group);
        check(toggled(autoUpdates) == ToggleState_On, "The switch follows the service's state");
        updates.canCheck = false;
        owner.controller().setUpdateStatus(updates);
        frame();
        binding.refresh(group);
        check(!boolean(check_, UIA_IsEnabledPropertyId) && invoke(check_) == UIA_E_ELEMENTNOTENABLED && updateChecks == 1, "An unavailable check is a disabled button that refuses Invoke");
        check(key(VK_NEXT), "Page Down scrolls About");
        binding.refresh(group);
        const auto credits = find(binding, "credits"), fan = find(binding, "credit:0"), inspiration = find(binding, "credit:1");
        check(type(credits) == UIA_TextControlTypeId && integer(credits, headingLevelProperty) == 80052 && name(credits) == "Credits:", "Credits: is a heading 2");
        check(type(fan) == UIA_TextControlTypeId && name(fan) == "Arknights: Endfield, Unofficial fan project" && type(inspiration) == UIA_HyperlinkControlTypeId &&
              name(inspiration) == "llynxxx, Inspiration", "A credit without an address is text, one with an address a hyperlink");
        check(invoke(inspiration) == S_OK && links.back() == "https://www.bilibili.com/video/BV1DBaP6yEHs/", "A credit link opens like its click");
        check(visualOrder(), "About follows the visual order");
        frame();
    }

    // --- Hidden: nothing is exposed --------------------------------------------------
    {
        const auto before = find(binding, "license");
        owner.setOverlayVisible(false, now);
        frame();
        binding.refresh(group);
        check(binding.count() == 0 && available(before) == UIA_E_ELEMENTNOTAVAILABLE && !owner.inputModule() && !owner.clientRect({0, 0, 400, 334}) && !owner.perform("license", now),
              "Hidden Settings lists, projects and performs nothing");
        binding.disconnect();
        check(!binding.stale(), "A disconnected binding asks for no content event");
        owner.setOverlayVisible(true, now);
        frame();
        show(c::Module::power);
        binding.refresh(group);
        check(binding.count() == 0 && !owner.inputModule(), "Another module shown: Settings lists nothing");
    }

    // --- Under the HUD window's root provider ----------------------------------------
    {
        SettingsOwner forward(owner, binding);
        a::HudAutomationSource source;
        source.owners = [&](std::vector<a::ModuleOwner*>& out) { out.push_back(&forward); };
        source.refresh = [](a::ModuleOwner& o, IRawElementProviderFragmentRoot* root) { return o.automationRefresh(root); };
        source.name = [](const a::ModuleOwner& o) { const auto module = o.automationModule(); return module ? std::string(c::moduleIdentifier(*module)) : std::string("settings"); };
        a::HudAutomation uia(hwnd, source);
        show(c::Module::hotkeys);
        check(uia.refresh() && uia.groups() == 1, "The root lists the Settings group once Hotkeys accepts input");
        Fragment root, settingsGroup, first, parent;
        check(SUCCEEDED(uia.root()->QueryInterface(IID_PPV_ARGS(&root))) && SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, &settingsGroup)) && settingsGroup && name(settingsGroup) == "hotkeys",
              "The group is named after the page that accepts input");
        check(SUCCEEDED(settingsGroup->Navigate(NavigateDirection_FirstChild, &first)) && first && id(first) == "settings:heading" && SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) &&
              same(parent.Get(), settingsGroup.Get()), "The page's fragments are the group's children and navigate back to it");
        const auto capture = find(binding, "capture");
        const auto r = bounds(capture);
        Fragment hit, focused;
        check(SUCCEEDED(uia.root()->ElementProviderFromPoint(r.left + r.width / 2, r.top + r.height / 2, &hit)) && same(hit.Get(), capture.Get()), "The root's hit test reaches the recorder");
        check(invoke(capture) == S_OK && SUCCEEDED(uia.root()->GetFocus(&focused)) && same(focused.Get(), capture.Get()), "While recording, the root's focus is the recorder");
        check(key(VK_ESCAPE) && !owner.controller().capturingShortcut(), "Escape ends recording");
        (void)uia.refresh();
        focused.Reset();
        check(SUCCEEDED(uia.root()->GetFocus(&focused)) && !focused, "No Settings fragment holds the focus afterwards");
        show(c::Module::display);
        (void)uia.refresh();
        check(name(settingsGroup) == "display" && name(find(binding, "settings:heading")) == "Display", "The group's name follows the page");
        uia.disconnectOwners();
        check(forward.automationCount() == 0 && uia.groups() == 0 && available(capture) == UIA_E_ELEMENTNOTAVAILABLE, "Concealing disconnects every Settings fragment");
        uia.shutdown();
    }

    composition.detach(renderer);
    owner.release(renderer);
    finished = true;
    check(!IsWindowVisible(hwnd) && renderer.stats().presents == 0, "The fixture never shows a window");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the HLSL shader");
        run(argv[1]);
        CoUninitialize();
        std::cout << "PASS " << checks << " Settings UI Automation checks (hidden owner; injected services)\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
