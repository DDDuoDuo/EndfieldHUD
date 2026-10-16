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
//   --trace <file>         with ENDFIELDHUD_STARTUP_TRACE=1 in the environment
//                          (as EndfieldHUD.exe reads it), write a synthetic
//                          startup trace there once two openings happened, or
//                          at exit with what was recorded
//   --slow-open            the traced cold opening has a slow first frame and
//                          five late frames (the gate must report it)
//   --fatal-record <file>  where EndfieldHUD's fatal record would be
//   --fail-start           write a fatal record there at startup and stay idle,
//                          like EndfieldHUD behind its fatal-error message box
//   --threads <n>          start n idle threads described "fixture worker" and
//                          keep one Windows thread-pool worker waiting (the
//                          footprint census must group and attribute them)
//   --hold-mib <n>         while open, hold n MiB of committed, touched private
//                          memory in one allocation; closing releases it
//   --device <n>           a stand-in graphics device: a thread described
//                          "fixture graphics device" that wakes about n times
//                          a second (the graphics driver's thread of a live
//                          D3D11 device). Like EndfieldHUD it exists from the
//                          start, stays while open and for the closed graphics
//                          grace after launch and after every close, and is
//                          released afterwards; the grace is ten minutes, or
//                          ENDFIELDHUD_CLOSED_GRAPHICS_GRACE seconds from the
//                          environment (EndfieldHUD honours that only with
//                          --data-root; the stand-in has no data root)
//   --keep-device          never release the device (the gate must report it)
//   --login                accepted and ignored (restart command line)
//
// Every instance ends by itself after five minutes, so a failed test can
// never leave it running.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr UINT toggleMessage = WM_APP + 1;
constexpr UINT_PTR lifetimeTimer = 1, busyTimer = 2, graceTimer = 3;
std::wstring marker;
bool refuse{}, leak{}, busy{}, open{}, slowOpen{};
std::wstring traceFile, fatalFile;
bool failStart{};
unsigned holdMiB{};
void* held{};
unsigned deviceWakes{};
bool keepDevice{};
UINT graceMilliseconds{600 * 1000};
HANDLE deviceStop{}, deviceThread{};
HANDLE quitWorkers{};
std::vector<HANDLE> workers;
unsigned tracedOpenings{};
bool traceWritten{};
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
// A synthetic startup trace in EndfieldHUD's format (app/application.cpp
// StartupTrace::report, schema 1): fixed numbers, written atomically.
std::string openingJson(unsigned index) {
    const bool slow = index == 0 && slowOpen;
    std::string intervals = slow ? "412.5" : "12.5", cpu = slow ? "40.0" : "2.0";
    for (int n = 1; n < 36; ++n) {
        const bool late = slow && n % 7 == 0;
        intervals += late ? ",48.0" : ",16.7";
        cpu += late ? ",21.0" : ",2.5";
    }
    return std::string("{\"index\":") + std::to_string(index) + ",\"cold\":" + (index == 0 ? "true" : "false") +
           ",\"requestedAtMilliseconds\":" + (index == 0 ? "3000" : "9000") + ",\"requestToFirstFrameMilliseconds\":" + (slow ? "412.5" : "12.5") +
           ",\"requestToOpenedMilliseconds\":620,\"frames\":36,\"frameIntervalsMilliseconds\":[" + intervals + "],\"frameCPUMilliseconds\":[" + cpu + "]}";
}
void writeTrace() {
    if (traceFile.empty() || traceWritten) return;
    wchar_t flag[8]{};
    if (GetEnvironmentVariableW(L"ENDFIELDHUD_STARTUP_TRACE", flag, 8) != 1 || flag[0] != L'1') return;
    traceWritten = true;
    std::string openings;
    for (unsigned n = 0; n < tracedOpenings && n < 2; ++n) openings += (n ? "," : "") + openingJson(n);
    const std::string text = "{\"schema\":1,\"numericOnly\":true,\"preparationMilliseconds\":35.5,\"launchToFirstFrameMilliseconds\":180.25,"
                             "\"processToFirstFrameMilliseconds\":210.75,\"startupStages\":[{\"stage\":\"renderer\",\"wallMilliseconds\":61.5,"
                             "\"processCPUSeconds\":0.05},{\"stage\":\"owners\",\"wallMilliseconds\":24.0,\"processCPUSeconds\":0.02}],\"prewarm\":[],"
                             "\"openings\":[" + openings + "],\"openingStages\":[{\"phase\":0,\"slowestOwners\":[{\"owner\":\"fixture\","
                             "\"updateTotalMS\":4.5,\"updateMaxMS\":1.5,\"uploadTotalMS\":2.0,\"uploadMaxMS\":1.0}]}]}";
    const auto staging = traceFile + L".partial";
    HANDLE h = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written{};
    const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(h);
    if (!ok || !MoveFileExW(staging.c_str(), traceFile.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(staging.c_str());
}
// The stand-in graphics device: one thread that wakes deviceWakes times a
// second for as long as the device exists.
void createDevice() {
    if (!deviceWakes || deviceThread) return;
    deviceStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    deviceThread = CreateThread(nullptr, 0, [](void*) -> DWORD {
        const DWORD period = std::max<DWORD>(1, 1000 / deviceWakes);
        while (WaitForSingleObject(deviceStop, period) == WAIT_TIMEOUT) {}
        return 0;
    }, nullptr, 0, nullptr);
    if (deviceThread) SetThreadDescription(deviceThread, L"fixture graphics device");
}
void releaseDevice() {
    if (!deviceThread) return;
    SetEvent(deviceStop);
    WaitForSingleObject(deviceThread, 5000);
    CloseHandle(deviceThread);
    CloseHandle(deviceStop);
    deviceThread = deviceStop = nullptr;
}
// Closed (after launch or a close): the device goes after the grace.
void armGrace(HWND window) {
    if (deviceWakes && !keepDevice) SetTimer(window, graceTimer, graceMilliseconds, nullptr);
}
void toggle(HWND window) {
    open = !open;
    if (open && tracedOpenings < 2 && ++tracedOpenings == 2) writeTrace();
    if (open) {
        KillTimer(window, graceTimer);
        createDevice();
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
        if (holdMiB && !held) {
            const SIZE_T bytes = static_cast<SIZE_T>(holdMiB) * 1024 * 1024;
            held = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (held) for (SIZE_T at = 0; at < bytes; at += 4096) static_cast<volatile char*>(held)[at] = 1; // resident, not only committed
        }
    } else {
        if (held) VirtualFree(held, 0, MEM_RELEASE);
        held = nullptr;
        for (auto h : openHandles) CloseHandle(h);
        for (auto o : openObjects) DeleteObject(o);
        openHandles.clear();
        openObjects.clear();
        KillTimer(window, busyTimer);
        armGrace(window);
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
        if (wParam == graceTimer) {
            KillTimer(window, graceTimer);
            if (!open) releaseDevice();
        }
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
    unsigned workerCount{};
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
        else if (a == L"--trace" && n + 1 < count) traceFile = argv[++n];
        else if (a == L"--slow-open") slowOpen = true;
        else if (a == L"--fatal-record" && n + 1 < count) fatalFile = argv[++n];
        else if (a == L"--fail-start") failStart = true;
        else if (a == L"--threads" && n + 1 < count) workerCount = static_cast<unsigned>(_wtoi(argv[++n]));
        else if (a == L"--hold-mib" && n + 1 < count) holdMiB = static_cast<unsigned>(_wtoi(argv[++n]));
        else if (a == L"--device" && n + 1 < count) deviceWakes = static_cast<unsigned>(_wtoi(argv[++n]));
        else if (a == L"--keep-device") keepDevice = true;
    }
    if (wchar_t grace[32]{}; GetEnvironmentVariableW(L"ENDFIELDHUD_CLOSED_GRAPHICS_GRACE", grace, 32) && grace[0]) {
        const double seconds = _wtof(grace);
        if (seconds >= 0.05 && seconds <= 3600) graceMilliseconds = static_cast<UINT>(seconds * 1000);
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
    // Like EndfieldHUD's launch prewarm: the device exists from the start and
    // the closed HUD's grace runs from there.
    createDevice();
    armGrace(window);
    if (workerCount) {
        // Idle threads with one start routine and description, and one
        // thread-pool worker kept waiting: none of them wakes.
        quitWorkers = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        for (unsigned n = 0; n < workerCount && n < 64; ++n) {
            HANDLE thread = CreateThread(nullptr, 0, [](void*) -> DWORD { WaitForSingleObject(quitWorkers, INFINITE); return 0; }, nullptr, 0, nullptr);
            if (!thread) break;
            SetThreadDescription(thread, L"fixture worker");
            workers.push_back(thread);
        }
        TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE instance, void*) {
            CallbackMayRunLong(instance);
            WaitForSingleObject(quitWorkers, INFINITE);
        }, nullptr, nullptr);
    }
    if (failStart && !fatalFile.empty()) {
        // The shape of app/main.cpp's record (redacted, no paths).
        const std::string record = "EndfieldHUD fatal error\nexception: class std::runtime_error\nmessage: Cannot locate the current display\n"
                                   "stage: launch > application start\n";
        HANDLE h = CreateFileW(fatalFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written{};
            WriteFile(h, record.data(), static_cast<DWORD>(record.size()), &written, nullptr);
            CloseHandle(h);
        }
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    writeTrace(); // at exit with what was recorded, like EndfieldHUD
    if (quitWorkers) {
        SetEvent(quitWorkers);
        if (!workers.empty()) WaitForMultipleObjects(static_cast<DWORD>(workers.size()), workers.data(), TRUE, 5000);
        for (auto h : workers) CloseHandle(h);
    }
    if (held) VirtualFree(held, 0, MEM_RELEASE);
    releaseDevice();
    DestroyWindow(window);
    for (auto h : leaked) CloseHandle(h);
    for (auto o : leakedObjects) DeleteObject(o);
    if (single) CloseHandle(single);
    return 0;
}
