#include "native/app_shortcut_automation.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
std::wstring wide(std::string_view s) {
    if (s.empty() || s.size() > INT_MAX) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::optional<std::string> narrow(const wchar_t* s) {
    if (!s) return std::string{};
    const auto length = wcslen(s);
    if (!length) return std::string{};
    if (length > INT_MAX) return std::nullopt;
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::nullopt;
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, static_cast<int>(length), out.data(), n, nullptr, nullptr);
    return out;
}
std::atomic<int> nextRuntime{1};
VARIANT text(std::string_view s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(wide(s).c_str()); return v; }
VARIANT flag(bool b) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return v; }
} // namespace

struct NativeAppShortcutAutomation::State {
    AppShortcutAutomationHost host; std::vector<ComPtr<AppShortcutAutomationElement>> order; AppShortcutAutomationStats statistics;
    std::optional<core::Rect> screen(core::Rect r) const {
        if (!host.toScreen) return std::nullopt;
        const auto out = host.toScreen(r);
        if (!out || !std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->width) || !std::isfinite(out->height) || out->width <= 0 || out->height <= 0) return std::nullopt;
        return out;
    }
};

class AppShortcutAutomationElement final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IValueProvider {
public:
    using State = NativeAppShortcutAutomation::State;
    AppShortcutAutomationElement(std::weak_ptr<State> s, AppShortcutAutomationControl c) : state(std::move(s)), control(std::move(c)), runtime(nextRuntime++) {}
    std::weak_ptr<State> state; AppShortcutAutomationControl control; int runtime; bool connected{true};
    bool edit() const noexcept { return control.kind == AppShortcutAutomationKind::edit; }
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider) && !edit()) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IValueProvider) && edit()) *out = static_cast<IValueProvider*>(this);
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
        if (id == UIA_InvokePatternId && !edit()) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
        else if (id == UIA_ValuePatternId && edit()) { *out = static_cast<IValueProvider*>(this); AddRef(); }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        switch (id) {
        case UIA_NamePropertyId: *out = text(control.label); break;
        case UIA_HelpTextPropertyId: if (!control.help.empty()) *out = text(control.help); break;
        case UIA_AutomationIdPropertyId: *out = text(control.id); break;
        case UIA_ControlTypePropertyId: out->vt = VT_I4; out->lVal = edit() ? UIA_EditControlTypeId : UIA_ButtonControlTypeId; break;
        case UIA_IsEnabledPropertyId: *out = flag(true); break;
        // The name field is the first responder while it exists; projected
        // buttons are not tab stops.
        case UIA_IsKeyboardFocusablePropertyId: case UIA_HasKeyboardFocusPropertyId: *out = flag(edit()); break;
        case UIA_IsOffscreenPropertyId: { const auto s = state.lock(); *out = flag(!s || !s->screen(control.rect)); break; }
        case UIA_FrameworkIdPropertyId: *out = text("EndfieldHUD"); break;
        case UIA_ValueValuePropertyId: if (edit()) *out = text(control.value); break;
        default: break;
        }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_Parent) return s->host.root ? s->host.root->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
        if (direction != NavigateDirection_NextSibling && direction != NavigateDirection_PreviousSibling) return S_OK;
        for (std::size_t i = 0; i < s->order.size(); ++i) if (s->order[i].Get() == this) {
            const bool next = direction == NavigateDirection_NextSibling;
            if (next && i + 1 < s->order.size()) return s->order[i + 1]->QueryInterface(IID_PPV_ARGS(out));
            if (!next && i > 0) return s->order[i - 1]->QueryInterface(IID_PPV_ARGS(out));
            break;
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
        if (const auto r = s->screen(control.rect)) *out = {r->x, r->y, r->width, r->height};
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return connected ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (s->host.root) { s->host.root->AddRef(); *out = s->host.root; }
        return S_OK;
    }
    // IInvokeProvider: HUDAppShortcutInteraction.activateAction.
    IFACEMETHODIMP Invoke() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto s = state.lock();
        if (!s || !s->host.invoke) return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may rebuild the tree; keep this element and its id alive.
        const ComPtr<AppShortcutAutomationElement> self(this);
        const std::string id = control.id;
        ++s->statistics.invokes;
        return s->host.invoke(id) ? S_OK : UIA_E_INVALIDOPERATION;
    }
    // IValueProvider: the name field's text (NSTextView accessibilityValue).
    IFACEMETHODIMP SetValue(LPCWSTR value) override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto s = state.lock();
        if (!s || !s->host.setText) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto utf8 = narrow(value);
        if (!utf8) return E_INVALIDARG;
        const ComPtr<AppShortcutAutomationElement> self(this);
        const std::string id = control.id;
        ++s->statistics.values;
        return s->host.setText(id, *utf8) ? S_OK : UIA_E_INVALIDOPERATION;
    }
    IFACEMETHODIMP get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = SysAllocString(wide(control.value).c_str());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL* out) override {
        if (!out) return E_POINTER;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto s = state.lock();
        *out = s && s->host.setText ? FALSE : TRUE;
        return S_OK;
    }
    void disconnect() noexcept { if (!connected) return; connected = false; UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this)); }
private:
    ~AppShortcutAutomationElement() = default;
    std::atomic<ULONG> refs{1};
};

NativeAppShortcutAutomation::NativeAppShortcutAutomation(AppShortcutAutomationHost host) : state_(std::make_shared<State>()) {
    state_->host = std::move(host);
    if (state_->host.root) state_->host.root->AddRef();
}
NativeAppShortcutAutomation::~NativeAppShortcutAutomation() {
    disconnect();
    if (state_->host.root) { state_->host.root->Release(); state_->host.root = nullptr; }
}
std::size_t NativeAppShortcutAutomation::size() const noexcept { return state_->order.size(); }
AppShortcutAutomationStats NativeAppShortcutAutomation::stats() const noexcept { return state_->statistics; }
bool NativeAppShortcutAutomation::refresh() {
    auto& s = *state_;
    ++s.statistics.refreshes;
    auto list = s.host.controls ? s.host.controls() : std::vector<AppShortcutAutomationControl>{};
    const bool listening = UiaClientsAreListening() != FALSE;
    std::vector<ComPtr<AppShortcutAutomationElement>> next;
    next.reserve(list.size());
    bool changed = list.size() != s.order.size();
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto& c = list[i];
        ComPtr<AppShortcutAutomationElement> e;
        for (const auto& old : s.order) if (old->connected && old->control.id == c.id && old->control.kind == c.kind) {
            // An id appears once per role; a duplicate never shares identity.
            bool taken = false;
            for (const auto& n : next) if (n.Get() == old.Get()) { taken = true; break; }
            if (!taken) { e = old; break; }
        }
        if (!e) { e.Attach(new AppShortcutAutomationElement(state_, std::move(c))); ++s.statistics.created; changed = true; }
        else {
            if (listening) {
                auto* simple = static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise = [&](PROPERTYID id, VARIANT before, VARIANT after) { UiaRaiseAutomationPropertyChangedEvent(simple, id, before, after); VariantClear(&before); VariantClear(&after); };
                if (e->control.label != c.label) raise(UIA_NamePropertyId, text(e->control.label), text(c.label));
                if (e->control.help != c.help) raise(UIA_HelpTextPropertyId, text(e->control.help), text(c.help));
                if (e->control.value != c.value) raise(UIA_ValueValuePropertyId, text(e->control.value), text(c.value));
            }
            e->control = std::move(c);
        }
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
    return changed;
}
HRESULT NativeAppShortcutAutomation::child(std::size_t index, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index >= state_->order.size()) return E_INVALIDARG;
    return state_->order[index]->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativeAppShortcutAutomation::fromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto& s = *state_;
    for (auto it = s.order.rbegin(); it != s.order.rend(); ++it) if (const auto r = s.screen((*it)->control.rect))
        if (x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) return (*it)->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
void NativeAppShortcutAutomation::disconnect() noexcept {
    auto& s = *state_;
    for (auto& e : s.order) { e->disconnect(); ++s.statistics.disconnected; }
    if (!s.order.empty()) ++s.statistics.structureChanges;
    s.order.clear();
}
} // namespace endfield::native
#endif
