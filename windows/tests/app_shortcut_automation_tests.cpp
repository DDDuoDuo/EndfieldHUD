#ifdef _WIN32
#include "native/app_shortcut_automation.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// UI Automation peers of the + Add App canvas over injected controls and host
// callbacks (HUDAppShortcutInteraction.layoutAccessibility): no window is
// shown and no application, clipboard or user data is touched.
namespace {
namespace n = endfield::native;namespace c = endfield::core;using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Apartment { HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); } };

// Synthetic owner group root: lists the + Add App peers as its children.
class Root final : public IRawElementProviderFragmentRoot, public IRawElementProviderFragment, public IRawElementProviderSimple {
    std::atomic<ULONG> refs{1};
public:
    n::NativeAppShortcutAutomation* tree{};
    ULONG count() const noexcept { return refs.load(); }
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
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* o) override { *o = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading); return S_OK; }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* o) override { VariantInit(o); return S_OK; }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection d, IRawElementProviderFragment** o) override {
        *o = nullptr;
        if (!tree || !tree->size()) return S_OK;
        if (d == NavigateDirection_FirstChild) return tree->child(0, o);
        if (d == NavigateDirection_LastChild) return tree->child(tree->size() - 1, o);
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* o) override { *o = {}; return S_OK; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** o) override { *o = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** o) override { AddRef(); *o = this; return S_OK; }
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** o) override { *o = nullptr; return tree ? tree->fromPoint(x, y, o) : S_OK; }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** o) override { *o = nullptr; return S_OK; }
private:
    ~Root() = default;
};

VARIANT raw(IRawElementProviderFragment* f, PROPERTYID id, HRESULT* result = nullptr) {
    ComPtr<IRawElementProviderSimple> s;
    check(SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s))), "Peers are simple providers");
    VARIANT v;VariantInit(&v);
    const HRESULT hr = s->GetPropertyValue(id, &v);
    if (result) *result = hr;
    return v;
}
std::wstring property(IRawElementProviderFragment* f, PROPERTYID id) {
    auto v = raw(f, id);
    std::wstring out = v.vt == VT_BSTR && v.bstrVal ? std::wstring(v.bstrVal, SysStringLen(v.bstrVal)) : std::wstring{};
    VariantClear(&v);
    return out;
}
long integer(IRawElementProviderFragment* f, PROPERTYID id) { auto v = raw(f, id); const long out = v.vt == VT_I4 ? v.lVal : -1; VariantClear(&v); return out; }
bool boolean(IRawElementProviderFragment* f, PROPERTYID id) { auto v = raw(f, id); const bool out = v.vt == VT_BOOL && v.boolVal == VARIANT_TRUE; VariantClear(&v); return out; }
int runtime(IRawElementProviderFragment* f) {
    SAFEARRAY* a{};
    check(SUCCEEDED(f->GetRuntimeId(&a)) && a, "Peers have runtime ids");
    int value{};LONG i = 1;SafeArrayGetElement(a, &i, &value);SafeArrayDestroy(a);
    return value;
}
ComPtr<IRawElementProviderFragment> child(const n::NativeAppShortcutAutomation& tree, std::size_t index) {
    ComPtr<IRawElementProviderFragment> out;
    check(SUCCEEDED(tree.child(index, &out)) && out, "Listed peer");
    return out;
}

void run() {
    const Apartment apartment;check(SUCCEEDED(apartment.hr), "STA for the UIA peers");
    auto* root = new Root;
    std::vector<n::AppShortcutAutomationControl> controls{
        {"apps:choose", "Choose application", "Application shortcuts", {270, 2, 118, 28}},
        {"apps:a1:launch", "Open Alpha", "Application shortcuts", {12, 44, 306, 46}},
        {"apps:name:field", "Shortcut name", "Return applies the name. Save shortcut stores your changes.", {78, 77, 304, 27}, n::AppShortcutAutomationKind::edit, "Alpha"}};
    std::vector<std::string> invoked, texts;
    bool projects = true, accept = true;
    {
        n::AppShortcutAutomationHost host;
        host.root = root;
        host.controls = [&] { return controls; };
        // Synthetic plane: canvas -> screen = (100, 50) + 2 x canvas.
        host.toScreen = [&](c::Rect r) -> std::optional<c::Rect> { if (!projects) return std::nullopt; return c::Rect{100 + r.x * 2, 50 + r.y * 2, r.width * 2, r.height * 2}; };
        host.invoke = [&](std::string_view id) { invoked.emplace_back(id); return accept; };
        host.setText = [&](std::string_view id, std::string_view value) { texts.push_back(std::string(id) + "=" + std::string(value)); return accept; };
        n::NativeAppShortcutAutomation tree(std::move(host));
        root->tree = &tree;
        check(root->count() == 2, "The tree holds its group root");
        check(tree.refresh() && tree.size() == 3 && !tree.refresh(), "Refresh builds the peers once; an unchanged list changes nothing");
        const auto choose = child(tree, 0), open = child(tree, 1), field = child(tree, 2);
        check(property(choose.Get(), UIA_NamePropertyId) == L"Choose application" && property(choose.Get(), UIA_HelpTextPropertyId) == L"Application shortcuts" &&
              property(choose.Get(), UIA_AutomationIdPropertyId) == L"apps:choose" && integer(choose.Get(), UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId &&
              !boolean(choose.Get(), UIA_IsKeyboardFocusablePropertyId) && boolean(choose.Get(), UIA_IsEnabledPropertyId), "A button peer carries the action's label and the canvas status");
        check(integer(field.Get(), UIA_ControlTypePropertyId) == UIA_EditControlTypeId && boolean(field.Get(), UIA_IsKeyboardFocusablePropertyId) &&
              property(field.Get(), UIA_ValueValuePropertyId) == L"Alpha", "The name field is an edit peer with its text");
        UiaRect bounds{};
        check(SUCCEEDED(open->get_BoundingRectangle(&bounds)) && bounds.left == 124 && bounds.top == 138 && bounds.width == 612 && bounds.height == 92 &&
              !boolean(open.Get(), UIA_IsOffscreenPropertyId), "Bounds follow the projected canvas rect");
        // Patterns: buttons invoke, the edit carries Value.
        ComPtr<IInvokeProvider> invoke, noInvoke;ComPtr<IValueProvider> value, noValue;ComPtr<IUnknown> pattern;
        check(SUCCEEDED(choose.As(&invoke)) && FAILED(choose.As(&noValue)) && FAILED(field.As(&noInvoke)) && SUCCEEDED(field.As(&value)) && invoke && value,
              "Buttons invoke and the edit holds a value");
        ComPtr<IRawElementProviderSimple> simple;check(SUCCEEDED(field.As(&simple)) && SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &pattern)) && !pattern, "The edit offers no Invoke");
        check(SUCCEEDED(invoke->Invoke()) && invoked == std::vector<std::string>{"apps:choose"}, "Invoke performs the click's action");
        accept = false;check(invoke->Invoke() == UIA_E_INVALIDOPERATION, "A declined action reports an invalid operation");accept = true;
        BSTR text{};BOOL readOnly{TRUE};
        check(SUCCEEDED(value->get_Value(&text)) && std::wstring(text, SysStringLen(text)) == L"Alpha" && SUCCEEDED(value->get_IsReadOnly(&readOnly)) && !readOnly, "The edit's value is its text");
        SysFreeString(text);
        check(SUCCEEDED(value->SetValue(L"Alpha 应用")) && texts == std::vector<std::string>{"apps:name:field=Alpha \xe5\xba\x94\xe7\x94\xa8"}, "SetValue sends UTF-8 text to the field");
        // Navigation and hit testing (topmost = last listed).
        ComPtr<IRawElementProviderFragment> link;
        check(SUCCEEDED(open->Navigate(NavigateDirection_Parent, &link)) && link && SUCCEEDED(open->Navigate(NavigateDirection_NextSibling, &link)) && link.Get() == field.Get() &&
              SUCCEEDED(open->Navigate(NavigateDirection_PreviousSibling, &link)) && link.Get() == choose.Get(), "Peers navigate to their root and siblings");
        check(SUCCEEDED(tree.fromPoint(100 + 2 * 200, 50 + 2 * 90, &link)) && link.Get() == field.Get(), "The hit test finds the field above the row");
        check(SUCCEEDED(tree.fromPoint(10, 10, &link)) && !link, "A point beside the canvas finds nothing");
        // Identity across a content change, removal and an unprojected plane.
        const auto id = runtime(open.Get());
        controls[1].label = "Open Alpha Prime";controls.erase(controls.begin() + 2);
        check(tree.refresh() && tree.size() == 2 && runtime(child(tree, 1).Get()) == id && property(open.Get(), UIA_NamePropertyId) == L"Open Alpha Prime",
              "A renamed peer keeps its identity");
        HRESULT gone{};auto stale = raw(field.Get(), UIA_NamePropertyId, &gone);VariantClear(&stale);
        check(gone == UIA_E_ELEMENTNOTAVAILABLE && value->SetValue(L"x") == UIA_E_ELEMENTNOTAVAILABLE, "A vanished peer is disconnected");
        projects = false;
        check(boolean(open.Get(), UIA_IsOffscreenPropertyId) && SUCCEEDED(open->get_BoundingRectangle(&bounds)) && bounds.width == 0, "An unprojected canvas hides its peers");
        projects = true;
        tree.disconnect();
        check(tree.size() == 0 && invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE && tree.stats().disconnected == 3 && tree.stats().invokes == 2 && tree.stats().values == 1,
              "Disconnect releases every peer");
        root->tree = nullptr;
    }
    check(root->count() == 1, "The tree released its group root");
    root->Release();
}
} // namespace

int main() {
    try {
        run();
        std::cout << "App shortcut automation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "App shortcut automation after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
