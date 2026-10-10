#pragma once
// Command line of EndfieldHUD-Setup.exe and uninstall.exe (portable).
//
//   EndfieldHUD-Setup.exe [--quiet] [--launch-at-login on|off] [--open]
//                         [--allow-downgrade] [--verify-payload] [--result <file>]
//   uninstall.exe         [--quiet] [--remove-user-data] [--result <file>]
//
// --quiet (also /quiet, /S) shows no window. Without --launch-at-login the
// app's saved preference is kept. --remove-user-data is the explicit consent
// to delete the EndfieldHUD data root; nothing else ever touches user data.
//
// Isolation (tests): --test-registry Software\EndfieldHUD-Test-<id> moves every
// registry value (uninstall entry, Run, StartupApproved, Classes) below that
// private HKCU subtree and REQUIRES --install-root, --programs-folder and
// --data-base (temporary folders). Only then are --work-folder (the
// uninstaller's relocated worker) and --fail-after <step> (rollback
// injection) accepted. Isolated runs never stop or restart processes outside
// the temporary install root and never touch scheduled notifications.
//
// Internal: uninstall.exe relaunches a copy of itself outside the install
// folder with --worker --parent <pid> --install-root <folder>.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::packaging {
enum class SetupRole : std::uint8_t { install, uninstall };
enum class InstallStep : std::uint8_t { extract, stop, swap, uninstallEntry, identity, loginItem, settings, commit, count };
std::string_view installStepName(InstallStep) noexcept;
std::optional<InstallStep> installStepFromName(std::string_view) noexcept;

struct SetupArguments {
    SetupRole role{SetupRole::install};
    bool quiet{}, verifyPayload{}, open{}, allowDowngrade{}, removeUserData{}, worker{};
    std::optional<bool> launchAtLogin;
    std::string installRoot, programsFolder, dataBase, workFolder, resultFile, testRegistry; // UTF-8; empty: unset
    std::optional<InstallStep> failAfter;
    std::optional<std::uint32_t> parentProcess;
    bool isolated() const noexcept { return !testRegistry.empty(); }
};
// Throws std::invalid_argument naming the problem.
SetupArguments parseSetupArguments(SetupRole, std::span<const std::string_view>);
// Arguments for the relocated uninstall worker (everything the user gave,
// plus --worker --parent <pid> --install-root <root>).
std::vector<std::string> uninstallWorkerArguments(const SetupArguments&, std::uint32_t parent, std::string_view installRoot);
bool validTestRegistryRoot(std::string_view) noexcept;
bool absoluteWindowsPath(std::string_view) noexcept;
std::string_view setupUsage(SetupRole) noexcept;

// Exit codes of both programs.
enum class SetupExit : int { success = 0, failed = 1, usage = 2, refused = 3, cancelled = 4, busy = 5, damaged = 6, incomplete = 7 };
} // namespace endfield::packaging
