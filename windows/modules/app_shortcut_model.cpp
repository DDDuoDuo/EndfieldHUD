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
             target["kind"] == J("packagedApp") || target["kind"] == J("internetShortcut"));
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
    out.erase("groupID");
    J::Array paths; for (const auto& path : value.desktopPaths) paths.emplace_back(path);
    out["desktopPaths"] = std::move(paths); out["desktopOnly"] = value.desktopOnly; out["unavailable"] = value.unavailable;
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
    : std::runtime_error(c == ShortcutErrorCode::invalidName ? "Enter a name of 1–128 characters without line breaks"
        : c == ShortcutErrorCode::duplicate ? "This application is already saved. Edit its existing shortcut instead"
        : c == ShortcutErrorCode::missing ? "This app shortcut is no longer available"
        : c == ShortcutErrorCode::applicationChanged ? "The application changed. Choose it again"
        : c == ShortcutErrorCode::newerVersion ? "These shortcuts were saved by a newer version"
        : c == ShortcutErrorCode::changedOnDisk ? "App shortcuts changed outside this window. Restart before editing"
        : c == ShortcutErrorCode::unavailable ? "The application is unavailable"
        : c == ShortcutErrorCode::capacity ? "App shortcut metadata exceeds its storage limit"
        : "The saved app shortcuts could not be read. The original data has been preserved"), code_(c) {}
bool validShortcutIcon(std::string_view s) noexcept {
    constexpr std::array<std::string_view, 14> icons{"original", "bolt", "star", "terminal", "globe",
        "folder", "music", "play", "brush", "code", "game", "camera", "grid", "textBubble"};
    return std::find(icons.begin(), icons.end(), s) != icons.end();
}
bool ShortcutRecord::native() const noexcept { return locator["referencePlatform"] == J("windows"); }
bool ShortcutRecord::operator==(const ShortcutRecord& v) const {
    return std::tie(id, name, originalName, iconPreset, bundleIdentifier, createdAt, locator, originalFields, groupID, desktopPaths, desktopOnly, unavailable) ==
        std::tie(v.id, v.name, v.originalName, v.iconPreset, v.bundleIdentifier, v.createdAt, v.locator, v.originalFields, v.groupID, v.desktopPaths, v.desktopOnly, v.unavailable);
}
void validateShortcut(const ShortcutRecord& v, const ShortcutTextRules& r) {
    need(r.validName && r.trimmed && ehud::data::validUUID(v.id) && std::isfinite(v.createdAt));
    text(v.name); need(r.validName(v.name)); text(v.originalName); need(!v.originalName.empty());
    need(validShortcutIcon(v.iconPreset) && v.originalFields.isObject());
    if (v.bundleIdentifier) text(*v.bundleIdentifier);
    need(v.groupID.empty() || ehud::data::validUUID(v.groupID));
    locator(v.locator);
}
ShortcutFile decodeShortcuts(const J& j, const ShortcutTextRules& rules) {
    try {
        need(j.isObject() && j["version"].isNumber());
        const auto version = j["version"].integer();
        need(version <= 2, ShortcutErrorCode::newerVersion); need((version == 1 || version == 2) && j["items"].isArray());
        ShortcutFile out; out.originalFields = j; out.originalFields.erase("version"); out.originalFields.erase("items");
        std::set<std::string> ids;
        if (j.contains("groups")) {
            need(j["groups"].isArray() && j["groups"].array().size() <= 128);
            for (const auto& group : j["groups"].array()) {
                ShortcutGroup value{uuid(group["id"].string()), group["name"].string()};
                need(rules.validName(value.name) && ids.insert(value.id).second);
                if (group.contains("itemIDs")) for (const auto& id : group["itemIDs"].array()) value.itemIDs.push_back(uuid(id.string()));
                out.groups.push_back(std::move(value));
            }
        }
        out.originalFields.erase("groups");
        if (j.contains("hiddenDesktop")) for (const auto& path : j["hiddenDesktop"].array()) { text(path.string(),32768); out.hiddenDesktop.push_back(path.string()); }
        if (j.contains("activeGroupID")) out.activeGroupID = j["activeGroupID"].string();
        out.originalFields.erase("hiddenDesktop"); out.originalFields.erase("activeGroupID");
        for (const auto& item : j["items"].array()) {
            need(item.isObject()); ShortcutRecord v;
            v.id = uuid(item["id"].string()); v.name = item["name"].string(); v.originalName = item["originalName"].string();
            v.iconPreset = item["iconPreset"].string(); v.createdAt = item["createdAt"].number(); v.originalDate = item["createdAt"];
            if (!item["bundleIdentifier"].isNull()) v.bundleIdentifier = item["bundleIdentifier"].string();
            if (item.contains("groupID")) {
                v.groupID = uuid(item["groupID"].string());
                need(std::any_of(out.groups.begin(), out.groups.end(), [&](const auto& group) { return group.id == v.groupID; }));
            }
            if (item.contains("desktopPaths")) for (const auto& path : item["desktopPaths"].array()) { need(ehud::data::validWindowsFilePath(path.string())); v.desktopPaths.push_back(path.string()); }
            if (item.contains("desktopOnly")) v.desktopOnly = item["desktopOnly"].boolean();
            if (item.contains("unavailable")) v.unavailable = item["unavailable"].boolean();
            v.originalFields = item;
            for (const auto* key : {"groupID","desktopPaths","desktopOnly","unavailable"}) v.originalFields.erase(key);
            for (const auto* key : locatorKeys) {
                if (item.contains(key)) v.locator[key] = item[key];
                v.originalFields.erase(key);
            }
            for (const auto* key : {"id", "name", "originalName", "iconPreset", "createdAt", "bundleIdentifier"}) v.originalFields.erase(key);
            validateShortcut(v, rules); need(ids.insert(v.id).second); out.items.push_back(std::move(v));
        }
        normalizeShortcutGroups(out);
        return out;
    } catch (const ShortcutError&) { throw; }
    catch (...) { throw ShortcutError(ShortcutErrorCode::invalidRecord); }
}
J encodeShortcuts(const ShortcutFile& file, const ShortcutTextRules& rules) {
    need(file.originalFields.isObject()); auto out = file.originalFields; out["version"] = 2;
    out["activeGroupID"] = file.activeGroupID; J::Array hidden; for (const auto& path : file.hiddenDesktop) hidden.emplace_back(path); out["hiddenDesktop"] = std::move(hidden);
    J::Array items; items.reserve(file.items.size()); std::set<std::string> ids;
    J::Array groups;
    need(file.groups.size() <= 128);
    for (const auto& group : file.groups) {
        need(ids.insert(uuid(group.id)).second && rules.validName(group.name));
        J::Array members; std::set<std::string> unique;
        for (const auto& id : group.itemIDs) { need(unique.insert(id).second && std::any_of(file.items.begin(),file.items.end(),[&](const auto& item){return item.id==id;})); members.emplace_back(uuid(id)); }
        groups.push_back(J::Object{{"id", uuid(group.id)}, {"name", group.name}, {"itemIDs",std::move(members)}});
    }
    if (!groups.empty()) out["groups"] = std::move(groups); else out.erase("groups");
    for (const auto& v : file.items) {
        need(v.groupID.empty() || std::any_of(file.groups.begin(), file.groups.end(), [&](const auto& group) { return group.id == v.groupID; }));
        need(ids.insert(uuid(v.id)).second); items.push_back(record(v, rules));
    }
    out["items"] = std::move(items); return out;
}
std::string saveShortcutDraft(ShortcutFile& file, const ShortcutCandidate& selected,
    const ShortcutCandidate& verified, std::string name, std::string icon,
    std::optional<std::string> editingID, std::string newID, double now,
    const ShortcutTextRules& rules, const ShortcutSameTarget& same, std::string groupID) {
    need(rules.trimmed && rules.validName && same); name = rules.trimmed(name);
    need(J::validUtf8(name) && rules.validName(name), ShortcutErrorCode::invalidName);
    if (editingID) *editingID = uuid(*editingID);
    auto next = file;
    auto found = std::find_if(next.items.begin(), next.items.end(), [&](const auto& v) { return editingID && uuid(v.id) == *editingID; });
    need(!editingID || found != next.items.end(), ShortcutErrorCode::missing);

    need(groupID.empty() || std::any_of(next.groups.begin(), next.groups.end(), [&](const auto& group) { return group.id == groupID; }), ShortcutErrorCode::missing);
    locator(selected.locator); locator(verified.locator);
    need(selected.bundleIdentifier == verified.bundleIdentifier && same(selected.locator, verified.locator), ShortcutErrorCode::applicationChanged);
    for (const auto& v : next.items) if ((!editingID || uuid(v.id) != *editingID) && same(v.locator, verified.locator)) {
        if (editingID || groupID.empty()) throw ShortcutError(ShortcutErrorCode::duplicate);
        const auto id = v.id; setShortcutMembership(next,id,groupID,true); file=std::move(next); return id;
    }
    ShortcutRecord result = found != next.items.end() ? *found : ShortcutRecord{};
    result.id = found != next.items.end() ? found->id : uuid(std::move(newID));
    result.createdAt = found != next.items.end() ? found->createdAt : now;
    result.groupID.clear(); result.unavailable = false;
    if(found!=next.items.end() && !same(found->locator,verified.locator)){result.desktopOnly=false;result.desktopPaths.clear();}
    result.name = std::move(name); result.originalName = verified.name; result.bundleIdentifier = verified.bundleIdentifier;
    result.iconPreset = std::move(icon); result.locator = verified.locator; validateShortcut(result, rules);
    const auto id = result.id;
    if (found == next.items.end()) next.items.push_back(std::move(result)); else *found = std::move(result);
    if (!groupID.empty()) setShortcutMembership(next,id,groupID,true);
    file = std::move(next); return id;
}
bool removeShortcut(ShortcutFile& file, std::string_view id) {
    const auto key = uuid(std::string(id));
    for (auto& group : file.groups) std::erase(group.itemIDs,key);
    return std::erase_if(file.items, [&](const auto& v) { return uuid(v.id) == key; }) != 0;
}
bool moveShortcut(ShortcutFile& file, std::string_view id, std::ptrdiff_t index) {
    const auto key = uuid(std::string(id));
    auto& items = file.items;
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& v) { return uuid(v.id) == key; });
    if (found == items.end()) return false;
    const auto from = found - items.begin();
    const auto target = std::clamp<std::ptrdiff_t>(index, 0, std::ptrdiff_t(items.size()) - 1);
    if (target == from) return false;
    auto moved = std::move(*found);
    items.erase(found);
    items.insert(items.begin() + target, std::move(moved));
    return true;
}

bool sameShortcutTarget(const J& a,const J& b) noexcept {
    try {
        if(a["referencePlatform"]!=J("windows") || b["referencePlatform"]!=J("windows")) return a==b;
        const auto& x=a["windowsTarget"]; const auto& y=b["windowsTarget"];
        const auto args=[](const J& t){return t["launch"]["arguments"].isString()?t["launch"]["arguments"].string():std::string{};};
        if(args(x)!=args(y)) return false;
        const auto& kx=x.contains("resolvedApplicationKey")?x["resolvedApplicationKey"]:x["applicationKey"];
        const auto& ky=y.contains("resolvedApplicationKey")?y["resolvedApplicationKey"]:y["applicationKey"];
        return kx.isString() && !kx.string().empty() && kx==ky;
    } catch(...) { return false; }
}
void setShortcutMembership(ShortcutFile& file,std::string_view id,std::string_view groupID,bool add) {
    auto group=std::find_if(file.groups.begin(),file.groups.end(),[&](const auto& g){return g.id==groupID;});
    need(group!=file.groups.end(),ShortcutErrorCode::missing);
    need(std::any_of(file.items.begin(),file.items.end(),[&](const auto& item){return item.id==id;}),ShortcutErrorCode::missing);
    if(add) { if(std::find(group->itemIDs.begin(),group->itemIDs.end(),id)==group->itemIDs.end())group->itemIDs.emplace_back(id); }
    else std::erase(group->itemIDs,id);
}
bool shortcutGrouped(const ShortcutFile& file,std::string_view id) {
    return std::any_of(file.groups.begin(),file.groups.end(),[&](const auto& g){return std::find(g.itemIDs.begin(),g.itemIDs.end(),id)!=g.itemIDs.end();});
}
std::vector<ShortcutRecord> shortcutItems(const ShortcutFile& file,std::string_view groupID) {
    if(groupID.empty()) return file.items;
    std::vector<ShortcutRecord> items;
    for(const auto& group:file.groups)if(group.id==groupID) for(const auto& id:group.itemIDs) {
        const auto found=std::find_if(file.items.begin(),file.items.end(),[&](const auto& item){return item.id==id;});
        if(found!=file.items.end())items.push_back(*found);
    }
    return items;
}
void normalizeShortcutGroups(ShortcutFile& file) {
    // v1 stored separate records per group. Preserve the first identity/name and
    // merge every membership in stored order, without conflating launch arguments.
    std::vector<ShortcutRecord> items;
    for(auto item:file.items) {
        auto existing=std::find_if(items.begin(),items.end(),[&](const auto& v){return sameShortcutTarget(v.locator,item.locator);});
        const auto id=existing==items.end()?item.id:existing->id;
        for(auto& group:file.groups) {
            for(auto& member:group.itemIDs)if(member==item.id)member=id;
            if(group.id==item.groupID && std::find(group.itemIDs.begin(),group.itemIDs.end(),id)==group.itemIDs.end())group.itemIDs.push_back(id);
        }
        if(existing==items.end()){item.groupID.clear();items.push_back(std::move(item));}
        else {
            // A manual record must not become auto-removable when it merges with a desktop import.
            if(existing->desktopOnly && !item.desktopOnly)existing->locator=item.locator;
            existing->desktopOnly=existing->desktopOnly && item.desktopOnly;
            existing->unavailable=existing->unavailable && item.unavailable;
            for(const auto& path:item.desktopPaths)if(std::find(existing->desktopPaths.begin(),existing->desktopPaths.end(),path)==existing->desktopPaths.end())existing->desktopPaths.push_back(path);
        }
    }
    file.items=std::move(items);
    for(auto& group:file.groups) {
        std::set<std::string> seen;
        std::erase_if(group.itemIDs,[&](const auto& id){
            need(std::any_of(file.items.begin(),file.items.end(),[&](const auto& item){return item.id==id;}));
            return !seen.insert(id).second;
        });
    }
    if(!file.activeGroupID.empty() && std::none_of(file.groups.begin(),file.groups.end(),[&](const auto& group){return group.id==file.activeGroupID;}))file.activeGroupID.clear();
}

void mergeDesktopShortcuts(ShortcutFile& file,const std::vector<DesktopShortcut>& snapshot,const ShortcutTextRules& rules) {
    // Work on a copy: any invalid metadata/storage limit leaves the previous snapshot intact.
    auto next=file;
    for(auto& item:next.items)if(item.desktopOnly)item.unavailable=true;
    for(const auto& source:snapshot) {
        if(std::find(next.hiddenDesktop.begin(),next.hiddenDesktop.end(),source.path)!=next.hiddenDesktop.end())continue;
        auto found=std::find_if(next.items.begin(),next.items.end(),[&](const auto& item){
            return std::find(item.desktopPaths.begin(),item.desktopPaths.end(),source.path)!=item.desktopPaths.end();
        });
        if(!source.candidate)continue; // An unreadable link is retained; never infer deletion from an inspection error.
        if(found==next.items.end())found=std::find_if(next.items.begin(),next.items.end(),[&](const auto& item){return sameShortcutTarget(item.locator,source.candidate->locator);});
        if(found==next.items.end()) {
            ShortcutRecord item; item.id=ehud::data::makeUUID(); item.createdAt=ehud::data::foundationNow();
            item.name=rules.trimmed(source.candidate->name); item.originalName=source.candidate->name;
            if(!rules.validName(item.name))continue;
            item.locator=source.candidate->locator; item.desktopOnly=true;
            next.items.push_back(std::move(item)); found=next.items.end()-1;
        }
        if(std::find(found->desktopPaths.begin(),found->desktopPaths.end(),source.path)==found->desktopPaths.end())found->desktopPaths.push_back(source.path);
        if(found->desktopOnly || found->unavailable)found->locator=source.candidate->locator;
        found->unavailable=false;
    }
    std::vector<std::string> removed;
    for(auto& item:next.items) {
        const auto& selected=item.locator["windowsTarget"]["path"];
        const bool desktopTarget=selected.isString() && std::find(item.desktopPaths.begin(),item.desktopPaths.end(),selected.string())!=item.desktopPaths.end();
        std::erase_if(item.desktopPaths,[&](const auto& path){return std::none_of(snapshot.begin(),snapshot.end(),[&](const auto& source){return source.path==path;});});
        if(desktopTarget && item.desktopPaths.empty())item.unavailable=true;
        if(item.desktopOnly && item.desktopPaths.empty()) {
            if(shortcutGrouped(next,item.id))item.unavailable=true; else removed.push_back(item.id);
        }
    }
    for(const auto& id:removed)removeShortcut(next,id);
    normalizeShortcutGroups(next); file=std::move(next);
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
    try {
        if(persisted_ && J::parse(*persisted_,shortcutMaximumBytes)["version"]==J(1)) {
            const auto backup=directory_/"shortcuts-v1-backup.json";
            if(!ehud::data::detail::readFile(backup,shortcutMaximumBytes))ehud::data::detail::replaceFile(backup,std::nullopt,*persisted_,shortcutMaximumBytes);
        }
        ehud::data::detail::replaceFile(path_, persisted_, *bytes, shortcutMaximumBytes);
    }
    catch (const ehud::data::StoreError& e) {
        if (e.code() == ehud::data::StoreErrorCode::changedOnDisk) throw ShortcutError(ShortcutErrorCode::changedOnDisk);
        throw;
    }
    value_ = std::move(next); persisted_ = std::move(bytes);
}
}
