// Contract suite of native/hud_design_automation (the one UIA element tree):
// the design roles and patterns of LIBRARY-DESIGN.md section 9, identity per
// id and role across refreshes, disconnection, disabled refusals, the
// re-rooting of a re-registered group and the hit order. No UIA client, no
// window, no input; the owner's actions are fakes that record what reached them.
#include "native/hud_design_automation.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace endfield;
using Microsoft::WRL::ComPtr;
namespace d = modules::design;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
// A group the tree is parented to (AddRef'd while held).
class FakeRoot final : public IRawElementProviderFragmentRoot {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderFragmentRoot)) { *out = static_cast<IRawElementProviderFragmentRoot*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs; } // stack object
    IFACEMETHODIMP ElementProviderFromPoint(double, double, IRawElementProviderFragment** out) override { *out = nullptr; return S_OK; }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override { *out = nullptr; return S_OK; }
    std::atomic<ULONG> refs{1};
};
d::AccessibilityItem item(std::string id, d::AccessibilityRole role, double x, std::string value = {}, bool on = false) {
    d::AccessibilityItem i;
    i.id = id; i.label = "Label " + id; i.role = role; i.value = std::move(value); i.on = on; i.rect = {x, 10, 40, 20};
    return i;
}
long controlType(IRawElementProviderFragment* f) {
    ComPtr<IRawElementProviderSimple> simple;
    check(SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&simple))), "Every element is a simple provider");
    VARIANT v; VariantInit(&v);
    check(SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &v)) && v.vt == VT_I4, "Control type is reported");
    return v.lVal;
}
template <class P> ComPtr<P> pattern(IRawElementProviderFragment* f, PATTERNID id) {
    ComPtr<IRawElementProviderSimple> simple;
    f->QueryInterface(IID_PPV_ARGS(&simple));
    ComPtr<IUnknown> unknown;
    if (FAILED(simple->GetPatternProvider(id, &unknown)) || !unknown) return {};
    ComPtr<P> out;
    unknown.As(&out);
    return out;
}
void run() {
    // --- the role mapping (pure) ---------------------------------------------
    {
        std::vector<d::AccessibilityItem> items{item("b", d::AccessibilityRole::button, 0), item("s", d::AccessibilityRole::slider, 0, "1.00×"),
            item("t", d::AccessibilityRole::text, 0, "42 %"), item("e", d::AccessibilityRole::edit, 0, "abc")};
        items[1].number = 2; items[1].minimum = 1; items[1].maximum = 3; items[1].step = .5;
        items[3].focused = true;
        const auto nodes = native::automationNodes(items);
        using R = native::AutomationNode::Role;
        check(nodes.size() == 4 && nodes[0].role == R::button && !nodes[0].valued, "A design button is an invocable button without a value (the Mac's nil value)");
        check(nodes[1].role == R::slider && nodes[1].help == "1.00×" && nodes[1].number == 2 && nodes[1].step == .5, "A slider's help is its formatted reading; its range and step are kept");
        check(nodes[2].role == R::text && nodes[2].valued && nodes[2].value == "42 %", "A readout is text with a read-only value");
        check(nodes[3].role == R::edit && nodes[3].focusable && nodes[3].focused, "An editing field is focusable and holds the focus");
    }
    // --- the tree through NativeDesignAutomation ---------------------------------
    FakeRoot group, other;
    std::vector<d::AccessibilityItem> items{
        item("button", d::AccessibilityRole::button, 0), item("toggle", d::AccessibilityRole::toggle, 50, {}, true),
        item("dropdown", d::AccessibilityRole::popUp, 100, "English"), item("row", d::AccessibilityRole::menuItem, 150, {}, true),
        item("tab", d::AccessibilityRole::tab, 200, {}, false), item("slider", d::AccessibilityRole::slider, 250, "1.00×"),
        item("readout", d::AccessibilityRole::text, 300, "42 %"), item("field", d::AccessibilityRole::edit, 350, "abc"),
        item("group", d::AccessibilityRole::group, 400, "3 / 5"), item("off", d::AccessibilityRole::button, 450)};
    items[5].minimum = 0; items[5].maximum = 2; items[5].number = 1; items[5].step = .25;
    items[9].enabled = false;
    std::vector<std::string> log;
    native::DesignAutomationHost host;
    host.root = &group;
    host.items = [&] { return items; };
    host.toScreen = [](core::Rect r) -> std::optional<core::Rect> { return core::Rect{r.x + 1000, r.y + 500, r.width, r.height}; };
    host.invoke = [&](std::string_view id) { log.push_back("invoke:" + std::string(id)); return true; };
    host.setValue = [&](std::string_view id, double v) { log.push_back("value:" + std::string(id) + ":" + std::to_string(v)); return true; };
    host.setText = [&](std::string_view id, std::string_view t) { log.push_back("text:" + std::string(id) + ":" + std::string(t)); return true; };
    host.focus = [&](std::string_view id) { log.push_back("focus:" + std::string(id)); return true; };
    host.expand = [&](std::string_view id, bool e) { log.push_back(std::string(e ? "expand:" : "collapse:") + std::string(id)); return true; };
    unsigned changed{};
    host.changed = [&] { ++changed; };
    {
        native::NativeDesignAutomation tree(std::move(host));
        check(group.refs > 1, "The tree holds its group");
        check(tree.refresh(&group) && tree.size() == items.size(), "One element per item in router order");
        check(!tree.refresh(&group), "An unchanged listing changes nothing");
        std::vector<ComPtr<IRawElementProviderFragment>> e(items.size());
        for (std::size_t n = 0; n < e.size(); ++n) check(SUCCEEDED(tree.child(n, &e[n])) && e[n], "Every element is listed");
        check(controlType(e[0].Get()) == UIA_ButtonControlTypeId && controlType(e[1].Get()) == UIA_ButtonControlTypeId && controlType(e[2].Get()) == UIA_ComboBoxControlTypeId &&
              controlType(e[3].Get()) == UIA_MenuItemControlTypeId && controlType(e[4].Get()) == UIA_TabItemControlTypeId && controlType(e[5].Get()) == UIA_SliderControlTypeId &&
              controlType(e[6].Get()) == UIA_TextControlTypeId && controlType(e[7].Get()) == UIA_EditControlTypeId && controlType(e[8].Get()) == UIA_GroupControlTypeId,
              "Roles map to the UIA control types of section 9");
        // Button + Invoke; a disabled one refuses like accessibilityPerformPress.
        check(pattern<IInvokeProvider>(e[0].Get(), UIA_InvokePatternId) && SUCCEEDED(pattern<IInvokeProvider>(e[0].Get(), UIA_InvokePatternId)->Invoke()) && log.back() == "invoke:button", "Invoke presses the button");
        check(changed == 1, "An accepted action presents a frame");
        check(pattern<IInvokeProvider>(e[9].Get(), UIA_InvokePatternId)->Invoke() == UIA_E_ELEMENTNOTENABLED && log.back() == "invoke:button", "A disabled control refuses its pattern");
        check(!pattern<IValueProvider>(e[0].Get(), UIA_ValuePatternId), "A design button has no value");
        // Toggle.
        auto toggle = pattern<IToggleProvider>(e[1].Get(), UIA_TogglePatternId);
        ToggleState state{};
        check(toggle && SUCCEEDED(toggle->get_ToggleState(&state)) && state == ToggleState_On && SUCCEEDED(toggle->Toggle()) && log.back() == "invoke:toggle", "The switch toggles by its press");
        // PopUp: ExpandCollapse (pressing toggles the menu) + read-only value.
        auto expand = pattern<IExpandCollapseProvider>(e[2].Get(), UIA_ExpandCollapsePatternId);
        ExpandCollapseState expanded{};
        check(expand && SUCCEEDED(expand->get_ExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Collapsed, "A closed dropdown reports collapsed");
        const auto before = log.size();
        check(SUCCEEDED(expand->Collapse()) && log.size() == before, "Collapsing a closed dropdown changes nothing");
        check(SUCCEEDED(expand->Expand()) && log.back() == "expand:dropdown", "Expand opens its menu");
        auto popValue = pattern<IValueProvider>(e[2].Get(), UIA_ValuePatternId);
        BSTR text{}; BOOL readOnly{};
        check(popValue && SUCCEEDED(popValue->get_Value(&text)) && std::wstring(text) == L"English" && SUCCEEDED(popValue->get_IsReadOnly(&readOnly)) && readOnly, "The dropdown's value is read-only");
        SysFreeString(text);
        check(popValue->SetValue(L"x") == UIA_E_INVALIDOPERATION, "Only edits take text");
        // MenuItem: Invoke + SelectionItem; Tab: SelectionItem.
        auto row = pattern<ISelectionItemProvider>(e[3].Get(), UIA_SelectionItemPatternId);
        BOOL selected{};
        check(row && SUCCEEDED(row->get_IsSelected(&selected)) && selected && pattern<IInvokeProvider>(e[3].Get(), UIA_InvokePatternId), "A checked menu row is selected and invocable");
        auto tab = pattern<ISelectionItemProvider>(e[4].Get(), UIA_SelectionItemPatternId);
        check(tab && !pattern<IInvokeProvider>(e[4].Get(), UIA_InvokePatternId) && SUCCEEDED(tab->Select()) && log.back() == "invoke:tab", "A tab selects by its press");
        // Slider: RangeValue clamps and commits; the reading is a read-only value.
        auto range = pattern<IRangeValueProvider>(e[5].Get(), UIA_RangeValuePatternId);
        double step{};
        check(range && SUCCEEDED(range->get_SmallChange(&step)) && step == .25, "The slider's step");
        check(SUCCEEDED(range->SetValue(9)) && log.back() == "value:slider:2.000000", "Writes clamp into the range and commit");
        check(range->SetValue(std::nan("")) == E_INVALIDARG, "A non-finite value is refused");
        // Text and group: read-only values.
        auto readout = pattern<IValueProvider>(e[6].Get(), UIA_ValuePatternId);
        check(readout && SUCCEEDED(readout->get_IsReadOnly(&readOnly)) && readOnly, "A readout's value is read-only");
        auto groupValue = pattern<IValueProvider>(e[8].Get(), UIA_ValuePatternId);
        check(groupValue && SUCCEEDED(groupValue->get_Value(&text)) && std::wstring(text) == L"3 / 5", "A group carries its value");
        SysFreeString(text);
        // Edit: writable value, SetFocus begins editing.
        auto edit = pattern<IValueProvider>(e[7].Get(), UIA_ValuePatternId);
        check(edit && SUCCEEDED(edit->get_IsReadOnly(&readOnly)) && !readOnly, "An enabled edit is writable");
        check(SUCCEEDED(edit->SetValue(L"Grüße")) && log.back() == "text:field:Grüße", "Text reaches the field as UTF-8");
        check(SUCCEEDED(e[7]->SetFocus()) && log.back() == "focus:field", "SetFocus on an edit begins editing");
        IRawElementProviderFragment* focus{};
        check(SUCCEEDED(tree.focused(&focus)) && !focus, "Nothing holds the focus before editing");
        // Bounding rectangles project on demand; the hit test takes the topmost.
        UiaRect bounds{};
        check(SUCCEEDED(e[0]->get_BoundingRectangle(&bounds)) && bounds.left == 1000 && bounds.top == 510 && bounds.width == 40, "Rects project through toScreen");
        ComPtr<IRawElementProviderFragment> hit;
        check(SUCCEEDED(tree.fromPoint(1260, 520, &hit)) && hit.Get() == e[5].Get(), "The element under a screen point");
        // Identity across refreshes; a vanished item disconnects; focus moves.
        items[7].focused = true;
        items[2].on = true;
        items.erase(items.begin() + 6);
        check(tree.refresh(&group) && tree.size() == items.size(), "A removed item changes the structure");
        ComPtr<IRawElementProviderFragment> again;
        check(SUCCEEDED(tree.child(0, &again)) && again.Get() == e[0].Get(), "Elements keep their COM identity per id and role");
        ComPtr<IRawElementProviderSimple> gone;
        e[6].As(&gone);
        VARIANT v; VariantInit(&v);
        check(gone->GetPropertyValue(UIA_NamePropertyId, &v) == UIA_E_ELEMENTNOTAVAILABLE, "A vanished element answers not available");
        check(SUCCEEDED(expand->get_ExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Expanded, "An open dropdown reports expanded");
        check(SUCCEEDED(tree.focused(&focus)) && focus == e[7].Get(), "The editing field holds the keyboard focus");
        focus->Release();
        check(tree.stats().focusMoves == 1, "The focus move is counted once");
        items[1].role = d::AccessibilityRole::button;
        tree.refresh(&group);
        ComPtr<IRawElementProviderFragment> retyped;
        check(SUCCEEDED(tree.child(1, &retyped)) && retyped.Get() != e[1].Get(), "A new role is a new element");
        // Re-rooting: a re-registered group disconnects every element first.
        check(tree.refresh(&other) && other.refs > 1, "A new group re-roots the tree");
        check(e[0]->GetRuntimeId(nullptr) == E_POINTER, "Null out pointers are refused");
        SAFEARRAY* id{};
        check(e[0]->GetRuntimeId(&id) == UIA_E_ELEMENTNOTAVAILABLE, "Elements of the previous group are disconnected");
        check(tree.stats().refreshes >= 5 && tree.stats().invokes == 1 && tree.stats().toggles == 1 && tree.stats().selections == 1 && tree.stats().texts == 1, "Statistics survive the re-rooting");
    }
    check(group.refs == 1 && other.refs == 1, "The groups are released with the tree");
}
}
int wmain() {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com), "COM apartment");
        run();
        CoUninitialize();
        std::cout << "HUD design automation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "HUD design automation after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
