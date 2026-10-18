// Stand-in EndfieldHUD-Setup.exe for the updater hand-off tests (never the real
// installer; it installs nothing). It accepts exactly the updater's command
// line, `--quiet --open --result <absolute file>`, then writes the outcome
// named by `fixture-outcome.txt` beside its own image ("<exit code>\n<result
// JSON>") to the result file and exits with that code. Any other command line
// exits with 2 (usage), like Setup. Packed with a synthetic payload it is also
// the stub of a verifiable synthetic setup. A WIN32 program: no window.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>

namespace {
std::string readAll(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string out;
    char buffer[4096];
    DWORD read{};
    while (ReadFile(h, buffer, sizeof buffer, &read, nullptr) && read) out.append(buffer, read);
    CloseHandle(h);
    return out;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) return 2;
    const bool usage = count != 5 || std::wstring(argv[1]) != L"--quiet" || std::wstring(argv[2]) != L"--open" || std::wstring(argv[3]) != L"--result";
    const std::wstring result = count == 5 ? argv[4] : L"";
    LocalFree(argv);
    if (usage || result.size() < 3 || result[1] != L':' || result[2] != L'\\') return 2;
    wchar_t self[MAX_PATH * 4]{};
    const auto n = GetModuleFileNameW(nullptr, self, MAX_PATH * 4);
    if (!n) return 1;
    std::wstring folder(self, n);
    folder.resize(folder.find_last_of(L'\\') + 1);
    const auto outcome = readAll(folder + L"fixture-outcome.txt");
    const auto newline = outcome.find('\n');
    if (newline == std::string::npos) return 1;
    const int code = std::stoi(outcome.substr(0, newline));
    const auto json = outcome.substr(newline + 1);
    HANDLE h = CreateFileW(result.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 1;
    DWORD written{};
    WriteFile(h, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
    CloseHandle(h);
    return code;
}
