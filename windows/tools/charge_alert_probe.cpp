// User-run, read-only diagnostics for the floating charge alert. The default
// invocation is inert (the registered test checks only that).
//   --screens         list monitor work areas, DPI, identity and the preferred display
//   --layered-target  create the alert's hidden click-through layered window and
//                     bind a composition renderer to it; the window is never shown
#ifdef _WIN32
#include "native/charge_indicator_panel.hpp"
#include "native/layer_scene.hpp"
#include <iostream>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace endfield;

int wmain(int argc, wchar_t** argv) {
    const std::wstring_view mode = argc == 3 || argc == 2 ? argv[1] : L"";
    if (mode == L"--screens") {
        const auto scan = native::scanChargeScreens();
        for (std::size_t n = 0; n < scan.screens.size(); ++n) {
            const auto& s = scan.screens[n];
            std::wcout << L"screen " << n << L" id=" << s.id << L" work=" << s.bounds.x << L',' << s.bounds.y << L' ' << s.bounds.width
                       << L'x' << s.bounds.height << L" pixelsPerPoint=" << s.pixelsPerPoint << (scan.preferred == n ? L" preferred" : L"")
                       << L" identity=" << scan.identities[n] << L'\n';
        }
        return scan.screens.empty() ? 1 : 0;
    }
    if (mode == L"--layered-target" && argc == 3) {
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"EndfieldChargeAlertProbe";
        RegisterClassW(&c);
        for (const bool layered : {true, false}) {
            HWND window = CreateWindowExW(native::chargeAlertWindowStyle(layered), c.lpszClassName, L"EndfieldHUD probe", WS_POPUP, 0, 0, 300, 84,
                                          nullptr, nullptr, c.hInstance, nullptr);
            if (!window) { std::cout << "layered=" << layered << " create failed " << GetLastError() << '\n'; continue; }
            if (layered) SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
            for (const auto driver : {native::Driver::hardware, native::Driver::warpForTests}) {
                native::Renderer renderer;
                try {
                    renderer.initialize(window, 300, 84, {driver, argv[2], native::RenderTarget::composition});
                    renderer.setCamera(native::layerViewportProjection(300, 84));
                    renderer.draw(true);
                    std::cout << "layered=" << layered << " driver=" << (driver == native::Driver::hardware ? "hardware" : "warp")
                              << " composition target: ok, visible=" << IsWindowVisible(window) << '\n';
                } catch (const std::exception& e) {
                    std::cout << "layered=" << layered << " driver=" << (driver == native::Driver::hardware ? "hardware" : "warp")
                              << " composition target: " << e.what() << '\n';
                }
                renderer.reset();
            }
            DestroyWindow(window);
        }
        UnregisterClassW(c.lpszClassName, c.hInstance);
        return 0;
    }
    std::cout << "charge_alert_probe: inert. Pass --screens, or --layered-target <hud.hlsl> (hidden window).\n";
    return 0;
}
#else
#include <iostream>
int main() { std::cout << "charge_alert_probe: inert (Windows only)\n"; return 0; }
#endif
