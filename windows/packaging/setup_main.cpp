// EndfieldHUD-Setup.exe (EHUD_SETUP_ROLE_INSTALL) and uninstall.exe
// (EHUD_SETUP_ROLE_UNINSTALL): one small native program, no third-party
// installer framework. The interface is a Windows task dialog (progress,
// launch-at-login choice, explicit data-removal consent); --quiet runs
// without any window. See packaging/installer.hpp for what is installed.
//
// uninstall.exe cannot delete its own running image, so when it runs from the
// install folder it copies itself to %TEMP%\EndfieldHUD-Uninstall-<id>\ and
// waits for that copy (--worker), which moves the waiting image out of the
// install folder before removing it. Stale worker folders are removed by the
// next Setup or uninstall run.
#include "packaging/installer.hpp"
#include "packaging/package_io.hpp"
#include "packaging/setup_arguments.hpp"
#include "packaging/setup_text.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#if !defined(EHUD_SETUP_ROLE_INSTALL) && !defined(EHUD_SETUP_ROLE_UNINSTALL)
#error "Define EHUD_SETUP_ROLE_INSTALL or EHUD_SETUP_ROLE_UNINSTALL"
#endif

namespace p = endfield::packaging;
namespace core = endfield::core;
namespace fs = std::filesystem;
using p::SetupText;

namespace {
#ifdef EHUD_SETUP_ROLE_INSTALL
constexpr p::SetupRole role = p::SetupRole::install;
#else
constexpr p::SetupRole role = p::SetupRole::uninstall;
#endif
constexpr int buttonPrimary = 100, buttonOpen = 101, buttonDeleteData = 102, buttonKeepData = 103, buttonContinue = 104;
constexpr int progressRange = 1000;

fs::path modulePath() {
    std::wstring buffer(32768, L'\0');
    const auto n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size()) throw p::PackageError("The setup program cannot locate itself");
    buffer.resize(n);
    return fs::path(buffer);
}
p::SetupExit exitFor(const p::OperationResult& r) {
    switch (r.outcome) {
    case p::Outcome::installed:
    case p::Outcome::upgraded:
    case p::Outcome::repaired:
    case p::Outcome::uninstalled: return p::SetupExit::success;
    case p::Outcome::refused: return r.refusal == p::Refusal::incomplete ? p::SetupExit::incomplete : p::SetupExit::refused;
    case p::Outcome::cancelled: return p::SetupExit::cancelled;
    case p::Outcome::failed: break;
    }
    return p::SetupExit::failed;
}
void writeResult(const p::SetupArguments& a, const p::OperationResult& r) {
    if (a.resultFile.empty()) return;
    const auto text = p::encodeOperationResult(r);
    const auto path = p::widen(a.resultFile);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written{};
    WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(h);
}
void report(const p::OperationResult& r) {
    std::cout << "EndfieldHUD " << (role == p::SetupRole::install ? "Setup" : "uninstall") << ": " << p::outcomeName(r.outcome);
    if (r.refusal != p::Refusal::none) std::cout << " (" << p::refusalName(r.refusal) << ")";
    if (r.current) std::cout << ' ' << r.current->display();
    std::cout << '\n';
    for (const auto& w : r.warnings) std::cout << "warning: " << w << '\n';
    if (!r.error.empty()) std::cerr << "error: " << r.error << '\n';
    std::cout.flush();
}
// Serializes Setup and uninstall runs for one layout (installer operations
// on the notification identity are not concurrent-safe across processes).
class SetupLock {
public:
    explicit SetupLock(const p::InstallLayout& l) {
        std::wstring name = L"Local\\EndfieldHUD.Setup.{3B0F9C61-7A4E-4D1B-9E25-5C8A2F14D7B3}";
        if (l.isolated()) {
            auto id = l.registryRoot;
            for (auto& c : id) if (c == L'\\') c = L'.';
            name += L"." + id;
        }
        handle_ = CreateMutexW(nullptr, TRUE, name.c_str());
        owned_ = handle_ && GetLastError() != ERROR_ALREADY_EXISTS;
    }
    ~SetupLock() {
        if (handle_) { if (owned_) ReleaseMutex(handle_); CloseHandle(handle_); }
    }
    bool owned() const noexcept { return owned_; }
private:
    HANDLE handle_{};
    bool owned_{};
};

// --- Task dialog pages ----------------------------------------------------------
struct Page {
    std::wstring heading, content, expanded, verification;
    std::vector<std::pair<int, std::wstring>> buttons;
    TASKDIALOG_COMMON_BUTTON_FLAGS common{};
    PCWSTR icon{};
    bool progress{}, verificationChecked{};
    int defaultButton{};
};
class Wizard {
public:
    Wizard(std::wstring title, HINSTANCE instance) : title_(std::move(title)), instance_(instance) {}
    virtual ~Wizard() = default;
    int run(const Page& first) {
        show(first);
        int button{};
        BOOL verification{};
        if (FAILED(TaskDialogIndirect(&config_, &button, nullptr, &verification))) return -1;
        return button;
    }
protected:
    HWND window_{};
    bool verificationChecked_{};
    virtual HRESULT clicked(int button) = 0;      // S_FALSE keeps the dialog open
    virtual void navigated() {}
    void navigate(const Page& page) {
        show(page);
        SendMessageW(window_, TDM_NAVIGATE_PAGE, 0, reinterpret_cast<LPARAM>(&config_));
    }
    void progress(double fraction) {
        const auto position = static_cast<int>(std::clamp(fraction, 0.0, 1.0) * progressRange);
        if (position == lastPosition_) return;
        lastPosition_ = position;
        SendMessageW(window_, TDM_SET_PROGRESS_BAR_POS, static_cast<WPARAM>(position), 0);
    }
    void status(const std::wstring& text) {
        if (text == lastStatus_) return;
        lastStatus_ = text;
        SendMessageW(window_, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(lastStatus_.c_str()));
    }
private:
    std::wstring title_;
    HINSTANCE instance_{};
    Page page_;
    std::vector<TASKDIALOG_BUTTON> buttons_;
    TASKDIALOGCONFIG config_{};
    int lastPosition_{-1};
    std::wstring lastStatus_;
    void show(const Page& page) {
        page_ = page;
        buttons_.clear();
        for (const auto& [id, label] : page_.buttons) buttons_.push_back({id, label.c_str()});
        config_ = {};
        config_.cbSize = sizeof(config_);
        config_.hInstance = instance_;
        config_.dwFlags = TDF_SIZE_TO_CONTENT | TDF_POSITION_RELATIVE_TO_WINDOW | (page_.progress ? TDF_SHOW_PROGRESS_BAR : 0) |
                          (page_.verificationChecked ? TDF_VERIFICATION_FLAG_CHECKED : 0) |
                          ((page_.common & TDCBF_CANCEL_BUTTON) ? TDF_ALLOW_DIALOG_CANCELLATION : 0);
        config_.dwCommonButtons = page_.common;
        config_.pszWindowTitle = title_.c_str();
        config_.pszMainIcon = page_.icon ? page_.icon : MAKEINTRESOURCEW(1);
        config_.pszMainInstruction = page_.heading.c_str();
        config_.pszContent = page_.content.empty() ? nullptr : page_.content.c_str();
        config_.pszExpandedInformation = page_.expanded.empty() ? nullptr : page_.expanded.c_str();
        config_.pszVerificationText = page_.verification.empty() ? nullptr : page_.verification.c_str();
        config_.cButtons = static_cast<UINT>(buttons_.size());
        config_.pButtons = buttons_.empty() ? nullptr : buttons_.data();
        config_.nDefaultButton = page_.defaultButton;
        config_.pfCallback = &Wizard::callback;
        config_.lpCallbackData = reinterpret_cast<LONG_PTR>(this);
        verificationChecked_ = page_.verificationChecked;
        lastPosition_ = -1;
        lastStatus_ = page_.content;
    }
    static HRESULT CALLBACK callback(HWND window, UINT message, WPARAM wParam, LPARAM, LONG_PTR data) {
        auto* self = reinterpret_cast<Wizard*>(data);
        switch (message) {
        case TDN_CREATED:
        case TDN_NAVIGATED:
            self->window_ = window;
            if (self->page_.progress) {
                SendMessageW(window, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, progressRange));
                SendMessageW(window, TDM_ENABLE_BUTTON, IDCLOSE, FALSE);
            }
            self->navigated();
            return S_OK;
        case TDN_VERIFICATION_CLICKED:
            self->verificationChecked_ = wParam != 0;
            return S_OK;
        case TDN_BUTTON_CLICKED:
            return self->clicked(static_cast<int>(wParam));
        default:
            return S_OK;
        }
    }
};
// CommandLineToArgvW quoting: backslashes are literal unless they precede a quote.
[[maybe_unused]] std::wstring commandArgument(std::wstring_view value) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++backslashes; out += c; continue; }
        if (c == L'"') { out.append(backslashes + 1, L'\\'); }
        backslashes = 0;
        out += c;
    }
    out.append(backslashes, L'\\');
    return out + L"\"";
}
std::wstring joined(const std::vector<std::string>& lines) {
    std::wstring out;
    for (const auto& line : lines) { if (!out.empty()) out += L"\n"; out += p::widen(line); }
    return out;
}
std::wstring w(SetupText id, core::Language language) { return p::widen(p::setupText(id, language)); }
std::wstring w(SetupText id, core::Language language, std::initializer_list<std::string_view> arguments) {
    return p::widen(p::setupText(id, language, arguments));
}

#ifdef EHUD_SETUP_ROLE_INSTALL
// --- Install -----------------------------------------------------------------------
class InstallWizard final : public Wizard {
public:
    InstallWizard(const p::SetupPayload& payload, p::InstallPlan plan, const p::SetupArguments& args, core::Language language, HINSTANCE instance)
        : Wizard(w(SetupText::setupTitle, language), instance), payload_(payload), plan_(std::move(plan)), args_(args), language_(language) {}
    ~InstallWizard() override { if (worker_.joinable()) worker_.join(); }
    p::SetupExit start() {
        const auto& version = payload_.manifest().version;
        if (plan_.intended == p::Outcome::refused) {
            result_.outcome = p::Outcome::refused;
            result_.refusal = plan_.refusal;
            run(refusedPage());
            return exitFor(result_);
        }
        Page first;
        const auto root = p::narrow(plan_.layout.installRoot.native());
        if (plan_.intended == p::Outcome::upgraded) {
            first.heading = w(SetupText::upgradeHeading, language_, {version.display()});
            first.content = w(SetupText::upgradeBody, language_, {plan_.installed->version.display(), root, version.display()});
            first.buttons = {{buttonPrimary, w(SetupText::updateButton, language_)}};
        } else if (plan_.intended == p::Outcome::repaired) {
            first.heading = w(SetupText::repairHeading, language_, {version.display()});
            first.content = w(SetupText::repairBody, language_, {root});
            first.buttons = {{buttonPrimary, w(SetupText::repairButton, language_)}};
        } else {
            first.heading = w(SetupText::installHeading, language_, {version.display()});
            first.content = w(SetupText::installBody, language_, {root, p::narrow(plan_.layout.dataBase.native())});
            first.buttons = {{buttonPrimary, w(SetupText::installButton, language_)}};
        }
        first.common = TDCBF_CANCEL_BUTTON;
        first.verification = w(SetupText::launchAtLogin, language_);
        first.verificationChecked = args_.launchAtLogin.value_or(plan_.launchAtLogin);
        first.expanded = joined(plan_.warnings);
        first.defaultButton = buttonPrimary;
        stage_ = Stage::welcome;
        result_.outcome = p::Outcome::cancelled;
        run(first);
        if (worker_.joinable()) worker_.join();
        if (result_.succeeded() && open_ && !result_.restarted) (void)p::launchInstalledApplication(plan_.layout, false);
        return exitFor(result_);
    }
    const p::OperationResult& result() const noexcept { return result_; }
protected:
    HRESULT clicked(int button) override {
        if (stage_ == Stage::welcome && button == buttonPrimary) {
            launchAtLogin_ = verificationChecked_;
            if (!p::runningApplications(plan_.layout, plan_.installed).empty()) {
                stage_ = Stage::running;
                Page running;
                running.heading = w(SetupText::runningHeading, language_);
                running.content = w(SetupText::runningInstallBody, language_);
                running.buttons = {{buttonContinue, w(SetupText::continueButton, language_)}};
                running.common = TDCBF_CANCEL_BUTTON;
                running.defaultButton = buttonContinue;
                navigate(running);
                return S_FALSE;
            }
            beginInstall();
            return S_FALSE;
        }
        if (stage_ == Stage::running && button == buttonContinue) { beginInstall(); return S_FALSE; }
        if (stage_ == Stage::progress) return S_FALSE;
        if (stage_ == Stage::finished && button == buttonOpen) open_ = true;
        return S_OK;
    }
    void navigated() override {
        if (stage_ != Stage::progress || worker_.joinable()) return;
        worker_ = std::thread([this] {
            p::InstallOptions options;
            options.launchAtLogin = launchAtLogin_;
            options.language = language_;
            p::OperationCallbacks callbacks;
            callbacks.progress = [this](double fraction, SetupText step) { progress(fraction); status(w(step, language_)); };
            callbacks.confirmStop = [](std::span<const p::RunningApplication>) { return true; }; // confirmed on the previous page
            try { result_ = p::install(payload_, plan_, options, callbacks); }
            catch (const std::exception& e) { result_.outcome = p::Outcome::failed; result_.error = e.what(); }
            stage_ = Stage::finished;
            navigate(finishedPage());
        });
    }
private:
    enum class Stage { welcome, running, progress, finished };
    const p::SetupPayload& payload_;
    p::InstallPlan plan_;
    const p::SetupArguments& args_;
    core::Language language_;
    std::atomic<Stage> stage_{Stage::welcome};
    bool launchAtLogin_{true}, open_{};
    std::thread worker_;
    p::OperationResult result_;

    void beginInstall() {
        stage_ = Stage::progress;
        Page progressPage;
        progressPage.heading = w(SetupText::installingHeading, language_);
        progressPage.content = w(SetupText::stepExtract, language_);
        progressPage.progress = true;
        progressPage.common = TDCBF_CLOSE_BUTTON;
        navigate(progressPage);
    }
    Page refusedPage() const {
        Page page;
        page.icon = TD_ERROR_ICON;
        page.heading = w(SetupText::refusedHeading, language_);
        switch (plan_.refusal) {
        case p::Refusal::newerInstalled: page.content = w(SetupText::refusedNewer, language_, {plan_.installed->version.display()}); break;
        case p::Refusal::foreignFolder: page.content = w(SetupText::refusedForeign, language_, {p::narrow(plan_.layout.installRoot.native())}); break;
        case p::Refusal::incomplete: page.content = w(SetupText::refusedIncomplete, language_); break;
        case p::Refusal::unsupportedWindows: page.content = w(SetupText::refusedWindows, language_, {std::to_string(plan_.windowsBuild)}); break;
        default: page.content = w(SetupText::refusedPath, language_, {p::narrow(plan_.layout.installRoot.native())}); break;
        }
        page.common = TDCBF_CLOSE_BUTTON;
        return page;
    }
    Page finishedPage() const {
        Page page;
        page.expanded = joined(result_.warnings);
        page.common = TDCBF_CLOSE_BUTTON;
        if (result_.succeeded()) {
            page.heading = w(result_.outcome == p::Outcome::upgraded ? SetupText::updatedHeading
                             : result_.outcome == p::Outcome::repaired ? SetupText::repairedHeading : SetupText::installedHeading, language_);
            page.content = w(SetupText::installedBody, language_);
            if (!result_.restarted) page.buttons = {{buttonOpen, w(SetupText::openButton, language_)}};
            page.defaultButton = result_.restarted ? IDCLOSE : buttonOpen;
        } else if (result_.outcome == p::Outcome::refused) {
            page.icon = TD_ERROR_ICON;
            page.heading = w(SetupText::failedHeading, language_);
            page.content = w(SetupText::closeFailed, language_);
        } else {
            page.icon = TD_ERROR_ICON;
            page.heading = w(SetupText::failedHeading, language_);
            page.content = w(SetupText::failedBody, language_, {result_.error});
        }
        return page;
    }
};

int runInstall(const p::SetupArguments& args, HINSTANCE instance) {
    const auto self = modulePath();
    std::optional<p::SetupPayload> payload;
    try { payload.emplace(p::SetupPayload::open(self)); }
    catch (const std::exception& e) {
        std::cerr << "EndfieldHUD Setup: " << e.what() << '\n';
        if (!args.quiet && !args.verifyPayload) {
            const auto language = p::setupLanguage(p::InstallLayout::production());
            MessageBoxW(nullptr, w(SetupText::damagedPayload, language).c_str(), w(SetupText::setupTitle, language).c_str(), MB_OK | MB_ICONERROR);
        }
        return static_cast<int>(p::SetupExit::damaged);
    }
    const auto& m = payload->manifest();
    if (args.verifyPayload) {
        std::cout << "EndfieldHUD Setup payload verified: " << m.version.display() << ", commit " << m.commit << ", " << m.files.size() << " files, "
                  << m.installedBytes() << " installed bytes, " << m.storedBytes() << " stored bytes, " << (m.complete ? "complete" : "INCOMPLETE")
                  << ", " << (payload->signatureTable() ? "signed" : "unsigned") << '\n';
        return 0;
    }
    const auto layout = p::InstallLayout::fromArguments(args);
    SetupLock lock(layout);
    if (!lock.owned()) {
        std::cerr << "EndfieldHUD Setup: another Setup is running\n";
        if (!args.quiet) {
            const auto language = p::setupLanguage(layout);
            MessageBoxW(nullptr, w(SetupText::busy, language).c_str(), w(SetupText::setupTitle, language).c_str(), MB_OK | MB_ICONWARNING);
        }
        return static_cast<int>(p::SetupExit::busy);
    }
    p::removeStaleUninstallWorkers(layout.workFolder);
    // Windows 11 or later (package_format.hpp minimumWindowsBuild); isolated
    // tests may name the build the plan is made for.
    auto plan = p::planInstall(m, layout, args.allowDowngrade, args.testWindowsBuild.value_or(p::currentWindowsBuild()));
    const auto language = p::setupLanguage(plan.layout);
    if (args.quiet) {
        p::InstallOptions options;
        options.launchAtLogin = args.launchAtLogin;
        options.failAfter = args.failAfter;
        options.language = language;
        p::OperationCallbacks callbacks;
        callbacks.confirmStop = [](std::span<const p::RunningApplication>) { return true; };
        auto r = p::install(*payload, plan, options, callbacks);
        if (r.succeeded() && args.open && !r.restarted) (void)p::launchInstalledApplication(plan.layout, false);
        writeResult(args, r);
        report(r);
        return static_cast<int>(exitFor(r));
    }
    InstallWizard wizard(*payload, std::move(plan), args, language, instance);
    const auto code = wizard.start();
    writeResult(args, wizard.result());
    return static_cast<int>(code);
}
#else
// --- Uninstall ---------------------------------------------------------------------
class UninstallWizard final : public Wizard {
public:
    UninstallWizard(p::InstallLayout layout, const p::UninstallOptions& options, std::optional<p::InstallManifest> manifest, core::Language language, HINSTANCE instance)
        : Wizard(w(SetupText::uninstallTitle, language), instance), layout_(std::move(layout)), options_(options), manifest_(std::move(manifest)), language_(language) {}
    ~UninstallWizard() override { if (worker_.joinable()) worker_.join(); }
    p::SetupExit start() {
        result_.outcome = p::Outcome::cancelled;
        Page first;
        first.heading = w(SetupText::uninstallHeading, language_, {manifest_ ? manifest_->version.display() : std::string()});
        first.content = w(SetupText::uninstallBody, language_, {p::narrow(layout_.installRoot.native()), p::narrow(layout_.dataBase.native())});
        first.verification = w(SetupText::deleteData, language_);
        first.verificationChecked = options_.removeUserData;
        first.buttons = {{buttonPrimary, w(SetupText::uninstallButton, language_)}};
        first.common = TDCBF_CANCEL_BUTTON;
        first.defaultButton = IDCANCEL;
        run(first);
        if (worker_.joinable()) worker_.join();
        return exitFor(result_);
    }
    const p::OperationResult& result() const noexcept { return result_; }
protected:
    HRESULT clicked(int button) override {
        if (stage_ == Stage::welcome && button == buttonPrimary) {
            if (verificationChecked_) {
                stage_ = Stage::confirmData;
                Page confirm;
                confirm.icon = TD_WARNING_ICON;
                confirm.heading = w(SetupText::confirmDataHeading, language_);
                confirm.content = w(SetupText::confirmDataBody, language_, {p::narrow(layout_.dataBase.native())});
                confirm.buttons = {{buttonDeleteData, w(SetupText::deleteDataButton, language_)}, {buttonKeepData, w(SetupText::keepDataButton, language_)}};
                confirm.common = TDCBF_CANCEL_BUTTON;
                confirm.defaultButton = buttonKeepData;
                navigate(confirm);
                return S_FALSE;
            }
            return proceed(false);
        }
        if (stage_ == Stage::confirmData && (button == buttonDeleteData || button == buttonKeepData)) return proceed(button == buttonDeleteData);
        if (stage_ == Stage::running && button == buttonContinue) { begin(); return S_FALSE; }
        if (stage_ == Stage::progress) return S_FALSE;
        return S_OK;
    }
    void navigated() override {
        if (stage_ != Stage::progress || worker_.joinable()) return;
        worker_ = std::thread([this] {
            p::OperationCallbacks callbacks;
            callbacks.progress = [this](double fraction, SetupText) { progress(fraction); };
            callbacks.confirmStop = [](std::span<const p::RunningApplication>) { return true; };
            try {
                const auto notifications = layout_.isolated() ? nullptr : p::installedScheduledNotifications();
                result_ = p::uninstall(layout_, options_, callbacks, notifications.get());
            } catch (const std::exception& e) {
                result_.outcome = p::Outcome::failed;
                result_.error = e.what();
            }
            stage_ = Stage::finished;
            navigate(finishedPage());
        });
    }
private:
    enum class Stage { welcome, confirmData, running, progress, finished };
    p::InstallLayout layout_;
    p::UninstallOptions options_;
    std::optional<p::InstallManifest> manifest_;
    core::Language language_;
    std::atomic<Stage> stage_{Stage::welcome};
    std::thread worker_;
    p::OperationResult result_;

    HRESULT proceed(bool removeData) {
        options_.removeUserData = removeData;
        if (!p::runningApplications(layout_, manifest_, options_.parentProcess).empty()) {
            stage_ = Stage::running;
            Page running;
            running.heading = w(SetupText::runningHeading, language_);
            running.content = w(SetupText::runningUninstallBody, language_);
            running.buttons = {{buttonContinue, w(SetupText::continueButton, language_)}};
            running.common = TDCBF_CANCEL_BUTTON;
            running.defaultButton = buttonContinue;
            navigate(running);
            return S_FALSE;
        }
        begin();
        return S_FALSE;
    }
    void begin() {
        stage_ = Stage::progress;
        Page page;
        page.heading = w(SetupText::uninstallingHeading, language_);
        page.progress = true;
        page.common = TDCBF_CLOSE_BUTTON;
        navigate(page);
    }
    Page finishedPage() const {
        Page page;
        page.common = TDCBF_CLOSE_BUTTON;
        std::vector<std::string> details = result_.warnings;
        if (!result_.keptFiles.empty()) {
            std::string files;
            for (const auto& f : result_.keptFiles) files += (files.empty() ? "" : "\n") + f;
            details.push_back(p::setupText(SetupText::keptFiles, language_, {files}));
        }
        page.expanded = joined(details);
        if (result_.outcome == p::Outcome::uninstalled) {
            page.heading = w(SetupText::uninstalledHeading, language_);
            page.content = result_.dataRemoved ? w(SetupText::dataDeleted, language_)
                                               : w(SetupText::dataKept, language_, {p::narrow(layout_.dataBase.native())});
        } else if (result_.outcome == p::Outcome::refused && result_.refusal == p::Refusal::closeFailed) {
            page.icon = TD_ERROR_ICON;
            page.heading = w(SetupText::uninstallFailedHeading, language_);
            page.content = w(SetupText::closeFailed, language_);
        } else {
            page.icon = TD_ERROR_ICON;
            page.heading = w(SetupText::uninstallFailedHeading, language_);
            std::string files;
            for (const auto& f : result_.failedFiles) files += (files.empty() ? "" : "\n") + f;
            page.content = w(SetupText::uninstallFailedBody, language_, {files.empty() ? result_.error : files});
        }
        return page;
    }
};

int runUninstallHere(const p::SetupArguments& args, const p::InstallLayout& layout, p::UninstallOptions options, HINSTANCE instance) {
    SetupLock lock(layout);
    const auto language = p::setupLanguage(layout);
    options.language = language;
    options.removeUserData = args.removeUserData; // explicit consent (pre-checks the box in the window)
    if (!lock.owned()) {
        if (!args.quiet) MessageBoxW(nullptr, w(SetupText::busy, language).c_str(), w(SetupText::uninstallTitle, language).c_str(), MB_OK | MB_ICONWARNING);
        std::cerr << "EndfieldHUD uninstall: another Setup is running\n";
        return static_cast<int>(p::SetupExit::busy);
    }
    auto manifest = p::readInstallManifest(layout.installRoot);
    if (args.quiet) {
        p::OperationCallbacks callbacks;
        callbacks.confirmStop = [](std::span<const p::RunningApplication>) { return true; };
        const auto notifications = layout.isolated() ? nullptr : p::installedScheduledNotifications();
        auto r = p::uninstall(layout, options, callbacks, notifications.get());
        writeResult(args, r);
        report(r);
        return static_cast<int>(exitFor(r));
    }
    if (!manifest) {
        // Same refusal as the engine, shown before asking anything.
        p::OperationResult refused;
        refused.outcome = p::Outcome::refused;
        refused.refusal = p::Refusal::notInstalled;
        MessageBoxW(nullptr, w(SetupText::notInstalled, language).c_str(), w(SetupText::uninstallTitle, language).c_str(), MB_OK | MB_ICONINFORMATION);
        writeResult(args, refused);
        return static_cast<int>(p::SetupExit::refused);
    }
    UninstallWizard wizard(layout, options, std::move(manifest), language, instance);
    const auto code = wizard.start();
    writeResult(args, wizard.result());
    return static_cast<int>(code);
}

int runUninstall(const p::SetupArguments& args, HINSTANCE instance) {
    const auto self = modulePath();
    auto layout = p::InstallLayout::fromArguments(args);
    if (!args.worker) {
        if (!args.isolated()) layout.installRoot = self.parent_path();
        // Keep no handle on the install folder (current directory).
        SetCurrentDirectoryW(layout.workFolder.c_str());
        p::removeStaleUninstallWorkers(layout.workFolder);
        if (!p::samePath(self.parent_path(), layout.installRoot)) return runUninstallHere(args, layout, {}, instance);
        // Relocate: a copy outside the install folder does the work while this
        // process waits and returns its exit code.
        const auto folder = layout.workFolder / (L"EndfieldHUD-Uninstall-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        if (!CreateDirectoryW(folder.c_str(), nullptr)) { std::cerr << "EndfieldHUD uninstall: cannot create its work folder\n"; return static_cast<int>(p::SetupExit::failed); }
        const auto copy = folder / p::uninstallerExecutable;
        if (!CopyFileW(self.c_str(), copy.c_str(), TRUE)) { std::cerr << "EndfieldHUD uninstall: cannot copy itself\n"; return static_cast<int>(p::SetupExit::failed); }
        std::wstring command = commandArgument(copy.native());
        for (const auto& a : p::uninstallWorkerArguments(args, GetCurrentProcessId(), p::narrow(layout.installRoot.native())))
            command += L" " + commandArgument(p::widen(a));
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(copy.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, folder.c_str(), &startup, &process)) {
            std::cerr << "EndfieldHUD uninstall: cannot start its worker\n";
            return static_cast<int>(p::SetupExit::failed);
        }
        AllowSetForegroundWindow(process.dwProcessId);
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = static_cast<DWORD>(p::SetupExit::failed);
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        return static_cast<int>(code);
    }
    // Worker: the waiting parent's image is moved next to this copy.
    p::UninstallOptions options;
    options.parentProcess = *args.parentProcess;
    options.relocatedImage = layout.uninstaller();
    options.relocationFolder = self.parent_path();
    return runUninstallHere(args, layout, options, instance);
}
#endif
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    int count{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> values;
    for (int n = 1; argv && n < count; ++n) values.push_back(p::narrow(argv[n]));
    if (argv) LocalFree(argv);
    std::vector<std::string_view> views(values.begin(), values.end());
    p::SetupArguments args;
    try {
        args = p::parseSetupArguments(role, views);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n' << p::setupUsage(role) << '\n';
        const bool quiet = std::find(views.begin(), views.end(), "--quiet") != views.end() || std::find(views.begin(), views.end(), "/S") != views.end();
        if (!quiet) MessageBoxW(nullptr, (p::widen(e.what()) + L"\n\n" + p::widen(p::setupUsage(role))).c_str(), L"EndfieldHUD", MB_OK | MB_ICONERROR);
        return static_cast<int>(p::SetupExit::usage);
    }
    try {
#ifdef EHUD_SETUP_ROLE_INSTALL
        return runInstall(args, instance);
#else
        return runUninstall(args, instance);
#endif
    } catch (const std::exception& e) {
        std::cerr << "EndfieldHUD " << (role == p::SetupRole::install ? "Setup" : "uninstall") << " failed: " << e.what() << '\n';
        if (!args.quiet) MessageBoxW(nullptr, p::widen(e.what()).c_str(), L"EndfieldHUD", MB_OK | MB_ICONERROR);
        return static_cast<int>(p::SetupExit::failed);
    }
}
