#pragma once
// Test-only copy of OBS Studio's Game Capture rule for its "Capture any
// full-screen application" mode (plugins/win-capture/game-capture.c,
// get_fullscreen_window and rects_nearly_equal): the foreground window is
// taken when its GetWindowRect lies within 2 px of the rcMonitor of its
// nearest monitor on every edge, unless it is a regular maximized window
// (WS_MAXIMIZE with WS_BORDER). Visibility, cloaking and the extended styles
// are not checked. Asked of a given window instead of GetForegroundWindow(),
// so the hidden tests never change the foreground.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdlib>

namespace endfield::tests {
inline bool obsTakesAsFullscreen(HWND window) {
    RECT rect{};
    if (!window || !GetWindowRect(window, &rect)) return false;
    const auto styles = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    if ((styles & WS_MAXIMIZE) != 0 && (styles & WS_BORDER) != 0) return false;
    const auto monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return false;
    const auto within = [](LONG a, LONG b) { return std::labs(a - b) <= 2; };
    return within(rect.left, info.rcMonitor.left) && within(rect.right, info.rcMonitor.right) &&
        within(rect.top, info.rcMonitor.top) && within(rect.bottom, info.rcMonitor.bottom);
}
}
#endif
