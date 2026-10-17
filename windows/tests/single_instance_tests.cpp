// Single-instance guard and relaunch forwarding. Every name contains a fresh
// GUID, so a running EndfieldHUD (if any) is never contacted or blocked. The
// cross-process case launches only this test executable as the second copy.
#ifdef _WIN32
#include "app/single_instance.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace app = endfield::app;
namespace {
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
std::wstring guid() {
    GUID value{};
    check(SUCCEEDED(CoCreateGuid(&value)), "Fresh test GUID");
    wchar_t text[64]{};
    check(StringFromGUID2(value, text, 64) > 0, "Format test GUID");
    return text;
}
app::SingleInstanceNames names(const std::wstring& id) { return {L"Local\\EndfieldHUD.Test.Instance." + id, L"EndfieldHUD.Test.Activation." + id}; }
void pump() { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }

int secondary(const std::wstring& id) {
    app::SingleInstanceGuard guard(names(id));
    if (guard.primary()) return 3;
    const std::array<std::string_view, 2> arguments{"--settings", "云终末地"};
    return guard.forward(arguments, 10000) ? 0 : 4;
}

void inProcess() {
    const auto id = guid();
    std::vector<std::vector<std::string>> delivered;
    {
        app::SingleInstanceGuard first(names(id));
        check(first.primary(), "First guard owns the session name");
        app::SingleInstanceGuard second(names(id));
        check(!second.primary(), "Second guard detects the running instance");
        bool threw{};
        try { first.forward({}, 10); } catch (const std::logic_error&) { threw = true; }
        check(threw, "The primary never forwards to itself");
        const auto started = GetTickCount64();
        check(!second.forward({}, 200) && GetTickCount64() - started < 2000, "No listener: forwarding fails within its bound");
        first.listen();
        const std::array<std::string_view, 1> plain{"--login"};
        check(second.forward(plain, 2000) && first.received() == 1, "Activation before the handler is queued");
        first.setHandler([&](std::vector<std::string> value) { delivered.push_back(std::move(value)); });
        check(delivered.size() == 1 && delivered[0] == std::vector<std::string>{"--login"}, "Queued activation is delivered once the handler exists");
        const std::array<std::string_view, 0> none{};
        check(second.forward(none, 2000), "Plain relaunch forwards");
        pump();
        check(delivered.size() == 2 && delivered[1].empty(), "Relaunch is delivered from the owner message loop");
        // Malformed payloads are rejected without delivery.
        const auto window = FindWindowExW(HWND_MESSAGE, nullptr, names(id).windowClass.c_str(), nullptr);
        check(window != nullptr, "Activation window is message-only");
        char bytes[] = "EHUD-ACTIVATE-1";
        COPYDATASTRUCT wrong{0x12345678, sizeof(bytes), bytes};
        check(SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&wrong)) == FALSE, "Foreign COPYDATA is rejected");
        COPYDATASTRUCT truncated{0x45485544, sizeof(bytes) - 1, bytes};
        check(SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&truncated)) == FALSE, "Malformed payload is rejected");
        std::string huge(70000, 'a');
        COPYDATASTRUCT oversized{0x45485544, static_cast<DWORD>(huge.size()), huge.data()};
        check(SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&oversized)) == FALSE, "Oversized payload is rejected");
        pump();
        check(delivered.size() == 2 && first.received() == 2, "Rejected payloads never reach the owner");
        // Cross-process second launch of this executable.
        wchar_t self[MAX_PATH]{};
        check(GetModuleFileNameW(nullptr, self, MAX_PATH) > 0, "Locate the test executable");
        std::wstring command = L"\"" + std::wstring(self) + L"\" --secondary " + id;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        check(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "Launch the second copy");
        const auto deadline = GetTickCount64() + 20000;
        DWORD wait{};
        while ((wait = MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, 100, QS_ALLINPUT)) != WAIT_OBJECT_0) {
            pump();
            if (GetTickCount64() > deadline) { TerminateProcess(process.hProcess, 98); break; }
        }
        DWORD code{};
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        pump();
        check(code == 0, "The second process delivers its arguments and exits");
        check(delivered.size() == 3 && delivered[2] == std::vector<std::string>{"--settings", "云终末地"}, "Cross-process UTF-8 arguments arrive intact");
    }
    app::SingleInstanceGuard after(names(id));
    check(after.primary(), "The name is released when the primary exits");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--secondary") return secondary(argv[2]);
        inProcess();
        std::cout << "Single instance: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Single instance failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
