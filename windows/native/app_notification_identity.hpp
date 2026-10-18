#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// An installer supplies its own stable identity, not an identity generated at
// startup. Paths are absolute UTF-8 Windows paths. This helper never discovers
// a user's Start Menu, writes a default registry root or installs a shortcut
// implicitly. The executable must be an ordinary existing file and its parent
// working directory must already exist.
struct AppNotificationIdentity {
    std::string appUserModelID,activatorCLSID,executable,workingDirectory,shortcut;
    bool operator==(const AppNotificationIdentity&)const=default;
};
bool validAppNotificationID(std::string_view)noexcept;
bool validAppNotificationCLSID(std::string_view)noexcept;
bool validAppNotificationIdentity(const AppNotificationIdentity&)noexcept;
// Fixed activation-only command. The normal host must recognize this argument
// and await the COM callback before choosing a notification's module/event.
inline constexpr std::string_view appNotificationActivationArgument="--notification-activation";
std::string appNotificationServerCommand(const AppNotificationIdentity&);
inline constexpr std::size_t appNotificationMaximumArguments=65536;
inline constexpr std::size_t appNotificationMaximumQueuedActivations=32;
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <notificationactivationcallback.h>
namespace endfield::native {
namespace detail {struct AppNotificationActivationState;}
enum class AppNotificationRegistration {missing,matching,conflict};
enum class AppNotificationRegistryLifetime {persistent,volatileFixture};
// Explicit installer operations. classesRoot is a borrowed opened Classes
// root (shipping: HKCU\Software\Classes; tests: a PRIVATE temporary subtree).
// The shortcut is normally installer-selected Start Menu Programs/*.lnk.
// Existing conflicting files/values are never replaced. Unknown sibling registry keys/values remain untouched; an existing matching
// shortcut is never rewritten. The normal shortcut has no activation-only argument;
// only LocalServer32 carries that argument. Fresh installation stages the shortcut and rolls back its new binding on
// failure. Removal can report a partial filesystem/registry failure; inspect
// its result before treating an uninstall as complete.
// Installer serializes these operations; no cross-process installer lock or
// transaction spanning the filesystem and registry is created here.
HRESULT inspectAppNotificationRegistration(HKEY classesRoot,const AppNotificationIdentity&,
    AppNotificationRegistration*)noexcept;
HRESULT installAppNotificationIdentity(HKEY classesRoot,const AppNotificationIdentity&,
    AppNotificationRegistryLifetime=AppNotificationRegistryLifetime::persistent)noexcept;
HRESULT removeAppNotificationIdentity(HKEY classesRoot,const AppNotificationIdentity&)noexcept;
// App startup only, before creating its first window. Tests do not call this or
// bind a real application identity. Caller owns restoration if used in a host.
HRESULT establishAppNotificationProcessIdentity(std::string_view appUserModelID)noexcept;

struct AppNotificationActivationRoute {HWND owner{};UINT message{};std::uint64_t generation{};};
struct AppNotificationActivation {std::string arguments;};
// ONE app-owned activation server. Construction performs no COM registration.
// start/stop/take use the window's existing STA thread; start uses the supplied
// stable CLSID and never initializes another apartment. Activate may arrive on
// another COM thread: it copies bounded arguments and coalesces one WM_APP,
// never calls a view or reads input. Call take() for matching route generation.
// Owner-thread destruction is mandatory. Stop BEFORE HWND/apartment destruction. Existing callback objects become
// inert after stop/destruction. No timer, worker or permanent notification poll.
class NativeAppNotificationActivation final {
public:
    NativeAppNotificationActivation(std::string appUserModelID,std::string activatorCLSID,
        AppNotificationActivationRoute);
    ~NativeAppNotificationActivation();
    NativeAppNotificationActivation(const NativeAppNotificationActivation&)=delete;
    NativeAppNotificationActivation&operator=(const NativeAppNotificationActivation&)=delete;
    HRESULT start()noexcept;
    HRESULT stop()noexcept;
    std::vector<AppNotificationActivation>take();
    bool registered()const noexcept;
    // Same callback implementation without CoRegisterClassObject. Synthetic
    // hidden-window fixtures use this to exercise late/reentrant COM lifetime;
    // it creates no installed identity or notification-center registration.
    HRESULT createCallback(INotificationActivationCallback**)noexcept;
private:std::shared_ptr<detail::AppNotificationActivationState>impl_;
};
// Integration recipe (no operation below is automatic):
// 1. Installer STA opens HKCU Software\Classes explicitly with read/write
//    access, chooses Programs/<app>.lnk, then installAppNotificationIdentity.
// 2. Runtime uses its existing STA; establish the SAME process AUMID before
//    HWND creation, construct/start ONE activation owner after HWND creation,
//    then use that AUMID in CalendarNotificationOptions before scheduling.
// 3. Matching WM_APP generation -> take() -> calendarNotificationEvent(args).
//    Do not treat the static --notification-activation argument as an event.
// 4. At explicit quit: drain CalendarState saves/scheduling and reminder work
//    through the shared executor WHILE State remains alive (receipt saves can
//    follow reminder completion). Then destroy State, stop activation, and
//    destroy HWND/COM apartment. Uninstall explicitly
//    removes only the matching registration after notification cleanup.
// Primary contracts:
// https://learn.microsoft.com/en-us/windows/win32/shell/enable-desktop-toast-with-appusermodelid
// https://learn.microsoft.com/en-us/windows/win32/api/notificationactivationcallback/nn-notificationactivationcallback-inotificationactivationcallback
}
#endif
