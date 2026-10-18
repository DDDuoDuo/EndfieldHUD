#pragma once
// Toast identity of an unpackaged, per-user EndfieldHUD install. Calendar
// reminders are the app's only Windows notifications (Mac
// HUDCalendarNativeNotifications through UNUserNotificationCenter). An
// unpackaged desktop app can show and schedule toasts only under an
// AppUserModelID that a Start-menu shortcut declares, and a click reaches the
// running app only through the COM activator that shortcut names
// (native/app_notification_identity.hpp). The INSTALLER creates both; the app
// itself never writes the Start menu or HKCU\Software\Classes:
//
//   install   tools/calendar_notification_setup install <EndfieldHUD.exe>
//             -> %APPDATA%\Microsoft\Windows\Start Menu\Programs\EndfieldHUD.lnk
//                (System.AppUserModel.ID + ToastActivatorCLSID, no arguments)
//             -> HKCU\Software\Classes\CLSID\{activator}\LocalServer32 =
//                "<EndfieldHUD.exe>" --notification-activation
//   runtime   the calendarReminders area (app/areas/calendar_reminder_module)
//             finds that registration read-only, establishes the process
//             AppUserModelID, registers the activator on the HUD's STA and
//             consumes clicks; NativeCalendarNotifications then schedules with
//             the process identity (native/calendar_notifications.hpp).
//   uninstall tools/calendar_notification_setup remove <EndfieldHUD.exe>
//             -> removes every scheduled Calendar toast, then exactly the
//                matching shortcut and LocalServer32 binding.
//
// A registration that names another executable (a moved or second install)
// is a conflict: it is never repaired or used, and reminders stay
// "unavailable" rather than appearing under someone else's identity.
#include "native/app_notification_identity.hpp"
#include "native/calendar_notifications.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace endfield::native {
// Stable for every EndfieldHUD.exe build. The AppUserModelID is the Mac bundle
// identifier (Resources/Info.plist CFBundleIdentifier); the activator CLSID and
// shortcut name are fixed so an update keeps the user's notification setting.
inline constexpr std::string_view calendarNotificationAppUserModelID = "io.github.endfieldcharge.EndfieldCharge";
inline constexpr std::string_view calendarNotificationActivatorCLSID = "{FF18639C-ADE8-4BA7-AC09-7F1216F9E200}";
inline constexpr std::string_view calendarNotificationShortcutName = "EndfieldHUD.lnk";

// The installed identity of an executable: its folder is the working
// directory and the shortcut is <programs>\EndfieldHUD.lnk. Both arguments
// are absolute UTF-8 Windows paths (drive or UNC); throws
// std::invalid_argument otherwise. Pure: no file system or registry access.
AppNotificationIdentity calendarNotificationIdentity(std::string_view executable, std::string_view programsFolder);
} // namespace endfield::native

#ifdef _WIN32
namespace endfield::native {
// The current user's Start menu Programs folder (FOLDERID_Programs), UTF-8.
std::string currentUserProgramsFolder();
// This process's executable (GetModuleFileNameW) in the current user's Start
// menu Programs folder. Reads two paths only.
AppNotificationIdentity currentCalendarNotificationIdentity();
// Read-only inspection on the caller's STA: the AppUserModelID when the
// registration under `classesRoot` and the shortcut match `identity` exactly,
// nullopt when they are missing or conflict. Throws on an inspection failure.
std::optional<std::string> installedCalendarNotificationAppID(HKEY classesRoot, const AppNotificationIdentity&);
// Production lookup: currentCalendarNotificationIdentity() inspected against
// HKCU\Software\Classes opened for reading. Nullopt when this executable is
// not the installed one (development builds, a moved install, no installer).
std::optional<AppNotificationIdentity> installedCurrentCalendarNotificationIdentity();
// The process's explicit AppUserModelID when one valid identity was
// established (SetCurrentProcessExplicitAppUserModelID); nullopt otherwise.
std::optional<std::string> processAppUserModelID();
// Establishes `appUserModelID` for this process unless it already is its
// explicit identity. Production only (tests never change their process).
HRESULT establishCalendarNotificationProcessIdentity(std::string_view appUserModelID) noexcept;
// Uninstall order: when the registration under `classesRoot` matches, every
// scheduled Calendar toast is cancelled through `scheduled` (the real
// provider for that identity; foreign notifications are untouched), then the
// shortcut and LocalServer32 binding are removed. A missing registration is
// S_OK; a conflicting one is left untouched and reported (ERROR_ALREADY_EXISTS).
HRESULT removeCalendarNotificationIdentity(HKEY classesRoot, const AppNotificationIdentity&,
                                           CalendarNotificationProvider& scheduled) noexcept;
} // namespace endfield::native
#endif
