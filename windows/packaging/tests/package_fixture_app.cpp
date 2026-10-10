// Stand-in EndfieldHUD.exe for the packaging tests (never the real app).
// A hidden top-level window answers the session-end protocol the Restart
// Manager uses, exactly like the HUD window does:
//
//   --marker <file>        append "started <pid>" / "ended <pid>" lines
//   --register-restart     RegisterApplicationRestart("--login --marker <file>")
//   --refuse-shutdown      answer FALSE to WM_QUERYENDSESSION and keep running
//   --perf <name>          performance-gate fixture: a primary instance named
//                          <name> stays idle; a second launch with the same name
//                          forwards --toggle and exits (like a relaunch of
//                          EndfieldHUD forwards to the running copy)
//   --leak                 each toggle to "open" leaks one event handle and one
//                          GDI bitmap (the gate must report growth)
//   --busy                 wakes every 10 ms while open (the gate must report it)
//   --toggle               see --perf
//   --login                accepted and ignored (restart command line)
//
// Every instance ends by itself after five minutes, so a failed test can
// never leave it running.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace {
constexpr UINT toggleMessage = WM_APP + 1;
constexpr UINT_PTR lifetimeTimer = 1, busyTimer = 2;
std::wstring marker;
bool refuse{}, leak{}, busy{}, open{};
std::vector<HANDLE> leaked;
std::vector<HGDIOBJ> leakedObjects;
std::vector<HANDLE> openHandles;
std::vector<HGDIOBJ> openObjects;

void note(const std::wstring& text) {
    if (marker.empty()) return;
    HANDLE h = CreateFileW(marker.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const auto line = text + L" " + std::to_wstring(GetCurrentProcessId()) + L"\r\n";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), bytes, nullptr, nullptr);
    DWORD written{};
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
}
void toggle(HWND window) {
    open = !open;
    if (open) {
        // "Opening" allocates like a HUD frame would; closing releases it.
        for (int n = 0; n < 4; ++n) openHandles.push_back(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        HDC screen = GetDC(nullptr);
        for (int n = 0; n < 2; ++n) openObjects.push_back(CreateCompatibleBitmap(screen, 16, 16));
        ReleaseDC(nullptr, screen);
        if (leak) {
            leaked.push_back(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            HDC dc = GetDC(nullptr);
            leakedObjects.push_back(CreateCompatibleBitmap(dc, 8, 8));
            ReleaseDC(nullptr, dc);
        }
        if (busy) SetTimer(window, busyTimer, 10, nullptr);
    } else {
        for (auto h : openHandles) CloseHandle(h);
        for (auto o : openObjects) DeleteObject(o);
        openHandles.clear();
        openObjects.clear();
        KillTimer(window, busyTimer);
    }
    note(open ? L"open" : L"closed");
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_QUERYENDSESSION:
        note(L"query");
        return refuse ? FALSE : TRUE;
    case WM_ENDSESSION:
        if (wParam && !refuse) { note(L"ended"); PostQuitMessage(0); }
        return 0;
    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data && data->dwData == toggleMessage) toggle(window);
        return TRUE;
    }
    case WM_TIMER:
        if (wParam == lifetimeTimer) { note(L"expired"); PostQuitMessage(0); }
        return 0;
    case WM_CLOSE:
        note(L"closed-window");
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int count{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    bool registerRestart{}, forwardToggle{};
    std::wstring perf;
    for (int n = 1; argv && n < count; ++n) {
        const std::wstring a = argv[n];
        if (a == L"--marker" && n + 1 < count) marker = argv[++n];
        else if (a == L"--register-restart") registerRestart = true;
        else if (a == L"--refuse-shutdown") refuse = true;
        else if (a == L"--perf" && n + 1 < count) perf = argv[++n];
        else if (a == L"--leak") leak = true;
        else if (a == L"--busy") busy = true;
        else if (a == L"--toggle") forwardToggle = true;
    }
    if (argv) LocalFree(argv);
    const std::wstring className = L"EndfieldHUDPackagingFixture." + (perf.empty() ? std::to_wstring(GetCurrentProcessId()) : perf);
    HANDLE single{};
    if (!perf.empty()) {
        single = CreateMutexW(nullptr, TRUE, (L"Local\\EndfieldHUDPackagingFixture." + perf).c_str());
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Secondary launch: forward and exit, like EndfieldHUD's relaunch.
            for (int attempt = 0; attempt < 50; ++attempt) {
                if (HWND primary = FindWindowW(className.c_str(), nullptr)) {
                    if (forwardToggle) {
                        COPYDATASTRUCT data{toggleMessage, 0, nullptr};
                        SendMessageW(primary, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
                    }
                    if (single) CloseHandle(single);
                    return 0;
                }
                Sleep(100);
            }
            return 3;
        }
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = procedure;
    wc.hInstance = instance;
    wc.lpszClassName = className.c_str();
    if (!RegisterClassExW(&wc)) return 2;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, className.c_str(), L"EndfieldHUD packaging fixture", WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    SetTimer(window, lifetimeTimer, 5 * 60 * 1000, nullptr);
    if (registerRestart) {
        const auto command = L"--login --marker \"" + marker + L"\"";
        RegisterApplicationRestart(command.c_str(), RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT);
    }
    note(L"started");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyWindow(window);
    for (auto h : leaked) CloseHandle(h);
    for (auto o : leakedObjects) DeleteObject(o);
    if (single) CloseHandle(single);
    return 0;
}
