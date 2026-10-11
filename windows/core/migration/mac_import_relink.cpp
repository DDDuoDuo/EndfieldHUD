#include "core/migration/mac_import_relink.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <set>

namespace ehud::migration {
namespace {
using data::Json;
using data::StoreError;
using data::StoreErrorCode;
void need(bool condition, const char* message) { if (!condition) throw StoreError(StoreErrorCode::invalid, message); }
constexpr std::array<std::string_view, 5> stores{relinkNotesMedia, relinkArchiveMedia, relinkReaderBook, relinkAppShortcut, relinkShelfItem};
bool clean(const std::string& text, std::size_t maximum) { return text.size() <= maximum && Json::validUtf8(text) && text.find('\0') == std::string::npos; }
bool knownStore(std::string_view store) noexcept { return std::find(stores.begin(), stores.end(), store) != stores.end(); }
// "shell:AppsFolder\<PackageFamilyName>!<ApplicationId>": the explicit target
// of a packaged application chosen in the App shortcuts applications dialog.
bool packagedApplicationTarget(std::string_view target) noexcept {
    constexpr std::string_view prefix = "shell:AppsFolder\\";
    constexpr std::size_t maximumID = 130; // APPLICATION_USER_MODEL_ID_MAX_LENGTH
    if (!target.starts_with(prefix)) return false;
    const auto id = target.substr(prefix.size());
    const auto bang = id.find('!');
    if (id.empty() || id.size() > maximumID || bang == std::string_view::npos || bang == 0 || bang + 1 == id.size() || id.find('!', bang + 1) != std::string_view::npos)
        return false;
    for (std::size_t n = 0; n < id.size(); ++n) {
        const char c = id[n];
        const bool identity = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!identity && n != bang) return false;
    }
    return true;
}
// The same Mac reference: objects naming the same bookmark and last known
// path (the locator; other metadata may be re-encoded by the owner's store),
// or equal objects when they carry no bookmark.
bool sameMacLocator(const Json& a, const Json& b) noexcept {
    if (!a.isObject() || !b.isObject()) return false;
    if (a.contains("referencePlatform") || b.contains("referencePlatform")) return false;
    if (a["bookmark"].isString() || b["bookmark"].isString())
        return a["bookmark"] == b["bookmark"] && a["lastKnownPath"] == b["lastKnownPath"];
    return a == b;
}
// Record identifiers are UUID strings; owners and the ledger may differ in case.
bool sameID(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t n = 0; n < a.size(); ++n) {
        char x = a[n], y = b[n];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
std::string_view stateName(RelinkState state) {
    switch (state) {
    case RelinkState::unresolved: return "unresolved";
    case RelinkState::resolved: return "resolved";
    case RelinkState::dismissed: return "dismissed";
    }
    return "unresolved";
}
Json mergeUnknown(const Json& original, Json replacement) {
    static const std::set<std::string, std::less<>> locator{"version", "kind", "bookmark", "isSecurityScoped", "lastKnownPath", "displayName",
        "pixelWidth", "pixelHeight", "frameCount", "duration", "referencePlatform", "windowsPath"};
    if (original.isObject())
        for (const auto& [key, value] : original.object())
            if (!locator.contains(key) && !replacement.contains(key)) replacement[key] = value;
    return replacement;
}
}
bool validMacRelinkTarget(std::string_view store, std::string_view target) noexcept {
    if (!knownStore(store)) return false;
    if (data::validWindowsFilePath(target)) return true;
    return store == relinkAppShortcut && packagedApplicationTarget(target);
}
Json encodeRelinkItem(const MacRelinkItem& item) {
    Json::Object out{{"id", item.id}, {"store", item.store}, {"recordID", item.recordID}, {"displayName", item.displayName},
                     {"lastKnownPath", item.lastKnownPath}, {"kind", item.kind}, {"macReference", item.macReference},
                     {"state", std::string(stateName(item.state))}};
    if (item.index) out["index"] = static_cast<std::int64_t>(*item.index);
    if (item.windowsPath) out["windowsPath"] = *item.windowsPath;
    return out;
}
MacRelinkItem decodeRelinkItem(const Json& json) {
    need(json.isObject(), "Invalid relink entry");
    MacRelinkItem item;
    auto text = [&](const char* key, std::size_t maximum) {
        need(json[key].isString() && clean(json[key].string(), maximum), "Invalid relink entry text");
        return json[key].string();
    };
    item.id = text("id", 36);
    item.store = text("store", 64);
    item.recordID = text("recordID", 36);
    item.displayName = text("displayName", 4096);
    item.lastKnownPath = text("lastKnownPath", 32768);
    item.kind = text("kind", 64);
    need(data::validUUID(item.id) && data::validUUID(item.recordID), "Invalid relink identifiers");
    need(knownStore(item.store), "Unknown relink store");
    item.macReference = json["macReference"];
    need(item.macReference.isObject(), "Invalid relink reference");
    if (json.contains("index")) {
        need(json["index"].isNumber(), "Invalid relink index");
        const auto index = json["index"].integer();
        need(index >= 0 && index < 4096, "Invalid relink index");
        item.index = static_cast<std::size_t>(index);
    }
    const auto state = text("state", 16);
    item.state = state == "resolved" ? RelinkState::resolved : state == "dismissed" ? RelinkState::dismissed : RelinkState::unresolved;
    need(state == stateName(item.state), "Unknown relink state");
    if (json.contains("windowsPath")) {
        item.windowsPath = text("windowsPath", 32768);
        need(validMacRelinkTarget(item.store, *item.windowsPath), "Invalid relinked Windows path");
    }
    need((item.state == RelinkState::resolved) == item.windowsPath.has_value(), "Relink state and Windows path disagree");
    return item;
}
MacRelinkLedger::MacRelinkLedger(std::filesystem::path root) {
    data::detail::validateRoot(root);
    path_ = std::move(root) / "Migration" / "relink.json";
    persisted_ = data::detail::readFile(path_, maximumBytes);
    if (persisted_) items_ = decode(*persisted_);
}
std::vector<MacRelinkItem> MacRelinkLedger::decode(const std::string& bytes) {
    Json document;
    try { document = Json::parse(bytes, maximumBytes); } catch (const std::exception&) { throw StoreError(StoreErrorCode::invalid, "Relink list is not JSON; the original was preserved"); }
    need(document.isObject() && document["version"].isNumber(), "Relink list has no version");
    if (document["version"].integer() > 1) throw StoreError(StoreErrorCode::newerVersion, "Relink list needs a newer app version");
    need(document["version"].integer() == 1 && document["items"].isArray() && document["items"].array().size() <= maximumItems, "Invalid relink list");
    std::vector<MacRelinkItem> items;
    items.reserve(document["items"].array().size());
    std::set<std::string> ids;
    for (const auto& row : document["items"].array()) {
        auto item = decodeRelinkItem(row);
        need(ids.insert(item.id).second, "Duplicate relink entry");
        items.push_back(std::move(item));
    }
    return items;
}
std::vector<MacRelinkItem> MacRelinkLedger::unresolved(std::string_view store) const {
    std::vector<MacRelinkItem> out;
    for (const auto& item : items_) if (item.state == RelinkState::unresolved && (store.empty() || item.store == store)) out.push_back(item);
    return out;
}
const MacRelinkItem* MacRelinkLedger::find(std::string_view id) const noexcept {
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.id == id; });
    return found == items_.end() ? nullptr : &*found;
}
std::string MacRelinkLedger::encode(const std::vector<MacRelinkItem>& items) {
    Json::Array rows;
    rows.reserve(items.size());
    for (const auto& item : items) rows.push_back(encodeRelinkItem(item));
    return Json(Json::Object{{"version", 1}, {"items", std::move(rows)}}).encode(maximumBytes);
}
void MacRelinkLedger::commit(std::vector<MacRelinkItem> next) {
    const auto bytes = encode(next);
    data::detail::replaceFile(path_, persisted_, bytes, maximumBytes);
    items_ = std::move(next);
    persisted_ = bytes;
}
void MacRelinkLedger::markResolved(std::string_view id, std::string windowsPath) {
    auto next = items_;
    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == id; });
    need(found != next.end(), "Unknown relink entry");
    need(validMacRelinkTarget(found->store, windowsPath), "A relinked file needs an explicit Windows path");
    need(found->state != RelinkState::dismissed, "A dismissed reference cannot be relinked");
    found->state = RelinkState::resolved;
    found->windowsPath = std::move(windowsPath);
    commit(std::move(next));
}
void MacRelinkLedger::dismiss(std::string_view id) {
    auto next = items_;
    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == id; });
    need(found != next.end(), "Unknown relink entry");
    found->state = RelinkState::dismissed;
    found->windowsPath.reset();
    commit(std::move(next));
}
std::size_t MacRelinkLedger::reconcile(std::string_view store, const std::function<RelinkRecordCheck(const MacRelinkItem&)>& check) {
    need(static_cast<bool>(check), "A record check is required");
    auto next = items_;
    std::size_t changed{};
    for (auto& item : next) {
        if (item.store != store || item.state != RelinkState::unresolved) continue;
        const auto current = check(item);
        if (current.state == RelinkRecordState::relinked) {
            need(validMacRelinkTarget(item.store, current.windowsPath), "A relinked record needs an explicit Windows path");
            item.state = RelinkState::resolved;
            item.windowsPath = current.windowsPath;
            ++changed;
        } else if (current.state == RelinkRecordState::missing) {
            item.state = RelinkState::dismissed;
            item.windowsPath.reset();
            ++changed;
        }
    }
    if (changed) commit(std::move(next));
    return changed;
}

// --- Owner API -------------------------------------------------------------------
namespace {
// One read-modify-write at a time in this process (the shelf, the App
// shortcuts worker and the other owners share one file); replaceFile's lock
// file and expected bytes still guard against other processes.
std::mutex& ledgerWriters() { static std::mutex value; return value; }
constexpr unsigned ledgerAttempts = 4;
void validKey(const MacRelinkKey& key) {
    need(knownStore(key.store), "Unknown relink store");
    need(data::validUUID(key.recordID), "Invalid relink record identifier");
    need(!key.macReference || key.macReference->isObject(), "Invalid relink reference");
}
// `apply` maps the current rows to the next ones (counting into `change`);
// nothing is written unless a row changed. A ledger replaced by another
// writer between the read and the write is read again and `apply` repeated.
template <class Apply> MacRelinkChange updateLedger(const std::filesystem::path& root, std::string_view store, Apply&& apply) {
    data::detail::validateRoot(root);
    const auto path = root / "Migration" / "relink.json";
    const std::lock_guard lock(ledgerWriters());
    for (unsigned attempt = 1;; ++attempt) {
        const auto before = data::detail::readFile(path, MacRelinkLedger::maximumBytes);
        if (!before) return {}; // no import: nothing is read into existence
        auto rows = MacRelinkLedger::decode(*before);
        MacRelinkChange change;
        apply(rows, change);
        try {
            if (change.resolved || change.dismissed) data::detail::replaceFile(path, before, MacRelinkLedger::encode(rows), MacRelinkLedger::maximumBytes);
        } catch (const StoreError& error) {
            if (error.code() != StoreErrorCode::changedOnDisk || attempt >= ledgerAttempts) throw;
            continue;
        }
        for (const auto& item : rows) change.open += item.state == RelinkState::unresolved && item.store == store;
        return change;
    }
}
}
bool macRelinkMatches(const MacRelinkItem& item, const MacRelinkKey& key) noexcept {
    if (item.store != key.store || !sameID(item.recordID, key.recordID)) return false;
    if (!item.index) return true;                 // one row per record
    if (key.macReference) return sameMacLocator(item.macReference, *key.macReference);
    if (key.index) return *item.index == *key.index;
    return true;                                  // every attachment of the record
}
MacRelinkChange resolveMacRelink(const std::filesystem::path& root, const MacRelinkKey& key, std::string_view target) {
    validKey(key);
    need(validMacRelinkTarget(key.store, target), "A relinked file needs an explicit Windows path");
    return updateLedger(root, key.store, [&](std::vector<MacRelinkItem>& rows, MacRelinkChange& change) {
        for (auto& item : rows) {
            if (item.state != RelinkState::unresolved || !macRelinkMatches(item, key)) continue;
            item.state = RelinkState::resolved;
            item.windowsPath = std::string(target);
            ++change.resolved;
        }
    });
}
MacRelinkChange dismissMacRelink(const std::filesystem::path& root, const MacRelinkKey& key) {
    validKey(key);
    return updateLedger(root, key.store, [&](std::vector<MacRelinkItem>& rows, MacRelinkChange& change) {
        for (auto& item : rows) {
            if (item.state != RelinkState::unresolved || !macRelinkMatches(item, key)) continue;
            item.state = RelinkState::dismissed;
            item.windowsPath.reset();
            ++change.dismissed;
        }
    });
}
MacRelinkChange reconcileMacRelinks(const std::filesystem::path& root, std::string_view store, const std::function<RelinkRecordCheck(const MacRelinkItem&)>& check) {
    need(knownStore(store), "Unknown relink store");
    need(static_cast<bool>(check), "A record check is required");
    return updateLedger(root, store, [&](std::vector<MacRelinkItem>& rows, MacRelinkChange& change) {
        for (auto& item : rows) {
            if (item.store != store || item.state != RelinkState::unresolved) continue;
            const auto current = check(item);
            if (current.state == RelinkRecordState::relinked) {
                need(validMacRelinkTarget(item.store, current.windowsPath), "A relinked record needs an explicit Windows path");
                item.state = RelinkState::resolved;
                item.windowsPath = current.windowsPath;
                ++change.resolved;
            } else if (current.state == RelinkRecordState::missing) {
                item.state = RelinkState::dismissed;
                item.windowsPath.reset();
                ++change.dismissed;
            }
        }
    });
}
RelinkRecordCheck macRelinkMediaCheck(const MacRelinkItem& item, std::span<const Json> media) {
    for (const auto& object : media) if (sameMacLocator(object, item.macReference)) return {}; // still the Mac locator
    if (item.index) return {RelinkRecordState::missing, {}}; // relinked or removed; never attributed by position
    if (media.empty()) return {RelinkRecordState::missing, {}};
    if (media.size() != 1) return {};
    const auto& payload = media.front();
    if (payload.isObject() && payload["referencePlatform"] == Json("windows") && payload["windowsPath"].isString() &&
        validMacRelinkTarget(item.store, payload["windowsPath"].string()))
        return {RelinkRecordState::relinked, payload["windowsPath"].string()};
    return {}; // another Mac payload, or one this rule cannot attribute, stays listed
}

endfield::modules::ReaderBook relinkedReaderBook(const endfield::modules::ReaderBook& book, std::string windowsPath) {
    auto result = endfield::modules::windowsReaderReference(book.id, std::move(windowsPath), book.title); // validates the locator
    result.location = book.location;
    result.progress = book.progress;
    result.bookmarks = book.bookmarks;
    result.originalFields = book.originalFields;     // excludes the Mac locator keys by construction
    result.originalNumbers = book.originalNumbers;
    return result;
}
std::string relinkedNoteMedia(std::string_view macMediaJSON, const WindowsMediaDescriptor& descriptor) {
    const auto original = Json::parse(macMediaJSON, 16 * 1024 * 1024);
    const auto replacement = Json::parse(data::makeWindowsMediaReference(descriptor.windowsPath, descriptor.displayName,
        descriptor.pixelWidth, descriptor.pixelHeight, descriptor.kind, descriptor.duration, descriptor.frameCount));
    return mergeUnknown(original, replacement).encode(16 * 1024 * 1024);
}
Json relinkedMediaObject(const Json& macMedia, const WindowsMediaDescriptor& descriptor) {
    const auto replacement = Json::parse(data::makeWindowsMediaReference(descriptor.windowsPath, descriptor.displayName,
        descriptor.pixelWidth, descriptor.pixelHeight, descriptor.kind, descriptor.duration, descriptor.frameCount));
    return mergeUnknown(macMedia, replacement);
}
data::FileShelfStore::Creation shelfRelinkCreation(const MacRelinkItem& item) {
    need(item.store == relinkShelfItem && item.macReference["createdAt"].isNumber(), "Not an imported shelf item");
    const auto id = item.recordID;
    const auto created = item.macReference["createdAt"].number();
    data::FileShelfStore::Creation creation;
    creation.id = [id] { return id; };
    creation.timestamp = [created] { return created; };
    return creation;
}
}

namespace ehud::migration {
struct ShelfRelinkSlot::State {
    std::optional<std::string> id;
    std::optional<double> createdAt;
};
ShelfRelinkSlot::ShelfRelinkSlot() : state_(std::make_shared<State>()) {}
data::FileShelfStore::Creation ShelfRelinkSlot::creation() const {
    data::FileShelfStore::Creation creation;
    creation.id = [state = state_] { auto id = state->id.value_or(data::makeUUID()); state->id.reset(); return id; };
    creation.timestamp = [state = state_] { auto at = state->createdAt.value_or(data::foundationNow()); state->createdAt.reset(); return at; };
    return creation;
}
void ShelfRelinkSlot::arm(const MacRelinkItem& item) {
    const auto creation = shelfRelinkCreation(item);
    state_->id = creation.id();
    state_->createdAt = creation.timestamp();
}
void ShelfRelinkSlot::disarm() noexcept { state_->id.reset(); state_->createdAt.reset(); }
bool ShelfRelinkSlot::armed() const noexcept { return state_->id.has_value(); }
}
