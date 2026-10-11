#include "app/areas/app_shortcut_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/app_shortcut_navigation.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/localization.hpp"
#include "core/migration/mac_import_native.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "modules/app_shortcut_navigation.hpp"
#include "modules/app_shortcut_presentation.hpp"
#include "modules/app_shortcut_tasks.hpp"
#include "native/app_shortcut_assets.hpp"
#include "native/app_shortcut_automation.hpp"
#include "native/app_shortcut_icons.hpp"
#include "native/layer_image_source.hpp"
#include "native/app_shortcut_text.hpp"
#include "native/notes_clipboard.hpp"
#include "tools/app_shortcut_preview.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
    {"Edit this shortcut and choose its Windows application.", "请编辑此快捷方式并选择对应的 Windows 应用。", "請編輯此捷徑並選擇對應的 Windows 應用程式。",
     "このショートカットを編集して、対応するWindowsアプリを選択してください。", "이 바로가기를 편집하고 해당 Windows 앱을 선택하세요."},
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

constexpr std::string_view nameFieldID = "apps:name:field";

// Hidden coverage: the seeded Mac archive, the import's ledger rows for it and
// the synthetic packaged logo (a coverage restart of the store writes the
// first two back; see Driver::restartStore).
struct CoverageSeed { std::string archive, ledger; std::vector<std::uint8_t> logo; };

class AppShortcutArea final : public app::ModuleOwner, public app::AppShortcutNavigation {
public:
    AppShortcutArea(const app::ApplicationModuleContext& c, fs::path assets, fs::path directory, fs::path relinkRoot, std::shared_ptr<SyntheticApplications> synthetic, CoverageSeed seed)
        : host_(*c.host), window_(static_cast<HWND>(c.window)), utility_(*c.utility), mode_(c.mode), directory_(std::move(directory)),
          relinkRoot_(std::move(relinkRoot)), coverageRoot_(c.coverageRoot), synthetic_(std::move(synthetic)), seed_(std::move(seed)), assets_(std::move(assets)),
          alive_(std::make_shared<bool>(true)) {
        need(window_ && IsWindow(window_), "the HUD window is required");
        textMessage_ = host_.registerMessage();
        const auto& a = host_.appearance();
        appearance_ = {a.dark, a.language, a.accent};language_ = a.language;
        tools::AppShortcutPreviewOptions options;options.appearance = appearance_;options.textMessage = textMessage_;
        options.raster.memoryImages = images_.get(); // original application icons (AppShortcutStore.icon(for:))
        // Edit-menu clipboard of the name field and Command-V application import.
        if (synthetic_) {syntheticClipboard_ = std::make_shared<SyntheticClipboard>();clipboard_ = syntheticClipboard_;}
        else clipboard_ = g::systemNotesClipboard();
        options.clipboard = clipboard_;
        extractor_ = synthetic_ ? syntheticIcons(synthetic_, seed_.logo) : g::windowsAppShortcutIconExtractor();
        iconRoute_ = utility_.makeRoute();
        auto* manager = static_cast<ITfThreadMgr*>(c.textManager);
        preview_ = std::make_unique<tools::AppShortcutPreview>(window_, c.rasterizer, assets_, manager,
            manager ? static_cast<TfClientId>(c.textClient) : TF_CLIENTID_NULL, std::move(options));
        if (c.metrics.pixelWidth && c.metrics.pixelHeight) preview_->resize(c.metrics);
        tasks_ = makeTasks();
        host_.publish<app::AppShortcutNavigation>(app::appShortcutNavigationService, *this);
        // AppShortcutStore(directory:) at startup: the one initial load.
        loadPending_ = true;pump();
    }
    ~AppShortcutArea() override {
        automation_.reset(); // UIA peers disconnect before the canvas they read
        *alive_ = false;observer_ = {};
        host_.withdraw(app::appShortcutNavigationService);
        tasks_.reset(); // suppresses completions; accepted writes/launches still finish on the executor
        utility_.invalidate(iconRoute_); // disposable icon work: no callback into this owner
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
        const bool opening = !overlayVisible_;
        overlayVisible_ = true;preview_->setOverlayVisible(true, t);
        // OverlayController creates SystemHUDView (and a fresh canvas) on
        // every open: no draft, no error, the list at its top - and the last
        // failed launch's error until a later launch is verified.
        if (opening && preview_->resetSession(t) && launchError_) preview_->showError(*launchError_, t);
        // Mac OverlayController retries a failed store on the next opening.
        if (loadFailed_ && !loadPending_) {loadFailed_ = false;loadPending_ = true;}
        pump();
    }
    void overlayClosing(double t) override { closing(t); }
    void update(const app::ModuleFrame& f) override {
        preview_->update(f.center, f.chrome, f.presentation, f.opacity, f.time);pump();
        if (preview_->state().revision() != iconStateRevision_) refreshIcons();
        // The name field opens (beginEditing -> makeFirstResponder) or closes
        // inside a frame: UI Automation re-lists on the next wake.
        if (const bool field = preview_->editingName(); field != automationField_) {automationField_ = field;automationWake_ = true;}
    }
    bool requiresFrames(double t) const override { return preview_->requiresFrames(t); }
    // One immediate wake on the shared timer after a frame changed the
    // accessible controls (never a poll): deadline() reports changed artwork,
    // which the host answers by re-listing UI Automation and presenting.
    std::optional<double> nextWakeTime(double now) const override { return automationWake_ ? std::optional(now) : std::nullopt; }
    bool deadline(double) override { return std::exchange(automationWake_, false); }
    void upload(g::Renderer& r) override { preview_->upload(r); }
    Entries entries(core::Module module) override { return module == core::Module::addApp ? preview_->entries() : Entries{}; }
    void collected(g::Renderer& r) override { preview_->collected(r); }
    void release(g::Renderer& r) override { preview_->release(r); }
    bool covers(core::Point p) const override { return preview_->covers(p); }
    bool pointerLocked() const override { return preview_->pointerLocked(); }
    bool pointer(const app::PointerEvent& e, double t) override { const bool used = preview_->pointer(e, t);pump();return used; }
    bool wheel(const app::WheelEvent& e, double t) override { return preview_->wheel(e, t); }
    // The host's key chain runs inside the HUD window's key-message dispatch,
    // where the keyboard state matches the message (HUDAppShortcutInteraction
    // .keyDown reads event.modifierFlags); coverage states its modifiers.
    bool key(const app::KeyEvent& e, double t) override { return key(e, tools::shortcutKeyboardModifiers(e), t); }
    bool filterKey(const app::NativeMessage& m) override { return preview_->filterKey(m); }
    bool message(const app::NativeMessage& m, double t) override { return preview_->message(m, t); }
    void focus(bool value, double t) override { focused_ = value;preview_->focus(value, t); }
    void cancelInteraction(double t) override { dropHovering_ = false;preview_->cancelInteraction(t); }
    bool finishEditing(double t) override { return preview_->finishEditing(true, t); }
    bool flush(double) override {
        // Bounded durable drain at quit/session end: an accepted save/remove
        // (or one retained while the executor was full) reaches the disk.
        for (unsigned n = 0; n < 16 && tasks_->busy() && inflight_ && durable(inflight_->kind); ++n) {
            utility_.waitIdle();utility_.drain();tasks_->queueCapacityAvailable();
        }
        return !(tasks_->busy() && inflight_ && durable(inflight_->kind));
    }
    // SystemHUDView.performDragOperation -> acceptsAppShortcutDrop: exactly one
    // application file over the 400x334 canvas while + Add App is interactive
    // opens its draft (importPasteboard -> AppShortcutCanvas.importURLs).
    bool drop(std::span<const std::string> paths, core::Point point, double t) override {
        if (dropHovering_) {dropHovering_ = false;preview_->setExternalDrag(false, t);} // endFileDrop
        if (paths.size() != 1 || !applicationFile(paths.front()) || !importable() || !preview_->covers(point)) return false;
        const bool used = preview_->inspectSelection({g::AppShortcutSelectionKind::file, paths.front()}, t);
        pump();host_.requestFrame();
        return used;
    }
    // Drag feedback (SystemHUDView.draggingUpdated -> acceptsAppShortcutDrop ->
    // beginExternalDrag / endExternalDrag): while exactly one application file
    // is dragged over the canvas it shows its drop target and locks input; a
    // leave, cancel or drop (point none) ends it. True when a drop there would
    // be taken (the host then offers the copy effect).
    bool dropHover(std::span<const std::string> paths, std::optional<core::Point> point, double t) override {
        const bool accepts = point && paths.size() == 1 && applicationFile(paths.front()) && importable() && preview_->covers(*point);
        if (accepts != dropHovering_) {
            preview_->setExternalDrag(accepts, t);
            dropHovering_ = accepts && preview_->state().dropTarget();
            host_.requestFrame();
        }
        return dropHovering_;
    }
    // HUDProjectedTextView.projectedCursor: the I-beam over the open name
    // field's projected rect (the pointer routing's own test), nothing else.
    std::optional<app::ModuleCursor> cursorAt(core::Point p) const override {
        return overlayVisible_ && preview_->textCursorAt(p) ? std::optional(app::ModuleCursor::text) : std::nullopt;
    }
    // HUDAppShortcutInteraction.layoutAccessibility: one peer per canvas
    // action, and the open name field (created on the first client request).
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        if (!automation_ || root != automationRoot_) {
            automation_.reset(); // a new group root (stable per registration) gets new peers
            automationRoot_ = root;automation_ = std::make_unique<g::NativeAppShortcutAutomation>(automationHost(root));
        }
        return automation_->refresh();
    }
    std::size_t automationCount() const override { return automation_ ? automation_->size() : 0; }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override {
        if (!automation_) {if (out) *out = nullptr;return E_INVALIDARG;}
        return automation_->child(index, out);
    }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override {
        if (!automation_) {if (out) *out = nullptr;return S_OK;}
        return automation_->fromPoint(x, y, out);
    }
    void automationDisconnect() noexcept override { if (automation_) automation_->disconnect(); }
    bool hostEvent(const app::HostEvent& e, double t) override {
        using K = app::HostEvent::Kind;
        switch (e.kind) {
        case K::utilityCompleted:
            tasks_->queueCapacityAvailable();pump();scheduleIcon();
            return std::exchange(contentChanged_, false); // completions ran in the drain: re-list UI Automation
        case K::overlayConcealed: concealed(t);return false;
        case K::suspended: case K::quitting: cancelPendingLaunch(t);return false; // forceCloseSystemOverlay / requestQuit
        default: return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override;

    // --- AppShortcutNavigation -------------------------------------------------
    Plan plan(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right) override { return planNavigation(modules, right); }
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
    // a module switch or close) in its applications mode (NSOpenPanel
    // allowedContentTypes [.applicationBundle] starting in /Applications):
    // one application, starting in AppsFolder - an existing .exe/.lnk through
    // the application filter, or a packaged application returned as
    // "shell:AppsFolder\<AUMID>". Links are not dereferenced, so a chosen .lnk
    // is saved as the shell link itself. Hidden coverage resolves it
    // synthetically.
    static app::ApplicationFileRequest chooserRequest(core::Language l) {
        app::ApplicationFileRequest request{text(l, "Choose an application", "选择应用"), text(l, "Choose", "选择"), text(l, "Choose", "选择")};
        request.singleFile = true;request.applications = true;
        request.filterName = text(l, "Applications", "应用程序");request.filterSpec = "*.exe;*.lnk";
        return request;
    }
    void choose(std::uint64_t generation) {
        chooser_ = generation;
        if (coverageMode()) {++coverageChooserRequests_;return;}
        const auto request = chooserRequest(language_);
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
        // An empty choice is an item the dialog could not name as an
        // application (AppsFolder entries that are not packaged apps).
        else if (result.paths.empty()) preview_->completeRequest(generation, invalidApplicationText(language_), t);
        else if (result.paths.size() != 1) preview_->completeRequest(generation, text(language_, "Choose one application to customize.", "请选择一个应用进行设置。"), t);
        else if (const auto selection = chosenApplication(result.paths.front()); !selection) preview_->completeRequest(generation, invalidApplicationText(language_), t);
        else {
            preview_->completeRequest(generation, {}, t);
            (void)preview_->inspectSelection(*selection, t);
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
            if (generation) preview_->completeRequest(*generation, message, t);else showLaunchError(message, t);
            host_.requestFrame();return;
        }
        launch_ = Launch{generation, record, LaunchPhase::verify};
        pump();
    }
    void finishLaunch(std::optional<std::string> error, double t) {
        auto launch = std::exchange(launch_, std::nullopt);
        if (!launch) return;
        if (launch->generation) preview_->completeRequest(*launch->generation, std::move(error), t);
        else if (error) showLaunchError(std::move(*error), t);
    }
    // SystemHUDView.showAppShortcutError: a navigation launch reports its
    // error in + Add App, which the open HUD selects when another module is
    // shown (after a close the failed launch reopens + Add App instead).
    void showLaunchError(std::string message, double t) {
        preview_->showError(std::move(message), t);
        if (!overlayVisible_ || host_.selectedModule() == core::Module::addApp) return;
        if (coverageMode()) {++coverageSelectRequests_;return;} // the coverage harness owns module selection
        host_.selectModule(core::Module::addApp);
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
        overlayVisible_ = false;dropHovering_ = false;
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

    // --- Keys, paste and drop -----------------------------------------------------
    bool key(const app::KeyEvent& e, const tools::ShortcutKeyModifiers& k, double t) {
        // HUDAppShortcutInteraction.keyDown: Command-V without the name editor
        // imports the pasteboard's application file.
        if (e.kind == app::KeyKind::down && e.value == 'V' && k.control && !k.shift && !k.system && !preview_->editingName() && preview_->acceptsInput()) {
            const bool used = pasteApplications(t);
            pump();if (used) host_.requestFrame();
            return used;
        }
        const bool used = preview_->key(e, k, t);pump();return used;
    }
    // HUDAppShortcutInteraction.importPasteboard guard: active, no chooser.
    bool importable() const { return overlayVisible_ && preview_->acceptsInput() && preview_->state().active() && !chooser_ && !preview_->presentingPanel(); }
    // importPasteboard(.general): the clipboard's file paths (CF_HDROP), read
    // once on this explicit key; none leaves the key unhandled.
    bool pasteApplications(double t) {
        if (!importable() || !clipboard_) return false;
        auto paths = clipboard_->readFiles();
        if (paths.empty()) return false;
        // AppShortcutCanvas.importURLs: exactly one application.
        if (paths.size() != 1) {preview_->showError(text(language_, "Choose one application to customize.", "请选择一个应用进行设置。"), t);return true;}
        if (!applicationFile(paths.front())) {preview_->showError(invalidApplicationText(language_), t);return true;}
        return preview_->inspectSelection({g::AppShortcutSelectionKind::file, std::move(paths.front())}, t);
    }

    // --- UI Automation --------------------------------------------------------------
    g::AppShortcutAutomationHost automationHost(IRawElementProviderFragmentRoot* root) {
        g::AppShortcutAutomationHost view;
        view.root = root;
        view.controls = [this] { return accessibleControls(); };
        view.toScreen = [this](core::Rect canvas) -> std::optional<core::Rect> {
            // Canvas -> logical client (current module plane) -> screen pixels.
            const auto client = preview_->clientRect(canvas);
            const double scale = host_.metrics().scale;
            POINT origin{0, 0};
            if (!client || !(scale > 0) || !ClientToScreen(window_, &origin)) return std::nullopt;
            return core::Rect{origin.x + client->x * scale, origin.y + client->y * scale, client->width * scale, client->height * scale};
        };
        view.invoke = [this](std::string_view id) { return performAccessible(id, now()); };
        view.setText = [this](std::string_view id, std::string_view value) {
            if (id != nameFieldID) return false;
            const bool used = preview_->setFieldText(value, now());
            host_.requestFrame();return used;
        };
        // The open name field is the first responder while the HUD has focus.
        view.focused = [this] { return focused_; };
        return view;
    }
    // AppShortcutCanvas.accessibleActions (without the name action while its
    // field is open) with accessibilityStatus as help, then the name field.
    std::vector<g::AppShortcutAutomationControl> accessibleControls() const {
        std::vector<g::AppShortcutAutomationControl> out;
        if (!overlayVisible_ || !preview_->acceptsInput()) return out;
        const auto& s = preview_->state();
        const auto help = s.error() ? *s.error() : s.editing() ? text(language_, "Edit app shortcut", "编辑应用快捷方式") : text(language_, "Application shortcuts", "应用快捷方式");
        const auto field = preview_->fieldText();
        out.reserve(s.actions().size() + 1);
        for (const auto& action : s.actions()) {
            if (field && action.kind == m::ShortcutActionKind::name) continue;
            out.push_back({m::shortcutActionID(action), m::shortcutActionLabel(action, s, language_), help, action.rect});
        }
        if (field) out.push_back({std::string(nameFieldID), text(language_, "Shortcut name", "快捷方式名称"),
            text(language_, "Return applies the name. Save shortcut stores your changes.", "回车应用名称，点击保存快捷方式以保存修改。"),
            m::shortcutNameRect, g::AppShortcutAutomationKind::edit, *field});
        return out;
    }
    // HUDAppShortcutInteraction.activateAction: the click's action.
    bool performAccessible(std::string_view id, double t) {
        if (!overlayVisible_ || !preview_->acceptsInput()) return false;
        const auto actions = preview_->state().actions();
        const auto found = std::find_if(actions.begin(), actions.end(), [&](const auto& a) { return m::shortcutActionID(a) == id; });
        if (found == actions.end()) return false;
        const auto action = *found; // perform may rebuild the action list
        const bool used = preview_->perform(action, t);
        pump();host_.requestFrame();
        return used;
    }

    // --- Original application icons ---------------------------------------------
    // The draft ("" key), the visible saved rows and then the navigation's
    // other saved apps, in stored order, that use the App icon preset
    // (AppShortcutCanvas rows and SystemHUDView.appNavigationPresentations);
    // at most 20 retained references (the shared image cache keeps 24
    // identities, so nothing this owner names is evicted). Further App icon
    // shortcuts keep the source grid until one of these is released.
    struct IconWant { std::string id, applicationKey; g::AppShortcutSelection selection; };
    struct IconEntry { std::string applicationKey; std::uint64_t revision{}, used{}; bool attempted{}; };
    static constexpr std::size_t maximumIcons = 20;
    static_assert(maximumIcons < g::LayerImageSource::maximumCachedImages);
    static std::string iconKey(std::string_view id) { return "appShortcut.icon." + (id.empty() ? std::string("draft") : std::string(id)); }
    static std::string applicationKey(const J& locator) {
        try {const auto& key = locator["windowsTarget"]["applicationKey"];return key.isString() ? key.string() : std::string{};}
        catch (...) {return {};}
    }
    void refreshIcons() {
        const auto& s = preview_->state();iconStateRevision_ = s.revision();
        wanted_.clear();
        const auto want = [&](std::string id, const J& locator) {
            if (wanted_.size() >= maximumIcons || std::any_of(wanted_.begin(), wanted_.end(), [&](const auto& w) { return w.id == id; })) return;
            if (const auto selection = g::appShortcutIconSelection(locator)) wanted_.push_back({std::move(id), applicationKey(locator), *selection});
        };
        if (s.editing() && s.candidate()) want({}, s.candidate()->locator);
        const auto& items = s.file().items;
        const auto first = std::min(items.size(), static_cast<std::size_t>(s.scrollOffset() / 52)), end = std::min(items.size(), first + 7);
        for (auto n = first; n < end; ++n) if (items[n].iconPreset == "original") want(items[n].id, items[n].locator);
        for (const auto& item : items) if (item.iconPreset == "original") want(item.id, item.locator);
        for (const auto& w : wanted_) icons_[w.id].used = ++iconUse_;
        // Removed shortcuts, shortcuts without a Windows target (an imported
        // record restored by a store restart) and the least recently shown
        // beyond the bound.
        std::erase_if(icons_, [&](const auto& entry) {
            if (entry.first.empty()) return false;
            const auto item = std::find_if(items.begin(), items.end(), [&](const auto& v) { return v.id == entry.first; });
            const bool removed = item == items.end() || !g::appShortcutIconSelection(item->locator);
            if (removed && entry.second.revision) images_->retire(iconKey(entry.first));
            return removed;
        });
        while (icons_.size() > maximumIcons) {
            const auto oldest = std::min_element(icons_.begin(), icons_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            if (oldest->second.revision) images_->retire(iconKey(oldest->first));
            icons_.erase(oldest);
        }
        publishIcons();scheduleIcon();
    }
    void publishIcons() {
        m::ShortcutOriginalImages images;
        for (const auto& [id, entry] : icons_) {
            if (!entry.revision) continue;
            const m::ShortcutImageReference reference{iconKey(id), entry.revision};
            if (id.empty()) images.draft = reference;else images.items.emplace(id, reference);
        }
        preview_->setOriginalImages(std::move(images));
        // The navigation names the same snapshots: rebind it whenever a saved
        // app's published icon changed or was released.
        std::map<std::string, std::uint64_t, std::less<>> navigation;
        for (const auto& [id, entry] : icons_) if (!id.empty() && entry.revision) navigation.emplace(id, entry.revision);
        if (navigation != navigationImages_) {navigationImages_ = std::move(navigation);if (observer_) observer_();}
    }
    std::optional<m::ShortcutImageReference> navigationImage(std::string_view id) const {
        const auto found = icons_.find(id);
        if (id.empty() || found == icons_.end() || !found->second.revision) return std::nullopt;
        return m::ShortcutImageReference{iconKey(id), found->second.revision};
    }
    Plan planNavigation(std::span<const g::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right) {
        return planner_.plan(modules, right, preview_->state().file(), [this](std::string_view id) { return navigationImage(id); }, images_);
    }
    void scheduleIcon() {
        if (iconJob_) return;
        for (const auto& w : wanted_) {
            const auto found = icons_.find(w.id);
            if (found != icons_.end() && found->second.attempted && found->second.applicationKey == w.applicationKey) continue;
            auto result = std::make_shared<std::optional<g::AppShortcutIconPixels>>();
            std::weak_ptr<bool> alive = alive_;
            if (!utility_.submit(iconRoute_, [extractor = extractor_, selection = w.selection, result] { *result = extractor(selection, g::appShortcutIconPixels); },
                    [this, alive, id = w.id, key = w.applicationKey, result](std::exception_ptr error) {
                        if (const auto token = alive.lock(); token && *token) guarded("icon", [&] { iconDone(id, key, error ? std::nullopt : std::move(*result)); });
                    })) return; // full: retried on the next utilityCompleted
            iconJob_ = w.id;return;
        }
    }
    void iconDone(const std::string& id, const std::string& key, std::optional<g::AppShortcutIconPixels> pixels) {
        iconJob_.reset();
        // A result for a target that is no longer wanted (removed, relinked
        // or a different draft) is dropped; the current one is scheduled next.
        const auto current = std::find_if(wanted_.begin(), wanted_.end(), [&](const auto& w) { return w.id == id && w.applicationKey == key; });
        const auto found = icons_.find(id);
        if (current == wanted_.end() || found == icons_.end()) {scheduleIcon();return;}
        auto& entry = found->second;
        entry.applicationKey = key;entry.attempted = true;
        if (pixels) {
            const auto revision = ++iconRevision_;
            try {(void)images_->publish(iconKey(id), revision, pixels->width, pixels->height, pixels->straightRGBA);entry.revision = revision;}
            catch (const std::bad_alloc&) {throw;}
            catch (const std::exception&) {} // rejected image: the source grid fallback remains
        } else if (entry.revision) {images_->retire(iconKey(id));entry.revision = 0;}
        publishIcons();scheduleIcon();host_.requestFrame();
    }

    // The committed list changed: icons follow it at once (also before the
    // HUD first opens, so the navigation has them), then the host rebinds.
    void notify() {
        ++listRevision_;
        if (preview_->state().revision() != iconStateRevision_) refreshIcons();
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
    ApplicationAreaMode mode_{};
    fs::path directory_, relinkRoot_, coverageRoot_;
    std::shared_ptr<SyntheticApplications> synthetic_;
    CoverageSeed seed_; // hidden coverage only
    g::NativeAppShortcutAssets assets_;
    std::shared_ptr<bool> alive_;
    // Outlives the preview whose raster options borrow it; shared with the
    // navigation entries that name its snapshots.
    std::shared_ptr<g::LayerImageSource> images_{std::make_shared<g::LayerImageSource>()};
    std::map<std::string, std::uint64_t, std::less<>> navigationImages_;
    g::AppShortcutIconExtractor extractor_;
    app::UtilityExecutor::Route iconRoute_{};
    std::vector<IconWant> wanted_;
    std::map<std::string, IconEntry, std::less<>> icons_;
    std::optional<std::string> iconJob_;
    std::uint64_t iconRevision_{}, iconUse_{}, iconStateRevision_{~std::uint64_t{0}};
    std::uint32_t textMessage_{};
    m::ShortcutAppearance appearance_;
    core::Language language_{core::Language::english};
    std::unique_ptr<tools::AppShortcutPreview> preview_;
    std::unique_ptr<m::AppShortcutTasks> tasks_; // destroyed before the preview its completions update
    std::shared_ptr<g::NotesClipboard> clipboard_;
    std::shared_ptr<SyntheticClipboard> syntheticClipboard_; // hidden coverage
    std::unique_ptr<g::NativeAppShortcutAutomation> automation_;
    IRawElementProviderFragmentRoot* automationRoot_{}; // the host's group root (borrowed; the peers hold their reference)
    NavigationPlanner planner_;
    std::function<void()> observer_;
    std::optional<Inflight> inflight_;
    std::optional<Launch> launch_;
    std::optional<std::uint64_t> chooser_;
    std::optional<std::string> launchError_; // OverlayController.appLaunchError
    std::uint64_t token_{}, listRevision_{};
    bool loadPending_{}, loadFailed_{}, overlayVisible_{}, pumping_{}, dropHovering_{}, focused_{};
    bool contentChanged_{}, automationField_{}, automationWake_{}; // UI Automation re-listing (hostEvent / deadline)
    unsigned coverageChooserRequests_{}, coverageCloseRequests_{}, coverageReopenRequests_{}, coverageSelectRequests_{}, launchesOpened_{};
    unsigned relinkSyncs_{}, relinkFailures_{};
    m::ShortcutRelinkSync lastRelink_;
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
    bool working() const {
        return a.tasks_->busy() || a.loadPending_ || a.iconJob_ || (a.launch_ && (a.launch_->phase == LaunchPhase::verify || a.launch_->phase == LaunchPhase::dispatch));
    }
    // The published App icon image of a shortcut ("" is the draft).
    bool icon(const std::string& id, std::string_view applicationKey) {
        const auto found = a.icons_.find(id);
        return found != a.icons_.end() && found->second.revision && found->second.applicationKey == applicationKey &&
            a.images_->acquire(iconKey(id), found->second.revision) != nullptr;
    }
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
    void choose(const fs::path& application) { choose(utf8(application)); }
    void choose(std::string selection) {
        const auto before = a.coverageChooserRequests_;
        click(329, 16); // + Choose app
        step.check(a.coverageChooserRequests_ == before + 1 && a.chooser_ && a.preview_->presentingPanel() && a.preview_->pointerLocked(),
                   "+ Choose app requests the one chooser and locks the canvas behind it");
        a.chosen(*a.chooser_, {false, false, {std::move(selection)}});
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
        const auto& items = state().file().items;
        for (std::size_t n = 0; ordered && n < items.size(); ++n) {
            const auto& item = items[n];
            const auto app = action("app." + item.id);
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.action == app; });
            ordered = app && at + 1 + std::ptrdiff_t(n) < right.end() && *(at + 1 + std::ptrdiff_t(n)) == app && entry != entries.end() && !entry->module &&
                entry->title == item.name && entry->iconKey == "shortcut:" + item.iconPreset && a.planner_.shortcutID(app) == std::optional<std::string_view>(item.id);
        }
        step.check(ordered && std::size_t(right.end() - at) == items.size() + 2, "Saved apps follow Power in their stored order and + Add App stays last");
        // HUDAppShortcutPresentation.icon: the published original application
        // image for the "original" preset only, shared with the navigation.
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
        step.check(icons, "Navigation entries name exactly the published App icon images");
    }
    // The navigation entry of a saved app carries its own application icon.
    bool navigationIcon(std::string_view id) {
        plan();
        const auto entries = a.planner_.entries();const auto app = action("app." + std::string(id));
        const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.action == app; });
        return entry != entries.end() && !entry->originalImageKey.empty() && entry->originalImages == a.images_;
    }
    static std::string utf8(const fs::path& path) { const auto value = path.u8string();return std::string(value.begin(), value.end()); }
    // A localized UIA string as the peers report it (the coverage language).
    std::wstring wide(std::string_view english, std::string_view chinese) const {
        const auto value = text(a.language_, english, chinese);
        if (value.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
        std::wstring out(std::size_t(n > 0 ? n : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), out.data(), n);
        return out;
    }
    static tools::ShortcutKeyModifiers control() { tools::ShortcutKeyModifiers k;k.modified = k.control = true;return k; }
    // The UIA peer under the centre of a canvas action, by hit test.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> peer(std::string_view id) {
        const auto actions = state().actions();
        const auto found = std::find_if(actions.begin(), actions.end(), [&](const auto& v) { return m::shortcutActionID(v) == id; });
        const auto rect = found != actions.end() ? found->rect : m::shortcutNameRect;
        const auto screen = a.automationHost(a.automationRoot_).toScreen(rect);
        step.check(screen.has_value(), "A + Add App peer projects to the screen");
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> element;
        step.check(SUCCEEDED(a.automationFromPoint(screen->x + screen->width / 2, screen->y + screen->height / 2, &element)) && element, "The UIA hit test finds a peer");
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;VARIANT automationId;VariantInit(&automationId);
        step.check(SUCCEEDED(element.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &automationId)) && automationId.vt == VT_BSTR &&
                   std::wstring_view(automationId.bstrVal) == std::wstring(id.begin(), id.end()), "The hit peer carries its action's automation id");
        VariantClear(&automationId);
        return element;
    }
    std::wstring property(const Microsoft::WRL::ComPtr<IRawElementProviderFragment>& element, PROPERTYID id) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;VARIANT value;VariantInit(&value);std::wstring out;
        if (SUCCEEDED(element.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(id, &value)) && value.vt == VT_BSTR && value.bstrVal) out = value.bstrVal;
        VariantClear(&value);
        return out;
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
        step.check(!working() && !a.preview_->busy() && !a.inflight_, "The store restarts idle");
        a.tasks_.reset();
        const auto file = a.directory_ / "shortcuts.json", ledger = a.relinkRoot_ / "Migration" / "relink.json";
        ehud::data::detail::replaceFile(file, ehud::data::detail::readFile(file, m::shortcutMaximumBytes), a.seed_.archive, m::shortcutMaximumBytes);
        ehud::data::detail::replaceFile(ledger, ehud::data::detail::readFile(ledger, mig::MacRelinkLedger::maximumBytes), a.seed_.ledger, mig::MacRelinkLedger::maximumBytes);
        const auto syncs = a.relinkSyncs_;const auto revision = a.listRevision_;
        a.tasks_ = a.makeTasks();a.loadPending_ = true;a.pump();settle();frames(.1);
        step.check(a.listRevision_ == revision + 1 && a.relinkSyncs_ == syncs + 1 && a.lastRelink_ == m::ShortcutRelinkSync{0, 1, 1} && !a.relinkFailures_,
                   "The restarted store loads once and reconciles the ledger");
    }
    // A plain close and the next opening (no launch): a new HUD session.
    void reopenSession() {
        a.closing(t);t += .5;a.concealed(t);
        step.check(!a.overlayVisible_ && !a.preview_->acceptsInput(), "The HUD closed");
        a.setOverlayVisible(true, t);frames(.4);
        step.check(state().active() && a.preview_->acceptsInput(), "+ Add App is interactive in the new session");
    }
};

void AppShortcutArea::coverage(app::ModuleCoverage& step) {
    Driver d{*this, step, step.time};
    if (!step.module) {
        // Per-cycle area step: nothing pending, the disk equals the shown
        // list and the navigation keeps saved apps between Power and Add App.
        step.check(!tasks_->busy() && !launch_ && !chooser_ && !loadFailed_ && !preview_->busy() && !relinkFailures_, "App shortcuts are idle between cycles");
        step.check(d.disk() == preview_->state().file(), "The data root still holds the shown list");
        d.checkNavigation();
        // A navigation launch of the imported record shows its relink error
        // in + Add App and selects it when another module is shown.
        const auto& seeded = preview_->state().file().items.front();
        const auto selects = coverageSelectRequests_, closes = coverageCloseRequests_;
        const bool elsewhere = overlayVisible_ && host_.selectedModule() != core::Module::addApp;
        step.check(activate(d.action("app." + seeded.id), d.t) && !launch_ && preview_->state().error() && coverageCloseRequests_ == closes &&
                   coverageSelectRequests_ == selects + (elsewhere ? 1u : 0u), "A navigation launch of an imported app shows its error in + Add App");
        preview_->showError(std::nullopt, d.t);
        d.frame();return;
    }
    step.check(*step.module == core::Module::addApp, "App shortcuts present only + Add App");
    d.settle();d.frames(.1);
    const auto& state = preview_->state();
    const auto seeded = state.file();
    step.check(state.active() && !state.editing() && !state.error() && !preview_->entries().empty() && seeded.items.size() == 1 && !seeded.items[0].native() &&
               d.disk() == seeded, "+ Add App shows the seeded imported Mac shortcut from the data root");
    // The load reconciled the import ledger on the repository worker: the
    // removed shortcut's row is dismissed, the imported one stays listed.
    step.check(relinkSyncs_ >= 1 && lastRelink_ == m::ShortcutRelinkSync{0, 1, 1} && !relinkFailures_, "The load reconciles Migration/relink.json once");
    d.checkLedger(std::nullopt);
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
    {
        const auto selects = coverageSelectRequests_;
        preview_->showError(std::nullopt, d.t);
        step.check(activate(d.action("app." + seeded.items[0].id), d.t) && state.error() && !launch_ && coverageCloseRequests_ == closes && coverageSelectRequests_ == selects,
                   "Its navigation action shows the same error in the selected + Add App");
    }

    // 3. A missing target fails inspection and opens no draft.
    d.choose(applications / "Missing.exe");
    step.check(!state.editing() && state.error() && state.file().items.size() == 2, "A missing application reports its error and opens no draft");

    // 3b. A packaged application from the chooser's applications mode
    //     ("shell:AppsFolder\<AUMID>") opens its draft; a document does not.
    const auto logos = synthetic_->logos.load();
    d.choose(std::string("shell:AppsFolder\\Synthetic.Packaged_8wekyb3d8bbwe!App"));
    step.check(state.editing() && state.candidate()->name == "Packaged Synthetic" && state.candidate()->locator["windowsTarget"]["kind"] == J("packagedApp") &&
               state.candidate()->locator["windowsTarget"]["appUserModelID"] == J("Synthetic.Packaged_8wekyb3d8bbwe!App"), "A chosen packaged application opens its draft");
    {
        // Its App icon is the package's logo (the synthetic fixture through
        // the production decoder): the opaque glyph's exact colour at the centre.
        d.settle();
        const auto key = applicationKey(state.candidate()->locator);
        const auto found = icons_.find(std::string{});
        const auto image = d.icon("", key) ? images_->acquire(iconKey({}), found->second.revision) : nullptr;
        const auto center = image ? (std::size_t(image->height() / 2) * image->width() + image->width() / 2) * 4 : 0;
        step.check(image && synthetic_->logos == logos + 1 && image->width() == g::appShortcutIconPixels &&
                   image->premultipliedBGRA()[center] == 90 && image->premultipliedBGRA()[center + 1] == 160 && image->premultipliedBGRA()[center + 2] == 40 && image->premultipliedBGRA()[center + 3] == 255,
                   "The packaged draft shows its own logo from the package");
    }
    d.click(188, 288);d.frames(.32);
    step.check(!state.editing(), "Cancel discards the packaged draft");
    {
        const auto requests = coverageChooserRequests_;
        d.click(329, 16);
        step.check(coverageChooserRequests_ == requests + 1 && chooser_, "+ Choose app requests the chooser again");
        chosen(*chooser_, {false, false, {Driver::utf8(applications / "Readme.txt")}});d.frame();
        step.check(!state.editing() && state.error() == invalidApplicationText(language_), "A chosen document asks for a Windows application");
        const auto request = chooserRequest(core::Language::english);
        step.check(request.applications && request.singleFile && request.filterSpec == "*.exe;*.lnk" && request.filterName == "Applications" && request.title == "Choose an application",
                   "The chooser asks the shared dialog for one application (AppsFolder) through the application filter");
        // An item the dialog cannot name as an application (an empty choice).
        d.click(329, 16);
        step.check(chooser_.has_value(), "+ Choose app requests the chooser once more");
        chosen(*chooser_, {false, false, {}});d.frame();
        step.check(!state.editing() && state.error() == invalidApplicationText(language_), "A choice that names no application asks for a Windows application");
    }

    // 4. A launch that fails after the close reports in + Add App and reopens it.
    synthetic_->failLaunch = true;
    d.click(100, Driver::row(d.index(alpha.id)));
    d.close(false);
    synthetic_->failLaunch = false;
    step.check(!launch_ && state.error() && coverageReopenRequests_ == reopened + 1 && launchesOpened_ == opened && synthetic_->launched == launched,
               "A failed launch keeps the shortcut, shows its error and reopens + Add App");
    d.reopen();
    step.check(launchError_ && state.error() == launchError_, "The reopened + Add App shows the failed launch's error");

    // 5. Add a shell link: keep the inspected name, then rename it through the
    //    name control; the new draft clears the previous error.
    d.choose(applications / "Beta.lnk");
    step.check(state.editing() && !state.error() && state.draftName() == "Beta Synthetic", "A new draft clears the previous error");
    d.settle();
    step.check(d.icon("", applicationKey(state.candidate()->locator)), "The draft shows the chosen application's own icon");
    step.check(preview_->key({app::KeyKind::down, VK_RETURN}, false, d.t) && !preview_->editingName(), "Return keeps the inspected name");
    d.click(230, 90); // the name control
    d.type(u"Beta");
    d.commit(320, 288, "Save shortcut commits the shell link");
    const auto beta = state.file().items.back();
    step.check(state.file().items.size() == 3 && beta.name == "Beta" && beta.iconPreset == "original" && beta.locator["windowsTarget"]["kind"] == J("shellLink"),
               "The shell link is saved after the executable with the App icon preset");
    d.checkNavigation();
    step.check(d.action("app." + alpha.id) == alphaAction, "Adding an app keeps the existing navigation action");
    d.settle();
    step.check(d.icon(beta.id, applicationKey(beta.locator)) && !icons_.contains(alpha.id) && !icons_.contains(seeded.items[0].id),
               "Only rows with the App icon preset and a Windows target request the application icon");
    d.checkNavigation();
    step.check(d.navigationIcon(beta.id) && !d.navigationIcon(alpha.id) && !d.navigationIcon(seeded.items[0].id),
               "The right-side navigation shows the shell link's own application icon");

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

    // 7b. Drag and drop (SystemHUDView.acceptsAppShortcutDrop): exactly one
    //     application file over the canvas opens its draft.
    const auto inside = step.project({200, 160}, 400, 334), outside = step.project({-40, -40}, 400, 334);
    step.check(inside && outside && covers(*inside) && !covers(*outside), "Drop points project inside and outside the + Add App canvas");
    const std::array two{Driver::utf8(applications / "Gamma.exe"), Driver::utf8(applications / "Delta.lnk")};
    const std::array gamma{two[0]}, document{Driver::utf8(applications / "Readme.txt")};
    step.check(!drop(two, *inside, d.t) && !drop(document, *inside, d.t) && !drop(gamma, *outside, d.t) && !state.editing() && !preview_->busy(),
               "Two files, a document or a drop beside the canvas are refused");
    step.check(!dropHover(two, *inside, d.t) && !dropHover(document, *inside, d.t) && !dropHover(gamma, *outside, d.t) && !state.dropTarget(),
               "Two files, a document or a drag beside the canvas show no drop target");
    step.check(dropHover(gamma, *inside, d.t) && state.dropTarget() && pointerLocked(), "One application over the canvas shows its drop target and locks input");
    d.frame();
    step.check(!dropHover(gamma, std::nullopt, d.t) && !state.dropTarget() && !pointerLocked(), "Leaving the canvas ends the drop target");
    step.check(dropHover(gamma, *inside, d.t) && state.dropTarget(), "The application is dragged back over the canvas");
    d.frame();
    step.check(drop(gamma, *inside, d.t) && !state.dropTarget(), "A dropped application is accepted and ends the drop target");
    d.settle();d.frames(.32);
    step.check(state.editing() && state.candidate()->name == "Gamma Synthetic" && !state.editingID() && !state.error(), "A dropped application opens its new draft");
    // Closing ends the session: the next opening shows the list from its top
    // without the draft, and still the failed launch's error.
    d.reopenSession();
    step.check(!state.editing() && state.scrollOffset() == 0 && state.file().items.size() == 3 && launchError_ && state.error() == launchError_,
               "The next opening starts a new + Add App session that keeps only the launch error");

    // 7c. Command-V (HUDAppShortcutInteraction.keyDown -> importPasteboard)
    //     reads the synthetic clipboard's files; then the name field's Edit
    //     menu (Cut/Copy/Paste) and its UIA edit peer.
    const app::KeyEvent paste{app::KeyKind::down, 'V'};
    const auto reads = syntheticClipboard_->fileReads;
    syntheticClipboard_->files.clear();
    step.check(!key(paste, Driver::control(), d.t) && syntheticClipboard_->fileReads == reads + 1 && !state.editing(), "Command-V without application files is left unhandled");
    syntheticClipboard_->files.assign(two.begin(), two.end());
    step.check(key(paste, Driver::control(), d.t) && state.error() == text(language_, "Choose one application to customize.", "请选择一个应用进行设置。") && !state.editing(),
               "Pasting two files asks for one application");
    syntheticClipboard_->files.assign(document.begin(), document.end());
    step.check(key(paste, Driver::control(), d.t) && state.error() == invalidApplicationText(language_) && !state.editing(), "Pasting a document asks for a Windows application");
    syntheticClipboard_->files = {two[1]};
    step.check(key(paste, Driver::control(), d.t), "A pasted application is accepted");
    d.settle();d.frames(.32);
    step.check(state.editing() && state.candidate()->name == "Delta Synthetic" && !state.error() && preview_->editingName() && preview_->fieldText() == "Delta Synthetic",
               "A pasted application opens its draft with the selected name field");
    {
        // HUDProjectedTextView.projectedCursor: the I-beam over the open field only.
        const auto field = step.project({230, 90}, 400, 334), beside = step.project({230, 190}, 400, 334);
        step.check(field && beside && cursorAt(*field) == std::optional(app::ModuleCursor::text) && !cursorAt(*beside), "The open name field answers the I-beam, the canvas beside it does not");
        // The field opened inside a frame: one immediate wake re-lists UIA.
        step.check(nextWakeTime(d.t) == std::optional(d.t) && deadline(d.t) && !nextWakeTime(d.t) && !deadline(d.t), "The field opening wakes the host once to re-list UI Automation");
        step.check(!contentChanged_, "Completions were reported through the utility broadcast");
    }
    const auto writes = syntheticClipboard_->writes;
    step.check(preview_->key({app::KeyKind::down, 'C'}, Driver::control(), d.t) && syntheticClipboard_->writes == writes + 1 && syntheticClipboard_->value &&
               syntheticClipboard_->value->text == u"Delta Synthetic", "Ctrl+C copies the selected name");
    syntheticClipboard_->value = g::NotesClipboardText{u"Delta\nPasted", std::nullopt};
    step.check(preview_->key({app::KeyKind::down, 'V'}, Driver::control(), d.t) && preview_->fieldText() == "Delta Pasted", "Ctrl+V pastes text with its line break as a space");
    step.check(preview_->key({app::KeyKind::down, 'A'}, Driver::control(), d.t) && preview_->key({app::KeyKind::down, 'X'}, Driver::control(), d.t) &&
               preview_->fieldText() == "" && syntheticClipboard_->value && syntheticClipboard_->value->text == u"Delta Pasted", "Ctrl+X cuts the selected name");
    (void)automationRefresh(automationRoot_);
    step.check(automationCount() == state.actions().size(), "UIA lists the draft's actions and its name field instead of Rename");
    {
        const auto field = d.peer(nameFieldID);
        Microsoft::WRL::ComPtr<IValueProvider> value;
        // The open field is the first responder: it holds keyboard focus
        // exactly while the HUD does.
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;VARIANT has;VariantInit(&has);
        step.check(SUCCEEDED(field.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_HasKeyboardFocusPropertyId, &has)) && has.vt == VT_BOOL &&
                   (has.boolVal == VARIANT_TRUE) == focused_, "The name field holds keyboard focus exactly while the HUD does");
        step.check(SUCCEEDED(field.As(&value)) && d.property(field, UIA_NamePropertyId) == d.wide("Shortcut name", "快捷方式名称") &&
                   d.property(field, UIA_HelpTextPropertyId) == d.wide("Return applies the name. Save shortcut stores your changes.", "回车应用名称，点击保存快捷方式以保存修改。"),
                   "The name field is a named edit peer");
        step.check(SUCCEEDED(value->SetValue(L"Delta Speech")) && preview_->fieldText() == "Delta Speech", "UIA SetValue replaces the name through its editor");
    }
    step.check(preview_->key({app::KeyKind::down, VK_RETURN}, false, d.t) && !preview_->editingName() && state.draftName() == "Delta Speech", "Return applies the edited name");
    {
        const auto field = step.project({230, 90}, 400, 334);
        d.frame();
        step.check(field && !cursorAt(*field) && deadline(d.t) && !nextWakeTime(d.t), "A closed name field shows the source cursor and re-lists UIA once");
    }
    (void)automationRefresh(automationRoot_);
    {
        const auto cancel = d.peer("apps:cancel");
        Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
        step.check(d.property(cancel, UIA_HelpTextPropertyId) == d.wide("Edit app shortcut", "编辑应用快捷方式") && SUCCEEDED(cancel.As(&invoke)) && SUCCEEDED(invoke->Invoke()),
                   "The Cancel peer invokes Cancel");
    }
    d.frames(.32);
    step.check(!state.editing() && state.file().items.size() == 3 && !preview_->busy(), "The pasted draft is discarded");

    // 7d. UIA over the list: one peer per action, invoking the click's action.
    (void)automationRefresh(automationRoot_);
    step.check(automationCount() == state.actions().size() && automationCount() == 1 + 3 * state.file().items.size(),
               "UIA lists + Choose app and each row's Open, Edit and Remove");
    {
        const auto open = d.peer(m::shortcutActionID({m::ShortcutActionKind::launch, beta.id, {}}));
        step.check(d.property(open, UIA_NamePropertyId) == d.wide("Open ", "打开 ") + L"Beta" && d.property(open, UIA_HelpTextPropertyId) == d.wide("Application shortcuts", "应用快捷方式"),
                   "A row peer is named after its app");
        const auto choose = d.peer("apps:choose");
        Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
        const auto requests = coverageChooserRequests_;
        step.check(SUCCEEDED(choose.As(&invoke)) && SUCCEEDED(invoke->Invoke()) && coverageChooserRequests_ == requests + 1 && chooser_, "The + Choose app peer requests the chooser");
    }
    chosen(*chooser_, {true, false, {}});
    d.frames(.1);
    step.check(!chooser_ && !preview_->presentingPanel() && !state.error() && !state.editing(), "A cancelled chooser leaves the list unchanged");
    automationDisconnect();
    step.check(automationCount() == 0, "Closing the UIA tree disconnects every peer");

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
    step.check(!icons_.contains(alpha.id) && !icons_.contains(beta.id) && icons_.size() <= maximumIcons, "Removed shortcuts release their icon images");
    d.checkNavigation();
    step.check(!planner_.shortcutID(alphaAction), "A removed app's action launches nothing");

    // 12. Relink the imported Mac shortcut (Edit, + Choose app, Save): it
    //     keeps its identity, name, icon preset, date and position and gains
    //     the Windows target; Migration/relink.json resolves its row with
    //     that application (record first, ledger second, on the worker).
    {
        const auto& imported = seeded.items[0];
        d.click(337, Driver::row(0));d.settle();d.frames(.32);
        step.check(state.editing() && state.editingID() == imported.id && !state.candidate()->locator.contains("referencePlatform"), "Edit opens the imported shortcut with its Mac locator");
        const auto relinkedPath = applications / "Relinked.exe";
        d.choose(relinkedPath);
        step.check(state.editing() && state.editingID() == imported.id && state.candidate()->locator["referencePlatform"] == J("windows") && state.draftName() == imported.name &&
                   state.draftIcon() == imported.iconPreset, "A chosen Windows application relinks the imported draft and keeps its name and icon");
        const auto syncs = relinkSyncs_;
        d.commit(320, 288, "Save shortcut commits the relink");
        const auto target = Driver::utf8(relinkedPath);
        const auto& relinked = d.record(imported.id);
        step.check(d.index(imported.id) == 0 && relinked.native() && relinked.name == imported.name && relinked.iconPreset == imported.iconPreset &&
                   relinked.createdAt == imported.createdAt && relinked.locator["windowsTarget"]["path"] == J(target), "The relinked shortcut keeps its identity and names its Windows application");
        step.check(relinkSyncs_ == syncs + 1 && lastRelink_ == m::ShortcutRelinkSync{1, 0, 0} && !relinkFailures_, "Saving the relink resolves its ledger row on the repository worker");
        d.checkLedger(target);
        d.settle();
        step.check(d.icon(imported.id, applicationKey(relinked.locator)) && d.navigationIcon(imported.id), "The relinked shortcut shows its application's icon in the navigation");
        // A store restart returns the seeded Mac archive and ledger.
        d.restartStore();
        step.check(state.file() == seeded && d.disk() == seeded && !icons_.contains(imported.id) && !d.navigationIcon(imported.id) && !state.editing(),
                   "The restarted store shows the imported Mac shortcut again without an application icon");
        d.checkLedger(std::nullopt);
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
