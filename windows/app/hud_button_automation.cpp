#include "app/hud_button_automation.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>
#include <utility>

namespace endfield::app {
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
// Runtime ids of the host buttons (UiaAppendRuntimeId + this number).
std::atomic<int> nextRuntime{0x48420001};
// COM entry points never let a C++ exception escape.
template <class F> HRESULT guarded(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
} // namespace

class HudButtonElement;
struct HudButtonAutomation::State {
    HudButtonSource source;
    IRawElementProviderFragmentRoot* root{}; // the owner's group (AddRef'd while held)
    std::vector<HudButtonPeer> peers, scratch;
    std::vector<ComPtr<HudButtonElement>> elements; // parallel to peers
    std::vector<bool> enabled;                       // last announced IsEnabled
    HudButtonStats statistics;
    std::optional<std::size_t> index(const HudButtonElement* element) const noexcept {
        for (std::size_t n = 0; n < elements.size(); ++n) if (elements[n].Get() == element) return n;
        return std::nullopt;
    }
    bool isEnabled(const HudButtonPeer& peer) const { return source.enabled && source.enabled(peer) && screen(peer).has_value(); }
    // Logical client rect -> physical screen pixels of the HUD window.
    std::optional<core::Rect> screen(const HudButtonPeer& peer) const {
        if (!source.frame || !source.window || !source.scale) return std::nullopt;
        const auto client = source.frame(peer);
        const double scale = source.scale();
        POINT origin{};
        if (!client || !std::isfinite(scale) || !(scale > 0) || !ClientToScreen(static_cast<HWND>(source.window), &origin)) return std::nullopt;
        const core::Rect out{origin.x + client->x * scale, origin.y + client->y * scale, client->width * scale, client->height * scale};
        if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.width) || !std::isfinite(out.height) || !(out.width > 0) || !(out.height > 0)) return std::nullopt;
        return out;
    }
    void disconnect() noexcept;
};

class HudButtonElement final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IValueProvider {
public:
    using State = HudButtonAutomation::State;
    explicit HudButtonElement(std::weak_ptr<State> s) : state(std::move(s)), runtime(nextRuntime++) {}
    std::weak_ptr<State> state;
    int runtime;
    bool connected{true};
    std::shared_ptr<State> live() const { auto s = state.lock(); return s && connected ? s : nullptr; }
    // The element's current snapshot entry (its index in the owner's list).
    std::optional<HudButtonPeer> peer(const State& s) const {
        const auto n = s.index(this);
        return n && *n < s.peers.size() ? std::optional(s.peers[*n]) : std::nullopt;
    }
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
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID id, IUnknown** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto p = peer(*s);
            if (id == UIA_InvokePatternId) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
            else if (id == UIA_ValuePatternId && p && p->hasValue) { *out = static_cast<IValueProvider*>(this); AddRef(); }
            return S_OK;
        });
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto p = peer(*s);
            if (!p) return S_OK;
            switch (id) {
            case UIA_NamePropertyId: *out = text(p->name); break;
            case UIA_HelpTextPropertyId: if (!p->help.empty()) *out = text(p->help); break;
            case UIA_AutomationIdPropertyId: *out = text(p->id); break;
            case UIA_ControlTypePropertyId: out->vt = VT_I4; out->lVal = UIA_ButtonControlTypeId; break;
            case UIA_IsEnabledPropertyId: *out = flag(s->isEnabled(*p)); break;
            // Projected HUD controls are not tab stops (the HUD routes its own keys).
            case UIA_IsKeyboardFocusablePropertyId: case UIA_HasKeyboardFocusPropertyId: *out = flag(false); break;
            case UIA_IsOffscreenPropertyId: *out = flag(!s->screen(*p)); break;
            case UIA_IsValuePatternAvailablePropertyId: *out = flag(p->hasValue); break;
            case UIA_ValueValuePropertyId: if (p->hasValue) *out = text(p->value); break;
            case UIA_ValueIsReadOnlyPropertyId: if (p->hasValue) *out = flag(true); break;
            case UIA_FrameworkIdPropertyId: *out = text("EndfieldHUD"); break;
            default: break;
            }
            return S_OK;
        });
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            if (direction == NavigateDirection_Parent) return s->root ? s->root->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
            const auto n = s->index(this);
            if (!n) return S_OK;
            if (direction == NavigateDirection_NextSibling && *n + 1 < s->elements.size()) return s->elements[*n + 1]->QueryInterface(IID_PPV_ARGS(out));
            if (direction == NavigateDirection_PreviousSibling && *n > 0) return s->elements[*n - 1]->QueryInterface(IID_PPV_ARGS(out));
            return S_OK; // leaves: no children
        });
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
            if (const auto p = peer(*s)) if (const auto r = s->screen(*p)) *out = {r->x, r->y, r->width, r->height};
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
        if (s->root) { s->root->AddRef(); *out = s->root; }
        return S_OK;
    }
    // accessibilityPerformPress: a hidden or disabled button refuses the press;
    // otherwise the click's own route runs.
    IFACEMETHODIMP Invoke() override {
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto p = peer(*s);
            if (!p) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!s->isEnabled(*p)) return UIA_E_ELEMENTNOTENABLED;
            if (!s->source.invoke) return UIA_E_INVALIDOPERATION;
            const ComPtr<HudButtonElement> self(this); // the press may re-list the buttons
            ++s->statistics.invokes;
            if (!s->source.invoke(*p)) return UIA_E_INVALIDOPERATION;
            if (s->source.changed) s->source.changed();
            return S_OK;
        });
    }
    IFACEMETHODIMP SetValue(LPCWSTR) override { return live() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto p = peer(*s);
            *out = SysAllocString(wide(p ? p->value : std::string{}).c_str());
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
    ~HudButtonElement() = default;
    std::atomic<ULONG> refs{1};
};

void HudButtonAutomation::State::disconnect() noexcept {
    for (auto& e : elements) if (e) { e->disconnect(); ++statistics.disconnected; }
    elements.clear();
    peers.clear();
    enabled.clear();
}

HudButtonAutomation::HudButtonAutomation(HudButtonSource source) : state_(std::make_shared<State>()) { state_->source = std::move(source); }
HudButtonAutomation::~HudButtonAutomation() {
    state_->disconnect();
    if (state_->root) { state_->root->Release(); state_->root = nullptr; }
}
std::string_view HudButtonAutomation::name() const noexcept { return state_->source.name; }
HudButtonStats HudButtonAutomation::stats() const noexcept { return state_->statistics; }
std::size_t HudButtonAutomation::automationCount() const { return state_->elements.size(); }
bool HudButtonAutomation::automationRefresh(IRawElementProviderFragmentRoot* root) {
    auto& s = *state_;
    ++s.statistics.refreshes;
    bool structure = false;
    if (root != s.root) {
        // A new group (another root provider): fresh elements under it.
        structure = !s.elements.empty();
        s.disconnect();
        if (root) root->AddRef();
        if (s.root) s.root->Release();
        s.root = root;
    }
    s.scratch.clear();
    if (s.source.peers) s.source.peers(s.scratch);
    std::vector<ComPtr<HudButtonElement>> next(s.scratch.size());
    std::vector<bool> enabled(s.scratch.size());
    const bool listening = UiaClientsAreListening() != FALSE;
    std::vector<bool> kept(s.elements.size());
    for (std::size_t n = 0; n < s.scratch.size(); ++n) {
        const auto& peer = s.scratch[n];
        enabled[n] = s.isEnabled(peer);
        for (std::size_t old = 0; old < s.peers.size(); ++old) {
            if (kept[old] || s.peers[old].id != peer.id) continue;
            kept[old] = true;
            next[n] = s.elements[old];
            if (listening) {
                auto* simple = static_cast<IRawElementProviderSimple*>(next[n].Get());
                const auto raise = [&](PROPERTYID id, VARIANT before, VARIANT after) {
                    UiaRaiseAutomationPropertyChangedEvent(simple, id, before, after);
                    VariantClear(&before); VariantClear(&after);
                    ++s.statistics.propertyEvents;
                };
                const auto& was = s.peers[old];
                if (was.name != peer.name) raise(UIA_NamePropertyId, text(was.name), text(peer.name));
                if (was.help != peer.help) raise(UIA_HelpTextPropertyId, text(was.help), text(peer.help));
                if (was.value != peer.value && peer.hasValue) raise(UIA_ValueValuePropertyId, text(was.value), text(peer.value));
                if (old < s.enabled.size() && s.enabled[old] != enabled[n]) raise(UIA_IsEnabledPropertyId, flag(s.enabled[old]), flag(enabled[n]));
            }
            if (old != n) structure = true;
            break;
        }
        if (!next[n]) { next[n].Attach(new HudButtonElement(state_)); ++s.statistics.created; structure = true; }
    }
    for (std::size_t old = 0; old < s.elements.size(); ++old)
        if (!kept[old] && s.elements[old]) { s.elements[old]->disconnect(); ++s.statistics.disconnected; structure = true; }
    s.peers.swap(s.scratch);
    s.elements = std::move(next);
    s.enabled = std::move(enabled);
    return structure;
}
long HudButtonAutomation::automationChild(std::size_t index, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index >= state_->elements.size() || !state_->elements[index]) return E_INVALIDARG;
    return state_->elements[index]->QueryInterface(IID_PPV_ARGS(out));
}
long HudButtonAutomation::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto& s = *state_;
    if (!std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
    return guarded([&]() -> HRESULT {
        // Later buttons are listed above earlier ones.
        for (std::size_t n = s.elements.size(); n-- > 0;) {
            if (n >= s.peers.size() || !s.elements[n]) continue;
            const auto r = s.screen(s.peers[n]);
            if (r && x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) return s.elements[n]->QueryInterface(IID_PPV_ARGS(out));
        }
        return S_OK;
    });
}
void HudButtonAutomation::automationDisconnect() noexcept { state_->disconnect(); }
} // namespace endfield::app
