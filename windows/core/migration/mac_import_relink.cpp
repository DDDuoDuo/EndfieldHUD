#include "core/migration/mac_import_relink.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace ehud::migration {
namespace {
using data::Json;
using data::StoreError;
using data::StoreErrorCode;
void need(bool condition, const char* message) { if (!condition) throw StoreError(StoreErrorCode::invalid, message); }
constexpr std::array<std::string_view, 5> stores{relinkNotesMedia, relinkArchiveMedia, relinkReaderBook, relinkAppShortcut, relinkShelfItem};
bool clean(const std::string& text, std::size_t maximum) { return text.size() <= maximum && Json::validUtf8(text) && text.find('\0') == std::string::npos; }
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
    need(std::find(stores.begin(), stores.end(), item.store) != stores.end(), "Unknown relink store");
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
        need(data::validWindowsFilePath(*item.windowsPath), "Invalid relinked Windows path");
    }
    need((item.state == RelinkState::resolved) == item.windowsPath.has_value(), "Relink state and Windows path disagree");
    return item;
}
MacRelinkLedger::MacRelinkLedger(std::filesystem::path root) {
    data::detail::validateRoot(root);
    path_ = std::move(root) / "Migration" / "relink.json";
    persisted_ = data::detail::readFile(path_, maximumBytes);
    if (!persisted_) return;
    Json document;
    try { document = Json::parse(*persisted_, maximumBytes); } catch (const std::exception&) { throw StoreError(StoreErrorCode::invalid, "Relink list is not JSON; the original was preserved"); }
    need(document.isObject() && document["version"].isNumber(), "Relink list has no version");
    if (document["version"].integer() > 1) throw StoreError(StoreErrorCode::newerVersion, "Relink list needs a newer app version");
    need(document["version"].integer() == 1 && document["items"].isArray() && document["items"].array().size() <= maximumItems, "Invalid relink list");
    std::set<std::string> ids;
    for (const auto& row : document["items"].array()) {
        auto item = decodeRelinkItem(row);
        need(ids.insert(item.id).second, "Duplicate relink entry");
        items_.push_back(std::move(item));
    }
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
    need(data::validWindowsFilePath(windowsPath), "A relinked file needs an explicit Windows path");
    auto next = items_;
    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == id; });
    need(found != next.end(), "Unknown relink entry");
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
            need(data::validWindowsFilePath(current.windowsPath), "A relinked record needs an explicit Windows path");
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
