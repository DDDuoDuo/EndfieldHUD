#include "app/areas/shelf_module.hpp"
#ifdef _WIN32
#include "core/data/file_shelf_store.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "native/file_shelf_files.hpp"
#include "native/layer_raster.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "tools/shelf_owner.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endfield::shelf {
namespace {
namespace fs = std::filesystem;
namespace mod = modules;
namespace d = modules::design;
namespace data = ehud::data;
namespace mig = ehud::migration;
using Microsoft::WRL::ComPtr;
constexpr wchar_t windowClass[] = L"EndfieldShelfCoverageOwner.v2";
constexpr UINT firstShelfMessage = tools::ShelfService::noticeMessage, lastShelfMessage = tools::ShelfService::workMessage; // service notices/actions
void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("File Shelf coverage: ") + why); }
std::string utf8(const fs::path& path) { const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    need(bool(out), "synthetic file written");
}
core::ModulePresentationSample presented(bool shown) {
    core::ModulePresentationSample s;
    const auto module = shown ? core::Module::fileShelf : core::Module::clipboard;
    s.selected = s.requested = module;
    s.current.module = module; s.current.contentFrame = core::moduleLocalContentFrame(module);
    s.current.designScreen = true; s.current.hostClip = false; s.current.attached = true; s.current.opacity = 1;
    s.acceptsModuleInput = true;
    return s;
}

// A private Shelf (service, design Context, owner) on a NEW data root and a
// never-shown window: what the coverage scenario drives.
struct PrivateShelf {
    native::LayerRasterizer& raster;
    std::unique_ptr<tools::DesignOverlayOwner> overlay;
    std::unique_ptr<tools::ShelfService> service;
    std::unique_ptr<tools::DesignModuleOwner> owner;
    double time{};
    PrivateShelf(HWND window, native::LayerRasterizer& r, tools::ShelfServiceOptions options, app::UtilityExecutor* utility, const app::ClientMetrics& metrics, core::Language language) : raster(r) {
        tools::DesignOverlayOptions o; o.raster.pixelsPerPoint = 1; o.window = window;
        overlay = std::make_unique<tools::DesignOverlayOwner>(raster, o);
        service = std::make_unique<tools::ShelfService>(window, std::move(options));
        service->setUtility(utility); // as EndfieldHUD.exe's host does
        tools::DesignOwnerOptions ownerOptions; ownerOptions.name = "fileShelfCoverage"; ownerOptions.routing = tools::ShelfService::routing(); ownerOptions.raster.pixelsPerPoint = 1;
        ownerOptions.now = [this] { return time; };
        owner = service->makeOwner(raster, overlay->context(), std::move(ownerOptions));
        owner->resize(metrics); overlay->resize(metrics);
        app::ModuleAppearance appearance; appearance.language = language; appearance.reduceMotion = true;
        overlay->setAppearance(appearance, 0); owner->setAppearance(appearance, 0);
        overlay->setOverlayVisible(true, 0); owner->setOverlayVisible(true, 0);
    }
    ~PrivateShelf() { owner.reset(); service.reset(); overlay.reset(); }
    // Presents (or withdraws) the Shelf on a 1280 x 800 canvas; nothing is published (no renderer in a coverage step).
    void present(bool shown, double t) {
        time = std::max(time, t);
        core::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, 1280, 800}; chrome.module = shown ? core::Module::fileShelf : core::Module::clipboard;
        const core::Matrix4 center{};
        const auto s = presented(shown);
        const app::ModuleFrame f{center, chrome, s, 1, time, true};
        overlay->update(f); owner->update(f);
    }
    mod::FileShelfCanvas& canvas() { return *service->canvas(); }
    const mod::FileShelfItem* item(std::string_view name) {
        const auto& items = canvas().items();
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto& value) { return value.name == name; });
        return found == items.end() ? nullptr : &*found;
    }
    std::vector<std::wstring> automationIDs() {
        std::vector<std::wstring> out;
        for (std::size_t n = 0; n < owner->automationCount(); ++n) {
            ComPtr<IRawElementProviderFragment> fragment; ComPtr<IRawElementProviderSimple> simple;
            if (FAILED(owner->automationChild(n, &fragment)) || !fragment || FAILED(fragment.As(&simple))) continue;
            VARIANT value; VariantInit(&value);
            if (SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value)) && value.vt == VT_BSTR && value.bstrVal) out.emplace_back(value.bstrVal);
            VariantClear(&value);
        }
        return out;
    }
    // SelectionItem.Select of a tile (a tile is a tab).
    HRESULT select(std::wstring_view id) {
        for (std::size_t n = 0; n < owner->automationCount(); ++n) {
            ComPtr<IRawElementProviderFragment> fragment; ComPtr<IRawElementProviderSimple> simple;
            if (FAILED(owner->automationChild(n, &fragment)) || !fragment || FAILED(fragment.As(&simple))) continue;
            VARIANT value; VariantInit(&value);
            const bool match = SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value)) && value.vt == VT_BSTR && value.bstrVal && id == value.bstrVal;
            VariantClear(&value);
            if (!match) continue;
            ComPtr<IUnknown> pattern; ComPtr<ISelectionItemProvider> provider;
            if (FAILED(simple->GetPatternProvider(UIA_SelectionItemPatternId, &pattern)) || !pattern || FAILED(pattern.As(&provider))) return E_FAIL;
            return provider->Select();
        }
        return E_FAIL;
    }
};

class ShelfCoverageArea final : public app::ModuleOwner {
public:
    explicit ShelfCoverageArea(const app::ApplicationModuleContext& c) : root_(c.coverageRoot), language_(c.host->appearance().language), utility_(c.utility), metrics_(c.metrics) {
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = windowClass;
        need(RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "private owner class registered");
        // Private, never-shown owner of this Shelf's notices; the HUD HWND
        // keeps the built-in Shelf's routes untouched.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, this);
        need(window_ && !IsWindowVisible(window_), "private owner window stays hidden");
        need(utility_ != nullptr, "the shared utility executor is given");
        tools::ShelfServiceOptions options{root_ / "shelf-data", false, false};
        options.recordEvent = [this](mod::EventKind kind, mod::EventMetadata metadata) { events_.push_back({kind, std::move(metadata)}); };
        options.openOperation = [this](const data::ShelfFileAccess&) { ++opened_; return S_OK; };
        // A private rasterizer, design Context and owner: this area presents
        // its Shelf for the UI Automation steps, outside the harness's measured
        // module raster usage and the shared overlay.
        shelf_ = std::make_unique<PrivateShelf>(window_, raster_, std::move(options), utility_, metrics_, language_);
    }
    ~ShelfCoverageArea() override {
        shelf_.reset();
        if (window_) DestroyWindow(window_);
    }
    std::string_view name() const noexcept override { return "fileShelfCoverage"; }
    void resize(const app::ClientMetrics& metrics) override { metrics_ = metrics; if (shelf_) { shelf_->owner->resize(metrics); shelf_->overlay->resize(metrics); } }
    void release(native::Renderer& renderer) override {
        if (released_ || !shelf_) return;
        released_ = true;
        shelf_->owner->release(renderer); shelf_->overlay->release(renderer);
    }
    void coverage(app::ModuleCoverage& step) override;

private:
    struct Record { mod::EventKind kind; mod::EventMetadata metadata; };
    fs::path root_;
    core::Language language_;
    app::UtilityExecutor* utility_{};
    app::ClientMetrics metrics_;
    HWND window_{};
    std::vector<Record> events_;
    unsigned opened_{};
    bool released_{};
    native::LayerRasterizer raster_;
    std::unique_ptr<PrivateShelf> shelf_;

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams));
        auto* self = reinterpret_cast<ShelfCoverageArea*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self && self->shelf_ && message >= firstShelfMessage && message <= lastShelfMessage) {
            try { (void)self->shelf_->owner->message({hwnd, message, wParam, lParam}, self->shelf_->time); } catch (...) {}
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    // Delivers this Shelf's queued private notices (picker, utility, content).
    void pump() {
        MSG message{};
        for (unsigned n = 0; n < 64 && PeekMessageW(&message, window_, firstShelfMessage, lastShelfMessage, PM_REMOVE); ++n) DispatchMessageW(&message);
    }
    bool recorded(std::size_t index, mod::EventKind kind, std::string_view key, std::string_view value) const {
        if (index >= events_.size() || events_[index].kind != kind) return false;
        const auto found = events_[index].metadata.find(key);
        return found != events_[index].metadata.end() && found->second == value;
    }
};

void ShelfCoverageArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no module; one scenario per cycle
    auto& s = *shelf_;
    auto& shelf = *s.service;
    s.time = std::max(s.time, step.time);
    const auto next = [&] { s.time += .001; return s.time; };
    const auto cycle = root_ / ("cycle-" + std::to_string(step.cycle));
    std::error_code error;
    fs::remove_all(cycle, error);
    need(fs::create_directories(cycle), "cycle directory created");
    const auto report = cycle / "Quarterly report.txt", notes = cycle / "Synthetic notes.ehudshelf", kept = cycle / "Kept folder", recycled = cycle / "Recycled folder";
    write(report, "Synthetic shelf report\n");
    write(notes, "Synthetic notes without a registered preview handler\n");
    need(fs::create_directory(kept) && fs::create_directory(recycled), "synthetic folders created");
    write(kept / "one.txt", "1"); write(kept / "two.txt", "2");
    events_.clear();
    s.present(true, next());

    // Import: one reference per original, Event Log shelfAdded per new item;
    // nothing is selected (FileShelfCanvas.importURLs).
    step.check(s.canvas().itemCount() == 0, "The coverage Shelf starts every cycle empty");
    const std::vector<std::string> paths{utf8(report), utf8(notes), utf8(kept), utf8(recycled)};
    step.check(shelf.importFiles(paths, next()) && s.canvas().itemCount() == 4, "Four synthetic references are added");
    step.check(events_.size() == 4 && recorded(0, mod::EventKind::shelfAdded, "filename", "Quarterly report.txt") &&
               recorded(1, mod::EventKind::shelfAdded, "filename", "Synthetic notes.ehudshelf") && recorded(2, mod::EventKind::shelfAdded, "filename", "Kept folder") &&
               recorded(3, mod::EventKind::shelfAdded, "filename", "Recycled folder"), "Every new reference records shelfAdded with its filename");
    step.check(shelf.importFiles(paths, next()) && s.canvas().itemCount() == 4 && events_.size() == 4, "A duplicate-only import adds and records nothing");
    step.check(s.canvas().selectedIDs().empty(), "Adding references selects nothing");
    step.check(s.item("Quarterly report.txt") && !s.item("Quarterly report.txt")->typeDescription.empty(), "Files carry a type description");

    // Stale paths: renamed and moved originals are found again by identity.
    const auto renamed = cycle / "Renamed report.txt", moved = cycle / "Moved" / "Synthetic notes.ehudshelf", bin = cycle / "$Recycle.Bin";
    need(fs::create_directory(cycle / "Moved") && fs::create_directory(bin), "synthetic destinations created");
    fs::rename(report, renamed);
    fs::rename(notes, moved);
    fs::rename(recycled, bin / "Recycled folder");
    shelf.refresh(next());
    step.drain(); pump();
    const auto* first = s.item("Renamed report.txt");
    step.check(first && !first->availabilityError && first->lastKnownPath == utf8(renamed), "A renamed original is found again by identity under its new name");
    const auto* second = s.item("Synthetic notes.ehudshelf");
    step.check(second && !second->availabilityError && second->lastKnownPath == utf8(moved), "A moved original is found again by identity in its new folder");
    step.check(s.item("Recycled folder") && s.item("Recycled folder")->availabilityError, "An original moved into a Recycle Bin folder stays unavailable");
    step.check(s.item("Kept folder") && !s.item("Kept folder")->availabilityError, "An untouched folder stays available");
    {
        native::NativeFileShelfFiles files;
        data::FileShelfStore saved(root_ / "shelf-data", files.platform());
        const auto& rows = saved.items();
        const auto has = [&](const fs::path& path) { return std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row.windowsPath == utf8(path); }); };
        step.check(rows.size() == 4 && has(renamed) && has(moved), "The renewed paths are saved like renewed Mac bookmarks");
    }
    fs::remove(renamed);
    shelf.refresh(next()); step.drain(); pump();
    step.check(s.item("Renamed report.txt") && s.item("Renamed report.txt")->availabilityError, "A deleted original becomes unavailable");
    step.check(s.canvas().sizeTag(*s.item("Renamed report.txt"), language_) == std::optional<std::string>(core::localized("Unavailable", "不可用", language_)), "Its tile says Unavailable");

    // A Mac-imported Shelf item (the import area's Migration/relink.json) is an
    // unavailable tile until a file on this PC is chosen for it.
    const auto recordID = data::makeUUID();
    mig::MacRelinkItem imported;
    imported.id = data::makeUUID(); imported.store = std::string(mig::relinkShelfItem); imported.recordID = recordID;
    imported.displayName = "Imported plan.key"; imported.lastKnownPath = "/Users/doctor/Desktop/Imported plan.key"; imported.kind = "file";
    imported.macReference = data::Json::Object{{"id", recordID}, {"name", "Imported plan.key"}, {"typeDescription", "Keynote Presentation"},
        {"lastKnownPath", imported.lastKnownPath}, {"byteCount", 4096}, {"isDirectory", false}, {"createdAt", 700000000.0 + step.cycle}, {"bookmark", "Ym9va21hcms="}, {"isSecurityScoped", true}};
    {
        const auto shelfData = root_ / "shelf-data";
        std::vector<mig::MacRelinkItem> rows;
        try { rows = mig::MacRelinkLedger(shelfData).items(); } catch (const std::exception&) {}
        rows.push_back(imported);
        fs::create_directories(shelfData / "Migration");
        write(shelfData / "Migration" / "relink.json", mig::MacRelinkLedger::encode(rows));
    }
    shelf.refresh(next());
    step.drain(); pump(); // the ledger is read (and reconciled) on the utility worker
    const auto* listed = s.item("Imported plan.key");
    step.check(shelf.pendingRelinks() == 1 && listed && listed->awaitingRelink && listed->availabilityError && listed->id == recordID,
               "The imported item is listed as an unavailable tile with its Mac identifier");
    step.check(s.canvas().dragSelection(recordID).empty(), "An imported item is never dragged");

    // UI Automation of the presented Shelf: the header actions and one tab per
    // tile; Select acts like a click; the card adds its actions.
    s.present(true, next());
    step.check(s.owner->automationRefresh(nullptr) || s.owner->automationCount() > 0, "The presented Shelf lists its tree");
    auto ids = s.automationIDs();
    const auto has = [&](std::wstring_view id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    step.check(has(L"shelf.add") && has(L"shelf.clear") && has(L"file:1") && has(L"file:5") && !has(L"props.open"), "Add, Clear and one tab per tile are listed");
    step.check(s.select(L"file:5") == S_OK && s.canvas().selectedIDs().size() == 1 && s.canvas().selectedIDs().contains(recordID), "Selecting the imported tile through UI Automation");
    s.present(true, next()); (void)s.owner->automationRefresh(nullptr); ids = s.automationIDs();
    step.check(has(L"props.relink") && has(L"props.reveal") && has(L"props.remove") && !has(L"props.open"), "The imported item's card offers Relink in place of Open");
    step.check(shelf.perform("shelf:" + recordID + ":relink", next()), "Relink is offered");
    const auto request = shelf.takeAction();
    step.check(request && request->kind == tools::ShelfServiceAction::Kind::choose, "Relink asks for the file picker");

    // The picker's choice (synthetic): the reference keeps the Mac identifier.
    const auto replacement = cycle / "Plan on this PC.key";
    write(replacement, "Synthetic replacement\n");
    events_.clear();
    step.check(shelf.relink(recordID, std::vector<std::string>{utf8(replacement)}, next()) && shelf.pendingRelinks() == 0, "The chosen file relinks the imported item");
    step.check(s.item("Plan on this PC.key") && s.item("Plan on this PC.key")->id == recordID && !s.item("Plan on this PC.key")->availabilityError, "The relinked reference keeps the imported identifier");
    step.check(events_.size() == 1 && recorded(0, mod::EventKind::shelfAdded, "filename", "Plan on this PC.key"), "Relinking records shelfAdded");
    step.drain(); pump(); // resolveMacRelink runs on the utility worker after the record write
    {
        const mig::MacRelinkLedger ledger(root_ / "shelf-data");
        const auto* entry = ledger.find(imported.id);
        step.check(entry && entry->state == mig::RelinkState::resolved && entry->windowsPath == utf8(replacement), "The import ledger entry is resolved with the chosen path");
    }

    // The properties card: its values are read on the shared utility worker.
    const auto keptID = s.item("Kept folder")->id;
    const auto reads = shelf.panels().propertyReads;
    s.canvas().click(keptID); s.present(true, next());
    step.check(s.canvas().hasPropertiesCardForTesting() && shelf.panels().propertyReads == reads + 1, "The folder's card reads its properties on the utility worker, not the HUD thread");
    step.drain(); pump();
    const auto& properties = s.canvas().propertiesForTesting();
    step.check(properties.contains(keptID) && properties.at(keptID).itemCount == std::optional<std::int64_t>(2) && properties.at(keptID).modified, "The worker delivered the folder's item count and dates");
    // Open: the Shell's verb from the host's action message, the HUD stays.
    step.check(shelf.perform("shelf:" + keptID + ":open", next()), "Open is accepted");
    const auto open = shelf.takeAction();
    step.check(open && open->kind == tools::ShelfServiceAction::Kind::open && open->itemID == keptID, "Open is handed to the host's action message");
    shelf.open(open->itemID, next());
    step.check(opened_ == step.cycle + 1, "The revalidated item opens through the Shell handoff");

    // Remove and clear: references only, with their Event Log records.
    events_.clear();
    const auto deleted = s.item("Renamed report.txt")->id;
    step.check(shelf.perform("shelf:" + deleted + ":remove", next()) && !s.item("Renamed report.txt") && events_.size() == 1 &&
               recorded(0, mod::EventKind::shelfRemoved, "filename", "Renamed report.txt"), "Removing a reference records shelfRemoved with its filename");
    const auto count = s.canvas().itemCount();
    step.check(shelf.perform("shelf:clear", next()) && s.overlay->context().overlay().isConfirming(), "Clear asks through the confirm bar first");
    s.overlay->context().overlay().decide(d::ConfirmState::Decision::confirm);
    step.check(s.canvas().itemCount() == 0, "Confirming clears every reference");
    step.check(events_.size() == 2 && recorded(1, mod::EventKind::shelfCleared, "count", std::to_string(count)), "Clearing records shelfCleared with the cleared count");
    step.check(fs::exists(moved) && fs::exists(kept) && fs::exists(bin / "Recycled folder"), "Removing and clearing references leave every original in place");
    s.overlay->context().overlay().reset();
    s.present(false, next());
    (void)s.owner->automationRefresh(nullptr);
    step.check(s.owner->automationCount() == 0, "A withdrawn Shelf lists nothing");

    // Windows names for the platform actions, in the HUD language.
    step.check(mod::shelfText(mod::ShelfText::revealInExplorer, language_) != core::localized("Reveal in Finder", "在访达中显示", language_), "The card's Reveal names File Explorer");
    fs::remove_all(cycle, error);
}
} // namespace

std::unique_ptr<app::ModuleOwner> shelfCoverageModule(const app::ApplicationModuleContext& context) {
    // The built-in "fileShelf" owner is the production path; this area runs
    // only in hidden coverage.
    if (context.mode != app::ApplicationAreaMode::hiddenCoverage || context.coverageRoot.empty() || !context.host) return nullptr;
    return std::make_unique<ShelfCoverageArea>(context);
}
} // namespace endfield::shelf
#else
namespace endfield::shelf {
std::unique_ptr<app::ModuleOwner> shelfCoverageModule(const app::ApplicationModuleContext&) { return nullptr; }
}
#endif
