#pragma once
// The owner proof of classified-document recovery on Windows (Mac
// ArchiveDeviceOwnerAuthentication, Sources/ArchiveClassification.swift:206-
// 222; inventory DOCS-336): ArchiveState::setOwnerAuthentication's native hook.
// The Mac asks LAContext .deviceOwnerAuthentication (Touch ID, Apple Watch or
// the login password) with the reason "restore access to a classified
// document" and approves without a prompt only when the account has no
// password (LAError.passcodeNotSet).
//
// Windows:
//  1. Windows Hello (Windows.Security.Credentials.UI.UserConsentVerifier for
//     the HUD window, IUserConsentVerifierInterop::
//     RequestVerificationForWindowAsync) when it is available: Verified
//     approves; Canceled, RetriesExhausted and every other result deny.
//  2. Otherwise (no Hello device, not set up, disabled by policy): the Windows
//     credential prompt for the CURRENT user (CredUIPromptForWindowsCredentials
//     with CREDUIWIN_ENUMERATE_CURRENT_USER, owned by the HUD window),
//     the entered password checked with LogonUserW for that user and zeroed.
//     A cancel denies. An account without a password (LogonUserW refuses a
//     blank password: ERROR_ACCOUNT_RESTRICTION) approves, the Mac's
//     passcodeNotSet rule; every other failure denies.
// The answer is delivered exactly once on the owner thread: Hello completions
// arrive on a WinRT thread and are posted to the window as `message`
// (deliver them with message()); the credential prompt is modal on the owner
// thread. A request while one runs is denied at once. The canvas keeps the
// HUD open meanwhile (presentingPanel while recovering).
//
// Hidden tests never construct this: they inject a fake
// ArchiveState::OwnerAuthentication (no prompt, no window).
#include "modules/archive_state.hpp"
#ifdef _WIN32
#include <memory>

namespace endfield::native {
class NativeArchiveOwnerAuthentication final {
public:
    NativeArchiveOwnerAuthentication(void* window, std::uint32_t message);
    ~NativeArchiveOwnerAuthentication();
    NativeArchiveOwnerAuthentication(const NativeArchiveOwnerAuthentication&) = delete;
    NativeArchiveOwnerAuthentication& operator=(const NativeArchiveOwnerAuthentication&) = delete;
    // The hook for ArchiveState::setOwnerAuthentication (valid while this lives).
    modules::ArchiveState::OwnerAuthentication authenticator();
    // The posted Hello answer (owner thread); true when it was this message.
    bool message(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif
