#include "native/charge_alert_automation.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <new>
#include <string>
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
template <class F> HRESULT guarded(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
} // namespace

struct ChargeAlertAutomation::State {
    ChargeAlertAutomationHost host;
    ComPtr<ChargeAlertRoot> root;
    std::unique_ptr<AutomationTree> tree;
    ChargeAlertAutomationStats statistics;
    bool alive{true};
};

// The window's root: its name, control type and bounds come from the HWND
// host; its children are the AutomationTree's top-level nodes.
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
        const std::size_t count = s->tree ? s->tree->size() : 0;
        if (!count) return S_OK;
        if (direction == NavigateDirection_FirstChild) return s->tree->child(0, out);
        if (direction == NavigateDirection_LastChild) return s->tree->child(count - 1, out);
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
    // The strip's controls are listed after the indicator and answer first;
    // outside every node the window itself answers.
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return s->tree ? s->tree->fromPoint(x, y, out) : S_OK;
    }
    // The editor's keys go to the window itself (no focusable child).
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = live();
        if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
        return s->tree ? s->tree->focused(out) : S_OK;
    }
    void disconnect() noexcept {
        if (!connected) return;
        connected = false;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    }
private:
    ~ChargeAlertRoot() = default;
    std::atomic<ULONG> refs{1};
};

ChargeAlertAutomation::ChargeAlertAutomation(ChargeAlertAutomationHost host) : state_(std::make_shared<State>()) {
    auto& s = *state_;
    s.host = std::move(host);
    s.root.Attach(new ChargeAlertRoot(state_));
    AutomationTreeHost tree;
    tree.root = s.root.Get();
    std::weak_ptr<State> weak = state_;
    tree.nodes = [weak] {
        const auto s = weak.lock();
        return s && s->alive && s->host.nodes ? s->host.nodes() : std::vector<AutomationNode>{};
    };
    tree.toScreen = [weak](core::Rect r, std::uint8_t space) -> std::optional<core::Rect> {
        const auto s = weak.lock();
        return s && s->alive && s->host.toScreen ? s->host.toScreen(r, space) : std::nullopt;
    };
    tree.invoke = [weak](std::string_view id) { const auto s = weak.lock(); return s && s->alive && s->host.invoke && s->host.invoke(id); };
    tree.setValue = [weak](std::string_view id, double value) {
        const auto s = weak.lock();
        return s && s->alive && s->host.setValue && s->host.setValue(id, value);
    };
    s.tree = std::make_unique<AutomationTree>(std::move(tree));
    (void)s.tree->refresh();
}
ChargeAlertAutomation::~ChargeAlertAutomation() { disconnect(); }
bool ChargeAlertAutomation::rootRequest(LPARAM lParam) noexcept { return static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId); }
LRESULT ChargeAlertAutomation::answer(WPARAM wParam, LPARAM lParam) {
    auto& s = *state_;
    if (!s.alive || !s.root || !rootRequest(lParam)) return 0;
    ++s.statistics.rootRequests;
    // A client arriving mid-edit sees the current strip.
    try { (void)s.tree->refresh(); } catch (...) {}
    return UiaReturnRawElementProvider(s.host.window, wParam, lParam, static_cast<IRawElementProviderSimple*>(s.root.Get()));
}
void ChargeAlertAutomation::changed() {
    auto& s = *state_;
    if (s.alive && s.tree) (void)s.tree->refresh();
}
HRESULT ChargeAlertAutomation::root(IRawElementProviderFragmentRoot** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto& s = *state_;
    if (!s.alive || !s.root) return UIA_E_ELEMENTNOTAVAILABLE;
    return s.root->QueryInterface(IID_PPV_ARGS(out));
}
std::size_t ChargeAlertAutomation::size() const noexcept { return state_->alive && state_->tree ? state_->tree->size() : 0; }
ChargeAlertAutomationStats ChargeAlertAutomation::stats() const noexcept {
    auto out = state_->statistics;
    if (state_->tree) out.tree = state_->tree->stats();
    return out;
}
void ChargeAlertAutomation::disconnect() noexcept {
    auto& s = *state_;
    if (!s.alive) return;
    // Releases UIA's references to the window's provider first.
    if (s.host.window && IsWindow(s.host.window)) (void)UiaReturnRawElementProvider(s.host.window, 0, 0, nullptr);
    if (s.tree) { s.statistics.tree = s.tree->stats(); s.tree->disconnect(); s.tree.reset(); }
    s.alive = false;
    if (s.root) { s.root->disconnect(); s.root.Reset(); }
    ++s.statistics.disconnected;
}
} // namespace endfield::native
#endif
