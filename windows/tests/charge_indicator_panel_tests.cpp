// Hidden WARP fixture for the floating charge alert panel: injected screens,
// synthetic reading, offscreen target; the window is never shown and no
// setting is written (the bar editor's callback is captured in memory). No
// input is injected into the system: the panel's pointer and key entry points
// are called directly.
#ifdef _WIN32
#include "native/charge_indicator_panel.hpp"
#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <limits>

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
    std::vector<std::optional<m::ChargeBarLayout>> finished;
    gpu::ChargeIndicatorPanel panel(raster, {shader, true}, [&](std::optional<m::ChargeBarLayout> layout) { finished.push_back(layout); });
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
    // The bar editor: the place-and-confirm strip (Size, Length, Cancel,
    // Confirm) at 0.7 beside the bar; drag, keys, pills and drafts.
    settings.customPlacement = false;
    settings.scale = 1;
    state.setSettings(settings);
    check(panel.beginPositionEditing(m::chargeDemoReading(), 10) && state.editingPosition() && panel.editor(), "Editor opens with its strip");
    {
        const auto* strip = panel.editor();
        check(strip->sliders().size() == 2 && strip->sliders()[0]->id() == "chargeBar.size" && strip->sliders()[1]->id() == "chargeBar.length" &&
              *strip->value("chargeBar.size") == 100 && *strip->value("chargeBar.length") == 18 && strip->sliders()[1]->range().minimum == 14 &&
              strip->sliders()[0]->range().minimum == 65 && strip->sliders()[0]->range().maximum == 160,
              "Size 65 to 160 % at 100 %, and the classic bar's 18 % Length (14 to 100 %) on a 1440-point display");
        const auto stripRect = *panel.stripRect();
        const auto size = strip->size();
        check(std::abs(stripRect.width - size.width * .7) < 1e-9 && std::abs(stripRect.height - (size.height + 8) * .7) < 1e-9 &&
              std::abs(stripRect.y - (48 + 19 * 1.6 + 10)) < 1e-9 && std::abs(stripRect.x + stripRect.width / 2 - 720) < 1e-9,
              "The strip is drawn at 0.7 with its bubble room, 10 points below the reach of the largest Size, centred on the bar");
        const auto editor = panel.windowRect();
        check(editor.y == -12 && std::abs(editor.x - stripRect.x) < 1e-9 && std::abs(editor.y + editor.height - (stripRect.y + stripRect.height)) < 1e-9,
              "The window holds the bar's canvas and the strip (no room reserved below the bar)");
        // The confirm bar's entrance: the band wipes open, the rows and pills slide in.
        check(panel.requiresFrames(10.1), "The strip's entrance asks for frames");
        for (double t = 10; t < 10.8; t += 1.0 / 60) panel.advance(t);
        check(!panel.requiresFrames(10.8), "The settled strip asks for no frames");
        image = panel.readbackForTests();
        const auto at = [&](c::Point local) { const auto p = *panel.stripToScreen(local); return pixel(image, p.x - editor.x, p.y - editor.y); };
        const auto confirmFrame = strip->confirm().layer().frame();
        check(at({size.width / 2, 4})[3] > 200 && at({confirmFrame.x + confirmFrame.width / 2, confirmFrame.y + confirmFrame.height / 2})[3] > 200 &&
              pixel(image, 720 - editor.x, 30 - editor.y)[3] > 200, "The band, the Confirm pill and the bar are painted");
    }
    // Accessibility of the editor (OverlayController: ChargeIndicatorView is a
    // static text; HUDAccessibilityStandIns list the strip's sliders and pills).
    ComPtr<IRawElementProviderFragment> indicator, lengthPeer, cancelPeer, confirmPeer;
    {
        ComPtr<IRawElementProviderFragmentRoot> root;
        ComPtr<IRawElementProviderFragment> fragment, sizePeer, last, end, parent;
        ComPtr<IRawElementProviderSimple> simple, host;
        check(panel.automationRoot(&root) == S_OK && root && SUCCEEDED(root.As(&fragment)) && SUCCEEDED(root.As(&simple)) && panel.automationChildren() == 5,
              "The editor's UI Automation root lists the indicator and the strip's sliders and pills");
        check(SUCCEEDED(simple->get_HostRawElementProvider(&host)) && host && textProperty(root, UIA_AutomationIdPropertyId) == L"chargeAlert",
              "The root is hosted by the alert window");
        check(SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, &indicator)) && indicator &&
              SUCCEEDED(indicator->Navigate(NavigateDirection_NextSibling, &sizePeer)) && sizePeer &&
              SUCCEEDED(sizePeer->Navigate(NavigateDirection_NextSibling, &lengthPeer)) && lengthPeer &&
              SUCCEEDED(lengthPeer->Navigate(NavigateDirection_NextSibling, &cancelPeer)) && cancelPeer &&
              SUCCEEDED(cancelPeer->Navigate(NavigateDirection_NextSibling, &confirmPeer)) && confirmPeer &&
              SUCCEEDED(confirmPeer->Navigate(NavigateDirection_NextSibling, &end)) && !end &&
              SUCCEEDED(fragment->Navigate(NavigateDirection_LastChild, &last)) && sameElement(last.Get(), confirmPeer.Get()) &&
              SUCCEEDED(confirmPeer->Navigate(NavigateDirection_Parent, &parent)) && sameElement(parent.Get(), root.Get()),
              "Children: the indicator, then Size, Length, Cancel and Confirm");
        check(textProperty(indicator, UIA_NamePropertyId) == wideText(panel.accessibilityLabel()) && !panel.accessibilityLabel().empty() &&
              integerProperty(indicator, UIA_ControlTypePropertyId) == UIA_TextControlTypeId && textProperty(indicator, UIA_AutomationIdPropertyId) == L"chargeAlert:indicator",
              "The indicator is a static text carrying its accessibility label");
        check(textProperty(sizePeer, UIA_NamePropertyId) == L"Size" && textProperty(lengthPeer, UIA_NamePropertyId) == L"Length" &&
              integerProperty(sizePeer, UIA_ControlTypePropertyId) == UIA_SliderControlTypeId &&
              textProperty(cancelPeer, UIA_NamePropertyId) == L"Cancel" && textProperty(confirmPeer, UIA_NamePropertyId) == L"Confirm" &&
              integerProperty(confirmPeer, UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId && integerProperty(confirmPeer, UIA_IsEnabledPropertyId) == 1 &&
              textProperty(confirmPeer, UIA_HelpTextPropertyId).empty() && integerProperty(confirmPeer, UIA_IsOffscreenPropertyId) == 1,
              "The strip's sliders and pills carry the Mac names, no tool tips (offscreen: the test window is never shown)");
        const auto nodes = panel.automationNodes();
        const auto stripRect = *panel.stripRect();
        const auto pill = panel.editor()->confirm().layer().frame();
        const auto corner = *panel.stripToScreen({pill.x, pill.y});
        check(nodes.size() == 5 && nodes[0].rect == c::Rect{570, -12, 300, 84} && std::abs(nodes[4].rect.x - corner.x) < 1e-9 &&
              std::abs(nodes[4].rect.y - corner.y) < 1e-9 && std::abs(nodes[4].rect.width - pill.width * .7) < 1e-9 &&
              nodes[4].rect.x >= stripRect.x && nodes[4].rect.y >= stripRect.y, "Frames: the indicator view and the strip's pills scaled by 0.7 in screen pixels");
        // Length through RangeValue, run from the window procedure.
        ComPtr<IRangeValueProvider> range;
        check(SUCCEEDED(lengthPeer.As(&range)) && range->SetValue(50) == S_OK && !state.barSettings().length, "The value waits for the window procedure");
        check(pumpInvokes(panel) == 1 && *panel.editor()->value("chargeBar.length") == 50 && state.barSettings().length == 0.5 &&
              panel.windowRect().x == 720 - (720 + 36) / 2.0 && panel.windowRect().width == 720 + 36, "The Length value applies as a draft: the bar stretches");
    }
    // Hover lights the strip's controls only.
    {
        const auto pill = panel.editor()->confirm().layer().frame();
        const auto centre = *panel.stripToScreen({pill.x + pill.width / 2, pill.y + pill.height / 2});
        panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::move, centre, 10.5);
        check(panel.editor()->confirm().hovered() && !panel.editor()->cancel().hovered(), "Hover lights the Confirm pill");
        panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::move, {720, 30}, 10.6);
        check(!panel.editor()->confirm().hovered(), "Hover leaves it over the bar");
    }
    check(panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::down, {720, 30}, 11) && panel.editor()->dragging(), "Drag begins on the bar");
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::move, {820, 230}, 11);
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::up, {820, 230}, 11);
    check(panel.windowRect().x == 820 - 378 && panel.windowRect().y == 188 && state.draftMoved(), "Window follows the dragged anchor");
    check(panel.key(VK_RETURN, 12) && finished.size() == 1 && finished[0] && finished[0]->position && finished[0]->position->screenID == 11u &&
          std::abs(finished[0]->position->x - 820.0 / 1440) < 1e-12 && std::abs(finished[0]->position->y - 670.0 / 900) < 1e-12 &&
          finished[0]->scale == 1 && finished[0]->length == 0.5, "Return confirms the dragged position with Size and Length");
    check(!state.editingPosition() && !state.windowVisible() && !panel.editor(), "Confirm closes the editor and its strip");
    check(panel.automationChildren() == 1, "Closing the editor withdraws the strip's peers (" + std::to_string(panel.automationChildren()) + " listed)");
    check(textProperty(confirmPeer, UIA_NamePropertyId).empty() && textProperty(indicator, UIA_NamePropertyId) == wideText(panel.accessibilityLabel()),
          "A withdrawn peer answers nothing; the indicator keeps its label");
    // Confirm without a drag keeps the placement (Top centre) and saves only Size and Length.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 13) && !panel.editor()->sliders().empty(), "Editor reopens");
    check(panel.key(VK_RETURN, 13.1) && finished.size() == 2 && finished[1] && !finished[1]->position && finished[1]->scale == 1 && !finished[1]->length,
          "Confirm without a drag keeps Top centre and the classic bar");
    check(panel.beginPositionEditing(m::chargeDemoReading(), 13.2), "Editor reopens");
    check(panel.key(VK_ESCAPE, 14) && finished.size() == 3 && !finished[2], "Escape discards");
    check(panel.beginPositionEditing(m::chargeDemoReading(), 15), "Editor reopens again");
    {
        const auto pill = panel.editor()->cancel().layer().frame();
        const auto cancel = *panel.stripToScreen({pill.x + pill.width / 2, pill.y + pill.height / 2});
        panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::down, cancel, 16);
        check(finished.size() == 3 && !panel.editor()->dragging(), "A press on the Cancel pill waits for release and drags nothing");
        panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::up, cancel, 16);
        check(finished.size() == 4 && !finished[3] && !state.editingPosition(), "The Cancel pill discards");
    }
    check(!panel.key(VK_ESCAPE, 17), "Keys are ignored outside editing");
    // A host cancellation on the state (a summon, Projection, sleep) takes the
    // strip with it on the next frame, without a callback.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 17.2) && panel.editor(), "Editor opens for a host cancellation");
    state.discardPosition(17.3);
    panel.advance(17.3);
    check(!panel.editor() && finished.size() == 4 && panel.automationChildren() == 1 && !panel.key(VK_RETURN, 17.4),
          "A host cancellation withdraws the strip without a callback");
    // A Size slider press: the bar follows the draft while the strip holds
    // still, then settles; a pushed configuration keeps the drafts.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 17.5), "Editor opens for Size");
    {
        const auto before = *panel.stripRect();
        const auto& slider = panel.editor()->sliders()[0]->layer();
        const auto left = *panel.stripToScreen({slider.frame().x + 1, slider.frame().y + 15});
        panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::down, left, 17.6);
        check(state.stripAdjusting() && state.barSettings().scale < 0.7 && *panel.stripRect() == before && panel.windowRect().width == before.width,
              "Pressing Size shrinks the bar under a still strip");
        panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::up, left, 17.7);
        check(!state.stripAdjusting() && state.barSettings().scale < 0.7, "Release keeps the draft and settles the strip");
        auto pushed = state.settings();
        pushed.scale = 1.5; pushed.length = 0.9;
        state.setSettings(pushed);
        panel.advance(17.8);
        check(state.barSettings().scale < 0.7 && !state.barSettings().length && *panel.editor()->value("chargeBar.size") < 70,
              "A configuration pushed during editing does not overwrite the drafts");
        pushed.scale = 1; pushed.length.reset();
        state.setSettings(pushed);
        check(panel.key(VK_ESCAPE, 17.9) && finished.size() == 5 && !finished[4], "Escape drops the drafts");
    }
    // A UI Automation press runs the pill's action from the window procedure,
    // in the current language; a press queued for a finished edit is dropped.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 18) && panel.automationChildren() == 5, "The editor relists its strip");
    auto chinese = appearance;
    chinese.language = c::Language::simplifiedChinese;
    panel.setAppearance(chinese);
    panel.advance(18.1);
    {
        ComPtr<IRawElementProviderFragmentRoot> root;
        ComPtr<IRawElementProviderFragment> fragment, last;
        ComPtr<IInvokeProvider> invoke;
        check(panel.automationRoot(&root) == S_OK && SUCCEEDED(root.As(&fragment)) && SUCCEEDED(fragment->Navigate(NavigateDirection_LastChild, &last)) &&
              textProperty(last, UIA_NamePropertyId) == L"确认" && SUCCEEDED(last.As(&invoke)) && panel.editor()->sliders()[1]->label == "长度",
              "The strip follows the language");
        check(invoke->Invoke() == S_OK && finished.size() == 5 && state.editingPosition(), "The press waits for the window procedure");
        check(pumpInvokes(panel) == 1 && finished.size() == 6 && finished[5] && !state.editingPosition() && panel.automationChildren() == 1,
              "The posted press confirms like the pill");
        check(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "The withdrawn pill refuses presses");
        check(panel.beginPositionEditing(m::chargeDemoReading(), 19), "Editor opens for a stale press");
        ComPtr<IRawElementProviderFragment> cancelNode;
        ComPtr<IInvokeProvider> cancel;
        check(SUCCEEDED(fragment->Navigate(NavigateDirection_LastChild, &last)) && SUCCEEDED(last->Navigate(NavigateDirection_PreviousSibling, &cancelNode)) &&
              SUCCEEDED(cancelNode.As(&cancel)) && cancel->Invoke() == S_OK, "Cancel is pressed");
        check(panel.key(VK_ESCAPE, 19.1) && finished.size() == 7 && !finished[6], "Escape finishes the edit before the press runs");
        check(panel.beginPositionEditing(m::chargeDemoReading(), 19.2) && pumpInvokes(panel) == 1 && state.editingPosition() && finished.size() == 7,
              "The stale press does not touch the next edit");
        check(panel.key(VK_ESCAPE, 19.3) && finished.size() == 8, "Escape discards the next edit");
    }
    check(panel.residency().recoveries == 0, "No editor frame needed a device recovery");
    panel.setAppearance(appearance);
    ComPtr<IRawElementProviderFragmentRoot> before;
    check(panel.automationRoot(&before) == S_OK, "The root exists before shutdown");
    panel.shutdown();
    check(!panel.window() && panel.automationChildren() == 0 && !panel.editor(), "Shutdown releases the window, device and UI Automation");
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

// Residency: nothing exists at construction; a presentation creates the
// window and device on demand (build-time programs when built in) and keeps
// no CPU copy of the installed artwork; park() releases them only while
// nothing shows; the next presentation rebuilds them pixel-identically; a
// device lost while the window existed is replaced once by the next render.
void residency(const std::filesystem::path& shader) {
    gpu::LayerRasterizer raster;
    gpu::ChargeIndicatorPanel panel(raster, {shader, true});
    auto& state = panel.state();
    m::BatteryReading reading;
    reading.present = reading.pluggedIn = reading.charging = true;
    reading.percentage = 50;
    const std::vector<m::ChargeWorkArea> screens{{11, {0, 0, 1440, 900}, 2}};
    panel.setScreens(screens, 0);
    panel.setAppearance({});
    state.update(reading, false, 0);
    panel.advance(0);
    auto r = panel.residency();
    check(!r.window && !r.device && r.creations == 0 && r.gpuBytes == 0 && r.targetBytes == 0 && !panel.window() && !panel.park(),
          "Nothing exists before an alert; there is nothing to park");
    // The plug-in alert creates them on demand.
    state.show(false, 3, true, 2);
    for (double t = 2; t < 3.6; t += 1.0 / 60) panel.advance(t);
    r = panel.residency();
    check(state.windowVisible() && state.stage() == m::ChargeStage::compact && r.window && r.device && r.creations == 1 && r.lastCreationMilliseconds > 0,
          "The alert creates its window and device on demand");
    check(!gpu::Renderer::shadersEmbedded() || r.embeddedShaders, "The device's programs come from the build-time hud.hlsl bytecode");
    check(r.gpuBytes > 0 && r.cpuBytes < 64 * 1024 && r.targetBytes == std::size_t{600} * 168 * (4 + 8),
          "The artwork is installed on the GPU; little stays on the CPU");
    std::cout << "alert at 2x: " << r.lastCreationMilliseconds << " ms window+device, " << r.gpuBytes << " GPU resource bytes, " << r.targetBytes
              << " target bytes, " << r.cpuBytes << " CPU bytes\n";
    const auto compact = panel.readbackForTests();
    check(pixel(compact, 380, 52)[3] > 240, "The compact plate is painted at 2x");
    check(!panel.park() && panel.window(), "park refuses while the alert shows");
    for (double t = 3.6; t < 7.6; t += 1.0 / 60) panel.advance(t);
    check(!state.windowVisible() && panel.window() && !panel.windowVisible() && panel.residency().device,
          "The panel itself keeps the ordered-out window until its owner parks it");
    check(panel.park(), "park releases them while nothing shows");
    r = panel.residency();
    check(!r.window && !r.device && r.parks == 1 && r.gpuBytes == 0 && r.targetBytes == 0 && !panel.window(), "Parked: no window, device or GPU resource");
    check(!panel.park(), "Parking twice releases nothing");
    // The next alert rebuilds everything from the retained content key.
    state.show(false, 3, true, 8);
    for (double t = 8; t < 9.6; t += 1.0 / 60) panel.advance(t);
    r = panel.residency();
    check(state.windowVisible() && r.window && r.device && r.creations == 2 && panel.lastFailure().empty() && r.cpuBytes < 64 * 1024,
          "The next alert creates them again");
    check(panel.readbackForTests().pixels == compact.pixels, "The rebuilt alert renders pixel-identically");
    for (double t = 9.6; t < 13.6; t += 1.0 / 60) panel.advance(t);
    check(!state.windowVisible() && panel.window(), "The rebuilt alert is dismissed");
    // A device lost while the window existed (driver update, TDR): the next
    // render replaces it once instead of failing.
    panel.loseDeviceForTests();
    state.show(false, 3, true, 14);
    for (double t = 14; t < 15.6; t += 1.0 / 60) panel.advance(t);
    r = panel.residency();
    check(state.windowVisible() && r.device && r.recoveries == 1 && r.creations == 3 && panel.lastFailure().empty(),
          "A lost device is replaced once by the next render");
    check(panel.readbackForTests().pixels == compact.pixels, "The recovered alert renders pixel-identically");
    state.hide(false, 16);
    panel.advance(16);
    // The position editor holds the window too.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 17) && !panel.park() && panel.window(), "park refuses while the editor shows");
    check(panel.key(VK_ESCAPE, 18) && !state.editingPosition() && panel.park() && !panel.window(), "After the edit it parks");
    panel.shutdown();
    check(!panel.window() && !panel.residency().device, "Shutdown releases everything");
}

// The first frame of a presentation (content raster, window and device
// creation) postpones it by that frame's duration, so the entrance plays
// from its first frame once the window shows (Core Animation starts the
// animations of a commit when the panel is on screen). Hidden tests inject a
// fixed 45 ms; a reference panel without it shows the same frames 45 ms
// earlier. A continuing presentation and the editor are never postponed.
void firstFrame(const std::filesystem::path& shader) {
    constexpr double lag = 0.045;
    gpu::LayerRasterizer raster;
    gpu::ChargePanelOptions lagged{shader, true};
    lagged.firstFrameLagForTests = lag;
    gpu::ChargeIndicatorPanel panel(raster, lagged), reference(raster, {shader, true});
    m::BatteryReading reading;
    reading.present = reading.pluggedIn = reading.charging = true;
    reading.percentage = 71;
    const std::vector<m::ChargeWorkArea> screens{{11, {0, 0, 1440, 900}, 1}};
    for (auto* p : {&panel, &reference}) {
        p->setScreens(screens, 0);
        p->setAppearance({});
        p->state().update(reading, false, 0);
        p->state().show(false, 3, true, 2);
        p->advance(2);
    }
    auto r = panel.residency();
    check(r.postponements == 1 && std::abs(r.lastPostponedMilliseconds - 45) < 1e-9 && r.lastFirstFrameMilliseconds > 0 && reference.residency().postponements == 0,
          "The first frame of the plug-in alert postpones its presentation once");
    check(panel.nextWakeTime() && reference.nextWakeTime() && std::abs(*panel.nextWakeTime() - (*reference.nextWakeTime() + lag)) < 1e-9 &&
          std::abs(*panel.nextWakeTime() - 2.245) < 1e-9 && panel.requiresFrames(2.01),
          "The supercharge step moves with it (2 + 0.045 + 0.20 s)");
    // Identical frames, 45 ms apart: the circle grows from its first frame.
    for (const double at : {2.02, 2.1, 2.19, 2.3, 2.6, 3.4}) {
        panel.advance(at + lag);
        reference.advance(at);
        const auto a = panel.readbackForTests(), b = reference.readbackForTests();
        check(a.width == b.width && a.height == b.height && a.pixels == b.pixels,
              "The postponed entrance renders the reference frame of " + std::to_string(at) + " s at " + std::to_string(at + lag) + " s");
    }
    check(panel.state().stage() == m::ChargeStage::compact && reference.state().stage() == m::ChargeStage::compact, "Both entrances settle compact");
    panel.advance(3.6 + lag);
    reference.advance(3.6);
    const auto dismissal = panel.nextWakeTime(), referenceDismissal = reference.nextWakeTime();
    check(dismissal && referenceDismissal && std::abs(*dismissal - (*referenceDismissal + lag)) < 1e-9 && std::abs(*dismissal - 6.545) < 1e-9,
          "The dismissal follows the postponed entrance (2.045 + 1.5 + 3 s)");
    // A replayed preview while the alert shows continues its presentation.
    panel.state().show(false, 3, true, 4);
    panel.advance(4);
    check(panel.residency().postponements == 1 && std::abs(*panel.nextWakeTime() - 4.2) < 1e-9, "A replay inside a presentation is not postponed");
    for (double t = 4; t < 10; t += 1.0 / 60) panel.advance(t);
    check(!panel.state().windowVisible(), "The replay is dismissed");
    // The next presentation (window and device still there) postpones again.
    panel.state().show(false, 3, true, 11);
    panel.advance(11);
    check(panel.residency().postponements == 2 && panel.residency().creations == 1 && std::abs(*panel.nextWakeTime() - 11.245) < 1e-9,
          "Every presentation's first frame postpones it, with or without a creation");
    panel.state().hide(false, 12);
    panel.advance(12);
    // The position editor shows compact at once: nothing to postpone.
    check(panel.beginPositionEditing(m::chargeDemoReading(), 13) && panel.residency().postponements == 2 && !panel.nextWakeTime(),
          "The editor is never postponed");
    check(panel.key(VK_ESCAPE, 14) && !panel.state().editingPosition(), "The editor closes");
    // The portable state moves its deadlines without touching a settled one.
    m::ChargeOverlayState settled;
    settled.setScreens(screens, 0);
    settled.postpone(1);
    check(!settled.nextDeadline() && !settled.windowVisible(), "Postponing an idle state changes nothing");
    settled.show(false, 3, true, 20);
    settled.postpone(0);
    settled.postpone(-1);
    settled.postpone(std::numeric_limits<double>::quiet_NaN());
    settled.postpone(std::numeric_limits<double>::infinity());
    check(settled.nextDeadline() && std::abs(*settled.nextDeadline() - 20.2) < 1e-9, "Only a positive finite postponement moves a presentation");
    panel.shutdown();
    reference.shutdown();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result) && argc == 2, "Pass HLSL to the synthetic charge alert panel test");
        run(argv[1]);
        residency(argv[1]);
        firstFrame(argv[1]);
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
