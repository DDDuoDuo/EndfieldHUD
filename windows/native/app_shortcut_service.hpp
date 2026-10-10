#pragma once
#include "modules/app_shortcut_model.hpp"
#include <cstdint>
#include <memory>
#include <optional>

namespace endfield::native {
enum class AppShortcutSelectionKind {file,packagedApp};
struct AppShortcutSelection {
    AppShortcutSelectionKind kind{AppShortcutSelectionKind::file};
    std::string value; // absolute Windows .exe/.lnk path, or exact AUMID
    bool operator==(const AppShortcutSelection&)const=default;
};
enum class AppShortcutLaunchKind {executable,shellLink,packagedApp};
// One bounded OS inspection. Native read handles are short-lived and permit
// normal application/link updates; file IDs are diagnostic, not launch grants.
// No COM object or lease is stored in JSON.
struct AppShortcutResolved {
    AppShortcutSelection selection;
    std::string name,applicationKey,selectedFileKey,resolvedApplicationKey;
    AppShortcutLaunchKind launchKind{AppShortcutLaunchKind::executable};
    std::string executablePath,arguments,workingDirectory,appUserModelID;
    bool runAsUser{};
    std::shared_ptr<void>lease;
};
struct AppShortcutLaunchReceipt {std::uint32_t processID{};};
struct AppShortcutOS {
    std::function<AppShortcutResolved(const AppShortcutSelection&)>inspect;
    std::function<AppShortcutLaunchReceipt(const AppShortcutResolved&,std::uintptr_t owner)>launch;
};
// Fresh OS queries happen only on explicit selection/save/launch events. The
// host schedules inspection on its existing COM utility worker. Launch is an
// explicit action after the HUD's close/focus handoff; this class never starts
// a worker, window, timer, scan, process observer, store write or auto-launch.
class NativeAppShortcutService final {
public:
    explicit NativeAppShortcutService(AppShortcutOS);
    modules::ShortcutCandidate inspect(const AppShortcutSelection&)const;
    modules::ShortcutCandidate reinspect(const modules::ShortcutJson&locator)const;
    AppShortcutLaunchReceipt launch(const modules::ShortcutRecord&,std::uintptr_t owner=0)const;
    static bool sameTarget(const modules::ShortcutJson&,const modules::ShortcutJson&)noexcept;
private:AppShortcutOS os_;
};
enum class AppShortcutPickerMode {files,installedApps};
struct AppShortcutPickerLabels {
    std::string title,accept,applications;
    AppShortcutPickerMode mode{AppShortcutPickerMode::files};
};
// Host invokes choose outside store/state callbacks, on its existing STA owner.
// The native dialog performs its normal user-directed navigation. No private
// inventory is built. Cancellation or destruction discards any stale result.
class AppShortcutPickerDialog {
public:
    virtual ~AppShortcutPickerDialog()=default;
    virtual std::optional<AppShortcutSelection>show(std::uintptr_t owner,const AppShortcutPickerLabels&)=0;
    virtual void cancel()noexcept=0;
};
class NativeAppShortcutPicker final {
public:
    using Factory=std::function<std::shared_ptr<AppShortcutPickerDialog>()>;
    explicit NativeAppShortcutPicker(Factory);
    ~NativeAppShortcutPicker();
    NativeAppShortcutPicker(const NativeAppShortcutPicker&)=delete;
    NativeAppShortcutPicker&operator=(const NativeAppShortcutPicker&)=delete;
    std::optional<AppShortcutSelection>choose(std::uintptr_t owner,const AppShortcutPickerLabels&);
    void cancel();bool presenting()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
#ifdef _WIN32
// Caller already owns COM/WinRT initialization; no apartment is created here.
AppShortcutOS windowsAppShortcutOS();
NativeAppShortcutPicker::Factory windowsAppShortcutPickerFactory();
#endif
}
