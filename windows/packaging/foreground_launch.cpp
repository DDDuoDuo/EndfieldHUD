#include "packaging/foreground_launch.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::packaging {
static_assert(anyProcess == ASFW_ANY, "anyProcess is AllowSetForegroundWindow's ASFW_ANY");

bool grantForeground(const ForegroundGrant& grant, std::uint32_t processId) {
    if (grant) return grant(processId);
    return AllowSetForegroundWindow(static_cast<DWORD>(processId)) != FALSE;
}

std::optional<StartedProcess> startWithForeground(const std::filesystem::path& executable, std::wstring commandLine,
                                                  const std::filesystem::path& folder, std::uint32_t creationFlags,
                                                  const ForegroundGrant& grant) {
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, creationFlags | CREATE_SUSPENDED, nullptr,
                        folder.empty() ? nullptr : folder.c_str(), &startup, &info))
        return std::nullopt;
    // Before its first instruction: the program's first window can use it.
    StartedProcess started{info.dwProcessId, info.hProcess, false};
    started.foregroundGranted = grantForeground(grant, info.dwProcessId);
    if (ResumeThread(info.hThread) == static_cast<DWORD>(-1)) {
        const auto error = GetLastError();
        TerminateProcess(info.hProcess, 1);
        WaitForSingleObject(info.hProcess, 5000);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        SetLastError(error);
        return std::nullopt;
    }
    CloseHandle(info.hThread);
    return started;
}
} // namespace endfield::packaging
#endif
