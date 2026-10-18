#include "modules/app_shortcut_model.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <tuple>

namespace endfield::modules {
namespace {
using J = ShortcutJson;
void need(bool ok, ShortcutErrorCode code = ShortcutErrorCode::invalidRecord) {
    if (!ok) throw ShortcutError(code);
}
std::string uuid(std::string id) {
    need(ehud::data::validUUID(id));
    for (auto& c : id) if (c >= 'a' && c <= 'f') c = char(c - 'a' + 'A');
    return id;
}
void text(std::string_view s, std::size_t maximum = shortcutMaximumBytes) {
    need(s.size() <= maximum && J::validUtf8(s) && s.find('\0') == s.npos);
}
bool opaqueBookmark(std::string_view s) {
    if (s.empty() || s.size() > shortcutMaximumBytes || s.size() % 4) return false;
    const auto pad = s.ends_with("==") ? 2u : s.ends_with('=') ? 1u : 0u;
    return std::all_of(s.begin(), s.end() - pad, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/';
    });
}
constexpr std::array locatorKeys{"bookmark", "securityScoped", "lastKnownPath",
    "referencePlatform", "windowsTarget"};
void locator(const J& j) {
    need(j.isObject());
    if (j.contains("referencePlatform") || j.contains("windowsTarget")) {
        need(j["referencePlatform"] == J("windows") && j["windowsTarget"].isObject());
        for (const auto* k : {"bookmark", "securityScoped", "lastKnownPath"}) need(!j.contains(k));
        const auto& target = j["windowsTarget"];
        need(target["kind"] == J("executable") || target["kind"] == J("shellLink") ||
             target["kind"] == J("packagedApp"));
        text(target["applicationKey"].string(), 32768);
        need(!target["applicationKey"].string().empty());
        if (target["kind"] == J("packagedApp")) {
            text(target["appUserModelID"].string(), 32768);
            need(!target["appUserModelID"].string().empty() && !target.contains("path"));
        } else {
            need(ehud::data::validWindowsFilePath(target["path"].string()));
            need(!target.contains("appUserModelID"));
        }
    } else {
        need(j["bookmark"].isString() && opaqueBookmark(j["bookmark"].string()) &&
             j["securityScoped"].isBool());
        const auto path = j["lastKnownPath"].string();
        text(path, 32768);
        need(!path.empty() && path.front() == '/');
        auto suffix = path.substr(path.size() >= 4 ? path.size() - 4 : 0);
        for (auto& c : suffix) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        need(suffix == ".app");
    }
}
J record(const ShortcutRecord& value, const ShortcutTextRules& rules) {
    validateShortcut(value, rules);
    auto out = value.originalFields;
    for (const auto* key : locatorKeys) out.erase(key);
    for (const auto& [key, v] : value.locator.object()) out[key] = v;
    out["id"] = uuid(value.id); out["name"] = value.name;
    out["originalName"] = value.originalName; out["iconPreset"] = value.iconPreset;
    if (value.bundleIdentifier) out["bundleIdentifier"] = *value.bundleIdentifier;
    else out.erase("bundleIdentifier");
    out["createdAt"] = value.originalDate.isNumber() && value.originalDate.number() == value.createdAt
        ? value.originalDate : J(value.createdAt);
    return out;
}
}
ShortcutError::ShortcutError(ShortcutErrorCode c)
    : std::runtime_error(c == ShortcutErrorCode::invalidName ? "Enter a name of 1–128 characters without line breaks."
        : c == ShortcutErrorCode::duplicate ? "This application is already saved. Edit its existing shortcut instead."
        : c == ShortcutErrorCode::missing ? "This app shortcut is no longer available."
        : c == ShortcutErrorCode::applicationChanged ? "The application changed. Choose it again."
        : c == ShortcutErrorCode::newerVersion ? "These shortcuts were saved by a newer version."
        : c == ShortcutErrorCode::changedOnDisk ? "App shortcuts changed outside this window. Restart before editing."
        : c == ShortcutErrorCode::unavailable ? "The application is unavailable."
        : c == ShortcutErrorCode::capacity ? "App shortcut metadata exceeds its storage limit."
        : "The saved app shortcuts could not be read. The original data has been preserved."), code_(c) {}
bool validShortcutIcon(std::string_view s) noexcept {
    constexpr std::array<std::string_view, 14> icons{"original", "bolt", "star", "terminal", "globe",
        "folder", "music", "play", "brush", "code", "game", "camera", "grid", "textBubble"};
    return std::find(icons.begin(), icons.end(), s) != icons.end();
}
bool ShortcutRecord::native() const noexcept { return locator["referencePlatform"] == J("windows"); }
bool ShortcutRecord::operator==(const ShortcutRecord& v) const {
    return std::tie(id, name, originalName, iconPreset, bundleIdentifier, createdAt, locator, originalFields) ==
        std::tie(v.id, v.name, v.originalName, v.iconPreset, v.bundleIdentifier, v.createdAt, v.locator, v.originalFields);
}
void validateShortcut(const ShortcutRecord& v, const ShortcutTextRules& r) {
    need(r.validName && r.trimmed && ehud::data::validUUID(v.id) && std::isfinite(v.createdAt));
    text(v.name); need(r.validName(v.name)); text(v.originalName); need(!v.originalName.empty());
    need(validShortcutIcon(v.iconPreset) && v.originalFields.isObject());
    if (v.bundleIdentifier) text(*v.bundleIdentifier);
    locator(v.locator);
}
ShortcutFile decodeShortcuts(const J& j, const ShortcutTextRules& rules) {
    try {
        need(j.isObject() && j["version"].isNumber());
        const auto version = j["version"].integer();
        need(version <= 1, ShortcutErrorCode::newerVersion); need(version == 1 && j["items"].isArray());
        ShortcutFile out; out.originalFields = j; out.originalFields.erase("version"); out.originalFields.erase("items");
        std::set<std::string> ids;
        for (const auto& item : j["items"].array()) {
            need(item.isObject()); ShortcutRecord v;
            v.id = uuid(item["id"].string()); v.name = item["name"].string(); v.originalName = item["originalName"].string();
            v.iconPreset = item["iconPreset"].string(); v.createdAt = item["createdAt"].number(); v.originalDate = item["createdAt"];
            if (!item["bundleIdentifier"].isNull()) v.bundleIdentifier = item["bundleIdentifier"].string();
            v.originalFields = item;
            for (const auto* key : locatorKeys) {
                if (item.contains(key)) v.locator[key] = item[key];
                v.originalFields.erase(key);
            }
            for (const auto* key : {"id", "name", "originalName", "iconPreset", "createdAt", "bundleIdentifier"}) v.originalFields.erase(key);
            validateShortcut(v, rules); need(ids.insert(v.id).second); out.items.push_back(std::move(v));
        }
        return out;
    } catch (const ShortcutError&) { throw; }
    catch (...) { throw ShortcutError(ShortcutErrorCode::invalidRecord); }
}
J encodeShortcuts(const ShortcutFile& file, const ShortcutTextRules& rules) {
    need(file.originalFields.isObject()); auto out = file.originalFields; out["version"] = 1;
    J::Array items; items.reserve(file.items.size()); std::set<std::string> ids;
    for (const auto& v : file.items) { need(ids.insert(uuid(v.id)).second); items.push_back(record(v, rules)); }
    out["items"] = std::move(items); return out;
}
std::string saveShortcutDraft(ShortcutFile& file, const ShortcutCandidate& selected,
    const ShortcutCandidate& verified, std::string name, std::string icon,
    std::optional<std::string> editingID, std::string newID, double now,
    const ShortcutTextRules& rules, const ShortcutSameTarget& same) {
    need(rules.trimmed && rules.validName && same); name = rules.trimmed(name);
    need(J::validUtf8(name) && rules.validName(name), ShortcutErrorCode::invalidName);
    if (editingID) *editingID = uuid(*editingID);
    auto next = file;
    auto found = std::find_if(next.items.begin(), next.items.end(), [&](const auto& v) { return editingID && uuid(v.id) == *editingID; });
    need(!editingID || found != next.items.end(), ShortcutErrorCode::missing);
    locator(selected.locator); locator(verified.locator);
    need(selected.bundleIdentifier == verified.bundleIdentifier && same(selected.locator, verified.locator), ShortcutErrorCode::applicationChanged);
    for (const auto& v : next.items) if ((!editingID || uuid(v.id) != *editingID) && same(v.locator, verified.locator)) throw ShortcutError(ShortcutErrorCode::duplicate);
    ShortcutRecord result = found != next.items.end() ? *found : ShortcutRecord{};
    result.id = found != next.items.end() ? found->id : uuid(std::move(newID));
    result.createdAt = found != next.items.end() ? found->createdAt : now;
    result.name = std::move(name); result.originalName = verified.name; result.bundleIdentifier = verified.bundleIdentifier;
    result.iconPreset = std::move(icon); result.locator = verified.locator; validateShortcut(result, rules);
    const auto id = result.id;
    if (found == next.items.end()) next.items.push_back(std::move(result)); else *found = std::move(result);
    file = std::move(next); return id;
}
bool removeShortcut(ShortcutFile& file, std::string_view id) {
    const auto key = uuid(std::string(id));
    return std::erase_if(file.items, [&](const auto& v) { return uuid(v.id) == key; }) != 0;
}
ShortcutRepository::ShortcutRepository(std::filesystem::path directory, ShortcutTextRules rules)
    : directory_(std::move(directory)), path_(directory_ / "shortcuts.json"), rules_(std::move(rules)) {
    need(directory_.is_absolute() && rules_.validName && rules_.trimmed);
}
void ShortcutRepository::onOwner() {
    if (!owner_) owner_ = std::this_thread::get_id();
    else if (*owner_ != std::this_thread::get_id()) throw std::logic_error("App shortcuts belong to one FIFO executor");
}
ShortcutFile ShortcutRepository::load() {
    onOwner(); if (loaded_) return value_;
    ehud::data::detail::validateRoot(directory_);
    auto bytes = ehud::data::detail::readFile(path_, shortcutMaximumBytes);
    auto next = bytes ? decodeShortcuts(J::parse(*bytes, shortcutMaximumBytes), rules_) : ShortcutFile{};
    value_ = std::move(next); persisted_ = std::move(bytes); loaded_ = true; return value_;
}
void ShortcutRepository::save(const ShortcutFile& value) {
    onOwner(); if (!loaded_) (void)load(); if (value == value_) return;
    auto next = value; std::optional<std::string> bytes;
    try { bytes = encodeShortcuts(next, rules_).encode(shortcutMaximumBytes); }
    catch (const ShortcutError&) { throw; }
    catch (...) { throw ShortcutError(ShortcutErrorCode::capacity); }
    try { ehud::data::detail::replaceFile(path_, persisted_, *bytes, shortcutMaximumBytes); }
    catch (const ehud::data::StoreError& e) {
        if (e.code() == ehud::data::StoreErrorCode::changedOnDisk) throw ShortcutError(ShortcutErrorCode::changedOnDisk);
        throw;
    }
    value_ = std::move(next); persisted_ = std::move(bytes);
}
}
