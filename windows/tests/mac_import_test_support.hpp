#pragma once
// Shared synthetic macOS export builder for the import tests. Everything is
// written below a fresh canonical temporary directory and removed afterwards.
#include "app/utility_executor.hpp"
#include "core/data/event_log_store.hpp"
#include "core/data/file_io.hpp"
#include "core/data/map_store.hpp"
#include "core/migration/mac_import.hpp"
#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_service.hpp"
#include "core/migration/mac_import_sha256.hpp"
#include "core/migration/plist.hpp"
#include "modules/reader_repository.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif

namespace m = ehud::migration;
namespace d = ehud::data;
namespace mod = endfield::modules;
namespace fs = std::filesystem;
using J = d::Json;
using P = m::PlistValue;

namespace mac_import_test {
inline unsigned checks{};
inline void check(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> inline std::string rejects(F f, const std::string& message) {
    try { f(); } catch (const std::exception& e) { ++checks; return e.what(); }
    check(false, message);
    return {};
}
struct Temporary {
    fs::path root = fs::canonical(fs::temp_directory_path()) / ("Endfield-mac-import-" + d::makeUUID());
    Temporary() { fs::create_directories(root); }
    ~Temporary() { std::error_code e; fs::remove_all(root, e); }
};
inline void write(const fs::path& p, std::string_view bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream stream(p, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(stream), "Write only explicit temporary fixture files");
}
inline std::string read(const fs::path& p) { return m::files::readFile(p, 512 * 1024 * 1024); }
// Snapshot of a directory tree: relative path -> SHA-256 (directories marked).
inline std::map<std::string, std::string> tree(const fs::path& root) {
    std::map<std::string, std::string> out;
    if (!fs::exists(root)) return out;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto key = fs::relative(entry.path(), root).generic_string();
        out[key] = entry.is_directory() ? std::string("<dir>") : m::Sha256::hex(read(entry.path()));
    }
    return out;
}
inline bool siblingsClean(const fs::path& destination) {
    for (const auto& entry : fs::directory_iterator(destination.parent_path()))
        if (entry.path().filename().string().find(".import-") != std::string::npos) return false;
    return true;
}
inline std::size_t backups(const fs::path& destination) {
    std::size_t n{};
    for (const auto& entry : fs::directory_iterator(destination.parent_path()))
        if (entry.path().filename().string().starts_with(destination.filename().string() + ".backup-")) ++n;
    return n;
}
inline const std::string png1x1 = std::string(
    "\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"
    "\x00\x00\x00\x0aIDATx\x9c\x63\x00\x01\x00\x00\x05\x00\x01\x0d\x0a\x2d\xb4\x00\x00\x00\x00IEND\xae\x42\x60\x82", 67);
inline constexpr const char* bookmark = "Ym9va21hcmstZGF0YQ==";

// ---------- synthetic macOS export ----------
struct Export {
    fs::path root;
    std::map<std::string, std::string> files;
    std::map<std::string, std::int64_t> sqlite;
    std::optional<std::string> preferences;
    J extraManifest{J::Object{}};
    void add(const std::string& path, std::string bytes) { files[path] = std::move(bytes); }
    void write() {
        J::Array rows;
        for (const auto& [path, bytes] : files) {
            mac_import_test::write(root / path, bytes);
            rows.push_back(J::Object{{"path", path}, {"bytes", static_cast<std::int64_t>(bytes.size())}, {"sha256", m::Sha256::hex(bytes)}});
        }
        J::Object databases;
        for (const auto& [path, version] : sqlite) databases[path] = J::Object{{"userVersion", version}};
        J manifest = J::Object{{"format", "EndfieldHUD.macExport"}, {"version", 1},
            {"source", J::Object{{"bundleIdentifier", "io.github.endfieldcharge.EndfieldCharge"}, {"appVersion", "1.2.0"}, {"build", "18"}}},
            {"exportedAt", 1760000000.5}, {"files", std::move(rows)}, {"sqlite", std::move(databases)}};
        if (preferences) manifest["preferences"] = *preferences;
        for (const auto& [key, value] : extraManifest.object()) manifest[key] = value;
        mac_import_test::write(root / "manifest.json", manifest.encode());
    }
};
inline std::string uuid(unsigned n) { const auto s = std::to_string(n); return "00000000-0000-4000-8000-" + std::string(12 - s.size(), '0') + s; }
inline std::string sqliteFile(const fs::path& path) { return read(path); }
inline void exec(sqlite3* db, const char* sql) { char* error{}; if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) { const std::string m = error ? error : "sqlite"; sqlite3_free(error); throw std::runtime_error(m); } }
inline std::string notesDatabase(const fs::path& scratch, int version, bool withMedia = true) {
    Temporary local;
    {
        d::NotesStore store(local.root);
        auto text = d::Note::textNote("Doctor \xE5\x8D\x9A\xE5\xA3\xAB \xF0\x9F\x9A\x80 e\xCC\x81");
        text.id = uuid(1); text.createdAt = 700000000.123456; store.upsert(text);
        d::Note image; image.id = uuid(2); image.kind = d::NoteKind::image; image.imageName = uuid(90) + ".png"; image.createdAt = 700000001; store.upsert(image);
        d::Note missing; missing.id = uuid(3); missing.kind = d::NoteKind::image; missing.imageName = uuid(91) + ".png"; missing.createdAt = 700000002; store.upsert(missing);
        if (withMedia) {
            d::Note media; media.id = uuid(4); media.kind = d::NoteKind::image; media.createdAt = 700000003;
            media.media = J(J::Object{{"version", 1}, {"kind", "image"}, {"bookmark", bookmark}, {"isSecurityScoped", true}, {"lastKnownPath", "/Users/doctor/Pictures/rhodes.png"},
                                      {"displayName", "rhodes.png"}, {"pixelWidth", 640}, {"pixelHeight", 480}, {"frameCount", 1}, {"futureMediaField", "kept"}}).encode();
            store.upsert(media);
        }
    }
    const auto path = local.root / "Notes" / "notes.sqlite3";
    if (version != 2) {
        sqlite3* db{};
        check(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK, "Open synthetic Notes database");
        exec(db, ("PRAGMA user_version=" + std::to_string(version)).c_str());
        sqlite3_close(db);
    }
    (void)scratch;
    return sqliteFile(path);
}
// A valid, empty SQLite database at Archive's schema version; only the
// installed-ICU validator can accept or reject its contents.
inline std::string placeholderArchive() {
    Temporary local;
    const auto path = local.root / "archive.sqlite";
    sqlite3* db{};
    check(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK, "Create placeholder Archive database");
    exec(db, "CREATE TABLE placeholder(value TEXT)");
    exec(db, "PRAGMA user_version=2");
    sqlite3_close(db);
    return sqliteFile(path);
}
inline std::string legacyNotesV1() {
    Temporary local;
    const auto path = local.root / "notes.sqlite3";
    sqlite3* db{};
    check(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK, "Create v1 Notes database");
    exec(db, "CREATE TABLE notes(id TEXT PRIMARY KEY NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('text','todo','image')),text TEXT NOT NULL,checklist TEXT NOT NULL,image_name TEXT,x REAL NOT NULL,y REAL NOT NULL,width REAL NOT NULL,height REAL NOT NULL,z_index INTEGER NOT NULL,created_at REAL NOT NULL,is_pinned INTEGER NOT NULL CHECK(is_pinned IN (0,1)))");
    exec(db, ("INSERT INTO notes VALUES('" + uuid(7) + "','todo','','[{\"id\":\"" + uuid(8) + "\",\"text\":\"Sanity\",\"isChecked\":true}]',NULL,10,20,180,110,0,700000000.5,1)").c_str());
    exec(db, "PRAGMA user_version=1");
    sqlite3_close(db);
    return sqliteFile(path);
}
inline std::string profileJSON(bool images) {
    Temporary local;
    d::ProfileStore store(local.root);
    auto p = store.value();
    p.name = "Doctor\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x94\xAC"; p.tag = "1234"; p.awakeningDate = 600000000.25; p.accumulatedWorkSeconds = 3600;
    p.gamePlayerID = "18446744073709551617"; // above 2^64: must stay a string
    if (images) { p.avatarFilename = uuid(70) + ".image"; p.backgroundFilename = uuid(71) + ".png"; }
    store.update(p);
    auto json = J::parse(read(store.path()));
    auto profile = json["profile"];
    profile["futureProfileField"] = J::parse("9007199254740993");
    json["profile"] = profile;
    return json.encode();
}
inline std::string readerJSON() {
    mod::ReaderLibrary library;
    mod::ReaderBook book;
    book.id = uuid(20); book.title = "\xE7\xBB\x88\xE6\x9C\xAB\xE5\x9C\xB0"; book.bookmarkBase64 = bookmark; book.path = "/Users/doctor/Books/endfield.epub"; book.scoped = true;
    book.location.section = 3; book.location.block = 7; book.location.character = 42; book.progress = .375;
    mod::ReaderBookmark mark; mark.id = uuid(21); mark.location.section = 1; mark.progress = .1; book.bookmarks.push_back(mark);
    library.books.push_back(book);
    library.selected = book.id;
    auto json = mod::encodeReaderLibrary(library);
    json["futureLibraryField"] = J::Array{1, 2};
    return json.encode();
}
inline std::string calendarJSON(int version = 1) {
    return J(J::Object{{"version", version}, {"events", J::Array{J::Object{{"id", uuid(30)}, {"title", "Contingency"}, {"details", "Bring sanity"},
        {"day", J::Object{{"year", 2026}, {"month", 11}, {"day", 3}}}, {"created", J::parse("700000000.000001")}, {"modified", 700000100.5}, {"catchUpPending", false}}}}}).encode();
}
inline std::string mapJSON(int version) {
    mod::MapSnapshot s;
    s.pins = {{uuid(40), .25, .75, -12345.125, mod::MapPinStyle::player}};
    auto json = J::parse(d::encodeMapArchive(s));
    json["version"] = version;
    if (version == 2) json["viewport"]["zoom"] = 24;
    json["futureRoot"] = "kept";
    return json.encode();
}
inline std::string eventsJSON() {
    Temporary local;
    d::EventLogStore store(local.root);
    store.record({uuid(50), mod::EventKind::overlayOpened, 700000000.5, {}}, 1);
    store.record({uuid(51), mod::EventKind::calendarAction, 700000001.5, {{"action", "added"}}}, 2);
    auto save = store.takeSave(3, true);
    check(save && save->execute().success, "Write synthetic event log");
    return read(local.root / "EventLog" / "events.json");
}
inline std::string accountJSON() {
    const std::string endfieldRole = R"({"region":"mainland","game":"endfield","bindingUID":"9007199254740993","roleID":"18446744073709551619","serverID":"1","name":"Doctor#1234","isDefault":true,"isAvailable":true})";
    const auto role = J::parse(endfieldRole);
    std::string selected;
    for (const auto* key : {"region", "game", "bindingUID", "roleID", "serverID"}) {
        const auto part = role[key].string();
        if (!selected.empty()) selected += "|";
        selected += std::to_string(part.size()) + ":" + part;
    }
    return R"({"version":1,"region":"mainland","header":"endfield","syncProfile":true,"syncAvatar":false,"futureCacheField":{"a":1},"records":{"mainland":{"linked":true,"requiresReconnect":false,"roles":[)" + endfieldRole +
        R"(],"selectedRoleID":")" + selected + R"(","snapshots":{")" + selected + R"(":{"role":)" + endfieldRole +
        R"(,"observedAt":700000000.25,"level":60,"stamina":{"current":120,"maximum":240,"fullRecoveryAt":700050000.5},"token":"must-not-transfer"}},"bindingsAt":700000000}}})";
}
inline std::string shelfJSON(int version = 1) {
    return J(J::Object{{"version", version}, {"items", J::Array{J::Object{{"id", uuid(60)}, {"bookmark", bookmark}, {"isSecurityScoped", true},
        {"lastKnownPath", "/Users/doctor/Desktop/report.pdf"}, {"name", "report.pdf"}, {"typeDescription", "PDF document"}, {"byteCount", 1234},
        {"isDirectory", false}, {"createdAt", 700000000.75}, {"identity", J::Object{{"inode", J::parse("18446744073709551615")}, {"device", 16777220}, {"volumeUUID", "ABC"}}}}}}}).encode();
}
inline P preferences(bool customLogo) {
    P::Dictionary root{{"hudSettingsSchemaVersion", P::integer(1)}, {"language", P::string("simplifiedChinese")}, {"theme", P::string("light")},
        {"hudScale", P::real(1.25)}, {"accentHex", P::string("12abef")}, {"launchAtLogin", P::boolean(false)},
        {"summonShortcut", P::data(R"({"keyCode":50,"modifiers":1})")}, {"orbipom.bestScore.v1", P::integer(88)}, {"hasLaunched", P::boolean(true)},
        {"SUEnableAutomaticChecks", P::boolean(true)}, {"NSWindow Frame Settings", P::string("0 0 1 1")}};
    if (customLogo) { root.push_back({"centerLogo", P::string("custom")}); root.push_back({"centerLogoRevision", P::string("9b2d1c3a-1111-4222-8333-444455556666")}); }
    return P::dictionary(std::move(root));
}
inline Export fullExport(const fs::path& root) {
    Export e{root};
    e.add("EndfieldCharge/Notes/notes.sqlite3", notesDatabase(root, 2));
    e.sqlite["EndfieldCharge/Notes/notes.sqlite3"] = 2;
    e.add("EndfieldCharge/Notes/Images/" + uuid(90) + ".png", png1x1);
    e.add("EndfieldCharge/Notes/Images/" + uuid(99) + ".png", png1x1); // orphan
    e.add("EndfieldCharge/Archive/archive.sqlite", placeholderArchive());
    e.add("EndfieldCharge/Profile/profile.json", profileJSON(true));
    e.add("EndfieldCharge/Profile/Images/" + uuid(70) + ".image", std::string("\xFF\xD8\xFF\xE0JFIF-original-bytes", 21));
    e.add("EndfieldCharge/Reader/library.json", readerJSON());
    e.add("EndfieldCharge/Calendar/calendar.json", calendarJSON());
    e.add("EndfieldCharge/WorldMap/map.json", mapJSON(2));
    e.add("EndfieldCharge/EventLog/events.json", eventsJSON());
    e.add("EndfieldCharge/Account/profile-cache.json", accountJSON());
    e.add("EndfieldCharge/FileShelf/shelf.json", shelfJSON());
    e.add("EndfieldCharge/CenterLogo/9B2D1C3A-1111-4222-8333-444455556666.png", png1x1);
    e.add("EndfieldCharge/CenterLogo/00000000-0000-4000-8000-000000000001.png", png1x1);
    e.add("EndfieldCharge/Unknown/future.bin", "future");
    e.add("Preferences/io.github.endfieldcharge.EndfieldCharge.plist", m::encodeBinaryPlist(preferences(true)));
    e.preferences = "Preferences/io.github.endfieldcharge.EndfieldCharge.plist";
    return e;
}
// Portable fixture rules for ASCII/simple test text; native tests use ICU.
inline m::MacImportPlatform platform() {
    m::MacImportPlatform p;
    auto prefix = [](std::string_view text, std::size_t count) {
        std::size_t i = 0, n = 0;
        while (i < text.size() && n < count) { const auto b = static_cast<unsigned char>(text[i]); i += b < 0x80 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : 4; ++n; }
        return std::string(text.substr(0, i));
    };
    p.settingsText.prefix = prefix;
    p.calendarText.characters = [](std::string_view text) { std::size_t n{}; for (const unsigned char c : text) if ((c & 0xc0) != 0x80) ++n; return n; };
    p.calendarText.trimmed = [](std::string_view text) { return m::foundationTrimmed(text); };
    p.calendarText.prefix = prefix;
    p.unixNow = [] { return 1760000000.0; };
    return p;
}
inline const m::MacImportStoreReport& store(const m::MacImportSummary& s, m::MacImportStore which) { return s.store(which); }
inline bool hasWarning(const m::MacImportStoreReport& r, std::string_view text) {
    return std::any_of(r.warnings.begin(), r.warnings.end(), [&](const auto& w) { return w.find(text) != std::string::npos; });
}

}
using namespace mac_import_test;

namespace mac_import_test {
inline std::string describe(const m::MacImportSummary& s) {
    std::string out;
    for (const auto& r : s.stores) out += std::string(m::macImportStoreName(r.store)) + "=" + std::string(m::macImportStatusName(r.status)) + (r.detail.empty() ? "" : "(" + r.detail + ")") + " ";
    return out;
}
}
