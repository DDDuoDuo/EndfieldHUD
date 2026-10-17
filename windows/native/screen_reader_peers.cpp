#include "native/screen_reader_peers.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <new>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
using Role = ScreenReaderRole;
std::atomic<int> nextRuntime{1};
std::wstring wide(std::string_view s) {
    if (s.empty() || s.size() > INT_MAX) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
// Strict: an invalid UTF-16 value is refused rather than altered.
std::optional<std::string> narrow(std::wstring_view w) {
    if (w.empty()) return std::string{};
    if (w.size() > INT_MAX) return std::nullopt;
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::nullopt;
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
VARIANT text(std::string_view s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(wide(s).c_str()); return v; }
VARIANT flag(bool b) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return v; }
long controlType(Role role) {
    switch (role) {
    case Role::edit: return UIA_EditControlTypeId;
    case Role::text: return UIA_TextControlTypeId;
    case Role::group: return UIA_GroupControlTypeId;
    case Role::button: break;
    }
    return UIA_ButtonControlTypeId;
}
bool hasValue(const ScreenReaderPeer& p) { return p.role == Role::edit || (p.role == Role::group && !p.value.empty()); }
} // namespace

std::optional<core::Rect> screenReaderClientToScreen(void* window, double scale, const std::optional<core::Rect>& client) {
    if (!client || !window || !std::isfinite(scale) || scale <= 0) return std::nullopt;
    POINT origin{0, 0};
    if (!ClientToScreen(static_cast<HWND>(window), &origin)) return std::nullopt;
    const core::Rect out{origin.x + client->x * scale, origin.y + client->y * scale, client->width * scale, client->height * scale};
    if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.width) || !std::isfinite(out.height)) return std::nullopt;
    return out;
}

struct NativeScreenReaderPeers::State {
    ScreenReaderPeerSource source;
    IRawElementProviderFragmentRoot* group{}; // AddRef'd while held
    std::vector<ComPtr<ScreenReaderPeerElement>> order;
    ScreenReaderPeerStats statistics;
    std::weak_ptr<State> weakSelf;
    bool alive{true}; // false once the owning object is destroyed: the module is never called again
    std::optional<core::Rect> screen(core::Rect r) const {
        if (!alive || !source.toScreen) return std::nullopt;
        std::optional<core::Rect> out;
        try { out = source.toScreen(r); } catch (...) { return std::nullopt; }
        if (!out || !std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->width) || !std::isfinite(out->height) || out->width <= 0 || out->height <= 0)
            return std::nullopt;
        return out;
    }
    bool relist();
    void contentChanged() noexcept;
    void disconnectAll() noexcept;
    void setGroup(IRawElementProviderFragmentRoot* next) noexcept {
        if (group == next) return;
        if (next) next->AddRef();
        if (group) group->Release();
        group = next;
    }
};

class ScreenReaderPeerElement final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IValueProvider {
public:
    using State = NativeScreenReaderPeers::State;
    ScreenReaderPeerElement(std::weak_ptr<State> s, ScreenReaderPeer p) : state(std::move(s)), peer(std::move(p)), runtime(nextRuntime++) {}
    std::weak_ptr<State> state;
    ScreenReaderPeer peer;
    int runtime;
    bool connected{true};
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider) && peer.role == Role::button) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IValueProvider) && hasValue(peer)) *out = static_cast<IValueProvider*>(this);
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
        if (id == UIA_InvokePatternId && peer.role == Role::button) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
        else if (id == UIA_ValuePatternId && hasValue(peer)) { *out = static_cast<IValueProvider*>(this); AddRef(); }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        switch (id) {
        case UIA_NamePropertyId: *out = text(peer.label); break;
        case UIA_HelpTextPropertyId: if (!peer.help.empty()) *out = text(peer.help); break;
        case UIA_AutomationIdPropertyId: *out = text(peer.id); break;
        case UIA_ControlTypePropertyId: out->vt = VT_I4; out->lVal = controlType(peer.role); break;
        case UIA_IsEnabledPropertyId: *out = flag(peer.enabled); break;
        // Only the editors take the HUD's text focus; projected buttons are
        // not tab stops (the HUD routes its own keys).
        case UIA_IsKeyboardFocusablePropertyId: *out = flag(peer.role == Role::edit && peer.enabled); break;
        case UIA_HasKeyboardFocusPropertyId: *out = flag(peer.role == Role::edit && peer.focused); break;
        case UIA_IsOffscreenPropertyId: { const auto s = state.lock(); *out = flag(!s || !s->screen(peer.rect)); break; }
        case UIA_FrameworkIdPropertyId: *out = text("EndfieldHUD"); break;
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
        if (direction == NavigateDirection_Parent) return s->group ? s->group->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
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
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (const auto r = s->screen(peer.rect)) *out = {r->x, r->y, r->width, r->height};
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    // An editor takes the HUD's text focus (the Mac makeFirstResponder on its
    // text view); the other peers are not focusable.
    IFACEMETHODIMP SetFocus() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (peer.role != Role::edit) return S_OK;
        if (!peer.enabled) return UIA_E_ELEMENTNOTENABLED;
        return call([](State& s, const std::string& id) { ++s.statistics.focuses; return s.source.focus && s.source.focus(id); });
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto s = state.lock();
        if (!s || !connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (s->group) { s->group->AddRef(); *out = s->group; }
        return S_OK;
    }
    // IInvokeProvider: the button's action exactly as its click.
    IFACEMETHODIMP Invoke() override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!peer.enabled) return UIA_E_ELEMENTNOTENABLED;
        return call([](State& s, const std::string& id) { ++s.statistics.invokes; return s.source.invoke && s.source.invoke(id); });
    }
    // IValueProvider: an editor's text (writable, replaced like typing) or a
    // group's read-only value.
    IFACEMETHODIMP SetValue(LPCWSTR value) override {
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (peer.role != Role::edit) return UIA_E_INVALIDOPERATION;
        if (!peer.enabled) return UIA_E_ELEMENTNOTENABLED;
        if (!value) return E_INVALIDARG;
        const auto utf8 = narrow(value);
        if (!utf8) return E_INVALIDARG;
        return call([&utf8](State& s, const std::string& id) { ++s.statistics.texts; return s.source.setText && s.source.setText(id, *utf8); });
    }
    IFACEMETHODIMP get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = SysAllocString(wide(peer.value).c_str());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL* out) override {
        if (!out) return E_POINTER;
        if (!connected) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = peer.role == Role::edit && peer.enabled ? FALSE : TRUE;
        return S_OK;
    }
    void disconnect() noexcept {
        if (!connected) return;
        connected = false;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    }

private:
    ~ScreenReaderPeerElement() = default;
    std::atomic<ULONG> refs{1};
    // Runs a module action outside any C++ exception boundary; a performed
    // action re-lists the peers at once (a menu may replace them).
    template <class F> HRESULT call(F&& action) {
        const auto s = state.lock();
        if (!s || !s->alive) return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may rebuild the tree; keep this element and its id alive.
        const ComPtr<ScreenReaderPeerElement> self(this);
        const std::string id = peer.id;
        bool used{};
        try { used = action(*s, id); } catch (...) { return E_FAIL; }
        if (!used) return UIA_E_INVALIDOPERATION;
        if (s->alive) s->contentChanged();
        return S_OK;
    }
};

bool NativeScreenReaderPeers::State::relist() {
    ++statistics.refreshes;
    auto list = source.peers ? source.peers() : std::vector<ScreenReaderPeer>{};
    const bool listening = UiaClientsAreListening() != FALSE;
    std::vector<ComPtr<ScreenReaderPeerElement>> next;
    next.reserve(list.size());
    std::vector<ScreenReaderPeerElement*> focused;
    bool changed = list.size() != order.size();
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto& p = list[i];
        ComPtr<ScreenReaderPeerElement> e;
        for (const auto& old : order) if (old->connected && old->peer.id == p.id && old->peer.role == p.role) {
            // An id appears once per role; a duplicate never shares identity.
            const bool taken = std::any_of(next.begin(), next.end(), [&](const auto& n) { return n.Get() == old.Get(); });
            if (!taken) { e = old; break; }
        }
        if (!e) {
            e.Attach(new ScreenReaderPeerElement(weakSelf, std::move(p)));
            ++statistics.created;
            changed = true;
            if (e->peer.focused) focused.push_back(e.Get());
        } else {
            if (listening) {
                auto* simple = static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise = [&](PROPERTYID id, VARIANT before, VARIANT after) {
                    UiaRaiseAutomationPropertyChangedEvent(simple, id, before, after);
                    VariantClear(&before);
                    VariantClear(&after);
                };
                if (e->peer.label != p.label) raise(UIA_NamePropertyId, text(e->peer.label), text(p.label));
                if (e->peer.help != p.help) raise(UIA_HelpTextPropertyId, text(e->peer.help), text(p.help));
                if (hasValue(p) && e->peer.value != p.value) raise(UIA_ValueValuePropertyId, text(e->peer.value), text(p.value));
                if (e->peer.enabled != p.enabled) raise(UIA_IsEnabledPropertyId, flag(e->peer.enabled), flag(p.enabled));
                if (e->peer.focused != p.focused) raise(UIA_HasKeyboardFocusPropertyId, flag(e->peer.focused), flag(p.focused));
            }
            if (p.focused && !e->peer.focused) focused.push_back(e.Get());
            e->peer = std::move(p);
        }
        if (i >= order.size() || order[i].Get() != e.Get()) changed = true;
        next.push_back(std::move(e));
    }
    for (auto& old : order) {
        const bool kept = std::any_of(next.begin(), next.end(), [&](const auto& n) { return n.Get() == old.Get(); });
        if (!kept) { old->disconnect(); ++statistics.disconnected; }
    }
    order = std::move(next);
    if (changed) ++statistics.structureChanges;
    // An editor that gained the HUD's text focus is announced like a native
    // text view becoming first responder.
    if (listening) for (auto* e : focused) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(e), UIA_AutomationFocusChangedEventId);
    return changed;
}
void NativeScreenReaderPeers::State::contentChanged() noexcept {
    // The press changed the module outside the host's input routes: the
    // host presents it (and takes queued actions), then the peers are
    // re-listed now and the group reports ChildrenInvalidated like the host.
    try { if (source.changed) source.changed(); } catch (...) {}
    if (!alive) return;
    bool changed{};
    try { changed = relist(); } catch (...) { return; }
    if (!changed || !group || !UiaClientsAreListening()) return;
    ComPtr<IRawElementProviderSimple> simple;
    if (SUCCEEDED(group->QueryInterface(IID_PPV_ARGS(&simple))))
        UiaRaiseStructureChangedEvent(simple.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
}
void NativeScreenReaderPeers::State::disconnectAll() noexcept {
    for (auto& e : order) { e->disconnect(); ++statistics.disconnected; }
    if (!order.empty()) ++statistics.structureChanges;
    order.clear();
}

NativeScreenReaderPeers::NativeScreenReaderPeers(ScreenReaderPeerSource source) : state_(std::make_shared<State>()) {
    state_->source = std::move(source);
    state_->weakSelf = state_;
}
NativeScreenReaderPeers::~NativeScreenReaderPeers() {
    // A retained element may still lock the state; it must never reach the
    // module again (its owner is being destroyed). The source is kept: a
    // press in progress may still be running one of its functions.
    state_->alive = false;
    state_->disconnectAll();
    state_->setGroup(nullptr);
}
bool NativeScreenReaderPeers::refresh(IRawElementProviderFragmentRoot* group) {
    auto& s = *state_;
    bool regrouped{};
    if (s.group != group) {
        // Peers name their group as parent and fragment root: a new group
        // (another root provider) starts a new set.
        if (!s.order.empty()) regrouped = true;
        s.disconnectAll();
        s.setGroup(group);
    }
    return s.relist() || regrouped;
}
std::size_t NativeScreenReaderPeers::size() const noexcept { return state_->order.size(); }
ScreenReaderPeerStats NativeScreenReaderPeers::stats() const noexcept { return state_->statistics; }
HRESULT NativeScreenReaderPeers::child(std::size_t index, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index >= state_->order.size()) return E_INVALIDARG;
    return state_->order[index]->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativeScreenReaderPeers::fromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto& s = *state_;
    for (auto it = s.order.rbegin(); it != s.order.rend(); ++it)
        if (const auto r = s.screen((*it)->peer.rect))
            if (x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) return (*it)->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
void NativeScreenReaderPeers::disconnect() noexcept { state_->disconnectAll(); }
} // namespace endfield::native
#endif
