#pragma once
// Calendar reminders area (app/module_owner.hpp, "Adding a module area";
// registered from cmake/modules/local.cmake). The built-in Calendar owner
// plans, dedupes and schedules reminder toasts through NativeCalendarNotifications
// (Mac HUDCalendarController/HUDCalendarNativeNotifications). This area owns
// the remaining Windows half of an unpackaged per-user install:
//   - finds the installer-created Start-menu identity of THIS executable,
//     read-only (native/calendar_notification_identity.hpp), and establishes
//     its AppUserModelID for the process, which the native provider then
//     schedules under (Windows has no identity without the installer, so
//     reminders report "unavailable" rather than being faked);
//   - registers the shortcut's toast activator (INotificationActivationCallback)
//     on the HUD's STA, so a click is delivered to the running HUD through
//     the area's private WM_APP message instead of starting a second process
//     that would forward and exit while COM waits for a server;
//   - consumes each click without opening the HUD: the Mac LSUIElement app
//     only becomes active and shows no window (HUDUpdateController handles
//     update notifications only). Becoming active makes the Mac Calendar
//     refresh (HUDCalendarController observes didBecomeActive ->
//     refreshForSystemChange); here a consumed Calendar click calls the
//     Calendar owner's published refresh sink once per notice. Untrusted or
//     foreign launch arguments are ignored and refresh nothing.
// Absent (null factory) in development previews and when this executable is
// not the installed one. Hidden coverage uses the synthetic identity on a
// private message-only route: no registry, Start menu, COM class registration,
// process identity or notification-center access.
#include "app/application.hpp"
#include "native/app_notification_identity.hpp"
#include <notificationactivationcallback.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::areas {
// Published by the host for the built-in Calendar owner under this key: the
// refresh the Mac runs when the app becomes active (CalendarPreview::
// systemChanged, then the Calendar wake is rescheduled), called with the
// notice's time after a Calendar reminder click was consumed. The area looks
// it up when used; without it a click refreshes nothing until the next HUD
// activation.
inline constexpr std::string_view calendarReminderActivationKey = "calendar.reminderActivation";
using CalendarReminderActivationSink = std::function<void(double time)>;

// Diagnostics of the area, published under `key` (UI thread; valid while the
// area is registered).
class CalendarRemindersService {
public:
    static constexpr std::string_view key = "calendar.reminders";
    virtual std::string_view appUserModelID() const noexcept = 0;
    virtual bool activatorRegistered() const noexcept = 0;   // COM class object live (production)
    virtual std::uint64_t calendarActivations() const noexcept = 0; // clicks on Calendar reminders
    virtual std::uint64_t ignoredActivations() const noexcept = 0;  // foreign or malformed launch arguments
    virtual std::uint64_t calendarRefreshes() const noexcept = 0;   // notices delivered to the Calendar refresh sink
    virtual const std::optional<std::string>& lastEvent() const noexcept = 0; // event UUID of the last Calendar click
    // The area's actual activation callback without any class registration
    // (synthetic fixtures; NativeAppNotificationActivation::createCallback).
    virtual HRESULT createActivationCallback(INotificationActivationCallback**) noexcept = 0;
protected:
    ~CalendarRemindersService() = default;
};

// Explicit inputs for tests; the registered factory derives them from the
// context mode (production: the installed identity of the running executable).
struct CalendarRemindersOptions {
    std::function<std::optional<native::AppNotificationIdentity>()> installed; // nullopt: area absent
    bool establishProcessIdentity{};  // SetCurrentProcessExplicitAppUserModelID
    bool registerActivator{};         // CoRegisterClassObject for the activator CLSID
    bool privateRoute{};              // post clicks to a private message-only window (coverage)
};
std::unique_ptr<app::ModuleOwner> makeCalendarRemindersArea(const app::ApplicationModuleContext&, CalendarRemindersOptions);

// Area factory: the installed identity with its live activator in
// EndfieldHUD.exe, the synthetic identity in hidden coverage, null otherwise.
std::unique_ptr<app::ModuleOwner> calendarRemindersModule(const app::ApplicationModuleContext&);
} // namespace endfield::areas
