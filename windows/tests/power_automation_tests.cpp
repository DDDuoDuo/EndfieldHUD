// NativePowerAutomation (native/power_automation): the UI Automation peer of
// the HUD's Power navigation entry, under a fake owner group. Synthetic
// element values only: no window, HUD, battery or UIA client is involved.
#ifdef _WIN32
#include "native/power_automation.hpp"
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace gpu = endfield::native;
namespace c = endfield::core;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool v, const std::string& message) {
    ++checks;
    if (!v) throw std::runtime_error(message);
}

// The owner's group as the HUD root provider hands it to automationRefresh.
class FakeGroup final : public IRawElementProviderFragmentRoot, public IRawElementProviderFragment {
public:
    std::atomic<ULONG> refs{1};
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs; } // stack object
    IFACEMETHODIMP ElementProviderFromPoint(double, double, IRawElementProviderFragment** out) override { if (out) *out = nullptr; return S_OK; }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override { if (out) *out = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection, IRawElementProviderFragment** out) override { if (out) *out = nullptr; return S_OK; }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override { if (out) *out = nullptr; return S_OK; }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override { if (out) *out = {}; return S_OK; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (out) *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override { if (out) *out = nullptr; return S_OK; }
};

std::wstring property(IRawElementProviderSimple* e, PROPERTYID id) {
    VARIANT v;
    VariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(e->GetPropertyValue(id, &v)) && v.vt == VT_BSTR && v.bstrVal) out = v.bstrVal;
    VariantClear(&v);
    return out;
}
long integer(IRawElementProviderSimple* e, PROPERTYID id) {
    VARIANT v;
    VariantInit(&v);
    long out = -1;
    if (SUCCEEDED(e->GetPropertyValue(id, &v))) out = v.vt == VT_BOOL ? (v.boolVal == VARIANT_TRUE ? 1 : 0) : v.vt == VT_I4 ? v.lVal : -1;
    VariantClear(&v);
    return out;
}
std::wstring value(IValueProvider* p) {
    BSTR raw{};
    std::wstring out;
    if (SUCCEEDED(p->get_Value(&raw)) && raw) out = raw;
    SysFreeString(raw);
    return out;
}

void run() {
    FakeGroup group;
    std::optional<gpu::PowerAutomationElement> element;
    unsigned invokes{};
    bool accept{true};
    {
        gpu::PowerAutomationHost host;
        host.root = &group;
        host.element = [&] { return element; };
        // Synthetic screen: a 1.5 DPI scale at origin (100, 50).
        host.toScreen = [](c::Rect r) -> std::optional<c::Rect> { return c::Rect{100 + r.x * 1.5, 50 + r.y * 1.5, r.width * 1.5, r.height * 1.5}; };
        host.invoke = [&] { ++invokes; return accept; };
        gpu::NativePowerAutomation automation(std::move(host));
        check(group.refs == 2, "The peer holds its group while it exists");
        check(!automation.refresh() && automation.size() == 0, "A hidden HUD lists no entry");
        ComPtr<IRawElementProviderFragment> entry, other, parent;
        check(automation.child(0, &entry) == E_INVALIDARG && !entry, "No child while hidden");

        element = gpu::PowerAutomationElement{"Power / Device Battery", "EndfieldHUD, Battery: 63%, On battery", "Not selected", c::Rect{600, 560, 30, 30}, true};
        check(automation.refresh() && automation.size() == 1 && !automation.refresh(), "The shown HUD lists the entry once");
        check(automation.child(0, &entry) == S_OK && entry && automation.child(1, &other) == E_INVALIDARG && !other, "One child");
        ComPtr<IRawElementProviderSimple> simple;
        ComPtr<IUnknown> pattern, a, b;
        ComPtr<IInvokeProvider> invoke;
        ComPtr<IValueProvider> valueProvider;
        check(SUCCEEDED(entry.As(&simple)), "Simple provider");
        check(SUCCEEDED(entry->Navigate(NavigateDirection_Parent, &parent)) && parent && SUCCEEDED(parent.As(&a)) &&
              a.Get() == static_cast<IUnknown*>(static_cast<IRawElementProviderFragmentRoot*>(&group)), "The parent is the owner's group");
        parent.Reset();
        a.Reset();
        check(SUCCEEDED(entry->Navigate(NavigateDirection_NextSibling, &other)) && !other && SUCCEEDED(entry->Navigate(NavigateDirection_FirstChild, &other)) && !other,
              "A leaf without siblings");
        ComPtr<IRawElementProviderFragmentRoot> root;
        check(SUCCEEDED(entry->get_FragmentRoot(&root)) && root.Get() == &group, "The fragment root is the group");
        root.Reset();
        ProviderOptions options{};
        check(SUCCEEDED(simple->get_ProviderOptions(&options)) && (options & ProviderOptions_ServerSideProvider) && (options & ProviderOptions_UseComThreading),
              "Server-side provider on the UI thread");
        SAFEARRAY* id{};
        check(SUCCEEDED(entry->GetRuntimeId(&id)) && id, "Runtime id");
        SafeArrayDestroy(id);
        check(property(simple.Get(), UIA_NamePropertyId) == L"Power / Device Battery" && property(simple.Get(), UIA_HelpTextPropertyId) == L"EndfieldHUD, Battery: 63%, On battery" &&
              property(simple.Get(), UIA_AutomationIdPropertyId) == L"power" && property(simple.Get(), UIA_FrameworkIdPropertyId) == L"EndfieldHUD" &&
              integer(simple.Get(), UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId && integer(simple.Get(), UIA_IsEnabledPropertyId) == 1 &&
              integer(simple.Get(), UIA_IsKeyboardFocusablePropertyId) == 0 && integer(simple.Get(), UIA_IsOffscreenPropertyId) == 0,
              "Navigation title, charge help, an enabled Button");
        check(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &pattern)) && pattern && SUCCEEDED(pattern.As(&invoke)), "Invoke pattern");
        pattern.Reset();
        check(SUCCEEDED(simple->GetPatternProvider(UIA_ValuePatternId, &pattern)) && pattern && SUCCEEDED(pattern.As(&valueProvider)), "Value pattern");
        pattern.Reset();
        check(SUCCEEDED(simple->GetPatternProvider(UIA_TogglePatternId, &pattern)) && !pattern, "No other pattern");
        BOOL readOnly{};
        check(value(valueProvider.Get()) == L"Not selected" && SUCCEEDED(valueProvider->get_IsReadOnly(&readOnly)) && readOnly &&
              valueProvider->SetValue(L"Selected") == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && value(valueProvider.Get()) == L"Not selected",
              "The selection value is read-only");
        UiaRect r{};
        check(SUCCEEDED(entry->get_BoundingRectangle(&r)) && r.left == 1000 && r.top == 890 && r.width == 45 && r.height == 45, "The frame maps to screen pixels");
        check(automation.fromPoint(1022, 912, &other) == S_OK && other && SUCCEEDED(other.As(&a)) && SUCCEEDED(entry.As(&b)) && a.Get() == b.Get(),
              "Hit testing inside the frame finds the entry");
        other.Reset();
        check(automation.fromPoint(1045, 912, &other) == S_OK && !other && automation.fromPoint(999, 900, &other) == S_OK && !other, "Outside the frame: none");

        // Properties are read when asked: the frame and help follow the badge.
        element->rect = c::Rect{560, 560, 110, 30};
        element->help = "EndfieldHUD, Battery: 64%, Charging";
        check(SUCCEEDED(entry->get_BoundingRectangle(&r)) && r.width == 165 && property(simple.Get(), UIA_HelpTextPropertyId) == L"EndfieldHUD, Battery: 64%, Charging",
              "The expanded capsule and the new reading are live");
        element->value = "Selected";
        automation.notify();
        check(!automation.refresh() && value(valueProvider.Get()) == L"Selected", "A content change keeps the structure");

        // activateNavigationButton.
        check(invoke->Invoke() == S_OK && invokes == 1, "A press selects Power");
        accept = false;
        check(invoke->Invoke() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && invokes == 2, "A refused press reports an invalid operation");
        accept = true;
        element->enabled = false;
        check(integer(simple.Get(), UIA_IsEnabledPropertyId) == 0 && invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED) && invokes == 2,
              "A disabled entry refuses presses");
        element->enabled = true;
        // Hidden/waiting/retracting badge: no frame, offscreen, no hit.
        element->rect.reset();
        check(integer(simple.Get(), UIA_IsOffscreenPropertyId) == 1 && SUCCEEDED(entry->get_BoundingRectangle(&r)) && r.width == 0 &&
              automation.fromPoint(1022, 912, &other) == S_OK && !other, "Without a hit region the entry is offscreen");

        // The HUD conceals: the entry is withdrawn and the old peer is dead.
        element.reset();
        check(automation.refresh() && automation.size() == 0, "Concealment withdraws the entry");
        VARIANT gone;
        VariantInit(&gone);
        BSTR goneValue{};
        check(simple->GetPropertyValue(UIA_NamePropertyId, &gone) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) &&
              invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) &&
              entry->Navigate(NavigateDirection_Parent, &parent) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && !parent &&
              valueProvider->get_Value(&goneValue) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && !goneValue,
              "A withdrawn peer answers element-not-available");
        VariantClear(&gone);
        element = gpu::PowerAutomationElement{"Power / Device Battery", "EndfieldHUD, Battery: 63%, On battery", "Selected", c::Rect{600, 560, 30, 30}, true};
        ComPtr<IRawElementProviderFragment> fresh;
        ComPtr<IRawElementProviderSimple> freshSimple;
        check(automation.refresh() && automation.child(0, &fresh) == S_OK && fresh && fresh.Get() != entry.Get() && SUCCEEDED(fresh.As(&freshSimple)) &&
              property(freshSimple.Get(), UIA_NamePropertyId) == L"Power / Device Battery", "Reopening lists a new peer");
        automation.disconnect();
        check(automation.size() == 0 && integer(freshSimple.Get(), UIA_NamePropertyId) == -1, "Disconnect withdraws every peer");
        const auto stats = automation.stats();
        check(stats.created == 2 && stats.disconnected == 2 && stats.structureChanges == 4 && stats.invokes == 2, "Peer statistics");
    }
    check(group.refs == 1, "Destruction releases the group");
}
} // namespace

int wmain() {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result), "COM");
        run();
        CoUninitialize();
        std::cout << "Power automation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(result)) CoUninitialize();
        std::cerr << "Power automation after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
