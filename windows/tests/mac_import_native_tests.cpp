#include "mac_import_test_support.hpp"
#include "core/migration/mac_import_native.hpp"
#include "modules/archive_repository.hpp"
#include "native/app_shortcut_text.hpp"
#include "native/archive_text_rules.hpp"
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace {
std::string archiveDatabase() {
    Temporary local;
    {
        mod::ArchiveSQLiteRepository repository(local.root / "Archive", endfield::native::nativeArchiveTextRules());
        mod::ArchiveEntry entry;
        entry.id = uuid(80);
        entry.title = "Rhodes Island \xE6\xA1\xA3\xE6\xA1\x88";
        entry.body = "Body with \xF0\x9F\x9A\x80";
        entry.date = 700000000.5;
        entry.modified = 700000100.25;
        entry.media.push_back(J::Object{{"version", 1}, {"kind", "image"}, {"bookmark", bookmark}, {"isSecurityScoped", true},
            {"lastKnownPath", "/Users/doctor/Pictures/archive.png"}, {"displayName", "archive.png"}, {"pixelWidth", 320}, {"pixelHeight", 200}, {"frameCount", 1}});
        repository.save(entry);
    }
    return read(local.root / "Archive" / "archive.sqlite");
}
std::string shortcutsJSON() {
    return J(J::Object{{"version", 1}, {"items", J::Array{J::Object{{"id", uuid(85)}, {"name", "Terminal"}, {"originalName", "Terminal"}, {"iconPreset", "original"},
        {"createdAt", 700000000.5}, {"bundleIdentifier", "com.apple.Terminal"}, {"bookmark", bookmark}, {"securityScoped", false},
        {"lastKnownPath", "/System/Applications/Utilities/Terminal.app"}}}}}).encode();
}
// Archive user_version 1 exactly as the previous Mac schema stored it: Unix
// seconds in the index columns and the Foundation-epoch JSON document BLOB.
std::string archiveV1() {
    Temporary local;
    const auto rules = endfield::native::nativeArchiveTextRules();
    mod::ArchiveEntry entry;
    entry.id = uuid(81); entry.title = "Legacy"; entry.body = "v1 body"; entry.date = 600000000.5; entry.modified = 600000001.5;
    entry.media.push_back(J::Object{{"version", 1}, {"kind", "image"}, {"bookmark", bookmark}, {"isSecurityScoped", false},
        {"lastKnownPath", "/Users/doctor/old.png"}, {"displayName", "old.png"}, {"pixelWidth", 8}, {"pixelHeight", 8}, {"frameCount", 1}});
    const auto payload = mod::encodeArchiveEntry(entry, rules).encode();
    const auto path = local.root / "archive.sqlite";
    sqlite3* db{};
    check(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK, "Create v1 Archive database");
    exec(db, "CREATE TABLE entries(id TEXT PRIMARY KEY,template TEXT NOT NULL,title TEXT NOT NULL,date REAL NOT NULL,modified REAL NOT NULL,mediaCount INTEGER NOT NULL,payload BLOB NOT NULL);CREATE TABLE state(key TEXT PRIMARY KEY,value TEXT NOT NULL);PRAGMA user_version=1");
    sqlite3_stmt* insert{};
    sqlite3_prepare_v2(db, "INSERT INTO entries VALUES(?,?,?,?,?,?,?)", -1, &insert, nullptr);
    sqlite3_bind_text(insert, 1, entry.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 2, "journal", -1, SQLITE_STATIC);
    sqlite3_bind_text(insert, 3, entry.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(insert, 4, entry.date + mod::archiveFoundationToUnix);
    sqlite3_bind_double(insert, 5, entry.modified + mod::archiveFoundationToUnix);
    sqlite3_bind_int64(insert, 6, 1);
    sqlite3_bind_blob(insert, 7, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT);
    check(sqlite3_step(insert) == SQLITE_DONE, "Insert v1 Archive row");
    sqlite3_finalize(insert);
    sqlite3_close(db);
    return read(path);
}
void archiveMigration() {
    using S = m::MacImportStore;
    Temporary t;
    Export e{t.root / "export"};
    e.add("EndfieldCharge/Archive/archive.sqlite", archiveV1());
    e.sqlite["EndfieldCharge/Archive/archive.sqlite"] = 1;
    e.write();
    const auto destination = t.root / "dest";
    m::MacImportSession session(e.root, destination, m::nativeMacImportPlatform());
    session.stage();
    check(store(session.summary(), S::archive).status == m::MacImportStatus::importedWithWarnings && store(session.summary(), S::archive).relinkItems == 1,
          "Archive v1 validates through the repository migration: " + describe(session.summary()));
    session.commit();
    sqlite3* db{};
    check(sqlite3_open_v2((destination / "Archive" / "archive.sqlite").string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "Open migrated Archive");
    sqlite3_stmt* statement{};
    sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &statement, nullptr);
    check(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 2, "Archive v1 migrated to v2 inside the stage");
    sqlite3_finalize(statement);
    sqlite3_close(db);
    check(read(e.root / "EndfieldCharge" / "Archive" / "archive.sqlite") == e.files["EndfieldCharge/Archive/archive.sqlite"], "The v1 export database is untouched");
    mod::ArchiveSQLiteRepository repository(destination / "Archive", endfield::native::nativeArchiveTextRules());
    check(repository.entry(uuid(81))->date == 600000000.5, "Migrated document keeps its Foundation-epoch date");
}
m::MacImportPlatform nativePlatform() {
    auto p = m::nativeMacImportPlatform();
    p.unixNow = [] { return 1760000000.0; };
    return p;
}
void nativeFullImport() {
    using S = m::MacImportStore;
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.add("EndfieldCharge/Archive/archive.sqlite", archiveDatabase());
    e.sqlite["EndfieldCharge/Archive/archive.sqlite"] = 2;
    e.add("EndfieldCharge/AppShortcuts/shortcuts.json", shortcutsJSON());
    e.write();
    const auto destination = t.root / "EndfieldHUD";
    m::MacImportSession session(e.root, destination, nativePlatform());
    session.stage();
    const auto& s = session.summary();
    check(s.rejected().empty(), "Every store validates with the installed Unicode rules: " + describe(s));
    check(store(s, S::archive).relinkItems == 1 && store(s, S::appShortcuts).relinkItems == 1, "Archive attachment and .app shortcut need relinking");
    check(session.committable(), "Native import is committable without consent");
    session.commit();
    check(read(destination / "Archive" / "archive.sqlite") == e.files["EndfieldCharge/Archive/archive.sqlite"], "Archive database is byte-identical");
    check(read(destination / "AppShortcuts" / "shortcuts.json") == e.files["EndfieldCharge/AppShortcuts/shortcuts.json"], "App shortcuts are byte-identical");
    m::MacRelinkLedger ledger(destination);
    const auto archiveItems = ledger.unresolved(m::relinkArchiveMedia);
    check(archiveItems.size() == 1 && archiveItems[0].recordID == uuid(80) && archiveItems[0].index == std::optional<std::size_t>(0) &&
          archiveItems[0].lastKnownPath == "/Users/doctor/Pictures/archive.png", "Archive relink entry names entry and attachment");
    const auto shortcut = ledger.unresolved(m::relinkAppShortcut);
    check(shortcut.size() == 1 && shortcut[0].macReference["bundleIdentifier"].string() == "com.apple.Terminal", "Shortcut relink keeps bundle identity for display only");
    // Archive relink through the Archive codec and repository.
    mod::ArchiveSQLiteRepository repository(destination / "Archive", endfield::native::nativeArchiveTextRules());
    auto entry = *repository.entry(uuid(80));
    entry.media[0] = m::relinkedArchiveMedia(entry.media[0], {"C:\\Users\\Doctor\\Pictures\\archive.png", "archive.png", "image", 320, 200});
    repository.save(entry);
    check(repository.entry(uuid(80))->media[0]["referencePlatform"].string() == "windows", "Relinked Archive attachment persists");
    // Shortcut relink through the unchanged editor transaction.
    const auto rules = endfield::native::nativeShortcutTextRules();
    mod::ShortcutRepository shortcuts(destination / "AppShortcuts", rules);
    auto file = shortcuts.load();
    const auto before = file.items[0];
    const mod::ShortcutCandidate candidate{"Calculator", std::nullopt, J::Object{{"referencePlatform", "windows"}, {"windowsTarget",
        J::Object{{"kind", "executable"}, {"applicationKey", "c:\\windows\\system32\\calc.exe"}, {"path", "C:\\Windows\\System32\\calc.exe"}}}}};
    const auto same = [](const J& a, const J& b) { return a == b; };
    check(m::relinkShortcut(file, before.id, candidate, candidate, 800000000, rules, same) == before.id, "Relinked shortcut keeps its identifier");
    shortcuts.save(file);
    const auto after = mod::ShortcutRepository(destination / "AppShortcuts", rules).load().items[0];
    check(after.native() && after.name == before.name && after.iconPreset == before.iconPreset && after.createdAt == before.createdAt,
          "Relinked shortcut keeps name, icon preset and date; only the target changed");
    rejects([&] { (void)m::relinkShortcut(file, uuid(999), candidate, candidate, 1, rules, same); }, "Relinking an unknown shortcut fails");
}
void unicodeRules() {
    const auto platform = m::nativeMacImportPlatform();
    // 130 family emoji (each one grapheme of several scalars) keep exactly 128 clusters.
    std::string name;
    for (int i = 0; i < 130; ++i) name += "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    const auto mapped = m::mapMacSettings(P::dictionary({{"hudDisplayUUID", P::string("3f2504e0-4f89-41d3-9a0c-0305e82c3301")}, {"hudDisplayName", P::string(" " + name + "\n")}}), platform.settingsText);
    check(mapped.settings.fields["hudDisplayName"].string().size() == 128 * 18, "Display names keep 128 extended grapheme clusters like Swift prefix(128)");
}
#ifdef _WIN32
struct Handle { HANDLE h{INVALID_HANDLE_VALUE}; ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
// Directory junction made with FSCTL_SET_REPARSE_POINT (no privilege needed).
bool junction(const fs::path& link, const fs::path& target) {
    fs::create_directories(link);
    Handle h;
    h.h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h.h == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + target.wstring(), print = target.wstring();
    const auto subBytes = static_cast<USHORT>(substitute.size() * sizeof(wchar_t)), printBytes = static_cast<USHORT>(print.size() * sizeof(wchar_t));
    std::vector<BYTE> buffer(8 + 8 + subBytes + 2 + printBytes + 2);
    auto put16 = [&](std::size_t at, USHORT v) { std::memcpy(buffer.data() + at, &v, 2); };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(buffer.data(), &tag, 4);
    put16(4, static_cast<USHORT>(buffer.size() - 8));
    put16(8, 0); put16(10, subBytes); put16(12, static_cast<USHORT>(subBytes + 2)); put16(14, printBytes);
    std::memcpy(buffer.data() + 16, substitute.data(), subBytes);
    std::memcpy(buffer.data() + 16 + subBytes + 2, print.data(), printBytes);
    DWORD returned{};
    return DeviceIoControl(h.h, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != FALSE;
}
void windowsFilesystem() {
    using S = m::MacImportStore;
    {   // WIC validates managed images on the worker thread.
        Temporary t;
        write(t.root / "ok.png", png1x1);
        auto broken = png1x1; broken[40] = 'Z'; broken[41] = 'Z';
        write(t.root / "broken.png", broken);
        check(!m::decodeImageWithWIC(t.root / "ok.png"), "WIC decodes a managed PNG");
        check(m::decodeImageWithWIC(t.root / "broken.png").has_value(), "WIC rejects corrupt pixel data");
        auto e = fullExport(t.root / "export");
        e.add("EndfieldCharge/Notes/Images/" + uuid(90) + ".png", broken);
        e.write();
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        m::MacImportSession session(e.root, t.root / "dest", nativePlatform(), options);
        session.stage();
        check(hasWarning(store(session.summary(), S::notes), "does not decode"), "Undecodable managed image is reported, never substituted");
    }
    {   // A file held open without delete sharing blocks activation; rollback is exact.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        const auto destination = t.root / "dest";
        write(destination / "Notes" / "keep.txt", "existing");
        const auto before = tree(destination);
        Handle lock;
        lock.h = CreateFileW((destination / "Notes" / "keep.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        check(lock.h != INVALID_HANDLE_VALUE, "Hold a synthetic lock");
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        m::MacImportSession session(e.root, destination, nativePlatform(), options);
        session.stage();
        try { (void)session.commit(); check(false, "Locked destination must not activate"); }
        catch (const m::MacImportError& error) { check(error.code() == m::MacImportErrorCode::conflict, std::string("A lock reports a retryable conflict: ") + error.what()); }
        CloseHandle(lock.h); lock.h = INVALID_HANDLE_VALUE;
        check(tree(destination) == before && siblingsClean(destination) && backups(destination) == 0, "Locked destination stays exactly as it was");
    }
    {   // Carried Windows data held open for writing at commit: retryable conflict, root unchanged.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        const auto destination = t.root / "dest";
        write(destination / "WindowsOnly" / "busy.bin", "carried at commit");
        const auto before = tree(destination);
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        m::MacImportSession session(e.root, destination, nativePlatform(), options);
        session.stage();
        Handle writer;
        writer.h = CreateFileW((destination / "WindowsOnly" / "busy.bin").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        check(writer.h != INVALID_HANDLE_VALUE, "Hold a synthetic writer on carried data");
        try { (void)session.commit(); check(false, "A busy carried file must stop the commit"); }
        catch (const m::MacImportError& error) { check(error.code() == m::MacImportErrorCode::conflict, std::string("A busy carried file is a retryable conflict: ") + error.what()); }
        CloseHandle(writer.h); writer.h = INVALID_HANDLE_VALUE;
        check(tree(destination) == before && siblingsClean(destination) && backups(destination) == 0 && !session.committed(), "The root stays exactly as it was");
    }
    {   // An export file opened for writing by another program is refused.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        Handle writer;
        writer.h = CreateFileW((e.root / "EndfieldCharge" / "Reader" / "library.json").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        check(writer.h != INVALID_HANDLE_VALUE, "Hold a synthetic writer");
        rejects([&] { m::MacImportSession session(e.root, t.root / "dest", nativePlatform()); session.stage(); }, "A file being written is not imported");
        check(siblingsClean(t.root / "dest") && !fs::exists(t.root / "dest"), "Nothing is left after a sharing violation");
    }
    {   // Junctions are never followed: in the export, in existing data, or as the destination.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        const auto outside = t.root / "outside";
        write(outside / "secret.txt", "outside the data root");
        const auto movedCalendar = t.root / "calendar-elsewhere";
        fs::rename(e.root / "EndfieldCharge" / "Calendar", movedCalendar);
        check(junction(e.root / "EndfieldCharge" / "Calendar", movedCalendar), "Create a synthetic export junction");
        rejects([&] { m::MacImportSession session(e.root, t.root / "dest1", nativePlatform()); session.stage(); }, "Export junction is refused");
        const auto destination = t.root / "dest2";
        write(destination / "settings-placeholder.txt", "x");
        check(junction(destination / "Linked", outside), "Create a synthetic data-root junction");
        Temporary t2;
        auto e2 = fullExport(t2.root / "export");
        e2.write();
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        rejects([&] { m::MacImportSession session(e2.root, destination, nativePlatform(), options); session.stage(); }, "A junction in existing Windows data is refused");
        check(read(outside / "secret.txt") == "outside the data root" && siblingsClean(destination), "Junction target untouched");
        const auto linkedRoot = t.root / "dest3";
        check(junction(linkedRoot, outside), "Create a synthetic destination junction");
        rejects([&] { m::MacImportSession session(e2.root, linkedRoot, nativePlatform(), options); session.stage(); }, "A junction destination is refused");
        RemoveDirectoryW(linkedRoot.c_str());
        RemoveDirectoryW((destination / "Linked").c_str());
        RemoveDirectoryW((e.root / "EndfieldCharge" / "Calendar").c_str());
    }
}
#endif
void golden(const fs::path& fixture) {
    if (!fs::exists(fixture)) throw std::runtime_error("Missing Mac-generated golden export fixture: " + fixture.string());
    const auto document = J::parse(read(fixture), 64 * 1024 * 1024);
    Temporary t;
    const auto root = t.root / "export";
    for (const auto& [path, encoded] : document["files"].object()) {
        const auto bytes = m::base64Decode(encoded.string());
        check(bytes.has_value(), "Golden fixture file decodes");
        write(root / fs::path(path), *bytes);
    }
    const auto destination = t.root / "EndfieldHUD";
    m::MacImportSession session(root, destination, nativePlatform());
    session.stage();
    const auto& s = session.summary();
    const auto& expected = document["expected"];
    for (const auto& [name, status] : expected["stores"].object()) {
        const auto found = std::find_if(s.stores.begin(), s.stores.end(), [&](const auto& r) { return m::macImportStoreName(r.store) == name; });
        check(found != s.stores.end() && m::macImportStatusName(found->status) == status.string(),
              "Golden " + name + " status " + std::string(found == s.stores.end() ? "missing" : m::macImportStatusName(found->status)) + " (" + (found == s.stores.end() ? "" : found->detail) + ")");
    }
    check(s.relink.size() == static_cast<std::size_t>(expected["relinkItems"].integer()), "Golden relink count");
    check(s.profileSyncLocked == expected["profileSyncLocked"].boolean(), "Golden profile sync lock matches HypergryphAccountController.gameSyncActive");
    session.commit();
    for (const auto& path : expected["byteIdentical"].array()) {
        const auto relative = path.string();
        const auto windows = relative.substr(std::string("EndfieldCharge/").size());
        check(read(destination / fs::path(windows)) == *m::base64Decode(document["files"][relative].string()), "Golden byte-identical: " + relative);
    }
    check(J::parse(read(destination / "settings.json"))["hasLaunched"] == J(expected["hasLaunched"].boolean()), "Golden first-run marker in the settings envelope");
    const d::SettingsStore settings(destination);
    for (const auto& [key, value] : expected["settings"].object()) {
        const auto& actual = settings.value().fields[key];
        check(value.isNumber() ? actual.isNumber() && actual.number() == value.number() : actual == value, "Golden setting " + key);
    }
    // Both date epochs: Archive SQL columns hold Unix seconds, its JSON BLOBs and
    // Notes created_at hold Foundation seconds since 2001.
    const auto foundation = expected["foundationDate"].number();
    mod::ArchiveSQLiteRepository archive(destination / "Archive", endfield::native::nativeArchiveTextRules());
    const auto summaries = archive.summaries();
    check(summaries.size() == 1 && summaries[0].date == foundation && archive.entry(summaries[0].id)->date == foundation, "Archive document date is the Mac Foundation-epoch value");
    sqlite3* db{};
    check(sqlite3_open_v2((destination / "Archive" / "archive.sqlite").string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "Open imported Archive read-only");
    sqlite3_stmt* statement{};
    sqlite3_prepare_v2(db, "SELECT date FROM entries", -1, &statement, nullptr);
    check(sqlite3_step(statement) == SQLITE_ROW && std::abs(sqlite3_column_double(statement, 0) - (foundation + 978307200.0)) < 1e-3, "Archive SQL date column is Unix seconds");
    sqlite3_finalize(statement);
    sqlite3_close(db);
    const auto notes = d::NotesStore(destination).notes();
    check(!notes.empty() && notes.front().createdAt == foundation, "Notes created_at keeps the exact Foundation-epoch double");
    check(notes.size() == 5 && std::count_if(notes.begin(), notes.end(), [](const auto& n) { return n.kind == d::NoteKind::drawing; }) == 1 &&
          std::count_if(notes.begin(), notes.end(), [](const auto& n) { return n.richText.has_value(); }) == 1, "Mac drawing (SQL kind text) and UTF-16 rich runs survive");
}
}
int main(int argc, char** argv) {
    try {
        unicodeRules();
        nativeFullImport();
        archiveMigration();
#ifdef _WIN32
        windowsFilesystem();
#endif
        if (argc >= 2) golden(fs::absolute(argv[1]));
        std::cout << "PASS " << checks << " native Mac import checks; synthetic exports only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
