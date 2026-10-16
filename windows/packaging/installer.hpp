#pragma once
// Per-user, unpackaged EndfieldHUD install engine (no MSIX, no elevation).
//
// Production layout (InstallLayout::production):
//   %LOCALAPPDATA%\Programs\EndfieldHUD\     EndfieldHUD.exe, THIRD-PARTY-NOTICES.txt,
//                                           fonts\, resources\, uninstall.exe,
//                                           EndfieldHUD.install.json (the files to remove)
//   Start menu Programs\EndfieldHUD.lnk      AppUserModelID + toast activator CLSID
//   HKCU\Software\Classes\CLSID\{activator}\LocalServer32
//   HKCU\...\CurrentVersion\Uninstall\EndfieldHUD
//   HKCU\...\CurrentVersion\Run  "EndfieldHUD" = "<exe>" --login (per the app's
//                                launchAtLogin preference; app/application_login_item)
//   %LOCALAPPDATA%\EndfieldHUD\              user data: never touched, except the
//                                           launchAtLogin choice made in Setup and
//                                           an explicitly confirmed data removal
//
// Install/upgrade is a transaction: the payload is extracted and verified
// beside the install folder; a running EndfieldHUD.exe from that folder is
// closed through the Restart Manager (WM_QUERYENDSESSION/WM_ENDSESSION, so the
// app saves first; nothing else is stopped); the folders are swapped by rename;
// registrations are written with snapshots. Any failure restores the previous
// folder and every registry value, then restarts the app it closed. An
// interrupted swap is repaired by the next Setup or uninstall run.
#ifdef _WIN32
#include "core/localization.hpp"
#include "packaging/package_format.hpp"
#include "packaging/package_io.hpp"
#include "packaging/setup_arguments.hpp"
#include "packaging/setup_text.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::packaging {
// The notification identity the Calendar reminders area looks up read-only
// (native/calendar_notification_identity.hpp); cmake/modules/packaging.cmake
// checks that both definitions agree whenever that library is built.
inline constexpr std::string_view installedAppUserModelID = "io.github.endfieldcharge.EndfieldCharge";
inline constexpr std::string_view installedActivatorCLSID = "{FF18639C-ADE8-4BA7-AC09-7F1216F9E200}";
inline constexpr std::wstring_view installedShortcutName = L"EndfieldHUD.lnk";
inline constexpr std::wstring_view registrationName = L"EndfieldHUD";

struct InstallLayout {
    std::filesystem::path installRoot, programsFolder, dataBase, workFolder;
    std::wstring registryRoot; // empty: production HKCU keys; else a private HKCU test subtree
    bool isolated() const noexcept { return !registryRoot.empty(); }
    std::wstring uninstallKey() const;
    std::wstring runKey() const;
    std::wstring approvedKey() const;
    std::wstring classesKey() const;
    std::filesystem::path executable() const { return installRoot / applicationExecutable; }
    std::filesystem::path uninstaller() const { return installRoot / uninstallerExecutable; }
    std::filesystem::path manifest() const { return installRoot / installManifestName; }
    std::filesystem::path shortcut() const { return programsFolder / installedShortcutName; }
    std::wstring loginCommand() const; // identical to app::LoginItemLocation::production
    static InstallLayout production();
    // Isolated when --test-registry is given (all paths then come from the
    // arguments); production otherwise.
    static InstallLayout fromArguments(const SetupArguments&);
};

struct RunningApplication {
    std::uint32_t processId{};
    std::uint64_t startTime{};
    std::wstring name;
    bool restartable{};
};

struct OperationCallbacks {
    std::function<void(double fraction, SetupText step)> progress;
    // Called before anything changes when EndfieldHUD runs from the install
    // folder; false cancels.
    std::function<bool(std::span<const RunningApplication>)> confirmStop;
};

// Scheduled Calendar toasts of the installed identity (removed before the
// identity on uninstall). Production uses the WinRT notifier; isolated runs
// pass a fixture and never reach the real notification platform.
class ScheduledNotifications {
public:
    virtual ~ScheduledNotifications() = default;
    virtual void clear() = 0;
};
std::unique_ptr<ScheduledNotifications> installedScheduledNotifications();

enum class Outcome : std::uint8_t { installed, upgraded, repaired, uninstalled, refused, failed, cancelled };
enum class Refusal : std::uint8_t { none, newerInstalled, foreignFolder, incomplete, path, notInstalled, closeFailed, unsupportedWindows };
std::string_view outcomeName(Outcome) noexcept;
std::string_view refusalName(Refusal) noexcept;

struct OperationResult {
    Outcome outcome{Outcome::failed};
    Refusal refusal{Refusal::none};
    std::optional<ProductVersion> previous, current;
    std::filesystem::path installRoot;
    std::vector<std::string> warnings;
    std::vector<std::string> keptFiles;   // install-folder entries Setup did not create (never deleted)
    std::vector<std::string> failedFiles; // installed files that could not be removed
    std::string error;
    std::size_t stoppedApplications{};
    bool restarted{}, dataRemoved{}, launchAtLogin{};
    bool succeeded() const noexcept {
        return outcome == Outcome::installed || outcome == Outcome::upgraded || outcome == Outcome::repaired || outcome == Outcome::uninstalled;
    }
};
std::string encodeOperationResult(const OperationResult&);

// What Setup will do, decided before any change (and shown to the user).
// Repairs an interrupted swap first (warnings name what was restored).
struct InstallPlan {
    InstallLayout layout;                     // the root of an existing registration wins
    std::optional<InstallManifest> installed; // EndfieldHUD.install.json in that root
    Outcome intended{Outcome::installed};     // installed, upgraded, repaired or refused
    Refusal refusal{Refusal::none};
    bool launchAtLogin{true};                 // the app's saved preference (Mac default: on)
    std::uint32_t windowsBuild{};             // the build the plan was made for
    std::vector<std::string> warnings;
};
// Below Windows 11 (supportedWindowsBuild) the plan is refused before
// anything is read or repaired.
InstallPlan planInstall(const PayloadManifest&, InstallLayout, bool allowDowngrade, std::uint32_t windowsBuild);
// The running Windows build (RtlGetVersion, never shimmed by compatibility modes).
std::uint32_t currentWindowsBuild() noexcept;

struct InstallOptions {
    std::optional<bool> launchAtLogin;      // explicit choice: also saved to the app's setting
    std::optional<InstallStep> failAfter;   // isolated layouts only (rollback tests)
    core::Language language{core::Language::english};
};
OperationResult install(const SetupPayload&, const InstallPlan&, const InstallOptions&, const OperationCallbacks&);

struct UninstallOptions {
    bool removeUserData{};                 // explicit consent only
    std::uint32_t parentProcess{};         // the waiting uninstall.exe (never stopped)
    std::filesystem::path relocatedImage;  // that process's image inside the install folder
    std::filesystem::path relocationFolder;// where it is moved (same volume), since a running image cannot be deleted
    core::Language language{core::Language::english};
};
OperationResult uninstall(const InstallLayout&, const UninstallOptions&, const OperationCallbacks&, ScheduledNotifications*);

std::optional<InstallManifest> readInstallManifest(const std::filesystem::path& root);
// EndfieldHUD.exe processes started from the install folder (found through
// the Restart Manager by the files they use); `excludedProcess` is skipped.
std::vector<RunningApplication> runningApplications(const InstallLayout&, const std::optional<InstallManifest>&, std::uint32_t excludedProcess = 0);
// Starts the installed EndfieldHUD.exe (optionally with --login).
bool launchInstalledApplication(const InstallLayout&, bool login);
// Stale relocated uninstall workers (EndfieldHUD-Uninstall-*) in the work
// folder; folders still in use stay.
void removeStaleUninstallWorkers(const std::filesystem::path& workFolder);
bool launchAtLoginPreference(const InstallLayout&);
// The app's saved language when set, otherwise the Windows display language.
core::Language setupLanguage(const InstallLayout&);
void recoverInterruptedInstall(const InstallLayout&, std::vector<std::string>& warnings);
// Removes a tree without following links or junctions; returns what remained.
std::vector<std::filesystem::path> removeTree(const std::filesystem::path&);
bool samePath(const std::filesystem::path&, const std::filesystem::path&) noexcept;
std::wstring widen(std::string_view);
std::string narrow(std::wstring_view);
} // namespace endfield::packaging
#endif
