// Hidden WARP fixture for the floating charge alert panel: injected screens,
// synthetic reading, offscreen target; the window is never shown and no
// setting is written (the position callback is captured in memory).
#ifdef _WIN32
#include "native/charge_indicator_panel.hpp"
#include <objbase.h>
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
    check(panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::down, {720, 30}, 11), "Drag begins on the indicator");
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::move, {820, 230}, 11);
    panel.pointer(gpu::ChargeIndicatorPanel::PointerKind::up, {820, 230}, 11);
    check(panel.windowRect().x == 670 && panel.windowRect().y == 188, "Window follows the dragged anchor");
    check(panel.key(VK_RETURN, 12) && finished.size() == 1 && finished[0] && finished[0]->screenID == 11u &&
          std::abs(finished[0]->x - 820.0 / 1440) < 1e-12 && std::abs(finished[0]->y - 670.0 / 900) < 1e-12,
          "Return confirms the normalized position");
    check(!state.editingPosition() && !state.windowVisible(), "Confirm closes the editor");
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
    panel.shutdown();
    check(!panel.window(), "Shutdown releases the window and device");
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
