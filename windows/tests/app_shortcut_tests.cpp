#include "modules/app_shortcut_model.hpp"
#include "native/app_shortcut_text.hpp"
#include "core/data/file_io.hpp"
#include <fstream>
#include <iostream>

namespace {
namespace m = endfield::modules;
using J = m::ShortcutJson;
unsigned checks{};
void check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(m::ShortcutErrorCode code, F f, const char* why) {
    try { f(); } catch (const m::ShortcutError& e) { check(e.code() == code, why); return; }
    throw std::runtime_error(why);
}
std::string id(unsigned n) {
    char out[40]; std::snprintf(out, sizeof(out), "ABCDEF00-0000-4000-8000-%012u", n); return out;
}
struct Temp {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
        ("Endfield-shortcut-test-" + ehud::data::makeUUID());
    ~Temp() { std::error_code error; std::filesystem::remove_all(root, error); }
};
void write(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path()); std::ofstream f(path, std::ios::binary);
    f << bytes; check(bool(f), "Write only owned synthetic shortcut fixture");
}
std::string read(const std::filesystem::path& path) { return *ehud::data::detail::readFile(path, m::shortcutMaximumBytes); }
J target(std::string key, std::string path = R"(C:\Synthetic\Application.exe)") {
    return J::Object{{"referencePlatform", "windows"}, {"windowsTarget", J::Object{
        {"kind", "executable"}, {"path", path}, {"applicationKey", key}, {"futureIdentity", "kept"}}}};
}
bool same(const J& a, const J& b) {
    if (a["referencePlatform"] != b["referencePlatform"]) return false;
    return a["referencePlatform"] == J("windows") ? a["windowsTarget"]["applicationKey"] == b["windowsTarget"]["applicationKey"] : a["lastKnownPath"] == b["lastKnownPath"];
}
void source(const J& oracle, const m::ShortcutTextRules& rules) {
    for (const auto& entry : oracle["names"].array()) {
        const auto s = entry["text"].string();
        check(rules.validName(s) == entry["valid"].boolean(), "Original Swift shortcut grapheme/control validation");
        check(rules.trimmed(s) == entry["trimmed"].string(), "Original Foundation shortcut name trimming");
    }
    for (const auto& entry : oracle["records"].array()) {
        const auto status = entry["result"].string();
        if (status == "accepted") {
            auto file = m::decodeShortcuts(entry["archive"], rules);
            check(m::decodeShortcuts(m::encodeShortcuts(file, rules), rules) == file, "Original accepted Mac archive round trip");
            for (const auto& item : file.items) check(!item.native(), "Mac bookmarks are never treated as executable Windows references");
        } else rejects(status == "newerVersion" ? m::ShortcutErrorCode::newerVersion : m::ShortcutErrorCode::invalidRecord,
            [&] { (void)m::decodeShortcuts(entry["archive"], rules); }, "Original rejected Mac archive stays rejected");
    }
    check(oracle["icons"].array().size() == 14, "Original fourteen icon IDs");
    for (const auto& icon : oracle["icons"].array()) check(m::validShortcutIcon(icon.string()), "Each original icon ID retained");
    check(!m::validShortcutIcon("future"), "Unknown icon does not silently become a different one");
}
void edits(const J& oracle, const m::ShortcutTextRules& rules) {
    auto file = m::decodeShortcuts(oracle["records"].array()[0]["archive"], rules);
    m::ShortcutCandidate candidate{"Application", {}, target("synthetic-identity")};
    check(m::saveShortcutDraft(file, candidate, candidate, " Application ", "star", {}, id(2), 700, rules, same) == id(2), "Native target is saved after verified inspection");
    check(file.items.size() == 2 && file.items[1].native() && file.items[1].name == "Application", "Save trims draft name and appends");
    auto native = m::encodeShortcuts(file, rules)["items"].array()[1];
    check(!native.contains("bookmark") && !native.contains("securityScoped") && !native.contains("lastKnownPath"), "No fabricated Mac access context in native record");
    auto before = file;
    rejects(m::ShortcutErrorCode::duplicate, [&] { m::saveShortcutDraft(file, candidate, candidate, "Duplicate", "star", {}, id(3), 701, rules, same); }, "Duplicate target rejected regardless of label");
    check(file == before, "Rejected duplicate leaves file model untouched");
    auto changed = candidate; changed.locator = target("replacement");
    rejects(m::ShortcutErrorCode::applicationChanged, [&] { m::saveShortcutDraft(file, candidate, changed, "New", "star", id(2), id(3), 701, rules, same); }, "Replaced candidate target rejected at save");
    check(file == before, "Stale draft rejection preserves metadata");
    m::saveShortcutDraft(file, candidate, candidate, "Edited", "brush", id(2), "ignored", 999, rules, same);
    check(file.items[1].createdAt == 700 && file.items[1].id == id(2) && file.items[1].name == "Edited", "Rename preserves identity and creation time");
    rejects(m::ShortcutErrorCode::invalidName, [&] { m::saveShortcutDraft(file, candidate, candidate, "\n", "star", {}, id(3), 701, rules, same); }, "Blank trimmed name rejected");
    rejects(m::ShortcutErrorCode::missing, [&] { m::saveShortcutDraft(file, candidate, candidate, "New", "star", id(99), id(3), 701, rules, same); }, "A disappeared edited shortcut is not recreated");
    check(!m::removeShortcut(file, id(99)) && m::removeShortcut(file, id(2)) && file.items.size() == 1, "Remove affects references only and missing remove is a no-op");
    auto mixed = native; mixed["bookmark"] = "AQIDBA==";
    rejects(m::ShortcutErrorCode::invalidRecord, [&] { m::decodeShortcuts(J::Object{{"version", 1}, {"items", J::Array{mixed}}}, rules); }, "Mixed Windows and Mac locator is rejected");
    auto packaged = candidate; packaged.locator = J::Object{{"referencePlatform", "windows"}, {"windowsTarget", J::Object{
        {"kind", "packagedApp"}, {"appUserModelID", "Synthetic.Package!App"}, {"applicationKey", "Synthetic.Package!App"}}}};
    m::saveShortcutDraft(file, packaged, packaged, "Packaged", "original", {}, id(3), 800, rules, same);
    check(file.items.back().native(), "Explicit packaged app identity can be retained without file path forgery");
}
void persistence(const J& oracle, const m::ShortcutTextRules& rules) {
    Temp temp; m::ShortcutRepository store(temp.root, rules);
    check(!std::filesystem::exists(temp.root), "Shortcut construction is IO-free");
    check(store.load().items.empty() && !std::filesystem::exists(temp.root), "Empty read creates no file");
    auto archive = oracle["records"].array()[0]["archive"];
    archive["futureRoot"] = J::parse("9007199254740993"); auto rows = archive["items"].array();
    rows[0]["createdAt"] = J::parse("800000000.000000000001"); rows[0]["futureItem"] = "unchanged"; archive["items"] = rows;
    auto file = m::decodeShortcuts(archive, rules); store.save(file);
    const auto saved = read(store.path()); m::ShortcutRepository reload(temp.root, rules); auto edited = reload.load();
    check(edited == file, "Persisted Mac metadata remains readable without resolving or opening its path");
    edited.items[0].name = "Edited"; reload.save(edited);
    const auto decoded = J::parse(read(store.path()));
    check(decoded["futureRoot"] == archive["futureRoot"] && decoded["items"].array()[0]["futureItem"] == J("unchanged") &&
        decoded["items"].array()[0]["createdAt"] == rows[0]["createdAt"] && decoded["items"].array()[0]["bookmark"] == rows[0]["bookmark"], "Unknown fields, numeric date token and opaque Mac bookmark survive rename");
    write(store.path(), saved); edited.items[0].name = "Conflict";
    rejects(m::ShortcutErrorCode::changedOnDisk, [&] { reload.save(edited); }, "External writes reject replacement atomically");
    check(read(store.path()) == saved, "Failed save preserves external file bytes");
    write(store.path(), "{\"version\":2,\"items\":[]}");
    rejects(m::ShortcutErrorCode::newerVersion, [&] { m::ShortcutRepository future(temp.root, rules); future.load(); }, "Future archive preserved without repair/write");
    check(read(store.path()) == "{\"version\":2,\"items\":[]}", "Future-version file remains byte-identical");
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected source fixture path");
        const auto oracle = J::parse(read(std::filesystem::u8path(argv[1])));
        const auto rules = endfield::native::nativeShortcutTextRules();
        source(oracle, rules); edits(oracle, rules); persistence(oracle, rules);
        std::cout << "Shortcut model/store passed " << checks << " checks\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << checks << ": " << e.what() << '\n'; return 1; }
}
