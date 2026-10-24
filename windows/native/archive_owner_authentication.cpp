#include "native/archive_owner_authentication.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <windows.h>
#include <wincred.h>
#include <security.h>
#include <UserConsentVerifierInterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.UI.h>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace endfield::native {
namespace {
namespace UI = winrt::Windows::Security::Credentials::UI;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(std::size_t(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
// The message's wParam: which phase answered. lParam: the request token.
enum Phase : WPARAM { availability = 1, verification = 2 };
}

struct NativeArchiveOwnerAuthentication::Impl : std::enable_shared_from_this<Impl> {
    HWND window;
    UINT message;
    struct Pending { std::wstring reason; std::function<void(bool)> done; };
    std::map<LPARAM, Pending> pending;
    // Results that arrived on a WinRT thread, read on the owner thread.
    std::mutex mutex;
    std::map<LPARAM, int> results; // availability: UserConsentVerifierAvailability; verification: UserConsentVerificationResult
    LPARAM serial{};
    bool busy{};
    Impl(HWND w, UINT m) : window(w), message(m) {}
    void finish(LPARAM token, bool approved) {
        const auto it = pending.find(token);
        if (it == pending.end()) return;
        auto done = std::move(it->second.done);
        pending.erase(it);
        busy = false;
        if (done) done(approved);
    }
    void post(Phase phase, LPARAM token, int value) {
        { std::lock_guard lock(mutex); results[token * 4 + LPARAM(phase)] = value; }
        if (!PostMessageW(window, message, phase, token)) { std::lock_guard lock(mutex); results.erase(token * 4 + LPARAM(phase)); }
    }
    // The credential prompt for the current user, checked with LogonUserW.
    bool credentials(const std::wstring& reason) {
        wchar_t user[256]{};
        ULONG userLength = ARRAYSIZE(user);
        if (!GetUserNameExW(NameSamCompatible, user, &userLength)) return false;
        CREDUI_INFOW info{sizeof(info), window, L"EndfieldHUD", reason.c_str(), nullptr};
        ULONG package{};
        void* buffer{};
        ULONG size{};
        BOOL save{};
        const auto status = CredUIPromptForWindowsCredentialsW(&info, 0, &package, nullptr, 0, &buffer, &size, &save, CREDUIWIN_ENUMERATE_CURRENT_USER);
        if (status != ERROR_SUCCESS) { if (buffer) { SecureZeroMemory(buffer, size); CoTaskMemFree(buffer); } return false; }
        wchar_t name[CREDUI_MAX_USERNAME_LENGTH + 1]{}, domain[CREDUI_MAX_DOMAIN_TARGET_LENGTH + 1]{}, password[CREDUI_MAX_PASSWORD_LENGTH + 1]{};
        DWORD nameLength = ARRAYSIZE(name), domainLength = ARRAYSIZE(domain), passwordLength = ARRAYSIZE(password);
        const BOOL unpacked = CredUnPackAuthenticationBufferW(0, buffer, size, name, &nameLength, domain, &domainLength, password, &passwordLength);
        SecureZeroMemory(buffer, size);
        CoTaskMemFree(buffer);
        bool approved{};
        if (unpacked) {
            // The prompt lists only the current user; its name may come back
            // as DOMAIN\user, a UPN or the bare name.
            std::wstring account(name), realm(domain);
            if (realm.empty()) if (const auto slash = account.find(L'\\'); slash != std::wstring::npos) { realm = account.substr(0, slash); account = account.substr(slash + 1); }
            HANDLE token{};
            if (LogonUserW(account.c_str(), realm.empty() ? nullptr : realm.c_str(), password, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token)) {
                CloseHandle(token);
                approved = true;
            } else if (GetLastError() == ERROR_ACCOUNT_RESTRICTION && password[0] == L'\0') {
                approved = true; // no password on the account: the Mac's passcodeNotSet
            }
        }
        SecureZeroMemory(password, sizeof(password));
        return approved;
    }
    void request(std::string reason, std::function<void(bool)> done) {
        if (busy || !IsWindow(window)) { if (done) done(false); return; }
        busy = true;
        const LPARAM token = ++serial;
        pending[token] = {wide(reason), std::move(done)};
        const std::weak_ptr<Impl> weak = weak_from_this();
        try {
            auto check = UI::UserConsentVerifier::CheckAvailabilityAsync();
            check.Completed([weak, token](const IAsyncOperation<UI::UserConsentVerifierAvailability>& operation, AsyncStatus status) {
                const auto self = weak.lock();
                if (!self) return;
                int value = -1;
                try { if (status == AsyncStatus::Completed) value = int(operation.GetResults()); } catch (...) {}
                self->post(availability, token, value);
            });
        } catch (...) {
            // No Windows Runtime consent service: straight to the credential prompt.
            const auto approved = credentials(pending[token].reason);
            finish(token, approved);
        }
    }
    bool handle(WPARAM phase, LPARAM token) {
        int value = -1;
        {
            std::lock_guard lock(mutex);
            const auto it = results.find(token * 4 + LPARAM(phase));
            if (it == results.end()) return true;
            value = it->second;
            results.erase(it);
        }
        if (!pending.contains(token)) return true;
        if (phase == availability) {
            if (value == int(UI::UserConsentVerifierAvailability::Available)) {
                try {
                    const auto factory = winrt::get_activation_factory<UI::UserConsentVerifier, IUserConsentVerifierInterop>();
                    IAsyncOperation<UI::UserConsentVerificationResult> operation{nullptr};
                    const winrt::hstring text{pending[token].reason};
                    winrt::check_hresult(factory->RequestVerificationForWindowAsync(window, static_cast<HSTRING>(winrt::get_abi(text)),
                        winrt::guid_of<IAsyncOperation<UI::UserConsentVerificationResult>>(), winrt::put_abi(operation)));
                    const std::weak_ptr<Impl> weak = weak_from_this();
                    operation.Completed([weak, token](const IAsyncOperation<UI::UserConsentVerificationResult>& op, AsyncStatus status) {
                        const auto self = weak.lock();
                        if (!self) return;
                        int result = -1;
                        try { if (status == AsyncStatus::Completed) result = int(op.GetResults()); } catch (...) {}
                        self->post(verification, token, result);
                    });
                    return true;
                } catch (...) {}
            }
            // Hello unavailable (no device, not set up, disabled by policy): the account password.
            const auto approved = credentials(pending[token].reason);
            finish(token, approved);
            return true;
        }
        finish(token, value == int(UI::UserConsentVerificationResult::Verified));
        return true;
    }
};

NativeArchiveOwnerAuthentication::NativeArchiveOwnerAuthentication(void* window, std::uint32_t message) : impl_(std::make_shared<Impl>(static_cast<HWND>(window), message)) {
    need(impl_->window && IsWindow(impl_->window) && message >= WM_APP && message <= 0xbfff, "Archive owner authentication needs the HUD window and a private message");
}
NativeArchiveOwnerAuthentication::~NativeArchiveOwnerAuthentication() {
    // Answers still owed deny (exactly once); late WinRT completions find no owner.
    auto pending = std::move(impl_->pending);
    impl_->pending.clear();
    for (auto& [token, p] : pending) { (void)token; if (p.done) try { p.done(false); } catch (...) {} }
}
modules::ArchiveState::OwnerAuthentication NativeArchiveOwnerAuthentication::authenticator() {
    const std::weak_ptr<Impl> weak = impl_;
    return [weak](std::string reason, std::function<void(bool)> done) {
        const auto self = weak.lock();
        if (!self) { if (done) done(false); return; }
        self->request(std::move(reason), std::move(done));
    };
}
bool NativeArchiveOwnerAuthentication::message(std::uint32_t m, std::uintptr_t wParam, std::intptr_t lParam) {
    if (m != impl_->message) return false;
    return impl_->handle(WPARAM(wParam), LPARAM(lParam));
}
} // namespace endfield::native
#endif
