#include "native/power_automation.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>
#include <string_view>
#include <utility>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
std::wstring wide(std::string_view s) {
    if (s.empty() || s.size() > INT_MAX) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
VARIANT text(std::string_view s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(wide(s).c_str()); return v; }
VARIANT flag(bool b) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return v; }
// Runtime ids of Power peers (UiaAppendRuntimeId + this number).
std::atomic<int> nextRuntime{0x50570001};
// COM entry points never let a C++ exception escape.
template <class F> HRESULT guarded(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
} // namespace

struct NativePowerAutomation::State {
    PowerAutomationHost host;
    ComPtr<PowerAutomationPeer> peer;
    std::optional<PowerAutomationElement> notified; // last state announced to clients
    PowerAutomationStats statistics;
    std::optional<PowerAutomationElement> current() const { return host.element ? host.element() : std::nullopt; }
    std::optional<core::Rect> screen(const PowerAutomationElement& e) const {
        if (!e.rect || !host.toScreen) return std::nullopt;
        const auto out = host.toScreen(*e.rect);
        if (!out || !std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->width) || !std::isfinite(out->height) ||
            out->width <= 0 || out->height <= 0) return std::nullopt;
        return out;
    }
    void announce(PowerAutomationElement next);
};

class PowerAutomationPeer final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IValueProvider {
public:
    using State = NativePowerAutomation::State;
    explicit PowerAutomationPeer(std::weak_ptr<State> s) : state(std::move(s)), runtime(nextRuntime++) {}
    std::weak_ptr<State> state;
    int runtime;
    bool connected{true};
    std::shared_ptr<State> live() const { auto s = state.lock(); return s && connected ? s : nullptr; }
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
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
        if (!live()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_InvokePatternId) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
        else if (id == UIA_ValuePatternId) { *out = static_cast<IValueProvider*>(this); AddRef(); }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto e = s->current().value_or(PowerAutomationElement{});
            switch (id) {
            case UIA_NamePropertyId: *out = text(e.name); break;
            case UIA_HelpTextPropertyId: *out = text(e.help); break;
            case UIA_ValueValuePropertyId: *out = text(e.value); break;
            case UIA_ValueIsReadOnlyPropertyId: *out = flag(true); break;
            case UIA_AutomationIdPropertyId: *out = text("power"); break;
            case UIA_ControlTypePropertyId: out->vt = VT_I4; out->lVal = UIA_ButtonControlTypeId; break;
            case UIA_IsEnabledPropertyId: *out = flag(e.enabled); break;
            // Projected HUD controls are not tab stops (the HUD routes its keys).
            case UIA_IsKeyboardFocusablePropertyId: *out = flag(false); break;
            case UIA_IsOffscreenPropertyId: *out = flag(!s->screen(e)); break;
            case UIA_FrameworkIdPropertyId: *out = text("EndfieldHUD"); break;
            default: break;
            }
            return S_OK;
        });
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    // IRawElementProviderFragment: one leaf under the owner's group.
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_Parent && s->host.root) return s->host.root->QueryInterface(IID_PPV_ARGS(out));
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!live()) return UIA_E_ELEMENTNOTAVAILABLE;
        int ids[2]{UiaAppendRuntimeId, runtime};
        SAFEARRAY* a = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!a) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; ++i) {
            const HRESULT hr = SafeArrayPutElement(a, &i, &ids[i]);
            if (FAILED(hr)) { SafeArrayDestroy(a); return hr; }
        }
        *out = a;
        return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        *out = {};
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            if (const auto e = s->current()) if (const auto r = s->screen(*e)) *out = {r->x, r->y, r->width, r->height};
            return S_OK;
        });
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        if (s->host.root) { s->host.root->AddRef(); *out = s->host.root; }
        return S_OK;
    }
    // IInvokeProvider: HUDNavigationHitButton's press (activateNavigationButton
    // refuses a disabled or hidden button).
    IFACEMETHODIMP Invoke() override {
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto e = s->current();
            if (!e || !e->enabled) return UIA_E_ELEMENTNOTENABLED;
            if (!s->host.invoke) return UIA_E_ELEMENTNOTAVAILABLE;
            const ComPtr<PowerAutomationPeer> self(this); // the press may rebuild the tree
            ++s->statistics.invokes;
            return s->host.invoke() ? S_OK : UIA_E_INVALIDOPERATION;
        });
    }
    // IValueProvider: the read-only accessibilityValue.
    IFACEMETHODIMP SetValue(LPCWSTR) override { return live() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto e = s->current();
            *out = SysAllocString(wide(e ? e->value : std::string{}).c_str());
            return *out ? S_OK : E_OUTOFMEMORY;
        });
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL* out) override {
        if (!out) return E_POINTER;
        *out = TRUE;
        return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    void disconnect() noexcept {
        if (!connected) return;
        connected = false;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    }
private:
    ~PowerAutomationPeer() = default;
    std::atomic<ULONG> refs{1};
};

void NativePowerAutomation::State::announce(PowerAutomationElement next) {
    if (notified && peer && UiaClientsAreListening() != FALSE) {
        auto* simple = static_cast<IRawElementProviderSimple*>(peer.Get());
        const auto raise = [&](PROPERTYID id, VARIANT before, VARIANT after) {
            UiaRaiseAutomationPropertyChangedEvent(simple, id, before, after);
            VariantClear(&before);
            VariantClear(&after);
            ++statistics.propertyEvents;
        };
        if (notified->name != next.name) raise(UIA_NamePropertyId, text(notified->name), text(next.name));
        if (notified->help != next.help) raise(UIA_HelpTextPropertyId, text(notified->help), text(next.help));
        if (notified->value != next.value) raise(UIA_ValueValuePropertyId, text(notified->value), text(next.value));
        if (notified->enabled != next.enabled) raise(UIA_IsEnabledPropertyId, flag(notified->enabled), flag(next.enabled));
    }
    notified = std::move(next);
}

NativePowerAutomation::NativePowerAutomation(PowerAutomationHost host) : state_(std::make_shared<State>()) {
    state_->host = std::move(host);
    if (state_->host.root) state_->host.root->AddRef();
}
NativePowerAutomation::~NativePowerAutomation() {
    disconnect();
    if (state_->host.root) { state_->host.root->Release(); state_->host.root = nullptr; }
}
std::size_t NativePowerAutomation::size() const noexcept { return state_->peer ? 1 : 0; }
PowerAutomationStats NativePowerAutomation::stats() const noexcept { return state_->statistics; }
bool NativePowerAutomation::refresh() {
    auto& s = *state_;
    ++s.statistics.refreshes;
    auto element = s.current();
    if (!element) {
        if (!s.peer) return false;
        disconnect();
        return true;
    }
    if (s.peer) { s.announce(std::move(*element)); return false; }
    s.peer.Attach(new PowerAutomationPeer(state_));
    s.notified = std::move(element);
    ++s.statistics.created;
    ++s.statistics.structureChanges;
    return true;
}
void NativePowerAutomation::notify() {
    auto& s = *state_;
    if (!s.peer) return;
    if (auto element = s.current()) s.announce(std::move(*element));
}
HRESULT NativePowerAutomation::child(std::size_t index, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index != 0 || !state_->peer) return E_INVALIDARG;
    return state_->peer->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativePowerAutomation::fromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto& s = *state_;
    if (!s.peer) return S_OK;
    return guarded([&]() -> HRESULT {
        const auto e = s.current();
        const auto r = e ? s.screen(*e) : std::nullopt;
        if (r && x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) return s.peer->QueryInterface(IID_PPV_ARGS(out));
        return S_OK;
    });
}
void NativePowerAutomation::disconnect() noexcept {
    auto& s = *state_;
    if (!s.peer) return;
    s.peer->disconnect();
    s.peer.Reset();
    s.notified.reset();
    ++s.statistics.disconnected;
    ++s.statistics.structureChanges;
}
} // namespace endfield::native
#endif
