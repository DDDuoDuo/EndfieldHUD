#pragma once
// Windows updater controller (portable; every OS service is injected).
// Port of the Mac HUDUpdateController/HUDUpdateState over the Windows releases
// of the shared GitHub release list (packaging/update_check.hpp: releases
// carrying the Windows installer assets), with the Windows install path in
// place of Sparkle:
//
//   check     explicit "Check for Updates" (About row, tray), or at most once
//             per day when the HUD opens while "Check for updates daily" is on.
//             One GitHub request; no timer, no polling loop.
//   download  explicit consent on an available installable release: the
//             release's SHA256SUMS and then its setup are downloaded (HTTPS,
//             redirects only to github.com/*.githubusercontent.com, exact
//             published size), the setup's SHA-256 must equal its checksum
//             line and its own verified payload must name exactly that
//             version (and, when this build is Authenticode-signed, the same
//             publisher): `verify`.
//   install   explicit consent on a verified download: the HUD closes first
//             (Mac prepareForRestart), the setup is re-hashed while it is
//             held without write sharing, a hand-off record is saved, and
//             Setup runs quietly in place: it closes EndfieldHUD through the
//             session-end path (data saved), swaps the install folder, rolls
//             back on any failure and restarts the app. The next run (or the
//             Setup exit while this run lives) reports the outcome.
//
// Only an installed copy updates itself (developer-build protection: the
// running executable must be the registered install with a matching
// EndfieldHUD.install.json). Other copies, isolated data roots and
// development runs only discover releases and open the release page.
//
// Persistent state: <folder>/state.json (format EndfieldHUD.Windows.Updates,
// schema 1): the daily-check preference, the last check time and a pending
// hand-off. The folder also holds at most one download. Mac updater state
// (HUDUpdateAutomaticallyInstall, Sparkle keys) is never imported.
//
// Threading: UI thread only. File work runs through `submit` (the shared
// UtilityExecutor); network work through the injected cancellable Transport,
// whose completions run inside drain(). Deadlines come from nextWakeTime().
#include "core/localization.hpp"
#include "packaging/package_format.hpp"
#include "packaging/update_check.hpp"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace endfield::packaging::updates {
// --- Transport ---------------------------------------------------------------------
struct TransportRequest {
    std::string url;               // absolute https URL (test transports may also serve http://127.0.0.1)
    Headers headers;
    std::uint64_t maximumBytes{};  // body cap (a larger Content-Length or body fails as tooLarge)
    std::filesystem::path file;    // non-empty: a 200 body streams into this NEW file (removed on failure)
    double timeout{20};            // resource deadline (host-clock seconds)
};
struct TransportResponse {
    enum class Failure : std::uint8_t { none, network, cancelled, tooLarge, timeout, file };
    Failure failure{Failure::none};
    HttpResponse response;         // status, headers, body (memory mode), url (= request URL)
    std::uint64_t bytes{};         // body bytes received
};
std::string_view transportFailureName(TransportResponse::Failure) noexcept;
class Transport {
public:
    virtual ~Transport() = default;
    using Completion = std::function<void(TransportResponse)>;
    // Completions run only inside drain(), never re-entrantly from start().
    virtual std::uint64_t start(TransportRequest, Completion) = 0;
    virtual void cancel(std::uint64_t) noexcept = 0;    // no completion afterwards
    virtual std::size_t drain() = 0;
    virtual std::optional<double> nextDeadline() const = 0;
    virtual bool deadline(double now) = 0;              // fails overdue requests (timeout)
    virtual std::size_t active() const = 0;
};

// --- State (HUDUpdateState) -----------------------------------------------------------
enum class Phase : std::uint8_t { idle, checking, current, available, downloading, ready, installing, failed };
std::string_view phaseName(Phase) noexcept;
// What the detail line says, rendered per language (Mac texts where the Mac
// has one, Windows texts in the five source languages otherwise).
struct Detail {
    enum class Kind : std::uint8_t {
        none, checkError, downloadPrompt, discoverOnly, installPrompt, downloadFailed,
        verificationFailed, launchFailed, setupFailed, closeRefused, updated, interrupted
    };
    Kind kind{Kind::none};
    CheckError error;   // checkError
    std::string value;  // version (downloadPrompt, discoverOnly, updated) or Setup reason (setupFailed)
    std::uint64_t bytes{}; // downloadPrompt
    bool operator==(const Detail&) const = default;
};
struct UpdateState {
    Phase phase{Phase::idle};
    std::string latestVersion;  // newest Windows release for this channel ("1.2.1 (19)"); empty: none
    std::string updateVersion;  // the install candidate
    std::string releaseURL;     // its GitHub release page
    Detail detail;
    bool automaticChecks{true}; // "Check for updates daily" (Windows; default on like the Mac checks)
    bool installationSupported{};
    bool hasUpdate() const noexcept;
    bool operator==(const UpdateState&) const = default;
};
std::string phaseTitle(Phase, core::Language);              // HUDUpdateState.title
std::string detailText(const Detail&, core::Language);
std::string trayTitle(const UpdateState&, core::Language);   // AppDelegate.updateUpdateMenu
std::string automaticChecksTitle(core::Language);            // About toggle label on Windows
std::string releaseDisplay(const Release&);                  // "1.2.1 (19)", "1.3.0-beta.1"

// --- Setup hand-off -----------------------------------------------------------------------
// A launched EndfieldHUD-Setup.exe. exitCode() is empty while it runs; the
// platform posts the owner's notice when it exits (no polling).
class SetupProcess {
public:
    virtual ~SetupProcess() = default;
    virtual std::optional<int> exitCode() const = 0;
};
// Setup command line of an in-place upgrade (packaging/setup_arguments.hpp).
std::vector<std::string> handoffArguments(const std::filesystem::path& resultFile);
inline constexpr std::string_view stateFileName = "state.json";
inline constexpr std::string_view handoffResultName = "handoff-result.json";
inline constexpr std::string_view stateFormat = "EndfieldHUD.Windows.Updates";
inline constexpr int stateSchema = 1;
inline constexpr double dailyInterval = 86400;
inline constexpr double handoffPatience = 600; // an unresolved hand-off older than this is reported interrupted

struct UpdaterOptions {
    ProductVersion current;            // this build (source-authority release and build)
    std::filesystem::path folder;      // <data root>/Updates (absolute)
    bool automaticAllowed{};           // daily checks may run (never for an isolated data root)
    bool installAllowed{};             // downloads/installs may run when `installedCopy` agrees
    std::string endpoint{releasesEndpoint};
};
struct UpdaterServices {
    Transport* transport{};
    // Shared executor: false when full (the caller retries on its next event).
    std::function<bool(std::function<void()> work, std::function<void(std::exception_ptr)> completion)> submit;
    std::function<double()> clock;      // host clock (transport deadlines)
    std::function<double()> wallClock;  // Unix seconds (daily check, Retry-After)
    // Executor: is the running executable the registered installed copy?
    std::function<bool()> installedCopy;
    // Executor: verify a complete download (SHA-256 against `sha256`, the
    // setup's own payload names `expected`, publisher continuity). Throws a
    // std::exception naming the problem.
    std::function<void(const std::filesystem::path& setup, const std::string& sha256, const ProductVersion& expected)> verify;
    // Executor: hold the verified setup without write sharing and re-hash it;
    // the returned object releases it. Throws when it changed.
    std::function<std::shared_ptr<void>(const std::filesystem::path& setup, const std::string& sha256)> hold;
    // UI thread: start Setup with these arguments. Throws when it cannot start.
    std::function<std::unique_ptr<SetupProcess>(const std::filesystem::path& setup, const std::vector<std::string>& arguments)> launch;
    std::function<bool()> overlayVisible;
    std::function<void()> closeOverlay;
    std::function<void()> changed;      // state changed (observer; UI thread)
};

class Updater final {
public:
    Updater(UpdaterOptions, UpdaterServices);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    // Loads the saved state, removes stale downloads and resolves a pending
    // hand-off (executor). Nothing is requested from the network.
    void start(double time);
    void stop() noexcept;               // quitting: cancel every request; no further work
    // "Check for Updates": one metadata request (false: busy, stopped, not loaded).
    bool check(double time);
    // The About update row: check, download (consent), install (consent) or,
    // for a release this copy cannot install, the page to open (returned).
    std::optional<std::string> activate(double time);
    bool download(double time);
    bool install(double time);
    void setAutomaticChecks(bool, double time);
    void overlayOpened(double time);    // daily check when due; re-resolves a pending hand-off
    void overlayConcealed(double time); // continues a consented install
    void setupExited(double time);      // the launched Setup ended while this run lives
    std::size_t drain();                // transport completions
    std::optional<double> nextWakeTime(double now) const;
    bool deadline(double now);

    const UpdateState& state() const noexcept { return state_; }
    bool loaded() const noexcept { return loaded_; }
    std::optional<double> lastCheck() const noexcept { return lastCheck_; }
    const std::optional<Release>& candidate() const noexcept { return candidate_; }
    std::filesystem::path downloadedSetup() const;   // empty unless ready
    const SetupProcess* setup() const noexcept { return setup_.get(); }
    std::uint64_t requests() const noexcept { return requests_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    UpdateState state_;
    bool loaded_{};
    std::optional<double> lastCheck_;
    std::optional<Release> candidate_;
    std::unique_ptr<SetupProcess> setup_;
    std::uint64_t requests_{};
    friend struct Impl;
};
} // namespace endfield::packaging::updates
