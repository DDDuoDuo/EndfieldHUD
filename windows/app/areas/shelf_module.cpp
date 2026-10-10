#include "app/areas/shelf_module.hpp"
#ifdef _WIN32
#include "core/data/file_shelf_store.hpp"
#include "native/file_shelf_files.hpp"
#include "native/shelf_assets.hpp"
#include "tools/shelf_preview.hpp"
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
constexpr wchar_t windowClass[] = L"EndfieldShelfCoverageOwner.v1";
constexpr UINT firstShelfMessage = WM_APP + 185, lastShelfMessage = WM_APP + 189; // ShelfPreview notices/actions
void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("File Shelf coverage: ") + why); }
std::string utf8(const fs::path& path) { const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    need(bool(out), "synthetic file written");
}

class ShelfCoverageArea final : public app::ModuleOwner {
public:
    ShelfCoverageArea(const app::ApplicationModuleContext& c, const fs::path& assets) : root_(c.coverageRoot), assets_(assets), language_(c.host->appearance().language) {
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = windowClass;
        need(RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "private owner class registered");
        // Private, never-shown owner of this Shelf's notices and hidden preview
        // popup; the HUD HWND keeps the built-in Shelf's routes untouched.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, this);
        need(window_ && !IsWindowVisible(window_), "private owner window stays hidden");
        tools::ShelfPreviewOptions options{root_ / "shelf-data", false, false, {}};
        options.recordEvent = [this](mod::EventKind kind, mod::EventMetadata metadata) { events_.push_back({kind, std::move(metadata)}); };
        options.visiblePanels = false;
        shelf_ = std::make_unique<tools::ShelfPreview>(window_, c.rasterizer, assets_, std::move(options));
        shelf_->resize(c.metrics);
        shelf_->setLanguage(language_);
    }
    ~ShelfCoverageArea() override {
        shelf_.reset();
        if (window_) DestroyWindow(window_);
    }
    std::string_view name() const noexcept override { return "fileShelfCoverage"; }
    void resize(const app::ClientMetrics& metrics) override { shelf_->resize(metrics); }
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
    HWND window_{};
    std::vector<Record> events_;
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
    // Delivers this Shelf's queued private notices (preview work and events).
    void pump() {
        MSG message{};
        for (unsigned n = 0; n < 32 && PeekMessageW(&message, window_, firstShelfMessage, lastShelfMessage, PM_REMOVE); ++n) DispatchMessageW(&message);
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

    // Quick Look's generic preview: a folder and a type without a handler.
    for (const auto* name : {"Kept folder", "Synthetic notes.ehudshelf"}) {
        const auto* target = item(name);
        step.check(target && !target->availabilityError, "The preview target is available");
        const auto before = shelf.panels().previewFallbacks;
        shelf.requestPreview(target->id);
        pump();
        const auto panels = shelf.panels();
        step.check(panels.previewActive && panels.previewFallback && !panels.previewVisible && panels.previewFallbacks == before + 1,
                   "The generic preview opens in the hidden coverage popup");
        step.check(!shelf.state().error(), "Opening the generic preview reports no Shelf error");
        shelf.cancelPanels();
        pump();
        step.check(!shelf.panels().previewActive, "The generic preview closes");
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
               "Card actions name File Explorer and Preview in the HUD language");
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
