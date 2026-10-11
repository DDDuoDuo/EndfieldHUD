#include "app/areas/update_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include "packaging/package_io.hpp"
#include "packaging/setup_arguments.hpp"
#include "packaging/update_fixture.hpp"
#include "packaging/update_platform.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if !defined(EHUD_UPDATE_RELEASE) || !defined(EHUD_UPDATE_BUILD)
#error "cmake/modules/packaging.cmake defines this build's release and build number"
#endif

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace u = packaging::updates;
using app::ApplicationAreaMode;

// wParam of the area's private notice.
constexpr std::uintptr_t transportNotice = 1, setupNotice = 2;
// Hidden coverage: synthetic wall clock origin (2026-10-11T00:00:00Z).
constexpr double coverageEpoch = 1791676800;
constexpr std::string_view releasesPage = "https://github.com/DDDuoDuo/EndfieldHUD/releases";

packaging::ProductVersion buildVersion() {
    const auto v = packaging::parseProductVersion(EHUD_UPDATE_RELEASE, EHUD_UPDATE_BUILD);
    if (!v) throw std::logic_error("This build's release version is invalid");
    return *v;
}
// The shared UtilityExecutor through one route of this area. Accepted state
// writes still finish when the area goes away.
class ExecutorRoute {
public:
    explicit ExecutorRoute(app::UtilityExecutor& e) : executor_(e), route_(e.makeRoute()) {}
    ~ExecutorRoute() { executor_.invalidate(route_, false); }
    ExecutorRoute(const ExecutorRoute&) = delete;
    ExecutorRoute& operator=(const ExecutorRoute&) = delete;
    bool submit(std::function<void()> work, std::function<void(std::exception_ptr)> completion) {
        return executor_.submit(route_, std::move(work), std::move(completion));
    }
private:
    app::UtilityExecutor& executor_;
    app::UtilityExecutor::Route route_;
};
class RecordedSetup final : public u::SetupProcess {
public:
    explicit RecordedSetup(std::shared_ptr<std::optional<int>> code) : code_(std::move(code)) {}
    std::optional<int> exitCode() const override { return *code_; }
private:
    std::shared_ptr<std::optional<int>> code_;
};
void writeText(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("Coverage fixture could not be written");
}
std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

class UpdatesArea final : public app::ModuleOwner, public app::UpdateService {
public:
    UpdatesArea(const app::ApplicationModuleContext& c, bool coverage)
        : host_(*c.host), mode_(c.mode), window_(static_cast<HWND>(c.window)), clock_(c.clock), coverageRoot_(c.coverageRoot) {
        message_ = host_.registerMessage();
        executor_ = std::make_unique<ExecutorRoute>(*c.utility);
        u::UpdaterOptions o;
        o.current = buildVersion();
        u::UpdaterServices s;
        if (coverage) {
            o.folder = c.coverageRoot / "Updates";
            o.automaticAllowed = o.installAllowed = automaticAllowed_ = true;
            auto routes = std::make_unique<u::RouteTransport>();
            routes->setClock(c.clock);
            routes_ = routes.get();
            transport_ = std::move(routes);
            // A current-release baseline answers the daily check of an opening.
            routes_->serve(std::string(u::releasesEndpoint), {200, {}, u::releaseFeed({{"windows-v1.2.0", "EndfieldHUD 1.2.0", "", false, false, true, {}}}), {}});
            s.wallClock = [this] { return coverageEpoch + clock_() + wallOffset_; };
            s.installedCopy = [] { return true; };
            s.verify = [](const fs::path& p, const std::string& sha, const packaging::ProductVersion& v) { u::verifySetup(p, sha, v, u::runningExecutable()); };
            s.hold = [](const fs::path& p, const std::string& sha) { return u::holdSetup(p, sha); };
            s.launch = [this](const fs::path& p, const std::vector<std::string>& a) -> std::unique_ptr<u::SetupProcess> {
                launches_.emplace_back(p, a);
                return std::make_unique<RecordedSetup>(setupExit_);
            };
            s.overlayVisible = [this] { return coverageOverlay_; };
            s.closeOverlay = [this] { ++closeRequests_; };
        } else {
            o.folder = c.dataRoot / "Updates";
            // An isolated data root (section 9 gate) never checks on its own and never installs.
            o.automaticAllowed = o.installAllowed = automaticAllowed_ = !host_.isolatedDataRoot();
            u::WinHttpTransport::Options t;
            t.notify = [window = window_, message = message_] { PostMessageW(window, message, transportNotice, 0); };
            t.now = c.clock;
            transport_ = std::make_unique<u::WinHttpTransport>(std::move(t));
            s.wallClock = [] { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); };
            s.installedCopy = [current = o.current] { return u::installedCopy(u::runningExecutable(), current, u::productionUninstallKey); };
            s.verify = [](const fs::path& p, const std::string& sha, const packaging::ProductVersion& v) { u::verifySetup(p, sha, v, u::runningExecutable()); };
            s.hold = [](const fs::path& p, const std::string& sha) { return u::holdSetup(p, sha); };
            s.launch = [window = window_, message = message_](const fs::path& p, const std::vector<std::string>& a) {
                return u::launchSetup(p, a, window, message, setupNotice);
            };
            s.overlayVisible = [this] { return host_.overlayVisible(); };
            s.closeOverlay = [this] { host_.closeOverlay(); };
        }
        s.transport = transport_.get();
        s.submit = [route = executor_.get()](std::function<void()> work, std::function<void(std::exception_ptr)> done) {
            return route->submit(std::move(work), std::move(done));
        };
        s.clock = c.clock;
        s.changed = [this] {
            ++publications_;
            if (observer_) observer_();
        };
        folder_ = o.folder;
        updater_ = std::make_unique<u::Updater>(std::move(o), std::move(s));
        host_.publish<app::UpdateService>(app::UpdateService::key, *this);
        updater_->start(clock_());
    }
    ~UpdatesArea() override {
        host_.withdraw(app::UpdateService::key);
        updater_->stop();
        updater_.reset();
        transport_.reset();
        executor_.reset();
    }
    UpdatesArea(const UpdatesArea&) = delete;
    UpdatesArea& operator=(const UpdatesArea&) = delete;

    std::string_view name() const noexcept override { return "windowsUpdates"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;
        r.message = 42;
        return r;
    }
    void setOverlayVisible(bool visible, double time) override { if (visible) updater_->overlayOpened(time); }
    // A quit can still be cancelled (a failed save keeps the app running), so
    // `quitting` stops nothing; requests end with the area.
    bool hostEvent(const app::HostEvent& e, double time) override {
        if (e.kind == app::HostEvent::Kind::overlayConcealed) updater_->overlayConcealed(time);
        return false;
    }
    bool message(const app::NativeMessage& m, double time) override {
        if (m.message != message_) return false;
        if (m.wParam == transportNotice) updater_->drain();
        else if (m.wParam == setupNotice) updater_->setupExited(time);
        return true;
    }
    std::optional<double> nextWakeTime(double now) const override { return updater_->nextWakeTime(now); }
    bool deadline(double time) override { (void)updater_->deadline(time); return false; }
    void coverage(app::ModuleCoverage&) override;

    // UpdateService
    bool check(double time) override { return updater_->check(time); }
    std::optional<std::string> activate(double time) override { return updater_->activate(time); }
    void setAutomaticChecks(bool enabled, double time) override { updater_->setAutomaticChecks(enabled, time); }
    modules::SettingsUpdateStatus settingsStatus(core::Language l) const override {
        const auto& st = updater_->state();
        modules::SettingsUpdateStatus s;
        s.title = u::phaseTitle(st.phase, l);
        s.detail = u::detailText(st.detail, l);
        s.latestVersion = st.latestVersion;
        s.releaseURL = st.releaseURL.empty() ? std::string(releasesPage) : st.releaseURL;
        s.automaticallyInstalls = st.automaticChecks;
        s.canCheck = st.phase != u::Phase::installing;
        s.canSetAutomatic = automaticAllowed_ && updater_->loaded();
        return s;
    }
    std::string automaticTitle(core::Language l) const override { return u::automaticChecksTitle(l); }
    std::string trayTitle(core::Language l) const override { return u::trayTitle(updater_->state(), l); }
    std::string trayHelp(core::Language l) const override {
        auto detail = u::detailText(updater_->state().detail, l);
        return detail.empty() ? u::phaseTitle(updater_->state().phase, l) : detail;
    }
    bool hasUpdate() const override { return updater_->state().hasUpdate(); }
    void setObserver(std::function<void()> observer) override { observer_ = std::move(observer); }

private:
    app::ApplicationHost& host_;
    const ApplicationAreaMode mode_;
    HWND window_{};
    std::function<double()> clock_;
    fs::path coverageRoot_, folder_;
    std::uint32_t message_{};
    std::unique_ptr<ExecutorRoute> executor_;
    std::unique_ptr<u::Transport> transport_;
    u::RouteTransport* routes_{};
    std::unique_ptr<u::Updater> updater_;
    std::function<void()> observer_;
    std::uint64_t publications_{};
    bool automaticAllowed_{};
    // Hidden coverage only.
    double wallOffset_{};
    bool coverageOverlay_{};
    unsigned closeRequests_{};
    std::vector<std::pair<fs::path, std::vector<std::string>>> launches_;
    std::shared_ptr<std::optional<int>> setupExit_ = std::make_shared<std::optional<int>>();
    struct Fixture { fs::path setup; std::string bytes, sha; };
    std::optional<Fixture> fixture_;
    const Fixture& coverageFixture();
};

// A synthetic EndfieldHUD 1.2.1 (19) setup: the synthetic stub, a verified
// payload of two synthetic files, written once into the coverage root.
const UpdatesArea::Fixture& UpdatesArea::coverageFixture() {
    if (fixture_) return *fixture_;
    const auto root = coverageRoot_ / "fixture";
    fs::create_directories(root / "tree");
    const auto stub = u::syntheticSetupStub();
    writeText(root / "stub.exe", std::string_view(reinterpret_cast<const char*>(stub.data()), stub.size()));
    writeText(root / "tree" / "EndfieldHUD.exe", "synthetic EndfieldHUD 1.2.1 coverage image\n");
    writeText(root / "uninstall.exe", "synthetic uninstall coverage image\n");
    auto sources = packaging::collectTree(root / "tree");
    sources.push_back({"uninstall.exe", root / "uninstall.exe"});
    packaging::PackOptions pack;
    pack.version = {1, 2, 1, 19};
    pack.commit = std::string(40, 'c');
    Fixture f;
    f.setup = root / u::setupAssetName("1.2.1", 19);
    packaging::writeSetup(root / "stub.exe", std::move(sources), pack, f.setup);
    f.bytes = readText(f.setup);
    f.sha = packaging::fileSha256Hex(f.setup);
    fixture_ = std::move(f);
    return *fixture_;
}

// Hidden coverage (module-less steps). Cycle 0: discovery among Mac and
// Windows releases, consented download through validated redirects with
// SHA-256 and payload verification, consented install that closes the HUD
// first, the Setup hand-off record and a refused Setup reported after its
// exit. Cycle 1: ETag reuse, the daily check and its preference, Retry-After.
// Cycle 2: a tampered checksum, a hostile redirect and a Mac-only feed.
void UpdatesArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no HUD module
    const auto& expect = step.check;
    const double t = step.time;
    const auto settle = [&] {
        for (int n = 0; n < 12; ++n) {
            step.drain();
            expect(step.message(message_, transportNotice, 0) && !routes_->active(), "Windows updates: the transport notice reaches the area and drains its requests");
            step.drain();
            if (!routes_->active() && updater_->loaded()) return;
        }
    };
    const auto describe = [&] {
        return " (cycle " + std::to_string(step.cycle) + ", phase " + std::string(u::phaseName(updater_->state().phase)) + ", loaded " +
               std::to_string(updater_->loaded()) + ", requests in flight " + std::to_string(routes_->active()) + ")";
    };
    const auto endpoint = std::string(u::releasesEndpoint);
    const auto& st = updater_->state();
    expect(mode_ == ApplicationAreaMode::hiddenCoverage && routes_ && host_.find<app::UpdateService>(app::UpdateService::key) == this,
          "Windows updates: hidden coverage publishes the update service over synthetic routes");
    settle();
    expect(updater_->loaded() && st.installationSupported, "Windows updates: the saved state loads on the shared executor");
    const auto& f = coverageFixture();
    const auto tag = std::string("windows-v1.2.1");
    const auto setupName = u::setupAssetName("1.2.1", 19), sumsName = u::sumsAssetName("1.2.1", 19);
    const auto sums = u::sumsText({{f.sha, setupName}, {std::string(64, '0'), "EndfieldHUD-1.2.1-build19-windows-x64-portable.zip"}});
    std::vector<u::FeedRelease> feed{
        {"v1.3.0", "EndfieldHUD 1.3.0 (macOS)", "", false, false, true,
         {{"EndfieldHUD-1.3.0.dmg", u::releaseDownloadURL("v1.3.0", "EndfieldHUD-1.3.0.dmg"), 9000000, "uploaded"},
          {"appcast.xml", u::releaseDownloadURL("v1.3.0", "appcast.xml"), 2000, "uploaded"}}},
        {"windows-v9.0.0", "draft", "", true, false, true, u::installerAssets("windows-v9.0.0", "9.0.0", 90, 1000, 200)},
        {"windows-v1.4.0-beta.1", "beta", "", false, true, true, u::installerAssets("windows-v1.4.0-beta.1", "1.4.0", 30, 1000, 200)},
        {tag, "EndfieldHUD 1.2.1 for Windows", "", false, false, true, u::installerAssets(tag, "1.2.1", 19, f.bytes.size(), sums.size())},
        {"windows-v1.2.0", "EndfieldHUD 1.2.0", "", false, false, true, u::installerAssets("windows-v1.2.0", "1.2.0", 18, 1000, 200)}};
    const auto sumsURL = u::releaseDownloadURL(tag, sumsName), setupURL = u::releaseDownloadURL(tag, setupName);
    const auto sumsCDN = std::string("https://release-assets.githubusercontent.com/github-production-release-asset/1/sums?sig=coverage");
    const auto setupCDN = std::string("https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/setup?X-Amz-Signature=coverage");
    const auto serveRelease = [&](std::string sumsBody) {
        routes_->clear();
        routes_->serve(endpoint, {200, {{"ETag", "W/\"coverage-" + std::to_string(step.cycle) + "\""}}, u::releaseFeed(feed), {}});
        routes_->serve(sumsURL, {302, {{"Location", sumsCDN}}, {}, {}});
        routes_->serve(sumsCDN, {200, {}, std::move(sumsBody), {}});
        routes_->serve(setupURL, {302, {{"Location", setupCDN}}, {}, {}});
        routes_->serve(setupCDN, {200, {}, f.bytes, {}});
    };
    const auto noFiles = [&] {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(folder_, error)) {
            const auto n = entry.path().filename().u8string();
            if (std::string(n.begin(), n.end()).ends_with(".exe") || std::string(n.begin(), n.end()).ends_with(".partial")) return false;
        }
        return true;
    };
    const auto discover = [&] {
        routes_->forget();
        // HUDUpdateController.checkMetadata: an offered update keeps its phase while checking.
        const bool offered = st.hasUpdate();
        const bool started = check(t);
        expect(started && st.phase == (offered ? u::Phase::available : u::Phase::checking) && routes_->requests().size() == 1,
               "Windows updates: an explicit check starts one GitHub request" + describe());
        settle();
        expect(st.phase == u::Phase::available && updater_->candidate() && updater_->candidate()->tag == tag && st.latestVersion == "1.2.1 (19)" &&
              st.detail.kind == u::Detail::Kind::downloadPrompt && st.releaseURL == "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/" + tag,
              "Windows updates: the newest stable windows-v release is offered; Mac, draft and prerelease releases are not");
    };

    if (step.cycle == 0) {
        serveRelease(sums);
        discover();
        bool windowsOnly = !routes_->requests().empty();
        for (const auto& r : routes_->requests()) windowsOnly = windowsOnly && r.url == endpoint && r.file.empty() &&
            u::findHeader(r.headers, "Accept") == "application/vnd.github+json" && u::findHeader(r.headers, "X-GitHub-Api-Version") == "2022-11-28";
        expect(windowsOnly, "Windows updates: only the GitHub release list is requested (never the Mac appcast or Mac assets)");
        for (const auto l : {core::Language::english, core::Language::simplifiedChinese, core::Language::traditionalChinese, core::Language::japanese, core::Language::korean}) {
            const auto status = settingsStatus(l);
            expect(!status.title.empty() && status.detail.find("1.2.1 (19)") != std::string::npos && status.canCheck && status.canSetAutomatic &&
                  status.latestVersion == "1.2.1 (19)" && !automaticTitle(l).empty() && trayTitle(l).find("1.2.1 (19)") != std::string::npos,
                  "Windows updates: About and tray texts name the update in every language");
        }
        expect(trayTitle(core::Language::english) == "Update available · 1.2.1 (19)…" && hasUpdate(), "Windows updates: the tray title follows the Mac menu");
        // Consent 1: download and verify.
        routes_->forget();
        expect(!activate(t) && st.phase == u::Phase::downloading, "Windows updates: the About row click downloads the installable update");
        settle();
        const auto& requests = routes_->requests();
        expect(requests.size() == 4 && requests[0].url == sumsURL && requests[1].url == sumsCDN && requests[2].url == setupURL && requests[3].url == setupCDN &&
              requests[3].maximumBytes == f.bytes.size() && !requests[3].file.empty(),
              "Windows updates: the checksum list and the setup follow validated GitHub redirects with the published size");
        expect(st.phase == u::Phase::ready && st.detail.kind == u::Detail::Kind::installPrompt && updater_->downloadedSetup() == folder_ / setupName &&
              packaging::fileSha256Hex(updater_->downloadedSetup()) == f.sha,
              "Windows updates: the download matches its SHA256SUMS line and its own verified payload");
        // Consent 2: install. The HUD closes first; Setup starts after concealment.
        coverageOverlay_ = true;
        const auto closes = closeRequests_;
        expect(!activate(t) && st.phase == u::Phase::installing && closeRequests_ == closes + 1 && launches_.empty(),
              "Windows updates: installing closes the HUD before Setup starts");
        coverageOverlay_ = false;
        updater_->overlayConcealed(t); // the host's overlayConcealed broadcast
        settle();
        const auto result = folder_ / u::handoffResultName;
        expect(launches_.size() == 1 && launches_[0].first == folder_ / setupName && launches_[0].second == u::handoffArguments(result),
              "Windows updates: Setup is handed the verified download with an in-place quiet upgrade");
        {
            std::vector<std::string_view> views(launches_[0].second.begin(), launches_[0].second.end());
            const auto parsed = packaging::parseSetupArguments(packaging::SetupRole::install, views);
            expect(parsed.quiet && parsed.open && !parsed.isolated() && parsed.resultFile == launches_[0].second[3] && !parsed.allowDowngrade,
                  "Windows updates: the hand-off command line is a valid Setup command line");
        }
        const auto saved = ehud::data::Json::parse(readText(folder_ / u::stateFileName));
        expect(saved["handoff"]["target"].string() == "1.2.1" && saved["handoff"]["build"].integer() == 19 && saved["handoff"]["from"].string() == EHUD_UPDATE_RELEASE &&
              saved["handoff"]["setup"].string() == setupName, "Windows updates: the hand-off is saved before Setup starts");
        // Setup refuses because EndfieldHUD did not close (a failed save).
        writeText(result, "{\"outcome\":\"refused\",\"refusal\":\"close-failed\",\"error\":\"\",\"previous\":\"1.2.0.18\",\"current\":\"1.2.1.19\"}\n");
        *setupExit_ = 3;
        expect(step.message(message_, setupNotice, 0), "Windows updates: the Setup exit notice routes through the message chain");
        settle();
        expect(st.phase == u::Phase::failed && st.detail.kind == u::Detail::Kind::closeRefused && !updater_->setup(),
              "Windows updates: a refused Setup is reported after its exit");
        expect(ehud::data::Json::parse(readText(folder_ / u::stateFileName))["handoff"].isNull() && noFiles() && !fs::exists(result),
              "Windows updates: the resolved hand-off leaves no download or result behind");
        *setupExit_ = std::nullopt;
        launches_.clear();
    } else if (step.cycle == 1) {
        serveRelease(sums);
        discover();
        expect(updater_->lastCheck() && *updater_->lastCheck() >= coverageEpoch + wallOffset_ && *updater_->lastCheck() <= coverageEpoch + clock_() + wallOffset_,
              "Windows updates: the check time is the synthetic wall clock");
        routes_->queue(endpoint, {304, {}, {}, {}});
        routes_->forget();
        expect(check(t), "Windows updates: a second explicit check runs");
        settle();
        expect(routes_->requests().size() == 1 && u::findHeader(routes_->requests()[0].headers, "If-None-Match") == "W/\"coverage-1\"" &&
              st.phase == u::Phase::available && updater_->candidate()->tag == tag,
              "Windows updates: a 304 reuses the cached release (ETag sent)");
        // At most one automatic check per day, only when the HUD opens.
        routes_->forget();
        updater_->overlayOpened(t);
        settle();
        expect(routes_->requests().empty(), "Windows updates: opening the HUD within a day of the last check sends nothing");
        wallOffset_ += u::dailyInterval + 1;
        updater_->overlayOpened(t);
        settle();
        expect(routes_->requests().size() == 1 && routes_->requests()[0].url == endpoint, "Windows updates: the first opening a day later checks once");
        setAutomaticChecks(false, t);
        settle();
        wallOffset_ += 2 * u::dailyInterval;
        routes_->forget();
        updater_->overlayOpened(t);
        settle();
        expect(routes_->requests().empty() && !st.automaticChecks && !settingsStatus(core::Language::english).automaticallyInstalls &&
              ehud::data::Json::parse(readText(folder_ / u::stateFileName))["automaticChecks"].boolean() == false,
              "Windows updates: the daily check respects its saved preference");
        setAutomaticChecks(true, t);
        settle();
        // GitHub rate limit: Retry-After blocks further requests until it passes.
        routes_->queue(endpoint, {429, {{"Retry-After", "60"}}, {}, {}});
        routes_->forget();
        expect(check(t), "Windows updates: a check is sent");
        settle();
        expect(st.phase == u::Phase::failed && st.detail.kind == u::Detail::Kind::checkError && st.detail.error.kind == u::CheckErrorKind::rateLimited,
              "Windows updates: a 429 reports GitHub's rate limit");
        expect(check(t) && routes_->requests().size() == 1 && st.detail.error.kind == u::CheckErrorKind::rateLimited,
              "Windows updates: no request is sent inside the Retry-After window");
        wallOffset_ += 61;
        expect(check(t), "Windows updates: a check after Retry-After is sent");
        settle();
        expect(routes_->requests().size() == 2 && st.phase == u::Phase::available, "Windows updates: requests resume after Retry-After");
    } else {
        // A checksum that does not match the download.
        serveRelease(u::sumsText({{std::string(64, 'a'), setupName}}));
        discover();
        expect(!activate(t), "Windows updates: the download starts");
        settle();
        expect(st.phase == u::Phase::failed && st.detail.kind == u::Detail::Kind::verificationFailed && noFiles(),
              "Windows updates: a download that does not match SHA256SUMS is rejected and deleted");
        // A redirect away from GitHub is never followed.
        serveRelease(sums);
        routes_->serve(setupURL, {302, {{"Location", "https://downloads.example.invalid/EndfieldHUD-setup.exe"}}, {}, {}});
        discover();
        routes_->forget();
        expect(!activate(t), "Windows updates: the download starts");
        settle();
        bool contained = true;
        for (const auto& r : routes_->requests()) contained = contained && r.url.find("example.invalid") == std::string::npos;
        expect(st.phase == u::Phase::failed && st.detail.kind == u::Detail::Kind::downloadFailed && contained && noFiles(),
              "Windows updates: a redirect outside GitHub fails the download without a request");
        // A feed with Mac releases only offers nothing to Windows.
        routes_->clear();
        routes_->serve(endpoint, {200, {}, u::releaseFeed({feed[0]}), {}});
        expect(check(t), "Windows updates: a check is sent");
        settle();
        expect(st.phase == u::Phase::failed && st.detail.error.kind == u::CheckErrorKind::noPublishedReleases &&
              settingsStatus(core::Language::english).detail == "No published release found on GitHub.",
              "Windows updates: Mac releases are never offered to Windows");
        routes_->clear();
        routes_->serve(endpoint, {200, {}, u::releaseFeed({{"windows-v1.2.0", "EndfieldHUD 1.2.0", "", false, false, true, {}}}), {}});
        expect(check(t), "Windows updates: a check is sent");
        settle();
        expect(st.phase == u::Phase::current && st.detail.kind == u::Detail::Kind::none && st.latestVersion == "1.2.0",
              "Windows updates: the running release reports up to date");
    }
    expect(publications_ > 0 && !updater_->nextWakeTime(t), "Windows updates: changes are published and no request is left waiting");
}
} // namespace

std::unique_ptr<app::ModuleOwner> windowsUpdatesModule(const app::ApplicationModuleContext& context) {
    if (!context.host || !context.utility || !context.window || !context.clock) throw std::invalid_argument("The updates area needs the shared host, executor, window and clock");
    switch (context.mode) {
    case ApplicationAreaMode::production: return std::make_unique<UpdatesArea>(context, false);
    case ApplicationAreaMode::hiddenCoverage: return std::make_unique<UpdatesArea>(context, true);
    default: return nullptr; // development previews never contact GitHub
    }
}
} // namespace endfield::areas
