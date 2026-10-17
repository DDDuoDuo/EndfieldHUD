#pragma once
// Windows services of the updater (packaging/updater.hpp): the asynchronous
// WinHTTP transport, the installed-copy check (developer-build protection),
// download verification, the held hand-off file and the Setup launcher.
//
// WinHTTP: HTTPS only (TLS 1.2+), no cookies, no automatic logon, no
// credentials, no cache and NO automatic redirects (the updater validates
// every hop itself). Bodies are bounded (a larger Content-Length or body
// fails as tooLarge); a 200 body in file mode streams into a NEW file that is
// removed on any failure or cancellation. WinHTTP's completion threads only
// queue results and call `notify` (one posted owner message); completions
// run in drain() on the owner thread. Resource deadlines are host-clock
// seconds folded into the host's single scheduler: no timer, thread or poll.
// The WinHTTP session exists only while a request waits or is in flight: the
// first request opens it (on the `background` queue when given) and the last
// completion, cancellation or deadline closes it. Constructing the transport
// opens none, so a run that never checks starts none of WinHTTP's threads.
#ifdef _WIN32
#include "packaging/foreground_launch.hpp"
#include "packaging/package_format.hpp"
#include "packaging/updater.hpp"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::packaging::updates {
class WinHttpTransport final : public Transport {
public:
    struct Options {
        std::function<void()> notify;   // thread-safe: posts one owner message
        std::function<double()> now;    // host clock (seconds)
        std::wstring userAgent{L"EndfieldHUD-release-check"};
        double operationTimeout{12};    // WinHTTP resolve/connect/send/receive timeouts (Mac timeoutIntervalForRequest)
        // Tests only: plain http://127.0.0.1:<port>/ URLs and no proxy, so a
        // test never resolves or contacts anything beyond the loopback.
        bool loopbackOnly{};
        // Opens the session off the owner thread (EndfieldHUD: the shared
        // utility queue; `done` runs on the owner thread). False when the
        // queue cannot take it: the session then opens on the owner thread,
        // as it does without `background`. Requests wait for the session.
        std::function<bool(std::function<void()> work, std::function<void(std::exception_ptr)> done)> background;
    };
    explicit WinHttpTransport(Options);
    ~WinHttpTransport() override;
    WinHttpTransport(const WinHttpTransport&) = delete;
    WinHttpTransport& operator=(const WinHttpTransport&) = delete;
    std::uint64_t start(TransportRequest, Completion) override;
    void cancel(std::uint64_t) noexcept override;
    std::size_t drain() override;
    std::optional<double> nextDeadline() const override;
    bool deadline(double now) override;
    std::size_t active() const override;
    // Diagnostics: whether a WinHTTP session is open now, and how many were opened.
    bool sessionOpen() const noexcept;
    std::uint64_t sessionOpens() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// The running executable (GetModuleFileNameW).
std::filesystem::path runningExecutable();
// HKCU uninstall entry written by EndfieldHUD Setup (packaging/installer.hpp).
inline constexpr std::wstring_view productionUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EndfieldHUD";
// True when `executable` is the registered installed copy of `current`: its
// folder holds EndfieldHUD.install.json naming exactly this version, and the
// HKCU uninstall entry (`uninstallKey`, read only) has that folder as
// InstallLocation. Development builds, portable copies and moved installs
// only discover releases.
bool installedCopy(const std::filesystem::path& executable, const ProductVersion& current, std::wstring_view uninstallKey);
// Authenticode signer display name of a file that verifies (WinVerifyTrust,
// no UI, cached revocation data only); nullopt when unsigned or untrusted.
std::optional<std::string> authenticodeSigner(const std::filesystem::path&);
// Download verification (throws PackageError naming the problem): the file's
// SHA-256 equals `sha256`, its own payload verifies (packaging/package_io)
// and names exactly `expected` (x64, complete), and when `running` is
// Authenticode-signed the setup is signed by the same publisher.
void verifySetup(const std::filesystem::path& setup, const std::string& sha256, const ProductVersion& expected,
                 const std::filesystem::path& running);
// Opens the setup without write or delete sharing and re-hashes it; the file
// cannot change until the returned object is released (after CreateProcess).
std::shared_ptr<void> holdSetup(const std::filesystem::path& setup, const std::string& sha256);
// Starts Setup (CreateProcessW, no shell, outside any job when allowed) and
// registers a one-shot thread-pool wait that posts (message, wParam, 0) to
// `window` when it exits. The returned process reports its exit code. Setup
// starts suspended until this run has passed it the right to take the
// foreground it holds from the Install click, which Setup passes on to the
// EndfieldHUD it reopens (packaging/foreground_launch.hpp; `grant` only in
// tests).
std::unique_ptr<SetupProcess> launchSetup(const std::filesystem::path& setup, const std::vector<std::string>& arguments,
                                          void* window, std::uint32_t message, std::uintptr_t wParam, const ForegroundGrant& grant = {});
// CommandLineToArgvW-compatible quoting of one argument.
std::wstring commandLineArgument(std::wstring_view);
} // namespace endfield::packaging::updates
#endif
