#include "packaging/updater.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace endfield::packaging::updates {
namespace fs = std::filesystem;
using ehud::data::Json;
namespace {
constexpr std::size_t maximumStateBytes = 64 * 1024;
constexpr int maximumRedirects = 5;
constexpr double metadataTimeout = 20, sumsTimeout = 30, setupTimeout = 1800;

// Windows-only text in the five source languages (the Mac catalog has no entry).
std::string text5(core::Language l, std::string_view en, std::string_view hans, std::string_view hant, std::string_view ja, std::string_view ko,
                  std::initializer_list<std::string_view> arguments = {}) {
    std::string_view value = en;
    switch (l) {
    case core::Language::simplifiedChinese: value = hans; break;
    case core::Language::traditionalChinese: value = hant; break;
    case core::Language::japanese: value = ja; break;
    case core::Language::korean: value = ko; break;
    default: break;
    }
    const std::vector<std::string_view> args(arguments);
    if (auto rendered = core::renderTranslation(value, args)) return *rendered;
    if (auto rendered = core::renderTranslation(en, args)) return *rendered;
    throw std::invalid_argument("Update text arguments do not match their placeholders");
}
std::string megabytes(std::uint64_t bytes) {
    std::array<char, 32> buffer{};
    const int n = std::snprintf(buffer.data(), buffer.size(), "%.1f MB", static_cast<double>(bytes) / 1048576.0);
    return n > 0 ? std::string(buffer.data(), static_cast<std::size_t>(n)) : std::string("? MB");
}
bool redirect(int status) noexcept { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }
bool setupName(std::string_view name) noexcept {
    return name.starts_with("EndfieldHUD-") && (name.ends_with("-windows-x64-setup.exe") || name.ends_with("-windows-x64-setup.exe.partial"));
}
std::optional<std::string> readSmall(const fs::path& path) {
    try { return ehud::data::detail::readFile(path, maximumStateBytes); }
    catch (...) { return std::nullopt; }
}
void removeQuietly(const fs::path& path) noexcept {
    std::error_code error;
    if (fs::is_regular_file(fs::symlink_status(path, error))) fs::remove(path, error);
}

struct Handoff {
    std::string target, from, setup;
    int build{}, fromBuild{};
    double started{};
};
struct SavedState {
    bool automaticChecks{true};
    std::optional<double> lastCheck;
    std::optional<Handoff> handoff;
    bool newer{};
};
SavedState decodeState(const std::optional<std::string>& bytes) {
    SavedState saved;
    if (!bytes) return saved;
    try {
        const auto json = Json::parse(*bytes, maximumStateBytes);
        if (!json.isObject() || json["format"].string() != stateFormat) return saved;
        if (!json["schema"].isNumber() || json["schema"].integer() != stateSchema) { saved.newer = json["schema"].isNumber() && json["schema"].integer() > stateSchema; return saved; }
        if (json["automaticChecks"].isBool()) saved.automaticChecks = json["automaticChecks"].boolean();
        if (json["lastCheck"].isNumber() && std::isfinite(json["lastCheck"].number())) saved.lastCheck = json["lastCheck"].number();
        const auto& h = json["handoff"];
        if (h.isObject() && h["target"].isString() && h["build"].isNumber() && h["from"].isString() && h["fromBuild"].isNumber() &&
            h["started"].isNumber() && h["setup"].isString() && setupName(h["setup"].string()) && h["setup"].string().find_first_of("/\\") == std::string::npos) {
            Handoff value;
            value.target = h["target"].string();
            value.build = static_cast<int>(h["build"].integer());
            value.from = h["from"].string();
            value.fromBuild = static_cast<int>(h["fromBuild"].integer());
            value.started = h["started"].number();
            value.setup = h["setup"].string();
            saved.handoff = std::move(value);
        }
    } catch (...) {
        return SavedState{};
    }
    return saved;
}
std::string encodeState(bool automatic, const std::optional<double>& lastCheck, const std::optional<Handoff>& handoff) {
    Json::Object root;
    root["format"] = std::string(stateFormat);
    root["schema"] = stateSchema;
    root["automaticChecks"] = automatic;
    root["lastCheck"] = lastCheck ? Json(*lastCheck) : Json(nullptr);
    if (handoff) {
        Json::Object h;
        h["target"] = handoff->target;
        h["build"] = handoff->build;
        h["from"] = handoff->from;
        h["fromBuild"] = handoff->fromBuild;
        h["started"] = handoff->started;
        h["setup"] = handoff->setup;
        root["handoff"] = std::move(h);
    } else {
        root["handoff"] = nullptr;
    }
    return Json(std::move(root)).encode() + "\n";
}
// The Setup result file (packaging/installer.cpp encodeOperationResult).
struct SetupOutcome {
    std::string outcome, refusal, error;
};
std::optional<SetupOutcome> decodeOutcome(const std::optional<std::string>& bytes) {
    if (!bytes) return std::nullopt;
    try {
        const auto json = Json::parse(*bytes, maximumStateBytes);
        if (!json.isObject() || !json["outcome"].isString()) return std::nullopt;
        SetupOutcome o;
        o.outcome = json["outcome"].string();
        if (json["refusal"].isString()) o.refusal = json["refusal"].string();
        if (json["error"].isString()) o.error = json["error"].string().substr(0, 200);
        return o;
    } catch (...) {
        return std::nullopt;
    }
}
bool succeeded(std::string_view outcome) noexcept { return outcome == "installed" || outcome == "upgraded" || outcome == "repaired"; }
std::string setupExitReason(int code) {
    switch (code) {
    case 1: return "failed";
    case 2: return "usage";
    case 3: return "refused";
    case 4: return "cancelled";
    case 5: return "busy";
    case 6: return "damaged";
    case 7: return "incomplete";
    default: return "exit " + std::to_string(code);
    }
}
} // namespace

std::string_view transportFailureName(TransportResponse::Failure f) noexcept {
    switch (f) {
    case TransportResponse::Failure::none: return "none";
    case TransportResponse::Failure::network: return "network";
    case TransportResponse::Failure::cancelled: return "cancelled";
    case TransportResponse::Failure::tooLarge: return "tooLarge";
    case TransportResponse::Failure::timeout: return "timeout";
    case TransportResponse::Failure::file: return "file";
    }
    return "network";
}
std::string_view phaseName(Phase p) noexcept {
    switch (p) {
    case Phase::idle: return "idle";
    case Phase::checking: return "checking";
    case Phase::current: return "current";
    case Phase::available: return "available";
    case Phase::downloading: return "downloading";
    case Phase::ready: return "ready";
    case Phase::installing: return "installing";
    case Phase::failed: return "failed";
    }
    return "idle";
}
bool UpdateState::hasUpdate() const noexcept {
    return phase == Phase::available || phase == Phase::downloading || phase == Phase::ready || phase == Phase::installing;
}
std::string phaseTitle(Phase p, core::Language l) {
    switch (p) {
    case Phase::idle: return core::localized("Not checked yet", "尚未检查", l);
    case Phase::checking: return core::localized("Checking…", "正在检查…", l);
    case Phase::current: return core::localized("Up to date", "已是最新版本", l);
    case Phase::available: return core::localized("Update available", "发现新版本", l);
    case Phase::downloading: return core::localized("Downloading…", "正在下载…", l);
    case Phase::ready: return core::localized("Ready to update", "更新已就绪", l);
    case Phase::installing: return core::localized("Restarting…", "正在重启…", l);
    case Phase::failed: return core::localized("Check failed — retry", "检查失败 · 重试", l);
    }
    return {};
}
std::string detailText(const Detail& d, core::Language l) {
    using K = Detail::Kind;
    switch (d.kind) {
    case K::none: return {};
    case K::checkError:
        // HUDUpdateController.releaseErrorDescription
        switch (d.error.kind) {
        case CheckErrorKind::rateLimited: return core::localized("GitHub is limiting requests. Try again later.", "GitHub 暂时限制请求，请稍后重试。", l);
        case CheckErrorKind::noPublishedReleases: return core::localized("No published release found on GitHub.", "GitHub 上暂无已发布版本。", l);
        case CheckErrorKind::noMatchingReleases: return core::localized("No release is available for this update channel.", "此更新通道暂无已发布版本。", l);
        case CheckErrorKind::invalidCurrentVersion: return core::localized("This build has no valid release version.", "此构建没有有效的版本号。", l);
        default: return core::localized("Could not check GitHub. Check your connection and retry.", "无法检查 GitHub。请检查网络后重试。", l);
        }
    case K::downloadPrompt:
        return text5(l, "Click to download and verify {0} ({1}).", "点击下载并校验 {0}（{1}）。", "點擊下載並驗證 {0}（{1}）。",
                     "クリックして {0}（{1}）をダウンロードし、検証します。", "클릭하여 {0}({1})을(를) 다운로드하고 확인합니다.", {d.value, megabytes(d.bytes)});
    case K::discoverOnly:
        return text5(l, "This copy was not installed by EndfieldHUD Setup. Get {0} from its GitHub release page.",
                     "此副本不是由 EndfieldHUD 安装程序安装的。请从 GitHub 发布页获取 {0}。", "此副本並非由 EndfieldHUD 安裝程式安裝。請從 GitHub 發佈頁取得 {0}。",
                     "このコピーは EndfieldHUD セットアップでインストールされていません。{0} は GitHub のリリースページから入手してください。",
                     "이 사본은 EndfieldHUD 설치 프로그램으로 설치되지 않았습니다. GitHub 릴리스 페이지에서 {0}을(를) 받으세요.", {d.value});
    case K::waitingForPackage:
        return text5(l, "Waiting for a compatible Windows update package.", "正在等待兼容的 Windows 更新包。", "正在等待相容的 Windows 更新套件。",
                     "互換性のある Windows 更新パッケージを待っています。", "호환되는 Windows 업데이트 패키지를 기다리는 중입니다.");
    case K::installPrompt:
        return text5(l, "Click to install. EndfieldHUD closes, updates and reopens.", "点击安装。EndfieldHUD 将关闭、更新并重新打开。",
                     "點擊安裝。EndfieldHUD 將關閉、更新並重新開啟。", "クリックしてインストールします。EndfieldHUD は終了し、更新後に再び開きます。",
                     "클릭하여 설치합니다. EndfieldHUD가 종료되고 업데이트된 후 다시 열립니다.");
    case K::downloadFailed:
        return text5(l, "The update could not be downloaded. Check your connection and retry.", "无法下载更新。请检查网络后重试。",
                     "無法下載更新。請檢查網路後重試。", "アップデートをダウンロードできませんでした。接続を確認してもう一度お試しください。",
                     "업데이트를 다운로드하지 못했습니다. 연결을 확인하고 다시 시도하세요.");
    case K::verificationFailed:
        return text5(l, "The downloaded update did not match its published checksum and was deleted.", "下载的更新与发布的校验和不符，已删除。",
                     "下載的更新與發佈的校驗和不符，已刪除。", "ダウンロードしたアップデートが公開されたチェックサムと一致しなかったため、削除しました。",
                     "다운로드한 업데이트가 게시된 체크섬과 일치하지 않아 삭제했습니다.");
    case K::launchFailed:
        return text5(l, "Setup could not be started.", "无法启动安装程序。", "無法啟動安裝程式。", "セットアップを開始できませんでした。", "설치 프로그램을 시작하지 못했습니다.");
    case K::setupFailed:
        return text5(l, "Setup could not update EndfieldHUD ({0}). EndfieldHUD was left unchanged.", "安装程序无法更新 EndfieldHUD（{0}）。EndfieldHUD 未作更改。",
                     "安裝程式無法更新 EndfieldHUD（{0}）。EndfieldHUD 未作變更。", "セットアップで EndfieldHUD を更新できませんでした（{0}）。EndfieldHUD は変更されていません。",
                     "설치 프로그램이 EndfieldHUD를 업데이트하지 못했습니다({0}). EndfieldHUD는 변경되지 않았습니다.", {d.value});
    case K::closeRefused:
        return text5(l, "EndfieldHUD could not close for the update. Finish your edits and retry.", "EndfieldHUD 无法关闭以进行更新。请完成编辑后重试。",
                     "EndfieldHUD 無法關閉以進行更新。請完成編輯後重試。", "更新のために EndfieldHUD を終了できませんでした。編集を終えてからもう一度お試しください。",
                     "업데이트를 위해 EndfieldHUD를 종료하지 못했습니다. 편집을 마친 후 다시 시도하세요.");
    case K::updated:
        return text5(l, "Updated to {0}.", "已更新到 {0}。", "已更新至 {0}。", "{0} に更新しました。", "{0}(으)로 업데이트했습니다.", {d.value});
    case K::interrupted:
        return text5(l, "The last update did not finish. EndfieldHUD was left unchanged.", "上次更新未完成。EndfieldHUD 未作更改。",
                     "上次更新未完成。EndfieldHUD 未作變更。", "前回のアップデートは完了しませんでした。EndfieldHUD は変更されていません。",
                     "지난 업데이트가 완료되지 않았습니다. EndfieldHUD는 변경되지 않았습니다.");
    }
    return {};
}
std::string trayTitle(const UpdateState& s, core::Language l) {
    if (s.hasUpdate()) {
        const auto& version = !s.updateVersion.empty() ? s.updateVersion : s.latestVersion;
        return core::localized("Update available", "发现新版本", l) + (version.empty() ? std::string() : " · " + version) + "…";
    }
    return s.phase == Phase::checking ? core::localized("Checking for updates…", "正在检查更新…", l) : core::localized("Check for Updates…", "检查更新…", l);
}
std::string automaticChecksTitle(core::Language l) {
    return text5(l, "Check for updates daily", "每天检查更新", "每天檢查更新", "毎日アップデートを確認", "매일 업데이트 확인");
}
std::string releaseDisplay(const Release& r) {
    std::string_view tag = r.tag;
    if (tag.starts_with(windowsTagPrefix)) tag.remove_prefix(windowsTagPrefix.size());
    else if (tag.starts_with('v')) tag.remove_prefix(1);
    return std::string(tag) + (r.install ? " (" + std::to_string(r.install->build) + ")" : std::string());
}
std::vector<std::string> handoffArguments(const fs::path& resultFile) {
    // Quiet in-place upgrade: Setup closes EndfieldHUD through the session-end
    // path, restarts it (or starts it, --open) and records its outcome.
    const auto text = resultFile.u8string();
    return {"--quiet", "--open", "--result", std::string(text.begin(), text.end())};
}

// ---------------------------------------------------------------------------------
struct Updater::Impl {
    Updater& self;
    UpdaterOptions options;
    UpdaterServices services;
    ReleaseClient client;
    std::string currentTag;
    std::shared_ptr<int> token = std::make_shared<int>(0);
    bool stopped{}, loading{}, checking{}, pendingInstall{}, writing{}, writeDirty{}, readOnly{}, resolving{};
    bool checkAfterLoad{}, openedBeforeLoad{};
    std::uint64_t request{};
    std::optional<Handoff> handoff;
    bool handoffUnresolved{};
    std::string expectedSha;
    fs::path downloaded;

    Impl(Updater& u, UpdaterOptions o, UpdaterServices s)
        : self(u), options(std::move(o)), services(std::move(s)), client(TagNamespace::windows, options.endpoint) {
        if (!services.transport || !services.submit || !services.clock || !services.wallClock)
            throw std::invalid_argument("The updater needs a transport, the shared executor and both clocks");
        if (options.folder.empty() || !options.folder.is_absolute()) throw std::invalid_argument("The updater folder must be absolute");
        currentTag = windowsReleaseTag(options.current.release());
    }
    fs::path statePath() const { return options.folder / stateFileName; }
    fs::path resultPath() const { return options.folder / handoffResultName; }
    void publish() { if (services.changed) services.changed(); }
    // Executor job whose completion runs only while this updater lives.
    template <class Work, class Done> bool submit(Work work, Done done) {
        std::weak_ptr<int> alive = token;
        return services.submit(std::move(work), [alive, done = std::move(done)](std::exception_ptr error) mutable {
            if (alive.lock()) done(error);
        });
    }
    template <class Done> void transport(TransportRequest request, Done done) {
        std::weak_ptr<int> alive = token;
        ++self.requests_;
        this->request = services.transport->start(std::move(request), [this, alive, done = std::move(done)](TransportResponse r) mutable {
            if (!alive.lock()) return;
            this->request = 0;
            if (stopped) return;
            done(std::move(r));
        });
    }

    // --- Persistence ---------------------------------------------------------------
    void persist() {
        // Never before the saved record was read (it may hold a hand-off).
        if (readOnly || !self.loaded_) return;
        if (writing) { writeDirty = true; return; }
        writing = true;
        auto bytes = encodeState(self.state_.automaticChecks, self.lastCheck_, handoff);
        auto path = statePath();
        const bool accepted = submit([path, bytes] {
            fs::create_directories(path.parent_path());
            ehud::data::detail::replaceFile(path, ehud::data::detail::readFile(path, maximumStateBytes), bytes, maximumStateBytes);
        }, [this](std::exception_ptr) {
            writing = false;
            if (writeDirty) { writeDirty = false; persist(); }
        });
        if (!accepted) { writing = false; writeDirty = true; }
    }
    void retryWrites() { if (writeDirty && !writing) { writeDirty = false; persist(); } }
    void cleanup(std::vector<std::string> names) {
        auto folder = options.folder;
        (void)submit([folder, names] { for (const auto& n : names) removeQuietly(folder / n); }, [](std::exception_ptr) {});
    }

    // --- Start and hand-off resolution ------------------------------------------------
    struct Loaded { std::optional<std::string> bytes, result; bool installed{}; };
    void load(double) {
        if (loading || self.loaded_ || stopped) return;
        loading = true;
        auto out = std::make_shared<Loaded>();
        auto folder = options.folder;
        const bool allowed = options.installAllowed;
        auto installed = services.installedCopy;
        const bool accepted = submit([out, folder, allowed, installed] {
            fs::create_directories(folder);
            out->bytes = readSmall(folder / stateFileName);
            const auto saved = decodeState(out->bytes);
            std::error_code error;
            // Bounded folder: a partial or unreferenced download never survives a run.
            for (const auto& entry : fs::directory_iterator(folder, error)) {
                const auto name = entry.path().filename().u8string();
                const std::string text(name.begin(), name.end());
                if (!setupName(text)) continue;
                if (saved.handoff && text == saved.handoff->setup) continue;
                removeQuietly(entry.path());
            }
            if (saved.handoff) out->result = readSmall(folder / handoffResultName);
            else removeQuietly(folder / handoffResultName);
            out->installed = allowed && installed && installed();
        }, [this, out](std::exception_ptr error) {
            loading = false;
            self.loaded_ = true;
            if (error) out->bytes.reset();
            const auto saved = decodeState(out->bytes);
            readOnly = saved.newer;
            self.state_.automaticChecks = saved.automaticChecks;
            self.lastCheck_ = saved.lastCheck;
            handoff = saved.handoff;
            self.state_.installationSupported = !error && out->installed;
            resolve(out->result, std::nullopt);
            publish();
            // Requests that arrived while the record was loading.
            const bool explicitCheck = std::exchange(checkAfterLoad, false);
            const bool opened = std::exchange(openedBeforeLoad, false) && services.overlayVisible && services.overlayVisible();
            if (explicitCheck) (void)check(services.clock());
            else if (opened) self.overlayOpened(services.clock());
        });
        if (!accepted) loading = false;
    }
    // Outcome of a hand-off: this run's version, Setup's result file, or its
    // exit code while this run lives (then `exitCode`).
    void resolve(const std::optional<std::string>& resultBytes, std::optional<int> exitCode) {
        handoffUnresolved = false;
        if (!handoff) return;
        auto& s = self.state_;
        const auto target = handoff->target + " (" + std::to_string(handoff->build) + ")";
        const bool running = options.current.release() == handoff->target && options.current.build == handoff->build;
        const auto outcome = decodeOutcome(resultBytes);
        if (running || (outcome && succeeded(outcome->outcome))) {
            s.phase = Phase::current;
            s.detail = {Detail::Kind::updated, {}, target, 0};
            s.latestVersion = target;
            s.updateVersion.clear();
        } else if (outcome) {
            s.phase = Phase::failed;
            if (outcome->refusal == "close-failed") s.detail = {Detail::Kind::closeRefused, {}, {}, 0};
            else s.detail = {Detail::Kind::setupFailed, {}, outcome->outcome + (outcome->refusal.empty() || outcome->refusal == "none" ? std::string() : ": " + outcome->refusal), 0};
        } else if (exitCode) {
            s.phase = *exitCode == 0 ? Phase::current : Phase::failed;
            s.detail = *exitCode == 0 ? Detail{Detail::Kind::updated, {}, target, 0} : Detail{Detail::Kind::setupFailed, {}, setupExitReason(*exitCode), 0};
        } else {
            const double now = services.wallClock();
            if (now - handoff->started <= handoffPatience && handoff->started <= now + 60) { handoffUnresolved = true; return; }
            s.phase = Phase::failed;
            s.detail = {Detail::Kind::interrupted, {}, {}, 0};
        }
        cleanup({handoff->setup, std::string(handoffResultName)});
        handoff.reset();
        downloaded.clear();
        self.candidate_.reset();
        persist();
    }
    void reresolve() {
        if (!handoff || !handoffUnresolved || resolving) return;
        resolving = true;
        auto out = std::make_shared<std::optional<std::string>>();
        auto path = resultPath();
        if (!submit([out, path] { *out = readSmall(path); }, [this, out](std::exception_ptr) {
                resolving = false;
                resolve(*out, std::nullopt);
                publish();
            }))
            resolving = false;
    }

    // --- Metadata check -----------------------------------------------------------------
    bool busy() const {
        const auto p = self.state_.phase;
        return checking || p == Phase::downloading || p == Phase::ready || p == Phase::installing;
    }
    bool check(double) {
        if (stopped || !self.loaded_ || busy()) return false;
        auto begun = client.begin(currentTag, options.current.build, services.wallClock());
        if (auto* immediate = std::get_if<CheckResult>(&begun)) { apply(*immediate); return true; }
        checking = true;
        if (!self.state_.hasUpdate()) self.state_.phase = Phase::checking;
        self.lastCheck_ = services.wallClock();
        persist();
        publish();
        TransportRequest r;
        r.url = options.endpoint;
        r.headers = std::move(std::get<Headers>(begun));
        r.maximumBytes = maximumResponseBytes;
        r.timeout = metadataTimeout;
        transport(std::move(r), [this](TransportResponse response) {
            checking = false;
            CheckResult result;
            switch (response.failure) {
            case TransportResponse::Failure::none: result = client.finish(response.response, services.wallClock()); break;
            case TransportResponse::Failure::tooLarge: result.kind = CheckResult::Kind::failed; result.error = {CheckErrorKind::responseTooLarge, 0, {}}; break;
            case TransportResponse::Failure::cancelled: result = client.fail(CheckErrorKind::cancelled); break;
            default: result = client.fail(CheckErrorKind::network); break;
            }
            apply(result);
        });
        return true;
    }
    void apply(const CheckResult& result) {
        auto& s = self.state_;
        s.detail = {};
        switch (result.kind) {
        case CheckResult::Kind::current:
            s.latestVersion = releaseDisplay(*result.release);
            s.releaseURL = result.release->url;
            s.updateVersion.clear();
            s.phase = Phase::current;
            self.candidate_.reset();
            break;
        case CheckResult::Kind::available: {
            const auto& r = *result.release;
            s.latestVersion = s.updateVersion = releaseDisplay(r);
            s.releaseURL = r.url;
            s.phase = Phase::available;
            self.candidate_ = r;
            if (!s.installationSupported) s.detail = {Detail::Kind::discoverOnly, {}, s.updateVersion, 0};
            else if (!r.install) s.detail = {Detail::Kind::waitingForPackage, {}, {}, 0};
            else s.detail = {Detail::Kind::downloadPrompt, {}, s.updateVersion, r.install->setup.bytes};
            break;
        }
        case CheckResult::Kind::failed:
            s.phase = Phase::failed;
            s.detail = {Detail::Kind::checkError, result.error, {}, 0};
            break;
        }
        publish();
    }

    // --- Download (consent) ----------------------------------------------------------------
    void fetch(std::string url, fs::path file, std::uint64_t maximum, double timeout, int hop, std::function<void(TransportResponse)> done) {
        TransportRequest r;
        r.url = std::move(url);
        r.headers = {{"Accept", "application/octet-stream"}, {"User-Agent", std::string(releaseUserAgent)}};
        r.maximumBytes = maximum;
        r.file = file;
        r.timeout = timeout;
        transport(std::move(r), [this, file, maximum, timeout, hop, done = std::move(done)](TransportResponse response) mutable {
            if (response.failure == TransportResponse::Failure::none && redirect(response.response.status)) {
                const auto location = findHeader(response.response.headers, "Location");
                const auto next = location ? validatedDownloadLocation(*location) : std::nullopt;
                if (!next || hop >= maximumRedirects) { response.failure = TransportResponse::Failure::network; done(std::move(response)); return; }
                fetch(*next, file, maximum, timeout, hop + 1, std::move(done));
                return;
            }
            done(std::move(response));
        });
    }
    void failDownload(Detail::Kind kind) {
        self.state_.phase = Phase::failed;
        self.state_.detail = {kind, {}, {}, 0};
        downloaded.clear();
        publish();
    }
    bool download(double) {
        auto& s = self.state_;
        // Never while an earlier hand-off may still be running Setup.
        if (stopped || handoff || s.phase != Phase::available || !self.candidate_ || !self.candidate_->install || !s.installationSupported) return false;
        s.phase = Phase::downloading;
        s.detail = {};
        publish();
        const auto release = *self.candidate_;
        fetch(release.install->sums.url, {}, maximumSumsBytes, sumsTimeout, 0, [this, release](TransportResponse sums) {
            if (sums.failure != TransportResponse::Failure::none || sums.response.status != 200) { failDownload(Detail::Kind::downloadFailed); return; }
            const auto& setup = release.install->setup;
            auto digest = checksumFor(sums.response.body, setup.name);
            if (!digest) { failDownload(Detail::Kind::verificationFailed); return; }
            expectedSha = *digest;
            const auto partial = options.folder / (setup.name + ".partial");
            fetch(setup.url, partial, setup.bytes, setupTimeout, 0, [this, release, partial](TransportResponse body) {
                const auto& setup = release.install->setup;
                if (body.failure != TransportResponse::Failure::none || body.response.status != 200 || body.bytes != setup.bytes) {
                    cleanup({setup.name + ".partial"});
                    failDownload(Detail::Kind::downloadFailed);
                    return;
                }
                verifyDownload(release, partial);
            });
        });
        return true;
    }
    void verifyDownload(const Release& release, const fs::path& partial) {
        const auto final = options.folder / release.install->setup.name;
        auto verify = services.verify;
        const auto sha = expectedSha;
        ProductVersion expected;
        {
            const auto& v = release.version;
            const auto number = [](const std::string& text) { int value{}; std::from_chars(text.data(), text.data() + text.size(), value); return value; };
            expected = {number(v.major), number(v.minor), number(v.patch), release.install->build};
        }
        const bool accepted = submit([verify, partial, final, sha, expected] {
            try {
                if (!verify) throw std::runtime_error("No setup verifier");
                verify(partial, sha, expected);
                removeQuietly(final);
                fs::rename(partial, final);
            } catch (...) {
                removeQuietly(partial);
                throw;
            }
        }, [this, final](std::exception_ptr error) {
            if (stopped) return;
            if (error) { failDownload(Detail::Kind::verificationFailed); return; }
            downloaded = final;
            self.state_.phase = Phase::ready;
            self.state_.detail = {Detail::Kind::installPrompt, {}, {}, 0};
            publish();
        });
        if (!accepted) { cleanup({release.install->setup.name + ".partial"}); failDownload(Detail::Kind::downloadFailed); }
    }

    // --- Install (consent) --------------------------------------------------------------------
    bool install(double) {
        if (stopped || handoff || self.state_.phase != Phase::ready || downloaded.empty() || !self.candidate_ || !self.candidate_->install) return false;
        self.state_.phase = Phase::installing;
        self.state_.detail = {};
        publish();
        // Mac prepareForRestart: the HUD closes before installation begins.
        if (services.overlayVisible && services.overlayVisible()) {
            pendingInstall = true;
            if (services.closeOverlay) services.closeOverlay();
            return true;
        }
        proceed();
        return true;
    }
    void proceed() {
        const auto& r = *self.candidate_;
        Handoff h;
        h.target = r.version.major + "." + r.version.minor + "." + r.version.patch;
        h.build = r.install->build;
        h.from = options.current.release();
        h.fromBuild = options.current.build;
        h.started = services.wallClock();
        h.setup = r.install->setup.name;
        handoff = h;
        const auto bytes = encodeState(self.state_.automaticChecks, self.lastCheck_, handoff);
        auto hold = services.hold;
        auto lock = std::make_shared<std::shared_ptr<void>>();
        const auto setup = downloaded, state = statePath(), result = resultPath();
        const auto sha = expectedSha;
        const bool accepted = submit([hold, lock, setup, state, result, bytes, sha] {
            if (!hold) throw std::runtime_error("No setup hold");
            *lock = hold(setup, sha);
            removeQuietly(result);
            ehud::data::detail::replaceFile(state, ehud::data::detail::readFile(state, maximumStateBytes), bytes, maximumStateBytes);
        }, [this, lock, setup, result](std::exception_ptr error) {
            if (stopped) return;
            if (error) { abandon(Detail::Kind::verificationFailed); return; }
            try {
                if (!services.launch) throw std::runtime_error("No setup launcher");
                self.setup_ = services.launch(setup, handoffArguments(result));
            } catch (...) {
                lock->reset();
                abandon(Detail::Kind::launchFailed);
                return;
            }
            lock->reset(); // Setup holds its own image open from here on
        });
        if (!accepted) abandon(Detail::Kind::launchFailed);
    }
    void abandon(Detail::Kind kind) {
        if (handoff) cleanup({handoff->setup});
        handoff.reset();
        persist();
        self.candidate_.reset();
        failDownload(kind);
    }
    void setupExited() {
        if (!self.setup_) return;
        const auto code = self.setup_->exitCode();
        if (!code) return;
        self.setup_.reset();
        auto out = std::make_shared<std::optional<std::string>>();
        auto path = resultPath();
        const int exit = *code;
        if (!submit([out, path] { *out = readSmall(path); }, [this, out, exit](std::exception_ptr) {
                resolve(*out, exit);
                publish();
            })) {
            resolve(std::nullopt, exit);
            publish();
        }
    }
};

Updater::Updater(UpdaterOptions options, UpdaterServices services) : impl_(std::make_unique<Impl>(*this, std::move(options), std::move(services))) {}
Updater::~Updater() { stop(); }
void Updater::start(double time) { impl_->load(time); }
void Updater::stop() noexcept {
    auto& i = *impl_;
    i.stopped = true;
    i.pendingInstall = false;
    if (i.request) { i.services.transport->cancel(i.request); i.request = 0; }
}
bool Updater::check(double time) {
    impl_->retryWrites();
    if (!loaded_) { impl_->checkAfterLoad = !impl_->stopped; impl_->load(time); return false; }
    return impl_->check(time);
}
std::optional<std::string> Updater::activate(double time) {
    switch (state_.phase) {
    case Phase::available:
        if (candidate_ && candidate_->install && state_.installationSupported) { (void)download(time); return std::nullopt; }
        // A copy that cannot install itself: the release page (host opens it).
        return state_.releaseURL.empty() ? std::nullopt : std::optional<std::string>(state_.releaseURL);
    case Phase::ready: (void)install(time); return std::nullopt;
    case Phase::checking:
    case Phase::downloading:
    case Phase::installing: return std::nullopt;
    default: (void)check(time); return std::nullopt;
    }
}
bool Updater::download(double time) { return impl_->download(time); }
bool Updater::install(double time) { return impl_->install(time); }
void Updater::setAutomaticChecks(bool value, double) {
    if (impl_->stopped || !loaded_ || impl_->readOnly || !impl_->options.automaticAllowed || state_.automaticChecks == value) return;
    state_.automaticChecks = value;
    impl_->persist();
    impl_->publish();
}
void Updater::overlayOpened(double time) {
    auto& i = *impl_;
    i.retryWrites();
    if (!loaded_) { i.openedBeforeLoad = !i.stopped; i.load(time); return; }
    if (i.handoffUnresolved) { i.reresolve(); return; }
    if (!i.options.automaticAllowed || !state_.automaticChecks || i.stopped || i.busy() || i.handoff) return;
    const double now = i.services.wallClock();
    const bool due = !lastCheck_ || now - *lastCheck_ >= dailyInterval || *lastCheck_ > now + 3600;
    if (due) (void)i.check(time);
}
void Updater::overlayConcealed(double) {
    auto& i = *impl_;
    if (!i.pendingInstall || i.stopped) return;
    i.pendingInstall = false;
    i.proceed();
}
void Updater::setupExited(double) { impl_->setupExited(); }
std::size_t Updater::drain() { return impl_->services.transport->drain(); }
std::optional<double> Updater::nextWakeTime(double) const { return impl_->services.transport->nextDeadline(); }
bool Updater::deadline(double now) {
    const bool expired = impl_->services.transport->deadline(now);
    if (expired) impl_->services.transport->drain();
    return expired;
}
fs::path Updater::downloadedSetup() const { return state_.phase == Phase::ready ? impl_->downloaded : fs::path{}; }
} // namespace endfield::packaging::updates
