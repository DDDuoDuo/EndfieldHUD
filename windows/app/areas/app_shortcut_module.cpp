#include "app/areas/app_shortcut_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/app_shortcut_navigation.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/localization.hpp"
#include "core/migration/mac_import_native.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "modules/app_shortcut_navigation.hpp"
#include "modules/app_shortcut_canvas.hpp"
#include "modules/app_shortcut_tasks.hpp"
#include "native/app_shortcut_assets.hpp"
#include "native/app_shortcut_icons.hpp"
#include "native/layer_image_source.hpp"
#include "native/app_shortcut_text.hpp"
#include "native/notes_clipboard.hpp"
#include "tools/hud_design_owner.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace m = modules;
namespace d = modules::design;
namespace g = native;
namespace mig = ehud::migration;
using app::ApplicationAreaMode;
using Entries = std::span<const g::LayerCompositionEntry>;
using J = m::ShortcutJson;

void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("App shortcuts: ") + why); }
// Windows-only texts (the Mac catalog names macOS and EndfieldCharge), in
// the five source languages; every other text comes from the shared catalog.
struct WindowsText { std::string_view english, simplified, traditional, japanese, korean; };
constexpr std::array<WindowsText, 8> windowsTexts{{
    {"Choose a Windows application (.exe) or its shortcut (.lnk).", "请选择 Windows 应用程序（.exe）或其快捷方式（.lnk）。", "請選擇 Windows 應用程式（.exe）或其捷徑（.lnk）。",
     "Windowsアプリ（.exe）またはそのショートカット（.lnk）を選択してください。", "Windows 앱(.exe) 또는 해당 바로가기(.lnk)를 선택하세요."},
    {"Open this shortcut to choose its Windows application.", "请打开此快捷方式，选择对应的 Windows 应用。", "請開啟此捷徑，選擇對應的 Windows 應用程式。",
     "このショートカットを開いて、対応するWindowsアプリを選択してください。", "이 바로가기를 열어 해당 Windows 앱을 선택하세요."},
    {"These app shortcuts were saved by a newer version of EndfieldHUD.", "这些快捷方式由较新版本的 EndfieldHUD 保存。", "這些捷徑由較新版本的 EndfieldHUD 儲存。",
     "これらのショートカットは新しいバージョンのEndfieldHUDで保存されています。", "이 앱 바로가기는 더 최신 버전의 EndfieldHUD에서 저장되었습니다."},
    {"App shortcuts changed outside this window. Restart EndfieldHUD before editing.", "应用快捷方式已在外部更改，请重启 EndfieldHUD 后再编辑。", "應用程式捷徑已在外部更改，請重啟 EndfieldHUD 後再編輯。",
     "アプリのショートカットが外部で変更されました。EndfieldHUDを再起動してから編集してください。", "다른 창이나 앱에서 바로가기가 변경되었습니다. EndfieldHUD를 재시작한 후 편집하세요."},
    {"The application is unavailable.", "应用不可用。", "應用程式不可用。", "アプリを利用できません。", "앱을 사용할 수 없습니다."},
    {"File picker is busy", "文件选择器正忙", "檔案選擇器忙碌中", "ファイル選択画面は使用中です", "파일 선택기가 사용 중입니다"},
    {"File picker is unavailable", "文件选择器不可用", "檔案選擇器無法使用", "ファイル選択画面を利用できません", "파일 선택기를 사용할 수 없습니다"},
    {"Applications", "应用程序", "應用程式", "アプリケーション", "응용 프로그램"},
}};
std::string text(core::Language l, std::string_view english, std::string_view chinese) {
    for (const auto& t : windowsTexts) if (t.english == english) switch (l) {
        case core::Language::simplifiedChinese: return std::string(t.simplified);
        case core::Language::traditionalChinese: return std::string(t.traditional);
        case core::Language::japanese: return std::string(t.japanese);
        case core::Language::korean: return std::string(t.korean);
        default: return std::string(t.english);
    }
    return core::localized(english, chinese, l);
}
// AppShortcutStoreError.invalidApplication for Windows targets.
std::string invalidApplicationText(core::Language l) {
    return text(l, "Choose a Windows application (.exe) or its shortcut (.lnk).", "请选择 Windows 应用程序（.exe）或其快捷方式（.lnk）。");
}
std::string lower(std::string value) { for (auto& c : value) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return value; }
std::string suffix(std::string_view path) { const auto dot = path.rfind('.'); return dot == path.npos ? std::string{} : lower(std::string(path.substr(dot))); }
// A Windows application file the Add App canvas takes (Mac: one .app URL).
bool applicationFile(std::string_view path) { const auto kind = suffix(path); return kind == ".exe" || kind == ".lnk"; }
// A chosen application: an .exe/.lnk file, or a packaged application named
// "shell:AppsFolder\<AUMID>" (the shared dialog's applications mode).
std::optional<g::AppShortcutSelection> chosenApplication(std::string_view path) {
    constexpr std::string_view folder = "shell:appsfolder\\";
    if (path.size() > folder.size() && lower(std::string(path.substr(0, folder.size()))) == folder)
        return g::AppShortcutSelection{g::AppShortcutSelectionKind::packagedApp, std::string(path.substr(folder.size()))};
    if (applicationFile(path)) return g::AppShortcutSelection{g::AppShortcutSelectionKind::file, std::string(path)};
    return std::nullopt;
}

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
// `image(id)` names a saved app's published original application icon
// (SystemHUDView.appNavigationPresentations: icon only for the "original"
// preset), shared from `images` with the navigation that paints it.
class NavigationPlanner {
public:
    template <class Image>
    app::AppShortcutNavigation::Plan plan(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right, const m::ShortcutFile& file,
                                          Image&& image, const std::shared_ptr<g::LayerImageSource>& images) {
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
                } else {
                    g::WatchAppearanceEntry entry{e.action, e.target, e.title, e.iconKey, false};
                    if (e.iconKey == "shortcut:original")
                        if (const auto id = navigation_->shortcutID(e.action))
                            if (const auto reference = image(*id); reference && !reference->key.empty() && reference->revision) {
                                entry.originalImageKey = reference->key;entry.originalImageRevision = reference->revision;entry.originalImages = images;
                            }
                    entries_.push_back(std::move(entry));
                }
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
    std::atomic<unsigned> inspected{}, launched{}, icons{}, logos{};
    std::atomic<bool> failLaunch{};
};
// The worker keeps its own reference: accepted work may outlive the owner.
g::AppShortcutOS syntheticOS(std::shared_ptr<SyntheticApplications> self) {
        return {[self](const g::AppShortcutSelection& s) {
                    ++self->inspected;
                    if (s.kind == g::AppShortcutSelectionKind::packagedApp) {
                        // A synthetic packaged application (never an installed one).
                        if (!s.value.starts_with("Synthetic.")) throw m::ShortcutError(m::ShortcutErrorCode::unavailable);
                        g::AppShortcutResolved r;r.selection = s;r.name = "Packaged Synthetic";r.applicationKey = "package:" + lower(s.value);
                        r.launchKind = g::AppShortcutLaunchKind::packagedApp;r.appUserModelID = s.value;
                        return r;
                    }
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

// Hidden coverage: a deterministic opaque square per synthetic file target;
// a synthetic packaged application's logo is the declared fixture
// (tools/app_shortcut_logo_fixture.py) through the production decoder.
g::AppShortcutIconExtractor syntheticIcons(std::shared_ptr<SyntheticApplications> self, std::vector<std::uint8_t> logo) {
    auto packaged = g::windowsAppShortcutIconExtractor([self, logo = std::move(logo)](std::string_view id, unsigned) -> std::optional<std::vector<std::uint8_t>> {
        ++self->logos;
        if (!id.starts_with("Synthetic.") || logo.empty()) return std::nullopt; // never an installed application
        return logo;
    });
    return [self, packaged = std::move(packaged)](const g::AppShortcutSelection& s, unsigned size) -> std::optional<g::AppShortcutIconPixels> {
        if (s.kind == g::AppShortcutSelectionKind::packagedApp) return packaged(s, size);
        ++self->icons;
        std::uint32_t hash = 2166136261u;for (const unsigned char c : s.value) hash = (hash ^ c) * 16777619u;
        g::AppShortcutIconPixels out;out.width = out.height = size;out.straightRGBA.resize(std::size_t(size) * size * 4);
        for (std::size_t n = 0; n < out.straightRGBA.size(); n += 4) {
            out.straightRGBA[n] = std::uint8_t(hash);out.straightRGBA[n + 1] = std::uint8_t(hash >> 8);out.straightRGBA[n + 2] = std::uint8_t(hash >> 16);out.straightRGBA[n + 3] = 255;
        }
        return out;
    };
}

// Hidden coverage: an in-memory clipboard; the system clipboard is never read
// or written there.
struct SyntheticClipboard final : g::NotesClipboard {
    std::optional<g::NotesClipboardText> value;
    std::vector<std::string> files;
    unsigned textReads{}, fileReads{}, writes{};
    std::optional<g::NotesClipboardText> readText(std::uint32_t maximum) override {
        ++textReads;
        if (!value || value->text.size() > maximum) return std::nullopt;
        return value;
    }
    std::vector<std::string> readFiles() override { ++fileReads; return files; }
    bool writeText(const g::NotesClipboardText& text) override { ++writes; value = text; return true; }
};

// Hidden coverage: the seeded Mac archive, the import's ledger rows for it and
// the synthetic packaged logo (a coverage restart of the store writes the
// first two back; see Driver::restartStore).
struct CoverageSeed { std::string archive, ledger; std::vector<std::uint8_t> logo; };

// One original application icon as extracted on the worker: the 96 x 96
// image the canvas tile shows (AppShortcutStore.icon(for:)), the same icon
// trimmed to its visible pixels for the side list (HUDSourceDesktopIconLayout
// .image) and the tile's darkened average colour (AppShortcutCanvas.averageColor).
struct ExtractedIcon {
    g::AppShortcutIconPixels pixels;
    std::optional<m::TrimmedShortcutIcon> trimmed;
    std::optional<std::array<double, 3>> color;
};

class AppShortcutArea final : public app::ModuleOwner, public app::AppShortcutNavigation {
public:
    AppShortcutArea(const app::ApplicationModuleContext& c, fs::path assets, fs::path directory, fs::path relinkRoot, std::shared_ptr<SyntheticApplications> synthetic, CoverageSeed seed)
        : host_(*c.host), window_(static_cast<HWND>(c.window)), utility_(*c.utility), raster_(c.rasterizer), mode_(c.mode), directory_(std::move(directory)),
          relinkRoot_(std::move(relinkRoot)), coverageRoot_(c.coverageRoot), synthetic_(std::move(synthetic)), seed_(std::move(seed)), assets_(std::move(assets)),
          alive_(std::make_shared<bool>(true)) {
        need(window_ && IsWindow(window_), "the HUD window is required");
        auto* context = host_.designContext();
        need(context != nullptr, "the shared design context is required");
        language_ = host_.appearance().language;
        // Edit-menu clipboard of the name fields and Ctrl+V application import.
        if (synthetic_) {syntheticClipboard_ = std::make_shared<SyntheticClipboard>();clipboard_ = syntheticClipboard_;}
        else clipboard_ = g::systemNotesClipboard();
        extractor_ = synthetic_ ? syntheticIcons(synthetic_, seed_.logo) : g::windowsAppShortcutIconExtractor();
        iconRoute_ = utility_.makeRoute();
        tools::DesignOwnerOptions options;
        need(host_.designOwnerOptions(options, "appShortcuts", routing()), "the design owner services are required");
        options.raster.assetRoot = assets_.root();          // the tile artwork (pinned PNGs)
        options.raster.memoryImages = images_.get();        // original application icons
        core_ = std::make_unique<tools::DesignOwnerCore>(c.rasterizer, *context, std::move(options));
        auto canvas = std::make_unique<m::AppShortcutCanvas>(*context, assets_.artwork(), services());
        canvas_ = canvas.get();
        core_->add(std::move(canvas));
        // SystemHUDView.registerModule(appShortcutCanvas, for: .addApp): a design screen.
        host_.setDesignScreen(core::Module::addApp);
        if (c.metrics.pixelWidth && c.metrics.pixelHeight) core_->resize(c.metrics);
        tasks_ = makeTasks();
        host_.publish<app::AppShortcutNavigation>(app::appShortcutNavigationService, *this);
        // AppShortcutStore(directory:) at startup: the one initial load.
        loadPending_ = true;pump();
    }
    ~AppShortcutArea() override {
        *alive_ = false;observer_ = {};
        host_.withdraw(app::appShortcutNavigationService);
        tasks_.reset(); // suppresses completions; accepted writes/launches still finish on the executor
        utility_.invalidate(iconRoute_); // disposable icon work: no callback into this owner
        core_.reset(); // the canvas and its scene, before the image source their rasters borrow
    }
    AppShortcutArea(const AppShortcutArea&) = delete;
    AppShortcutArea& operator=(const AppShortcutArea&) = delete;

    // --- ModuleOwner -----------------------------------------------------------
    std::string_view name() const noexcept override { return "appShortcuts"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;r.pointerCapture = 20;r.pointer = 30;r.wheel = 30;r.keyCapture = 20;r.key = 30;r.filterKey = 30;r.filterRequiresOwnerTarget = true;
        r.message = 30;r.messageRetriesPendingClose = true;r.finishEditing = 30;r.flush = 30;return r;
    }
    bool presents(core::Module module) const noexcept override { return module == core::Module::addApp; }
    void resize(const app::ClientMetrics& metrics) override { if (metrics.pixelWidth && metrics.pixelHeight) core_->resize(metrics); }
    void setAppearance(const app::ModuleAppearance& a, double t) override { language_ = a.language;core_->setAppearance(a, t); }
    void setOverlayVisible(bool visible, double t) override {
        if (!visible) {closing(t, false);return;}
        const bool opening = !overlayVisible_;
        overlayVisible_ = true;
        if (opening) canvas_->resetSession(); // OverlayController creates SystemHUDView (and a fresh canvas) on every open
        core_->setOverlayVisible(true, t);
        // OverlayController.openSystemOverlay: the last failed launch's error
        // shows on every opening until a later launch is verified.
        if (opening && launchError_) canvas_->showError(*launchError_);
        // Mac OverlayController retries a failed store on the next opening.
        if (loadFailed_ && !loadPending_) {loadFailed_ = false;loadPending_ = true;}
        pump();
    }
    void overlayClosing(double t) override { closing(t, true); }
    void update(const app::ModuleFrame& f) override {
        core_->update(f);pump();
        if (const auto offset = listOffset(); offset != iconOffset_) refreshIcons();
    }
    void prewarm(const app::ModuleFrame& f) override { core_->prewarm(f); }
    bool requiresFrames(double t) const override { return core_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double now) const override { return core_->nextWakeTime(now); }
    bool deadline(double t) override { const bool changed = core_->deadline(t);pump();return changed; }
    void upload(g::Renderer& r) override { core_->upload(r); }
    Entries entries(core::Module module) override { return core_->entries(module); }
    void collected(g::Renderer& r) override { core_->collected(r); }
    void release(g::Renderer& r) override { core_->release(r); }
    bool covers(core::Point p) const override { return core_->covers(p); }
    bool pointerLocked() const override { return core_->pointerLocked(); }
    bool capturesPointer() const override { return core_->capturesPointer(); }
    bool capturesWheel() const override { return core_->capturesWheel(); }
    bool capturesKeys() const override { return core_->capturesKeys(); }
    bool suppressesKeyFilters() const override { return core_->suppressesKeyFilters(); }
    bool pointer(const app::PointerEvent& e, double t) override { const bool used = core_->pointer(e, t);pump();return used; }
    bool wheel(const app::WheelEvent& e, double t) override { const bool used = core_->wheel(e, t);pump();return used; }
    bool key(const app::KeyEvent& e, double t) override { const bool used = core_->key(e, t);pump();return used; }
    bool filterKey(const app::NativeMessage& m) override { return core_->filterKey(m); }
    bool message(const app::NativeMessage& m, double t) override { return core_->message(m, t); }
    void focus(bool value, double t) override { core_->focus(value, t); }
    void cancelInteraction(double t) override { canvas_->setDropTarget(false);core_->cancelInteraction(t); }
    bool finishEditing(double t) override { return core_->finishEditing(t); }
    bool flush(double t) override {
        (void)core_->flush(t);
        // Bounded durable drain at quit/session end: an accepted save/remove
        // (or one retained while the executor was full) reaches the disk.
        for (unsigned n = 0; n < 16 && tasks_->busy() && inflight_ && durable(inflight_->kind); ++n) {
            utility_.waitIdle();utility_.drain();tasks_->queueCapacityAvailable();
        }
        return !(tasks_->busy() && inflight_ && durable(inflight_->kind));
    }
    // SystemHUDView.performDragOperation -> acceptsAppShortcutDrop: exactly one
    // application over the centre while + Add App is the active canvas ->
    // AppShortcutCanvas.importPasteboard.
    bool drop(std::span<const std::string> paths, core::Point point, double t) override {
        canvas_->setDropTarget(false); // endFileDrop
        if (!acceptsDrop(paths, point)) return false;
        const bool used = canvas_->importDrop(paths);
        pump();host_.requestFrame();(void)t;
        return used;
    }
    // draggingUpdated: the drop target shows while the canvas accepts (the
    // plane holds still); a leave, cancel or drop (point none) ends it.
    bool dropHover(std::span<const std::string> paths, std::optional<core::Point> point, double) override {
        const bool accepts = point && acceptsDrop(paths, *point);
        if (accepts != canvas_->dropTarget()) {canvas_->setDropTarget(accepts);host_.requestFrame();}
        return accepts;
    }
    std::optional<app::ModuleCursor> cursorAt(core::Point p) const override { return overlayVisible_ ? core_->cursorAt(p) : std::nullopt; }
    // UI Automation: the library's tree (HUDDesignCanvas.accessibilityItems).
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return core_->automationRefresh(root); }
    std::size_t automationCount() const override { return core_->automationCount(); }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override { return core_->automationChild(index, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return core_->automationFromPoint(x, y, out); }
    void automationDisconnect() noexcept override { core_->automationDisconnect(); }
    std::optional<core::Module> automationModule() const noexcept override { return core_->automationModule(); }
    app::ModuleResidency residency() const override { return core_->residency(); }
    bool hostEvent(const app::HostEvent& e, double t) override {
        using K = app::HostEvent::Kind;
        bool changed = core_->hostEvent(e, t);
        switch (e.kind) {
        case K::utilityCompleted:
            tasks_->queueCapacityAvailable();pump();scheduleIcon();
            return std::exchange(contentChanged_, false) || changed; // completions ran in the drain: re-list UI Automation
        case K::overlayConcealed: concealed(t);return changed;
        case K::suspended: case K::quitting: cancelPendingLaunch(t);return changed; // forceCloseSystemOverlay / requestQuit
        default: return changed;
        }
    }
    void coverage(app::ModuleCoverage& step) override;

    // --- AppShortcutNavigation -------------------------------------------------
    Plan plan(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right) override { return planNavigation(modules, right); }
    bool activate(std::uint64_t action, double t) override {
        const auto id = planner_.shortcutID(action);
        if (!id) return false;
        const auto& items = file_.items;
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto& v) { return v.id == *id; });
        if (found != items.end()) beginLaunch(*found, t);
        return true;
    }
    void setObserver(std::function<void()> observer) override { observer_ = std::move(observer); }

private:
    enum class Purpose { load, request, verify, launch };
    struct Inflight { std::uint64_t token{}; Purpose purpose{}; m::ShortcutTaskKind kind{}; };
    // Mac pendingAppLaunch / appLaunchInFlight: verify -> closing (our close
    // was requested) -> closed (its overlayClosing arrived) -> dispatch (the
    // close finished: overlayConcealed) -> running (the launch was accepted).
    enum class LaunchPhase { verify, closing, closed, dispatch, running };
    struct Launch { m::ShortcutRecord record; LaunchPhase phase{}; };
    // One canvas request (AppShortcutCanvas's synchronous store call) as its
    // two worker steps: inspect / reinspect (a candidate), then save; or remove.
    struct Work { m::AppShortcutRequest request; bool saving{}; std::optional<m::ShortcutCandidate> candidate; };

    static bool durable(m::ShortcutTaskKind k) noexcept { return k == m::ShortcutTaskKind::save || k == m::ShortcutTaskKind::remove || k == m::ShortcutTaskKind::launch; }
    bool coverageMode() const noexcept { return mode_ == ApplicationAreaMode::hiddenCoverage; }
    // Executor completions run outside the registry's isolation: in
    // production a failure is logged without user content and the owner
    // keeps its last consistent state; hidden coverage surfaces it.
    template <class F> void guarded(const char* what, F&& f) {
        if (coverageMode()) {f();return;}
        try {f();}
        catch (const std::exception& e) {
            const auto line = std::string("EndfieldHUD app shortcuts ") + what + " failed (" + typeid(e).name() + ")\n";
            OutputDebugStringA(line.c_str());
        }
    }
    double now() const { return host_.now(); }
    // The one transaction owner over the repository, the inspector and (with
    // a relink root) the Mac import ledger.
    std::unique_ptr<m::AppShortcutTasks> makeTasks() {
        return std::make_unique<m::AppShortcutTasks>(utility_, directory_, g::nativeShortcutTextRules(),
            synthetic_ ? syntheticOS(synthetic_) : g::windowsAppShortcutOS(), [this](m::ShortcutTaskResult r) { guarded("completion", [&] { completed(std::move(r)); }); },
            relinkRoot_);
    }
    // The canvas's platform side (AppShortcutCanvasServices).
    m::AppShortcutCanvasServices services() {
        m::AppShortcutCanvasServices s;
        s.request = [this](const m::AppShortcutRequest& r) { work_.push_back({r, false, {}});pump();return true; };
        s.choose = [this] { return choose(); };
        s.cancelChooser = [this] { cancelChooser(); };
        s.launch = [this](std::string_view id) {
            const auto found = std::find_if(file_.items.begin(), file_.items.end(), [&](const auto& v) { return v.id == id; });
            if (found != file_.items.end()) beginLaunch(*found, now());
        };
        s.isApplication = [](std::string_view path) { return chosenApplication(path).has_value(); };
        s.pasteboard = [this] { return clipboard_ ? clipboard_->readFiles() : std::vector<std::string>{}; };
        s.trimmed = [rules = g::nativeShortcutTextRules()](std::string_view value) { return rules.trimmed(value); };
        s.icon = [this](std::string_view id) -> std::optional<d::ImageSource> {
            const auto found = icons_.find(id);
            if (found == icons_.end() || !found->second.revision) return std::nullopt;
            return d::ImageSource{d::ImageSource::Kind::memory, iconKey(id), {}, found->second.revision};
        };
        s.tileColor = [this](std::string_view id) -> std::optional<d::Color> {
            const auto found = icons_.find(id);
            if (found == icons_.end() || !found->second.revision || !found->second.color) return std::nullopt;
            const auto& c = *found->second.color;return d::Color{c[0], c[1], c[2], 1};
        };
        return s;
    }

    bool submit(m::ShortcutTask task, Purpose purpose) {
        task.token = ++token_;const auto kind = task.kind;
        if (!tasks_->submit(std::move(task))) return false;
        inflight_ = Inflight{token_, purpose, kind};return true;
    }
    void pump() {
        if (pumping_ || !tasks_) return;
        pumping_ = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{pumping_};
        for (unsigned n = 0; n < 4 && pumpOnce(); ++n) {}
    }
    const m::ShortcutRecord* record(std::string_view id) const {
        const auto found = std::find_if(file_.items.begin(), file_.items.end(), [&](const auto& v) { return v.id == id; });
        return found == file_.items.end() ? nullptr : &*found;
    }
    // One step of the single transaction owner; true when it settled a step
    // without occupying it (an answered request).
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
        if (work_.empty()) return false;
        auto& w = work_.front();
        using K = m::AppShortcutRequest::Kind;
        m::ShortcutTask t;
        if (w.saving) {
            // AppShortcutStore.save(candidate:name:iconPreset:editingID:).
            t.kind = m::ShortcutTaskKind::save;t.candidate = w.candidate;
            if (w.request.kind == K::add && w.request.id.empty()) {t.name = w.candidate->name;t.icon = "original";} // importURLs: its own name, the App icon
            else {
                const auto* current = record(w.request.id);
                if (!current) {finishWork(std::nullopt, errorText(language_, m::ShortcutErrorCode::missing, false, {}));return true;}
                t.editingID = current->id;
                // A relink keeps the record's name and preset (Windows only).
                t.name = w.request.kind == K::add ? current->name : w.request.name;
                t.icon = w.request.kind == K::add ? current->iconPreset : w.request.icon;
            }
            t.newID = ehud::data::makeUUID();t.createdAt = ehud::data::foundationNow();
        } else if (w.request.kind == K::add) {
            const auto selection = chosenApplication(w.request.path);
            if (!selection) {finishWork(std::nullopt, invalidApplicationText(language_));return true;}
            t.kind = m::ShortcutTaskKind::inspect;t.selection = selection;
        } else {
            const auto* current = record(w.request.id);
            if (!current) {finishWork(std::nullopt, errorText(language_, m::ShortcutErrorCode::missing, false, {}));return true;}
            // save: store.inspect(url: store.resolvedURL(for: id)); remove: store.remove(id:).
            t.kind = w.request.kind == K::save ? m::ShortcutTaskKind::edit : m::ShortcutTaskKind::remove;t.record = *current;
        }
        (void)submit(std::move(t), Purpose::request);
        return false;
    }
    void finishWork(std::optional<m::ShortcutFile> committed, std::optional<std::string> error) {
        if (work_.empty()) return;
        const auto request = work_.front().request;
        work_.pop_front();
        if (committed) {file_ = *committed;notify();}
        canvas_->requestFinished(request, std::move(committed), std::move(error));
        host_.requestFrame();
    }

    // AppShortcutCanvas.chooseApplication through the host's one shared
    // dialog (it owns the focus-loss exemption and the cancellation on a
    // module switch or close) in its applications mode (NSOpenPanel
    // allowedContentTypes [.applicationBundle] starting in /Applications):
    // one application, starting in AppsFolder - an existing .exe/.lnk through
    // the application filter, or a packaged application returned as
    // "shell:AppsFolder\<AUMID>". Links are not dereferenced, so a chosen .lnk
    // is saved as the shell link itself. Hidden coverage answers it itself.
    static app::ApplicationFileRequest chooserRequest(core::Language l) {
        app::ApplicationFileRequest request{text(l, "Choose an application", "选择应用"), text(l, "Choose", "选择"), text(l, "Choose", "选择")};
        request.singleFile = true;request.applications = true;
        request.filterName = text(l, "Applications", "应用程序");request.filterSpec = "*.exe;*.lnk";
        return request;
    }
    bool choose() {
        if (chooser_) return false;
        if (coverageMode()) {++coverageChooserRequests_;chooser_ = ++chooserGeneration_;return true;}
        const auto generation = ++chooserGeneration_;
        std::weak_ptr<bool> alive = alive_;
        if (!host_.chooseFiles(chooserRequest(language_), [this, alive, generation](app::ApplicationFileResult result) {
                if (const auto token = alive.lock(); token && *token) chosen(generation, std::move(result));
            })) {
            canvas_->showError(text(language_, "File picker is busy", "文件选择器正忙"));
            return false;
        }
        chooser_ = generation;return true;
    }
    void cancelChooser() {
        if (!std::exchange(chooser_, std::nullopt)) return;
        if (!coverageMode()) host_.cancelFileChoice();
    }
    void chosen(std::uint64_t generation, app::ApplicationFileResult result) {
        if (chooser_ != generation) return;
        chooser_.reset();
        if (result.cancelled) canvas_->chooserFinished(std::nullopt);
        else if (result.failed) {canvas_->chooserFinished(std::nullopt);canvas_->showError(text(language_, "File picker is unavailable", "文件选择器不可用"));}
        // An empty choice is an item the dialog could not name as an
        // application (AppsFolder entries that are not packaged apps).
        else if (result.paths.empty()) {canvas_->chooserFinished(std::nullopt);canvas_->showError(invalidApplicationText(language_));}
        else canvas_->chooserFinished(std::move(result.paths));
        pump();host_.requestFrame();
    }
    // SystemHUDView.acceptsAppShortcutDrop: the active canvas, the centre, one application.
    bool acceptsDrop(std::span<const std::string> paths, core::Point point) const {
        return overlayVisible_ && core_->inputModule() == std::optional(core::Module::addApp) && core_->covers(point) && canvas_->acceptsApplications(paths);
    }

    // OverlayController.requestAppShortcutLaunch.
    void beginLaunch(const m::ShortcutRecord& item, double t) {
        if (launch_ || !overlayVisible_) return;
        if (!item.native()) {
            // An imported Mac record keeps its opaque bookmark; it launches
            // only after it is relinked to a Windows application: its Open
            // in + Add App (where the error shows) asks for one.
            showLaunchError(text(language_, "The application is unavailable: ", "应用不可用：") +
                text(language_, "Open this shortcut to choose its Windows application.", "请打开此快捷方式，选择对应的 Windows 应用。"), t);
            host_.requestFrame();return;
        }
        launch_ = Launch{item, LaunchPhase::verify};
        pump();
    }
    void finishLaunch(std::optional<std::string> error, double t) {
        if (!std::exchange(launch_, std::nullopt)) return;
        if (error) showLaunchError(std::move(*error), t);
    }
    // SystemHUDView.showAppShortcutError: a launch reports its error in
    // + Add App, which the open HUD selects when another module is shown
    // (after a close the failed launch reopens + Add App instead).
    void showLaunchError(std::string message, double) {
        canvas_->showError(std::move(message));
        if (!overlayVisible_ || host_.selectedModule() == core::Module::addApp) return;
        if (coverageMode()) {++coverageSelectRequests_;return;} // the coverage harness owns module selection
        host_.selectModule(core::Module::addApp);
    }
    void cancelPendingLaunch(double t) {
        if (!launch_ || launch_->phase == LaunchPhase::running) return;
        if (inflight_ && inflight_->purpose == Purpose::verify && tasks_->cancel(inflight_->token)) inflight_.reset();
        finishLaunch({}, t);
    }
    void closing(double t, bool animated) {
        if (launch_) switch (launch_->phase) {
            case LaunchPhase::verify: cancelPendingLaunch(t);break;          // the user closed the HUD first
            case LaunchPhase::closing: launch_->phase = LaunchPhase::closed;break; // our close began
            case LaunchPhase::closed: cancelPendingLaunch(t);break;          // a forced close interrupted it
            default: break;
        }
        overlayVisible_ = false;canvas_->setDropTarget(false);
        if (animated) core_->overlayClosing(t);else core_->setOverlayVisible(false, t);
        pump();
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

    // --- Original application icons ---------------------------------------------
    // Every saved app with the App icon preset and a Windows target, the
    // visible rows first (m::shortcutOriginalIconOrder), loaded one at a time
    // on the shared executor. Like the Mac's icon cache every one is kept;
    // only a list beyond m::shortcutOriginalIconCapacity leaves the least
    // recently shown with the fallback glyph. Each icon is published twice in
    // this owner's image source: as extracted (the canvas tile) and trimmed
    // (the side list), with the room the rasters' borrowed generations need.
    struct IconWant { std::string id, applicationKey; g::AppShortcutSelection selection; };
    struct IconEntry { std::string applicationKey; std::uint64_t revision{}, used{}; bool attempted{}, trimmed{}; std::optional<std::array<double, 3>> color; };
    static constexpr std::size_t maximumIcons = m::shortcutOriginalIconCapacity;
    static constexpr std::size_t iconBytes = std::size_t(g::appShortcutIconPixels) * g::appShortcutIconPixels * 4;
    static constexpr g::LayerImageSourceLimits iconLimits{2 * (maximumIcons + 4), 4 * (maximumIcons + 4), 4 * (maximumIcons + 4) * iconBytes};
    static std::string iconKey(std::string_view id) { return "appShortcut.icon." + std::string(id); }
    static std::string navigationKey(std::string_view id) { return "appShortcut.navigation." + std::string(id); }
    static std::string applicationKey(const J& locator) {
        try {const auto& key = locator["windowsTarget"]["applicationKey"];return key.isString() ? key.string() : std::string{};}
        catch (...) {return {};}
    }
    double listOffset() const {
        if (!canvas_->renderedLanguage()) return 0;
        const auto areas = canvas_->page().scrollAreas();
        return areas.empty() ? 0 : areas.front()->offset();
    }
    void retire(const std::string& id, IconEntry& entry) {
        if (!entry.revision) return;
        images_->retire(iconKey(id));if (entry.trimmed) images_->retire(navigationKey(id));
        entry.revision = 0;entry.trimmed = false;entry.color.reset();
    }
    void refreshIcons() {
        iconOffset_ = listOffset();
        wanted_.clear();
        const auto& items = file_.items;
        for (const auto n : m::shortcutOriginalIconOrder(file_, iconOffset_, maximumIcons,
                 [](const m::ShortcutRecord& item) { return g::appShortcutIconSelection(item.locator).has_value(); }))
            if (const auto selection = g::appShortcutIconSelection(items[n].locator)) wanted_.push_back({items[n].id, applicationKey(items[n].locator), *selection});
        for (const auto& w : wanted_) icons_[w.id].used = ++iconUse_;
        // Removed shortcuts, shortcuts without a Windows target (an imported
        // record restored by a store restart) or the App icon preset, and the
        // least recently shown beyond the bound.
        bool released = false;
        std::erase_if(icons_, [&](auto& entry) {
            const auto item = std::find_if(items.begin(), items.end(), [&](const auto& v) { return v.id == entry.first; });
            const bool removed = item == items.end() || !g::appShortcutIconSelection(item->locator) || item->iconPreset != "original";
            if (removed) {released = released || entry.second.revision;retire(entry.first, entry.second);}
            return removed;
        });
        while (icons_.size() > maximumIcons) {
            const auto oldest = std::min_element(icons_.begin(), icons_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            released = released || oldest->second.revision;retire(oldest->first, oldest->second);
            icons_.erase(oldest);
        }
        if (released) iconsPublished();
        scheduleIcon();
    }
    // A published icon changed: the canvas tiles and the navigation follow.
    void iconsPublished() {
        canvas_->iconsChanged();
        std::map<std::string, std::uint64_t, std::less<>> navigation;
        for (const auto& [id, entry] : icons_) if (entry.trimmed) navigation.emplace(id, entry.revision);
        if (navigation != navigationImages_) {navigationImages_ = std::move(navigation);if (observer_) observer_();}
        host_.requestFrame();
    }
    std::optional<m::ShortcutImageReference> navigationImage(std::string_view id) const {
        const auto found = icons_.find(id);
        if (found == icons_.end() || !found->second.trimmed) return std::nullopt;
        return m::ShortcutImageReference{navigationKey(id), found->second.revision};
    }
    Plan planNavigation(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right) {
        return planner_.plan(modules, right, file_, [this](std::string_view id) { return navigationImage(id); }, images_);
    }
    void scheduleIcon() {
        if (iconJob_) return;
        for (const auto& w : wanted_) {
            const auto found = icons_.find(w.id);
            if (found != icons_.end() && found->second.attempted && found->second.applicationKey == w.applicationKey) continue;
            auto result = std::make_shared<std::optional<ExtractedIcon>>();
            std::weak_ptr<bool> alive = alive_;
            if (!utility_.submit(iconRoute_, [extractor = extractor_, selection = w.selection, result] {
                    auto pixels = extractor(selection, g::appShortcutIconPixels);
                    if (!pixels) return;
                    ExtractedIcon icon{std::move(*pixels), std::nullopt, std::nullopt};
                    icon.trimmed = m::trimDesktopIcon(icon.pixels.width, icon.pixels.height, icon.pixels.straightRGBA);
                    icon.color = m::shortcutTileColor(icon.pixels.width, icon.pixels.height, icon.pixels.straightRGBA);
                    *result = std::move(icon);
                }, [this, alive, id = w.id, key = w.applicationKey, result](std::exception_ptr error) {
                    if (const auto token = alive.lock(); token && *token) guarded("icon", [&] { iconDone(id, key, error ? std::nullopt : std::move(*result)); });
                })) return; // full: retried on the next utilityCompleted
            iconJob_ = w.id;return;
        }
    }
    void iconDone(const std::string& id, const std::string& key, std::optional<ExtractedIcon> icon) {
        iconJob_.reset();
        // A result for a target that is no longer wanted (removed, relinked)
        // is dropped; the current one is scheduled next.
        const auto current = std::find_if(wanted_.begin(), wanted_.end(), [&](const auto& w) { return w.id == id && w.applicationKey == key; });
        const auto found = icons_.find(id);
        if (current == wanted_.end() || found == icons_.end()) {scheduleIcon();return;}
        auto& entry = found->second;
        entry.applicationKey = key;entry.attempted = true;
        retire(id, entry);
        if (icon) {
            const auto revision = ++iconRevision_;
            try {
                (void)images_->publish(iconKey(id), revision, icon->pixels.width, icon->pixels.height, icon->pixels.straightRGBA);
                entry.revision = revision;entry.color = icon->color;
                if (icon->trimmed) {(void)images_->publish(navigationKey(id), revision, icon->trimmed->width, icon->trimmed->height, icon->trimmed->straightRGBA);entry.trimmed = true;}
            }
            catch (const std::bad_alloc&) {throw;}
            catch (const std::exception&) {retire(id, entry);} // rejected image: the fallback glyph remains
        }
        iconsPublished();scheduleIcon();
    }

    // The committed list changed: icons follow it at once (also before the
    // HUD first opens, so the navigation has them), then the host rebinds.
    void notify() {
        ++listRevision_;
        refreshIcons();
        if (observer_) observer_();
    }
    void completed(m::ShortcutTaskResult r) {
        // The import ledger was updated on the worker after the committed
        // file (Windows-only bookkeeping; nothing is shown for it).
        if (r.relink) {++relinkSyncs_;lastRelink_ = *r.relink;}
        if (r.relinkFailed) {
            ++relinkFailures_;
            if (!coverageMode()) OutputDebugStringA("EndfieldHUD app shortcuts: the Mac import relink list could not be updated\n");
        }
        if (!inflight_ || inflight_->token != r.token) {pump();return;}
        contentChanged_ = true;
        const auto f = *std::exchange(inflight_, std::nullopt);const auto t = now();
        const auto failure = [&](bool osWork) { return errorText(language_, r.code, osWork, r.error.value_or(std::string{})); };
        switch (f.purpose) {
        case Purpose::load:
            if (r.file) {loadFailed_ = false;file_ = *r.file;canvas_->setStore(*r.file);notify();}
            else {loadFailed_ = true;canvas_->setStoreError(failure(false));}
            break;
        case Purpose::request:
            if (work_.empty()) break;
            if (f.kind == m::ShortcutTaskKind::inspect || f.kind == m::ShortcutTaskKind::edit) {
                if (r.candidate) {work_.front().saving = true;work_.front().candidate = std::move(r.candidate);}
                else finishWork(std::nullopt, failure(true));
            } else if (r.file) finishWork(std::move(r.file), std::nullopt);
            else finishWork(std::nullopt, failure(f.kind == m::ShortcutTaskKind::save && r.code == m::ShortcutErrorCode::unavailable));
            break;
        case Purpose::verify:
            if (!launch_ || launch_->phase != LaunchPhase::verify) break;
            if (r.error) {finishLaunch(failure(true), t);break;}
            if (!overlayVisible_) {finishLaunch({}, t);break;}
            launchError_.reset(); // requestAppShortcutLaunch: appLaunchError = nil
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
                // Keep the failed target editable and report it in Add App,
                // also on every later opening until a launch is verified.
                launchError_ = failure(true);
                finishLaunch(launchError_, t);
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
    g::LayerRasterizer& raster_; // the shared rasterizer (hidden coverage reads its usage)
    ApplicationAreaMode mode_{};
    fs::path directory_, relinkRoot_, coverageRoot_;
    std::shared_ptr<SyntheticApplications> synthetic_;
    CoverageSeed seed_; // hidden coverage only
    g::NativeAppShortcutAssets assets_;
    std::shared_ptr<bool> alive_;
    // Outlives the design owner whose rasters borrow it; shared with the
    // navigation entries that name its snapshots.
    std::shared_ptr<g::LayerImageSource> images_{std::make_shared<g::LayerImageSource>(iconLimits)};
    std::map<std::string, std::uint64_t, std::less<>> navigationImages_;
    g::AppShortcutIconExtractor extractor_;
    app::UtilityExecutor::Route iconRoute_{};
    std::vector<IconWant> wanted_;
    std::map<std::string, IconEntry, std::less<>> icons_;
    std::optional<std::string> iconJob_;
    std::uint64_t iconRevision_{}, iconUse_{};
    double iconOffset_{-1};
    core::Language language_{core::Language::english};
    std::shared_ptr<g::NotesClipboard> clipboard_;
    std::shared_ptr<SyntheticClipboard> syntheticClipboard_; // hidden coverage
    std::unique_ptr<tools::DesignOwnerCore> core_;
    m::AppShortcutCanvas* canvas_{};            // owned by core_
    std::unique_ptr<m::AppShortcutTasks> tasks_; // destroyed before the canvas its completions update
    m::ShortcutFile file_;                       // the committed list (the store's items)
    std::deque<Work> work_;
    NavigationPlanner planner_;
    std::function<void()> observer_;
    std::optional<Inflight> inflight_;
    std::optional<Launch> launch_;
    std::optional<std::uint64_t> chooser_;
    std::uint64_t chooserGeneration_{};
    std::optional<std::string> launchError_; // OverlayController.appLaunchError
    std::uint64_t token_{}, listRevision_{};
    bool loadPending_{}, loadFailed_{}, overlayVisible_{}, pumping_{}, contentChanged_{};
    unsigned coverageChooserRequests_{}, coverageCloseRequests_{}, coverageReopenRequests_{}, coverageSelectRequests_{}, launchesOpened_{};
    unsigned relinkSyncs_{}, relinkFailures_{};
    m::ShortcutRelinkSync lastRelink_;
};

// One combined module cycle of hidden coverage on the + Add App canvas. Every
// cycle runs the same add / rename / Choose icon / error / launch / drop /
// paste / remove / relink scenario through projected pointer input, the
// canvas's accessibility actions and the UIA tree, and ends in the seeded
// state, so the shared raster usage repeats exactly.
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

    m::AppShortcutCanvas& canvas() const { return *a.canvas_; }
    const m::ShortcutFile& file() const { return a.file_; }
    d::Context& context() const { return *a.host_.designContext(); }
    void frame(double dt = 1. / 60) { t += dt;step.present(t); }
    void frames(double seconds) { const double end = t + seconds;while (t < end) frame(); }
    bool working() const {
        return a.tasks_->busy() || a.loadPending_ || a.iconJob_ || !a.work_.empty() ||
            (a.launch_ && (a.launch_->phase == LaunchPhase::verify || a.launch_->phase == LaunchPhase::dispatch));
    }
    // The published App icon image of a shortcut (and its trimmed side-list copy).
    bool icon(const std::string& id, std::string_view applicationKey) {
        const auto found = a.icons_.find(id);
        return found != a.icons_.end() && found->second.revision && found->second.applicationKey == applicationKey &&
            a.images_->acquire(iconKey(id), found->second.revision) != nullptr;
    }
    void settle() {
        for (unsigned n = 0; n < 16 && working(); ++n) {step.drain();frame();}
        step.check(!working(), "App shortcut work settles through the shared executor");
    }
    // A box point of the + Add App canvas -> logical client (the current module plane).
    core::Point project(d::Point p) {
        const auto client = a.core_->clientRect({p.x, p.y, 0, 0});
        step.check(client.has_value(), "The + Add App canvas point projects onto the current module plane");
        return {client->x, client->y};
    }
    d::Control* control(std::string_view id) const {
        for (d::Control* c : canvas().page().controls()) if (c->id() == id) return c;
        for (d::Control* c : canvas().header().controls()) if (c->id() == id) return c;
        return nullptr;
    }
    void click(std::string_view id) {
        const auto* c = control(id);
        step.check(c != nullptr, "The + Add App control is shown");
        const auto r = c->rect(canvas().root());
        const auto p = project({r.x + r.width / 2, r.y + r.height / 2});
        step.check(a.covers(p), "The projected control belongs to + Add App");
        a.pointer({app::PointerKind::move, app::PointerButton::none, p.x, p.y, 0}, t);
        a.pointer({app::PointerKind::down, app::PointerButton::left, p.x, p.y, 0}, t);
        a.pointer({app::PointerKind::up, app::PointerButton::left, p.x, p.y, 0}, t);
        frame();
    }
    static std::string utf8(const fs::path& path) { const auto value = path.u8string();return std::string(value.begin(), value.end()); }
    // The chooser answered with `paths` (the host dialog's result).
    void answer(std::vector<std::string> paths, bool cancelled = false) {
        step.check(a.chooser_.has_value() && canvas().chooserOpen() && canvas().presentingPanel() && a.pointerLocked(), "The chooser is open and the canvas waits behind it");
        a.chosen(*a.chooser_, {cancelled, false, std::move(paths)});
        settle();frame();
    }
    // Add App -> the chooser -> one application.
    void add(std::string path) {
        const auto before = a.coverageChooserRequests_;
        click("apps:add");
        step.check(a.coverageChooserRequests_ == before + 1, "Add App requests the one chooser");
        answer({std::move(path)});
    }
    std::optional<d::ToastRequest> toast() const { return context().overlay().toastState().current(); }
    void clearToast() { context().overlay().clearToast(); }
    m::ShortcutFile disk() const {
        const auto bytes = ehud::data::detail::readFile(a.directory_ / "shortcuts.json", m::shortcutMaximumBytes);
        step.check(bytes.has_value(), "The data root holds shortcuts.json");
        return m::decodeShortcuts(J::parse(*bytes, m::shortcutMaximumBytes), g::nativeShortcutTextRules());
    }
    void committed(const char* why) {
        step.check(disk() == file() && canvas().items() == file().items, why);
    }
    const m::ShortcutRecord& record(std::string_view id) const {
        const auto found = std::find_if(file().items.begin(), file().items.end(), [&](const auto& v) { return v.id == id; });
        step.check(found != file().items.end(), "The saved app is listed");
        return *found;
    }
    std::string last() const { return file().items.back().id; }
    // Remove -> the shared confirm bar -> Confirm (its 120 ms callback).
    void remove(std::string_view id) {
        const auto before = file().items.size();
        click("apps:" + std::string(id) + ":remove");
        const auto& request = context().overlay().confirmState().request();
        step.check(request && request->id == "apps:remove" && request->danger && context().overlay().capturesInput(), "Remove asks through the confirm bar");
        step.check(context().overlay().decide(d::ConfirmState::Decision::confirm), "The confirm bar takes the decision");
        t += .2;context().advance(t);settle();frame();
        step.check(file().items.size() == before - 1 && toast() && toast()->title == text(a.language_, "Shortcut removed", "应用未被卸载"), "The confirmed shortcut is removed and toasted");
        clearToast();committed("The committed shortcuts.json is exactly the shown list");
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
        (void)a.planNavigation(modules, right);
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
        const auto& items = file().items;
        for (std::size_t n = 0; ordered && n < items.size(); ++n) {
            const auto& item = items[n];
            const auto app = action("app." + item.id);
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.action == app; });
            ordered = app && at + 1 + std::ptrdiff_t(n) < right.end() && *(at + 1 + std::ptrdiff_t(n)) == app && entry != entries.end() && !entry->module &&
                entry->title == item.name && entry->iconKey == "shortcut:" + item.iconPreset && a.planner_.shortcutID(app) == std::optional<std::string_view>(item.id);
        }
        step.check(ordered && std::size_t(right.end() - at) == items.size() + 2, "Saved apps follow Power in their stored order and + Add App stays last");
        // HUDAppShortcutPresentation.icon trimmed (HUDSourceDesktopIconLayout.image):
        // the published side-list image for the "original" preset only.
        bool icons = true;
        for (const auto& item : items) {
            const auto app = action("app." + item.id);
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.action == app; });
            if (entry == entries.end()) {icons = false;break;}
            const auto image = item.iconPreset == "original" ? a.navigationImage(item.id) : std::nullopt;
            icons = icons && (image ? entry->originalImageKey == image->key && entry->originalImageRevision == image->revision && entry->originalImages == a.images_ &&
                                          a.images_->acquire(image->key, image->revision) != nullptr
                                    : entry->originalImageKey.empty() && !entry->originalImageRevision && !entry->originalImages);
        }
        step.check(icons, "Navigation entries name exactly the published trimmed App icon images");
    }
    bool navigationIcon(std::string_view id) {
        plan();
        const auto entries = a.planner_.entries();const auto app = action("app." + std::string(id));
        const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.action == app; });
        return entry != entries.end() && !entry->originalImageKey.empty() && entry->originalImages == a.images_;
    }
    // The UIA peer of an accessibility item, by automation id.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> peer(std::string_view id) {
        for (std::size_t n = 0; n < a.automationCount(); ++n) {
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> element;
            if (FAILED(a.automationChild(n, &element)) || !element) continue;
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;VARIANT value;VariantInit(&value);
            const bool match = SUCCEEDED(element.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value)) && value.vt == VT_BSTR &&
                std::wstring_view(value.bstrVal) == std::wstring(id.begin(), id.end());
            VariantClear(&value);
            if (match) return element;
        }
        step.check(false, "The UIA tree lists the + Add App control");
        return nullptr;
    }
    // Close-first handoff on this owner's own close/conceal path (the hidden
    // harness keeps the HUD lifecycle; the owner's close request is counted).
    void close(bool forced) {
        const auto closes = a.coverageCloseRequests_;
        settle(); // reinspection before closing
        step.check(a.launch_ && a.launch_->phase == LaunchPhase::closing && a.coverageCloseRequests_ == closes + 1, "A verified launch first requests the HUD close");
        a.closing(t, true);
        step.check(a.launch_ && a.launch_->phase == LaunchPhase::closed && !a.overlayVisible_, "The launch waits for the close to finish");
        if (forced) a.closing(t, false); // forceCloseSystemOverlay interrupts the closing animation
        t += .5;a.concealed(t);settle();
    }
    void reopen() {
        a.setOverlayVisible(true, t);frames(.4);
        step.check(canvas().isActive() && !a.launch_, "+ Add App is interactive again after the handoff");
    }
    // Migration/relink.json as the import card reads it, and the rows the
    // factory seeded: [0] the imported shortcut, [1] a shortcut the user had
    // already removed, [2] a Reader row with the same record id.
    std::vector<mig::MacRelinkItem> seededRows() const {
        std::vector<mig::MacRelinkItem> rows;
        const auto document = J::parse(a.seed_.ledger, mig::MacRelinkLedger::maximumBytes);
        for (const auto& row : document["items"].array()) rows.push_back(mig::decodeRelinkItem(row));
        step.check(rows.size() == 3, "The seeded import ledger has its three rows");
        return rows;
    }
    void checkLedger(std::optional<std::string> relinked) {
        const auto rows = seededRows();
        const mig::MacRelinkLedger ledger(a.relinkRoot_);
        const auto* imported = ledger.find(rows[0].id);const auto* removed = ledger.find(rows[1].id);const auto* book = ledger.find(rows[2].id);
        step.check(imported && removed && book && ledger.items().size() == rows.size(), "Migration/relink.json keeps the import's rows");
        step.check(removed->state == mig::RelinkState::dismissed && !removed->windowsPath, "The row of a shortcut the user removed is dismissed");
        step.check(*book == rows[2], "A Reader row with the same record id is left to the Reader");
        step.check(relinked ? imported->state == mig::RelinkState::resolved && imported->windowsPath == relinked
                            : imported->state == mig::RelinkState::unresolved && !imported->windowsPath,
                   relinked ? "The relinked shortcut's row names its Windows application" : "The imported shortcut's row stays listed until it is relinked");
    }
    // A coverage restart of the store (as when EndfieldHUD starts again): the
    // seeded Mac archive and the import's ledger return on disk and a new
    // transaction owner loads and reconciles them.
    void restartStore() {
        const auto ledger = a.relinkRoot_ / "Migration" / "relink.json";
        ehud::data::detail::replaceFile(ledger, ehud::data::detail::readFile(ledger, mig::MacRelinkLedger::maximumBytes), a.seed_.ledger, mig::MacRelinkLedger::maximumBytes);
        const auto syncs = a.relinkSyncs_;
        loadStore(a.seed_.archive);
        step.check(a.relinkSyncs_ == syncs + 1 && a.lastRelink_ == m::ShortcutRelinkSync{0, 1, 1}, "The restarted store reconciles the ledger");
    }
    void loadStore(const std::string& archive) {
        step.check(!working() && !a.inflight_, "The store restarts idle");
        a.tasks_.reset();
        const auto path = a.directory_ / "shortcuts.json";
        ehud::data::detail::replaceFile(path, ehud::data::detail::readFile(path, m::shortcutMaximumBytes), archive, m::shortcutMaximumBytes);
        const auto revision = a.listRevision_;
        a.tasks_ = a.makeTasks();a.loadPending_ = true;a.pump();settle();frames(.1);
        step.check(a.listRevision_ == revision + 1 && !a.relinkFailures_, "The restarted store loads once");
    }
    // `base` plus `count` synthetic applications (Many0.exe ...) saved by the
    // production rules (AppShortcutStore.save), every second one with the
    // App icon preset, as the encoded shortcuts.json.
    std::string manyArchive(const fs::path& applications, m::ShortcutFile base, unsigned count) const {
        const g::NativeAppShortcutService service(syntheticOS(a.synthetic_));
        const auto rules = g::nativeShortcutTextRules();
        for (unsigned n = 0; n < count; ++n) {
            const auto candidate = service.inspect({g::AppShortcutSelectionKind::file, utf8(applications / ("Many" + std::to_string(n) + ".exe"))});
            (void)m::saveShortcutDraft(base, candidate, candidate, candidate.name, n % 2 ? "terminal" : "original", std::nullopt, ehud::data::makeUUID(),
                                       ehud::data::foundationNow(), rules, g::NativeAppShortcutService::sameTarget);
        }
        return m::encodeShortcuts(base, rules).encode(m::shortcutMaximumBytes);
    }
    double offset() const { return a.listOffset(); }
    bool wheel(double steps) {
        const auto p = project({295, 300});
        step.check(a.covers(p), "The list projects onto the current module plane");
        const bool used = a.wheel({p.x, p.y, steps, false, 0, 3}, t);
        frames(.3);
        return used;
    }
};

void AppShortcutArea::coverage(app::ModuleCoverage& step) {
    Driver d{*this, step, step.time};
    if (!step.module) {
        // Per-cycle area step: nothing pending, the disk equals the shown
        // list and the navigation keeps saved apps between Power and Add App.
        step.check(!tasks_->busy() && !launch_ && !chooser_ && !loadFailed_ && work_.empty() && canvas_->pendingRequests().empty() && !relinkFailures_,
                   "App shortcuts are idle between cycles");
        step.check(d.disk() == file_ && canvas_->items() == file_.items, "The data root still holds the shown list");
        d.checkNavigation();
        // A navigation launch of the imported record shows its relink error
        // in + Add App and selects it when another module is shown.
        const auto& seeded = file_.items.front();
        const auto selects = coverageSelectRequests_, closes = coverageCloseRequests_;
        const bool elsewhere = overlayVisible_ && host_.selectedModule() != core::Module::addApp;
        step.check(activate(d.action("app." + seeded.id), d.t) && !launch_ && d.toast() && d.toast()->title == text(language_, "Notice", "提示") && coverageCloseRequests_ == closes &&
                   coverageSelectRequests_ == selects + (elsewhere ? 1u : 0u), "A navigation launch of an imported app shows its error in + Add App");
        d.clearToast();
        // The original application icons within their memory bound (two
        // images each: the tile's and the trimmed side-list copy).
        {
            const auto icons = images_->stats();
            step.check(images_->limits().cachedImages > 2 * maximumIcons && icons.cachedImages <= 2 * maximumIcons && icons.cachedBytes <= 2 * maximumIcons * iconBytes &&
                           icons.liveBytes <= iconLimits.liveBytes, "The original application icons stay within their 128 shortcuts of 96 pixels");
        }
        // Memory (Mac OverlayController: systemView = nil on every close): a
        // hidden + Add App keeps no artwork once the HUD closed; the next
        // cycle shows it again with the same artwork.
        if (elsewhere && core_->entries(core::Module::addApp).empty()) {
            const auto open = core_->sceneStats(core::Module::addApp);const auto shared = raster_.stats();
            overlayClosing(d.t);(void)hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, d.t);
            const auto closed = core_->sceneStats(core::Module::addApp);const auto released = raster_.stats();
            step.check(open && closed && closed->rasterEntries == 0 && closed->residentPixelBytes == 0 && closed->parks >= open->parks,
                       "Once the HUD is closed the hidden + Add App keeps no canvas artwork");
            step.check(released.entries <= shared.entries && released.resourceBytes <= shared.resourceBytes, "The released canvas returns its rasters to the shared rasterizer");
            setOverlayVisible(true, d.t); // this owner's next opening (the harness keeps the HUD open)
            step.check(core_->entries(core::Module::addApp).empty(), "Opening the HUD on another module draws no + Add App canvas");
        }
        d.frame();return;
    }
    step.check(*step.module == core::Module::addApp, "App shortcuts present only + Add App");
    d.settle();d.frames(.1);
    const auto seeded = file_;
    step.check(canvas_->isActive() && canvas_->storeAvailable() && !core_->entries(core::Module::addApp).empty() && seeded.items.size() == 1 && !seeded.items[0].native() &&
               d.disk() == seeded && canvas_->items() == seeded.items && canvas_->header().counterText() == "01", "+ Add App shows the seeded imported Mac shortcut from the data root");
    // The load reconciled the import ledger on the repository worker.
    step.check(relinkSyncs_ >= 1 && lastRelink_ == m::ShortcutRelinkSync{0, 1, 1} && !relinkFailures_, "The load reconciles Migration/relink.json once");
    d.checkLedger(std::nullopt);
    d.checkNavigation();
    const auto applications = coverageRoot_ / "Applications";
    const auto launched = synthetic_->launched.load();
    const auto opened = launchesOpened_, reopened = coverageReopenRequests_, closes = coverageCloseRequests_;
    const auto& imported = seeded.items[0];

    // 1. Add App: an executable is saved at once with its own name and the
    //    App icon preset (AppShortcutCanvas.importURLs), toasted, listed last.
    d.add(Driver::utf8(applications / "Alpha.exe"));
    const auto alphaID = d.last();
    {
        const auto& alpha = d.record(alphaID);
        step.check(file_.items.size() == 2 && alpha.name == "Alpha Synthetic" && alpha.iconPreset == "original" && alpha.native() &&
                   alpha.locator["windowsTarget"]["kind"] == J("executable") && d.toast() && d.toast()->title == text(language_, "App added", "已添加到右侧列表"),
                   "An added application is saved with its inspected name and the App icon preset, and toasted");
        d.clearToast();d.committed("The committed shortcuts.json is exactly the shown list");
        d.settle();
        step.check(d.icon(alphaID, applicationKey(alpha.locator)) && d.navigationIcon(alphaID), "The new App icon shortcut shows its own icon in the tile and the side list");
    }
    d.checkNavigation();
    const auto alphaAction = d.action("app." + alphaID);
    // 2. Rename in place (the name field's commit through its accessibility value).
    step.check(canvas_->setAccessibilityText("apps:" + alphaID + ":name", "  Alpha \xe5\xba\x94\xe7\x94\xa8\n", d.t), "The name field takes a new name");
    d.settle();d.frame();
    step.check(d.record(alphaID).name == "Alpha \xe5\xba\x94\xe7\x94\xa8" && d.action("app." + alphaID) == alphaAction, "The trimmed name is saved; the navigation action stays");
    d.committed("A rename is committed");
    step.check(canvas_->setAccessibilityText("apps:" + alphaID + ":name", " ", d.t) && !d.working() && d.record(alphaID).name == "Alpha \xe5\xba\x94\xe7\x94\xa8",
               "An empty name restores the saved one and saves nothing");
    // 3. Choose icon: the tile opens the 5 x 3 preset grid; Camera is saved.
    d.click("apps:" + alphaID + ":icon");
    step.check(canvas_->menuOpen() && core_->capturesPointer(), "The tile opens Choose icon, which captures the pointer");
    step.check(canvas_->performAccessibilityAction("apps:iconMenu:camera", d.t), "The Camera tile is picked");
    d.settle();d.frame();
    step.check(!canvas_->menuOpen() && d.record(alphaID).iconPreset == "camera" && !icons_.contains(alphaID), "A picked preset is saved and the App icon is released");
    d.committed("The preset is committed");
    d.checkNavigation();

    // 4. The imported Mac record has no Windows target: Open relinks it (Windows only); cancelled here.
    {
        const auto requests = coverageChooserRequests_;
        d.click("apps:" + imported.id + ":open");
        step.check(coverageChooserRequests_ == requests + 1 && !launch_ && coverageCloseRequests_ == closes, "Open on an imported shortcut asks for its Windows application instead of closing");
        d.answer({}, true);
        step.check(!chooser_ && !canvas_->chooserOpen() && file_ == d.disk() && !canvas_->presentingPanel(), "A cancelled relink changes nothing");
    }
    // 5. A missing target fails inspection: an error toast, nothing saved.
    d.add(Driver::utf8(applications / "Missing.exe"));
    step.check(file_.items.size() == 2 && d.toast() && d.toast()->title == text(language_, "Notice", "提示"), "A missing application reports its error");
    d.clearToast();
    // 6. A packaged application from the chooser's applications mode.
    const auto logos = synthetic_->logos.load();
    d.add("shell:AppsFolder\\Synthetic.Packaged_8wekyb3d8bbwe!App");
    const auto packagedID = d.last();
    {
        const auto& packaged = d.record(packagedID);
        step.check(file_.items.size() == 3 && packaged.name == "Packaged Synthetic" && packaged.locator["windowsTarget"]["kind"] == J("packagedApp") &&
                   packaged.locator["windowsTarget"]["appUserModelID"] == J("Synthetic.Packaged_8wekyb3d8bbwe!App"), "A chosen packaged application is saved");
        d.clearToast();d.settle();
        // Its App icon is the package's logo (the synthetic fixture through
        // the production decoder): the opaque glyph's exact colour at the centre.
        const auto found = icons_.find(packagedID);
        const auto image = d.icon(packagedID, applicationKey(packaged.locator)) ? images_->acquire(iconKey(packagedID), found->second.revision) : nullptr;
        const auto center = image ? (std::size_t(image->height() / 2) * image->width() + image->width() / 2) * 4 : 0;
        step.check(image && synthetic_->logos == logos + 1 && image->width() == g::appShortcutIconPixels &&
                   image->premultipliedBGRA()[center] == 90 && image->premultipliedBGRA()[center + 1] == 160 && image->premultipliedBGRA()[center + 2] == 40 && image->premultipliedBGRA()[center + 3] == 255,
                   "The packaged shortcut shows its own logo from the package");
        const auto trimmed = images_->acquire(navigationKey(packagedID), found->second.revision);
        step.check(trimmed && trimmed->width() <= g::appShortcutIconPixels && trimmed->height() <= g::appShortcutIconPixels && found->second.color, "The side list gets the logo trimmed to its pixels, the tile its average colour");
    }
    // 7. A document, or a choice the dialog cannot name, asks for a Windows application.
    {
        d.add(Driver::utf8(applications / "Readme.txt"));
        step.check(file_.items.size() == 3 && d.toast() && d.toast()->caption == invalidApplicationText(language_), "A chosen document asks for a Windows application");
        d.clearToast();
        d.click("apps:add");d.answer({});
        step.check(d.toast() && d.toast()->caption == invalidApplicationText(language_), "A choice that names no application asks for a Windows application");
        d.clearToast();
        const auto request = chooserRequest(core::Language::english);
        step.check(request.applications && request.singleFile && request.filterSpec == "*.exe;*.lnk" && request.filterName == "Applications" && request.title == "Choose an application",
                   "The chooser asks the shared dialog for one application (AppsFolder) through the application filter");
    }
    // 8. A launch that fails after the close reports in + Add App and reopens it.
    synthetic_->failLaunch = true;
    d.click("apps:" + packagedID + ":open");
    step.check(d.toast() && d.toast()->title == text(language_, "Opening", "正在打开"), "Open toasts Opening");
    d.close(false);
    synthetic_->failLaunch = false;
    step.check(!launch_ && launchError_ && coverageReopenRequests_ == reopened + 1 && launchesOpened_ == opened && synthetic_->launched == launched,
               "A failed launch keeps the shortcut, keeps its error and reopens + Add App");
    d.reopen();
    step.check(d.toast() && d.toast()->caption == *launchError_, "The reopened + Add App shows the failed launch's error");
    d.clearToast();
    // 9. A shell link keeps the App icon preset and gets its icon; the duplicate is refused.
    d.add(Driver::utf8(applications / "Beta.lnk"));
    const auto betaID = d.last();
    d.clearToast();d.settle();
    step.check(d.record(betaID).locator["windowsTarget"]["kind"] == J("shellLink") && d.icon(betaID, applicationKey(d.record(betaID).locator)) && !icons_.contains(alphaID) &&
               !icons_.contains(imported.id) && d.navigationIcon(betaID) && !d.navigationIcon(alphaID), "Only App icon shortcuts with a Windows target show their application's icon");
    d.add(Driver::utf8(applications / "Alpha.exe"));
    step.check(file_.items.size() == 4 && d.toast() && d.toast()->title == text(language_, "Notice", "提示"), "The same application cannot be saved twice");
    d.clearToast();d.checkNavigation();
    // 10. Drag and drop (SystemHUDView.acceptsAppShortcutDrop): exactly one application over the centre.
    {
        const auto inside = d.project({295, 300}), outside = d.project({-160, 300});
        step.check(covers(inside) && !covers(outside), "Drop points project inside and outside the + Add App canvas");
        const std::array two{Driver::utf8(applications / "Gamma.exe"), Driver::utf8(applications / "Delta.lnk")};
        const std::array gamma{two[0]}, document{Driver::utf8(applications / "Readme.txt")};
        step.check(!drop(two, inside, d.t) && !drop(document, inside, d.t) && !drop(gamma, outside, d.t) && !d.working(), "Two files, a document or a drop beside the canvas are refused");
        step.check(!dropHover(two, inside, d.t) && !dropHover(document, inside, d.t) && !dropHover(gamma, outside, d.t) && !canvas_->dropTarget(), "They show no drop target");
        step.check(dropHover(gamma, inside, d.t) && canvas_->dropTarget() && pointerLocked(), "One application over the canvas shows its drop target and holds the plane");
        d.frame();
        step.check(!dropHover(gamma, std::nullopt, d.t) && !canvas_->dropTarget(), "Leaving the canvas ends the drop target");
        step.check(dropHover(gamma, inside, d.t) && drop(gamma, inside, d.t) && !canvas_->dropTarget(), "A dropped application is accepted and ends the drop target");
        d.settle();d.frame();
        step.check(file_.items.size() == 5 && d.record(d.last()).name == "Gamma Synthetic" && d.toast() && d.toast()->title == text(language_, "App added", "已添加到右侧列表"),
                   "A dropped application is added");
        d.clearToast();
    }
    const auto gammaID = d.last();
    // 11. Ctrl+V (AppShortcutCanvas.handleKey): application files from the synthetic clipboard.
    {
        // The canvas takes Ctrl+V through its own key handler (the core reads
        // Ctrl from the keyboard state of the HUD's key dispatch).
        d::KeyInput paste;paste.virtualKey = 'V';paste.character = U'v';paste.control = true;
        const auto reads = syntheticClipboard_->fileReads;
        syntheticClipboard_->files = {Driver::utf8(applications / "Readme.txt")};
        step.check(!canvas_->keyDown(paste, d.t) && syntheticClipboard_->fileReads == reads + 1 && file_.items.size() == 5 && !d.toast(), "Ctrl+V without an application is left unhandled");
        syntheticClipboard_->files = {Driver::utf8(applications / "Gamma.exe"), Driver::utf8(applications / "Delta.lnk")};
        step.check(canvas_->keyDown(paste, d.t) && d.toast() && d.toast()->caption == text(language_, "Choose one application to customize.", "请选择一个应用进行设置。"),
                   "Pasting two applications asks for one");
        d.clearToast();
        syntheticClipboard_->files = {Driver::utf8(applications / "Delta.lnk")};
        step.check(canvas_->keyDown(paste, d.t), "A pasted application is accepted");
        d.settle();d.frame();
        step.check(file_.items.size() == 6 && d.record(d.last()).name == "Delta Synthetic", "The pasted application is added");
        d.clearToast();
    }
    const auto deltaID = d.last();
    // 12. UI Automation over the library tree: the canvas's items, Invoke on a peer.
    (void)automationRefresh(nullptr);
    {
        std::vector<d::AccessibilityItem> items;canvas_->accessibilityItems(items);
        step.check(automationCount() >= items.size() && items.size() >= 1 + 4 * file_.items.size(), "UIA lists Add App and each row's tile, name, Open and Remove");
        const auto open = d.peer("apps:" + betaID + ":open");
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;VARIANT name;VariantInit(&name);
        step.check(SUCCEEDED(open.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &name)) && name.vt == VT_BSTR && std::wstring_view(name.bstrVal).ends_with(L"Beta Synthetic"),
                   "A row peer is named after its app");
        VariantClear(&name);
        const auto addPeer = d.peer("apps:add");
        Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
        const auto requests = coverageChooserRequests_;
        step.check(SUCCEEDED(addPeer.As(&invoke)) && SUCCEEDED(invoke->Invoke()) && coverageChooserRequests_ == requests + 1 && chooser_, "The Add App peer requests the chooser");
        d.answer({}, true);
    }
    automationDisconnect();
    step.check(automationCount() == 0, "Closing the UIA tree disconnects every peer");
    // 13. A successful close-first launch records the Event Log entry. Only
    //     in the first cycle: the shared Event Log then shows the same rows
    //     in every later cycle (its raster usage must repeat).
    const unsigned successes = step.cycle == 0 ? 1 : 0;
    if (successes) {
        d.click("apps:" + betaID + ":open");
        d.close(false);
        step.check(!launch_ && synthetic_->launched == launched + 1 && launchesOpened_ == opened + 1 && coverageReopenRequests_ == reopened + 1,
                   "The application starts only after the close and is recorded as opened");
        d.reopen();
    }
    d.clearToast();
    // 14. A navigation activation follows the same handoff; a forced close
    //     (sleep, session end) cancels it before anything starts.
    step.check(activate(alphaAction, d.t) && launch_, "The navigation action of a saved app requests its launch");
    step.check(activate(alphaAction, d.t) && launch_ && launch_->phase == LaunchPhase::verify, "One launch at a time");
    d.close(true);
    step.check(!launch_ && synthetic_->launched == launched + successes, "A forced close cancels the pending launch");
    d.reopen();
    step.check(!activate(d.action("addApp"), d.t) && !launch_, "Module actions are not app launches");
    // 15. Quitting cancels a launch whose close has not finished.
    step.check(activate(d.action("app." + betaID), d.t), "The second app's navigation action requests its launch");
    d.settle();
    step.check(launch_ && launch_->phase == LaunchPhase::closing, "The verified launch waits for the close");
    (void)hostEvent({app::HostEvent::Kind::quitting, 0}, d.t);
    step.check(!launch_ && synthetic_->launched == launched + successes, "An accepted quit cancels the pending launch");
    concealed(d.t);
    step.check(coverageCloseRequests_ == closes + 3 + successes, "Each verified launch requested exactly one close");
    // 16. Remove the added apps (the confirm bar): the seeded state returns.
    for (const auto& id : {alphaID, packagedID, betaID, gammaID, deltaID}) d.remove(id);
    step.check(file_ == seeded && d.disk() == seeded && !d.working(), "The seeded imported shortcut remains alone");
    step.check(!icons_.contains(alphaID) && !icons_.contains(betaID) && !icons_.contains(packagedID) && !icons_.contains(gammaID) && !icons_.contains(deltaID),
               "Removed shortcuts release their icon images");
    d.checkNavigation();
    step.check(!planner_.shortcutID(alphaAction), "A removed app's action launches nothing");
    // 17. Relink the imported Mac shortcut (Open -> the chooser): it keeps its
    //     identity, name, preset, date and position and gains the Windows
    //     target; Migration/relink.json resolves its row with that application.
    {
        const auto relinkedPath = applications / "Relinked.exe";
        const auto syncs = relinkSyncs_;
        d.click("apps:" + imported.id + ":open");
        d.answer({Driver::utf8(relinkedPath)});
        const auto target = Driver::utf8(relinkedPath);
        const auto& relinked = d.record(imported.id);
        step.check(file_.items.size() == 1 && relinked.native() && relinked.name == imported.name && relinked.iconPreset == imported.iconPreset &&
                   relinked.createdAt == imported.createdAt && relinked.locator["windowsTarget"]["path"] == J(target) && !d.toast(),
                   "The relinked shortcut keeps its identity and names its Windows application");
        step.check(relinkSyncs_ == syncs + 1 && lastRelink_ == m::ShortcutRelinkSync{1, 0, 0} && !relinkFailures_, "Saving the relink resolves its ledger row on the repository worker");
        d.checkLedger(target);
        d.restartStore();
        step.check(file_ == seeded && d.disk() == seeded && canvas_->items() == seeded.items, "The restarted store shows the imported Mac shortcut again");
        d.checkLedger(std::nullopt);
        d.checkNavigation();
    }
    // 18. More saved apps than the list shows: the wheel scrolls the list
    //     (row-local parts: no repaint), a scrolled row's Remove commits, and
    //     the seeded file returns.
    {
        d.loadStore(d.manyArchive(applications, seeded, 9));
        const auto many = file_;
        step.check(many.items.size() == 10 && canvas_->items() == many.items && d.offset() == 0 && d.disk() == many, "Ten saved apps load in their order and the list starts at its top");
        d.checkNavigation();d.settle();
        const auto lastID = many.items.back().id;
        step.check(many.items.back().iconPreset == "original" && d.icon(lastID, applicationKey(many.items.back().locator)) && d.navigationIcon(lastID), "The last saved app shows its own icon");
        const auto before = core_->sceneStats(core::Module::addApp);
        step.check(d.wheel(-4) && d.offset() > 0, "The wheel scrolls the list");
        const auto after = core_->sceneStats(core::Module::addApp);
        step.check(before && after && after->generations == before->generations, "Scrolling moves the retained rows without a new generation");
        for (unsigned n = 0; n < 8; ++n) (void)d.wheel(-4);
        step.check(d.offset() == canvas_->page().scrollAreas().front()->maximumOffset(), "The list reaches its end");
        d.remove(lastID);
        step.check(!icons_.contains(lastID), "The removed scrolled row releases its icon");
        (void)d.wheel(20);
        step.check(d.offset() == 0, "Wheel notches back return the list to its top");
        d.restartStore();
        step.check(file_ == seeded && d.disk() == seeded, "The seeded list returns");
        d.checkNavigation();
    }
    d.frames(.2);
}

} // namespace


std::unique_ptr<app::ModuleOwner> appShortcutModule(const app::ApplicationModuleContext& context) {
    need(context.host && context.utility && context.window, "the shared host, executor and window are required");
    // The verified packaged source artwork; absent (no verified package, as in
    // development previews) leaves the module absent.
    const auto assets = context.resource ? context.resource("appShortcut.assets") : fs::path{};
    if (assets.empty()) return nullptr;
    // The redesigned screen needs the HUD's design overlay (absent: no module, as without artwork).
    if (!context.host->designContext()) return nullptr;
    std::shared_ptr<SyntheticApplications> synthetic;
    fs::path directory, relinkRoot;
    CoverageSeed seed;
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
        relinkRoot = context.coverageRoot.lexically_normal();
        directory = relinkRoot / "AppShortcuts";
        seed.archive = archive.encode(m::shortcutMaximumBytes);
        ehud::data::detail::replaceFile(directory / "shortcuts.json", std::nullopt, seed.archive, m::shortcutMaximumBytes);
        // The import's own ledger rows for that archive (validateStagedShortcuts,
        // as the offline import writes Migration/relink.json), one row whose
        // shortcut the user already removed and one Reader row with the same
        // record id, which only the Reader decides.
        auto rows = mig::validateStagedShortcuts(directory, g::nativeShortcutTextRules());
        need(rows.size() == 1, "the import lists the seeded Mac shortcut for relinking");
        rows[0].id = ehud::data::makeUUID();
        auto removed = rows[0];removed.id = ehud::data::makeUUID();removed.recordID = "abcdef00-0000-4000-8000-0000000000ff";removed.displayName = "Removed";
        auto book = rows[0];book.id = ehud::data::makeUUID();book.store = std::string(mig::relinkReaderBook);book.kind = "book";
        rows.push_back(std::move(removed));rows.push_back(std::move(book));
        seed.ledger = mig::MacRelinkLedger::encode(rows);
        ehud::data::detail::replaceFile(relinkRoot / "Migration" / "relink.json", std::nullopt, seed.ledger, mig::MacRelinkLedger::maximumBytes);
        // A synthetic packaged application's logo (never an installed one).
        const auto logo = context.fixture ? context.fixture("appShortcut.packagedLogo") : fs::path{};
        need(!logo.empty(), "hidden coverage needs the synthetic packaged logo fixture");
        const auto logoBytes = ehud::data::detail::readFile(logo, 64 * 1024);
        need(logoBytes.has_value() && !logoBytes->empty(), "the synthetic packaged logo fixture is readable");
        seed.logo.assign(logoBytes->begin(), logoBytes->end());
        synthetic = std::make_shared<SyntheticApplications>();
    } else {
        // AppShortcutStore.applicationDirectory(): <app root>/AppShortcuts;
        // the import ledger is <app root>/Migration/relink.json.
        need(!context.dataRoot.empty() && context.dataRoot.is_absolute(), "an absolute data root is required");
        relinkRoot = context.dataRoot.lexically_normal();
        directory = relinkRoot / "AppShortcuts";
    }
    return std::make_unique<AppShortcutArea>(context, assets, std::move(directory), std::move(relinkRoot), std::move(synthetic), std::move(seed));
}
} // namespace endfield::areas
