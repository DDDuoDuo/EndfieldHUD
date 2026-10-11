#include "app/areas/shelf_module.hpp"
#ifdef _WIN32
#include "core/data/file_shelf_store.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "native/file_shelf_files.hpp"
#include "native/shelf_assets.hpp"
#include "native/layer_raster.hpp"
#include "tools/shelf_preview.hpp"
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
namespace data = ehud::data;
namespace mig = ehud::migration;
using Microsoft::WRL::ComPtr;
constexpr wchar_t windowClass[] = L"EndfieldShelfCoverageOwner.v1";
constexpr UINT firstShelfMessage = WM_APP + 185, lastShelfMessage = tools::ShelfPreview::workMessage; // ShelfPreview notices/actions
void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("File Shelf coverage: ") + why); }
std::string utf8(const fs::path& path) { const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    need(bool(out), "synthetic file written");
}

class ShelfCoverageArea final : public app::ModuleOwner {
public:
    ShelfCoverageArea(const app::ApplicationModuleContext& c, const fs::path& assets) : root_(c.coverageRoot), assets_(assets), language_(c.host->appearance().language), utility_(c.utility) {
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
        tools::ShelfPreviewOptions options{root_ / "shelf-data", false, false, {}};
        options.recordEvent = [this](mod::EventKind kind, mod::EventMetadata metadata) { events_.push_back({kind, std::move(metadata)}); };
        // A private rasterizer: this area presents the Shelf for its UI
        // Automation steps, outside the harness's measured module raster usage.
        shelf_ = std::make_unique<tools::ShelfPreview>(window_, raster_, assets_, std::move(options));
        shelf_->setUtility(utility_); // as EndfieldHUD.exe's host does
        shelf_->resize(c.metrics);
        shelf_->setLanguage(language_);
        metrics_ = c.metrics;
    }
    ~ShelfCoverageArea() override {
        shelf_.reset();
        if (window_) DestroyWindow(window_);
    }
    std::string_view name() const noexcept override { return "fileShelfCoverage"; }
    void resize(const app::ClientMetrics& metrics) override { metrics_ = metrics; shelf_->resize(metrics); }
    void setAppearance(const app::ModuleAppearance& appearance, double) override { language_ = appearance.language; shelf_->setLanguage(appearance.language); }
    void overlayClosing(double) override { shelf_->cancelPanels(); shelf_->cancelInteraction(); }
    void release(native::Renderer& renderer) override {
        if (released_) return;
        released_ = true;
        shelf_->release(renderer);
    }
    void coverage(app::ModuleCoverage& step) override;

private:
    struct Record { mod::EventKind kind; mod::EventMetadata metadata; };
    fs::path root_;
    native::NativeShelfAssets assets_;
    core::Language language_;
    app::UtilityExecutor* utility_{};
    HWND window_{};
    app::ClientMetrics metrics_;
    std::vector<Record> events_;
    native::LayerRasterizer raster_;
    std::unique_ptr<tools::ShelfPreview> shelf_;
    double time_{};
    bool released_{};

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams));
        auto* self = reinterpret_cast<ShelfCoverageArea*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self && self->shelf_ && message >= firstShelfMessage && message <= lastShelfMessage) {
            try { self->shelf_->message({hwnd, message, wParam, lParam}, self->time_); } catch (...) {}
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    // Delivers this Shelf's queued private notices (picker, icons, content).
    void pump() {
        MSG message{};
        for (unsigned n = 0; n < 32 && PeekMessageW(&message, window_, firstShelfMessage, lastShelfMessage, PM_REMOVE); ++n) DispatchMessageW(&message);
    }
    // Presents (or withdraws) a Shelf on a 1280 x 800 canvas. Presented content
    // is never published here (no renderer in a coverage step), so each Shelf
    // presented this way is a fresh instance used once.
    static void present(tools::ShelfPreview& shelf, bool shown, double time) {
        core::source::DesktopChromeSettings settings{{0, 0, 1280, 800}, 1, {}, core::Module::fileShelf, true};
        const auto center = core::source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
        core::ModulePresentation modules(shown ? core::Module::fileShelf : core::Module::clipboard);
        shelf.update(center, settings, modules.sample(time).presentation, 1, time);
    }
    static ComPtr<IRawElementProviderSimple> peer(const tools::ShelfPreview& shelf, std::string_view id) {
        const std::wstring wanted(id.begin(), id.end());
        for (std::size_t n = 0; n < shelf.automationCount(); ++n) {
            ComPtr<IRawElementProviderFragment> fragment; ComPtr<IRawElementProviderSimple> simple;
            if (FAILED(shelf.automationChild(n, &fragment)) || !fragment || FAILED(fragment.As(&simple))) continue;
            VARIANT value; VariantInit(&value);
            const bool match = SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value)) && value.vt == VT_BSTR && value.bstrVal && wanted == value.bstrVal;
            VariantClear(&value); if (match) return simple;
        }
        return nullptr;
    }
    static HRESULT invoke(IRawElementProviderSimple* simple) {
        ComPtr<IUnknown> pattern; ComPtr<IInvokeProvider> provider;
        if (!simple || FAILED(simple->GetPatternProvider(UIA_InvokePatternId, &pattern)) || !pattern || FAILED(pattern.As(&provider))) return E_FAIL;
        return provider->Invoke();
    }
    const mod::FileShelfItem* item(std::string_view name) const {
        const auto items = shelf_->state().items();
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto& value) { return value.name == name; });
        return found == items.end() ? nullptr : &*found;
    }
    bool recorded(std::size_t index, mod::EventKind kind, std::string_view key, std::string_view value) const {
        if (index >= events_.size() || events_[index].kind != kind) return false;
        const auto found = events_[index].metadata.find(key);
        return found != events_[index].metadata.end() && found->second == value;
    }
};

void ShelfCoverageArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no module; one scenario per cycle
    auto& shelf = *shelf_;
    time_ = std::max(time_, step.time);
    const auto next = [&] { time_ += .001; return time_; };
    const auto cycle = root_ / ("cycle-" + std::to_string(step.cycle));
    std::error_code error;
    fs::remove_all(cycle, error);
    need(fs::create_directories(cycle), "cycle directory created");
    const auto report = cycle / "Quarterly report.txt", notes = cycle / "Synthetic notes.ehudshelf", kept = cycle / "Kept folder", recycled = cycle / "Recycled folder";
    write(report, "Synthetic shelf report\n");
    write(notes, "Synthetic notes without a registered preview handler\n");
    need(fs::create_directory(kept) && fs::create_directory(recycled), "synthetic folders created");
    events_.clear();

    // Import: one reference per original, Event Log shelfAdded per new item.
    step.check(shelf.state().items().empty(), "The coverage Shelf starts every cycle empty");
    const std::vector<std::string> paths{utf8(report), utf8(notes), utf8(kept), utf8(recycled)};
    step.check(shelf.importFiles(paths, next()) && shelf.state().items().size() == 4, "Four synthetic references are added");
    step.check(events_.size() == 4 && recorded(0, mod::EventKind::shelfAdded, "filename", "Quarterly report.txt") &&
               recorded(1, mod::EventKind::shelfAdded, "filename", "Synthetic notes.ehudshelf") && recorded(2, mod::EventKind::shelfAdded, "filename", "Kept folder") &&
               recorded(3, mod::EventKind::shelfAdded, "filename", "Recycled folder"), "Every new reference records shelfAdded with its filename");
    step.check(shelf.importFiles(paths, next()) && shelf.state().items().size() == 4 && events_.size() == 4, "A duplicate-only import adds and records nothing");
    step.check(item("Quarterly report.txt") && !item("Quarterly report.txt")->typeDescription.empty(), "Files carry a type description");

    // Stale paths: renamed and moved originals are found again by identity.
    const auto renamed = cycle / "Renamed report.txt", moved = cycle / "Moved" / "Synthetic notes.ehudshelf", bin = cycle / "$Recycle.Bin";
    need(fs::create_directory(cycle / "Moved") && fs::create_directory(bin), "synthetic destinations created");
    fs::rename(report, renamed);
    fs::rename(notes, moved);
    fs::rename(recycled, bin / "Recycled folder");
    shelf.refresh(next());
    const auto* first = item("Renamed report.txt");
    step.check(first && !first->availabilityError && first->lastKnownPath == utf8(renamed), "A renamed original is found again by identity under its new name");
    const auto* second = item("Synthetic notes.ehudshelf");
    step.check(second && !second->availabilityError && second->lastKnownPath == utf8(moved), "A moved original is found again by identity in its new folder");
    step.check(item("Recycled folder") && item("Recycled folder")->availabilityError, "An original moved into a Recycle Bin folder stays unavailable");
    step.check(item("Kept folder") && !item("Kept folder")->availabilityError, "An untouched folder stays available");
    {
        native::NativeFileShelfFiles files;
        data::FileShelfStore saved(root_ / "shelf-data", files.platform());
        const auto& rows = saved.items();
        const auto has = [&](const fs::path& path) { return std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row.windowsPath == utf8(path); }); };
        step.check(rows.size() == 4 && has(renamed) && has(moved), "The renewed paths are saved like renewed Mac bookmarks");
    }
    fs::remove(renamed);
    shelf.refresh(next());
    step.check(item("Renamed report.txt") && item("Renamed report.txt")->availabilityError, "A deleted original becomes unavailable");

    // A Mac-imported Shelf item (the import area's Migration/relink.json) is an
    // unavailable card until a file on this PC is chosen for it.
    const auto recordID = data::makeUUID();
    mig::MacRelinkItem imported;
    imported.id = data::makeUUID(); imported.store = std::string(mig::relinkShelfItem); imported.recordID = recordID;
    imported.displayName = "Imported plan.key"; imported.lastKnownPath = "/Users/doctor/Desktop/Imported plan.key"; imported.kind = "file";
    imported.macReference = data::Json::Object{{"id", recordID}, {"name", "Imported plan.key"}, {"typeDescription", "Keynote Presentation"},
        {"lastKnownPath", imported.lastKnownPath}, {"byteCount", 4096}, {"isDirectory", false}, {"createdAt", 700000000.0 + step.cycle}, {"bookmark", "Ym9va21hcms="}, {"isSecurityScoped", true}};
    {
        const auto data = root_ / "shelf-data";
        std::vector<mig::MacRelinkItem> rows;
        try { rows = mig::MacRelinkLedger(data).items(); } catch (const std::exception&) {}
        rows.push_back(imported);
        fs::create_directories(data / "Migration");
        write(data / "Migration" / "relink.json", mig::MacRelinkLedger::encode(rows));
    }
    shelf.refresh(next());
    step.drain(); pump(); // the ledger is read (and reconciled) on the utility worker
    step.check(shelf.pendingRelinks() == 1 && item("Imported plan.key") && item("Imported plan.key")->availabilityError && item("Imported plan.key")->id == recordID,
               "The imported item is listed as an unavailable card with its Mac identifier");

    // UI Automation of a presented Shelf (a fresh instance on its own NEW data
    // root, with the same references and imported item): one peer per
    // accessible action plus the imported card's Relink peer; Invoke performs
    // the action through ShelfPreview::perform.
    {
        const auto uiaRoot = root_ / ("automation-" + std::to_string(step.cycle));
        fs::remove_all(uiaRoot, error);
        tools::ShelfPreviewOptions options{uiaRoot, false, false, {}};
        tools::ShelfPreview presented(window_, raster_, assets_, std::move(options));
        presented.setUtility(utility_);
        presented.resize(metrics_); presented.setLanguage(language_);
        fs::create_directories(uiaRoot / "Migration");
        write(uiaRoot / "Migration" / "relink.json", mig::MacRelinkLedger::encode({imported}));
        step.check(presented.importFiles(std::vector<std::string>{utf8(moved), utf8(kept)}, next()), "The presented Shelf lists two references");
        presented.refresh(next()); // reads the import ledger written after construction
        step.drain();
        step.check(presented.pendingRelinks() == 1 && presented.state().items().size() == 3, "The presented Shelf lists the imported item after its references");
        present(presented, true, next());
        presented.automationRefresh(nullptr);
        const auto actions = presented.state().accessibleActions();
        step.check(presented.automationCount() == actions.size() + 1 && actions.size() == 2 + 4 + 4 + 2, "Every accessible action and the Relink action have a UI Automation peer");
        const auto keptID = presented.state().items()[1].id;
        const auto plate = peer(presented, "shelf:" + keptID + ":select");
        step.check(plate && invoke(plate.Get()) == S_OK && presented.state().selectedIDs().size() == 1 && presented.state().selectedID() == std::optional<std::string>(keptID),
                   "Invoking a card's peer selects that card");
        const auto relinkPeer = peer(presented, "shelf:" + recordID + ":relink");
        step.check(relinkPeer && invoke(relinkPeer.Get()) == S_OK, "The imported card's Relink peer is invokable");
        const auto request = presented.takeAction();
        step.check(request && request->kind == tools::ShelfPreviewAction::Kind::choose && presented.state().selectedID() == std::optional<std::string>(recordID),
                   "Relink selects the imported card and asks for the file picker");
        // The properties card (the eye button) is modal for UI Automation:
        // the card itself and its close, open and reveal plates only.
        const auto eye = peer(presented, "shelf:" + keptID + ":preview");
        step.check(eye && invoke(eye.Get()) == S_OK && presented.panels().properties && presented.panels().propertiesItem == keptID, "The eye button opens the properties card");
        present(presented, true, next());
        presented.automationRefresh(nullptr);
        const auto cardPeer = peer(presented, "shelf.properties:card");
        step.check(presented.automationCount() == 4 && cardPeer && peer(presented, "shelf.properties:close") && peer(presented, "shelf.properties:reveal"), "The open card lists only its own peers");
        const auto openPeer = peer(presented, "shelf.properties:open");
        step.check(openPeer && invoke(openPeer.Get()) == S_OK && !presented.panels().properties, "Open closes the card");
        const auto open = presented.takeAction();
        step.check(open && open->kind == tools::ShelfPreviewAction::Kind::open && open->itemID == keptID, "Open is handed to the host with the item");
        step.drain();
        present(presented, false, next());
        presented.automationRefresh(nullptr);
        step.check(presented.automationCount() == 0, "A withdrawn Shelf lists no peers");
        presented.automationDisconnect();
        presented.cancelPanels();
    }
    { MSG message{}; while (PeekMessageW(&message, window_, firstShelfMessage, lastShelfMessage, PM_REMOVE)) DispatchMessageW(&message); }

    // The picker's choice (synthetic): the reference keeps the Mac identifier.
    const auto replacement = cycle / "Plan on this PC.key";
    write(replacement, "Synthetic replacement\n");
    events_.clear();
    step.check(shelf.relink(recordID, std::vector<std::string>{utf8(replacement)}, next()) && shelf.pendingRelinks() == 0, "The chosen file relinks the imported item");
    step.check(item("Plan on this PC.key") && item("Plan on this PC.key")->id == recordID && !item("Plan on this PC.key")->availabilityError, "The relinked reference keeps the imported identifier");
    step.check(events_.size() == 1 && recorded(0, mod::EventKind::shelfAdded, "filename", "Plan on this PC.key"), "Relinking records shelfAdded");
    step.drain(); pump(); // resolveMacRelink runs on the utility worker after the record write
    {
        const mig::MacRelinkLedger ledger(root_ / "shelf-data");
        const auto* entry = ledger.find(imported.id);
        step.check(entry && entry->state == mig::RelinkState::resolved && entry->windowsPath == utf8(replacement), "The import ledger entry is resolved with the chosen path");
    }

    // The properties card (replaces Quick Look) for a folder and a file: it
    // opens at once and its properties are read on the shared utility worker.
    for (const auto* name : {"Kept folder", "Synthetic notes.ehudshelf"}) {
        const auto* target = item(name);
        step.check(target && !target->availabilityError, "The properties target is available");
        const auto reads = shelf.panels().propertyReads;
        shelf.requestPreview(target->id);
        auto panels = shelf.panels();
        step.check(panels.properties && panels.propertiesItem == target->id && panels.propertyReads == reads + 1 && !panels.propertiesLoaded,
                   "The card opens at once; its properties are read on the utility worker, not the HUD thread");
        step.drain(); pump();
        step.check(shelf.panels().propertiesLoaded, "The utility worker delivered the properties");
        const auto content = shelf.properties();
        const auto row = [&](tools::ShelfText label) -> const mod::ShelfPropertiesRow* {
            if (!content) return nullptr;
            const auto found = std::find_if(content->rows.begin(), content->rows.end(), [&](const auto& value) { return value.label == tools::shelfText(label, language_); });
            return found == content->rows.end() ? nullptr : &*found;
        };
        const bool folder = std::string_view(name) == "Kept folder";
        step.check(content && content->heading == tools::shelfText(tools::ShelfText::properties, language_) && row(tools::ShelfText::propertyName) && row(tools::ShelfText::propertyName)->value == name,
                   "The card names the item in the HUD language");
        step.check(row(tools::ShelfText::propertyLocation) && !row(tools::ShelfText::propertyLocation)->value.empty() && row(tools::ShelfText::propertyCreated) && row(tools::ShelfText::propertyModified) &&
                   row(tools::ShelfText::propertyModified)->value != "…" && row(tools::ShelfText::propertyModified)->value != "—", "Location and the read times are shown");
        step.check(folder ? row(tools::ShelfText::propertyContains) && !row(tools::ShelfText::propertySize) : row(tools::ShelfText::propertySize) && !row(tools::ShelfText::propertyContains),
                   "A folder counts its items; a file shows its size");
        step.check(!shelf.state().error(), "Reading the properties reports no Shelf error");
        shelf.requestPreview(target->id);
        step.check(!shelf.panels().properties && !shelf.properties(), "The same item again closes the card");
    }

    // Remove and clear: references only, with their Event Log records.
    events_.clear();
    const auto deleted = item("Renamed report.txt")->id;
    step.check(shelf.perform("shelf:" + deleted + ":remove", next()) && !item("Renamed report.txt") && events_.size() == 1 &&
               recorded(0, mod::EventKind::shelfRemoved, "filename", "Renamed report.txt"), "Removing a reference records shelfRemoved with its filename");
    const auto count = shelf.state().items().size();
    step.check(shelf.perform("shelf:clear", next()) && shelf.state().confirmingClear(), "Clear asks for confirmation first");
    step.check(shelf.perform("shelf:confirmClear", next()) && shelf.state().items().empty(), "Confirming clears every reference");
    step.check(events_.size() == 2 && recorded(1, mod::EventKind::shelfCleared, "count", std::to_string(count)), "Clearing records shelfCleared with the cleared count");
    step.check(fs::exists(moved) && fs::exists(kept) && fs::exists(bin / "Recycled folder"), "Removing and clearing references leave every original in place");

    // Windows names for the platform actions, in the HUD language.
    step.check(shelf.state().strings().revealPrefix == tools::shelfText(tools::ShelfText::revealPrefix, language_) &&
               shelf.state().strings().previewPrefix == tools::shelfText(tools::ShelfText::previewPrefix, language_),
               "Card actions name File Explorer and Properties in the HUD language");
    fs::remove_all(cycle, error);
}
} // namespace

std::unique_ptr<app::ModuleOwner> shelfCoverageModule(const app::ApplicationModuleContext& context) {
    // The built-in "fileShelf" owner is the production path; this area runs
    // only in hidden coverage with the staged package (common.shelf artwork).
    if (context.mode != app::ApplicationAreaMode::hiddenCoverage || context.coverageRoot.empty() || !context.host) return nullptr;
    const auto assets = context.resource ? context.resource("common.shelf") : fs::path{};
    if (assets.empty()) return nullptr;
    return std::make_unique<ShelfCoverageArea>(context, assets);
}
} // namespace endfield::shelf
#else
namespace endfield::shelf {
std::unique_ptr<app::ModuleOwner> shelfCoverageModule(const app::ApplicationModuleContext&) { return nullptr; }
}
#endif
