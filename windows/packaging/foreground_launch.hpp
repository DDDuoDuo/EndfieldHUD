#pragma once
// Starting a program that may come to the front: EndfieldHUD from Setup
// (*Open EndfieldHUD*, --open, the restart after an update) and Setup from
// the in-app updater, which reopens EndfieldHUD.
//
// Windows' foreground lock lets a process bring its window to the front
// (SetForegroundWindow) only while it may set the foreground window: it is
// the foreground process or was started by it, it received the last input,
// or a process that may set it passed that right on (AllowSetForegroundWindow).
// EndfieldHUD started by Setup after Setup's own window had closed had none
// of these, so the HUD it opened stayed behind the active window (tester
// report on release candidate 136). Each launcher in the chain therefore
// starts the program suspended, passes its own right on to the new process
// ID, and only then lets it run: the grant is in place before the program's
// first instruction, so its first window can use it. Windows ignores the
// grant when the launcher holds no right itself, and drops it at the user's
// next input elsewhere; the HUD then still opens above the other windows (the
// topmost band) and is activated by the first click on it
// (app/overlay_host.hpp). Nothing here injects input or attaches thread input.
#ifdef _WIN32
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace endfield::packaging {
// Passes the right to take the foreground on to a process ID (production:
// AllowSetForegroundWindow); answers whether Windows accepted it. Tests
// record the calls instead.
using ForegroundGrant = std::function<bool(std::uint32_t processId)>;
// The process ID of a grant to whichever process asks next (ASFW_ANY), for a
// start whose process ID is unknown beforehand (the Restart Manager's).
inline constexpr std::uint32_t anyProcess = 0xFFFFFFFFu;
// `grant`, or AllowSetForegroundWindow when it is empty.
bool grantForeground(const ForegroundGrant& grant, std::uint32_t processId);

struct StartedProcess {
    std::uint32_t processId{};
    void* process{}; // the process handle; the caller closes it
    bool foregroundGranted{};
};
// CreateProcessW(executable, commandLine) - no shell, no inherited handles,
// `creationFlags` plus CREATE_SUSPENDED, `folder` as its working folder -
// then the foreground grant to its process ID while it is still suspended,
// then ResumeThread. A refused grant never stops the start. nullopt when it
// could not start (GetLastError() tells why); nothing is left running then.
std::optional<StartedProcess> startWithForeground(const std::filesystem::path& executable, std::wstring commandLine,
                                                  const std::filesystem::path& folder, std::uint32_t creationFlags = 0,
                                                  const ForegroundGrant& grant = {});
} // namespace endfield::packaging
#endif
