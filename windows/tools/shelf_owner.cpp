#include "tools/shelf_owner.hpp"
#ifdef _WIN32
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "native/file_shelf_files.hpp"
#include "native/layer_image_source.hpp"
#include "native/shelf_icon_provider.hpp"
#include "native/shelf_properties.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <ole2.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace endfield::tools {
namespace {
namespace gpu = native;
namespace mod = modules;
namespace data = ehud::data;
namespace mig = ehud::migration;
namespace d = modules::design;
using Failure = mod::FileShelfServices::Failure;
void need(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
constexpr UINT iconMessage = WM_APP + 187, pickerMessage = WM_APP + 188;
// noticeMessage lParam of a utility completion or UI Automation action (drop
// notices carry 0): the host presents a frame and refreshes UIA after it.
constexpr LPARAM contentNotice = 1;
std::atomic<UINT_PTR> nextGeneration{1};
bool sameID(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return (x >= 'a' && x <= 'z' ? char(x - 32) : x) == (y >= 'a' && y <= 'z' ? char(y - 32) : y); });
}
std::string lowered(std::string_view value) { std::string out(value); for (auto& c : out) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return out; }
bool contains(std::string_view text, std::string_view part) { return lowered(text).find(lowered(part)) != std::string::npos; }
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(std::size_t(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), out.data(), count, nullptr, nullptr) != count) return {};
    return out;
}
// HUDFormat.dotted's calendar fields: a FILETIME in the local time zone rules of that date.
std::optional<mod::ShelfLocalTime> localTime(std::optional<std::int64_t> ticks) {
    if (!ticks || *ticks <= 0) return std::nullopt;
    const auto value = static_cast<std::uint64_t>(*ticks);
    const FILETIME utc{DWORD(value & 0xFFFFFFFFu), DWORD(value >> 32)};
    SYSTEMTIME universal{}, local{};
    if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local)) return std::nullopt;
    return mod::ShelfLocalTime{local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute};
}
std::string profileFolder() {
    wchar_t* path{};
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DONT_VERIFY, nullptr, &path)) && path) out = narrow(path);
    CoTaskMemFree(path);
    return out;
}
// Logical client points per physical pixel of the HUD window.
double windowScale(HWND hwnd) { const auto dpi = GetDpiForWindow(hwnd); return dpi ? double(dpi) / 96 : 1; }
}

std::string shelfErrorText(const std::exception& error, Failure failure, core::Language language) {
    using C = data::StoreErrorCode; const std::string_view message = error.what();
    const auto persistence = [&] { return core::localized("The file shelf could not be saved or opened: ", "无法保存或打开暂存架：", language) + std::string(message); };
    const auto unavailable = [&] { return core::localized("This item is unavailable: ", "此项目不可用：", language) + mod::shelfText(mod::ShelfText::unavailableReason, language); };
    if (contains(message, "no longer on the shelf")) return core::localized("This item is no longer on the shelf.", "此项目已不在暂存架中。", language);
    if (contains(message, "different item occupies")) return core::localized("The original item is unavailable. A different item now occupies its old location.", "原始项目不可用，其他项目已占用了原来的位置。", language);
    const auto* store = dynamic_cast<const data::StoreError*>(&error);
    if (!store) return persistence();
    switch (store->code()) {
    case C::changedOnDisk: return mod::shelfText(mod::ShelfText::changedOnDisk, language);
    case C::newerVersion: return mod::shelfText(mod::ShelfText::newerVersion, language);
    case C::tooLarge:
        if (contains(message, "archive byte bound")) return mod::shelfText(mod::ShelfText::tooManyReferences, language);
        return failure == Failure::importing ? mod::shelfText(mod::ShelfText::chooseAvailable, language) : unavailable();
    case C::unavailable: case C::invalid:
        if (failure == Failure::importing) return mod::shelfText(mod::ShelfText::chooseAvailable, language);
        if (failure == Failure::action || failure == Failure::refreshing) return unavailable();
        return persistence();
    }
    return persistence();
}
std::optional<std::string> shelfKnownMessage(std::string_view message, core::Language language) {
    // Mac importPasteboard ignores a clipboard without file URLs silently.
    if (message == "Drop source did not provide filesystem paths") return std::string{};
    if (message == "Shelf references could not be committed" || message == "Shelf drop failed") return shelfErrorText(std::runtime_error(std::string(message)), Failure::changing, language);
    if (contains(message, "no longer on the shelf") || contains(message, "different item occupies")) return shelfErrorText(std::runtime_error(std::string(message)), Failure::action, language);
    for (const auto prefix : {"Cannot acquire shelf file metadata", "Shelf reference ", "This shelf provider does not support", "Filesystem does not provide", "Original shelf item", "Outgoing shelf reference changed"})
        if (message.starts_with(prefix)) return shelfErrorText(data::StoreError(data::StoreErrorCode::unavailable, std::string(message)), Failure::action, language);
    return std::nullopt;
}

struct ShelfService::Impl {
    struct DropState {
        HWND hwnd{}; bool enabled{}, noticeExternal{};
        // The Shelf's own hit test (box px inside the canvas's hit bounds),
        // set while the owner lives.
        std::function<bool(core::Point logical)> inside;
        std::shared_ptr<const gpu::ShelfDropTarget::Callbacks> external;
        // Commit can reenter a route transition. Its deferred notice still
        // belongs to the callback snapshot that completed the transaction.
        std::optional<bool> committedExternal;
        // References a Shelf-route drop added (shelfAdded records, the reveal).
        std::vector<std::string> addedNames, addedIDs;
    };
    HWND hwnd; ShelfServiceOptions options;
    // Bookmark-equivalent resolution and Shell type names (FileShelfStore.swift).
    gpu::NativeFileShelfFiles files{{"文件", "文件夹"}, {true, true}};
    std::shared_ptr<data::FileShelfStore> store;
    std::optional<std::pair<data::StoreErrorCode, std::string>> storeError; // the saved shelf did not open
    // Mac-imported items awaiting relink (Migration/relink.json, unresolved
    // fileShelf.item rows without a Windows record) and the one whose picker
    // is open; the slot gives the relinked record the imported identifier and
    // date. The ledger is read and written on the utility worker; rows whose
    // dismissal is still queued stay hidden; only the latest read is applied.
    mig::ShelfRelinkSlot relinkSlot; std::vector<mig::MacRelinkItem> pendingRelinks; std::optional<mig::MacRelinkItem> relinkTarget;
    std::set<std::string, std::less<>> dismissing; std::uint64_t relinkReads{};
    // The Shell icon worker (drag images only: the tiles draw the doc glyph)
    // starts with the first shown Shelf, never at launch, and is parked while
    // the Shelf is switched away or the HUD closes with every wanted icon
    // extracted; a missing icon starts it again. The 24-icon cache stays.
    gpu::LayerImageSource memoryImages;
    std::unique_ptr<gpu::NativeShelfIconProvider> icons; bool iconsFailed{}, iconsParked{}, iconsDirty{};
    std::vector<gpu::ShelfIconRequest> requests; std::map<std::string, gpu::ShelfIconRequest, std::less<>> requestVersions; std::uint64_t iconRevision{};
    std::shared_ptr<DropState> dropState = std::make_shared<DropState>(); std::unique_ptr<gpu::ShelfDropTarget> drop;
    bool externalDrag{};
    std::unique_ptr<gpu::NativeShelfFilePicker> picker;
    std::function<void(mod::EventKind, mod::EventMetadata)> recordEvent;
    // The shared utility executor (setUtility), else a private one-worker
    // executor notified through workMessage. Completions check `alive`.
    app::UtilityExecutor* utility{}; std::unique_ptr<app::UtilityExecutor> ownUtility; app::UtilityExecutor::Route sharedRoute{}, ownRoute{};
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool contentPosted{}, released{};
    std::optional<ShelfServiceAction> action;
    std::vector<std::string> dragIDs; bool nativeDrag{};
    std::optional<std::pair<std::string, data::ShelfFileAccess>> heldReveal;
    std::uint64_t propertyReads{}; std::size_t pendingReads{};
    UINT_PTR generation{nextGeneration++};
    std::string home;
    // The owner (makeOwner): the canvas, its core and the shared Context. The
    // canvas holds `life` in its services: once the owner is destroyed the
    // weak reference expires and the service stops calling it.
    mod::FileShelfCanvas* canvasPointer{}; DesignOwnerCore* ownerCore{}; d::Context* context{}; std::weak_ptr<int> life;
    std::function<double()> clock;
    double time{};

    Impl(HWND h, ShelfServiceOptions o) : hwnd(h), options(std::move(o)) {
        if (options.persistentDataRoot) {
            need(options.newDataRoot.is_absolute(), "Shelf requires an absolute data root");
            data::detail::validateRoot(options.newDataRoot); std::error_code e; std::filesystem::create_directories(options.newDataRoot, e); need(!e, "Create Shelf data root");
        } else {
            need(options.newDataRoot.is_absolute() && !std::filesystem::exists(options.newDataRoot), "Shelf fixture requires a NEW absolute data root");
            data::detail::validateRoot(options.newDataRoot); need(std::filesystem::create_directory(options.newDataRoot), "Create isolated Shelf fixture data");
        }
        // A saved shelf that cannot open is the canvas's notice
        // (FileShelfCanvas(store: nil, error:)), not a missing module.
        try { store = std::make_shared<data::FileShelfStore>(options.newDataRoot, files.platform(), relinkSlot.creation()); }
        catch (const data::StoreError& e) { storeError.emplace(e.code(), e.what()); }
        home = options.homeFolder ? *options.homeFolder : profileFolder();
        recordEvent = std::move(options.recordEvent);
        picker = std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{hwnd, pickerMessage, generation}, gpu::ShelfPickerLabels{"添加到文件暂存架", "添加引用", "添加引用"});
        dropState->hwnd = hwnd;
        if (options.registerDropTarget) {
            auto shared = dropState; auto retainedStore = store;
            drop = std::make_unique<gpu::ShelfDropTarget>(gpu::ShelfTransferRoute{hwnd, noticeMessage, generation}, gpu::ShelfDropTarget::Callbacks{
                [shared](POINTL p) {
                    const auto route = shared->external; shared->noticeExternal = bool(route);
                    if (route) return route->acceptsPoint(p);
                    if (!shared->enabled || !shared->inside) return false;
                    POINT point{p.x, p.y}; if (!ScreenToClient(shared->hwnd, &point)) return false;
                    const double scale = windowScale(shared->hwnd);
                    return shared->inside({double(point.x) / scale, double(point.y) / scale});
                },
                [shared, retainedStore](auto paths) {
                    const auto route = shared->external; shared->noticeExternal = bool(route);
                    if (route) { const bool committed = route->commit(paths); if (committed) shared->committedExternal = true; return committed; }
                    if (!retainedStore) return false;
                    std::set<std::string, std::less<>> before; for (const auto& item : retainedStore->items()) before.insert(item.id);
                    retainedStore->add(paths); shared->committedExternal = false;
                    for (const auto& item : retainedStore->items()) if (!before.contains(item.id)) { shared->addedNames.push_back(item.name); shared->addedIDs.push_back(item.id); }
                    return true;
                },
                // Drag feedback belongs to an external route (the HUD's area owners); the Shelf's own drops read no payload while hovering.
                [shared](std::span<const std::string> paths, std::optional<POINTL> p) { const auto route = shared->external; return !route || !route->hover || route->hover(paths, p); },
                [shared] { const auto route = shared->external; return route && route->hover; }});
            need(SUCCEEDED(drop->registerTarget()), "Register Windows Shelf file drop target"); drop->setEnabled(false);
            // Incoming drags keep their Shell drag image across the HUD.
            drop->setDragImages(options.nativeIcons);
        }
        requests.reserve(gpu::NativeShelfIconProvider::maximumVisible);
    }
    ~Impl() {
        *alive = false;
        if (utility && sharedRoute) utility->invalidate(sharedRoute);
        ownUtility.reset();
        dropState->enabled = false; dropState->inside = nullptr; dropState->external.reset();
        if (drop) drop->stop();
        if (icons) icons->stop();
    }
    core::Language language() const { return context ? context->env().language : core::Language::english; }
    mod::FileShelfCanvas* canvas() const { return life.expired() ? nullptr : canvasPointer; }
    // Canvas code reads the context clock (motion, toasts): service events
    // carry their own time.
    mod::FileShelfCanvas* at(double t) {
        need(std::isfinite(t), "Shelf service requires finite time");
        time = std::max(time, t);
        if (context) context->env().now = std::max(context->env().now, time);
        return canvas();
    }
    void changed() { if (!contentPosted) contentPosted = PostMessageW(hwnd, noticeMessage, generation, contentNotice) != FALSE; }
    void emit(mod::EventKind kind, mod::EventMetadata metadata) const { if (recordEvent) recordEvent(kind, std::move(metadata)); }
    bool request(ShelfServiceAction value) {
        if (action || nativeDrag) return false;
        action = std::move(value);
        need(PostMessageW(hwnd, actionMessage, generation, 0) != FALSE, "Queue Shelf user action");
        return true;
    }
    bool picking() const { const auto stats = picker->stats(); return stats.queued || stats.presenting; }

    // ---- Utility work (properties reads, import-ledger writes) ----
    // Work receives value-owned inputs only. Each completion runs on the HUD
    // thread (the shared executor's drain or this service's workMessage) and
    // then asks the host for a frame.
    bool submit(app::UtilityExecutor::Work work, std::function<void(std::exception_ptr)> done) {
        if (released) return false;
        app::UtilityExecutor* executor = utility; app::UtilityExecutor::Route* route = &sharedRoute;
        if (!executor) {
            if (!ownUtility) { const auto window = hwnd; const auto token = generation; ownUtility = std::make_unique<app::UtilityExecutor>([window, token] { PostMessageW(window, workMessage, token, 0); }, 8); ownRoute = ownUtility->makeRoute(); }
            executor = ownUtility.get(); route = &ownRoute;
        }
        if (!*route) *route = executor->makeRoute();
        const bool shared = executor == utility;
        return executor->submit(*route, std::move(work), [this, shared, alive = alive, done = std::move(done)](std::exception_ptr error) {
            if (!*alive) return;
            try { done(error); }
            catch (const std::exception& failure) { try { if (auto* c = canvas()) c->showError(shelfErrorText(failure, Failure::changing, language())); } catch (...) {} }
            catch (...) {}
            if (shared) changed();
        });
    }

    // ---- References and Mac-imported items ----
    const data::ShelfRecord* record(std::string_view id) const {
        if (!store) return nullptr;
        const auto& rows = store->items();
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& i) { return sameID(i.id, id); });
        return found == rows.end() ? nullptr : &*found;
    }
    const mig::MacRelinkItem* pending(std::string_view id) const { for (const auto& item : pendingRelinks) if (sameID(item.recordID, id)) return &item; return nullptr; }
    bool onShelf(std::string_view id) const { return record(id) != nullptr; }
    // The card of an imported item: the source record's metadata, unavailable.
    mod::FileShelfItem relinkItem(const mig::MacRelinkItem& item) const {
        const auto& reference = item.macReference; mod::FileShelfItem value; value.id = item.recordID;
        value.name = reference["name"].isString() && !reference["name"].string().empty() ? reference["name"].string() : !item.displayName.empty() ? item.displayName : std::string("Item");
        value.lastKnownPath = item.lastKnownPath; value.typeDescription = reference["typeDescription"].isString() ? reference["typeDescription"].string() : std::string{};
        if (reference["byteCount"].isNumber() && reference["byteCount"].number() >= 0) value.byteCount = reference["byteCount"].integer();
        value.isDirectory = reference["isDirectory"].isBool() ? reference["isDirectory"].boolean() : item.kind == "folder";
        value.availabilityError = mod::shelfText(mod::ShelfText::relinkNeeded, language()); value.awaitingRelink = true;
        return value;
    }
    std::vector<mod::FileShelfItem> items() const {
        std::vector<mod::FileShelfItem> out;
        if (store) for (const auto& i : store->items()) out.push_back({i.id, i.name, i.windowsPath, i.typeDescription, i.byteCount, i.isDirectory, i.availabilityError, false});
        for (const auto& item : pendingRelinks) if (!onShelf(item.recordID)) out.push_back(relinkItem(item));
        return out;
    }
    // Reads the ledger (activation and refresh) through the import ledger API
    // on the utility worker: a row whose record already exists here (the app
    // stopped between the record and the ledger write) is resolved with that
    // record's path first (reconcileMacRelinks); no file is ever looked up by
    // path or name. Only the latest read is applied.
    void readRelinks() {
        if (!store) return;
        std::vector<std::pair<std::string, std::string>> rows; rows.reserve(store->items().size());
        for (const auto& row : store->items()) rows.emplace_back(row.id, row.windowsPath);
        auto result = std::make_shared<std::vector<mig::MacRelinkItem>>(); const auto read = ++relinkReads;
        (void)submit([root = options.newDataRoot, rows = std::move(rows), result] {
                (void)mig::reconcileMacRelinks(root, mig::relinkShelfItem, [&](const mig::MacRelinkItem& item) {
                    const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& value) { return sameID(value.first, item.recordID); });
                    return row == rows.end() ? mig::RelinkRecordCheck{} : mig::RelinkRecordCheck{mig::RelinkRecordState::relinked, row->second}; });
                *result = mig::MacRelinkLedger(root).unresolved(mig::relinkShelfItem); },
            [this, read, result](std::exception_ptr error) {
                if (read != relinkReads) return; // a newer read follows
                std::vector<mig::MacRelinkItem> open; if (!error) open = std::move(*result); // an unreadable ledger lists nothing; the import card keeps it
                std::erase_if(open, [&](const auto& item) { return onShelf(item.recordID) || dismissing.contains(item.recordID) || !item.macReference["createdAt"].isNumber(); });
                if (open == pendingRelinks) return;
                pendingRelinks = std::move(open);
                if (relinkTarget && !pending(relinkTarget->recordID)) relinkTarget.reset();
                if (auto* c = at(time)) c->storeChanged();
            });
        // a full queue keeps the current list; the next activation reads again
    }
    // Dismisses the rows of records the user removed (dismissMacRelink by
    // store and record id); they stay hidden until the write finished.
    void dismissRelinks(std::vector<std::string> recordIDs) {
        if (recordIDs.empty()) return;
        for (const auto& id : recordIDs) dismissing.insert(id);
        const auto clear = [this, recordIDs] { for (const auto& id : recordIDs) dismissing.erase(id); };
        if (!submit([root = options.newDataRoot, recordIDs] { for (const auto& id : recordIDs) (void)mig::dismissMacRelink(root, {mig::relinkShelfItem, id}); },
                [clear](std::exception_ptr error) { clear(); if (error) std::rethrow_exception(error); })) {
            clear(); throw std::runtime_error("Shelf references could not be committed");
        }
    }
    void removeReference(std::string_view id) {
        if (const auto* item = pending(id)) {
            std::vector<std::string> ids{item->recordID};
            std::erase_if(pendingRelinks, [&](const auto& row) { return sameID(row.recordID, ids.front()); });
            dismissRelinks(std::move(ids)); return;
        }
        need(bool(store), "File shelf storage is unavailable");
        store->remove(id);
    }
    void clearReferences() {
        if (store) store->clear();
        std::vector<std::string> ids; for (const auto& item : pendingRelinks) ids.push_back(item.recordID);
        pendingRelinks.clear(); relinkTarget.reset(); dismissRelinks(std::move(ids));
    }
    // Opens the picker for exactly this imported item (through the host's
    // choose action, which releases pointer capture first).
    void requestRelink(std::string_view id) {
        const auto* item = pending(id); if (!item) return;
        auto target = *item; relinkTarget = std::move(target);
        if (!request({ShelfServiceAction::Kind::choose, {}})) relinkTarget.reset();
    }

    // ---- Properties (ShelfFileProperties.read on the utility worker) ----
    bool readProperties(const mod::FileShelfItem& item) {
        const auto* row = record(item.id); if (!row || row->availabilityError) return false;
        auto result = std::make_shared<std::optional<gpu::ShelfProperties>>();
        const bool queued = submit([copy = *row, resolve = files.platform().resolve, result] {
                // An unreadable reference shows "—" (the Mac's try? reads).
                try { const auto lease = resolve(copy); *result = gpu::readShelfProperties(lease); } catch (const std::exception&) {} },
            [this, id = row->id, path = row->windowsPath, result](std::exception_ptr) {
                --pendingReads;
                mod::ShelfFileProperties value; value.path = *result ? (*result)->windowsPath : path;
                if (*result) {
                    value.created = localTime((*result)->created); value.modified = localTime((*result)->modified);
                    if ((*result)->directory && (*result)->folderItems) value.itemCount = std::int64_t(*(*result)->folderItems);
                }
                if (auto* c = at(time)) c->propertiesRead(id, std::move(value));
            });
        if (queued) { ++propertyReads; ++pendingReads; }
        return queued;
    }

    // ---- Shell icons (drag images) ----
    gpu::NativeShelfIconProvider* iconProvider() {
        if (icons) return icons.get();
        if (!options.nativeIcons || released || iconsFailed) return nullptr;
        // Fails only while another provider's worker still holds the
        // process-wide gate: the drag keeps the system cursor until the next
        // activation tries again.
        try { icons = std::make_unique<gpu::NativeShelfIconProvider>(memoryImages, files.platform().resolve, gpu::ShelfIconRoute{hwnd, iconMessage, generation}); }
        catch (const std::bad_alloc&) { throw; } catch (const std::exception&) { iconsFailed = true; return nullptr; }
        return icons.get();
    }
    void showIcons() {
        auto* provider = iconProvider(); if (!provider) return;
        // A worker that cannot start leaves the Shell icons out; invalid requests still fail the owner.
        iconsDirty = false;
        try { provider->setVisible(requests); iconsParked = false; } catch (const std::runtime_error&) { icons.reset(); iconsFailed = true; }
    }
    // The next shown frame asks again (updateInput).
    void parkIcons() { iconsDirty = true; if (icons && !iconsParked) { icons->park(); iconsParked = true; } }
    bool iconsMissing() { return std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return !memoryImages.acquire(r.imageKey, r.revision); }); }
    void wantIcons(const std::vector<std::string>& ids) {
        requests.clear();
        for (const auto& id : ids) {
            if (requests.size() == gpu::NativeShelfIconProvider::maximumVisible) break;
            const auto* file = record(id); if (!file || file->availabilityError) continue;
            gpu::ShelfIconRequest request{"shelf.icon." + file->id, 0, file->id, file->windowsPath, file->identity, file->isDirectory, false};
            const auto old = requestVersions.find(request.itemID);
            if (old != requestVersions.end()) { request.revision = old->second.revision; if (request != old->second) request.revision = ++iconRevision; }
            else request.revision = ++iconRevision;
            requestVersions[request.itemID] = request; requests.push_back(request);
        }
        std::erase_if(requestVersions, [&](const auto& item) { return !record(item.first); });
        iconsDirty = true;
        if (const auto* c = canvas(); c && c->isActive()) showIcons();
    }
    // HUDFileShelfInteraction.beginOutgoingDrag frames: side = min(72, max(36,
    // projected tile height x 0.6)) points (52 without a tile), each later item
    // 5 points up and right, at most five.
    void dragImage(gpu::ShelfDragTransfer& transfer, std::span<const std::string> ids, std::string_view primary) {
        if (!options.nativeIcons || ids.empty()) return;
        std::vector<std::shared_ptr<const gpu::LayerMemoryImage>> pixels; std::vector<gpu::ShelfDragIcon> frames;
        for (const auto& id : ids) {
            if (frames.size() == gpu::shelfDragImageMaximumFrames) break;
            const auto found = requestVersions.find(id); if (found == requestVersions.end()) continue;
            auto image = memoryImages.acquire(found->second.imageKey, found->second.revision);
            if (!image || image->width() < 2 || image->height() < 2) continue;
            frames.push_back({image->width(), image->height(), image->premultipliedBGRA()}); pixels.push_back(std::move(image));
        }
        if (frames.empty()) return;
        double tile = 52;
        if (const auto* c = canvas(); c && ownerCore) if (const auto rect = c->tileRect(primary)) if (const auto client = ownerCore->clientRect(*rect)) tile = client->height;
        const double scale = windowScale(hwnd);
        const auto side = unsigned(std::lround(std::clamp(tile * .6, 36., 72.) * scale)), offset = unsigned(std::lround(5 * scale));
        // Without the Shell helper the drag keeps the system cursor only.
        (void)transfer.setDragImage(gpu::composeShelfDragImage(frames, std::min(side, gpu::shelfDragImageMaximumSide), std::min(offset, side)));
    }

    // ---- Drops ----
    void drainDrop() {
        if (!drop) return;
        auto changes = drop->takeChanges(generation);
        auto* c = at(time);
        if (changes.committed) {
            const bool external = dropState->committedExternal.value_or(dropState->noticeExternal); dropState->committedExternal.reset();
            if (!external) {
                // importURLs: the new references enter from below, the grid at its bottom; nothing is selected.
                const auto names = std::exchange(dropState->addedNames, {}); const auto ids = std::exchange(dropState->addedIDs, {});
                for (const auto& name : names) emit(mod::EventKind::shelfAdded, {{"filename", name}});
                if (c) c->revealItems(ids, true);
            }
        }
        if (!dropState->noticeExternal) {
            if (changes.feedbackChanged) { externalDrag = changes.hovering; if (c) c->setDropTarget(changes.hovering); }
            if (changes.error) {
                auto text = changes.errorCode ? shelfErrorText(data::StoreError(*changes.errorCode, *changes.error), Failure::importing, language()) : shelfKnownMessage(*changes.error, language()).value_or(*changes.error);
                if (!text.empty() && c) c->showError(std::move(text));
            }
            if (changes.virtualFiles && c) c->showError(mod::shelfText(mod::ShelfText::virtualFiles, language()));
        }
    }
    // The frame hook: the Shelf takes drops while it accepts input.
    // The Shell icons of the shown Shelf's drag candidates are asked once it takes input.
    void updateInput() {
        const bool accepts = ownerCore && ownerCore->inputModule() == core::Module::fileShelf && !nativeDrag;
        dropState->enabled = accepts;
        if (drop) drop->setEnabled(bool(dropState->external) || accepts);
        if (accepts && iconsDirty && !requests.empty()) showIcons();
    }

    mod::FileShelfServices services(std::shared_ptr<int> token) {
        mod::FileShelfServices s;
        s.available = bool(store);
        s.items = [this] { return items(); };
        s.refresh = [this] { if (store) store->refresh(); readRelinks(); };
        s.add = [this](std::span<const std::string> paths) { need(bool(store), "File shelf storage is unavailable"); (void)store->add(paths); };
        s.remove = [this](std::string_view id) { removeReference(id); };
        s.clear = [this] { clearReferences(); };
        s.describe = [this](const std::exception& e, Failure failure) { return shelfErrorText(e, failure, language()); };
        // Add files is never a relink.
        s.chooseFiles = [this] { relinkTarget.reset(); (void)request({ShelfServiceAction::Kind::choose, {}}); };
        // HUDFileShelfInteraction.deactivate: the chooser and the drop
        // feedback end; drag leases stay with the drag (the host owns them).
        s.deactivate = [this, token] {
            (void)token;
            relinkTarget.reset(); picker->cancel(); externalDrag = false;
            parkIcons(); // the Shelf switched away: the icon worker's thread ends
        };
        // Open runs from the host's action message (ShellExecuteEx may pump
        // messages); FileShelfCanvas::opened toasts when it succeeded.
        s.open = [this](std::string_view id) { if (!nativeDrag) (void)request({ShelfServiceAction::Kind::open, std::string(id)}); return false; };
        // Reveal: the lease is taken now (failure: the store's reason) and
        // handed to the host, which closes the HUD before File Explorer opens.
        s.reveal = [this](std::string_view id) {
            if (nativeDrag || action || !store) return false;
            try { heldReveal.emplace(std::string(id), store->access(id)); }
            catch (const std::exception& e) { if (auto* c = canvas()) c->showError(shelfErrorText(e, Failure::action, language())); return false; }
            if (!request({ShelfServiceAction::Kind::reveal, std::string(id)})) { heldReveal.reset(); return false; }
            return true;
        };
        s.beginDrag = [this](std::vector<std::string> ids, std::string primary) { if (nativeDrag) return; dragIDs = std::move(ids); if (!request({ShelfServiceAction::Kind::drag, std::move(primary)})) dragIDs.clear(); };
        s.relink = [this](std::string_view id) { requestRelink(id); };
        s.paste = [this] { return request({ShelfServiceAction::Kind::paste, {}}); };
        s.readProperties = [this](const mod::FileShelfItem& item) { return readProperties(item); };
        s.itemsAdded = [this](const std::vector<std::string>& names) { for (const auto& name : names) emit(mod::EventKind::shelfAdded, {{"filename", name}}); };
        s.itemRemoved = [this](const std::string& name) { emit(mod::EventKind::shelfRemoved, {{"filename", name}}); };
        s.cleared = [this](std::size_t count) { emit(mod::EventKind::shelfCleared, {{"count", std::to_string(count)}}); };
        s.iconsWanted = [this](const std::vector<std::string>& ids) { wantIcons(ids); };
        s.inputLocked = [this] { return externalDrag || nativeDrag || picking(); };
        s.presentingPanel = [this] { return picking(); };
        s.homeFolder = home;
        return s;
    }
};

ShelfService::ShelfService(HWND hwnd, ShelfServiceOptions options) : impl_(std::make_unique<Impl>(hwnd, std::move(options))) {}
ShelfService::~ShelfService() = default;
app::ModuleRouting ShelfService::routing() noexcept { app::ModuleRouting r; r.pointer = 8; r.wheel = 4; r.key = 4; r.filterKey = 1; r.message = 5; return r; }
std::unique_ptr<DesignModuleOwner> ShelfService::makeOwner(native::LayerRasterizer& raster, d::Context& context, DesignOwnerOptions options) {
    auto& i = *impl_;
    need(!i.canvas(), "One File Shelf owner per service");
    i.context = &context; i.clock = options.now;
    options.message = [this](const app::NativeMessage& m, double t) { return message(m, t); };
    options.updated = [this](const app::ModuleFrame& frame) { impl_->time = std::max(impl_->time, frame.time); impl_->updateInput(); };
    // HUD closing: the picker is cancelled; the icon worker ends once every
    // wanted icon is cached (else it finishes them for the next opening).
    options.closing = [this](double) { auto& s = *impl_; s.relinkTarget.reset(); s.picker->cancel(); s.heldReveal.reset(); if (!s.iconsMissing()) s.parkIcons(); };
    auto ownerCore = std::make_unique<DesignOwnerCore>(raster, context, std::move(options));
    auto token = std::make_shared<int>(0);
    std::optional<std::string> error;
    if (i.storeError) error = shelfErrorText(data::StoreError(i.storeError->first, i.storeError->second), Failure::changing, i.language());
    auto canvas = std::make_unique<mod::FileShelfCanvas>(context, i.services(token), std::move(error));
    i.canvasPointer = canvas.get(); i.life = token;
    ownerCore->add(std::move(canvas));
    i.ownerCore = ownerCore.get();
    // The Shelf's own drops: a point inside the canvas's hit bounds (Mac
    // centreContains), unprojected like the canvas's own input.
    i.dropState->inside = [this](core::Point logical) {
        auto& s = *impl_; const auto* c = s.canvas();
        if (!c || !s.ownerCore) return false;
        const auto q = s.ownerCore->boxPoint(logical); const auto b = c->hitBounds();
        return q && q->x >= b.x && q->y >= b.y && q->x < b.x + b.width && q->y < b.y + b.height;
    };
    return std::make_unique<DesignModuleOwner>(std::move(ownerCore));
}
mod::FileShelfCanvas* ShelfService::canvas() const noexcept { return impl_->canvas(); }
DesignOwnerCore* ShelfService::core() const noexcept { return impl_->canvas() ? impl_->ownerCore : nullptr; }
void ShelfService::setUtility(app::UtilityExecutor* value) {
    auto& i = *impl_; if (value == i.utility) return;
    if (i.utility && i.sharedRoute) i.utility->invalidate(i.sharedRoute, false);
    i.utility = value; i.sharedRoute = 0;
}
bool ShelfService::message(const app::NativeMessage& m, double t) {
    auto& i = *impl_;
    if (m.wParam != i.generation) return false;
    // The native modal picker pumps nested messages; its completion runs here.
    if (m.message == pickerMessage) {
        i.picker->handleMessage(m.wParam, m.lParam);
        if (auto result = i.picker->drain(m.wParam)) {
            auto target = std::exchange(i.relinkTarget, {});
            if (SUCCEEDED(result->result) && !result->paths.empty()) { if (target) relink(target->recordID, result->paths, t); else importFiles(result->paths, t); }
            else if (!result->canceled()) showError(mod::shelfText(mod::ShelfText::pickerFailed, i.language()), t);
        }
        return true;
    }
    (void)i.at(t);
    if (m.message == noticeMessage && m.lParam == contentNotice) { i.contentPosted = false; return true; }
    if (m.message == workMessage) { if (i.ownUtility) i.ownUtility->drain(); return true; }
    if (m.message == noticeMessage && i.drop) { i.drainDrop(); return true; }
    if (m.message == iconMessage && i.icons) { (void)i.icons->drain(m.wParam); return true; }
    return false;
}
std::optional<ShelfServiceAction> ShelfService::takeAction() { return std::exchange(impl_->action, {}); }
void ShelfService::requestFiles() {
    auto& i = *impl_;
    const auto language = i.language();
    if (i.relinkTarget && !i.pending(i.relinkTarget->recordID)) i.relinkTarget.reset();
    if (i.relinkTarget) {
        // One replacement for one imported item: any single file, or (for a
        // Mac folder) a single item through the mixed file/folder dialog.
        const auto row = i.relinkItem(*i.relinkTarget); const std::array<std::string_view, 1> argument{row.name};
        const auto accept = mod::shelfText(mod::ShelfText::relinkAccept, language);
        auto title = core::renderTranslation(mod::shelfText(mod::ShelfText::relinkTitle, language), argument).value_or(accept);
        i.picker->setLabels({std::move(title), accept, accept, row.isDirectory ? gpu::ShelfPickerMode::mixedReferences : gpu::ShelfPickerMode::singleFile});
    } else {
        // HUDFileShelfInteraction.chooseFiles: "Add to Temporary File Shelf", prompt "Add references".
        const auto add = core::localized("Add references", "添加引用", language);
        i.picker->setLabels({core::localized("Add to Temporary File Shelf", "添加到文件暂存架", language), add, add});
    }
    i.picker->request();
}
bool ShelfService::paste(double t) {
    auto& i = *impl_;
    Microsoft::WRL::ComPtr<IDataObject> source; if (FAILED(OleGetClipboard(&source)) || !source) return false;
    FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (SUCCEEDED(source->QueryGetData(&files))) {
        std::vector<std::string> paths;
        try { paths = gpu::readShelfTransferPaths(*source.Get()); } catch (const std::exception& error) { source.Reset(); showError(error, t); return false; }
        source.Reset(); return importFiles(paths, t);
    }
    for (const auto* name : {L"FileGroupDescriptorW", L"FileGroupDescriptor"}) {
        FORMATETC descriptor{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        if (descriptor.cfFormat && SUCCEEDED(source->QueryGetData(&descriptor))) { source.Reset(); showError(mod::shelfText(mod::ShelfText::virtualFiles, i.language()), t); return false; }
    }
    return false;
}
// HUDFileShelfInteraction.open(id:): the identity is revalidated, the Shell
// opens the item with its default verb (S_FALSE: the user cancelled the
// Shell's "open with" choice: no toast, no notice); failure: "This item
// could not be opened."
void ShelfService::open(std::string_view id, double t) {
    auto& i = *impl_;
    auto* c = i.at(t);
    if (i.nativeDrag || !i.store) return;
    HRESULT result{E_FAIL};
    try { result = gpu::openShelfReference(i.store->access(id), {}, i.options.openOperation); }
    catch (const std::exception& e) { if (c) c->showError(shelfErrorText(e, Failure::action, i.language())); return; }
    if (!c) return;
    if (result == S_OK) c->opened(id);
    else if (FAILED(result)) c->showError(core::localized("This item could not be opened.", "无法打开此项目。", i.language()));
}
ehud::data::ShelfFileAccess ShelfService::access(std::string_view id) {
    auto& i = *impl_;
    if (i.heldReveal && sameID(i.heldReveal->first, id)) { auto lease = std::move(i.heldReveal->second); i.heldReveal.reset(); return lease; }
    need(bool(i.store), "File shelf storage is unavailable");
    return i.store->access(id);
}
std::unique_ptr<gpu::ShelfDragTransfer> ShelfService::prepareDrag(std::string_view primary) {
    auto& i = *impl_;
    need(bool(i.store), "File shelf storage is unavailable");
    auto ids = std::exchange(i.dragIDs, {});
    if (ids.empty()) if (const auto* c = i.canvas()) ids = c->dragSelection(primary);
    // All references are resolved before the drag starts (all or nothing).
    auto transfer = std::make_unique<gpu::ShelfDragTransfer>(i.store->prepareCopy(ids), i.files.platform().resolve);
    i.dragImage(*transfer, ids, primary);
    return transfer;
}
void ShelfService::setNativeDragActive(bool value) {
    auto& i = *impl_;
    i.nativeDrag = value;
    // The OLE loop owns the mouse: the press that started the drag ends
    // without a click (ShelfTile's dragging press).
    if (value && i.ownerCore && i.canvas()) i.ownerCore->cancelInteraction(i.clock ? i.clock() : i.time);
    i.updateInput();
}
void ShelfService::revealFailed() { if (auto* c = impl_->canvas()) c->deferError(mod::shelfText(mod::ShelfText::revealFailed, impl_->language())); }
bool ShelfService::importFiles(std::span<const std::string> paths, double t) {
    auto& i = *impl_;
    if (auto* c = i.at(t)) { const bool done = c->importPaths(paths); i.changed(); return done; }
    if (!i.store) return false;
    std::set<std::string, std::less<>> before; for (const auto& item : i.store->items()) before.insert(item.id);
    i.store->add(paths);
    for (const auto& item : i.store->items()) if (!before.contains(item.id)) i.emit(mod::EventKind::shelfAdded, {{"filename", item.name}});
    return true;
}
void ShelfService::showError(std::string message, double t) {
    auto& i = *impl_;
    if (auto known = shelfKnownMessage(message, i.language())) { if (known->empty()) return; message = std::move(*known); }
    if (auto* c = i.at(t)) c->showError(std::move(message));
}
void ShelfService::showError(const std::exception& error, double t) { showError(shelfErrorText(error, Failure::action, impl_->language()), t); }
void ShelfService::refresh(double t) {
    auto& i = *impl_;
    auto* c = i.at(t);
    if (i.store) try { i.store->refresh(); } catch (const std::exception& error) { if (c) c->showError(shelfErrorText(error, Failure::refreshing, i.language())); }
    i.readRelinks();
    if (c) c->storeChanged();
}
bool ShelfService::perform(std::string_view action, double t) {
    auto& i = *impl_;
    auto* c = i.at(t);
    if (!c || i.nativeDrag) return false;
    const bool done = c->perform(action);
    i.changed();
    return done;
}
bool ShelfService::relink(std::string_view id, std::span<const std::string> paths, double t) {
    auto& i = *impl_;
    auto* c = i.at(t);
    const auto show = [&](std::string text) { if (c) c->showError(std::move(text)); };
    const auto* found = i.pending(id);
    if (!found || !i.store) { show(shelfErrorText(std::runtime_error("Item is no longer on the shelf"), Failure::action, i.language())); return false; }
    if (paths.size() != 1) { show(mod::shelfText(mod::ShelfText::relinkChooseOne, i.language())); return false; }
    const auto item = *found; std::size_t added{};
    // The chosen object becomes a reference with the imported identifier and
    // date; a duplicate of an existing reference adds nothing.
    try { i.relinkSlot.arm(item); added = i.store->add(paths); i.relinkSlot.disarm(); }
    catch (const std::exception& error) { i.relinkSlot.disarm(); show(shelfErrorText(error, Failure::importing, i.language())); return false; }
    std::string chosen(paths.front()); std::replace(chosen.begin(), chosen.end(), '/', '\\');
    if (const auto* row = i.record(item.recordID)) chosen = row->windowsPath;
    // The ledger after the record, on the utility worker (resolveMacRelink by
    // store and record id); a lost write is repaired by the next reconcile.
    std::erase_if(i.pendingRelinks, [&](const auto& row) { return sameID(row.recordID, item.recordID); });
    if (i.relinkTarget && sameID(i.relinkTarget->recordID, item.recordID)) i.relinkTarget.reset();
    if (!i.submit([root = i.options.newDataRoot, recordID = item.recordID, chosen] { (void)mig::resolveMacRelink(root, {mig::relinkShelfItem, recordID}, chosen); },
            [](std::exception_ptr error) { if (error) std::rethrow_exception(error); }))
        show(shelfErrorText(std::runtime_error("Shelf references could not be committed"), Failure::changing, i.language()));
    if (added) if (const auto* row = i.record(item.recordID)) i.emit(mod::EventKind::shelfAdded, {{"filename", row->name}});
    if (c) c->storeChanged();
    i.changed();
    return true;
}
std::size_t ShelfService::pendingRelinks() const { return impl_->pendingRelinks.size(); }
std::vector<mod::FileShelfItem> ShelfService::items() const { return impl_->items(); }
bool ShelfService::preservesFocusOnLoss() const { const auto& i = *impl_; return i.nativeDrag || i.picking() || i.externalDrag; }
void ShelfService::setExternalDropCallbacks(std::optional<gpu::ShelfDropTarget::Callbacks> callbacks, double t) {
    auto& i = *impl_; auto* c = i.at(t);
    need(!i.released, "Shelf drop route is released");
    if (callbacks) need(bool(callbacks->acceptsPoint) && bool(callbacks->commit), "Incomplete external file-drop callbacks");
    if (!callbacks && !i.dropState->external) return;
    need(i.drop != nullptr, "Shelf file-drop registration is disabled");
    auto next = callbacks ? std::make_shared<const gpu::ShelfDropTarget::Callbacks>(std::move(*callbacks)) : nullptr;
    i.drop->setEnabled(false); i.drainDrop();
    i.dropState->external = std::move(next);
    // A route can change while COM prevents notice draining. Remove an old
    // Shelf hover now; later commits retain their own origin independently.
    if (i.externalDrag) { i.externalDrag = false; if (c) c->setDropTarget(false); }
    i.drop->setEnabled(bool(i.dropState->external) || i.dropState->enabled);
}
IDropTarget* ShelfService::fileDropTarget() const noexcept { return impl_->drop ? impl_->drop->dropTarget() : nullptr; }
void ShelfService::setEventRecorder(std::function<void(mod::EventKind, mod::EventMetadata)> value) { impl_->recordEvent = std::move(value); }
ShelfPanelState ShelfService::panels() const { const auto& i = *impl_; return {i.picking(), i.propertyReads, i.pendingReads}; }
ShelfResourceStats ShelfService::resources() const {
    const auto& i = *impl_; ShelfResourceStats out;
    if (i.ownerCore && i.canvas()) if (const auto scene = i.ownerCore->sceneStats(core::Module::fileShelf)) {
        out.sceneParts = scene->parts; out.sceneSurfaces = scene->surfaces; out.scenePixelBytes = scene->residentPixelBytes; out.sceneParks = scene->parks;
    }
    if (i.icons) { const auto icons = i.icons->stats(); out.iconProvider = true; out.iconWorkers = icons.workers; out.iconWorkerStarts = icons.workerStarts; out.iconParks = icons.parks; out.iconRequests = icons.requests; }
    const auto cache = i.memoryImages.stats(); out.cachedIcons = cache.cachedImages; out.cachedIconBytes = cache.cachedBytes;
    return out;
}
} // namespace endfield::tools
#endif
