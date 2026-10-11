#ifdef _WIN32
#include "native/mac_import_automation.hpp"
#include <ole2.h> // VARIANT/BSTR helpers even under WIN32_LEAN_AND_MEAN
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// UI Automation parts of the import card over synthetic nodes and an injected
// host: no window is shown and no import, file or user data is touched.
namespace {
namespace n = endfield::native;
namespace c = endfield::core;
using Microsoft::WRL::ComPtr;
using Node = n::MacImportAutomationNode;
using Role = Node::Role;
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }

// Synthetic owner group: lists the card's top-level parts as its children.
class Group final : public IRawElementProviderFragmentRoot, public IRawElementProviderFragment, public IRawElementProviderSimple {
    std::atomic<ULONG> refs{1};
public:
    n::NativeMacImportAutomation* tree{};
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
    ~Group() = default;
};

std::wstring text(IRawElementProviderFragment* f, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> s;
    check(f && SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s))), "Simple provider");
    VARIANT v;
    check(SUCCEEDED(s->GetPropertyValue(id, &v)), "Property read");
    std::wstring out = v.vt == VT_BSTR && v.bstrVal ? std::wstring(v.bstrVal, SysStringLen(v.bstrVal)) : std::wstring{};
    VariantClear(&v);
    return out;
}
long value(IRawElementProviderFragment* f, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> s;
    f->QueryInterface(IID_PPV_ARGS(&s));
    VARIANT v;
    VariantInit(&v);
    s->GetPropertyValue(id, &v);
    const long out = v.vt == VT_I4 ? v.lVal : v.vt == VT_BOOL ? (v.boolVal == VARIANT_TRUE ? 1 : 0) : -1;
    VariantClear(&v);
    return out;
}
ComPtr<IRawElementProviderFragment> go(IRawElementProviderFragment* f, NavigateDirection d) {
    ComPtr<IRawElementProviderFragment> out;
    check(SUCCEEDED(f->Navigate(d, &out)), "Navigate");
    return out;
}
bool same(IUnknown* a, IUnknown* b) {
    ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}

// A synthetic card: the area builds the same pre-order list.
struct Card {
    std::wstring title = L"Review import";
    double progress{-1};
    std::size_t rows{14}, first{};
    bool shown{true}, confirm{true}, offscreen{};
    int focused{};
    std::vector<std::string> invoked, focusRequests;
    std::vector<Node> nodes() const {
        std::vector<Node> out;
        if (!shown) return out;
        const auto add = [&](std::string id, Role role, std::string name, std::optional<c::Rect> rect, std::size_t parent) {
            Node node; node.id = std::move(id); node.role = role; node.name = std::move(name); node.rect = rect; node.parent = parent;
            out.push_back(std::move(node));
            return out.size() - 1;
        };
        const auto dialog = add("dialog", Role::dialog, "Review import", c::Rect{0, 0, 390, 411}, Node::topLevel);
        out[dialog].help = "12 of 12 data sets are ready.";
        add("title", Role::text, "Review import", c::Rect{23, 42, 344, 28}, dialog);
        if (progress >= 0) { const auto bar = add("progress", Role::progress, "Checking export…", c::Rect{23, 138, 344, 2}, dialog); out[bar].value = progress; }
        const auto list = add("list", Role::list, "Review import", c::Rect{19, 146, 352, 200}, dialog);
        for (std::size_t n = 0; n < rows; ++n) {
            const bool visible = n >= first && n < first + 12;
            add("row." + std::to_string(n), Role::item, "Row " + std::to_string(n) + ": Ready",
                visible ? std::optional<c::Rect>(c::Rect{29, 150 + 16 * double(n - first), 332, 14}) : std::nullopt, list);
        }
        const auto cancel = add("cancel", Role::button, "Cancel", c::Rect{23, 358, 166, 32}, dialog);
        const auto ok = add("confirm", Role::button, "Import and Restart", c::Rect{201, 358, 166, 32}, dialog);
        out[cancel].focused = focused == 0;
        out[ok].focused = focused == 1;
        out[ok].enabled = confirm;
        return out;
    }
    n::MacImportAutomationHost host(Group* group) {
        n::MacImportAutomationHost h;
        h.root = group;
        h.nodes = [this] { return nodes(); };
        // Client points at 1.5x, offset like a window at (100, 50).
        h.toScreen = [this](c::Rect r) -> std::optional<c::Rect> { if (offscreen) return std::nullopt; return c::Rect{100 + r.x * 1.5, 50 + r.y * 1.5, r.width * 1.5, r.height * 1.5}; };
        h.invoke = [this](std::string_view id) { invoked.emplace_back(id); return true; };
        // The area moves the card's focus like Tab (a disabled button refuses).
        h.focus = [this](std::string_view id) {
            focusRequests.emplace_back(id);
            const int button = id == "confirm" ? 1 : 0;
            if (button == 1 && !confirm) return false;
            focused = button;
            return true;
        };
        return h;
    }
};
ComPtr<IRawElementProviderFragment> find(IRawElementProviderFragment* from, const std::wstring& id) {
    if (text(from, UIA_AutomationIdPropertyId) == id) return ComPtr<IRawElementProviderFragment>(from);
    for (auto child = go(from, NavigateDirection_FirstChild); child; child = go(child.Get(), NavigateDirection_NextSibling))
        if (auto hit = find(child.Get(), id)) return hit;
    return {};
}

void run() {
    Card card;
    auto* group = new Group();
    {
        n::NativeMacImportAutomation tree(card.host(group));
        group->tree = &tree;
        check(tree.refresh() && tree.size() == 1 && tree.parts() == 5 + card.rows, "One dialog with title, list, rows and two buttons");
        check(!tree.refresh() && tree.stats().created == 5 + card.rows, "An unchanged refresh creates nothing");
        ComPtr<IRawElementProviderFragment> dialog;
        check(SUCCEEDED(tree.child(0, &dialog)) && tree.child(1, dialog.ReleaseAndGetAddressOf()) == E_INVALIDARG, "Only the dialog is top level");
        check(SUCCEEDED(tree.child(0, &dialog)), "Dialog");
        check(text(dialog.Get(), UIA_NamePropertyId) == L"Review import" && text(dialog.Get(), UIA_HelpTextPropertyId) == L"12 of 12 data sets are ready." &&
              value(dialog.Get(), UIA_ControlTypePropertyId) == UIA_PaneControlTypeId && value(dialog.Get(), 30174) == 1 &&
              text(dialog.Get(), UIA_AutomationIdPropertyId) == L"macImport.dialog", "The dialog is a named pane marked as a dialog");
        check(same(go(dialog.Get(), NavigateDirection_Parent).Get(), static_cast<IRawElementProviderSimple*>(group)), "The dialog's parent is the owner's group");
        const auto title = go(dialog.Get(), NavigateDirection_FirstChild), confirm = go(dialog.Get(), NavigateDirection_LastChild);
        check(text(title.Get(), UIA_AutomationIdPropertyId) == L"macImport.title" && text(confirm.Get(), UIA_AutomationIdPropertyId) == L"macImport.confirm",
              "Children in card order");
        const auto list = go(title.Get(), NavigateDirection_NextSibling);
        check(value(list.Get(), UIA_ControlTypePropertyId) == UIA_ListControlTypeId && same(go(list.Get(), NavigateDirection_PreviousSibling).Get(), title.Get()),
              "Siblings skip the list's own items");
        const auto first = go(list.Get(), NavigateDirection_FirstChild), last = go(list.Get(), NavigateDirection_LastChild);
        check(text(first.Get(), UIA_NamePropertyId) == L"Row 0: Ready" && text(last.Get(), UIA_NamePropertyId) == L"Row 13: Ready" &&
              value(first.Get(), UIA_ControlTypePropertyId) == UIA_ListItemControlTypeId && same(go(last.Get(), NavigateDirection_Parent).Get(), list.Get()) &&
              !go(last.Get(), NavigateDirection_NextSibling) && !go(first.Get(), NavigateDirection_PreviousSibling), "List items under the list");
        UiaRect r{};
        check(value(first.Get(), UIA_IsOffscreenPropertyId) == 0 && value(last.Get(), UIA_IsOffscreenPropertyId) == 1 &&
              SUCCEEDED(last->get_BoundingRectangle(&r)) && r.width == 0, "Rows outside the card are offscreen without bounds");
        check(SUCCEEDED(confirm->get_BoundingRectangle(&r)) && r.left == 100 + 201 * 1.5 && r.top == 50 + 358 * 1.5 && r.width == 166 * 1.5, "Screen bounds through the host");

        // Focus and enabled state are properties; Invoke is the click.
        const auto cancel = go(confirm.Get(), NavigateDirection_PreviousSibling);
        check(value(cancel.Get(), UIA_HasKeyboardFocusPropertyId) == 1 && value(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 0 &&
              value(cancel.Get(), UIA_IsKeyboardFocusablePropertyId) == 1 && value(title.Get(), UIA_IsKeyboardFocusablePropertyId) == 0, "Buttons carry the card's focus");
        // AutomationFocusChanged: the first listing focused Cancel once.
        ComPtr<IRawElementProviderFragment> focus;
        check(tree.stats().focusMoves == 1 && SUCCEEDED(tree.focused(&focus)) && same(focus.Get(), cancel.Get()), "The appearing card announces Cancel once");
        check(!tree.refresh() && tree.stats().focusMoves == 1, "An unchanged refresh announces nothing");
        card.focused = 1;
        check(!tree.refresh() && value(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 1, "Focus moves without restructuring");
        check(tree.stats().focusMoves == 2 && SUCCEEDED(tree.focused(focus.ReleaseAndGetAddressOf())) && same(focus.Get(), confirm.Get()),
              "A focus move announces the newly focused button");
        // SetFocus routes to the host; the next refresh announces the move.
        check(SUCCEEDED(cancel->SetFocus()) && card.focusRequests == std::vector<std::string>{"cancel"} && card.focused == 0 && tree.stats().focusRequests == 1,
              "SetFocus on a button asks the host to move the card's focus");
        check(!tree.refresh() && tree.stats().focusMoves == 3 && value(cancel.Get(), UIA_HasKeyboardFocusPropertyId) == 1, "The moved focus is announced");
        check(dialog->SetFocus() == S_OK && title->SetFocus() == UIA_E_INVALIDOPERATION && first->SetFocus() == UIA_E_INVALIDOPERATION &&
              card.focusRequests.size() == 1, "The dialog holds its button's focus; text and rows are not focusable");
        check(value(list.Get(), UIA_IsKeyboardFocusablePropertyId) == 0 && value(first.Get(), UIA_HasKeyboardFocusPropertyId) == 0, "Lists and rows never report focus");
        card.focused = 1;
        check(!tree.refresh() && tree.stats().focusMoves == 4, "Back on confirm");
        ComPtr<IInvokeProvider> press;
        ComPtr<IRawElementProviderSimple> simple;
        ComPtr<IUnknown> unknown;
        check(SUCCEEDED(confirm.As(&simple)) && SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &unknown)) && unknown && SUCCEEDED(unknown.As(&press)), "Buttons expose Invoke");
        check(SUCCEEDED(press->Invoke()) && card.invoked == std::vector<std::string>{"confirm"}, "Invoke routes the button's id");
        card.confirm = false;
        check(!tree.refresh() && press->Invoke() == UIA_E_ELEMENTNOTENABLED && card.invoked.size() == 1 && value(confirm.Get(), UIA_IsEnabledPropertyId) == 0,
              "A disabled button refuses Invoke");
        // A disabled focused button reports no keyboard focus (nothing to
        // announce) and refuses SetFocus; the area moves the focus to Cancel.
        check(value(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 0 && value(confirm.Get(), UIA_IsKeyboardFocusablePropertyId) == 0 &&
              SUCCEEDED(tree.focused(focus.ReleaseAndGetAddressOf())) && !focus && tree.stats().focusMoves == 4, "A disabled button holds no focus");
        check(confirm->SetFocus() == UIA_E_ELEMENTNOTENABLED && card.focusRequests.size() == 1, "A disabled button refuses SetFocus");
        card.focused = 0;
        check(!tree.refresh() && tree.stats().focusMoves == 5, "Focus returning to an enabled button is announced");
        ComPtr<IRawElementProviderSimple> titleSimple;
        title.As(&titleSimple);
        check(SUCCEEDED(titleSimple->GetPatternProvider(UIA_InvokePatternId, unknown.ReleaseAndGetAddressOf())) && !unknown, "Text parts expose no Invoke");

        // Progress: a read-only range in percent; parts keep their identity.
        card.progress = 42;
        check(tree.refresh() && tree.parts() == 6 + card.rows && same(go(dialog.Get(), NavigateDirection_FirstChild).Get(), title.Get()), "The progress bar joins the dialog");
        const auto bar = find(dialog.Get(), L"macImport.progress");
        ComPtr<IRawElementProviderSimple> barSimple;
        ComPtr<IRangeValueProvider> range;
        bar.As(&barSimple);
        check(SUCCEEDED(barSimple->GetPatternProvider(UIA_RangeValuePatternId, unknown.ReleaseAndGetAddressOf())) && unknown && SUCCEEDED(unknown.As(&range)), "Progress exposes RangeValue");
        double d{};
        BOOL readOnly{};
        check(SUCCEEDED(range->get_Value(&d)) && d == 42 && SUCCEEDED(range->get_Maximum(&d)) && d == 100 && SUCCEEDED(range->get_IsReadOnly(&readOnly)) && readOnly &&
              range->SetValue(10) == UIA_E_INVALIDOPERATION && value(bar.Get(), UIA_ControlTypePropertyId) == UIA_ProgressBarControlTypeId, "Read-only percent progress");
        card.progress = 57;
        check(!tree.refresh() && SUCCEEDED(range->get_Value(&d)) && d == 57, "Progress follows without restructuring");

        // Hit testing returns the deepest part; scrolling moves the bounds.
        ComPtr<IRawElementProviderFragment> hit;
        check(SUCCEEDED(tree.fromPoint(100 + 30 * 1.5, 50 + 151 * 1.5, &hit)) && same(hit.Get(), first.Get()), "A row wins over its list and the dialog");
        check(SUCCEEDED(tree.fromPoint(100 + 5 * 1.5, 50 + 5 * 1.5, hit.ReleaseAndGetAddressOf())) && same(hit.Get(), dialog.Get()), "The card plate is the dialog");
        check(SUCCEEDED(tree.fromPoint(10, 10, hit.ReleaseAndGetAddressOf())) && !hit && tree.fromPoint(NAN, 0, hit.ReleaseAndGetAddressOf()) == E_INVALIDARG, "Outside the card nothing");
        card.first = 2;
        check(!tree.refresh() && value(first.Get(), UIA_IsOffscreenPropertyId) == 1 && value(last.Get(), UIA_IsOffscreenPropertyId) == 0, "Scrolling changes which rows are on screen");

        // A new step (title changes) keeps parts; fewer rows disconnect the rest.
        card.rows = 3;
        card.first = 0;
        check(tree.refresh() && tree.parts() == 6 + 3, "Rows that vanished leave the tree");
        check(last->get_BoundingRectangle(&r) == UIA_E_ELEMENTNOTAVAILABLE && text(first.Get(), UIA_NamePropertyId) == L"Row 0: Ready", "Removed parts are disconnected, kept ones stay");

        // A malformed (not pre-order) list exposes nothing.
        {
            Card broken;
            auto* other = new Group();
            n::MacImportAutomationHost h = broken.host(other);
            h.nodes = [] { Node a; a.id = "row"; a.role = Role::item; a.parent = 3; return std::vector<Node>{a}; };
            n::NativeMacImportAutomation bad(h);
            check(!bad.refresh() && bad.size() == 0, "Parents must precede their children");
            other->Release();
        }

        // The card closes: every part is disconnected.
        card.shown = false;
        check(tree.refresh() && tree.size() == 0 && tree.parts() == 0 && confirm->get_BoundingRectangle(&r) == UIA_E_ELEMENTNOTAVAILABLE &&
              press->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "A hidden card lists nothing; old parts answer ElementNotAvailable");
        const auto moves = tree.stats().focusMoves;
        card.shown = true;
        check(tree.refresh() && tree.size() == 1, "Showing the card again connects fresh parts");
        check(tree.stats().focusMoves == moves + 1, "The shown card announces its focused button again");
        tree.disconnect();
        check(tree.size() == 0 && tree.parts() == 0, "Disconnect drops every part");
        check(tree.refresh() && tree.stats().focusMoves == moves + 2, "After a disconnect the next listing announces its focus again");
        tree.disconnect();
        group->tree = nullptr;
    }
    check(group->count() == 1, "The tree releases the owner's group");
    group->Release();
}
}

int main() {
    try {
        run();
        std::cout << "PASS " << checks << " import card UI Automation checks; synthetic nodes only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
