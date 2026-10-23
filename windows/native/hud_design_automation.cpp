#include "native/hud_design_automation.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h> // VARIANT/BSTR/SAFEARRAY helpers even under WIN32_LEAN_AND_MEAN
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
using Node = AutomationNode;
using Role = Node::Role;
namespace d = modules::design;
// Windows 10 1803/1809 properties (UIA_HeadingLevelPropertyId with
// HeadingLevel_None + n, UIA_IsDialogPropertyId); older systems ignore them.
constexpr PROPERTYID headingLevelProperty = 30173;
constexpr long headingLevelNone = 80050;
constexpr PROPERTYID isDialogProperty = 30174;
std::atomic<int> nextRuntime{0x53540001};
std::wstring wide(std::string_view s) {
    if (s.empty() || s.size() > INT_MAX) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* s) {
    if (!s || !*s) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}
VARIANT text(std::string_view s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(wide(s).c_str()); return v; }
VARIANT flag(bool b) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return v; }
VARIANT number(double d) { VARIANT v; VariantInit(&v); v.vt = VT_R8; v.dblVal = d; return v; }
VARIANT integer(long l) { VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = l; return v; }
template <class F> HRESULT guarded(F&& f) noexcept { try { return f(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; } }
long controlType(Role role) noexcept {
    switch (role) {
    case Role::heading: case Role::text: case Role::status: return UIA_TextControlTypeId;
    case Role::button: case Role::toggle: return UIA_ButtonControlTypeId;
    case Role::link: return UIA_HyperlinkControlTypeId;
    case Role::slider: return UIA_SliderControlTypeId;
    case Role::list: return UIA_ListControlTypeId;
    case Role::item: return UIA_ListItemControlTypeId;
    case Role::dialog: return UIA_PaneControlTypeId;
    case Role::popUp: return UIA_ComboBoxControlTypeId;
    case Role::menuItem: return UIA_MenuItemControlTypeId;
    case Role::tab: return UIA_TabItemControlTypeId;
    case Role::edit: return UIA_EditControlTypeId;
    case Role::group: return UIA_GroupControlTypeId;
    }
    return UIA_TextControlTypeId;
}
bool invocable(const Node& n) noexcept { return n.role == Role::button || n.role == Role::link || n.role == Role::menuItem; }
bool selectable(const Node& n) noexcept { return n.role == Role::item || n.role == Role::menuItem || n.role == Role::tab; }
// A Value: a slider's reading, a popUp's value, an edit's text, or the value
// a text / button / group shows. Only an enabled edit's is writable.
bool valued(const Node& n) noexcept {
    return n.role == Role::slider || n.role == Role::popUp || n.role == Role::edit || (n.valued && (n.role == Role::text || n.role == Role::group || invocable(n)));
}
bool focusable(const Node& n) noexcept { return n.focusable && n.enabled; }
bool hasFocus(const Node& n) noexcept { return focusable(n) && n.focused; }
long toggleState(bool on) noexcept { return on ? ToggleState_On : ToggleState_Off; }
long expandState(bool on) noexcept { return on ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed; }
} // namespace

class AutomationElement;
struct AutomationTree::State {
    AutomationTreeHost host;
    std::vector<ComPtr<AutomationElement>> order;
    AutomationTreeStats statistics;
    std::weak_ptr<State> self;
    int focusedRuntime{}; // the node that last received the focus event (0: none)
    bool alive{true};     // false once the owning object is destroyed: the host is never called again
    std::optional<core::Rect> screen(const Node& n) const {
        if (!alive || !host.toScreen) return std::nullopt;
        std::optional<core::Rect> out;
        try { out = host.toScreen(n.rect, n.space); } catch (...) { return std::nullopt; }
        if (!out || !std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->width) || !std::isfinite(out->height) || out->width <= 0 || out->height <= 0)
            return std::nullopt;
        return out;
    }
    bool relist();
    void contentChanged() noexcept;
    void disconnectAll() noexcept;
};

class AutomationElement final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IToggleProvider,
                                public IRangeValueProvider, public ISelectionProvider, public ISelectionItemProvider, public IExpandCollapseProvider {
public:
    using State = AutomationTree::State;
    AutomationElement(std::weak_ptr<State> s, Node n) : state(std::move(s)), data(std::move(n)), runtime(nextRuntime++) {}
    std::weak_ptr<State> state;
    Node data;
    std::size_t index{}; // position in State::order (refreshed with it)
    int runtime;
    bool connected{true};
    bool is(Role role) const noexcept { return data.role == role; }
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider) && invocable(data)) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IToggleProvider) && is(Role::toggle)) *out = static_cast<IToggleProvider*>(this);
        else if (riid == __uuidof(IRangeValueProvider) && is(Role::slider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (riid == __uuidof(ISelectionProvider) && is(Role::list)) *out = static_cast<ISelectionProvider*>(this);
        else if (riid == __uuidof(ISelectionItemProvider) && selectable(data)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (riid == __uuidof(IExpandCollapseProvider) && is(Role::popUp)) *out = static_cast<IExpandCollapseProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID id, IUnknown** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_InvokePatternId && invocable(data)) *out = static_cast<IInvokeProvider*>(this);
        else if (id == UIA_TogglePatternId && is(Role::toggle)) *out = static_cast<IToggleProvider*>(this);
        else if (id == UIA_RangeValuePatternId && is(Role::slider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (id == UIA_SelectionPatternId && is(Role::list)) *out = static_cast<ISelectionProvider*>(this);
        else if (id == UIA_SelectionItemPatternId && selectable(data)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (id == UIA_ExpandCollapsePatternId && is(Role::popUp)) *out = static_cast<IExpandCollapseProvider*>(this);
        else if (id == UIA_ValuePatternId && valued(data)) return valueText(out);
        else return S_OK;
        AddRef();
        return S_OK;
    }
    HRESULT valueText(IUnknown** out);
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        const HRESULT hr = guarded([&] { property(id, *out); return S_OK; });
        if (FAILED(hr)) VariantClear(out);
        return hr;
    }
    void property(PROPERTYID id, VARIANT& out) {
        switch (id) {
        case UIA_NamePropertyId: out = text(data.name); break;
        case UIA_HelpTextPropertyId: if (!data.help.empty()) out = text(data.help); break;
        case UIA_AutomationIdPropertyId: out = text(data.id); break;
        case UIA_ControlTypePropertyId: out = integer(controlType(data.role)); break;
        case UIA_IsEnabledPropertyId: out = flag(data.enabled); break;
        case UIA_IsKeyboardFocusablePropertyId: out = flag(focusable(data)); break;
        case UIA_HasKeyboardFocusPropertyId: out = flag(hasFocus(data)); break;
        case UIA_IsOffscreenPropertyId: { const auto s = state.lock(); out = flag(!s || !s->screen(data)); break; }
        case UIA_IsPasswordPropertyId: if (is(Role::edit)) out = flag(data.secure); break;
        case UIA_FrameworkIdPropertyId: out = text("EndfieldHUD"); break;
        case UIA_LiveSettingPropertyId: if (is(Role::status)) out = integer(Polite); break;
        case headingLevelProperty: if (is(Role::heading) && data.level >= 1 && data.level <= 9) out = integer(headingLevelNone + data.level); break;
        case isDialogProperty: if (is(Role::dialog)) out = flag(true); break;
        default: break;
        }
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto& order = s->order;
        if (index >= order.size() || order[index].Get() != this) return UIA_E_ELEMENTNOTAVAILABLE;
        switch (direction) {
        case NavigateDirection_Parent:
            if (data.parent == Node::topLevel) return s->host.root ? s->host.root->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
            return data.parent < order.size() ? order[data.parent]->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
        case NavigateDirection_FirstChild:
            for (std::size_t j = index + 1; j < order.size(); ++j) if (order[j]->data.parent == index) return order[j]->QueryInterface(IID_PPV_ARGS(out));
            return S_OK;
        case NavigateDirection_LastChild:
            for (std::size_t j = order.size(); j-- > index + 1;) if (order[j]->data.parent == index) return order[j]->QueryInterface(IID_PPV_ARGS(out));
            return S_OK;
        case NavigateDirection_NextSibling:
            for (std::size_t j = index + 1; j < order.size(); ++j) if (order[j]->data.parent == data.parent) return order[j]->QueryInterface(IID_PPV_ARGS(out));
            return S_OK;
        case NavigateDirection_PreviousSibling:
            for (std::size_t j = index; j-- > 0;) if (order[j]->data.parent == data.parent) return order[j]->QueryInterface(IID_PPV_ARGS(out));
            return S_OK;
        }
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        int ids[2]{UiaAppendRuntimeId, runtime};
        SAFEARRAY* a = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!a) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; ++i) { const HRESULT hr = SafeArrayPutElement(a, &i, &ids[i]); if (FAILED(hr)) { SafeArrayDestroy(a); return hr; } }
        *out = a;
        return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        *out = {};
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&] { if (const auto r = s->screen(data)) *out = {r->x, r->y, r->width, r->height}; return S_OK; });
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    // A client moves the keyboard focus: a focusable element takes it through
    // the host (the card's focus like Left/Right, an edit begins editing); the
    // other elements take no focus (the HUD routes its own keys), so the
    // request changes nothing for them.
    IFACEMETHODIMP SetFocus() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!data.focusable || data.focused) return S_OK;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        return act([](State& s, const std::string& id) { ++s.statistics.focusRequests; return s.host.focus && s.host.focus(id); });
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (s->host.root) { s->host.root->AddRef(); *out = s->host.root; }
        return S_OK;
    }
    // IInvokeProvider: a button's, link's or menu row's click.
    IFACEMETHODIMP Invoke() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!invocable(data)) return UIA_E_INVALIDOPERATION;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        return act([](State& s, const std::string& id) { ++s.statistics.invokes; return s.host.invoke && s.host.invoke(id); });
    }
    // IToggleProvider: the switch's click flips it.
    IFACEMETHODIMP Toggle() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!is(Role::toggle)) return UIA_E_INVALIDOPERATION;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        return act([](State& s, const std::string& id) { ++s.statistics.toggles; return s.host.invoke && s.host.invoke(id); });
    }
    IFACEMETHODIMP get_ToggleState(ToggleState* out) override {
        if (!out) return E_POINTER;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = static_cast<ToggleState>(toggleState(data.on));
        return S_OK;
    }
    // IExpandCollapseProvider: a dropdown opens and closes its menu by its
    // own press (the Mac stand-in is a pop-up button that presses).
    HRESULT expandTo(bool expand) {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!is(Role::popUp)) return UIA_E_INVALIDOPERATION;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        if (data.on == expand) return S_OK;
        return act([expand](State& s, const std::string& id) {
            ++s.statistics.expansions;
            if (s.host.expand) return s.host.expand(id, expand);
            return s.host.invoke && s.host.invoke(id);
        });
    }
    IFACEMETHODIMP Expand() override { return expandTo(true); }
    IFACEMETHODIMP Collapse() override { return expandTo(false); }
    IFACEMETHODIMP get_ExpandCollapseState(ExpandCollapseState* out) override {
        if (!out) return E_POINTER;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = static_cast<ExpandCollapseState>(expandState(data.on));
        return S_OK;
    }
    // IRangeValueProvider: the source NSSlider (clamped into its range, its step).
    IFACEMETHODIMP SetValue(double value) override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!is(Role::slider)) return UIA_E_INVALIDOPERATION;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        if (!std::isfinite(value)) return E_INVALIDARG;
        const double clamped = std::min(std::max(value, data.minimum), data.maximum);
        return act([clamped](State& s, const std::string& id) { ++s.statistics.values; return s.host.setValue && s.host.setValue(id, clamped); });
    }
    IFACEMETHODIMP get_Value(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.number; return S_OK; }
    IFACEMETHODIMP get_IsReadOnly(BOOL* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.enabled ? FALSE : TRUE; return S_OK; }
    IFACEMETHODIMP get_Maximum(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.maximum; return S_OK; }
    IFACEMETHODIMP get_Minimum(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.minimum; return S_OK; }
    IFACEMETHODIMP get_LargeChange(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.step; return S_OK; }
    IFACEMETHODIMP get_SmallChange(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.step; return S_OK; }
    // ISelectionProvider: a choice list selects one row at a time.
    IFACEMETHODIMP GetSelection(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        std::vector<AutomationElement*> chosen;
        for (const auto& e : s->order) if (e->data.parent == index && e->is(Role::item) && e->data.on) chosen.push_back(e.Get());
        SAFEARRAY* a = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(chosen.size()));
        if (!a) return E_OUTOFMEMORY;
        for (LONG i = 0; i < static_cast<LONG>(chosen.size()); ++i) {
            const HRESULT hr = SafeArrayPutElement(a, &i, static_cast<IUnknown*>(static_cast<IRawElementProviderSimple*>(chosen[static_cast<std::size_t>(i)])));
            if (FAILED(hr)) { SafeArrayDestroy(a); return hr; }
        }
        *out = a;
        return S_OK;
    }
    IFACEMETHODIMP get_CanSelectMultiple(BOOL* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = FALSE; return S_OK; }
    IFACEMETHODIMP get_IsSelectionRequired(BOOL* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = FALSE; return S_OK; }
    // ISelectionItemProvider: Select is the row's / tab's / menu row's click
    // (which may also leave a choice page or close a menu, as the click
    // does); a selected one stays selected.
    IFACEMETHODIMP Select() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!selectable(data)) return UIA_E_INVALIDOPERATION;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        return act([](State& s, const std::string& id) { ++s.statistics.selections; return s.host.invoke && s.host.invoke(id); });
    }
    IFACEMETHODIMP AddToSelection() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (data.on) return S_OK;
        const auto s = state.lock();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        for (const auto& e : s->order)
            if (e.Get() != this && e->data.parent == data.parent && e->data.role == data.role && e->data.on) return UIA_E_INVALIDOPERATION;
        return Select();
    }
    IFACEMETHODIMP RemoveFromSelection() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        return data.on ? UIA_E_INVALIDOPERATION : S_OK;
    }
    IFACEMETHODIMP get_IsSelected(BOOL* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.on ? TRUE : FALSE; return S_OK; }
    IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (data.parent < s->order.size() && s->order[data.parent]->is(Role::list)) return s->order[data.parent]->QueryInterface(IID_PPV_ARGS(out));
        return S_OK;
    }
    void disconnect() noexcept {
        if (!connected) return;
        connected = false;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    }
    // Runs an owner action outside any C++ exception boundary; an accepted
    // one re-lists the nodes at once (a page may replace them).
    template <class F> HRESULT act(F&& action) {
        const auto s = state.lock();
        if (!s || !s->alive) return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may rebuild the tree; keep this element and its id alive.
        const ComPtr<AutomationElement> self(this);
        const std::string id = data.id;
        bool used{};
        try { used = action(*s, id); } catch (...) { return E_FAIL; }
        if (!used) return UIA_E_INVALIDOPERATION;
        if (s->alive) s->contentChanged();
        return S_OK;
    }
private:
    ~AutomationElement() = default;
    std::atomic<ULONG> refs{1};
};

// Value pattern tear-off: the reading a slider, popUp, text, group or valued
// button shows ("1.00x", "English", "Ctrl + `"), read-only; an enabled edit's
// text, writable through the host (the field's own bounding rules apply).
class AutomationValue final : public IValueProvider {
public:
    explicit AutomationValue(ComPtr<AutomationElement> e) : element(std::move(e)) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    bool writable() const noexcept { return element->is(Role::edit) && element->data.enabled; }
    IFACEMETHODIMP SetValue(LPCWSTR value) override {
        if (!element->connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!element->is(Role::edit)) return UIA_E_INVALIDOPERATION;
        if (!element->data.enabled) return UIA_E_ELEMENTNOTENABLED;
        std::string utf8;
        try { utf8 = narrow(value); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        return element->act([&utf8](AutomationTree::State& s, const std::string& id) { ++s.statistics.texts; return s.host.setText && s.host.setText(id, utf8); });
    }
    IFACEMETHODIMP get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!element->connected) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = SysAllocString(wide(element->data.value).c_str());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL* out) override { if (!out) return E_POINTER; if (!element->connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = writable() ? FALSE : TRUE; return S_OK; }
private:
    ~AutomationValue() = default;
    ComPtr<AutomationElement> element;
    std::atomic<ULONG> refs{1};
};
HRESULT AutomationElement::valueText(IUnknown** out) {
    try { *out = static_cast<IValueProvider*>(new AutomationValue(ComPtr<AutomationElement>(this))); return S_OK; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}

bool AutomationTree::State::relist() {
    ++statistics.refreshes;
    auto list = host.nodes ? host.nodes() : std::vector<Node>{};
    // Pre-order with valid parents only; anything else lists nothing.
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].parent != Node::topLevel && list[i].parent >= i) { list.clear(); break; }
    const bool listening = UiaClientsAreListening() != FALSE;
    const bool listed = !order.empty();
    std::vector<ComPtr<AutomationElement>> next;
    next.reserve(list.size());
    std::vector<AutomationElement*> selected, announced;
    bool changed = list.size() != order.size();
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto& n = list[i];
        ComPtr<AutomationElement> e;
        for (const auto& old : order)
            if (old->connected && old->data.id == n.id && old->data.role == n.role) {
                // An id appears once per role; a duplicate never shares identity.
                bool taken = false;
                for (const auto& kept : next) if (kept.Get() == old.Get()) { taken = true; break; }
                if (!taken) { e = old; break; }
            }
        if (!e) {
            e.Attach(new AutomationElement(self, std::move(n)));
            ++statistics.created;
            changed = true;
            // A status line appearing on a listed page is news, like a new text.
            if (e->is(Role::status) && listed && !e->data.name.empty()) announced.push_back(e.Get());
        } else {
            if (e->data.parent != n.parent) changed = true;
            if (listening) {
                auto* simple = static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise = [&](PROPERTYID id, VARIANT before, VARIANT after) { UiaRaiseAutomationPropertyChangedEvent(simple, id, before, after); VariantClear(&before); VariantClear(&after); };
                const auto& o = e->data;
                if (o.name != n.name) raise(UIA_NamePropertyId, text(o.name), text(n.name));
                if (o.help != n.help) raise(UIA_HelpTextPropertyId, text(o.help), text(n.help));
                if (o.enabled != n.enabled) raise(UIA_IsEnabledPropertyId, flag(o.enabled), flag(n.enabled));
                if (focusable(o) != focusable(n)) raise(UIA_IsKeyboardFocusablePropertyId, flag(focusable(o)), flag(focusable(n)));
                if (hasFocus(o) != hasFocus(n)) raise(UIA_HasKeyboardFocusPropertyId, flag(hasFocus(o)), flag(hasFocus(n)));
                if (n.role == Role::toggle && o.on != n.on) raise(UIA_ToggleToggleStatePropertyId, integer(toggleState(o.on)), integer(toggleState(n.on)));
                if (n.role == Role::popUp && o.on != n.on) raise(UIA_ExpandCollapseExpandCollapseStatePropertyId, integer(expandState(o.on)), integer(expandState(n.on)));
                if (selectable(n) && o.on != n.on) raise(UIA_SelectionItemIsSelectedPropertyId, flag(o.on), flag(n.on));
                if (n.role == Role::slider && o.number != n.number) raise(UIA_RangeValueValuePropertyId, number(o.number), number(n.number));
                if (n.role == Role::slider && (o.minimum != n.minimum || o.maximum != n.maximum)) {
                    raise(UIA_RangeValueMinimumPropertyId, number(o.minimum), number(n.minimum));
                    raise(UIA_RangeValueMaximumPropertyId, number(o.maximum), number(n.maximum));
                }
                if (valued(n) && o.value != n.value) raise(UIA_ValueValuePropertyId, text(o.value), text(n.value));
            }
            if (selectable(n) && n.on && !e->data.on) selected.push_back(e.Get());
            if (n.role == Role::status && n.name != e->data.name && !n.name.empty()) announced.push_back(e.Get());
            e->data = std::move(n);
        }
        e->index = i;
        if (i >= order.size() || order[i].Get() != e.Get()) changed = true;
        next.push_back(std::move(e));
    }
    for (auto& old : order) {
        bool kept = false;
        for (const auto& n : next) if (n.Get() == old.Get()) { kept = true; break; }
        if (!kept) { old->disconnect(); ++statistics.disconnected; }
    }
    order = std::move(next);
    if (changed) ++statistics.structureChanges;
    statistics.announcements += announced.size();
    if (listening) {
        for (auto* e : selected) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(e), UIA_SelectionItem_ElementSelectedEventId);
        for (auto* e : announced) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(e), UIA_LiveRegionChangedEventId);
    }
    // The keyboard focus moved (a card appeared, Left/Right, recording or
    // editing began): announce the newly focused node once it is in the tree.
    AutomationElement* focus{};
    for (const auto& e : order) if (hasFocus(e->data)) { focus = e.Get(); break; }
    const int runtime = focus ? focus->runtime : 0;
    if (runtime != focusedRuntime) {
        focusedRuntime = runtime;
        if (focus) {
            ++statistics.focusMoves;
            if (listening) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(focus), UIA_AutomationFocusChangedEventId);
        }
    }
    return changed;
}
void AutomationTree::State::contentChanged() noexcept {
    // The action changed the owner outside the host's input routes: the host
    // presents it (and re-lists through its own refresh), then the nodes are
    // re-listed now and the group reports ChildrenInvalidated like the host.
    try { if (host.changed) host.changed(); } catch (...) {}
    if (!alive) return;
    bool changed{};
    try { changed = relist(); } catch (...) { return; }
    if (!changed || !host.root || !UiaClientsAreListening()) return;
    ComPtr<IRawElementProviderSimple> simple;
    if (SUCCEEDED(host.root->QueryInterface(IID_PPV_ARGS(&simple))))
        UiaRaiseStructureChangedEvent(simple.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
}
void AutomationTree::State::disconnectAll() noexcept {
    for (auto& e : order) { e->disconnect(); ++statistics.disconnected; }
    if (!order.empty()) ++statistics.structureChanges;
    order.clear();
    focusedRuntime = 0; // the next listing announces its focus again
}

AutomationTree::AutomationTree(AutomationTreeHost host) : state_(std::make_shared<State>()) {
    state_->host = std::move(host);
    state_->self = state_;
    if (state_->host.root) state_->host.root->AddRef();
}
AutomationTree::~AutomationTree() {
    // A retained element may still lock the state; it must never reach the
    // owner again. The host functions are kept: an action in progress may
    // still be running one of them.
    state_->alive = false;
    state_->disconnectAll();
    if (state_->host.root) { state_->host.root->Release(); state_->host.root = nullptr; }
}
bool AutomationTree::refresh() { return state_->alive && state_->relist(); }
std::size_t AutomationTree::size() const noexcept {
    std::size_t n{};
    for (const auto& e : state_->order) n += e->data.parent == Node::topLevel;
    return n;
}
std::size_t AutomationTree::nodes() const noexcept { return state_->order.size(); }
IRawElementProviderFragmentRoot* AutomationTree::root() const noexcept { return state_->host.root; }
AutomationTreeStats AutomationTree::stats() const noexcept { return state_->statistics; }
long AutomationTree::child(std::size_t index, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    std::size_t seen{};
    for (const auto& e : state_->order)
        if (e->data.parent == Node::topLevel && seen++ == index) return e->QueryInterface(IID_PPV_ARGS(out));
    return E_INVALIDARG;
}
long AutomationTree::focused(IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    for (const auto& e : state_->order) if (hasFocus(e->data)) return e->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
long AutomationTree::fromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
    const auto& s = *state_;
    return guarded([&] {
        // Pre-order reversed: a node's descendants (and later, topmost
        // controls) are tested before it.
        for (auto it = s.order.rbegin(); it != s.order.rend(); ++it)
            if (const auto r = s.screen((*it)->data))
                if (x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) return (*it)->QueryInterface(IID_PPV_ARGS(out));
        return S_OK;
    });
}
void AutomationTree::disconnect() noexcept { state_->disconnectAll(); }

// ---------------------------------------------------------------------------
// The design canvases' view
// ---------------------------------------------------------------------------
std::vector<AutomationNode> automationNodes(std::span<const d::AccessibilityItem> items) {
    std::vector<AutomationNode> out;
    out.reserve(items.size());
    for (const auto& item : items) {
        Node n;
        n.id = item.id; n.name = item.label; n.help = item.help; n.value = item.value; n.rect = item.rect; n.enabled = item.enabled; n.on = item.on;
        switch (item.role) {
        case d::AccessibilityRole::button: n.role = Role::button; break;
        case d::AccessibilityRole::toggle: n.role = Role::toggle; break;
        case d::AccessibilityRole::popUp: n.role = Role::popUp; break;
        case d::AccessibilityRole::menuItem: n.role = Role::menuItem; break;
        case d::AccessibilityRole::tab: n.role = Role::tab; break;
        case d::AccessibilityRole::slider:
            // HUDAccessibilityStandIns: help and value description are the
            // formatted reading.
            n.role = Role::slider; n.number = item.number; n.minimum = item.minimum; n.maximum = std::max(item.minimum, item.maximum); n.step = item.step;
            if (n.help.empty()) n.help = item.value;
            break;
        case d::AccessibilityRole::text: n.role = Role::text; n.valued = true; break;
        case d::AccessibilityRole::edit: n.role = Role::edit; n.focusable = true; n.focused = item.focused; break;
        case d::AccessibilityRole::group: n.role = Role::group; n.valued = true; break;
        }
        out.push_back(std::move(n));
    }
    return out;
}
namespace {
void accumulate(AutomationTreeStats& to, const AutomationTreeStats& from) noexcept {
    to.refreshes += from.refreshes; to.structureChanges += from.structureChanges; to.created += from.created; to.disconnected += from.disconnected;
    to.invokes += from.invokes; to.toggles += from.toggles; to.selections += from.selections; to.values += from.values; to.texts += from.texts;
    to.expansions += from.expansions; to.focusMoves += from.focusMoves; to.focusRequests += from.focusRequests; to.announcements += from.announcements;
}
}
struct NativeDesignAutomation::State {
    DesignAutomationHost host;
    std::unique_ptr<AutomationTree> tree;
    AutomationTreeStats retired; // statistics of trees replaced by a re-rooting
    void make(IRawElementProviderFragmentRoot* root) {
        AutomationTreeHost h;
        h.root = root;
        h.nodes = [this] { return host.items ? automationNodes(host.items()) : std::vector<AutomationNode>{}; };
        h.toScreen = [this](core::Rect r, std::uint8_t) { return host.toScreen ? host.toScreen(r) : std::nullopt; };
        h.invoke = [this](std::string_view id) { return host.invoke && host.invoke(id); };
        h.setValue = [this](std::string_view id, double v) { return host.setValue && host.setValue(id, v); };
        h.setText = [this](std::string_view id, std::string_view t) { return host.setText && host.setText(id, t); };
        h.focus = [this](std::string_view id) { return host.focus && host.focus(id); };
        if (host.expand) h.expand = [this](std::string_view id, bool e) { return host.expand(id, e); };
        h.changed = [this] { if (host.changed) host.changed(); };
        if (tree) { accumulate(retired, tree->stats()); tree.reset(); }
        tree = std::make_unique<AutomationTree>(std::move(h));
    }
};
NativeDesignAutomation::NativeDesignAutomation(DesignAutomationHost host) : state_(std::make_shared<State>()) {
    state_->host = std::move(host);
    state_->make(state_->host.root);
}
NativeDesignAutomation::~NativeDesignAutomation() { state_->tree.reset(); }
bool NativeDesignAutomation::refresh(IRawElementProviderFragmentRoot* group) {
    auto& s = *state_;
    if (group && group != s.tree->root()) { s.make(group); s.host.root = group; }
    return s.tree->refresh();
}
std::size_t NativeDesignAutomation::size() const noexcept { return state_->tree->size(); }
long NativeDesignAutomation::child(std::size_t index, IRawElementProviderFragment** out) const { return state_->tree->child(index, out); }
long NativeDesignAutomation::focused(IRawElementProviderFragment** out) const { return state_->tree->focused(out); }
long NativeDesignAutomation::fromPoint(double x, double y, IRawElementProviderFragment** out) const { return state_->tree->fromPoint(x, y, out); }
void NativeDesignAutomation::disconnect() noexcept { state_->tree->disconnect(); }
DesignAutomationStats NativeDesignAutomation::stats() const noexcept {
    auto s = state_->tree->stats();
    accumulate(s, state_->retired);
    return s;
}
} // namespace endfield::native
#endif
