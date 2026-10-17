#include "native/mac_import_automation.hpp"
#ifdef _WIN32
#include <ole2.h> // VARIANT/BSTR helpers even under WIN32_LEAN_AND_MEAN
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
using Node = MacImportAutomationNode;
using Role = Node::Role;
// UIA_IsDialogPropertyId (Windows 10 1809); older systems ignore the query.
constexpr PROPERTYID isDialogProperty = 30174;
std::wstring wide(std::string_view s) {
    if (s.empty() || s.size() > INT_MAX) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::atomic<int> nextRuntime{0x4d490001};
VARIANT text(std::string_view s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(wide(s).c_str()); return v; }
VARIANT flag(bool b) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return v; }
VARIANT number(double d) { VARIANT v; VariantInit(&v); v.vt = VT_R8; v.dblVal = d; return v; }
template <class F> HRESULT guarded(F&& f) noexcept { try { return f(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; } }
long controlType(Role role) noexcept {
    switch (role) {
    case Role::dialog: return UIA_PaneControlTypeId;
    case Role::text: return UIA_TextControlTypeId;
    case Role::progress: return UIA_ProgressBarControlTypeId;
    case Role::list: return UIA_ListControlTypeId;
    case Role::item: return UIA_ListItemControlTypeId;
    case Role::button: return UIA_ButtonControlTypeId;
    }
    return UIA_TextControlTypeId;
}
}

class MacImportAutomationElement;
struct NativeMacImportAutomation::State {
    MacImportAutomationHost host;
    std::vector<ComPtr<MacImportAutomationElement>> order;
    MacImportAutomationStats statistics;
    int focusedRuntime{}; // the part that last received the focus event (0: none)
    std::optional<core::Rect> screen(const std::optional<core::Rect>& r) const {
        if (!r || !host.toScreen) return std::nullopt;
        const auto out = host.toScreen(*r);
        if (!out || !std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->width) || !std::isfinite(out->height) || out->width <= 0 || out->height <= 0)
            return std::nullopt;
        return out;
    }
};

class MacImportAutomationElement final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IRangeValueProvider {
public:
    using State = NativeMacImportAutomation::State;
    MacImportAutomationElement(std::weak_ptr<State> s, Node n) : state(std::move(s)), data(std::move(n)), runtime(nextRuntime++) {}
    std::weak_ptr<State> state;
    Node data;
    std::size_t index{};   // position in State::order (refreshed with it)
    int runtime;
    bool connected{true};
    bool button() const noexcept { return data.role == Role::button; }
    bool progress() const noexcept { return data.role == Role::progress; }
    bool focusable() const noexcept { return button() && data.enabled; }
    bool hasFocus() const noexcept { return focusable() && data.focused; }
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider) && button()) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IRangeValueProvider) && progress()) *out = static_cast<IRangeValueProvider*>(this);
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
        if (id == UIA_InvokePatternId && button()) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
        else if (id == UIA_RangeValuePatternId && progress()) { *out = static_cast<IRangeValueProvider*>(this); AddRef(); }
        return S_OK;
    }
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
        case UIA_AutomationIdPropertyId: out = text("macImport." + data.id); break;
        case UIA_ControlTypePropertyId: out.vt = VT_I4; out.lVal = controlType(data.role); break;
        case UIA_IsEnabledPropertyId: out = flag(data.enabled); break;
        case UIA_IsKeyboardFocusablePropertyId: out = flag(focusable()); break;
        case UIA_HasKeyboardFocusPropertyId: out = flag(hasFocus()); break;
        case UIA_IsOffscreenPropertyId: { const auto s = state.lock(); out = flag(!s || !s->screen(data.rect)); break; }
        case UIA_FrameworkIdPropertyId: out = text("EndfieldHUD"); break;
        case isDialogProperty: if (data.role == Role::dialog) out = flag(true); break;
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
        return guarded([&] { if (const auto r = s->screen(data.rect)) *out = {r->x, r->y, r->width, r->height}; return S_OK; });
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    // A client moves the keyboard focus (Mac accessibility focus): the card's
    // own focus follows through the host, exactly like Tab or the arrows.
    // The dialog holds the focus of its button; text, list, rows and the
    // progress bar are not keyboard focusable.
    IFACEMETHODIMP SetFocus() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (data.role == Role::dialog) return S_OK;
        if (!button()) return UIA_E_INVALIDOPERATION;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        const auto s = state.lock();
        if (!s || !s->host.focus) return UIA_E_ELEMENTNOTAVAILABLE;
        // The move may refresh the tree; keep this element and its id alive.
        const ComPtr<MacImportAutomationElement> self(this);
        ++s->statistics.focusRequests;
        return guarded([&] { const std::string id = data.id; return s->host.focus(id) ? S_OK : UIA_E_INVALIDOPERATION; });
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (s->host.root) { s->host.root->AddRef(); *out = s->host.root; }
        return S_OK;
    }
    // IInvokeProvider: the card button's click action.
    IFACEMETHODIMP Invoke() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!data.enabled) return UIA_E_ELEMENTNOTENABLED;
        const auto s = state.lock();
        if (!s || !s->host.invoke) return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may refresh the tree; keep this element and its id alive.
        const ComPtr<MacImportAutomationElement> self(this);
        ++s->statistics.invokes;
        return guarded([&] { const std::string id = data.id; return s->host.invoke(id) ? S_OK : UIA_E_INVALIDOPERATION; });
    }
    // IRangeValueProvider: the bounded job's progress, read-only.
    IFACEMETHODIMP SetValue(double) override { return connected ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_Value(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = data.value; return S_OK; }
    IFACEMETHODIMP get_IsReadOnly(BOOL* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = TRUE; return S_OK; }
    IFACEMETHODIMP get_Maximum(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = 100; return S_OK; }
    IFACEMETHODIMP get_Minimum(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = 0; return S_OK; }
    IFACEMETHODIMP get_LargeChange(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = 0; return S_OK; }
    IFACEMETHODIMP get_SmallChange(double* out) override { if (!out) return E_POINTER; if (!connected) return UIA_E_ELEMENTNOTAVAILABLE; *out = 0; return S_OK; }
    void disconnect() noexcept { if (!connected) return; connected = false; UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this)); }
private:
    ~MacImportAutomationElement() = default;
    std::atomic<ULONG> refs{1};
};

NativeMacImportAutomation::NativeMacImportAutomation(MacImportAutomationHost host) : state_(std::make_shared<State>()) {
    state_->host = std::move(host);
    if (state_->host.root) state_->host.root->AddRef();
}
NativeMacImportAutomation::~NativeMacImportAutomation() {
    disconnect();
    if (state_->host.root) { state_->host.root->Release(); state_->host.root = nullptr; }
}
std::size_t NativeMacImportAutomation::size() const noexcept {
    std::size_t n{};
    for (const auto& e : state_->order) n += e->data.parent == Node::topLevel;
    return n;
}
std::size_t NativeMacImportAutomation::parts() const noexcept { return state_->order.size(); }
MacImportAutomationStats NativeMacImportAutomation::stats() const noexcept { return state_->statistics; }
bool NativeMacImportAutomation::refresh() {
    auto& s = *state_;
    ++s.statistics.refreshes;
    auto list = s.host.nodes ? s.host.nodes() : std::vector<Node>{};
    // Pre-order with valid parents only; anything else lists nothing.
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].parent != Node::topLevel && list[i].parent >= i) { list.clear(); break; }
    const bool listening = UiaClientsAreListening() != FALSE;
    std::vector<ComPtr<MacImportAutomationElement>> next;
    next.reserve(list.size());
    bool changed = list.size() != s.order.size();
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto& n = list[i];
        ComPtr<MacImportAutomationElement> e;
        for (const auto& old : s.order)
            if (old->connected && old->data.id == n.id && old->data.role == n.role) {
                bool taken = false;
                for (const auto& kept : next) if (kept.Get() == old.Get()) { taken = true; break; }
                if (!taken) { e = old; break; }
            }
        if (!e) { e.Attach(new MacImportAutomationElement(state_, std::move(n))); ++s.statistics.created; changed = true; }
        else {
            if (e->data.parent != n.parent) changed = true;
            if (listening) {
                auto* simple = static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise = [&](PROPERTYID id, VARIANT before, VARIANT after) { UiaRaiseAutomationPropertyChangedEvent(simple, id, before, after); VariantClear(&before); VariantClear(&after); };
                if (e->data.name != n.name) raise(UIA_NamePropertyId, text(e->data.name), text(n.name));
                if (e->data.help != n.help) raise(UIA_HelpTextPropertyId, text(e->data.help), text(n.help));
                if (e->data.enabled != n.enabled) raise(UIA_IsEnabledPropertyId, flag(e->data.enabled), flag(n.enabled));
                if (e->data.role == Role::button) {
                    const bool before = e->data.enabled && e->data.focused, after = n.enabled && n.focused;
                    if (before != after) raise(UIA_HasKeyboardFocusPropertyId, flag(before), flag(after));
                    if (e->data.enabled != n.enabled) raise(UIA_IsKeyboardFocusablePropertyId, flag(e->data.enabled), flag(n.enabled));
                }
                if (e->data.role == Role::progress && e->data.value != n.value) raise(UIA_RangeValueValuePropertyId, number(e->data.value), number(n.value));
                if (e->data.rect.has_value() != n.rect.has_value()) raise(UIA_IsOffscreenPropertyId, flag(!e->data.rect), flag(!n.rect));
            }
            e->data = std::move(n);
        }
        e->index = i;
        if (i >= s.order.size() || s.order[i].Get() != e.Get()) changed = true;
        next.push_back(std::move(e));
    }
    for (auto& old : s.order) {
        bool kept = false;
        for (const auto& n : next) if (n.Get() == old.Get()) { kept = true; break; }
        if (!kept) { old->disconnect(); ++s.statistics.disconnected; }
    }
    s.order = std::move(next);
    if (changed) ++s.statistics.structureChanges;
    // Keyboard focus moved (the card appeared, Tab, an arrow, a press,
    // SetFocus): announce the newly focused part once it is in the tree.
    MacImportAutomationElement* focus{};
    for (const auto& e : s.order) if (e->hasFocus()) { focus = e.Get(); break; }
    const int runtime = focus ? focus->runtime : 0;
    if (runtime != s.focusedRuntime) {
        s.focusedRuntime = runtime;
        if (focus) {
            ++s.statistics.focusMoves;
            if (listening) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(focus), UIA_AutomationFocusChangedEventId);
        }
    }
    return changed;
}
HRESULT NativeMacImportAutomation::focused(IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    for (const auto& e : state_->order) if (e->hasFocus()) return e->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
HRESULT NativeMacImportAutomation::child(std::size_t index, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    std::size_t seen{};
    for (const auto& e : state_->order)
        if (e->data.parent == Node::topLevel && seen++ == index) return e->QueryInterface(IID_PPV_ARGS(out));
    return E_INVALIDARG;
}
HRESULT NativeMacImportAutomation::fromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
    const auto& s = *state_;
    return guarded([&] {
        // Pre-order reversed: a part's descendants are tested before it.
        for (auto it = s.order.rbegin(); it != s.order.rend(); ++it)
            if (const auto r = s.screen((*it)->data.rect))
                if (x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) return (*it)->QueryInterface(IID_PPV_ARGS(out));
        return S_OK;
    });
}
void NativeMacImportAutomation::disconnect() noexcept {
    auto& s = *state_;
    for (auto& e : s.order) { e->disconnect(); ++s.statistics.disconnected; }
    if (!s.order.empty()) ++s.statistics.structureChanges;
    s.order.clear();
    s.focusedRuntime = 0; // the next connected card announces its focus again
}
}
#endif
