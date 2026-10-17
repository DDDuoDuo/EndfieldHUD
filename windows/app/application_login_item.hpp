#pragma once
// Launch at login for the unpackaged EndfieldHUD.exe (Mac LoginItemManager).
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run holds one "EndfieldHUD"
// value: "<exe>" --login (AppDelegate: --login suppresses onboarding). When
// the user turned the entry off in Windows Settings → Apps → Startup (or Task
// Manager), Explorer's StartupApproved record says so: that choice is reported
// and never overridden. Registry access is synchronous and local (no IPC), on
// the UI thread at startup, on explicit Settings edits and on activation.
// An MSIX package would use StartupTask instead (installer decision pending).
#ifdef _WIN32
#include "core/localization.hpp"
#include <filesystem>
#include <optional>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::app {
struct LoginItemStatus {
    bool registered{};      // our value exists with the current command
    bool stale{};           // our value exists but launches another path
    bool disabledByUser{};  // StartupApproved marks it off
    bool enabled() const noexcept { return registered && !disabledByUser; }
    bool operator==(const LoginItemStatus&) const = default;
};
struct LoginItemLocation {
    HKEY root{HKEY_CURRENT_USER};
    std::wstring runKey, approvedKey, valueName, command;
    static LoginItemLocation production(const std::filesystem::path& executable);
};
class LoginItemRegistration final {
public:
    explicit LoginItemRegistration(LoginItemLocation);
    LoginItemStatus read() const;
    // Explicit Settings edit; returns localized error text (nothing changed).
    std::optional<std::string> setEnabled(bool enabled, core::Language) const;
    // Launch: register (or repair a stale path) only when the saved preference
    // is on and the user has not turned the entry off.
    std::optional<std::string> ensureEnabled(core::Language) const;
    static std::string statusDescription(const LoginItemStatus&, core::Language);
    const LoginItemLocation& location() const noexcept { return location_; }
private:
    LoginItemLocation location_;
};
} // namespace endfield::app
#endif
