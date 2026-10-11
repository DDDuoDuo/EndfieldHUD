#include "native/charge_alert_automation.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>
#include <optional>
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
// Runtime ids of alert peers (UiaAppendRuntimeId + this number).
std::atomic<int> nextRuntime{0x43410001};
template <class F> HRESULT guarded(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
bool usable(const core::Rect& r) noexcept {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) && r.width > 0 && r.height > 0;
}
bool inside(const core::Rect& r, double x, double y) noexcept { return usable(r) && x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height; }
// Children in the panel's subview order: indicator, cancel, confirm.
enum class Role : std::uint8_t { indicator, discard, confirm };
} // namespace

struct ChargeAlertAutomation::State {
    ChargeAlertAutomationHost host;
    ComPtr<ChargeAlertRoot> root;
    ComPtr<ChargeAlertElement> indicator;
    std::array<ComPtr<ChargeAlertElement>, 2> buttons;
    std::optional<ChargeAlertAutomationView> notified; // last state announced to clients
    ChargeAlertAutomationStats statistics;
    bool alive{true};
    ChargeAlertAutomationView current() const { return host.view ? host.view() : ChargeAlertAutomationView{}; }
    std::size_t count() const noexcept { return indicator ? 1 + (buttons[0] ? 2 : 0) : 0; }
    ChargeAlertElement* at(std::size_t index) const noexcept {
        if (index == 0) return indicator.Get();
        return index <= 2 && buttons[0] ? buttons[index - 1].Get() : nullptr;
    }
    HRESULT child(std::size_t index, IRawElementProviderFragment** out) const;
    void sync(ChargeAlertAutomationView next);
};

class ChargeAlertRoot final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    using State = ChargeAlertAutomation::State;
    explicit ChargeAlertRoot(std::weak_ptr<State> s) : state(std::move(s)) {}
    std::weak_ptr<State> state;
    bool connected{true};
    std::shared_ptr<State> live() const { auto s = state.lock(); return s && s->alive && connected ? s : nullptr; }
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
    IFACEMETHODIMP_(ULONG) Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** out) override { if (!out) return E_POINTER; *out = nullptr; return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    // Name (the window text), control type and bounds come from the HWND host.
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        if (!live()) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            if (id == UIA_AutomationIdPropertyId) *out = text("chargeAlert");
            else if (id == UIA_FrameworkIdPropertyId) *out = text("EndfieldHUD");
            return S_OK;
        });
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s || !IsWindow(s->host.window)) return UIA_E_ELEMENTNOTAVAILABLE;
        return UiaHostProviderFromHwnd(s->host.window, out);
    }
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_FirstChild) return s->child(0, out);
        if (direction == NavigateDirection_LastChild) return s->count() ? s->child(s->count() - 1, out) : S_OK;
        return S_OK; // parent and siblings come from the HWND host
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override { if (!out) return E_POINTER; *out = {}; return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!live()) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        AddRef();
        return S_OK;
    }
    // The edit buttons are subviews added after the indicator and overlap its
    // lower edge, so they answer first; outside both the window itself answers.
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto v = s->current();
            if (v.editing && s->buttons[0])
                for (std::size_t b = 0; b < 2; ++b)
                    if (inside(v.buttons[b], x, y)) return s->child(b + 1, out);
            if (inside(v.indicator, x, y)) return s->child(0, out);
            return S_OK;
        });
    }
    // The editor's keys go to the window itself (no focusable child).
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override { if (!out) return E_POINTER; *out = nullptr; return live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    void disconnect() noexcept {
        if (!connected) return;
        connected = false;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    }
private:
    ~ChargeAlertRoot() = default;
    std::atomic<ULONG> refs{1};
};

class ChargeAlertElement final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider {
public:
    using State = ChargeAlertAutomation::State;
    ChargeAlertElement(std::weak_ptr<State> s, Role r) : state(std::move(s)), role(r), runtime(nextRuntime++) {}
    std::weak_ptr<State> state;
    Role role;
    int runtime;
    bool connected{true};
    std::shared_ptr<State> live() const { auto s = state.lock(); return s && s->alive && connected ? s : nullptr; }
    bool button() const noexcept { return role != Role::indicator; }
    std::size_t index() const noexcept { return static_cast<std::size_t>(role) - 1; }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider) && button()) *out = static_cast<IInvokeProvider*>(this);
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
        if (!live()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_InvokePatternId && button()) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            const auto v = s->current();
            switch (id) {
            case UIA_NamePropertyId: *out = text(button() ? v.names[index()] : v.label); break;
            case UIA_HelpTextPropertyId: if (button()) *out = text(v.help[index()]); break;
            case UIA_AutomationIdPropertyId:
                *out = text(role == Role::indicator ? "chargeAlert:indicator" : role == Role::discard ? "chargeAlert:discard" : "chargeAlert:confirm");
                break;
            case UIA_ControlTypePropertyId: out->vt = VT_I4; out->lVal = button() ? UIA_ButtonControlTypeId : UIA_TextControlTypeId; break;
            case UIA_IsEnabledPropertyId: *out = flag(!button() || v.editing); break;
            // The editor routes Return/Escape itself; the buttons are no tab stops.
            case UIA_IsKeyboardFocusablePropertyId: *out = flag(false); break;
            case UIA_HasKeyboardFocusPropertyId: *out = flag(false); break;
            case UIA_IsOffscreenPropertyId: *out = flag(!v.shown); break;
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
        const std::size_t self = role == Role::indicator ? 0 : index() + 1;
        switch (direction) {
        case NavigateDirection_Parent: return s->root ? s->root->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
        case NavigateDirection_NextSibling: return self + 1 < s->count() ? s->child(self + 1, out) : S_OK;
        case NavigateDirection_PreviousSibling: return self > 0 && self - 1 < s->count() ? s->child(self - 1, out) : S_OK;
        default: return S_OK; // leaves
        }
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
            const auto v = s->current();
            const auto& r = button() ? v.buttons[index()] : v.indicator;
            if (usable(r)) *out = {r.x, r.y, r.width, r.height};
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
        return s->root ? s->root->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
    }
    // NSButton.performClick -> discardPosition / confirmPosition, queued by
    // the panel and run from its window procedure (never inside this call).
    IFACEMETHODIMP Invoke() override {
        const auto s = live();
        if (!s || !button()) return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]() -> HRESULT {
            if (!s->current().editing) return UIA_E_ELEMENTNOTENABLED;
            if (!s->host.invoke) return UIA_E_ELEMENTNOTAVAILABLE;
            const ComPtr<ChargeAlertElement> keep(this);
            ++s->statistics.invokes;
            return s->host.invoke(index()) ? S_OK : UIA_E_INVALIDOPERATION;
        });
    }
    void disconnect() noexcept {
        if (!connected) return;
        connected = false;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    }
private:
    ~ChargeAlertElement() = default;
    std::atomic<ULONG> refs{1};
};

HRESULT ChargeAlertAutomation::State::child(std::size_t index, IRawElementProviderFragment** out) const {
    auto* element = at(index);
    return element ? element->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
}

void ChargeAlertAutomation::State::sync(ChargeAlertAutomationView next) {
    if (!alive || !root) return;
    const bool listening = UiaClientsAreListening() != FALSE;
    // PositionActionButton.isHidden follows the editor: listed while editing.
    const bool listed = static_cast<bool>(buttons[0]);
    if (next.editing != listed) {
        if (next.editing) {
            buttons[0].Attach(new ChargeAlertElement(root->state, Role::discard));
            buttons[1].Attach(new ChargeAlertElement(root->state, Role::confirm));
        } else {
            for (auto& b : buttons) { b->disconnect(); b.Reset(); }
        }
        ++statistics.structureChanges;
        if (listening) UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(root.Get()), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }
    if (notified && listening) {
        const auto raise = [&](IRawElementProviderSimple* element, PROPERTYID id, VARIANT before, VARIANT after) {
            UiaRaiseAutomationPropertyChangedEvent(element, id, before, after);
            VariantClear(&before);
            VariantClear(&after);
            ++statistics.propertyEvents;
        };
        if (notified->label != next.label && indicator)
            raise(static_cast<IRawElementProviderSimple*>(indicator.Get()), UIA_NamePropertyId, text(notified->label), text(next.label));
        // Kept buttons whose language changed.
        if (notified->editing && next.editing && buttons[0])
            for (std::size_t b = 0; b < 2; ++b) {
                auto* element = static_cast<IRawElementProviderSimple*>(buttons[b].Get());
                if (notified->names[b] != next.names[b]) raise(element, UIA_NamePropertyId, text(notified->names[b]), text(next.names[b]));
                if (notified->help[b] != next.help[b]) raise(element, UIA_HelpTextPropertyId, text(notified->help[b]), text(next.help[b]));
            }
    }
    notified = std::move(next);
}

ChargeAlertAutomation::ChargeAlertAutomation(ChargeAlertAutomationHost host) : state_(std::make_shared<State>()) {
    auto& s = *state_;
    s.host = std::move(host);
    s.root.Attach(new ChargeAlertRoot(state_));
    s.indicator.Attach(new ChargeAlertElement(state_, Role::indicator));
    s.sync(s.current());
}
ChargeAlertAutomation::~ChargeAlertAutomation() { disconnect(); }
bool ChargeAlertAutomation::rootRequest(LPARAM lParam) noexcept { return static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId); }
LRESULT ChargeAlertAutomation::answer(WPARAM wParam, LPARAM lParam) {
    auto& s = *state_;
    if (!s.alive || !s.root || !rootRequest(lParam)) return 0;
    ++s.statistics.rootRequests;
    // A client arriving mid-edit sees the current buttons.
    s.sync(s.current());
    return UiaReturnRawElementProvider(s.host.window, wParam, lParam, static_cast<IRawElementProviderSimple*>(s.root.Get()));
}
void ChargeAlertAutomation::changed() {
    auto& s = *state_;
    if (s.alive) s.sync(s.current());
}
HRESULT ChargeAlertAutomation::root(IRawElementProviderFragmentRoot** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto& s = *state_;
    if (!s.alive || !s.root) return UIA_E_ELEMENTNOTAVAILABLE;
    return s.root->QueryInterface(IID_PPV_ARGS(out));
}
std::size_t ChargeAlertAutomation::size() const noexcept { return state_->alive ? state_->count() : 0; }
ChargeAlertAutomationStats ChargeAlertAutomation::stats() const noexcept { return state_->statistics; }
void ChargeAlertAutomation::disconnect() noexcept {
    auto& s = *state_;
    if (!s.alive) return;
    s.alive = false;
    // Releases UIA's references to the window's provider first.
    if (s.host.window && IsWindow(s.host.window)) (void)UiaReturnRawElementProvider(s.host.window, 0, 0, nullptr);
    for (auto& b : s.buttons) if (b) { b->disconnect(); b.Reset(); }
    if (s.indicator) { s.indicator->disconnect(); s.indicator.Reset(); }
    if (s.root) { s.root->disconnect(); s.root.Reset(); }
    s.notified.reset();
    ++s.statistics.disconnected;
}
} // namespace endfield::native
#endif
