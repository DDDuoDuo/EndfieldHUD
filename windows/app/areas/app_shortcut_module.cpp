#include "app/areas/app_shortcut_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/app_shortcut_navigation.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/localization.hpp"
#include "modules/app_shortcut_navigation.hpp"
#include "modules/app_shortcut_tasks.hpp"
#include "native/app_shortcut_assets.hpp"
#include "native/app_shortcut_text.hpp"
#include "tools/app_shortcut_preview.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace m = modules;
namespace g = native;
using app::ApplicationAreaMode;
using Entries = std::span<const g::LayerCompositionEntry>;
using J = m::ShortcutJson;

void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("App shortcuts: ") + why); }
std::string text(core::Language l, std::string_view english, std::string_view chinese) { return core::localized(english, chinese, l); }
std::string lower(std::string value) { for (auto& c : value) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return value; }
std::string suffix(std::string_view path) { const auto dot = path.rfind('.'); return dot == path.npos ? std::string{} : lower(std::string(path.substr(dot))); }

// Mac AppShortcutStoreError.errorDescription (Windows product name). Failures
// without a store code are "unavailable" for OS work and "persistence" for
// repository work, each with its short detail (never a path).
std::string errorText(core::Language l, std::optional<m::ShortcutErrorCode> code, bool osWork, std::string_view detail) {
    using C = m::ShortcutErrorCode;
    if (code) switch (*code) {
    case C::invalidName: return text(l, "Enter a name of 1–128 characters without line breaks.", "请输入 1–128 个字符的名称，不含换行。");
    case C::duplicate: return text(l, "This application is already saved. Edit its existing shortcut instead.", "此应用已添加，请编辑现有快捷方式。");
    case C::missing: return text(l, "This app shortcut is no longer available.", "此应用快捷方式已不存在。");
    case C::applicationChanged: return text(l, "A different application now occupies this location. Choose the application again.", "此位置的应用已更改，请重新选择应用。");
    case C::invalidRecord: return text(l, "The saved app shortcuts could not be read. The original data has been preserved.", "无法读取应用快捷方式，原始数据已保留。");
    case C::newerVersion: return text(l, "These app shortcuts were saved by a newer version of EndfieldHUD.", "这些快捷方式由较新版本的 EndfieldHUD 保存。");
    case C::changedOnDisk: return text(l, "App shortcuts changed outside this window. Restart EndfieldHUD before editing.", "应用快捷方式已在外部更改，请重启 EndfieldHUD 后再编辑。");
    case C::unavailable: return text(l, "The application is unavailable.", "应用不可用。");
    case C::capacity: return text(l, "App shortcuts could not be saved or opened: ", "无法保存或打开应用快捷方式：") + std::string(detail);
    }
    return osWork ? text(l, "The application is unavailable: ", "应用不可用：") + std::string(detail)
                  : text(l, "App shortcuts could not be saved or opened: ", "无法保存或打开应用快捷方式：") + std::string(detail);
}

// Combines the source catalog's module entries with the committed saved apps
// (HUDDesktopWatchNavigation.entries(shortcuts:)). Plans are content events:
// the host calls plan() on preference/list changes, never on warm frames.
class NavigationPlanner {
public:
    app::AppShortcutNavigation::Plan plan(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right, const m::ShortcutFile& file) {
        try {
            const bool sameBase = navigation_ && modules.size() == baseActions_.size() && right.size() == baseRight_.size() &&
                std::equal(right.begin(), right.end(), baseRight_.begin()) &&
                std::equal(modules.begin(), modules.end(), baseActions_.begin(), [](const auto& e, std::uint64_t a) { return e.action == a; });
            if (!sameBase) {
                std::vector<m::ShortcutNavigationEntry> base;base.reserve(modules.size());
                for (const auto& e : modules) base.push_back({e.action, e.target, e.title, e.iconKey, true});
                navigation_.emplace(std::move(base), std::vector<std::uint64_t>(right.begin(), right.end()));
                baseActions_.clear();for (const auto& e : modules) baseActions_.push_back(e.action);
                baseRight_.assign(right.begin(), right.end());
            }
            std::vector<std::pair<std::uint64_t, std::string>> titles;titles.reserve(modules.size());
            for (const auto& e : modules) titles.emplace_back(e.action, e.title);
            navigation_->setModuleTitles(titles);
            navigation_->replace(file);
            entries_.clear();
            for (const auto& e : navigation_->entries()) {
                if (e.module) {
                    const auto found = std::find_if(modules.begin(), modules.end(), [&](const auto& v) { return v.action == e.action; });
                    need(found != modules.end(), "a planned module entry left the catalog");
                    entries_.push_back(*found);
                } else entries_.push_back({e.action, e.target, e.title, e.iconKey, false, {}, 0});
            }
            right_.assign(navigation_->rightActions().begin(), navigation_->rightActions().end());
        } catch (const std::exception&) {
            // Invalid base (no Add App last) or record: the source catalog only.
            navigation_.reset();baseActions_.clear();baseRight_.clear();
            entries_.assign(modules.begin(), modules.end());right_.assign(right.begin(), right.end());
        }
        ++plans_;
        return {entries_, right_};
    }
    std::optional<std::string_view> shortcutID(std::uint64_t action) const noexcept { return navigation_ ? navigation_->shortcutID(action) : std::nullopt; }
    std::span<const g::WatchAppearanceEntry> entries() const noexcept { return entries_; }
    std::span<const std::uint64_t> right() const noexcept { return right_; }
    std::uint64_t plans() const noexcept { return plans_; }
private:
    std::optional<m::ShortcutNavigation> navigation_;
    std::vector<std::uint64_t> baseActions_, baseRight_;
    std::vector<g::WatchAppearanceEntry> entries_;
    std::vector<std::uint64_t> right_;
    std::uint64_t plans_{};
};

// Hidden coverage only: synthetic applications, inspected and "launched" on
// the shared worker without touching the file system, the Shell or any
// installed application. Paths are owned synthetic names under the area's
// coverage root; a stem starting with "Missing" is an unavailable target.
struct SyntheticApplications {
    std::atomic<unsigned> inspected{}, launched{};
    std::atomic<bool> failLaunch{};
};
// The worker keeps its own reference: accepted work may outlive the owner.
g::AppShortcutOS syntheticOS(std::shared_ptr<SyntheticApplications> self) {
        return {[self](const g::AppShortcutSelection& s) {
                    ++self->inspected;
                    if (s.kind != g::AppShortcutSelectionKind::file) throw m::ShortcutError(m::ShortcutErrorCode::unavailable);
                    const auto stem = fs::path(std::u8string(s.value.begin(), s.value.end())).stem().u8string();
                    const std::string name(stem.begin(), stem.end());
                    if (name.starts_with("Missing")) throw m::ShortcutError(m::ShortcutErrorCode::unavailable);
                    g::AppShortcutResolved r;r.selection = s;r.name = name + " Synthetic";
                    r.applicationKey = "path:" + lower(s.value);r.selectedFileKey = "file:synthetic:" + lower(s.value);
                    if (suffix(s.value) == ".lnk") r.launchKind = g::AppShortcutLaunchKind::shellLink;
                    else {r.launchKind = g::AppShortcutLaunchKind::executable;r.executablePath = s.value;}
                    return r;
                },
                [self](const g::AppShortcutResolved&, std::uintptr_t) {
                    if (self->failLaunch) throw m::ShortcutError(m::ShortcutErrorCode::unavailable);
                    ++self->launched;return g::AppShortcutLaunchReceipt{4242};
                }};
}

class AppShortcutArea final : public app::ModuleOwner, public app::AppShortcutNavigation {
public:
    AppShortcutArea(const app::ApplicationModuleContext& c, fs::path assets, fs::path directory, std::shared_ptr<SyntheticApplications> synthetic)
        : host_(*c.host), window_(static_cast<HWND>(c.window)), utility_(*c.utility), mode_(c.mode), directory_(std::move(directory)),
          coverageRoot_(c.coverageRoot), synthetic_(std::move(synthetic)), assets_(std::move(assets)), alive_(std::make_shared<bool>(true)) {
        need(window_ && IsWindow(window_), "the HUD window is required");
        textMessage_ = host_.registerMessage();
        const auto& a = host_.appearance();
        appearance_ = {a.dark, a.language, a.accent};language_ = a.language;
        tools::AppShortcutPreviewOptions options;options.appearance = appearance_;options.textMessage = textMessage_;
        auto* manager = static_cast<ITfThreadMgr*>(c.textManager);
        preview_ = std::make_unique<tools::AppShortcutPreview>(window_, c.rasterizer, assets_, manager,
            manager ? static_cast<TfClientId>(c.textClient) : TF_CLIENTID_NULL, std::move(options));
        if (c.metrics.pixelWidth && c.metrics.pixelHeight) preview_->resize(c.metrics);
        tasks_ = std::make_unique<m::AppShortcutTasks>(utility_, directory_, g::nativeShortcutTextRules(),
            synthetic_ ? syntheticOS(synthetic_) : g::windowsAppShortcutOS(), [this](m::ShortcutTaskResult r) { completed(std::move(r)); });
        host_.publish<app::AppShortcutNavigation>(app::appShortcutNavigationService, *this);
        // AppShortcutStore(directory:) at startup: the one initial load.
        loadPending_ = true;pump();
    }
    ~AppShortcutArea() override {
        *alive_ = false;observer_ = {};
        host_.withdraw(app::appShortcutNavigationService);
        tasks_.reset(); // suppresses completions; accepted writes/launches still finish on the executor
    }
    AppShortcutArea(const AppShortcutArea&) = delete;
    AppShortcutArea& operator=(const AppShortcutArea&) = delete;

    // --- ModuleOwner -----------------------------------------------------------
    std::string_view name() const noexcept override { return "appShortcuts"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;r.pointer = 30;r.wheel = 30;r.key = 30;r.filterKey = 30;r.filterRequiresOwnerTarget = true;
        r.message = 30;r.messageRetriesPendingClose = true;r.finishEditing = 30;r.flush = 30;return r;
    }
    bool presents(core::Module module) const noexcept override { return module == core::Module::addApp; }
    void resize(const app::ClientMetrics& metrics) override { if (metrics.pixelWidth && metrics.pixelHeight) preview_->resize(metrics); }
    void setAppearance(const app::ModuleAppearance& a, double t) override {
        language_ = a.language;appearance_ = {a.dark, a.language, a.accent};
        preview_->setAppearance(appearance_, t);preview_->setReduceMotion(a.reduceMotion, t);
    }
    void setOverlayVisible(bool visible, double t) override {
        if (!visible) {closing(t);return;}
        overlayVisible_ = true;preview_->setOverlayVisible(true, t);
        // Mac OverlayController retries a failed store on the next opening.
        if (loadFailed_ && !loadPending_) {loadFailed_ = false;loadPending_ = true;}
        pump();
    }
    void overlayClosing(double t) override { closing(t); }
    void update(const app::ModuleFrame& f) override { preview_->update(f.center, f.chrome, f.presentation, f.opacity, f.time);pump(); }
    bool requiresFrames(double t) const override { return preview_->requiresFrames(t); }
    void upload(g::Renderer& r) override { preview_->upload(r); }
    Entries entries(core::Module module) override { return module == core::Module::addApp ? preview_->entries() : Entries{}; }
    void collected(g::Renderer& r) override { preview_->collected(r); }
    void release(g::Renderer& r) override { preview_->release(r); }
    bool covers(core::Point p) const override { return preview_->covers(p); }
    bool pointerLocked() const override { return preview_->pointerLocked(); }
    bool pointer(const app::PointerEvent& e, double t) override { const bool used = preview_->pointer(e, t);pump();return used; }
    bool wheel(const app::WheelEvent& e, double t) override { return preview_->wheel(e, t); }
    bool key(const app::KeyEvent& e, double t) override { const bool used = preview_->key(e, t);pump();return used; }
    bool filterKey(const app::NativeMessage& m) override { return preview_->filterKey(m); }
    bool message(const app::NativeMessage& m, double t) override { return preview_->message(m, t); }
    void focus(bool value, double t) override { preview_->focus(value, t); }
    void cancelInteraction(double t) override { preview_->cancelInteraction(t); }
    bool finishEditing(double t) override { return preview_->finishEditing(true, t); }
    bool flush(double) override {
        // Bounded durable drain at quit/session end: an accepted save/remove
        // (or one retained while the executor was full) reaches the disk.
        for (unsigned n = 0; n < 16 && tasks_->busy() && inflight_ && durable(inflight_->kind); ++n) {
            utility_.waitIdle();utility_.drain();tasks_->queueCapacityAvailable();
        }
        return !(tasks_->busy() && inflight_ && durable(inflight_->kind));
    }
    bool hostEvent(const app::HostEvent& e, double t) override {
        using K = app::HostEvent::Kind;
        switch (e.kind) {
        case K::utilityCompleted: tasks_->queueCapacityAvailable();pump();return false;
        case K::overlayConcealed: concealed(t);return false;
        case K::suspended: case K::quitting: cancelPendingLaunch(t);return false; // forceCloseSystemOverlay / requestQuit
        default: return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override;

    // --- AppShortcutNavigation -------------------------------------------------
    Plan plan(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right) override {
        return planner_.plan(modules, right, preview_->state().file());
    }
    bool activate(std::uint64_t action, double t) override {
        const auto id = planner_.shortcutID(action);
        if (!id) return false;
        const auto& items = preview_->state().file().items;
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto& v) { return v.id == *id; });
        if (found != items.end()) beginLaunch(*found, std::nullopt, t);
        return true;
    }
    void setObserver(std::function<void()> observer) override { observer_ = std::move(observer); }

private:
    enum class Purpose { load, request, verify, launch };
    struct Inflight { std::uint64_t token{}; Purpose purpose{}; m::ShortcutTaskKind kind{}; std::uint64_t generation{}; };
    // Mac pendingAppLaunch / appLaunchInFlight: verify -> closing (our close
    // was requested) -> closed (its overlayClosing arrived) -> dispatch (the
    // close finished: overlayConcealed) -> running (the launch was accepted).
    enum class LaunchPhase { verify, closing, closed, dispatch, running };
    struct Launch { std::optional<std::uint64_t> generation; m::ShortcutRecord record; LaunchPhase phase{}; };

    static bool durable(m::ShortcutTaskKind k) noexcept { return k == m::ShortcutTaskKind::save || k == m::ShortcutTaskKind::remove || k == m::ShortcutTaskKind::launch; }
    bool coverageMode() const noexcept { return mode_ == ApplicationAreaMode::hiddenCoverage; }
    double now() const { return host_.now(); }

    bool submit(m::ShortcutTask task, Purpose purpose, std::uint64_t generation = 0) {
        task.token = ++token_;const auto kind = task.kind;
        if (!tasks_->submit(std::move(task))) return false;
        inflight_ = Inflight{token_, purpose, kind, generation};return true;
    }
    void pump() {
        if (pumping_ || !tasks_) return;
        pumping_ = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{pumping_};
        // Concealing or a module switch cancels stale reads and the chooser.
        if (const auto cancelled = preview_->takeCancelledRequest()) {
            if (inflight_ && inflight_->purpose == Purpose::request && inflight_->generation == *cancelled && tasks_->cancel(inflight_->token)) inflight_.reset();
            if (chooser_ == *cancelled) {chooser_.reset();if (!coverageMode()) host_.cancelFileChoice();}
        }
        for (unsigned n = 0; n < 4 && pumpOnce(); ++n) {}
    }
    // One step of the single transaction owner; true when it changed state
    // without occupying it (a launch request became a pending verification).
    bool pumpOnce() {
        if (tasks_->busy()) return false;
        if (loadPending_) {
            m::ShortcutTask load;load.kind = m::ShortcutTaskKind::load;
            if (submit(std::move(load), Purpose::load)) loadPending_ = false;
            return false;
        }
        if (launch_ && launch_->phase == LaunchPhase::verify) {
            // Mac store.resolvedURL before closing: a missing target reports
            // its error in the open HUD instead of closing it.
            m::ShortcutTask verify;verify.kind = m::ShortcutTaskKind::edit;verify.record = launch_->record;
            (void)submit(std::move(verify), Purpose::verify);return false;
        }
        if (launch_ && launch_->phase == LaunchPhase::dispatch) {
            m::ShortcutTask run;run.kind = m::ShortcutTaskKind::launch;run.record = launch_->record;
            if (submit(std::move(run), Purpose::launch)) launch_->phase = LaunchPhase::running;
            return false;
        }
        if (chooser_) return false;
        auto request = preview_->takeRequest();
        if (!request) return false;
        const auto generation = request->generation;
        using K = tools::ShortcutRequestKind;
        m::ShortcutTask t;t.record = request->record;t.editingID = request->editingID;
        switch (request->kind) {
        case K::choose: choose(generation);return false;
        case K::launch: beginLaunch(*request->record, generation, now());return true;
        case K::inspect: t.kind = m::ShortcutTaskKind::inspect;t.selection = request->selection;break;
        case K::edit: t.kind = m::ShortcutTaskKind::edit;break;
        case K::save: t.kind = m::ShortcutTaskKind::save;t.candidate = request->candidate;t.name = request->name;t.icon = request->icon;
            t.newID = ehud::data::makeUUID();t.createdAt = ehud::data::foundationNow();break;
        case K::remove: t.kind = m::ShortcutTaskKind::remove;break;
        }
        (void)submit(std::move(t), Purpose::request, generation);
        return false;
    }

    // HUDAppShortcutInteraction.chooseApplication through the host's one
    // shared dialog (it owns the focus-loss exemption and the cancellation on
    // a module switch or close). Hidden coverage resolves it synthetically.
    void choose(std::uint64_t generation) {
        chooser_ = generation;
        if (coverageMode()) {++coverageChooserRequests_;return;}
        const app::ApplicationFileRequest request{text(language_, "Choose an application", "选择应用"), text(language_, "Choose", "选择"), text(language_, "Choose", "选择")};
        std::weak_ptr<bool> alive = alive_;
        if (!host_.chooseFiles(request, [this, alive, generation](app::ApplicationFileResult result) {
                if (const auto token = alive.lock(); token && *token) chosen(generation, std::move(result));
            })) {
            chooser_.reset();preview_->completeRequest(generation, text(language_, "File picker is busy", "文件选择器正忙"), now());
        }
    }
    void chosen(std::uint64_t generation, app::ApplicationFileResult result) {
        if (chooser_ != generation) return;
        chooser_.reset();const auto t = now();
        if (result.cancelled) preview_->completeRequest(generation, {}, t);
        else if (result.failed) preview_->completeRequest(generation, text(language_, "File picker is unavailable", "文件选择器不可用"), t);
        else if (result.paths.size() != 1) preview_->completeRequest(generation, text(language_, "Choose one application to customize.", "请选择一个应用进行设置。"), t);
        else if (const auto kind = suffix(result.paths.front()); kind != ".exe" && kind != ".lnk")
            preview_->completeRequest(generation, text(language_, "Choose a Windows application (.exe) or its shortcut (.lnk).", "请选择 Windows 应用程序（.exe）或其快捷方式（.lnk）。"), t);
        else {
            preview_->completeRequest(generation, {}, t);
            (void)preview_->inspectSelection({g::AppShortcutSelectionKind::file, std::move(result.paths.front())}, t);
        }
        pump();host_.requestFrame();
    }

    // OverlayController.requestAppShortcutLaunch.
    void beginLaunch(const m::ShortcutRecord& record, std::optional<std::uint64_t> generation, double t) {
        if (launch_ || !overlayVisible_) {if (generation) preview_->completeRequest(*generation, {}, t);return;}
        if (!record.native()) {
            // An imported Mac record keeps its opaque bookmark; it launches
            // only after it is edited and relinked to a Windows application.
            const auto message = text(language_, "The application is unavailable: ", "应用不可用：") +
                text(language_, "Edit this shortcut and choose its Windows application.", "请编辑此快捷方式并选择对应的 Windows 应用。");
            if (generation) preview_->completeRequest(*generation, message, t);else preview_->showError(message, t);
            host_.requestFrame();return;
        }
        launch_ = Launch{generation, record, LaunchPhase::verify};
        pump();
    }
    void finishLaunch(std::optional<std::string> error, double t) {
        auto launch = std::exchange(launch_, std::nullopt);
        if (!launch) return;
        if (launch->generation) preview_->completeRequest(*launch->generation, std::move(error), t);
        else if (error) preview_->showError(std::move(error), t);
    }
    void cancelPendingLaunch(double t) {
        if (!launch_ || launch_->phase == LaunchPhase::running) return;
        if (inflight_ && inflight_->purpose == Purpose::verify && tasks_->cancel(inflight_->token)) inflight_.reset();
        finishLaunch({}, t);
    }
    void closing(double t) {
        if (launch_) switch (launch_->phase) {
            case LaunchPhase::verify: cancelPendingLaunch(t);break;          // the user closed the HUD first
            case LaunchPhase::closing: launch_->phase = LaunchPhase::closed;break; // our close began
            case LaunchPhase::closed: cancelPendingLaunch(t);break;          // a forced close interrupted it
            default: break;
        }
        overlayVisible_ = false;
        preview_->cancelInteraction(t);preview_->setOverlayVisible(false, t);
        pump(); // consume the preview's cancelled reads
    }
    void concealed(double t) {
        if (!launch_) return;
        if (launch_->phase == LaunchPhase::closed) {
            // completeAppShortcutHandoff: the HUD is gone; the selected
            // application receives the user's foreground activation.
            launch_->phase = LaunchPhase::dispatch;
            if (mode_ == ApplicationAreaMode::production) (void)AllowSetForegroundWindow(ASFW_ANY);
            pump();
        } else if (launch_->phase == LaunchPhase::closing) cancelPendingLaunch(t);
    }
    void requestClose() {
        if (coverageMode()) {++coverageCloseRequests_;return;} // the coverage harness owns the lifecycle
        host_.closeOverlay();
    }
    void requestReopen() {
        if (coverageMode()) {++coverageReopenRequests_;return;}
        host_.openModule(core::Module::addApp);
    }

    void notify() { ++listRevision_;if (observer_) observer_(); }
    void completed(m::ShortcutTaskResult r) {
        if (!inflight_ || inflight_->token != r.token) {pump();return;}
        const auto f = *std::exchange(inflight_, std::nullopt);const auto t = now();
        const auto failure = [&](bool osWork) { return errorText(language_, r.code, osWork, r.error.value_or(std::string{})); };
        switch (f.purpose) {
        case Purpose::load:
            if (r.file) {loadFailed_ = false;preview_->replaceItems(std::move(*r.file), t);notify();}
            else {loadFailed_ = true;preview_->showError(failure(false), t);}
            break;
        case Purpose::request:
            if (f.kind == m::ShortcutTaskKind::inspect || f.kind == m::ShortcutTaskKind::edit) {
                if (r.candidate) (void)preview_->completeCandidate(f.generation, std::move(*r.candidate), t);
                else (void)preview_->completeRequest(f.generation, failure(true), t);
            } else if (r.file) {(void)preview_->completeFile(f.generation, std::move(*r.file), t);notify();}
            else (void)preview_->completeRequest(f.generation, failure(false), t);
            break;
        case Purpose::verify:
            if (!launch_ || launch_->phase != LaunchPhase::verify) break;
            if (r.error) {finishLaunch(failure(true), t);break;}
            if (!overlayVisible_) {finishLaunch({}, t);break;}
            launch_->phase = LaunchPhase::closing;
            requestClose(); // may synchronously deliver overlayClosing
            break;
        case Purpose::launch: {
            if (!launch_) break;
            const auto name = launch_->record.name;
            if (!r.error) {
                ++launchesOpened_;
                host_.recordEvent(modules::EventKind::appShortcutOpened, modules::EventMetadata{{"app", name}});
                finishLaunch({}, t);
            } else {
                // Keep the failed target editable and report it in Add App.
                finishLaunch(failure(true), t);
                if (!overlayVisible_) requestReopen(); // Mac: only while the HUD is still closed
            }
            break;
        }
        }
        pump();host_.requestFrame();
    }

    // --- Hidden coverage --------------------------------------------------------
    struct Driver;

    app::ApplicationHost& host_;
    HWND window_{};
    app::UtilityExecutor& utility_;
    ApplicationAreaMode mode_{};
    fs::path directory_, coverageRoot_;
    std::shared_ptr<SyntheticApplications> synthetic_;
    g::NativeAppShortcutAssets assets_;
    std::shared_ptr<bool> alive_;
    std::uint32_t textMessage_{};
    m::ShortcutAppearance appearance_;
    core::Language language_{core::Language::english};
    std::unique_ptr<tools::AppShortcutPreview> preview_;
    std::unique_ptr<m::AppShortcutTasks> tasks_; // destroyed before the preview its completions update
    NavigationPlanner planner_;
    std::function<void()> observer_;
    std::optional<Inflight> inflight_;
    std::optional<Launch> launch_;
    std::optional<std::uint64_t> chooser_;
    std::uint64_t token_{}, listRevision_{};
    bool loadPending_{}, loadFailed_{}, overlayVisible_{}, pumping_{};
    unsigned coverageChooserRequests_{}, coverageCloseRequests_{}, coverageReopenRequests_{}, launchesOpened_{};
};

// One combined module cycle of hidden coverage on the + Add App canvas. Every
// cycle runs the same add / relink error / edit / duplicate / launch / remove
// scenario through projected pointer input and ends in the seeded state, so
// the shared raster usage repeats exactly.
struct AppShortcutArea::Driver {
    AppShortcutArea& a;
    app::ModuleCoverage& step;
    double t;
    // The source catalog's runtime order without saved apps
    // (HUDDesktopWatchNavigation.entries(shortcuts: [])), used to drive the
    // same planner while the host has not attached its navigation observer.
    static constexpr std::array<std::string_view, 24> sourceNavigation{
        "system", "display", "hotkeys", "about", "storage", "activityMonitor", "notes", "fileShelf", "clipboard", "archive", "mediaAssembly", "minigame",
        "nowPlaying", "volume", "projection", "reader", "workMode", "calendar", "map", "eventLog", "profile", "account", "power", "addApp"};

    const m::AppShortcutState& state() const { return a.preview_->state(); }
    void frame(double dt = 1. / 60) { t += dt;step.present(t); }
    void frames(double seconds) { const double end = t + seconds;while (t < end) frame(); }
    bool working() const { return a.tasks_->busy() || a.loadPending_ || (a.launch_ && (a.launch_->phase == LaunchPhase::verify || a.launch_->phase == LaunchPhase::dispatch)); }
    void settle() {
        for (unsigned n = 0; n < 12 && working(); ++n) {step.drain();frame();}
        step.check(!working(), "App shortcut work settles through the shared executor");
    }
    void click(double x, double y) {
        const auto p = step.project({x, y}, 400, 334);
        step.check(p.has_value(), "The + Add App canvas point projects onto the current module plane");
        step.check(a.covers(*p), "The projected canvas point belongs to + Add App");
        a.pointer({app::PointerKind::move, app::PointerButton::none, p->x, p->y, 0}, t);
        a.pointer({app::PointerKind::down, app::PointerButton::left, p->x, p->y, 0}, t);
        a.pointer({app::PointerKind::up, app::PointerButton::left, p->x, p->y, 0}, t);
        frame();
    }
    static double row(std::size_t index) { return 44 + 52. * double(index) + 23; }
    std::size_t index(std::string_view id) const {
        const auto& items = state().file().items;
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto& v) { return v.id == id; });
        step.check(found != items.end(), "The saved app is listed");
        return std::size_t(found - items.begin());
    }
    const m::ShortcutRecord& record(std::string_view id) const { return state().file().items[index(id)]; }
    void type(std::u16string_view value) {
        step.check(a.preview_->editingName(), "The name field is open");
        for (const auto c : value) step.check(a.preview_->key({app::KeyKind::character, c}, false, t), "The name field accepts typed text");
        step.check(a.preview_->key({app::KeyKind::down, VK_RETURN}, false, t) && !a.preview_->editingName(), "Return commits the name field");
        frame();
    }
    void choose(const fs::path& application) {
        const auto before = a.coverageChooserRequests_;
        click(329, 16); // + Choose app
        step.check(a.coverageChooserRequests_ == before + 1 && a.chooser_ && a.preview_->presentingPanel() && a.preview_->pointerLocked(),
                   "+ Choose app requests the one chooser and locks the canvas behind it");
        const auto path = application.u8string();
        a.chosen(*a.chooser_, {false, false, {std::string(path.begin(), path.end())}});
        settle();frames(.32); // inspection, then the source .26 s draft arrival
    }
    m::ShortcutFile disk() const {
        const auto bytes = ehud::data::detail::readFile(a.directory_ / "shortcuts.json", m::shortcutMaximumBytes);
        step.check(bytes.has_value(), "The data root holds shortcuts.json");
        return m::decodeShortcuts(J::parse(*bytes, m::shortcutMaximumBytes), g::nativeShortcutTextRules());
    }
    void commit(double x, double y, const char* why) {
        const auto revision = a.listRevision_;
        click(x, y);settle();frames(.3);
        step.check(a.listRevision_ == revision + 1 && !state().editing(), why);
        step.check(disk() == state().file(), "The committed shortcuts.json is exactly the shown list");
    }
    // Navigation: the host's plan once it attached its observer, otherwise the
    // same planner over the source catalog order.
    void plan() {
        if (a.observer_) {frame();return;}
        std::vector<g::WatchAppearanceEntry> modules;std::vector<std::uint64_t> right;
        for (std::size_t n = 0; n < sourceNavigation.size(); ++n) {
            const std::string target(sourceNavigation[n]);
            modules.push_back({n + 1, target, target, "module:" + target, true, {}, 0});
            if (n >= 4 && target != "storage" && target != "activityMonitor") right.push_back(n + 1);
        }
        (void)a.planner_.plan(modules, right, state().file());
    }
    std::uint64_t action(std::string_view target) const {
        const auto entries = a.planner_.entries();
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.target == target; });
        return found == entries.end() ? 0 : found->action;
    }
    void checkNavigation() {
        plan();
        const auto right = a.planner_.right();const auto entries = a.planner_.entries();
        const auto power = action("power"), add = action("addApp");
        const auto at = std::find(right.begin(), right.end(), power);
        bool ordered = power && add && !right.empty() && right.back() == add && !entries.empty() && entries.back().target == "addApp" && at != right.end();
        const auto& items = state().file().items;
        for (std::size_t n = 0; ordered && n < items.size(); ++n) {
            const auto& item = items[n];
            const auto app = action("app." + item.id);
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.action == app; });
            ordered = app && at + 1 + std::ptrdiff_t(n) < right.end() && *(at + 1 + std::ptrdiff_t(n)) == app && entry != entries.end() && !entry->module &&
                entry->title == item.name && entry->iconKey == "shortcut:" + item.iconPreset && a.planner_.shortcutID(app) == std::optional<std::string_view>(item.id);
        }
        step.check(ordered && std::size_t(right.end() - at) == items.size() + 2, "Saved apps follow Power in their stored order and + Add App stays last");
    }
    // Close-first handoff on this owner's own close/conceal path (the hidden
    // harness keeps the HUD lifecycle; the owner's close request is counted).
    void close(bool forced) {
        const auto closes = a.coverageCloseRequests_;
        settle(); // reinspection before closing
        step.check(a.launch_ && a.launch_->phase == LaunchPhase::closing && a.coverageCloseRequests_ == closes + 1, "A verified launch first requests the HUD close");
        a.closing(t);
        step.check(a.launch_ && a.launch_->phase == LaunchPhase::closed && !a.overlayVisible_, "The launch waits for the close to finish");
        if (forced) a.closing(t); // forceCloseSystemOverlay interrupts the closing animation
        t += .5;a.concealed(t);settle();
    }
    void reopen() {
        a.setOverlayVisible(true, t);frames(.4);
        step.check(state().active() && !a.launch_, "+ Add App is interactive again after the handoff");
    }
};

void AppShortcutArea::coverage(app::ModuleCoverage& step) {
    Driver d{*this, step, step.time};
    if (!step.module) {
        // Per-cycle area step: nothing pending, the disk equals the shown
        // list and the navigation keeps saved apps between Power and Add App.
        step.check(!tasks_->busy() && !launch_ && !chooser_ && !loadFailed_ && !preview_->busy(), "App shortcuts are idle between cycles");
        step.check(d.disk() == preview_->state().file(), "The data root still holds the shown list");
        d.checkNavigation();
        d.frame();return;
    }
    step.check(*step.module == core::Module::addApp, "App shortcuts present only + Add App");
    d.settle();d.frames(.1);
    const auto& state = preview_->state();
    const auto seeded = state.file();
    step.check(state.active() && !state.editing() && !state.error() && !preview_->entries().empty() && seeded.items.size() == 1 && !seeded.items[0].native() &&
               d.disk() == seeded, "+ Add App shows the seeded imported Mac shortcut from the data root");
    d.checkNavigation();
    const auto applications = coverageRoot_ / "Applications";
    const auto launched = synthetic_->launched.load();
    const auto opened = launchesOpened_, reopened = coverageReopenRequests_, closes = coverageCloseRequests_;

    // 1. Add an executable: inspected name, typed Unicode name, Camera preset.
    d.choose(applications / "Alpha.exe");
    step.check(state.editing() && state.candidate()->name == "Alpha Synthetic" && state.draftIcon() == "original" && !state.editingID(),
               "A chosen application opens a new draft with its inspected name and the App icon preset");
    d.type(u"Alpha 应用");
    d.click(36 + 4 * 54, 164 + 55); // icon 11: Camera
    step.check(state.draftIcon() == "camera", "The Camera preset is selected");
    d.frames(.2);d.commit(320, 288, "Save shortcut commits the new app");
    const auto alpha = state.file().items.back();
    step.check(state.file().items.size() == 2 && alpha.name == "Alpha \xe5\xba\x94\xe7\x94\xa8" && alpha.iconPreset == "camera" && alpha.native() &&
               alpha.locator["windowsTarget"]["kind"] == J("executable"), "The saved app keeps its name, preset and Windows locator");
    d.checkNavigation();
    const auto alphaAction = d.action("app." + alpha.id);

    // 2. The imported Mac record launches only after relinking: no close.
    d.click(100, Driver::row(0));d.settle();
    step.check(state.error() && !launch_ && !preview_->busy() && coverageCloseRequests_ == closes, "An imported Mac target reports its relink error without closing");

    // 3. A missing target fails inspection and opens no draft.
    d.choose(applications / "Missing.exe");
    step.check(!state.editing() && state.error() && state.file().items.size() == 2, "A missing application reports its error and opens no draft");

    // 4. A launch that fails after the close reports in + Add App and reopens it.
    synthetic_->failLaunch = true;
    d.click(100, Driver::row(d.index(alpha.id)));
    d.close(false);
    synthetic_->failLaunch = false;
    step.check(!launch_ && state.error() && coverageReopenRequests_ == reopened + 1 && launchesOpened_ == opened && synthetic_->launched == launched,
               "A failed launch keeps the shortcut, shows its error and reopens + Add App");
    d.reopen();

    // 5. Add a shell link: keep the inspected name, then rename it through the
    //    name control; the new draft clears the previous error.
    d.choose(applications / "Beta.lnk");
    step.check(state.editing() && !state.error() && state.draftName() == "Beta Synthetic", "A new draft clears the previous error");
    step.check(preview_->key({app::KeyKind::down, VK_RETURN}, false, d.t) && !preview_->editingName(), "Return keeps the inspected name");
    d.click(230, 90); // the name control
    d.type(u"Beta");
    d.commit(320, 288, "Save shortcut commits the shell link");
    const auto beta = state.file().items.back();
    step.check(state.file().items.size() == 3 && beta.name == "Beta" && beta.iconPreset == "original" && beta.locator["windowsTarget"]["kind"] == J("shellLink"),
               "The shell link is saved after the executable with the App icon preset");
    d.checkNavigation();
    step.check(d.action("app." + alpha.id) == alphaAction, "Adding an app keeps the existing navigation action");

    // 6. Edit the first app: reinspection restores its name and preset; rename
    //    and Star keep its identity, creation date and position.
    d.click(337, Driver::row(d.index(alpha.id)));d.settle();d.frames(.32);
    step.check(state.editing() && state.editingID() == alpha.id && state.draftName() == alpha.name && state.draftIcon() == "camera",
               "Editing reinspects the target and restores the saved name and preset");
    d.type(u"Alpha Prime");
    d.click(144, 164); // icon 2: Star
    d.frames(.2);d.commit(320, 288, "Save shortcut commits the edit");
    const auto& edited = d.record(alpha.id);
    step.check(d.index(alpha.id) == 1 && edited.name == "Alpha Prime" && edited.iconPreset == "star" && edited.createdAt == alpha.createdAt &&
               state.file().items.back().id == beta.id, "An edit keeps the record identity, creation date and list order");
    d.checkNavigation();
    step.check(d.action("app." + alpha.id) == alphaAction, "Rename and icon edits keep the navigation action");

    // 7. The same application cannot be saved twice; Cancel discards the draft.
    d.choose(applications / "Alpha.exe");
    step.check(preview_->key({app::KeyKind::down, VK_RETURN}, false, d.t), "Return keeps the duplicate draft name");
    d.click(320, 288);d.settle();d.frame();
    step.check(state.editing() && state.error() && state.file().items.size() == 3 && d.disk() == state.file(), "A duplicate application is rejected and the draft stays editable");
    d.click(188, 288);d.frames(.32);
    step.check(!state.editing() && !state.error(), "Cancel returns to the list and clears the error");

    // 8. A successful close-first launch from the list records the Event Log
    //    entry. Only in the first cycle: the shared Event Log then shows the
    //    same rows in every later cycle (its raster usage must repeat).
    const unsigned successes = step.cycle == 0 ? 1 : 0;
    if (successes) {
        d.click(100, Driver::row(d.index(beta.id)));
        d.close(false);
        step.check(!launch_ && !state.error() && synthetic_->launched == launched + 1 && launchesOpened_ == opened + 1 && coverageReopenRequests_ == reopened + 1,
                   "The application starts only after the close and is recorded as opened");
        d.reopen();
    }

    // 9. A navigation activation follows the same handoff; a forced close
    //    (sleep, session end) cancels it before anything starts.
    step.check(activate(alphaAction, d.t) && launch_ && !preview_->busy(), "The navigation action of a saved app requests its launch");
    step.check(activate(alphaAction, d.t) && launch_ && launch_->phase == LaunchPhase::verify, "One launch at a time");
    d.close(true);
    step.check(!launch_ && synthetic_->launched == launched + successes, "A forced close cancels the pending launch");
    d.reopen();
    step.check(!activate(d.action("addApp"), d.t) && !launch_, "Module actions are not app launches");

    // 10. Quitting cancels a launch whose close has not finished.
    step.check(activate(d.action("app." + beta.id), d.t), "The second app's navigation action requests its launch");
    d.settle();
    step.check(launch_ && launch_->phase == LaunchPhase::closing, "The verified launch waits for the close");
    (void)hostEvent({app::HostEvent::Kind::quitting, 0}, d.t);
    step.check(!launch_ && synthetic_->launched == launched + successes, "An accepted quit cancels the pending launch");
    concealed(d.t);
    step.check(coverageCloseRequests_ == closes + 3 + successes, "Each verified launch requested exactly one close");

    // 11. Remove both apps: the seeded state, file and navigation return.
    d.commit(369, Driver::row(d.index(alpha.id)), "Remove commits the first app");
    d.commit(369, Driver::row(d.index(beta.id)), "Remove commits the shell link");
    step.check(state.file() == seeded && d.disk() == seeded && !state.error() && !preview_->busy(), "The seeded imported shortcut remains alone");
    d.checkNavigation();
    step.check(!planner_.shortcutID(alphaAction), "A removed app's action launches nothing");
    d.frames(.2);
}

} // namespace

std::unique_ptr<app::ModuleOwner> appShortcutModule(const app::ApplicationModuleContext& context) {
    need(context.host && context.utility && context.window, "the shared host, executor and window are required");
    // The verified packaged source artwork; absent (no verified package, as in
    // development previews) leaves the module absent.
    const auto assets = context.resource ? context.resource("appShortcut.assets") : fs::path{};
    if (assets.empty()) return nullptr;
    std::shared_ptr<SyntheticApplications> synthetic;
    fs::path directory;
    if (context.mode == ApplicationAreaMode::hiddenCoverage) {
        // The area's NEW coverage root, seeded with the Mac oracle's accepted
        // archive (one imported record with an opaque bookmark).
        need(!context.coverageRoot.empty(), "hidden coverage needs the area's coverage root");
        const auto fixture = context.fixture ? context.fixture("appShortcut.macArchive") : fs::path{};
        need(!fixture.empty(), "hidden coverage needs the Mac shortcut oracle fixture");
        const auto bytes = ehud::data::detail::readFile(fixture, 1024 * 1024);
        need(bytes.has_value(), "the Mac shortcut oracle fixture is readable");
        const auto oracle = J::parse(*bytes, 1024 * 1024);
        J archive;
        for (const auto& record : oracle["records"].array()) if (record["name"] == J("mac") && record["result"] == J("accepted")) archive = record["archive"];
        need(archive.isObject(), "the oracle holds the accepted Mac archive");
        directory = context.coverageRoot / "AppShortcuts";
        ehud::data::detail::replaceFile(directory / "shortcuts.json", std::nullopt, archive.encode(m::shortcutMaximumBytes), m::shortcutMaximumBytes);
        synthetic = std::make_shared<SyntheticApplications>();
    } else {
        // AppShortcutStore.applicationDirectory(): <app root>/AppShortcuts.
        need(!context.dataRoot.empty() && context.dataRoot.is_absolute(), "an absolute data root is required");
        directory = context.dataRoot / "AppShortcuts";
    }
    return std::make_unique<AppShortcutArea>(context, assets, std::move(directory), std::move(synthetic));
}
} // namespace endfield::areas
