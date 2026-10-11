// Hidden WARP fixture for the floating charge alert panel: injected screens,
// synthetic reading, offscreen target; the window is never shown and no
// setting is written (the position callback is captured in memory).
#ifdef _WIN32
#include "native/charge_indicator_panel.hpp"
#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>

namespace {
namespace gpu = endfield::native;
namespace c = endfield::core;
namespace m = endfield::modules;
unsigned checks{};
void check(bool v, const std::string& message) {
    ++checks;
    if (!v) throw std::runtime_error(message);
}
using Microsoft::WRL::ComPtr;
std::wstring wideText(const std::string& text) {
    std::wstring w(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), w.data(), static_cast<int>(w.size()));
    w.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    return w;
}
template <class T> std::wstring textProperty(const ComPtr<T>& element, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> simple;
    std::wstring out;
    if (!element || FAILED(element.As(&simple))) return out;
    VARIANT v;
    VariantInit(&v);
    if (SUCCEEDED(simple->GetPropertyValue(id, &v)) && v.vt == VT_BSTR && v.bstrVal) out = v.bstrVal;
    VariantClear(&v);
    return out;
}
template <class T> long integerProperty(const ComPtr<T>& element, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> simple;
    if (!element || FAILED(element.As(&simple))) return -2;
    VARIANT v;
    VariantInit(&v);
    long out = -1;
    if (SUCCEEDED(simple->GetPropertyValue(id, &v))) out = v.vt == VT_BOOL ? (v.boolVal == VARIANT_TRUE ? 1 : 0) : v.vt == VT_I4 ? v.lVal : -1;
    VariantClear(&v);
    return out;
}
bool sameElement(IUnknown* a, IUnknown* b) {
    ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}
bool rectEquals(const UiaRect& r, double x, double y, double w, double h) {
    return std::abs(r.left - x) < 1e-9 && std::abs(r.top - y) < 1e-9 && std::abs(r.width - w) < 1e-9 && std::abs(r.height - h) < 1e-9;
}
// Dispatches the UI Automation presses queued on the panel's own hidden window.
unsigned pumpInvokes(const gpu::ChargeIndicatorPanel& panel) {
    unsigned count{};
    MSG msg;
    const auto hwnd = static_cast<HWND>(panel.window());
    const auto id = gpu::ChargeIndicatorPanel::automationInvokeMessage();
    while (hwnd && PeekMessageW(&msg, hwnd, id, id, PM_REMOVE)) { DispatchMessageW(&msg); ++count; }
    return count;
}

std::array<int, 4> pixel(const gpu::Readback& image, double x, double y) {
    const auto px = static_cast<unsigned>(std::lround(x)), py = static_cast<unsigned>(std::lround(y));
    if (px >= image.width || py >= image.height) return {0, 0, 0, 0};
    const auto* q = &image.pixels[static_cast<std::size_t>(py) * image.rowBytes + px * 4];
    return {q[0], q[1], q[2], q[3]};
}

void run(const std::filesystem::path& shader) {
    gpu::LayerRasterizer raster;
    std::vector<std::optional<m::ChargePosition>> finished;
    gpu::ChargeIndicatorPanel panel(raster, {shader, true}, [&](std::optional<m::ChargePosition> p) { finished.push_back(p); });
    const std::vector<m::ChargeWorkArea> screens{{11, {0, 0, 1440, 900}, 1}, {22, {1440, 0, 1920, 1040}, 2}};
    panel.setScreens(screens, 0);
    m::ChargeIndicatorAppearance appearance;
    panel.setAppearance(appearance);
    auto& state = panel.state();
    m::BatteryReading reading;
    reading.present = reading.pluggedIn = true;
    reading.percentage = 49;
    state.update(reading, false, 0);
    panel.advance(0);
    check(!panel.window() && !panel.requiresFrames(0) && !panel.nextWakeTime(), "No window, frames or deadlines before an alert");
    state.show(false, 3, true, 1);
    panel.advance(1);
    check(panel.window() && !panel.windowVisible() && state.windowVisible(), "Alert creates its own hidden test window");
    const auto rect = panel.windowRect();
    check(rect.x == 570 && rect.y == -12 && rect.width == 300 && rect.height == 84, "Top-center placement 30 points from the work-area top");
    for (double t = 1; t < 2.6; t += 1.0 / 60) panel.advance(t);
    check(state.stage() == m::ChargeStage::compact && !panel.requiresFrames(2.6), "Entrance settles compact without frames");
    auto image = panel.readbackForTests();
    const auto plate = pixel(image, 190, 26);
    check(plate[3] > 240 && std::abs(plate[2] - 27) <= 3, "Compact plate rendered in the panel");
    check(panel.accessibilityLabel() == "EndfieldHUD, Battery: 49%, Power connected", "Window name carries the accessibility label");
    check(panel.nextWakeTime() && std::abs(*panel.nextWakeTime() - 5.5) < 1e-6, "Dismissal deadline after the entrance");
    panel.advance(5.5);
    check(panel.requiresFrames(5.6), "Dismissal animates the exit");
    for (double t = 5.5; t < 6.3; t += 1.0 / 60) panel.advance(t);
    check(!state.windowVisible() && !panel.requiresFrames(6.3) && !panel.nextWakeTime(), "Panel is ordered out after the exit");
    // Second monitor at 200%: scale and per-monitor pixels.
    auto settings = state.settings();
    settings.customPlacement = true;
    settings.customScreenID = 22;
    settings.customX = 0.5; settings.customY = 0.5;
    settings.scale = 1.25;
    state.setSettings(settings);
    state.show(true, 0, true, 7);
    panel.advance(7);
    const auto big = panel.windowRect();
    check(big.width == 750 && big.height == 210 && std::abs(big.x - (1440 + 960 - 375)) < 1e-9 && std::abs(big.y - (520 - 105)) < 1e-9,
          "Custom position on a 200% display at alert scale 1.25");
    state.hide(false, 8);
    panel.advance(8);
    // Position editing: drag, buttons and keys.
    settings.customPlacement = false;
    settings.scale = 1;
    state.setSettings(settings);
    check(panel.beginPositionEditing(m::chargeDemoReading(), 10) && state.editingPosition(), "Editor opens");
    const auto editor = panel.windowRect();
    check(editor.width == 300 && editor.height == 100 && editor.x == 570 && editor.y == -12, "Editor keeps the indicator centred with controls below");
    image = panel.readbackForTests();
    check(pixel(image, 150 + 20, 66 + 14)[3] > 200 && pixel(image, 150 - 20, 66 + 14)[3] > 200, "Confirm and cancel buttons are painted");
    // Accessibility of the editor (OverlayController: ChargeIndicatorView is a
    // static text; the PositionActionButtons carry labels and tool tips).
    ComPtr<IRawElementProviderFragment> indicator, discardButton, confirmButton;
    {
        ComPtr<IRawElementProviderFragmentRoot> root;
        ComPtr<IRawElementProviderFragment> fragment, last, end, parent, hit;
        ComPtr<IRawElementProviderSimple> simple, host;
        check(panel.automationRoot(&root) == S_OK && root && SUCCEEDED(root.As(&fragment)) && SUCCEEDED(root.As(&simple)) && panel.automationChildren() == 3,
              "The editor's UI Automation root lists the indicator and both buttons");
        check(SUCCEEDED(simple->get_HostRawElementProvider(&host)) && host && textProperty(root, UIA_AutomationIdPropertyId) == L"chargeAlert",
              "The root is hosted by the alert window");
        check(SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, &indicator)) && indicator &&
              SUCCEEDED(indicator->Navigate(NavigateDirection_NextSibling, &discardButton)) && discardButton &&
              SUCCEEDED(discardButton->Navigate(NavigateDirection_NextSibling, &confirmButton)) && confirmButton &&
              SUCCEEDED(confirmButton->Navigate(NavigateDirection_NextSibling, &end)) && !end &&
              SUCCEEDED(fragment->Navigate(NavigateDirection_LastChild, &last)) && sameElement(last.Get(), confirmButton.Get()) &&
              SUCCEEDED(confirmButton->Navigate(NavigateDirection_Parent, &parent)) && sameElement(parent.Get(), root.Get()),
              "Children follow the panel's subview order: indicator, cancel, confirm");
        check(textProperty(indicator, UIA_NamePropertyId) == wideText(panel.accessibilityLabel()) && !panel.accessibilityLabel().empty() &&
              integerProperty(indicator, UIA_ControlTypePropertyId) == UIA_TextControlTypeId && textProperty(indicator, UIA_AutomationIdPropertyId) == L"chargeAlert:indicator",
              "The indicator is a static text carrying its accessibility label");
        check(textProperty(discardButton, UIA_NamePropertyId) == L"Discard position" && textProperty(discardButton, UIA_HelpTextPropertyId) == L"Discard (Esc)" &&
              textProperty(confirmButton, UIA_NamePropertyId) == L"Confirm position" && textProperty(confirmButton, UIA_HelpTextPropertyId) == L"Confirm (Return)" &&
              integerProperty(discardButton, UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId && integerProperty(confirmButton, UIA_IsEnabledPropertyId) == 1 &&
              integerProperty(confirmButton, UIA_IsKeyboardFocusablePropertyId) == 0 && integerProperty(confirmButton, UIA_IsOffscreenPropertyId) == 1,
              "The buttons carry the Mac accessibility labels and tool tips (offscreen: the test window is never shown)");
        UiaRect r{};
        check(SUCCEEDED(indicator->get_BoundingRectangle(&r)) && rectEquals(r, 570, -12, 300, 84) &&
              SUCCEEDED(discardButton->get_BoundingRectangle(&r)) && rectEquals(r, 570 + 150 - 34, -12 + 66, 28, 28) &&
              SUCCEEDED(confirmButton->get_BoundingRectangle(&r)) && rectEquals(r, 570 + 150 + 6, -12 + 66, 28, 28),
              "Frames are the indicator view and the 28-point buttons in screen pixels");
        check(SUCCEEDED(root->ElementProviderFromPoint(570 + 150 + 20, -12 + 80, &hit)) && sameElement(hit.Get(), confirmButton.Get()), "The hit test reaches Confirm above the indicator");
        hit.Reset();
        check(SUCCEEDED(root->ElementProviderFromPoint(600, 10, &hit)) && sameElement(hit.Get(), indicator.Get()), "The hit test reaches the indicator");
        hit.Reset();
        check(SUCCEEDED(root->ElementProviderFromPoint(10, 500, &hit)) && !hit, "Outside the window nothing answers");
        const auto tips = panel.toolTips();
        check(tips.size() == 2 && panel.toolTipsActive() && tips[0].text == "Discard (Esc)" && tips[1].text == "Confirm (Return)" &&
              tips[0].client.x == 116 && tips[0].client.y == 66 && tips[0].client.width == 28 && tips[0].client.height == 28 &&
              tips[1].client.x == 156 && tips[1].client.y == 66, "The editor buttons show the system tool tips over their 28-point frames");
    }
    check(panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::down, {720, 30}, 11), "Drag begins on the indicator");
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::move, {820, 230}, 11);
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::up, {820, 230}, 11);
    check(panel.windowRect().x == 670 && panel.windowRect().y == 188, "Window follows the dragged anchor");
    check(panel.key(VK_RETURN, 12) && finished.size() == 1 && finished[0] && finished[0]->screenID == 11u &&
          std::abs(finished[0]->x - 820.0 / 1440) < 1e-12 && std::abs(finished[0]->y - 670.0 / 900) < 1e-12,
          "Return confirms the normalized position");
    check(!state.editingPosition() && !state.windowVisible(), "Confirm closes the editor");
    check(panel.automationChildren() == 1 && !panel.toolTipsActive() && integerProperty(confirmButton, UIA_NamePropertyId) == -1 &&
          textProperty(indicator, UIA_NamePropertyId) == wideText(panel.accessibilityLabel()),
          "Closing the editor withdraws the buttons and deactivates their tool tips");
    check(panel.beginPositionEditing(m::chargeDemoReading(), 13), "Editor reopens");
    check(panel.key(VK_ESCAPE, 14) && finished.size() == 2 && !finished[1], "Escape discards");
    check(panel.beginPositionEditing(m::chargeDemoReading(), 15), "Editor reopens again");
    const auto r = panel.windowRect();
    const c::Point cancel{r.x + 150 - 34 + 14, r.y + 66 + 14};
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::down, cancel, 16);
    check(finished.size() == 2, "Button press waits for release");
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::up, cancel, 16);
    check(finished.size() == 3 && !finished[2] && !state.editingPosition(), "Cancel button discards");
    check(!panel.key(VK_ESCAPE, 17), "Keys are ignored outside editing");
    // A UI Automation press runs the button's action from the window procedure,
    // in the current language; a press queued for a finished edit is dropped.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 18) && panel.automationChildren() == 3 && panel.toolTipsActive(), "The editor relists its buttons");
    auto chinese = appearance;
    chinese.language = c::Language::simplifiedChinese;
    panel.setAppearance(chinese);
    panel.advance(18.1);
    {
        ComPtr<IRawElementProviderFragmentRoot> root;
        ComPtr<IRawElementProviderFragment> fragment, last;
        ComPtr<IInvokeProvider> invoke;
        check(panel.automationRoot(&root) == S_OK && SUCCEEDED(root.As(&fragment)) && SUCCEEDED(fragment->Navigate(NavigateDirection_LastChild, &last)) &&
              textProperty(last, UIA_NamePropertyId) == L"确认位置" && textProperty(last, UIA_HelpTextPropertyId) == L"确认（回车）" && SUCCEEDED(last.As(&invoke)),
              "The buttons follow the language");
        const auto tips = panel.toolTips();
        check(tips.size() == 2 && tips[0].text == "取消（Esc）" && tips[1].text == "确认（回车）", "The tool tips follow the language");
        check(invoke->Invoke() == S_OK && finished.size() == 3 && state.editingPosition(), "The press waits for the window procedure");
        check(pumpInvokes(panel) == 1 && finished.size() == 4 && finished[3] && !state.editingPosition() && panel.automationChildren() == 1,
              "The posted press confirms like the button");
        check(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "The withdrawn button refuses presses");
        check(panel.beginPositionEditing(m::chargeDemoReading(), 19), "Editor opens for a stale press");
        ComPtr<IRawElementProviderFragment> first, discardPeer;
        ComPtr<IInvokeProvider> discard;
        check(SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, &first)) && SUCCEEDED(first->Navigate(NavigateDirection_NextSibling, &discardPeer)) &&
              SUCCEEDED(discardPeer.As(&discard)) && discard->Invoke() == S_OK, "Discard is pressed");
        check(panel.key(VK_ESCAPE, 19.1) && finished.size() == 5 && !finished[4], "Escape finishes the edit before the press runs");
        check(panel.beginPositionEditing(m::chargeDemoReading(), 19.2) && pumpInvokes(panel) == 1 && state.editingPosition() && finished.size() == 5,
              "The stale press does not touch the next edit");
        check(panel.key(VK_ESCAPE, 19.3) && finished.size() == 6, "Escape discards the next edit");
    }
    panel.setAppearance(appearance);
    ComPtr<IRawElementProviderFragmentRoot> before;
    check(panel.automationRoot(&before) == S_OK, "The root exists before shutdown");
    panel.shutdown();
    check(!panel.window() && panel.automationChildren() == 0 && panel.toolTips().empty(), "Shutdown releases the window, device, tool tips and UI Automation");
    ComPtr<IRawElementProviderFragment> orphan;
    check(before->ElementProviderFromPoint(600, 10, &orphan) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && !orphan &&
          panel.automationRoot(&before) == E_FAIL, "The disconnected root answers no more");
    // A presentation whose device/window cannot be created is not fatal: the
    // alert state and its deadlines continue, and the failure is reported.
    gpu::ChargeIndicatorPanel broken(raster, {shader.parent_path() / "missing-shader.hlsl", true});
    broken.setScreens(screens, 0);
    broken.setAppearance(appearance);
    broken.state().update(reading, false, 0);
    broken.state().show(false, 3, true, 1);
    broken.advance(1);
    check(!broken.window() && !broken.lastFailure().empty() && broken.state().windowVisible(), "Device failure is reported, not thrown");
    for (double t = 1; t < 6.3; t += 1.0 / 60) broken.advance(t);
    check(!broken.state().windowVisible() && !broken.nextWakeTime(), "Alert timeline still completes without a window");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result) && argc == 2, "Pass HLSL to the synthetic charge alert panel test");
        run(argv[1]);
        CoUninitialize();
        std::cout << "Charge alert panel: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(result)) CoUninitialize();
        std::cerr << "Charge alert panel after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
